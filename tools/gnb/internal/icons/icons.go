package icons

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/mobileapps"
)

var DefaultIcoSizes = []int{16, 32, 48, 64, 128, 256}

var AndroidMipmapSizes = map[string]int{
	"mipmap-mdpi":    48,
	"mipmap-hdpi":    72,
	"mipmap-xhdpi":   96,
	"mipmap-xxhdpi":  144,
	"mipmap-xxxhdpi": 192,
}

func clamp(v, min, max float64) float64 {
	if v < min {
		return min
	}
	if v > max {
		return max
	}
	return v
}

// ResizeAreaAverage resizes an image to dstW x dstH using an area-averaging box filter
// with proper un-premultiplied alpha handling.
func ResizeAreaAverage(src image.Image, dstW, dstH int) *image.RGBA {
	bounds := src.Bounds()
	srcW := bounds.Dx()
	srcH := bounds.Dy()
	dst := image.NewRGBA(image.Rect(0, 0, dstW, dstH))

	for dy := 0; dy < dstH; dy++ {
		for dx := 0; dx < dstW; dx++ {
			sx0 := float64(dx) * float64(srcW) / float64(dstW)
			sx1 := float64(dx+1) * float64(srcW) / float64(dstW)
			sy0 := float64(dy) * float64(srcH) / float64(dstH)
			sy1 := float64(dy+1) * float64(srcH) / float64(dstH)

			var rSum, gSum, bSum, aSum, totalWeight float64

			minSX := int(sx0)
			maxSX := int(sx1)
			if maxSX >= srcW {
				maxSX = srcW - 1
			}
			minSY := int(sy0)
			maxSY := int(sy1)
			if maxSY >= srcH {
				maxSY = srcH - 1
			}

			for sy := minSY; sy <= maxSY; sy++ {
				for sx := minSX; sx <= maxSX; sx++ {
					wx0 := math.Max(sx0, float64(sx))
					wx1 := math.Min(sx1, float64(sx+1))
					wy0 := math.Max(sy0, float64(sy))
					wy1 := math.Min(sy1, float64(sy+1))

					if wx1 > wx0 && wy1 > wy0 {
						weight := (wx1 - wx0) * (wy1 - wy0)
						r, g, b, a := src.At(bounds.Min.X+sx, bounds.Min.Y+sy).RGBA()
						rSum += float64(r) * weight
						gSum += float64(g) * weight
						bSum += float64(b) * weight
						aSum += float64(a) * weight
						totalWeight += weight
					}
				}
			}

			if totalWeight > 0 && aSum > 0 {
				alpha := aSum / totalWeight
				r := (rSum / totalWeight) * 65535.0 / alpha
				g := (gSum / totalWeight) * 65535.0 / alpha
				b := (bSum / totalWeight) * 65535.0 / alpha
				dst.SetRGBA(dx, dy, color.RGBA{
					R: uint8(clamp(r/257.0, 0, 255)),
					G: uint8(clamp(g/257.0, 0, 255)),
					B: uint8(clamp(b/257.0, 0, 255)),
					A: uint8(clamp(alpha/257.0, 0, 255)),
				})
			} else {
				dst.SetRGBA(dx, dy, color.RGBA{0, 0, 0, 0})
			}
		}
	}
	return dst
}

// EncodeICO encodes a source image into Windows .ico binary format with the requested sizes.
func EncodeICO(src image.Image, sizes []int) ([]byte, error) {
	type entry struct {
		size int
		data []byte
	}
	var entries []entry
	for _, sz := range sizes {
		var img image.Image
		if sz == src.Bounds().Dx() && sz == src.Bounds().Dy() {
			img = src
		} else {
			img = ResizeAreaAverage(src, sz, sz)
		}
		var buf bytes.Buffer
		if err := png.Encode(&buf, img); err != nil {
			return nil, fmt.Errorf("encode png for size %d: %w", sz, err)
		}
		entries = append(entries, entry{size: sz, data: buf.Bytes()})
	}

	buf := new(bytes.Buffer)
	// Header: 6 bytes
	binary.Write(buf, binary.LittleEndian, uint16(0))           // reserved
	binary.Write(buf, binary.LittleEndian, uint16(1))           // type = 1 (.ico)
	binary.Write(buf, binary.LittleEndian, uint16(len(entries))) // count

	offset := uint32(6 + 16*len(entries))
	for _, e := range entries {
		w := uint8(e.size)
		if e.size >= 256 {
			w = 0
		}
		h := w
		binary.Write(buf, binary.LittleEndian, w)
		binary.Write(buf, binary.LittleEndian, h)
		binary.Write(buf, binary.LittleEndian, uint8(0))   // color count
		binary.Write(buf, binary.LittleEndian, uint8(0))   // reserved
		binary.Write(buf, binary.LittleEndian, uint16(1))  // planes
		binary.Write(buf, binary.LittleEndian, uint16(32)) // bpp
		binary.Write(buf, binary.LittleEndian, uint32(len(e.data)))
		binary.Write(buf, binary.LittleEndian, offset)
		offset += uint32(len(e.data))
	}

	for _, e := range entries {
		buf.Write(e.data)
	}

	return buf.Bytes(), nil
}

// ConvertPngToIco converts an input PNG file to an output Windows ICO file.
func ConvertPngToIco(inputPng, outputIco string) error {
	f, err := os.Open(inputPng)
	if err != nil {
		return fmt.Errorf("open input png %s: %w", inputPng, err)
	}
	defer f.Close()

	img, err := png.Decode(f)
	if err != nil {
		return fmt.Errorf("decode png %s: %w", inputPng, err)
	}

	icoBytes, err := EncodeICO(img, DefaultIcoSizes)
	if err != nil {
		return fmt.Errorf("encode ico: %w", err)
	}

	if err := os.MkdirAll(filepath.Dir(outputIco), 0o755); err != nil {
		return fmt.Errorf("create ico dir: %w", err)
	}

	if err := os.WriteFile(outputIco, icoBytes, 0o644); err != nil {
		return fmt.Errorf("write ico %s: %w", outputIco, err)
	}

	return nil
}

// EnsureIco scans assets/icons/*.png and ensures that corresponding .ico files exist
// and are up to date under assets/icons/ico/. Returns the count of updated files.
func EnsureIco(repoRoot string, force bool) (int, error) {
	iconsDir := filepath.Join(repoRoot, "assets", "icons")
	icoDir := filepath.Join(iconsDir, "ico")

	entries, err := os.ReadDir(iconsDir)
	if err != nil {
		return 0, fmt.Errorf("read icons dir %s: %w", iconsDir, err)
	}

	if err := os.MkdirAll(icoDir, 0o755); err != nil {
		return 0, fmt.Errorf("create ico dir %s: %w", icoDir, err)
	}

	updatedCount := 0
	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(strings.ToLower(entry.Name()), ".png") {
			continue
		}

		pngPath := filepath.Join(iconsDir, entry.Name())
		icoName := strings.TrimSuffix(entry.Name(), filepath.Ext(entry.Name())) + ".ico"
		icoPath := filepath.Join(icoDir, icoName)

		needUpdate := force
		if !needUpdate {
			icoInfo, err := os.Stat(icoPath)
			if err != nil {
				needUpdate = true
			} else {
				pngInfo, err := entry.Info()
				if err != nil || pngInfo.ModTime().After(icoInfo.ModTime()) {
					needUpdate = true
				}
			}
		}

		if needUpdate {
			if err := ConvertPngToIco(pngPath, icoPath); err != nil {
				return updatedCount, fmt.Errorf("generate %s: %w", icoPath, err)
			}
			updatedCount++
		}
	}

	return updatedCount, nil
}

// GenerateAndroidIcons generates mdpi through xxxhdpi ic_launcher.png files from inputPng into outputResDir.
func GenerateAndroidIcons(inputPng, outputResDir string) error {
	f, err := os.Open(inputPng)
	if err != nil {
		return fmt.Errorf("open input png %s: %w", inputPng, err)
	}
	defer f.Close()

	img, err := png.Decode(f)
	if err != nil {
		return fmt.Errorf("decode png %s: %w", inputPng, err)
	}

	for dirName, size := range AndroidMipmapSizes {
		targetDir := filepath.Join(outputResDir, dirName)
		if err := os.MkdirAll(targetDir, 0o755); err != nil {
			return fmt.Errorf("create dir %s: %w", targetDir, err)
		}

		resized := ResizeAreaAverage(img, size, size)
		targetFile := filepath.Join(targetDir, "ic_launcher.png")
		outF, err := os.Create(targetFile)
		if err != nil {
			return fmt.Errorf("create %s: %w", targetFile, err)
		}
		if err := png.Encode(outF, resized); err != nil {
			outF.Close()
			return fmt.Errorf("encode %s: %w", targetFile, err)
		}
		outF.Close()
	}

	return nil
}

type Manifest struct {
	Style string         `json:"style"`
	Icons []ManifestIcon `json:"icons"`
}

type ManifestIcon struct {
	Target      string `json:"target"`
	File        string `json:"file"`
	Description string `json:"description"`
}

// ResolveAppIcon locates assets/icons/<app>.png, falling back to manifest.json/mobileapps aliases
// and finally the universal default engine icon gkNextEngine.png.
func ResolveAppIcon(repoRoot, app string) string {
	if app != "" {
		p := filepath.Join(repoRoot, "assets", "icons", app+".png")
		if _, err := os.Stat(p); err == nil {
			return p
		}

		// Check manifest.json
		manifestPath := filepath.Join(repoRoot, "assets", "icons", "manifest.json")
		if data, err := os.ReadFile(manifestPath); err == nil {
			var mf Manifest
			if err := json.Unmarshal(data, &mf); err == nil {
				for _, icon := range mf.Icons {
					if strings.EqualFold(icon.Target, app) && icon.File != "" {
						targetPath := filepath.Join(repoRoot, "assets", "icons", icon.File)
						if _, err := os.Stat(targetPath); err == nil {
							return targetPath
						}
					}
				}
			}
		}

		// Check mobileapps.json
		if apps, err := mobileapps.Load(repoRoot); err == nil {
			for _, a := range apps {
				if strings.EqualFold(a.Target, app) && a.Icon != "" {
					targetPath := filepath.Join(repoRoot, "assets", "icons", a.Icon+".png")
					if _, err := os.Stat(targetPath); err == nil {
						return targetPath
					}
				}
			}
		}
	}
	engineIcon := filepath.Join(repoRoot, "assets", "icons", "gkNextEngine.png")
	if _, err := os.Stat(engineIcon); err == nil {
		return engineIcon
	}
	return filepath.Join(repoRoot, "assets", "icons", "gkNextRenderer.png")
}

// GenerateAndroidAppIcons resolves the app icon and generates Android mipmaps into outputResDir.
func GenerateAndroidAppIcons(repoRoot, app, outputResDir string) error {
	iconPng := ResolveAppIcon(repoRoot, app)
	if _, err := os.Stat(iconPng); err != nil {
		return fmt.Errorf("app icon not found: %s", iconPng)
	}
	return GenerateAndroidIcons(iconPng, outputResDir)
}
