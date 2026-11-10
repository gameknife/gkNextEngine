package packager

import (
	"archive/tar"
	"archive/zip"
	"bytes"
	"compress/gzip"
	"debug/pe"
	"encoding/binary"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func TestLinuxServerLibrariesCollectOnlyActualDependencies(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("requires the native ELF loader")
	}
	cc, err := exec.LookPath("gcc")
	if err != nil {
		t.Skip("gcc unavailable")
	}
	root, stage := t.TempDir(), t.TempDir()
	libSource := filepath.Join(root, "fixture.c")
	mainSource := filepath.Join(root, "main.c")
	for path, content := range map[string]string{libSource: "int fixture(void) { return 7; }", mainSource: "extern int fixture(void); int main(void) { return fixture(); }"} {
		if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	library := filepath.Join(root, "libserverfixture.so")
	exe := filepath.Join(root, "NextDayz")
	for _, args := range [][]string{{"-shared", "-fPIC", "-Wl,-soname,libserverfixture.so", "-o", library, libSource},
		{"-o", exe, mainSource, "-L" + root, "-lserverfixture", "-Wl,-rpath," + root}} {
		if out, err := exec.Command(cc, args...).CombinedOutput(); err != nil {
			t.Fatalf("compile fixture: %v: %s", err, out)
		}
	}
	if err := os.WriteFile(filepath.Join(root, "unrelated.so"), []byte("unrelated"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := collectServerLibraries(exe, stage); err != nil {
		t.Fatal(err)
	}
	files, err := os.ReadDir(filepath.Join(stage, "bin"))
	if err != nil {
		t.Fatal(err)
	}
	if len(files) != 1 || files[0].Name() != "libserverfixture.so" {
		t.Fatalf("unexpected libraries: %v", files)
	}
	if err := os.Remove(library); err != nil {
		t.Fatal(err)
	}
	if err := collectServerLibraries(exe, stage); err == nil {
		t.Fatal("accepted unresolved ELF dependency")
	}
}

func TestServerPEImportsReadsDescriptorsAndDelayImports(t *testing.T) {
	data := make([]byte, 256)
	binary.LittleEndian.PutUint32(data[12:], 0x1080)
	copy(data[128:], "vendor.dll\x00")
	binary.LittleEndian.PutUint32(data[64:], 1)
	binary.LittleEndian.PutUint32(data[68:], 0x1090)
	copy(data[144:], "delay.dll\x00")
	header := &pe.OptionalHeader64{Magic: 0x20b, NumberOfRvaAndSizes: 16}
	header.DataDirectory[pe.IMAGE_DIRECTORY_ENTRY_IMPORT] = pe.DataDirectory{VirtualAddress: 0x1000, Size: 40}
	header.DataDirectory[pe.IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT] = pe.DataDirectory{VirtualAddress: 0x1040, Size: 64}
	var encoded bytes.Buffer
	dos := make([]byte, 64)
	copy(dos, "MZ")
	binary.LittleEndian.PutUint32(dos[60:], 64)
	encoded.Write(dos)
	encoded.WriteString("PE\x00\x00")
	for _, value := range []any{pe.FileHeader{Machine: pe.IMAGE_FILE_MACHINE_AMD64, NumberOfSections: 1, SizeOfOptionalHeader: 240}, header,
		pe.SectionHeader32{VirtualAddress: 0x1000, VirtualSize: 256, SizeOfRawData: 256, PointerToRawData: 368}} {
		if err := binary.Write(&encoded, binary.LittleEndian, value); err != nil {
			t.Fatal(err)
		}
	}
	encoded.Write(data)
	file, err := pe.NewFile(bytes.NewReader(encoded.Bytes()))
	if err != nil {
		t.Fatal(err)
	}
	names, err := serverPEImports(file)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Join(names, ",") != "vendor.dll,delay.dll" {
		t.Fatalf("imports = %v", names)
	}
	binary.LittleEndian.PutUint32(encoded.Bytes()[368+12:], 0xffff0000)
	if _, err := serverPEImports(file); err == nil {
		t.Fatal("accepted invalid import RVA")
	}
}

func TestNextDayzAssetsPublishedDependencyClosure(t *testing.T) {
	root := t.TempDir()
	write := func(name, content string) {
		t.Helper()
		path := filepath.Join(root, filepath.FromSlash(name))
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("assets/configs/nextdayz_maps.json", `{"maps":[{"id":"a","scene":"assets/scad/published/scene.scad","status":"published"},{"id":"old","scene":"assets/scad/missing.scad","status":"draft"}]}`)
	write("assets/scad/published/scene.scad", "// use <missing.scad>\n/* include <missing2.scad> */\ninclude <lib.scad>\nx=\"assets/scad/published/terrain.hmap\";")
	write("assets/scad/published/lib.scad", "use <scene.scad>\n") // dependency cycle
	write("assets/scad/characters/nextdayz_survivor.scad", "use <../lib/rig.scad>\n")
	write("assets/scad/lib/rig.scad", "cube(1);")
	write("out/build/test/assets/shaders/test.comp.slang.spv", "shader")
	write("assets/scad/scenes/nextdayz_helicopter_lobby.scad", "use <../lib/cabin.scad>")
	write("assets/scad/lib/cabin.scad", "cube(1);")
	write("assets/sounds/nextdayz/shot_rifle_1.wav", "sound")
	write("assets/sounds/nextdayz/manifest.json", "metadata")
	write("assets/locale/en.txt", "locale")
	write("assets/brand/gknext_logo_icon_200.png", "icon")
	names, err := nextDayzAssets(root, filepath.Join(root, "out", "build", "test"), false)
	if err != nil {
		t.Fatal(err)
	}
	joined := "\n" + strings.Join(names, "\n") + "\n"
	for _, name := range []string{"assets/scad/published/scene.scad", "assets/scad/published/lib.scad", "assets/scad/lib/rig.scad", "assets/scad/published/terrain.hmap", "assets/shaders/test.comp.slang.spv"} {
		if !strings.Contains(joined, "\n"+name+"\n") {
			t.Fatalf("missing transitive dependency %s", name)
		}
	}
	if strings.Contains(joined, "missing") || strings.Contains(joined, "cvar_user") {
		t.Fatalf("unwanted assets: %v", names)
	}
	for _, name := range []string{"assets/scad/scenes/nextdayz_helicopter_lobby.scad", "assets/sounds/nextdayz/shot_rifle_1.wav", "assets/configs/nextdayz/world_land.json"} {
		if strings.Contains(joined, name) {
			t.Fatalf("server unexpectedly includes client asset: %s", name)
		}
	}
	client, err := nextDayzAssets(root, filepath.Join(root, "out", "build", "test"), true)
	if err != nil {
		t.Fatal(err)
	}
	clientList := "\n" + strings.Join(client, "\n") + "\n"
	for _, name := range []string{"assets/scad/scenes/nextdayz_helicopter_lobby.scad", "assets/scad/lib/cabin.scad", "assets/sounds/nextdayz/shot_rifle_1.wav", "assets/configs/nextdayz/world_land.json", "assets/configs/nextdayz_servers.json", "assets/locale/en.txt", "assets/brand/gknext_logo_icon_200.png"} {
		if !strings.Contains(clientList, "\n"+name+"\n") {
			t.Fatalf("client missing %s", name)
		}
	}
	if strings.Contains(clientList, "manifest.json") {
		t.Fatal("included unused audio metadata")
	}
	write("assets/scad/published/lib.scad", "use <../../../../outside.scad>")
	if _, err := nextDayzAssets(root, filepath.Join(root, "out", "build", "test"), false); err == nil {
		t.Fatal("accepted dependency escaping assets/")
	}
}

func TestNextDayzClientMasterConfiguration(t *testing.T) {
	for _, input := range []string{"https://game.example.com:7780", "http://host:0", "http://host:65536", "http://user:password@host", "http://host/api", "http://host?token=secret"} {
		if _, err := normalizeNextDayzMasterURL(input); err == nil {
			t.Fatalf("accepted %q", input)
		}
	}
	root := t.TempDir()
	if err := writeNextDayzClientServers(root, " game.example.com:7780/ "); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(root, "assets", "configs", "nextdayz_servers.json")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(data), `"url": "http://game.example.com:7780"`) {
		t.Fatalf("bad directory: %s", data)
	}
	if err := writeNextDayzClientServers(root, ""); err != nil {
		t.Fatal(err)
	}
	preserved, _ := os.ReadFile(path)
	if !bytes.Equal(data, preserved) {
		t.Fatal("overwrote configured servers")
	}
	if err := writeNextDayzClientServers(t.TempDir(), ""); err != nil {
		t.Fatalf("default directory: %v", err)
	}
}

func TestNextDayzArchivesPreservePayloadAndExecutableMode(t *testing.T) {
	for _, variant := range []string{"windows", "linux"} {
		t.Run(variant, func(t *testing.T) {
			root := t.TempDir()
			source := filepath.Join(root, "gnb")
			if err := os.WriteFile(source, []byte("payload"), 0o755); err != nil {
				t.Fatal(err)
			}
			archive := filepath.Join(root, "package")
			if err := writeServerArchive(archive, []entry{{source: source, name: "bin/gnb"}}, variant); err != nil {
				t.Fatal(err)
			}
			var reader io.Reader
			if variant == "windows" {
				z, err := zip.OpenReader(archive)
				if err != nil {
					t.Fatal(err)
				}
				defer z.Close()
				if len(z.File) != 1 || z.File[0].Name != "bin/gnb" {
					t.Fatal("wrong zip layout")
				}
				f, err := z.File[0].Open()
				if err != nil {
					t.Fatal(err)
				}
				defer f.Close()
				reader = f
			} else {
				f, err := os.Open(archive)
				if err != nil {
					t.Fatal(err)
				}
				defer f.Close()
				gz, err := gzip.NewReader(f)
				if err != nil {
					t.Fatal(err)
				}
				defer gz.Close()
				tr := tar.NewReader(gz)
				header, err := tr.Next()
				if err != nil {
					t.Fatal(err)
				}
				if header.Name != "bin/gnb" {
					t.Fatal("wrong tar layout")
				}
				// Windows does not retain POSIX execute bits on a source file.
				info, _ := os.Stat(source)
				if header.Mode != int64(info.Mode().Perm()) {
					t.Fatal("lost mode")
				}
				reader = tr
			}
			data, err := io.ReadAll(reader)
			if err != nil || string(data) != "payload" {
				t.Fatalf("archive payload: %q, %v", data, err)
			}
		})
	}
}
