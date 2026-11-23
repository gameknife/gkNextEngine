package main

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/spf13/cobra"
)

const trainingGateScene = "assets/scad/proc/nextdayz/training_ground/published/v3/scene.scad"

type webScadManifest struct {
	Scene string   `json:"scene"`
	Files []string `json:"files"`
}

func stageWebScadProbe(root, output string) error {
	probe := webScadManifest{
		Scene: trainingGateScene,
		Files: []string{"assets/fonts/DroidSansFallback.ttf", "assets/scad/lib/gk_texgraph.scad"},
	}
	sceneDir := filepath.Join(root, filepath.Dir(trainingGateScene))
	err := filepath.WalkDir(sceneDir, func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !entry.Type().IsRegular() {
			return nil
		}
		switch filepath.Ext(path) {
		case ".scad", ".hmap":
			relative, err := filepath.Rel(root, path)
			if err != nil {
				return err
			}
			probe.Files = append(probe.Files, filepath.ToSlash(relative))
		}
		return nil
	})
	if err != nil {
		return err
	}
	for _, path := range probe.Files {
		data, err := os.ReadFile(filepath.Join(root, path))
		if err != nil {
			return err
		}
		target := filepath.Join(output, "sources", path)
		if err := os.MkdirAll(filepath.Dir(target), 0755); err != nil {
			return err
		}
		if err := os.WriteFile(target, data, 0644); err != nil {
			return err
		}
		if err := webGzip(target); err != nil {
			return err
		}
	}
	for _, file := range []string{"ndscad.mjs", "ndscad.wasm"} {
		if err := webGzip(filepath.Join(output, file)); err != nil {
			return err
		}
	}
	data, err := json.Marshal(probe)
	if err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(output, "manifest.json"), append(data, '\n'), 0644)
}

func newWebScadProbeCommand(ctx appContext) *cobra.Command {
	var buildOnly bool
	var port int
	command := &cobra.Command{
		Use:   "scad-probe",
		Short: "Generate the published training ground gate in a browser Worker",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error {
			_, toolchain := webSDK(ctx)
			if _, err := os.Stat(toolchain); err != nil {
				return fmt.Errorf("run gnb web setup first: %w", err)
			}
			headers, err := webHeaders(ctx)
			if err != nil {
				return err
			}
			source := filepath.Join(ctx.repoRoot, "src", "Application", "Web", "NextDayzScadProbe")
			output := filepath.Join(webOutput(ctx), "scad-probe")
			build := filepath.Join(output, "build")
			if err := webChild(cmd.Context(), source, nil, cmd.OutOrStdout(), cmd.ErrOrStderr(),
				"cmake", "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
				"-DCMAKE_TOOLCHAIN_FILE="+toolchain, "-DND_SCAD_HEADERS="+headers).Run(); err != nil {
				return err
			}
			if err := webChild(cmd.Context(), source, nil, cmd.OutOrStdout(), cmd.ErrOrStderr(),
				"cmake", "--build", build, "--target", "ndscad", "--parallel").Run(); err != nil {
				return err
			}
			if err := stageWebScadProbe(ctx.repoRoot, filepath.Join(output, "public")); err != nil || buildOnly {
				return err
			}
			if err := webInstall(cmd.Context(), cmd, ctx); err != nil {
				return err
			}
			fmt.Fprintf(cmd.OutOrStdout(), "SCAD review: http://127.0.0.1:%d/scad-review/\n", port)
			return webNPM(cmd.Context(), cmd, ctx, "run", "dev", "--", "--port", fmt.Sprint(port))
		},
	}
	command.Flags().BoolVar(&buildOnly, "build-only", false, "Build and stage files without serving the preview")
	command.Flags().IntVar(&port, "port", 4184, "Local review server port")
	return command
}
