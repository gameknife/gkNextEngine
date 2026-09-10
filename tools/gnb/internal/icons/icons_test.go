package icons

import (
	"image"
	"image/color"
	"os"
	"path/filepath"
	"testing"
)

func TestEncodeICOAndResize(t *testing.T) {
	// Create a 64x64 synthetic image with semi-transparent circle
	src := image.NewRGBA(image.Rect(0, 0, 64, 64))
	for y := 0; y < 64; y++ {
		for x := 0; x < 64; x++ {
			dx := float64(x - 32)
			dy := float64(y - 32)
			if dx*dx+dy*dy <= 20*20 {
				src.SetRGBA(x, y, color.RGBA{R: 200, G: 50, B: 50, A: 180})
			} else {
				src.SetRGBA(x, y, color.RGBA{R: 0, G: 0, B: 0, A: 0})
			}
		}
	}

	icoBytes, err := EncodeICO(src, []int{16, 32, 64})
	if err != nil {
		t.Fatalf("EncodeICO failed: %v", err)
	}

	if len(icoBytes) < 22 {
		t.Fatalf("ICO too small: %d bytes", len(icoBytes))
	}

	tmpDir := t.TempDir()
	icoFile := filepath.Join(tmpDir, "test.ico")
	if err := os.WriteFile(icoFile, icoBytes, 0o644); err != nil {
		t.Fatalf("WriteFile failed: %v", err)
	}
}

func TestGenerateAndroidIcons(t *testing.T) {
	tmpDir := t.TempDir()

	realPng := "../../../../assets/icons/gkNextRenderer.png"
	if _, err := os.Stat(realPng); err == nil {
		resDir := filepath.Join(tmpDir, "res")
		if err := GenerateAndroidIcons(realPng, resDir); err != nil {
			t.Fatalf("GenerateAndroidIcons failed: %v", err)
		}
		for dirName := range AndroidMipmapSizes {
			target := filepath.Join(resDir, dirName, "ic_launcher.png")
			if _, err := os.Stat(target); err != nil {
				t.Errorf("missing %s: %v", target, err)
			}
		}
	}
}

func TestResolveAppIcon(t *testing.T) {
	repoRoot := "../../../.."
	// BrickPlayer should resolve to MagicaLego.png via alias
	icon := ResolveAppIcon(repoRoot, "BrickPlayer")
	if filepath.Base(icon) != "MagicaLego.png" {
		t.Errorf("expected MagicaLego.png for BrickPlayer, got %s", icon)
	}

	// Direct check for gkNextRenderer
	rendererIcon := ResolveAppIcon(repoRoot, "gkNextRenderer")
	if filepath.Base(rendererIcon) != "gkNextRenderer.png" {
		t.Errorf("expected gkNextRenderer.png, got %s", rendererIcon)
	}

	// gkNextEngine itself should resolve to gkNextEngine.png
	engineIcon := ResolveAppIcon(repoRoot, "gkNextEngine")
	if filepath.Base(engineIcon) != "gkNextEngine.png" {
		t.Errorf("expected gkNextEngine.png, got %s", engineIcon)
	}

	// Unconfigured or unknown app should resolve to default gkNextEngine.png
	defaultIcon := ResolveAppIcon(repoRoot, "NonExistentApp")
	if filepath.Base(defaultIcon) != "gkNextEngine.png" {
		t.Errorf("expected gkNextEngine.png for unconfigured app, got %s", defaultIcon)
	}
}

func TestGenerateIOSIcons(t *testing.T) {
	tmpDir := t.TempDir()

	realPng := "../../../../assets/icons/gkNextRenderer.png"
	if _, err := os.Stat(realPng); err == nil {
		outDir := filepath.Join(tmpDir, "ios_icons")
		if err := GenerateIOSIcons(realPng, outDir); err != nil {
			t.Fatalf("GenerateIOSIcons failed: %v", err)
		}
		for fileName := range IOSIconSizes {
			target := filepath.Join(outDir, fileName)
			if _, err := os.Stat(target); err != nil {
				t.Errorf("missing iOS icon %s: %v", target, err)
			}
		}
	}
}

func TestGenerateIOSAppIcons(t *testing.T) {
	tmpDir := t.TempDir()
	repoRoot := "../../../.."

	outDir := filepath.Join(tmpDir, "ios_app_icons")
	if err := GenerateIOSAppIcons(repoRoot, "gkNextRenderer", outDir); err != nil {
		t.Fatalf("GenerateIOSAppIcons failed: %v", err)
	}

	required := []string{
		"AppIcon.png",
		"AppIcon@2x.png",
		"AppIcon@3x.png",
		"AppIcon~ipad.png",
		"AppIcon@2x~ipad.png",
		"AppIcon60x60@2x.png",
		"AppIcon60x60@3x.png",
		"AppIcon76x76@2x~ipad.png",
		"AppIcon83.5x83.5@2x~ipad.png",
	}
	for _, req := range required {
		target := filepath.Join(outDir, req)
		if _, err := os.Stat(target); err != nil {
			t.Errorf("expected %s in output: %v", req, err)
		}
	}
}

