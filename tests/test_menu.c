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
#include "room_credits.h"
#include "room_menu.h"
#include "room_sound.h"

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
    const room_menu_ctx ctx = { &rs, &sens, NULL, NULL, NULL, NULL, NULL, NULL };
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
    const room_menu_ctx ctx = { &rs, &sens, NULL, NULL, NULL, NULL, NULL, NULL };

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
    const room_menu_ctx ctx = { &rs, &sens, NULL, NULL, NULL, NULL, NULL, NULL };
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
    const room_menu_ctx ctx = { &rs, &sens, NULL, NULL, NULL, NULL, NULL, NULL };
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
     * Elle est visée par son LIBELLÉ. Elle l'était par sa position depuis la
     * fin, et une ligne « TEMPS REEL » insérée avant les boutons a montré ce
     * que ça vaut : le test réglait le temps réel en croyant régler la souris,
     * et se plaignait d'une valeur qui n'était pas la sienne.
     */
    const int row = room_menu_row("SOURIS");
    CHECK(row >= 0, "la ligne « SOURIS » existe");
    go_to(&m, &ctx, row);
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
    const room_menu_ctx ctx = { &rs, &sens, NULL, NULL, NULL, NULL, NULL, NULL };
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

/*
 * Les deux niveaux de la salle : « PAS » et « FOND DE SALLE ».
 *
 * Ils sont les seuls réglages sonores que ce test PEUT exercer, et ce n'est pas
 * une commodité : c'est une conséquence de leur nature. Les quatre volumes de
 * bus vivent dans le mixeur, qui n'existe pas sur une machine d'intégration
 * continue sans carte son — `bus_step` y est un no-op, et les vérifier
 * reviendrait à vérifier que rien ne se passe. Ces deux-là sont des préférences
 * de la salle, tenues par `room_sound` et écrites dans `settings.cfg` que le
 * mixeur ait démarré ou non. Ils se règlent, se bornent et se persistent sans
 * périphérique — donc ils se testent.
 */
static void test_room_levels(void)
{
    room_menu m; memset(&m, 0, sizeof m);
    ns_render_settings rs; ns_render_settings_defaults(&rs, NS_QUALITY_MEDIUM);
    float sens = 1.0f;
    bool realtime = false;
    const room_menu_ctx ctx = { &rs, &sens, &realtime, NULL, NULL, NULL, NULL, NULL };
    room_menu_open(&m);

    /* Visées par leur LIBELLÉ, et non par arithmétique sur le nombre de lignes.
     * La version précédente écrivait `n - 5` et `n - 4` : elle a survécu à
     * l'insertion d'une ligne « TEMPS REEL » entre SOURIS et REPRENDRE en
     * continuant de tourner sur la mauvaise ligne. */
    const int row_steps = room_menu_row("PAS");
    const int row_tone  = room_menu_row("FOND DE SALLE");
    CHECK(row_steps >= 0 && row_tone >= 0 && row_tone == row_steps + 1,
          "les lignes « PAS » et « FOND DE SALLE » existent et se suivent (%d, %d)",
          row_steps, row_tone);

    room_sound_set_level(ROOM_LEVEL_STEPS, 0.50f);
    go_to(&m, &ctx, row_steps);
    room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    CHECK(room_sound_get_level(ROOM_LEVEL_STEPS) > 0.59f
          && room_sound_get_level(ROOM_LEVEL_STEPS) < 0.61f,
          "« PAS » monte par crans de 5 %% (%.3f)",
          (double)room_sound_get_level(ROOM_LEVEL_STEPS));
    /* Un réglage sonore ne demande PAS de réappliquer le rendu. C'est ce qui
     * évite de reconstruire les cibles GPU chaque fois qu'on bouge un volume. */
    CHECK(!m.render_dirty, "…sans redemander une application du rendu");

    for (int i = 0; i < 40; ++i) room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
    CHECK(room_sound_get_level(ROOM_LEVEL_STEPS) == 0.0f,
          "« PAS » se coupe et ne passe pas sous zéro (%.3f)",
          (double)room_sound_get_level(ROOM_LEVEL_STEPS));
    for (int i = 0; i < 60; ++i) room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    CHECK(room_sound_get_level(ROOM_LEVEL_STEPS) == 1.0f,
          "…et ne monte pas au-dessus de 100 %% (%.3f)",
          (double)room_sound_get_level(ROOM_LEVEL_STEPS));

    /* Et l'autre ligne n'a pas bougé : c'est LE défaut qu'un menu à table
     * partagée produit — deux entrées qui écrivent la même case. */
    room_sound_set_level(ROOM_LEVEL_TONE, 0.70f);
    go_to(&m, &ctx, row_steps);
    for (int i = 0; i < 5; ++i) room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
    CHECK(room_sound_get_level(ROOM_LEVEL_TONE) > 0.69f
          && room_sound_get_level(ROOM_LEVEL_TONE) < 0.71f,
          "régler « PAS » ne touche pas « FOND DE SALLE » (%.3f)",
          (double)room_sound_get_level(ROOM_LEVEL_TONE));

    go_to(&m, &ctx, row_tone);
    for (int i = 0; i < 40; ++i) room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
    CHECK(room_sound_get_level(ROOM_LEVEL_TONE) == 0.0f,
          "« FOND DE SALLE » se coupe entièrement (%.3f)",
          (double)room_sound_get_level(ROOM_LEVEL_TONE));

    /* Changer de palier de qualité ne doit rien effacer ici non plus : c'est
     * exactement le piège que ce fichier existe pour tenir. */
    room_sound_set_level(ROOM_LEVEL_STEPS, 0.35f);
    room_sound_set_level(ROOM_LEVEL_TONE, 0.15f);
    go_to(&m, &ctx, 0);
    room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    CHECK(room_sound_get_level(ROOM_LEVEL_STEPS) > 0.34f
          && room_sound_get_level(ROOM_LEVEL_STEPS) < 0.36f,
          "changer de palier n'efface pas « PAS » (%.3f)",
          (double)room_sound_get_level(ROOM_LEVEL_STEPS));
    CHECK(room_sound_get_level(ROOM_LEVEL_TONE) > 0.14f
          && room_sound_get_level(ROOM_LEVEL_TONE) < 0.16f,
          "…ni « FOND DE SALLE » (%.3f)",
          (double)room_sound_get_level(ROOM_LEVEL_TONE));

    /* Les flèches sur un bouton restent inertes maintenant que deux lignes se
     * sont insérées : la garde porte sur l'INDICE des boutons, et un décalage
     * d'entrée est exactement ce qui la casserait sans bruit. */
    go_to(&m, &ctx, room_menu_row("QUITTER LE JEU"));
    const float keep = room_sound_get_level(ROOM_LEVEL_TONE);
    room_menu_input(&m, &ctx, ROOM_MENU_LEFT);
    room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    CHECK(room_sound_get_level(ROOM_LEVEL_TONE) == keep,
          "« QUITTER » ne règle pas le voisin (%.3f)",
          (double)room_sound_get_level(ROOM_LEVEL_TONE));
}

/*
 * CE QUE LE MENU GARDE, ET CE QU'IL NE DOIT PAS GARDER.
 *
 * Les trois champs de fenêtre du contexte partent de l'état RÉEL du jeu, ligne
 * de commande comprise — c'est voulu, et documenté dans `room_menu.h`. Mais
 * `room_menu_persist` les écrivait sans distinction, et la conséquence était
 * mesurable sur le binaire : une capture lancée avec `--width=1920
 * --height=900` laissait « window.width = 1920 » dans `settings.cfg`, effaçant
 * la définition choisie par le joueur — qui n'avait jamais ouvert le menu.
 *
 * Ce test tient les DEUX moitiés de la règle. Une seule des deux ne suffit
 * pas : « n'écrit jamais » passerait aussi bien, et le réglage cesserait
 * d'exister.
 */
static void test_persist_fenetre(void)
{
    ns_render_settings rs; ns_render_settings_defaults(&rs, NS_QUALITY_MEDIUM);
    float sens = 1.0f;
    int  w = 1920, h = 900;          /* ce que la ligne de commande a imposé */
    bool fs = true;                  /* idem, par `--fullscreen` */
    bool w_touched = false, fs_touched = false;
    const room_menu_ctx ctx = { &rs, &sens, NULL, &w, &h, &fs,
                                &w_touched, &fs_touched };

    /* --- moitié 1 : sans geste du joueur, rien ne s'écrit ------------------ */
    ns_config_init("menu-fenetre-test.cfg");
    ns_config_set_int(NS_CFG_WINDOW_W, 1280);
    ns_config_set_int(NS_CFG_WINDOW_H, 720);
    ns_config_set_bool(NS_CFG_FULLSCREEN, false);

    room_menu_persist(&ctx);
    CHECK(ns_config_get_int(NS_CFG_WINDOW_W, 0) == 1280,
          "une définition imposée en ligne de commande ne remplace pas celle "
          "qui est gardée (%d)", ns_config_get_int(NS_CFG_WINDOW_W, 0));
    CHECK(ns_config_get_int(NS_CFG_WINDOW_H, 0) == 720,
          "…sur les deux axes (%d)", ns_config_get_int(NS_CFG_WINDOW_H, 0));
    CHECK(ns_config_get_bool(NS_CFG_FULLSCREEN, true) == false,
          "…et `--fullscreen` ne coche pas la case pour toujours");

    /* --- moitié 2 : après un geste dans le menu, ça s'écrit ---------------- */
    room_menu m; memset(&m, 0, sizeof m);
    room_menu_open(&m);
    const int row_def = room_menu_row("DEFINITION");
    const int row_fs  = room_menu_row("PLEIN ECRAN");
    CHECK(row_def >= 0 && row_fs == row_def + 1,
          "les lignes « DEFINITION » et « PLEIN ECRAN » existent et se suivent "
          "(%d, %d)", row_def, row_fs);

    go_to(&m, &ctx, row_def);
    room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    CHECK(w_touched, "changer la définition lève le drapeau");
    CHECK(m.window_dirty, "…et demande à la fenêtre de suivre");
    const int chosen_w = w, chosen_h = h;
    CHECK(chosen_w != 1920 || chosen_h != 900,
          "…et la valeur a bougé (%d x %d)", chosen_w, chosen_h);
    /* …et PAS l'autre. Sans cette ligne, un code qui lève les deux drapeaux
     * d'un seul geste passait le test : mesuré, cette mutation-là ne faisait
     * tomber aucune assertion. C'est pourtant le défaut d'origine sous un autre
     * geste — changer la définition emporterait un plein écran imposé par la
     * ligne de commande. */
    CHECK(!fs_touched, "…et ne lève pas celui du plein écran");

    go_to(&m, &ctx, row_fs);
    room_menu_input(&m, &ctx, ROOM_MENU_RIGHT);
    CHECK(fs_touched, "basculer le plein écran lève l'autre drapeau");
    CHECK(fs == false, "…et bascule bien");

    /*
     * LA CONFIGURATION EST SEMÉE D'UNE VALEUR QUE LE MENU NE PEUT PAS CHOISIR,
     * et c'est la mutation qui l'a exigé.
     *
     * Ce test posait 1280 x 720 dans la première moitié et vérifiait ensuite
     * que la valeur écrite valait celle du menu. Or, partant d'une définition
     * hors liste, le premier cran atterrit sur la PREMIÈRE case — qui est
     * précisément 1280 x 720. Les deux nombres coïncidaient, et un
     * `room_menu_persist` qui n'écrivait RIEN passait le test : mesuré, la
     * mutation « ne jamais écrire » ne faisait tomber aucune assertion.
     * 800 x 600 n'est dans aucune case, donc seule une écriture réelle peut
     * l'effacer.
     */
    ns_config_set_int(NS_CFG_WINDOW_W, 800);
    ns_config_set_int(NS_CFG_WINDOW_H, 600);
    ns_config_set_bool(NS_CFG_FULLSCREEN, true);
    CHECK(chosen_w != 800 && chosen_h != 600,
          "la valeur semée n'est pas celle que le menu vient de choisir "
          "(%d x %d)", chosen_w, chosen_h);

    room_menu_persist(&ctx);
    CHECK(ns_config_get_int(NS_CFG_WINDOW_W, 0) == chosen_w,
          "ce que le joueur a choisi est écrit (%d au lieu de %d)",
          ns_config_get_int(NS_CFG_WINDOW_W, 0), chosen_w);
    CHECK(ns_config_get_int(NS_CFG_WINDOW_H, 0) == chosen_h,
          "…sur les deux axes (%d au lieu de %d)",
          ns_config_get_int(NS_CFG_WINDOW_H, 0), chosen_h);
    CHECK(ns_config_get_bool(NS_CFG_FULLSCREEN, true) == false,
          "…et le plein écran aussi");

    /*
     * Les deux drapeaux sont SÉPARÉS : toucher l'un ne doit pas faire écrire
     * l'autre. Sans ça, basculer le plein écran emporterait avec lui une
     * définition imposée par la ligne de commande — le défaut d'origine, sous
     * un autre geste.
     */
    int  w2 = 640, h2 = 360;
    bool fs2 = false;
    bool w2_touched = false, fs2_touched = true;
    const room_menu_ctx ctx2 = { &rs, &sens, NULL, &w2, &h2, &fs2,
                                 &w2_touched, &fs2_touched };
    ns_config_set_int(NS_CFG_WINDOW_W, 1600);
    room_menu_persist(&ctx2);
    CHECK(ns_config_get_int(NS_CFG_WINDOW_W, 0) == 1600,
          "un geste sur le plein écran n'emporte pas la définition (%d)",
          ns_config_get_int(NS_CFG_WINDOW_W, 0));

    /* Un pointeur de drapeau NUL vaut « pas touché » : c'est le sens sûr, et
     * c'est ce que font les sept autres contextes de ce fichier. */
    const room_menu_ctx ctx3 = { &rs, &sens, NULL, &w2, &h2, &fs2, NULL, NULL };
    room_menu_persist(&ctx3);
    CHECK(ns_config_get_int(NS_CFG_WINDOW_W, 0) == 1600,
          "un drapeau nul n'écrit rien (%d)",
          ns_config_get_int(NS_CFG_WINDOW_W, 0));

    ns_config_shutdown();
}

static void test_persist(const char *dir)
{
    ns_render_settings rs; ns_render_settings_defaults(&rs, NS_QUALITY_HIGH);
    rs.render_scale = 0.75f;
    float sens = 1.85f;
    const room_menu_ctx ctx = { &rs, &sens, NULL, NULL, NULL, NULL, NULL, NULL };

    room_sound_set_level(ROOM_LEVEL_STEPS, 0.45f);
    room_sound_set_level(ROOM_LEVEL_TONE, 0.20f);

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

    /*
     * Les deux niveaux de la salle, écrits SANS mixeur.
     *
     * C'est la vérification qui compte pour eux : ils sont volontairement hors
     * du garde `ns_audio_ready` dans `room_menu_persist`, pour qu'un réglage
     * fait sur une machine muette ne soit pas effacé en silence. Ce test tourne
     * précisément sur une telle machine — il n'appelle jamais `ns_audio_init` —
     * donc il mesure exactement ce cas.
     */
    CHECK(ns_config_get_float(ROOM_CFG_VOL_STEPS, -1.0f) > 0.44f
          && ns_config_get_float(ROOM_CFG_VOL_STEPS, -1.0f) < 0.46f,
          "« PAS » est écrit même sans sortie audio (%.3f)",
          (double)ns_config_get_float(ROOM_CFG_VOL_STEPS, -1.0f));
    CHECK(ns_config_get_float(ROOM_CFG_VOL_TONE, -1.0f) > 0.19f
          && ns_config_get_float(ROOM_CFG_VOL_TONE, -1.0f) < 0.21f,
          "« FOND DE SALLE » aussi (%.3f)",
          (double)ns_config_get_float(ROOM_CFG_VOL_TONE, -1.0f));

    ns_config_shutdown();
    (void)dir;
}

/*
 * L'ATTRIBUTION DE CESIUMMAN — la seule vérification de ce fichier qui ne porte
 * pas sur le confort, mais sur le DROIT de distribuer le jeu.
 *
 * Le personnage est sous CC BY 4.0 : l'emploi est libre, y compris commercial,
 * à la condition que l'auteur soit crédité. Cette condition-là ne se documente
 * pas, elle se tient — et une mention légale que rien ne défend finit par
 * disparaître dans un nettoyage, ou par survivre à un fichier qu'on renomme
 * sans que personne s'en aperçoive.
 *
 * On vérifie donc les quatre choses que la licence demande nommément : l'ŒUVRE
 * (CesiumMan), l'AUTEUR (Cesium), la LICENCE (CC BY 4.0) et un LIEN vers elle.
 * Et on vérifie qu'on y arrive : une page de crédits qu'aucune ligne du menu
 * n'ouvre n'est pas un écran de crédits.
 */
static bool credits_contain(const char *needle)
{
    int n = 0;
    const room_credit_line *l = room_credits_lines(&n);
    for (int i = 0; i < n; ++i) {
        if (l[i].what && SDL_strstr(l[i].what, needle)) return true;
        if (l[i].who  && SDL_strstr(l[i].who,  needle)) return true;
    }
    return false;
}

static void test_credits(void)
{
    int n = 0;
    room_credits_lines(&n);
    CHECK(n > 0, "l'écran de crédits a des lignes (%d)", n);

    CHECK(credits_contain("CESIUMMAN"), "les crédits nomment l'œuvre : CesiumMan");
    CHECK(credits_contain("CESIUM"),    "…et son auteur : Cesium");
    CHECK(credits_contain("CC BY 4.0"), "…et sa licence : CC BY 4.0");
    /* La cinquième mention, exigée par CC BY 4.0 § 3.a.1.B des lors que l'oeuvre
     * est modifiee — et elle l'est : la texture d'origine a ete remplacee par
     * une peau peinte. L'omettre laisserait croire que Cesium a dessine ce
     * blouson. */
    CHECK(credits_contain("MODIFIE"), "…et qu'elle a ete MODIFIEE");
    CHECK(credits_contain("CREATIVECOMMONS.ORG"), "…et le lien vers cette licence");

    /* Les bibliothèques dont la licence exige, elle aussi, que la mention
     * accompagne le binaire distribué. */
    CHECK(credits_contain("SDL3"),  "les crédits nomment SDL3");
    CHECK(credits_contain("CGLTF"), "…cgltf");
    CHECK(credits_contain("JSMN"),  "…jsmn");
    CHECK(credits_contain("STB"),   "…stb");

    /* Et la page s'ATTEINT. Un écran qu'on ne peut pas ouvrir n'attribue rien. */
    room_menu m; memset(&m, 0, sizeof m);
    ns_render_settings rs; ns_render_settings_defaults(&rs, NS_QUALITY_MEDIUM);
    float sens = 1.0f;
    const room_menu_ctx ctx = { &rs, &sens, NULL, NULL, NULL, NULL, NULL, NULL };

    const int row = room_menu_row("CREDITS");
    CHECK(row >= 0, "la ligne « CREDITS » existe dans le menu");
    room_menu_open(&m);
    CHECK(m.page == ROOM_MENU_PAGE_SETTINGS, "le menu s'ouvre sur les réglages");
    go_to(&m, &ctx, row);
    room_menu_input(&m, &ctx, ROOM_MENU_ACCEPT);
    CHECK(m.page == ROOM_MENU_PAGE_CREDITS, "…et Entrée y ouvre les crédits");
    CHECK(!m.close_request && !m.quit_request,
          "ouvrir les crédits ne ferme pas le menu et ne quitte pas le jeu");

    /* On en revient, et on revient aux RÉGLAGES — pas au bureau. */
    room_menu_input(&m, &ctx, ROOM_MENU_CANCEL);
    CHECK(m.page == ROOM_MENU_PAGE_SETTINGS, "Échap remonte aux réglages");
    CHECK(!m.close_request, "…sans fermer le menu");

    /* Les flèches sur une page d'information n'y déplacent pas un curseur
     * invisible : elles ramènent, comme tout le reste. */
    room_menu_input(&m, &ctx, ROOM_MENU_ACCEPT);
    CHECK(m.page == ROOM_MENU_PAGE_CREDITS, "on rouvre les crédits");
    const int cursor = m.cursor;
    room_menu_input(&m, &ctx, ROOM_MENU_DOWN);
    CHECK(m.page == ROOM_MENU_PAGE_SETTINGS && m.cursor == cursor,
          "une flèche ramène aux réglages sans bouger le curseur (%d, attendu %d)",
          m.cursor, cursor);

    /* La page des commandes, par le même chemin. */
    const int crow = room_menu_row("COMMANDES");
    CHECK(crow >= 0, "la ligne « COMMANDES » existe");
    go_to(&m, &ctx, crow);
    room_menu_input(&m, &ctx, ROOM_MENU_ACCEPT);
    CHECK(m.page == ROOM_MENU_PAGE_CONTROLS, "…et elle ouvre la page des commandes");

    int cn = 0;
    room_controls_lines(&cn);
    CHECK(cn > 0, "la page des commandes a des lignes (%d)", cn);

    /*
     * ET ELLE DIT LA VERITE, dans les deux sens.
     *
     * Ce controle exigeait l'inverse : que la page ne promette PAS de manette,
     * parce qu'il n'y en avait pas et qu'annoncer une commande inexistante fait
     * croire au joueur que son materiel est casse. La manette existe depuis
     * `room/room_pad.c`, et la meme regle demande donc l'oppose — une commande
     * qui existe et que rien n'annonce est une commande que personne n'emploie.
     *
     * On ne verifie pas seulement le mot « MANETTE » : un titre de section sans
     * lignes en dessous serait une promesse vide. Les quatre gestes qu'un
     * joueur cherche en premier doivent y etre nommes.
     */
    int i;
    bool promises_pad = false;
    const room_credit_line *cl = room_controls_lines(&i);
    for (i = 0; i < cn; ++i) {
        if ((cl[i].what && SDL_strstr(cl[i].what, "MANETTE"))
         || (cl[i].who  && SDL_strstr(cl[i].who,  "MANETTE"))) promises_pad = true;
    }
    CHECK(promises_pad, "la page des commandes annonce la manette : il y en a une");

    /*
     * ET ELLE ANNONCE LE COUPERET, pour la raison qui a fait ajouter la manette
     * juste au-dessus : c'est le seul MODE du jeu, rien dans la salle ne dit
     * qu'il existe, et sa touche est une touche de fonction. Non annoncée ici,
     * elle n'est nulle part — et un mode que personne ne trouve n'existe pas.
     */
    bool promet_couperet = false;
    for (i = 0; i < cn; ++i) {
        if (cl[i].what && SDL_strstr(cl[i].what, "COUPERET")) promet_couperet = true;
    }
    CHECK(promet_couperet, "la page des commandes annonce le Couperet : il existe");

    static const char *const gestes[] = { "STICK GAUCHE", "STICK DROIT", "SAUTER", "COURIR",
                                          "F9", "TAB", "1 A 6" };
    for (size_t k = 0; k < SDL_arraysize(gestes); ++k) {
        bool trouve = false;
        for (i = 0; i < cn; ++i) {
            if ((cl[i].what && SDL_strstr(cl[i].what, gestes[k]))
             || (cl[i].who  && SDL_strstr(cl[i].who,  gestes[k]))) trouve = true;
        }
        CHECK(trouve, "la page des commandes nomme « %s »", gestes[k]);
    }

    room_menu_close(&m);
    CHECK(m.page == ROOM_MENU_PAGE_SETTINGS, "fermer le menu repose la page des réglages");
}

int main(int argc, char **argv)
{
    ns_paths_init(argv[0]);
    test_navigation();
    test_quality_preserves_the_other_rows();
    test_bounds();
    test_buttons();
    test_room_levels();
    test_credits();
    test_persist(argc > 1 ? argv[1] : ".");
    test_persist_fenetre();
    ns_paths_shutdown();
    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
