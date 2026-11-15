package geo

import (
	"math"
	"strings"
	"testing"
)

func TestExpeditionWaterwaysCarveRiversAndPaintDryBeds(t *testing.T) {
	ir := &IR{SizeM: 1000, Waterways: []Waterway{
		// Leaves the square and comes back: both runs must survive.
		{ID: 1, Class: "river", Pts: [][2]float64{{-600, 0}, {-300, 0}, {-300, 600}, {300, 600}, {300, 0}, {600, 0}}},
		{ID: 2, Class: "stream", Pts: [][2]float64{{0, -400}, {0, -200}}},
		{ID: 3, Class: "ditch", Pts: [][2]float64{{100, -400}, {100, -200}}},
	}}
	channels, err := expeditionChannels(ir, map[string]ExpeditionWaterway{
		"river": {Width: 16, Depth: 2}, "stream": {Width: 8, Dry: true}})
	if err != nil || len(channels) != 3 {
		t.Fatalf("channels = %d, %v; want two river runs and one dry stream", len(channels), err)
	}
	terr := insertExpeditionRivers(`[["hmap", "a.hmap", "set", 1, 0],`+"\n"+`     ["road", [[0, 0], [1, 1]], 5]]];`, channels)
	if strings.Count(terr, `["river"`) != 2 || strings.Index(terr, `["river"`) > strings.Index(terr, `["road"`) {
		t.Fatalf("rivers must be carved before the roads:\n%s", terr)
	}
	if beds := expeditionDryBeds(channels); strings.Count(beds, `"sand"`) != 1 {
		t.Fatalf("dry beds = %q", beds)
	}
	if d := wetChannelDistance(channels, [2]float64{-300, 100}); d > -8 {
		t.Fatalf("a point on the centreline is inside the channel, got %.1f", d)
	}
	if d := wetChannelDistance(channels, [2]float64{0, -300}); d < 100 {
		t.Fatalf("a dry bed is not water, got %.1f", d)
	}
	ir.channels = channels
	ir.Terrain = &TerrainMeta{}
	grid := NewHeightGrid(3, 3, -500, -500, 500, 500)
	if err := validateExpeditionPad(ir, grid, [2]float64{-300, 100}, [2]float64{14, 14}, 0); err == nil {
		t.Fatal("a pad on the river must be rejected")
	}
	a, b, err := expeditionBridgeBanks(ir, grid, [2]float64{-304, 100}, [2]float64{-296, 100})
	if err != nil || math.Abs(a[0]+300) < 11 || math.Abs(b[0]+300) < 11 {
		t.Fatalf("bridge ends must move to the banks: %v %v %v", a, b, err)
	}
	if _, err := expeditionChannels(ir, map[string]ExpeditionWaterway{"river": {Width: 1, Depth: 2}}); err == nil {
		t.Fatal("an unusable width must be rejected")
	}
}
