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
	"os"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"

	"nineteen/internal/auth"
	"nineteen/internal/runs"
	"nineteen/internal/salons"
	"nineteen/internal/store"
	"nineteen/internal/telechargements"
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
	// Le paquet de cette version est-il PUBLIE sur GitHub ?
	//
	// N'entre en jeu que si `depot` ne contient rien : un serveur qui heberge
	// ses propres paquets n'a aucune raison d'envoyer ailleurs. Voir
	// handleTelechargements.
	publiee bool
	// L'adresse PUBLIQUE du serveur, celle que le joueur passe à `--server=`.
	// Vide quand `NINETEEN_PUBLIC_URL` n'est pas posée : la page cache alors le
	// bloc plutôt que d'annoncer une adresse devinée. Contrôlée avant d'arriver
	// ici — voir `urlPubliqueValide` dans cmd/nineteend.
	publicURL string
	// L'adresse du relais TELLE QU'ON L'ANNONCE aux joueurs d'un salon.
	//
	// Elle n'a rien a voir avec l'adresse d'ECOUTE du relais : celle-ci dit ou
	// le monde le joint, celle-la ou le processus se pose. Derriere une
	// publication Docker ou un proxy, les deux different toujours — c'est le
	// meme partage que `-addr` et `-public-url`, et il se regle de la meme
	// facon. Voir `adresseRelaisValide` dans cmd/nineteend.
	//
	// Vide quand rien n'est annonce, et les salons repondent alors 503. C'est le
	// sens sur : un salon dont on ne peut pas donner le relais est un salon
	// qu'on ne peut pas jouer, et l'ouvrir quand meme donnerait au joueur un code
	// et aucun moyen de s'en servir.
	relais RelaisPublic
	// LE REPERTOIRE DES PAQUETS, quand ce serveur les heberge lui-meme.
	//
	// Nil ou vide, la page de telechargement se rabat sur la release GitHub, et
	// n'offre rien du tout si celle-ci n'est pas publiee non plus. C'est l'etat
	// qu'avait la pile Docker : elle se montait entiere et ne distribuait rien.
	depot  *telechargements.Depot
	assets http.Handler
}

// RelaisPublic — ou joindre le relais, tel qu'on l'ecrit au joueur.
type RelaisPublic struct {
	Hote string
	Port int
}

// Annonce dit s'il y a quelque chose a annoncer.
func (r RelaisPublic) Annonce() bool { return r.Hote != "" && r.Port > 0 }

type Config struct {
	Store     *store.Store
	Logger    *slog.Logger
	Secure    bool
	Version   string
	Publiee   bool
	PublicURL string
	Relais    RelaisPublic
	Depot     *telechargements.Depot
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
		relais:    cfg.Relais,
		depot:     cfg.Depot,
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

	// LE CLASSEMENT CLASSE. Sans compte, comme le reste : un classement qu'il
	// faut se connecter pour lire est un classement que personne ne regarde.
	s.mux.HandleFunc("GET /api/v1/saison", s.handleSaison)
	s.mux.HandleFunc("GET /api/v1/saison/{cle}", s.handleSaison)
	s.mux.HandleFunc("GET /api/v1/joueurs/{pseudo}", s.handleJoueur)

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

	// Les salons du Couperet. La creation, l'entree, la sortie et le battement
	// exigent un compte : une place dans une manche est une ressource qu'on tient
	// contre les autres, contrairement a la presence dans l'allee, qui n'enleve
	// rien a personne.
	//
	// La LISTE et le CLASSEMENT LIVE sont publics, pour la meme raison que le
	// classement l'est : on doit pouvoir regarder une manche, et decider de
	// s'inscrire, sans etre deja inscrit.
	s.mux.HandleFunc("POST /api/v1/salons", s.handleSalonCreer)
	s.mux.HandleFunc("GET /api/v1/salons", s.handleSalonsListe)
	s.mux.HandleFunc("POST /api/v1/salons/{code}/join", s.handleSalonEntrer)
	s.mux.HandleFunc("POST /api/v1/salons/{code}/leave", s.handleSalonSortir)
	s.mux.HandleFunc("DELETE /api/v1/salons/{code}", s.handleSalonFermer)
	s.mux.HandleFunc("POST /api/v1/salons/{code}/beat", s.handleSalonBattre)
	s.mux.HandleFunc("GET /api/v1/salons/{code}/live", s.handleSalonLive)

	// LES PAQUETS DU JEU. La liste est publique et la route de fichier aussi :
	// telecharger le jeu ne demande pas de compte, et en demander un serait
	// prendre le probleme a l'envers. On s'inscrit pour jouer en ligne, pas
	// pour obtenir le binaire.
	s.mux.HandleFunc("GET /api/v1/telechargements", s.handleTelechargements)
	s.mux.HandleFunc("GET /telechargements/{nom}", s.handleTelechargementFichier)

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
/* Salons du Couperet                                                         */
/* ========================================================================== */

// Le service de rendez-vous du mode competitif.
//
// CE QU'IL REPARE. Le Couperet se joue en ligne depuis qu'il existe, et le seul
// moyen d'y retrouver quelqu'un etait de convenir HORS BANDE d'un numero de
// salon et d'un numero de place, puis de les taper en ligne de commande
// (« --couperet-en-ligne=hote:port,salon,place,places », room/main.c). Deux
// joueurs qui choisissent la meme place ne se voient jamais ; celui qui se
// trompe sur le nombre de places est refuse par le relais sans savoir pourquoi.
// Ce n'etait pas un mode difficile a lancer, c'etait un mode qu'on ne pouvait
// pas lancer sans se parler ailleurs d'abord.
//
// LES REGLES NE SONT PAS ICI. Elles sont dans `internal/salons`, qui se teste
// sans base et sans socket. Ce qui suit ne fait que trois choses : borner ce
// qu'un client peut demander, traduire les refus en codes HTTP, et decider QUI
// voit QUOI. La troisieme est la seule qui compte pour la securite, et elle
// tient en une regle : `relais` ne sort jamais vers qui n'est pas assis.

// Le nombre de salons qu'un meme joueur peut tenir ouverts en meme temps.
//
// TROIS, et la borne existe parce qu'un salon coute des places sur le relais,
// qui en a un budget fixe (`maxPlacesOuvertes`, 512, internal/duel/relay.go).
// Sans elle, une boucle de creation reserverait ce budget sans jamais jouer, et
// les salons des autres seraient refuses par un relais plein. Trois laisse de
// quoi preparer une soiree — un salon public, un prive, un qu'on vient
// d'abandonner et qui n'a pas encore ete fauche — sans qu'un seul compte puisse
// prendre plus de vingt-quatre places sur les 512.
const salonsParProprietaire = 3

type salonCreerRequest struct {
	Nom    string `json:"nom"`
	Places int    `json:"places"`
	Camps  int    `json:"camps"`
	Prive  bool   `json:"prive"`
}

// Les champs d'un battement. Ce sont ceux du mode, aux types du mode : `points`
// et `fusibles` sont des `int32_t` dans `room_cp_place` (room/room_couperet.h),
// et les declarer plus larges ici laisserait entrer des valeurs que le client ne
// saurait pas relire.
type salonBattementRequest struct {
	Points   int32  `json:"points"`
	Fusibles int32  `json:"fusibles"`
	Vivante  bool   `json:"vivante"`
	Borne    string `json:"borne"`
	Camp     int    `json:"camp"`
	Commence bool   `json:"commence"`
}

/* -------------------------------------------------------------------------- */
/* Les formes rendues                                                         */
/* -------------------------------------------------------------------------- */

// Declarees une fois et partagees par les six routes. Le tableau des occupants
// est le meme a la creation, a l'entree, au battement et au classement live :
// le decrire quatre fois aurait garanti que les quatre finissent par differer
// d'un champ, et le client C lit ces noms-la.
type salonOccupantJSON struct {
	Place    int    `json:"place"`
	Pseudo   string `json:"pseudo"`
	Camp     int    `json:"camp"`
	Points   int32  `json:"points"`
	Fusibles int32  `json:"fusibles"`
	Vivante  bool   `json:"vivante"`
	Borne    string `json:"borne"`
}

// salonRelaisJSON — de quoi se brancher, et rien d'autre.
//
// C'EST LA CAPACITE. `salon` est l'identifiant de session sur le relais, tire
// avec `crypto/rand` a la creation ; le relais n'a aucune notion de compte et
// apparie sur ce seul nombre. Cette structure n'apparait donc que dans la
// reponse d'une creation ou d'une entree — c'est-a-dire vers un joueur
// authentifie a qui le serveur vient d'attribuer une place. Elle n'est ni dans
// la liste, ni dans le classement live. Voir internal/salons/salons.go.
type salonRelaisJSON struct {
	Hote  string `json:"hote"`
	Port  int    `json:"port"`
	Salon int64  `json:"salon"`
}

type salonVueJSON struct {
	OK           bool                `json:"ok"`
	Code         string              `json:"code"`
	Nom          string              `json:"nom"`
	Places       int                 `json:"places"`
	Camps        int                 `json:"camps"`
	Prive        bool                `json:"prive"`
	Proprietaire string              `json:"proprietaire"`
	Etat         string              `json:"etat"`
	Relais       salonRelaisJSON     `json:"relais"`
	Place        int                 `json:"place"`
	Occupants    []salonOccupantJSON `json:"occupants"`
}

type salonResumeJSON struct {
	Code         string `json:"code"`
	Nom          string `json:"nom"`
	Places       int    `json:"places"`
	Occupes      int    `json:"occupes"`
	Camps        int    `json:"camps"`
	Proprietaire string `json:"proprietaire"`
	Etat         string `json:"etat"`
	DepuisMs     int64  `json:"depuisMs"`
}

// salonLiveJSON — ce que lit la page du classement live.
//
// AUCUN CHAMP `relais`, et ce n'est pas un oubli : cette route est PUBLIQUE.
// Y publier l'identifiant de session reviendrait a donner l'entree de la manche
// a quiconque connait le code — c'est-a-dire a faire du code la capacite,
// exactement ce que le module refuse. Un salon prive repond ici comme un autre,
// justement parce qu'on ne risque rien a laisser regarder.
type salonLiveJSON struct {
	OK   bool   `json:"ok"`
	Code string `json:"code"`
	Nom  string `json:"nom"`
	Etat string `json:"etat"`

	// `places` et `camps` sont ici parce que la page web ne peut pas
	// interpreter une ligne sans eux. Le camp d'un joueur vaut sa place en
	// individuel et 0 ou 1 en equipes : sans savoir lequel des deux, la page
	// affichait un tiret pour tout le monde, y compris sur une manche a deux
	// camps. C'est le meme raisonnement que pour les autres vues — ce qu'on
	// rend doit se lire sans avoir a deviner.
	Places int `json:"places"`
	Camps  int `json:"camps"`

	DepuisMs  int64               `json:"depuisMs"`
	Occupants []salonOccupantJSON `json:"occupants"`
}

type salonBattementJSON struct {
	OK        bool                `json:"ok"`
	Etat      string              `json:"etat"`
	Places    int                 `json:"places"`
	Occupants []salonOccupantJSON `json:"occupants"`
}

// vueOccupants rend le TABLEAU : les vivants tant que la manche court, tout le
// monde une fois qu'elle est finie — voir `salons.Tableau`, ou la raison est
// ecrite. L'ordre, celui des places, est garanti la-bas plutot qu'ici parce que
// ce n'est pas une question de presentation : le client indexe ses propres
// tableaux par la place.
func vueOccupants(sal *salons.Salon, maintenant time.Time) []salonOccupantJSON {
	vivants := sal.Tableau(maintenant)
	out := make([]salonOccupantJSON, 0, len(vivants))
	for _, o := range vivants {
		out = append(out, salonOccupantJSON{
			Place: o.Place, Pseudo: o.Pseudo, Camp: o.Camp,
			Points: o.Points, Fusibles: o.Fusibles,
			Vivante: o.Vivante, Borne: o.Borne,
		})
	}
	return out
}

// depuisMs — l'age de l'ETAT COURANT, jamais celui du salon.
//
// Un seul champ pour deux questions qui n'en font qu'une : « il attend depuis
// combien de temps ? » quand on choisit un salon, « la manche court depuis
// combien de temps ? » quand on la regarde. Negatif est impossible en principe
// et rendu a zero : une horloge de base legerement en avance ne doit pas
// afficher un compteur qui remonte.
func depuisMs(sal *salons.Salon, maintenant time.Time) int64 {
	ms := maintenant.Sub(sal.ChangeA).Milliseconds()
	if ms < 0 {
		return 0
	}
	return ms
}

func vueSalon(sal *salons.Salon, place int, relais RelaisPublic, maintenant time.Time) salonVueJSON {
	return salonVueJSON{
		OK: true, Code: sal.Code, Nom: sal.Nom,
		Places: sal.Places, Camps: sal.Camps, Prive: sal.Prive,
		Proprietaire: sal.ProprietairePseudo, Etat: string(sal.Etat),
		Relais: salonRelaisJSON{Hote: relais.Hote, Port: relais.Port, Salon: sal.Relais},
		Place:  place, Occupants: vueOccupants(sal, maintenant),
	}
}

func vueResume(sal *salons.Salon, maintenant time.Time) salonResumeJSON {
	return salonResumeJSON{
		Code: sal.Code, Nom: sal.Nom, Places: sal.Places,
		Occupes: sal.Occupes(maintenant), Camps: sal.Camps,
		Proprietaire: sal.ProprietairePseudo, Etat: string(sal.Etat),
		DepuisMs: depuisMs(sal, maintenant),
	}
}

func vueLive(sal *salons.Salon, maintenant time.Time) salonLiveJSON {
	return salonLiveJSON{
		OK: true, Code: sal.Code, Nom: sal.Nom, Etat: string(sal.Etat),
		Places: sal.Places, Camps: sal.Camps,
		DepuisMs: depuisMs(sal, maintenant), Occupants: vueOccupants(sal, maintenant),
	}
}

func vueBattement(sal *salons.Salon, maintenant time.Time) salonBattementJSON {
	return salonBattementJSON{
		OK: true, Etat: string(sal.Etat), Places: sal.Places,
		Occupants: vueOccupants(sal, maintenant),
	}
}

/* -------------------------------------------------------------------------- */
/* Garde-fous communs                                                         */
/* -------------------------------------------------------------------------- */

// salonsOuverts refuse TOUT le service quand aucun relais n'est annonce.
//
// Un salon sans relais est un code et aucun moyen de s'en servir : le creer
// serait promettre un rendez-vous a une adresse qu'on ne connait pas. La liste
// et le classement live tombent sous la meme regle, parce qu'un service qui
// n'ouvre pas de salons n'a pas de salons a montrer.
//
// Le controle passe AVANT l'authentification, et l'ordre est reflechi : c'est un
// fait de configuration du serveur, identique pour tout le monde et pour toutes
// les requetes, donc il ne renseigne personne sur personne. Demander des
// identifiants pour ensuite repondre « ce service n'existe pas ici » ferait
// chercher la faute du mauvais cote.
func (s *Server) salonsOuverts(w http.ResponseWriter, r *http.Request) bool {
	if s.relais.Annonce() {
		return true
	}
	s.fail(w, r, http.StatusServiceUnavailable,
		"les salons sont indisponibles : ce serveur n'annonce aucun relais", nil)
	return false
}

// codeSalon lit le code de l'URL et le refuse SANS interroger la base quand ce
// n'en est pas un.
//
// 404 et non 400 : du point de vue de qui tape, un code mal forme et un code
// inexistant sont la meme chose — un salon qu'on ne trouve pas — et distinguer
// les deux renseignerait sur la forme des codes valides sans rendre service.
func (s *Server) codeSalon(w http.ResponseWriter, r *http.Request) (string, bool) {
	code := salons.NormaliserCode(r.PathValue("code"))
	if code == "" {
		s.fail(w, r, http.StatusNotFound, "salon inconnu", nil)
		return "", false
	}
	return code, true
}

// echecSalon traduit un refus des regles en code HTTP.
//
// Un seul endroit pour la table, parce que six gestionnaires qui la recopient
// finissent par rendre 404 la ou l'un d'eux rend 409, et le client n'a alors
// plus de comportement defini.
func (s *Server) echecSalon(w http.ResponseWriter, r *http.Request, err error) {
	switch {
	case errors.Is(err, store.ErrNotFound):
		s.fail(w, r, http.StatusNotFound, "salon inconnu", nil)
	case errors.Is(err, salons.ErrComplet):
		s.fail(w, r, http.StatusConflict, "salon complet", nil)
	case errors.Is(err, salons.ErrFerme):
		// 410 et non 404 : le salon a existé, il n'accepte plus. Le client peut
		// dire « la manche est déjà lancée » au lieu de « code inconnu », qui
		// enverrait le joueur retaper un code juste.
		s.fail(w, r, http.StatusGone, "ce salon ne se rejoint plus", nil)
	case errors.Is(err, salons.ErrPasProprietaire):
		s.fail(w, r, http.StatusForbidden, "seul le propriétaire peut fermer ce salon", nil)
	case errors.Is(err, salons.ErrPasAssis):
		s.fail(w, r, http.StatusConflict, "vous n'avez pas de place dans ce salon", nil)
	case errors.Is(err, salons.ErrTropDeSalons):
		s.fail(w, r, http.StatusConflict,
			"vous tenez déjà trop de salons ouverts : fermez-en un", nil)
	default:
		s.fail(w, r, http.StatusInternalServerError, "salon indisponible", err)
	}
}

/* -------------------------------------------------------------------------- */
/* Les six routes                                                             */
/* -------------------------------------------------------------------------- */

func (s *Server) handleSalonCreer(w http.ResponseWriter, r *http.Request) {
	if !s.salonsOuverts(w, r) {
		return
	}
	sess, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	// Creer un salon ecrit une ligne qui vit plusieurs minutes et reserve des
	// places sur le relais. La limite est donc par COMPTE et non par adresse :
	// c'est le compte qui porte le plafond de salons simultanes, et le compter
	// par adresse punirait quatre joueurs derriere le meme routeur.
	if !s.rateLimit(w, r, "salon-creer|"+strconv.FormatInt(sess.PlayerID, 10), 30, time.Hour) {
		return
	}

	var req salonCreerRequest
	if !decodeJSON(w, r, s, &req) {
		return
	}

	// Zero veut dire « pas d'avis », pas « zero place » : un champ absent d'un
	// JSON arrive ici a zero, et refuser la demande pour ca obligerait tout
	// client a repeter les valeurs par defaut. Un salon plein en individuel est
	// ce que le mode fait de plus courant.
	if req.Places == 0 {
		req.Places = salons.PlacesMax
	}
	if req.Camps == 0 {
		req.Camps = salons.CampsMin
	}

	// Le nom passe par l'assainisseur des champs courts, celui des pseudos et
	// des noms de borne. Reutilise et non recopie : deux assainissements pour
	// une meme sorte de champ finissent par diverger, et la divergence ne se
	// voit que le jour ou l'un laisse passer ce que l'autre refusait.
	nom := sanitizeShort(req.Nom, salons.NomMax)
	if err := salons.Valider(nom, req.Places, req.Camps); err != nil {
		s.fail(w, r, http.StatusBadRequest,
			"salon invalide : un nom, 2 à 8 places, 1 ou 2 camps", err)
		return
	}

	maintenant := time.Now()
	sal, err := s.store.CreerSalon(r.Context(), nom, req.Places, req.Camps, req.Prive,
		sess.PlayerID, sanitizeNickname(sess.Username), salonsParProprietaire, maintenant)
	if err != nil {
		s.echecSalon(w, r, err)
		return
	}

	place, _ := sal.PlaceDe(sess.PlayerID)
	s.log.Info("salon ouvert", "code", sal.Code, "proprietaire", sess.Username,
		"places", sal.Places, "camps", sal.Camps, "prive", sal.Prive)
	writeJSON(w, http.StatusCreated, vueSalon(sal, place, s.relais, maintenant))
}

// handleSalonsListe — ce qu'on peut rejoindre, maintenant.
//
// PUBLIQUE, sans compte, pour la meme raison que le classement l'est : on doit
// pouvoir regarder ce qui se joue, et decider de s'inscrire, sans etre deja
// inscrit. Elle ne montre QUE les salons publics en attente ; un salon prive
// n'y parait jamais, quel que soit son etat.
func (s *Server) handleSalonsListe(w http.ResponseWriter, r *http.Request) {
	if !s.salonsOuverts(w, r) {
		return
	}
	// La liste se rafraichit dans un menu pendant qu'on choisit. La limite tient
	// donc un rythme de consultation humain — plusieurs par seconde en pointe —
	// tout en fermant la porte a une boucle.
	if !s.rateLimit(w, r, "salons-liste", 600, 10*time.Minute) {
		return
	}

	maintenant := time.Now()
	liste, err := s.store.SalonsPublics(r.Context(), maintenant, 50)
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "liste indisponible", err)
		return
	}
	resumes := make([]salonResumeJSON, 0, len(liste))
	for i := range liste {
		resumes = append(resumes, vueResume(&liste[i], maintenant))
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "salons": resumes})
}

func (s *Server) handleSalonEntrer(w http.ResponseWriter, r *http.Request) {
	if !s.salonsOuverts(w, r) {
		return
	}
	sess, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	code, ok := s.codeSalon(w, r)
	if !ok {
		return
	}

	// DEUX LIMITES, et la premiere est celle qui compte.
	//
	// Par ADRESSE, elle est ce qui rend le code de six caracteres tenable. Les
	// 32 symboles et les 6 positions font 1 073 741 824 codes (`EspaceDesCodes`,
	// internal/salons) ; a 60 essais par dix minutes, soit 6 par minute,
	// balayer de quoi tomber sur l'un des cent salons vivants demanderait en
	// moyenne 10,7 millions d'essais, c'est-a-dire plus de trois ans. Le code
	// n'est pas un secret solide ; c'est cette limite qui fait qu'il n'a pas
	// besoin de l'etre.
	//
	// Par COMPTE, elle borne simplement le va-et-vient d'un joueur legitime.
	if !s.rateLimit(w, r, "salon-entrer", 60, 10*time.Minute) {
		return
	}
	if !s.rateLimit(w, r, "salon-entrer|"+strconv.FormatInt(sess.PlayerID, 10), 120, time.Hour) {
		return
	}

	maintenant := time.Now()
	sal, place, err := s.store.RejoindreSalon(r.Context(), code, sess.PlayerID,
		sanitizeNickname(sess.Username), maintenant)
	if err != nil {
		s.echecSalon(w, r, err)
		return
	}
	s.log.Info("entree dans un salon", "code", sal.Code, "joueur", sess.Username, "place", place)
	writeJSON(w, http.StatusOK, vueSalon(sal, place, s.relais, maintenant))
}

// handleSalonSortir — partir proprement.
//
// Le faucheur suffirait : une place cesse d'etre tenue douze secondes apres le
// dernier battement. Ceci evite seulement que sept joueurs attendent ces douze
// secondes pour quelqu'un qui vient de fermer sa fenetre — exactement la raison
// pour laquelle la presence a son champ `leaving`.
func (s *Server) handleSalonSortir(w http.ResponseWriter, r *http.Request) {
	if !s.salonsOuverts(w, r) {
		return
	}
	sess, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	code, ok := s.codeSalon(w, r)
	if !ok {
		return
	}
	if !s.rateLimit(w, r, "salon-sortir|"+strconv.FormatInt(sess.PlayerID, 10), 120, time.Hour) {
		return
	}

	if err := s.store.QuitterSalon(r.Context(), code, sess.PlayerID, time.Now()); err != nil {
		s.echecSalon(w, r, err)
		return
	}
	// 204 : il n'y a rien a rendre. Le client sait deja ce qu'il a quitte, et
	// lui renvoyer l'etat d'un salon dont il ne fait plus partie l'inviterait a
	// continuer de l'afficher.
	w.WriteHeader(http.StatusNoContent)
}

func (s *Server) handleSalonFermer(w http.ResponseWriter, r *http.Request) {
	if !s.salonsOuverts(w, r) {
		return
	}
	sess, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	code, ok := s.codeSalon(w, r)
	if !ok {
		return
	}
	if !s.rateLimit(w, r, "salon-fermer|"+strconv.FormatInt(sess.PlayerID, 10), 60, time.Hour) {
		return
	}

	if err := s.store.SupprimerSalon(r.Context(), code, sess.PlayerID, time.Now()); err != nil {
		s.echecSalon(w, r, err)
		return
	}
	s.log.Info("salon ferme", "code", code, "par", sess.Username)
	w.WriteHeader(http.StatusNoContent)
}

// handleSalonBattre — le signe de vie ET la ligne de classement, en un seul
// aller-retour.
//
// Les deux ensemble pour la meme raison que la presence : c'est le seul echange
// periodique du mode, et lui faire couter deux requetes doublerait le trafic
// sans rien apprendre de plus. Ce qui est publie ici n'a AUCUNE autorite — les
// points d'une manche ne sont pas des scores de classement, ils sont ce que
// l'arbitre du salon annonce, et l'autorite sur les scores enregistres reste ou
// elle est depuis toujours : le journal scelle, recalcule par le serveur.
func (s *Server) handleSalonBattre(w http.ResponseWriter, r *http.Request) {
	if !s.salonsOuverts(w, r) {
		return
	}
	sess, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	code, ok := s.codeSalon(w, r)
	if !ok {
		return
	}
	// La meme limite que la presence, parce que c'est le meme rythme et le meme
	// client : 4 Hz au plus (`NS_ARENE_PERIODE_MS`), 2 000 sur dix minutes, soit
	// un peu plus de 3/s soutenus. Voir `handlePresence`.
	if !s.rateLimit(w, r, "salon-battre|"+strconv.FormatInt(sess.PlayerID, 10), 2000, 10*time.Minute) {
		return
	}

	var req salonBattementRequest
	if !decodeJSON(w, r, s, &req) {
		return
	}

	maintenant := time.Now()
	sal, err := s.store.BattreSalon(r.Context(), code, sess.PlayerID, salons.Battement{
		Points:   req.Points,
		Fusibles: req.Fusibles,
		Vivante:  req.Vivante,
		// Le nom de borne est un identifiant venu du client, pas du texte libre :
		// meme traitement que dans la presence.
		Borne:    sanitizeShort(req.Borne, salons.BorneMax),
		Camp:     req.Camp,
		Commence: req.Commence,
	}, maintenant)
	if err != nil {
		s.echecSalon(w, r, err)
		return
	}
	writeJSON(w, http.StatusOK, vueBattement(sal, maintenant))
}

// handleSalonLive — le classement live, tel que la page web l'interroge.
//
// PUBLIQUE, et un salon PRIVE repond aussi : connaitre le code suffit pour
// regarder. C'est exactement ce qu'on veut d'un salon entre amis — il ne parait
// dans aucune liste, et le lien se partage a qui l'on veut. Rien n'est concede
// par la, parce que ce qui donne l'entree n'est pas le code mais l'identifiant
// de relais, et il n'est pas dans cette reponse : voir `salonLiveJSON`.
func (s *Server) handleSalonLive(w http.ResponseWriter, r *http.Request) {
	if !s.salonsOuverts(w, r) {
		return
	}
	// LA FORME DU CODE EST CONTROLEE AVANT LA LIMITE DE DEBIT, et l'ordre est
	// voulu : un code mal forme n'est jamais un salon, le refuser ne coute pas
	// une lecture, et lui faire consommer un jeton punirait une faute de frappe
	// aussi cher qu'un balayage. Ce que la limite protege, ce sont les codes
	// BIEN formes — ceux qui, eux, interrogent la base.
	code, ok := s.codeSalon(w, r)
	if !ok {
		return
	}
	// Une page de classement se rafraichit toute seule. La limite tient le
	// rythme de publication du client (4 Hz) pour que le tableau ne soit jamais
	// plus vieux que la source, sans laisser une boucle s'installer.
	if !s.rateLimit(w, r, "salon-live", 1200, 10*time.Minute) {
		return
	}

	maintenant := time.Now()
	sal, err := s.store.SalonParCode(r.Context(), code, maintenant)
	if err != nil {
		s.echecSalon(w, r, err)
		return
	}
	writeJSON(w, http.StatusOK, vueLive(sal, maintenant))
}

/* ========================================================================== */
/* Divers                                                                     */
/* ========================================================================== */

// La version, l'etat de publication, et l'adresse a viser.
//
// `publiee` ne sert plus qu'a UN cas depuis que le serveur peut heberger ses
// propres paquets : celui d'un deploiement qui n'en heberge aucun et compte sur
// la release GitHub. Voir handleTelechargements, qui tranche les trois etats.
// Faux par defaut, et c'est le sens sur : un serveur qu'on lance sans rien dire
// n'affirme pas qu'un paquet existe.
//
// `serveur` : l'adresse que le JEU doit viser, et que lui seul ignore.
//
// Le site, lui, n'en a aucun besoin. Ses scripts appellent l'API en relatif avec
// `credentials: "same-origin"`, donc il fonctionne sur n'importe quel hote sans
// rien savoir de son propre nom. Ce champ sert a ce que la page de
// telechargement ecrive, sous le bouton, la ligne exacte a taper. Sans lui le
// joueur repart avec un binaire et aucune adresse.
//
// Vide quand `NINETEEN_PUBLIC_URL` n'est pas posee. La page dit alors que le
// serveur n'annonce rien, au lieu d'afficher une ligne a trou.
func (s *Server) handleVersion(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{
		"ok": true, "version": s.version, "publiee": s.publiee,
		"serveur": s.publicURL,
	})
}

// LES PAQUETS DU JEU, tels que CE serveur peut les fournir.
//
// Trois etats, et la page n'en decide aucun : c'est ici que la question se
// tranche, une fois, en Go, ou elle se teste.
//
//	« locale »  un repertoire est monte et contient des paquets. C'est le cas
//	            de la composition Docker, qui les fabrique a cote du serveur.
//	« github »  aucun paquet local, mais la release de cette version est
//	            declaree publiee. On donne les liens du depot.
//	« aucune »  ni l'un ni l'autre. La page le dit et n'affiche pas de bouton
//	            mort : un bouton qui rend 404 fait douter du reste de la page.
//
// LE NAVIGATEUR NE CHOISISSAIT PAS BIEN. `app.js` batissait les trois URL de
// release a partir du seul numero de version, donc trois 404 tant que la
// release n'existait pas, et rien du tout quand la pile hebergeait pourtant les
// fichiers a cote d'elle. Le serveur est le seul a savoir ce qu'il a sous la
// main.
func (s *Server) handleTelechargements(w http.ResponseWriter, r *http.Request) {
	paquets, annexes := s.depot.Liste()
	source := "locale"

	// LA VERSION ANNONCEE EST CELLE DES PAQUETS, pas celle du binaire Go.
	//
	// Le jeu compare ce numero au sien pour decider s'il se met a jour. Un
	// serveur avance devant un repertoire pas encore refait annoncerait une
	// version qu'il ne peut pas livrer : le joueur telechargerait l'ancien
	// paquet, l'installerait, ne changerait pas de version, et se le verrait
	// reproposer au lancement suivant. Le repertoire reste la verite.
	version := s.version
	if v := s.depot.Version(); v != "" {
		version = v
	}

	if len(paquets) == 0 {
		annexes = nil
		if s.publiee {
			paquets, source = telechargements.SurGitHub(s.version), "github"
		} else {
			source = "aucune"
		}
	}

	// Jamais nil dans le JSON : une page qui doit distinguer `null` de `[]`
	// avant de compter est une page ou l'on finit par oublier le cas.
	if paquets == nil {
		paquets = []telechargements.Fichier{}
	}
	if annexes == nil {
		annexes = []telechargements.Annexe{}
	}

	writeJSON(w, http.StatusOK, map[string]any{
		"ok":         true,
		"version":    version,
		"source":     source,
		"serveur":    s.publicURL,
		"fichiers":   paquets,
		"manifestes": annexes,
	})
}

// UN PAQUET, en octets.
//
// LE DELAI D'ECRITURE EST LEVE ICI, ET IL LE FAUT. `http.Server` porte
// `WriteTimeout: 60s`, ce qui est le bon reglage pour une API JSON et une
// coupure nette pour un fichier de 175,6 Mio : le tenir demanderait 2,9 Mio/s
// soutenus, soit 23 Mbit/s, et toute connexion plus lente recevrait un fichier
// TRONQUE sans le moindre message. Le controleur de reponse retire l'echeance
// pour cette route seule, et la borne redevient celle du reseau.
func (s *Server) handleTelechargementFichier(w http.ResponseWriter, r *http.Request) {
	nom := r.PathValue("nom")
	chemin, ok := s.depot.Chemin(nom)
	if !ok {
		s.fail(w, r, http.StatusNotFound, "paquet inconnu", nil)
		return
	}

	f, err := os.Open(chemin)
	if err != nil {
		s.fail(w, r, http.StatusNotFound, "paquet inconnu", err)
		return
	}
	defer f.Close()

	info, err := f.Stat()
	if err != nil {
		s.fail(w, r, http.StatusInternalServerError, "paquet illisible", err)
		return
	}

	if err := http.NewResponseController(w).SetWriteDeadline(time.Time{}); err != nil {
		// Le serveur de test n'expose pas toujours le controleur. On continue :
		// la seule consequence est le delai de 60 s ci-dessus, et un echec ici
		// serait une mauvaise raison de refuser un telechargement.
		s.log.Debug("delai d'ecriture non levable", "err", err)
	}

	// `attachment` et pas `inline` : un .deb ou un .AppImage n'a rien a faire
	// dans un onglet. Le nom est deja passe par `nomSain`, qui n'accepte ni
	// guillemet ni saut de ligne, donc il ne peut pas casser l'en-tete.
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Disposition", `attachment; filename="`+nom+`"`)
	// Une heure, et pas un jour : ces fichiers sont reecrits par une
	// refabrication sans changer de nom.
	w.Header().Set("Cache-Control", "public, max-age=3600")

	// `ServeContent` et non `io.Copy` : il gere les requetes par plage, donc la
	// reprise d'un telechargement interrompu de 175 Mio, et les requetes
	// conditionnelles.
	http.ServeContent(w, r, nom, info.ModTime(), f)
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
