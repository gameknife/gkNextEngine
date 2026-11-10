package packager

import (
	"encoding/json"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"strings"
)

// Streamline loads its feature DLLs dynamically; they are absent from the PE
// import table. Ship the renderer plugins and their own dependency closures.
func collectNextDayzClientPlugins(binDir, stage string) error {
	if runtime.GOOS != "windows" {
		return nil
	}
	files, err := os.ReadDir(binDir)
	if err != nil {
		return err
	}
	for _, item := range files {
		if item.IsDir() {
			continue
		}
		name, lower := item.Name(), strings.ToLower(item.Name())
		plugin := strings.HasSuffix(lower, ".dll") && (strings.HasPrefix(lower, "sl.") || strings.HasPrefix(lower, "nvngx_") || lower == "nvlowlatencyvk.dll" || lower == "amd_fidelityfx_vk.dll")
		if !plugin && !strings.HasSuffix(lower, ".license.txt") {
			continue
		}
		source := filepath.Join(binDir, name)
		if err := copyServerFile(source, filepath.Join(stage, "bin", name), 0o644); err != nil {
			return err
		}
		if plugin {
			if err := collectServerPELibraries(source, stage); err != nil {
				return err
			}
		}
	}
	return nil
}

func normalizeNextDayzMasterURL(input string) (string, error) {
	input = strings.TrimSpace(input)
	if !strings.Contains(input, "://") {
		input = "http://" + input
	}
	// Match the game's current HTTP-only ServerDirectory contract.
	if !regexp.MustCompile(`^http://(\[[0-9a-fA-F:]+\]|[a-zA-Z0-9._-]+)(:[0-9]{1,5})?/?$`).MatchString(input) {
		return "", fmt.Errorf("--master-url must be an HTTP host[:port] without credentials, path, query or fragment")
	}
	parsed, err := url.Parse(input)
	if err != nil {
		return "", err
	}
	if parsed.Port() != "" {
		port, err := strconv.Atoi(parsed.Port())
		if err != nil || port < 1 || port > 65535 {
			return "", fmt.Errorf("invalid Master port")
		}
	}
	return strings.TrimSuffix(input, "/"), nil
}

func writeNextDayzClientServers(stage, masterURL string) error {
	path := filepath.Join(stage, "assets", "configs", "nextdayz_servers.json")
	if masterURL == "" {
		if _, err := os.Stat(path); err == nil {
			return nil
		} else if !os.IsNotExist(err) {
			return err
		}
		masterURL = "http://127.0.0.1:7780"
	}
	normalized, err := normalizeNextDayzMasterURL(masterURL)
	if err != nil {
		return err
	}
	raw, err := json.MarshalIndent(map[string]any{"version": 1, "servers": []map[string]string{{"name": "NextDayz Master", "url": normalized}}}, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	return os.WriteFile(path, append(raw, '\n'), 0o644)
}

func nextDayzClientBundleFiles() map[string]string {
	return map[string]string{
		"start-client.sh":  "#!/bin/sh\nset -eu\nroot=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\nexport LD_LIBRARY_PATH=\"$root/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\"\nexec \"$root/bin/NextDayz\" \"$@\"\n",
		"start-client.bat": "@echo off\r\n\"%~dp0bin\\NextDayz.exe\" %*\r\nexit /b %errorlevel%\r\n",
	}
}
