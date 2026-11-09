package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/spf13/cobra"
)

// gnb ds: a local multiplayer test session. One dedicated server (hidden
// window, no local player) plus N clients that connect to it. Logs of every
// process land in out/build/<preset>/ds-runs/<timestamp>/. The session ends
// when every client has exited (or on Ctrl+C); the server is stopped with it.
// See docs/projects/nextdayz/nextdayz-multiplayer-design.md.

type dsOptions struct {
	Target       string
	Scene        string
	Clients      int
	Port         int
	Pvp          bool
	Cheats       bool
	ShowServer   bool
	KeepServer   bool
	ReadyTimeout time.Duration
	DryRun       bool
}

const (
	dsReadyLine     = "committed scene"
	dsListeningLine = "[net] server listening"
)

func newDSCommand(ctx appContext) *cobra.Command {
	opts := dsOptions{Target: "NextDayz", Clients: 1, Port: 27015, ReadyTimeout: 5 * time.Minute}
	cmd := &cobra.Command{
		Use:   "ds [flags] [-- client-args]",
		Short: tr("cli.ds.short"),
		Long: "Start a local dedicated server (hidden window, no local player) and connect clients to it.\n" +
			"By default one NextDayz server on UDP 27015 and one visible client. The session ends when every\n" +
			"client exits (or on Ctrl+C) and the server is stopped with it. Logs: out/build/<preset>/ds-runs/<time>/.\n\n" +
			"Examples:\n" +
			"  gnb ds                         # 1 server + 1 client\n" +
			"  gnb ds --clients 2 --pvp       # two survivors, friendly fire on\n" +
			"  gnb ds --clients 0             # server only; connect clients yourself\n" +
			"  gnb ds -- --cvar \"nextdayz.net.kit false\"   # extra args for every client",
		RunE: func(cmd *cobra.Command, args []string) error {
			return runDS(ctx, opts, args)
		},
	}
	cmd.Flags().StringVar(&opts.Target, "target", opts.Target, "game executable to run as server and clients")
	cmd.Flags().StringVar(&opts.Scene, "scene", "", "map to host (default: the game's own choice); clients follow the server")
	cmd.Flags().IntVar(&opts.Clients, "clients", opts.Clients, "local clients to start (0 = server only)")
	cmd.Flags().IntVar(&opts.Port, "port", opts.Port, "UDP port of the server")
	cmd.Flags().BoolVar(&opts.Pvp, "pvp", false, "allow survivors to hurt each other")
	cmd.Flags().BoolVar(&opts.Cheats, "cheats", false, "let clients' validation hooks hurt their own survivor (automated tests)")
	cmd.Flags().BoolVar(&opts.ShowServer, "show-server", false, "show the server window (it has no player, only the world)")
	cmd.Flags().BoolVar(&opts.KeepServer, "keep-server", false, "leave the server running after the clients exit (Ctrl+C stops it)")
	cmd.Flags().DurationVar(&opts.ReadyTimeout, "ready-timeout", opts.ReadyTimeout, "how long to wait for the server to load its map")
	cmd.Flags().BoolVar(&opts.DryRun, "dry-run", false, "print the commands without running them")
	return cmd
}

func dsServerArgs(opts dsOptions) []string {
	// Nobody looks at the server's frames: keep NVIDIA Streamline out of it.
	args := []string{"--disable-streamline"}
	if !opts.ShowServer {
		// The hidden server only needs simulation; avoid another full-size path tracer.
		args = append(args, "--hidden-window", "--width=320", "--height=180",
			"--cvar", "r.rendererType 4", "--cvar", "r.upscaler.type 0")
	}
	if opts.Scene != "" {
		args = append(args, "--load-scene="+opts.Scene)
	}
	maxPlayers := opts.Clients
	if maxPlayers < 8 {
		maxPlayers = 8
	}
	return append(args,
		"--cvar", "nextdayz.net.mode server",
		"--cvar", fmt.Sprintf("nextdayz.net.port %d", opts.Port),
		"--cvar", fmt.Sprintf("nextdayz.net.pvp %t", opts.Pvp),
		"--cvar", fmt.Sprintf("nextdayz.net.maxPlayers %d", maxPlayers),
		"--cvar", fmt.Sprintf("nextdayz.net.cheats %t", opts.Cheats),
	)
}

func dsClientArgs(opts dsOptions, index int, extra []string) []string {
	args := []string{}
	if opts.Clients > 1 {
		// Multiple clients cannot share Streamline; keep upscaling instead of falling back to native.
		args = append(args, "--disable-streamline",
			"--cvar", "r.upscaler.type 4", "--cvar", "r.upscaler.qualityMode 0")
	}
	if opts.Scene != "" {
		args = append(args, "--load-scene="+opts.Scene)
	}
	args = append(args,
		"--cvar", "nextdayz.net.mode client",
		"--cvar", "nextdayz.net.host 127.0.0.1",
		"--cvar", fmt.Sprintf("nextdayz.net.port %d", opts.Port),
		"--cvar", fmt.Sprintf("nextdayz.net.name client%d", index),
	)
	return append(args, extra...)
}

// dsProcess is one child with its log file and a line filter echoed to the console.
type dsProcess struct {
	name string
	cmd  *exec.Cmd
	log  *os.File
	done chan error
}

func startDSProcess(name, exe string, args []string, logPath string, onLine func(string)) (*dsProcess, error) {
	logFile, err := os.Create(logPath)
	if err != nil {
		return nil, err
	}
	cmd := exec.Command(exe, args...)
	cmd.Dir = filepath.Dir(exe)
	pipeReader, pipeWriter := io.Pipe()
	cmd.Stdout = pipeWriter
	cmd.Stderr = pipeWriter
	if err := cmd.Start(); err != nil {
		logFile.Close()
		return nil, err
	}
	p := &dsProcess{name: name, cmd: cmd, log: logFile, done: make(chan error, 1)}
	go func() {
		scanner := bufio.NewScanner(pipeReader)
		scanner.Buffer(make([]byte, 64*1024), 1024*1024)
		for scanner.Scan() {
			line := scanner.Text()
			fmt.Fprintln(logFile, line)
			if onLine != nil {
				onLine(line)
			}
		}
	}()
	go func() {
		err := cmd.Wait()
		pipeWriter.Close()
		p.done <- err
	}()
	return p, nil
}

func (p *dsProcess) stop() {
	if p.cmd.ProcessState == nil && p.cmd.Process != nil {
		_ = p.cmd.Process.Kill()
	}
}

// dsEcho prints the lines a tester cares about: networking, errors, readiness.
func dsEcho(prefix string) func(string) {
	return func(line string) {
		switch {
		case strings.Contains(line, "[net]"), strings.Contains(line, "[error]"), strings.Contains(line, "[critical]"):
			fmt.Printf("%s %s\n", console.Dim(prefix), dsTrimLogPrefix(line))
		}
	}
}

// dsTrimLogPrefix drops spdlog's "[date time] [level] [file:line] " decoration.
func dsTrimLogPrefix(line string) string {
	if idx := strings.Index(line, "[NextDayz]"); idx >= 0 {
		return line[idx:]
	}
	return line
}

func runDS(ctx appContext, opts dsOptions, clientExtra []string) error {
	if opts.Clients < 0 {
		return fmt.Errorf("--clients must be 0 or more")
	}
	binDir := platform.BinDir(ctx.repoRoot, ctx.preset)
	exe := platform.ExecutablePath(binDir, opts.Target)
	serverArgs := dsServerArgs(opts)
	console.Header("Dedicated server test")
	console.Label("server", fmt.Sprintf("%s  UDP %d  friendly fire %t", opts.Target, opts.Port, opts.Pvp))
	console.Label("clients", fmt.Sprintf("%d", opts.Clients))
	if opts.DryRun {
		console.CommandLine(exe + " " + dsQuoteArgs(serverArgs))
		for index := 1; index <= opts.Clients; index++ {
			console.CommandLine(exe + " " + dsQuoteArgs(dsClientArgs(opts, index, clientExtra)))
		}
		return nil
	}
	if _, err := os.Stat(exe); err != nil {
		return fmt.Errorf("executable not found: %s\n请先运行 `gnb build %s`", exe, opts.Target)
	}
	runDir := filepath.Join(filepath.Dir(binDir), "ds-runs", time.Now().Format("20060102-150405"))
	if err := os.MkdirAll(runDir, 0o755); err != nil {
		return err
	}
	console.Label("logs", runDir)

	var readyOnce sync.Once
	ready := make(chan struct{})
	var listening bool
	var mu sync.Mutex
	echo := dsEcho("[ds]")
	console.CommandLine(exe + " " + dsQuoteArgs(serverArgs))
	server, err := startDSProcess("ds", exe, serverArgs, filepath.Join(runDir, "server.log"), func(line string) {
		echo(line)
		mu.Lock()
		defer mu.Unlock()
		if strings.Contains(line, dsListeningLine) {
			listening = true
		}
		if listening && strings.Contains(line, dsReadyLine) {
			readyOnce.Do(func() { close(ready) })
		}
	})
	if err != nil {
		return fmt.Errorf("start server: %w", err)
	}
	defer server.log.Close()
	defer server.stop()

	interrupt := make(chan os.Signal, 1)
	signal.Notify(interrupt, os.Interrupt)
	defer signal.Stop(interrupt)

	console.Info("waiting for the server to load its map...")
	select {
	case <-ready:
		console.Success("server ready on UDP %d", opts.Port)
	case err := <-server.done:
		return fmt.Errorf("server exited before it was ready (%v); see %s", err, filepath.Join(runDir, "server.log"))
	case <-time.After(opts.ReadyTimeout):
		return fmt.Errorf("server not ready after %s; see %s", opts.ReadyTimeout, filepath.Join(runDir, "server.log"))
	case <-interrupt:
		console.Warn("interrupted")
		return nil
	}

	clients := make([]*dsProcess, 0, opts.Clients)
	defer func() {
		for _, client := range clients {
			client.stop()
			client.log.Close()
		}
	}()
	for index := 1; index <= opts.Clients; index++ {
		args := dsClientArgs(opts, index, clientExtra)
		console.CommandLine(exe + " " + dsQuoteArgs(args))
		name := fmt.Sprintf("client%d", index)
		client, err := startDSProcess(name, exe, args, filepath.Join(runDir, name+".log"), dsEcho("["+name+"]"))
		if err != nil {
			return fmt.Errorf("start %s: %w", name, err)
		}
		clients = append(clients, client)
		time.Sleep(time.Second) // stagger the map loads a little
	}

	if opts.Clients == 0 || opts.KeepServer {
		console.Info("server running; connect with: %s --cvar \"nextdayz.net.mode client\" --cvar \"nextdayz.net.port %d\"", exe, opts.Port)
		console.Info("Ctrl+C stops it")
	}
	remaining := len(clients)
	clientDone := make(chan string, len(clients))
	for _, client := range clients {
		go func(p *dsProcess) {
			err := <-p.done
			if err != nil {
				console.Warn("%s exited: %v", p.name, err)
			}
			clientDone <- p.name
		}(client)
	}
	for {
		if remaining == 0 && !opts.KeepServer && len(clients) > 0 {
			console.Success("all clients exited; stopping the server")
			return nil
		}
		select {
		case name := <-clientDone:
			remaining--
			console.Info("%s exited (%d still running)", name, remaining)
		case err := <-server.done:
			return fmt.Errorf("server exited (%v); see %s", err, filepath.Join(runDir, "server.log"))
		case <-interrupt:
			console.Warn("interrupted; stopping server and clients")
			return nil
		}
	}
}

func dsQuoteArgs(args []string) string {
	quoted := make([]string, len(args))
	for i, arg := range args {
		if strings.ContainsAny(arg, " \t") {
			quoted[i] = `"` + arg + `"`
		} else {
			quoted[i] = arg
		}
	}
	return strings.Join(quoted, " ")
}
