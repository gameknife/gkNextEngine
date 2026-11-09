package dzmaster

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"math/big"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

// MapInfo is one entry of assets/configs/nextdayz_maps.json.
type MapInfo struct {
	ID       string `json:"id"`
	Name     string `json:"name"`
	Scene    string `json:"scene"`
	Training bool   `json:"training"`
}

// LoadMaps reads the published maps of the NextDayz map registry.
func LoadMaps(path string) ([]MapInfo, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var registry struct {
		Maps []struct {
			ID     string `json:"id"`
			Name   string `json:"name"`
			Scene  string `json:"scene"`
			Status string `json:"status"`
			Kind   string `json:"kind"`
		} `json:"maps"`
	}
	if err := json.Unmarshal(data, &registry); err != nil {
		return nil, err
	}
	var maps []MapInfo
	for _, m := range registry.Maps {
		if m.Status == "published" && m.Scene != "" {
			maps = append(maps, MapInfo{ID: m.ID, Name: m.Name, Scene: m.Scene, Training: m.Kind == "training"})
		}
	}
	if len(maps) == 0 {
		return nil, fmt.Errorf("%s lists no published maps", path)
	}
	return maps, nil
}

// Process is a running DS; Launcher starts them (the real one runs NextDayz).
type Process interface {
	Wait() error
	Kill() error
}

type Launcher interface {
	Launch(server *Server, args []string, logPath string) (Process, error)
}

type Config struct {
	Listen      string // address the Master serves on, e.g. 127.0.0.1:7780
	PublicHost  string // host clients are told to connect their DS to
	DataDir     string // master.json, DS logs
	Exe         string // the NextDayz executable a DS runs
	Maps        []MapInfo
	PortBase    int      // first UDP port handed to a DS
	MaxPlayers  int      // survivors per DS
	IdleSeconds int      // an empty DS is stopped after this long
	Pvp         bool     // friendly fire on new servers
	DSArgs      []string // extra arguments for every DS
	Launch      Launcher // nil: run Exe
	Logf        func(format string, args ...any)
}

const (
	ticketLifetime   = 2 * time.Minute
	heartbeatTimeout = 15 * time.Second
	startTimeout     = 5 * time.Minute
	stopGrace        = 30 * time.Second
	stateStarting    = "starting"
	stateReady       = "ready"
	stateStopping    = "stopping"
	stateDead        = "dead"
	outcomeField     = "field"
	outcomeLeft      = "left"
	outcomeDead      = "dead"
	outcomeExtracted = "extracted"
)

// Server is one DS as the Master tracks it (not persisted: a restarted Master
// starts its fleet afresh; survivors resume from their last checkpoint).
type Server struct {
	ID         string    `json:"id"`
	Map        string    `json:"map"`
	Scene      string    `json:"scene"`
	Port       int       `json:"port"`
	State      string    `json:"state"`
	Players    []uint64  `json:"players"`
	Pvp        bool      `json:"pvp"`
	StartedAt  time.Time `json:"startedAt"`
	LastBeat   time.Time `json:"lastBeat"`
	EmptySince time.Time `json:"emptySince"`
	key        string
	lastSeq    uint64
	kick       []uint64
	stopAt     time.Time
	proc       Process
}

type ticket struct {
	player  uint64
	server  string
	sortie  uint64
	expires time.Time
}

type Master struct {
	cfg     Config
	store   *Store
	mu      sync.Mutex
	servers map[string]*Server
	leases  map[uint64]string // survivor -> DS holding them
	tickets map[string]*ticket
	nextDS  int
	http    *http.Server
}

func New(cfg Config) (*Master, error) {
	if cfg.Listen == "" {
		cfg.Listen = "127.0.0.1:7780"
	}
	if cfg.PublicHost == "" {
		cfg.PublicHost = "127.0.0.1"
	}
	if cfg.PortBase == 0 {
		cfg.PortBase = 27100
	}
	if cfg.MaxPlayers == 0 {
		cfg.MaxPlayers = 16
	}
	if cfg.IdleSeconds == 0 {
		cfg.IdleSeconds = 300
	}
	if cfg.Logf == nil {
		cfg.Logf = log.Printf
	}
	if cfg.Launch == nil {
		cfg.Launch = execLauncher{exe: cfg.Exe}
	}
	if len(cfg.Maps) == 0 {
		return nil, errors.New("no maps")
	}
	store, err := OpenStore(filepath.Join(cfg.DataDir, "master.json"))
	if err != nil {
		return nil, err
	}
	return &Master{cfg: cfg, store: store, servers: map[string]*Server{}, leases: map[uint64]string{},
		tickets: map[string]*ticket{}, nextDS: 1}, nil
}

func (m *Master) URL() string { return "http://" + m.cfg.Listen }

// Handler serves the client API (/api), the DS API (/ds) and admin (/admin).
func (m *Master) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/api/login", m.handleLogin)
	mux.HandleFunc("/api/profile", m.handleProfile)
	mux.HandleFunc("/api/deploy", m.handleDeploy)
	mux.HandleFunc("/ds/register", m.handleRegister)
	mux.HandleFunc("/ds/heartbeat", m.handleHeartbeat)
	mux.HandleFunc("/ds/world", m.handleWorld)
	mux.HandleFunc("/ds/redeem", m.handleRedeem)
	mux.HandleFunc("/ds/checkpoint", m.handleCheckpoint)
	mux.HandleFunc("/admin/status", m.handleStatus)
	mux.HandleFunc("/admin/pvp", m.handleAdminPvp)
	mux.HandleFunc("/admin/kick", m.handleAdminKick)
	mux.HandleFunc("/admin/stop", m.handleAdminStop)
	return mux
}

// Run serves until ctx ends, then stops every DS (they checkpoint first).
func (m *Master) Run(ctx context.Context) error {
	listener, err := net.Listen("tcp", m.cfg.Listen)
	if err != nil {
		return err
	}
	m.http = &http.Server{Handler: m.Handler(), ReadHeaderTimeout: 10 * time.Second}
	go func() { _ = m.http.Serve(listener) }()
	m.cfg.Logf("master listening on %s (%d maps, data %s)", m.URL(), len(m.cfg.Maps), m.cfg.DataDir)
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			m.shutdown()
			shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			return m.http.Shutdown(shutdownCtx)
		case now := <-ticker.C:
			m.tick(now)
		}
	}
}

// ---------------------------------------------------------------- client API

func (m *Master) handleLogin(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Name string `json:"name"`
	}
	if !decode(w, r, &req) {
		return
	}
	name := strings.TrimSpace(req.Name)
	if name == "" || len(name) > 32 {
		httpError(w, http.StatusBadRequest, "a name of 1-32 characters is needed")
		return
	}
	var account Account
	err := m.store.Update(func(st *State) error {
		a := st.AccountByName(name)
		if a == nil {
			a = &Account{ID: st.NextID, Name: name, Token: randomHex(16)}
			st.NextID++
			st.Accounts[a.ID] = a
			m.cfg.Logf("account %d %q created", a.ID, name)
		}
		account = *a
		return nil
	})
	if err != nil {
		httpError(w, http.StatusInternalServerError, err.Error())
		return
	}
	reply(w, map[string]any{"playerId": account.ID, "token": account.Token})
}

func (m *Master) account(r *http.Request) *Account {
	token := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
	var found *Account
	m.store.View(func(st *State) {
		if a := st.AccountByToken(token); a != nil {
			snapshot := *a
			found = &snapshot
		}
	})
	return found
}

func (m *Master) handleProfile(w http.ResponseWriter, r *http.Request) {
	a := m.account(r)
	if a == nil {
		httpError(w, http.StatusUnauthorized, "log in first")
		return
	}
	m.mu.Lock()
	playing := ""
	if id, ok := m.leases[a.ID]; ok {
		if s := m.servers[id]; s != nil && s.State != stateDead {
			playing = id
		}
	}
	m.mu.Unlock()
	reply(w, map[string]any{
		"playerId": a.ID, "name": a.Name, "gold": a.Gold, "storage": nonNil(a.Storage),
		"character": a.Character, "sorties": a.Sorties, "extractions": a.Extractions, "deaths": a.Deaths,
		"lastOutcome": a.LastOutcome, "maps": m.cfg.Maps, "playing": playing,
	})
}

func (m *Master) handleDeploy(w http.ResponseWriter, r *http.Request) {
	a := m.account(r)
	if a == nil {
		httpError(w, http.StatusUnauthorized, "log in first")
		return
	}
	var req struct {
		Map   string  `json:"map"`
		Bring []Bring `json:"bring"`
	}
	if !decode(w, r, &req) {
		return
	}
	m.mu.Lock()
	if id, ok := m.leases[a.ID]; ok {
		if s := m.servers[id]; s != nil && s.State != stateDead {
			m.mu.Unlock()
			httpError(w, http.StatusConflict, "already in the field on "+id)
			return
		}
		delete(m.leases, a.ID)
	}
	m.mu.Unlock()

	// A new sortie takes its gear out of storage; a survivor still in the field resumes.
	var character Character
	err := m.store.Update(func(st *State) error {
		acct := st.Accounts[a.ID]
		if acct.Character == nil {
			mapInfo, err := m.pickMap(req.Map)
			if err != nil {
				return err
			}
			remaining, taken, err := TakeFromStorage(acct.Storage, req.Bring)
			if err != nil {
				return err
			}
			acct.Storage = remaining
			acct.Character = &Character{Map: mapInfo.ID, Inventory: nonNil(taken), Sortie: st.NextSortie, Fresh: true}
			st.NextSortie++
			acct.Sorties++
			m.cfg.Logf("survivor %d %q sortie %d to %s with %d stacks", acct.ID, acct.Name,
				acct.Character.Sortie, mapInfo.ID, len(taken))
		}
		character = *acct.Character
		return nil
	})
	if err != nil {
		httpError(w, http.StatusBadRequest, err.Error())
		return
	}
	mapInfo, _ := m.findMap(character.Map)
	m.mu.Lock()
	defer m.mu.Unlock()
	server, err := m.serverForLocked(mapInfo)
	if err != nil {
		httpError(w, http.StatusServiceUnavailable, err.Error())
		return
	}
	// One ticket per survivor: asking again (while a server starts) replaces it,
	// so a waiting client never counts as more than one place.
	for old, t := range m.tickets {
		if t.player == a.ID {
			delete(m.tickets, old)
		}
	}
	token := randomHex(16)
	m.tickets[token] = &ticket{player: a.ID, server: server.ID, sortie: character.Sortie,
		expires: time.Now().Add(ticketLifetime)}
	state := stateStarting
	if server.State == stateReady {
		state = stateReady
	}
	reply(w, map[string]any{"state": state, "server": server.ID, "host": m.cfg.PublicHost, "port": server.Port,
		"ticket": token, "map": mapInfo.ID, "scene": mapInfo.Scene, "resume": !character.Fresh})
}

func (m *Master) pickMap(requested string) (MapInfo, error) {
	if requested != "" {
		mapInfo, ok := m.findMap(requested)
		if !ok {
			return MapInfo{}, fmt.Errorf("no published map %q", requested)
		}
		return mapInfo, nil
	}
	var candidates []MapInfo
	for _, mapInfo := range m.cfg.Maps {
		if !mapInfo.Training {
			candidates = append(candidates, mapInfo)
		}
	}
	if len(candidates) == 0 {
		candidates = m.cfg.Maps
	}
	pick, _ := rand.Int(rand.Reader, big.NewInt(int64(len(candidates))))
	return candidates[pick.Int64()], nil
}

func (m *Master) findMap(id string) (MapInfo, bool) {
	for _, mapInfo := range m.cfg.Maps {
		if mapInfo.ID == id {
			return mapInfo, true
		}
	}
	return MapInfo{}, false
}

// serverForLocked returns a live DS of the map with room, starting one if needed.
func (m *Master) serverForLocked(mapInfo MapInfo) (*Server, error) {
	var best *Server
	for _, s := range m.servers {
		if s.Map != mapInfo.ID || (s.State != stateReady && s.State != stateStarting) {
			continue
		}
		if len(s.Players)+m.pendingLocked(s.ID) >= m.cfg.MaxPlayers {
			continue
		}
		if best == nil || s.ID < best.ID {
			best = s
		}
	}
	if best != nil {
		return best, nil
	}
	return m.launchLocked(mapInfo)
}

func (m *Master) pendingLocked(serverID string) int {
	count := 0
	now := time.Now()
	for _, t := range m.tickets {
		if t.server == serverID && now.Before(t.expires) {
			count++
		}
	}
	return count
}

func (m *Master) launchLocked(mapInfo MapInfo) (*Server, error) {
	port := m.cfg.PortBase
	for {
		taken := false
		for _, s := range m.servers {
			if s.State != stateDead && s.Port == port {
				taken = true
			}
		}
		if !taken {
			break
		}
		port++
	}
	server := &Server{ID: fmt.Sprintf("ds%d", m.nextDS), Map: mapInfo.ID, Scene: mapInfo.Scene, Port: port,
		State: stateStarting, Pvp: m.cfg.Pvp, StartedAt: time.Now(), key: randomHex(16)}
	m.nextDS++
	args := append([]string{
		"--dedicated", "--disable-streamline", "--width=320", "--height=180",
		"--cvar", "r.rendererType 4", "--cvar", "r.upscaler.type 0",
		"--load-scene=" + mapInfo.Scene,
		"--cvar", "nextdayz.net.mode server",
		"--cvar", fmt.Sprintf("nextdayz.net.port %d", port),
		"--cvar", fmt.Sprintf("nextdayz.net.pvp %t", server.Pvp),
		"--cvar", fmt.Sprintf("nextdayz.net.maxPlayers %d", m.cfg.MaxPlayers),
		"--cvar", "nextdayz.net.master " + m.URL(),
		"--cvar", "nextdayz.net.dsId " + server.ID,
		"--cvar", "nextdayz.net.masterKey " + server.key,
	}, m.cfg.DSArgs...)
	logPath := filepath.Join(m.cfg.DataDir, "logs", server.ID+".log")
	proc, err := m.cfg.Launch.Launch(server, args, logPath)
	if err != nil {
		return nil, fmt.Errorf("start a server for %s: %w", mapInfo.ID, err)
	}
	server.proc = proc
	m.servers[server.ID] = server
	m.cfg.Logf("%s starting for %s on UDP %d (log %s)", server.ID, mapInfo.ID, port, logPath)
	go func(id string, p Process) {
		err := p.Wait()
		m.mu.Lock()
		defer m.mu.Unlock()
		if s := m.servers[id]; s != nil && s.State != stateDead {
			m.markDeadLocked(s, fmt.Sprintf("process exited (%v)", err))
		}
	}(server.ID, proc)
	return server, nil
}

// ---------------------------------------------------------------- DS API

type dsRequest struct {
	ID  string `json:"id"`
	Key string `json:"key"`
}

func (m *Master) dsLocked(req dsRequest) *Server {
	s := m.servers[req.ID]
	if s == nil || s.key == "" || s.key != req.Key || s.State == stateDead {
		return nil
	}
	return s
}

func (m *Master) handleRegister(w http.ResponseWriter, r *http.Request) {
	var req struct {
		dsRequest
		Port int    `json:"port"`
		Map  string `json:"map"`
	}
	if !decode(w, r, &req) {
		return
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	s := m.dsLocked(req.dsRequest)
	if s == nil {
		httpError(w, http.StatusForbidden, "unknown server")
		return
	}
	s.LastBeat = time.Now()
	reply(w, map[string]any{"pvp": s.Pvp})
}

func (m *Master) handleHeartbeat(w http.ResponseWriter, r *http.Request) {
	var req struct {
		dsRequest
		Players []uint64 `json:"players"`
		Ready   bool     `json:"ready"`
	}
	if !decode(w, r, &req) {
		return
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	s := m.dsLocked(req.dsRequest)
	if s == nil {
		httpError(w, http.StatusForbidden, "unknown server")
		return
	}
	now := time.Now()
	s.LastBeat = now
	s.Players = req.Players
	if req.Ready && s.State == stateStarting {
		s.State = stateReady
		m.cfg.Logf("%s ready (%s, UDP %d)", s.ID, s.Map, s.Port)
	}
	if len(s.Players) > 0 || m.pendingLocked(s.ID) > 0 {
		s.EmptySince = time.Time{}
	} else if s.EmptySince.IsZero() {
		s.EmptySince = now
	}
	kick := s.kick
	s.kick = nil
	reply(w, map[string]any{"stop": s.State == stateStopping, "pvp": s.Pvp, "kick": nonNil(kick)})
}

func (m *Master) handleWorld(w http.ResponseWriter, r *http.Request) {
	var req dsRequest
	if !decode(w, r, &req) {
		return
	}
	m.mu.Lock()
	s := m.dsLocked(req)
	m.mu.Unlock()
	if s == nil {
		httpError(w, http.StatusForbidden, "unknown server")
		return
	}
	var world World
	m.store.View(func(st *State) {
		if saved := st.Worlds[s.Map]; saved != nil {
			world = *saved
		}
	})
	reply(w, map[string]any{"seq": world.Seq, "blob": world.Blob})
}

func (m *Master) handleRedeem(w http.ResponseWriter, r *http.Request) {
	var req struct {
		dsRequest
		Ticket string `json:"ticket"`
	}
	if !decode(w, r, &req) {
		return
	}
	m.mu.Lock()
	s := m.dsLocked(req.dsRequest)
	t := m.tickets[req.Ticket]
	if s == nil || t == nil || t.server != s.ID || time.Now().After(t.expires) {
		m.mu.Unlock()
		httpError(w, http.StatusForbidden, "no such ticket for this server")
		return
	}
	delete(m.tickets, req.Ticket)
	m.leases[t.player] = s.ID
	m.mu.Unlock()
	var a Account
	m.store.View(func(st *State) {
		if acct := st.Accounts[t.player]; acct != nil {
			a = *acct
		}
	})
	if a.Character == nil || a.Character.Sortie != t.sortie {
		httpError(w, http.StatusConflict, "that sortie is over")
		return
	}
	m.cfg.Logf("survivor %d %q joined %s (sortie %d)", a.ID, a.Name, s.ID, t.sortie)
	reply(w, map[string]any{"playerId": a.ID, "name": a.Name, "sortie": t.sortie, "character": a.Character})
}

type checkpointCharacter struct {
	PlayerID  uint64          `json:"playerId"`
	Sortie    uint64          `json:"sortie"`
	Outcome   string          `json:"outcome"`
	Position  [3]float64      `json:"position"`
	Yaw       float64         `json:"yaw"`
	Survival  json.RawMessage `json:"survival"`
	Inventory []Stack         `json:"inventory"`
}

// handleCheckpoint commits a DS's world and every survivor on it in one write:
// a crash rolls both back to the same moment, so nothing is ever both on the
// ground and in a pack.
func (m *Master) handleCheckpoint(w http.ResponseWriter, r *http.Request) {
	var req struct {
		dsRequest
		Seq        uint64                `json:"seq"`
		World      json.RawMessage       `json:"world"`
		Characters []checkpointCharacter `json:"characters"`
	}
	if !decode(w, r, &req) {
		return
	}
	m.mu.Lock()
	s := m.dsLocked(req.dsRequest)
	if s == nil || req.Seq <= s.lastSeq {
		m.mu.Unlock()
		httpError(w, http.StatusConflict, "unknown server or stale checkpoint")
		return
	}
	s.lastSeq = req.Seq
	mapID := s.Map
	serverID := s.ID
	m.mu.Unlock()

	var released []uint64
	err := m.store.Update(func(st *State) error {
		if len(req.World) > 0 && string(req.World) != "null" {
			world := st.Worlds[mapID]
			if world == nil {
				world = &World{}
				st.Worlds[mapID] = world
			}
			world.Seq++
			world.Blob = req.World
		}
		for _, c := range req.Characters {
			acct := st.Accounts[c.PlayerID]
			if acct == nil || acct.Character == nil || acct.Character.Sortie != c.Sortie {
				continue // a sortie that already ended elsewhere
			}
			switch c.Outcome {
			case outcomeField, outcomeLeft:
				acct.Character.Position = c.Position
				acct.Character.Yaw = c.Yaw
				acct.Character.Survival = c.Survival
				acct.Character.Inventory = nonNil(c.Inventory)
				acct.Character.Fresh = false
				if c.Outcome == outcomeLeft {
					released = append(released, c.PlayerID)
				}
			case outcomeDead:
				acct.Character = nil
				acct.Deaths++
				acct.LastOutcome = "died on " + mapID
				released = append(released, c.PlayerID)
			case outcomeExtracted:
				acct.Storage = AddToStorage(acct.Storage, c.Inventory)
				acct.Character = nil
				acct.Extractions++
				acct.LastOutcome = "extracted from " + mapID
				released = append(released, c.PlayerID)
			}
		}
		return nil
	})
	if err != nil {
		httpError(w, http.StatusInternalServerError, err.Error())
		return
	}
	m.mu.Lock()
	for _, id := range released {
		if m.leases[id] == serverID {
			delete(m.leases, id)
		}
	}
	m.mu.Unlock()
	reply(w, map[string]any{"ok": true})
}

// ---------------------------------------------------------------- admin

func (m *Master) handleStatus(w http.ResponseWriter, r *http.Request) {
	m.mu.Lock()
	servers := make([]Server, 0, len(m.servers))
	for _, s := range m.servers {
		servers = append(servers, *s)
	}
	leases := map[string]string{}
	for player, server := range m.leases {
		leases[fmt.Sprint(player)] = server
	}
	m.mu.Unlock()
	sort.Slice(servers, func(i, j int) bool { return servers[i].ID < servers[j].ID })
	type accountRow struct {
		ID          uint64 `json:"id"`
		Name        string `json:"name"`
		InField     string `json:"inField"`
		Storage     int    `json:"storageStacks"`
		Sorties     uint64 `json:"sorties"`
		Extractions uint64 `json:"extractions"`
		Deaths      uint64 `json:"deaths"`
	}
	var accounts []accountRow
	m.store.View(func(st *State) {
		for _, a := range st.Accounts {
			row := accountRow{ID: a.ID, Name: a.Name, Storage: len(a.Storage), Sorties: a.Sorties,
				Extractions: a.Extractions, Deaths: a.Deaths}
			if a.Character != nil {
				row.InField = a.Character.Map
			}
			accounts = append(accounts, row)
		}
	})
	sort.Slice(accounts, func(i, j int) bool { return accounts[i].ID < accounts[j].ID })
	reply(w, map[string]any{"servers": servers, "accounts": accounts, "leases": leases})
}

func (m *Master) handleAdminPvp(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Server string `json:"server"`
		On     bool   `json:"on"`
	}
	if !decode(w, r, &req) {
		return
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	s := m.servers[req.Server]
	if s == nil {
		httpError(w, http.StatusNotFound, "no such server")
		return
	}
	s.Pvp = req.On
	reply(w, map[string]any{"ok": true})
}

func (m *Master) handleAdminKick(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Player uint64 `json:"player"`
	}
	if !decode(w, r, &req) {
		return
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	s := m.servers[m.leases[req.Player]]
	if s == nil {
		httpError(w, http.StatusNotFound, "that survivor is not on a server")
		return
	}
	s.kick = append(s.kick, req.Player)
	reply(w, map[string]any{"ok": true, "server": s.ID})
}

func (m *Master) handleAdminStop(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Server string `json:"server"`
	}
	if !decode(w, r, &req) {
		return
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	s := m.servers[req.Server]
	if s == nil || s.State == stateDead {
		httpError(w, http.StatusNotFound, "no such server")
		return
	}
	m.stopLocked(s, "stopped by admin")
	reply(w, map[string]any{"ok": true})
}

// ---------------------------------------------------------------- fleet upkeep

func (m *Master) tick(now time.Time) {
	m.mu.Lock()
	defer m.mu.Unlock()
	for token, t := range m.tickets {
		if now.After(t.expires) {
			delete(m.tickets, token)
		}
	}
	for _, s := range m.servers {
		switch s.State {
		case stateStarting:
			if now.Sub(s.StartedAt) > startTimeout {
				m.markDeadLocked(s, "never became ready")
			}
		case stateReady:
			if !s.LastBeat.IsZero() && now.Sub(s.LastBeat) > heartbeatTimeout {
				m.markDeadLocked(s, "heartbeat lost")
			} else if !s.EmptySince.IsZero() && now.Sub(s.EmptySince) > time.Duration(m.cfg.IdleSeconds)*time.Second {
				m.stopLocked(s, "idle")
			}
		case stateStopping:
			if now.After(s.stopAt) {
				m.markDeadLocked(s, "did not stop in time")
			}
		}
	}
}

func (m *Master) stopLocked(s *Server, why string) {
	if s.State == stateStopping || s.State == stateDead {
		return
	}
	s.State = stateStopping
	s.stopAt = time.Now().Add(stopGrace)
	m.cfg.Logf("%s stopping: %s", s.ID, why)
}

// markDeadLocked forgets a DS. Survivors it held resume later from its last
// checkpoint, together with the world it last committed.
func (m *Master) markDeadLocked(s *Server, why string) {
	s.State = stateDead
	if s.proc != nil {
		_ = s.proc.Kill()
	}
	for player, server := range m.leases {
		if server == s.ID {
			delete(m.leases, player)
		}
	}
	for token, t := range m.tickets {
		if t.server == s.ID {
			delete(m.tickets, token)
		}
	}
	m.cfg.Logf("%s down: %s", s.ID, why)
}

func (m *Master) shutdown() {
	m.mu.Lock()
	for _, s := range m.servers {
		m.stopLocked(s, "master shutting down")
	}
	m.mu.Unlock()
	deadline := time.Now().Add(stopGrace)
	for time.Now().Before(deadline) {
		m.mu.Lock()
		alive := 0
		for _, s := range m.servers {
			if s.State != stateDead {
				alive++
			}
		}
		m.mu.Unlock()
		if alive == 0 {
			return
		}
		time.Sleep(200 * time.Millisecond)
	}
	m.mu.Lock()
	for _, s := range m.servers {
		if s.State != stateDead {
			m.markDeadLocked(s, "killed at shutdown")
		}
	}
	m.mu.Unlock()
}

// Servers is a snapshot of the fleet (for tools and tests).
func (m *Master) Servers() []Server {
	m.mu.Lock()
	defer m.mu.Unlock()
	out := make([]Server, 0, len(m.servers))
	for _, s := range m.servers {
		out = append(out, *s)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

// ---------------------------------------------------------------- helpers

func decode(w http.ResponseWriter, r *http.Request, into any) bool {
	if r.Method != http.MethodPost && r.Method != http.MethodGet {
		httpError(w, http.StatusMethodNotAllowed, "use POST")
		return false
	}
	body, err := io.ReadAll(io.LimitReader(r.Body, 16<<20))
	if err != nil {
		httpError(w, http.StatusBadRequest, err.Error())
		return false
	}
	if len(body) == 0 {
		body = []byte("{}")
	}
	if err := json.Unmarshal(body, into); err != nil {
		httpError(w, http.StatusBadRequest, "bad JSON: "+err.Error())
		return false
	}
	return true
}

func reply(w http.ResponseWriter, value any) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(value)
}

func httpError(w http.ResponseWriter, status int, message string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(map[string]string{"error": message})
}

func randomHex(bytes int) string {
	buffer := make([]byte, bytes)
	_, _ = rand.Read(buffer)
	return hex.EncodeToString(buffer)
}

func nonNil[T any](items []T) []T {
	if items == nil {
		return []T{}
	}
	return items
}
