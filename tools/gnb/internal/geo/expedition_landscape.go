package geo

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"regexp"
	"sort"
)

// Layouts use literal SCAD arrays as their editable PCG control protocol. Their
// contents are JSON-compatible, while the actual assembly/scatter stays in SCAD.
func expeditionControlArray(source, name string, target interface{}) error {
	clean := regexp.MustCompile(`(?m)//[^\n]*`).ReplaceAllString(source, "")
	pattern := regexp.MustCompile(`(?s)\b` + regexp.QuoteMeta(name) + `\s*=\s*(\[.*?\])\s*;`)
	found := pattern.FindStringSubmatch(clean)
	if len(found) != 2 {
		return fmt.Errorf("layout needs a literal %s control array", name)
	}
	if err := json.Unmarshal([]byte(found[1]), target); err != nil {
		return fmt.Errorf("%s: %w", name, err)
	}
	return nil
}

func readExpeditionControls(path string, ir *IR, grid *HeightGrid, budget int) (ExpeditionReport, error) {
	var report ExpeditionReport
	text, err := os.ReadFile(path)
	if err != nil {
		return report, err
	}
	var rows [][]json.RawMessage
	if err = expeditionControlArray(string(text), "PCG_SITES", &rows); err != nil {
		return report, err
	}
	ids := map[string]bool{}
	kinds := map[string]bool{"castle": true, "village": true, "market": true, "medical": true, "industry": true, "military": true, "checkpoint": true, "farm": true, "urban": true, "downtown": true, "port": true, "park": true, "visitor": true}
	for k := range settlementRules {
		kinds[k] = true
	}
	for _, r := range rows {
		if len(r) != 8 {
			return report, fmt.Errorf("each site needs 8 fields")
		}
		var p ExpeditionSite
		fields := []interface{}{&p.ID, &p.Name, &p.Kind, &p.At, &p.Size, &p.Rotation, &p.Buildings, &p.Risk}
		for i, f := range fields {
			if err = json.Unmarshal(r[i], f); err != nil {
				return report, err
			}
		}
		rule, settlement := settlementRules[p.Kind]
		cityBlock := p.Kind == "urban" || p.Kind == "downtown" || p.Kind == "port"
		maxBuildings, maxWidth, maxDepth := 12, 140.0, 120.0
		if cityBlock || settlement {
			maxBuildings, maxWidth, maxDepth = 24, 220, 200
		}
		if settlement {
			maxBuildings = rule.max
		}
		if p.ID == "" || ids[p.ID] || !kinds[p.Kind] || p.Buildings < 1 || p.Buildings > maxBuildings || p.Risk < 1 || p.Risk > 3 || p.Size[0] < 40 || p.Size[1] < 42 || p.Size[0] > maxWidth || p.Size[1] > maxDepth {
			return report, fmt.Errorf("invalid PCG site %q", p.ID)
		}
		if settlement {
			if p.Buildings < rule.min {
				return report, fmt.Errorf("site %s: a %s needs at least %d buildings", p.ID, p.Kind, rule.min)
			}
			if w, d := rule.size(p.Buildings); p.Size[0] < w || p.Size[1] < d {
				return report, fmt.Errorf("site %s: a %s of %d buildings needs at least %.0fx%.0f m", p.ID, p.Kind, p.Buildings, w, d)
			}
		} else {
			minimumWidth := 20 + 15*float64((p.Buildings+1)/2)
			if cityBlock {
				minimumWidth = 28 + 22*float64((p.Buildings+3)/4)
			}
			if p.Size[0] < minimumWidth || (cityBlock && p.Size[1] < 120) {
				return report, fmt.Errorf("site %s has too many buildings for its courtyard", p.ID)
			}
		}
		ids[p.ID] = true
		span := 12.0
		if settlement && rule.terraced {
			span = 20 // each plot is levelled on its own pad
		}
		if err = validateExpeditionPadSpan(ir, grid, p.At, p.Size, p.Rotation, span); err != nil {
			return report, fmt.Errorf("site %s: %w", p.ID, err)
		}
		for _, q := range report.Sites {
			px, py := expeditionPadExtent(p.Size, p.Rotation)
			qx, qy := expeditionPadExtent(q.Size, q.Rotation)
			if math.Abs(p.At[0]-q.At[0]) < px+qx+5 && math.Abs(p.At[1]-q.At[1]) < py+qy+5 {
				return report, fmt.Errorf("PCG pads overlap: %s / %s", p.ID, q.ID)
			}
		}
		report.Sites = append(report.Sites, p)
		report.Buildings += p.Buildings
	}
	if report.Buildings != budget {
		return report, fmt.Errorf("PCG control budget is %d buildings; recipe declares %d", report.Buildings, budget)
	}
	if err = expeditionControlArray(string(text), "PCG_SPAWNS", &report.Spawns); err != nil {
		return report, err
	}
	if err = expeditionControlArray(string(text), "PCG_EXTRACTIONS", &report.Extractions); err != nil {
		return report, err
	}
	if len(report.Spawns) < 3 || len(report.Extractions) != 2 {
		return report, fmt.Errorf("PCG needs >=3 safe spawns and 2 extraction sites")
	}
	for _, p := range report.Spawns {
		if err = validateExpeditionPad(ir, grid, p, [2]float64{14, 14}, 0); err != nil {
			return report, fmt.Errorf("safe spawn %v: %w", p, err)
		}
		for _, site := range report.Sites {
			if math.Hypot(p[0]-site.At[0], p[1]-site.At[1]) < math.Hypot(site.Size[0], site.Size[1])/2+35 {
				return report, fmt.Errorf("safe spawn too close to encounter %s", site.ID)
			}
		}
	}
	for _, p := range report.Extractions {
		if err = validateExpeditionPad(ir, grid, p, [2]float64{42, 42}, 0); err != nil {
			return report, fmt.Errorf("extraction %v: %w", p, err)
		}
	}
	var vegetation [][]json.RawMessage
	if err = expeditionControlArray(string(text), "PCG_VEGETATION", &vegetation); err != nil {
		return report, err
	}
	for _, v := range vegetation {
		if len(v) != 6 {
			return report, fmt.Errorf("vegetation control requires 6 fields")
		}
		var count int
		var kind string
		var bounds [4]float64
		var slope float64
		var scale [2]float64
		if err = json.Unmarshal(v[1], &bounds); err != nil {
			return report, err
		}
		if err = json.Unmarshal(v[3], &slope); err != nil {
			return report, err
		}
		if err = json.Unmarshal(v[4], &scale); err != nil {
			return report, err
		}
		if err = json.Unmarshal(v[2], &count); err != nil {
			return report, err
		}
		if err = json.Unmarshal(v[5], &kind); err != nil {
			return report, err
		}
		if count < 0 || count > 2400 || bounds[0] >= bounds[2] || bounds[1] >= bounds[3] ||
			bounds[0] < -ir.SizeM/2+8 || bounds[1] < -ir.SizeM/2+8 || bounds[2] > ir.SizeM/2-8 || bounds[3] > ir.SizeM/2-8 ||
			slope < 0 || slope > 45 || scale[0] <= 0 || scale[1] < scale[0] || (kind != "shrub" && kind != "tree" && kind != "dead" && kind != "bamboo") {
			return report, fmt.Errorf("invalid vegetation control")
		}
		if kind == "shrub" {
			report.Shrubs += count
		} else {
			report.Trees += count
		}
	}
	// Require the complete control protocol before emitting a playable scene.
	var paths [][]json.RawMessage
	if err = expeditionControlArray(string(text), "PCG_PATHS", &paths); err != nil {
		return report, err
	}
	for _, path := range paths {
		if len(path) != 3 {
			return report, fmt.Errorf("PCG paths require road, points and width")
		}
		var kind string
		var points [][2]float64
		var width float64
		if err = json.Unmarshal(path[0], &kind); err != nil {
			return report, err
		}
		if err = json.Unmarshal(path[1], &points); err != nil {
			return report, err
		}
		if err = json.Unmarshal(path[2], &width); err != nil {
			return report, err
		}
		if kind != "road" || len(points) < 2 || width < 2 || width > 16 {
			return report, fmt.Errorf("invalid tactical road")
		}
		for _, p := range points {
			if math.Abs(p[0]) > ir.SizeM/2 || math.Abs(p[1]) > ir.SizeM/2 {
				return report, fmt.Errorf("tactical road outside map")
			}
		}
	}
	var corridors [][3]json.RawMessage
	if err = expeditionControlArray(string(text), "PCG_CLEAR", &corridors); err != nil {
		return report, err
	}
	for _, c := range corridors {
		var a, b [2]float64
		var width float64
		if err = json.Unmarshal(c[0], &a); err != nil {
			return report, err
		}
		if err = json.Unmarshal(c[1], &b); err != nil {
			return report, err
		}
		if err = json.Unmarshal(c[2], &width); err != nil {
			return report, err
		}
		if width <= 0 || math.Hypot(a[0]-b[0], a[1]-b[1]) < 1 {
			return report, fmt.Errorf("invalid clear corridor")
		}
	}
	if regexp.MustCompile(`\bPCG_LANDCOVER\s*=`).Match(text) {
		var cover [][]json.RawMessage
		if err = expeditionControlArray(string(text), "PCG_LANDCOVER", &cover); err != nil {
			return report, err
		}
		for _, c := range cover {
			var lc ExpeditionLandcover
			var ring [][2]float64
			if len(c) != 3 || json.Unmarshal(c[0], &lc.ID) != nil || json.Unmarshal(c[1], &lc.Kind) != nil || json.Unmarshal(c[2], &ring) != nil {
				return report, fmt.Errorf("land cover needs [id,kind,[[x,y],...]]")
			}
			if !landcoverKinds[lc.Kind] || len(ring) < 3 {
				return report, fmt.Errorf("land cover %q: unknown kind %q or open polygon", lc.ID, lc.Kind)
			}
			for _, q := range ring {
				if math.Abs(q[0]) > ir.SizeM/2 || math.Abs(q[1]) > ir.SizeM/2 {
					return report, fmt.Errorf("land cover %q leaves the map", lc.ID)
				}
			}
			lc.AreaM2 = math.Round(math.Abs(RingArea(Ring(ring))))
			report.Landcover = append(report.Landcover, lc)
		}
	}
	if regexp.MustCompile(`\bPCG_ALLEYS\s*=`).Match(text) {
		var alleys [][]json.RawMessage
		if err = expeditionControlArray(string(text), "PCG_ALLEYS", &alleys); err != nil {
			return report, err
		}
		for _, a := range alleys {
			var pts [][2]float64
			var species string
			var spacing, offset float64
			if len(a) != 4 || json.Unmarshal(a[0], &pts) != nil || json.Unmarshal(a[1], &species) != nil || json.Unmarshal(a[2], &spacing) != nil || json.Unmarshal(a[3], &offset) != nil {
				return report, fmt.Errorf("alleys need [[[x,y],...],species,spacing,offset]")
			}
			if len(pts) < 2 || spacing < 4 || spacing > 40 || offset < 0 || offset > 14 || species == "" {
				return report, fmt.Errorf("invalid tree alley")
			}
		}
		report.Alleys = len(alleys)
	}
	if regexp.MustCompile(`\bPCG_BRIDGES\s*=`).Match(text) {
		var bridges [][3]json.RawMessage
		if err = expeditionControlArray(string(text), "PCG_BRIDGES", &bridges); err != nil {
			return report, err
		}
		for _, bridge := range bridges {
			var a, b [2]float64
			var width float64
			if err = json.Unmarshal(bridge[0], &a); err != nil {
				return report, err
			}
			if err = json.Unmarshal(bridge[1], &b); err != nil {
				return report, err
			}
			if err = json.Unmarshal(bridge[2], &width); err != nil {
				return report, err
			}
			if math.Hypot(a[0]-b[0], a[1]-b[1]) < 12 || width < 3 || width > 12 {
				return report, fmt.Errorf("invalid PCG bridge span/width")
			}
			for _, p := range [][2]float64{a, b} {
				if math.Abs(p[0]) > ir.SizeM/2-8 || math.Abs(p[1]) > ir.SizeM/2-8 || grid.Sample(p[0], p[1]) < ir.Terrain.WaterLevel+0.25 {
					return report, fmt.Errorf("PCG bridge must end on dry banks inside the map")
				}
			}
		}
		report.Bridges = len(bridges)
	}
	return report, nil
}

func expeditionPadExtent(size [2]float64, angle float64) (float64, float64) {
	a := angle * math.Pi / 180
	c, s := math.Abs(math.Cos(a)), math.Abs(math.Sin(a))
	return (size[0]*c + size[1]*s) / 2, (size[0]*s + size[1]*c) / 2
}

// Settlement kinds expand in SCAD (nextdayz_settlement.scad); these rules keep
// a site large enough for its plots, streets and gardens. terraced kinds level
// every plot on its own pad, so they tolerate a wider total height range.
type settlementRule struct {
	min, max int
	terraced bool
	size     func(n int) (float64, float64)
}

// Mirrors nextdayz_settlement.scad: rows of plots keep settlementGap metres
// apart so each plot stays exactly flat on the 3.9 m terrain mesh.
const settlementGap = 12.0

func navesRows(n int) (top, bottom float64) {
	spec := 0
	switch {
	case n >= 13:
		spec = 3
	case n >= 9:
		spec = 2
	case n >= 5:
		spec = 1
	}
	farms := (n - spec) / 2
	if farms < 1 {
		farms = 1
	}
	up, down := (farms+1)/2, farms/2
	row := func(plots int, special bool) float64 {
		width, count := float64(plots)*24, plots
		if special {
			width, count = width+16, count+1
		}
		if count == 0 {
			return 0
		}
		return width + settlementGap*float64(count-1)
	}
	return row(up, spec >= 3), row(down, spec >= 2)
}

func terraceRow(houses int, plot float64) float64 {
	if houses == 0 {
		return 0
	}
	groups := (houses + 2) / 3
	return float64(houses)*plot + settlementGap*float64(groups-1)
}

var settlementRules = map[string]settlementRule{
	"hokkaido_lane": {2, 20, true, func(n int) (float64, float64) {
		groups := math.Ceil(math.Ceil(float64(n)/2) / 2)
		return 8 + groups*26 + (groups-1)*settlementGap, 64
	}},
	"shotengai": {2, 24, true, func(n int) (float64, float64) {
		groups := math.Ceil(math.Ceil(float64(n)/2) / 2)
		return 8 + groups*25 + (groups-1)*settlementGap, 46
	}},
	"canal":    {2, 6, true, func(n int) (float64, float64) { return 8 + float64(n)*33, 60 }},
	"portyard": {2, 8, true, func(n int) (float64, float64) { return 10 + math.Ceil(float64(n)/2)*42, 100 }},
	"shrine":   {1, 2, true, func(n int) (float64, float64) { return 66, 80 }},
	"naves": {3, 16, true, func(n int) (float64, float64) {
		top, bottom := navesRows(n)
		return math.Max(top, bottom) + 8, 110
	}},
	"homestead": {2, 3, true, func(n int) (float64, float64) { return 40, 60 }},
	"square":    {6, 30, false, func(n int) (float64, float64) { return math.Max(90, 6+float64(n)*4.2), 70 }},
	"street": {4, 24, true, func(n int) (float64, float64) {
		return 10 + float64((n+1)/2)*8.5 + settlementGap, 40
	}},
	"suburb": {2, 14, true, func(n int) (float64, float64) { return 6 + terraceRow((n+1)/2, 16), 40 }},
	"villas": {2, 10, true, func(n int) (float64, float64) { return 6 + terraceRow((n+1)/2, 21), 50 }},
	"chaty": {3, 14, true, func(n int) (float64, float64) {
		side := 11 + settlementGap
		cols := math.Ceil(math.Sqrt(float64(n)))
		return side * cols, side * math.Ceil(float64(n)/cols)
	}},
	"jzd": {3, 7, false, func(n int) (float64, float64) {
		if n >= 5 {
			return 80, 84
		}
		return 80, 66
	}},
	"factory": {1, 5, false, func(n int) (float64, float64) { return 70, 50 }},
	"hillcastle": {1, 5, false, func(n int) (float64, float64) {
		if n <= 1 {
			return 38, 30
		}
		return 38, 42 + math.Max(12, 11*math.Ceil(float64(n-1)/2)+1)
	}},
}

var landcoverKinds = map[string]bool{"spruce": true, "mixed": true, "beech": true, "pine": true, "beetle": true,
	"riparian": true, "orchard": true, "field": true, "meadow": true, "clearing": true,
	"hokkaido_mixed": true, "coastal_grass": true}

func validateExpeditionPad(ir *IR, grid *HeightGrid, at, size [2]float64, angle float64) error {
	return validateExpeditionPadSpan(ir, grid, at, size, angle, 12)
}

func validateExpeditionPadSpan(ir *IR, grid *HeightGrid, at, size [2]float64, angle, maxSpan float64) error {
	lo, hi := math.Inf(1), math.Inf(-1)
	a := angle * math.Pi / 180
	for i := -2; i <= 2; i++ {
		for j := -2; j <= 2; j++ {
			u, v := float64(i)*size[0]/4, float64(j)*size[1]/4
			x, y := at[0]+u*math.Cos(a)-v*math.Sin(a), at[1]+u*math.Sin(a)+v*math.Cos(a)
			if math.Abs(x) > ir.SizeM/2-8 || math.Abs(y) > ir.SizeM/2-8 {
				return fmt.Errorf("pad outside map bounds")
			}
			z := grid.Sample(x, y)
			lo = math.Min(lo, z)
			hi = math.Max(hi, z)
			if ir.Terrain.HasWater && z < ir.Terrain.WaterLevel+0.25 {
				return fmt.Errorf("pad overlaps water at %.1f,%.1f", x, y)
			}
		}
	}
	if hi-lo > maxSpan {
		return fmt.Errorf("earthworks span %.1fm; move/resize the PCG pad", hi-lo)
	}
	return nil
}

type expeditionDistrict struct {
	Center   [2]float64 `json:"center"`
	Size     [2]float64 `json:"size"`
	Coverage float64    `json:"coverage"`
}

func expeditionBuiltAreas(ir *IR) interface{} {
	areas := map[[2]int]float64{}
	for _, b := range ir.Buildings {
		c := Centroid(b.Outer)
		key := [2]int{int(math.Floor(c[0] / 100)), int(math.Floor(c[1] / 100))}
		areas[key] += RingArea(b.Outer)
	}
	var keys [][2]int
	for k := range areas {
		keys = append(keys, k)
	}
	sort.Slice(keys, func(i, j int) bool {
		if keys[i][1] != keys[j][1] {
			return keys[i][1] < keys[j][1]
		}
		return keys[i][0] < keys[j][0]
	})
	var districts []expeditionDistrict
	for _, k := range keys {
		districts = append(districts, expeditionDistrict{Center: [2]float64{float64(k[0])*100 + 50, float64(k[1])*100 + 50}, Size: [2]float64{100, 100}, Coverage: math.Min(1, areas[k]/10000)})
	}
	return map[string]interface{}{"usage": "district reference only; never a building spawn list", "districts": districts, "geographicCenter": ir.Center, "sizeM": ir.SizeM, "source": "OpenStreetMap contributors (ODbL 1.0)"}
}
