package csharpsln

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// writeProject lays out one managed project the way the repository does: root is ManagedRoot for
// the engine's assemblies, ProjectsRoot for a game project's Scripts/.
func writeProject(t *testing.T, repoRoot, root, relDir, name, source string) {
	t.Helper()
	dir := filepath.Join(repoRoot, filepath.FromSlash(root), filepath.FromSlash(relDir))
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatalf("create %s: %v", dir, err)
	}
	if err := os.WriteFile(filepath.Join(dir, name+".csproj"), []byte("<Project />"), 0o644); err != nil {
		t.Fatalf("write csproj: %v", err)
	}
	if err := os.WriteFile(filepath.Join(dir, name+".cs"), []byte(source), 0o644); err != nil {
		t.Fatalf("write source: %v", err)
	}
}

const gameSource = "[GameInstance]\npublic sealed class Game { }\n"

func newFixture(t *testing.T) string {
	t.Helper()
	repoRoot := t.TempDir()
	writeProject(t, repoRoot, ManagedRoot, "GkNext.Engine", "GkNext.Engine", "namespace GkNext;\n")
	writeProject(t, repoRoot, ProjectsRoot, "Flappy/Scripts", "FlappyCSharp", gameSource)
	return repoRoot
}

func TestDiscoverSplitsEngineFromGames(t *testing.T) {
	projects, err := Discover(newFixture(t))
	if err != nil {
		t.Fatalf("Discover() failed: %v", err)
	}
	if len(projects) != 2 {
		t.Fatalf("Discover() returned %d projects, want 2", len(projects))
	}
	if projects[0].Name != "GkNext.Engine" || projects[0].Folder != engineFolder {
		t.Fatalf("first project = %+v, want GkNext.Engine in %s", projects[0], engineFolder)
	}
	if projects[1].Name != "FlappyCSharp" || projects[1].Folder != gamesFolder {
		t.Fatalf("second project = %+v, want FlappyCSharp in %s", projects[1], gamesFolder)
	}
}

// A restore copy of the project file lives in obj/; listing it would add a phantom project. The
// same goes for a template check that was interrupted before it cleaned up.
func TestDiscoverSkipsBuildAndScratchDirectories(t *testing.T) {
	repoRoot := newFixture(t)
	writeProject(t, repoRoot, ManagedRoot, "GkNext.Engine/obj/Debug", "GkNext.Engine", "namespace GkNext;\n")
	writeProject(t, repoRoot, ProjectsRoot, "Flappy/Scripts/obj/Debug", "FlappyCSharp", gameSource)
	writeProject(t, repoRoot, ProjectsRoot, "_templatecheck_blank/Scripts", "_templatecheck_blank", gameSource)

	projects, err := Discover(repoRoot)
	if err != nil {
		t.Fatalf("Discover() failed: %v", err)
	}
	if len(projects) != 2 {
		t.Fatalf("Discover() returned %d projects, want 2", len(projects))
	}
}

// A repository with no game projects yet is still a valid solution: the engine alone.
func TestDiscoverWithoutProjectsDirectory(t *testing.T) {
	repoRoot := t.TempDir()
	writeProject(t, repoRoot, ManagedRoot, "GkNext.Engine", "GkNext.Engine", "namespace GkNext;\n")

	projects, err := Discover(repoRoot)
	if err != nil {
		t.Fatalf("Discover() failed: %v", err)
	}
	if len(projects) != 1 || projects[0].Name != "GkNext.Engine" {
		t.Fatalf("Discover() = %+v, want only GkNext.Engine", projects)
	}
}

func TestRunIsIdempotent(t *testing.T) {
	repoRoot := newFixture(t)

	first, err := Run(repoRoot, false)
	if err != nil {
		t.Fatalf("Run() failed: %v", err)
	}
	if !first.Changed {
		t.Fatal("Run() reported no change while creating the solution")
	}

	second, err := Run(repoRoot, false)
	if err != nil {
		t.Fatalf("second Run() failed: %v", err)
	}
	if second.Changed {
		t.Fatal("Run() rewrote an up-to-date solution")
	}
	if _, err := Run(repoRoot, true); err != nil {
		t.Fatalf("Run(check) rejected its own output: %v", err)
	}
}

func TestRunCheckReportsStaleSolution(t *testing.T) {
	repoRoot := newFixture(t)
	if _, err := Run(repoRoot, false); err != nil {
		t.Fatalf("Run() failed: %v", err)
	}
	writeProject(t, repoRoot, ProjectsRoot, "Brotato3D/Scripts", "Brotato3DCSharp", gameSource)

	if _, err := Run(repoRoot, true); err == nil {
		t.Fatal("Run(check) accepted a solution missing a project")
	}
}

// Visual Studio parses the .sln literally: the header, the BOM and the Windows separators in a
// project path all have to survive generation.
func TestRenderShape(t *testing.T) {
	projects, err := Discover(newFixture(t))
	if err != nil {
		t.Fatalf("Discover() failed: %v", err)
	}
	content := Render(projects)

	if !strings.HasPrefix(content, byteOrderMark+"\r\nMicrosoft Visual Studio Solution File, Format Version 12.00\r\n") {
		t.Fatalf("solution header is malformed: %q", content[:64])
	}
	// Relative to the solution in assets/csharp, so a game project is two levels up.
	for _, want := range []string{`"GkNext.Engine\GkNext.Engine.csproj"`, `"..\..\projects\Flappy\Scripts\FlappyCSharp.csproj"`} {
		if !strings.Contains(content, want) {
			t.Fatalf("solution does not contain %s", want)
		}
	}
	if strings.Contains(content, `\\`) {
		t.Fatal("solution contains escaped separators")
	}
	if strings.Contains(strings.ReplaceAll(content, "\r\n", ""), "\n") {
		t.Fatal("solution contains a bare LF")
	}
}

func TestDeterministicGUIDIsStable(t *testing.T) {
	first := deterministicGUID("GkNext.Engine/GkNext.Engine.csproj")
	second := deterministicGUID("gknext.engine/GkNext.Engine.csproj")
	if first != second {
		t.Fatalf("GUID is case sensitive: %s != %s", first, second)
	}
	if len(first) != 38 || first[0] != '{' || first[37] != '}' {
		t.Fatalf("GUID %q is not in solution format", first)
	}
	if first == deterministicGUID("../../projects/Flappy/Scripts/FlappyCSharp.csproj") {
		t.Fatal("two projects share a GUID")
	}
}
