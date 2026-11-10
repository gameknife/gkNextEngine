package packager

import (
	"archive/tar"
	"archive/zip"
	"compress/gzip"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

func writeServerArchive(path string, entries []entry, platform string) (retErr error) {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	file, err := os.CreateTemp(filepath.Dir(path), ".nextdayz-archive-*")
	if err != nil {
		return err
	}
	defer func() { file.Close(); os.Remove(file.Name()) }()
	sort.Slice(entries, func(i, j int) bool { return entries[i].name < entries[j].name })
	seen := map[string]bool{}
	var zipOut *zip.Writer
	var tarOut *tar.Writer
	var gzipOut *gzip.Writer
	switch platform {
	case "windows":
		zipOut = zip.NewWriter(file)
	case "linux":
		gzipOut = gzip.NewWriter(file)
		tarOut = tar.NewWriter(gzipOut)
	default:
		return fmt.Errorf("unsupported server archive platform %q", platform)
	}
	for _, e := range entries {
		name := filepath.ToSlash(e.name)
		if seen[name] || strings.HasPrefix(name, "/") || strings.Contains(name, "../") {
			return fmt.Errorf("invalid/duplicate archive entry %s", name)
		}
		seen[name] = true
		info, err := os.Stat(e.source)
		if err != nil {
			return err
		}
		var out io.Writer
		if zipOut != nil {
			header, err := zip.FileInfoHeader(info)
			if err != nil {
				return err
			}
			header.Name, header.Method = name, zip.Deflate
			out, err = zipOut.CreateHeader(header)
			if err != nil {
				return err
			}
		} else {
			header, err := tar.FileInfoHeader(info, "")
			if err != nil {
				return err
			}
			header.Name = name
			if err := tarOut.WriteHeader(header); err != nil {
				return err
			}
			out = tarOut
		}
		in, err := os.Open(e.source)
		if err != nil {
			return err
		}
		_, copyErr := io.Copy(out, in)
		closeErr := in.Close()
		if copyErr != nil {
			return copyErr
		}
		if closeErr != nil {
			return closeErr
		}
	}
	if zipOut != nil {
		if err := zipOut.Close(); err != nil {
			return err
		}
	}
	if tarOut != nil {
		if err := tarOut.Close(); err != nil {
			return err
		}
		if err := gzipOut.Close(); err != nil {
			return err
		}
	}
	if err := file.Close(); err != nil {
		return err
	}
	return os.Rename(file.Name(), path)
}
