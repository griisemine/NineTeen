/*
 * room_poste.c — l'établissement du poste de jeu.
 *
 * Tout ce qui explique POURQUOI est dans l'en-tête, y compris la table qui a
 * décidé des deux leviers. Ici : les cotes, les bornes de sécurité, et la
 * cinématique.
 */
#include "room_poste.h"

#include "ns_env.h"

#include <math.h>

/* ==========================================================================
 * Les réglages
 * ========================================================================== */
/*
 * Ils sont relus une fois au démarrage plutôt qu'à chaque prise de poste : ce
 * sont des cotes de mise en scène, pas un état, et les relire par partie
 * donnerait un jeu qui change de cadrage au milieu d'une soirée si quelqu'un
 * touche au fichier.
 */
static float g_fov      = 46.0f;   /* champ vertical au poste, degrés */
static float g_part_h   = 0.58f;   /* part de la hauteur du cadre prise par la dalle */
static float g_avance   = 0.55f;   /* secondes pour se pencher */
static float g_retour   = 0.38f;   /* secondes pour se redresser */
/*
 * Le RECUL MINIMAL, en mètres, compté depuis le centre de la dalle.
 *
 * Ce n'est pas un garde-fou de confort, c'est une cote du meuble. Le verre de la
 * borne est au fond d'une ouverture bordée par le bandeau des haut-parleurs
 * au-dessus et par le jonc du cadre sur les côtés ; l'œil qui passe ce plan voit
 * l'intérieur de la caisse par la tranche, et le plan de coupe proche taille
 * dans le bandeau. 0,40 m laisse la tête devant l'ouverture quel que soit le
 * tangage. Il ne mord PAS au réglage livré — le poste calculé tombe à 0,633 m —
 * et il existe pour le jour où quelqu'un montera `poste.partHauteur` à 0,95.
 */
static float g_recul_min = 0.40f;
/*
 * La hauteur d'œil minimale au-dessus du manche, en mètres.
 *
 * Se pencher fait descendre l'œil ; rien dans le calcul de distance ne l'empêche
 * de descendre SOUS les commandes, où la vue passerait derrière le panneau. Sur
 * les dix-neuf bornes le manche est à 1,072 m et le poste calculé met l'œil à
 * 1,467 m, soit 0,395 m au-dessus : la marge de 0,25 m ne mord pas non plus. Les
 * deux bornes sont là pour que le fichier de réglages ne puisse pas produire une
 * vue impossible, pas pour corriger le réglage livré.
 */
static float g_oeil_min = 0.25f;

void room_poste_read_env(void)
{
    g_fov       = ns_env_float("poste.champVertical",  g_fov);
    g_part_h    = ns_env_float("poste.partHauteur",    g_part_h);
    g_avance    = ns_env_float("poste.dureeAvance",    g_avance);
    g_retour    = ns_env_float("poste.dureeRetour",    g_retour);
    g_recul_min = ns_env_float("poste.reculMinimum",   g_recul_min);

    /* Bornes de bon sens. Un champ de 3° ou une part de 4 ne produisent pas un
     * cadrage discutable, ils produisent une distance absurde ou négative. */
    if (g_fov    < 12.0f) g_fov    = 12.0f;
    if (g_fov    > 90.0f) g_fov    = 90.0f;
    if (g_part_h < 0.10f) g_part_h = 0.10f;
    if (g_part_h > 0.98f) g_part_h = 0.98f;
    if (g_avance < 0.05f) g_avance = 0.05f;
    if (g_retour < 0.05f) g_retour = 0.05f;
    if (g_recul_min < 0.05f) g_recul_min = 0.05f;
}

void room_poste_init(room_poste *p)
{
    if (!p) return;
    const room_poste vide = { 0 };
    *p = vide;
}

/* ==========================================================================
 * La prise de poste
 * ========================================================================== */

bool room_poste_prendre(room_poste *p, const ns_cabinet *cab, ns_v3 oeil_corps)
{
    if (!p || !cab) return false;

    /*
     * Sans cotes de dalle ni normale, il n'y a pas de poste à calculer. C'est le
     * cas de la salle de 2020, qui ne déclare ni l'une ni l'autre : elle se joue
     * comme avant, et le dire ici évite un `NaN` qui se propagerait jusqu'à la
     * matrice de vue.
     */
    const float h = cab->screen_height;
    const float n2 = ns_v3_dot(cab->screen_normal, cab->screen_normal);
    if (!(h > 0.01f) || !(n2 > 1e-6f)) return false;

    const ns_v3 n = ns_v3_norm(cab->screen_normal);

    /* d = (h/2) / tan(part * champ / 2) — la règle de l'en-tête, telle quelle. */
    const float demi = 0.5f * g_part_h * g_fov * NS_DEG2RAD;
    const float t = tanf(demi);
    if (!(t > 1e-4f)) return false;
    float d = (0.5f * h) / t;
    if (d < g_recul_min) d = g_recul_min;

    ns_v3 oeil = ns_v3_add(cab->screen_center, ns_v3_scale(n, d));

    /*
     * Les deux bornes de sécurité, appliquées en RECULANT le long de la normale
     * et jamais en déplaçant l'œil de côté : le poste doit rester dans l'axe de
     * la dalle, sinon on la regarde de biais et le calcul de couverture ne veut
     * plus rien dire.
     */
    const float plancher = cab->stick_top.y + g_oeil_min;
    if (oeil.y < plancher && n.y > 1e-3f) {
        const float d2 = (plancher - cab->screen_center.y) / n.y;
        if (d2 > d) {
            d = d2;
            oeil = ns_v3_add(cab->screen_center, ns_v3_scale(n, d));
        }
    }

    /*
     * ON N'AVANCE JAMAIS EN RECULANT. Le poste est un rapprochement ; si l'œil
     * du corps est déjà plus près que lui, il n'y a rien à faire. Ça n'arrive pas
     * sur l'ancre déclarée (0,870 m contre 0,616 m calculés) mais `--pos=` place
     * la caméra où l'on veut, et une vue qui recule au démarrage d'une partie
     * serait un défaut plus visible que celui qu'on corrige.
     */
    const float d_corps = ns_v3_len(ns_v3_sub(oeil_corps, cab->screen_center));
    if (d_corps <= d) return false;

    p->oeil = oeil;
    p->fov  = g_fov;

    /*
     * LE TANGAGE DEPUIS LE POSTE, et non depuis le corps. Voir l'en-tête : sur
     * `borne_arcade_1` les deux diffèrent de onze degrés, et poser celui du corps
     * fait monter la dalle vers le haut du cadre pendant toute l'avancée.
     *
     * Il se lit directement sur la normale : l'œil est SUR cette normale, donc
     * regarder le centre de la dalle, c'est regarder à −n. Pas d'atan2 à écrire,
     * et pas de cas dégénéré quand la borne est vue de face.
     */
    p->tangage = -asinf(ns_clampf(n.y, -1.0f, 1.0f));

    p->pris = true;
    return true;
}

void room_poste_lacher(room_poste *p)
{
    if (p) p->pris = false;
}

void room_poste_tick(room_poste *p, float dt)
{
    if (!p) return;
    p->prev_part = p->part;
    if (dt <= 0.0f) return;

    /*
     * Deux vitesses, et c'est le détail qui fait que le mouvement se lit comme
     * un geste plutôt que comme une interpolation : on se penche vers une
     * machine plus lentement qu'on ne s'en redresse. La partie est finie, on se
     * relève — c'est un mouvement de rappel, pas une intention.
     */
    const float duree = p->pris ? g_avance : g_retour;
    const float pas = dt / duree;
    if (p->pris) {
        p->part += pas;
        if (p->part > 1.0f) p->part = 1.0f;
    } else {
        p->part -= pas;
        if (p->part < 0.0f) p->part = 0.0f;
    }
}

/* ==========================================================================
 * Ce que le rendu lit
 * ========================================================================== */
/*
 * LE LISSAGE. La part avance linéairement — c'est ce qui la rend simple à
 * borner et à tester — et c'est ici qu'elle est adoucie aux deux bouts. Une
 * rampe linéaire appliquée à une position donne un départ et un arrêt SECS :
 * la vitesse saute de zéro à sa valeur puis retombe, et l'œil le voit comme
 * deux à-coups. Le lissage cubique (3t² − 2t³) annule la dérivée aux deux
 * extrémités.
 *
 * Il est appliqué à la LECTURE et non à l'état pour que `room_poste_part` reste
 * monotone et vérifiable, et pour que l'interpolation de rendu porte sur la
 * grandeur simulée, pas sur son image.
 */
static float lisse(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

float room_poste_part(const room_poste *p, float alpha)
{
    if (!p) return 0.0f;
    const float a = ns_clampf(alpha, 0.0f, 1.0f);
    return lisse(ns_lerpf(p->prev_part, p->part, a));
}

ns_v3 room_poste_oeil(const room_poste *p, ns_v3 oeil_corps, float alpha)
{
    const float k = room_poste_part(p, alpha);
    if (k <= 0.0f) return oeil_corps;
    return ns_v3_lerp(oeil_corps, p->oeil, k);
}

float room_poste_fov(const room_poste *p, float fov_corps, float alpha)
{
    const float k = room_poste_part(p, alpha);
    if (k <= 0.0f) return fov_corps;
    return ns_lerpf(fov_corps, p->fov, k);
}

ns_v3 room_poste_oeil_pas(const room_poste *p, ns_v3 oeil_pas, bool courant)
{
    if (!p) return oeil_pas;
    const float k = lisse(courant ? p->part : p->prev_part);
    if (k <= 0.0f) return oeil_pas;
    return ns_v3_lerp(oeil_pas, p->oeil, k);
}

float room_poste_tangage(const room_poste *p)
{
    return p ? p->tangage : 0.0f;
}
