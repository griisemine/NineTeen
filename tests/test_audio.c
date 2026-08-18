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

    test_buses_and_clips();
    test_distance_attenuation();
    test_occlusion_attenuates_without_cutting();
    test_zone_reverb();

    ns_paths_shutdown();
    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
