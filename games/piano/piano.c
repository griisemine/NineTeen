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
 * LE MORCEAU A UNE FIN, ET LA RAMPE A UN PLAFOND
 * ==========================================================================
 * Ce qu'il y avait : `g->speed *= 1.08f` à chaque tour, sans borne, et des
 * tours ajoutés tant qu'il fallait des notes. Écrit ainsi, la partition n'a pas
 * de fin — elle a pire, elle a une fin qui arrive trop tôt : la durée d'un tour
 * vaut 16,43 / 1,08^k seconde, et la somme de cette série CONVERGE, à
 * 16,43 x 13,5 = 221,8 secondes. Le morceau infini se consomme donc en trois
 * minutes et quarante secondes, après quoi la boucle en ajoute un par pas,
 * c'est-à-dire cent vingt par seconde.
 *
 * La mesure le dit sans détour, borne difficile, cinq graines identiques :
 * vitesse 1,73 à 11 s, 6,39 à 111 s, 2 587 à 171 s, 207 949 à 191 s. À cette
 * vitesse une mesure de 600 px dure sept microsecondes et les notes descendent
 * à quatre-vingt-sept millions de pixels par seconde : l'écran ne montre plus
 * rien, et le score continue de monter. Il sextuplait entre la 120ᵉ et la
 * 240ᵉ seconde, ce qui ne mesurait plus une adresse mais une endurance.
 *
 * Deux bornes, donc, et elles se justifient l'une l'autre :
 *
 * `PN_SPEED_MAX` — la vitesse plafonne. Elle vaut ce que la LECTURE permet, et
 * ce nombre-là se calcule : la note parcourt les 864 px qui séparent le haut de
 * l'écran de la ligne de frappe à `420 x vitesse` px/s, ce qui laisse
 * 2,06 / vitesse seconde pour la voir venir et bouger le manche. À cinq, il
 * reste 0,41 s — au-dessus du temps de réaction, et à peine. Au-delà on ne
 * demande plus de jouer, on demande de deviner.
 *
 * `PN_LOOPS` — le morceau fait quatorze tours et s'arrête. Un jeu d'arcade
 * finit, et celui-ci finissait par épuisement du joueur ou jamais. Quatorze
 * tours font une centaine de secondes sur la borne ordinaire et une
 * soixantaine sur la difficile, qui démarre déjà lancée.
 *
 * `PN_SPEED_STEP` passe de 1,08 à 1,18 pour la même raison qu'il y a un
 * plafond : avec 8 % par tour, la partition met deux minutes à doubler et le
 * morceau se terminerait avant d'avoir été difficile. À 18 %, le plafond arrive
 * au dixième tour et les quatre derniers se jouent au maximum.
 * ========================================================================== */
#define PN_SPEED_STEP 1.18f
#define PN_SPEED_MAX  5.0f
#define PN_LOOPS      14u

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
    g->speed *= PN_SPEED_STEP;
    if (g->speed > PN_SPEED_MAX) g->speed = PN_SPEED_MAX;
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
        case PN_DONE:                 return "MORCEAU TERMINE";
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

    /*
     * Faut-il rallonger la partition ? On regarde la dernière note connue — et
     * on s'arrête au quatorzième tour, parce que le morceau a une longueur.
     */
    float last = 0.0f;
    for (uint32_t i = 0; i < g->note_count; ++i) {
        const float end = g->note[i].start;
        if (end > last) last = end;
    }
    if (last < g->time + 3.0f && g->loops + 1u < PN_LOOPS) extend_chart(g);

    /*
     * LE MORCEAU EST FINI quand le dernier tour est écrit ET que sa dernière
     * note est passée. On attend la fenêtre de frappe : sans ce délai, la note
     * finale serait comptée manquée à l'instant même où l'on pouvait encore la
     * jouer, et une partie parfaite se terminerait sur un oubli.
     */
    if (g->loops + 1u >= PN_LOOPS && g->time > last + PN_WINDOW) {
        g->phase = PN_DEAD;
        g->dead_time = 0.0f;
        g->died = true;
        g->fail_reason = PN_DONE;
        return;
    }

    /*
     * Laisser passer trop de notes termine la partie — dans LES DEUX modes.
     *
     * Le commentaire d'origine disait déjà pourquoi, pour la borne « hard » :
     * « sans quoi une borne se jouerait en ne touchant à rien ». L'objection
     * vaut mot pour mot pour la borne normale, et personne ne l'y avait
     * appliquée : `g->hard &&` laissait le mode ordinaire sans aucune condition
     * de défaite. Mesuré, cinq graines : le pilote automatique passe 300 s sans
     * mourir, et un joueur qui ne touche à RIEN en fait autant — indéfiniment,
     * avec un score de zéro qu'aucune règle ne vient interrompre.
     *
     * Un jeu qu'on ne peut pas perdre n'a pas de fin, donc pas de score qui
     * compte, donc rien à défendre au classement. C'est la seule des trois
     * questions d'une borne à laquelle Piano ne répondait pas.
     *
     * Douze en normal contre trois en difficile : la partition boucle en
     * s'accélérant, donc les oublis finissent par arriver à tout le monde et
     * la limite se paie d'elle-même. Douze laisse la place d'apprendre une
     * mesure ratée sans finir la partie dessus.
     */
    const uint32_t cap = g->hard ? 3u : 12u;
    if (g->misses >= cap) fail(g, PN_FAIL_TOO_MANY_MISSES);
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
    /*
     * LE DÉFILEMENT NE SUIT PAS LE TEMPO, ET C'ÉTAIT À L'ENVERS.
     *
     * Il valait `PN_PX_PER_SECOND * base * g->speed` : plus la partition
     * accélérait, plus les notes descendaient vite, donc moins on en voyait à la
     * fois. La distance entre le haut de l'écran et la ligne de frappe est fixe
     * — 864 px dans le repère de 1920 — si bien que le temps de lecture valait
     * 2,06 / vitesse seconde et fondait avec elle. Capture à la 60ᵉ seconde,
     * vitesse 3,4 : UNE note à l'écran, et des voies vides le reste du temps.
     *
     * Une partition plus rapide doit montrer PLUS de notes, pas moins : c'est
     * ainsi qu'on voit venir la difficulté au lieu de la subir. Le défilement
     * garde donc l'allure de la première mesure et n'en bouge plus — 2,06 s de
     * lecture, quel que soit le tempo — et c'est la DENSITÉ qui monte. Au
     * plafond de `PN_SPEED_MAX`, six notes sont à l'écran au lieu d'une.
     */
    const float px_per_s = PN_PX_PER_SECOND * base;

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
        ns_sprite_text(s, 30.0f * base, 104.0f * base, base * 5.0f, amber, line);
    }
    /*
     * OÙ L'ON EN EST DU MORCEAU.
     *
     * Le morceau fait quatorze tours et s'arrête ; le dire est la moitié de ce
     * que la borne d'à côté vend. Un joueur qui ignore qu'il y a une fin joue
     * jusqu'à ce qu'il rate, un joueur qui voit « 7/14 » joue pour finir — et
     * c'est la seule différence entre un exercice et une partie.
     */
    {
        /* « SUR » et pas une barre oblique : sur la dalle d'une borne, la barre
         * de la fonte 5 x 7 se confond avec le chiffre un. */
        SDL_snprintf(line, sizeof line, "TOUR %u SUR %u", g->loops + 1u, (unsigned)PN_LOOPS);
        ns_sprite_text(s, 30.0f * base, 174.0f * base, base * 5.0f, white, line);
    }

    /* Les oublis restants. On ne les montre qu'une fois le premier commis :
     * avant, c'est du bruit ; après, c'est le compte à rebours qui dit qu'on
     * joue sa partie. Une limite qu'on ne voit pas venir est arbitraire. */
    if (g->misses > 0) {
        static const float red[4] = { 1.0f, 0.40f, 0.36f, 1.0f };
        const uint32_t cap = g->hard ? 3u : 12u;
        SDL_snprintf(line, sizeof line, "OUBLIS %u/%u", g->misses, cap);
        ns_sprite_text(s, PN_LOGICAL_W * base - 30.0f * base
                          - ns_sprite_text_width(line, base * 5.0f),
                       24.0f * base, base * 5.0f, red, line);
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
        /* Le morceau terminé s'écrit en VERT : c'est la fin qu'on vient
         * chercher, et elle ne doit pas ressembler aux deux autres. */
        static const float green[4] = { 0.42f, 0.94f, 0.52f, 1.0f };
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(why, sc)) * 0.5f,
                       logical_h * 0.37f, sc,
                       (g->fail_reason == PN_DONE) ? green : amber, why);
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
    .sound_score = "games/envol/score.wav",
    .sound_die = "games/snake/gameover.wav",
    .art_load = pn_art_load, .art_free = pn_art_free,
    .reset = pn_reset, .press = pn_press, .hold = pn_hold,
    .tick = pn_tick, .draw = pn_draw, .autopilot = pn_autopilot,
    .event_kinds = pn_kinds,
    .score = pn_score, .best = pn_best, .set_best = pn_set_best,
    .dead = pn_dead, .events = pn_events,
};
