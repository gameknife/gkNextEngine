package packager

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/dzmaster"
)

// StageNextDayzWeb assembles the exact production tree consumed by Master.
// Only files in the current bake manifest are carried into the server bundle.
func StageNextDayzWeb(output, dest string) error {
	sources := map[string]string{
		"ndcore/ndcore.mjs":         filepath.Join(output, "core", "ndcore.mjs"),
		"ndcore/ndcore.wasm":        filepath.Join(output, "core", "ndcore.wasm"),
		"game-assets/manifest.json": filepath.Join(output, "assets", "manifest.json"),
	}
	var baked struct {
		Protocol    uint16
		KernelBuild string
		Maps, Rigs  map[string]webAssets
		Shared      webAssets
	}
	manifestData, err := os.ReadFile(sources["game-assets/manifest.json"])
	if err != nil {
		return fmt.Errorf("read Web assets (run gnb web bake): %w", err)
	}
	if err := json.Unmarshal(manifestData, &baked); err != nil {
		return err
	}
	if baked.Protocol == 0 || len(baked.Maps) == 0 || len(baked.Rigs) == 0 || len(baked.Shared.Files) == 0 {
		return fmt.Errorf("incomplete Web asset manifest")
	}
	bundles := []webAssets{baked.Shared}
	for _, group := range []map[string]webAssets{baked.Maps, baked.Rigs} {
		for _, bundle := range group {
			bundles = append(bundles, bundle)
		}
	}
	for _, bundle := range bundles {
		for _, file := range bundle.Files {
			if !fs.ValidPath(file.Path) || strings.ContainsAny(file.Path, "\\:") || file.Path == "manifest.json" {
				return fmt.Errorf("invalid Web asset path %q", file.Path)
			}
			sources["game-assets/"+file.Path] = filepath.Join(output, "assets", filepath.FromSlash(file.Path))
			if file.GzipBytes > 0 {
				sources["game-assets/"+file.Path+".gz"] = filepath.Join(output, "assets", filepath.FromSlash(file.Path)+".gz")
			}
		}
	}

	site := filepath.Join(output, "site")
	index, err := os.ReadFile(filepath.Join(site, "index.html"))
	if err != nil {
		return fmt.Errorf("read Web client (run gnb web build): %w", err)
	}
	if strings.Count(string(index), dzmaster.WebBuildMarker) != 1 {
		return fmt.Errorf("Web client index is missing its release marker")
	}
	err = filepath.WalkDir(filepath.Join(site, "assets"), func(path string, item fs.DirEntry, err error) error {
		if err != nil || item.IsDir() {
			return err
		}
		if !item.Type().IsRegular() {
			return fmt.Errorf("unexpected Web client asset: %s", path)
		}
		rel, err := filepath.Rel(site, path)
		if err != nil {
			return err
		}
		sources["site/"+filepath.ToSlash(rel)] = path
		return nil
	})
	if err != nil {
		return err
	}

	names := make([]string, 0, len(sources))
	for name := range sources {
		names = append(names, name)
	}
	slices.Sort(names)
	build := sha256.New()
	fmt.Fprintf(build, "index.html\n%x\n", sha256.Sum256(index))
	for _, name := range names {
		destination := filepath.Join(dest, filepath.FromSlash(name))
		if err := copyServerFile(sources[name], destination, 0o644); err != nil {
			return fmt.Errorf("stage Web file %s: %w", name, err)
		}
		if name == "ndcore/ndcore.wasm" || name == "ndcore/ndcore.mjs" || strings.HasPrefix(name, "site/") {
			file, err := os.Open(destination)
			if err != nil {
				return err
			}
			hash := sha256.New()
			_, err = io.Copy(hash, file)
			file.Close()
			if err != nil {
				return err
			}
			sum := hash.Sum(nil)
			if name == "ndcore/ndcore.wasm" && hex.EncodeToString(sum) != baked.KernelBuild {
				return fmt.Errorf("Web asset manifest and WASM build differ (run gnb web build)")
			}
			fmt.Fprintf(build, "%s\n%x\n", name, sum)
		}
	}
	fmt.Fprintf(build, "manifest.json\n%x\n", sha256.Sum256(manifestData))
	release := dzmaster.WebManifest{Build: hex.EncodeToString(build.Sum(nil)), Protocol: baked.Protocol, Assets: dzmaster.WebAssetManifest}
	data, err := json.Marshal(release)
	if err != nil {
		return err
	}
	html := strings.Replace(string(index), dzmaster.WebBuildMarker, release.Build, 1)
	if err := os.MkdirAll(filepath.Join(dest, "site"), 0o755); err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(dest, "site", "index.html"), []byte(html), 0o644); err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(dest, "release.json"), data, 0o644)
}

type webAssets struct {
	Files map[string]struct {
		Path      string
		GzipBytes int64
	}
}
