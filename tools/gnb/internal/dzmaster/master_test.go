package dzmaster

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"sync"
	"testing"
	"time"
)

type fakeProcess struct{ done chan struct{} }

func (p *fakeProcess) Wait() error { <-p.done; return nil }
func (p *fakeProcess) Kill() error {
	select {
	case <-p.done:
	default:
		close(p.done)
	}
	return nil
}

type fakeLauncher struct {
	mu       sync.Mutex
	launched []*Server
	args     [][]string
}

func (l *fakeLauncher) Launch(server *Server, args []string, _ string) (Process, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.launched = append(l.launched, server)
	l.args = append(l.args, args)
	return &fakeProcess{done: make(chan struct{})}, nil
}

func newTestMaster(t *testing.T) (*Master, *httptest.Server, *fakeLauncher) {
	t.Helper()
	launcher := &fakeLauncher{}
	m, err := New(Config{
		DataDir: t.TempDir(), Launch: launcher, MaxPlayers: 2, IdleSeconds: 1,
		Maps: []MapInfo{{ID: "loket", Name: "LOKET", Scene: "loket.scad"}, {ID: "range", Scene: "range.scad", Training: true}},
		Logf: t.Logf,
	})
	if err != nil {
		t.Fatal(err)
	}
	server := httptest.NewServer(m.Handler())
	t.Cleanup(server.Close)
	return m, server, launcher
}

func call(t *testing.T, server *httptest.Server, path, token string, body any, out any) int {
	t.Helper()
	data, _ := json.Marshal(body)
	req, _ := http.NewRequest(http.MethodPost, server.URL+path, bytes.NewReader(data))
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if out != nil {
		_ = json.NewDecoder(resp.Body).Decode(out)
	}
	return resp.StatusCode
}

func dsKey(m *Master, id string) string {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.servers[id].key
}

func TestPublicMasterCallbacksAndAdminBoundary(t *testing.T) {
	m, _, _ := newTestMaster(t)
	for _, tc := range []struct{ listen, want string }{
		{"0.0.0.0:7780", "http://127.0.0.1:7780"}, {":7780", "http://127.0.0.1:7780"},
		{"[::]:7780", "http://[::1]:7780"}, {"127.0.0.1:7780", "http://127.0.0.1:7780"},
	} {
		m.cfg.Listen = tc.listen
		if got := m.URL(); got != tc.want {
			t.Fatalf("callback %s: got %s", tc.listen, got)
		}
	}
	for _, address := range []string{"203.0.113.8:55000", "[2001:db8::1]:55000"} {
		req := httptest.NewRequest(http.MethodPost, "/admin/status", bytes.NewReader([]byte("{}")))
		req.RemoteAddr = address
		req.Header.Set("X-Forwarded-For", "127.0.0.1")
		out := httptest.NewRecorder()
		m.Handler().ServeHTTP(out, req)
		if out.Code != http.StatusForbidden {
			t.Fatalf("remote admin accepted from %s", address)
		}
	}
}

func TestMasterBoundsDSFleetAndReusesFreedPort(t *testing.T) {
	m, _, _ := newTestMaster(t)
	m.cfg.MaxServers = 1
	m.mu.Lock()
	defer m.mu.Unlock()
	first, err := m.launchLocked(m.cfg.Maps[0])
	if err != nil {
		t.Fatal(err)
	}
	if _, err := m.launchLocked(m.cfg.Maps[1]); err == nil {
		t.Fatal("exceeded server capacity")
	}
	first.State = stateDead
	_ = first.proc.Kill()
	second, err := m.launchLocked(m.cfg.Maps[1])
	if err != nil {
		t.Fatal(err)
	}
	if second.Port != first.Port {
		t.Fatal("did not reuse released port")
	}
	second.State = stateDead
	_ = second.proc.Kill()
}

func TestMasterSortieLifecycle(t *testing.T) {
	m, server, launcher := newTestMaster(t)

	var login struct {
		PlayerID uint64 `json:"playerId"`
		Token    string `json:"token"`
	}
	if call(t, server, "/api/login", "", map[string]string{"name": "alpha"}, &login) != 200 || login.Token == "" {
		t.Fatal("login failed")
	}
	// Logging in again with the same name is the same account.
	var again struct{ Token string }
	call(t, server, "/api/login", "", map[string]string{"name": "alpha"}, &again)
	if again.Token != login.Token {
		t.Fatal("second login made a new account")
	}
	// Give the account something in storage.
	_ = m.store.Update(func(st *State) error {
		st.Accounts[login.PlayerID].Storage = []Stack{{ID: "ak", Count: 1, Loaded: 30}, {ID: "bandage", Count: 5}}
		return nil
	})

	// Deploy: a DS starts for the map, the brought gear leaves storage.
	var deploy struct {
		State, Server, Ticket, Map string
		Port                       int
	}
	status := call(t, server, "/api/deploy", login.Token,
		map[string]any{"map": "loket", "bring": []Bring{{Index: 0, Count: 1}, {Index: 1, Count: 2}}}, &deploy)
	if status != 200 || deploy.State != "starting" || deploy.Ticket == "" || deploy.Map != "loket" {
		t.Fatalf("deploy: %d %+v", status, deploy)
	}
	if len(launcher.launched) != 1 {
		t.Fatalf("expected one DS, got %d", len(launcher.launched))
	}
	// Polling while the server starts (MaxPlayers is 2 here) never starts another one.
	for poll := 0; poll < 5; poll++ {
		call(t, server, "/api/deploy", login.Token, map[string]any{}, &deploy)
	}
	if len(launcher.launched) != 1 {
		t.Fatalf("polling started %d servers", len(launcher.launched))
	}
	var profile struct {
		Storage   []Stack
		Character *Character
	}
	call(t, server, "/api/profile", login.Token, nil, &profile)
	if len(profile.Storage) != 1 || profile.Storage[0].Count != 3 || profile.Character == nil ||
		len(profile.Character.Inventory) != 2 || !profile.Character.Fresh {
		t.Fatalf("profile after deploy: %+v", profile)
	}

	// The DS comes up and redeems the ticket (once).
	key := dsKey(m, deploy.Server)
	ds := map[string]any{"id": deploy.Server, "key": key}
	if call(t, server, "/ds/register", "", map[string]any{"id": deploy.Server, "key": key, "port": deploy.Port}, nil) != 200 {
		t.Fatal("register refused")
	}
	if call(t, server, "/ds/register", "", map[string]any{"id": deploy.Server, "key": "wrong"}, nil) != 403 {
		t.Fatal("a wrong key was accepted")
	}
	call(t, server, "/ds/heartbeat", "", map[string]any{"id": deploy.Server, "key": key, "ready": true}, nil)
	var redeem struct {
		PlayerID  uint64 `json:"playerId"`
		Sortie    uint64
		Character Character
	}
	if call(t, server, "/ds/redeem", "", map[string]any{"id": deploy.Server, "key": key, "ticket": deploy.Ticket}, &redeem) != 200 ||
		redeem.PlayerID != login.PlayerID || len(redeem.Character.Inventory) != 2 {
		t.Fatalf("redeem: %+v", redeem)
	}
	if call(t, server, "/ds/redeem", "", map[string]any{"id": deploy.Server, "key": key, "ticket": deploy.Ticket}, nil) != 403 {
		t.Fatal("a ticket was redeemed twice")
	}
	// While in the field a second deploy is refused.
	if call(t, server, "/api/deploy", login.Token, map[string]any{}, nil) != 409 {
		t.Fatal("deployed twice at once")
	}

	// A checkpoint moves the survivor; a stale one is ignored.
	checkpoint := func(seq uint64, outcome string, inventory []Stack) int {
		return call(t, server, "/ds/checkpoint", "", map[string]any{
			"id": deploy.Server, "key": key, "seq": seq, "world": map[string]any{"loot": seq},
			"characters": []checkpointCharacter{{PlayerID: login.PlayerID, Sortie: redeem.Sortie, Outcome: outcome,
				Position: [3]float64{10, 0, 5}, Inventory: inventory}},
		}, nil)
	}
	if checkpoint(1, outcomeField, []Stack{{ID: "ak", Count: 1, Loaded: 12}, {ID: "food_can", Count: 1}}) != 200 {
		t.Fatal("checkpoint refused")
	}
	if checkpoint(1, outcomeField, nil) != 409 {
		t.Fatal("a stale checkpoint was accepted")
	}
	m.store.View(func(st *State) {
		c := st.Accounts[login.PlayerID].Character
		if c == nil || c.Fresh || c.Position[0] != 10 || len(c.Inventory) != 2 || st.Worlds["loket"] == nil {
			t.Fatalf("after checkpoint: %+v", c)
		}
	})
	_ = ds

	// Extraction: the pack goes into storage, the sortie ends, the lease is released.
	if checkpoint(2, outcomeExtracted, []Stack{{ID: "food_can", Count: 1}, {ID: "bandage", Count: 1}}) != 200 {
		t.Fatal("extraction checkpoint refused")
	}
	call(t, server, "/api/profile", login.Token, nil, &profile)
	if profile.Character != nil || len(profile.Storage) != 2 || profile.Storage[0].Count != 4 {
		t.Fatalf("profile after extraction: %+v", profile)
	}
	// Free to deploy again; the existing DS has room.
	if call(t, server, "/api/deploy", login.Token, map[string]any{"map": "loket"}, &deploy) != 200 || len(launcher.launched) != 1 {
		t.Fatalf("redeploy: %+v (launched %d)", deploy, len(launcher.launched))
	}
}

func TestMasterDeathAndCrashRollBack(t *testing.T) {
	m, server, _ := newTestMaster(t)
	var login struct {
		PlayerID uint64 `json:"playerId"`
		Token    string `json:"token"`
	}
	call(t, server, "/api/login", "", map[string]string{"name": "bravo"}, &login)
	var deploy struct{ Server, Ticket string }
	call(t, server, "/api/deploy", login.Token, map[string]any{"map": "loket"}, &deploy)
	key := dsKey(m, deploy.Server)
	call(t, server, "/ds/heartbeat", "", map[string]any{"id": deploy.Server, "key": key, "ready": true}, nil)
	var redeem struct{ Sortie uint64 }
	call(t, server, "/ds/redeem", "", map[string]any{"id": deploy.Server, "key": key, "ticket": deploy.Ticket}, &redeem)
	call(t, server, "/ds/checkpoint", "", map[string]any{"id": deploy.Server, "key": key, "seq": 1,
		"characters": []checkpointCharacter{{PlayerID: login.PlayerID, Sortie: redeem.Sortie, Outcome: outcomeField,
			Inventory: []Stack{{ID: "medkit", Count: 1}}}}}, nil)

	// The DS dies: its lease goes, the survivor keeps the last checkpoint and can resume.
	m.mu.Lock()
	m.markDeadLocked(m.servers[deploy.Server], "test crash")
	m.mu.Unlock()
	var resume struct {
		Resume bool
		Server string
	}
	if call(t, server, "/api/deploy", login.Token, map[string]any{}, &resume) != 200 || !resume.Resume ||
		resume.Server == deploy.Server {
		t.Fatalf("resume after crash: %+v", resume)
	}
	m.store.View(func(st *State) {
		if inv := st.Accounts[login.PlayerID].Character.Inventory; len(inv) != 1 || inv[0].ID != "medkit" {
			t.Fatalf("inventory after crash: %+v", inv)
		}
	})
	// Death ends the sortie with nothing returned to storage.
	key = dsKey(m, resume.Server)
	call(t, server, "/ds/heartbeat", "", map[string]any{"id": resume.Server, "key": key, "ready": true}, nil)
	call(t, server, "/ds/checkpoint", "", map[string]any{"id": resume.Server, "key": key, "seq": 1,
		"characters": []checkpointCharacter{{PlayerID: login.PlayerID, Sortie: redeem.Sortie, Outcome: outcomeDead}}}, nil)
	m.store.View(func(st *State) {
		a := st.Accounts[login.PlayerID]
		if a.Character != nil || a.Deaths != 1 || len(a.Storage) != 0 {
			t.Fatalf("after death: %+v", a)
		}
	})
}

func TestMasterStopsIdleServersAndSurvivesRestart(t *testing.T) {
	m, server, _ := newTestMaster(t)
	var login struct{ Token string }
	call(t, server, "/api/login", "", map[string]string{"name": "carol"}, &login)
	var deploy struct{ Server, Ticket string }
	call(t, server, "/api/deploy", login.Token, map[string]any{}, &deploy)
	key := dsKey(m, deploy.Server)
	// Ready and empty, with the ticket expired: idle.
	m.mu.Lock()
	m.tickets = map[string]*ticket{}
	m.mu.Unlock()
	call(t, server, "/ds/heartbeat", "", map[string]any{"id": deploy.Server, "key": key, "ready": true}, nil)
	m.tick(time.Now().Add(2 * time.Second))
	var beat struct{ Stop bool }
	call(t, server, "/ds/heartbeat", "", map[string]any{"id": deploy.Server, "key": key, "ready": true}, &beat)
	if !beat.Stop {
		t.Fatal("an idle server was not asked to stop")
	}
	// The store reopens with the account and its sortie.
	reopened, err := OpenStore(filepath.Join(m.cfg.DataDir, "master.json"))
	if err != nil {
		t.Fatal(err)
	}
	reopened.View(func(st *State) {
		a := st.AccountByName("carol")
		if a == nil || a.Character == nil {
			t.Fatal("account or sortie lost on reopen")
		}
	})
}

func TestStorageTakeIsAllOrNothing(t *testing.T) {
	storage := []Stack{{ID: "bandage", Count: 2}, {ID: "ak", Count: 1, Loaded: 30}}
	if _, _, err := TakeFromStorage(storage, []Bring{{Index: 0, Count: 3}}); err == nil {
		t.Fatal("took more than a stack holds")
	}
	if _, _, err := TakeFromStorage(storage, []Bring{{Index: 0, Count: 1}, {Index: 9, Count: 1}}); err == nil {
		t.Fatal("took from a missing stack")
	}
	remaining, taken, err := TakeFromStorage(storage, []Bring{{Index: 0, Count: 1}, {Index: 1, Count: 1}})
	if err != nil || len(taken) != 2 || len(remaining) != 1 || remaining[0].Count != 1 {
		t.Fatalf("take: %v %+v %+v", err, taken, remaining)
	}
	merged := AddToStorage(remaining, []Stack{{ID: "bandage", Count: 4}, {ID: "ak", Count: 1, Loaded: 5}})
	if len(merged) != 2 || merged[0].Count != 5 {
		t.Fatalf("merge: %+v", merged)
	}
}
