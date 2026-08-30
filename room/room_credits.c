/* room_credits.c — voir room_credits.h pour le raisonnement. */
#include "room_credits.h"

#include "room_hud.h"

#include <stddef.h>

/* Les mêmes teintes ambrées que le reste de l'affichage : un panneau blanc sur
 * une salle au tungstène se lit comme une capture collée par-dessus. */
static const float C_TITLE[4] = { 1.00f, 0.72f, 0.32f, 1.00f };
static const float C_HEAD[4]  = { 1.00f, 0.82f, 0.35f, 1.00f };
static const float C_WHAT[4]  = { 0.90f, 0.86f, 0.78f, 1.00f };
static const float C_WHO[4]   = { 0.68f, 0.62f, 0.54f, 1.00f };
static const float C_FOOT[4]  = { 0.50f, 0.45f, 0.40f, 1.00f };

/* ========================================================================== */
/* Les crédits                                                                */
/* ========================================================================== */
/*
 * L'ORDRE de cette table n'est pas décoratif.
 *
 * Le personnage vient EN PREMIER parce que c'est la seule ligne qui soit une
 * obligation : tout le reste est du CC0 ou du domaine public, crédité parce que
 * c'est correct. Mettre la mention obligatoire au milieu d'une liste de
 * courtoisies, c'est la rendre aussi facile à supprimer que les autres.
 *
 * Chaque entrée dit l'AUTEUR et la LICENCE. Une attribution qui nomme l'œuvre
 * sans nommer l'auteur n'attribue rien, et CC BY 4.0 demande nommément le
 * titre, l'auteur, la licence et un lien vers elle : les quatre sont là.
 *
 * ET UNE CINQUIÈME MENTION, qui manquait. CC BY 4.0 § 3.a.1.B demande
 * d'indiquer si l'œuvre a été MODIFIÉE. Elle l'a été : la texture d'origine —
 * le logotype de Cesium, rubans bleus et verts — a été remplacée par une peau
 * peinte dans son espace UV (`tools/skinart`). Le maillage, le squelette et
 * l'animation sont intacts ; la peau ne l'est pas, et c'est une modification au
 * sens de la licence. Ne pas le dire aurait laissé croire que Cesium a dessiné
 * ce blouson rouge.
 */
static const room_credit_line g_credits[] = {
    { "LE PERSONNAGE", NULL, true },
    { "CESIUMMAN",            "(C) 2017 CESIUM", false },
    { "LICENCE",              "CC BY 4.0 INTERNATIONAL", false },
    { "MODIFIE",              "TEXTURE REPEINTE - MAILLAGE ET ANIMATION INTACTS", false },
    { "",                     "CREATIVECOMMONS.ORG/LICENSES/BY/4.0/", false },

    { "DECOR ET MOBILIER", NULL, true },
    { "12 MODELES, 6 TEXTURES", "POLY HAVEN - CC0 1.0 (DOMAINE PUBLIC)", false },
    { "",                       "POLYHAVEN.COM", false },

    { "LOGICIEL", NULL, true },
    { "SDL3",       "SAM LANTINGA ET CONTRIBUTEURS - LICENCE ZLIB", false },
    { "CGLTF",      "JOHANNES KUHLMANN - MIT", false },
    { "JSMN",       "SERGE ZAITSEV - MIT", false },
    { "STB",        "SEAN BARRETT - MIT OU DOMAINE PUBLIC", false },
    { "MINIAUDIO",  "DAVID REID - MIT-0 OU DOMAINE PUBLIC", false },

    { "LA SALLE", NULL, true },
    { "SALLE, BORNES, JEUX",  "NINETEEN - PROJET DE 2020, RECONSTRUIT", false },
    { "TEXTURES ET AFFICHES", "VOIR LICENSES.MD, LIVRE AVEC LE JEU", false },

    { NULL, NULL, false }
};

const room_credit_line *room_credits_lines(int *count)
{
    if (count) {
        int n = 0;
        while (g_credits[n].what) ++n;
        *count = n;
    }
    return g_credits;
}

/* ========================================================================== */
/* Les commandes                                                              */
/* ========================================================================== */
/*
 * Ce qui est écrit ici doit être ce que `main.c` fait, et rien d'autre.
 *
 * La règle a d'abord servi à REFUSER une ligne « MANETTE » : il n'y en avait
 * pas, et une page d'aide qui annonce une commande inexistante est pire que pas
 * de page du tout — le joueur croit sa manette cassée. Elle sert maintenant à
 * l'exiger, pour la raison symétrique : la manette MARCHE depuis
 * `room/room_pad.c`, et un acheteur qui en branche une n'avait aucun moyen, DANS
 * le jeu, de l'apprendre. Une commande qui existe et que rien n'annonce est une
 * commande que personne n'emploie.
 *
 * Les libellés de manette suivent SDL3, qui nomme les boutons par leur POSITION
 * et non par leur lettre — une manette Nintendo porte le A là où une Xbox porte
 * le B. On dit donc « BOUTON DU BAS » et on donne la lettre Xbox entre
 * parenthèses, parce que c'est celle que la plupart des gens ont sous les yeux.
 *
 * Les touches de déplacement sont lues par POSITION physique, d'où « ZQSD /
 * WASD » sur la même ligne : ce n'est pas deux liaisons, c'est le même bloc de
 * touches sous deux noms de disposition.
 */
static const room_credit_line g_controls[] = {
    { "SE DEPLACER", NULL, true },
    { "ZQSD / WASD / FLECHES", "AVANCER, RECULER, PAS DE COTE", false },
    { "SOURIS",                "REGARDER", false },
    { "MAJ GAUCHE",            "COURIR", false },
    { "ESPACE",                "SAUTER", false },
    { "CTRL GAUCHE OU C",      "S'ACCROUPIR", false },

    { "A LA MANETTE", NULL, true },
    { "STICK GAUCHE / CROIX", "SE DEPLACER, ET LE MANCHE EN PARTIE", false },
    { "STICK DROIT",          "REGARDER", false },
    { "BOUTON DU BAS (A)",    "AGIR : LE JETON, LA PORTE, LE BOUTON DE BORNE", false },
    { "BOUTON GAUCHE (X)",    "SAUTER", false },
    { "BOUTON DROIT (B)",     "S'ACCROUPIR", false },
    { "GACHETTE HAUTE G. (LB)", "COURIR", false },
    { "START",                "LES REGLAGES", false },

    { "JOUER", NULL, true },
    { "E",       "DEVANT UNE BORNE : INSERER UN JETON ET JOUER", false },
    { "FLECHES", "EN PARTIE : LE MANCHE DE LA BORNE", false },
    { "ESPACE",  "EN PARTIE : LE BOUTON - ET REJOUER APRES LA MORT", false },
    { "F",       "COGNER LA BORNE - LA MAIN QUITTE LES BOUTONS", false },
    { "ECHAP",   "EN PARTIE : SORTIR DE LA PARTIE, PAS DU JEU", false },

    { "LE RESTE", NULL, true },
    { "ECHAP",    "LES REGLAGES, LES COMMANDES, LES CREDITS, QUITTER", false },
    { "F2",       "CAPTURE D'ECRAN DANS LE REPERTOIRE UTILISATEUR", false },
    { "F5 / F6",  "CAMERA LIBRE / CAMERA ORBITE", false },
    { "F7 / F8",  "PALIER DE QUALITE / ECHELLE DE RENDU", false },
    { "F10",      "PREMIERE OU TROISIEME PERSONNE", false },

    { NULL, NULL, false }
};

const room_credit_line *room_controls_lines(int *count)
{
    if (count) {
        int n = 0;
        while (g_controls[n].what) ++n;
        *count = n;
    }
    return g_controls;
}

/* ========================================================================== */
/* Le dessin                                                                  */
/* ========================================================================== */
/*
 * Une seule fonction pour les deux pages : elles ont la même forme, et deux
 * copies du même calcul de mise en page finiraient par diverger sur un pixel
 * que personne ne saurait expliquer.
 *
 * La hauteur du cadre est DÉDUITE du nombre de lignes, comme dans `room_menu` —
 * et pour la raison que ce fichier-là a apprise à ses dépens : un cadre de
 * taille fixe est un piège qui se referme sur la personne suivante qui ajoute
 * une ligne. Ajouter une bibliothèque tierce aux crédits ne doit pas demander
 * de se souvenir qu'il faut aussi rallonger un rectangle.
 */
static void draw_page(ns_sprite *s, const char *title, const char *footer,
                      const room_credit_line *lines, int count)
{
    const float W = ROOM_HUD_W, H = ROOM_HUD_H;

    /* Un voile plus opaque que celui du menu : on ne règle rien ici, il n'y a
     * donc rien à regarder derrière — et un texte long se lit d'autant mieux
     * que le fond est calme. */
    const float veil[4] = { 0.02f, 0.015f, 0.012f, 0.88f };
    ns_sprite_rect(s, 0, 0, W, H, veil);

    const float head_h = 74.0f;
    const float foot_h = 52.0f;
    const float panel_w = 1080.0f;

    /*
     * Le pas des lignes cède avant le cadre — la même règle que `room_menu`, et
     * pour la raison que ce fichier-là a payée : raboter la hauteur du cadre
     * sans resserrer les lignes ne fait pas tenir la page, il fait écrire la
     * dernière ligne par-dessus le pied. Ici c'est d'autant plus important que
     * la table est faite pour GRANDIR : chaque bibliothèque tierce ajoutée un
     * jour y prendra une ligne, et personne ne doit avoir à s'en souvenir.
     */
    const float avail = H - 20.0f - head_h - foot_h;
    float row_h = 26.0f;
    if ((float)count * row_h > avail) row_h = avail / (float)count;
    const float panel_h = head_h + (float)count * row_h + foot_h;
    const float px = (W - panel_w) * 0.5f, py = (H - panel_h) * 0.5f;

    const float border[4] = { 0.78f, 0.42f, 0.14f, 0.95f };
    const float back[4]   = { 0.055f, 0.040f, 0.030f, 0.96f };
    ns_sprite_rect(s, px - 3, py - 3, panel_w + 6, panel_h + 6, border);
    ns_sprite_rect(s, px, py, panel_w, panel_h, back);

    const float title_scale = 3.0f;
    ns_sprite_text(s, px + (panel_w - ns_sprite_text_width(title, title_scale)) * 0.5f,
                   py + 22.0f, title_scale, C_TITLE, title);

    const float top = py + head_h;
    const float lx  = px + 40.0f;
    /*
     * La colonne de droite commence au TIERS du cadre, pas après le plus long
     * libellé de gauche.
     *
     * Mesurer le plus long donnerait une colonne qui se déplace chaque fois
     * qu'on ajoute une ligne — donc une page dont la mise en page dépend de son
     * contenu, et qui ne se relit pas d'une version à l'autre. Un tiers fixe
     * tient « ZQSD / WASD / FLECHES », qui est le plus long des deux tables,
     * avec de la marge.
     */
    const float rx  = px + panel_w * 0.34f;

    for (int i = 0; i < count; ++i) {
        const float y = top + (float)i * row_h;
        if (lines[i].heading) {
            /* Un intertitre respire au-dessus, pas en dessous : c'est ce qui le
             * rattache visuellement à ce qu'il annonce. */
            ns_sprite_text(s, lx - 14.0f, y + 4.0f, 1.8f, C_HEAD, lines[i].what);
            continue;
        }
        if (lines[i].what && lines[i].what[0]) {
            ns_sprite_text(s, lx, y + 4.0f, 1.6f, C_WHAT, lines[i].what);
        }
        if (lines[i].who) {
            ns_sprite_text(s, rx, y + 4.0f, 1.6f, C_WHO, lines[i].who);
        }
    }

    ns_sprite_text(s, px + (panel_w - ns_sprite_text_width(footer, 1.5f)) * 0.5f,
                   py + panel_h - 30.0f, 1.5f, C_FOOT, footer);
}

void room_credits_draw(ns_sprite *s)
{
    if (!s) return;
    int n = 0;
    const room_credit_line *l = room_credits_lines(&n);
    draw_page(s, "CREDITS", "ECHAP OU ENTREE : RETOUR AUX REGLAGES", l, n);
}

void room_controls_draw(ns_sprite *s)
{
    if (!s) return;
    int n = 0;
    const room_credit_line *l = room_controls_lines(&n);
    draw_page(s, "COMMANDES", "ECHAP OU ENTREE : RETOUR AUX REGLAGES", l, n);
}

/* -------------------------------------------------------------------------- */

/*
 * L'aide d'arrivée.
 *
 * Quatre commandes, pas quinze. Ce bandeau n'est pas une table des matières :
 * il répond à « qu'est-ce que je fais » et se tait. Celui qui veut la liste
 * complète y est envoyé par la dernière colonne, « ECHAP : REGLAGES ET AIDE » —
 * c'est la seule touche qu'il ait besoin de retenir.
 *
 * En BAS de l'écran, et sur une seule ligne. Au centre il masquerait la salle,
 * qui est précisément ce qu'on veut faire regarder ; sur plusieurs lignes il
 * deviendrait un panneau, donc quelque chose à lire avant de jouer.
 */
void room_credits_draw_intro(ns_sprite *s, float alpha)
{
    if (!s || alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;

    static const char *const key[] = { "ZQSD/WASD", "SOURIS", "E", "ECHAP" };
    static const char *const act[] = { "SE DEPLACER", "REGARDER",
                                       "JOUER SUR UNE BORNE", "REGLAGES ET AIDE" };
    enum { N = 4 };

    const float ks = 1.8f, as = 1.6f;
    const float gap = 10.0f, sep = 34.0f;

    /* La largeur se mesure AVANT de dessiner : le bandeau est centré, et un
     * bandeau centré dont on découvre la largeur en cours de route ne l'est
     * pas. */
    float total = 0.0f;
    for (int i = 0; i < N; ++i) {
        total += ns_sprite_text_width(key[i], ks) + gap
               + ns_sprite_text_width(act[i], as);
        if (i + 1 < N) total += sep;
    }

    const float h = ns_sprite_text_height(ks) + 20.0f;
    const float y = ROOM_HUD_H - h - 26.0f;
    const float x = (ROOM_HUD_W - total) * 0.5f;

    const float bg[4]   = { 0.05f, 0.04f, 0.03f, 0.72f * alpha };
    const float edge[4] = { 1.00f, 0.72f, 0.34f, 0.34f * alpha };
    ns_sprite_rect(s, x - 26.0f, y, total + 52.0f, h, bg);
    ns_sprite_rect(s, x - 26.0f, y, total + 52.0f, 2.0f, edge);
    ns_sprite_rect(s, x - 26.0f, y + h - 2.0f, total + 52.0f, 2.0f, edge);

    const float ck[4] = { 1.00f, 0.78f, 0.28f, alpha };
    const float ca[4] = { 0.90f, 0.86f, 0.78f, alpha };

    float cx = x;
    for (int i = 0; i < N; ++i) {
        cx += ns_sprite_text(s, cx, y + 10.0f, ks, ck, key[i]) + gap;
        cx += ns_sprite_text(s, cx, y + 11.0f, as, ca, act[i]);
        cx += sep;
    }
}
