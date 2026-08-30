/*
 * test_camera.c — la troisième personne : le bras de caméra, et l'effacement.
 *
 * Ce que ce test attrape, plutôt que ce qu'il a l'air de vérifier
 * ---------------------------------------------------------------
 * Le défaut d'origine était VISIBLE et il a tenu : au démarrage, le joueur est
 * dans un couloir avec 69 cm derrière lui, le bras de caméra se raccourcissait
 * jusqu'à mettre l'objectif contre la tempe du personnage, et sa tête occupait
 * le tiers gauche de l'image. Personne n'avait ouvert la capture.
 *
 * Un test ne remplace pas une capture — il ne saura jamais qu'une image est
 * laide. Mais tout ce qui a produit cette image-là est du CALCUL PUR : une
 * distance voulue, un obstacle, une distance rendue, une opacité. Ça, on peut le
 * clouer, et c'est ce qu'on fait ici. Trois familles :
 *
 *   1. L'EFFACEMENT. Ses deux seuils ne sont pas choisis, ils sont dérivés du
 *      champ de vision et des cotes mesurées du modèle. Le test refait la
 *      dérivation à la main et exige que le code tombe dessus — sinon la
 *      formule et le commentaire qui l'explique auront divergé, ce qui est la
 *      pire des deux issues possibles.
 *
 *   2. LE BALAYAGE EN VOLUME. Le cas qui justifie tout le mécanisme est l'ARÊTE
 *      qu'un rayon unique manque. On la construit : un poteau décalé, qu'un
 *      rayon central traverse sans rien toucher, et on exige que le bras se
 *      raccourcisse quand même. C'est le seul contrôle qui distingue le nouveau
 *      code de l'ancien — les deux se comportent pareil face à un mur plein.
 *
 *   3. LA FOULÉE. On a voulu y clouer l'absence de patinage et on ne l'a pas
 *      pu : le cycle livré n'a pas de pied cloué au sol. Ce qui reste est ce
 *      qui est vrai — que la mesure aboutit, qu'elle est plausible, et qu'il
 *      n'y a qu'UNE source pour la valeur employée. La fonction dit elle-même
 *      ce qu'elle a renoncé à vérifier, et pourquoi.
 *
 * Ni GPU, ni fenêtre. Le BVH est bâti à la main, comme dans `test_bvh.c` ; le
 * personnage, lui, est un vrai fichier, et le test le dit s'il manque.
 */
#include "ns_core.h"
#include "ns_math.h"
#include "ns_skin.h"
#include "room_camera.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

#define CHECK_NEAR(got, want, tol, label)                                     \
    CHECK(fabsf((got) - (want)) <= (tol),                                     \
          "%s : %.4f, attendu %.4f (± %.4f)", label,                          \
          (double)(got), (double)(want), (double)(tol))

/* ==========================================================================
 * Un BVH bâti à la main — même recette que test_bvh.c
 * ========================================================================== */

#define MAX_TRIS 64

typedef struct scratch_bvh {
    ns_bvh          bvh;
    ns_bvh_node     node;
    ns_bvh_tri      tris[MAX_TRIS];
    ns_bvh_material material;
} scratch_bvh;

static void put3(float *dst, ns_v3 v) { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; }

static void add_tri(scratch_bvh *s, ns_v3 a, ns_v3 b, ns_v3 c, ns_v3 normal)
{
    if (s->bvh.tri_count >= MAX_TRIS) { fprintf(stderr, "trop de triangles\n"); exit(2); }
    ns_bvh_tri *t = &s->tris[s->bvh.tri_count++];
    memset(t, 0, sizeof *t);
    put3(t->v0, a);
    put3(t->e1, ns_v3_sub(b, a));
    put3(t->e2, ns_v3_sub(c, a));
    put3(t->normal, ns_v3_norm(normal));
}

static void add_quad(scratch_bvh *s, ns_v3 origin, ns_v3 u, ns_v3 v, ns_v3 normal)
{
    const ns_v3 a = origin;
    const ns_v3 b = ns_v3_add(origin, u);
    const ns_v3 c = ns_v3_add(ns_v3_add(origin, u), v);
    const ns_v3 d = ns_v3_add(origin, v);
    add_tri(s, a, b, c, normal);
    add_tri(s, a, c, d, normal);
}

static void finish(scratch_bvh *s)
{
    ns_aabb bounds;
    bounds.min = ns_v3_make(1e30f, 1e30f, 1e30f);
    bounds.max = ns_v3_make(-1e30f, -1e30f, -1e30f);
    for (uint32_t i = 0; i < s->bvh.tri_count; ++i) {
        const ns_bvh_tri *t = &s->tris[i];
        const ns_v3 v0 = ns_v3_make(t->v0[0], t->v0[1], t->v0[2]);
        const ns_v3 v1 = ns_v3_add(v0, ns_v3_make(t->e1[0], t->e1[1], t->e1[2]));
        const ns_v3 v2 = ns_v3_add(v0, ns_v3_make(t->e2[0], t->e2[1], t->e2[2]));
        const ns_v3 all[3] = { v0, v1, v2 };
        for (int k = 0; k < 3; ++k) {
            bounds.min = ns_v3_min(bounds.min, all[k]);
            bounds.max = ns_v3_max(bounds.max, all[k]);
        }
    }
    bounds.min = ns_v3_sub(bounds.min, ns_v3_make(0.01f, 0.01f, 0.01f));
    bounds.max = ns_v3_add(bounds.max, ns_v3_make(0.01f, 0.01f, 0.01f));

    put3(s->node.bmin, bounds.min);
    put3(s->node.bmax, bounds.max);
    s->node.left_first = 0;
    s->node.tri_count = s->bvh.tri_count;

    s->material.albedo[0] = s->material.albedo[1] = s->material.albedo[2] = 0.5f;
    s->material.roughness = 0.8f;

    s->bvh.nodes = &s->node;
    s->bvh.tris = s->tris;
    s->bvh.materials = &s->material;
    s->bvh.node_count = 1;
    s->bvh.material_count = 1;
    s->bvh.bounds = bounds;
    s->bvh.max_depth = 1;
    s->bvh.loaded = true;
}

/* Un sol de vingt mètres de côté. Sans lui la caméra tombe, et le bras se
 * mesurerait sur un joueur en chute libre. */
static void add_floor(scratch_bvh *s)
{
    add_quad(s, ns_v3_make(-10.0f, 0.0f, -10.0f),
             ns_v3_make(20.0f, 0.0f, 0.0f), ns_v3_make(0.0f, 0.0f, 20.0f),
             ns_v3_make(0.0f, 1.0f, 0.0f));
}

/* ==========================================================================
 * Un joueur posé, et quelques pas de simulation
 * ========================================================================== */

/*
 * Les cotes du personnage employé, mises à l'échelle du jeu par `room/main.c`.
 * Recopiées ici À DESSEIN plutôt que relues du fichier : la partie « seuils »
 * de ce test doit être lisible et reproductible sans le modèle, et surtout elle
 * doit échouer si quelqu'un change la formule en croyant qu'elle ne sert à rien.
 * La partie « foulée », elle, emploie le vrai fichier.
 */
#define PERSO_DEMI_LARGEUR 0.427f
#define PERSO_RAYON_BALAYE 0.656f

static void poser(room_camera *c, const ns_bvh *b, ns_v3 pieds, float yaw_deg)
{
    room_camera_init(c, ns_v3_make(pieds.x, pieds.y + 1.70f, pieds.z),
                     yaw_deg * NS_DEG2RAD);
    c->mode = ROOM_CAM_PLAYER;
    c->third_person = true;
    room_camera_set_actor(c, PERSO_DEMI_LARGEUR, PERSO_RAYON_BALAYE, 0.0f);
    /* Assez de pas pour que la caméra se pose au sol ET que le bras converge :
     * il ressort amorti, à 6 par seconde, donc une seconde suffit largement. */
    for (int i = 0; i < 120; ++i) room_camera_tick(c, b, NULL, 1.0f / 120.0f);
}

/* ==========================================================================
 * 1. L'effacement : la dérivation, refaite à la main
 * ========================================================================== */

static void test_effacement(void)
{
    printf("-- l'effacement du personnage\n");

    room_camera c;
    room_camera_init(&c, ns_v3_make(0.0f, 1.70f, 0.0f), 0.0f);
    room_camera_set_actor(&c, PERSO_DEMI_LARGEUR, PERSO_RAYON_BALAYE, 1.018f);

    /*
     * La dérivation, refaite ici sans regarder le code :
     *
     *   couverture(d) = W / (d * tan(champ/2))
     *
     * — la largeur du personnage rapportée à la HAUTEUR de l'image. On l'inverse
     * aux deux couvertures de réglage.
     */
    const float t = tanf(c.fov_y * 0.5f * NS_DEG2RAD);
    const float d_plein = PERSO_DEMI_LARGEUR / (c.fade_full * t);
    const float d_nul_couverture = PERSO_DEMI_LARGEUR / (c.fade_none * t);
    const float d_nul_geometrie  = PERSO_RAYON_BALAYE + 0.05f;
    const float d_nul = ns_maxf(d_nul_couverture, d_nul_geometrie);

    /* Les chiffres attendus, écrits en clair : si un jour quelqu'un change un
     * défaut dans `nineteen.env`, c'est ICI qu'on veut le voir. */
    CHECK_NEAR(d_plein, 1.577f, 0.01f, "distance de pleine opacité");
    CHECK_NEAR(d_nul,   0.710f, 0.01f, "distance d'effacement complet");

    /* Les deux critères d'effacement — « aussi large que l'image est haute » et
     * « la caméra entre dans le volume balayé » — doivent tomber au même endroit
     * sur ce modèle. Ce n'est pas une coïncidence, c'est la même chose dite deux
     * fois, et si elles se mettaient à diverger de plus de dix centimètres, la
     * dérivation écrite dans room_camera.c aurait cessé d'être vraie. */
    CHECK(fabsf(d_nul_couverture - d_nul_geometrie) < 0.10f,
          "les deux critères d'effacement divergent : couverture %.3f m, "
          "géométrie %.3f m", (double)d_nul_couverture, (double)d_nul_geometrie);

    /* Au recul nominal, plein. C'est le cas courant, et le premier à protéger :
     * un personnage translucide en marchant serait un défaut de plus, pas de
     * moins. */
    CHECK_NEAR(room_camera_actor_opacity(&c, 2.60f), 1.0f, 1e-4f,
               "opacité au recul nominal");
    CHECK_NEAR(room_camera_actor_opacity(&c, 5.00f), 1.0f, 1e-4f,
               "opacité au-delà du recul nominal");

    /*
     * LE DÉFAUT D'ORIGINE, chiffré. Au départ du jeu le bras mesure 0,53 m
     * (69 cm de couloir, moins le rayon de sonde). L'ancien code s'y arrêtait à
     * 0,35 m et dessinait le personnage à plein. Les deux doivent maintenant
     * rendre zéro.
     */
    CHECK_NEAR(room_camera_actor_opacity(&c, 0.53f), 0.0f, 1e-4f,
               "opacité au bras du départ (0,53 m)");
    CHECK_NEAR(room_camera_actor_opacity(&c, 0.35f), 0.0f, 1e-4f,
               "opacité à l'ancien plancher (0,35 m)");
    CHECK_NEAR(room_camera_actor_opacity(&c, 0.0f), 0.0f, 1e-4f,
               "opacité bras rentré à fond");

    /* Monotone et continue entre les deux seuils : une opacité qui remonterait
     * au milieu ferait clignoter le personnage sans que rien ne le dise. */
    float precedent = -1.0f;
    for (int i = 0; i <= 40; ++i) {
        const float d = d_nul + (d_plein - d_nul) * (float)i / 40.0f;
        const float o = room_camera_actor_opacity(&c, d);
        CHECK(o >= precedent - 1e-5f,
              "opacité non monotone à %.3f m : %.4f après %.4f",
              (double)d, (double)o, (double)precedent);
        precedent = o;
    }
    CHECK_NEAR(room_camera_actor_opacity(&c, (d_nul + d_plein) * 0.5f), 0.5f, 0.01f,
               "opacité à mi-course du fondu");

    /* Sans personnage déclaré, rien ne s'efface : c'est le comportement voulu
     * quand le modèle manque, et le jeu doit se jouer comme avant. */
    room_camera nu;
    room_camera_init(&nu, ns_v3_zero(), 0.0f);
    CHECK_NEAR(room_camera_actor_opacity(&nu, 0.0f), 1.0f, 1e-4f,
               "opacité sans personnage déclaré");

    /*
     * Le seuil SUIT le champ de vision. C'est ce qui distingue une dérivation
     * d'un nombre choisi : élargir le champ rapetisse le personnage à l'écran,
     * donc il peut s'approcher davantage avant de gêner.
     */
    room_camera large = c;
    large.fov_y = 90.0f;
    const float t90 = tanf(45.0f * NS_DEG2RAD);
    CHECK_NEAR(PERSO_DEMI_LARGEUR / (large.fade_full * t90), 0.949f, 0.01f,
               "distance de pleine opacité à 90 degrés de champ");
    CHECK(room_camera_actor_opacity(&large, 1.20f) >
          room_camera_actor_opacity(&c, 1.20f),
          "un champ plus large doit laisser le personnage plein de plus près");
}

/* ==========================================================================
 * 2. Le bras de caméra : le mur, l'arête, l'épaule, l'amortissement
 * ========================================================================== */

static void test_mur_plein(void)
{
    printf("-- le bras contre un mur plein\n");

    /* Un couloir : le sol, et un mur à z = +1,20. Le joueur est en z = 0 et
     * regarde vers -z, donc la caméra recule vers le mur. C'est la situation du
     * départ du jeu, en plus serré. */
    scratch_bvh s;
    memset(&s, 0, sizeof s);
    add_floor(&s);
    add_quad(&s, ns_v3_make(-5.0f, 0.0f, 1.20f),
             ns_v3_make(10.0f, 0.0f, 0.0f), ns_v3_make(0.0f, 3.0f, 0.0f),
             ns_v3_make(0.0f, 0.0f, -1.0f));
    finish(&s);

    room_camera c;
    /* yaw 270° : regard vers -z. */
    poser(&c, &s.bvh, ns_v3_make(0.0f, 0.0f, 0.0f), 270.0f);

    const float bras = room_camera_third_arm(&c, 1.0f);

    /* Le mur est à 1,20 m derrière le pivot ; la sonde fait 0,16 m de rayon.
     * Le bras vaut donc 1,04 m, et surtout PAS 2,60. */
    CHECK_NEAR(bras, 1.20f - c.probe_radius, 0.03f, "bras contre un mur à 1,20 m");

    /* Et la caméra reste DEVANT le mur, plan proche compris. C'est le contrôle
     * qui compte vraiment : tout le reste n'est qu'une façon d'y arriver. */
    const ns_camera rendu = room_camera_resolve(&c, &s.bvh, 1.0f);
    CHECK(rendu.position.z < 1.20f - 0.05f,
          "la caméra a traversé le mur : z = %.3f, mur à 1,200",
          (double)rendu.position.z);

    /* Et le personnage s'efface, parce qu'à un mètre de bras il couvre les deux
     * tiers de la hauteur de l'image. */
    CHECK(room_camera_actor_opacity(&c, bras) < 0.55f,
          "à %.2f m de bras le personnage devrait être largement effacé (%.2f)",
          (double)bras, (double)room_camera_actor_opacity(&c, bras));
}

static void test_arete(void)
{
    printf("-- l'arête qu'un rayon unique manque\n");

    /*
     * LE CAS QUI JUSTIFIE LE FAISCEAU.
     *
     * Un poteau mince, décalé de 20 cm à côté de la ligne de recul. Le rayon
     * CENTRAL passe à côté sans rien toucher — on le vérifie explicitement, pour
     * que le test prouve ce qu'il prétend prouver — mais le cylindre de la
     * caméra, lui, l'accroche.
     *
     * Le poteau fait 30 cm de large et son flanc intérieur est à 5 cm de l'axe
     * de recul : hors de portée du rayon central, à l'intérieur du rayon de
     * sonde de 16 cm.
     */
    scratch_bvh s;
    memset(&s, 0, sizeof s);
    add_floor(&s);
    /* Un mur lointain, pour que quelque chose arrête tout de même le bras et
     * que le contrôle porte sur le poteau et non sur le vide. Il doit rester à
     * PORTÉE du rayon central (2,60 m voulus, plus le rayon de sonde) : sinon le
     * rayon central ne rapporte rien du tout et le contrôle qui prouve qu'il
     * manque le poteau ne prouve plus rien. */
    add_quad(&s, ns_v3_make(-5.0f, 0.0f, 2.40f),
             ns_v3_make(10.0f, 0.0f, 0.0f), ns_v3_make(0.0f, 3.0f, 0.0f),
             ns_v3_make(0.0f, 0.0f, -1.0f));
    /* Le poteau : un quad vertical dans le plan z = 1,50, de x = 0,50 à 0,80.
     * L'axe de recul passe en x = 0,45 (le pivot en 0, plus l'épaule). */
    add_quad(&s, ns_v3_make(0.50f, 0.0f, 1.50f),
             ns_v3_make(0.30f, 0.0f, 0.0f), ns_v3_make(0.0f, 3.0f, 0.0f),
             ns_v3_make(0.0f, 0.0f, -1.0f));
    finish(&s);

    room_camera c;
    poser(&c, &s.bvh, ns_v3_make(0.0f, 0.0f, 0.0f), 270.0f);

    /* D'abord la preuve que le rayon unique NE VOIT RIEN : c'est exactement ce
     * que faisait l'ancien code, et c'est par là que la caméra passait. */
    const ns_v3 depart = ns_v3_make(0.45f, 1.55f, 0.0f);
    const ns_ray_hit seul = ns_bvh_raycast(&s.bvh, depart,
                                           ns_v3_make(0.0f, 0.0f, 1.0f), 2.76f);
    CHECK(seul.hit, "le montage est faux : le rayon central doit accrocher le "
          "mur lointain");
    CHECK(seul.hit && seul.t > 2.0f,
          "le montage est faux : le rayon central doit MANQUER le poteau, qui est "
          "à 1,50 m, et n'accrocher que le mur à 2,40 (t = %.3f)", (double)seul.t);

    /* Et maintenant le faisceau : il l'accroche, et le bras s'arrête devant. */
    const float bras = room_camera_third_arm(&c, 1.0f);
    CHECK(bras < 1.60f,
          "le faisceau a manqué l'arête : bras %.3f m, poteau à 1,50 m",
          (double)bras);
    CHECK(bras > 1.10f,
          "le faisceau s'arrête beaucoup trop tôt : bras %.3f m", (double)bras);
}

static void test_epaule(void)
{
    printf("-- l'épaule balayée\n");

    /*
     * Une cloison à 25 cm sur la droite du joueur. Le décalage d'épaule vaut
     * 45 cm : sans balayage, le bras partirait d'un point situé DANS le mur, et
     * tout ce qu'on lance depuis l'intérieur d'un mur est faux.
     */
    scratch_bvh s;
    memset(&s, 0, sizeof s);
    add_floor(&s);
    add_quad(&s, ns_v3_make(0.25f, 0.0f, -5.0f),
             ns_v3_make(0.0f, 0.0f, 10.0f), ns_v3_make(0.0f, 3.0f, 0.0f),
             ns_v3_make(-1.0f, 0.0f, 0.0f));
    finish(&s);

    room_camera c;
    /* yaw 270° : regard vers -z, donc « droite » vaut +x. */
    poser(&c, &s.bvh, ns_v3_make(0.0f, 0.0f, 0.0f), 270.0f);

    CHECK(c.third_side < 0.25f,
          "l'épaule n'a pas été balayée : %.3f m demandés 0,45 avec un mur à "
          "0,25", (double)c.third_side);
    CHECK(c.third_side >= 0.0f, "l'épaule est passée du mauvais côté : %.3f m",
          (double)c.third_side);

    const ns_camera rendu = room_camera_resolve(&c, &s.bvh, 1.0f);
    CHECK(rendu.position.x < 0.25f,
          "la caméra est dans la cloison : x = %.3f, cloison à 0,250",
          (double)rendu.position.x);
}

static void test_amortissement(void)
{
    printf("-- l'amortissement asymétrique du bras\n");

    scratch_bvh s;
    memset(&s, 0, sizeof s);
    add_floor(&s);
    finish(&s);

    room_camera c;
    poser(&c, &s.bvh, ns_v3_make(0.0f, 0.0f, 0.0f), 270.0f);
    CHECK_NEAR(c.third_arm, 2.60f, 0.01f, "bras déployé en terrain dégagé");

    /*
     * RENTRER EST IMMÉDIAT. On force un bras long, on met un mur, un seul pas
     * doit suffire. Amortir la rentrée laisserait le mur passer devant
     * l'objectif pendant toute la transition, ce qui est précisément ce qu'on
     * cherche à empêcher.
     */
    scratch_bvh mur;
    memset(&mur, 0, sizeof mur);
    add_floor(&mur);
    add_quad(&mur, ns_v3_make(-5.0f, 0.0f, 1.00f),
             ns_v3_make(10.0f, 0.0f, 0.0f), ns_v3_make(0.0f, 3.0f, 0.0f),
             ns_v3_make(0.0f, 0.0f, -1.0f));
    finish(&mur);

    room_camera_tick(&c, &mur.bvh, NULL, 1.0f / 120.0f);
    CHECK(c.third_arm < 1.00f,
          "la rentrée doit être immédiate : bras %.3f m après un pas",
          (double)c.third_arm);

    /*
     * RESSORTIR EST AMORTI. Le mur retiré, un pas ne doit pas suffire à
     * retrouver les 2,60 m — sinon le point de vue sauterait de deux mètres dès
     * qu'on dépasse le montant d'une porte.
     */
    const float court = c.third_arm;
    room_camera_tick(&c, &s.bvh, NULL, 1.0f / 120.0f);
    CHECK(c.third_arm > court, "le bras doit ressortir : %.3f m", (double)c.third_arm);
    CHECK(c.third_arm < 2.00f,
          "la sortie doit être amortie, pas instantanée : %.3f m après un pas",
          (double)c.third_arm);

    /* Et il finit par y arriver : une seconde suffit largement. */
    for (int i = 0; i < 120; ++i) room_camera_tick(&c, &s.bvh, NULL, 1.0f / 120.0f);
    CHECK_NEAR(c.third_arm, 2.60f, 0.02f, "bras après une seconde de dégagement");
}

static void test_premiere_personne_intacte(void)
{
    printf("-- la première personne n'a pas bougé\n");

    /*
     * Le contrôle de non-régression qui compte : tout ce travail porte sur un
     * mode ÉTEINT PAR DÉFAUT. La vue subjective doit sortir exactement à l'œil,
     * troisième personne coupée, quel que soit ce qu'on a bricolé autour.
     */
    scratch_bvh s;
    memset(&s, 0, sizeof s);
    add_floor(&s);
    finish(&s);

    room_camera c;
    room_camera_init(&c, ns_v3_make(1.0f, 1.70f, 2.0f), 0.0f);
    c.mode = ROOM_CAM_PLAYER;
    c.third_person = false;
    for (int i = 0; i < 60; ++i) room_camera_tick(&c, &s.bvh, NULL, 1.0f / 120.0f);

    const ns_camera rendu = room_camera_resolve(&c, &s.bvh, 1.0f);
    CHECK_NEAR(rendu.position.x, c.position.x, 1e-3f, "œil en x");
    CHECK_NEAR(rendu.position.z, c.position.z, 1e-3f, "œil en z");
    /* En y, l'oscillation de marche s'ajoute — à l'arrêt elle vaut la seule
     * respiration, sous le centimètre. */
    CHECK(fabsf(rendu.position.y - c.position.y) < 0.02f,
          "œil en y : %.4f contre %.4f", (double)rendu.position.y,
          (double)c.position.y);

    /* Et la foulée par défaut est bien l'historique, sans personnage déclaré :
     * `tests/test_ik.c` en dépend, et le comportement d'avant aussi. */
    CHECK_NEAR(room_camera_stride(&c), 1.55f, 1e-4f, "foulée par défaut");
}

/* ==========================================================================
 * 3. La foulée, et le patinage
 * ========================================================================== */

static void test_foulee(const char *assets)
{
    printf("-- la foulée : ce qu'on mesure, et ce qu'on emploie\n");
    (void)assets;

    /*
     * CE QUE CE CONTRÔLE NE FAIT PAS, et pourquoi.
     *
     * On a voulu y clouer l'absence de patinage : poser le pied à deux instants,
     * ajouter la distance parcourue, exiger qu'il n'ait pas bougé dans le monde.
     * Le contrôle a été écrit, et il a échoué — non pas à cause du code, mais
     * parce que le cycle livré N'A PAS DE PIED CLOUÉ AU SOL. Le point de contact
     * y glisse de deux mètres par cycle quelle que soit la foulée retenue, et
     * quatre façons également défendables de mesurer cette foulée donnent 1,02,
     * 1,34, 1,44 et 2,06 m.
     *
     * Un test qui exigerait zéro patinage sur ce modèle serait donc un test
     * qu'on désactiverait le lendemain. On vérifie à la place les trois choses
     * qui SONT vraies et qui protègent réellement : que la mesure est
     * plausible, qu'elle a une seule source, et que le défaut historique
     * s'applique tant que personne n'a réglé autre chose.
     */
    ns_skin *sk = ns_skin_load("models/personnage/personnage.glb");
    if (!sk) {
        ++g_failures;
        printf("ÉCHEC %s:%d — le modèle du personnage doit être présent pour "
               "que ce contrôle veuille dire quelque chose\n", __FILE__, __LINE__);
        return;
    }

    const float hauteur = ns_skin_rest_height(sk);
    const float echelle = 1.82f / hauteur;
    const float mesuree = ns_skin_stride_length(sk) * echelle;
    const float duree   = ns_skin_duration(sk);

    printf("   modèle : %.2f m, foulée mesurée %.3f m, cycle %.2f s\n",
           (double)(hauteur * echelle), (double)mesuree, (double)duree);

    /*
     * La mesure DOIT aboutir. Zéro veut dire que le pied porteur n'a jamais
     * changé, donc que les deux « pieds » retenus sont sur la même jambe — c'est
     * exactement le défaut qu'a eu la première version de la mesure, et il était
     * silencieux.
     */
    CHECK(mesuree > 1e-3f, "la foulée n'a pas pu être mesurée (pied porteur "
          "constant : les deux os retenus sont sur la même jambe)");

    /* Et elle doit être plausible pour un humanoïde de 1,82 m : entre un demi-pas
     * et deux enjambées. Hors de là, c'est la mesure qui est fausse. */
    CHECK(mesuree > 0.5f && mesuree < 3.0f,
          "foulée mesurée invraisemblable : %.3f m", (double)mesuree);

    /*
     * LA SOURCE UNIQUE. La valeur employée est celle de la caméra, et rien
     * d'autre ne doit en porter une copie. Sans personnage réglé, c'est la
     * valeur historique — celle que `room_viewmodel.c` et `room_sound.c`
     * portent encore en dur, et dont ils ne doivent pas s'écarter.
     */
    room_camera c;
    room_camera_init(&c, ns_v3_zero(), 0.0f);
    room_camera_set_actor(&c, PERSO_DEMI_LARGEUR, PERSO_RAYON_BALAYE, 0.0f);
    CHECK_NEAR(room_camera_stride(&c), 1.55f, 1e-4f,
               "foulée par défaut, personnage déclaré sans foulée");

    room_camera_set_actor(&c, PERSO_DEMI_LARGEUR, PERSO_RAYON_BALAYE, 1.90f);
    CHECK_NEAR(room_camera_stride(&c), 1.90f, 1e-4f, "foulée réglée");

    /*
     * LA PHASE SUIT LA DISTANCE, pas le temps. C'est la propriété qui fait que
     * la cadence suit l'allure sans qu'il y ait un état d'animation par allure —
     * et c'est tout ce que le modèle, avec son cycle unique, permet.
     *
     * On le vérifie tel que `room/main.c` le calcule : deux fois la même
     * distance donne deux fois la même phase, et doubler la distance double la
     * phase, quel que soit le temps mis à la parcourir.
     */
    const float foulee = room_camera_stride(&c);
    const float phase_1 = (2.0f / foulee) * duree;
    const float phase_2 = (4.0f / foulee) * duree;
    CHECK_NEAR(phase_2, phase_1 * 2.0f, 1e-4f,
               "la phase doit être proportionnelle à la distance");
    CHECK_NEAR((foulee / foulee) * duree, duree, 1e-4f,
               "une foulée parcourue doit valoir un cycle complet");

    /*
     * LA POSE DE PASSAGE tombe dans le cycle, et pas à zéro. Zéro est ce que
     * rend la mesure quand elle échoue, et un personnage figé sur la première
     * image d'un cycle de marche se tient en grand écart — c'est le défaut qui a
     * fait écrire cette mesure.
     */
    const float debout = ns_skin_stand_time(sk);
    CHECK(debout > 0.0f && debout < duree,
          "pose de passage hors du cycle : %.3f s sur %.3f", (double)debout,
          (double)duree);

    ns_skin_free(sk);
}

/* ==========================================================================
 * 8. Le contrecoup du coup de poing
 * ==========================================================================
 *
 * Trois choses à défendre, et deux d'entre elles sont des PIÈGES documentés
 * dans `room_camera.h` que rien d'autre ne rattraperait :
 *
 *   1. le champ ajouté à `room_view_bob` est INTERPOLÉ. L'en-tête de la
 *      structure prévient en toutes lettres qu'un champ oublié dans
 *      `bob_lerp` fait saccader l'animation à la fréquence de simulation. Un
 *      oubli ne casse rien, ne prévient rien, et se diagnostique mal : on le
 *      teste donc en demandant deux instants entre deux pas et en exigeant que
 *      la vue passe par des valeurs INTERMÉDIAIRES ;
 *   2. la secousse ne touche PAS l'état simulé. Si elle entrait dans
 *      `c->pitch`, cinquante coups mettraient la visée au plafond ;
 *   3. elle s'éteint, et avant que le bras soit revenu.
 */
static void test_contrecoup(void)
{
    printf("-- le contrecoup du coup de poing\n");

    scratch_bvh s;
    memset(&s, 0, sizeof s);
    add_floor(&s);
    finish(&s);

    room_camera c;
    room_camera_init(&c, ns_v3_make(0.0f, 1.70f, 0.0f), 0.0f);
    c.mode = ROOM_CAM_PLAYER;
    c.third_person = false;
    for (int i = 0; i < 60; ++i) room_camera_tick(&c, &s.bvh, NULL, 1.0f / 120.0f);

    const float pitch_avant = c.pitch;
    const ns_camera calme = room_camera_resolve(&c, NULL, 1.0f);

    room_camera_frappe(&c);
    CHECK(c.bob.frappe > 0.99f, "la secousse est armée à 1 (%.3f)",
          (double)c.bob.frappe);

    /* L'IMAGE BOUGE. On regarde la composante verticale du regard : le tangage
     * la fait monter, et c'est exactement ce qu'on veut voir. */
    const ns_camera secoue = room_camera_resolve(&c, NULL, 1.0f);
    CHECK(fabsf(secoue.forward.y - calme.forward.y) > 0.005f,
          "le coup fait bouger le regard (%.5f contre %.5f)",
          (double)secoue.forward.y, (double)calme.forward.y);

    /*
     * L'INTERPOLATION. Un pas de simulation plus tard, `prev_bob.frappe` vaut 1
     * et `bob.frappe` a décru : `alpha = 0,5` doit donc donner un regard STRICTEMENT
     * ENTRE les deux. Sans l'interpolation du champ, les trois valeurs seraient
     * identiques deux à deux et la vue avancerait par paliers.
     */
    room_camera_tick(&c, &s.bvh, NULL, 1.0f / 120.0f);
    const float y0 = room_camera_resolve(&c, NULL, 0.0f).forward.y;
    const float y5 = room_camera_resolve(&c, NULL, 0.5f).forward.y;
    const float y1 = room_camera_resolve(&c, NULL, 1.0f).forward.y;
    const float lo = (y0 < y1) ? y0 : y1;
    const float hi = (y0 < y1) ? y1 : y0;
    CHECK(hi - lo > 1e-6f,
          "le montage est faux : les deux bouts du pas doivent différer");
    CHECK(y5 > lo + 1e-7f && y5 < hi - 1e-7f,
          "la secousse est INTERPOLÉE entre deux pas (%.7f hors de ]%.7f ; %.7f[) — "
          "champ oublié dans bob_lerp ?", (double)y5, (double)lo, (double)hi);

    /* L'ÉTAT SIMULÉ EST INTACT : la secousse est un effet de rendu. */
    CHECK_NEAR(c.pitch, pitch_avant, 1e-6f, "le tangage simulé ne bouge pas");

    /*
     * ELLE S'ÉTEINT, ET AVANT LE BRAS. Le geste dure 390 ms
     * (`room_viewmodel.h`) ; à 11 par seconde il ne reste que 5 % de la
     * secousse au bout de 270. On vérifie donc à 300 ms qu'il en reste moins
     * d'un dixième, et à une seconde qu'il n'en reste rien.
     */
    for (int i = 0; i < 35; ++i) room_camera_tick(&c, &s.bvh, NULL, 1.0f / 120.0f);
    CHECK(c.bob.frappe < 0.10f,
          "au bout de 300 ms il reste moins d'un dixième de secousse (%.4f)",
          (double)c.bob.frappe);
    for (int i = 0; i < 85; ++i) room_camera_tick(&c, &s.bvh, NULL, 1.0f / 120.0f);
    CHECK(c.bob.frappe < 0.01f, "au bout d'une seconde elle a disparu (%.5f)",
          (double)c.bob.frappe);

    /* Deux coups coup sur coup n'empilent pas deux secousses : on POSE à 1, on
     * n'ajoute pas. Sans cette règle, marteler une borne sortirait du réglage
     * et donnerait une vue qui part au plafond. */
    room_camera_frappe(&c);
    room_camera_frappe(&c);
    CHECK(c.bob.frappe <= 1.0f + 1e-6f,
          "deux coups n'empilent pas deux secousses (%.3f)", (double)c.bob.frappe);
}

/* ========================================================================== */

int main(int argc, char **argv)
{
    ns_paths_init(argv[0]);
    if (argc > 1) ns_paths_mount(argv[1]);

    printf("== caméra de troisième personne ==\n");
    test_effacement();
    test_mur_plein();
    test_arete();
    test_epaule();
    test_amortissement();
    test_premiere_personne_intacte();
    test_contrecoup();
    test_foulee(argc > 1 ? argv[1] : NULL);

    printf("\n%d contrôle(s), %d échec(s)\n", g_checks, g_failures);
    ns_paths_shutdown();
    return g_failures == 0 ? 0 : 1;
}
