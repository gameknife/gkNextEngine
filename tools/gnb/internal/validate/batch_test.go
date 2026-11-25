package validate

import (
	"context"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

func writeScript(t *testing.T, dir, name, body string) string {
	t.Helper()
	path := filepath.Join(dir, name+".agentscript.json")
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestSelectScriptsByTagAndTarget(t *testing.T) {
	dir := t.TempDir()
	writeScript(t, dir, "a", `{"name":"a","tags":["smoke","ui"],"steps":[{"type":"quit"}]}`)
	writeScript(t, dir, "b", `{"name":"b","target":"NextDayz","tags":["smoke"],"steps":[{"type":"quit"}]}`)
	writeScript(t, dir, "c", `{"name":"c","steps":[{"type":"quit"}]}`)
	if err := os.MkdirAll(filepath.Join(dir, "fixtures"), 0o755); err != nil {
		t.Fatal(err)
	}
	writeScript(t, filepath.Join(dir, "fixtures"), "ignored", `{"tags":["smoke"]}`)

	names := func(scripts []BatchScript) []string {
		var out []string
		for _, s := range scripts {
			out = append(out, s.Name+"@"+s.Target)
		}
		return out
	}
	all, err := SelectScripts([]string{dir}, nil, "")
	if err != nil || len(all) != 3 {
		t.Fatalf("all = %v, %v", names(all), err)
	}
	smoke, _ := SelectScripts([]string{dir}, []string{"smoke"}, "")
	if got := names(smoke); len(got) != 2 || got[0] != "a@gkNextRenderer" || got[1] != "b@NextDayz" {
		t.Fatalf("smoke = %v", got)
	}
	both, _ := SelectScripts([]string{dir}, []string{"smoke", "ui"}, "")
	if got := names(both); len(got) != 1 || got[0] != "a@gkNextRenderer" {
		t.Fatalf("smoke+ui = %v", got)
	}
	dayz, _ := SelectScripts([]string{dir}, []string{"smoke"}, "NextDayz")
	if got := names(dayz); len(got) != 1 || got[0] != "b@NextDayz" {
		t.Fatalf("NextDayz smoke = %v", got)
	}
	if targets := BatchTargets(smoke); len(targets) != 2 || targets[0] != "NextDayz" {
		t.Fatalf("targets = %v", targets)
	}
}

func TestRunBatchKeepsGoingAndCounts(t *testing.T) {
	scripts := []BatchScript{{Name: "ok", Path: "ok.json", Target: "T"}, {Name: "bad", Path: "bad.json", Target: "T"}, {Name: "ok2", Path: "ok2.json", Target: "T"}}
	var seen []string
	result := RunBatch(context.Background(), BatchOptions{
		ReportDir: t.TempDir(),
		run: func(_ context.Context, o Options) (Result, error) {
			seen = append(seen, filepath.Base(o.Report))
			if !o.Batch {
				t.Error("runs inside a batch must be marked Batch")
			}
			if o.Script == "bad.json" {
				return Result{StepsTotal: 3, StepsRun: 3, StepsFailed: 1, Failure: "#1 assert: nope"}, errors.New("agent validation failed")
			}
			return Result{StepsTotal: 2, StepsRun: 2}, nil
		},
	}, scripts)
	if len(seen) != 3 || seen[1] != "bad.json" {
		t.Fatalf("runs = %v; a failure must not stop the batch", seen)
	}
	if result.Passed != 2 || result.Failed != 1 || result.Skipped != 0 {
		t.Fatalf("counts = %+v", result)
	}
	if bad := result.Entries[1]; bad.Status != "failed" || bad.Failure != "#1 assert: nope" {
		t.Fatalf("failed entry = %+v", bad)
	}
	path := filepath.Join(t.TempDir(), "batch.json")
	if err := WriteBatchReport(path, result); err != nil {
		t.Fatal(err)
	}
}

func TestProcessWaiterSeesAnEarlyExit(t *testing.T) {
	cmd := exec.Command(os.Args[0], "-test.run=^$")
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	proc := newProcessWaiter(cmd)
	select {
	case <-proc.done:
	case <-time.After(30 * time.Second):
		t.Fatal("waiter never saw the process exit")
	}
	if !proc.exited() || proc.exitCode() == nil || *proc.exitCode() != 0 {
		t.Fatalf("exited=%v code=%v", proc.exited(), proc.exitCode())
	}
	if err := proc.kill(); err != nil {
		t.Fatalf("kill after exit should return the exit error (nil), got %v", err)
	}
}
