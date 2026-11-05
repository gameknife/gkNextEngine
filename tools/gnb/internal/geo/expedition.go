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
	Mode        string           `json:"mode"`
	Buildings   int              `json:"buildings"`
	Trees       int              `json:"treeBudget"`
	Shrubs      int              `json:"shrubBudget"`
	Roads       int              `json:"referenceRoads"`
	Sites       []ExpeditionSite `json:"sites"`
	Spawns      [][2]float64     `json:"spawns"`
	Extractions [][2]float64     `json:"extractions"`
	Center      [2]float64       `json:"center"`
	SizeM       float64          `json:"sizeM"`
	Datum       float64          `json:"sourceDatumM"`
	ReliefScale float64          `json:"reliefScale"`
	Bridges     int              `json:"gameplayBridges,omitempty"`
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
	tile := Tile{Name: recipe.ID, SizeM: ir.SizeM, Cells: 176, Seed: recipe.Seed}
	writeTerrain(&reference, tile, assetRoot+"/terrain.hmap", roads, ir.Terrain, scope{})
	ref := strings.Replace(reference.String(), "TERR =", "GEO_TERR =", 1)
	ref = strings.Replace(ref, "\"urban\"", "\"temperate\"", 1)
	out.WriteString(ref)
	out.WriteString("TERR=[\"gkterr1\",GEO_TERR[1],GEO_TERR[2],GEO_TERR[3],GEO_TERR[4],GEO_TERR[5],GEO_TERR[6],concat(GEO_TERR[7],PCG_PATHS,nd_pcg_pads(PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS))];\ngk_terrain(TERR);\nfunction nd_ground(x,y)=gk_terrain_height(TERR,x,y);\n")
	writeExpeditionBridges(&out, &ir, recipe.Seed)
	if report.Bridges > 0 {
		fmt.Fprintf(&out, "for(i=[0:len(PCG_BRIDGES)-1]) nd_pcg_bridge(TERR,PCG_BRIDGES[i][0],PCG_BRIDGES[i][1],PCG_BRIDGES[i][2],%d+i*17);\n", recipe.Seed)
	}
	fmt.Fprintf(&out, "for(i=[0:len(PCG_SITES)-1]) nd_pcg_site(TERR,PCG_SITES[i],%q,%d+i*131);\n", recipe.Region, recipe.Seed)
	out.WriteString("for(p=PCG_SPAWNS) translate([p[0],p[1],nd_ground(p[0],p[1])+0.15]) nd_spawn_player_safe();\n")
	for i, p := range report.Extractions {
		fmt.Fprintf(&out, "translate([%s,%s,nd_ground(%s,%s)]) { gk_flatten() az_prop_helipad(seed=%d); translate([0,0,0.15]) nd_extraction(id=%q,name=%q); }\n", fnum(p[0]), fnum(p[1]), fnum(p[0]), fnum(p[1]), recipe.Seed+i, fmt.Sprintf("%s_pad_%d", recipe.ID, i), []string{"Rescue zone ALPHA", "Rescue zone BRAVO"}[i])
	}
	fmt.Fprintf(&out, "nd_pcg_vegetation(TERR,PCG_VEGETATION,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,%d,%q);\n", recipe.Seed, recipe.Region)
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

func writeExpeditionBridges(out *strings.Builder, ir *IR, seed int) {
	roads := append([]Road(nil), ir.Roads...)
	sort.SliceStable(roads, func(i, j int) bool { return roads[i].Width > roads[j].Width })
	var centers [][2]float64
	for _, r := range roads {
		if !r.Bridge || len(r.Pts) < 2 {
			continue
		}
		a, b := r.Pts[0], r.Pts[len(r.Pts)-1]
		mid := [2]float64{(a[0] + b[0]) / 2, (a[1] + b[1]) / 2}
		length := math.Hypot(b[0]-a[0], b[1]-a[1])
		half := ir.SizeM/2 - 8
		if length < 12 || math.Abs(a[0]) > half || math.Abs(a[1]) > half || math.Abs(b[0]) > half || math.Abs(b[1]) > half {
			continue
		}
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
	for _, name := range []string{"nextdayz_pcg.scad", "nextdayz_coldwar.scad", "kit_abandoned.scad", "kit_coldwar.scad", "nextdayz_geography.scad", "kit_road.scad", "gk_camera.scad", "kit_layout.scad", "kit_terrain.scad"} {
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
