package geo

// Gameplay-first geographic levels. DEM/routes/districts are reference data;
// hand-authored SCAD controls own encounter sites, budgets, loot and vegetation.
import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

type ExpeditionGameplay struct {
	Controls       string   `json:"controls"`
	BuildingBudget int      `json:"buildingBudget"`
	ReliefScale    float64  `json:"reliefScale"`
	RoadLimit      int      `json:"roadLimit"`
	RoadClasses    []string `json:"roadClasses,omitempty"`
	// Terrain palette; empty picks the region default (bohemia: continental).
	Palette string `json:"palette,omitempty"`
	// Water surface material; nil keeps the palette's matte water.
	Water *ExpeditionWater `json:"water,omitempty"`
	// Textured terrain look: gk_terrain(TERR, detail=true) prints ground
	// textures per biome and draws roads as smooth draped ribbons instead of
	// a staircase of road-coloured cells. Off keeps the flat-colour terrain.
	TerrainDetail bool `json:"terrainDetail,omitempty"`
}

// ExpeditionWater is the gk_material() triple for the terrain water slab:
// gk_terrain(TERR, water=[colour|undef, roughness, metalness]). An empty
// colour keeps the palette's water colour.
type ExpeditionWater struct {
	Color     []float64 `json:"color,omitempty"`
	Roughness float64   `json:"roughness"`
	Metalness float64   `json:"metalness,omitempty"`
}

type ExpeditionRecipe struct {
	ID       string              `json:"id"`
	Region   string              `json:"region"`
	Seed     int                 `json:"seed"`
	Version  int                 `json:"version"`
	Gameplay *ExpeditionGameplay `json:"gameplay"`
}
type ExpeditionSite struct {
	ID        string     `json:"id"`
	Name      string     `json:"name"`
	Kind      string     `json:"kind"`
	At        [2]float64 `json:"at"`
	Size      [2]float64 `json:"size"`
	Rotation  float64    `json:"rotation"`
	Buildings int        `json:"buildings"`
	Risk      int        `json:"risk"`
}
type ExpeditionReport struct {
	Mode        string                `json:"mode"`
	Buildings   int                   `json:"buildings"`
	Trees       int                   `json:"treeBudget"`
	Shrubs      int                   `json:"shrubBudget"`
	Roads       int                   `json:"referenceRoads"`
	Sites       []ExpeditionSite      `json:"sites"`
	Spawns      [][2]float64          `json:"spawns"`
	Extractions [][2]float64          `json:"extractions"`
	Center      [2]float64            `json:"center"`
	SizeM       float64               `json:"sizeM"`
	Datum       float64               `json:"sourceDatumM"`
	ReliefScale float64               `json:"reliefScale"`
	Bridges     int                   `json:"gameplayBridges,omitempty"`
	Landcover   []ExpeditionLandcover `json:"landcover,omitempty"`
	Alleys      int                   `json:"treeAlleys,omitempty"`
}
type ExpeditionLandcover struct {
	ID     string  `json:"id"`
	Kind   string  `json:"kind"`
	AreaM2 float64 `json:"areaM2"`
}

var expeditionID = regexp.MustCompile(`^[a-z][a-z0-9_]{2,63}$`)

func ExportExpedition(repoRoot, recipePath string, publish bool) (string, ExpeditionReport, error) {
	var recipe ExpeditionRecipe
	var report ExpeditionReport
	if err := readJSON(recipePath, &recipe); err != nil {
		return "", report, err
	}
	if !expeditionID.MatchString(recipe.ID) || (recipe.Region != "bohemia" && recipe.Region != "hokkaido" && recipe.Region != "sichuan") || recipe.Version < 1 || recipe.Gameplay == nil {
		return "", report, fmt.Errorf("geographic extraction recipes require a region and gameplay PCG controls")
	}
	gp := recipe.Gameplay
	if gp.ReliefScale <= 0 || gp.ReliefScale > 1 || gp.BuildingBudget < 1 || gp.BuildingBudget > 120 {
		return "", report, fmt.Errorf("invalid gameplay budget or relief scale")
	}
	paths := NewPaths(repoRoot, recipe.ID)
	var ir IR
	if err := readJSON(paths.PartIRPath("p0_0"), &ir); err != nil {
		return "", report, fmt.Errorf("run geo fetch/build first: %w", err)
	}
	if ir.Terrain == nil || ir.SizeM > 1000 || ir.SizeM < 100 {
		return "", report, fmt.Errorf("PCG expedition needs a built single tile")
	}
	blob, err := os.ReadFile(paths.HmapPath())
	if err != nil {
		return "", report, err
	}
	grid, err := DecodeHmap(blob)
	if err != nil {
		return "", report, err
	}
	meta := *ir.Terrain
	datum := meta.BaseElevation
	for i := range grid.Values {
		grid.Values[i] = 10 + (grid.Values[i]-datum)*gp.ReliefScale
	}
	meta.BaseElevation = 10
	meta.WaterLevel = 10 + (meta.WaterLevel-datum)*gp.ReliefScale
	meta.PaletteSpan = 160
	ir.Terrain = &meta
	assetRoot := "assets/scad/proc/nextdayz/" + recipe.ID
	output := filepath.Join(repoRoot, filepath.FromSlash(assetRoot))
	if err = os.MkdirAll(output, 0755); err != nil {
		return "", report, err
	}
	controls := filepath.Join(output, "layout.scad")
	if _, err = os.Stat(controls); os.IsNotExist(err) {
		if !strings.HasPrefix(gp.Controls, "assets/scad/") || strings.Contains(gp.Controls, "..") {
			return "", report, fmt.Errorf("invalid control source path")
		}
		data, e := os.ReadFile(filepath.Join(repoRoot, filepath.FromSlash(gp.Controls)))
		if e != nil {
			return "", report, e
		}
		if e = os.WriteFile(controls, data, 0644); e != nil {
			return "", report, e
		}
	}
	report, err = readExpeditionControls(controls, &ir, grid, gp.BuildingBudget)
	if err != nil {
		return "", report, err
	}
	report.Mode = "geographic-skeleton/gameplay-pcg"
	report.Center, report.SizeM, report.Datum, report.ReliefScale = ir.Center, ir.SizeM, datum, gp.ReliefScale
	shifted, err := EncodeHmap(grid)
	if err != nil {
		return "", report, err
	}
	if err = os.WriteFile(filepath.Join(output, "terrain.hmap"), shifted, 0644); err != nil {
		return "", report, err
	}
	opt := DefaultEmitOptions()
	opt.RoadClasses = []string{"motorway", "trunk", "primary", "secondary", "tertiary", "residential", "unclassified", "living_street", "service"}
	if len(gp.RoadClasses) > 0 {
		opt.RoadClasses = gp.RoadClasses
	}
	opt.MaxRoads = gp.RoadLimit
	if opt.MaxRoads < 1 {
		opt.MaxRoads = 48
	}
	roads := selectRoads(&ir, ir.SizeM/2, opt)
	report.Roads = len(roads)
	var out strings.Builder
	fmt.Fprintf(&out, "// Geographic reference only; gameplay lives in layout.scad.\n// %.5f, %.5f; %.0fm square. DEM relief scale %.2f.\n// © OpenStreetMap contributors (ODbL); SRTM / NASA-USGS.\nuse <../../../lib/gk_camera.scad>\nuse <../../../lib/kit_abandoned.scad>\nuse <../../../lib/nextdayz_pcg.scad>\nuse <../../../lib/nextdayz_coldwar.scad>\n$fn=12;\ninclude <layout.scad>\n", ir.Center[0], ir.Center[1], ir.SizeM, gp.ReliefScale)
	var reference strings.Builder
	// Settlements level every plot exactly; the finer mesh (3.9 m cells) keeps
	// the transition between neighbouring plots short. Legacy layouts keep 176.
	cells := 176
	for _, s := range report.Sites {
		if _, ok := settlementRules[s.Kind]; ok {
			cells = 256
		}
	}
	tile := Tile{Name: recipe.ID, SizeM: ir.SizeM, Cells: cells, Seed: recipe.Seed}
	writeTerrain(&reference, tile, assetRoot+"/terrain.hmap", roads, ir.Terrain, scope{})
	ref := strings.Replace(reference.String(), "TERR =", "GEO_TERR =", 1)
	ref = strings.Replace(ref, "\"urban\"", fmt.Sprintf("%q", expeditionPalette(recipe)), 1)
	out.WriteString(ref)
	landcover := "[]"
	if len(report.Landcover) > 0 {
		landcover = "PCG_LANDCOVER"
	}
	fmt.Fprintf(&out, "TERR=[\"gkterr1\",GEO_TERR[1],GEO_TERR[2],GEO_TERR[3],GEO_TERR[4],GEO_TERR[5],GEO_TERR[6],concat(GEO_TERR[7],PCG_PATHS,nd_pcg_terrain(PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,%d,%s))];\ngk_terrain(TERR%s);\nfunction nd_ground(x,y)=gk_terrain_height(TERR,x,y);\n", recipe.Seed, landcover, expeditionTerrainArgs(recipe))
	if err = writeExpeditionBridges(&out, &ir, grid, recipe.Seed); err != nil {
		return "", report, err
	}
	if report.Bridges > 0 {
		fmt.Fprintf(&out, "for(i=[0:len(PCG_BRIDGES)-1]) nd_pcg_bridge(TERR,PCG_BRIDGES[i][0],PCG_BRIDGES[i][1],PCG_BRIDGES[i][2],%d+i*17);\n", recipe.Seed)
	}
	fmt.Fprintf(&out, "for(i=[0:len(PCG_SITES)-1]) nd_pcg_site(TERR,PCG_SITES[i],%q,%d+i*131);\n", recipe.Region, recipe.Seed)
	out.WriteString("for(p=PCG_SPAWNS) translate([p[0],p[1],nd_ground(p[0],p[1])+0.15]) nd_spawn_player_safe();\n")
	for i, p := range report.Extractions {
		fmt.Fprintf(&out, "translate([%s,%s,nd_ground(%s,%s)]) { gk_flatten() az_prop_helipad(seed=%d); translate([0,0,0.15]) nd_extraction(id=%q,name=%q); }\n", fnum(p[0]), fnum(p[1]), fnum(p[0]), fnum(p[1]), recipe.Seed+i, fmt.Sprintf("%s_pad_%d", recipe.ID, i), []string{"Rescue zone ALPHA", "Rescue zone BRAVO"}[i])
	}
	if report.Trees+report.Shrubs > 0 {
		fmt.Fprintf(&out, "nd_pcg_vegetation(TERR,PCG_VEGETATION,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,%d,%q);\n", recipe.Seed, recipe.Region)
	}
	if len(report.Landcover) > 0 {
		fmt.Fprintf(&out, "nd_pcg_landcover(TERR,PCG_LANDCOVER,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,%d,%q);\n", recipe.Seed, recipe.Region)
	}
	if report.Alleys > 0 {
		fmt.Fprintf(&out, "nd_pcg_alleys(TERR,PCG_ALLEYS,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,%d,%q);\n", recipe.Seed, recipe.Region)
	}
	out.WriteString("gk_camera_lookat([-625,-725,520],[0,0,25],\"overview\",48);\ngk_camera_lookat([-150,-230,135],[20,85,25],\"tactical_core\",55);\n")
	if err = os.WriteFile(filepath.Join(output, "base.scad"), []byte(out.String()), 0644); err != nil {
		return "", report, err
	}
	for name, content := range map[string]string{
		"authoring.scad":   "// Preserved manual authoring parameters.\n",
		"adjustments.scad": "// Preserved gameplay additions: cover, routes, loot and encounters.\n",
		"scene.scad":       "// Gameplay-authored level over a geographic skeleton.\ninclude <authoring.scad>\ninclude <layout.scad>\ninclude <base.scad>\ninclude <adjustments.scad>\n",
	} {
		path := filepath.Join(output, name)
		if _, err = os.Stat(path); os.IsNotExist(err) {
			if err = os.WriteFile(path, []byte(content), 0644); err != nil {
				return "", report, err
			}
		}
	}
	if err = writeJSON(filepath.Join(output, "reference.json"), expeditionBuiltAreas(&ir)); err != nil {
		return "", report, err
	}
	attribution := fmt.Sprintf("# %s\n\nGeographic reference: %.5f, %.5f. © OpenStreetMap contributors (ODbL 1.0); SRTM / NASA-USGS.\n\nTerrain relief, roads and district hints are adapted for gameplay. Building counts, tactical sites, vegetation and loot are authored PCG decisions; this is not a reconstruction.\n", recipe.ID, ir.Center[0], ir.Center[1])
	if err = os.WriteFile(filepath.Join(output, "ATTRIBUTION.md"), []byte(attribution), 0644); err != nil {
		return "", report, err
	}
	if err = writeJSON(filepath.Join(output, "generation.json"), report); err != nil {
		return "", report, err
	}
	if publish {
		return publishExpedition(repoRoot, output, assetRoot, recipe, report)
	}
	return filepath.Join(output, "scene.scad"), report, nil
}

// Regional ground colours. Existing regions keep the palette their published
// maps were made with; a recipe may name any engine palette explicitly.
func expeditionPalette(recipe ExpeditionRecipe) string {
	if recipe.Gameplay != nil && recipe.Gameplay.Palette != "" {
		return recipe.Gameplay.Palette
	}
	if recipe.Region == "bohemia" {
		return "continental"
	}
	return "temperate"
}

// Optional water material argument for gk_terrain. "" keeps matte water, so
// recipes without a water block regenerate byte-identically.
func expeditionWaterArg(recipe ExpeditionRecipe) string {
	if recipe.Gameplay == nil || recipe.Gameplay.Water == nil {
		return ""
	}
	w := recipe.Gameplay.Water
	unit := func(v float64) string { return fnum(math.Max(0, math.Min(1, v))) }
	colour := "undef"
	if len(w.Color) >= 3 {
		parts := make([]string, 0, 4)
		for i := 0; i < len(w.Color) && i < 4; i++ {
			parts = append(parts, unit(w.Color[i]))
		}
		colour = "[" + strings.Join(parts, ",") + "]"
	}
	return fmt.Sprintf(",water=[%s,%s,%s]", colour, unit(w.Roughness), unit(w.Metalness))
}

// Optional terrain look argument for gk_terrain; "" keeps the flat-colour
// terrain, so recipes without terrainDetail regenerate byte-identically.
func expeditionTerrainArgs(recipe ExpeditionRecipe) string {
	args := expeditionWaterArg(recipe)
	if recipe.Gameplay != nil && recipe.Gameplay.TerrainDetail {
		args += ",detail=true"
	}
	return args
}

// OSM abutments can lie in a DEM shoreline cell. Extend along the bridge axis
// to dry ground instead of sampling a submerged deck datum from those cells.
// An endpoint that is already dry stays where OSM put it. A submerged one
// moves to a bank 0.5 m above the water, or, on a flat floodplain that never
// rises that far (Loket's Ohře meadows sit 0.16 m above it at relief 0.4),
// to the first point that is dry. Only water on a whole side fails.
func expeditionBridgeBanks(ir *IR, grid *HeightGrid, a, b [2]float64) ([2]float64, [2]float64, error) {
	if !ir.Terrain.HasWater {
		return a, b, nil
	}
	length := math.Hypot(b[0]-a[0], b[1]-a[1])
	dir := [2]float64{(b[0] - a[0]) / length, (b[1] - a[1]) / length}
	bank := func(p [2]float64, side float64) ([2]float64, bool) {
		if grid.Sample(p[0], p[1]) >= ir.Terrain.WaterLevel+0.1 {
			return p, true
		}
		low, haveLow := p, false
		for distance := 2.0; distance <= 64; distance += 2 {
			q := [2]float64{p[0] + side*dir[0]*distance, p[1] + side*dir[1]*distance}
			if math.Abs(q[0]) > ir.SizeM/2-8 || math.Abs(q[1]) > ir.SizeM/2-8 {
				break
			}
			h := grid.Sample(q[0], q[1])
			if h >= ir.Terrain.WaterLevel+0.5 {
				return q, true
			}
			if !haveLow && h >= ir.Terrain.WaterLevel+0.1 {
				low, haveLow = q, true
			}
		}
		return low, haveLow
	}
	left, okLeft := bank(a, -1)
	right, okRight := bank(b, 1)
	if !okLeft || !okRight {
		return a, b, fmt.Errorf("OSM bridge %v to %v has no dry abutments within 64m", a, b)
	}
	return left, right, nil
}

func writeExpeditionBridges(out *strings.Builder, ir *IR, grid *HeightGrid, seed int) error {
	roads := append([]Road(nil), ir.Roads...)
	sort.SliceStable(roads, func(i, j int) bool { return roads[i].Width > roads[j].Width })
	var centers [][2]float64
	for _, r := range roads {
		if !r.Bridge || len(r.Pts) < 2 {
			continue
		}
		a, b := r.Pts[0], r.Pts[len(r.Pts)-1]
		length := math.Hypot(b[0]-a[0], b[1]-a[1])
		half := ir.SizeM/2 - 8
		if length < 12 || math.Abs(a[0]) > half || math.Abs(a[1]) > half || math.Abs(b[0]) > half || math.Abs(b[1]) > half {
			continue
		}
		var err error
		a, b, err = expeditionBridgeBanks(ir, grid, a, b)
		if err != nil {
			return err
		}
		mid := [2]float64{(a[0] + b[0]) / 2, (a[1] + b[1]) / 2}
		length = math.Hypot(b[0]-a[0], b[1]-a[1])
		duplicate := false
		for _, c := range centers {
			if math.Hypot(mid[0]-c[0], mid[1]-c[1]) < 18 {
				duplicate = true
			}
		}
		if duplicate {
			continue
		}
		centers = append(centers, mid)
		width := math.Max(3, r.Width)
		angle := math.Atan2(b[1]-a[1], b[0]-a[0]) * 180 / math.Pi
		fmt.Fprintf(out, "let(za=nd_ground(%s,%s)+0.06,zb=nd_ground(%s,%s)+0.06) translate([%s,%s,(za+zb)/2]) rotate([0,0,%s]) gk_flatten() az_prop_bridge(seed=%d,L=%s,W=%s,rise=zb-za,pierDepth=max(10,(za+zb)/2-%s+8));\n", fnum(a[0]), fnum(a[1]), fnum(b[0]), fnum(b[1]), fnum(mid[0]), fnum(mid[1]), fnum(angle), seed+len(centers), fnum(length+3), fnum(width), fnum(ir.Terrain.WaterLevel))
	}
	return nil
}

func publishExpedition(repoRoot, output, assetRoot string, recipe ExpeditionRecipe, report ExpeditionReport) (string, ExpeditionReport, error) {
	publishedRef := fmt.Sprintf("%s/published/v%d", assetRoot, recipe.Version)
	destination := filepath.Join(repoRoot, filepath.FromSlash(publishedRef))
	// Published versions are immutable. A new release needs a new version number.
	if _, err := os.Stat(destination); err == nil {
		return "", report, fmt.Errorf("published v%d already exists; increment recipe version", recipe.Version)
	}
	if err := os.MkdirAll(filepath.Join(destination, "lib"), 0755); err != nil {
		return "", report, err
	}
	for _, name := range []string{"scene.scad", "base.scad", "authoring.scad", "adjustments.scad", "terrain.hmap", "layout.scad", "reference.json", "ATTRIBUTION.md", "generation.json"} {
		data, err := os.ReadFile(filepath.Join(output, name))
		if err != nil {
			return "", report, err
		}
		if strings.HasSuffix(name, ".scad") {
			text := strings.ReplaceAll(string(data), "../../../lib/", "lib/")
			text = strings.ReplaceAll(text, assetRoot+"/terrain.hmap", publishedRef+"/terrain.hmap")
			data = []byte(text)
		}
		if err = os.WriteFile(filepath.Join(destination, name), data, 0644); err != nil {
			return "", report, err
		}
	}
	for _, name := range []string{"nextdayz_pcg.scad", "nextdayz_settlement.scad", "nextdayz_hokkaido.scad", "nextdayz_coldwar.scad", "kit_abandoned.scad", "kit_bohemia.scad", "kit_hokkaido.scad", "kit_coldwar.scad", "nextdayz_geography.scad", "kit_road.scad", "gk_camera.scad", "kit_layout.scad", "kit_terrain.scad"} {
		data, err := os.ReadFile(filepath.Join(repoRoot, "assets/scad/lib", name))
		if err != nil {
			return "", report, err
		}
		if err = os.WriteFile(filepath.Join(destination, "lib", name), data, 0644); err != nil {
			return "", report, err
		}
	}
	registryPath := filepath.Join(repoRoot, "assets/configs/nextdayz_maps.json")
	var registry map[string]json.RawMessage
	if err := readJSON(registryPath, &registry); err != nil {
		return "", report, err
	}
	var entries []map[string]interface{}
	if err := json.Unmarshal(registry["maps"], &entries); err != nil {
		return "", report, err
	}
	found := false
	for _, entry := range entries {
		if entry["id"] == recipe.ID {
			entry["draftScene"] = assetRoot + "/scene.scad"
			entry["scene"] = publishedRef + "/scene.scad"
			entry["mapVersion"] = recipe.Version
			entry["status"] = "published"
			found = true
		}
	}
	if !found {
		return "", report, fmt.Errorf("map is missing from nextdayz_maps.json")
	}
	registry["maps"], _ = json.Marshal(entries)
	if err := writeJSON(registryPath, registry); err != nil {
		return "", report, err
	}
	return filepath.Join(destination, "scene.scad"), report, nil
}
