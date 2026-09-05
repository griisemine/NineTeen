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

/*
 * LA PLAQUE DES INVITES — la même que `panel`, mais aux coins arrondis.
 *
 * Pourquoi une seconde fonction plutôt qu'un arrondi posé sur `panel` : les
 * autres cadres de cet écran sont des PAGES — le solde, la fin de partie, le
 * quitte ou double — et une page a un bord franc, qui est ce qui la sépare de
 * la salle. Les deux invites, elles, sont des ÉTIQUETTES posées
 * dans l'image : elles doivent se lire comme un objet du jeu et non comme un
 * bout d'interface collé par-dessus, et c'est l'angle vif qui trahit le second.
 *
 * L'arrondi est fait de bandes d'un point de haut, parce que `ns_sprite` ne
 * sait dessiner que des rectangles. Vingt-quatre rectangles de plus pour un lot
 * qui en tient 4096 : ça ne se discute pas. Le repère logique étant fixe
 * (1280 x 720), le rayon de 12 points garde la même proportion aux neuf
 * résolutions — mesuré à 1280x720 et à 3840x2160, l'arrondi occupe la même
 * fraction de la plaque.
 *
 * Le liseré ne fait plus toute la largeur : il s'arrête au rayon, sinon il
 * dépasserait des coins qu'il est censé suivre.
 */
static void plaque(ns_sprite *s, float x, float y, float w, float h)
{
    static const float edge[4] = { 1.00f, 0.72f, 0.34f, 0.34f };
    const float r = ns_minf(12.0f, ns_minf(w, h) * 0.4f);

    ns_sprite_rect(s, x, y + r, w, h - 2.0f * r, C_PANEL);
    for (int i = 0; i < (int)r; ++i) {
        const float d = r - (float)i - 0.5f;
        const float dx = r - sqrtf(ns_maxf(0.0f, r * r - d * d));
        ns_sprite_rect(s, x + dx, y + (float)i,            w - 2.0f * dx, 1.0f, C_PANEL);
        ns_sprite_rect(s, x + dx, y + h - (float)i - 1.0f, w - 2.0f * dx, 1.0f, C_PANEL);
    }

    ns_sprite_rect(s, x + r, y,            w - 2.0f * r, 2.0f, edge);
    ns_sprite_rect(s, x + r, y + h - 2.0f, w - 2.0f * r, 2.0f, edge);
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

    /*
     * LE PRIX DE LA BORNE, pendant une manche seulement.
     *
     * « x3,5 EN 157 S » : ce que cette borne paie, et ce qu'elle coûte en
     * exposition. Les deux nombres ensemble, jamais l'un sans l'autre — un
     * multiplicateur sans durée ferait choisir toujours le plus gros, et c'est
     * exactement le choix que le mode existe pour rendre difficile.
     */
    char prix[48];
    prix[0] = '\0';
    if (st->cp_multiplicateur > 0.0f) {
        SDL_snprintf(prix, sizeof prix, "x%.2f  EN %.0f S",
                     (double)st->cp_multiplicateur, (double)st->cp_duree);
    }

    /*
     * LE MEILLEUR SCORE LOCAL DE CETTE BORNE, calculé ICI, avant la mise en
     * page — c'est l'information qui transforme « je peux jouer » en « je peux
     * faire mieux », et c'est aussi une LIGNE, donc de l'encombrement. La
     * mesurer après avoir posé la plaque est exactement l'erreur que l'invite
     * des comptoirs a déjà commise et corrigée.
     */
    char sub[64];
    sub[0] = '\0';
    {
        const uint32_t best = ns_scores_best(st->near->game,
                                             ns_scores_bucket(st->near->difficulty));
        if (best > 0) SDL_snprintf(sub, sizeof sub, "MEILLEUR : %u", best);
    }

    const float scale = 3.0f;
    const float prix_scale = 2.4f;
    const float sub_scale  = 2.0f;
    const float key_w = ns_sprite_text_width("E", scale);
    const float txt_w = ns_sprite_text_width(line, scale);
    const float gap   = 14.0f;
    const float total = key_w + gap * 2.0f + txt_w;
    const float ligne = ns_sprite_text_height(scale);

    const float prix_h = prix[0] ? ns_sprite_text_height(prix_scale) + 8.0f : 0.0f;
    const float sub_h  = sub[0]  ? ns_sprite_text_height(sub_scale)  + 8.0f : 0.0f;
    const float h = ligne + 22.0f + prix_h + sub_h;

    /*
     * L'INVITE EST ANCRÉE EN BAS, ET C'EST LE MÊME RAISONNEMENT QUE POUR CELLE
     * DES COMPTOIRS, plus bas dans ce fichier — repris ici parce qu'il n'y avait
     * été appliqué qu'à moitié.
     *
     * Elle était à 0,70 de la hauteur. Mesuré sur une capture prise sur l'ancre
     * de `borne_arcade_1` — c'est-à-dire à l'endroit exact où elle s'allume :
     * la plaque tombait EN TRAVERS DE LA DALLE de la borne, et la ligne
     * « MEILLEUR : 6 » avec elle. Une invite qui propose de jouer en couvrant
     * l'écran du jeu qu'elle propose se retire elle-même son argument — la
     * dalle est ce qui donne envie d'appuyer, et c'est la seule chose que cette
     * plaque n'avait pas le droit de cacher.
     *
     * 0,925 de la hauteur pour le BAS de la plaque, exactement comme l'invite
     * des comptoirs : les deux ne s'affichent jamais ensemble, et les poser au
     * même endroit fait qu'on ne les cherche jamais.
     *
     * Et « MEILLEUR » REJOINT LA PLAQUE au lieu de pendre dessous. Il était
     * dessiné à `y + h + 6`, c'est-à-dire hors du cadre : une fois la plaque
     * descendue au ras du bas, cette ligne se serait imprimée sur le bandeau
     * d'aide — l'erreur exacte que la première version du correctif de la
     * vitrine a produite, et qu'une capture a montrée.
     */
    const float y = ROOM_HUD_H * 0.925f - h;
    const float x = (ROOM_HUD_W - total) * 0.5f;
    const float larg = ns_maxf(total + 44.0f,
                               ns_maxf(prix[0] ? ns_sprite_text_width(prix, prix_scale) + 44.0f : 0.0f,
                                       sub[0]  ? ns_sprite_text_width(sub,  sub_scale)  + 44.0f : 0.0f));

    plaque(s, (ROOM_HUD_W - larg) * 0.5f, y - 11.0f, larg, h);

    /* La touche dans un carré : c'est ce qui la distingue du mot qui suit, et
     * c'est la convention que tout le monde lit sans l'avoir apprise. */
    ns_sprite_rect(s, x - 7.0f, y - 5.0f, key_w + 14.0f, ligne + 10.0f, C_KEY);
    static const float dark[4] = { 0.08f, 0.06f, 0.03f, 1.0f };
    ns_sprite_text(s, x, y, scale, dark, "E");
    ns_sprite_text(s, x + key_w + gap * 2.0f, y, scale, C_TEXT, line);

    if (prix[0]) {
        centred(s, ROOM_HUD_W * 0.5f, y + ligne + 10.0f, prix_scale, C_GOLD, prix);
    }
    if (sub[0]) {
        centred(s, ROOM_HUD_W * 0.5f, y + ligne + prix_h + 10.0f, sub_scale, C_DIM, sub);
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

    plaque(s, (ROOM_HUD_W - larg) * 0.5f, y - 11.0f, larg, haut);
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

/*
 * LE BANDEAU DE MISE À JOUR, en haut à gauche et sur une seule ligne.
 *
 * En HAUT, parce que tout le bas de l'écran est déjà pris : l'invite des
 * comptoirs y a été descendue pour cesser de couvrir la vitrine, et le quitte
 * ou double s'y installe à la fin d'une partie. En haut à GAUCHE parce que le
 * solde tient le coin droit.
 *
 * Il ne clignote pas et ne bouge pas. Une mise à jour n'est pas urgente : elle
 * attend qu'on ait fini de jouer, et un bandeau qui s'agite apprend surtout à
 * ne plus le regarder.
 */
static void draw_maj(ns_sprite *s, const room_hud_state *st)
{
    if (!st->maj_texte || !st->maj_texte[0]) return;

    const float scale = 2.0f;
    const float w = ns_sprite_text_width(st->maj_texte, scale) + 28.0f;
    const float h = ns_sprite_text_height(scale) + 16.0f;
    const float x = 24.0f, y = 24.0f;

    ns_sprite_rect(s, x, y, w, h, C_PANEL);
    ns_sprite_text(s, x + 14.0f, y + 8.0f, scale, C_KEY, st->maj_texte);

    /* La barre, seulement pendant un transfert. Deux pixels de haut sous le
     * texte : elle dit qu'il se passe quelque chose sans réclamer l'écran. */
    if (st->maj_avancement >= 0.0f) {
        const float bw = w - 28.0f;
        const float by = y + h - 5.0f;
        const float fond[4] = { 0.20f, 0.17f, 0.13f, 0.9f };
        ns_sprite_rect(s, x + 14.0f, by, bw, 3.0f, fond);
        const float part = (st->maj_avancement > 1.0f) ? 1.0f : st->maj_avancement;
        if (part > 0.0f) ns_sprite_rect(s, x + 14.0f, by, bw * part, 3.0f, C_GOLD);
    }
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
    draw_maj(s, st);
    draw_intro(s, st);
}

/* ========================================================================== */
/* L'écran de la borne de classement                                          */
/* ========================================================================== */

/*
 * LA PAGE DE SAISON, sur la dalle de la borne de classement.
 *
 * Elle repond a une question que les records locaux ne posent meme pas : « qui
 * est fort EN CE MOMENT, et ou est-ce que j'en suis ». Un record de toujours ne
 * donne envie de rien a qui arrive, parce qu'il est deja pris depuis des mois.
 * Une saison qui se termine dans vingt-huit jours, si.
 *
 * GROS ET PEU, comme le reste de cette dalle : elle fait 62 cm et se lit a deux
 * metres et demi. Cinq lignes, pas six. Le conseil detaille — « marquez 4 401
 * sur Piano » — ne tient pas a cette taille et vit sur le site, qui a la place.
 */
static void draw_saison(ns_sprite *s, float w, float u, const ns_saison *sa)
{
    static const float title[4] = { 0.66f, 0.95f, 1.00f, 1.0f };
    static const float head[4]  = { 1.00f, 0.82f, 0.35f, 1.0f };
    static const float row[4]   = { 0.88f, 0.94f, 1.00f, 1.0f };
    static const float dim[4]   = { 0.44f, 0.56f, 0.70f, 1.0f };
    static const float moi[4]   = { 0.21f, 0.88f, 0.63f, 1.0f };

    static const float filet[4] = { 0.18f, 0.46f, 0.68f, 1.0f };
    ns_sprite_rect(s, w * 0.08f, 46.0f * u, w * 0.84f, 2.0f * u, filet);
    ns_sprite_rect(s, w * 0.08f, 196.0f * u, w * 0.84f, 2.0f * u, filet);

    centred(s, w * 0.5f, 14.0f * u, 3.0f * u, title, "SAISON");

    char ligne[64];
    /* Le mois EN MAJUSCULES : tout le reste de cette dalle l'est, et une
     * minuscule au milieu se lit comme une faute a cette distance. */
    char mois[32];
    SDL_strlcpy(mois, sa->libelle[0] ? sa->libelle : "EN COURS", sizeof mois);
    for (char *p = mois; *p; ++p) {
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    }
    if (sa->jours_restants > 0) {
        SDL_snprintf(ligne, sizeof ligne, "%s   %d JOURS", mois, sa->jours_restants);
    } else {
        SDL_snprintf(ligne, sizeof ligne, "%s   DERNIER JOUR", mois);
    }
    centred(s, w * 0.5f, 56.0f * u, 1.8f * u, head, ligne);

    if (sa->podium_count == 0) {
        centred(s, w * 0.5f, 130.0f * u, 2.2f * u, row, "AUCUN SCORE CE MOIS-CI");
        centred(s, w * 0.5f, 168.0f * u, 1.8f * u, moi, "LA PREMIERE PLACE EST LIBRE");
        return;
    }

    for (uint32_t i = 0; i < sa->podium_count; ++i) {
        const ns_saison_ligne *l = &sa->podium[i];
        /* Le pseudo est borne a huit caracteres : au-dela, deux noms longs se
         * touchent et l'oeil ne sait plus ou finit le premier. */
        SDL_snprintf(ligne, sizeof ligne, "%u  %-8.8s %5u  %s",
                     i + 1u, l->pseudo, l->points, l->palier);
        centred(s, w * 0.5f, (92.0f + 34.0f * (float)i) * u, 2.0f * u,
                i == 0 ? head : row, ligne);
    }

    /* MA LIGNE, en vert d'ecran : c'est la seule de la dalle qui parle de moi,
     * et elle doit se trouver sans etre cherchee. */
    if (sa->moi) {
        SDL_snprintf(ligne, sizeof ligne, "VOUS  %u%s  %u PTS  %s",
                     sa->ma_ligne.rang, sa->ma_ligne.rang == 1u ? "er" : "e",
                     sa->ma_ligne.points, sa->ma_ligne.palier);
        centred(s, w * 0.5f, 212.0f * u, 2.0f * u, moi, ligne);
        if (sa->ma_ligne.rang > 1u && sa->mon_ecart > 0u) {
            /*
             * 1,8 ET PAS 1,5. La police de la couche 2D est une police
             * matricielle : sous 1,7 unite, les glyphes se decomposent et le
             * texte devient illisible. Mesure sur capture de cette dalle :
             * a 1,5 « POINTS DU RANG » se lit « FCINTS CL RANG », a 1,3
             * « SCORES LOCAUX CI-DESSUS » se lit « SCCRES LOCAUX CI-CESSUS ».
             * A 1,7 et au-dessus, tout est net. Le seuil est donc entre les
             * deux, et rien ici ne descend plus dessous.
             */
            SDL_snprintf(ligne, sizeof ligne, "%u POINTS DU RANG DEVANT", sa->mon_ecart);
            centred(s, w * 0.5f, 250.0f * u, 1.8f * u, dim, ligne);
        }
    } else {
        centred(s, w * 0.5f, 212.0f * u, 1.8f * u, moi, "F1 POUR CREER UN COMPTE");
        centred(s, w * 0.5f, 250.0f * u, 1.8f * u, dim, "TROIS PARTIES SUFFISENT");
    }
}

void room_hud_draw_leaderboard(ns_sprite *s, float w, float h, double time_seconds,
                               const ns_saison *saison)
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
    static const float title[4] = { 0.66f, 0.95f, 1.00f, 1.0f };

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
        centred(s, w * 0.5f, 14.0f * u, 3.0f * u, title, "MEILLEURS SCORES");
        centred(s, w * 0.5f, 130.0f * u, 2.0f * u, title, "AUCUN JEU PORTE");
        return;
    }

    const int cols = (total < 4) ? total : 4;
    const int pages = (total + cols - 1) / cols;

    /*
     * LA SAISON PREND UNE PAGE DE PLUS, et pas la place d'une autre.
     *
     * Six secondes chacune, comme les pages de records : le temps de lire en
     * passant, et de voir qu'il y en a d'autres. Un joueur hors ligne garde
     * exactement l'ecran qu'il avait — `saison` est alors nul et le total ne
     * bouge pas.
     */
    const bool avec_saison = saison && saison->fresh;
    const int total_pages = pages + (avec_saison ? 1 : 0);
    const int page = total_pages > 1 ? (int)((time_seconds / 6.0)) % total_pages : 0;

    /*
     * Dire qu'il y a une suite. Un tableau qui change tout seul sans
     * l'annoncer ressemble a un bogue ; annonce, il invite a attendre la page
     * d'apres.
     *
     * La pastille est posee ICI, avant que les deux branches se separent, et
     * elle compte TOUTES les pages. Elle etait dessinee dans la branche des
     * records seule et comptait les records seuls : la dalle affichait « 1/4 »
     * alors que cinq pages defilaient, et la cinquieme avait l'air d'arriver de
     * nulle part.
     */
    if (total_pages > 1) {
        static const float pastille[4] = { 0.44f, 0.56f, 0.70f, 1.0f };
        char tag[16];
        SDL_snprintf(tag, sizeof tag, "%d/%d", page + 1, total_pages);
        centred(s, w * 0.94f, 16.0f * u, 1.6f * u, pastille, tag);
    }

    /*
     * LA PAGE DE SAISON EST UNE PAGE ENTIERE, et l'on sort AVANT que la mise en
     * page des records ne pose la sienne.
     *
     * Le premier jet sortait plus bas : le fond, les deux filets et le titre
     * « MEILLEURS SCORES » etaient deja dessines, si bien que « SAISON »
     * s'ecrivait par-dessus et que le pied « SCORES LOCAUX CI-DESSUS » passait
     * sous l'ecart. Vu sur capture, deux titres empiles et une ligne illisible.
     */
    if (avec_saison && page == pages) {
        draw_saison(s, w, u, saison);
        return;
    }

    ns_sprite_rect(s, w * 0.08f, 46.0f * u, w * 0.84f, 2.0f * u, rule);
    ns_sprite_rect(s, w * 0.08f, 246.0f * u, w * 0.84f, 2.0f * u, rule);
    centred(s, w * 0.5f, 14.0f * u, 3.0f * u, title, "MEILLEURS SCORES");

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
        centred(s, w * 0.5f, 254.0f * u, 1.7f * u, gold, line);
        /* 1,7 comme la ligne du dessus, et non 1,3 : voir la note sur le seuil
         * de lisibilite dans `draw_saison`. A 1,3 cette ligne se lisait
         * « SCCRES LOCAUX CI-CESSUS », ce qui est pire que de ne rien ecrire. */
        centred(s, w * 0.5f, 272.0f * u, 1.7f * u, dim, "SCORES LOCAUX CI-DESSUS");
    } else {
        centred(s, w * 0.5f, 258.0f * u, 1.7f * u, dim, "SCORES LOCAUX");
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

/* Aligné à DROITE sur `x`. Un score se lit par la droite : c'est là que sont les
 * unités, et c'est ce qui met les chiffres d'une colonne en regard les uns des
 * autres quel que soit leur nombre. La version d'avant les posait à gauche, et
 * un 980 tombait donc sous le premier chiffre d'un 12 400. */
static void a_droite(ns_sprite *s, float x, float y, float scale,
                     const float rgba[4], const char *text)
{
    ns_sprite_text(s, x - ns_sprite_text_width(text, scale), y, scale, rgba, text);
}

/*
 * LE TABLEAU DU BAR, refait pour être LU.
 *
 * Le défaut, mesuré, et il était écrit dans `room/main.c` sans être corrigé :
 * « de LOIN, le lettrage reste sous le seuil de lisibilité du projet. Mesuré sur
 * la vue « bar » (caméra à 4,10 m) en 1400 x 875, un caractère du titre fait
 * 8,4 px de haut quand le seuil relevé sur ce dépôt est 11. La définition n'y
 * change rien : c'est la TAILLE DU LETTRAGE SUR LE PANNEAU qu'il faudrait
 * revoir, pas le nombre de texels. »
 *
 * C'est ce qu'on fait ici. La capture le confirmait sans appel : depuis le
 * comptoir, l'enseigne au néon au-dessus du panneau se lit, la petite borne de
 * classement à trois mètres à droite se lit, et le grand écran entre les deux
 * est une bouillie grise. Un téléviseur qu'on ne lit pas de la pièce où il est
 * accroché n'est pas un téléviseur, quelle que soit sa dalle.
 *
 * LE CALCUL, puisqu'il décide de tout le reste. Un caractère de `scale` s
 * mesure 7 s unités du repère 640 x 320, donc 7 s / 320 x 0,89 m sur une dalle
 * haute de 0,89 m. Vu de 4,24 m avec un champ vertical de 62° sur 875 px, il
 * fait 875 x (hauteur / 4,24) / (2 tan 31°) pixels :
 *
 *     scale   hauteur   pixels à 4,24 m
 *      1,8    4,50 cm       6,0        (l'ancien pied de page)
 *      2,2    5,50 cm       7,3        (les anciennes lignes)
 *      2,4    6,00 cm       8,0        (les anciens titres)
 *      2,6    6,50 cm       8,7
 *      3,6    9,00 cm      12,0        (le bandeau)
 *      4,0   10,00 cm      13,4        (les lignes)
 *
 * Le seuil est 11. Rien n'y était ; les lignes et le bandeau y sont maintenant.
 * Le pied de page reste dessous, à 8,7, et c'est ASSUMÉ : il porte une consigne
 * qu'on lit une fois en s'approchant, pas une donnée qu'on saisit en passant.
 *
 * CE QUE ÇA COÛTE, ET COMMENT ON LE PAIE. Du lettrage deux fois plus haut, c'est
 * quatre fois moins de lignes : on passait huit jeux et six joueurs sur une
 * seule image, en deux colonnes. On ne peut plus. Le panneau TOURNE donc — deux
 * pages de quatre records, entrecoupées de la page « en direct » — et c'est ce
 * que fait n'importe quel afficheur de hall. La règle qui justifiait les deux
 * colonnes tient toujours et elle est même mieux servie : « c'est un tableau
 * qu'on lit en passant devant le bar, et QUI TIENT LE RECORD DE QUOI est la
 * question qu'on se pose de loin. »
 *
 * LE NOIR EST VRAI. Le fond était à (0,020 ; 0,026 ; 0,045) : sur une dalle
 * désormais neutre et à 1,9 d'émissif, ça donnait un panneau bleu allumé de
 * bout en bout, l'inverse exact de ce qui distingue un panneau haut de gamme.
 * Il tombe à (0,003 ; 0,004 ; 0,008), c'est-à-dire éteint. Ce que le panneau
 * rend à la salle, il le rend par son BANDEAU et par ses chiffres — comme un
 * vrai afficheur, où la lumière vient de ce qui est écrit.
 */
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

    static const float bg[4]    = { 0.003f, 0.004f, 0.008f, 1.0f };
    static const float bande[4] = { 0.100f, 0.235f, 0.440f, 1.0f };
    static const float rule[4]  = { 0.20f, 0.52f, 0.85f, 1.0f };
    static const float clair[4] = { 0.95f, 0.98f, 1.00f, 1.0f };
    static const float title[4] = { 1.00f, 0.80f, 0.30f, 1.0f };
    static const float live[4]  = { 0.40f, 1.00f, 0.58f, 1.0f };
    static const float row[4]   = { 0.90f, 0.95f, 1.00f, 1.0f };
    static const float dim[4]   = { 0.45f, 0.55f, 0.68f, 1.0f };
    static const float me[4]    = { 1.00f, 0.88f, 0.40f, 1.0f };
    static const float eteint[4]= { 0.16f, 0.24f, 0.36f, 1.0f };

    ns_sprite_rect(s, 0.0f, 0.0f, w, h, bg);

    /*
     * QUATRE VOLETS, et l'alternance n'est pas décorative : « en direct » revient
     * une fois sur deux. Un afficheur qui montrerait les records pendant douze
     * secondes d'affilée laisserait quelqu'un qui traverse la salle sans jamais
     * voir qu'il y est. Six secondes par volet : le temps de lire quatre lignes
     * sans avoir à s'arrêter.
     */
    const int volet = (int)(time_seconds / 6.0) % 4;
    const bool page_direct = (volet % 2) == 1;
    const int  page_scores = volet / 2;          /* 0 puis 1 */
    const int  par_page = 4;

    /* --- le bandeau : c'est lui qui éclaire, et lui qu'on lit en premier --- */
    ns_sprite_rect(s, 0.0f, 0.0f, w, 58.0f * u, bande);
    ns_sprite_rect(s, 0.0f, 58.0f * u, w, 3.0f * u, rule);
    ns_sprite_text(s, 22.0f * u, 15.0f * u, 3.6f * u, clair,
                   page_direct ? "EN DIRECT" : "MEILLEURS SCORES");

    /* Où l'on en est dans le cycle. Quatre pastilles : sans elles, un panneau
     * qui change tout seul donne l'impression d'avoir raté quelque chose. */
    for (int i = 0; i < 4; ++i) {
        ns_sprite_rect(s, (556.0f + (float)i * 20.0f) * u, 26.0f * u,
                       12.0f * u, 6.0f * u, (i == volet) ? clair : eteint);
    }

    const float y0 = 78.0f, pas = 56.0f, ligne = 4.0f, menu_ = 2.6f;
    const float droite = 618.0f;

    if (!page_direct) {
        /*
         * LE MEILLEUR DE CHAQUE JEU, quatre par page. On montre toujours le
         * meilleur de chaque jeu plutôt que le classement complet d'un seul :
         * le détail d'un jeu est sur la borne de classement, qui est faite pour
         * ça.
         */
        const int total = ns_game_count();
        int shown = 0;
        for (int i = page_scores * par_page; i < total && shown < par_page; ++i) {
            const ns_game_api *api = ns_game_at(i);
            if (!api) continue;
            const float y = (y0 + (float)shown * pas) * u;

            const ns_score_board *b = ns_scores_board(api->id, "normal");
            const bool any = (b && b->count > 0);

            ns_sprite_text(s, 22.0f * u, y, ligne * u, row, api->label);
            if (any) {
                char n[24];
                group_number(n, sizeof n, b->entry[0].score);
                a_droite(s, droite * u, y, ligne * u, title, n);
                /* Le nom, s'il y en a un. Un classement local est souvent
                 * anonyme, et une colonne de « --- » vaut mieux qu'une colonne
                 * absente : elle dit que la place existe et qu'elle est à
                 * prendre. Posé sur la ligne de base des grands caractères,
                 * plus 8 unités : deux corps différents alignés par le HAUT se
                 * lisent comme un décalage. */
                ns_sprite_text(s, 200.0f * u, y + 8.0f * u, menu_ * u, dim,
                               b->entry[0].name[0] ? b->entry[0].name : "---");
            } else {
                a_droite(s, droite * u, y, ligne * u, dim, "---");
            }
            shown++;
        }
        if (shown == 0) {
            centred(s, 320.0f * u, 150.0f * u, ligne * u, dim, "AUCUN JEU");
        }
    } else {
        /*
         * QUI EST LÀ, et à combien il en est.
         *
         * `ns_realtime_peers` ne rend RIEN quand le temps réel est éteint, ce
         * qui est le cas par défaut — et c'est très bien. On affiche alors le
         * joueur local seul, ce qui est la vérité de la salle : il y est seul.
         */
        ns_realtime_peer peer[8];
        /* La date du lot ne sert à rien ici : ce tableau ne fait qu'écrire des
         * noms, il n'interpole aucune position. */
        const uint32_t n = ns_realtime_peers(peer, 8, NULL);

        int line = 0;
        if (my_name && my_name[0]) {
            const float y = (y0 + (float)line * pas) * u;
            ns_sprite_text(s, 22.0f * u, y, ligne * u, me, my_name);
            if (my_game && my_game[0]) {
                char sc[24];
                group_number(sc, sizeof sc, my_score);
                ns_sprite_text(s, 250.0f * u, y + 8.0f * u, menu_ * u, dim, my_game);
                a_droite(s, droite * u, y, ligne * u, me, sc);
            } else {
                ns_sprite_text(s, 250.0f * u, y + 8.0f * u, menu_ * u, dim, "DANS LA SALLE");
            }
            line++;
        }
        for (uint32_t i = 0; i < n && line < par_page; ++i, ++line) {
            const float y = (y0 + (float)line * pas) * u;
            ns_sprite_text(s, 22.0f * u, y, ligne * u,
                           peer[i].verified ? row : dim, peer[i].name);
            if (peer[i].game[0]) {
                char sc[24];
                group_number(sc, sizeof sc, (uint32_t)(peer[i].score > 0 ? peer[i].score : 0));
                ns_sprite_text(s, 250.0f * u, y + 8.0f * u, menu_ * u, dim, peer[i].game);
                a_droite(s, droite * u, y, ligne * u, live, sc);
            } else {
                ns_sprite_text(s, 250.0f * u, y + 8.0f * u, menu_ * u, dim, "DANS LA SALLE");
            }
        }
        if (line == 0) {
            centred(s, 320.0f * u, 150.0f * u, 4.6f * u, dim, "SALLE VIDE");
        }
    }

    /* Le bandeau du bas : ce qu'il faut faire pour y apparaître. Un tableau qui
     * ne dit pas comment y entrer est une décoration. Il reste sous le seuil de
     * lisibilité à quatre mètres, et c'est assumé : c'est une consigne qu'on lit
     * une fois en s'approchant, pas une donnée qu'on saisit en passant. */
    ns_sprite_rect(s, 24.0f * u, 284.0f * u, 592.0f * u, 2.0f * u, rule);
    centred(s, 320.0f * u, 296.0f * u, 2.6f * u, dim,
            "UN JETON, PUIS E DEVANT UNE BORNE");
}

/* ==========================================================================
 * LE DÉRAILLEMENT D'UNE DALLE FRAPPÉE
 * ==========================================================================
 * Le raisonnement est dans `room_hud.h` ; ici, les trois traits et leurs cotes.
 *
 * Tout est proportionnel à `h`, jamais en pixels : la même fonction dessine
 * dans la dalle de 512 x 288 d'une borne et dans n'importe quelle autre cible,
 * exactement comme le reste de ce fichier rapporte ses cotes à 640.
 */
void room_hud_draw_choc(ns_sprite *s, float w, float h, float choc, float phase)
{
    if (!s || choc <= 0.01f) return;
    const float k = (choc > 1.0f) ? 1.0f : choc;

    /*
     * 1. LA BARRE DE DÉSYNCHRONISATION.
     *
     * Elle monte, et ce n'est pas indifférent : sur un tube dont la fréquence
     * verticale décroche vers le bas, la trame se répète avant la fin de
     * l'image et le raccord DÉFILE VERS LE HAUT. C'est le sens qu'on a tous vu
     * sur un téléviseur mal réglé, et le prendre à l'envers donne quelque chose
     * qui ne rappelle rien.
     *
     * Trois passages par seconde et cinq pour cent de la hauteur : assez lent
     * pour qu'on la suive des yeux, assez fine pour qu'on continue de voir le
     * jeu derrière. Une barre large cacherait la partie, ce qui punirait deux
     * fois — la conséquence de jeu est ailleurs, elle est dans la main qui
     * quitte les boutons.
     */
    float y = h - SDL_fmodf(phase * 3.0f, 1.0f) * (h * 1.10f);
    const float bh = h * 0.05f;
    const float sombre[4] = { 0.0f, 0.0f, 0.0f, 0.55f * k };
    ns_sprite_rect(s, 0.0f, y, w, bh, sombre);
    /* Le liseré clair juste au-dessus : le bord d'une trame qui se recouvre est
     * plus lumineux, pas plus sombre. Sans lui la barre se lit comme une ombre
     * portée, ce qui n'est pas du tout le même défaut. */
    const float lisere[4] = { 0.85f, 0.90f, 1.0f, 0.30f * k };
    ns_sprite_rect(s, 0.0f, y - h * 0.006f, w, h * 0.006f, lisere);

    /*
     * 2. LES DÉCHIRURES.
     *
     * Quatre bandes fines, à des hauteurs qui ne bougent pas pendant le choc :
     * une déchirure de synchronisation reste sur sa ligne tant que le défaut
     * dure. Les faire sauter d'une image à l'autre donnerait de la NEIGE, qui
     * est un autre défaut — celui d'un signal absent, pas d'un signal secoué.
     *
     * Leurs positions sont écrites à la main et irrégulières, pour la même
     * raison que les glouglous de la chasse d'eau sont irréguliers : quatre
     * bandes régulièrement espacées se lisent comme une mire.
     */
    static const float bandes[4] = { 0.17f, 0.38f, 0.61f, 0.83f };
    for (int i = 0; i < 4; ++i) {
        const float a = 0.22f * k * ((i & 1) ? 0.7f : 1.0f);
        const float clair[4] = { 1.0f, 1.0f, 1.0f, a };
        ns_sprite_rect(s, 0.0f, h * bandes[i], w, h * 0.008f, clair);
    }

    /*
     * 3. LE BLANCHIMENT.
     *
     * Très bref — il suit `k * k`, donc il a disparu bien avant la barre. C'est
     * l'à-coup d'alimentation à l'instant du choc, et il ne dure que ça. Un
     * voile constant sur toute la durée délaverait l'image et se lirait comme
     * un défaut de rendu.
     */
    const float voile[4] = { 0.72f, 0.78f, 0.95f, 0.16f * k * k };
    ns_sprite_rect(s, 0.0f, 0.0f, w, h, voile);
}

/* ==========================================================================
 * LE COUPERET
 * ==========================================================================
 *
 * Ce que ces deux fonctions ont à résoudre, et qui n'existait pas ailleurs
 * dans ce fichier : le mode se joue LES YEUX SUR LA DALLE D'UNE BORNE. Tout
 * ce qui est écrit ici est lu en vision périphérique, par quelqu'un qui est en
 * train de faire autre chose.
 *
 * D'où trois règles de mise en page, et elles sont contraignantes :
 *
 *   1. LE MILIEU RESTE LIBRE. La dalle qu'on joue est au centre du cadre — le
 *      poste de jeu l'y met délibérément, à 31 % de l'aire (`room_poste.h`).
 *      Un panneau au centre couvrirait la partie.
 *   2. CE QUI PRESSE EST EN HAUT ET GRAND. Le compte à rebours et le nom du
 *      menacé sont les deux seules choses qui puissent faire changer d'avis en
 *      cours de partie ; tout le reste peut attendre la fin de la partie.
 *   3. LES SIX ACTIONS SONT TOUJOURS AFFICHÉES, jamais dans un menu qu'on
 *      ouvre. Un menu demande de quitter la borne des yeux, donc de perdre la
 *      partie qu'on est en train de protéger — ce qui serait exactement le
 *      contraire de ce que ces actions servent à faire.
 */

/* Une couleur par camp. Huit, franches et distinctes à faible luminance : le
 * mode se joue dans une salle en tungstène, et deux bleus voisins y deviennent
 * le même bleu. */
static const float C_CAMP[ROOM_CP_MAX_PLACES][4] = {
    { 1.00f, 0.78f, 0.28f, 1.0f },   /* ambre */
    { 0.36f, 0.78f, 1.00f, 1.0f },   /* cyan */
    { 0.52f, 0.92f, 0.42f, 1.0f },   /* vert */
    { 1.00f, 0.44f, 0.42f, 1.0f },   /* rouge */
    { 0.82f, 0.56f, 1.00f, 1.0f },   /* violet */
    { 1.00f, 0.62f, 0.20f, 1.0f },   /* orange */
    { 0.40f, 0.98f, 0.86f, 1.0f },   /* turquoise */
    { 0.95f, 0.95f, 0.95f, 1.0f },   /* blanc */
};

static const float *couleur_camp(uint8_t camp)
{
    return C_CAMP[camp % ROOM_CP_MAX_PLACES];
}

/* Le nom d'une place, jamais vide : une ligne de classement sans nom se lit
 * comme une ligne cassée. */
static const char *nom_place(const room_couperet *c, uint8_t i)
{
    if (!c || i >= ROOM_CP_MAX_PLACES) return "?";
    return c->place[i].pseudo[0] ? c->place[i].pseudo : "SANS NOM";
}

/*
 * Le nom TRONQUÉ à ce qui tient dans la colonne. Mesuré sur capture : la
 * colonne du tableau du bar fait 252 unités et la police avance de 6 par
 * caractère à l'échelle 3, soit quatorze caractères. « PIED-DE-BICHE » en fait
 * treize et passait de justesse ; un pseudo saisi par un joueur n'a aucune
 * raison de s'arrêter là, et il écrivait par-dessus la colonne d'à côté.
 *
 * On coupe plutôt qu'on ne rétrécit : rétrécir la police rendrait TOUTES les
 * lignes moins lisibles à cinq mètres pour un seul nom trop long.
 */
static void nom_court(char *out, size_t n, const room_couperet *c, uint8_t i)
{
    SDL_strlcpy(out, nom_place(c, i), n);
}

void room_hud_draw_couperet(ns_sprite *s, const room_couperet *c, uint8_t moi,
                            uint8_t cible, double time_seconds)
{
    if (!s || !c || c->phase != ROOM_CP_COURSE) return;

    static const float rouge[4] = { 1.00f, 0.32f, 0.26f, 1.0f };
    static const float vert[4]  = { 0.46f, 1.00f, 0.56f, 1.0f };
    static const float noir[4]  = { 0.02f, 0.02f, 0.03f, 0.80f };

    const uint8_t menace = room_cp_menace(c);
    const bool je_suis_menace = (menace < ROOM_CP_MAX_PLACES && menace == moi);

    /* ---- LA BANDE DU HAUT : le compte à rebours et le menacé -------------
     *
     * Le nombre de secondes ET le nom, jamais l'un sans l'autre. Un compte à
     * rebours qui ne dit pas qui est visé n'oblige personne à changer d'avis :
     * il presse tout le monde également, donc personne.
     */
    {
        const float bw = 600.0f, bx = (ROOM_HUD_W - bw) * 0.5f;
        panel(s, bx, 8.0f, bw, 70.0f);

        char t[16];
        SDL_snprintf(t, sizeof t, "%d", (int)ceilf(c->prochain));
        /* Sous dix secondes, le nombre bat. Le battement est en secondes
         * ENTIÈRES d'horloge de manche et non du temps d'image : il doit tomber
         * avec le chiffre qui change, sinon on lit deux rythmes. */
        const bool urgent = (c->prochain <= 10.0f);
        const float pulse = urgent
            ? 0.72f + 0.28f * (float)fabs(cos(time_seconds * 6.283185307))
            : 1.0f;
        float couleur[4];
        for (int k = 0; k < 4; ++k) {
            couleur[k] = (urgent ? rouge[k] : C_GOLD[k]) * ((k == 3) ? 1.0f : pulse);
        }
        couleur[3] = 1.0f;
        ns_sprite_text(s, bx + 20.0f, 22.0f, 5.2f, couleur, t);
        ns_sprite_text(s, bx + 20.0f + ns_sprite_text_width(t, 5.2f) + 8.0f, 40.0f,
                       2.0f, C_DIM, "S");

        /*
         * ÊTRE SORTI DOIT SE LIRE ICI, et pas seulement sur le téléviseur du
         * bar. Un joueur qu'on vient d'éliminer voit sa borne s'éteindre et ne
         * comprend pas pourquoi ; le tableau du bar le dit, mais il est à cinq
         * mètres et derrière lui. La bande du haut est le seul endroit qu'il
         * regarde déjà.
         *
         * Et elle dit ce qui RESTE plutôt que ce qui est perdu : sortir change
         * de métier, ça ne met pas à la porte — on garde ses fusibles, on en
         * reçoit un à chaque lame, et on continue d'agir. C'est la moitié de la
         * phrase qu'il faut lire à ce moment-là, parce que c'est celle qui
         * donne une raison de rester.
         */
        const bool spectre = (moi < ROOM_CP_MAX_PLACES) && c->place[moi].occupee
                          && !c->place[moi].vivante;
        if (spectre) {
            ns_sprite_text(s, bx + 140.0f, 22.0f, 2.6f, C_DIM,
                           "SORTI - IL VOUS RESTE VOS FUSIBLES");
        } else if (menace < ROOM_CP_MAX_PLACES) {
            char l[80];
            if (je_suis_menace) {
                SDL_snprintf(l, sizeof l, "LE COUPERET EST SUR TOI");
            } else {
                SDL_snprintf(l, sizeof l, "LE COUPERET VISE %s", nom_place(c, menace));
            }
            ns_sprite_text(s, bx + 140.0f, 22.0f, 2.6f,
                           je_suis_menace ? rouge : C_TEXT, l);
        }
        if (moi < ROOM_CP_MAX_PLACES) {
            char l[96];
            const room_cp_place *p = &c->place[moi];
            SDL_snprintf(l, sizeof l, "%d PTS   %d FUSIBLES%s",
                         p->points, p->fusibles, p->blindage ? "   [BLINDE]" : "");
            ns_sprite_text(s, bx + 140.0f, 50.0f, 2.2f, p->vivante ? C_TEXT : C_DIM, l);
        }
    }

    /* ---- CE QUE JE SUBIS, à gauche ---------------------------------------
     *
     * Un effet qu'on subit sans savoir qu'on le subit se lit comme une panne de
     * jeu. « Ton manche est inversé » transforme le même événement en coup
     * reçu, donc en quelque chose qui appelle une réponse.
     */
    if (moi < ROOM_CP_MAX_PLACES) {
        const room_cp_place *p = &c->place[moi];
        float y = 96.0f;
        struct { float reste; const char *quoi; } effets[] = {
            { p->brouillage, "BROUILLAGE" },
            { p->inversion,  "MANCHE INVERSE" },
        };
        for (unsigned i = 0; i < sizeof effets / sizeof effets[0]; ++i) {
            if (effets[i].reste <= 0.0f) continue;
            char l[48];
            SDL_snprintf(l, sizeof l, "%s  %.1f s", effets[i].quoi, (double)effets[i].reste);
            panel(s, 16.0f, y, 260.0f, 28.0f);
            ns_sprite_text(s, 26.0f, y + 7.0f, 2.2f, rouge, l);
            y += 34.0f;
        }
        if (p->leurre) {
            panel(s, 16.0f, y, 260.0f, 28.0f);
            ns_sprite_text(s, 26.0f, y + 7.0f, 2.2f, vert, "LEURRE ARME");
        }
    }

    /* ---- LA BANDE DU BAS : la cible, et les six actions -------------------
     *
     * Le PRIX est écrit sur chaque touche, et ce qu'on ne peut pas payer est
     * éteint. C'est ce qui remplace un didacticiel : au bout de deux manches on
     * sait ce que coûte une coupure sans que personne l'ait expliqué.
     */
    if (moi < ROOM_CP_MAX_PLACES && c->place[moi].occupee) {
        const room_cp_place *p = &c->place[moi];
        /*
         * AU-DESSUS DU BANDEAU D'AIDE, et pas au ras du bas. `room_hud_draw`
         * écrit les commandes de la salle sur la dernière ligne pendant les
         * premières secondes ; posée en bas, cette bande-ci se superposait
         * exactement à elle — deux textes ambrés l'un sur l'autre, illisibles
         * tous les deux. Mesuré sur capture, puis remonté de 46 points.
         */
        const float by = ROOM_HUD_H - 124.0f;
        panel(s, 16.0f, by, ROOM_HUD_W - 32.0f, 58.0f);

        char t[96];
        if (cible < ROOM_CP_MAX_PLACES && c->place[cible].occupee) {
            SDL_snprintf(t, sizeof t, "TAB  CIBLE : %s", nom_place(c, cible));
            ns_sprite_text(s, 28.0f, by + 10.0f, 2.4f, couleur_camp(c->place[cible].camp), t);
        } else {
            ns_sprite_text(s, 28.0f, by + 10.0f, 2.4f, C_DIM, "TAB  AUCUNE CIBLE");
        }

        /*
         * UNE SEULE LIGNE PAR ACTION : « 3 COUPURE -4 ». La touche, le nom, le
         * prix. Le prix sur une seconde ligne se lisait comme un second
         * numéro de touche — la première capture donnait « 1 BROUILLAGE » avec
         * un « 1 » dessous, et rien ne disait lequel des deux était la touche.
         * Le signe moins dit que ça se retire.
         */
        const float x0 = 28.0f, pas = (ROOM_HUD_W - 88.0f) / (float)ROOM_CP_ACTION_COUNT;
        for (int a = 0; a < ROOM_CP_ACTION_COUNT; ++a) {
            const room_cp_action act = (room_cp_action)a;
            const int32_t cout = room_cp_action_cout(act);
            const bool payable = (p->fusibles >= cout) && p->vivante;
            SDL_snprintf(t, sizeof t, "%d %s -%d", a + 1, room_cp_action_titre(act), cout);
            ns_sprite_text(s, x0 + (float)a * pas, by + 34.0f, 1.8f,
                           payable ? (room_cp_action_offensive(act) ? C_KEY : vert)
                                   : C_DIM, t);
        }
        (void)noir;
    }
}

/*
 * LE TÉLÉVISEUR DU BAR PENDANT UNE MANCHE.
 *
 * Il remplace les quatre volets du tableau ordinaire pour la durée de la
 * manche, et c'est le bon compromis : pendant qu'une manche court, « qui tient
 * le record de snake » n'intéresse plus personne, et le classement de la
 * manche intéresse tout le monde — y compris les spectres, pour qui c'est la
 * seule chose qui reste à regarder.
 *
 * Huit lignes sur un panneau 2:1 de 1,78 x 0,89 m : chaque ligne fait 4,4 cm de
 * haut à l'échelle réelle, soit un angle de 30 minutes d'arc à cinq mètres —
 * au-dessus du seuil de lisibilité de la police 5x7 établi sur ce dépôt (11 px
 * de hauteur de glyphe).
 */
void room_hud_draw_arene(ns_sprite *s, float w, float h, const room_couperet *c,
                         uint8_t moi, double time_seconds)
{
    if (!s || !c) return;

    const float u = w / 640.0f;
    static const float bg[4]    = { 0.003f, 0.004f, 0.008f, 1.0f };
    static const float bande[4] = { 0.290f, 0.070f, 0.070f, 1.0f };
    static const float rule[4]  = { 0.85f, 0.24f, 0.20f, 1.0f };
    static const float clair[4] = { 1.00f, 0.96f, 0.94f, 1.0f };
    static const float dim[4]   = { 0.45f, 0.42f, 0.46f, 1.0f };
    static const float rouge[4] = { 1.00f, 0.32f, 0.26f, 1.0f };

    ns_sprite_rect(s, 0.0f, 0.0f, w, h, bg);
    ns_sprite_rect(s, 0.0f, 0.0f, w, 58.0f * u, bande);
    ns_sprite_rect(s, 0.0f, 58.0f * u, w, 3.0f * u, rule);

    char t[96];
    if (c->phase == ROOM_CP_SALON) {
        int assis = 0;
        for (int i = 0; i < c->places; ++i) if (c->place[i].occupee) assis++;
        SDL_snprintf(t, sizeof t, "LE COUPERET   %d / %d PLACES", assis, (int)c->places);
    } else if (c->phase == ROOM_CP_FINI) {
        SDL_snprintf(t, sizeof t, "LE COUPERET   TERMINE");
    } else {
        SDL_snprintf(t, sizeof t, "LE COUPERET   LAME %d", c->couperets + 1);
    }
    ns_sprite_text(s, 22.0f * u, 15.0f * u, 3.6f * u, clair, t);

    if (c->phase == ROOM_CP_COURSE) {
        SDL_snprintf(t, sizeof t, "%d", (int)ceilf(c->prochain));
        a_droite(s, 618.0f * u, 12.0f * u, 4.2f * u,
                 (c->prochain <= 10.0f) ? rouge : clair, t);
    }

    const uint8_t menace = room_cp_menace(c);
    uint8_t ordre[ROOM_CP_MAX_PLACES];
    const int n = room_cp_classement(c, ordre);

    const float y0 = 76.0f, pas = 30.0f;
    for (int r = 0; r < n; ++r) {
        const uint8_t i = ordre[r];
        const room_cp_place *p = &c->place[i];
        const float y = (y0 + (float)r * pas) * u;
        const bool vise = (i == menace) && (c->phase == ROOM_CP_COURSE);

        /* La ligne du menacé est SOULIGNÉE D'UN APLAT, pas seulement écrite en
         * rouge : à cinq mètres une couleur de texte se perd dans le tungstène
         * de la salle, un aplat non. */
        if (vise) {
            ns_sprite_rect(s, 12.0f * u, y - 4.0f * u, 616.0f * u, 26.0f * u,
                           (const float[4]){ 0.32f, 0.06f, 0.05f, 1.0f });
        }
        /* Le fanion du camp. En individuel il y a huit camps d'une place, donc
         * huit couleurs : c'est ce qui permet de suivre quelqu'un du regard
         * dans la salle sans lire son nom. */
        ns_sprite_rect(s, 16.0f * u, y, 8.0f * u, 18.0f * u, couleur_camp(p->camp));

        SDL_snprintf(t, sizeof t, "%d", r + 1);
        ns_sprite_text(s, 32.0f * u, y, 3.0f * u, dim, t);
        char nom[15];
        nom_court(nom, sizeof nom, c, i);
        ns_sprite_text(s, 56.0f * u, y, 3.0f * u,
                       (i == moi) ? C_GOLD : (p->vivante ? clair : dim), nom);

        if (!p->vivante) {
            ns_sprite_text(s, 314.0f * u, y, 2.6f * u, dim, "SPECTRE");
        } else if (p->jeu[0]) {
            char maj[16];
            SDL_strlcpy(maj, p->jeu, sizeof maj);
            for (char *q = maj; *q; ++q) *q = (char)SDL_toupper((unsigned char)*q);
            /* Le « + » du régime difficile : un signe et pas le mot, parce que
             * la colonne fait treize caractères et que « DEMINEUR HARD » en
             * fait quatorze. */
            SDL_snprintf(t, sizeof t, "%s%s", maj, p->hard ? "+" : "");
            ns_sprite_text(s, 314.0f * u, y, 2.6f * u, dim, t);
        } else {
            ns_sprite_text(s, 314.0f * u, y, 2.6f * u, dim, "AU MONNAYEUR");
        }

        SDL_snprintf(t, sizeof t, "%d", p->fusibles);
        a_droite(s, 530.0f * u, y, 2.8f * u, C_KEY, t);
        SDL_snprintf(t, sizeof t, "%d", p->points);
        a_droite(s, 618.0f * u, y, 3.4f * u, p->vivante ? clair : dim, t);
    }

    /*
     * Les en-têtes de colonne EN BAS et non en haut : le bandeau du haut porte
     * déjà le titre et le compte à rebours, et empiler une ligne de service
     * avant la première place ferait descendre le classement hors de la moitié
     * haute — celle qu'on voit par-dessus les têtes au comptoir.
     *
     * À 304 et non à 296 : mesuré sur capture, la huitième ligne descend
     * jusqu'à 293 et les deux textes se chevauchaient sur les deux dernières
     * places, c'est-à-dire exactement là où l'on regarde quand on perd.
     */
    a_droite(s, 524.0f * u, 304.0f * u, 1.8f * u, dim, "FUSIBLES");
    a_droite(s, 616.0f * u, 304.0f * u, 1.8f * u, dim, "POINTS");
    (void)time_seconds;
}

void room_hud_draw_brouillage(ns_sprite *s, float w, float h, float force, float phase)
{
    if (!s || force <= 0.0f) return;
    if (force > 1.0f) force = 1.0f;

    /*
     * Ce qu'on dessine, et pourquoi ça ressemble à un brouillage plutôt qu'à
     * du bruit : un tube brouillé ne perd pas ses pixels au hasard, il perd des
     * LIGNES — des bandes horizontales qui se déplacent lentement, parce que
     * l'interférence bat contre la fréquence de trame. C'est ce battement lent
     * qu'on reconnaît, pas le grain.
     *
     * Onze bandes, tirées d'un générateur à état explicite plutôt que de
     * `rand()` : deux images voisines doivent porter le MÊME motif décalé, et
     * non deux motifs indépendants, sans quoi l'écran scintille au lieu de
     * défiler.
     */
    static const float voile[4] = { 0.42f, 0.46f, 0.52f, 1.0f };
    ns_sprite_rect(s, 0.0f, 0.0f, w, h, (const float[4]){
        voile[0], voile[1], voile[2], 0.30f * force });

    uint32_t etat = 0x9E3779B9u;
    for (int i = 0; i < 11; ++i) {
        etat = etat * 1664525u + 1013904223u;
        const float base = (float)(etat >> 8 & 0xFFFFu) / 65535.0f;
        etat = etat * 1664525u + 1013904223u;
        const float ep = 2.0f + (float)(etat >> 8 & 0xFFu) / 255.0f * 9.0f;
        /* Vitesses différentes par bande : à vitesse commune les onze bandes
         * forment un peigne rigide, qui se lit comme un défaut de rendu. */
        const float v = 0.05f + (float)i * 0.021f;
        float y = fmodf(base + (float)phase * v, 1.0f) * h;
        const float a = (0.10f + 0.22f * (float)((i * 37) % 5) / 4.0f) * force;
        ns_sprite_rect(s, 0.0f, y, w, ep, (const float[4]){ 0.02f, 0.03f, 0.05f, a });
        ns_sprite_rect(s, 0.0f, y + ep, w, 1.0f, (const float[4]){ 0.85f, 0.92f, 1.0f, a * 0.5f });
    }

    /* La bande large qui balaie : c'est elle qui rend le brouillage ILLISIBLE
     * par moments plutôt que seulement laid. Sans elle on s'habitue en dix
     * secondes, et l'action achetée ne coûte plus rien à sa cible. */
    const float large = h * 0.16f;
    const float yb = fmodf((float)phase * 0.31f, 1.0f) * (h + large) - large;
    ns_sprite_rect(s, 0.0f, yb, w, large,
                   (const float[4]){ 0.62f, 0.68f, 0.78f, 0.34f * force });
}

void room_hud_draw_verdict(ns_sprite *s, const room_couperet *c, uint8_t moi,
                           uint8_t rivaux, const room_cp_carnet *carnet,
                           float reste)
{
    if (!s || !c || c->phase != ROOM_CP_FINI || reste <= 0.0f) return;

    static const float or_[4]   = { 1.00f, 0.84f, 0.36f, 1.0f };
    static const float rouge[4] = { 1.00f, 0.42f, 0.34f, 1.0f };

    uint8_t ordre[ROOM_CP_MAX_PLACES];
    const int n = room_cp_classement_final(c, ordre);
    if (n <= 0) return;

    /* Le fondu de sortie, sur la dernière seconde seulement. Une page de
     * verdict qui s'effacerait progressivement pendant dix secondes serait
     * illisible pendant neuf. */
    const float a = (reste < 1.0f) ? reste : 1.0f;

    const float pw = 660.0f, ph = 62.0f + (float)n * 30.0f + 92.0f;
    const float px = (ROOM_HUD_W - pw) * 0.5f;
    const float py = (ROOM_HUD_H - ph) * 0.5f;
    ns_sprite_rect(s, px, py, pw, ph, (const float[4]){ 0.03f, 0.02f, 0.02f, 0.90f * a });
    ns_sprite_rect(s, px, py, pw, 3.0f, (const float[4]){ or_[0], or_[1], or_[2], a });
    ns_sprite_rect(s, px, py + ph - 3.0f, pw, 3.0f, (const float[4]){ or_[0], or_[1], or_[2], a });

    /*
     * LE TITRE DIT CE QUI M'EST ARRIVÉ, pas ce qui s'est passé. « VOUS ÊTES
     * TROISIÈME » vaut mieux que « MARQUISE GAGNE » pour les sept joueurs qui
     * n'ont pas gagné, et ils sont sept sur huit.
     */
    int mon_rang = 0;
    for (int r = 0; r < n; ++r) if (ordre[r] == moi) mon_rang = r + 1;

    char t[96];
    const bool gagne = (mon_rang == 1);
    if (moi >= ROOM_CP_MAX_PLACES || mon_rang == 0) {
        SDL_snprintf(t, sizeof t, "%s L'EMPORTE", nom_place(c, ordre[0]));
    } else if (gagne) {
        SDL_strlcpy(t, "DERNIER DEBOUT", sizeof t);
    } else {
        SDL_snprintf(t, sizeof t, "%d%s SUR %d", mon_rang,
                     (mon_rang == 1) ? "er" : "e", n);
    }
    float titre[4] = { gagne ? or_[0] : 1.00f, gagne ? or_[1] : 0.94f,
                       gagne ? or_[2] : 0.82f, a };
    centred(s, ROOM_HUD_W * 0.5f, py + 18.0f, 4.4f, titre, t);

    const float y0 = py + 62.0f;
    for (int r = 0; r < n; ++r) {
        const uint8_t i = ordre[r];
        const room_cp_place *p = &c->place[i];
        const float y = y0 + (float)r * 30.0f;
        const bool cest_moi = (i == moi);

        if (cest_moi) {
            ns_sprite_rect(s, px + 12.0f, y - 4.0f, pw - 24.0f, 26.0f,
                           (const float[4]){ 0.18f, 0.14f, 0.05f, a });
        }
        ns_sprite_rect(s, px + 24.0f, y, 8.0f, 18.0f,
                       (const float[4]){ couleur_camp(p->camp)[0], couleur_camp(p->camp)[1],
                                         couleur_camp(p->camp)[2], a });
        SDL_snprintf(t, sizeof t, "%d", r + 1);
        ns_sprite_text(s, px + 42.0f, y, 3.0f, (const float[4]){ C_DIM[0], C_DIM[1], C_DIM[2], a }, t);
        ns_sprite_text(s, px + 74.0f, y, 3.0f,
                       (const float[4]){ cest_moi ? or_[0] : C_TEXT[0],
                                         cest_moi ? or_[1] : C_TEXT[1],
                                         cest_moi ? or_[2] : C_TEXT[2], a },
                       nom_place(c, i));

        /*
         * TROIS COLONNES ET PAS UNE DE PLUS : les points, les parties finies,
         * les coupures encaissées. La troisième est celle qui manque partout
         * ailleurs et c'est la plus parlante — « j'ai perdu deux parties parce
         * qu'on m'a éteint la borne » explique un classement que les points
         * seuls rendraient incompréhensible.
         */
        /* La marque du rival : discrète, à droite du nom, et seulement ici. Le
         * raisonnement est au-dessus de la déclaration. */
        if (rivaux & (1u << i)) {
            ns_sprite_text(s, px + 74.0f + ns_sprite_text_width(nom_place(c, i), 3.0f) + 12.0f,
                           y + 3.0f, 1.8f,
                           (const float[4]){ C_DIM[0], C_DIM[1], C_DIM[2], a * 0.8f },
                           "MACHINE");
        }
        SDL_snprintf(t, sizeof t, "%d", p->points);
        a_droite(s, px + 436.0f, y, 3.0f, (const float[4]){ C_TEXT[0], C_TEXT[1], C_TEXT[2], a }, t);
        SDL_snprintf(t, sizeof t, "%d", p->parties);
        a_droite(s, px + 528.0f, y, 2.6f, (const float[4]){ C_DIM[0], C_DIM[1], C_DIM[2], a }, t);
        if (p->annulees > 0) {
            SDL_snprintf(t, sizeof t, "-%d", p->annulees);
            a_droite(s, px + 628.0f, y, 2.6f, (const float[4]){ rouge[0], rouge[1], rouge[2], a }, t);
        }
    }

    /*
     * Les libellés SOUS les colonnes, et raccourcis pour tenir dedans. Les
     * trois s'écrivaient « POINTS », « PARTIES », « COUPES » à l'échelle 1,9 :
     * quatre-vingts unités de large pour des colonnes espacées de quatre-vingts,
     * donc collés bout à bout sans un pixel entre eux — la première capture
     * rendait « POINTSPARTIES COUPES », qui ne se lit pas.
     */
    const float yf = y0 + (float)n * 30.0f + 8.0f;
    const float dimf[4] = { C_DIM[0], C_DIM[1], C_DIM[2], a };
    a_droite(s, px + 436.0f, yf, 1.8f, dimf, "PTS");
    a_droite(s, px + 528.0f, yf, 1.8f, dimf, "FINIES");
    a_droite(s, px + 628.0f, yf, 1.8f, dimf, "COUPEES");
    /*
     * LE CARNET, sur une ligne, sous le classement.
     *
     * C'est la seule chose qui survive à la manche, et la seule raison d'en
     * jouer une seconde qui ne demande rien au joueur : « 3 victoires sur 11 »
     * n'est pas une récompense, c'est une phrase sur ce qu'on a fait. Elle est
     * SOUS le classement et pas au-dessus — ce qui vient de se passer d'abord,
     * ce qu'on cumule ensuite.
     */
    if (carnet && carnet->manches > 0) {
        char l[96];
        if (carnet->serie >= 2) {
            SDL_snprintf(l, sizeof l, "%d VICTOIRE%s SUR %d   -   %d D'AFFILEE",
                         carnet->victoires, (carnet->victoires > 1) ? "S" : "",
                         carnet->manches, carnet->serie);
        } else {
            SDL_snprintf(l, sizeof l, "%d VICTOIRE%s SUR %d   -   MEILLEUR RANG %d",
                         carnet->victoires, (carnet->victoires > 1) ? "S" : "",
                         carnet->manches, carnet->meilleur_rang);
        }
        centred(s, ROOM_HUD_W * 0.5f, yf + 24.0f, 2.0f,
                (const float[4]){ C_DIM[0], C_DIM[1], C_DIM[2], a }, l);
    }
    centred(s, ROOM_HUD_W * 0.5f, yf + 48.0f, 2.4f,
            (const float[4]){ C_KEY[0], C_KEY[1], C_KEY[2], a }, "F9  UNE AUTRE MANCHE");
}
