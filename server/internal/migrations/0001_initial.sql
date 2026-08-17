-- Nineteen V15 — schéma initial.
--
-- Reprise du schéma d'origine (Rapport/Annexes/creation base de donnees.sql),
-- avec les corrections qu'appelait l'audit :
--
--   * encodage : latin1 -> UTF-8 (le jeu affiche des pseudos accentués) ;
--   * clés étrangères réellement déclarées, avec ON DELETE — l'original écrivait
--     `REFERENCES (nineteen_players.userId)` dans un commentaire de colonne, ce
--     que MySQL ignorait silencieusement, laissant des scores orphelins ;
--   * unicité du nom de compte, absente à l'origine ;
--   * horodatages de création et de dernière activité ;
--   * table des parties, qui n'existait pas : c'est elle qui permet au serveur
--     de recalculer un score au lieu de croire celui du client ;
--   * index sur les colonnes réellement interrogées.

BEGIN;

CREATE EXTENSION IF NOT EXISTS citext;      -- comparaison de pseudo insensible à la casse
CREATE EXTENSION IF NOT EXISTS pgcrypto;    -- gen_random_uuid()

-- ---------------------------------------------------------------------------
-- Joueurs
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS players (
    id            bigserial PRIMARY KEY,
    username      citext      NOT NULL,
    email         citext,
    -- Argon2id encodé, sel et paramètres compris. 255 caractères couvrent
    -- largement le format, y compris après un durcissement des paramètres.
    password_hash text        NOT NULL,
    created_at    timestamptz NOT NULL DEFAULT now(),
    last_seen_at  timestamptz NOT NULL DEFAULT now(),
    disabled      boolean     NOT NULL DEFAULT false,

    CONSTRAINT players_username_unique UNIQUE (username),
    CONSTRAINT players_username_length CHECK (length(username::text) BETWEEN 3 AND 24),
    -- Validation minimale : la vérification sérieuse est applicative. On veut
    -- surtout empêcher qu'une chaîne manifestement absurde entre en base.
    CONSTRAINT players_email_shape CHECK (email IS NULL OR email ~ '^[^@[:space:]]+@[^@[:space:]]+\.[^@[:space:]]+$')
);

CREATE UNIQUE INDEX IF NOT EXISTS players_email_unique ON players (email) WHERE email IS NOT NULL;

-- ---------------------------------------------------------------------------
-- Sessions
-- ---------------------------------------------------------------------------
-- On stocke l'EMPREINTE de la clé, jamais la clé. Une fuite de cette table ne
-- permet donc pas d'usurper une session, contrairement au schéma d'origine qui
-- conservait `sessionKey` en clair.
CREATE TABLE IF NOT EXISTS sessions (
    key_hash     text        PRIMARY KEY,
    player_id    bigint      NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    ip           inet,
    user_agent   text,
    created_at   timestamptz NOT NULL DEFAULT now(),
    last_used_at timestamptz NOT NULL DEFAULT now(),
    expires_at   timestamptz NOT NULL,
    revoked_at   timestamptz
);

CREATE INDEX IF NOT EXISTS sessions_player_idx  ON sessions (player_id);
CREATE INDEX IF NOT EXISTS sessions_expiry_idx  ON sessions (expires_at) WHERE revoked_at IS NULL;

-- ---------------------------------------------------------------------------
-- Jeux
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS games (
    id         integer PRIMARY KEY,
    slug       text    NOT NULL UNIQUE,
    name       text    NOT NULL,
    difficulty text    NOT NULL DEFAULT 'normal'
               CHECK (difficulty IN ('easy', 'normal', 'hard')),
    -- Le multiplicateur pondère les jeux entre eux dans le classement général :
    -- un point de Démineur ne vaut pas un point d'Asteroid.
    multiplier double precision NOT NULL DEFAULT 1.0 CHECK (multiplier > 0),
    -- Plafond de plausibilité : au-delà, un score est rejeté sans même
    -- examiner le journal. Défense en profondeur, pas défense principale.
    max_plausible_score bigint NOT NULL DEFAULT 1000000 CHECK (max_plausible_score > 0)
);

-- Les identifiants reprennent l'ordre des bornes de la V1 (legacy/room/room.c).
INSERT INTO games (id, slug, name, difficulty, multiplier, max_plausible_score) VALUES
    ( 1, 'flappy-hard',   'Flappy Bird', 'hard',   2.0,    5000),
    ( 2, 'tetris-hard',   'Tetris',      'hard',   2.0,  999999),
    ( 3, 'asteroid-hard', 'Asteroid',    'hard',   2.0,  500000),
    ( 4, 'shooter-hard',  'Shooter',     'hard',   2.0,  500000),
    ( 5, 'snake-hard',    'Snake',       'hard',   2.0,   20000),
    ( 6, 'demineur-hard', 'Démineur',    'hard',   2.5,   10000),
    ( 7, 'demineur-easy', 'Démineur',    'easy',   1.0,   10000),
    ( 8, 'snake-easy',    'Snake',       'easy',   1.0,   20000),
    ( 9, 'shooter-easy',  'Shooter',     'easy',   1.0,  500000),
    (10, 'asteroid-easy', 'Asteroid',    'easy',   1.0,  500000),
    (11, 'tetris-easy',   'Tetris',      'easy',   1.0,  999999),
    (12, 'flappy-easy',   'Flappy Bird', 'easy',   1.0,    5000),
    -- Les deux emplacements que la V1 affichait en « COMMING SOON ».
    (13, 'pacman',        'Pac-Man',     'normal', 1.5,  200000),
    (14, 'piano',         'Piano',       'normal', 1.0,   50000)
ON CONFLICT (id) DO NOTHING;

-- ---------------------------------------------------------------------------
-- Parties
-- ---------------------------------------------------------------------------
-- Le cœur du modèle anti-triche. Une partie est ouverte AVANT d'être jouée ; le
-- serveur tire la graine et le secret HMAC. À la soumission, il vérifie le sceau
-- et recalcule le score depuis les événements. Une partie ne peut être soumise
-- qu'une fois : `submitted_at IS NULL` dans la clause de mise à jour interdit le
-- rejeu, que l'audit relevait comme trivial sur la V1.
CREATE TABLE IF NOT EXISTS runs (
    id           uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    player_id    bigint      NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    game_id      integer     NOT NULL REFERENCES games(id),
    secret       bytea       NOT NULL,
    seed         bigint      NOT NULL,
    started_at   timestamptz NOT NULL DEFAULT now(),
    submitted_at timestamptz,
    score        bigint,
    verdict      text
);

CREATE INDEX IF NOT EXISTS runs_player_idx ON runs (player_id, started_at DESC);
-- Purge : une partie jamais soumise n'a pas à rester indéfiniment.
CREATE INDEX IF NOT EXISTS runs_open_idx ON runs (started_at) WHERE submitted_at IS NULL;

-- ---------------------------------------------------------------------------
-- Scores
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS scores (
    player_id   bigint      NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    game_id     integer     NOT NULL REFERENCES games(id),
    score       bigint      NOT NULL DEFAULT 0 CHECK (score >= 0),
    run_id      uuid        REFERENCES runs(id) ON DELETE SET NULL,
    achieved_at timestamptz NOT NULL DEFAULT now(),

    PRIMARY KEY (player_id, game_id)
);

-- Index de classement : la requête trie par score décroissant par jeu.
CREATE INDEX IF NOT EXISTS scores_ranking_idx ON scores (game_id, score DESC);

-- ---------------------------------------------------------------------------
-- Limitation de débit
-- ---------------------------------------------------------------------------
-- Absente de la V1, qui n'opposait donc rien à une attaque par dictionnaire sur
-- la page de connexion. En base plutôt qu'en mémoire pour que la limite tienne
-- avec plusieurs instances du serveur.
CREATE TABLE IF NOT EXISTS rate_limits (
    key          text        PRIMARY KEY,
    window_start timestamptz NOT NULL DEFAULT now(),
    count        integer     NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS rate_limits_window_idx ON rate_limits (window_start);

-- ---------------------------------------------------------------------------
-- Journal d'audit
-- ---------------------------------------------------------------------------
-- Trace les actions sensibles. La V1 n'avait aucune trace : impossible de savoir
-- après coup qu'un classement avait été réécrit, ni par qui.
CREATE TABLE IF NOT EXISTS audit_log (
    id         bigserial   PRIMARY KEY,
    at         timestamptz NOT NULL DEFAULT now(),
    player_id  bigint      REFERENCES players(id) ON DELETE SET NULL,
    action     text        NOT NULL,
    ip         inet,
    detail     jsonb
);

CREATE INDEX IF NOT EXISTS audit_log_at_idx     ON audit_log (at DESC);
CREATE INDEX IF NOT EXISTS audit_log_action_idx ON audit_log (action, at DESC);

COMMIT;
