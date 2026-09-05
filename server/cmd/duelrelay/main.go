// Command duelrelay ne fait tourner QUE le relais de duel.
//
// Il existe pour deux raisons, et les deux comptent :
//
//  1. Le tester. `ns_test_lockstep` a besoin d'un relais et de rien d'autre ;
//     l'obliger a monter PostgreSQL et le serveur HTTP pour verifier un echange
//     de trames rendrait le test lourd, donc rarement lance, donc inutile.
//  2. Le deployer seul. Le relais ne touche a aucune base et n'a aucune
//     autorite : il peut vivre ailleurs que le classement, et il n'y a aucune
//     raison de l'y attacher.
package main

import (
	"flag"
	"log/slog"
	"os"

	"nineteen/internal/duel"
)

func main() {
	addr := flag.String("addr", envOr("NINETEEN_DUEL_ADDR", "127.0.0.1:8081"),
		"adresse d'ecoute")
	flag.Parse()

	log := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelInfo}))
	r := duel.New(log)
	if err := r.Listen(*addr); err != nil {
		log.Error("relais arrete", "err", err)
		os.Exit(1)
	}
}

func envOr(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}
