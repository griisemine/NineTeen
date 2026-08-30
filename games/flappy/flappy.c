/* flappy.c — voir flappy.h pour ce qui est repris de 2020 et ce qui ne l'est pas. */
#include "flappy.h"

#include "ns_core.h"

/* stb_image est déjà vendoré et déjà instancié par `texgen` ; ici on n'a besoin
 * que des déclarations, l'implémentation vient de la bibliothèque du moteur. */
#include "stb_image.h"

#include <string.h>

/* ==========================================================================
 * Les cotes de 2020
 * ==========================================================================
 * Chaque constante porte le nom qu'elle avait dans `flappy_bird.c`, et son
 * facteur d'échelle. `SCALE_TO_FIT` valait 4 : la planche est dessinée en basse
 * définition et affichée quatre fois plus grande, ce qui donne le gros pixel
 * d'arcade.
 */
#define SCALE 4.0f

#define SPR_BIRD_W   17.0f      /* PERSO.w */
#define SPR_BIRD_H   12.0f      /* PERSO.h */
#define SPR_PIPE_W   26.0f      /* OBSTACLE_VERT.w */
#define SPR_PIPE_H  160.0f      /* OBSTACLE_VERT.h */
#define SPR_GROUND_W 168.0f     /* SOL.w */
#define SPR_GROUND_H  55.0f     /* SOL.h */
#define SPR_DIGIT_W   12.0f     /* CHIFFRE.w */
#define SPR_DIGIT_H   18.0f     /* CHIFFRE.h */
#define SPR_BG_W     144.0f     /* BACKGROUND.w */
#define SPR_BG_H     256.0f     /* BACKGROUND.h */

#define BIRD_W  (SPR_BIRD_W * SCALE)     /*  68 px */
#define BIRD_H  (SPR_BIRD_H * SCALE)     /*  48 px */
#define PIPE_W  (SPR_PIPE_W * SCALE)     /* 104 px */
#define PIPE_H  (SPR_PIPE_H * SCALE)     /* 640 px */
#define GROUND_H (SPR_GROUND_H * SCALE)  /* 220 px */

/* L'oiseau ne bouge pas en x : c'est le décor qui défile. WINDOW_L/2 de 2020. */
#define BIRD_X (FLAPPY_W * 0.5f)

/* DISTANCE_BETWEEN_OBSTACLE = 49, l'écart VERTICAL du passage. Le nom est
 * trompeur dans l'original — il désigne bien la hauteur de la porte, pas un
 * écart entre deux tuyaux. */
#define GAP_HEIGHT (49.0f * SCALE)       /* 196 px */

/*
 * DISTANCE_UNDER_OBSTACLE = 100, l'écart HORIZONTAL entre deux tuyaux — et c'est
 * LUI que 2020 réduisait pour durcir le jeu, comme le dit son commentaire :
 * « difficulte max en distance d'obstacle ». La hauteur du passage, elle, ne
 * bouge pas.
 *
 * J'avais d'abord fait l'inverse — resserrer le passage — et le test l'a
 * refusé : une impulsion fait monter de 120 px, donc l'oiseau oscille sur une
 * bande de 120 px, et un passage de 164 px moins les 48 px de l'oiseau n'en
 * laisse pas assez pour l'y loger. Le jeu devenait infranchissable, pas
 * difficile. La règle d'origine était la bonne, et pour cette raison exactement.
 *
 * La valeur de 82 est mesurée, pas choisie : à 78 le joueur automatique meurt au
 * premier tuyau, à 86 il tient une minute entière comme en normal. À 82 il passe
 * dix-neuf tuyaux puis se fait avoir, quand il en passe trente-trois en normal.
 * C'est ça, « plus dur » : franchissable, et perdu tôt ou tard.
 */
#define SPACING_EASY (100.0f * SCALE)    /* 400 px */
#define SPACING_HARD  (82.0f * SCALE)    /* 328 px : 224 px d'air libre entre deux tuyaux */

/* TRANCHE_POS_OBSTACLE = 550 sur NB_POS_OBSTACLE = 5 hauteurs possibles.
 * Le haut du passage vaut, dans 2020 :
 *     −550 + 110 * slot + OBSTACLE_VERT.h * 4
 * soit de 90 à 530 px. On garde l'expression telle quelle. */
#define SLOT_COUNT 5
#define SLOT_SPAN  550.0f

/* VITESSE_DEPLACEMENT_DECOR = 8 / (FPS/30) = 4 px par image à 60 Hz. */
#define SCROLL_SPEED (4.0f * 60.0f)      /* 240 px/s */

/*
 * La physique, en flottant.
 *
 * L'original monte de ~100 px en huit images à 60 Hz, soit 0,133 s, puis tombe
 * sous une gravité qui s'accumule une image sur deux. Rejoué tel quel au pas
 * fixe du moteur, ça donnerait une chute deux fois trop rapide et une simulation
 * dépendante de la fréquence d'affichage.
 *
 * On garde donc la SENSATION mesurée plutôt que les entiers : une impulsion qui
 * fait monter de 123 px (800² / (2 x 2600)), et une chute qui traverse l'écart
 * de 196 px en 0,39 s depuis l'apogée. À 240 px/s et 400 px entre deux tuyaux,
 * il s'écoule 1,67 s d'un tuyau au suivant : il faut donc battre des ailes
 * quatre fois environ par tuyau, ce qui est le rythme de Flappy Bird.
 */
#define GRAVITY   2600.0f       /* px/s² */
#define FLAP_IMPULSE (-800.0f)  /* px/s, vers le haut */
#define FALL_MAX   1300.0f      /* px/s : sans plafond, un long piqué traverse le sol */

#define ANGLE_UP   (-30.0f)     /* ANGLE_UP de 2020 */
#define ANGLE_DOWN  (90.0f)     /* ANGLE_DOWN */

/* COMPENSATITION_HITBOX_DOWN = 20 : l'original élargit la hitbox vers le bas,
 * ce qui rend le jeu un peu plus indulgent qu'il n'en a l'air. On le garde,
 * parce que c'est une décision de game design et pas un accident. */
#define HITBOX_DOWN 20.0f

/* ==========================================================================
 * Les planches
 * ========================================================================== */

/*
 * `chiffre.png` est en blanc sur NOIR OPAQUE : la planche de 2020 n'a pas de
 * couche alpha. Chargée telle quelle, chaque chiffre traîne son rectangle noir —
 * ce que la capture a montré du premier coup.
 *
 * En 2020, `SDL_Renderer` s'en sortait par une clé de couleur ; ici c'est fait
 * au chargement, une fois, plutôt que par un cas particulier dans le shader qui
 * pénaliserait tous les autres sprites. Le seuil est bas (12 %) : on retire le
 * fond, pas les anti-crénelages sombres du bord des chiffres.
 */
static bool load_black_keyed(ns_rhi *r, ns_texture *out, const char *logical)
{
    char path[1024];
    if (!ns_path_resolve(logical, path, sizeof path)) return false;

    size_t bytes = 0;
    void *file = SDL_LoadFile(path, &bytes);
    if (!file) return false;

    int w = 0, h = 0, comp = 0;
    unsigned char *px = stbi_load_from_memory((const unsigned char *)file, (int)bytes,
                                              &w, &h, &comp, 4);
    SDL_free(file);
    if (!px) return false;

    for (int i = 0; i < w * h; ++i) {
        unsigned char *p = &px[i * 4];
        const int lum = (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100;
        if (lum < 31) p[3] = 0;          /* 31/255 ≈ 12 % */
    }

    ns_texture_desc td;
    SDL_zero(td);
    td.width = (uint32_t)w; td.height = (uint32_t)h;
    td.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    td.sampled = true;
    td.name = logical;
    const bool ok = ns_texture_create(r, out, &td)
                 && ns_texture_upload(r, out, px, (uint32_t)(w * h * 4));
    stbi_image_free(px);
    return ok;
}

bool flappy_art_load(ns_rhi *r, flappy_art *a)
{
    memset(a, 0, sizeof *a);
    /* `srgb = true` : ce sont des images destinées à être vues, pas des données.
     * `gen_mips = false` : le filtrage est au plus proche, une pyramide ne
     * servirait qu'à flouter ce qu'on veut net. */
    /*
     * QUATRE PLANCHES SUR CINQ SONT DESSINÉES, et pas reprises de 2020.
     *
     * `birds.png`, `pipes.png`, `backgrounds.png` et `sol.png` étaient les
     * planches de Flappy Bird : l'oiseau, ses tuyaux, son ciel, son sol. Elles
     * ne sont plus copiées dans le paquet — voir le bloc « LES PLANCHES DES
     * MINI-JEUX » d'`assets/CMakeLists.txt` — et `tools/spriteart` les
     * remplace par un cerf-volant, des pylônes, un ciel à collines et une
     * passerelle.
     *
     * Les COTES et le découpage n'ont pas bougé d'un pixel : `SPR_BIRD_W 17`,
     * `SPR_PIPE_W 26`, `SPR_BG_W 144`, quatre colonnes de tuyaux, trois images
     * d'aile. C'est la condition pour que rien d'autre dans ce fichier ne
     * change, et donc pour que le remplacement soit vérifiable en regardant
     * l'écran plutôt qu'en relisant du code.
     *
     * `chiffre.png` reste : dix chiffres carrés en blanc sur noir, sans marque
     * ni personnage. Il n'y avait aucune raison de le redessiner.
     */
    const bool ok =
        ns_texture_load(r, &a->background, "games/flappy/fonds.png",  true, false) &&
        ns_texture_load(r, &a->birds,      "games/flappy/oiseau.png", true, false) &&
        ns_texture_load(r, &a->pipes,      "games/flappy/tuyaux.png", true, false) &&
        ns_texture_load(r, &a->ground,     "games/flappy/passerelle.png", true, false) &&
        load_black_keyed(r, &a->digits,    "games/flappy/chiffre.png");
    a->ready = ok;
    if (!ok) NS_WARN("flappy : planches introuvables, le jeu tournera sans images");
    return ok;
}

void flappy_art_free(ns_rhi *r, flappy_art *a)
{
    if (!a->ready) return;
    ns_texture_destroy(r, &a->background);
    ns_texture_destroy(r, &a->birds);
    ns_texture_destroy(r, &a->pipes);
    ns_texture_destroy(r, &a->ground);
    ns_texture_destroy(r, &a->digits);
    memset(a, 0, sizeof *a);
}

/* ==========================================================================
 * Règles
 * ========================================================================== */

/* Le haut du passage, exactement comme 2020 le calcule. */
static float gap_top(int slot)
{
    return -SLOT_SPAN + (SLOT_SPAN / (float)SLOT_COUNT) * (float)slot + PIPE_H;
}

static float gap_height(const flappy *g) { (void)g; return GAP_HEIGHT; }

/*
 * L'ÉCART QUI SE RESSERRE — la courbe que ce jeu n'avait pas.
 *
 * Ce qui a été mesuré, en faisant tourner le pilote automatique sur cinq
 * graines et six durées (6, 12, 30, 60, 120, 240 s) :
 *
 *     durée      6    12    30    60   120   240
 *     score      1     4    15    33    69   141      morts : 0 sur 5, partout
 *
 * Le score est une DROITE — 0,588 tuyau par seconde, du début à la fin — et il
 * est identique aux cinq graines près. Autrement dit : l'écart ne bouge jamais,
 * rien n'accélère, et une partie de quatre minutes n'est pas plus difficile que
 * ses douze premières secondes. Ce n'est pas un jeu d'arcade, c'est un chrono.
 *
 * La borne « hard » prouvait pourtant que le levier existait : le MÊME pilote,
 * avec l'écart à 328 px au lieu de 400, meurt à 31 s après dix-neuf tuyaux. Le
 * mécanisme marchait ; personne ne l'avait branché sur le temps.
 *
 * On fait donc ce que faisait 2020 — « difficulte max en distance d'obstacle »,
 * son propre commentaire — mais progressivement : l'écart part de sa valeur
 * facile et descend vers sa valeur dure au fil des tuyaux franchis. Les deux
 * bornes de la rampe sont les deux valeurs DÉJÀ mesurées, ce qui garantit que
 * ni le début ni la fin ne sont des réglages inventés : on commence exactement
 * au jeu d'hier, on finit exactement à sa borne « hard ».
 *
 * La hauteur du passage, elle, ne bouge pas, et c'est mesuré aussi : une
 * impulsion fait monter de 123 px, un passage de 196 px en laisse juste assez
 * pour loger l'oiseau. Le resserrer donne un jeu infranchissable, pas
 * difficile — voir le commentaire de SPACING_HARD.
 */
#define RAMP_PIPES 35.0f     /* tuyaux pour aller d'un bout à l'autre de la rampe */
#define SPACING_FLOOR (76.0f * SCALE)   /* 304 px : le plancher de la borne dure */

static float pipe_spacing(const flappy *g)
{
    const float from = g->hard ? SPACING_HARD : SPACING_EASY;
    const float to   = g->hard ? SPACING_FLOOR : SPACING_HARD;
    float t = (float)g->score / RAMP_PIPES;
    if (t > 1.0f) t = 1.0f;
    return from + (to - from) * t;
}

void flappy_reset(flappy *g, uint64_t seed, bool hard)
{
    memset(g, 0, sizeof *g);
    g->phase = FLAPPY_READY;
    g->hard = hard;
    g->bird_y = FLAPPY_H * 0.42f;
    ns_rng_seed(&g->rng, seed, 0x9E3779B97F4A7C15ull);

    /* Les huit tuyaux du tampon, alignés à partir du bord droit. Le premier est
     * volontairement loin : on doit avoir le temps de comprendre qu'on joue. */
    for (int i = 0; i < FLAPPY_PIPES; ++i) {
        g->pipes[i].position = FLAPPY_W + 200.0f + (float)i * pipe_spacing(g);
        g->pipes[i].slot = (int)ns_rng_below(&g->rng, SLOT_COUNT);
        g->pipes[i].scored = false;
    }
}

void flappy_flap(flappy *g)
{
    if (g->phase == FLAPPY_DEAD) return;
    if (g->phase == FLAPPY_READY) g->phase = FLAPPY_PLAYING;
    g->bird_vy = FLAP_IMPULSE;
    g->flapped = true;
}

/* Collision. Reprise du raisonnement de 2020 : un rectangle contre les deux
 * tuyaux, avec la compensation de hitbox vers le bas, plus le sol. L'original
 * ajoutait quatre tests de distance aux coins pour les cas rasants ; un
 * rectangle contre rectangle les couvre tous, et sans les faux positifs que
 * produisaient ces cercles de rayon `PERSO.h/2` centrés sur les coins. */
static bool hits_anything(const flappy *g)
{
    const float bx0 = BIRD_X - BIRD_W * 0.5f, bx1 = BIRD_X + BIRD_W * 0.5f;
    const float by0 = g->bird_y - BIRD_H * 0.5f, by1 = g->bird_y + BIRD_H * 0.5f;

    if (by1 > FLAPPY_H - GROUND_H) return true;    /* le sol */
    if (by0 < 0.0f) return true;                   /* le plafond : 2020 laissait
                                                    * sortir par le haut, ce qui
                                                    * permettait de survoler tout
                                                    * le niveau. */

    for (int i = 0; i < FLAPPY_PIPES; ++i) {
        const float px0 = g->pipes[i].position, px1 = px0 + PIPE_W;
        if (bx1 <= px0 || bx0 >= px1) continue;

        const float top = gap_top(g->pipes[i].slot);
        const float bottom = top + gap_height(g);
        if (by0 < top || by1 > bottom + HITBOX_DOWN) return true;
    }
    return false;
}

void flappy_tick(flappy *g, float dt)
{
    /*
     * `flapped` n'est PAS remis à zéro ici, et c'est une correction.
     *
     * Il l'était, en tête de `tick` — or `flappy_flap` est appelée depuis le
     * gestionnaire d'événements, donc AVANT la boucle de pas fixe de l'image.
     * Le drapeau était donc effacé avant que quiconque puisse le lire : le son
     * du battement n'a jamais été joué, l'index droit n'a jamais tapé sur le
     * bouton, et l'événement « flap » n'est jamais entré dans le journal de
     * partie. C'est `flappy_events` qui consomme désormais les trois drapeaux,
     * ce qui les rend indépendants du moment où ils ont été levés.
     */
    g->scored_now = g->died_now = false;
    g->wing_time += dt;

    if (g->phase == FLAPPY_READY) {
        /* Sursis : l'oiseau flotte, rien ne défile. C'est l'écran « appuyez pour
         * jouer » de l'original, sans le texte clignotant. */
        g->bird_y = FLAPPY_H * 0.42f + sinf(g->wing_time * 4.0f) * 14.0f;
        g->bird_angle = 0.0f;
        return;
    }

    if (g->phase == FLAPPY_DEAD) {
        /* On tombe encore, mais plus rien ne défile : la chute finale fait partie
         * du jeu, c'est elle qui laisse le temps de voir où l'on s'est raté. */
        g->dead_time += dt;
        g->bird_vy = ns_minf(g->bird_vy + GRAVITY * dt, FALL_MAX);
        g->bird_y += g->bird_vy * dt;
        const float floor_y = FLAPPY_H - GROUND_H - BIRD_H * 0.5f;
        if (g->bird_y > floor_y) { g->bird_y = floor_y; g->bird_vy = 0.0f; }
        g->bird_angle = ns_minf(g->bird_angle + 420.0f * dt, ANGLE_DOWN);
        return;
    }

    /* --- vol -------------------------------------------------------- */
    g->bird_vy = ns_minf(g->bird_vy + GRAVITY * dt, FALL_MAX);
    g->bird_y += g->bird_vy * dt;

    /* L'angle suit la vitesse : piqué en descente, nez levé juste après un
     * battement. C'est ce qui donne à l'oiseau l'air de peser quelque chose. */
    const float want = (g->bird_vy < 0.0f)
        ? ANGLE_UP
        : ns_minf(ANGLE_DOWN, ANGLE_UP + (g->bird_vy / FALL_MAX) * 140.0f);
    g->bird_angle = ns_damp(g->bird_angle, want, 9.0f, dt);

    /* --- décor ------------------------------------------------------ */
    const float dx = SCROLL_SPEED * dt;
    g->ground_scroll += dx;

    for (int i = 0; i < FLAPPY_PIPES; ++i) {
        flappy_pipe *p = &g->pipes[i];
        p->position -= dx;

        if (!p->scored && p->position + PIPE_W < BIRD_X - BIRD_W * 0.5f) {
            p->scored = true;
            g->score++;
            g->scored_now = true;
            if (g->score > g->best) g->best = g->score;
        }

        /* Recyclage : un tuyau sorti par la gauche repart à droite, derrière le
         * plus avancé. C'est le tampon glissant de 2020, sans le décalage de
         * tout le tableau à chaque fois. */
        if (p->position + PIPE_W < -PIPE_W) {
            float furthest = p->position;
            for (int k = 0; k < FLAPPY_PIPES; ++k) {
                if (g->pipes[k].position > furthest) furthest = g->pipes[k].position;
            }
            p->position = furthest + pipe_spacing(g);
            p->slot = (int)ns_rng_below(&g->rng, SLOT_COUNT);
            p->scored = false;
        }
    }

    if (hits_anything(g)) {
        g->phase = FLAPPY_DEAD;
        g->died_now = true;
        g->bird_vy = ns_maxf(g->bird_vy, 0.0f);
    }
}

/* ==========================================================================
 * Joueur automatique
 * ========================================================================== */

bool flappy_autopilot(flappy *g)
{
    if (g->phase == FLAPPY_DEAD) return false;
    if (g->phase == FLAPPY_READY) { flappy_flap(g); return true; }

    /*
     * Le prochain tuyau — mais seulement s'il est PROCHE.
     *
     * Viser le trou d'un tuyau encore à mille pixels était le premier défaut du
     * bot : il montait vers une porte lointaine, atteignait le plafond et
     * mourait avant d'avoir vu un seul tuyau. On ne se met en ligne qu'à partir
     * du moment où il y a quelque chose à viser.
     */
    const flappy_pipe *next = NULL;
    float best = 1e9f;
    for (int i = 0; i < FLAPPY_PIPES; ++i) {
        const float p = g->pipes[i].position;
        if (p + PIPE_W < BIRD_X - BIRD_W * 0.5f) continue;   /* déjà franchi */
        if (p > BIRD_X + 620.0f) continue;                   /* trop loin pour viser */
        if (p < best) { best = p; next = &g->pipes[i]; }
    }

    /*
     * On vise le POINT DE BATTEMENT, pas le centre du trou.
     *
     * Un battement fait monter de 120 px, toujours : l'oiseau oscille donc sur
     * une bande de cette hauteur, dont le point bas est là où l'on bat. Viser le
     * centre du trou revient à faire passer l'apogée 120 px au-dessus — c'est le
     * plafond, et c'est ce que faisait la première version du bot avant que le
     * test ne le dise. On bat donc 40 px sous le bas du passage moins la
     * compensation, et l'apogée tombe juste sous le haut.
     */
    float target = FLAPPY_H * 0.55f;
    if (next) target = gap_top(next->slot) + gap_height(g) - 40.0f;

    /*
     * Et surtout : on ne bat des ailes que si l'on n'est PAS déjà en train de
     * monter. Sans cette condition le bot bat à chaque pas — cent vingt fois par
     * seconde — l'oiseau part comme une fusée et percute le plafond en une
     * seconde. C'est exactement ce que faisait la première version, et le test
     * l'a dit avant qu'aucune capture ne le montre.
     */
    if (g->bird_vy < -120.0f) return false;

    /*
     * On compare la position NUE à la cible, sans anticipation.
     *
     * La version précédente ajoutait `vy * 0.14` pour « anticiper la chute ». En
     * piqué, `vy` atteint 700 px/s : l'anticipation valait donc une centaine de
     * pixels, c'est-à-dire précisément la hauteur d'un battement — elle avançait
     * le battement d'exactement ce qu'elle était censée corriger, et l'apogée
     * finissait dans le plafond. `target` étant déjà le point de battement, il
     * n'y a rien à anticiper.
     */
    if (g->bird_y > target) { flappy_flap(g); return true; }
    return false;
}

/* ==========================================================================
 * Affichage
 * ==========================================================================
 * Tout passe par `blit`, qui met le repère du jeu (1920 x 1080) à l'échelle de
 * la cible et le centre. C'est ce qui fait que la MÊME fonction remplit l'écran
 * d'une borne et le plein écran : il n'y a pas deux chemins de rendu à garder
 * d'accord, seulement une taille logique différente.
 */
typedef struct blit_ctx {
    ns_sprite *s;
    float scale, ox, oy;
} blit_ctx;

static void blit(const blit_ctx *c, const ns_texture *tex,
                 float x, float y, float w, float h,
                 float sx, float sy, float sw, float sh,
                 float tex_w, float tex_h, const float rgba[4])
{
    ns_sprite_texture(c->s, tex);
    ns_sprite_quad(c->s,
                   c->ox + x * c->scale, c->oy + y * c->scale,
                   w * c->scale, h * c->scale,
                   sx / tex_w, sy / tex_h, (sx + sw) / tex_w, (sy + sh) / tex_h,
                   rgba);
}

static void draw_number(const blit_ctx *c, const flappy_art *a,
                        uint32_t value, float cx, float y, float scale)
{
    char buf[16];
    SDL_snprintf(buf, sizeof buf, "%u", value);
    const size_t n = SDL_strlen(buf);
    const float dw = SPR_DIGIT_W * scale, dh = SPR_DIGIT_H * scale;
    float x = cx - (float)n * dw * 0.5f;
    for (size_t i = 0; i < n; ++i) {
        const int d = buf[i] - '0';
        blit(c, &a->digits, x, y, dw, dh,
             (float)d * SPR_DIGIT_W, 0.0f, SPR_DIGIT_W, SPR_DIGIT_H,
             SPR_DIGIT_W * 10.0f, SPR_DIGIT_H, NULL);
        x += dw;
    }
}

void flappy_draw(ns_sprite *s, const flappy *g, const flappy_art *a,
                 float logical_w, float logical_h)
{
    blit_ctx c;
    c.s = s;
    c.scale = ns_minf(logical_w / FLAPPY_W, logical_h / FLAPPY_H);
    c.ox = (logical_w - FLAPPY_W * c.scale) * 0.5f;
    c.oy = (logical_h - FLAPPY_H * c.scale) * 0.5f;

    /* Bandes noires : le terrain est en 16/9 et la cible ne l'est pas forcément.
     * Les peindre plutôt que de laisser voir ce qu'il y avait dessous. */
    static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ns_sprite_rect(s, 0, 0, logical_w, logical_h, black);

    if (!a->ready) return;

    /* --- fond : la planche fait 288 de large pour deux ciels de 144 ------ */
    const float bg_w = SPR_BG_W * SCALE * (FLAPPY_H / (SPR_BG_H * SCALE));
    const float bg_h = FLAPPY_H;
    for (float x = 0.0f; x < FLAPPY_W; x += bg_w) {
        blit(&c, &a->background, x, 0.0f, bg_w, bg_h,
             0.0f, 0.0f, SPR_BG_W, SPR_BG_H, SPR_BG_W * 2.0f, SPR_BG_H, NULL);
    }

    /* --- tuyaux ---------------------------------------------------------
     * La planche fait 104 de large : le tuyau vert est à x 52, l'orange à 0.
     * `hard` prend l'orange, comme la borne du même nom.
     */
    const float pipe_u = g->hard ? 0.0f : 52.0f;
    for (int i = 0; i < FLAPPY_PIPES; ++i) {
        const float px = g->pipes[i].position;
        if (px > FLAPPY_W || px + PIPE_W < 0.0f) continue;

        const float top = gap_top(g->pipes[i].slot);
        const float bottom = top + gap_height(g);

        /* Le tuyau du haut est la planche RETOURNÉE : on inverse v, ce qui
         * ramène l'embouchure — dessinée EN HAUT de la planche, ligne 0 —
         * contre l'ouverture. Le tuyau du bas la prend telle quelle, et pour la
         * même raison : sa première ligne est celle qui borde l'ouverture. Le
         * commentaire d'origine disait « en bas » ; la planche de 2020 le
         * contredisait déjà, et celle de `spriteart` suit la planche. */
        blit(&c, &a->pipes, px, top - PIPE_H, PIPE_W, PIPE_H,
             pipe_u, SPR_PIPE_H, SPR_PIPE_W, -SPR_PIPE_H,
             SPR_PIPE_W * 4.0f, SPR_PIPE_H, NULL);
        blit(&c, &a->pipes, px, bottom, PIPE_W, PIPE_H,
             pipe_u, 0.0f, SPR_PIPE_W, SPR_PIPE_H,
             SPR_PIPE_W * 4.0f, SPR_PIPE_H, NULL);
    }

    /* --- sol : il défile, donc il se répète ------------------------------ */
    const float gw = SPR_GROUND_W * SCALE;
    const float goff = -fmodf(g->ground_scroll, gw);
    for (float x = goff; x < FLAPPY_W; x += gw) {
        blit(&c, &a->ground, x, FLAPPY_H - GROUND_H, gw, GROUND_H,
             0.0f, 0.0f, SPR_GROUND_W, SPR_GROUND_H,
             SPR_GROUND_W, SPR_GROUND_H, NULL);
    }

    /* --- l'oiseau -------------------------------------------------------
     * Trois images d'aile, la planche en compte trois colonnes de 17. On bat
     * plus vite en montée : c'est ce que fait l'original en accélérant son
     * compteur d'animation après un saut.
     */
    const int frame = (int)(g->wing_time * (g->bird_vy < 0.0f ? 18.0f : 9.0f)) % 3;
    blit(&c, &a->birds,
         BIRD_X - BIRD_W * 0.5f, g->bird_y - BIRD_H * 0.5f, BIRD_W, BIRD_H,
         (float)frame * SPR_BIRD_W, 0.0f, SPR_BIRD_W, SPR_BIRD_H,
         SPR_BIRD_W * 3.0f, SPR_BIRD_H * 3.0f, NULL);

    /* --- score ---------------------------------------------------------- */
    draw_number(&c, a, g->score, FLAPPY_W * 0.5f, 90.0f, SCALE * 1.6f);

    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float amber[4] = { 1.0f, 0.82f, 0.30f, 1.0f };

    if (g->phase == FLAPPY_READY) {
        const float sc = c.scale * 7.0f;
        const char *msg = "APPUYEZ POUR VOLER";
        const float w = ns_sprite_text_width(msg, sc);
        ns_sprite_text(s, c.ox + (FLAPPY_W * c.scale - w) * 0.5f,
                       c.oy + FLAPPY_H * c.scale * 0.62f, sc, white, msg);
    } else if (g->phase == FLAPPY_DEAD) {
        const float sc = c.scale * 9.0f;
        const char *msg = "PERDU";
        float w = ns_sprite_text_width(msg, sc);
        ns_sprite_text(s, c.ox + (FLAPPY_W * c.scale - w) * 0.5f,
                       c.oy + FLAPPY_H * c.scale * 0.34f, sc, amber, msg);

        char best[48];
        SDL_snprintf(best, sizeof best, "MEILLEUR %u", g->best);
        const float sc2 = c.scale * 5.0f;
        w = ns_sprite_text_width(best, sc2);
        ns_sprite_text(s, c.ox + (FLAPPY_W * c.scale - w) * 0.5f,
                       c.oy + FLAPPY_H * c.scale * 0.46f, sc2, white, best);

        if (g->dead_time > 0.8f) {
            const char *again = "ESPACE POUR REJOUER";
            w = ns_sprite_text_width(again, sc2);
            ns_sprite_text(s, c.ox + (FLAPPY_W * c.scale - w) * 0.5f,
                           c.oy + FLAPPY_H * c.scale * 0.56f, sc2, white, again);
        }
    }
}

/* ==========================================================================
 * L'adaptation à `ns_game_api`
 * ==========================================================================
 * Des enveloppes, pas une réécriture. Flappy garde son API typée — c'est elle
 * que `tests/test_flappy.c` interroge, et un test qui passe par un pointeur de
 * fonction ne vérifie plus les types.
 * ========================================================================== */

static void fl_reset(void *g, uint64_t seed, bool hard) { flappy_reset((flappy *)g, seed, hard); }

static void fl_press(void *g, ns_game_button b)
{
    /* Toutes les touches battent des ailes. Sur une borne il n'y a qu'un bouton
     * qui compte, et chercher lequel n'apprend rien à personne. */
    (void)b;
    flappy_flap((flappy *)g);
}

static void fl_tick(void *g, float dt) { flappy_tick((flappy *)g, dt); }

static void fl_draw(ns_sprite *s, const void *g, const void *a, float w, float h)
{
    flappy_draw(s, (const flappy *)g, (const flappy_art *)a, w, h);
}

static bool fl_art_load(ns_rhi *r, void *a) { return flappy_art_load(r, (flappy_art *)a); }
static void fl_art_free(ns_rhi *r, void *a) { flappy_art_free(r, (flappy_art *)a); }
static bool fl_autopilot(void *g)           { return flappy_autopilot((flappy *)g); }

static uint32_t fl_score(const void *g)    { return ((const flappy *)g)->score; }
static uint32_t fl_best(const void *g)     { return ((const flappy *)g)->best; }
static void     fl_set_best(void *g, uint32_t b) { ((flappy *)g)->best = b; }

static bool fl_dead(const void *g, float *dead_time)
{
    const flappy *f = (const flappy *)g;
    if (dead_time) *dead_time = f->dead_time;
    return f->phase == FLAPPY_DEAD;
}

/*
 * Le vocabulaire du serveur pour Flappy : un tuyau franchi vaut un point
 * (« pipe »), le battement d'aile ne vaut rien mais il est limité en fréquence,
 * la mort clôt la partie. Ce sont les noms de `rulesTable["flappy"]`, pas des
 * noms choisis ici.
 */
static const char *const fl_kinds[] = { "pipe", "flap", "death", NULL };

static void fl_events(void *g, ns_game_events *out)
{
    flappy *f = (flappy *)g;
    out->blip        = f->flapped;
    out->blip_kind   = "flap";
    out->score       = f->scored_now;
    out->score_kind  = "pipe";
    out->score_value = 1;
    out->die         = f->died_now;

    /* Consommés : lus une fois, une seule. Sans ça un battement levé par le
     * gestionnaire d'événements serait relayé à chaque image jusqu'au suivant. */
    f->flapped = f->scored_now = f->died_now = false;
}

const ns_game_api g_flappy_api = {
    .id = "flappy", .title = "FLAPPY BIRD", .label = "FLAPPY",
    .state_size = sizeof(flappy), .art_size = sizeof(flappy_art),
    .sound_blip = "games/flappy/flap.wav",
    .sound_score = "games/flappy/score.wav",
    .sound_die = "games/flappy/hurt.wav",
    .art_load = fl_art_load, .art_free = fl_art_free,
    .reset = fl_reset, .press = fl_press, .hold = NULL,
    .tick = fl_tick, .draw = fl_draw, .autopilot = fl_autopilot,
    .event_kinds = fl_kinds,
    .score = fl_score, .best = fl_best, .set_best = fl_set_best,
    .dead = fl_dead, .events = fl_events,
};
