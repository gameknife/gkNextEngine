package i18n

import (
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestParse(t *testing.T) {
	cases := map[string]Lang{
		"":      "",
		"auto":  "",
		"zh":    LangZh,
		"zh-CN": LangZh,
		"zh_CN": LangZh,
		"zhCN":  LangZh,
		"en":    LangEn,
		"en-US": LangEn,
		"en_GB": LangEn,
		"fr":    "",
	}
	for in, want := range cases {
		if got := Parse(in); got != want {
			t.Errorf("Parse(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestResolvePrefersFlagThenCookieThenConfig(t *testing.T) {
	got := Resolve(Input{Flag: "en", Cookie: "zh", Config: "zh"})
	if got != LangEn {
		t.Fatalf("flag should win, got %q", got)
	}
	got = Resolve(Input{Cookie: "en", Config: "zh"})
	if got != LangEn {
		t.Fatalf("cookie should beat config, got %q", got)
	}
	t.Setenv("LC_ALL", "en_US.UTF-8")
	t.Setenv("LC_MESSAGES", "")
	t.Setenv("LANG", "")
	got = Resolve(Input{Config: "auto"})
	if got != LangEn {
		t.Fatalf("OS locale should apply when config is auto, got %q", got)
	}
}

func TestTranslateFallbackAndFormat(t *testing.T) {
	if got := Translate(LangEn, "nav.settings"); got != "Settings" {
		t.Fatalf("en nav.settings = %q", got)
	}
	if got := Translate(LangZh, "nav.settings"); got != "设置" {
		t.Fatalf("zh nav.settings = %q", got)
	}
	if got := Translate(LangEn, "todo.recent_count", 3); got != "3 items" {
		t.Fatalf("format en = %q", got)
	}
	if got := Translate(LangZh, "todo.recent_count", 3); got != "3 条" {
		t.Fatalf("format zh = %q", got)
	}
	if got := Translate(LangEn, "does.not.exist"); got != "does.not.exist" {
		t.Fatalf("missing key = %q", got)
	}
}

func TestCatalogKeyParity(t *testing.T) {
	for k := range en {
		if _, ok := zh[k]; !ok {
			t.Errorf("zh missing key %q", k)
		}
	}
	for k := range zh {
		if _, ok := en[k]; !ok {
			t.Errorf("en missing key %q", k)
		}
	}
}

func TestSetCookieRoundTrip(t *testing.T) {
	rec := httptest.NewRecorder()
	SetCookie(rec, LangEn)
	cookies := rec.Result().Cookies()
	if len(cookies) == 0 {
		t.Fatal("no cookie set")
	}
	req := httptest.NewRequest(http.MethodGet, "/", nil)
	req.AddCookie(cookies[0])
	if got := Parse(CookieValue(req)); got != LangEn {
		t.Fatalf("cookie lang = %q", got)
	}
}

func TestHTMLLang(t *testing.T) {
	if LangZh.HTML() != "zh-CN" {
		t.Fatalf("zh HTML = %q", LangZh.HTML())
	}
	if LangEn.HTML() != "en" {
		t.Fatalf("en HTML = %q", LangEn.HTML())
	}
}
