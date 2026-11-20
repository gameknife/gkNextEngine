package dzmaster

import (
	"net/http"
	"strings"
	"time"
)

const (
	rtcOfferTimeout = 15 * time.Second
	rtcPollTimeout  = 2 * time.Second
	rtcMaxSDPBytes  = 16 * 1024
)

type rtcJob struct {
	server  string
	player  uint64
	ticket  string
	sdp     string
	created time.Time
	taken   bool
	answer  chan rtcAnswer
}

type rtcAnswer struct {
	SDP   string `json:"sdp,omitempty"`
	Error string `json:"error,omitempty"`
}

func validSDP(sdp string) bool {
	return len(sdp) <= rtcMaxSDPBytes && strings.HasPrefix(sdp, "v=0") &&
		strings.Contains(sdp, "m=application ")
}

func (m *Master) rtcTicketLocked(job *rtcJob) bool {
	t := m.tickets[job.ticket]
	return t != nil && t.player == job.player && t.server == job.server && time.Now().Before(t.expires)
}

func (m *Master) handleRtcOffer(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		httpError(w, http.StatusMethodNotAllowed, "use POST")
		return
	}
	a := m.account(r)
	if a == nil {
		httpError(w, http.StatusUnauthorized, "log in first")
		return
	}
	var req struct {
		Server string `json:"server"`
		Ticket string `json:"ticket"`
		SDP    string `json:"sdp"`
	}
	r.Body = http.MaxBytesReader(w, r.Body, 64*1024)
	if !decode(w, r, &req) {
		return
	}
	if !validSDP(req.SDP) {
		httpError(w, http.StatusBadRequest, "invalid WebRTC offer")
		return
	}
	job := &rtcJob{server: req.Server, ticket: req.Ticket, player: a.ID, sdp: req.SDP,
		created: time.Now(), answer: make(chan rtcAnswer, 1)}
	id := randomHex(16)
	m.mu.Lock()
	if !m.rtcTicketLocked(job) {
		m.mu.Unlock()
		httpError(w, http.StatusForbidden, "WebRTC offer requires your active dispatch ticket")
		return
	}
	server := m.servers[job.server]
	if server == nil || server.State != stateReady || server.RtcPort == 0 {
		m.mu.Unlock()
		httpError(w, http.StatusServiceUnavailable, "map server is not ready for WebRTC")
		return
	}
	for _, queued := range m.rtcJobs {
		if queued.player == job.player {
			m.mu.Unlock()
			httpError(w, http.StatusConflict, "WebRTC offer is already pending")
			return
		}
	}
	m.rtcJobs[id] = job
	select {
	case server.rtcNotify <- struct{}{}:
	default:
	}
	m.mu.Unlock()
	defer func() {
		m.mu.Lock()
		delete(m.rtcJobs, id)
		m.mu.Unlock()
	}()
	timer := time.NewTimer(rtcOfferTimeout)
	defer timer.Stop()
	select {
	case result := <-job.answer:
		if result.Error != "" {
			httpError(w, http.StatusBadGateway, result.Error)
		} else {
			reply(w, result)
		}
	case <-timer.C:
		httpError(w, http.StatusGatewayTimeout, "WebRTC offer timed out")
	case <-r.Context().Done():
	}
}

func (m *Master) handleDsSignal(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		httpError(w, http.StatusMethodNotAllowed, "use POST")
		return
	}
	var req struct {
		dsRequest
		Request string `json:"request"`
		rtcAnswer
	}
	r.Body = http.MaxBytesReader(w, r.Body, 64*1024)
	if !decode(w, r, &req) {
		return
	}
	m.mu.Lock()
	server := m.dsLocked(req.dsRequest)
	if server == nil || server.RtcPort == 0 {
		m.mu.Unlock()
		httpError(w, http.StatusForbidden, "unknown WebRTC map server")
		return
	}
	if req.Request != "" {
		job := m.rtcJobs[req.Request]
		if job == nil || job.server != server.ID || !job.taken || !m.rtcTicketLocked(job) {
			m.mu.Unlock()
			httpError(w, http.StatusNotFound, "WebRTC offer is no longer pending")
			return
		}
		if (req.Error == "" && !validSDP(req.SDP)) || len(req.Error) > 1024 {
			m.mu.Unlock()
			httpError(w, http.StatusBadRequest, "invalid WebRTC answer")
			return
		}
		delete(m.rtcJobs, req.Request)
		job.answer <- req.rtcAnswer
		m.mu.Unlock()
		w.WriteHeader(http.StatusNoContent)
		return
	}
	m.mu.Unlock()

	timer := time.NewTimer(rtcPollTimeout)
	defer timer.Stop()
	for {
		m.mu.Lock()
		var oldest *rtcJob
		var id string
		for candidate, job := range m.rtcJobs {
			if job.server == server.ID && !job.taken && m.rtcTicketLocked(job) &&
				(oldest == nil || job.created.Before(oldest.created)) {
				oldest, id = job, candidate
			}
		}
		if oldest != nil {
			oldest.taken = true
			m.mu.Unlock()
			reply(w, map[string]string{"request": id, "sdp": oldest.sdp})
			return
		}
		m.mu.Unlock()
		select {
		case <-server.rtcNotify:
		case <-timer.C:
			w.WriteHeader(http.StatusNoContent)
			return
		case <-r.Context().Done():
			return
		}
	}
}
