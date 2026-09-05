-- Nineteen V15 — le temps réel : présence dans la salle, et duels en différé.
--
-- Deux tables, et deux raisons distinctes de les séparer :
--
--   * `presence` est VOLATILE. Une ligne y vaut quelques secondes, elle est
--     réécrite plusieurs fois par seconde et par joueur, et rien n'est perdu
--     quand on la jette. Elle n'a donc ni clé étrangère obligatoire vers
--     `players` — on n'exige aucun compte pour se montrer dans la salle — ni
--     historique.
--
--   * `ghosts` est DURABLE et rattachée à une partie déjà validée. C'est ce qui
--     fait qu'un fantôme ne peut pas être fabriqué : on n'enregistre le journal
--     d'entrées que d'une partie que le serveur a lui-même ouverte, dont il a
--     tiré la graine, et dont il a RECALCULÉ le score depuis le journal
--     d'événements scellé. Le fantôme hérite de cette vérification ; il n'en
--     introduit aucune nouvelle.

BEGIN;

-- ---------------------------------------------------------------------------
-- Présence
-- ---------------------------------------------------------------------------
-- Qui est dans la salle, où, et devant quelle borne.
--
-- Aucun compte n'est exigé — c'est la règle du projet depuis la V1 — donc la
-- clé est un identifiant de CLIENT tiré au hasard par le jeu, pas un joueur.
-- Un client authentifié porte en plus son `player_id`, et c'est ce qui permet
-- de distinguer un pseudo VÉRIFIÉ d'un pseudo simplement déclaré. Le client
-- affiche la différence ; le serveur ne prétend pas savoir ce qu'il ne sait
-- pas.
CREATE TABLE IF NOT EXISTS presence (
    client_id  text        PRIMARY KEY,
    player_id  bigint      REFERENCES players(id) ON DELETE CASCADE,
    -- Le pseudo affiché. Pour un client authentifié c'est le nom du compte,
    -- réécrit par le serveur ; pour un anonyme c'est ce qu'il a déclaré, et
    -- `verified` vaut alors false.
    nickname   text        NOT NULL,
    verified   boolean     NOT NULL DEFAULT false,
    -- Position dans le repère de la salle, en mètres, et cap en radians.
    x          real        NOT NULL DEFAULT 0,
    y          real        NOT NULL DEFAULT 0,
    z          real        NOT NULL DEFAULT 0,
    yaw        real        NOT NULL DEFAULT 0,
    -- La borne devant laquelle il se tient, et le jeu qu'il y joue. Vide quand
    -- il marche dans l'allée.
    cabinet    text        NOT NULL DEFAULT '',
    game       text        NOT NULL DEFAULT '',
    score      integer     NOT NULL DEFAULT 0 CHECK (score >= 0),
    seen_at    timestamptz NOT NULL DEFAULT now(),

    CONSTRAINT presence_nickname_length CHECK (length(nickname) BETWEEN 1 AND 24),
    CONSTRAINT presence_client_length   CHECK (length(client_id) BETWEEN 8 AND 64),
    CONSTRAINT presence_cabinet_length  CHECK (length(cabinet) <= 32),
    CONSTRAINT presence_game_length     CHECK (length(game) <= 32)
);

-- La lecture est toujours « les vivants », donc toujours filtrée sur seen_at.
CREATE INDEX IF NOT EXISTS presence_seen_idx ON presence (seen_at DESC);

-- ---------------------------------------------------------------------------
-- Fantômes
-- ---------------------------------------------------------------------------
-- Le journal d'ENTRÉES d'une partie validée : la suite des appuis, un
-- changement par ligne, telle que `ns_runlog_write_inputs` l'écrit.
--
-- Ce n'est PAS le journal d'événements. Les deux ne répondent pas à la même
-- question : celui des événements enregistre des conséquences et sert à
-- AUTHENTIFIER un score — c'est lui que le serveur recalcule et que le sceau
-- protège ; celui-ci enregistre des appuis et sert à REJOUER. Il n'entre pas
-- dans la charge canonique et n'a rien à prouver.
--
-- Il est stocké en `text` parce que c'est du texte : quelques kilo-octets par
-- partie, lisibles à l'œil quand on débogue. Le compresser serait optimiser ce
-- qu'on n'a pas mesuré.
CREATE TABLE IF NOT EXISTS ghosts (
    run_id     uuid        PRIMARY KEY REFERENCES runs(id) ON DELETE CASCADE,
    player_id  bigint      NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    game_id    integer     NOT NULL REFERENCES games(id),
    -- Recopiés depuis la partie pour que le classement des fantômes se lise
    -- sans jointure, et surtout pour que la graine soit servie AVEC le journal :
    -- rejouer des entrées sur une autre graine ne rejoue rien.
    seed       bigint      NOT NULL,
    score      bigint      NOT NULL CHECK (score >= 0),
    inputs     text        NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),

    -- 256 Kio : un journal d'entrées de partie tient dans quelques kilo-octets,
    -- et cette borne est déjà cent fois trop large. Elle est ici EN PLUS de
    -- celle du décodeur HTTP, parce qu'une borne applicative peut changer et
    -- que la base doit rester saine quoi qu'on lui envoie.
    CONSTRAINT ghosts_inputs_size CHECK (length(inputs) <= 262144)
);

-- On sert « les meilleurs fantômes de ce jeu » : c'est l'index qu'il faut.
CREATE INDEX IF NOT EXISTS ghosts_ranking_idx ON ghosts (game_id, score DESC);

COMMIT;
