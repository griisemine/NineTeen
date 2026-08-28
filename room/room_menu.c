#include "room_menu.h"

#include "ns_audio.h"
#include "ns_config.h"
#include "ns_core.h"
#include "ns_math.h"
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
    MI_RESUME,
    MI_QUIT,
    MI_COUNT
} menu_item;

static const char *const g_label[MI_COUNT] = {
    "QUALITE",
    "ECHELLE DE RENDU",
    "POUSSIERE",
    "LUMINOSITE",
    "VOLUME GENERAL",
    "MUSIQUE",
    "EFFETS",
    "AMBIANCE",
    "PAS",
    "FOND DE SALLE",
    "SOURIS",
    "REPRENDRE",
    "QUITTER LE JEU",
};

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
        default:            out[0] = '\0'; break;
    }
    if (!ns_audio_ready() && i >= MI_VOL_MASTER && i <= MI_VOL_AMBIENCE) {
        SDL_snprintf(out, n, "-");
        SDL_snprintf(hint, hn, "PAS DE SORTIE AUDIO");
    }
}

static bool item_is_button(int i) { return i == MI_RESUME || i == MI_QUIT; }

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
    m->render_dirty = m->close_request = m->quit_request = false;
    if (m->cursor < 0 || m->cursor >= MI_COUNT) m->cursor = 0;
}

void room_menu_close(room_menu *m) { m->open = false; m->close_request = false; }

void room_menu_update(room_menu *m, float dt) { if (m->open) m->time += dt; }

void room_menu_input(room_menu *m, const room_menu_ctx *ctx, room_menu_action a)
{
    if (!m->open) return;

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
    const float row_h  = 38.0f;
    const float head_h = 78.0f;    /* titre et respiration au-dessus des lignes */
    const float foot_h = 74.0f;    /* les deux lignes d'aide, et leur marge */
    const float panel_w = 720.0f;
    float panel_h = head_h + (float)MI_COUNT * row_h + foot_h;
    if (panel_h > H - 20.0f) panel_h = H - 20.0f;
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
