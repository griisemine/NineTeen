/*
 * main.c — point d'entrée de Nineteen.
 *
 * Ce que faisait l'original (legacy/main.c, 1332 lignes) : ouvrir une fenêtre
 * SDL, vérifier l'existence de 54 fichiers listés en dur, afficher un menu de
 * connexion, appeler room(), et gérer au passage le réseau et les mises à jour.
 *
 * Ici, le point d'entrée ne fait que trois choses : monter les répertoires de
 * données, construire les sous-systèmes, et faire tourner la boucle. Le reste
 * vit dans le moteur.
 *
 * Un mode « capture » permet de rendre un nombre d'images donné et d'écrire un
 * PNG sans écran, ce qui rend le rendu vérifiable en intégration continue :
 *
 *   nineteen --headless --screenshot=salle.png --frames=8 --camera=orbit
 */
#include "ns_core.h"
#include "ns_config.h"
#include "ns_render.h"
#include "ns_rhi.h"
#include "ns_scene.h"
#include "ns_sprite.h"

#include "games.h"
#include "ns_online.h"
#include "ns_realtime.h"
#include "ns_runlog.h"
#include "ns_scores.h"

#include "room_camera.h"
#include "room_hud.h"
#include "room_menu.h"
#include "room_sound.h"
#include "room_viewmodel.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct options {
    bool        headless;
    const char *screenshot;
    int         frames;
    int         width, height;
    bool        fullscreen;
    bool        vsync;
    bool        debug_gpu;
    ns_quality  quality;
    room_camera_mode camera_mode;
    float       camera_angle;
    float       render_scale;
    const char *debug_view;
    const char *position;   /* "x,y,z" */
    float       yaw, pitch;
    bool        has_view;
    float       exposure;
    float       particles;  /* densité de poussière, < 0 = celle du palier */
    bool        offline;    /* verrou : interdit toute sortie réseau */
    /*
     * Le temps réel — présence et duels. Trois états et non deux : la ligne de
     * commande peut l'ACTIVER, le DÉSACTIVER, ou ne rien dire — auquel cas
     * c'est le réglage persistant qui tranche. Un simple booléen ferait de
     * l'absence d'option un « non » explicite, et `--no-temps-reel` ne pourrait
     * plus rien vouloir dire de différent.
     */
    int         realtime;   /* -1 : la config décide ; 0 : non ; 1 : oui */
    bool        quality_set; /* la ligne de commande a tranché : ne pas relire la config */
    bool        no_hud;      /* captures d'architecture : la scène sans un pixel de texte */
    const char *server;      /* URL du classement en ligne, sinon la config */
    const char *input_log;   /* où déposer le journal d'entrées de la partie */
    const char *replay;      /* un journal d'entrées à rejouer, sans fenêtre */
    bool        menu;        /* ouvre le menu au démarrage — pour le photographier */
    int         menu_row;    /* et s'y placer sur une ligne précise */
    const char *player;     /* nom porté au classement local */
    const char *room;       /* "generated" | "legacy" */
    const char *viewpoint;  /* point de vue nommé, déclaré par la scène */
    bool        bench;      /* mesure le temps GPU réel, image par image */
    const char *pose;       /* pose de bras figée, pour les captures */
    const char *game;       /* démarrer directement dans un mini-jeu */
    bool        autoplay;   /* le jeu se joue tout seul : captures et CI */
    float       warmup;     /* secondes de simulation avancées avant la 1re image */
    const char *play_at;    /* nom d'une borne : s'y placer et lancer sa partie */
} options;

static void print_usage(const char *exe)
{
    printf(
        "Nineteen %s — salle d'arcade\n"
        "usage : %s [options]\n\n"
        "  --headless           pas de fenêtre visible (rendu hors écran)\n"
        "  --screenshot=CHEMIN  écrit une capture PNG puis quitte\n"
        "  --frames=N           nombre d'images à rendre avant la capture (défaut 4)\n"
        "  --width=N --height=N résolution (défaut 1600x900)\n"
        "  --scale=F            échelle de rendu interne, 0.4 à 2.0 — sans elle,\n"
        "                       c'est le PALIER qui la fixe (0.50 à medium)\n"
        "  --fullscreen         plein écran\n"
        "  --no-vsync           désactive la synchronisation verticale\n"
        "  --quality=Q          potato | low | medium | high | ultra (défaut medium)\n"
        "                       high et ultra activent le lancer de rayons : superbe\n"
        "                       en capture, coûteux en temps réel\n"
        "  --room=R             generated | legacy (défaut generated si présente)\n"
        "  --view=NOM           point de vue nommé déclaré par la scène : c'est ce\n"
        "                       qui permet de comparer deux salles d'échelles\n"
        "                       différentes au même cadrage\n"
        "  --camera=M           player | free | orbit (défaut player)\n"
        "  --angle=F            angle de départ de la caméra orbite, en degrés\n"
        "  --pos=X,Y,Z          place la caméra à un point précis (mode libre)\n"
        "  --yaw=D --pitch=D    orientation en degrés\n"
        "  --exposure=F         exposition du tone mapping (défaut 1.15)\n"
        "  --particles=F        densité de poussière, 0 à 1 (défaut : le palier)\n"
        "  --rejouer=F          rejoue le journal d'entrées F et imprime le score,\n"
        "                       sans fenêtre ni GPU : c'est ce qui rend un rapport\n"
        "                       de bug reproductible\n"
        "  --journal-entrees=F  écrit les commandes de la partie dans F : une partie\n"
        "                       redevient reproductible, et un rapport de bug devient\n"
        "                       un fichier plutôt qu'un souvenir\n"
        "  --server=URL         classement en ligne (http://hôte:port) ; sinon la config\n"
        "  --offline            verrou : aucune connexion, aucune mise en file\n"
        "  --temps-reel         présence dans la salle et duels (INERTE par défaut) ;\n"
        "                       demande un serveur, et se règle aussi dans Échap\n"
        "  --no-temps-reel      force l'inverse, quel que soit le réglage gardé\n"
        "  --menu[=N]           ouvre le menu de réglages (ligne N) : pour les captures\n"
        "  --no-hud             pas d'affichage : la scène seule, pour les captures\n"
        "\n"
        "  En jeu : Échap réglages, F5 caméra libre, F6 orbite,\n"
        "           F7 palier de qualité, F8 échelle de rendu, F2 capture.\n"
        "  --nom=NOM            nom porté au classement local\n"
        "  --debug=VUE          affiche une cible intermédiaire : albedo, normal,\n"
        "                       emissive, depth, visibility, hdr, bloom\n"
        "  --bench              mesure le temps GPU réel de chaque image\n"
        "  --game=NOM           démarre directement dans un mini-jeu\n"
        "  --autoplay           le mini-jeu se joue tout seul (captures, CI)\n"
        "  --warmup=S           avance le mini-jeu de S secondes avant de rendre\n"
        "  --play-at=BORNE      se place devant la borne nommée et lance sa partie,\n"
        "                       en restant EN 3D : le jeu tourne dans sa dalle\n"
        "  --pose=NOM           fige les bras : idle, walk, reach, insert, press\n"
        "                       (impose le mode joueur : pas de bras en caméra libre)\n"
        "  --debug-gpu          active les couches de validation du pilote\n"
        "  --help               affiche ce message\n",
        NINETEEN_VERSION, exe);
}

/* ==========================================================================
 * Réglages par machine : fichier .env, puis variables d'environnement
 * ========================================================================== */

/*
 * Trois niveaux, du plus faible au plus fort : le fichier `.env`, les variables
 * d'environnement, puis la ligne de commande. C'est l'ordre habituel, et le seul
 * qui permette d'essayer un réglage sans éditer un fichier.
 *
 * L'implémentation est volontairement minuscule : `CLÉ=VALEUR` par ligne, `#`
 * pour commenter, pas de guillemets, pas d'expansion. Un format plus riche
 * demanderait un analyseur, donc des messages d'erreur, donc un test — pour
 * régler une qualité et une résolution.
 */
static void apply_env_file(const char *path)
{
    if (!path || !path[0]) return;
    SDL_IOStream *io = SDL_IOFromFile(path, "r");
    if (!io) return;

    size_t size = 0;
    char *text = (char *)SDL_LoadFile_IO(io, &size, true);
    if (!text) return;

    char *line = text;
    while (line && *line) {
        char *end = SDL_strchr(line, '\n');
        if (end) *end = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p && *p != '#') {
            char *eq = SDL_strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char *key = p, *value = eq + 1;
                /* Rogner la fin de la clé et le \r d'un fichier écrit sous Windows. */
                size_t kl = SDL_strlen(key);
                while (kl > 0 && (key[kl - 1] == ' ' || key[kl - 1] == '\t')) key[--kl] = '\0';
                size_t vl = SDL_strlen(value);
                while (vl > 0 && (value[vl - 1] == ' ' || value[vl - 1] == '\t'
                               || value[vl - 1] == '\r')) value[--vl] = '\0';

                /* `false` : une variable déjà présente dans l'environnement
                 * l'emporte sur le fichier, comme annoncé. */
                if (kl > 0) SDL_SetEnvironmentVariable(SDL_GetEnvironment(), key, value, false);
            }
        }

        line = end ? end + 1 : NULL;
    }
    SDL_free(text);
}

static void load_env_defaults(options *o)
{
    const char *forced = SDL_getenv("NINETEEN_ENV");
    if (forced && forced[0]) {
        apply_env_file(forced);
    } else {
        apply_env_file(".env");
        char beside[1024];
        const char *base = SDL_GetBasePath();
        if (base) {
            SDL_snprintf(beside, sizeof beside, "%s.env", base);
            apply_env_file(beside);
        }
    }

    const char *v;
    if ((v = SDL_getenv("NINETEEN_QUALITY")) != NULL) {
        if (SDL_strcmp(v, "potato") == 0)      o->quality = NS_QUALITY_POTATO;
        else if (SDL_strcmp(v, "low") == 0)    o->quality = NS_QUALITY_LOW;
        else if (SDL_strcmp(v, "medium") == 0) o->quality = NS_QUALITY_MEDIUM;
        else if (SDL_strcmp(v, "high") == 0)   o->quality = NS_QUALITY_HIGH;
        else if (SDL_strcmp(v, "ultra") == 0)  o->quality = NS_QUALITY_ULTRA;
        else NS_WARN("NINETEEN_QUALITY=%s inconnu — ignoré", v);
        /* L'environnement compte comme un choix explicite : sans ça il serait
         * écrasé par le palier gardé de la session précédente, et un `.env`
         * cesserait de faire ce qu'il dit. */
        if (SDL_strcmp(v, "potato") == 0 || SDL_strcmp(v, "low") == 0
            || SDL_strcmp(v, "medium") == 0 || SDL_strcmp(v, "high") == 0
            || SDL_strcmp(v, "ultra") == 0) o->quality_set = true;
    }
    if ((v = SDL_getenv("NINETEEN_WIDTH")) != NULL)     o->width = SDL_atoi(v);
    if ((v = SDL_getenv("NINETEEN_HEIGHT")) != NULL)    o->height = SDL_atoi(v);
    if ((v = SDL_getenv("NINETEEN_SCALE")) != NULL)     o->render_scale = (float)SDL_atof(v);
    if ((v = SDL_getenv("NINETEEN_EXPOSURE")) != NULL)  o->exposure = (float)SDL_atof(v);
    if ((v = SDL_getenv("NINETEEN_VSYNC")) != NULL)     o->vsync = (SDL_atoi(v) != 0);
    if ((v = SDL_getenv("NINETEEN_FULLSCREEN")) != NULL) o->fullscreen = (SDL_atoi(v) != 0);
    if ((v = SDL_getenv("NINETEEN_ROOM")) != NULL)      o->room = v;
    if ((v = SDL_getenv("NINETEEN_PARTICLES")) != NULL)
        o->particles = ns_clampf((float)SDL_atof(v), 0.0f, 1.0f);
    if ((v = SDL_getenv("NINETEEN_OFFLINE")) != NULL)   o->offline = (SDL_atoi(v) != 0);
    if ((v = SDL_getenv("NINETEEN_NOM")) != NULL)       o->player = v;
}

static bool parse_options(int argc, char **argv, options *o)
{
    SDL_zerop(o);
    o->frames = 4;
    o->width = 1600;
    o->height = 900;
    o->vsync = true;
    o->quality = NS_QUALITY_MEDIUM;
    o->camera_mode = ROOM_CAM_PLAYER;
    o->render_scale = 0.0f;   /* 0 = non demandé : le palier ou la config décide */
    o->particles = -1.0f;       /* < 0 : on garde la densité du palier de qualité */
    o->realtime = -1;           /* < 0 : non demandé, c'est le réglage gardé qui tranche */

    /* Avant la ligne de commande : elle doit pouvoir tout écraser. */
    load_env_defaults(o);

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (SDL_strcmp(a, "--help") == 0 || SDL_strcmp(a, "-h") == 0) {
            print_usage(argv[0]);
            return false;
        } else if (SDL_strcmp(a, "--headless") == 0) {
            o->headless = true;
        } else if (SDL_strncmp(a, "--screenshot=", 13) == 0) {
            o->screenshot = a + 13;
        } else if (SDL_strncmp(a, "--frames=", 9) == 0) {
            o->frames = SDL_atoi(a + 9);
        } else if (SDL_strncmp(a, "--width=", 8) == 0) {
            o->width = SDL_atoi(a + 8);
        } else if (SDL_strncmp(a, "--height=", 9) == 0) {
            o->height = SDL_atoi(a + 9);
        } else if (SDL_strncmp(a, "--scale=", 8) == 0) {
            o->render_scale = (float)SDL_atof(a + 8);
        } else if (SDL_strcmp(a, "--fullscreen") == 0) {
            o->fullscreen = true;
        } else if (SDL_strcmp(a, "--no-vsync") == 0) {
            o->vsync = false;
        } else if (SDL_strcmp(a, "--bench") == 0) {
            o->bench = true;
        } else if (SDL_strncmp(a, "--play-at=", 10) == 0) {
            o->play_at = a + 10;
        } else if (SDL_strncmp(a, "--warmup=", 9) == 0) {
            o->warmup = (float)SDL_atof(a + 9);
        } else if (SDL_strcmp(a, "--autoplay") == 0) {
            o->autoplay = true;
        } else if (SDL_strncmp(a, "--game=", 7) == 0) {
            o->game = a + 7;
        } else if (SDL_strncmp(a, "--pose=", 7) == 0) {
            o->pose = a + 7;
        } else if (SDL_strcmp(a, "--debug-gpu") == 0) {
            o->debug_gpu = true;
        } else if (SDL_strncmp(a, "--debug=", 8) == 0) {
            o->debug_view = a + 8;
        } else if (SDL_strncmp(a, "--room=", 7) == 0) {
            o->room = a + 7;
            if (SDL_strcmp(o->room, "generated") != 0 && SDL_strcmp(o->room, "legacy") != 0) {
                fprintf(stderr, "salle inconnue : %s (attendu generated ou legacy)\n", o->room);
                return false;
            }
        } else if (SDL_strncmp(a, "--view=", 7) == 0) {
            o->viewpoint = a + 7;
        } else if (SDL_strncmp(a, "--pos=", 6) == 0) {
            o->position = a + 6; o->has_view = true;
        } else if (SDL_strncmp(a, "--yaw=", 6) == 0) {
            o->yaw = (float)SDL_atof(a + 6) * NS_DEG2RAD; o->has_view = true;
        } else if (SDL_strncmp(a, "--pitch=", 8) == 0) {
            o->pitch = (float)SDL_atof(a + 8) * NS_DEG2RAD; o->has_view = true;
        } else if (SDL_strncmp(a, "--exposure=", 11) == 0) {
            o->exposure = (float)SDL_atof(a + 11);
        } else if (SDL_strncmp(a, "--particles=", 12) == 0) {
            o->particles = ns_clampf((float)SDL_atof(a + 12), 0.0f, 1.0f);
        } else if (SDL_strncmp(a, "--journal-entrees=", 18) == 0) {
            o->input_log = a + 18;
        } else if (SDL_strncmp(a, "--rejouer=", 10) == 0) {
            o->replay = a + 10;
        } else if (SDL_strncmp(a, "--server=", 9) == 0) {
            o->server = a + 9;
        } else if (SDL_strcmp(a, "--temps-reel") == 0) {
            o->realtime = 1;
        } else if (SDL_strcmp(a, "--no-temps-reel") == 0) {
            o->realtime = 0;
        } else if (SDL_strcmp(a, "--offline") == 0) {
            o->offline = true;
        } else if (SDL_strcmp(a, "--no-hud") == 0) {
            o->no_hud = true;
        } else if (SDL_strcmp(a, "--menu") == 0) {
            o->menu = true;
        } else if (SDL_strncmp(a, "--menu=", 7) == 0) {
            o->menu = true;
            o->menu_row = SDL_atoi(a + 7);
        } else if (SDL_strncmp(a, "--nom=", 6) == 0) {
            o->player = a + 6;
        } else if (SDL_strncmp(a, "--quality=", 10) == 0) {
            const char *q = a + 10;
            if (SDL_strcmp(q, "potato") == 0)      o->quality = NS_QUALITY_POTATO;
            else if (SDL_strcmp(q, "low") == 0)    o->quality = NS_QUALITY_LOW;
            else if (SDL_strcmp(q, "medium") == 0) o->quality = NS_QUALITY_MEDIUM;
            else if (SDL_strcmp(q, "high") == 0)   o->quality = NS_QUALITY_HIGH;
            else if (SDL_strcmp(q, "ultra") == 0)  o->quality = NS_QUALITY_ULTRA;
            else { fprintf(stderr, "qualité inconnue : %s (potato, low, medium, high, ultra)\n", q); return false; }
            o->quality_set = true;
        } else if (SDL_strncmp(a, "--camera=", 9) == 0) {
            const char *m = a + 9;
            if (SDL_strcmp(m, "player") == 0)     o->camera_mode = ROOM_CAM_PLAYER;
            else if (SDL_strcmp(m, "free") == 0)  o->camera_mode = ROOM_CAM_FREE;
            else if (SDL_strcmp(m, "orbit") == 0) o->camera_mode = ROOM_CAM_ORBIT;
            else { fprintf(stderr, "mode de caméra inconnu : %s\n", m); return false; }
        } else if (SDL_strncmp(a, "--angle=", 8) == 0) {
            o->camera_angle = (float)SDL_atof(a + 8) * NS_DEG2RAD;
        } else {
            fprintf(stderr, "option inconnue : %s\n", a);
            print_usage(argv[0]);
            return false;
        }
    }

    if (o->width < 64 || o->height < 64) {
        fprintf(stderr, "résolution trop petite\n");
        return false;
    }
    if (o->frames < 1) o->frames = 1;
    return true;
}

/*
 * Monte les répertoires de données, du plus prioritaire au moins prioritaire.
 *
 * L'ordre d'appel *est* l'ordre de priorité : la résolution retient le premier
 * montage qui contient le fichier. D'où cet ordre, et pas un autre :
 *
 *   1. `$NINETEEN_ASSETS` — une surcharge explicite doit gagner, sinon ce n'en est
 *      pas une. Elle était montée en dernier et ne pouvait donc rien surcharger.
 *   2. l'arbre de build — prioritaire en développement.
 *   3. le répertoire du binaire, monté en dernier recours par `ns_paths_init` et
 *      marqué comme tel : c'est la disposition d'un paquet installé.
 */
static void mount_asset_directories(void)
{
    const char *env = SDL_getenv("NINETEEN_ASSETS");
    if (env && *env) ns_paths_mount(env);
#ifdef NINETEEN_BUILD_ASSET_DIR
    ns_paths_mount(NINETEEN_BUILD_ASSET_DIR);
#endif
}

/* Le nom d'un palier, pour la configuration et pour le journal. Une seule table
 * plutôt qu'un `switch` recopié à chaque endroit qui l'affiche. */
static const char *quality_name(ns_quality q)
{
    switch (q) {
        case NS_QUALITY_POTATO: return "potato";
        case NS_QUALITY_LOW:    return "low";
        case NS_QUALITY_MEDIUM: return "medium";
        case NS_QUALITY_HIGH:   return "high";
        case NS_QUALITY_ULTRA:  return "ultra";
    }
    return "medium";
}

/* ==========================================================================
 * Le duel en différé — le fantôme
 * ========================================================================== */

/*
 * L'adversaire d'un duel : une partie déjà jouée, rejouée à côté de la sienne.
 *
 * Tout tient dans ces champs parce qu'un fantôme n'est rien d'autre qu'un
 * second état de jeu avancé par un journal d'appuis. Il ne parle pas au réseau,
 * ne lit pas d'horloge, et avance d'un pas exactement quand le joueur avance
 * d'un pas — c'est ce qui garantit que les deux parties restent comparables.
 */
typedef struct duel_ghost {
    ns_realtime_ghost_info info;
    void          *state;        /* l'état du jeu de l'adversaire */
    size_t         state_size;   /* celle du jeu pour lequel il a été alloué */
    ns_run_input  *input;        /* son journal d'appuis */
    uint32_t       count;
    uint32_t       cursor;       /* où l'on en est dans le journal */
    uint8_t        held;         /* les maintiens EN COURS, conservés entre deux lignes */
    int32_t        last_tick;
    int64_t        seed;
    char           game[32];
    bool           loaded;       /* un journal est en mémoire */
    bool           running;      /* et il est en train d'être rejoué */
    bool           finished;
} duel_ghost;

static void duel_release(duel_ghost *d)
{
    SDL_free(d->input);
    d->input = NULL;
    d->count = d->cursor = 0;
    d->loaded = d->running = d->finished = false;
}

/*
 * Prend le journal que le fil réseau vient de télécharger, s'il y en a un.
 *
 * Le journal est AUTOPORTANT : sa première ligne dit le jeu, la difficulté et
 * la graine. On n'a donc rien à recouper avec ce qu'on croyait avoir demandé —
 * et c'est voulu, parce que ce qu'on rejoue doit être ce qu'on a reçu.
 */
static void duel_take(duel_ghost *d)
{
    char *text = NULL;
    size_t len = 0;
    ns_realtime_ghost_info info;
    if (!ns_realtime_take_ghost(&text, &len, &info)) return;

    duel_release(d);

    char diff[16] = { 0 };
    if (ns_runlog_parse_inputs(text, len, d->game, sizeof d->game,
                               diff, sizeof diff, &d->seed,
                               &d->input, &d->count)) {
        d->info = info;
        d->last_tick = d->count ? d->input[d->count - 1].tick : 0;
        d->loaded = d->count > 0;
        if (d->loaded) {
            NS_INFO("duel : fantôme de %s chargé (%s, graine %lld, %u appuis, score %lld)",
                    info.name[0] ? info.name : "?", d->game,
                    (long long)d->seed, d->count, (long long)info.score);
            /* Le prochain billet doit être tiré sur LA graine de ce fantôme —
             * sans quoi on jouerait une autre partie que la sienne, et le duel
             * n'en serait pas un. */
            ns_online_set_duel(d->info.run_id);
        }
    }
    SDL_free(text);
}

/*
 * Arme le fantôme pour une partie qui commence.
 *
 * Il ne court QUE si sa graine est celle de la partie : c'est la condition qui
 * fait un duel. Sans billet du serveur — hors ligne — la partie se joue sur la
 * graine locale, qui n'est pas celle du fantôme, et le fantôme reste donc au
 * repos plutôt que de courir une partie sans rapport à côté de la nôtre.
 */
static void duel_begin(duel_ghost *d, const ns_game_api *api, uint64_t seed, bool hard)
{
    d->running = false;
    d->finished = false;
    d->cursor = 0;
    d->held = 0;
    if (!d->loaded || !api) return;
    if (SDL_strcmp(d->game, api->id) != 0) return;
    if ((uint64_t)d->seed != seed) {
        NS_INFO("duel : le fantôme joue la graine %lld, la partie %llu — pas de duel",
                (long long)d->seed, (unsigned long long)seed);
        return;
    }

    /*
     * L'état de l'adversaire est alloué ICI, à la taille du jeu qu'on est en
     * train de lancer.
     *
     * Le faire à la charge du chargement de jeu aurait demandé de le tenir à
     * jour à chaque changement de borne ; le faire ici le lie à la seule chose
     * dont il dépend — le jeu réellement joué — et un duel qui n'a pas lieu ne
     * coûte alors pas un octet.
     */
    if (d->state_size != api->state_size) {
        SDL_free(d->state);
        d->state = SDL_calloc(1, api->state_size);
        d->state_size = d->state ? api->state_size : 0;
    }
    if (!d->state) return;

    api->reset(d->state, seed, hard);
    api->set_best(d->state, 0);
    d->running = true;
}

/*
 * Avance le fantôme d'UN pas, exactement quand le joueur en avance d'un.
 *
 * Les maintiens sont CONSERVÉS entre deux lignes du journal — c'est tout
 * l'intérêt de n'enregistrer que les changements, et l'oublier donnerait un
 * adversaire qui relâche ses commandes entre deux appuis.
 */
static void duel_tick(duel_ghost *d, const ns_game_api *api, int32_t tick, float dt)
{
    if (!d->running || !d->state || !api) return;
    if (tick > d->last_tick) { d->finished = true; d->running = false; return; }

    uint8_t press = 0;
    while (d->cursor < d->count && d->input[d->cursor].tick == tick) {
        d->held = d->input[d->cursor].held;
        press |= d->input[d->cursor].pressed;
        d->cursor++;
    }
    for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
        if (press & (1u << b)) api->press(d->state, (ns_game_button)b);
    }
    if (api->hold) {
        bool h[NS_GAME_BUTTON_COUNT];
        for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) h[b] = (d->held & (1u << b)) != 0;
        api->hold(d->state, h);
    }
    api->tick(d->state, dt);
    /* `events` CONSOMME : la boucle de jeu l'appelle à chaque pas, et ne pas le
     * faire ici rejouerait un jeu différent de celui qu'on rejoue. */
    ns_game_events ev; SDL_zero(ev);
    api->events(d->state, &ev);
}

/*
 * Le tableau du duel : son score, le sien, et l'écart.
 *
 * C'est tout ce qu'un duel a besoin d'afficher. Le fantôme n'a pas de corps à
 * l'écran — il joue la MÊME partie que nous, donc le montrer voudrait dire
 * dessiner deux parties superposées — et ce qui compte, quand on affronte le
 * fantôme de quelqu'un, c'est de savoir si on est devant.
 */
static void draw_duel(ns_sprite *s, const duel_ghost *d, const ns_game_api *api,
                      uint32_t my_score)
{
    if (!d->loaded || !api || !d->state) return;

    const uint32_t his = api->score(d->state);
    const float ahead[4]  = { 0.55f, 0.95f, 0.60f, 0.95f };
    const float behind[4] = { 0.95f, 0.55f, 0.50f, 0.95f };
    const float label[4]  = { 0.60f, 0.72f, 0.88f, 0.85f };
    const float back[4]   = { 0.02f, 0.04f, 0.08f, 0.60f };

    const float scale = 2.0f;
    const float x = 24.0f;
    float y = ROOM_HUD_H * 0.5f - 40.0f;

    char line[80];
    SDL_snprintf(line, sizeof line, "DUEL - %s",
                 d->info.name[0] ? d->info.name : "FANTOME");

    ns_sprite_rect(s, x - 10.0f, y - 8.0f, 300.0f, 96.0f, back);
    ns_sprite_text(s, x, y, 1.7f, label, line);
    y += ns_sprite_text_height(1.7f) + 8.0f;

    SDL_snprintf(line, sizeof line, "TOI  %u", my_score);
    ns_sprite_text(s, x, y, scale, (my_score >= his) ? ahead : behind, line);
    y += ns_sprite_text_height(scale) + 4.0f;

    SDL_snprintf(line, sizeof line, "LUI  %u%s", his, d->finished ? " (FINI)" : "");
    ns_sprite_text(s, x, y, scale, label, line);
}

/*
 * Démarrer une partie, meilleur score compris.
 *
 * `set_best` n'était appelée qu'à la RELANCE, pour reporter le meilleur d'une
 * partie sur la suivante. Conséquence : la toute première partie d'une session
 * affichait « MEILLEUR 0 » sur son écran de fin, alors que le classement local
 * connaissait la valeur et que la ligne juste au-dessous, dans le journal,
 * l'imprimait correctement. L'écran mentait à qui n'avait pas la console.
 *
 * Le meilleur vient donc de `ns_scores`, qui est la seule source de vérité :
 * `finish_run` l'y écrit avant qu'on le relise.
 */
static void start_run(const ns_game_api *api, void *game, ns_runlog *log,
                      uint64_t seed, bool hard, duel_ghost *duel)
{
    const char *difficulty = hard ? "hard" : "normal";

    /*
     * Le BILLET, et pourquoi la graine peut changer sous nos pieds.
     *
     * `ns_online_take_ticket` rend, sans réseau et sans attente, un billet que
     * le fil est allé chercher AVANT qu'on en ait besoin. Il porte trois choses
     * qu'on ne peut pas fabriquer soi-même : l'identifiant de la partie, la
     * graine que le serveur a tirée, et le secret dont dépend le sceau. Quand il
     * y en a un, la partie se joue sur LA graine du serveur — c'est ce qui fait
     * que le score cesse d'être une valeur annoncée pour devenir une conséquence
     * recalculable.
     *
     * Sans billet — hors ligne, serveur muet, ou billet pas encore arrivé — on
     * garde la graine locale et un secret nul. La partie se joue exactement
     * pareil, `ns_runlog_enqueue` la refusera d'elle-même, et rien n'échoue.
     * C'est la règle d'A2b, tenue jusqu'ici.
     *
     * Et les captures ? La graine passée par l'appelant est fixe là où une image
     * doit se refaire à l'identique (`--play-at=`, `--game=` sans écran). Un
     * billet la remplace — mais un billet n'existe que si un serveur est
     * configuré, ce qu'aucune capture ne fait. La garantie exacte est donc :
     * **une capture hors ligne est reproductible**, ce qui couvre toutes celles
     * du dépôt. Prétendre plus serait faux, et interdire le billet en mode sans
     * écran rendrait au passage la chaîne en ligne invérifiable ici.
     */
    ns_online_ticket ticket;
    const bool ticketed = ns_online_take_ticket(api->id, difficulty, &ticket);
    if (ticketed) seed = (uint64_t)ticket.seed;

    api->reset(game, seed, hard);
    api->set_best(game, ns_scores_best(api->id, difficulty));

    /* Une nouvelle partie, donc un nouveau journal : poursuivre l'ancien
     * enverrait au serveur deux parties collées bout à bout. */
    if (log) {
        ns_runlog_begin(log, api->id, difficulty,
                        (int64_t)(seed & 0x7FFFFFFFFFFFFFFFull),
                        ticketed ? ticket.secret : NULL,
                        ticketed ? ticket.secret_len : 0);
        ns_runlog_set_run_id(log, ticketed ? ticket.run_id : NULL);
        if (ticketed) {
            NS_INFO("%s : partie %s ouverte sur le serveur (graine %lld)",
                    api->title, ticket.run_id, (long long)ticket.seed);
        }
    }

    /*
     * Le FANTÔME est armé ici, et pas chez l'appelant, pour une raison précise :
     * c'est cette fonction — et elle seule — qui sait quelle graine a REELLEMENT
     * été jouée. Le billet du serveur remplace celle qu'on lui a passée, et un
     * duel armé sur l'autre graine ferait courir un adversaire qui joue une
     * partie différente de la nôtre, sans que rien ne le dise.
     *
     * Six appelants lancent une partie ; les faire tous se souvenir de ça était
     * six occasions de l'oublier.
     */
    if (duel) duel_begin(duel, api, seed, hard);
}

/*
 * Fin de partie : le classement local d'abord, la file d'envoi ensuite.
 *
 * Extraite parce qu'elle a DEUX appelants — la boucle de jeu et l'avance rapide
 * de `--warmup=`, qui peut tuer l'oiseau avant que la boucle ne démarre. La
 * première version ne l'appelait que depuis la boucle : une partie qui se
 * terminait pendant l'avance rapide n'était ni classée ni mise en file, et la
 * capture montrait un écran de fin dont le score n'existait nulle part.
 *
 * L'ordre compte et il est le même que celui de la V1 inversé : le score local
 * est acquis AVANT qu'on se demande s'il y a un réseau. C'était l'erreur de
 * fond de 2020 — sans serveur, la partie n'existait pas.
 */
static uint32_t finish_run(ns_runlog *log, int64_t run_ms,
                           const ns_game_api *api, const void *game,
                           const char *difficulty,
                           const char *player, bool offline, bool demo)
{
    const uint32_t score = api->score(game);

    ns_runlog_event(log, run_ms, "death", 0);
    ns_runlog_end(log, run_ms, (int64_t)score);

    /*
     * Une partie de DÉMONSTRATION ne se classe pas, et ne s'envoie pas.
     *
     * `--autoplay` sert aux captures, à l'intégration continue et à
     * l'attract mode : ce n'est pas un joueur. Il écrivait pourtant dans le
     * `scores.txt` DU JOUEUR — la revue de ce dépôt y a laissé six lignes
     * sans le vouloir, dont des parties de 152 ms. Un outil qui pollue les
     * données de celui qui l'emploie est un outil cassé, et le classement
     * mondial mériterait encore moins qu'un robot y figure.
     */
    if (demo) {
        NS_INFO("%s : partie de démonstration, score %u — ni classée ni envoyée",
                api->title, score);
        return 0;
    }

    const uint32_t rank = ns_scores_record(api->id, difficulty, score,
                                           (uint32_t)run_ms, player);
    ns_scores_save();
    NS_INFO("%s : perdu à %u en %.1f s — %s, meilleur local %u",
            api->title, score, (double)run_ms / 1000.0,
            rank ? "classé" : "hors classement",
            ns_scores_best(api->id, difficulty));

    /*
     * Au mieux, jamais bloquant. Sans secret de partie — c'est-à-dire hors
     * ligne — la mise en file refuse d'elle-même : voir `ns_runlog_enqueue`.
     *
     * Et la file DOIT être poussée, sinon elle n'est qu'un dossier qui grossit.
     * C'était le trou : le journal était scellé et rangé, et personne ne
     * demandait jamais son envoi. `ns_online_flush_queue` ne fait que poser la
     * demande — le fil s'en charge, la boucle de jeu ne s'arrête pas.
     */
    if (!offline) {
        if (ns_runlog_enqueue(log)) ns_online_flush_queue();
    }
    return rank;
}

/*
 * Charge un mini-jeu : son état, ses planches, ses trois sons.
 *
 * Un seul jeu vit à la fois — on ne joue pas à deux bornes en même temps — donc
 * le précédent est libéré. Recharger le MÊME jeu ne refait rien : à la borne on
 * relance une partie plusieurs fois de suite, et recharger cinq planches à
 * chaque jeton se verrait.
 *
 * Renvoie NULL si le jeu n'est pas porté. Ce n'est pas une erreur : dix-neuf
 * bornes déclarent huit jeux, et tous ne sont pas là.
 */
static const ns_game_api *load_game(ns_rhi *rhi, ns_sprite *sprites, const char *id,
                                    const ns_game_api **cur, void **state, void **art,
                                    int *blip, int *score, int *die)
{
    const ns_game_api *api = ns_game_find(id);
    if (!api || !sprites) return NULL;

    if (*cur == api) return api;

    if (*cur) {
        if (*art) { (*cur)->art_free(rhi, *art); SDL_free(*art); *art = NULL; }
        SDL_free(*state); *state = NULL;
    }

    *state = SDL_calloc(1, api->state_size);
    *art   = SDL_calloc(1, api->art_size);
    if (!*state || !*art) {
        SDL_free(*state); SDL_free(*art);
        *state = *art = NULL;
        *cur = NULL;
        NS_WARN("« %s » : mémoire indisponible", api->id);
        return NULL;
    }
    if (!api->art_load(rhi, *art)) {
        NS_WARN("« %s » : planches indisponibles, le jeu tournera sans image", api->id);
    }
    *blip  = api->sound_blip  ? ns_audio_load(api->sound_blip)  : -1;
    *score = api->sound_score ? ns_audio_load(api->sound_score) : -1;
    *die   = api->sound_die   ? ns_audio_load(api->sound_die)   : -1;

    *cur = api;
    return api;
}


/*
 * LES AUTRES JOUEURS, dessinés dans la salle.
 *
 * Un avatar SIMPLE ET HONNÊTE plutôt qu'un personnage raté
 * -------------------------------------------------------
 * Ce qu'on dessine est une plaque au nom du joueur, posée à sa position, avec
 * la borne devant laquelle il se tient et son score en cours. Pas un bonhomme.
 *
 * Ce n'est pas un renoncement, c'est le constat que le projet a déjà fait pour
 * lui-même : il n'existe aucun modèle de personnage dans ce dépôt — on n'a que
 * des bras en vue subjective — et la scène est un tampon de géométrie CUIT au
 * build, sans chemin pour y ajouter un maillage animé à l'exécution. Fabriquer
 * une silhouette à la va-vite donnerait un pantin qui glisse dans l'allée, ce
 * qui est moins lisible qu'une étiquette et beaucoup plus laid.
 *
 * Une plaque, en revanche, répond exactement à la question qu'on se pose en
 * entrant dans une salle d'arcade : QUI est là, et à QUELLE borne. Elle est
 * dessinée dans la couche 2D, par-dessus la scène, donc sans toucher au rendu.
 *
 * Ce qu'elle ne fait pas : elle n'est pas occultée par les murs. Un joueur
 * derrière une cloison se voit à travers. C'est faux, et c'est assumé — le test
 * d'occultation demanderait un lancer de rayon par joueur et par image contre le
 * BVH, pour cacher une étiquette. La salle fait une seule pièce ouverte : le cas
 * est rare, et la corriger coûterait plus qu'elle ne gêne.
 */
static void draw_presence(ns_sprite *s, const ns_camera *cam, float aspect)
{
    ns_realtime_peer peers[NS_RT_MAX_PEERS];
    const uint32_t n = ns_realtime_peers(peers, NS_RT_MAX_PEERS);
    if (n == 0) return;

    const ns_m4 view = ns_m4_look_at(cam->position,
                                     ns_v3_add(cam->position, cam->forward),
                                     cam->up);
    const ns_m4 proj = ns_m4_perspective(cam->fov_y_degrees * NS_DEG2RAD, aspect,
                                         cam->znear, cam->zfar, true);
    const ns_m4 vp = ns_m4_mul(proj, view);

    const float white[4] = { 0.92f, 0.95f, 1.00f, 0.95f };
    const float dim[4]   = { 0.62f, 0.72f, 0.85f, 0.80f };
    const float back[4]  = { 0.03f, 0.05f, 0.09f, 0.62f };
    /* Un pseudo NON VÉRIFIÉ se distingue : le serveur n'en répond pas, et
     * l'afficher comme les autres reviendrait à garantir ce qu'on ne sait pas. */
    const float unverified[4] = { 0.85f, 0.78f, 0.45f, 0.95f };

    for (uint32_t i = 0; i < n; ++i) {
        const ns_realtime_peer *p = &peers[i];

        /* À hauteur de tête : une étiquette au niveau du sol se lit mal et se
         * confond avec les bornes. */
        const ns_v3 world = ns_v3_make(p->x, p->y + 1.75f, p->z);
        const ns_v4 clip = ns_m4_mul_v4(vp, ns_v4_from_v3(world, 1.0f));
        if (clip.w <= 0.0001f) continue;          /* derrière la caméra */

        const float ndx = clip.x / clip.w;
        const float ndy = clip.y / clip.w;
        if (ndx < -1.4f || ndx > 1.4f || ndy < -1.4f || ndy > 1.4f) continue;

        const float sx = (ndx * 0.5f + 0.5f) * ROOM_HUD_W;
        const float sy = (0.5f - ndy * 0.5f) * ROOM_HUD_H;

        /*
         * L'échelle suit la DISTANCE, comme le ferait un objet réel : une
         * étiquette de taille fixe fait paraître proche un joueur qui est au
         * fond de la salle. Bornée aux deux bouts pour rester lisible.
         */
        const float dist = ns_v3_len(ns_v3_sub(world, cam->position));
        float scale = 22.0f / ns_maxf(dist, 1.0f);
        scale = ns_clampf(scale, 1.0f, 3.0f);

        const float tw = ns_sprite_text_width(p->name, scale);
        const float th = ns_sprite_text_height(scale);

        ns_sprite_rect(s, sx - tw * 0.5f - 6.0f, sy - th * 0.5f - 4.0f,
                       tw + 12.0f, th + 8.0f, back);
        ns_sprite_text(s, sx - tw * 0.5f, sy - th * 0.5f, scale,
                       p->verified ? white : unverified, p->name);

        /* Ce qu'il fait, sous son nom. C'est ça qui rend la salle vivante :
         * « Bob — TETRIS 1200 » raconte quelque chose, une position non. */
        if (p->game[0]) {
            char line[64];
            if (p->score > 0) {
                SDL_snprintf(line, sizeof line, "%s %d", p->game, (int)p->score);
            } else {
                SDL_snprintf(line, sizeof line, "%s", p->game);
            }
            const float sub = ns_maxf(scale * 0.7f, 1.0f);
            const float sw = ns_sprite_text_width(line, sub);
            ns_sprite_text(s, sx - sw * 0.5f, sy + th * 0.5f + 4.0f, sub, dim, line);
        }
    }
}

/*
 * La liste des présents, en haut à droite.
 *
 * Les plaques ne montrent que ce qu'on REGARDE ; cette liste dit qui est dans
 * la salle même quand on leur tourne le dos. Les deux répondent à deux
 * questions différentes, et c'est pour ça qu'il y a les deux.
 */
static void draw_presence_roster(ns_sprite *s)
{
    ns_realtime_peer peers[NS_RT_MAX_PEERS];
    const uint32_t n = ns_realtime_peers(peers, NS_RT_MAX_PEERS);
    if (n == 0) return;

    const float title[4] = { 0.55f, 0.75f, 0.95f, 0.85f };
    const float name[4]  = { 0.88f, 0.92f, 1.00f, 0.90f };
    const float back[4]  = { 0.02f, 0.04f, 0.08f, 0.55f };

    const float scale = 1.6f;
    const float lh = ns_sprite_text_height(scale) + 5.0f;
    const float x = ROOM_HUD_W - 210.0f;
    float y = 18.0f;

    ns_sprite_rect(s, x - 10.0f, y - 8.0f, 200.0f, lh * (float)(n + 1) + 14.0f, back);

    char head[32];
    SDL_snprintf(head, sizeof head, "DANS LA SALLE (%u)", n);
    ns_sprite_text(s, x, y, scale, title, head);
    y += lh;

    for (uint32_t i = 0; i < n; ++i) {
        char line[64];
        if (peers[i].game[0]) {
            SDL_snprintf(line, sizeof line, "%s - %s", peers[i].name, peers[i].game);
        } else {
            SDL_snprintf(line, sizeof line, "%s", peers[i].name);
        }
        ns_sprite_text(s, x, y, scale, name, line);
        y += lh;
    }
}

/*
 * Rejoue un journal d'entrées et imprime ce qu'il produit.
 *
 * C'est la seconde moitié de `--journal-entrees=`. Un enregistrement qu'on ne
 * sait pas relire est une moitié d'outil : c'est la lecture qui transforme le
 * fichier en « je reproduis ton bug » plutôt qu'en « j'ai ton fichier ».
 *
 * Le journal donne le jeu, la difficulté et la graine ; on remonte la partie
 * dessus et on la fait avancer pas par pas, en appliquant les changements de
 * commandes aux tics où ils ont été enregistrés. Entre deux changements, l'état
 * des maintiens est CONSERVÉ — c'est tout l'intérêt de n'écrire que les
 * changements, et l'oublier donnerait un joueur qui relâche tout entre deux
 * lignes.
 */
static int replay_inputs(const char *path)
{
    char game[32] = { 0 }, difficulty[16] = { 0 };
    int64_t seed = 0;
    ns_run_input *input = NULL;
    uint32_t count = 0;

    if (!ns_runlog_read_inputs(path, game, sizeof game, difficulty, sizeof difficulty,
                               &seed, &input, &count)) {
        return 2;
    }

    const ns_game_api *api = ns_game_find(game);
    if (!api) {
        fprintf(stderr, "« %s » : jeu « %s » inconnu\n", path, game);
        SDL_free(input);
        return 2;
    }

    void *state = SDL_calloc(1, api->state_size);
    if (!state) { SDL_free(input); return 1; }

    const bool hard = (SDL_strcasecmp(difficulty, "hard") == 0);
    api->reset(state, (uint64_t)seed, hard);
    api->set_best(state, 0);

    /* Le dernier tic du journal donne la durée : rejouer plus longtemps
     * ajouterait des pas que personne n'a joués. */
    const int32_t last = count ? input[count - 1].tick : 0;
    const float step = 1.0f / (float)NS_DEFAULT_TICK_HZ;

    uint8_t held_mask = 0;
    uint32_t cursor = 0;
    int64_t gains = 0;
    int32_t died_at = -1;

    for (int32_t tick = 0; tick <= last; ++tick) {
        uint8_t press_mask = 0;
        while (cursor < count && input[cursor].tick == tick) {
            held_mask = input[cursor].held;
            press_mask |= input[cursor].pressed;
            cursor++;
        }

        for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
            if (press_mask & (1u << b)) api->press(state, (ns_game_button)b);
        }
        if (api->hold) {
            bool held[NS_GAME_BUTTON_COUNT];
            for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
                held[b] = (held_mask & (1u << b)) != 0;
            }
            api->hold(state, held);
        }
        api->tick(state, step);

        /* `events` CONSOMME : la boucle de jeu l'appelle à chaque pas, et ne pas
         * le faire ici rejouerait un jeu différent de celui qu'on a joué. */
        ns_game_events ev; SDL_zero(ev);
        api->events(state, &ev);
        if (ev.score) gains += ev.score_value;
        if (ev.die && died_at < 0) died_at = tick;
    }

    printf("rejeu de « %s »\n", path);
    printf("  jeu ............ %s (%s), graine %lld\n", api->title, difficulty, (long long)seed);
    printf("  journal ........ %u changement(s), %d pas\n", count, last + 1);
    printf("  score .......... %u\n", api->score(state));
    /*
     * L'empreinte de l'ÉTAT, et pas seulement le score.
     *
     * C'est ce qui rend `--rejouer=` utilisable comme instrument : deux
     * machines, deux compilateurs ou deux architectures peuvent tomber sur le
     * même score par des chemins différents — un oiseau mort deux pas plus tôt
     * mais après avoir passé le même nombre de tuyaux donne le même chiffre.
     * L'empreinte, elle, couvre les `state_size` octets.
     *
     * C'est aussi la brique n°1 du duel en pas verrouillé, et elle sert ici
     * avant d'y servir : mesurer le déterminisme inter-architecture demande de
     * comparer des états, pas des scores.
     */
    printf("  empreinte ...... %016llx sur %zu octets d'etat\n",
           (unsigned long long)ns_game_state_hash(api, state), api->state_size);
    printf("  gains cumulés .. %lld\n", (long long)gains);
    if (died_at >= 0) {
        printf("  mort au pas .... %d (%.1f s)\n", died_at, (double)died_at * (double)step);
    } else {
        printf("  mort ........... non, la partie courait encore\n");
    }

    SDL_free(state);
    SDL_free(input);
    return 0;
}

int main(int argc, char **argv)
{
    options opt;
    if (!parse_options(argc, argv, &opt)) return 0;

    /*
     * LE REJEU, avant tout le reste.
     *
     * Il ne demande ni fenêtre, ni GPU, ni assets, ni réseau : un mini-jeu est
     * une simulation à pas fixe et le dessin n'y change rien. C'est ce qui
     * permet de rejouer le journal de quelqu'un sur une machine sans écran, dans
     * l'intégration continue, ou sous un débogueur — c'est-à-dire là où l'on
     * cherche un bug.
     *
     * Placé ici, avant la moindre allocation, pour que le mode ne traîne pas
     * derrière lui l'initialisation de tout un moteur dont il n'a aucun besoin.
     */
    if (opt.replay) return replay_inputs(opt.replay);

    /*
     * `--pose` est validé ici, avant que quoi que ce soit soit alloué : c'est une
     * option de capture, et une capture prise avec une pose silencieusement
     * ignorée finit dans la documentation en montrant autre chose que son titre.
     */
    if (opt.pose) {
        room_viewmodel probe;
        room_viewmodel_init(&probe);
        if (!room_viewmodel_set_forced_pose(&probe, opt.pose)) {
            fprintf(stderr, "--pose=%s inconnu (idle, walk, reach, insert, press)\n",
                    opt.pose);
            return 2;
        }
    }

    /* SDL_INIT_AUDIO manquait : miniaudio ouvre son propre périphérique, mais
     * SDL doit connaître le sous-système pour que la sortie survive à un
     * changement de périphérique en cours de partie. */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init a échoué : %s\n", SDL_GetError());
        return 1;
    }

    ns_paths_init(argv[0]);
    mount_asset_directories();

    /* Le journal va dans le répertoire utilisateur, pas à côté du binaire :
     * une application installée n'a pas le droit d'écrire chez elle. */
    char log_path[1024];
    SDL_snprintf(log_path, sizeof log_path, "%snineteen.log", ns_path_user_dir());
    ns_log_open_file(log_path);
    ns_log_set_level(NS_LOG_INFO);
    NS_INFO("Nineteen %s — démarrage", NINETEEN_VERSION);

    ns_config_init("settings.cfg");

    /* La ligne de commande gagne sur la configuration, la configuration gagne
     * sur les valeurs par défaut. */
    const int win_w = (opt.width  != 1600) ? opt.width  : ns_config_get_int(NS_CFG_WINDOW_W, 1600);
    const int win_h = (opt.height != 900)  ? opt.height : ns_config_get_int(NS_CFG_WINDOW_H, 900);

    ns_rhi_desc rhi_desc;
    SDL_zero(rhi_desc);
    rhi_desc.window_title = "Nineteen";
    rhi_desc.width = win_w;
    rhi_desc.height = win_h;
    rhi_desc.fullscreen = opt.fullscreen || ns_config_get_bool(NS_CFG_FULLSCREEN, false);
    rhi_desc.vsync = opt.vsync && ns_config_get_bool(NS_CFG_VSYNC, true);
    rhi_desc.headless = opt.headless;
    rhi_desc.debug = opt.debug_gpu;
    rhi_desc.frames_in_flight = 2;

    ns_rhi *rhi = ns_rhi_create(&rhi_desc);
    if (!rhi) {
        NS_FATAL("aucun périphérique GPU utilisable");
        SDL_Quit();
        return 1;
    }

    /*
     * Le palier vient de la configuration quand la ligne de commande ne le dit
     * pas. `render.quality` était une clé réservée jamais relue depuis M1 —
     * l'audit la listait avec `render.shadowResolution` et
     * `input.mouseSensitivity` parmi les réglages qui existaient sur le papier.
     * Un réglage qu'on ne peut pas garder d'une session à l'autre n'est pas un
     * réglage, c'est une option de ligne de commande.
     */
    if (!opt.quality_set) {
        const char *q = ns_config_get_str(NS_CFG_QUALITY, "");
        if      (SDL_strcasecmp(q, "potato") == 0) opt.quality = NS_QUALITY_POTATO;
        else if (SDL_strcasecmp(q, "low") == 0)    opt.quality = NS_QUALITY_LOW;
        else if (SDL_strcasecmp(q, "medium") == 0) opt.quality = NS_QUALITY_MEDIUM;
        else if (SDL_strcasecmp(q, "high") == 0)   opt.quality = NS_QUALITY_HIGH;
        else if (SDL_strcasecmp(q, "ultra") == 0)  opt.quality = NS_QUALITY_ULTRA;
        else if (*q) NS_WARN("configuration : qualité « %s » inconnue, ignorée", q);
    }

    ns_render_settings rs;
    ns_render_settings_defaults(&rs, opt.quality);
    if (opt.render_scale <= 0.0f) {
        const float cfg = ns_config_get_float(NS_CFG_RENDER_SCALE, 0.0f);
        if (cfg > 0.0f) rs.render_scale = ns_clampf(cfg, 0.4f, 2.0f);
    }
    if (opt.render_scale > 0.0f) rs.render_scale = ns_clampf(opt.render_scale, 0.4f, 2.0f);
    if (opt.exposure > 0.0f) rs.exposure = opt.exposure;
    if (opt.particles >= 0.0f) rs.particle_density = opt.particles;
    if (opt.debug_view) {
        const int v = ns_debug_view_from_name(opt.debug_view);
        if (v < 0) {
            fprintf(stderr, "vue de débogage inconnue : %s\n", opt.debug_view);
            return 1;
        }
        rs.debug_view = v;
    }

    ns_renderer *renderer = ns_renderer_create(rhi, &rs);
    if (!renderer) {
        NS_FATAL("pipeline de rendu indisponible");
        ns_rhi_destroy(rhi);
        SDL_Quit();
        return 1;
    }

    /*
     * Deux salles coexistent : celle reconstruite par `roomgen` et celle
     * convertie du modèle de 2020. Le choix est fait **à l'exécution** et non à
     * la compilation, pour que les captures avant/après sortent du même binaire
     * avec les mêmes réglages de rendu — sinon toute différence d'image devient
     * inattribuable.
     */
    const char *room_choice = opt.room ? opt.room : ns_config_get_str(NS_CFG_ROOM_SOURCE, "generated");
    const char *scene_path = (SDL_strcmp(room_choice, "legacy") == 0)
                           ? "scene/salle-legacy.gltf"
                           : "scene/salle.gltf";

    ns_scene scene;
    if (!ns_scene_load(rhi, &scene, scene_path)) {
        /* La salle reconstruite peut ne pas avoir encore été produite ; dans ce
         * cas on retombe sur celle de 2020 plutôt que de refuser de démarrer. */
        bool loaded = false;
        if (!opt.room && SDL_strcmp(scene_path, "scene/salle.gltf") == 0) {
            NS_WARN("salle reconstruite absente, repli sur celle de 2020");
            loaded = ns_scene_load(rhi, &scene, "scene/salle-legacy.gltf");
        }
        if (!loaded) {
            NS_FATAL("la salle n'a pas pu être chargée (%s)", scene_path);
            ns_renderer_destroy(rhi, renderer);
            ns_rhi_destroy(rhi);
            SDL_Quit();
            return 1;
        }
    }

    /*
     * Point de départ : à l'intérieur de la salle jouable, à hauteur d'yeux.
     * On se base sur `room_bounds` et non sur l'emprise totale — le décor
     * lointain du modèle d'origine placerait la caméra dehors, face à un mur.
     */
    const ns_aabb room = scene.room_bounds;
    const ns_v3 centre = ns_aabb_center(room);
    const ns_v3 extent = ns_aabb_extent(room);
    const float floor_y = room.min.y;

    /*
     * Hauteur d'yeux : 1,70 m, converti à l'échelle du décor.
     *
     * Le facteur est ce qui manquait. Le modèle de 2020 est en unités Blender,
     * 2,06 par mètre — l'auteur l'écrivait lui-même (`HAUTEUR_CAMERA_DEBOUT
     * 3.5F`, legacy/room/room.c:90). La constante 1.68f posée ici plaçait donc
     * l'œil à **82 cm du sol** : un joueur d'un mètre de haut au milieu de bornes
     * de 2,4 m, qui regardait les écrans par en dessous. Exprimer la valeur en
     * mètres et la convertir vaut pour les deux salles.
     */
    const float upm = scene.units_per_metre > 0.0f ? scene.units_per_metre : 1.0f;
    const float eye_height = 1.70f * upm;

    room_camera cam;
    room_camera_init(&cam,
                     scene.has_player_start
                       ? ns_v3_make(scene.player_start.x,
                                    scene.player_start.y + eye_height,
                                    scene.player_start.z)
                       : ns_v3_make(centre.x, floor_y + eye_height,
                                    centre.z + extent.z * 0.28f),
                     scene.has_player_start ? scene.player_yaw : -90.0f * NS_DEG2RAD);

    /* Sensibilité de la souris : `input.mouseSensitivity` existait depuis M1 et
     * n'avait aucun lecteur — l'audit la listait parmi les réglages qui n'en
     * étaient pas. Le multiplicateur porte sur la valeur par défaut plutôt que
     * de la remplacer : un joueur règle « deux fois plus sensible », il ne
     * choisit pas un nombre de radians par pixel. */
    const float mouse_sens_base = cam.mouse_sensitivity;
    float mouse_sens_mult = ns_clampf(ns_config_get_float(NS_CFG_MOUSE_SENS, 1.0f), 0.1f, 8.0f);
    cam.mouse_sensitivity = mouse_sens_base * mouse_sens_mult;
    /*
     * Le corps du joueur, en mètres, converti à l'échelle du décor chargé.
     *
     * Les proportions sont celles de l'auteur de 2020 — 3,5 unités debout,
     * 2,7 accroupi (legacy/room/room.c:90-91), soit un rapport de 0,771 que l'on
     * conserve. Tout ce qui suit se déduit d'une taille d'adulte, et non de
     * constantes choisies au jugé.
     */
    cam.eye_height = cam.prev_eye_height = eye_height;
    cam.eye_height_stand  = eye_height;
    cam.eye_height_crouch = 1.31f * upm;
    cam.body_radius       = 0.32f * upm;
    cam.body_height_stand = 1.82f * upm;   /* le crâne, pas les yeux */
    cam.body_height_crouch = 1.42f * upm;
    cam.step_height       = 0.35f * upm;
    cam.gravity           = 9.81f * upm;
    cam.jump_speed        = 3.0f * upm;
    cam.speed_walk   = 1.4f * upm;    /* marche tranquille */
    cam.speed_run    = 3.3f * upm;    /* pas pressé, pas un sprint d'athlète */
    cam.speed_crouch = 0.75f * upm;
    cam.mode = opt.camera_mode;
    cam.orbit_angle = opt.camera_angle;

    /* La caméra d'orbite reste DANS la salle : elle tourne autour du centre à
     * un rayon inférieur au demi-côté, à hauteur d'homme un peu surélevée.
     * Une orbite extérieure ne montrerait que la face arrière des murs. */
    cam.orbit_center = ns_v3_make(centre.x, floor_y, centre.z);
    cam.orbit_radius = ns_minf(extent.x, extent.z) * 0.30f;
    cam.orbit_height = 1.4f * upm;

    /*
     * Point de vue nommé, déclaré par la scène.
     *
     * C'est le seul moyen honnête de comparer deux salles : `--pos=0,1.68,8`
     * désigne deux endroits différents selon l'échelle du modèle, alors que
     * `--view=allee` désigne le même cadrage dans le référentiel de chacune.
     */
    if (opt.viewpoint) {
        const ns_viewpoint *vp = ns_scene_find_viewpoint(&scene, opt.viewpoint);
        /*
         * Deux situations à ne pas confondre.
         *
         * Une salle qui ne déclare **aucun** point de vue ne peut pas satisfaire
         * `--view=`, et ce n'est pas une faute de l'appelant : c'est le cas de la
         * salle reconstruite tant que `roomgen` n'a pas écrit son fichier de
         * scène. On avertit et on garde la caméra par défaut. Sinon la cible
         * `render-compare`, dont le rôle est justement d'étayer la comparaison,
         * échouerait sur la moitié de ses captures.
         *
         * Une salle qui en déclare mais **pas celui-là**, c'est une faute de
         * frappe : erreur franche, et on liste les noms disponibles.
         */
        if (!vp && scene.viewpoint_count == 0) {
            NS_WARN("« %s » ignoré : cette salle ne déclare aucun point de vue "
                    "(fichier .scene.json absent) — caméra par défaut",
                    opt.viewpoint);
        } else if (!vp) {
            fprintf(stderr, "point de vue inconnu : %s\n  disponibles :", opt.viewpoint);
            for (uint32_t i = 0; i < scene.viewpoint_count; ++i) {
                fprintf(stderr, " %s", scene.viewpoints[i].name);
            }
            fprintf(stderr, "\n");
            ns_scene_unload(rhi, &scene);
            ns_renderer_destroy(rhi, renderer);
            ns_rhi_destroy(rhi);
            SDL_Quit();
            return 1;
        }
        if (vp) {
            if (vp->orbit) {
                cam.mode = ROOM_CAM_ORBIT;
                cam.orbit_center = vp->position;
                if (vp->orbit_radius > 0.0f) cam.orbit_radius = vp->orbit_radius;
                if (vp->orbit_height > 0.0f) cam.orbit_height = vp->orbit_height;
            } else {
                cam.mode = ROOM_CAM_FREE;
                cam.position = cam.prev_position = vp->position;
                cam.yaw   = cam.prev_yaw   = vp->yaw;
                cam.pitch = cam.prev_pitch = vp->pitch;
            }
            NS_INFO("point de vue « %s »", vp->name);
        }
    }

    /* Point de vue imposé en ligne de commande : sert au cadrage des captures
     * de référence, et à revenir exactement au même endroit d'une version du
     * moteur à l'autre pour comparer. */
    if (opt.has_view) {
        cam.mode = ROOM_CAM_FREE;
        if (opt.position) {
            float px = 0, py = 0, pz = 0;
            if (SDL_sscanf(opt.position, "%f,%f,%f", &px, &py, &pz) == 3) {
                cam.position = cam.prev_position = ns_v3_make(px, py, pz);
            } else {
                NS_WARN("--pos attend trois nombres séparés par des virgules");
            }
        }
        cam.yaw = cam.prev_yaw = opt.yaw;
        cam.pitch = cam.prev_pitch = opt.pitch;
    }

    ns_viewmodel_pose viewmodel;
    ns_viewmodel_pose_clear(&viewmodel);

    /*
     * Le son. Un échec n'arrête rien : une machine sans carte son doit pouvoir
     * jouer, et `ns_audio_*` devient alors une suite de no-ops.
     */
    ns_audio_config acfg;
    SDL_zero(acfg);
    if (ns_audio_init(&acfg)) {
        ns_audio_set_bus_volume(NS_BUS_MASTER,   ns_config_get_float(NS_CFG_VOL_MASTER, 0.9f));
        ns_audio_set_bus_volume(NS_BUS_MUSIC,    ns_config_get_float(NS_CFG_VOL_MUSIC, 0.7f));
        ns_audio_set_bus_volume(NS_BUS_SFX,      ns_config_get_float(NS_CFG_VOL_SFX, 1.0f));
        ns_audio_set_bus_volume(NS_BUS_AMBIENCE, ns_config_get_float(NS_CFG_VOL_AMBIENCE, 0.8f));
    }
    room_sound sound;
    room_sound_init(&sound, &scene);

    /*
     * La couche 2D et le mini-jeu.
     *
     * Le pipeline de sprites est lié au FORMAT de sa cible : celui de la
     * swapchain ici. En créer un par format qu'on rencontrera serait
     * l'architecture correcte le jour où les écrans de bornes rendront dans une
     * cible d'un autre format ; aujourd'hui il n'y en a qu'un, et l'annoncer
     * ainsi vaut mieux qu'un `if` qui prétendrait le contraire.
     */
    ns_sprite *sprites = ns_sprite_create(rhi, ns_rhi_swapchain_format(rhi));

    /*
     * Le jeu en cours, par POINTEUR sur son interface.
     *
     * `main.c` nommait « flappy » à quinze endroits. Ajouter les sept jeux
     * restants sur ce modèle aurait voulu dire cent-cinq modifications ici, dans
     * un fichier qui gère déjà la salle, la caméra, le son et le classement.
     * Les états sont alloués une fois, à la taille que chaque jeu déclare.
     */
    const ns_game_api *game_api = NULL;   /* le jeu chargé, NULL si aucun */
    void *game = NULL;                    /* son état */
    void *game_art = NULL;                /* ses planches */
    bool  game_hard = false;
    int   sfx_blip = -1, sfx_score = -1, sfx_die = -1;
    bool in_game = false;
    /*
     * L'objectif de regard, quand une partie démarre : la vue se pose sur la
     * dalle en `look_settle` secondes, puis la tête redevient entièrement au
     * joueur. Un tangage imposé d'un coup se lit comme un bogue de caméra ;
     * imposé en permanence, il enlève le droit de regarder ailleurs.
     */
    float look_pitch = 0.0f, look_settle = 0.0f;
    /*
     * Le journal de la partie en cours, et son horloge de simulation.
     *
     * Il est tenu même hors ligne : sans secret de serveur il ne sera pas mis en
     * file, mais l'écrire quand même coûte quelques kilo-octets et garantit que
     * le chemin est exercé à chaque partie plutôt qu'au premier branchement du
     * réseau. Un code qui ne tourne jamais est un code qui ne marche pas.
     */
    ns_runlog *runlog = ns_runlog_create(16384);
    int64_t    run_ms = 0;
    /*
     * Le numéro de pas de la partie, et les appuis survenus depuis le pas
     * précédent. Le journal d'entrées se compte en PAS FIXES et pas en
     * millisecondes : c'est la seule unité à laquelle un rejeu retombe sur ses
     * pieds, puisque c'est celle à laquelle le jeu avance.
     */
    int32_t    run_tick = 0;
    uint8_t    pending_press = 0;
    /*
     * LE DUEL EN DIFFÉRÉ — le « fantôme ».
     *
     * L'adversaire est une partie déjà jouée par quelqu'un d'autre, sur LA MÊME
     * GRAINE que la nôtre, qu'on rejoue pas par pas à côté de la sienne. Il n'y
     * a ni socket persistante, ni latence, ni divergence possible : le fantôme
     * est un journal d'appuis, et `tests/test_replay.c` a mesuré que ce journal
     * reproduit une partie au bit près pour les huit jeux.
     *
     * C'est ce qui le rend jouable AUJOURD'HUI là où le duel en direct ne l'est
     * pas : le pas verrouillé exigerait que deux machines différentes calculent
     * la même chose en virgule flottante, ce qui n'est pas mesuré ici.
     *
     * Et il marche quand l'autre est déconnecté — ce qui est la situation
     * normale entre amis.
     */
    duel_ghost duel;
    SDL_zero(duel);
    /*
     * La graine des parties de DÉMONSTRATION, qui s'enchaînent en pilote
     * automatique. Elle avance par un pas déterministe plutôt que par l'horloge :
     * une capture d'une salle en attract mode doit se refaire à l'identique, et
     * c'est le même raisonnement que pour l'avance rapide de `--warmup=`.
     */
    uint64_t   demo_seed = 20240418u;
    ns_scores_load();

    /*
     * Le classement en ligne, ACTIVABLE et jamais bloquant.
     *
     * Sans URL — le défaut — aucune socket n'est ouverte et le fil ne démarre
     * pas ; le jeu se comporte exactement comme avant. C'est la règle posée en
     * A2b : on joue d'abord, on se demande ensuite s'il y a un serveur.
     */
    {
        ns_online_config oc;
        SDL_zero(oc);
        const char *url = opt.server ? opt.server
                                     : ns_config_get_str(NS_CFG_SERVER_URL, "");
        oc.server_url = url;
        oc.token = ns_config_get_str(NS_CFG_SERVER_TOKEN, "");
        oc.locked = opt.offline;
        if (ns_online_init(&oc)) {
            /* On demande le classement de Flappy dès le départ : c'est celui que
             * la borne de classement affiche en premier. */
            ns_online_request_board("flappy", "normal");
        }
    }

    /*
     * Le TEMPS RÉEL — se voir dans la salle, et affronter les fantômes.
     *
     * Inerte par défaut, et il faut trois « oui » pour qu'il s'anime : un
     * serveur configuré, l'absence de `--offline`, et une activation explicite
     * du joueur. Les deux premiers sont vérifiés par `ns_online` ; le troisième
     * est celui-ci.
     *
     * Ce troisième verrou existe parce qu'un classement et une présence
     * n'engagent pas la même chose. Consulter un classement ne diffuse rien de
     * soi ; la présence publie un pseudo et une position. Une URL de serveur ne
     * doit pas décider ça à la place du joueur — et c'est réversible d'une
     * touche, dans le menu Échap.
     */
    {
        const bool want = (opt.realtime >= 0)
                        ? (opt.realtime != 0)
                        : ns_config_get_bool(NS_CFG_REALTIME, false);
        ns_realtime_config rc;
        SDL_zero(rc);
        rc.enabled = want;
        rc.nickname = opt.player;
        ns_realtime_init(&rc);
    }

    if (opt.offline) {
        NS_INFO("--offline : le verrou est posé, aucune partie ne sera mise en file");
    }
    {
        const uint32_t pending = ns_runlog_pending();
        if (pending) {
            NS_INFO("%u partie(s) en attente d'envoi dans « %s »",
                    pending, ns_runlog_queue_dir());
            /* Ce qui a été joué hors ligne la dernière fois part maintenant. Une
             * partie mise en file survit à une coupure, à une fermeture et à un
             * serveur éteint — elle attend le prochain démarrage, et c'est ici
             * que l'attente se termine. */
            ns_online_flush_queue();
        }
    }
    /*
     * Le chargement d'un jeu : son état, ses planches, ses trois sons.
     *
     * Un seul jeu vit à la fois — on ne joue pas à deux bornes en même temps —
     * donc on libère le précédent. Recharger le MÊME jeu ne refait rien : à la
     * borne on relance une partie plusieurs fois de suite, et recharger cinq
     * planches à chaque jeton se verrait.
     */
    #define LOAD_GAME(id) load_game(rhi, sprites, (id), &game_api, &game, &game_art, \
                                    &sfx_blip, &sfx_score, &sfx_die)

    /*
     * La dalle sur laquelle le jeu tourne : une cible de rendu 512 x 288, du même
     * format que la swapchain puisqu'elle emprunte le pipeline de la couche 2D.
     *
     * 512 x 288 et pas plus : c'est un tube d'arcade vu à un mètre, et la
     * courbure du verre en mange déjà les bords. Un écran plus défini ne se
     * verrait pas et coûterait une passe de rendu plus chère à chaque image.
     */
    ns_texture screen_rt;
    SDL_zero(screen_rt);
    /* La dalle de la borne de classement, rendue elle aussi à chaque image.
     * Séparée de `screen_rt` : les deux doivent pouvoir vivre en même temps —
     * on regarde le classement PENDANT qu'une partie tourne à côté. */
    ns_texture board_rt;
    SDL_zero(board_rt);
    int32_t board_material = -1;
    if (sprites) {
        ns_texture_desc sd;
        SDL_zero(sd);
        sd.width = 512; sd.height = 288;
        sd.format = ns_rhi_swapchain_format(rhi);
        sd.render_target = true;
        sd.sampled = true;
        sd.name = "écran de borne";
        if (!ns_texture_create(rhi, &screen_rt, &sd)) {
            NS_WARN("écran de borne : cible indisponible, le jeu ne sera qu'en plein écran");
        }
        sd.name = "écran de classement";
        if (!ns_texture_create(rhi, &board_rt, &sd)) {
            NS_WARN("classement : cible indisponible, la borne gardera son image de 2020");
        }
    }

    /*
     * Le matériau de la dalle de la borne de classement, cherché une fois.
     *
     * Par le JEU déclaré et non par le nom : `salle.room.json` peut renommer la
     * borne, il ne peut pas lui retirer son jeu sans que ce soit intentionnel.
     */
    for (uint32_t i = 0; i < scene.cabinet_count; ++i) {
        if (SDL_strcasecmp(scene.cabinets[i].game, "leaderboard") == 0) {
            board_material = scene.cabinets[i].screen_material;
            NS_INFO("classement : « %s », matériau de dalle %d",
                    scene.cabinets[i].name, board_material);
            break;
        }
    }
    /* La borne devant laquelle on joue, et son matériau de dalle. */
    int32_t playing_material = -1;
    /* La borne sur laquelle on joue. Sert à poser les mains sur ses commandes —
     * et seulement à ça : la scène ne bouge pas pendant une partie, donc un
     * pointeur suffit là où la machine à états, elle, recopie ses cibles. */
    const ns_cabinet *playing_cab = NULL;
    bool    fullscreen_game = false;
    /* Le bandeau de réglages : quelques secondes après F7 ou F8. Un réglage
     * qu'on change sans retour visuel est un réglage dont on doute. */
    float   settings_banner = 0.0f;

    /*
     * Le menu. `Échap` l'ouvre — c'est la convention, et c'est mieux que ce qu'il
     * faisait : un premier Échap relâchait la souris, un second QUITTAIT le jeu.
     * Perdre sa partie parce qu'on a appuyé deux fois sur Échap est un défaut,
     * pas un raccourci.
     */
    room_menu menu; SDL_zero(menu);
    /*
     * L'interrupteur du temps réel, que le menu écrit et persiste.
     *
     * Sa valeur de départ est celle qui a RÉELLEMENT été retenue — ligne de
     * commande comprise — et non le réglage sur disque : le menu doit montrer
     * l'état du jeu qui tourne, pas une case cochée dans le vide. C'est aussi
     * ce qui fait que `--no-temps-reel` se voit dans le menu.
     */
    bool menu_realtime = ns_realtime_enabled();
    room_menu_ctx menu_ctx = { &rs, &mouse_sens_mult, &menu_realtime };
    if (opt.menu) {
        room_menu_open(&menu);
        /* Une ligne hors bornes ne surligne rien et ne se répare jamais :
         * le menu la normalise, la ligne de commande ne doit pas l'y forcer. */
        if (opt.menu_row > 0) for (int i = 0; i < opt.menu_row; ++i)
            room_menu_input(&menu, &menu_ctx, ROOM_MENU_DOWN);
    }
    /* Le rang de la dernière partie, pour l'écran de fin. */
    uint32_t last_rank = 0;

    if (opt.game && sprites) {
        if (LOAD_GAME(opt.game)) {
            /*
             * Sans écran, la graine est FIXE — la même que celle de `--play-at`.
             *
             * Une capture doit se reproduire à l'identique, et elle ne le
             * faisait pas : `--game=tetris --autoplay --warmup=40` rendait 16 600
             * points d'un build à l'autre et 244 100 du suivant, ce qui a
             * d'abord ressemblé à un défaut de calcul avant d'être simplement
             * l'horloge. Une image qu'on ne peut pas refaire ne prouve rien.
             */
            const uint64_t seed = opt.headless ? 20240418u
                                               : (uint64_t)SDL_GetPerformanceCounter();
            game_hard = false;
            /* Le billet n'arrivera pas à temps pour CETTE partie — une partie ne
             * s'attend pas — mais il sera là pour la suivante, et `--autoplay`
             * en enchaîne. */
            ns_online_prefetch_ticket(game_api->id, "normal");
            start_run(game_api, game, runlog, seed, game_hard, &duel);
            run_ms = 0;
            run_tick = 0; pending_press = 0;
            in_game = true;
            fullscreen_game = true;      /* `--game=` est le mode plein écran */
            NS_INFO("%s : partie démarrée", game_api->title);
        } else {
            char known[256]; known[0] = '\0';
            for (int i = 0; i < ns_game_count(); ++i) {
                if (i) SDL_strlcat(known, ", ", sizeof known);
                SDL_strlcat(known, ns_game_at(i)->id, sizeof known);
            }
            NS_WARN("--game=%s : pas encore porté (%s)", opt.game, known);
        }
    }

    /*
     * `--play-at=` : se planter devant une borne nommée et lancer sa partie, en
     * restant en 3D. C'est le seul moyen de photographier — ou de vérifier en
     * intégration continue — un jeu qui tourne DANS son écran, puisqu'il n'y a
     * pas de clavier en headless.
     */
    if (opt.play_at && sprites && screen_rt.handle) {
        const ns_cabinet *pick = NULL;
        for (uint32_t i = 0; i < scene.cabinet_count; ++i) {
            if (SDL_strcasecmp(scene.cabinets[i].name, opt.play_at) == 0) { pick = &scene.cabinets[i]; break; }
        }
        if (!pick) {
            fprintf(stderr, "borne inconnue : %s\n  disponibles :", opt.play_at);
            for (uint32_t i = 0; i < scene.cabinet_count; ++i) {
                fprintf(stderr, " %s", scene.cabinets[i].name);
            }
            fprintf(stderr, "\n");
        } else {
            cam.mode = ROOM_CAM_PLAYER;
            /*
             * **On se place sur l'ancre, sans reculer.**
             *
             * Le premier jet reculait de 35 cm « pour voir la borne », à une
             * époque où la dalle était en 4:3 et sortait du cadre par le bas.
             * Elle est en 16:9 depuis, donc plus basse, et le recul coûtait
             * cher : il portait le panneau de commande à 1,05 m de l'épaule pour
             * 57 cm de bras. Les mains ne pouvaient pas se poser sur les
             * commandes — on voyait la partie tourner et deux bras tendus vers
             * rien.
             *
             * L'ancre est calculée pour qu'on ATTEIGNE les boutons. C'est aussi
             * là que la collision arrête le joueur qui vient jouer. S'y placer,
             * c'est donc cadrer ce que le jeu cadre vraiment.
             */
            const ns_v3 back = ns_v3_zero();
            cam.position = cam.prev_position = ns_v3_make(pick->player_anchor.x + back.x,
                                                          pick->player_anchor.y + cam.eye_height,
                                                          pick->player_anchor.z + back.z);
            /* Regarder la borne : la normale d'écran pointe vers le joueur, donc
             * on regarde dans le sens opposé. Le lacet suit
             * `forward = (cos, ., sin)`. */
            cam.yaw = cam.prev_yaw = atan2f(-pick->screen_normal.z, -pick->screen_normal.x);
            /*
             * Le tangage est CALCULÉ, plus codé en dur.
             *
             * Il valait −23°, mesuré pour une dalle à 1,05 m de l'œil. Le
             * caisson est devenu un profil en gradins : l'écran a reculé de
             * 32 cm et l'angle juste est passé à −33°. Une constante mesurée sur
             * une géométrie donnée redevient fausse dès que la géométrie bouge,
             * et personne ne pense à la rouvrir — alors que la borne déclare
             * déjà où est son écran.
             */
            {
                const float ex = pick->screen_center.x - cam.position.x;
                const float ey = pick->screen_center.y - cam.position.y;
                const float ez = pick->screen_center.z - cam.position.z;
                const float flat = sqrtf(ex * ex + ez * ez);
                cam.pitch = cam.prev_pitch = atan2f(ey, ns_maxf(0.05f, flat));
            }
            cam.velocity = ns_v3_zero();

            const bool hard = (SDL_strcasecmp(pick->difficulty, "hard") == 0);
            if (!LOAD_GAME(pick->game)) {
                NS_WARN("borne « %s » : « %s » n'est pas encore porté",
                        pick->name, pick->game);
                goto play_at_done;
            }
            game_hard = hard;
            ns_online_prefetch_ticket(game_api->id, hard ? "hard" : "normal");
            start_run(game_api, game, runlog, 20240418, hard, &duel);
            run_ms = 0;
            run_tick = 0; pending_press = 0;
            in_game = true;
            fullscreen_game = false;
            playing_material = pick->screen_material;
            /* Personne n'a inséré de jeton ici : la boucle posera les mains sur
             * les commandes dès le premier pas. Sans ça, la capture montrait la
             * partie tourner dans la dalle et les bras pendre hors du cadre. */
            playing_cab = pick;
            NS_INFO("borne « %s » (%s, %s) : partie dans la dalle, matériau %d",
                    pick->name, pick->game, hard ? "hard" : "normal", pick->screen_material);
        }
play_at_done: ;
    }

    /*
     * Avance de simulation, pour les captures — et pour LES DEUX modes.
     *
     * Elle ne valait d'abord que pour `--game=`, donc une capture prise devant
     * une borne montrait toujours un ciel vide : le premier tuyau est à 1 056 px
     * de l'oiseau et le décor défile à 240 px/s, il faut 4,4 s de jeu avant que
     * l'écran montre quoi que ce soit. On avance la simulation seule — c'est
     * gratuit, et ça n'existe que parce que la partie est une structure qu'on
     * fait avancer sans rien dessiner.
     */
    if (in_game && opt.warmup > 0.0f) {
        const float step = (float)(1.0 / NS_DEFAULT_TICK_HZ);
        const int steps = (int)(opt.warmup / step);
        /* Fixe, pour que deux captures identiques le restent. */
        const uint64_t warm_seed = 20240418u;
        int runs = 0;
        for (int k = 0; k < steps; ++k) {
            if (opt.autoplay && game_api->autopilot) game_api->autopilot(game);
            game_api->tick(game, step);
            /*
             * L'avance rapide tient le journal comme la boucle normale.
             *
             * Sans ça, une partie capturée après `--warmup=6` était soumise
             * amputée de ses six premières secondes : le serveur recalculait un
             * score plus bas que celui affiché et refusait la partie, sans que
             * rien du côté client ne le laisse prévoir. « Avancer plus vite »
             * doit rester la seule différence entre les deux boucles.
             */
            run_ms += (int64_t)(step * 1000.0f + 0.5f);
            ns_game_events ev; SDL_zero(ev);
            game_api->events(game, &ev);
            if (ev.blip)  ns_runlog_event(runlog, run_ms, ev.blip_kind, 0);
            if (ev.score) ns_runlog_event(runlog, run_ms, ev.score_kind, ev.score_value);
            if (ev.die) {
                last_rank = finish_run(runlog, run_ms, game_api, game,
                                       game_hard ? "hard" : "normal",
                                       opt.player, opt.offline, opt.autoplay);
                runs++;
                /*
                 * On RELANCE, parce que `--warmup=N` veut dire « joue N
                 * secondes », pas « joue jusqu'à la première mort puis attends ».
                 *
                 * La nuance ne se voyait pas tant que le seul jeu automatisé
                 * était Flappy, dont le pilote survit indéfiniment. Le Démineur
                 * a une grille minée au quart : quand la déduction s'épuise il
                 * doit deviner, et une capture sur trois montrait un écran de
                 * fin au lieu d'une partie. La graine dérive du numéro de
                 * partie, donc la capture reste rejouable à l'identique.
                 */
                const uint64_t seed = warm_seed + 0x9E3779B97F4A7C15ull * (uint64_t)runs;
                start_run(game_api, game, runlog, seed, game_hard, &duel);
                run_ms = 0;
            run_tick = 0; pending_press = 0;
                run_tick = 0; pending_press = 0;
            }
        }
        NS_INFO("%s : %.1f s avancées (%d pas), %d partie(s), score %u",
                game_api->title, (double)opt.warmup, steps, runs + 1,
                game_api->score(game));
    }

    /*
     * La poussière, déclarée par la salle. La recopie tient en huit champs et
     * évite que `engine/scene` dépende de `engine/fx` — charger une salle ne doit
     * pas imposer d'avoir un moteur de rendu.
     */
    if (scene.dust_count) {
        ns_particle_zone zones[NS_MAX_DUST_ZONES];
        for (uint32_t i = 0; i < scene.dust_count; ++i) {
            const ns_dust_zone *d = &scene.dust[i];
            SDL_zero(zones[i]);
            zones[i].bounds = d->bounds;
            zones[i].density = d->density;
            SDL_memcpy(zones[i].drift, d->drift, sizeof d->drift);
            zones[i].size = d->size;
            SDL_memcpy(zones[i].color, d->color, sizeof d->color);
            zones[i].brightness = d->brightness;
        }
        ns_renderer_set_particle_zones(renderer, zones, scene.dust_count, 0xA11CEu);
    }

    room_viewmodel vmstate;
    room_viewmodel_init(&vmstate);
    (void)room_viewmodel_set_forced_pose(&vmstate, opt.pose);   /* déjà validé */

    /*
     * `--pose` impose le mode joueur, quel que soit ce que `--view` a demandé.
     *
     * Les points de vue nommés basculent en caméra libre (`:515`) — c'est ce
     * qu'on veut pour cadrer un mur —, et les bras ne sont dessinés qu'en mode
     * joueur. La combinaison `--view=allee --pose=reach` produisait donc une
     * capture sans bras, sans le moindre message : exactement le silence contre
     * lequel `--pose` refuse déjà un nom mal orthographié.
     */
    if (opt.pose && cam.mode != ROOM_CAM_PLAYER) {
        cam.mode = ROOM_CAM_PLAYER;
        cam.velocity = ns_v3_zero();
        cam.grounded = false;
        NS_INFO("--pose : caméra en mode joueur (les bras n'existent pas en caméra libre)");
    }

    /*
     * Un geste a besoin d'une cible. `--pose=reach` fige bien l'état, mais sans
     * borne à portée la machine à états n'a rien à viser et la main reste au
     * repos — une capture qui montre le contraire de son nom.
     *
     * On déclenche donc la séquence une fois, ici : `--view=borne` place le
     * joueur sur l'ancre déclarée de `borne_arcade_1`, et la borne est trouvée
     * par la même fonction que la touche `E`. Sans borne à portée on le dit, au
     * lieu de livrer une image muette.
     */
    if (opt.pose) {
        const ns_cabinet *near = room_viewmodel_target(&scene, &cam);
        if (near) {
            room_viewmodel_interact(&vmstate, near);
            NS_INFO("--pose : geste dirigé vers « %s » (%s)", near->name, near->game);
        } else if (SDL_strcasecmp(opt.pose, "idle") != 0
                && SDL_strcasecmp(opt.pose, "repos") != 0
                && SDL_strcasecmp(opt.pose, "walk") != 0
                && SDL_strcasecmp(opt.pose, "marche") != 0) {
            NS_WARN("--pose=%s : aucune borne à portée, les bras resteront au repos "
                    "(essayer --view=borne)", opt.pose);
        }
    }

    ns_clock clock;
    ns_clock_init(&clock, NS_DEFAULT_TICK_HZ);

    bool running = true;
    bool mouse_captured = false;
    int frames_rendered = 0;

    double bench_total = 0.0, bench_min = 1e30, bench_max = 0.0;
    int    bench_count = 0;

    if (!opt.headless) {
        SDL_SetWindowRelativeMouseMode(ns_rhi_window(rhi), true);
        mouse_captured = true;
    }

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                /*
                 * Le menu prend TOUT le clavier tant qu'il est ouvert.
                 *
                 * Laisser passer le reste donnerait un joueur qui bat des ailes
                 * en réglant le volume — et c'est le genre de chose qu'on ne
                 * découvre qu'en jouant, une fois livré.
                 */
                if (menu.open) {
                    switch (ev.key.key) {
                    case SDLK_UP:     case SDLK_W:
                        room_menu_input(&menu, &menu_ctx, ROOM_MENU_UP); break;
                    case SDLK_DOWN:   case SDLK_S:
                        room_menu_input(&menu, &menu_ctx, ROOM_MENU_DOWN); break;
                    case SDLK_LEFT:   case SDLK_A:
                        room_menu_input(&menu, &menu_ctx, ROOM_MENU_LEFT); break;
                    case SDLK_RIGHT:  case SDLK_D:
                        room_menu_input(&menu, &menu_ctx, ROOM_MENU_RIGHT); break;
                    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
                        room_menu_input(&menu, &menu_ctx, ROOM_MENU_ACCEPT); break;
                    case SDLK_ESCAPE:
                        room_menu_input(&menu, &menu_ctx, ROOM_MENU_CANCEL); break;
                    default: break;
                    }
                    break;
                }
                switch (ev.key.key) {
                case SDLK_ESCAPE:
                    if (in_game) {
                        /* Quitter la partie rend la salle, pas le bureau. */
                        in_game = false;
                        playing_material = -1;
                        playing_cab = NULL;
                        room_viewmodel_stop_playing(&vmstate);
                        NS_INFO("%s : score %u, meilleur %u", game_api->title,
                                game_api->score(game), game_api->best(game));
                        break;
                    }
                    room_menu_open(&menu);
                    if (mouse_captured) {
                        SDL_SetWindowRelativeMouseMode(ns_rhi_window(rhi), false);
                        mouse_captured = false;
                    }
                    break;
                case SDLK_F2: {
                    /* Capture à la demande, dans le répertoire utilisateur. */
                    char shot[1024];
                    SDL_snprintf(shot, sizeof shot, "%snineteen-%llu.png",
                                 ns_path_user_dir(), (unsigned long long)ns_rhi_frame_index(rhi));
                    ns_rhi_request_screenshot(rhi, shot);
                    break;
                }
                case SDLK_SPACE: case SDLK_RETURN:
                case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
                    if (in_game) {
                        /*
                         * En partie, le clavier appartient au jeu. Après la mort
                         * l'action relance — mais seulement une fois la chute
                         * finie, sinon un appui maintenu au moment du choc
                         * redémarre avant qu'on ait vu ce qui s'est passé.
                         */
                        if (!ev.key.repeat) {
                            float dead_time = 0.0f;
                            const bool dead = game_api->dead(game, &dead_time);
                            const bool action = (ev.key.key == SDLK_SPACE
                                              || ev.key.key == SDLK_RETURN);
                            if (dead && action && dead_time > 0.8f) {
                                const uint64_t seed = (uint64_t)SDL_GetPerformanceCounter();
                                start_run(game_api, game, runlog, seed, game_hard, &duel);
                                run_ms = 0;
                                run_tick = 0; pending_press = 0;
            run_tick = 0; pending_press = 0;
                run_tick = 0; pending_press = 0;
                            } else {
                                ns_game_button b = NS_GAME_ACTION;
                                switch (ev.key.key) {
                                    case SDLK_UP:    b = NS_GAME_UP; break;
                                    case SDLK_DOWN:  b = NS_GAME_DOWN; break;
                                    case SDLK_LEFT:  b = NS_GAME_LEFT; break;
                                    case SDLK_RIGHT: b = NS_GAME_RIGHT; break;
                                    default: break;
                                }
                                game_api->press(game, b);
                                /* Le même appui, pour le journal d'entrées : il
                                 * arrive du gestionnaire d'événements, donc
                                 * AVANT le pas, et sera consommé par lui. */
                                pending_press |= (uint8_t)(1u << b);
                            }
                        }
                        break;
                    }
                    /* Hors partie, seule l'espace veut dire quelque chose. Le saut
                     * est une transition, pas un état : lu en événement pour qu'un
                     * appui bref ne se perde pas entre deux pas. */
                    if (ev.key.key == SDLK_SPACE && !ev.key.repeat) cam.jump_requested = true;
                    break;
                case SDLK_F5:
                    cam.mode = (cam.mode == ROOM_CAM_FREE) ? ROOM_CAM_PLAYER : ROOM_CAM_FREE;
                    /* Repartir du sol : en passant du vol libre au mode joueur,
                     * la vitesse accumulée en l'air se transformerait en chute. */
                    cam.velocity = ns_v3_zero();
                    cam.grounded = false;
                    NS_INFO("caméra : %s", cam.mode == ROOM_CAM_FREE ? "libre" : "joueur");
                    break;
                case SDLK_F6:
                    cam.mode = ROOM_CAM_ORBIT;
                    NS_INFO("caméra : orbite");
                    break;

                /*
                 * Les deux réglages qui décident vraiment de la fluidité, à
                 * portée de touche et sans quitter la partie.
                 *
                 * Elles restent des RACCOURCIS. Le menu (`Échap`) fait la
                 * même chose en le montrant ; ces deux touches font gagner
                 * l'ouverture quand on compare deux paliers d'affilée, ce qui
                 * est exactement ce qu'on fait en cherchant le bon réglage.
                 *
                 * Mesuré sur lavapipe en 1280 x 720 depuis `allee` :
                 * potato 114 ms, low 281, medium 675, high 1056, ultra 2647. Et
                 * l'échelle de rendu, à palier constant : 0,8 fait gagner 34 %,
                 * 0,6 en fait gagner 62 %.
                 */
                case SDLK_F7: {
                    if (ev.key.repeat) break;
                    const ns_quality next =
                        (rs.quality >= NS_QUALITY_ULTRA) ? NS_QUALITY_POTATO
                                                         : (ns_quality)(rs.quality + 1);
                    const float keep_scale = rs.render_scale;
                    ns_render_settings_defaults(&rs, next);
                    /* L'échelle de rendu est un réglage à part : elle a sa propre
                     * touche, et changer de palier ne doit pas l'écraser. */
                    rs.render_scale = keep_scale;
                    if (opt.exposure > 0.0f) rs.exposure = opt.exposure;
                    if (opt.particles >= 0.0f) rs.particle_density = opt.particles;
                    ns_renderer_set_settings(rhi, renderer, &rs);
                    settings_banner = 2.6f;
                    NS_INFO("qualité : %s", quality_name(rs.quality));
                    break;
                }
                case SDLK_F8: {
                    if (ev.key.repeat) break;
                    /* Par pas de 0,1 entre 0,5 et 1,0, puis retour en bas. En
                     * dessous de 0,5 le texte des écrans cesse d'être lisible, et
                     * au-dessus de 1,0 on paie du sur-échantillonnage que le halo
                     * et le tone mapping rendent invisible. */
                    float sc = rs.render_scale + 0.1f;
                    if (sc > 1.005f) sc = 0.5f;
                    rs.render_scale = sc;
                    ns_renderer_set_settings(rhi, renderer, &rs);
                    settings_banner = 2.6f;
                    NS_INFO("échelle de rendu : %.2f", (double)rs.render_scale);
                    break;
                }
                case SDLK_E: {
                    /*
                     * `ns_scene_nearest_cabinet` est écrite depuis M4 et n'avait
                     * jamais eu un seul appelant — comme `screen_center`,
                     * `player_anchor` et `ns_poi`. Elle en a un.
                     *
                     * L'invite affichée à l'écran (« E pour jouer ») demande la
                     * couche 2D, qui n'existe pas encore : c'est A9. En attendant
                     * le geste part quand on est à portée, et le journal le dit —
                     * ce qui suffit à le vérifier sans texte à l'écran.
                     */
                    if (ev.key.repeat) break;
                    const ns_cabinet *near = room_viewmodel_target(&scene, &cam);
                    if (near && room_viewmodel_interact(&vmstate, near)) {
                        room_sound_coin(&sound, near->coin_slot);
                        NS_INFO("borne « %s » (%s) : jeton", near->name, near->game);

                        /*
                         * La borne décide du jeu, et de sa difficulté. C'est ce
                         * que `salle.room.json` déclare depuis A4 et que rien ne
                         * lisait : dix-neuf bornes affectées à un jeu, et aucune
                         * qui en lançait un.
                         */
                        if (LOAD_GAME(near->game)) {
                            const bool hard = (SDL_strcasecmp(near->difficulty, "hard") == 0);
                            const uint64_t seed = (uint64_t)SDL_GetPerformanceCounter();
                            game_hard = hard;
                            start_run(game_api, game, runlog, seed, hard, &duel);
                            run_ms = 0;
                            run_tick = 0; pending_press = 0;
                            in_game = true;

                            /*
                             * POSER LE REGARD SUR LA DALLE — la vraie cause du
                             * « je suis obligé de m'accroupir ».
                             *
                             * `--play-at=` calculait déjà ce tangage ; ce
                             * chemin-ci, celui qu'on emprunte RÉELLEMENT en
                             * jouant, ne le faisait pas. On appuyait sur E, la
                             * partie démarrait, et la vue restait à
                             * l'horizontale — avec un champ vertical de 62°
                             * (donc ±31°) et une dalle 26° plus bas, l'image
                             * était en bas du cadre, presque hors champ. On
                             * s'accroupissait pour la ramener au centre.
                             *
                             * Deux enseignements de la capture `--play-at`, que
                             * j'ai mis trop longtemps à rapprocher : elle était
                             * bien cadrée, et elle était la SEULE à l'être. Une
                             * vérification qui emprunte un chemin que le joueur
                             * n'emprunte pas ne vérifie rien.
                             *
                             * C'est un objectif, pas une téléportation :
                             * `look_settle` amène la vue en un tiers de seconde
                             * et rend la main. Baisser les yeux vers l'écran est
                             * le geste qu'on fait devant une vraie borne ; le
                             * lui arracher ensuite ne l'est pas.
                             */
                            {
                                const float ex = near->screen_center.x - cam.position.x;
                                const float ey = near->screen_center.y - cam.position.y;
                                const float ez = near->screen_center.z - cam.position.z;
                                const float flat = sqrtf(ex * ex + ez * ez);
                                look_pitch = atan2f(ey, ns_maxf(0.05f, flat));
                                look_settle = 0.33f;
                            }
                            /*
                             * On reste EN 3D : le jeu tourne dans la dalle de la
                             * borne, et la tête reste libre. C'est toute la
                             * différence entre « lancer un mini-jeu » et « jouer
                             * sur une borne » — on voit l'écran à travers son
                             * verre bombé, on peut se pencher, reculer, regarder
                             * la borne d'à côté.
                             */
                            playing_material = near->screen_material;
                            fullscreen_game = false;
                            playing_cab = near;
                        } else {
                            /* Le jeton part quand même : le geste est celui de la
                             * salle, pas celui du jeu. Mais on le DIT, plutôt que
                             * de laisser croire à une borne cassée. */
                            NS_INFO("borne « %s » : « %s » n'est pas encore porté",
                                    near->name, near->game);
                        }
                    }
                    break;
                }
                default:
                    break;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (mouse_captured) {
                    cam.mouse_dx += ev.motion.xrel;
                    cam.mouse_dy += ev.motion.yrel;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!mouse_captured && !opt.headless) {
                    SDL_SetWindowRelativeMouseMode(ns_rhi_window(rhi), true);
                    mouse_captured = true;
                }
                break;

            default:
                break;
            }
        }

        /*
         * Clavier lu en continu plutôt qu'en événements : on veut l'état, pas
         * les transitions.
         *
         * Les *scancodes* désignent une position physique sur le clavier, pas
         * une lettre : `SDL_SCANCODE_W` est la touche qui porte un W en QWERTY
         * et un Z en AZERTY. Le bloc ZQSD est donc déjà branché, et il l'était —
         * un commentaire prétendait ici que les deux étaient acceptés « en plus »
         * l'un de l'autre, ce qui n'a pas de sens : c'est la même touche. Il n'y
         * a rien à ajouter, seulement à cesser de le décrire de travers.
         */
        /*
         * Ce que le menu a demandé pendant les événements. Appliqué ICI, une
         * fois par image, et pas dans le gestionnaire de touche : régler la
         * qualité recrée les cibles de rendu, et on ne veut pas le faire cinq
         * fois parce qu'une flèche a été maintenue.
         */
        if (menu.render_dirty) {
            menu.render_dirty = false;
            if (opt.exposure > 0.0f) rs.exposure = opt.exposure;
            if (opt.particles >= 0.0f) rs.particle_density = opt.particles;
            ns_renderer_set_settings(rhi, renderer, &rs);
            NS_INFO("réglages : %s, échelle %.2f", quality_name(rs.quality),
                    (double)rs.render_scale);
        }
        cam.mouse_sensitivity = mouse_sens_base * mouse_sens_mult;
        if (menu.quit_request) {
            room_menu_persist(&menu_ctx);
            running = false;
        }
        if (menu.close_request) {
            room_menu_close(&menu);
            room_menu_persist(&menu_ctx);
            /* Reprendre la souris tout de suite : autrement il faut un clic pour
             * revenir au jeu, ce qui se lit comme un bogue. */
            if (!opt.headless && !mouse_captured) {
                SDL_SetWindowRelativeMouseMode(ns_rhi_window(rhi), true);
                mouse_captured = true;
                cam.mouse_dx = cam.mouse_dy = 0.0f;
            }
        }

        const bool *keys = SDL_GetKeyboardState(NULL);
        cam.input_forward = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] ? 1.0f : 0.0f)
                          - (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f);
        cam.input_strafe  = (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f)
                          - (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
        cam.running = keys[SDL_SCANCODE_LSHIFT];
        if (menu.open) {
            /* Le monde continue de vivre derrière le voile — la poussière, les
             * écrans, le brouillard : c'est ce qui permet de JUGER un réglage
             * pendant qu'on le change. Seul le joueur est figé. */
            cam.input_forward = cam.input_strafe = 0.0f;
            cam.running = false;
            cam.mouse_dx = cam.mouse_dy = 0.0f;
        }
        if (in_game) {
            /*
             * PENDANT UNE PARTIE, LES DIRECTIONS APPARTIENNENT AU JEU.
             *
             * Elles ne l'étaient pas : ZQSD et les flèches pilotaient le serpent
             * ET le joueur en même temps. On dirigeait sa partie en s'éloignant
             * de la borne — jusqu'à sortir de portée, la partie continuant à
             * jouer toute seule dans une dalle qu'on ne regardait plus.
             *
             * Le menu était déjà traité ainsi trois lignes plus haut ; c'est le
             * même besoin, et il manquait pour le cas le plus fréquent.
             *
             * La TÊTE, elle, reste libre : on joue dans la salle, pas dans un
             * plein écran, et pouvoir regarder son voisin pendant qu'on joue est
             * précisément ce qu'on cherchait en gardant la 3D autour du jeu.
             */
            cam.input_forward = cam.input_strafe = 0.0f;
            cam.running = false;
        }

        /* Espace et Ctrl ne veulent pas dire la même chose selon le mode : en vol
         * libre ils montent et descendent, en mode joueur ils sautent et
         * accroupissent. Les confondre donnait un joueur capable de s'envoler. */
        cam.crouch_held = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_C];
        if (cam.mode == ROOM_CAM_FREE) {
            cam.input_up = (keys[SDL_SCANCODE_SPACE] ? 1.0f : 0.0f)
                         - (keys[SDL_SCANCODE_LCTRL] ? 1.0f : 0.0f);
        } else {
            cam.input_up = 0.0f;
        }

        /*
         * Le billet se demande quand le joueur ARRIVE devant la borne, pas quand
         * il appuie. Entre les deux il y a la main qui s'avance, le jeton qui
         * tombe et le bouton qu'on presse — une seconde et demie, largement de
         * quoi faire un aller-retour HTTP sans que la salle ne se fige. C'est
         * toute la raison d'être du billet d'avance : une partie commence quand
         * le joueur appuie, pas quand le serveur répond.
         */
        if (!in_game && cam.mode == ROOM_CAM_PLAYER) {
            const ns_cabinet *ahead = room_viewmodel_target(&scene, &cam);
            if (ahead && ahead->game[0]) {
                ns_online_prefetch_ticket(ahead->game, ahead->difficulty);
            }
        }

        /*
         * LA PRÉSENCE : où je suis, et devant quelle borne.
         *
         * Déposée à chaque image, ce qui ne coûte rien — `ns_realtime_publish`
         * écrit trois flottants sous un verrou et rend la main. C'est le fil de
         * travail qui décide quand publier (4 Hz), et la boucle de jeu n'attend
         * jamais une requête. Sans temps réel actif, l'appel ne fait rien du
         * tout.
         *
         * On publie la borne DEVANT LAQUELLE on se tient, et pas seulement une
         * position : « Ada est à la borne Tetris » se lit, « Ada est en (3,2 ;
         * 0 ; 11,4) » ne se lit pas. C'est ce qui rend une salle vivante plutôt
         * que peuplée de coordonnées.
         */
        /*
         * LE FANTÔME À AFFRONTER, demandé en même temps que le billet.
         *
         * Même raisonnement que pour le billet : quand le joueur ARRIVE devant
         * la borne, il lui reste le temps d'avancer la main et d'insérer le
         * jeton — largement de quoi demander la liste des fantômes et
         * télécharger le meilleur. Attendre l'appui figerait la salle.
         *
         * On prend le PREMIER de la liste, c'est-à-dire le meilleur score. Pas
         * de menu de sélection : à une borne d'arcade on ne choisit pas son
         * adversaire dans une liste, on essaie de battre le meilleur.
         */
        if (ns_realtime_enabled() && !in_game && !duel.loaded) {
            const ns_cabinet *ahead2 = (cam.mode == ROOM_CAM_PLAYER)
                                     ? room_viewmodel_target(&scene, &cam) : NULL;
            if (ahead2 && ahead2->game[0]) {
                ns_realtime_request_ghosts(ahead2->game, ahead2->difficulty);
                ns_realtime_ghost_info best[NS_RT_MAX_GHOSTS];
                if (ns_realtime_ghosts(best, NS_RT_MAX_GHOSTS) > 0) {
                    ns_realtime_fetch_ghost(best[0].run_id);
                }
            }
        }
        if (ns_realtime_enabled()) duel_take(&duel);

        if (ns_realtime_enabled()) {
            const ns_cabinet *here = playing_cab;
            if (!here && cam.mode == ROOM_CAM_PLAYER) {
                here = room_viewmodel_target(&scene, &cam);
            }
            ns_realtime_publish(cam.position.x, cam.position.y, cam.position.z,
                                cam.yaw,
                                here ? here->name : "",
                                (in_game && game_api) ? game_api->id
                                                      : (here ? here->game : ""),
                                (in_game && game_api && game)
                                    ? (int32_t)game_api->score(game) : 0);
        }

        ns_clock_begin_frame(&clock);
        while (ns_clock_consume_tick(&clock)) {
            room_camera_tick(&cam, &scene.bvh, (float)clock.tick_seconds);

            /*
             * La descente du regard vers la dalle, APRÈS le pas de caméra : la
             * souris du joueur a déjà été intégrée, donc bouger la souris
             * pendant ces trois dixièmes de seconde n'est pas ignoré, seulement
             * ramené vers l'écran. Et quand `look_settle` tombe à zéro, plus
             * rien ne touche au tangage — la tête est rendue, entièrement.
             */
            if (look_settle > 0.0f) {
                const float dt = (float)clock.tick_seconds;
                const float k = ns_minf(1.0f, dt / look_settle);
                cam.pitch += (look_pitch - cam.pitch) * k;
                look_settle -= dt;
            }
            room_viewmodel_tick(&vmstate, &cam, (float)clock.tick_seconds);

            /*
             * Les mains se posent sur les commandes dès que la séquence du jeton
             * est finie — c'est-à-dire dès que la machine à états est retombée au
             * repos avec une partie en cours.
             *
             * Le raccordement est fait ici plutôt que dans la machine à états
             * parce que c'est `main` qui sait s'il y a une partie : le viewmodel
             * ne connaît ni Flappy ni le plein écran, et lui apprendre l'un ou
             * l'autre le lierait au jeu qu'il anime.
             */
            if (in_game && !fullscreen_game && playing_cab
                && vmstate.state == ROOM_VM_IDLE) {
                room_viewmodel_start_playing(&vmstate, playing_cab);
            }
            settings_banner = ns_maxf(0.0f, settings_banner - (float)clock.tick_seconds);
            room_sound_update(&sound, &scene, &cam, (float)clock.tick_seconds);
            ns_renderer_tick_particles(renderer, (float)clock.tick_seconds);

            /*
             * Le jeu avance au MÊME pas fixe que la salle. C'est ce qui le rend
             * reproductible à la graine près — donc scriptable pour les captures
             * — et c'est précisément ce que la version de 2020 ne pouvait pas
             * faire : elle intégrait en nombre d'images, à 60 Hz supposés.
             */
            room_menu_update(&menu, (float)clock.tick_seconds);
            if (in_game && !menu.open) {
                if (opt.autoplay && game_api->autopilot) game_api->autopilot(game);
                /* Les maintiens : un jeu qui tourne à l'angle (le serpent) a
                 * besoin de savoir qu'on tient la direction, pas qu'on l'a
                 * pressée. Ceux qui n'en veulent pas laissent le pointeur nul. */
                if (game_api->hold) {
                    bool held[NS_GAME_BUTTON_COUNT] = { false };
                    held[NS_GAME_UP]     = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
                    held[NS_GAME_DOWN]   = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
                    held[NS_GAME_LEFT]   = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
                    held[NS_GAME_RIGHT]  = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
                    held[NS_GAME_ACTION] = keys[SDL_SCANCODE_SPACE];
                    game_api->hold(game, held);
                }

                /*
                 * Le journal d'ENTRÉES, écrit ici parce que c'est le seul
                 * endroit où l'on sait ce que le jeu a réellement reçu à ce pas.
                 *
                 * Il ne sert pas au serveur — le sceau ne le couvre pas — mais
                 * il rend une partie REPRODUCTIBLE : `--journal-entrees=` la
                 * dépose dans un fichier, et un rapport de bug cesse d'être
                 * « ça a planté après deux minutes » pour devenir quelque chose
                 * qu'on rejoue. `tests/test_replay.c` vérifie que les huit jeux
                 * se rejouent à l'identique par ce chemin.
                 */
                {
                    uint8_t hmask = 0;
                    if (game_api->hold) {
                        if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) hmask |= 1u << NS_GAME_UP;
                        if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) hmask |= 1u << NS_GAME_DOWN;
                        if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) hmask |= 1u << NS_GAME_LEFT;
                        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) hmask |= 1u << NS_GAME_RIGHT;
                        if (keys[SDL_SCANCODE_SPACE]) hmask |= 1u << NS_GAME_ACTION;
                    }
                    ns_runlog_input(runlog, run_tick, hmask, pending_press);
                    pending_press = 0;

                    /*
                     * L'ADVERSAIRE avance du même pas, au même moment.
                     *
                     * C'est ici et nulle part ailleurs : un duel se synchronise
                     * sur des NUMÉROS DE PAS, jamais sur des secondes. Les deux
                     * parties partagent la même graine et le même pas fixe, donc
                     * elles restent comparables sans qu'aucune horloge n'entre
                     * en jeu — et c'est ce qui rend le duel exact plutôt
                     * qu'approximatif.
                     */
                    duel_tick(&duel, game_api, run_tick, (float)clock.tick_seconds);
                    run_tick++;
                }

                game_api->tick(game, (float)clock.tick_seconds);
                /*
                 * L'horloge de la partie est celle de la SIMULATION, pas celle
                 * du mur : elle avance d'un pas fixe. C'est ce qui rend le
                 * journal rejouable — le serveur revit la partie avec la même
                 * graine et les mêmes instants, et retrouve le même score.
                 */
                run_ms += (int64_t)(clock.tick_seconds * 1000.0 + 0.5);

                ns_game_events gev; SDL_zero(gev);
                game_api->events(game, &gev);
                if (gev.blip) {
                    ns_audio_play(sfx_blip, NS_BUS_SFX, 0.55f, 1.0f);
                    /* Le jeu ne connaît pas les bras, et c'est voulu : il ne
                     * publie qu'un événement, et c'est ici qu'on le relaie à
                     * l'index droit. Le même événement sert déjà au son. */
                    room_viewmodel_tap(&vmstate);
                    ns_runlog_event(runlog, run_ms, gev.blip_kind, 0);
                }
                if (gev.score) {
                    ns_audio_play(sfx_score, NS_BUS_SFX, 0.7f, 1.0f);
                    NS_INFO("%s : %u", game_api->title, game_api->score(game));
                    ns_runlog_event(runlog, run_ms, gev.score_kind, gev.score_value);
                }
                if (gev.die) {
                    ns_audio_play(sfx_die, NS_BUS_SFX, 0.8f, 1.0f);
                    last_rank = finish_run(runlog, run_ms, game_api, game,
                                           game_hard ? "hard" : "normal",
                                           opt.player, opt.offline, opt.autoplay);
                    /* La partie est finie : c'est le moment où son journal
                     * d'entrées est complet. L'écrire plus tôt donnerait une
                     * partie tronquée, plus tard une partie déjà relancée. */
                    if (opt.input_log) {
                        ns_runlog_write_inputs(runlog, opt.input_log);
                    }

                    /*
                     * ET ON DEVIENT LE FANTÔME DE QUELQU'UN D'AUTRE.
                     *
                     * Le journal d'entrées de la partie qui vient de finir est
                     * déposé sur le serveur, rattaché à la partie que celui-ci a
                     * lui-même ouverte et dont il vient de recalculer le score.
                     *
                     * Ce dépôt ne crée aucun score et n'ouvre aucune porte : le
                     * serveur refuse un journal qui ne se rattache pas à une
                     * partie validée, et il ne le rejoue pas. Au pire on dépose
                     * des appuis qui ne reproduisent rien, et le seul perdant est
                     * celui qui croyait avoir enregistré son fantôme.
                     *
                     * Sans temps réel, sans jeton, ou hors ligne, l'appel ne fait
                     * rien — comme tout le reste de ce fichier.
                     */
                    if (ns_realtime_enabled() && !opt.offline) {
                        const char *rid = ns_runlog_run_id(runlog);
                        if (rid && rid[0] && ns_runlog_input_count(runlog) > 0) {
                            const size_t need = ns_runlog_format_inputs(runlog, NULL, 0) + 1;
                            char *txt = (char *)SDL_malloc(need);
                            if (txt) {
                                const size_t len = ns_runlog_format_inputs(runlog, txt, need);
                                ns_realtime_publish_ghost(rid, txt, len);
                                SDL_free(txt);
                            }
                        }
                    }
                }

                /*
                 * En pilote automatique, la partie REPART toute seule.
                 *
                 * Sans ça, `--autoplay` jouait une partie et laissait ensuite un
                 * écran de fin figé jusqu'à la fermeture — une borne morte, ce
                 * qui est l'exact contraire de l'ambiance qu'on cherche : une
                 * salle d'arcade, c'est des écrans qui bougent tout seuls. La
                 * temporisation est la même que pour un humain (`dead_time`),
                 * pour qu'on ait le temps de LIRE le score avant que ça reparte.
                 *
                 * C'est aussi ce qui rend la chaîne en ligne observable sans
                 * joueur : la première partie d'un processus démarre forcément
                 * sans billet — une partie n'attend pas le réseau — et c'est la
                 * deuxième qui en porte un.
                 */
                if (opt.autoplay) {
                    float dead_time = 0.0f;
                    if (game_api->dead(game, &dead_time) && dead_time > 1.5f) {
                        demo_seed = demo_seed * 6364136223846793005ull
                                  + 1442695040888963407ull;
                        start_run(game_api, game, runlog, demo_seed, game_hard, &duel);
                        run_ms = 0;
                        run_tick = 0;
                        pending_press = 0;
                    }
                }
            }
        }
        ns_clock_end_frame(&clock);

        const double now = ns_time_seconds();
        ns_scene_animate_lights(&scene, now);

        if (ns_rhi_begin_frame(rhi)) {
            uint32_t w = 0, h = 0;
            ns_rhi_drawable_size(rhi, &w, &h);

            SDL_GPUTexture *target = ns_rhi_swapchain_texture(rhi);

            /* En headless, il n'y a pas de swapchain : on rend dans une texture
             * hors écran, créée à la demande, qui sera relue pour la capture. */
            static ns_texture offscreen;
            if (!target) {
                if (!offscreen.handle || offscreen.width != w || offscreen.height != h) {
                    ns_texture_destroy(rhi, &offscreen);
                    ns_texture_desc td;
                    SDL_zero(td);
                    td.width = w; td.height = h;
                    td.format = ns_rhi_swapchain_format(rhi);
                    td.render_target = true;
                    td.sampled = true;
                    td.name = "cible hors écran";
                    if (!ns_texture_create(rhi, &offscreen, &td)) {
                        NS_ERROR("cible hors écran indisponible");
                        ns_rhi_end_frame(rhi);
                        break;
                    }
                }
                target = offscreen.handle;
            }

            const double frame_start = opt.bench ? ns_time_seconds() : 0.0;

            /*
             * L'écran vivant : le jeu est rendu dans la dalle AVANT la scène,
             * puis la scène le dessine comme n'importe quelle surface — avec son
             * émissif, sa courbure et le reflet de la vitre.
             *
             * L'ordre compte : la passe géométrique lit cette texture, donc elle
             * doit être finie. Deux passes de rendu dans la même image, la
             * première écrivant ce que la seconde échantillonne.
             */
            ns_renderer_set_screen(renderer, -1, NULL);   /* on repart à zéro */

            /*
             * La borne de CLASSEMENT, toujours vivante.
             *
             * Elle affichait `leaderboard_font.jpg`, une image de 2020 avec des
             * scores peints dessus. Un score qu'on ne peut pas comparer ne donne
             * envie de rien ; les vrais, sur une borne qu'on croise en entrant,
             * donnent envie de reprendre la main. C'est toute la raison d'être
             * du classement local.
             */
            if (sprites && board_rt.handle && board_material >= 0) {
                ns_sprite_begin(sprites, 512.0f, 288.0f);
                room_hud_draw_leaderboard(sprites, 512.0f, 288.0f, now);
                static const float off[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                ns_sprite_end(rhi, sprites, board_rt.handle, 512, 288, off);
                ns_renderer_set_screen(renderer, board_material, board_rt.handle);
                /* Le même échappatoire que pour la dalle de jeu : regarder ce
                 * qui est REELLEMENT dessiné, sans la courbure ni le cadre. */
                if (SDL_getenv("NINETEEN_DUMP_BOARD")) {
                    ns_rhi_capture_texture_png(rhi, board_rt.handle, 512, 288,
                                               ns_rhi_swapchain_format(rhi),
                                               SDL_getenv("NINETEEN_DUMP_BOARD"));
                }
            }

            if (in_game && !fullscreen_game && sprites && screen_rt.handle) {
                ns_sprite_begin(sprites, 512.0f, 288.0f);
                game_api->draw(sprites, game, game_art, 512.0f, 288.0f);
                static const float off[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                ns_sprite_end(rhi, sprites, screen_rt.handle, 512, 288, off);
                ns_renderer_set_screen(renderer, playing_material, screen_rt.handle);
                if (SDL_getenv("NINETEEN_DUMP_SCREEN")) {
                    ns_rhi_capture_texture_png(rhi, screen_rt.handle, 512, 288,
                                               ns_rhi_swapchain_format(rhi),
                                               SDL_getenv("NINETEEN_DUMP_SCREEN"));
                }
            }

            if (in_game && fullscreen_game && sprites) {
                /*
                 * En partie, le jeu occupe l'écran. Le repère logique est fixé à
                 * la hauteur du terrain (1080) et la largeur suit l'aspect de la
                 * fenêtre : le jeu garde donc ses proportions sur un 16/9 comme
                 * sur un 4/3, et c'est lui qui se centre.
                 */
                const float aspect = (float)w / (float)ns_maxf(1.0f, (float)h);
                /* 1080 de haut : le repère logique des jeux de 2020, à l'échelle
                 * 4 comme le faisait leur `SCALE_TO_FIT`. La largeur suit
                 * l'aspect de la fenêtre, donc le jeu garde ses proportions sur
                 * un 16/9 comme sur un 4/3, et c'est lui qui se centre. */
                const float logical_h = 1080.0f;
                ns_sprite_begin(sprites, logical_h * aspect, logical_h);
                game_api->draw(sprites, game, game_art, logical_h * aspect, logical_h);
                static const float night[4] = { 0.02f, 0.02f, 0.03f, 1.0f };
                ns_sprite_end(rhi, sprites, target, w, h, night);
                ns_rhi_end_frame(rhi);
                frames_rendered++;
                /*
                 * `--frames=` s'arrête, capture ou pas.
                 *
                 * Ce chemin-ci — le mini-jeu en plein écran — ne s'arrêtait
                 * QUE si `--screenshot` était demandé, et il fait `continue`
                 * juste après, donc il sautait aussi le compteur d'images
                 * générique de la boucle principale. Résultat :
                 * `--headless --game=flappy --frames=2` jouait indéfiniment,
                 * et il fallait tuer le processus. Une option qui compte des
                 * images doit compter les images ; qu'on en fasse une PNG est
                 * une autre question.
                 */
                if (frames_rendered >= opt.frames && (opt.screenshot || opt.headless)) {
                    if (opt.screenshot) {
                        ns_rhi_capture_texture_png(rhi, target, w, h,
                                                   ns_rhi_swapchain_format(rhi), opt.screenshot);
                        NS_INFO("capture : %s, score %u, %u quads en %u lot(s)",
                                game_api->title, game_api->score(game),
                                ns_sprite_quad_count(sprites),
                                ns_sprite_batch_count(sprites));
                    }
                    ns_texture_destroy(rhi, &offscreen);
                    running = false;
                }
                continue;
            }

            const ns_camera render_cam = room_camera_resolve(&cam, (float)clock.alpha);
            /* Les bras : posés par room_viewmodel, jamais en caméra libre. */
            room_viewmodel_pose(&vmstate, &cam, (float)clock.alpha, &viewmodel);
            const ns_viewmodel_pose *vm = (cam.mode == ROOM_CAM_PLAYER) ? &viewmodel : NULL;
            if (!ns_renderer_draw(rhi, renderer, &scene, &render_cam, vm, target, w, h, now)) {
                /*
                 * Le rendu n'a rien écrit — cibles indisponibles, typiquement au
                 * milieu d'un redimensionnement. On ABANDONNE l'image plutôt que
                 * de présenter une swapchain vide, qui s'affiche noire. C'est la
                 * moitié visible des « flashs noirs ».
                 */
                ns_rhi_cancel_frame(rhi);
                continue;
            }

            /*
             * L'affichage, PAR-DESSUS la scène et après elle.
             *
             * Il n'entre ni dans la profondeur, ni dans le halo, ni dans le tone
             * mapping : un texte qui fleurit devient illisible, et c'est la
             * première chose qui doit rester lisible. C'est aussi pour ça qu'il
             * est dessiné ici et pas dans la cible HDR.
             */
            if (sprites && !opt.no_hud) {
                room_hud_state hud;
                SDL_zero(hud);
                hud.near = (cam.mode == ROOM_CAM_PLAYER)
                         ? room_viewmodel_target(&scene, &cam) : NULL;
                hud.can_interact = !room_viewmodel_is_playing(&vmstate)
                                && vmstate.state == ROOM_VM_IDLE;
                hud.playing = in_game;
                hud.score = in_game ? game_api->score(game) : 0u;
                hud.best  = in_game ? game_api->best(game) : 0u;
                hud.dead  = in_game && game_api->dead(game, NULL);
                hud.settings_timer = settings_banner;
                hud.quality_name = quality_name(rs.quality);
                hud.render_scale = rs.render_scale;
                hud.last_rank = last_rank;

                ns_sprite_begin(sprites, ROOM_HUD_W, ROOM_HUD_H);
                room_hud_draw(sprites, &hud);
                /*
                 * Les autres joueurs, quand le temps réel est actif. Dessinés
                 * APRÈS l'affichage de la borne et AVANT le menu : une plaque
                 * ne doit pas passer par-dessus des réglages qu'on est en train
                 * de lire.
                 *
                 * Les plaques sont dessinées dans TOUS les modes de caméra. Un
                 * premier jet les réservait au mode joueur, pour que les
                 * captures d'architecture n'aient pas d'étiquettes qui flottent
                 * — mais c'est `--no-hud` qui répond déjà à ce besoin, et il
                 * englobe tout ce bloc. La restriction ne protégeait donc rien
                 * et privait la caméra libre (F5) de ce que le mode joueur
                 * montre : on reste dans la même salle, avec les mêmes gens.
                 *
                 * Seul le plein écran d'un mini-jeu les masque : la salle n'y
                 * est plus visible, donc une plaque posée dans la salle n'aurait
                 * plus rien à désigner.
                 */
                if (ns_realtime_enabled()) {
                    if (!fullscreen_game) {
                        draw_presence(sprites, &render_cam,
                                      (h > 0) ? (float)w / (float)h : 1.777f);
                    }
                    draw_presence_roster(sprites);
                    /* Le tableau du duel, quand il y en a un : son score, le
                     * sien, et lequel des deux est devant. */
                    if (in_game && duel.running) {
                        draw_duel(sprites, &duel, game_api, game_api->score(game));
                    }
                }
                room_menu_draw(sprites, &menu, &menu_ctx);
                ns_sprite_end(rhi, sprites, target, w, h, NULL);
            }

            ns_rhi_end_frame(rhi);
            frames_rendered++;

            /*
             * Mesure. L'attente est INDISPENSABLE : sans elle on chronomètre
             * l'enregistrement des commandes, pas leur exécution — c'est ainsi
             * qu'un rendu à une image par seconde a pu être annoncé à 1 793.
             * La première image est écartée : elle porte la compilation des
             * pipelines par le pilote.
             */
            if (opt.bench) {
                ns_rhi_wait_idle(rhi);
                const double ms = (ns_time_seconds() - frame_start) * 1000.0;
                if (frames_rendered > 1) {
                    bench_total += ms;
                    bench_count++;
                    if (ms < bench_min) bench_min = ms;
                    if (ms > bench_max) bench_max = ms;
                }
            }

            if (opt.screenshot && frames_rendered >= opt.frames) {
                ns_rhi_capture_texture_png(rhi, target, w, h, ns_rhi_swapchain_format(rhi),
                                           opt.screenshot);
                const ns_render_stats st = ns_renderer_stats(renderer);
                NS_INFO("capture : %u lots dessinés, %u éliminés, %u triangles, %u lumières",
                        st.batches_drawn, st.batches_culled, st.triangles, st.lights_active);
            }

            /*
             * En headless, `--frames` est une LIMITE — pour tout le monde.
             *
             * Elle ne l'était que pour `--screenshot` et `--bench`. Sans l'un des
             * deux, `--headless --frames=3` rendait indéfiniment et finissait par
             * mourir sur SIGSEGV en ayant l'air d'avoir planté tout seul. C'est
             * aussi ce qui empêchait de vérifier quoi que ce soit sur le chemin
             * de SORTIE — les réglages gardés, le classement écrit, les fuites
             * annoncées : le programme n'y arrivait jamais.
             *
             * Ce que ça ne règle PAS, et il faut le dire : dans ce conteneur,
             * une session headless longue meurt vers la trentième image, quelle
             * que soit la valeur de `--frames` et quel que soit le palier de
             * qualité — `low` compris. La pile est ENTIÈREMENT dans
             * `libvulkan_lvp.so` (un memset sur un pointeur nul, sur un fil du
             * pilote), avec 15 Gio de mémoire libre et 2,8 Gio de RSS. Aucune
             * image du moteur n'y figure, et le moteur ne crée aucun objet GPU
             * par image : tampons de transfert créés et relâchés dans la même
             * fonction, pipelines et cibles créés à l'initialisation ou au
             * redimensionnement.
             *
             * Ce qui est donc établi : le rastériseur logiciel de ce conteneur
             * ne tient pas une longue session. Ce qui ne l'est PAS : que la
             * même chose n'arrive jamais sur un vrai GPU. Je n'en ai pas ici, je
             * ne peux pas le savoir, et je ne le prétends pas. Les captures
             * courtes (4 à 8 images) passent, c'est ce dont elles ont besoin.
             */
            if (opt.headless && frames_rendered >= opt.frames) {
                ns_texture_destroy(rhi, &offscreen);
                running = false;
            }
        }
    }

    if (opt.bench && bench_count > 0) {
        const double avg = bench_total / (double)bench_count;
        NS_INFO("mesure GPU sur %d images : %.1f ms en moyenne (%.1f images/s), "
                "min %.1f ms, max %.1f ms",
                bench_count, avg, 1000.0 / avg, bench_min, bench_max);
    } else {
        NS_INFO("arrêt après %d images (%.1f images/s en moyenne — sans attente GPU, "
                "ce chiffre ne mesure QUE l'enregistrement des commandes ; employer --bench)",
                frames_rendered, clock.fps_smoothed);
    }

    ns_texture_destroy(rhi, &screen_rt);
    /* `board_rt` n'était pas détruite. Une seule texture, libérée par le pilote
     * à la sortie du processus — mais c'est exactement la fuite qui s'installe :
     * elle est arrivée avec la borne de classement et personne ne l'a vue, parce
     * qu'on ne regarde le rapport d'ASan que quand on le cherche. */
    ns_texture_destroy(rhi, &board_rt);
    if (game_api) {
        if (game_art) game_api->art_free(rhi, game_art);
        SDL_free(game_art);
        SDL_free(game);
    }

    /* Les réglages suivent le joueur d'une session à l'autre. Le MÊME chemin que
     * la fermeture du menu : deux écritures parallèles finiraient par diverger,
     * et celle-ci ne gardait ni les volumes ni la sensibilité de la souris.
     * `ns_config_save` n'écrit que si quelque chose a changé. */
    room_menu_persist(&menu_ctx);
    ns_config_save();

    ns_scores_save();
    /* Le temps réel s'arrête AVANT le classement : il emprunte l'URL et le
     * jeton de `ns_online`, et son fil envoie un dernier « je m'en vais » pour
     * ne pas hanter la salle pendant la durée du TTL. */
    duel_release(&duel);
    SDL_free(duel.state);
    ns_realtime_shutdown();
    ns_online_shutdown();
    /*
     * LE JOURNAL D'ENTRÉES S'ÉCRIT AUSSI À LA SORTIE, et pas seulement quand la
     * partie se termine.
     *
     * Le premier jet ne l'écrivait qu'à la mort du joueur — et c'est
     * exactement l'inverse du besoin. On veut ce fichier quand quelque chose a
     * mal tourné : un plantage, une fermeture, une partie qu'on interrompt
     * parce qu'elle fait n'importe quoi. Dans tous ces cas la partie n'est
     * jamais « terminée », et le premier jet ne produisait rien.
     *
     * `ns_runlog_write_inputs` est idempotente : réécrire un journal déjà
     * écrit à la mort ne coûte qu'un fichier identique.
     */
    if (opt.input_log && ns_runlog_input_count(runlog) > 0) {
        ns_runlog_write_inputs(runlog, opt.input_log);
    }
    ns_runlog_destroy(runlog);
    if (sprites) ns_sprite_destroy(rhi, sprites);
    room_sound_shutdown(&sound);
    ns_audio_shutdown();
    ns_scene_unload(rhi, &scene);
    ns_renderer_destroy(rhi, renderer);
    ns_rhi_destroy(rhi);

    ns_config_save();
    ns_config_shutdown();

    /* Un bloc non libéré au dernier instant n'est pas grave, mais le signaler
     * pendant le développement évite qu'une fuite s'installe. */
    const size_t leaked = ns_alloc_live_bytes();
    if (leaked > 0) NS_WARN("%zu octets encore alloués à la sortie (%zu blocs)",
                            leaked, ns_alloc_live_blocks());

    ns_paths_shutdown();
    ns_log_close_file();
    SDL_Quit();
    return 0;
}
