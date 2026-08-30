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
#include "ns_lockstep.h"
#include "ns_runlog.h"
#include "ns_scores.h"

#include "room_camera.h"
#include "room_door.h"
#include "ns_env.h"
#include "ns_skin.h"
#include "room_attract.h"
#include "room_hud.h"
#include "room_menu.h"
#include "room_pad.h"
#include "room_presence.h"
#include "room_sound.h"
#include "room_viewmodel.h"
#include "room_poste.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * L'adresse du serveur CUITE AU BUILD, par `-DNINETEEN_SERVER_URL=…`.
 *
 * Vide quand personne n'a rien demandé — voir le bloc qui l'explique dans
 * `room/CMakeLists.txt` : un dépôt bâti tel quel n'ouvre aucune socket. Le repli
 * ci-dessous existe pour que ce fichier compile aussi hors de son CMakeLists
 * (un outil d'analyse, un `compile_commands.json` incomplet), et il vaut le même
 * vide : il ne peut donc pas changer le comportement.
 */
#ifndef NINETEEN_SERVER_URL
#define NINETEEN_SERVER_URL ""
#endif

typedef struct options {
    bool        headless;
    const char *env_path;   /* --env= : le fichier de reglages du personnage */
    const char *screenshot;
    /*
     * --sequence= : le même rendu que --screenshot, mais une image PAR image,
     * numérotée, pour qu'ffmpeg en fasse un film.
     *
     * Ce que ça remplace : filmer en relançant le binaire par image, avec
     * --pos=/--yaw= incrémentés. Mesuré ici, un lancement coûte 2,2 s de
     * chargement d'assets pour 0,1 s de rendu ; 240 images auraient donc pris
     * neuf minutes de chargement pour vingt secondes de rendu. Surtout, ça ne
     * peut pas filmer une PARTIE : chaque processus repart d'un état neuf, donc
     * rien ne bouge à l'écran d'une image à l'autre sauf la caméra.
     */
    const char *sequence;
    double      sequence_fps;
    int         frames;
    int         width, height;
    bool        fullscreen;
    bool        vsync;
    bool        debug_gpu;
    ns_quality  quality;
    room_camera_mode camera_mode;
    bool        camera_set;  /* --camera= a été donné : voir le point de vue */
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
    /*
     * COMBIEN DE PAIRS FABRIQUÉS, sans serveur ni socket. Zéro — le défaut —
     * n'en fabrique aucun, et le code de démonstration ne s'exécute pas.
     *
     * Il faut ça pour REGARDER les corps des autres joueurs : les voir demande
     * trois ou quatre personnes connectées à la même seconde, ce qu'aucune
     * capture et aucune machine d'intégration continue ne peut réunir. Les
     * échantillons entrent par le même chemin que ceux du réseau — voir
     * `room_presence_demo` — donc ce qui est photographié est bien ce qui sera
     * rendu, et non un rendu de démonstration à côté.
     *
     * Il n'ouvre RIEN : il ne lève aucun des deux verrous du temps réel, et
     * `ns_realtime` ne sait pas qu'il existe.
     */
    int         demo_peers;
    bool        quality_set; /* la ligne de commande a tranché : ne pas relire la config */
    bool        no_hud;      /* captures d'architecture : la scène sans un pixel de texte */
    const char *server;      /* --server= : le plus fort des quatre niveaux */
    /*
     * `NINETEEN_SERVER_URL` lue au LANCEMENT, gardée à part de `server` parce
     * qu'elle ne pèse pas le même poids : elle bat la config, et `--server=` la
     * bat. Les confondre reviendrait à ne plus pouvoir dire au journal laquelle
     * a gagné, ce qui est tout l'objet de `ns_online_resolve_url`.
     *
     * C'est elle qui rend le jeu utilisable dans un conteneur ou derrière un
     * `docker compose` SANS RECOMPILER — le défaut compilé, lui, demande une
     * compilation par serveur.
     */
    const char *server_env;
    const char *input_log;   /* où déposer le journal d'entrées de la partie */
    const char *replay;      /* un journal d'entrées à rejouer, sans fenêtre */
    const char *duel_live;   /* « hôte:port,identifiant,place » : duel EN DIRECT */
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
        "  --env=CHEMIN         fichier de réglages (défaut : nineteen.env,\n"
        "                       cherché dans le dossier courant, puis à côté du\n"
        "                       binaire, puis dans les assets)\n"
        "  --screenshot=CHEMIN  écrit une capture PNG puis quitte\n"
        "  --sequence=PREFIXE   écrit PREFIXE0000.png, PREFIXE0001.png… une par\n"
        "                       image rendue : de quoi monter un film. Le temps\n"
        "                       avance alors d'un pas FIXE par image, donc la\n"
        "                       vitesse du film ne dépend pas de la machine\n"
        "  --sequence-fps=F     ce pas fixe, en images par seconde (défaut 30)\n"
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
        "  --duel-direct=SPEC   duel EN DIRECT contre un adversaire, via un relais.\n"
        "                       SPEC vaut « hôte:port,identifiant,place » — la place\n"
        "                       est 0 ou 1, et les deux joueurs donnent le même\n"
        "                       identifiant. À employer avec --game=. Inerte sans.\n"
        "  --rejouer=F          rejoue le journal d'entrées F et imprime le score,\n"
        "                       sans fenêtre ni GPU : c'est ce qui rend un rapport\n"
        "                       de bug reproductible\n"
        "  --journal-entrees=F  écrit les commandes de la partie dans F : une partie\n"
        "                       redevient reproductible, et un rapport de bug devient\n"
        "                       un fichier plutôt qu'un souvenir\n"
        "  --server=URL         classement en ligne (http://hôte:port). C'est le plus\n"
        "                       fort de quatre niveaux ; du plus faible au plus fort :\n"
        "                       défaut compilé (-DNINETEEN_SERVER_URL au build) < config\n"
        "                       < variable NINETEEN_SERVER_URL < cette option. Le\n"
        "                       journal de démarrage DIT lequel a gagné. Les quatre\n"
        "                       vides — le défaut — n'ouvrent aucune socket\n"
        "  --offline            verrou : aucune connexion, aucune mise en file\n"
        "  --temps-reel         présence dans la salle et duels (INERTE par défaut) ;\n"
        "                       demande un serveur, et se règle aussi dans Échap\n"
        "  --no-temps-reel      force l'inverse, quel que soit le réglage gardé\n"
        "  --pairs-demo=N       peuple l'allée de N marcheurs FABRIQUÉS (0 à 16),\n"
        "                       sans serveur ni la moindre socket : c'est ce qui\n"
        "                       permet de photographier des corps qui marchent.\n"
        "                       INERTE par défaut (N = 0)\n"
        "  --menu[=N]           ouvre le menu de réglages (ligne N) : pour les captures\n"
        "  --no-hud             pas d'affichage : la scène seule, pour les captures\n"
        "\n"
        "  En jeu : Échap réglages, F5 caméra libre, F6 orbite,\n"
        "           F7 palier de qualité, F8 échelle de rendu, F2 capture,\n"
        "           F cogner la borne devant soi.\n"
        "  --nom=NOM            nom porté au classement local\n"
        "  --debug=VUE          affiche une cible intermédiaire : albedo, normal,\n"
        "                       emissive, depth, visibility, hdr, bloom\n"
        "  --bench              mesure le temps GPU réel de chaque image\n"
        "  --game=NOM           démarre directement dans un mini-jeu\n"
        "  --autoplay           le mini-jeu se joue tout seul (captures, CI)\n"
        "  --warmup=S           avance le mini-jeu de S secondes avant de rendre\n"
        "  --play-at=BORNE      se place devant la borne nommée et lance sa partie,\n"
        "                       en restant EN 3D : le jeu tourne dans sa dalle\n"
        "  --pose=NOM           fige les bras : idle, walk, reach, insert,\n"
        "                       press, frappe\n"
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
    /*
     * L'adresse du serveur. Elle ne s'applique PAS ici : elle est mise de côté,
     * et `ns_online_resolve_url` l'arbitre plus bas contre les trois autres
     * sources. C'est la différence avec les réglages ci-dessus, qui n'en ont
     * que deux et peuvent donc s'écraser sur place.
     */
    if ((v = SDL_getenv("NINETEEN_SERVER_URL")) != NULL) o->server_env = v;
}

static bool parse_options(int argc, char **argv, options *o)
{
    SDL_zerop(o);
    o->frames = 4;
    /* ZERO veut dire « pas demande », et non « 1600 ».
     *
     * Ces deux champs valaient le defaut, et plus bas on lisait « l'option
     * a-t-elle ete donnee ? » en COMPARANT au defaut. Consequence mesuree :
     * `--width=1600 --height=900` — la definition que `--help` annonce — etait
     * prise pour une absence d'option, et la configuration gardee l'emportait ;
     * la fenetre sortait en 1280x720. Pire, les deux axes decidant separement,
     * `--width=1600 --height=1080` rendait 2560x1080 : un rapport d'image faux,
     * en silence. Un defaut ne peut pas servir de sentinelle des lors qu'il est
     * aussi une valeur qu'on peut vouloir. */
    o->width = 0;
    o->height = 0;
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
        } else if (SDL_strncmp(a, "--env=", 6) == 0) {
            o->env_path = a + 6;
        } else if (SDL_strncmp(a, "--screenshot=", 13) == 0) {
            o->screenshot = a + 13;
        } else if (SDL_strncmp(a, "--sequence=", 11) == 0) {
            o->sequence = a + 11;
        } else if (SDL_strncmp(a, "--sequence-fps=", 15) == 0) {
            o->sequence_fps = SDL_atof(a + 15);
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
        } else if (SDL_strncmp(a, "--duel-direct=", 14) == 0) {
            o->duel_live = a + 14;
        } else if (SDL_strncmp(a, "--rejouer=", 10) == 0) {
            o->replay = a + 10;
        } else if (SDL_strncmp(a, "--server=", 9) == 0) {
            o->server = a + 9;
        } else if (SDL_strcmp(a, "--temps-reel") == 0) {
            o->realtime = 1;
        } else if (SDL_strcmp(a, "--no-temps-reel") == 0) {
            o->realtime = 0;
        } else if (SDL_strncmp(a, "--pairs-demo=", 13) == 0) {
            /* Borné à ce que la présence sait rapporter : au-delà, les corps
             * supplémentaires seraient rejetés plus bas sans rien dire. */
            const int n = SDL_atoi(a + 13);
            o->demo_peers = (n < 0) ? 0 : (n > NS_RT_MAX_PEERS ? NS_RT_MAX_PEERS : n);
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
            o->camera_set = true;
        } else if (SDL_strncmp(a, "--angle=", 8) == 0) {
            o->camera_angle = (float)SDL_atof(a + 8) * NS_DEG2RAD;
        } else {
            fprintf(stderr, "option inconnue : %s\n", a);
            print_usage(argv[0]);
            return false;
        }
    }

    /* Zero est la sentinelle « pas demande » — voir `parse_options` — et n'a donc
     * pas a passer ce controle. On refuse ce qui a ete DEMANDE et qui est
     * absurde, pas ce qui n'a pas ete demande du tout. */
    if ((o->width != 0 && o->width < 64) || (o->height != 0 && o->height < 64)) {
        fprintf(stderr, "résolution trop petite\n");
        return false;
    }
    if (o->frames < 1) o->frames = 1;

    /*
     * Le pas de la séquence est BORNÉ des deux côtés, et refusé plutôt que
     * corrigé en silence : une faute de frappe sur `--sequence-fps=` produirait
     * sinon un film au ralenti ou en accéléré, qui a l'air d'un défaut du jeu.
     */
    if (o->sequence != NULL) {
        if (o->sequence_fps == 0.0) o->sequence_fps = 30.0;
        if (o->sequence_fps < 1.0 || o->sequence_fps > 240.0) {
            fprintf(stderr, "--sequence-fps doit tenir entre 1 et 240\n");
            return false;
        }
    }
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
/*
 * L'ARBRE DE BUILD N'EXISTE QUE POUR CELUI QUI CONSTRUIT.
 *
 * `NINETEEN_BUILD_ASSET_DIR` est un chemin ABSOLU gravé dans le binaire à la
 * compilation, et il passe devant les assets posés à côté de l'exécutable. En
 * développement c'est exactement ce qu'on veut : on relance sans réinstaller.
 * Dans un PAQUET, c'est un piège à deux détentes.
 *
 * La première est un faux positif de recette, et on vient de la subir : le
 * paquet 17.0.0 a été déballé et lancé sur la machine de construction, il a
 * démarré, il a affiché sa salle — et il l'avait lue dans
 * `.../build/macos-universal/assets/`. Le paquet n'était donc PAS éprouvé, et
 * l'essai qui devait le prouver ne prouvait rien. C'est la même famille de
 * défaut que la version précédente, où `models/` et `nineteen.env` manquaient à
 * l'installation sans que personne le voie.
 *
 * La seconde est un vrai défaut chez le joueur, plus rare et plus vicieux : si
 * ce chemin existe sur SA machine — un développeur, un ancien arbre resté là —
 * le jeu installé lit des assets périmés, en silence.
 *
 * La règle : des assets À CÔTÉ du binaire, c'est une installation, et une
 * installation se suffit. On ne monte alors PAS l'arbre de build. L'arbre de
 * build, lui, n'a pas de `bin/assets` — ses assets sont un cran plus haut — donc
 * le développeur garde son montage. `$NINETEEN_ASSETS` reste prioritaire dans
 * les deux cas : c'est une surcharge demandée explicitement.
 */
static bool assets_beside_binary(void)
{
    const char *base = SDL_GetBasePath();
    if (!base) return false;
    char probe[512];
    SDL_snprintf(probe, sizeof probe, "%sassets/scene", base);
    SDL_PathInfo info;
    return SDL_GetPathInfo(probe, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static void mount_asset_directories(void)
{
    const char *env = SDL_getenv("NINETEEN_ASSETS");
    if (env && *env) ns_paths_mount(env);
#ifdef NINETEEN_BUILD_ASSET_DIR
    if (assets_beside_binary()) {
        NS_INFO("assets trouvés à côté du binaire : l'arbre de build n'est pas "
                "monté (c'est une installation, elle se suffit)");
    } else {
        ns_paths_mount(NINETEEN_BUILD_ASSET_DIR);
    }
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

/*
 * Une image de la séquence, numérotée.
 *
 * Le numéro est sur QUATRE chiffres et part de zéro parce que c'est ce que
 * `ffmpeg -i PREFIXE%04d.png` lit sans autre réglage. Passé 9999 images — cinq
 * minutes à 30 im/s — le format déborderait ; on s'arrête plutôt que d'écraser
 * silencieusement la première image, parce qu'un film qui reboucle au milieu
 * ressemble à un défaut d'encodage et se cherche longtemps.
 */
static bool write_sequence_frame(ns_rhi *rhi, SDL_GPUTexture *target, uint32_t w, uint32_t h,
                                 const char *prefix, int index)
{
    if (index > 9999) {
        NS_ERROR("séquence : 10000 images atteintes, le numéro déborderait — arrêt");
        return false;
    }
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s%04d.png", prefix, index);
    return ns_rhi_capture_texture_png(rhi, target, w, h, ns_rhi_swapchain_format(rhi), path);
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

    /*
     * La liaison d'un duel EN DIRECT, ou NULL.
     *
     * Le fantôme différé et le duel en direct partagent tout sauf la SOURCE des
     * entrées de l'adversaire : un journal téléchargé dans un cas, une socket
     * dans l'autre. Le reste — un second état de jeu avancé du même pas que le
     * sien, le tableau qui compare les deux scores — est identique, et c'est ce
     * qui a permis de livrer le direct sans réécrire le duel.
     */
    ns_lockstep *live;
    int32_t      live_stall;     /* pas passés à attendre l'adversaire */
    /*
     * Le dernier pas du FANTÔME déjà confronté à l'empreinte du pair.
     *
     * Il existe parce que le fantôme et nous n'avançons PAS du même nombre de
     * pas : quand l'entrée de l'adversaire n'est pas encore arrivée,
     * `duel_tick` rend la main sans rejouer — ce qui est exactement ce qu'il
     * doit faire. Sans mémoire de ce qu'on a déjà comparé, on re-confronterait
     * le même pas à chaque image de l'attente.
     */
    int32_t      live_verified;
} duel_ghost;

static void duel_release(duel_ghost *d)
{
    if (d->live) { ns_lockstep_say_bye(d->live); ns_lockstep_close(d->live); d->live = NULL; }
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
/* Un pas du fantôme : ses appuis, ses maintiens, son avancée, ses événements.
 * Isolé parce que le duel EN DIRECT peut en jouer PLUSIEURS dans la même image
 * quand il rattrape une attente — voir `duel_tick`. */
static void duel_step(duel_ghost *d, const ns_game_api *api, uint8_t press, float dt)
{
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

static void duel_tick(duel_ghost *d, const ns_game_api *api, int32_t tick, float dt)
{
    if (!d->running || !d->state || !api) return;

    if (d->live) {
        /*
         * Le duel EN DIRECT : les entrées de l'adversaire arrivent par la
         * socket, avec `NS_LOCKSTEP_DELAY` pas de retard.
         *
         * Le fantôme est donc en retard de huit pas sur nous — 66 ms — et c'est
         * VOULU : c'est ce retard qui donne au réseau le temps de livrer. Le
         * joueur, lui, n'attend pas : sa propre partie avance à pleine vitesse.
         * C'est toute la différence avec un pas verrouillé pur, où la partie de
         * chacun s'arrête dès que l'autre a un hoquet. Ici deux parties
         * SÉPARÉES courent sur la même graine, et chacun rejoue celle de
         * l'autre pour la voir — un hoquet fige l'adversaire à l'écran, pas la
         * borne sous les doigts.
         *
         * LE FANTÔME AVANCE À SON PROPRE RYTHME, et c'est la correction d'un
         * défaut qui a coûté cher à diagnostiquer. Le pas rejoué était calculé
         * comme « le nôtre moins huit ». Quand l'entrée de ce pas n'était pas
         * encore arrivée, on rendait la main — correctement — mais à l'image
         * suivante on demandait le pas SUIVANT, et le pas manqué n'était JAMAIS
         * rejoué. Le fantôme perdait une entrée et une avancée à chaque attente,
         * et l'empreinte le disait : mesuré, deux attentes suffisaient à faire
         * annoncer « divergence au pas 96 » alors que les deux parties réelles
         * portaient exactement la même empreinte à ce pas-là.
         *
         * On boucle donc depuis le dernier pas VRAIMENT rejoué jusqu'à la cible,
         * ce qui rattrape naturellement le retard dès que les entrées arrivent.
         */
        ns_lockstep_poll(d->live);
        const ns_lockstep_state st = ns_lockstep_status(d->live);
        if (st == NS_LOCKSTEP_DESYNC || st == NS_LOCKSTEP_ERROR ||
            st == NS_LOCKSTEP_ENDED) {
            d->finished = true;
            d->running = false;
            return;
        }
        const int32_t cible = tick - NS_LOCKSTEP_DELAY;
        while (d->last_tick < cible) {
            const int32_t suivant = d->last_tick + 1;
            uint8_t held = 0, pressed = 0;
            if (!ns_lockstep_peer_input(d->live, suivant, &held, &pressed)) {
                /* En retard : on ne devine pas. Rejouer une entrée qu'on n'a pas
                 * reçue est exactement ce qui fait diverger deux parties, et on
                 * s'est donné une empreinte pour ne PAS en arriver là. On
                 * REPRENDRA à ce pas-ci, pas au suivant. */
                d->live_stall++;
                return;
            }
            d->held = held;
            duel_step(d, api, pressed, dt);
            d->last_tick = suivant;
        }
        return;
    }

    uint8_t press = 0;
    if (tick > d->last_tick) { d->finished = true; d->running = false; return; }
    while (d->cursor < d->count && d->input[d->cursor].tick == tick) {
        d->held = d->input[d->cursor].held;
        press |= d->input[d->cursor].pressed;
        d->cursor++;
    }
    duel_step(d, api, press, dt);
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
/*
 * Ouvre un duel EN DIRECT depuis `--duel-direct=hôte:port,identifiant,place`.
 *
 * Rend la graine du relais, ou 0 si le duel n'a pas pu s'ouvrir — auquel cas la
 * partie se joue seule, comme elle se joue seule sans serveur. Un duel qui ne
 * s'ouvre pas ne doit jamais empêcher de jouer : c'est la même règle que pour
 * le billet de partie, et elle vaut ici pour la même raison.
 */
static uint64_t duel_live_open(duel_ghost *d, const char *spec,
                               const ns_game_api *api, bool hard)
{
    if (!spec || !api) return 0;

    char host[128] = {0};
    unsigned port = 0;
    unsigned long long id = 0;
    int slot = -1;
    if (SDL_sscanf(spec, "%127[^:]:%u,%llu,%d", host, &port, &id, &slot) != 4 ||
        port == 0 || port > 65535 || slot < 0 || slot > 1) {
        NS_WARN("--duel-direct : « %s » ne se lit pas — attendu "
                "« hôte:port,identifiant,place » avec une place valant 0 ou 1", spec);
        return 0;
    }

    char err[160] = {0};
    d->live = ns_lockstep_connect(host, (uint16_t)port, (uint64_t)id, slot,
                                  api->id, hard ? "hard" : "normal",
                                  3000, err, sizeof err);
    if (!d->live) {
        NS_WARN("--duel-direct : %s — la partie se jouera seule", err);
        return 0;
    }

    /* On attend l'autre joueur, mais pas indéfiniment : une borne qui se fige
     * en attendant quelqu'un qui ne vient pas est une borne cassée. */
    const uint64_t deadline = SDL_GetTicks() + 15000;
    while (SDL_GetTicks() < deadline) {
        ns_lockstep_poll(d->live);
        if (ns_lockstep_status(d->live) == NS_LOCKSTEP_RUNNING) break;
        if (ns_lockstep_status(d->live) >= NS_LOCKSTEP_ENDED) break;
        SDL_Delay(5);
    }
    if (ns_lockstep_status(d->live) != NS_LOCKSTEP_RUNNING) {
        NS_WARN("--duel-direct : aucun adversaire au bout de quinze secondes — "
                "la partie se jouera seule");
        ns_lockstep_close(d->live);
        d->live = NULL;
        return 0;
    }

    const uint64_t seed = ns_lockstep_seed(d->live);
    SDL_strlcpy(d->game, api->id, sizeof d->game);
    d->seed = (int64_t)seed;
    d->state_size = api->state_size;
    SDL_free(d->state);
    d->state = SDL_calloc(1, api->state_size);
    if (!d->state) { ns_lockstep_close(d->live); d->live = NULL; return 0; }
    api->reset(d->state, seed, hard);
    api->set_best(d->state, 0);
    d->cursor = 0;
    d->held = 0;
    /* −1 et non 0 : le fantôme n'a rejoué AUCUN pas. Le laisser à zéro ferait
     * croire au contrôle d'empreinte que le pas 0 est déjà joué, et il
     * confronterait un état neuf à l'empreinte d'un pas simulé. */
    d->last_tick = -1;
    d->live_verified = -1;
    d->live_stall = 0;
    d->loaded = d->running = true;
    d->finished = false;
    SDL_strlcpy(d->info.name, "EN DIRECT", sizeof d->info.name);
    NS_INFO("duel en direct : apparie sur %s:%u, duel %llu, place %d, graine %llu",
            host, port, id, slot, (unsigned long long)seed);
    return seed;
}

static void start_run(const ns_game_api *api, void *game, ns_runlog *log,
                      uint64_t seed, bool hard, bool demo, duel_ghost *duel)
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
    /*
     * UNE PARTIE DE DEMONSTRATION NE PREND PAS DE BILLET, et il a fallu mesurer
     * le serveur pour s'en apercevoir.
     *
     * `finish_run` refuse depuis toujours d'envoyer une partie d'`--autoplay` :
     * ce n'est pas un joueur, et le classement mondial merite encore moins
     * qu'un robot y figure. Mais on lui prenait quand meme un billet en
     * DEMARRANT, c'est-a-dire une ligne creee sur le serveur qui ne recevrait
     * jamais son journal. Deux ont ete comptees sur une pile Docker reelle.
     * Un robot qui n'a pas le droit de se classer n'a pas de raison d'ouvrir
     * un dossier.
     *
     * Et ca corrige un second defaut, celui-la sur les CAPTURES : le billet
     * REMPLACE la graine par celle du serveur. Le commentaire ci-dessus dit que
     * la garantie exacte est « une capture hors ligne est reproductible ». Avec
     * un serveur configure, la meme commande `--game= --autoplay` rendait deux
     * images differentes. La garantie devient : une capture de demonstration est
     * reproductible, serveur ou non.
     */
    ns_online_ticket ticket;
    const bool ticketed = !demo
                       && ns_online_take_ticket(api->id, difficulty, &ticket);
    if (ticketed) seed = (uint64_t)ticket.seed;

    api->reset(game, seed, hard);

    /*
     * Le record personnel entre dans l'état, et il ne doit pas y entrer pendant
     * un duel EN DIRECT.
     *
     * `set_best` écrit une valeur dans le bloc de jeu, et ce bloc est
     * exactement ce dont on compare l'empreinte pour détecter une divergence.
     * Or deux joueurs n'ont pas le même record : les deux parties seraient
     * déclarées divergentes dès le pas zéro alors qu'elles calculent la même
     * chose. Mesuré, et c'est ce qui s'est produit au premier essai — « duel :
     * divergence au pas 0 » sur les deux clients.
     *
     * On aurait pu n'empreindre qu'une partie de l'état ; il faudrait alors que
     * chaque jeu déclare quels octets comptent, c'est-à-dire ajouter à
     * `games.h` une notion « ce champ ne fait pas partie de la simulation » que
     * les huit jeux devraient tenir à jour. Mettre le record à zéro le temps
     * d'un duel coûte un chiffre à l'écran, et le record n'a de toute façon
     * rien à faire dans une course à deux.
     */
    api->set_best(game, (duel && duel->live) ? 0u
                                             : ns_scores_best(api->id, difficulty));

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
/*
 * Le tangage qui met une dalle au centre du cadre.
 *
 * Il se calculait EN TROIS ENDROITS — `--play-at=`, l'entrée en partie, et la
 * capture nommée `borne` qui l'écrivait carrément à la main dans le JSON. Trois
 * copies d'un même calcul, dont une figée dans un fichier de données : c'est la
 * configuration exacte qui laisse deux d'entre elles dériver sans que rien ne
 * le dise.
 */
/*
 * LE COMPTOIR À PORTÉE : le monnayeur, la vitrine, ou rien.
 *
 * ON NE PASSE PAS PAR `ns_scene_nearest_poi`, ET C'EST LE POINT DÉLICAT.
 *
 * Elle rend le lieu le plus proche TOUTES NATURES CONFONDUES. Filtrer son
 * résultat marcherait tant qu'aucun autre lieu n'est plus près — et la salle en
 * déclare huit, dont la porte, à quelques mètres de la vitrine dans l'alcôve
 * nord-est. Le jour où l'un d'eux passe devant, l'invite de la vitrine
 * s'éteint : sans erreur, sans message, et pour une raison qu'on ne trouve pas
 * en regardant la vitrine. On cherche donc parmi les DEUX natures qui nous
 * intéressent, ce qui rend le résultat indépendant des six autres.
 *
 * LA DISTANCE EST HORIZONTALE, ET C'EST LA SECONDE CHOSE QU'IL A FALLU
 * CORRIGER EN IMAGE. Une distance en trois dimensions est ce qu'on écrit
 * d'abord, et elle ne peut pas marcher ici : l'ancre d'un lieu est l'origine du
 * meuble, donc AU SOL, et l'œil du joueur est à 1,60 m. La composante verticale
 * mange à elle seule tout le budget, et l'invite ne s'allume jamais — quelle
 * que soit la valeur qu'on écrit, tant qu'elle est inférieure à la taille du
 * joueur. Aucun message, aucune erreur : le monnayeur reste muet et on cherche
 * du côté de la salle. Vu sur capture à 1,97 m mesurés pour une portée de 1,60.
 *
 * 1,6 m au sol. Le monnayeur fait 0,60 m de profondeur et la vitrine 0,42 ;
 * un joueur planté devant se tient entre 0,6 et 0,9 m de l'origine du meuble
 * une fois sa propre demi-largeur de 0,425 m comptée. 1,6 m laisse donc la
 * marge d'un pas de côté sans allumer l'invite depuis l'allée.
 *
 * Les six autres natures n'ont rien à proposer : billard, canapé, bar, radio,
 * toilettes et porte. Une invite qui s'allumerait devant un canapé apprendrait
 * au joueur à ne plus la lire.
 */
#define ECO_PORTEE_COMPTOIR 1.6f

/* `anchor` peut être NULL, et l'invite du bandeau le passe ainsi : elle a
 * besoin de savoir CE QU'IL Y A à portée, pas où. Le monnayeur, lui, a besoin
 * du point d'où tombent les pièces — sans quoi leur bruit viendrait de la tête
 * du joueur au lieu de la machine devant laquelle il se tient. */
static ns_poi_kind eco_poi_kind(const ns_scene *scene, const room_camera *cam,
                                ns_v3 *anchor)
{
    if (cam->mode != ROOM_CAM_PLAYER) return NS_POI_NONE;

    ns_poi_kind best_kind = NS_POI_NONE;
    float       best2 = ECO_PORTEE_COMPTOIR * ECO_PORTEE_COMPTOIR;
    for (uint32_t i = 0; i < scene->poi_count; ++i) {
        const ns_poi *p = &scene->pois[i];
        if (p->kind != NS_POI_TOKENS && p->kind != NS_POI_PRIZES) continue;
        const float dx = cam->position.x - p->anchor.x;
        const float dz = cam->position.z - p->anchor.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 < best2) { best2 = d2; best_kind = p->kind; if (anchor) *anchor = p->anchor; }
    }
    return best_kind;
}

static float pitch_onto(ns_v3 eye, ns_v3 target)
{
    const float ex = target.x - eye.x;
    const float ey = target.y - eye.y;
    const float ez = target.z - eye.z;
    return atan2f(ey, ns_maxf(0.05f, sqrtf(ex * ex + ez * ez)));
}

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
 * LES ÉTIQUETTES DES AUTRES JOUEURS, au-dessus de leur tête.
 *
 * CE QUI A CHANGÉ, et pourquoi l'étiquette reste
 * ----------------------------------------------
 * Elle était TOUT ce qu'un pair avait : une plaque à son nom flottant à
 * hauteur de tête, et rien en dessous. Le commentaire d'alors expliquait qu'il
 * n'existait aucun modèle de personnage dans ce dépôt. Il en existe un depuis —
 * celui que le joueur porte en troisième personne — et les pairs le portent
 * maintenant aussi : voir `room_presence.h` et la boucle de rendu.
 *
 * L'étiquette n'est donc plus l'avatar, elle est ce qui le NOMME, et c'est
 * pour ça qu'elle reste. Un corps qui marche dit qu'il y a quelqu'un ; il ne dit
 * pas que c'est Ada, ni qu'elle est à 1 200 sur Tetris. C'est cette phrase-là
 * qui rend une salle d'arcade vivante, et aucun maillage ne la remplace.
 *
 * SA HAUTEUR EST MESURÉE, elle ne l'était pas
 * -------------------------------------------
 * Elle était posée à `y + 1,75 m`. Deux erreurs dans un seul nombre : 1,75 était
 * écrit à la main pour un personnage qui fait 1,82 m, et surtout `y` est la
 * position de l'ŒIL du pair et non celle de ses pieds — l'étiquette montait donc
 * à trois mètres quarante du sol, sous un plafond qui en fait 2,92. Elle est
 * maintenant posée sur les PIEDS que `room_presence` calcule, plus la hauteur du
 * modèle telle que `ns_skin_rest_height` la mesure.
 *
 * CE QU'ELLE NE FAIT TOUJOURS PAS : elle n'est pas occultée par les murs. Un
 * joueur derrière une cloison voit son nom à travers. C'est faux, et c'est
 * assumé — le test d'occultation demanderait un lancer de rayon par joueur et
 * par image contre le BVH, pour cacher une étiquette. La salle fait une seule
 * pièce ouverte : le cas est rare, et la corriger coûterait plus qu'elle ne gêne.
 *
 * LE CORPS, LUI, L'EST — et c'est bien ce qu'on veut. Il ne passe pas par cette
 * couche 2D mais par la passe des personnages, qui CHARGE la profondeur de la
 * scène et teste contre elle (`enable_depth_test`, `LOADOP_LOAD` sur la
 * profondeur, dans `ns_render.c`). Un pair derrière une borne est donc caché par
 * la borne, seul son nom flotte au-dessus — ce qui est exactement le
 * comportement lisible : on sait que quelqu'un est là sans voir à travers le
 * décor.
 */
/*
 * LA TEINTE ACHETEE, posee sur les reglages de rendu.
 *
 * « PLAQUE DOREE » coute 320 tickets — une trentaine de parties medianes — et
 * promet que « la salle vous passe en or ». Elle ne faisait rien. Un lot paye
 * qui ne change rien est le pire defaut qu'un jeu d'arcade puisse avoir : il
 * apprend au joueur que ses tickets ne valent pas la peine d'etre gagnes, et
 * c'est toute la boucle qui tombe avec.
 *
 * L'OR EST PARTIEL, ET C'EST LA MESURE QUI LE DIT. A pleine force la salle
 * devient monochrome et les dix-neuf ecrans avec elle : on ne distingue plus
 * une partie de demineur d'une partie de snake, et le lot rend le jeu MOINS
 * jouable. A 0,38 la salle vire franchement — moquette, murs, bois — et les
 * dalles, qui sont les surfaces les plus saturees et les plus lumineuses du
 * champ, gardent leur couleur propre. On voit qu'on a paye, on voit encore ce
 * qu'on joue.
 *
 * La teinte est un or CHAUD et non un jaune pur : (1,00 ; 0,78 ; 0,34) est la
 * chromaticite d'un laiton poli, celle du bandeau du monnayeur. Un jaune pur
 * aurait donne un filtre sepia, ce qui est l'inverse d'une recompense.
 *
 * Appelee juste avant CHAQUE envoi de reglages, y compris apres un changement
 * de palier : `ns_render_settings_defaults` remet la structure a zero, donc
 * poser la teinte une seule fois a l'ouverture la ferait disparaitre au premier
 * F7. C'est pour ca que c'est une fonction et non une affectation.
 */
static void appliquer_teinte_achetee(ns_render_settings *rs)
{
    const room_eco *e = room_eco_salle();
    if (e && room_eco_lot_acquis(e, ROOM_ECO_LOT_DOREE)) {
        rs->grade_tint[0] = 1.00f;
        rs->grade_tint[1] = 0.78f;
        rs->grade_tint[2] = 0.34f;
        rs->grade_strength = 0.38f;
    } else {
        rs->grade_strength = 0.0f;
    }
}

static void draw_presence(ns_sprite *s, const room_presence *pr,
                          const ns_camera *cam, float aspect)
{
    const uint32_t n = pr ? pr->body_count : 0;
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

    /*
     * LES ETIQUETTES SE DESEMPILENT, et il a fallu quatre marcheurs pour le voir.
     *
     * Elles etaient dessinees dans l'ordre du tableau de pairs, chacune centree
     * sur la tete de son corps. A quatre personnes groupees dans l'allee — ce
     * qui est la situation NORMALE d'une salle d'arcade, pas un cas limite — les
     * quatre plaques se recouvrent et plus aucun nom ne se lit : la capture
     * montrait « Dan », « Chloe » et « SNAKE 1474 » imprimes les uns sur les
     * autres. Une etiquette illisible est pire qu'une etiquette absente, parce
     * qu'elle abime aussi celle du voisin.
     *
     * Trois regles, dans cet ordre :
     *
     *   1. DU PLUS PROCHE AU PLUS LOIN. Celui qui est devant a la priorite, et
     *      sa plaque opaque passe par-dessus. C'est aussi l'ordre qui donne la
     *      bonne lecture de profondeur : un nom lointain qui recouvre un nom
     *      proche est un contresens.
     *   2. ON DECALE VERS LE HAUT. Une plaque qui tombe sur une deja posee
     *      remonte d'une hauteur de plaque, jusqu'a trois fois. Vers le haut et
     *      non vers le bas parce qu'au-dessus d'une tete il y a le plafond,
     *      et au-dessous il y a le corps qu'on cherche a designer.
     *   3. SI CA NE SUFFIT PAS, ON RENONCE. Le nom reste dans la liste en haut
     *      a droite, qui existe precisement pour dire QUI est la quand
     *      l'etiquette ne le peut pas.
     *
     * Le rectangle teste inclut la SOUS-LIGNE (le jeu et le score) : c'est elle
     * qui depasse et qui va cogner le nom du voisin.
     */
    uint32_t order[ROOM_PRESENCE_MAX];
    float    order_d[ROOM_PRESENCE_MAX];
    uint32_t order_n = 0;
    for (uint32_t i = 0; i < n && order_n < ROOM_PRESENCE_MAX; ++i) {
        const room_presence_body *p = &pr->body[i];
        const ns_v3 w = ns_v3_make(p->feet.x, p->label_y, p->feet.z);
        order[order_n]   = i;
        order_d[order_n] = ns_v3_len(ns_v3_sub(w, cam->position));
        order_n++;
    }
    /* Tri par insertion : seize elements au plus, et il tourne une fois par
     * image. Un tri plus savant coûterait plus a lire qu'a executer. */
    for (uint32_t a = 1; a < order_n; ++a) {
        const uint32_t ki = order[a];
        const float    kd = order_d[a];
        uint32_t b = a;
        while (b > 0 && order_d[b - 1] > kd) {
            order[b] = order[b - 1]; order_d[b] = order_d[b - 1]; b--;
        }
        order[b] = ki; order_d[b] = kd;
    }

    /* Les plaques deja posees, en (x, y, largeur, hauteur). */
    float placed[ROOM_PRESENCE_MAX][4];
    uint32_t placed_n = 0;

    for (uint32_t k = 0; k < order_n; ++k) {
        const uint32_t i = order[k];
        const room_presence_body *p = &pr->body[i];

        /*
         * L'étiquette S'EFFACE AVEC LE CORPS. Le fondu de sortie porte sur les
         * deux, sinon un nom resterait seul en l'air pendant deux secondes et
         * demie au-dessus de personne — ce qui est précisément le clignotement
         * qu'on cherchait à éviter, déplacé d'une couche.
         */
        const float fade = ns_clampf(p->opacity, 0.0f, 1.0f);
        if (fade <= 0.01f) continue;

        /* Sur la TÊTE, à une hauteur que `room_presence` tire de la mesure du
         * modèle plutôt que d'une cote écrite à la main. */
        const ns_v3 world = ns_v3_make(p->feet.x, p->label_y, p->feet.z);
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

        /* La sous-ligne est calculee AVANT le placement : elle fait partie de
         * l'encombrement, et c'est elle qui depasse par le bas. */
        char line[64];
        line[0] = '\0';
        float sub = 0.0f, sw = 0.0f, sh = 0.0f;
        if (p->game[0]) {
            if (p->score > 0) {
                SDL_snprintf(line, sizeof line, "%s %d", p->game, (int)p->score);
            } else {
                SDL_snprintf(line, sizeof line, "%s", p->game);
            }
            sub = ns_maxf(scale * 0.7f, 1.0f);
            sw  = ns_sprite_text_width(line, sub);
            sh  = ns_sprite_text_height(sub) + 4.0f;
        }

        const float bw = ns_maxf(tw + 12.0f, sw);
        const float bh = th + 8.0f + sh;
        float bx = sx - bw * 0.5f;
        float by = sy - th * 0.5f - 4.0f;

        bool libre = false;
        for (int essai = 0; essai < 4 && !libre; ++essai) {
            libre = true;
            for (uint32_t q = 0; q < placed_n; ++q) {
                if (bx < placed[q][0] + placed[q][2] && bx + bw > placed[q][0]
                    && by < placed[q][1] + placed[q][3] && by + bh > placed[q][1]) {
                    libre = false;
                    break;
                }
            }
            if (!libre) by -= bh + 3.0f;
        }
        if (!libre) continue;      /* la liste en haut a droite le dira */

        placed[placed_n][0] = bx; placed[placed_n][1] = by;
        placed[placed_n][2] = bw; placed[placed_n][3] = bh;
        placed_n++;

        const float name_y = by + 4.0f + th * 0.5f;

        /* Les trois teintes reprises telles quelles, alpha multiplié par le
         * fondu : c'est le même effacement que le corps, pas un second. */
        const float name_src[4] = { p->verified ? white[0] : unverified[0],
                                    p->verified ? white[1] : unverified[1],
                                    p->verified ? white[2] : unverified[2],
                                    (p->verified ? white[3] : unverified[3]) * fade };
        const float back_f[4] = { back[0], back[1], back[2], back[3] * fade };
        const float dim_f[4]  = { dim[0],  dim[1],  dim[2],  dim[3]  * fade };

        ns_sprite_rect(s, bx, by, tw + 12.0f, th + 8.0f, back_f);
        ns_sprite_text(s, bx + 6.0f, name_y - th * 0.5f, scale, name_src, p->name);

        /* Ce qu'il fait, sous son nom. C'est ça qui rend la salle vivante :
         * « Bob — SNAKE 1200 » raconte quelque chose, une position non. */
        if (line[0]) {
            ns_sprite_text(s, bx + (bw - sw) * 0.5f, by + th + 8.0f, sub, dim_f, line);
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
static void draw_presence_roster(ns_sprite *s, const room_presence *pr)
{
    const uint32_t n = pr ? pr->body_count : 0;
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
        if (pr->body[i].game[0]) {
            SDL_snprintf(line, sizeof line, "%s - %s",
                         pr->body[i].name, pr->body[i].game);
        } else {
            SDL_snprintf(line, sizeof line, "%s", pr->body[i].name);
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

    /*
     * LA MANETTE, dans son propre sous-système et non dans l'appel ci-dessus.
     *
     * Séparée délibérément : le pilote de manettes s'appuie sur des services
     * que le système peut refuser — HID sur Windows, l'accès aux périphériques
     * dans un bac à sable sur macOS — et un échec ici ferait échouer `SDL_Init`
     * en bloc. Le jeu se lancerait alors sans image ni son parce qu'aucune
     * manette n'a pu être énumérée, ce qui n'a aucun sens : une salle d'arcade
     * se joue très bien au clavier.
     *
     * On le DIT quand ça échoue, plutôt que de laisser croire à une manette
     * cassée : c'est le sous-système qui manque, pas le périphérique.
     */
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        NS_WARN("manettes indisponibles : %s", SDL_GetError());
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

    /*
     * Les REGLAGES DU PERSONNAGE, avant toute chose qui pourrait en lire un.
     *
     * Apres `ns_paths_init` — qui monte les dossiers de donnees, et c'est l'un
     * des endroits ou le fichier est cherche — et avant `ns_config_init`, la
     * camera, le viewmodel et la scene, qui sont ses lecteurs.
     */
    ns_env_load(opt.env_path);
    /* Les cotes de bras sont lues tout de suite : elles doivent l'être même
     * quand aucune image ne dessine de bras — voir room_viewmodel.c. */
    room_viewmodel_read_env();
    room_poste_read_env();

    ns_config_init("settings.cfg");

    /* La ligne de commande gagne sur la configuration, la configuration gagne
     * sur les valeurs par défaut. */
    const int win_w = (opt.width  > 0) ? opt.width  : ns_config_get_int(NS_CFG_WINDOW_W, 1600);
    const int win_h = (opt.height > 0) ? opt.height : ns_config_get_int(NS_CFG_WINDOW_H, 900);

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
        /*
         * LE REPLI VA DANS LES DEUX SENS, et le second cas est celui d'un
         * PAQUET : la salle de 2020 n'y est pas installee — elle est batie sur
         * des textures dont on n'a pas les droits, et le jeu livre a une salle,
         * pas deux. Un joueur dont la configuration gardee dit « legacy », ou
         * qui tape `--room=legacy` par curiosite, ne doit pas se retrouver
         * devant « la salle n'a pas pu etre chargee ».
         */
        bool loaded = false;
        if (SDL_strcmp(scene_path, "scene/salle.gltf") == 0) {
            NS_WARN("salle reconstruite absente, repli sur celle de 2020");
            loaded = ns_scene_load(rhi, &scene, "scene/salle-legacy.gltf");
        } else {
            NS_WARN("la salle de 2020 n'est pas installee (outil de "
                    "developpement) : repli sur la salle reconstruite");
            loaded = ns_scene_load(rhi, &scene, "scene/salle.gltf");
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
                cam.position = cam.prev_position = vp->position;
                cam.yaw   = cam.prev_yaw   = vp->yaw;
                cam.pitch = cam.prev_pitch = vp->pitch;
                /*
                 * UN POINT DE VUE BASCULE EN CAMÉRA LIBRE — sauf si `--camera=`
                 * a explicitement demandé autre chose.
                 *
                 * La caméra libre est ce qu'on veut pour cadrer un mur, et
                 * toutes les captures de référence en dépendent : on n'y touche
                 * pas. Mais elle ne dessine NI les bras NI le personnage, et
                 * elle ignore la troisième personne, qui n'existe qu'en mode
                 * joueur. `--view=allee --camera=player` produisait donc une
                 * image sans personnage, sans un mot, en ayant l'air d'obéir —
                 * et c'est ce silence qui a fait croire à un défaut du jeu là où
                 * il n'y avait qu'un piège de l'outil de capture.
                 *
                 * Même remède que pour `--pose` vingt lignes plus bas : la
                 * demande explicite gagne, et on le DIT.
                 */
                if (opt.camera_set && opt.camera_mode != ROOM_CAM_FREE) {
                    cam.mode = opt.camera_mode;
                    NS_INFO("point de vue « %s » : caméra laissée en mode demandé "
                            "(--camera=), et non basculée en libre", vp->name);
                } else {
                    cam.mode = ROOM_CAM_FREE;
                }
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
     * Les portes coulissantes. Après la scène — elles y retrouvent leurs
     * sommets par le nom du nœud — et avant la boucle, qui les fait vivre en
     * trois temps : `room_doors_tick` au pas fixe, `room_doors_upload` dans
     * l'image, et `room_sound_update` pour les deux extraits de 2020.
     */
    room_doors doors;
    room_doors_init(&doors, &scene);

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
     * LE POSTE DE JEU : le point de vue s'approche de la dalle quand la partie
     * tourne dedans. Le corps ne bouge pas — voir `room_poste.h`, qui porte la
     * mesure du timbre-poste à 8 % du cadre et la table des deux leviers.
     */
    room_poste poste;
    room_poste_init(&poste);
    /*
     * LA PORTEE, en mètres depuis le centre de la dalle : au-delà, la vue est
     * rendue au corps. Elle est ce qui fait qu'on peut RECULER en jouant sans
     * que la caméra reste collée à la machine — la partie continue, c'est la
     * salle qui revient.
     *
     * 1,40 m contre 0,870 m à l'ancre déclarée : un demi-mètre de battement,
     * soit un pas en arrière. Plus serré, le poste se lâcherait sur le
     * balancement de la marche sur place ; plus large, on emmènerait la vue à
     * deux mètres de la borne.
     */
    const float poste_portee = 1.40f;
    /*
     * La borne sur laquelle le regard est DÉJÀ descendu.
     *
     * Sans cette mémoire, se planter devant une borne rabattrait le tangage à
     * chaque image : la tête serait clouée sur la dalle et le joueur ne
     * pourrait plus regarder ailleurs sans lutter. On ne pose le regard qu'au
     * moment où l'on ARRIVE devant une borne, et on rend la tête aussitôt
     * après — c'est le geste qu'on fait vraiment, et une seule fois.
     */
    const ns_cabinet *gazed_at = NULL;
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
    /* La dalle qu'on vient de frapper, ou -1. Retenue entre l'impact et la fin
     * du déraillement : `room_viewmodel_take_impact` ne la donne qu'une fois,
     * et le tremblement, lui, dure un tiers de seconde. */
    int32_t    choc_material = -1;
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
    /* Le portefeuille, à côté du classement et pour la même raison : les deux
     * sont l'état du joueur, ils se chargent ensemble et se sauvent ensemble.
     * Tout ce que la salle a à en dire tient dans `room_economie.h`. */
    room_eco_salle_ouvrir();

    /*
     * LA TEINTE ACHETEE, POSEE ICI ET PAS AVANT — et c'est une mesure qui l'a
     * dit. Je l'avais mise a la creation du renderer : la sonde y rendait
     * « eco=0x1008e8660 acquis=0 » a 0,084 s, c'est-a-dire un portefeuille pas
     * encore lu. Une capture d'un joueur qui POSSEDE la plaque doree sortait
     * identique au dixieme de niveau pres a celle d'un joueur qui ne l'a pas.
     * L'ordre est le seul defaut, et il ne se voit pas a la lecture.
     *
     * `teinte_active` suit ensuite l'etat d'une image a l'autre : l'envoi des
     * reglages n'a lieu qu'au CHANGEMENT — a l'achat, dans la salle, sans
     * relancer — parce que `ns_renderer_set_settings` reconstruit les cibles
     * hors ecran et qu'un appel par image les rebatirait soixante fois par
     * seconde.
     */
    bool teinte_active = false;
    {
        const room_eco *e0 = room_eco_salle();
        teinte_active = e0 && room_eco_lot_acquis(e0, ROOM_ECO_LOT_DOREE);
        if (teinte_active) {
            appliquer_teinte_achetee(&rs);
            ns_renderer_set_settings(rhi, renderer, &rs);
        }
    }

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
        /*
         * QUATRE sources, un seul arbitre. La cascade tenait autrefois en un
         * `?:` sur deux niveaux ; à quatre, elle mérite d'être écrite ailleurs,
         * testée pour elle-même (`tests/test_online.c`) et capable de DIRE d'où
         * vient ce qu'elle a retenu. L'ordre et ses raisons sont dans
         * `engine/net/ns_online.h`, au-dessus de `ns_online_resolve_url`.
         */
        const ns_online_url choix = ns_online_resolve_url(
            NINETEEN_SERVER_URL,
            ns_config_get_str(NS_CFG_SERVER_URL, ""),
            opt.server_env,
            opt.server);
        oc.server_url = choix.url;
        oc.source = choix.source;
        oc.token = ns_config_get_str(NS_CFG_SERVER_TOKEN, "");
        oc.locked = opt.offline;
        if (ns_online_init(&oc)) {
            /* On demande le classement d'ENVOL dès le départ : c'est celui que
             * la borne de classement affiche en premier. Le jeu s'appelait
             * `flappy` ; l'identifiant demandé ici ne suivait pas le renommage,
             * et le serveur répondait sur un jeu qui n'existe plus — le panneau
             * mondial restait vide, sans erreur. */
            ns_online_request_board("envol", "normal");
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
     * L'ATTRACT MODE : chaque borne joue sa propre partie.
     *
     * Créé après les deux cibles ci-dessus et non avant, parce qu'il en alloue
     * une par borne : si la mémoire vidéo venait à manquer, mieux vaut que ce
     * soit la démo qui y renonce que la partie du joueur.
     */
    /*
     * `NINETEEN_NO_ATTRACT` éteint les démos. Il existe pour une seule raison,
     * et c'est celle qui a servi : MESURER ce qu'elles coûtent. Le chiffre, sur
     * l'allée en palier medium, est 23,7 ms sans et 24,2 ms avec — un demi-pas
     * de temps pour dix-huit écrans vivants, parce qu'on n'en redessine que
     * quatre par image. Sans cet interrupteur, la seule façon de connaître ce
     * chiffre aurait été de croire une estimation.
     */
    /*
     * LE TABLEAU DU BAR. Un 2:1 — et non le 16:9 des dalles : un panneau deux
     * fois plus large que haut rendu en 16:9 étire tout ce qu'on y écrit.
     *
     * LA DEFINITION EST UNE DENSITE DE TEXELS, PAS UN NOMBRE ROND, et c'est la
     * mesure qui l'a fixée. Une dalle de borne fait 0,5333 m pour 512 px, soit
     * 960 texels par mètre (`salle.scene.json`, clé `screenWidth`). Le même
     * matériau habille ici deux surfaces bien plus grandes : l'écran du bar
     * (1,78 m) et « tableau_semaine » (2,40 m). A 640 px c'était 360 et 267
     * texels par mètre — deux fois et demie à trois fois et demie plus grossier
     * qu'une borne, ce qui se voit dès qu'on s'approche du comptoir : le
     * lettrage y est en marches d'escalier alors que la même fonte est nette
     * sur une borne à la même distance.
     *
     * 2560 x 1280 porte l'écran du bar à 1438 texels par mètre, soit 1,50 fois
     * une dalle de borne (960). Ce sont des DOUBLEMENTS EXACTS depuis 640 :
     * `room_hud_draw_scoreboard` multiplie toutes ses cotes par `w / 640`, donc
     * chaque coordonnée reste sur la même grille et aucune position ne se met à
     * tomber entre deux texels.
     *
     * POURQUOI CE SECOND DOUBLEMENT, alors que 1280 rattrapait déjà l'essentiel :
     * il ne se justifie PAS tout seul, il se justifie avec le filtre. Une dalle
     * déclarée « plat » est désormais échantillonnée en LINÉAIRE et non en
     * `nearest` — c'est ce qui la distingue d'un tube, dont la grille de texels
     * doit rester visible. Or un filtre linéaire sur 1280 texels ADOUCIT le
     * lettrage au lieu de le lisser : la fonte 5 x 7 y est dessinée à 4,4 texels
     * par pixel de fonte, et l'interpolation mange les contre-formes. À 2560
     * elle en a 8,8, les bords restent francs et le filtre ne fait plus que ce
     * qu'on lui demande — supprimer les marches. Les deux changements se tiennent
     * et n'ont pas de sens l'un sans l'autre.
     *
     * Coût mesuré : la cible passe de 3,3 Mo à 13,1 Mo, et le temps GPU de
     * l'allée en palier medium ne bouge pas — le tableau est un remplissage de
     * sprites, pas une passe d'éclairage.
     *
     * Ce que ça ne corrige PAS, et il faut le dire : de LOIN, le lettrage reste
     * sous le seuil de lisibilité du projet. Mesuré sur la vue « bar » (caméra
     * à 4,10 m) en 1400 x 875, un caractère du titre fait 8,4 px de haut quand
     * le seuil relevé sur ce dépôt est 11. La définition n'y change rien : à
     * cette distance le panneau n'occupe que 348 px, et c'est la TAILLE DU
     * LETTRAGE SUR LE PANNEAU qu'il faudrait revoir, pas le nombre de texels.
     */
    ns_texture bar_rt;
    SDL_zero(bar_rt);
    if (sprites && scene.scoreboard_material >= 0) {
        ns_texture_desc sd;
        SDL_zero(sd);
        sd.width = ROOM_BAR_RT_W; sd.height = ROOM_BAR_RT_H;
        sd.format = ns_rhi_swapchain_format(rhi);
        sd.render_target = true;
        sd.sampled = true;
        sd.name = "tableau du bar";
        if (!ns_texture_create(rhi, &bar_rt, &sd)) {
            NS_WARN("tableau du bar : cible indisponible, il gardera son image peinte");
        }
    }

    /*
     * LE PERSONNAGE de la vue à la troisième personne.
     *
     * Absent, le jeu se joue à la première personne et le dit une fois : ce
     * n'est pas une erreur, c'est un mode en moins. C'est aussi ce qui permet
     * de livrer le jeu sans lui si sa licence posait un jour problème.
     */
    ns_skin *personnage = ns_skin_load("models/personnage/personnage.glb");
    if (personnage && !ns_renderer_upload_character(rhi, renderer, personnage)) {
        ns_skin_free(personnage);
        personnage = NULL;
    }
    if (!personnage) {
        NS_INFO("personnage indisponible : la troisième personne restera éteinte");
        cam.third_person = false;
    }

    /*
     * LES COTES DU PERSONNAGE, PASSÉES À LA CAMÉRA.
     *
     * Trois mesures, toutes prises sur le modèle par `ns_skin` et toutes mises à
     * l'échelle du jeu ici, une fois. La caméra ne charge rien et n'inclut pas
     * `ns_skin.h` : elle reçoit des mètres.
     *
     * L'échelle est celle du rendu — le modèle est amené à `personnage.taille` —
     * et c'est bien elle qu'il faut : un rayon mesuré dans les unités du fichier
     * et comparé à un recul en mètres serait faux d'un facteur 1,25, ce qui est
     * assez pour que les seuils aient l'air de marcher.
     */
    float echelle_perso = 1.0f;
    if (personnage) {
        const float haut = ns_skin_rest_height(personnage);
        echelle_perso = (haut > 0.01f) ? (cam.body_height_stand / haut) : 1.0f;

        /*
         * LA FOULÉE : réglée, et CONFRONTÉE à la mesure.
         *
         * Zéro dans `nineteen.env` — le défaut — garde la valeur historique de
         * 1,55 m que portent aussi le viewmodel, l'oscillation de la tête et les
         * bruits de pas. On ne la remplace PAS par la mesure, et c'est un choix
         * argumenté : le cycle livré n'a pas de pied cloué au sol, quatre
         * mesures également défendables de sa foulée s'étalent de 1,02 à 2,06 m,
         * et aucune valeur ne fait descendre le glissement à zéro. Substituer
         * une mesure aussi dispersée à une valeur réglée à l'œil, c'est changer
         * le comportement sans preuve — et désaccorder les jambes des bruits de
         * pas, qui lisent la même foulée.
         *
         * Ce qu'on fait à la place : on COMPARE, et on avertit. Un écart d'un
         * tiers est le signe soit d'un modèle changé, soit d'un réglage à
         * reprendre ; dans les deux cas on veut le savoir au démarrage plutôt
         * que sur une capture six semaines plus tard.
         */
        const float reglee  = ns_env_float("personnage.foulee", 0.0f);
        const float mesuree = ns_skin_stride_length(personnage) * echelle_perso;
        if (reglee > 1e-3f) {
            room_camera_set_actor(&cam,
                                  ns_skin_half_width(personnage)   * echelle_perso,
                                  ns_skin_sweep_radius(personnage) * echelle_perso,
                                  reglee);
        } else {
            /* Foulée à zéro : on ne touche pas à celle de la caméra. */
            room_camera_set_actor(&cam,
                                  ns_skin_half_width(personnage)   * echelle_perso,
                                  ns_skin_sweep_radius(personnage) * echelle_perso,
                                  0.0f);
        }
        const float foulee = room_camera_stride(&cam);

        NS_INFO("personnage : %.2f m de haut, demi-largeur %.3f m, rayon balayé %.3f m, "
                "foulée %.3f m (%s ; mesurée sur le cycle : %.3f m)",
                (double)(ns_skin_rest_height(personnage) * echelle_perso),
                (double)(ns_skin_half_width(personnage) * echelle_perso),
                (double)(ns_skin_sweep_radius(personnage) * echelle_perso),
                (double)foulee, (reglee > 1e-3f) ? "réglée" : "défaut",
                (double)mesuree);

        if (mesuree > 1e-3f && foulee > 1e-3f) {
            const float ecart = SDL_fabsf(foulee - mesuree) / foulee;
            if (ecart > 0.33f) {
                NS_WARN("personnage : la foulée employée (%.2f m) s'écarte de %.0f %% de "
                        "celle mesurée sur le cycle (%.2f m) — les pieds glisseront "
                        "d'autant ; régler « personnage.foulee » après avoir REGARDÉ "
                        "le personnage marcher",
                        (double)foulee, (double)(ecart * 100.0f), (double)mesuree);
            }
        }

        /*
         * L'ACCROUPI, CALÉ SUR LA COTE DE COLLISION.
         *
         * Le modèle porte UN cycle et un seul — une marche. La marche et la
         * course s'en tirent sans rien ajouter, parce que la phase suit la
         * distance parcourue. L'accroupi, lui, ne s'en tirait pas : la caméra
         * descendait de 39 cm et le personnage restait DEBOUT à l'écran. Le jeu
         * l'annonçait au démarrage depuis des mois sans le corriger.
         *
         * Il est maintenant DÉRIVÉ, et calé sur les deux cotes que
         * `nineteen.env` donne déjà à la capsule de collision : le personnage
         * accroupi fait exactement la taille que sa capsule annonce, parce que
         * `ns_skin` balaie l'angle de genou et pèse VRAIMENT les sommets pour
         * trouver celui qui donne cette hauteur-là. Changer `taille` ou
         * `tailleAccroupi` déplace l'accroupi tout seul.
         */
        if (cam.body_height_stand > 0.01f) {
            (void)ns_skin_crouch_calibrate(personnage,
                                           cam.body_height_crouch / cam.body_height_stand,
                                           ns_env_float("personnage.busteAccroupi", 0.55f));
        }
        if (ns_skin_duration(personnage) > 0.0f) {
            NS_INFO("personnage : un seul cycle d'animation (%.2f s) — la cadence "
                    "suit l'allure ; l'accroupi, le balancement d'arrêt et la "
                    "frappe en sont DÉRIVÉS (accroupi %s, frappe %s)",
                    (double)ns_skin_duration(personnage),
                    ns_skin_can_crouch(personnage) ? "calé" : "IMPOSSIBLE, il restera debout",
                    ns_skin_can_hit(personnage) ? "prête" : "IMPOSSIBLE, le bras reste inerte");
        }
    }

    /*
     * LES CORPS DES AUTRES JOUEURS.
     *
     * Les cinq valeurs viennent des MÊMES mesures que le personnage du joueur —
     * la foulée que porte la caméra, la durée du cycle, la pose de passage, la
     * hauteur au repos mise à l'échelle, la hauteur d'œil debout. Les recopier à
     * la main dans `room_presence` en aurait fait cinq constantes libres de
     * dériver de celles qui décident vraiment ; les passer par une structure les
     * garde à une seule source, et laisse le module se vérifier sans charger le
     * modèle.
     *
     * Sans personnage chargé, la configuration reste nulle et aucun corps ne
     * sortira : la salle retombe exactement sur les étiquettes d'avant.
     */
    room_presence presence;
    {
        room_presence_config pcfg;
        SDL_zero(pcfg);
        if (personnage) {
            pcfg.stride      = room_camera_stride(&cam);
            pcfg.cycle       = ns_skin_duration(personnage);
            pcfg.stand_time  = ns_skin_stand_time(personnage);
            pcfg.height      = ns_skin_rest_height(personnage) * echelle_perso;
            pcfg.eye_default = cam.eye_height_stand;
        }
        room_presence_init(&presence, &pcfg);
    }

    /*
     * LE TABLEAU DES CORPS DE L'IMAGE, alloué une fois pour toutes.
     *
     * Trente-six kilo-octets — dix-sept poses de 2 160 — hors de la boucle de
     * rendu plutôt que dedans : le remplir coûte ce qu'il coûte, mais le
     * réserver soixante fois par seconde sur la pile n'apporterait rien.
     */
    ns_character_draw poses[NS_MAX_CHARACTERS];
    uint32_t          poses_n = 0;

    /* `--pairs-demo=` a-t-il déjà déposé son battement d'amorçage. Voir là où
     * il est déposé : sans lui, une capture courte photographie le fondu
     * d'entrée au lieu des corps. */
    bool demo_amorce = false;

    room_attract *attract = SDL_getenv("NINETEEN_NO_ATTRACT")
                          ? NULL : room_attract_create(rhi, &scene);

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
     * L'aide d'arrivée : les quatre commandes qui suffisent, pendant les
     * quatorze premières secondes. Voir `room_hud.h` pour ce qu'elle solde.
     *
     * Quatorze et pas trois : le sas fait neuf mètres, on met une bonne dizaine
     * de secondes à le traverser au pas, et une aide qui a disparu avant qu'on
     * arrive dans la salle n'a aidé personne. Elle se tait d'elle-même dès
     * qu'on est devant une borne, ce qui est le moment où l'on a trouvé.
     */
    float   intro_banner = 14.0f;

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
    /*
     * L'état de la FENÊTRE tel que le menu le montre et l'écrit.
     *
     * Il part de ce qui a RÉELLEMENT été retenu à l'ouverture — ligne de
     * commande comprise — et non du fichier de configuration, pour la même
     * raison que le temps réel juste au-dessus : le menu doit montrer le jeu qui
     * tourne. Lancer avec `--width=1280 --fullscreen` et voir « 1600 x 900,
     * NON » ferait douter de tout le reste de l'écran.
     */
    int  menu_win_w = win_w;
    int  menu_win_h = win_h;
    bool menu_fullscreen = rhi_desc.fullscreen;
    /*
     * ... mais ce que le menu MONTRE et ce qu'il GARDE sont deux choses.
     *
     * Ces deux drapeaux ne passent a vrai que si le joueur change la ligne
     * lui-meme. Sans eux, `room_menu_persist` ecrivait la valeur affichee, donc
     * celle de la ligne de commande : mesure sur cette machine, une capture
     * lancee avec `--width=1920 --height=900` laissait « window.width = 1920 »
     * dans `settings.cfg`, et la definition choisie par le joueur etait perdue
     * sans qu'il ait ouvert le menu.
     */
    bool menu_win_touched = false;
    bool menu_fullscreen_touched = false;
    room_menu_ctx menu_ctx = { &rs, &mouse_sens_mult, &menu_realtime,
                               &menu_win_w, &menu_win_h, &menu_fullscreen,
                               &menu_win_touched, &menu_fullscreen_touched };
    if (opt.menu) {
        room_menu_open(&menu);
        /* Une ligne hors bornes ne surligne rien et ne se répare jamais :
         * le menu la normalise, la ligne de commande ne doit pas l'y forcer. */
        if (opt.menu_row > 0) for (int i = 0; i < opt.menu_row; ++i)
            room_menu_input(&menu, &menu_ctx, ROOM_MENU_DOWN);
        /*
         * Une ligne qui ouvre une PAGE s'ouvre, plutôt que de rester surlignée.
         *
         * Sans ça, `--menu=13` cadrait la ligne « CREDITS » sans jamais montrer
         * les crédits : l'écran qui porte une obligation de licence était le
         * seul du jeu qu'on ne pouvait pas capturer en ligne de commande, donc
         * le seul qu'on ne pouvait pas vérifier sans le jouer à la main.
         *
         * Les deux pages sont nommées par leur LIBELLÉ et non par un indice :
         * un numéro de ligne se décale au premier réglage ajouté, et c'est
         * précisément ce qui vient d'arriver à ce menu.
         */
        if (menu.cursor == room_menu_row("CREDITS")
         || menu.cursor == room_menu_row("COMMANDES")) {
            room_menu_input(&menu, &menu_ctx, ROOM_MENU_ACCEPT);
        }
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
            uint64_t seed = opt.headless ? 20240418u
                                         : (uint64_t)SDL_GetPerformanceCounter();
            game_hard = false;

            /*
             * Le duel EN DIRECT s'ouvre AVANT la partie, parce que c'est le
             * relais qui donne la graine.
             *
             * Même règle que pour le billet du classement, et pour la même
             * raison : celui qui joue ne choisit pas ce sur quoi il joue. Deux
             * adversaires qui tireraient chacun leur graine ne courraient pas
             * la même course, et comparer leurs scores n'aurait aucun sens.
             */
            if (opt.duel_live && (opt.warmup > 0.0f || opt.autoplay)) {
                /*
                 * Deux options pilotent la simulation HORS du canal d'entrées,
                 * et aucune ne peut donc dueller.
                 *
                 * `--warmup=` avance la partie sans passer par la boucle, donc
                 * sans échanger une seule entrée. `--autoplay` est plus subtil
                 * et c'est ce qui a coûté une heure : `autopilot()` appelle les
                 * fonctions du jeu DIRECTEMENT — `flappy_flap` par exemple — au
                 * lieu de passer par `press`. Ses appuis n'entrent jamais dans
                 * le journal, donc jamais dans la socket, donc jamais chez
                 * l'adversaire.
                 *
                 * Ce n'est pas un défaut du duel : c'est le duel qui l'a
                 * révélé. Deux clients lancés ainsi divergeaient au pas 0 et
                 * l'annonçaient — le détecteur faisait exactement son travail
                 * dans la vraie boucle de jeu, sur un vrai relais. On refuse
                 * donc la combinaison plutôt que de laisser un faux positif
                 * accuser le réseau.
                 */
                NS_WARN("--duel-direct est incompatible avec %s : cette option "
                        "pilote la partie hors du canal d'entrées, donc "
                        "l'adversaire ne la verrait jamais. Duel ignoré.",
                        opt.autoplay ? "--autoplay" : "--warmup=");
            } else if (opt.duel_live) {
                const uint64_t s2 = duel_live_open(&duel, opt.duel_live, game_api, game_hard);
                if (s2) seed = s2;
            }

            /* Le billet n'arrivera pas à temps pour CETTE partie — une partie ne
             * s'attend pas — mais il sera là pour la suivante, et `--autoplay`
             * en enchaîne. */
            ns_online_prefetch_ticket(game_api->id, "normal");
            start_run(game_api, game, runlog, seed, game_hard, opt.autoplay, &duel);
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
                /*
                 * LE TANGAGE VIENT DU POSTE quand il y en a un, et c'est la même
                 * correction que celle du commentaire ci-dessus, un cran plus
                 * loin : viser depuis l'œil du CORPS alors que la vue va se
                 * placer 25 cm plus loin donne un cadrage juste pendant une
                 * demi-seconde, puis faux. Sur `borne_arcade_1` les deux angles
                 * valent -31,0 et -19,9 degrés.
                 */
                if (room_poste_prendre(&poste, pick, cam.position)) {
                    cam.pitch = cam.prev_pitch = room_poste_tangage(&poste);
                } else {
                    const float ex = pick->screen_center.x - cam.position.x;
                    const float ey = pick->screen_center.y - cam.position.y;
                    const float ez = pick->screen_center.z - cam.position.z;
                    const float flat = sqrtf(ex * ex + ez * ez);
                    cam.pitch = cam.prev_pitch = atan2f(ey, ns_maxf(0.05f, flat));
                }
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
            start_run(game_api, game, runlog, 20240418, hard, opt.autoplay, &duel);
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
                start_run(game_api, game, runlog, seed, game_hard, opt.autoplay, &duel);
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
                && SDL_strcasecmp(opt.pose, "marche") != 0
                /* La frappe non plus n'a besoin d'aucune cible : on cogne
                 * dans le vide aussi bien que sur une borne, et le geste est
                 * le même. Avertir ici enverrait chercher un défaut qui
                 * n'existe pas. */
                && SDL_strcasecmp(opt.pose, "frappe") != 0
                && SDL_strcasecmp(opt.pose, "hit") != 0) {
            NS_WARN("--pose=%s : aucune borne à portée, les bras resteront au repos "
                    "(essayer --view=borne)", opt.pose);
        }
    }

    /*
     * LA MANETTE.
     *
     * Une seule à la fois, et c'est un choix : ce jeu n'a pas de mode à deux sur
     * la même machine — un duel oppose deux CLIENTS, pas deux manches. Ouvrir la
     * deuxième donnerait deux joueurs qui pilotent le même personnage, ce qui se
     * lit comme une panne. La première branchée gagne ; débranchée, la suivante
     * prend la main (voir les deux événements dans la boucle).
     */
    room_pad_tuning pad_tune;
    room_pad_read_env(&pad_tune);
    SDL_Gamepad   *pad    = NULL;
    SDL_JoystickID pad_id = 0;
    room_pad_state pad_state; SDL_zero(pad_state);
    /*
     * Le masque de l'image PRÉCÉDENTE, d'où l'on tire les fronts montants.
     *
     * Le clavier reçoit ses appuis en ÉVÉNEMENTS ; le stick n'en produit aucun —
     * il n'y a pas de « SDL_EVENT_STICK_A_DÉPASSÉ_LA_ZONE_MORTE ». Sans cette
     * mémoire, un jeu qui tourne sur `press` (Tetris, Démineur) ne répondrait
     * qu'à la croix directionnelle, et le stick paraîtrait mort sur la moitié
     * des bornes.
     */
    uint8_t pad_prev = 0;
    /* Le saut se declenche sur un FRONT : maintenu, le bouton ferait rebondir le
     * joueur a chaque pas de simulation. Le clavier le tient de l'evenement
     * SDL ; la manette n'en produit pas, on garde donc l'etat precedent. */
    bool pad_jump_prev = false;

    /*
     * Le contrôle des réglages, ICI et pas ailleurs : tous les lecteurs ont
     * lu, la boucle n'a pas commencé. Une clé mal orthographiée est silencieuse
     * par construction — la valeur par défaut s'applique et rien ne change —
     * et c'est exactement le genre de silence que ce dépôt supprime.
     */
    ns_env_report_unread();

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
        /* La teinte achetee suit l'etat du portefeuille, et l'envoi n'a lieu
         * qu'au changement : voir le commentaire a l'ouverture de l'economie. */
        {
            const room_eco *ec = room_eco_salle();
            const bool veut = ec && room_eco_lot_acquis(ec, ROOM_ECO_LOT_DOREE);
            if (veut != teinte_active) {
                teinte_active = veut;
                appliquer_teinte_achetee(&rs);
                ns_renderer_set_settings(rhi, renderer, &rs);
            }
        }

        /*
         * CE QUE CETTE IMAGE A REÇU — clavier ET manette, dans les mêmes bits.
         *
         * Les appuis ne sont plus traités là où ils ARRIVENT mais là où ils sont
         * CONSOMMÉS, une fois par image, après la boucle d'événements. C'est ce
         * qui permet à la manette d'entrer dans le jeu par le même endroit que
         * le clavier — et c'est la seule façon de garantir que `hmask`, le
         * journal d'entrées et le duel voient exactement le même octet quelle
         * que soit la main qui joue.
         */
        uint8_t frame_press  = 0;    /* fronts montants des cinq boutons de jeu */
        bool    want_interact = false; /* « E » : le jeton */
        bool    want_gamble   = false; /* « R » : quitte ou double */
        bool    want_frappe   = false; /* « F » : cogner la borne */
        bool    want_menu     = false; /* « Échap » / Start */

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            /*
             * LE BRANCHEMENT À CHAUD.
             *
             * Une manette se branche pendant la partie, et c'est même le cas le
             * plus fréquent : on lance le jeu, on constate qu'on préfère la
             * manette, on la branche. SDL pousse aussi un `ADDED` par
             * périphérique déjà présent quand le sous-système démarre — il n'y a
             * donc pas d'énumération initiale à écrire, ce chemin-ci les voit
             * tous.
             */
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!pad) {
                    pad = SDL_OpenGamepad(ev.gdevice.which);
                    if (pad) {
                        pad_id   = ev.gdevice.which;
                        pad_prev = 0;
                        NS_INFO("manette : « %s » branchée",
                                SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "?");
                    } else {
                        NS_WARN("manette non ouverte : %s", SDL_GetError());
                    }
                }
                break;

            case SDL_EVENT_GAMEPAD_REMOVED:
                if (pad && ev.gdevice.which == pad_id) {
                    SDL_CloseGamepad(pad);
                    pad = NULL;
                    pad_id = 0;
                    /*
                     * Les maintiens sont OUBLIÉS, pas gelés. Débrancher au
                     * moment où l'on tient une direction laisserait le serpent
                     * tourner tout seul jusqu'au mur, et le joueur n'aurait plus
                     * rien pour l'arrêter.
                     */
                    pad_prev = 0;
                    SDL_zero(pad_state);
                    NS_INFO("manette : débranchée");
                }
                break;

            /*
             * Seuls DEUX boutons passent par les événements : Start et le bouton
             * de retour. Tout le reste — croix, stick, bouton d'action — est LU
             * une fois par image avec le clavier, parce que c'est le masque qui
             * compte et qu'un masque se lit, il ne s'accumule pas.
             */
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                    want_menu = true;
                } else if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_EAST && menu.open) {
                    /* Le bouton de DROITE annule : c'est la convention de tous
                     * les menus de console, et sans elle on ne peut refermer une
                     * page d'information qu'avec Start. */
                    room_menu_input(&menu, &menu_ctx, ROOM_MENU_CANCEL);
                }
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
                    /* Le geste est le même que celui de Start sur la manette :
                     * il est donc décidé une seule fois, après la boucle. */
                    want_menu = true;
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
                         * En partie, le clavier appartient au jeu — mais il n'y
                         * DÉCIDE plus rien. Il dépose son front montant dans le
                         * même octet que la manette, et c'est un seul endroit,
                         * après la boucle, qui tranche entre « relancer » et
                         * « appuyer ». Deux endroits qui tranchaient la même
                         * question auraient fini par ne plus la trancher pareil.
                         */
                        if (!ev.key.repeat) {
                            ns_game_button b = NS_GAME_ACTION;
                            switch (ev.key.key) {
                                case SDLK_UP:    b = NS_GAME_UP; break;
                                case SDLK_DOWN:  b = NS_GAME_DOWN; break;
                                case SDLK_LEFT:  b = NS_GAME_LEFT; break;
                                case SDLK_RIGHT: b = NS_GAME_RIGHT; break;
                                default: break;
                            }
                            frame_press |= (uint8_t)(1u << b);
                        }
                        break;
                    }
                    /* Hors partie, seule l'espace veut dire quelque chose. Le saut
                     * est une transition, pas un état : lu en événement pour qu'un
                     * appui bref ne se perde pas entre deux pas. */
                    if (ev.key.key == SDLK_SPACE && !ev.key.repeat) cam.jump_requested = true;
                    break;
                case SDLK_F10:
                    /* La bascule première / troisième personne. Sans personnage
                     * chargé elle ne fait rien, et elle le DIT — une touche qui
                     * ne répond pas se prend pour une touche cassée. */
                    if (!personnage) {
                        NS_INFO("troisième personne : aucun personnage chargé");
                    } else if (cam.mode == ROOM_CAM_PLAYER) {
                        cam.third_person = !cam.third_person;
                        NS_INFO("vue : %s personne",
                                cam.third_person ? "troisième" : "première");
                    }
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
                    appliquer_teinte_achetee(&rs);
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
                    appliquer_teinte_achetee(&rs);
                    ns_renderer_set_settings(rhi, renderer, &rs);
                    settings_banner = 2.6f;
                    NS_INFO("échelle de rendu : %.2f", (double)rs.render_scale);
                    break;
                }
                case SDLK_E:
                    /* Le jeton, décidé après la boucle : c'est aussi le geste du
                     * bouton d'action de la manette hors partie. */
                    if (!ev.key.repeat) want_interact = true;
                    break;
                case SDLK_F:
                    /*
                     * COGNER LA BORNE. « F » comme frapper, et la touche est
                     * libre : E met le jeton, R double la mise, C et Ctrl
                     * s'accroupissent, F2 et F5 à F10 sont les outils.
                     *
                     * Elle vaut EN PARTIE comme hors partie, et c'est tout
                     * l'intérêt : on rage sur la machine qui vient de nous
                     * tuer, pas sur celle d'à côté. Elle n'est donc pas dans
                     * le bloc qui rend le clavier au jeu — celui-ci ne réclame
                     * que l'espace, l'entrée et les flèches.
                     *
                     * Pas de répétition : maintenir la touche ne doit pas
                     * marteler. `room_viewmodel_frappe` refuse déjà pendant le
                     * geste, mais s'en remettre à ça ferait dépendre le rythme
                     * des coups de la durée d'une animation.
                     */
                    if (!ev.key.repeat) want_frappe = true;
                    break;
                case SDLK_R:
                    /* QUITTE OU DOUBLE. Décidé ici plutôt qu'après la boucle
                     * parce qu'il ne dépend d'aucune autre entrée : il n'y a
                     * qu'un état où il veut dire quelque chose, et c'est
                     * `room_economie` qui le connaît.
                     *
                     * Pas de touche pour REFUSER, et ce n'est pas un oubli : le
                     * refus est déjà l'action d'à côté — rejouer, repartir, ou
                     * ne rien faire. Lui donner sa propre touche demanderait au
                     * joueur de choisir entre deux gestes là où l'un des deux
                     * doit rester le chemin qu'on suit sans y penser. */
                    if (!ev.key.repeat) want_gamble = true;
                    break;
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

        /* ==================================================================
         * LES ENTRÉES DE L'IMAGE — clavier et manette, au même endroit
         * ==================================================================
         *
         * Tout ce qui suit s'exécute une fois par image, après que la file
         * d'événements est vide et AVANT que quoi que ce soit lise une entrée.
         * C'est la seule place où les deux périphériques peuvent se confondre
         * en un seul octet — et c'est cet octet-là que le journal d'entrées
         * écrit, que `--rejouer` relit et que le duel publie sur le réseau.
         */
        room_pad_sample(pad, &pad_state);
        const uint8_t pad_mask  = room_pad_mask(&pad_state, &pad_tune);
        const uint8_t pad_press = (uint8_t)(pad_mask & ~pad_prev);
        pad_prev = pad_mask;

        /*
         * LE MENU, À LA MANETTE.
         *
         * Il se parcourt avec les mêmes fronts montants que le jeu : un cran par
         * poussée. Pas de répétition automatique — celle du clavier vient du
         * système, on ne va pas en écrire une deuxième ici pour l'accorder
         * ensuite à la première.
         */
        if (menu.open) {
            if (pad_press & (1u << NS_GAME_UP))     room_menu_input(&menu, &menu_ctx, ROOM_MENU_UP);
            if (pad_press & (1u << NS_GAME_DOWN))   room_menu_input(&menu, &menu_ctx, ROOM_MENU_DOWN);
            if (pad_press & (1u << NS_GAME_LEFT))   room_menu_input(&menu, &menu_ctx, ROOM_MENU_LEFT);
            if (pad_press & (1u << NS_GAME_RIGHT))  room_menu_input(&menu, &menu_ctx, ROOM_MENU_RIGHT);
            if (pad_press & (1u << NS_GAME_ACTION)) room_menu_input(&menu, &menu_ctx, ROOM_MENU_ACCEPT);
        } else if (in_game) {
            /*
             * LES DEUX PÉRIPHÉRIQUES DANS LE MÊME OCTET.
             *
             * `frame_press` porte déjà les fronts du clavier. La manette y verse
             * les siens, et c'est tout : la décision qui suit ne sait plus, et
             * n'a plus à savoir, d'où vient l'appui.
             */
            frame_press |= pad_press;
            /*
             * LA CONSÉQUENCE DU COUP, et elle tient en une ligne.
             *
             * Pendant les 500 ms du geste, la main droite est SUR LA MACHINE et
             * pas sur les boutons : on jette donc les appuis. La partie, elle,
             * continue de tourner — c'est le seul point qui compte, et c'est ce
             * qui fait payer le coup au SCORE, donc aux tickets.
             *
             * Pourquoi pas un jeton retiré : `room_bareme.h` garantit un
             * plancher de cinq jetons au monnayeur, sans condition et sans
             * attente, et `room_economie.h` écrit qu'il n'y a « pas de minuterie
             * qui punit ». Un jeton retiré ne serait donc pas une perte mais un
             * aller-retour de 3,4 m. Le seul bien qu'on puisse vraiment perdre
             * ici est la partie en cours.
             */
            if (room_viewmodel_is_hitting(&vmstate)) frame_press = 0;
            if (frame_press) {
                /*
                 * Après la mort, l'action relance — mais seulement une fois la
                 * chute finie, sinon un appui maintenu au moment du choc
                 * redémarre avant qu'on ait vu ce qui s'est passé.
                 */
                float dead_time = 0.0f;
                const bool dead = game_api->dead(game, &dead_time);
                if (dead && (frame_press & (1u << NS_GAME_ACTION)) && dead_time > 0.8f) {
                    /*
                     * RELANCER EST UNE PARTIE, DONC UN JETON.
                     *
                     * Sans ça, la borne offrait une partie gratuite à qui reste
                     * devant elle — et le jeton n'aurait coûté qu'à celui qui
                     * marche. Le geste du bras n'est pas rejoué : on est déjà
                     * assis devant, la pièce est déjà dans la machine. C'est ce
                     * que fait une vraie borne quand on remet un crédit.
                     *
                     * ET C'EST LE REFUS DU QUITTE OU DOUBLE : appuyer sur
                     * action pour rejouer verse la mise. Refuser ne demande
                     * donc aucun geste de plus qu'accepter — il est même le
                     * geste qu'on ferait sans y penser.
                     */
                    if (room_eco_salle_offre()) room_eco_salle_refuser();
                    if (opt.autoplay || room_eco_salle_jeton()) {
                        /*
                         * LE JETON DE LA RELANCE S'ENTEND, ET SE VOIT.
                         *
                         * Il était débité en silence : les deux mains restaient
                         * sur les commandes et une partie neuve apparaissait.
                         * Le geste court fait l'aller-retour vers la fente en
                         * 400 ms, main droite seule, et son FRONT fait partir le
                         * son — voir `ROOM_VM_RELANCE`.
                         *
                         * `opt.autoplay` ne paie pas, donc il ne joue rien : une
                         * démonstration qui ferait tinter une pièce toutes les
                         * dix secondes mentirait sur ce qu'elle dépense.
                         */
                        if (!opt.autoplay && !room_viewmodel_relance(&vmstate)
                            && playing_cab) {
                            /* Le geste a été refusé — on cognait la borne à cet
                             * instant précis. Le jeton, lui, est bien parti : il
                             * doit s'entendre quand même, sinon on aurait payé
                             * sans rien voir NI rien entendre. */
                            room_sound_jeton_insere(&sound, playing_cab->coin_slot);
                        }
                        const uint64_t seed = room_eco_salle_graine(game_api->id);
                        start_run(game_api, game, runlog, seed, game_hard, opt.autoplay, &duel);
                        run_ms = 0;
                        run_tick = 0; pending_press = 0;
                    } else if (playing_cab) {
                        /* Plus de jetons : le monnayeur RECRACHE. C'est le seul
                         * retour que le joueur ait sur un appui qui n'a rien
                         * fait — le bandeau le dit, mais on regarde l'écran de
                         * la borne, pas le bandeau. */
                        room_sound_jeton_refuse(&sound, playing_cab->coin_slot);
                    }
                } else {
                    for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
                        if (frame_press & (1u << b)) game_api->press(game, (ns_game_button)b);
                    }
                    /* Les mêmes appuis, pour le journal d'entrées : ils arrivent
                     * AVANT le pas, et seront consommés par lui. */
                    pending_press |= frame_press;
                }
            }
        } else if (pad_press & (1u << NS_GAME_ACTION)) {
            /*
             * Hors partie, le bouton d'action MET LE JETON — c'est le « E » du
             * clavier. Une manette n'a pas de touche par verbe : le même bouton
             * lance la partie et la joue, comme sur une vraie borne où l'on
             * enfonce le jeton puis le bouton de tir avec le même pouce.
             */
            want_interact = true;
        }

        /*
         * ÉCHAP ET START — un seul geste, une seule décision.
         *
         * Trois issues selon l'état, et l'ordre compte : fermer le menu s'il est
         * ouvert, sinon quitter la partie s'il y en a une, sinon ouvrir le menu.
         * C'était écrit dans le gestionnaire de touche ; l'en sortir est ce qui
         * permet à la manette de faire exactement la même chose sans que ces
         * huit lignes existent deux fois.
         */
        if (want_menu) {
            if (menu.open) {
                room_menu_input(&menu, &menu_ctx, ROOM_MENU_CANCEL);
            } else if (in_game) {
                /* Quitter la partie rend la salle, pas le bureau. */
                in_game = false;
                playing_material = -1;
                playing_cab = NULL;
                room_viewmodel_stop_playing(&vmstate);
                NS_INFO("%s : score %u, meilleur %u", game_api->title,
                        game_api->score(game), game_api->best(game));
            } else {
                room_menu_open(&menu);
                if (mouse_captured) {
                    SDL_SetWindowRelativeMouseMode(ns_rhi_window(rhi), false);
                    mouse_captured = false;
                }
            }
        }

        /*
         * QUITTE OU DOUBLE : accepter relance la MÊME borne en régime
         * difficile, quel que soit celui de la partie d'origine. C'est ce qui
         * fait le risque, et c'est aussi ce qui rend la reprise comparable —
         * le score à battre est celui d'une partie qu'on rejoue plus dur.
         *
         * Le jeton de la reprise est dû comme celui de n'importe quelle partie.
         * S'il manque, l'offre RESTE sur la table : on n'a rien perdu, on va
         * chercher un jeton et on répond en revenant.
         */
        if (want_gamble && !menu.open && in_game && game_api->dead(game, NULL)
            && room_eco_salle_offre()) {
            if (room_eco_salle_jeton()) {
                if (room_eco_salle_accepter()) {
                    /* Même geste et même son que la relance ordinaire : c'est
                     * la même pièce dans la même fente. Ce qui distingue le
                     * quitte ou double est ce qu'on RISQUE, pas ce qu'on paie. */
                    if (!room_viewmodel_relance(&vmstate) && playing_cab) {
                        room_sound_jeton_insere(&sound, playing_cab->coin_slot);
                    }
                    game_hard = true;
                    const uint64_t seed = room_eco_salle_graine(game_api->id);
                    start_run(game_api, game, runlog, seed, game_hard, opt.autoplay, &duel);
                    run_ms = 0;
                    run_tick = 0; pending_press = 0;
                }
            } else if (playing_cab) {
                /* L'offre reste sur la table et on va chercher un jeton. Le
                 * refus s'entend : sans lui, appuyer sur « R » sans le sou ne
                 * produit rien du tout. */
                room_sound_jeton_refuse(&sound, playing_cab->coin_slot);
            }
        }

        /*
         * LE COUP, résolu avant le jeton parce qu'il ne coûte rien à décider :
         * il ne consulte ni l'économie, ni les comptoirs, ni le chargement d'un
         * jeu. `room_viewmodel_frappe` accepte une borne NULLE — on cogne alors
         * dans le vide, ce qui est le bon comportement quand on tape à côté.
         */
        if (want_frappe && !menu.open && cam.mode == ROOM_CAM_PLAYER) {
            const ns_cabinet *cible = room_viewmodel_target(&scene, &cam);
            room_viewmodel_frappe(&vmstate, cible);
        }

        if (want_interact && !menu.open) {
            /*
             * `ns_scene_nearest_cabinet` est écrite depuis M4 et n'avait
             * jamais eu un seul appelant — comme `screen_center`,
             * `player_anchor` et `ns_poi`. Elle en a un.
             */
            const ns_cabinet *near = room_viewmodel_target(&scene, &cam);

            /*
             * LES DEUX COMPTOIRS PASSENT AVANT, et seulement quand il n'y a pas
             * de borne : on ne se met pas devant un monnayeur pour jouer.
             * L'ordre est celui de l'invite affichée, sans quoi l'écran
             * promettrait une action et la touche en ferait une autre.
             */
            if (!near) {
                ns_v3 comptoir = cam.position;
                const ns_poi_kind k = eco_poi_kind(&scene, &cam, &comptoir);
                if (k == NS_POI_TOKENS) {
                    /*
                     * LE MONNAYEUR REND DES PIÈCES, ET ON LES ENTEND TOMBER.
                     *
                     * Autant de fois qu'il en rend, espacées : c'est
                     * l'intervalle qui fait entendre « cinq jetons » plutôt
                     * qu'un seul, plus épais. Zéro quand il n'a rien à donner,
                     * et il reste alors muet — un godet qui sonne à vide dirait
                     * qu'on vient de recevoir quelque chose.
                     *
                     * Le son part de l'ANCRE du meuble et non de l'œil : le
                     * godet est devant le joueur, à un mètre et demi au plus, et
                     * cinq pièces qui tomberaient dans sa tête ne viendraient de
                     * nulle part. La cote de l'ancre est celle du sol ; on la
                     * relève à hauteur de godet, qui est celle d'une main.
                     */
                    comptoir.y += 0.85f;
                    room_sound_jeton_bac(&sound, comptoir,
                                         (int)room_eco_salle_monnayeur());
                }
                else if (k == NS_POI_PRIZES) { room_eco_salle_vitrine(); }
            }

            /*
             * LE JETON EST DÉBITÉ AVANT LE GESTE, ET SEULEMENT S'IL AURA LIEU.
             *
             * Avant, parce que le bras insérerait sinon une pièce qu'on n'a
             * pas : le geste est joué, le son du monnayeur part, et la partie
             * ne démarre pas — le genre d'incohérence qu'on impute au moteur.
             *
             * Seulement s'il aura lieu, parce que `room_viewmodel_interact`
             * refuse tant qu'un geste est en cours (`room_viewmodel.c:270`) :
             * débiter d'abord et se faire refuser ensuite ferait payer un jeton
             * pour rien à chaque appui trop rapide. La condition est celle du
             * refus, lue au même endroit que lui.
             */
            if (near && vmstate.state == ROOM_VM_IDLE && !room_eco_salle_jeton()) {
                /* La borne ne prendra rien : on n'a pas de quoi. Le monnayeur
                 * RECRACHE, et c'est le seul retour audible d'un appui qui ne
                 * fait rien. */
                room_sound_jeton_refuse(&sound, near->coin_slot);
                near = NULL;
            }

            if (near && room_viewmodel_interact(&vmstate, near)) {
                /*
                 * LE SON NE PART PLUS D'ICI, et c'est tout l'objet du front.
                 *
                 * Il partait à l'appui sur « E », c'est-à-dire 722 ms avant que
                 * la pièce n'entre : le bras commençait à peine à se lever
                 * qu'on l'entendait déjà tomber. Il part maintenant du front
                 * `room_viewmodel_take_token`, consommé avec l'impact du coup
                 * de poing quelques centaines de lignes plus bas.
                 */
                NS_INFO("borne « %s » (%s) : jeton", near->name, near->game);

                /*
                 * La borne décide du jeu, et de sa difficulté. C'est ce
                 * que `salle.room.json` déclare depuis A4 et que rien ne
                 * lisait : dix-neuf bornes affectées à un jeu, et aucune
                 * qui en lançait un.
                 */
                if (LOAD_GAME(near->game)) {
                    /*
                     * LE RÉGIME, ET LE LOT QUI L'OUVRE PARTOUT.
                     *
                     * La borne déclare sa difficulté depuis A4 : six caissons
                     * portent « hard », les treize autres non. Le lot
                     * `REGIME DIFFICILE` ouvre le régime dur sur les dix-neuf,
                     * ce qui est la seule chose qu'un joueur puisse acheter et
                     * qui change ce qu'il JOUE — et qui paie 25 % de plus.
                     */
                    const bool hard = (SDL_strcasecmp(near->difficulty, "hard") == 0)
                                   || room_eco_lot_acquis(room_eco_salle(),
                                                          ROOM_ECO_LOT_DIFFICILE);
                    /* LA PARTIE DU JOUR : la graine vient de la date, pas de
                     * l'horloge à haute résolution. Voir `room_economie.h`. */
                    const uint64_t seed = room_eco_salle_graine(near->game);
                    game_hard = hard;
                    start_run(game_api, game, runlog, seed, hard, opt.autoplay, &duel);
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
                    if (room_poste_prendre(&poste, near, cam.position)) {
                        /* Le poste avance la vue : le tangage se prend DEPUIS
                         * lui, sinon la dalle monte vers le haut du cadre
                         * pendant toute l'avancée. */
                        look_pitch  = room_poste_tangage(&poste);
                        look_settle = 0.55f;
                    } else {
                        look_pitch  = pitch_onto(cam.position, near->screen_center);
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
            appliquer_teinte_achetee(&rs);
            ns_renderer_set_settings(rhi, renderer, &rs);
            NS_INFO("réglages : %s, échelle %.2f", quality_name(rs.quality),
                    (double)rs.render_scale);
        }
        /*
         * La FENÊTRE, appliquée ici pour la même raison que les réglages de
         * rendu juste au-dessus : recréer une swapchain à chaque flèche
         * maintenue ferait clignoter l'écran à chaque cran de la liste.
         */
        if (menu.window_dirty) {
            menu.window_dirty = false;
            ns_rhi_set_window_mode(rhi, menu_win_w, menu_win_h, menu_fullscreen);
            NS_INFO("fenêtre : %d x %d%s", menu_win_w, menu_win_h,
                    menu_fullscreen ? ", plein écran" : "");
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
        cam.running = keys[SDL_SCANCODE_LSHIFT] || pad_state.run;
        if (pad_state.jump && !pad_jump_prev) cam.jump_requested = true;
        pad_jump_prev = pad_state.jump;

        /*
         * LE STICK GAUCHE MARCHE, il ne se contente pas d'aller à fond.
         *
         * C'est la seule entrée de ce jeu qui soit vraiment analogique, et la
         * gâcher en la ramenant à quatre directions rendrait la manette moins
         * bonne que le clavier alors qu'elle peut être meilleure : on longe un
         * comptoir au pas, on s'approche d'une borne sans la dépasser.
         *
         * Il s'ADDITIONNE au clavier plutôt que de le remplacer — les deux sont
         * branchés en même temps, et rien n'oblige à en choisir un. La somme est
         * bornée parce que « clavier à fond + stick à fond » ne doit pas donner
         * une vitesse double ; `room_camera` normalise ensuite la direction.
         */
        {
            float pad_fwd = 0.0f, pad_strafe = 0.0f;
            room_pad_move(&pad_state, &pad_tune, &pad_fwd, &pad_strafe);
            cam.input_forward = ns_clampf(cam.input_forward + pad_fwd, -1.0f, 1.0f);
            cam.input_strafe  = ns_clampf(cam.input_strafe  + pad_strafe, -1.0f, 1.0f);
            /* La gâchette de course : le stick poussé à fond court, comme une
             * gâchette le ferait, sans réclamer un bouton de plus. */
            if (!cam.running) {
                const float mag2 = pad_fwd * pad_fwd + pad_strafe * pad_strafe;
                cam.running = (mag2 > 0.90f * 0.90f);
            }
        }

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
        cam.crouch_held = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_C]
                       || pad_state.crouch;
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
            /* La hauteur d'œil COURANTE, accroupissement compris : c'est elle
             * qui permet aux autres de poser mes pieds au sol au lieu de les
             * deviner. Voir `ns_realtime_peer.eye`. */
            ns_realtime_publish(cam.position.x, cam.position.y, cam.position.z,
                                cam.yaw, cam.eye_height,
                                here ? here->name : "",
                                (in_game && game_api) ? game_api->id
                                                      : (here ? here->game : ""),
                                (in_game && game_api && game)
                                    ? (int32_t)game_api->score(game) : 0);
        }

        /*
         * LES CORPS DES AUTRES, ALIMENTÉS.
         *
         * Deux sources possibles et une seule route : le réseau, ou les
         * marcheurs fabriqués de `--pairs-demo=`. Les seconds entrent par le
         * MÊME appel que les premiers, ce qui est tout l'intérêt — ce qu'une
         * capture montre est alors ce qu'un vrai pair produira, et non un rendu
         * de démonstration écrit à côté qui aurait sa propre vérité.
         *
         * `room_presence_sample` est idempotent : un lot déjà intégré ne fait
         * rien. On peut donc l'appeler à chaque image sans se demander si une
         * réponse est arrivée.
         *
         * LA DÉMONSTRATION REMPLACE LE RÉSEAU quand les deux sont demandés, et
         * ce n'est pas un oubli : mélanger seize marcheurs fabriqués aux vrais
         * pairs donnerait une salle dont on ne saurait plus dire qui est réel.
         * Le drapeau sert à photographier et à mettre au point ; il ne sert pas
         * à se faire de la compagnie pendant une vraie partie.
         */
        if (opt.demo_peers > 0) {
            /*
             * Un lot fabriqué au rythme du réseau, pas à celui de l'écran.
             * Fabriquer soixante lots par seconde ferait de l'interpolation un
             * calcul sans objet, et la capture ne montrerait plus le chemin que
             * les vrais pairs empruntent — c'est-à-dire pas le chemin qu'on
             * cherche à vérifier.
             */
            const uint64_t now = SDL_GetTicks();
            const uint64_t beat = (now / NS_RT_PERIOD_MS) * NS_RT_PERIOD_MS;
            ns_realtime_peer fake[NS_RT_MAX_PEERS];

            /*
             * L'AMORÇAGE : on rejoue le battement PRÉCÉDENT avant le premier.
             *
             * Sans lui, les marcheurs naissent à la première image et y sont
             * donc en plein fondu d'entrée. Une capture de huit images ne dure
             * pas les 250 ms qu'il faut pour le finir : les quatre corps
             * sortaient à demi transparents, ce qui n'est le rendu de rien du
             * tout. Deux lots plutôt qu'un leur donnent aussi un SEGMENT à
             * interpoler dès la première image, donc un cap déduit et une phase
             * de marche justes tout de suite.
             */
            if (!demo_amorce) {
                demo_amorce = true;
                const uint64_t before = (beat > NS_RT_PERIOD_MS)
                                      ? beat - NS_RT_PERIOD_MS : 0;
                const uint32_t n0 = room_presence_demo(fake, NS_RT_MAX_PEERS,
                                                       (uint32_t)opt.demo_peers,
                                                       (double)before * 0.001);
                room_presence_sample(&presence, fake, n0, before);
            }

            const uint32_t nf = room_presence_demo(fake, NS_RT_MAX_PEERS,
                                                   (uint32_t)opt.demo_peers,
                                                   (double)beat * 0.001);
            room_presence_sample(&presence, fake, nf, beat);
        } else if (ns_realtime_enabled()) {
            ns_realtime_peer peers[NS_RT_MAX_PEERS];
            uint64_t at_ms = 0;
            const uint32_t np = ns_realtime_peers(peers, NS_RT_MAX_PEERS, &at_ms);
            room_presence_sample(&presence, peers, np, at_ms);
        }

        /*
         * POSER LE REGARD EN ARRIVANT DEVANT UNE BORNE.
         *
         * C'est le second des deux moments qui manquaient, et le plus visible.
         * Le champ vertical vaut 62°, donc un demi-champ de 31,0° ; planté sur
         * l'ancre que la borne déclare, le centre de sa dalle est à 31,8° sous
         * l'horizon. Elle est DE HUIT DIXIÈMES DE DEGRÉ sous le bord bas du
         * cadre : on se plantait devant une borne et on ne voyait pas du tout
         * son écran. Ce qui occupait le milieu de l'image, à 48 cm de l'œil,
         * c'était le marquee.
         *
         * « la hauteur du personnage ne permet pas d'être bien positionné à
         * hauteur des bornes d'arcade » décrit exactement ça — et le défaut
         * n'est pas dans les cotes du meuble, qui sont toutes dans les plages
         * du matériel réel. Il est dans le fait que le moteur savait déjà
         * baisser les yeux, mais seulement au démarrage d'une partie : un
         * instant sur cinq.
         */
        if (!in_game && cam.mode == ROOM_CAM_PLAYER && !opt.play_at) {
            const ns_cabinet *front = room_viewmodel_target(&scene, &cam);
            if (front != gazed_at) {
                if (front && front->screen_material >= 0) {
                    look_pitch  = pitch_onto(cam.position, front->screen_center);
                    /* Plus lent qu'à l'entrée en partie : on s'approche, on ne
                     * s'assoit pas. Un demi-second se lit comme un regard qui
                     * descend, un tiers comme une caméra qu'on empoigne. */
                    look_settle = 0.50f;
                }
                gazed_at = front;
            }
        }

        /* Une séquence avance d'un pas CHOISI, jamais du temps qu'a pris le
         * rendu : voir `ns_clock_begin_frame_fixed`. */
        if (opt.sequence != NULL) {
            ns_clock_begin_frame_fixed(&clock, 1.0 / opt.sequence_fps);
        } else {
            ns_clock_begin_frame(&clock);
        }
        while (ns_clock_consume_tick(&clock)) {
            /*
             * Les portes AVANT la caméra, et ce n'est pas indifférent : la
             * capsule doit être repoussée par le vantail tel qu'il est À CE
             * PAS, pas tel qu'il était au précédent. Une porte qui se ferme à
             * 0,94 m/s parcourt 1,6 cm par pas de simulation ; l'inverser
             * laisserait le joueur d'autant à l'intérieur du panneau.
             */
            room_doors_tick(&doors, cam.position, (float)clock.tick_seconds);
            const room_blockers blockers = room_doors_blockers(&doors);

            /*
             * LE STICK DROIT REGARDE — en radians PAR SECONDE, et donc ici.
             *
             * La souris rend un déplacement : dix pixels sont dix pixels quelle
             * que soit la durée de l'image, et `room_camera` les intègre tels
             * quels. Un stick rend une POSITION maintenue ; ce qu'il commande
             * est une vitesse de rotation, qui doit donc être multipliée par le
             * temps écoulé. La confondre avec un déplacement de souris ferait
             * tourner la vue deux fois plus vite à 120 images par seconde qu'à
             * 60 — le genre de défaut qu'on impute à sa manette.
             *
             * D'où sa place DANS la boucle à pas fixe et non à côté : c'est le
             * seul endroit qui connaisse une durée.
             *
             * Écrit dans `yaw` et `pitch` plutôt que dans `mouse_dx` : la
             * sensibilité de la manette est la sienne, pas celle de la souris,
             * et passer par `mouse_dx` obligerait à diviser par une sensibilité
             * que le joueur peut régler à autre chose. Le tangage reste borné
             * par `room_camera_tick`, qui le rabote juste après.
             */
            if (!menu.open) {
                float dyaw = 0.0f, dpitch = 0.0f;
                room_pad_look(&pad_state, &pad_tune, (float)clock.tick_seconds,
                              &dyaw, &dpitch);
                cam.yaw   += dyaw;
                cam.pitch += dpitch;
            }

            room_camera_tick(&cam, &scene.bvh, &blockers, (float)clock.tick_seconds);

            /*
             * LE POSTE DE JEU, entretenu en UN SEUL endroit.
             *
             * Il aurait pu être pris et lâché aux cinq sites qui font commencer
             * et finir une partie ; ils sont cinq, et le cinquième aurait été
             * oublié — c'est déjà ce qui était arrivé au tangage, posé par
             * `--play-at` et pas par la touche E, c'est-à-dire pas par le chemin
             * que le joueur emprunte. Une condition relue à chaque pas ne peut
             * pas avoir de trou.
             *
             * Il se lâche donc tout seul : on quitte la partie, on ouvre le
             * menu, on passe en troisième personne, on recule d'un pas — et la
             * vue revient au corps sans que personne ait eu à y penser.
             */
            {
                const bool poste_voulu =
                       in_game && !fullscreen_game && playing_cab && !menu.open
                    && cam.mode == ROOM_CAM_PLAYER && !cam.third_person
                    && ns_v3_dist(cam.position, playing_cab->screen_center) < poste_portee;
                if (poste_voulu) {
                    /* Reprise SILENCIEUSE : on ne repose pas le tangage de
                     * quelqu'un qui vient de tourner la tête ou de reculer. Le
                     * regard n'est posé qu'à l'entrée en partie et à la mort. */
                    if (!poste.pris) (void)room_poste_prendre(&poste, playing_cab, cam.position);
                } else {
                    room_poste_lacher(&poste);
                }
                room_poste_tick(&poste, (float)clock.tick_seconds);
            }

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
             * L'IMPACT, consommé ICI et une seule fois.
             *
             * Les trois réactions partent du MÊME front : la vue encaisse, le
             * son part, la dalle retient qu'elle a été frappée. Calculées
             * séparément depuis l'état du bras, elles se décaleraient d'un pas
             * de simulation les unes des autres — et un choc dont le bruit
             * arrive huit millisecondes après l'image ne se lit plus comme un
             * choc.
             */
            {
                ns_v3 ou; int32_t quoi = -1;
                if (room_viewmodel_take_impact(&vmstate, &ou, &quoi)) {
                    room_camera_frappe(&cam);
                    /* Le son part du CENTRE DE LA DALLE quand il y a une borne,
                     * et de l'œil quand on cogne dans le vide : un choc sans
                     * cible n'a pas de position dans la salle, et le placer à
                     * l'origine le ferait venir d'un coin de la pièce. */
                    room_sound_frappe(&sound, (quoi >= 0) ? ou : cam.position);
                    choc_material = quoi;
                }
            }

            /*
             * LE JETON, consommé au même endroit et pour la même raison.
             *
             * Les deux gestes qui insèrent une pièce — la séquence complète et
             * le geste court de la relance — lèvent ce front à l'image exacte
             * où elle bascule dans le mécanisme, et il rend la fente visée.
             * Le son n'a donc rien à retrouver : il se place tout seul sur la
             * borne, y compris quand on a tourné la tête entre-temps.
             */
            {
                ns_v3 fente;
                if (room_viewmodel_take_token(&vmstate, &fente)) {
                    room_sound_jeton_insere(&sound, fente);
                }
            }
            /* Les dix-neuf démos avancent du même pas que la partie du joueur :
             * c'est la seule façon qu'elles aient la bonne vitesse quel que
             * soit le nombre d'images par seconde. */
            if (attract) {
                room_attract_tick(attract, (float)clock.tick_seconds,
                                  (in_game && !fullscreen_game) ? playing_material : -1);
            }

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
            intro_banner    = ns_maxf(0.0f, intro_banner - (float)clock.tick_seconds);
            /* Le bandeau de l'économie suit le même pas que les deux autres :
             * celui de la SIMULATION et non celui du mur, pour qu'une capture
             * rendue image par image le voie s'effacer au même rythme. */
            room_eco_salle_avancer((float)clock.tick_seconds);
            room_sound_update(&sound, &scene, &cam, &doors, (float)clock.tick_seconds);
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

                /*
                 * L'OCTET DES MAINTIENS — calculé UNE FOIS, ici, et pour tout le
                 * monde.
                 *
                 * Il l'était deux fois : une pour `held[]`, une pour `hmask`,
                 * vingt lignes plus bas, avec les mêmes touches recopiées. Rien
                 * n'obligeait les deux à rester d'accord, et c'est le genre de
                 * désaccord qu'aucun test ne voit : le jeu se joue normalement,
                 * et c'est le JOURNAL qui ment. Une manette branchée sur `held[]`
                 * seulement aurait produit exactement ça — des parties
                 * irrejouables et des duels qui divergent, sans une erreur.
                 *
                 * Le clavier et la manette entrent donc par la même porte, et
                 * `held[]` DESCEND du masque au lieu d'être calculé à côté. Il
                 * n'y a plus deux vérités à tenir d'accord, il n'y en a qu'une.
                 * `tests/test_pad.c` vérifie que les deux périphériques rendent
                 * bien le même octet à geste égal.
                 */
                const uint8_t hmask = (uint8_t)(room_keys_mask(keys) | pad_mask);

                /* Les maintiens : un jeu qui tourne à l'angle (le serpent) a
                 * besoin de savoir qu'on tient la direction, pas qu'on l'a
                 * pressée. Ceux qui n'en veulent pas laissent le pointeur nul. */
                if (game_api->hold) {
                    bool held[NS_GAME_BUTTON_COUNT] = { false };
                    for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
                        held[b] = (hmask & (1u << b)) != 0;
                    }
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
                    /*
                     * Un jeu sans `hold` n'a jamais reçu de maintiens : son
                     * journal n'en porte donc pas, et il ne doit pas commencer à
                     * en porter aujourd'hui — les journaux déjà enregistrés se
                     * rejoueraient autrement.
                     */
                    const uint8_t logged = game_api->hold ? hmask : 0u;
                    ns_runlog_input(runlog, run_tick, logged, pending_press);
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
                    /* Le duel EN DIRECT publie l'entrée de CE pas tout de
                     * suite : l'adversaire l'attend. Les EMPREINTES, elles,
                     * sont confrontées après le pas — voir plus bas. */
                    if (duel.live) {
                        ns_lockstep_send_input(duel.live, run_tick, logged, pending_press);
                    }

                    duel_tick(&duel, game_api, run_tick, (float)clock.tick_seconds);
                    run_tick++;
                }

                game_api->tick(game, (float)clock.tick_seconds);

                /*
                 * Les empreintes du duel EN DIRECT, prises APRÈS le pas.
                 *
                 * L'instant compte autant que la valeur, et le premier essai
                 * l'a payé : l'empreinte était publiée après avoir appliqué les
                 * appuis du pas mais AVANT de l'avancer, tandis que le fantôme
                 * était mesuré avant même ses appuis. Deux photographies prises
                 * à deux moments différents du même pas ne peuvent pas
                 * coïncider, et les deux clients annonçaient « divergence au
                 * pas 0 » dès la première seconde d'une partie parfaitement
                 * saine.
                 *
                 * « Après le pas » est le seul instant que les deux côtés
                 * peuvent nommer sans ambiguïté. Le fantôme, lui, est en retard
                 * de `NS_LOCKSTEP_DELAY` pas — on compare donc son pas à lui,
                 * pas le nôtre.
                 */
                if (duel.live && in_game) {
                    const int32_t done = run_tick - 1;
                    if (done >= 0 && (done % NS_LOCKSTEP_HASH_EVERY) == 0) {
                        const uint64_t h = ns_game_state_hash(game_api, game);
                        if (SDL_getenv("NINETEEN_DUEL_TRACE")) {
                            NS_INFO("TRACE moi pas %d : %016llx", done, (unsigned long long)h);
                        }
                        ns_lockstep_publish_hash(duel.live, done, h);
                    }
                    /*
                     * Le fantôme se compare à SON pas, celui qu'il a réellement
                     * rejoué — et non à « le nôtre moins le retard ».
                     *
                     * C'est la correction d'un défaut qui accusait le jeu d'une
                     * faute du RÉSEAU. Quand l'entrée de l'adversaire n'est pas
                     * arrivée, `duel_tick` rend la main sans rejouer : le
                     * fantôme prend du retard, et ce retard n'est pas de huit
                     * pas mais de huit PLUS le nombre de pas d'attente. En
                     * confrontant son état à l'empreinte publiée pour
                     * `nous − 8`, on comparait donc deux instants différents,
                     * et les deux clients annonçaient « divergence » sur une
                     * partie parfaitement saine.
                     *
                     * Mesuré : sans attract mode les images sont assez rapides
                     * pour qu'aucun pas ne stalle et le duel tenait ses quatre
                     * cents pas ; avec les dix-huit démos, une seule attente
                     * suffisait à rompre le duel au pas 32. Le défaut était là
                     * depuis le début — il fallait juste une image assez lente
                     * pour le révéler.
                     */
                    const int32_t g = duel.last_tick;
                    if (g >= 0 && duel.state && g != duel.live_verified &&
                        (g % NS_LOCKSTEP_HASH_EVERY) == 0) {
                        duel.live_verified = g;
                        const uint64_t gh = ns_game_state_hash(game_api, duel.state);
                        if (SDL_getenv("NINETEEN_DUEL_TRACE")) {
                            NS_INFO("TRACE fantome pas %d : %016llx (attentes %d)",
                                    g, (unsigned long long)gh, duel.live_stall);
                        }
                        ns_lockstep_verify_peer(duel.live, g, gh);
                    }
                }
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
                    /*
                     * Le troisième moment qui manquait : la partie est finie, et
                     * le tangage restait où le joueur l'avait laissé. On perd
                     * une partie en regardant ailleurs, et l'écran de fin — le
                     * score, le record, l'invite à recommencer — se joue hors
                     * cadre. On repose donc le regard sur la dalle, exactement
                     * comme à l'entrée.
                     */
                    if (playing_cab && !fullscreen_game && cam.mode == ROOM_CAM_PLAYER) {
                        look_pitch  = poste.pris
                                    ? room_poste_tangage(&poste)
                                    : pitch_onto(cam.position, playing_cab->screen_center);
                        look_settle = 0.45f;
                    }
                    last_rank = finish_run(runlog, run_ms, game_api, game,
                                           game_hard ? "hard" : "normal",
                                           opt.player, opt.offline, opt.autoplay);
                    /*
                     * LES TICKETS, juste après le classement et pour la même
                     * raison d'ordre : le score doit être arrêté avant qu'on le
                     * tarife. Un seul appel — c'est `room_economie` qui décide
                     * s'il verse, s'il propose le quitte ou double ou s'il
                     * résout celui qui était armé.
                     *
                     * PAS EN DÉMONSTRATION : `--autoplay` fait tourner les
                     * bornes toute la nuit pour les captures et l'intégration
                     * continue, et créditerait un portefeuille que personne n'a
                     * joué. `finish_run` écarte déjà ces parties du classement
                     * pour exactement ce motif.
                     */
                    if (!opt.autoplay) {
                        room_eco_salle_fin(game_api->id, game_hard,
                                           game_api->score(game));
                    }
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
                        start_run(game_api, game, runlog, demo_seed, game_hard, opt.autoplay, &duel);
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
            /*
             * Les sommets des vantaux qui ont bougé, poussés dès l'ouverture de
             * l'image.
             *
             * Ici et pas dans le pas fixe : `ns_rhi_stage_buffer` exige une image
             * commencée. Ici et pas plus tard non plus — les copies en attente
             * sont vidées par `ns_render_frame` avant sa première passe, donc
             * tout ce qui est mis en file après aurait une image de retard, ce
             * qui se verrait comme une porte qui traîne derrière son bruit.
             */
            room_doors_upload(&doors, rhi, &scene);

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

            /*
             * LE TABLEAU DU BAR : le classement et les joueurs EN DIRECT.
             *
             * Il portait `background_classement.png`, une image peinte de 2020
             * avec des scores dessinés dessus, et il sortait NOIR — mesuré :
             * cette texture a une réflectance de 0,0158, et `gbuffer.frag`
             * module l'émissif par l'albédo texel par texel. Aucune valeur
             * d'émissif ne rattrape un texel presque noir. C'était la plus
             * grande surface sombre du champ, juste sous l'enseigne.
             *
             * Rendu vivant, le problème disparaît par construction : ce qu'on
             * y dessine est clair, donc il s'allume.
             */
            if (sprites && bar_rt.handle && scene.scoreboard_material >= 0) {
                ns_sprite_begin(sprites, (float)ROOM_BAR_RT_W, (float)ROOM_BAR_RT_H);
                room_hud_draw_scoreboard(sprites, (float)ROOM_BAR_RT_W,
                                         (float)ROOM_BAR_RT_H, now,
                                         opt.player,
                                         (in_game && game_api) ? game_api->title : NULL,
                                         (in_game && game_api && game)
                                             ? game_api->score(game) : 0u);
                static const float off[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                ns_sprite_end(rhi, sprites, bar_rt.handle,
                              ROOM_BAR_RT_W, ROOM_BAR_RT_H, off);
                ns_renderer_set_screen(renderer, scene.scoreboard_material, bar_rt.handle);
            }

            if (in_game && !fullscreen_game && sprites && screen_rt.handle) {
                ns_sprite_begin(sprites, 512.0f, 288.0f);
                game_api->draw(sprites, game, game_art, 512.0f, 288.0f);
                /*
                 * LA DALLE ENCAISSE. Dessiné PAR-DESSUS le jeu et dans la même
                 * passe : c'est un défaut de l'écran, pas un élément du jeu, et
                 * il doit donc recouvrir ce que le jeu a dessiné.
                 *
                 * Seulement si c'est CETTE borne qu'on a frappée. Cogner la
                 * voisine ne doit pas faire dérailler la partie en cours — ce
                 * serait la seule façon de punir quelqu'un pour un coup qu'il
                 * n'a pas donné à cet écran-là.
                 */
                if (choc_material >= 0 && choc_material == playing_material) {
                    room_hud_draw_choc(sprites, 512.0f, 288.0f,
                                       room_viewmodel_choc(&vmstate, (float)clock.alpha),
                                       (float)now);
                }
                static const float off[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                ns_sprite_end(rhi, sprites, screen_rt.handle, 512, 288, off);
                ns_renderer_set_screen(renderer, playing_material, screen_rt.handle);
                if (SDL_getenv("NINETEEN_DUMP_SCREEN")) {
                    ns_rhi_capture_texture_png(rhi, screen_rt.handle, 512, 288,
                                               ns_rhi_swapchain_format(rhi),
                                               SDL_getenv("NINETEEN_DUMP_SCREEN"));
                }
            }

            /*
             * Les DÉMOS, après la partie du joueur et jamais avant : elles
             * doivent connaître la dalle qu'il occupe pour la laisser
             * tranquille. `playing_material` ne vaut quelque chose que si une
             * partie tourne dans une dalle — en plein écran il n'y en a pas, et
             * les dix-neuf bornes de la salle continuent de jouer derrière.
             */
            if (attract) {
                const int32_t taken = (in_game && !fullscreen_game) ? playing_material : -1;
                room_attract_draw(rhi, sprites, attract, renderer, taken);
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
                /* La séquence est écrite AVANT l'incrément, pour que la
                 * première image porte le numéro 0 sur les deux chemins de
                 * rendu — celui-ci et celui de la salle. */
                if (opt.sequence && !write_sequence_frame(rhi, target, (uint32_t)w, (uint32_t)h,
                                                          opt.sequence, frames_rendered)) {
                    running = false;
                }
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
                if (frames_rendered >= opt.frames && (opt.screenshot || opt.sequence || opt.headless)) {
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

            ns_camera render_cam = room_camera_resolve(&cam, &scene.bvh, (float)clock.alpha);
            /*
             * LE POSTE N'EST APPLIQUÉ QU'ICI, sur la caméra de RENDU.
             *
             * `cam.position` n'est pas touché, et c'est ce qui rend le poste
             * petit : la collision, le ramassage, le contrôle de portée,
             * l'écoute et les pas continuent de partir du CORPS. C'est la règle
             * que `room_camera.h` a posée pour la troisième personne, appliquée
             * dans l'autre sens — approcher la vue plutôt que la reculer.
             */
            render_cam.position = room_poste_oeil(&poste, render_cam.position,
                                                  (float)clock.alpha);
            render_cam.fov_y_degrees = room_poste_fov(&poste, render_cam.fov_y_degrees,
                                                      (float)clock.alpha);

            /*
             * TOUS LES CORPS DE L'IMAGE : le joueur, puis les pairs.
             *
             * Un seul tableau et un seul appel au rendu, parce que c'est le même
             * maillage pour tout le monde et qu'il ne monte au GPU qu'une fois —
             * voir `NS_MAX_CHARACTERS`, qui mesure ce que chaque corps ajoute.
             * Le compteur est remis à zéro à chaque image : un corps qui n'est
             * pas réinscrit disparaît, ce qui est exactement ce qu'on veut d'un
             * joueur qui s'en va.
             */
            poses_n = 0;

            /*
             * LA POSE DU PERSONNAGE.
             *
             * Sa phase d'animation vient de la DISTANCE PARCOURUE, pas du temps
             * — exactement comme l'oscillation de la vue et le balancement des
             * bras. C'est ce qui fait qu'un pas correspond à une foulée quelle
             * que soit l'allure : régler `personnage.vitesseMarche` dans
             * `nineteen.env` change la cadence des jambes sans qu'on ait rien
             * d'autre à toucher. Piloter par le temps donnerait un personnage
             * qui patine à basse vitesse et court sur place à haute.
             *
             * Le cycle importé fait une foulée complète — deux pas — sur sa
             * durée, et `VM_STRIDE` vaut 1,55 m pour la même chose : la phase
             * est donc le simple rapport des deux.
             */
            if (personnage && cam.third_person && cam.mode == ROOM_CAM_PLAYER) {
                const room_view_bob b = room_camera_bob(&cam, (float)clock.alpha);
                const float duree = ns_skin_duration(personnage);
                static float repos = 0.0f;
                /*
                 * LE DÉCALAGE ENTRE LA PHASE DE DISTANCE ET LA PHASE POSÉE.
                 *
                 * Il corrige un CLAQUEMENT qu'aucun réglage ne rattrapait, et
                 * qui ne se voit qu'en regardant les jambes au moment précis où
                 * l'on repart.
                 *
                 * L'entrée au repos était déjà continue : `repos` reprend la
                 * dernière phase de marche. La SORTIE ne l'était pas. Pendant
                 * l'arrêt, `repos` glisse vers la position de passage, tandis
                 * que la distance parcourue, elle, ne bouge plus. Au premier pas
                 * qui repart, la phase sautait donc de l'une à l'autre — et
                 * l'écart peut valoir une DEMI-DURÉE de cycle, soit une seconde
                 * sur les deux du modèle, c'est-à-dire un pas entier. Les deux
                 * jambes s'échangeaient en une image.
                 *
                 * Le décalage rend les deux sorties continues sans toucher à la
                 * CADENCE : il est constant pendant la marche, donc la phase
                 * avance toujours exactement comme la distance, et le patinage
                 * n'est ni amélioré ni aggravé. Il n'est mis à jour que pendant
                 * l'arrêt, c'est-à-dire quand la cadence ne veut rien dire.
                 */
                static float decalage = 0.0f;
                const float phase_distance =
                    (b.distance / room_camera_stride(&cam)) * duree;
                float when;
                if (b.amount > 0.02f) {
                    /*
                     * LA PHASE VIENT DE LA DISTANCE, et la foulée d'UN SEUL
                     * endroit.
                     *
                     * Le nombre 1,55 était écrit en dur ici, quatrième copie
                     * d'une constante que portent aussi `room_camera.c`,
                     * `room_viewmodel.c` et `room_sound.c`. Il est remplacé par
                     * l'accesseur : la valeur reste la même, mais elle n'a plus
                     * qu'une source, et `personnage.foulee` la règle sans
                     * recompiler.
                     *
                     * Que la phase suive la DISTANCE et non le temps est ce qui
                     * fait que la cadence suit l'allure — marche, course,
                     * accroupi — sans qu'il y ait un état d'animation par
                     * allure. C'était déjà juste, et c'est ce qui rend le cycle
                     * unique du modèle supportable.
                     */
                    when = phase_distance + decalage;
                    repos = when;
                } else {
                    /*
                     * À L'ARRÊT on RAMÈNE la phase vers la position de passage —
                     * l'instant du cycle où les deux pieds se croisent, jambes
                     * rassemblées. Elle est MESURÉE au chargement et non
                     * devinée : on balaie le cycle et on retient l'instant où
                     * les deux os les plus bas sont le plus proches.
                     *
                     * Geler la phase là où la marche s'est arrêtée laissait le
                     * personnage en grand écart — vu sur capture. Laisser
                     * tourner le cycle donnerait quelqu'un qui marche sur place,
                     * ce qui est pire et ne s'arrête jamais.
                     */
                    /*
                     * Amorti avec le pas d'AFFICHAGE, pas avec celui de la
                     * simulation : cette branche vit dans la boucle de rendu, et
                     * elle s'exécute donc une fois par image. Employer
                     * `tick_seconds` faisait dépendre la vitesse du retour au
                     * repos du nombre d'images par seconde — deux fois plus
                     * rapide sur un écran à 240 Hz que sur un 120 Hz, pour un
                     * mouvement censé durer une demi-seconde.
                     */
                    const float debout = ns_skin_stand_time(personnage);
                    repos = ns_damp(repos, debout, 6.0f, (float)clock.frame_seconds);
                    when = repos;
                    /* La marche reprendra EXACTEMENT ici. Ramené dans le cycle à
                     * chaque image : laissé libre, ce décalage dériverait avec
                     * la distance parcourue et finirait par perdre en précision
                     * de flottant ce qu'une phase d'animation ne peut pas se
                     * permettre de perdre. */
                    decalage = SDL_fmodf(repos - phase_distance, duree);
                }

                ns_character_draw d;
                SDL_zero(d);
                d.visible = true;
                d.joint_count = ns_skin_joint_count(personnage);

                /*
                 * LES DEUX ALLURES DÉRIVÉES, et pourquoi elles ne coûtent aucun
                 * état de plus.
                 *
                 * L'ACCROUPI se lit sur la hauteur d'œil, qui est DÉJÀ amortie
                 * entre debout et accroupi par `room_camera`. La fraction est
                 * donc continue et gratuite : la descente du personnage suit
                 * exactement la descente de la vue, image par image, sans qu'il
                 * y ait quoi que ce soit à interpoler ici. Recalculer un état
                 * « accroupi » à côté aurait fini par diverger de celui-là.
                 *
                 * LE BALANCEMENT reprend la phase de respiration de la caméra
                 * et son facteur de repos — les mêmes que la première personne
                 * emploie déjà pour faire respirer la vue. Les deux vues
                 * respirent donc ensemble, ce qui est le seul comportement
                 * défendable quand F10 bascule de l'une à l'autre.
                 */
                const float eye = ns_lerpf(cam.prev_eye_height, cam.eye_height,
                                           (float)clock.alpha);
                ns_skin_allure allure;
                SDL_zero(allure);
                const float plage = cam.eye_height_stand - cam.eye_height_crouch;
                if (plage > 1e-3f) {
                    allure.accroupi = ns_clampf((cam.eye_height_stand - eye) / plage,
                                                0.0f, 1.0f);
                }
                allure.souffle = b.breath;
                allure.souffle_force = 1.0f - ns_clampf(b.amount, 0.0f, 1.0f);
                /*
                 * LE COUP, sur la MÊME horloge que le bras de la première
                 * personne. F10 bascule d'une vue à l'autre en pleine partie :
                 * deux gestes qui ne dureraient pas pareil se verraient au
                 * basculement, et c'est le genre d'écart qu'on ne diagnostique
                 * qu'en le cherchant.
                 */
                allure.frappe = room_viewmodel_frappe_amount(&vmstate,
                                                             (float)clock.alpha);
                ns_skin_pose_allure(personnage, when, &allure, d.joint,
                                    NS_MAX_CHARACTER_JOINTS);

                /* L'échelle : mesurée et mise à l'échelle une fois, au
                 * chargement. Voir `echelle_perso`. */
                const float echelle = echelle_perso;

                /* Le lacet de la caméra : le personnage regarde là où le joueur
                 * regarde. */
                float dyaw = cam.yaw - cam.prev_yaw;
                while (dyaw >  NS_PI) dyaw -= NS_TAU;
                while (dyaw < -NS_PI) dyaw += NS_TAU;
                const float yaw = cam.prev_yaw + dyaw * (float)clock.alpha;

                /*
                 * LES PIEDS, sous le CORPS et INTERPOLÉS.
                 *
                 * Deux erreurs tenaient ici l'une dans l'autre. La première est
                 * réparée depuis : `render_cam.position` a reculé de deux mètres
                 * en troisième personne, il ne dit donc pas où se tient le
                 * joueur. La seconde restait : la position du corps était lue
                 * telle quelle, c'est-à-dire à l'état du DERNIER PAS DE
                 * SIMULATION, pendant que la caméra, elle, était interpolée. Le
                 * personnage avançait donc par saccades de 120 Hz devant une
                 * caméra fluide — un tremblement de deux centimètres et demi par
                 * pas à la course, exactement le genre de défaut qu'on attribue
                 * à « l'animation » sans le trouver.
                 *
                 * On interpole donc la position du corps comme tout le reste.
                 */
                const ns_v3 corps = ns_v3_lerp(cam.prev_position, cam.position,
                                               (float)clock.alpha);
                const ns_v3 sol = ns_v3_make(corps.x, corps.y - eye, corps.z);

                /*
                 * L'ORIENTATION, et le signe qui la rendait fausse.
                 *
                 * Le code tournait le modèle de `yaw` autour de la verticale. Ce
                 * n'est pas la bonne rotation, et le résultat n'était pas « un
                 * peu » décalé : la rotation employée envoie un vecteur local
                 * (x, z) sur son image tournée de MOINS l'angle, si bien que le
                 * regard du personnage sortait sur le MIROIR de celui de la
                 * caméra. À lacet nul, il marchait de profil, à quatre-vingt-dix
                 * degrés de la direction suivie. La capture le montre : de
                 * l'allée centrale on le voyait de côté, en crabe, alors qu'on
                 * devait le voir de dos.
                 *
                 * La bonne rotation se pose en une ligne dès qu'on écrit les
                 * angles. La rotation appliquée tourne l'horizontale de `-thêta`
                 * ; l'avant du modèle est à l'angle `phi` dans son repère, et on
                 * le veut à l'angle `yaw` dans le monde :
                 *
                 *      phi - thêta = yaw     donc     thêta = phi - yaw
                 *
                 * `phi` est MESURÉ sur le cycle — l'avant est l'opposé du recul
                 * du pied porteur — plutôt que supposé égal à la convention glTF.
                 * Sur ce modèle-ci la mesure donne 90,4 degrés, soit +Z à un
                 * demi-degré près, ce qui est la convention ; l'écart est le
                 * léger biais du cycle et non une erreur de mesure. Un modèle
                 * exporté autrement se posera droit tout seul.
                 */
                const float theta = ns_skin_forward_angle(personnage) - yaw;
                const ns_quat q = ns_quat_from_axis(ns_v3_make(0.0f, 1.0f, 0.0f), theta);
                d.model = ns_m4_trs(sol, q, ns_v3_splat(echelle));

                d.tint[0] = d.tint[1] = d.tint[2] = 1.0f;
                d.roughness = 0.72f;
                d.metallic = 0.0f;

                /*
                 * L'EFFACEMENT. Le bras de caméra est demandé à la caméra plutôt
                 * que recalculé : deux calculs du même recul se décaleraient
                 * d'une image et le personnage clignoterait. La dérivation des
                 * seuils est dans `room_camera.c`.
                 */
                d.opacity = room_camera_actor_opacity(&cam,
                                room_camera_third_arm(&cam, (float)clock.alpha));
                poses[poses_n++] = d;
            }

            /*
             * LES PAIRS, POSÉS COMME LE JOUEUR — le même maillage, le même
             * cycle, le même angle d'avant mesuré.
             *
             * Ce qui les distingue tient en une ligne : leur instant de cycle
             * vient de `room_presence`, qui l'a déduit de la distance qu'ils ont
             * réellement parcourue, tandis que celui du joueur vient de la
             * distance que la caméra a mesurée. Les deux répondent à la même
             * règle — la phase suit la distance — depuis deux sources
             * différentes, parce qu'on ne connaît d'un pair que des positions.
             *
             * Ils sont posés MÊME EN PREMIÈRE PERSONNE, et même en caméra libre :
             * c'est le joueur qu'on ne dessine pas quand il est dans sa propre
             * tête, pas les autres. Un pair reste un objet du monde.
             *
             * Aucune allure n'est appliquée : on ne sait pas si un pair est
             * accroupi (`eye` le dirait, mais son cycle serait tout de même celui
             * d'une marche debout), et le balancement de repos demanderait une
             * phase de respiration par pair pour un mouvement de deux
             * centimètres à trois mètres. La pose de passage, elle, est bien là :
             * un pair à l'arrêt se rassemble au lieu de rester en grand écart.
             */
            if (personnage) {
                const uint32_t nb = room_presence_step(&presence, SDL_GetTicks(),
                                                       (float)clock.frame_seconds);
                for (uint32_t i = 0; i < nb && poses_n < NS_MAX_CHARACTERS; ++i) {
                    const room_presence_body *pb = &presence.body[i];

                    ns_character_draw pd;
                    SDL_zero(pd);
                    pd.visible = true;
                    pd.joint_count = ns_skin_joint_count(personnage);
                    ns_skin_pose_allure(personnage, pb->cycle_time, NULL, pd.joint,
                                        NS_MAX_CHARACTER_JOINTS);

                    const float th = ns_skin_forward_angle(personnage) - pb->yaw;
                    const ns_quat pq = ns_quat_from_axis(ns_v3_make(0.0f, 1.0f, 0.0f), th);
                    pd.model = ns_m4_trs(pb->feet, pq, ns_v3_splat(echelle_perso));

                    pd.tint[0] = pd.tint[1] = pd.tint[2] = 1.0f;
                    pd.roughness = 0.72f;
                    pd.metallic = 0.0f;
                    /* Le fondu d'entrée et de sortie, calculé une fois par
                     * `room_presence` et porté aussi par l'étiquette : les deux
                     * s'effacent ensemble ou l'un survit à l'autre. */
                    pd.opacity = pb->opacity;
                    poses[poses_n++] = pd;
                }
            }

            ns_renderer_set_characters(renderer, poses, poses_n);

            /* Les bras : posés par room_viewmodel, jamais en caméra libre. */
            /*
             * LES BRAS SUIVENT LA VUE, et il le faut.
             *
             * Ils sont posés depuis l'ŒIL : les épaules y sont accrochées, les
             * poignets visent les commandes EN MONDE. Avancer la caméra sans
             * avancer les épaules les laisserait derrière le plan de coupe
             * proche — on verrait deux avant-bras flotter sans corps, ce qui est
             * exactement le défaut que la troisième personne a mis un an à
             * découvrir chez elle.
             *
             * On lui passe donc une caméra dont les DEUX pas de simulation sont
             * décalés, et non un œil déjà interpolé : `room_viewmodel_pose`
             * interpole lui-même, et interpoler deux fois fait dériver les bras
             * d'une image à chaque changement de vitesse.
             */
            room_camera cam_bras = cam;
            cam_bras.position      = room_poste_oeil_pas(&poste, cam.position, true);
            cam_bras.prev_position = room_poste_oeil_pas(&poste, cam.prev_position, false);
            room_viewmodel_pose(&vmstate, &cam_bras, (float)clock.alpha, &viewmodel);
            /*
             * ET JAMAIS EN TROISIÈME PERSONNE.
             *
             * Les bras du viewmodel sont posés dans le repère de la VUE, à
             * quarante centimètres devant l'objectif : ils sont faits pour être
             * regardés depuis l'intérieur du crâne. Le point de vue reculé de
             * deux mètres, ils flottent en travers de l'image, détachés du
             * personnage qui a les siens.
             *
             * Le défaut existait depuis le premier jour de la troisième
             * personne et il était CACHÉ par le défaut voisin : la caméra était
             * si près du personnage que son épaule masquait l'avant-bras. Le
             * corriger l'a découvert — on le voit sur la capture d'après, en
             * tube sombre à gauche, et sur celle d'avant on le prenait pour
             * l'épaule.
             *
             * `room_viewmodel_pose` est quand même appelé : c'est lui qui fait
             * AVANCER l'interpolation des bras, et le sauter les figerait le
             * temps d'un aller-retour en F10.
             */
            const bool bras_visibles = (cam.mode == ROOM_CAM_PLAYER) && !cam.third_person;
            const ns_viewmodel_pose *vm = bras_visibles ? &viewmodel : NULL;
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
                /*
                 * L'aide d'arrivée n'a de sens qu'en mode joueur : en caméra
                 * libre ou en orbite, on cadre une capture, on ne cherche pas
                 * où aller. Et pas non plus sous le menu — l'affichage est
                 * dessiné AVANT lui, et le bandeau se voyait donc dépasser du
                 * cadre des réglages : vu sur capture, corrigé ici plutôt que
                 * dans `room_hud`, qui n'a pas à connaître le menu.
                 */
                hud.intro_timer = (cam.mode == ROOM_CAM_PLAYER && !menu.open)
                                ? intro_banner : 0.0f;

                /* L'ÉCONOMIE. Quatre champs, tous en lecture : le portefeuille
                 * et le bandeau appartiennent à `room_economie`, et l'affichage
                 * ne possède rien de ce qu'il montre. Le lieu à portée est
                 * calculé par la même fonction que la touche « E » plus haut,
                 * pour que l'invite ne puisse pas promettre autre chose que ce
                 * que l'appui fera. */
                hud.eco = room_eco_salle();
                hud.poi = eco_poi_kind(&scene, &cam, NULL);
                hud.eco_message = room_eco_salle_message();
                hud.eco_message_timer = room_eco_salle_message_reste();

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
                /* Les marcheurs fabriqués ont droit aux mêmes étiquettes que les
                 * vrais : sans elles, la capture ne montrerait qu'une moitié de
                 * ce qu'un pair produit. */
                if (ns_realtime_enabled() || opt.demo_peers > 0) {
                    if (!fullscreen_game) {
                        draw_presence(sprites, &presence, &render_cam,
                                      (h > 0) ? (float)w / (float)h : 1.777f);
                    }
                    draw_presence_roster(sprites, &presence);
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
            if (opt.sequence && !write_sequence_frame(rhi, target, (uint32_t)w, (uint32_t)h,
                                                      opt.sequence, frames_rendered)) {
                running = false;
            }
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
                /*
                 * LE BRAS DE CAMÉRA sur la capture, en clair.
                 *
                 * Sans cette ligne, une capture de troisième personne sans
                 * personnage pose une question qu'on ne peut pas trancher en la
                 * regardant : est-il effacé parce que la caméra est contre lui,
                 * ou pas dessiné du tout ? C'est exactement la question qui a
                 * fait perdre du temps sur `--view=allee`, et deux nombres y
                 * répondent.
                 */
                if (cam.third_person && cam.mode == ROOM_CAM_PLAYER) {
                    const float bras = room_camera_third_arm(&cam, (float)clock.alpha);
                    NS_INFO("capture : troisième personne, bras %.2f m sur %.2f voulus, "
                            "épaule %.2f m, personnage à %.0f %% d'opacité",
                            (double)bras, (double)cam.third_distance,
                            (double)cam.third_side,
                            (double)(room_camera_actor_opacity(&cam, bras) * 100.0f));
                }
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
            /* `--sequence` compte ses images comme `--headless` : sans ça, une
             * séquence lancée avec une fenêtre tournerait sans fin en écrivant
             * des PNG jusqu'à remplir le disque. */
            if ((opt.headless || opt.sequence) && frames_rendered >= opt.frames) {
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

    room_attract_destroy(rhi, attract);
    ns_skin_free(personnage);
    ns_texture_destroy(rhi, &bar_rt);
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
    room_eco_salle_fermer();
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
    /* La manette avant SDL_Quit : `SDL_CloseGamepad` sur un sous-système déjà
     * arrêté n'est pas défini, et le vérificateur de fuites juste en dessous
     * compterait une poignée jamais rendue. */
    if (pad) { SDL_CloseGamepad(pad); pad = NULL; }
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
