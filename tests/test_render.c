/*
 * test_render.c — vérification de bout en bout de la chaîne GPU, sans écran.
 *
 * Crée un périphérique GPU, une cible hors écran, y dessine un dégradé via le
 * triangle plein écran, relit le résultat et l'écrit en PNG. Le test échoue si
 * l'image obtenue ne correspond pas à ce que le shader est censé produire.
 *
 * C'est la brique qui rend le rendu vérifiable en intégration continue : sur un
 * agent sans carte graphique, Vulkan passe par le rasteriseur logiciel lavapipe.
 */
#include "ns_rhi.h"

#include <SDL3/SDL.h>
#include <stdio.h>

/* L'implémentation de stb_image vit déjà dans le moteur, compilée sans accès
 * direct aux fichiers ; on ne prend ici que les prototypes et on lit soi-même. */
#define STBI_NO_STDIO
#include "stb_image.h"

static int g_failures;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("  ÉCHEC %s:%d — ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

#define TEST_W 320u
#define TEST_H 180u

int main(int argc, char **argv)
{
    const char *out_path = (argc > 1) ? argv[1] : "test-render.png";

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL_Init : %s\n", SDL_GetError());
        return 1;
    }
    ns_log_set_level(NS_LOG_INFO);
    ns_paths_init(argv[0]);

    ns_rhi_desc desc;
    SDL_zero(desc);
    desc.window_title = "Nineteen — test de rendu";
    desc.width  = (int)TEST_W;
    desc.height = (int)TEST_H;
    desc.headless = true;
    desc.vsync = false;

    ns_rhi *rhi = ns_rhi_create(&desc);
    if (!rhi) {
        /* Sur une machine sans pilote Vulkan du tout, le test ne peut rien
         * conclure : on le signale clairement plutôt que d'échouer à tort. */
        printf("aucun périphérique GPU disponible — test ignoré\n");
        SDL_Quit();
        return 77;      /* code « skipped » reconnu par CTest */
    }
    printf("backend GPU : %s\n", ns_rhi_backend_name(rhi));

    /* ---- cible hors écran ------------------------------------------------ */
    ns_texture target;
    ns_texture_desc td;
    SDL_zero(td);
    td.width = TEST_W;
    td.height = TEST_H;
    td.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    td.render_target = true;
    td.sampled = true;
    td.name = "cible de test";
    CHECK(ns_texture_create(rhi, &target, &td), "création de la cible hors écran");
    if (g_failures) goto done;

    /* ---- pipeline : triangle plein écran + dégradé ----------------------- */
    ns_shader_desc vs_desc = { .name = "fullscreen.vert" };
    ns_shader_desc fs_desc = { .name = "test_gradient.frag", .num_uniform_buffers = 1 };

    SDL_GPUShader *vs = ns_shader_load(rhi, &vs_desc, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fs = ns_shader_load(rhi, &fs_desc, SDL_GPU_SHADERSTAGE_FRAGMENT);
    CHECK(vs && fs, "chargement des shaders embarqués");
    if (!vs || !fs) goto done;

    SDL_GPUColorTargetDescription color_desc;
    SDL_zero(color_desc);
    color_desc.format = target.format;

    SDL_GPUGraphicsPipelineCreateInfo pi;
    SDL_zero(pi);
    pi.vertex_shader = vs;
    pi.fragment_shader = fs;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions = &color_desc;
    pi.target_info.num_color_targets = 1;

    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(ns_rhi_device(rhi), &pi);
    CHECK(pipeline != NULL, "création du pipeline : %s", SDL_GetError());
    SDL_ReleaseGPUShader(ns_rhi_device(rhi), vs);
    SDL_ReleaseGPUShader(ns_rhi_device(rhi), fs);
    if (!pipeline) goto done;

    /* ---- rendu ----------------------------------------------------------- */
    CHECK(ns_rhi_begin_frame(rhi), "début d'image");

    SDL_GPUColorTargetInfo cti;
    SDL_zero(cti);
    cti.texture = target.handle;
    cti.load_op = SDL_GPU_LOADOP_CLEAR;
    cti.store_op = SDL_GPU_STOREOP_STORE;
    cti.clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 1.0f };

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(ns_rhi_cmd(rhi), &cti, 1, NULL);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    /* Un uniforme pour prouver que le chemin des constantes fonctionne aussi. */
    const float params[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
    SDL_PushGPUFragmentUniformData(ns_rhi_cmd(rhi), 0, params, sizeof params);

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    ns_rhi_end_frame(rhi);

    /* ---- relecture et vérification --------------------------------------- */
    CHECK(ns_rhi_capture_texture_png(rhi, target.handle, TEST_W, TEST_H, target.format, out_path),
          "capture PNG");

    /* Relire le PNG écrit et vérifier le contenu : le shader produit un dégradé
     * rouge sur l'axe X et vert sur l'axe Y. Un écran noir, un écran uni ou une
     * image inversée seraient détectés ici. */
    int w = 0, h = 0, comp = 0;
    size_t png_bytes = 0;
    void *png_data = SDL_LoadFile(out_path, &png_bytes);
    CHECK(png_data != NULL, "relecture du fichier PNG produit");
    stbi_uc *px = png_data ? stbi_load_from_memory((const stbi_uc *)png_data, (int)png_bytes,
                                                   &w, &h, &comp, 4)
                           : NULL;
    SDL_free(png_data);
    CHECK(px != NULL, "décodage du PNG produit");
    if (px) {
        CHECK(w == (int)TEST_W && h == (int)TEST_H, "dimensions %dx%d attendues %ux%u", w, h, TEST_W, TEST_H);

        const stbi_uc *left   = &px[(( (int)(TEST_H/2) * w) + 4) * 4];
        const stbi_uc *right  = &px[(( (int)(TEST_H/2) * w) + (w - 5)) * 4];
        const stbi_uc *top    = &px[((4 * w) + (int)(TEST_W/2)) * 4];
        const stbi_uc *bottom = &px[(((h - 5) * w) + (int)(TEST_W/2)) * 4];

        printf("échantillons : gauche R=%u  droite R=%u  haut V=%u  bas V=%u\n",
               left[0], right[0], top[1], bottom[1]);

        CHECK(right[0] > left[0] + 100, "dégradé horizontal (rouge %u -> %u)", left[0], right[0]);
        CHECK(bottom[1] > top[1] + 100, "dégradé vertical (vert %u -> %u)", top[1], bottom[1]);
        CHECK(left[2] > 200, "canal bleu piloté par l'uniforme (%u)", left[2]);

        /* Aucun pixel ne doit être complètement noir : cela signalerait une
         * passe qui n'a rien écrit. */
        int black = 0;
        for (int i = 0; i < w * h; ++i) {
            if (px[i*4] == 0 && px[i*4+1] == 0 && px[i*4+2] == 0) black++;
        }
        CHECK(black < (w * h) / 100, "%d pixels noirs sur %d — la passe n'a probablement rien dessiné",
              black, w * h);

        stbi_image_free(px);
    }

    SDL_ReleaseGPUGraphicsPipeline(ns_rhi_device(rhi), pipeline);
    ns_texture_destroy(rhi, &target);

done:
    ns_rhi_destroy(rhi);
    ns_paths_shutdown();
    SDL_Quit();

    printf("\n%s : %d échec(s)\n", g_failures ? "ÉCHEC" : "OK", g_failures);
    return g_failures == 0 ? 0 : 1;
}
