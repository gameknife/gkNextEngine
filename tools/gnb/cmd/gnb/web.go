package main

import (
	"compress/gzip"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/spf13/cobra"
)

const webBakeRecipe = 1

type webMap struct {
	ID         string `json:"id"`
	Scene      string `json:"scene"`
	MapVersion int    `json:"mapVersion"`
	Status     string `json:"status"`
	Name       string `json:"name"`
	LocalName  string `json:"localName"`
	Profile    string `json:"profile"`
}

type webFile struct {
	Path       string `json:"path"`
	Bytes      int64  `json:"bytes"`
	SHA256     string `json:"sha256"`
	GzipBytes  int64  `json:"gzipBytes,omitempty"`
	GzipSHA256 string `json:"gzipSha256,omitempty"`
}

type webBundle struct {
	Directory string             `json:"directory"`
	Map       *webMap            `json:"map,omitempty"`
	Protocol  uint16             `json:"protocol"`
	Inputs    []string           `json:"inputs"`
	InputHash string             `json:"inputHash"`
	Files     map[string]webFile `json:"files"`
}

type webManifest struct {
	Recipe      int                  `json:"recipe"`
	Protocol    uint16               `json:"protocol"`
	KernelBuild *string              `json:"kernelBuild"`
	Maps        map[string]webBundle `json:"maps"`
	Rigs        map[string]webBundle `json:"rigs,omitempty"`
	Shared      *webBundle           `json:"shared,omitempty"`
}

func newWebCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{Use: "web", Short: "NextDayz web client tools"}
	var mapID string
	var check, watch, rigs bool
	bake := &cobra.Command{
		Use:   "bake",
		Short: "Bake the published NextDayz maps and check their web asset manifest",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error {
			if check && watch {
				return errors.New("--check and --watch are mutually exclusive")
			}
			bakeCtx, cancel := signal.NotifyContext(cmd.Context(), os.Interrupt)
			defer cancel()
			output := filepath.Join(ctx.repoRoot, "out", "web", "nextdayz", "assets")
			run := func(verifyFiles bool) error {
				return bakeWebAssets(bakeCtx, ctx, output, mapID, check, rigs, verifyFiles, cmd.OutOrStdout())
			}
			if err := run(true); err != nil {
				return err
			}
			if watch {
				ticker := time.NewTicker(time.Second)
				defer ticker.Stop()
				var previousError string
				for {
					select {
					case <-bakeCtx.Done():
						return nil
					case <-ticker.C:
						err := run(false)
						if err == nil {
							previousError = ""
						} else if err.Error() != previousError {
							previousError = err.Error()
							fmt.Fprintln(cmd.ErrOrStderr(), err)
						}
					}
				}
			}
			return nil
		},
	}
	bake.Flags().StringVar(&mapID, "map", "", "Bake a single published map ID")
	bake.Flags().BoolVar(&rigs, "rigs", false, "Include character rigs and shared client assets")
	bake.Flags().BoolVar(&check, "check", false, "Fail if the manifest, source inputs or output file hashes are stale")
	bake.Flags().BoolVar(&watch, "watch", false, "Watch published SCAD dependencies and update changed bundles")
	root.AddCommand(bake)
	addWebCoreCommands(root, ctx)
	return root
}

func readWebJSON(path string, into any) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if err := json.Unmarshal(data, into); err != nil {
		return fmt.Errorf("%s: %w", path, err)
	}
	return nil
}

func webInputHash(root string, inputs []string) (string, error) {
	h := sha256.New()
	fmt.Fprintln(h, webBakeRecipe)
	for _, path := range inputs {
		if !filepath.IsLocal(filepath.FromSlash(path)) {
			return "", fmt.Errorf("invalid bake dependency %q", path)
		}
		file, err := os.Open(filepath.Join(root, filepath.FromSlash(path)))
		if err != nil {
			return "", err
		}
		fileHash := sha256.New()
		_, err = io.Copy(fileHash, file)
		closeErr := file.Close()
		if err != nil || closeErr != nil {
			return "", errors.Join(err, closeErr)
		}
		fmt.Fprintf(h, "%s\x00%x\n", path, fileHash.Sum(nil))
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func webDigest(path string) (int64, string, error) {
	file, err := os.Open(path)
	if err != nil {
		return 0, "", err
	}
	defer file.Close()
	hash := sha256.New()
	size, err := io.Copy(hash, file)
	return size, hex.EncodeToString(hash.Sum(nil)), err
}

func verifyWebBundle(root, output string, bundle webBundle, verifyFiles bool) error {
	if bundle.InputHash == "" || len(bundle.Files) == 0 {
		return errors.New("missing published bundle")
	}
	hash, err := webInputHash(root, bundle.Inputs)
	if err != nil {
		return err
	}
	if hash != bundle.InputHash {
		return errors.New("source files have changed")
	}
	if verifyFiles {
		for _, entry := range bundle.Files {
			if !filepath.IsLocal(filepath.FromSlash(entry.Path)) {
				return fmt.Errorf("invalid asset path %q", entry.Path)
			}
			path := filepath.Join(output, filepath.FromSlash(entry.Path))
			if size, hash, err := webDigest(path); err != nil || size != entry.Bytes || hash != entry.SHA256 {
				return fmt.Errorf("missing or changed asset %s", entry.Path)
			}
			if entry.GzipBytes != 0 {
				if size, hash, err := webDigest(path + ".gz"); err != nil || size != entry.GzipBytes || hash != entry.GzipSHA256 {
					return fmt.Errorf("missing or changed gzip asset %s", entry.Path)
				}
			}
		}
	}
	return nil
}

func webGzip(source string) error {
	input, err := os.Open(source)
	if err != nil {
		return err
	}
	defer input.Close()
	output, err := os.Create(source + ".gz")
	if err != nil {
		return err
	}
	writer := gzip.NewWriter(output)
	_, writeErr := io.Copy(writer, input)
	return errors.Join(writeErr, writer.Close(), output.Close())
}

var bakeMapID = regexp.MustCompile(`^[a-z0-9_]+$`)

func bakeWebAssets(runCtx context.Context, ctx appContext, output, mapID string, check, rigs, verifyFiles bool, log io.Writer) error {
	var registry struct {
		Maps []webMap `json:"maps"`
	}
	if err := readWebJSON(filepath.Join(ctx.repoRoot, "assets", "configs", "nextdayz_maps.json"), &registry); err != nil {
		return err
	}

	if !check {
		if err := os.MkdirAll(output, 0755); err != nil {
			return err
		}
		lock, err := lockWebBake(filepath.Join(output, ".bake.lock"))
		if err != nil {
			return fmt.Errorf("lock NextDayz web bake: %w", err)
		}
		defer lock.Close()
	}

	manifestPath := filepath.Join(output, "manifest.json")
	manifest := webManifest{}
	if err := readWebJSON(manifestPath, &manifest); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	if manifest.Recipe != webBakeRecipe {
		if check {
			return errors.New("web manifest is missing or uses an older bake recipe")
		}
		manifest = webManifest{Recipe: webBakeRecipe, Maps: make(map[string]webBundle)}
	}
	if manifest.Maps == nil {
		manifest.Maps = make(map[string]webBundle)
	}

	selected := 0
	changed := false
	active := make(map[string]bool)
	for _, entry := range registry.Maps {
		if entry.Status != "published" {
			continue
		}
		if !bakeMapID.MatchString(entry.ID) || entry.MapVersion <= 0 || !filepath.IsLocal(entry.Scene) || active[entry.ID] {
			return fmt.Errorf("invalid published map %q", entry.ID)
		}
		active[entry.ID] = true
		if mapID != "" && mapID != entry.ID {
			continue
		}
		selected++
		bundle := manifest.Maps[entry.ID]
		var validation error
		if bundle.Map == nil || *bundle.Map != entry || bundle.Protocol != manifest.Protocol {
			validation = errors.New("published map version, metadata or protocol changed")
		} else {
			validation = verifyWebBundle(ctx.repoRoot, output, bundle, verifyFiles)
		}
		if validation == nil {
			if check {
				fmt.Fprintf(log, "%s v%d OK (%d assets)\n", entry.ID, entry.MapVersion, len(bundle.Files))
			}
			continue
		}
		if check {
			return fmt.Errorf("%s v%d: %w", entry.ID, entry.MapVersion, validation)
		}
		base := filepath.Join("maps", entry.ID, fmt.Sprintf("v%d", entry.MapVersion))
		bundle, err := produceWebBundle(runCtx, ctx, output, base, "map", entry.Scene, &entry, bundle, log)
		if err != nil {
			return fmt.Errorf("bake %s: %w", entry.ID, err)
		}
		manifest.Protocol = bundle.Protocol
		manifest.Maps[entry.ID] = bundle
		changed = true
	}
	if selected == 0 {
		return fmt.Errorf("no published map matches %q", mapID)
	}

	// A partial bake can introduce a new protocol version. Once published,
	// every bundle reachable from this manifest must speak that version.
	if changed {
		for id, bundle := range manifest.Maps {
			if bundle.Protocol != manifest.Protocol {
				delete(manifest.Maps, id)
			}
		}
		for name, bundle := range manifest.Rigs {
			if bundle.Protocol != manifest.Protocol {
				delete(manifest.Rigs, name)
			}
		}
		if manifest.Shared != nil && manifest.Shared.Protocol != manifest.Protocol {
			manifest.Shared = nil
		}
	}

	if rigs {
		if manifest.Rigs == nil {
			manifest.Rigs = make(map[string]webBundle)
		}
		for _, rig := range []string{"survivor", "infected", "fps_hands", "weapons", "loot_items", "attachments", "clothing"} {
			name := "nextdayz_" + rig
			bundle := manifest.Rigs[name]
			validation := verifyWebBundle(ctx.repoRoot, output, bundle, verifyFiles)
			if bundle.Protocol != manifest.Protocol {
				validation = errors.New("missing rig or a different protocol")
			}
			if validation == nil {
				if check {
					fmt.Fprintf(log, "%s OK (%d assets)\n", name, len(bundle.Files))
				}
				continue
			}
			if check {
				return fmt.Errorf("%s: %w", name, validation)
			}
			source := "assets/scad/characters/" + name + ".scad"
			base := filepath.Join("rigs", name)
			bundle, err := produceWebBundle(runCtx, ctx, output, base, "rig", source, nil, bundle, log)
			if err != nil {
				return fmt.Errorf("bake %s: %w", name, err)
			}
			if bundle.Protocol != manifest.Protocol {
				return errors.New("rig and maps have different protocol versions")
			}
			manifest.Rigs[name] = bundle
			changed = true
		}
		var validation error
		bundle := webBundle{}
		if manifest.Shared == nil || manifest.Shared.Protocol != manifest.Protocol {
			validation = errors.New("missing shared assets or a different protocol")
		} else {
			bundle = *manifest.Shared
			validation = verifyWebBundle(ctx.repoRoot, output, bundle, verifyFiles)
		}
		if validation != nil {
			if check {
				return fmt.Errorf("shared assets: %w", validation)
			}
			bundle, err := produceWebBundle(runCtx, ctx, output, "shared", "shared", "", nil, bundle, log)
			if err != nil {
				return fmt.Errorf("bake shared assets: %w", err)
			}
			if bundle.Protocol != manifest.Protocol {
				return errors.New("shared assets and maps have different protocol versions")
			}
			manifest.Shared = &bundle
			changed = true
		}
	}

	if mapID == "" {
		for id := range manifest.Maps {
			if !active[id] {
				if check {
					return fmt.Errorf("unpublished map %s remains in the manifest", id)
				}
				delete(manifest.Maps, id)
				changed = true
			}
		}
	}
	if changed {
		var kernel webKernel
		if err := readWebJSON(filepath.Join(webOutput(ctx), "core", "build.json"), &kernel); err == nil && kernel.SHA256 != "" {
			manifest.KernelBuild = &kernel.SHA256
		}
		if err := saveWebManifest(manifestPath, manifest); err != nil {
			return err
		}
		fmt.Fprintln(log, manifestPath)
	}

	if check && rigs {
		fmt.Fprintln(log, "shared assets OK")
	}
	return nil
}

func saveWebManifest(path string, manifest webManifest) error {
	saved, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		return err
	}
	tmp, err := os.CreateTemp(filepath.Dir(path), "manifest-*.tmp")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name())
	if err := tmp.Chmod(0644); err != nil {
		tmp.Close()
		return err
	}
	_, writeErr := tmp.Write(append(saved, '\n'))
	if err := errors.Join(writeErr, tmp.Close()); err != nil {
		return err
	}
	return os.Rename(tmp.Name(), path)
}

func produceWebBundle(runCtx context.Context, ctx appContext, output, base, kind, source string, entry *webMap, previous webBundle, log io.Writer) (webBundle, error) {
	bundle := webBundle{Map: entry, Files: make(map[string]webFile)}
	executable := platform.ExecutablePath(platform.BinDir(ctx.repoRoot, ctx.preset), "NextDayzBake")
	if _, err := os.Stat(executable); err != nil {
		return bundle, fmt.Errorf("run gnb build NextDayzBake first: %w", err)
	}
	if err := os.MkdirAll(output, 0755); err != nil {
		return bundle, err
	}
	stage, err := os.MkdirTemp(output, ".bake-")
	if err != nil {
		return bundle, err
	}
	defer os.RemoveAll(stage)

	// Check dependency inputs before and after a rebuilt bundle so editing during
	// watch cannot publish a result already known to be out of date.
	before, beforeErr := webInputHash(ctx.repoRoot, previous.Inputs)
	args := []string{kind, stage}
	if source != "" {
		args = []string{kind, source, stage}
	}
	cmd := exec.CommandContext(runCtx, executable, args...)
	cmd.Dir, cmd.Stdout, cmd.Stderr = ctx.repoRoot, log, log
	if err := cmd.Run(); err != nil {
		return bundle, err
	}
	if len(previous.Inputs) != 0 && beforeErr == nil {
		after, err := webInputHash(ctx.repoRoot, previous.Inputs)
		if err != nil || before != after {
			return bundle, errors.New("inputs changed during baking; retry")
		}
	}

	var report struct {
		Protocol     uint16   `json:"protocol"`
		Dependencies []string `json:"dependencies"`
	}
	reportPath := filepath.Join(stage, "bake.json")
	if err := readWebJSON(reportPath, &report); err != nil {
		return bundle, err
	}
	if report.Protocol == 0 || len(report.Dependencies) == 0 {
		return bundle, errors.New("missing native bake protocol or dependencies")
	}
	// Keep absolute checkout paths out of payloads and hashes.
	raw, err := os.ReadFile(reportPath)
	if err != nil {
		return bundle, err
	}
	var stats map[string]json.RawMessage
	if err := json.Unmarshal(raw, &stats); err != nil {
		return bundle, err
	}
	delete(stats, "dependencies")
	if entry != nil {
		envPath := filepath.Join(stage, "env.json")
		var env map[string]json.RawMessage
		if err := readWebJSON(envPath, &env); err != nil {
			return bundle, err
		}
		env["profile"], _ = json.Marshal(entry.Profile)
		encoded, err := json.Marshal(env)
		if err != nil {
			return bundle, err
		}
		if err := os.WriteFile(envPath, append(encoded, '\n'), 0644); err != nil {
			return bundle, err
		}
	}
	encoded, err := json.Marshal(stats)
	if err != nil {
		return bundle, err
	}
	if err := os.WriteFile(reportPath, append(encoded, '\n'), 0644); err != nil {
		return bundle, err
	}

	inputs := append(report.Dependencies, "src/Application/Game/NextDayz/Net/ProtocolVersion.hpp", "tools/gnb/cmd/gnb/web.go")
	for _, dir := range []string{"src/Application/Util/NextDayzBake", "src/Modules/ScadLoader"} {
		err := filepath.WalkDir(filepath.Join(ctx.repoRoot, dir), func(path string, d os.DirEntry, err error) error {
			if err == nil && !d.IsDir() {
				inputs = append(inputs, path)
			}
			return err
		})
		if err != nil {
			return bundle, err
		}
	}
	if entry != nil {
		inputs = append(inputs,
			"src/Application/Game/NextDayz/Inventory/MapLootKeys.hpp",
			"src/Application/Game/NextDayz/Data/WorldProfiles.hpp",
			"src/Application/Game/NextDayz/World/WorldAnchorRegistry.cpp")

	}
	for _, path := range inputs {
		if filepath.IsAbs(path) {
			path, err = filepath.Rel(ctx.repoRoot, path)
			if err != nil {
				return bundle, err
			}
		}
		bundle.Inputs = append(bundle.Inputs, filepath.ToSlash(filepath.Clean(path)))
	}
	slices.Sort(bundle.Inputs)
	bundle.Inputs = slices.Compact(bundle.Inputs)
	bundle.InputHash, err = webInputHash(ctx.repoRoot, bundle.Inputs)
	if err != nil {
		return bundle, err
	}

	keys := []string{}
	err = filepath.WalkDir(stage, func(path string, d os.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		relative, err := filepath.Rel(stage, path)
		if err != nil {
			return err
		}
		name := filepath.ToSlash(relative)
		if strings.HasSuffix(name, ".gz") {
			return nil
		}
		size, hash, err := webDigest(path)
		if err != nil {
			return err
		}
		asset := webFile{Bytes: size, SHA256: hash}
		switch filepath.Ext(name) {
		case ".glb", ".bin", ".json", ".wav":
			if err := webGzip(path); err != nil {
				return err
			}
			asset.GzipBytes, asset.GzipSHA256, err = webDigest(path + ".gz")
			if err != nil {
				return err
			}
			if asset.GzipBytes >= asset.Bytes {
				asset.GzipBytes, asset.GzipSHA256 = 0, ""
				if err := os.Remove(path + ".gz"); err != nil {
					return err
				}
			}
		}
		keys = append(keys, name)
		bundle.Files[name] = asset
		return nil
	})
	if err != nil {
		return bundle, err
	}

	bundleHash := sha256.New()
	slices.Sort(keys)
	var rawBytes, transferBytes int64
	for _, key := range keys {
		asset := bundle.Files[key]
		fmt.Fprintf(bundleHash, "%s %s %s\n", key, asset.SHA256, asset.GzipSHA256)
		rawBytes += asset.Bytes
		if asset.GzipBytes != 0 {
			transferBytes += asset.GzipBytes
		} else {
			transferBytes += asset.Bytes
		}
	}
	bundle.Directory = filepath.ToSlash(filepath.Join(base, hex.EncodeToString(bundleHash.Sum(nil))[:24]))
	bundle.Protocol = report.Protocol
	for _, key := range keys {
		asset := bundle.Files[key]
		asset.Path = bundle.Directory + "/" + key
		bundle.Files[key] = asset
	}
	destination := filepath.Join(output, filepath.FromSlash(bundle.Directory))
	if err := os.MkdirAll(filepath.Dir(destination), 0755); err != nil {
		return bundle, err
	}
	if err := os.Rename(stage, destination); err != nil {
		// A deterministic bake may produce the same directory on a retry.
		// Validate or replace each file atomically if a local file was damaged.
		if info, statErr := os.Stat(destination); statErr != nil || !info.IsDir() {
			return bundle, err
		}
		if err := filepath.WalkDir(stage, func(path string, d os.DirEntry, err error) error {
			if err != nil || d.IsDir() {
				return err
			}
			relative, err := filepath.Rel(stage, path)
			if err != nil {
				return err
			}
			target := filepath.Join(destination, relative)
			if err := os.MkdirAll(filepath.Dir(target), 0755); err != nil {
				return err
			}
			return os.Rename(path, target)
		}); err != nil {
			return bundle, err
		}
	}
	fmt.Fprintf(log, "%s — %d assets, %.2f MB raw, %.2f MB HTTP transfer\n",
		bundle.Directory, len(keys), float64(rawBytes)/1e6, float64(transferBytes)/1e6)
	for _, key := range keys {
		fmt.Fprintf(log, "  %s %d bytes\n", key, bundle.Files[key].Bytes)
	}
	return bundle, nil
}
