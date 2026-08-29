/* piano.c — voir piano.h pour ce qui vient de 2020 et ce qui a dû être écrit. */
#include "piano.h"

#include "ns_core.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * La partition
 * ==========================================================================
 * `legacy/games/12_piano/musique.txt`, recopié à l'entier près : quatre
 * touches puis la longueur de la note, en pixels dans l'original.
 *
 * Treize lignes. C'est court — cinq secondes de jeu — donc la partition BOUCLE,
 * un peu plus vite à chaque tour. C'est le seul moyen d'en faire une partie
 * plutôt qu'une démonstration, et le score vient alors de jusqu'où l'on tient.
 * ========================================================================== */

typedef struct chart_row { int lane[PN_LANES]; int length; } chart_row;

static const chart_row PN_CHART[] = {
    { { 0, 0, 0, 0 }, 100 },
    { { 0, 1, 0, 0 }, 600 },
    { { 0, 0, 0, 0 }, 400 },
    { { 0, 0, 1, 0 }, 600 },
    { { 0, 0, 0, 1 }, 600 },
    { { 0, 0, 1, 0 }, 600 },
    { { 0, 1, 0, 0 }, 600 },
    { { 1, 0, 0, 0 }, 600 },
    { { 0, 0, 0, 0 }, 1200 },
    { { 1, 0, 0, 0 }, 400 },
    { { 0, 1, 0, 0 }, 400 },
    { { 0, 0, 1, 0 }, 400 },
    { { 1, 0, 0, 0 }, 400 },
};
#define PN_CHART_ROWS ((int)(sizeof PN_CHART / sizeof PN_CHART[0]))

/* Les longueurs sont en pixels ; à 420 px/s la mesure de 600 px dure 1,43 s,
 * ce qui donne le tempo lisible de l'original. */
#define PN_PX_PER_SECOND 420.0f

/* La fenêtre de frappe, en secondes de part et d'autre de la ligne. Plus étroit
 * devient injuste au manche, plus large enlève toute exigence. */
#define PN_WINDOW 0.16f

/* Le barème de `rulesTable["piano"]`. */
#define PTS_NOTE  5
#define PTS_COMBO 25
#define PN_COMBO_EVERY 10

/* ==========================================================================
 * La partition, dépliée
 * ========================================================================== */

static void build_chart(piano *g, float offset, float speed)
{
    float cursor = offset;
    for (int r = 0; r < PN_CHART_ROWS; ++r) {
        const float dur = (float)PN_CHART[r].length / PN_PX_PER_SECOND / speed;
        for (int l = 0; l < PN_LANES; ++l) {
            if (!PN_CHART[r].lane[l]) continue;
            if (g->note_count >= PN_MAX_NOTES) return;
            pn_note *n = &g->note[g->note_count++];
            n->lane = l;
            n->start = cursor;
            n->length = dur;
            n->hit = n->missed = false;
        }
        cursor += dur;
    }
}

/* Le tour suivant : on retire ce qui est passé, on ajoute la boucle d'après. */
static void extend_chart(piano *g)
{
    uint32_t keep = 0;
    float last_end = g->time;
    for (uint32_t i = 0; i < g->note_count; ++i) {
        if (g->note[i].start + g->note[i].length < g->time - 1.0f) continue;
        g->note[keep++] = g->note[i];
        const float end = g->note[i].start + g->note[i].length;
        if (end > last_end) last_end = end;
    }
    g->note_count = keep;

    g->loops++;
    /* +8 % par tour : au dixième la partition va deux fois plus vite. C'est ce
     * qui fait qu'une partie finit, et donc qu'un score veut dire quelque
     * chose. */
    g->speed *= 1.08f;
    build_chart(g, last_end + 0.35f, g->speed);
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

void piano_reset(piano *g, uint64_t seed, bool hard)
{
    memset(g, 0, sizeof *g);
    ns_rng_seed(&g->rng, seed, 0x91A0u);
    g->hard = hard;
    g->phase = PN_READY;
    /* La borne « hard » démarre déjà lancée. C'est la seule différence, et elle
     * suffit : ce jeu se durcit tout seul. */
    g->speed = hard ? 1.6f : 1.0f;
    g->fail_reason = PN_FAIL_NONE;
    build_chart(g, 2.0f, g->speed);
}

/* La note frappable d'une voie : celle dont la ligne de frappe tombe dans la
 * fenêtre. −1 s'il n'y en a pas. */
int piano_note_at(const piano *g, int lane)
{
    for (uint32_t i = 0; i < g->note_count; ++i) {
        const pn_note *n = &g->note[i];
        if (n->lane != lane || n->hit || n->missed) continue;
        if (fabsf(n->start - g->time) <= PN_WINDOW) return (int)i;
    }
    return -1;
}

static void fail(piano *g, pn_fail why)
{
    g->phase = PN_DEAD;
    g->dead_time = 0.0f;
    g->died = true;
    g->fail_reason = why;
}

/* Le texte d'une fin. Il vit ICI, du côté de l'affichage : l'état ne porte que
 * la raison, ce qui le garde comparable d'une machine à l'autre. */
static const char *fail_text(pn_fail why)
{
    switch (why) {
        case PN_FAIL_WRONG_NOTE:      return "FAUSSE NOTE";
        case PN_FAIL_TOO_MANY_MISSES: return "TROP DE NOTES MANQUEES";
        case PN_FAIL_NONE:            break;
    }
    return "";
}

static void strike(piano *g, int lane)
{
    if (g->phase == PN_DEAD) return;
    if (g->phase == PN_READY) g->phase = PN_PLAYING;

    g->tapped = true;
    g->lane_flash[lane] = 0.16f;

    const int idx = piano_note_at(g, lane);
    if (idx < 0) {
        /*
         * La règle de 2020, et c'est elle qui fait le jeu : frapper une voie
         * VIDE termine la partie. `touche = -1`, et la boucle s'arrête. Sans
         * elle on martèlerait les quatre touches en continu.
         */
        fail(g, PN_FAIL_WRONG_NOTE);
        return;
    }

    g->note[idx].hit = true;
    g->hits++;
    g->combo++;
    if (g->combo > g->best_combo) g->best_combo = g->combo;
    g->score += PTS_NOTE;
    g->pend_note++;

    /* Un palier de combo tous les dix : c'est ce que `rulesTable` compte. */
    if (g->combo % PN_COMBO_EVERY == 0) {
        g->score += PTS_COMBO;
        g->pend_combo++;
    }
}

void piano_press(piano *g, ns_game_button b)
{
    if (g->phase == PN_DEAD) return;
    if (g->phase == PN_READY && b == NS_GAME_ACTION) { g->phase = PN_PLAYING; return; }
    switch (b) {
        case NS_GAME_LEFT:  strike(g, 0); break;
        case NS_GAME_UP:    strike(g, 1); break;
        case NS_GAME_DOWN:  strike(g, 2); break;
        case NS_GAME_RIGHT: strike(g, 3); break;
        default: break;
    }
}

void piano_hold(piano *g, const bool held[NS_GAME_BUTTON_COUNT])
{
    /* Le maintien ne rejoue pas la note : une touche tenue n'est pas une touche
     * frappée, et l'original ne comptait que les appuis. On garde l'état pour
     * l'affichage des touches enfoncées. */
    g->held[0] = held[NS_GAME_LEFT];
    g->held[1] = held[NS_GAME_UP];
    g->held[2] = held[NS_GAME_DOWN];
    g->held[3] = held[NS_GAME_RIGHT];
}

void piano_tick(piano *g, float dt)
{
    if (g->phase == PN_DEAD) { g->dead_time += dt; return; }
    if (g->phase == PN_READY) {
        /* On laisse la partition avancer jusqu'à la première note : la
         * démonstration défile, et le premier appui prend la main. */
        g->time += dt;
        if (g->time > 1.4f) g->time = 1.4f;
        return;
    }

    g->time += dt;
    for (int l = 0; l < PN_LANES; ++l) {
        if (g->lane_flash[l] > 0.0f) g->lane_flash[l] -= dt;
    }

    /* Une note laissée passer casse le combo — mais ne tue pas. C'est la
     * dissymétrie de 2020 : la faute punie est la FAUSSE note, pas l'oubli. */
    for (uint32_t i = 0; i < g->note_count; ++i) {
        pn_note *n = &g->note[i];
        if (n->hit || n->missed) continue;
        if (g->time > n->start + PN_WINDOW) {
            n->missed = true;
            g->misses++;
            g->combo = 0;
        }
    }

    /* Faut-il rallonger la partition ? On regarde la dernière note connue. */
    float last = 0.0f;
    for (uint32_t i = 0; i < g->note_count; ++i) {
        const float end = g->note[i].start;
        if (end > last) last = end;
    }
    if (last < g->time + 3.0f) extend_chart(g);

    /* En hardcore, laisser passer trois notes termine la partie : sans quoi
     * une borne « hard » se jouerait en ne touchant à rien. */
    if (g->hard && g->misses >= 3) fail(g, PN_FAIL_TOO_MANY_MISSES);
}

/* ==========================================================================
 * Le joueur automatique
 * ========================================================================== */

bool piano_autopilot(piano *g)
{
    if (g->phase == PN_DEAD) return false;
    if (g->phase == PN_READY) { piano_press(g, NS_GAME_ACTION); return true; }

    /* Il frappe la voie dont la note est dans la fenêtre — et RIEN sinon,
     * puisque frapper à vide termine la partie. C'est le joueur automatique le
     * plus simple qu'on puisse écrire, et c'est aussi le seul qui soit correct :
     * il n'y a pas de stratégie, il y a une lecture. */
    for (int l = 0; l < PN_LANES; ++l) {
        if (piano_note_at(g, l) < 0) continue;
        static const ns_game_button KEY[PN_LANES] = {
            NS_GAME_LEFT, NS_GAME_UP, NS_GAME_DOWN, NS_GAME_RIGHT
        };
        piano_press(g, KEY[l]);
        return true;
    }
    return true;
}

/* ==========================================================================
 * Le dessin
 * ========================================================================== */

void piano_draw(ns_sprite *s, const piano *g, const piano_art *a,
                float logical_w, float logical_h)
{
    (void)a;
    const float base = (logical_w / PN_LOGICAL_W < logical_h / PN_LOGICAL_H)
                     ? logical_w / PN_LOGICAL_W : logical_h / PN_LOGICAL_H;

    static const float back[4] = { 0.04f, 0.03f, 0.08f, 1.0f };
    ns_sprite_rect(s, 0.0f, 0.0f, logical_w, logical_h, back);

    const float lane_w = logical_w * 0.14f;
    const float board_w = lane_w * PN_LANES;
    const float ox = (logical_w - board_w) * 0.5f;
    const float hit_y = logical_h * 0.80f;
    /* Les notes descendent : `PN_PX_PER_SECOND` px/s à l'échelle de l'écran. */
    const float px_per_s = PN_PX_PER_SECOND * base * g->speed;

    static const float LANE[PN_LANES][4] = {
        { 0.94f, 0.32f, 0.36f, 1.0f },
        { 0.98f, 0.72f, 0.26f, 1.0f },
        { 0.34f, 0.80f, 0.52f, 1.0f },
        { 0.42f, 0.62f, 0.98f, 1.0f },
    };
    static const float lane_bg[4] = { 0.08f, 0.07f, 0.13f, 1.0f };
    static const float rule[4]    = { 0.28f, 0.26f, 0.42f, 1.0f };

    for (int l = 0; l < PN_LANES; ++l) {
        const float x = ox + (float)l * lane_w;
        ns_sprite_rect(s, x + 3.0f, 0.0f, lane_w - 6.0f, logical_h, lane_bg);
        /* La touche, en bas : elle s'allume quand on frappe. */
        float col[4];
        const float lit = (g->lane_flash[l] > 0.0f) ? 1.0f : (g->held[l] ? 0.55f : 0.22f);
        for (int k = 0; k < 3; ++k) col[k] = LANE[l][k] * lit;
        col[3] = 1.0f;
        ns_sprite_rect(s, x + 3.0f, hit_y, lane_w - 6.0f, logical_h * 0.06f, col);
    }
    ns_sprite_rect(s, ox, hit_y - 3.0f * base, board_w, 4.0f * base, rule);

    for (uint32_t i = 0; i < g->note_count; ++i) {
        const pn_note *n = &g->note[i];
        if (n->hit) continue;
        const float y = hit_y - (n->start - g->time) * px_per_s;
        const float h = (n->length > 0.0f ? n->length : 0.25f) * px_per_s * 0.55f;
        if (y + h < 0.0f || y - h > logical_h) continue;
        float col[4] = { LANE[n->lane][0], LANE[n->lane][1], LANE[n->lane][2], 1.0f };
        if (n->missed) { col[0] *= 0.30f; col[1] *= 0.30f; col[2] *= 0.30f; }
        const float x = ox + (float)n->lane * lane_w;
        ns_sprite_rect(s, x + 8.0f, y - h, lane_w - 16.0f, h, col);
    }

    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float amber[4] = { 1.0f, 0.82f, 0.30f, 1.0f };
    char line[64];
    SDL_snprintf(line, sizeof line, "%lld", (long long)g->score);
    ns_sprite_text(s, 30.0f * base, 24.0f * base, base * 6.0f, white, line);
    if (g->combo >= 2) {
        SDL_snprintf(line, sizeof line, "COMBO %u", g->combo);
        ns_sprite_text(s, 30.0f * base, 100.0f * base, base * 4.0f, amber, line);
    }

    if (g->phase == PN_READY) {
        const char *msg = "MANCHE : QUATRE TOUCHES   BOUTON POUR COMMENCER";
        const float sc = base * 3.4f;
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(msg, sc)) * 0.5f,
                       logical_h * 0.92f, sc, white, msg);
    } else if (g->phase == PN_DEAD) {
        static const float veil[4] = { 0.03f, 0.03f, 0.05f, 0.80f };
        ns_sprite_rect(s, 0.0f, logical_h * 0.30f, logical_w, logical_h * 0.40f, veil);
        const float sc = base * 8.0f;
        const char *why = fail_text(g->fail_reason);
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(why, sc)) * 0.5f,
                       logical_h * 0.37f, sc, amber, why);
        const float sc2 = base * 4.6f;
        SDL_snprintf(line, sizeof line, "%u NOTES   MEILLEUR COMBO %u", g->hits, g->best_combo);
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(line, sc2)) * 0.5f,
                       logical_h * 0.50f, sc2, white, line);
        SDL_snprintf(line, sizeof line, "MEILLEUR %u", g->best);
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(line, sc2)) * 0.5f,
                       logical_h * 0.57f, sc2, white, line);
        if (g->dead_time > 0.8f) {
            const char *again = "ESPACE POUR REJOUER";
            ns_sprite_text(s, (logical_w - ns_sprite_text_width(again, sc2)) * 0.5f,
                           logical_h * 0.64f, sc2, white, again);
        }
    }
}

/* ==========================================================================
 * Les planches
 * ========================================================================== */

bool piano_art_load(ns_rhi *r, piano_art *a)
{
    (void)r;
    /* Aucune : tout est dessiné. Le fond de 2020 est une photo de piano en
     * 640 x 900, et la remonter sur une dalle de borne n'apporterait rien
     * qu'un flou. Le dire plutôt que charger une texture qu'on n'emploie pas. */
    a->ready = true;
    return true;
}

void piano_art_free(ns_rhi *r, piano_art *a)
{
    (void)r;
    a->ready = false;
}

/* ==========================================================================
 * L'adaptation à `ns_game_api`
 * ========================================================================== */

static void pn_reset(void *g, uint64_t seed, bool hard) { piano_reset((piano *)g, seed, hard); }
static void pn_press(void *g, ns_game_button b) { piano_press((piano *)g, b); }
static void pn_hold(void *g, const bool h[NS_GAME_BUTTON_COUNT]) { piano_hold((piano *)g, h); }
static void pn_tick(void *g, float dt) { piano_tick((piano *)g, dt); }

static void pn_draw(ns_sprite *s, const void *g, const void *a, float w, float h)
{
    piano_draw(s, (const piano *)g, (const piano_art *)a, w, h);
}

static bool pn_art_load(ns_rhi *r, void *a) { return piano_art_load(r, (piano_art *)a); }
static void pn_art_free(ns_rhi *r, void *a) { piano_art_free(r, (piano_art *)a); }
static bool pn_autopilot(void *g) { return piano_autopilot((piano *)g); }

static uint32_t pn_score(const void *g)
{
    const int64_t v = ((const piano *)g)->score;
    return v > 0 ? (uint32_t)v : 0u;
}
static uint32_t pn_best(const void *g) { return ((const piano *)g)->best; }
static void     pn_set_best(void *g, uint32_t b) { ((piano *)g)->best = b; }

static bool pn_dead(const void *g, float *dead_time)
{
    const piano *p = (const piano *)g;
    if (dead_time) *dead_time = p->dead_time;
    return p->phase == PN_DEAD;
}

/* Le vocabulaire de `rulesTable["piano"]` : note 5, combo 25. La table
 * l'attendait depuis M6 alors que le jeu de 2020 ne comptait aucun point. */
static const char *const pn_kinds[] = { "note", "combo", "death", NULL };

static void pn_events(void *g, ns_game_events *out)
{
    piano *p = (piano *)g;

    /* Pas de « blip » : la frappe EST la note, et une frappe à vide termine la
     * partie. Journaliser un geste de plus ne dirait rien qu'on ne sache. */
    out->blip = false;
    out->blip_kind = "note";

    if (p->pend_note)       { out->score = true; out->score_kind = "note";  p->pend_note--; }
    else if (p->pend_combo) { out->score = true; out->score_kind = "combo"; p->pend_combo--; }
    out->score_value = 0;
    p->tapped = false;

    if (p->died && !p->pend_note && !p->pend_combo) {
        out->die = true;
        p->died = false;
    }
}

const ns_game_api g_piano_api = {
    .id = "piano", .title = "PIANO", .label = "PIANO",
    .state_size = sizeof(piano), .art_size = sizeof(piano_art),
    .sound_blip = NULL,
    .sound_score = "games/flappy/score.wav",
    .sound_die = "games/snake/gameover.wav",
    .art_load = pn_art_load, .art_free = pn_art_free,
    .reset = pn_reset, .press = pn_press, .hold = pn_hold,
    .tick = pn_tick, .draw = pn_draw, .autopilot = pn_autopilot,
    .event_kinds = pn_kinds,
    .score = pn_score, .best = pn_best, .set_best = pn_set_best,
    .dead = pn_dead, .events = pn_events,
};
