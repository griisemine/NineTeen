// Package api — les points d'entrée HTTP et leurs protections.
//
// Chaque protection ici répond à un constat précis de l'audit du site d'origine.
// Le détail est en commentaire à l'endroit concerné plutôt que dans un document
// séparé : c'est là qu'on le lira quand on modifiera le code.
package api

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"math"
	"net"
	"net/http"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"

	"nineteen/internal/auth"
	"nineteen/internal/runs"
	"nineteen/internal/store"
)

const (
	sessionCookie = "ns_session"
	csrfCookie    = "ns_csrf"
	csrfHeader    = "X-Nineteen-CSRF"
	authHeader    = "Authorization"

	sessionTTL = 30 * 24 * time.Hour
)

type Server struct {
	store   *store.Store
	log     *slog.Logger
	mux     *http.ServeMux
	secure  bool // cookies marqués Secure : vrai dès que le service est en HTTPS
	version string
	// Le paquet de cette version est-il PUBLIE ? Voir handleVersion : sans ça,
	// la page bâtissait trois liens de téléchargement vers une release qui
	// n'existe pas.
	publiee bool
	// L'adresse PUBLIQUE du serveur, celle que le joueur passe à `--server=`.
	// Vide quand `NINETEEN_PUBLIC_URL` n'est pas posée : la page cache alors le
	// bloc plutôt que d'annoncer une adresse devinée. Contrôlée avant d'arriver
	// ici — voir `urlPubliqueValide` dans cmd/nineteend.
	publicURL string
	assets    http.Handler
}

type Config struct {
	Store     *store.Store
	Logger    *slog.Logger
	Secure    bool
	Version   string
	Publiee   bool
	PublicURL string
	Assets    http.Handler
}

func New(cfg Config) *Server {
	s := &Server{
		store:     cfg.Store,
		log:       cfg.Logger,
		mux:       http.NewServeMux(),
		secure:    cfg.Secure,
		version:   cfg.Version,
		publiee:   cfg.Publiee,
		publicURL: cfg.PublicURL,
		assets:    cfg.Assets,
	}
	s.routes()
	return s
}

func (s *Server) routes() {
	// Le routage par motif de Go 1.22 rend la méthode explicite : un POST sur
	// une route en lecture seule renvoie 405 au lieu d'être servi.
	s.mux.HandleFunc("POST /api/v1/auth/register", s.handleRegister)
	s.mux.HandleFunc("POST /api/v1/auth/login", s.handleLogin)
	s.mux.HandleFunc("POST /api/v1/auth/logout", s.handleLogout)
	s.mux.HandleFunc("POST /api/v1/auth/logout-all", s.handleLogoutAll)
	s.mux.HandleFunc("GET /api/v1/me", s.handleMe)

	s.mux.HandleFunc("GET /api/v1/games", s.handleGames)
	s.mux.HandleFunc("GET /api/v1/leaderboard", s.handleLeaderboard)

	s.mux.HandleFunc("POST /api/v1/runs", s.handleRunBegin)
	s.mux.HandleFunc("POST /api/v1/runs/{id}/submit", s.handleRunSubmit)
	s.mux.HandleFunc("POST /api/v1/runs/{id}/inputs", s.handleRunInputs)

	// Le temps réel. La présence n'exige AUCUN compte — se montrer dans la
	// salle n'est pas une action privilégiée — et les fantômes se lisent
	// librement pour la même raison que le classement : on doit pouvoir
	// affronter quelqu'un sans s'inscrire.
	s.mux.HandleFunc("POST /api/v1/presence", s.handlePresence)
	s.mux.HandleFunc("GET /api/v1/ghosts", s.handleGhosts)
	s.mux.HandleFunc("GET /api/v1/ghosts/{id}", s.handleGhost)

	s.mux.HandleFunc("GET /api/v1/version", s.handleVersion)
	s.mux.HandleFunc("GET /api/v1/health", s.handleHealth)

	if s.assets != nil {
		s.mux.Handle("GET /", s.assets)
	}
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	s.securityHeaders(w)
	s.mux.ServeHTTP(w, r)
}

/* ========================================================================== */
/* En-têtes de sécurité                                                       */
/* ========================================================================== */

// securityHeaders — aucun de ces en-têtes n'existait sur le site d'origine.
func (s *Server) securityHeaders(w http.ResponseWriter) {
	h := w.Header()

	// Politique de contenu stricte : pas de script en ligne, pas de ressource
	// externe. C'est la protection de fond contre le XSS, en complément de
	// l'échappement — l'original n'avait ni l'un ni l'autre.
	h.Set("Content-Security-Policy",
		"default-src 'self'; "+
			"script-src 'self'; "+
			"style-src 'self'; "+
			"img-src 'self' data:; "+
			"font-src 'self'; "+
			"connect-src 'self'; "+
			"frame-ancestors 'none'; "+
			"base-uri 'none'; "+
			"form-action 'self'; "+
			"object-src 'none'")

	h.Set("X-Content-Type-Options", "nosniff")
	h.Set("Referrer-Policy", "strict-origin-when-cross-origin")
	h.Set("X-Frame-Options", "DENY")
	h.Set("Permissions-Policy", "geolocation=(), microphone=(), camera=(), payment=()")
	h.Set("Cross-Origin-Opener-Policy", "same-origin")
	h.Set("Cross-Origin-Resource-Policy", "same-origin")

	if s.secure {
		h.Set("Strict-Transport-Security", "max-age=63072000; includeSubDomains; preload")
	}
}

/* ========================================================================== */
/* Réponses                                                                   */
/* ========================================================================== */

func writeJSON(w http.ResponseWriter, code int, payload any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(code)
	if payload != nil {
		_ = json.NewEncoder(w).Encode(payload)
	}
}

// fail journalise le détail réel côté serveur et ne renvoie au client qu'un
// message générique.
//
// L'original faisait l'inverse : `echo $link->error` renvoyait le message de
// MySQL au navigateur, ce qui livrait le nom des tables, des colonnes, et
// souvent la requête elle-même — de quoi construire une injection sans tâtonner.
func (s *Server) fail(w http.ResponseWriter, r *http.Request, code int, public string, err error) {
	if err != nil {
		s.log.Warn("requête refusée",
			"path", r.URL.Path, "method", r.Method, "code", code,
			"ip", clientIP(r), "err", err)
	}
	writeJSON(w, code, map[string]any{"ok": false, "error": public})
}

/* ========================================================================== */
/* Identification du client                                                   */
/* ========================================================================== */

// clientIP renvoie l'adresse du pair.
//
// Volontairement `RemoteAddr` et non `X-Forwarded-For` : l'original faisait
// confiance à cet en-tête, que n'importe quel client peut écrire. Contourner la
// limitation par IP revenait donc à envoyer un en-tête différent à chaque essai.
// Derrière un reverse proxy de confiance, c'est à ce proxy de réécrire
// RemoteAddr — la décision ne revient pas à l'application.
func clientIP(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

/* ========================================================================== */
/* Limitation de débit                                                        */
/* ========================================================================== */

func (s *Server) rateLimit(w http.ResponseWriter, r *http.Request, action string, limit int, window time.Duration) bool {
	key := action + "|" + clientIP(r)
	allowed, retry, err := s.store.RateLimit(r.Context(), key, limit, window)
	if err != nil {
		// Un incident sur la table de limitation ne doit pas rendre le service
		// indisponible : on laisse passer, mais on le signale fort.
		s.log.Error("limitation de débit indisponible", "err", err)
		return true
	}
	if !allowed {
		w.Header().Set("Retry-After", strconv.Itoa(int(retry.Seconds())+1))
		s.fail(w, r, http.StatusTooManyRequests, "trop de tentatives, réessayez plus tard", nil)
		return false
	}
	return true
}

/* ========================================================================== */
/* Sessions et CSRF                                                           */
/* ========================================================================== */

func (s *Server) issueSession(w http.ResponseWriter, r *http.Request, playerID int64) (string, error) {
	clear, hashed, err := auth.NewSessionKey()
	if err != nil {
		return "", err
	}
	if err := s.store.CreateSession(r.Context(), playerID, hashed,
		clientIP(r), r.UserAgent(), sessionTTL); err != nil {
		return "", err
	}

	// #nosec G124 -- `Secure` est piloté par `s.secure`, et l'écrire en dur
	// casserait le développement local au lieu de le sécuriser : un navigateur
	// IGNORE purement et simplement un cookie `Secure` reçu sur `http://`, donc
	// un `true` constant rendrait la session impossible à établir hors HTTPS et
	// pousserait à désactiver le cookie entièrement. `s.secure` vient de
	// `-secure` / `NINETEEN_SECURE`, et `serveDeployment` refuse de démarrer en
	// production sans lui (voir cmd/nineteend). `HttpOnly` et `SameSite=Strict`
	// sont, eux, inconditionnels.
	http.SetCookie(w, &http.Cookie{
		Name:     sessionCookie,
		Value:    clear,
		Path:     "/",
		HttpOnly: true, // inaccessible au JavaScript : un XSS ne vole pas la session
		Secure:   s.secure,
		SameSite: http.SameSiteStrictMode, // le navigateur ne l'envoie pas depuis un autre site
		MaxAge:   int(sessionTTL.Seconds()),
	})

	// Jeton CSRF en double soumission : le cookie est lisible par le script du
	// site, qui le recopie dans un en-tête. Un site tiers peut faire envoyer le
	// cookie au navigateur, mais ne peut pas lire sa valeur pour reproduire
	// l'en-tête. L'original n'avait aucun jeton CSRF.
	token := make([]byte, 32)
	if _, err := rand.Read(token); err != nil {
		return "", err
	}
	csrf := base64.RawURLEncoding.EncodeToString(token)
	// #nosec G124 -- même raison que ci-dessus. `HttpOnly` est FAUX ici et c'est
	// voulu : le jeton CSRF en double soumission doit être lisible par le script
	// du site pour être recopié dans un en-tête. C'est tout le mécanisme.
	http.SetCookie(w, &http.Cookie{
		Name:     csrfCookie,
		Value:    csrf,
		Path:     "/",
		HttpOnly: false,
		Secure:   s.secure,
		SameSite: http.SameSiteStrictMode,
		MaxAge:   int(sessionTTL.Seconds()),
	})
	return clear, nil
}

func (s *Server) clearSessionCookies(w http.ResponseWriter) {
	for _, name := range []string{sessionCookie, csrfCookie} {
		// #nosec G124 -- cookies d'EFFACEMENT : `MaxAge: -1` et une valeur vide.
		// Leurs attributs doivent correspondre à ceux de la pose, sans quoi le
		// navigateur les considère comme d'autres cookies et ne supprime rien.
		http.SetCookie(w, &http.Cookie{
			Name: name, Value: "", Path: "/", MaxAge: -1,
			Secure: s.secure, SameSite: http.SameSiteStrictMode,
			HttpOnly: name == sessionCookie,
		})
	}
}

// sessionKeyFromRequest accepte le cookie (navigateur) ou l'en-tête Bearer
// (client de jeu, qui n'a pas de gestionnaire de cookies).
func sessionKeyFromRequest(r *http.Request) string {
	if c, err := r.Cookie(sessionCookie); err == nil && c.Value != "" {
		return c.Value
	}
	h := r.Header.Get(authHeader)
	if after, ok := strings.CutPrefix(h, "Bearer "); ok {
		return strings.TrimSpace(after)
	}
	return ""
}

func (s *Server) authenticate(r *http.Request) (*store.Session, error) {
	key := sessionKeyFromRequest(r)
	if key == "" {
		return nil, errors.New("aucune session fournie")
	}
	return s.store.SessionByKeyHash(r.Context(), auth.HashSessionKey(key))
}

// requireAuth résout la session et, pour les méthodes qui modifient l'état,
// vérifie le jeton CSRF.
func (s *Server) requireAuth(w http.ResponseWriter, r *http.Request) (*store.Session, bool) {
	sess, err := s.authenticate(r)
	if err != nil {
		s.fail(w, r, http.StatusUnauthorized, "authentification requise", err)
		return nil, false
	}

	// Le CSRF ne concerne que les requêtes portées par un cookie : un client
	// utilisant l'en-tête Bearer n'est pas exposé, puisqu'un site tiers ne peut
	// pas le faire ajouter par le navigateur.
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		if _, viaCookie := r.Cookie(sessionCookie); viaCookie == nil {
			if !s.checkCSRF(r) {
				s.fail(w, r, http.StatusForbidden, "jeton de sécurité invalide", errors.New("CSRF"))
				return nil, false
			}
		}
	}
	return sess, true
}

func (s *Server) checkCSRF(r *http.Request) bool {
	c, err := r.Cookie(csrfCookie)
	if err != nil || c.Value == "" {
		return false
	}
	sent := r.Header.Get(csrfHeader)
	if sent == "" {
		return false
	}
	return subtle.ConstantTimeCompare([]byte(c.Value), []byte(sent)) == 1
}

/* ========================================================================== */
/* Authentification                                                           */
/* ========================================================================== */

type registerRequest struct {
	Username string `json:"username"`
	Email    string `json:"email"`
	Password string `json:"password"`
}

func (s *Server) handleRegister(w http.ResponseWriter, r *http.Request) {
	if !s.rateLimit(w, r, "register", 5, time.Hour) {
		return
	}

	var req registerRequest
	if !decodeJSON(w, r, s, &req) {
		return
	}

	req.Username = strings.TrimSpace(req.Username)
	req.Email = strings.TrimSpace(req.Email)

	if err := auth.ValidateUsername(req.Username); err != nil {
		s.fail(w, r, http.StatusBadRequest, err.Error(), nil)
		return
	}
	if err := auth.ValidatePassword(req.Password); err != nil {
		s.fail(w, r, http.StatusBadRequest,
			"mot de passe trop faible : au moins 12 caractères et 5 caractères distincts", nil)
		return
	}

	hash, err := auth.HashPassword(req.Password)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "création impossible", err)
		return
	}

	player, err := s.store.CreatePlayer(r.Context(), req.Username, req.Email, hash)
	if errors.Is(err, store.ErrAlreadyExists) {
		s.fail(w, r, http.StatusConflict, "ce nom ou cet e-mail est déjà pris", nil)
		return
	}
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "création impossible", err)
		return
	}

	key, err := s.issueSession(w, r, player.ID)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "session impossible", err)
		return
	}

	s.log.Info("compte créé", "player", player.Username, "ip", clientIP(r))
	writeJSON(w, http.StatusCreated, map[string]any{
		"ok": true, "username": player.Username, "sessionKey": key,
	})
}

type loginRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

func (s *Server) handleLogin(w http.ResponseWriter, r *http.Request) {
	// Deux limites : par IP pour freiner un balayage, et plus loin par compte
	// pour qu'un attaquant distribué ne puisse pas cibler un joueur précis.
	if !s.rateLimit(w, r, "login", 20, 15*time.Minute) {
		return
	}

	var req loginRequest
	if !decodeJSON(w, r, s, &req) {
		return
	}
	req.Username = strings.TrimSpace(req.Username)

	if !s.rateLimit(w, r, "login-account|"+strings.ToLower(req.Username), 10, 15*time.Minute) {
		return
	}

	player, err := s.store.PlayerByUsername(r.Context(), req.Username)
	if err != nil {
		// Même message et même coût qu'un mot de passe faux : sinon le temps de
		// réponse révèle quels comptes existent.
		_, _ = auth.VerifyPassword(req.Password,
			"$argon2id$v=19$m=65536,t=2,p=2$YWFhYWFhYWFhYWFhYWFhYQ$"+
				"Y29tcGFyYWlzb24gZmFjdGljZSBwb3VyIGxlIHRlbXBz")
		s.fail(w, r, http.StatusUnauthorized, "identifiant ou mot de passe incorrect", nil)
		return
	}
	if player.Disabled {
		s.fail(w, r, http.StatusForbidden, "compte désactivé", nil)
		return
	}

	ok, err := auth.VerifyPassword(req.Password, player.PasswordHash)
	if err != nil || !ok {
		s.fail(w, r, http.StatusUnauthorized, "identifiant ou mot de passe incorrect", err)
		return
	}

	key, err := s.issueSession(w, r, player.ID)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "session impossible", err)
		return
	}
	_ = s.store.TouchPlayer(r.Context(), player.ID)

	s.log.Info("connexion", "player", player.Username, "ip", clientIP(r))
	writeJSON(w, http.StatusOK, map[string]any{
		"ok": true, "username": player.Username, "sessionKey": key,
	})
}

func (s *Server) handleLogout(w http.ResponseWriter, r *http.Request) {
	if key := sessionKeyFromRequest(r); key != "" {
		_ = s.store.RevokeSession(r.Context(), auth.HashSessionKey(key))
	}
	s.clearSessionCookies(w)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

// handleLogoutAll révoque toutes les sessions du joueur authentifié.
//
// C'est le remplaçant de disconnectAll.php, qui exécutait
// `DELETE FROM nineteen_session WHERE 1` en accès anonyme : visiter une URL
// suffisait à déconnecter tous les joueurs du jeu. Ici l'action exige une
// session valide et ne touche que le compte concerné.
func (s *Server) handleLogoutAll(w http.ResponseWriter, r *http.Request) {
	sess, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	if err := s.store.RevokePlayerSessions(r.Context(), sess.PlayerID); err != nil {
		s.fail(w, r, http.StatusInternalServerError, "opération impossible", err)
		return
	}
	s.clearSessionCookies(w)
	s.log.Info("toutes les sessions révoquées", "player", sess.Username)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

func (s *Server) handleMe(w http.ResponseWriter, r *http.Request) {
	sess, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	scores, err := s.store.PlayerScores(r.Context(), sess.PlayerID)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "lecture impossible", err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"ok": true, "username": sess.Username, "scores": scores,
		"expiresAt": sess.ExpiresAt,
	})
}

/* ========================================================================== */
/* Jeux et classement                                                         */
/* ========================================================================== */

func (s *Server) handleGames(w http.ResponseWriter, r *http.Request) {
	games, err := s.store.Games(r.Context())
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "lecture impossible", err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "games": games})
}

func (s *Server) handleLeaderboard(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()

	// Les bornes sont contraintes ici ET dans store.Leaderboard. La duplication
	// est volontaire : l'appelant peut changer, la requête SQL doit rester sûre
	// quel que soit ce qu'on lui passe.
	gameID := gameIDParam(q.Get("game"))
	limit := parseBoundedInt(q.Get("limit"), 20, 1, 100)
	offset := parseBoundedInt(q.Get("offset"), 0, 0, 10000)

	entries, err := s.store.Leaderboard(r.Context(), gameID, limit, offset)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "classement indisponible", err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"ok": true, "game": gameID, "entries": entries,
	})
}

/* ========================================================================== */
/* Parties                                                                    */
/* ========================================================================== */

type runBeginRequest struct {
	Game string `json:"game"`
	// Le fantôme qu'on veut affronter, ou vide. Sa présence change UNE chose :
	// la graine n'est plus tirée au hasard, c'est celle de sa partie.
	//
	// C'est ce qui fait d'un duel un duel. Deux joueurs sur deux graines
	// différentes ne jouent pas la même partie, ils jouent deux parties et
	// comparent deux nombres — ce qui est un classement, et on en a déjà un.
	//
	// Rien n'est affaibli par là. Une graine n'est pas un secret : c'est
	// justement ce qui doit être partagé pour que le duel existe. Le secret
	// HMAC de la nouvelle partie reste tiré pour elle seule, et le score reste
	// RECALCULÉ par le serveur depuis le journal d'événements scellé. Le
	// fantôme ne fait entrer aucun score par la porte de derrière.
	Ghost string `json:"ghost,omitempty"`
}

// handleRunBegin ouvre une partie AVANT qu'elle soit jouée.
//
// C'est le renversement de fond par rapport à la V1. Le serveur tire la graine
// du générateur aléatoire et un secret propre à cette partie ; le client joue
// avec cette graine et scelle son journal d'événements avec ce secret. Le score
// n'est plus une valeur que le client annonce, mais une conséquence que le
// serveur recalcule.
func (s *Server) handleRunBegin(w http.ResponseWriter, r *http.Request) {
	sess, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	if !s.rateLimit(w, r, "run-begin|"+strconv.FormatInt(sess.PlayerID, 10), 120, time.Hour) {
		return
	}

	var req runBeginRequest
	if !decodeJSON(w, r, s, &req) {
		return
	}

	games, err := s.store.Games(r.Context())
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "lecture impossible", err)
		return
	}
	var game *store.Game
	for i := range games {
		if games[i].Slug == req.Game {
			game = &games[i]
			break
		}
	}
	if game == nil {
		s.fail(w, r, http.StatusBadRequest, "jeu inconnu", nil)
		return
	}

	secret := make([]byte, 32)
	if _, err := rand.Read(secret); err != nil {
		s.fail(w, r, http.StatusInternalServerError, "ouverture impossible", err)
		return
	}

	// La graine : celle du fantôme qu'on affronte, ou une neuve.
	var seed int64
	if req.Ghost != "" {
		ghostSeed, ghostGame, gerr := s.store.RunSeedForGhost(r.Context(), req.Ghost)
		if errors.Is(gerr, store.ErrNotFound) {
			s.fail(w, r, http.StatusNotFound, "fantôme inconnu", nil)
			return
		}
		if gerr != nil {
			s.fail(w, r, http.StatusInternalServerError, "ouverture impossible", gerr)
			return
		}
		// Le fantôme doit être du jeu demandé. Sans ce contrôle, on pourrait
		// ouvrir une partie d'Aplomb sur la graine d'un fantôme d'Envol :
		// le duel n'aurait aucun sens, et le client rejouerait des entrées
		// faites pour un autre jeu.
		if ghostGame != game.ID {
			s.fail(w, r, http.StatusBadRequest, "ce fantôme n'est pas de ce jeu", nil)
			return
		}
		seed = ghostSeed
	} else {
		var serr error
		seed, serr = randomSeed()
		if serr != nil {
			s.fail(w, r, http.StatusInternalServerError, "ouverture impossible", serr)
			return
		}
	}

	id, err := s.store.BeginRun(r.Context(), sess.PlayerID, game.ID, secret, seed)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "ouverture impossible", err)
		return
	}

	writeJSON(w, http.StatusCreated, map[string]any{
		"ok":     true,
		"runId":  id,
		"seed":   seed,
		"secret": base64.RawStdEncoding.EncodeToString(secret),
		"game":   game.Slug,
	})
}

func (s *Server) handleRunSubmit(w http.ResponseWriter, r *http.Request) {
	sess, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	if !s.rateLimit(w, r, "run-submit|"+strconv.FormatInt(sess.PlayerID, 10), 120, time.Hour) {
		return
	}

	runID := r.PathValue("id")

	var submission runs.Submission
	if !decodeJSON(w, r, s, &submission) {
		return
	}

	run, err := s.store.RunByID(r.Context(), runID, sess.PlayerID)
	if errors.Is(err, store.ErrNotFound) {
		s.fail(w, r, http.StatusNotFound, "partie inconnue", nil)
		return
	}
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "lecture impossible", err)
		return
	}
	if run.Submitted {
		// Rejeu : la V1 acceptait de renvoyer le même score autant de fois
		// qu'on voulait, ce qui suffisait à monter le classement.
		s.fail(w, r, http.StatusConflict, "partie déjà soumise", nil)
		return
	}

	games, err := s.store.Games(r.Context())
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "lecture impossible", err)
		return
	}
	var game *store.Game
	for i := range games {
		if games[i].ID == run.GameID {
			game = &games[i]
			break
		}
	}
	if game == nil {
		s.fail(w, r, http.StatusInternalServerError, "jeu introuvable", nil)
		return
	}

	// Le plafond vient de la LIGNE DU JEU, pas d'une constante. Il valait
	// 1 000 000 pour tout le monde ; le demineur est plafonne a 10 000 dans la
	// table, et ses regles autorisent 15 cases par seconde a 10 000 points la
	// case, soit 750 000 points par seconde. Une partie de 1,4 s atteignait
	// donc le million en passant tous les autres controles.
	//
	// Le repli n'est pas une politesse : une colonne a zero (jeu insere a la
	// main, migration future incomplete) rendrait le plafond INFINI si on
	// passait la valeur telle quelle. On refuse de degrader en silence.
	plafond := game.MaxPlausibleScore
	if plafond <= 0 {
		plafond = 1_000_000
		slog.Warn("plafond de plausibilite absent, repli applique",
			"jeu", game.Slug, "plafond", plafond)
	}

	verdict := runs.Verify(runs.Context{
		Secret:            run.Secret,
		Seed:              run.Seed,
		StartedAt:         run.StartedAt,
		Now:               time.Now(),
		GameSlug:          game.Slug,
		MaxPlausibleScore: plafond,
	}, submission)

	closed, err := s.store.CloseRun(r.Context(), run.ID, verdict.Score, verdict.Reason)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "clôture impossible", err)
		return
	}
	if !closed {
		s.fail(w, r, http.StatusConflict, "partie déjà soumise", nil)
		return
	}

	if !verdict.Accepted {
		s.log.Warn("partie rejetée",
			"player", sess.Username, "game", game.Slug, "reason", verdict.Reason,
			"claimed", submission.ClaimedScore, "computed", verdict.Score)
		s.fail(w, r, http.StatusUnprocessableEntity, "partie invalide : "+verdict.Reason, nil)
		return
	}

	improved, err := s.store.RecordScore(r.Context(), sess.PlayerID, game.ID, verdict.Score, run.ID)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "enregistrement impossible", err)
		return
	}

	s.log.Info("score enregistré",
		"player", sess.Username, "game", game.Slug, "score", verdict.Score, "record", improved)
	writeJSON(w, http.StatusOK, map[string]any{
		"ok": true, "score": verdict.Score, "personalBest": improved,
	})
}

/* ========================================================================== */
/* Temps réel — présence                                                      */
/* ========================================================================== */

// La durée pendant laquelle une présence reste « vivante » sans nouvelle.
//
// Elle est volontairement plusieurs fois plus longue que la période d'émission
// du client (4 Hz) : sur un transport sans connexion persistante, une requête
// qui traîne ne doit pas faire clignoter un joueur hors de la salle.
const presenceTTL = 12 * time.Second

type presenceRequest struct {
	ClientID string  `json:"clientId"`
	Nickname string  `json:"nickname"`
	X        float32 `json:"x"`
	Y        float32 `json:"y"`
	Z        float32 `json:"z"`
	Yaw      float32 `json:"yaw"`
	// La hauteur d'œil au-dessus des pieds. Absente d'un client d'avant la
	// migration 0004 : elle vaut alors zéro, et c'est le client qui reçoit —
	// et non le serveur — qui décide de son repli. Voir `room_presence_config`.
	Eye     float32 `json:"eye"`
	Cabinet string  `json:"cabinet"`
	Game    string  `json:"game"`
	Score   int32   `json:"score"`
	// Le client s'en va proprement. Le TTL suffirait, mais un joueur qui quitte
	// la salle ne devrait pas y rester douze secondes de plus.
	Leaving bool `json:"leaving,omitempty"`
}

// handlePresence — « je suis là, qui d'autre ? », en un aller-retour.
//
// AUCUN COMPTE N'EST EXIGÉ, et c'est la règle du projet, pas une facilité : la
// V1 enfermait le jeu entier derrière une vérification en ligne et ne pouvait
// pas atteindre sa propre fenêtre sans serveur. Se montrer dans une salle
// d'arcade n'est pas une action privilégiée.
//
// Ce que le serveur ne fait donc PAS : croire le pseudo. Un client sans jeton
// déclare le nom qu'il veut, et la réponse le marque `verified: false`. Avec un
// jeton, le serveur ÉCRASE le nom déclaré par celui du compte. C'est la même
// logique que pour les scores — l'autorité est côté serveur — appliquée à ce
// qu'elle peut ici réellement établir.
func (s *Server) handlePresence(w http.ResponseWriter, r *http.Request) {
	// Une présence est périodique : la limite doit tenir le rythme normal du
	// client (4 Hz) tout en fermant la porte à une boucle. 2 000 sur dix
	// minutes, soit un peu plus de 3/s soutenus.
	if !s.rateLimit(w, r, "presence", 2000, 10*time.Minute) {
		return
	}

	var req presenceRequest
	if !decodeJSON(w, r, s, &req) {
		return
	}

	// L'identifiant de client est tiré par le jeu. Il n'a aucun privilège au
	// sens où il ne donne accès à rien — mais il DÉSIGNE la ligne qu'une
	// requête réécrit, et c'est un privilège suffisant : tant qu'il était
	// republié à tous les pairs, chacun pouvait renommer, déplacer ou faire
	// disparaître n'importe qui. Il ne sort plus d'ici (`store.poignee`), et
	// une ligne qui porte un compte ne s'écrase plus depuis l'anonymat.
	//
	// On le borne quand même : il devient une clé primaire.
	req.ClientID = strings.TrimSpace(req.ClientID)
	if len(req.ClientID) < 8 || len(req.ClientID) > 64 || !isASCIIToken(req.ClientID) {
		s.fail(w, r, http.StatusBadRequest, "identifiant de client invalide", nil)
		return
	}

	// La session se résout AVANT la branche « je pars ». Elle décidait
	// jusqu'ici du seul pseudo ; elle décide maintenant aussi du droit
	// d'écrire, et le départ est une écriture comme une autre.
	var playerID *int64
	var sessionUser string
	if sess, err := s.authenticate(r); err == nil {
		id := sess.PlayerID
		playerID = &id
		sessionUser = sess.Username
	}

	if req.Leaving {
		if err := s.store.DropPresence(r.Context(), req.ClientID, playerID); err != nil {
			s.fail(w, r, http.StatusInternalServerError, "présence indisponible", err)
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"ok": true, "peers": []store.Peer{}})
		return
	}

	peer := store.Peer{
		ClientID: req.ClientID,
		Nickname: sanitizeNickname(req.Nickname),
		X:        safeCoord(req.X),
		Y:        safeCoord(req.Y),
		Z:        safeCoord(req.Z),
		Yaw:      safeCoord(req.Yaw),
		Eye:      safeEye(req.Eye),
		Cabinet:  sanitizeShort(req.Cabinet, 32),
		Game:     sanitizeShort(req.Game, 32),
		Score:    req.Score,
	}
	if peer.Score < 0 {
		peer.Score = 0
	}

	// Le jeton, s'il y en a un, TRANCHE le pseudo. Sans jeton on garde ce qui a
	// été déclaré, et on le dit.
	if playerID != nil {
		peer.Nickname = sessionUser
		peer.Verified = true
	}

	peers, err := s.store.TouchPresence(r.Context(), peer, playerID, presenceTTL)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "présence indisponible", err)
		return
	}

	writeJSON(w, http.StatusOK, map[string]any{
		"ok": true, "peers": peers, "ttlMs": presenceTTL.Milliseconds(),
	})
}

/* ========================================================================== */
/* Temps réel — fantômes                                                      */
/* ========================================================================== */

// Le journal d'entrées voyage en TEXTE BRUT, dans les deux sens, et ce n'est
// pas un raccourci : c'est la correction d'un défaut qui aurait été invisible
// de chaque côté.
//
// Le premier jet le transportait dans un champ JSON. Or l'analyseur JSON du
// client (`ns_json_string`, engine/core/ns_json.c) ne DÉSÉCHAPPE pas : il rend
// les octets bruts entre les guillemets. Un journal de trois cents lignes,
// encodé en JSON, y serait donc arrivé comme une seule ligne parsemée de « \n »
// littéraux — et l'analyseur de journal, qui découpe sur les retours à la
// ligne, en aurait tiré zéro entrée. Le duel aurait échoué en silence, avec un
// fantôme immobile et aucun message pour dire pourquoi.
//
// Les deux moitiés étaient justes séparément : le serveur produisait du JSON
// valide, le client lisait ce qu'on lui avait dit de lire. C'est exactement la
// leçon que le changelog tire du classement en ligne, et la réponse est la
// même : supprimer l'endroit où les deux peuvent diverger. Du texte brut n'a
// pas d'échappement, donc pas de désaccord possible sur son échappement.
const inputsContentType = "text/plain"

// journalCharset — l'alphabet EXACT d'un journal d'entrées.
//
// Lettres, chiffres, espace, retour à la ligne, tiret et souligné. Rien d'autre.
//
// Ce n'est pas une coquetterie : c'est ce qui rend le contenu PROUVABLEMENT non
// exécutable. Un journal ne peut contenir ni '<', ni '>', ni guillemet, ni
// esperluette, donc il ne peut pas être du HTML, ni du JavaScript, ni une
// entité. Le service sert ce texte depuis sa propre origine ; l'invariant
// ci-dessous est ce qui fait que le servir est sans conséquence, plutôt que
// « sans conséquence tant que le navigateur respecte `nosniff` ».
//
// Il est vérifié À L'ENTRÉE — on ne stocke pas ce qu'on refuserait de servir —
// et redemandé à la SORTIE, parce qu'une base peut être alimentée autrement que
// par cette route (restauration, migration, accès direct) et qu'une défense qui
// dépend de l'historique des écritures n'en est pas une.
func validJournalByte(c byte) bool {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') ||
		c == ' ' || c == '\n' || c == '-' || c == '_'
}

func validJournal(s string) bool {
	if !strings.HasPrefix(s, "v1 ") {
		return false
	}
	for i := 0; i < len(s); i++ {
		if !validJournalByte(s[i]) {
			return false
		}
	}
	return true
}

// readPlainBody lit un corps en texte brut, borné.
func readPlainBody(w http.ResponseWriter, r *http.Request, s *Server, max int64) (string, bool) {
	if ct := r.Header.Get("Content-Type"); ct != "" &&
		!strings.HasPrefix(ct, inputsContentType) {
		s.fail(w, r, http.StatusUnsupportedMediaType, "corps attendu en texte brut", nil)
		return "", false
	}
	r.Body = http.MaxBytesReader(w, r.Body, max)
	b, err := io.ReadAll(r.Body)
	if err != nil {
		s.fail(w, r, http.StatusRequestEntityTooLarge, "journal trop volumineux", err)
		return "", false
	}
	return string(b), true
}

// handleRunInputs attache un journal d'ENTRÉES à une partie déjà soumise.
//
// C'est le transport qui manquait au duel, et rien de plus. Le journal d'entrées
// n'entre ni dans la charge canonique ni dans le sceau : il n'a rien à prouver
// au serveur, qui ne le rejoue pas. Ce qui garantit qu'un fantôme vaut son
// score, c'est que le score vient de la partie — ouverte par le serveur, scellée
// par le client, RECALCULÉE par le serveur — et pas de ces lignes-ci.
//
// Autrement dit : déposer un journal d'entrées ne peut pas créer de score. Au
// pire on dépose des entrées qui ne reproduisent pas la partie, et le seul
// perdant est celui qui croyait avoir enregistré son fantôme.
func (s *Server) handleRunInputs(w http.ResponseWriter, r *http.Request) {
	sess, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	if !s.rateLimit(w, r, "run-inputs|"+strconv.FormatInt(sess.PlayerID, 10), 120, time.Hour) {
		return
	}

	// La même borne que la contrainte SQL. Dupliquée volontairement : la
	// contrainte protège la base quoi qu'on lui passe, celle-ci donne au client
	// un refus lisible plutôt qu'une erreur de contrainte.
	inputs, ok2 := readPlainBody(w, r, s, 262144)
	if !ok2 {
		return
	}
	if inputs == "" {
		s.fail(w, r, http.StatusBadRequest, "journal vide", nil)
		return
	}
	// L'alphabet strict : en-tête « v1 », puis rien que des lettres, des
	// chiffres, des espaces et des retours à la ligne. Voir `validJournal`.
	//
	// Ce contrôle remplace trois vérifications séparées qu'il englobe toutes —
	// le préfixe, l'absence d'octet nul (que PostgreSQL refuse en colonne
	// `text`) et la validité UTF-8 — et il en ajoute la propriété qui compte :
	// ce qu'on stocke ne peut pas être du HTML.
	if !validJournal(inputs) {
		s.fail(w, r, http.StatusBadRequest,
			"journal mal formé : « v1 » puis des nombres, rien d'autre", nil)
		return
	}

	saved, err := s.store.SaveGhost(r.Context(), r.PathValue("id"), sess.PlayerID, inputs)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "enregistrement impossible", err)
		return
	}
	if !saved {
		// Partie inconnue, pas la sienne, pas encore soumise, refusée, ou
		// fantôme déjà déposé. On ne distingue pas : ça ne regarde pas
		// l'appelant, et distinguer renseignerait sur les parties des autres.
		s.fail(w, r, http.StatusConflict, "aucune partie validée à laquelle rattacher ce journal", nil)
		return
	}

	s.log.Info("fantôme enregistré", "player", sess.Username, "run", r.PathValue("id"),
		"octets", len(inputs))
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

// handleGhosts liste les fantômes d'un jeu, SANS leur journal.
//
// Deux routes et pas une, parce qu'un journal pèse mille fois la ligne qui le
// décrit : afficher « qui peux-tu affronter » ne doit pas télécharger dix
// parties dont on n'en jouera qu'une.
func (s *Server) handleGhosts(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	gameID := gameIDParam(q.Get("game"))
	limit := parseBoundedInt(q.Get("limit"), 10, 1, 50)

	list, err := s.store.Ghosts(r.Context(), gameID, limit)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "fantômes indisponibles", err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"ok": true, "game": gameID, "ghosts": list,
	})
}

func (s *Server) handleGhost(w http.ResponseWriter, r *http.Request) {
	g, err := s.store.GhostByID(r.Context(), r.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		s.fail(w, r, http.StatusNotFound, "fantôme inconnu", nil)
		return
	}
	if err != nil {
		// Un identifiant qui n'est pas un UUID arrive ici : c'est une requête
		// mal formée, pas une panne du serveur.
		if strings.Contains(err.Error(), "uuid") {
			s.fail(w, r, http.StatusBadRequest, "identifiant invalide", err)
			return
		}
		s.fail(w, r, http.StatusInternalServerError, "fantôme indisponible", err)
		return
	}
	// Le journal, tel quel. Pas de JSON autour : voir `inputsContentType`.
	//
	// Il est SELF-DESCRIPTIF — sa première ligne porte « v1 <jeu> <difficulté>
	// <graine> » — donc le client n'a besoin de rien d'autre pour le rejouer.
	// Le score et le nom de l'adversaire, eux, sont déjà venus par la liste.

	// L'invariant est REVÉRIFIÉ ici, et pas seulement à l'écriture.
	//
	// Ce contenu vient d'un autre joueur et ressort par notre origine : c'est
	// la définition d'un XSS stocké si jamais il pouvait être du HTML. Il ne
	// peut pas l'être — `validJournal` interdit '<', '>', '"' et '&' — mais
	// s'appuyer uniquement sur le contrôle fait à l'écriture reviendrait à
	// parier que la base n'a jamais été alimentée autrement (restauration,
	// migration, accès direct). Une défense qui dépend de l'historique des
	// écritures n'en est pas une, et ce contrôle coûte un parcours de quelques
	// kilo-octets.
	if !validJournal(g.Inputs) {
		s.fail(w, r, http.StatusInternalServerError, "fantôme illisible",
			errors.New("journal stocké hors alphabet"))
		return
	}

	h := w.Header()
	h.Set("Content-Type", inputsContentType+"; charset=utf-8")
	h.Set("Content-Length", strconv.Itoa(len(g.Inputs)))
	// `nosniff` est déjà posé pour tout le service ; on le redit ici parce que
	// c'est la seule route qui renvoie du contenu écrit par un autre joueur, et
	// qu'un en-tête global peut être déplacé par mégarde.
	h.Set("X-Content-Type-Options", "nosniff")
	// Et on refuse explicitement que le navigateur en fasse une page : ce
	// fichier est une donnée que le jeu consomme, pas un document à afficher.
	h.Set("Content-Disposition", "attachment; filename=\"ghost.txt\"")
	// Ce qu'on affiche à côté du fantôme, sans imposer un second aller-retour à
	// qui n'a pas gardé la liste. Le pseudo est passé au crible du même
	// alphabet : un en-tête HTTP ne tolère ni retour à la ligne ni octet exotique.
	h.Set("X-Nineteen-Ghost-Score", strconv.FormatInt(g.Score, 10))
	h.Set("X-Nineteen-Ghost-Player", sanitizeShort(g.Username, 24))
	w.WriteHeader(http.StatusOK)
	// #nosec G705 -- `g.Inputs` vient de passer `validJournal` deux lignes plus
	// haut : son alphabet exclut '<', '>', '"' et '&', donc le contenu ne peut
	// pas être du HTML ni un script. L'analyse par teinte voit une donnée issue
	// de la base atteindre la réponse et ne peut pas suivre cette garantie à
	// travers PostgreSQL ; la garantie est pourtant établie ICI, sur la valeur
	// exacte qui est écrite, et non ailleurs dans le programme. S'ajoutent
	// `nosniff` et `Content-Disposition: attachment` posés juste au-dessus.
	_, _ = io.WriteString(w, g.Inputs)
}

/* ========================================================================== */
/* Divers                                                                     */
/* ========================================================================== */

// La version, ET si son paquet est publié.
//
// Le second champ existe parce que la page s'en passait : `app.js` bâtissait
// trois liens vers `github.com/.../releases/download/v<version>/…` à partir du
// seul numéro de version, en supposant que la release existe. Mesuré contre
// l'API GitHub : le dépôt répond 200, `releases/tags/v17.0.0` répond 404, et la
// liste des releases est vide. Les trois boutons « Télécharger » — la raison
// d'être de la page — étaient donc trois 404.
//
// Faux par défaut, et c'est le sens sûr : un serveur qu'on lance sans rien dire
// n'affirme pas qu'un paquet existe. Le jour où la release est publiée,
// NINETEEN_RELEASE_PUBLIEE=1 rallume les boutons — une variable, pas un
// redéploiement du site.
// `serveur` : l'adresse que le JEU doit viser, et que lui seul ignore.
//
// Le site, lui, n'en a aucun besoin — `app.js` appelle l'API en relatif et
// `credentials: "same-origin"`, donc il fonctionne sur n'importe quel hôte sans
// rien savoir de son propre nom. Ce champ ne sert pas à l'API du site : il sert
// à ce que la page de téléchargement puisse écrire, sous le bouton, la ligne
// exacte à taper. Sans lui le joueur repart avec un binaire et aucune adresse.
//
// Vide quand `NINETEEN_PUBLIC_URL` n'est pas posée. La page cache alors le bloc
// — un serveur qu'on lance sans rien dire n'annonce rien, comme pour `publiee`.
func (s *Server) handleVersion(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{
		"ok": true, "version": s.version, "publiee": s.publiee,
		"serveur": s.publicURL,
	})
}

func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 2*time.Second)
	defer cancel()
	if err := s.store.Pool().Ping(ctx); err != nil {
		s.fail(w, r, http.StatusServiceUnavailable, "base injoignable", err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

/* ========================================================================== */

// decodeJSON borne la taille du corps et refuse les champs inconnus.
//
// La borne évite qu'une requête de plusieurs gigaoctets épuise la mémoire ; le
// refus des champs inconnus fait échouer bruyamment un client désynchronisé au
// lieu d'ignorer silencieusement ce qu'il envoie.
func decodeJSON(w http.ResponseWriter, r *http.Request, s *Server, dst any) bool {
	const maxBody = 1 << 20 // 1 Mio : très large pour un journal de partie

	if ct := r.Header.Get("Content-Type"); ct != "" &&
		!strings.HasPrefix(ct, "application/json") {
		s.fail(w, r, http.StatusUnsupportedMediaType, "corps attendu en JSON", nil)
		return false
	}

	r.Body = http.MaxBytesReader(w, r.Body, maxBody)
	dec := json.NewDecoder(r.Body)
	dec.DisallowUnknownFields()

	if err := dec.Decode(dst); err != nil {
		s.fail(w, r, http.StatusBadRequest, "requête mal formée", err)
		return false
	}
	// Un second objet JSON après le premier serait ignoré : on refuse.
	if dec.More() {
		s.fail(w, r, http.StatusBadRequest, "requête mal formée", errors.New("contenu superflu"))
		return false
	}
	return true
}

// gameIDParam lit un identifiant de jeu et le rend DÉJÀ borné, en int32.
//
// La conversion est faite ici, une fois, plutôt qu'à chaque appelant : bornée à
// 0..100 juste au-dessus, elle ne peut pas déborder — mais l'analyse statique ne
// peut pas le savoir en voyant un `int32(x)` isolé, et elle a raison d'être
// méfiante. Concentrer la conversion à l'endroit où la borne est visible rend
// la sûreté LOCALE : on n'a pas à remonter l'appelant pour se convaincre.
func gameIDParam(raw string) int32 {
	v := parseBoundedInt(raw, 0, 0, 100)
	if v < 0 {
		v = 0
	}
	if v > 100 {
		v = 100
	}
	return int32(v)
}

func parseBoundedInt(raw string, fallback, min, max int) int {
	if raw == "" {
		return fallback
	}
	v, err := strconv.Atoi(raw)
	if err != nil || v < min || v > max {
		return fallback
	}
	return v
}

/* -------------------------------------------------------------------------- */
/* Assainissement des champs de présence                                      */
/*                                                                            */
/* Ces champs viennent d'un client NON AUTHENTIFIÉ : c'est la seule surface du */
/* service dans ce cas, et c'est donc la seule où l'assainissement ne peut pas */
/* s'appuyer sur « de toute façon il a un compte ».                           */
/* -------------------------------------------------------------------------- */

// isASCIIToken — lettres, chiffres, tiret et souligné. L'identifiant de client
// devient une clé primaire et se retrouve dans des journaux : on ne lui laisse
// ni espace, ni caractère de contrôle, ni octet non ASCII.
func isASCIIToken(s string) bool {
	for i := 0; i < len(s); i++ {
		c := s[i]
		ok := (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_'
		if !ok {
			return false
		}
	}
	return true
}

// sanitizeNickname rend un pseudo affichable, jamais vide, d'au plus 24 runes.
//
// La troncature est faite en RUNES et pas en octets : couper « Émilie » au
// milieu d'un caractère produirait de l'UTF-8 invalide, que PostgreSQL refuse
// en colonne `text` — un 500 pour un pseudo accentué, c'est-à-dire pour un
// joueur français sur un jeu français.
//
// Les caractères de contrôle sautent : le pseudo est dessiné dans la salle et
// écrit dans les journaux du serveur, et un retour à la ligne dans un pseudo
// est le début d'une falsification de journal.
func sanitizeNickname(raw string) string {
	var b strings.Builder
	n := 0
	for _, r := range strings.TrimSpace(raw) {
		if n >= 24 {
			break
		}
		if r < 0x20 || r == 0x7F || !utf8.ValidRune(r) {
			continue
		}
		b.WriteRune(r)
		n++
	}
	out := strings.TrimSpace(b.String())
	if out == "" {
		// Anonyme est permis — on ne demande de compte à personne — mais la
		// contrainte SQL veut au moins un caractère, et un joueur sans nom doit
		// quand même se voir dans la salle.
		return "Anonyme"
	}
	return out
}

// sanitizeShort — même traitement pour les champs courts (borne, jeu), qui sont
// des identifiants et non du texte libre.
func sanitizeShort(raw string, max int) string {
	var b strings.Builder
	n := 0
	for _, r := range strings.TrimSpace(raw) {
		if n >= max {
			break
		}
		if r < 0x20 || r == 0x7F || !utf8.ValidRune(r) {
			continue
		}
		b.WriteRune(r)
		n++
	}
	return b.String()
}

// safeCoord écarte NaN et les infinis.
//
// PostgreSQL accepte NaN en `real`, et il ressortirait tel quel chez les autres
// joueurs — où il empoisonnerait une interpolation de position et ferait
// disparaître un avatar au lieu de le placer. Un client qui envoie NaN est
// soit cassé, soit malveillant ; dans les deux cas zéro est une réponse.
func safeCoord(v float32) float32 {
	f := float64(v)
	if math.IsNaN(f) || math.IsInf(f, 0) {
		return 0
	}
	// La salle fait quelques dizaines de mètres. Une coordonnée hors de cette
	// borne ne décrit rien de la salle : on la ramène plutôt que de la servir.
	const limit = 1000.0
	if f > limit {
		return limit
	}
	if f < -limit {
		return -limit
	}
	return v
}

// safeEye borne la hauteur d'œil publiée.
//
// Elle n'est pas une coordonnée et ne se borne pas comme une coordonnée : c'est
// une hauteur de corps, et les valeurs recevables tiennent dans un intervalle
// étroit qu'on connaît. Zéro et les valeurs négatives veulent dire « non
// publiée » — un œil sous les pieds n'existe pas —, et le client qui reçoit
// pose alors son propre repli. Le plafond de trois mètres est le plafond de la
// salle (2,92 m) arrondi au-dessus : au-delà, la valeur ne décrit plus personne
// qui puisse s'y tenir debout.
func safeEye(v float32) float32 {
	f := float64(v)
	if math.IsNaN(f) || math.IsInf(f, 0) || f <= 0 {
		return 0
	}
	if f > 3.0 {
		return 3.0
	}
	return v
}

func randomSeed() (int64, error) {
	b := make([]byte, 8)
	if _, err := rand.Read(b); err != nil {
		return 0, fmt.Errorf("graine : %w", err)
	}
	// Bit de poids fort effacé : la graine reste positive, ce qui évite les
	// surprises côté client si elle est lue en entier signé.
	var v int64
	for _, x := range b {
		v = v<<8 | int64(x)
	}
	return v & 0x7FFFFFFFFFFFFFFF, nil
}
