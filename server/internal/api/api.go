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
	"log/slog"
	"net"
	"net/http"
	"strconv"
	"strings"
	"time"

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
	assets  http.Handler
}

type Config struct {
	Store   *store.Store
	Logger  *slog.Logger
	Secure  bool
	Version string
	Assets  http.Handler
}

func New(cfg Config) *Server {
	s := &Server{
		store:   cfg.Store,
		log:     cfg.Logger,
		mux:     http.NewServeMux(),
		secure:  cfg.Secure,
		version: cfg.Version,
		assets:  cfg.Assets,
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
	gameID := parseBoundedInt(q.Get("game"), 0, 0, 100)
	limit := parseBoundedInt(q.Get("limit"), 20, 1, 100)
	offset := parseBoundedInt(q.Get("offset"), 0, 0, 10000)

	entries, err := s.store.Leaderboard(r.Context(), int32(gameID), limit, offset)
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
	seed, err := randomSeed()
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "ouverture impossible", err)
		return
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

	verdict := runs.Verify(runs.Context{
		Secret:            run.Secret,
		Seed:              run.Seed,
		StartedAt:         run.StartedAt,
		Now:               time.Now(),
		GameSlug:          game.Slug,
		MaxPlausibleScore: 1_000_000,
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
/* Divers                                                                     */
/* ========================================================================== */

func (s *Server) handleVersion(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{
		"ok": true, "version": s.version,
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
