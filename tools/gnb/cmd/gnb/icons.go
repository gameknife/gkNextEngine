package main

import (
	"fmt"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/icons"
	"github.com/spf13/cobra"
)

func newIconsCommand(ctx appContext) *cobra.Command {
	var force bool
	cmd := &cobra.Command{
		Use:   "icons",
		Short: tr("cli.icons.short"),
		RunE: func(cmd *cobra.Command, args []string) error {
			count, err := icons.EnsureIco(ctx.repoRoot, force)
			if err != nil {
				return err
			}
			if count > 0 {
				console.Success("updated %d Windows .ico icon(s) under assets/icons/ico/", count)
			} else {
				console.Info("Windows .ico icons under assets/icons/ico/ are already up to date")
			}
			return nil
		},
	}

	cmd.Flags().BoolVarP(&force, "force", "f", false, "force regeneration of all .ico files even if up to date")

	syncCmd := &cobra.Command{
		Use:   "sync",
		Short: tr("cli.icons.sync.short"),
		RunE: func(cmd *cobra.Command, args []string) error {
			count, err := icons.EnsureIco(ctx.repoRoot, force)
			if err != nil {
				return err
			}
			if count > 0 {
				console.Success("updated %d Windows .ico icon(s) under assets/icons/ico/", count)
			} else {
				console.Info("Windows .ico icons under assets/icons/ico/ are already up to date")
			}
			return nil
		},
	}
	syncCmd.Flags().BoolVarP(&force, "force", "f", false, "force regeneration of all .ico files even if up to date")
	cmd.AddCommand(syncCmd)

	androidApp := ""
	outputRes := ""
	androidCmd := &cobra.Command{
		Use:   "android",
		Short: tr("cli.icons.android.short"),
		RunE: func(cmd *cobra.Command, args []string) error {
			if outputRes == "" {
				return fmt.Errorf("--out is required (path to res directory)")
			}
			if err := icons.GenerateAndroidAppIcons(ctx.repoRoot, androidApp, outputRes); err != nil {
				return err
			}
			console.Success("generated Android launcher icons for %q into %s", androidApp, outputRes)
			return nil
		},
	}
	androidCmd.Flags().StringVar(&androidApp, "app", "", "application name (e.g. MagicaLego, Brotato3D)")
	androidCmd.Flags().StringVar(&outputRes, "out", "", "target Android res directory")
	cmd.AddCommand(androidCmd)

	iosApp := ""
	iosOut := ""
	iosCmd := &cobra.Command{
		Use:   "ios",
		Short: tr("cli.icons.ios.short"),
		RunE: func(cmd *cobra.Command, args []string) error {
			if iosOut == "" {
				return fmt.Errorf("--out is required (path to output directory)")
			}
			if err := icons.GenerateIOSAppIcons(ctx.repoRoot, iosApp, iosOut); err != nil {
				return err
			}
			console.Success("generated iOS app icons for %q into %s", iosApp, iosOut)
			return nil
		},
	}
	iosCmd.Flags().StringVar(&iosApp, "app", "", "application name (e.g. gkNextRenderer, ScadLibrary)")
	iosCmd.Flags().StringVar(&iosOut, "out", "", "target output directory for iOS AppIcon*.png files")
	cmd.AddCommand(iosCmd)

	return cmd
}
