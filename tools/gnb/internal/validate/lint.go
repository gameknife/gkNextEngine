package validate

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// Lint checks an agent script before anything is launched. Input steps are
// forwarded to the engine verbatim and the engine reads them with
// params.value(key, default), so a misspelled field never fails on its own:
// it silently becomes the default. Lint turns those silent mistakes into errors.

// Issue is one lint finding. Step is the zero-based step index, or -1 for the
// script as a whole.
type Issue struct {
	Error   bool
	Step    int
	Message string
}

func (i Issue) String() string {
	level := "warning"
	if i.Error {
		level = "error"
	}
	if i.Step < 0 {
		return fmt.Sprintf("%s: %s", level, i.Message)
	}
	return fmt.Sprintf("%s: step #%d: %s", level, i.Step, i.Message)
}

// LintOptions carries what the linter cannot see in the script itself.
type LintOptions struct {
	// RepoRoot enables the scene-exists check. Empty skips it.
	RepoRoot string
}

var (
	topLevelFields = fieldSet("name", "target", "scene", "args", "defaults", "viewport", "steps", "waitReady",
		"tags", "comment", "description")
	defaultsFields = fieldSet("waitFrames", "stepTimeoutMs")
	viewportFields = fieldSet("width", "height")
	commonFields   = fieldSet("type", "comment", "description", "timeoutMs", "fatal")

	// stepFields lists, per step type, the fields the runner or the engine reads.
	// Keep it in sync with execute() and FAgentControlService::HandleCommand.
	stepFields = map[string]map[string]bool{
		"wait-frames":  fieldSet("n", "readyQuery"),
		"wait-ms":      fieldSet("ms"),
		"wait-until":   fieldSet("query", "op", "value"),
		"assert":       fieldSet("query", "op", "value"),
		"cvar":         fieldSet("name", "set"),
		"exec":         fieldSet("line"),
		"log":          fieldSet("message", "text"),
		"quit":         fieldSet(),
		"screenshot":   fieldSet("out", "ui", "accumulateFrames", "quitAfterCapture"),
		"key":          fieldSet("code", "action", "mods"),
		"text":         fieldSet("value"),
		"mouse-move":   fieldSet("to", "relative"),
		"click":        fieldSet("at", "button", "count"),
		"mouse-button": fieldSet("at", "button", "count", "action"),
		"drag":         fieldSet("from", "to", "button"),
		"scroll":       fieldSet("x", "y"),
	}
	requiredFields = map[string][]string{
		"wait-ms":    {"ms"},
		"wait-until": {"query", "value"},
		"assert":     {"query", "value"},
		"cvar":       {"name"},
		"exec":       {"line"},
		"key":        {"code"},
		"text":       {"value"},
		"mouse-move": {"to"},
		"drag":       {"from", "to"},
	}
	compareOps   = fieldSet("eq", "ne", "gt", "ge", "lt", "le", "contains")
	pressActions = fieldSet("press", "down", "up")
	mouseButtons = fieldSet("left", "right", "middle", "x1", "x2")
)

func fieldSet(names ...string) map[string]bool {
	set := make(map[string]bool, len(names))
	for _, name := range names {
		set[name] = true
	}
	return set
}

// isCommentKey lets authors annotate scripts with "_comment"-style keys anywhere.
func isCommentKey(key string) bool { return strings.HasPrefix(key, "_") }

// Lint parses data and returns every issue it finds, errors first.
func Lint(data []byte, o LintOptions) []Issue {
	var raw map[string]any
	if err := json.Unmarshal(data, &raw); err != nil {
		return []Issue{{Error: true, Step: -1, Message: "invalid JSON: " + err.Error()}}
	}
	var issues []Issue
	errorf := func(step int, format string, args ...any) {
		issues = append(issues, Issue{Error: true, Step: step, Message: fmt.Sprintf(format, args...)})
	}
	warnf := func(step int, format string, args ...any) {
		issues = append(issues, Issue{Step: step, Message: fmt.Sprintf(format, args...)})
	}

	for _, key := range sortedKeys(raw) {
		if !topLevelFields[key] && !isCommentKey(key) {
			errorf(-1, "unknown top-level field %q (it would be ignored)", key)
		}
	}
	checkObjectFields(raw, "defaults", defaultsFields, errorf)
	checkObjectFields(raw, "viewport", viewportFields, errorf)
	if v, ok := raw["waitReady"]; ok {
		if _, isBool := v.(bool); !isBool {
			errorf(-1, "waitReady must be true or false")
		}
	}
	if v, ok := raw["tags"]; ok {
		tags, isList := v.([]any)
		if !isList {
			errorf(-1, "tags must be an array of strings")
		}
		for _, tag := range tags {
			if s, isString := tag.(string); !isString || strings.TrimSpace(s) == "" || strings.ContainsAny(s, " \t") {
				errorf(-1, "tags must be non-empty strings without spaces, got %v", tag)
			}
		}
	}

	if scene, _ := raw["scene"].(string); scene != "" && o.RepoRoot != "" && !strings.HasSuffix(scene, ".proc") {
		path := scene
		if !filepath.IsAbs(path) {
			path = filepath.Join(o.RepoRoot, filepath.FromSlash(scene))
		}
		if _, err := os.Stat(path); err != nil {
			warnf(-1, "scene %q does not exist (generated or optional pak content?)", scene)
		}
	}

	stepsRaw, present := raw["steps"]
	steps, isList := stepsRaw.([]any)
	switch {
	case !present:
		errorf(-1, "missing \"steps\"")
	case !isList:
		errorf(-1, "\"steps\" must be an array")
	case len(steps) == 0:
		errorf(-1, "\"steps\" is empty")
	}
	for i, item := range steps {
		step, ok := item.(map[string]any)
		if !ok {
			errorf(i, "step must be an object")
			continue
		}
		typ, _ := step["type"].(string)
		allowed, known := stepFields[typ]
		if !known {
			if typ == "" {
				errorf(i, "missing \"type\"")
			} else {
				errorf(i, "unknown step type %q", typ)
			}
			continue
		}
		for _, key := range sortedKeys(step) {
			if allowed[key] || commonFields[key] || isCommentKey(key) {
				continue
			}
			errorf(i, "%s: unknown field %q (it would be ignored)%s", typ, key, fieldHint(typ, key))
		}
		for _, key := range requiredFields[typ] {
			if _, ok := step[key]; !ok {
				errorf(i, "%s: missing %q", typ, key)
			}
		}
		if op, ok := step["op"]; ok {
			if s, _ := op.(string); !compareOps[s] {
				errorf(i, "%s: unknown op %v (use eq/ne/gt/ge/lt/le/contains)", typ, op)
			}
		}
		for _, key := range []string{"query", "readyQuery"} {
			// Query names live in the engine (NextValidationModule Query and game
			// registrations); an unknown one fails at runtime with the engine's reason.
			if v, ok := step[key]; ok {
				if q, isString := v.(string); !isString || q == "" {
					errorf(i, "%s: %s must be a non-empty string", typ, key)
				}
			}
		}
		if action, ok := step["action"]; ok {
			if s, _ := action.(string); !pressActions[s] {
				errorf(i, "%s: unknown action %v (use press/down/up)", typ, action)
			}
		}
		if button, ok := step["button"]; ok {
			if s, _ := button.(string); !mouseButtons[s] {
				errorf(i, "%s: unknown button %v (use left/right/middle/x1/x2)", typ, button)
			}
		}
		for _, key := range []string{"to", "at", "from"} {
			point, ok := step[key]
			if !ok {
				continue
			}
			if msg := checkPoint(point, typ == "mouse-move" && step["relative"] == true); msg != "" {
				errorf(i, "%s.%s: %s", typ, key, msg)
			}
		}
	}
	sort.SliceStable(issues, func(a, b int) bool { return issues[a].Error && !issues[b].Error })
	return issues
}

// FileLint is the lint result for one script file.
type FileLint struct {
	Path   string
	Issues []Issue
}

// LintPaths lints every *.agentscript.json named by paths. A directory contributes
// the scripts directly inside it (fixtures in subdirectories are not scripts).
func LintPaths(paths []string, repoRoot string) ([]FileLint, error) {
	files, err := expandScriptPaths(paths)
	if err != nil {
		return nil, err
	}
	results := make([]FileLint, 0, len(files))
	for _, file := range files {
		data, err := os.ReadFile(file)
		if err != nil {
			results = append(results, FileLint{Path: file, Issues: []Issue{{Error: true, Step: -1, Message: err.Error()}}})
			continue
		}
		results = append(results, FileLint{Path: file, Issues: Lint(data, LintOptions{RepoRoot: repoRoot})})
	}
	return results, nil
}

// LintErrors reports whether any issue is an error.
func LintErrors(issues []Issue) bool {
	for _, issue := range issues {
		if issue.Error {
			return true
		}
	}
	return false
}

func checkObjectFields(raw map[string]any, name string, allowed map[string]bool, errorf func(int, string, ...any)) {
	value, present := raw[name]
	if !present {
		return
	}
	obj, ok := value.(map[string]any)
	if !ok {
		errorf(-1, "%q must be an object", name)
		return
	}
	for _, key := range sortedKeys(obj) {
		if !allowed[key] && !isCommentKey(key) {
			errorf(-1, "%s: unknown field %q (it would be ignored)", name, key)
		}
	}
}

func checkPoint(point any, relative bool) string {
	switch p := point.(type) {
	case []any:
		if !isNumberPair(p) {
			return "expected [x, y]"
		}
	case map[string]any:
		switch {
		case p["norm"] != nil:
			pair, _ := p["norm"].([]any)
			if !isNumberPair(pair) {
				return "norm expects [x, y] in 0..1"
			}
			if relative {
				return "norm cannot be used with relative mouse motion"
			}
		case p["px"] != nil:
			pair, _ := p["px"].([]any)
			if !isNumberPair(pair) {
				return "px expects [x, y]"
			}
		case p["target"] != nil:
			if _, ok := p["target"].(string); !ok {
				return "target must be a string"
			}
		default:
			return "expected [x, y], {\"norm\": [x, y]}, {\"px\": [x, y]} or {\"target\": \"name\"}"
		}
	default:
		return "expected [x, y], {\"norm\": [x, y]}, {\"px\": [x, y]} or {\"target\": \"name\"}"
	}
	return ""
}

func isNumberPair(values []any) bool {
	if len(values) != 2 {
		return false
	}
	for _, v := range values {
		if _, ok := v.(float64); !ok {
			return false
		}
	}
	return true
}

// fieldHint names the field the author most likely meant for the classic mistakes.
func fieldHint(typ, key string) string {
	switch {
	case (typ == "click" || typ == "mouse-button") && (key == "x" || key == "y"):
		return "; coordinates go in \"at\", e.g. {\"at\": {\"norm\": [0.5, 0.5]}}"
	case typ == "key" && key == "key":
		return "; the key name goes in \"code\""
	case typ == "mouse-move" && key == "durationFrames":
		return "; for a smooth move write several mouse-move steps"
	}
	return ""
}

func sortedKeys(m map[string]any) []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}
