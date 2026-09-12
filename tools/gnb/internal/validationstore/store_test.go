package validationstore

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestStoreKeepsRunsAndAtomicallyUpdatesMetadata(t *testing.T) {
	store := NewAt(t.TempDir())
	run, err := store.Create(Record{Name: "smoke", Kind: "validate", Status: StatusRunning}, []byte(`{"name":"smoke"}`))
	if err != nil {
		t.Fatal(err)
	}
	if run.RunID == "" || !strings.Contains(run.RunID, "-") {
		t.Fatalf("run id = %q", run.RunID)
	}
	if err := store.Update(run.RunID, func(r *Record) error {
		r.Status = StatusFailed
		r.Phase = "step"
		r.Error = "assertion failed"
		return nil
	}); err != nil {
		t.Fatal(err)
	}
	got, err := store.Load(run.RunID)
	if err != nil {
		t.Fatal(err)
	}
	if got.Status != StatusFailed || got.Phase != "step" {
		t.Fatalf("updated run = %+v", got)
	}
	if _, err := os.Stat(filepath.Join(store.Root, run.RunID, "script.json")); err != nil {
		t.Fatal(err)
	}
}

func TestStoreReviewDoesNotChangeExecutionState(t *testing.T) {
	store := NewAt(t.TempDir())
	run, err := store.Create(Record{Name: "shot", Kind: "shot", Status: StatusPassed}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := store.WriteReview(run.RunID, Review{Status: ReviewIssue, Message: "画面有问题"}); err != nil {
		t.Fatal(err)
	}
	got, err := store.Load(run.RunID)
	if err != nil {
		t.Fatal(err)
	}
	if got.Status != StatusPassed {
		t.Fatalf("review changed machine status: %+v", got)
	}
	review, err := store.Review(run.RunID)
	if err != nil || review.Status != ReviewIssue {
		t.Fatalf("review = %+v, err=%v", review, err)
	}
}

func TestStoreListIncludesUnreadableRunWithoutFailing(t *testing.T) {
	store := NewAt(t.TempDir())
	if err := os.MkdirAll(filepath.Join(store.Root, "20260101-bad", "screenshots"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(store.Root, "20260101-bad", "run.json"), []byte("{"), 0o644); err != nil {
		t.Fatal(err)
	}
	runs, err := store.List(Filter{})
	if err != nil {
		t.Fatal(err)
	}
	if len(runs) != 1 || runs[0].Status != StatusUnreadable {
		t.Fatalf("runs = %+v", runs)
	}
}

func TestArtifactPathRejectsTraversalAndEscapingSymlink(t *testing.T) {
	store := NewAt(t.TempDir())
	run, err := store.Create(Record{Name: "shot", Kind: "shot"}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := store.ArtifactPath(run.RunID, "../run.json"); err == nil {
		t.Fatal("traversal was accepted")
	}
	outside := filepath.Join(t.TempDir(), "outside.txt")
	if err := os.WriteFile(outside, []byte("secret"), 0o644); err != nil {
		t.Fatal(err)
	}
	link := filepath.Join(store.Root, run.RunID, "screenshots", "outside.txt")
	if err := os.Symlink(outside, link); err != nil {
		t.Skipf("symlink unavailable: %v", err)
	}
	if _, err := store.ArtifactPath(run.RunID, "screenshots/outside.txt"); err == nil {
		t.Fatal("escaping symlink was accepted")
	}
}

func TestStoreListSortsNewestFirstAndSupportsPaging(t *testing.T) {
	store := NewAt(t.TempDir())
	first, err := store.Create(Record{Name: "first", StartedAt: time.Now().Add(-time.Hour)}, nil)
	if err != nil {
		t.Fatal(err)
	}
	second, err := store.Create(Record{Name: "second", StartedAt: time.Now()}, nil)
	if err != nil {
		t.Fatal(err)
	}
	runs, err := store.List(Filter{Limit: 1})
	if err != nil || len(runs) != 1 || runs[0].RunID != second.RunID {
		t.Fatalf("runs = %+v, err=%v", runs, err)
	}
	runs, err = store.List(Filter{Offset: 1})
	if err != nil || len(runs) != 1 || runs[0].RunID != first.RunID {
		t.Fatalf("paged runs = %+v, err=%v", runs, err)
	}
}
