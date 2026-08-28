/*
 * stepgen — les pas et les nappes d'ambiance, SYNTHÉTISÉS plutôt qu'enregistrés.
 *
 * Le problème, tel qu'on l'entend
 * ------------------------------
 * `legacy/room/sounds/walk.wav` est le seul enregistrement de pas de 2020. Un
 * seul. `room_sound.c` le rejouait pour les cinq matériaux de sol en changeant
 * sa hauteur et son gain — ce qui distingue une moquette d'un carrelage à peu
 * près comme accélérer un disque distingue deux instruments. Surtout : c'est la
 * MÊME forme d'onde toutes les 0,775 s. L'oreille repère une répétition exacte
 * bien avant de repérer un mauvais timbre, et c'est ce qui fait « jeu vidéo ».
 *
 * Pourquoi un générateur, et pas des enregistrements
 * -------------------------------------------------
 * Même raisonnement que `sideart` et `panelart`, qui DESSINENT leurs planches :
 * une banque téléchargée pose une question de licence, une question de poids
 * dans le dépôt, et casse la reconstructibilité hors ligne. Un pas, lui, se
 * décrit en trois lignes de physique — un choc excite le sol, le sol résonne,
 * la semelle frotte — et se synthétise donc entièrement. Ce fichier est cette
 * description, et les vingt fichiers qu'il produit se régénèrent à l'identique
 * sur les trois plateformes.
 *
 * Le modèle, en une phrase par composante
 * ---------------------------------------
 *   CHOC     une salve de bruit de 2 ms : l'énergie que le pied dépose.
 *   TALON    un résonateur AIGU excité par le choc, court. C'est le « clac »
 *            d'un talon sur du dur ; la moquette n'en a pas.
 *   CORPS    un résonateur GRAVE excité par le même choc : la masse du marcheur
 *            reçue par le sol. C'est lui qui porte le matériau.
 *   FROTTE   du bruit filtré, décalé de quelques millisecondes : la semelle qui
 *            glisse APRÈS le contact. Long et doux sur la moquette, bref et
 *            clair sur le carrelage.
 *   CAISSE   un grave très bas, réservé à l'estrade : une plateforme creuse est
 *            un tambour, et c'est ce qui la fait entendre comme telle.
 *
 * Quatre variantes par matériau, et pourquoi quatre
 * ------------------------------------------------
 * Chaque variante tire un bruit différent ET décale les fréquences, les
 * décroissances et les gains dans une plage étroite. Ce ne sont donc pas quatre
 * transpositions du même fichier : ce sont quatre pas. Quatre suffisent parce
 * que `room_sound.c` interdit de rejouer la variante précédente et ajoute son
 * propre écart de hauteur et de gain — le motif audible commence à 2, pas à 4.
 *
 * La normalisation est GLOBALE, et c'est important
 * -----------------------------------------------
 * Normaliser chaque fichier à son propre pic mettrait la moquette et le
 * carrelage au même niveau, c'est-à-dire annulerait exactement ce qu'on cherche
 * à produire. Les vingt pas sont donc mis à l'échelle par un seul facteur, celui
 * du plus fort d'entre eux : les rapports de niveau entre matériaux survivent au
 * fichier.
 */
#include "tools_common.h"

#include <math.h>

#define SG_RATE 48000

/* ==========================================================================
 * Bruit, filtres, enveloppes
 * ========================================================================== */

/* xorshift64* : reproductible, court, et sans dépendance. Le déterminisme n'est
 * pas un luxe ici — c'est ce qui fait qu'un build produit deux fois le même
 * asset, donc qu'une différence d'écoute est attribuable à un changement de
 * code et pas au hasard. */
typedef struct sg_rng { uint64_t s; } sg_rng;

static void sg_seed(sg_rng *r, uint64_t seed)
{
    r->s = seed ? seed : 0x9E3779B97F4A7C15ull;
}

static uint32_t sg_u32(sg_rng *r)
{
    uint64_t x = r->s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    r->s = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}

/* Bruit blanc dans [-1, 1]. */
static float sg_noise(sg_rng *r)
{
    return (float)((double)sg_u32(r) / 2147483648.0) - 1.0f;
}

/* Un tirage dans [lo, hi] : c'est ce qui écarte les variantes les unes des
 * autres sans qu'aucune ne sorte du matériau. */
static float sg_range(sg_rng *r, float lo, float hi)
{
    const float t = (float)(sg_u32(r) >> 8) / (float)(1u << 24);
    return lo + (hi - lo) * t;
}

/*
 * Filtre à variable d'état, sortie passe-bande.
 *
 * Choisi plutôt qu'un biquad parce que sa fréquence et sa résonance se règlent
 * séparément et se lisent directement — on écrit « 240 Hz, Q 3,5 » et c'est ce
 * qu'on obtient. Excité par une salve de bruit, il produit exactement ce qu'on
 * cherche : une résonance qui monte d'un coup et retombe.
 */
typedef struct sg_svf { float lo, band; float f, q; } sg_svf;

static void sg_svf_set(sg_svf *s, float hz, float q)
{
    s->lo = s->band = 0.0f;
    /* Approximation classique, valable tant que hz << rate/4 ; la plus haute
     * résonance employée ici est à 4 kHz pour 48 kHz d'échantillonnage. */
    s->f = 2.0f * sinf(3.14159265358979f * hz / (float)SG_RATE);
    s->q = 1.0f / ((q > 0.5f) ? q : 0.5f);
}

static float sg_svf_band(sg_svf *s, float in)
{
    const float high = in - s->lo - s->q * s->band;
    s->band += s->f * high;
    s->lo   += s->f * s->band;
    return s->band;
}

/* Passe-bas et passe-haut à un pôle : de quoi tailler une bande de bruit sans
 * sortir l'artillerie. Deux pôles empilés pour la pente là où elle compte. */
typedef struct sg_pole { float z; float a; } sg_pole;

static void sg_pole_set(sg_pole *p, float hz)
{
    p->z = 0.0f;
    const float x = expf(-2.0f * 3.14159265358979f * hz / (float)SG_RATE);
    p->a = (x < 0.0f) ? 0.0f : ((x > 0.9999f) ? 0.9999f : x);
}

static float sg_lowpass(sg_pole *p, float in)
{
    p->z = in * (1.0f - p->a) + p->z * p->a;
    return p->z;
}

static float sg_highpass(sg_pole *p, float in)
{
    p->z = in * (1.0f - p->a) + p->z * p->a;
    return in - p->z;
}

/* Décroissance exponentielle : `tau` est le temps pour tomber à 37 %. */
static float sg_decay(float t, float tau)
{
    return (tau > 1e-6f) ? expf(-t / tau) : 0.0f;
}

/* Attaque en cosinus surélevé. Un début abrupt fait un clic numérique qui
 * s'entend et qui n'est pas celui qu'on veut : celui d'un talon est PRODUIT par
 * la résonance aiguë, pas par une discontinuité d'échantillon. */
static float sg_attack(float t, float len)
{
    if (t >= len) return 1.0f;
    if (t <= 0.0f) return 0.0f;
    return 0.5f - 0.5f * cosf(3.14159265358979f * t / len);
}

/* ==========================================================================
 * Écriture WAV
 * ==========================================================================
 * PCM 16 bits mono, en-tête écrit à la main. Le mono n'est pas une économie :
 * `ns_audio` décode TOUT en mono parce qu'une source stéréo ne se spatialise
 * pas. Produire du stéréo ici ne ferait que doubler le fichier avant de le
 * réduire au chargement.
 */
static void sg_put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);         p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF); p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void sg_write_wav(const char *path, const float *mono, size_t frames, float scale)
{
    unsigned char h[44];
    const uint32_t bytes = (uint32_t)(frames * 2u);
    memcpy(h, "RIFF", 4);          sg_put_u32(h + 4, 36u + bytes);
    memcpy(h + 8, "WAVEfmt ", 8);  sg_put_u32(h + 16, 16u);
    h[20] = 1; h[21] = 0;                   /* PCM */
    h[22] = 1; h[23] = 0;                   /* mono */
    sg_put_u32(h + 24, SG_RATE);
    sg_put_u32(h + 28, SG_RATE * 2u);
    h[32] = 2; h[33] = 0;
    h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);     sg_put_u32(h + 40, bytes);

    FILE *f = fopen(path, "wb");
    if (!f) tool_fatalf("impossible d'écrire %s", path);
    fwrite(h, 1, sizeof h, f);

    for (size_t i = 0; i < frames; ++i) {
        float v = mono[i] * scale;
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        const short s = (short)(v * 32767.0f);
        const unsigned char le[2] = { (unsigned char)((unsigned)s & 0xFF),
                                      (unsigned char)(((unsigned)s >> 8) & 0xFF) };
        fwrite(le, 1, 2, f);
    }
    fclose(f);
}

/* ==========================================================================
 * Les cinq matériaux
 * ==========================================================================
 * Ce tableau EST la description du décor à l'oreille, et c'est le seul endroit
 * où elle vit. Les valeurs viennent de ce qui distingue physiquement ces sols :
 * une fibre absorbe l'aigu et allonge le frottement, un carreau posé sur chape
 * rend un choc bref et clair, une lame de bois résonne dans les médiums graves,
 * une estrade est une caisse.
 */
typedef struct sg_material {
    const char *name;

    float length;        /* durée du fichier, secondes */

    float heel_gain;     /* le « clac » — nul sur la moquette */
    float heel_hz, heel_q, heel_tau;

    float body_gain;     /* la masse reçue par le sol */
    float body_hz, body_q, body_tau;

    float scuff_gain;    /* la semelle, APRÈS le contact */
    float scuff_hp, scuff_lp, scuff_tau, scuff_delay;

    float drum_gain;     /* la caisse d'une estrade */
    float drum_hz, drum_tau;
} sg_material;

static const sg_material g_materials[] = {
    /*
     * MOQUETTE — aucun talon, un corps sourd et bas, et un frottement de fibre
     * qui dure trois fois plus longtemps que le choc. C'est le sol le plus
     * discret de la salle, et il doit le rester : l'allée en est couverte, donc
     * c'est ce pas-là qu'on entend le plus souvent de toute la partie.
     */
    { "moquette",  0.26f,
      0.00f, 1000.0f, 1.0f, 0.004f,
      0.42f,  112.0f, 1.3f, 0.042f,
      0.34f,  190.0f, 1700.0f, 0.095f, 0.008f,
      0.00f,   60.0f, 0.10f },

    /*
     * CARRELAGE — le talon domine et le corps est bref : une chape ne garde pas
     * l'énergie. Le reste de la queue ne vient pas du fichier mais de la ZONE :
     * les toilettes déclarent wet 0,42 / decay 0,52 dans `salle.room.json`, et
     * c'est le mixeur qui la fabrique. Mettre la réverbération dans
     * l'échantillon la ferait sonner pareil partout, y compris dans l'allée.
     */
    { "carrelage", 0.30f,
      0.58f, 3750.0f, 6.5f, 0.0090f,
      0.40f,  245.0f, 3.6f, 0.048f,
      0.30f,  850.0f, 9000.0f, 0.042f, 0.006f,
      0.00f,   60.0f, 0.10f },

    /*
     * BOIS — le matériau le plus « accordé » des cinq : une lame répond dans les
     * médiums graves avec un Q élevé, et c'est ce qui la fait reconnaître.
     */
    { "bois",      0.34f,
      0.30f, 2350.0f, 5.0f, 0.0120f,
      0.54f,  178.0f, 4.6f, 0.105f,
      0.23f,  480.0f, 5200.0f, 0.050f, 0.007f,
      0.14f,   94.0f, 0.13f },

    /*
     * BÉTON — large et mat. Un Q bas partout : le béton ne rend rien, il encaisse.
     * C'est le seul des cinq dont le spectre est à peu près plat.
     */
    { "beton",     0.24f,
      0.34f, 2950.0f, 3.0f, 0.0075f,
      0.48f,  152.0f, 2.1f, 0.038f,
      0.28f,  600.0f, 6200.0f, 0.034f, 0.005f,
      0.00f,   60.0f, 0.10f },

    /*
     * ESTRADE — du bois, plus la CAISSE. Les 62 Hz sous le pas sont ce qui fait
     * qu'on entend qu'on vient de monter sur une plateforme creuse, alors même
     * que c'est le même matériau de surface. C'est aussi le pas le plus long des
     * cinq : une caisse met du temps à se taire.
     */
    { "estrade",   0.44f,
      0.30f, 2000.0f, 4.2f, 0.0135f,
      0.60f,  148.0f, 5.2f, 0.150f,
      0.22f,  430.0f, 4200.0f, 0.058f, 0.007f,
      0.72f,   62.0f, 0.210f },
};

#define SG_MATERIAL_COUNT ((int)(sizeof g_materials / sizeof g_materials[0]))
#define SG_VARIANTS 4

/*
 * Un pas, rendu dans `out`. Renvoie le nombre de trames écrites.
 *
 * `variant` déplace les fréquences de ±10 %, les décroissances de −15/+18 % et
 * les gains de ±12 %, ET change le bruit d'excitation. Les deux comptent : sans
 * le bruit, quatre variantes seraient quatre transpositions ; sans le décalage,
 * ce seraient quatre réalisations du même filtre, ce qui s'entend comme le même
 * pas légèrement bruité.
 */
static size_t sg_render_step(const sg_material *m, int variant, float *out, size_t cap)
{
    sg_rng r;
    /* La graine mêle le matériau et la variante : deux matériaux ne partagent
     * jamais leur bruit, et le fichier est reproductible d'un build à l'autre. */
    sg_seed(&r, 0x51EDull * (uint64_t)(variant + 1) + 0x9E37ull * (uint64_t)(m->body_hz * 7.0f));

    const float jf = sg_range(&r, 0.90f, 1.10f);   /* fréquences */
    const float jt = sg_range(&r, 0.85f, 1.18f);   /* décroissances */
    const float jg = sg_range(&r, 0.88f, 1.12f);   /* gains */

    const size_t frames = (size_t)(m->length * (float)SG_RATE);
    if (frames > cap) tool_fatalf("tampon trop petit pour « %s »", m->name);

    sg_svf heel, body, drum;
    sg_svf_set(&heel, m->heel_hz * jf, m->heel_q);
    sg_svf_set(&body, m->body_hz * jf, m->body_q);
    sg_svf_set(&drum, m->drum_hz * jf, 8.0f);

    sg_pole scuff_lp1, scuff_lp2, scuff_hp;
    sg_pole_set(&scuff_lp1, m->scuff_lp * jf);
    sg_pole_set(&scuff_lp2, m->scuff_lp * jf);
    sg_pole_set(&scuff_hp,  m->scuff_hp * jf);

    /* Le choc : 2 ms de bruit, c'est tout. Toute la couleur vient des
     * résonateurs qu'il excite — ce qui est aussi ce qui se passe dans la
     * réalité, où le pied n'a pas de timbre et le sol en a un. */
    const float impact_tau = 0.0020f;

    for (size_t i = 0; i < frames; ++i) {
        const float t = (float)i / (float)SG_RATE;

        const float impact = sg_noise(&r) * sg_decay(t, impact_tau);

        float v = 0.0f;
        v += sg_svf_band(&heel, impact) * m->heel_gain * jg
           * sg_decay(t, m->heel_tau * jt) * sg_attack(t, 0.0004f);
        v += sg_svf_band(&body, impact) * m->body_gain * jg
           * sg_decay(t, m->body_tau * jt) * sg_attack(t, 0.0015f);
        if (m->drum_gain > 0.0f) {
            v += sg_svf_band(&drum, impact) * m->drum_gain * jg
               * sg_decay(t, m->drum_tau * jt) * sg_attack(t, 0.0030f);
        }

        /* Le frottement commence APRÈS le contact : c'est la semelle qui glisse
         * une fois le poids posé. Quelques millisecondes suffisent — c'est le
         * décalage qui fait entendre deux gestes au lieu d'un. */
        const float ts = t - m->scuff_delay;
        if (ts > 0.0f) {
            float n = sg_highpass(&scuff_hp, sg_noise(&r));
            n = sg_lowpass(&scuff_lp2, sg_lowpass(&scuff_lp1, n));
            v += n * m->scuff_gain * jg * sg_decay(ts, m->scuff_tau * jt)
               * sg_attack(ts, 0.0035f);
        }

        out[i] = v;
    }

    /* Fondu de sortie : un fichier qui s'arrête sur une valeur non nulle fait un
     * clic à la fin de CHAQUE pas, soit une fois par 0,775 m parcouru. */
    const size_t fade = (size_t)(0.012f * (float)SG_RATE);
    for (size_t i = 0; i < fade && i < frames; ++i) {
        const float w = (float)i / (float)fade;
        out[frames - 1 - i] *= w;
    }
    return frames;
}

/* ==========================================================================
 * Les nappes d'ambiance
 * ==========================================================================
 * Trois boucles, et une contrainte que rien d'autre dans ce fichier n'a : elles
 * doivent BOUCLER SANS COUTURE. Une nappe qui claque toutes les quatre secondes
 * est pire que pas de nappe du tout — on l'entend et on ne l'oublie plus.
 *
 * Deux moyens, employés ensemble :
 *   - toutes les sinusoïdes ont un nombre ENTIER de cycles dans la boucle. C'est
 *     vérifié à l'exécution plutôt que promis en commentaire ;
 *   - le bruit, lui, ne peut pas boucler : on en engendre un peu plus que
 *     nécessaire et on fond la fin dans le début à puissance constante.
 */

/* Vérifie qu'une fréquence tombe juste dans la boucle. Un outil de build qui se
 * trompe doit casser le build, pas livrer une nappe qui claque. */
static void sg_check_period(float hz, float loop_seconds, const char *what)
{
    const float cycles = hz * loop_seconds;
    if (fabsf(cycles - roundf(cycles)) > 1e-3f) {
        tool_fatalf("« %s » à %.3f Hz ne boucle pas en %.2f s (%.4f cycles)",
                    what, (double)hz, (double)loop_seconds, (double)cycles);
    }
}

/*
 * Fond la fin de `buf` dans son début, à puissance constante.
 *
 * `buf` contient `frames + fade` trames ; en sortie les `frames` premières
 * bouclent. La trame 0 devient le mélange de l'ancienne trame 0 et de la trame
 * `frames`, qui est justement celle qui SUIVAIT la dernière — le raccord tombe
 * donc sur du bruit continu et non sur une soudure.
 */
static void sg_seamless(float *buf, size_t frames, size_t fade)
{
    for (size_t i = 0; i < fade; ++i) {
        const float w = 3.14159265358979f * 0.5f * (float)i / (float)fade;
        buf[i] = buf[i] * sinf(w) + buf[frames + i] * cosf(w);
    }
}

/*
 * Le fond de salle : néons et tubes cathodiques.
 *
 * C'est ce qui manquait le plus. Une salle d'arcade n'est jamais silencieuse —
 * dix-neuf tubes et onze luminaires produisent un plancher continu, et son
 * ABSENCE s'entend : sans lui, entre deux pas, la salle est un vide numérique.
 *
 * Trois couches, et chacune répond à une observation :
 *   - le 100 Hz des ballasts domine, pas le 50 : un ballast redresse, donc
 *     ronfle à deux fois le secteur. C'est l'erreur qu'on fait quand on met du
 *     50 Hz « parce que c'est le secteur » ;
 *   - le sifflement de balayage ligne d'un tube PAL est à 15 625 Hz. Trois
 *     tubes légèrement désaccordés plutôt qu'un seul : dix-neuf téléviseurs ne
 *     sifflent pas à l'unisson, et un battement lent est ce qui le dit ;
 *   - un plancher de bruit, passé en passe-bas : l'air, la ventilation lointaine
 *     et tout ce qu'on n'identifie pas.
 */
static size_t sg_render_room_tone(float *out, size_t cap)
{
    const float loop = 4.0f;
    const size_t frames = (size_t)(loop * (float)SG_RATE);
    const size_t fade   = (size_t)(0.40f * (float)SG_RATE);
    if (frames + fade > cap) tool_fatalf("tampon trop petit pour le fond de salle");

    const float hum[5]  = { 50.0f, 100.0f, 150.0f, 200.0f, 300.0f };
    const float hum_g[5] = { 0.10f, 0.30f, 0.13f, 0.07f, 0.035f };
    const float crt[3]  = { 15625.0f, 15600.0f, 15650.0f };
    const float wander  = 0.25f;

    for (int i = 0; i < 5; ++i) sg_check_period(hum[i], loop, "ronflement");
    for (int i = 0; i < 3; ++i) sg_check_period(crt[i], loop, "balayage ligne");
    sg_check_period(wander, loop, "respiration");

    sg_rng r; sg_seed(&r, 0xB0DEull);
    sg_pole lp1, lp2, hp;
    sg_pole_set(&lp1, 1800.0f);
    sg_pole_set(&lp2, 1800.0f);
    sg_pole_set(&hp,    45.0f);

    for (size_t i = 0; i < frames + fade; ++i) {
        const float t = (float)i / (float)SG_RATE;
        const float w = 6.28318530717959f * t;

        float v = 0.0f;
        for (int k = 0; k < 5; ++k) v += hum_g[k] * sinf(w * hum[k]);
        /* Le sifflement est délibérément à la limite de l'audible : au-dessus il
         * devient un acouphène, en-dessous il ne dit plus « tube cathodique ».
         * Ceux qui ne l'entendent pas ne perdent rien — ceux qui l'entendent
         * reconnaissent la pièce. */
        for (int k = 0; k < 3; ++k) v += 0.0075f * sinf(w * crt[k]);

        float n = sg_highpass(&hp, sg_noise(&r));
        n = sg_lowpass(&lp2, sg_lowpass(&lp1, n));
        v += n * 0.55f;

        /* Une respiration lente : un fond RIGOUREUSEMENT constant se lit comme
         * un défaut de fichier, pas comme une pièce. */
        out[i] = v * (0.88f + 0.12f * sinf(w * wander));
    }

    sg_seamless(out, frames, fade);
    return frames;
}

/*
 * L'extracteur des toilettes.
 *
 * Placé là et pas ailleurs pour une raison : c'est la seule source d'ambiance de
 * la salle qui soit derrière une porte. Elle sert donc AUSSI à rendre l'occlusion
 * du BVH audible — on l'entend s'ouvrir en approchant, se fermer en s'éloignant.
 * Une source de démonstration au milieu de l'allée ne prouverait rien.
 *
 * Un ventilateur, c'est un moteur (ronflement à deux fois le secteur, comme les
 * ballasts) et de l'air haché par les pales. La fréquence de passage de pale est
 * ce qui distingue un ventilateur d'un simple souffle : six pales à quatre tours
 * par seconde font 24 Hz, et c'est cette modulation qu'on reconnaît.
 */
static size_t sg_render_fan(float *out, size_t cap)
{
    const float loop = 4.0f;
    const size_t frames = (size_t)(loop * (float)SG_RATE);
    const size_t fade   = (size_t)(0.35f * (float)SG_RATE);
    if (frames + fade > cap) tool_fatalf("tampon trop petit pour le ventilateur");

    const float blade = 24.0f, blade2 = 48.0f, motor = 100.0f, motor2 = 200.0f;
    sg_check_period(blade,  loop, "passage de pale");
    sg_check_period(blade2, loop, "harmonique de pale");
    sg_check_period(motor,  loop, "moteur");
    sg_check_period(motor2, loop, "harmonique moteur");

    sg_rng r; sg_seed(&r, 0x4A17ull);
    sg_pole lp1, lp2, hp1, hp2;
    sg_pole_set(&lp1, 3600.0f);
    sg_pole_set(&lp2, 3600.0f);
    sg_pole_set(&hp1,  320.0f);
    sg_pole_set(&hp2,  320.0f);

    for (size_t i = 0; i < frames + fade; ++i) {
        const float t = (float)i / (float)SG_RATE;
        const float w = 6.28318530717959f * t;

        float air = sg_highpass(&hp2, sg_highpass(&hp1, sg_noise(&r)));
        air = sg_lowpass(&lp2, sg_lowpass(&lp1, air));

        /* La modulation ne descend pas à zéro : un ventilateur hache l'air, il
         * ne le coupe pas. À 100 % de profondeur on entend un hélicoptère. */
        const float chop = 0.78f + 0.18f * sinf(w * blade) + 0.06f * sinf(w * blade2);

        out[i] = air * 0.72f * chop
               + 0.085f * sinf(w * motor)
               + 0.035f * sinf(w * motor2);
    }

    sg_seamless(out, frames, fade);
    return frames;
}

/*
 * La rue, entendue depuis le sas.
 *
 * Volontairement PAUVRE : pas de klaxon, pas de voix, pas d'oiseau. Ce qui
 * traverse une porte vitrée, c'est le grave — tout le reste est arrêté. Une
 * rumeur riche en médiums sonnerait comme si la porte était ouverte, et la
 * salle perdrait son dedans.
 *
 * Les gonflements — un véhicule qui passe — sont des bosses en cosinus placées
 * à l'intérieur de la boucle, jamais à cheval sur le raccord : le fondu du bruit
 * s'occupe de la couture, il n'a pas à s'occuper aussi de l'enveloppe.
 */
static size_t sg_render_street(float *out, size_t cap)
{
    const float loop = 6.0f;
    const size_t frames = (size_t)(loop * (float)SG_RATE);
    const size_t fade   = (size_t)(0.50f * (float)SG_RATE);
    if (frames + fade > cap) tool_fatalf("tampon trop petit pour la rue");

    sg_rng r; sg_seed(&r, 0x5C1Eull);
    sg_pole rumble1, rumble2, rumble3, hiss_lp, hiss_hp, dc;
    sg_pole_set(&rumble1, 170.0f);
    sg_pole_set(&rumble2, 170.0f);
    sg_pole_set(&rumble3, 170.0f);
    sg_pole_set(&hiss_lp, 1400.0f);
    sg_pole_set(&hiss_hp,  380.0f);
    sg_pole_set(&dc,        22.0f);

    /* Trois passages, à des instants et des durées différents : un rythme
     * régulier se remarquerait au deuxième tour de boucle. */
    const float pass_at[3]  = { 1.15f, 3.05f, 4.70f };
    const float pass_len[3] = { 1.30f, 0.85f, 1.60f };
    const float pass_amp[3] = { 0.55f, 0.34f, 0.70f };

    for (size_t i = 0; i < frames + fade; ++i) {
        const float t = (float)i / (float)SG_RATE;

        float n = sg_noise(&r);
        float low = sg_lowpass(&rumble3, sg_lowpass(&rumble2, sg_lowpass(&rumble1, n)));
        low = sg_highpass(&dc, low);          /* pas de continu : ça pompe le mixage */

        float hiss = sg_lowpass(&hiss_lp, sg_highpass(&hiss_hp, n));

        float swell = 0.0f;
        for (int k = 0; k < 3; ++k) {
            const float d = t - pass_at[k];
            if (d > -pass_len[k] && d < pass_len[k]) {
                const float u = d / pass_len[k];                 /* −1 .. 1 */
                swell += pass_amp[k] * 0.5f * (1.0f + cosf(3.14159265358979f * u));
            }
        }

        out[i] = low * (1.35f + 2.6f * swell) + hiss * (0.10f + 0.22f * swell);
    }

    sg_seamless(out, frames, fade);
    return frames;
}

/* ==========================================================================
 * Programme
 * ========================================================================== */

static float peak_of(const float *v, size_t n)
{
    float p = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float a = fabsf(v[i]);
        if (a > p) p = a;
    }
    return p;
}

/* Valeur efficace, en décibels relatifs à la pleine échelle. C'est le chiffre
 * qui dit si deux matériaux se distinguent vraiment — le pic, lui, ne dit rien
 * d'un pas, qui est un transitoire. */
static double rms_db(const float *v, size_t n, float scale)
{
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double s = (double)v[i] * (double)scale;
        sum += s * s;
    }
    const double rms = (n > 0) ? sqrt(sum / (double)n) : 0.0;
    return (rms > 1e-9) ? 20.0 * log10(rms) : -180.0;
}

#define SG_MAX_FRAMES ((size_t)(SG_RATE * 8))

int main(int argc, char **argv)
{
    const char *out_dir = NULL;
    float peak_target = 0.90f;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--peak=", 7) == 0) peak_target = (float)atof(argv[i] + 7);
        else if (!out_dir) out_dir = argv[i];
    }
    if (!out_dir) {
        fprintf(stderr,
            "stepgen — synthétise la banque de pas et les nappes d'ambiance\n"
            "usage : %s [--peak=F] <repertoire-de-sortie>\n"
            "  --peak=F   pic visé après normalisation GLOBALE (défaut 0.90)\n"
            "\n"
            "Produit %d x %d pas (`pas_<materiau>_<n>.wav`) et trois boucles\n"
            "d'ambiance (`amb_neon.wav`, `amb_ventilo.wav`, `amb_rue.wav`).\n",
            argv[0], SG_MATERIAL_COUNT, SG_VARIANTS);
        return 2;
    }
    if (peak_target <= 0.05f || peak_target > 1.0f) {
        tool_fatalf("pic visé hors de (0,05 ; 1,0] : %.3f", (double)peak_target);
    }

    /* ---- les pas : tout rendre AVANT d'écrire quoi que ce soit -------------
     * La normalisation est globale, donc on ne peut pas écrire le premier
     * fichier tant qu'on n'a pas rendu le dernier. C'est le prix de garder les
     * rapports de niveau entre matériaux, et il est modeste : vingt pas font
     * moins de six secondes de son au total. */
    static float steps[SG_MATERIAL_COUNT * SG_VARIANTS][SG_MAX_FRAMES];
    size_t step_frames[SG_MATERIAL_COUNT * SG_VARIANTS];
    float  global_peak = 0.0f;

    for (int m = 0; m < SG_MATERIAL_COUNT; ++m) {
        for (int v = 0; v < SG_VARIANTS; ++v) {
            const int k = m * SG_VARIANTS + v;
            step_frames[k] = sg_render_step(&g_materials[m], v, steps[k], SG_MAX_FRAMES);
            const float p = peak_of(steps[k], step_frames[k]);
            if (p > global_peak) global_peak = p;
        }
    }
    if (global_peak < 1e-6f) tool_fatalf("la banque de pas est silencieuse");

    const float step_scale = peak_target / global_peak;

    char path[1024];
    for (int m = 0; m < SG_MATERIAL_COUNT; ++m) {
        double loudest = -180.0, quietest = 0.0;
        for (int v = 0; v < SG_VARIANTS; ++v) {
            const int k = m * SG_VARIANTS + v;
            snprintf(path, sizeof path, "%s/pas_%s_%d.wav",
                     out_dir, g_materials[m].name, v + 1);
            sg_write_wav(path, steps[k], step_frames[k], step_scale);

            const double db = rms_db(steps[k], step_frames[k], step_scale);
            if (db > loudest)  loudest  = db;
            if (db < quietest) quietest = db;
        }
        tool_infof("pas « %-9s » : %d variantes, %.0f ms, efficace %.1f à %.1f dBFS",
                   g_materials[m].name, SG_VARIANTS,
                   (double)(g_materials[m].length * 1000.0f), quietest, loudest);
    }

    /* ---- les nappes : normalisées chacune pour soi -------------------------
     * Contrairement aux pas, elles ne se comparent pas entre elles : leurs
     * niveaux relatifs sont décidés par `room_sound.c`, qui sait où chacune est
     * placée et à quelle distance on l'entend. Les mettre à l'échelle ensemble
     * ne fixerait rien et ne ferait que gaspiller de la dynamique. */
    static float bed[SG_MAX_FRAMES];
    const struct {
        const char *file;
        size_t (*render)(float *, size_t);
        float peak;
    } beds[] = {
        { "amb_neon.wav",    sg_render_room_tone, 0.55f },
        { "amb_ventilo.wav", sg_render_fan,       0.70f },
        { "amb_rue.wav",     sg_render_street,    0.75f },
    };

    for (size_t i = 0; i < sizeof beds / sizeof beds[0]; ++i) {
        const size_t frames = beds[i].render(bed, SG_MAX_FRAMES);
        const float p = peak_of(bed, frames);
        if (p < 1e-6f) tool_fatalf("« %s » est silencieuse", beds[i].file);
        const float scale = beds[i].peak / p;

        snprintf(path, sizeof path, "%s/%s", out_dir, beds[i].file);
        sg_write_wav(path, bed, frames, scale);

        /* La couture est mesurée, pas promise : on compare l'écart entre la
         * dernière et la première trame à l'écart typique entre deux trames
         * voisines. S'il est du même ordre, la boucle ne claque pas. */
        double neighbour = 0.0;
        for (size_t k = 1; k < frames; ++k) neighbour += fabs((double)(bed[k] - bed[k - 1]));
        neighbour /= (double)(frames - 1);
        const double seam = fabs((double)(bed[0] - bed[frames - 1]));

        tool_infof("nappe « %-15s » : %.2f s, couture %.2e contre %.2e par trame (x%.1f)",
                   beds[i].file, (double)frames / (double)SG_RATE,
                   seam, neighbour, (neighbour > 0.0) ? seam / neighbour : 0.0);
        if (neighbour > 0.0 && seam > neighbour * 12.0) {
            tool_fatalf("« %s » claque au raccord", beds[i].file);
        }
    }

    tool_infof("banque écrite dans %s (pic global des pas ramené à %.2f)",
               out_dir, (double)peak_target);
    return 0;
}
