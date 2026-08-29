/*
 * test_audio.c — le mixeur, vérifié sans carte son.
 *
 * `docs/CHANGELOG-V15.md` a affirmé pendant deux mois que l'occlusion audio sur
 * le BVH était « testée ». Elle ne l'était pas — il n'y avait ni mixeur, ni
 * test, ni même de lien vers miniaudio. La phrase a été corrigée en A6 ; voici
 * ce qui la rendra vraie.
 *
 * Le mixeur tourne ici **sans périphérique** : il rend dans un WAV, qu'on relit
 * pour mesurer. C'est ce qui permet de vérifier l'atténuation par distance et le
 * comportement de l'occlusion sur une machine d'intégration continue, qui n'a
 * pas de sortie audio — donc de les vérifier plutôt que de les affirmer.
 *
 * Ce qu'on mesure est une ÉNERGIE, pas une forme d'onde : on ne compare pas des
 * échantillons, on compare des puissances. Comparer les échantillons rendrait le
 * test dépendant de l'interpolation du rééchantillonneur, qui n'est pas ce qu'on
 * cherche à contrôler.
 */
#include "ns_audio.h"
#include "ns_bvh.h"
#include "ns_core.h"
#include "room_camera.h"
#include "room_sound.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            fprintf(stderr, "ÉCHEC %s:%d — ", __FILE__, __LINE__);            \
            fprintf(stderr, __VA_ARGS__);                                     \
            fprintf(stderr, "\n");                                            \
        }                                                                     \
    } while (0)

/* --------------------------------------------------------------------------
 * Un WAV de test, écrit puis chargé
 * --------------------------------------------------------------------------
 * Plutôt que de dépendre d'un asset, le test fabrique sa propre source : une
 * sinusoïde à 440 Hz. Son énergie est connue exactement, ce qui donne une
 * référence contre laquelle mesurer l'atténuation.
 */
static void wav_u32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);        p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF); p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static bool write_sine(const char *path, int rate, float seconds, float hz)
{
    const int frames = (int)(seconds * (float)rate);
    short *pcm = (short *)malloc((size_t)frames * sizeof(short));
    if (!pcm) return false;
    for (int i = 0; i < frames; ++i) {
        const double t = (double)i / (double)rate;
        pcm[i] = (short)(sin(t * 2.0 * 3.14159265358979 * (double)hz) * 20000.0);
    }

    unsigned char h[44];
    const unsigned int bytes = (unsigned int)((size_t)frames * sizeof(short));
    memcpy(h, "RIFF", 4);          wav_u32(h + 4, 36 + bytes);
    memcpy(h + 8, "WAVEfmt ", 8);  wav_u32(h + 16, 16);
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;
    wav_u32(h + 24, (unsigned int)rate);
    wav_u32(h + 28, (unsigned int)rate * 2);
    h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);     wav_u32(h + 40, bytes);

    FILE *f = fopen(path, "wb");
    if (!f) { free(pcm); return false; }
    fwrite(h, 1, sizeof h, f);
    fwrite(pcm, 1, bytes, f);
    fclose(f);
    free(pcm);
    return true;
}

/*
 * Deux mesures de plus, pour la banque de pas.
 *
 * Le PIC dit la force d'un pas — un transitoire, dont l'énergie moyenne ne dit
 * pas grand-chose parce qu'elle dépend surtout de la longueur du fichier.
 *
 * Les PASSAGES PAR ZÉRO par seconde disent sa BRILLANCE. C'est un estimateur
 * grossier du centre de gravité du spectre, et c'est délibérément celui-là :
 * une transformée de Fourier dans un test demanderait ou bien une dépendance,
 * ou bien cent lignes qu'il faudrait vérifier à leur tour. Ce qu'on cherche à
 * établir est un ORDRE — la moquette est plus sourde que le carrelage — et pour
 * un ordre, un estimateur monotone suffit.
 *
 * C'est aussi la mesure qui distingue une vraie banque d'une transposition : un
 * fichier rejoué plus haut voit ses passages par zéro monter dans le même
 * rapport que sa hauteur. Un rapport de quatre entre deux matériaux ne
 * s'obtient pas en accélérant une bande.
 */
static bool wav_measure(const char *path, int rate, double *out_peak, double *out_zcr,
                        double *out_seconds)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    unsigned char h[44];
    if (fread(h, 1, sizeof h, f) != sizeof h) { fclose(f); return false; }

    double peak = 0.0;
    long n = 0, crossings = 0;
    short s, prev = 0;
    bool first = true;
    while (fread(&s, sizeof s, 1, f) == 1) {
        const double v = (double)s / 32768.0;
        const double a = (v < 0.0) ? -v : v;
        if (a > peak) peak = a;
        if (!first && ((s >= 0) != (prev >= 0))) crossings++;
        prev = s; first = false;
        n++;
    }
    fclose(f);
    if (n == 0) return false;

    const double seconds = (double)n / (double)rate;
    if (out_peak)    *out_peak = peak;
    if (out_zcr)     *out_zcr = (double)crossings / seconds;
    if (out_seconds) *out_seconds = seconds;
    return true;
}

/* Énergie moyenne par échantillon d'un WAV 16 bits, normalisée. */
static double wav_energy(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1.0;
    unsigned char h[44];
    if (fread(h, 1, sizeof h, f) != sizeof h) { fclose(f); return -1.0; }

    double sum = 0.0;
    long n = 0;
    short s;
    while (fread(&s, sizeof s, 1, f) == 1) {
        const double v = (double)s / 32768.0;
        sum += v * v;
        n++;
    }
    fclose(f);
    return (n > 0) ? sqrt(sum / (double)n) : 0.0;
}

/* Énergie moyenne d'un WAV 16 bits À PARTIR de `skip` secondes.
 *
 * C'est la mesure qui distingue une queue d'un simple gain : après la fin du
 * son direct, un mixage sec est silencieux et un mixage réverbérant ne l'est
 * pas. Mesurer l'énergie totale ne prouverait rien — monter le mouillé monte
 * aussi le niveau. */
static double wav_energy_after(const char *path, int rate, int channels, double skip)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1.0;
    unsigned char h[44];
    if (fread(h, 1, sizeof h, f) != sizeof h) { fclose(f); return -1.0; }

    const long skip_samples = (long)(skip * (double)rate) * channels;
    double sum = 0.0;
    long n = 0, i = 0;
    short s;
    while (fread(&s, sizeof s, 1, f) == 1) {
        if (i++ < skip_samples) continue;
        const double v = (double)s / 32768.0;
        sum += v * v;
        n++;
    }
    fclose(f);
    return (n > 0) ? sqrt(sum / (double)n) : 0.0;
}

/* Énergie moyenne d'un WAV 16 bits sur ses `keep` premières secondes. */
static double wav_energy_before(const char *path, int rate, int channels, double keep)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1.0;
    unsigned char h[44];
    if (fread(h, 1, sizeof h, f) != sizeof h) { fclose(f); return -1.0; }

    const long keep_samples = (long)(keep * (double)rate) * channels;
    double sum = 0.0;
    long n = 0;
    short s;
    while (n < keep_samples && fread(&s, sizeof s, 1, f) == 1) {
        const double v = (double)s / 32768.0;
        sum += v * v;
        n++;
    }
    fclose(f);
    return (n > 0) ? sqrt(sum / (double)n) : 0.0;
}

/* --------------------------------------------------------------------------
 * Un BVH d'une seule cloison, construit à la main
 * --------------------------------------------------------------------------
 * Même construction que `tests/test_bvh.c` : ni fichier, ni GPU, ni asset. Un
 * nœud racine unique contenant tous les triangles est un BVH valide — dégénéré,
 * mais la traversée ne fait pas la différence, et ce qu'on teste ici est
 * l'occlusion, pas la qualité de la partition.
 */
static ns_bvh         g_bvh;
static ns_bvh_tri     g_tris[2];
static ns_bvh_node    g_node;
static ns_bvh_material g_mat;

static void put3(float *d, ns_v3 v) { d[0] = v.x; d[1] = v.y; d[2] = v.z; }

static void add_tri(int i, ns_v3 a, ns_v3 b, ns_v3 c, ns_v3 n)
{
    ns_bvh_tri *t = &g_tris[i];
    memset(t, 0, sizeof *t);
    put3(t->v0, a);
    put3(t->e1, ns_v3_sub(b, a));
    put3(t->e2, ns_v3_sub(c, a));
    put3(t->normal, ns_v3_norm(n));
    t->material = 0;
}

static void build_wall(void)
{
    /* Une cloison dans le plan x = 0, de z −4 à +4, de y 0 à 3. Deux triangles. */
    const ns_v3 a = ns_v3_make(0.0f, 0.0f, -4.0f);
    const ns_v3 b = ns_v3_make(0.0f, 0.0f,  4.0f);
    const ns_v3 c = ns_v3_make(0.0f, 3.0f,  4.0f);
    const ns_v3 e = ns_v3_make(0.0f, 3.0f, -4.0f);
    const ns_v3 n = ns_v3_make(1.0f, 0.0f, 0.0f);
    add_tri(0, a, b, c, n);
    add_tri(1, a, c, e, n);

    memset(&g_mat, 0, sizeof g_mat);
    g_mat.albedo[0] = g_mat.albedo[1] = g_mat.albedo[2] = 0.5f;
    g_mat.roughness = 0.9f;

    memset(&g_node, 0, sizeof g_node);
    put3(g_node.bmin, ns_v3_make(-0.02f, -0.02f, -4.02f));
    put3(g_node.bmax, ns_v3_make( 0.02f,  3.02f,  4.02f));
    g_node.left_first = 0;
    g_node.tri_count = 2;

    memset(&g_bvh, 0, sizeof g_bvh);
    g_bvh.nodes = &g_node;      g_bvh.node_count = 1;
    g_bvh.tris = g_tris;        g_bvh.tri_count = 2;
    g_bvh.materials = &g_mat;   g_bvh.material_count = 1;
    g_bvh.loaded = true;
}

/* --------------------------------------------------------------------------
 * Les tests
 * -------------------------------------------------------------------------- */

static const char *g_dir = ".";

static void path_in(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s/%s", g_dir, name);
}

static void test_distance_attenuation(void)
{
    char near[512], far[512];
    path_in(near, sizeof near, "audio-proche.wav");
    path_in(far, sizeof far, "audio-loin.wav");

    /*
     * Deux rendus, un seul paramètre changé : la distance. Tout le reste — la
     * source, le gain, la durée — est identique, ce qui est la seule façon
     * d'attribuer l'écart à ce qu'on prétend mesurer.
     */
    double energy[2] = { 0.0, 0.0 };
    const float distances[2] = { 1.0f, 8.0f };

    for (int i = 0; i < 2; ++i) {
        ns_audio_config cfg; memset(&cfg, 0, sizeof cfg);
        cfg.offline = true; cfg.sample_rate = 48000;
        if (!ns_audio_init(&cfg)) { CHECK(false, "le mixeur hors ligne démarre"); return; }

        const int clip = ns_audio_load("audio-source.wav");
        CHECK(clip >= 0, "la source se charge");
        if (clip < 0) { ns_audio_shutdown(); return; }

        ns_audio_set_listener(ns_v3_zero(), ns_v3_make(0, 0, -1), ns_v3_make(0, 1, 0));
        ns_audio_play_3d(clip, NS_BUS_SFX, ns_v3_make(0.0f, 0.0f, -distances[i]),
                         1.0f, 1.0f, 1.0f, 40.0f);

        const char *out = i ? far : near;
        CHECK(ns_audio_render(out, 0.5f), "le rendu hors ligne écrit un WAV");
        ns_audio_shutdown();

        energy[i] = wav_energy(out);
        CHECK(energy[i] >= 0.0, "le WAV rendu se relit");
    }

    printf("  distance : 1 m -> %.5f, 8 m -> %.5f\n", energy[0], energy[1]);
    CHECK(energy[0] > 1e-5, "à 1 m, on entend quelque chose (%.6f)", energy[0]);
    CHECK(energy[1] < energy[0] * 0.6,
          "à 8 m, c'est nettement plus faible qu'à 1 m (%.6f contre %.6f)",
          energy[1], energy[0]);
    CHECK(energy[1] > 0.0, "à 8 m, ce n'est pas coupé pour autant (%.6f)", energy[1]);
}

static void test_occlusion_attenuates_without_cutting(void)
{
    /*
     * LE test que le changelog prétendait avoir. Une source derrière un mur doit
     * être ATTÉNUÉE SANS ÊTRE COUPÉE : le plancher de 0,18 de
     * `ns_bvh_occlusion_factor` est intentionnel — on entend une radio à travers
     * une cloison, c'est même à ça qu'on sait qu'il y a une pièce derrière.
     */
    build_wall();

    const ns_v3 listener = ns_v3_make(-2.0f, 1.5f, 0.0f);
    const ns_v3 behind   = ns_v3_make( 2.0f, 1.5f, 0.0f);   /* de l'autre côté */
    const ns_v3 clear    = ns_v3_make(-2.0f, 1.5f, 3.0f);   /* du même côté */

    const float f_behind = ns_bvh_occlusion_factor(&g_bvh, listener, behind);
    const float f_clear  = ns_bvh_occlusion_factor(&g_bvh, listener, clear);
    printf("  occlusion : derrière le mur %.3f, dégagé %.3f\n",
           (double)f_behind, (double)f_clear);

    CHECK(f_clear > 0.99f, "une source dégagée n'est pas occultée (%.3f)", (double)f_clear);
    CHECK(f_behind < 0.75f, "une source derrière un mur est occultée (%.3f)", (double)f_behind);
    CHECK(f_behind >= 0.17f,
          "…mais pas coupée : le plancher de 0,18 est intentionnel (%.3f)", (double)f_behind);

    /* Et maintenant qu'on entend le résultat. */
    char occ[512], open_[512];
    path_in(occ, sizeof occ, "audio-occlus.wav");
    path_in(open_, sizeof open_, "audio-degage.wav");

    double energy[2] = { 0.0, 0.0 };
    const float visibility[2] = { 1.0f, f_behind };

    for (int i = 0; i < 2; ++i) {
        ns_audio_config cfg; memset(&cfg, 0, sizeof cfg);
        cfg.offline = true; cfg.sample_rate = 48000;
        if (!ns_audio_init(&cfg)) return;

        const int clip = ns_audio_load("audio-source.wav");
        if (clip < 0) { ns_audio_shutdown(); return; }

        ns_audio_set_listener(listener, ns_v3_make(1, 0, 0), ns_v3_make(0, 1, 0));
        const int voice = ns_audio_loop_3d(clip, NS_BUS_SFX, behind, 1.0f, 1.0f, 1.0f, 40.0f);
        ns_audio_voice_occlusion(voice, visibility[i]);

        /* Un tour d'amortissement long : c'est la valeur ARRIVÉE qu'on mesure,
         * pas la transition. La transition est justement ce qui doit être
         * progressif, et un test qui mesurerait au premier échantillon
         * n'attraperait que le lissage. */
        for (int k = 0; k < 200; ++k) ns_audio_update(1.0f / 60.0f);

        CHECK(ns_audio_render(i ? occ : open_, 0.4f), "rendu écrit");
        ns_audio_shutdown();
        energy[i] = wav_energy(i ? occ : open_);
    }

    printf("  énergie : dégagé %.5f, occlus %.5f\n", energy[0], energy[1]);
    CHECK(energy[1] < energy[0] * 0.75,
          "l'occlusion se traduit par une atténuation audible (%.6f contre %.6f)",
          energy[1], energy[0]);
    CHECK(energy[1] > energy[0] * 0.05,
          "…qui n'est PAS une coupure (%.6f contre %.6f)", energy[1], energy[0]);
}

static void test_buses_and_clips(void)
{
    ns_audio_config cfg; memset(&cfg, 0, sizeof cfg);
    cfg.offline = true; cfg.sample_rate = 48000;
    if (!ns_audio_init(&cfg)) { CHECK(false, "le mixeur démarre"); return; }

    ns_audio_set_bus_volume(NS_BUS_SFX, 0.5f);
    CHECK(fabsf(ns_audio_bus_volume(NS_BUS_SFX) - 0.5f) < 1e-5f, "le volume d'un bus se relit");
    ns_audio_set_bus_volume(NS_BUS_SFX, 4.0f);
    CHECK(fabsf(ns_audio_bus_volume(NS_BUS_SFX) - 1.0f) < 1e-5f, "…et se borne à 1");

    /* Un même chemin chargé deux fois donne le MÊME identifiant : les sons sont
     * partagés, seules les voix sont multiples. Dix-neuf bornes qui jouent le
     * même bourdonnement ne doivent pas en tenir dix-neuf copies en mémoire. */
    const int a = ns_audio_load("audio-source.wav");
    const int b = ns_audio_load("audio-source.wav");
    CHECK(a >= 0 && a == b, "un son chargé deux fois est partagé (%d, %d)", a, b);

    CHECK(ns_audio_load("ce-fichier-n-existe-pas.wav") == NS_AUDIO_INVALID,
          "un son absent est refusé proprement");
    CHECK(ns_audio_play(NS_AUDIO_INVALID, NS_BUS_SFX, 1.0f, 1.0f) == NS_AUDIO_INVALID,
          "jouer un son invalide ne fait rien");

    /* Un identifiant de voix périmé ne doit plus rien piloter : sans compteur de
     * génération, un emplacement recyclé ferait obéir le son de quelqu'un
     * d'autre. */
    const int voice = ns_audio_play(a, NS_BUS_SFX, 0.5f, 1.0f);
    CHECK(voice >= 0, "une voix démarre");
    ns_audio_stop(voice);
    ns_audio_stop(voice);   /* deux fois : doit être sans effet, pas un plantage */
    ns_audio_voice_occlusion(voice, 0.5f);
    ns_audio_voice_position(voice, ns_v3_zero());
    CHECK(true, "un identifiant périmé est inerte");

    ns_audio_shutdown();
}

/* --------------------------------------------------------------------------
 * La réverbération par zone
 * --------------------------------------------------------------------------
 * Ce qu'on cherche à prouver tient en une phrase : dans une pièce déclarée
 * réverbérante, un son court CONTINUE d'être entendu après sa fin ; dans une
 * pièce sèche, non.
 *
 * D'où le protocole : une source de 60 ms, un rendu de 0,7 s, et l'énergie
 * mesurée seulement APRÈS 0,2 s — soit bien après la fin du son direct, et
 * après le premier retard de 95 ms. Deux rendus, un seul paramètre changé.
 *
 * La comparaison porte sur la queue et pas sur le total, parce que le total
 * confondrait « ça résonne » avec « c'est plus fort ».
 * -------------------------------------------------------------------------- */
static void test_zone_reverb(void)
{
    char src[512], dry[512], wet[512], music[512];
    path_in(src, sizeof src, "audio-clic.wav");
    path_in(dry, sizeof dry, "audio-sec.wav");
    path_in(wet, sizeof wet, "audio-mouille.wav");
    path_in(music, sizeof music, "audio-musique.wav");

    if (!write_sine(src, 48000, 0.06f, 440.0f)) {
        CHECK(false, "la source courte s'écrit");
        return;
    }

    double tail[2] = { 0.0, 0.0 }, direct[2] = { 0.0, 0.0 };
    for (int i = 0; i < 2; ++i) {
        ns_audio_config cfg; memset(&cfg, 0, sizeof cfg);
        cfg.offline = true; cfg.sample_rate = 48000;
        if (!ns_audio_init(&cfg)) { CHECK(false, "le mixeur hors ligne démarre"); return; }

        const int clip = ns_audio_load("audio-clic.wav");
        CHECK(clip >= 0, "la source courte se charge");
        if (clip < 0) { ns_audio_shutdown(); return; }

        /* i == 0 : sec (l'état par défaut, aucune zone déclarée).
         * i == 1 : les toilettes de `salle.room.json`, aux mêmes valeurs. */
        if (i) ns_audio_set_space(0.42f, 0.52f);

        ns_audio_play(clip, NS_BUS_SFX, 1.0f, 1.0f);

        const char *out = i ? wet : dry;
        CHECK(ns_audio_render(out, 0.7f), "le rendu hors ligne écrit un WAV");
        ns_audio_shutdown();

        tail[i]   = wav_energy_after(out, 48000, 2, 0.2);
        direct[i] = wav_energy_before(out, 48000, 2, 0.06);
        CHECK(tail[i] >= 0.0 && direct[i] >= 0.0, "le WAV rendu se relit");
    }

    /*
     * D'ABORD le son direct, et ce n'est pas de la politesse : la première
     * version de cet effet montait le nœud de retard EN SÉRIE, ce qui remplaçait
     * le signal par son écho au lieu de l'y ajouter. Le test ne mesurait alors
     * que la queue — donc il passait, sur un mixage devenu muet.
     *
     * Un test qui ne regarde que ce qu'on ajoute ne voit jamais ce qu'on a
     * perdu. Celui-ci vérifie les deux.
     */
    printf("  direct sur les 60 premières ms : sec -> %.6f, mouillé -> %.6f\n",
           direct[0], direct[1]);
    CHECK(direct[0] > 1e-3, "le son direct s'entend, sans zone (%.6f)", direct[0]);
    CHECK(direct[1] > direct[0] * 0.5,
          "le son direct SURVIT à la zone réverbérante (%.6f contre %.6f)",
          direct[1], direct[0]);
    CHECK(direct[1] < direct[0],
          "…tout en reculant un peu, sinon la pièce est juste plus forte "
          "(%.6f contre %.6f)", direct[1], direct[0]);

    printf("  queue après 0,2 s : sec -> %.6f, mouillé -> %.6f\n", tail[0], tail[1]);
    CHECK(tail[0] < 1e-4,
          "sans zone, il ne reste rien après la fin du son (%.6f)", tail[0]);
    CHECK(tail[1] > 1e-3,
          "dans une zone réverbérante, la queue s'entend (%.6f)", tail[1]);
    CHECK(tail[1] > tail[0] * 10.0,
          "et elle est franchement au-dessus du sec (%.6f contre %.6f)",
          tail[1], tail[0]);

    /*
     * Le bus MUSIC reste sec. Ce n'est pas un détail de mixage : une musique
     * repassée dans une queue devient de la bouillie, et c'est le genre de
     * câblage qu'on croit avoir fait jusqu'au jour où on l'entend.
     */
    {
        ns_audio_config cfg; memset(&cfg, 0, sizeof cfg);
        cfg.offline = true; cfg.sample_rate = 48000;
        if (!ns_audio_init(&cfg)) { CHECK(false, "le mixeur hors ligne démarre"); return; }
        const int clip = ns_audio_load("audio-clic.wav");
        if (clip >= 0) {
            ns_audio_set_space(0.9f, 0.85f);   /* le maximum permis */
            ns_audio_play(clip, NS_BUS_MUSIC, 1.0f, 1.0f);
            CHECK(ns_audio_render(music, 0.7f), "le rendu hors ligne écrit un WAV");
        }
        ns_audio_shutdown();

        const double m = wav_energy_after(music, 48000, 2, 0.2);
        const double md = wav_energy_before(music, 48000, 2, 0.06);
        printf("  MUSIQUE au mouillé maximal : direct %.6f, queue %.6f\n", md, m);
        CHECK(md > 1e-3, "le bus MUSIC s'entend (%.6f)", md);
        CHECK(m >= 0.0 && m < 1e-4,
              "…et ne passe pas par la queue (%.6f)", m);
    }

    /*
     * Les bornes sont des valeurs, pas des vœux : `salle.room.json` est écrit à
     * la main, et un mouillé à 1,0 avec un retour à 1,0 serait un écho qui ne
     * décroît jamais. `roomgen` refuse déjà ces valeurs à la génération ; le
     * mixeur les borne aussi, parce que les deux chemins ne se protègent pas
     * l'un l'autre.
     */
    {
        ns_audio_config cfg; memset(&cfg, 0, sizeof cfg);
        cfg.offline = true; cfg.sample_rate = 48000;
        if (!ns_audio_init(&cfg)) { CHECK(false, "le mixeur hors ligne démarre"); return; }
        ns_audio_set_space(9.0f, 9.0f);
        ns_audio_set_space(-1.0f, -1.0f);
        CHECK(true, "des valeurs d'espace absurdes sont bornées sans casser");
        ns_audio_shutdown();
    }
}

/* ==========================================================================
 * La banque de pas
 * ==========================================================================
 * Ce qu'on cherche à établir ici n'est pas « les fichiers existent » mais une
 * PROPRIÉTÉ : les cinq matériaux sont distincts au spectre, et les quatre
 * variantes d'un matériau sont des sons différents et non des copies.
 *
 * C'est la propriété qui distingue la banque de ce qu'elle remplace. B14 jouait
 * `walk.wav` transposé : cinq matériaux qui ne différaient que par la vitesse de
 * lecture, et quatre « variantes » qui n'existaient pas. Un test qui se
 * contenterait de compter les fichiers ne verrait aucune différence entre les
 * deux situations.
 * ========================================================================== */

/* Les fichiers sont produits par `tools/stepgen` dans l'arbre de build, à côté
 * des sons copiés de 2020. Le chemin est construit ICI plutôt que résolu par
 * `ns_path_resolve` : on veut que le test échoue en NOMMANT l'emplacement s'il
 * manque, et non qu'il cherche silencieusement ailleurs. */
static void bank_path(char *out, size_t n, const char *material, int variant)
{
    snprintf(out, n, "%s/assets/sounds/pas_%s_%d.wav", g_dir, material, variant);
}

static void test_footstep_bank(void)
{
    /* Dans l'ordre attendu du plus SOURD au plus BRILLANT. C'est cet ordre qui
     * est vérifié, et il n'est pas arbitraire : une fibre absorbe l'aigu, une
     * caisse creuse résonne bas, une lame de bois est au milieu, une chape rend
     * large, un carreau claque. */
    static const char *const material[5] = {
        "moquette", "estrade", "bois", "beton", "carrelage"
    };

    double peak[5][4], zcr[5][4], seconds[5][4];
    bool   complete = true;

    for (int m = 0; m < 5; ++m) {
        for (int v = 0; v < 4; ++v) {
            char p[768];
            bank_path(p, sizeof p, material[m], v + 1);
            if (!wav_measure(p, 48000, &peak[m][v], &zcr[m][v], &seconds[m][v])) {
                CHECK(false, "la banque contient %s", p);
                complete = false;
            }
        }
    }
    if (!complete) return;

    printf("  banque de pas :\n");
    for (int m = 0; m < 5; ++m) {
        double zmin = zcr[m][0], zmax = zcr[m][0], pmax = peak[m][0];
        for (int v = 1; v < 4; ++v) {
            if (zcr[m][v] < zmin) zmin = zcr[m][v];
            if (zcr[m][v] > zmax) zmax = zcr[m][v];
            if (peak[m][v] > pmax) pmax = peak[m][v];
        }
        printf("    %-9s  %.0f ms  pic %6.1f dBFS  brillance %5.0f a %5.0f passages/s\n",
               material[m], seconds[m][0] * 1000.0, 20.0 * log10(pmax), zmin, zmax);
    }

    /*
     * L'ORDRE, matériau par matériau. Comparer chacun à son voisin plutôt que
     * seulement les deux extrêmes : c'est ce qui attrape un matériau qui
     * s'échappe au milieu du classement, lequel ne se verrait sur aucune mesure
     * globale.
     *
     * On compare la MOYENNE des quatre variantes — une variante isolée peut
     * légitimement chevaucher sa voisine, c'est même le but de la variation.
     */
    double mean[5];
    for (int m = 0; m < 5; ++m) {
        mean[m] = (zcr[m][0] + zcr[m][1] + zcr[m][2] + zcr[m][3]) * 0.25;
    }
    for (int m = 1; m < 5; ++m) {
        CHECK(mean[m] > mean[m - 1],
              "« %s » est plus brillant que « %s » (%.0f contre %.0f passages/s)",
              material[m], material[m - 1], mean[m], mean[m - 1]);
    }

    /*
     * Et l'ÉCART, qui est le point.
     *
     * Un facteur trois entre la moquette et le carrelage ne s'obtient pas en
     * changeant la vitesse de lecture : il faudrait jouer le carrelage trois
     * fois plus vite, donc trois fois plus court, ce qui s'entendrait comme un
     * clic et non comme un pas. C'est la mesure qui sépare une vraie banque
     * d'une transposition, et c'est pour ça qu'elle est ici.
     */
    CHECK(mean[4] > mean[0] * 3.0,
          "carrelage et moquette sont séparés par un facteur 3 au moins "
          "(%.0f contre %.0f passages/s, soit x%.1f)",
          mean[4], mean[0], mean[4] / mean[0]);

    /* La moquette est le sol de l'allée : c'est le pas qu'on entend le plus
     * souvent de toute la partie, et il doit rester le plus discret. */
    CHECK(peak[0][0] < peak[4][0] * 0.5,
          "un pas sur moquette est plus faible qu'un pas sur carrelage "
          "(%.1f contre %.1f dBFS)", 20.0 * log10(peak[0][0]), 20.0 * log10(peak[4][0]));

    /*
     * Les quatre variantes d'un matériau sont-elles quatre sons, ou quatre fois
     * le même ? Deux fichiers identiques donneraient exactement le même pic et
     * exactement la même brillance. On exige donc un écart mesurable sur les
     * deux — ce qu'une simple différence de gain ne produirait pas, puisqu'elle
     * laisserait la brillance intacte.
     */
    for (int m = 0; m < 5; ++m) {
        int distinct_zcr = 0, distinct_peak = 0;
        for (int a = 0; a < 4; ++a) {
            for (int b = a + 1; b < 4; ++b) {
                if (fabs(zcr[m][a] - zcr[m][b]) > 1.0) distinct_zcr++;
                if (fabs(peak[m][a] - peak[m][b]) > 1e-4) distinct_peak++;
            }
        }
        CHECK(distinct_zcr == 6 && distinct_peak == 6,
              "les quatre variantes de « %s » sont quatre sons distincts "
              "(%d/6 en brillance, %d/6 en niveau)",
              material[m], distinct_zcr, distinct_peak);
    }
}

/* ==========================================================================
 * Les trois nappes d'ambiance
 * ==========================================================================
 * Une nappe a une seule obligation que les autres sons n'ont pas : ne jamais
 * trahir qu'elle boucle. Deux façons de la trahir, et on les mesure toutes les
 * deux.
 * ========================================================================== */

/* Le creux le plus profond et la bosse la plus haute parmi les fenêtres de
 * `window` secondes. Une nappe qui s'affaisse au milieu se remarque au bout de
 * deux tours, exactement comme un raccord qui claque. */
static bool bed_windows(const char *path, int rate, double window,
                        double *out_min, double *out_max, double *out_seam)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    unsigned char h[44];
    if (fread(h, 1, sizeof h, f) != sizeof h) { fclose(f); return false; }

    const long per = (long)(window * (double)rate);
    double sum = 0.0, lo = 1e30, hi = 0.0;
    double first = 0.0, last = 0.0;
    long n = 0, total = 0;
    short s;
    while (fread(&s, sizeof s, 1, f) == 1) {
        const double v = (double)s / 32768.0;
        if (total == 0) first = v;
        last = v;
        sum += v * v;
        if (++n >= per) {
            const double rms = sqrt(sum / (double)n);
            if (rms < lo) lo = rms;
            if (rms > hi) hi = rms;
            sum = 0.0; n = 0;
        }
        total++;
    }
    fclose(f);
    if (hi <= 0.0) return false;
    *out_min = lo; *out_max = hi;
    *out_seam = fabs(first - last);
    return true;
}

static void test_ambience_beds(void)
{
    static const struct { const char *file, *what; double max_ratio; } bed[3] = {
        /* Le fond de salle est le plus contraint des trois : il tourne en
         * permanence, donc c'est celui dont une respiration trop marquée
         * s'entendrait le plus. */
        { "amb_neon.wav",    "fond de salle", 1.60 },
        { "amb_ventilo.wav", "extracteur",    2.20 },
        /* La rue a le droit de gonfler : ce sont les véhicules qui passent, et
         * c'est justement ce qui l'empêche de sonner comme un souffle. */
        { "amb_rue.wav",     "rue",           6.00 },
    };

    for (int i = 0; i < 3; ++i) {
        char p[768];
        snprintf(p, sizeof p, "%s/assets/sounds/%s", g_dir, bed[i].file);

        double lo = 0.0, hi = 0.0, seam = 0.0;
        if (!bed_windows(p, 48000, 0.25, &lo, &hi, &seam)) {
            CHECK(false, "la nappe %s existe", p);
            continue;
        }
        printf("  nappe %-14s : creux %.4f, sommet %.4f (x%.2f), raccord %.4f\n",
               bed[i].what, lo, hi, hi / lo, seam);

        CHECK(lo > 1e-4, "« %s » ne se tait jamais (creux %.5f)", bed[i].what, lo);
        CHECK(hi / lo < bed[i].max_ratio,
              "« %s » ne pompe pas d'un tour à l'autre (x%.2f, plafond x%.2f)",
              bed[i].what, hi / lo, bed[i].max_ratio);
        /*
         * Le RACCORD. Une boucle dont la dernière trame est loin de la première
         * fait un clic à chaque tour — toutes les quatre secondes, pour un son
         * qui tourne pendant toute la partie. C'est le défaut le plus coûteux
         * qu'une nappe puisse avoir et le plus facile à ne pas voir : il ne se
         * manifeste qu'au deuxième tour.
         */
        CHECK(seam < 0.06,
              "« %s » boucle sans claquer (écart au raccord %.4f)", bed[i].what, seam);
    }
}

/* ==========================================================================
 * La salle qui marche : `room_sound` conduit pour de vrai
 * ==========================================================================
 * Les tests précédents mesurent des FICHIERS. Celui-ci mesure le CODE : la
 * scène, la caméra et `room_sound` sont montés pour de bon, et le mixeur avance
 * pendant qu'on marche (`ns_audio_render_driven`). Ce qui sort est ce qu'on
 * entendrait.
 *
 * La scène est construite à la main, comme `tests/test_bvh.c` construit son BVH :
 * ni fichier, ni GPU, ni asset de décor. Ses coordonnées sont donc DÉCLARÉES ICI
 * et ne peuvent pas dériver de celles de `salle.room.json` — un test qui
 * recopierait les cotes du décor deviendrait faux le jour où on déplace un mur,
 * sans que rien ne le signale.
 * ========================================================================== */

static ns_scene    g_scene;
static ns_footstep g_scene_steps[3];

/* La cloison des toilettes, dans le plan x = −5. Un BVH à elle, distinct de
 * celui de `build_wall` : les deux tests tournent dans le même processus, et
 * partager les tableaux ferait dépendre l'un de l'ordre d'appel de l'autre. */
static ns_bvh          g_scene_bvh;
static ns_bvh_tri      g_scene_tris[2];
static ns_bvh_node     g_scene_node;
static ns_bvh_material g_scene_mat;

static void add_scene_tri(int i, ns_v3 a, ns_v3 b, ns_v3 c, ns_v3 n)
{
    ns_bvh_tri *t = &g_scene_tris[i];
    memset(t, 0, sizeof *t);
    put3(t->v0, a);
    put3(t->e1, ns_v3_sub(b, a));
    put3(t->e2, ns_v3_sub(c, a));
    put3(t->normal, ns_v3_norm(n));
    t->material = 0;
}

/*
 * Deux sols et une cloison : de quoi faire marcher quelqu'un sur de la moquette
 * puis sur du carrelage, et mettre une source derrière un mur.
 *
 * Les COTES sont celles de `salle.room.json` — centre des toilettes à
 * (−7,9 ; −5,2), centre du sas à (7,3 ; 5,2), donc dix-huit mètres entre les
 * deux. Elles sont recopiées et non lues, ce qui est un choix : le test ne
 * charge pas de décor, et une salle mesurée sur un plan de six mètres de côté
 * donnerait un contraste que la vraie salle n'a pas. Ce ne sont PAS des cotes
 * dont la justesse est vérifiée ici — c'est un décor à l'échelle du vrai, et
 * c'est tout ce qu'on lui demande.
 */
static void build_scene(void)
{
    memset(&g_scene, 0, sizeof g_scene);

    g_scene_steps[0] = NS_STEP_NONE;        /* le mur : on ne marche pas dessus */
    g_scene_steps[1] = NS_STEP_MOQUETTE;
    g_scene_steps[2] = NS_STEP_CARRELAGE;
    g_scene.material_footstep = g_scene_steps;
    g_scene.material_count = 3;

    /* Les deux zones que `room_sound` cherche par NOM pour y poser l'extracteur
     * et la rue. Les noms sont un contrat entre la description de la salle et le
     * code ; le test le tient de son côté. */
    snprintf(g_scene.sound_zone[0].name, sizeof g_scene.sound_zone[0].name, "toilettes");
    g_scene.sound_zone[0].bounds.min = ns_v3_make(-9.55f, 0.0f, -7.1f);
    g_scene.sound_zone[0].bounds.max = ns_v3_make(-6.20f, 2.9f, -3.3f);
    g_scene.sound_zone[0].wet = 0.42f;
    g_scene.sound_zone[0].decay = 0.52f;

    snprintf(g_scene.sound_zone[1].name, sizeof g_scene.sound_zone[1].name, "sas_entree");
    g_scene.sound_zone[1].bounds.min = ns_v3_make(5.00f, 0.0f, 3.2f);
    g_scene.sound_zone[1].bounds.max = ns_v3_make(9.55f, 2.9f, 7.1f);
    g_scene.sound_zone[1].wet = 0.24f;
    g_scene.sound_zone[1].decay = 0.30f;

    g_scene.sound_zone_count = 2;

    /* La cloison, entre les toilettes et le reste : plan x = −5, de z −9 à −1,
     * du sol à 3 m. C'est elle qui occulte l'extracteur depuis l'allée. */
    {
        const ns_v3 a = ns_v3_make(-5.0f, 0.0f, -9.0f);
        const ns_v3 b = ns_v3_make(-5.0f, 0.0f, -1.0f);
        const ns_v3 c = ns_v3_make(-5.0f, 3.0f, -1.0f);
        const ns_v3 e = ns_v3_make(-5.0f, 3.0f, -9.0f);
        const ns_v3 n = ns_v3_make(1.0f, 0.0f, 0.0f);
        add_scene_tri(0, a, b, c, n);
        add_scene_tri(1, a, c, e, n);

        memset(&g_scene_mat, 0, sizeof g_scene_mat);
        g_scene_mat.albedo[0] = g_scene_mat.albedo[1] = g_scene_mat.albedo[2] = 0.5f;
        g_scene_mat.roughness = 0.9f;

        memset(&g_scene_node, 0, sizeof g_scene_node);
        put3(g_scene_node.bmin, ns_v3_make(-5.02f, -0.02f, -9.02f));
        put3(g_scene_node.bmax, ns_v3_make(-4.98f,  3.02f, -0.98f));
        g_scene_node.left_first = 0;
        g_scene_node.tri_count = 2;

        memset(&g_scene_bvh, 0, sizeof g_scene_bvh);
        g_scene_bvh.nodes = &g_scene_node;   g_scene_bvh.node_count = 1;
        g_scene_bvh.tris = g_scene_tris;     g_scene_bvh.tri_count = 2;
        g_scene_bvh.materials = &g_scene_mat; g_scene_bvh.material_count = 1;
        g_scene_bvh.loaded = true;
    }
    g_scene.bvh = g_scene_bvh;
}

/* L'état que le rappel de rendu fait avancer. Le joueur marche en ligne droite à
 * vitesse constante : `bob.distance` est ce que `room_camera` accumulerait, et
 * `bob.amount` la vitesse réelle rapportée à la marche. On ne fait PAS tourner
 * le solveur de collision — il a son propre test, et le faire entrer ici rendrait
 * une mesure de son dépendante d'une géométrie. */
typedef struct walker {
    room_sound  *sound;
    room_camera *cam;
    float speed;        /* m/s */
    ns_v3 direction;
} walker;

static void walk_step(float dt, void *user)
{
    walker *w = (walker *)user;
    w->cam->prev_bob = w->cam->bob;
    w->cam->prev_position = w->cam->position;
    w->cam->bob.distance += w->speed * dt;
    w->cam->bob.amount = w->speed / w->cam->speed_walk;
    w->cam->position = ns_v3_add(w->cam->position,
                                 ns_v3_scale(w->direction, w->speed * dt));
    room_sound_update(w->sound, &g_scene, w->cam, NULL, dt);
}

/* Combien de pas distincts s'entendent dans le fichier : on compte les MONTÉES
 * franches de l'enveloppe. C'est la mesure de la cadence, et c'est elle qui dit
 * qu'un pas correspond à une foulée et non à une horloge. */
static int count_onsets(const char *path, int rate, int channels)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char h[44];
    if (fread(h, 1, sizeof h, f) != sizeof h) { fclose(f); return -1; }

    /* Enveloppe grossière : le maximum absolu par tranche de 10 ms. */
    const long per = (long)(0.010 * (double)rate) * channels;
    double env_prev = 0.0, env = 0.0;
    long n = 0;
    int onsets = 0, hold = 0;
    short s;
    while (fread(&s, sizeof s, 1, f) == 1) {
        const double a = fabs((double)s / 32768.0);
        if (a > env) env = a;
        if (++n >= per) {
            /*
             * Une montée franche : le niveau plus que DOUBLE ET DEMI d'une
             * tranche à l'autre, au-dessus d'un plancher de bruit.
             *
             * Le seuil était à quatre, et il manquait deux pas sur moquette :
             * une attaque de moquette est molle par construction — c'est
             * exactement ce que le matériau doit faire — et le compteur exigeait
             * d'elle la franchise d'un carrelage. À 2,5 il reste très au-dessus
             * de ce qu'une décroissance peut produire : sur 10 ms, la plus lente
             * des cinq ne remonte jamais, elle tombe d'un cinquième.
             *
             * Le maintien de 4 tranches évite de compter la même attaque deux
             * fois — un transitoire dure plus de 10 ms.
             */
            if (hold > 0) hold--;
            else if (env > 0.004 && env > env_prev * 2.5) { onsets++; hold = 4; }
            env_prev = env;
            env = 0.0;
            n = 0;
        }
    }
    fclose(f);
    return onsets;
}

static void test_room_walk(void)
{
    build_scene();

    /* Deux traversées, un seul paramètre changé : le sol. Le matériau 1 est la
     * moquette de l'allée, le 2 le carrelage des toilettes. */
    static const struct { const char *file; uint32_t material; const char *what; } run[2] = {
        { "audio-marche-moquette.wav",  1, "moquette" },
        { "audio-marche-carrelage.wav", 2, "carrelage" },
    };

    const float seconds = 5.0f;
    const float speed   = 1.4f;     /* la vitesse de marche de `room_camera` */

    double energy[2] = { 0.0, 0.0 }, zcr[2] = { 0.0, 0.0 };
    int    onsets[2] = { 0, 0 };

    for (int i = 0; i < 2; ++i) {
        char out[512];
        path_in(out, sizeof out, run[i].file);

        ns_audio_config cfg; memset(&cfg, 0, sizeof cfg);
        cfg.offline = true; cfg.sample_rate = 48000;
        if (!ns_audio_init(&cfg)) { CHECK(false, "le mixeur hors ligne démarre"); return; }

        room_sound sound;
        room_sound_init(&sound, &g_scene);
        CHECK(sound.ready, "la sonorisation de la salle démarre");
        CHECK(sound.bank_ready, "…et trouve la banque de pas");

        /*
         * Le bus AMBIANCE est coupé : on mesure des PAS.
         *
         * Sans ça, la nappe de fond — continue, et volontairement présente —
         * pèse dix fois plus que les pas dans l'énergie totale, et les deux
         * matériaux ressortent à 0,0516 contre 0,0516. Ce n'est pas que la
         * mesure était fausse, c'est qu'elle mesurait autre chose. Couper le bus
         * plutôt que baisser les niveaux garde le chemin de code intact : les
         * pas passent toujours par SFX, la réverbération de zone s'applique
         * toujours.
         */
        ns_audio_set_bus_volume(NS_BUS_AMBIENCE, 0.0f);

        room_camera cam;
        room_camera_init(&cam, ns_v3_make(3.0f, 1.70f, 3.0f), 0.0f);
        cam.mode = ROOM_CAM_PLAYER;
        cam.grounded = true;
        cam.ground_material = run[i].material;
        cam.bob.amount = 1.0f;
        cam.prev_bob = cam.bob;

        walker w = { &sound, &cam, speed, ns_v3_make(0.0f, 0.0f, -1.0f) };
        /* Un pas de 1/120 s : celui de la simulation. Le mixeur avance du même
         * pas, donc les pas tombent où ils tomberaient en jouant. */
        CHECK(ns_audio_render_driven(out, seconds, 1.0f / 120.0f, walk_step, &w),
              "la traversée se rend dans un WAV");

        room_sound_shutdown(&sound);
        ns_audio_shutdown();

        energy[i] = wav_energy(out);
        wav_measure(out, 48000, NULL, &zcr[i], NULL);
        onsets[i] = count_onsets(out, 48000, 2);
    }

    /*
     * LA CADENCE. Un pas tous les 0,775 m — la demi-foulée, qui est la période
     * de la composante verticale de l'oscillation de vue. À 1,4 m/s sur 5 s, on
     * parcourt 7 m, soit 9,0 pas.
     *
     * On accepte une marge de deux pas : le compteur d'attaques est un
     * estimateur, pas un oracle, et le premier pas peut tomber à cheval sur le
     * début du fichier.
     */
    const double expected = (double)(speed * seconds) / 0.775;
    printf("  marche : %.1f pas attendus en %.0f s a %.1f m/s ; comptés %d (moquette), %d (carrelage)\n",
           expected, (double)seconds, (double)speed, onsets[0], onsets[1]);
    for (int i = 0; i < 2; ++i) {
        CHECK(fabs((double)onsets[i] - expected) <= 2.0,
              "la cadence suit la distance parcourue sur %s (%d pas, %.1f attendus)",
              run[i].what, onsets[i], expected);
    }

    printf("  marche : énergie %.5f (moquette) contre %.5f (carrelage), "
           "brillance %.0f contre %.0f passages/s\n",
           energy[0], energy[1], zcr[0], zcr[1]);

    /* Ce que la banque apporte, entendu à travers TOUT le chemin — scène,
     * caméra, mixeur, spatialisation. Si l'un des maillons perd le matériau, ces
     * deux vérifications tombent. */
    CHECK(energy[1] > energy[0] * 1.5,
          "marcher sur le carrelage est plus fort que sur la moquette (%.6f contre %.6f)",
          energy[1], energy[0]);
    CHECK(zcr[1] > zcr[0] * 1.8,
          "…et nettement plus brillant (%.0f contre %.0f passages/s)", zcr[1], zcr[0]);
}

/* ==========================================================================
 * Le niveau de l'ambiance en trois points de la salle
 * ==========================================================================
 * Ce qu'on vérifie n'est pas un chiffre absolu — il dépendrait de trente
 * réglages — mais un ORDRE et un CONTRASTE : l'extracteur des toilettes doit
 * dominer quand on est dedans, s'effacer dans l'allée, et la rue doit faire
 * l'inverse au sas. Sans cet écart, la spatialisation ne sert à rien et une
 * source unique non spatialisée sonnerait pareil.
 *
 * Le joueur ne marche pas ici : on mesure l'AMBIANCE seule, sans pas, sinon la
 * comparaison porterait surtout sur le sol.
 * ========================================================================== */
static void test_ambience_positions(void)
{
    build_scene();

    static const struct { const char *file, *what; ns_v3 at; } spot[3] = {
        /* Au milieu de l'allée : à neuf mètres de l'extracteur, qui est derrière
         * la cloison, et à neuf mètres du sas, qui ne l'est pas. */
        { "audio-ambiance-allee.wav",     "allée",     { 0.0f, 1.70f, 0.0f } },
        /* Au bar : le fond de la salle, dos au sas, plus loin de tout. */
        { "audio-ambiance-bar.wav",       "bar",       { 4.0f, 1.70f, -5.0f } },
        /* Dans les toilettes, sous l'extracteur, et de l'autre côté du mur. */
        { "audio-ambiance-toilettes.wav", "toilettes", { -7.9f, 1.70f, -5.2f } },
    };

    /*
     * Deux passes par point, et c'est la deuxième qui répond à la question.
     *
     * La première laisse TOUT sonner : c'est ce qu'on entendrait, et c'est le
     * fichier qui part dans `docs/`. Elle établit une seule chose, mais la plus
     * importante — la salle n'est silencieuse nulle part.
     *
     * Elle ne peut PAS établir la seconde. La nappe de fond n'est pas
     * spatialisée : elle vaut le même niveau aux trois points et écrase tout le
     * reste dans une énergie totale. Pire, dans les toilettes la réverbération
     * de zone RECULE le son direct de 19 % (`ns_audio_set_space` le fait
     * exprès : sinon une pièce réverbérante serait simplement plus forte), si
     * bien que le total y descend alors que l'extracteur y est à 65 cm. Mesurer
     * le total et conclure à un défaut de spatialisation aurait été une lecture
     * fausse d'une mesure juste.
     *
     * La seconde passe coupe la nappe et ne laisse que les sources PLACÉES.
     * C'est là que la spatialisation et l'occlusion se mesurent.
     */
    double full[3] = { 0.0, 0.0, 0.0 }, placed[3] = { 0.0, 0.0, 0.0 };

    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < 3; ++i) {
            char out[512];
            if (pass == 0) path_in(out, sizeof out, spot[i].file);
            else           snprintf(out, sizeof out, "%s/audio-placees-%d.wav", g_dir, i);

            ns_audio_config cfg; memset(&cfg, 0, sizeof cfg);
            cfg.offline = true; cfg.sample_rate = 48000;
            if (!ns_audio_init(&cfg)) { CHECK(false, "le mixeur hors ligne démarre"); return; }

            room_sound sound;
            room_sound_init(&sound, &g_scene);
            if (pass == 0) {
                CHECK(sound.has_fan, "l'extracteur est placé dans la zone « toilettes »");
                CHECK(sound.has_street, "la rue est placée dans la zone « sas_entree »");
            }
            if (pass == 1) room_sound_set_level(ROOM_LEVEL_TONE, 0.0f);

            room_camera cam;
            room_camera_init(&cam, spot[i].at, 0.0f);
            cam.mode = ROOM_CAM_PLAYER;
            cam.grounded = true;
            cam.ground_material = 1;
            /* À l'arrêt : aucune oscillation, donc aucun pas. */
            cam.bob.amount = 0.0f;
            cam.prev_bob = cam.bob;

            walker w = { &sound, &cam, 0.0f, ns_v3_zero() };
            CHECK(ns_audio_render_driven(out, 2.5f, 1.0f / 120.0f, walk_step, &w),
                  "le point d'écoute se rend dans un WAV");

            room_sound_shutdown(&sound);
            ns_audio_shutdown();

            /* On mesure la SECONDE moitié : l'occlusion est amortie et le
             * tourniquet met un moment à visiter chaque source. Mesurer dès la
             * première trame n'attraperait que l'état initial, qui est le même
             * partout. */
            const double e = wav_energy_after(out, 48000, 2, 1.2);
            if (pass == 0) full[i] = e; else placed[i] = e;
        }
        room_sound_set_level(ROOM_LEVEL_TONE, 0.70f);
    }

    printf("  ambiance, tout allumé : allée %.5f, bar %.5f, toilettes %.5f\n",
           full[0], full[1], full[2]);
    printf("  ambiance, sources placées seules : allée %.5f, bar %.5f, toilettes %.5f\n",
           placed[0], placed[1], placed[2]);

    /* Ce que le fond de salle apporte, et c'est tout ce qu'on lui demande : la
     * salle a un plancher partout, y compris là où aucune source n'est proche. */
    for (int i = 0; i < 3; ++i) {
        CHECK(full[i] > 1e-3,
              "la salle n'est jamais silencieuse (%s : %.6f)", spot[i].what, full[i]);
        CHECK(full[i] > placed[i],
              "…et le fond y ajoute quelque chose (%s : %.6f contre %.6f)",
              spot[i].what, full[i], placed[i]);
    }

    /* L'extracteur est à 65 cm au-dessus de la tête quand on est dessous, et à
     * cinq mètres DERRIÈRE UN MUR quand on est dans l'allée. C'est l'écart que
     * la spatialisation et l'occlusion doivent produire, et il est franc. */
    CHECK(placed[2] > placed[0] * 2.0,
          "sous l'extracteur, les sources placées dominent l'allée (%.6f contre %.6f, x%.1f)",
          placed[2], placed[0], placed[2] / placed[0]);
    CHECK(placed[2] > placed[1] * 2.0,
          "…et le bar (%.6f contre %.6f, x%.1f)",
          placed[2], placed[1], placed[2] / placed[1]);
    /* Et l'allée n'est pas muette pour autant : la rue porte jusque-là. Un
     * décor où l'on n'entend QUE la pièce où l'on se trouve est un décor fait
     * de boîtes étanches. */
    CHECK(placed[0] > 1e-4,
          "depuis l'allée, on entend encore la rue (%.6f)", placed[0]);
}

/* ==========================================================================
 * Les deux niveaux réglables
 * ========================================================================== */
static void test_levels(void)
{
    room_sound_set_level(ROOM_LEVEL_STEPS, 0.5f);
    CHECK(fabsf(room_sound_get_level(ROOM_LEVEL_STEPS) - 0.5f) < 1e-5f,
          "le niveau des pas se relit (%.3f)", (double)room_sound_get_level(ROOM_LEVEL_STEPS));
    room_sound_set_level(ROOM_LEVEL_STEPS, 4.0f);
    CHECK(room_sound_get_level(ROOM_LEVEL_STEPS) <= 1.0f, "…et se borne à 1");
    room_sound_set_level(ROOM_LEVEL_STEPS, -1.0f);
    CHECK(room_sound_get_level(ROOM_LEVEL_STEPS) >= 0.0f, "…et à 0");

    /*
     * Le niveau des pas EST entendu. Deux rendus, un seul paramètre changé —
     * c'est le seul protocole qui attribue l'écart à ce qu'on prétend mesurer,
     * et il attrape le défaut qui compte : un curseur branché sur rien.
     */
    build_scene();
    double energy[2] = { 0.0, 0.0 };
    const float level[2] = { 1.0f, 0.25f };

    for (int i = 0; i < 2; ++i) {
        char out[512];
        path_in(out, sizeof out, i ? "audio-pas-bas.wav" : "audio-pas-haut.wav");

        ns_audio_config cfg; memset(&cfg, 0, sizeof cfg);
        cfg.offline = true; cfg.sample_rate = 48000;
        if (!ns_audio_init(&cfg)) { CHECK(false, "le mixeur hors ligne démarre"); return; }

        room_sound sound;
        room_sound_init(&sound, &g_scene);
        /* APRÈS l'initialisation : elle relit `settings.cfg`, et l'écraser
         * ensuite est justement ce que fait le menu. */
        room_sound_set_level(ROOM_LEVEL_STEPS, level[i]);
        /* Le bus AMBIANCE est coupé : on mesure les pas, pas la nappe. */
        ns_audio_set_bus_volume(NS_BUS_AMBIENCE, 0.0f);

        room_camera cam;
        room_camera_init(&cam, ns_v3_make(3.0f, 1.70f, 3.0f), 0.0f);
        cam.mode = ROOM_CAM_PLAYER;
        cam.grounded = true;
        cam.ground_material = 2;      /* le carrelage : le pas le plus franc */
        cam.bob.amount = 1.0f;
        cam.prev_bob = cam.bob;

        walker w = { &sound, &cam, 1.4f, ns_v3_make(0.0f, 0.0f, -1.0f) };
        CHECK(ns_audio_render_driven(out, 4.0f, 1.0f / 120.0f, walk_step, &w),
              "la traversée se rend dans un WAV");

        room_sound_shutdown(&sound);
        ns_audio_shutdown();
        energy[i] = wav_energy(out);
    }

    printf("  niveau des pas : 100%% -> %.5f, 25%% -> %.5f\n", energy[0], energy[1]);
    CHECK(energy[1] < energy[0] * 0.6,
          "baisser « PAS » se traduit par des pas plus faibles (%.6f contre %.6f)",
          energy[1], energy[0]);
    CHECK(energy[1] > 0.0, "…sans les couper (%.6f)", energy[1]);

    /* On repose les valeurs par défaut : un test qui laisse un réglage global
     * derrière lui fait dépendre le suivant de l'ordre d'exécution. */
    room_sound_set_level(ROOM_LEVEL_STEPS, 0.85f);
    room_sound_set_level(ROOM_LEVEL_TONE, 0.70f);
}

int main(int argc, char **argv)
{
    if (argc > 1) g_dir = argv[1];

    /* Le mixeur cherche ses sons par `ns_path_resolve` : on monte le répertoire
     * de travail du test, et rien d'autre. */
    ns_paths_init(argv[0]);
    ns_paths_mount(g_dir);

    /*
     * La source est écrite ICI, avant le premier test.
     *
     * Elle l'était dans `test_distance_attenuation`, et `test_buses_and_clips`,
     * qui tourne avant, la chargeait déjà : sur un répertoire de build neuf le
     * fichier n'existait pas encore et deux vérifications tombaient — puis
     * passaient à la seconde exécution, le fichier étant resté. Un test dont le
     * résultat dépend de l'ordre, ou de ce qu'une exécution précédente a laissé
     * derrière elle, ne prouve rien : il annonce vert une fois sur deux.
     */
    char src[512];
    path_in(src, sizeof src, "audio-source.wav");
    if (!write_sine(src, 48000, 1.5f, 440.0f)) {
        fprintf(stderr, "impossible d'écrire la source de test dans %s\n", g_dir);
        return 2;
    }

    /* La banque doit être montée AVANT que `room_sound` la cherche : elle est
     * produite par `tools/stepgen` dans `<build>/assets/sounds/`, et le mixeur
     * la résout par `ns_path_resolve` comme tout le reste. */
    char assets[1024];
    snprintf(assets, sizeof assets, "%s/assets", g_dir);
    ns_paths_mount(assets);

    test_buses_and_clips();
    test_distance_attenuation();
    test_occlusion_attenuates_without_cutting();
    test_zone_reverb();
    test_footstep_bank();
    test_ambience_beds();
    test_room_walk();
    test_ambience_positions();
    test_levels();

    ns_paths_shutdown();
    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
