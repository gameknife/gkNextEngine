package validate

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

// Batch runs: many scripts back to back, selected by tag and target, with one
// summary at the end (`gnb validate --all --tag smoke`).

// BatchScript is one selectable script.
type BatchScript struct {
	Path   string   `json:"path"`
	Name   string   `json:"name"`
	Target string   `json:"target"`
	Tags   []string `json:"tags,omitempty"`
}

// BatchOptions selects and configures a batch.
type BatchOptions struct {
	// Paths are script files or directories (scripts directly inside).
	Paths []string
	// Tags: a script must carry every one of them.
	Tags []string
	// Target, when set, keeps only scripts for that executable.
	Target string
	// Base is applied to every run: repo, preset, args, timeouts, verbosity.
	Base Options
	// ReportDir receives each run's <name>.json report.
	ReportDir string
	// run executes one script; tests replace it.
	run func(context.Context, Options) (Result, error)
}

// BatchEntry is the outcome of one script.
type BatchEntry struct {
	BatchScript
	Status      string  `json:"status"` // passed | failed | skipped
	StepsTotal  int     `json:"stepsTotal"`
	StepsRun    int     `json:"stepsRun"`
	StepsFailed int     `json:"stepsFailed"`
	Seconds     float64 `json:"seconds"`
	RunID       string  `json:"runId,omitempty"`
	EvidenceDir string  `json:"evidenceDir,omitempty"`
	ReportPath  string  `json:"reportPath,omitempty"`
	Failure     string  `json:"failure,omitempty"`
}

// BatchResult is the whole batch.
type BatchResult struct {
	Tags    []string     `json:"tags,omitempty"`
	Target  string       `json:"target,omitempty"`
	Entries []BatchEntry `json:"entries"`
	Passed  int          `json:"passed"`
	Failed  int          `json:"failed"`
	Skipped int          `json:"skipped"`
	Seconds float64      `json:"seconds"`
}

func expandScriptPaths(paths []string) ([]string, error) {
	var files []string
	for _, path := range paths {
		info, err := os.Stat(path)
		if err != nil {
			return nil, err
		}
		if !info.IsDir() {
			files = append(files, path)
			continue
		}
		matches, err := filepath.Glob(filepath.Join(path, "*.agentscript.json"))
		if err != nil {
			return nil, err
		}
		files = append(files, matches...)
	}
	sort.Strings(files)
	return files, nil
}

// SelectScripts lists the scripts under paths that carry every tag and, when
// target is set, run that target. Unreadable scripts are still selected so the
// batch reports them instead of silently dropping them.
func SelectScripts(paths, tags []string, target string) ([]BatchScript, error) {
	files, err := expandScriptPaths(paths)
	if err != nil {
		return nil, err
	}
	var selected []BatchScript
	for _, file := range files {
		script := BatchScript{Path: file, Name: strings.TrimSuffix(filepath.Base(file), ".agentscript.json"), Target: "gkNextRenderer"}
		var s Script
		if data, readErr := os.ReadFile(file); readErr == nil && json.Unmarshal(data, &s) == nil {
			if s.Name != "" {
				script.Name = s.Name
			}
			if s.Target != "" {
				script.Target = s.Target
			}
			script.Tags = s.Tags
		} else if len(tags) > 0 {
			continue
		}
		if target != "" && script.Target != target {
			continue
		}
		if !hasAllTags(script.Tags, tags) {
			continue
		}
		selected = append(selected, script)
	}
	return selected, nil
}

func hasAllTags(have, want []string) bool {
	for _, tag := range want {
		found := false
		for _, candidate := range have {
			if candidate == tag {
				found = true
				break
			}
		}
		if !found {
			return false
		}
	}
	return true
}

// BatchTargets is the distinct targets a selection needs, for building them first.
func BatchTargets(scripts []BatchScript) []string {
	seen := map[string]bool{}
	var targets []string
	for _, script := range scripts {
		if !seen[script.Target] {
			seen[script.Target] = true
			targets = append(targets, script.Target)
		}
	}
	sort.Strings(targets)
	return targets
}

// RunBatch runs the selected scripts one after another. A failing script does not
// stop the batch; a target that is not built is skipped and reported.
func RunBatch(ctx context.Context, o BatchOptions, scripts []BatchScript) BatchResult {
	run := o.run
	if run == nil {
		run = RunWithResult
	}
	result := BatchResult{Tags: o.Tags, Target: o.Target}
	started := time.Now()
	binDir := platform.BinDir(o.Base.RepoRoot, o.Base.Preset)
	for i, script := range scripts {
		if ctx.Err() != nil {
			break
		}
		entry := BatchEntry{BatchScript: script}
		if o.run == nil {
			if _, err := os.Stat(platform.ExecutablePath(binDir, script.Target)); err != nil {
				entry.Status = "skipped"
				entry.Failure = "not built (gnb build " + script.Target + ", or pass --build)"
				result.Skipped++
				result.Entries = append(result.Entries, entry)
				console.Muted("(%d/%d) %s: skipped, %s is not built", i+1, len(scripts), script.Name, script.Target)
				continue
			}
		}
		console.Info("(%d/%d) %s [%s]", i+1, len(scripts), script.Name, script.Target)
		runOptions := o.Base
		runOptions.Script = script.Path
		runOptions.Batch = true
		if o.ReportDir != "" {
			runOptions.Report = filepath.Join(o.ReportDir, script.Name+".json")
		}
		begin := time.Now()
		runResult, err := run(ctx, runOptions)
		entry.Seconds = time.Since(begin).Seconds()
		entry.RunID, entry.EvidenceDir, entry.ReportPath = runResult.RunID, runResult.EvidenceDir, runResult.ReportPath
		entry.StepsTotal, entry.StepsRun, entry.StepsFailed = runResult.StepsTotal, runResult.StepsRun, runResult.StepsFailed
		if err != nil {
			entry.Status = "failed"
			entry.Failure = runResult.Failure
			if entry.Failure == "" {
				entry.Failure = err.Error()
			}
			result.Failed++
		} else {
			entry.Status = "passed"
			result.Passed++
		}
		result.Entries = append(result.Entries, entry)
	}
	result.Seconds = time.Since(started).Seconds()
	return result
}

// PrintBatchSummary prints one line per script and the totals.
func PrintBatchSummary(r BatchResult) {
	nameWidth, targetWidth := 4, 6
	for _, entry := range r.Entries {
		nameWidth = max(nameWidth, len(entry.Name))
		targetWidth = max(targetWidth, len(entry.Target))
	}
	fmt.Println()
	for _, entry := range r.Entries {
		label := map[string]string{"passed": "PASS", "failed": "FAIL", "skipped": "SKIP"}[entry.Status]
		steps := ""
		if entry.Status != "skipped" {
			steps = fmt.Sprintf("%d/%d", entry.StepsRun-entry.StepsFailed, entry.StepsTotal)
		}
		line := fmt.Sprintf("  %-4s  %-*s  %-*s  %7s  %6.1fs", label, nameWidth, entry.Name, targetWidth, entry.Target, steps, entry.Seconds)
		if entry.Failure != "" {
			failure := entry.Failure
			if len(failure) > 160 {
				failure = failure[:160] + "…"
			}
			line += "  " + failure
		}
		fmt.Println(line)
		if entry.Status == "failed" && entry.EvidenceDir != "" {
			fmt.Printf("  %s  evidence: %s\n", strings.Repeat(" ", 4), entry.EvidenceDir)
		}
	}
	totals := fmt.Sprintf("%d scripts in %.0fs: %d passed, %d failed, %d skipped",
		len(r.Entries), r.Seconds, r.Passed, r.Failed, r.Skipped)
	if r.Failed > 0 {
		console.Error("%s", totals)
	} else {
		console.Success("%s", totals)
	}
}

// WriteBatchReport writes the batch summary as JSON (for CI artifacts).
func WriteBatchReport(path string, r BatchResult) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(r, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(data, '\n'), 0o644)
}
