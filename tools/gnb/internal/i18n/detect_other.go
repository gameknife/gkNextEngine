//go:build !windows

package i18n

func detectOSLang() Lang {
	return ""
}
