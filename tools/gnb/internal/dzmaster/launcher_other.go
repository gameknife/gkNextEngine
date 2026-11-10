//go:build !windows

package dzmaster

import "os/exec"

func configureDSProcess(cmd *exec.Cmd) {}
