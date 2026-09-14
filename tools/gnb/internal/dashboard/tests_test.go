package dashboard

import (
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"reflect"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

func TestExtractTags(t *testing.T) {
	tests := []struct {
		input    string
		expected []string
	}{
		{input: "[AI][Bridge][Unit]", expected: []string{"AI", "Bridge", "Unit"}},
		{input: "[FixedStep][GPU][Integration][Kinematic][Physics]", expected: []string{"FixedStep", "GPU", "Integration", "Kinematic", "Physics"}},
		{input: "", expected: nil},
		{input: "no tags", expected: nil},
		{input: "[  Trim  ][Case ]", expected: []string{"Trim", "Case"}},
	}

	for _, tc := range tests {
		got := ExtractTags(tc.input)
		if len(got) == 0 && len(tc.expected) == 0 {
			continue
		}
		if !reflect.DeepEqual(got, tc.expected) {
			t.Errorf("ExtractTags(%q) = %v; want %v", tc.input, got, tc.expected)
		}
	}
}

func TestParseCatch2XML(t *testing.T) {
	xmlData := []byte(`<?xml version="1.0" encoding="UTF-8"?>
<MatchingTests>
  <TestCase>
    <Name>NextRA fixed arithmetic is deterministic</Name>
    <ClassName/>
    <Tags>[NextRA][Unit]</Tags>
    <SourceInfo>
      <File>Test_NextRAFixed.cpp</File>
      <Line>134</Line>
    </SourceInfo>
  </TestCase>
  <TestCase>
    <Name>Kinematic target remains bounded</Name>
    <ClassName>EngineTestFixture</ClassName>
    <Tags>[GPU][Integration][Physics]</Tags>
    <SourceInfo>
      <File>Test_PhysicsSync.cpp</File>
      <Line>379</Line>
    </SourceInfo>
  </TestCase>
</MatchingTests>`)

	cases := parseCatch2XML(xmlData)
	if len(cases) != 2 {
		t.Fatalf("expected 2 cases, got %d", len(cases))
	}
	if cases[0].Name != "NextRA fixed arithmetic is deterministic" {
		t.Errorf("unexpected name: %s", cases[0].Name)
	}
	if !reflect.DeepEqual(cases[0].Categories, []string{"NextRA", "Unit"}) {
		t.Errorf("unexpected categories: %v", cases[0].Categories)
	}

	cats := aggregateCategories(cases)
	// Categories should be sorted case-insensitively: GPU, Integration, NextRA, Physics, Unit
	expectedCats := []string{"GPU", "Integration", "NextRA", "Physics", "Unit"}
	if len(cats) != len(expectedCats) {
		t.Fatalf("expected %d categories, got %d", len(expectedCats), len(cats))
	}
	for i, c := range cats {
		if c.Name != expectedCats[i] {
			t.Errorf("cat[%d] = %s; want %s", i, c.Name, expectedCats[i])
		}
		if c.Count != 1 {
			t.Errorf("cat[%d].Count = %d; want 1", i, c.Count)
		}
	}
}

func TestParseCatch2List(t *testing.T) {
	text := `Matching test cases:
  Atmosphere parameter layout matches the Slang contract
      [Atmosphere][Unit]
  Scad difference hollows a unioned tapered cup shell
      [Scad][Unit]
2 matching test cases`

	cases := parseCatch2List(text)
	if len(cases) != 2 {
		t.Fatalf("expected 2 cases, got %d", len(cases))
	}
	if cases[0].Name != "Atmosphere parameter layout matches the Slang contract" {
		t.Errorf("unexpected name: %s", cases[0].Name)
	}
	if !reflect.DeepEqual(cases[0].Categories, []string{"Atmosphere", "Unit"}) {
		t.Errorf("unexpected categories: %v", cases[0].Categories)
	}

	cats := aggregateCategories(cases)
	// Atmosphere (1), Scad (1), Unit (2)
	if len(cats) != 3 {
		t.Fatalf("expected 3 categories, got %d", len(cats))
	}
	for _, c := range cats {
		if c.Name == "Unit" && c.Count != 2 {
			t.Errorf("Unit count = %d; want 2", c.Count)
		}
	}
}

func TestHandleTabTestRendersElements(t *testing.T) {
	root := t.TempDir()
	srv := newTestBuildServer(t, root, "macos-arm64")

	req := httptest.NewRequest("GET", "/tab/test", nil)
	req.SetPathValue("kind", "test")
	rec := httptest.NewRecorder()
	srv.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("expected 200, got %d", rec.Code)
	}
	body := rec.Body.String()

	requiredSnippets := []string{
		`test-page`,
		`id="test-category-filter"`,
		`id="test-search-filter"`,
		`id="test-count-badge"`,
		`id="test-log-panel-host"`,
		`hx-target="#test-log-panel-host"`,
		`test-target-all-item`,
	}
	for _, snippet := range requiredSnippets {
		if !strings.Contains(body, snippet) {
			t.Errorf("expected tab/test to contain %q", snippet)
		}
	}
}

func TestTestJobSpec(t *testing.T) {
	root := t.TempDir()
	srv := newTestBuildServer(t, root, "macos-arm64")

	// Binary not present -> error
	_, err := srv.testJobSpec("all")
	if err == nil {
		t.Fatal("expected error when test binary does not exist")
	}

	// Create dummy binary
	binDir := platform.BinDir(root, "macos-arm64")
	_ = os.MkdirAll(binDir, 0755)
	dummyExe := platform.ExecutablePath(binDir, "gkNextUnitTests")
	_ = os.WriteFile(dummyExe, []byte("#!/bin/sh\nexit 0\n"), 0755)

	// "all" tests
	specAll, err := srv.testJobSpec("all")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !reflect.DeepEqual(specAll.Args, []string{"--colour-mode", "ansi"}) {
		t.Errorf("expected args [--colour-mode ansi], got %v", specAll.Args)
	}

	// Specific category tag "[GPU]"
	specCat, err := srv.testJobSpec("[GPU]")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !reflect.DeepEqual(specCat.Args, []string{"[GPU]", "--colour-mode", "ansi"}) {
		t.Errorf("expected args [[GPU] --colour-mode ansi], got %v", specCat.Args)
	}

	// Specific test case
	specSingle, err := srv.testJobSpec("UI metrics scale deterministically")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !reflect.DeepEqual(specSingle.Args, []string{"UI metrics scale deterministically", "--colour-mode", "ansi"}) {
		t.Errorf("expected args [UI metrics scale deterministically --colour-mode ansi], got %v", specSingle.Args)
	}
}

func TestHandleJobTestStartsJob(t *testing.T) {
	root := t.TempDir()
	preset := "macos-arm64"
	binDir := platform.BinDir(root, preset)
	if err := os.MkdirAll(binDir, 0o755); err != nil {
		t.Fatal(err)
	}
	exePath := platform.ExecutablePath(binDir, "gkNextUnitTests")
	script := "#!/bin/sh\necho 'tests started'\nexit 0\n"
	if err := os.WriteFile(exePath, []byte(script), 0o755); err != nil {
		t.Fatal(err)
	}

	srv := newTestBuildServer(t, root, preset)

	form := url.Values{
		"target": {"all"},
	}
	req := httptest.NewRequest(http.MethodPost, "/jobs/test", strings.NewReader(form.Encode()))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("HX-Request", "true")
	req.SetPathValue("kind", "test")
	rec := httptest.NewRecorder()

	srv.handleJobStart(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d; body: %s", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, `log-panel`) {
		t.Fatalf("expected log-panel in response, got: %s", body)
	}
}
