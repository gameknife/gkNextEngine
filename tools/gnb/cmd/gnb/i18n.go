package main

import (
	"os"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/i18n"
)

func tr(key string) string {
	return i18n.T(key)
}

func explicitLang() string {
	for i, arg := range os.Args[1:] {
		if strings.HasPrefix(arg, "--lang=") {
			return strings.TrimPrefix(arg, "--lang=")
		}
		if arg == "--lang" && i+2 < len(os.Args) {
			return os.Args[i+2]
		}
	}
	return ""
}
