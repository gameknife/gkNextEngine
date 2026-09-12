package dashboard

import (
	"context"
	"encoding/json"
	"fmt"
	"io/fs"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/validationstore"
)

type validationVM struct {
	Runs            []validationRunVM
	Selected        *validationRunVM
	SelectedID      string
	Target          string
	Status          string
	Offset          int
	HasMore         bool
	RunningCount    int
	FailedCount     int
	PendingCount    int
	IssueCount      int
	UnreadableCount int
	Scripts         []validationScriptVM
	ScriptTargets   []string
	ScriptTarget    string
	ScriptsError    string
	Error           string
	Flash           string
}

type validationRunVM struct {
	Record          validationstore.Record
	Review          validationstore.Review
	EvidenceDir     string
	StatusLabel     string
	ReviewLabel     string
	Duration        string
	StartedLabel    string
	FinishedLabel   string
	CurrentStepText string
	FailureSummary  string
	NeedsReview     bool
	HasIssue        bool
	Unreadable      bool
	Screenshots     []validationScreenshotVM
	LogTail         string
	LogError        string
}

type validationScreenshotVM struct {
	Path string
	URL  string
	Name string
}

type validationScriptVM struct {
	Path      string
	Name      string
	Target    string
	Scene     string
	StepCount int
	Width     int
	Height    int
	Runnable  bool
	Error     string
}

type validationScriptMetadata struct {
	Name     string `json:"name"`
	Target   string `json:"target"`
	Scene    string `json:"scene"`
	Viewport struct {
		Width  int `json:"width"`
		Height int `json:"height"`
	} `json:"viewport"`
	Steps []json.RawMessage `json:"steps"`
}

func (s *Server) validationStore() validationstore.Store {
	if s.validation.Root != "" {
		return s.validation
	}
	return validationstore.New(s.opts.RepoRoot, s.opts.Preset)
}

func (s *Server) buildValidationVM(query url.Values) validationVM {
	store := s.validationStore()
	// A short grace period prevents a slow runner from being called interrupted
	// merely because the Dashboard happened to poll between heartbeats.
	_ = store.ReconcileStale(5 * time.Second)
	all, err := store.List(validationstore.Filter{})
	vm := validationVM{
		Target:       query.Get("target"),
		Status:       query.Get("status"),
		ScriptTarget: query.Get("script-target"),
		Flash:        query.Get("flash"),
	}
	allScripts, scriptsError := s.buildValidationScripts()
	vm.ScriptsError = scriptsError
	targets := make(map[string]struct{})
	for _, script := range allScripts {
		if script.Target != "" {
			targets[script.Target] = struct{}{}
		}
		if vm.ScriptTarget == "" || script.Target == vm.ScriptTarget {
			vm.Scripts = append(vm.Scripts, script)
		}
	}
	for target := range targets {
		vm.ScriptTargets = append(vm.ScriptTargets, target)
	}
	sort.Strings(vm.ScriptTargets)
	if err != nil {
		vm.Error = err.Error()
		return vm
	}
	for _, record := range all {
		switch record.Status {
		case validationstore.StatusRunning:
			vm.RunningCount++
		case validationstore.StatusFailed:
			vm.FailedCount++
		case validationstore.StatusUnreadable:
			vm.UnreadableCount++
		}
		if record.Status == validationstore.StatusUnreadable {
			continue
		}
		review, reviewErr := store.Review(record.RunID)
		if reviewErr == nil && len(record.Screenshots) > 0 && (review.Status == "" || review.Status == validationstore.ReviewPending) {
			vm.PendingCount++
		}
		if reviewErr == nil && review.Status == validationstore.ReviewIssue {
			vm.IssueCount++
		}
	}

	var filtered []validationstore.Record
	if vm.Status == "actionable" {
		for _, record := range all {
			if vm.Target != "" && record.Target != vm.Target {
				continue
			}
			if isActionableValidation(store, record) {
				filtered = append(filtered, record)
			}
		}
	} else {
		filtered, err = store.List(validationstore.Filter{Target: vm.Target, Status: validationstore.Status(vm.Status)})
		if err != nil {
			vm.Error = err.Error()
			return vm
		}
	}
	offset, _ := strconv.Atoi(query.Get("offset"))
	if offset < 0 {
		offset = 0
	}
	vm.Offset = offset
	if offset >= len(filtered) {
		filtered = nil
	} else {
		filtered = filtered[offset:]
	}
	if len(filtered) > 50 {
		vm.HasMore = true
		filtered = filtered[:50]
	}
	for _, record := range filtered {
		vm.Runs = append(vm.Runs, s.makeValidationRunVM(store, record))
	}
	selectedID := query.Get("run")
	if selectedID == "" && len(vm.Runs) > 0 {
		selectedID = vm.Runs[0].Record.RunID
	}
	vm.SelectedID = selectedID
	for i := range vm.Runs {
		if vm.Runs[i].Record.RunID == selectedID {
			vm.Selected = &vm.Runs[i]
			break
		}
	}
	if vm.Selected == nil && selectedID != "" {
		if record, loadErr := store.Load(selectedID); loadErr == nil {
			run := s.makeValidationRunVM(store, record)
			vm.Selected = &run
		}
	}
	return vm
}

func (s *Server) buildValidationScripts() ([]validationScriptVM, string) {
	root := filepath.Join(s.opts.RepoRoot, "assets", "agentscripts")
	if _, err := os.Stat(root); err != nil {
		if os.IsNotExist(err) {
			return nil, ""
		}
		return nil, err.Error()
	}

	binDir := platform.BinDir(s.opts.RepoRoot, s.opts.Preset)
	var scripts []validationScriptVM
	walkErr := filepath.WalkDir(root, func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() || !strings.HasSuffix(entry.Name(), ".agentscript.json") {
			return nil
		}
		data, readErr := os.ReadFile(path)
		if readErr != nil {
			return readErr
		}
		var metadata validationScriptMetadata
		rel, relErr := filepath.Rel(s.opts.RepoRoot, path)
		if relErr != nil {
			return relErr
		}
		script := validationScriptVM{Path: filepath.ToSlash(rel)}
		if decodeErr := json.Unmarshal(data, &metadata); decodeErr != nil {
			script.Name = strings.TrimSuffix(entry.Name(), ".agentscript.json")
			script.Error = "JSON 无法解析: " + decodeErr.Error()
			scripts = append(scripts, script)
			return nil
		}
		script.Name = strings.TrimSpace(metadata.Name)
		if script.Name == "" {
			script.Name = strings.TrimSuffix(entry.Name(), ".agentscript.json")
		}
		script.Target = strings.TrimSpace(metadata.Target)
		if script.Target == "" {
			script.Target = "gkNextRenderer"
		}
		script.Scene = strings.TrimSpace(metadata.Scene)
		script.StepCount = len(metadata.Steps)
		script.Width = metadata.Viewport.Width
		script.Height = metadata.Viewport.Height
		if filepath.Base(filepath.Clean(script.Target)) != script.Target {
			script.Error = "target 名称无效"
		} else if _, statErr := os.Stat(platform.ExecutablePath(binDir, script.Target)); statErr != nil {
			script.Error = "目标未构建"
		}
		script.Runnable = script.Error == ""
		scripts = append(scripts, script)
		return nil
	})
	if walkErr != nil {
		return scripts, walkErr.Error()
	}
	sort.Slice(scripts, func(i, j int) bool { return scripts[i].Path < scripts[j].Path })
	return scripts, ""
}

func (s *Server) makeValidationRunVM(store validationstore.Store, record validationstore.Record) validationRunVM {
	vm := validationRunVM{
		Record:        record,
		EvidenceDir:   func() string { path, _ := store.RunDir(record.RunID); return path }(),
		StatusLabel:   validationStatusLabel(record.Status),
		StartedLabel:  record.StartedAt.Local().Format("2006-01-02 15:04:05"),
		FinishedLabel: record.FinishedAt.Local().Format("2006-01-02 15:04:05"),
		Duration:      validationDuration(record),
		Unreadable:    record.Status == validationstore.StatusUnreadable,
	}
	if record.Status == validationstore.StatusPassed && record.Kind == "validate" {
		vm.StatusLabel = "脚本检查通过"
	}
	review, err := store.Review(record.RunID)
	if err == nil {
		vm.Review = review
	}
	if len(record.Screenshots) == 0 {
		vm.ReviewLabel = "无需审阅"
	} else if review.Status == "" || review.Status == validationstore.ReviewPending {
		vm.ReviewLabel = "待审阅"
		vm.NeedsReview = true
	} else {
		vm.ReviewLabel = validationReviewLabel(review.Status)
		vm.HasIssue = review.Status == validationstore.ReviewIssue
	}
	if record.Status == validationstore.StatusFailed || record.Status == validationstore.StatusInterrupted || record.Status == validationstore.StatusUnreadable {
		vm.FailureSummary = record.Error
	}
	if record.Status == validationstore.StatusRunning && record.CurrentStage != "" {
		vm.CurrentStepText = record.CurrentStage
	} else if len(record.Steps) > 0 {
		last := record.Steps[len(record.Steps)-1]
		vm.CurrentStepText = fmt.Sprintf("步骤 %d · %s", last.Index+1, last.Type)
	}
	for _, screenshot := range record.Screenshots {
		if _, pathErr := store.ArtifactPath(record.RunID, screenshot); pathErr != nil {
			continue
		}
		vm.Screenshots = append(vm.Screenshots, validationScreenshotVM{
			Path: screenshot,
			URL:  "/validation/" + url.PathEscape(record.RunID) + "/artifact/" + strings.TrimPrefix(filepath.ToSlash(screenshot), "/"),
			Name: filepath.Base(screenshot),
		})
	}
	if data, logErr := store.ReadLog(record.RunID, 64*1024); logErr == nil {
		vm.LogTail = string(data)
	} else {
		vm.LogError = logErr.Error()
	}
	return vm
}

func isActionableValidation(store validationstore.Store, record validationstore.Record) bool {
	if record.Status == validationstore.StatusFailed || record.Status == validationstore.StatusInterrupted || record.Status == validationstore.StatusUnreadable {
		return true
	}
	if record.Status != validationstore.StatusPassed || len(record.Screenshots) == 0 {
		return false
	}
	review, err := store.Review(record.RunID)
	return err != nil || review.Status == "" || review.Status == validationstore.ReviewPending || review.Status == validationstore.ReviewIssue
}

func validationStatusLabel(status validationstore.Status) string {
	switch status {
	case validationstore.StatusRunning:
		return "运行中"
	case validationstore.StatusPassed:
		return "采集完成"
	case validationstore.StatusFailed:
		return "失败"
	case validationstore.StatusCanceled:
		return "已取消"
	case validationstore.StatusInterrupted:
		return "失联待确认"
	case validationstore.StatusUnreadable:
		return "记录不可读"
	default:
		return string(status)
	}
}

func validationReviewLabel(status validationstore.ReviewStatus) string {
	switch status {
	case validationstore.ReviewAccepted:
		return "已看过，无问题"
	case validationstore.ReviewIssue:
		return "发现问题"
	case validationstore.ReviewPending:
		return "待审阅"
	default:
		return "待审阅"
	}
}

func validationDuration(record validationstore.Record) string {
	if record.StartedAt.IsZero() {
		return "—"
	}
	end := record.FinishedAt
	if end.IsZero() {
		end = time.Now().UTC()
	}
	d := end.Sub(record.StartedAt)
	if d < 0 {
		d = 0
	}
	if d < time.Second {
		return fmt.Sprintf("%d ms", d.Milliseconds())
	}
	return d.Round(time.Second).String()
}

func (s *Server) handleValidationRunScript(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	scriptPath := strings.TrimSpace(r.FormValue("script"))
	script, err := s.findValidationScript(scriptPath)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	if !script.Runnable {
		http.Error(w, "脚本当前不可运行: "+script.Error, http.StatusBadRequest)
		return
	}
	if hasActiveValidation(s.validationStore(), script.Target) {
		http.Error(w, "该 target 已有运行中的验证", http.StatusConflict)
		return
	}

	visible := r.FormValue("visible") == "1"
	reason := strings.TrimSpace(r.FormValue("reason"))
	if reason == "" {
		reason = "Dashboard: " + script.Name
	}
	args := s.validationScriptArgs(script, visible, reason)
	exe := s.opts.GNBPath
	if exe == "" {
		exe, err = os.Executable()
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
	}
	cmd := exec.Command(exe, args...)
	cmd.Dir = s.opts.RepoRoot
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	startedAt := time.Now().UTC()
	if err := cmd.Start(); err != nil {
		http.Error(w, "启动验证失败: "+err.Error(), http.StatusInternalServerError)
		return
	}
	go func() { _ = cmd.Wait() }()
	runID := ""
	if run, ok := waitForValidationRun(r.Context(), s.validationStore(), script.Target, cmd.Process.Pid, startedAt); ok {
		runID = run.RunID
	}

	query := url.Values{
		"tab":           {"validation"},
		"target":        {r.FormValue("target")},
		"status":        {r.FormValue("status")},
		"script-target": {r.FormValue("script-target")},
		"flash":         {"已启动「" + script.Name + "」验证"},
	}
	if runID != "" {
		query.Set("run", runID)
	}
	vm := s.buildHeader("validation")
	vm.ValidationVM = s.buildValidationVM(query)
	w.WriteHeader(http.StatusAccepted)
	s.render(w, "tab_validation", vm)
}

func (s *Server) findValidationScript(path string) (validationScriptVM, error) {
	clean := filepath.ToSlash(filepath.Clean(filepath.FromSlash(path)))
	if clean == "." || filepath.IsAbs(path) || strings.HasPrefix(clean, "../") {
		return validationScriptVM{}, fmt.Errorf("invalid validation script path")
	}
	scripts, errText := s.buildValidationScripts()
	if errText != "" {
		return validationScriptVM{}, fmt.Errorf("读取验证脚本失败: %s", errText)
	}
	for _, script := range scripts {
		if script.Path == clean {
			return script, nil
		}
	}
	return validationScriptVM{}, fmt.Errorf("验证脚本不存在: %s", path)
}

func (s *Server) validationScriptArgs(script validationScriptVM, visible bool, reason string) []string {
	scriptPath := filepath.Join(s.opts.RepoRoot, filepath.FromSlash(script.Path))
	args := []string{"--repo-root", s.opts.RepoRoot, "--preset", s.opts.Preset, "validate", "--script", scriptPath, "--target", script.Target, "--reason", reason}
	if script.Scene != "" {
		args = append(args, "--scene", script.Scene)
	}
	if script.Width > 0 {
		args = append(args, "--width", strconv.Itoa(script.Width))
	}
	if script.Height > 0 {
		args = append(args, "--height", strconv.Itoa(script.Height))
	}
	if visible {
		args = append(args, "--visible")
	}
	return args
}

func (s *Server) handleValidationSteps(w http.ResponseWriter, r *http.Request) {
	store := s.validationStore()
	record, err := store.Load(r.PathValue("id"))
	if err != nil {
		http.NotFound(w, r)
		return
	}
	s.render(w, "validation_steps_panel", s.makeValidationRunVM(store, record))
}

func (s *Server) handleValidationArtifact(w http.ResponseWriter, r *http.Request) {
	store := s.validationStore()
	runID := r.PathValue("id")
	record, err := store.Load(runID)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	rel := filepath.ToSlash(filepath.Clean(filepath.FromSlash(r.PathValue("path"))))
	registered := false
	for _, screenshot := range record.Screenshots {
		if filepath.ToSlash(filepath.Clean(screenshot)) == rel {
			registered = true
			break
		}
	}
	if !registered {
		http.NotFound(w, r)
		return
	}
	path, err := store.ArtifactPath(runID, rel)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	info, err := os.Stat(path)
	if err != nil || info.IsDir() {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Cache-Control", "no-store")
	http.ServeFile(w, r, path)
}

func (s *Server) handleValidationLog(w http.ResponseWriter, r *http.Request) {
	store := s.validationStore()
	if _, err := store.Load(r.PathValue("id")); err != nil {
		http.NotFound(w, r)
		return
	}
	path, err := store.ArtifactPath(r.PathValue("id"), "runner.log")
	if err != nil {
		http.NotFound(w, r)
		return
	}
	if _, err := os.Stat(path); err != nil {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.Header().Set("Content-Disposition", `attachment; filename="runner.log"`)
	http.ServeFile(w, r, path)
}

func (s *Server) handleValidationReview(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	store := s.validationStore()
	runID := r.PathValue("id")
	if _, err := store.Load(runID); err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	status := validationstore.ReviewStatus(strings.TrimSpace(r.FormValue("status")))
	if status == "" {
		status = validationstore.ReviewPending
	}
	author := strings.TrimSpace(r.FormValue("author"))
	if author == "" {
		author = os.Getenv("USER")
	}
	review := validationstore.Review{Status: status, Message: strings.TrimSpace(r.FormValue("message")), Author: author, Screenshot: strings.TrimSpace(r.FormValue("screenshot"))}
	if review.Screenshot != "" {
		if _, err := store.ArtifactPath(runID, review.Screenshot); err != nil {
			http.Error(w, "invalid screenshot: "+err.Error(), http.StatusBadRequest)
			return
		}
	}
	if err := store.WriteReview(runID, review); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	query := url.Values{"tab": {"validation"}, "run": {runID}, "flash": {"审阅已保存"}}
	vm := s.buildHeader("validation")
	vm.ValidationVM = s.buildValidationVM(query)
	s.render(w, "tab_validation", vm)
}

func (s *Server) handleValidationRerun(w http.ResponseWriter, r *http.Request) {
	store := s.validationStore()
	_ = store.ReconcileStale(5 * time.Second)
	runID := r.PathValue("id")
	record, err := store.Load(runID)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	if record.Status == validationstore.StatusRunning {
		http.Error(w, "该 target 已有运行中的验证", http.StatusConflict)
		return
	}
	if hasActiveValidation(store, record.Target) {
		http.Error(w, "该 target 已有运行中的验证", http.StatusConflict)
		return
	}
	if record.Status == validationstore.StatusUnreadable || record.Kind == "" {
		http.Error(w, "该运行记录不能重跑", http.StatusBadRequest)
		return
	}
	visible := r.FormValue("visible") == "1"
	args, err := s.validationRerunArgs(store, record, visible)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	exe := s.opts.GNBPath
	if exe == "" {
		exe, err = os.Executable()
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
	}
	cmd := exec.Command(exe, args...)
	cmd.Dir = s.opts.RepoRoot
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	startedAt := time.Now().UTC()
	if err := cmd.Start(); err != nil {
		http.Error(w, "启动重跑失败: "+err.Error(), http.StatusInternalServerError)
		return
	}
	go func() { _ = cmd.Wait() }()
	selectedRunID := runID
	if run, ok := waitForValidationRun(r.Context(), store, record.Target, cmd.Process.Pid, startedAt); ok {
		selectedRunID = run.RunID
	}
	query := url.Values{
		"tab":           {"validation"},
		"run":           {selectedRunID},
		"target":        {r.FormValue("target")},
		"status":        {r.FormValue("status")},
		"script-target": {r.FormValue("script-target")},
		"flash":         {"已按原脚本启动新的后台重跑"},
	}
	vm := s.buildHeader("validation")
	vm.ValidationVM = s.buildValidationVM(query)
	w.WriteHeader(http.StatusAccepted)
	s.render(w, "tab_validation", vm)
}

func waitForValidationRun(ctx context.Context, store validationstore.Store, target string, runnerPID int, after time.Time) (validationstore.Record, bool) {
	deadline := time.NewTimer(1500 * time.Millisecond)
	defer deadline.Stop()
	ticker := time.NewTicker(20 * time.Millisecond)
	defer ticker.Stop()
	find := func() (validationstore.Record, bool) {
		runs, err := store.List(validationstore.Filter{Target: target})
		if err != nil {
			return validationstore.Record{}, false
		}
		for _, run := range runs {
			if run.Status != validationstore.StatusUnreadable && run.RunnerPID == runnerPID && !run.StartedAt.Before(after) {
				return run, true
			}
		}
		return validationstore.Record{}, false
	}
	if run, ok := find(); ok {
		return run, true
	}
	for {
		select {
		case <-ctx.Done():
			return validationstore.Record{}, false
		case <-deadline.C:
			return validationstore.Record{}, false
		case <-ticker.C:
			if run, ok := find(); ok {
				return run, true
			}
		}
	}
}

func hasActiveValidation(store validationstore.Store, target string) bool {
	runs, err := store.List(validationstore.Filter{Target: target, Status: validationstore.StatusRunning})
	return err == nil && len(runs) > 0
}

func (s *Server) validationRerunArgs(store validationstore.Store, record validationstore.Record, visible bool) ([]string, error) {
	scriptPath, err := store.ScriptPath(record.RunID)
	if err != nil {
		return nil, err
	}
	if _, err := os.Stat(scriptPath); err != nil {
		return nil, fmt.Errorf("缺少脚本快照: %w", err)
	}
	args := []string{"--repo-root", s.opts.RepoRoot, "--preset", s.opts.Preset}
	if record.Kind == "shot" {
		args = append(args, "shot", "--target", record.Target, "--rerun-of", record.RunID)
		if record.Scene != "" {
			args = append(args, "--scene", record.Scene)
		}
		if record.Frames > 0 {
			args = append(args, "--frames", strconv.Itoa(record.Frames))
		}
		if record.IncludeUI {
			args = append(args, "--ui")
		}
		if record.Visible || visible {
			args = append(args, "--visible")
		}
	} else {
		args = append(args, "validate", "--script", scriptPath, "--target", record.Target, "--rerun-of", record.RunID)
		if record.Scene != "" {
			args = append(args, "--scene", record.Scene)
		}
		if record.Width > 0 {
			args = append(args, "--width", strconv.Itoa(record.Width))
		}
		if record.Height > 0 {
			args = append(args, "--height", strconv.Itoa(record.Height))
		}
		if record.Visible || visible {
			args = append(args, "--visible")
		}
		if record.SyncValidation {
			args = append(args, "--sync-validation")
		}
	}
	if record.Reason != "" {
		args = append(args, "--reason", "重跑: "+record.Reason)
	}
	args = append(args, record.ExtraArgs...)
	return args, nil
}
