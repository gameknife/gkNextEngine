package dzmaster

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
)

// execLauncher runs a DS as a child process, its output in a log file.
type execLauncher struct {
	exe string
}

type execProcess struct {
	cmd *exec.Cmd
	log *os.File
}

func (p *execProcess) Wait() error {
	err := p.cmd.Wait()
	p.log.Close()
	return err
}

func (p *execProcess) Kill() error {
	if p.cmd.Process == nil {
		return nil
	}
	return p.cmd.Process.Kill()
}

// ExecLauncher starts DS processes from the given executable.
func ExecLauncher(exe string) Launcher { return execLauncher{exe: exe} }

func (l execLauncher) Launch(server *Server, args []string, logPath string) (Process, error) {
	if err := os.MkdirAll(filepath.Dir(logPath), 0o755); err != nil {
		return nil, err
	}
	logFile, err := os.Create(logPath)
	if err != nil {
		return nil, err
	}
	cmd := exec.Command(l.exe, args...)
	configureDSProcess(cmd)
	cmd.Dir = filepath.Dir(l.exe)
	if runtime.GOOS == "linux" {
		cmd.Env = append(os.Environ(), "LD_LIBRARY_PATH="+cmd.Dir+":"+os.Getenv("LD_LIBRARY_PATH"))
	}
	cmd.Stdout = logFile
	cmd.Stderr = logFile
	if err := cmd.Start(); err != nil {
		logFile.Close()
		return nil, err
	}
	return &execProcess{cmd: cmd, log: logFile}, nil
}
