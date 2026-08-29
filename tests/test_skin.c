/*
 * test_skin.c — le personnage articulé : ce qu'on charge, et ce qu'on pose.
 *
 * Ce que ce test attrape réellement, plutôt que ce qu'il a l'air de vérifier.
 *
 * Un maillage pesé rate de trois façons, et les trois sont MUETTES — aucune ne
 * produit d'erreur, toutes produisent une image fausse qu'on met une heure à
 * imputer au rendu :
 *
 *   1. Un INDICE D'OS hors bornes. Le shader lit une matrice au hasard dans son
 *      uniforme et le sommet part à l'autre bout de la salle, en tirant un
 *      triangle en travers de l'écran.
 *   2. Des POIDS qui ne somment pas à un. Le personnage rétrécit ou gonfle, et
 *      il le fait DIFFÉREMMENT selon la pose — donc on croit à un problème
 *      d'animation.
 *   3. Un NaN dans une matrice d'os, par un quaternion nul ou une division par
 *      une durée nulle. Le membre disparaît.
 *
 * Aucun de ces trois ne demande de GPU pour se voir, et c'est tout l'intérêt de
 * les chercher ici : ni fenêtre, ni pilote, ni capture à regarder.
 */
#include "ns_core.h"
#include "ns_skin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_failures;                                                     \
            printf("ÉCHEC %s:%d — ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

static bool finite_m4(const ns_m4 *m)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!isfinite(m->m[c][r])) return false;
    return true;
}

int main(int argc, char **argv)
{
    ns_log_set_level(NS_LOG_WARN);
    /* Le chemin des assets vient du build : ce test lit un vrai fichier, parce
     * que ce qu'il vérifie est justement la lecture. */
    ns_paths_init(argv[0]);
    if (argc > 1) ns_paths_mount(argv[1]);

    ns_skin *s = ns_skin_load("models/personnage/personnage.glb");
    CHECK(s != NULL, "le personnage se charge");
    if (!s) {
        printf("%d vérification(s), %d échec(s)\n", g_checks, g_failures);
        return 1;
    }

    const int joints = ns_skin_joint_count(s);
    CHECK(joints > 0 && joints <= NS_SKIN_MAX_JOINTS,
          "nombre d'os plausible (%d)", joints);
    CHECK(ns_skin_duration(s) > 0.1f, "l'animation a une durée (%.3f s)",
          (double)ns_skin_duration(s));
    CHECK(ns_skin_rest_height(s) > 0.01f, "le personnage a une hauteur au repos (%.3f)",
          (double)ns_skin_rest_height(s));

    uint32_t vcount = 0, icount = 0;
    const ns_skin_vertex *v = ns_skin_vertices(s, &vcount);
    const uint32_t *idx = ns_skin_indices(s, &icount);
    CHECK(v && vcount > 0, "des sommets (%u)", vcount);
    CHECK(idx && icount > 0 && icount % 3 == 0,
          "des indices, multiples de trois (%u)", icount);

    /* 1. Les indices d'os, et les indices de sommet. */
    int hors_bornes = 0, idx_hors = 0;
    for (uint32_t i = 0; i < vcount; ++i) {
        for (int k = 0; k < NS_SKIN_INFLUENCES; ++k) {
            if (v[i].joints[k] >= joints) ++hors_bornes;
        }
    }
    for (uint32_t i = 0; i < icount; ++i) if (idx[i] >= vcount) ++idx_hors;
    CHECK(hors_bornes == 0, "aucun indice d'os hors bornes (%d)", hors_bornes);
    CHECK(idx_hors == 0, "aucun indice de sommet hors bornes (%d)", idx_hors);

    /* 2. Les poids somment à un, et aucun sommet n'est orphelin. */
    float pire = 0.0f;
    int orphelins = 0;
    for (uint32_t i = 0; i < vcount; ++i) {
        float sum = 0.0f;
        for (int k = 0; k < NS_SKIN_INFLUENCES; ++k) sum += v[i].weights[k];
        const float ecart = fabsf(sum - 1.0f);
        if (ecart > pire) pire = ecart;
        if (sum < 1e-4f) ++orphelins;
    }
    CHECK(pire < 1e-3f, "les poids somment à un (pire écart %.6f)", (double)pire);
    CHECK(orphelins == 0, "aucun sommet non pesé (%d)", orphelins);

    /* Les positions et les normales sont finies : un NaN ici se propage à tout
     * ce que le sommet touche. */
    int nan_pos = 0;
    for (uint32_t i = 0; i < vcount; ++i) {
        for (int k = 0; k < 3; ++k) {
            if (!isfinite(v[i].position[k]) || !isfinite(v[i].normal[k])) ++nan_pos;
        }
    }
    CHECK(nan_pos == 0, "aucune position ni normale non finie (%d)", nan_pos);

    /* 3. Les matrices d'os, sur toute la durée ET au-delà : la pose doit être
     *    bouclée, donc un temps négatif ou très grand doit rester fini. */
    ns_m4 pose[NS_SKIN_MAX_JOINTS];
    const float duree = ns_skin_duration(s);
    static const float instants[] = { -3.7f, 0.0f, 0.013f, 0.5f, 1.0f, 1.999f, 97.3f };
    for (size_t t = 0; t < sizeof instants / sizeof instants[0]; ++t) {
        const float when = instants[t] * (duree > 0.0f ? duree : 1.0f);
        memset(pose, 0, sizeof pose);
        ns_skin_pose(s, when, pose, NS_SKIN_MAX_JOINTS);
        int mauvaises = 0;
        for (int j = 0; j < joints; ++j) if (!finite_m4(&pose[j])) ++mauvaises;
        CHECK(mauvaises == 0, "t = %.3f s : %d matrice(s) non finie(s)",
              (double)when, mauvaises);
    }

    /*
     * L'animation BOUGE, et elle boucle.
     *
     * Sans ce contrôle, un fichier dont les pistes n'auraient pas été lues
     * passerait tous les tests ci-dessus : des matrices identiques sont
     * parfaitement finies. Le personnage serait figé en T-pose, et rien ne le
     * dirait.
     */
    ns_m4 a[NS_SKIN_MAX_JOINTS], b[NS_SKIN_MAX_JOINTS], c[NS_SKIN_MAX_JOINTS];
    ns_skin_pose(s, 0.0f, a, NS_SKIN_MAX_JOINTS);
    ns_skin_pose(s, duree * 0.37f, b, NS_SKIN_MAX_JOINTS);
    ns_skin_pose(s, duree, c, NS_SKIN_MAX_JOINTS);

    float bouge = 0.0f, boucle = 0.0f;
    for (int j = 0; j < joints; ++j) {
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                const float d1 = fabsf(a[j].m[col][row] - b[j].m[col][row]);
                const float d2 = fabsf(a[j].m[col][row] - c[j].m[col][row]);
                if (d1 > bouge)  bouge = d1;
                if (d2 > boucle) boucle = d2;
            }
        }
    }
    CHECK(bouge > 1e-3f, "l'animation bouge entre 0 et 37 %% (écart %.6f)", (double)bouge);
    CHECK(boucle < 1e-3f, "l'animation boucle : t=0 et t=durée coïncident (écart %.6f)",
          (double)boucle);

    ns_skin_free(s);
    ns_paths_shutdown();

    printf("%d vérification(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
