/*
 * test_core.c — vérifications du noyau, sans GPU.
 *
 * Ces tests portent sur ce qui a historiquement lâché dans la V1 : allocation
 * mal dimensionnée, aléatoire non semé, temps de simulation lié au framerate,
 * chemins de fichiers non bornés.
 */
#include "ns_core.h"
#include "ns_json.h"
#include "ns_math.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("  ÉCHEC %s:%d — ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabsf((a) - (b)) <= (eps), \
    "%s ≈ %s : %.6f vs %.6f", #a, #b, (double)(a), (double)(b))

/* -------------------------------------------------------------------------- */

static void test_arena(void)
{
    printf("arène mémoire\n");

    ns_arena a;
    CHECK(ns_arena_init(&a, 4096, "test"), "initialisation");

    /* Le piège de l'original : allouer d'après une valeur au lieu d'un type.
     * Les macros dérivent la taille du type, donc la taille est forcément juste. */
    typedef struct { double x, y, z; int tag; } big;
    big *p = NS_ARENA_NEW(&a, big);
    CHECK(p != NULL, "allocation simple");
    CHECK(a.used >= sizeof(big), "taille réservée %zu >= %zu", a.used, sizeof(big));

    /* Écrire dans toute la structure ne doit pas déborder. */
    p->x = 1.0; p->y = 2.0; p->z = 3.0; p->tag = 42;
    CHECK(p->tag == 42, "écriture complète sans corruption");

    big *arr = NS_ARENA_ARRAY(&a, big, 16);
    CHECK(arr != NULL, "allocation de tableau");
    memset(arr, 0xAB, sizeof(big) * 16);          /* détecté par ASan si trop court */

    /* Alignement respecté. */
    CHECK(((uintptr_t)arr % _Alignof(big)) == 0, "alignement du tableau");

    const size_t before = a.used;
    ns_arena_mark m = ns_arena_save(&a);
    (void)NS_ARENA_ARRAY(&a, big, 8);
    CHECK(a.used > before, "l'allocation temporaire consomme");
    ns_arena_restore(&a, m);
    CHECK(a.used == before, "restauration exacte de la marque");

    ns_arena_reset(&a);
    CHECK(a.used == 0, "remise à zéro");
    CHECK(a.peak > 0, "le pic est mesuré");

    ns_arena_free(&a);
}

static void test_alloc_overflow_guards(void)
{
    printf("garde-fous d'allocation\n");

    /* ns_calloc doit refuser un produit qui déborde plutôt que d'allouer un
     * bloc minuscule — variante exacte du bug d'origine. */
    void *p = ns_calloc(SIZE_MAX / 2, 4);
    CHECK(p == NULL, "produit qui déborde refusé");

    const size_t blocks_before = ns_alloc_live_blocks();
    void *q = ns_alloc(1024);
    CHECK(q != NULL, "allocation ordinaire");
    CHECK(ns_alloc_live_blocks() == blocks_before + 1, "comptage des blocs vivants");
    memset(q, 0x5A, 1024);
    ns_free(q);
    CHECK(ns_alloc_live_blocks() == blocks_before, "libération comptabilisée");

    CHECK(ns_alloc(0) == NULL, "taille nulle renvoie NULL");
    ns_free(NULL);                                   /* ne doit pas planter */
}

static void test_fixed_timestep(void)
{
    printf("boucle à pas fixe\n");

    /* Propriété visée : pour une même durée simulée, le nombre de pas est le
     * même quel que soit le découpage en images. C'est ce qui rend la physique
     * indépendante du framerate. */
    const double tick = 1.0 / 120.0;

    ns_clock c;
    ns_clock_init(&c, 120.0);

    /* On pilote l'accumulateur à la main pour ne pas dépendre de l'horloge réelle. */
    int ticks_at_60fps = 0;
    c.accumulator = 0.0;
    for (int frame = 0; frame < 60; ++frame) {           /* 60 images de 1/60 s = 1 s */
        c.accumulator += 1.0 / 60.0;
        while (ns_clock_consume_tick(&c)) ticks_at_60fps++;
    }

    ns_clock_init(&c, 120.0);
    int ticks_at_144fps = 0;
    c.accumulator = 0.0;
    for (int frame = 0; frame < 144; ++frame) {          /* 144 images de 1/144 s = 1 s */
        c.accumulator += 1.0 / 144.0;
        while (ns_clock_consume_tick(&c)) ticks_at_144fps++;
    }

    /* Une seconde simulée doit donner 120 pas, à un près : additionner 1/144 cent
     * quarante-quatre fois ne redonne pas exactement 1.0 en virgule flottante, et
     * le reste part dans l'accumulateur (donc dans alpha) plutôt que d'être perdu.
     * C'est le comportement voulu : aucun temps n'est jeté, il est seulement
     * reporté sur l'image suivante. */
    CHECK(abs(ticks_at_60fps - 120) <= 1, "1 s à 60 fps ≈ 120 pas (obtenu %d)", ticks_at_60fps);
    CHECK(abs(ticks_at_144fps - 120) <= 1, "1 s à 144 fps ≈ 120 pas (obtenu %d)", ticks_at_144fps);
    CHECK(abs(ticks_at_60fps - ticks_at_144fps) <= 1,
          "même durée simulée quel que soit le framerate (%d vs %d)", ticks_at_60fps, ticks_at_144fps);

    /* Sur une longue durée, le reste ne doit pas dériver : il est conservé dans
     * l'accumulateur, donc le total reste proportionnel au temps écoulé. */
    ns_clock_init(&c, 120.0);
    c.accumulator = 0.0;
    int long_ticks = 0;
    for (int frame = 0; frame < 144 * 60; ++frame) {     /* 60 s à 144 fps */
        c.accumulator += 1.0 / 144.0;
        while (ns_clock_consume_tick(&c)) long_ticks++;
    }
    CHECK(abs(long_ticks - 7200) <= 2, "60 s à 144 fps ≈ 7200 pas (obtenu %d)", long_ticks);

    /* alpha reste dans [0,1] même avec un reste. */
    ns_clock_init(&c, 120.0);
    c.accumulator = tick * 0.37;
    ns_clock_end_frame(&c);
    CHECK(c.alpha >= 0.0 && c.alpha <= 1.0, "alpha borné (%.3f)", c.alpha);
    CHECK_NEAR((float)c.alpha, 0.37f, 1e-4f);
}

static void test_rng_determinism(void)
{
    printf("générateur pseudo-aléatoire\n");

    /* La V1 appelait rand() sans srand() : suite identique à chaque lancement,
     * et impossible à rejouer volontairement. Ici la graine est explicite. */
    ns_rng a, b;
    ns_rng_seed(&a, 1234, 1);
    ns_rng_seed(&b, 1234, 1);
    for (int i = 0; i < 1000; ++i) {
        if (ns_rng_u32(&a) != ns_rng_u32(&b)) {
            CHECK(false, "deux générateurs de même graine divergent au tirage %d", i);
            break;
        }
    }
    CHECK(true, "même graine, même suite");

    ns_rng c;
    ns_rng_seed(&c, 4321, 1);
    ns_rng_seed(&a, 1234, 1);
    bool differs = false;
    for (int i = 0; i < 32 && !differs; ++i) {
        if (ns_rng_u32(&a) != ns_rng_u32(&c)) differs = true;
    }
    CHECK(differs, "graines différentes, suites différentes");

    /* ns_rng_below doit rester dans les bornes et ne pas biaiser comme `% n`. */
    ns_rng_seed(&a, 99, 1);
    int buckets[7] = { 0 };
    const int draws = 70000;
    for (int i = 0; i < draws; ++i) {
        const uint32_t v = ns_rng_below(&a, 7);
        CHECK(v < 7, "tirage hors bornes : %u", v);
        buckets[v]++;
    }
    /* Chaque case devrait recevoir ~10000 tirages ; on tolère 5 %. */
    for (int i = 0; i < 7; ++i) {
        CHECK(buckets[i] > 9500 && buckets[i] < 10500,
              "distribution case %d : %d tirages (attendu ~10000)", i, buckets[i]);
    }

    ns_rng_seed(&a, 7, 1);
    for (int i = 0; i < 10000; ++i) {
        const float f = ns_rng_float(&a);
        if (f < 0.0f || f >= 1.0f) { CHECK(false, "float hors [0,1) : %f", (double)f); break; }
    }
}

static void test_math(void)
{
    printf("mathématiques\n");

    /* Matrices : M * M⁻¹ = I */
    const ns_m4 view = ns_m4_look_at(ns_v3_make(3, 4, 5), ns_v3_make(0, 1, 0), ns_v3_make(0, 1, 0));
    const ns_m4 inv  = ns_m4_inverse(view);
    const ns_m4 id   = ns_m4_mul(view, inv);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            const float expect = (c == r) ? 1.0f : 0.0f;
            CHECK(fabsf(id.m[c][r] - expect) < 1e-4f,
                  "M*M⁻¹ [%d][%d] = %.6f, attendu %.1f", c, r, (double)id.m[c][r], (double)expect);
        }
    }

    /* Reverse-Z : le plan proche doit se projeter en 1, le lointain en 0.
     * C'est ce qui donne la précision de profondeur sur les grandes distances. */
    const ns_m4 proj = ns_m4_perspective(60.0f * NS_DEG2RAD, 16.0f / 9.0f, 0.05f, 100.0f, true);
    const ns_v3 near_pt = ns_m4_project(proj, ns_v3_make(0, 0, -0.05f));
    const ns_v3 far_pt  = ns_m4_project(proj, ns_v3_make(0, 0, -100.0f));
    CHECK_NEAR(near_pt.z, 1.0f, 1e-3f);
    CHECK_NEAR(far_pt.z, 0.0f, 1e-3f);

    /* Quaternions : deux rotations de 90° autour de Y font un demi-tour. */
    const ns_quat q90 = ns_quat_from_axis(ns_v3_make(0, 1, 0), NS_PI * 0.5f);
    const ns_m4 rot = ns_m4_from_quat(ns_quat_mul(q90, q90));
    const ns_v3 x_axis = ns_v4_xyz(ns_m4_mul_v4(rot, ns_v4_make(1, 0, 0, 0)));
    CHECK_NEAR(x_axis.x, -1.0f, 1e-4f);
    CHECK_NEAR(x_axis.z,  0.0f, 1e-4f);

    /* slerp aux extrémités. */
    const ns_quat qa = ns_quat_identity();
    const ns_quat mid = ns_quat_slerp(qa, q90, 0.0f);
    CHECK_NEAR(mid.w, 1.0f, 1e-5f);

    /* Rayon / triangle. Le triangle vit dans le plan z = -5, sommets (-1,0), (1,0)
     * et l'apex (0,2). Une direction (0, 0.2, -1) atteint ce plan en t = 5, au
     * point (0, 1, -5) : bien à l'intérieur. */
    const ns_v3 a = ns_v3_make(-1, 0, -5), b = ns_v3_make(1, 0, -5), c3 = ns_v3_make(0, 2, -5);
    float t = 0, u = 0, v = 0;
    CHECK(ns_ray_triangle(ns_v3_zero(), ns_v3_make(0, 0.2f, -1), a, b, c3, 100.0f, &t, &u, &v),
          "impact attendu à l'intérieur du triangle");
    CHECK_NEAR(t, 5.0f, 1e-3f);
    CHECK(u >= 0.0f && v >= 0.0f && u + v <= 1.0f, "coordonnées barycentriques valides (%f, %f)",
          (double)u, (double)v);

    /* La même direction plus inclinée passe au-dessus de l'apex : doit manquer. */
    CHECK(!ns_ray_triangle(ns_v3_zero(), ns_v3_make(0, 0.5f, -1), a, b, c3, 100.0f, &t, &u, &v),
          "tir au-dessus de l'apex : aucun impact");
    /* Un tir à l'opposé ne doit rien toucher non plus. */
    CHECK(!ns_ray_triangle(ns_v3_zero(), ns_v3_make(0, 0.2f, 1), a, b, c3, 100.0f, &t, &u, &v),
          "aucun impact derrière l'origine");
    /* Et une portée trop courte doit rejeter un impact pourtant géométrique. */
    CHECK(!ns_ray_triangle(ns_v3_zero(), ns_v3_make(0, 0.2f, -1), a, b, c3, 4.0f, &t, &u, &v),
          "impact au-delà de la portée rejeté");

    /* Rayon / AABB. */
    ns_aabb box = { ns_v3_make(-1, -1, -1), ns_v3_make(1, 1, 1) };
    const ns_v3 dir = ns_v3_make(0, 0, -1);
    const ns_v3 invd = ns_v3_make(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z);
    CHECK(ns_ray_aabb(ns_v3_make(0, 0, 5), invd, box, 100.0f, &t), "impact AABB");
    CHECK_NEAR(t, 4.0f, 1e-3f);

    /* AABB : union et surface (utilisée par la construction du BVH). */
    ns_aabb e = ns_aabb_empty();
    CHECK(!ns_aabb_valid(e), "AABB vide invalide avant tout point");
    e = ns_aabb_add_point(e, ns_v3_make(1, 2, 3));
    e = ns_aabb_add_point(e, ns_v3_make(-1, 0, 1));
    CHECK(ns_aabb_valid(e), "AABB valide après deux points");
    CHECK_NEAR(ns_aabb_center(e).y, 1.0f, 1e-5f);
    CHECK(ns_aabb_surface(e) > 0.0f, "surface positive");
    CHECK(ns_aabb_contains(e, ns_v3_make(0, 1, 2)), "point intérieur détecté");
    CHECK(!ns_aabb_contains(e, ns_v3_make(9, 9, 9)), "point extérieur rejeté");

    /* Normalisation d'un vecteur nul : zéro, pas NaN. */
    const ns_v3 n = ns_v3_norm(ns_v3_zero());
    CHECK(n.x == 0.0f && n.y == 0.0f && n.z == 0.0f, "normalisation d'un vecteur nul");

    /* Amortissement indépendant du framerate : atteindre la cible en une seconde
     * doit donner le même résultat en 60 ou 240 pas. */
    float v60 = 0.0f, v240 = 0.0f;
    for (int i = 0; i < 60; ++i)  v60  = ns_damp(v60,  1.0f, 5.0f, 1.0f / 60.0f);
    for (int i = 0; i < 240; ++i) v240 = ns_damp(v240, 1.0f, 5.0f, 1.0f / 240.0f);
    CHECK(fabsf(v60 - v240) < 1e-3f, "amortissement stable : %.5f vs %.5f", (double)v60, (double)v240);
}

static void test_paths(void)
{
    printf("chemins de données\n");

    CHECK(ns_paths_init(NULL), "initialisation");

    char out[512];
    /* Toute tentative de sortir de l'arborescence doit être refusée. */
    CHECK(!ns_path_resolve("../../etc/passwd", out, sizeof out), "remontée refusée");
    CHECK(!ns_path_resolve("/etc/passwd", out, sizeof out), "chemin absolu refusé");
    CHECK(!ns_path_resolve("a/../../b", out, sizeof out), "remontée masquée refusée");
    CHECK(!ns_path_resolve("", out, sizeof out), "chemin vide refusé");
    CHECK(!ns_path_resolve(NULL, out, sizeof out), "chemin nul refusé");
    CHECK(out[0] == '\0', "la sortie est vidée en cas de refus");

    /* Un fichier réellement présent doit être trouvé via un montage. */
    const char *dir = ns_path_user_dir();
    if (dir && *dir) {
        char probe[1024];
        SDL_snprintf(probe, sizeof probe, "%sns_probe.txt", dir);
        SDL_IOStream *io = SDL_IOFromFile(probe, "w");
        if (io) {
            SDL_WriteIO(io, "ok", 2);
            SDL_CloseIO(io);
            CHECK(ns_paths_mount(dir), "montage du répertoire utilisateur");
            CHECK(ns_path_resolve("ns_probe.txt", out, sizeof out), "fichier présent résolu");
            SDL_RemovePath(probe);
        }
    }
    ns_paths_shutdown();
}

/*
 * Priorité des montages : un montage ordinaire doit battre le répertoire du
 * binaire, même si celui-ci a été monté avant.
 *
 * `ns_paths_init` monte le répertoire du binaire dès l'initialisation, donc
 * *avant* que le jeu ait pu monter l'arbre de build ou `$NINETEEN_ASSETS`. Comme
 * la résolution retient le premier montage contenant le fichier, un simple ajout
 * en fin de tableau donnait la priorité au répertoire du binaire — l'inverse de ce
 * qu'annonçaient à la fois `ns_paths.c` et `room/main.c`. Installer le jeu à côté
 * d'un arbre d'assets périmé masquait alors l'arbre de build sans un mot.
 *
 * Le test met le même nom de fichier dans le répertoire du binaire et dans un
 * répertoire monté ensuite, et vérifie que c'est le second qui gagne. Il échoue
 * avec l'ancien comportement.
 */
static void test_mount_priority(void)
{
    printf("\npriorité des points de montage\n");

    const char *user = ns_path_user_dir();
    const char *bin  = SDL_GetBasePath();
    if (!user || !*user || !bin || !*bin) {
        printf("  (répertoires indisponibles, test ignoré)\n");
        return;
    }

    char over_dir[1024], over_file[1200], bin_file[1200];
    SDL_snprintf(over_dir, sizeof over_dir, "%sns_mount_over", user);
    SDL_CreateDirectory(over_dir);
    SDL_snprintf(over_file, sizeof over_file, "%s/ns_priority.txt", over_dir);
    SDL_snprintf(bin_file, sizeof bin_file, "%sns_priority.txt", bin);

    bool wrote = true;
    const char *paths[2] = { bin_file, over_file };
    const char *marks[2] = { "BIN", "OVER" };
    for (int i = 0; i < 2; ++i) {
        SDL_IOStream *io = SDL_IOFromFile(paths[i], "w");
        if (!io) { wrote = false; break; }
        SDL_WriteIO(io, marks[i], SDL_strlen(marks[i]));
        SDL_CloseIO(io);
    }

    if (!wrote) {
        printf("  (écriture impossible, test ignoré)\n");
    } else {
        CHECK(ns_paths_init(NULL), "initialisation");
        CHECK(ns_paths_mount(over_dir), "montage de surcharge");

        char out[1200];
        if (ns_path_resolve("ns_priority.txt", out, sizeof out)) {
            CHECK(SDL_strstr(out, "ns_mount_over") != NULL,
                  "un montage ordinaire doit battre le répertoire du binaire, "
                  "or on a résolu : %s", out);
        } else {
            CHECK(false, "ns_priority.txt devait être résolu");
        }

        /* Entre montages ordinaires, l'ordre d'ajout reste la priorité — c'est ce
         * qu'annonce l'API, et c'est pourquoi `room/main.c` monte la surcharge
         * d'environnement AVANT l'arbre de build. */
        char second[1024];
        SDL_snprintf(second, sizeof second, "%sns_mount_second", user);
        SDL_CreateDirectory(second);
        char second_file[1200];
        SDL_snprintf(second_file, sizeof second_file, "%s/ns_priority.txt", second);
        SDL_IOStream *io = SDL_IOFromFile(second_file, "w");
        if (io) {
            SDL_WriteIO(io, "SECOND", 6);
            SDL_CloseIO(io);
            CHECK(ns_paths_mount(second), "second montage ordinaire");
            if (ns_path_resolve("ns_priority.txt", out, sizeof out)) {
                CHECK(SDL_strstr(out, "ns_mount_over") != NULL,
                      "le premier montage ordinaire garde la priorité : %s", out);
            }
            SDL_RemovePath(second_file);
        }
        SDL_RemovePath(second);
        ns_paths_shutdown();
    }

    SDL_RemovePath(bin_file);
    SDL_RemovePath(over_file);
    SDL_RemovePath(over_dir);
}

/*
 * `NINETEEN_USER_DIR` : le répertoire de données se déplace-t-il VRAIMENT ?
 *
 * Le défaut cloué ici : toute exécution du jeu écrivait dans le répertoire de
 * sauvegarde RÉEL du propriétaire. Un `nineteen --headless --frames=1`, qui ne
 * joue rien et ne dessine rien, y réécrivait `nineteen.log`, `settings.cfg` ET
 * `portefeuille.txt` — et cette suite de tests faisait pareil. Il n'existait
 * aucun moyen de s'en abstraire : rediriger `HOME` ne protège rien, SDL passant
 * par `NSHomeDirectory` sur macOS, et trois intervenants s'y sont fait prendre
 * le même jour.
 *
 * Deux propriétés, et ce sont exactement les deux qui cassent en silence :
 *   1. le chemin demandé est CRÉÉ s'il manque — sans quoi chaque script devrait
 *      faire le `mkdir` lui-même, donc l'oublierait une fois sur deux, et
 *      retomberait chez le joueur sans un mot ;
 *   2. il ressort avec sa BARRE FINALE, parce que les onze appelants de
 *      `ns_path_user_dir()` concatènent sans séparateur : « …/bac » donnerait
 *      « …/bacnineteen.log », un fichier posé À CÔTÉ du répertoire visé.
 *
 * `SDL_SetEnvironmentVariable` et non `SDL_setenv_unsafe` : `SDL_getenv`, que
 * lit `ns_paths.c`, sert la copie CACHÉE de l'environnement, faite au démarrage
 * et que `setenv` ne touche pas. Le test passerait à côté de ce qu'il mesure.
 *
 * Le repli — chemin impossible, avertissement, répertoire habituel — n'est PAS
 * exercé ici, et c'est délibéré : il appelle `SDL_GetPrefPath`, donc il vise le
 * répertoire du joueur, ce que cette suite ne fait plus. Il se mesure à la main.
 */
static void test_user_dir_env(void)
{
    printf("\nrépertoire de données détourné\n");

    SDL_Environment *env = SDL_GetEnvironment();
    const char *avant = SDL_getenv("NINETEEN_USER_DIR");

    char sauvegarde[1024];
    sauvegarde[0] = '\0';
    if (avant) SDL_strlcpy(sauvegarde, avant, sizeof sauvegarde);

    if (sauvegarde[0] == '\0') {
        /* Sans valeur de départ il n'y a pas d'endroit sûr où écrire : le repli
         * irait chez le joueur. C'est `tests/CMakeLists.txt` qui la pose ; lancé
         * à la main sans elle, ce test s'abstient plutôt que de deviner. */
        printf("  (NINETEEN_USER_DIR absente, test ignoré)\n");
        return;
    }

    /* Volontairement SANS barre finale, et sur un niveau qui n'existe pas
     * encore : c'est le cas qu'un script écrit à la main produit. */
    char vise[1024];
    SDL_snprintf(vise, sizeof vise, "%sns_bac/essai", sauvegarde);
    (void)SDL_RemovePath(vise);

    CHECK(SDL_SetEnvironmentVariable(env, "NINETEEN_USER_DIR", vise, true),
          "la variable se pose pour la durée du test");
    CHECK(ns_paths_init(NULL), "initialisation avec un répertoire imposé");

    char attendu[1200];
    SDL_snprintf(attendu, sizeof attendu, "%s/", vise);
    const char *obtenu = ns_path_user_dir();
    CHECK(obtenu && SDL_strcmp(obtenu, attendu) == 0,
          "le répertoire imposé gagne, barre finale comprise : « %s » au lieu de « %s »",
          obtenu ? obtenu : "(nul)", attendu);

    SDL_PathInfo info;
    CHECK(SDL_GetPathInfo(vise, &info) && info.type == SDL_PATHTYPE_DIRECTORY,
          "le répertoire imposé a été créé, parents compris (%s)", vise);
    ns_paths_shutdown();

    /* On repose l'environnement, PUIS on réinitialise : ce qui suit — ici et
     * dans les autres exécutables de la suite — compte dessus pour ne pas
     * écrire chez le joueur. Un test qui laisse une variable globale derrière
     * lui fait dépendre le suivant de l'ordre d'exécution. */
    SDL_SetEnvironmentVariable(env, "NINETEEN_USER_DIR", sauvegarde, true);
    CHECK(ns_paths_init(NULL), "réinitialisation après restauration");
    CHECK(SDL_strcmp(ns_path_user_dir(), sauvegarde) == 0
          || SDL_strncmp(ns_path_user_dir(), sauvegarde, SDL_strlen(sauvegarde)) == 0,
          "le répertoire de la suite est revenu (« %s »)", ns_path_user_dir());
    ns_paths_shutdown();

    (void)SDL_RemovePath(vise);
    char parent[1100];
    SDL_snprintf(parent, sizeof parent, "%sns_bac", sauvegarde);
    (void)SDL_RemovePath(parent);
}

/* ========================================================================== */
/* JSON : les entiers 64 bits, et les octets bruts d'une valeur               */
/* ========================================================================== */

static void test_json_i64_and_span(void)
{
    /*
     * Ce que ce test protège, en une phrase : **la graine de partie**.
     *
     * Le serveur en tire une de 63 bits (`randomSeed`, bit de signe effacé) et
     * la renvoie en nombre JSON. Relue par `ns_json_get_float`, elle repasse par
     * 24 bits de mantisse : le client jouerait une autre partie que celle qui a
     * été ouverte, scellerait son journal sur SA graine, et le serveur
     * recalculerait sur la sienne. Chaque partie refusée pour sceau invalide,
     * sans qu'une seule ligne dise pourquoi.
     */
    printf("JSON : entiers 64 bits et bornes de valeur\n");

    const char *text =
        "{\"seed\":9007199254740993,\"neg\":-4611686018427387904,"
        "\"petit\":42,\"reel\":1.5,\"expo\":1e9,\"vrai\":true,\"nul\":null,"
        "\"mot\":\"12\",\"submission\":{\"a\":[1,2],\"s\":\"}{\"},\"apres\":7}";

    ns_arena arena;
    if (!ns_arena_init(&arena, 64u * 1024u, "test json")) { CHECK(false, "arène"); return; }

    ns_json doc;
    if (!ns_json_parse(&doc, text, strlen(text), &arena)) {
        CHECK(false, "analyse");
        ns_arena_free(&arena);
        return;
    }
    const ns_json_value *root = ns_json_root(&doc);

    /* 2^53+1 : le premier entier que même un `double` ne sait plus distinguer
     * de son voisin. Un `float` s'y trompe de plus de 500 millions. */
    CHECK(ns_json_get_i64(&doc, root, "seed", 0) == 9007199254740993LL,
          "2^53+1 revient exact : %lld", (long long)ns_json_get_i64(&doc, root, "seed", 0));
    CHECK((int64_t)ns_json_get_float(&doc, root, "seed", 0.0f) != 9007199254740993LL,
          "et le chemin flottant, lui, le perd — c'est bien le défaut visé");
    CHECK(ns_json_get_i64(&doc, root, "neg", 0) == -4611686018427387904LL, "un négatif aussi");
    CHECK(ns_json_get_i64(&doc, root, "petit", 0) == 42, "un petit entier");

    /* Le repli plutôt qu'une troncature muette : « 1.5 » n'est pas un entier,
     * et rendre 1 serait pire que d'annoncer qu'on n'a pas su lire. */
    CHECK(ns_json_get_i64(&doc, root, "reel", -7) == -7, "« 1.5 » rend le repli");
    CHECK(ns_json_get_i64(&doc, root, "expo", -7) == -7, "« 1e9 » aussi");
    CHECK(ns_json_get_i64(&doc, root, "vrai", -7) == -7, "« true » aussi");
    CHECK(ns_json_get_i64(&doc, root, "nul", -7) == -7, "« null » aussi");
    CHECK(ns_json_get_i64(&doc, root, "mot", -7) == -7, "et une CHAÎNE « 12 » n'est pas un nombre");
    CHECK(ns_json_get_i64(&doc, root, "absent", -7) == -7, "une clé absente rend le repli");

    /*
     * Les bornes d'un sous-document : c'est ce qui permet de sortir de
     * l'enveloppe de la file d'attente la soumission que le serveur attend,
     * sans la réécrire — donc sans changer l'octet sur lequel porte le sceau.
     * L'accolade PIÉGÉE dans « }{ » est là exprès : un découpage par comptage
     * naïf d'accolades s'arrêterait dessus.
     */
    size_t from = 0, to = 0;
    const ns_json_value *sub = ns_json_get(&doc, root, "submission");
    CHECK(ns_json_span(&doc, sub, &from, &to), "les bornes d'un objet se lisent");
    if (to > from) {
        const size_t n = to - from;
        CHECK(text[from] == '{' && text[to - 1] == '}',
              "elles vont d'accolade à accolade");
        CHECK(n == strlen("{\"a\":[1,2],\"s\":\"}{\"}"),
              "et couvrent l'objet entier, accolade piégée comprise (%zu)", n);
        CHECK(strncmp(text + from, "{\"a\":[1,2],\"s\":\"}{\"}", n) == 0,
              "octet pour octet");
    }
    CHECK(ns_json_get_i64(&doc, root, "apres", 0) == 7,
          "et le membre qui SUIT l'objet se lit encore");

    ns_arena_free(&arena);
}

int main(void)
{
    if (!SDL_Init(0)) {
        printf("SDL_Init a échoué : %s\n", SDL_GetError());
        return 1;
    }
    ns_log_set_level(NS_LOG_WARN);   /* silence le bavardage, garde les problèmes */

    printf("== tests du noyau Nineteen ==\n\n");
    test_arena();
    test_alloc_overflow_guards();
    test_fixed_timestep();
    test_rng_determinism();
    test_math();
    test_paths();
    test_mount_priority();
    test_user_dir_env();
    test_json_i64_and_span();

    printf("\n%d vérifications, %d échec(s)\n", g_checks, g_failures);
    SDL_Quit();
    return g_failures == 0 ? 0 : 1;
}
