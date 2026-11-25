package validate

import (
	"context"
	"fmt"
	"strings"
	"time"
)

// Watch mode: the same script, paced and narrated for a person in front of the
// window. Nothing here changes what a step does; it only adds captions, pauses
// and a gliding cursor around it.

// annotate updates the on-screen caption. Older targets without the overlay
// reject the method; watching still works, just without captions.
func (c *client) annotate(o Options, fields map[string]any) {
	if !o.Watch || c.dead {
		return
	}
	_, _ = c.call("annotate", fields)
}

func sleepContext(ctx context.Context, d time.Duration) {
	if d <= 0 {
		return
	}
	timer := time.NewTimer(d)
	defer timer.Stop()
	select {
	case <-timer.C:
	case <-ctx.Done():
	}
}

// watchPauseAfter is how long to linger after a step so its effect is visible.
// Waits already take real time at display rate; actions get the full pace.
func watchPauseAfter(typ string, pace time.Duration) time.Duration {
	switch typ {
	case "wait-frames", "wait-ms", "wait-until":
		return 0
	case "assert":
		return pace / 2
	case "log":
		return pace * 2
	default:
		return pace
	}
}

// describeStep is the one-line summary shown under the step counter.
func describeStep(step map[string]any) string {
	typ := str(step, "type", "")
	switch typ {
	case "key":
		label := str(step, "code", "")
		if mods, ok := step["mods"].([]any); ok && len(mods) > 0 {
			parts := make([]string, 0, len(mods)+1)
			for _, mod := range mods {
				parts = append(parts, fmt.Sprint(mod))
			}
			label = strings.Join(append(parts, label), "+")
		}
		if action := str(step, "action", "press"); action != "press" {
			label += " (" + action + ")"
		}
		return "press " + label
	case "text":
		return fmt.Sprintf("type %q", str(step, "value", ""))
	case "mouse-move":
		if boolv(step, "relative", false) {
			return "turn the view"
		}
		return "move the cursor"
	case "click":
		return str(step, "button", "left") + " click"
	case "mouse-button":
		return str(step, "button", "left") + " button " + str(step, "action", "press")
	case "drag":
		return "drag"
	case "scroll":
		return fmt.Sprintf("scroll %v", step["y"])
	case "wait-frames":
		return fmt.Sprintf("wait %v frames", step["n"])
	case "wait-ms":
		return fmt.Sprintf("wait %v ms", step["ms"])
	case "wait-until":
		return fmt.Sprintf("wait until %s %s %v", str(step, "query", ""), str(step, "op", "eq"), step["value"])
	case "assert":
		return fmt.Sprintf("check %s %s %v", str(step, "query", ""), str(step, "op", "eq"), step["value"])
	case "cvar":
		if value, ok := step["set"]; ok {
			return fmt.Sprintf("set %s = %v", str(step, "name", ""), value)
		}
		return "read " + str(step, "name", "")
	case "exec":
		return str(step, "line", "")
	case "log":
		return str(step, "message", str(step, "text", ""))
	case "screenshot":
		return "screenshot"
	case "quit":
		return "end of script (watch mode keeps the window open)"
	}
	return typ
}

// stepAnnotation is the caption for step i of total.
func stepAnnotation(i, total int, step map[string]any) map[string]any {
	detail := describeStep(step)
	if note := str(step, "description", str(step, "comment", "")); note != "" && str(step, "type", "") != "log" {
		detail += "  —  " + note
	}
	return map[string]any{
		"caption": fmt.Sprintf("Step %d/%d · %s", i+1, total, str(step, "type", "")),
		"detail":  detail,
		"status":  "running",
	}
}

func toPoint(v any) (float64, float64, bool) {
	values, ok := v.([]any)
	if !ok || len(values) != 2 {
		return 0, 0, false
	}
	x, xok := values[0].(float64)
	y, yok := values[1].(float64)
	if !xok || !yok {
		// normalizePoints produces float64; JSON numbers decode as float64 too.
		return 0, 0, false
	}
	return x, y, true
}

// glideTo moves the cursor to (x, y) in small steps so a watcher can follow it.
// The first absolute move of a run starts from the middle of the viewport.
func (c *client) glideTo(ctx context.Context, o Options, x, y float64) {
	if c.pointer == nil {
		width, height := c.pointSpace(o)
		if width <= 0 || height <= 0 {
			c.pointer = &[2]float64{x, y}
			return
		}
		c.pointer = &[2]float64{float64(width) / 2, float64(height) / 2}
	}
	const segments = 14
	from := *c.pointer
	duration := o.Pace / 2
	for i := 1; i <= segments && !c.dead && ctx.Err() == nil; i++ {
		t := float64(i) / segments
		t = t * t * (3 - 2*t) // ease in and out
		px := from[0] + (x-from[0])*t
		py := from[1] + (y-from[1])*t
		_, _ = c.call("mouse-move", map[string]any{"to": []any{px, py}})
		sleepContext(ctx, duration/segments)
	}
	c.pointer = &[2]float64{x, y}
}

// watchGlide runs before an input step in watch mode: it brings the cursor to
// where the step acts, then remembers where the step leaves it.
func (c *client) watchGlide(ctx context.Context, o Options, typ string, p map[string]any) {
	if !o.Watch {
		return
	}
	var first string
	switch typ {
	case "mouse-move":
		if boolv(p, "relative", false) {
			return
		}
		first = "to"
	case "click", "mouse-button":
		first = "at"
	case "drag":
		first = "from"
	default:
		return
	}
	if x, y, ok := toPoint(p[first]); ok {
		c.glideTo(ctx, o, x, y)
	}
	if typ == "drag" {
		if x, y, ok := toPoint(p["to"]); ok {
			c.pointer = &[2]float64{x, y}
		}
	}
}
