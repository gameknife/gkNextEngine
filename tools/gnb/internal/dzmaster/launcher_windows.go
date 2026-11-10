package dzmaster

import (
	"os/exec"
	"syscall"
)

func configureDSProcess(cmd *exec.Cmd) {
	// Let the Master request a checkpoint/quit over the heartbeat protocol
	// before console Ctrl+C/Ctrl+Break can terminate child map servers.
	cmd.SysProcAttr = &syscall.SysProcAttr{CreationFlags: 0x00000200, HideWindow: true}
}
