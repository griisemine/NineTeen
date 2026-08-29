#include "room_menu.h"

#include "ns_audio.h"
#include "ns_config.h"
#include "ns_core.h"
#include "ns_math.h"
#include "ns_realtime.h"
#include "room_credits.h"
#include "room_hud.h"
#include "room_sound.h"

#include <SDL3/SDL.h>
#include <math.h>

/* --------------------------------------------------------------------------
 * Les entrées
 * --------------------------------------------------------------------------
 * Une table, pas une cascade de `if`. Chaque ligne sait lire sa valeur, la
 * déplacer d'un cran et l'écrire en toutes lettres — donc ajouter un réglage
 * demande une ligne, et le dessin n'a rien à savoir de ce qu'il affiche.
 * -------------------------------------------------------------------------- */

typedef enum menu_item {
    MI_QUALITY = 0,
    MI_SCALE,
    MI_PARTICLES,
    MI_EXPOSURE,
    /*
     * La FENÊTRE, sous les réglages d'image et au-dessus du son.
     *
     * Ici et pas en tête : `QUALITE` et `ECHELLE DE RENDU` sont ce qu'on vient
     * chercher quand ça rame, c'est-à-dire dans neuf ouvertures du menu sur dix,
     * et les repousser d'un cran pour deux réglages qu'on touche une fois par
     * installation serait le mauvais échange. Ici et pas en fin de liste non
     * plus : ce sont des réglages d'IMAGE, et les mettre après les volumes
     * obligerait à traverser le son pour les trouver.
     *
     * Conséquence utile : les quatre premières lignes ne bougent pas, et
     * `--menu=2` continue de cadrer « POUSSIERE » comme dans la documentation.
     */
    MI_RESOLUTION,
    MI_FULLSCREEN,
    MI_VOL_MASTER,
    MI_VOL_MUSIC,
    MI_VOL_SFX,
    MI_VOL_AMBIENCE,
    /* Les deux niveaux de la salle sont placés SOUS les quatre bus, et pas
     * ailleurs : ce sont des sous-réglages de « EFFETS » et de « AMBIANCE », et
     * l'ordre de l'écran doit dire cette dépendance. Mettre « PAS » avant
     * « VOLUME GENERAL » laisserait croire qu'il s'y soustrait. */
    MI_VOL_STEPS,
    MI_VOL_TONE,
    MI_MOUSE,
    MI_REALTIME,
    /*
     * Les deux pages d'information, JUSTE AVANT les deux boutons.
     *
     * Cette place n'est pas indifférente. « CREDITS » est l'endroit où se tient
     * l'attribution de CesiumMan, qui est une obligation de licence et non une
     * courtoisie (voir `room_credits.h`) : la mettre en fin de liste la rend
     * introuvable, la mettre en tête ferait passer les crédits avant les
     * réglages, ce qu'aucun joueur n'attend. Juste au-dessus de « REPRENDRE »,
     * elle est la dernière chose qu'on lit en parcourant le menu — donc vue.
     *
     * Elles restent AU-DESSUS des deux boutons pour une raison plus terre à
     * terre : `tests/test_menu.c` vise « REPRENDRE » et « QUITTER » par
     * `n - 2` et `n - 1`. Insérer ici ne déplace pas ce que ce test croit
     * savoir ; insérer après le déplacerait en silence.
     */
    MI_CONTROLS,
    MI_CREDITS,
    MI_RESUME,
    MI_QUIT,
    MI_COUNT
} menu_item;

static const char *const g_label[MI_COUNT] = {
    "QUALITE",
    "ECHELLE DE RENDU",
    "POUSSIERE",
    "LUMINOSITE",
    "DEFINITION",
    "PLEIN ECRAN",
    "VOLUME GENERAL",
    "MUSIQUE",
    "EFFETS",
    "AMBIANCE",
    "PAS",
    "FOND DE SALLE",
    "SOURIS",
    "TEMPS REEL",
    "COMMANDES",
    "CREDITS",
    "REPRENDRE",
    "QUITTER LE JEU",
};

int room_menu_row(const char *label)
{
    if (!label) return -1;
    for (int i = 0; i < MI_COUNT; ++i) {
        if (SDL_strcmp(g_label[i], label) == 0) return i;
    }
    return -1;
}

static const char *quality_word(ns_quality q)
{
    switch (q) {
        case NS_QUALITY_POTATO: return "POTATO";
        case NS_QUALITY_LOW:    return "BASSE";
        case NS_QUALITY_MEDIUM: return "MOYENNE";
        case NS_QUALITY_HIGH:   return "HAUTE";
        case NS_QUALITY_ULTRA:  return "ULTRA";
    }
    return "MOYENNE";
}

/*
 * Le coût mesuré de chaque palier, affiché À CÔTÉ du nom.
 *
 * C'est le seul chiffre qui aide vraiment à choisir, et le laisser hors de
 * l'écran revient à demander au joueur d'essayer les cinq à l'aveugle.
 *
 * Ces valeurs-ci sont mesurées sur un **vrai GPU** — Apple M1, Metal, cible
 * 1600 x 900, `--bench` sur 40 images, minimum de trois exécutions, sur les
 * vues `allee` et `bar`. Les précédentes venaient du rastériseur LOGICIEL du
 * conteneur de développement, avec la note « c'est le rapport entre paliers
 * qui transporte, pas la milliseconde ». Cette note était fausse, et c'est
 * mesuré :
 *
 *     palier    annonce (logiciel)   mesure (M1)
 *     potato          x0.17             x0.30
 *     low             x0.42             x0.31
 *     medium          x1.00             x1.00
 *     high            x1.56             x1.71
 *     ultra           x3.92             x6.20
 *
 * L'écart le plus utile au joueur n'est pas l'ultra : c'est que **potato et low
 * coûtent la même chose** sur du vrai matériel, là où le logiciel les séparait
 * d'un facteur 2,5. Ce qui les distingue — brouillard volumétrique, occlusion
 * ambiante, poussière — ne pèse presque rien à côté de ce qui reste commun,
 * l'écriture du G-buffer et une passe d'éclairage à quarante-sept sources sur
 * chaque pixel. Personne ne devrait donc choisir `potato` : il coûte autant que
 * `low` et rend moins.
 */
static const char *quality_hint(ns_quality q)
{
    switch (q) {
        case NS_QUALITY_POTATO: return "x0.30";
        case NS_QUALITY_LOW:    return "x0.31";
        case NS_QUALITY_MEDIUM: return "x1.00";
        case NS_QUALITY_HIGH:   return "x1.71";
        case NS_QUALITY_ULTRA:  return "x6.20";
    }
    return "";
}

/*
 * LES DÉFINITIONS PROPOSÉES.
 *
 * Une liste et non un champ libre : un menu qui se parcourt à la manette ou aux
 * flèches ne sait pas saisir « 1728 x 1117 », et une définition tapée de travers
 * donne une fenêtre qu'on ne peut plus redimensionner pour rejoindre le menu.
 *
 * Toutes en 16/9 sauf la première. `1280 x 800` est là pour les portables 16/10,
 * qui sont redevenus la moitié du marché ; sans elle, la plus petite définition
 * proposée laisse deux bandes noires sur ces écrans-là. Le reste couvre du 720p
 * au 4K — au-delà, personne ne joue en fenêtré.
 *
 * Une définition qui ne serait dans aucune de ces cases — celle du fichier de
 * configuration, ou `--width/--height` — s'affiche telle quelle et le premier
 * cran la ramène dans la liste. On ne l'ÉCRASE pas au premier affichage : un
 * menu qui change un réglage rien qu'en s'ouvrant est un menu dont on se méfie.
 */
static const struct { int w, h; } g_resolutions[] = {
    { 1280,  720 },
    { 1280,  800 },
    { 1366,  768 },
    { 1600,  900 },
    { 1920, 1080 },
    { 2560, 1440 },
    { 3840, 2160 },
};
#define RES_COUNT ((int)(sizeof g_resolutions / sizeof g_resolutions[0]))

/* L'indice de la définition courante, ou −1 si elle n'est dans aucune case. */
static int resolution_index(const room_menu_ctx *ctx)
{
    if (!ctx->window_w || !ctx->window_h) return -1;
    for (int i = 0; i < RES_COUNT; ++i) {
        if (g_resolutions[i].w == *ctx->window_w && g_resolutions[i].h == *ctx->window_h) return i;
    }
    return -1;
}

static float bus_get(ns_audio_bus b) { return ns_audio_ready() ? ns_audio_bus_volume(b) : 0.0f; }

static void bus_step(ns_audio_bus b, int dir)
{
    if (!ns_audio_ready()) return;
    const float v = ns_clampf(ns_audio_bus_volume(b) + (float)dir * 0.05f, 0.0f, 1.0f);
    ns_audio_set_bus_volume(b, v);
}

static void pct(char *out, size_t n, float v)
{
    SDL_snprintf(out, n, "%d%%", (int)(v * 100.0f + 0.5f));
}

/* Écrit la valeur de l'entrée `i` dans `out`, et son indice éventuel dans
 * `hint` (chaîne vide s'il n'y en a pas). */
static void item_value(const room_menu_ctx *ctx, int i, char *out, size_t n,
                       char *hint, size_t hn)
{
    hint[0] = '\0';
    switch (i) {
        case MI_QUALITY:
            SDL_snprintf(out, n, "%s", quality_word(ctx->rs->quality));
            SDL_snprintf(hint, hn, "%s", quality_hint(ctx->rs->quality));
            break;
        case MI_SCALE:
            SDL_snprintf(out, n, "%.2f", (double)ctx->rs->render_scale);
            /* Le gain de pixels est exact et vaut la peine d'être dit : c'est le
             * levier le moins coûteux en netteté. */
            if (ctx->rs->render_scale < 0.995f) {
                const float p = ctx->rs->render_scale * ctx->rs->render_scale;
                SDL_snprintf(hint, hn, "-%d%% PIXELS", (int)((1.0f - p) * 100.0f + 0.5f));
            }
            break;
        case MI_PARTICLES:
            if (ctx->rs->particle_density <= 0.001f) SDL_snprintf(out, n, "AUCUNE");
            else pct(out, n, ctx->rs->particle_density);
            break;
        case MI_EXPOSURE:   SDL_snprintf(out, n, "%.2f", (double)ctx->rs->exposure); break;
        case MI_RESOLUTION:
            if (!ctx->window_w || !ctx->window_h) { SDL_snprintf(out, n, "-"); break; }
            SDL_snprintf(out, n, "%d x %d", *ctx->window_w, *ctx->window_h);
            /*
             * En plein écran, la ligne dit à quoi elle sert.
             *
             * Sans ce mot, elle affiche une définition qui n'est PAS celle de
             * l'image qu'on regarde, et le joueur conclut que le réglage ne
             * prend pas. C'est la définition qu'on retrouvera en ressortant.
             */
            if (ctx->fullscreen && *ctx->fullscreen) SDL_snprintf(hint, hn, "EN FENETRE");
            break;
        case MI_FULLSCREEN:
            SDL_snprintf(out, n, "%s",
                         (ctx->fullscreen && *ctx->fullscreen) ? "OUI" : "NON");
            break;
        case MI_VOL_MASTER: pct(out, n, bus_get(NS_BUS_MASTER)); break;
        case MI_VOL_MUSIC:  pct(out, n, bus_get(NS_BUS_MUSIC)); break;
        case MI_VOL_SFX:    pct(out, n, bus_get(NS_BUS_SFX)); break;
        case MI_VOL_AMBIENCE: pct(out, n, bus_get(NS_BUS_AMBIENCE)); break;
        /* Ces deux-là gardent leur vraie valeur même sans sortie audio, à la
         * différence des quatre bus au-dessus. Ce ne sont pas des états du
         * mixeur — ce sont des préférences de la salle, écrites dans
         * `settings.cfg` et relues au prochain démarrage. Les afficher « - » sur
         * une machine sans carte son reviendrait à dire qu'elles sont perdues,
         * alors qu'elles sont gardées ; c'est aussi ce qui les rend vérifiables
         * sans périphérique par `tests/test_menu.c`. */
        case MI_VOL_STEPS:  pct(out, n, room_sound_get_level(ROOM_LEVEL_STEPS)); break;
        case MI_VOL_TONE:   pct(out, n, room_sound_get_level(ROOM_LEVEL_TONE)); break;
        case MI_MOUSE:      SDL_snprintf(out, n, "%.2f", (double)*ctx->mouse_sensitivity); break;
        case MI_REALTIME:
            /*
             * L'INTERRUPTEUR du temps réel — présence dans la salle et duels.
             *
             * Il montre trois états et non deux, parce qu'il y a trois
             * situations et que les confondre rendrait le réglage incompréhensible :
             *
             *   ACTIF    — demandé, et le fil tourne ;
             *   SANS SERVEUR — demandé, mais aucune URL n'est configurée (ou
             *                  `--offline` verrouille). Le joueur a dit oui et
             *                  il ne se passe rien : il faut le lui DIRE, sans
             *                  quoi il croira à une panne ;
             *   INACTIF  — non demandé, ce qui est le défaut.
             *
             * Le troisième état est ce qui manquait à la première version : elle
             * affichait « ACTIF » dès que la case était cochée, y compris quand
             * rien ne pouvait démarrer.
             */
            /*
             * `realtime` peut être NUL : un appelant qui ne pilote pas ce
             * réglage — `tests/test_menu.c`, qui construit son contexte avec
             * les seuls champs dont il a besoin — ne doit pas faire tomber le
             * menu. Une ligne qui ne sait rien affiche « INACTIF » plutôt que
             * de déréférencer.
             */
            if (!ctx->realtime || !*ctx->realtime) {
                SDL_snprintf(out, n, "INACTIF");
            } else if (ns_realtime_enabled()) {
                SDL_snprintf(out, n, "ACTIF");
                SDL_snprintf(hint, hn, "%s", ns_realtime_status());
            } else {
                SDL_snprintf(out, n, "SANS SERVEUR");
                SDL_snprintf(hint, hn, "AU PROCHAIN LANCEMENT");
            }
            break;
        default:            out[0] = '\0'; break;
    }
    if (!ns_audio_ready() && i >= MI_VOL_MASTER && i <= MI_VOL_AMBIENCE) {
        SDL_snprintf(out, n, "-");
        SDL_snprintf(hint, hn, "PAS DE SORTIE AUDIO");
    }
}

/*
 * Une ligne qui AGIT, par opposition à une ligne qui se règle.
 *
 * Les deux pages d'information en sont : elles n'ont pas de valeur, les flèches
 * n'y font donc rien et aucun chevron ne s'affiche. Les oublier ici les aurait
 * dessinées avec un « < » et un « > » qui ne mènent nulle part, ce qui est la
 * façon la plus sûre de faire douter d'un menu.
 */
static bool item_is_button(int i)
{
    return i == MI_RESUME || i == MI_QUIT || i == MI_CONTROLS || i == MI_CREDITS;
}

/* Déplace l'entrée `i` d'un cran. Renvoie true si le rendu doit être réappliqué. */
static bool item_step(room_menu *m, const room_menu_ctx *ctx, int i, int dir)
{
    switch (i) {
        case MI_QUALITY: {
            int q = (int)ctx->rs->quality + dir;
            if (q < NS_QUALITY_POTATO) q = NS_QUALITY_ULTRA;
            if (q > NS_QUALITY_ULTRA)  q = NS_QUALITY_POTATO;
            /* Changer de palier reprend TOUS les défauts du palier — sauf les
             * trois réglages qui ont leur propre ligne dans ce menu. Sans ça,
             * régler la poussière puis changer de palier l'effacerait sans le
             * dire, ce qui est la façon la plus sûre de rendre un menu suspect. */
            const float keep_scale = ctx->rs->render_scale;
            const float keep_part  = ctx->rs->particle_density;
            const float keep_expo  = ctx->rs->exposure;
            ns_render_settings_defaults(ctx->rs, (ns_quality)q);
            ctx->rs->render_scale     = keep_scale;
            ctx->rs->particle_density = keep_part;
            ctx->rs->exposure         = keep_expo;
            return true;
        }
        case MI_SCALE:
            ctx->rs->render_scale = ns_clampf(ctx->rs->render_scale + (float)dir * 0.05f,
                                              0.50f, 1.00f);
            return true;
        case MI_PARTICLES:
            ctx->rs->particle_density = ns_clampf(ctx->rs->particle_density + (float)dir * 0.1f,
                                                  0.0f, 2.0f);
            return true;
        case MI_EXPOSURE:
            ctx->rs->exposure = ns_clampf(ctx->rs->exposure + (float)dir * 0.05f, 0.60f, 2.00f);
            return true;
        case MI_RESOLUTION: {
            if (!ctx->window_w || !ctx->window_h) return false;
            const int cur = resolution_index(ctx);
            /*
             * Depuis une définition hors liste, le premier cran atterrit sur la
             * PREMIÈRE case — et non « à côté de la plus proche ». Chercher la
             * plus proche demanderait une distance dans un espace à deux
             * dimensions dont personne n'a la même idée, et rendrait le geste
             * imprévisible pour gagner un cran.
             */
            int next = (cur < 0) ? 0 : cur + dir;
            if (next < 0)          next = RES_COUNT - 1;
            if (next >= RES_COUNT) next = 0;
            *ctx->window_w = g_resolutions[next].w;
            *ctx->window_h = g_resolutions[next].h;
            m->window_dirty = true;
            /* Le rendu N'EST PAS à réappliquer : les cibles hors écran suivent
             * la taille de la fenêtre à l'image suivante, comme elles le font
             * déjà quand on tire la poignée. */
            return false;
        }
        case MI_FULLSCREEN:
            if (!ctx->fullscreen) return false;
            /* Une bascule ignore le SENS : gauche et droite font la même chose
             * sur une valeur qui n'en a que deux, et exiger « droite pour oui »
             * serait une règle de plus à deviner. */
            *ctx->fullscreen = !*ctx->fullscreen;
            m->window_dirty = true;
            return false;
        case MI_VOL_MASTER:   bus_step(NS_BUS_MASTER, dir);   return false;
        case MI_VOL_MUSIC:    bus_step(NS_BUS_MUSIC, dir);    return false;
        case MI_VOL_SFX:      bus_step(NS_BUS_SFX, dir);      return false;
        case MI_VOL_AMBIENCE: bus_step(NS_BUS_AMBIENCE, dir); return false;
        /* Même pas de 5 % que les bus : deux familles de volumes qui se règlent
         * par crans différents rendent le menu imprévisible sous le pouce. */
        case MI_VOL_STEPS:
            room_sound_set_level(ROOM_LEVEL_STEPS,
                                 room_sound_get_level(ROOM_LEVEL_STEPS) + (float)dir * 0.05f);
            return false;
        case MI_VOL_TONE:
            room_sound_set_level(ROOM_LEVEL_TONE,
                                 room_sound_get_level(ROOM_LEVEL_TONE) + (float)dir * 0.05f);
            return false;
        case MI_MOUSE:
            *ctx->mouse_sensitivity = ns_clampf(*ctx->mouse_sensitivity + (float)dir * 0.05f,
                                                0.20f, 3.00f);
            return false;
        case MI_REALTIME:
            /*
             * Le choix est enregistré tout de suite et prend effet au prochain
             * lancement. On ne démarre ni n'arrête le fil ici : ce menu ne
             * possède aucun sous-système, il écrit des valeurs — c'est ce qui le
             * garde dessinable et testable sans fenêtre, et la règle est déjà
             * celle des autres lignes.
             */
            if (ctx->realtime) *ctx->realtime = !*ctx->realtime;
            return false;
        default: break;
    }
    (void)m;
    return false;
}

/* --------------------------------------------------------------------------
 * Cycle de vie
 * -------------------------------------------------------------------------- */

void room_menu_open(room_menu *m)
{
    m->open = true;
    m->time = 0.0f;
    /* On rouvre TOUJOURS sur les réglages. Rouvrir sur la page où l'on était
     * parti demanderait au joueur de se souvenir de ce qu'il a fait la fois
     * d'avant pour comprendre ce qu'il voit. */
    m->page = ROOM_MENU_PAGE_SETTINGS;
    m->render_dirty = m->window_dirty = false;
    m->close_request = m->quit_request = false;
    if (m->cursor < 0 || m->cursor >= MI_COUNT) m->cursor = 0;
}

void room_menu_close(room_menu *m)
{
    m->open = false;
    m->page = ROOM_MENU_PAGE_SETTINGS;
    m->close_request = false;
}

void room_menu_update(room_menu *m, float dt) { if (m->open) m->time += dt; }

void room_menu_input(room_menu *m, const room_menu_ctx *ctx, room_menu_action a)
{
    if (!m->open) return;

    /*
     * Sur une page d'information, TOUT ramène aux réglages.
     *
     * Y compris les flèches, et c'est délibéré. Une page qui se lit d'un coup
     * d'œil n'a rien à parcourir ; laisser les flèches déplacer un curseur
     * invisible dans le menu qu'on ne voit plus donnerait un joueur qui revient
     * sur une ligne qu'il n'a pas choisie. Le seul geste possible est donc de
     * revenir, quelle que soit la touche — ce qui est aussi ce qu'on fait
     * instinctivement devant un écran dont on a fini la lecture.
     *
     * Échap ne ferme PAS le menu depuis ici : il remonte d'un cran. Fermer
     * ferait sortir du menu quelqu'un qui voulait seulement quitter les
     * crédits, et lui ferait rouvrir Échap pour retrouver ses réglages.
     */
    if (m->page != ROOM_MENU_PAGE_SETTINGS) {
        m->page = ROOM_MENU_PAGE_SETTINGS;
        return;
    }

    switch (a) {
        case ROOM_MENU_UP:
            m->cursor = (m->cursor + MI_COUNT - 1) % MI_COUNT;
            break;
        case ROOM_MENU_DOWN:
            m->cursor = (m->cursor + 1) % MI_COUNT;
            break;
        case ROOM_MENU_LEFT:
            if (!item_is_button(m->cursor) && item_step(m, ctx, m->cursor, -1))
                m->render_dirty = true;
            break;
        case ROOM_MENU_RIGHT:
            if (!item_is_button(m->cursor) && item_step(m, ctx, m->cursor, +1))
                m->render_dirty = true;
            break;
        case ROOM_MENU_ACCEPT:
            if (m->cursor == MI_QUIT)        m->quit_request = true;
            else if (m->cursor == MI_RESUME) m->close_request = true;
            else if (m->cursor == MI_CONTROLS) m->page = ROOM_MENU_PAGE_CONTROLS;
            else if (m->cursor == MI_CREDITS)  m->page = ROOM_MENU_PAGE_CREDITS;
            /* Sur une ligne de réglage, Entrée avance d'un cran : c'est ce que
             * fait la main quand on ne sait pas encore que ce sont les flèches. */
            else if (item_step(m, ctx, m->cursor, +1)) m->render_dirty = true;
            break;
        case ROOM_MENU_CANCEL:
            m->close_request = true;
            break;
    }
}

void room_menu_persist(const room_menu_ctx *ctx)
{
    const char *q = "medium";
    switch (ctx->rs->quality) {
        case NS_QUALITY_POTATO: q = "potato"; break;
        case NS_QUALITY_LOW:    q = "low";    break;
        case NS_QUALITY_MEDIUM: q = "medium"; break;
        case NS_QUALITY_HIGH:   q = "high";   break;
        case NS_QUALITY_ULTRA:  q = "ultra";  break;
    }
    ns_config_set_str(NS_CFG_QUALITY, q);
    ns_config_set_float(NS_CFG_RENDER_SCALE, ctx->rs->render_scale);
    ns_config_set_float(NS_CFG_MOUSE_SENS, *ctx->mouse_sensitivity);
    /* Hors du garde `ns_audio_ready`, délibérément : ces deux-là n'ont pas
     * besoin du mixeur pour exister. Régler les pas sur une machine muette puis
     * retrouver le réglage sur une machine sonore est le comportement attendu ;
     * les mettre sous le garde les effacerait silencieusement. */
    ns_config_set_float(ROOM_CFG_VOL_STEPS, room_sound_get_level(ROOM_LEVEL_STEPS));
    ns_config_set_float(ROOM_CFG_VOL_TONE,  room_sound_get_level(ROOM_LEVEL_TONE));

    /* Le temps réel est un RÉGLAGE PERSISTANT, pas un état de session : on ne
     * doit pas avoir à repasser `--temps-reel` à chaque lancement. */
    if (ctx->realtime) ns_config_set_bool(NS_CFG_REALTIME, *ctx->realtime);

    /* La fenêtre. `main.c` relit ces trois clés au démarrage depuis toujours ;
     * ce qui manquait, c'était quelqu'un pour les écrire. */
    if (ctx->window_w)   ns_config_set_int(NS_CFG_WINDOW_W, *ctx->window_w);
    if (ctx->window_h)   ns_config_set_int(NS_CFG_WINDOW_H, *ctx->window_h);
    if (ctx->fullscreen) ns_config_set_bool(NS_CFG_FULLSCREEN, *ctx->fullscreen);
    if (ns_audio_ready()) {
        ns_config_set_float(NS_CFG_VOL_MASTER,   ns_audio_bus_volume(NS_BUS_MASTER));
        ns_config_set_float(NS_CFG_VOL_MUSIC,    ns_audio_bus_volume(NS_BUS_MUSIC));
        ns_config_set_float(NS_CFG_VOL_SFX,      ns_audio_bus_volume(NS_BUS_SFX));
        ns_config_set_float(NS_CFG_VOL_AMBIENCE, ns_audio_bus_volume(NS_BUS_AMBIENCE));
    }
}

/* --------------------------------------------------------------------------
 * Le dessin
 * -------------------------------------------------------------------------- */

void room_menu_draw(ns_sprite *s, const room_menu *m, const room_menu_ctx *ctx)
{
    if (!m->open) return;

    /* Les pages d'information remplacent les réglages, elles ne s'empilent pas
     * dessus : deux cadres l'un sur l'autre se lisent comme un défaut. */
    if (m->page == ROOM_MENU_PAGE_CONTROLS) { room_controls_draw(s); return; }
    if (m->page == ROOM_MENU_PAGE_CREDITS)  { room_credits_draw(s);  return; }

    const float W = ROOM_HUD_W, H = ROOM_HUD_H;

    /* Un voile, pas un noir : on garde la salle derrière, c'est elle qui montre
     * ce que le réglage change. */
    const float veil[4] = { 0.02f, 0.015f, 0.012f, 0.72f };
    ns_sprite_rect(s, 0, 0, W, H, veil);

    /*
     * La hauteur du cadre est DÉDUITE du nombre d'entrées, elle n'est plus
     * écrite en dur.
     *
     * Elle valait 560 pour onze lignes, ce qui laissait exactement seize pixels
     * sous la dernière — et les deux lignes ajoutées ici passaient donc PAR
     * DESSUS le texte d'aide. Un cadre de taille fixe est un piège qui se
     * referme sur la personne suivante qui ajoute un réglage : la formule le
     * désamorce une fois pour toutes, et la borne dit ce qui arrive si l'on
     * dépasse l'écran plutôt que de le laisser déborder en silence.
     */
    const float head_h = 78.0f;    /* titre et respiration au-dessus des lignes */
    const float foot_h = 74.0f;    /* les deux lignes d'aide, et leur marge */
    const float panel_w = 720.0f;

    /*
     * C'est le PAS DES LIGNES qui cède, pas le cadre — et il a fallu le voir
     * pour le corriger.
     *
     * La version précédente déduisait la hauteur du cadre du nombre de lignes,
     * puis la rabotait à `H - 20` si elle dépassait. Le commentaire annonçait
     * que la formule « désamorce le piège une fois pour toutes ». Elle ne le
     * désamorçait que tant que le rabot ne servait pas : les lignes, elles,
     * continuaient de se poser tous les 38 points depuis le haut, sans rien
     * savoir du cadre qu'on venait de raccourcir.
     *
     * Mesuré à seize lignes, en ajoutant COMMANDES et CREDITS : 78 + 16 x 38 +
     * 74 = 760 pour 700 disponibles. Le cadre a été ramené à 700, les lignes
     * sont descendues jusqu'à 686, et « QUITTER LE JEU » s'est écrit PAR DESSUS
     * « FLECHES CHOISIR ET REGLER ». Le piège s'était refermé sur la ligne
     * suivante, exactement comme annoncé, et l'annonce n'avait rien empêché.
     *
     * Le pas se resserre donc jusqu'à ce que tout tienne. Un menu un peu plus
     * serré reste lisible ; un menu qui écrit deux textes au même endroit ne
     * l'est plus. Et le calcul se fait au dessin, sur `MI_COUNT` : ajouter un
     * réglage ne demande de se souvenir de rien.
     */
    const float avail = H - 20.0f - head_h - foot_h;
    float row_h = 38.0f;
    if ((float)MI_COUNT * row_h > avail) row_h = avail / (float)MI_COUNT;
    const float panel_h = head_h + (float)MI_COUNT * row_h + foot_h;
    const float px = (W - panel_w) * 0.5f, py = (H - panel_h) * 0.5f;

    const float border[4] = { 0.78f, 0.42f, 0.14f, 0.95f };
    const float back[4]   = { 0.055f, 0.040f, 0.030f, 0.95f };
    ns_sprite_rect(s, px - 3, py - 3, panel_w + 6, panel_h + 6, border);
    ns_sprite_rect(s, px, py, panel_w, panel_h, back);

    const float amber[4] = { 1.00f, 0.72f, 0.32f, 1.0f };
    const float pale[4]  = { 0.86f, 0.82f, 0.74f, 1.0f };
    const float dim[4]   = { 0.50f, 0.45f, 0.40f, 1.0f };

    const float title_scale = 3.0f;
    const char *title = "REGLAGES";
    ns_sprite_text(s, px + (panel_w - ns_sprite_text_width(title, title_scale)) * 0.5f,
                   py + 22.0f, title_scale, amber, title);

    const float top = py + head_h;
    const float lx = px + 40.0f;          /* libellés */
    const float vx = px + panel_w - 40.0f; /* valeurs, alignées à droite */

    for (int i = 0; i < MI_COUNT; ++i) {
        const float y = top + (float)i * row_h;
        const bool sel = (i == m->cursor);

        if (sel) {
            /* Un liseré qui respire : c'est ce qui rend le curseur trouvable
             * d'un coup d'œil sur un écran de télévision, à trois mètres. */
            const float pulse = 0.16f + 0.10f * sinf(m->time * 4.0f);
            const float hl[4] = { 0.85f, 0.48f, 0.16f, pulse };
            ns_sprite_rect(s, px + 24.0f, y - 6.0f, panel_w - 48.0f, row_h - 6.0f, hl);
            ns_sprite_text(s, px + 10.0f, y, 2.0f, amber, ">");
        }

        const float *col = sel ? amber : (item_is_button(i) ? pale : pale);
        ns_sprite_text(s, lx, y, 2.0f, col, g_label[i]);

        if (!item_is_button(i)) {
            char value[32], hint[32];
            item_value(ctx, i, value, sizeof value, hint, sizeof hint);
            const float vw = ns_sprite_text_width(value, 2.0f);
            ns_sprite_text(s, vx - vw, y, 2.0f, sel ? amber : pale, value);

            /* Le chevron gauche se place APRÈS l'indice, pas à un décalage fixe :
             * avec un décalage fixe il se superposait au « x1.00 » du palier de
             * qualité — la seule ligne qui porte les deux. */
            float left = vx - vw - 18.0f;
            if (hint[0]) {
                const float hw = ns_sprite_text_width(hint, 1.4f);
                left -= hw;
                ns_sprite_text(s, left, y + 4.0f, 1.4f, dim, hint);
                left -= 18.0f;
            }
            if (sel) {
                ns_sprite_text(s, left - ns_sprite_text_width("<", 2.0f), y, 2.0f, amber, "<");
                ns_sprite_text(s, vx + 14.0f, y, 2.0f, amber, ">");
            }
        }
    }

    const char *help = "FLECHES CHOISIR ET REGLER    ENTREE VALIDER    ECHAP FERMER";
    ns_sprite_text(s, px + (panel_w - ns_sprite_text_width(help, 1.5f)) * 0.5f,
                   py + panel_h - 48.0f, 1.5f, pale, help);
    /* Même échelle que la ligne au-dessus : la fonte est un bitmap 5 x 7, et
     * une échelle plus fine ne la rend pas plus discrète, elle la rend floue. */
    const char *kept = "LES REGLAGES SONT GARDES";
    ns_sprite_text(s, px + (panel_w - ns_sprite_text_width(kept, 1.5f)) * 0.5f,
                   py + panel_h - 26.0f, 1.5f, dim, kept);
}
