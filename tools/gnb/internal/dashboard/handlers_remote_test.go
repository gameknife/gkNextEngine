package dashboard

import (
	"html/template"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/i18n"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/remoteplay"
)

func setupRemoteServer(t *testing.T, repoRoot string, cfg config.Config) *Server {
	t.Helper()
	tpl, err := template.New("dashboard").
		Funcs(templateFuncs()).
		ParseFS(templateFS, "templates/*.html")
	if err != nil {
		t.Fatal(err)
	}
	return &Server{
		opts:  Options{RepoRoot: repoRoot, Preset: "windows", Config: cfg, Lang: i18n.LangZh},
		tpl:   tpl,
		jobs:  NewJobManager(),
		chats: NewChatStore(),
	}
}

func TestHandleTabRemoteRendersRemoteLauncher(t *testing.T) {
	repoRoot := t.TempDir()
	remoteAsset := filepath.Join(repoRoot, "assets", "remote")
	if err := os.MkdirAll(remoteAsset, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(remoteAsset, "index.html"), []byte("<!doctype html><title>stub remote</title>"), 0o644); err != nil {
		t.Fatal(err)
	}
	s := setupRemoteServer(t, repoRoot, config.Config{
		Targets: config.TargetsConfig{
			All: []string{"gkNextRenderer", "Packager", "gkNextEditor", "gkNextUnitTests"},
		},
	})

	req := httptest.NewRequest("GET", "/tab/remote", nil)
	req.SetPathValue("kind", "remote")
	rec := httptest.NewRecorder()

	s.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, "启动 Remote") {
		t.Fatalf("rendered body missing remote action:\n%s", body)
	}
	if !strings.Contains(body, "内嵌串流") || !strings.Contains(body, "data-remote-frame") {
		t.Fatalf("rendered body missing embedded remote surface:\n%s", body)
	}
	if !strings.Contains(body, `name="bind" value="0.0.0.0"`) {
		t.Fatalf("rendered body missing bind default:\n%s", body)
	}
	visibleBody := strings.SplitN(body, `<script type="application/x-gnb-remote-legacy">`, 2)[0]
	if !strings.Contains(visibleBody, `data-remote-diag-client`) {
		t.Fatalf("rendered body missing compact connection status:\n%s", body)
	}
	if strings.Contains(visibleBody, `data-remote-url-preview`) || strings.Contains(visibleBody, `data-remote-diag-url`) {
		t.Fatalf("rendered body should not include redundant remote diagnostics:\n%s", body)
	}
	if strings.Contains(visibleBody, `<aside class="tab-side">`) || !strings.Contains(visibleBody, `class="remote-dock remote-bottom-grid"`) {
		t.Fatalf("rendered body should use the full-width preview with a bottom control dock:\n%s", body)
	}
	for _, card := range []string{"remote-source-card", "remote-settings-card", "remote-controls-card"} {
		if !strings.Contains(visibleBody, card) {
			t.Fatalf("rendered body missing remote dock card %q:\n%s", card, body)
		}
	}
	if !strings.Contains(visibleBody, `class="remote-log-drawer"`) || strings.Contains(visibleBody, `remote-dock-card remote-log-host`) {
		t.Fatalf("rendered body should expose logs through the compact drawer:\n%s", body)
	}
	if !strings.Contains(visibleBody, `name="target"`) || !strings.Contains(visibleBody, `<details class="remote-advanced">`) {
		t.Fatalf("rendered body missing compact launcher controls:\n%s", body)
	}
	if !strings.Contains(body, "gkNextRenderer") || !strings.Contains(body, "gkNextEditor") {
		t.Fatalf("rendered body missing runnable targets:\n%s", body)
	}
	if strings.Contains(body, "Packager") || strings.Contains(body, "gkNextUnitTests") {
		t.Fatalf("rendered body should exclude non-remote targets:\n%s", body)
	}
}

func TestRemoteTabUsesPersistentDirectHostController(t *testing.T) {
	layout, err := templateFS.ReadFile("templates/layout.html")
	if err != nil {
		t.Fatal(err)
	}
	content := string(layout)

	if !strings.Contains(content, "function initRemoteTab(root)") ||
		!strings.Contains(content, "function remoteHostUrl(page)") {
		t.Fatal("remote controls must be owned by the persistent dashboard document")
	}
	if !strings.Contains(content, "'127.0.0.1'") ||
		!strings.Contains(content, "frame.dataset.remoteOrigin = frameUrl.origin;") {
		t.Fatal("remote iframe must use the verified loopback host and track its origin")
	}
	if !strings.Contains(content, "else if (kind === 'remote')") {
		t.Fatal("remote controls must initialise through initPaneComponents after an HTMX swap")
	}
	for _, marker := range []string{
		"function remoteStartConnectionAfterLaunch(page)",
		"function remoteStopJob(page)",
		"form?.addEventListener('htmx:afterRequest'",
		"function remoteFitFrame(page)",
		"data.type === 'video-size'",
	} {
		if !strings.Contains(content, marker) {
			t.Fatalf("persistent remote controller missing %q", marker)
		}
	}

	partial, err := templateFS.ReadFile("templates/partials.html")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(partial), `type="application/x-gnb-remote-legacy"`) {
		t.Fatal("swapped remote partial must not install a second controller")
	}
	if !strings.Contains(string(partial), `data-remote-start`) {
		t.Fatal("remote dock must expose the start/stop control")
	}

	client, err := os.ReadFile(filepath.Join("..", "..", "..", "..", "assets", "remote", "index.html"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(client), `postToParent("video-size"`) {
		t.Fatal("remote client must report its video dimensions to the dashboard")
	}
	if strings.Contains(string(client), "requestPointerLock") || strings.Contains(string(partial), "pointer-lock") {
		t.Fatal("remote input must not request browser pointer lock")
	}
}

func TestHandleRemoteClientServesRepoAsset(t *testing.T) {
	repoRoot := t.TempDir()
	remoteAsset := filepath.Join(repoRoot, "assets", "remote")
	if err := os.MkdirAll(remoteAsset, 0o755); err != nil {
		t.Fatal(err)
	}
	const html = "<!doctype html><title>gkNext Remote Play</title><body>remote</body>"
	if err := os.WriteFile(filepath.Join(remoteAsset, "index.html"), []byte(html), 0o644); err != nil {
		t.Fatal(err)
	}
	s := setupRemoteServer(t, repoRoot, config.Config{})

	req := httptest.NewRequest("GET", "/remote/client", nil)
	rec := httptest.NewRecorder()

	s.handleRemoteClient(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	if got := rec.Header().Get("Content-Type"); !strings.Contains(got, "text/html") {
		t.Fatalf("Content-Type = %q, want text/html", got)
	}
	if rec.Body.String() != html {
		t.Fatalf("body = %q, want %q", rec.Body.String(), html)
	}
}

func TestRemoteJobSpecBuildsDefaultRemoteArgs(t *testing.T) {
	repoRoot := t.TempDir()
	s := setupRemoteServer(t, repoRoot, config.Config{})

	spec, err := s.remoteJobSpec("gkNextRenderer", remoteplay.Options{
		Scene: "assets/models/playground.glb",
	}, []string{"--validation"})
	if err != nil {
		t.Fatalf("remoteJobSpec returned error: %v", err)
	}

	wantExe := platform.ExecutablePath(platform.BinDir(repoRoot, "windows"), "gkNextRenderer")
	wantArgs := remoteplay.RunArgs(remoteplay.Options{
		Bind:          "0.0.0.0",
		Encoder:       "auto",
		HttpPort:      8088,
		SignalingPort: 8089,
		Fps:           30,
		Scene:         "assets/models/playground.glb",
	}, []string{"--validation"})

	if spec.Kind != JobRemote || spec.Target != "gkNextRenderer" {
		t.Fatalf("spec identity = %+v, want remote gkNextRenderer", spec)
	}
	if spec.Name != wantExe || spec.WorkDir != platform.BinDir(repoRoot, "windows") {
		t.Fatalf("spec paths = (%q, %q), want (%q, %q)", spec.Name, spec.WorkDir, wantExe, platform.BinDir(repoRoot, "windows"))
	}
	if !reflect.DeepEqual(spec.Args, wantArgs) {
		t.Fatalf("spec args = %#v, want %#v", spec.Args, wantArgs)
	}
	if spec.AfterStart == nil {
		t.Fatal("spec.AfterStart = nil, want activation hook")
	}
}
