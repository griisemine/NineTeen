#include "room_menu.h"

#include "ns_audio.h"
#include "ns_config.h"
#include "ns_core.h"
#include "ns_math.h"
#include "room_hud.h"

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
 * C'est le seul chiffre qui aide vraiment à choisir, et il existe déjà : il est
 * dans le journal et dans `docs/JOUER.md` depuis B9. Le laisser hors de l'écran
 * revient à demander au joueur d'essayer les cinq à l'aveugle. Mesuré sur le
 * rasteriseur logiciel du conteneur de développement, donc en valeur RELATIVE :
 * c'est le rapport entre paliers qui transporte, pas la milliseconde.
 */
static const char *quality_hint(ns_quality q)
{
    switch (q) {
        case NS_QUALITY_POTATO: return "x0.17";
        case NS_QUALITY_LOW:    return "x0.42";
        case NS_QUALITY_MEDIUM: return "x1.00";
        case NS_QUALITY_HIGH:   return "x1.56";
        case NS_QUALITY_ULTRA:  return "x3.92";
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

    const float panel_w = 720.0f, panel_h = 560.0f;
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

    const float row_h = 38.0f;
    const float top = py + 78.0f;
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
