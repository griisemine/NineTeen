/*
 * test_menu.c — le menu de réglages, sans écran.
 *
 * Ce qui mérite un test ici n'est pas le dessin, c'est le COMPORTEMENT :
 * un menu qui écrase un réglage voisin, qui laisse une valeur sortir de ses
 * bornes ou qui n'écrit pas ce qu'il affiche est un menu dont on se méfie —
 * et un joueur qui s'en méfie ne s'en sert plus.
 *
 * Le piège précis qu'on vérifie : changer de palier de qualité reprend TOUS les
 * défauts du palier. C'est voulu — c'est ce qui rend un palier prévisible — mais
 * les trois réglages qui ont leur propre ligne dans le menu (échelle, poussière,
 * luminosité) doivent survivre. Sans ça, régler la poussière puis changer de
 * palier l'effacerait en silence.
 */
#include "ns_config.h"
#include "ns_core.h"
#include "ns_render.h"
#include "room_menu.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            printf("ÉCHEC %s:%d — ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* Le nombre d'entrées est privé au .c : on le retrouve en tournant. */
static int menu_item_count(void)
{
    room_menu m; memset(&m, 0, sizeof m);
    ns_render_settings rs; ns_render_settings_defaults(&rs, NS_QUALITY_MEDIUM);
    float sens = 1.0f;
    const room_menu_ctx ctx = { &rs, &sens, NULL };
    room_menu_open(&m);
    for (int i = 1; i <= 64; ++i) {
        room_menu_input(&m, &ctx, ROOM_MENU_DOWN);
        if (m.cursor == 0) return i;
    }
    return -1;
}

static void go_to(room_menu *m, const room_menu_ctx *ctx, int row)
{
    m->cursor = 0;
    for (int i = 0; i < row; ++i) room_menu_input(m, ctx, ROOM_MENU_DOWN);
}

static void test_navigation(void)
{
    const int n = menu_item_count();
    CHECK(n > 4, "le menu a des entrées (%d)", n);

    room_menu m; memset(&m, 0, sizeof m);
    ns_render_settings rs; ns_render_settings_defaults(&rs, NS_QUALITY_MEDIUM);
    float sens = 1.0f;
    const room_menu_ctx ctx = { &rs, &sens, NULL };

    /* Fermé, le menu ignore tout : une touche pressée pendant la partie ne doit
     * pas déplacer un curseur invisible. */
    m.cursor = 0;
    room_menu_input(&m, &ctx, ROOM_MENU_DOWN);
    CHECK(m.cursor == 0, "un menu fermé n'écoute pas (%d)", m.cursor);

    room_menu_open(&m);
    CHECK(m.open, "le menu s'ouvre");
    room_menu_input(&m, &ctx, ROOM_MENU_UP);
    CHECK(m.cursor == n - 1, "vers le haut depuis la première ligne, on boucle en bas (%d)",
          m.cursor);
    room_menu_input(&m, &ctx, ROOM_MENU_DOWN);
    CHECK(m.cursor == 0, "et retour en haut (%d)", m.cursor);

    room_menu_close(&m);
    CHECK(!m.open, "le menu se ferme");
}

static void test_quality_preserves_the_other_rows(void)
{
    room_menu m; memset(&m, 0, sizeof m);
    ns_render_settings rs; ns_render_settings_defaults(&rs, NS_QUALITY_MEDIUM);
    float sens = 1.0f;
    const room_menu_ctx ctx = { &rs, &sens, NULL };
    room_menu_open(&m);

    /* Ligne 1 : l'échelle. Deux crans vers le bas. */
    go_to(&m, &ctx, 1);
    room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
    room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
    const float scale = rs.render_scale;
    CHECK(scale > 0.89f && scale < 0.91f, "l'échelle descend par crans de 0,05 (%.3f)",
          (double)scale);
    CHECK(m.render_dirty, "…et demande une application");

    /* Ligne 2 : la poussière, mise à zéro. */
    go_to(&m, &ctx, 2);
    for (int i = 0; i < 30; ++i) room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
    CHECK(rs.particle_density == 0.0f, "la poussière se coupe et ne passe pas sous zéro (%.3f)",
          (double)rs.particle_density);

    /* Ligne 0 : le palier. C'est LE test. */
    go_to(&m, &ctx, 0);
    const ns_quality before = rs.quality;
    room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    CHECK(rs.quality != before, "le palier change (%d -> %d)", (int)before, (int)rs.quality);
    CHECK(rs.render_scale == scale,
          "changer de palier NE touche PAS l'échelle de rendu (%.3f, attendu %.3f)",
          (double)rs.render_scale, (double)scale);
    CHECK(rs.particle_density == 0.0f,
          "…ni la poussière qu'on venait de couper (%.3f)", (double)rs.particle_density);

    /* Le palier boucle dans les deux sens et ne sort jamais de l'énumération. */
    go_to(&m, &ctx, 0);
    for (int i = 0; i < 23; ++i) {
        room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
        if (rs.quality < NS_QUALITY_POTATO || rs.quality > NS_QUALITY_ULTRA) break;
    }
    CHECK(rs.quality >= NS_QUALITY_POTATO && rs.quality <= NS_QUALITY_ULTRA,
          "le palier reste dans l'énumération en montant (%d)", (int)rs.quality);
    for (int i = 0; i < 23; ++i) {
        room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
        if (rs.quality < NS_QUALITY_POTATO || rs.quality > NS_QUALITY_ULTRA) break;
    }
    CHECK(rs.quality >= NS_QUALITY_POTATO && rs.quality <= NS_QUALITY_ULTRA,
          "…et en descendant (%d)", (int)rs.quality);
}

static void test_bounds(void)
{
    room_menu m; memset(&m, 0, sizeof m);
    ns_render_settings rs; ns_render_settings_defaults(&rs, NS_QUALITY_MEDIUM);
    float sens = 1.0f;
    const room_menu_ctx ctx = { &rs, &sens, NULL };
    room_menu_open(&m);

    go_to(&m, &ctx, 1);
    for (int i = 0; i < 40; ++i) room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
    CHECK(rs.render_scale >= 0.499f, "l'échelle ne descend pas sous 0,50 (%.3f)",
          (double)rs.render_scale);
    for (int i = 0; i < 40; ++i) room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    CHECK(rs.render_scale <= 1.001f, "…et ne monte pas au-dessus de 1,00 (%.3f)",
          (double)rs.render_scale);

    /*
     * La sensibilité de la souris.
     *
     * Elle est repérée par sa POSITION depuis la fin : le menu se termine par
     * TEMPS RÉEL, REPRENDRE et QUITTER, donc la souris est la quatrième en
     * partant du bas. Ce repérage est fragile — il l'était déjà, et une ligne
     * insérée avant les boutons vient de le montrer : le test réglait le temps
     * réel en croyant régler la souris, et échouait sur une borne qui n'était
     * pas la sienne.
     */
    const int n = menu_item_count();
    go_to(&m, &ctx, n - 4);
    for (int i = 0; i < 100; ++i) room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
    CHECK(sens >= 0.19f && sens <= 0.21f, "la sensibilité se borne en bas (%.3f)", (double)sens);
    for (int i = 0; i < 100; ++i) room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    CHECK(sens >= 2.99f && sens <= 3.01f, "…et en haut (%.3f)", (double)sens);
}

static void test_buttons(void)
{
    room_menu m; memset(&m, 0, sizeof m);
    ns_render_settings rs; ns_render_settings_defaults(&rs, NS_QUALITY_MEDIUM);
    float sens = 1.0f;
    const room_menu_ctx ctx = { &rs, &sens, NULL };
    const int n = menu_item_count();

    room_menu_open(&m);
    go_to(&m, &ctx, n - 2);            /* REPRENDRE */
    room_menu_input(&m, &ctx, ROOM_MENU_ACCEPT);
    CHECK(m.close_request, "« Reprendre » demande la fermeture");
    CHECK(!m.quit_request, "…et ne quitte pas le jeu");

    /* Les flèches sur un bouton ne doivent RIEN faire : sans cette garde,
     * « Quitter » se comporterait comme un réglage et changerait le voisin. */
    memset(&m, 0, sizeof m);
    room_menu_open(&m);
    go_to(&m, &ctx, n - 1);            /* QUITTER */
    const float keep = rs.render_scale;
    room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
    room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    CHECK(rs.render_scale == keep && !m.render_dirty,
          "les flèches sur un bouton sont inertes (%.3f)", (double)rs.render_scale);
    room_menu_input(&m, &ctx, ROOM_MENU_ACCEPT);
    CHECK(m.quit_request, "« Quitter » le demande");

    memset(&m, 0, sizeof m);
    room_menu_open(&m);
    room_menu_input(&m, &ctx, ROOM_MENU_CANCEL);
    CHECK(m.close_request, "Échap ferme");
}

static void test_persist(const char *dir)
{
    ns_render_settings rs; ns_render_settings_defaults(&rs, NS_QUALITY_HIGH);
    rs.render_scale = 0.75f;
    float sens = 1.85f;
    const room_menu_ctx ctx = { &rs, &sens, NULL };

    ns_config_init("menu-test.cfg");
    room_menu_persist(&ctx);
    CHECK(ns_config_save(), "la configuration s'écrit");

    /* On relit ce qui a été posé : c'est la seule façon de savoir que le menu
     * écrit bien ce qu'il affiche, et pas seulement qu'il ne plante pas. */
    CHECK(SDL_strcmp(ns_config_get_str(NS_CFG_QUALITY, "?"), "high") == 0,
          "le palier est écrit (%s)", ns_config_get_str(NS_CFG_QUALITY, "?"));
    CHECK(ns_config_get_float(NS_CFG_RENDER_SCALE, 0.0f) > 0.74f
          && ns_config_get_float(NS_CFG_RENDER_SCALE, 0.0f) < 0.76f,
          "l'échelle est écrite (%.3f)", (double)ns_config_get_float(NS_CFG_RENDER_SCALE, 0.0f));
    CHECK(ns_config_get_float(NS_CFG_MOUSE_SENS, 0.0f) > 1.84f
          && ns_config_get_float(NS_CFG_MOUSE_SENS, 0.0f) < 1.86f,
          "la sensibilité est écrite (%.3f)", (double)ns_config_get_float(NS_CFG_MOUSE_SENS, 0.0f));
    ns_config_shutdown();
    (void)dir;
}

int main(int argc, char **argv)
{
    ns_paths_init(argv[0]);
    test_navigation();
    test_quality_preserves_the_other_rows();
    test_bounds();
    test_buttons();
    test_persist(argc > 1 ? argv[1] : ".");
    ns_paths_shutdown();
    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
