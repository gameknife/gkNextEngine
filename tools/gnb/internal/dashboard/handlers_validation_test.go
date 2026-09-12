package dashboard

import (
	"context"
	"html/template"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/validationstore"
)

func TestValidationTabRendersRunsAndFailureEvidence(t *testing.T) {
	s := setupTODORepo(t)
	s.validation = validationstore.NewAt(filepath.Join(s.opts.RepoRoot, "validation_runs"))
	run, err := s.validation.Create(validationstore.Record{
		Name: "smoke", Kind: "validate", Target: "gkNextRenderer", Status: validationstore.StatusFailed,
		Phase: "step", Error: "assertion failed", StartedAt: time.Now().Add(-time.Second), FinishedAt: time.Now(),
		Steps:       []validationstore.Step{{Index: 0, Type: "assert", Query: "scene.nodeCount", Op: "ge", Expected: float64(10), Actual: float64(2), Passed: false, Message: "assertion failed"}},
		Screenshots: []string{"screenshots/failure.jpg"},
	}, []byte(`{"name":"smoke"}`))
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(s.validation.Root, run.RunID, "screenshots", "failure.jpg"), []byte("fake"), 0o644); err != nil {
		t.Fatal(err)
	}
	req := httptest.NewRequest("GET", "/tab/validation?run="+run.RunID, nil)
	req.SetPathValue("kind", "validation")
	rec := httptest.NewRecorder()
	s.handleTab(rec, req)
	if rec.Code != 200 {
		t.Fatalf("status = %d (%s)", rec.Code, rec.Body.String())
	}
	for _, want := range []string{"验证中心", "assertion failed", "scene.nodeCount", "failure.jpg", "可见重跑"} {
		if !strings.Contains(rec.Body.String(), want) {
			t.Fatalf("validation page missing %q:\n%s", want, rec.Body.String())
		}
	}
}

func TestValidationReviewHandlerLeavesRunStatusUntouched(t *testing.T) {
	s := setupTODORepo(t)
	s.validation = validationstore.NewAt(filepath.Join(s.opts.RepoRoot, "validation_runs"))
	run, err := s.validation.Create(validationstore.Record{Name: "shot", Kind: "shot", Status: validationstore.StatusPassed}, nil)
	if err != nil {
		t.Fatal(err)
	}
	req := httptest.NewRequest("POST", "/validation/"+run.RunID+"/review", strings.NewReader("status=accepted&message=看过"))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.SetPathValue("id", run.RunID)
	rec := httptest.NewRecorder()
	s.handleValidationReview(rec, req)
	if rec.Code != 200 {
		t.Fatalf("status = %d (%s)", rec.Code, rec.Body.String())
	}
	got, err := s.validation.Load(run.RunID)
	if err != nil || got.Status != validationstore.StatusPassed {
		t.Fatalf("run = %+v, err=%v", got, err)
	}
	review, err := s.validation.Review(run.RunID)
	if err != nil || review.Status != validationstore.ReviewAccepted {
		t.Fatalf("review = %+v, err=%v", review, err)
	}
}

func TestValidationTemplatesParseWithValidationTab(t *testing.T) {
	if _, err := template.New("dashboard").Funcs(templateFuncs()).ParseFS(templateFS, "templates/*.html"); err != nil {
		t.Fatal(err)
	}
}

func TestBuildValidationScriptsDiscoversRunnableAndUnbuiltScripts(t *testing.T) {
	s := setupTODORepo(t)
	s.opts.Preset = "test"
	scriptRoot := filepath.Join(s.opts.RepoRoot, "assets", "agentscripts")
	if err := os.MkdirAll(scriptRoot, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(scriptRoot, "smoke.agentscript.json"), []byte(`{
  "name": "dashboard smoke",
  "target": "FakeRenderer",
  "scene": "assets/models/playground.glb",
  "viewport": {"width": 800, "height": 450},
  "steps": [{"type": "wait-frames"}, {"type": "quit"}]
}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(scriptRoot, "unbuilt.agentscript.json"), []byte(`{
  "name": "unbuilt",
  "target": "MissingRenderer",
  "steps": []
}`), 0o644); err != nil {
		t.Fatal(err)
	}
	binDir := filepath.Join(s.opts.RepoRoot, "out", "build", s.opts.Preset, "bin")
	if err := os.MkdirAll(binDir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(platform.ExecutablePath(binDir, "FakeRenderer"), []byte("fake"), 0o755); err != nil {
		t.Fatal(err)
	}

	scripts, errText := s.buildValidationScripts()
	if errText != "" || len(scripts) != 2 {
		t.Fatalf("scripts = %+v, error = %q", scripts, errText)
	}
	if !scripts[0].Runnable || scripts[0].Name != "dashboard smoke" || scripts[0].StepCount != 2 || scripts[0].Width != 800 || scripts[0].Height != 450 {
		t.Fatalf("runnable script = %+v", scripts[0])
	}
	if scripts[1].Runnable || scripts[1].Error != "目标未构建" {
		t.Fatalf("unbuilt script = %+v", scripts[1])
	}
	vm := s.buildValidationVM(url.Values{"script-target": {"FakeRenderer"}})
	if len(vm.Scripts) != 1 || vm.Scripts[0].Target != "FakeRenderer" {
		t.Fatalf("filtered scripts = %+v", vm.Scripts)
	}
	if _, err := s.findValidationScript("../secret.agentscript.json"); err == nil {
		t.Fatal("findValidationScript accepted traversal")
	}
}

func TestValidationRunScriptHandlerStartsGNB(t *testing.T) {
	s := setupTODORepo(t)
	s.opts.Preset = "test"
	s.opts.GNBPath = "/usr/bin/true"
	scriptRoot := filepath.Join(s.opts.RepoRoot, "assets", "agentscripts")
	if err := os.MkdirAll(scriptRoot, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(scriptRoot, "smoke.agentscript.json"), []byte(`{
  "name": "dashboard smoke",
  "target": "FakeRenderer",
  "steps": []
}`), 0o644); err != nil {
		t.Fatal(err)
	}
	binDir := filepath.Join(s.opts.RepoRoot, "out", "build", s.opts.Preset, "bin")
	if err := os.MkdirAll(binDir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(platform.ExecutablePath(binDir, "FakeRenderer"), []byte("fake"), 0o755); err != nil {
		t.Fatal(err)
	}
	req := httptest.NewRequest("POST", "/validation/run-script", strings.NewReader("script=assets%2Fagentscripts%2Fsmoke.agentscript.json&visible=1"))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	rec := httptest.NewRecorder()
	s.handleValidationRunScript(rec, req)
	if rec.Code != http.StatusAccepted {
		t.Fatalf("status = %d (%s)", rec.Code, rec.Body.String())
	}
	for _, want := range []string{"验证脚本", "dashboard smoke", "已启动"} {
		if !strings.Contains(rec.Body.String(), want) {
			t.Fatalf("response missing %q:\n%s", want, rec.Body.String())
		}
	}
}

func TestValidationStepsHandlerStopsPollingAfterRunFinishes(t *testing.T) {
	s := setupTODORepo(t)
	s.validation = validationstore.NewAt(filepath.Join(s.opts.RepoRoot, "validation_runs"))
	run, err := s.validation.Create(validationstore.Record{
		Name: "running", Kind: "validate", Target: "gkNextRenderer", Status: validationstore.StatusRunning,
		Steps: []validationstore.Step{{Index: 0, Type: "wait-frames", Passed: true}},
	}, nil)
	if err != nil {
		t.Fatal(err)
	}

	req := httptest.NewRequest("GET", "/validation/"+run.RunID+"/steps", nil)
	req.SetPathValue("id", run.RunID)
	rec := httptest.NewRecorder()
	s.handleValidationSteps(rec, req)
	if rec.Code != http.StatusOK || !strings.Contains(rec.Body.String(), `id="validation-steps-card"`) || !strings.Contains(rec.Body.String(), `hx-get="/validation/`+run.RunID+`/steps"`) {
		t.Fatalf("running response = %d:\n%s", rec.Code, rec.Body.String())
	}
	if err := s.validation.Update(run.RunID, func(record *validationstore.Record) error {
		record.Status = validationstore.StatusPassed
		return nil
	}); err != nil {
		t.Fatal(err)
	}
	req = httptest.NewRequest("GET", "/validation/"+run.RunID+"/steps", nil)
	req.SetPathValue("id", run.RunID)
	rec = httptest.NewRecorder()
	s.handleValidationSteps(rec, req)
	if rec.Code != http.StatusOK || strings.Contains(rec.Body.String(), `hx-get="/validation/`) {
		t.Fatalf("finished response = %d:\n%s", rec.Code, rec.Body.String())
	}
}

func TestWaitForValidationRunFindsNewRunnerRecord(t *testing.T) {
	store := validationstore.NewAt(filepath.Join(t.TempDir(), "validation_runs"))
	startedAt := time.Now().UTC()
	if _, err := store.Create(validationstore.Record{
		Name: "new run", Target: "gkNextRenderer", Status: validationstore.StatusRunning,
		RunnerPID: os.Getpid(), StartedAt: startedAt,
	}, nil); err != nil {
		t.Fatal(err)
	}
	if run, ok := waitForValidationRun(context.Background(), store, "gkNextRenderer", os.Getpid(), startedAt); !ok || run.Name != "new run" {
		t.Fatalf("run = %+v, ok = %v", run, ok)
	}
}
