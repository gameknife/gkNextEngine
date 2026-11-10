package main

import (
	"fmt"
	"runtime"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/packager"
	"github.com/spf13/cobra"
)

func newNextDayzPackageCommand(ctx appContext, client bool) *cobra.Command {
	var skipBuild bool
	opts := packager.NextDayzOptions{Client: client}
	name, description := "nextdayz-server", "Build a standalone NextDayz Master + dedicated-server bundle (native Windows/Linux x64)"
	if client {
		name, description = "nextdayz-client", "Build a standalone NextDayz client with maps, lobby, UI and audio (native Windows/Linux x64)"
	}
	cmd := &cobra.Command{
		Use:   name,
		Short: description,
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			if runtime.GOARCH != "amd64" || (runtime.GOOS != "windows" && runtime.GOOS != "linux") {
				return fmt.Errorf("NextDayz packaging supports native Windows/Linux amd64 only")
			}
			if !skipBuild {
				build := newBuildCommand(ctx)
				if err := build.Flags().Set("tracy", "off"); err != nil {
					return err
				}
				if err := build.RunE(build, []string{"NextDayz", "Packager"}); err != nil {
					return err
				}
			}
			return packager.PackageNextDayz(ctx.repoRoot, ctx.preset, opts)
		},
	}
	cmd.Flags().BoolVar(&skipBuild, "skip-build", false, "reuse already built NextDayz and Packager")
	cmd.Flags().StringVar(&opts.Version, "version", "local", "version embedded in the archive and manifest")
	cmd.Flags().StringVar(&opts.Output, "output", "out/dist/nextdayz", "archive output directory")
	if client {
		cmd.Flags().StringVar(&opts.MasterURL, "master-url", "", "preconfigure a public Master HTTP URL in the editable server list")
	}
	return cmd
}
