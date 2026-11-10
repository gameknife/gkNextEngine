// Package validationstore persists validation runs independently from the
// dashboard.  The CLI is the writer of execution state; the dashboard only
// needs this package to discover and annotate runs.
package validationstore

import (
	"bytes"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

const SchemaVersion = 1

type Status string

const (
	StatusRunning     Status = "running"
	StatusPassed      Status = "passed"
	StatusFailed      Status = "failed"
	StatusCanceled    Status = "canceled"
	StatusInterrupted Status = "interrupted"
	StatusUnreadable  Status = "unreadable"
)

type ReviewStatus string

const (
	ReviewPending  ReviewStatus = "pending"
	ReviewAccepted ReviewStatus = "accepted"
	ReviewIssue    ReviewStatus = "issue"
)

type Review struct {
	Status     ReviewStatus `json:"status"`
	Message    string       `json:"message,omitempty"`
	Author     string       `json:"author,omitempty"`
	Screenshot string       `json:"screenshot,omitempty"`
	UpdatedAt  time.Time    `json:"updatedAt"`
}

type Step struct {
	Index       int       `json:"index"`
	Type        string    `json:"type"`
	Description string    `json:"description,omitempty"`
	Query       string    `json:"query,omitempty"`
	Op          string    `json:"op,omitempty"`
	Expected    any       `json:"expected,omitempty"`
	Actual      any       `json:"actual,omitempty"`
	StartedAt   time.Time `json:"startedAt"`
	FinishedAt  time.Time `json:"finishedAt,omitempty"`
	DurationMs  int64     `json:"durationMs,omitempty"`
	Passed      bool      `json:"passed"`
	Message     string    `json:"message,omitempty"`
	Screenshot  string    `json:"screenshot,omitempty"`
}

// Record is the durable machine-owned state of one validation invocation.
// Review is deliberately stored in review.json and is not part of this type.
type Record struct {
	SchemaVersion  int    `json:"schemaVersion"`
	RunID          string `json:"runId"`
	Kind           string `json:"kind"`
	Name           string `json:"name"`
	Target         string `json:"target"`
	Preset         string `json:"preset"`
	Reason         string `json:"reason,omitempty"`
	ScriptSource   string `json:"scriptSource,omitempty"`
	ScriptSnapshot string `json:"scriptSnapshot,omitempty"`

	Parameters     []string `json:"parameters,omitempty"`
	ExtraArgs      []string `json:"extraArgs,omitempty"`
	Scene          string   `json:"scene,omitempty"`
	Width          int      `json:"width,omitempty"`
	Height         int      `json:"height,omitempty"`
	Visible        bool     `json:"visible,omitempty"`
	SyncValidation bool     `json:"syncValidation,omitempty"`
	Frames         int      `json:"frames,omitempty"`
	IncludeUI      bool     `json:"includeUI,omitempty"`

	GitHead  string `json:"gitHead,omitempty"`
	GitDirty bool   `json:"gitDirty"`

	StartedAt    time.Time `json:"startedAt"`
	FinishedAt   time.Time `json:"finishedAt,omitempty"`
	HeartbeatAt  time.Time `json:"heartbeatAt"`
	RunnerPID    int       `json:"runnerPid,omitempty"`
	Status       Status    `json:"status"`
	Phase        string    `json:"phase,omitempty"`
	CurrentStep  int       `json:"currentStep,omitempty"`
	CurrentStage string    `json:"currentStage,omitempty"`
	ExitCode     *int      `json:"exitCode,omitempty"`
	Error        string    `json:"error,omitempty"`
	Steps        []Step    `json:"steps,omitempty"`
	Screenshots  []string  `json:"screenshots,omitempty"`
	RerunOf      string    `json:"rerunOf,omitempty"`
}

type Filter struct {
	Target string
	Status Status
	Limit  int
	Offset int
}

type Store struct {
	Root string
}

var validationMu sync.Mutex

func New(repoRoot, preset string) Store {
	return NewAt(filepath.Join(platform.BinDir(repoRoot, preset), "..", "validation_runs"))
}

func NewAt(root string) Store {
	abs, err := filepath.Abs(root)
	if err == nil {
		root = abs
	}
	return Store{Root: filepath.Clean(root)}
}

func (s Store) RunDir(runID string) (string, error) {
	if !validRunID(runID) {
		return "", fmt.Errorf("invalid validation run id %q", runID)
	}
	return filepath.Join(s.Root, runID), nil
}

func (s Store) Create(r Record, script []byte) (Record, error) {
	if r.RunID == "" {
		var err error
		r.RunID, err = newRunID()
		if err != nil {
			return Record{}, err
		}
	}
	if !validRunID(r.RunID) {
		return Record{}, fmt.Errorf("invalid validation run id %q", r.RunID)
	}
	if r.SchemaVersion == 0 {
		r.SchemaVersion = SchemaVersion
	}
	if r.StartedAt.IsZero() {
		r.StartedAt = time.Now().UTC()
	}
	if r.HeartbeatAt.IsZero() {
		r.HeartbeatAt = r.StartedAt
	}
	if r.Status == "" {
		r.Status = StatusRunning
	}
	dir, err := s.RunDir(r.RunID)
	if err != nil {
		return Record{}, err
	}
	validationMu.Lock()
	defer validationMu.Unlock()
	if err := os.MkdirAll(filepath.Join(dir, "screenshots"), 0o755); err != nil {
		return Record{}, fmt.Errorf("create validation evidence directory: %w", err)
	}
	if _, err := os.Stat(filepath.Join(dir, "run.json")); err == nil {
		return Record{}, fmt.Errorf("validation run %s already exists", r.RunID)
	} else if !errors.Is(err, os.ErrNotExist) {
		return Record{}, err
	}
	if err := atomicWriteJSON(filepath.Join(dir, "run.json"), r); err != nil {
		return Record{}, err
	}
	if err := atomicWrite(filepath.Join(dir, "script.json"), script, 0o644); err != nil {
		return Record{}, err
	}
	if err := atomicWrite(filepath.Join(dir, "runner.log"), nil, 0o644); err != nil {
		return Record{}, err
	}
	return r, nil
}

func (s Store) Load(runID string) (Record, error) {
	dir, err := s.RunDir(runID)
	if err != nil {
		return Record{}, err
	}
	var r Record
	if err := readJSON(filepath.Join(dir, "run.json"), &r); err != nil {
		return Record{}, fmt.Errorf("read validation run %s: %w", runID, err)
	}
	if r.RunID == "" {
		r.RunID = runID
	}
	return r, nil
}

func (s Store) Update(runID string, fn func(*Record) error) error {
	validationMu.Lock()
	defer validationMu.Unlock()
	r, err := s.loadUnlocked(runID)
	if err != nil {
		return err
	}
	if err := fn(&r); err != nil {
		return err
	}
	return atomicWriteJSON(filepath.Join(s.Root, runID, "run.json"), r)
}

func (s Store) Touch(runID string) error {
	return s.Update(runID, func(r *Record) error {
		r.HeartbeatAt = time.Now().UTC()
		return nil
	})
}

func (s Store) AppendLog(runID string, data []byte) error {
	dir, err := s.RunDir(runID)
	if err != nil {
		return err
	}
	validationMu.Lock()
	defer validationMu.Unlock()
	f, err := os.OpenFile(filepath.Join(dir, "runner.log"), os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = f.Write(data)
	return err
}

func (s Store) WriteScript(runID string, data []byte) error {
	dir, err := s.RunDir(runID)
	if err != nil {
		return err
	}
	return atomicWrite(filepath.Join(dir, "script.json"), data, 0o644)
}

func (s Store) ScriptPath(runID string) (string, error) {
	dir, err := s.RunDir(runID)
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "script.json"), nil
}

func (s Store) Review(runID string) (Review, error) {
	dir, err := s.RunDir(runID)
	if err != nil {
		return Review{}, err
	}
	var review Review
	if err := readJSON(filepath.Join(dir, "review.json"), &review); err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return Review{}, nil
		}
		return Review{}, err
	}
	return review, nil
}

func (s Store) WriteReview(runID string, review Review) error {
	dir, err := s.RunDir(runID)
	if err != nil {
		return err
	}
	if review.Status != ReviewPending && review.Status != ReviewAccepted && review.Status != ReviewIssue {
		return fmt.Errorf("invalid review status %q", review.Status)
	}
	review.UpdatedAt = time.Now().UTC()
	validationMu.Lock()
	defer validationMu.Unlock()
	return atomicWriteJSON(filepath.Join(dir, "review.json"), review)
}

func (s Store) LogPath(runID string) (string, error) {
	dir, err := s.RunDir(runID)
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "runner.log"), nil
}

func (s Store) ReadLog(runID string, tailBytes int) ([]byte, error) {
	path, err := s.LogPath(runID)
	if err != nil {
		return nil, err
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	if tailBytes <= 0 {
		return io.ReadAll(f)
	}
	info, err := f.Stat()
	if err != nil {
		return nil, err
	}
	start := info.Size() - int64(tailBytes)
	if start < 0 {
		start = 0
	}
	if _, err := f.Seek(start, io.SeekStart); err != nil {
		return nil, err
	}
	return io.ReadAll(f)
}

// ArtifactPath accepts only paths inside the selected run directory and
// rejects symlinks that resolve outside it. It is the single gate used by
// dashboard artifact handlers.
func (s Store) ArtifactPath(runID, rel string) (string, error) {
	dir, err := s.RunDir(runID)
	if err != nil {
		return "", err
	}
	clean := filepath.Clean(filepath.FromSlash(rel))
	if clean == "." || filepath.IsAbs(clean) || clean == ".." || strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
		return "", fmt.Errorf("artifact path escapes validation run")
	}
	full := filepath.Join(dir, clean)
	inside, err := filepath.Rel(dir, full)
	if err != nil || inside == ".." || strings.HasPrefix(inside, ".."+string(filepath.Separator)) {
		return "", fmt.Errorf("artifact path escapes validation run")
	}
	resolvedDir, err := filepath.EvalSymlinks(dir)
	if err != nil {
		return "", err
	}
	resolved, err := filepath.EvalSymlinks(full)
	if err != nil {
		return "", err
	}
	inside, err = filepath.Rel(resolvedDir, resolved)
	if err != nil || inside == ".." || strings.HasPrefix(inside, ".."+string(filepath.Separator)) {
		return "", fmt.Errorf("artifact symlink escapes validation run")
	}
	return full, nil
}

// List returns at most Filter.Limit records, newest first. A malformed run
// is represented as an unreadable record so one bad directory cannot blank
// the whole Dashboard.
func (s Store) List(filter Filter) ([]Record, error) {
	entries, err := os.ReadDir(s.Root)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return []Record{}, nil
		}
		return nil, err
	}
	runs := make([]Record, 0, len(entries))
	for _, entry := range entries {
		if !entry.IsDir() || !validRunID(entry.Name()) {
			continue
		}
		r, loadErr := s.Load(entry.Name())
		if loadErr != nil {
			r = Record{SchemaVersion: SchemaVersion, RunID: entry.Name(), Name: "记录不可读", Status: StatusUnreadable, Error: loadErr.Error()}
		}
		if filter.Target != "" && r.Target != filter.Target {
			continue
		}
		if filter.Status != "" && r.Status != filter.Status {
			continue
		}
		runs = append(runs, r)
	}
	sort.SliceStable(runs, func(i, j int) bool {
		if runs[i].StartedAt.Equal(runs[j].StartedAt) {
			return runs[i].RunID > runs[j].RunID
		}
		return runs[i].StartedAt.After(runs[j].StartedAt)
	})
	offset := filter.Offset
	if offset < 0 {
		offset = 0
	}
	if offset >= len(runs) {
		return []Record{}, nil
	}
	runs = runs[offset:]
	if filter.Limit > 0 && len(runs) > filter.Limit {
		runs = runs[:filter.Limit]
	}
	return runs, nil
}

// ReconcileStale marks an old running record interrupted only when its runner
// process is no longer alive. A stale heartbeat alone is intentionally not
// enough to claim an engine crash.
func (s Store) ReconcileStale(maxAge time.Duration) error {
	all, err := s.List(Filter{})
	if err != nil {
		return err
	}
	now := time.Now()
	for _, r := range all {
		if r.Status != StatusRunning || r.HeartbeatAt.IsZero() || now.Sub(r.HeartbeatAt) <= maxAge || r.RunnerPID <= 0 || processAlive(r.RunnerPID) {
			continue
		}
		_ = s.Update(r.RunID, func(current *Record) error {
			if current.Status == StatusRunning {
				current.Status = StatusInterrupted
				current.Phase = "interrupted"
				current.Error = "runner heartbeat expired and the runner process is no longer alive"
				current.FinishedAt = time.Now().UTC()
			}
			return nil
		})
	}
	return nil
}

func (s Store) loadUnlocked(runID string) (Record, error) {
	dir, err := s.RunDir(runID)
	if err != nil {
		return Record{}, err
	}
	var r Record
	if err := readJSON(filepath.Join(dir, "run.json"), &r); err != nil {
		return Record{}, err
	}
	return r, nil
}

func atomicWriteJSON(path string, value any) error {
	b, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return err
	}
	b = append(b, '\n')
	return atomicWrite(path, b, 0o644)
}

func atomicWrite(path string, data []byte, mode os.FileMode) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	tmp, err := os.CreateTemp(filepath.Dir(path), ".validation-*")
	if err != nil {
		return err
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName)
	if err := tmp.Chmod(mode); err != nil {
		tmp.Close()
		return err
	}
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Sync(); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	return os.Rename(tmpName, path)
}

func readJSON(path string, dst any) error {
	b, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	return json.NewDecoder(bytes.NewReader(b)).Decode(dst)
}

func newRunID() (string, error) {
	b := make([]byte, 5)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return time.Now().UTC().Format("20060102-150405.000") + "-" + hex.EncodeToString(b), nil
}

func validRunID(id string) bool {
	return id != "" && id != "." && id != ".." && filepath.Base(id) == id && !strings.ContainsAny(id, `/\\`) && !strings.ContainsRune(id, 0)
}

func processAlive(pid int) bool {
	if pid <= 0 {
		return false
	}
	process, err := os.FindProcess(pid)
	if err != nil {
		return false
	}
	if runtime.GOOS == "windows" {
		output, err := exec.Command("tasklist", "/FI", fmt.Sprintf("PID eq %d", pid), "/NH").Output()
		return err == nil && !bytes.Contains(output, []byte("No tasks"))
	}
	if err := process.Signal(syscall.Signal(0)); err == nil {
		return true
	}
	return !errors.Is(err, os.ErrProcessDone)
}
