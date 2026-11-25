package validate

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"net"
	"strings"
	"testing"
)

func lintMessages(t *testing.T, script string, o LintOptions) []string {
	t.Helper()
	var messages []string
	for _, issue := range Lint([]byte(script), o) {
		messages = append(messages, issue.String())
	}
	return messages
}

func requireIssue(t *testing.T, messages []string, fragment string) {
	t.Helper()
	for _, message := range messages {
		if strings.Contains(message, fragment) {
			return
		}
	}
	t.Fatalf("expected an issue containing %q, got:\n%s", fragment, strings.Join(messages, "\n"))
}

func TestLintAcceptsAValidScript(t *testing.T) {
	script := `{
		"name": "ok", "target": "gkNextRenderer", "_comment": "authors may annotate",
		"viewport": { "width": 1280, "height": 720 },
		"steps": [
			{ "type": "wait-until", "query": "scene.ready", "value": true, "timeoutMs": 60000, "fatal": true },
			{ "type": "click", "at": { "norm": [0.5, 0.5] }, "comment": "center" },
			{ "type": "drag", "from": [10, 10], "to": { "px": [20, 20] }, "button": "right" },
			{ "type": "mouse-move", "relative": true, "to": [40, 0] },
			{ "type": "key", "code": "W", "action": "down", "mods": "CTRL" },
			{ "type": "assert", "query": "ambient.scrollCount", "op": "ge", "value": 1, "_why": "custom query family" },
			{ "type": "screenshot", "out": "screenshots/ok", "ui": true }
		]
	}`
	if messages := lintMessages(t, script, LintOptions{}); len(messages) != 0 {
		t.Fatalf("valid script reported issues:\n%s", strings.Join(messages, "\n"))
	}
}

func TestLintCatchesTheDocumentedSilentMistakes(t *testing.T) {
	script := `{
		"name": "bad", "viewportt": {},
		"steps": [
			{ "type": "click", "x": 100, "y": 200 },
			{ "type": "key", "key": "W" },
			{ "type": "mouse-move", "to": { "norm": [0.5, 0.5] }, "durationFrames": 20 },
			{ "type": "wait-untill", "query": "scene.ready", "value": true },
			{ "type": "assert", "query": "scene.nodeCount", "op": "gte", "value": 1 },
			{ "type": "mouse-button", "button": "centre" }
		]
	}`
	messages := lintMessages(t, script, LintOptions{})
	requireIssue(t, messages, `unknown top-level field "viewportt"`)
	requireIssue(t, messages, `step #0: click: unknown field "x"`)
	requireIssue(t, messages, `coordinates go in "at"`)
	requireIssue(t, messages, `step #1: key: unknown field "key"`)
	requireIssue(t, messages, `step #1: key: missing "code"`)
	requireIssue(t, messages, `step #2: mouse-move: unknown field "durationFrames"`)
	requireIssue(t, messages, `step #3: unknown step type "wait-untill"`)
	requireIssue(t, messages, `step #4: assert: unknown op gte`)
	requireIssue(t, messages, `step #5: mouse-button: unknown button centre`)
	if !LintErrors(Lint([]byte(script), LintOptions{})) {
		t.Fatal("LintErrors should report errors")
	}
}

func TestNormResolvesAgainstTheReportedWindowSize(t *testing.T) {
	// The engine reports 1600x900 window coordinates for a 1280x720 viewport on a
	// 125% Windows display; the center must land at the window's center.
	var moved []any
	c := queryClient(t, func(q string) (any, error) {
		switch q {
		case "engine.windowWidth":
			return 1600, nil
		case "engine.windowHeight":
			return 900, nil
		}
		return map[string]any{"ok": true}, nil
	})
	c.readWindowSize()
	p := map[string]any{"to": map[string]any{"norm": []any{0.5, 0.5}}}
	width, height := c.pointSpace(Options{Width: 1280, Height: 720})
	normalizePoints(p, width, height)
	moved = p["to"].([]any)
	if moved[0] != 800.0 || moved[1] != 450.0 {
		t.Fatalf("norm center resolved to %v, want [800 450]", moved)
	}
}

func TestNormWithoutAnyWindowSizeFailsInsteadOfClickingTheCorner(t *testing.T) {
	c := queryClient(t, func(string) (any, error) { return map[string]any{"ok": true}, nil })
	_, err := execute(context.Background(), c, map[string]any{"type": "click", "at": map[string]any{"norm": []any{0.5, 0.5}}}, Script{}, Options{}, 0)
	if err == nil || !strings.Contains(err.Error(), "norm coordinates need the window size") {
		t.Fatalf("err = %v", err)
	}
}

func TestLintRejectsMalformedScripts(t *testing.T) {
	requireIssue(t, lintMessages(t, `{ "steps": [ `, LintOptions{}), "invalid JSON")
	requireIssue(t, lintMessages(t, `{ "name": "x" }`, LintOptions{}), `missing "steps"`)
	requireIssue(t, lintMessages(t, `{ "steps": [] }`, LintOptions{}), `"steps" is empty`)
	requireIssue(t, lintMessages(t, `{ "waitReady": "no", "steps": [ { "type": "quit" } ] }`, LintOptions{}), "waitReady must be true or false")
}

func TestAssertReportsTheEngineQueryError(t *testing.T) {
	c := queryClient(t, func(q string) (any, error) {
		return nil, fmt.Errorf("unknown query %s", q)
	})
	_, err := execute(context.Background(), c, map[string]any{"type": "assert", "query": "scene.nodeCuont", "op": "gt", "value": 0}, Script{}, Options{}, 0)
	if err == nil || !strings.Contains(err.Error(), "query scene.nodeCuont: unknown query scene.nodeCuont") {
		t.Fatalf("assert error = %v, want the engine's reason", err)
	}
}

func TestAssertFailureNamesTheComparison(t *testing.T) {
	c := queryClient(t, func(string) (any, error) { return 95, nil })
	_, err := execute(context.Background(), c, map[string]any{"type": "assert", "query": "scene.nodeCount", "op": "gt", "value": 999999}, Script{}, Options{}, 0)
	if err == nil || !strings.Contains(err.Error(), "scene.nodeCount gt 999999, actual 95") {
		t.Fatalf("assert error = %v", err)
	}
}

func TestControlChannelLossMarksTheClientDead(t *testing.T) {
	conn, server := net.Pipe()
	server.Close()
	c := &client{conn: conn, rw: bufio.NewReadWriter(bufio.NewReader(conn), bufio.NewWriter(conn))}
	if _, err := c.call("query", map[string]any{"query": "engine.status"}); !errors.Is(err, errControlLost) {
		t.Fatalf("first call error = %v, want errControlLost", err)
	}
	if !c.dead {
		t.Fatal("client should be marked dead after a transport failure")
	}
	if _, err := c.call("query", map[string]any{"query": "engine.status"}); !errors.Is(err, errControlLost) {
		t.Fatalf("later call error = %v, want errControlLost without touching the socket", err)
	}
}
