package packager

import (
	"bytes"
	"debug/pe"
	"encoding/binary"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

// Collect the actual loader dependency closure, rather than every DLL/SO left
// beside executables by unrelated applications, .NET or GPU upscalers.
func collectServerLibraries(exe, stage string) error {
	if runtime.GOOS == "windows" {
		return collectServerPELibraries(exe, stage)
	}
	cmd := exec.Command("ldd", exe)
	cmd.Env = append(os.Environ(), "LC_ALL=C")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("resolve DS libraries with ldd: %w: %s", err, output)
	}
	for _, line := range strings.Split(string(output), "\n") {
		fields := strings.Fields(line)
		if len(fields) < 3 || fields[1] != "=>" {
			continue
		}
		if fields[2] == "not" {
			return fmt.Errorf("unresolved DS library: %s", strings.TrimSpace(line))
		}
		if !filepath.IsAbs(fields[2]) || serverSystemELF(fields[0]) {
			continue
		}
		if err := copyServerFile(fields[2], filepath.Join(stage, "bin", fields[0]), 0o755); err != nil {
			return err
		}
	}
	return nil
}

func serverSystemELF(name string) bool {
	for _, prefix := range []string{"libc.so.", "libm.so.", "libpthread.so.", "libdl.so.", "librt.so.", "libresolv.so.", "libutil.so.", "ld-linux"} {
		if strings.HasPrefix(name, prefix) {
			return true
		}
	}
	return false
}

func collectServerPELibraries(exe, stage string) error {
	binDir := filepath.Dir(exe)
	items, err := os.ReadDir(binDir)
	if err != nil {
		return err
	}
	local := map[string]string{}
	for _, item := range items {
		if !item.IsDir() {
			local[strings.ToLower(item.Name())] = item.Name()
		}
	}
	visited := map[string]bool{}
	var visit func(string) error
	visit = func(path string) error {
		key := strings.ToLower(filepath.Base(path))
		if visited[key] {
			return nil
		}
		visited[key] = true
		file, err := pe.Open(path)
		if err != nil {
			return err
		}
		if file.Machine != pe.IMAGE_FILE_MACHINE_AMD64 {
			file.Close()
			return fmt.Errorf("expected x64 PE: %s", path)
		}
		imports, err := serverPEImports(file)
		file.Close()
		if err != nil {
			return err
		}
		for _, dep := range imports {
			lower := strings.ToLower(dep)
			if name, ok := local[lower]; ok {
				source := filepath.Join(binDir, name)
				if err := copyServerFile(source, filepath.Join(stage, "bin", name), 0o755); err != nil {
					return err
				}
				if err := visit(source); err != nil {
					return err
				}
				continue
			}
			// Windows API sets are virtual DLLs; Vulkan comes from the driver.
			if strings.HasPrefix(lower, "api-ms-") || strings.HasPrefix(lower, "ext-ms-") || lower == "vulkan-1.dll" {
				continue
			}
			if strings.HasPrefix(lower, "msvcp") || strings.HasPrefix(lower, "vcruntime") {
				// The engine uses static CRT, but linked vendor DLLs may use the
				// redistributable dynamic CRT. Do not assume the target has it.
				source := filepath.Join(os.Getenv("SystemRoot"), "System32", dep)
				if err := copyServerFile(source, filepath.Join(stage, "bin", dep), 0o755); err != nil {
					return err
				}
				if err := visit(source); err != nil {
					return err
				}
				continue
			}
			if _, err := os.Stat(filepath.Join(os.Getenv("SystemRoot"), "System32", dep)); err != nil {
				return fmt.Errorf("required DS library %s is missing from %s", dep, binDir)
			}
		}
		return nil
	}
	return visit(exe)
}

// debug/pe.ImportedLibraries is a TODO in Go. Read descriptor names directly;
// this also works for DLLs importing by ordinal without any named symbols.
func serverPEImports(file *pe.File) ([]string, error) {
	header, ok := file.OptionalHeader.(*pe.OptionalHeader64)
	if !ok {
		return nil, fmt.Errorf("expected PE32+ optional header")
	}
	rvaData := func(rva uint32) ([]byte, error) {
		for _, section := range file.Sections {
			if rva < section.VirtualAddress {
				continue
			}
			offset := rva - section.VirtualAddress
			if offset >= section.Size {
				continue
			}
			data, err := section.Data()
			if err != nil {
				return nil, err
			}
			if uint64(offset) < uint64(len(data)) {
				return data[offset:], nil
			}
		}
		return nil, fmt.Errorf("PE import RVA %#x outside file sections", rva)
	}
	var names []string
	for _, directory := range []struct{ index, stride, nameOffset int }{{pe.IMAGE_DIRECTORY_ENTRY_IMPORT, 20, 12}, {pe.IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT, 32, 4}} {
		if int(header.NumberOfRvaAndSizes) <= directory.index {
			continue
		}
		dd := header.DataDirectory[directory.index]
		if dd.VirtualAddress == 0 || dd.Size == 0 {
			continue
		}
		data, err := rvaData(dd.VirtualAddress)
		if err != nil {
			return nil, err
		}
		if uint64(dd.Size) < uint64(len(data)) {
			data = data[:dd.Size]
		}
		for len(data) >= directory.stride {
			rva := binary.LittleEndian.Uint32(data[directory.nameOffset:])
			if rva == 0 {
				break
			}
			if directory.index == pe.IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT && binary.LittleEndian.Uint32(data)&1 == 0 {
				return nil, fmt.Errorf("unsupported VA-based delay imports")
			}
			name, err := rvaData(rva)
			if err != nil {
				return nil, err
			}
			end := bytes.IndexByte(name, 0)
			if end < 0 {
				return nil, fmt.Errorf("unterminated PE import name")
			}
			names = append(names, string(name[:end]))
			data = data[directory.stride:]
		}
	}
	return names, nil
}
