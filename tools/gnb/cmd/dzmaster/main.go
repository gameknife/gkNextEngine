// dzmaster is the NextDayz Master as a standalone server binary (the same
// package `gnb master serve` runs), for hosts without the gnb toolchain:
//
//	go build -o dzmaster ./cmd/dzmaster
//	dzmaster -exe /srv/nextdayz/NextDayz -maps /srv/nextdayz/assets/configs/nextdayz_maps.json -listen 0.0.0.0:7780
package main

import (
	"context"
	"flag"
	"log"
	"os"
	"os/signal"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/dzmaster"
)

func main() {
	exe := flag.String("exe", "", "NextDayz executable the map servers run")
	mapsPath := flag.String("maps", "assets/configs/nextdayz_maps.json", "NextDayz map registry")
	listen := flag.String("listen", "127.0.0.1:7780", "address to serve on")
	publicHost := flag.String("public-host", "127.0.0.1", "host clients connect their map server to")
	data := flag.String("data", "master-data", "directory for master.json and server logs")
	portBase := flag.Int("port-base", 27100, "first UDP port given to a map server")
	maxPlayers := flag.Int("max-players", 16, "survivors per map server")
	idle := flag.Int("idle", 300, "seconds an empty map server lives")
	pvp := flag.Bool("pvp", false, "friendly fire on new map servers")
	flag.Parse()
	if *exe == "" {
		log.Fatal("-exe is required")
	}
	maps, err := dzmaster.LoadMaps(*mapsPath)
	if err != nil {
		log.Fatal(err)
	}
	m, err := dzmaster.New(dzmaster.Config{Listen: *listen, PublicHost: *publicHost, DataDir: *data, Exe: *exe,
		Maps: maps, PortBase: *portBase, MaxPlayers: *maxPlayers, IdleSeconds: *idle, Pvp: *pvp,
		Launch: dzmaster.ExecLauncher(*exe)})
	if err != nil {
		log.Fatal(err)
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()
	if err := m.Run(ctx); err != nil {
		log.Fatal(err)
	}
}
