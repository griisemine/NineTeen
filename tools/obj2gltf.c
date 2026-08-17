/*
 * obj2gltf — convertit la salle d'arcade d'origine en scène moderne.
 *
 * Entrée  : legacy/room/textures/salle.obj + salle.mtl
 *           (127 objets, 120 matériaux, 55 333 faces, exportés de Blender 2.82)
 * Sortie  : salle.gltf + salle.bin  — glTF 2.0, éditable à nouveau dans Blender
 *           salle.lights.json       — lumières et points d'intérêt déduits
 *
 * Ce que l'outil fait au-delà d'une simple traduction de format :
 *
 *  1. Conversion Blinn-Phong -> metallic-roughness. Le MTL décrit l'éclairage
 *     d'il y a vingt ans (Kd/Ks/Ns) ; le rendu PBR veut albédo, métallicité,
 *     rugosité. La rugosité se dérive de l'exposant spéculaire Ns.
 *
 *  2. Génération des tangentes. Absentes du OBJ, indispensables pour appliquer
 *     des normal maps — donc pour que les bornes cessent d'être des boîtes
 *     lisses avec une photo collée dessus.
 *
 *  3. Déduction des sources lumineuses. Les appliques murales (sconce_02.*),
 *     les enseignes et les écrans de bornes étaient *peints* dans la texture.
 *     On les repère par nom d'objet et par émissivité du matériau (Ke), et on
 *     émet de vraies lumières ponctuelles à leur position. C'est le geste qui
 *     fait passer la salle de « décor » à « lieu éclairé ».
 *
 *  4. Repérage des bornes. Les noms d'objets portent le jeu (FLAPPY_BIRD_HARD,
 *     PANNEAU_EASY/HARD…) : on en extrait la liste des bornes et l'emprise de
 *     leur écran, pour y afficher le mini-jeu en direct.
 */
#include "tools_common.h"

#include <math.h>

#define MAX_NAME 128

/* ========================================================================== */
/* Structures intermédiaires                                                  */
/* ========================================================================== */

typedef struct { float x, y, z; } v3;
typedef struct { float x, y; }    v2;

typedef struct material {
    char  name[MAX_NAME];
    float kd[3];          /* diffus            -> baseColorFactor */
    float ks[3];          /* spéculaire        -> metallic (heuristique) */
    float ke[3];          /* émissif           -> emissiveFactor */
    float ns;             /* exposant spéculaire -> roughness */
    float ni;             /* indice de réfraction (informatif) */
    float alpha;
    char  map_kd[MAX_NAME];
    int   illum;
    /* Dérivés PBR */
    float base_color[4];
    float metallic;
    float roughness;
    float emissive[3];
    float emissive_strength;
    const char *family;   /* famille reconnue, NULL sinon */
    int   gltf_index;     /* -1 tant qu'il n'est pas émis */
    int   texture_index;  /* -1 si pas de map_Kd */
} material;

/* Sommet dédupliqué : la clé est le triplet d'indices OBJ. */
typedef struct vertex {
    v3 position;
    v3 normal;
    v2 uv;
    float tangent[4];
} vertex;

typedef struct vkey {
    int32_t v, vt, vn;
    int32_t out_index;
    int32_t next;         /* chaînage dans le seau de hachage */
} vkey;

typedef struct primitive {
    int      material;
    tool_vec indices;     /* uint32_t */
} primitive;

typedef struct object {
    char     name[MAX_NAME];
    tool_vec prims;       /* primitive */
    v3       bbox_min, bbox_max;
    bool     has_bounds;
} object;

/* ========================================================================== */
/* Conversion Blinn-Phong -> metallic-roughness                               */
/* ========================================================================== */
/*
 * Il n'existe pas de conversion exacte : les deux modèles ne décrivent pas la
 * même physique. On applique la correspondance usuelle, en documentant les
 * choix pour qu'ils soient révisables.
 */
static bool g_blender_source = false;   /* renseigné en lisant l'en-tête du MTL */

/*
 * Bibliothèque de matériaux, indexée par le nom de la texture diffuse.
 *
 * Le MTL ne distingue pas une moquette d'un carrelage : tous deux sortent de
 * Blender avec la même rugosité 0.5 par défaut. Or c'est précisément cette
 * différence qui fait qu'une salle a l'air construite en matériaux plutôt qu'en
 * plastique uniforme — le carrelage des toilettes doit renvoyer les néons, la
 * moquette doit les absorber.
 *
 * L'auteur d'origine a nommé ses textures de façon parlante (moquette, bois,
 * carllage_toilette, marbre_toilettes, cuir_rouge, billard_table…). On s'en sert
 * pour attribuer à chacune des paramètres physiques plausibles. Le nom n'est pas
 * une donnée fiable en général, mais ici c'est la meilleure information
 * disponible, et le résultat est nettement supérieur à une valeur unique.
 */
typedef struct material_family {
    const char *token;        /* sous-chaîne recherchée dans le nom de texture */
    float roughness;
    float metallic;
    const char *label;
} material_family;

static const material_family g_families[] = {
    /* Surfaces très diffuses : absorbent la lumière. */
    { "moquette",   0.96f, 0.0f, "moquette" },
    { "billard",    0.92f, 0.0f, "feutrine" },
    { "mur_brique", 0.88f, 0.0f, "brique" },
    { "plafond",    0.80f, 0.0f, "plafond" },

    /* Surfaces lisses : renvoient les néons, c'est ce qui fait vivre la salle. */
    { "marbre",     0.10f, 0.0f, "marbre" },
    { "carllage",   0.16f, 0.0f, "carrelage" },
    { "carrelage",  0.16f, 0.0f, "carrelage" },
    { "lavabo",     0.14f, 0.0f, "céramique" },
    { "toilet",     0.18f, 0.0f, "céramique" },
    { "floor",      0.24f, 0.0f, "sol ciré" },
    { "sol",        0.24f, 0.0f, "sol ciré" },

    /* Bois : mat à satiné selon la finition. */
    { "bois",       0.58f, 0.0f, "bois" },
    { "poutre",     0.72f, 0.0f, "bois brut" },
    { "desk",       0.42f, 0.0f, "bois verni" },

    /* Métaux : ce sont eux qui donnent les reflets nets sur les bornes. */
    { "pilonne",    0.34f, 0.85f, "métal peint" },
    { "radio",      0.30f, 0.55f, "métal brossé" },

    /* Divers. */
    { "cuir",       0.62f, 0.0f, "cuir" },
    { "porte",      0.55f, 0.0f, "porte" },

    /* Écrans, affiches et enseignes : surfaces vitrées, très lisses. */
    { "_font",      0.12f, 0.0f, "écran" },
    { "poster",     0.30f, 0.0f, "affiche" },
    { "pub",        0.28f, 0.0f, "affiche" },
    { "classement", 0.12f, 0.0f, "écran" },
    { "chargement", 0.12f, 0.0f, "écran" },
    { "coming_soon",0.12f, 0.0f, "écran" },
    { "nineteen",   0.20f, 0.3f, "enseigne" },
    { "exit",       0.25f, 0.0f, "enseigne" },

    /* Flancs de bornes : peinture laquée. */
    { "flappy",     0.34f, 0.0f, "laque" },
    { "snake",      0.34f, 0.0f, "laque" },
    { "tetris",     0.34f, 0.0f, "laque" },
    { "asteroid",   0.34f, 0.0f, "laque" },
};

/* Compare sans tenir compte de la casse, sous-chaîne. */
static bool name_has_token(const char *haystack, const char *needle)
{
    const size_t nl = strlen(needle);
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < nl && p[i] && ((p[i] | 32) == (needle[i] | 32))) i++;
        if (i == nl) return true;
    }
    return false;
}

static const material_family *family_for_texture(const char *texture)
{
    if (!texture || !*texture) return NULL;
    for (size_t i = 0; i < sizeof g_families / sizeof g_families[0]; ++i) {
        if (name_has_token(texture, g_families[i].token)) return &g_families[i];
    }
    return NULL;
}

static void derive_pbr(material *m)
{
    /*
     * Rugosité depuis l'exposant spéculaire.
     *
     * Deux cas, parce qu'ils ne donnent pas du tout le même résultat :
     *
     *  - Fichier exporté par Blender (le nôtre : « Blender MTL File: salle.blend »).
     *    Son exporteur écrit Ns = (1 - rugosité)² × 900. L'inversion est donc
     *    exacte, et c'est la seule façon de retrouver les valeurs voulues par
     *    l'auteur. La distribution du fichier le confirme : 101 matériaux sur 120
     *    ont Ns = 225, soit exactement la rugosité 0.5 par défaut de Blender, et
     *    le maximum observé est 900, soit rugosité 0.
     *
     *  - Fichier d'origine inconnue : on retombe sur l'équivalence classique
     *    entre lobe de Blinn-Phong et rugosité GGX.
     *
     * Se tromper ici ne se voit pas sur une capture fixe mais saute aux yeux en
     * mouvement : avec la formule générique, Ns = 225 donnait une rugosité de
     * 0.09 et transformait les murs en miroirs.
     */
    float roughness;
    if (g_blender_source) {
        float ns = m->ns;
        if (ns < 0.0f)   ns = 0.0f;
        if (ns > 900.0f) ns = 900.0f;
        roughness = 1.0f - sqrtf(ns / 900.0f);
    } else {
        roughness = sqrtf(2.0f / (m->ns + 2.0f));
    }
    if (roughness > 1.0f) roughness = 1.0f;
    if (roughness < 0.03f) roughness = 0.03f;   /* un miroir parfait scintille */

    /* Métallicité : le MTL n'a pas la notion. Un matériau très spéculaire et
     * peu diffus est probablement métallique (chrome des bornes, pieds de
     * tabouret) ; le reste est diélectrique. Le seuil est volontairement
     * conservateur — mieux vaut un métal manqué qu'un mur en aluminium. */
    const float ks_max = fmaxf(m->ks[0], fmaxf(m->ks[1], m->ks[2]));
    const float kd_max = fmaxf(m->kd[0], fmaxf(m->kd[1], m->kd[2]));
    float metallic = 0.0f;
    if (ks_max > 0.6f && kd_max < 0.25f) {
        metallic = 1.0f;
    } else if (ks_max > 0.5f && kd_max < 0.5f) {
        metallic = 0.5f;
    }

    /* La famille de matériau, quand on la reconnaît, prime sur la valeur
     * générique de Blender : elle porte une information que le MTL n'a pas. */
    const material_family *fam = family_for_texture(m->map_kd);
    if (fam) {
        roughness = fam->roughness;
        metallic  = fam->metallic;
        m->family = fam->label;
    } else {
        m->family = NULL;
    }

    m->base_color[0] = m->kd[0];
    m->base_color[1] = m->kd[1];
    m->base_color[2] = m->kd[2];
    m->base_color[3] = m->alpha;
    m->metallic  = metallic;
    m->roughness = roughness;

    /* Émissif. Blender exporte souvent un Ke gris uniforme non nul sur des
     * matériaux qui ne sont pas censés émettre : on ne retient que ce qui est
     * franchement lumineux, et on normalise la couleur en séparant l'intensité
     * (extension KHR_materials_emissive_strength) — sinon tout le décor brille
     * faiblement et l'image devient laiteuse. */
    const float ke_max = fmaxf(m->ke[0], fmaxf(m->ke[1], m->ke[2]));
    if (ke_max > 0.25f) {
        m->emissive[0] = m->ke[0] / ke_max;
        m->emissive[1] = m->ke[1] / ke_max;
        m->emissive[2] = m->ke[2] / ke_max;
        m->emissive_strength = ke_max;
    } else {
        m->emissive[0] = m->emissive[1] = m->emissive[2] = 0.0f;
        m->emissive_strength = 0.0f;
    }
}

/* ========================================================================== */
/* Analyse du MTL                                                             */
/* ========================================================================== */

static int find_material(tool_vec *mats, const char *name)
{
    for (size_t i = 0; i < mats->count; ++i) {
        if (strcmp(TOOL_VEC_AT(mats, material, i).name, name) == 0) return (int)i;
    }
    return -1;
}

static void parse_mtl(const char *path, tool_vec *mats)
{
    size_t size = 0;
    char *text = tool_read_file(path, &size);
    if (!text) tool_fatalf("MTL illisible : %s", path);

    /* L'en-tête indique l'exporteur, ce qui détermine comment interpréter Ns. */
    if (strstr(text, "Blender MTL File") != NULL) {
        g_blender_source = true;
        tool_infof("export Blender détecté : Ns interprété comme (1-rugosité)² × 900");
    }

    material *cur = NULL;
    char tok[MAX_NAME];

    for (char *line = text; line; line = tool_next_line(line)) {
        char *p = tool_skip_ws(line);
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        p = tool_token(p, tok, sizeof tok);

        if (strcmp(tok, "newmtl") == 0) {
            char name[MAX_NAME];
            tool_token(p, name, sizeof name);
            cur = (material *)tool_vec_push(mats);
            memset(cur, 0, sizeof *cur);
            snprintf(cur->name, sizeof cur->name, "%s", name);
            cur->alpha = 1.0f;
            cur->ns = 32.0f;
            cur->ni = 1.45f;
            cur->gltf_index = -1;
            cur->texture_index = -1;
        } else if (!cur) {
            continue;                              /* directive avant tout newmtl */
        } else if (strcmp(tok, "Kd") == 0) {
            char a[32], b[32], c[32];
            p = tool_token(p, a, sizeof a); p = tool_token(p, b, sizeof b); tool_token(p, c, sizeof c);
            cur->kd[0] = (float)atof(a); cur->kd[1] = (float)atof(b); cur->kd[2] = (float)atof(c);
        } else if (strcmp(tok, "Ks") == 0) {
            char a[32], b[32], c[32];
            p = tool_token(p, a, sizeof a); p = tool_token(p, b, sizeof b); tool_token(p, c, sizeof c);
            cur->ks[0] = (float)atof(a); cur->ks[1] = (float)atof(b); cur->ks[2] = (float)atof(c);
        } else if (strcmp(tok, "Ke") == 0) {
            char a[32], b[32], c[32];
            p = tool_token(p, a, sizeof a); p = tool_token(p, b, sizeof b); tool_token(p, c, sizeof c);
            cur->ke[0] = (float)atof(a); cur->ke[1] = (float)atof(b); cur->ke[2] = (float)atof(c);
        } else if (strcmp(tok, "Ns") == 0) {
            tool_token(p, tok, sizeof tok);
            cur->ns = (float)atof(tok);
        } else if (strcmp(tok, "Ni") == 0) {
            tool_token(p, tok, sizeof tok);
            cur->ni = (float)atof(tok);
        } else if (strcmp(tok, "d") == 0) {
            tool_token(p, tok, sizeof tok);
            cur->alpha = (float)atof(tok);
        } else if (strcmp(tok, "Tr") == 0) {       /* certains exporteurs écrivent l'inverse */
            tool_token(p, tok, sizeof tok);
            cur->alpha = 1.0f - (float)atof(tok);
        } else if (strcmp(tok, "illum") == 0) {
            tool_token(p, tok, sizeof tok);
            cur->illum = atoi(tok);
        } else if (strcmp(tok, "map_Kd") == 0) {
            /* La ligne peut contenir des options (-s, -o…) avant le fichier ;
             * on retient le dernier mot, qui est le chemin. */
            char last[MAX_NAME] = { 0 };
            for (;;) {
                char w[MAX_NAME];
                char *q = tool_token(p, w, sizeof w);
                if (w[0] == '\0') break;
                snprintf(last, sizeof last, "%s", w);
                p = q;
            }
            snprintf(cur->map_kd, sizeof cur->map_kd, "%s", last);
        }
    }
    free(text);

    for (size_t i = 0; i < mats->count; ++i) {
        derive_pbr(&TOOL_VEC_AT(mats, material, i));
    }
}

/* ========================================================================== */
/* Analyse du OBJ                                                             */
/* ========================================================================== */

/* Table de hachage des triplets v/vt/vn -> index de sommet de sortie. */
#define HASH_BUCKETS (1u << 18)

typedef struct dedup {
    int32_t  *buckets;      /* index dans keys, -1 si vide */
    tool_vec  keys;         /* vkey */
} dedup;

static void dedup_init(dedup *d)
{
    d->buckets = (int32_t *)malloc(sizeof(int32_t) * HASH_BUCKETS);
    if (!d->buckets) tool_fatalf("mémoire épuisée (table de déduplication)");
    for (uint32_t i = 0; i < HASH_BUCKETS; ++i) d->buckets[i] = -1;
    tool_vec_init(&d->keys, sizeof(vkey));
}

static void dedup_free(dedup *d)
{
    free(d->buckets);
    tool_vec_free(&d->keys);
}

static uint32_t hash3(int32_t a, int32_t b, int32_t c)
{
    uint32_t h = 2166136261u;
    const int32_t vals[3] = { a, b, c };
    for (int i = 0; i < 3; ++i) {
        uint32_t x = (uint32_t)vals[i];
        for (int byte = 0; byte < 4; ++byte) {
            h ^= (x >> (byte * 8)) & 0xFFu;
            h *= 16777619u;
        }
    }
    return h & (HASH_BUCKETS - 1u);
}

/* Résout un indice OBJ (1-based, négatif = relatif à la fin). */
static int32_t resolve_index(int32_t raw, size_t count)
{
    if (raw > 0) return raw - 1;
    if (raw < 0) return (int32_t)count + raw;
    return -1;                                  /* 0 = absent */
}

/* Analyse "12/34/56", "12//56", "12/34", "12". */
static void parse_face_vertex(const char *s, int32_t *v, int32_t *vt, int32_t *vn)
{
    *v = *vt = *vn = 0;
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);

    char *slash1 = strchr(buf, '/');
    if (!slash1) { *v = atoi(buf); return; }
    *slash1 = '\0';
    *v = atoi(buf);

    char *rest = slash1 + 1;
    char *slash2 = strchr(rest, '/');
    if (!slash2) { *vt = atoi(rest); return; }
    *slash2 = '\0';
    if (*rest) *vt = atoi(rest);
    *vn = atoi(slash2 + 1);
}

/* ========================================================================== */
/* Tangentes                                                                  */
/* ========================================================================== */
/*
 * Une normal map est exprimée dans l'espace tangent de la surface ; sans
 * tangentes, elle est inapplicable. On les accumule par triangle (méthode de
 * Lengyel), puis on orthonormalise par rapport à la normale et on stocke la
 * chiralité dans w — c'est ce que glTF attend.
 */
static void generate_tangents(vertex *verts, size_t vcount,
                              const uint32_t *indices, size_t icount)
{
    v3 *tan = (v3 *)calloc(vcount, sizeof(v3));
    v3 *bit = (v3 *)calloc(vcount, sizeof(v3));
    if (!tan || !bit) tool_fatalf("mémoire épuisée (tangentes)");

    for (size_t i = 0; i + 2 < icount; i += 3) {
        const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        const vertex *a = &verts[i0], *b = &verts[i1], *c = &verts[i2];

        const float x1 = b->position.x - a->position.x;
        const float y1 = b->position.y - a->position.y;
        const float z1 = b->position.z - a->position.z;
        const float x2 = c->position.x - a->position.x;
        const float y2 = c->position.y - a->position.y;
        const float z2 = c->position.z - a->position.z;

        const float s1 = b->uv.x - a->uv.x, t1 = b->uv.y - a->uv.y;
        const float s2 = c->uv.x - a->uv.x, t2 = c->uv.y - a->uv.y;

        const float det = s1 * t2 - s2 * t1;
        /* Triangle dégénéré en UV (fréquent sur les faces sans dépliage) :
         * on le saute plutôt que de propager un infini. */
        if (fabsf(det) < 1e-12f) continue;
        const float r = 1.0f / det;

        const v3 sdir = { (t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r };
        const v3 tdir = { (s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r };

        const uint32_t idx[3] = { i0, i1, i2 };
        for (int k = 0; k < 3; ++k) {
            tan[idx[k]].x += sdir.x; tan[idx[k]].y += sdir.y; tan[idx[k]].z += sdir.z;
            bit[idx[k]].x += tdir.x; bit[idx[k]].y += tdir.y; bit[idx[k]].z += tdir.z;
        }
    }

    for (size_t i = 0; i < vcount; ++i) {
        const v3 n = verts[i].normal;
        const v3 t = tan[i];

        /* Gram-Schmidt : t' = t - n (n·t) */
        const float ndt = n.x * t.x + n.y * t.y + n.z * t.z;
        v3 o = { t.x - n.x * ndt, t.y - n.y * ndt, t.z - n.z * ndt };
        float len = sqrtf(o.x * o.x + o.y * o.y + o.z * o.z);

        if (len < 1e-8f) {
            /* Aucun UV exploitable : on fabrique une tangente arbitraire mais
             * orthogonale, pour ne pas produire de NaN dans le shader. */
            const v3 up = (fabsf(n.y) < 0.99f) ? (v3){ 0, 1, 0 } : (v3){ 1, 0, 0 };
            o.x = up.y * n.z - up.z * n.y;
            o.y = up.z * n.x - up.x * n.z;
            o.z = up.x * n.y - up.y * n.x;
            len = sqrtf(o.x * o.x + o.y * o.y + o.z * o.z);
            if (len < 1e-8f) { o = (v3){ 1, 0, 0 }; len = 1.0f; }
        }
        o.x /= len; o.y /= len; o.z /= len;

        /* Chiralité : signe du produit mixte (n × t) · b */
        const v3 cross = { n.y * o.z - n.z * o.y, n.z * o.x - n.x * o.z, n.x * o.y - n.y * o.x };
        const float w = (cross.x * bit[i].x + cross.y * bit[i].y + cross.z * bit[i].z) < 0.0f ? -1.0f : 1.0f;

        verts[i].tangent[0] = o.x;
        verts[i].tangent[1] = o.y;
        verts[i].tangent[2] = o.z;
        verts[i].tangent[3] = w;
    }
    free(tan);
    free(bit);
}

/* ========================================================================== */
/* Déduction des lumières et des bornes                                       */
/* ========================================================================== */

static float ns_max_strength(float s) { return (s > 0.0f) ? s : 1.0f; }

static bool name_contains_ci(const char *hay, const char *needle)
{
    const size_t nl = strlen(needle);
    for (const char *p = hay; *p; ++p) {
        size_t i = 0;
        while (i < nl && p[i] &&
               (p[i] | 32) == (needle[i] | 32)) i++;
        if (i == nl) return true;
    }
    return false;
}

/*
 * Repérage des bornes.
 *
 * On aimerait déduire le jeu du nom de l'objet. Ce n'est pas possible dans ce
 * fichier : les 15 bornes de la salle ont été dupliquées dans Blender à partir
 * d'une seule, et portent donc toutes le nom « FLAPPY_BIRD_HARD.NNN », quel que
 * soit le jeu qu'elles hébergent. Inventer une correspondance à partir de ce nom
 * produirait quinze bornes Flappy Bird.
 *
 * L'outil se contente donc de ce qu'il sait vraiment : il repère les bornes, les
 * numérote dans un ordre spatial **stable** (par X croissant, puis Z), et laisse
 * l'affectation des jeux à un fichier d'override écrit à la main
 * (assets/scene/cabinets.json). Le moteur fusionne les deux au chargement.
 */
static bool object_is_cabinet(const char *name)
{
    return name_contains_ci(name, "FLAPPY_BIRD")
        || name_contains_ci(name, "BORNE")
        || name_contains_ci(name, "ARCADE");
}

/* ========================================================================== */
/* Écriture glTF                                                              */
/* ========================================================================== */

typedef struct writer {
    FILE  *json;
    FILE  *bin;
    size_t bin_offset;
} writer;

/* Aligne le buffer binaire : glTF exige que chaque accesseur soit aligné sur
 * la taille de son composant. */
static void bin_align(writer *w, size_t alignment)
{
    while (w->bin_offset % alignment) {
        fputc(0, w->bin);
        w->bin_offset++;
    }
}

static size_t bin_write(writer *w, const void *data, size_t bytes)
{
    const size_t offset = w->bin_offset;
    if (fwrite(data, 1, bytes, w->bin) != bytes) tool_fatalf("écriture du .bin interrompue");
    w->bin_offset += bytes;
    return offset;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
            "obj2gltf — convertit un OBJ/MTL en glTF 2.0 avec matériaux PBR et lumières déduites\n"
            "usage : %s <entrée.obj> <sortie.gltf>\n", argv[0]);
        return 2;
    }
    const char *in_path  = argv[1];
    const char *out_path = argv[2];

    printf("obj2gltf : %s\n", in_path);

    /* ---------------------------------------------------------- lecture OBJ */
    size_t obj_size = 0;
    char *text = tool_read_file(in_path, &obj_size);
    if (!text) tool_fatalf("OBJ illisible : %s", in_path);
    tool_infof("%.1f Mio lus", (double)obj_size / (1024.0 * 1024.0));

    char in_dir[512];
    tool_dirname(in_path, in_dir, sizeof in_dir);

    tool_vec positions; tool_vec_init(&positions, sizeof(v3));
    tool_vec uvs;       tool_vec_init(&uvs, sizeof(v2));
    tool_vec normals;   tool_vec_init(&normals, sizeof(v3));
    tool_vec mats;      tool_vec_init(&mats, sizeof(material));
    tool_vec objects;   tool_vec_init(&objects, sizeof(object));
    tool_vec verts;     tool_vec_init(&verts, sizeof(vertex));

    dedup dd;
    dedup_init(&dd);

    object *cur_obj = NULL;
    primitive *cur_prim = NULL;
    int cur_mat = -1;
    size_t degenerate = 0, missing_normals = 0;

    char tok[MAX_NAME];

    for (char *line = text; line; line = tool_next_line(line)) {
        char *p = tool_skip_ws(line);
        if (*p == '#' || *p == '\n' || *p == '\0' || *p == '\r') continue;

        p = tool_token(p, tok, sizeof tok);

        if (strcmp(tok, "v") == 0) {
            char a[32], b[32], c[32];
            p = tool_token(p, a, sizeof a); p = tool_token(p, b, sizeof b); tool_token(p, c, sizeof c);
            v3 *v = (v3 *)tool_vec_push(&positions);
            v->x = (float)atof(a); v->y = (float)atof(b); v->z = (float)atof(c);

        } else if (strcmp(tok, "vt") == 0) {
            char a[32], b[32];
            p = tool_token(p, a, sizeof a); tool_token(p, b, sizeof b);
            v2 *t = (v2 *)tool_vec_push(&uvs);
            t->x = (float)atof(a);
            /* OBJ place l'origine des UV en bas à gauche, glTF en haut à gauche. */
            t->y = 1.0f - (float)atof(b);

        } else if (strcmp(tok, "vn") == 0) {
            char a[32], b[32], c[32];
            p = tool_token(p, a, sizeof a); p = tool_token(p, b, sizeof b); tool_token(p, c, sizeof c);
            v3 *n = (v3 *)tool_vec_push(&normals);
            n->x = (float)atof(a); n->y = (float)atof(b); n->z = (float)atof(c);

        } else if (strcmp(tok, "mtllib") == 0) {
            char file[MAX_NAME], full[768];
            tool_token(p, file, sizeof file);
            snprintf(full, sizeof full, "%s%s", in_dir, file);
            parse_mtl(full, &mats);
            tool_infof("%zu matériaux lus dans %s", mats.count, file);

        } else if (strcmp(tok, "o") == 0 || strcmp(tok, "g") == 0) {
            char name[MAX_NAME];
            tool_token(p, name, sizeof name);
            cur_obj = (object *)tool_vec_push(&objects);
            memset(cur_obj, 0, sizeof *cur_obj);
            snprintf(cur_obj->name, sizeof cur_obj->name, "%s", name[0] ? name : "objet");
            tool_vec_init(&cur_obj->prims, sizeof(primitive));
            cur_prim = NULL;

        } else if (strcmp(tok, "usemtl") == 0) {
            char name[MAX_NAME];
            tool_token(p, name, sizeof name);
            cur_mat = find_material(&mats, name);
            cur_prim = NULL;                     /* forcera la création d'une primitive */

        } else if (strcmp(tok, "f") == 0) {
            if (!cur_obj) {                      /* OBJ sans directive `o` */
                cur_obj = (object *)tool_vec_push(&objects);
                memset(cur_obj, 0, sizeof *cur_obj);
                snprintf(cur_obj->name, sizeof cur_obj->name, "objet");
                tool_vec_init(&cur_obj->prims, sizeof(primitive));
                cur_prim = NULL;
            }
            if (!cur_prim) {
                /* Réutiliser la primitive du même matériau si elle existe déjà
                 * dans cet objet : sinon un OBJ qui alterne les matériaux
                 * produirait des dizaines de primitives d'un triangle. */
                for (size_t i = 0; i < cur_obj->prims.count; ++i) {
                    primitive *pr = &TOOL_VEC_AT(&cur_obj->prims, primitive, i);
                    if (pr->material == cur_mat) { cur_prim = pr; break; }
                }
                if (!cur_prim) {
                    cur_prim = (primitive *)tool_vec_push(&cur_obj->prims);
                    cur_prim->material = cur_mat;
                    tool_vec_init(&cur_prim->indices, sizeof(uint32_t));
                }
            }

            /* Lire tous les sommets de la face, puis trianguler en éventail. */
            uint32_t face[64];
            int fc = 0;
            for (;;) {
                char w[64];
                char *q = tool_token(p, w, sizeof w);
                if (w[0] == '\0') break;
                p = q;
                if (fc >= 64) { tool_warnf("face de plus de 64 sommets tronquée"); break; }

                int32_t rv, rt, rn;
                parse_face_vertex(w, &rv, &rt, &rn);
                const int32_t iv = resolve_index(rv, positions.count);
                const int32_t it = resolve_index(rt, uvs.count);
                const int32_t in = resolve_index(rn, normals.count);
                if (iv < 0 || (size_t)iv >= positions.count) { tool_warnf("indice de sommet invalide"); continue; }

                const uint32_t bucket = hash3(iv, it, in);
                int32_t found = -1;
                for (int32_t k = dd.buckets[bucket]; k >= 0; k = TOOL_VEC_AT(&dd.keys, vkey, (size_t)k).next) {
                    const vkey *key = &TOOL_VEC_AT(&dd.keys, vkey, (size_t)k);
                    if (key->v == iv && key->vt == it && key->vn == in) { found = key->out_index; break; }
                }

                if (found < 0) {
                    vertex *nv = (vertex *)tool_vec_push(&verts);
                    memset(nv, 0, sizeof *nv);
                    nv->position = TOOL_VEC_AT(&positions, v3, (size_t)iv);
                    if (it >= 0 && (size_t)it < uvs.count) nv->uv = TOOL_VEC_AT(&uvs, v2, (size_t)it);
                    if (in >= 0 && (size_t)in < normals.count) {
                        nv->normal = TOOL_VEC_AT(&normals, v3, (size_t)in);
                    } else {
                        missing_normals++;
                        nv->normal = (v3){ 0, 1, 0 };
                    }

                    found = (int32_t)(verts.count - 1);
                    vkey *key = (vkey *)tool_vec_push(&dd.keys);
                    key->v = iv; key->vt = it; key->vn = in;
                    key->out_index = found;
                    key->next = dd.buckets[bucket];
                    dd.buckets[bucket] = (int32_t)(dd.keys.count - 1);

                    /* Emprise de l'objet, utilisée pour poser les lumières. */
                    const v3 pos = nv->position;
                    if (!cur_obj->has_bounds) {
                        cur_obj->bbox_min = cur_obj->bbox_max = pos;
                        cur_obj->has_bounds = true;
                    } else {
                        if (pos.x < cur_obj->bbox_min.x) cur_obj->bbox_min.x = pos.x;
                        if (pos.y < cur_obj->bbox_min.y) cur_obj->bbox_min.y = pos.y;
                        if (pos.z < cur_obj->bbox_min.z) cur_obj->bbox_min.z = pos.z;
                        if (pos.x > cur_obj->bbox_max.x) cur_obj->bbox_max.x = pos.x;
                        if (pos.y > cur_obj->bbox_max.y) cur_obj->bbox_max.y = pos.y;
                        if (pos.z > cur_obj->bbox_max.z) cur_obj->bbox_max.z = pos.z;
                    }
                }
                face[fc++] = (uint32_t)found;
            }

            for (int k = 2; k < fc; ++k) {
                /* Rejeter les triangles dégénérés : ils ne dessinent rien mais
                 * polluent le BVH et faussent les tangentes. */
                if (face[0] == face[k - 1] || face[0] == face[k] || face[k - 1] == face[k]) {
                    degenerate++;
                    continue;
                }
                *(uint32_t *)tool_vec_push(&cur_prim->indices) = face[0];
                *(uint32_t *)tool_vec_push(&cur_prim->indices) = face[k - 1];
                *(uint32_t *)tool_vec_push(&cur_prim->indices) = face[k];
            }
        }
    }
    free(text);

    tool_infof("%zu positions, %zu UV, %zu normales -> %zu sommets dédupliqués",
               positions.count, uvs.count, normals.count, verts.count);
    tool_infof("%zu objets", objects.count);
    if (degenerate)       tool_warnf("%zu triangles dégénérés écartés", degenerate);
    if (missing_normals)  tool_warnf("%zu sommets sans normale (remplacée par +Y)", missing_normals);

    /* ------------------------------------------------------------ tangentes */
    {
        /* Les tangentes s'accumulent sur l'ensemble des triangles, tous objets
         * confondus, puisque les sommets sont partagés. */
        tool_vec all_indices; tool_vec_init(&all_indices, sizeof(uint32_t));
        for (size_t o = 0; o < objects.count; ++o) {
            const object *ob = &TOOL_VEC_AT(&objects, object, o);
            for (size_t pr = 0; pr < ob->prims.count; ++pr) {
                const primitive *p = &((const primitive *)ob->prims.data)[pr];
                for (size_t i = 0; i < p->indices.count; ++i) {
                    *(uint32_t *)tool_vec_push(&all_indices) = ((const uint32_t *)p->indices.data)[i];
                }
            }
        }
        generate_tangents((vertex *)verts.data, verts.count,
                          (const uint32_t *)all_indices.data, all_indices.count);
        tool_infof("tangentes générées sur %zu triangles", all_indices.count / 3);
        tool_vec_free(&all_indices);
    }

    /* ------------------------------------------------------- écriture du bin */
    char out_dir[512], out_base[256], bin_name[300], bin_path[1024], lights_path[1024];
    tool_dirname(out_path, out_dir, sizeof out_dir);
    tool_basename_noext(out_path, out_base, sizeof out_base);
    snprintf(bin_name, sizeof bin_name, "%s.bin", out_base);
    snprintf(bin_path, sizeof bin_path, "%s%s", out_dir, bin_name);
    snprintf(lights_path, sizeof lights_path, "%s%s.lights.json", out_dir, out_base);

    writer w = { NULL, NULL, 0 };
    w.bin = fopen(bin_path, "wb");
    if (!w.bin) tool_fatalf("écriture impossible : %s", bin_path);

    /* Attributs, chacun tightly packed dans son propre bufferView. */
    const size_t vcount = verts.count;
    float *scratch = (float *)malloc(sizeof(float) * 4 * (vcount ? vcount : 1));
    if (!scratch) tool_fatalf("mémoire épuisée (tampon d'écriture)");

    float pos_min[3] = { 1e30f, 1e30f, 1e30f }, pos_max[3] = { -1e30f, -1e30f, -1e30f };

    for (size_t i = 0; i < vcount; ++i) {
        const vertex *v = &TOOL_VEC_AT(&verts, vertex, i);
        scratch[i * 3 + 0] = v->position.x;
        scratch[i * 3 + 1] = v->position.y;
        scratch[i * 3 + 2] = v->position.z;
        if (v->position.x < pos_min[0]) pos_min[0] = v->position.x;
        if (v->position.y < pos_min[1]) pos_min[1] = v->position.y;
        if (v->position.z < pos_min[2]) pos_min[2] = v->position.z;
        if (v->position.x > pos_max[0]) pos_max[0] = v->position.x;
        if (v->position.y > pos_max[1]) pos_max[1] = v->position.y;
        if (v->position.z > pos_max[2]) pos_max[2] = v->position.z;
    }
    bin_align(&w, 4);
    const size_t off_pos = bin_write(&w, scratch, sizeof(float) * 3 * vcount);

    for (size_t i = 0; i < vcount; ++i) {
        const vertex *v = &TOOL_VEC_AT(&verts, vertex, i);
        scratch[i * 3 + 0] = v->normal.x;
        scratch[i * 3 + 1] = v->normal.y;
        scratch[i * 3 + 2] = v->normal.z;
    }
    bin_align(&w, 4);
    const size_t off_nrm = bin_write(&w, scratch, sizeof(float) * 3 * vcount);

    for (size_t i = 0; i < vcount; ++i) {
        const vertex *v = &TOOL_VEC_AT(&verts, vertex, i);
        scratch[i * 2 + 0] = v->uv.x;
        scratch[i * 2 + 1] = v->uv.y;
    }
    bin_align(&w, 4);
    const size_t off_uv = bin_write(&w, scratch, sizeof(float) * 2 * vcount);

    for (size_t i = 0; i < vcount; ++i) {
        const vertex *v = &TOOL_VEC_AT(&verts, vertex, i);
        memcpy(&scratch[i * 4], v->tangent, sizeof(float) * 4);
    }
    bin_align(&w, 4);
    const size_t off_tan = bin_write(&w, scratch, sizeof(float) * 4 * vcount);
    free(scratch);

    /* Indices : un bufferView par primitive, pour garder les accesseurs simples. */
    typedef struct { size_t offset, count; } idx_view;
    tool_vec idx_views; tool_vec_init(&idx_views, sizeof(idx_view));

    for (size_t o = 0; o < objects.count; ++o) {
        object *ob = &TOOL_VEC_AT(&objects, object, o);
        for (size_t pr = 0; pr < ob->prims.count; ++pr) {
            primitive *p = &TOOL_VEC_AT(&ob->prims, primitive, pr);
            bin_align(&w, 4);
            idx_view *iv = (idx_view *)tool_vec_push(&idx_views);
            iv->offset = bin_write(&w, p->indices.data, sizeof(uint32_t) * p->indices.count);
            iv->count  = p->indices.count;
        }
    }
    const size_t bin_total = w.bin_offset;
    fclose(w.bin);
    tool_infof("%s : %.1f Mio", bin_name, (double)bin_total / (1024.0 * 1024.0));

    /* ------------------------------------------------------ écriture du glTF */
    FILE *g = fopen(out_path, "wb");
    if (!g) tool_fatalf("écriture impossible : %s", out_path);

    fprintf(g, "{\n");
    fprintf(g, "  \"asset\": { \"version\": \"2.0\", \"generator\": \"Nineteen obj2gltf V15\" },\n");
    fprintf(g, "  \"extensionsUsed\": [\"KHR_materials_emissive_strength\"],\n");
    fprintf(g, "  \"scene\": 0,\n");

    /* --- textures : une image par map_Kd distincte --- */
    tool_vec images; tool_vec_init(&images, sizeof(char[MAX_NAME]));
    for (size_t i = 0; i < mats.count; ++i) {
        material *m = &TOOL_VEC_AT(&mats, material, i);
        if (!m->map_kd[0]) continue;
        int found = -1;
        for (size_t k = 0; k < images.count; ++k) {
            if (strcmp((const char *)images.data + k * MAX_NAME, m->map_kd) == 0) { found = (int)k; break; }
        }
        if (found < 0) {
            char *slot = (char *)tool_vec_push(&images);
            snprintf(slot, MAX_NAME, "%s", m->map_kd);
            found = (int)(images.count - 1);
        }
        m->texture_index = found;
    }

    if (images.count) {
        fprintf(g, "  \"images\": [\n");
        for (size_t i = 0; i < images.count; ++i) {
            fprintf(g, "    { \"uri\": \"textures/%s\" }%s\n",
                    (const char *)images.data + i * MAX_NAME, (i + 1 < images.count) ? "," : "");
        }
        fprintf(g, "  ],\n");
        fprintf(g, "  \"samplers\": [ { \"magFilter\": 9729, \"minFilter\": 9987, \"wrapS\": 10497, \"wrapT\": 10497 } ],\n");
        fprintf(g, "  \"textures\": [\n");
        for (size_t i = 0; i < images.count; ++i) {
            fprintf(g, "    { \"source\": %zu, \"sampler\": 0 }%s\n", i, (i + 1 < images.count) ? "," : "");
        }
        fprintf(g, "  ],\n");
    }

    /* --- matériaux --- */
    fprintf(g, "  \"materials\": [\n");
    for (size_t i = 0; i < mats.count; ++i) {
        material *m = &TOOL_VEC_AT(&mats, material, i);
        m->gltf_index = (int)i;
        fprintf(g, "    {\n      \"name\": \"%s\",\n", m->name);
        fprintf(g, "      \"pbrMetallicRoughness\": {\n");
        fprintf(g, "        \"baseColorFactor\": [%.6f, %.6f, %.6f, %.6f],\n",
                (double)m->base_color[0], (double)m->base_color[1],
                (double)m->base_color[2], (double)m->base_color[3]);
        if (m->texture_index >= 0) {
            fprintf(g, "        \"baseColorTexture\": { \"index\": %d },\n", m->texture_index);
        }
        fprintf(g, "        \"metallicFactor\": %.4f,\n", (double)m->metallic);
        fprintf(g, "        \"roughnessFactor\": %.4f\n", (double)m->roughness);
        fprintf(g, "      }");
        if (m->emissive_strength > 0.0f) {
            fprintf(g, ",\n      \"emissiveFactor\": [%.6f, %.6f, %.6f]",
                    (double)m->emissive[0], (double)m->emissive[1], (double)m->emissive[2]);
            fprintf(g, ",\n      \"extensions\": { \"KHR_materials_emissive_strength\": "
                       "{ \"emissiveStrength\": %.4f } }", (double)m->emissive_strength);
        }
        if (m->base_color[3] < 0.999f) {
            fprintf(g, ",\n      \"alphaMode\": \"BLEND\"");
        }
        fprintf(g, ",\n      \"doubleSided\": false\n    }%s\n", (i + 1 < mats.count) ? "," : "");
    }
    fprintf(g, "  ],\n");

    /* --- bufferViews --- */
    fprintf(g, "  \"buffers\": [ { \"uri\": \"%s\", \"byteLength\": %zu } ],\n", bin_name, bin_total);
    fprintf(g, "  \"bufferViews\": [\n");
    fprintf(g, "    { \"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"target\": 34962 },\n",
            off_pos, sizeof(float) * 3 * vcount);
    fprintf(g, "    { \"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"target\": 34962 },\n",
            off_nrm, sizeof(float) * 3 * vcount);
    fprintf(g, "    { \"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"target\": 34962 },\n",
            off_uv, sizeof(float) * 2 * vcount);
    fprintf(g, "    { \"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"target\": 34962 }",
            off_tan, sizeof(float) * 4 * vcount);
    for (size_t i = 0; i < idx_views.count; ++i) {
        const idx_view *iv = &TOOL_VEC_AT(&idx_views, idx_view, i);
        fprintf(g, ",\n    { \"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"target\": 34963 }",
                iv->offset, sizeof(uint32_t) * iv->count);
    }
    fprintf(g, "\n  ],\n");

    /* --- accessors : 0..3 attributs partagés, puis un par primitive --- */
    fprintf(g, "  \"accessors\": [\n");
    fprintf(g, "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": %zu, \"type\": \"VEC3\", "
               "\"min\": [%.6f, %.6f, %.6f], \"max\": [%.6f, %.6f, %.6f] },\n",
            vcount, (double)pos_min[0], (double)pos_min[1], (double)pos_min[2],
            (double)pos_max[0], (double)pos_max[1], (double)pos_max[2]);
    fprintf(g, "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": %zu, \"type\": \"VEC3\" },\n", vcount);
    fprintf(g, "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": %zu, \"type\": \"VEC2\" },\n", vcount);
    fprintf(g, "    { \"bufferView\": 3, \"componentType\": 5126, \"count\": %zu, \"type\": \"VEC4\" }", vcount);
    for (size_t i = 0; i < idx_views.count; ++i) {
        const idx_view *iv = &TOOL_VEC_AT(&idx_views, idx_view, i);
        fprintf(g, ",\n    { \"bufferView\": %zu, \"componentType\": 5125, \"count\": %zu, \"type\": \"SCALAR\" }",
                4 + i, iv->count);
    }
    fprintf(g, "\n  ],\n");

    /* --- meshes : un par objet, une primitive par matériau --- */
    fprintf(g, "  \"meshes\": [\n");
    size_t prim_counter = 0;
    size_t emitted_meshes = 0;
    for (size_t o = 0; o < objects.count; ++o) {
        const object *ob = &TOOL_VEC_AT(&objects, object, o);
        if (emitted_meshes) fprintf(g, ",\n");
        fprintf(g, "    { \"name\": \"%s\", \"primitives\": [\n", ob->name);
        for (size_t pr = 0; pr < ob->prims.count; ++pr) {
            const primitive *p = &((const primitive *)ob->prims.data)[pr];
            fprintf(g, "      { \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, "
                       "\"TEXCOORD_0\": 2, \"TANGENT\": 3 }, \"indices\": %zu",
                    4 + prim_counter);
            if (p->material >= 0) fprintf(g, ", \"material\": %d", p->material);
            fprintf(g, ", \"mode\": 4 }%s\n", (pr + 1 < ob->prims.count) ? "," : "");
            prim_counter++;
        }
        fprintf(g, "    ] }");
        emitted_meshes++;
    }
    fprintf(g, "\n  ],\n");

    /* --- nodes et scène --- */
    fprintf(g, "  \"nodes\": [\n");
    for (size_t o = 0; o < objects.count; ++o) {
        const object *ob = &TOOL_VEC_AT(&objects, object, o);
        fprintf(g, "    { \"name\": \"%s\", \"mesh\": %zu }%s\n",
                ob->name, o, (o + 1 < objects.count) ? "," : "");
    }
    fprintf(g, "  ],\n");

    fprintf(g, "  \"scenes\": [ { \"name\": \"salle\", \"nodes\": [");
    for (size_t o = 0; o < objects.count; ++o) {
        fprintf(g, "%s%zu", o ? ", " : "", o);
    }
    fprintf(g, "] } ]\n}\n");
    fclose(g);

    /* -------------------------------------- lumières et bornes (JSON annexe) */
    FILE *lf = fopen(lights_path, "wb");
    if (!lf) tool_fatalf("écriture impossible : %s", lights_path);

    fprintf(lf, "{\n  \"_comment\": \"Généré par obj2gltf — lumières et bornes déduites des noms d'objets et de l'émissivité des matériaux. Ajustable à la main : le jeu relit ce fichier au démarrage.\",\n");
    fprintf(lf, "  \"bounds\": { \"min\": [%.4f, %.4f, %.4f], \"max\": [%.4f, %.4f, %.4f] },\n",
            (double)pos_min[0], (double)pos_min[1], (double)pos_min[2],
            (double)pos_max[0], (double)pos_max[1], (double)pos_max[2]);

    fprintf(lf, "  \"lights\": [\n");
    size_t light_count = 0;

    /*
     * Génération des sources lumineuses.
     *
     * Le modèle d'origine peignait la lumière dans les textures : les appliques,
     * les panneaux du plafond et les écrans de bornes étaient des surfaces
     * claires, pas des émetteurs. Résultat, une salle uniformément sombre dès
     * qu'on l'éclaire pour de vrai.
     *
     * On corrige en deux temps :
     *
     *  1. Toute surface au matériau émissif devient une **source étendue**.
     *     Une grande surface (le plafond lumineux fait plusieurs mètres carrés)
     *     ne peut pas être représentée par un point unique : le sol serait
     *     éclairé par un projecteur au lieu d'une nappe. On répartit donc
     *     plusieurs points sur son emprise, proportionnellement à son aire.
     *
     *  2. Les luminaires reconnus par leur nom gardent leur source dédiée,
     *     chaude et scintillante.
     *
     * L'intensité est proportionnelle à l'aire : un panneau de 4 m² doit
     * éclairer quatre fois plus qu'un panneau d'un mètre carré, à luminance
     * égale. C'est ce qui manquait le plus.
     */
    for (size_t o = 0; o < objects.count; ++o) {
        const object *ob = &TOOL_VEC_AT(&objects, object, o);
        if (!ob->has_bounds) continue;

        /* Aire émissive de l'objet et couleur associée. */
        double emissive_area = 0.0;
        float  emissive_color[3] = { 1.0f, 1.0f, 1.0f };
        float  emissive_strength = 0.0f;

        for (size_t pr = 0; pr < ob->prims.count; ++pr) {
            const primitive *p = &((const primitive *)ob->prims.data)[pr];
            if (p->material < 0) continue;
            const material *m = &TOOL_VEC_AT(&mats, material, (size_t)p->material);
            if (m->emissive_strength <= 0.0f) continue;

            const uint32_t *idx = (const uint32_t *)p->indices.data;
            for (size_t i = 0; i + 2 < p->indices.count; i += 3) {
                const vertex *a = &TOOL_VEC_AT(&verts, vertex, idx[i]);
                const vertex *b = &TOOL_VEC_AT(&verts, vertex, idx[i + 1]);
                const vertex *c = &TOOL_VEC_AT(&verts, vertex, idx[i + 2]);
                const v3 e1 = { b->position.x - a->position.x, b->position.y - a->position.y,
                                b->position.z - a->position.z };
                const v3 e2 = { c->position.x - a->position.x, c->position.y - a->position.y,
                                c->position.z - a->position.z };
                const v3 cr = { e1.y * e2.z - e1.z * e2.y,
                                e1.z * e2.x - e1.x * e2.z,
                                e1.x * e2.y - e1.y * e2.x };
                emissive_area += 0.5 * sqrt((double)(cr.x * cr.x + cr.y * cr.y + cr.z * cr.z));
            }
            if (m->emissive_strength > emissive_strength) {
                emissive_strength = m->emissive_strength;
                emissive_color[0] = m->emissive[0];
                emissive_color[1] = m->emissive[1];
                emissive_color[2] = m->emissive[2];
            }
        }

        const bool named_fixture = name_contains_ci(ob->name, "sconce")
                                || name_contains_ci(ob->name, "neon")
                                || name_contains_ci(ob->name, "lampe")
                                || name_contains_ci(ob->name, "lumiere");

        if (emissive_area < 0.02 && !named_fixture) continue;

        const v3 bmin = ob->bbox_min, bmax = ob->bbox_max;
        const v3 extent = { bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z };

        /* Nombre de points : un tous les 2.5 m² environ, borné pour ne pas
         * saturer la boucle d'éclairage sur un plafond entier. */
        int nx = 1, nz = 1;
        if (emissive_area > 2.5) {
            nx = (int)(extent.x / 2.4f) + 1;
            nz = (int)(extent.z / 2.4f) + 1;
            if (nx > 4) nx = 4;
            if (nz > 4) nz = 4;
        }
        const int points = nx * nz;

        /* Luminance -> intensité. Calibré pour que le plafond lumineux éclaire
         * la salle sans la brûler, avec l'exposition par défaut. */
        float total_intensity = named_fixture
            ? 340.0f
            : (float)(emissive_area * 130.0 * (double)ns_max_strength(emissive_strength));
        if (total_intensity < 30.0f) total_intensity = 30.0f;
        if (total_intensity > 4200.0f) total_intensity = 4200.0f;
        const float per_point = total_intensity / (float)points;

        float color[3];
        if (named_fixture) {
            /* Applique murale : blanc chaud, ce qui contraste avec le froid des
             * néons de bornes et donne du relief à l'éclairage. */
            color[0] = 1.0f; color[1] = 0.84f; color[2] = 0.62f;
        } else {
            color[0] = emissive_color[0];
            color[1] = emissive_color[1];
            color[2] = emissive_color[2];
        }

        /* Portée : au-delà, la contribution passe sous le bruit de
         * quantification. Racine de l'intensité, facteur ajusté à l'œil. */
        const float range = sqrtf(per_point) * 1.35f + 1.5f;

        for (int gz = 0; gz < nz; ++gz) {
            for (int gx = 0; gx < nx; ++gx) {
                const float fx = (nx == 1) ? 0.5f : ((float)gx + 0.5f) / (float)nx;
                const float fz = (nz == 1) ? 0.5f : ((float)gz + 0.5f) / (float)nz;

                float px = bmin.x + extent.x * fx;
                float pz = bmin.z + extent.z * fz;
                /* Décollé de la surface vers l'intérieur de la pièce, sinon la
                 * source est dans la géométrie et n'éclaire rien. */
                float py = bmin.y + extent.y * 0.5f;
                if (extent.y < 0.6f) py = bmin.y - 0.25f;   /* panneau horizontal : plafond */

                if (light_count >= 96) break;
                if (light_count) fprintf(lf, ",\n");
                fprintf(lf, "    { \"name\": \"%s#%d\", \"type\": \"point\", "
                            "\"position\": [%.4f, %.4f, %.4f], "
                            "\"color\": [%.4f, %.4f, %.4f], \"intensity\": %.3f, "
                            "\"range\": %.3f, \"flicker\": %s, \"area\": %.3f }",
                        ob->name, gz * nx + gx,
                        (double)px, (double)py, (double)pz,
                        (double)color[0], (double)color[1], (double)color[2],
                        (double)per_point, (double)range,
                        (named_fixture && name_contains_ci(ob->name, "neon")) ? "true" : "false",
                        emissive_area);
                light_count++;
            }
        }
    }
    fprintf(lf, "\n  ],\n");

    /* Bornes, numérotées dans un ordre spatial stable pour que les slots ne
     * changent pas d'une reconversion à l'autre. */
    tool_vec cabs; tool_vec_init(&cabs, sizeof(size_t));
    for (size_t o = 0; o < objects.count; ++o) {
        const object *ob = &TOOL_VEC_AT(&objects, object, o);
        if (ob->has_bounds && object_is_cabinet(ob->name)) {
            *(size_t *)tool_vec_push(&cabs) = o;
        }
    }
    /* Tri par insertion : quinze éléments, la simplicité prime. */
    for (size_t i = 1; i < cabs.count; ++i) {
        const size_t key = TOOL_VEC_AT(&cabs, size_t, i);
        const object *ko = &TOOL_VEC_AT(&objects, object, key);
        size_t j = i;
        while (j > 0) {
            const size_t prev = TOOL_VEC_AT(&cabs, size_t, j - 1);
            const object *po = &TOOL_VEC_AT(&objects, object, prev);
            const float kx = (ko->bbox_min.x + ko->bbox_max.x) * 0.5f;
            const float px = (po->bbox_min.x + po->bbox_max.x) * 0.5f;
            const float kz = (ko->bbox_min.z + ko->bbox_max.z) * 0.5f;
            const float pz = (po->bbox_min.z + po->bbox_max.z) * 0.5f;
            if (px < kx || (px == kx && pz <= kz)) break;
            TOOL_VEC_AT(&cabs, size_t, j) = prev;
            j--;
        }
        TOOL_VEC_AT(&cabs, size_t, j) = key;
    }

    fprintf(lf, "  \"_cabinetsNote\": \"Les 15 bornes ont été dupliquées dans Blender depuis une seule et portent toutes le même nom : le jeu ne peut pas être déduit du modèle. Elles sont numérotées ici par ordre spatial stable (X croissant, puis Z) ; l'affectation des jeux se fait dans assets/scene/cabinets.json, que le moteur fusionne par slot.\",\n");
    fprintf(lf, "  \"cabinets\": [\n");
    for (size_t i = 0; i < cabs.count; ++i) {
        const object *ob = &TOOL_VEC_AT(&objects, object, TOOL_VEC_AT(&cabs, size_t, i));
        if (i) fprintf(lf, ",\n");
        fprintf(lf, "    { \"slot\": %zu, \"name\": \"%s\", "
                    "\"bboxMin\": [%.4f, %.4f, %.4f], \"bboxMax\": [%.4f, %.4f, %.4f] }",
                i, ob->name,
                (double)ob->bbox_min.x, (double)ob->bbox_min.y, (double)ob->bbox_min.z,
                (double)ob->bbox_max.x, (double)ob->bbox_max.y, (double)ob->bbox_max.z);
    }
    fprintf(lf, "\n  ]\n}\n");
    fclose(lf);

    const size_t cab_count = cabs.count;
    tool_infof("%zu lumières déduites, %zu bornes repérées (jeux à affecter dans cabinets.json)",
               light_count, cab_count);
    tool_infof("écrit : %s", out_path);
    tool_infof("écrit : %s", lights_path);

    /* Les allocations restantes appartiennent au processus qui se termine ;
     * les libérer une par une n'apporterait rien ici. */
    dedup_free(&dd);
    return 0;
}
