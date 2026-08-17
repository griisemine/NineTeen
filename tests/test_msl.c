/*
 * test_msl.c — les emplacements de ressources du MSL, vérifiés sans Mac.
 *
 * Ce que ce test attrape, et que rien d'autre n'attrape
 * -----------------------------------------------------
 * Sur Metal, un shader dont les index [[buffer]] / [[texture]] / [[sampler]] ne
 * correspondent pas à ce que le moteur déclare à SDL ne produit **aucun
 * message** : le pipeline se crée, et le shader lit une ressource qui n'est pas
 * la sienne. On obtient un écran noir, ou pire, une image presque juste. C'est
 * le mode d'échec le plus coûteux du moteur, et il n'est visible que sur du
 * matériel Apple.
 *
 * La correspondance est pourtant entièrement déterminée par deux choses connues
 * ici : la table `ns_shader_table` (ce que le moteur annonce à SDL) et le texte
 * MSL embarqué dans le binaire (ce que le shader attend). Le test confronte les
 * deux, sur n'importe quelle machine, sans GPU.
 *
 * La convention vérifiée est celle de `SDL_gpu.h` :
 *   [[texture]] : textures échantillonnées, puis images de stockage (RO puis RW)
 *   [[sampler]] : un par texture échantillonnée, au même index
 *   [[buffer]]  : blocs uniformes, puis tampons de stockage (RO puis RW)
 *
 * Une égalité **stricte** est exigée entre le nombre d'emplacements du MSL et
 * celui de la table. Un shader qui déclare une ressource sans l'employer la voit
 * supprimée par l'optimiseur SPIR-V : le compte du MSL tombe alors en dessous de
 * celui de la table, et le moteur lie une ressource que personne ne lit. C'est
 * un défaut, pas une tolérance — il s'est produit une fois (l'albédo du G-buffer
 * lié à `raytrace.comp`), et il décalait tous les emplacements suivants.
 */
#include "ns_core.h"
#include "ns_shaders.h"
#include "shader_blobs.h"

#include <stdio.h>
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

/* Recherche d'une sous-chaîne dans un tampon non terminé par zéro. Écrite plutôt
 * qu'empruntée à `memmem`, qui est une extension GNU absente de MSVC. */
static bool contains(const char *hay, size_t hay_len, const char *needle)
{
    const size_t n = strlen(needle);
    if (n == 0 || hay_len < n) return false;
    for (size_t i = 0; i + n <= hay_len; ++i) {
        if (memcmp(hay + i, needle, n) == 0) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ lecture */

static const ns_shader_blob *blob_for(const char *name, unsigned int format)
{
    for (size_t i = 0; i < ns_shader_registry_count; ++i) {
        if (ns_shader_registry[i].format == format
            && strcmp(ns_shader_registry[i].name, name) == 0) {
            return &ns_shader_registry[i];
        }
    }
    return NULL;
}

/*
 * Relève les index employés derrière un attribut Metal, sous forme de masque.
 *
 * Un masque plutôt qu'un compteur : c'est ce qui distingue « 0, 1, 2 » de
 * « 0, 1, 3 ». Le second cas — un trou — est exactement le symptôme d'un
 * décalage d'emplacement, et un simple décompte le laisserait passer.
 */
static uint64_t index_mask(const char *text, size_t len, const char *attribute)
{
    const size_t alen = strlen(attribute);
    uint64_t mask = 0;

    for (size_t i = 0; i + alen < len; ++i) {
        if (memcmp(text + i, attribute, alen) != 0) continue;
        const char *p = text + i + alen;
        if (*p < '0' || *p > '9') continue;
        unsigned value = 0;
        while (*p >= '0' && *p <= '9') { value = value * 10 + (unsigned)(*p - '0'); ++p; }
        if (value < 64) mask |= (uint64_t)1 << value;
        i += alen;
    }
    return mask;
}

/* Le masque attendu pour `count` emplacements : les `count` bits de poids
 * faible, c'est-à-dire 0..count-1 sans trou. */
static uint64_t dense_mask(uint32_t count)
{
    return (count >= 64) ? ~(uint64_t)0 : (((uint64_t)1 << count) - 1);
}

static void describe_mask(uint64_t mask, char *out, size_t out_size)
{
    size_t used = 0;
    out[0] = '\0';
    if (mask == 0) { snprintf(out, out_size, "aucun"); return; }
    for (unsigned i = 0; i < 64 && used + 8 < out_size; ++i) {
        if (mask & ((uint64_t)1 << i)) {
            used += (size_t)snprintf(out + used, out_size - used, "%s%u", used ? "," : "", i);
        }
    }
}

static void check_mask(const char *shader, const char *attribute,
                       uint64_t got, uint32_t expected_count)
{
    const uint64_t want = dense_mask(expected_count);
    if (got == want) { g_checks++; return; }

    char got_s[256], want_s[256];
    describe_mask(got, got_s, sizeof got_s);
    describe_mask(want, want_s, sizeof want_s);
    g_checks++;
    g_failures++;
    fprintf(stderr,
            "ÉCHEC %s : %s — le MSL emploie {%s}, la table de ns_shaders.c en annonce %u {%s}\n"
            "       (un décalage d'emplacement ne produit AUCUN message sur Metal)\n",
            shader, attribute, got_s, expected_count, want_s);
}

/* ------------------------------------------------------------------- main */

int main(void)
{
    printf("Vérification des emplacements MSL (%zu shaders dans la table)\n",
           ns_shader_table_count);

    /* Le registre et la table doivent décrire le même ensemble de shaders. Un
     * shader ajouté dans engine/shaders/ mais absent de la table se traduirait
     * en écran noir au premier pipeline qui le demande. */
    for (size_t i = 0; i < ns_shader_registry_count; ++i) {
        CHECK(ns_shader_info_find(ns_shader_registry[i].name) != NULL,
              "le shader embarqué « %s » n'est pas décrit dans ns_shader_table",
              ns_shader_registry[i].name);
    }

    size_t translated = 0;

    for (size_t i = 0; i < ns_shader_table_count; ++i) {
        const ns_shader_info *info = &ns_shader_table[i];

        /* 16 = SDL_GPU_SHADERFORMAT_MSL. Valeur littérale : le registre généré
         * ne connaît pas les énumérations de SDL. */
        const ns_shader_blob *msl = blob_for(info->name, 16u);
        CHECK(msl != NULL, "aucune variante MSL embarquée pour « %s »", info->name);
        if (!msl) continue;
        translated++;

        const char  *text = (const char *)msl->bytes;
        const size_t len  = (size_t)*msl->len;
        CHECK(len > 64, "%s : MSL suspicieusement court (%zu octets)", info->name, len);

        /* Point d'entrée : SPIRV-Cross renomme `main` en `main0`, et le
         * qualificatif doit correspondre à l'étage sous peine d'un « Creating
         * MTLFunction failed » sans autre indication. */
        const char *qualifier = (info->stage == NS_SHADER_STAGE_VERTEX)   ? "vertex "
                              : (info->stage == NS_SHADER_STAGE_FRAGMENT) ? "fragment "
                                                                          : "kernel ";
        CHECK(contains(text, len, "main0"),
              "%s : point d'entrée « main0 » introuvable", info->name);
        CHECK(contains(text, len, qualifier),
              "%s : qualificatif d'étage « %s» absent du MSL", info->name, qualifier);

        /* Les trois espaces d'index de Metal, chacun dense depuis 0. */
        const uint32_t textures = info->num_samplers
                                + info->num_storage_textures
                                + info->num_rw_storage_textures;
        const uint32_t buffers  = info->num_uniform_buffers
                                + info->num_storage_buffers
                                + info->num_rw_storage_buffers;

        check_mask(info->name, "[[texture]]", index_mask(text, len, "[[texture("), textures);
        check_mask(info->name, "[[sampler]]", index_mask(text, len, "[[sampler("), info->num_samplers);
        check_mask(info->name, "[[buffer]]",  index_mask(text, len, "[[buffer("),  buffers);

        /* Les tampons implicites de SPIRV-Cross (tailles, permutation) ne sont
         * jamais liés par SDL. `spv2msl` les refuse déjà ; on vérifie ici que le
         * texte livré n'en contient effectivement pas. */
        CHECK(!contains(text, len, "spvBufferSize"),
              "%s : le MSL référence un tampon de tailles que SDL ne liera pas", info->name);
        CHECK(!contains(text, len, "spvSwizzle"),
              "%s : le MSL référence un tampon de permutation que SDL ne liera pas", info->name);
    }

    printf("%zu shaders traduits, %d vérifications, %d échec(s)\n",
           translated, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
