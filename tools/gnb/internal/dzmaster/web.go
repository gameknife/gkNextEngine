package dzmaster

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"mime"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

const WebAssetManifest = "/game-assets/manifest.json"
const WebBuildMarker = "NEXTDAYZ_RELEASE_BUILD"

type WebManifest struct {
	Build    string `json:"build"`
	Protocol uint16 `json:"protocol"`
	Assets   string `json:"assets"`
}

type webRelease struct {
	root     string
	manifest WebManifest
}

func openWebRelease(root string) (*webRelease, error) {
	data, err := os.ReadFile(filepath.Join(root, "release.json"))
	if err != nil {
		return nil, fmt.Errorf("read Web release: %w", err)
	}
	release := &webRelease{root: root}
	if err := json.Unmarshal(data, &release.manifest); err != nil {
		return nil, fmt.Errorf("invalid Web release: %w", err)
	}
	if !regexp.MustCompile(`^[a-f0-9]{64}$`).MatchString(release.manifest.Build) ||
		release.manifest.Protocol == 0 || release.manifest.Assets != WebAssetManifest {
		return nil, fmt.Errorf("invalid Web release in %s", root)
	}
	for _, file := range []string{"site/index.html", "ndcore/ndcore.mjs", "ndcore/ndcore.wasm", "game-assets/manifest.json"} {
		if info, err := os.Stat(filepath.Join(root, filepath.FromSlash(file))); err != nil || !info.Mode().IsRegular() {
			return nil, fmt.Errorf("Web release missing %s in %s", file, root)
		}
	}
	return release, nil
}

func (s *webRelease) routes(mux *http.ServeMux, requirePassword bool) {
	mux.HandleFunc("/api/web/manifest", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			httpError(w, http.StatusMethodNotAllowed, "use GET")
			return
		}
		w.Header().Set("Cache-Control", "no-store")
		reply(w, struct {
			WebManifest
			PasswordRequired bool `json:"passwordRequired"`
		}{s.manifest, requirePassword})
	})
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		requestPath := r.URL.Path
		switch {
		case requestPath == "/" || requestPath == "/index.html":
			s.serve(w, r, "site/index.html", false, false)
		case strings.HasPrefix(requestPath, "/assets/"):
			s.serve(w, r, "site"+requestPath, true, false)
		case requestPath == "/ndcore/ndcore.mjs" || requestPath == "/ndcore/ndcore.wasm":
			s.serve(w, r, strings.TrimPrefix(requestPath, "/"), false, false)
		case strings.HasPrefix(requestPath, "/game-assets/"):
			immutable := requestPath != WebAssetManifest
			s.serve(w, r, strings.TrimPrefix(requestPath, "/"), immutable, immutable)
		default:
			http.NotFound(w, r)
		}
	})
}

func (s *webRelease) serve(w http.ResponseWriter, r *http.Request, name string, immutable, gzipAvailable bool) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		httpError(w, http.StatusMethodNotAllowed, "use GET")
		return
	}
	if !fs.ValidPath(name) || strings.ContainsAny(name, "\\:") || strings.HasSuffix(name, ".gz") {
		http.NotFound(w, r)
		return
	}
	for _, part := range strings.Split(name, "/") {
		if strings.HasPrefix(part, ".") {
			http.NotFound(w, r)
			return
		}
	}
	filePath := filepath.Join(s.root, filepath.FromSlash(name))
	if gzipAvailable {
		w.Header().Set("Vary", "Accept-Encoding")
		if acceptsGzip(r.Header.Get("Accept-Encoding")) {
			if info, err := os.Stat(filePath + ".gz"); err == nil && info.Mode().IsRegular() {
				filePath += ".gz"
			}
		}
	}
	file, err := os.Open(filePath)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	defer file.Close()
	info, err := file.Stat()
	if err != nil || !info.Mode().IsRegular() {
		http.NotFound(w, r)
		return
	}
	ext := path.Ext(name)
	contentType := mime.TypeByExtension(ext)
	switch ext {
	case ".wasm":
		contentType = "application/wasm"
	case ".mjs", ".js":
		contentType = "text/javascript; charset=utf-8"
	case ".glb":
		contentType = "model/gltf-binary"
	}
	if contentType != "" {
		w.Header().Set("Content-Type", contentType)
	}
	if strings.HasSuffix(filePath, ".gz") {
		w.Header().Set("Content-Encoding", "gzip")
		w.Header().Set("Content-Length", strconv.FormatInt(info.Size(), 10))
	}
	w.Header().Set("Cache-Control", "no-store")
	if immutable {
		w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
	}
	w.Header().Set("X-Content-Type-Options", "nosniff")
	http.ServeContent(w, r, path.Base(name), info.ModTime(), file)
}

func acceptsGzip(value string) bool {
	for _, encoding := range strings.Split(value, ",") {
		name, params, _ := strings.Cut(encoding, ";")
		if !strings.EqualFold(strings.TrimSpace(name), "gzip") {
			continue
		}
		quality, isQuality := strings.CutPrefix(strings.TrimSpace(params), "q=")
		if !isQuality {
			return true
		}
		weight, err := strconv.ParseFloat(quality, 64)
		return err == nil && weight > 0
	}
	return false
}
