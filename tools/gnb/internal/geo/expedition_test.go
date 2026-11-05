package geo

import (
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestLongRoadJunctionUsesLastDensifiedStation(t *testing.T) {
	roads := []Road{
		{ID: 1, Width: 7, Class: "residential", Pts: [][2]float64{{-400, 0}, {0, 0}, {400, 0}}, Nodes: []int64{1, 2, 3}},
		{ID: 2, Width: 7, Class: "residential", Pts: [][2]float64{{0, 0}, {0, 100}}, Nodes: []int64{2, 4}},
	}
	_, junctions := BuildRoadNetwork(roads, 500, DefaultEmitOptions())
	if len(junctions) != 1 {
		t.Fatalf("want one junction, got %d", len(junctions))
	}
	for _, p := range junctions[0].Ring {
		if math.Hypot(p[0], p[1]) > 15 {
			t.Fatalf("junction stretched far from real intersection: %v", p)
		}
	}
}

func TestExpeditionRegenerationPreservesAuthoringAndRegionalKits(t *testing.T) {
	root := t.TempDir()
	paths := NewPaths(root, "test_region")
	if err := os.MkdirAll(filepath.Dir(paths.PartIRPath("p0_0")), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(paths.AssetDir(), 0755); err != nil {
		t.Fatal(err)
	}
	ir := IR{Tile: "test_region", Center: [2]float64{50, 12}, SizeM: 1000,
		Terrain: &TerrainMeta{BaseElevation: 50}, Buildings: []Building{
			{ID: 71, AreaM2: 120, Outer: Ring{{40, 40}, {52, 40}, {52, 50}, {40, 50}}},
		}, Roads: []Road{{ID: 1, Width: 7, Class: "residential", Pts: [][2]float64{{-480, 0}, {480, 0}}, Nodes: []int64{1, 2}},
			{ID: 2, Width: 3, Class: "cycleway", Pts: [][2]float64{{-400, 300}, {400, 300}}, Nodes: []int64{3, 4}}},
		Landuse: []Area{
			{ID: 21, Tag: "leisure=park", Outer: Ring{{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}},
			{ID: 22, Tag: "leisure=park", Outer: Ring{{290, -10}, {310, -10}, {310, 10}, {290, 10}}},
		}}
	if err := writeJSON(paths.PartIRPath("p0_0"), ir); err != nil {
		t.Fatal(err)
	}
	grid := NewHeightGrid(177, 177, -500, -500, 1000.0/176, 1000.0/176)
	for i := range grid.Values {
		grid.Values[i] = 50
	}
	blob, err := EncodeHmap(grid)
	if err != nil {
		t.Fatal(err)
	}
	if err = os.WriteFile(paths.HmapPath(), blob, 0644); err != nil {
		t.Fatal(err)
	}
	recipePath := filepath.Join(root, "recipe.json")
	controlPath := filepath.Join(root, "assets/scad/test_controls.scad")
	const controls = `PCG_SITES = [["market","Market","market",[0,0],[80,60],0,6,2]];
PCG_SPAWNS = [[-400,-400],[400,400],[-400,400]];
PCG_EXTRACTIONS = [[-300,0],[300,0]];
PCG_VEGETATION = [["woods",[-480,-480,480,480],200,30,[1,2],"tree"],["grass",[-400,-400,400,400],80,30,[1,1],"shrub"]];
PCG_PATHS = [["road",[[-400,0],[400,0]],6]];
PCG_CLEAR = [];
`
	if err = os.MkdirAll(filepath.Dir(controlPath), 0755); err != nil {
		t.Fatal(err)
	}
	if err = os.WriteFile(controlPath, []byte(controls), 0644); err != nil {
		t.Fatal(err)
	}
	if err = writeJSON(recipePath, ExpeditionRecipe{ID: "test_region", Region: "sichuan", Seed: 7, Version: 1,
		Gameplay: &ExpeditionGameplay{Controls: "assets/scad/test_controls.scad", BuildingBudget: 6, ReliefScale: 0.4, RoadLimit: 48, RoadClasses: []string{"cycleway"}}}); err != nil {
		t.Fatal(err)
	}
	scene, report, err := ExportExpedition(root, recipePath, false)
	if err != nil {
		t.Fatal(err)
	}
	if report.Buildings != 6 || len(report.Sites) != 1 || len(report.Extractions) != 2 || len(report.Spawns) < 3 {
		t.Fatalf("bad output: %+v", report)
	}
	basePath := filepath.Join(filepath.Dir(scene), "base.scad")
	before, err := os.ReadFile(basePath)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(before), "nd_pcg_site(") || strings.Contains(string(before), "nd_geo_building(") || strings.Contains(string(before), "linear_extrude(height") {
		t.Fatal("gameplay sites must assemble kits independently of individual OSM footprints")
	}
	if strings.Contains(string(before), "rd_street") || strings.Contains(string(before), "kit_road.scad") || !strings.Contains(string(before), "[\"road\",") {
		t.Fatal("ordinary OSM roads must be terrain operators, never solid road geometry")
	}
	if report.Roads != 1 || !strings.Contains(string(before), "[-400, 300]") || strings.Contains(string(before), "[-480, 0]") {
		t.Fatal("park recipes must select cycling/foot routes independently of road-only defaults")
	}
	if !strings.Contains(string(before), "nd_pcg_pads(") || !strings.Contains(string(before), "nd_pcg_vegetation(") || report.Trees != 200 || report.Shrubs != 80 {
		t.Fatal("SCAD controls must own shared earthworks and vegetation budgets")
	}
	authoring := filepath.Join(filepath.Dir(scene), "adjustments.scad")
	const edit = "// hand-authored cover and route correction\ntranslate([10,20,0]) cube([1,2,3]);\n"
	if err = os.WriteFile(authoring, []byte(edit), 0644); err != nil {
		t.Fatal(err)
	}
	layout := filepath.Join(filepath.Dir(scene), "layout.scad")
	const layoutEdit = controls + "\n// Manual tactical layout preserved.\n"
	if err = os.WriteFile(layout, []byte(layoutEdit), 0644); err != nil {
		t.Fatal(err)
	}
	// More OSM footprints can change the district reference, never the gameplay budget.
	ir.Buildings = append(ir.Buildings, ir.Buildings...)
	if err = writeJSON(paths.PartIRPath("p0_0"), ir); err != nil {
		t.Fatal(err)
	}
	if _, _, err = ExportExpedition(root, recipePath, false); err != nil {
		t.Fatal(err)
	}
	after, _ := os.ReadFile(basePath)
	edited, _ := os.ReadFile(authoring)
	if string(before) != string(after) {
		t.Fatal("same cached geography/recipe must regenerate identically")
	}
	if string(edited) != edit {
		t.Fatal("regeneration overwrote manual work")
	}
	preserved, _ := os.ReadFile(layout)
	if string(preserved) != layoutEdit {
		t.Fatal("regeneration overwrote PCG controls")
	}
}

func TestExpeditionPadsRejectWaterAndExcessiveEarthworks(t *testing.T) {
	ir := IR{SizeM: 1000, Terrain: &TerrainMeta{HasWater: true, WaterLevel: 10}}
	g := NewHeightGrid(11, 11, -500, -500, 100, 100)
	for i := range g.Values {
		g.Values[i] = 9
	}
	if validateExpeditionPad(&ir, g, [2]float64{}, [2]float64{80, 60}, 0) == nil {
		t.Fatal("underwater encounter accepted")
	}
	for y := 0; y < 11; y++ {
		for x := 0; x < 11; x++ {
			g.Values[y*11+x] = 30 + float64(x)*40
		}
	}
	if validateExpeditionPad(&ir, g, [2]float64{}, [2]float64{80, 60}, 0) == nil {
		t.Fatal("excessive hillside cut accepted")
	}
}

func TestExpeditionUrbanControlsReserveThroughStreets(t *testing.T) {
	ir := IR{SizeM: 1000, Terrain: &TerrainMeta{}}
	grid := NewHeightGrid(11, 11, -500, -500, 100, 100)
	for i := range grid.Values {
		grid.Values[i] = 20
	}
	const source = `PCG_SITES=[["blocks","Residential streets","urban",[0,0],[200,160],0,24,1]];
PCG_SPAWNS=[[-400,-400],[400,400],[-400,400]];
PCG_EXTRACTIONS=[[-300,0],[300,0]];
PCG_PATHS=[]; PCG_VEGETATION=[]; PCG_CLEAR=[];`
	path := filepath.Join(t.TempDir(), "layout.scad")
	if err := os.WriteFile(path, []byte(source), 0644); err != nil {
		t.Fatal(err)
	}
	report, err := readExpeditionControls(path, &ir, grid, 24)
	if err != nil || report.Buildings != 24 {
		t.Fatalf("larger city block rejected: %+v %v", report, err)
	}
	for _, edit := range []string{strings.Replace(source, "[200,160]", "[120,160]", 1), strings.Replace(source, "[200,160]", "[200,80]", 1)} {
		if err = os.WriteFile(path, []byte(edit), 0644); err != nil {
			t.Fatal(err)
		}
		if _, err = readExpeditionControls(path, &ir, grid, 24); err == nil {
			t.Fatal("crowded urban layout accepted without through-street space")
		}
	}
}

func TestExpeditionBridgesRequireDryBanks(t *testing.T) {
	ir := IR{SizeM: 1000, Terrain: &TerrainMeta{HasWater: true, WaterLevel: 10}}
	g := NewHeightGrid(11, 11, -500, -500, 100, 100)
	for i := range g.Values {
		g.Values[i] = 20
	}
	const source = `PCG_SITES=[["rest","Rest stop","park",[0,0],[80,60],0,4,1]];
PCG_SPAWNS=[[-400,-400],[400,400],[-400,400]];
PCG_EXTRACTIONS=[[-300,0],[300,0]];
PCG_VEGETATION=[["bamboo",[-480,-480,480,480],120,28,[1,2],"bamboo"]];
PCG_PATHS=[];PCG_CLEAR=[];
PCG_BRIDGES=[[[-200,200],[200,200],4]];`
	path := filepath.Join(t.TempDir(), "layout.scad")
	if err := os.WriteFile(path, []byte(source), 0644); err != nil {
		t.Fatal(err)
	}
	report, err := readExpeditionControls(path, &ir, g, 4)
	if err != nil || report.Bridges != 1 || report.Trees != 120 {
		t.Fatalf("valid park controls rejected: %+v %v", report, err)
	}
	g.Values[7*11+7] = 9 // east bridge bank only; no other pad depends on it
	if _, err = readExpeditionControls(path, &ir, g, 4); err == nil {
		t.Fatal("bridge ending underwater accepted")
	}
}
