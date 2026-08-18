/* ns_particles.c — voir ns_particles.h pour le raisonnement. */
#include "ns_particles.h"

#include "ns_core.h"
#include "ns_shaders.h"

#include <math.h>
#include <string.h>

/*
 * Demi-taille minimale d'un grain à l'écran, en pixels. En dessous, le
 * rasteriseur ne sait plus le représenter fidèlement (cf. half_size_in_pixels).
 * 0,9 donne un grain d'environ 1,8 px : assez pour être stable, assez petit pour
 * rester un grain.
 */
#define NS_PARTICLE_MIN_HALF_PX 0.9f

/*
 * Le grossissement de sauvetage est PLAFONNÉ, et c'est un garde-fou, pas un
 * réglage. Sans plafond, un grain à quinze mètres est élargi neuf fois : il
 * devient une tache pâle de deux pixels, trois mille taches pâles se recouvrent,
 * et la salle se remplit d'un voile laiteux. C'est le brouillard volumétrique
 * une seconde fois, et deux brouillards superposés donnent exactement l'image
 * qu'on cherche à éviter.
 */
#define NS_PARTICLE_MAX_GROW 2.0f

/*
 * Le partage du travail avec le brouillard volumétrique, et c'est LA décision de
 * conception de ce fichier.
 *
 * Le volumétrique rend la lumière visible dans l'air à toute distance. Les
 * particules, elles, ne servent qu'à une chose que lui ne sait pas faire : le
 * GRAIN, c'est-à-dire des poussières qu'on distingue une à une. Or on ne
 * distingue une poussière que de près — au-delà de quelques mètres elle est
 * sous-pixel, et tout ce qu'on peut en tirer est une moyenne, que le
 * volumétrique calcule déjà, mieux, et avec l'occultation en prime.
 *
 * D'où ces deux distances : plein grain jusqu'à quatre mètres, plus rien à sept.
 * Ce n'est pas une économie, c'est la limite de ce que la passe apporte.
 */
#define NS_PARTICLE_FAR_FULL 4.0f
#define NS_PARTICLE_FAR_GONE 7.0f

typedef struct particle_vertex {
    float position[3];
    float uv[2];
    float color[4];
} particle_vertex;

typedef struct particle {
    ns_v3 position;
    ns_v3 velocity;
    float size;
    float brightness;
    float phase;       /* déphase la dérive : sans ça, tous les grains ondulent ensemble */
    uint8_t zone;
} particle;

struct ns_particles {
    SDL_GPUGraphicsPipeline *pipeline;

    ns_buffer vertices, indices;
    particle_vertex *cpu_verts;

    particle *items;
    uint32_t  capacity, count;

    ns_particle_zone zones[NS_MAX_PARTICLE_ZONES];
    uint32_t         zone_count;

    const ns_light_gpu *lights;
    uint32_t            light_count;

    float    density;
    float    time;
    ns_rng   rng;
};

/* -------------------------------------------------------------------------- */

static float rand_in(ns_rng *r, float lo, float hi)
{
    const float t = (float)(ns_rng_u32(r) >> 8) / (float)(1u << 24);
    return lo + (hi - lo) * t;
}

static void spawn(ns_particles *p, particle *it, uint32_t zone_index)
{
    const ns_particle_zone *z = &p->zones[zone_index];
    it->zone = (uint8_t)zone_index;
    it->position = ns_v3_make(rand_in(&p->rng, z->bounds.min.x, z->bounds.max.x),
                              rand_in(&p->rng, z->bounds.min.y, z->bounds.max.y),
                              rand_in(&p->rng, z->bounds.min.z, z->bounds.max.z));
    /* La dérive de la zone, plus un écart individuel : un courant d'air n'est pas
     * uniforme, et des grains parfaitement parallèles se lisent comme de la
     * pluie. */
    it->velocity = ns_v3_make(z->drift[0] + rand_in(&p->rng, -0.02f, 0.02f),
                              z->drift[1] + rand_in(&p->rng, -0.015f, 0.015f),
                              z->drift[2] + rand_in(&p->rng, -0.02f, 0.02f));
    it->size = z->size * rand_in(&p->rng, 0.6f, 1.5f);
    it->brightness = z->brightness * rand_in(&p->rng, 0.45f, 1.0f);
    it->phase = rand_in(&p->rng, 0.0f, 6.28318f);
}

/* -------------------------------------------------------------------------- */

ns_particles *ns_particles_create(ns_rhi *r, SDL_GPUTextureFormat target_format,
                                  SDL_GPUTextureFormat depth_format,
                                  uint32_t max_particles)
{
    ns_particles *p = (ns_particles *)SDL_calloc(1, sizeof *p);
    if (!p) return NULL;
    p->capacity = max_particles ? max_particles : 2048;
    p->density = 1.0f;
    ns_rng_seed(&p->rng, 0xC0FFEEu, 7u);

    /*
     * `r == NULL` : simulation seule, sans rien de graphique.
     *
     * Ce n'est pas une commodité de test glissée dans le moteur, c'est le seul
     * moyen d'exercer ce que ce fichier a de délicat — le recyclage aux bornes de
     * la zone, le déterminisme à graine fixe, le comptage — là où il n'y a pas de
     * GPU : l'intégration continue de macOS et de Windows n'en a pas. Le dessin,
     * lui, sort tout de suite faute de pipeline.
     */
    /* Les tampons CPU d'abord : ils servent dans les deux cas, et le mode
     * simulation seule s'arrête juste après. */
    p->items = (particle *)SDL_calloc(p->capacity, sizeof(particle));
    p->cpu_verts = (particle_vertex *)SDL_calloc((size_t)p->capacity * 4, sizeof(particle_vertex));
    if (!p->items || !p->cpu_verts) { ns_particles_destroy(r, p); return NULL; }

    if (!r) return p;

    ns_shader_desc vsd, fsd;
    if (!ns_shader_desc_fill("particle.vert", &vsd) || !ns_shader_desc_fill("particle.frag", &fsd)) {
        ns_particles_destroy(r, p); return NULL;
    }
    SDL_GPUShader *vs = ns_shader_load(r, &vsd, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fs = ns_shader_load(r, &fsd, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(ns_rhi_device(r), vs);
        if (fs) SDL_ReleaseGPUShader(ns_rhi_device(r), fs);
        ns_particles_destroy(r, p);
        return NULL;
    }

    SDL_GPUVertexBufferDescription vb;
    SDL_zero(vb);
    vb.slot = 0; vb.pitch = sizeof(particle_vertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[3];
    SDL_zeroa(attrs);
    attrs[0].location = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attrs[0].offset = offsetof(particle_vertex, position);
    attrs[1].location = 1; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[1].offset = offsetof(particle_vertex, uv);
    attrs[2].location = 2; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attrs[2].offset = offsetof(particle_vertex, color);

    /*
     * Mélange ADDITIF, pas « par-dessus ».
     *
     * Un grain éclairé ajoute de la lumière, il n'en cache pas : c'est ce qui le
     * distingue d'un confetti. Et c'est aussi ce qui dispense de trier — une
     * somme ne dépend pas de l'ordre des termes, donc deux poussières l'une
     * derrière l'autre donnent le même résultat dans les deux sens.
     */
    SDL_GPUColorTargetDescription target;
    SDL_zero(target);
    target.format = target_format;
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_zero(info);
    info.vertex_shader = vs;
    info.fragment_shader = fs;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.vertex_input_state.vertex_buffer_descriptions = &vb;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attrs;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = depth_format;
    /*
     * On TESTE la profondeur, on ne l'ÉCRIT pas.
     *
     * Tester : une poussière derrière une borne doit disparaître derrière elle.
     * Ne pas écrire : d'abord parce qu'un grain ne masque pas celui d'après, mais
     * surtout parce que l'accumulation temporelle du lancer de rayons relit cette
     * profondeur — y inscrire des particules mobiles les ferait traîner dans les
     * ombres, exactement comme pour les bras.
     *
     * `GREATER_OR_EQUAL` : la profondeur est inversée dans tout le moteur.
     */
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = false;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;

    p->pipeline = SDL_CreateGPUGraphicsPipeline(ns_rhi_device(r), &info);
    SDL_ReleaseGPUShader(ns_rhi_device(r), vs);
    SDL_ReleaseGPUShader(ns_rhi_device(r), fs);
    if (!p->pipeline) {
        NS_ERROR("particules : pipeline indisponible (%s)", SDL_GetError());
        ns_particles_destroy(r, p);
        return NULL;
    }

    uint16_t *idx = (uint16_t *)SDL_calloc((size_t)p->capacity * 6, sizeof(uint16_t));
    if (!idx) { ns_particles_destroy(r, p); return NULL; }
    for (uint32_t q = 0; q < p->capacity; ++q) {
        const uint16_t v = (uint16_t)(q * 4);
        uint16_t *i = &idx[q * 6];
        i[0] = v; i[1] = (uint16_t)(v + 1); i[2] = (uint16_t)(v + 2);
        i[3] = v; i[4] = (uint16_t)(v + 2); i[5] = (uint16_t)(v + 3);
    }

    const bool ok =
        ns_buffer_create(r, &p->vertices, NS_BUFFER_VERTEX,
                         p->capacity * 4 * (uint32_t)sizeof(particle_vertex), "particules") &&
        ns_buffer_create(r, &p->indices, NS_BUFFER_INDEX,
                         p->capacity * 6 * (uint32_t)sizeof(uint16_t), "particules idx");
    if (ok) ns_buffer_upload(r, &p->indices, idx, p->capacity * 6 * (uint32_t)sizeof(uint16_t), 0);
    SDL_free(idx);
    if (!ok) { ns_particles_destroy(r, p); return NULL; }

    NS_INFO("particules : %u grains au maximum, mélange additif, profondeur testée", p->capacity);
    return p;
}

void ns_particles_destroy(ns_rhi *r, ns_particles *p)
{
    if (!p) return;
    if (p->pipeline) SDL_ReleaseGPUGraphicsPipeline(ns_rhi_device(r), p->pipeline);
    ns_buffer_destroy(r, &p->vertices);
    ns_buffer_destroy(r, &p->indices);
    SDL_free(p->items);
    SDL_free(p->cpu_verts);
    SDL_free(p);
}

void ns_particles_set_density(ns_particles *p, float factor)
{
    if (p) p->density = ns_clampf(factor, 0.0f, 1.0f);
}

void ns_particles_set_zones(ns_particles *p, const ns_particle_zone *zones, uint32_t count,
                            uint64_t seed)
{
    if (!p) return;
    p->zone_count = (count < NS_MAX_PARTICLE_ZONES) ? count : NS_MAX_PARTICLE_ZONES;
    if (p->zone_count) memcpy(p->zones, zones, sizeof(ns_particle_zone) * p->zone_count);
    ns_rng_seed(&p->rng, seed, 0x2545F4914F6CDD1Dull);

    /* Chaque zone reçoit sa part du budget, au prorata de ce qu'elle demande —
     * volume fois densité. Une petite zone dense obtient donc autant de grains
     * qu'une grande zone claire, ce qui est le comportement qu'on attend. */
    float want[NS_MAX_PARTICLE_ZONES], total = 0.0f;
    for (uint32_t z = 0; z < p->zone_count; ++z) {
        const ns_v3 e = ns_aabb_extent(p->zones[z].bounds);
        want[z] = ns_maxf(0.0f, e.x * e.y * e.z) * ns_maxf(0.0f, p->zones[z].density);
        total += want[z];
    }

    p->count = 0;
    if (total <= 0.0f) return;

    const uint32_t budget = (uint32_t)((float)p->capacity * p->density);
    for (uint32_t z = 0; z < p->zone_count && p->count < budget; ++z) {
        uint32_t n = (uint32_t)((want[z] / total) * (float)budget);
        while (n-- > 0 && p->count < budget) spawn(p, &p->items[p->count++], z);
    }
    NS_INFO("particules : %u grains dans %u zone(s)", p->count, p->zone_count);
}

void ns_particles_set_lights(ns_particles *p, const ns_light_gpu *lights, uint32_t count)
{
    if (!p) return;
    p->lights = lights;
    p->light_count = count;
}

/*
 * Ce que le grain reçoit. Décroissance en 1/d² bornée par le rayon de source,
 * comme dans `lighting.frag` — un grain qui frôle une ampoule ne doit pas partir
 * à l'infini.
 */
static float illumination(const ns_particles *p, ns_v3 at, float out_rgb[3])
{
    out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
    float total = 0.0f;
    for (uint32_t i = 0; i < p->light_count; ++i) {
        const ns_light_gpu *l = &p->lights[i];
        if (l->type == 2) continue;        /* une directionnelle n'a pas de faisceau ici */

        const ns_v3 d = ns_v3_make(l->position[0] - at.x, l->position[1] - at.y,
                                   l->position[2] - at.z);
        const float dist2 = ns_v3_len_sq(d);
        if (dist2 > l->range * l->range) continue;

        const float r = ns_maxf(l->source_radius, 0.05f);
        const float atten = 1.0f / ns_maxf(dist2, r * r);
        /* Coupure douce au bord de portée : sans elle, un grain qui franchit la
         * limite s'éteint d'un coup et clignote quand il dérive. */
        const float t = 1.0f - dist2 / (l->range * l->range);
        const float k = l->intensity * atten * t * t;

        out_rgb[0] += l->color[0] * k;
        out_rgb[1] += l->color[1] * k;
        out_rgb[2] += l->color[2] * k;
        total += k;
    }
    return total;
}

void ns_particles_tick(ns_particles *p, float dt)
{
    if (!p || p->count == 0) return;
    p->time += dt;

    for (uint32_t i = 0; i < p->count; ++i) {
        particle *it = &p->items[i];
        const ns_particle_zone *z = &p->zones[it->zone];

        /*
         * Une ondulation lente en plus de la dérive. Sans elle les grains
         * descendent en ligne droite et l'œil y voit de la neige ; avec, ils
         * flottent, ce qui est ce que fait la poussière dans l'air d'une pièce.
         */
        const float w = p->time * 0.6f + it->phase;
        it->position.x += (it->velocity.x + sinf(w) * 0.012f) * dt;
        it->position.y += (it->velocity.y + sinf(w * 1.7f) * 0.008f) * dt;
        it->position.z += (it->velocity.z + cosf(w * 0.8f) * 0.012f) * dt;

        /*
         * Sortie de zone : on rentre par la face opposée plutôt que de mourir et
         * renaître. La densité reste donc exactement constante, sans compteur ni
         * durée de vie — et ça se défend : la poussière d'une pièce fermée ne
         * s'en va pas, elle tourne.
         */
        const ns_aabb b = z->bounds;
        if (it->position.x < b.min.x) it->position.x = b.max.x;
        else if (it->position.x > b.max.x) it->position.x = b.min.x;
        if (it->position.y < b.min.y) it->position.y = b.max.y;
        else if (it->position.y > b.max.y) it->position.y = b.min.y;
        if (it->position.z < b.min.z) it->position.z = b.max.z;
        else if (it->position.z > b.max.z) it->position.z = b.min.z;
    }
}

uint32_t ns_particles_live(const ns_particles *p) { return p ? p->count : 0; }

bool ns_particles_cloud_bounds(const ns_particles *p, ns_aabb *out)
{
    if (!p || !out || p->count == 0) return false;
    ns_aabb b;
    b.min = b.max = p->items[0].position;
    for (uint32_t i = 1; i < p->count; ++i) {
        const ns_v3 q = p->items[i].position;
        if (q.x < b.min.x) b.min.x = q.x;  else if (q.x > b.max.x) b.max.x = q.x;
        if (q.y < b.min.y) b.min.y = q.y;  else if (q.y > b.max.y) b.max.y = q.y;
        if (q.z < b.min.z) b.min.z = q.z;  else if (q.z > b.max.z) b.max.z = q.z;
    }
    *out = b;
    return true;
}

/*
 * La demi-hauteur du grain, en pixels, telle que le rasteriseur la verra.
 *
 * Pourquoi ce calcul existe : un grain de 6 mm à huit mètres couvre un tiers de
 * pixel. Le rasteriseur, lui, ne connaît pas les tiers de pixel — il prend le
 * centre du pixel ou rien. Une moitié des grains lointains disparaissait donc
 * complètement, l'autre s'allumait à pleine intensité, et l'ensemble scintillait
 * dès que la caméra bougeait d'un pouce. C'est le défaut classique du billboard
 * sous-pixel, et il ne se corrige pas en grossissant les grains : on obtient
 * alors de la neige.
 *
 * On projette donc réellement le centre et le bord supérieur du quad. Deux
 * produits matrice-vecteur par grain, trois mille grains : moins d'une
 * microseconde, à comparer aux passes de rendu.
 */
static float half_size_in_pixels(const ns_m4 *vp, ns_v3 centre, ns_v3 edge, uint32_t height)
{
    /* ns_m4 est colonne-majeure et s'indexe m[colonne][ligne]. */
    const float yc = vp->m[0][1] * centre.x + vp->m[1][1] * centre.y + vp->m[2][1] * centre.z + vp->m[3][1];
    const float wc = vp->m[0][3] * centre.x + vp->m[1][3] * centre.y + vp->m[2][3] * centre.z + vp->m[3][3];
    const float ye = vp->m[0][1] * edge.x   + vp->m[1][1] * edge.y   + vp->m[2][1] * edge.z   + vp->m[3][1];
    const float we = vp->m[0][3] * edge.x   + vp->m[1][3] * edge.y   + vp->m[2][3] * edge.z   + vp->m[3][3];
    if (wc < 1e-4f || we < 1e-4f) return 0.0f;      /* derrière l'œil */
    return fabsf(ye / we - yc / wc) * 0.5f * (float)height;
}

void ns_particles_draw(ns_rhi *r, ns_particles *p, const ns_m4 *view_proj,
                       ns_v3 camera_position, ns_v3 camera_right, ns_v3 camera_up,
                       SDL_GPUTexture *target, SDL_GPUTexture *depth,
                       uint32_t width, uint32_t height)
{
    if (!p || !p->pipeline || p->count == 0 || !target) return;
    SDL_GPUCommandBuffer *cmd = ns_rhi_cmd(r);
    if (!cmd) return;

    uint32_t written = 0;
    for (uint32_t i = 0; i < p->count; ++i) {
        const particle *it = &p->items[i];
        const ns_particle_zone *z = &p->zones[it->zone];

        /*
         * Atténuation à l'approche : un grain à dix centimètres de l'œil remplit
         * l'écran d'un disque flou. On l'efface plutôt que de le laisser gêner —
         * une poussière qu'on ne voit qu'en louchant n'apporte rien.
         */
        const float d = ns_v3_dist(it->position, camera_position);
        if (d < 0.28f || d > NS_PARTICLE_FAR_GONE) continue;
        float fade = ns_minf(1.0f, (d - 0.28f) / 0.5f);
        if (d > NS_PARTICLE_FAR_FULL) {
            const float t = (d - NS_PARTICLE_FAR_FULL) / (NS_PARTICLE_FAR_GONE - NS_PARTICLE_FAR_FULL);
            fade *= (1.0f - t) * (1.0f - t);   /* au carré : la disparition ne doit pas avoir de bord */
        }

        /*
         * L'éclairage reçu. Le facteur 0,004 ramène les intensités du moteur —
         * une rampe vaut 74, une borne 38 — dans une échelle mesurée sur la salle
         * où le grain le plus sombre vaut 0,016, la moyenne 0,38 et le plus
         * éclairé 4,6.
         *
         * Puis on l'ÉLÈVE AU CARRÉ, et c'est le point qui décide de tout. Avec la
         * réponse linéaire, un grain moyen valait 8 % du grain le plus vif : tous
         * les grains de la salle se voyaient un peu, et le résultat se lisait
         * comme un voile — ou, en plus gros, comme de la neige. Au carré le même
         * grain moyen tombe à 0,7 %, et l'écart entre « dans le faisceau » et
         * « à côté » devient celui qu'a la vraie poussière : on ne la voit que
         * lorsqu'elle traverse la lumière.
         *
         * C'est aussi ce qui rend le seuil ci-dessous utile plutôt que cosmétique
         * — il retire réellement du dessin les grains hors des cônes.
         */
        float lit_rgb[3];
        const float received = illumination(p, it->position, lit_rgb) * 0.004f;
        const float lit = received * received;
        if (lit < 0.05f) continue;        /* hors faisceau : invisible, donc pas dessiné */

        float h = it->size * 0.5f;

        /*
         * Taille minimale à l'écran, à énergie constante.
         *
         * Sous un pixel et demi, on élargit le grain jusqu'à ce seuil et on
         * divise son opacité par le carré du grossissement : il couvre la même
         * quantité de lumière, répartie sur une surface que le rasteriseur sait
         * représenter. Un grain lointain devient donc un voile très pâle et
         * STABLE au lieu d'un point qui clignote — ce qui est aussi ce que fait
         * l'œil, qui ne résout pas une poussière à huit mètres mais voit très
         * bien le halo qu'elles forment ensemble.
         *
         * C'est le carré et non la racine : diviser moins conserverait moins, et
         * la poussière lointaine deviendrait plus lumineuse que la proche.
         */
        float energy = 1.0f;
        {
            const float px = half_size_in_pixels(view_proj, it->position,
                                                 ns_v3_add(it->position, ns_v3_scale(camera_up, h)),
                                                 height);
            if (px <= 1e-5f) continue;                  /* hors champ ou derrière l'œil */
            if (px < NS_PARTICLE_MIN_HALF_PX) {
                const float grow = ns_minf(NS_PARTICLE_MIN_HALF_PX / px, NS_PARTICLE_MAX_GROW);
                h *= grow;
                energy = 1.0f / (grow * grow);
            }
        }

        const ns_v3 rx = ns_v3_scale(camera_right, h);
        const ns_v3 ry = ns_v3_scale(camera_up, h);

        const ns_v3 corner[4] = {
            ns_v3_sub(ns_v3_sub(it->position, rx), ry),
            ns_v3_sub(ns_v3_add(it->position, rx), ry),
            ns_v3_add(ns_v3_add(it->position, rx), ry),
            ns_v3_add(ns_v3_sub(it->position, rx), ry),
        };
        const float uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
        const float a = it->brightness * fade * energy * ns_minf(lit, 2.2f);

        /* La teinte du grain est celle de ce qui l'éclaire, pas la sienne : une
         * poussière sous un néon bleu est bleue. On normalise ce qu'on a reçu et
         * on le mélange à la couleur déclarée de la zone. */
        const float sum = lit_rgb[0] + lit_rgb[1] + lit_rgb[2];
        float tint[3] = { 1.0f, 1.0f, 1.0f };
        if (sum > 1e-6f) {
            /* Bornée : sous un néon saturé la normalisation donnerait (0, 0, 3),
             * et un grain trois fois plus bleu que blanc n'est plus une poussière,
             * c'est un pixel bleu. */
            for (int k = 0; k < 3; ++k) tint[k] = ns_minf((lit_rgb[k] * 3.0f) / sum, 1.8f);
        }

        particle_vertex *v = &p->cpu_verts[written * 4];
        for (int k = 0; k < 4; ++k) {
            v[k].position[0] = corner[k].x;
            v[k].position[1] = corner[k].y;
            v[k].position[2] = corner[k].z;
            v[k].uv[0] = uv[k][0]; v[k].uv[1] = uv[k][1];
            v[k].color[0] = z->color[0] * tint[0];
            v[k].color[1] = z->color[1] * tint[1];
            v[k].color[2] = z->color[2] * tint[2];
            v[k].color[3] = a;
        }
        written++;
    }
    if (written == 0) return;

    ns_rhi_stage_buffer(r, &p->vertices, p->cpu_verts,
                        written * 4 * (uint32_t)sizeof(particle_vertex), 0);
    ns_rhi_flush_staging(r);

    SDL_GPUColorTargetInfo ci;
    SDL_zero(ci);
    ci.texture = target;
    ci.load_op = SDL_GPU_LOADOP_LOAD;      /* on ajoute à l'image, on ne l'efface pas */
    ci.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo ds;
    SDL_zero(ds);
    ds.texture = depth;
    ds.load_op = SDL_GPU_LOADOP_LOAD;
    ds.store_op = SDL_GPU_STOREOP_STORE;
    ds.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    ds.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ci, 1, depth ? &ds : NULL);
    if (!pass) return;

    SDL_BindGPUGraphicsPipeline(pass, p->pipeline);
    SDL_GPUViewport vp = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    SDL_SetGPUViewport(pass, &vp);

    SDL_GPUBufferBinding vbind = { p->vertices.handle, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);
    SDL_GPUBufferBinding ibind = { p->indices.handle, 0 };
    SDL_BindGPUIndexBuffer(pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    SDL_PushGPUVertexUniformData(cmd, 0, view_proj->m, sizeof(float) * 16);
    SDL_DrawGPUIndexedPrimitives(pass, written * 6, 1, 0, 0, 0);

    SDL_EndGPURenderPass(pass);
}
