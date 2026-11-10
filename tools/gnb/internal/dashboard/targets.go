package dashboard

import (
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

var addExecutablePattern = regexp.MustCompile(`(?im)\b(?:add_executable|gk_add_application)\s*\(\s*([A-Za-z0-9_.+-]+)`)

type targetVM struct {
	Name     string
	Category string
	Runnable bool
	Built    bool
}

type TargetGroupVM struct {
	Name    string
	Targets []targetVM
}

type discoveredTarget struct {
	Name     string
	Category string
}

func categorizeTarget(name, path string) string {
	lowerPath := strings.ToLower(filepath.ToSlash(path))
	switch {
	case strings.Contains(lowerPath, "application/render"):
		return "Render"
	case strings.Contains(lowerPath, "application/editor"):
		return "Editor"
	case strings.Contains(lowerPath, "application/game"):
		return "Game"
	case strings.Contains(lowerPath, "application/util"):
		return "Util"
	case strings.Contains(lowerPath, "tests"):
		return "Tests"
	}
	lowerName := strings.ToLower(name)
	switch {
	case strings.Contains(lowerName, "game") || strings.Contains(lowerName, "flappy") || strings.Contains(lowerName, "brotato") || strings.Contains(lowerName, "sim") || strings.Contains(lowerName, "astrobot") || strings.Contains(lowerName, "totalwar") || strings.Contains(lowerName, "dayz") || strings.Contains(lowerName, "travel") || strings.Contains(lowerName, "voyage") || strings.Contains(lowerName, "konglie"):
		return "Game"
	case strings.Contains(lowerName, "editor") || strings.Contains(lowerName, "scad"):
		return "Editor"
	case strings.Contains(lowerName, "render") || strings.Contains(lowerName, "benchmark") || strings.Contains(lowerName, "visual") || strings.Contains(lowerName, "rmlui"):
		return "Render"
	case strings.Contains(lowerName, "test"):
		return "Tests"
	case strings.Contains(lowerName, "packager") || strings.Contains(lowerName, "catalog"):
		return "Util"
	default:
		return "Other"
	}
}

func groupTargets(targets []targetVM) []TargetGroupVM {
	order := []string{"Render", "Editor", "Game", "Util", "Tests", "Other"}
	groupsMap := make(map[string][]targetVM)
	for _, t := range targets {
		cat := t.Category
		if cat == "" {
			cat = "Other"
		}
		groupsMap[cat] = append(groupsMap[cat], t)
	}

	var res []TargetGroupVM
	for _, name := range order {
		if items, ok := groupsMap[name]; ok && len(items) > 0 {
			res = append(res, TargetGroupVM{
				Name:    name,
				Targets: items,
			})
			delete(groupsMap, name)
		}
	}
	var leftovers []string
	for k := range groupsMap {
		leftovers = append(leftovers, k)
	}
	sort.Strings(leftovers)
	for _, k := range leftovers {
		if len(groupsMap[k]) > 0 {
			res = append(res, TargetGroupVM{
				Name:    k,
				Targets: groupsMap[k],
			})
		}
	}
	return res
}

func discoverTargets(repoRoot, preset string, fallback []string) []targetVM {
	discovered := discoverCMakeTargets(filepath.Join(repoRoot, "src"))
	seen := make(map[string]bool, len(discovered)+len(fallback))
	type item struct {
		Name     string
		Category string
	}
	var merged []item
	appendItem := func(name, category string) {
		name = strings.TrimSpace(name)
		if name == "" || seen[name] {
			return
		}
		seen[name] = true
		if category == "" {
			category = categorizeTarget(name, "")
		}
		merged = append(merged, item{Name: name, Category: category})
	}
	for _, d := range discovered {
		appendItem(d.Name, d.Category)
	}
	for _, name := range fallback {
		appendItem(name, "")
	}

	binDir := platform.BinDir(repoRoot, preset)
	targets := make([]targetVM, 0, len(merged))
	for _, m := range merged {
		_, err := os.Stat(platform.ExecutablePath(binDir, m.Name))
		targets = append(targets, targetVM{
			Name:     m.Name,
			Category: m.Category,
			Runnable: m.Name != "gkNextUnitTests",
			Built:    err == nil,
		})
	}
	return targets
}

func discoverCMakeTargets(root string) []discoveredTarget {
	var files []string
	_ = filepath.WalkDir(root, func(path string, entry os.DirEntry, err error) error {
		if err != nil || entry.IsDir() {
			return nil
		}
		name := entry.Name()
		if name == "CMakeLists.txt" || strings.EqualFold(filepath.Ext(name), ".cmake") {
			files = append(files, path)
		}
		return nil
	})
	sort.Strings(files)

	seen := map[string]bool{}
	var targets []discoveredTarget
	for _, path := range files {
		content, err := os.ReadFile(path)
		if err != nil {
			continue
		}
		for _, match := range addExecutablePattern.FindAllSubmatch(content, -1) {
			name := string(match[1])
			if !seen[name] {
				seen[name] = true
				targets = append(targets, discoveredTarget{
					Name:     name,
					Category: categorizeTarget(name, path),
				})
			}
		}
	}
	return targets
}

func discoverCMakeExecutables(root string) []string {
	targets := discoverCMakeTargets(root)
	names := make([]string, len(targets))
	for i, t := range targets {
		names[i] = t.Name
	}
	return names
}
