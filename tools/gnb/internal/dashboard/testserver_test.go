package dashboard

import (
	"html/template"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/i18n"
)

// newTestServer returns a Server rooted at a fresh temp directory with the
// real templates parsed, for handler tests that do not need a full repo.
func newTestServer(t *testing.T) *Server {
	t.Helper()
	tpl, err := template.New("dashboard").
		Funcs(templateFuncs()).
		ParseFS(templateFS, "templates/*.html")
	if err != nil {
		t.Fatal(err)
	}
	return &Server{
		opts:  Options{RepoRoot: t.TempDir(), Lang: i18n.LangZh},
		tpl:   tpl,
		jobs:  NewJobManager(),
		chats: NewChatStore(),
	}
}
