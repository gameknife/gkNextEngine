package dzmaster

import (
	"bytes"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestPasswordLoginAndLegacyEnrollment(t *testing.T) {
	m, legacy, _ := newTestMaster(t)
	var old, enrolled, session struct {
		PlayerID uint64 `json:"playerId"`
		Token    string `json:"token"`
	}
	name := map[string]string{"name": "legacy"}
	if status := call(t, legacy, "/api/login", "", name, &old); status != http.StatusOK {
		t.Fatal(status)
	}
	cfg := m.cfg
	cfg.RequirePassword = true
	protected, err := New(cfg)
	if err != nil {
		t.Fatal(err)
	}
	server := httptest.NewServer(protected.Handler())
	defer server.Close()
	status := func(password string, into any) int {
		return call(t, server, "/api/login", "", map[string]string{"name": "legacy", "password": password}, into)
	}
	if status("", nil) != http.StatusUnauthorized || status("new phrase", &enrolled) != http.StatusOK {
		t.Fatal("legacy accounts must enroll with a password")
	}
	if old.PlayerID != enrolled.PlayerID || old.Token == enrolled.Token {
		t.Fatal("enrollment must keep the survivor and revoke the old token")
	}
	if got := call(t, server, "/api/profile", old.Token, nil, nil); got != http.StatusUnauthorized {
		t.Fatal(got)
	}
	persisted, err := os.ReadFile(filepath.Join(cfg.DataDir, "master.json"))
	if err != nil || bytes.Contains(persisted, []byte("new phrase")) {
		t.Fatal("passwords must be stored only as hashes")
	}
	if status("new phrase", &session) != http.StatusOK || enrolled != session {
		t.Fatal("correct password must log in to the same survivor")
	}
	for i := 1; i < maxLoginFailures; i++ {
		if status("wrong", nil) != http.StatusUnauthorized {
			t.Fatalf("failed attempt %d", i+1)
		}
	}
	if status("new phrase", nil) != http.StatusTooManyRequests {
		t.Fatal("consecutive failures must be throttled")
	}
}

func TestWebReleaseRoutingAndVersionGate(t *testing.T) {
	existing, _, launcher := newTestMaster(t)
	cfg := existing.cfg
	cfg.RtcPortBase = 27200
	cfg.WebRoot = t.TempDir()
	build := strings.Repeat("a", 64)
	for name, content := range map[string]string{
		"release.json":              `{"build":"` + build + `","protocol":5,"assets":"/game-assets/manifest.json"}`,
		"site/index.html":           build,
		"ndcore/ndcore.mjs":         "module",
		"ndcore/ndcore.wasm":        "wasm",
		"game-assets/manifest.json": "{}",
		"game-assets/map.glb":       "raw",
		"game-assets/map.glb.gz":    "gzipped",
	} {
		path := filepath.Join(cfg.WebRoot, filepath.FromSlash(name))
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(content), 0644); err != nil {
			t.Fatal(err)
		}
	}
	m, err := New(cfg)
	if err != nil {
		t.Fatal(err)
	}
	server := httptest.NewServer(m.Handler())
	defer server.Close()
	for _, test := range []struct {
		path, encoding, wantType, wantBody string
		status                             int
	}{
		{"/", "", "text/html", build, 200},
		{"/api/web/manifest", "", "application/json", build, 200},
		{"/ndcore/ndcore.wasm", "", "application/wasm", "wasm", 200},
		{"/game-assets/map.glb", "gzip;q=0", "model/gltf-binary", "raw", 200},
		{"/game-assets/map.glb", "gzip", "model/gltf-binary", "gzipped", 200},
		{"/game-assets/map.glb.gz", "", "", "404", 404},
		{"/release.json", "", "", "404", 404},
	} {
		req, _ := http.NewRequest(http.MethodGet, server.URL+test.path, nil)
		// Explicit identity also suppresses Go's transparent gzip decoding.
		req.Header.Set("Accept-Encoding", "identity, "+test.encoding)
		response, err := server.Client().Do(req)
		if err != nil {
			t.Fatal(err)
		}
		body, err := io.ReadAll(response.Body)
		response.Body.Close()
		if err != nil || response.StatusCode != test.status || !strings.HasPrefix(response.Header.Get("Content-Type"), test.wantType) ||
			!bytes.Contains(body, []byte(test.wantBody)) {
			t.Fatalf("%s: %s %q %v", test.path, response.Status, body, err)
		}
		if test.encoding == "gzip" && response.Header.Get("Content-Encoding") != "gzip" {
			t.Fatal("expected the gzip variant")
		}
	}
	var user struct{ Token string }
	if got := call(t, server, "/api/login", "", map[string]string{"name": "web"}, &user); got != http.StatusOK {
		t.Fatal(got)
	}
	if got := call(t, server, "/api/deploy", user.Token, map[string]string{"map": "range", "webBuild": "previous"}, nil); got != http.StatusConflict {
		t.Fatal(got)
	}
	if len(launcher.launched) != 0 {
		t.Fatal("stale client must be rejected before dispatch")
	}
	if got := call(t, server, "/api/deploy", user.Token, map[string]string{"map": "range", "webBuild": build}, nil); got != http.StatusOK {
		t.Fatal(got)
	}
	// Fake servers cannot checkpoint on shutdown. Terminate before cleanup.
	launcher.launched[0].proc.Kill()
}
