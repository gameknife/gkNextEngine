package geo

// Synthetic expedition terrain: levels that stand for no real place (the
// NextDayz training ground) still go through the expedition pipeline -- the
// same layout.scad controls, pads, land cover, kits, validation and frozen
// publishing -- but their ground comes from the recipe instead of SRTM/OSM.
import (
	"fmt"
	"math"
)

// ExpeditionSynthetic describes procedural ground: a gentle fBm field around
// a base height, authored hills and ridges on top, and plateaus where the
// field is flattened to the base for compounds, ranges and parade grounds.
type ExpeditionSynthetic struct {
	SizeM  float64 `json:"sizeM"`
	Base   float64 `json:"base"`   // ground height of the plateaus, metres
	Relief float64 `json:"relief"` // fBm amplitude, metres
	// Wavelength of the largest fBm octave, metres (default 260).
	Wavelength float64 `json:"wavelength,omitempty"`
	// [x, y, radius, falloff]: inside radius the field is exactly Base; it
	// blends back to the natural field over falloff metres.
	Flat [][4]float64 `json:"flat,omitempty"`
	// [x, y, radiusX, radiusY, rotationDeg, height]: smooth elliptic hills
	// (negative height digs a hollow). Added after flattening.
	Hills [][6]float64 `json:"hills,omitempty"`
}

func syntheticExpeditionTerrain(recipe ExpeditionRecipe) (IR, *HeightGrid, error) {
	s := recipe.Synthetic
	if s.SizeM < 200 || s.SizeM > 1000 || s.Relief < 0 || s.Relief > 40 || s.Base < 0 || s.Base > 200 {
		return IR{}, nil, fmt.Errorf("synthetic terrain needs sizeM 200..1000, relief 0..40 and base 0..200")
	}
	wavelength := s.Wavelength
	if wavelength <= 0 {
		wavelength = 260
	}
	for _, f := range s.Flat {
		if f[2] <= 0 || f[3] < 0 {
			return IR{}, nil, fmt.Errorf("synthetic plateau needs a positive radius")
		}
	}
	for _, h := range s.Hills {
		if h[2] <= 0 || h[3] <= 0 || math.Abs(h[5]) > 60 {
			return IR{}, nil, fmt.Errorf("synthetic hill needs positive radii and |height| <= 60")
		}
	}
	const cell = 4.0
	n := int(math.Ceil(s.SizeM/cell)) + 1
	origin := -s.SizeM / 2
	step := s.SizeM / float64(n-1)
	grid := NewHeightGrid(n, n, origin, origin, step, step)
	for row := 0; row < n; row++ {
		for col := 0; col < n; col++ {
			x, y := grid.PosX(col), grid.PosY(row)
			v := s.Base + s.Relief*syntheticFBM(x/wavelength, y/wavelength, recipe.Seed)
			// Plateaus pull the field to the base; the strongest one wins.
			pull := 0.0
			for _, f := range s.Flat {
				d := math.Hypot(x-f[0], y-f[1])
				w := 1.0
				if d > f[2] {
					if f[3] <= 0 {
						w = 0
					} else {
						w = 1 - smoothstep(0, 1, (d-f[2])/f[3])
					}
				}
				pull = math.Max(pull, w)
			}
			v = v*(1-pull) + s.Base*pull
			for _, h := range s.Hills {
				a := h[4] * math.Pi / 180
				dx, dy := x-h[0], y-h[1]
				u := (dx*math.Cos(a) + dy*math.Sin(a)) / h[2]
				w := (-dx*math.Sin(a) + dy*math.Cos(a)) / h[3]
				r := math.Sqrt(u*u + w*w)
				if r < 1 {
					// Smooth dome: zero slope at the rim and on top.
					k := 1 - smoothstep(0, 1, r)
					v += h[5] * k
				}
			}
			grid.Values[row*n+col] = v
		}
	}
	ir := IR{
		Tile:    recipe.ID,
		Center:  [2]float64{0, 0},
		SizeM:   s.SizeM,
		Terrain: &TerrainMeta{BaseElevation: s.Base, PaletteSpan: 160},
	}
	return ir, grid, nil
}

func smoothstep(a, b, t float64) float64 {
	t = clampF((t-a)/(b-a), 0, 1)
	return t * t * (3 - 2*t)
}

// syntheticFBM is deterministic value-noise fBm in about [-1, 1].
func syntheticFBM(x, y float64, seed int) float64 {
	sum, amp, norm := 0.0, 1.0, 0.0
	for octave := 0; octave < 5; octave++ {
		sum += amp * syntheticValueNoise(x, y, seed+octave*1013)
		norm += amp
		amp *= 0.5
		x, y = x*2.03+17.1, y*2.03-9.7
	}
	return sum / norm
}

func syntheticValueNoise(x, y float64, seed int) float64 {
	x0, y0 := math.Floor(x), math.Floor(y)
	tx, ty := x-x0, y-y0
	tx, ty = tx*tx*(3-2*tx), ty*ty*(3-2*ty)
	h := func(i, j float64) float64 {
		k := uint32(int32(i))*0x8DA6B343 ^ uint32(int32(j))*0xD8163841 ^ uint32(int32(seed))*0xCB1AB31F
		k ^= k >> 13
		k *= 0x5BD1E995
		k ^= k >> 15
		return float64(k%20001)/10000 - 1
	}
	a, b := h(x0, y0), h(x0+1, y0)
	c, d := h(x0, y0+1), h(x0+1, y0+1)
	return (a*(1-tx)+b*tx)*(1-ty) + (c*(1-tx)+d*tx)*ty
}
