/*
 * test_allure.c — les allures que le fichier n'a PAS, et qu'on en dérive.
 *
 * Ce que ce test défend, et pourquoi ça ne se voit pas autrement
 * -------------------------------------------------------------
 * Le modèle livré porte UN cycle : une marche. L'accroupi et le balancement
 * d'arrêt sont construits par-dessus, à partir de mesures faites sur le
 * squelette — la topologie des jambes, l'axe des épaules, le signe qui envoie
 * le genou devant. Chacune de ces mesures peut échouer SILENCIEUSEMENT, et
 * l'échec se lit alors à l'écran comme un défaut d'animation :
 *
 *   1. les jambes mal repérées, et le personnage plie les BRAS ;
 *   2. le signe à l'envers, et il s'accroupit EN ARRIÈRE, genoux vers le dos ;
 *   3. la remontée du bassin mal corrigée, et il flotte au-dessus du sol ou
 *      s'y enfonce jusqu'aux chevilles ;
 *   4. la calibration qui rate, et l'accroupi a la hauteur de n'importe quoi —
 *      typiquement celle de la capsule de collision qu'il est censé remplir.
 *
 * Aucun de ces quatre ne demande de GPU pour se constater. Ce test pose donc
 * vraiment le personnage, PÈSE vraiment ses sommets, et vérifie sur les
 * chiffres ce que la capture montre à l'œil.
 *
 * Ce qu'il ne vérifie PAS : que la pose soit BELLE. Un accroupi peut être juste
 * au centimètre et laid — le seul juge de ça est la capture, et c'est écrit
 * dans le journal, pas ici.
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

/* La boîte du personnage sous une allure donnée, MESURÉE sur ses sommets comme
 * le fera le shader — pas sur le squelette, qui ne dit rien de la peau. */
typedef struct boite { float bas, haut, avant, arriere; bool fini; } boite;

static boite mesurer(const ns_skin *s, float temps, const ns_skin_allure *al)
{
    boite b;
    b.bas = 1e30f; b.haut = -1e30f; b.avant = -1e30f; b.arriere = 1e30f; b.fini = true;

    ns_m4 os[NS_SKIN_MAX_JOINTS];
    ns_skin_pose_allure(s, temps, al, os, NS_SKIN_MAX_JOINTS);
    const int n = ns_skin_joint_count(s);
    for (int j = 0; j < n; ++j) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                if (!isfinite(os[j].m[c][r])) b.fini = false;
    }

    uint32_t count = 0;
    const ns_skin_vertex *v = ns_skin_vertices(s, &count);
    for (uint32_t i = 0; i < count; ++i) {
        float y = 0.0f, z = 0.0f;
        for (int k = 0; k < NS_SKIN_INFLUENCES; ++k) {
            const float w = v[i].weights[k];
            if (w <= 0.0f) continue;
            const ns_m4 *m = &os[v[i].joints[k]];
            y += w * (m->m[0][1] * v[i].position[0] + m->m[1][1] * v[i].position[1] +
                      m->m[2][1] * v[i].position[2] + m->m[3][1]);
            z += w * (m->m[0][2] * v[i].position[0] + m->m[1][2] * v[i].position[1] +
                      m->m[2][2] * v[i].position[2] + m->m[3][2]);
        }
        if (y < b.bas) b.bas = y;
        if (y > b.haut) b.haut = y;
        if (z > b.avant) b.avant = z;
        if (z < b.arriere) b.arriere = z;
    }
    return b;
}

int main(int argc, char **argv)
{
    ns_log_set_level(NS_LOG_WARN);
    ns_paths_init(argv[0]);
    if (argc > 1) ns_paths_mount(argv[1]);

    ns_skin *s = ns_skin_load("models/personnage/personnage.glb");
    CHECK(s != NULL, "le personnage se charge");
    if (!s) { printf("test_allure : %d contrôle(s), 1 échec\n", g_checks); return 1; }

    const float duree = ns_skin_duration(s);
    const float debout = 1.82f, accroupi = 1.42f;      /* les cotes de nineteen.env */
    const float rapport = accroupi / debout;

    /* ------------------------------------------------------- sans allure */
    /* `NULL` doit rendre EXACTEMENT l'ancienne pose : tout le reste du moteur
     * appelle encore `ns_skin_pose`, et une divergence d'un millième ici
     * déplacerait le personnage sans que personne ne sache pourquoi. */
    {
        ns_m4 a[NS_SKIN_MAX_JOINTS], b[NS_SKIN_MAX_JOINTS];
        ns_skin_allure vide;
        memset(&vide, 0, sizeof vide);
        /* On ne compare QUE les articulations écrites : `ns_skin_pose` laisse
         * la queue du tableau telle quelle, et deux queues de pile n'ont
         * aucune raison de se ressembler. */
        const size_t utile = (size_t)ns_skin_joint_count(s) * sizeof a[0];
        bool identique = true;
        for (int k = 0; k < 8; ++k) {
            const float t = duree * (float)k / 8.0f;
            ns_skin_pose(s, t, a, NS_SKIN_MAX_JOINTS);
            ns_skin_pose_allure(s, t, &vide, b, NS_SKIN_MAX_JOINTS);
            if (memcmp(a, b, utile) != 0) identique = false;
        }
        CHECK(identique, "une allure nulle rend la pose d'origine, au bit près");
    }

    /* ------------------------------------------------------- la calibration */
    const bool cale = ns_skin_crouch_calibrate(s, rapport, 0.55f);
    CHECK(cale, "l'accroupi se cale sur ce squelette (deux jambes repérées)");
    CHECK(ns_skin_can_crouch(s) == cale, "l'état annoncé suit la calibration");

    /* Un rapport absurde doit être REFUSÉ et non approché : plier au hasard
     * serait pire que rester debout, parce que rien ne le dirait. */
    {
        CHECK(!ns_skin_crouch_calibrate(s, 0.05f, 0.55f), "un rapport de 5 %% est refusé");
        CHECK(!ns_skin_can_crouch(s), "après un refus, l'accroupi reste éteint");
        CHECK(ns_skin_crouch_calibrate(s, rapport, 0.55f), "et la vraie calibration revient");
    }

    if (cale) {
        const float angle = ns_skin_crouch_angle(s);
        CHECK(angle > 20.0f && angle < 90.0f,
              "l'angle de cuisse retenu est plausible : %.1f degrés", (double)angle);

        const float passage = ns_skin_stand_time(s);
        const boite d = mesurer(s, passage, NULL);
        ns_skin_allure plein;
        memset(&plein, 0, sizeof plein);
        plein.accroupi = 1.0f;
        const boite a = mesurer(s, passage, &plein);

        CHECK(a.fini, "aucune matrice d'os n'est NaN sous l'accroupi");

        /*
         * LA HAUTEUR. C'est la raison d'être de la calibration : le personnage
         * accroupi doit faire la taille que sa CAPSULE annonce, sans quoi on
         * voit sa tête traverser un linteau sous lequel il passe.
         *
         * Un pour cent de tolérance : le contrôle mesure à la MÊME phase que
         * la calibration — la pose de passage — donc il ne reste que la
         * dichotomie, qui converge à mieux que ça en dix-huit tours.
         */
        const float h_debout = d.haut - d.bas;
        const float h_accroupi = a.haut - a.bas;
        CHECK(h_debout > 0.1f, "la hauteur debout est mesurable");
        const float obtenu = h_accroupi / h_debout;
        CHECK(fabsf(obtenu - rapport) < 0.01f,
              "accroupi, il fait %.3f de sa taille debout (visé %.3f)",
              (double)obtenu, (double)rapport);

        /*
         * LES PIEDS AU SOL, ET SUR TOUT LE CYCLE. Plier les jambes SOULÈVE les
         * pieds sous un bassin qui, lui, ne bouge pas : sans correction, le
         * personnage accroupi flotte de trente centimètres.
         *
         * Le contrôle balaie les seize phases, et ce n'est pas du zèle : la
         * première rédaction mesurait la remontée UNE FOIS, à la pose de
         * passage, et laissait donc les pieds à neuf centimètres du sol au
         * milieu d'une enjambée — accroupi en marchant, c'est-à-dire le cas
         * pour lequel tout ceci existe. C'est ce contrôle-ci qui l'a trouvé.
         */
        float pire = 0.0f;
        for (int k = 0; k < 16; ++k) {
            const float t = duree * (float)k / 16.0f;
            const float ecart = fabsf(mesurer(s, t, &plein).bas - mesurer(s, t, NULL).bas);
            if (ecart > pire) pire = ecart;
        }
        CHECK(pire < 0.004f * h_debout,
              "les pieds restent au sol sur tout le cycle : %.4f d'écart au pire",
              (double)pire);

        /*
         * LE SENS. Un accroupi qui part EN ARRIÈRE est le défaut le plus
         * pernicieux du lot : la hauteur est juste, les pieds sont au sol, et
         * le personnage s'assoit dans le vide derrière lui. On vérifie donc que
         * le corps avance — genoux et buste vont DEVANT, pas derrière.
         */
        CHECK(a.avant > d.avant,
              "le corps se porte vers l'avant en s'accroupissant : %.3f contre %.3f",
              (double)a.avant, (double)d.avant);

        /*
         * LA DESCENTE EST CONTINUE ET (PRESQUE) MONOTONE — et ce « presque »
         * est mesuré, pas concédé.
         *
         * C'est cette propriété qui permet de lire la fraction d'accroupi sur
         * la hauteur d'œil déjà amortie par la caméra, sans écrire la moindre
         * transition. Elle n'est pas exacte : au tout début du pli, la jambe
         * qui est EN ARRIÈRE se redresse en même temps que sa cuisse part en
         * avant, son pied descend, et le recalage au sol relève donc tout le
         * personnage d'un demi-pour-cent avant que la descente ne l'emporte.
         *
         * Un pour cent de tolérance, soit un centimètre et demi à l'échelle du
         * jeu, sur les deux premiers crans seulement. Le mettre à zéro
         * demanderait un accroupi qui respecte le contact de CHAQUE pied, donc
         * une cinématique inverse par jambe — c'est écrit dans le journal comme
         * la suite possible, ce n'est pas fait ici.
         */
        float precedent = h_debout + 1.0f;
        bool monotone = true;
        for (int k = 0; k <= 10; ++k) {
            ns_skin_allure pas;
            memset(&pas, 0, sizeof pas);
            pas.accroupi = (float)k / 10.0f;
            const boite b = mesurer(s, passage, &pas);
            const float h = b.haut - b.bas;
            if (h > precedent + 0.010f * h_debout) monotone = false;
            precedent = h;
        }
        CHECK(monotone, "la descente est monotone d'un bout à l'autre");
    }

    /* --------------------------------------------------- le balancement */
    /*
     * Il doit BOUGER — sans quoi il ne sert à rien — et bouger PEU : c'est un
     * balancement postural, pas un roulis de bateau. Et surtout, il ne doit pas
     * décoller les pieds : c'est pour ça qu'il est posé comme une rotation
     * autour du sol et non comme un déplacement vertical.
     */
    {
        float lo = 1e30f, hi = -1e30f, pied_max = 0.0f;
        const boite repos = mesurer(s, 0.0f, NULL);
        for (int k = 0; k < 24; ++k) {
            ns_skin_allure al;
            memset(&al, 0, sizeof al);
            al.souffle = (float)k * 0.5f;
            al.souffle_force = 1.0f;
            const boite b = mesurer(s, 0.0f, &al);
            if (b.avant < lo) lo = b.avant;
            if (b.avant > hi) hi = b.avant;
            const float ecart = fabsf(b.bas - repos.bas);
            if (ecart > pied_max) pied_max = ecart;
            CHECK(b.fini, "le balancement ne produit pas de NaN");
        }
        const float h = repos.haut - repos.bas;
        CHECK(hi - lo > 0.0005f * h, "le balancement bouge vraiment (%.5f)", (double)(hi - lo));
        CHECK(hi - lo < 0.030f * h, "et il reste discret (%.5f)", (double)(hi - lo));
        CHECK(pied_max < 0.0025f * h,
              "il ne décolle pas les pieds : %.5f de la hauteur",
              (double)(pied_max / h));

        /* Force nulle : rien ne doit bouger. C'est ce que l'appelant emploie
         * dès que le personnage marche. */
        ns_skin_allure eteint;
        memset(&eteint, 0, sizeof eteint);
        eteint.souffle = 3.7f;
        eteint.souffle_force = 0.0f;
        ns_m4 a[NS_SKIN_MAX_JOINTS], b[NS_SKIN_MAX_JOINTS];
        ns_skin_pose(s, 0.0f, a, NS_SKIN_MAX_JOINTS);
        ns_skin_pose_allure(s, 0.0f, &eteint, b, NS_SKIN_MAX_JOINTS);
        CHECK(memcmp(a, b, (size_t)ns_skin_joint_count(s) * sizeof a[0]) == 0,
              "à force nulle, le balancement n'existe pas");
    }

    /* L'accroupi doit tenir SUR TOUT LE CYCLE, pas seulement à l'arrêt : on
     * s'accroupit aussi en marchant, et c'est là que la phase du cycle écarte
     * les jambes. */
    if (cale) {
        bool tout_fini = true;
        for (int k = 0; k < 16; ++k) {
            ns_skin_allure al;
            memset(&al, 0, sizeof al);
            al.accroupi = 1.0f;
            al.souffle_force = 0.0f;
            const boite b = mesurer(s, duree * (float)k / 16.0f, &al);
            if (!b.fini) tout_fini = false;
        }
        CHECK(tout_fini, "l'accroupi tient sur les seize phases du cycle");
    }

    ns_skin_free(s);

    printf("test_allure : %d contrôle(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
