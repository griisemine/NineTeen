/* ns_scene.c — chargement de la salle depuis le glTF produit par obj2gltf. */
#include "ns_scene.h"
#include "ns_json.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <string.h>

/* Arène de chargement : la scène complète (96k sommets, 102k triangles,
 * 120 matériaux) tient largement dedans, et tout est libéré d'un coup. */
#define SCENE_ARENA_BYTES (96u * 1024u * 1024u)

/* ========================================================================== */
/* Utilitaires                                                                */
/* ========================================================================== */

/* Concatène le répertoire d'un chemin logique avec un nom de fichier. */
static void logical_sibling(const char *logical, const char *file, char *out, size_t out_size)
{
    const char *slash = NULL;
    for (const char *p = logical; *p; ++p) {
        if (*p == '/') slash = p;
    }
    if (!slash) {
        SDL_strlcpy(out, file, out_size);
        return;
    }
    const size_t dir_len = (size_t)(slash - logical) + 1;
    if (dir_len >= out_size) { out[0] = '\0'; return; }
    SDL_memcpy(out, logical, dir_len);
    SDL_strlcpy(out + dir_len, file, out_size - dir_len);
}

static void basename_noext(const char *path, char *out, size_t out_size)
{
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    const char *dot = SDL_strrchr(base, '.');
    const size_t n = dot ? (size_t)(dot - base) : SDL_strlen(base);
    const size_t copy = (n < out_size - 1) ? n : out_size - 1;
    SDL_memcpy(out, base, copy);
    out[copy] = '\0';
}

/* ========================================================================== */
/* Textures                                                                   */
/* ========================================================================== */
/*
 * Chaque image du glTF donne trois textures GPU : le diffus d'origine, la
 * normal map et la carte ORM produites par texgen. Les deux dernières peuvent
 * manquer (texture non traitée, build partiel) : on retombe alors sur les
 * substituts 1x1, ce qui donne une surface plate et mate plutôt qu'un plantage.
 */
typedef struct texture_triplet {
    int32_t albedo, normal, orm;
} texture_triplet;

static int32_t load_one(ns_rhi *r, ns_scene *s, uint32_t *cursor,
                        const char *logical, bool srgb)
{
    if (*cursor >= s->texture_count) {
        NS_WARN("plus d'emplacement de texture disponible pour %s", logical);
        return -1;
    }
    ns_texture *slot = &s->textures[*cursor];
    if (!ns_texture_load(r, slot, logical, srgb, true)) {
        return -1;
    }
    return (int32_t)(*cursor)++;
}

/* ========================================================================== */
/* Lumières et bornes (fichiers JSON annexes)                                 */
/* ========================================================================== */

static void load_lights(ns_scene *s, const char *lights_logical)
{
    ns_arena_mark mark = ns_arena_save(&s->arena);
    size_t size = 0;
    char *text = (char *)ns_file_read_all(&s->arena, lights_logical, &size);
    if (!text) {
        NS_WARN("lumières introuvables (%s) : la salle sera éclairée par la seule "
                "lumière directionnelle", lights_logical);
        ns_arena_restore(&s->arena, mark);
        return;
    }

    ns_json doc;
    if (!ns_json_parse(&doc, text, size, &s->arena)) {
        NS_ERROR("lumières illisibles : %s", lights_logical);
        ns_arena_restore(&s->arena, mark);
        return;
    }

    const ns_json_value *lights = ns_json_get(&doc, ns_json_root(&doc), "lights");
    const int count = ns_json_array_count(&doc, lights);
    for (int i = 0; i < count && s->light_count < NS_MAX_LIGHTS; ++i) {
        const ns_json_value *e = ns_json_at(&doc, lights, i);
        ns_light_gpu *l = &s->lights[s->light_count];
        ns_light_anim *a = &s->light_anim[s->light_count];
        SDL_zerop(l);
        SDL_zerop(a);

        ns_json_get_vec3(&doc, e, "position", l->position, 0.0f);
        ns_json_get_vec3(&doc, e, "color", l->color, 1.0f);
        l->intensity = ns_json_get_float(&doc, e, "intensity", 5.0f);
        l->range     = ns_json_get_float(&doc, e, "range", 8.0f);
        l->type      = NS_LIGHT_POINT;
        l->shadow_index = -1;

        a->flicker        = ns_json_get_bool(&doc, e, "flicker", false);
        a->base_intensity = l->intensity;
        /* Une phase distincte par lumière : sinon tous les néons clignotent
         * ensemble, ce qui se voit immédiatement comme artificiel. */
        a->phase = (float)i * 2.399963f;      /* angle d'or, bonne dispersion */

        s->light_count++;
    }

    const ns_json_value *bounds = ns_json_get(&doc, ns_json_root(&doc), "bounds");
    if (bounds) {
        float bmin[3], bmax[3];
        ns_json_get_vec3(&doc, bounds, "min", bmin, 0.0f);
        ns_json_get_vec3(&doc, bounds, "max", bmax, 0.0f);
        s->bounds.min = ns_v3_make(bmin[0], bmin[1], bmin[2]);
        s->bounds.max = ns_v3_make(bmax[0], bmax[1], bmax[2]);
    }

    /* Bornes : positions et emprises. L'affectation des jeux vient du second
     * fichier, écrit à la main. */
    const ns_json_value *cabs = ns_json_get(&doc, ns_json_root(&doc), "cabinets");
    const int cab_count = ns_json_array_count(&doc, cabs);
    for (int i = 0; i < cab_count && s->cabinet_count < NS_MAX_CABINETS; ++i) {
        const ns_json_value *e = ns_json_at(&doc, cabs, i);
        ns_cabinet *c = &s->cabinets[s->cabinet_count];
        SDL_zerop(c);

        c->slot = (int)ns_json_get_float(&doc, e, "slot", (float)i);
        ns_json_get_string(&doc, e, "name", c->name, sizeof c->name);
        SDL_strlcpy(c->game, "unassigned", sizeof c->game);

        float bmin[3], bmax[3];
        ns_json_get_vec3(&doc, e, "bboxMin", bmin, 0.0f);
        ns_json_get_vec3(&doc, e, "bboxMax", bmax, 0.0f);
        c->bounds.min = ns_v3_make(bmin[0], bmin[1], bmin[2]);
        c->bounds.max = ns_v3_make(bmax[0], bmax[1], bmax[2]);

        s->cabinet_count++;
    }

    NS_INFO("%u lumières, %u bornes chargées", s->light_count, s->cabinet_count);
    ns_arena_restore(&s->arena, mark);
}

static void load_cabinet_assignment(ns_scene *s, const char *logical)
{
    ns_arena_mark mark = ns_arena_save(&s->arena);
    size_t size = 0;
    char *text = (char *)ns_file_read_all(&s->arena, logical, &size);
    if (!text) {
        NS_WARN("affectation des bornes introuvable (%s) : les bornes resteront "
                "sans jeu", logical);
        ns_arena_restore(&s->arena, mark);
        return;
    }

    ns_json doc;
    if (!ns_json_parse(&doc, text, size, &s->arena)) {
        NS_ERROR("affectation des bornes illisible : %s", logical);
        ns_arena_restore(&s->arena, mark);
        return;
    }

    const ns_json_value *list = ns_json_get(&doc, ns_json_root(&doc), "cabinets");
    const int count = ns_json_array_count(&doc, list);
    uint32_t assigned = 0;

    for (int i = 0; i < count; ++i) {
        const ns_json_value *e = ns_json_at(&doc, list, i);
        const int slot = (int)ns_json_get_float(&doc, e, "slot", -1.0f);

        for (uint32_t c = 0; c < s->cabinet_count; ++c) {
            if (s->cabinets[c].slot != slot) continue;
            ns_cabinet *cab = &s->cabinets[c];
            ns_json_get_string(&doc, e, "game", cab->game, sizeof cab->game);
            ns_json_get_string(&doc, e, "difficulty", cab->difficulty, sizeof cab->difficulty);
            cab->attract = ns_json_get_bool(&doc, e, "attract", true);
            assigned++;
            break;
        }
    }

    /* Géométrie de l'écran, commune à toutes les bornes puisqu'elles sortent
     * du même modèle. */
    const ns_json_value *screen = ns_json_get(&doc, ns_json_root(&doc), "screen");
    float origin_f[3] = { 0.14f, 0.46f, 0.02f };
    float size_f[3]   = { 0.72f, 0.30f, 0.0f };
    if (screen) {
        ns_json_get_vec3(&doc, screen, "originFraction", origin_f, 0.0f);
        ns_json_get_vec3(&doc, screen, "sizeFraction", size_f, 0.0f);
    }

    for (uint32_t c = 0; c < s->cabinet_count; ++c) {
        ns_cabinet *cab = &s->cabinets[c];
        const ns_v3 extent = ns_aabb_extent(cab->bounds);
        const ns_v3 centre = ns_aabb_center(cab->bounds);

        cab->screen_center = ns_v3_make(
            cab->bounds.min.x + extent.x * (origin_f[0] + size_f[0] * 0.5f),
            cab->bounds.min.y + extent.y * (origin_f[1] + size_f[1] * 0.5f),
            centre.z);

        /* La borne regarde le long de son axe le plus court : c'est la face
         * avant, celle où se tient le joueur. */
        if (extent.x < extent.z) {
            cab->screen_normal = ns_v3_make(centre.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
        } else {
            cab->screen_normal = ns_v3_make(0.0f, 0.0f, centre.z < 0.0f ? -1.0f : 1.0f);
        }
        cab->player_anchor = ns_v3_add(cab->screen_center, ns_v3_scale(cab->screen_normal, 1.1f));
        cab->player_anchor.y = cab->bounds.min.y;
    }

    NS_INFO("%u bornes affectées à un jeu", assigned);
    ns_arena_restore(&s->arena, mark);
}

/* ========================================================================== */
/* Chargement principal                                                       */
/* ========================================================================== */

bool ns_scene_load(ns_rhi *r, ns_scene *out, const char *gltf_logical)
{
    NS_ASSERT(r && out && gltf_logical);
    SDL_zerop(out);

    if (!ns_arena_init(&out->arena, SCENE_ARENA_BYTES, "scène")) return false;

    /* cgltf lit depuis le disque ; on résout d'abord le chemin réel. */
    char gltf_path[1024];
    if (!ns_path_resolve(gltf_logical, gltf_path, sizeof gltf_path)) {
        NS_ERROR("scène introuvable : %s", gltf_logical);
        ns_arena_free(&out->arena);
        return false;
    }
    NS_INFO("chargement de la scène : %s", gltf_path);

    cgltf_options opt;
    SDL_zero(opt);
    cgltf_data *data = NULL;
    if (cgltf_parse_file(&opt, gltf_path, &data) != cgltf_result_success) {
        NS_ERROR("glTF illisible : %s", gltf_path);
        ns_arena_free(&out->arena);
        return false;
    }
    if (cgltf_load_buffers(&opt, data, gltf_path) != cgltf_result_success) {
        NS_ERROR("tampons du glTF introuvables (le .bin est-il à côté ?)");
        cgltf_free(data);
        ns_arena_free(&out->arena);
        return false;
    }

    /* ---------------------------------------------------------- géométrie */
    /*
     * Un premier passage compte, un second remplit.
     *
     * Subtilité qui coûte cher si on la manque : obj2gltf émet **un seul jeu
     * d'accesseurs d'attributs** partagé par toutes les primitives, chacune
     * n'ayant que sa propre plage d'indices. Compter naïvement les sommets par
     * primitive multiplierait donc le total par le nombre de primitives — ici
     * 96 067 sommets deviendraient 17,5 millions. On mémorise l'accesseur de
     * positions déjà rencontré pour ne le téléverser qu'une fois.
     */
    size_t total_vertices = 0, total_indices = 0, total_prims = 0;

    const cgltf_accessor **seen_pos = NULL;
    size_t seen_count = 0;
    if (data->nodes_count) {
        /* Borne supérieure : une entrée par primitive. */
        size_t max_prims = 0;
        for (cgltf_size n = 0; n < data->nodes_count; ++n) {
            if (data->nodes[n].mesh) max_prims += data->nodes[n].mesh->primitives_count;
        }
        seen_pos = NS_ARENA_ARRAY(&out->arena, const cgltf_accessor *, max_prims ? max_prims : 1);
    }

    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node *node = &data->nodes[n];
        if (!node->mesh) continue;
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            const cgltf_primitive *prim = &node->mesh->primitives[p];
            if (prim->type != cgltf_primitive_type_triangles || !prim->indices) continue;

            const cgltf_accessor *pos = NULL;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                if (prim->attributes[a].type == cgltf_attribute_type_position) {
                    pos = prim->attributes[a].data;
                }
            }
            if (pos && seen_pos) {
                bool already = false;
                for (size_t k = 0; k < seen_count; ++k) {
                    if (seen_pos[k] == pos) { already = true; break; }
                }
                if (!already) {
                    seen_pos[seen_count++] = pos;
                    total_vertices += pos->count;
                }
            } else if (pos) {
                total_vertices += pos->count;
            }
            total_indices += prim->indices->count;
            total_prims++;
        }
    }
    NS_INFO("%zu sommets, %zu indices, %zu primitives", total_vertices, total_indices, total_prims);

    if (total_vertices == 0 || total_indices == 0) {
        NS_ERROR("scène vide");
        cgltf_free(data);
        ns_arena_free(&out->arena);
        return false;
    }

    ns_vertex *vertices = NS_ARENA_ARRAY(&out->arena, ns_vertex, total_vertices);
    uint32_t  *indices  = NS_ARENA_ARRAY(&out->arena, uint32_t, total_indices);
    out->batches = NS_ARENA_ARRAY(&out->arena, ns_draw_batch, total_prims);
    if (!vertices || !indices || !out->batches) {
        cgltf_free(data);
        ns_arena_free(&out->arena);
        return false;
    }

    uint32_t vcursor = 0, icursor = 0;
    ns_aabb scene_bounds = ns_aabb_empty();

    /* Correspondance accesseur de positions -> premier sommet déjà écrit, pour
     * que les primitives partageant le même tampon le partagent aussi en GPU. */
    typedef struct { const cgltf_accessor *acc; uint32_t base; } pos_slot;
    pos_slot *pos_map = NS_ARENA_ARRAY(&out->arena, pos_slot, total_prims ? total_prims : 1);
    size_t pos_map_count = 0;

    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node *node = &data->nodes[n];
        if (!node->mesh) continue;

        cgltf_float world[16];
        cgltf_node_transform_world(node, world);

        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            const cgltf_primitive *prim = &node->mesh->primitives[p];
            if (prim->type != cgltf_primitive_type_triangles || !prim->indices) continue;

            const cgltf_accessor *acc_pos = NULL, *acc_nrm = NULL, *acc_uv = NULL, *acc_tan = NULL;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                switch (prim->attributes[a].type) {
                case cgltf_attribute_type_position: acc_pos = prim->attributes[a].data; break;
                case cgltf_attribute_type_normal:   acc_nrm = prim->attributes[a].data; break;
                case cgltf_attribute_type_texcoord: if (!acc_uv) acc_uv = prim->attributes[a].data; break;
                case cgltf_attribute_type_tangent:  acc_tan = prim->attributes[a].data; break;
                default: break;
                }
            }
            if (!acc_pos) continue;

            /* Ce jeu de sommets a-t-il déjà été écrit par une primitive
             * précédente ? Si oui, on réutilise sa base plutôt que de le
             * recopier. */
            uint32_t base_vertex = UINT32_MAX;
            for (size_t k = 0; k < pos_map_count; ++k) {
                if (pos_map[k].acc == acc_pos) { base_vertex = pos_map[k].base; break; }
            }
            const bool need_vertices = (base_vertex == UINT32_MAX);
            if (need_vertices) {
                base_vertex = vcursor;
                pos_map[pos_map_count].acc = acc_pos;
                pos_map[pos_map_count].base = base_vertex;
                pos_map_count++;
            }

            const uint32_t first_index = icursor;
            ns_aabb prim_bounds = ns_aabb_empty();

            for (cgltf_size v = 0; need_vertices && v < acc_pos->count; ++v) {
                ns_vertex *dst = &vertices[vcursor + v];
                SDL_zerop(dst);

                float local[3] = { 0, 0, 0 };
                cgltf_accessor_read_float(acc_pos, v, local, 3);
                /* Transformation en espace monde une fois pour toutes : la salle
                 * est statique, inutile de payer une matrice par objet à chaque
                 * image. */
                dst->position[0] = world[0]*local[0] + world[4]*local[1] + world[8] *local[2] + world[12];
                dst->position[1] = world[1]*local[0] + world[5]*local[1] + world[9] *local[2] + world[13];
                dst->position[2] = world[2]*local[0] + world[6]*local[1] + world[10]*local[2] + world[14];

                if (acc_nrm) {
                    float ln[3] = { 0, 1, 0 };
                    cgltf_accessor_read_float(acc_nrm, v, ln, 3);
                    /* Rotation seule (pas de translation) ; l'échelle de la
                     * scène est uniforme, donc pas besoin de la transposée de
                     * l'inverse. */
                    dst->normal[0] = world[0]*ln[0] + world[4]*ln[1] + world[8] *ln[2];
                    dst->normal[1] = world[1]*ln[0] + world[5]*ln[1] + world[9] *ln[2];
                    dst->normal[2] = world[2]*ln[0] + world[6]*ln[1] + world[10]*ln[2];
                } else {
                    dst->normal[1] = 1.0f;
                }
                if (acc_uv)  cgltf_accessor_read_float(acc_uv, v, dst->uv, 2);
                if (acc_tan) {
                    float lt[4] = { 1, 0, 0, 1 };
                    cgltf_accessor_read_float(acc_tan, v, lt, 4);
                    dst->tangent[0] = world[0]*lt[0] + world[4]*lt[1] + world[8] *lt[2];
                    dst->tangent[1] = world[1]*lt[0] + world[5]*lt[1] + world[9] *lt[2];
                    dst->tangent[2] = world[2]*lt[0] + world[6]*lt[1] + world[10]*lt[2];
                    dst->tangent[3] = lt[3];
                } else {
                    dst->tangent[0] = 1.0f; dst->tangent[3] = 1.0f;
                }

            }
            if (need_vertices) vcursor += (uint32_t)acc_pos->count;

            /* L'emprise de la primitive se calcule sur ses indices, pas sur tout
             * le tampon partagé : sinon chaque lot aurait l'emprise de la salle
             * entière et l'élimination par frustum ne servirait à rien. */
            for (cgltf_size i = 0; i < prim->indices->count; ++i) {
                const uint32_t vi = base_vertex + (uint32_t)cgltf_accessor_read_index(prim->indices, i);
                indices[icursor + i] = vi;
                if (vi < vcursor) {
                    const ns_vertex *v = &vertices[vi];
                    prim_bounds = ns_aabb_add_point(prim_bounds,
                                                    ns_v3_make(v->position[0], v->position[1], v->position[2]));
                }
            }
            icursor += (uint32_t)prim->indices->count;

            ns_draw_batch *batch = &out->batches[out->batch_count++];
            batch->first_index = first_index;
            batch->index_count = (uint32_t)prim->indices->count;
            batch->material    = prim->material
                               ? (int32_t)cgltf_material_index(data, prim->material) : -1;
            batch->bounds      = prim_bounds;

            scene_bounds = ns_aabb_union(scene_bounds, prim_bounds);
        }
    }

    out->vertex_count = vcursor;
    out->index_count  = icursor;
    if (ns_aabb_valid(scene_bounds)) out->bounds = scene_bounds;

    /* ---------------------------------------------------------- matériaux */
    out->material_count = (uint32_t)data->materials_count;
    if (out->material_count == 0) out->material_count = 1;
    out->material_data = NS_ARENA_ARRAY(&out->arena, ns_material_gpu, out->material_count);

    /* Trois textures possibles par matériau, plus les substituts. */
    out->texture_count = (uint32_t)data->images_count * 3u + 4u;
    out->textures = NS_ARENA_ARRAY(&out->arena, ns_texture, out->texture_count);
    SDL_memset(out->textures, 0, sizeof(ns_texture) * out->texture_count);

    out->fallback_white  = ns_texture_white(r);
    out->fallback_normal = ns_texture_flat_normal(r);
    /* ORM neutre : occlusion 1, rugosité 1, métallicité 0. Les facteurs du
     * matériau font le reste. */
    out->fallback_orm = ns_texture_white(r);

    texture_triplet *triplets = NULL;
    uint32_t tex_cursor = 0;
    if (data->images_count) {
        triplets = NS_ARENA_ARRAY(&out->arena, texture_triplet, data->images_count);
        for (cgltf_size i = 0; i < data->images_count; ++i) {
            triplets[i].albedo = triplets[i].normal = triplets[i].orm = -1;
            if (!data->images[i].uri) continue;

            char logical[512], name[128], maps[512];
            logical_sibling(gltf_logical, data->images[i].uri, logical, sizeof logical);
            triplets[i].albedo = load_one(r, out, &tex_cursor, logical, true);

            /* Les cartes générées vivent dans materials/, pas à côté du glTF. */
            basename_noext(data->images[i].uri, name, sizeof name);
            SDL_snprintf(maps, sizeof maps, "materials/%s_n.png", name);
            triplets[i].normal = load_one(r, out, &tex_cursor, maps, false);
            SDL_snprintf(maps, sizeof maps, "materials/%s_orm.png", name);
            triplets[i].orm = load_one(r, out, &tex_cursor, maps, false);
        }
        NS_INFO("%u textures GPU chargées (%zu images du glTF)", tex_cursor, (size_t)data->images_count);
    }

    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material *src = &data->materials[i];
        ns_material_gpu *m = &out->material_data[i];
        SDL_zerop(m);
        m->albedo_texture = m->normal_texture = m->orm_texture = -1;

        if (src->has_pbr_metallic_roughness) {
            SDL_memcpy(m->base_color, src->pbr_metallic_roughness.base_color_factor, sizeof m->base_color);
            m->metallic  = src->pbr_metallic_roughness.metallic_factor;
            m->roughness = src->pbr_metallic_roughness.roughness_factor;

            const cgltf_texture *t = src->pbr_metallic_roughness.base_color_texture.texture;
            if (t && t->image && triplets) {
                const cgltf_size img = (cgltf_size)cgltf_image_index(data, t->image);
                m->albedo_texture = triplets[img].albedo;
                m->normal_texture = triplets[img].normal;
                m->orm_texture    = triplets[img].orm;
            }
        } else {
            m->base_color[0] = m->base_color[1] = m->base_color[2] = m->base_color[3] = 1.0f;
            m->roughness = 0.8f;
        }
        SDL_memcpy(m->emissive, src->emissive_factor, sizeof m->emissive);
        m->emissive_strength = src->has_emissive_strength
                             ? src->emissive_strength.emissive_strength : 1.0f;
    }
    if (data->materials_count == 0) {
        ns_material_gpu *m = &out->material_data[0];
        SDL_zerop(m);
        m->base_color[0] = m->base_color[1] = m->base_color[2] = m->base_color[3] = 1.0f;
        m->roughness = 0.8f;
        m->albedo_texture = m->normal_texture = m->orm_texture = -1;
    }

    cgltf_free(data);

    /* ------------------------------------------------------ téléversement */
    bool ok = true;
    ok = ok && ns_buffer_create(r, &out->vertices, NS_BUFFER_VERTEX,
                                (uint32_t)(sizeof(ns_vertex) * out->vertex_count), "sommets salle");
    ok = ok && ns_buffer_create(r, &out->indices, NS_BUFFER_INDEX,
                                (uint32_t)(sizeof(uint32_t) * out->index_count), "indices salle");
    ok = ok && ns_buffer_create(r, &out->materials, NS_BUFFER_STORAGE,
                                (uint32_t)(sizeof(ns_material_gpu) * out->material_count), "matériaux");
    ok = ok && ns_buffer_upload(r, &out->vertices, vertices,
                                (uint32_t)(sizeof(ns_vertex) * out->vertex_count), 0);
    ok = ok && ns_buffer_upload(r, &out->indices, indices,
                                (uint32_t)(sizeof(uint32_t) * out->index_count), 0);
    ok = ok && ns_buffer_upload(r, &out->materials, out->material_data,
                                (uint32_t)(sizeof(ns_material_gpu) * out->material_count), 0);
    if (!ok) {
        NS_ERROR("téléversement de la scène impossible");
        ns_scene_unload(r, out);
        return false;
    }

    /* ------------------------------------------------- lumières et bornes */
    char sibling[512];
    logical_sibling(gltf_logical, "salle.lights.json", sibling, sizeof sibling);
    load_lights(out, sibling);
    logical_sibling(gltf_logical, "cabinets.json", sibling, sizeof sibling);
    load_cabinet_assignment(out, sibling);

    /* Emprise jouable : union des bornes et des lumières, élargie d'une marge
     * de circulation. C'est ce volume qui sert à placer la caméra et le joueur. */
    {
        ns_aabb rb = ns_aabb_empty();
        for (uint32_t i = 0; i < out->cabinet_count; ++i) {
            rb = ns_aabb_union(rb, out->cabinets[i].bounds);
        }
        for (uint32_t i = 0; i < out->light_count; ++i) {
            rb = ns_aabb_add_point(rb, ns_v3_make(out->lights[i].position[0],
                                                  out->lights[i].position[1],
                                                  out->lights[i].position[2]));
        }
        if (ns_aabb_valid(rb)) {
            const ns_v3 margin = ns_v3_make(3.0f, 0.5f, 3.0f);
            rb.min = ns_v3_sub(rb.min, margin);
            rb.max = ns_v3_add(rb.max, margin);
            /* Ne jamais déborder de la géométrie réelle. */
            rb.min = ns_v3_max(rb.min, out->bounds.min);
            rb.max = ns_v3_min(rb.max, out->bounds.max);
            out->room_bounds = rb;
        } else {
            out->room_bounds = out->bounds;
        }
        NS_INFO("emprise jouable : (%.1f %.1f %.1f) à (%.1f %.1f %.1f)",
                (double)out->room_bounds.min.x, (double)out->room_bounds.min.y,
                (double)out->room_bounds.min.z, (double)out->room_bounds.max.x,
                (double)out->room_bounds.max.y, (double)out->room_bounds.max.z);
    }

    NS_INFO("scène prête : %u sommets, %u indices, %u lots, %u matériaux, %u lumières",
            out->vertex_count, out->index_count, out->batch_count,
            out->material_count, out->light_count);
    NS_INFO("emprise : (%.1f %.1f %.1f) à (%.1f %.1f %.1f)",
            (double)out->bounds.min.x, (double)out->bounds.min.y, (double)out->bounds.min.z,
            (double)out->bounds.max.x, (double)out->bounds.max.y, (double)out->bounds.max.z);
    return true;
}

void ns_scene_unload(ns_rhi *r, ns_scene *s)
{
    if (!s) return;
    for (uint32_t i = 0; i < s->texture_count; ++i) {
        if (s->textures && s->textures[i].handle) ns_texture_destroy(r, &s->textures[i]);
    }
    ns_texture_destroy(r, &s->fallback_white);
    ns_texture_destroy(r, &s->fallback_normal);
    ns_texture_destroy(r, &s->fallback_orm);
    ns_buffer_destroy(r, &s->vertices);
    ns_buffer_destroy(r, &s->indices);
    ns_buffer_destroy(r, &s->materials);
    ns_arena_free(&s->arena);
    SDL_zerop(s);
}

/* ========================================================================== */
/* Animation de l'éclairage                                                   */
/* ========================================================================== */

void ns_scene_animate_lights(ns_scene *s, double time_seconds)
{
    const float t = (float)time_seconds;
    for (uint32_t i = 0; i < s->light_count; ++i) {
        ns_light_anim *a = &s->light_anim[i];
        if (!a->flicker) continue;

        /* Scintillement de tube fluorescent : une oscillation lente pour la
         * respiration, une rapide pour le grésillement, et un creux occasionnel.
         * Une simple sinusoïde donnerait un clignotement de guirlande. */
        const float slow = sinf(t * 1.7f + a->phase) * 0.04f;
        const float fast = sinf(t * 37.0f + a->phase * 3.1f) * 0.02f;
        const float dip  = (sinf(t * 0.53f + a->phase) > 0.985f) ? -0.35f : 0.0f;

        s->lights[i].intensity = a->base_intensity * (1.0f + slow + fast + dip);
        if (s->lights[i].intensity < 0.0f) s->lights[i].intensity = 0.0f;
    }
}

const ns_cabinet *ns_scene_nearest_cabinet(const ns_scene *s, ns_v3 position, float max_distance)
{
    const ns_cabinet *best = NULL;
    float best_dist = max_distance;

    for (uint32_t i = 0; i < s->cabinet_count; ++i) {
        const ns_cabinet *c = &s->cabinets[i];
        /* Distance au point où se tient le joueur, pas au centre de la borne :
         * on veut détecter « je suis devant », pas « je suis à côté ». */
        const float d = ns_v3_dist(position, c->player_anchor);
        if (d < best_dist) { best_dist = d; best = c; }
    }
    return best;
}
