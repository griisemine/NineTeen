/*
 * spv2msl.c — SPIR-V -> MSL, aux emplacements de ressources que SDL3 attend.
 *
 * Pourquoi cet outil existe
 * -------------------------
 * Metal ne consomme pas de SPIR-V. Le jeu embarquait pourtant du SPIR-V sur les
 * trois plateformes, et annonçait à SDL savoir produire du MSL : sur un Mac,
 * SDL rendait donc un périphérique Metal parfaitement valide, puis refusait les
 * seize shaders l'un après l'autre. Le jeu n'avait jamais démarré sur macOS.
 *
 * Traduire le SPIR-V est mécanique — SPIRV-Cross le fait. Ce qui ne l'est pas,
 * et qui est la totalité du risque, c'est **l'emplacement des ressources** :
 * Vulkan les désigne par (set, binding), Metal par trois espaces d'index plats
 * ([[buffer]], [[texture]], [[sampler]]). SDL3 impose une correspondance
 * précise entre les deux, documentée dans `SDL_gpu.h` :
 *
 *   SPIR-V, étage graphique       sommet : set 0 ressources, set 1 uniformes
 *                                fragment : set 2 ressources, set 3 uniformes
 *                                dans un set : textures, puis images de
 *                                stockage, puis tampons de stockage
 *   SPIR-V, compute              set 0 lecture seule, set 1 lecture-écriture,
 *                                set 2 uniformes
 *
 *   MSL, étage graphique         [[texture]] : échantillonnées puis stockage
 *                                [[sampler]] : l'index de sa texture
 *                                [[buffer]]  : uniformes puis stockage
 *   MSL, compute                 [[buffer]]  : uniformes, stockage RO, RW
 *                                [[texture]] : RO puis RW
 *
 * Se tromper d'un index ne produit ni erreur de compilation ni message : le
 * pipeline se crée, et le shader lit une ressource qui n'est pas la sienne. Un
 * écran noir, ou pire, une image presque juste. D'où le parti pris de cet
 * outil : il **vérifie** la convention côté SPIR-V avant de traduire, et
 * s'arrête en nommant le shader et la ressource fautive. Une convention non
 * respectée casse le build, elle ne se découvre pas sur un Mac.
 *
 * Pourquoi pas SDL_shadercross, qui fait déjà exactement ça
 * --------------------------------------------------------
 * Son CMake en mode « vendored » exige la présence des sous-modules
 * SPIRV-Cross, SPIRV-Headers, SPIRV-Tools **et** DirectXShaderCompiler — ce
 * dernier vérifié même lorsque la compilation HLSL est désactivée, soit environ
 * un gigaoctet de LLVM pour un compilateur dont ce projet n'a aucun usage. En
 * mode non vendoré il passe par `find_package(spirv_cross_c)`, donc par une
 * installation système. Ni l'un ni l'autre ne tient dans un dépôt qui se
 * reconstruit en une commande sur trois plateformes. On récupère donc
 * SPIRV-Cross seul — dépôt autonome, sans sous-module — et on écrit les trois
 * cents lignes de correspondance, qui sont de toute façon les seules qu'il
 * fallait relire.
 *
 * Usage : spv2msl <entrée.spv> <sortie.msl>
 */

#include "tools_common.h"

#include <spirv_cross_c.h>

/* --------------------------------------------------------------------------
 * Ressources
 * -------------------------------------------------------------------------- */

/* L'ordre de l'énumération EST l'ordre imposé à l'intérieur d'un set par la
 * convention SPIR-V de SDL. On s'en sert pour vérifier, pas seulement pour
 * classer. */
typedef enum res_kind {
    KIND_SAMPLED = 0,        /* sampler2D : une texture ET son échantillonneur */
    KIND_STORAGE_IMAGE = 1,  /* image2D */
    KIND_STORAGE_BUFFER = 2, /* buffer { ... } */
    KIND_UNIFORM = 3,        /* uniform { ... } */
    KIND_COUNT
} res_kind;

static const char *kind_name(res_kind k)
{
    switch (k) {
        case KIND_SAMPLED:        return "texture échantillonnée";
        case KIND_STORAGE_IMAGE:  return "image de stockage";
        case KIND_STORAGE_BUFFER: return "tampon de stockage";
        case KIND_UNIFORM:        return "bloc uniforme";
        default:                  return "?";
    }
}

typedef struct res_entry {
    unsigned    set;
    unsigned    binding;
    res_kind    kind;
    const char *name;
    /* Rempli à l'affectation. ~0u = sans objet pour ce genre de ressource. */
    unsigned    msl_buffer, msl_texture, msl_sampler;
} res_entry;

#define MAX_RES 64

typedef struct res_table {
    res_entry e[MAX_RES];
    size_t    count;
} res_table;

static void res_add(res_table *t, const char *shader, unsigned set, unsigned binding,
                    res_kind kind, const char *name)
{
    if (t->count >= MAX_RES) {
        tool_fatalf("%s : plus de %d ressources — la limite de l'outil, pas celle de SDL",
                    shader, MAX_RES);
    }
    res_entry *e = &t->e[t->count++];
    e->set = set;
    e->binding = binding;
    e->kind = kind;
    e->name = name ? name : "(anonyme)";
    e->msl_buffer = e->msl_texture = e->msl_sampler = ~0u;
}

/* --------------------------------------------------------------------------
 * SPIRV-Cross : garde-fous
 * -------------------------------------------------------------------------- */

static const char *g_shader_name = "?";

static void spvc_error(void *userdata, const char *message)
{
    (void)userdata;
    tool_fatalf("%s : SPIRV-Cross — %s", g_shader_name, message);
}

static void spvc_check(spvc_result r, const char *what)
{
    if (r != SPVC_SUCCESS) {
        tool_fatalf("%s : %s a échoué (code %d)", g_shader_name, what, (int)r);
    }
}

/* --------------------------------------------------------------------------
 * Lecture du binaire SPIR-V
 * -------------------------------------------------------------------------- */

static SpvId *read_spirv(const char *path, size_t *out_words)
{
    FILE *f = fopen(path, "rb");
    if (!f) tool_fatalf("impossible d'ouvrir %s", path);

    if (fseek(f, 0, SEEK_END) != 0) tool_fatalf("%s : fseek", path);
    long size = ftell(f);
    if (size < 0) tool_fatalf("%s : ftell", path);
    rewind(f);

    if (size == 0 || (size % 4) != 0) {
        tool_fatalf("%s : %ld octets — le SPIR-V est un flux de mots de 32 bits", path, size);
    }

    SpvId *words = (SpvId *)malloc((size_t)size);
    if (!words) tool_fatalf("mémoire épuisée pour %s", path);
    if (fread(words, 1, (size_t)size, f) != (size_t)size) tool_fatalf("%s : lecture courte", path);
    fclose(f);

    /* 0x07230203 en tête, ou son boutisme inverse. Vérifié parce qu'un fichier
     * tronqué par un build interrompu produirait sinon une erreur de SPIRV-Cross
     * parlant de jetons, pas de fichier. */
    if (words[0] != 0x07230203u) {
        tool_fatalf("%s : ce n'est pas du SPIR-V (mot magique 0x%08x)", path, words[0]);
    }

    *out_words = (size_t)size / 4;
    return words;
}

/* --------------------------------------------------------------------------
 * Collecte des ressources déclarées
 * -------------------------------------------------------------------------- */

static void collect(spvc_compiler compiler, spvc_resources resources,
                    spvc_resource_type type, res_kind kind, res_table *out)
{
    const spvc_reflected_resource *list = NULL;
    size_t count = 0;
    spvc_check(spvc_resources_get_resource_list_for_type(resources, type, &list, &count),
               "énumération des ressources");

    for (size_t i = 0; i < count; ++i) {
        const unsigned set = spvc_compiler_get_decoration(compiler, list[i].id,
                                                          SpvDecorationDescriptorSet);
        const unsigned binding = spvc_compiler_get_decoration(compiler, list[i].id,
                                                              SpvDecorationBinding);
        res_add(out, g_shader_name, set, binding, kind, list[i].name);
    }
}

/* Les genres de ressources que SDL3 ne sait pas lier. Les refuser ici plutôt
 * que de les traduire : le pipeline se créerait, et la ressource ne serait
 * jamais alimentée. */
static void refuse_unsupported(spvc_compiler compiler, spvc_resources resources,
                               spvc_resource_type type, const char *what)
{
    const spvc_reflected_resource *list = NULL;
    size_t count = 0;
    (void)compiler;
    spvc_check(spvc_resources_get_resource_list_for_type(resources, type, &list, &count),
               "énumération des ressources");
    if (count > 0) {
        tool_fatalf("%s : %s (« %s ») — l'API GPU de SDL3 ne sait pas lier ça ; "
                    "employer un sampler2D, une image2D, un tampon de stockage ou un bloc uniforme",
                    g_shader_name, what, list[0].name ? list[0].name : "?");
    }
}

/* --------------------------------------------------------------------------
 * Vérification de la convention SPIR-V de SDL
 * -------------------------------------------------------------------------- */

/* Dans un set donné : les genres doivent apparaître dans l'ordre de
 * l'énumération, et les bindings être contigus depuis 0. La contiguïté n'est
 * pas un raffinement — SDL lie des tableaux à partir de l'emplacement 0, donc
 * un trou décale tout ce qui suit. */
static void check_set_layout(const res_table *t, unsigned set)
{
    unsigned expected_binding = 0;
    res_kind seen_max = KIND_SAMPLED;
    bool any = false;

    /* Les bindings d'un même set sont parcourus dans l'ordre croissant. */
    for (unsigned b = 0; b < MAX_RES; ++b) {
        const res_entry *found = NULL;
        for (size_t i = 0; i < t->count; ++i) {
            if (t->e[i].set == set && t->e[i].binding == b) {
                if (found) {
                    tool_fatalf("%s : set %u, binding %u déclaré deux fois (« %s » et « %s »)",
                                g_shader_name, set, b, found->name, t->e[i].name);
                }
                found = &t->e[i];
            }
        }
        if (!found) continue;

        if (found->binding != expected_binding) {
            /* Deux causes possibles, et la première est de loin la plus
             * fréquente : une ressource déclarée que le GLSL n'échantillonne
             * jamais est supprimée par l'optimiseur SPIR-V, et laisse un trou
             * ici. Le moteur, lui, continue de la lier — donc tous les
             * emplacements suivants sont décalés d'un cran en MSL. */
            tool_fatalf("%s : set %u — binding %u absent, alors que %u existe.\n"
                        "  Soit la ressource est déclarée sans être employée (l'optimiseur\n"
                        "  l'a retirée) : la supprimer du GLSL *et* décrémenter le compteur\n"
                        "  correspondant dans engine/render/ns_shaders.c.\n"
                        "  Soit la numérotation des bindings saute : SDL lie à partir de\n"
                        "  l'emplacement 0, un trou décale tout ce qui suit.",
                        g_shader_name, set, expected_binding, found->binding);
        }
        if (any && found->kind < seen_max) {
            tool_fatalf("%s : set %u, binding %u — « %s » est un(e) %s après un(e) %s. "
                        "SDL impose l'ordre : textures, images de stockage, tampons de stockage",
                        g_shader_name, set, b, found->name,
                        kind_name(found->kind), kind_name(seen_max));
        }
        if (found->kind > seen_max) seen_max = found->kind;
        any = true;
        expected_binding++;
    }
}

/* --------------------------------------------------------------------------
 * Affectation des index Metal
 * -------------------------------------------------------------------------- */

/* Parcourt les ressources d'un set, d'un genre donné, par binding croissant, et
 * appelle `visit`. Renvoie combien ont été visitées. */
typedef void (*assign_fn)(res_entry *e, unsigned *counter);

static unsigned for_each(res_table *t, unsigned set, res_kind kind,
                         assign_fn visit, unsigned *counter)
{
    unsigned n = 0;
    for (unsigned b = 0; b < MAX_RES; ++b) {
        for (size_t i = 0; i < t->count; ++i) {
            if (t->e[i].set == set && t->e[i].binding == b && t->e[i].kind == kind) {
                visit(&t->e[i], counter);
                n++;
            }
        }
    }
    return n;
}

static void take_buffer(res_entry *e, unsigned *c)  { e->msl_buffer  = (*c)++; }
static void take_storage_image(res_entry *e, unsigned *c) { e->msl_texture = (*c)++; }

/* Un sampler2D GLSL devient en MSL une texture ET un échantillonneur. SDL veut
 * l'échantillonneur au même index que sa texture — « Samplers with indices
 * corresponding to the sampled textures ». */
static void take_sampled(res_entry *e, unsigned *c)
{
    e->msl_texture = *c;
    e->msl_sampler = *c;
    (*c)++;
}

/* --------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: spv2msl <entrée.spv> <sortie.msl>\n");
        return 2;
    }
    const char *in_path = argv[1];
    const char *out_path = argv[2];

    /* Le nom sert à tous les messages : sans lui, une erreur de convention parle
     * d'un binding sans dire dans quel shader. */
    const char *slash = strrchr(in_path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(in_path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    g_shader_name = slash ? slash + 1 : in_path;

    size_t words = 0;
    SpvId *spirv = read_spirv(in_path, &words);

    spvc_context context = NULL;
    spvc_check(spvc_context_create(&context), "création du contexte");
    spvc_context_set_error_callback(context, spvc_error, NULL);

    spvc_parsed_ir ir = NULL;
    spvc_check(spvc_context_parse_spirv(context, spirv, words, &ir), "analyse du SPIR-V");

    spvc_compiler compiler = NULL;
    spvc_check(spvc_context_create_compiler(context, SPVC_BACKEND_MSL, ir,
                                            SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler),
               "création du traducteur MSL");

    const SpvExecutionModel model = spvc_compiler_get_execution_model(compiler);

    /* Quel set porte quoi, selon l'étage. C'est la convention SPIR-V de SDL3,
     * recopiée depuis `SDL_gpu.h`. */
    unsigned set_resources[2];   /* sets contenant textures / images / tampons */
    size_t   set_resources_count = 0;
    unsigned set_uniform = 0;
    const char *stage_word = "?";

    switch (model) {
        case SpvExecutionModelVertex:
            set_resources[0] = 0; set_resources_count = 1; set_uniform = 1;
            stage_word = "vertex";
            break;
        case SpvExecutionModelFragment:
            set_resources[0] = 2; set_resources_count = 1; set_uniform = 3;
            stage_word = "fragment";
            break;
        case SpvExecutionModelGLCompute:
            /* set 0 : lecture seule, set 1 : lecture-écriture. */
            set_resources[0] = 0; set_resources[1] = 1; set_resources_count = 2;
            set_uniform = 2;
            stage_word = "kernel";
            break;
        default:
            tool_fatalf("%s : étage SPIR-V %d non pris en charge (ni sommet, ni fragment, ni compute)",
                        g_shader_name, (int)model);
            return 1;
    }

    spvc_resources resources = NULL;
    spvc_check(spvc_compiler_create_shader_resources(compiler, &resources),
               "réflexion des ressources");

    refuse_unsupported(compiler, resources, SPVC_RESOURCE_TYPE_PUSH_CONSTANT,
                       "constante de poussée");
    refuse_unsupported(compiler, resources, SPVC_RESOURCE_TYPE_SUBPASS_INPUT,
                       "entrée de sous-passe");
    refuse_unsupported(compiler, resources, SPVC_RESOURCE_TYPE_ATOMIC_COUNTER,
                       "compteur atomique");
    refuse_unsupported(compiler, resources, SPVC_RESOURCE_TYPE_SEPARATE_IMAGE,
                       "texture séparée de son échantillonneur");
    refuse_unsupported(compiler, resources, SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS,
                       "échantillonneur séparé de sa texture");
    refuse_unsupported(compiler, resources, SPVC_RESOURCE_TYPE_ACCELERATION_STRUCTURE,
                       "structure d'accélération");

    res_table table;
    table.count = 0;
    collect(compiler, resources, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE,  KIND_SAMPLED,        &table);
    collect(compiler, resources, SPVC_RESOURCE_TYPE_STORAGE_IMAGE,  KIND_STORAGE_IMAGE,  &table);
    collect(compiler, resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, KIND_STORAGE_BUFFER, &table);
    collect(compiler, resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, KIND_UNIFORM,        &table);

    /* Aucune ressource ne doit vivre en dehors des sets prévus : un set 4, ou un
     * bloc uniforme rangé dans le set des textures, ne serait jamais lié. */
    for (size_t i = 0; i < table.count; ++i) {
        const res_entry *e = &table.e[i];
        bool ok = false;
        if (e->kind == KIND_UNIFORM) {
            ok = (e->set == set_uniform);
        } else {
            for (size_t s = 0; s < set_resources_count; ++s) {
                if (e->set == set_resources[s]) ok = true;
            }
        }
        if (!ok) {
            tool_fatalf("%s (%s) : « %s » est un(e) %s dans le set %u — attendu : %s",
                        g_shader_name, stage_word, e->name, kind_name(e->kind), e->set,
                        e->kind == KIND_UNIFORM ? "le set des uniformes"
                                                : "un set de ressources");
        }
    }

    check_set_layout(&table, set_uniform);
    for (size_t s = 0; s < set_resources_count; ++s) {
        check_set_layout(&table, set_resources[s]);
    }

    /* --- Les index Metal, dans l'ordre exact que SDL attend --- */
    unsigned next_buffer = 0, next_texture = 0;

    /* [[buffer]] : uniformes d'abord... */
    for_each(&table, set_uniform, KIND_UNIFORM, take_buffer, &next_buffer);
    /* ...puis les tampons de stockage, set par set (RO avant RW en compute). */
    for (size_t s = 0; s < set_resources_count; ++s) {
        for_each(&table, set_resources[s], KIND_STORAGE_BUFFER, take_buffer, &next_buffer);
    }

    /* [[texture]] : échantillonnées d'abord, puis les images de stockage. */
    for (size_t s = 0; s < set_resources_count; ++s) {
        for_each(&table, set_resources[s], KIND_SAMPLED, take_sampled, &next_texture);
    }
    for (size_t s = 0; s < set_resources_count; ++s) {
        for_each(&table, set_resources[s], KIND_STORAGE_IMAGE, take_storage_image, &next_texture);
    }

    for (size_t i = 0; i < table.count; ++i) {
        const res_entry *e = &table.e[i];
        spvc_msl_resource_binding b;
        spvc_msl_resource_binding_init(&b);
        b.stage = model;
        b.desc_set = e->set;
        b.binding = e->binding;
        if (e->msl_buffer  != ~0u) b.msl_buffer  = e->msl_buffer;
        if (e->msl_texture != ~0u) b.msl_texture = e->msl_texture;
        if (e->msl_sampler != ~0u) b.msl_sampler = e->msl_sampler;
        spvc_check(spvc_compiler_msl_add_resource_binding(compiler, &b),
                   "affectation d'un emplacement Metal");
    }

    /* --- Options de traduction --- */
    spvc_compiler_options options = NULL;
    spvc_check(spvc_compiler_create_compiler_options(compiler, &options), "options");

    /* Metal 2.0 : disponible depuis macOS 10.13, très en dessous de notre cible
     * de déploiement (11.0), et suffisant pour tout ce qu'on écrit. */
    spvc_check(spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_VERSION,
                                              SPVC_MAKE_MSL_VERSION(2, 0, 0)), "version MSL");
    spvc_check(spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_PLATFORM,
                                              SPVC_MSL_PLATFORM_MACOS), "plateforme MSL");
    /* Les tampons d'arguments regrouperaient les ressources en une structure
     * unique — ce n'est pas ce que SDL lie. */
    spvc_check(spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_MSL_ARGUMENT_BUFFERS,
                                              SPVC_FALSE), "tampons d'arguments");
    /* On impose les emplacements ci-dessus ; ne pas laisser le binding SPIR-V
     * servir d'index Metal par défaut, ce qui donnerait un autre résultat. */
    spvc_check(spvc_compiler_options_set_bool(options,
                                              SPVC_COMPILER_OPTION_MSL_ENABLE_DECORATION_BINDING,
                                              SPVC_FALSE), "index depuis le binding");
    spvc_check(spvc_compiler_install_compiler_options(compiler, options), "installation des options");

    const char *msl = NULL;
    spvc_check(spvc_compiler_compile(compiler, &msl), "traduction en MSL");
    if (!msl || !*msl) tool_fatalf("%s : le traducteur a produit un texte vide", g_shader_name);

    /*
     * Les tampons implicites : la vraie chausse-trape.
     *
     * Certaines constructions GLSL n'ont pas d'équivalent Metal et sont
     * traduites par un tampon *supplémentaire* que le traducteur s'attend à voir
     * lié — `arr.length()` en est le cas type. SDL ne lie que ce que le shader
     * déclare, donc ce tampon-là resterait vide : le shader tournerait en lisant
     * des zéros, sans erreur, sans message. On refuse plutôt que de livrer ça.
     */
    struct { spvc_bool needed; const char *what; const char *cause; } implicit[] = {
        { spvc_compiler_msl_needs_buffer_size_buffer(compiler), "tampon de tailles",
          "un `.length()` sur un tableau de taille variable — passer le compte par un uniforme" },
        { spvc_compiler_msl_needs_swizzle_buffer(compiler), "tampon de permutation",
          "un échantillonnage qui demande un remaniement de composantes" },
        { spvc_compiler_msl_needs_output_buffer(compiler), "tampon de sortie",
          "une sortie de shader que Metal ne sait pas exprimer directement" },
        { spvc_compiler_msl_needs_patch_output_buffer(compiler), "tampon de sortie de patch",
          "de la tessellation" },
        { spvc_compiler_msl_needs_input_threadgroup_mem(compiler), "mémoire de groupe en entrée",
          "de la tessellation" },
    };
    for (size_t i = 0; i < sizeof implicit / sizeof implicit[0]; ++i) {
        if (implicit[i].needed) {
            tool_fatalf("%s : la traduction réclame un %s que SDL ne liera jamais.\n"
                        "  Cause probable : %s.\n"
                        "  Le shader se compilerait et lirait dans le vide sur Metal.",
                        g_shader_name, implicit[i].what, implicit[i].cause);
        }
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) tool_fatalf("impossible d'écrire %s", out_path);
    const size_t len = strlen(msl);
    if (fwrite(msl, 1, len, out) != len) tool_fatalf("%s : écriture incomplète", out_path);
    fclose(out);

    spvc_context_destroy(context);
    free(spirv);
    return 0;
}
