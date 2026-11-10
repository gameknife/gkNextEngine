package packager

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/dzmaster"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

type NextDayzOptions struct {
	Version, Output, MasterURL string
	Client                     bool
}

// PackageNextDayz never launches Vulkan. Published SCAD maps and their transitive
// dependencies supply a deterministic asset list on GPU-less build workers too.
func PackageNextDayz(repoRoot, preset string, opts NextDayzOptions) error {
	if runtime.GOARCH != "amd64" || (runtime.GOOS != "windows" && runtime.GOOS != "linux") {
		return fmt.Errorf("NextDayz bundles support Windows/Linux amd64 only")
	}
	kind := "server"
	if opts.Client {
		kind = "client"
	}
	version := fallbackVersion(opts.Version)
	if !regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9._-]*$`).MatchString(version) {
		return fmt.Errorf("invalid package version %q", version)
	}
	buildRoot := filepath.Join(repoRoot, "out", "build", preset)
	stage, err := os.MkdirTemp(buildRoot, "nextdayz-package-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(stage)
	binDir := filepath.Join(buildRoot, "bin")
	exe := platform.ExecutablePath(binDir, "NextDayz")
	if err := copyServerFile(exe, platform.ExecutablePath(filepath.Join(stage, "bin"), "NextDayz"), 0o755); err != nil {
		return err
	}
	if err := collectServerLibraries(exe, stage); err != nil {
		return err
	}

	if opts.Client {
		if err := collectNextDayzClientPlugins(binDir, stage); err != nil {
			return err
		}
	} else {
		// Build the CLI from this checkout, without its optional desktop dashboard.
		// Copying the invoking executable could silently ship a stale bootstrap CLI.
		gnb := exec.Command("go", "build", "-trimpath", "-ldflags", "-s -w -X main.version="+version,
			"-o", platform.ExecutablePath(filepath.Join(stage, "bin"), "gnb"), "./cmd/gnb")
		gnb.Dir = filepath.Join(repoRoot, "tools", "gnb")
		gnb.Stdout, gnb.Stderr = os.Stdout, os.Stderr
		if err := gnb.Run(); err != nil {
			return fmt.Errorf("build packaged gnb: %w", err)
		}
	}

	assets, err := nextDayzAssets(repoRoot, buildRoot, opts.Client)
	if err != nil {
		return err
	}
	// Source assets come from the checkout; shader binaries come from this build.
	// Never let an old build-tree SCAD copy override current published content.
	for _, name := range assets {
		source := filepath.Join(repoRoot, filepath.FromSlash(name))
		if strings.HasSuffix(name, ".spv") {
			source = filepath.Join(buildRoot, filepath.FromSlash(name))
		}
		if _, err := os.Stat(source); os.IsNotExist(err) && strings.HasPrefix(name, "assets/textures/") {
			continue // Packager resolves these HDRs from optional.pak below.
		}
		if opts.Client && name == "assets/configs/nextdayz_servers.json" {
			if _, err := os.Stat(source); os.IsNotExist(err) {
				continue
			}
		}
		if err := copyServerFile(source, filepath.Join(stage, filepath.FromSlash(name)), 0o644); err != nil {
			return err
		}
	}
	if opts.Client {
		if err := writeNextDayzClientServers(stage, opts.MasterURL); err != nil {
			return err
		}
	}
	pakDir := filepath.Join(stage, "assets", "paks")
	if err := os.MkdirAll(pakDir, 0o755); err != nil {
		return err
	}
	needsOptional := false
	for _, name := range assets {
		if strings.HasPrefix(name, "assets/textures/") {
			if _, err := os.Stat(filepath.Join(stage, filepath.FromSlash(name))); os.IsNotExist(err) {
				needsOptional = true
			}
		}
	}
	if needsOptional {
		if err := copyServerFile(filepath.Join(repoRoot, "assets", "paks", "optional.pak"), filepath.Join(pakDir, "optional.pak"), 0o644); err != nil {
			return fmt.Errorf("NextDayz engine HDR assets require `gnb paks fetch optional`: %w", err)
		}
	}
	listPath := filepath.Join(stage, "runtime-assets.txt")
	if err := os.WriteFile(listPath, []byte(strings.Join(assets, "\n")+"\n"), 0o644); err != nil {
		return err
	}
	packer := exec.Command(platform.ExecutablePath(binDir, "Packager"), "--root="+stage,
		"--list="+listPath, "--out="+filepath.Join(pakDir, "runtime.pak"),
		"--manifest="+filepath.Join(stage, "runtime.manifest.json"))
	packer.Dir = binDir
	packer.Stdout, packer.Stderr = os.Stdout, os.Stderr
	if err := packer.Run(); err != nil {
		return fmt.Errorf("build NextDayz runtime.pak: %w", err)
	}

	// The Go Master reads the registry directly. Everything the engine needs is
	// in runtime.pak; loose authoring files, source paks and caches stay behind.
	var entries []entry
	err = filepath.WalkDir(filepath.Join(stage, "bin"), func(path string, item os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if item.IsDir() {
			return nil
		}
		rel, err := filepath.Rel(stage, path)
		if err != nil {
			return err
		}
		entries = append(entries, entry{source: path, name: filepath.ToSlash(rel)})
		return nil
	})
	if err != nil {
		return err
	}
	loose := []string{"assets/paks/runtime.pak", "runtime-assets.txt", "runtime.manifest.json"}
	bundleFiles := serverBundleFiles()
	guideName, guideDest := "nextdayz-server-deployment.md", "DEPLOYMENT.md"
	if opts.Client {
		loose = append(loose, "assets/configs/nextdayz_servers.json")
		// SDL reads the window icon before the asset pak is mounted.
		for _, asset := range assets {
			if strings.HasPrefix(asset, "assets/brand/") {
				loose = append(loose, asset)
			}
		}
		bundleFiles = nextDayzClientBundleFiles()
		guideName, guideDest = "nextdayz-client-package.md", "README.md"
	} else {
		loose = append(loose, "assets/configs/nextdayz_maps.json")
	}
	for _, name := range loose {
		entries = append(entries, entry{source: filepath.Join(stage, filepath.FromSlash(name)), name: name})
	}
	for name, content := range bundleFiles {
		path := filepath.Join(stage, name)
		mode := os.FileMode(0o644)
		if strings.HasSuffix(name, ".sh") {
			mode = 0o755
		}
		if err := os.WriteFile(path, []byte(content), mode); err != nil {
			return err
		}
		entries = append(entries, entry{source: path, name: name})
	}
	for _, name := range []string{"LICENSE", "THIRD-PARTY-NOTICES.md"} {
		path := filepath.Join(repoRoot, name)
		if _, err := os.Stat(path); err != nil {
			return err
		}
		entries = append(entries, entry{source: path, name: name})
	}
	guide := filepath.Join(repoRoot, "docs", "guides", guideName)
	entries = append(entries, entry{source: guide, name: guideDest})
	// Keep relative cross-links usable in both extracted guides.
	for _, name := range []string{"nextdayz-client-package.md", "nextdayz-server-deployment.md"} {
		entries = append(entries, entry{source: filepath.Join(repoRoot, "docs", "guides", name), name: name})
	}
	if runtime.GOOS == "linux" {
		if _, err := stripELFDebugInfo(stage); err != nil {
			return err
		}
	}
	// Hash the final payload (after stripping), so operators can audit a copy.
	files := map[string]string{}
	for _, e := range entries {
		f, err := os.Open(e.source)
		if err != nil {
			return err
		}
		hash := sha256.New()
		_, err = io.Copy(hash, f)
		f.Close()
		if err != nil {
			return err
		}
		files[e.name] = hex.EncodeToString(hash.Sum(nil))
	}
	manifest, err := json.MarshalIndent(map[string]any{"formatVersion": 1, "preset": "nextdayz-" + kind, "version": version,
		"os": runtime.GOOS, "arch": runtime.GOARCH, "files": files}, "", "  ")
	if err != nil {
		return err
	}
	markerName := "nextdayz-" + kind + ".json"
	marker := filepath.Join(stage, markerName)
	if err := os.WriteFile(marker, manifest, 0o644); err != nil {
		return err
	}
	entries = append(entries, entry{source: marker, name: markerName})
	output := opts.Output
	if output == "" {
		output = "out/dist/nextdayz"
	}
	if !filepath.IsAbs(output) {
		output = filepath.Join(repoRoot, output)
	}
	variant, ext := "win64", ".zip"
	if runtime.GOOS == "linux" {
		variant, ext = "linux64", ".tar.gz"
	}
	path := filepath.Join(output, "NextDayz-"+kind+"_"+variant+"_"+version+ext)
	if err := writeServerArchive(path, entries, runtime.GOOS); err != nil {
		return err
	}
	raw, err := os.Open(path)
	if err != nil {
		return err
	}
	defer raw.Close()
	hash := sha256.New()
	if _, err := io.Copy(hash, raw); err != nil {
		return err
	}
	if err := os.WriteFile(path+".sha256", []byte(hex.EncodeToString(hash.Sum(nil))+"  "+filepath.Base(path)+"\n"), 0o644); err != nil {
		return err
	}
	console.Success("NextDayz %s package: %s (%d assets)", kind, path, len(assets))
	return nil
}

var scadComments = regexp.MustCompile(`(?s)/\*.*?\*/|//[^\n]*`)
var scadIncludes = regexp.MustCompile(`\b(?:use|include)\s*<([^>]+)>`)
var scadAssetRefs = regexp.MustCompile(`"(assets/[^"\r\n]+)"`)

func nextDayzAssets(repoRoot, buildRoot string, client bool) ([]string, error) {
	selected := map[string]bool{}
	var add func(string) error
	add = func(name string) error {
		name = filepath.ToSlash(filepath.Clean(filepath.FromSlash(name)))
		if !strings.HasPrefix(name, "assets/") || strings.Contains(name, "../") {
			return fmt.Errorf("asset outside assets/: %s", name)
		}
		if selected[name] {
			return nil
		}
		selected[name] = true
		if !strings.HasSuffix(name, ".scad") {
			return nil
		}
		data, err := os.ReadFile(filepath.Join(repoRoot, filepath.FromSlash(name)))
		if err != nil {
			return err
		}
		code := scadComments.ReplaceAllString(string(data), "")
		for _, match := range scadIncludes.FindAllStringSubmatch(code, -1) {
			dep := filepath.ToSlash(filepath.Join(filepath.Dir(name), match[1]))
			if err := add(dep); err != nil {
				return fmt.Errorf("%s includes %s: %w", name, match[1], err)
			}
		}
		for _, match := range scadAssetRefs.FindAllStringSubmatch(code, -1) {
			if err := add(match[1]); err != nil {
				return err
			}
		}
		return nil
	}
	maps, err := dzmaster.LoadMaps(filepath.Join(repoRoot, "assets", "configs", "nextdayz_maps.json"))
	if err != nil {
		return nil, err
	}
	for _, m := range maps {
		if err := add(m.Scene); err != nil {
			return nil, err
		}
	}
	if client {
		for _, name := range []string{"assets/scad/scenes/nextdayz_helicopter_lobby.scad", "assets/configs/nextdayz/world_land.json", "assets/configs/nextdayz_servers.json"} {
			if err := add(name); err != nil {
				return nil, err
			}
		}
		for _, group := range []struct{ root, pattern string }{{"assets/sounds/nextdayz", ".wav"}, {"assets/locale", ".txt"}, {"assets/brand", ".png"}} {
			count := 0
			err := filepath.WalkDir(filepath.Join(repoRoot, filepath.FromSlash(group.root)), func(path string, item os.DirEntry, err error) error {
				if err != nil {
					return err
				}
				if item.IsDir() || !strings.EqualFold(filepath.Ext(path), group.pattern) {
					return nil
				}
				rel, err := filepath.Rel(repoRoot, path)
				if err != nil {
					return err
				}
				count++
				return add(rel)
			})
			if err != nil {
				return nil, err
			}
			if count == 0 {
				return nil, fmt.Errorf("client assets missing: %s", group.root)
			}
		}
	}
	rigs, err := filepath.Glob(filepath.Join(repoRoot, "assets", "scad", "characters", "nextdayz*.scad"))
	if err != nil {
		return nil, err
	}
	if len(rigs) == 0 {
		return nil, fmt.Errorf("NextDayz character assets are missing")
	}
	for _, path := range rigs {
		rel, _ := filepath.Rel(repoRoot, path)
		if err := add(rel); err != nil {
			return nil, err
		}
	}
	for _, name := range []string{
		"assets/configs/cvar_default.json", "assets/configs/nextdayz_maps.json", "assets/configs/nextdayz_extraction.json",
		"assets/fonts/Roboto-Regular.ttf", "assets/fonts/Roboto-BoldCondensed.ttf", "assets/fonts/DroidSansFallback.ttf",
		"assets/fonts/fa-regular-400.ttf", "assets/fonts/fa-solid-900.ttf", "assets/fonts/fa-brands-400.ttf",
	} {
		if err := add(name); err != nil {
			return nil, err
		}
	}
	// Older checkouts predate the server-directory UI. Include its configuration
	// when present, since current game initialization reads it in DS mode too.
	if _, err := os.Stat(filepath.Join(repoRoot, "assets", "configs", "nextdayz_servers.json")); err == nil {
		if err := add("assets/configs/nextdayz_servers.json"); err != nil {
			return nil, err
		}
	}
	// Renderer creation currently loads the shared HDR set even in dedicated mode.
	for _, name := range []string{"river_road_2", "canary_wharf_1k", "kloppenheim_01_puresky_1k", "kloppenheim_07_1k", "std_env", "rainforest_trail_1k", "studio_small_03_1k", "studio_small_09_1k", "sunset_fairway_1k", "umhlanga_sunrise_1k", "shanghai_bund_1k"} {
		if err := add("assets/textures/" + name + ".hdr"); err != nil {
			return nil, err
		}
	}
	shaders := 0
	err = filepath.WalkDir(filepath.Join(buildRoot, "assets", "shaders"), func(path string, item os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if item.IsDir() || !strings.HasSuffix(path, ".spv") {
			return nil
		}
		rel, err := filepath.Rel(buildRoot, path)
		if err != nil {
			return err
		}
		shaders++
		return add(rel)
	})
	if err != nil {
		return nil, err
	}
	if shaders == 0 {
		return nil, fmt.Errorf("no compiled shaders in %s", buildRoot)
	}
	names := make([]string, 0, len(selected))
	for name := range selected {
		names = append(names, name)
	}
	sort.Strings(names)
	return names, nil
}

func copyServerFile(source, destination string, mode os.FileMode) error {
	in, err := os.Open(source)
	if err != nil {
		return err
	}
	defer in.Close()
	if err := os.MkdirAll(filepath.Dir(destination), 0o755); err != nil {
		return err
	}
	out, err := os.OpenFile(destination, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, mode)
	if err != nil {
		return err
	}
	_, copyErr := io.Copy(out, in)
	closeErr := out.Close()
	if copyErr != nil {
		return copyErr
	}
	return closeErr
}

func serverBundleFiles() map[string]string {
	return map[string]string{
		"start-master.sh":  "#!/bin/sh\nset -eu\nroot=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\nexport LD_LIBRARY_PATH=\"$root/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\"\nexec \"$root/bin/gnb\" master serve --runtime-root \"$root\" \"$@\"\n",
		"start-master.bat": "@echo off\r\n\"%~dp0bin\\gnb.exe\" master serve --runtime-root \"%~dp0.\" %*\r\nexit /b %errorlevel%\r\n",
	}
}
