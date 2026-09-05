/* shooter.c — voir shooter.h pour ce qui est repris de 2020 et ce qui ne l'est pas. */
#include "shooter.h"

#include "ns_core.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * Les tables de 2020, converties
 * ==========================================================================
 * Tout est en IMAGES à 30 Hz dans l'original. Chaque constante garde son nom et
 * sa valeur, le facteur de conversion à côté.
 * ========================================================================== */

#define FPS30 30.0f

/* --- vaisseau ------------------------------------------------------------ */
#define SHIP_SPEED        10.0f              /* px/image -> 300 px/s */
#define SHIP_RATE         (SHIP_SPEED * FPS30)
#define RATIO_SPEED_DOWN  0.9f               /* amortissement par image */
#define SHIP_DAMP_PER_S   (FPS30 * 0.10536f) /* -ln(0,9) x 30 */
#define SHIP_RADIUS       26.0f
#define SHIP_MARGIN       34.0f

/*
 * `WEAPON_DISPOSITION` : selon le NOMBRE d'armes, lesquels des cinq
 * emplacements s'allument. C'est ce qui fait que deux canons ne sont pas les
 * deux premiers mais le deuxième et le quatrième — donc symétriques.
 * Les indices de 2020 partent à 1 ; on retranche 1, et 0 veut dire « aucun ».
 */
static const int WEAPON_DISPOSITION[SH_MAX_WEAPONS][SH_MAX_WEAPONS] = {
    { 3, 0, 0, 0, 0 },
    { 2, 4, 0, 0, 0 },
    { 3, 2, 4, 0, 0 },
    { 1, 2, 4, 5, 0 },
    { 3, 2, 4, 1, 5 },
};
/* `WEAPON_DEST` : l'abscisse de chaque emplacement sur la coque de 132 px. */
static const float WEAPON_X[SH_MAX_WEAPONS] = { 36.0f, 49.0f, 64.0f, 80.0f, 93.0f };
#define SHIP_ART_W 132.0f

int shooter_weapon_slots(int weapons, int slot_out[SH_MAX_WEAPONS])
{
    if (weapons < 1) weapons = 1;
    if (weapons > SH_MAX_WEAPONS) weapons = SH_MAX_WEAPONS;
    int n = 0;
    for (int i = 0; i < SH_MAX_WEAPONS; ++i) {
        const int slot = WEAPON_DISPOSITION[weapons - 1][i];
        if (slot <= 0) continue;
        slot_out[n++] = slot - 1;
    }
    return n;
}

/* --- missiles ------------------------------------------------------------ */
/*
 * Les demi-largeurs de `MISSILE_HITBOX`, et il fallait lire le ratio.
 *
 * L'ellipse du missile ennemi de base s'écrit `{0, 0, 12.5*RATIO_SIZE_MISSILE_3,
 * 12.5*RATIO_SIZE_MISSILE_3}` — et `RATIO_SIZE_MISSILE_3` vaut **0,4**. Son
 * rayon est donc de 5 pixels, pas de 12,5. Le premier jet a recopié le facteur
 * 12,5 en oubliant le ratio : chaque balle ennemie était deux fois et demie
 * trop grosse, ce qui, sur un tir en rafale dans un couloir de six cent
 * quarante pixels, ne se corrige par aucune adresse. Le joueur automatique
 * tenait huit secondes.
 *
 * Le laser ennemi est une ellipse de 26 sur 178 : un mur étroit et long, pas un
 * disque de 26. On garde sa demi-largeur.
 */
static const float MISSILE_RADIUS[SH_SHOT_KINDS] = { 6.0f, 6.0f, 6.0f, 5.0f, 13.0f };
/* Les vitesses de 2020 sont en px/image ; le laser ennemi est un mur qui tombe
 * lentement, le missile allié va vite. */
static const float MISSILE_SPEED[SH_SHOT_KINDS] = {
    22.0f * FPS30, 20.0f * FPS30, 17.0f * FPS30, 9.0f * FPS30, 6.0f * FPS30
};
static const float MISSILE_DAMAGE[SH_SHOT_KINDS] = { 1.0f, 1.0f, 1.4f, 1.0f, 2.0f };

/* --- ennemis ------------------------------------------------------------- */
static const float ENEMY_HP[SH_ENEMY_KINDS]    = { 1.0f, 7.0f, 25.0f, 40.0f, 90.0f };
static const float ENEMY_SPEED[SH_ENEMY_KINDS] = { 5.3f, 6.3f, 5.0f, 1.0f, 0.0f };
static const float ENEMY_RADIUS[SH_ENEMY_KINDS] = { 26.0f, 44.0f, 74.0f, 40.0f, 150.0f };
static const int   ENEMY_WEAPONS[SH_ENEMY_KINDS] = { 1, 4, 7, 1, 2 };

/* `TYPE_ENEMY_FIRE` : visé, tout droit, ou vertical. */
enum { AIMED = 0, STRAIGHT, VERTICAL };
static const int TYPE_ENEMY_FIRE[SH_ENEMY_KINDS][7] = {
    { AIMED },
    { AIMED, VERTICAL, VERTICAL, AIMED },
    { AIMED, AIMED, AIMED, VERTICAL, AIMED, AIMED, AIMED },
    { STRAIGHT },
    { STRAIGHT, STRAIGHT },
};
/* `RELOAD_FRAME_ENEMY`, en images -> secondes. */
static const float RELOAD_ENEMY[SH_ENEMY_KINDS][7] = {
    { 50 },
    { 40, 80, 80, 40 },
    { 66, 66, 66, 100, 66, 66, 66 },
    { 500 },
    { 15, 15 },
};
/* `RELOAD_FRAME_COMBO_ENEMY` : l'écart DANS la rafale, par arme et en images.
 * Il vaut 15 pour le laser de l'ennemi 2 et 2 ou 3 pour ses canons — un écart
 * unique pour tous rendait ses sept armes strictement injouables, et c'est ce
 * que le premier jet a produit : cinq morts en trente secondes. */
static const float BURST_GAP[SH_ENEMY_KINDS][7] = {
    { 0 },
    { 2, 8, 8, 2 },
    { 3, 3, 2, 15, 2, 3, 3 },
    { 6 },
    { 0, 0 },
};
static const int COMBO_ENEMY[SH_ENEMY_KINDS][7] = {
    { 0 },
    { 4, 1, 1, 4 },
    { 3, 3, 4, 2, 4, 3, 3 },
    { 2 },
    { 12, 12 },
};
/* Quel missile ennemi : `W_BASE_ENEMY` ou `W_LASER_ENEMY`. */
static const int WEAPON_KIND_ENEMY[SH_ENEMY_KINDS][7] = {
    { SH_ENEMY_BASE },
    { SH_ENEMY_BASE, SH_ENEMY_LASER, SH_ENEMY_LASER, SH_ENEMY_BASE },
    { SH_ENEMY_BASE, SH_ENEMY_BASE, SH_ENEMY_BASE, SH_ENEMY_LASER,
      SH_ENEMY_BASE, SH_ENEMY_BASE, SH_ENEMY_BASE },
    { SH_ENEMY_LASER },
    { SH_ENEMY_BASE, SH_ENEMY_BASE },
};

/* Le barème, tel que `rulesTable["shooter"]` l'écrit depuis M6 : quinze points
 * par point de vie. Un ennemi à 90 vaut 1 350, un ennemi à 1 en vaut 15. */
#define PTS_PER_HP 15
#define PTS_BOSS   2000
#define PTS_WAVE   300

#define WAVE_PERIOD_BASE 5.5f
#define BOSS_EVERY 5u

/* Voir le commentaire de `die` : trois vies, c'est le plus petit nombre qui
 * amène le joueur jusqu'au boss de la cinquième vague. */
#define SH_LIVES 3

/* ==========================================================================
 * Aides
 * ========================================================================== */

static float frandf(ns_rng *r, float lo, float hi)
{
    return lo + (hi - lo) * (float)ns_rng_below(r, 10000) / 10000.0f;
}

int shooter_live_enemies(const shooter *g)
{
    int n = 0;
    for (int i = 0; i < SH_MAX_ENEMIES; ++i) if (g->enemy[i].alive) n++;
    return n;
}

static sh_shot *free_shot(shooter *g)
{
    for (int i = 0; i < SH_MAX_SHOTS; ++i) if (!g->shot[i].alive) return &g->shot[i];
    return NULL;
}

static sh_enemy *free_enemy(shooter *g)
{
    for (int i = 0; i < SH_MAX_ENEMIES; ++i) if (!g->enemy[i].alive) return &g->enemy[i];
    return NULL;
}

/* ==========================================================================
 * Les vagues
 * ========================================================================== */

static void spawn_enemy(shooter *g, int kind, float x)
{
    sh_enemy *e = free_enemy(g);
    if (!e) return;
    SDL_zerop(e);
    e->kind = kind;
    e->hp = e->hp_max = ENEMY_HP[kind] * (g->hard ? 1.4f : 1.0f);
    e->x = x;
    e->y = -ENEMY_RADIUS[kind] - 20.0f;
    e->vx = 0.0f;
    e->vy = ENEMY_SPEED[kind] * FPS30;
    /* Les gros descendent puis TIENNENT leur ligne : c'est ce que font
     * `SPAWN_ENEMY_Y` et `TARGET_ENEMY_Y` de 2020, où le boss arrive de très
     * haut et s'arrête en haut de l'écran. */
    e->target_y = (kind >= 2) ? (SH_H * (kind == 4 ? 0.18f : 0.22f)) : -1.0f;

    /*
     * LE BOSS QUI N'ARRIVAIT JAMAIS.
     *
     * `ENEMY_SPEED[4]` vaut 0, et c'est fidèle à 2020 : là-bas, c'est la
     * vitesse du boss UNE FOIS EN PLACE, parce que l'original le fait
     * APPARAÎTRE sur sa ligne. Ici, tous les ennemis entrent par le haut —
     * `e->y` part à `-ENEMY_RADIUS - 20`. Pour le boss, ça fait −170, et une
     * vitesse d'entrée de zéro l'y laissait POUR TOUJOURS.
     *
     * Mesuré, graine 1000, pilote automatique : à 30 s, 40 s et 50 s de partie,
     * le boss est toujours à y = −170 pour une cible à 194, avec ses 90 points
     * de vie intacts. Il ne pouvait pas les perdre : les missiles alliés sont
     * retirés dès `y < -60`, donc aucun ne l'atteignait jamais. Et comme les
     * vagues sont suspendues tant que `boss_alive` est vrai, la partie restait
     * bloquée à la vague 5, score gelé, pendant les 128 secondes suivantes.
     *
     * Un boss invisible, invulnérable, et qui arrête le jeu : les trois d'un
     * coup, à cause d'un zéro qui voulait dire autre chose.
     *
     * 130 px/s met 2,8 s à l'amener de −170 à sa ligne — le temps de le voir
     * arriver. La clause `target_y` existante l'y arrête ensuite, ce qui rend
     * bien la vitesse nulle de 2020 : immobile UNE FOIS EN PLACE.
     */
    if (e->target_y > 0.0f && e->vy <= 0.0f) e->vy = 130.0f;
    for (int w = 0; w < ENEMY_WEAPONS[kind] && w < 7; ++w) {
        e->reload[w] = RELOAD_ENEMY[kind][w] / FPS30 * frandf(&g->rng, 0.6f, 1.4f);
        e->burst[w] = 0;
    }
    e->alive = true;
}

static void spawn_wave(shooter *g)
{
    g->wave++;
    g->pend_wave++;
    g->score += PTS_WAVE;

    /*
     * UNE ARME TOUTES LES DEUX VAGUES — la sortie d'une impasse.
     *
     * Jusqu'ici, la SEULE source d'armement était `kill_enemy` quand on abat un
     * boss. Et le premier boss arrive vague 5. Autrement dit : pour renforcer
     * son tir il fallait tuer un boss, et pour tuer un boss il aurait fallu un
     * tir renforcé. La table `WEAPON_DISPOSITION`, ses cinq emplacements et les
     * trois types de missiles — tout ce que l'en-tête décrit longuement —
     * étaient inatteignables : `weapons` valait 1 du début à la fin de chaque
     * partie jamais jouée.
     *
     * Le compte : un emplacement tire un missile toutes les 0,18 s à un point
     * de dégât, soit 5,5 points par seconde. Le boss en a 90. Il fallait donc
     * SEIZE SECONDES d'alignement parfait sous une rafale de douze, avec le
     * tir le plus faible du jeu — quand toutes les vagues d'avant se nettoient
     * en moins de cinq. Ce n'était pas un boss difficile, c'était un mur.
     *
     * Une arme aux vagues 2 et 4 met trois emplacements en face du premier
     * boss : 16,6 points par seconde, cinq secondes et demie de combat. Les
     * boss abattus continuent d'en donner, ce qui garde la récompense.
     */
    if ((g->wave % 2u) == 0u && g->weapons < SH_MAX_WEAPONS) g->weapons++;

    if (g->wave % BOSS_EVERY == 0) {
        /* Le boss : l'ennemi 4, quatre-vingt-dix points de vie, immobile en
         * haut de l'écran et tirant en rafales de douze. */
        spawn_enemy(g, 4, SH_W * 0.5f);
        g->boss_alive = true;
        return;
    }

    const int count = 3 + (int)ns_rng_below(&g->rng, 3u + (g->wave / 2u));
    for (int i = 0; i < count && i < 10; ++i) {
        /* La difficulté fait monter la proportion d'ennemis lourds. */
        /*
         * Les lourds arrivent TARD. L'ennemi 2 porte sept armes : en faire
         * apparaître dès la première vague ne donne pas un jeu difficile, ça
         * donne un mur. Il attend la troisième, l'ennemi 1 la deuxième.
         */
        const uint32_t roll = ns_rng_below(&g->rng, 100);
        int kind = 0;
        if (g->wave >= 3u && roll > 90u - (g->wave > 9u ? 18u : (g->wave - 3u) * 3u)) kind = 2;
        else if (g->wave >= 2u && roll > 62u) kind = 1;
        spawn_enemy(g, kind, frandf(&g->rng, 70.0f, SH_W - 70.0f));
    }
}

/* ==========================================================================
 * Le tir
 * ========================================================================== */

static void fire_ally(shooter *g)
{
    int slots[SH_MAX_WEAPONS];
    const int n = shooter_weapon_slots(g->weapons, slots);
    for (int i = 0; i < n; ++i) {
        sh_shot *s = free_shot(g);
        if (!s) return;
        SDL_zerop(s);
        /* L'emplacement, ramené au centre de la coque puis à l'échelle. */
        s->x = g->ship_x + (WEAPON_X[slots[i]] - SHIP_ART_W * 0.5f) * 0.62f;
        s->y = g->ship_y - 26.0f;
        s->kind = g->ammo_kind;
        s->vx = 0.0f;
        s->vy = -MISSILE_SPEED[s->kind];
        s->damage = MISSILE_DAMAGE[s->kind];
        s->radius = MISSILE_RADIUS[s->kind];
        s->life = 3.0f;
        s->hostile = false;
        s->alive = true;
    }
    g->fired = true;
}

static void fire_enemy(shooter *g, const sh_enemy *e, int w)
{
    sh_shot *s = free_shot(g);
    if (!s) return;
    const int kind = WEAPON_KIND_ENEMY[e->kind][w];
    const int type = TYPE_ENEMY_FIRE[e->kind][w];

    SDL_zerop(s);
    s->x = e->x + (float)(w - ENEMY_WEAPONS[e->kind] / 2) * 16.0f;
    s->y = e->y + ENEMY_RADIUS[e->kind] * 0.6f;
    s->kind = kind;
    s->damage = MISSILE_DAMAGE[kind];
    s->radius = MISSILE_RADIUS[kind];
    s->life = 5.0f;
    s->hostile = true;
    s->alive = true;

    const float speed = MISSILE_SPEED[kind];
    if (type == AIMED) {
        const float dx = g->ship_x - s->x, dy = g->ship_y - s->y;
        const float d = sqrtf(dx * dx + dy * dy);
        s->vx = (d > 1e-3f) ? dx / d * speed : 0.0f;
        s->vy = (d > 1e-3f) ? dy / d * speed : speed;
    } else {
        /* STRAIGHT et VERTICAL descendent tous deux ; ce qui les distingue chez
         * l'original est la planche employée, pas la trajectoire. */
        s->vx = 0.0f;
        s->vy = speed;
    }
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

void shooter_reset(shooter *g, uint64_t seed, bool hard)
{
    memset(g, 0, sizeof *g);
    ns_rng_seed(&g->rng, seed, 0x5400Eu);
    g->hard = hard;
    g->phase = SH_READY;

    g->ship_x = SH_W * 0.5f;
    g->ship_y = SH_H * 0.82f;
    g->weapons = 1;
    g->ammo_kind = SH_ALLY_BASE;
    g->fire_period = 0.18f;
    g->wave_timer = 1.5f;
    g->invuln = 1.5f;
    g->lives = SH_LIVES;
}

void shooter_press(shooter *g, ns_game_button b)
{
    if (g->phase == SH_DEAD) return;
    if (g->phase == SH_READY) g->phase = SH_PLAYING;
    if (b == NS_GAME_ACTION && g->fire_timer <= 0.0f) {
        fire_ally(g);
        g->fire_timer = g->fire_period;
    }
}

void shooter_hold(shooter *g, const bool held[NS_GAME_BUTTON_COUNT])
{
    for (int i = 0; i < NS_GAME_BUTTON_COUNT; ++i) g->held[i] = held[i];
}

/*
 * LES VIES — ce qui manquait pour que le jeu atteigne son propre contenu.
 *
 * Mesuré, cinq graines, pilote automatique : la partie s'arrête à 10,3 s en
 * normal et 9,2 s en difficile, et le score plafonne à 870 de la douzième
 * seconde jusqu'à la deux-cent-quarantième. Une touche, c'était fini.
 *
 * Or ce jeu a une montée en puissance ÉCRITE, et la lire suffit à voir le
 * problème : l'ennemi 1 arrive vague 2, l'ennemi 2 — celui à sept armes —
 * vague 3, et `BOSS_EVERY` pose un boss toutes les CINQ vagues. Les vagues
 * tombent toutes les 5,5 s moins 0,12 s par vague. L'ennemi 2 apparaît donc
 * vers 16 s et le premier boss vers 22 s. Le boss est la pièce dont l'en-tête
 * du fichier parle le plus longuement — et PERSONNE NE L'AVAIT JAMAIS VU.
 *
 * Trois vies, c'est le contrat d'arcade ordinaire, et c'est le plus petit
 * nombre qui amène au boss : il faut survivre deux vagues de plus.
 *
 * Ce qui est rendu à la mort : la position de départ, l'invulnérabilité
 * d'entrée, et RIEN d'autre. Les armes ramassées ne sont pas rendues, sinon
 * mourir deviendrait indolore. L'écran est vidé de ses missiles hostiles, parce
 * que réapparaître dans une rafale déjà en vol n'est pas une difficulté, c'est
 * une confiscation.
 */
static void die(shooter *g)
{
    if (g->lives > 0) {
        g->lives--;
        g->ship_x = SH_W * 0.5f;
        g->ship_y = SH_H * 0.82f;
        g->ship_vx = 0.0f;
        g->invuln = 2.0f;
        for (int i = 0; i < SH_MAX_SHOTS; ++i) {
            if (g->shot[i].alive && g->shot[i].hostile) g->shot[i].alive = false;
        }
        return;
    }
    g->phase = SH_DEAD;
    g->dead_time = 0.0f;
    g->died = true;
}

static void kill_enemy(shooter *g, sh_enemy *e)
{
    /* La VALEUR d'un ennemi, c'est ses points de vie de base : quinze points
     * par point de vie, comme `scaled{"enemy": 15}` l'écrit côté serveur. */
    const int64_t hp = (int64_t)(ENEMY_HP[e->kind] + 0.5f);
    g->score += hp * PTS_PER_HP;
    g->pend_enemy += hp;
    g->killed++;
    if (e->kind == 4) {
        g->score += PTS_BOSS;
        g->pend_boss++;
        g->boss_alive = false;
        /* Un boss abattu améliore l'armement : c'est la récompense qui donne
         * envie d'y retourner. */
        if (g->weapons < SH_MAX_WEAPONS) g->weapons++;
        if (g->ammo_kind < SH_ALLY_HOMING) g->ammo_kind++;
    }
    e->alive = false;
}

void shooter_tick(shooter *g, float dt)
{
    if (g->phase == SH_DEAD) { g->dead_time += dt; return; }
    g->time += dt;
    g->scroll += dt * 260.0f;
    if (g->phase == SH_READY) return;

    if (g->fire_timer > 0.0f) g->fire_timer -= dt;
    if (g->invuln > 0.0f) g->invuln -= dt;

    /* --- le vaisseau : accélération, amortissement, bords --- */
    const bool left = g->held[NS_GAME_LEFT], right = g->held[NS_GAME_RIGHT];
    if (left && !right) g->ship_vx -= SHIP_RATE * dt * 6.0f;
    if (right && !left) g->ship_vx += SHIP_RATE * dt * 6.0f;
    g->ship_vx *= expf(-SHIP_DAMP_PER_S * dt);
    if (g->ship_vx > SHIP_RATE) g->ship_vx = SHIP_RATE;
    if (g->ship_vx < -SHIP_RATE) g->ship_vx = -SHIP_RATE;
    g->ship_x += g->ship_vx * dt;
    if (g->ship_x < SHIP_MARGIN) { g->ship_x = SHIP_MARGIN; g->ship_vx = 0.0f; }
    if (g->ship_x > SH_W - SHIP_MARGIN) { g->ship_x = SH_W - SHIP_MARGIN; g->ship_vx = 0.0f; }

    /* Le haut et le bas : une marge, pas tout l'écran — un shmup se joue en bas. */
    if (g->held[NS_GAME_UP])   g->ship_y -= SHIP_RATE * 0.55f * dt;
    if (g->held[NS_GAME_DOWN]) g->ship_y += SHIP_RATE * 0.55f * dt;
    if (g->ship_y < SH_H * 0.45f) g->ship_y = SH_H * 0.45f;
    if (g->ship_y > SH_H - 50.0f) g->ship_y = SH_H - 50.0f;

    if (g->held[NS_GAME_ACTION] && g->fire_timer <= 0.0f) {
        fire_ally(g);
        g->fire_timer = g->fire_period;
    }

    /* --- les vagues --- */
    /*
     * Les vagues s'arrêtent pendant un boss — SAUF s'il s'éternise.
     *
     * Le boss tire à la VERTICALE depuis le haut du couloir : le seul endroit
     * d'où on l'atteint est la colonne qu'il balaie en permanence. C'est un
     * duel qui se gagne en échangeant des coups, et un joueur le fait. Le
     * pilote automatique, lui, refuse par construction d'entrer dans une
     * colonne battue : mesuré, il ne passe que 5 % du temps aligné, laisse le
     * boss à 47 points de vie sur 90 et se gare définitivement à 175 px de lui.
     * La partie ne se terminait plus jamais — score figé de la trentième à la
     * deux-cent-quarantième seconde, sans mort. Une borne en mode attraction
     * montrait un combat immobile.
     *
     * `wave_timer` continue de descendre sous zéro pendant le boss : sa valeur
     * dit donc, en secondes et sans champ nouveau, depuis quand il tient. Passé
     * vingt secondes, les vagues reprennent PAR-DESSUS lui. Ce n'est pas une
     * échappatoire offerte au joueur, c'est l'inverse : celui qui n'abat pas
     * son boss se retrouve avec le boss ET la suite.
     */
    g->wave_timer -= dt;
    if (g->wave_timer <= 0.0f && (!g->boss_alive || g->wave_timer < -20.0f)) {
        spawn_wave(g);
        float period = WAVE_PERIOD_BASE - (float)g->wave * 0.12f;
        if (period < 2.2f) period = 2.2f;
        g->wave_timer = period;
    }

    /* --- les ennemis --- */
    for (int i = 0; i < SH_MAX_ENEMIES; ++i) {
        sh_enemy *e = &g->enemy[i];
        if (!e->alive) continue;
        if (e->hit_flash > 0.0f) e->hit_flash -= dt;

        if (e->target_y > 0.0f && e->y >= e->target_y) {
            e->y = e->target_y;
            /* Une fois en place, il DÉRIVE latéralement : un gros immobile est
             * une cible, un gros qui balaie est une menace. */
            e->x += sinf(g->time * 0.9f + (float)i) * 40.0f * dt;
        } else {
            e->y += e->vy * dt;
        }
        if (e->x < ENEMY_RADIUS[e->kind]) e->x = ENEMY_RADIUS[e->kind];
        if (e->x > SH_W - ENEMY_RADIUS[e->kind]) e->x = SH_W - ENEMY_RADIUS[e->kind];

        if (e->y > SH_H + ENEMY_RADIUS[e->kind] + 40.0f) {
            /* Un ennemi qui s'échappe ne rapporte rien — et c'est la punition
             * qui pousse à tirer plutôt qu'à esquiver. */
            e->alive = false;
            if (e->kind == 4) g->boss_alive = false;
            continue;
        }

        for (int w = 0; w < ENEMY_WEAPONS[e->kind] && w < 7; ++w) {
            e->reload[w] -= dt;
            if (e->reload[w] > 0.0f) continue;
            if (e->y < 0.0f) { e->reload[w] = 0.2f; continue; }
            fire_enemy(g, e, w);
            /* La RAFALE : `COMBO_ENEMY` tirs rapprochés, puis le rechargement
             * complet. C'est ce qui donne leur rythme aux ennemis de 2020. */
            if (e->burst[w] < COMBO_ENEMY[e->kind][w]) {
                e->burst[w]++;
                const float gap = BURST_GAP[e->kind][w];
                e->reload[w] = ((gap > 0.0f) ? gap : 2.0f) / FPS30;
            } else {
                e->burst[w] = 0;
                e->reload[w] = RELOAD_ENEMY[e->kind][w] / FPS30;
                if (g->hard) e->reload[w] *= 0.7f;
            }
        }

        /* La collision : un ennemi qui touche le vaisseau le détruit. */
        const float dx = e->x - g->ship_x, dy = e->y - g->ship_y;
        const float rr = ENEMY_RADIUS[e->kind] * 0.65f + SHIP_RADIUS;
        if (dx * dx + dy * dy <= rr * rr && g->invuln <= 0.0f) { die(g); return; }
    }

    /* --- les missiles --- */
    for (int i = 0; i < SH_MAX_SHOTS; ++i) {
        sh_shot *s = &g->shot[i];
        if (!s->alive) continue;
        s->life -= dt;
        if (s->life <= 0.0f) { s->alive = false; continue; }

        if (s->kind == SH_ALLY_ZIGZAG) {
            s->vx = sinf(s->life * 22.0f) * 260.0f;
        } else if (s->kind == SH_ALLY_HOMING) {
            /* La tête chercheuse vise l'ennemi le plus proche DEVANT elle. */
            float best = 1e30f;
            const sh_enemy *t = NULL;
            for (int k = 0; k < SH_MAX_ENEMIES; ++k) {
                if (!g->enemy[k].alive || g->enemy[k].y > s->y) continue;
                const float dx = g->enemy[k].x - s->x, dy = g->enemy[k].y - s->y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; t = &g->enemy[k]; }
            }
            if (t) {
                const float dx = t->x - s->x;
                s->vx += (dx > 0.0f ? 1.0f : -1.0f) * 900.0f * dt;
                if (s->vx > 400.0f) s->vx = 400.0f;
                if (s->vx < -400.0f) s->vx = -400.0f;
            }
        }

        s->x += s->vx * dt;
        s->y += s->vy * dt;
        if (s->y < -60.0f || s->y > SH_H + 60.0f || s->x < -60.0f || s->x > SH_W + 60.0f) {
            s->alive = false;
            continue;
        }

        if (s->hostile) {
            const float dx = s->x - g->ship_x, dy = s->y - g->ship_y;
            const float rr = s->radius + SHIP_RADIUS * 0.7f;
            if (dx * dx + dy * dy <= rr * rr) {
                s->alive = false;
                if (g->invuln <= 0.0f) { die(g); return; }
            }
            continue;
        }

        for (int k = 0; k < SH_MAX_ENEMIES; ++k) {
            sh_enemy *e = &g->enemy[k];
            if (!e->alive) continue;
            const float dx = e->x - s->x, dy = e->y - s->y;
            const float rr = ENEMY_RADIUS[e->kind] * 0.7f + s->radius;
            if (dx * dx + dy * dy > rr * rr) continue;
            e->hp -= s->damage;
            e->hit_flash = 0.08f;
            s->alive = false;
            if (e->hp <= 0.0f) kill_enemy(g, e);
            break;
        }
    }
}

/* ==========================================================================
 * Le joueur automatique
 * ========================================================================== */

/*
 * Il ÉCHANTILLONNE le couloir plutôt que de fuir la menace la plus proche.
 *
 * Le premier jet s'écartait du tir le plus proche : dans un couloir de six
 * cent quarante pixels sous un feu visé, ça produit une oscillation qui ne
 * mène nulle part — huit secondes de survie, quatre parties en trente
 * secondes. Un tir VISÉ suit le vaisseau : ce qui compte n'est pas de quel
 * côté aller, c'est où le couloir sera libre.
 *
 * On note donc dix-sept positions du couloir par le danger qu'elles portent —
 * chaque projectile hostile projeté dans le temps qu'il mettra à descendre,
 * chaque ennemi par sa masse — et on va vers la meilleure, en préférant à
 * danger égal celle qui est sous un ennemi.
 */
#define SH_SAMPLES 17

bool shooter_autopilot(shooter *g)
{
    if (g->phase == SH_DEAD) return false;
    if (g->phase == SH_READY) g->phase = SH_PLAYING;

    bool held[NS_GAME_BUTTON_COUNT];
    memset(held, 0, sizeof held);
    held[NS_GAME_ACTION] = true;   /* on tire toujours : c'est un shmup */

    float best_x = g->ship_x;
    float best_cost = 1e30f;

    for (int i = 0; i < SH_SAMPLES; ++i) {
        const float x = SHIP_MARGIN
                      + (SH_W - 2.0f * SHIP_MARGIN) * (float)i / (float)(SH_SAMPLES - 1);
        float cost = fabsf(x - g->ship_x) * 0.004f;   /* le déplacement coûte un peu */

        for (int k = 0; k < SH_MAX_SHOTS; ++k) {
            const sh_shot *s = &g->shot[k];
            if (!s->alive || !s->hostile || s->vy <= 0.0f) continue;
            /* Où ce tir sera quand il atteindra notre ligne. */
            const float t = (g->ship_y - s->y) / s->vy;
            if (t < 0.0f || t > 2.5f) continue;
            const float hit_x = s->x + s->vx * t;
            const float gap = fabsf(hit_x - x);
            const float reach = s->radius + SHIP_RADIUS + 14.0f;
            if (gap < reach) cost += 60.0f / (0.25f + t);
            else if (gap < reach * 2.5f) cost += 8.0f / (0.25f + t);
        }
        for (int k = 0; k < SH_MAX_ENEMIES; ++k) {
            const sh_enemy *e = &g->enemy[k];
            if (!e->alive) continue;
            const float r = ENEMY_RADIUS[e->kind] * 0.65f + SHIP_RADIUS;
            const float dy = g->ship_y - e->y;
            if (dy < 0.0f) continue;
            if (fabsf(e->x - x) < r && dy < 420.0f) cost += 260.0f / (0.4f + dy / 200.0f);
            /*
             * À danger égal, se placer SOUS un ennemi : c'est là qu'on tire.
             *
             * La prime est PROPORTIONNELLE aux points de vie restants, et la
             * fenêtre s'élargit avec la taille de la cible. Sans ça, le pilote
             * esquivait indéfiniment devant le boss : mesuré, il ne lui retirait
             * que 14 points de vie sur 90 en cent secondes, et la partie restait
             * bloquée à la vague 5 puisque les vagues attendent qu'il tombe.
             * Une borne en mode attraction affichait donc un combat figé.
             *
             * Une prime de 6 ne pesait rien face aux 60 que coûte un tir à
             * esquiver ; un boss à 90 points de vie en vaut 27, ce qui décide le
             * pilote à tenir sa ligne entre deux rafales.
             */
            else if (fabsf(e->x - x) < 26.0f + ENEMY_RADIUS[e->kind] * 0.25f)
                cost -= 6.0f + e->hp * 0.24f;
        }
        if (cost < best_cost) { best_cost = cost; best_x = x; }
    }

    if (best_x < g->ship_x - 10.0f) held[NS_GAME_LEFT] = true;
    else if (best_x > g->ship_x + 10.0f) held[NS_GAME_RIGHT] = true;

    /*
     * Et il reste EN BAS. Le couloir donne six cents pixels de hauteur de jeu ;
     * chaque pixel gagné vers le bas est du temps de réaction en plus contre
     * tout ce qui descend. Le premier jet laissait le vaisseau où il était, et
     * la moitié de sa marge de manœuvre avec.
     */
    if (g->ship_y < SH_H - 60.0f) held[NS_GAME_DOWN] = true;

    shooter_hold(g, held);
    return true;
}

/* ==========================================================================
 * Le dessin
 * ========================================================================== */

void shooter_draw(ns_sprite *s, const shooter *g, const shooter_art *a,
                  float logical_w, float logical_h)
{
    const float sx = logical_w / SH_LOGICAL_W, sy = logical_h / SH_LOGICAL_H;
    const float sc = (sx < sy) ? sx : sy;
    const float ox = (logical_w - SH_W * sc) * 0.5f;
    const float oy = (logical_h - SH_H * sc) * 0.5f;

    static const float space[4] = { 0.02f, 0.02f, 0.05f, 1.0f };
    ns_sprite_rect(s, 0.0f, 0.0f, logical_w, logical_h, space);

    /* Le couloir. `background.png` défile en trois couches dans l'original ;
     * on en garde une, à la vitesse de la couche du milieu. */
    if (a && a->background.handle) {
        static const float dim[4] = { 0.62f, 0.62f, 0.70f, 1.0f };
        const float v = fmodf(g->scroll / 2048.0f, 1.0f);
        ns_sprite_texture(s, &a->background);
        ns_sprite_quad(s, ox, oy, SH_W * sc, SH_H * sc, 0.0f, 1.0f - v, 1.0f, 2.0f - v, dim);
    }
    static const float edge[4] = { 0.20f, 0.24f, 0.40f, 1.0f };
    ns_sprite_rect(s, ox - 5.0f * sc, oy, 5.0f * sc, SH_H * sc, edge);
    ns_sprite_rect(s, ox + SH_W * sc, oy, 5.0f * sc, SH_H * sc, edge);

    static const float ENEMY_TINT[SH_ENEMY_KINDS][4] = {
        { 0.86f, 0.36f, 0.32f, 1.0f },
        { 0.90f, 0.58f, 0.24f, 1.0f },
        { 0.70f, 0.36f, 0.86f, 1.0f },
        { 0.40f, 0.72f, 0.52f, 1.0f },
        { 0.95f, 0.28f, 0.52f, 1.0f },
    };
    for (int i = 0; i < SH_MAX_ENEMIES; ++i) {
        const sh_enemy *e = &g->enemy[i];
        if (!e->alive) continue;
        const float r = ENEMY_RADIUS[e->kind] * 0.7f;
        float col[4];
        for (int k = 0; k < 3; ++k) {
            col[k] = ENEMY_TINT[e->kind][k] * ((e->hit_flash > 0.0f) ? 2.0f : 1.0f);
            if (col[k] > 1.0f) col[k] = 1.0f;
        }
        col[3] = 1.0f;
        ns_sprite_rect(s, ox + (e->x - r) * sc, oy + (e->y - r * 0.7f) * sc,
                       r * 2.0f * sc, r * 1.4f * sc, col);
        /* La jauge de vie des gros : sans elle on ne sait pas si l'on avance. */
        if (e->hp_max > 5.0f) {
            static const float bar_bg[4] = { 0.10f, 0.10f, 0.14f, 1.0f };
            static const float bar_fg[4] = { 0.95f, 0.32f, 0.36f, 1.0f };
            const float w = r * 2.0f;
            ns_sprite_rect(s, ox + (e->x - r) * sc, oy + (e->y - r * 0.7f - 12.0f) * sc,
                           w * sc, 6.0f * sc, bar_bg);
            ns_sprite_rect(s, ox + (e->x - r) * sc, oy + (e->y - r * 0.7f - 12.0f) * sc,
                           w * (e->hp / e->hp_max) * sc, 6.0f * sc, bar_fg);
        }
    }

    static const float SHOT_TINT[SH_SHOT_KINDS][4] = {
        { 1.00f, 0.94f, 0.52f, 1.0f },
        { 0.52f, 0.98f, 0.70f, 1.0f },
        { 0.56f, 0.76f, 1.00f, 1.0f },
        { 1.00f, 0.42f, 0.30f, 1.0f },
        { 1.00f, 0.24f, 0.68f, 1.0f },
    };
    for (int i = 0; i < SH_MAX_SHOTS; ++i) {
        const sh_shot *sh = &g->shot[i];
        if (!sh->alive) continue;
        const float w = sh->radius, h = sh->radius * (sh->hostile ? 1.4f : 2.2f);
        ns_sprite_rect(s, ox + (sh->x - w * 0.5f) * sc, oy + (sh->y - h * 0.5f) * sc,
                       w * sc, h * sc, SHOT_TINT[sh->kind]);
    }

    if (g->phase != SH_DEAD) {
        const bool blink = (g->invuln > 0.0f) && (((int)(g->invuln * 12.0f)) & 1);
        if (!blink) {
            static const float hull[4] = { 0.84f, 0.92f, 1.00f, 1.0f };
            static const float trim[4] = { 0.36f, 0.56f, 0.92f, 1.0f };
            ns_sprite_rect(s, ox + (g->ship_x - 8.0f) * sc, oy + (g->ship_y - 28.0f) * sc,
                           16.0f * sc, 40.0f * sc, hull);
            ns_sprite_rect(s, ox + (g->ship_x - 26.0f) * sc, oy + (g->ship_y - 2.0f) * sc,
                           52.0f * sc, 14.0f * sc, trim);
            ns_sprite_rect(s, ox + (g->ship_x - 16.0f) * sc, oy + (g->ship_y + 10.0f) * sc,
                           32.0f * sc, 10.0f * sc, hull);
        }
    }

    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float amber[4] = { 1.0f, 0.82f, 0.30f, 1.0f };
    char line[64];
    SDL_snprintf(line, sizeof line, "%lld", (long long)g->score);
    ns_sprite_text(s, 30.0f * sc, 24.0f * sc, sc * 6.0f, white, line);
    static const char *const AMMO[3] = { "CANON", "ZIGZAG", "CHERCHEUR" };
    SDL_snprintf(line, sizeof line, "VAGUE %u   %s x%d",
                 g->wave, AMMO[g->ammo_kind < 3 ? g->ammo_kind : 0], g->weapons);
    ns_sprite_text(s, 30.0f * sc, 104.0f * sc, sc * 5.0f, amber, line);

    /* Les vies restantes, en clair : une vie qu'on ne voit pas ne change rien
     * à la façon dont on joue. Des losanges plutôt qu'un nombre — on les compte
     * d'un coup d'œil sans quitter le vaisseau des yeux. */
    for (int i = 0; i < g->lives; ++i) {
        static const float life[4] = { 0.45f, 0.90f, 1.0f, 1.0f };
        ns_sprite_texture(s, NULL);
        ns_sprite_rect(s, (34.0f + (float)i * 40.0f) * sc, 156.0f * sc,
                       26.0f * sc, 26.0f * sc, life);
    }

    if (g->phase == SH_READY) {
        const char *msg = "MANCHE POUR SE DEPLACER   BOUTON POUR TIRER";
        const float t = sc * 5.0f;
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(msg, t)) * 0.5f,
                       logical_h * 0.93f, t, white, msg);
    } else if (g->phase == SH_DEAD) {
        static const float veil[4] = { 0.03f, 0.03f, 0.05f, 0.78f };
        ns_sprite_rect(s, 0.0f, logical_h * 0.33f, logical_w, logical_h * 0.34f, veil);
        const char *msg = "PERDU";
        const float t = sc * 9.0f;
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(msg, t)) * 0.5f,
                       logical_h * 0.40f, t, amber, msg);
        SDL_snprintf(line, sizeof line, "MEILLEUR %u", g->best);
        const float t2 = sc * 5.0f;
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(line, t2)) * 0.5f,
                       logical_h * 0.52f, t2, white, line);
        if (g->dead_time > 0.8f) {
            const char *again = "ESPACE POUR REJOUER";
            ns_sprite_text(s, (logical_w - ns_sprite_text_width(again, t2)) * 0.5f,
                           logical_h * 0.60f, t2, white, again);
        }
    }
}

/* ==========================================================================
 * Les planches
 * ========================================================================== */

bool shooter_art_load(ns_rhi *r, shooter_art *a)
{
    memset(a, 0, sizeof *a);
    a->ready = ns_texture_load(r, &a->background, "games/shooter/background.png", true, false);
    if (!a->ready) NS_WARN("shooter : fond introuvable, le couloir sera uni");
    return a->ready;
}

void shooter_art_free(ns_rhi *r, shooter_art *a)
{
    if (!a->ready) return;
    ns_texture_destroy(r, &a->background);
    a->ready = false;
}

/* ==========================================================================
 * L'adaptation à `ns_game_api`
 * ========================================================================== */

static void sh_reset(void *g, uint64_t seed, bool hard) { shooter_reset((shooter *)g, seed, hard); }
static void sh_press(void *g, ns_game_button b) { shooter_press((shooter *)g, b); }
static void sh_hold(void *g, const bool h[NS_GAME_BUTTON_COUNT]) { shooter_hold((shooter *)g, h); }
static void sh_tick(void *g, float dt) { shooter_tick((shooter *)g, dt); }

static void sh_draw(ns_sprite *s, const void *g, const void *a, float w, float h)
{
    shooter_draw(s, (const shooter *)g, (const shooter_art *)a, w, h);
}

static bool sh_art_load(ns_rhi *r, void *a) { return shooter_art_load(r, (shooter_art *)a); }
static void sh_art_free(ns_rhi *r, void *a) { shooter_art_free(r, (shooter_art *)a); }
static bool sh_autopilot(void *g) { return shooter_autopilot((shooter *)g); }

static uint32_t sh_score(const void *g)
{
    const int64_t v = ((const shooter *)g)->score;
    return v > 0 ? (uint32_t)v : 0u;
}
static uint32_t sh_best(const void *g) { return ((const shooter *)g)->best; }
static void     sh_set_best(void *g, uint32_t b) { ((shooter *)g)->best = b; }

static bool sh_dead(const void *g, float *dead_time)
{
    const shooter *p = (const shooter *)g;
    if (dead_time) *dead_time = p->dead_time;
    return p->phase == SH_DEAD;
}

/* Le vocabulaire de `rulesTable["shooter"]`. « enemy » porte les POINTS DE VIE,
 * pas les points : le serveur les multiplie par quinze, ce qu'il déclare depuis
 * M6. C'est le seul jeu porté dont la table n'a rien eu à changer. */
static const char *const sh_kinds[] = { "enemy", "boss", "wave", "shot", "death", NULL };

static void sh_events(void *g, ns_game_events *out)
{
    shooter *p = (shooter *)g;

    out->blip = p->fired;
    out->blip_kind = "shot";
    p->fired = false;

    if (p->pend_enemy) {
        out->score = true;
        out->score_kind = "enemy";
        out->score_value = p->pend_enemy;
        p->pend_enemy = 0;
    } else if (p->pend_boss) {
        out->score = true;
        out->score_kind = "boss";
        out->score_value = 0;
        p->pend_boss--;
    } else if (p->pend_wave) {
        out->score = true;
        out->score_kind = "wave";
        out->score_value = 0;
        p->pend_wave--;
    }

    if (p->died && !p->pend_enemy && !p->pend_boss && !p->pend_wave) {
        out->die = true;
        p->died = false;
    }
}

const ns_game_api g_shooter_api = {
    .id = "shooter", .title = "SHOOTER", .label = "SHOOT",
    .state_size = sizeof(shooter), .art_size = sizeof(shooter_art),
    .sound_blip = "games/asteroid/shoot1.wav",
    .sound_score = "games/asteroid/explo.wav",
    .sound_die = "games/asteroid/big_explo.wav",
    .art_load = sh_art_load, .art_free = sh_art_free,
    .reset = sh_reset, .press = sh_press, .hold = sh_hold,
    .tick = sh_tick, .draw = sh_draw, .autopilot = sh_autopilot,
    .event_kinds = sh_kinds,
    .score = sh_score, .best = sh_best, .set_best = sh_set_best,
    .dead = sh_dead, .events = sh_events,
};
