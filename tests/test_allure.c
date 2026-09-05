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

/*
 * LES ARTICULATIONS QUE LE COUP EMPORTE, repérées de l'extérieur : on pose deux
 * fois et on regarde lesquelles ont changé.
 *
 * Le test n'a pas accès aux indices que `ns_skin` s'est donnés, et c'est bien —
 * il vérifie un comportement, pas une implémentation. Comparer deux poses suffit
 * à savoir QUI bouge, ce qui est exactement la question.
 */
static int bras_bouge(const ns_skin *s, bool *masque)
{
    ns_m4 a[NS_SKIN_MAX_JOINTS], b[NS_SKIN_MAX_JOINTS];
    ns_skin_allure rien, coup;
    memset(&rien, 0, sizeof rien);
    memset(&coup, 0, sizeof coup);
    coup.frappe = 1.0f;
    ns_skin_pose_allure(s, 0.0f, &rien, a, NS_SKIN_MAX_JOINTS);
    ns_skin_pose_allure(s, 0.0f, &coup, b, NS_SKIN_MAX_JOINTS);

    int n = 0;
    const int total = ns_skin_joint_count(s);
    for (int j = 0; j < NS_SKIN_MAX_JOINTS; ++j) masque[j] = false;
    for (int j = 0; j < total; ++j) {
        if (memcmp(&a[j], &b[j], sizeof a[j]) != 0) { masque[j] = true; ++n; }
    }
    return n;
}

/*
 * JUSQU'OÙ LE POING VA, et rien d'autre.
 *
 * La boîte du personnage entier ne peut pas répondre : accroupi, le point le
 * plus avancé du corps est le GENOU, qui sort de trente centimètres devant
 * l'épaule. Un poing qui part parfaitement bien passe alors inaperçu dans la
 * mesure, et le contrôle échoue sur un défaut qui n'existe pas — c'est
 * exactement ce qui est arrivé.
 *
 * On ne pèse donc que les sommets tenus par les os du BRAS.
 */
static float avant_du_poing(const ns_skin *s, float temps, const ns_skin_allure *al,
                            const bool *masque)
{
    ns_m4 os[NS_SKIN_MAX_JOINTS];
    ns_skin_pose_allure(s, temps, al, os, NS_SKIN_MAX_JOINTS);

    uint32_t count = 0;
    const ns_skin_vertex *v = ns_skin_vertices(s, &count);
    float avant = -1e30f;
    for (uint32_t i = 0; i < count; ++i) {
        int best = 0;
        for (int k = 1; k < NS_SKIN_INFLUENCES; ++k) {
            if (v[i].weights[k] > v[i].weights[best]) best = k;
        }
        if (!masque[v[i].joints[best]]) continue;

        float z = 0.0f;
        for (int k = 0; k < NS_SKIN_INFLUENCES; ++k) {
            const float w = v[i].weights[k];
            if (w <= 0.0f) continue;
            const ns_m4 *m = &os[v[i].joints[k]];
            z += w * (m->m[0][2] * v[i].position[0] + m->m[1][2] * v[i].position[1] +
                      m->m[2][2] * v[i].position[2] + m->m[3][2]);
        }
        if (z > avant) avant = z;
    }
    return avant;
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

    /* ------------------------------------------------------------ la frappe */
    /*
     * Ce que ces contrôles défendent, et qui ne se voit PAS autrement.
     *
     * Le bras est repéré par la géométrie du squelette — pas de nom d'os — et
     * chacun des quatre pas de ce repérage peut se tromper en silence :
     *
     *   1. les deux « mains » attrapées du même côté, et le coup part de la
     *      colonne vertébrale ;
     *   2. le coude pris pour la clavicule, et c'est l'épaule qui se hausse au
     *      lieu que le bras se lance ;
     *   3. le signe à l'envers, et le personnage frappe DERRIÈRE lui ;
     *   4. les masques trop larges, et c'est le buste entier qui pivote.
     *
     * Aucun ne fait planter quoi que ce soit. Tous se lisent sur des chiffres.
     */
    const bool cogne = ns_skin_can_hit(s);
    CHECK(cogne, "le bras se repère sur ce squelette (deux mains, une poitrine)");

    if (cogne) {
        const float angle = ns_skin_hit_angle(s);
        printf("  frappe : épaule à %.1f degrés au coup porté\n", (double)angle);
        /* Un bras qui monte à l'horizontale tourne d'un quart de tour, à la
         * pose de repos du modèle près. Hors de [30 ; 160] on n'a pas trouvé un
         * bras. */
        CHECK(angle > 30.0f && angle < 160.0f,
              "l'angle d'épaule est celui d'un bras (%.1f degrés)", (double)angle);

        ns_skin_allure repos, mi, plein;
        memset(&repos, 0, sizeof repos);
        memset(&mi, 0, sizeof mi);
        memset(&plein, 0, sizeof plein);
        mi.frappe = 0.5f;
        plein.frappe = 1.0f;

        const boite r = mesurer(s, 0.0f, &repos);
        const boite m = mesurer(s, 0.0f, &mi);
        const boite p = mesurer(s, 0.0f, &plein);
        const float h = r.haut - r.bas;

        CHECK(p.fini && m.fini, "aucune matrice d'os n'est NaN sous la frappe");

        /* Les os du bras, repérés en comparant deux poses : c'est eux, et eux
         * seuls, qu'on va peser pour savoir où le poing arrive. */
        bool bras[NS_SKIN_MAX_JOINTS];
        const int n_bras = bras_bouge(s, bras);
        const float poing_r = avant_du_poing(s, 0.0f, &repos, bras);
        const float poing_m = avant_du_poing(s, 0.0f, &mi, bras);
        const float poing_p = avant_du_poing(s, 0.0f, &plein, bras);

        /*
         * LE COUP PART DEVANT. C'est le contrôle qui attrape le signe inversé,
         * et il n'a pas d'équivalent à l'œil : un personnage vu de dos qui
         * frappe en arrière ressemble à un personnage qui frappe.
         *
         * La mesure est en unités du fichier ; à l'échelle du jeu, le
         * personnage fait 1,82 m pour 1,458 d'unités, donc un centième d'unité
         * vaut 1,25 cm.
         */
        printf("  frappe : poing à %.4f -> %.4f (mi-course %.4f), "
               "hauteur du personnage %.4f -> %.4f\n",
               (double)poing_r, (double)poing_p, (double)poing_m,
               (double)(r.haut - r.bas), (double)(p.haut - p.bas));
        CHECK(poing_p > poing_r + 0.05f * h,
              "le poing part DEVANT (%.4f contre %.4f au repos)",
              (double)poing_p, (double)poing_r);

        /*
         * LE COUDE PLIE À MI-COURSE, et se tend à l'arrivée. C'est l'armé, et
         * c'est la seule chose qui distingue un coup de poing d'un bras qu'on
         * lève : sans lui, le poing avancerait de façon monotone avec `frappe`,
         * ce qui se lit comme quelqu'un qui montre du doigt.
         */
        CHECK(poing_m < poing_p - 0.05f * h,
              "à mi-course le poing est encore ARMÉ, en retrait du coup porté "
              "(%.4f contre %.4f)", (double)poing_m, (double)poing_p);

        /* Les pieds ne bougent pas : lever un bras ne déplace pas une semelle.
         * Un masque trop large — le buste, ou le bassin — se voit ici tout de
         * suite, et nulle part ailleurs sans capture. */
        CHECK(fabsf(p.bas - r.bas) < 0.0005f * h,
              "la frappe ne bouge pas les pieds (%.6f de la hauteur)",
              (double)(fabsf(p.bas - r.bas) / h));

        /* Le poing s'arrête à hauteur d'épaule, donc SOUS la tête : la hauteur
         * du personnage ne change pas. Un bras qui monterait au-dessus du crâne
         * voudrait dire que la calibration a manqué sa cote. */
        CHECK(fabsf(p.haut - r.haut) < 0.005f * h,
              "le coup ne dépasse pas la tête (%.5f de la hauteur)",
              (double)(fabsf(p.haut - r.haut) / h));

        /*
         * UN SEUL BRAS, et c'est le contrôle le plus sévère des six : on compte
         * les articulations dont la matrice a bougé. Quatre ou cinq, c'est un
         * bras. Au-delà de huit sur un squelette de dix-neuf os, c'est qu'on
         * emporte le buste — et le personnage se met à pivoter du torse à
         * chaque coup, ce qui est exactement le défaut que le repérage par
         * topologie doit éviter.
         */
        printf("  frappe : %d os sur %d emportés par le coup\n",
               n_bras, ns_skin_joint_count(s));
        CHECK(n_bras >= 2 && n_bras <= 8,
              "le coup emporte un BRAS et pas le buste (%d os sur %d)",
              n_bras, ns_skin_joint_count(s));

        /* Et le geste doit tenir sur tout le cycle : on cogne aussi en
         * marchant, et c'est là que l'épaule est déjà tournée par la foulée. */
        {
            bool tout_fini = true;
            for (int k = 0; k < 16; ++k) {
                const boite b = mesurer(s, duree * (float)k / 16.0f, &plein);
                if (!b.fini) tout_fini = false;
            }
            CHECK(tout_fini, "la frappe tient sur les seize phases du cycle");
        }

        /* Elle se compose avec l'accroupi sans que l'un défasse l'autre : on
         * peut cogner une borne accroupi, et les deux transformations sont
         * posées l'une après l'autre sur le même échantillon. */
        if (cale) {
            ns_skin_allure deux;
            memset(&deux, 0, sizeof deux);
            deux.accroupi = 1.0f;
            deux.frappe = 1.0f;
            const boite b = mesurer(s, 0.0f, &deux);

            ns_skin_allure seul;
            memset(&seul, 0, sizeof seul);
            seul.accroupi = 1.0f;
            const boite c = mesurer(s, 0.0f, &seul);

            CHECK(b.fini, "accroupi ET frappe ensemble ne produisent pas de NaN");
            CHECK(fabsf(b.bas - c.bas) < 0.0015f * h,
                  "frapper accroupi ne décolle pas les pieds (%.5f de la hauteur)",
                  (double)(fabsf(b.bas - c.bas) / h));

            /*
             * ACCROUPI, LE POING NE RECULE PAS — et c'est LE contrôle qui
             * justifie que le geste soit une VISÉE et non un angle ajouté.
             *
             * Ce qu'il attrape, et le chiffre est celui du défaut réel : avec
             * un angle CONSTANT ajouté à la pose courante, le poing du
             * personnage accroupi reculait de 0,4756 à 0,2966 unité, soit
             * vingt-deux centimètres à l'échelle du jeu, DANS LE DOS. La visée
             * l'amène à 0,4808. Rien de spectaculaire, et c'est normal — le
             * corps est plié en deux, il ne reste presque plus de course
             * devant l'épaule — mais le geste ne part plus à l'envers.
             *
             * À MI-ACCROUPI, en revanche, il doit vraiment porter : c'est la
             * posture qu'on traverse chaque fois qu'on se baisse, et elle dure
             * assez pour qu'on y frappe. Mesuré : 0,4876 -> 0,5568, soit huit
             * centimètres et demi de gagnés.
             */
            const float poing_accr_0 = avant_du_poing(s, 0.0f, &seul, bras);
            const float poing_accr_1 = avant_du_poing(s, 0.0f, &deux, bras);
            printf("  frappe : accroupi, poing à %.4f -> %.4f\n",
                   (double)poing_accr_0, (double)poing_accr_1);
            CHECK(poing_accr_1 >= poing_accr_0,
                  "accroupi, le coup ne part pas EN ARRIÈRE (%.4f contre %.4f)",
                  (double)poing_accr_1, (double)poing_accr_0);

            {
                ns_skin_allure mi_bas, mi_coup;
                memset(&mi_bas, 0, sizeof mi_bas);
                memset(&mi_coup, 0, sizeof mi_coup);
                mi_bas.accroupi = 0.5f;
                mi_coup.accroupi = 0.5f;
                mi_coup.frappe = 1.0f;
                const float a0 = avant_du_poing(s, 0.0f, &mi_bas, bras);
                const float a1 = avant_du_poing(s, 0.0f, &mi_coup, bras);
                printf("  frappe : mi-accroupi, poing à %.4f -> %.4f\n",
                       (double)a0, (double)a1);
                CHECK(a1 > a0 + 0.04f * h,
                      "mi-accroupi, le coup porte vraiment (%.4f contre %.4f)",
                      (double)a1, (double)a0);
            }
        }

        /* Force nulle : rien ne bouge. C'est l'état de toutes les images où
         * l'on ne frappe pas, c'est-à-dire presque toutes. */
        {
            ns_m4 a[NS_SKIN_MAX_JOINTS], b[NS_SKIN_MAX_JOINTS];
            ns_skin_pose(s, 0.31f, a, NS_SKIN_MAX_JOINTS);
            ns_skin_pose_allure(s, 0.31f, &repos, b, NS_SKIN_MAX_JOINTS);
            CHECK(memcmp(a, b, (size_t)ns_skin_joint_count(s) * sizeof a[0]) == 0,
                  "à frappe nulle, le bras est celui du cycle, au bit près");
        }
    }

    ns_skin_free(s);

    printf("test_allure : %d contrôle(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
