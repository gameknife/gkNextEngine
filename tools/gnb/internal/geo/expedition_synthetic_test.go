package geo

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// A level for no real place exports without any geocache: the recipe's
// synthetic ground, the regular controls, pads and the training site kinds.
func TestExpeditionSyntheticTrainingGround(t *testing.T) {
	root := t.TempDir()
	controls := `PCG_SITES = [["armory","Armory","tg_armory",[0,0],[60,48],0,2,1],["range","Range","tg_range",[150,100],[110,380],0,3,1]];
PCG_SPAWNS = [[-250,-300],[-200,-300],[-300,-250]];
PCG_EXTRACTIONS = [[-250,250],[250,-250]];
PCG_VEGETATION = [];
PCG_PATHS = [["road",[[0,-390],[0,-30]],6]];
PCG_CLEAR = [];
`
	controlPath := filepath.Join(root, "assets/scad/training_controls.scad")
	if err := os.MkdirAll(filepath.Dir(controlPath), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(controlPath, []byte(controls), 0644); err != nil {
		t.Fatal(err)
	}
	recipe := ExpeditionRecipe{ID: "test_training", Region: "training", Seed: 3, Version: 1,
		Synthetic: &ExpeditionSynthetic{SizeM: 800, Base: 40, Relief: 6,
			Flat:  [][4]float64{{0, 0, 120, 40}, {150, 100, 200, 40}},
			Hills: [][6]float64{{150, 360, 120, 40, 0, 20}}},
		Gameplay: &ExpeditionGameplay{Controls: "assets/scad/training_controls.scad", BuildingBudget: 5, ReliefScale: 1, RoadLimit: 1}}
	recipePath := filepath.Join(root, "recipe.json")
	if err := writeJSON(recipePath, recipe); err != nil {
		t.Fatal(err)
	}
	scene, report, err := ExportExpedition(root, recipePath, false)
	if err != nil {
		t.Fatal(err)
	}
	if report.Mode != "synthetic-terrain/gameplay-pcg" || report.SizeM != 800 || report.Buildings != 5 || report.Roads != 0 {
		t.Fatalf("bad synthetic report: %+v", report)
	}
	base, err := os.ReadFile(filepath.Join(filepath.Dir(scene), "base.scad"))
	if err != nil {
		t.Fatal(err)
	}
	text := string(base)
	if !strings.Contains(text, "Synthetic ground") || strings.Contains(text, "OpenStreetMap") || !strings.Contains(text, "[800, 800], [256, 256]") {
		t.Fatalf("synthetic base must carry no geographic attribution and a fine settlement mesh:\n%s", text[:400])
	}
	blob, err := os.ReadFile(filepath.Join(filepath.Dir(scene), "terrain.hmap"))
	if err != nil {
		t.Fatal(err)
	}
	grid, err := DecodeHmap(blob)
	if err != nil {
		t.Fatal(err)
	}
	if h := grid.Sample(0, 0); h < 9.9 || h > 10.1 {
		t.Fatalf("plateau must sit exactly at the datum, got %.2f", h)
	}
	if grid.Sample(150, 360) < 25 {
		t.Fatalf("authored backstop hill missing: %.2f", grid.Sample(150, 360))
	}
	attribution, _ := os.ReadFile(filepath.Join(filepath.Dir(scene), "ATTRIBUTION.md"))
	if !strings.Contains(string(attribution), "no geographic or third-party map data") {
		t.Fatal("synthetic attribution must not claim OSM/SRTM sources")
	}
	// The training kinds keep their own size rules.
	bad := strings.Replace(controls, `[60,48],0,2,1]`, `[44,43],0,2,1]`, 1)
	if err = os.WriteFile(filepath.Join(filepath.Dir(scene), "layout.scad"), []byte(bad), 0644); err != nil {
		t.Fatal(err)
	}
	if _, _, err = ExportExpedition(root, recipePath, false); err == nil || !strings.Contains(err.Error(), "tg_armory") {
		t.Fatalf("undersized training site must be rejected, got %v", err)
	}
}
