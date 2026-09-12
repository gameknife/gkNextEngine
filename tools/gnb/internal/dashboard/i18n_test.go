package dashboard

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/i18n"
)

func TestSettingsTabFollowsLanguageCookie(t *testing.T) {
	s := setupDocsRepo(t)

	zhReq := httptest.NewRequest(http.MethodGet, "/tab/settings", nil)
	zhReq.SetPathValue("kind", "settings")
	zhReq.AddCookie(&http.Cookie{Name: i18n.CookieName, Value: "zh"})
	zhRec := httptest.NewRecorder()
	s.handleTab(zhRec, zhReq)
	if zhRec.Code != 200 {
		t.Fatalf("zh status = %d (%s)", zhRec.Code, zhRec.Body.String())
	}
	if !strings.Contains(zhRec.Body.String(), "显示设置") {
		t.Fatalf("zh settings missing 显示设置:\n%s", zhRec.Body.String())
	}

	enReq := httptest.NewRequest(http.MethodGet, "/tab/settings", nil)
	enReq.SetPathValue("kind", "settings")
	enReq.AddCookie(&http.Cookie{Name: i18n.CookieName, Value: "en"})
	enRec := httptest.NewRecorder()
	s.handleTab(enRec, enReq)
	if enRec.Code != 200 {
		t.Fatalf("en status = %d (%s)", enRec.Code, enRec.Body.String())
	}
	body := enRec.Body.String()
	if !strings.Contains(body, "Display") {
		t.Fatalf("en settings missing Display:\n%s", body)
	}
	if strings.Contains(body, "显示设置") {
		t.Fatalf("en settings still has Chinese chrome:\n%s", body)
	}
}

func TestHandleSetLangSetsCookie(t *testing.T) {
	s := setupDocsRepo(t)
	req := httptest.NewRequest(http.MethodPost, "/settings/lang", strings.NewReader("lang=en"))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("Referer", "/?tab=settings")
	rec := httptest.NewRecorder()
	s.handleSetLang(rec, req)
	if rec.Code != http.StatusSeeOther {
		t.Fatalf("status = %d, want 303", rec.Code)
	}
	found := false
	for _, c := range rec.Result().Cookies() {
		if c.Name == i18n.CookieName && c.Value == "en" {
			found = true
		}
	}
	if !found {
		t.Fatalf("missing gnb_lang=en cookie: %v", rec.Result().Cookies())
	}
}
