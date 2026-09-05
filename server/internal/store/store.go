// Package store — accès à PostgreSQL.
//
// Toutes les requêtes sont paramétrées. Ce n'est pas une précaution de style :
// l'audit du site d'origine a relevé la concaténation d'entrées utilisateur dans
// chacune de ses requêtes, dont `WHERE username = '$username' AND password =
// '$password'`, qui donnait un contournement d'authentification en tapant
// `admin' -- ` dans le champ identifiant.
package store

import (
	"context"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"

	"nineteen/internal/saison"
	"nineteen/internal/salons"
)

var (
	ErrNotFound      = errors.New("introuvable")
	ErrAlreadyExists = errors.New("existe déjà")
)

type Store struct {
	pool *pgxpool.Pool

	// Le poivre des poignees de presence. Voir `poignee`.
	poivre []byte
}

func Open(ctx context.Context, url string) (*Store, error) {
	cfg, err := pgxpool.ParseConfig(url)
	if err != nil {
		return nil, fmt.Errorf("URL de base invalide : %w", err)
	}

	// Bornes explicites : sans elles, une rafale de requêtes ouvre autant de
	// connexions que PostgreSQL en accepte, puis tout s'effondre d'un coup.
	cfg.MaxConns = 16
	cfg.MinConns = 2
	cfg.MaxConnLifetime = time.Hour
	cfg.MaxConnIdleTime = 15 * time.Minute
	cfg.HealthCheckPeriod = time.Minute

	pool, err := pgxpool.NewWithConfig(ctx, cfg)
	if err != nil {
		return nil, fmt.Errorf("connexion : %w", err)
	}
	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, fmt.Errorf("base injoignable : %w", err)
	}
	poivre := make([]byte, 32)
	if _, err := rand.Read(poivre); err != nil {
		pool.Close()
		return nil, fmt.Errorf("tirage du poivre : %w", err)
	}
	return &Store{pool: pool, poivre: poivre}, nil
}

func (s *Store) Close() { s.pool.Close() }

func (s *Store) Pool() *pgxpool.Pool { return s.pool }

// isUniqueViolation reconnaît le conflit de clé unique, pour le traduire en
// erreur métier plutôt que d'exposer le message de PostgreSQL au client.
func isUniqueViolation(err error) bool {
	var pgErr *pgconn.PgError
	return errors.As(err, &pgErr) && pgErr.Code == "23505"
}

/* ========================================================================== */
/* Joueurs                                                                    */
/* ========================================================================== */

type Player struct {
	ID           int64
	Username     string
	Email        string
	PasswordHash string
	CreatedAt    time.Time
	LastSeenAt   time.Time
	Disabled     bool
}

func (s *Store) CreatePlayer(ctx context.Context, username, email, passwordHash string) (*Player, error) {
	const q = `
		INSERT INTO players (username, email, password_hash)
		VALUES ($1, NULLIF($2, ''), $3)
		RETURNING id, username, COALESCE(email, ''), password_hash, created_at, last_seen_at, disabled`

	var p Player
	err := s.pool.QueryRow(ctx, q, username, email, passwordHash).Scan(
		&p.ID, &p.Username, &p.Email, &p.PasswordHash, &p.CreatedAt, &p.LastSeenAt, &p.Disabled)
	if err != nil {
		if isUniqueViolation(err) {
			return nil, ErrAlreadyExists
		}
		return nil, fmt.Errorf("création du joueur : %w", err)
	}
	return &p, nil
}

func (s *Store) PlayerByUsername(ctx context.Context, username string) (*Player, error) {
	// citext côté schéma : la comparaison est insensible à la casse sans
	// LOWER() dans la requête, ce qui laisse l'index utilisable.
	const q = `
		SELECT id, username, COALESCE(email, ''), password_hash, created_at, last_seen_at, disabled
		FROM players WHERE username = $1`

	var p Player
	err := s.pool.QueryRow(ctx, q, username).Scan(
		&p.ID, &p.Username, &p.Email, &p.PasswordHash, &p.CreatedAt, &p.LastSeenAt, &p.Disabled)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, ErrNotFound
	}
	if err != nil {
		return nil, fmt.Errorf("lecture du joueur : %w", err)
	}
	return &p, nil
}

func (s *Store) TouchPlayer(ctx context.Context, id int64) error {
	_, err := s.pool.Exec(ctx, `UPDATE players SET last_seen_at = now() WHERE id = $1`, id)
	return err
}

/* ========================================================================== */
/* Sessions                                                                   */
/* ========================================================================== */

type Session struct {
	PlayerID  int64
	Username  string
	ExpiresAt time.Time
}

// CreateSession enregistre l'EMPREINTE de la clé, jamais la clé elle-même.
func (s *Store) CreateSession(ctx context.Context, playerID int64, keyHash, ip, userAgent string, ttl time.Duration) error {
	const q = `
		INSERT INTO sessions (player_id, key_hash, ip, user_agent, expires_at)
		VALUES ($1, $2, $3::inet, $4, now() + $5::interval)`
	_, err := s.pool.Exec(ctx, q, playerID, keyHash, ip, truncate(userAgent, 256), ttl)
	if err != nil {
		return fmt.Errorf("création de session : %w", err)
	}
	return nil
}

// SessionByKeyHash résout une session valide et prolonge sa fenêtre d'activité.
func (s *Store) SessionByKeyHash(ctx context.Context, keyHash string) (*Session, error) {
	const q = `
		UPDATE sessions SET last_used_at = now()
		WHERE key_hash = $1 AND expires_at > now() AND revoked_at IS NULL
		RETURNING player_id,
		          (SELECT username FROM players WHERE players.id = sessions.player_id),
		          expires_at`

	var sess Session
	err := s.pool.QueryRow(ctx, q, keyHash).Scan(&sess.PlayerID, &sess.Username, &sess.ExpiresAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, ErrNotFound
	}
	if err != nil {
		return nil, fmt.Errorf("résolution de session : %w", err)
	}
	return &sess, nil
}

func (s *Store) RevokeSession(ctx context.Context, keyHash string) error {
	_, err := s.pool.Exec(ctx,
		`UPDATE sessions SET revoked_at = now() WHERE key_hash = $1 AND revoked_at IS NULL`, keyHash)
	return err
}

// RevokePlayerSessions révoque toutes les sessions d'un joueur.
//
// L'équivalent d'origine, disconnectAll.php, exécutait `DELETE FROM
// nineteen_session WHERE 1` sans la moindre authentification : n'importe qui
// pouvait déconnecter tous les joueurs du jeu en visitant une URL. Ici l'action
// est limitée à un joueur, et le point d'entrée exige sa session.
func (s *Store) RevokePlayerSessions(ctx context.Context, playerID int64) error {
	_, err := s.pool.Exec(ctx,
		`UPDATE sessions SET revoked_at = now() WHERE player_id = $1 AND revoked_at IS NULL`, playerID)
	return err
}

// PurgeExpiredSessions est appelée périodiquement par le serveur.
func (s *Store) PurgeExpiredSessions(ctx context.Context) (int64, error) {
	tag, err := s.pool.Exec(ctx,
		`DELETE FROM sessions WHERE expires_at < now() - interval '7 days'`)
	if err != nil {
		return 0, err
	}
	return tag.RowsAffected(), nil
}

/* ========================================================================== */
/* Scores                                                                     */
/* ========================================================================== */

type LeaderboardEntry struct {
	Rank       int       `json:"rank"`
	Username   string    `json:"username"`
	Score      int64     `json:"score"`
	AchievedAt time.Time `json:"achievedAt"`
}

type Game struct {
	ID         int32   `json:"id"`
	Slug       string  `json:"slug"`
	Name       string  `json:"name"`
	Difficulty string  `json:"difficulty"`
	Multiplier float64 `json:"multiplier"`

	// Le plafond de plausibilite de CE jeu, en points. Il est dans la table
	// depuis la premiere migration et il n'etait lu par personne : le chemin
	// de soumission passait 1 000 000 en dur pour les dix-neuf bornes. Un
	// plafond ecrit une fois et jamais relu ne protege rien, et l'ecart n'est
	// pas cosmetique — le demineur plafonne a 10 000, soit CENT fois moins.
	MaxPlausibleScore int64 `json:"-"`
}

func (s *Store) Games(ctx context.Context) ([]Game, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT id, slug, name, difficulty, multiplier, max_plausible_score
		   FROM games ORDER BY id`)
	if err != nil {
		return nil, fmt.Errorf("liste des jeux : %w", err)
	}
	defer rows.Close()

	var out []Game
	for rows.Next() {
		var g Game
		if err := rows.Scan(&g.ID, &g.Slug, &g.Name, &g.Difficulty, &g.Multiplier,
			&g.MaxPlausibleScore); err != nil {
			return nil, err
		}
		out = append(out, g)
	}
	return out, rows.Err()
}

// RecordScore ne conserve que le meilleur score du joueur pour un jeu donné.
func (s *Store) RecordScore(ctx context.Context, playerID int64, gameID int32, score int64, runID string) (bool, error) {
	const q = `
		INSERT INTO scores (player_id, game_id, score, run_id, achieved_at)
		VALUES ($1, $2, $3, $4, now())
		ON CONFLICT (player_id, game_id) DO UPDATE
		    SET score = EXCLUDED.score,
		        run_id = EXCLUDED.run_id,
		        achieved_at = EXCLUDED.achieved_at
		    WHERE scores.score < EXCLUDED.score
		RETURNING true`

	var improved bool
	err := s.pool.QueryRow(ctx, q, playerID, gameID, score, runID).Scan(&improved)
	if errors.Is(err, pgx.ErrNoRows) {
		return false, nil // score inférieur au meilleur : conservé tel quel
	}
	if err != nil {
		return false, fmt.Errorf("enregistrement du score : %w", err)
	}
	return improved, nil
}

// Leaderboard renvoie le classement d'un jeu, ou le classement général si
// gameID vaut 0. Les bornes de pagination sont contraintes ici, pas par
// l'appelant : `LIMIT $offset` avec une valeur venue du client était l'une des
// injections de l'original.
func (s *Store) Leaderboard(ctx context.Context, gameID int32, limit, offset int) ([]LeaderboardEntry, error) {
	if limit <= 0 || limit > 100 {
		limit = 20
	}
	if offset < 0 || offset > 10000 {
		offset = 0
	}

	var q string
	var args []any
	if gameID > 0 {
		q = `
			SELECT RANK() OVER (ORDER BY sc.score DESC) AS rank,
			       p.username, sc.score, sc.achieved_at
			FROM scores sc
			JOIN players p ON p.id = sc.player_id
			WHERE sc.game_id = $1 AND NOT p.disabled AND sc.score > 0
			ORDER BY sc.score DESC, sc.achieved_at ASC
			LIMIT $2 OFFSET $3`
		args = []any{gameID, limit, offset}
	} else {
		// Classement général : somme des scores pondérés par le multiplicateur
		// du jeu, comme dans la V1 — mais calculée par la base, pas reconstruite
		// à coups de requêtes imbriquées dans une boucle PHP.
		q = `
			SELECT RANK() OVER (ORDER BY total DESC) AS rank, username, total, last_at
			FROM (
			    SELECT p.username,
			           FLOOR(SUM(sc.score * g.multiplier))::bigint AS total,
			           MAX(sc.achieved_at) AS last_at
			    FROM scores sc
			    JOIN players p ON p.id = sc.player_id
			    JOIN games   g ON g.id = sc.game_id
			    WHERE NOT p.disabled
			    GROUP BY p.username
			    HAVING SUM(sc.score) > 0
			) t
			ORDER BY total DESC
			LIMIT $1 OFFSET $2`
		args = []any{limit, offset}
	}

	rows, err := s.pool.Query(ctx, q, args...)
	if err != nil {
		return nil, fmt.Errorf("classement : %w", err)
	}
	defer rows.Close()

	out := make([]LeaderboardEntry, 0, limit)
	for rows.Next() {
		var e LeaderboardEntry
		if err := rows.Scan(&e.Rank, &e.Username, &e.Score, &e.AchievedAt); err != nil {
			return nil, err
		}
		out = append(out, e)
	}
	return out, rows.Err()
}

// PlayerScores renvoie les scores d'un joueur, jeu par jeu.
/* ========================================================================== */
/* Saison classee                                                             */
/* ========================================================================== */

// SaisonMeilleurs rend, pour la fenetre demandee, le MEILLEUR score de chaque
// joueur sur chaque creneau, avec le nombre de parties valides qu'il y a
// faites.
//
// TROIS FILTRES, ET CHACUN A UNE RAISON.
//
//	verdict = 'ok'   Seul un score RECALCULE par le serveur depuis le journal
//	                 scelle entre au classement. C'est tout l'interet du
//	                 renversement de preuve : on ne classe pas ce que le client
//	                 annonce.
//	score > 0        Sans lui, la strategie optimale serait d'inserer un jeton
//	                 et de mourir aussitot sur une borne deserte : un zero
//	                 donnerait la premiere place, donc cent points, pour n'avoir
//	                 rien joue. C'est le seul trou par lequel le bareme se
//	                 percerait, et il se bouche ici.
//	NOT p.disabled   Un compte ferme ne tient pas un podium.
//
// La date rendue est celle du MEILLEUR score, pas du dernier : c'est elle qui
// tranche les egalites, et la regle des bornes est que celui qui a pose le
// score le premier le detient.
func (s *Store) SaisonMeilleurs(ctx context.Context, debut, fin time.Time) ([]saison.Meilleur, error) {
	const q = `
		WITH valides AS (
		    SELECT r.player_id, r.game_id, r.score, r.submitted_at
		    FROM runs r
		    WHERE r.verdict = 'ok'
		      AND r.submitted_at >= $1 AND r.submitted_at < $2
		      AND r.score > 0
		),
		comptes AS (
		    SELECT player_id, game_id, count(*) AS parties
		    FROM valides GROUP BY player_id, game_id
		),
		sommets AS (
		    SELECT DISTINCT ON (player_id, game_id)
		           player_id, game_id, score, submitted_at
		    FROM valides
		    ORDER BY player_id, game_id, score DESC, submitted_at ASC
		)
		SELECT p.username, g.slug, g.name, g.difficulty, g.multiplier,
		       s.score, s.submitted_at, c.parties
		FROM sommets s
		JOIN comptes c ON c.player_id = s.player_id AND c.game_id = s.game_id
		JOIN players p ON p.id = s.player_id
		JOIN games   g ON g.id = s.game_id
		WHERE NOT p.disabled
		ORDER BY g.id, s.score DESC`

	rows, err := s.pool.Query(ctx, q, debut, fin)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	out := []saison.Meilleur{}
	for rows.Next() {
		var m saison.Meilleur
		if err := rows.Scan(&m.Pseudo, &m.Jeu, &m.JeuNom, &m.Difficulte,
			&m.Multiplicateur, &m.Score, &m.Quand, &m.Parties); err != nil {
			return nil, err
		}
		out = append(out, m)
	}
	return out, rows.Err()
}

// FicheJoueur — ce qu'on sait d'un joueur en dehors d'une saison.
type FicheJoueur struct {
	Pseudo    string    `json:"pseudo"`
	Inscrit   time.Time `json:"inscrit"`
	VuLe      time.Time `json:"vuLe"`
	Parties   int       `json:"parties"`   // parties valides, toutes saisons
	Meilleurs []Sommet  `json:"meilleurs"` // record personnel par creneau
}

// Sommet — le record personnel d'un joueur sur un creneau, toutes saisons
// confondues, et la place que ce record tient aujourd'hui.
type Sommet struct {
	Jeu        string    `json:"jeu"`
	JeuNom     string    `json:"jeuNom"`
	Difficulte string    `json:"difficulte"`
	Score      int64     `json:"score"`
	Quand      time.Time `json:"quand"`
	Rang       int       `json:"rang"`    // sa place au classement de tous les temps
	Joueurs    int       `json:"joueurs"` // combien ont un score sur ce creneau
}

// Joueur rend la fiche d'un joueur, ou ErrNotFound.
//
// Le rang de chaque record est calcule PAR LA BASE, dans la meme requete que le
// record : le ramener en Go demanderait de charger tout le classement de chaque
// creneau pour n'en garder qu'une ligne.
func (s *Store) Joueur(ctx context.Context, pseudo string) (*FicheJoueur, error) {
	const qp = `
		SELECT username, created_at, last_seen_at
		FROM players WHERE username = $1 AND NOT disabled`

	var f FicheJoueur
	err := s.pool.QueryRow(ctx, qp, pseudo).Scan(&f.Pseudo, &f.Inscrit, &f.VuLe)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, ErrNotFound
	}
	if err != nil {
		return nil, err
	}

	const qs = `
		WITH classe AS (
		    SELECT sc.player_id, sc.game_id, sc.score, sc.achieved_at,
		           RANK() OVER (PARTITION BY sc.game_id ORDER BY sc.score DESC) AS rang,
		           COUNT(*) OVER (PARTITION BY sc.game_id) AS joueurs
		    FROM scores sc
		    JOIN players p ON p.id = sc.player_id
		    WHERE NOT p.disabled AND sc.score > 0
		)
		SELECT g.slug, g.name, g.difficulty, c.score, c.achieved_at, c.rang, c.joueurs
		FROM classe c
		JOIN games   g ON g.id = c.game_id
		JOIN players p ON p.id = c.player_id
		WHERE p.username = $1
		ORDER BY c.rang, g.id`

	rows, err := s.pool.Query(ctx, qs, pseudo)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	f.Meilleurs = []Sommet{}
	for rows.Next() {
		var m Sommet
		if err := rows.Scan(&m.Jeu, &m.JeuNom, &m.Difficulte, &m.Score,
			&m.Quand, &m.Rang, &m.Joueurs); err != nil {
			return nil, err
		}
		f.Meilleurs = append(f.Meilleurs, m)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}

	const qn = `
		SELECT count(*) FROM runs r JOIN players p ON p.id = r.player_id
		WHERE p.username = $1 AND r.verdict = 'ok' AND r.score > 0`
	if err := s.pool.QueryRow(ctx, qn, pseudo).Scan(&f.Parties); err != nil {
		return nil, err
	}
	return &f, nil
}

func (s *Store) PlayerScores(ctx context.Context, playerID int64) (map[string]int64, error) {
	rows, err := s.pool.Query(ctx, `
		SELECT g.slug, sc.score
		FROM scores sc JOIN games g ON g.id = sc.game_id
		WHERE sc.player_id = $1`, playerID)
	if err != nil {
		return nil, fmt.Errorf("scores du joueur : %w", err)
	}
	defer rows.Close()

	out := map[string]int64{}
	for rows.Next() {
		var slug string
		var score int64
		if err := rows.Scan(&slug, &score); err != nil {
			return nil, err
		}
		out[slug] = score
	}
	return out, rows.Err()
}

/* ========================================================================== */
/* Parties (journal signé)                                                    */
/* ========================================================================== */

// BeginRun ouvre une partie et renvoie son identifiant et son secret HMAC.
//
// Le secret est propre à la partie et ne quitte le serveur qu'une fois : c'est
// lui qui scelle le journal d'événements. Il remplace le « secret » de la V1,
// qui était compilé dans le binaire distribué à tous les joueurs.
func (s *Store) BeginRun(ctx context.Context, playerID int64, gameID int32, secret []byte, seed int64) (string, error) {
	const q = `
		INSERT INTO runs (player_id, game_id, secret, seed)
		VALUES ($1, $2, $3, $4)
		RETURNING id::text`
	var id string
	if err := s.pool.QueryRow(ctx, q, playerID, gameID, secret, seed).Scan(&id); err != nil {
		return "", fmt.Errorf("ouverture de partie : %w", err)
	}
	return id, nil
}

type Run struct {
	ID        string
	PlayerID  int64
	GameID    int32
	Secret    []byte
	Seed      int64
	StartedAt time.Time
	Submitted bool
}

func (s *Store) RunByID(ctx context.Context, id string, playerID int64) (*Run, error) {
	const q = `
		SELECT id::text, player_id, game_id, secret, seed, started_at, submitted_at IS NOT NULL
		FROM runs WHERE id = $1::uuid AND player_id = $2`

	var r Run
	err := s.pool.QueryRow(ctx, q, id, playerID).Scan(
		&r.ID, &r.PlayerID, &r.GameID, &r.Secret, &r.Seed, &r.StartedAt, &r.Submitted)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, ErrNotFound
	}
	if err != nil {
		return nil, fmt.Errorf("lecture de partie : %w", err)
	}
	return &r, nil
}

// CloseRun marque la partie comme soumise. L'unicité est garantie par la clause
// WHERE : une seconde soumission ne modifie aucune ligne, ce qui bloque le rejeu.
func (s *Store) CloseRun(ctx context.Context, id string, score int64, verdict string) (bool, error) {
	const q = `
		UPDATE runs SET submitted_at = now(), score = $2, verdict = $3
		WHERE id = $1::uuid AND submitted_at IS NULL`
	tag, err := s.pool.Exec(ctx, q, id, score, verdict)
	if err != nil {
		return false, fmt.Errorf("clôture de partie : %w", err)
	}
	return tag.RowsAffected() == 1, nil
}

/* ========================================================================== */
/* Limitation de débit                                                        */
/* ========================================================================== */

// RateLimit compte les tentatives par clé sur une fenêtre glissante et indique
// si la limite est franchie. Stocké en base plutôt qu'en mémoire pour que la
// limite tienne même avec plusieurs instances du serveur.
func (s *Store) RateLimit(ctx context.Context, key string, limit int, window time.Duration) (allowed bool, retryAfter time.Duration, err error) {
	const q = `
		INSERT INTO rate_limits (key, window_start, count)
		VALUES ($1, now(), 1)
		ON CONFLICT (key) DO UPDATE
		    SET count = CASE
		            WHEN rate_limits.window_start < now() - $2::interval THEN 1
		            ELSE rate_limits.count + 1
		        END,
		        window_start = CASE
		            WHEN rate_limits.window_start < now() - $2::interval THEN now()
		            ELSE rate_limits.window_start
		        END
		RETURNING count, window_start`

	var count int
	var start time.Time
	if err := s.pool.QueryRow(ctx, q, key, window).Scan(&count, &start); err != nil {
		// Un incident sur la table de limitation ne doit pas rendre le service
		// indisponible ; on laisse passer et on journalise côté appelant.
		return true, 0, fmt.Errorf("limitation de débit : %w", err)
	}
	if count > limit {
		return false, time.Until(start.Add(window)), nil
	}
	return true, 0, nil
}

func (s *Store) PurgeRateLimits(ctx context.Context) error {
	_, err := s.pool.Exec(ctx, `DELETE FROM rate_limits WHERE window_start < now() - interval '1 day'`)
	return err
}

/* ========================================================================== */
/* Présence                                                                   */
/* ========================================================================== */

// Peer — un joueur vu dans la salle.
//
// `Verified` dit si le pseudo est celui d'un compte authentifié ou un simple
// nom déclaré. On ne peut pas exiger de compte pour se montrer — c'est la règle
// du projet — mais on peut dire honnêtement lequel des deux on affiche.
type Peer struct {
	ClientID string  `json:"clientId"`
	Nickname string  `json:"nickname"`
	Verified bool    `json:"verified"`
	X        float32 `json:"x"`
	Y        float32 `json:"y"`
	Z        float32 `json:"z"`
	Yaw      float32 `json:"yaw"`
	// Eye — la hauteur de l'œil au-dessus des pieds, en mètres.
	//
	// `Y` est la position de la CAMÉRA, pas celle du sol : c'est ce que le
	// client publie depuis le premier jour. Les pieds sont donc `Y - Eye`, et
	// c'est ce dont un client a besoin depuis que les pairs ont un corps à
	// poser. Zéro signifie « ce client ne la publie pas » — voir la migration
	// 0004, qui explique pourquoi c'est une colonne de plus et non un
	// changement de sens de `Y`.
	Eye     float32 `json:"eye"`
	Cabinet string  `json:"cabinet"`
	Game    string  `json:"game"`
	Score   int32   `json:"score"`
}

// TouchPresence écrit la position d'un client et renvoie celle des AUTRES.
//
// Un seul aller-retour pour les deux : la présence est le seul échange du jeu
// qui soit périodique, et lui faire coûter deux requêtes doublerait le trafic
// pour rien. C'est aussi ce qui la rend utilisable sur le transport qu'on a —
// HTTP/1.1 sans connexion persistante.
//
// `ttl` borne ce qu'on considère vivant. Un client qui s'arrête d'émettre
// disparaît donc de lui-même : il n'y a pas de « déconnexion » à manquer, ce
// qui est exactement ce qu'on veut d'un jeu qu'on ferme d'un coup de croix.
func (s *Store) TouchPresence(ctx context.Context, p Peer, playerID *int64, ttl time.Duration) ([]Peer, error) {
	const upsert = `
		INSERT INTO presence (client_id, player_id, nickname, verified,
		                      x, y, z, yaw, eye, cabinet, game, score, seen_at)
		VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, now())
		ON CONFLICT (client_id) DO UPDATE SET
		    player_id = EXCLUDED.player_id,
		    nickname  = EXCLUDED.nickname,
		    verified  = EXCLUDED.verified,
		    x = EXCLUDED.x, y = EXCLUDED.y, z = EXCLUDED.z, yaw = EXCLUDED.yaw,
		    eye       = EXCLUDED.eye,
		    cabinet   = EXCLUDED.cabinet,
		    game      = EXCLUDED.game,
		    score     = EXCLUDED.score,
		    seen_at   = now()
		WHERE presence.player_id IS NULL
		   OR presence.player_id = EXCLUDED.player_id`

	if _, err := s.pool.Exec(ctx, upsert,
		p.ClientID, playerID, truncate(p.Nickname, 24), p.Verified,
		p.X, p.Y, p.Z, p.Yaw, p.Eye,
		truncate(p.Cabinet, 32), truncate(p.Game, 32), p.Score); err != nil {
		return nil, fmt.Errorf("présence : %w", err)
	}

	// Les autres, et seulement les vivants. La borne est passée en secondes
	// plutôt qu'interpolée dans le SQL : `make_interval` prend un paramètre,
	// une concaténation prendrait une injection.
	const list = `
		SELECT client_id, nickname, verified, x, y, z, yaw, eye, cabinet, game, score
		FROM presence
		WHERE client_id <> $1
		  AND seen_at > now() - make_interval(secs => $2)
		ORDER BY nickname
		LIMIT 32`

	rows, err := s.pool.Query(ctx, list, p.ClientID, ttl.Seconds())
	if err != nil {
		return nil, fmt.Errorf("présence : %w", err)
	}
	defer rows.Close()

	peers := []Peer{}
	for rows.Next() {
		var q Peer
		if err := rows.Scan(&q.ClientID, &q.Nickname, &q.Verified,
			&q.X, &q.Y, &q.Z, &q.Yaw, &q.Eye,
			&q.Cabinet, &q.Game, &q.Score); err != nil {
			return nil, err
		}
		// On rend une POIGNEE, jamais la cle. `client_id` est ce qui decide
		// quelle ligne une requete reecrit : le publier a tous les pairs
		// revenait a donner a chacun le droit d'ecrire chez les autres. Le
		// client ne s'en sert que pour reconnaitre un pair d'une trame a la
		// suivante (ns_realtime.c:233, il le range dans `p->id` et rien de
		// plus), donc une valeur stable et opaque suffit exactement.
		q.ClientID = s.poignee(q.ClientID)
		peers = append(peers, q)
	}
	return peers, rows.Err()
}

// poignee — l'identite PUBLIQUE d'un pair, derivee de sa cle privee.
//
// HMAC et non un hachage nu : `client_id` est un jeton court choisi par le
// client, donc enumerable. Un SHA-256 sans cle se retournerait par table.
//
// La cle est tiree au demarrage et jamais persistee. Une poignee change donc
// au redemarrage du serveur — sans consequence, la presence expire en 12 s et
// le client ne fait que comparer des poignees entre elles.
func (s *Store) poignee(clientID string) string {
	m := hmac.New(sha256.New, s.poivre)
	m.Write([]byte(clientID))
	return hex.EncodeToString(m.Sum(nil)[:12])
}

// DropPresence retire un client immédiatement, quand il a la politesse de le
// dire. Le TTL suffirait ; ceci évite juste un fantôme de quelques secondes.
// Le predicat sur `player_id` est le meme que celui de l'upsert, et pour la
// meme raison : sans lui, « je pars » supprimait la ligne de n'importe qui.
func (s *Store) DropPresence(ctx context.Context, clientID string, playerID *int64) error {
	_, err := s.pool.Exec(ctx,
		`DELETE FROM presence
		  WHERE client_id = $1
		    AND (player_id IS NULL OR player_id IS NOT DISTINCT FROM $2)`,
		clientID, playerID)
	return err
}

func (s *Store) PurgePresence(ctx context.Context, ttl time.Duration) error {
	_, err := s.pool.Exec(ctx,
		`DELETE FROM presence WHERE seen_at < now() - make_interval(secs => $1)`, ttl.Seconds())
	return err
}

/* ========================================================================== */
/* Fantômes                                                                   */
/* ========================================================================== */

// GhostInfo — de quoi lister les fantômes disponibles sans transporter leur
// journal, qui pèse mille fois plus que la ligne qui le décrit.
type GhostInfo struct {
	RunID    string `json:"runId"`
	Username string `json:"username"`
	Score    int64  `json:"score"`
	Seed     int64  `json:"seed"`
	Ticks    int    `json:"ticks"`
}

// Ghost — le journal d'entrées lui-même, avec la graine sur laquelle il se
// rejoue. Les deux voyagent ensemble parce que l'un sans l'autre ne rejoue rien.
type Ghost struct {
	GhostInfo
	Inputs string `json:"inputs"`
}

// SaveGhost attache un journal d'entrées à une partie DÉJÀ soumise et acceptée.
//
// La clause `WHERE` est la protection, et c'est la seule qui compte : on ne
// peut déposer un fantôme que sur une partie qui nous appartient, qui a été
// close, et dont le serveur a lui-même recalculé le score. Un journal d'entrées
// n'est donc jamais une manière d'entrer un score par la porte de derrière — il
// n'ajoute rien à ce que la partie valait déjà.
//
// `ON CONFLICT DO NOTHING` : un fantôme est écrit une fois. Le renvoyer ne doit
// pas permettre de remplacer les entrées d'un score déjà publié.
func (s *Store) SaveGhost(ctx context.Context, runID string, playerID int64, inputs string) (bool, error) {
	const q = `
		INSERT INTO ghosts (run_id, player_id, game_id, seed, score, inputs)
		SELECT r.id, r.player_id, r.game_id, r.seed, r.score, $3
		FROM runs r
		WHERE r.id = $1::uuid
		  AND r.player_id = $2
		  AND r.submitted_at IS NOT NULL
		  AND r.score IS NOT NULL
		  AND r.verdict = 'ok'
		ON CONFLICT (run_id) DO NOTHING`

	tag, err := s.pool.Exec(ctx, q, runID, playerID, inputs)
	if err != nil {
		return false, fmt.Errorf("fantôme : %w", err)
	}
	return tag.RowsAffected() == 1, nil
}

// Ghosts liste les meilleurs fantômes d'un jeu, sans leur journal.
func (s *Store) Ghosts(ctx context.Context, gameID int32, limit int) ([]GhostInfo, error) {
	if limit < 1 || limit > 50 {
		limit = 10
	}
	const q = `
		SELECT g.run_id::text, p.username::text, g.score, g.seed,
		       length(g.inputs) - length(replace(g.inputs, E'\n', ''))
		FROM ghosts g
		JOIN players p ON p.id = g.player_id
		WHERE g.game_id = $1
		ORDER BY g.score DESC, g.created_at ASC
		LIMIT $2`

	rows, err := s.pool.Query(ctx, q, gameID, limit)
	if err != nil {
		return nil, fmt.Errorf("fantômes : %w", err)
	}
	defer rows.Close()

	out := []GhostInfo{}
	for rows.Next() {
		var g GhostInfo
		if err := rows.Scan(&g.RunID, &g.Username, &g.Score, &g.Seed, &g.Ticks); err != nil {
			return nil, err
		}
		out = append(out, g)
	}
	return out, rows.Err()
}

// GhostByID rend un fantôme complet, journal compris.
func (s *Store) GhostByID(ctx context.Context, runID string) (*Ghost, error) {
	const q = `
		SELECT g.run_id::text, p.username::text, g.score, g.seed,
		       length(g.inputs) - length(replace(g.inputs, E'\n', '')), g.inputs
		FROM ghosts g
		JOIN players p ON p.id = g.player_id
		WHERE g.run_id = $1::uuid`

	var g Ghost
	err := s.pool.QueryRow(ctx, q, runID).Scan(
		&g.RunID, &g.Username, &g.Score, &g.Seed, &g.Ticks, &g.Inputs)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, ErrNotFound
	}
	if err != nil {
		return nil, fmt.Errorf("fantôme : %w", err)
	}
	return &g, nil
}

// RunSeed rend la graine et le jeu d'une partie, quel qu'en soit le joueur.
//
// C'est ce qui permet d'ouvrir une partie SUR LA MÊME GRAINE qu'un fantôme —
// sans quoi un duel n'en est pas un : deux joueurs sur deux graines différentes
// ne jouent pas la même partie, ils jouent deux parties.
//
// Aucun secret ne sort d'ici : une graine n'est pas un secret, c'est justement
// ce qui doit être PARTAGÉ. Le secret HMAC de la nouvelle partie, lui, reste
// tiré au hasard pour elle seule.
func (s *Store) RunSeedForGhost(ctx context.Context, runID string) (seed int64, gameID int32, err error) {
	const q = `SELECT seed, game_id FROM ghosts WHERE run_id = $1::uuid`
	err = s.pool.QueryRow(ctx, q, runID).Scan(&seed, &gameID)
	if errors.Is(err, pgx.ErrNoRows) {
		return 0, 0, ErrNotFound
	}
	if err != nil {
		return 0, 0, fmt.Errorf("graine du fantôme : %w", err)
	}
	return seed, gameID, nil
}

/* ========================================================================== */

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n]
}

/* ========================================================================== */
/* Salons du Couperet                                                         */
/* ========================================================================== */

// Le magasin ne JUGE rien d'un salon : il charge, il laisse `internal/salons`
// decider, il reecrit. C'est la meme separation que pour les parties, ou le
// score est recalcule par `internal/runs` et non par une requete.
//
// La consequence pratique vaut d'etre ecrite : aucune de ces requetes ne
// contient de regle de jeu. On n'y trouvera ni « la place la plus petite », ni
// « qui herite du salon », ni « depuis quand un occupant est mort ». Tout cela
// se teste sans base, et c'est ce qui rend ces requetes-ci ennuyeuses.

// requeteur — le minimum qu'il faut savoir faire pour lire ou ecrire un salon.
//
// `*pgxpool.Pool` et `pgx.Tx` le satisfont tous les deux. Ce n'est pas de
// l'abstraction gratuite : les lectures se font hors transaction, les
// modifications a l'interieur d'une, et sans cette interface le chargement
// serait ecrit deux fois — donc, un jour, differemment.
type requeteur interface {
	Query(ctx context.Context, sql string, args ...any) (pgx.Rows, error)
	QueryRow(ctx context.Context, sql string, args ...any) pgx.Row
	Exec(ctx context.Context, sql string, args ...any) (pgconn.CommandTag, error)
}

// Le pseudo du proprietaire vient de `players`, jamais d'une copie dans
// `salons` : un compte renomme ne doit pas laisser un ancien nom dans une liste
// publique.
const salonColonnes = `s.id, s.code, s.nom, s.places, s.camps, s.prive, s.etat,
	       s.relais, s.proprietaire, p.username::text, s.cree_a, s.change_a`

// lireSalon decode une ligne de salon. Les entiers etroits sont scannes dans
// leur type exact puis convertis : la colonne est un `smallint`, et compter sur
// une conversion implicite du pilote serait parier sur un detail qu'aucun test
// d'ici ne couvre.
func lireSalon(row pgx.Row) (int64, *salons.Salon, error) {
	var (
		id            int64
		places, camps int16
		etat          string
		sal           salons.Salon
	)
	err := row.Scan(&id, &sal.Code, &sal.Nom, &places, &camps, &sal.Prive, &etat,
		&sal.Relais, &sal.Proprietaire, &sal.ProprietairePseudo, &sal.CreeA, &sal.ChangeA)
	if errors.Is(err, pgx.ErrNoRows) {
		return 0, nil, ErrNotFound
	}
	if err != nil {
		return 0, nil, fmt.Errorf("lecture du salon : %w", err)
	}
	sal.Places = int(places)
	sal.Camps = int(camps)
	sal.Etat = salons.Etat(etat)
	return id, &sal, nil
}

// occupantsDe charge les places de plusieurs salons en une requete.
//
// Une requete et non une par salon : la liste publique en montre plusieurs
// dizaines, et huit places chacun feraient autant d'allers-retours que de
// lignes affichees.
func occupantsDe(ctx context.Context, q requeteur, ids []int64) (map[int64][]salons.Occupant, error) {
	out := make(map[int64][]salons.Occupant, len(ids))
	if len(ids) == 0 {
		return out, nil
	}
	const req = `
		SELECT salon_id, place, player_id, pseudo, camp, points, fusibles,
		       vivante, borne, entre_a, battu_a
		FROM salon_places
		WHERE salon_id = ANY($1)
		ORDER BY salon_id, place`

	rows, err := q.Query(ctx, req, ids)
	if err != nil {
		return nil, fmt.Errorf("places du salon : %w", err)
	}
	defer rows.Close()

	for rows.Next() {
		var (
			salonID     int64
			place, camp int16
			o           salons.Occupant
		)
		if err := rows.Scan(&salonID, &place, &o.PlayerID, &o.Pseudo, &camp,
			&o.Points, &o.Fusibles, &o.Vivante, &o.Borne, &o.EntreA, &o.BattuA); err != nil {
			return nil, err
		}
		o.Place = int(place)
		o.Camp = int(camp)
		out[salonID] = append(out[salonID], o)
	}
	return out, rows.Err()
}

// salonParCode charge un salon complet. `verrou` pose un FOR UPDATE, obligatoire
// des qu'on s'apprete a ecrire : sans lui, deux entrees simultanees liraient la
// meme place libre et la seconde echouerait sur la cle primaire au lieu de
// prendre la place suivante.
func salonParCode(ctx context.Context, q requeteur, code string, verrou bool) (int64, *salons.Salon, error) {
	req := `SELECT ` + salonColonnes + `
		FROM salons s JOIN players p ON p.id = s.proprietaire
		WHERE s.code = $1`
	if verrou {
		// « OF s » et non un FOR UPDATE nu : verrouiller aussi la ligne de
		// `players` bloquerait deux salons du meme proprietaire l'un derriere
		// l'autre, pour rien.
		req += ` FOR UPDATE OF s`
	}
	id, sal, err := lireSalon(q.QueryRow(ctx, req, code))
	if err != nil {
		return 0, nil, err
	}
	places, err := occupantsDe(ctx, q, []int64{id})
	if err != nil {
		return 0, nil, err
	}
	sal.Occupants = places[id]
	return id, sal, nil
}

// ecrireSalon reecrit ce que les regles ont pu changer.
//
// Les trois colonnes partent ensemble et sans condition, meme quand rien n'a
// bouge. Ecrire « seulement si ca a change » demanderait de comparer avant et
// apres, c'est-a-dire de redire ici ce que les regles ont decide — et c'est
// exactement le genre de redite qui finit par se tromper de sens.
func ecrireSalon(ctx context.Context, q requeteur, id int64, sal *salons.Salon) error {
	_, err := q.Exec(ctx,
		`UPDATE salons SET etat = $2, proprietaire = $3, change_a = $4 WHERE id = $1`,
		id, string(sal.Etat), sal.Proprietaire, sal.ChangeA)
	if err != nil {
		return fmt.Errorf("mise a jour du salon : %w", err)
	}
	return nil
}

// vider retire les places que le faucheur vient de rendre.
func vider(ctx context.Context, q requeteur, id int64, places []int) error {
	if len(places) == 0 {
		return nil
	}
	nums := make([]int16, len(places))
	for i, p := range places {
		nums[i] = int16(p)
	}
	_, err := q.Exec(ctx,
		`DELETE FROM salon_places WHERE salon_id = $1 AND place = ANY($2)`, id, nums)
	if err != nil {
		return fmt.Errorf("liberation de place : %w", err)
	}
	return nil
}

// CreerSalon ouvre un salon et y assied son createur.
//
// `maxParProprietaire` borne le nombre de salons qu'un meme joueur tient
// ouverts. La borne est passee par l'appelant plutot qu'ecrite ici : c'est une
// politique de service, elle vit avec les autres, dans internal/api.
//
// UNE SEULE HORLOGE POUR TOUS LES SALONS, et c'est celle de l'application.
//
// Toutes les dates de ces deux tables sont ECRITES depuis Go — jamais par un
// `DEFAULT now()` — et toutes les comparaisons de peremption se font en Go, dans
// `internal/salons`. Le melange serait le vrai danger : si la creation datait de
// l'horloge de la base et le battement de celle du serveur, un decalage de
// quelques secondes entre les deux machines suffirait a faucher des places
// vivantes, ou a en garder des mortes, sans que rien ne le dise. Le prix de
// cette regle est qu'un deploiement a plusieurs serveurs demande des horloges
// d'accord entre elles — ce que NTP fait deja, et qui se diagnostique, alors
// qu'un ecart base/serveur ne se voit nulle part.
func (s *Store) CreerSalon(ctx context.Context, nom string, places, camps int, prive bool,
	playerID int64, pseudo string, maxParProprietaire int, maintenant time.Time) (*salons.Salon, error) {

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return nil, fmt.Errorf("creation de salon : %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var ouverts int
	if err := tx.QueryRow(ctx,
		`SELECT count(*) FROM salons WHERE proprietaire = $1 AND etat <> 'fini'`,
		playerID).Scan(&ouverts); err != nil {
		return nil, fmt.Errorf("comptage des salons : %w", err)
	}
	if ouverts >= maxParProprietaire {
		return nil, salons.ErrTropDeSalons
	}

	relais, err := salons.TirerRelais()
	if err != nil {
		return nil, err
	}

	// Le code est TIRE, donc il peut collisionner. A 2^30 codes et quelques
	// dizaines de salons vivants, la collision est un evenement qu'on n'a jamais
	// vu — mais « jamais vu » n'est pas « impossible », et une collision non
	// traitee rendrait au joueur une erreur interne pour un tirage malchanceux.
	//
	// Chaque essai se fait dans un POINT DE REPRISE et non dans la transaction
	// nue : dans PostgreSQL, une violation de contrainte avorte la transaction
	// entiere, donc reessayer sans point de reprise echouerait sur toutes les
	// requetes suivantes, y compris celles qui n'ont rien a voir.
	var (
		id  int64
		sal *salons.Salon
	)
	for essai := 0; essai < 8; essai++ {
		code, cerr := salons.TirerCode()
		if cerr != nil {
			return nil, cerr
		}

		point, perr := tx.Begin(ctx)
		if perr != nil {
			return nil, fmt.Errorf("creation de salon : %w", perr)
		}
		const req = `
			INSERT INTO salons (code, nom, places, camps, prive, relais, proprietaire,
			                    cree_a, change_a)
			VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $8)
			RETURNING id`
		err = point.QueryRow(ctx, req, code, nom, int16(places), int16(camps), prive,
			relais, playerID, maintenant).Scan(&id)
		if err != nil {
			_ = point.Rollback(ctx)
			if isUniqueViolation(err) {
				continue
			}
			return nil, fmt.Errorf("creation de salon : %w", err)
		}
		if err := point.Commit(ctx); err != nil {
			return nil, fmt.Errorf("creation de salon : %w", err)
		}
		sal = &salons.Salon{
			Code: code, Nom: nom, Places: places, Camps: camps, Prive: prive,
			Etat: salons.Attente, Relais: relais,
			Proprietaire: playerID, ProprietairePseudo: pseudo,
			CreeA: maintenant, ChangeA: maintenant,
		}
		break
	}
	if sal == nil {
		return nil, errors.New("aucun code de salon libre apres huit tirages")
	}

	// La place 0 revient au createur — c'est `Asseoir` qui le decide, pas cette
	// requete : la plus petite place libre d'un salon neuf EST la zero.
	place, err := sal.Asseoir(maintenant, playerID, pseudo)
	if err != nil {
		return nil, err
	}
	if err := poserPlace(ctx, tx, id, sal, place); err != nil {
		return nil, err
	}
	if err := tx.Commit(ctx); err != nil {
		return nil, fmt.Errorf("creation de salon : %w", err)
	}
	return sal, nil
}

// occupantA retrouve la ligne que les regles viennent d'ecrire pour une place.
//
// Rendre une erreur plutot qu'indexer : `sal.Occupants` est ce que le paquet de
// regles a produit, et si la place demandee n'y est pas, c'est ce paquet qui a
// tort. Un `panic` sur un index perdrait le seul renseignement utile — laquelle.
func occupantA(sal *salons.Salon, place int) (*salons.Occupant, error) {
	for i := range sal.Occupants {
		if sal.Occupants[i].Place == place {
			return &sal.Occupants[i], nil
		}
	}
	return nil, fmt.Errorf("place %d absente du salon", place)
}

// poserPlace ecrit la ligne d'un occupant, telle que les regles l'ont produite.
func poserPlace(ctx context.Context, q requeteur, id int64, sal *salons.Salon, place int) error {
	o, err := occupantA(sal, place)
	if err != nil {
		return err
	}
	const req = `
		INSERT INTO salon_places (salon_id, place, player_id, pseudo, camp,
		                          points, fusibles, vivante, borne, entre_a, battu_a)
		VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
		ON CONFLICT (salon_id, place) DO UPDATE SET
		    player_id = EXCLUDED.player_id,
		    pseudo    = EXCLUDED.pseudo,
		    camp      = EXCLUDED.camp,
		    battu_a   = EXCLUDED.battu_a`
	if _, err := q.Exec(ctx, req, id, int16(o.Place), o.PlayerID, o.Pseudo, int16(o.Camp),
		o.Points, o.Fusibles, o.Vivante, o.Borne, o.EntreA, o.BattuA); err != nil {
		return fmt.Errorf("attribution de place : %w", err)
	}
	return nil
}

// SalonsPublics rend les salons que la liste doit montrer.
//
// Le filtre SQL — non prive, en attente — est une PRE-SELECTION, pas la regle :
// c'est `Ouvert()` qui tranche, apres que le faucheur a retire les occupants
// disparus. Un salon dont tout le monde vient de s'evaporer sort donc de la
// liste au moment ou on la lit, sans attendre le menage periodique.
func (s *Store) SalonsPublics(ctx context.Context, maintenant time.Time, limite int) ([]salons.Salon, error) {
	if limite < 1 || limite > 100 {
		limite = 50
	}
	req := `SELECT ` + salonColonnes + `
		FROM salons s JOIN players p ON p.id = s.proprietaire
		WHERE NOT s.prive AND s.etat = 'attente'
		ORDER BY s.cree_a DESC
		LIMIT $1`

	rows, err := s.pool.Query(ctx, req, limite)
	if err != nil {
		return nil, fmt.Errorf("liste des salons : %w", err)
	}
	defer rows.Close()

	var (
		ids   []int64
		liste []*salons.Salon
	)
	for rows.Next() {
		id, sal, err := lireSalon(rows)
		if err != nil {
			return nil, err
		}
		ids = append(ids, id)
		liste = append(liste, sal)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	// Les lignes sont relachees AVANT la seconde requete : les garder ouvertes
	// tiendrait une connexion du pool pendant qu'on en demande une autre, ce qui
	// est le debut d'un epuisement de pool sous charge.
	rows.Close()

	places, err := occupantsDe(ctx, s.pool, ids)
	if err != nil {
		return nil, err
	}

	out := make([]salons.Salon, 0, len(liste))
	for i, sal := range liste {
		sal.Occupants = places[ids[i]]
		sal.Faucher(maintenant)
		if !sal.Ouvert() {
			continue
		}
		out = append(out, *sal)
	}
	return out, nil
}

// SalonParCode rend un salon, faucheur applique. C'est la lecture du classement
// live et celle qui accompagne un battement.
func (s *Store) SalonParCode(ctx context.Context, code string, maintenant time.Time) (*salons.Salon, error) {
	_, sal, err := salonParCode(ctx, s.pool, code, false)
	if err != nil {
		return nil, err
	}
	sal.Faucher(maintenant)
	return sal, nil
}

// RejoindreSalon assied un joueur et rend le salon tel qu'il est apres coup.
//
// Tout se passe dans UNE transaction avec la ligne du salon verrouillee : c'est
// ce qui fait que huit joueurs qui entrent en meme temps obtiennent huit places
// distinctes au lieu de se disputer la zero.
func (s *Store) RejoindreSalon(ctx context.Context, code string, playerID int64, pseudo string,
	maintenant time.Time) (*salons.Salon, int, error) {

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return nil, 0, fmt.Errorf("entree dans le salon : %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	id, sal, err := salonParCode(ctx, tx, code, true)
	if err != nil {
		return nil, 0, err
	}

	retires, _ := sal.Faucher(maintenant)
	if err := vider(ctx, tx, id, retires); err != nil {
		return nil, 0, err
	}

	place, refus := sal.Asseoir(maintenant, playerID, pseudo)
	if refus != nil {
		// Complet ou ferme. On VALIDE quand meme ce que le faucheur vient de
		// nettoyer : ce menage-la est juste, il ne depend pas de l'entree, et le
		// jeter ferait recommencer le meme travail au prochain appel — dans un
		// salon plein, c'est-a-dire a chaque essai de tout le monde.
		if err := ecrireSalon(ctx, tx, id, sal); err == nil {
			_ = tx.Commit(ctx)
		}
		return nil, 0, refus
	}
	if err := poserPlace(ctx, tx, id, sal, place); err != nil {
		return nil, 0, err
	}
	if err := ecrireSalon(ctx, tx, id, sal); err != nil {
		return nil, 0, err
	}
	if err := tx.Commit(ctx); err != nil {
		return nil, 0, fmt.Errorf("entree dans le salon : %w", err)
	}
	return sal, place, nil
}

// QuitterSalon leve un joueur. Partir sans etre assis n'est pas une erreur :
// voir `Lever` (internal/salons).
func (s *Store) QuitterSalon(ctx context.Context, code string, playerID int64, maintenant time.Time) error {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return fmt.Errorf("sortie du salon : %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	id, sal, err := salonParCode(ctx, tx, code, true)
	if err != nil {
		return err
	}

	retires, _ := sal.Faucher(maintenant)
	place, assis := sal.PlaceDe(playerID)
	if assis {
		retires = append(retires, place)
	}
	sal.Lever(maintenant, playerID)

	if err := vider(ctx, tx, id, retires); err != nil {
		return err
	}
	if err := ecrireSalon(ctx, tx, id, sal); err != nil {
		return err
	}
	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("sortie du salon : %w", err)
	}
	return nil
}

// SupprimerSalon ferme un salon. Le PROPRIETAIRE seul, et le proprietaire est
// celui que les regles designent MAINTENANT : le faucheur passe d'abord, donc
// un heritier peut fermer un salon que son createur a abandonne.
func (s *Store) SupprimerSalon(ctx context.Context, code string, playerID int64, maintenant time.Time) error {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return fmt.Errorf("fermeture du salon : %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	id, sal, err := salonParCode(ctx, tx, code, true)
	if err != nil {
		return err
	}
	sal.Faucher(maintenant)
	if sal.Proprietaire != playerID {
		return salons.ErrPasProprietaire
	}

	// La ligne part, et les places avec elle par cascade. On n'ecrit pas un etat
	// « fini » avant de supprimer : la fermeture demandee est explicite, elle
	// n'a personne a informer, et garder dix minutes un salon que son
	// proprietaire vient de fermer n'aiderait aucun spectateur.
	if _, err := tx.Exec(ctx, `DELETE FROM salons WHERE id = $1`, id); err != nil {
		return fmt.Errorf("fermeture du salon : %w", err)
	}
	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("fermeture du salon : %w", err)
	}
	return nil
}

// BattreSalon enregistre un battement et rend le salon tel qu'il en ressort.
func (s *Store) BattreSalon(ctx context.Context, code string, playerID int64,
	b salons.Battement, maintenant time.Time) (*salons.Salon, error) {

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return nil, fmt.Errorf("battement : %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	id, sal, err := salonParCode(ctx, tx, code, true)
	if err != nil {
		return nil, err
	}

	retires, _ := sal.Faucher(maintenant)
	if err := vider(ctx, tx, id, retires); err != nil {
		return nil, err
	}

	if err := sal.Battre(maintenant, playerID, b); err != nil {
		return nil, err
	}
	place, _ := sal.PlaceDe(playerID)
	o, err := occupantA(sal, place)
	if err != nil {
		return nil, err
	}

	const req = `
		UPDATE salon_places
		   SET camp = $3, points = $4, fusibles = $5, vivante = $6,
		       borne = $7, battu_a = $8
		 WHERE salon_id = $1 AND place = $2`
	if _, err := tx.Exec(ctx, req, id, int16(place), int16(o.Camp), o.Points,
		o.Fusibles, o.Vivante, o.Borne, o.BattuA); err != nil {
		return nil, fmt.Errorf("battement : %w", err)
	}
	if err := ecrireSalon(ctx, tx, id, sal); err != nil {
		return nil, err
	}
	if err := tx.Commit(ctx); err != nil {
		return nil, fmt.Errorf("battement : %w", err)
	}
	return sal, nil
}

// PurgerSalons est le menage periodique. Il ne decide RIEN : il execute en gros
// ce que `Faucher` decide en detail, et le service afficherait exactement la
// meme chose s'il ne tournait jamais — seule la table grossirait. C'est la
// propriete que `PurgePresence` a deja, et elle vaut qu'on la garde : elle
// interdit qu'une periodicite mal reglee change ce que les joueurs voient.
//
// Cette phrase a ete FAUSSE, et le dire ici sert de garde. La regle a change —
// une manche abandonnee garde son tableau — et ce menage-ci a continue
// d'effacer les lignes en SQL pendant que `Faucher` les gardait en Go. La page
// web affichait donc un classement final vide, et rien dans le code ne
// signalait le desaccord : le commentaire, lui, affirmait qu'il n'y en avait
// pas. Toute regle ajoutee a `Faucher` doit etre repercutee ci-dessous, ou la
// phrase d'ouverture redevient un mensonge.
func (s *Store) PurgerSalons(ctx context.Context, maintenant time.Time,
	ttl, retention time.Duration) (int64, int64, error) {

	// Les bornes sont CALCULEES EN GO et passees en parametre, plutot que
	// derivees du `now()` de la base. C'est la meme horloge que celle qui juge
	// la peremption a la lecture (voir `CreerSalon`) : deux horloges pour une
	// meme decision finiraient par ne pas dire la meme chose, et l'ecart ne se
	// verrait nulle part.
	limite := maintenant.Add(-ttl)

	// L'ORDRE DES QUATRE ETAPES EST LA REGLE ELLE-MEME. Il suit `Faucher`, pas
	// l'inverse, et la premiere doit passer AVANT la deuxieme : c'est elle qui
	// met la manche a l'etat fini pendant que ses lignes sont encore la.
	//
	// 1. Une MANCHE dont plus personne n'est frais se termine, ET GARDE SES
	//    LIGNES. Ce sont le classement final. Un client cesse de battre quand
	//    la manche s'arrete — il n'a plus rien a publier — donc les effacer
	//    donnait « MANCHE TERMINEE » au-dessus d'un tableau vide sur la page
	//    web, mesure ainsi avant correction.
	if _, err := s.pool.Exec(ctx, `
		UPDATE salons SET etat = 'fini', change_a = $1
		 WHERE etat = 'manche'
		   AND NOT EXISTS (SELECT 1 FROM salon_places p
		                    WHERE p.salon_id = salons.id AND p.battu_a >= $2)`,
		maintenant, limite); err != nil {
		return 0, 0, fmt.Errorf("cloture des manches abandonnees : %w", err)
	}

	// 2. Ailleurs — donc dans un salon qui vit encore — les places perimees
	//    tombent une a une, et leur numero redevient libre.
	tag, err := s.pool.Exec(ctx, `
		DELETE FROM salon_places p
		 USING salons s
		 WHERE p.salon_id = s.id AND s.etat <> 'fini' AND p.battu_a < $1`, limite)
	if err != nil {
		return 0, 0, fmt.Errorf("purge des places : %w", err)
	}
	places := tag.RowsAffected()

	// 3. Un salon vide est fini. En pratique c'est le salon en ATTENTE que tout
	//    le monde a quitte avant de commencer : celui-la n'a rien a montrer, et
	//    il se vide pour de bon. `change_a` prend la date du constat : c'est de
	//    la que court la retention.
	if _, err := s.pool.Exec(ctx, `
		UPDATE salons SET etat = 'fini', change_a = $1
		 WHERE etat <> 'fini'
		   AND NOT EXISTS (SELECT 1 FROM salon_places WHERE salon_id = salons.id)`,
		maintenant); err != nil {
		return places, 0, fmt.Errorf("fermeture des salons vides : %w", err)
	}

	tag, err = s.pool.Exec(ctx,
		`DELETE FROM salons WHERE etat = 'fini' AND change_a < $1`,
		maintenant.Add(-retention))
	if err != nil {
		return places, 0, fmt.Errorf("purge des salons : %w", err)
	}
	return places, tag.RowsAffected(), nil
}
