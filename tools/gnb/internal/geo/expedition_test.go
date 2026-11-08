package geo

import (
	"fmt"
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

func TestExpeditionWaterMaterialArgument(t *testing.T) {
	if got := expeditionWaterArg(ExpeditionRecipe{Gameplay: &ExpeditionGameplay{}}); got != "" {
		t.Fatalf("recipes without water must keep the legacy call, got %q", got)
	}
	glossy := ExpeditionRecipe{Gameplay: &ExpeditionGameplay{Water: &ExpeditionWater{Roughness: 0.06}}}
	if got := expeditionWaterArg(glossy); got != ",water=[undef,0.06,0]" {
		t.Fatalf("palette colour must stay undef, got %q", got)
	}
	tinted := ExpeditionRecipe{Gameplay: &ExpeditionGameplay{Water: &ExpeditionWater{
		Color: []float64{0.07, 0.11, 1.4}, Roughness: 0.08, Metalness: 0.1}}}
	if got := expeditionWaterArg(tinted); got != ",water=[[0.07,0.11,1],0.08,0.1]" {
		t.Fatalf("colour must be clamped and emitted, got %q", got)
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
	if !strings.Contains(string(before), "nd_pcg_terrain(PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,7,[])") || !strings.Contains(string(before), "nd_pcg_vegetation(") || report.Trees != 200 || report.Shrubs != 80 {
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

func TestExpeditionSettlementsAndLandCover(t *testing.T) {
	ir := IR{SizeM: 1000, Terrain: &TerrainMeta{}}
	grid := NewHeightGrid(11, 11, -500, -500, 100, 100)
	for i := range grid.Values {
		grid.Values[i] = 20 + float64(i%11)
	}
	const source = `PCG_SITES=[["green","Village green","naves",[0,0],[120,110],0,12,1],
 ["town","Square","square",[-250,250],[120,80],10,20,2]];
PCG_SPAWNS=[[-400,-400],[400,400],[-400,400]];
PCG_EXTRACTIONS=[[-300,-100],[300,0]];
PCG_PATHS=[]; PCG_VEGETATION=[]; PCG_CLEAR=[];
PCG_LANDCOVER=[["woods","spruce",[[200,-400],[450,-400],[450,-150],[200,-150]]],["fields","field",[[-450,-300],[-200,-300],[-200,-150]]]];
PCG_ALLEYS=[[[[-200,-80],[200,-80]],"fruit",11,6]];`
	path := filepath.Join(t.TempDir(), "layout.scad")
	if err := os.WriteFile(path, []byte(source), 0644); err != nil {
		t.Fatal(err)
	}
	report, err := readExpeditionControls(path, &ir, grid, 32)
	if err != nil || report.Buildings != 32 || len(report.Landcover) != 2 || report.Alleys != 1 || report.Landcover[0].AreaM2 != 62500 {
		t.Fatalf("settlement controls rejected: %+v %v", report, err)
	}
	for _, edit := range []string{
		strings.Replace(source, "[120,110],0,12", "[80,110],0,12", 1), // farmsteads do not fit along the green
		strings.Replace(source, "\"square\",[-250,250],[120,80],10,20", "\"square\",[-250,250],[120,80],10,40", 1),
		strings.Replace(source, "\"spruce\"", "\"jungle\"", 1),
	} {
		if err = os.WriteFile(path, []byte(edit), 0644); err != nil {
			t.Fatal(err)
		}
		if _, err = readExpeditionControls(path, &ir, grid, 32); err == nil {
			t.Fatal("invalid settlement or land cover accepted")
		}
	}
}

func TestExpeditionHokkaidoSettlementBounds(t *testing.T) {
	ir := IR{SizeM: 1000, Terrain: &TerrainMeta{}}
	grid := NewHeightGrid(11, 11, -500, -500, 100, 100)
	for i := range grid.Values {
		grid.Values[i] = 20
	}
	for _, kind := range []string{"hokkaido_lane", "shotengai", "canal", "portyard", "shrine"} {
		t.Run(kind, func(t *testing.T) {
			rule := settlementRules[kind]
			n := rule.max
			w, d := rule.size(n)
			source := fmt.Sprintf(`PCG_SITES=[["s","Hokkaido","%s",[0,0],[%g,%g],0,%d,2]];
PCG_SPAWNS=[[-400,-400],[400,400],[-400,400]];
PCG_EXTRACTIONS=[[-300,0],[300,0]];
PCG_PATHS=[]; PCG_VEGETATION=[]; PCG_CLEAR=[];`, kind, w, d, n)
			path := filepath.Join(t.TempDir(), "layout.scad")
			if err := os.WriteFile(path, []byte(source), 0644); err != nil {
				t.Fatal(err)
			}
			if _, err := readExpeditionControls(path, &ir, grid, n); err != nil {
				t.Fatal(err)
			}
			undersized := strings.Replace(source, fmt.Sprintf("[%g,%g]", w, d), fmt.Sprintf("[%g,%g]", w-1, d), 1)
			if err := os.WriteFile(path, []byte(undersized), 0644); err != nil {
				t.Fatal(err)
			}
			if _, err := readExpeditionControls(path, &ir, grid, n); err == nil {
				t.Fatal("undersized regional plots accepted")
			}
		})
	}
}

func TestExpeditionOSMBridgeReachesDryAbutments(t *testing.T) {
	ir := IR{SizeM: 1000, Terrain: &TerrainMeta{HasWater: true, WaterLevel: 10}}
	grid := NewHeightGrid(101, 3, -50, -1, 1, 1)
	for y := 0; y < grid.Rows; y++ {
		for x := 0; x < grid.Cols; x++ {
			grid.Values[y*grid.Cols+x] = 12
			if math.Abs(float64(x)-50) < 20 {
				grid.Values[y*grid.Cols+x] = 7
			}
		}
	}
	a, b, err := expeditionBridgeBanks(&ir, grid, [2]float64{-12, 0}, [2]float64{12, 0})
	if err != nil || a[0] > -20 || b[0] < 20 || grid.Sample(a[0], a[1]) < 10.5 || grid.Sample(b[0], b[1]) < 10.5 {
		t.Fatalf("submerged OSM endpoints were not extended to dry banks: %v %v %v", a, b, err)
	}
	left, right, err := expeditionBridgeBanks(&ir, grid, a, b)
	if err != nil || left != a || right != b {
		t.Fatal("already dry abutments must not move")
	}
	// A flat floodplain just above the water on one side: that dry OSM endpoint
	// stays the abutment instead of failing generation.
	for y := 0; y < grid.Rows; y++ {
		for x := 0; x < grid.Cols; x++ {
			if x > 50 {
				grid.Values[y*grid.Cols+x] = 10.2
			}
		}
	}
	left, right, err = expeditionBridgeBanks(&ir, grid, [2]float64{-12, 0}, [2]float64{12, 0})
	if err != nil || left[0] > -20 || right[0] != 12 {
		t.Fatalf("a dry floodplain bank must be accepted where it is: %v %v %v", left, right, err)
	}
	for i := range grid.Values {
		grid.Values[i] = 7
	}
	if _, _, err := expeditionBridgeBanks(&ir, grid, a, b); err == nil {
		t.Fatal("an unbridgeable water span must fail generation")
	}
}
