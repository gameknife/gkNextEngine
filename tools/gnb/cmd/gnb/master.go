package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"syscall"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/dzmaster"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/spf13/cobra"
)

// gnb master: the NextDayz Master (accounts, storage, the DS fleet, dispatch,
// checkpoints). `serve` runs it; the other subcommands talk to a running one.
// See docs/projects/nextdayz/nextdayz-multiplayer-design.md.

type masterOptions struct {
	Listen      string
	PublicHost  string
	DataDir     string
	PortBase    int
	MaxPlayers  int
	MaxServers  int
	IdleSeconds int
	Pvp         bool
	Cheats      bool
	RuntimeRoot string
	Headless    bool
}

func newMasterCommand(ctx appContext) *cobra.Command {
	opts := masterOptions{Listen: "127.0.0.1:7780", PortBase: 27100, MaxPlayers: 16, MaxServers: 32, IdleSeconds: 300, Headless: runtime.GOOS == "linux"}
	cmd := &cobra.Command{
		Use:   "master",
		Short: tr("cli.master.short"),
	}
	serve := &cobra.Command{
		Use:   "serve",
		Short: "Run the NextDayz Master; it starts map servers (NextDayz --dedicated) on demand",
		RunE: func(cmd *cobra.Command, args []string) error {
			m, err := newMaster(ctx, opts)
			if err != nil {
				return err
			}
			runCtx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
			defer stop()
			return m.Run(runCtx)
		},
	}
	serve.Flags().StringVar(&opts.Listen, "listen", opts.Listen, "address to serve on (clients and servers reach the Master here)")
	serve.Flags().StringVar(&opts.PublicHost, "public-host", "", "host clients are told to connect their map server to (default: the listen host)")
	serve.Flags().StringVar(&opts.DataDir, "data", "", "directory for master.json and server logs (default out/build/<preset>/master)")
	serve.Flags().IntVar(&opts.PortBase, "port-base", opts.PortBase, "first UDP port given to a map server")
	serve.Flags().IntVar(&opts.MaxPlayers, "max-players", opts.MaxPlayers, "survivors per map server")
	serve.Flags().IntVar(&opts.MaxServers, "max-servers", opts.MaxServers, "maximum concurrent DS processes; UDP range is port-base through port-base + max-servers - 1")
	serve.Flags().IntVar(&opts.IdleSeconds, "idle", opts.IdleSeconds, "seconds an empty map server lives")
	serve.Flags().BoolVar(&opts.Pvp, "pvp", false, "friendly fire on new map servers")
	serve.Flags().BoolVar(&opts.Cheats, "cheats", false, "map servers honour clients' test hooks (automated tests)")
	serve.Flags().StringVar(&opts.RuntimeRoot, "runtime-root", "", "standalone server bundle root (auto-detected beside bin/gnb)")
	serve.Flags().BoolVar(&opts.Headless, "headless", opts.Headless, "DS uses VK_EXT_headless_surface without a desktop (default on Linux)")

	var url string
	status := &cobra.Command{
		Use:   "status",
		Short: "Show the map servers, accounts and who is where",
		RunE: func(cmd *cobra.Command, args []string) error {
			body, err := masterCall(url, "/admin/status", nil)
			if err != nil {
				return err
			}
			var pretty bytes.Buffer
			_ = json.Indent(&pretty, body, "", "  ")
			fmt.Println(pretty.String())
			return nil
		},
	}
	kick := &cobra.Command{
		Use:   "kick <playerId>",
		Short: "Remove a survivor from its map server (it is kept, not killed)",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			var id uint64
			if _, err := fmt.Sscan(args[0], &id); err != nil {
				return err
			}
			_, err := masterCall(url, "/admin/kick", map[string]any{"player": id})
			return err
		},
	}
	var pvpOn bool
	pvp := &cobra.Command{
		Use:   "pvp <serverId>",
		Short: "Switch friendly fire on a running map server (--on / --on=false)",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			_, err := masterCall(url, "/admin/pvp", map[string]any{"server": args[0], "on": pvpOn})
			return err
		},
	}
	pvp.Flags().BoolVar(&pvpOn, "on", true, "friendly fire on")
	stopServer := &cobra.Command{
		Use:   "stop <serverId>",
		Short: "Stop a map server (its survivors are saved and can return)",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			_, err := masterCall(url, "/admin/stop", map[string]any{"server": args[0]})
			return err
		},
	}
	for _, c := range []*cobra.Command{status, kick, pvp, stopServer} {
		c.Flags().StringVar(&url, "url", "http://127.0.0.1:7780", "the Master")
	}
	cmd.AddCommand(serve, status, kick, pvp, stopServer)
	return cmd
}

func newMaster(ctx appContext, opts masterOptions) (*dzmaster.Master, error) {
	binDir := platform.BinDir(ctx.repoRoot, ctx.preset)
	assetRoot := ctx.repoRoot
	if opts.RuntimeRoot == "" {
		if executable, err := os.Executable(); err == nil {
			root := filepath.Dir(filepath.Dir(executable))
			if _, err := os.Stat(filepath.Join(root, "nextdayz-server.json")); err == nil {
				opts.RuntimeRoot = root
			}
		}
	}
	if opts.RuntimeRoot != "" {
		root, err := filepath.Abs(opts.RuntimeRoot)
		if err != nil {
			return nil, err
		}
		assetRoot, binDir = root, filepath.Join(root, "bin")
	} else if ctx.repoRoot == "" {
		return nil, fmt.Errorf("no server bundle found; use --runtime-root <extracted-package>")
	}
	exe := platform.ExecutablePath(binDir, "NextDayz")
	if _, err := os.Stat(exe); err != nil {
		return nil, fmt.Errorf("executable not found: %s\n请先运行 `gnb build NextDayz`", exe)
	}
	maps, err := dzmaster.LoadMaps(filepath.Join(assetRoot, "assets", "configs", "nextdayz_maps.json"))
	if err != nil {
		return nil, err
	}
	if opts.DataDir == "" {
		opts.DataDir = filepath.Join(filepath.Dir(binDir), "master")
		if opts.RuntimeRoot != "" {
			opts.DataDir = filepath.Join(assetRoot, "data")
		}
	}
	publicHost := opts.PublicHost
	if publicHost == "" {
		host, _, _ := splitHostPort(opts.Listen)
		publicHost = host
	}
	if publicHost == "" || publicHost == "0.0.0.0" || publicHost == "::" {
		return nil, fmt.Errorf("--public-host must specify the public IP or DNS name when listening on all interfaces")
	}
	dsArgs := []string{}
	if opts.Headless {
		dsArgs = append(dsArgs, "--headless-surface")
	}
	if opts.Cheats {
		dsArgs = append(dsArgs, "--cvar", "nextdayz.net.cheats true")
	}
	return dzmaster.New(dzmaster.Config{
		Listen: opts.Listen, PublicHost: publicHost, DataDir: opts.DataDir, Exe: exe, Maps: maps,
		PortBase: opts.PortBase, MaxPlayers: opts.MaxPlayers, MaxServers: opts.MaxServers, IdleSeconds: opts.IdleSeconds, Pvp: opts.Pvp,
		DSArgs: dsArgs, Launch: dzmaster.ExecLauncher(exe),
		Logf: func(format string, args ...any) { console.Info("[master] "+format, args...) },
	})
}

func splitHostPort(address string) (string, string, error) {
	return net.SplitHostPort(address)
}

func masterCall(url, path string, body any) ([]byte, error) {
	data, _ := json.Marshal(body)
	if body == nil {
		data = []byte("{}")
	}
	client := &http.Client{Timeout: 10 * time.Second}
	resp, err := client.Post(url+path, "application/json", bytes.NewReader(data))
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	out, _ := io.ReadAll(resp.Body)
	if resp.StatusCode/100 != 2 {
		return nil, fmt.Errorf("%s: %s", resp.Status, bytes.TrimSpace(out))
	}
	return out, nil
}
