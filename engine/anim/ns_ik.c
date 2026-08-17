/* ns_ik.c — voir ns_ik.h pour le raisonnement. */
#include "ns_ik.h"

/* Marge sous laquelle on considère la chaîne tendue ou repliée à fond. Un
 * cosinus calculé exactement à la limite sort parfois à 1,0000001 selon
 * l'arrondi, et `acosf` y renvoie NaN. */
#define IK_EPS 1e-4f

ns_ik2 ns_ik_two_bone(ns_v3 root, ns_v3 target, ns_v3 pole,
                      float upper_length, float lower_length)
{
    ns_ik2 out;
    out.root = root;
    out.joint = root;
    out.end = root;
    out.reached = false;

    const float l1 = upper_length;
    const float l2 = lower_length;
    if (l1 <= IK_EPS || l2 <= IK_EPS) {
        /* Chaîne sans longueur : tout est à l'épaule. Fini, pas de NaN. */
        return out;
    }

    ns_v3 toTarget = ns_v3_sub(target, root);
    float dist = ns_v3_len(toTarget);

    /* Direction vers la cible. Cible confondue avec l'épaule : aucune direction
     * n'existe, on en choisit une plutôt que de diviser par zéro. */
    ns_v3 dir;
    if (dist < IK_EPS) {
        dir = ns_v3_make(0.0f, 0.0f, 1.0f);
        dist = IK_EPS;
    } else {
        dir = ns_v3_scale(toTarget, 1.0f / dist);
    }

    /* Domaine atteignable : entre |l1 − l2| (bras replié au maximum) et l1 + l2
     * (bras tendu). Hors de là, on se rabat sur la borne la plus proche — le
     * membre se tend ou se replie au lieu de disparaître. */
    const float reach_max = l1 + l2;
    const float reach_min = (l1 > l2) ? (l1 - l2) : (l2 - l1);
    float clamped = dist;
    bool reached = true;
    if (dist > reach_max - IK_EPS) { clamped = reach_max - IK_EPS; reached = false; }
    if (clamped < reach_min + IK_EPS) { clamped = reach_min + IK_EPS; reached = false; }

    /*
     * Loi des cosinus, sur le triangle épaule-coude-poignet :
     *     l2² = l1² + d² − 2·l1·d·cos(a)
     * où `a` est l'angle entre l'axe épaule→poignet et le segment épaule→coude.
     */
    float cos_a = (l1 * l1 + clamped * clamped - l2 * l2) / (2.0f * l1 * clamped);
    cos_a = ns_clampf(cos_a, -1.0f, 1.0f);
    const float sin_a = sqrtf(ns_maxf(0.0f, 1.0f - cos_a * cos_a));

    /*
     * Le coude vit sur un cercle autour de l'axe épaule→poignet. Le pôle choisit
     * lequel : on prend sa composante ORTHOGONALE à l'axe, ce qui donne la
     * direction dans laquelle pousser le coude.
     */
    ns_v3 toPole = ns_v3_sub(pole, root);
    ns_v3 perp = ns_v3_sub(toPole, ns_v3_scale(dir, ns_v3_dot(toPole, dir)));
    if (ns_v3_len_sq(perp) < 1e-8f) {
        /* Pôle aligné avec la chaîne : il ne désigne plus rien. N'importe quel
         * axe perpendiculaire convient, et n'en choisir aucun donnerait un coude
         * à l'infini. */
        ns_v3 axis = ns_v3_cross(dir, ns_v3_make(0.0f, 1.0f, 0.0f));
        if (ns_v3_len_sq(axis) < 1e-8f) axis = ns_v3_cross(dir, ns_v3_make(1.0f, 0.0f, 0.0f));
        perp = ns_v3_cross(axis, dir);
        if (ns_v3_len_sq(perp) < 1e-8f) perp = ns_v3_make(0.0f, 1.0f, 0.0f);
    }
    perp = ns_v3_norm(perp);

    out.joint = ns_v3_add(root, ns_v3_add(ns_v3_scale(dir, l1 * cos_a),
                                          ns_v3_scale(perp, l1 * sin_a)));
    /* Le poignet est posé sur le point atteignable, pas sur la cible : si elle
     * était hors de portée, prétendre l'avoir atteinte allongerait l'avant-bras. */
    out.end = ns_v3_add(root, ns_v3_scale(dir, clamped));
    out.reached = reached;
    return out;
}
