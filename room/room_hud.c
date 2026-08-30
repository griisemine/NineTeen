/* room_hud.c — voir room_hud.h pour le raisonnement. */
#include "room_hud.h"

#include "games.h"
#include "ns_online.h"
#include "room_credits.h"

#include "ns_core.h"
#include "ns_realtime.h"
#include "ns_scores.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>

/* Huit jeux, deux difficultés : la borne ne montrera jamais plus de colonnes
 * que ça, quoi qu'il arrive à `g_games[]`. */
#define NS_HUD_MAX_COLUMNS 16

/* Les couleurs de l'affichage. Ambrées comme la salle : un HUD blanc pur sur une
 * ambiance tungstène se lit comme une capture d'écran collée par-dessus. */
static const float C_TEXT[4]   = { 1.00f, 0.94f, 0.82f, 1.00f };
static const float C_DIM[4]    = { 0.78f, 0.68f, 0.55f, 1.00f };
static const float C_KEY[4]    = { 1.00f, 0.78f, 0.28f, 1.00f };
static const float C_PANEL[4]  = { 0.05f, 0.04f, 0.03f, 0.72f };
static const float C_GOLD[4]   = { 1.00f, 0.82f, 0.35f, 1.00f };

/* Un cadre : le fond translucide plus un liseré. Sans le liseré, un panneau
 * sombre sur une salle sombre n'a pas de bord, et le texte a l'air de flotter. */
static void panel(ns_sprite *s, float x, float y, float w, float h)
{
    static const float edge[4] = { 1.00f, 0.72f, 0.34f, 0.34f };
    ns_sprite_rect(s, x, y, w, h, C_PANEL);
    ns_sprite_rect(s, x, y, w, 2.0f, edge);
    ns_sprite_rect(s, x, y + h - 2.0f, w, 2.0f, edge);
}

static void centred(ns_sprite *s, float cx, float y, float scale,
                    const float rgba[4], const char *text)
{
    ns_sprite_text(s, cx - ns_sprite_text_width(text, scale) * 0.5f, y, scale, rgba, text);
}

/* -------------------------------------------------------------------------- */

static void draw_prompt(ns_sprite *s, const room_hud_state *st)
{
    if (!st->near || !st->can_interact || st->playing) return;

    char line[96];
    /*
     * Le NOM DU JEU, pas celui de la borne. « E — JOUER À FLAPPY (HARD) » dit ce
     * qu'on va faire ; « borne_arcade_10 » dit comment le fichier de salle
     * l'appelle, ce qui n'intéresse que moi.
     */
    const bool hard = (SDL_strcasecmp(st->near->difficulty, "hard") == 0);
    char game[32];
    SDL_strlcpy(game, st->near->game[0] ? st->near->game : "?", sizeof game);
    for (char *p = game; *p; ++p) *p = (char)SDL_toupper((unsigned char)*p);
    SDL_snprintf(line, sizeof line, "JOUER A %s%s", game, hard ? " (HARD)" : "");

    const float scale = 3.0f;
    const float key_w = ns_sprite_text_width("E", scale);
    const float txt_w = ns_sprite_text_width(line, scale);
    const float gap   = 14.0f;
    const float total = key_w + gap * 2.0f + txt_w;

    const float h = ns_sprite_text_height(scale) + 22.0f;
    const float y = ROOM_HUD_H * 0.70f;
    const float x = (ROOM_HUD_W - total) * 0.5f;

    panel(s, x - 22.0f, y - 11.0f, total + 44.0f, h);

    /* La touche dans un carré : c'est ce qui la distingue du mot qui suit, et
     * c'est la convention que tout le monde lit sans l'avoir apprise. */
    const float ky = y;
    ns_sprite_rect(s, x - 7.0f, ky - 5.0f, key_w + 14.0f,
                   ns_sprite_text_height(scale) + 10.0f, C_KEY);
    static const float dark[4] = { 0.08f, 0.06f, 0.03f, 1.0f };
    ns_sprite_text(s, x, ky, scale, dark, "E");
    ns_sprite_text(s, x + key_w + gap * 2.0f, ky, scale, C_TEXT, line);

    /* Le meilleur score local de CETTE borne, sous l'invite. C'est l'information
     * qui transforme « je peux jouer » en « je peux faire mieux ». */
    const uint32_t best = ns_scores_best(st->near->game,
                                        ns_scores_bucket(st->near->difficulty));
    if (best > 0) {
        char sub[64];
        SDL_snprintf(sub, sizeof sub, "MEILLEUR : %u", best);
        centred(s, ROOM_HUD_W * 0.5f, y + h + 6.0f, 2.0f, C_DIM, sub);
    }
}

static void draw_game_overlay(ns_sprite *s, const room_hud_state *st)
{
    if (!st->playing) return;

    char line[64];
    SDL_snprintf(line, sizeof line, "%u", st->score);
    centred(s, ROOM_HUD_W * 0.5f, 34.0f, 5.0f, C_TEXT, line);

    if (st->best > 0) {
        SDL_snprintf(line, sizeof line, "MEILLEUR %u", st->best);
        centred(s, ROOM_HUD_W * 0.5f, 34.0f + ns_sprite_text_height(5.0f) + 8.0f,
                2.0f, C_DIM, line);
    }

    if (!st->dead) return;

    /* L'écran de fin. Le RANG d'abord : c'est lui qui décide si l'on relance. */
    const float w = 520.0f, h = 190.0f;
    const float x = (ROOM_HUD_W - w) * 0.5f, y = ROOM_HUD_H * 0.34f;
    panel(s, x, y, w, h);

    centred(s, ROOM_HUD_W * 0.5f, y + 24.0f, 4.0f, C_TEXT, "PERDU");
    if (st->last_rank > 0) {
        SDL_snprintf(line, sizeof line, "%u E MEILLEUR SCORE LOCAL", st->last_rank);
        centred(s, ROOM_HUD_W * 0.5f, y + 78.0f, 2.5f, C_GOLD, line);
    } else {
        centred(s, ROOM_HUD_W * 0.5f, y + 78.0f, 2.5f, C_DIM, "HORS CLASSEMENT");
    }
    centred(s, ROOM_HUD_W * 0.5f, y + 128.0f, 2.0f, C_DIM, "ESPACE : REJOUER    ECHAP : SORTIR");
}

static void draw_settings(ns_sprite *s, const room_hud_state *st)
{
    if (st->settings_timer <= 0.0f) return;

    /* Fondu sur la dernière demi-seconde : un bandeau qui disparaît d'un coup se
     * lit comme un défaut d'affichage. */
    const float a = (st->settings_timer < 0.5f) ? (st->settings_timer / 0.5f) : 1.0f;

    char line[96];
    SDL_snprintf(line, sizeof line, "QUALITE %s    ECHELLE %.2f",
                 st->quality_name ? st->quality_name : "?", (double)st->render_scale);

    const float scale = 2.2f;
    const float w = ns_sprite_text_width(line, scale) + 44.0f;
    const float h = ns_sprite_text_height(scale) + 22.0f;
    const float x = (ROOM_HUD_W - w) * 0.5f, y = ROOM_HUD_H - h - 28.0f;

    const float bg[4]  = { C_PANEL[0], C_PANEL[1], C_PANEL[2], C_PANEL[3] * a };
    const float fg[4]  = { C_TEXT[0], C_TEXT[1], C_TEXT[2], a };
    ns_sprite_rect(s, x, y, w, h, bg);
    centred(s, ROOM_HUD_W * 0.5f, y + 11.0f, scale, fg, line);

    const float hint[4] = { C_DIM[0], C_DIM[1], C_DIM[2], a * 0.9f };
    centred(s, ROOM_HUD_W * 0.5f, y - 24.0f, 1.6f, hint, "F7 QUALITE   F8 ECHELLE");
}

/*
 * L'aide d'arrivée, et les deux cas où elle se tait.
 *
 * En PARTIE : l'écran de la borne occupe le champ, et les commandes de la salle
 * n'y servent plus à rien. Devant une BORNE : l'invite « E — JOUER À … » occupe
 * déjà le bas de l'écran, et deux panneaux superposés ne se lisent ni l'un ni
 * l'autre. Dans les deux cas le joueur a trouvé quoi faire — ce bandeau a donc
 * fini son travail avant la fin de son minuteur.
 *
 * Le fondu porte sur la dernière seconde et demie. Il est plus long que celui
 * du bandeau de réglages (une demi-seconde) parce qu'on ne le regarde pas : il
 * doit s'effacer sans qu'on remarque le moment où il part.
 */
static void draw_intro(ns_sprite *s, const room_hud_state *st)
{
    if (st->intro_timer <= 0.0f) return;
    if (st->playing || (st->near && st->can_interact)) return;

    const float a = (st->intro_timer < 1.5f) ? (st->intro_timer / 1.5f) : 1.0f;
    room_credits_draw_intro(s, a);
}

/* ========================================================================== */
/* L'ÉCONOMIE : le solde, les deux comptoirs, et le quitte ou double          */
/* ========================================================================== */

/*
 * LE SOLDE, en haut à gauche.
 *
 * En haut à GAUCHE et non à droite : la liste des présents occupe le coin droit
 * depuis le temps réel, et le score d'une partie occupe le haut du centre. Le
 * coin gauche est le seul des quatre qui soit libre en toutes circonstances.
 *
 * Il reste affiché PENDANT la partie, et c'est délibéré : c'est le moment où le
 * joueur se demande ce que la partie en cours va lui rapporter. Le cacher
 * ferait de l'économie quelque chose qui n'existe qu'entre deux parties.
 *
 * La série n'est montrée QUE si elle vaut quelque chose. Une ligne « SERIE 0 J »
 * affichée en permanence à un joueur qui vient d'arriver ne l'informe pas, elle
 * lui reproche quelque chose — et cette économie ne reproche rien.
 */
static void draw_wallet(ns_sprite *s, const room_hud_state *st)
{
    if (!st->eco) return;

    const float scale = 2.2f;
    const float lh    = ns_sprite_text_height(scale) + 6.0f;
    char l[3][40];
    int  n = 0;

    SDL_snprintf(l[n++], sizeof l[0], "JETONS  %d", st->eco->jetons);
    SDL_snprintf(l[n++], sizeof l[0], "TICKETS %d", st->eco->tickets);
    if (st->eco->serie > 0) {
        SDL_snprintf(l[n++], sizeof l[0], "SERIE   %d J", st->eco->serie);
    }

    float w = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float tw = ns_sprite_text_width(l[i], scale);
        if (tw > w) w = tw;
    }

    const float x = 22.0f, y = 22.0f;
    panel(s, x - 12.0f, y - 9.0f, w + 24.0f, lh * (float)n + 18.0f);
    for (int i = 0; i < n; ++i) {
        /* La série en or : c'est la seule des trois lignes qui récompense, et
         * elle doit se distinguer d'un compteur. */
        ns_sprite_text(s, x, y + lh * (float)i, scale,
                       (i == 2) ? C_GOLD : C_TEXT, l[i]);
    }
}

/*
 * L'INVITE DES DEUX COMPTOIRS.
 *
 * Même forme que celle des bornes — la touche dans un carré, puis le verbe —
 * parce que c'est le même geste et qu'un joueur ne doit pas avoir à apprendre
 * deux conventions dans la même salle. Elle est posée plus BAS que celle des
 * bornes (0,76 contre 0,70) : les deux ne peuvent pas s'afficher en même temps,
 * mais si la portée d'un comptoir venait un jour à recouvrir celle d'une borne,
 * elles se liraient encore l'une sous l'autre au lieu de se superposer.
 */
static void draw_poi_prompt(ns_sprite *s, const room_hud_state *st)
{
    if (st->playing || !st->eco) return;
    if (st->poi != NS_POI_TOKENS && st->poi != NS_POI_PRIZES) return;
    if (st->near && st->can_interact) return;   /* la borne d'abord : on vient jouer */

    char line[96];
    if (st->poi == NS_POI_TOKENS) {
        SDL_snprintf(line, sizeof line, "PRENDRE DES JETONS");
    } else {
        const room_eco_lot l = room_eco_lot_en_vue(st->eco);
        if (l == ROOM_ECO_LOT_COUNT) {
            SDL_snprintf(line, sizeof line, "VITRINE : TOUT EST A VOUS");
        } else {
            SDL_snprintf(line, sizeof line, "%s : %d TICKETS",
                         room_eco_lot_titre(l), room_eco_lot_prix(l));
        }
    }

    /* CE QUE LE LOT CHANGE. Calcule ICI, avant la mise en page, parce qu'il
     * fait partie de l'encombrement : c'est lui qui decide de la hauteur de la
     * plaque, et c'est en le dessinant SOUS une plaque deja posee qu'il allait
     * cogner le bandeau d'aide du bas. */
    char sub[80];
    sub[0] = '\0';
    if (st->poi == NS_POI_PRIZES) {
        const room_eco_lot l = room_eco_lot_en_vue(st->eco);
        if (l != ROOM_ECO_LOT_COUNT) {
            SDL_snprintf(sub, sizeof sub, "%s", room_eco_lot_quoi(l));
            for (char *p = sub; *p; ++p) *p = (char)SDL_toupper((unsigned char)*p);
        }
    }

    const float scale = 3.0f;
    const float sub_s = 2.0f;
    const float key_w = ns_sprite_text_width("E", scale);
    const float txt_w = ns_sprite_text_width(line, scale);
    const float sub_w = sub[0] ? ns_sprite_text_width(sub, sub_s) : 0.0f;
    const float gap   = 14.0f;
    const float total = key_w + gap * 2.0f + txt_w;
    const float ligne = ns_sprite_text_height(scale);
    const float sub_h = sub[0] ? (ns_sprite_text_height(sub_s) + 8.0f) : 0.0f;
    const float larg  = ns_maxf(total + 44.0f, sub_w + 44.0f);
    const float haut  = ligne + 22.0f + sub_h;

    /*
     * L'INVITE EST ANCREE EN BAS, ET CE N'EST PAS UN GOUT.
     *
     * Elle etait a 0,76 de la hauteur, avec l'effet du lot dessine SOUS elle.
     * Mesure sur une capture prise a 1,4 m de la vitrine — c'est-a-dire a la
     * distance ou l'on est quand on achete : la plaque tombait au milieu de la
     * liste des lots et en cachait les deux dernieres lignes. Une invite qui
     * annonce « PLAQUE DOREE » en couvrant la ligne « PLAQUE DOREE » de la
     * liste demande de choisir sans voir.
     *
     * Deux corrections, et la seconde vient de la premiere. D'abord l'ancrage :
     * le bas de la plaque se cale au-dessus du bandeau d'aide, quelle que soit
     * sa hauteur. Ensuite l'effet du lot, qui REJOINT la plaque au lieu de
     * pendre dessous — descendue au ras du bandeau, une sous-ligne exterieure
     * s'imprimait par-dessus, ce que la premiere version de ce correctif a
     * effectivement produit et ce qu'une capture a montre.
     *
     * Ce que ca deplace : le message d'economie etait juste dessous, a 0,86. Il
     * monte a 0,19, sous le solde. C'est sa vraie place — « +14 TICKETS » est
     * une NOTIFICATION, elle appartient au coin ou l'on suit son compte, pas au
     * bas de l'ecran ou l'on agit.
     */
    const float y = ROOM_HUD_H * 0.925f - haut;
    const float x = (ROOM_HUD_W - total) * 0.5f;

    panel(s, (ROOM_HUD_W - larg) * 0.5f, y - 11.0f, larg, haut);
    ns_sprite_rect(s, x - 7.0f, y - 5.0f, key_w + 14.0f, ligne + 10.0f, C_KEY);
    static const float dark[4] = { 0.08f, 0.06f, 0.03f, 1.0f };
    ns_sprite_text(s, x, y, scale, dark, "E");
    ns_sprite_text(s, x + key_w + gap * 2.0f, y, scale, C_TEXT, line);

    /* Un prix sans effet annonce ne se decide pas : c'est ce qui distingue une
     * vitrine d'un distributeur. */
    if (sub[0]) centred(s, ROOM_HUD_W * 0.5f, y + ligne + 12.0f, sub_s, C_DIM, sub);
}

/*
 * LE BANDEAU : ce qui vient de se passer, en bas au centre.
 *
 * Fondu sur la dernière demi-seconde, comme le bandeau de réglages, et pour la
 * même raison : un texte qui disparaît d'un coup se lit comme un défaut
 * d'affichage.
 */
static void draw_eco_message(ns_sprite *s, const room_hud_state *st)
{
    if (!st->eco_message || !st->eco_message[0] || st->eco_message_timer <= 0.0f) return;

    const float a = (st->eco_message_timer < 0.5f) ? (st->eco_message_timer / 0.5f) : 1.0f;
    const float scale = 2.6f;
    const float w = ns_sprite_text_width(st->eco_message, scale) + 44.0f;
    const float h = ns_sprite_text_height(scale) + 22.0f;
    const float x = (ROOM_HUD_W - w) * 0.5f;
    /* 0,19 de la hauteur : SOUS LE SOLDE, et non plus au bas de l'écran.
     * « +14 TICKETS » dit ce que le compteur du coin vient de faire ; le mettre
     * à l'autre bout de l'image obligeait à regarder deux endroits pour une
     * seule information. Et le bas est désormais pris par l'invite des
     * comptoirs, qui a dû descendre pour cesser de couvrir la vitrine. */
    const float y = ROOM_HUD_H * 0.19f;

    const float bg[4] = { C_PANEL[0], C_PANEL[1], C_PANEL[2], C_PANEL[3] * a };
    const float fg[4] = { C_GOLD[0], C_GOLD[1], C_GOLD[2], a };
    ns_sprite_rect(s, x, y, w, h, bg);
    centred(s, ROOM_HUD_W * 0.5f, y + 11.0f, scale, fg, st->eco_message);
}

/*
 * LE QUITTE OU DOUBLE, sur l'écran de fin.
 *
 * LE REFUS EST AUSSI FACILE QUE L'ACCEPTATION, et c'est une contrainte de
 * conception, pas une politesse. Les deux réponses sont une touche, elles sont
 * écrites sur la même ligne, dans la même taille et dans la même couleur ; ne
 * rien faire et repartir vaut refus, et verse. Il n'y a ni compte à rebours, ni
 * « êtes-vous sûr », ni animation qui pousse vers le oui.
 *
 * Ce qui est écrit est le RISQUE, pas le gain : « RISQUER 14 » avant « GARDER
 * 14 ». Un pari qui annonce d'abord ce qu'on peut gagner ment par cadrage,
 * même quand tous ses chiffres sont justes.
 */
static void draw_gamble(ns_sprite *s, const room_hud_state *st)
{
    if (!st->playing || !st->dead || !st->eco) return;
    if (st->eco->mise <= 0) return;

    const float w = 640.0f, h = 132.0f;
    const float x = (ROOM_HUD_W - w) * 0.5f, y = ROOM_HUD_H * 0.34f + 200.0f;
    panel(s, x, y, w, h);

    char l[96];
    SDL_snprintf(l, sizeof l, "QUITTE OU DOUBLE : BATTRE %d EN DUR", st->eco->mise_score);
    centred(s, ROOM_HUD_W * 0.5f, y + 18.0f, 2.4f, C_GOLD, l);
    SDL_snprintf(l, sizeof l, "R : RISQUER %d      ESPACE : GARDER %d",
                 st->eco->mise, st->eco->mise);
    centred(s, ROOM_HUD_W * 0.5f, y + 62.0f, 2.2f, C_TEXT, l);
    centred(s, ROOM_HUD_W * 0.5f, y + 98.0f, 1.7f, C_DIM,
            "REPARTIR SANS REPONDRE VAUT GARDER");
}

void room_hud_draw(ns_sprite *s, const room_hud_state *st)
{
    if (!s || !st) return;
    draw_wallet(s, st);
    draw_prompt(s, st);
    draw_poi_prompt(s, st);
    draw_game_overlay(s, st);
    draw_gamble(s, st);
    draw_settings(s, st);
    draw_eco_message(s, st);
    draw_intro(s, st);
}

/* ========================================================================== */
/* L'écran de la borne de classement                                          */
/* ========================================================================== */

void room_hud_draw_leaderboard(ns_sprite *s, float w, float h, double time_seconds)
{
    if (!s) return;

    /*
     * Les cotes sont données pour une dalle de 512 x 288 et mises à l'échelle
     * depuis là.
     *
     * Elles ont été réglées DEUX FOIS, et la seconde est la bonne leçon. La
     * première version était lisible en regardant la texture — et illisible dans
     * le jeu : la dalle fait 62 cm de large, on la lit à deux mètres et demi, et
     * elle n'occupe alors que trois cents pixels de l'écran. Un caractère de six
     * pixels de large sur la texture en fait quatre à l'arrivée.
     *
     * On dessine donc GROS et PEU : quatre lignes par colonne au lieu de cinq,
     * des caractères deux fois plus hauts. Un tableau de scores se lit en
     * passant, pas en s'accroupissant devant.
     *
     * La marge n'est pas décorative non plus : la dalle est déformée en barillet
     * et son cadre déborde, donc les bords ne se voient pas.
     */
    const float u = w / 512.0f;          /* l'unité : un pixel de la dalle de référence */

    static const float bg[4] = { 0.02f, 0.05f, 0.11f, 1.0f };
    ns_sprite_rect(s, 0.0f, 0.0f, w, h, bg);

    static const float rule[4] = { 0.18f, 0.46f, 0.68f, 1.0f };
    ns_sprite_rect(s, w * 0.08f, 46.0f * u, w * 0.84f, 2.0f * u, rule);
    ns_sprite_rect(s, w * 0.08f, 246.0f * u, w * 0.84f, 2.0f * u, rule);

    static const float title[4] = { 0.66f, 0.95f, 1.00f, 1.0f };
    centred(s, w * 0.5f, 14.0f * u, 3.0f * u, title, "MEILLEURS SCORES");

    /*
     * Les colonnes viennent des jeux PORTÉS, et elles défilent.
     *
     * Elles étaient écrites à la main, quatre lignes à rallonger à chaque
     * portage — donc oubliées un jour ou l'autre. Elles se construisent
     * maintenant depuis `ns_game_at`, comme tout ce qui touche aux jeux depuis
     * B12 : un jeu porté apparaît au classement sans qu'on y pense.
     *
     * Mais on n'en affiche que QUATRE à la fois, et c'est le point important.
     * La dalle fait 62 cm et se lit à deux mètres et demi ; les cotes ont été
     * réglées deux fois pour ça (voir plus haut). Serrer huit colonnes dedans
     * rendrait le tableau complet et illisible, ce qui est pire qu'incomplet.
     * On tourne donc les pages toutes les six secondes — le temps de lire
     * quatre colonnes en passant, et de voir qu'il y en a d'autres.
     */
    struct column { const char *game, *diff, *label; };
    struct column all[NS_HUD_MAX_COLUMNS];
    char hard_label[NS_HUD_MAX_COLUMNS][8];
    int total = 0;

    for (int i = 0; i < ns_game_count() && total + 2 <= NS_HUD_MAX_COLUMNS; ++i) {
        const ns_game_api *api = ns_game_at(i);
        if (!api) continue;
        all[total].game = api->id;
        all[total].diff = "normal";
        all[total].label = api->label;
        total++;
        /* « SNAKE » devient « S.HARD » : l'initiale suffit à distinguer, et six
         * caractères est tout ce qu'une colonne accepte. */
        SDL_snprintf(hard_label[total], sizeof hard_label[total], "%c.HARD",
                     api->label[0] ? api->label[0] : '?');
        all[total].game = api->id;
        all[total].diff = "hard";
        all[total].label = hard_label[total];
        total++;
    }
    if (total == 0) {
        centred(s, w * 0.5f, 130.0f * u, 2.0f * u, title, "AUCUN JEU PORTE");
        return;
    }

    const int cols = (total < 4) ? total : 4;
    const int pages = (total + cols - 1) / cols;
    const int page = pages > 1 ? (int)((time_seconds / 6.0)) % pages : 0;
    const int first = page * cols;
    const struct column *col = &all[first];
    const int shown = (total - first < cols) ? (total - first) : cols;

    static const float head[4] = { 1.00f, 0.82f, 0.35f, 1.0f };
    static const float row[4]  = { 0.88f, 0.94f, 1.00f, 1.0f };
    static const float dim[4]  = { 0.44f, 0.56f, 0.70f, 1.0f };

    /* La dernière page peut être incomplète : on la CENTRE plutôt que de la
     * laisser calée à gauche avec un demi-tableau vide à droite. La largeur de
     * colonne, elle, ne bouge pas d'une page à l'autre — sinon le tableau
     * semblerait respirer à chaque changement. */
    const float page_shift = w * (float)(cols - shown) / (2.0f * (float)cols);

    /*
     * Un FILET entre les colonnes.
     *
     * Le tableau se lisait « 1 2 1 1 --- 1 37 » : quatre colonnes de rang et de
     * score posées côte à côte, sans rien pour dire où l'une finit. L'œil
     * appariait alors le score d'une colonne avec le rang de la suivante, et
     * une ligne juste devenait une ligne fausse. Le pas des colonnes est le
     * même partout, donc un trait vertical suffit — deux points de large,
     * assez sombre pour ne pas concurrencer les chiffres.
     */
    for (int c = 1; c < shown; ++c) {
        const float x = page_shift + w * (float)c / (float)cols;
        ns_sprite_rect(s, x - 1.0f * u, 48.0f * u, 2.0f * u, 196.0f * u, dim);
    }

    for (int c = 0; c < shown; ++c) {
        /* Réparties régulièrement : à deux colonnes on pouvait les poser à la
         * main, à quatre il faut compter. */
        const float cx = page_shift + w * (0.5f + (float)c) / (float)cols;
        float y = 58.0f * u;

        centred(s, cx, y, 1.8f * u, head, col[c].label);
        y = 92.0f * u;

        const ns_score_board *b = ns_scores_board(col[c].game, col[c].diff);
        const uint32_t count = b ? b->count : 0u;

        /*
         * Le nom n'occupe sa colonne que si QUELQU'UN en a un.
         *
         * Le format était `"%u %-3.3s %5u"` en toutes circonstances. Or le jeu
         * ne demande jamais de nom — `--nom=` existe, personne ne le passe — et
         * un nom vide y laissait SEPT blancs entre le rang et le score, c'est-à-
         * dire plus large que l'espace qui sépare deux colonnes du tableau.
         * L'œil appariait alors le score d'une colonne avec le rang de la
         * suivante : la ligne « 1  2  1  1 » se lit aussi bien 1-2 / 1-1 que
         * 1 / 2-1 / 1. Un tableau dont on ne sait pas à quelle colonne appartient
         * un chiffre ne dit rien.
         *
         * La décision se prend par TABLEAU et non par ligne : si un seul joueur
         * s'est nommé, la colonne du nom reste pour tous, sinon les quatre lignes
         * ne s'aligneraient plus entre elles.
         */
        bool has_name = false;
        for (uint32_t i = 0; i < count && i < 4; ++i) {
            if (b->entry[i].name[0]) { has_name = true; break; }
        }

        if (count == 0) {
            /*
             * Un tableau vide se dessine comme un tableau, pas comme un message.
             *
             * Il portait « AUCUN » puis « SCORE », sur deux lignes posées au `y`
             * exact de la première ligne de résultat et espacées de 26 u quand
             * les lignes le sont de 32. Résultat : à côté d'une colonne pleine,
             * on lisait « rang 1 : AUCUN, rang 2 : SCORE ». Le message d'absence
             * se faisait passer pour deux résultats.
             *
             * Quatre rangs et des tirets disent la même chose sans pouvoir être
             * pris pour autre chose, et ils s'alignent exactement sur les
             * colonnes voisines — ce qui est justement ce qu'on leur demande.
             */
            for (uint32_t i = 0; i < 4; ++i) {
                char line[48];
                if (has_name) SDL_snprintf(line, sizeof line, "%u %-3.3s %5s", i + 1u, "", "---");
                else          SDL_snprintf(line, sizeof line, "%u %5s", i + 1u, "---");
                centred(s, cx, y, 1.6f * u, dim, line);
                y += 32.0f * u;
            }
            continue;
        }

        for (uint32_t i = 0; i < count && i < 4; ++i) {
            char line[48];
            const ns_score_entry *e = &b->entry[i];
            /*
             * Largeurs FIXES pour que les scores s'alignent : un tableau dont
             * les chiffres ne sont pas en colonne ne se lit pas d'un coup d'œil,
             * et un coup d'œil est tout ce qu'on lui accorde en passant.
             *
             * Le nom est tronqué à TROIS caractères. C'est la tradition du
             * genre, et c'est ici une contrainte de place : à quatre colonnes
             * une colonne fait 128 points, et une ligne plus longue déborde sur
             * la voisine — les deux deviennent illisibles au lieu d'une.
             *
             * Un nom vide reste vide plutôt que de devenir « ANONYME » : le jeu
             * n'a jamais demandé de nom, il n'a pas à en inventer un.
             */
            if (has_name) {
                SDL_snprintf(line, sizeof line, "%u %-3.3s %5u",
                             i + 1u, e->name[0] ? e->name : "", e->score);
            } else {
                SDL_snprintf(line, sizeof line, "%u %5u", i + 1u, e->score);
            }

            /* La première ligne respire lentement : c'est le record à battre. */
            const float pulse = (i == 0)
                ? (0.74f + 0.26f * (float)(0.5 + 0.5 * sin(time_seconds * 2.2)))
                : 1.0f;
            const float rgba[4] = { row[0] * pulse, row[1] * pulse, row[2] * pulse, 1.0f };
            centred(s, cx, y, 1.6f * u, rgba, line);
            y += 32.0f * u;
        }
    }

    /*
     * Le classement MONDIAL, sous les scores locaux — quand il y en a un.
     *
     * C'est tout l'intérêt du réseau : voir qu'on est vingtième plutôt que
     * premier chez soi. Sans serveur configuré, rien ne s'affiche et la ligne
     * du bas dit simplement « scores locaux » — le jeu n'a pas à s'excuser de
     * tourner seul.
     */
    ns_online_board world;
    /* `envol` et non `flappy` : le jeu a été rebaptisé, et un identifiant resté
     * en arrière ne rend pas d'erreur — il rend un tableau vide. */
    if (ns_online_board_get("envol", "normal", &world) && world.count > 0) {
        /*
         * UNE ligne, pas un second tableau : sous le trait il reste 42 points de
         * haut, et un bloc de quatre lignes en débordait — les scores mondiaux
         * tombaient hors de la dalle. Le meneur mondial suffit à dire ce qu'on a
         * besoin de savoir : jusqu'où il faut monter.
         */
        static const float gold[4] = { 1.00f, 0.84f, 0.38f, 1.0f };
        char line[64];
        SDL_snprintf(line, sizeof line, "MONDIAL ENVOL  %-3.3s %u",
                     world.row[0].name, world.row[0].score);
        centred(s, w * 0.5f, 256.0f * u, 1.7f * u, gold, line);
        centred(s, w * 0.5f, 274.0f * u, 1.3f * u, dim, "SCORES LOCAUX CI-DESSUS");
    } else {
        centred(s, w * 0.5f, 258.0f * u, 1.6f * u, dim, "SCORES LOCAUX");
    }

    /* Dire qu'il y a une suite. Un tableau qui change tout seul sans l'annoncer
     * ressemble à un bogue ; annoncé, il invite à attendre la page d'après. */
    if (pages > 1) {
        char tag[16];
        SDL_snprintf(tag, sizeof tag, "%d/%d", page + 1, pages);
        centred(s, w * 0.94f, 16.0f * u, 1.6f * u, dim, tag);
    }
}

/* --------------------------------------------------------------------------
 * Le tableau du bar
 * -------------------------------------------------------------------------- */

/* Un score, rendu lisible de loin : 1 240 plutôt que 1240. */
static void group_number(char *out, size_t n, uint32_t v)
{
    char raw[16];
    SDL_snprintf(raw, sizeof raw, "%u", v);
    const size_t len = SDL_strlen(raw);
    size_t o = 0;
    for (size_t i = 0; i < len && o + 2 < n; ++i) {
        if (i > 0 && ((len - i) % 3) == 0) out[o++] = ' ';
        out[o++] = raw[i];
    }
    out[o] = '\0';
}

void room_hud_draw_scoreboard(ns_sprite *s, float w, float h, double time_seconds,
                              const char *my_name, const char *my_game,
                              uint32_t my_score)
{
    if (!s) return;

    /* Le repère reste 640 x 320 quelle que soit la cible : toutes les cotes
     * ci-dessous sont dans ce système, et `u` les y ramène. La cible réelle
     * vaut ROOM_BAR_RT_W x ROOM_BAR_RT_H, un multiple entier — c'est ce qui
     * permet d'en changer sans redessiner la mise en page. */
    const float u = w / 640.0f;

    static const float bg[4]    = { 0.020f, 0.026f, 0.045f, 1.0f };
    static const float rule[4]  = { 0.16f, 0.34f, 0.55f, 1.0f };
    static const float title[4] = { 1.00f, 0.82f, 0.35f, 1.0f };
    static const float live[4]  = { 0.45f, 1.00f, 0.62f, 1.0f };
    static const float row[4]   = { 0.88f, 0.94f, 1.00f, 1.0f };
    static const float dim[4]   = { 0.42f, 0.52f, 0.66f, 1.0f };
    static const float me[4]    = { 1.00f, 0.90f, 0.45f, 1.0f };

    ns_sprite_rect(s, 0.0f, 0.0f, w, h, bg);

    /* Le filet vertical qui sépare les deux moitiés. Sans lui, huit lignes de
     * texte sur 1,70 m se lisent comme un seul bloc. */
    ns_sprite_rect(s, 318.0f * u, 34.0f * u, 2.0f * u, 264.0f * u, rule);
    ns_sprite_rect(s, 20.0f * u, 30.0f * u, 600.0f * u, 2.0f * u, rule);

    centred(s, 160.0f * u, 8.0f * u, 2.4f * u, title, "MEILLEURS SCORES");
    centred(s, 480.0f * u, 8.0f * u, 2.4f * u, live,  "EN DIRECT");

    /*
     * À GAUCHE : le meilleur de chaque jeu, en pages de six.
     *
     * On montre le MEILLEUR de chaque jeu plutôt que le classement complet d'un
     * seul : c'est un tableau qu'on lit en passant devant le bar, et « qui tient
     * le record de quoi » est la question qu'on se pose de loin. Le détail d'un
     * jeu est sur la borne de classement, qui est faite pour ça.
     */
    int shown = 0;
    const int per_page = 6;
    const int total = ns_game_count();
    const int pages = (total + per_page - 1) / per_page;
    const int page = (pages > 1) ? (int)(time_seconds / 7.0) % pages : 0;

    for (int i = page * per_page; i < total && shown < per_page; ++i) {
        const ns_game_api *api = ns_game_at(i);
        if (!api) continue;
        const float y = (48.0f + (float)shown * 38.0f) * u;

        const ns_score_board *b = ns_scores_board(api->id, "normal");
        const bool any = (b && b->count > 0);

        ns_sprite_text(s, 26.0f * u, y, 2.2f * u, row, api->label);
        if (any) {
            char n[24];
            group_number(n, sizeof n, b->entry[0].score);
            ns_sprite_text(s, 150.0f * u, y, 2.2f * u, title, n);
            /* Le nom, s'il y en a un. Un classement local est souvent anonyme,
             * et une colonne de « --- » vaut mieux qu'une colonne absente : elle
             * dit que la place existe et qu'elle est à prendre. */
            ns_sprite_text(s, 236.0f * u, y, 1.8f * u, dim,
                           b->entry[0].name[0] ? b->entry[0].name : "---");
        } else {
            ns_sprite_text(s, 150.0f * u, y, 2.2f * u, dim, "---");
        }
        shown++;
    }

    /*
     * À DROITE : qui est là, et à combien il en est.
     *
     * `ns_realtime_peers` ne rend RIEN quand le temps réel est éteint, ce qui
     * est le cas par défaut — et c'est très bien. On affiche alors le joueur
     * local seul, ce qui est la vérité de la salle : il y est seul.
     */
    ns_realtime_peer peer[8];
    /* La date du lot ne sert à rien ici : ce tableau ne fait qu'écrire des
     * noms, il n'interpole aucune position. */
    const uint32_t n = ns_realtime_peers(peer, 8, NULL);

    int line = 0;
    if (my_name && my_name[0]) {
        const float y = (48.0f + (float)line * 38.0f) * u;
        ns_sprite_text(s, 336.0f * u, y, 2.2f * u, me, my_name);
        if (my_game && my_game[0]) {
            char sc[24];
            group_number(sc, sizeof sc, my_score);
            ns_sprite_text(s, 470.0f * u, y, 1.8f * u, dim, my_game);
            ns_sprite_text(s, 566.0f * u, y, 2.2f * u, me, sc);
        } else {
            ns_sprite_text(s, 470.0f * u, y, 1.8f * u, dim, "dans la salle");
        }
        line++;
    }
    for (uint32_t i = 0; i < n && line < 6; ++i, ++line) {
        const float y = (48.0f + (float)line * 38.0f) * u;
        ns_sprite_text(s, 336.0f * u, y, 2.2f * u,
                       peer[i].verified ? row : dim, peer[i].name);
        if (peer[i].game[0]) {
            char sc[24];
            group_number(sc, sizeof sc, (uint32_t)(peer[i].score > 0 ? peer[i].score : 0));
            ns_sprite_text(s, 470.0f * u, y, 1.8f * u, dim, peer[i].game);
            ns_sprite_text(s, 566.0f * u, y, 2.2f * u, live, sc);
        } else {
            ns_sprite_text(s, 470.0f * u, y, 1.8f * u, dim, "dans la salle");
        }
    }
    if (line == 0) {
        centred(s, 480.0f * u, 140.0f * u, 2.0f * u, dim, "SALLE VIDE");
    }

    /* Le bandeau du bas : ce qu'il faut faire pour y apparaître. Un tableau qui
     * ne dit pas comment y entrer est une décoration. */
    ns_sprite_rect(s, 20.0f * u, 292.0f * u, 600.0f * u, 2.0f * u, rule);
    centred(s, 320.0f * u, 300.0f * u, 1.8f * u, dim,
            "GLISSE UN JETON - E DEVANT UNE BORNE");
}
