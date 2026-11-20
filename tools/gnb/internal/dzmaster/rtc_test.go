package dzmaster

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
)

const testRtcSDP = "v=0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"

func TestRTCSignalingChecksDispatchAndRelaysOnce(t *testing.T) {
	m, server, _ := newTestMaster(t)
	m.cfg.RtcPortBase = 28100
	login := func(name string) string {
		t.Helper()
		var result struct{ Token string }
		if call(t, server, "/api/login", "", map[string]string{"name": name}, &result) != 200 || result.Token == "" {
			t.Fatal("login failed")
		}
		return result.Token
	}
	alpha, bravo := login("rtc-alpha"), login("rtc-bravo")
	var deploy struct {
		Server, Ticket string
		RtcPort        int
	}
	if call(t, server, "/api/deploy", alpha, map[string]string{"map": "loket"}, &deploy) != 200 || deploy.RtcPort != 28100 {
		t.Fatalf("WebRTC port not dispatched: %+v", deploy)
	}
	key := dsKey(m, deploy.Server)
	if call(t, server, "/ds/heartbeat", "", map[string]any{"id": deploy.Server, "key": key, "ready": true}, nil) != 200 {
		t.Fatal("DS heartbeat failed")
	}

	offer := map[string]string{"server": deploy.Server, "ticket": deploy.Ticket, "sdp": testRtcSDP}
	if code := call(t, server, "/api/rtc/offer", "", offer, nil); code != http.StatusUnauthorized {
		t.Fatalf("anonymous offer: %d", code)
	}
	if code := call(t, server, "/api/rtc/offer", bravo, offer, nil); code != http.StatusForbidden {
		t.Fatalf("another account's dispatch: %d", code)
	}
	offer["server"] = "ds-other"
	if code := call(t, server, "/api/rtc/offer", alpha, offer, nil); code != http.StatusForbidden {
		t.Fatalf("wrong DS: %d", code)
	}
	offer["server"] = deploy.Server
	if code := call(t, server, "/ds/signal", "", map[string]string{"id": deploy.Server, "key": "wrong"}, nil); code != http.StatusForbidden {
		t.Fatalf("incorrect DS key: %d", code)
	}

	data, _ := json.Marshal(offer)
	request := httptest.NewRequest(http.MethodPost, "/api/rtc/offer", bytes.NewReader(data))
	request.Header.Set("Authorization", "Bearer "+alpha)
	w := httptest.NewRecorder()
	done := make(chan struct{})
	go func() {
		defer close(done)
		m.Handler().ServeHTTP(w, request)
	}()
	var job struct{ Request, SDP string }
	if call(t, server, "/ds/signal", "", map[string]string{"id": deploy.Server, "key": key}, &job) != 200 ||
		job.Request == "" || job.SDP != testRtcSDP {
		t.Fatal("DS did not get the offer once")
	}
	if code := call(t, server, "/api/rtc/offer", alpha, offer, nil); code != http.StatusConflict {
		t.Fatalf("second inflight offer: %d", code)
	}
	answer := testRtcSDP + "a=setup:active\r\n"
	body := map[string]string{"id": deploy.Server, "key": key, "request": job.Request, "sdp": answer}
	if code := call(t, server, "/ds/signal", "", body, nil); code != http.StatusNoContent {
		t.Fatalf("DS answer failed: %d", code)
	}
	<-done
	var relayed rtcAnswer
	if w.Code != http.StatusOK || json.Unmarshal(w.Body.Bytes(), &relayed) != nil || relayed.SDP != answer {
		t.Fatalf("offer did not complete: %d %s", w.Code, w.Body.String())
	}
	if code := call(t, server, "/ds/signal", "", body, nil); code != http.StatusNotFound {
		t.Fatalf("duplicate DS answer: %d", code)
	}
	// Signaling does not consume the one-time game admission ticket.
	if code := call(t, server, "/ds/redeem", "", map[string]string{"id": deploy.Server, "key": key, "ticket": deploy.Ticket}, nil); code != 200 {
		t.Fatalf("the signaling exchange consumed the admission ticket: %d", code)
	}
}

func TestRTCSignalingExpiresWhenDSDoesNotAnswer(t *testing.T) {
	m, server, _ := newTestMaster(t)
	m.cfg.RtcPortBase = 28100
	var login struct{ Token string }
	call(t, server, "/api/login", "", map[string]string{"name": "rtc-timeout"}, &login)
	var deploy struct{ Server, Ticket string }
	call(t, server, "/api/deploy", login.Token, map[string]string{"map": "loket"}, &deploy)
	key := dsKey(m, deploy.Server)
	call(t, server, "/ds/heartbeat", "", map[string]any{"id": deploy.Server, "key": key, "ready": true}, nil)
	offer := map[string]string{"server": deploy.Server, "ticket": deploy.Ticket, "sdp": testRtcSDP}
	if code := call(t, server, "/api/rtc/offer", login.Token, offer, nil); code != http.StatusGatewayTimeout {
		t.Fatalf("silent DS did not time out: %d", code)
	}
	if code := call(t, server, "/ds/signal", "", map[string]string{"id": deploy.Server, "key": key}, nil); code != http.StatusNoContent {
		t.Fatalf("expired offer still queued: %d", code)
	}
}
