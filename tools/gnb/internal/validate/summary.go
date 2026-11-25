package validate

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
)

// staleTargetWarning returns a warning when the target executable is older than
// the compiled shaders it will load. Shaders are compiled into one directory
// shared by every target, so building any other target can leave this one
// running against shaders it was not built with (an August binary against
// November shaders lost the Vulkan device on startup).
func staleTargetWarning(exe, target, shaderDir string) string {
	exeInfo, err := os.Stat(exe)
	if err != nil {
		return ""
	}
	newest, ok := newestModTime(shaderDir, ".spv")
	if !ok {
		return ""
	}
	// A normal build compiles shaders before linking; allow for slow links.
	if newest.Sub(exeInfo.ModTime()) < 10*time.Minute {
		return ""
	}
	return fmt.Sprintf("%s was built %s, before the current shaders (%s); it may not match them. "+
		"Rebuild it (`gnb build %s`, or pass --build).",
		target, exeInfo.ModTime().Format("2006-01-02 15:04"), newest.Format("2006-01-02 15:04"), target)
}

func newestModTime(dir, ext string) (time.Time, bool) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return time.Time{}, false
	}
	var newest time.Time
	found := false
	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(entry.Name(), ext) {
			continue
		}
		info, err := entry.Info()
		if err != nil {
			continue
		}
		if !found || info.ModTime().After(newest) {
			newest, found = info.ModTime(), true
		}
	}
	return newest, found
}

// engineErrorTail returns the last few error lines of a runner.log, which is
// what a failed run needs once the engine log no longer streams to the terminal.
func engineErrorTail(logPath string, max int) []string {
	file, err := os.Open(logPath)
	if err != nil {
		return nil
	}
	defer file.Close()
	var lines []string
	scanner := bufio.NewScanner(file)
	scanner.Buffer(make([]byte, 64*1024), 1024*1024)
	for scanner.Scan() {
		line := scanner.Text()
		lower := strings.ToLower(line)
		if strings.Contains(lower, "[error]") || strings.Contains(lower, "[critical]") ||
			strings.HasPrefix(strings.TrimSpace(line), "Exception:") {
			if len(line) > 220 {
				line = line[:220] + "…"
			}
			lines = append(lines, line)
		}
	}
	if len(lines) > max {
		lines = lines[len(lines)-max:]
	}
	return lines
}

// printRunSummary tells the terminal what happened without the engine log:
// a pass line, or every failed step with its reason and the evidence to open.
func printRunSummary(kind string, rep Report, total int, evidenceDir string) {
	failed := 0
	for _, step := range rep.Steps {
		if step["passed"] != true {
			failed++
		}
	}
	if rep.Passed {
		if kind == "validate" {
			console.Success("%s: %d/%d steps passed", rep.Name, len(rep.Steps), total)
		}
		return
	}
	switch {
	case rep.Error != "" && failed == 0:
		console.Error("%s: %s", rep.Name, rep.Error)
	case rep.Error != "":
		console.Error("%s: %d of %d steps failed; %s", rep.Name, failed, total, rep.Error)
	default:
		console.Error("%s: %d of %d steps failed", rep.Name, failed, total)
	}
	// The details follow the [error] header on the same stream, so a caller that
	// merges stdout and stderr still reads them in order.
	detail := func(format string, args ...any) { fmt.Fprintf(os.Stderr, format+"\n", args...) }
	for _, step := range rep.Steps {
		if step["passed"] == true {
			continue
		}
		line := fmt.Sprintf("  #%v %v — %v", step["idx"], step["type"], step["message"])
		if note, ok := step["description"].(string); ok && note != "" {
			line += "  (" + note + ")"
		}
		detail("%s", line)
	}
	for _, shot := range rep.Screenshots {
		if strings.Contains(filepath.Base(shot), "failure_") {
			detail("  failure screenshot: %s", shot)
		}
	}
	if evidenceDir != "" {
		if tail := engineErrorTail(filepath.Join(evidenceDir, "runner.log"), 6); len(tail) > 0 {
			detail("  engine errors (runner.log):")
			for _, line := range tail {
				detail("    %s", line)
			}
		}
	}
}
