package dashboard

import (
	"encoding/binary"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/pakbrowser"
)

func TestPaksTabRendersArchiveAnalysis(t *testing.T) {
	repoRoot := t.TempDir()
	pakDir := filepath.Join(repoRoot, "assets", "paks")
	if err := os.MkdirAll(pakDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeDashboardTestPak(t, filepath.Join(pakDir, "sample.pak"))

	server, err := New(Options{RepoRoot: repoRoot, Preset: "windows"})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	recorder := httptest.NewRecorder()
	request := httptest.NewRequest(http.MethodGet, "/tab/paks", nil)
	server.routes().ServeHTTP(recorder, request)

	if recorder.Code != http.StatusOK {
		t.Fatalf("status = %d, body = %s", recorder.Code, recorder.Body.String())
	}
	body := recorder.Body.String()
	for _, want := range []string{"sample.pak", "assets/models", "ship.glb", "文件组织结构", "文件类型", "60 B"} {
		if !strings.Contains(body, want) {
			t.Errorf("response does not contain %q", want)
		}
	}
}

func TestBuildPaksVMRejectsUndiscoveredPath(t *testing.T) {
	repoRoot := t.TempDir()
	pakDir := filepath.Join(repoRoot, "assets", "paks")
	if err := os.MkdirAll(pakDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeDashboardTestPak(t, filepath.Join(pakDir, "sample.pak"))
	server := &Server{opts: Options{RepoRoot: repoRoot, Preset: "windows"}}
	vm := server.buildPaksVM("../secret.pak", "", 0)
	if !strings.Contains(vm.Error, "不在可浏览范围") || len(vm.Files) != 1 {
		t.Fatalf("unexpected result: error = %q, files = %d", vm.Error, len(vm.Files))
	}
}

func TestPaksTabTruncationAndFilter(t *testing.T) {
	repoRoot := t.TempDir()
	pakDir := filepath.Join(repoRoot, "assets", "paks")
	if err := os.MkdirAll(pakDir, 0o755); err != nil {
		t.Fatal(err)
	}
	names := make([]string, 50)
	stored := make([]uint32, 50)
	original := make([]uint32, 50)
	for i := 0; i < 50; i++ {
		names[i] = filepath.ToSlash(filepath.Join("assets", "items", strings.Repeat("a", i+1)+".bin"))
		stored[i] = 10
		original[i] = 20
	}
	writeCustomDashboardTestPak(t, filepath.Join(pakDir, "large.pak"), names, stored, original)

	server, err := New(Options{RepoRoot: repoRoot, Preset: "windows"})
	if err != nil {
		t.Fatalf("New: %v", err)
	}

	// 1. 测试 limit 截断
	vmTruncated := server.buildPaksVM("assets/paks/large.pak", "", 10)
	if !vmTruncated.Truncated {
		t.Errorf("expected Truncated = true")
	}
	if len(vmTruncated.Rows) != 10 {
		t.Errorf("expected 10 rows, got %d", len(vmTruncated.Rows))
	}
	if vmTruncated.TotalRows <= 10 {
		t.Errorf("expected TotalRows > 10, got %d", vmTruncated.TotalRows)
	}

	// 2. 测试 query 过滤
	vmFiltered := server.buildPaksVM("assets/paks/large.pak", "aaaaa", 100)
	if len(vmFiltered.Rows) == 0 {
		t.Errorf("expected filtered rows, got 0")
	}
	for _, r := range vmFiltered.Rows {
		if !r.Directory && !strings.Contains(r.Path, "aaaaa") {
			t.Errorf("row %s should contain query aaaaa", r.Path)
		}
	}

	// 3. HTTP GET 测试渲染带加载指示器与纯文字列表
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/tab/paks?file=assets/paks/large.pak&limit=10", nil)
	server.routes().ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, body = %s", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, "pak-loading-overlay") {
		t.Errorf("expected loading overlay in body")
	}
	if !strings.Contains(body, "pak-tree-truncated-banner") {
		t.Errorf("expected truncated banner in body")
	}
	if strings.Contains(body, "pak-file-icon") {
		t.Errorf("pak-file-icon should be removed")
	}
}

func TestRealLdrawPerformance(t *testing.T) {
	repoRoot, err := filepath.Abs("../../../../")
	if err != nil {
		t.Fatal(err)
	}
	ldrawPath := filepath.Join(repoRoot, "assets", "paks", "ldraw.pak")
	if _, err := os.Stat(ldrawPath); err != nil {
		t.Skip("ldraw.pak not found, skipping real pak test")
	}

	server, err := New(Options{RepoRoot: repoRoot, Preset: "macos-arm64"})
	if err != nil {
		t.Fatalf("New: %v", err)
	}

	t0 := time.Now()
	arch, err := pakbrowser.Open(ldrawPath)
	dOpen := time.Since(t0)
	t.Logf("pakbrowser.Open took: %v (read index only, %d entries)", dOpen, len(arch.Entries))

	t1 := time.Now()
	rows, totalRows, trunc := buildPakTreeRows(arch.Entries, 100)
	dTree := time.Since(t1)
	t.Logf("buildPakTreeRows(100) took: %v (rows: %d, total: %d, trunc: %v)", dTree, len(rows), totalRows, trunc)

	t2 := time.Now()
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/tab/paks?file=assets/paks/ldraw.pak&limit=100", nil)
	server.routes().ServeHTTP(rec, req)
	d := time.Since(t2)

	t.Logf("ldraw.pak total ServeHTTP took %v, body length %d bytes", d, rec.Body.Len())
	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rec.Code)
	}
	// 验证不再输出 46MB 超大 HTML
	if rec.Body.Len() > 500*1024 {
		t.Fatalf("body length %d exceeds safe limit (should be under 500KB)", rec.Body.Len())
	}
	body := rec.Body.String()
	if !strings.Contains(body, "pak-tree-truncated-banner") {
		t.Errorf("expected truncated banner for ldraw.pak")
	}
}

func writeCustomDashboardTestPak(t *testing.T, path string, names []string, stored, original []uint32) {
	t.Helper()
	indexSize := 7 + len(names)*12
	for _, name := range names {
		indexSize += len(name) + 1
	}
	data := make([]byte, indexSize)
	copy(data, "GNP")
	binary.LittleEndian.PutUint32(data[3:7], uint32(len(names)))
	position := 7
	for _, name := range names {
		copy(data[position:], name)
		position += len(name) + 1
	}
	offset := uint32(indexSize)
	for index := range names {
		binary.LittleEndian.PutUint32(data[position:position+4], offset)
		binary.LittleEndian.PutUint32(data[position+4:position+8], stored[index])
		binary.LittleEndian.PutUint32(data[position+8:position+12], original[index])
		position += 12
		data = append(data, make([]byte, stored[index])...)
		offset += stored[index]
	}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
}

func writeDashboardTestPak(t *testing.T, path string) {
	t.Helper()
	names := []string{"assets/models/ship.glb", "assets/textures/ship.png"}
	stored := []uint32{40, 20}
	original := []uint32{100, 20}
	indexSize := 7 + len(names)*12
	for _, name := range names {
		indexSize += len(name) + 1
	}
	data := make([]byte, indexSize)
	copy(data, "GNP")
	binary.LittleEndian.PutUint32(data[3:7], uint32(len(names)))
	position := 7
	for _, name := range names {
		copy(data[position:], name)
		position += len(name) + 1
	}
	offset := uint32(indexSize)
	for index := range names {
		binary.LittleEndian.PutUint32(data[position:position+4], offset)
		binary.LittleEndian.PutUint32(data[position+4:position+8], stored[index])
		binary.LittleEndian.PutUint32(data[position+8:position+12], original[index])
		position += 12
		data = append(data, make([]byte, stored[index])...)
		offset += stored[index]
	}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
}
