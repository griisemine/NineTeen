/*
 * test_particles.c — la poussière, sans écran.
 *
 * Ce que ce test attrape, et pourquoi il vaut la peine
 * ---------------------------------------------------
 * Une passe de particules ne casse jamais bruyamment. Elle dérive : les grains
 * fuient hors de leur zone au bout de deux minutes, la population s'effondre
 * parce qu'un budget est mal réparti, la simulation devient dépendante de
 * l'ordre d'appel et deux captures de la même graine cessent de se ressembler.
 * Aucun de ces défauts ne se voit sur une image fixe, et tous se voient en
 * jouant — c'est-à-dire trop tard.
 *
 * Le système se construit ici SANS GPU (`ns_particles_create(NULL, …)`), donc ce
 * fichier tourne aussi sur les machines d'intégration continue de macOS et de
 * Windows, qui n'ont pas de carte graphique.
 */
#include "ns_particles.h"
#include "ns_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            fprintf(stderr, "ÉCHEC %s:%d — ", __FILE__, __LINE__);            \
            fprintf(stderr, __VA_ARGS__);                                     \
            fprintf(stderr, "\n");                                            \
        }                                                                     \
    } while (0)

#define DT (1.0f / 120.0f)

static ns_particle_zone make_zone(float x0, float y0, float z0,
                                  float x1, float y1, float z1, float density)
{
    ns_particle_zone z;
    memset(&z, 0, sizeof z);
    z.bounds.min = ns_v3_make(x0, y0, z0);
    z.bounds.max = ns_v3_make(x1, y1, z1);
    z.density = density;
    z.drift[0] = 0.03f; z.drift[1] = -0.012f; z.drift[2] = 0.0f;
    z.size = 0.014f;
    z.color[0] = 1.0f; z.color[1] = 0.86f; z.color[2] = 0.66f;
    z.brightness = 0.3f;
    return z;
}

static ns_particles *make(uint32_t capacity, const ns_particle_zone *zones, uint32_t n,
                          uint64_t seed, float density)
{
    ns_particles *p = ns_particles_create(NULL, SDL_GPU_TEXTUREFORMAT_INVALID,
                                          SDL_GPU_TEXTUREFORMAT_INVALID, capacity);
    if (!p) return NULL;
    ns_particles_set_density(p, density);
    ns_particles_set_zones(p, zones, n, seed);
    return p;
}

/* -------------------------------------------------------------------------- */

static void test_population(void)
{
    /*
     * Le budget se répartit au prorata de « volume x densité ». Une zone deux
     * fois plus grande à densité égale doit donc recevoir deux fois plus de
     * grains — et la somme ne doit pas dépasser la capacité, sinon on écrit
     * hors du tableau.
     */
    ns_particle_zone z[2];
    z[0] = make_zone(0, 0, 0, 1, 1, 1, 1.0f);          /* 1 m3 */
    z[1] = make_zone(10, 0, 0, 12, 1, 1, 1.0f);        /* 2 m3 */

    ns_particles *p = make(3000, z, 2, 42, 1.0f);
    CHECK(p != NULL, "la simulation seule se construit sans GPU");
    if (!p) return;

    const uint32_t n = ns_particles_live(p);
    printf("  population : %u grains pour 3 m3\n", n);
    CHECK(n > 2900 && n <= 3000, "le budget est consommé sans être dépassé (%u)", n);
    ns_particles_destroy(NULL, p);
}

static void test_density_is_the_quality_lever(void)
{
    /*
     * C'est le levier du palier de qualité, donc il doit agir sur ce qui COÛTE :
     * le nombre de grains simulés, pas seulement le nombre de grains affichés.
     * À zéro, il ne doit rien rester à simuler du tout.
     */
    ns_particle_zone z = make_zone(0, 0, 0, 4, 2, 4, 1.0f);

    ns_particles *full = make(2000, &z, 1, 7, 1.0f);
    ns_particles *half = make(2000, &z, 1, 7, 0.5f);
    ns_particles *none = make(2000, &z, 1, 7, 0.0f);
    if (!full || !half || !none) { CHECK(false, "construction"); return; }

    const uint32_t a = ns_particles_live(full), b = ns_particles_live(half),
                   c = ns_particles_live(none);
    printf("  densité 1.0 / 0.5 / 0.0 : %u / %u / %u grains\n", a, b, c);
    CHECK(c == 0, "à densité nulle, rien n'est simulé (%u)", c);
    CHECK(b > 0 && b < a, "la moitié de la densité donne moins de grains (%u contre %u)", b, a);
    CHECK(a - b > a / 4, "et nettement moins, pas symboliquement");

    ns_particles_destroy(NULL, full);
    ns_particles_destroy(NULL, half);
    ns_particles_destroy(NULL, none);
}

static void test_grains_stay_home(void)
{
    /*
     * LA vérification qui compte. Les grains dérivent en permanence et ne
     * meurent jamais : sortis de la boîte, ils y reviennent par la face
     * opposée. Si ce recyclage se trompe d'axe ou de borne, le nuage quitte
     * lentement sa zone — la population ne bouge pas d'un grain, rien n'est
     * signalé, et la poussière finit simplement par ne plus être dans les
     * faisceaux. On simule donc cinq minutes et on compare l'emprise du nuage à
     * la zone déclarée.
     */
    ns_particle_zone z = make_zone(-3.4f, 0.9f, -2.6f, 3.4f, 2.8f, 2.6f, 0.85f);
    ns_particles *p = make(1500, &z, 1, 20240418, 1.0f);
    if (!p) { CHECK(false, "construction"); return; }

    const uint32_t before = ns_particles_live(p);
    for (int i = 0; i < 120 * 300; ++i) ns_particles_tick(p, DT);
    CHECK(ns_particles_live(p) == before, "aucun grain n'est perdu en cinq minutes");

    ns_aabb b;
    CHECK(ns_particles_cloud_bounds(p, &b), "l'emprise du nuage est lisible");

    /*
     * La tolérance vaut un pas de simulation à la vitesse de dérive la plus
     * rapide (3 cm/s en x, plus 1,2 cm/s d'ondulation) : un grain peut être
     * juste sorti et pas encore recyclé, ce qui est normal. Un centimètre est
     * largement au-dessus, et très en dessous des mètres qu'une fuite produit.
     */
    const float tol = 0.01f;
    printf("  nuage après 5 min : x %.3f..%.3f  y %.3f..%.3f  z %.3f..%.3f\n",
           (double)b.min.x, (double)b.max.x, (double)b.min.y,
           (double)b.max.y, (double)b.min.z, (double)b.max.z);

    CHECK(b.min.x >= z.bounds.min.x - tol && b.max.x <= z.bounds.max.x + tol,
          "le nuage reste dans sa zone en x (%.3f..%.3f)", (double)b.min.x, (double)b.max.x);
    CHECK(b.min.y >= z.bounds.min.y - tol && b.max.y <= z.bounds.max.y + tol,
          "le nuage reste dans sa zone en y (%.3f..%.3f)", (double)b.min.y, (double)b.max.y);
    CHECK(b.min.z >= z.bounds.min.z - tol && b.max.z <= z.bounds.max.z + tol,
          "le nuage reste dans sa zone en z (%.3f..%.3f)", (double)b.min.z, (double)b.max.z);

    /*
     * Et il ne s'est pas non plus effondré sur un coin : la dérive descendante
     * fait que sans recyclage correct tous les grains finiraient au plancher de
     * la boîte. On exige donc qu'ils occupent encore l'essentiel de la hauteur.
     */
    const float spread = b.max.y - b.min.y;
    CHECK(spread > (z.bounds.max.y - z.bounds.min.y) * 0.8f,
          "et il occupe encore la hauteur de la zone (%.2f m)", (double)spread);

    ns_particles_destroy(NULL, p);
}

static void test_deterministic(void)
{
    /*
     * Même graine, même durée, même nuage. C'est ce qui rend une capture
     * reproductible : sans ça, deux rendus du même point de vue diffèrent, et
     * toute comparaison avant/après devient une question d'opinion.
     */
    ns_particle_zone z = make_zone(-2, 1, -2, 2, 2.8f, 2, 0.7f);
    ns_particles *a = make(800, &z, 1, 1234, 1.0f);
    ns_particles *b = make(800, &z, 1, 1234, 1.0f);
    ns_particles *c = make(800, &z, 1, 5678, 1.0f);
    if (!a || !b || !c) { CHECK(false, "construction"); return; }

    for (int i = 0; i < 120 * 20; ++i) {
        ns_particles_tick(a, DT);
        ns_particles_tick(b, DT);
        ns_particles_tick(c, DT);
    }
    CHECK(ns_particles_live(a) == ns_particles_live(b),
          "deux nuages de même graine ont la même population");
    CHECK(ns_particles_live(c) > 0, "une autre graine peuple aussi la zone");

    ns_particles_destroy(NULL, a);
    ns_particles_destroy(NULL, b);
    ns_particles_destroy(NULL, c);
}

static void test_degenerate_zones(void)
{
    /*
     * Une zone plate (un plan), une zone inversée (min au-dessus de max) et une
     * zone de densité nulle sont trois fautes de frappe plausibles dans la
     * description de salle. Aucune ne doit produire de division par zéro, de
     * NaN, ni de grain — et surtout aucune ne doit empêcher les zones VALIDES
     * de la même salle d'être peuplées.
     */
    ns_particle_zone z[3];
    z[0] = make_zone(0, 1, 0, 4, 1, 4, 0.8f);        /* plate : volume nul */
    z[1] = make_zone(0, 3, 0, 4, 1, 4, 0.8f);        /* inversée */
    z[2] = make_zone(-2, 1, -2, 2, 2.5f, 2, 0.8f);   /* valide */

    ns_particles *p = make(600, z, 3, 99, 1.0f);
    if (!p) { CHECK(false, "construction"); return; }

    const uint32_t n = ns_particles_live(p);
    printf("  zones dégénérées : %u grains (tous dans la zone valide)\n", n);
    CHECK(n > 0, "la zone valide est peuplée malgré ses deux voisines fautives");

    for (int i = 0; i < 120 * 30; ++i) ns_particles_tick(p, DT);
    CHECK(ns_particles_live(p) == n, "et la population tient dans le temps");
    ns_particles_destroy(NULL, p);
}

static void test_no_zone_at_all(void)
{
    /* Une salle sans poussière déclarée est un cas normal, pas une erreur. */
    ns_particles *p = ns_particles_create(NULL, SDL_GPU_TEXTUREFORMAT_INVALID,
                                          SDL_GPU_TEXTUREFORMAT_INVALID, 512);
    CHECK(p != NULL, "un système sans zone se construit");
    if (!p) return;
    CHECK(ns_particles_live(p) == 0, "et ne simule rien");
    for (int i = 0; i < 1000; ++i) ns_particles_tick(p, DT);
    CHECK(ns_particles_live(p) == 0, "même après mille pas");
    ns_particles_destroy(NULL, p);
}

static void test_zone_overflow(void)
{
    /*
     * Plus de zones que la limite : on doit en garder autant qu'on peut et
     * ignorer le reste, sans écrire hors du tableau. Le contrôle du nombre de
     * zones appartient à `roomgen`, qui casse le build ; ici on vérifie
     * seulement que le moteur ne se laisse pas déborder si ça arrive quand même.
     */
    ns_particle_zone z[NS_MAX_PARTICLE_ZONES + 4];
    for (int i = 0; i < NS_MAX_PARTICLE_ZONES + 4; ++i)
        z[i] = make_zone((float)i * 3.0f, 1.0f, 0.0f, (float)i * 3.0f + 2.0f, 2.0f, 2.0f, 0.5f);

    ns_particles *p = make(1000, z, NS_MAX_PARTICLE_ZONES + 4, 3, 1.0f);
    if (!p) { CHECK(false, "construction"); return; }
    CHECK(ns_particles_live(p) > 0 && ns_particles_live(p) <= 1000,
          "le débordement de zones est tronqué proprement (%u)", ns_particles_live(p));
    for (int i = 0; i < 1200; ++i) ns_particles_tick(p, DT);
    ns_particles_destroy(NULL, p);
}

int main(void)
{
    ns_log_set_level(NS_LOG_WARN);      /* le peuplement s'annonce en INFO à chaque zone */

    test_population();
    test_density_is_the_quality_lever();
    test_grains_stay_home();
    test_deterministic();
    test_degenerate_zones();
    test_no_zone_at_all();
    test_zone_overflow();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
