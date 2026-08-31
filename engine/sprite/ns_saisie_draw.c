/*
 * ns_saisie_draw.c — cadre, texte, curseur.
 */
#include "ns_saisie_draw.h"

#include <math.h>

/*
 * La palette est celle du menu : ambre sur brun sombre. Elle est recopiée plutôt
 * que partagée avec `room_menu.c` pour une raison de sens et non de commodité —
 * un module du moteur qui irait chercher ses couleurs dans `room/` inverserait
 * la dépendance que `engine/` tient depuis le début.
 */
static const float g_bord_actif[4]  = { 1.00f, 0.72f, 0.32f, 1.00f };
static const float g_bord_dormant[4]= { 0.45f, 0.32f, 0.20f, 1.00f };
static const float g_fond[4]        = { 0.045f, 0.035f, 0.028f, 0.95f };
static const float g_texte[4]       = { 0.94f, 0.90f, 0.82f, 1.00f };
static const float g_invite[4]      = { 0.44f, 0.39f, 0.34f, 1.00f };

float ns_saisie_echelle_lisible(float demandee)
{
    /*
     * Le seuil se convertit en échelle par la fonte elle-même plutôt que par une
     * division écrite ici : `ns_sprite_text_height` EST la hauteur d'un glyphe,
     * et le jour où la fonte grandit, cette fonction suit sans qu'on y touche.
     */
    const float haute = ns_sprite_text_height(1.0f);
    const float mini  = (haute > 0.0f) ? NS_SAISIE_SEUIL_PX / haute : 1.0f;
    return demandee < mini ? mini : demandee;
}

void ns_saisie_dessiner(ns_sprite *s, const ns_saisie *champ, const ns_saisie_cadre *c)
{
    if (!s || !champ || !c) return;

    const float e = ns_saisie_echelle_lisible(c->echelle);

    /* L'avance d'un caractère se DEMANDE à la couche 2D : elle vaut cinq
     * colonnes plus une d'espacement, et ce « plus une » n'a pas à être connu
     * ici. Un seul caractère suffit à la mesurer, la fonte étant à chasse fixe. */
    const float avance  = ns_sprite_text_width("M", e);
    const float hauteur = ns_sprite_text_height(e);
    if (avance <= 0.0f) return;

    /* Une demi-chasse de marge : assez pour que le texte ne touche pas le cadre,
     * et proportionnel à l'échelle, donc juste à toutes les tailles. */
    const float marge = avance * 0.5f;

    const float *bord = c->actif ? g_bord_actif : g_bord_dormant;
    ns_sprite_rect(s, c->x - 2.0f, c->y - 2.0f, c->w + 4.0f, c->h + 4.0f, bord);
    ns_sprite_rect(s, c->x, c->y, c->w, c->h, g_fond);

    const float tx = c->x + marge;
    const float ty = c->y + (c->h - hauteur) * 0.5f;

    /* Combien de caractères tiennent. Au moins un : un cadre trop étroit doit
     * montrer quelque chose de faux et visible, pas une division par zéro. */
    const float dedans = c->w - 2.0f * marge;
    size_t colonnes = (dedans >= avance) ? (size_t)(dedans / avance) : 1u;

    char vue[NS_SAISIE_OCTETS + 1];
    const size_t n  = ns_saisie_affichage(champ, vue, sizeof vue);
    const size_t ic = ns_saisie_curseur_caractere(champ);

    /*
     * LE DÉFILEMENT, et ce qu'il choisit de ne pas montrer.
     *
     * La règle tient en une ligne et ne garde AUCUN état : le curseur reste dans
     * la dernière colonne dès que le texte déborde. C'est exactement ce qu'on
     * veut en tapant — on lit ce qu'on vient d'écrire — et c'est encore ce qu'on
     * veut en reculant pour corriger, puisque le caractère qu'on s'apprête à
     * effacer est celui de gauche.
     *
     * Ce que ça ne montre pas, et il faut le dire : la fin du texte quand le
     * curseur est revenu au milieu d'un contenu qui déborde. La garder
     * demanderait au champ de se souvenir d'où il a été dessiné la fois d'avant,
     * c'est-à-dire de posséder un état d'affichage — et un état d'affichage dans
     * un module que les tests construisent à la main est un état qui finit par
     * mentir. Aucun des trois champs prévus (pseudo, mot de passe, code) ne
     * déborde d'un cadre correctement dimensionné.
     */
    const size_t derniere = colonnes - 1u;
    const size_t depart = (ic > derniere) ? ic - derniere : 0u;

    if (n == 0 && c->invite) {
        ns_sprite_text(s, tx, ty, e, g_invite, c->invite);
    } else if (depart < n) {
        /* `vue` est une copie locale : on la coupe sans façon. */
        size_t fin = depart + colonnes;
        if (fin > n) fin = n;
        vue[fin] = '\0';
        ns_sprite_text(s, tx, ty, e, g_texte, vue + depart);
    }

    /*
     * LE CURSEUR, sans lequel un champ vide et actif est indiscernable d'un
     * champ vide et inerte. La cadence est celle de l'en-tête, et lui seul.
     *
     * Il est POSÉ, pas ajouté au texte. Un curseur écrit comme un caractère de
     * plus — ce que fait le comptoir aujourd'hui en accolant un « _ » — ne peut
     * se tenir qu'au bout de la ligne et fait sauter le texte d'une colonne
     * chaque fois qu'il s'éteint. Ici il se pose entre deux lettres, ce qui est
     * la seule façon de montrer où l'on corrige.
     *
     * Large d'un pixel DE LA FONTE, pas d'un pixel d'écran : à l'échelle 3 le
     * trait grossit avec les lettres au lieu de devenir un cheveu.
     */
    if (c->actif) {
        const double cycle = (double)NS_SAISIE_CYCLE_MS / 1000.0;
        const double phase = c->temps - floor(c->temps / cycle) * cycle;
        if (phase * 1000.0 < (double)NS_SAISIE_ALLUME_MS) {
            const float cx = tx + (float)(ic - depart) * avance;
            ns_sprite_rect(s, cx, ty, e, hauteur, g_bord_actif);
        }
    }
}
