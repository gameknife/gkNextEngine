package validate

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/validationstore"
)

func TestCompare(t *testing.T) {
	tests := []struct {
		actual   any
		op       string
		expected any
		want     bool
	}{
		{3.0, "ge", 2.0, true}, {"Running", "eq", "Running", true},
		{"abcdef", "contains", "bcd", true}, {1.0, "gt", 2.0, false},
	}
	for _, tt := range tests {
		if got := compare(tt.actual, tt.op, tt.expected); got != tt.want {
			t.Fatalf("compare(%v,%s,%v)=%v", tt.actual, tt.op, tt.expected, got)
		}
	}
}

func TestNormalizePoints(t *testing.T) {
	p := map[string]any{"to": map[string]any{"norm": []any{0.5, 0.25}}, "at": map[string]any{"px": []any{12.0, 34.0}}}
	normalizePoints(p, 1280, 720)
	to := p["to"].([]any)
	if to[0] != 640.0 || to[1] != 180.0 {
		t.Fatalf("to=%v", to)
	}
	at := p["at"].([]any)
	if at[0] != 12.0 || at[1] != 34.0 {
		t.Fatalf("at=%v", at)
	}
}

func TestRunCreatesFailureRecordBeforeReadingScript(t *testing.T) {
	root := t.TempDir()
	storeRoot := filepath.Join(root, "validation-runs")
	script := filepath.Join(root, "bad.agentscript.json")
	if err := os.WriteFile(script, []byte("{"), 0o644); err != nil {
		t.Fatal(err)
	}
	result, err := RunWithResult(context.Background(), Options{
		RepoRoot: root, Preset: "test", Script: script, ValidationRoot: storeRoot,
	})
	if err == nil || !strings.Contains(err.Error(), "unexpected end") {
		t.Fatalf("err = %v, want parse error", err)
	}
	if result.RunID == "" {
		t.Fatal("missing run id for parse failure")
	}
	run, loadErr := validationstore.NewAt(storeRoot).Load(result.RunID)
	if loadErr != nil {
		t.Fatal(loadErr)
	}
	if run.Status != validationstore.StatusFailed || run.Phase != "parse" {
		t.Fatalf("run = %+v", run)
	}
	var snapshot map[string]any
	data, readErr := os.ReadFile(filepath.Join(storeRoot, result.RunID, "script.json"))
	if readErr != nil {
		t.Fatal(readErr)
	}
	if json.Unmarshal(data, &snapshot) == nil {
		t.Fatal("invalid script unexpectedly became valid snapshot")
	}
}

func TestRunMissingExecutableCreatesLaunchFailureRecord(t *testing.T) {
	root := t.TempDir()
	storeRoot := filepath.Join(root, "validation-runs")
	script := filepath.Join(root, "smoke.json")
	if err := os.WriteFile(script, []byte(`{"name":"smoke","steps":[]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	result, err := RunWithResult(context.Background(), Options{
		RepoRoot: root, Preset: "test", Script: script, ValidationRoot: storeRoot,
	})
	if err == nil || !strings.Contains(err.Error(), "executable not found") {
		t.Fatalf("err = %v, want missing executable", err)
	}
	run, loadErr := validationstore.NewAt(storeRoot).Load(result.RunID)
	if loadErr != nil {
		t.Fatal(loadErr)
	}
	if run.Status != validationstore.StatusFailed || run.Phase != "launch" {
		t.Fatalf("run = %+v", run)
	}
}
