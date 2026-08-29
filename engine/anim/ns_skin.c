/* Voir ns_skin.h pour ce que fait ce module et pourquoi il existe. */
#include "ns_skin.h"

#include "ns_core.h"

#include "cgltf.h"

#include <SDL3/SDL.h>

#include <float.h>
#include <string.h>

/* Un canal d'animation : la piste d'un nœud pour une propriété. */
typedef struct skin_track {
    const float *times;     /* `count` instants, croissants */
    const float *values;    /* `count` x `stride` flottants */
    uint32_t     count;
    uint8_t      stride;    /* 3 pour T et S, 4 pour R */
    bool         step;      /* interpolation en marches plutôt que linéaire */
} skin_track;

typedef struct skin_node {
    int         parent;
    ns_v3       t, s;
    ns_quat     r;
    /*
     * La transformation FIXE d'un nœud qui porte une matrice plutôt qu'un
     * triplet. glTF autorise les deux formes et INTERDIT de les mélanger : un
     * nœud à matrice ne peut pas être la cible d'un canal d'animation.
     *
     * Ce n'est pas un cas de bord exotique — c'est même la règle sur ce
     * personnage-ci : `Z_UP` porte le changement de repère Z-haut vers Y-haut,
     * et `Armature` la pose du squelette. Les ignorer couche le personnage sur
     * le flanc.
     */
    ns_m4       fixed;
    bool        has_fixed;
    skin_track  track_t, track_r, track_s;
} skin_node;

struct ns_skin {
    ns_skin_vertex *verts;
    uint32_t        vert_count;
    uint32_t       *indices;
    uint32_t        index_count;

    skin_node      *node;
    int             node_count;

    int             joint_node[NS_SKIN_MAX_JOINTS];
    ns_m4           inverse_bind[NS_SKIN_MAX_JOINTS];
    int             joint_count;

    float           duration;
    float           rest_height;
    float           stand_time;

    /* L'image reste CONST : elle appartient au tampon de cgltf, qu'on garde
     * vivant pour ça. La copier serait un demi-mégaoctet de plus pour rien. */
    const void     *image;
    size_t          image_size;

    cgltf_data     *data;      /* gardé : les pistes pointent dans ses tampons */
    char           *bytes;     /* le fichier, gardé pour la même raison */
};

/* ------------------------------------------------------------------ lecture */

static const float *accessor_floats(const cgltf_accessor *a, uint32_t *count, uint8_t *stride)
{
    if (!a || !a->buffer_view || !a->buffer_view->buffer) return NULL;
    /*
     * On exige du flottant NON entrelacé et non compressé, et on le VÉRIFIE.
     *
     * cgltf sait convertir (`cgltf_accessor_read_float`), mais appeler ça par
     * sommet pour trois mille sommets et par clé pour cinquante-sept pistes
     * revient à écrire un décodeur lent pour un cas qui ne se produit pas : les
     * exportateurs écrivent du float32 serré. On lit donc directement, et si un
     * jour un fichier ne s'y conforme pas, on le DIT au lieu de lire de
     * travers.
     */
    if (a->component_type != cgltf_component_type_r_32f || a->is_sparse) return NULL;
    const cgltf_size n = cgltf_num_components(a->type);
    const cgltf_size packed = n * sizeof(float);
    if (a->stride != packed) return NULL;

    const uint8_t *base = (const uint8_t *)a->buffer_view->buffer->data;
    if (!base) return NULL;
    if (count)  *count  = (uint32_t)a->count;
    if (stride) *stride = (uint8_t)n;
    return (const float *)(base + a->buffer_view->offset + a->offset);
}

static void read_track(const cgltf_animation_sampler *smp, skin_track *out)
{
    if (!smp) return;
    uint8_t st = 0;
    const float *t = accessor_floats(smp->input, &out->count, NULL);
    const float *v = accessor_floats(smp->output, NULL, &st);
    if (!t || !v) { out->count = 0; return; }
    out->times = t;
    out->values = v;
    out->stride = st;
    out->step = (smp->interpolation == cgltf_interpolation_type_step);
}

/* Où se trouve `time` dans la piste, et de combien on est entre deux clés. */
static uint32_t track_locate(const skin_track *tr, float time, float *frac)
{
    *frac = 0.0f;
    if (tr->count < 2) return 0;
    if (time <= tr->times[0]) return 0;
    if (time >= tr->times[tr->count - 1]) return tr->count - 1;
    /* Recherche dichotomique : cinquante-sept pistes fois soixante images par
     * seconde fois une recherche linéaire sur quarante-huit clés, ce serait
     * cent soixante mille comparaisons par seconde pour rien. */
    uint32_t lo = 0, hi = tr->count - 1;
    while (hi - lo > 1) {
        const uint32_t mid = (lo + hi) / 2;
        if (tr->times[mid] <= time) lo = mid; else hi = mid;
    }
    const float span = tr->times[hi] - tr->times[lo];
    if (span > 1e-9f) *frac = (time - tr->times[lo]) / span;
    return lo;
}

static ns_v3 track_v3(const skin_track *tr, float time, ns_v3 fallback)
{
    if (tr->count == 0 || tr->stride < 3) return fallback;
    float f = 0.0f;
    const uint32_t i = track_locate(tr, time, &f);
    const uint32_t j = (i + 1 < tr->count) ? i + 1 : i;
    const float *a = tr->values + (size_t)i * tr->stride;
    const float *b = tr->values + (size_t)j * tr->stride;
    if (tr->step) f = 0.0f;
    return ns_v3_make(a[0] + (b[0] - a[0]) * f,
                      a[1] + (b[1] - a[1]) * f,
                      a[2] + (b[2] - a[2]) * f);
}

static ns_quat track_quat(const skin_track *tr, float time, ns_quat fallback)
{
    if (tr->count == 0 || tr->stride < 4) return fallback;
    float f = 0.0f;
    const uint32_t i = track_locate(tr, time, &f);
    const uint32_t j = (i + 1 < tr->count) ? i + 1 : i;
    const float *a = tr->values + (size_t)i * 4;
    const float *b = tr->values + (size_t)j * 4;
    if (tr->step) f = 0.0f;

    ns_quat qa = { a[0], a[1], a[2], a[3] };
    ns_quat qb = { b[0], b[1], b[2], b[3] };
    /*
     * Le SIGNE, et il n'est pas optionnel : q et −q sont la même rotation, mais
     * interpoler entre les deux fait faire le tour long. Un exportateur n'a
     * aucune raison de garder les clés du même côté, et le symptôme — un membre
     * qui part à l'envers sur une image — est le bug d'animation classique.
     */
    const float dot = qa.x*qb.x + qa.y*qb.y + qa.z*qb.z + qa.w*qb.w;
    if (dot < 0.0f) { qb.x = -qb.x; qb.y = -qb.y; qb.z = -qb.z; qb.w = -qb.w; }

    /* Interpolation linéaire renormalisée plutôt qu'un vrai slerp : entre deux
     * clés à 24 Hz l'angle dépasse rarement quelques degrés, et l'écart de
     * vitesse angulaire y est sous le millième. */
    ns_quat q = { qa.x + (qb.x - qa.x) * f, qa.y + (qb.y - qa.y) * f,
                  qa.z + (qb.z - qa.z) * f, qa.w + (qb.w - qa.w) * f };
    const float l = SDL_sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (l > 1e-8f) { q.x /= l; q.y /= l; q.z /= l; q.w /= l; }
    else q = fallback;
    return q;
}

/* ------------------------------------------------------------------ chargement */

static int node_index(const cgltf_data *d, const cgltf_node *n)
{
    if (!n) return -1;
    return (int)(n - d->nodes);
}

ns_skin *ns_skin_load(const char *logical)
{
    char path[1024];
    if (!ns_path_resolve(logical, path, sizeof path)) {
        NS_WARN("personnage : « %s » introuvable", logical);
        return NULL;
    }

    cgltf_options opt;
    SDL_zero(opt);
    cgltf_data *d = NULL;
    if (cgltf_parse_file(&opt, path, &d) != cgltf_result_success) {
        NS_WARN("personnage : « %s » illisible", path);
        return NULL;
    }
    if (cgltf_load_buffers(&opt, d, path) != cgltf_result_success) {
        NS_WARN("personnage : « %s » — tampons illisibles", path);
        cgltf_free(d);
        return NULL;
    }
    if (d->skins_count == 0 || d->animations_count == 0 || d->meshes_count == 0) {
        NS_WARN("personnage : « %s » n'a pas de peau, d'animation ou de maillage "
                "(%zu / %zu / %zu)", path,
                (size_t)d->skins_count, (size_t)d->animations_count, (size_t)d->meshes_count);
        cgltf_free(d);
        return NULL;
    }

    ns_skin *s = SDL_calloc(1, sizeof *s);
    if (!s) { cgltf_free(d); return NULL; }
    s->data = d;

    /* --- le squelette --- */
    const cgltf_skin *sk = &d->skins[0];
    if (sk->joints_count > NS_SKIN_MAX_JOINTS) {
        NS_WARN("personnage : %zu os pour %d au maximum",
                (size_t)sk->joints_count, NS_SKIN_MAX_JOINTS);
        SDL_free(s); cgltf_free(d); return NULL;
    }
    s->joint_count = (int)sk->joints_count;
    for (int i = 0; i < s->joint_count; ++i) {
        s->joint_node[i] = node_index(d, sk->joints[i]);
        s->inverse_bind[i] = ns_m4_identity();
    }
    if (sk->inverse_bind_matrices) {
        uint32_t n = 0; uint8_t st = 0;
        const float *m = accessor_floats(sk->inverse_bind_matrices, &n, &st);
        if (m && st == 16) {
            for (int i = 0; i < s->joint_count && (uint32_t)i < n; ++i) {
                SDL_memcpy(s->inverse_bind[i].m, m + (size_t)i * 16, sizeof(float) * 16);
            }
        } else {
            NS_WARN("personnage : matrices de liaison inverse illisibles, "
                    "le maillage sortira déformé");
        }
    }

    /* --- la hiérarchie de nœuds, à plat --- */
    s->node_count = (int)d->nodes_count;
    s->node = SDL_calloc((size_t)s->node_count, sizeof *s->node);
    if (!s->node) { SDL_free(s); cgltf_free(d); return NULL; }
    for (int i = 0; i < s->node_count; ++i) {
        const cgltf_node *n = &d->nodes[i];
        skin_node *o = &s->node[i];
        o->parent = node_index(d, n->parent);
        o->t = n->has_translation ? ns_v3_make(n->translation[0], n->translation[1], n->translation[2])
                                  : ns_v3_zero();
        o->s = n->has_scale ? ns_v3_make(n->scale[0], n->scale[1], n->scale[2])
                            : ns_v3_make(1.0f, 1.0f, 1.0f);
        if (n->has_rotation) { o->r.x = n->rotation[0]; o->r.y = n->rotation[1];
                               o->r.z = n->rotation[2]; o->r.w = n->rotation[3]; }
        else                 { o->r.x = o->r.y = o->r.z = 0.0f; o->r.w = 1.0f; }
        /*
         * `has_matrix` est ignoré ET signalé. Un nœud qui porte une matrice au
         * lieu d'un triplet ne s'anime pas dans glTF — les canaux ciblent T, R
         * et S. Le rencontrer voudrait dire que le fichier n'est pas ce qu'on
         * croit, et le décomposer en silence donnerait une pose fausse sans
         * rien pour l'expliquer.
         */
        if (n->has_matrix) {
            SDL_memcpy(o->fixed.m, n->matrix, sizeof(float) * 16);
            o->has_fixed = true;
        }
    }

    /* --- l'animation : on prend la première --- */
    const cgltf_animation *an = &d->animations[0];
    for (cgltf_size c = 0; c < an->channels_count; ++c) {
        const cgltf_animation_channel *ch = &an->channels[c];
        const int ni = node_index(d, ch->target_node);
        if (ni < 0 || ni >= s->node_count) continue;
        skin_node *o = &s->node[ni];
        /* glTF l'interdit, et pour cause : on ne sait pas quoi appliquer en
         * premier. Le rencontrer veut dire que le fichier n'est pas ce qu'on
         * croit — on le DIT plutôt que de choisir au hasard. */
        if (o->has_fixed) {
            NS_WARN("personnage : le nœud « %s » porte une matrice ET une "
                    "animation — glTF l'interdit, l'animation est ignorée",
                    ch->target_node->name ? ch->target_node->name : "?");
            continue;
        }
        switch (ch->target_path) {
        case cgltf_animation_path_type_translation: read_track(ch->sampler, &o->track_t); break;
        case cgltf_animation_path_type_rotation:    read_track(ch->sampler, &o->track_r); break;
        case cgltf_animation_path_type_scale:       read_track(ch->sampler, &o->track_s); break;
        default: break;
        }
    }
    for (int i = 0; i < s->node_count; ++i) {
        const skin_node *o = &s->node[i];
        const skin_track *tr[3] = { &o->track_t, &o->track_r, &o->track_s };
        for (int k = 0; k < 3; ++k) {
            if (tr[k]->count > 0) {
                const float end = tr[k]->times[tr[k]->count - 1];
                if (end > s->duration) s->duration = end;
            }
        }
    }
    if (s->duration <= 0.0f) s->duration = 1.0f;

    /* --- le maillage : la première primitive qui a une peau --- */
    const cgltf_primitive *prim = NULL;
    for (cgltf_size m = 0; m < d->meshes_count && !prim; ++m) {
        for (cgltf_size p = 0; p < d->meshes[m].primitives_count; ++p) {
            const cgltf_primitive *pp = &d->meshes[m].primitives[p];
            bool has_joints = false, has_weights = false;
            for (cgltf_size a = 0; a < pp->attributes_count; ++a) {
                if (pp->attributes[a].type == cgltf_attribute_type_joints)  has_joints = true;
                if (pp->attributes[a].type == cgltf_attribute_type_weights) has_weights = true;
            }
            if (has_joints && has_weights) { prim = pp; break; }
        }
    }
    if (!prim) {
        NS_WARN("personnage : aucune primitive pesée dans « %s »", path);
        ns_skin_free(s);
        return NULL;
    }

    const cgltf_accessor *pos = NULL, *nrm = NULL, *uv = NULL, *jnt = NULL, *wgt = NULL;
    for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
        const cgltf_attribute *at = &prim->attributes[a];
        if (at->index != 0) continue;
        switch (at->type) {
        case cgltf_attribute_type_position: pos = at->data; break;
        case cgltf_attribute_type_normal:   nrm = at->data; break;
        case cgltf_attribute_type_texcoord: uv  = at->data; break;
        case cgltf_attribute_type_joints:   jnt = at->data; break;
        case cgltf_attribute_type_weights:  wgt = at->data; break;
        default: break;
        }
    }
    if (!pos || !jnt || !wgt) {
        NS_WARN("personnage : attributs manquants");
        ns_skin_free(s);
        return NULL;
    }

    s->vert_count = (uint32_t)pos->count;
    s->verts = SDL_calloc(s->vert_count, sizeof *s->verts);
    if (!s->verts) { ns_skin_free(s); return NULL; }

    float lo = FLT_MAX, hi = -FLT_MAX;
    for (uint32_t i = 0; i < s->vert_count; ++i) {
        ns_skin_vertex *v = &s->verts[i];
        float tmp[4];
        cgltf_accessor_read_float(pos, i, tmp, 3);
        v->position[0] = tmp[0]; v->position[1] = tmp[1]; v->position[2] = tmp[2];
        if (tmp[1] < lo) lo = tmp[1];
        if (tmp[1] > hi) hi = tmp[1];

        if (nrm) { cgltf_accessor_read_float(nrm, i, tmp, 3);
                   v->normal[0] = tmp[0]; v->normal[1] = tmp[1]; v->normal[2] = tmp[2]; }
        else     { v->normal[1] = 1.0f; }

        if (uv)  { cgltf_accessor_read_float(uv, i, tmp, 2);
                   v->uv[0] = tmp[0]; v->uv[1] = tmp[1]; }

        /* Les indices d'os sont entiers — u8 ou u16 selon l'exportateur — et
         * `read_uint` les normalise pour nous. */
        cgltf_uint ji[4] = { 0, 0, 0, 0 };
        cgltf_accessor_read_uint(jnt, i, ji, 4);
        cgltf_accessor_read_float(wgt, i, tmp, 4);
        float sum = 0.0f;
        for (int k = 0; k < NS_SKIN_INFLUENCES; ++k) {
            v->joints[k] = (uint8_t)((ji[k] < NS_SKIN_MAX_JOINTS) ? ji[k] : 0);
            v->weights[k] = tmp[k];
            sum += tmp[k];
        }
        /* Renormaliser : un exportateur qui quantifie les poids en octets laisse
         * une somme à 0,996, et l'erreur se voit comme un maillage qui « respire ».
         * Une somme nulle — un sommet non pesé — est rattachée à l'os 0 : sans
         * ça il resterait à l'origine du monde et tirerait un triangle en travers
         * de l'écran, ce qui est le défaut le plus voyant qui soit. */
        if (sum > 1e-6f) { for (int k = 0; k < NS_SKIN_INFLUENCES; ++k) v->weights[k] /= sum; }
        else             { v->weights[0] = 1.0f; v->joints[0] = 0; }
    }
    /* Mesurée plus bas, une fois la pose de liaison appliquée : voir
     * `measure_rest_height`. Les extrêmes bruts ne veulent rien dire. */
    (void)lo; (void)hi;

    if (prim->indices) {
        s->index_count = (uint32_t)prim->indices->count;
        s->indices = SDL_calloc(s->index_count, sizeof *s->indices);
        if (!s->indices) { ns_skin_free(s); return NULL; }
        for (uint32_t i = 0; i < s->index_count; ++i) {
            s->indices[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
        }
    } else {
        s->index_count = s->vert_count;
        s->indices = SDL_calloc(s->index_count, sizeof *s->indices);
        if (!s->indices) { ns_skin_free(s); return NULL; }
        for (uint32_t i = 0; i < s->index_count; ++i) s->indices[i] = i;
    }

    /* --- l'image --- */
    if (prim->material && prim->material->has_pbr_metallic_roughness) {
        const cgltf_texture *tex = prim->material->pbr_metallic_roughness.base_color_texture.texture;
        if (tex && tex->image && tex->image->buffer_view) {
            const cgltf_buffer_view *bv = tex->image->buffer_view;
            const uint8_t *base = (const uint8_t *)bv->buffer->data;
            if (base) { s->image = base + bv->offset; s->image_size = bv->size; }
        }
    }

    /*
     * LA HAUTEUR, mesurée SOUS SA POSE et non sur ses sommets bruts.
     *
     * Les sommets d'un maillage pesé sont donnés dans le repère de la peau, qui
     * n'a aucune raison d'être celui du monde — sur ce personnage-ci, deux
     * nœuds portent une matrice, dont un changement de repère Z-haut vers
     * Y-haut. Mesurer l'étendue en Y des sommets bruts mesure donc une
     * PROFONDEUR, et l'appeler « hauteur » donne un facteur d'échelle faux.
     *
     * Constaté à l'image avant d'être compris : le personnage sortait deux fois
     * trop grand et dominait les bornes. La bonne mesure est celle que le
     * shader fera — appliquer les matrices d'os — donc on la fait pour de vrai,
     * une fois, au chargement.
     */
    {
        ns_m4 pose[NS_SKIN_MAX_JOINTS];
        ns_skin_pose(s, 0.0f, pose, NS_SKIN_MAX_JOINTS);
        float ylo = FLT_MAX, yhi = -FLT_MAX;
        for (uint32_t i = 0; i < s->vert_count; ++i) {
            const ns_skin_vertex *v = &s->verts[i];
            float y = 0.0f;
            for (int k = 0; k < NS_SKIN_INFLUENCES; ++k) {
                const float w = v->weights[k];
                if (w <= 0.0f) continue;
                const ns_m4 *m = &pose[v->joints[k]];
                y += w * (m->m[0][1] * v->position[0] + m->m[1][1] * v->position[1] +
                          m->m[2][1] * v->position[2] + m->m[3][1]);
            }
            if (y < ylo) ylo = y;
            if (y > yhi) yhi = y;
        }
        s->rest_height = (yhi > ylo) ? (yhi - ylo) : 1.0f;
    }

    /*
     * LA POSE DE PASSAGE, mesurée. Voir `ns_skin_stand_time`.
     *
     * On repère d'abord les deux os les plus BAS de la pose de liaison — ce
     * sont les pieds, quel que soit leur nom dans le fichier — puis on balaie
     * le cycle et on retient l'instant où ils sont le plus proches
     * horizontalement, jambes rassemblées.
     */
    {
        ns_m4 pose[NS_SKIN_MAX_JOINTS];
        ns_skin_pose(s, 0.0f, pose, NS_SKIN_MAX_JOINTS);
        int pied_a = -1, pied_b = -1;
        float ya = FLT_MAX, yb = FLT_MAX;
        for (int j = 0; j < s->joint_count; ++j) {
            /* La translation d'un os EN MONDE, une fois la liaison défaite :
             * c'est la colonne de translation de world = pose * bind. */
            const ns_m4 bind = ns_m4_inverse(s->inverse_bind[j]);
            const ns_m4 w = ns_m4_mul(pose[j], bind);
            const float y = w.m[3][1];
            if (y < ya) { yb = ya; pied_b = pied_a; ya = y; pied_a = j; }
            else if (y < yb) { yb = y; pied_b = j; }
        }
        s->stand_time = 0.0f;
        if (pied_a >= 0 && pied_b >= 0) {
            float best = FLT_MAX;
            for (int k = 0; k < 96; ++k) {
                const float t = s->duration * (float)k / 96.0f;
                ns_skin_pose(s, t, pose, NS_SKIN_MAX_JOINTS);
                const ns_m4 wa = ns_m4_mul(pose[pied_a], ns_m4_inverse(s->inverse_bind[pied_a]));
                const ns_m4 wb = ns_m4_mul(pose[pied_b], ns_m4_inverse(s->inverse_bind[pied_b]));
                const float dx = wa.m[3][0] - wb.m[3][0];
                const float dz = wa.m[3][2] - wb.m[3][2];
                const float ecart = dx * dx + dz * dz;
                if (ecart < best) { best = ecart; s->stand_time = t; }
            }
        }
    }

    NS_INFO("personnage « %s » : %u sommets, %u triangles, %d os, %.2f s d'animation, "
            "%.2f de haut au repos",
            logical, s->vert_count, s->index_count / 3u, s->joint_count,
            (double)s->duration, (double)s->rest_height);
    NS_INFO("personnage : pose de passage mesurée à %.3f s du cycle",
            (double)s->stand_time);
    return s;
}

void ns_skin_free(ns_skin *s)
{
    if (!s) return;
    SDL_free(s->verts);
    SDL_free(s->indices);
    SDL_free(s->node);
    if (s->data) cgltf_free(s->data);
    SDL_free(s->bytes);
    SDL_free(s);
}

const ns_skin_vertex *ns_skin_vertices(const ns_skin *s, uint32_t *count)
{
    if (count) *count = s ? s->vert_count : 0;
    return s ? s->verts : NULL;
}

const uint32_t *ns_skin_indices(const ns_skin *s, uint32_t *count)
{
    if (count) *count = s ? s->index_count : 0;
    return s ? s->indices : NULL;
}

int   ns_skin_joint_count(const ns_skin *s) { return s ? s->joint_count : 0; }
float ns_skin_duration(const ns_skin *s)    { return s ? s->duration : 0.0f; }
float ns_skin_rest_height(const ns_skin *s) { return s ? s->rest_height : 1.0f; }
float ns_skin_stand_time(const ns_skin *s)  { return s ? s->stand_time : 0.0f; }

const void *ns_skin_image(const ns_skin *s, size_t *size)
{
    if (size) *size = s ? s->image_size : 0;
    return s ? s->image : NULL;
}

void ns_skin_pose(const ns_skin *s, float time, ns_m4 *out, int max)
{
    if (!s || !out) return;

    /* Bouclée : un cycle de marche n'a de sens qu'en boucle, et `fmodf` d'un
     * négatif rend un négatif — d'où le rattrapage. */
    float t = SDL_fmodf(time, s->duration);
    if (t < 0.0f) t += s->duration;

    /*
     * Les matrices LOCALES d'abord, puis la composition descendante.
     *
     * On peut se permettre une simple boucle croissante plutôt qu'un parcours
     * d'arbre parce que glTF garantit qu'un nœud précède ses enfants dans le
     * tableau… ce qui est FAUX, la spécification ne le garantit pas. On fait
     * donc le calcul en remontant les parents pour chaque os : la profondeur
     * d'un squelette humanoïde est de six, et dix-neuf os font cent quatorze
     * produits — moins que ce qu'un tri topologique coûterait à écrire et à
     * relire.
     */
    ns_m4 local[256];
    const int n = (s->node_count < 256) ? s->node_count : 256;
    for (int i = 0; i < n; ++i) {
        const skin_node *o = &s->node[i];
        if (o->has_fixed) { local[i] = o->fixed; continue; }
        const ns_v3   tr = track_v3(&o->track_t, t, o->t);
        const ns_quat rt = track_quat(&o->track_r, t, o->r);
        const ns_v3   sc = track_v3(&o->track_s, t, o->s);
        local[i] = ns_m4_trs(tr, rt, sc);
    }

    const int count = (s->joint_count < max) ? s->joint_count : max;
    for (int j = 0; j < count; ++j) {
        ns_m4 world = ns_m4_identity();
        int chain[64];
        int depth = 0;
        for (int i = s->joint_node[j]; i >= 0 && i < n && depth < 64; i = s->node[i].parent) {
            chain[depth++] = i;
        }
        for (int k = depth - 1; k >= 0; --k) world = ns_m4_mul(world, local[chain[k]]);
        out[j] = ns_m4_mul(world, s->inverse_bind[j]);
    }
}
