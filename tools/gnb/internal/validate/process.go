package validate

import (
	"context"
	"fmt"
	"os/exec"
	"time"
)

// processWaiter waits on a started target from the moment it launches, so the
// runner can tell at any point whether it already exited (a startup failure, a
// crash) without blocking, and every later wait or kill shares the one Wait call.
type processWaiter struct {
	cmd  *exec.Cmd
	done chan struct{}
	err  error
}

func newProcessWaiter(cmd *exec.Cmd) *processWaiter {
	w := &processWaiter{cmd: cmd, done: make(chan struct{})}
	go func() {
		w.err = cmd.Wait()
		close(w.done)
	}()
	return w
}

func (w *processWaiter) exited() bool {
	select {
	case <-w.done:
		return true
	default:
		return false
	}
}

// kill ends the process if it is still running and returns its exit error.
func (w *processWaiter) kill() error {
	if !w.exited() && w.cmd.Process != nil {
		_ = w.cmd.Process.Kill()
	}
	<-w.done
	return w.err
}

// wait lets the process exit on its own, killing it on cancellation or timeout.
func (w *processWaiter) wait(ctx context.Context, timeout time.Duration) error {
	timer := time.NewTimer(timeout)
	defer timer.Stop()
	select {
	case <-w.done:
		return w.err
	case <-ctx.Done():
		_ = w.kill()
		return ctx.Err()
	case <-timer.C:
		_ = w.kill()
		return fmt.Errorf("timed out after %s waiting for the agent process to exit", timeout)
	}
}

// exitCode is the exit code once the process has exited, nil while it runs.
func (w *processWaiter) exitCode() *int {
	if w == nil || !w.exited() || w.cmd.ProcessState == nil {
		return nil
	}
	code := w.cmd.ProcessState.ExitCode()
	return &code
}
