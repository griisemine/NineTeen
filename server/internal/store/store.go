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
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"
)

var (
	ErrNotFound      = errors.New("introuvable")
	ErrAlreadyExists = errors.New("existe déjà")
)

type Store struct {
	pool *pgxpool.Pool
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
	return &Store{pool: pool}, nil
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
}

func (s *Store) Games(ctx context.Context) ([]Game, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT id, slug, name, difficulty, multiplier FROM games ORDER BY id`)
	if err != nil {
		return nil, fmt.Errorf("liste des jeux : %w", err)
	}
	defer rows.Close()

	var out []Game
	for rows.Next() {
		var g Game
		if err := rows.Scan(&g.ID, &g.Slug, &g.Name, &g.Difficulty, &g.Multiplier); err != nil {
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

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n]
}
