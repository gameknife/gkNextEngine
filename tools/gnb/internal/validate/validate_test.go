package validate

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/validationstore"
)

func queryClient(t *testing.T, respond func(string) (any, error)) *client {
	t.Helper()
	conn, server := net.Pipe()
	done := make(chan struct{})
	go func() {
		defer close(done)
		defer server.Close()
		decoder, encoder := json.NewDecoder(server), json.NewEncoder(server)
		for {
			var request struct{ Params struct{ Query string } }
			if decoder.Decode(&request) != nil {
				return
			}
			value, err := respond(request.Params.Query)
			response := map[string]any{"result": value}
			if err != nil {
				response["error"] = err.Error()
			}
			if encoder.Encode(response) != nil {
				return
			}
		}
	}()
	t.Cleanup(func() { conn.Close(); <-done })
	return &client{conn: conn, rw: bufio.NewReadWriter(bufio.NewReader(conn), bufio.NewWriter(conn))}
}

func TestWaitFramesCountsOnlyAfterReadinessAndRestartsForLateUploads(t *testing.T) {
	ready := []bool{false, false, true, true, false, true, true, true}
	frames := []int{1000, 1004, 2000, 2004, 2005}
	readyIndex, frameIndex := 0, 0
	c := queryClient(t, func(q string) (any, error) {
		if q == "scene.ready" && readyIndex < len(ready) {
			value := ready[readyIndex]
			readyIndex++
			return value, nil
		}
		if q == "engine.totalFrames" && frameIndex < len(frames) {
			value := frames[frameIndex]
			frameIndex++
			return value, nil
		}
		return nil, fmt.Errorf("unexpected query %s", q)
	})
	_, err := execute(context.Background(), c, map[string]any{
		"type": "wait-frames", "n": 5, "readyQuery": "scene.ready", "timeoutMs": 2000,
	}, Script{}, Options{}, 0)
	if err != nil || readyIndex != len(ready) || frameIndex != len(frames) {
		t.Fatalf("err=%v, readiness polls=%d, frame polls=%d", err, readyIndex, frameIndex)
	}
}

func TestWaitFramesRestartsWhenSceneResetsFrameCounter(t *testing.T) {
	frames := []int{10, 12, 1, 3, 5, 6}
	index := 0
	c := queryClient(t, func(q string) (any, error) {
		if q != "engine.totalFrames" || index >= len(frames) {
			return nil, fmt.Errorf("unexpected query %s", q)
		}
		value := frames[index]
		index++
		return value, nil
	})
	_, err := execute(context.Background(), c, map[string]any{
		"type": "wait-frames", "n": 5, "timeoutMs": 2000,
	}, Script{}, Options{}, 0)
	if err != nil || index != len(frames) {
		t.Fatalf("err=%v, frame polls=%d", err, index)
	}
}

func TestWaitFramesPropagatesSceneLoadFailure(t *testing.T) {
	c := queryClient(t, func(q string) (any, error) {
		return nil, fmt.Errorf("failed to load scene [missing.glb]")
	})
	_, err := execute(context.Background(), c, map[string]any{
		"type": "wait-frames", "n": 5, "readyQuery": "scene.ready",
	}, Script{}, Options{}, 0)
	if err == nil || !strings.Contains(err.Error(), "missing.glb") {
		t.Fatalf("err=%v, want scene load failure", err)
	}
}

func TestShotWaitsForReadinessEvenWithoutExplicitScene(t *testing.T) {
	for _, scene := range []string{"", "assets/models/playground.glb"} {
		t.Run(scene, func(t *testing.T) {
			root := t.TempDir()
			result, err := ShotWithResult(context.Background(), Options{
				RepoRoot: root, Preset: "test", Scene: scene, ValidationRoot: root,
			}, 1, false, "shot")
			if err == nil || !strings.Contains(err.Error(), "executable not found") {
				t.Fatalf("err=%v, want missing executable", err)
			}
			data, err := os.ReadFile(filepath.Join(result.EvidenceDir, "script.json"))
			if err != nil {
				t.Fatal(err)
			}
			var script Script
			if err := json.Unmarshal(data, &script); err != nil {
				t.Fatal(err)
			}
			if len(script.Steps) != 3 || script.Steps[0]["query"] != "scene.ready" ||
				script.Steps[1]["readyQuery"] != "scene.ready" || script.Steps[1]["n"] != float64(1) ||
				script.Steps[0]["fatal"] != true || script.Steps[1]["fatal"] != true {
				t.Fatalf("shot must gate capture on readiness and ready frames: %s", data)
			}
		})
	}
}

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
