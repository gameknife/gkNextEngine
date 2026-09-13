package dashboard

import (
	"html/template"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/i18n"
)

func setupDocsRepo(t *testing.T) *Server {
	t.Helper()
	dir := t.TempDir()
	for path, body := range map[string]string{
		"docs/zeta.md":                         "# Zeta\n\n根目录末尾文档。\n",
		"docs/alpha.md":                        "---\ntitle: \"Alpha 标题\"\ncategory: guide\nstatus: 现行\nowner: NextTest\n---\n\n# Alpha\n\n第一篇文档。\n",
		"docs/architecture/overview.md":        "# Overview\n\n架构文档。\n",
		"docs/projects/zeta.md":                "# Project Zeta\n\n项目末尾文档。\n",
		"docs/projects/guide.md":               "# Guide\n\n项目文档。\n",
		"docs/projects/brotato-3d/index.md":    "# Brotato\n\n子目录文档。\n",
		"docs/gallery/ignore.avif":             "not-markdown",
		"docs/projects/ignore.txt":             "ignore me",
		"src/example.hpp":                      "#pragma once\n\nstruct Example\n{\n    int value;\n};\n",
		"assets/binary.dat":                    "binary\x00data",
		".spec/TODO.md":                        "# TODO\n\n## Milestone: 测试  <!-- status: active -->\n\n### 下一步\n\n(暂无)\n\n### 待规划\n\n(暂无)\n\n### 最近完成\n\n(暂无)\n",
	} {
		full := filepath.Join(dir, filepath.FromSlash(path))
		if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	tpl, err := template.New("dashboard").
		Funcs(templateFuncs()).
		ParseFS(templateFS, "templates/*.html")
	if err != nil {
		t.Fatal(err)
	}
	return &Server{
		opts:  Options{RepoRoot: dir, Lang: i18n.LangZh},
		tpl:   tpl,
		jobs:  NewJobManager(),
		chats: NewChatStore(),
	}
}

func TestBuildDocsVMListsMarkdownFilesOnly(t *testing.T) {
	s := setupDocsRepo(t)

	vm := s.buildDocsVM("", false, "", "")

	if len(vm.Files) != 6 {
		t.Fatalf("len(files) = %d, want 6", len(vm.Files))
	}
	wantFiles := []string{
		"docs/alpha.md",
		"docs/zeta.md",
		"docs/architecture/overview.md",
		"docs/projects/guide.md",
		"docs/projects/zeta.md",
		"docs/projects/brotato-3d/index.md",
	}
	for i, want := range wantFiles {
		if vm.Files[i].RelPath != want {
			t.Fatalf("files[%d] = %q, want %q; files = %+v", i, vm.Files[i].RelPath, want, vm.Files)
		}
	}
	if len(vm.Folders) != 4 {
		t.Fatalf("len(folders) = %d, want 4", len(vm.Folders))
	}
	wantFolders := []string{"docs", "docs/architecture", "docs/projects", "docs/projects/brotato-3d"}
	for i, want := range wantFolders {
		if vm.Folders[i].Dir != want {
			t.Fatalf("folders[%d] = %q, want %q; folders = %+v", i, vm.Folders[i].Dir, want, vm.Folders)
		}
	}
	if vm.Folders[3].DisplayName != "docs/.../brotato-3d" {
		t.Fatalf("folders[3].DisplayName = %q, want docs/.../brotato-3d", vm.Folders[3].DisplayName)
	}
	if !vm.HasDoc || vm.Selected.RelPath != "docs/alpha.md" {
		t.Fatalf("selected = %+v, want docs/alpha.md", vm.Selected)
	}
	if !vm.Folders[0].Active || vm.Folders[1].Active || vm.Folders[2].Active || vm.Folders[3].Active {
		t.Fatalf("folder active states = %+v, want only docs active", vm.Folders)
	}
	if !strings.Contains(vm.Content, "第一篇文档") {
		t.Fatalf("content = %q, want alpha markdown", vm.Content)
	}
	if strings.Contains(vm.Content, "title: \"Alpha 标题\"") {
		t.Fatalf("content should have frontmatter stripped, got %q", vm.Content)
	}
	if !vm.Meta.HasMeta || vm.Meta.Title != "Alpha 标题" || vm.Meta.Category != "guide" || vm.Meta.Status != "现行" || vm.Meta.Owner != "NextTest" {
		t.Fatalf("vm.Meta = %+v, want parsed frontmatter", vm.Meta)
	}
	if !strings.Contains(vm.EditorBody, "title: \"Alpha 标题\"") {
		t.Fatalf("editorBody should retain full frontmatter, got %q", vm.EditorBody)
	}
}

func TestResolveDocMarkdownPathRejectsTraversalAndNonMarkdown(t *testing.T) {
	s := setupDocsRepo(t)

	if _, _, err := resolveDocMarkdownPath(s.opts.RepoRoot, "../AGENTS.md"); err == nil {
		t.Fatal("expected traversal path to fail")
	}
	if _, _, err := resolveDocMarkdownPath(s.opts.RepoRoot, "docs/gallery/ignore.avif"); err == nil {
		t.Fatal("expected non-markdown path to fail")
	}
	if rel, _, err := resolveDocMarkdownPath(s.opts.RepoRoot, "docs/projects/guide.md"); err != nil || rel != "docs/projects/guide.md" {
		t.Fatalf("resolve nested markdown = (%q, %v), want docs/projects/guide.md", rel, err)
	}
}

func TestHandleTabDocsRendersEditView(t *testing.T) {
	s := setupDocsRepo(t)

	req := httptest.NewRequest("GET", "/tab/docs?file=docs/projects/guide.md&edit=1", nil)
	req.SetPathValue("kind", "docs")
	rec := httptest.NewRecorder()

	s.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, `name="path" value="docs/projects/guide.md"`) {
		t.Fatalf("rendered body missing selected path:\n%s", body)
	}
	if !strings.Contains(body, "<textarea") {
		t.Fatalf("rendered body missing textarea:\n%s", body)
	}
	if !strings.Contains(body, "项目文档") {
		t.Fatalf("rendered body missing markdown text:\n%s", body)
	}
	if !strings.Contains(body, `data-doc-folder="docs/projects"`) {
		t.Fatalf("rendered body missing projects folder group:\n%s", body)
	}
	if !strings.Contains(body, `class="docs-folder active"`) {
		t.Fatalf("rendered body missing active folder:\n%s", body)
	}
	if !strings.Contains(body, `data-docs-search`) || !strings.Contains(body, `class="docs-main"`) {
		t.Fatalf("rendered body missing docs workspace controls:\n%s", body)
	}
}

func TestHandleDocsSaveWritesFileAndReturnsPreview(t *testing.T) {
	s := setupDocsRepo(t)

	form := strings.NewReader("path=docs%2Falpha.md&body=%23%23+Updated%0A%0A%E4%BF%AE%E6%94%B9%E5%90%8E")
	req := httptest.NewRequest("POST", "/docs/save", form)
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	rec := httptest.NewRecorder()

	s.handleDocsSave(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	data, err := os.ReadFile(filepath.Join(s.opts.RepoRoot, "docs", "alpha.md"))
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != "## Updated\n\n修改后\n" {
		t.Fatalf("saved doc = %q, want normalized markdown", string(data))
	}
	body := rec.Body.String()
	if strings.Contains(body, "<textarea") {
		t.Fatalf("save should return preview mode, got edit form:\n%s", body)
	}
	if !strings.Contains(body, "修改后") {
		t.Fatalf("preview missing saved body:\n%s", body)
	}
}

func TestBuildDocsSourceVMReadsRepoFileAndFocusesLine(t *testing.T) {
	s := setupDocsRepo(t)

	vm := buildDocsSourceVM(s.opts.RepoRoot, "src/example.hpp", "5")

	if vm.Error != "" {
		t.Fatalf("unexpected error: %s", vm.Error)
	}
	if vm.RelPath != "src/example.hpp" || vm.Line != 5 || vm.LineCount != 6 {
		t.Fatalf("source metadata = %+v", vm)
	}
	if vm.Language != "cpp" || !strings.Contains(vm.Content, "struct Example") {
		t.Fatalf("source highlighting metadata = %+v", vm)
	}
	if len(vm.Lines) != 6 || !vm.Lines[4].Focus || vm.Lines[4].Text != "    int value;" {
		t.Fatalf("focused source line = %+v", vm.Lines)
	}
}

func TestBuildDocsSourceVMRejectsTraversalAndBinary(t *testing.T) {
	s := setupDocsRepo(t)

	if vm := buildDocsSourceVM(s.opts.RepoRoot, "../outside.txt", ""); vm.Error == "" {
		t.Fatal("expected traversal path to fail")
	}
	if vm := buildDocsSourceVM(s.opts.RepoRoot, "assets/binary.dat", ""); vm.Error == "" {
		t.Fatal("expected binary file to fail")
	}
}

func TestHandleDocsSourceRendersHighlightedLine(t *testing.T) {
	s := setupDocsRepo(t)
	req := httptest.NewRequest("GET", "/docs/source?path=src/example.hpp&line=5", nil)
	rec := httptest.NewRecorder()

	s.handleDocsSource(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, `class="docs-source-line-number focus" data-line="5"`) {
		t.Fatalf("rendered body missing focused line:\n%s", body)
	}
	if !strings.Contains(body, `<code class="language-cpp">`) {
		t.Fatalf("rendered body missing source language:\n%s", body)
	}
	if !strings.Contains(body, "int value;") {
		t.Fatalf("rendered body missing source text:\n%s", body)
	}
}

func TestDocsSourceLanguageUsesProjectRelevantHighlighters(t *testing.T) {
	cases := map[string]string{
		"src/main.cpp":                   "cpp",
		"src/Common/CoreMinimal.hpp":     "cpp",
		"assets/shaders/main.frag.slang": "cpp",
		"assets/shaders/shared.glsl":     "glsl",
		"assets/scad/example.scad":       "openscad",
		"tools/gnb/internal/main.go":     "go",
		"tools/build.ps1":                "powershell",
		"assets/scripts/game.ts":         "typescript",
		"cmake/toolchain.cmake":          "cmake",
		"CMakeLists.txt":                 "cmake",
		"docs/unknown.custom-extension":  "",
	}
	for path, want := range cases {
		if got := docsSourceLanguage(path); got != want {
			t.Errorf("docsSourceLanguage(%q) = %q, want %q", path, got, want)
		}
	}
}

func TestHandleTabSettingsRendersDisplayControls(t *testing.T) {
	s := setupDocsRepo(t)
	req := httptest.NewRequest("GET", "/tab/settings", nil)
	req.SetPathValue("kind", "settings")
	rec := httptest.NewRecorder()

	s.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	for _, want := range []string{
		`data-display-settings`,
		`data-display-setting="uiFontSize"`,
		`data-display-setting="codeFontSize"`,
		`data-display-setting="density"`,
		`data-display-reset`,
	} {
		if !strings.Contains(body, want) {
			t.Fatalf("settings response missing %q:\n%s", want, body)
		}
	}
}

func TestParseDocFrontmatter(t *testing.T) {
	raw := "---\ntitle: \"测试文档标题\"\ncategory: guide\nstatus: 现行\nowner: engine\ncreated: 2026-07-01\nlast_updated: 2026-08-01\n---\n\n# 真正的正文\n\n正文内容"
	meta, body := parseDocFrontmatter(raw)

	if !meta.HasMeta {
		t.Fatal("expected HasMeta = true")
	}
	if meta.Title != "测试文档标题" {
		t.Errorf("Title = %q, want 测试文档标题", meta.Title)
	}
	if meta.Category != "guide" {
		t.Errorf("Category = %q, want guide", meta.Category)
	}
	if meta.Status != "现行" {
		t.Errorf("Status = %q, want 现行", meta.Status)
	}
	if meta.Owner != "engine" {
		t.Errorf("Owner = %q, want engine", meta.Owner)
	}
	if meta.Created != "2026-07-01" {
		t.Errorf("Created = %q, want 2026-07-01", meta.Created)
	}
	if meta.LastUpdated != "2026-08-01" {
		t.Errorf("LastUpdated = %q, want 2026-08-01", meta.LastUpdated)
	}
	if strings.Contains(body, "title:") || strings.Contains(body, "---") {
		t.Errorf("body still contains frontmatter:\n%s", body)
	}
	if !strings.HasPrefix(body, "# 真正的正文") {
		t.Errorf("body missing heading:\n%s", body)
	}
}

func TestFormatDocFolderDisplay(t *testing.T) {
	cases := []struct {
		in   string
		want string
	}{
		{"docs", "docs"},
		{"docs/guides", "docs/guides"},
		{"docs/designs", "docs/designs"},
		{"docs/projects", "docs/projects"},
		{"docs/projects/brotato-3d", "docs/.../brotato-3d"},
		{"docs/projects/flappy-bird-parity", "docs/.../flappy-bird-parity"},
		{"docs/projects/airport-sim", "docs/.../airport-sim"},
		{"docs/a/b/c", "docs/.../c"},
	}

	for _, tc := range cases {
		if got := formatDocFolderDisplay(tc.in); got != tc.want {
			t.Errorf("formatDocFolderDisplay(%q) = %q, want %q", tc.in, got, tc.want)
		}
	}
}

func TestDocsViewRendersMetaBarAndNotRawTitle(t *testing.T) {
	s := setupDocsRepo(t)
	req := httptest.NewRequest("GET", "/tab/docs?file=docs/alpha.md", nil)
	req.SetPathValue("kind", "docs")
	rec := httptest.NewRecorder()

	s.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()

	// 验证包含 meta bar 和标签
	if !strings.Contains(body, `class="docs-meta-bar"`) {
		t.Fatalf("rendered body missing docs-meta-bar:\n%s", body)
	}
	if !strings.Contains(body, "现行") || !strings.Contains(body, "NextTest") || !strings.Contains(body, "guide") {
		t.Fatalf("rendered body missing meta tags:\n%s", body)
	}
	// 验证在 preview 区域没有 raw title: "Alpha 标题"
	if strings.Contains(body, `title: "Alpha 标题"`) {
		t.Fatalf("rendered body exposes raw title frontmatter in preview:\n%s", body)
	}
	// 验证子文件夹显示收敛
	if !strings.Contains(body, "docs/.../brotato-3d") {
		t.Fatalf("rendered body missing converged folder display docs/.../brotato-3d:\n%s", body)
	}
}

func TestDocsCacheInvalidationOnSave(t *testing.T) {
	s := setupDocsRepo(t)

	// 首次读取，建立缓存
	files1, err := s.listDocsFilesCached()
	if err != nil || len(files1) != 6 {
		t.Fatalf("initial cache read = %d files, err = %v", len(files1), err)
	}

	// 再次读取，应命中缓存（指针相同）
	files2, err := s.listDocsFilesCached()
	if err != nil || len(files2) != 6 {
		t.Fatalf("cached read = %d files, err = %v", len(files2), err)
	}

	// 写入新文档
	newDocPath := filepath.Join(s.opts.RepoRoot, "docs", "new_doc.md")
	if err := os.WriteFile(newDocPath, []byte("# New Doc\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	// 在缓存有效期内直接读依然是 6 个
	filesCached, _ := s.listDocsFilesCached()
	if len(filesCached) != 6 {
		t.Fatalf("expected cache hit with 6 files, got %d", len(filesCached))
	}

	// 调用 invalidateDocsCache 后应读出 7 个
	s.invalidateDocsCache()
	filesAfter, err := s.listDocsFilesCached()
	if err != nil || len(filesAfter) != 7 {
		t.Fatalf("after invalidation = %d files, want 7, err = %v", len(filesAfter), err)
	}
}

func TestHandleIndexDirectDocsTab(t *testing.T) {
	s := setupDocsRepo(t)

	req := httptest.NewRequest("GET", "/?tab=docs&file=docs/alpha.md", nil)
	rec := httptest.NewRecorder()

	s.handleIndex(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, `data-tab-pane="docs"`) {
		t.Fatalf("handleIndex with tab=docs should render docs pane active:\n%s", body)
	}
	if !strings.Contains(body, "第一篇文档。") {
		t.Fatalf("handleIndex with tab=docs should contain selected doc content:\n%s", body)
	}
	if !strings.Contains(body, `class="docs-meta-chip status active"`) {
		t.Fatalf("handleIndex with tab=docs should render meta bar:\n%s", body)
	}
}

