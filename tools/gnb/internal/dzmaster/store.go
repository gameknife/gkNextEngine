// Package dzmaster is the NextDayz Master: accounts and their safehouse
// storage, persistent survivors in the field, the fleet of map dedicated
// servers (DS), dispatch with one-time tickets, and atomic DS checkpoints
// (a map's world and every survivor on it, committed together).
// See docs/projects/nextdayz/nextdayz-multiplayer-design.md.
package dzmaster

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sync"
)

// Stack is one ground/storage stack, the same shape as NextDayz FLootStack.
type Stack struct {
	ID          string `json:"id"`
	Count       int    `json:"count"`
	Loaded      int    `json:"loaded,omitempty"`
	Attachments []int  `json:"attachments,omitempty"`
}

// Plain says whether two stacks of the same item can merge (no rounds, no parts).
func (s Stack) Plain() bool {
	if s.Loaded != 0 {
		return false
	}
	for _, a := range s.Attachments {
		if a != 0 {
			return false
		}
	}
	return true
}

// Character is a survivor in the field: what the DS last checkpointed.
type Character struct {
	Map       string          `json:"map"`
	Position  [3]float64      `json:"position"`
	Yaw       float64         `json:"yaw"`
	Survival  json.RawMessage `json:"survival,omitempty"`
	Inventory []Stack         `json:"inventory"`
	Sortie    uint64          `json:"sortie"`
	Fresh     bool            `json:"fresh"` // never reached a DS yet: spawn at a safe point
}

type Account struct {
	ID          uint64     `json:"id"`
	Name        string     `json:"name"`
	Token       string     `json:"token"`
	Gold        int64      `json:"gold"`
	Storage     []Stack    `json:"storage"`
	Character   *Character `json:"character,omitempty"`
	Sorties     uint64     `json:"sorties"`
	Extractions uint64     `json:"extractions"`
	Deaths      uint64     `json:"deaths"`
	LastOutcome string     `json:"lastOutcome,omitempty"`
}

// World is a map's persistent state as its DS last committed it (opaque JSON).
type World struct {
	Seq  uint64          `json:"seq"`
	Blob json.RawMessage `json:"blob"`
}

type State struct {
	Version    int                 `json:"version"`
	NextID     uint64              `json:"nextId"`
	NextSortie uint64              `json:"nextSortie"`
	Accounts   map[uint64]*Account `json:"accounts"`
	Worlds     map[string]*World   `json:"worlds"`
}

// Store keeps State in one JSON file. Every Save writes a temporary file and
// renames it over the old one (keeping a .bak), so a commit is all or nothing.
type Store struct {
	mu    sync.Mutex
	path  string
	state State
}

func OpenStore(path string) (*Store, error) {
	s := &Store{path: path, state: State{Version: 1, NextID: 1, NextSortie: 1,
		Accounts: map[uint64]*Account{}, Worlds: map[string]*World{}}}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return nil, err
	}
	data, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return s, s.saveLocked()
	}
	if err != nil {
		return nil, err
	}
	if err := json.Unmarshal(data, &s.state); err != nil {
		// Fall back to the previous good file rather than starting over.
		backup, berr := os.ReadFile(path + ".bak")
		if berr != nil || json.Unmarshal(backup, &s.state) != nil {
			return nil, fmt.Errorf("master store %s is unreadable: %w", path, err)
		}
	}
	if s.state.Accounts == nil {
		s.state.Accounts = map[uint64]*Account{}
	}
	if s.state.Worlds == nil {
		s.state.Worlds = map[string]*World{}
	}
	return s, nil
}

// Update runs fn under the store lock and commits the result atomically. If fn
// returns an error nothing is written (and fn must not have changed state).
func (s *Store) Update(fn func(*State) error) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	snapshot, err := json.Marshal(s.state)
	if err != nil {
		return err
	}
	if err := fn(&s.state); err != nil {
		var restored State
		_ = json.Unmarshal(snapshot, &restored)
		s.state = restored
		return err
	}
	return s.saveLocked()
}

// View runs fn under the store lock without committing.
func (s *Store) View(fn func(*State)) {
	s.mu.Lock()
	defer s.mu.Unlock()
	fn(&s.state)
}

func (s *Store) saveLocked() error {
	data, err := json.MarshalIndent(s.state, "", "  ")
	if err != nil {
		return err
	}
	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	if _, err := os.Stat(s.path); err == nil {
		_ = os.Remove(s.path + ".bak")
		if err := os.Rename(s.path, s.path+".bak"); err != nil {
			return err
		}
	}
	return os.Rename(tmp, s.path)
}

func (st *State) AccountByName(name string) *Account {
	for _, a := range st.Accounts {
		if a.Name == name {
			return a
		}
	}
	return nil
}

func (st *State) AccountByToken(token string) *Account {
	if token == "" {
		return nil
	}
	for _, a := range st.Accounts {
		if a.Token == token {
			return a
		}
	}
	return nil
}

// AddToStorage merges plain stacks of the same item; anything with rounds or parts stays its own stack.
func AddToStorage(storage []Stack, items []Stack) []Stack {
	for _, item := range items {
		if item.Count <= 0 || item.ID == "" {
			continue
		}
		merged := false
		if item.Plain() {
			for i := range storage {
				if storage[i].ID == item.ID && storage[i].Plain() {
					storage[i].Count += item.Count
					merged = true
					break
				}
			}
		}
		if !merged {
			storage = append(storage, item)
		}
	}
	return storage
}

// Bring is a request to take `Count` of storage stack `Index` into the field.
type Bring struct {
	Index int `json:"index"`
	Count int `json:"count"`
}

// TakeFromStorage moves the requested counts out of storage. All or nothing:
// an index out of range or more than a stack holds refuses the whole request.
func TakeFromStorage(storage []Stack, bring []Bring) (remaining []Stack, taken []Stack, err error) {
	want := map[int]int{}
	for _, b := range bring {
		if b.Index < 0 || b.Index >= len(storage) || b.Count <= 0 {
			return storage, nil, fmt.Errorf("no storage stack %d", b.Index)
		}
		want[b.Index] += b.Count
	}
	for index, count := range want {
		if count > storage[index].Count {
			return storage, nil, fmt.Errorf("storage stack %d holds %d, not %d", index, storage[index].Count, count)
		}
	}
	for index, stack := range storage {
		count := want[index]
		if count > 0 {
			piece := stack
			piece.Count = count
			taken = append(taken, piece)
			stack.Count -= count
		}
		if stack.Count > 0 {
			remaining = append(remaining, stack)
		}
	}
	return remaining, taken, nil
}
