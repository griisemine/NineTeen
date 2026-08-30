/* Voir ns_skin.h pour ce que fait ce module et pourquoi il existe. */
#include "ns_skin.h"

#include "ns_core.h"

#include "cgltf.h"

#include <SDL3/SDL.h>

#include <float.h>
#include <string.h>

/* Le nombre de crans où la remontée du bassin est mesurée. Neuf suffisent :
 * la courbe est un cosinus, et l'interpolation linéaire entre deux crans
 * distants d'un huitième s'écarte de moins d'un millimètre. */
#define NS_SKIN_ACCROUPI_CRANS 9

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
    float           sweep_radius;
    float           half_width;
    float           stride_length;
    float           forward_angle;
    float           stand_time;

    /*
     * DE QUOI DÉRIVER LES ALLURES QUE LE FICHIER N'A PAS. Tout est MESURÉ au
     * chargement : aucun nom d'os, aucune convention d'exportateur.
     */
    ns_v3           axe_lateral;    /* l'axe des épaules, perpendiculaire à la marche */
    int             pied[2];        /* les deux pieds, un par jambe */
    int             hanche[2];      /* NŒUD de la hanche de chaque jambe */
    int             buste_noeud;    /* NŒUD du premier os du buste, au-dessus du bassin */
    int8_t          etage[NS_SKIN_MAX_JOINTS];  /* -1 hors jambe, 0 cuisse, 1 mollet, 2 pied */
    int8_t          jambe[NS_SKIN_MAX_JOINTS];  /* -1, 0 ou 1 */
    bool            suit_buste[NS_SKIN_MAX_JOINTS];
    float           sens_avant;     /* +1 ou -1 : le signe qui envoie le genou DEVANT */
    bool            accroupi_pret;
    float           accroupi_angle; /* l'angle de cuisse à plein accroupi, en radians */
    /* La REMONTÉE du point le plus bas, par cran d'accroupi. C'est elle qu'on
     * retranche pour reposer les pieds au sol : plier les jambes SOULÈVE les
     * pieds sous un bassin qui, lui, ne bouge pas. Mesurée et non calculée —
     * elle dépend de la longueur des segments du modèle. */
    float           accroupi_remontee[NS_SKIN_ACCROUPI_CRANS];

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

/* Déclarée ici : la mesure de chargement pose déjà des matrices monde, et la
 * définition vit plus bas avec le reste de la pose. */
static void poser_mondes(const ns_skin *s, float t, ns_m4 *world, int count);

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
     * LES PIEDS : la pose de passage, la FOULÉE, et l'AXE DU PERSONNAGE.
     *
     * On repère d'abord les deux os les plus BAS de la pose de liaison — ce sont
     * les pieds, quel que soit leur nom dans le fichier — puis on balaie le
     * cycle une seule fois pour les trois mesures.
     *
     * LA FOULÉE, et pourquoi elle ne se lit PAS sur un seul pied
     * ----------------------------------------------------------
     * Un cycle de marche est animé SUR PLACE : le bassin ne translate pas, ce
     * sont les pieds qui vont et viennent sous lui. Pendant son APPUI, un pied
     * est cloué au sol : dans le repère du personnage il recule donc exactement
     * de ce dont le corps avance. La distance parcourue en un cycle est par
     * conséquent la somme des reculs du pied PORTEUR, celui-ci changeant deux
     * fois par cycle.
     *
     * La mesure évidente — l'aller-retour d'un seul pied — donne la moitié de
     * ça, et c'est un piège dans lequel on est tombé avant de mesurer : un pied
     * part d'un demi-pas devant le corps et finit un demi-pas derrière, son
     * excursion vaut donc UN PAS et non une foulée. Sur ce modèle, 1,02 m au
     * lieu de 2,06 — un facteur deux, et un personnage qui court sur place.
     *
     * On INTÈGRE donc, en prenant à chaque instant le pied le plus bas comme
     * porteur. Aucun seuil de contact n'entre là-dedans, et c'est ce qui rend la
     * mesure robuste : « lequel des deux est le plus bas » est une question sans
     * réglage, alors que « ce pied touche-t-il le sol » en demande un que
     * personne ne sait poser pour tous les modèles.
     *
     * On somme des VECTEURS et on prend la longueur du total, pas la somme des
     * longueurs. La différence n'est pas cosmétique : sommer des longueurs
     * compterait aussi le tremblement d'un pied posé, et gonflerait la foulée
     * d'un cycle bruité. Le total vectoriel, lui, pointe vers l'ARRIÈRE du
     * personnage — ce qui donne du même coup son axe, MESURÉ, sans nom d'os et
     * sans convention d'exportateur. La perpendiculaire est l'axe des épaules,
     * et c'est de celui-là qu'on tire la largeur qui compte à l'écran.
     */
    ns_v3 axe_lateral = ns_v3_make(1.0f, 0.0f, 0.0f);
    {
        ns_m4 pose[NS_SKIN_MAX_JOINTS];
        ns_skin_pose(s, 0.0f, pose, NS_SKIN_MAX_JOINTS);

        /*
         * DEUX PIEDS, un par JAMBE — et c'est plus subtil qu'il n'y paraît.
         *
         * « Les deux os les plus bas » ne marche pas : sur ce squelette-ci, les
         * quatre os les plus bas sont la cheville ET l'orteil de CHAQUE jambe.
         * Prendre les deux plus bas peut donc prendre deux fois la même jambe,
         * auquel cas l'un est toujours sous l'autre, le pied porteur ne change
         * jamais, et l'intégration d'un cycle bouclé rend exactement zéro.
         *
         * « Le plus bas, écarté horizontalement du premier » ne marche pas non
         * plus, et c'est le piège suivant : une cheville et son orteil sont
         * écartés horizontalement, eux aussi. Vers l'AVANT, pas sur le côté — et
         * on ne connaît pas encore l'axe du personnage, c'est justement ce que
         * ce bloc cherche.
         *
         * Ce qui marche, et qui ne demande ni axe ni seuil : le pied opposé est
         * celui qui MONTE quand l'autre descend. On prend donc, parmi les os
         * bas, celui dont la hauteur est la plus ANTI-CORRÉLÉE à celle du
         * premier sur le cycle. C'est la définition même d'une démarche — les
         * deux jambes sont en opposition de phase — et un orteil de la même
         * jambe est au contraire fortement corrélé, donc écarté d'office.
         */
        int pied_a = -1, pied_b = -1;
        float ya = FLT_MAX;
        for (int j = 0; j < s->joint_count; ++j) {
            /* La translation d'un os EN MONDE, une fois la liaison défaite :
             * c'est la colonne de translation de world = pose * bind. */
            const ns_m4 bind = ns_m4_inverse(s->inverse_bind[j]);
            const ns_m4 w = ns_m4_mul(pose[j], bind);
            if (w.m[3][1] < ya) { ya = w.m[3][1]; pied_a = j; }
        }
        if (pied_a >= 0) {
            /* Les hauteurs de chaque os sur le cycle, centrées. Seuls les os
             * BAS sont candidats : une main descend aussi quand un pied monte,
             * et elle est parfaitement anti-corrélée. Le quart inférieur de la
             * hauteur du personnage est large pour un pied et exclut le genou,
             * qui est à mi-cuisse. */
            enum { ECH = 64 };
            static float hy[NS_SKIN_MAX_JOINTS][ECH];
            bool candidat[NS_SKIN_MAX_JOINTS];
            const float plafond = ya + s->rest_height * 0.25f;
            for (int j = 0; j < s->joint_count; ++j) {
                const ns_m4 bind = ns_m4_inverse(s->inverse_bind[j]);
                const ns_m4 w = ns_m4_mul(pose[j], bind);
                candidat[j] = (j != pied_a) && (w.m[3][1] <= plafond);
            }
            for (int k = 0; k < ECH; ++k) {
                ns_m4 p[NS_SKIN_MAX_JOINTS];
                ns_skin_pose(s, s->duration * (float)k / (float)ECH, p, NS_SKIN_MAX_JOINTS);
                for (int j = 0; j < s->joint_count; ++j) {
                    const ns_m4 bind = ns_m4_inverse(s->inverse_bind[j]);
                    hy[j][k] = ns_m4_mul(p[j], bind).m[3][1];
                }
            }
            for (int j = 0; j < s->joint_count; ++j) {
                float moy = 0.0f;
                for (int k = 0; k < ECH; ++k) moy += hy[j][k];
                moy /= (float)ECH;
                for (int k = 0; k < ECH; ++k) hy[j][k] -= moy;
            }
            float pire = 0.0f;   /* on ne retient qu'une corrélation NÉGATIVE */
            for (int j = 0; j < s->joint_count; ++j) {
                if (!candidat[j]) continue;
                float num = 0.0f, na = 0.0f, nb = 0.0f;
                for (int k = 0; k < ECH; ++k) {
                    num += hy[pied_a][k] * hy[j][k];
                    na  += hy[pied_a][k] * hy[pied_a][k];
                    nb  += hy[j][k] * hy[j][k];
                }
                if (na < 1e-9f || nb < 1e-9f) continue;
                const float r = num / sqrtf(na * nb);
                if (r < pire) { pire = r; pied_b = j; }
            }
        }
        s->pied[0] = pied_a;
        s->pied[1] = pied_b;
        s->stand_time = 0.0f;
        s->stride_length = 0.0f;
        s->forward_angle = NS_PI * 0.5f;
        if (pied_a >= 0 && pied_b >= 0) {
            /*
             * 128 échantillons : le cycle fait deux secondes, donc un tous les
             * 16 ms. L'appui dure environ une demi-seconde, soit trente
             * échantillons — largement de quoi que l'instant du changement de
             * pied porteur soit trouvé à mieux qu'un pour cent de la foulée.
             */
            enum { PAS = 128 };
            static float px[2][PAS], py[2][PAS], pz[2][PAS];
            const int pied[2] = { pied_a, pied_b };
            const ns_m4 bind[2] = { ns_m4_inverse(s->inverse_bind[pied_a]),
                                    ns_m4_inverse(s->inverse_bind[pied_b]) };

            float best = FLT_MAX;
            for (int k = 0; k < PAS; ++k) {
                const float t = s->duration * (float)k / (float)PAS;
                ns_skin_pose(s, t, pose, NS_SKIN_MAX_JOINTS);
                for (int f = 0; f < 2; ++f) {
                    const ns_m4 w = ns_m4_mul(pose[pied[f]], bind[f]);
                    px[f][k] = w.m[3][0];
                    py[f][k] = w.m[3][1];
                    pz[f][k] = w.m[3][2];
                }
                const float dx = px[0][k] - px[1][k];
                const float dz = pz[0][k] - pz[1][k];
                const float ecart = dx * dx + dz * dz;
                if (ecart < best) { best = ecart; s->stand_time = t; }
            }

            /* L'intégration. Le cycle est bouclé, donc le dernier échantillon
             * revient au premier — sans ce modulo il manquerait un pas de la
             * foulée, et l'erreur serait de moins d'un pour cent, c'est-à-dire
             * invisible et fausse. */
            float sx = 0.0f, sz = 0.0f;
            for (int k = 0; k < PAS; ++k) {
                const int n = (k + 1) % PAS;
                const int f = (py[0][k] < py[1][k]) ? 0 : 1;
                sx += px[f][n] - px[f][k];
                sz += pz[f][n] - pz[f][k];
            }
            const float total = sqrtf(sx * sx + sz * sz);
            s->stride_length = total;
            /* Une foulée nulle veut dire que le porteur n'a jamais changé, donc
             * que les deux « pieds » n'en sont pas. On le DIT : l'appelant
             * retombera sur sa valeur par défaut, et il vaut mieux qu'il sache
             * pourquoi. */
            if (total <= 1e-4f) {
                NS_WARN("personnage : foulée non mesurable (le pied porteur ne "
                        "change jamais) — la valeur par défaut s'appliquera");
            }

            /* L'axe. Un cycle où les pieds ne bougeraient pas — une pose figée
             * exportée comme animation — laisserait l'axe par défaut plutôt
             * qu'une direction tirée d'un bruit d'arrondi. */
            if (total > 1e-4f) {
                const float inv = 1.0f / total;
                /* La perpendiculaire horizontale : l'axe des épaules. Son SIGNE
                 * est indifférent, on n'en prend que des valeurs absolues. */
                axe_lateral = ns_v3_make(-sz * inv, 0.0f, sx * inv);
                s->axe_lateral = axe_lateral;
                /* L'AVANT est l'opposé du recul du pied porteur, et son signe,
                 * lui, compte : c'est ce qui décide de quel côté le personnage
                 * regarde. */
                s->forward_angle = atan2f(-sz, -sx);
            }
        }
    }

    /*
     * LES DEUX JAMBES ET LE BUSTE, repérés par la TOPOLOGIE.
     *
     * C'est ce qui permet de dériver un accroupi d'un cycle qui n'en contient
     * pas, sans lire un seul nom d'os. Le chemin est court et chaque pas se
     * justifie tout seul :
     *
     *   - le BASSIN est le premier ancêtre COMMUN aux deux pieds. Sur un
     *     bipède, il n'y a pas d'autre candidat ;
     *   - la HANCHE de chaque jambe est l'enfant du bassin sur le chemin de
     *     son pied ;
     *   - l'ÉTAGE d'un os est sa profondeur sous sa hanche : 0 la cuisse, 1 le
     *     mollet, 2 et au-delà le pied et ce qu'il porte. C'est exactement le
     *     découpage dont les trois rotations de `plier_jambes` ont besoin ;
     *   - le BUSTE est l'autre enfant du bassin — celui qui n'est ni l'une ni
     *     l'autre hanche.
     *
     * Le SIGNE de la flexion n'est pas deviné non plus. La vitesse d'un point
     * tourné autour d'un axe est `axe x (point - pivot)` ; on la projette sur
     * l'AVANT mesuré, et on garde le signe qui envoie le genou devant. Un
     * modèle exporté dans l'autre sens plie donc du bon côté sans qu'on ait
     * rien à régler.
     */
    for (int j = 0; j < NS_SKIN_MAX_JOINTS; ++j) {
        s->etage[j] = -1;
        s->jambe[j] = -1;
        s->suit_buste[j] = false;
    }
    s->hanche[0] = s->hanche[1] = -1;
    s->buste_noeud = -1;
    s->sens_avant = 1.0f;

    if (s->pied[0] >= 0 && s->pied[1] >= 0) {
        int bassin = -1;
        {
            int chaine[64], profond = 0;
            for (int i = s->joint_node[s->pied[0]];
                 i >= 0 && i < s->node_count && profond < 64; i = s->node[i].parent) {
                chaine[profond++] = i;
            }
            for (int i = s->joint_node[s->pied[1]]; i >= 0 && i < s->node_count && bassin < 0;
                 i = s->node[i].parent) {
                for (int k = 0; k < profond; ++k) if (chaine[k] == i) { bassin = i; break; }
            }
        }
        for (int f = 0; f < 2 && bassin >= 0; ++f) {
            for (int i = s->joint_node[s->pied[f]]; i >= 0 && i < s->node_count;
                 i = s->node[i].parent) {
                if (s->node[i].parent == bassin) { s->hanche[f] = i; break; }
            }
        }
        if (s->hanche[0] >= 0 && s->hanche[1] >= 0 && s->hanche[0] != s->hanche[1]) {
            for (int j = 0; j < s->joint_count; ++j) {
                int niveau = 0;
                for (int i = s->joint_node[j]; i >= 0 && i < s->node_count && niveau < 64;
                     i = s->node[i].parent, ++niveau) {
                    if (i == s->hanche[0] || i == s->hanche[1]) {
                        s->jambe[j] = (int8_t)((i == s->hanche[0]) ? 0 : 1);
                        s->etage[j] = (int8_t)((niveau > 2) ? 2 : niveau);
                        break;
                    }
                }
            }
            /* Le buste : l'enfant du bassin qui n'est aucune des deux hanches. */
            for (int j = 0; j < s->joint_count && s->buste_noeud < 0; ++j) {
                for (int i = s->joint_node[j]; i >= 0 && i < s->node_count;
                     i = s->node[i].parent) {
                    if (s->node[i].parent != bassin) continue;
                    if (i != s->hanche[0] && i != s->hanche[1]) { s->buste_noeud = i; }
                    break;
                }
            }
            for (int j = 0; j < s->joint_count && s->buste_noeud >= 0; ++j) {
                for (int i = s->joint_node[j]; i >= 0 && i < s->node_count;
                     i = s->node[i].parent) {
                    if (i == s->buste_noeud) { s->suit_buste[j] = true; break; }
                }
            }

            /* Le signe, mesuré sur la pose de passage. */
            ns_m4 pose[NS_SKIN_MAX_JOINTS];
            poser_mondes(s, s->stand_time, pose, s->joint_count);
            int cuisse = -1, mollet = -1;
            for (int j = 0; j < s->joint_count; ++j) {
                if (s->jambe[j] != 0) continue;
                if (s->etage[j] == 0) cuisse = j;
                else if (s->etage[j] == 1 && mollet < 0) mollet = j;
            }
            if (cuisse >= 0 && mollet >= 0) {
                const ns_v3 ph = ns_v3_make(pose[cuisse].m[3][0], pose[cuisse].m[3][1],
                                            pose[cuisse].m[3][2]);
                const ns_v3 pg = ns_v3_make(pose[mollet].m[3][0], pose[mollet].m[3][1],
                                            pose[mollet].m[3][2]);
                const ns_v3 v = ns_v3_cross(axe_lateral, ns_v3_sub(pg, ph));
                const ns_v3 avant = ns_v3_make(SDL_cosf(s->forward_angle), 0.0f,
                                               SDL_sinf(s->forward_angle));
                s->sens_avant = (ns_v3_dot(v, avant) >= 0.0f) ? 1.0f : -1.0f;
            }
        }
    }
    if (s->hanche[0] < 0 || s->hanche[1] < 0) {
        NS_WARN("personnage : les deux jambes n'ont pas été repérées dans le squelette — "
                "l'accroupi restera debout");
    }

    /*
     * LES DEUX RAYONS, mesurés sur TOUT LE CYCLE et non sur la seule pose de
     * liaison.
     *
     * Même précaution que pour la hauteur : on mesure ce que le shader fera,
     * donc sous la pose. Mais ici on balaie l'animation entière, parce qu'un
     * bras qui balance sort de la silhouette au repos et que c'est le
     * personnage QUI MARCHE qu'on regarde.
     *
     * L'axe de référence est x = z = 0 du repère du personnage, parce que
     * c'est là que `room/main.c` pose ses PIEDS : `ns_m4_trs(sol, ...)` envoie
     * l'origine du modèle sur le sol sous le joueur. Mesurer autour d'un autre
     * axe donnerait un rayon juste et inutilisable.
     *
     * DEUX rayons, et c'est le point : ils ne servent pas à la même chose.
     *
     *   - `sweep_radius` est le rayon du CYLINDRE qui contient tout le
     *     personnage en mouvement, main tendue comprise. C'est le volume dans
     *     lequel une caméra ne doit jamais entrer, quelle que soit la direction
     *     d'où elle vient.
     *   - `half_width` est la demi-largeur EN TRAVERS, sur l'axe des épaules.
     *     C'est elle qui décide de la place prise à l'écran, parce qu'on
     *     regarde le personnage de dos : un bras balancé vers l'avant est
     *     caché derrière le corps, il ne fait pas un pixel de plus.
     *
     * Les confondre donne un personnage transparent en permanence, la mesure
     * brute valant ici le double de la demi-largeur.
     *
     * Vingt-quatre instants : le cycle fait deux secondes, donc un échantillon
     * toutes les 83 ms. L'épaule d'un humanoïde ne parcourt pas plus d'un
     * centimètre en 83 ms à cadence de marche, et l'erreur de mesure est donc
     * sous le centimètre — largement sous la marge du plan proche qui s'y
     * ajoute plus loin.
     */
    {
        ns_m4 pose[NS_SKIN_MAX_JOINTS];
        float r2max = 0.0f, lmax = 0.0f;
        for (int k = 0; k < 24; ++k) {
            ns_skin_pose(s, s->duration * (float)k / 24.0f, pose, NS_SKIN_MAX_JOINTS);
            for (uint32_t i = 0; i < s->vert_count; ++i) {
                const ns_skin_vertex *v = &s->verts[i];
                float x = 0.0f, z = 0.0f;
                for (int b = 0; b < NS_SKIN_INFLUENCES; ++b) {
                    const float w = v->weights[b];
                    if (w <= 0.0f) continue;
                    const ns_m4 *m = &pose[v->joints[b]];
                    x += w * (m->m[0][0] * v->position[0] + m->m[1][0] * v->position[1] +
                              m->m[2][0] * v->position[2] + m->m[3][0]);
                    z += w * (m->m[0][2] * v->position[0] + m->m[1][2] * v->position[1] +
                              m->m[2][2] * v->position[2] + m->m[3][2]);
                }
                const float r2 = x * x + z * z;
                if (r2 > r2max) r2max = r2;
                const float l = SDL_fabsf(x * axe_lateral.x + z * axe_lateral.z);
                if (l > lmax) lmax = l;
            }
        }
        s->sweep_radius = sqrtf(r2max);
        s->half_width   = lmax;
    }

    NS_INFO("personnage « %s » : %u sommets, %u triangles, %d os, %.2f s d'animation, "
            "%.2f de haut au repos",
            logical, s->vert_count, s->index_count / 3u, s->joint_count,
            (double)s->duration, (double)s->rest_height);
    NS_INFO("personnage : pose de passage mesurée à %.3f s du cycle, "
            "rayon balayé %.3f, demi-largeur %.3f, foulée %.3f (unités du fichier), "
            "avant à %.1f degrés",
            (double)s->stand_time, (double)s->sweep_radius,
            (double)s->half_width, (double)s->stride_length,
            (double)(s->forward_angle / NS_DEG2RAD));
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
float ns_skin_sweep_radius(const ns_skin *s) { return s ? s->sweep_radius : 0.0f; }
float ns_skin_half_width(const ns_skin *s)  { return s ? s->half_width : 0.0f; }
float ns_skin_stride_length(const ns_skin *s) { return s ? s->stride_length : 0.0f; }
/* Un quart de tour par défaut : +Z, ce que glTF recommande pour un personnage. */
float ns_skin_forward_angle(const ns_skin *s)
{
    return s ? s->forward_angle : (NS_PI * 0.5f);
}
float ns_skin_stand_time(const ns_skin *s)  { return s ? s->stand_time : 0.0f; }

const void *ns_skin_image(const ns_skin *s, size_t *size)
{
    if (size) *size = s ? s->image_size : 0;
    return s ? s->image : NULL;
}

/* ------------------------------------------------------------------- pose */

/*
 * Les matrices MONDE de chaque articulation, avant la liaison inverse.
 *
 * On peut se permettre une simple boucle croissante plutôt qu'un parcours
 * d'arbre parce que glTF garantit qu'un nœud précède ses enfants dans le
 * tableau… ce qui est FAUX, la spécification ne le garantit pas. On fait donc
 * le calcul en remontant les parents pour chaque os : la profondeur d'un
 * squelette humanoïde est de six, et dix-neuf os font cent quatorze produits —
 * moins que ce qu'un tri topologique coûterait à écrire et à relire.
 */
static void poser_mondes(const ns_skin *s, float t, ns_m4 *world, int count)
{
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
    for (int j = 0; j < count; ++j) {
        ns_m4 w = ns_m4_identity();
        int chain[64];
        int depth = 0;
        for (int i = s->joint_node[j]; i >= 0 && i < n && depth < 64; i = s->node[i].parent) {
            chain[depth++] = i;
        }
        for (int k = depth - 1; k >= 0; --k) w = ns_m4_mul(w, local[chain[k]]);
        world[j] = w;
    }
}

static ns_v3 origine(const ns_m4 *m) { return ns_v3_make(m->m[3][0], m->m[3][1], m->m[3][2]); }

/* Une rotation d'angle `a` autour de `axe`, l'axe PASSANT PAR `pivot`. */
static ns_m4 rotation_autour(ns_v3 pivot, ns_v3 axe, float a)
{
    return ns_m4_mul(ns_m4_translate(pivot),
                     ns_m4_mul(ns_m4_rotate_axis(axe, a),
                               ns_m4_translate(ns_v3_neg(pivot))));
}

/*
 * PLIER LES JAMBES, dans l'espace MONDE.
 *
 * Pourquoi en monde et pas en local, qui serait le geste habituel : parce que
 * le repère local d'un os dépend entièrement de l'exportateur — sur ce
 * squelette-ci l'axe de l'os est son X local, ailleurs ce sera son Y — et qu'un
 * angle de genou écrit dans ce repère-là ne veut rien dire d'un fichier à
 * l'autre. En monde, il n'y a qu'un seul axe qui compte, celui des ÉPAULES, et
 * il est MESURÉ (`axe_lateral`, tiré de l'intégration de la foulée).
 *
 * Trois rotations par jambe, chacune autour de l'articulation qu'elle plie :
 *
 *   1. la CUISSE part en avant de `a` ;
 *   2. le MOLLET revient en arrière de `2a`, ce qui laisse le tibia incliné de
 *      `-a` : la cheville reste alors À L'APLOMB de la hanche, et le
 *      personnage s'accroupit sans partir en arrière ;
 *   3. le PIED reprend `a` pour retrouver son horizontale. Sans cette
 *      troisième-là, la pointe traverse le sol — c'est la première chose que
 *      la capture a montrée.
 *
 * Le buste s'incline de `0,45 a`, et ce n'est pas un ornement : sans lui, la
 * totalité des trente-neuf centimètres de descente doit venir des jambes, ce
 * qui demande un genou à cent trente-six degrés. Avec, il en faut cent vingt —
 * et un accroupi où le dos reste vertical se lit comme quelqu'un assis sur une
 * chaise invisible.
 */
static void plier_jambes(const ns_skin *s, float a, ns_m4 *world, int count)
{
    if (a <= 1e-5f) return;
    const ns_v3 axe = s->axe_lateral;
    const float sgn = s->sens_avant;

    for (int f = 0; f < 2; ++f) {
        /* Les trois articulations de la jambe, dans la pose courante. */
        int cuisse = -1, mollet = -1, pied = -1;
        for (int j = 0; j < count; ++j) {
            if (s->jambe[j] != (int8_t)f) continue;
            if (s->etage[j] == 0 && cuisse < 0) cuisse = j;
            else if (s->etage[j] == 1 && mollet < 0) mollet = j;
            else if (s->etage[j] == 2 && pied < 0) pied = j;
        }
        if (cuisse < 0 || mollet < 0) continue;

        const ns_m4 A = rotation_autour(origine(&world[cuisse]), axe, sgn * a);
        const ns_m4 apres_a = ns_m4_mul(A, world[mollet]);
        const ns_m4 B = ns_m4_mul(rotation_autour(origine(&apres_a), axe, -sgn * 2.0f * a), A);
        ns_m4 C = B;
        if (pied >= 0) {
            const ns_m4 apres_b = ns_m4_mul(B, world[pied]);
            C = ns_m4_mul(rotation_autour(origine(&apres_b), axe, sgn * a), B);
        }

        for (int j = 0; j < count; ++j) {
            if (s->jambe[j] != (int8_t)f) continue;
            const ns_m4 *m = (s->etage[j] == 0) ? &A : (s->etage[j] == 1 ? &B : &C);
            world[j] = ns_m4_mul(*m, world[j]);
        }
    }

    if (s->buste_noeud >= 0) {
        /* Le pivot du buste : la première articulation qui le suit. */
        int racine = -1;
        for (int j = 0; j < count && racine < 0; ++j) {
            if (s->suit_buste[j] && s->joint_node[j] == s->buste_noeud) racine = j;
        }
        if (racine >= 0) {
            const ns_m4 D = rotation_autour(origine(&world[racine]), axe, sgn * 0.45f * a);
            for (int j = 0; j < count; ++j) {
                if (s->suit_buste[j]) world[j] = ns_m4_mul(D, world[j]);
            }
        }
    }
}

/* La remontée du point le plus bas, interpolée entre deux crans mesurés. */
static float remontee(const ns_skin *s, float c)
{
    if (c <= 0.0f) return 0.0f;
    if (c >= 1.0f) return s->accroupi_remontee[NS_SKIN_ACCROUPI_CRANS - 1];
    const float u = c * (float)(NS_SKIN_ACCROUPI_CRANS - 1);
    const int   k = (int)u;
    return ns_lerpf(s->accroupi_remontee[k], s->accroupi_remontee[k + 1], u - (float)k);
}

void ns_skin_pose(const ns_skin *s, float time, ns_m4 *out, int max)
{
    ns_skin_pose_allure(s, time, NULL, out, max);
}

void ns_skin_pose_allure(const ns_skin *s, float time, const ns_skin_allure *al,
                         ns_m4 *out, int max)
{
    if (!s || !out) return;

    /* Bouclée : un cycle de marche n'a de sens qu'en boucle, et `fmodf` d'un
     * négatif rend un négatif — d'où le rattrapage. */
    float t = SDL_fmodf(time, s->duration);
    if (t < 0.0f) t += s->duration;

    const int count = (s->joint_count < max) ? s->joint_count : max;
    ns_m4 world[NS_SKIN_MAX_JOINTS];
    poser_mondes(s, t, world, count);

    ns_m4 monde = ns_m4_identity();
    bool  pose_monde = false;

    if (al && s->accroupi_pret && al->accroupi > 0.0f) {
        const float c = (al->accroupi > 1.0f) ? 1.0f : al->accroupi;
        plier_jambes(s, c * s->accroupi_angle, world, count);
        monde = ns_m4_translate(ns_v3_make(0.0f, -remontee(s, c), 0.0f));
        pose_monde = true;
    }

    /*
     * LE BALANCEMENT POSTURAL.
     *
     * Un corps debout n'est jamais immobile : il oscille autour de ses
     * chevilles, d'un demi-degré et à une demi-période par seconde environ.
     * C'est pour ça qu'un personnage figé sur une image de marche se lit comme
     * une STATUE même quand la pose est juste.
     *
     * On le pose donc comme une rotation du corps entier autour du SOL, pas
     * comme un déplacement vertical : un corps qui monte et descend en bloc
     * décolle les pieds, et huit millimètres de pied en l'air se voient. À un
     * demi-degré, un pied à dix centimètres du pivot bouge de neuf dixièmes de
     * millimètre — c'est-à-dire rien.
     *
     * Deux fréquences incommensurables plutôt qu'une : une oscillation d'une
     * seule période se reconnaît au bout de trois secondes et devient un tic.
     */
    if (al && al->souffle_force > 0.0f) {
        const float w = al->souffle;
        const float ang = (SDL_sinf(w * 1.31f) * 0.0075f + SDL_sinf(w * 0.57f + 1.7f) * 0.0045f)
                        * al->souffle_force;
        const ns_v3 avant = ns_v3_make(SDL_cosf(s->forward_angle), 0.0f, SDL_sinf(s->forward_angle));
        const ns_m4 tangage = rotation_autour(ns_v3_zero(), s->axe_lateral, ang);
        const ns_m4 roulis  = rotation_autour(ns_v3_zero(), avant, ang * 0.6f);
        monde = ns_m4_mul(ns_m4_mul(tangage, roulis), monde);
        pose_monde = true;
    }

    for (int j = 0; j < count; ++j) {
        const ns_m4 w = pose_monde ? ns_m4_mul(monde, world[j]) : world[j];
        out[j] = ns_m4_mul(w, s->inverse_bind[j]);
    }
}

/* ------------------------------------------------------------- l'accroupi */

/* La hauteur du personnage, plié de `a`, MESURÉE sur ses sommets sous la pose.
 * `bas` reçoit l'altitude du point le plus bas — c'est la remontée à corriger. */
static float hauteur_pliee(const ns_skin *s, float a, float *bas)
{
    ns_m4 world[NS_SKIN_MAX_JOINTS];
    poser_mondes(s, s->stand_time, world, s->joint_count);
    plier_jambes(s, a, world, s->joint_count);

    ns_m4 os[NS_SKIN_MAX_JOINTS];
    for (int j = 0; j < s->joint_count; ++j) os[j] = ns_m4_mul(world[j], s->inverse_bind[j]);

    float lo = FLT_MAX, hi = -FLT_MAX;
    for (uint32_t i = 0; i < s->vert_count; ++i) {
        const ns_skin_vertex *v = &s->verts[i];
        float y = 0.0f;
        for (int k = 0; k < NS_SKIN_INFLUENCES; ++k) {
            const float w = v->weights[k];
            if (w <= 0.0f) continue;
            const ns_m4 *m = &os[v->joints[k]];
            y += w * (m->m[0][1] * v->position[0] + m->m[1][1] * v->position[1] +
                      m->m[2][1] * v->position[2] + m->m[3][1]);
        }
        if (y < lo) lo = y;
        if (y > hi) hi = y;
    }
    if (bas) *bas = lo;
    return (hi > lo) ? (hi - lo) : 0.0f;
}

bool ns_skin_crouch_calibrate(ns_skin *s, float rapport)
{
    if (!s) return false;
    s->accroupi_pret = false;
    if (s->hanche[0] < 0 || s->hanche[1] < 0) {
        NS_WARN("personnage : les deux jambes n'ont pas été repérées — pas d'accroupi");
        return false;
    }
    if (!(rapport > 0.35f && rapport < 0.98f)) {
        NS_WARN("personnage : rapport d'accroupi %.2f hors de [0,35 ; 0,98] — ignoré",
                (double)rapport);
        return false;
    }

    float bas0 = 0.0f;
    const float h0 = hauteur_pliee(s, 0.0f, &bas0);
    if (h0 <= 1e-4f) return false;
    const float vise = h0 * rapport;

    /*
     * BALAYAGE puis DICHOTOMIE. Le balayage d'abord parce que la hauteur n'est
     * PAS garantie monotone : passé un certain pli, le genou ressort au-dessus
     * de la tête et la « hauteur » du personnage se met à remonter. Une
     * dichotomie lancée à l'aveugle sur [0 ; 2 rad] pourrait donc atterrir sur
     * la mauvaise branche. On cherche le premier intervalle qui encadre la
     * cote, et on n'affine que celui-là.
     */
    const float amax = 2.0f;
    float lo = -1.0f, hi = -1.0f;
    float precedent = h0;
    for (int k = 1; k <= 40; ++k) {
        const float a = amax * (float)k / 40.0f;
        const float h = hauteur_pliee(s, a, NULL);
        if (h <= vise && precedent > vise) {
            lo = amax * (float)(k - 1) / 40.0f;
            hi = a;
            break;
        }
        precedent = h;
    }
    if (lo < 0.0f) {
        NS_WARN("personnage : impossible d'atteindre %.0f %% de la hauteur en pliant "
                "les jambes — pas d'accroupi", (double)(rapport * 100.0f));
        return false;
    }
    for (int k = 0; k < 18; ++k) {
        const float mid = 0.5f * (lo + hi);
        if (hauteur_pliee(s, mid, NULL) > vise) lo = mid; else hi = mid;
    }
    s->accroupi_angle = 0.5f * (lo + hi);

    for (int k = 0; k < NS_SKIN_ACCROUPI_CRANS; ++k) {
        const float c = (float)k / (float)(NS_SKIN_ACCROUPI_CRANS - 1);
        float bas = 0.0f;
        (void)hauteur_pliee(s, c * s->accroupi_angle, &bas);
        s->accroupi_remontee[k] = bas - bas0;
    }
    s->accroupi_pret = true;

    NS_INFO("personnage : accroupi calé à %.1f degrés de cuisse (genou %.1f), "
            "hauteur %.2f -> %.2f, bassin descendu de %.3f (unités du fichier)",
            (double)(s->accroupi_angle / NS_DEG2RAD),
            (double)(2.0f * s->accroupi_angle / NS_DEG2RAD),
            (double)h0, (double)(h0 * rapport),
            (double)s->accroupi_remontee[NS_SKIN_ACCROUPI_CRANS - 1]);
    return true;
}

bool  ns_skin_can_crouch(const ns_skin *s) { return s && s->accroupi_pret; }
float ns_skin_crouch_angle(const ns_skin *s)
{
    return s ? (s->accroupi_angle / NS_DEG2RAD) : 0.0f;
}
