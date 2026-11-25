package validate

import (
	"context"
	"strings"
	"testing"
	"time"
)

func TestWatchGlideStartsAtViewportCenterAndEndsOnTarget(t *testing.T) {
	calls := 0
	c := queryClient(t, func(string) (any, error) { calls++; return map[string]any{"ok": true}, nil })
	o := Options{Watch: true, Pace: 10 * time.Millisecond, Width: 1000, Height: 500}

	c.watchGlide(context.Background(), o, "click", map[string]any{"at": []any{900.0, 100.0}})
	if calls != 14 {
		t.Fatalf("glide sent %d moves, want 14", calls)
	}
	if c.pointer == nil || c.pointer[0] != 900 || c.pointer[1] != 100 {
		t.Fatalf("pointer = %v, want the click target", c.pointer)
	}

	c.watchGlide(context.Background(), o, "drag", map[string]any{"from": []any{10.0, 10.0}, "to": []any{20.0, 30.0}})
	if c.pointer[0] != 20 || c.pointer[1] != 30 {
		t.Fatalf("after a drag the pointer should rest on its end, got %v", c.pointer)
	}

	before := calls
	c.watchGlide(context.Background(), o, "mouse-move", map[string]any{"to": []any{5.0, 5.0}, "relative": true})
	c.watchGlide(context.Background(), Options{}, "click", map[string]any{"at": []any{1.0, 1.0}})
	if calls != before {
		t.Fatal("relative moves and non-watch runs must not glide")
	}
}

func TestWatchCaptions(t *testing.T) {
	step := map[string]any{"type": "key", "code": "S", "mods": []any{"CTRL"}, "comment": "save"}
	annotation := stepAnnotation(2, 10, step)
	if annotation["caption"] != "Step 3/10 · key" {
		t.Fatalf("caption = %v", annotation["caption"])
	}
	if detail := annotation["detail"].(string); !strings.Contains(detail, "press CTRL+S") || !strings.Contains(detail, "save") {
		t.Fatalf("detail = %q", detail)
	}
	if got := describeStep(map[string]any{"type": "assert", "query": "scene.ready", "value": true}); got != "check scene.ready eq true" {
		t.Fatalf("assert description = %q", got)
	}
	if watchPauseAfter("wait-frames", time.Second) != 0 || watchPauseAfter("log", time.Second) != 2*time.Second {
		t.Fatal("waits should not pause; log captions should linger")
	}
}
