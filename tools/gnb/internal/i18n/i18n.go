// Package i18n is gnb's Chinese/English UI catalog.
//
// Resolution order for a process: --lang, GNB_LANG, gnb.toml [gnb].lang,
// then the OS UI language. Dashboard requests may further override that
// with a ?lang= query or the gnb_lang cookie.
package i18n

import (
	"encoding/json"
	"fmt"
	"html/template"
	"net/http"
	"os"
	"strings"
	"sync/atomic"
)

type Lang string

const (
	LangEn Lang = "en"
	LangZh Lang = "zh"

	CookieName = "gnb_lang"
)

type Input struct {
	Flag   string
	Query  string
	Cookie string
	Env    string
	Config string
}

var current atomic.Value // Lang

func init() {
	current.Store(LangZh)
}

func Parse(s string) Lang {
	s = strings.ToLower(strings.TrimSpace(s))
	s = strings.ReplaceAll(s, "-", "_")
	switch {
	case s == "" || s == "auto":
		return ""
	case strings.HasPrefix(s, "zh"):
		return LangZh
	case strings.HasPrefix(s, "en"):
		return LangEn
	default:
		return ""
	}
}

func Detect() Lang {
	for _, key := range []string{"LC_ALL", "LC_MESSAGES", "LANG"} {
		if lang := Parse(os.Getenv(key)); lang != "" {
			return lang
		}
	}
	if lang := detectOSLang(); lang != "" {
		return lang
	}
	return LangZh
}

func Resolve(in Input) Lang {
	for _, s := range []string{in.Flag, in.Query, in.Cookie, in.Env, in.Config} {
		if lang := Parse(s); lang != "" {
			return lang
		}
	}
	return Detect()
}

func Set(lang Lang) {
	if lang == "" {
		lang = Detect()
	}
	current.Store(lang)
}

func Current() Lang {
	if v, ok := current.Load().(Lang); ok && v != "" {
		return v
	}
	return LangZh
}

func (l Lang) HTML() string {
	if l == LangZh {
		return "zh-CN"
	}
	return "en"
}

func T(key string, args ...any) string {
	return Translate(Current(), key, args...)
}

func Translate(lang Lang, key string, args ...any) string {
	s := lookup(lang, key)
	if len(args) == 0 {
		return s
	}
	return fmt.Sprintf(s, args...)
}

func lookup(lang Lang, key string) string {
	if lang == LangZh {
		if s, ok := zh[key]; ok {
			return s
		}
	}
	if s, ok := en[key]; ok {
		return s
	}
	if s, ok := zh[key]; ok {
		return s
	}
	return key
}

func JSON(lang Lang) template.JS {
	merged := make(map[string]string, len(en)+len(zh))
	for k, v := range en {
		merged[k] = v
	}
	if lang == LangZh {
		for k, v := range zh {
			merged[k] = v
		}
	}
	raw, err := json.Marshal(merged)
	if err != nil {
		return " {}"
	}
	return template.JS(raw)
}

func CookieValue(r *http.Request) string {
	if r == nil {
		return ""
	}
	c, err := r.Cookie(CookieName)
	if err != nil {
		return ""
	}
	return c.Value
}

func SetCookie(w http.ResponseWriter, lang Lang) {
	http.SetCookie(w, &http.Cookie{
		Name:     CookieName,
		Value:    string(lang),
		Path:     "/",
		MaxAge:   365 * 24 * 60 * 60,
		SameSite: http.SameSiteLaxMode,
	})
}

func CatalogKeys(lang Lang) map[string]struct{} {
	src := en
	if lang == LangZh {
		src = zh
	}
	keys := make(map[string]struct{}, len(src))
	for k := range src {
		keys[k] = struct{}{}
	}
	return keys
}
