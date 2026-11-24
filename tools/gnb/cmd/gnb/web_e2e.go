package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/spf13/cobra"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

// webE2EScenarios run in order against one Master and one review-mode site. Each is a
// Playwright script under web/nextdayz/test that reads its services from ND_TEST_*.
var webE2EScenarios = []string{"w7-mixed", "w7-loot", "w7-respawn", "e2e-return", "w7"}

type webE2EOptions struct {
	Scenarios  []string
	Browser    string
	SoftwareGL bool
	MasterPort int
	SitePort   int
	PortBase   int
	RtcBase    int
}

// runWebE2E starts a fresh Master (WebRTC ports, cheats for the review shortcuts) and the
// Vite site in review mode, runs the scenarios against them, and stops everything it
// started, including the map servers the Master launched.
func runWebE2E(cmd *cobra.Command, ctx appContext, opts webE2EOptions) error {
	out := cmd.OutOrStdout()
	output := webOutput(ctx)
	for _, need := range []struct{ path, how string }{
		{filepath.Join(output, "core", "ndcore-review.mjs"), "run gnb web build"},
		{filepath.Join(output, "assets", "manifest.json"), "run gnb web icons and gnb web bake --rigs"},
		{platform.ExecutablePath(platform.BinDir(ctx.repoRoot, ctx.preset), "NextDayz"), "run gnb build NextDayz"},
	} {
		if _, err := os.Stat(need.path); err != nil {
			return fmt.Errorf("%s is missing: %s", need.path, need.how)
		}
	}
	if err := webInstall(cmd.Context(), cmd, ctx); err != nil {
		return err
	}
	run := filepath.Join(output, "e2e", time.Now().Format("20060102-150405"))
	if err := os.MkdirAll(run, 0o755); err != nil {
		return err
	}
	runCtx, cancel := context.WithCancel(cmd.Context())
	defer cancel()

	masterURL := fmt.Sprintf("http://127.0.0.1:%d", opts.MasterPort)
	self, err := os.Executable()
	if err != nil {
		return err
	}
	master, err := startWebE2EProcess(runCtx, filepath.Join(run, "master.log"), ctx.repoRoot, nil, self,
		"master", "serve", "--listen", fmt.Sprintf("127.0.0.1:%d", opts.MasterPort),
		"--port-base", fmt.Sprint(opts.PortBase), "--rtc-port-base", fmt.Sprint(opts.RtcBase),
		"--data", filepath.Join(run, "master"), "--cheats")
	if err != nil {
		return err
	}
	defer stopWebE2EMaster(masterURL, master, out)
	if err := waitWebE2E(runCtx, masterURL+"/", 30*time.Second, func(*http.Response) bool { return true }); err != nil {
		return fmt.Errorf("Master did not start (see %s): %w", filepath.Join(run, "master.log"), err)
	}

	siteURL := fmt.Sprintf("http://127.0.0.1:%d/", opts.SitePort)
	vite := filepath.Join(webProject(ctx), "node_modules", "vite", "bin", "vite.js")
	siteEnv := append(os.Environ(), "VITE_ND_REVIEW=1", "ND_MASTER_URL="+masterURL, fmt.Sprintf("ND_WEB_PORT=%d", opts.SitePort))
	site, err := startWebE2EProcess(runCtx, filepath.Join(run, "site.log"), webProject(ctx), siteEnv, "node", vite)
	if err != nil {
		return err
	}
	defer stopWebE2EProcess(site)
	if err := waitWebE2E(runCtx, siteURL, 60*time.Second, func(r *http.Response) bool { return r.StatusCode == http.StatusOK }); err != nil {
		return fmt.Errorf("the review site did not start (see %s): %w", filepath.Join(run, "site.log"), err)
	}
	fmt.Fprintf(out, "e2e: Master %s, site %s, evidence %s\n", masterURL, siteURL, run)

	scenarioEnv := append(os.Environ(), "ND_TEST_SITE="+siteURL, "ND_TEST_MASTER="+masterURL,
		"ND_TEST_PRESET="+ctx.preset, "ND_TEST_EVIDENCE="+run, "ND_TEST_BROWSER="+opts.Browser)
	if opts.SoftwareGL {
		// A GPU-less runner: WebGL on the CPU and the desktop client on a headless surface.
		scenarioEnv = append(scenarioEnv, "ND_TEST_SOFTWARE_GL=1", "ND_TEST_HEADLESS=1")
	}
	var failed []string
	for _, scenario := range opts.Scenarios {
		started := time.Now()
		fmt.Fprintf(out, "e2e: %s ...\n", scenario)
		log, err := os.Create(filepath.Join(run, scenario+".log"))
		if err != nil {
			return err
		}
		child := webChild(runCtx, webProject(ctx), scenarioEnv, io.MultiWriter(out, log), io.MultiWriter(cmd.ErrOrStderr(), log),
			"node", filepath.Join("test", scenario+".mjs"))
		err = child.Run()
		log.Close()
		status := "passed"
		if err != nil {
			status = "FAILED: " + err.Error()
			failed = append(failed, scenario)
		}
		fmt.Fprintf(out, "e2e: %s %s in %s\n", scenario, status, time.Since(started).Round(time.Second))
	}
	if len(failed) > 0 {
		return fmt.Errorf("e2e scenarios failed: %s (logs in %s)", strings.Join(failed, ", "), run)
	}
	fmt.Fprintf(out, "e2e: all %d scenarios passed (evidence in %s)\n", len(opts.Scenarios), run)
	return nil
}

func startWebE2EProcess(ctx context.Context, logPath, dir string, env []string, program string, args ...string) (*exec.Cmd, error) {
	log, err := os.Create(logPath)
	if err != nil {
		return nil, err
	}
	child := exec.CommandContext(ctx, program, args...)
	child.Dir, child.Env, child.Stdout, child.Stderr = dir, env, log, log
	if err := child.Start(); err != nil {
		log.Close()
		return nil, err
	}
	go func() {
		_ = child.Wait()
		log.Close()
	}()
	return child, nil
}

func stopWebE2EProcess(child *exec.Cmd) {
	if child.Process != nil {
		_ = child.Process.Kill()
	}
}

// stopWebE2EMaster asks the Master to stop every map server it launched (they save and
// exit on their own) before the Master itself goes; killing it first would orphan them.
func stopWebE2EMaster(url string, master *exec.Cmd, out io.Writer) {
	defer stopWebE2EProcess(master)
	live := func() []string {
		body, err := masterCall(url, "/admin/status", nil)
		if err != nil {
			return nil
		}
		var status struct {
			Servers []struct {
				ID    string `json:"id"`
				State string `json:"state"`
			} `json:"servers"`
		}
		if json.Unmarshal(body, &status) != nil {
			return nil
		}
		var ids []string
		for _, server := range status.Servers {
			if server.State != "dead" {
				ids = append(ids, server.ID)
			}
		}
		return ids
	}
	for _, id := range live() {
		if _, err := masterCall(url, "/admin/stop", map[string]any{"server": id}); err != nil {
			fmt.Fprintf(out, "e2e: could not stop %s: %v\n", id, err)
		}
	}
	for deadline := time.Now().Add(30 * time.Second); time.Now().Before(deadline) && len(live()) > 0; {
		time.Sleep(500 * time.Millisecond)
	}
}

func waitWebE2E(ctx context.Context, url string, limit time.Duration, ready func(*http.Response) bool) error {
	client := &http.Client{Timeout: 2 * time.Second}
	deadline := time.Now().Add(limit)
	for {
		response, err := client.Get(url)
		if err == nil {
			ok := ready(response)
			response.Body.Close()
			if ok {
				return nil
			}
		} else if !isWebE2ERefused(err) && time.Now().After(deadline) {
			return err
		}
		if time.Now().After(deadline) {
			return errors.New("timed out")
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(300 * time.Millisecond):
		}
	}
}

func isWebE2ERefused(err error) bool {
	var opErr *net.OpError
	return errors.As(err, &opErr)
}
