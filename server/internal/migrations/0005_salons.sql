-- 0005 — les salons du Couperet : le rendez-vous qui manquait au mode en ligne.
--
-- CE QUE CES DEUX TABLES RÉPARENT
-- -------------------------------
-- Le mode compétitif se joue en ligne depuis qu'il existe, et le seul moyen d'y
-- retrouver quelqu'un était de convenir HORS BANDE d'un numéro de salon et d'un
-- numéro de place, puis de les taper en ligne de commande
-- (« --couperet-en-ligne=hote:port,salon,place,places », room/main.c). Deux
-- joueurs qui choisissent la même place ne se voient jamais ; celui qui se
-- trompe sur le nombre de places est refusé par le relais sans savoir pourquoi.
-- Ce n'était pas un mode difficile à lancer, c'était un mode qu'on ne pouvait
-- pas lancer sans se parler ailleurs d'abord.
--
-- POURQUOI EN BASE, ET NON EN MÉMOIRE DU SERVEUR
-- ----------------------------------------------
-- Un salon vit plusieurs minutes — une manche pleine de huit places en dure 315
-- (room/room_couperet.h) — et un redéploiement du serveur dure quelques
-- secondes. Garder les salons en mémoire ferait donc perdre les manches en
-- cours à chaque mise à jour, sans qu'aucun joueur comprenne ce qui vient de se
-- passer. La présence, elle, est volatile et le reste (voir 0002) : elle ne vaut
-- que douze secondes et se reconstruit toute seule au battement suivant.
--
-- POURQUOI DEUX TABLES ET NON UNE COLONNE DE TABLEAU
-- --------------------------------------------------
-- Une place est écrite plusieurs fois par seconde par SON joueur et par lui
-- seul. Une ligne par place laisse PostgreSQL sérialiser ces écritures place par
-- place ; un tableau dans la ligne du salon les ferait toutes se disputer la
-- même ligne, huit fois par battement.
--
-- TOUTES LES DATES SONT ÉCRITES PAR L'APPLICATION
-- ----------------------------------------------
-- Les `DEFAULT now()` ci-dessous ne servent qu'à une insertion faite à la main,
-- en console : le serveur, lui, écrit chaque horodatage explicitement, depuis
-- l'horloge de Go. C'est délibéré. Les règles de péremption s'évaluent en Go
-- (`internal/salons`), et si la création datait de l'horloge de la base pendant
-- que le battement datait de celle du serveur, un décalage de quelques secondes
-- entre les deux machines suffirait à faucher des places vivantes — sans que
-- rien ne le signale. Une seule horloge décide, et c'est celle qui compare.
--
-- LE CODE D'ACCÈS N'EST PAS LA CAPACITÉ
-- -------------------------------------
-- `code` fait six caractères parce qu'il se tape à la main et se lit à voix
-- haute. `relais` est l'identifiant de session sur le relais de duel, tiré avec
-- `crypto/rand`, et c'est LUI qui donne réellement accès à la manche — le relais
-- n'a aucune notion de compte et apparie sur ce seul nombre. Les deux sont donc
-- séparés exprès, et `relais` ne sort jamais vers qui n'est pas assis. Le
-- raisonnement complet est en tête de internal/salons/salons.go, qui est
-- l'endroit où on le lira en modifiant la règle.

BEGIN;

-- ---------------------------------------------------------------------------
-- Salons
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS salons (
    id     bigserial PRIMARY KEY,

    -- Le code tapé par le joueur. Toujours en capitales, dans un alphabet sans
    -- ambiguïté visuelle : ni O ni 0, ni I ni 1. La contrainte ci-dessous est
    -- la forme exacte de `salons.Alphabet` (internal/salons) ; un test compare
    -- les deux et échoue si l'un bouge sans l'autre.
    code   text      NOT NULL UNIQUE,
    nom    text      NOT NULL,

    -- Les bornes sont celles du relais (`placesMin`/`placesMax`,
    -- internal/duel/relay.go) et celles du mode (`ROOM_CP_MAX_PLACES`). Elles
    -- sont redites ici parce qu'une contrainte applicative peut changer et que
    -- la base doit rester saine quoi qu'on lui envoie — c'est la même raison
    -- qui fait borner `ghosts.inputs` des deux côtés (voir 0002).
    places smallint  NOT NULL CHECK (places BETWEEN 2 AND 8),
    -- Un camp = chacun pour soi (le camp vaut alors la place, comme
    -- `room_cp_place.camp` le documente). Deux = les équipes.
    camps  smallint  NOT NULL CHECK (camps BETWEEN 1 AND 2),

    -- Un salon privé ne paraît dans AUCUNE liste. Il répond quand même au
    -- classement live : connaître le code suffit pour regarder, ce qui est tout
    -- l'intérêt d'un salon entre amis.
    prive  boolean   NOT NULL DEFAULT false,

    etat   text      NOT NULL DEFAULT 'attente'
           CHECK (etat IN ('attente', 'manche', 'fini')),

    -- L'identifiant de SESSION sur le relais. Voir l'en-tête : ce n'est pas le
    -- code, et ce n'est pas dérivé du code.
    --
    -- `bigint` est SIGNÉ, et c'est pour cela que le tirage efface son bit de
    -- poids fort — comme le fait déjà la graine de partie, et pour la même
    -- raison côté client, dont l'analyseur JSON lit les entiers avec
    -- `SDL_strtoll`.
    relais bigint    NOT NULL,

    -- Le propriétaire CHANGE : quand celui qui a créé le salon s'en va et qu'il
    -- reste du monde, la main passe au plus ancien occupant restant. La colonne
    -- est donc une référence courante, pas une trace de création — celle-là est
    -- `cree_a`.
    proprietaire bigint NOT NULL REFERENCES players(id) ON DELETE CASCADE,

    cree_a   timestamptz NOT NULL DEFAULT now(),
    -- La date de la dernière TRANSITION D'ÉTAT, et non celle de la création.
    -- C'est ce qui permet de dire « en attente depuis 40 s » à qui choisit un
    -- salon et « manche en cours depuis 2 min » à qui la regarde, avec un seul
    -- champ et sans que l'un des deux mente.
    change_a timestamptz NOT NULL DEFAULT now(),

    CONSTRAINT salons_code_forme    CHECK (code ~ '^[A-HJ-NP-Z2-9]{6}$'),
    CONSTRAINT salons_nom_longueur  CHECK (length(nom) BETWEEN 1 AND 32)
);

-- La liste publique lit exactement ceci : les salons non privés qui attendent,
-- les plus récents d'abord. L'index partiel ne porte donc que ce qu'elle sert.
CREATE INDEX IF NOT EXISTS salons_liste_idx
    ON salons (cree_a DESC) WHERE NOT prive AND etat = 'attente';

-- Le plafond « combien de salons un même joueur peut-il tenir ouverts » se
-- compte sur cet index. Sans lui, une boucle de création ferait un balayage
-- complet à chaque appel — et c'est précisément l'appel qu'on cherche à borner.
CREATE INDEX IF NOT EXISTS salons_proprietaire_idx
    ON salons (proprietaire) WHERE etat <> 'fini';

-- Le ménage périodique cherche les salons finis depuis assez longtemps.
CREATE INDEX IF NOT EXISTS salons_fin_idx ON salons (change_a) WHERE etat = 'fini';

-- ---------------------------------------------------------------------------
-- Places
-- ---------------------------------------------------------------------------
-- Une ligne par joueur assis. Elle porte À LA FOIS le signe de vie qui garde la
-- place (`battu_a`) et la ligne de classement que la page live affiche : c'est
-- le même battement qui écrit les deux, en un seul aller-retour, pour la raison
-- qui vaut déjà pour la présence — c'est le seul échange périodique du mode.
CREATE TABLE IF NOT EXISTS salon_places (
    salon_id  bigint   NOT NULL REFERENCES salons(id) ON DELETE CASCADE,

    -- 0 à 7, et jamais au-delà de `salons.places` : le client indexe des
    -- tableaux de ROOM_CP_MAX_PLACES entrées et le relais refuse toute place
    -- hors du salon. La borne large est ici, la borne exacte est applicative
    -- (`PlaceLibre`, internal/salons) parce qu'elle dépend d'une autre ligne.
    place     smallint NOT NULL CHECK (place BETWEEN 0 AND 7),

    player_id bigint   NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    pseudo    text     NOT NULL,

    -- 0..7 : en individuel le camp EST la place, comme le mode le définit.
    camp      smallint NOT NULL DEFAULT 0 CHECK (camp BETWEEN 0 AND 7),

    -- `integer` et non `bigint` : ce sont les types du mode
    -- (`room_cp_place.points` et `.fusibles` sont des `int32_t`). Une colonne
    -- plus large laisserait entrer des valeurs que le client ne saurait pas
    -- relire.
    points    integer  NOT NULL DEFAULT 0,
    fusibles  integer  NOT NULL DEFAULT 0 CHECK (fusibles >= 0),
    -- Faux = spectre : ne joue plus, agit encore. Ses points restent acquis.
    vivante   boolean  NOT NULL DEFAULT true,
    borne     text     NOT NULL DEFAULT '',

    -- L'ordre d'arrivée, qui décide de l'héritage du salon. Il ne se recycle
    -- pas, contrairement au numéro de place — voir `Lever` (internal/salons).
    entre_a   timestamptz NOT NULL DEFAULT now(),
    battu_a   timestamptz NOT NULL DEFAULT now(),

    PRIMARY KEY (salon_id, place),

    -- UN SEUL SIÈGE PAR JOUEUR ET PAR SALON, garanti par la base et pas
    -- seulement par le code. L'entrée est idempotente côté application ; cette
    -- contrainte est ce qui tient encore si deux requêtes se croisent malgré le
    -- verrou, et elle transforme une course en erreur bruyante plutôt qu'en
    -- salon peuplé de doublons portant le même nom.
    CONSTRAINT salon_places_un_siege UNIQUE (salon_id, player_id),

    CONSTRAINT salon_places_pseudo_longueur CHECK (length(pseudo) BETWEEN 1 AND 24),
    CONSTRAINT salon_places_borne_longueur  CHECK (length(borne) <= 24)
);

-- Le faucheur cherche les places qui ne battent plus, toutes salles confondues.
CREATE INDEX IF NOT EXISTS salon_places_battu_idx ON salon_places (battu_a);

COMMIT;
