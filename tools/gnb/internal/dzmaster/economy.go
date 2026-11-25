package dzmaster

// The safehouse economy online (docs/projects/nextdayz/nextdayz-economy-contracts-design.md §7).
// The rules are a port of the offline campaign's C++ (World/Economy.cpp,
// World/Contracts.cpp): the same price table, the same SplitMix64 rolls, the
// same board. tools/nextdayz/economy_golden.json pins both to the same results;
// change a rule in both places and regenerate the golden in the same commit.

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strings"
)

// AttachmentItems maps EWeaponAttachment (Weapons/AttachmentDefs.hpp) to the
// item a mounted part sells as. Index 0 is "no part". The golden checks it.
var AttachmentItems = []string{"", "optic_red_dot", "optic_holographic", "optic_4x", "grip_vertical",
	"mag_quick", "mag_extended", "muzzle_suppressor", "tactical_flashlight"}

type TraderEntry struct {
	Item  string
	Tier  uint32
	Stock int
	Pack  int
	Buy   uint64
}

type ContractItemRange struct {
	Item     string
	Min, Max int
}

type ContractTemplate struct {
	ID, Issuer, Title, Type string
	Tier, Weight            uint32
	Items                   []ContractItemRange
	Bonus                   uint64
	PoiKinds                []string
	PoiCount                uint32
	RiskMin, RiskMax        uint32
	Parcel                  string
	RewardMin, RewardMax    uint64
	ItemRewards             []string
}

type BoardRules struct {
	Slots             []uint32
	ReplaceAfter      uint64
	ExpireAfter       uint64
	MaxActive         int
	DeliverMultiplier float64
}

type WarehouseStep struct {
	Capacity uint32
	Price    uint64
}

// EconomyDefs is assets/configs/nextdayz_economy.json. The C++ loader
// validates the file in full (gkNextUnitTests); this side trusts its shape.
type EconomyDefs struct {
	Sell              map[string]uint64
	Trader            []TraderEntry
	WarehouseBase     uint32
	WarehouseSteps    []WarehouseStep
	ReputationTiers   []uint32
	ReputationGain    []uint32
	ReputationPenalty uint32
	Board             BoardRules
	Contracts         []ContractTemplate
}

// Poi is one site of a published map (its generation.json), as World/PoiIndex reads it.
type Poi struct {
	ID   string `json:"id"`
	Name string `json:"name"`
	Kind string `json:"kind"`
	Risk uint32 `json:"risk"`
}

// Economy is the price table plus the sites of every dispatchable map.
type Economy struct {
	Defs  EconomyDefs
	Sites map[string][]Poi
	maps  []string // Sites keys in order: board rolls walk maps by id
}

func ParseEconomyDefs(data []byte) (EconomyDefs, error) {
	var raw struct {
		Version   int               `json:"version"`
		Sell      map[string]uint64 `json:"sell"`
		BuyMarkup float64           `json:"buyMarkup"`
		Trader    []struct {
			Item  string  `json:"item"`
			Tier  uint32  `json:"tier"`
			Stock int     `json:"stock"`
			Pack  int     `json:"pack"`
			Buy   *uint64 `json:"buy"`
		} `json:"trader"`
		Services struct {
			Warehouse struct {
				Base  uint32      `json:"base"`
				Steps [][2]uint64 `json:"steps"`
			} `json:"warehouse"`
		} `json:"services"`
		Reputation struct {
			Tiers   []uint32 `json:"tiers"`
			Gain    []uint32 `json:"gain"`
			Penalty uint32   `json:"penalty"`
		} `json:"reputation"`
		Board struct {
			Slots             []uint32 `json:"slots"`
			ReplaceAfter      uint64   `json:"replaceAfter"`
			ExpireAfter       uint64   `json:"expireAfter"`
			MaxActive         int      `json:"maxActive"`
			DeliverMultiplier float64  `json:"deliverMultiplier"`
		} `json:"board"`
		Contracts []struct {
			ID     string               `json:"id"`
			Issuer string               `json:"issuer"`
			Title  string               `json:"title"`
			Type   string               `json:"type"`
			Tier   uint32               `json:"tier"`
			Weight uint32               `json:"weight"`
			Items  [][3]json.RawMessage `json:"items"`
			Bonus  uint64               `json:"bonus"`
			Poi    *struct {
				Kind  []string `json:"kind"`
				Count uint32   `json:"count"`
				Risk  []uint32 `json:"risk"`
			} `json:"poi"`
			Reward     []uint64 `json:"reward"`
			Parcel     string   `json:"parcel"`
			ItemReward []string `json:"itemReward"`
		} `json:"contracts"`
	}
	if err := json.Unmarshal(data, &raw); err != nil {
		return EconomyDefs{}, err
	}
	if raw.Version != 1 || len(raw.Sell) == 0 || len(raw.Reputation.Tiers) == 0 || len(raw.Board.Slots) == 0 {
		return EconomyDefs{}, fmt.Errorf("unsupported or incomplete economy table")
	}
	defs := EconomyDefs{Sell: raw.Sell, WarehouseBase: raw.Services.Warehouse.Base,
		ReputationTiers: raw.Reputation.Tiers, ReputationGain: raw.Reputation.Gain, ReputationPenalty: raw.Reputation.Penalty,
		Board: BoardRules{Slots: raw.Board.Slots, ReplaceAfter: raw.Board.ReplaceAfter, ExpireAfter: raw.Board.ExpireAfter,
			MaxActive: raw.Board.MaxActive, DeliverMultiplier: raw.Board.DeliverMultiplier}}
	for _, step := range raw.Services.Warehouse.Steps {
		defs.WarehouseSteps = append(defs.WarehouseSteps, WarehouseStep{Capacity: uint32(step[0]), Price: step[1]})
	}
	for _, shelf := range raw.Trader {
		entry := TraderEntry{Item: shelf.Item, Tier: shelf.Tier, Stock: shelf.Stock, Pack: shelf.Pack}
		if shelf.Buy != nil {
			entry.Buy = *shelf.Buy
		} else {
			entry.Buy = uint64(math.Round(float64(raw.Sell[shelf.Item]) * raw.BuyMarkup))
		}
		defs.Trader = append(defs.Trader, entry)
	}
	for _, c := range raw.Contracts {
		entry := ContractTemplate{ID: c.ID, Issuer: c.Issuer, Title: c.Title, Type: c.Type, Tier: c.Tier, Weight: c.Weight,
			Bonus: c.Bonus, PoiCount: 1, RiskMin: 1, RiskMax: 3, Parcel: c.Parcel, ItemRewards: c.ItemReward}
		for _, item := range c.Items {
			var r ContractItemRange
			if json.Unmarshal(item[0], &r.Item) != nil || json.Unmarshal(item[1], &r.Min) != nil || json.Unmarshal(item[2], &r.Max) != nil {
				return EconomyDefs{}, fmt.Errorf("contract %s: items are [item, min, max]", c.ID)
			}
			entry.Items = append(entry.Items, r)
		}
		if c.Poi != nil {
			entry.PoiKinds = c.Poi.Kind
			if c.Poi.Count > 0 {
				entry.PoiCount = c.Poi.Count
			}
			if len(c.Poi.Risk) == 2 {
				entry.RiskMin, entry.RiskMax = c.Poi.Risk[0], c.Poi.Risk[1]
			}
		}
		if len(c.Reward) == 2 {
			entry.RewardMin, entry.RewardMax = c.Reward[0], c.Reward[1]
		}
		defs.Contracts = append(defs.Contracts, entry)
	}
	return defs, nil
}

// PoiKind is nd_poi_kind (nextdayz_pcg.scad), as World/PoiIndex.cpp ports it.
func PoiKind(siteType string) string {
	switch {
	case siteType == "medical":
		return "hospital"
	case siteType == "military" || strings.HasPrefix(siteType, "tg_"):
		return "military"
	case siteType == "hillcastle" || siteType == "castle":
		return "fortress"
	case siteType == "square" || siteType == "street":
		return "town"
	case siteType == "jzd" || siteType == "factory" || siteType == "industry":
		return "industrial"
	case siteType == "homestead" || siteType == "farm":
		return "farm"
	}
	return "village"
}

// LoadPois reads the sites of a published map from the generation record next to its scene.
func LoadPois(generation string) ([]Poi, error) {
	data, err := os.ReadFile(generation)
	if err != nil {
		return nil, err
	}
	var record struct {
		Sites []struct {
			ID   string `json:"id"`
			Name string `json:"name"`
			Kind string `json:"kind"`
			Risk uint32 `json:"risk"`
		} `json:"sites"`
	}
	if err := json.Unmarshal(data, &record); err != nil {
		return nil, err
	}
	var pois []Poi
	for _, site := range record.Sites {
		risk := site.Risk
		if risk < 1 {
			risk = 1
		} else if risk > 3 {
			risk = 3
		}
		pois = append(pois, Poi{ID: site.ID, Name: site.Name, Kind: PoiKind(site.Kind), Risk: risk})
	}
	return pois, nil
}

// LoadEconomy reads the price table and the sites of every non-training map under assetRoot.
func LoadEconomy(assetRoot string, maps []MapInfo) (*Economy, error) {
	data, err := os.ReadFile(filepath.Join(assetRoot, "assets", "configs", "nextdayz_economy.json"))
	if err != nil {
		return nil, err
	}
	defs, err := ParseEconomyDefs(data)
	if err != nil {
		return nil, err
	}
	sites := map[string][]Poi{}
	for _, m := range maps {
		if m.Training {
			continue
		}
		generation := filepath.Join(assetRoot, filepath.FromSlash(path.Join(path.Dir(m.Scene), "generation.json")))
		pois, err := LoadPois(generation)
		if err != nil || len(pois) == 0 {
			continue // a map without a generation record offers no field contracts
		}
		sites[m.ID] = pois
	}
	return NewEconomy(defs, sites), nil
}

func NewEconomy(defs EconomyDefs, sites map[string][]Poi) *Economy {
	e := &Economy{Defs: defs, Sites: sites}
	for id := range sites {
		e.maps = append(e.maps, id)
	}
	sort.Strings(e.maps)
	return e
}

// ---------------------------------------------------------------- random numbers

// boardRng is Data/DeterministicRng.hpp FDeterministicRng (SplitMix64), bit for bit.
type boardRng struct{ state uint64 }

func (r *boardRng) next() uint32 {
	r.state += 0x9e3779b97f4a7c15
	v := r.state
	v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9
	v = (v ^ (v >> 27)) * 0x94d049bb133111eb
	return uint32((v ^ (v >> 31)) >> 32)
}

func (r *boardRng) rangeOf(n uint32) uint32 {
	if n == 0 {
		return 0
	}
	return r.next() % n
}

func (r *boardRng) roll(min, max uint64) uint64 {
	span := max - min + 1
	if span > math.MaxUint32 {
		return min + uint64(r.next())
	}
	return min + uint64(r.rangeOf(uint32(span)))
}

// ---------------------------------------------------------------- account economy

// Contract mirrors FContract. Accepted contracts live in Account.Contracts.
type Contract struct {
	ID         uint64         `json:"id"`
	Template   string         `json:"template"`
	Type       string         `json:"type"`
	Tier       uint32         `json:"tier"`
	Map        string         `json:"map,omitempty"`
	Targets    []string       `json:"targets,omitempty"`
	Items      []ContractItem `json:"items,omitempty"`
	Parcel     string         `json:"parcel,omitempty"`
	Reward     uint64         `json:"reward"`
	ItemReward string         `json:"itemReward,omitempty"`
	OfferedAt  uint64         `json:"offeredAt"`
	AcceptedAt uint64         `json:"acceptedAt"`
	Accepted   bool           `json:"accepted,omitempty"`
}

type ContractItem struct {
	Item  string `json:"item"`
	Count int    `json:"count"`
}

func isField(kind string) bool { return kind == "recon" || kind == "fetch" }

// FinishedSorties counts ended sorties; a survivor in the field is the one still open.
func FinishedSorties(a *Account) uint64 {
	if a.Character != nil && a.Sorties > 0 {
		return a.Sorties - 1
	}
	return a.Sorties
}

func cycleOf(a *Account) uint64 { return FinishedSorties(a) + 1 }

func (e *Economy) ReputationTier(reputation uint32) uint32 {
	tier := uint32(0)
	for index := 1; index < len(e.Defs.ReputationTiers); index++ {
		if reputation >= e.Defs.ReputationTiers[index] {
			tier = uint32(index)
		}
	}
	return tier
}

func (e *Economy) allowedTier(a *Account) uint32 {
	if tier := e.ReputationTier(a.Reputation); tier > 1 {
		return tier
	}
	return 1
}

func (e *Economy) BoardSlots(a *Account) uint32 {
	slots := e.Defs.Board.Slots
	tier := int(e.ReputationTier(a.Reputation))
	if tier >= len(slots) {
		tier = len(slots) - 1
	}
	return slots[tier]
}

func (e *Economy) template(id string) *ContractTemplate {
	for i := range e.Defs.Contracts {
		if e.Defs.Contracts[i].ID == id {
			return &e.Defs.Contracts[i]
		}
	}
	return nil
}

func usesTemplate(list []Contract, id string) bool {
	for _, c := range list {
		if c.Template == id {
			return true
		}
	}
	return false
}

func usesItem(list []Contract, item string) bool {
	for _, c := range list {
		for _, entry := range c.Items {
			if entry.Item == item {
				return true
			}
		}
	}
	return false
}

func usesParcel(list []Contract, parcel string) bool {
	for _, c := range list {
		if c.Parcel == parcel {
			return true
		}
	}
	return false
}

type placement struct {
	mapID      string
	candidates []*Poi
}

func (e *Economy) placements(t *ContractTemplate) []placement {
	var result []placement
	for _, mapID := range e.maps {
		var candidates []*Poi
		sites := e.Sites[mapID]
		for i := range sites {
			site := &sites[i]
			kind := len(t.PoiKinds) == 0
			for _, k := range t.PoiKinds {
				kind = kind || k == site.Kind
			}
			if kind && site.Risk >= t.RiskMin && site.Risk <= t.RiskMax {
				candidates = append(candidates, site)
			}
		}
		if uint32(len(candidates)) >= t.PoiCount {
			result = append(result, placement{mapID, candidates})
		}
	}
	return result
}

func (e *Economy) eligible(a *Account, t *ContractTemplate, fieldOnly bool) bool {
	if t.Tier > e.allowedTier(a) || (fieldOnly && !isField(t.Type)) {
		return false
	}
	if usesTemplate(a.Board, t.ID) || usesTemplate(a.Contracts, t.ID) {
		return false
	}
	if t.Type == "deliver" {
		for _, item := range t.Items {
			if usesItem(a.Board, item.Item) {
				return false
			}
		}
		return true
	}
	if t.Parcel != "" && (usesParcel(a.Board, t.Parcel) || usesParcel(a.Contracts, t.Parcel)) {
		return false
	}
	return len(e.placements(t)) > 0
}

func (e *Economy) rollContract(a *Account, t *ContractTemplate, rng *boardRng) Contract {
	a.ContractSerial++
	c := Contract{ID: a.ContractSerial, Template: t.ID, Type: t.Type, Tier: t.Tier, OfferedAt: FinishedSorties(a)}
	if t.Type == "deliver" {
		value := 0.0
		for _, r := range t.Items {
			count := int(rng.roll(uint64(r.Min), uint64(r.Max)))
			c.Items = append(c.Items, ContractItem{Item: r.Item, Count: count})
			value += float64(e.Defs.Sell[r.Item]) * float64(count)
		}
		c.Reward = uint64(math.Round(value*e.Defs.Board.DeliverMultiplier)) + t.Bonus
		return c
	}
	places := e.placements(t)
	place := places[rng.rangeOf(uint32(len(places)))]
	c.Map = place.mapID
	candidates := place.candidates
	for pick := uint32(0); pick < t.PoiCount; pick++ {
		index := pick + rng.rangeOf(uint32(len(candidates))-pick)
		candidates[pick], candidates[index] = candidates[index], candidates[pick]
		c.Targets = append(c.Targets, candidates[pick].Name)
	}
	c.Parcel = t.Parcel
	c.Reward = rng.roll(t.RewardMin, t.RewardMax)
	if len(t.ItemRewards) > 0 {
		c.ItemReward = t.ItemRewards[rng.rangeOf(uint32(len(t.ItemRewards)))]
	}
	return c
}

// RefreshBoard is Contracts.cpp RefreshBoard.
func (e *Economy) RefreshBoard(a *Account) {
	cycle := cycleOf(a)
	if a.BoardCycle == cycle || a.Character != nil {
		return
	}
	if a.BoardRng == 0 && a.BoardCycle == 0 {
		a.BoardRng = a.ID*0x9e3779b97f4a7c15 ^ 0x636f6e7472616374 // derived from the account
	}
	a.BoardCycle = cycle
	finished := FinishedSorties(a)
	board := a.Board[:0:0]
	for _, c := range a.Board {
		stale := finished-min(finished, c.OfferedAt) >= e.Defs.Board.ReplaceAfter || e.template(c.Template) == nil
		if !stale && isField(c.Type) {
			sites, ok := e.Sites[c.Map]
			stale = !ok
			for _, target := range c.Targets {
				found := false
				for _, site := range sites {
					found = found || site.Name == target
				}
				stale = stale || !found
			}
		}
		if !stale {
			board = append(board, c)
		}
	}
	a.Board = board
	rng := boardRng{state: a.BoardRng}
	slots := e.BoardSlots(a)
	for attempt := 0; uint32(len(a.Board)) < slots && attempt < 32; attempt++ {
		hasField := false
		for _, c := range a.Board {
			hasField = hasField || isField(c.Type)
		}
		var eligible []*ContractTemplate
		for i := range e.Defs.Contracts {
			if e.eligible(a, &e.Defs.Contracts[i], !hasField) {
				eligible = append(eligible, &e.Defs.Contracts[i])
			}
		}
		if len(eligible) == 0 && !hasField {
			for i := range e.Defs.Contracts {
				if e.eligible(a, &e.Defs.Contracts[i], false) {
					eligible = append(eligible, &e.Defs.Contracts[i])
				}
			}
		}
		if len(eligible) == 0 {
			break
		}
		total := uint32(0)
		for _, t := range eligible {
			total += t.Weight
		}
		pick := rng.rangeOf(total)
		chosen := eligible[len(eligible)-1]
		for _, t := range eligible {
			if pick < t.Weight {
				chosen = t
				break
			}
			pick -= t.Weight
		}
		a.Board = append(a.Board, e.rollContract(a, chosen, &rng))
	}
	a.BoardRng = rng.state
}

// ContractReport is FContractReport without the parcels (online has no fetch contracts yet).
type ContractReport struct {
	Completed   []string `json:"completed"`
	Expired     []string `json:"expired"`
	Gold        uint64   `json:"gold"`
	Reputation  int64    `json:"reputation"`
	ItemRewards []string `json:"itemRewards"`
}

func (e *Economy) penalize(a *Account) int64 {
	floor := e.Defs.ReputationTiers[e.ReputationTier(a.Reputation)]
	before := a.Reputation
	a.Reputation -= min(a.Reputation, e.Defs.ReputationPenalty)
	if a.Reputation < floor {
		a.Reputation = floor
	}
	return int64(a.Reputation) - int64(before)
}

func (e *Economy) complete(a *Account, c Contract, report *ContractReport) {
	a.Gold += int64(c.Reward)
	gain := uint32(0)
	if c.Tier >= 1 && int(c.Tier) <= len(e.Defs.ReputationGain) {
		gain = e.Defs.ReputationGain[c.Tier-1]
	}
	a.Reputation += gain
	if c.ItemReward != "" {
		a.Storage = AddToStorage(a.Storage, []Stack{{ID: c.ItemReward, Count: 1}})
		report.ItemRewards = append(report.ItemRewards, c.ItemReward)
	}
	a.ContractsCompleted++
	report.Completed = append(report.Completed, c.Template)
	report.Gold += c.Reward
	report.Reputation += int64(gain)
}

func storedCount(storage []Stack, item string) int {
	count := 0
	for _, s := range storage {
		if s.ID == item {
			count += s.Count
		}
	}
	return count
}

func canHandIn(a *Account, c *Contract) bool {
	if c.Type != "deliver" {
		return false
	}
	for _, item := range c.Items {
		if storedCount(a.Storage, item.Item) < item.Count {
			return false
		}
	}
	return true
}

func takeUnits(storage []Stack, item string, count int) []Stack {
	for _, plainPass := range []bool{true, false} {
		for i := range storage {
			if count == 0 {
				break
			}
			if storage[i].ID != item || storage[i].Plain() != plainPass {
				continue
			}
			taken := min(count, storage[i].Count)
			storage[i].Count -= taken
			count -= taken
		}
	}
	kept := storage[:0]
	for _, s := range storage {
		if s.Count > 0 {
			kept = append(kept, s)
		}
	}
	return kept
}

func findContract(list []Contract, id uint64) int {
	for i, c := range list {
		if c.ID == id {
			return i
		}
	}
	return -1
}

var (
	errInField      = fmt.Errorf("contracts are accepted and abandoned between expeditions")
	errNoContract   = fmt.Errorf("that contract is no longer on offer")
	errWrongState   = fmt.Errorf("that contract does not work that way")
	errTooMany      = fmt.Errorf("you are already working on as many field contracts as you can")
	errMissingItems = fmt.Errorf("your storage does not hold everything this delivery needs")
	errFetchOffline = fmt.Errorf("fetch contracts are offline-only for now; take a recon contract")
)

func (e *Economy) Accept(a *Account, id uint64) error {
	if a.Character != nil {
		return errInField
	}
	index := findContract(a.Board, id)
	if index < 0 {
		return errNoContract
	}
	c := a.Board[index]
	if !isField(c.Type) {
		return errWrongState
	}
	if c.Type == "fetch" {
		return errFetchOffline // a parcel would have to belong to one survivor in a shared world (§7.4)
	}
	if len(a.Contracts) >= e.Defs.Board.MaxActive {
		return errTooMany
	}
	a.Board = append(a.Board[:index:index], a.Board[index+1:]...)
	c.Accepted = true
	c.AcceptedAt = FinishedSorties(a)
	a.Contracts = append(a.Contracts, c)
	return nil
}

func (e *Economy) Abandon(a *Account, id uint64, report *ContractReport) error {
	if a.Character != nil {
		return errInField
	}
	index := findContract(a.Contracts, id)
	if index < 0 {
		return errNoContract
	}
	report.Expired = append(report.Expired, a.Contracts[index].Template)
	a.Contracts = append(a.Contracts[:index:index], a.Contracts[index+1:]...)
	report.Reputation += e.penalize(a)
	return nil
}

// HandIn completes a delivery from storage. Storage belongs to the account, so
// this works while the survivor is in the field too.
func (e *Economy) HandIn(a *Account, id uint64, report *ContractReport) error {
	index := findContract(a.Board, id)
	if index < 0 {
		return errNoContract
	}
	c := a.Board[index]
	if c.Type != "deliver" {
		return errWrongState
	}
	if !canHandIn(a, &c) {
		return errMissingItems
	}
	a.Board = append(a.Board[:index:index], a.Board[index+1:]...)
	for _, item := range c.Items {
		a.Storage = takeUnits(a.Storage, item.Item, item.Count)
	}
	e.complete(a, c, report)
	return nil
}

func reconComplete(c *Contract, visited []string, mapID string) bool {
	if c.Type != "recon" || c.Map != mapID || len(c.Targets) == 0 {
		return false
	}
	for _, target := range c.Targets {
		found := false
		for _, v := range visited {
			found = found || v == target
		}
		if !found {
			return false
		}
	}
	return true
}

// Settle runs at extraction, before anything expires (Contracts.cpp SettleContracts).
func (e *Economy) Settle(a *Account, visited []string, mapID string) ContractReport {
	var report ContractReport
	var remaining []Contract
	for _, c := range a.Contracts {
		if reconComplete(&c, visited, mapID) {
			e.complete(a, c, &report)
		} else {
			remaining = append(remaining, c)
		}
	}
	a.Contracts = remaining
	return report
}

// Expire runs once the sortie has ended (the survivor is out of the field).
func (e *Economy) Expire(a *Account, report *ContractReport) {
	finished := FinishedSorties(a)
	var remaining []Contract
	for _, c := range a.Contracts {
		if finished-min(finished, c.AcceptedAt) < e.Defs.Board.ExpireAfter {
			remaining = append(remaining, c)
			continue
		}
		report.Expired = append(report.Expired, c.Template)
		report.Reputation += e.penalize(a)
	}
	a.Contracts = remaining
}

// EndSortie is the order shared with the offline campaign: settle (extraction
// only) before the survivor leaves the field, then expire and refresh.
func (e *Economy) EndSortie(a *Account, extracted bool, visited []string, mapID string) ContractReport {
	var report ContractReport
	if extracted {
		report = e.Settle(a, visited, mapID)
	}
	a.Character = nil
	e.Expire(a, &report)
	e.RefreshBoard(a)
	return report
}

// ---------------------------------------------------------------- trader and warehouse

func (e *Economy) UnitSellValue(s Stack) uint64 {
	price := e.Defs.Sell[s.ID]
	for _, part := range s.Attachments {
		if part > 0 && part < len(AttachmentItems) {
			price += e.Defs.Sell[AttachmentItems[part]]
		}
	}
	return price
}

func (e *Economy) traderEntry(item string) *TraderEntry {
	for i := range e.Defs.Trader {
		if e.Defs.Trader[i].Item == item {
			return &e.Defs.Trader[i]
		}
	}
	return nil
}

func (e *Economy) refreshTrader(a *Account) {
	cycle := cycleOf(a)
	if a.TraderCycle == cycle {
		return
	}
	a.TraderCycle = cycle
	a.TraderStock = map[string]int{}
	for _, entry := range e.Defs.Trader {
		a.TraderStock[entry.Item] = entry.Stock
	}
}

func (e *Economy) TraderRemaining(a *Account, entry *TraderEntry) int {
	if a.TraderCycle != cycleOf(a) {
		return entry.Stock
	}
	left, ok := a.TraderStock[entry.Item]
	if !ok {
		return entry.Stock
	}
	return max(0, min(left, entry.Stock))
}

func (e *Economy) levelCapacity(level uint32) uint32 {
	if level == 0 || len(e.Defs.WarehouseSteps) == 0 {
		return e.Defs.WarehouseBase
	}
	if int(level) > len(e.Defs.WarehouseSteps) {
		level = uint32(len(e.Defs.WarehouseSteps))
	}
	return e.Defs.WarehouseSteps[level-1].Capacity
}

func (e *Economy) WarehouseCapacity(a *Account) uint32 {
	return max(e.levelCapacity(a.WarehouseLevel), a.WarehouseFloor)
}

func (e *Economy) WarehouseOver(a *Account) bool {
	return uint32(len(a.Storage)) > e.WarehouseCapacity(a)
}

func (e *Economy) nextStep(a *Account) *WarehouseStep {
	if int(a.WarehouseLevel) < len(e.Defs.WarehouseSteps) {
		return &e.Defs.WarehouseSteps[a.WarehouseLevel]
	}
	return nil
}

// ResolveWarehouse turns a migrated account's floor into the lowest level that holds it.
func (e *Economy) ResolveWarehouse(a *Account) {
	if a.WarehouseFloor == 0 || len(e.Defs.WarehouseSteps) == 0 {
		return
	}
	for int(a.WarehouseLevel) < len(e.Defs.WarehouseSteps) && e.levelCapacity(a.WarehouseLevel) < a.WarehouseFloor {
		a.WarehouseLevel++
	}
	if e.levelCapacity(a.WarehouseLevel) >= a.WarehouseFloor {
		a.WarehouseFloor = 0
	}
}

var (
	errNotStocked  = fmt.Errorf("the trader does not sell that")
	errLocked      = fmt.Errorf("your reputation is too low for this shelf")
	errSoldOut     = fmt.Errorf("sold out; the trader restocks after your next expedition")
	errNoGold      = fmt.Errorf("not enough gold")
	errStorageFull = fmt.Errorf("your storage is full; sell, hand in or carry items, or expand the warehouse")
	errNoSale      = fmt.Errorf("that storage stack cannot be sold")
	errMaxLevel    = fmt.Errorf("the warehouse is fully expanded")
)

// Buy puts one pack into storage (Economy.cpp Buy). Storage belongs to the
// account, so the trader deals while the survivor is in the field as well.
func (e *Economy) Buy(a *Account, item string) error {
	entry := e.traderEntry(item)
	if entry == nil {
		return errNotStocked
	}
	if entry.Tier > e.ReputationTier(a.Reputation) {
		return errLocked
	}
	if e.WarehouseOver(a) {
		return errStorageFull
	}
	if e.TraderRemaining(a, entry) <= 0 {
		return errSoldOut
	}
	price := entry.Buy * uint64(entry.Pack)
	if a.Gold < int64(price) {
		return errNoGold
	}
	storage := AddToStorage(append([]Stack(nil), a.Storage...), []Stack{{ID: entry.Item, Count: entry.Pack}})
	if len(storage) > len(a.Storage) && uint32(len(storage)) > e.WarehouseCapacity(a) {
		return errStorageFull
	}
	e.refreshTrader(a)
	a.Storage = storage
	a.Gold -= int64(price)
	a.TraderStock[entry.Item]--
	return nil
}

// Sell sells `count` of storage stack `index`, which must still hold `item`
// (indices shift as stacks empty, so a stale request is refused, not misapplied).
func (e *Economy) Sell(a *Account, index int, item string, count int) (uint64, error) {
	if index < 0 || index >= len(a.Storage) || a.Storage[index].ID != item || count <= 0 || count > a.Storage[index].Count {
		return 0, errNoSale
	}
	price := e.UnitSellValue(a.Storage[index])
	if price == 0 {
		return 0, errNoSale
	}
	value := price * uint64(count)
	a.Gold += int64(value)
	a.Storage[index].Count -= count
	if a.Storage[index].Count == 0 {
		a.Storage = append(a.Storage[:index:index], a.Storage[index+1:]...)
	}
	return value, nil
}

func (e *Economy) UpgradeWarehouse(a *Account) error {
	step := e.nextStep(a)
	if step == nil {
		return errMaxLevel
	}
	if a.Gold < int64(step.Price) {
		return errNoGold
	}
	a.Gold -= int64(step.Price)
	a.WarehouseLevel++
	if a.WarehouseFloor <= e.levelCapacity(a.WarehouseLevel) {
		a.WarehouseFloor = 0
	}
	return nil
}

// ---------------------------------------------------------------- profile view

// View is the "economy" section of /api/profile: everything a lobby shows,
// already priced, so clients add no rules.
func (e *Economy) View(a *Account) map[string]any {
	tier := e.ReputationTier(a.Reputation)
	next := uint32(0)
	if int(tier)+1 < len(e.Defs.ReputationTiers) {
		next = e.Defs.ReputationTiers[tier+1]
	}
	describe := func(list []Contract) []map[string]any {
		out := []map[string]any{}
		finished := FinishedSorties(a)
		for _, c := range list {
			t := e.template(c.Template)
			title, issuer := c.Template, ""
			if t != nil {
				title, issuer = t.Title, t.Issuer
			}
			since, limit := finished-min(finished, c.OfferedAt), e.Defs.Board.ReplaceAfter
			if c.Accepted {
				since, limit = finished-min(finished, c.AcceptedAt), e.Defs.Board.ExpireAfter
			}
			gain := uint32(0)
			if c.Tier >= 1 && int(c.Tier) <= len(e.Defs.ReputationGain) {
				gain = e.Defs.ReputationGain[c.Tier-1]
			}
			entry := map[string]any{"id": c.ID, "template": c.Template, "title": title, "issuer": issuer, "type": c.Type,
				"tier": c.Tier, "map": c.Map, "targets": nonNil(c.Targets), "items": nonNil(c.Items), "reward": c.Reward,
				"reputation": gain, "itemReward": c.ItemReward, "sortiesLeft": limit - min(limit, since),
				"canHandIn": canHandIn(a, &c), "online": c.Type != "fetch"}
			out = append(out, entry)
		}
		return out
	}
	trader := []map[string]any{}
	for i := range e.Defs.Trader {
		entry := &e.Defs.Trader[i]
		trader = append(trader, map[string]any{"item": entry.Item, "tier": entry.Tier, "pack": entry.Pack,
			"price": entry.Buy * uint64(entry.Pack), "left": e.TraderRemaining(a, entry), "locked": entry.Tier > tier})
	}
	values := make([]uint64, len(a.Storage))
	for i, s := range a.Storage {
		values[i] = e.UnitSellValue(s)
	}
	warehouse := map[string]any{"used": len(a.Storage), "capacity": e.WarehouseCapacity(a), "over": e.WarehouseOver(a)}
	if step := e.nextStep(a); step != nil {
		warehouse["nextCapacity"] = step.Capacity
		warehouse["nextPrice"] = step.Price
	}
	return map[string]any{"reputation": a.Reputation, "tier": tier, "nextTier": next, "board": describe(a.Board),
		"contracts": describe(a.Contracts), "maxActive": e.Defs.Board.MaxActive, "slots": e.BoardSlots(a),
		"contractsCompleted": a.ContractsCompleted, "trader": trader, "sellValues": values, "warehouse": warehouse}
}
