package dashboard

import (
	"html/template"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/i18n"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

func newTestBuildServer(t *testing.T, repoRoot, preset string) *Server {
	t.Helper()
	tpl, err := template.New("dashboard").
		Funcs(templateFuncs()).
		ParseFS(templateFS, "templates/*.html")
	if err != nil {
		t.Fatal(err)
	}
	return &Server{
		opts: Options{
			RepoRoot: repoRoot,
			Preset:   preset,
			Lang:     i18n.LangZh,
			Config: config.Config{
				Targets: config.TargetsConfig{
					All: []string{"gkNextRenderer", "gkNextEditor"},
				},
			},
		},
		tpl:  tpl,
		jobs: NewJobManager(),
	}
}

func TestHandleTabBuildRendersElements(t *testing.T) {
	root := t.TempDir()
	srv := newTestBuildServer(t, root, "macos-arm64")

	req := httptest.NewRequest(http.MethodGet, "/tab/build", nil)
	req.SetPathValue("kind", "build")
	rec := httptest.NewRecorder()
	srv.handleTab(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rec.Code)
	}
	body := rec.Body.String()

	requiredSnippets := []string{
		`data-build-pane`,
		`build-run-targets`,
		`id="run-args-list"`,
		`id="custom-run-args-input"`,
		`id="run-cmd-preview-code"`,
		`id="reset-run-args-btn"`,
		`data-run-button`,
		`data-build-button`,
	}
	for _, snippet := range requiredSnippets {
		if !strings.Contains(body, snippet) {
			t.Errorf("rendered /tab/build body missing %q", snippet)
		}
	}
}

func TestHandleJobRunUnbuiltTargetReturnsFriendlyLogPanel(t *testing.T) {
	root := t.TempDir()
	srv := newTestBuildServer(t, root, "macos-arm64")

	form := url.Values{
		"target":    {"gkNextRenderer"},
		"extraArgs": {"--width 1280"},
	}
	req := httptest.NewRequest(http.MethodPost, "/jobs/run", strings.NewReader(form.Encode()))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("HX-Request", "true")
	req.SetPathValue("kind", "run")
	rec := httptest.NewRecorder()

	srv.handleJobStart(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200 with error log_panel for HTMX request, got %d", rec.Code)
	}
	body := rec.Body.String()
	if !strings.Contains(body, "可执行文件尚未生成") && !strings.Contains(body, "尚未编译") {
		t.Fatalf("expected unbuilt hint in log panel, got: %s", body)
	}
	if !strings.Contains(body, "log-panel") {
		t.Fatalf("expected log-panel in response, got: %s", body)
	}
}

func TestHandleJobRunAllTargetRejected(t *testing.T) {
	root := t.TempDir()
	srv := newTestBuildServer(t, root, "macos-arm64")

	form := url.Values{
		"target": {"all"},
	}
	req := httptest.NewRequest(http.MethodPost, "/jobs/run", strings.NewReader(form.Encode()))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("HX-Request", "true")
	req.SetPathValue("kind", "run")
	rec := httptest.NewRecorder()

	srv.handleJobStart(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200 with error panel, got %d", rec.Code)
	}
	body := rec.Body.String()
	if !strings.Contains(body, "不能为 all") {
		t.Fatalf("expected cannot run all hint, got: %s", body)
	}
}

func TestHandleJobRunBuiltTargetStartsJob(t *testing.T) {
	root := t.TempDir()
	preset := "macos-arm64"
	binDir := platform.BinDir(root, preset)
	if err := os.MkdirAll(binDir, 0o755); err != nil {
		t.Fatal(err)
	}
	exePath := platform.ExecutablePath(binDir, "MockApp")

	script := "#!/bin/sh\necho 'MockApp started'\nexit 0\n"
	if err := os.WriteFile(exePath, []byte(script), 0o755); err != nil {
		t.Fatal(err)
	}

	srv := newTestBuildServer(t, root, preset)

	form := url.Values{
		"target":    {"MockApp"},
		"extraArgs": {"--width 800"},
	}
	req := httptest.NewRequest(http.MethodPost, "/jobs/run", strings.NewReader(form.Encode()))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("HX-Request", "true")
	req.SetPathValue("kind", "run")
	rec := httptest.NewRecorder()

	srv.handleJobStart(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d; body: %s", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, "MockApp") {
		t.Fatalf("expected target in log panel, got: %s", body)
	}
}

func TestHandleJobBuildWithReconfigure(t *testing.T) {
	root := t.TempDir()
	srv := newTestBuildServer(t, root, "macos-arm64")

	form := url.Values{
		"target":      {"gkNextRenderer"},
		"reconfigure": {"1"},
	}
	req := httptest.NewRequest(http.MethodPost, "/jobs/build", strings.NewReader(form.Encode()))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("HX-Request", "true")
	req.SetPathValue("kind", "build")
	rec := httptest.NewRecorder()

	srv.handleJobStart(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d; body: %s", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, "gkNextRenderer") {
		t.Fatalf("expected target in log panel, got: %s", body)
	}
}

func TestBuildJobSpecUsesGnbBuildWorkflow(t *testing.T) {
	root := t.TempDir()
	srv := newTestBuildServer(t, root, "windows")
	srv.opts.GNBPath = "C:/tools/gnb.exe"

	spec := srv.buildJobSpec("gkNextRenderer", true)

	if spec.Name != srv.opts.GNBPath {
		t.Fatalf("build executable = %q, want %q", spec.Name, srv.opts.GNBPath)
	}
	if spec.WorkDir != root {
		t.Fatalf("build work dir = %q, want %q", spec.WorkDir, root)
	}
	want := []string{"--repo-root", root, "--preset", "windows", "build", "gkNextRenderer", "--reconfigure"}
	if strings.Join(spec.Args, "\x00") != strings.Join(want, "\x00") {
		t.Fatalf("build args = %q, want %q", spec.Args, want)
	}
}

func TestBuildJobSpecUsesAllFlagForAllTargets(t *testing.T) {
	root := t.TempDir()
	srv := newTestBuildServer(t, root, "windows")
	srv.opts.GNBPath = "C:/tools/gnb.exe"

	spec := srv.buildJobSpec("all", false)

	if !strings.Contains(strings.Join(spec.Args, " "), "build --all") {
		t.Fatalf("all-target build args = %q, want build --all", spec.Args)
	}
}
