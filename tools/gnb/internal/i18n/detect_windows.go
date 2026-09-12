//go:build windows

package i18n

import "syscall"

var procGetUserDefaultUILanguage = syscall.NewLazyDLL("kernel32.dll").NewProc("GetUserDefaultUILanguage")

func detectOSLang() Lang {
	r, _, err := procGetUserDefaultUILanguage.Call()
	if r == 0 && err != nil && err != syscall.Errno(0) {
		return ""
	}
	if uint32(r)&0x3FF == 0x04 {
		return LangZh
	}
	if r == 0 {
		return ""
	}
	return LangEn
}
