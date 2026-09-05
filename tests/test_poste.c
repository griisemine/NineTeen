/*
 * test_poste.c — LE POSTE DE JEU : ce qui se cloue, et ce qui ne se cloue pas.
 *
 * Ce que ce test attrape
 * ----------------------
 * Le défaut d'origine est VISIBLE — on jouait sur 8,4 % du cadre — et un test
 * ne saura jamais qu'une image est trop petite. Mais tout ce qui produit ce
 * cadrage est du CALCUL PUR : une hauteur de dalle, une part voulue, un champ,
 * une distance, un tangage, une rampe. Ça, on peut le clouer.
 *
 * Quatre familles, et chacune correspond à une façon dont le poste peut casser
 * sans qu'on s'en aperçoive à l'œil :
 *
 *   1. LA DISTANCE EST UNE RÈGLE, pas un nombre. Le test refait la dérivation
 *      — d = (h/2) / tan(part * champ / 2) — et exige que le code tombe
 *      dessus. Si quelqu'un remplace la formule par la valeur qu'elle donnait,
 *      une borne à plus grande dalle se cadrera mal en silence.
 *
 *   2. LE TANGAGE VIENT DU POSTE et non du corps. C'est le piège qui avait déjà
 *      pris `--play-at` : viser depuis l'œil du corps donne un cadrage juste
 *      pendant une demi-seconde, puis faux. Les deux angles diffèrent de onze
 *      degrés sur la borne de référence, et le test les compare.
 *
 *   3. LES REFUS. Une salle sans cotes de dalle (celle de 2020), une caméra
 *      déjà plus près que le poste. Dans les deux cas le poste doit refuser
 *      SANS RIEN CHANGER — un poste qui recule au démarrage d'une partie serait
 *      pire que celui qu'on corrige.
 *
 *   4. LA RAMPE. Monotone, bornée, et de dérivée nulle aux deux bouts. C'est
 *      cette dernière propriété qui distingue un geste d'une interpolation : une
 *      rampe linéaire fait sauter la vitesse de zéro à sa valeur, et l'œil le
 *      voit comme un à-coup au départ et un autre à l'arrivée.
 *
 * Ce qu'il NE vérifie pas, et il faut le dire : que le cadrage soit beau. La
 * part livrée (0,58) a été choisie sur capture, en comparant cinq réglages ; le
 * test ne fait que garantir que le code produit bien celui qui est déclaré.
 *
 * Ni GPU, ni fenêtre, ni salle : les bornes sont construites à la main avec les
 * cotes réelles de `borne_arcade_1`, relevées dans `salle.scene.json`.
 */
#include "ns_core.h"
#include "ns_math.h"
#include "ns_scene.h"
#include "room_poste.h"

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

#define CHECK_NEAR(got, want, tol, label)                                     \
    CHECK(fabsf((got) - (want)) <= (tol),                                     \
          "%s : %.4f, attendu %.4f (± %.4f)", label,                          \
          (double)(got), (double)(want), (double)(tol))

/* ==========================================================================
 * La borne de référence
 * ==========================================================================
 * Les cotes de `borne_arcade_1` telles que `roomgen` les écrit dans
 * `salle.scene.json`. Recopiées plutôt que chargées : le test doit tourner sans
 * assets, et surtout ces valeurs sont ce que le commentaire de `room_poste.h`
 * annonce. Si la borne change, la table du commentaire est fausse et ce test
 * doit le dire.
 */
static ns_cabinet borne_reference(void)
{
    ns_cabinet c;
    memset(&c, 0, sizeof c);
    snprintf(c.name, sizeof c.name, "borne_arcade_1");
    c.screen_center = ns_v3_make(-3.186f, 1.2521f, 1.3701f);
    c.screen_normal = ns_v3_make(0.0f, 0.3399f, 0.9405f);
    c.screen_width  = 0.5333f;
    c.screen_height = 0.3000f;
    c.player_anchor = ns_v3_make(-3.186f, 0.0f, 2.1156f);
    c.stick_top     = ns_v3_make(-3.271f, 1.072f, 1.656f);
    return c;
}

/* L'œil du CORPS debout sur l'ancre déclarée. */
static ns_v3 oeil_du_corps(const ns_cabinet *c)
{
    return ns_v3_make(c->player_anchor.x, 1.70f, c->player_anchor.z);
}

/* Les valeurs LIVRÉES, celles que `nineteen.env` déclare et que la table de
 * `room_poste.h` a servi à choisir. Elles sont ici en dur exprès : si quelqu'un
 * change le défaut sans rouvrir la table, le test le dit. */
#define POSTE_PART   0.58f
#define POSTE_CHAMP  46.0f

/* ==========================================================================
 * 1. La distance est une règle
 * ========================================================================== */
static void test_distance(void)
{
    printf("\n-- la distance suit la formule, pas un nombre --\n");
    const ns_cabinet c = borne_reference();
    room_poste p;
    room_poste_init(&p);
    CHECK(room_poste_prendre(&p, &c, oeil_du_corps(&c)), "le poste doit être pris");

    /* La dérivation, refaite à la main. */
    const float attendu = (0.5f * c.screen_height)
                        / tanf(0.5f * POSTE_PART * POSTE_CHAMP * NS_DEG2RAD);
    const float obtenu = ns_v3_len(ns_v3_sub(p.oeil, c.screen_center));
    CHECK_NEAR(obtenu, attendu, 1e-3f, "distance du poste au centre de la dalle");
    CHECK_NEAR(obtenu, 0.6331f, 2e-3f, "la distance annoncée par room_poste.h");

    /* L'œil est SUR la normale : le poste regarde la dalle de face, sinon la
     * couverture calculée ne veut rien dire. Le produit vectoriel du décalage
     * par la normale doit être nul. */
    const ns_v3 d = ns_v3_sub(p.oeil, c.screen_center);
    const ns_v3 croix = ns_v3_cross(ns_v3_norm(d), ns_v3_norm(c.screen_normal));
    CHECK(ns_v3_len(croix) < 1e-3f,
          "l'œil du poste doit être sur la normale de la dalle (écart %.5f)",
          (double)ns_v3_len(croix));

    /* Et il AVANCE : le poste est un rapprochement. */
    const float avant = ns_v3_len(ns_v3_sub(oeil_du_corps(&c), c.screen_center));
    CHECK(obtenu < avant, "le poste doit rapprocher (%.3f m contre %.3f m)",
          (double)obtenu, (double)avant);
    CHECK_NEAR(avant - obtenu, 0.2369f, 3e-3f, "l'avancée annoncée");

    /*
     * UNE DALLE PLUS GRANDE RECULE LE POSTE, et c'est tout l'intérêt d'avoir
     * écrit une règle : une borne à écran de 24 pouces ne se règle pas à la
     * main. Le rapport des distances doit être exactement celui des hauteurs.
     */
    ns_cabinet grande = c;
    grande.screen_height = 0.600f;
    grande.screen_width  = 1.0666f;
    room_poste q;
    room_poste_init(&q);
    CHECK(room_poste_prendre(&q, &grande, ns_v3_make(-3.186f, 1.70f, 4.0f)),
          "le poste doit être pris devant une grande dalle");
    const float d2 = ns_v3_len(ns_v3_sub(q.oeil, grande.screen_center));
    CHECK_NEAR(d2 / obtenu, 2.0f, 1e-3f,
               "doubler la hauteur de dalle doit doubler la distance");
}

/* ==========================================================================
 * 2. Le tangage vient du poste
 * ========================================================================== */
static void test_tangage(void)
{
    printf("\n-- le tangage se prend DEPUIS le poste --\n");
    const ns_cabinet c = borne_reference();
    room_poste p;
    room_poste_init(&p);
    CHECK(room_poste_prendre(&p, &c, oeil_du_corps(&c)), "le poste doit être pris");

    /* Depuis le poste, regarder le centre de la dalle c'est regarder à −n. */
    const float attendu = -asinf(ns_v3_norm(c.screen_normal).y);
    CHECK_NEAR(room_poste_tangage(&p), attendu, 1e-4f, "tangage du poste (radians)");
    CHECK_NEAR(room_poste_tangage(&p) / NS_DEG2RAD, -19.87f, 0.05f,
               "tangage du poste (degrés)");

    /*
     * LE PIÈGE, cloué : le tangage vu du CORPS n'est pas le même. Poser
     * celui-là puis avancer vers le poste fait dériver la dalle vers le haut du
     * cadre pendant toute la transition — c'est-à-dire précisément pendant
     * qu'on la regarde.
     */
    const ns_v3 oeil = oeil_du_corps(&c);
    const float dy = c.screen_center.y - oeil.y;
    const float plat = sqrtf((c.screen_center.x - oeil.x) * (c.screen_center.x - oeil.x)
                           + (c.screen_center.z - oeil.z) * (c.screen_center.z - oeil.z));
    const float depuis_corps = atan2f(dy, plat);
    CHECK_NEAR(depuis_corps / NS_DEG2RAD, -31.00f, 0.05f, "tangage depuis le corps");
    CHECK(fabsf(depuis_corps - room_poste_tangage(&p)) > 8.0f * NS_DEG2RAD,
          "les deux tangages doivent différer nettement : %.2f contre %.2f degrés",
          (double)(depuis_corps / NS_DEG2RAD),
          (double)(room_poste_tangage(&p) / NS_DEG2RAD));
}

/* ==========================================================================
 * 3. Les refus
 * ========================================================================== */
static void test_refus(void)
{
    printf("\n-- ce que le poste refuse, et sans rien changer --\n");
    const ns_cabinet c = borne_reference();

    /* a. La salle de 2020 ne déclare ni cotes de dalle ni normale. */
    ns_cabinet muette = c;
    muette.screen_height = 0.0f;
    room_poste p;
    room_poste_init(&p);
    CHECK(!room_poste_prendre(&p, &muette, oeil_du_corps(&c)),
          "une borne sans hauteur de dalle ne doit pas donner de poste");
    CHECK(!p.pris, "un refus ne doit pas marquer le poste comme pris");

    ns_cabinet sans_normale = c;
    sans_normale.screen_normal = ns_v3_make(0.0f, 0.0f, 0.0f);
    CHECK(!room_poste_prendre(&p, &sans_normale, oeil_du_corps(&c)),
          "une borne sans normale ne doit pas donner de poste");

    /* b. NULL des deux côtés : le jeu tourne sans salle dans les tests. */
    CHECK(!room_poste_prendre(&p, NULL, oeil_du_corps(&c)), "borne NULL refusée");
    CHECK(!room_poste_prendre(NULL, &c, oeil_du_corps(&c)), "poste NULL refusé");

    /*
     * c. ON N'AVANCE JAMAIS EN RECULANT. `--pos=` place la caméra où l'on veut ;
     * une vue qui RECULE au démarrage d'une partie serait un défaut plus visible
     * que celui qu'on corrige.
     */
    const ns_v3 colle = ns_v3_add(c.screen_center,
                                  ns_v3_scale(ns_v3_norm(c.screen_normal), 0.30f));
    CHECK(!room_poste_prendre(&p, &c, colle),
          "un œil déjà plus près que le poste ne doit pas le faire reculer");

    /* d. Et le cas limite juste au-delà : là, il doit accepter. */
    const ns_v3 juste_derriere = ns_v3_add(c.screen_center,
                                           ns_v3_scale(ns_v3_norm(c.screen_normal), 0.70f));
    CHECK(room_poste_prendre(&p, &c, juste_derriere),
          "un œil à 0,70 m est plus loin que le poste (0,633 m) : il doit avancer");
}

/* ==========================================================================
 * 4. La rampe
 * ========================================================================== */
static void test_rampe(void)
{
    printf("\n-- la rampe : monotone, bornée, sans à-coup aux deux bouts --\n");
    const ns_cabinet c = borne_reference();
    const ns_v3 oeil = oeil_du_corps(&c);
    room_poste p;
    room_poste_init(&p);

    CHECK_NEAR(room_poste_part(&p, 1.0f), 0.0f, 1e-6f, "part au repos");
    ns_v3 v = room_poste_oeil(&p, oeil, 1.0f);
    CHECK(ns_v3_dist(v, oeil) < 1e-6f, "au repos, l'œil est celui du corps");
    CHECK_NEAR(room_poste_fov(&p, 62.0f, 1.0f), 62.0f, 1e-4f, "au repos, le champ est celui de la salle");

    CHECK(room_poste_prendre(&p, &c, oeil), "le poste doit être pris");

    /* Établissement : 0,55 s au pas de 120 Hz. On échantillonne. */
    const float dt = 1.0f / 120.0f;
    float precedent = 0.0f;
    float vitesse_max = 0.0f;
    int pas = 0;
    for (; pas < 240; ++pas) {
        room_poste_tick(&p, dt);
        const float k = room_poste_part(&p, 1.0f);
        CHECK(k >= precedent - 1e-6f, "la rampe doit être monotone croissante (%.4f puis %.4f)",
              (double)precedent, (double)k);
        CHECK(k >= 0.0f && k <= 1.0f, "la rampe doit rester dans [0 ; 1] (%.4f)", (double)k);
        const float dv = (k - precedent) / dt;
        if (dv > vitesse_max) vitesse_max = dv;
        precedent = k;
        if (k >= 1.0f) break;
    }
    CHECK(precedent >= 0.999f, "la rampe doit atteindre 1 (arrivée à %.4f)", (double)precedent);
    /* 0,55 s à 120 Hz = 66 pas. On tolère l'arrondi du dernier. */
    CHECK(pas >= 64 && pas <= 68, "l'établissement doit prendre 0,55 s (%d pas de 1/120 s)", pas);

    /*
     * PAS D'À-COUP AUX DEUX BOUTS. Une rampe linéaire aurait une vitesse
     * constante de 1/0,55 = 1,82 par seconde du premier pas au dernier ; le
     * lissage cubique la fait partir de zéro, culminer à 1,5 fois la moyenne au
     * milieu, et retomber à zéro. C'est cette DÉRIVÉE NULLE aux extrémités qui
     * fait la différence entre un geste et une interpolation.
     */
    room_poste q;
    room_poste_init(&q);
    CHECK(room_poste_prendre(&q, &c, oeil), "poste pris pour la mesure de dérivée");
    room_poste_tick(&q, dt);
    const float v_debut = room_poste_part(&q, 1.0f) / dt;
    CHECK(v_debut < 0.25f * (1.0f / 0.55f),
          "la rampe doit DÉMARRER lentement : %.3f/s pour une moyenne de %.3f/s",
          (double)v_debut, (double)(1.0f / 0.55f));
    CHECK_NEAR(vitesse_max, 1.5f / 0.55f, 0.25f,
               "la vitesse de pointe du lissage cubique (1,5 fois la moyenne)");

    /* Aux deux extrémités, l'œil et le champ valent exactement leurs bornes. */
    v = room_poste_oeil(&p, oeil, 1.0f);
    CHECK(ns_v3_dist(v, p.oeil) < 1e-4f, "à part pleine, l'œil est celui du poste");
    CHECK_NEAR(room_poste_fov(&p, 62.0f, 1.0f), POSTE_CHAMP, 1e-3f,
               "à part pleine, le champ est celui du poste");

    /*
     * LE RETOUR EST PLUS RAPIDE QUE L'ALLER, et c'est voulu : on se penche vers
     * une machine plus lentement qu'on ne s'en redresse.
     */
    room_poste_lacher(&p);
    int retour = 0;
    for (; retour < 240; ++retour) {
        room_poste_tick(&p, dt);
        if (room_poste_part(&p, 1.0f) <= 0.0f) break;
    }
    CHECK(retour < pas, "le retour (%d pas) doit être plus court que l'aller (%d pas)",
          retour, pas);
    CHECK(retour >= 44 && retour <= 48, "le retour doit prendre 0,38 s (%d pas)", retour);
    v = room_poste_oeil(&p, oeil, 1.0f);
    CHECK(ns_v3_dist(v, oeil) < 1e-6f, "après le retour, l'œil est rendu au corps");
}

/* ==========================================================================
 * 5. Les deux pas, pour les bras
 * ========================================================================== */
static void test_deux_pas(void)
{
    printf("\n-- l'œil des bras : deux pas, jamais une interpolation de plus --\n");
    const ns_cabinet c = borne_reference();
    const ns_v3 oeil = oeil_du_corps(&c);
    room_poste p;
    room_poste_init(&p);
    CHECK(room_poste_prendre(&p, &c, oeil), "le poste doit être pris");

    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < 20; ++i) room_poste_tick(&p, dt);

    /*
     * Le contrat : `room_poste_oeil_pas(..., courant)` doit rendre EXACTEMENT ce
     * que `room_poste_oeil(..., alpha)` rend aux deux extrémités de
     * l'interpolation. C'est ce qui garantit que les bras et la vue décrivent la
     * même trajectoire — un décalage d'une image entre eux ferait flotter les
     * avant-bras devant le cadre.
     */
    const ns_v3 a0 = room_poste_oeil_pas(&p, oeil, false);
    const ns_v3 a1 = room_poste_oeil_pas(&p, oeil, true);
    CHECK(ns_v3_dist(a0, room_poste_oeil(&p, oeil, 0.0f)) < 1e-5f,
          "l'œil du pas précédent doit valoir l'œil interpolé à alpha 0");
    CHECK(ns_v3_dist(a1, room_poste_oeil(&p, oeil, 1.0f)) < 1e-5f,
          "l'œil du pas courant doit valoir l'œil interpolé à alpha 1");
    CHECK(ns_v3_dist(a0, a1) > 1e-4f,
          "les deux pas doivent différer pendant l'établissement (sinon rien n'avance)");
}

/* ========================================================================== */

int main(int argc, char **argv)
{
    ns_paths_init(argv[0]);
    if (argc > 1) ns_paths_mount(argv[1]);

    printf("== le poste de jeu ==\n");
    test_distance();
    test_tangage();
    test_refus();
    test_rampe();
    test_deux_pas();

    printf("\n%d contrôle(s), %d échec(s)\n", g_checks, g_failures);
    ns_paths_shutdown();
    return g_failures == 0 ? 0 : 1;
}
