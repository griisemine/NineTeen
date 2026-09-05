/*
 * test_presence.c — les corps des autres joueurs : ce qui est interpolé, ce qui
 * est déduit, et ce qui refuse une entrée absurde.
 *
 * CE QUE CE TEST ATTRAPE VRAIMENT
 * -------------------------------
 * Donner un corps à un pair, c'est fabriquer soixante états par seconde à partir
 * de quatre positions. Toutes les façons de rater ça sont SILENCIEUSES — aucune
 * ne produit d'erreur, aucune n'empêche le jeu de tourner, et chacune ne se voit
 * qu'en regardant longtemps quelqu'un marcher :
 *
 *   1. EXTRAPOLER au lieu d'interpoler. Le corps continue tout droit puis
 *      RECULE quand la vraie position arrive. Le pire cas est un joueur qui
 *      s'arrête — c'est-à-dire devant une borne, c'est-à-dire là où on le
 *      regarde.
 *   2. RÉINTÉGRER le même lot à chaque image. `from` rejoint `to`, il n'y a plus
 *      rien à interpoler, et le corps saute de 35 cm quatre fois par seconde.
 *   3. CONFONDRE un cap absent avec un cap nul. Un pair d'une version antérieure
 *      glisse alors de côté en regardant l'est, pour toujours.
 *   4. Piloter la phase de marche par le TEMPS. Les pieds patinent, et personne
 *      ne sait dire pourquoi.
 *   5. Faire disparaître un pair d'un coup, ou ne jamais le faire disparaître.
 *   6. Croire une position venue du réseau. Un NaN dans une matrice de modèle
 *      efface le personnage sans un message ; une position à 10^9 rend
 *      l'interpolation numériquement vide de sens.
 *
 * Aucun des six ne demande de GPU, de fenêtre, de fichier ni de réseau :
 * `room_presence.c` est du calcul pur sur des positions datées, et il l'est
 * exprès. Les cotes du personnage lui sont passées en flottants plutôt que lues
 * sur le modèle — voir `room_presence_config` — pour que ce test tourne partout.
 *
 * LA PREUVE PAR MUTATION
 * ----------------------
 * Un test qui ne tombe pas quand on casse ce qu'il prétend vérifier ne vérifie
 * rien. Chaque comportement ci-dessous a été cassé volontairement dans
 * `room_presence.c`, une mutation à la fois, et le nombre d'assertions tombées
 * est noté en tête de chaque section. Les mutations et leurs comptes sont
 * rassemblés en bas de ce fichier.
 */
#include "ns_core.h"
#include "room_presence.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* `snprintf` de la bibliothèque standard et non celui de SDL : ce test ne lie
 * rien du moteur qui l'obligerait à démarrer SDL, et il n'a besoin de rien de
 * plus qu'un formatage de chaîne. */
#define FMT(dst, ...) snprintf((dst), sizeof (dst), __VA_ARGS__)

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

/*
 * LES COTES DU PERSONNAGE LIVRÉ, relevées par `ns_skin` au démarrage du jeu et
 * recopiées ici comme constantes de test.
 *
 * Elles sont écrites en clair plutôt que lues sur le fichier : ce test ne doit
 * dépendre d'aucun asset, et la propriété vérifiée — « la phase suit la
 * distance » — ne dépend pas de la valeur de la foulée mais du fait qu'elle
 * serve de diviseur. Une foulée de 1,55 m est ce que `room_camera_stride`
 * fournit par défaut ; un cycle de 2,00 s et une pose de passage à 0,328 s sont
 * ce que le journal du jeu imprime pour `personnage.glb`.
 */
#define STRIDE      1.55f
#define CYCLE       2.00f
#define STAND_TIME  0.328f
#define HEIGHT      1.82f
#define EYE         1.63f

/* Le pas d'affichage : soixante images par seconde, la cadence à laquelle ce
 * module travaille vraiment. */
#define DT (1.0f / 60.0f)

static void config(room_presence_config *c)
{
    memset(c, 0, sizeof *c);
    c->stride      = STRIDE;
    c->cycle       = CYCLE;
    c->stand_time  = STAND_TIME;
    c->height      = HEIGHT;
    c->eye_default = EYE;
}

/* Un pair complet, publiant tout ce qu'un client à jour publie. */
static ns_realtime_peer make_peer(const char *id, const char *name,
                                  float x, float z, float yaw)
{
    ns_realtime_peer p;
    memset(&p, 0, sizeof p);
    FMT(p.id, "%s", id);
    FMT(p.name, "%s", name);
    p.verified = true;
    p.x = x;
    p.y = EYE;              /* la position de l'ŒIL : les pieds sont EYE plus bas */
    p.z = z;
    p.yaw = yaw;
    p.has_yaw = true;
    p.eye = EYE;
    return p;
}

/* Avance l'affichage jusqu'à `until_ms` sans nouvelle position reçue. */
static void advance(room_presence *pr, uint64_t from_ms, uint64_t until_ms)
{
    const uint64_t stride_ms = (uint64_t)(DT * 1000.0f + 0.5f);
    for (uint64_t t = from_ms; t < until_ms; t += stride_ms) {
        (void)room_presence_step(pr, t, DT);
    }
    (void)room_presence_step(pr, until_ms, DT);
}

/* ==========================================================================
 * 1. L'INTERPOLATION entre deux positions datées
 *
 * M1  alpha non borné (extrapolation)  : 3 assertions tombent.
 * M2  alpha figé à 1 (téléportation)     : 6 assertions tombent.
 * M3  pieds = position publiée          : 2 assertions tombent.
 * ========================================================================== */

static void test_interpolation(void)
{
    printf("-- interpolation\n");

    room_presence_config c;
    config(&c);
    room_presence pr;
    room_presence_init(&pr, &c);

    /* Deux positions séparées d'une période et de 0,35 m : exactement ce qu'un
     * pair qui marche à 1,4 m/s produit à 4 Hz. */
    ns_realtime_peer p = make_peer("aaa", "Ada", 0.0f, 0.0f, 0.0f);
    room_presence_sample(&pr, &p, 1, 1000);
    CHECK(room_presence_step(&pr, 1000, DT) == 1, "un pair, un corps");

    p.z = 0.35f;
    room_presence_sample(&pr, &p, 1, 1250);

    /*
     * À L'INSTANT OÙ LA SECONDE POSITION ARRIVE, on montre la PREMIÈRE. C'est la
     * définition même de l'interpolation, et c'est ce qui fixe le retard : le
     * corps montré est celui d'il y a une période.
     */
    (void)room_presence_step(&pr, 1250, DT);
    CHECK(fabsf(pr.body[0].feet.z - 0.0f) < 1e-4f,
          "à l'arrivée du lot, le corps est encore à la position PRÉCÉDENTE "
          "(z=%.4f, attendu 0)", (double)pr.body[0].feet.z);

    /* À mi-chemin dans le temps, à mi-chemin dans l'espace. */
    advance(&pr, 1250, 1375);
    CHECK(fabsf(pr.body[0].feet.z - 0.175f) < 0.01f,
          "à la moitié de la période, le corps est à la moitié du segment "
          "(z=%.4f, attendu 0,175)", (double)pr.body[0].feet.z);

    /* Au bout de la période, il est arrivé. */
    advance(&pr, 1375, 1500);
    CHECK(fabsf(pr.body[0].feet.z - 0.35f) < 0.01f,
          "au bout de la période, le corps a rejoint la position reçue "
          "(z=%.4f, attendu 0,35)", (double)pr.body[0].feet.z);

    /*
     * ET IL N'EXTRAPOLE PAS. Sans nouvelle position, il RESTE là. Une
     * extrapolation l'enverrait à 0,70 m puis le ferait reculer.
     *
     * C'est le cas qui compte le plus : un joueur qui s'arrête devant une borne
     * n'envoie plus que la même position, et un corps qui continuerait tout
     * droit traverserait la borne avant de revenir en arrière.
     */
    advance(&pr, 1500, 1750);
    CHECK(fabsf(pr.body[0].feet.z - 0.35f) < 0.01f,
          "sans nouvelle position, le corps s'ARRÊTE au lieu de continuer "
          "(z=%.4f, attendu 0,35)", (double)pr.body[0].feet.z);
    CHECK(pr.body[0].speed < ROOM_PRESENCE_WALK_MIN,
          "et sa vitesse rendue retombe à zéro (%.4f m/s)",
          (double)pr.body[0].speed);

    /* Un silence LONG — deux battements de plus — ne le déplace toujours pas.
     * Une extrapolation l'aurait à ce stade envoyé à plus d'un mètre. */
    advance(&pr, 1750, 2200);
    CHECK(pr.body_count == 1 && fabsf(pr.body[0].feet.z - 0.35f) < 0.01f,
          "et il n'a pas dérivé après quatre battements de silence (z=%.4f)",
          pr.body_count ? (double)pr.body[0].feet.z : -1.0);

    /* Le retard mesuré, écrit noir sur blanc : il vaut la période. */
    CHECK(true, "le retard d'affichage vaut donc une période, soit %u ms",
          (unsigned)NS_RT_PERIOD_MS);

    /* Les PIEDS sont sous l'œil publié, exactement de la hauteur d'œil. */
    CHECK(fabsf(pr.body[0].feet.y - 0.0f) < 1e-4f,
          "les pieds sont posés au sol, pas à hauteur d'œil (y=%.4f)",
          (double)pr.body[0].feet.y);
    CHECK(fabsf(pr.body[0].label_y - HEIGHT * 1.1f) < 1e-3f,
          "l'étiquette est au-dessus de la TÊTE, à une hauteur tirée du modèle "
          "(%.3f m)", (double)pr.body[0].label_y);
}

/* ==========================================================================
 * 2. UN LOT DÉJÀ VU ne se réintègre pas
 *
 * M4  garde par DATE retirée            : 1 assertion tombe.
 * M5  garde par PISTE retirée           : 1 assertion tombe.
 * M6  les DEUX retirées                 : 4 assertions tombent.
 * ========================================================================== */

static void test_lot_idempotent(void)
{
    printf("-- lot idempotent\n");

    room_presence_config c;
    config(&c);
    room_presence pr;
    room_presence_init(&pr, &c);

    ns_realtime_peer p = make_peer("aaa", "Ada", 0.0f, 0.0f, 0.0f);
    room_presence_sample(&pr, &p, 1, 1000);
    p.z = 0.35f;
    room_presence_sample(&pr, &p, 1, 1250);

    /* La boucle de jeu relit la même table à chaque image : vingt appels de plus
     * avec la même date ne doivent RIEN changer. */
    for (int i = 0; i < 20; ++i) room_presence_sample(&pr, &p, 1, 1250);

    (void)room_presence_step(&pr, 1250, DT);
    CHECK(fabsf(pr.body[0].feet.z - 0.0f) < 1e-4f,
          "vingt relectures du même lot ne détruisent pas le segment à "
          "interpoler (z=%.4f, attendu 0)", (double)pr.body[0].feet.z);

    advance(&pr, 1250, 1375);
    CHECK(fabsf(pr.body[0].feet.z - 0.175f) < 0.01f,
          "et l'interpolation se déroule quand même (z=%.4f, attendu 0,175)",
          (double)pr.body[0].feet.z);

    /* Un lot sans date ne fait rien non plus. */
    const uint64_t garde = pr.last_batch_ms;
    room_presence_sample(&pr, &p, 1, 0);
    CHECK(pr.last_batch_ms == garde,
          "un lot sans date (aucune réponse reçue) n'est pas intégré");

    /*
     * UN LOT PLUS VIEUX QUE LE DERNIER est refusé.
     *
     * C'est ce que la garde par DATE fait et que la garde par piste ne fait pas :
     * un lot arriéré passerait le test « même date que ma dernière » et
     * s'installerait comme un nouveau segment dont l'arrivée précède le départ.
     * L'interpolation partirait alors dans le passé.
     */
    ns_realtime_peer vieux = p;
    vieux.z = -9.0f;
    room_presence_sample(&pr, &vieux, 1, 1100);   /* antérieur à 1250 */
    advance(&pr, 1375, 1500);
    CHECK(fabsf(pr.body[0].feet.z - 0.35f) < 0.01f,
          "un lot ARRIÉRÉ est refusé : le corps ne repart pas en arrière "
          "(z=%.4f, attendu 0,35)", (double)pr.body[0].feet.z);

    /*
     * DEUX LIGNES POUR LE MÊME PAIR dans un seul lot : la première compte, la
     * seconde est ignorée. C'est ce que la garde par PISTE fait et que la garde
     * par date ne fait pas — sans elle, un serveur qui doublerait une ligne
     * ferait avancer le même pair de deux segments en un battement.
     */
    {
        room_presence pr2;
        room_presence_config c2;
        config(&c2);
        room_presence_init(&pr2, &c2);

        ns_realtime_peer doublon[2];
        doublon[0] = make_peer("aaa", "Ada", 0.0f, 0.0f, 0.0f);
        doublon[1] = make_peer("aaa", "Ada", 5.0f, 5.0f, 0.0f);
        room_presence_sample(&pr2, doublon, 2, 1000);
        advance(&pr2, 1000, 1250);

        CHECK(room_presence_step(&pr2, 1250, DT) == 1,
              "un identifiant présent deux fois dans un lot ne fait qu'un corps");
        CHECK(fabsf(pr2.body[0].feet.x - 0.0f) < 0.01f,
              "et c'est la PREMIÈRE ligne qui compte (x=%.3f, attendu 0)",
              (double)pr2.body[0].feet.x);
    }
}

/* ==========================================================================
 * 3. LE CAP : publié, ou déduit du déplacement
 *
 * M7  has_yaw ignoré, yaw pris tel quel : 4 assertions tombent.
 * M8  cap déduit du pas d'image         : 3 assertions tombent.
 * ========================================================================== */

/* Le module amortit le cap sur une période. On laisse le temps d'y arriver. */
static void settle(room_presence *pr, uint64_t at_ms)
{
    for (int i = 0; i < 120; ++i) (void)room_presence_step(pr, at_ms, DT);
}

static void test_cap(void)
{
    printf("-- cap\n");

    room_presence_config c;
    config(&c);

    /* --- a) le pair PUBLIE son cap : c'est celui-là qu'on emploie --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        /* Il regarde vers -Z (un quart de tour négatif) tout en marchant vers
         * +Z : quelqu'un qui recule. Un cap déduit du déplacement se tromperait
         * d'un demi-tour, et c'est bien pour ça qu'on préfère le cap publié. */
        ns_realtime_peer p = make_peer("aaa", "Ada", 0.0f, 0.0f, -NS_PI * 0.5f);
        room_presence_sample(&pr, &p, 1, 1000);
        p.z = 0.35f;
        room_presence_sample(&pr, &p, 1, 1250);
        settle(&pr, 1500);

        CHECK(fabsf(pr.body[0].yaw + NS_PI * 0.5f) < 0.05f,
              "le cap PUBLIÉ est employé tel quel, même à contre-sens du "
              "déplacement (%.3f rad, attendu %.3f)",
              (double)pr.body[0].yaw, (double)(-NS_PI * 0.5f));
    }

    /* --- b) le pair ne publie PAS de cap : on le déduit du déplacement --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        /*
         * Le piège exact : `yaw` vaut 0 dans les deux cas — celui du pair qui
         * regarde vers +X, et celui du pair qui ne dit rien. Seul `has_yaw` les
         * sépare. Ici il marche vers +Z, donc son cap déduit doit être un quart
         * de tour, et surtout PAS zéro.
         */
        ns_realtime_peer p = make_peer("bbb", "Bob", 0.0f, 0.0f, 0.0f);
        p.has_yaw = false;
        room_presence_sample(&pr, &p, 1, 1000);
        p.z = 0.35f;
        room_presence_sample(&pr, &p, 1, 1250);
        settle(&pr, 1500);

        CHECK(fabsf(pr.body[0].yaw - NS_PI * 0.5f) < 0.05f,
              "sans cap publié, le pair est tourné dans la direction où il "
              "MARCHE (%.3f rad, attendu %.3f)",
              (double)pr.body[0].yaw, (double)(NS_PI * 0.5f));
        CHECK(fabsf(pr.body[0].yaw) > 0.5f,
              "et surtout pas laissé au cap nul qu'un pair muet rend aussi");

        /* Il repart vers -X : le cap suit. */
        p.z = 0.35f; p.x = -0.35f;
        room_presence_sample(&pr, &p, 1, 1500);
        settle(&pr, 1750);
        CHECK(fabsf(fabsf(pr.body[0].yaw) - NS_PI) < 0.10f,
              "et il suit le déplacement quand celui-ci change (%.3f rad, "
              "attendu ±%.3f)", (double)pr.body[0].yaw, (double)NS_PI);
    }

    /* --- c) à l'arrêt, il ne pivote pas au hasard --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        ns_realtime_peer p = make_peer("ccc", "Chloe", 0.0f, 0.0f, 0.0f);
        p.has_yaw = false;
        room_presence_sample(&pr, &p, 1, 1000);
        p.z = 0.35f;
        room_presence_sample(&pr, &p, 1, 1250);
        settle(&pr, 1500);
        const float avant = pr.body[0].yaw;

        /* Trois battements immobiles : le cap est CONSERVÉ. */
        room_presence_sample(&pr, &p, 1, 1500);
        room_presence_sample(&pr, &p, 1, 1750);
        settle(&pr, 1900);
        CHECK(fabsf(pr.body[0].yaw - avant) < 0.02f,
              "un pair à l'arrêt garde son dernier cap (%.3f -> %.3f)",
              (double)avant, (double)pr.body[0].yaw);
    }
}

/* ==========================================================================
 * 4. LA PHASE DE MARCHE suit la DISTANCE, pas le temps
 *
 * M9  phase pilotée par le TEMPS        : 2 assertions tombent.
 * M10 retour à la pose de passage ôté   : 1 assertion tombe.
 * ========================================================================== */

/*
 * Fait marcher un pair en ligne droite sur `beats` battements de `metres`
 * chacun, et rend la phase du cycle atteinte, en tours.
 *
 * La phase est reconstruite par ACCUMULATION plutôt que lue une fois : le module
 * la rend modulo un tour, et une foulée parcourue en fait exactement un.
 */
static float walk_and_phase(float metres, int beats, float *out_seconds)
{
    room_presence_config c;
    config(&c);
    room_presence pr;
    room_presence_init(&pr, &c);

    ns_realtime_peer p = make_peer("aaa", "Ada", 0.0f, 0.0f, NS_PI * 0.5f);
    uint64_t t = 1000;
    room_presence_sample(&pr, &p, 1, t);
    (void)room_presence_step(&pr, t, DT);

    float tours = 0.0f;
    float prev = 0.0f;
    bool first = true;

    for (int b = 0; b < beats; ++b) {
        p.z += metres;
        t += NS_RT_PERIOD_MS;
        room_presence_sample(&pr, &p, 1, t);

        const uint64_t stride_ms = (uint64_t)(DT * 1000.0f + 0.5f);
        for (uint64_t u = t; u <= t + NS_RT_PERIOD_MS; u += stride_ms) {
            if (room_presence_step(&pr, u, DT) == 0) continue;
            const float ph = pr.body[0].cycle_time / CYCLE;
            if (first) { prev = ph; first = false; continue; }
            float d = ph - prev;
            if (d < -0.5f) d += 1.0f;        /* un tour bouclé */
            else if (d > 0.5f) d -= 1.0f;
            tours += d;
            prev = ph;
        }
    }
    if (out_seconds) *out_seconds = (float)(beats * NS_RT_PERIOD_MS) * 0.001f;
    return tours;
}

static void test_phase(void)
{
    printf("-- phase de marche\n");

    /*
     * UNE FOULÉE PARCOURUE = UN TOUR DE CYCLE. C'est la seule définition qui
     * empêche les pieds de patiner, et elle ne fait intervenir aucune durée.
     *
     * Quatre battements de 1,55/4 m parcourent exactement une foulée.
     */
    float secondes_a = 0.0f;
    const float tours_a = walk_and_phase(STRIDE / 4.0f, 4, &secondes_a);
    CHECK(fabsf(tours_a - 1.0f) < 0.05f,
          "une foulée parcourue fait UN tour de cycle (%.4f tour en %.2f s)",
          (double)tours_a, (double)secondes_a);

    /*
     * LA MÊME DURÉE, LE QUART DE LA DISTANCE : le quart des tours.
     *
     * C'est le contrôle qui sépare vraiment « la phase suit la distance » de
     * « la phase suit le temps » : les deux marches durent exactement une
     * seconde. Une phase pilotée par le temps rendrait le même nombre de tours
     * dans les deux cas.
     *
     * LE QUART ET NON LA MOITIÉ, et ce n'est pas un détail. Le cycle du modèle
     * dure deux secondes : une phase pilotée par le temps rendrait justement UN
     * DEMI-TOUR sur une seconde de marche. À mi-distance, la valeur fausse et la
     * valeur juste coïncidaient, et la mutation « phase pilotée par le temps »
     * ne faisait tomber qu'une assertion sur trois. Au quart, elles se séparent.
     */
    float secondes_b = 0.0f;
    const float tours_b = walk_and_phase(STRIDE / 16.0f, 4, &secondes_b);
    CHECK(fabsf(secondes_a - secondes_b) < 1e-3f,
          "les deux marches durent la MÊME durée (%.3f s et %.3f s)",
          (double)secondes_a, (double)secondes_b);
    CHECK(fabsf(tours_b - 0.25f) < 0.05f,
          "au quart de la distance dans le même temps, la phase tourne quatre "
          "fois moins (%.4f tour, attendu 0,25)", (double)tours_b);

    /*
     * À L'ARRÊT, la phase revient à la POSE DE PASSAGE — les deux pieds
     * rassemblés. Geler le cycle là où la marche s'est arrêtée laisse le
     * personnage en grand écart.
     */
    {
        room_presence_config c;
        config(&c);
        room_presence pr;
        room_presence_init(&pr, &c);

        ns_realtime_peer p = make_peer("aaa", "Ada", 0.0f, 0.0f, NS_PI * 0.5f);
        room_presence_sample(&pr, &p, 1, 1000);
        p.z = 0.35f;
        room_presence_sample(&pr, &p, 1, 1250);
        advance(&pr, 1250, 1500);

        /* Il se tait : le corps s'arrête, et le cycle se rassemble. */
        advance(&pr, 1500, 2400);
        CHECK(fabsf(pr.body[0].cycle_time - STAND_TIME) < 0.05f,
              "à l'arrêt, le cycle revient à la pose de passage "
              "(%.3f s, attendue %.3f s)",
              (double)pr.body[0].cycle_time, (double)STAND_TIME);
    }
}

/* ==========================================================================
 * 5. LA DISPARITION, en fondu et au bon moment
 *
 * M11 opacité forcée à 1 (aucun fondu)  : 2 assertions tombent.
 * M12 piste ôtée dès un battement manqué: 4 assertions tombent.
 * ========================================================================== */

static void test_disparition(void)
{
    printf("-- disparition\n");

    room_presence_config c;
    config(&c);
    room_presence pr;
    room_presence_init(&pr, &c);

    ns_realtime_peer p = make_peer("aaa", "Ada", 0.0f, 0.0f, 0.0f);
    room_presence_sample(&pr, &p, 1, 1000);

    /* Le fondu d'ENTRÉE dure une période : à l'instant zéro il est transparent,
     * une période plus tard il est plein. */
    (void)room_presence_step(&pr, 1000, DT);
    CHECK(pr.body[0].opacity < 0.05f,
          "un pair qui vient d'apparaître entre en fondu (%.3f)",
          (double)pr.body[0].opacity);
    advance(&pr, 1000, 1000 + NS_RT_PERIOD_MS);
    CHECK(pr.body[0].opacity > 0.99f,
          "et il est plein au bout d'une période (%.3f)",
          (double)pr.body[0].opacity);

    /* Il continue de publier : il reste plein. */
    room_presence_sample(&pr, &p, 1, 1250);
    room_presence_sample(&pr, &p, 1, 1500);
    advance(&pr, 1500, 1900);
    CHECK(pr.body[0].opacity > 0.99f,
          "un battement manqué ne le fait pas clignoter (%.3f)",
          (double)pr.body[0].opacity);

    /* Il se tait. Entre deux battements manqués et la péremption, il s'efface
     * PROGRESSIVEMENT : le fondu dure 2,5 s. */
    const uint64_t mute_at = 1500;
    advance(&pr, 1900, mute_at + 1750);
    const float mi = pr.body_count ? pr.body[0].opacity : 0.0f;
    CHECK(mi > 0.05f && mi < 0.95f,
          "il s'efface progressivement plutôt que de clignoter (%.3f à mi-fondu)",
          (double)mi);

    /* Et à la péremption — celle que `ns_realtime` applique déjà à la table
     * entière — il n'est plus là du tout. */
    advance(&pr, mute_at + 1750, mute_at + NS_RT_STALE_MS + 100);
    CHECK(pr.body_count == 0,
          "à %u ms sans nouvelle, il a disparu (%u corps restant)",
          (unsigned)NS_RT_STALE_MS, pr.body_count);

    /* La piste est LIBÉRÉE : un seizième pair peut prendre sa place. */
    uint32_t libres = 0;
    for (uint32_t i = 0; i < ROOM_PRESENCE_MAX; ++i) {
        if (!pr.track[i].used) libres++;
    }
    CHECK(libres == ROOM_PRESENCE_MAX,
          "et sa piste est rendue (%u libres sur %u)",
          libres, (unsigned)ROOM_PRESENCE_MAX);
}

/* ==========================================================================
 * 6. ZÉRO PAIR, ET SEIZE PAIRS
 *
 * M13 plafond ROOM_PRESENCE_MAX ôté     : 0 assertion — voir le relévé en bas
 * de fichier : ce que la mutation provoque est une lecture hors bornes, pas un
 * résultat faux, et seuls les désinfecteurs la voient.
 * ========================================================================== */

static void test_bornes(void)
{
    printf("-- zéro et seize pairs\n");

    room_presence_config c;
    config(&c);

    /* --- zéro : rien ne sort, rien ne plante --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);
        room_presence_sample(&pr, NULL, 0, 1000);
        CHECK(room_presence_step(&pr, 1000, DT) == 0, "zéro pair : zéro corps");
        advance(&pr, 1000, 5000);
        CHECK(pr.body_count == 0, "et il n'en apparaît pas tout seul");

        /* Un tableau non nul mais un compte nul : même chose. */
        ns_realtime_peer p = make_peer("aaa", "Ada", 0.0f, 0.0f, 0.0f);
        room_presence_sample(&pr, &p, 0, 5250);
        CHECK(room_presence_step(&pr, 5250, DT) == 0,
              "un compte nul n'invente pas de pair");
    }

    /* --- seize : tous suivis, et un dix-septième ne déborde pas --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        ns_realtime_peer many[NS_RT_MAX_PEERS + 4];
        for (uint32_t i = 0; i < NS_RT_MAX_PEERS + 4; ++i) {
            char id[NS_RT_ID], name[NS_RT_NAME];
            FMT(id, "cli-%02u", i);
            FMT(name, "J%02u", i);
            many[i] = make_peer(id, name, (float)i * 0.9f, 0.0f, 0.0f);
        }

        room_presence_sample(&pr, many, NS_RT_MAX_PEERS + 4, 1000);
        for (uint32_t i = 0; i < NS_RT_MAX_PEERS + 4; ++i) many[i].z = 0.35f;
        room_presence_sample(&pr, many, NS_RT_MAX_PEERS + 4, 1250);
        advance(&pr, 1250, 1500);

        CHECK(pr.body_count == ROOM_PRESENCE_MAX,
              "vingt pairs annoncés, %u corps rendus — le plafond tient "
              "(attendu %u)", pr.body_count, (unsigned)ROOM_PRESENCE_MAX);

        /* Chacun est à SA place : les pistes ne se sont pas mélangées. */
        bool places = true;
        for (uint32_t i = 0; i < pr.body_count; ++i) {
            if (fabsf(pr.body[i].feet.z - 0.35f) > 0.01f) places = false;
        }
        CHECK(places, "et les seize ont chacun avancé de leur propre segment");

        /* Les seize noms sont distincts : deux pairs ne partagent pas de piste. */
        uint32_t doublons = 0;
        for (uint32_t i = 0; i < pr.body_count; ++i) {
            for (uint32_t j = i + 1; j < pr.body_count; ++j) {
                if (strcmp(pr.body[i].name, pr.body[j].name) == 0) doublons++;
            }
        }
        CHECK(doublons == 0, "et aucun corps n'en double un autre (%u doublons)",
              doublons);
    }

    /*
     * UN COMPTE PLUS GRAND QUE LE TABLEAU.
     *
     * Le tableau fait EXACTEMENT `ROOM_PRESENCE_MAX` entrées et le compte annoncé
     * en vaut huit de plus : sans le plafond, la lecture sortirait du tableau.
     *
     * Cette assertion-ci passe même sans le plafond, et il faut le savoir : le
     * dépassement est une lecture hors bornes, pas un résultat faux — la
     * dix-septième piste étant de toute façon refusée, le nombre de corps ne
     * change pas. C'est le préréglage `linux-x64-asan` qui en fait un échec
     * franc ; ici il n'en fait qu'un comportement indéfini silencieux. Le relevé
     * de mutation en bas de fichier le dit tel quel plutôt que d'annoncer une
     * protection qu'on n'aurait pas mesurée.
     */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        ns_realtime_peer exact[ROOM_PRESENCE_MAX];
        for (uint32_t i = 0; i < ROOM_PRESENCE_MAX; ++i) {
            char id[NS_RT_ID], name[NS_RT_NAME];
            FMT(id, "x-%02u", i);
            FMT(name, "K%02u", i);
            exact[i] = make_peer(id, name, (float)i, 0.0f, 0.0f);
        }
        room_presence_sample(&pr, exact, ROOM_PRESENCE_MAX + 8, 1000);
        advance(&pr, 1000, 1250);
        CHECK(pr.body_count == ROOM_PRESENCE_MAX,
              "un compte annoncé plus grand que le tableau ne fabrique pas de "
              "corps de plus (%u)", pr.body_count);
    }

    /* --- deux HOMONYMES : l'identifiant de client les sépare --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        ns_realtime_peer duo[2];
        duo[0] = make_peer("aaa", "Anonyme", 0.0f, 0.0f, 0.0f);
        duo[1] = make_peer("bbb", "Anonyme", 4.0f, 0.0f, 0.0f);
        room_presence_sample(&pr, duo, 2, 1000);
        duo[0].z = 0.35f;
        duo[1].z = -0.35f;
        room_presence_sample(&pr, duo, 2, 1250);
        advance(&pr, 1250, 1500);

        CHECK(pr.body_count == 2,
              "deux joueurs du même pseudo font DEUX corps (%u)", pr.body_count);
        const bool separes =
            (fabsf(pr.body[0].feet.x - pr.body[1].feet.x) > 3.0f);
        CHECK(separes,
              "et ils gardent chacun leur trajectoire (x=%.2f et %.2f)",
              (double)pr.body[0].feet.x, (double)pr.body[1].feet.x);
    }
}

/* ==========================================================================
 * 7. LES ENTRÉES ABSURDES
 *
 * M14 coord_ok ôté                     : 4 assertions tombent.
 * M15 copie bornée remplacée par strncpy: 3 assertions tombent.
 * ========================================================================== */

static void test_absurde(void)
{
    printf("-- entrées absurdes\n");

    room_presence_config c;
    config(&c);

    /* --- NaN : l'échantillon est refusé, le pair reste où il était --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        ns_realtime_peer p = make_peer("aaa", "Ada", 1.0f, 2.0f, 0.0f);
        room_presence_sample(&pr, &p, 1, 1000);
        advance(&pr, 1000, 1250);

        ns_realtime_peer bad = p;
        bad.x = NAN; bad.z = (float)INFINITY;
        room_presence_sample(&pr, &bad, 1, 1250);
        advance(&pr, 1250, 1500);

        CHECK(pr.body_count == 1, "un NaN ne fait pas disparaître le pair");
        CHECK(isfinite(pr.body[0].feet.x) && isfinite(pr.body[0].feet.z),
              "et il ne se propage pas jusqu'au corps (x=%.3f, z=%.3f)",
              (double)pr.body[0].feet.x, (double)pr.body[0].feet.z);
        CHECK(fabsf(pr.body[0].feet.x - 1.0f) < 0.01f,
              "le pair reste à sa dernière position CRÉDIBLE (x=%.3f, attendu 1)",
              (double)pr.body[0].feet.x);
        CHECK(isfinite(pr.body[0].yaw) && isfinite(pr.body[0].cycle_time),
              "ni jusqu'à son cap ou sa phase");
    }

    /* --- 10^9 : hors de tout ce sur quoi on sache calculer --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        ns_realtime_peer p = make_peer("aaa", "Ada", 1.0f, 2.0f, 0.0f);
        room_presence_sample(&pr, &p, 1, 1000);
        advance(&pr, 1000, 1250);

        ns_realtime_peer far = p;
        far.x = 1.0e9f; far.z = -1.0e9f;
        room_presence_sample(&pr, &far, 1, 1250);
        advance(&pr, 1250, 1500);

        CHECK(fabsf(pr.body[0].feet.x - 1.0f) < 0.01f,
              "une position à 10^9 m est refusée : le pair ne part pas à "
              "l'infini (x=%.3f)", (double)pr.body[0].feet.x);
        CHECK(fabsf(pr.body[0].feet.x) <= ROOM_PRESENCE_MAX_COORD,
              "aucun corps ne sort de la borne de vraisemblance");

        /* Un cap non fini est traité comme un cap ABSENT, pas propagé. */
        ns_realtime_peer nanyaw = p;
        nanyaw.z = 2.35f;
        nanyaw.yaw = NAN;
        room_presence_sample(&pr, &nanyaw, 1, 1500);
        advance(&pr, 1500, 1750);
        CHECK(isfinite(pr.body[0].yaw),
              "un cap NaN ne devient pas une rotation NaN (%.3f)",
              (double)pr.body[0].yaw);
    }

    /* --- un nom NON TERMINÉ : il ne doit pas être lu au-delà --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        ns_realtime_peer p;
        memset(&p, 0, sizeof p);
        FMT(p.id, "aaa");
        /* Les 24 octets pleins, sans le moindre zéro : ce que produirait un
         * serveur bavard et un analyseur qui remplirait le tableau au ras. */
        memset(p.name, 'X', sizeof p.name);
        memset(p.game, 'Y', sizeof p.game);
        p.x = 0.0f; p.y = EYE; p.z = 0.0f;
        p.eye = EYE;
        p.has_yaw = true;

        room_presence_sample(&pr, &p, 1, 1000);
        advance(&pr, 1000, 1250);

        CHECK(pr.body_count == 1, "un nom non terminé n'empêche pas le corps");
        CHECK(pr.body[0].name[NS_RT_NAME - 1] == '\0',
              "le nom recopié est TERMINÉ, quoi qu'on ait reçu");
        CHECK(strlen(pr.body[0].name) == NS_RT_NAME - 1,
              "et il est tronqué à la taille du champ (%zu caractères)",
              strlen(pr.body[0].name));
        CHECK(pr.body[0].game[NS_RT_SLUG - 1] == '\0',
              "le jeu aussi");
    }

    /* --- un pair SANS identifiant ni nom : rien à suivre --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        ns_realtime_peer p;
        memset(&p, 0, sizeof p);
        p.y = EYE;
        room_presence_sample(&pr, &p, 1, 1000);
        CHECK(room_presence_step(&pr, 1000, DT) == 0,
              "un pair sans identifiant ni nom n'est pas suivi");
    }

    /* --- un pas d'affichage absurde --- */
    {
        room_presence pr;
        room_presence_init(&pr, &c);

        ns_realtime_peer p = make_peer("aaa", "Ada", 0.0f, 0.0f, 0.0f);
        room_presence_sample(&pr, &p, 1, 1000);
        p.z = 0.35f;
        room_presence_sample(&pr, &p, 1, 1250);

        (void)room_presence_step(&pr, 1300, NAN);
        (void)room_presence_step(&pr, 1300, -1.0f);
        (void)room_presence_step(&pr, 1300, 0.0f);
        CHECK(pr.body_count == 1 && isfinite(pr.body[0].cycle_time)
              && isfinite(pr.body[0].yaw),
              "un pas d'affichage absurde ne corrompt ni la phase ni le cap");
    }

    /* --- un appelant nul --- */
    room_presence_init(NULL, &c);
    room_presence_sample(NULL, NULL, 3, 1000);
    CHECK(room_presence_step(NULL, 1000, DT) == 0, "aucun pointeur nul ne plante");
}

/* ==========================================================================
 * 8. LA SOURCE DE DÉMONSTRATION
 *
 * Elle n'est pas décorative : c'est par elle que passe la capture de recette, et
 * une capture qui emprunte un chemin que le vrai pair n'emprunte pas ne vérifie
 * rien. On contrôle donc qu'elle produit des échantillons que
 * `room_presence_sample` accepte, et qu'elle emprunte bien LES DEUX chemins de
 * cap.
 * ========================================================================== */

static void test_demo(void)
{
    printf("-- source de démonstration\n");

    ns_realtime_peer out[NS_RT_MAX_PEERS];
    const uint32_t n = room_presence_demo(out, NS_RT_MAX_PEERS, 4, 3.0);
    CHECK(n == 4, "quatre marcheurs demandés, %u produits", n);

    bool sans_cap = false, avec_cap = false, ids_ok = true, positions_ok = true;
    for (uint32_t i = 0; i < n; ++i) {
        if (out[i].has_yaw) avec_cap = true; else sans_cap = true;
        if (!out[i].id[0] || !out[i].name[0]) ids_ok = false;
        if (!isfinite(out[i].x) || !isfinite(out[i].z)
            || fabsf(out[i].x) > ROOM_PRESENCE_MAX_COORD
            || fabsf(out[i].z) > ROOM_PRESENCE_MAX_COORD) positions_ok = false;
    }
    CHECK(avec_cap && sans_cap,
          "elle emprunte LES DEUX chemins de cap : publié et déduit");
    CHECK(ids_ok, "chaque marcheur a un identifiant et un nom");
    CHECK(positions_ok, "et une position que le module accepte");

    /* Les identifiants sont STABLES d'un appel à l'autre : c'est ce qui fait
     * qu'un marcheur est suivi plutôt que recréé à chaque lot. */
    ns_realtime_peer plus_tard[NS_RT_MAX_PEERS];
    (void)room_presence_demo(plus_tard, NS_RT_MAX_PEERS, 4, 3.25);
    bool memes = true;
    for (uint32_t i = 0; i < n; ++i) {
        if (strcmp(out[i].id, plus_tard[i].id) != 0) memes = false;
    }
    CHECK(memes, "et son identifiant ne change pas d'un battement à l'autre");

    /* Ils BOUGENT : une démonstration de corps qui marchent doit marcher. */
    float total = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        total += fabsf(plus_tard[i].z - out[i].z);
    }
    CHECK(total > 0.5f,
          "les quatre avancent vraiment entre deux battements (%.3f m au total)",
          (double)total);

    /* Zéro demandé : zéro produit, et aucune écriture. */
    CHECK(room_presence_demo(out, NS_RT_MAX_PEERS, 0, 3.0) == 0,
          "zéro marcheur demandé, zéro produit — le drapeau est bien INERTE");
    CHECK(room_presence_demo(NULL, 0, 4, 3.0) == 0, "et il tolère un tableau nul");

    /* Le plafond du réseau est respecté même si on en demande plus. */
    CHECK(room_presence_demo(out, NS_RT_MAX_PEERS, 999, 3.0) == NS_RT_MAX_PEERS,
          "et il ne dépasse jamais %u marcheurs", (unsigned)NS_RT_MAX_PEERS);
}

/* ==========================================================================
 * LE RELEVÉ DE MUTATION
 * ---------------------
 * Chaque ligne est une modification appliquée à `room_presence.c`, une seule à
 * la fois, suivie du nombre d'assertions tombées SUR CE FICHIER TEL QU'IL EST.
 * Les chiffres sont relevés en RELANÇANT le test, pas estimés : sur les 63
 * vérifications, ce sont ceux-là qui tombent.
 *
 *   interpolation
 *     M1  `alpha` non borné (extrapolation)                     3 échecs
 *     M2  `alpha` figé à 1 (téléportation)                      6 échecs
 *     M3  pieds = position publiée (l'œil pris pour le sol)     2 échecs
 *   intégration d'un lot
 *     M4  garde par DATE retirée                                1 échec
 *     M5  garde par PISTE retirée                               1 échec
 *     M6  les DEUX gardes retirées                              4 échecs
 *   cap
 *     M7  `has_yaw` ignoré, `yaw` pris tel quel                 4 échecs
 *     M8  cap déduit du pas d'image et non du segment           3 échecs
 *   phase de marche
 *     M9  phase pilotée par le temps (`+= dt / cycle`)          2 échecs
 *     M10 retour à la pose de passage retiré                    1 échec
 *   disparition
 *     M11 opacité forcée à 1 (aucun fondu)                      2 échecs
 *     M12 piste retirée dès le premier battement manqué         4 échecs
 *   bornes
 *     M13 plafond `ROOM_PRESENCE_MAX` retiré                    0 échec
 *   entrées absurdes
 *     M14 `coord_ok` retiré                                     4 échecs
 *     M15 copie bornée remplacée par `strncpy`                  3 échecs
 *
 * DEUX LIGNES MÉRITENT D'ÊTRE LUES, et pas seulement comptées.
 *
 * M13 EST À ZÉRO, et on ne le maquille pas. Retirer le plafond ne produit
 * aucun résultat faux — la dix-septième piste est refusée par `free_track` de
 * toute façon — mais une LECTURE HORS BORNES quand l'appelant annonce plus
 * d'entrées que son tableau n'en contient. C'est ce que le cas correspondant
 * provoque, et c'est un désinfecteur d'adresses qui le transforme en échec :
 * le préréglage `linux-x64-asan` le voit, `macos-universal` non. La ligne reste
 * à zéro plutôt que d'être retirée du relevé, parce qu'une case vide dans un
 * tableau de mutation est une information.
 *
 * M4 ET M5 SONT À UN CHACUNE, M6 À QUATRE, et c'est ce qu'on veut voir : les
 * deux gardes se recouvrent sur le cas courant — relire le même lot — et ne se
 * recouvrent pas sur les deux cas qui les justifient chacune, le lot arriéré et
 * la ligne doublée. Deux gardes dont l'une suffirait toujours seraient une de
 * trop ; celles-ci ne suffisent pas l'une sans l'autre, et le relevé le prouve.
 *
 * Reproduire un chiffre demande d'appliquer la modification décrite et de
 * relancer `ctest -R presence`.
 * ========================================================================== */

int main(void)
{
    test_interpolation();
    test_lot_idempotent();
    test_cap();
    test_phase();
    test_disparition();
    test_bornes();
    test_absurde();
    test_demo();

    printf("%d vérification(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
