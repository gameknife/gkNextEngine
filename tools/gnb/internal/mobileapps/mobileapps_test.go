package mobileapps

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// repoRoot walks up from the test's working directory to the checkout root.
func repoRoot(t *testing.T) string {
	t.Helper()
	dir, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	for {
		if _, err := os.Stat(filepath.Join(dir, "vcpkg.json")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			t.Fatalf("repository root not found above %s", dir)
		}
		dir = parent
	}
}

func TestRegistryEntriesAreComplete(t *testing.T) {
	root := repoRoot(t)
	apps, err := Load(root)
	if err != nil {
		t.Fatalf("Load() error = %v", err)
	}

	targets := map[string]bool{}
	androidIDs := map[string]string{}
	iosIDs := map[string]string{}
	for _, app := range apps {
		if app.Target == "" || app.Label == "" || app.AndroidID == "" || app.IOSBundleID == "" {
			t.Errorf("incomplete registry entry: %+v", app)
			continue
		}
		if targets[app.Target] {
			t.Errorf("duplicate target %q", app.Target)
		}
		targets[app.Target] = true

		// A shared identifier would make one application overwrite another on a device.
		if previous, seen := androidIDs[app.AndroidID]; seen {
			t.Errorf("%s and %s share androidId %q", previous, app.Target, app.AndroidID)
		}
		androidIDs[app.AndroidID] = app.Target
		if previous, seen := iosIDs[app.IOSBundleID]; seen {
			t.Errorf("%s and %s share iosBundleId %q", previous, app.Target, app.IOSBundleID)
		}
		iosIDs[app.IOSBundleID] = app.Target

		if !app.SupportsPlatform(Android) && !app.SupportsPlatform(IOS) {
			t.Errorf("%s lists no mobile platform; remove it instead", app.Target)
		}
	}
}

// The registry names one directory per application, and a mobile configure adds exactly that
// directory. A typo there is only visible as a CMake failure on a device build, so check it here.
func TestRegistryDirectoriesExist(t *testing.T) {
	root := repoRoot(t)
	data, err := os.ReadFile(ManifestPath(root))
	if err != nil {
		t.Fatalf("read manifest: %v", err)
	}
	for _, line := range strings.Split(string(data), "\n") {
		trimmed := strings.TrimSpace(line)
		if !strings.HasPrefix(trimmed, `"directory"`) {
			continue
		}
		value := strings.Trim(strings.TrimSuffix(strings.TrimSpace(
			strings.SplitN(trimmed, ":", 2)[1]), ","), `"`)
		listPath := filepath.Join(root, "src", "Application",
			filepath.FromSlash(value), "CMakeLists.txt")
		if _, err := os.Stat(listPath); err != nil {
			t.Errorf("registry directory %q has no CMakeLists.txt: %v", value, err)
		}
	}
}

func TestResolveDefaultsToFirstListedApplication(t *testing.T) {
	root := repoRoot(t)
	for _, platform := range []string{Android, IOS} {
		apps, err := ForPlatform(root, platform)
		if err != nil {
			t.Fatalf("ForPlatform(%s) error = %v", platform, err)
		}
		resolved, err := Resolve(root, platform, "")
		if err != nil {
			t.Fatalf("Resolve(%s, \"\") error = %v", platform, err)
		}
		if resolved.Target != apps[0].Target {
			t.Errorf("Resolve(%s, \"\") = %q, want %q", platform, resolved.Target, apps[0].Target)
		}
	}
}

func TestResolveIsCaseInsensitiveAndRejectsUnknown(t *testing.T) {
	root := repoRoot(t)
	resolved, err := Resolve(root, Android, "gknextrenderer")
	if err != nil {
		t.Fatalf("Resolve() error = %v", err)
	}
	if resolved.Target != "gkNextRenderer" {
		t.Errorf("Resolve() = %q, want gkNextRenderer", resolved.Target)
	}
	if _, err := Resolve(root, Android, "NotAnApplication"); err == nil {
		t.Error("Resolve() accepted an application that is not in the registry")
	}
}

func TestResolveDiscoveredCSharpProject(t *testing.T) {
	root := t.TempDir()
	manifestPath := filepath.Join(root, "src", "Application", "MobileApplications.json")
	projectPath := filepath.Join(root, "projects", "TestTPS", "Scripts", "TestTPS.csproj")
	if err := os.MkdirAll(filepath.Dir(projectPath), 0o755); err != nil {
		t.Fatalf("create test C# project directory: %v", err)
	}
	if err := os.MkdirAll(filepath.Dir(manifestPath), 0o755); err != nil {
		t.Fatalf("create test registry directory: %v", err)
	}
	if err := os.WriteFile(manifestPath, []byte(`{"applications":[{"target":"NativeApp","label":"Native App","platforms":["android","ios"],"androidId":"com.gknext.native","iosBundleId":"gknext.native"}]}`), 0o644); err != nil {
		t.Fatalf("write test registry: %v", err)
	}
	if err := os.WriteFile(projectPath, []byte("<Project />"), 0o644); err != nil {
		t.Fatalf("write test C# project: %v", err)
	}

	for _, platform := range []string{Android, IOS} {
		resolved, err := Resolve(root, platform, "testtps")
		if err != nil {
			t.Fatalf("Resolve(%s, TestTPS) error = %v", platform, err)
		}
		if resolved.Target != "TestTPS" {
			t.Errorf("Resolve(%s, TestTPS) target = %q, want TestTPS", platform, resolved.Target)
		}
		if !resolved.RequiresDotNet || resolved.AndroidID != "com.gknext.testtps" ||
			resolved.IOSBundleID != "gknext.testtps" {
			t.Errorf("Resolve(%s, TestTPS) = %+v, want generated managed application", platform, resolved)
		}
	}
}

func TestProjectManagedGamesAreNotRegisteredMobileApplications(t *testing.T) {
	root := repoRoot(t)
	apps, err := Load(root)
	if err != nil {
		t.Fatalf("Load() error = %v", err)
	}
	registered := make(map[string]bool, len(apps))
	for _, app := range apps {
		registered[app.Target] = true
	}
	for _, target := range []string{"Brotato3DCSharp", "FlappyCSharp"} {
		if registered[target] {
			t.Errorf("%s is a projects/ C# game and must be discovered, not registered", target)
		}
	}
}

func TestResolveProjectManagedGame(t *testing.T) {
	root := repoRoot(t)
	for _, platform := range []string{Android, IOS} {
		resolved, err := Resolve(root, platform, "FlappyCSharp")
		if err != nil {
			t.Fatalf("Resolve(%s, FlappyCSharp) error = %v", platform, err)
		}
		if resolved.Target != "FlappyCSharp" || !resolved.RequiresDotNet ||
			resolved.AndroidID != "com.gknext.flappycsharp" ||
			resolved.IOSBundleID != "gknext.flappycsharp" {
			t.Errorf("Resolve(%s, FlappyCSharp) = %+v, want automatic managed application", platform, resolved)
		}
	}
}
