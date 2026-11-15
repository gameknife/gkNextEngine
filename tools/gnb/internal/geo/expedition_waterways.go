package geo

// Linear waterways for gameplay levels.
//
// A city tile gets its water from OSM polygons and one global water plane
// (terrain.go). Most of the world's rivers are not mapped that way: outside
// dense cities a river is a single centreline, and in a dry climate half of the
// "streams" are sand beds that only run after rain. A recipe therefore says how
// each OSM waterway class is realised:
//
//	"waterways": { "river":  {"width": 16, "depth": 2.2},
//	               "stream": {"width": 7, "dry": true} }
//
// A wet class becomes a TERR `river` operator (the engine carves the channel
// and lays a water surface that follows the bed downstream, so no global water
// level is needed); a dry class becomes a sand ribbon painted over the land
// cover. Classes the recipe does not name are ignored, and a recipe without the
// block regenerates byte-identically.
import (
	"fmt"
	"math"
	"sort"
	"strings"
)

type ExpeditionWaterway struct {
	Width float64 `json:"width"`
	Depth float64 `json:"depth,omitempty"`
	Dry   bool    `json:"dry,omitempty"`
}

// expeditionChannel is one clipped, simplified waterway in tile metres.
type expeditionChannel struct {
	Name  string
	Class string
	Pts   [][2]float64
	Width float64
	Depth float64
	Dry   bool
}

func expeditionChannels(ir *IR, rules map[string]ExpeditionWaterway) ([]expeditionChannel, error) {
	if len(rules) == 0 {
		return nil, nil
	}
	for class, r := range rules {
		if r.Width < 3 || r.Width > 60 || r.Depth < 0 || r.Depth > 8 || (!r.Dry && r.Depth < 0.5) {
			return nil, fmt.Errorf("waterway class %q: width 3..60 m, depth 0.5..8 m (dry beds need no depth)", class)
		}
	}
	ways := append([]Waterway(nil), ir.Waterways...)
	sort.SliceStable(ways, func(i, j int) bool { return ways[i].ID < ways[j].ID })
	var out []expeditionChannel
	for _, w := range ways {
		rule, ok := rules[w.Class]
		if !ok {
			continue
		}
		// A meander leaves and re-enters the square: keep every run, not only
		// the longest one a road would keep.
		for _, pts := range clipRunsToSquare(w.Pts, ir.SizeM/2) {
			if polylineLength(pts) < 2*rule.Width {
				continue
			}
			// 30 m DEM posts cannot resolve a tighter bend than this anyway,
			// and every vertex costs a distance test per terrain sample.
			pts = douglasPeucker(Ring(pts), 2.5)
			out = append(out, expeditionChannel{Name: w.Name, Class: w.Class, Pts: pts, Width: rule.Width, Depth: rule.Depth, Dry: rule.Dry})
		}
	}
	return out, nil
}

// wetChannelDistance is the distance from p to the nearest wet channel edge
// (negative inside the channel); +Inf when the level has no carved rivers.
func wetChannelDistance(channels []expeditionChannel, p [2]float64) float64 {
	best := math.Inf(1)
	for _, c := range channels {
		if c.Dry {
			continue
		}
		best = math.Min(best, PolylineDistance(c.Pts, p[0], p[1])-c.Width*0.55)
	}
	return best
}

// channelRibbon is the closed outline of a polyline widened to `width`.
func channelRibbon(pts [][2]float64, width float64) [][2]float64 {
	n := len(pts)
	left := make([][2]float64, 0, n)
	right := make([][2]float64, 0, n)
	for i, p := range pts {
		a, b := pts[max(0, i-1)], pts[min(n-1, i+1)]
		dx, dy := b[0]-a[0], b[1]-a[1]
		l := math.Hypot(dx, dy)
		if l < 1e-6 {
			continue
		}
		nx, ny := -dy/l*width/2, dx/l*width/2
		left = append(left, [2]float64{p[0] + nx, p[1] + ny})
		right = append(right, [2]float64{p[0] - nx, p[1] - ny})
	}
	for i := len(right) - 1; i >= 0; i-- {
		left = append(left, right[i])
	}
	return left
}

// insertExpeditionRivers puts the wet channels right after the hmap operator:
// TERR features apply in order, so the rivers carve the DEM before the roads
// and pads level their own ground (a road leaves the carved gap to its bridge).
func insertExpeditionRivers(terr string, channels []expeditionChannel) string {
	var rivers strings.Builder
	for _, c := range channels {
		if c.Dry {
			continue
		}
		rivers.WriteString(",\n     [\"river\", ")
		writePoints(&rivers, c.Pts)
		fmt.Fprintf(&rivers, ", %s, %s]", fnum(c.Width), fnum(c.Depth))
	}
	if rivers.Len() == 0 {
		return terr
	}
	const anchor = "\"set\", 1, 0]"
	return strings.Replace(terr, anchor, anchor+rivers.String(), 1)
}

// expeditionDryBeds is the GEO_DRYBEDS literal: sand ribbons painted after the
// land cover, so a lugga stays readable where it crosses a painted field.
func expeditionDryBeds(channels []expeditionChannel) string {
	var beds []string
	for _, c := range channels {
		if !c.Dry {
			continue
		}
		var b strings.Builder
		b.WriteString("[\"paint\", ")
		writePoints(&b, channelRibbon(c.Pts, c.Width))
		b.WriteString(", \"sand\"]")
		beds = append(beds, b.String())
	}
	if len(beds) == 0 {
		return ""
	}
	return "GEO_DRYBEDS = [" + strings.Join(beds, ",\n    ") + "];\n"
}

// clipRunsToSquare returns every stretch of the polyline inside the square,
// with the crossing points on the border as their ends.
func clipRunsToSquare(pts [][2]float64, half float64) [][][2]float64 {
	inside := func(p [2]float64) bool { return math.Abs(p[0]) <= half && math.Abs(p[1]) <= half }
	var runs [][][2]float64
	var run [][2]float64
	for i, p := range pts {
		if inside(p) {
			if len(run) == 0 && i > 0 {
				if t0, _, ok := clipParam(pts[i-1], p, half); ok {
					run = append(run, lerpPoint(pts[i-1], p, t0))
				}
			}
			run = append(run, p)
			continue
		}
		if len(run) > 0 {
			if _, t1, ok := clipParam(pts[i-1], p, half); ok {
				run = append(run, lerpPoint(pts[i-1], p, t1))
			}
			if len(run) >= 2 {
				runs = append(runs, run)
			}
			run = nil
		}
	}
	if len(run) >= 2 {
		runs = append(runs, run)
	}
	return runs
}
