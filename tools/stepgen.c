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

/*
 * Change la fréquence SANS toucher à l'état.
 *
 * `sg_svf_set` remet `lo` et `band` à zéro : c'est ce qu'on veut pour armer un
 * résonateur avant de l'exciter, et c'est exactement ce qu'il ne faut pas faire
 * quand la fréquence glisse d'une trame à l'autre — on réinitialiserait le
 * filtre 48 000 fois par seconde, et il ne resterait qu'un souffle plat. Le
 * remplissage d'un réservoir a besoin de ce glissement : c'est LUI qu'on
 * reconnaît.
 */
static void sg_svf_tune(sg_svf *s, float hz)
{
    s->f = 2.0f * sinf(3.14159265358979f * hz / (float)SG_RATE);
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
 * La chasse d'eau
 * ==========================================================================
 * Ce n'est PAS une nappe : elle ne boucle pas, elle a un début et une fin, et
 * `room_sound.c` la déclenche de loin en loin depuis les cabines. Elle est donc
 * rendue à part des trois ambiances, avec son propre tampon — dix secondes ne
 * tiennent pas dans `SG_MAX_FRAMES`, qui est dimensionné pour des pas.
 *
 * Trois choses qui se suivent, et la troisième est celle qui compte
 * -----------------------------------------------------------------
 *   LA CHASSE      la vanne s'ouvre d'un coup, le réservoir se vide dans la
 *                  cuvette. Bruit large, qui monte en deux dixièmes et décroît.
 *                  C'est le plus fort, et c'est aussi le moins caractéristique :
 *                  pris seul, il sonne comme n'importe quelle fuite d'eau.
 *   L'ÉCOULEMENT   l'eau tourne dans la cuvette et s'engage dans le siphon.
 *                  Plus grave, modulé, avec quelques glouglous — des résonances
 *                  basses excitées brièvement, une bulle étant une cavité qui
 *                  sonne puis se referme.
 *   LE REMPLISSAGE le réservoir se remplit et la colonne d'air AU-DESSUS de
 *                  l'eau raccourcit. Sa résonance monte donc pendant toute la
 *                  durée du remplissage — de 300 à 1 400 Hz ici. C'est ce
 *                  glissement, et lui seul, qui fait qu'on reconnaît une chasse
 *                  d'eau les yeux fermés : un bruit filtré fixe donnerait un
 *                  robinet ouvert, pas un réservoir qui se remplit.
 *
 * La fin n'est pas un fondu : le flotteur ferme la vanne, ce qui coupe le jet en
 * une fraction de seconde et laisse un petit coup dans la tuyauterie. Un fondu
 * de sortie sonnerait comme quelqu'un qui baisse le volume.
 */
/* En DIXIÈMES de seconde, et non en flottant : le tampon est un tableau
 * statique, donc sa taille doit être une constante entière — un produit par un
 * `float` en ferait un tableau de longueur variable replié, ce que le projet
 * compile en avertissement. */
#define SG_FLUSH_TENTHS  104
#define SG_FLUSH_FRAMES  ((size_t)SG_RATE * SG_FLUSH_TENTHS / 10u)

static size_t sg_render_flush(float *out, size_t cap)
{
    const size_t frames = SG_FLUSH_FRAMES;
    if (frames > cap) tool_fatalf("tampon trop petit pour la chasse d'eau");

    /* Les trois phases, en secondes. Elles SE CHEVAUCHENT, et c'est le point :
     * le remplissage commence pendant que la cuvette s'écoule encore, parce que
     * la vanne d'arrivée s'ouvre dès que le flotteur descend. Trois phases
     * jointes bout à bout s'entendraient comme trois sons différents. */
    const float rush_tau   = 1.25f;   /* décroissance de la vidange */
    const float swirl_at   = 0.42f;
    const float swirl_tau  = 2.30f;
    const float fill_at    = 2.25f;
    const float fill_end   = 9.85f;   /* le flotteur ferme ici */

    sg_rng r; sg_seed(&r, 0x0EA0ull);

    sg_pole rush_hp, rush_lp1, rush_lp2;
    sg_pole_set(&rush_hp,   190.0f);
    sg_pole_set(&rush_lp1, 5200.0f);
    sg_pole_set(&rush_lp2, 5200.0f);

    sg_pole jet_hp, jet_lp;
    sg_pole_set(&jet_hp,  950.0f);
    sg_pole_set(&jet_lp, 4600.0f);

    sg_pole dc;
    sg_pole_set(&dc, 24.0f);

    /* La cuvette : une résonance large et basse, c'est un volume d'eau qui
     * tourne, pas un tuyau. */
    sg_svf bowl;  sg_svf_set(&bowl, 560.0f, 1.30f);
    /* La colonne d'air du réservoir : étroite, puisqu'on doit ENTENDRE sa
     * hauteur monter. Trop large, le glissement se perd dans le souffle. */
    sg_svf column; sg_svf_set(&column, 300.0f, 5.60f);

    /* Les glouglous du siphon. Placés à la main : un intervalle régulier
     * s'entendrait comme un moteur. Chacun est une cavité qui sonne puis se
     * referme, donc un résonateur excité une fois. */
    const float glou_at[5] = { 1.55f, 2.10f, 2.72f, 3.45f, 4.60f };
    const float glou_hz[5] = { 118.0f, 96.0f, 143.0f, 88.0f, 126.0f };
    const float glou_amp[5]= { 0.55f, 0.42f, 0.38f, 0.30f, 0.20f };
    sg_svf glou[5];
    for (int k = 0; k < 5; ++k) sg_svf_set(&glou[k], glou_hz[k], 7.5f);

    for (size_t i = 0; i < frames; ++i) {
        const float t = (float)i / (float)SG_RATE;
        const float n = sg_noise(&r);

        /* ---- 1. la chasse ------------------------------------------------ */
        float rush = sg_lowpass(&rush_lp2, sg_lowpass(&rush_lp1, sg_highpass(&rush_hp, n)));
        const float rush_env = sg_attack(t, 0.16f) * sg_decay(t > 0.16f ? t - 0.16f : 0.0f, rush_tau);

        /* ---- 2. l'écoulement --------------------------------------------- */
        float swirl = sg_svf_band(&bowl, n);
        float swirl_env = 0.0f;
        if (t > swirl_at) {
            const float d = t - swirl_at;
            /* La modulation lente est le tournoiement. Elle ne descend pas à
             * zéro : l'eau ne s'arrête pas entre deux tours. */
            const float wobble = 0.80f + 0.20f * sinf(6.28318530717959f * 2.7f * d)
                                       + 0.08f * sinf(6.28318530717959f * 1.1f * d);
            swirl_env = sg_attack(d, 0.30f) * sg_decay(d, swirl_tau) * wobble;
        }

        float glous = 0.0f;
        for (int k = 0; k < 5; ++k) {
            const float d = t - glou_at[k];
            if (d < 0.0f || d > 0.55f) { (void)sg_svf_band(&glou[k], 0.0f); continue; }
            /* Excitée les vingt premières millisecondes, puis laissée sonner. */
            const float drive = (d < 0.020f) ? n : 0.0f;
            glous += glou_amp[k] * sg_svf_band(&glou[k], drive) * sg_decay(d, 0.13f);
        }

        /* ---- 3. le remplissage ------------------------------------------- */
        float fill = 0.0f;
        if (t > fill_at) {
            const float d = t - fill_at;
            const float span = fill_end - fill_at;
            float u = d / span;
            if (u > 1.0f) u = 1.0f;

            /*
             * La montée n'est pas linéaire : le niveau monte à débit constant,
             * mais la fréquence de la colonne d'air varie comme l'inverse de sa
             * hauteur. Elle traîne donc au début et s'envole à la fin, ce qui
             * est précisément le geste qu'on reconnaît.
             */
            const float hz = 300.0f + 1100.0f * (u * u * (0.35f + 0.65f * u));
            sg_svf_tune(&column, hz);

            const float jet = sg_lowpass(&jet_lp, sg_highpass(&jet_hp, n));
            const float body = sg_svf_band(&column, n);

            /* Le jet faiblit à mesure que la contre-pression monte ; la colonne,
             * elle, se renforce parce qu'elle résonne de mieux en mieux.
             *
             * Le jet est VOLONTAIREMENT discret. Réglé à 0,30 il couvrait la
             * colonne : mesuré, le remplissage perdait en brillance au lieu d'en
             * gagner (13 600 puis 7 800 passages par zéro et par seconde), parce
             * qu'un souffle large à 4,6 kHz écrase toujours une résonance à
             * 1 kHz. Le contrôle en fin de programme est ce qui l'a montré — et
             * c'est exactement ce qu'un réservoir donne à entendre : une note qui
             * monte, pas un sifflement. */
            float env = sg_attack(d, 0.40f);
            if (t > fill_end) {
                /* Le flotteur : 90 ms pour couper, pas un fondu. */
                const float c = (t - fill_end) / 0.090f;
                env *= (c >= 1.0f) ? 0.0f : (1.0f - c);
            }
            fill = env * (0.13f * jet * (1.0f - 0.45f * u) + 0.72f * body * (0.55f + 0.45f * u));
        }

        float s = 0.95f * rush * rush_env
                + 0.85f * swirl * swirl_env
                + glous
                + fill;

        /* Le coup de bélier : la vanne se ferme, la colonne d'eau s'arrête. */
        const float d = t - fill_end - 0.090f;
        if (d > 0.0f && d < 0.30f) {
            s += 0.22f * sinf(6.28318530717959f * 78.0f * d) * sg_decay(d, 0.045f);
        }

        out[i] = sg_highpass(&dc, s);
    }

    return frames;
}

/* ==========================================================================
 * LE COUP DE POING SUR UNE BORNE
 * ==========================================================================
 * Comme la chasse d'eau, ce n'est pas une nappe : un debut, une fin, et
 * `room_sound.c` le declenche a la demande. Il est donc rendu a part.
 *
 * Ce qu'on frappe, et pourquoi ca ne sonne pas comme un mur
 * ---------------------------------------------------------
 * Une borne d'arcade n'est pas un bloc : c'est un COFFRE de contreplaque de
 * dix-huit millimetres, ferme par des flancs peints et un panneau de commande
 * en tole laquee, et il est VIDE a l'interieur — un tube, une carte, un
 * monnayeur, et beaucoup d'air. Un coup dessus fait donc quatre choses a la
 * fois, et c'est leur superposition qui la rend reconnaissable :
 *
 *   LE POING     mat, et c'est ce qui le distingue d'un coup de marteau. La
 *                chair amortit : l'excitation est un bruit BASSE bande de trois
 *                millisecondes et demie, pas la salve de deux millisecondes
 *                large d'un pas. Un choc dur ici donnerait un impact de
 *                percussion, ce qu'un poing n'est pas.
 *   LA TOLE      le panneau repond sur DEUX modes, 420 et 780 Hz. Deux et non
 *                un : une plaque a un spectre, un seul resonateur donne une
 *                NOTE, et une borne qu'on frappe ne chante pas. Les Q sont
 *                moderes parce que la laque amortit — de la tole nue sonnerait
 *                deux fois plus longtemps, et ce serait une poubelle.
 *   LA CAISSE    82 Hz, et c'est elle qui porte le volume du meuble. C'est le
 *                meme raisonnement que les 62 Hz de l'estrade : une boite
 *                creuse est un tambour, et sans ce grave on entend frapper une
 *                planche pleine.
 *   LES TRIPES   quatre petits chocs aigus, irreguliers, dans les cent
 *                premieres millisecondes : la vitre dans sa feuillure, le
 *                monnayeur, les vis. C'est le detail qui fait entendre une
 *                MACHINE et non du mobilier, et il ne coute que quatre
 *                resonateurs excites une fois. Leurs instants sont poses a la
 *                main : un intervalle regulier s'entendrait comme un moteur,
 *                exactement comme pour les glouglous de la chasse.
 */
#define SG_COUP_LEN 0.55f

static size_t sg_render_coup(float *out, size_t cap)
{
    const size_t frames = (size_t)(SG_COUP_LEN * (float)SG_RATE);
    if (frames > cap) tool_fatalf("tampon trop petit pour le coup sur la borne");

    sg_rng r; sg_seed(&r, 0xC0DEull);

    /* Les deux modes de la tole. */
    sg_svf tole1, tole2;
    sg_svf_set(&tole1, 420.0f, 3.2f);
    sg_svf_set(&tole2, 780.0f, 4.0f);

    /* La caisse du meuble. */
    sg_svf caisse; sg_svf_set(&caisse, 82.0f, 6.5f);

    /* Le mat du poing : du bruit dont on a retire tout l'aigu. Deux poles en
     * cascade et non un : un seul laisse passer une brillance qui rend le coup
     * claquant, et un poing ne claque pas. */
    sg_pole poing1, poing2;
    sg_pole_set(&poing1, 340.0f);
    sg_pole_set(&poing2, 340.0f);

    /* Les tripes. */
    const float cliq_at[4]  = { 0.013f, 0.032f, 0.059f, 0.096f };
    const float cliq_hz[4]  = { 2600.0f, 3400.0f, 1900.0f, 2950.0f };
    const float cliq_amp[4] = { 0.26f, 0.17f, 0.20f, 0.11f };
    sg_svf cliq[4];
    for (int k = 0; k < 4; ++k) sg_svf_set(&cliq[k], cliq_hz[k], 9.0f);

    /* Le retrait du continu : la caisse descend bas, et un resonateur a 82 Hz
     * excite par du bruit laisse une composante lente qui deplace le zero du
     * fichier sans s'entendre. Elle mangerait de la dynamique a la
     * normalisation. */
    sg_pole dc; sg_pole_set(&dc, 22.0f);

    for (size_t i = 0; i < frames; ++i) {
        const float t = (float)i / (float)SG_RATE;
        const float n = sg_noise(&r);

        /* L'excitation : 3,5 ms. C'est la duree du contact d'un poing, et elle
         * est ce qui rend le choc MAT — a 2 ms comme un pas, le meme filtrage
         * donnerait un coup sec. */
        const float choc = n * sg_decay(t, 0.0035f);

        float v = 0.0f;

        /* La tole. Le second mode s'eteint plus vite que le premier : les modes
         * hauts d'une plaque sont toujours les plus amortis. */
        v += sg_svf_band(&tole1, choc) * 0.62f * sg_decay(t, 0.085f) * sg_attack(t, 0.0006f);
        v += sg_svf_band(&tole2, choc) * 0.34f * sg_decay(t, 0.052f) * sg_attack(t, 0.0004f);

        /* La caisse : elle met plus longtemps a s'etablir — un grand volume ne
         * repond pas dans la milliseconde — et beaucoup plus a se taire. */
        v += sg_svf_band(&caisse, choc) * 0.78f * sg_decay(t, 0.240f) * sg_attack(t, 0.0045f);

        /* Le mat du poing lui-meme. */
        {
            const float m = sg_lowpass(&poing2, sg_lowpass(&poing1, choc));
            v += m * 1.35f * sg_decay(t, 0.020f);
        }

        /* Les tripes. */
        for (int k = 0; k < 4; ++k) {
            const float d = t - cliq_at[k];
            if (d < 0.0f || d > 0.10f) { (void)sg_svf_band(&cliq[k], 0.0f); continue; }
            const float drive = (d < 0.0015f) ? n : 0.0f;
            v += cliq_amp[k] * sg_svf_band(&cliq[k], drive) * sg_decay(d, 0.008f);
        }

        out[i] = sg_highpass(&dc, v);
    }

    /* Fondu de sortie, meme raison que pour les pas : un fichier qui s'arrete
     * sur une valeur non nulle clique a chaque coup. */
    const size_t fade = (size_t)(0.015f * (float)SG_RATE);
    for (size_t i = 0; i < fade && i < frames; ++i) {
        out[frames - 1 - i] *= (float)i / (float)fade;
    }
    return frames;
}

/* ==========================================================================
 * LE JETON — trois bruits, et pourquoi ce n'est pas un seul
 * ==========================================================================
 * Un jeton fait trois choses distinctes dans cette salle, et les confondre
 * s'entend : on l'INSERE dans une borne, le monnayeur le RECRACHE quand il ne
 * passe pas, et il TOMBE DANS LE GODET quand on en prend au distributeur. Le
 * meme fichier joue trois fois ne raconterait qu'une chose, et il la
 * raconterait faux deux fois sur trois.
 *
 * Le modele : un disque de metal, pas une corde
 * ---------------------------------------------
 * Une corde vibre sur des harmoniques ENTIERES, et c'est ce qui lui donne une
 * note. Un disque, non : ses modes de flexion sont INHARMONIQUES — pour une
 * plaque circulaire libre, les premiers tombent autour de 1 : 1,72 : 2,31 —
 * et c'est exactement pour ca qu'un jeton qui tombe ne CHANTE pas. Il tinte.
 * Trois modes suffisent : c'est le meme raisonnement que les deux modes de
 * tole du coup de poing, ou un resonateur unique donnait une note.
 *
 * Les modes sont haut places — le fondamental du jeton insere est a 2 100 Hz —
 * parce qu'un disque de deux centimetres et demi est petit. C'est cette hauteur
 * qui rend le jeton BRILLANT, l'exact contraire du coup de poing qui est mat :
 * mesures par le meme `sg_brillance`, le coup vaut 0,45 fois un bruit blanc et
 * les jetons 3,08, 13,09 et 15,14. Le rendu VERIFIE ces deux proprietes — la
 * brillance et le compte de rebonds — et refuse d'ecrire un fichier qui ment.
 *
 * Les rebonds, et l'intervalle qui RACCOURCIT
 * -------------------------------------------
 * C'est le detail qui fait toute la difference entre « ca tombe » et « ca sonne
 * trois fois ». Une piece qui rebondit perd de l'energie a chaque choc, donc
 * remonte moins haut, donc RETOMBE PLUS TOT : l'intervalle se resserre. Un
 * intervalle constant s'entendrait comme un mecanisme regulier — c'est
 * l'argument des glouglous de la chasse et des tripes du coup de poing, la
 * troisieme fois qu'il sert ici.
 *
 * Chaque choc a donc son propre banc de resonateurs, et non un banc commun :
 * une enveloppe unique ancree a t = 0 eteindrait les rebonds tardifs, et un
 * resonateur laisse a son seul Q sonnerait tres court — il faudrait un Q de
 * 400 pour tenir soixante millisecondes a 2 100 Hz, ce que ce filtre a variable
 * d'etat ne tient pas proprement. Douze filtres au total, pour trois fichiers
 * rendus une fois au build : ca ne coute rien.
 *
 * Ce qui reste sous le tintement
 * ------------------------------
 * LE BAC. La piece ne rebondit pas dans le vide : elle rebondit SUR quelque
 * chose, et ce quelque chose sonne. Une caisse de monnayeur est un coffret de
 * tole — grave et long ; un godet de distributeur est petit — plus aigu et plus
 * court ; le clapet de refus est amorti — il n'a presque rien a rendre. C'est
 * cette resonance-la, et non la piece, qui distingue les trois lieux.
 */

/* Trois modes, quatre chocs au plus : un contact d'entree et trois rebonds. */
#define SG_COIN_MODES 3
#define SG_COIN_CHOCS 4

/*
 * Les rapports inharmoniques d'un disque libre. Ecrits une fois pour les trois
 * pieces : c'est le MEME jeton qui tombe dans les trois fichiers, seuls le lieu
 * et la maniere changent. Leur donner trois jeux de rapports ferait entendre
 * trois pieces differentes, ce qui est l'inverse de ce qu'on cherche.
 */
static const float g_coin_ratio[SG_COIN_MODES] = { 1.00f, 1.72f, 2.31f };

typedef struct sg_coin {
    const char *file;
    const char *quoi;        /* ce qu'on entend, pour le journal */
    float length;

    float base_hz;                       /* le mode fondamental du disque */
    float mode_q[SG_COIN_MODES];
    float mode_gain[SG_COIN_MODES];
    float mode_tau[SG_COIN_MODES];       /* les modes hauts s'eteignent avant */

    int   chocs;             /* le contact d'entree, plus les rebonds */
    float gap;               /* intervalle du premier rebond, en secondes */
    float gap_ratio;         /* < 1 : il RACCOURCIT, c'est le point */
    float chute;             /* l'energie que garde un rebond sur le precedent */
    float excit_tau;         /* duree du contact : dur, ou amorti par le clapet */

    float bac_gain;          /* ce sur quoi la piece rebondit */
    float bac_hz, bac_q, bac_tau;

    float peak;              /* pic vise a l'ecriture */

    /*
     * CE QU'ON EXIGE DU FICHIER PRODUIT, verifie et non promis.
     *
     * Le nombre d'attaques est BORNE DES DEUX COTES, et ce n'est pas de la
     * coquetterie : un jeton refuse n'a qu'un seul choc — c'est ce qui le
     * distingue a l'oreille des deux autres — donc lui demander « au moins
     * deux » reviendrait a exiger le defaut qu'on cherche a eviter. Chaque
     * fichier declare donc ce qu'il pretend etre, et le refus tombe des qu'il
     * ment dans un sens ou dans l'autre.
     */
    int   attaques_min, attaques_max;
    double brillance_min;
} sg_coin;

static const sg_coin g_coins[] = {
    /*
     * INSERE — la piece glisse dans la fente, bascule dans le mecanisme et
     * tombe au fond de la caisse. Trois chocs apres le contact d'entree, un
     * intervalle qui passe de 62 a 30 ms, et la caisse de tole du monnayeur a
     * 190 Hz sous le tout. C'est le plus long des trois parce que c'est le seul
     * ou la piece tombe VRAIMENT : elle a de la hauteur a perdre.
     */
    { "jeton_insere.wav", "insere dans la fente", 0.42f,
      2100.0f,
      { 26.0f, 20.0f, 16.0f },
      { 1.00f, 0.62f, 0.34f },
      { 0.090f, 0.055f, 0.032f },
      4, 0.062f, 0.62f, 0.55f, 0.00050f,
      0.55f, 190.0f, 4.0f, 0.075f,
      0.88f,
      /* MESURE : 13,09 fois un bruit blanc, 3 attaques a 0, 61 et 99 ms —
       * l'intervalle passe donc de 61 a 38 ms, il RACCOURCIT. Le quatrieme
       * choc existe dans le rendu (a 124 ms, un sixieme de l'energie du
       * premier) et reste sous le declencheur : il s'entend comme une queue,
       * pas comme un rebond, ce qui est exactement son role. */
      3, 5, 6.00 },

    /*
     * REFUSE — le monnayeur n'en veut pas et le rend par le clapet.
     *
     * UN SEUL CHOC, et c'est toute la difference : la piece ne tombe pas, elle
     * est POUSSEE hors du mecanisme et retenue par un clapet amorti. Le contact
     * dure quatre fois plus longtemps que celui d'une piece qui rebondit sur de
     * la tole (2 ms contre 0,5), ce qui est exactement ce qui rend le coup de
     * poing mat dans la section precedente — un contact long filtre l'aigu. Le
     * fondamental descend a 1 550 Hz : la piece est tenue, donc ses modes hauts
     * sont etouffes avant de s'etablir.
     */
    { "jeton_refuse.wav", "refuse par le monnayeur", 0.22f,
      1550.0f,
      { 14.0f, 11.0f, 9.0f },
      { 1.00f, 0.40f, 0.16f },
      { 0.030f, 0.018f, 0.010f },
      1, 0.050f, 1.00f, 1.00f, 0.00200f,
      0.34f, 260.0f, 2.6f, 0.040f,
      0.72f,
      /* MESURE : 3,08 fois un bruit blanc — quatre fois moins brillant que les
       * deux autres, et sept fois plus que le coup de poing. C'est bien la
       * place qu'on lui cherchait : du metal, mais etouffe. Une seule attaque,
       * a 0 ms. */
      1, 1, 1.60 },

    /*
     * BAC — la piece rendue par le distributeur, qui tombe dans le godet.
     *
     * Deux rebonds seulement et des intervalles courts : le godet est petit, la
     * piece n'a que quelques centimetres a tomber. Sa resonance est HAUTE —
     * 520 Hz contre 190 pour la caisse — parce qu'un petit volume sonne haut,
     * et c'est elle qui fait entendre qu'on prend une piece plutot qu'on n'en
     * met une. Le fichier est court : c'est celui qu'on joue CINQ FOIS de
     * suite quand le monnayeur rend cinq jetons, et un fichier qui traine
     * empilerait cinq queues.
     */
    { "jeton_bac.wav", "tombe dans le godet", 0.26f,
      2450.0f,
      { 22.0f, 17.0f, 14.0f },
      { 1.00f, 0.55f, 0.30f },
      { 0.048f, 0.030f, 0.018f },
      3, 0.041f, 0.66f, 0.48f, 0.00045f,
      0.42f, 520.0f, 5.0f, 0.045f,
      0.85f,
      /* MESURE : 15,14 fois un bruit blanc — le plus brillant des trois, ce que
       * le godet explique — et 3 attaques a 0, 39 et 67 ms, soit 39 puis 28 ms
       * d'intervalle. */
      2, 4, 6.00 },
};

#define SG_COIN_COUNT ((int)(sizeof g_coins / sizeof g_coins[0]))

static size_t sg_render_coin(const sg_coin *c, float *out, size_t cap)
{
    const size_t frames = (size_t)(c->length * (float)SG_RATE);
    if (frames > cap) tool_fatalf("tampon trop petit pour « %s »", c->file);
    if (c->chocs < 1 || c->chocs > SG_COIN_CHOCS) {
        tool_fatalf("« %s » declare %d chocs, hors de 1..%d", c->file,
                    c->chocs, SG_COIN_CHOCS);
    }

    /* La graine melange le nom du fichier : les trois pieces ne partagent
     * jamais leur bruit, et chacune se regenere a l'identique. */
    sg_rng r;
    uint64_t h = 0xCB1Eull;
    for (const char *p = c->file; *p; ++p) h = h * 131ull + (uint64_t)(unsigned char)*p;
    sg_seed(&r, h);

    /*
     * Les instants des chocs. L'intervalle est multiplie par `gap_ratio` a
     * chaque rebond, donc il DECROIT geometriquement — la piece remonte moins
     * haut, donc elle retombe plus tot. L'energie decroit de son cote, et les
     * deux ensemble sont ce qui s'entend comme une chute.
     */
    float at[SG_COIN_CHOCS], amp[SG_COIN_CHOCS];
    {
        float t = 0.0f, gap = c->gap, e = 1.0f;
        for (int k = 0; k < c->chocs; ++k) {
            at[k] = t; amp[k] = e;
            t += gap; gap *= c->gap_ratio; e *= c->chute;
        }
    }

    /* Un banc de resonateurs PAR CHOC : voir l'en-tete de section. */
    sg_svf mode[SG_COIN_CHOCS][SG_COIN_MODES];
    sg_svf bac[SG_COIN_CHOCS];
    for (int k = 0; k < c->chocs; ++k) {
        for (int m = 0; m < SG_COIN_MODES; ++m) {
            sg_svf_set(&mode[k][m], c->base_hz * g_coin_ratio[m], c->mode_q[m]);
        }
        sg_svf_set(&bac[k], c->bac_hz, c->bac_q);
    }

    /* Le retrait du continu, meme raison que pour le coup : une resonance basse
     * excitee par du bruit laisse une composante lente qui deplace le zero du
     * fichier sans s'entendre, et mange de la dynamique a la normalisation. */
    sg_pole dc; sg_pole_set(&dc, 30.0f);

    for (size_t i = 0; i < frames; ++i) {
        const float t = (float)i / (float)SG_RATE;
        const float n = sg_noise(&r);

        float v = 0.0f;
        for (int k = 0; k < c->chocs; ++k) {
            const float d = t - at[k];
            if (d < 0.0f) continue;

            /* L'excitation de CE choc : une salve courte, d'autant plus courte
             * que le contact est dur. C'est elle qui porte le timbre du
             * contact ; le disque, lui, ne fait que repondre. */
            const float choc = n * amp[k] * sg_decay(d, c->excit_tau);

            for (int m = 0; m < SG_COIN_MODES; ++m) {
                v += sg_svf_band(&mode[k][m], choc) * c->mode_gain[m]
                   * sg_decay(d, c->mode_tau[m]) * sg_attack(d, 0.00025f);
            }
            v += sg_svf_band(&bac[k], choc) * c->bac_gain
               * sg_decay(d, c->bac_tau) * sg_attack(d, 0.0020f);
        }

        out[i] = sg_highpass(&dc, v);
    }

    /* Fondu de sortie, meme raison que partout ailleurs dans ce fichier. */
    const size_t fade = (size_t)(0.010f * (float)SG_RATE);
    for (size_t i = 0; i < fade && i < frames; ++i) {
        out[frames - 1 - i] *= (float)i / (float)fade;
    }
    return frames;
}

/* ==========================================================================
 * LE COUPERET — les cinq bruits d'une manche
 * ==========================================================================
 * Le mode competitif de `room/room_couperet.h` etait entierement MUET, et c'est
 * le defaut le plus couteux qu'il restait : sa regle tient dans une minuterie de
 * quarante-cinq secondes, et une minuterie qu'on ne peut pas ENTENDRE n'arbitre
 * rien. Le joueur a les yeux sur la dalle d'une borne — c'est meme tout le mode,
 * puisqu'il faut jouer pour marquer — donc il ne regarde pas le compte a
 * rebours. Ce qui doit lui dire l'heure ne peut etre qu'un son.
 *
 * LE VOCABULAIRE EST ELECTRIQUE, ET CE N'EST PAS UNE DECORATION
 * -------------------------------------------------------------
 * « Couperet » nomme aussi le geste d'un disjoncteur, et les six actions du mode
 * sont toutes des gestes d'electricien : brouiller une image, inverser un
 * cablage, couper le courant, blinder un tableau, renvoyer une surtension. La
 * salle, elle, a des neons, des tubes et un compteur — `sg_render_room_tone`
 * plus haut est deja batie sur le ronflement du secteur a 50 Hz et ses
 * harmoniques, et ces cinq bruits en reprennent le 100 Hz des ballasts. Ils
 * sortent donc du meme vocabulaire : des contacts, des armatures, de la tole de
 * tableau, une alimentation qui tombe. Aucun n'est une lame de guillotine, et
 * aucun n'est une nappe de science-fiction — les deux diraient un autre jeu.
 *
 * POURQUOI CINQ, ET PAS SIX NI TROIS
 * ----------------------------------
 * Un son de plus qu'on ne joue jamais est un son de trop. Chacun des cinq repond
 * donc a UN evenement de `room_cp_evt`, et il n'y en a pas d'autre :
 *
 *   couperet_tic       les dix dernieres secondes de `couperet.prochain`
 *   couperet_lame      ROOM_CP_EVT_COUPERET — la manche vient de sortir quelqu'un
 *   couperet_coupure   ROOM_CP_EVT_ACTION / ROOM_CP_COUPURE — une borne s'eteint
 *   couperet_blindage  ROOM_CP_EVT_ABSORBE — la plaque a tenu
 *   couperet_renvoi    ROOM_CP_EVT_RENVOYE — le leurre a retourne la surtension
 *
 * ABSORBE et RENVOYE sont DEUX evenements distincts dans l'enumeration, et c'est
 * pour ca qu'ils ont deux sons : « ca a tenu » et « c'est reparti chez toi » ne
 * sont pas la meme nouvelle, et l'attaquant qui les confondrait n'apprendrait
 * jamais ce que coute un leurre. Faire jouer le blindage sur un renvoi serait la
 * faute exacte que `room_sound.h` reproche au jeton de 2020 : un son emprunte
 * qui dit faux.
 *
 * ROOM_CP_EVT_ACTION, lui, couvre les SIX actions. Lui donner un son unique
 * dirait la meme chose six fois et la dirait fausse cinq fois ; seule la coupure
 * en recoit un, parce que c'est la seule qui DETRUISE quelque chose chez la
 * cible. ARRIVEE, DEPART, DEBUT, PARTIE et FIN restent muets : ce sont des
 * changements d'etat que le bandeau affiche, et aucun n'arrive pendant que le
 * joueur a les yeux ailleurs — ce qui est le seul argument qui justifie un son.
 *
 * CE QU'ON VERIFIE, ET AVEC QUOI
 * ------------------------------
 * Trois instruments, et pas un de plus, pour que les chiffres se comparent d'un
 * son a l'autre — c'est l'argument que `sg_brillance` porte deja dans son
 * en-tete :
 *
 *   BRILLANCE  `sg_brillance`, la meme que pour le coup de poing (0,45) et les
 *              trois jetons (3,08 a 15,14). Elle dit ou se trouve l'energie.
 *   DUREE UTILE `sg_t95`, l'instant ou 95 % de l'energie du fichier est passee.
 *              La longueur d'un fichier ne dit rien — un tic de 40 ms peut
 *              n'etre qu'un clic de 5 ms suivi de silence, et c'est justement ce
 *              qu'on veut de lui.
 *   GLISSE     `sg_glisse`, le rapport de puissance entre 1 200 et 300 Hz sur
 *              une fenetre. Mesure sur deux fenetres, il dit si la hauteur
 *              MONTE ou DESCEND — c'est le meme instrument que celui qui verifie
 *              le remplissage de la chasse d'eau, ici applique a deux sons dont
 *              c'est toute la definition.
 *
 * ET UN QUATRIEME QU'ON N'EMPLOIE PAS, ce qu'il faut dire plutot que le laisser
 * deviner. `sg_attaques` compte les rebonds des jetons et rend ici des nombres
 * qui ne veulent rien dire : 107 pour la lame, 69 pour la coupure, 112 pour le
 * renvoi. Ce n'est pas un defaut de ces fichiers, c'est le DOMAINE de
 * l'instrument, et son propre en-tete le donne : son detecteur d'enveloppe est
 * un passe-bas a 120 Hz, cale sur des jetons dont le mode le plus grave est a
 * 1 550 Hz. Ces trois sons-la sont batis sur 58, 85 et 110 Hz — leur porteuse
 * redressee tombe DANS la bande du detecteur, qui compte alors une attaque par
 * periode. Le blindage, lui, en annonce 3, toutes les trois dans ses six
 * premieres millisecondes : c'est un seul impact que le battement des modes
 * jumeaux fait franchir trois fois le seuil.
 *
 * Seul le tic est dans le domaine de cet instrument — il n'a rien sous 520 Hz —
 * et lui seul porte donc une borne d'attaques. Elle y vaut son prix : un tic qui
 * en compterait deux serait un DOUBLE declic, et joue dix fois de suite un
 * double declic ne se lit plus comme une seconde qui passe. Pour les quatre
 * autres, nommer la limite de la mesure vaut mieux qu'inventer un seuil qui
 * verifierait le bruit du detecteur.
 */

/* ---- le tic ---------------------------------------------------------------
 * LE BATTEMENT DES DIX DERNIERES SECONDES, joue une fois par seconde entiere.
 *
 * C'est un CONTACT DE RELAIS et rien d'autre : une armature qui claque sur son
 * noyau, dans un boitier de la taille d'une boite d'allumettes. Deux resonances
 * — l'armature a 3 250 Hz, le boitier a 1 480 — excitees par une salve de bruit
 * de 0,15 ms. C'est le contact le plus DUR de toute la banque : le poing dure
 * 3,5 ms, le jeton 0,5, celui-ci treize fois moins que le poing, parce que du
 * metal sur du metal sans rien entre les deux ne s'amortit pas.
 *
 * LA DUREE, ET POURQUOI ELLE EST CE QU'ELLE EST
 * ---------------------------------------------
 * Le fichier fait 40 ms ; sa DUREE UTILE mesuree — l'instant ou 95 % de son
 * energie est passee, voir `sg_t95` — est de 4,6 ms. C'est la contrainte
 * principale de ce son et non une economie : il est joue DIX FOIS de suite, a
 * une seconde d'intervalle, dans le moment ou le joueur est deja sous pression.
 * Deux defauts le rendraient alors insupportable :
 *
 *   - une queue longue. Au-dela d'une centaine de millisecondes, le tic cesse
 *     d'etre un instant et devient une TEXTURE qui occupe un dixieme de chaque
 *     seconde. A 4,6 ms il en occupe un deux-centieme.
 *   - une hauteur tenue. Un resonateur trop resonant donne une NOTE, et dix
 *     notes identiques font une melodie qu'on suit au lieu d'un compte qu'on
 *     subit. Un `sg_svf` s'eteint a 1/e en Q / (pi f), soit ici 2,7 ms a
 *     3 250 Hz et 4,3 ms a 1 480 : neuf cycles et six cycles. C'est assez pour
 *     TIMBRER — sans quoi le tic serait un clic quelconque — et beaucoup trop
 *     peu pour chanter.
 *
 * RIEN SOUS 520 Hz, et il faut dire exactement ce que ce passe-haut fait, parce
 * que ce n'est pas ce que la premiere version de ce commentaire affirmait.
 *
 * Il ne faconne RIEN aujourd'hui : le retirer fait passer la brillance de 9,46 a
 * 8,71, c'est-a-dire presque rien, puisqu'il n'y a rien de grave a retirer dans
 * un son bati sur 1 480 et 3 250 Hz. Ce qu'il fait, c'est empecher qu'il y en
 * ait un jour. Mesure : en ajoutant au tic une tole a 205 Hz — celle de la
 * lame, au gain 2,0 —, la brillance tombe a 2,95 sans le passe-haut et reste a
 * 8,60 avec. Il est donc une GARDE contre un reglage futur, et pas une etape de
 * mise en forme ; c'est le plancher de brillance a 5,00 qui refuse le fichier
 * dans le cas ou les deux sautent ensemble, et il le fait (2,95 < 5,00).
 *
 * A 9,46 fois un bruit blanc, le tic n'est d'ailleurs pas le son le plus clair
 * de la banque — les deux jetons qui tombent le depassent, a 13,09 et 15,14 —
 * mais il est 345 fois plus clair que la lame, et c'est ce chiffre-la qui
 * compte. Voir la verification croisee en fin de fichier : les deux sons disent
 * le meme evenement a deux instants, donc ils doivent etre aux deux bouts de
 * l'instrument qui les mesure.
 */
#define SG_CP_TIC_LEN 0.040f

static size_t sg_render_cp_tic(float *out, size_t cap)
{
    const size_t frames = (size_t)(SG_CP_TIC_LEN * (float)SG_RATE);
    if (frames > cap) tool_fatalf("tampon trop petit pour le tic du couperet");

    sg_rng r; sg_seed(&r, 0x71C0ull);

    sg_svf armature, boitier;
    sg_svf_set(&armature, 3250.0f, 28.0f);
    sg_svf_set(&boitier,  1480.0f, 20.0f);

    /* Deux poles en cascade : un seul laisse remonter un grave qui, a dix
     * repetitions, s'entend comme un battement de coeur. Ce n'est pas ce qu'on
     * raconte — le couperet n'est pas un organe, c'est une horloge. */
    sg_pole hp1, hp2;
    sg_pole_set(&hp1, 520.0f);
    sg_pole_set(&hp2, 520.0f);

    for (size_t i = 0; i < frames; ++i) {
        const float t = (float)i / (float)SG_RATE;
        const float n = sg_noise(&r);

        /* 0,15 ms : sept echantillons. Le contact d'un petit relais. */
        const float choc = n * sg_decay(t, 0.00015f);

        float v = 0.0f;
        /* AUCUNE enveloppe de decroissance sur ces deux lignes, et c'est une
         * correction : la premiere version en posait une, a 3,2 et 6,0 ms, et
         * elle ne servait a rien. Un `sg_svf` s'eteint tout seul en Q / (pi f) —
         * a Q 6 et 3 250 Hz, en 0,6 ms — donc c'etait le FILTRE qui decidait, et
         * le commentaire annoncait une duree que le fichier n'avait pas. Les Q
         * portent maintenant ce qu'ils annoncent : 28 a 3 250 Hz font 2,7 ms,
         * 20 a 1 480 Hz en font 4,3. */
        v += sg_svf_band(&armature, choc) * 1.00f * sg_attack(t, 0.00015f);
        v += sg_svf_band(&boitier,  choc) * 0.50f * sg_attack(t, 0.00025f);
        /* Le contact NU, non filtre : c'est lui qui fait le « t » de « tic ».
         * Sans lui on entend deux resonances s'allumer, ce qui est un carillon
         * miniature et non un declic. Il pesait 0,60 et emportait a lui seul
         * 95 % de l'energie du fichier — mesure : la duree utile tombait a 1 ms,
         * et le relais n'avait plus de timbre du tout. */
        v += choc * 0.28f;

        out[i] = sg_highpass(&hp2, sg_highpass(&hp1, v));
    }

    /* Fondu de sortie, meme raison que partout ailleurs. Court, parce que le son
     * est fini depuis longtemps quand il arrive. */
    const size_t fade = (size_t)(0.003f * (float)SG_RATE);
    for (size_t i = 0; i < fade && i < frames; ++i) {
        out[frames - 1 - i] *= (float)i / (float)fade;
    }
    return frames;
}

/* ---- la lame --------------------------------------------------------------
 * LA MANCHE VIENT DE SORTIR QUELQU'UN. Le seul son du mode qui ait le droit
 * d'etre gros, et il l'est parce que c'est le seul qui concerne TOUT LE MONDE :
 * huit joueurs apprennent en meme temps qu'il y en a un de moins.
 *
 * Ce qui tombe, c'est un CONTACTEUR DE PUISSANCE dans une armoire, et il fait
 * quatre choses a la fois — meme construction que le coup de poing sur une
 * borne, et pour la meme raison : c'est leur superposition qui rend un objet
 * reconnaissable.
 *
 *   L'ARMATURE   une masse mobile de quelques centaines de grammes qui claque
 *                sur son noyau. DEUX chocs et non un, a 34 ms d'intervalle : un
 *                contacteur qui colle rebondit, et ses contacts auxiliaires
 *                suivent le principal. Un choc unique donne un interrupteur ;
 *                deux donnent une machine. Chaque choc a son propre banc de
 *                resonateurs, exactement pour la raison ecrite au-dessus des
 *                jetons — une enveloppe ancree a t = 0 eteindrait le second.
 *   LE TABLEAU   la tole de l'armoire, sur deux modes (205 et 455 Hz). Elle est
 *                NUE, contrairement au panneau laque de la borne : les Q sont
 *                donc plus hauts (7,0 et 9,0 contre 3,2 et 4,0) et elle sonne
 *                deux fois plus longtemps. C'est ce qui fait entendre une
 *                armoire electrique plutot qu'un meuble.
 *   LA SALLE     58 Hz, presque une demi-seconde. C'est « la salle qui accuse le
 *                coup » : le meme role que la caisse a 82 Hz du coup de poing,
 *                une octave plus bas parce que ce n'est plus un meuble qu'on
 *                frappe mais un batiment qui encaisse.
 *   LES BALLASTS le ronflement du secteur a 100 Hz et ses deux harmoniques, qui
 *                ENFLENT puis retombent en un tiers de seconde. C'est la seule
 *                composante qui ne soit pas un choc, et c'est elle qui dit que
 *                l'evenement est electrique : quand la puissance bascule, tous
 *                les tubes de la salle le repercutent avant de se rasseoir.
 *                Elle est a 100 Hz et non a 50 parce qu'un ballast ronfle au
 *                DOUBLE du secteur — c'est deja ce qu'ecrit `sg_render_room_tone`
 *                et ce que reprend l'extracteur.
 */
#define SG_CP_LAME_LEN   1.15f
#define SG_CP_LAME_CHOCS 2

static size_t sg_render_cp_lame(float *out, size_t cap)
{
    const size_t frames = (size_t)(SG_CP_LAME_LEN * (float)SG_RATE);
    if (frames > cap) tool_fatalf("tampon trop petit pour la lame du couperet");

    sg_rng r; sg_seed(&r, 0x1AA3ull);

    /* Le principal, puis le rebond des auxiliaires. */
    const float at[SG_CP_LAME_CHOCS]  = { 0.000f, 0.034f };
    const float amp[SG_CP_LAME_CHOCS] = { 1.000f, 0.38f };

    sg_svf coeur[SG_CP_LAME_CHOCS], pastille[SG_CP_LAME_CHOCS];
    sg_svf tole1[SG_CP_LAME_CHOCS], tole2[SG_CP_LAME_CHOCS];
    sg_svf salle[SG_CP_LAME_CHOCS];
    for (int k = 0; k < SG_CP_LAME_CHOCS; ++k) {
        sg_svf_set(&coeur[k],     340.0f, 4.0f);   /* la masse mobile */
        sg_svf_set(&pastille[k], 1150.0f, 8.0f);   /* les pastilles de contact */
        sg_svf_set(&tole1[k],     205.0f, 7.0f);
        sg_svf_set(&tole2[k],     455.0f, 9.0f);
        sg_svf_set(&salle[k],      58.0f, 5.0f);
    }

    /* Le retrait du continu, meme raison que pour le coup et les jetons. Pose a
     * 18 Hz et non 22 : la salle descend a 58 Hz ici, et un passe-haut trop haut
     * mangerait precisement ce qu'on cherche a produire. */
    sg_pole dc; sg_pole_set(&dc, 18.0f);

    for (size_t i = 0; i < frames; ++i) {
        const float t = (float)i / (float)SG_RATE;
        const float w = 6.28318530717959f * t;
        const float n = sg_noise(&r);

        float v = 0.0f;

        for (int k = 0; k < SG_CP_LAME_CHOCS; ++k) {
            const float d = t - at[k];
            if (d < 0.0f) continue;

            /* 1,8 ms : plus long que le relais (0,15) parce que la masse est
             * mille fois plus grande, plus court que le poing (3,5) parce que
             * rien n'amortit du fer contre du fer. */
            const float choc = n * amp[k] * sg_decay(d, 0.0018f);

            v += sg_svf_band(&coeur[k],    choc) * 0.95f * sg_decay(d, 0.060f)
               * sg_attack(d, 0.0008f);
            v += sg_svf_band(&pastille[k], choc) * 0.30f * sg_decay(d, 0.012f)
               * sg_attack(d, 0.0003f);
            v += sg_svf_band(&tole1[k],    choc) * 0.70f * sg_decay(d, 0.300f)
               * sg_attack(d, 0.0020f);
            v += sg_svf_band(&tole2[k],    choc) * 0.40f * sg_decay(d, 0.190f)
               * sg_attack(d, 0.0012f);
            /* La salle met le plus longtemps a repondre — un grand volume ne
             * demarre pas dans la milliseconde — et le plus longtemps a se
             * taire. Meme raisonnement que la caisse du coup de poing, avec le
             * double de constante. */
            v += sg_svf_band(&salle[k],    choc) * 1.15f * sg_decay(d, 0.450f)
               * sg_attack(d, 0.0060f);
        }

        /* Les ballasts. Ils enflent en 12 ms — le temps que la puissance
         * bascule — et retombent en 0,30 s. Phase nulle a l'origine, donc pas de
         * discontinuite au premier echantillon. */
        {
            const float e = sg_attack(t, 0.012f) * sg_decay(t, 0.300f);
            v += 0.150f * sinf(w * 100.0f) * e;
            v += 0.065f * sinf(w * 200.0f) * e;
            v += 0.030f * sinf(w * 300.0f) * e;
        }

        out[i] = sg_highpass(&dc, v);
    }

    const size_t fade = (size_t)(0.030f * (float)SG_RATE);
    for (size_t i = 0; i < fade && i < frames; ++i) {
        out[frames - 1 - i] *= (float)i / (float)fade;
    }
    return frames;
}

/* ---- la coupure -----------------------------------------------------------
 * ON VIENT D'ETEINDRE VOTRE BORNE. Ce n'est PAS la lame, et le fichier existe
 * pour qu'on ne puisse pas les confondre : la lame est la manche qui tranche,
 * la coupure est un adversaire qui a depense quatre fusibles contre vous. Elle
 * est donc plus proche, deux fois plus breve, et surtout elle raconte autre
 * chose.
 *
 * CE QU'ON ENTEND EST UNE PANNE, PAS UNE EXPLOSION
 * ------------------------------------------------
 * Une explosion est du bruit large qui part fort et decroit. Une alimentation
 * qui tombe fait l'inverse d'un evenement : elle DESCEND. Le transformateur et
 * le balayage de la borne perdent leur frequence a mesure que la tension chute,
 * et c'est cette GLISSADE VERS LE BAS qui est le son d'une panne — tout le monde
 * l'a entendue sur un appareil qu'on debranche. Elle est ici de 640 a 85 Hz en
 * une constante de 0,10 s, et le plancher de bruit de la borne se referme avec
 * elle : un passe-bande promene de 3 000 a 300 Hz par `sg_svf_tune`, qui existe
 * exactement pour ca et dont l'en-tete explique pourquoi on ne peut pas utiliser
 * `sg_svf_set` a la place.
 *
 * Le contact du relais qui a lache est LA, mais mixe bas et sans aigu (900 et
 * 2 200 Hz, pas 3 250) : ce n'est pas un declic qu'on veut entendre, c'est ce
 * qui vient apres. Un contact trop present ferait de ce fichier un deuxieme tic.
 *
 * Aucun grave de salle, aucune tole d'armoire : la coupure ne concerne qu'une
 * borne. C'est la difference que `room_sound.c` traduit en portee, et le fichier
 * doit la porter aussi — un fichier qui gronde s'entend gros meme joue bas.
 */
#define SG_CP_COUPURE_LEN 0.50f

static size_t sg_render_cp_coupure(float *out, size_t cap)
{
    const size_t frames = (size_t)(SG_CP_COUPURE_LEN * (float)SG_RATE);
    if (frames > cap) tool_fatalf("tampon trop petit pour la coupure");

    sg_rng r; sg_seed(&r, 0xC0FFull);

    sg_svf contact1, contact2;
    sg_svf_set(&contact1,  900.0f, 5.0f);
    sg_svf_set(&contact2, 2200.0f, 6.0f);

    /* Le plancher de bruit de la borne, qui se referme. */
    sg_svf souffle; sg_svf_set(&souffle, 3000.0f, 1.6f);

    sg_pole dc; sg_pole_set(&dc, 30.0f);

    float ph = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        const float t = (float)i / (float)SG_RATE;
        const float n = sg_noise(&r);

        float v = 0.0f;

        /* Le relais qui lache : 0,8 ms, discret. */
        {
            const float choc = n * sg_decay(t, 0.0008f);
            v += sg_svf_band(&contact1, choc) * 0.42f * sg_decay(t, 0.010f)
               * sg_attack(t, 0.0004f);
            v += sg_svf_band(&contact2, choc) * 0.20f * sg_decay(t, 0.005f)
               * sg_attack(t, 0.0002f);
        }

        /* L'alimentation qui tombe. La frequence suit une exponentielle, donc
         * la glissade est rapide au debut et tenue a la fin — c'est ce que fait
         * un condensateur qui se vide, et c'est ce qui distingue une panne d'un
         * simple fondu. */
        {
            const float hz = 85.0f + (640.0f - 85.0f) * sg_decay(t, 0.100f);
            ph += 6.28318530717959f * hz / (float)SG_RATE;
            const float e = sg_attack(t, 0.004f);
            v += 0.62f * sinf(ph)        * e * sg_decay(t, 0.140f);
            v += 0.20f * sinf(ph * 2.0f) * e * sg_decay(t, 0.095f);
        }

        /* Le souffle qui se referme : meme glissade, sur le bruit. */
        {
            const float hz = 300.0f + (3000.0f - 300.0f) * sg_decay(t, 0.085f);
            sg_svf_tune(&souffle, hz);
            v += sg_svf_band(&souffle, n) * 0.55f * sg_decay(t, 0.180f)
               * sg_attack(t, 0.0025f);
        }

        out[i] = sg_highpass(&dc, v);
    }

    const size_t fade = (size_t)(0.020f * (float)SG_RATE);
    for (size_t i = 0; i < fade && i < frames; ++i) {
        out[frames - 1 - i] *= (float)i / (float)fade;
    }
    return frames;
}

/* ---- le blindage ----------------------------------------------------------
 * UNE ATTAQUE ENCAISSEE. C'est la SEULE bonne nouvelle des cinq, et il faut
 * qu'elle s'entende comme telle — ce qui, en pratique, veut dire trois choses
 * mesurables et non un adjectif :
 *
 *   COURT      0,30 s de fichier. Une bonne nouvelle qui traine devient une
 *              annonce ; ce qu'on veut, c'est un accuse de reception.
 *   METALLIQUE une plaque d'acier boulonnee sur un tableau, sur trois modes
 *              INHARMONIQUES (1 : 1,51 : 2,14). Le raisonnement est celui du
 *              jeton : une corde donne une note, une plaque tinte. Les rapports
 *              ne sont pas ceux du disque (1 : 1,72 : 2,31) parce que ce n'est
 *              pas un disque — c'est une plaque rectangulaire tenue par ses
 *              bords, et ses modes sont plus resserres.
 *   SONNANT    et c'est la difference d'avec le tic, qui est l'autre son court
 *              et clair du lot. Le tic est un contact MORT : il claque et il n'y
 *              a plus rien — 4,6 ms de duree utile. Le blindage SONNE : 97,1 ms,
 *              soit vingt et une fois plus, parce qu'une plaque qui encaisse
 *              rend l'energie au lieu de l'absorber. C'est exactement ce que
 *              `sg_t95` mesure, et c'est la seule chose qui separe ces deux-la —
 *              en brillance ils sont du meme cote de l'echelle.
 *
 * Sous le tintement, un seul grave : la fixation de la plaque a 320 Hz, tres
 * court (14 ms). C'est le POIDS de l'objet — sans lui la plaque sonne comme une
 * feuille de tole et non comme un blindage — mais il ne tient pas, parce que le
 * blindage n'est pas un evenement de salle. Il pesait 0,30 pour 20 ms dans une
 * premiere version : il emportait alors assez d'energie basse pour ramener la
 * brillance a 0,94, c'est-a-dire au niveau d'un coup de poing sur du bois.
 */
#define SG_CP_BLINDAGE_LEN   0.30f
#define SG_CP_BLINDAGE_MODES 3

static size_t sg_render_cp_blindage(float *out, size_t cap)
{
    const size_t frames = (size_t)(SG_CP_BLINDAGE_LEN * (float)SG_RATE);
    if (frames > cap) tool_fatalf("tampon trop petit pour le blindage");

    sg_rng r; sg_seed(&r, 0xB11Dull);

    /*
     * LES MODES SONT DES SINUSOIDES ET NON DES `sg_svf`, et c'est la seule fois
     * dans ce fichier — donc il faut dire pourquoi.
     *
     * Un `sg_svf` excite par une impulsion s'eteint en Q / (pi f). Pour tenir
     * les cent millisecondes qu'on veut ici a 1 180 Hz il faudrait un Q de 370,
     * ce que ce filtre ne tient pas proprement — c'est deja l'argument ecrit
     * au-dessus des jetons, qui s'en sortent en RE-EXCITANT le banc a chaque
     * rebond. Une plaque qu'on frappe une fois n'a pas de rebond a offrir : elle
     * est frappee une fois et elle sonne. Mesure de la version precedente, qui
     * empilait des Q de 30 sous une enveloppe de 130 ms : 13 ms de duree utile,
     * soit un blindage aussi mort que le tic — exactement ce que ce son ne doit
     * pas etre.
     *
     * Une somme de sinusoides amorties EST la solution exacte pour une plaque :
     * c'est ce que ses modes font, et la duree devient celle qu'on ecrit.
     *
     * LE JUMEAU DESACCORDE (1 191 contre 1 180 Hz) n'est pas un quatrieme mode :
     * c'est le meme. Les modes d'une plaque rectangulaire viennent par paires
     * degenerees que la moindre asymetrie — un boulon, une soudure — separe de
     * quelques hertz. Ce qu'on entend alors est un BATTEMENT, ici a 11 Hz, et
     * c'est ce qui distingue une plaque reelle d'un carillon de synthese.
     */
    static const float mhz[SG_CP_BLINDAGE_MODES] = { 1320.0f, 1993.0f, 2825.0f };
    static const float mg[SG_CP_BLINDAGE_MODES]  = {   1.00f,   0.62f,   0.34f };
    static const float mt[SG_CP_BLINDAGE_MODES]  = {  0.075f,  0.050f,  0.032f };

    /* Le contact de l'attaque sur l'acier, et le poids de la fixation. */
    sg_svf contact;  sg_svf_set(&contact, 3400.0f, 8.0f);
    sg_svf fixation; sg_svf_set(&fixation, 320.0f, 3.0f);

    sg_pole dc; sg_pole_set(&dc, 60.0f);

    for (size_t i = 0; i < frames; ++i) {
        const float t = (float)i / (float)SG_RATE;
        const float w = 6.28318530717959f * t;
        const float n = sg_noise(&r);

        /* 0,30 ms : l'attaque arrive sur de l'acier, pas sur de la chair. */
        const float choc = n * sg_decay(t, 0.00030f);

        float v = 0.0f;
        for (int m = 0; m < SG_CP_BLINDAGE_MODES; ++m) {
            v += mg[m] * sinf(w * mhz[m]) * sg_decay(t, mt[m])
               * sg_attack(t, 0.0008f);
        }
        v += 0.55f * sinf(w * 1332.0f) * sg_decay(t, mt[0]) * sg_attack(t, 0.0008f);

        v += sg_svf_band(&contact,  choc) * 0.60f * sg_decay(t, 0.004f)
           * sg_attack(t, 0.00025f);
        /* Le poids, et rien de plus : 20 ms. Une plaque sans grave sonne comme
         * une feuille de tole, une plaque dont le grave TIENT devient un
         * evenement de salle — et le blindage n'en est pas un. */
        v += sg_svf_band(&fixation, choc) * 0.18f * sg_decay(t, 0.014f)
           * sg_attack(t, 0.0015f);

        out[i] = sg_highpass(&dc, v);
    }

    const size_t fade = (size_t)(0.012f * (float)SG_RATE);
    for (size_t i = 0; i < fade && i < frames; ++i) {
        out[frames - 1 - i] *= (float)i / (float)fade;
    }
    return frames;
}

/* ---- le renvoi ------------------------------------------------------------
 * LE LEURRE A RETOURNE LA SURTENSION. C'est l'exact MIROIR de la coupure, et
 * c'est comme ca qu'il est construit : la coupure descend, le renvoi monte.
 *
 * Ce n'est pas une coquetterie de symetrie. Ces deux sons arrivent au meme
 * joueur — celui qui vient d'attaquer — a une seconde d'intervalle selon que sa
 * cible etait nue ou couverte, et ce sont les deux verdicts opposes du meme
 * geste. Une hauteur qui monte contre une hauteur qui descend est la seule
 * difference qu'une oreille classe SANS apprendre : personne n'a jamais eu
 * besoin qu'on lui explique lequel des deux est une mauvaise nouvelle.
 *
 * La montee est EXPONENTIELLE et non lineaire — de 110 Hz vers 1 500 en une
 * constante de 76 ms — parce qu'une surtension qui repart s'emballe. Une rampe
 * lineaire s'entend comme un effet ; une exponentielle s'entend comme une
 * cause.
 *
 * L'ARC. Huit craquements a des instants poses a la main, dont l'ECART SE
 * RESSERRE : 27, 33, 28, 22, 18, 14 puis 11 ms. C'est le troisieme emploi du
 * meme argument dans ce fichier — les glouglous de la chasse, les tripes du
 * coup, les rebonds du jeton — et il vaut ici a l'envers : un intervalle qui se
 * resserre pendant qu'une hauteur monte est ce qui s'entend comme « ca
 * s'emballe » plutot que « ca dure ». Ils passent par un passe-bande fixe a
 * 2 600 Hz : un arc electrique crepite dans l'aigu quelle que soit la tension.
 *
 * Et la fin : le retour ARRIVE. Un choc dur a 195 ms, la ou la glissade est en
 * haut — sans lui la montee se dissoudrait, et un renvoi qui ne touche pas ne
 * dit pas qu'il a touche.
 */
#define SG_CP_RENVOI_LEN 0.42f
#define SG_CP_RENVOI_ARCS 8

static size_t sg_render_cp_renvoi(float *out, size_t cap)
{
    const size_t frames = (size_t)(SG_CP_RENVOI_LEN * (float)SG_RATE);
    if (frames > cap) tool_fatalf("tampon trop petit pour le renvoi");

    sg_rng r; sg_seed(&r, 0x5E7Aull);

    static const float arc_at[SG_CP_RENVOI_ARCS] = {
        0.012f, 0.039f, 0.072f, 0.100f, 0.122f, 0.140f, 0.154f, 0.165f
    };
    sg_svf arc; sg_svf_set(&arc, 2600.0f, 7.0f);

    /* L'impact du retour, a 195 ms. */
    const float coup_at = 0.195f;
    sg_svf impact1, impact2;
    sg_svf_set(&impact1, 1450.0f, 9.0f);
    sg_svf_set(&impact2,  520.0f, 5.0f);

    sg_pole dc; sg_pole_set(&dc, 40.0f);

    float ph = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        const float t = (float)i / (float)SG_RATE;
        const float n = sg_noise(&r);

        float v = 0.0f;

        /* La surtension qui remonte le cable. Bornee a 4 kHz : au-dela on
         * replierait le spectre, et un repliement s'entend comme une descente —
         * l'exact contraire de ce que ce fichier existe pour dire. */
        {
            float hz = 110.0f * expf(t / 0.076f);
            if (hz > 4000.0f) hz = 4000.0f;
            ph += 6.28318530717959f * hz / (float)SG_RATE;
            /* L'amplitude ENFLE jusqu'a l'impact puis lache : c'est ce qui fait
             * que la montee a une destination. */
            const float e = sg_attack(t, 0.020f)
                          * ((t < coup_at) ? (0.35f + 0.65f * t / coup_at)
                                           : sg_decay(t - coup_at, 0.050f));
            v += 0.55f * sinf(ph)        * e;
            v += 0.16f * sinf(ph * 2.0f) * e;
        }

        /* L'arc. */
        for (int k = 0; k < SG_CP_RENVOI_ARCS; ++k) {
            const float d = t - arc_at[k];
            if (d < 0.0f || d > 0.030f) { (void)sg_svf_band(&arc, 0.0f); continue; }
            const float drive = (d < 0.0008f) ? n : 0.0f;
            /* Les derniers craquent plus fort : la tension monte avec la
             * frequence, et un arc suit la tension. */
            const float g = 0.16f + 0.22f * (float)k / (float)(SG_CP_RENVOI_ARCS - 1);
            v += g * sg_svf_band(&arc, drive) * sg_decay(d, 0.0045f);
        }

        /* L'arrivee. */
        {
            const float d = t - coup_at;
            if (d >= 0.0f) {
                const float choc = n * sg_decay(d, 0.0006f);
                v += sg_svf_band(&impact1, choc) * 0.85f * sg_decay(d, 0.045f)
                   * sg_attack(d, 0.0004f);
                v += sg_svf_band(&impact2, choc) * 0.45f * sg_decay(d, 0.090f)
                   * sg_attack(d, 0.0015f);
            } else {
                (void)sg_svf_band(&impact1, 0.0f);
                (void)sg_svf_band(&impact2, 0.0f);
            }
        }

        out[i] = sg_highpass(&dc, v);
    }

    const size_t fade = (size_t)(0.015f * (float)SG_RATE);
    for (size_t i = 0; i < fade && i < frames; ++i) {
        out[frames - 1 - i] *= (float)i / (float)fade;
    }
    return frames;
}

/*
 * LA TABLE DES CINQ, et ce que chacun doit prouver.
 *
 * Les bornes sont posees APRES avoir mesure ce que le rendu produit vraiment —
 * les valeurs mesurees sont dans chaque ligne — et avec la marge qu'il faut pour
 * qu'un reglage de timbre ne casse pas le build sans raison, mais pas plus :
 * au-dela elles ne refuseraient plus rien. C'est la meme discipline que la table
 * des jetons juste au-dessus.
 *
 * Une borne a 0 veut dire « pas de plancher » ; une borne haute a 0 veut dire
 * « pas de plafond ». Toutes les lignes n'ont pas les memes proprietes
 * DEFINISSANTES, et remplir une colonne sans raison serait exactement le genre
 * de verification decorative qui donne l'illusion d'un test.
 */
typedef struct sg_cp {
    const char *file;
    const char *quoi;
    size_t (*render)(float *, size_t);
    float  length;
    float  peak;

    double brillance_min, brillance_max;   /* 0 = pas de borne de ce cote */
    double t95_min_ms, t95_max_ms;
    int    attaques_min, attaques_max;
} sg_cp;

static const sg_cp g_cps[] = {
    /*
     * MESURE : brillance 9,46 fois un bruit blanc, duree utile 4,6 ms pour un
     * fichier de 40, une attaque a 0 ms.
     *
     * Le plafond de duree utile a 12 ms est la borne la plus serree du lot, et
     * c'est voulu : c'est elle qui garantit que le tic reste jouable dix fois de
     * suite. Le plancher de brillance a 5,00 le tient loin de la lame (0,03), et
     * la borne d'attaques a exactement 1 interdit le double declic.
     */
    { "couperet_tic.wav", "le battement des dix dernieres secondes",
      sg_render_cp_tic, SG_CP_TIC_LEN, 0.80f,
      5.00, 0.0,   0.0, 12.0,   1, 1 },

    /*
     * MESURE : brillance 0,03 — quinze fois plus sombre que le coup de poing,
     * qui est deja le son mat de reference a 0,45 — et 451,3 ms de duree utile.
     *
     * Le plafond de brillance a 0,15 est ce qui interdit a la lame de devenir
     * claquante, donc de se rapprocher du tic. Le plancher de duree utile a
     * 300 ms est ce qui interdit qu'elle redevienne un simple choc : ce qu'on
     * veut entendre, c'est la salle qui accuse le coup, et une salle met du
     * temps. Pas de borne d'attaques : voir l'en-tete de section, l'instrument
     * ne s'applique pas a un son bati sur 58 Hz.
     */
    { "couperet_lame.wav", "le contacteur qui tombe",
      sg_render_cp_lame, SG_CP_LAME_LEN, 0.95f,
      0.0, 0.15,   300.0, 0.0,   0, 0 },

    /*
     * MESURE : 201,0 ms de duree utile pour 500 de fichier. La glissade est
     * verifiee a part, plus bas : c'est sa propriete definissante et elle ne
     * tient pas dans une colonne de cette table.
     *
     * Le plafond de duree utile a 300 ms la tient a l'ecart de la lame (451,3) —
     * une coupure qui durerait comme elle serait un second couperet, et le
     * joueur apprendrait le contraire de ce qui vient de se passer. Aucune borne
     * de brillance : ce fichier n'a pas de couleur a tenir, il a une hauteur a
     * perdre.
     */
    { "couperet_coupure.wav", "l'alimentation d'une borne qui tombe",
      sg_render_cp_coupure, SG_CP_COUPURE_LEN, 0.90f,
      0.0, 0.0,   0.0, 300.0,   0, 0 },

    /*
     * MESURE : brillance 1,71 — quatre fois celle du coup de poing (0,45) et
     * au-dessous du jeton refuse (3,08), qui est deja decrit comme « du metal,
     * mais etouffe ». C'est la place qu'on lui cherchait : une plaque d'acier
     * epaisse est plus sombre qu'une piece de deux centimetres et demi. Duree
     * utile 97,1 ms.
     *
     * Les deux bornes disent les deux mots. METALLIQUE par le plancher de
     * brillance a 1,00, qui le tient au-dessus de la lame (0,03), de la coupure
     * (0,02) et du renvoi (0,17). SONNANT par le plancher de duree utile a
     * 50 ms : c'est lui qui compte, parce que sans lui le blindage pourrait
     * redevenir ce qu'il etait dans une premiere version — 13 ms, c'est-a-dire
     * un second tic. Le plafond a 180 ms l'empeche de trainer : une bonne
     * nouvelle est un accuse de reception, pas une annonce.
     */
    { "couperet_blindage.wav", "la plaque qui encaisse",
      sg_render_cp_blindage, SG_CP_BLINDAGE_LEN, 0.88f,
      1.00, 0.0,   50.0, 180.0,   0, 0 },

    /*
     * MESURE : 230,1 ms de duree utile. Comme pour la coupure, c'est la glissade
     * qui le definit et elle est verifiee a part. Les bornes de duree n'ont
     * qu'un role : le tenir entre la coupure (201,0) et la lame (451,3), ou il
     * doit rester.
     */
    { "couperet_renvoi.wav", "la surtension qui repart",
      sg_render_cp_renvoi, SG_CP_RENVOI_LEN, 0.88f,
      0.0, 0.0,   120.0, 320.0,   0, 0 },
};

#define SG_CP_COUNT ((int)(sizeof g_cps / sizeof g_cps[0]))

/*
 * Les rangs dans la table, pour les trois verifications qui ne sont pas des
 * colonnes. Des indices et non des comparaisons de nom de fichier : une chaine
 * qu'on retape est une occasion de divergence de plus, et celle-ci se
 * tromperait en silence.
 */
enum {
    SG_CP_I_TIC = 0,
    SG_CP_I_LAME,
    SG_CP_I_COUPURE,
    SG_CP_I_BLINDAGE,
    SG_CP_I_RENVOI
};
_Static_assert(SG_CP_I_RENVOI + 1 == SG_CP_COUNT,
               "les rangs du couperet ne suivent plus la table");

/*
 * LES QUATRE SEUILS QUI SEPARENT LES SONS LES UNS DES AUTRES.
 *
 * Ils sont ici et pas dans la table parce qu'ils ne portent sur AUCUN fichier
 * en particulier : ce sont des rapports entre deux mesures, et les ecrire dans
 * une ligne reviendrait a dire qu'ils appartiennent a l'un des deux sons.
 *
 * Poses, comme partout dans ce fichier, apres avoir mesure — les valeurs
 * reelles sont dans le journal de `stepgen` et dans les commentaires de la
 * table — et sous les valeurs obtenues, avec la marge qu'il faut pour qu'un
 * reglage de timbre ne casse pas le build sans raison, mais pas plus.
 */
/* Le tic contre la lame, en brillance. MESURE x345 (9,46 contre 0,03). */
#define SG_CP_ECART_SPECTRE 120.0
/* La lame contre le tic, en duree utile. MESURE x98 (451,3 contre 4,6 ms). Les
 * deux bornes de la table imposent deja x25 a elles seules ; ce seuil-ci en
 * demande davantage, sans quoi il ne dirait rien de plus qu'elles. */
#define SG_CP_ECART_DUREE    40.0
/* Les deux glissades. MESURE x645,65 pour le renvoi (0,037 puis 23,589) et
 * x0,08 pour la coupure (0,883 puis 0,071). Les deux seuils sont loin de 1 de
 * part et d'autre : un facteur voisin de 1 voudrait dire que la hauteur ne bouge
 * pas, donc que le fichier ne dit plus ce pour quoi il existe. */
#define SG_CP_MONTEE_MIN     10.00
#define SG_CP_CHUTE_MAX       0.30

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

/*
 * LA BRILLANCE : la puissance d'une bande haute contre une bande basse,
 * RAPPORTEE A CELLE D'UN BRUIT BLANC.
 *
 * La normalisation n'est pas un detail, et elle a coute un seuil faux. Les deux
 * filtres sont a Q constant, donc leurs largeurs sont proportionnelles a leur
 * frequence, et la bande de 2 200 Hz en couvre 4,9 fois plus que celle de 450.
 * Compares bruts, ils annoncaient 2,2 pour le coup de poing — un son qui est en
 * realite DEUX FOIS PLUS SOMBRE que du bruit blanc. C'etait le premier seuil
 * ecrit ici, et il refusait un fichier correct. Divisee par 4,9, la mesure vaut
 * 0,45 et veut enfin dire quelque chose : « moitie moins d'aigu qu'un bruit
 * blanc ».
 *
 * Une seule fonction pour le coup ET pour les trois jetons, et c'est le point :
 * « le jeton est brillant » et « le coup est mat » ne sont deux affirmations
 * comparables que si elles sortent du meme instrument. Deux mesures voisines
 * mais distinctes laisseraient croire a une comparaison qui n'en serait pas
 * une.
 */
#define SG_BRILLANCE_BAS   450.0f
#define SG_BRILLANCE_HAUT 2200.0f

static double sg_brillance(const float *v, size_t n)
{
    sg_svf bas, haut;
    sg_svf_set(&bas,  SG_BRILLANCE_BAS,  2.0f);
    sg_svf_set(&haut, SG_BRILLANCE_HAUT, 2.0f);
    double ebas = 0.0, ehaut = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double a = sg_svf_band(&bas, v[i]);
        const double b = sg_svf_band(&haut, v[i]);
        ebas += a * a; ehaut += b * b;
    }
    /* La reference : le rapport que donnerait du bruit blanc, c'est-a-dire le
     * rapport des largeurs de bande, c'est-a-dire celui des frequences
     * centrales puisque le Q est le meme. */
    const double blanc = (double)SG_BRILLANCE_HAUT / (double)SG_BRILLANCE_BAS;
    return (ebas > 1e-12) ? (ehaut / ebas) / blanc : 1e9;
}

/*
 * LES ATTAQUES : le nombre de fois ou de l'energie ARRIVE dans le fichier.
 *
 * C'est ce qui separe une piece qui tombe d'une piece qui sonne une fois, et
 * c'est donc ce qu'il faut mesurer plutot que promettre — la table des jetons
 * declare des rebonds, ce compte dit s'ils sont la.
 *
 * ON NE COMPTE PAS LES SOMMETS DE L'ENVELOPPE, et l'avoir essaye est ce qui a
 * mene ici : les rebonds se CHEVAUCHENT. Le mode fondamental du jeton insere
 * tient 90 ms et le deuxieme choc arrive au bout de 62 — l'enveloppe n'a donc
 * pas le temps de redescendre entre les deux, elle fait une bosse sur une pente
 * et non un pic isole. Compte ainsi, un jeton parfaitement audible n'a qu'une
 * seule attaque.
 *
 * On compte donc les MONTEES : la variation de l'enveloppe sur deux
 * millisecondes. Un choc etablit la sienne en moins d'une milliseconde, une
 * decroissance ne remonte jamais. Le declencheur est a hysteresis — haut a
 * 15 % de la plus forte montee, bas a 5 % — parce qu'une seule attaque etalee
 * franchirait sinon le seuil plusieurs fois de suite et compterait double.
 */
static int sg_attaques(const float *v, size_t n, float *quand, int cap)
{
    static float env[SG_MAX_FRAMES];
    const size_t w = (size_t)(0.002f * (float)SG_RATE);
    if (n <= w || n > SG_MAX_FRAMES) return 0;

    /* 120 Hz : assez lent pour effacer la porteuse redressee — le mode le plus
     * grave des trois pieces est a 1 550 Hz, donc a 3 100 apres redressement,
     * soit vingt-six fois plus haut — et assez rapide pour suivre un contact
     * d'une demi-milliseconde. */
    sg_pole lp; sg_pole_set(&lp, 120.0f);
    for (size_t i = 0; i < n; ++i) env[i] = sg_lowpass(&lp, fabsf(v[i]));

    float rmax = 0.0f;
    for (size_t i = w; i < n; ++i) {
        const float r = env[i] - env[i - w];
        if (r > rmax) rmax = r;
    }
    if (rmax <= 1e-9f) return 0;

    const float haut = 0.15f * rmax, bas = 0.05f * rmax;
    int count = 0;
    bool dedans = false;
    for (size_t i = w; i < n; ++i) {
        const float r = env[i] - env[i - w];
        if (!dedans && r >= haut) {
            dedans = true;
            if (count < cap) quand[count] = (float)(i - w) / (float)SG_RATE;
            ++count;
        } else if (dedans && r <= bas) {
            dedans = false;
        }
    }
    return count;
}

/*
 * LA DUREE UTILE : l'instant ou 95 % de l'energie du fichier est passee.
 *
 * La longueur d'un fichier ne dit RIEN de la longueur d'un son. Le tic du
 * couperet fait 40 ms de fichier et se tait au bout de six ; la lame fait
 * 1,15 s et sonne presque jusqu'au bout. Ecrire « le tic est court » en
 * verifiant sa duree de fichier reviendrait a verifier ce qu'on a soi-meme
 * ecrit dans une constante, ce qui n'est pas une mesure.
 *
 * 95 % et non 99 : la queue d'une exponentielle n'a pas de fin, et le dernier
 * pour cent d'energie d'un resonateur bien amorti tombe la ou le fondu de
 * sortie l'attrape. Ce qu'on cherche a chiffrer est le moment ou le son est
 * FINI pour l'oreille, pas le moment ou l'echantillon atteint zero.
 */
static double sg_t95(const float *v, size_t n)
{
    double total = 0.0;
    for (size_t i = 0; i < n; ++i) total += (double)v[i] * (double)v[i];
    if (total <= 1e-12) return 0.0;

    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) {
        acc += (double)v[i] * (double)v[i];
        if (acc >= 0.95 * total) return (double)i / (double)SG_RATE;
    }
    return (double)n / (double)SG_RATE;
}

/*
 * LA GLISSE : le rapport de puissance entre 1 200 et 300 Hz, sur une FENETRE.
 *
 * C'est le meme instrument que celui qui verifie le remplissage de la chasse
 * d'eau — deux bandes, leur puissance, deux fenetres — et il sert ici pour la
 * meme raison : mesuree en deux endroits du fichier, cette grandeur dit si la
 * hauteur MONTE ou DESCEND. C'est toute la definition de la coupure (une
 * alimentation qui tombe) et du renvoi (une surtension qui repart), et ces deux
 * fichiers n'ont aucune autre propriete qui les distingue aussi surement.
 *
 * Les bandes ne sont pas celles de `sg_brillance` (450 et 2 200 Hz) et il faut
 * le dire : elles sont posees de part et d'autre des glissades reelles — de 640
 * a 85 Hz pour la coupure, de 110 a 1 500 pour le renvoi. Un instrument de
 * mesure se choisit pour ce qu'on mesure ; celui de la brillance sert a
 * comparer des sons entre eux, celui-ci a comparer un son a lui-meme plus tard.
 *
 * Le filtre est REARME a chaque fenetre : le laisser courir depuis le debut du
 * fichier ferait porter a la seconde fenetre l'energie de la premiere, ce qui
 * est exactement l'ecart qu'on cherche a mesurer.
 */
#define SG_GLISSE_BAS   300.0f
#define SG_GLISSE_HAUT 1200.0f

static double sg_glisse(const float *v, size_t n, double t0, double t1)
{
    size_t a = (size_t)(t0 * (double)SG_RATE);
    size_t b = (size_t)(t1 * (double)SG_RATE);
    if (b > n) b = n;
    if (a >= b) return 0.0;

    sg_svf bas, haut;
    sg_svf_set(&bas,  SG_GLISSE_BAS,  2.0f);
    sg_svf_set(&haut, SG_GLISSE_HAUT, 2.0f);
    double ebas = 0.0, ehaut = 0.0;
    for (size_t i = a; i < b; ++i) {
        const double x = sg_svf_band(&bas,  v[i]);
        const double y = sg_svf_band(&haut, v[i]);
        ebas += x * x; ehaut += y * y;
    }
    return (ebas > 1e-12) ? ehaut / ebas : 1e9;
}

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
            "Produit %d x %d pas (`pas_<materiau>_<n>.wav`), trois boucles\n"
            "d'ambiance (`amb_neon.wav`, `amb_ventilo.wav`, `amb_rue.wav`),\n"
            "la chasse d'eau, le coup sur une borne (`coup_borne.wav`), les\n"
            "%d bruits de jeton (`jeton_insere`, `jeton_refuse`, `jeton_bac`)\n"
            "et les %d bruits du Couperet (`couperet_tic`, `couperet_lame`,\n"
            "`couperet_coupure`, `couperet_blindage`, `couperet_renvoi`),\n"
            "soit %d fichiers en tout.\n",
            argv[0], SG_MATERIAL_COUNT, SG_VARIANTS, SG_COIN_COUNT, SG_CP_COUNT,
            SG_MATERIAL_COUNT * SG_VARIANTS + 3 + 1 + 1 + SG_COIN_COUNT + SG_CP_COUNT);
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

    /* ---- la chasse d'eau ---------------------------------------------------
     * Hors de la table des nappes : elle ne boucle pas, elle dure dix secondes
     * — donc elle ne tient pas dans `SG_MAX_FRAMES` — et il n'y a pas de couture
     * à mesurer. Ce qu'on mesure à la place, c'est la MONTÉE du remplissage :
     * c'est la seule composante qui distingue une chasse d'eau d'un robinet, et
     * l'affirmer sans la vérifier serait exactement le genre de promesse que ce
     * dépôt s'interdit. */
    {
        static float flush[SG_FLUSH_FRAMES];
        const size_t frames = sg_render_flush(flush, SG_FLUSH_FRAMES);
        const float p = peak_of(flush, frames);
        if (p < 1e-6f) tool_fatalf("la chasse d'eau est silencieuse");
        const float scale = 0.88f / p;

        snprintf(path, sizeof path, "%s/chasse_eau.wav", out_dir);
        sg_write_wav(path, flush, frames, scale);

        /*
         * Le rapport de PUISSANCE entre une bande haute et une bande basse, sur
         * deux fenêtres d'une seconde prises dans le remplissage : la première
         * après son attaque, la seconde juste avant la fermeture du flotteur. Si
         * la colonne d'air raccourcit, l'énergie passe de l'une à l'autre.
         *
         * Pourquoi pas les passages par zéro, qui étaient là d'abord
         * ------------------------------------------------------------
         * Parce qu'ils ne pèsent RIEN par l'amplitude : un filet de souffle à
         * 4 kHz posé sur une résonance à 1 kHz ajoute autant de croisements que
         * s'il portait toute l'énergie. Mesurés ainsi, ces dix secondes
         * paraissaient s'assombrir (6 683 puis 5 189 par seconde) alors que le
         * centroïde spectral, lui, montait bel et bien de 856 à 1 513 Hz. Deux
         * bandes et leur puissance disent la même chose que le centroïde, pour
         * deux filtres déjà écrits plus haut.
         *
         * Les fenêtres sont placées APRÈS l'extinction de la vidange — quatre
         * secondes et demie, soit trois constantes de temps et demie de
         * `rush_tau`. Plus tôt, on mesurerait le bruit large de la chasse
         * elle-même, qui est brillant et sans rapport avec ce qu'on vérifie.
         */
        const size_t w = (size_t)SG_RATE;
        const size_t starts[2] = { (size_t)(4.5f * (float)SG_RATE),
                                   (size_t)(8.8f * (float)SG_RATE) };
        double ratio[2] = { 0.0, 0.0 };
        for (int k = 0; k < 2; ++k) {
            sg_svf lo, hi;
            sg_svf_set(&lo,  380.0f, 3.0f);
            sg_svf_set(&hi, 1150.0f, 3.0f);
            double elo = 0.0, ehi = 0.0;
            for (size_t j = starts[k]; j < starts[k] + w && j < frames; ++j) {
                const double a = sg_svf_band(&lo, flush[j]);
                const double b = sg_svf_band(&hi, flush[j]);
                elo += a * a; ehi += b * b;
            }
            ratio[k] = (elo > 1e-12) ? ehi / elo : 0.0;
        }
        tool_infof("chasse « %-15s » : %.1f s, remplissage 1150/380 Hz "
                   "%.3f -> %.3f (x%.1f)",
                   "chasse_eau.wav", (double)frames / (double)SG_RATE,
                   ratio[0], ratio[1],
                   (ratio[0] > 0.0) ? ratio[1] / ratio[0] : 0.0);
        if (ratio[0] <= 0.0 || ratio[1] < ratio[0] * 2.0) {
            tool_fatalf("le remplissage ne monte pas : la chasse ne s'entendra "
                        "pas comme une chasse");
        }
    }

    /* ---- le coup sur une borne ---------------------------------------------
     * Ce qu'on mesure ici, c'est qu'il est COURT et MAT — les deux mots qui le
     * décrivent, et les deux qu'on peut vérifier plutôt que promettre.
     *
     *   COURT : l'énergie des trente premières millisecondes contre celle de la
     *   queue. Un choc a un rapport franc ; un son qui traîne n'est plus un
     *   choc mais une cloche, et c'est exactement ce qu'on obtient si un Q part
     *   à la hausse.
     *
     *   MAT : `sg_brillance`, dont l'en-tête porte la normalisation par le bruit
     *   blanc et le seuil faux qu'elle a corrigé. C'est le seul chiffre de ce
     *   fichier qui se compare d'un son à l'autre — le coup vaut 0,45, les
     *   jetons plus de 1,6 — et il ne le pourrait pas s'il était mesuré deux
     *   fois de deux façons voisines.
     */
    {
        static float coup[SG_MAX_FRAMES];
        const size_t frames = sg_render_coup(coup, SG_MAX_FRAMES);
        const float p = peak_of(coup, frames);
        if (p < 1e-6f) tool_fatalf("le coup sur la borne est silencieux");
        const float scale = 0.92f / p;

        snprintf(path, sizeof path, "%s/coup_borne.wav", out_dir);
        sg_write_wav(path, coup, frames, scale);

        const size_t tete = (size_t)(0.030f * (float)SG_RATE);
        double e_tete = 0.0, e_queue = 0.0;
        for (size_t i = 0; i < frames; ++i) {
            const double v = (double)coup[i];
            if (i < tete) e_tete += v * v; else e_queue += v * v;
        }
        /* Ramenées à la trame : les deux fenêtres n'ont pas la même longueur,
         * et comparer des sommes brutes ferait passer la queue pour l'essentiel
         * du son au seul motif qu'elle est dix-sept fois plus longue. */
        const double d_tete  = (tete > 0) ? e_tete / (double)tete : 0.0;
        const double d_queue = (frames > tete) ? e_queue / (double)(frames - tete) : 0.0;

        /* La MEME mesure que celle des trois jetons, et c'est ce qui rend les
         * deux chiffres comparables : `sg_brillance` porte la normalisation par
         * le bruit blanc et l'histoire du seuil faux qu'elle a corrige. */
        const double mat = sg_brillance(coup, frames);

        tool_infof("coup  « %-15s » : %.0f ms, tête/queue %.0fx, "
                   "aigu %.2f fois celui d'un bruit blanc (mat)",
                   "coup_borne.wav", (double)SG_COUP_LEN * 1000.0,
                   (d_queue > 1e-12) ? d_tete / d_queue : 0.0, mat);

        if (!(d_queue > 1e-12) || d_tete < d_queue * 20.0) {
            tool_fatalf("le coup traîne : c'est une cloche, pas un choc");
        }
        if (mat > 0.60) {
            tool_fatalf("le coup est trop brillant (%.2f fois un bruit blanc) : "
                        "c'est un marteau, pas un poing", mat);
        }
    }

    /* ---- les trois jetons ---------------------------------------------------
     * Normalises CHACUN POUR SOI, comme les nappes et contrairement aux pas.
     * Les trois ne se comparent pas entre eux : ils ne sonnent jamais ensemble,
     * ils sortent de trois endroits differents de la salle, et c'est
     * `room_sound.c` qui decide de leurs niveaux relatifs en sachant ou chacun
     * est place. Les mettre a l'echelle ensemble ne fixerait rien.
     *
     * Ce qu'on verifie, ce sont les DEUX mots qui decrivent un jeton, comme
     * « court » et « mat » decrivent le coup juste au-dessus :
     *
     *   BRILLANT — l'exact contraire du coup de poing. Meme mesure, meme
     *   normalisation, donc les deux chiffres se comparent, et l'ecart est
     *   celui qu'on esperait : le coup vaut 0,45 fois un bruit blanc, le jeton
     *   refuse 3,08, l'insere 13,09 et celui du godet 15,14. Les seuils sont
     *   ecrits APRES la mesure et sous les valeurs reelles — 6,00 pour les deux
     *   pieces qui tombent, 1,60 pour celle qu'on rend —, avec la marge qu'il
     *   faut pour qu'un reglage de timbre ne casse pas le build sans raison,
     *   mais pas plus : au-dela ils ne refuseraient plus rien.
     *
     *   REBONDISSANT — le compte des attaques, borne des DEUX cotes par la
     *   table. Un jeton insere qui n'aurait qu'une attaque ne tomberait pas ;
     *   un jeton refuse qui en aurait trois ne serait plus un refus.
     */
    {
        static float coin[SG_MAX_FRAMES];
        for (int i = 0; i < SG_COIN_COUNT; ++i) {
            const sg_coin *c = &g_coins[i];
            const size_t frames = sg_render_coin(c, coin, SG_MAX_FRAMES);
            const float p = peak_of(coin, frames);
            if (p < 1e-6f) tool_fatalf("« %s » est silencieux", c->file);
            const float scale = c->peak / p;

            snprintf(path, sizeof path, "%s/%s", out_dir, c->file);
            sg_write_wav(path, coin, frames, scale);

            const double brillance = sg_brillance(coin, frames);
            float quand[8];
            const int n = sg_attaques(coin, frames, quand, 8);

            /* Les instants sont dans le journal et pas seulement le compte :
             * c'est ce qui permet de VOIR que l'intervalle raccourcit, ce que
             * le nombre seul ne dit pas. */
            char liste[128]; size_t used = 0; liste[0] = 0;
            for (int k = 0; k < n && k < 8 && used + 8 < sizeof liste; ++k) {
                used += (size_t)snprintf(liste + used, sizeof liste - used,
                                         "%s%.0f", k ? " " : "", (double)quand[k] * 1000.0);
            }
            tool_infof("jeton « %-16s » : %.0f ms, %s, aigu %.2f fois celui d'un "
                       "bruit blanc, %d attaque(s) a %s ms",
                       c->file, (double)c->length * 1000.0, c->quoi,
                       brillance, n, liste);

            if (brillance < c->brillance_min) {
                tool_fatalf("« %s » n'est pas assez brillant (%.2f fois un bruit "
                            "blanc, plancher %.2f) : ce n'est plus du metal",
                            c->file, brillance, c->brillance_min);
            }
            if (n < c->attaques_min || n > c->attaques_max) {
                tool_fatalf("« %s » compte %d attaque(s), attendu %d a %d : "
                            "les rebonds ne sont pas ceux qu'il annonce",
                            c->file, n, c->attaques_min, c->attaques_max);
            }
        }
    }

    /* ---- les cinq bruits du couperet ---------------------------------------
     * Normalises CHACUN POUR SOI, comme les nappes et les jetons : ils ne
     * sonnent jamais ensemble, ils ne sortent pas du meme endroit de la salle,
     * et c'est `room_sound.c` qui decide de leurs niveaux relatifs en sachant ou
     * chacun est place — dont deux qui ne sont pas places du tout.
     *
     * Ce qu'on verifie sort de la table : les bornes de brillance, de duree
     * utile et d'attaques y sont ecrites ligne par ligne, avec la valeur mesuree
     * qui les a fait poser. Restent DEUX verifications qui ne tiennent pas dans
     * une colonne, et ce sont les deux qui comptent le plus.
     */
    {
        static float cp[SG_MAX_FRAMES];
        double brillance[SG_CP_COUNT], utile[SG_CP_COUNT];

        for (int i = 0; i < SG_CP_COUNT; ++i) {
            const sg_cp *c = &g_cps[i];
            const size_t frames = c->render(cp, SG_MAX_FRAMES);
            const float p = peak_of(cp, frames);
            if (p < 1e-6f) tool_fatalf("« %s » est silencieux", c->file);
            const float scale = c->peak / p;

            snprintf(path, sizeof path, "%s/%s", out_dir, c->file);
            sg_write_wav(path, cp, frames, scale);

            brillance[i] = sg_brillance(cp, frames);
            utile[i]     = sg_t95(cp, frames) * 1000.0;

            float quand[8];
            const int n = sg_attaques(cp, frames, quand, 8);
            char liste[128]; size_t used = 0; liste[0] = 0;
            for (int k = 0; k < n && k < 8 && used + 8 < sizeof liste; ++k) {
                used += (size_t)snprintf(liste + used, sizeof liste - used,
                                         "%s%.0f", k ? " " : "", (double)quand[k] * 1000.0);
            }

            tool_infof("couperet « %-22s » : %.0f ms de fichier, %.1f ms utiles, "
                       "aigu %.2f fois celui d'un bruit blanc, %d attaque(s) a %s ms "
                       "— %s",
                       c->file, (double)c->length * 1000.0, utile[i],
                       brillance[i], n, liste, c->quoi);

            if (c->brillance_min > 0.0 && brillance[i] < c->brillance_min) {
                tool_fatalf("« %s » n'est pas assez brillant (%.2f fois un bruit "
                            "blanc, plancher %.2f)", c->file, brillance[i],
                            c->brillance_min);
            }
            if (c->brillance_max > 0.0 && brillance[i] > c->brillance_max) {
                tool_fatalf("« %s » est trop brillant (%.2f fois un bruit blanc, "
                            "plafond %.2f)", c->file, brillance[i], c->brillance_max);
            }
            if (c->t95_min_ms > 0.0 && utile[i] < c->t95_min_ms) {
                tool_fatalf("« %s » ne dure que %.0f ms utiles, plancher %.0f : "
                            "il ne raconte plus rien", c->file, utile[i], c->t95_min_ms);
            }
            if (c->t95_max_ms > 0.0 && utile[i] > c->t95_max_ms) {
                tool_fatalf("« %s » traine %.0f ms utiles, plafond %.0f", c->file,
                            utile[i], c->t95_max_ms);
            }
            /* Un plafond a 0 veut dire « pas de borne », et une seule ligne
             * s'en sert : voir la lame dans la table. */
            if (c->attaques_max > 0 && (n < c->attaques_min || n > c->attaques_max)) {
                tool_fatalf("« %s » compte %d attaque(s), attendu %d a %d", c->file,
                            n, c->attaques_min, c->attaques_max);
            }

            /*
             * LA GLISSADE, pour les deux fichiers dont c'est TOUTE la
             * definition. Deux fenetres, prises dans la partie ou la hauteur
             * bouge encore, et le rapport de l'une a l'autre.
             *
             * La coupure DESCEND : c'est ce qui fait entendre une panne plutot
             * qu'une explosion. Le renvoi MONTE : c'est ce qui le separe de la
             * coupure sans que personne n'ait a l'apprendre, puisque les deux
             * arrivent au meme joueur pour le meme geste et disent le verdict
             * contraire.
             */
            if (i == SG_CP_I_COUPURE || i == SG_CP_I_RENVOI) {
                const bool monte = (i == SG_CP_I_RENVOI);
                const double t0a = monte ? 0.010 : 0.004;
                const double t1a = monte ? 0.060 : 0.050;
                const double t0b = monte ? 0.140 : 0.100;
                const double t1b = monte ? 0.200 : 0.160;
                const double ga = sg_glisse(cp, frames, t0a, t1a);
                const double gb = sg_glisse(cp, frames, t0b, t1b);
                const double f = (ga > 1e-9) ? gb / ga : 0.0;

                /* Le sens annonce est celui qu'on MESURE, pas celui qu'on
                 * attend : la premiere version imprimait l'intention, et un
                 * journal qui affichait « elle DESCEND » sous un facteur de 5,39
                 * disait le contraire de son propre chiffre. */
                tool_infof("couperet « %-22s » : glisse 1200/300 Hz %.3f -> %.3f "
                           "(x%.2f), elle %s — attendu : elle %s",
                           c->file, ga, gb, f,
                           (f > 1.0) ? "MONTE" : "DESCEND",
                           monte ? "MONTE" : "DESCEND");

                /* Les facteurs exiges sont poses sous les valeurs mesurees, et
                 * loin de 1 des deux cotes : un facteur voisin de 1 voudrait
                 * dire que la hauteur ne bouge pas, donc que le fichier ne dit
                 * plus ce pour quoi il existe. */
                if (monte  && f < SG_CP_MONTEE_MIN) {
                    tool_fatalf("« %s » ne monte pas (x%.2f, plancher x%.2f) : "
                                "une surtension qui repart ne descend pas",
                                c->file, f, SG_CP_MONTEE_MIN);
                }
                if (!monte && f > SG_CP_CHUTE_MAX) {
                    tool_fatalf("« %s » ne descend pas (x%.2f, plafond x%.2f) : "
                                "c'est une explosion, pas une panne",
                                c->file, f, SG_CP_CHUTE_MAX);
                }
            }
        }

        /*
         * LE TIC ET LA LAME NE DOIVENT PAS SE CONFONDRE, et c'est la
         * verification la plus importante des cinq fichiers.
         *
         * Ce sont les DEUX SONS DU MEME EVENEMENT a deux instants : le compte a
         * rebours, puis sa fin. S'ils se ressemblent, le joueur n'apprendra
         * jamais lequel veut dire quoi — et il l'apprendra a un moment ou il
         * regarde ailleurs, ce qui est justement la raison d'etre de ces deux
         * fichiers. Une ressemblance ici ne coute pas du confort, elle coute la
         * regle du mode.
         *
         * La mesure imposee est le RAPPORT DE BRILLANCE, mesuree par le meme
         * `sg_brillance` que le coup de poing et les jetons — donc comparable a
         * eux, ce qui est tout l'argument de son en-tete. Les deux sons sont aux
         * deux bouts de l'echelle de la banque : le tic est le plus clair de
         * tous, la lame la plus sombre.
         *
         * La duree utile est verifiee en second, et elle n'est pas redondante :
         * la brillance dit OU est l'energie, `sg_t95` dit COMBIEN DE TEMPS elle
         * dure. Deux sons peuvent partager un spectre et differer par la duree,
         * et c'est meme le cas du tic et du blindage — dont la table s'occupe.
         */
        const double ecart_spectre = (brillance[SG_CP_I_LAME] > 1e-9)
            ? brillance[SG_CP_I_TIC] / brillance[SG_CP_I_LAME] : 1e9;
        const double ecart_duree = (utile[SG_CP_I_TIC] > 1e-9)
            ? utile[SG_CP_I_LAME] / utile[SG_CP_I_TIC] : 1e9;

        tool_infof("couperet : le tic est x%.0f plus brillant que la lame "
                   "(%.2f contre %.2f) et x%.0f plus court (%.1f contre %.0f ms)",
                   ecart_spectre, brillance[SG_CP_I_TIC], brillance[SG_CP_I_LAME],
                   ecart_duree, utile[SG_CP_I_TIC], utile[SG_CP_I_LAME]);

        if (ecart_spectre < SG_CP_ECART_SPECTRE) {
            tool_fatalf("le tic et la lame se ressemblent : x%.0f de brillance "
                        "seulement, plancher x%.0f. Le joueur ne saura pas "
                        "lequel dit quoi", ecart_spectre, SG_CP_ECART_SPECTRE);
        }
        if (ecart_duree < SG_CP_ECART_DUREE) {
            tool_fatalf("le tic et la lame durent presque autant : x%.1f, "
                        "plancher x%.1f", ecart_duree, SG_CP_ECART_DUREE);
        }
    }

    tool_infof("banque écrite dans %s (pic global des pas ramené à %.2f)",
               out_dir, (double)peak_target);
    return 0;
}
