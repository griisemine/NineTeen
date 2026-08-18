/* ns_audio.c — voir ns_audio.h pour le raisonnement. */
#include "ns_audio.h"

#include "ns_core.h"

/*
 * miniaudio est une bibliothèque « header-only » : c'est ICI, et nulle part
 * ailleurs, qu'on l'instancie. Les options coupent ce dont on n'a aucun usage —
 * chaque décodeur retiré est du code qu'on ne compile pas et une surface
 * d'attaque de moins sur des fichiers qu'on ne lira jamais.
 */
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_GENERATION
#define MA_NO_ENGINE_AVX2
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <SDL3/SDL.h>
#include <string.h>

#define NS_AUDIO_MAX_CLIPS  64
#define NS_AUDIO_MAX_VOICES 64

/* Vitesse d'amortissement de l'occlusion. `ns_bvh_occlusion_factor` ne rend que
 * quatre valeurs distinctes : sans ce lissage, marcher derrière un pilier fait
 * entendre le monde changer par crans. */
#define NS_AUDIO_OCCLUSION_RATE 6.0f

/* Un mur n'éteint pas une radio, il l'étouffe. Le plancher de la fonction du BVH
 * est 0,18 ; on borne le gain au même endroit, ce qui garde la cohérence entre
 * ce qu'on mesure et ce qu'on entend. */
#define NS_AUDIO_OCCLUSION_FLOOR 0.18f

typedef struct ns_clip {
    char     logical[192];
    float   *frames;        /* mono, à la fréquence du moteur */
    uint64_t frame_count;
    bool     used;
} ns_clip;

typedef struct ns_voice {
    ma_sound            sound;
    ma_audio_buffer_ref ref;
    bool                active;
    bool                spatial;
    bool                looping;
    int                 clip;
    uint32_t            generation;

    float occlusion;        /* valeur amortie, 0..1 */
    float occlusion_target;
    float base_gain;
} ns_voice;

static struct {
    bool           ready;
    bool           offline;
    ma_engine      engine;
    ma_sound_group groups[NS_BUS_COUNT];

    /*
     * L'écho de la pièce, monté en DÉPART/RETOUR — un séparateur qui double le
     * signal, une branche directe vers la sortie, une branche retardée.
     *
     *      SFX ─┐                  ┌─ bus 0 ─────────────────────┐
     *           ├─> reverb_split ──┤                             ├─> endpoint
     *  AMBIENCE ┘                  └─ bus 1 ─> reverb (retard) ──┘
     *
     * Ce montage n'est pas une élégance : c'est ce qu'impose la sémantique
     * réelle de `ma_delay`, que j'avais lue de travers. Son `dry` n'est PAS un
     * passage direct vers la sortie, c'est le gain d'entrée DANS la ligne à
     * retard ; sa sortie vaut exactement `ligne * wet`. Un bus branché en série
     * dessus perd donc son signal direct — et à `wet = 0` il devient muet.
     * Mis en série, le nœud ne réverbérait pas la salle : il la remplaçait par
     * son écho. (`third_party/miniaudio/miniaudio.h:50446-50466`.)
     *
     * La LONGUEUR du retard est fixée une fois pour toutes — `ma_delay_node` la
     * prend à l'initialisation et ne la reprend pas — et ce sont le mouillé et
     * la décroissance qui changent d'une pièce à l'autre. 95 ms, c'est la queue
     * d'une petite pièce carrelée : au-delà on entend deux sons distincts au
     * lieu d'un son qui traîne.
     */
    ma_splitter_node reverb_split;
    ma_delay_node    reverb;
    bool             reverb_ready;
    float            reverb_wet, reverb_decay;
    float          bus_volume[NS_BUS_COUNT];

    ns_clip  clips[NS_AUDIO_MAX_CLIPS];
    ns_voice voices[NS_AUDIO_MAX_VOICES];
    uint32_t generation;
} g;

/* --------------------------------------------------------------------------
 * Identifiants de voix
 * --------------------------------------------------------------------------
 * Un index seul ne suffit pas : une voix recyclée réutilise son emplacement, et
 * un appelant qui aurait gardé l'ancien identifiant piloterait le son de
 * quelqu'un d'autre. On empaquette donc index et génération, et un identifiant
 * périmé ne désigne plus rien — ce qui est exactement ce qu'on veut.
 */
static int voice_handle(int index) { return (int)((g.voices[index].generation << 8) | (uint32_t)index); }

static ns_voice *voice_from_handle(int handle)
{
    if (handle < 0) return NULL;
    const int index = handle & 0xFF;
    if (index < 0 || index >= NS_AUDIO_MAX_VOICES) return NULL;
    ns_voice *v = &g.voices[index];
    if (!v->active || v->generation != ((uint32_t)handle >> 8)) return NULL;
    return v;
}

/* --------------------------------------------------------------------------
 * Cycle de vie
 * -------------------------------------------------------------------------- */

bool ns_audio_ready(void) { return g.ready; }

bool ns_audio_init(const ns_audio_config *cfg)
{
    if (g.ready) return true;
    memset(&g, 0, sizeof g);

    const uint32_t rate = (cfg && cfg->sample_rate) ? cfg->sample_rate : 48000u;
    g.offline = cfg && cfg->offline;

    ma_engine_config ec = ma_engine_config_init();
    ec.sampleRate = rate;
    ec.channels   = 2;
    if (g.offline) {
        /*
         * Sans périphérique, le moteur n'avance que lorsqu'on lui lit des
         * trames. C'est ce qui permet de rendre un mixage dans un WAV sur une
         * machine sans carte son — donc de vérifier l'atténuation et l'occlusion
         * en intégration continue plutôt que de les affirmer.
         */
        ec.noDevice   = MA_TRUE;
        ec.noAutoStart = MA_TRUE;
    }

    if (ma_engine_init(&ec, &g.engine) != MA_SUCCESS) {
        /* Pas fatal, jamais : une machine sans sortie audio doit pouvoir jouer. */
        NS_WARN("audio : aucune sortie disponible, le jeu tournera en silence");
        return false;
    }

    for (int i = 1; i < NS_BUS_COUNT; ++i) {
        if (ma_sound_group_init(&g.engine, 0, NULL, &g.groups[i]) != MA_SUCCESS) {
            NS_WARN("audio : bus %d indisponible", i);
            ma_engine_uninit(&g.engine);
            return false;
        }
    }

    /*
     * Le nœud d'écho, et le re-routage des deux bus qui le traversent.
     *
     * MUSIC ne passe PAS par lui : une musique dans une queue devient de la
     * bouillie. MASTER non plus — les sons joués sans bus sont les alertes et
     * les captures, qui doivent rester nets.
     *
     * S'il échoue, on continue sans : le jeu doit sonner, pas parfaitement.
     */
    {
        ma_node *endpoint = ma_engine_get_endpoint(&g.engine);
        ma_node_graph *graph = ma_engine_get_node_graph(&g.engine);

        const ma_uint32 delay_frames = (ma_uint32)((float)rate * 0.095f);
        ma_delay_node_config dc = ma_delay_node_config_init(2, rate, delay_frames, 0.0f);
        /* `dry` = ce qui ENTRE dans la ligne, `wet` = ce qui en SORT. Le signal
         * direct ne passe pas par ici : il prend la branche 0 du séparateur. */
        dc.delay.dry = 1.0f;
        dc.delay.wet = 0.0f;

        ma_splitter_node_config sc = ma_splitter_node_config_init(2);

        if (ma_splitter_node_init(graph, &sc, NULL, &g.reverb_split) == MA_SUCCESS) {
            if (ma_delay_node_init(graph, &dc, NULL, &g.reverb) == MA_SUCCESS) {
                ma_node_attach_output_bus(&g.reverb_split, 0, endpoint, 0);      /* direct */
                ma_node_attach_output_bus(&g.reverb_split, 1, &g.reverb, 0);     /* départ */
                ma_node_attach_output_bus(&g.reverb, 0, endpoint, 0);            /* retour */

                /* MUSIC ne passe PAS par le départ : une musique dans une queue
                 * devient de la bouillie. MASTER non plus — les sons joués sans
                 * bus sont les alertes et les captures, qui doivent rester nets. */
                ma_node_attach_output_bus(&g.groups[NS_BUS_SFX], 0, &g.reverb_split, 0);
                ma_node_attach_output_bus(&g.groups[NS_BUS_AMBIENCE], 0, &g.reverb_split, 0);
                g.reverb_ready = true;
            } else {
                ma_splitter_node_uninit(&g.reverb_split, NULL);
            }
        }
        if (!g.reverb_ready) {
            NS_WARN("audio : écho de pièce indisponible, le mixage restera sec");
        }
    }

    for (int i = 0; i < NS_BUS_COUNT; ++i) g.bus_volume[i] = 1.0f;
    g.ready = true;

    NS_INFO("audio : %s, %u Hz, 2 canaux", g.offline ? "hors ligne (rendu WAV)" : "périphérique",
            rate);
    return true;
}

void ns_audio_shutdown(void)
{
    if (!g.ready) return;

    for (int i = 0; i < NS_AUDIO_MAX_VOICES; ++i) {
        if (g.voices[i].active) {
            ma_sound_uninit(&g.voices[i].sound);
            g.voices[i].active = false;
        }
    }
    if (g.reverb_ready) {
        ma_delay_node_uninit(&g.reverb, NULL);
        ma_splitter_node_uninit(&g.reverb_split, NULL);
        g.reverb_ready = false;
    }
    for (int i = 1; i < NS_BUS_COUNT; ++i) ma_sound_group_uninit(&g.groups[i]);
    ma_engine_uninit(&g.engine);

    for (int i = 0; i < NS_AUDIO_MAX_CLIPS; ++i) {
        if (g.clips[i].used) SDL_free(g.clips[i].frames);
    }
    memset(&g, 0, sizeof g);
}

void ns_audio_set_bus_volume(ns_audio_bus bus, float volume)
{
    if (bus < 0 || bus >= NS_BUS_COUNT) return;
    g.bus_volume[bus] = ns_clampf(volume, 0.0f, 1.0f);
    if (!g.ready) return;

    if (bus == NS_BUS_MASTER) ma_engine_set_volume(&g.engine, g.bus_volume[bus]);
    else                      ma_sound_group_set_volume(&g.groups[bus], g.bus_volume[bus]);
}

float ns_audio_bus_volume(ns_audio_bus bus)
{
    if (bus < 0 || bus >= NS_BUS_COUNT) return 0.0f;
    return g.bus_volume[bus];
}

/* --------------------------------------------------------------------------
 * Chargement
 * -------------------------------------------------------------------------- */

int ns_audio_load(const char *logical)
{
    if (!g.ready || !logical || !logical[0]) return NS_AUDIO_INVALID;

    for (int i = 0; i < NS_AUDIO_MAX_CLIPS; ++i) {
        if (g.clips[i].used && strcmp(g.clips[i].logical, logical) == 0) return i;
    }

    int slot = -1;
    for (int i = 0; i < NS_AUDIO_MAX_CLIPS; ++i) { if (!g.clips[i].used) { slot = i; break; } }
    if (slot < 0) { NS_WARN("audio : plus d'emplacement pour « %s »", logical); return NS_AUDIO_INVALID; }

    char path[1024];
    if (!ns_path_resolve(logical, path, sizeof path)) {
        NS_WARN("audio : « %s » introuvable", logical);
        return NS_AUDIO_INVALID;
    }

    size_t bytes = 0;
    void *data = SDL_LoadFile(path, &bytes);
    if (!data) { NS_WARN("audio : « %s » illisible", logical); return NS_AUDIO_INVALID; }

    /*
     * Décodage en MONO, à la fréquence du moteur. Le mono n'est pas un
     * appauvrissement : une source stéréo ne se spatialise pas — miniaudio la
     * jouerait telle quelle, et une borne à trois mètres sur la gauche sortirait
     * des deux enceintes. La position vient de la scène, pas du fichier.
     */
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 1,
                                                  ma_engine_get_sample_rate(&g.engine));
    ma_decoder dec;
    if (ma_decoder_init_memory(data, bytes, &dc, &dec) != MA_SUCCESS) {
        NS_WARN("audio : « %s » n'est pas un WAV lisible", logical);
        SDL_free(data);
        return NS_AUDIO_INVALID;
    }

    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &total) != MA_SUCCESS || total == 0) {
        /* Longueur inconnue : on lit par blocs plutôt que de renoncer. */
        total = 0;
        float chunk[4096];
        ma_uint64 got = 0;
        while (ma_decoder_read_pcm_frames(&dec, chunk, 4096, &got) == MA_SUCCESS && got > 0) {
            total += got;
            if (got < 4096) break;
        }
        ma_decoder_seek_to_pcm_frame(&dec, 0);
    }
    if (total == 0) {
        NS_WARN("audio : « %s » est vide", logical);
        ma_decoder_uninit(&dec); SDL_free(data);
        return NS_AUDIO_INVALID;
    }

    float *frames = (float *)SDL_malloc((size_t)total * sizeof(float));
    if (!frames) { ma_decoder_uninit(&dec); SDL_free(data); return NS_AUDIO_INVALID; }

    ma_uint64 read = 0;
    ma_decoder_read_pcm_frames(&dec, frames, total, &read);
    ma_decoder_uninit(&dec);
    SDL_free(data);

    if (read == 0) { SDL_free(frames); return NS_AUDIO_INVALID; }

    ns_clip *c = &g.clips[slot];
    SDL_snprintf(c->logical, sizeof c->logical, "%s", logical);
    c->frames = frames;
    c->frame_count = read;
    c->used = true;

    NS_INFO("audio : « %s » chargé (%.2f s)", logical,
            (double)read / (double)ma_engine_get_sample_rate(&g.engine));
    return slot;
}

/* --------------------------------------------------------------------------
 * Lecture
 * -------------------------------------------------------------------------- */

void ns_audio_set_listener(ns_v3 position, ns_v3 forward, ns_v3 up)
{
    if (!g.ready) return;
    ma_engine_listener_set_position(&g.engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&g.engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&g.engine, 0, up.x, up.y, up.z);
}

static int voice_start(int clip, ns_audio_bus bus, bool spatial, ns_v3 position,
                       float gain, float pitch, float radius, float max_distance,
                       bool looping)
{
    if (!g.ready) return NS_AUDIO_INVALID;
    if (clip < 0 || clip >= NS_AUDIO_MAX_CLIPS || !g.clips[clip].used) return NS_AUDIO_INVALID;
    if (bus < 0 || bus >= NS_BUS_COUNT) bus = NS_BUS_SFX;

    int slot = -1;
    for (int i = 0; i < NS_AUDIO_MAX_VOICES; ++i) { if (!g.voices[i].active) { slot = i; break; } }
    if (slot < 0) return NS_AUDIO_INVALID;   /* saturé : on laisse tomber ce son, pas le jeu */

    ns_voice *v = &g.voices[slot];
    memset(v, 0, sizeof *v);

    const ns_clip *c = &g.clips[clip];
    ma_audio_buffer_ref_init(ma_format_f32, 1, c->frames, c->frame_count, &v->ref);
    /* `ma_audio_buffer_ref_init` laisse la fréquence à zéro : le champ se renseigne
     * directement, il n'existe pas d'accesseur. Elle vaut celle du moteur parce
     * que le décodage l'a déjà imposée — donc aucun rééchantillonnage par voix,
     * ce qui est le but de tout décoder au chargement. */
    v->ref.sampleRate = ma_engine_get_sample_rate(&g.engine);

    ma_uint32 flags = MA_SOUND_FLAG_NO_PITCH * 0;
    if (!spatial) flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;

    ma_sound_group *group = (bus == NS_BUS_MASTER) ? NULL : &g.groups[bus];
    if (ma_sound_init_from_data_source(&g.engine, &v->ref, flags, group, &v->sound) != MA_SUCCESS) {
        return NS_AUDIO_INVALID;
    }

    ma_sound_set_volume(&v->sound, gain);
    ma_sound_set_pitch(&v->sound, (pitch > 0.01f) ? pitch : 1.0f);
    ma_sound_set_looping(&v->sound, looping ? MA_TRUE : MA_FALSE);

    if (spatial) {
        ma_sound_set_position(&v->sound, position.x, position.y, position.z);
        /*
         * Atténuation inverse bornée. `radius` est la distance en deçà de
         * laquelle le son ne grossit plus — sans elle, passer exactement sur une
         * source donnerait un gain infini, ce qui s'entend très bien.
         */
        ma_sound_set_attenuation_model(&v->sound, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(&v->sound, (radius > 0.05f) ? radius : 0.5f);
        ma_sound_set_max_distance(&v->sound, (max_distance > 0.0f) ? max_distance : 30.0f);
        ma_sound_set_rolloff(&v->sound, 1.0f);
    }

    v->active = true;
    v->spatial = spatial;
    v->looping = looping;
    v->clip = clip;
    v->base_gain = gain;
    v->occlusion = v->occlusion_target = 1.0f;
    v->generation = ++g.generation;

    ma_sound_start(&v->sound);
    return voice_handle(slot);
}

int ns_audio_play(int clip, ns_audio_bus bus, float gain, float pitch)
{
    return voice_start(clip, bus, false, ns_v3_zero(), gain, pitch, 0, 0, false);
}

int ns_audio_play_3d(int clip, ns_audio_bus bus, ns_v3 position,
                     float gain, float pitch, float radius, float max_distance)
{
    return voice_start(clip, bus, true, position, gain, pitch, radius, max_distance, false);
}

int ns_audio_loop_3d(int clip, ns_audio_bus bus, ns_v3 position,
                     float gain, float pitch, float radius, float max_distance)
{
    return voice_start(clip, bus, true, position, gain, pitch, radius, max_distance, true);
}

int ns_audio_loop(int clip, ns_audio_bus bus, float gain)
{
    return voice_start(clip, bus, false, ns_v3_zero(), gain, 1.0f, 0, 0, true);
}

void ns_audio_stop(int handle)
{
    ns_voice *v = voice_from_handle(handle);
    if (!v) return;
    ma_sound_stop(&v->sound);
    ma_sound_uninit(&v->sound);
    v->active = false;
}

void ns_audio_voice_position(int handle, ns_v3 position)
{
    ns_voice *v = voice_from_handle(handle);
    if (!v || !v->spatial) return;
    ma_sound_set_position(&v->sound, position.x, position.y, position.z);
}

void ns_audio_voice_occlusion(int handle, float visibility)
{
    ns_voice *v = voice_from_handle(handle);
    if (!v) return;
    v->occlusion_target = ns_clampf(visibility, 0.0f, 1.0f);
}

void ns_audio_set_space(float wet, float decay)
{
    if (!g.ready || !g.reverb_ready) return;

    const float w = ns_clampf(wet, 0.0f, 0.9f);
    const float d = ns_clampf(decay, 0.0f, 0.85f);
    /* Rien à faire si rien ne change : `ma_delay_node_set_*` traverse le
     * verrouillage du graphe, et l'appeler soixante fois par seconde pour la
     * même valeur ne sert qu'à contendre. */
    if (w == g.reverb_wet && d == g.reverb_decay) return;

    g.reverb_wet = w;
    g.reverb_decay = d;

    /* Le RETOUR : niveau du premier écho, puis décroissance à chaque tour de
     * ligne. L'entrée dans la ligne reste à 1 — la doser deux fois ne ferait que
     * rendre le réglage illisible. */
    ma_delay_node_set_wet(&g.reverb, w);
    ma_delay_node_set_decay(&g.reverb, d);

    /* Le DIRECT diminue quand le mouillé monte, sinon une pièce réverbérante est
     * simplement plus forte qu'une pièce sèche — et l'oreille entend un
     * changement de volume, pas un changement d'espace. */
    ma_node_set_output_bus_volume(&g.reverb_split, 0, 1.0f - w * 0.45f);
}

void ns_audio_update(float dt)
{
    if (!g.ready) return;

    for (int i = 0; i < NS_AUDIO_MAX_VOICES; ++i) {
        ns_voice *v = &g.voices[i];
        if (!v->active) continue;

        /* Recyclage : une voix ponctuelle terminée libère son emplacement. */
        if (!v->looping && !ma_sound_is_playing(&v->sound)) {
            ma_sound_uninit(&v->sound);
            v->active = false;
            continue;
        }

        if (v->occlusion != v->occlusion_target) {
            v->occlusion = ns_damp(v->occlusion, v->occlusion_target,
                                   NS_AUDIO_OCCLUSION_RATE, dt);
            const float floor = NS_AUDIO_OCCLUSION_FLOOR;
            const float k = floor + (1.0f - floor) * ns_clampf(v->occlusion, 0.0f, 1.0f);
            ma_sound_set_volume(&v->sound, v->base_gain * k);
        }
    }
}

/* --------------------------------------------------------------------------
 * Rendu hors temps réel
 * -------------------------------------------------------------------------- */

static void wav_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}

bool ns_audio_render(const char *wav_path, float seconds)
{
    if (!g.ready || !g.offline || !wav_path || seconds <= 0.0f) return false;

    const uint32_t rate = ma_engine_get_sample_rate(&g.engine);
    const uint64_t total = (uint64_t)(seconds * (float)rate);
    const uint32_t channels = 2;

    int16_t *pcm = (int16_t *)SDL_malloc((size_t)total * channels * sizeof(int16_t));
    if (!pcm) return false;

    float block[512 * 2];
    uint64_t done = 0;
    while (done < total) {
        const ma_uint64 want = (total - done > 512) ? 512 : (total - done);
        ma_uint64 got = 0;
        if (ma_engine_read_pcm_frames(&g.engine, block, want, &got) != MA_SUCCESS) break;
        if (got == 0) break;

        /* L'amortissement de l'occlusion doit avancer avec le temps rendu, pas
         * avec l'horloge murale — sinon un rendu hors ligne n'entendrait jamais
         * une transition. */
        ns_audio_update((float)got / (float)rate);

        for (ma_uint64 f = 0; f < got * channels; ++f) {
            const float s = ns_clampf(block[f], -1.0f, 1.0f);
            pcm[(done * channels) + f] = (int16_t)(s * 32767.0f);
        }
        done += got;
    }

    /* En-tête WAV PCM 16 bits, écrit à la main : 44 octets, et pas une
     * dépendance de plus pour un fichier de vérification. */
    uint8_t hdr[44];
    const uint32_t data_bytes = (uint32_t)(done * channels * sizeof(int16_t));
    memcpy(hdr, "RIFF", 4);          wav_u32(hdr + 4, 36 + data_bytes);
    memcpy(hdr + 8, "WAVEfmt ", 8);  wav_u32(hdr + 16, 16);
    hdr[20] = 1; hdr[21] = 0;                      /* PCM */
    hdr[22] = (uint8_t)channels; hdr[23] = 0;
    wav_u32(hdr + 24, rate);
    wav_u32(hdr + 28, rate * channels * 2);        /* octets par seconde */
    hdr[32] = (uint8_t)(channels * 2); hdr[33] = 0;
    hdr[34] = 16; hdr[35] = 0;
    memcpy(hdr + 36, "data", 4);     wav_u32(hdr + 40, data_bytes);

    SDL_IOStream *io = SDL_IOFromFile(wav_path, "wb");
    if (!io) { SDL_free(pcm); return false; }
    SDL_WriteIO(io, hdr, sizeof hdr);
    SDL_WriteIO(io, pcm, data_bytes);
    SDL_CloseIO(io);
    SDL_free(pcm);

    NS_INFO("audio : %.2f s rendues dans %s", (double)done / (double)rate, wav_path);
    return true;
}
