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
	"net/url"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"

	"nineteen/internal/api"
	"nineteen/internal/duel"
	"nineteen/internal/migrations"
	"nineteen/internal/salons"
	"nineteen/internal/store"
	"nineteen/internal/telechargements"
	"nineteen/internal/web"
)

// version — celle du PROJET, recopiee de `CMakeLists.txt`.
//
// Elle etait restee a 15.0.0 pendant deux versions. Le site lit `/api/v1/version`
// pour sa page de telechargement et pour construire les liens de release GitHub :
// il annoncait donc « Version 15.0.0 » et pointait vers des paquets
// `Nineteen-15.0.0-*` qui n'existent pas. Rien ne le disait, parce que rien ne
// confrontait cette ligne a quoi que ce soit.
//
// C'est maintenant le cas : `TestVersionSuitCMake` lit `CMakeLists.txt` et
// echoue si les deux divergent. Le serveur Go ne peut pas lire ce fichier au
// demarrage — le conteneur ne contient que `server/` — donc la copie reste, mais
// elle est desormais CONTROLEE.
const version = "17.0.0"

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
		addr      = flag.String("addr", envOr("NINETEEN_ADDR", ":8080"), "adresse d'écoute")
		dbURL     = flag.String("db", os.Getenv("NINETEEN_DB_URL"), "URL PostgreSQL")
		migrate   = flag.Bool("migrate", true, "appliquer les migrations au démarrage")
		secure    = flag.Bool("secure", envOr("NINETEEN_SECURE", "") != "", "servi derrière HTTPS (cookies Secure, HSTS)")
		logFormat = flag.String("log", envOr("NINETEEN_LOG", "text"), "format de journal : text ou json")
		duelAddr  = flag.String("duel-addr", envOr("NINETEEN_DUEL_ADDR", ""),
			"adresse d'ecoute du relais de duel (vide = pas de duel en direct)")
		// L'adresse du relais TELLE QU'ON L'ANNONCE, distincte de celle
		// ci-dessus comme `-public-url` est distincte de `-addr`, et pour la
		// meme raison : l'une dit ou le processus se pose, l'autre ou le monde
		// le joint. Elle est ce qu'un joueur recoit en creant ou en rejoignant
		// un salon ; sans elle, un code de salon ne mene nulle part, et les
		// routes de salon repondent 503 plutot que d'annoncer une adresse
		// devinee.
		duelPublic = flag.String("duel-public", envOr("NINETEEN_DUEL_PUBLIC", ""),
			"adresse « hote:port » du relais annoncee aux joueurs d'un salon (vide = salons desactives)")
		insecureOK = flag.Bool("insecure-ok", envOr("NINETEEN_INSECURE_OK", "") != "",
			"autoriser l'écoute publique SANS cookies Secure (à n'employer qu'en connaissance de cause)")
		// L'ADRESSE PUBLIQUE, celle que le JEU doit viser.
		//
		// Elle n'a rien à voir avec `-addr`, et c'est tout l'intérêt : `-addr`
		// dit où le processus écoute — « :8080 » dans un conteneur — tandis que
		// celle-ci dit où le monde le joint. Derrière une publication Docker ou
		// un proxy TLS, les deux diffèrent toujours, et le serveur est le seul
		// à connaître la seconde.
		//
		// Ce qu'elle répare : la page de téléchargement livrait un binaire et
		// aucun moyen de savoir quoi lui donner. Le joueur téléchargeait le jeu,
		// et devait deviner l'URL à mettre dans `--server=`. Elle sort
		// maintenant par `/api/v1/version`, et la page l'affiche telle qu'on la
		// tape.
		//
		// Vide par défaut, exactement comme `NINETEEN_RELEASE_PUBLIEE` : un
		// serveur lancé sans rien dire n'affirme rien. La page cache simplement
		// le bloc.
		publicURL = flag.String("public-url", envOr("NINETEEN_PUBLIC_URL", ""),
			"URL publique du serveur, annoncée au joueur pour --server= (vide = rien n'est annoncé)")
		// LE REPERTOIRE DES PAQUETS, que ce serveur sert lui-meme.
		//
		// Ce qui s'y trouve est offert au telechargement, ce qui ne s'y trouve
		// pas n'est pas annonce. Aucune variable ne le decrit, parce qu'une
		// variable peut mentir : la page lit un `os.ReadDir`.
		//
		// Vide par defaut. La page se rabat alors sur la release GitHub si
		// NINETEEN_RELEASE_PUBLIEE=1, et n'offre rien sinon.
		telech = flag.String("telechargements", envOr("NINETEEN_TELECHARGEMENTS", ""),
			"répertoire des paquets du jeu servis par ce serveur (vide = aucun)")
	)
	flag.Parse()

	logger := newLogger(*logFormat)
	slog.SetDefault(logger)

	// Une adresse publique fausse est PIRE que pas d'adresse : elle envoie tous
	// les joueurs de la page vers un serveur qui n'existe pas, et le seul
	// symptôme côté jeu est un classement qui reste local. On la contrôle donc
	// ici, et on refuse de l'annoncer plutôt que de la propager.
	*publicURL = urlPubliqueValide(*publicURL, logger)
	relaisPublic := adresseRelaisValide(*duelPublic, logger)

	// Un relais qui ecoute et que personne n'annonce est le piege exact que
	// `-public-url` a deja pour le site : tout marche, sauf que le joueur n'a
	// aucun moyen d'apprendre l'adresse. On le dit au demarrage, une fois,
	// plutot que de laisser chercher pourquoi les salons repondent 503.
	if *duelAddr != "" && !relaisPublic.Annonce() {
		logger.Warn("le relais ecoute mais n'est annonce a personne : les salons resteront fermes",
			"ecoute", *duelAddr,
			"aide", "poser NINETEEN_DUEL_PUBLIC (ou -duel-public) sur l'adresse « hote:port » "+
				"par laquelle les joueurs joignent ce relais")
	}

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

	// Le depot de paquets. Il est ouvert meme si le repertoire n'existe pas
	// encore : la composition Docker fabrique les paquets EN PARALLELE du
	// serveur, et le site doit rester servi pendant ce temps. Ce qui apparait
	// dans le repertoire apparait sur la page, sans redemarrage.
	depot := telechargements.Ouvrir(*telech)
	if depot.Actif() {
		paquets, _ := depot.Liste()
		logger.Info("paquets servis par ce serveur",
			"répertoire", *telech, "fichiers", len(paquets))
	}

	assets, err := staticHandler()
	if err != nil {
		logger.Error("front embarqué illisible", "err", err)
		os.Exit(1)
	}

	srv := api.New(api.Config{
		Store:     db,
		Logger:    logger,
		Secure:    *secure,
		Version:   version,
		Publiee:   os.Getenv("NINETEEN_RELEASE_PUBLIEE") == "1",
		PublicURL: *publicURL,
		Relais:    relaisPublic,
		Depot:     depot,
		Assets:    assets,
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
	// Les sommes SHA-256, calculees en fond. Trente secondes entre deux tours :
	// le repertoire ne change qu'a une fabrication, et un tour ne relit que la
	// liste tant que rien n'a bouge.
	go depot.Chauffer(ctx, logger, 30*time.Second)

	errCh := make(chan error, 1)

	// Le relais de duel, sur SON port et seulement s'il est demande.
	//
	// Vide par defaut : un serveur de classement n'a aucune raison d'ouvrir un
	// port de plus tant que personne ne joue en direct, et la promesse « rien ne
	// s'ouvre sans qu'on le demande » vaut aussi pour le serveur.
	var relay *duel.Relay
	if *duelAddr != "" {
		relay = duel.New(logger)
		go func() {
			if err := relay.Listen(*duelAddr); err != nil {
				errCh <- err
			}
		}()
		defer func() { _ = relay.Close() }()
	}

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

// urlPubliqueValide contrôle `NINETEEN_PUBLIC_URL` et rend ce qu'on peut
// annoncer — la chaîne vide quand il n'y a rien de sûr à dire.
//
// Trois refus, et chacun correspond à une faute qu'on fait vraiment :
//
//   - PAS D'HÔTE (« http:// », « /api », « arcade.example » sans schéma). Le
//     joueur recopierait une adresse que rien ne peut joindre.
//
//   - UNE BARRE OBLIQUE FINALE. Le client la retire déjà de son côté
//     (`ns_online_init`), donc elle ne casse rien — mais la page afficherait
//     « --server=http://x:8080/ » là où le journal du jeu répondra
//     « http://x:8080 », et deux textes qui diffèrent sur la même chose font
//     douter du bon. On la retire ici plutôt que d'expliquer la différence.
//
//   - `https`. Ce n'est PAS un avertissement de confort : le client refuse
//     explicitement `https://` — `ns_http_parse_url` échoue, et
//     `tests/test_online.c` le cloue (« https est refusé »), parce que le
//     traiter comme du HTTP en clair enverrait le jeton de session sur un port
//     qui ne le comprend pas. Annoncer une adresse `https` au joueur lui
//     donnerait donc à coup sûr une URL que son jeu ne sait pas ouvrir. On la
//     refuse au lieu de la propager, et le message dit quoi faire.
func urlPubliqueValide(brut string, logger *slog.Logger) string {
	brut = strings.TrimSpace(brut)
	if brut == "" {
		return ""
	}

	u, err := url.Parse(brut)
	if err != nil || u.Host == "" {
		logger.Error("NINETEEN_PUBLIC_URL illisible, rien ne sera annoncé au joueur",
			"valeur", brut,
			"aide", "attendu « http://hote:port », schéma et hôte compris")
		return ""
	}

	if u.Scheme == "https" {
		logger.Error("NINETEEN_PUBLIC_URL en https : le jeu ne sait pas l'ouvrir, rien ne sera annoncé",
			"valeur", brut,
			"aide", "le client refuse https (voir ns_http_parse_url) ; annoncer l'adresse "+
				"http joignable par le jeu, ou ne rien annoncer tant que le client ne parle pas TLS")
		return ""
	}
	if u.Scheme != "http" {
		logger.Error("NINETEEN_PUBLIC_URL : schéma inattendu, rien ne sera annoncé",
			"valeur", brut, "schema", u.Scheme, "aide", "seul « http » est utilisable par le jeu")
		return ""
	}

	// Le chemin est vidé : le jeu colle « /api/v1/… » derrière ce qu'on lui
	// donne, donc tout suffixe produirait « http://x/quelquechose/api/v1/games ».
	u.Path = strings.TrimRight(u.Path, "/")
	if u.Path != "" || u.RawQuery != "" || u.Fragment != "" {
		logger.Warn("NINETEEN_PUBLIC_URL : chemin ou paramètres ignorés",
			"valeur", brut, "aide", "le jeu ajoute lui-même « /api/v1/… » derrière l'origine")
		u.Path, u.RawQuery, u.Fragment = "", "", ""
	}

	propre := u.String()
	logger.Info("adresse publique annoncée au joueur", "url", propre)
	return propre
}

// adresseRelaisValide controle l'adresse du relais qu'on ANNONCE aux joueurs
// d'un salon, et rend le zero quand il n'y a rien de sur a dire.
//
// C'est le pendant exact de `urlPubliqueValide` pour l'autre port, et le meme
// raisonnement s'applique : une adresse fausse est PIRE que pas d'adresse. Ici
// elle donnerait a chaque joueur d'un salon un hote et un port ou personne ne
// repond, apres qu'il a obtenu un code, une place et l'accord des autres — le
// mode echouerait donc au dernier moment, et pour tout le monde a la fois.
//
// Trois refus, et chacun correspond a une faute qu'on fait vraiment :
//
//   - UN SCHEMA. Le relais n'est pas du HTTP : c'est un protocole binaire sur
//     TCP (voir l'en-tete de internal/duel/relay.go). « http://x:8081 » n'a pas
//     de sens ici, et le recopier tel quel donnerait un nom d'hote a rallonge.
//
//   - UNE ADRESSE D'ECOUTE. « :8081 », « 0.0.0.0:8081 » et « [::]:8081 » sont
//     ce qu'on ecrit dans `-duel-addr` : elles disent « toutes les interfaces »,
//     ce qui ne designe aucune machine vue de l'exterieur. Les recopier
//     enverrait le client se connecter a lui-meme.
//
//   - UN PORT QUI N'EN EST PAS UN. Absent, non numerique, ou hors de 1..65535.
func adresseRelaisValide(brut string, logger *slog.Logger) api.RelaisPublic {
	brut = strings.TrimSpace(brut)
	if brut == "" {
		return api.RelaisPublic{}
	}

	if strings.Contains(brut, "://") {
		logger.Error("NINETEEN_DUEL_PUBLIC porte un schema : le relais n'est pas du HTTP",
			"valeur", brut, "aide", "attendu « hote:port », sans schema")
		return api.RelaisPublic{}
	}

	hote, port, err := net.SplitHostPort(brut)
	if err != nil {
		logger.Error("NINETEEN_DUEL_PUBLIC illisible, aucun salon ne sera ouvert",
			"valeur", brut, "aide", "attendu « hote:port », par exemple arcade.example:8081")
		return api.RelaisPublic{}
	}

	switch hote {
	case "", "0.0.0.0", "::", "[::]", "*":
		logger.Error("NINETEEN_DUEL_PUBLIC est une adresse d'ecoute, pas une adresse a annoncer",
			"valeur", brut,
			"aide", "« toutes les interfaces » ne designe aucune machine vue du joueur ; "+
				"annoncer le nom ou l'adresse par laquelle il joint ce relais")
		return api.RelaisPublic{}
	}

	n, err := strconv.Atoi(port)
	if err != nil || n < 1 || n > 65535 {
		logger.Error("NINETEEN_DUEL_PUBLIC : port invalide, aucun salon ne sera ouvert",
			"valeur", brut, "port", port)
		return api.RelaisPublic{}
	}

	logger.Info("relais annonce aux joueurs de salon", "hote", hote, "port", n)
	return api.RelaisPublic{Hote: hote, Port: n}
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
		//
		// LE MÉDIA A SA PROPRE DURÉE, et il en a besoin. Depuis que la page
		// montre le jeu, elle référence une cinquantaine de fichiers — deux
		// formats de vidéo par jeu, dix-neuf photos de bornes, huit vues de
		// salle — et le `no-cache` générique forçait une revalidation pour
		// CHACUN à chaque visite. Les réponses étaient des 304, donc le
		// gaspillage n'était pas en octets mais en allers-retours : cinquante
		// requêtes conditionnelles avant que la page ne se peigne.
		//
		// La durée reste modérée, et c'est délibéré : ces noms de fichiers ne
		// portent pas d'empreinte de contenu, donc `tools/site-media.py` peut
		// réécrire `salle.mp4` sans que son nom change. Une journée est le
		// compromis : assez pour qu'une visite de retour ne redemande rien,
		// assez peu pour qu'un média régénéré atteigne tout le monde le
		// lendemain sans purge manuelle.
		path := r.URL.Path
		switch {
		case strings.HasSuffix(path, ".css"), strings.HasSuffix(path, ".js"):
			// LE SCRIPT ET LA FEUILLE REVALIDENT, ET C'EST UN DÉFAUT MESURÉ QUI
			// L'A IMPOSÉ.
			//
			// Ils étaient à `max-age=3600`. Or `index.html` est en `no-cache` :
			// après un déploiement, un visiteur de retour recevait le NOUVEAU
			// document et l'ANCIEN script, pendant une heure. Ce n'est pas une
			// hypothèse — c'est arrivé sur cette machine en ajoutant le drapeau
			// « publiée » : le conteneur servait bien le nouveau `app.js`
			// (vérifié au `curl`), et la page continuait d'exécuter l'ancien,
			// même après un rechargement forcé.
			//
			// Une page dont le document et le script peuvent dater de deux
			// versions différentes n'a aucun comportement défini. Et ces noms
			// ne portent pas d'empreinte de contenu, donc rien ne les
			// distingue d'une version à l'autre.
			//
			// `no-cache` ne veut pas dire « ne garde rien » mais « redemande
			// avant de servir ». J'ai d'abord écrit ici que la revalidation
			// coûterait un 304 sans corps, puis je l'ai mesurée : la réponse
			// ne porte NI `Last-Modified` NI `ETag`. Les fichiers sont servis
			// depuis un `embed.FS`, dont chaque entrée a une date de
			// modification nulle, et `http.FileServer` n'émet alors aucun
			// validateur. Le coût réel est donc le corps entier à chaque
			// visite : 18 Kio de script et 24 Kio de feuille, une fois. C'est
			// le prix qu'on paie, et il vaut mieux que la page cassée.
			//
			// Le média, lui, garde sa journée : il ne peut pas se contredire
			// avec le document qui le nomme, et c'est là que sont les mégaoctets.
			w.Header().Set("Cache-Control", "no-cache")
		case strings.HasPrefix(path, "/media/"), strings.HasPrefix(path, "/img/"),
			strings.HasPrefix(path, "/fonts/"):
			// Le manifeste est l'exception dans son propre dossier : c'est lui
			// qui annonce les autres, donc le mettre en cache aussi longtemps
			// qu'eux ferait afficher l'ancienne galerie avec les nouveaux
			// fichiers.
			if strings.HasSuffix(path, ".json") {
				w.Header().Set("Cache-Control", "no-cache")
			} else {
				w.Header().Set("Cache-Control", "public, max-age=86400")
			}
		default:
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
			// Les salons suivent le rythme rapide pour la même raison que la
			// présence, et avec la même conséquence limitée : les lectures
			// filtrent déjà sur `battu_a`, donc une place morte n'est jamais
			// servie et ce passage-ci ne fait que garder les deux tables à la
			// taille de ce qui se joue réellement. Un service où il ne
			// tournerait jamais afficherait exactement la même chose.
			//
			// Les deux durées viennent des règles, pas d'ici : `TTLOccupant`
			// est justifiée par le rythme du client, `RetentionFini` par la
			// durée d'une manche. Voir internal/salons.
			if places, salles, err := db.PurgerSalons(ctx, time.Now(),
				salons.TTLOccupant, salons.RetentionFini); err != nil {
				logger.Warn("purge des salons", "err", err)
			} else if places > 0 || salles > 0 {
				logger.Info("salons purgés", "places", places, "salons", salles)
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
