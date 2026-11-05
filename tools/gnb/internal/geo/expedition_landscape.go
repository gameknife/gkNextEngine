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
		cityBlock := p.Kind == "urban" || p.Kind == "downtown" || p.Kind == "port"
		maxBuildings, maxWidth, maxDepth := 12, 140.0, 120.0
		if cityBlock {
			maxBuildings, maxWidth, maxDepth = 24, 220, 200
		}
		if p.ID == "" || ids[p.ID] || !kinds[p.Kind] || p.Buildings < 1 || p.Buildings > maxBuildings || p.Risk < 1 || p.Risk > 3 || p.Size[0] < 40 || p.Size[1] < 42 || p.Size[0] > maxWidth || p.Size[1] > maxDepth {
			return report, fmt.Errorf("invalid PCG site %q", p.ID)
		}
		minimumWidth := 20 + 15*float64((p.Buildings+1)/2)
		if cityBlock {
			minimumWidth = 28 + 22*float64((p.Buildings+3)/4)
		}
		if p.Size[0] < minimumWidth || (cityBlock && p.Size[1] < 120) {
			return report, fmt.Errorf("site %s has too many buildings for its courtyard", p.ID)
		}
		ids[p.ID] = true
		if err = validateExpeditionPad(ir, grid, p.At, p.Size, p.Rotation); err != nil {
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

func validateExpeditionPad(ir *IR, grid *HeightGrid, at, size [2]float64, angle float64) error {
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
	if hi-lo > 12 {
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
