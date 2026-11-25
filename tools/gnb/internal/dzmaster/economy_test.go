package dzmaster

import (
	"encoding/json"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

// repoRoot is the checkout this package lives in (tools/gnb/internal/dzmaster).
var repoRoot = filepath.Join("..", "..", "..", "..")

func loadRepoEconomy(t *testing.T) (*Economy, []MapInfo) {
	t.Helper()
	maps, err := LoadMaps(filepath.Join(repoRoot, "assets", "configs", "nextdayz_maps.json"))
	if err != nil {
		t.Fatal(err)
	}
	e, err := LoadEconomy(repoRoot, maps)
	if err != nil {
		t.Fatal(err)
	}
	if len(e.Sites["cz_loket_riverbend"]) != 15 {
		t.Fatalf("Loket sites: %d", len(e.Sites["cz_loket_riverbend"]))
	}
	return e, maps
}

func newHTTPServer(t *testing.T, m *Master) *httptest.Server {
	t.Helper()
	server := httptest.NewServer(m.Handler())
	t.Cleanup(server.Close)
	return server
}

func canon(c Contract) map[string]any {
	items := []any{}
	for _, item := range c.Items {
		items = append(items, []any{item.Item, item.Count})
	}
	return map[string]any{"id": c.ID, "template": c.Template, "type": c.Type, "tier": c.Tier, "map": c.Map,
		"targets": nonNil(c.Targets), "items": items, "parcel": c.Parcel, "reward": c.Reward,
		"itemReward": c.ItemReward, "offeredAt": c.OfferedAt, "acceptedAt": c.AcceptedAt, "accepted": c.Accepted}
}

func snapshot(step string, a *Account) map[string]any {
	board, accepted := []any{}, []any{}
	for _, c := range a.Board {
		board = append(board, canon(c))
	}
	for _, c := range a.Contracts {
		accepted = append(accepted, canon(c))
	}
	return map[string]any{"step": step, "gold": a.Gold, "reputation": a.Reputation,
		"contractsCompleted": a.ContractsCompleted, "board": board, "contracts": accepted}
}

func normalize(t *testing.T, value any) any {
	t.Helper()
	data, err := json.Marshal(value)
	if err != nil {
		t.Fatal(err)
	}
	var out any
	if err := json.Unmarshal(data, &out); err != nil {
		t.Fatal(err)
	}
	return out
}

// TestEconomyGolden replays the scenario Test_NextDayzContracts.cpp wrote into
// tools/nextdayz/economy_golden.json: the Master and the offline campaign must
// roll the same boards and settle the same way.
func TestEconomyGolden(t *testing.T) {
	e, _ := loadRepoEconomy(t)
	data, err := os.ReadFile(filepath.Join(repoRoot, "tools", "nextdayz", "economy_golden.json"))
	if err != nil {
		t.Fatal(err)
	}
	var golden struct {
		AttachmentItems []string `json:"attachmentItems"`
		Boards          []struct {
			Seed       uint64 `json:"seed"`
			Reputation uint32 `json:"reputation"`
			Board      any    `json:"board"`
		} `json:"boards"`
		Scenario struct {
			Seed  uint64 `json:"seed"`
			Steps []any  `json:"steps"`
		} `json:"scenario"`
	}
	if err := json.Unmarshal(data, &golden); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(golden.AttachmentItems, AttachmentItems) {
		t.Fatalf("attachment items differ: C++ %v, Go %v", golden.AttachmentItems, AttachmentItems)
	}
	for _, want := range golden.Boards {
		a := &Account{BoardRng: want.Seed, Reputation: want.Reputation}
		e.RefreshBoard(a)
		got := normalize(t, snapshot("refresh", a)["board"])
		if !reflect.DeepEqual(got, normalize(t, want.Board)) {
			t.Fatalf("board seed %d reputation %d:\n Go  %v\n C++ %v", want.Seed, want.Reputation, got, want.Board)
		}
	}

	a := &Account{BoardRng: golden.Scenario.Seed}
	var steps []any
	e.RefreshBoard(a)
	steps = append(steps, snapshot("refresh", a))
	acceptRecon := func() {
		for _, c := range a.Board {
			if c.Type == "recon" {
				if err := e.Accept(a, c.ID); err != nil {
					t.Fatal(err)
				}
				break
			}
		}
		steps = append(steps, snapshot("accept-recon", a))
	}
	extract := func(visit bool) {
		a.Sorties++
		a.Character = &Character{}
		var visited []string
		mapID := ""
		if len(a.Contracts) > 0 {
			mapID = a.Contracts[0].Map
			if visit {
				visited = a.Contracts[0].Targets
			}
		}
		e.EndSortie(a, true, visited, mapID)
		step := "extract"
		if visit {
			step = "extract-visited"
		}
		steps = append(steps, snapshot(step, a))
	}
	acceptRecon()
	for _, c := range a.Board {
		if c.Type == "deliver" {
			for _, item := range c.Items {
				a.Storage = AddToStorage(a.Storage, []Stack{{ID: item.Item, Count: item.Count}})
			}
			if err := e.HandIn(a, c.ID, &ContractReport{}); err != nil {
				t.Fatal(err)
			}
			break
		}
	}
	steps = append(steps, snapshot("handin", a))
	extract(true)
	acceptRecon()
	extract(false)
	extract(false)
	extract(false)
	got, want := normalize(t, steps).([]any), normalize(t, golden.Scenario.Steps).([]any)
	if len(got) != len(want) {
		t.Fatalf("scenario steps: Go %d, C++ %d", len(got), len(want))
	}
	for i := range want {
		if !reflect.DeepEqual(got[i], want[i]) {
			t.Fatalf("scenario step %d differs:\n Go  %v\n C++ %v", i, got[i], want[i])
		}
	}
}

func TestEconomyTraderWarehouseAndSales(t *testing.T) {
	e, _ := loadRepoEconomy(t)
	a := &Account{Gold: 1000}
	if err := e.Buy(a, "medkit"); err != nil {
		t.Fatal(err)
	}
	if a.Gold != 925 || storedCount(a.Storage, "medkit") != 1 {
		t.Fatalf("after buying a medkit: gold %d, storage %v", a.Gold, a.Storage)
	}
	if err := e.Buy(a, "signal_flare"); err != errLocked {
		t.Fatalf("flare at tier 0: %v", err)
	}
	if _, err := e.Sell(a, 0, "bandage", 1); err != errNoSale {
		t.Fatalf("stale sale: %v", err)
	}
	value, err := e.Sell(a, 0, "medkit", 1)
	if err != nil || value != 25 || len(a.Storage) != 0 {
		t.Fatalf("sale: %d %v %v", value, err, a.Storage)
	}
	// A loaded rifle with a scope sells with its part.
	a.Storage = []Stack{{ID: "ak", Count: 1, Loaded: 20, Attachments: []int{3, 0, 0, 0, 0}}}
	if v := e.UnitSellValue(a.Storage[0]); v != 120+25 {
		t.Fatalf("ak with 4x scope: %d", v)
	}
	for i := 0; i < 41; i++ {
		a.Storage = append(a.Storage, Stack{ID: "helmet", Count: 1, Loaded: 1}) // never merges
	}
	if !e.WarehouseOver(a) || e.Buy(a, "bandage") != errStorageFull {
		t.Fatal("an over-full warehouse must refuse purchases")
	}
	a.Gold = 300
	if err := e.UpgradeWarehouse(a); err != nil || e.WarehouseCapacity(a) != 60 || e.WarehouseOver(a) {
		t.Fatalf("upgrade: %v capacity %d", err, e.WarehouseCapacity(a))
	}
	migrated := &Account{WarehouseFloor: 70}
	e.ResolveWarehouse(migrated)
	if migrated.WarehouseLevel != 2 || migrated.WarehouseFloor != 0 {
		t.Fatalf("migrated warehouse: level %d floor %d", migrated.WarehouseLevel, migrated.WarehouseFloor)
	}
}

func TestEconomyEndpointsAndCheckpointSettlement(t *testing.T) {
	e, _ := loadRepoEconomy(t)
	launcher := &fakeLauncher{}
	m, err := New(Config{
		DataDir: t.TempDir(), Launch: launcher, MaxPlayers: 4, IdleSeconds: 60, Economy: e,
		Maps: []MapInfo{{ID: "cz_loket_riverbend", Name: "LOKET", Scene: "assets/scad/proc/nextdayz/cz_loket_riverbend/published/v8/scene.scad"}},
		Logf: t.Logf,
	})
	if err != nil {
		t.Fatal(err)
	}
	server := newHTTPServer(t, m)
	var login struct {
		Token string `json:"token"`
	}
	call(t, server, "/api/login", "", map[string]string{"name": "trader"}, &login)
	type profileView struct {
		Gold    int64   `json:"gold"`
		Storage []Stack `json:"storage"`
		Economy struct {
			Reputation uint32 `json:"reputation"`
			Board      []struct {
				ID      uint64         `json:"id"`
				Type    string         `json:"type"`
				Targets []string       `json:"targets"`
				Map     string         `json:"map"`
				Items   []ContractItem `json:"items"`
				Reward  uint64         `json:"reward"`
			} `json:"board"`
			Contracts []struct {
				ID uint64 `json:"id"`
			} `json:"contracts"`
			Trader    []map[string]any `json:"trader"`
			Warehouse struct {
				Capacity uint32 `json:"capacity"`
			} `json:"warehouse"`
		} `json:"economy"`
	}
	profile := func() profileView {
		var p profileView
		call(t, server, "/api/profile", login.Token, struct{}{}, &p)
		return p
	}
	p := profile()
	if len(p.Economy.Board) != 3 || p.Economy.Warehouse.Capacity != 40 || len(p.Economy.Trader) == 0 {
		t.Fatalf("a new account's economy: %+v", p.Economy)
	}
	// Give the account goods through storage, then sell and buy over HTTP.
	_ = m.store.Update(func(st *State) error {
		for _, a := range st.Accounts {
			a.Storage = []Stack{{ID: "svd", Count: 1}}
		}
		return nil
	})
	if status := call(t, server, "/api/sell", login.Token, map[string]any{"index": 0, "item": "ak", "count": 1}, nil); status != 409 {
		t.Fatalf("selling the wrong item: HTTP %d", status)
	}
	if status := call(t, server, "/api/sell", login.Token, map[string]any{"index": 0, "item": "svd", "count": 1}, nil); status != 200 {
		t.Fatalf("selling: HTTP %d", status)
	}
	if status := call(t, server, "/api/buy", login.Token, map[string]any{"item": "medkit"}, nil); status != 200 {
		t.Fatalf("buying: HTTP %d", status)
	}
	if p = profile(); p.Gold != 200-75 || len(p.Storage) != 1 {
		t.Fatalf("after trading: gold %d storage %v", p.Gold, p.Storage)
	}

	// Accept a recon contract, deploy, report the sites from the DS, extract: paid.
	var recon uint64
	var targets []string
	for _, c := range p.Economy.Board {
		if c.Type == "recon" {
			recon, targets = c.ID, c.Targets
		}
	}
	if recon == 0 {
		t.Fatal("the first board has no recon contract")
	}
	if status := call(t, server, "/api/contracts/accept", login.Token, map[string]any{"contractId": recon}, nil); status != 200 {
		t.Fatalf("accept: HTTP %d", status)
	}
	var dispatch struct {
		Server string `json:"server"`
		Ticket string `json:"ticket"`
	}
	call(t, server, "/api/deploy", login.Token, map[string]any{"map": "cz_loket_riverbend"}, &dispatch)
	if status := call(t, server, "/api/contracts/abandon", login.Token, map[string]any{"contractId": recon}, nil); status != 409 {
		t.Fatalf("abandoning in the field: HTTP %d", status)
	}
	key := dsKey(m, dispatch.Server)
	call(t, server, "/ds/register", "", map[string]any{"id": dispatch.Server, "key": key, "port": 1}, nil)
	var redeemed struct {
		PlayerID uint64 `json:"playerId"`
		Sortie   uint64 `json:"sortie"`
	}
	call(t, server, "/ds/redeem", "", map[string]any{"id": dispatch.Server, "key": key, "ticket": dispatch.Ticket}, &redeemed)
	checkpoint := func(seq int, outcome string, visited []string) {
		body := map[string]any{"id": dispatch.Server, "key": key, "seq": seq, "characters": []map[string]any{{
			"playerId": redeemed.PlayerID, "sortie": redeemed.Sortie, "outcome": outcome,
			"inventory": []Stack{}, "visited": visited}}}
		if status := call(t, server, "/ds/checkpoint", "", body, nil); status != 200 {
			t.Fatalf("checkpoint %s: HTTP %d", outcome, status)
		}
	}
	checkpoint(1, "field", targets)
	checkpoint(2, "extracted", targets)
	p = profile()
	if p.Economy.Reputation != 1 || len(p.Economy.Contracts) != 0 || p.Gold < 125+100 {
		t.Fatalf("after extracting with the sites visited: gold %d economy %+v", p.Gold, p.Economy)
	}
}

func TestStoreMigratesV1AccountsToTheEconomy(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "master.json")
	storage := []Stack{}
	for i := 0; i < 70; i++ {
		storage = append(storage, Stack{ID: "helmet", Count: 1, Loaded: i + 1})
	}
	v1 := map[string]any{"version": 1, "nextId": 2, "nextSortie": 1, "worlds": map[string]any{},
		"accounts": map[string]any{"1": map[string]any{"id": 1, "name": "old", "token": "t", "gold": 5, "storage": storage}}}
	data, _ := json.Marshal(v1)
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
	e, _ := loadRepoEconomy(t)
	m, err := New(Config{DataDir: dir, Launch: &fakeLauncher{}, Economy: e,
		Maps: []MapInfo{{ID: "cz_loket_riverbend", Scene: "assets/scad/proc/nextdayz/cz_loket_riverbend/published/v8/scene.scad"}}})
	if err != nil {
		t.Fatal(err)
	}
	m.store.View(func(st *State) {
		a := st.Accounts[1]
		if st.Version != StateVersion || a.WarehouseLevel != 2 || a.WarehouseFloor != 0 || len(a.Board) != 3 || e.WarehouseOver(a) {
			t.Fatalf("migrated: version %d level %d floor %d board %d", st.Version, a.WarehouseLevel, a.WarehouseFloor, len(a.Board))
		}
	})
}
