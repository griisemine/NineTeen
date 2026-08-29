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

/* Définies plus bas, utilisées par le chargement. */
static void add_cabinet_screen_lights(ns_scene *s);
static void detect_points_of_interest(ns_scene *s, const char *gltf_logical);

/*
 * Nature d'un lieu, depuis son nom.
 *
 * Deux appelants aux exigences opposées, et c'est pour cela que la table est ici
 * plutôt que dans l'un des deux : la salle reconstruite passe une nature
 * **déclarée**, qu'on veut voir refusée si elle est mal orthographiée ; l'ancien
 * chemin passe un nom de nœud glTF où l'on cherche une sous-chaîne, faute de
 * mieux. Une seule liste de vocabulaire, deux façons de l'interroger.
 */
static ns_poi_kind ns_poi_kind_from_name(const char *name)
{
    static const struct { const char *token; ns_poi_kind kind; } table[] = {
        { "billiard",    NS_POI_BILLIARD },
        { "billard",     NS_POI_BILLIARD },
        { "sofa",        NS_POI_SOFA },
        { "canape",      NS_POI_SOFA },
        { "bar",         NS_POI_BAR },
        { "accueil",     NS_POI_BAR },
        { "radio",       NS_POI_RADIO },
        { "toilette",    NS_POI_TOILETS },
        { "toilettes",   NS_POI_TOILETS },
        { "toilets",     NS_POI_TOILETS },
        { "lavabo",      NS_POI_TOILETS },
        { "porte",       NS_POI_EXIT },
        { "exit",        NS_POI_EXIT },
        { "leaderboard", NS_POI_LEADERBOARD },
        { "classement",  NS_POI_LEADERBOARD },
    };
    if (!name || !name[0]) return NS_POI_NONE;
    for (size_t t = 0; t < SDL_arraysize(table); ++t) {
        if (SDL_strcasecmp(name, table[t].token) == 0) return table[t].kind;
    }
    return NS_POI_NONE;
}

static ns_poi_kind ns_poi_kind_in_name(const char *name)
{
    static const struct { const char *token; ns_poi_kind kind; } table[] = {
        { "billiard", NS_POI_BILLIARD }, { "billard", NS_POI_BILLIARD },
        { "sofa", NS_POI_SOFA },         { "canape", NS_POI_SOFA },
        { "bar_", NS_POI_BAR },          { "accueil", NS_POI_BAR },
        { "radio", NS_POI_RADIO },       { "toilette", NS_POI_TOILETS },
        { "lavabo", NS_POI_TOILETS },    { "porte", NS_POI_EXIT },
        { "exit", NS_POI_EXIT },
    };
    if (!name) return NS_POI_NONE;
    for (size_t t = 0; t < SDL_arraysize(table); ++t) {
        if (SDL_strcasestr(name, table[t].token)) return table[t].kind;
    }
    return NS_POI_NONE;
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
        /* 0,22 m par défaut : l'ampoule d'applique pour laquelle la constante
         * globale du shader avait été dimensionnée. Les fichiers qui ne déclarent
         * pas de rayon gardent donc exactement le comportement d'avant. */
        l->source_radius = ns_json_get_float(&doc, e, "radius", 0.22f);

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

/*
 * Repli pour les deux points que la main vise, quand la salle ne les déclare pas
 * — c'est le cas de celle de 2020, dont aucun objet ne s'appelle autrement que
 * `Cube.0XX`.
 *
 * Ce n'est **pas** une heuristique de plus déguisée : c'est un repli assumé, qui
 * ne sert qu'au chemin `legacy`, et dont on sait qu'il se trompe de quelques
 * centimètres. Le chemin normal lit les chiffres écrits par `roomgen` à trois
 * lignes des boîtes qu'ils désignent. La différence tient dans le nom de la
 * fonction, et elle compte : un repli qu'on n'appelle pas « repli » finit par
 * être pris pour la vérité.
 */
static const char *const g_footstep_labels[NS_STEP_COUNT] = {
    "", "moquette", "carrelage", "bois", "beton", "estrade"
};

const char *ns_footstep_label(ns_footstep k)
{
    if (k <= NS_STEP_NONE || k >= NS_STEP_COUNT) return "";
    return g_footstep_labels[k];
}

static ns_footstep footstep_from_name(const char *name)
{
    if (!name || !name[0]) return NS_STEP_NONE;
    for (int i = 1; i < NS_STEP_COUNT; ++i) {
        if (SDL_strcasecmp(name, g_footstep_labels[i]) == 0) return (ns_footstep)i;
    }
    /* Une classe inconnue n'est pas une erreur fatale — on marche, simplement, sur
     * du générique. Mais elle se dit : c'est presque toujours une faute de frappe
     * dans la description de salle, et un pas muet ne l'aurait jamais révélée. */
    NS_WARN("classe de pas « %s » inconnue — traitée comme de la moquette", name);
    return NS_STEP_MOQUETTE;
}

static void derive_hand_anchors(ns_cabinet *cab)
{
    const ns_v3 extent = ns_aabb_extent(cab->bounds);
    const ns_v3 centre = ns_aabb_center(cab->bounds);
    const ns_v3 half   = ns_v3_scale(extent, 0.5f);
    const ns_v3 fwd    = cab->screen_normal;

    /* Panneau de commande : la saillie devant le caisson, à mi-hauteur d'homme.
     * 0,93 m et 1,06 de demi-profondeur sont les cotes de `build_cabinet`, donc
     * exprimées en fraction pour survivre à une borne d'une autre taille. */
    cab->panel_centre = ns_v3_make(
        centre.x + fwd.x * (fabsf(fwd.x) > 0.5f ? half.x * 1.06f : 0.0f),
        cab->bounds.min.y + extent.y * 0.50f,
        centre.z + fwd.z * (fabsf(fwd.z) > 0.5f ? half.z * 1.06f : 0.0f));

    cab->coin_slot = ns_v3_make(
        centre.x + fwd.x * (fabsf(fwd.x) > 0.5f ? half.x * 1.02f : 0.0f),
        cab->bounds.min.y + extent.y * 0.28f,
        centre.z + fwd.z * (fabsf(fwd.z) > 0.5f ? half.z * 1.02f : 0.0f));

}

/*
 * Le manche, déduit du panneau — et déduit SÉPARÉMENT du reste.
 *
 * Une salle décrite avant que `stickTop` existe déclare bien son panneau et sa
 * fente : redériver les trois parce qu'il en manque un remplacerait deux cotes
 * justes par deux approximations. Cette fonction ne touche donc qu'au manche, et
 * elle part de `panel_centre`, qu'il soit déclaré ou déduit.
 *
 * « À gauche du panneau » s'entend du point de vue du joueur, pas du monde : on
 * tourne la normale d'un quart de tour autour de la verticale, ce qui vaut pour
 * les quatre orientations sans avoir à les énumérer.
 */
static void derive_stick_anchor(ns_cabinet *cab)
{
    const ns_v3 extent = ns_aabb_extent(cab->bounds);
    const ns_v3 half   = ns_v3_scale(extent, 0.5f);
    const ns_v3 fwd    = cab->screen_normal;
    const ns_v3 left   = ns_v3_make(-fwd.z, 0.0f, fwd.x);
    const float side   = ns_maxf(fabsf(half.x), fabsf(half.z)) * 0.28f;

    cab->stick_top = ns_v3_add(cab->panel_centre,
                               ns_v3_add(ns_v3_scale(left, side),
                                         ns_v3_make(0.0f, extent.y * 0.035f, 0.0f)));
}

/*
 * Fichier annexe de scène : `<nom>.scene.json`.
 *
 * C'est le fichier que `roomgen` écrira, et il porte ce que la salle *déclare*
 * au lieu de ce que le moteur devine : son échelle, son emprise jouable, le
 * point d'apparition du joueur, et les points de vue nommés qui rendent les
 * captures comparables d'une salle à l'autre.
 *
 * Il est optionnel. Absent, on retombe sur la déduction — c'est ce qui permet à
 * l'ancienne salle et à la nouvelle de coexister sans branchement conditionnel
 * dans le reste du moteur.
 */
static void load_scene_sidecar(ns_scene *s, const char *logical)
{
    /*
     * On sonde d'abord la présence du fichier.
     *
     * `ns_file_read_all` journalise une ERREUR quand il ne résout pas son chemin —
     * ce qui est juste pour un asset requis, et faux ici : l'absence de ce fichier
     * est le cas *normal* pour une salle dont tout est déduit. Sans cette sonde, le
     * code déclarait l'absence anodine tandis que la couche du dessous l'annonçait
     * comme une erreur, et la cible `render-compare` crachait quatre ERREUR
     * parfaitement attendues. `ns_path_resolve` échoue en silence sur un fichier
     * absent : c'est donc la bonne façon de demander « est-il là ? ».
     */
    char resolved[1024];
    if (!ns_path_resolve(logical, resolved, sizeof resolved)) return;

    ns_arena_mark mark = ns_arena_save(&s->arena);
    size_t size = 0;
    char *text = (char *)ns_file_read_all(&s->arena, logical, &size);
    if (!text) {
        ns_arena_restore(&s->arena, mark);
        return;
    }

    ns_json doc;
    if (!ns_json_parse(&doc, text, size, &s->arena)) {
        NS_ERROR("fichier de scène illisible : %s", logical);
        ns_arena_restore(&s->arena, mark);
        return;
    }
    const ns_json_value *root = ns_json_root(&doc);

    const float upm = ns_json_get_float(&doc, root, "unitsPerMetre", 0.0f);
    if (upm > 0.0f) s->units_per_metre = upm;

    const ns_json_value *rb = ns_json_get(&doc, root, "roomBounds");
    if (rb) {
        float bmin[3], bmax[3];
        ns_json_get_vec3(&doc, rb, "min", bmin, 0.0f);
        ns_json_get_vec3(&doc, rb, "max", bmax, 0.0f);
        const ns_aabb declared = { ns_v3_make(bmin[0], bmin[1], bmin[2]),
                                   ns_v3_make(bmax[0], bmax[1], bmax[2]) };
        if (ns_aabb_valid(declared)) {
            s->room_bounds = declared;
            s->has_declared_room_bounds = true;
        }
    }

    const ns_json_value *start = ns_json_get(&doc, root, "playerStart");
    if (start) {
        float p[3];
        ns_json_get_vec3(&doc, start, "position", p, 0.0f);
        s->player_start = ns_v3_make(p[0], p[1], p[2]);
        s->player_yaw = ns_json_get_float(&doc, start, "yaw", -90.0f) * NS_DEG2RAD;
        s->has_player_start = true;
    }

    const ns_json_value *views = ns_json_get(&doc, root, "captures");
    const int view_count = ns_json_array_count(&doc, views);
    for (int i = 0; i < view_count && s->viewpoint_count < NS_MAX_VIEWPOINTS; ++i) {
        const ns_json_value *e = ns_json_at(&doc, views, i);
        ns_viewpoint *v = &s->viewpoints[s->viewpoint_count];
        SDL_zerop(v);
        ns_json_get_string(&doc, e, "name", v->name, sizeof v->name);
        if (!v->name[0]) continue;      /* un point de vue sans nom est inutilisable */

        float p[3];
        ns_json_get_vec3(&doc, e, "position", p, 0.0f);
        v->position = ns_v3_make(p[0], p[1], p[2]);
        v->yaw   = ns_json_get_float(&doc, e, "yaw", 0.0f) * NS_DEG2RAD;
        v->pitch = ns_json_get_float(&doc, e, "pitch", 0.0f) * NS_DEG2RAD;
        v->orbit = ns_json_get_bool(&doc, e, "orbit", false);
        v->orbit_radius = ns_json_get_float(&doc, e, "radius", 0.0f);
        v->orbit_height = ns_json_get_float(&doc, e, "height", 0.0f);
        s->viewpoint_count++;
    }

    /*
     * Les bornes, telles que la salle les déclare.
     *
     * Ce bloc remplace trois déductions, et il faut être précis sur ce qu'elles
     * coûtaient : les fractions inventées de la boîte englobante plaçaient le
     * centre de l'écran ~31 cm trop bas ; l'orientation venait du barycentre du
     * troupeau de bornes, donc une borne isolée regardait n'importe où ; et
     * l'affectation des jeux suivait un tri en X puis Z qui ne correspondait pas
     * aux images peintes sur les marquees.
     */
    /* Le tableau du bar. −1 quand la salle n'en déclare pas, ce qui est le cas
     * de celle de 2020 : elle continue d'afficher son image peinte. */
    s->scoreboard_material = (int32_t)ns_json_get_i64(&doc, root, "scoreboard", -1);

    const ns_json_value *cabs = ns_json_get(&doc, root, "cabinets");
    const int cab_count = ns_json_array_count(&doc, cabs);
    if (cab_count > 0) {
        s->cabinet_count = 0;
        for (int i = 0; i < cab_count && s->cabinet_count < NS_MAX_CABINETS; ++i) {
            const ns_json_value *e = ns_json_at(&doc, cabs, i);
            ns_cabinet *c = &s->cabinets[s->cabinet_count];
            SDL_zerop(c);

            ns_json_get_string(&doc, e, "name", c->name, sizeof c->name);
            ns_json_get_string(&doc, e, "game", c->game, sizeof c->game);
            ns_json_get_string(&doc, e, "difficulty", c->difficulty, sizeof c->difficulty);
            c->slot = (int)ns_json_get_float(&doc, e, "slot", (float)i);
            c->attract = ns_json_get_bool(&doc, e, "attract", true);

            float v[3];
            ns_json_get_vec3(&doc, e, "bboxMin", v, 0.0f);
            c->bounds.min = ns_v3_make(v[0], v[1], v[2]);
            ns_json_get_vec3(&doc, e, "bboxMax", v, 0.0f);
            c->bounds.max = ns_v3_make(v[0], v[1], v[2]);
            ns_json_get_vec3(&doc, e, "screenCenter", v, 0.0f);
            c->screen_center = ns_v3_make(v[0], v[1], v[2]);
            ns_json_get_vec3(&doc, e, "screenNormal", v, 0.0f);
            c->screen_normal = ns_v3_norm(ns_v3_make(v[0], v[1], v[2]));
            ns_json_get_vec3(&doc, e, "playerAnchor", v, 0.0f);
            c->player_anchor = ns_v3_make(v[0], v[1], v[2]);
            c->screen_material = (int32_t)ns_json_get_float(&doc, e, "screenMaterial", -1.0f);
            ns_json_get_vec3(&doc, e, "panelCentre", v, NAN);
            c->panel_centre = ns_v3_make(v[0], v[1], v[2]);
            ns_json_get_vec3(&doc, e, "coinSlot", v, NAN);
            c->coin_slot = ns_v3_make(v[0], v[1], v[2]);
            ns_json_get_vec3(&doc, e, "stickTop", v, NAN);
            c->stick_top = ns_v3_make(v[0], v[1], v[2]);

            /* Deux scalaires plutôt qu'un couple : le lecteur du moteur n'a que
             * `vec3`, et lui faire lire un tableau de deux éléments demanderait
             * une API de plus pour économiser une ligne. */
            c->screen_width  = ns_json_get_float(&doc, e, "screenWidth", 0.0f);
            c->screen_height = ns_json_get_float(&doc, e, "screenHeight", 0.0f);

            /* Une salle écrite avant que ces deux clés existent reste chargeable :
             * la valeur par défaut du lecteur est NaN, précisément pour qu'une
             * absence se distingue d'un zéro légitime — l'origine du monde est une
             * coordonnée valide, et une borne pourrait s'y trouver. */
            if (isnan(c->panel_centre.x) || isnan(c->coin_slot.x)) derive_hand_anchors(c);
            if (isnan(c->stick_top.x)) derive_stick_anchor(c);

            s->cabinet_count++;
        }
        s->has_declared_cabinets = true;
    }

    /*
     * Les classes de pas, dans l'ordre des matériaux du glTF — donc directement
     * indexables par le `ground_material` que renvoie la collision. Le tableau
     * est alloué dans l'arène de la scène : il vit et meurt avec elle.
     */
    const ns_json_value *dust = ns_json_get(&doc, root, "dust");
    const int dust_count = ns_json_array_count(&doc, dust);
    s->dust_count = 0;
    for (int i = 0; i < dust_count && s->dust_count < NS_MAX_DUST_ZONES; ++i) {
        const ns_json_value *e = ns_json_at(&doc, dust, i);
        ns_dust_zone *z = &s->dust[s->dust_count++];
        SDL_zerop(z);
        ns_json_get_string(&doc, e, "name", z->name, sizeof z->name);
        float v[3];
        ns_json_get_vec3(&doc, e, "min", v, 0.0f); z->bounds.min = ns_v3_make(v[0], v[1], v[2]);
        ns_json_get_vec3(&doc, e, "max", v, 0.0f); z->bounds.max = ns_v3_make(v[0], v[1], v[2]);
        ns_json_get_vec3(&doc, e, "drift", z->drift, 0.0f);
        ns_json_get_vec3(&doc, e, "color", z->color, 1.0f);
        z->density = ns_json_get_float(&doc, e, "density", 0.5f);
        z->size = ns_json_get_float(&doc, e, "size", 0.02f);
        z->brightness = ns_json_get_float(&doc, e, "brightness", 0.5f);
    }
    if (s->dust_count) NS_INFO("%u zone(s) de poussière", s->dust_count);

    const ns_json_value *zones = ns_json_get(&doc, root, "soundZones");
    const int zone_count = ns_json_array_count(&doc, zones);
    s->sound_zone_count = 0;
    for (int i = 0; i < zone_count && s->sound_zone_count < NS_MAX_SOUND_ZONES; ++i) {
        const ns_json_value *e = ns_json_at(&doc, zones, i);
        ns_sound_zone *z = &s->sound_zone[s->sound_zone_count++];
        SDL_zerop(z);
        ns_json_get_string(&doc, e, "name", z->name, sizeof z->name);
        float v[3];
        ns_json_get_vec3(&doc, e, "min", v, 0.0f); z->bounds.min = ns_v3_make(v[0], v[1], v[2]);
        ns_json_get_vec3(&doc, e, "max", v, 0.0f); z->bounds.max = ns_v3_make(v[0], v[1], v[2]);
        z->wet = ns_json_get_float(&doc, e, "wet", 0.0f);
        z->decay = ns_json_get_float(&doc, e, "decay", 0.0f);
    }
    if (s->sound_zone_count) NS_INFO("%u zone(s) sonore(s)", s->sound_zone_count);

    const ns_json_value *steps = ns_json_get(&doc, root, "materialFootsteps");
    const int step_count = ns_json_array_count(&doc, steps);
    if (step_count > 0) {
        s->material_footstep = (ns_footstep *)ns_arena_alloc(
            &s->arena, sizeof(ns_footstep) * (size_t)step_count, _Alignof(ns_footstep));
        if (s->material_footstep) {
            int declared = 0;
            for (int i = 0; i < step_count; ++i) {
                char name[32] = { 0 };
                ns_json_string(&doc, ns_json_at(&doc, steps, i), name, sizeof name);
                s->material_footstep[i] = footstep_from_name(name);
                if (s->material_footstep[i] != NS_STEP_NONE) declared++;
            }
            NS_INFO("%d matériau(x) portent une classe de pas", declared);
        }
    }

    const ns_json_value *pois = ns_json_get(&doc, root, "pois");
    const int poi_count = ns_json_array_count(&doc, pois);
    if (poi_count > 0) {
        s->poi_count = 0;
        for (int i = 0; i < poi_count && s->poi_count < NS_MAX_POI; ++i) {
            const ns_json_value *e = ns_json_at(&doc, pois, i);
            ns_poi *p = &s->pois[s->poi_count];
            SDL_zerop(p);

            ns_json_get_string(&doc, e, "name", p->name, sizeof p->name);
            char kind[24];
            ns_json_get_string(&doc, e, "kind", kind, sizeof kind);
            p->kind = ns_poi_kind_from_name(kind);
            if (p->kind == NS_POI_NONE) {
                NS_WARN("lieu « %s » de nature inconnue (« %s ») : ignoré", p->name, kind);
                continue;
            }

            float v[3];
            ns_json_get_vec3(&doc, e, "boundsMin", v, 0.0f);
            p->bounds.min = ns_v3_make(v[0], v[1], v[2]);
            ns_json_get_vec3(&doc, e, "boundsMax", v, 0.0f);
            p->bounds.max = ns_v3_make(v[0], v[1], v[2]);
            ns_json_get_vec3(&doc, e, "anchor", v, 0.0f);
            p->anchor = ns_v3_make(v[0], v[1], v[2]);

            s->poi_count++;
        }
        s->has_declared_pois = true;
    }

    NS_INFO("scène déclarée : %.3f unité/m, %u point(s) de vue%s, %u borne(s), "
            "%u lieu(x)",
            (double)s->units_per_metre, s->viewpoint_count,
            s->has_declared_room_bounds ? ", emprise jouable déclarée" : "",
            s->has_declared_cabinets ? s->cabinet_count : 0u,
            s->has_declared_pois ? s->poi_count : 0u);
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

    /*
     * Orientation des bornes.
     *
     * Une borne d'arcade est plus profonde que large : sa face avant est donc
     * perpendiculaire à son plus GRAND côté horizontal, pas au plus petit — la
     * première version avait l'inverse et faisait éclairer les bornes vers
     * l'intérieur du meuble.
     *
     * Reste le sens. Les bornes sont disposées en deux rangées face à face de
     * part et d'autre d'une allée ; chacune regarde donc vers le barycentre de
     * l'ensemble. C'est une heuristique, mais elle est exacte sur cette salle et
     * ne dépend d'aucune valeur codée en dur.
     */
    ns_v3 flock = ns_v3_zero();
    for (uint32_t c = 0; c < s->cabinet_count; ++c) {
        flock = ns_v3_add(flock, ns_aabb_center(s->cabinets[c].bounds));
    }
    if (s->cabinet_count) flock = ns_v3_scale(flock, 1.0f / (float)s->cabinet_count);

    for (uint32_t c = 0; c < s->cabinet_count; ++c) {
        ns_cabinet *cab = &s->cabinets[c];
        const ns_v3 extent = ns_aabb_extent(cab->bounds);
        const ns_v3 centre = ns_aabb_center(cab->bounds);
        const ns_v3 toward = ns_v3_sub(flock, centre);

        if (extent.x >= extent.z) {
            cab->screen_normal = ns_v3_make(toward.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
        } else {
            cab->screen_normal = ns_v3_make(0.0f, 0.0f, toward.z >= 0.0f ? 1.0f : -1.0f);
        }

        /* L'écran est à mi-hauteur de la fraction déclarée, sur la face avant. */
        const ns_v3 half = ns_v3_scale(extent, 0.5f);
        cab->screen_center = ns_v3_make(
            centre.x + cab->screen_normal.x * half.x * 0.92f,
            cab->bounds.min.y + extent.y * (origin_f[1] + size_f[1] * 0.5f),
            centre.z + cab->screen_normal.z * half.z * 0.92f);

        cab->player_anchor = ns_v3_add(cab->screen_center, ns_v3_scale(cab->screen_normal, 1.0f));
        cab->player_anchor.y = cab->bounds.min.y;
        cab->screen_material = -1;   /* la salle de 2020 ne le déclare pas */
        derive_hand_anchors(cab);
        derive_stick_anchor(cab);
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

    /*
     * Table des objets nommés, construite **pendant** le parcours des nœuds : le
     * nom est là, sous la main. La version précédente relançait une seconde
     * analyse complète du glTF juste pour relire ces noms — `cgltf_parse_file`
     * *et* `cgltf_load_buffers` sur 5,8 Mio de binaire, une deuxième fois dans le
     * même chargement.
     */
    out->objects = NS_ARENA_ARRAY(&out->arena, ns_scene_object,
                                  data->nodes_count ? data->nodes_count : 1);
    out->object_count = 0;

    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node *node = &data->nodes[n];
        if (!node->mesh) continue;

        cgltf_float world[16];
        cgltf_node_transform_world(node, world);

        /* Un objet par nœud porteur de maillage. La tranche de lots est contiguë
         * parce que l'écrivain glTF conserve l'ordre des nœuds — ce qui est
         * précisément pourquoi il ne trie pas par matériau. */
        ns_scene_object *obj = NULL;
        if (out->object_count < data->nodes_count) {
            obj = &out->objects[out->object_count];
            SDL_zerop(obj);
            SDL_strlcpy(obj->name, node->name ? node->name : "(sans nom)", sizeof obj->name);
            obj->kind        = NS_OBJ_DECOR;
            obj->bounds      = ns_aabb_empty();
            obj->first_batch = out->batch_count;
            obj->batch_count = 0;
        }

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
            batch->object      = obj ? (int32_t)out->object_count : -1;
            batch->bounds      = prim_bounds;

            if (obj) {
                obj->batch_count++;
                obj->bounds = ns_aabb_union(obj->bounds, prim_bounds);
            }

            scene_bounds = ns_aabb_union(scene_bounds, prim_bounds);
        }

        /* Un nœud dont aucune primitive n'a survécu (non triangulée, sans
         * indices) ne devient pas un objet : une tranche de zéro lot n'aurait
         * rien à désigner. */
        if (obj && obj->batch_count > 0) out->object_count++;
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
            basename_noext(data->images[i].uri, name, sizeof name);

            /*
             * La couleur de base vient de la carte DÉ-CUITE quand `texgen` en a
             * produit une, et de l'image du glTF sinon.
             *
             * Ce n'est pas une préférence esthétique. Les textures de 2020 ont
             * l'ombre PEINTE DEDANS — leur moteur n'éclairait rien, donc la
             * pénombre devait être dans l'image. Mesuré : la moquette a une
             * réflectance linéaire de 0,029 et le plafond de 0,002, quand du
             * bitume frais en fait 0,04. Les éclairer revient à appliquer la
             * pénombre deux fois, et aucune quantité de lumière ne rattrape une
             * réflectance de deux pour mille.
             *
             * `tools/texgen.c` explique la conversion. Ici, la seule règle : la
             * carte dé-cuite gagne quand elle existe, exactement comme `_n` et
             * `_orm`. Une texture sans carte dé-cuite — les écrans, les
             * affiches, les marquees, qui n'ont rien de cuit — continue de
             * passer par l'image du glTF sans qu'on ait à la déclarer.
             */
            SDL_snprintf(maps, sizeof maps, "materials/%s_c.png", name);
            /*
             * `ns_path_resolve` d'abord, et pas un chargement qui échoue.
             *
             * Une carte dé-cuite est FACULTATIVE — la moitié des textures n'en
             * ont pas et n'en veulent pas. Tenter le chargement pour voir
             * imprimait une ligne ERROR par texture sans carte, soit une
             * cinquantaine à chaque démarrage : un journal où l'on ne peut plus
             * repérer la vraie erreur ne sert plus à rien.
             */
            char probe[1024];
            triplets[i].albedo = ns_path_resolve(maps, probe, sizeof probe)
                                     ? load_one(r, out, &tex_cursor, maps, true)
                                     : -1;
            if (triplets[i].albedo < 0) {
                logical_sibling(gltf_logical, data->images[i].uri, logical, sizeof logical);
                triplets[i].albedo = load_one(r, out, &tex_cursor, logical, true);
            }

            /* Les cartes générées vivent dans materials/, pas à côté du glTF. */
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

    /*
     * L'échelle par défaut est celle du modèle de 2020 : le fichier de scène,
     * quand il existe, la remplace. Mettre 1,0 par défaut ferait marcher le
     * joueur à 82 cm de haut dans l'ancienne salle sans que rien ne le signale —
     * c'est exactement le défaut qu'on corrige.
     */
    out->units_per_metre = NS_LEGACY_UNITS_PER_METRE;

    /*
     * Les fichiers annexes portent le nom de base du glTF, et non « salle » en
     * dur : deux salles cohabitent dans le même répertoire (salle.gltf et
     * salle-legacy.gltf), et elles doivent lire *leurs* lumières.
     *
     * Pourquoi le même répertoire plutôt qu'un sous-dossier : l'URI des textures
     * dans un glTF est relative au fichier, donc un sous-dossier imposerait un
     * préfixe « ../textures/ » — que `ns_path_resolve` refuse, à raison, puisque
     * son rôle est justement d'interdire les chemins remontants. Partager le
     * répertoire évite à la fois la remontée et la duplication des 58 textures.
     */
    char base[256];
    basename_noext(gltf_logical, base, sizeof base);
    char annex[320];

    SDL_snprintf(annex, sizeof annex, "%s.lights.json", base);
    logical_sibling(gltf_logical, annex, sibling, sizeof sibling);
    load_lights(out, sibling);

    /*
     * Le fichier de scène passe AVANT les déductions, et non après.
     *
     * L'ordre inverse — deviner puis écraser — laisse chaque champ que la
     * déclaration ne mentionne pas garni d'une valeur devinée, sans qu'on puisse
     * dire lequel. C'est comme ça qu'une orientation d'écran calculée par
     * barycentre survit dans une salle qui déclare pourtant la sienne. Ici, quand
     * la salle se décrit, les heuristiques **ne s'exécutent pas** : il n'y a rien
     * à écraser, et rien à vérifier.
     *
     * L'ancienne salle, qui n'a que son OBJ, prend l'autre branche. Les deux
     * chemins vivent dans le même binaire, ce qui est ce qui rend la comparaison
     * possible.
     */
    SDL_snprintf(annex, sizeof annex, "%s.scene.json", base);
    logical_sibling(gltf_logical, annex, sibling, sizeof sibling);
    load_scene_sidecar(out, sibling);

    if (!out->has_declared_cabinets) {
        logical_sibling(gltf_logical, "cabinets.json", sibling, sizeof sibling);
        load_cabinet_assignment(out, sibling);
    }
    add_cabinet_screen_lights(out);
    if (!out->has_declared_pois) {
        detect_points_of_interest(out, gltf_logical);
    }

    /*
     * Emprise jouable : union des bornes et des lumières, élargie d'une marge
     * de circulation. C'est ce volume qui sert à placer la caméra et le joueur.
     *
     * Uniquement quand elle n'est pas déclarée. La déduction est une
     * approximation nécessaire sur l'ancien modèle — dont le décor lointain
     * étire la boîte englobante au-delà de cinquante mètres — mais la salle
     * reconstruite connaît sa propre emprise, et une valeur déclarée ne doit
     * jamais être écrasée par une valeur devinée.
     */
    if (!out->has_declared_room_bounds) {
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

    /*
     * BVH : facultatif. Sans lui, le rendu retombe sur l'espace écran et la
     * collision est désactivée — le jeu reste lançable, ce qui vaut mieux qu'un
     * refus de démarrer si l'étape de build a été sautée.
     *
     * Le nom est dérivé du nom de base, comme les autres annexes. Il était codé
     * en dur à « salle.nsbvh », si bien que `--room=legacy` chargeait le BVH de
     * l'*autre* salle. Invisible tant que les deux salles sortaient du même OBJ
     * et que leurs deux BVH étaient identiques à l'octet — et cassant net dès que
     * `roomgen` produit une vraie géométrie, puisque le lancer de rayons et la
     * collision auraient alors travaillé contre le mauvais maillage.
     */
    {
        char bvh_path[512];
        SDL_snprintf(annex, sizeof annex, "%s.nsbvh", base);
        logical_sibling(gltf_logical, annex, bvh_path, sizeof bvh_path);
        if (!ns_bvh_load(r, &out->bvh, bvh_path)) {
            NS_WARN("BVH absent (%s) : pas de lancer de rayons ni de collision "
                    "(lancer `cmake --build` pour le générer)", bvh_path);
        }
    }

    /*
     * Les tranches d'objets doivent partitionner exactement le tableau de lots :
     * contiguës, dans l'ordre, sans trou ni chevauchement. Tout le reste en
     * dépend — c'est ce qui permet de désigner la géométrie d'une borne par une
     * paire (premier lot, nombre de lots) au lieu d'un parcours. La propriété
     * tient parce que l'écrivain glTF conserve l'ordre des nœuds ; la vérifier
     * ici coûte un parcours au chargement et évite de découvrir la rupture
     * comme un écran de borne affiché sur le mur d'en face.
     */
    {
        uint32_t walked = 0;
        bool contiguous = true;
        for (uint32_t i = 0; i < out->object_count; ++i) {
            if (out->objects[i].first_batch != walked) { contiguous = false; break; }
            walked += out->objects[i].batch_count;
        }
        if (!contiguous || walked != out->batch_count) {
            NS_ERROR("tranches d'objets incohérentes : %u lots couverts sur %u",
                     walked, out->batch_count);
        }
    }

    NS_INFO("scène prête : %u sommets, %u indices, %u lots, %u objets, %u matériaux, %u lumières",
            out->vertex_count, out->index_count, out->batch_count,
            out->object_count, out->material_count, out->light_count);
    NS_INFO("emprise : (%.1f %.1f %.1f) à (%.1f %.1f %.1f)",
            (double)out->bounds.min.x, (double)out->bounds.min.y, (double)out->bounds.min.z,
            (double)out->bounds.max.x, (double)out->bounds.max.y, (double)out->bounds.max.z);
    return true;
}

void ns_scene_unload(ns_rhi *r, ns_scene *s)
{
    if (!s) return;
    ns_bvh_unload(r, &s->bvh);
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
/* Couleurs d'écran                                                           */
/* ========================================================================== */
/*
 * Chaque jeu a une dominante, reprise de ses propres textures d'origine. La
 * borne projette cette couleur devant elle : c'est ce qui fait qu'en marchant
 * dans l'allée, on passe du vert de Flappy au bleu de Tetris. La V1 avait ces
 * couleurs dans ses images ; ici elles éclairent réellement le sol.
 */
typedef struct game_tint { const char *game; float rgb[3]; } game_tint;

static const game_tint g_game_tints[] = {
    { "flappy",      { 0.32f, 1.00f, 0.52f } },   /* vert du tuyau */
    { "tetris",      { 0.35f, 0.58f, 1.00f } },   /* bleu de la grille */
    { "asteroid",    { 0.70f, 0.84f, 1.00f } },   /* blanc-bleu spatial */
    { "snake",       { 0.45f, 1.00f, 0.38f } },
    { "shooter",     { 1.00f, 0.46f, 0.24f } },   /* orange des explosions */
    { "demineur",    { 1.00f, 0.86f, 0.38f } },
    { "pacman",      { 1.00f, 0.90f, 0.22f } },
    { "piano",       { 0.88f, 0.42f, 1.00f } },
    { "leaderboard", { 0.40f, 0.92f, 1.00f } },
};

void ns_game_screen_color(const char *game, float out_rgb[3])
{
    out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.85f;   /* écran éteint : gris froid */
    if (!game) return;
    for (size_t i = 0; i < SDL_arraysize(g_game_tints); ++i) {
        if (SDL_strcasecmp(game, g_game_tints[i].game) == 0) {
            SDL_memcpy(out_rgb, g_game_tints[i].rgb, sizeof(float) * 3);
            return;
        }
    }
}

/*
 * Ajoute une source lumineuse devant l'écran de chaque borne.
 *
 * C'est le geste qui change le plus la salle. Les écrans du modèle d'origine
 * sont des textures claires : ils n'éclairaient rien. Une vraie borne d'arcade
 * dans une salle sombre projette une flaque de lumière colorée sur la moquette
 * et sur le joueur qui s'en approche — c'est l'image même d'une salle d'arcade.
 */
static void add_cabinet_screen_lights(ns_scene *s)
{
    uint32_t added = 0;
    for (uint32_t i = 0; i < s->cabinet_count && s->light_count < NS_MAX_LIGHTS; ++i) {
        const ns_cabinet *c = &s->cabinets[i];

        float tint[3];
        ns_game_screen_color(c->game, tint);

        ns_light_gpu *l = &s->lights[s->light_count];
        ns_light_anim *a = &s->light_anim[s->light_count];
        SDL_zerop(l);
        SDL_zerop(a);

        /*
         * **Dans le plan de la dalle, pas 45 cm devant.**
         *
         * Elle était à 45 cm devant l'écran, et c'est ce qui brûlait la face de
         * la borne elle-même : le bandeau de caisson juste au-dessus de la dalle
         * se retrouvait à 51 cm d'une source de 38 vue sous un angle de 28°,
         * soit 129 d'éclairement — mesuré sur l'image à (168, 223, 178) pour un
         * albédo de 0,46, à 30 cm d'une moquette à (19, 11, 9). Un facteur neuf
         * à l'intérieur d'un même objet : la borne se lisait comme un caisson
         * lumineux en plastique, pas comme une machine peinte dans une salle
         * tamisée.
         *
         * Ramenée DANS le plan de la dalle, la géométrie fait le travail toute
         * seule : la face du caisson est coplanaire avec l'écran, donc le
         * cosinus d'incidence y vaut zéro et elle ne reçoit rien de son propre
         * écran — ce qui est aussi ce que fait une vraie dalle, qui émet vers
         * l'avant et pas sur son propre cadre. Le joueur, lui, est à un mètre
         * DEVANT : il reçoit toujours sa flaque de couleur.
         *
         * Les 2 cm restants ne servent qu'à ce que la source soit du bon côté de
         * la surface, sans quoi un flottant près de zéro déciderait du signe.
         */
        const ns_v3 p = ns_v3_add(c->screen_center, ns_v3_scale(c->screen_normal, 0.02f));
        l->position[0] = p.x; l->position[1] = p.y; l->position[2] = p.z;
        /*
         * Désaturation vers le blanc.
         *
         * La teinte pure repeignait tout : huit bornes du même jeu alignées le
         * long d'une allée rendaient le mur, la moquette et le plafond d'un vert
         * uniforme, et on ne lisait plus ni la texture ni la forme. Une lampe
         * colorée réelle porte surtout de la lumière blanche avec une dominante.
         * À 45 % de teinte l'identité de chaque borne reste lisible sur le
         * mètre qu'elle éclaire, sans avaler la salle.
         */
        const float TINT = 0.45f;
        for (int k = 0; k < 3; ++k) {
            l->color[k] = tint[k] * TINT + (1.0f - TINT);
        }
        /*
         * 90 et non 240, 4,5 m et non 6,2.
         *
         * Ces valeurs avaient été réglées sur la salle de 2020, où quinze bornes
         * se serraient dans un décor à 2,06 unités par mètre. La salle
         * reconstruite en aligne dix-neuf, en mètres, contre des murs à trois
         * mètres : à 240 elles noyaient l'allée entière dans la teinte du jeu le
         * plus proche — un couloir vert vif d'un bout à l'autre. Un écran de
         * borne éclaire son joueur et un mètre de moquette, pas une salle.
         */
        /* Remontée depuis 90 : la salle a été rééclairée en A5b, et les écrans
         * sont devenus la source PRINCIPALE plutôt qu'un appoint. Vingt
         * plafonniers de 500 les écrasaient ; il n'en reste que quatre, faibles.
         * C'est ce qui donne l'ambiance d'une vraie salle d'arcade, où ce sont
         * les machines qui éclairent. */
        /*
         * 150 pour 4,2 m de portée : c'était deux fois et demie le plafonnier de
         * l'allée (62), multiplié par dix-neuf bornes. Les caissons ressortaient
         * blancs à un mètre, la salle entière était éclairée par ses écrans, et
         * l'ambiance tamisée demandée n'avait aucune chance d'exister.
         *
         * Une dalle d'arcade est une source faible. À 38 pour 2,6 m elle fait une
         * flaque de couleur sur la moquette et sur les mains du joueur, et rien
         * au-delà de l'allée — ce qui est exactement le rôle qu'on lui veut.
         */
        /*
         * 30 pour 0,40 m de demi-rayon, et le couple est calculé, pas cherché.
         *
         * L'objectif est de ne RIEN changer à ce que le joueur reçoit, et de ne
         * retirer que l'auto-éclairage du caisson. Le point de contrôle est le
         * panneau de commande, à 29 cm de la dalle : avec l'ancienne source
         * (38, 45 cm devant, demi-rayon 0,20) il recevait 38 x 4,5 x 0,90 = 154.
         * Avec la nouvelle (dans le plan, demi-rayon 0,40) la décroissance y est
         * bornée à 1/0,16, donc 30 x 6,25 x 0,82 = 154. Le même chiffre.
         *
         * Le demi-rayon n'est pas non plus un réglage libre : la dalle fait
         * 0,62 x 0,35, soit 0,36 m de demi-diagonale. Une source de cette taille
         * cesse de décroître en 1/d² dès qu'on l'approche à moins que sa propre
         * dimension — c'est ce que 0,40 exprime, et c'est ce qui empêche un
         * panneau de commande à 29 cm de partir en blanc.
         *
         * Ce qui change, et c'est tout ce qui devait changer : le bandeau de
         * caisson au-dessus de la dalle passe de 129 à zéro.
         */
        l->intensity = 30.0f;
        /*
         * La PORTÉE, 2,8 -> 3,4, et l'intensité ne bouge pas.
         *
         * L'intention déclarée de la salle est que « l'essentiel de la lumière
         * vient des DIX-NEUF ÉCRANS ». Mesurée, elle était vraie à 96 % sur un
         * plan à 90 cm devant une dalle — et fausse partout ailleurs : nulle au
         * point médian du sol, et au-dessus de 20 % sur seulement 70 m² des 250
         * du hall. Les écrans éclairaient là où l'on JOUE, pas là où l'on
         * MARCHE, et c'est entre les deux que la salle paraissait morte.
         *
         * 3,4 m est le rayon qui fait se recouvrir les halos de deux bornes
         * voisines : la couverture passe de 70,2 à 88,7 m². L'intensité reste
         * à 30 précisément pour que la borne devant laquelle on se tient ne
         * change pas — c'est la portée qui manquait, pas la puissance.
         */
        l->range = 3.4f;
        l->type = NS_LIGHT_POINT;
        l->shadow_index = -1;
        /* Un écran de borne fait ~40 cm de diagonale : c'est une source étendue,
         * pas une ampoule, et son demi-rayon borne la décroissance de sorte que
         * le joueur qui s'en approche soit éclairé sans être brûlé. */
        l->source_radius = 0.40f;

        a->base_intensity = l->intensity;
        /* Pulsation lente et désynchronisée : un écran de jeu n'a pas une
         * luminosité constante, et voir quinze bornes battre à l'unisson
         * détruirait immédiatement l'illusion. */
        a->flicker = true;
        a->screen = true;
        a->phase = (float)i * 1.7f + 0.3f;

        s->light_count++;
        added++;
    }
    if (added) NS_INFO("%u lumières d'écran de borne ajoutées", added);
}

/* ========================================================================== */
/* Points d'intérêt                                                           */
/* ========================================================================== */

static const char *const g_poi_labels[NS_POI_KIND_COUNT] = {
    "", "Billard", "Canapé", "Bar", "Radio", "Toilettes", "Sortie", "Classement"
};

const char *ns_poi_label(ns_poi_kind kind)
{
    if (kind <= NS_POI_NONE || kind >= NS_POI_KIND_COUNT) return "";
    return g_poi_labels[kind];
}

/*
 * Repère les lieux du décor par le nom des objets du glTF.
 *
 * Le modèle contient un billard, un canapé, un bar d'accueil, des radios, des
 * toilettes et une porte de sortie — tous modélisés, tous inertes dans la V1.
 * Ce sont autant d'endroits où accrocher une interaction, une source sonore
 * positionnelle et une zone de réverbération propre.
 */
static void detect_points_of_interest(ns_scene *s, const char *gltf_logical)
{
    ns_arena_mark mark = ns_arena_save(&s->arena);

    char path[1024];
    if (!ns_path_resolve(gltf_logical, path, sizeof path)) {
        ns_arena_restore(&s->arena, mark);
        return;
    }

    cgltf_options opt;
    SDL_zero(opt);
    cgltf_data *data = NULL;
    if (cgltf_parse_file(&opt, path, &data) != cgltf_result_success) {
        ns_arena_restore(&s->arena, mark);
        return;
    }

    for (cgltf_size n = 0; n < data->nodes_count && s->poi_count < NS_MAX_POI; ++n) {
        const cgltf_node *node = &data->nodes[n];
        if (!node->name || !node->mesh) continue;

        const ns_poi_kind kind = ns_poi_kind_in_name(node->name);
        if (kind == NS_POI_NONE) continue;

        /* Éviter les doublons : le décor contient plusieurs radios et plusieurs
         * éléments de toilettes ; on ne garde pas dix entrées pour le même lieu
         * si elles se touchent. */
        cgltf_float world[16];
        cgltf_node_transform_world(node, world);
        const ns_v3 pos = ns_v3_make(world[12], world[13], world[14]);

        bool duplicate = false;
        for (uint32_t i = 0; i < s->poi_count; ++i) {
            if (s->pois[i].kind == kind && ns_v3_dist(s->pois[i].anchor, pos) < 3.0f) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        ns_poi *p = &s->pois[s->poi_count++];
        SDL_zerop(p);
        SDL_strlcpy(p->name, node->name, sizeof p->name);
        p->kind = kind;
        p->anchor = pos;
    }
    cgltf_free(data);

    if (s->poi_count) {
        NS_INFO("%u points d'intérêt repérés dans le décor :", s->poi_count);
        for (uint32_t i = 0; i < s->poi_count; ++i) {
            NS_INFO("    %-12s %s", ns_poi_label(s->pois[i].kind), s->pois[i].name);
        }
    }
    ns_arena_restore(&s->arena, mark);
}

const ns_poi *ns_scene_nearest_poi(const ns_scene *s, ns_v3 position, float max_distance)
{
    const ns_poi *best = NULL;
    float best_dist = max_distance;
    for (uint32_t i = 0; i < s->poi_count; ++i) {
        const float d = ns_v3_dist(position, s->pois[i].anchor);
        if (d < best_dist) { best_dist = d; best = &s->pois[i]; }
    }
    return best;
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

        /* Deux comportements distincts.
         *
         * Tube fluorescent : une oscillation lente pour la respiration, une
         * rapide pour le grésillement, et un creux occasionnel. Une simple
         * sinusoïde donnerait un clignotement de guirlande de Noël.
         *
         * Écran de borne : une pulsation lente et douce, comme une image de jeu
         * dont la luminosité moyenne varie. Quand les mini-jeux tourneront
         * réellement dans les écrans, cette approximation sera remplacée par la
         * luminance mesurée de l'image rendue. */
        float modulation;
        if (a->screen) {
            modulation = sinf(t * 2.3f + a->phase) * 0.10f
                       + sinf(t * 0.7f + a->phase * 2.0f) * 0.06f;
        } else {
            const float slow = sinf(t * 1.7f + a->phase) * 0.04f;
            const float fast = sinf(t * 37.0f + a->phase * 3.1f) * 0.02f;
            const float dip  = (sinf(t * 0.53f + a->phase) > 0.985f) ? -0.35f : 0.0f;
            modulation = slow + fast + dip;
        }

        s->lights[i].intensity = a->base_intensity * (1.0f + modulation);
        if (s->lights[i].intensity < 0.0f) s->lights[i].intensity = 0.0f;
    }
}

const ns_scene_object *ns_scene_find_object(const ns_scene *s, const char *name)
{
    if (!s || !name || !*name) return NULL;
    for (uint32_t i = 0; i < s->object_count; ++i) {
        if (SDL_strcmp(s->objects[i].name, name) == 0) return &s->objects[i];
    }
    return NULL;
}

const ns_viewpoint *ns_scene_find_viewpoint(const ns_scene *s, const char *name)
{
    if (!s || !name || !*name) return NULL;
    for (uint32_t i = 0; i < s->viewpoint_count; ++i) {
        if (SDL_strcasecmp(s->viewpoints[i].name, name) == 0) return &s->viewpoints[i];
    }
    return NULL;
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
