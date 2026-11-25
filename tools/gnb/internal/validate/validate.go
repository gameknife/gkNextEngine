package validate

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/gitops"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/validationstore"
)

type Options struct {
	RepoRoot, Preset, Target, Scene, Script, Report string
	Width, Height                                   int
	Visible, SyncValidation                         bool
	Reason                                          string
	ValidationRoot                                  string
	DisableStore                                    bool
	Frames                                          int
	IncludeUI                                       bool
	ArtifactDir                                     string
	RerunOf                                         string
	Args                                            []string
	Env                                             []string
	// Verbose streams the target's stdout/stderr to the terminal. By default it
	// only goes to the run's runner.log and failures print a short summary.
	Verbose bool
	// Watch runs for a person to follow: visible window at display rate, an
	// on-screen caption per step, a pause of Pace after each action, a cursor that
	// glides to its target, real input ignored, and the window kept open at the
	// end (and on failure) until the person closes it.
	Watch bool
	Pace  time.Duration
	// TimeoutScale multiplies every step timeout and the scene-ready wait. Software
	// Vulkan (Lavapipe in CI) runs one to two orders of magnitude slower than a GPU.
	TimeoutScale float64
	// Batch suppresses per-run chatter (the launch command line) when many
	// scripts run back to back; the batch prints its own summary.
	Batch bool
}

// defaultWatchPace is how long watch mode lingers after each action.
const defaultWatchPace = 700 * time.Millisecond
type Script struct {
	Name, Target, Scene string
	// Extra engine arguments the script cannot do without, so a scenario that only exists under a
	// flag (e.g. --force-compatibility-renderer) stays runnable as plain `gnb validate --script X`.
	// Command-line args are appended after these.
	Args     []string
	Defaults struct{ WaitFrames, StepTimeoutMs int }
	Viewport struct{ Width, Height int }
	// WaitReady (default true) makes `gnb validate` wait for scene.ready and a few
	// warm-up frames before the first step. Set it to false for scripts that test
	// startup or loading itself.
	WaitReady *bool
	// Tags select scripts for batch runs (`gnb validate --all --tag smoke`).
	Tags  []string
	Steps []map[string]any
}
type Report struct {
	Name        string           `json:"name"`
	Passed      bool             `json:"passed"`
	StartedAt   string           `json:"startedAt"`
	FinishedAt  string           `json:"finishedAt"`
	Error       string           `json:"error,omitempty"`
	Steps       []map[string]any `json:"steps"`
	Screenshots []string         `json:"screenshots"`
}

// Result identifies the durable evidence created by RunWithResult or
// ShotWithResult. The legacy Run/Shot helpers remain available for callers
// that only need an error.
type Result struct {
	RunID       string
	EvidenceDir string
	ReportPath  string
	Screenshot  string
	// Step outcome, for batch summaries. StepsRun counts steps that executed.
	StepsTotal, StepsRun, StepsFailed int
	// Failure is the first failed step's message, or the run-level error.
	Failure string
}
type client struct {
	conn  net.Conn
	rw    *bufio.ReadWriter
	token string
	next  int
	// dead is set once the control connection fails at the transport level,
	// which in practice means the target process exited or crashed.
	dead bool
	// pointer is the last absolute cursor position sent, so watch mode can glide
	// from there to the next target instead of jumping.
	pointer *[2]float64
	// windowW/windowH are the target's window size in the coordinate space of
	// mouse events, as the engine reports it. On a scaled Windows display it is
	// larger than the script viewport (1280x720 at 125% is 1600x900), so "norm"
	// points resolve against this. Zero when the target does not report it.
	windowW, windowH int
}

// readWindowSize asks the target for its window size; older targets do not
// know the query and the script viewport is used instead.
func (c *client) readWindowSize() {
	w, errW := query(c, "engine.windowWidth")
	h, errH := query(c, "engine.windowHeight")
	if errW == nil && errH == nil && num(w) > 0 && num(h) > 0 {
		c.windowW, c.windowH = int(num(w)), int(num(h))
	}
}

// pointSpace is the size "norm" coordinates scale to.
func (c *client) pointSpace(o Options) (int, int) {
	if c.windowW > 0 && c.windowH > 0 {
		return c.windowW, c.windowH
	}
	return o.Width, o.Height
}

// errControlLost marks a transport failure on the control channel, as opposed to
// an error the engine returned for a request.
var errControlLost = errors.New("control channel lost (target process exited or crashed)")

func Run(ctx context.Context, o Options) error {
	_, err := RunWithResult(ctx, o)
	return err
}

func RunWithResult(ctx context.Context, o Options) (Result, error) {
	data, readErr := os.ReadFile(o.Script)
	var s Script
	if readErr == nil {
		readErr = json.Unmarshal(data, &s)
	}
	if s.Name == "" {
		if o.Script != "" {
			s.Name = strings.TrimSuffix(filepath.Base(o.Script), filepath.Ext(o.Script))
		} else {
			s.Name = "validation"
		}
	}
	if readErr == nil {
		lintOpts := LintOptions{}
		if o.Scene == "" {
			lintOpts.RepoRoot = o.RepoRoot
		}
		issues := Lint(data, lintOpts)
		var problems []string
		for _, issue := range issues {
			if issue.Error {
				problems = append(problems, "  "+issue.String())
			} else {
				console.Warn("%s", issue.String())
			}
		}
		if len(problems) > 0 {
			readErr = fmt.Errorf("script check failed (nothing was launched):\n%s", strings.Join(problems, "\n"))
		}
	}
	return run(ctx, o, s, data, readErr, "validate")
}

func Shot(ctx context.Context, o Options, frames int, ui bool, out string) error {
	_, err := ShotWithResult(ctx, o, frames, ui, out)
	return err
}

func ShotWithResult(ctx context.Context, o Options, frames int, ui bool, out string) (Result, error) {
	if frames <= 0 {
		frames = 90
	}
	s := Script{Name: "agent_validation"}
	// Large scenes can take minutes to parse and upload asynchronously. Wait for
	// readiness rather than spending the frame budget on the empty startup scene.
	s.Defaults.StepTimeoutMs = 300000
	s.Steps = []map[string]any{
		{"type": "wait-until", "query": "scene.ready", "value": true, "fatal": true},
		{"type": "wait-frames", "n": frames, "readyQuery": "scene.ready", "fatal": true},
		// Make capture and normal process shutdown a single engine-side operation.
		// A separate quit control request can race with the server teardown that
		// follows a synchronous screenshot on a headless host.
		{"type": "screenshot", "out": out, "ui": ui, "quitAfterCapture": true},
	}
	script, _ := json.MarshalIndent(s, "", "  ")
	o.Frames = frames
	o.IncludeUI = ui
	return run(ctx, o, s, script, nil, "shot")
}

// Trace runs a target to a stable frame without creating a screenshot. Callers
// pass --asset-trace through Options.Args so the engine records runtime coverage.
func Trace(ctx context.Context, o Options, frames int) error {
	if frames <= 0 {
		frames = 120
	}
	s := Script{Name: "asset_trace"}
	s.Defaults.StepTimeoutMs = 30000
	s.Steps = []map[string]any{{"type": "wait-until", "query": "engine.status", "op": "eq", "value": "Running", "timeoutMs": 30000}, {"type": "wait-frames", "n": frames}, {"type": "quit"}}
	o.DisableStore = true
	_, err := run(ctx, o, s, nil, nil, "trace")
	return err
}

func run(ctx context.Context, o Options, s Script, scriptData []byte, loadErr error, kind string) (result Result, retErr error) {
	if o.Watch {
		o.Visible = true
		if o.Pace <= 0 {
			o.Pace = defaultWatchPace
		}
	}
	if o.Target == "" {
		o.Target = s.Target
	}
	if o.Target == "" {
		o.Target = "gkNextRenderer"
	}
	if o.Scene == "" {
		o.Scene = s.Scene
	}
	o.Scene = resolveFixtureScene(o.RepoRoot, o.Scene)
	if o.Width == 0 {
		o.Width = s.Viewport.Width
	}
	if o.Height == 0 {
		o.Height = s.Viewport.Height
	}
	if o.Reason == "" {
		o.Reason = s.Name
	}
	if o.Script != "" {
		o.Script, _ = filepath.Abs(o.Script)
	}
	var store validationstore.Store
	var record validationstore.Record
	var recording bool
	if !o.DisableStore {
		if o.ValidationRoot != "" {
			store = validationstore.NewAt(o.ValidationRoot)
		} else {
			store = validationstore.New(o.RepoRoot, o.Preset)
		}
		if scriptData == nil {
			scriptData = []byte("{}\n")
		}
		record = validationstore.Record{
			Kind:           kind,
			Name:           s.Name,
			Target:         o.Target,
			Preset:         o.Preset,
			Reason:         o.Reason,
			ScriptSource:   o.Script,
			ScriptSnapshot: "script.json",
			Parameters:     launchParameters(o, s),
			ExtraArgs:      append(append([]string{}, s.Args...), o.Args...),
			Scene:          o.Scene,
			Width:          o.Width,
			Height:         o.Height,
			Visible:        o.Visible,
			SyncValidation: o.SyncValidation,
			Frames:         o.Frames,
			IncludeUI:      o.IncludeUI,
			RerunOf:        o.RerunOf,
			Status:         validationstore.StatusRunning,
			Phase:          "prepare",
			CurrentStage:   "准备验证",
			RunnerPID:      os.Getpid(),
		}
		if status, err := gitops.GetStatus(o.RepoRoot); err == nil {
			record.GitHead = status.Head
			record.GitDirty = status.Dirty
		}
		var err error
		record, err = store.Create(record, scriptData)
		if err != nil {
			return Result{}, fmt.Errorf("create validation record: %w", err)
		}
		recording = true
		result.RunID = record.RunID
		result.EvidenceDir, _ = store.RunDir(record.RunID)
		o.ArtifactDir = filepath.Join(result.EvidenceDir, "screenshots")
	}
	finish := func(status validationstore.Status, phase string, cause error, exitCode *int) {
		if !recording {
			return
		}
		_ = store.Update(record.RunID, func(r *validationstore.Record) error {
			r.Status = status
			r.Phase = phase
			r.CurrentStage = phase
			r.FinishedAt = time.Now().UTC()
			r.HeartbeatAt = r.FinishedAt
			r.ExitCode = exitCode
			if cause != nil {
				r.Error = cause.Error()
			}
			return nil
		})
		recording = false
	}
	defer func() {
		if recording {
			status := validationstore.StatusFailed
			phase := "runner"
			if errorsIsContext(ctx) {
				status = validationstore.StatusCanceled
				phase = "canceled"
			}
			finish(status, phase, retErr, nil)
		}
	}()
	if loadErr != nil {
		writeFailureReport(o.Report, s.Name, loadErr)
		finish(validationstore.StatusFailed, "parse", loadErr, nil)
		return result, loadErr
	}
	bin := platform.BinDir(o.RepoRoot, o.Preset)
	exe := platform.ExecutablePath(bin, o.Target)
	if _, err := os.Stat(exe); err != nil {
		err = fmt.Errorf("executable not found: %s\n请先运行 `gnb build %s`", exe, o.Target)
		writeFailureReport(o.Report, s.Name, err)
		finish(validationstore.StatusFailed, "launch", err, nil)
		return result, err
	}
	staleWarning := staleTargetWarning(exe, o.Target, filepath.Join(filepath.Dir(bin), "assets", "shaders"))
	if staleWarning != "" {
		console.Warn("%s", staleWarning)
	}
	if recording {
		_ = store.Update(record.RunID, func(r *validationstore.Record) error {
			r.Phase = "launch"
			r.CurrentStage = "启动目标"
			return nil
		})
	}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		finish(validationstore.StatusFailed, "launch", err, nil)
		return result, err
	}
	endpoint := ln.Addr().String()
	_ = ln.Close()
	b := make([]byte, 24)
	_, _ = rand.Read(b)
	token := hex.EncodeToString(b)
	args := []string{"--agent-validation", "--agent-control=" + endpoint, "--agent-control-token=" + token}
	if o.Visible {
		args = append(args, "--agent-visible-window")
	}
	if o.Watch {
		args = append(args, "--agent-watch")
	}
	if o.SyncValidation {
		args = append(args, "--sync-validation")
	}
	if o.Width > 0 {
		args = append(args, fmt.Sprintf("--width=%d", o.Width))
	}
	if o.Height > 0 {
		args = append(args, fmt.Sprintf("--height=%d", o.Height))
	}
	if o.Scene != "" {
		args = append(args, "--load-scene="+o.Scene)
	}
	args = append(args, s.Args...)
	args = append(args, o.Args...)
	displayArgs := append([]string(nil), args...)
	for i, arg := range displayArgs {
		if strings.HasPrefix(arg, "--agent-control-token=") {
			displayArgs[i] = "--agent-control-token=<redacted>"
		}
	}
	if !o.Batch {
		console.CommandLine(exe + " " + strings.Join(displayArgs, " "))
	}
	cmd := exec.CommandContext(ctx, exe, args...)
	cmd.Dir = filepath.Dir(exe)
	cmd.Env = append(os.Environ(), o.Env...)
	var logFile *os.File
	if recording {
		logPath, logErr := store.LogPath(record.RunID)
		if logErr != nil {
			finish(validationstore.StatusFailed, "launch", logErr, nil)
			return result, logErr
		}
		logFile, err = os.OpenFile(logPath, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0o644)
		if err != nil {
			finish(validationstore.StatusFailed, "launch", err, nil)
			return result, err
		}
		defer logFile.Close()
		_, _ = logFile.WriteString("command: " + exe + " " + strings.Join(displayArgs, " ") + "\n")
		if staleWarning != "" {
			_, _ = logFile.WriteString("warning: " + staleWarning + "\n")
		}
		if o.Verbose {
			cmd.Stdout = io.MultiWriter(os.Stdout, logFile)
			cmd.Stderr = io.MultiWriter(os.Stderr, logFile)
		} else {
			// The engine log stays in runner.log; the terminal gets a summary.
			cmd.Stdout, cmd.Stderr = logFile, logFile
		}
	} else {
		cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
	}
	if err = cmd.Start(); err != nil {
		if recording {
			_ = store.AppendLog(record.RunID, []byte(fmt.Sprintf("runner start failed: %v\n", err)))
		}
		status := validationstore.StatusFailed
		phase := "launch"
		if errorsIsContext(ctx) {
			status = validationstore.StatusCanceled
			phase = "canceled"
		}
		finish(status, phase, err, nil)
		return result, err
	}
	if recording {
		_ = store.Update(record.RunID, func(r *validationstore.Record) error {
			r.Phase = "connect"
			r.CurrentStage = "连接控制通道"
			return nil
		})
		heartbeatCtx, stopHeartbeat := context.WithCancel(ctx)
		defer stopHeartbeat()
		go heartbeat(heartbeatCtx, store, record.RunID)
	}
	// Wait on the process from the start, so an early exit (a startup failure) is
	// noticed at once instead of after the connect timeout.
	proc := newProcessWaiter(cmd)
	defer func() { _ = proc.kill() }()
	// startupFailure explains a target that died before the run could begin.
	startupFailure := func(cause error) error {
		if !proc.exited() {
			return cause
		}
		msg := fmt.Sprintf("target exited during startup (%v)", proc.err)
		if tail := engineErrorTail(filepath.Join(result.EvidenceDir, "runner.log"), 1); recording && len(tail) > 0 {
			msg += ": " + strings.TrimSpace(tail[0])
		}
		return errors.New(msg)
	}
	var conn net.Conn
	deadline := time.Now().Add(30 * time.Second)
	for time.Now().Before(deadline) {
		conn, err = net.DialTimeout("tcp", endpoint, 200*time.Millisecond)
		if err == nil || proc.exited() {
			break
		}
		select {
		case <-ctx.Done():
			finish(validationstore.StatusCanceled, "canceled", ctx.Err(), nil)
			return result, ctx.Err()
		case <-time.After(50 * time.Millisecond):
		}
	}
	if err != nil {
		err = startupFailure(fmt.Errorf("connect engine control %s: %w", endpoint, err))
		if errorsIsContext(ctx) {
			finish(validationstore.StatusCanceled, "canceled", ctx.Err(), proc.exitCode())
		} else {
			finish(validationstore.StatusFailed, "connect", err, proc.exitCode())
		}
		result.Failure = err.Error()
		return result, err
	}
	c := &client{conn: conn, rw: bufio.NewReadWriter(bufio.NewReader(conn), bufio.NewWriter(conn)), token: token}
	defer conn.Close()
	if _, err = c.call("handshake", map[string]any{}); err != nil {
		if errors.Is(err, errControlLost) {
			// The engine closes the control socket early in its shutdown; the process
			// itself can take a few seconds more. Let it finish so its reason is logged.
			select {
			case <-proc.done:
			case <-time.After(10 * time.Second):
			}
		}
		err = startupFailure(err)
		if errorsIsContext(ctx) {
			finish(validationstore.StatusCanceled, "canceled", ctx.Err(), proc.exitCode())
		} else {
			finish(validationstore.StatusFailed, "connect", err, proc.exitCode())
		}
		result.Failure = err.Error()
		return result, err
	}
	c.readWindowSize()
	rep := Report{Name: s.Name, Passed: true, StartedAt: time.Now().Format(time.RFC3339), Steps: []map[string]any{}, Screenshots: []string{}}
	var compatibilityCopies []screenshotCopy
	// captureFailure saves the screen at the moment something went wrong, before
	// later steps (usually the final quit) change or end it.
	failureCaptured := false
	captureFailure := func(label string, stepIndex int) {
		if failureCaptured || c.dead || errorsIsContext(ctx) {
			return
		}
		failureCaptured = true
		captureEntry, captureErr := execute(ctx, c, map[string]any{"type": "screenshot", "out": "failure_" + label}, s, o, len(s.Steps))
		path, _ := captureEntry["out"].(string)
		if captureErr != nil || path == "" {
			return
		}
		rep.Screenshots = append(rep.Screenshots, path)
		if !recording {
			return
		}
		rel := filepath.ToSlash(path)
		if relative, relErr := filepath.Rel(result.EvidenceDir, filepath.FromSlash(path)); relErr == nil {
			rel = filepath.ToSlash(relative)
		}
		_ = store.Update(record.RunID, func(r *validationstore.Record) error {
			r.Screenshots = append(r.Screenshots, rel)
			if stepIndex >= 0 && stepIndex < len(r.Steps) && r.Steps[stepIndex].Screenshot == "" {
				r.Steps[stepIndex].Screenshot = rel
			}
			return nil
		})
	}
	steps := s.Steps
	c.annotate(o, map[string]any{"caption": "Loading " + s.Name, "detail": "waiting for the scene to be ready", "status": "running"})
	if kind == "validate" && (s.WaitReady == nil || *s.WaitReady) {
		// Engine status "Running" and node counts do not prove the scene finished
		// loading; scene.ready does. Do it here once instead of in every script.
		if recording {
			_ = store.Update(record.RunID, func(r *validationstore.Record) error {
				r.CurrentStage = "等待场景就绪"
				return nil
			})
		}
		ready := Script{Name: s.Name}
		ready.Defaults.StepTimeoutMs = 300000
		for _, step := range []map[string]any{
			{"type": "wait-until", "query": "scene.ready", "value": true},
			{"type": "wait-frames", "n": 3, "readyQuery": "scene.ready"},
		} {
			if _, e := execute(ctx, c, step, ready, o, -1); e != nil {
				rep.Passed = false
				if c.dead {
					rep.Error = "target process exited unexpectedly while waiting for scene.ready; no steps were run"
				} else {
					rep.Error = "scene never became ready: " + e.Error()
				}
				captureFailure("ready", -1)
				steps = nil
				break
			}
		}
	}
	quitSent := false
	for i, step := range steps {
		startedAt := time.Now().UTC()
		if recording {
			_ = store.Update(record.RunID, func(r *validationstore.Record) error {
				r.CurrentStep = i
				r.CurrentStage = fmt.Sprintf("步骤 %d/%d: %s", i+1, len(s.Steps), str(step, "type", ""))
				return nil
			})
		}
		typ := str(step, "type", "")
		c.annotate(o, stepAnnotation(i, len(steps), step))
		runStep := step
		if o.Watch {
			// The person closes the window when they are done looking, so the
			// script's own exit is skipped and captures keep the window alive.
			if typ == "screenshot" && boolv(step, "quitAfterCapture", false) {
				runStep = clone(step)
				runStep["quitAfterCapture"] = false
			}
			if typ == "screenshot" && boolv(step, "ui", false) {
				c.annotate(o, map[string]any{"hidden": true})
			}
		} else if typ == "quit" || (typ == "screenshot" && boolv(step, "quitAfterCapture", false)) {
			quitSent = true
		}
		var entry map[string]any
		var e error
		if o.Watch && typ == "quit" {
			entry = map[string]any{"type": typ, "passed": true, "skipped": "watch mode keeps the window open"}
		} else {
			entry, e = execute(ctx, c, runStep, s, o, i)
		}
		if o.Watch {
			if typ == "screenshot" && boolv(step, "ui", false) {
				c.annotate(o, map[string]any{"hidden": false})
			}
			switch {
			case e != nil:
				c.annotate(o, map[string]any{"status": "fail", "detail": describeStep(step) + "  —  " + e.Error()})
			case typ == "assert" || typ == "wait-until":
				c.annotate(o, map[string]any{"status": "pass"})
			}
			sleepContext(ctx, watchPauseAfter(typ, o.Pace))
		}
		entry["idx"] = i
		if note := str(step, "description", str(step, "comment", "")); note != "" {
			entry["description"] = note
		}
		if e != nil {
			entry["passed"] = false
			entry["message"] = e.Error()
			rep.Passed = false
		}
		rep.Steps = append(rep.Steps, entry)
		if recording {
			stepRecord := stepRecord(i, step, entry, startedAt, time.Now().UTC())
			if stepRecord.Screenshot != "" {
				if rel, relErr := filepath.Rel(result.EvidenceDir, filepath.FromSlash(stepRecord.Screenshot)); relErr == nil {
					stepRecord.Screenshot = filepath.ToSlash(rel)
				}
			}
			_ = store.Update(record.RunID, func(r *validationstore.Record) error {
				r.Steps = append(r.Steps, stepRecord)
				if stepRecord.Screenshot != "" {
					r.Screenshots = append(r.Screenshots, stepRecord.Screenshot)
				}
				return nil
			})
		}
		if p, ok := entry["out"].(string); ok && entry["passed"] == true {
			rep.Screenshots = append(rep.Screenshots, p)
			if requested, ok := entry["requestedOut"].(string); ok && requested != "" {
				compatibilityCopies = append(compatibilityCopies, screenshotCopy{source: p, requested: requested})
			}
		}
		if e != nil {
			captureFailure(fmt.Sprintf("%03d", i), i)
		}
		if c.dead && !quitSent {
			// Every later step would fail the same way; say what happened instead.
			rep.Passed = false
			rep.Error = fmt.Sprintf("target process exited unexpectedly during step #%d (%s); %d later steps were not run",
				i, typ, len(steps)-i-1)
			break
		}
		if e != nil && boolv(step, "fatal", false) {
			break
		}
	}
	exitTimeout := 30 * time.Second
	summaryPrinted := false
	if o.Watch && !c.dead {
		// Leave the result on screen and in the terminal, then wait for the
		// person to close the window.
		failedSteps := 0
		for _, entry := range rep.Steps {
			if entry["passed"] != true {
				failedSteps++
			}
		}
		final := map[string]any{"status": "done", "detail": "Close the window when you are done."}
		if rep.Passed {
			final["caption"] = fmt.Sprintf("Finished: %d/%d steps passed", len(rep.Steps), len(s.Steps))
		} else {
			final["status"] = "failed"
			final["caption"] = fmt.Sprintf("Failed: %d of %d steps", failedSteps, len(s.Steps))
			if rep.Error != "" {
				final["detail"] = rep.Error + "  —  close the window when you are done."
			}
		}
		c.annotate(o, final)
		printRunSummary(kind, rep, len(s.Steps), result.EvidenceDir)
		summaryPrinted = true
		console.Info("watch mode: the window stays open; close it to finish")
		exitTimeout = 24 * time.Hour
	} else if rep.Passed && !quitSent && !c.dead {
		// Scripts no longer need to end with "quit"; without it the passing run
		// used to sit until the exit timeout and then fail.
		_, _ = c.call("quit", map[string]any{"exitCode": 0})
	}
	rep.FinishedAt = time.Now().Format(time.RFC3339)
	result.StepsTotal, result.StepsRun = len(s.Steps), len(rep.Steps)
	for _, entry := range rep.Steps {
		if entry["passed"] == true {
			continue
		}
		result.StepsFailed++
		if result.Failure == "" {
			result.Failure = fmt.Sprintf("#%v %v: %v", entry["idx"], entry["type"], entry["message"])
		}
	}
	if rep.Error != "" {
		result.Failure = rep.Error
	}
	if !rep.Passed {
		err = fmt.Errorf("agent validation failed")
		if rep.Error != "" {
			err = fmt.Errorf("agent validation failed: %s", rep.Error)
		}
		if o.Watch && !c.dead {
			_ = proc.wait(ctx, exitTimeout)
		} else if exitErr := proc.kill(); exitErr != nil && c.dead && !quitSent {
			err = fmt.Errorf("%w (target %v)", err, exitErr)
		}
		if !summaryPrinted {
			printRunSummary(kind, rep, len(s.Steps), result.EvidenceDir)
		}
		copyScreenshotCompatibility(cmd.Dir, compatibilityCopies)
		if o.Report != "" {
			rep.Passed = false
			if reportErr := writeReport(o.Report, rep); reportErr != nil {
				finish(validationstore.StatusFailed, "artifact", reportErr, proc.exitCode())
				return result, reportErr
			}
			result.ReportPath = o.Report
		}
		if errorsIsContext(ctx) {
			finish(validationstore.StatusCanceled, "canceled", ctx.Err(), proc.exitCode())
		} else {
			finish(validationstore.StatusFailed, "step", err, proc.exitCode())
		}
		return result, err
	}

	// The quit command is acknowledged before the next engine tick observes the
	// close request. Waiting here lets the application finish that tick and exit
	// with its requested code instead of being unconditionally SIGKILLed by the
	// cleanup defer above.
	err = proc.wait(ctx, exitTimeout)
	if err != nil {
		err = fmt.Errorf("agent process exit: %w", err)
		rep.Passed = false
		if o.Report != "" {
			_ = writeReport(o.Report, rep)
			result.ReportPath = o.Report
		}
		if errorsIsContext(ctx) {
			finish(validationstore.StatusCanceled, "canceled", ctx.Err(), proc.exitCode())
		} else {
			finish(validationstore.StatusFailed, "exit", err, proc.exitCode())
		}
		return result, err
	}
	for _, screenshot := range rep.Screenshots {
		if err = pollContext(ctx, 5*time.Second, func() (bool, error) {
			_, statErr := os.Stat(screenshot)
			return statErr == nil, nil
		}); err != nil {
			err = fmt.Errorf("agent screenshot %s: %w", screenshot, err)
			rep.Passed = false
			if o.Report != "" {
				_ = writeReport(o.Report, rep)
				result.ReportPath = o.Report
			}
			finish(validationstore.StatusFailed, "artifact", err, proc.exitCode())
			return result, err
		}
	}
	copyScreenshotCompatibility(cmd.Dir, compatibilityCopies)
	if o.Report != "" {
		if err = writeReport(o.Report, rep); err != nil {
			finish(validationstore.StatusFailed, "artifact", err, proc.exitCode())
			return result, err
		}
		result.ReportPath = o.Report
	}
	if len(rep.Screenshots) > 0 {
		result.Screenshot = rep.Screenshots[0]
	}
	if !summaryPrinted {
		printRunSummary(kind, rep, len(s.Steps), result.EvidenceDir)
	}
	finish(validationstore.StatusPassed, "complete", nil, proc.exitCode())
	return result, nil
}


func (c *client) call(method string, params any) (any, error) {
	if c.dead {
		return nil, errControlLost
	}
	lost := func(err error) error {
		c.dead = true
		return fmt.Errorf("%w: %v", errControlLost, err)
	}
	// Software Vulkan ICDs (notably Lavapipe) may need much longer than a hardware
	// driver for first-pipeline compilation and a single validation frame.
	_ = c.conn.SetDeadline(time.Now().Add(2 * time.Minute))
	c.next++
	req := map[string]any{"id": strconv.Itoa(c.next), "token": c.token, "method": method, "params": params}
	b, _ := json.Marshal(req)
	if _, err := c.rw.Write(append(b, '\n')); err != nil {
		return nil, lost(err)
	}
	if err := c.rw.Flush(); err != nil {
		return nil, lost(err)
	}
	line, err := c.rw.ReadBytes('\n')
	if err != nil {
		return nil, lost(err)
	}
	var r struct {
		Result any `json:"result"`
		Error  any `json:"error"`
	}
	if err = json.Unmarshal(line, &r); err != nil {
		return nil, lost(err)
	}
	if r.Error != nil {
		return nil, fmt.Errorf("%v", r.Error)
	}
	return r.Result, nil
}
func execute(ctx context.Context, c *client, st map[string]any, s Script, o Options, stepIndex int) (map[string]any, error) {
	typ := str(st, "type", "")
	e := map[string]any{"type": typ, "passed": true}
	timeout := duration(st, s)
	if o.TimeoutScale > 1 {
		timeout = time.Duration(float64(timeout) * o.TimeoutScale)
	}
	switch typ {
	case "wait-frames":
		var start, lastFrame float64
		started := false
		frames := numv(st, "n", float64(max(1, s.Defaults.WaitFrames)))
		return e, pollContext(ctx, timeout, func() (bool, error) {
			if readyQuery := str(st, "readyQuery", ""); readyQuery != "" {
				ready, err := query(c, readyQuery)
				if err != nil {
					return false, err
				}
				if ready != true {
					// Late texture/HDR uploads or another scene load restart warm-up.
					started = false
					return false, nil
				}
			}
			v, err := query(c, "engine.totalFrames")
			if err != nil {
				return false, err
			}
			frame := num(v)
			if !started || frame < lastFrame {
				start, started = frame, true
			}
			lastFrame = frame
			return frame-start >= frames, nil
		})
	case "wait-ms":
		timer := time.NewTimer(time.Duration(numv(st, "ms", 0)) * time.Millisecond)
		defer timer.Stop()
		select {
		case <-timer.C:
			return e, nil
		case <-ctx.Done():
			return e, ctx.Err()
		}
	case "wait-until", "assert":
		q, op := str(st, "query", ""), str(st, "op", "eq")
		v, er := query(c, q)
		e["actual"] = v
		e["expected"] = st["value"]
		if er == nil && compare(v, op, st["value"]) {
			return e, nil
		}
		if er != nil {
			// An unknown query name or a failed scene load: report the engine's
			// reason rather than a bare "actual=<nil>".
			return e, fmt.Errorf("query %s: %w", q, er)
		}
		if typ == "assert" {
			return e, fmt.Errorf("assertion failed: %s %s %v, actual %v", q, op, st["value"], v)
		}
		er = pollContext(ctx, timeout, func() (bool, error) {
			v, x := query(c, q)
			e["actual"] = v
			return compare(v, op, st["value"]), x
		})
		if er != nil && !errors.Is(er, errControlLost) && !errorsIsContext(ctx) {
			if strings.HasPrefix(er.Error(), "timed out") {
				return e, fmt.Errorf("wait-until %s %s %v %s, last actual %v", q, op, st["value"], er, e["actual"])
			}
			return e, fmt.Errorf("query %s: %w", q, er)
		}
		return e, er
	case "screenshot":
		requested := str(st, "out", fmt.Sprintf("screenshots/%s_step", s.Name))
		p := requested
		if o.ArtifactDir != "" {
			name := sanitizeArtifactName(filepath.Base(filepath.Clean(requested)))
			p = filepath.Join(o.ArtifactDir, fmt.Sprintf("%03d_%s", stepIndex, name))
		}
		r, er := c.call(typ, map[string]any{
			"out":              p,
			"ui":               boolv(st, "ui", false),
			"accumulateFrames": uint32(numv(st, "accumulateFrames", 0)),
			"quitAfterCapture": boolv(st, "quitAfterCapture", false),
		})
		if er != nil {
			return e, er
		}
		m, _ := r.(map[string]any)
		path, _ := m["path"].(string)
		e["out"] = path
		e["requestedOut"] = requested
		if boolv(st, "quitAfterCapture", false) {
			// The engine will exit when the synchronous capture is complete.
			// Verify the output after its process has reaped in run().
			return e, nil
		}
		er = pollContext(ctx, timeout, func() (bool, error) { _, x := os.Stat(path); return x == nil, nil })
		return e, er
	case "log":
		e["message"] = str(st, "message", str(st, "text", ""))
		return e, nil
	case "quit":
		_, _ = c.call(typ, map[string]any{"exitCode": 0})
		// The engine may begin teardown immediately after accepting quit, which
		// can close the control socket before its acknowledgement is read. The
		// child process exit code checked by run() is authoritative; a failed
		// control reply simply leads to the normal exit timeout if quit was not
		// actually delivered.
		return e, nil
	case "cvar":
		p := map[string]any{"name": str(st, "name", "")}
		if v, ok := st["set"]; ok {
			p["set"] = v
		}
		_, er := c.call(typ, p)
		return e, er
	case "exec":
		_, er := c.call(typ, map[string]any{"line": str(st, "line", "")})
		return e, er
	case "key", "text", "mouse-move", "mouse-button", "click", "drag", "scroll":
		p := clone(st)
		delete(p, "type")
		if er := resolveTargets(c, p); er != nil {
			return e, er
		}
		width, height := c.pointSpace(o)
		if hasNormPoint(p) && (width <= 0 || height <= 0) {
			return e, fmt.Errorf("%s: norm coordinates need the window size; this target does not report it, so set \"viewport\" or pass --width/--height", typ)
		}
		normalizePoints(p, width, height)
		c.watchGlide(ctx, o, typ, p)
		_, er := c.call(typ, p)
		return e, er
	default:
		return e, fmt.Errorf("unknown step type %q", typ)
	}
}

func launchParameters(o Options, s Script) []string {
	args := []string{"--agent-validation"}
	if o.Visible {
		args = append(args, "--agent-visible-window")
	}
	if o.Watch {
		args = append(args, "--agent-watch")
	}
	if o.SyncValidation {
		args = append(args, "--sync-validation")
	}
	if o.Width > 0 {
		args = append(args, fmt.Sprintf("--width=%d", o.Width))
	}
	if o.Height > 0 {
		args = append(args, fmt.Sprintf("--height=%d", o.Height))
	}
	if o.Scene != "" {
		args = append(args, "--load-scene="+o.Scene)
	}
	if o.Frames > 0 {
		args = append(args, fmt.Sprintf("--frames=%d", o.Frames))
	}
	if o.IncludeUI {
		args = append(args, "--ui")
	}
	args = append(args, s.Args...)
	args = append(args, o.Args...)
	return args
}

func stepRecord(index int, raw map[string]any, entry map[string]any, started, finished time.Time) validationstore.Step {
	step := validationstore.Step{
		Index:       index,
		Type:        str(raw, "type", ""),
		Description: str(raw, "description", str(raw, "comment", "")),
		Query:       str(raw, "query", ""),
		Op:          str(raw, "op", ""),
		Expected:    raw["value"],
		Actual:      entry["actual"],
		StartedAt:   started,
		FinishedAt:  finished,
		DurationMs:  finished.Sub(started).Milliseconds(),
		Passed:      boolv(entry, "passed", false),
		Message:     str(entry, "message", ""),
	}
	if path, ok := entry["out"].(string); ok && path != "" {
		step.Screenshot = filepath.ToSlash(path)
	}
	return step
}

func sanitizeArtifactName(name string) string {
	name = strings.TrimSpace(name)
	if name == "" || name == "." || name == string(filepath.Separator) {
		return "screenshot"
	}
	var b strings.Builder
	for _, r := range name {
		if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') || r == '-' || r == '_' || r == '.' {
			b.WriteRune(r)
		} else {
			b.WriteByte('_')
		}
	}
	if b.Len() == 0 {
		return "screenshot"
	}
	return b.String()
}

func heartbeat(ctx context.Context, store validationstore.Store, runID string) {
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			_ = store.Touch(runID)
		}
	}
}

func writeFailureReport(path, name string, cause error) {
	if path == "" {
		return
	}
	_ = writeReport(path, Report{
		Name:       name,
		Passed:     false,
		StartedAt:  time.Now().UTC().Format(time.RFC3339),
		FinishedAt: time.Now().UTC().Format(time.RFC3339),
		Steps:      []map[string]any{{"passed": false, "message": cause.Error()}},
	})
}


type screenshotCopy struct {
	source    string
	requested string
}

func copyScreenshotCompatibility(workDir string, copies []screenshotCopy) {
	for _, copy := range copies {
		target := copy.requested
		if !filepath.IsAbs(target) {
			target = filepath.Join(workDir, target)
		}
		if filepath.Ext(target) == "" {
			target += filepath.Ext(copy.source)
		}
		data, err := os.ReadFile(copy.source)
		if err != nil {
			continue
		}
		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			continue
		}
		_ = os.WriteFile(target, data, 0o644)
	}
}

func errorsIsContext(ctx context.Context) bool {
	return ctx != nil && ctx.Err() != nil
}

// Test fixtures live beside the scripts in assets/agentscripts, which is not one of the
// runtime asset folders the build copies (assets/cmake/RuntimeAssets.cmake). Hand the game
// their path in the repository instead.
func resolveFixtureScene(repoRoot, scene string) string {
	if repoRoot == "" || scene == "" || filepath.IsAbs(scene) ||
		!strings.HasPrefix(filepath.ToSlash(scene), "assets/agentscripts/") {
		return scene
	}
	path := filepath.Join(repoRoot, filepath.FromSlash(scene))
	if _, err := os.Stat(path); err != nil {
		return scene
	}
	return filepath.ToSlash(path)
}

func query(c *client, q string) (any, error) { return c.call("query", map[string]any{"query": q}) }
func poll(d time.Duration, f func() (bool, error)) error {
	return pollContext(context.Background(), d, f)
}

func pollContext(ctx context.Context, d time.Duration, f func() (bool, error)) error {
	end := time.Now().Add(d)
	for {
		ok, e := f()
		if ok {
			return nil
		}
		if e != nil {
			return e
		}
		if time.Now().After(end) {
			return fmt.Errorf("timed out after %s", d)
		}
		timer := time.NewTimer(16 * time.Millisecond)
		select {
		case <-timer.C:
		case <-ctx.Done():
			timer.Stop()
			return ctx.Err()
		}
	}
}
func compare(a any, op string, b any) bool {
	switch op {
	case "eq":
		return fmt.Sprint(a) == fmt.Sprint(b)
	case "ne":
		return fmt.Sprint(a) != fmt.Sprint(b)
	case "gt":
		return num(a) > num(b)
	case "ge":
		return num(a) >= num(b)
	case "lt":
		return num(a) < num(b)
	case "le":
		return num(a) <= num(b)
	case "contains":
		return strings.Contains(fmt.Sprint(a), fmt.Sprint(b))
	}
	return false
}
// resolveTargets turns a named point, {"target": "cell:1:0:0"}, into a normalized one
// using the game's own list of named targets ("name=x,y,visible;", x/y in 0..1). The
// list comes from game.inventoryTargets unless the point names another "query".
func resolveTargets(c *client, p map[string]any) error {
	for _, k := range []string{"to", "at", "from"} {
		v, ok := p[k].(map[string]any)
		if !ok {
			continue
		}
		name, ok := v["target"].(string)
		if !ok {
			continue
		}
		q := "game.inventoryTargets"
		if s, ok := v["query"].(string); ok && s != "" {
			q = s
		}
		raw, err := query(c, q)
		if err != nil {
			return err
		}
		x, y, visible, found := findTarget(fmt.Sprint(raw), name)
		if !found {
			return fmt.Errorf("target %q is not drawn (%s)", name, q)
		}
		if !visible {
			return fmt.Errorf("target %q is scrolled out of view; reveal it first", name)
		}
		p[k] = map[string]any{"norm": []any{x, y}}
	}
	return nil
}

func findTarget(list, name string) (x, y float64, visible, found bool) {
	for _, entry := range strings.Split(list, ";") {
		key, value, ok := strings.Cut(entry, "=")
		if !ok || key != name {
			continue
		}
		parts := strings.Split(value, ",")
		if len(parts) < 2 {
			return 0, 0, false, false
		}
		x, errX := strconv.ParseFloat(parts[0], 64)
		y, errY := strconv.ParseFloat(parts[1], 64)
		if errX != nil || errY != nil {
			return 0, 0, false, false
		}
		return x, y, len(parts) < 3 || parts[2] != "0", true
	}
	return 0, 0, false, false
}

func hasNormPoint(p map[string]any) bool {
	for _, k := range []string{"to", "at", "from"} {
		if v, ok := p[k].(map[string]any); ok && v["norm"] != nil {
			return true
		}
	}
	return false
}

func normalizePoints(p map[string]any, w, h int) {
	for _, k := range []string{"to", "at", "from"} {
		v, ok := p[k].(map[string]any)
		if !ok {
			continue
		}
		if n, ok := v["norm"].([]any); ok && len(n) > 1 {
			p[k] = []any{num(n[0]) * float64(w), num(n[1]) * float64(h)}
		} else if px, ok := v["px"].([]any); ok {
			p[k] = px
		}
	}
}
func writeReport(path string, r Report) error {
	if !filepath.IsAbs(path) {
		path, _ = filepath.Abs(path)
	}
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return err
	}
	b, _ := json.MarshalIndent(r, "", "  ")
	return os.WriteFile(path, b, 0644)
}
func duration(st map[string]any, s Script) time.Duration {
	ms := numv(st, "timeoutMs", float64(s.Defaults.StepTimeoutMs))
	if ms <= 0 {
		ms = 8000
	}
	return time.Duration(ms) * time.Millisecond
}
func clone(m map[string]any) map[string]any {
	r := map[string]any{}
	for k, v := range m {
		r[k] = v
	}
	return r
}
func str(m map[string]any, k, d string) string {
	if v, ok := m[k].(string); ok {
		return v
	}
	return d
}
func boolv(m map[string]any, k string, d bool) bool {
	if v, ok := m[k].(bool); ok {
		return v
	}
	return d
}
func numv(m map[string]any, k string, d float64) float64 {
	if v, ok := m[k]; ok {
		return num(v)
	}
	return d
}
func num(v any) float64 {
	switch x := v.(type) {
	case float64:
		return x
	case int:
		return float64(x)
	case json.Number:
		r, _ := x.Float64()
		return r
	case string:
		r, _ := strconv.ParseFloat(x, 64)
		return r
	}
	return 0
}
func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}
