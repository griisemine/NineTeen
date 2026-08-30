/* room_presence.c — voir room_presence.h : ce qui est interpolé, ce qui est
 * déduit, et pourquoi un pair n'est pas un obstacle. */
#include "room_presence.h"

#include <math.h>
#include <string.h>

/*
 * LE FONDU DE SORTIE, et d'où viennent ses deux bornes.
 *
 * Aucune n'est inventée ici : la première est un battement manqué, la seconde
 * est la péremption que `ns_realtime.c` applique déjà à toute la table.
 *
 *   * Jusqu'à DEUX périodes sans nouvelle (500 ms), le pair reste plein. Une
 *     requête HTTP qui traîne ne doit pas le faire clignoter — c'est exactement
 *     ce que le serveur invoque pour son propre TTL de douze secondes.
 *   * À `NS_RT_STALE_MS` (3 000 ms), il a disparu et sa piste est libérée.
 *
 * Le fondu dure donc 2 500 ms. C'est long, et c'est voulu : ce n'est pas une
 * animation de sortie, c'est le TEMPS PENDANT LEQUEL ON NE SAIT PAS. Un joueur
 * dont la liaison hoquette revient sans avoir jamais clignoté ; un joueur qui a
 * fermé le jeu s'efface sans qu'on ait eu à décider d'un instant précis où il
 * n'était plus là.
 */
#define FADE_HOLD_MS (2u * NS_RT_PERIOD_MS)

/*
 * LE FONDU D'ENTRÉE : une période. Un corps qui apparaît d'un coup au milieu de
 * l'allée se lit comme un défaut d'affichage ; le même corps qui prend un quart
 * de seconde à devenir opaque se lit comme quelqu'un qui arrive. La durée est
 * celle du battement plutôt qu'un nombre choisi — c'est de toute façon le temps
 * qu'il faut pour connaître sa deuxième position, donc pour savoir s'il marche.
 */
#define FADE_IN_MS   NS_RT_PERIOD_MS

/*
 * L'AMORTI DU CAP, en fractions rattrapées par seconde.
 *
 * 12, c'est-à-dire 95 % du chemin en une période de 250 ms (1 - e^-3). Ce n'est
 * pas un lissage cosmétique : le cap DÉDUIT d'un déplacement est constant sur
 * tout un segment puis change d'un coup au segment suivant. Un pair qui tourne
 * au bout de l'allée pivoterait de quatre-vingt-dix degrés en une image. Amorti
 * sur la durée même du battement, il tourne pendant qu'il avance.
 *
 * Le cap PUBLIÉ passe par le même amorti, et c'est délibéré : sans quoi un pair
 * tournerait autrement selon la version qu'il fait tourner, ce qui est
 * exactement le genre d'écart qu'on ne remarque jamais et qui rend une salle
 * incohérente.
 */
#define YAW_RATE 12.0f

/*
 * LE RETOUR À LA POSE DEBOUT, à l'arrêt. 6 par seconde, la MÊME valeur que le
 * joueur emploie pour lui-même dans `room/main.c`. Deux valeurs différentes
 * donneraient deux façons de s'arrêter dans la même salle.
 */
#define STAND_RATE 6.0f

/* ========================================================================== */
/* Outils                                                                     */
/* ========================================================================== */

/*
 * Une copie qui TERMINE toujours, même si la source ne le fait pas.
 *
 * `ns_realtime_peer.name` est un tableau de 24 octets rempli par un analyseur
 * JSON qui a lu ce qu'un serveur lui a envoyé. Rien dans le type ne garantit
 * qu'il porte un zéro : `strncpy` recopierait alors 24 octets sans terminer, et
 * le premier `snprintf("%s")` de l'étiquette lirait au-delà. On borne donc à la
 * TAILLE DE DESTINATION moins un, et on écrit le zéro nous-mêmes.
 */
static void copy_bounded(char *dst, size_t dst_size, const char *src, size_t src_size)
{
    size_t n = 0;
    const size_t cap = (dst_size - 1 < src_size) ? dst_size - 1 : src_size;
    while (n < cap && src[n] != '\0') { dst[n] = src[n]; ++n; }
    dst[n] = '\0';
}

/* Une coordonnée sur laquelle on sache calculer. Voir ROOM_PRESENCE_MAX_COORD. */
static bool coord_ok(float v)
{
    return isfinite(v) && fabsf(v) <= ROOM_PRESENCE_MAX_COORD;
}

/* L'écart le plus court entre deux angles, dans (-pi, pi]. */
static float angle_delta(float from, float to)
{
    float d = to - from;
    while (d >  NS_PI) d -= NS_TAU;
    while (d < -NS_PI) d += NS_TAU;
    return d;
}

/* Une fraction de cycle ramenée dans [0,1). `fmodf` rend un résultat du signe du
 * dividende, et une phase négative échantillonnerait le cycle à l'envers. */
static float wrap01(float v)
{
    if (!isfinite(v)) return 0.0f;
    v = fmodf(v, 1.0f);
    return (v < 0.0f) ? v + 1.0f : v;
}

/* ========================================================================== */
/* Cycle de vie                                                               */
/* ========================================================================== */

void room_presence_init(room_presence *pr, const room_presence_config *cfg)
{
    if (!pr) return;
    memset(pr, 0, sizeof *pr);
    if (cfg) pr->cfg = *cfg;
}

/* ========================================================================== */
/* L'intégration d'un lot                                                     */
/* ========================================================================== */

/*
 * La CLÉ de suivi.
 *
 * L'identifiant de client d'abord : c'est le seul champ qui distingue deux
 * joueurs qui se seraient donné le même pseudo, et « Anonyme » est le pseudo par
 * défaut. À défaut — un serveur qui ne le renverrait pas — on retombe sur le
 * nom, ce qui vaut mieux que de ne rien suivre du tout : deux homonymes
 * échangeront leurs jambes, quinze autres marcheront juste.
 */
static void peer_key(const ns_realtime_peer *p, char *out, size_t out_size)
{
    if (p->id[0]) copy_bounded(out, out_size, p->id, sizeof p->id);
    else          copy_bounded(out, out_size, p->name, sizeof p->name);
}

static room_presence_track *find_track(room_presence *pr, const char *key)
{
    for (uint32_t i = 0; i < ROOM_PRESENCE_MAX; ++i) {
        if (pr->track[i].used && strcmp(pr->track[i].id, key) == 0) return &pr->track[i];
    }
    return NULL;
}

static room_presence_track *free_track(room_presence *pr)
{
    for (uint32_t i = 0; i < ROOM_PRESENCE_MAX; ++i) {
        if (!pr->track[i].used) return &pr->track[i];
    }
    return NULL;
}

void room_presence_sample(room_presence *pr, const ns_realtime_peer *peers,
                          uint32_t count, uint64_t at_ms)
{
    if (!pr) return;

    /*
     * Une date NULLE veut dire « aucun lot » : c'est ce que rend
     * `ns_realtime_peers` tant qu'aucune réponse n'est arrivée. Il n'y a rien à
     * intégrer, et surtout rien à dater.
     */
    if (at_ms == 0) return;

    /*
     * Un lot DÉJÀ VU ne se réintègre pas. La boucle de jeu relit la table à
     * chaque image, et sans ce test `from` rejoindrait `to` en une image :
     * l'interpolation n'aurait plus rien à faire et les corps sauteraient d'un
     * battement à l'autre. Voir `room_presence.last_batch_ms`.
     *
     * Un lot plus VIEUX que le dernier intégré est refusé pour la même raison,
     * et parce qu'il ne peut venir que d'un appelant qui s'est trompé d'horloge.
     */
    if (at_ms <= pr->last_batch_ms) return;
    pr->last_batch_ms = at_ms;

    if (!peers) count = 0;
    if (count > ROOM_PRESENCE_MAX) count = ROOM_PRESENCE_MAX;

    for (uint32_t i = 0; i < count; ++i) {
        const ns_realtime_peer *p = &peers[i];

        char key[NS_RT_ID];
        peer_key(p, key, sizeof key);
        if (!key[0]) continue;          /* ni identifiant ni nom : rien à suivre */

        /* Un échantillon inutilisable laisse le pair sur son segment précédent
         * plutôt que de l'envoyer nulle part. Voir `coord_ok`. */
        if (!coord_ok(p->x) || !coord_ok(p->y) || !coord_ok(p->z)) continue;

        /*
         * LES PIEDS, et non l'œil. `y` est la position de la caméra du pair ;
         * son corps commence `eye` plus bas. Un pair qui ne publie pas sa
         * hauteur d'œil (`eye == 0`) reçoit le repli de la configuration —
         * sinon son corps flotterait, pieds à hauteur de regard.
         */
        float eye = (p->eye > 0.0f && coord_ok(p->eye)) ? p->eye : pr->cfg.eye_default;
        const ns_v3 feet = ns_v3_make(p->x, p->y - eye, p->z);

        room_presence_track *t = find_track(pr, key);
        if (t && t->last_ms == at_ms) continue;   /* deux lignes pour un même pair */

        if (!t) {
            t = free_track(pr);
            if (!t) continue;                     /* seize suivis, le dix-septième attend */
            memset(t, 0, sizeof *t);
            t->used    = true;
            t->born_ms = at_ms;
            copy_bounded(t->id, sizeof t->id, key, sizeof key);
            /* Premier échantillon : le segment est un point. `room_presence_step`
             * le rend tel quel, et le pair se met à marcher au second. */
            t->from = t->to = feet;
            t->from_ms = t->to_ms = at_ms;
            t->yaw_from = t->yaw_to = 0.0f;
            t->phase = wrap01((pr->cfg.cycle > 0.0f)
                              ? pr->cfg.stand_time / pr->cfg.cycle : 0.0f);
        } else {
            t->from    = t->to;
            t->from_ms = t->to_ms;
            t->to      = feet;
            t->to_ms   = at_ms;
            t->yaw_from = t->yaw_to;
        }

        /*
         * LE CAP, et la seule chose qui sépare « absent » de « nul ».
         *
         * `has_yaw` vient de la PRÉSENCE DE LA CLÉ dans le JSON, pas de sa
         * valeur : voir `ns_realtime.c`. Un cap non fini est traité comme un cap
         * absent, ce qui renvoie le pair sur la déduction par déplacement plutôt
         * que de propager un NaN dans une matrice de rotation.
         */
        t->has_yaw = p->has_yaw && isfinite(p->yaw);
        t->yaw_to  = t->has_yaw ? p->yaw : t->yaw_to;

        t->eye      = eye;
        t->verified = p->verified;
        t->score    = p->score;
        t->last_ms  = at_ms;
        copy_bounded(t->name, sizeof t->name, p->name, sizeof p->name);
        copy_bounded(t->game, sizeof t->game, p->game, sizeof p->game);
    }
}

/* ========================================================================== */
/* L'avancement des corps                                                     */
/* ========================================================================== */

/* L'âge de la dernière nouvelle. Une horloge qui recule ne doit pas produire un
 * âge énorme par soustraction non signée : c'est un appelant qui s'est trompé,
 * pas une disparition. */
static uint64_t track_age(const room_presence_track *t, uint64_t now_ms)
{
    return (now_ms > t->last_ms) ? now_ms - t->last_ms : 0;
}

/*
 * L'OPACITÉ d'un pair : le plus petit de son fondu d'entrée et de son fondu de
 * sortie.
 *
 * ELLE NE DÉCIDE PAS DE LA DISPARITION, et c'est la leçon d'un défaut que le
 * test a trouvé avant l'écran : la première version retirait la piste dès que
 * l'opacité tombait à zéro, or elle vaut EXACTEMENT zéro à la première image
 * d'un pair — c'est le début du fondu d'entrée. Tout pair était donc effacé à
 * l'instant même où il apparaissait, et la salle restait vide sans qu'aucune
 * erreur ne soit émise. C'est l'ÂGE qui décide, et lui seul.
 */
static float track_opacity(const room_presence_track *t, uint64_t now_ms)
{
    const uint64_t age = track_age(t, now_ms);
    if (age >= NS_RT_STALE_MS) return 0.0f;

    float out = 1.0f;
    if (age > FADE_HOLD_MS) {
        out = 1.0f - (float)(age - FADE_HOLD_MS)
                   / (float)(NS_RT_STALE_MS - FADE_HOLD_MS);
    }

    const uint64_t life = (now_ms > t->born_ms) ? now_ms - t->born_ms : 0;
    const float in = (life >= FADE_IN_MS) ? 1.0f : (float)life / (float)FADE_IN_MS;

    return ns_clampf(ns_minf(in, out), 0.0f, 1.0f);
}

uint32_t room_presence_step(room_presence *pr, uint64_t now_ms, float dt)
{
    if (!pr) return 0;
    if (!(dt > 0.0f) || !isfinite(dt)) dt = 0.0f;

    pr->body_count = 0;

    for (uint32_t i = 0; i < ROOM_PRESENCE_MAX; ++i) {
        room_presence_track *t = &pr->track[i];
        if (!t->used) continue;

        /* LA PÉREMPTION, et elle seule, retire une piste. Voir `track_opacity` :
         * une opacité nulle est aussi ce que rend la PREMIÈRE image d'un pair. */
        if (track_age(t, now_ms) >= NS_RT_STALE_MS) { memset(t, 0, sizeof *t); continue; }
        const float opacity = track_opacity(t, now_ms);

        /*
         * L'INTERPOLATION, et le retard qu'elle coûte — mesuré, pas estimé.
         *
         * `alpha` court de 0 à 1 sur la durée qui a séparé les deux dernières
         * positions reçues, en partant de l'instant où la SECONDE est arrivée.
         * Autrement dit : à l'instant où la position k arrive, on affiche la
         * position k-1 ; une période plus tard, on affiche la position k.
         *
         * Le corps montré est donc en retard d'exactement une période sur ce que
         * le serveur sait, soit 250 ms au rythme nominal — plus le trajet réseau
         * du pair jusqu'au serveur et du serveur jusqu'ici. À 1,4 m/s, cela met
         * le corps 35 cm derrière sa position réelle. C'est le prix de ne jamais
         * extrapoler, et c'est aussi le chiffre qui interdit d'en faire un
         * obstacle (voir l'en-tête).
         *
         * `alpha` est BORNÉ À 1 et jamais au-delà : c'est là toute la différence
         * avec une extrapolation. Un pair qui se tait s'arrête là où on l'a vu
         * pour la dernière fois, au lieu de continuer tout droit puis de reculer
         * quand la vérité arrive.
         */
        float alpha = 1.0f;
        if (t->to_ms > t->from_ms) {
            const uint64_t span = t->to_ms - t->from_ms;
            const uint64_t since = (now_ms > t->to_ms) ? now_ms - t->to_ms : 0;
            alpha = ns_clampf((float)since / (float)span, 0.0f, 1.0f);
        }
        const ns_v3 pos = ns_v3_lerp(t->from, t->to, alpha);

        /*
         * LA DISTANCE, mesurée sur le corps RENDU et non sur le segment reçu.
         *
         * C'est ce qui empêche les pieds de patiner : le personnage avance
         * exactement de ce dont sa phase tourne, quelle que soit la façon dont
         * l'interpolation a réparti ce déplacement dans le temps. Prendre la
         * longueur du segment réseau et la diviser par sa durée donnerait la
         * bonne moyenne mais la mauvaise vitesse instantanée — et le glissement
         * se voit sur la première image où les deux diffèrent.
         *
         * HORIZONTALE seulement : une marche ne fait pas tourner les jambes plus
         * vite parce que le sol descend.
         */
        float step = 0.0f;
        if (t->shown_ok) {
            const float dx = pos.x - t->shown.x;
            const float dz = pos.z - t->shown.z;
            step = sqrtf(dx * dx + dz * dz);
        }
        t->shown = pos;
        t->shown_ok = true;

        const float speed = (dt > 0.0f) ? step / dt : 0.0f;

        /*
         * LA PHASE : elle suit la distance, jamais le temps.
         *
         * Une foulée parcourue fait exactement un tour de cycle. À l'arrêt on
         * RAMÈNE la phase vers la pose de passage — l'instant mesuré où les deux
         * pieds se croisent — plutôt que de la geler : figer le cycle là où la
         * marche s'est arrêtée laisse le personnage en grand écart, ce que le
         * joueur avait déjà payé en capture.
         *
         * Le retour se fait par le PLUS COURT CHEMIN SUR LE CERCLE. Amortir une
         * phase de 0,95 vers une cible de 0,16 en ligne droite ferait reculer le
         * cycle de huit dixièmes de tour : le personnage marcherait à l'envers
         * pendant une demi-seconde avant de s'arrêter.
         */
        if (speed > ROOM_PRESENCE_WALK_MIN && pr->cfg.stride > 0.0f) {
            t->phase = wrap01(t->phase + step / pr->cfg.stride);
        } else if (pr->cfg.cycle > 0.0f) {
            const float target = wrap01(pr->cfg.stand_time / pr->cfg.cycle);
            float d = target - t->phase;
            while (d >  0.5f) d -= 1.0f;
            while (d < -0.5f) d += 1.0f;
            t->phase = wrap01(t->phase + d * (1.0f - expf(-STAND_RATE * dt)));
        }

        /*
         * LE CAP. Publié quand le pair le publie ; DÉDUIT du déplacement sinon.
         *
         * La déduction ne s'appuie pas sur le pas d'une image — trop court, donc
         * trop bruité — mais sur le SEGMENT ENTIER reçu : c'est la direction sur
         * 250 ms, et elle ne bouge pas pendant qu'on la parcourt. Sous le seuil
         * de marche, on garde le dernier cap connu : quelqu'un qui s'arrête ne
         * pivote pas au hasard.
         *
         * La convention est celle du jeu — l'angle est compté depuis +X vers +Z,
         * comme `room_camera` fait avancer la caméra vers (cos, sin).
         */
        float yaw_target;
        if (t->has_yaw) {
            yaw_target = t->yaw_from + angle_delta(t->yaw_from, t->yaw_to) * alpha;
        } else {
            const float sx = t->to.x - t->from.x;
            const float sz = t->to.z - t->from.z;
            const float seg = sqrtf(sx * sx + sz * sz);
            const uint64_t span = (t->to_ms > t->from_ms) ? t->to_ms - t->from_ms : 0;
            const float seg_speed = (span > 0) ? seg / ((float)span * 0.001f) : 0.0f;
            if (seg_speed > ROOM_PRESENCE_WALK_MIN) yaw_target = atan2f(sz, sx);
            else if (t->yaw_shown_ok)               yaw_target = t->yaw_shown;
            else                                    yaw_target = 0.0f;
        }
        if (!t->yaw_shown_ok) { t->yaw_shown = yaw_target; t->yaw_shown_ok = true; }
        else {
            t->yaw_shown += angle_delta(t->yaw_shown, yaw_target)
                          * (1.0f - expf(-YAW_RATE * dt));
        }

        room_presence_body *b = &pr->body[pr->body_count++];
        memset(b, 0, sizeof *b);
        copy_bounded(b->name, sizeof b->name, t->name, sizeof t->name);
        copy_bounded(b->game, sizeof b->game, t->game, sizeof t->game);
        b->verified   = t->verified;
        b->score      = t->score;
        b->feet       = pos;
        b->yaw        = t->yaw_shown;
        b->cycle_time = t->phase * pr->cfg.cycle;
        b->speed      = speed;
        b->opacity    = opacity;
        /*
         * L'ÉTIQUETTE se pose sur la TÊTE, et sa hauteur est celle du modèle.
         *
         * Elle flottait à 1,75 m au-dessus de `y`, c'est-à-dire au-dessus de
         * l'ŒIL du pair — donc à trois mètres quarante du sol, sous le plafond
         * qui en fait 2,92. Elle est maintenant posée sur les pieds plus la
         * hauteur mesurée du personnage, plus un dégagement d'un dixième de
         * cette hauteur : de quoi ne pas la coller au crâne sans dépendre d'une
         * cote écrite à la main.
         */
        b->label_y = pos.y + pr->cfg.height * 1.1f;
    }

    return pr->body_count;
}

/* ========================================================================== */
/* La salle peuplée sans serveur                                              */
/* ========================================================================== */

uint32_t room_presence_demo(ns_realtime_peer *out, uint32_t max,
                            uint32_t count, double t_seconds)
{
    if (!out || max == 0) return 0;
    if (count > max) count = max;
    if (count > NS_RT_MAX_PEERS) count = NS_RT_MAX_PEERS;

    /*
     * L'ALLÉE CENTRALE, cadrée SUR LE POINT DE VUE QUI SERT À LA REGARDER.
     *
     * La salle déclare une capture nommée « allee » en (0 ; 1,70 ; -2,60), cap
     * 90 — c'est-à-dire regardant vers +Z. Les marcheurs font donc l'aller-
     * retour sur Z entre 0,5 et 6,0 : entièrement DEVANT cet objectif, et à
     * l'intérieur de l'emprise de circulation que la salle déclare (z de -7,0 à
     * 12,6).
     *
     * LES DEUX BORNES SONT CADRÉES, et c'est ce qui rend la capture reproductible
     * quel que soit l'instant où elle tombe. Au plus près, un marcheur est à
     * 3,1 m de l'objectif : sa demi-hauteur y couvre 16,8 degrés pour un
     * demi-champ vertical de 31, donc il tient entier dans le cadre. Les deux
     * premiers essais commençaient à -5,0 puis à -1,0, c'est-à-dire à moins de
     * deux mètres du point de vue : le cadre était rempli d'un dos et on ne
     * voyait plus la salle. Au plus loin, 8,6 m, il reste devant le comptoir du
     * bar (z = 6,05) et non derrière.
     *
     * Quatre couloirs à ±0,35 et ±1,05 m de l'axe. Ils étaient à ±1,50 : à
     * cinq mètres, ces deux-là passaient DERRIÈRE les grappes de bornes, et la
     * capture ne montrait que deux corps sur quatre avec deux étiquettes
     * flottant au-dessus de rien. C'est d'ailleurs la preuve en image que le
     * corps est occulté par le décor et que l'étiquette ne l'est pas — voir
     * `draw_presence` — mais ce n'est pas ce qu'on cherchait à photographier.
     *
     * 1,4 m/s : la vitesse de marche d'un piéton, celle que `ns_realtime.h`
     * invoque déjà pour justifier son rythme de 4 Hz.
     */
    static const char *NOMS[] = { "Ada", "Bob", "Chloe", "Dan",
                                  "Elise", "Femi", "Gus", "Hana" };
    static const char *JEUX[] = { "TETRIS", "", "SNAKE", "", "PONG", "", "CASSE", "" };

    const float EYE = 1.63f;      /* la hauteur d'œil debout du jeu */
    const float SPEED = 1.4f;
    const float Z0 = 0.5f, Z1 = 6.0f;
    const float SPAN = Z1 - Z0;

    for (uint32_t i = 0; i < count; ++i) {
        ns_realtime_peer *p = &out[i];
        memset(p, 0, sizeof *p);

        /* Un identifiant stable d'un appel à l'autre : c'est lui qui fait que le
         * marcheur est SUIVI plutôt que recréé à chaque lot. */
        p->id[0] = 'd'; p->id[1] = 'e'; p->id[2] = 'm'; p->id[3] = 'o';
        p->id[4] = (char)('0' + (int)(i % 10u));
        p->id[5] = '\0';
        copy_bounded(p->name, sizeof p->name, NOMS[i % 8u], 8u);
        copy_bounded(p->game, sizeof p->game, JEUX[i % 8u], 8u);
        p->verified = ((i % 3u) != 1u);
        p->score = (int32_t)(1200 + 137 * (int)i);

        /*
         * Un aller-retour en dents de scie, de période 2 : la première moitié
         * monte, la seconde redescend. Les marcheurs sont décalés RÉGULIÈREMENT
         * sur cette période — `2 i / count` et non un pas fixe — pour qu'ils
         * restent étalés quel qu'en soit le nombre. Un pas fixe finissait par
         * les grouper : à quatre, trois d'entre eux se retrouvaient au fond
         * contre le bar et un seul dans l'allée.
         *
         * Le décalage les met aussi en sens opposés deux à deux, ce qui vaut
         * mieux qu'un défilé : on voit des visages ET des dos, donc on voit que
         * le cap est vraiment tenu.
         *
         * CHACUN SON ALLURE, et ce n'est pas de la décoration. Une dent de scie
         * est SYMÉTRIQUE : à vitesse commune, deux décalages également écartés
         * de son repli se replient au même endroit, et les quatre marcheurs se
         * retrouvent groupés deux par deux au milieu de l'allée — c'est ce que
         * montrait la capture. Des allures différentes les font dériver les uns
         * par rapport aux autres, ce qui casse la symétrie ; et elles exercent
         * au passage ce que le module promet, à savoir une cadence de jambes qui
         * suit la vitesse sans qu'on la lui dise. De 1,15 à 1,65 m/s : l'écart
         * réel entre un piéton lent et un piéton pressé.
         */
        const double vitesse = (double)SPEED * (0.82 + 0.12 * (double)(i % 4u));
        const double phase = fmod(t_seconds * vitesse / (double)SPAN
                                  + 2.0 * (double)i / (double)count, 2.0);
        const double u = (phase < 1.0) ? phase : 2.0 - phase;
        const bool   going_north = (phase < 1.0);

        p->x = -1.05f + 0.7f * (float)(i % 4u);
        p->y = EYE;                       /* le sol de la salle est à zéro */
        p->z = Z0 + SPAN * (float)u;
        p->eye = EYE;

        /*
         * LE PAIR D'INDICE 1 NE PUBLIE PAS SON CAP, exprès.
         *
         * C'est le chemin de compatibilité — un client d'avant le champ `yaw` —
         * et un chemin qui ne serait emprunté que par un test finit par ne plus
         * être emprunté du tout. Sur la capture, il doit marcher droit devant
         * lui comme les autres, sans qu'on puisse dire lequel c'est.
         */
        if (i == 1u) {
            p->has_yaw = false;
            p->yaw = 0.0f;
        } else {
            p->has_yaw = true;
            /* Vers +Z, c'est-à-dire un quart de tour ; vers -Z, l'opposé. */
            p->yaw = going_north ? (NS_PI * 0.5f) : (-NS_PI * 0.5f);
        }
    }
    return count;
}
