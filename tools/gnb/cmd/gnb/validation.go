package main

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/validationstore"
	"github.com/spf13/cobra"
)

func newValidationCommand(ctx appContext) *cobra.Command {
	var issue bool
	var accepted bool
	var status string
	var message string
	var author string
	var screenshot string
	note := &cobra.Command{
		Use:   "note <runId>",
		Short: tr("cli.validation.note.short"),
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			if issue && accepted || (issue && status != "") || (accepted && status != "") {
				return fmt.Errorf("choose only one of --issue, --accepted, or --status")
			}
			if issue {
				status = string(validationstore.ReviewIssue)
			} else if accepted {
				status = string(validationstore.ReviewAccepted)
			}
			if status == "" {
				return fmt.Errorf("one of --issue, --accepted, or --status is required")
			}
			store := validationstore.New(ctx.repoRoot, ctx.preset)
			run, err := store.Load(args[0])
			if err != nil {
				return err
			}
			if author == "" {
				author = os.Getenv("USER")
				if author == "" {
					author = os.Getenv("USERNAME")
				}
			}
			if screenshot != "" {
				rel := filepath.ToSlash(filepath.Clean(screenshot))
				if _, err := store.ArtifactPath(run.RunID, rel); err != nil {
					return fmt.Errorf("invalid screenshot: %w", err)
				}
				screenshot = rel
			}
			review := validationstore.Review{
				Status:     validationstore.ReviewStatus(status),
				Message:    message,
				Author:     author,
				Screenshot: screenshot,
			}
			if err := store.WriteReview(run.RunID, review); err != nil {
				return err
			}
			console.Success("validation review saved: %s (%s)", run.RunID, status)
			return nil
		},
	}
	note.Flags().BoolVar(&issue, "issue", false, "record a visual or behavioral issue")
	note.Flags().BoolVar(&accepted, "accepted", false, "mark the run as reviewed without an issue")
	note.Flags().StringVar(&status, "status", "", "review status: pending, accepted, or issue")
	note.Flags().StringVar(&message, "message", "", "review note")
	note.Flags().StringVar(&author, "author", "", "review author")
	note.Flags().StringVar(&screenshot, "screenshot", "", "screenshot path relative to the run evidence directory")

	cmd := &cobra.Command{
		Use:   "validation",
		Short: tr("cli.validation.short"),
	}
	cmd.AddCommand(note)
	return cmd
}

func copyValidationScreenshot(source, target string) error {
	data, err := os.ReadFile(source)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
		return err
	}
	if err := os.WriteFile(target, data, 0o644); err != nil {
		return err
	}
	return nil
}
