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

#include "flappy/flappy.h"

#include "room_camera.h"
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
        "  --scale=F            échelle de rendu interne, 0.4 à 2.0 (défaut 1.0)\n"
        "  --fullscreen         plein écran\n"
        "  --no-vsync           désactive la synchronisation verticale\n"
        "  --quality=Q          low | medium | high | ultra (défaut medium)\n"
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
        "  --debug=VUE          affiche une cible intermédiaire : albedo, normal,\n"
        "                       emissive, depth, visibility, hdr, bloom\n"
        "  --bench              mesure le temps GPU réel de chaque image\n"
        "  --game=NOM           démarre directement dans un mini-jeu (flappy)\n"
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
        if (SDL_strcmp(v, "low") == 0)         o->quality = NS_QUALITY_LOW;
        else if (SDL_strcmp(v, "medium") == 0) o->quality = NS_QUALITY_MEDIUM;
        else if (SDL_strcmp(v, "high") == 0)   o->quality = NS_QUALITY_HIGH;
        else if (SDL_strcmp(v, "ultra") == 0)  o->quality = NS_QUALITY_ULTRA;
        else NS_WARN("NINETEEN_QUALITY=%s inconnu — ignoré", v);
    }
    if ((v = SDL_getenv("NINETEEN_WIDTH")) != NULL)     o->width = SDL_atoi(v);
    if ((v = SDL_getenv("NINETEEN_HEIGHT")) != NULL)    o->height = SDL_atoi(v);
    if ((v = SDL_getenv("NINETEEN_SCALE")) != NULL)     o->render_scale = (float)SDL_atof(v);
    if ((v = SDL_getenv("NINETEEN_EXPOSURE")) != NULL)  o->exposure = (float)SDL_atof(v);
    if ((v = SDL_getenv("NINETEEN_VSYNC")) != NULL)     o->vsync = (SDL_atoi(v) != 0);
    if ((v = SDL_getenv("NINETEEN_FULLSCREEN")) != NULL) o->fullscreen = (SDL_atoi(v) != 0);
    if ((v = SDL_getenv("NINETEEN_ROOM")) != NULL)      o->room = v;
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
    o->render_scale = 1.0f;
    o->particles = -1.0f;       /* < 0 : on garde la densité du palier de qualité */

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
        } else if (SDL_strncmp(a, "--quality=", 10) == 0) {
            const char *q = a + 10;
            if (SDL_strcmp(q, "low") == 0)         o->quality = NS_QUALITY_LOW;
            else if (SDL_strcmp(q, "medium") == 0) o->quality = NS_QUALITY_MEDIUM;
            else if (SDL_strcmp(q, "high") == 0)   o->quality = NS_QUALITY_HIGH;
            else if (SDL_strcmp(q, "ultra") == 0)  o->quality = NS_QUALITY_ULTRA;
            else { fprintf(stderr, "qualité inconnue : %s\n", q); return false; }
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

int main(int argc, char **argv)
{
    options opt;
    if (!parse_options(argc, argv, &opt)) return 0;

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

    ns_render_settings rs;
    ns_render_settings_defaults(&rs, opt.quality);
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
    flappy_art flappy_assets;
    flappy game;
    bool in_game = false;
    if (sprites) flappy_art_load(rhi, &flappy_assets);
    else         SDL_zero(flappy_assets);

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
    }
    /* La borne devant laquelle on joue, et son matériau de dalle. */
    int32_t playing_material = -1;
    bool    fullscreen_game = false;

    /* Les trois sons du jeu, ceux de 2020. Le jeu ne connaît pas le mixeur : il
     * lève des drapeaux d'événement, et c'est ici qu'on les entend. */
    const int sfx_flap  = ns_audio_load("games/flappy/flap.wav");
    const int sfx_hurt  = ns_audio_load("games/flappy/hurt.wav");
    const int sfx_score = ns_audio_load("games/flappy/score.wav");

    if (opt.game && sprites) {
        if (SDL_strcasecmp(opt.game, "flappy") == 0) {
            flappy_reset(&game, (uint64_t)SDL_GetPerformanceCounter(), false);
            in_game = true;
            fullscreen_game = true;      /* `--game=` est le mode plein écran */
            NS_INFO("Flappy Bird : partie démarrée");

        } else {
            NS_WARN("--game=%s inconnu (flappy)", opt.game);
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
             * On recule de 35 cm par rapport à l'ancre déclarée. L'ancre place le
             * joueur à 70 cm de la dalle — la bonne distance pour ATTEINDRE les
             * boutons, pas pour VOIR la borne : à 70 cm le caisson remplit tout
             * le cadre et l'écran sort par le bas. Un joueur recule d'ailleurs
             * naturellement dès qu'il ne tend plus le bras.
             */
            const ns_v3 back = ns_v3_scale(pick->screen_normal, 0.35f);
            cam.position = cam.prev_position = ns_v3_make(pick->player_anchor.x + back.x,
                                                          pick->player_anchor.y + cam.eye_height,
                                                          pick->player_anchor.z + back.z);
            /* Regarder la borne : la normale d'écran pointe vers le joueur, donc
             * on regarde dans le sens opposé. Le lacet suit
             * `forward = (cos, ., sin)`. */
            cam.yaw = cam.prev_yaw = atan2f(-pick->screen_normal.z, -pick->screen_normal.x);
            /* La dalle est à 1,22 m, l'œil à 1,70 m, à 1,05 m de distance : il faut
             * plonger de 22° pour l'avoir au centre du cadre. */
            cam.pitch = cam.prev_pitch = -22.0f * NS_DEG2RAD;
            cam.velocity = ns_v3_zero();

            const bool hard = (SDL_strcasecmp(pick->difficulty, "hard") == 0);
            flappy_reset(&game, 20240418, hard);
            in_game = true;
            fullscreen_game = false;
            playing_material = pick->screen_material;
            NS_INFO("borne « %s » (%s, %s) : partie dans la dalle, matériau %d",
                    pick->name, pick->game, hard ? "hard" : "normal", pick->screen_material);
        }
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
        for (int k = 0; k < steps; ++k) {
            if (opt.autoplay) flappy_autopilot(&game);
            flappy_tick(&game, step);
        }
        NS_INFO("Flappy Bird : %.1f s avancées (%d pas), score %u",
                (double)opt.warmup, steps, game.score);
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
                switch (ev.key.key) {
                case SDLK_ESCAPE:
                    if (in_game) {
                        /* Quitter la partie rend la salle, pas le bureau. */
                        in_game = false;
                        playing_material = -1;
                        NS_INFO("Flappy Bird : score %u, meilleur %u", game.score, game.best);
                        break;
                    }
                    if (mouse_captured) {
                        SDL_SetWindowRelativeMouseMode(ns_rhi_window(rhi), false);
                        mouse_captured = false;
                    } else {
                        running = false;
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
                case SDLK_SPACE:
                    if (in_game) {
                        /* En partie, l'espace bat des ailes. Après la mort il
                         * relance — mais seulement une fois la chute finie,
                         * sinon un appui maintenu au moment du choc redémarre
                         * avant qu'on ait vu ce qui s'est passé. */
                        if (!ev.key.repeat) {
                            if (game.phase == FLAPPY_DEAD && game.dead_time > 0.8f) {
                                const uint32_t best = game.best;
                                flappy_reset(&game, (uint64_t)SDL_GetPerformanceCounter(),
                                             game.hard);
                                game.best = best;
                            } else {
                                flappy_flap(&game);
                            }
                        }
                        break;
                    }
                    /* Le saut est une transition, pas un état : lu en événement
                     * pour qu'un appui bref ne se perde pas entre deux pas. */
                    if (!ev.key.repeat) cam.jump_requested = true;
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
                        if (sprites && SDL_strcasecmp(near->game, "flappy") == 0) {
                            const bool hard = (SDL_strcasecmp(near->difficulty, "hard") == 0);
                            flappy_reset(&game, (uint64_t)SDL_GetPerformanceCounter(), hard);
                            in_game = true;
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
        const bool *keys = SDL_GetKeyboardState(NULL);
        cam.input_forward = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] ? 1.0f : 0.0f)
                          - (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f);
        cam.input_strafe  = (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f)
                          - (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
        cam.running = keys[SDL_SCANCODE_LSHIFT];

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

        ns_clock_begin_frame(&clock);
        while (ns_clock_consume_tick(&clock)) {
            room_camera_tick(&cam, &scene.bvh, (float)clock.tick_seconds);
            room_viewmodel_tick(&vmstate, &cam, (float)clock.tick_seconds);
            room_sound_update(&sound, &scene, &cam, (float)clock.tick_seconds);
            ns_renderer_tick_particles(renderer, (float)clock.tick_seconds);

            /*
             * Le jeu avance au MÊME pas fixe que la salle. C'est ce qui le rend
             * reproductible à la graine près — donc scriptable pour les captures
             * — et c'est précisément ce que la version de 2020 ne pouvait pas
             * faire : elle intégrait en nombre d'images, à 60 Hz supposés.
             */
            if (in_game) {
                if (opt.autoplay) flappy_autopilot(&game);
                flappy_tick(&game, (float)clock.tick_seconds);
                if (game.flapped)    ns_audio_play(sfx_flap, NS_BUS_SFX, 0.55f, 1.0f);
                if (game.scored_now) { ns_audio_play(sfx_score, NS_BUS_SFX, 0.7f, 1.0f);
                                       NS_INFO("Flappy : %u", game.score); }
                if (game.died_now)   { ns_audio_play(sfx_hurt, NS_BUS_SFX, 0.8f, 1.0f);
                                       NS_INFO("Flappy : perdu à %u", game.score); }
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
            if (in_game && !fullscreen_game && sprites && screen_rt.handle) {
                ns_sprite_begin(sprites, 512.0f, 288.0f);
                flappy_draw(sprites, &game, &flappy_assets, 512.0f, 288.0f);
                static const float off[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                ns_sprite_end(rhi, sprites, screen_rt.handle, 512, 288, off);
                ns_renderer_set_screen(renderer, playing_material, screen_rt.handle);
                if (SDL_getenv("NINETEEN_DUMP_SCREEN")) {
                    ns_rhi_capture_texture_png(rhi, screen_rt.handle, 512, 288,
                                               ns_rhi_swapchain_format(rhi),
                                               SDL_getenv("NINETEEN_DUMP_SCREEN"));
                }
            } else {
                ns_renderer_set_screen(renderer, -1, NULL);
            }

            if (in_game && fullscreen_game && sprites) {
                /*
                 * En partie, le jeu occupe l'écran. Le repère logique est fixé à
                 * la hauteur du terrain (1080) et la largeur suit l'aspect de la
                 * fenêtre : le jeu garde donc ses proportions sur un 16/9 comme
                 * sur un 4/3, et c'est lui qui se centre.
                 */
                const float aspect = (float)w / (float)ns_maxf(1.0f, (float)h);
                ns_sprite_begin(sprites, FLAPPY_H * aspect, FLAPPY_H);
                flappy_draw(sprites, &game, &flappy_assets, FLAPPY_H * aspect, FLAPPY_H);
                static const float night[4] = { 0.02f, 0.02f, 0.03f, 1.0f };
                ns_sprite_end(rhi, sprites, target, w, h, night);
                ns_rhi_end_frame(rhi);
                frames_rendered++;
                if (opt.screenshot && frames_rendered >= opt.frames) {
                    ns_rhi_capture_texture_png(rhi, target, w, h,
                                               ns_rhi_swapchain_format(rhi), opt.screenshot);
                    NS_INFO("capture : Flappy Bird, score %u, %u quads en %u lot(s)",
                            game.score, ns_sprite_quad_count(sprites),
                            ns_sprite_batch_count(sprites));
                    ns_texture_destroy(rhi, &offscreen);
                    running = false;
                }
                continue;
            }

            const ns_camera render_cam = room_camera_resolve(&cam, (float)clock.alpha);
            /* Les bras : posés par room_viewmodel, jamais en caméra libre. */
            room_viewmodel_pose(&vmstate, &cam, (float)clock.alpha, &viewmodel);
            const ns_viewmodel_pose *vm = (cam.mode == ROOM_CAM_PLAYER) ? &viewmodel : NULL;
            ns_renderer_draw(rhi, renderer, &scene, &render_cam, vm, target, w, h, now);
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

            /* `--bench` sans capture doit s'arrêter aussi : sans cette
             * condition la boucle tournait indéfiniment, et la mesure ne
             * revenait jamais. */
            if (!opt.screenshot && opt.bench && frames_rendered >= opt.frames) {
                running = false;
            }
            if (opt.screenshot && frames_rendered >= opt.frames) {
                ns_rhi_capture_texture_png(rhi, target, w, h, ns_rhi_swapchain_format(rhi),
                                           opt.screenshot);
                const ns_render_stats st = ns_renderer_stats(renderer);
                NS_INFO("capture : %u lots dessinés, %u éliminés, %u triangles, %u lumières",
                        st.batches_drawn, st.batches_culled, st.triangles, st.lights_active);
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
    flappy_art_free(rhi, &flappy_assets);
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
