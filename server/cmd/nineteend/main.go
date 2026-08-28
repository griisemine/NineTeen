// nineteend — le serveur de Nineteen : API de jeu et site web, en un binaire.
//
// Tout est embarqué : le front, les migrations, la configuration par défaut. On
// dépose un fichier, on lui donne une URL de base de données, et il tourne.
// C'est l'inverse de l'original, qui supposait un hébergement mutualisé avec la
// bonne version de PHP, les bons modules et une arborescence exacte.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io/fs"
	"log/slog"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"nineteen/internal/api"
	"nineteen/internal/migrations"
	"nineteen/internal/store"
	"nineteen/internal/web"
)

const version = "15.0.0"

// isLoopbackAddr dit si une adresse d'ecoute ne sort pas de la machine.
//
// Une adresse sans hote — « :8080 » — ecoute sur TOUTES les interfaces : c'est
// le defaut, et c'est le cas dangereux, pas le cas local.
func isLoopbackAddr(addr string) bool {
	host, _, err := net.SplitHostPort(addr)
	if err != nil {
		host = addr
	}
	if host == "" {
		return false // « :8080 » : toutes les interfaces
	}
	if host == "localhost" {
		return true
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func main() {
	var (
		addr       = flag.String("addr", envOr("NINETEEN_ADDR", ":8080"), "adresse d'écoute")
		dbURL      = flag.String("db", os.Getenv("NINETEEN_DB_URL"), "URL PostgreSQL")
		migrate    = flag.Bool("migrate", true, "appliquer les migrations au démarrage")
		secure     = flag.Bool("secure", envOr("NINETEEN_SECURE", "") != "", "servi derrière HTTPS (cookies Secure, HSTS)")
		logFormat  = flag.String("log", envOr("NINETEEN_LOG", "text"), "format de journal : text ou json")
		insecureOK = flag.Bool("insecure-ok", envOr("NINETEEN_INSECURE_OK", "") != "",
			"autoriser l'écoute publique SANS cookies Secure (à n'employer qu'en connaissance de cause)")
	)
	flag.Parse()

	logger := newLogger(*logFormat)
	slog.SetDefault(logger)

	// Une adresse publique sans `-secure` envoie le cookie de session en clair.
	//
	// Le drapeau existait, rien ne verifiait qu'on l'avait mis. Un oubli dans un
	// `docker run` suffisait donc a servir la session sans l'attribut `Secure`,
	// sur une adresse joignable — et le seul symptome aurait ete son absence
	// dans un en-tete que personne ne lit. On refuse.
	//
	// L'ecoute en boucle locale est exemptee : c'est le developpement, et un
	// navigateur IGNORE un cookie `Secure` recu sur `http://`, donc l'exiger la
	// rendrait la session impossible a etablir. `-insecure-ok` reste pour qui
	// termine son TLS ailleurs et le sait.
	if !*secure && !*insecureOK && !isLoopbackAddr(*addr) {
		logger.Error("ecoute publique sans cookies Secure",
			"addr", *addr,
			"aide", "passer -secure (ou NINETEEN_SECURE=1) quand un proxy TLS est devant ; "+
				"sinon ecouter sur 127.0.0.1, ou assumer avec -insecure-ok")
		os.Exit(1)
	}

	if *dbURL == "" {
		logger.Error("aucune base configurée",
			"aide", "définir NINETEEN_DB_URL ou passer -db "+
				"(ex. postgres://nineteen:motdepasse@localhost:5432/nineteen?sslmode=require)")
		os.Exit(1)
	}

	// Le mot de passe ne doit jamais apparaître dans les journaux, y compris
	// lors d'une erreur de connexion.
	logger.Info("démarrage", "version", version, "addr", *addr, "db", redactURL(*dbURL))

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	db, err := openWithRetry(ctx, *dbURL, logger)
	if err != nil {
		logger.Error("base injoignable", "err", err)
		os.Exit(1)
	}
	defer db.Close()

	if *migrate {
		if err := runMigrations(ctx, db, logger); err != nil {
			logger.Error("migrations", "err", err)
			os.Exit(1)
		}
	}

	assets, err := staticHandler()
	if err != nil {
		logger.Error("front embarqué illisible", "err", err)
		os.Exit(1)
	}

	srv := api.New(api.Config{
		Store:   db,
		Logger:  logger,
		Secure:  *secure,
		Version: version,
		Assets:  assets,
	})

	httpServer := &http.Server{
		Addr:    *addr,
		Handler: withRecovery(withAccessLog(srv, logger), logger),

		// Délais explicites. Sans eux, une connexion lente ou malveillante
		// immobilise une goroutine et un descripteur de fichier indéfiniment —
		// le déni de service le moins coûteux qui soit.
		ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout:       30 * time.Second,
		WriteTimeout:      60 * time.Second,
		IdleTimeout:       2 * time.Minute,
		MaxHeaderBytes:    1 << 16,
	}

	go housekeeping(ctx, db, logger)

	errCh := make(chan error, 1)
	go func() {
		logger.Info("à l'écoute", "addr", *addr)
		if err := httpServer.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			errCh <- err
		}
	}()

	select {
	case err := <-errCh:
		logger.Error("serveur arrêté", "err", err)
		os.Exit(1)
	case <-ctx.Done():
		logger.Info("arrêt demandé, fin des requêtes en cours")
	}

	// Arrêt en douceur : les requêtes en vol se terminent, les nouvelles sont
	// refusées. Sans cela, un déploiement coupe des parties en cours de
	// soumission.
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	if err := httpServer.Shutdown(shutdownCtx); err != nil {
		logger.Warn("arrêt forcé", "err", err)
	}
	logger.Info("arrêté proprement")
}

/* ========================================================================== */

func newLogger(format string) *slog.Logger {
	opts := &slog.HandlerOptions{Level: slog.LevelInfo}
	if strings.EqualFold(format, "json") {
		return slog.New(slog.NewJSONHandler(os.Stdout, opts))
	}
	return slog.New(slog.NewTextHandler(os.Stdout, opts))
}

func envOr(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

// redactURL masque le mot de passe d'une URL de connexion.
func redactURL(raw string) string {
	at := strings.LastIndex(raw, "@")
	scheme := strings.Index(raw, "://")
	if at < 0 || scheme < 0 || at < scheme {
		return raw
	}
	return raw[:scheme+3] + "***@" + raw[at+1:]
}

// openWithRetry attend que la base soit prête.
//
// Nécessaire en conteneur : le serveur démarre souvent avant PostgreSQL, et
// échouer immédiatement transformerait un démarrage normal en boucle de
// redémarrage.
func openWithRetry(ctx context.Context, url string, logger *slog.Logger) (*store.Store, error) {
	var lastErr error
	for attempt := 1; attempt <= 15; attempt++ {
		db, err := store.Open(ctx, url)
		if err == nil {
			return db, nil
		}
		lastErr = err
		wait := time.Duration(attempt) * 500 * time.Millisecond
		if wait > 5*time.Second {
			wait = 5 * time.Second
		}
		logger.Warn("base pas encore prête", "tentative", attempt, "attente", wait)
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-time.After(wait):
		}
	}
	return nil, lastErr
}

// runMigrations applique les fichiers SQL embarqués, dans l'ordre.
//
// Chaque migration est enregistrée pour ne pas être rejouée. Volontairement
// minimaliste : pas de dépendance à un outil externe, donc rien à installer sur
// le serveur.
func runMigrations(ctx context.Context, db *store.Store, logger *slog.Logger) error {
	pool := db.Pool()

	if _, err := pool.Exec(ctx, `
		CREATE TABLE IF NOT EXISTS schema_migrations (
		    name       text PRIMARY KEY,
		    applied_at timestamptz NOT NULL DEFAULT now()
		)`); err != nil {
		return fmt.Errorf("table des migrations : %w", err)
	}

	entries, err := fs.Glob(migrations.FS, "*.sql")
	if err != nil {
		return err
	}
	// fs.Glob renvoie déjà un ordre lexicographique, sur lequel repose la
	// numérotation 0001_, 0002_…

	for _, name := range entries {
		var exists bool
		if err := pool.QueryRow(ctx,
			`SELECT EXISTS(SELECT 1 FROM schema_migrations WHERE name = $1)`, name).Scan(&exists); err != nil {
			return fmt.Errorf("état de %s : %w", name, err)
		}
		if exists {
			continue
		}

		sqlBytes, err := migrations.FS.ReadFile(name)
		if err != nil {
			return err
		}

		// Une migration s'applique en une transaction : soit elle passe
		// entièrement, soit la base reste dans son état précédent.
		tx, err := pool.Begin(ctx)
		if err != nil {
			return err
		}
		if _, err := tx.Exec(ctx, string(sqlBytes)); err != nil {
			_ = tx.Rollback(ctx)
			return fmt.Errorf("migration %s : %w", name, err)
		}
		if _, err := tx.Exec(ctx,
			`INSERT INTO schema_migrations (name) VALUES ($1)`, name); err != nil {
			_ = tx.Rollback(ctx)
			return err
		}
		if err := tx.Commit(ctx); err != nil {
			return err
		}
		logger.Info("migration appliquée", "name", name)
	}
	return nil
}

// staticHandler sert le front embarqué.
func staticHandler() (http.Handler, error) {
	sub, err := fs.Sub(web.FS, "assets")
	if err != nil {
		return nil, err
	}
	fileServer := http.FileServer(http.FS(sub))

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// Les fichiers versionnés peuvent être mis en cache longtemps ; le HTML
		// ne doit pas l'être, sinon une mise à jour du site n'atteint jamais
		// les visiteurs déjà venus.
		if strings.HasSuffix(r.URL.Path, ".css") || strings.HasSuffix(r.URL.Path, ".js") {
			w.Header().Set("Cache-Control", "public, max-age=3600")
		} else {
			w.Header().Set("Cache-Control", "no-cache")
		}
		fileServer.ServeHTTP(w, r)
	}), nil
}

// housekeeping purge périodiquement ce qui n'a plus lieu d'être.
//
// Deux rythmes, parce que deux durées de vie. Les sessions et les compteurs se
// comptent en heures ; la PRÉSENCE se compte en secondes — un joueur qui ferme
// le jeu n'envoie rien de plus, et sa ligne n'a plus de sens douze secondes
// après. La lecture filtre déjà sur `seen_at`, donc une ligne morte n'est
// jamais servie ; ce passage-ci ne fait que garder la table à sa taille, qui
// est le nombre de joueurs réellement présents.
func housekeeping(ctx context.Context, db *store.Store, logger *slog.Logger) {
	slow := time.NewTicker(time.Hour)
	defer slow.Stop()
	fast := time.NewTicker(time.Minute)
	defer fast.Stop()

	// Large devant le TTL applicatif : on efface ce qui est mort depuis
	// longtemps, pas ce qui vient d'expirer. Un client dont la requête traîne
	// ne doit pas voir sa ligne disparaître sous lui.
	const presenceKeep = 5 * time.Minute

	for {
		select {
		case <-ctx.Done():
			return

		case <-fast.C:
			if err := db.PurgePresence(ctx, presenceKeep); err != nil {
				logger.Warn("purge de la présence", "err", err)
			}

		case <-slow.C:
			if n, err := db.PurgeExpiredSessions(ctx); err != nil {
				logger.Warn("purge des sessions", "err", err)
			} else if n > 0 {
				logger.Info("sessions expirées purgées", "n", n)
			}
			if err := db.PurgeRateLimits(ctx); err != nil {
				logger.Warn("purge des compteurs", "err", err)
			}
		}
	}
}

/* ========================================================================== */
/* Intergiciels                                                               */
/* ========================================================================== */

type statusRecorder struct {
	http.ResponseWriter
	status int
	bytes  int
}

func (s *statusRecorder) WriteHeader(code int) {
	s.status = code
	s.ResponseWriter.WriteHeader(code)
}

func (s *statusRecorder) Write(b []byte) (int, error) {
	if s.status == 0 {
		s.status = http.StatusOK
	}
	n, err := s.ResponseWriter.Write(b)
	s.bytes += n
	return n, err
}

func withAccessLog(next http.Handler, logger *slog.Logger) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		rec := &statusRecorder{ResponseWriter: w}
		next.ServeHTTP(rec, r)

		level := slog.LevelInfo
		if rec.status >= 500 {
			level = slog.LevelError
		} else if rec.status >= 400 {
			level = slog.LevelWarn
		}
		// Le chemin est journalisé, jamais la chaîne de requête : elle peut
		// contenir des valeurs qu'on ne veut pas conserver.
		logger.Log(r.Context(), level, "requête",
			"method", r.Method, "path", r.URL.Path,
			"status", rec.status, "bytes", rec.bytes,
			"ms", time.Since(start).Milliseconds())
	})
}

// withRecovery évite qu'une panique dans un gestionnaire n'emporte tout le
// serveur, ce qui déconnecterait tous les joueurs pour un bug isolé.
func withRecovery(next http.Handler, logger *slog.Logger) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer func() {
			if rec := recover(); rec != nil {
				logger.Error("panique dans un gestionnaire",
					"path", r.URL.Path, "panic", rec)
				http.Error(w, `{"ok":false,"error":"erreur interne"}`, http.StatusInternalServerError)
			}
		}()
		next.ServeHTTP(w, r)
	})
}
