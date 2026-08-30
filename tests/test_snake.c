/*
 * test_snake.c — les règles de Snake, confrontées à celles de 2020.
 *
 * Ce qui mérite un test ici n'est pas le dessin, ce sont les règles : ce sont
 * elles qu'on prétend avoir reprises, et elles ne se voient pas sur une capture.
 * Le portage convertit tout un jeu écrit en IMAGES à 30 Hz vers un pas fixe en
 * secondes ; une conversion ratée donne un jeu qui a l'air juste et se joue
 * autrement.
 *
 * Le cas qui justifie le fichier à lui seul : **le hardcore est l'inverse du
 * normal**. `RATIO_GET_FRUIT_HARDCORE` vaut −5, donc manger COÛTE et le score
 * vient des fruits qu'on laisse expirer. C'est la règle la plus surprenante de
 * 2020 et exactement celle qu'on « corrigerait » par mégarde.
 */
#include "games.h"
#include "ns_core.h"
#include "snake.h"

#include <SDL3/SDL.h>

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
            printf("ÉCHEC %s:%d — ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* La même vérification, mais dans une boucle de plusieurs milliers de tours :
 * on compte le passage et on ne se plaint qu'une fois. */
#define CHECK_SILENT(cond)                                                    \
    do { if (!(cond)) { g_checks++; g_failures++;                             \
             printf("ÉCHEC %s:%d — invariant rompu en cours de partie\n",     \
                    __FILE__, __LINE__); return; } } while (0)

#define STEP (1.0f / 120.0f)

static void run(snake *g, float seconds)
{
    const int n = (int)(seconds / STEP);
    for (int i = 0; i < n; ++i) snake_tick(g, STEP);
}

/* -------------------------------------------------------------------------- */

static void test_depart(void)
{
    snake g;
    snake_reset(&g, 1234, false);

    CHECK(g.parts == SNAKE_PRE + 30,
          "le serpent part avec 30 segments plus le tuyau de digestion (%u)", g.parts);
    CHECK(fabsf(g.angle - 3.0f * NS_PI / 2.0f) < 1e-4f,
          "il regarde vers le haut, comme BASE_ANGLE (%.4f)", (double)g.angle);
    CHECK(fabsf(g.speed - 7.0f) < 1e-4f, "BASE_SPEED vaut 7 (%.3f)", (double)g.speed);
    CHECK(g.score == 0 && g.phase == SNAKE_READY, "il commence à zéro, en sursis");

    /* La tête au centre du TERRAIN, pas de la fenêtre : 1728 x 972. */
    CHECK(fabsf(g.part[SNAKE_PRE].x - SNAKE_PLAY_W * 0.5f) < 0.5f
       && fabsf(g.part[SNAKE_PRE].y - SNAKE_PLAY_H * 0.5f) < 0.5f,
          "la tête est au centre du terrain (%.1f, %.1f)",
          (double)g.part[SNAKE_PRE].x, (double)g.part[SNAKE_PRE].y);
}

static void test_rotation(void)
{
    snake g;
    snake_reset(&g, 1234, false);
    const float a0 = g.angle;

    /* TURN_AMMOUNT 0,13 rad par image à 30 Hz = 3,9 rad/s. En une demi-seconde,
     * 1,95 rad — et le sens compte : « droite » augmente l'angle, comme
     * `turnRight` de 2020. */
    snake_hold(&g, false, true, false);
    run(&g, 0.5f);
    float delta = g.angle - a0;
    while (delta < -NS_PI) delta += 2.0f * NS_PI;
    while (delta > NS_PI) delta -= 2.0f * NS_PI;
    CHECK(fabsf(delta - 1.95f) < 0.05f,
          "tourner à droite une demi-seconde vaut 1,95 rad (%.3f)", (double)delta);

    snake_reset(&g, 1234, false);
    snake_hold(&g, true, false, false);
    run(&g, 0.5f);
    delta = g.angle - a0;
    while (delta < -NS_PI) delta += 2.0f * NS_PI;
    while (delta > NS_PI) delta -= 2.0f * NS_PI;
    CHECK(fabsf(delta + 1.95f) < 0.05f,
          "et à gauche, l'opposé exact (%.3f)", (double)delta);

    /* Les deux ensemble ne tournent pas : c'est ce qui arrive quand on écrase
     * les deux flèches, et un serpent qui partirait d'un côté serait arbitraire. */
    snake_reset(&g, 1234, false);
    snake_hold(&g, true, true, false);
    run(&g, 0.5f);
    CHECK(fabsf(g.angle - a0) < 1e-3f, "les deux flèches ensemble ne tournent pas");
}

static void test_vitesse_independante_du_pas(void)
{
    /*
     * LE test de la conversion. Le même temps de jeu doit produire le même
     * déplacement, que le moteur tourne à 120 Hz ou à 40 Hz. Sans l'accumulateur
     * de distance, la vitesse dépendrait de la fréquence de tick — le défaut
     * qu'on a passé le projet à retirer.
     */
    snake a, b;
    snake_reset(&a, 77, false);
    snake_reset(&b, 77, false);

    for (int i = 0; i < 240; ++i) snake_tick(&a, 1.0f / 120.0f);   /* 2 s */
    for (int i = 0; i < 80; ++i)  snake_tick(&b, 1.0f / 40.0f);    /* 2 s */

    const float dx = a.part[SNAKE_PRE].x - b.part[SNAKE_PRE].x;
    const float dy = a.part[SNAKE_PRE].y - b.part[SNAKE_PRE].y;
    CHECK(sqrtf(dx * dx + dy * dy) < 6.0f,
          "à 120 Hz et à 40 Hz, le serpent est au même endroit après 2 s (écart %.2f px)",
          (double)sqrtf(dx * dx + dy * dy));

    /* Et la distance parcourue est celle de 2020 : 7 px par image à 30 Hz,
     * soit 210 px/s, donc 420 px en deux secondes — vers le haut. */
    const float moved = SNAKE_PLAY_H * 0.5f - a.part[SNAKE_PRE].y;
    CHECK(fabsf(moved - 420.0f) < 12.0f,
          "il a parcouru 420 px en 2 s, comme BASE_SPEED l'impose (%.1f)", (double)moved);
}

static void test_mort_contre_le_mur(void)
{
    /*
     * LE MUR TUE, ET IL TUE TOUJOURS.
     *
     * 2020 écrit ce test-ci hors de toute condition d'invincibilité :
     * `if (tooCloseFromWall(...)) done = 1;`. Le portage l'avait rangé derrière
     * la même garde que la morsure, et accordait en plus huit secondes de
     * sursis au coup d'envoi — donc l'arène n'avait pas de murs pendant les huit
     * premières secondes. Le symptôme se lisait sur la courbe : une graine sur
     * cinq quittait le terrain à 2,5 s, dérivait dans le vide, et mourait pile
     * à la huitième seconde avec un score de zéro.
     */
    snake g;
    snake_reset(&g, 5, false);
    CHECK(g.invincible <= 0.0f, "aucun sursis au coup d'envoi (%.2f)", (double)g.invincible);

    /* Tout droit : la tête part du centre vers le haut, 486 px à 210 px/s. */
    snake_hold(&g, false, false, false);
    run(&g, 3.0f);
    CHECK(g.phase == SNAKE_DEAD, "il se tue contre le mur du haut en moins de 3 s");

    /* Et le sursis n'y change rien : c'est le point de la correction. */
    snake i;
    snake_reset(&i, 5, false);
    i.invincible = 8.0f;
    i.phase = SNAKE_PLAYING;
    snake_hold(&i, false, false, false);
    run(&i, 3.0f);
    CHECK(i.phase == SNAKE_DEAD, "même invincible, le mur tue");
}

/*
 * Allonge le corps À LA MAIN, en posant les segments en file derrière la tête.
 *
 * Le premier jet faisait grandir le serpent en le nourrissant, ce qui demandait
 * de le faire vivre plusieurs secondes — donc de le faire tourner, donc de le
 * faire se mordre : le montage mesurait la digestion et la manœuvre en croyant
 * préparer une morsure. Ici la longueur est une donnée du test, pas un résultat.
 */
static void grow_to(snake *g, uint32_t want)
{
    if (want >= SNAKE_MAX_PARTS) want = SNAKE_MAX_PARTS - 1;
    while (g->parts < want) {
        const uint32_t k = g->parts;
        g->part[k].x = g->part[k - 1].x;
        g->part[k].y = g->part[k - 1].y + 5.0f;   /* SPEED_DECOMPOSITION */
        g->part[k].radius = 0.0f;
        g->parts++;
    }
}

static void test_la_potion_est_une_vie(void)
{
    /*
     * `nbPotion` DE 2020 NE SERT QU'À SURVIVRE À SA PROPRE QUEUE, et c'est le
     * seul endroit où `NB_FRAME_INVINCIBILITY` est accordé :
     *
     *     if (frameUnkillable == 0 && hitboxTail(...)) {
     *         if (nbPotion > 0) { nbPotion--; frameUnkillable = NB_FRAME_INVINCIBILITY; }
     *         else done = 1;
     *     }
     *
     * Le portage en avait fait un élargissement de la hitbox des FRUITS, ce qui
     * lui donnait exactement l'effet inverse de celui prévu : elle faisait
     * manger plus, donc grossir plus vite, donc mourir plus tôt. Elle rend
     * maintenant ce qu'elle promet — une vie — et c'est elle qui autorise une
     * partie longue.
     */
    snake g;
    snake_reset(&g, 9, false);
    g.phase = SNAKE_PLAYING;
    grow_to(&g, SNAKE_PRE + (uint32_t)90);
    CHECK(g.parts > SNAKE_PRE + 80u, "le corps est assez long pour se mordre (%u)", g.parts);

    g.potions = 1;
    /* Un segment planté sur la tête, au-delà des trente-cinq sautés. */
    for (uint32_t k = SNAKE_PRE + 40u; k < SNAKE_PRE + 50u && k < g.parts; ++k) {
        g.part[k].x = g.part[SNAKE_PRE].x;
        g.part[k].y = g.part[SNAKE_PRE].y - 20.0f;
    }
    run(&g, 0.1f);
    CHECK(g.phase != SNAKE_DEAD, "la potion encaisse la morsure");
    CHECK(g.potions == 0, "et elle est dépensée (%d restante)", g.potions);
    CHECK(fabsf(g.invincible - 8.0f) < 0.3f,
          "elle paie les 8 s de NB_FRAME_INVINCIBILITY (%.2f)", (double)g.invincible);

    /* Puis le compteur descend d'une seconde par seconde, et s'épuise. */
    snake_hold(&g, false, true, false);
    run(&g, 2.0f);
    CHECK(fabsf(g.invincible - 6.0f) < 0.2f,
          "l'invincibilité descend d'une seconde par seconde (%.2f)", (double)g.invincible);
    run(&g, 6.2f);
    CHECK(g.invincible <= 0.0f, "et elle s'épuise après 8 s (%.2f)", (double)g.invincible);
}

static void test_mort_dans_sa_queue(void)
{
    /*
     * La morsure, testée SUR SA RÈGLE plutôt que sur une mise en situation.
     *
     * Premier jet : faire tourner le serpent en rond jusqu'à ce qu'il se morde.
     * Il ne se mord jamais, et c'est CORRECT — le rayon de virage vaut
     * 210 / 3,9 = 54 px, donc un tour fait 338 px, quand un serpent de trente
     * segments espacés de 5 px n'en mesure que 150. Un serpent court tourne
     * librement, exactement comme dans l'original ; il faut manger pour devenir
     * assez long pour se rattraper. Le test mesurait donc la longueur du
     * serpent en croyant mesurer la hitbox.
     *
     * On pose donc un segment à une distance connue de la tête, et on regarde.
     */
    /* Le rayon de morsure de 2020 : `2 * BODY_RADIUS - HITBOX_GENTILLE`, soit
     * 37,67 px, et non les 35 de `BODY_DEATH_HITBOX`. */
    const float R = 37.67f;

    /*
     * `BODY_DEATH_HITBOX` EST UN NOMBRE DE SEGMENTS, PAS UNE DISTANCE.
     *
     * 2020 boucle `for (i = BODY_DEATH_HITBOX + SIZE_PRE_RADIUS; i < size; i++)`
     * : les trente-cinq segments qui suivent la tête sont ignorés, soit 175 px
     * de corps. Le portage l'avait lu comme un rayon et n'en sautait que treize
     * — 65 px — ce qui interdisait tout demi-tour serré : le virage du serpent a
     * un rayon de 54 px et décrit donc un arc de 169 px, trente-quatre segments,
     * qu'un saut de treize déclare mortel.
     *
     * Ce test plante donc son segment APRÈS les trente-cinq sautés, et il doit
     * d'abord faire grandir le serpent : à sa longueur de départ — trente
     * segments — l'index n'existe même pas, et c'est correct. Un serpent neuf ne
     * peut pas se mordre.
     *
     * Deux précautions, apprises en écrivant :
     *
     * — on remplit PLUSIEURS index consécutifs, parce que chaque pas décale
     *   tout le corps d'une case : un seul segment posé se serait déplacé avant
     *   d'être regardé ;
     * — on avance une dizaine de ticks, parce que la collision se teste par PAS
     *   de 5 px et qu'un tick à 120 Hz n'en parcourt que 1,75.
     */
    #define PLANT(g, X, Y)                                                     \
        do { for (uint32_t k = SNAKE_PRE + 40u; k < SNAKE_PRE + 50u            \
                                             && k < (g).parts; ++k) {          \
                 (g).part[k].x = (X); (g).part[k].y = (Y); } } while (0)

    /* Un serpent NEUF ne peut pas se mordre : il est plus court que les
     * trente-cinq segments sautés. C'est la règle, pas une tolérance. */
    {
        snake g;
        snake_reset(&g, 11, false);
        g.phase = SNAKE_PLAYING;
        CHECK(g.parts <= SNAKE_PRE + (uint32_t)35,
              "trente segments, c'est moins que les trente-cinq sautés (%u)", g.parts);
    }

    /* Droit devant, à portée : ça tue. Le serpent part vers le HAUT. */
    {
        snake g;
        snake_reset(&g, 11, false);
        g.phase = SNAKE_PLAYING;
        grow_to(&g, SNAKE_PRE + (uint32_t)90);
        CHECK(g.parts > SNAKE_PRE + 80u, "le corps est assez long pour l'expérience (%u)", g.parts);
        PLANT(g, g.part[SNAKE_PRE].x, g.part[SNAKE_PRE].y - R * 0.7f);
        run(&g, 0.1f);
        CHECK(g.phase == SNAKE_DEAD, "un segment à moins de 37,67 px de la tête la tue");
        CHECK(g.died, "et la mort est signalée");
    }

    /* Loin sur le côté : ça ne tue pas. Sans ce second cas, une hitbox infinie
     * passerait le premier. */
    {
        snake g;
        snake_reset(&g, 11, false);
        g.phase = SNAKE_PLAYING;
        grow_to(&g, SNAKE_PRE + (uint32_t)90);
        PLANT(g, g.part[SNAKE_PRE].x + R * 8.0f, g.part[SNAKE_PRE].y);
        run(&g, 0.1f);
        CHECK(g.phase != SNAKE_DEAD, "un segment à 300 px sur le côté ne tue pas");
    }

    /* Et sous invincibilité — celle que paie une potion — la morsure ne tue
     * pas. C'est la SEULE chose dont elle protège : voir le mur, à côté. */
    {
        snake g;
        snake_reset(&g, 11, false);
        g.phase = SNAKE_PLAYING;
        grow_to(&g, SNAKE_PRE + (uint32_t)90);
        g.invincible = 8.0f;
        PLANT(g, g.part[SNAKE_PRE].x, g.part[SNAKE_PRE].y - R * 0.7f);
        run(&g, 0.1f);
        CHECK(g.phase != SNAKE_DEAD, "pendant l'invincibilité, se mordre ne tue pas");
    }
    #undef PLANT
}

static void test_digestion_allonge(void)
{
    /*
     * Manger allonge — mais PAS tout de suite : le rayon traverse d'abord les
     * 31 cases du tuyau de digestion. C'est ce délai qui fait qu'on voit la
     * bosse descendre, et c'est la signature visuelle du jeu.
     */
    snake g;
    snake_reset(&g, 3, false);
    const uint32_t before = g.parts;

    /* On dépose à la main la courbe d'un fruit copieux, sans dépendre du hasard
     * des apparitions : le test porte sur la digestion, pas sur le tirage. */
    for (int i = 0; i < SNAKE_PRE; ++i) g.part[i].radius += 2.0f;

    run(&g, 0.05f);
    CHECK(g.parts == before, "il ne s'allonge pas dans l'instant (%u -> %u)", before, g.parts);

    run(&g, 6.0f);
    CHECK(g.parts > before, "puis il s'allonge (%u -> %u)", before, g.parts);
    CHECK(g.parts < before + 200, "et pas de façon absurde (%u)", g.parts);
}

static void test_determinisme(void)
{
    /*
     * Même graine, mêmes entrées, même partie. C'est la condition pour que le
     * journal signé veuille dire quelque chose : le serveur rejoue la partie et
     * doit retrouver le même score.
     */
    snake a, b;
    snake_reset(&a, 987654321u, false);
    snake_reset(&b, 987654321u, false);

    for (int i = 0; i < 10000; ++i) {
        const bool left = (i / 37) % 3 == 0;
        const bool right = (i / 53) % 4 == 0;
        snake_hold(&a, left, right, false);
        snake_hold(&b, left, right, false);
        snake_tick(&a, STEP);
        snake_tick(&b, STEP);
    }
    CHECK(a.score == b.score, "même score (%lld contre %lld)",
          (long long)a.score, (long long)b.score);
    CHECK(a.parts == b.parts, "même longueur (%u contre %u)", a.parts, b.parts);
    CHECK(a.part[SNAKE_PRE].x == b.part[SNAKE_PRE].x
       && a.part[SNAKE_PRE].y == b.part[SNAKE_PRE].y, "même position, au bit près");

    /*
     * Et une graine différente donne une partie différente — sinon le
     * déterminisme ne prouverait que l'immobilité.
     *
     * On compare les FRUITS et pas le score : le déplacement du serpent ne tire
     * rien au sort, il ne dépend que des entrées. La graine ne pilote que les
     * apparitions, et c'est donc là qu'il faut regarder. Comparer les scores
     * aurait donné deux zéros identiques et un test qui passe pour rien.
     */
    snake c;
    snake_reset(&c, 123456789u, false);
    snake d;
    snake_reset(&d, 987654321u, false);
    run(&c, 12.0f);
    run(&d, 12.0f);

    bool same = true;
    for (int i = 0; i < SNAKE_MAX_FRUITS; ++i) {
        if (c.fruit[i].id != d.fruit[i].id
            || c.fruit[i].x != d.fruit[i].x || c.fruit[i].y != d.fruit[i].y) {
            same = false;
            break;
        }
    }
    CHECK(!same, "une autre graine pose les fruits ailleurs");
}

static void test_autopilote_tient_une_minute(void)
{
    snake g;
    snake_reset(&g, 24680u, false);

    int alive_steps = 0;
    for (int i = 0; i < 120 * 60; ++i) {
        snake_autopilot(&g);
        snake_tick(&g, STEP);
        if (g.phase != SNAKE_DEAD) alive_steps++;

        /* Aucun NaN ne doit s'installer, et le corps reste sur le terrain. */
        const snake_part *h = &g.part[SNAKE_PRE];
        if (!(h->x == h->x) || !(h->y == h->y)) { CHECK(false, "NaN au pas %d", i); break; }
    }
    CHECK(alive_steps > 120 * 20,
          "le joueur automatique tient au moins vingt secondes (%.1f s)",
          (double)alive_steps / 120.0);
    CHECK(g.score > 0, "et il marque (%lld)", (long long)g.score);
    CHECK(g.parts < SNAKE_MAX_PARTS, "sans faire déborder le corps (%u)", g.parts);
}

static void test_le_bareme_correspond_au_score(void)
{
    /*
     * L'invariant que le serveur recalcule : la somme des valeurs émises vaut le
     * score. S'il est faux, TOUTE partie est rejetée — et silencieusement, la
     * soumission étant « au mieux, jamais bloquante ».
     */
    snake g;
    snake_reset(&g, 4242u, false);

    int64_t sum = 0;
    for (int i = 0; i < 120 * 60; ++i) {
        snake_autopilot(&g);
        snake_tick(&g, STEP);

        ns_game_events ev; SDL_zero(ev);
        g_snake_api.events(&g, &ev);
        if (ev.score) {
            /* Le bonus est à barème FIXE côté serveur (50 points) ; sa valeur ne
             * voyage pas, donc on la compte comme le serveur la comptera. */
            sum += (SDL_strcmp(ev.score_kind, "bonus") == 0) ? 50 : ev.score_value;
        }
        if (g.phase == SNAKE_DEAD) break;
    }
    CHECK(sum == g.score, "la somme des gains émis vaut le score (%lld contre %lld)",
          (long long)sum, (long long)g.score);
}

static void test_hardcore_est_l_inverse(void)
{
    /*
     * LA règle qu'on casserait sans s'en rendre compte : en hardcore, manger un
     * fruit COÛTE cinq fois sa valeur, et le score vient des fruits qu'on laisse
     * expirer.
     *
     * Le premier jet la mesurait en comparant DEUX PILOTES — l'un et l'autre
     * courant après les fruits — et concluait « en hardcore la même stratégie
     * rapporte moins ». C'était vrai et sans portée : ça comparait deux façons
     * de jouer contre la règle. La règle se teste sur elle-même, en posant un
     * fruit à un endroit connu et en regardant ce qui sort.
     */
    {
        /* Le même fruit, mangé dans les deux modes. */
        snake n, h;
        snake_reset(&n, 31337u, false);
        snake_reset(&h, 31337u, true);
        for (int i = 0; i < SNAKE_MAX_FRUITS; ++i) { n.fruit[i].id = -1; h.fruit[i].id = -1; }
        n.phase = h.phase = SNAKE_PLAYING;
        /* La pomme vaut 107 : positive en normal, cinq fois négative en hardcore. */
        const float hx = n.part[SNAKE_PRE].x, hy = n.part[SNAKE_PRE].y - 12.0f;
        n.fruit[0] = (snake_fruit){ hx, hy, hx, hy, 5 /* POMME */, false, false, 0.1f, 1.0f };
        h.fruit[0] = (snake_fruit){ hx, hy, hx, hy, 5, false, false, 0.1f, 1.0f };
        /* En hardcore on part avec une réserve, sinon la perte est rognée à
         * zéro — ce qui est l'autre règle, testée juste en dessous. */
        h.score = 100000;

        run(&n, 0.1f);
        run(&h, 0.1f);
        CHECK(n.score > 0, "en normal, manger une pomme rapporte (%lld)", (long long)n.score);
        CHECK(h.score < 100000, "en hardcore, manger la même pomme coûte (%lld)",
              (long long)(h.score - 100000));
        CHECK(h.score - 100000 == -5 * n.score,
              "et il coûte exactement cinq fois ce qu'il rapporterait (%lld contre %lld)",
              (long long)(h.score - 100000), (long long)n.score);
    }

    {
        /*
         * Le même fruit, laissé POURRIR en hardcore : il rapporte le quart.
         *
         * Le montage doit empêcher la cadence hardcore — une apparition toutes
         * les deux dixièmes de seconde — de venir brouiller le compte. Les
         * vingt-trois autres cases sont donc occupées par des PLUMES, qui valent
         * zéro point : le terrain est plein, `spawn_fruit` ne trouve plus de
         * place, et le seul fruit qui puisse bouger le score est la pomme.
         */
        snake h;
        snake_reset(&h, 31337u, true);
        h.phase = SNAKE_PLAYING;
        for (int i = 1; i < SNAKE_MAX_FRUITS; ++i) {
            const float px = 100.0f + 60.0f * (float)i, py = 100.0f;
            h.fruit[i] = (snake_fruit){ px, py, px, py, 25 /* PLUME */, false, false, 0.0f, 1.0f };
        }
        /* La pomme est déjà vieille : elle expire dans un dixième de seconde,
         * bien avant les plumes, donc avant qu'une case ne se libère. */
        h.fruit[0] = (snake_fruit){ 200.0f, 900.0f, 200.0f, 900.0f, 5 /* POMME */,
                                    false, false, 6.9f, 1.0f };
        const int64_t before = h.score;
        run(&h, 0.3f);
        /* La case est déjà reprise par un fruit neuf quand on regarde : c'est la
         * règle du terrain jamais vide, qui rejoue aussitôt. On lit donc l'ÂGE,
         * qui dit que ce n'est plus la même pomme. */
        CHECK(h.fruit[0].age < 1.0f, "la pomme a pourri et la case a resservi (%.2f)",
              (double)h.fruit[0].age);
        CHECK(h.score - before == 26,
              "elle rapporte le quart de ses 107 points, arrondi comme 2020 (%lld)",
              (long long)(h.score - before));
    }

    /*
     * ON NE DOIT RIEN À LA SALLE : le total ne passe pas sous zéro, et la
     * VALEUR ÉMISE est rognée avec lui.
     *
     * C'est la seconde moitié qui compte. `runs.go` additionne les valeurs
     * reçues et ne ramène le total à zéro qu'à la fin ; si le client écrêtait
     * son total sans écrêter l'événement, les deux additions divergeraient et la
     * partie serait refusée avec un « score incohérent » que rien ne laisse
     * prévoir. On vérifie donc les deux ensemble, sur une partie entière.
     */
    {
        snake h;
        snake_reset(&h, 555u, true);
        int64_t sum = 0;
        bool saw_negative = false, saw_positive = false;
        for (int i = 0; i < 120 * 120; ++i) {
            snake_autopilot(&h);
            snake_tick(&h, STEP);
            ns_game_events ev; SDL_zero(ev);
            g_snake_api.events(&h, &ev);
            if (ev.score) {
                sum += (SDL_strcmp(ev.score_kind, "bonus") == 0) ? 50 : ev.score_value;
                if (ev.score_value < 0) saw_negative = true;
                if (ev.score_value > 0) saw_positive = true;
            }
            CHECK_SILENT(h.score >= 0);
            if (h.phase == SNAKE_DEAD) break;
        }
        CHECK(h.score >= 0, "le total interne ne descend jamais sous zéro (%lld)",
              (long long)h.score);
        CHECK(sum == h.score,
              "la somme des valeurs émises vaut le total, écrêtage compris (%lld contre %lld)",
              (long long)sum, (long long)h.score);
        CHECK(saw_negative, "manger en hardcore émet une valeur négative");
        CHECK(saw_positive, "et laisser pourrir en émet une positive");
        CHECK(g_snake_api.score(&h) == (uint32_t)h.score,
              "le score exposé est le total (%u pour %lld)",
              g_snake_api.score(&h), (long long)h.score);
    }
}

static void test_bornes_du_serveur(void)
{
    /*
     * Les valeurs émises doivent tenir dans les bornes que `runs.go` déclare
     * pour Snake : −100 000 à 100 000. Le muffin rose vaut 10 000, ×10 en géant,
     * ×(−5) en hardcore — donc −500 000 serait possible si l'on cumulait les
     * deux. On vérifie que ça n'arrive pas.
     */
    for (int hard = 0; hard < 2; ++hard) {
        snake g;
        snake_reset(&g, 8080u + (uint64_t)hard, hard != 0);
        for (int i = 0; i < 120 * 90; ++i) {
            snake_autopilot(&g);
            snake_tick(&g, STEP);
            ns_game_events ev; SDL_zero(ev);
            g_snake_api.events(&g, &ev);
            if (ev.score && ev.score_value != 0) {
                CHECK(ev.score_value >= -100000 && ev.score_value <= 100000,
                      "%s : la valeur émise tient dans les bornes du serveur (%lld)",
                      hard ? "hardcore" : "normal", (long long)ev.score_value);
                if (ev.score_value < -100000 || ev.score_value > 100000) return;
            }
            if (g.phase == SNAKE_DEAD) snake_reset(&g, 8080u + (uint64_t)i, hard != 0);
        }
    }
    CHECK(true, "quatre-vingt-dix secondes de jeu restent dans les bornes");
}

int main(void)
{
    test_depart();
    test_rotation();
    test_vitesse_independante_du_pas();

    test_mort_contre_le_mur();
    test_la_potion_est_une_vie();
    test_mort_dans_sa_queue();
    test_digestion_allonge();
    test_determinisme();
    test_autopilote_tient_une_minute();
    test_le_bareme_correspond_au_score();
    test_hardcore_est_l_inverse();
    test_bornes_du_serveur();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
