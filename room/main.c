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

#include "room_camera.h"

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
        "  --quality=Q          low | medium | high | ultra (défaut high)\n"
        "  --camera=M           player | free | orbit (défaut player)\n"
        "  --angle=F            angle de départ de la caméra orbite, en degrés\n"
        "  --pos=X,Y,Z          place la caméra à un point précis (mode libre)\n"
        "  --yaw=D --pitch=D    orientation en degrés\n"
        "  --exposure=F         exposition du tone mapping (défaut 1.15)\n"
        "  --debug=VUE          affiche une cible intermédiaire : albedo, normal,\n"
        "                       emissive, depth, visibility, hdr, bloom\n"
        "  --debug-gpu          active les couches de validation du pilote\n"
        "  --help               affiche ce message\n",
        NINETEEN_VERSION, exe);
}

static bool parse_options(int argc, char **argv, options *o)
{
    SDL_zerop(o);
    o->frames = 4;
    o->width = 1600;
    o->height = 900;
    o->vsync = true;
    o->quality = NS_QUALITY_HIGH;
    o->camera_mode = ROOM_CAM_PLAYER;
    o->render_scale = 1.0f;

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
        } else if (SDL_strcmp(a, "--debug-gpu") == 0) {
            o->debug_gpu = true;
        } else if (SDL_strncmp(a, "--debug=", 8) == 0) {
            o->debug_view = a + 8;
        } else if (SDL_strncmp(a, "--pos=", 6) == 0) {
            o->position = a + 6; o->has_view = true;
        } else if (SDL_strncmp(a, "--yaw=", 6) == 0) {
            o->yaw = (float)SDL_atof(a + 6) * NS_DEG2RAD; o->has_view = true;
        } else if (SDL_strncmp(a, "--pitch=", 8) == 0) {
            o->pitch = (float)SDL_atof(a + 8) * NS_DEG2RAD; o->has_view = true;
        } else if (SDL_strncmp(a, "--exposure=", 11) == 0) {
            o->exposure = (float)SDL_atof(a + 11);
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

/* Monte les répertoires de données. En développement, l'arbre de build est
 * prioritaire ; en paquet installé, seul le répertoire du binaire existe. */
static void mount_asset_directories(void)
{
#ifdef NINETEEN_BUILD_ASSET_DIR
    ns_paths_mount(NINETEEN_BUILD_ASSET_DIR);
#endif
    const char *env = SDL_getenv("NINETEEN_ASSETS");
    if (env && *env) ns_paths_mount(env);
}

int main(int argc, char **argv)
{
    options opt;
    if (!parse_options(argc, argv, &opt)) return 0;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
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

    ns_scene scene;
    if (!ns_scene_load(rhi, &scene, "scene/salle.gltf")) {
        NS_FATAL("la salle n'a pas pu être chargée");
        ns_renderer_destroy(rhi, renderer);
        ns_rhi_destroy(rhi);
        SDL_Quit();
        return 1;
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

    room_camera cam;
    room_camera_init(&cam,
                     ns_v3_make(centre.x, floor_y + 1.68f, centre.z + extent.z * 0.28f),
                     -90.0f * NS_DEG2RAD);
    cam.mode = opt.camera_mode;
    cam.orbit_angle = opt.camera_angle;

    /* La caméra d'orbite reste DANS la salle : elle tourne autour du centre à
     * un rayon inférieur au demi-côté, à hauteur d'homme un peu surélevée.
     * Une orbite extérieure ne montrerait que la face arrière des murs. */
    cam.orbit_center = ns_v3_make(centre.x, floor_y, centre.z);
    cam.orbit_radius = ns_minf(extent.x, extent.z) * 0.30f;
    cam.orbit_height = 2.3f;

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

    ns_clock clock;
    ns_clock_init(&clock, NS_DEFAULT_TICK_HZ);

    bool running = true;
    bool mouse_captured = false;
    int frames_rendered = 0;

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
                case SDLK_F5:
                    cam.mode = (cam.mode == ROOM_CAM_FREE) ? ROOM_CAM_PLAYER : ROOM_CAM_FREE;
                    NS_INFO("caméra : %s", cam.mode == ROOM_CAM_FREE ? "libre" : "joueur");
                    break;
                case SDLK_F6:
                    cam.mode = ROOM_CAM_ORBIT;
                    NS_INFO("caméra : orbite");
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

        /* Clavier lu en continu plutôt qu'en événements : on veut l'état, pas
         * les transitions. Les touches ZQSD sont acceptées en plus de WASD,
         * comme dans la version d'origine. */
        const bool *keys = SDL_GetKeyboardState(NULL);
        cam.input_forward = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] ? 1.0f : 0.0f)
                          - (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f);
        cam.input_strafe  = (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f)
                          - (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
        cam.input_up      = (keys[SDL_SCANCODE_SPACE] ? 1.0f : 0.0f)
                          - (keys[SDL_SCANCODE_LCTRL] ? 1.0f : 0.0f);
        cam.running = keys[SDL_SCANCODE_LSHIFT];

        ns_clock_begin_frame(&clock);
        while (ns_clock_consume_tick(&clock)) {
            room_camera_tick(&cam, (float)clock.tick_seconds);
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

            const ns_camera render_cam = room_camera_resolve(&cam, (float)clock.alpha);
            ns_renderer_draw(rhi, renderer, &scene, &render_cam, target, w, h, now);
            ns_rhi_end_frame(rhi);
            frames_rendered++;

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

    NS_INFO("arrêt après %d images (%.1f images/s en moyenne)",
            frames_rendered, clock.fps_smoothed);

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
