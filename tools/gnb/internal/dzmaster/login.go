package dzmaster

import (
	"net"
	"net/http"
	"strings"
	"time"

	"golang.org/x/crypto/bcrypt"
)

const maxLoginFailures = 5

type loginFailures struct {
	count   int
	expires time.Time
}

func (m *Master) handleLogin(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Cache-Control", "no-store")
	r.Body = http.MaxBytesReader(w, r.Body, 4096)
	var req struct {
		Name     string `json:"name"`
		Password string `json:"password"`
	}
	if !decode(w, r, &req) {
		return
	}
	name := strings.TrimSpace(req.Name)
	if name == "" || len(name) > 32 {
		httpError(w, http.StatusBadRequest, "a name of 1-32 bytes is needed")
		return
	}

	// Serialize enrollment and throttling independently of DS checkpoints.
	m.loginMu.Lock()
	defer m.loginMu.Unlock()
	var account Account
	m.store.View(func(st *State) {
		if existing := st.AccountByName(name); existing != nil {
			account = *existing
		}
	})
	enrolled := false
	if m.cfg.RequirePassword || account.PasswordHash != "" {
		source, _, _ := net.SplitHostPort(r.RemoteAddr)
		if m.cfg.TrustLocalProxy && net.ParseIP(source).IsLoopback() {
			if forwarded := net.ParseIP(r.Header.Get("X-Real-IP")); forwarded != nil {
				source = forwarded.String()
			}
		}
		now := time.Now()
		for key, failure := range m.loginFailures {
			if !now.Before(failure.expires) {
				delete(m.loginFailures, key)
			}
		}
		failure := m.loginFailures[source]
		if failure.count >= maxLoginFailures {
			w.Header().Set("Retry-After", "60")
			httpError(w, http.StatusTooManyRequests, "Too many login attempts. Try again in a minute.")
			return
		}
		password := []byte(req.Password)
		if len(password) == 0 || len(password) > 72 ||
			(account.PasswordHash != "" && bcrypt.CompareHashAndPassword([]byte(account.PasswordHash), password) != nil) {
			m.loginFailures[source] = loginFailures{count: failure.count + 1, expires: now.Add(time.Minute)}
			httpError(w, http.StatusUnauthorized, "A valid password of 1-72 bytes is required.")
			return
		}
		if account.PasswordHash == "" {
			hash, err := bcrypt.GenerateFromPassword(password, bcrypt.DefaultCost)
			if err != nil {
				httpError(w, http.StatusInternalServerError, "Could not set the account password.")
				return
			}
			account.PasswordHash = string(hash)
			enrolled = true
			// Invalidate any token issued while the nickname was unprotected.
			account.Token = randomHex(16)
		}
	}

	if account.ID == 0 || enrolled {
		err := m.store.Update(func(st *State) error {
			a := st.AccountByName(name)
			if a == nil {
				a = &Account{ID: st.NextID, Name: name, Token: randomHex(16)}
				st.NextID++
				st.Accounts[a.ID] = a
				if m.cfg.Economy != nil {
					m.cfg.Economy.RefreshBoard(a)
				}
				m.cfg.Logf("account %d %q created", a.ID, name)
			}
			if a.PasswordHash == "" && account.PasswordHash != "" {
				a.PasswordHash, a.Token = account.PasswordHash, account.Token
			}
			account = *a
			return nil
		})
		if err != nil {
			httpError(w, http.StatusInternalServerError, "Could not save the account.")
			return
		}
	}
	reply(w, map[string]any{"playerId": account.ID, "token": account.Token})
}
