package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/packager"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	validatepkg "github.com/gameknife/gknextrenderer/tools/gnb/internal/validate"
	"github.com/spf13/cobra"
)

const emsdkVersion = "4.0.23"

func webOutput(ctx appContext) string {
	return filepath.Join(ctx.repoRoot, "out", "web", "nextdayz")
}

func webProject(ctx appContext) string {
	return filepath.Join(ctx.repoRoot, "web", "nextdayz")
}

func webSDK(ctx appContext) (sdk, toolchain string) {
	sdk = os.Getenv("ND_EMSDK")
	if sdk == "" {
		sdk = filepath.Join(webOutput(ctx), "emsdk")
		if _, err := os.Stat(sdk); errors.Is(err, os.ErrNotExist) {
			probe := filepath.Join(webOutput(ctx), "w0", "emsdk")
			if _, err := os.Stat(filepath.Join(probe, ".emscripten")); err == nil {
				sdk = probe
			}
		}
	}
	toolchain = filepath.Join(sdk, "upstream", "emscripten", "cmake", "Modules", "Platform", "Emscripten.cmake")
	return
}

func webChild(ctx context.Context, dir string, env []string, out, stderr io.Writer, program string, args ...string) *exec.Cmd {
	cmd := exec.CommandContext(ctx, program, args...)
	cmd.Dir, cmd.Env, cmd.Stdout, cmd.Stderr = dir, env, out, stderr
	return cmd
}

func setupWebSDK(cmd *cobra.Command, ctx appContext) error {
	sdk, toolchain := webSDK(ctx)
	if _, err := os.Stat(toolchain); err == nil {
		fmt.Fprintln(cmd.OutOrStdout(), sdk)
		return nil
	}
	if _, err := os.Stat(sdk); errors.Is(err, os.ErrNotExist) && os.Getenv("ND_EMSDK") == "" {
		if err := os.MkdirAll(filepath.Dir(sdk), 0755); err != nil {
			return err
		}
		if err := webChild(cmd.Context(), ctx.repoRoot, nil, cmd.OutOrStdout(), cmd.ErrOrStderr(),
			"git", "clone", "--depth", "1", "--branch", emsdkVersion, "https://github.com/emscripten-core/emsdk.git", sdk).Run(); err != nil {
			return err
		}
	}
	program := filepath.Join(sdk, "emsdk")
	for _, action := range []string{"install", "activate"} {
		args := []string{action, emsdkVersion}
		if runtime.GOOS == "windows" {
			if err := webChild(cmd.Context(), sdk, nil, cmd.OutOrStdout(), cmd.ErrOrStderr(),
				"cmd", append([]string{"/c", program + ".bat"}, args...)...).Run(); err != nil {
				return err
			}
		} else if err := webChild(cmd.Context(), sdk, nil, cmd.OutOrStdout(), cmd.ErrOrStderr(), program, args...).Run(); err != nil {
			return err
		}
	}
	return nil
}

func webHeaders(ctx appContext) (string, error) {
	if include := os.Getenv("ND_CORE_HEADERS"); include != "" {
		return filepath.Abs(include)
	}
	base := filepath.Join(ctx.repoRoot, "out", "build", ctx.preset, "vcpkg_installed")
	matches, err := filepath.Glob(filepath.Join(base, "*", "include", "glm", "glm.hpp"))
	if err != nil {
		return "", err
	}
	for _, path := range matches {
		include := filepath.Dir(filepath.Dir(path))
		if _, err := os.Stat(filepath.Join(include, "nlohmann", "json.hpp")); err == nil {
			return include, nil
		}
	}
	return "", errors.New("build NextDayz for the native header dependencies first")
}

type webKernel struct {
	SHA256 string `json:"sha256"`
	Bytes  int64  `json:"bytes"`
}

func stampWebKernel(ctx appContext) error {
	output := webOutput(ctx)
	dir := filepath.Join(output, "core")
	size, hash, err := webDigest(filepath.Join(dir, "ndcore.wasm"))
	if err != nil {
		return err
	}
	body, err := json.Marshal(webKernel{SHA256: hash, Bytes: size})
	if err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(dir, "build.json"), append(body, '\n'), 0644); err != nil {
		return err
	}
	assets := filepath.Join(output, "assets")
	manifestPath := filepath.Join(assets, "manifest.json")
	if _, err := os.Stat(manifestPath); errors.Is(err, os.ErrNotExist) {
		return nil
	}
	lock, err := lockWebBake(filepath.Join(assets, ".bake.lock"))
	if err != nil {
		return err
	}
	defer lock.Close()
	var manifest webManifest
	if err := readWebJSON(manifestPath, &manifest); err != nil {
		return err
	}
	if manifest.KernelBuild != nil && *manifest.KernelBuild == hash {
		return nil
	}
	manifest.KernelBuild = &hash
	return saveWebManifest(manifestPath, manifest)
}

func checkWebCoreIncludes(dir, source string) error {
	deps, err := exec.Command("ninja", "-C", dir, "-t", "deps").Output()
	if err != nil {
		return err
	}
	root := filepath.ToSlash(source) + "/"
	for _, line := range strings.Split(string(deps), "\n") {
		path := filepath.ToSlash(strings.TrimSpace(line))
		relative, found := strings.CutPrefix(path, root)
		if !found {
			continue
		}
		switch relative {
		case "Engine/Assets/Data/AnimationChannel.hpp",
			"Engine/Assets/Data/RigDefinition.hpp",
			"Engine/Runtime/Subsystems/CharacterPhysics.hpp",
			"Engine/Runtime/Subsystems/NextPhysicsTypes.hpp",
			"Gameplay/Character/NextCharacterController.cpp",
			"Gameplay/Character/NextCharacterController.h",
			"Gameplay/Rig/RigLayeredAnimator.cpp",
			"Gameplay/Rig/RigLayeredAnimator.h",
			"Gameplay/Rig/RigPoseOps.cpp",
			"Gameplay/Rig/RigPoseOps.h",
			"Modules/NextPhysics/JoltCharacter.cpp",
			"Modules/NextPhysics/JoltCharacter.hpp":
			continue
		}
		if strings.HasPrefix(relative, "Engine/") && relative != "Engine/Common/CoreMinimal.hpp" ||
			strings.HasPrefix(relative, "Gameplay/") ||
			strings.HasPrefix(relative, "Modules/") && relative != "Modules/NextNet/NetBuffer.hpp" {
			return fmt.Errorf("WASM dependency crosses the core boundary: %s", path)
		}
	}
	return nil
}

func buildWebCore(cmd *cobra.Command, ctx appContext) error {
	_, toolchain := webSDK(ctx)
	if _, err := os.Stat(toolchain); err != nil {
		return fmt.Errorf("run gnb web setup first: %w", err)
	}
	headers, err := webHeaders(ctx)
	if err != nil {
		return err
	}
	source := filepath.Join(ctx.repoRoot, "src", "Application", "Web", "NextDayzWebCore")
	dir := filepath.Join(webOutput(ctx), "core")
	env := append(os.Environ(), "ND_CORE_HEADERS="+headers, "ND_EMSCRIPTEN_TOOLCHAIN="+toolchain)
	if os.Getenv("ND_JOLT_SOURCE") == "" {
		localJolt, _ := filepath.Glob(filepath.Join(ctx.repoRoot, ".vcpkg", "buildtrees", "joltphysics", "src", "v5.4.0-*.clean"))
		if len(localJolt) > 0 {
			env = append(env, "ND_JOLT_SOURCE="+localJolt[0])
		}
	}
	if err := webChild(cmd.Context(), source, env, cmd.OutOrStdout(), cmd.ErrOrStderr(),
		"cmake", "--preset", "nextdayz-wasm").Run(); err != nil {
		return err
	}
	if err := webChild(cmd.Context(), source, env, cmd.OutOrStdout(), cmd.ErrOrStderr(),
		"cmake", "--build", "--preset", "nextdayz-wasm", "--parallel").Run(); err != nil {
		return err
	}
	if err := checkWebCoreIncludes(dir, filepath.Join(ctx.repoRoot, "src")); err != nil {
		return err
	}
	if err := checkWebGolden(cmd, dir, source, "NextDayzProtocolProbe.cjs", "protocol-golden.txt"); err != nil {
		return err
	}
	if err := webChild(cmd.Context(), dir, nil, cmd.OutOrStdout(), cmd.ErrOrStderr(),
		"node", "NextDayzCoreRules.cjs").Run(); err != nil {
		return fmt.Errorf("WASM rules check: %w", err)
	}
	// The shared client session must treat a DS message stream exactly as the
	// native build does: the desktop client prints the same file.
	if err := checkWebGolden(cmd, dir, source, "NextDayzClientReplay.cjs", "client-replay-golden.txt"); err != nil {
		return err
	}
	fmt.Fprintln(cmd.OutOrStdout(), "NextDayzCore include boundary, protocol and client replay goldens OK")
	return stampWebKernel(ctx)
}

// checkWebGolden runs a WASM probe in Node and compares its output with a
// golden file that the native build of the same probe must also print.
func checkWebGolden(cmd *cobra.Command, dir, source, probe, golden string) error {
	actual, err := webChild(cmd.Context(), dir, nil, nil, cmd.ErrOrStderr(), "node", probe).Output()
	if err != nil {
		return fmt.Errorf("%s: %w", probe, err)
	}
	expected, err := os.ReadFile(filepath.Join(source, golden))
	if err != nil {
		return err
	}
	normalize := func(text []byte) []byte { return bytes.ReplaceAll(text, []byte("\r\n"), []byte("\n")) }
	if !bytes.Equal(normalize(actual), normalize(expected)) {
		return fmt.Errorf("WASM output of %s differs from %s", probe, golden)
	}
	return nil
}

func webNPM(runCtx context.Context, cmd *cobra.Command, ctx appContext, args ...string) error {
	npm := "npm"
	if platform.IsWindows() {
		return webChild(runCtx, webProject(ctx), nil, cmd.OutOrStdout(), cmd.ErrOrStderr(),
			"cmd", append([]string{"/c", "npm.cmd"}, args...)...).Run()
	}
	return webChild(runCtx, webProject(ctx), nil, cmd.OutOrStdout(), cmd.ErrOrStderr(), npm, args...).Run()
}

func webInstall(runCtx context.Context, cmd *cobra.Command, ctx appContext) error {
	if _, err := os.Stat(filepath.Join(webProject(ctx), "node_modules", "vite", "package.json")); err == nil {
		return nil
	}
	return webNPM(runCtx, cmd, ctx, "ci")
}

func exportWebIcons(runCtx context.Context, ctx appContext) error {
	script := filepath.Join(ctx.repoRoot, "assets", "agentscripts", "nextdayz-web-icon-export.agentscript.json")
	opts := validatepkg.Options{
		RepoRoot: ctx.repoRoot, Preset: ctx.preset, Script: script,
		Env: []string{"NEXTDAYZ_ICON_EXPORT=" + filepath.Join(webOutput(ctx), "icons-desktop")},
	}
	if runtime.GOOS == "linux" {
		opts.Args = append(opts.Args, "--headless-surface")
	}
	result, err := validatepkg.RunWithResult(runCtx, opts)
	if result.EvidenceDir != "" {
		fmt.Println("NextDayz icon export evidence:", result.EvidenceDir)
	}
	return err
}

func addWebCoreCommands(root *cobra.Command, ctx appContext) {
	setup := &cobra.Command{Use: "setup", Short: "Install the pinned NextDayz WASM SDK", Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error { return setupWebSDK(cmd, ctx) }}

	var onlyCore bool
	build := &cobra.Command{Use: "build", Short: "Build the NextDayz WASM core and browser client", Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error {
			if err := buildWebCore(cmd, ctx); err != nil || onlyCore {
				return err
			}
			if err := webInstall(cmd.Context(), cmd, ctx); err != nil {
				return err
			}
			return webNPM(cmd.Context(), cmd, ctx, "run", "build")
		}}
	build.Flags().BoolVar(&onlyCore, "core-only", false, "Build and verify only the WASM core")

	var watch bool
	dev := &cobra.Command{Use: "dev", Short: "Start the NextDayz browser development server", Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error {
			runCtx, cancel := signal.NotifyContext(cmd.Context(), os.Interrupt)
			defer cancel()
			if err := buildWebCore(cmd, ctx); err != nil {
				return err
			}
			if err := webInstall(runCtx, cmd, ctx); err != nil {
				return err
			}
			output := filepath.Join(webOutput(ctx), "assets")
			bake := func(verify bool) error {
				return bakeWebAssets(runCtx, ctx, output, "", false, false, verify, cmd.OutOrStdout())
			}
			if err := bake(true); err != nil {
				return err
			}
			if watch {
				go func() {
					ticker := time.NewTicker(time.Second)
					defer ticker.Stop()
					var previous string
					for {
						select {
						case <-runCtx.Done():
							return
						case <-ticker.C:
							if err := bake(false); err != nil {
								if previous != err.Error() {
									fmt.Fprintln(cmd.ErrOrStderr(), err)
									previous = err.Error()
								}
							} else {
								previous = ""
							}
						}
					}
				}()
			}
			err := webNPM(runCtx, cmd, ctx, "run", "dev")
			if runCtx.Err() != nil {
				return nil
			}
			return err
		}}
	dev.Flags().BoolVar(&watch, "watch", true, "Rebake published maps after source changes")

	var bots, latency, seconds int
	var loss float64
	var master, mapID string
	test := &cobra.Command{Use: "test", Short: "Run NextDayz web tests and WebRTC bots", Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error {
			if err := webInstall(cmd.Context(), cmd, ctx); err != nil {
				return err
			}
			if bots == 0 {
				return webNPM(cmd.Context(), cmd, ctx, "test")
			}
			return webChild(cmd.Context(), webProject(ctx), nil, cmd.OutOrStdout(), cmd.ErrOrStderr(),
				"node", "shared/bots.mjs", "--bots", fmt.Sprint(bots), "--seconds", fmt.Sprint(seconds),
				"--latency", fmt.Sprint(latency), "--loss", fmt.Sprint(loss), "--master", master, "--map", mapID).Run()
		}}
	test.Flags().IntVar(&bots, "bots", 0, "Run this many bots against a running WebRTC-enabled Master")
	test.Flags().IntVar(&seconds, "seconds", 180, "Time to keep all bots online before they extract")
	test.Flags().IntVar(&latency, "latency", 0, "Outgoing delay for bot packets in milliseconds")
	test.Flags().Float64Var(&loss, "loss", 0, "Percentage of outgoing unreliable packets to drop")
	test.Flags().StringVar(&master, "master", "http://127.0.0.1:7784", "Master URL for the bots")
	test.Flags().StringVar(&mapID, "map", "training_ground", "Map for the bot script")
	stage := &cobra.Command{Use: "stage", Short: "Prepare the production web directory for gnb master serve --web", Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error {
			output := webOutput(ctx)
			if err := bakeWebAssets(cmd.Context(), ctx, filepath.Join(output, "assets"), "", true, true, true, cmd.OutOrStdout()); err != nil {
				return err
			}
			dest, err := os.MkdirTemp(output, "web-release-")
			if err != nil {
				return err
			}
			if err := packager.StageNextDayzWeb(output, dest); err != nil {
				os.RemoveAll(dest)
				return err
			}
			fmt.Fprintln(cmd.OutOrStdout(), dest)
			return nil
		}}
	icons := &cobra.Command{Use: "icons", Short: "Export the desktop item icons for the Web asset bake", Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error { return exportWebIcons(cmd.Context(), ctx) }}
	root.AddCommand(setup, build, dev, test, stage, icons)
}
