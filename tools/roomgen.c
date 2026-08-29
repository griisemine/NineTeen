/*
 * roomgen.c — la salle, générée depuis sa description.
 *
 * Lit `assets/scene/salle.room.json` et écrit trois fichiers :
 *
 *   salle.gltf + salle.bin   la géométrie, via `gltf_write`
 *   salle.lights.json        les lumières et l'emprise, au format que
 *                            `load_lights` lit déjà (ns_scene.c:83)
 *   salle.scene.json         l'échelle, l'emprise jouable, le départ du joueur
 *                            et les points de vue nommés, au format que
 *                            `load_scene_sidecar` lit déjà (ns_scene.c:174)
 *
 * **Pourquoi un générateur plutôt qu'un modèle.** La salle de 2020 est un OBJ de
 * 127 objets dont la moitié s'appelle `Cube.0XX`. Le moteur devait donc *deviner*
 * quelle borne joue à quoi, où est son écran, quelle boîte est une lampe — et
 * chacune de ces déductions se trompe quelque part. Ici tout est déclaré : le
 * nom, la place, le matériau, la lumière. La classe entière de bugs disparaît
 * avec les heuristiques, au lieu d'être corrigée une par une.
 *
 * **L'unité est le mètre**, et c'est la seconde raison. Le modèle d'origine est
 * en unités Blender à 2,06 par mètre — l'auteur l'a écrit lui-même
 * (`HAUTEUR_CAMERA_DEBOUT 3.5F`, legacy/room/room.c:90) — et le moteur V15 l'a
 * lu comme des mètres, ce qui donnait un joueur de 82 cm. Le problème cesse
 * d'exister par construction.
 *
 * **Ce qui casse le build plutôt que de passer.** Un matériau inconnu, une
 * texture absente, un budget dépassé, une baie incohérente : tout est fatal, et
 * nommé. Un outil de build qui tronque en silence produit un asset dont le
 * défaut se découvre à l'exécution, deux étapes plus loin, dans un message qui
 * parle d'octets.
 */
#include "geo_import.h"
#include "geo_shapes.h"
#include "gltf_write.h"
#include "reach_grid.h"
#include "tool_json.h"

#include <math.h>

/* Budgets. Ils viennent des constantes du moteur (engine/scene/ns_scene.h) ou
 * d'une mesure : l'ancienne salle fait 102 231 triangles, et un plafond de dalles
 * plus des chanfreins partout montent vite. Dépassement = arrêt. */
/*
 * 64 était le budget d'origine, et il était plein au dernier octet. Le porter à
 * 128 n'est pas un renoncement : c'est ce qui permet de donner à chaque famille
 * de bornes sa propre teinte de caisson, et une salle d'arcade est justement un
 * endroit où rien n'a la même couleur que son voisin. Le moteur, lui, n'a aucune
 * limite fixe — il alloue les matériaux dans son arène — donc le seul coût est
 * ce tableau et un changement de matériau de plus par lot, ce qui ne se mesure
 * pas face à 600 tirages.
 */
#define RG_MAX_MATERIALS   192   /* releve de 128 : huit modeles CC0 de plus, et les dix-neuf clones de dalle poussaient a 131 */
#define RG_MAX_TEXTURE_DIRS 4
#define RG_MAX_TEXTURES    128   /* relevé de 64 : les douze albédos des modèles CC0 ont fait déborder */
#define RG_MAX_LIGHTS     128        /* NS_MAX_LIGHTS */
#define RG_MAX_OBJECTS   1024        /* NS_MAX_OBJECTS */
#define RG_MAX_CABINETS    24        /* NS_MAX_CABINETS */
#define RG_MAX_POIS        32        /* NS_MAX_POI */
#define RG_MAX_SOLIDS     512        /* obstacles suivis pour le contrôle des points de vue */
/* Remonté ici depuis la section des murs : la coquille est gardée dans
 * `rg_builder`, qui est déclaré avant elle. */
#define RG_MAX_WALL_POINTS 64
#define RG_MAX_TRIANGLES 250000

/* Angles de lissage. Une arête sous ce seuil est adoucie, au-dessus elle reste
 * vive. Un chanfrein doit rester net — sans quoi il cesse de se lire comme un
 * chanfrein, et l'objet redevient une boîte molle. */
#define RG_SMOOTH_HARD    1.0f       /* boîtes, panneaux, murs */
#define RG_SMOOTH_PROFILE 35.0f      /* moulures : le galbe s'adoucit, l'arête non */

/* ========================================================================== */
/* État de construction                                                       */
/* ========================================================================== */

typedef struct rg_light {
    char  name[64];
    float position[3];
    float color[3];
    float intensity, range, source_radius;
    bool  flicker;
    bool  ceiling_panel;
    float panel_size[2];    /* emprise du luminaire dans la trame, en mètres */
    /* Assume d'être posée DANS un solide. Voir `check_lights_not_enclosed` : le
     * cas normal est une erreur, celui-ci doit s'écrire. */
    bool  inside_ok;
} rg_light;

/*
 * Une borne, telle qu'elle sera **déclarée** au moteur.
 *
 * Aujourd'hui le moteur devine tout cela : l'écran par des fractions inventées de
 * la boîte englobante (centre 31 cm trop bas), l'orientation par le barycentre du
 * troupeau de bornes, l'affectation des jeux par un tri en X puis Z qui ne colle
 * pas aux images peintes sur les marquees. Trois déductions, trois erreurs.
 */
typedef struct rg_cabinet {
    char  name[64];
    char  game[32];
    char  difficulty[16];
    int   slot;
    float bounds_min[3], bounds_max[3];
    float screen_center[3];
    float screen_normal[3];
    float screen_size[2];
    float screen_tilt;          /* radians ; la dalle bascule vers l'arrière */       /* largeur et hauteur utiles de la dalle */
    float player_anchor[3];
    int   screen_material;      /* index du matériau de la dalle */
    float panel_centre[3];      /* là où la main appuie */
    float coin_slot[3];         /* là où le jeton entre */
    float stick_top[3];         /* là où la main gauche empoigne */
    bool  attract;
} rg_cabinet;

/*
 * Les points remarquables d'une borne, en repère **local** — c'est-à-dire dans
 * le même repère que les boîtes de `build_cabinet`, avant placement.
 *
 * Pourquoi les faire sortir d'ici plutôt que de les redériver côté `room/` :
 * ces cotes sont écrites une fois, dans le générateur, à trois lignes des
 * boîtes qu'elles désignent. Les recalculer ailleurs à partir de la boîte
 * englobante en ferait une seconde copie — et une seconde copie d'un chiffre
 * est une copie qui dérive. C'est exactement l'erreur qu'on vient de retirer
 * du moteur, où le centre d'écran était deviné à 31 cm près.
 */
typedef struct rg_cab_anchors {
    int   screen_material;   /* index du matériau de la dalle */
    float screen[3];
    float screen_size[2];
    float panel[3];     /* centre de la grappe de boutons, sur la face du dessus */
    float coin[3];      /* fente à jetons, sur la face avant de la trappe */
    float stick[3];     /* le dessus de la boule du joystick */
    /*
     * L'inclinaison de la dalle, en radians, comptée depuis la verticale.
     *
     * Elle est ici parce que la scène l'exportait FAUSSE : `screenNormal`
     * valait l'horizontale exacte alors que la dalle est penchée de 10° dans
     * la géométrie depuis A4. Une donnée qui ment sur sa propre géométrie est
     * pire qu'une donnée absente — le placement des bras et le reflet de
     * l'écran s'appuient dessus sans pouvoir s'en apercevoir.
     */
    float screen_tilt;
} rg_cab_anchors;

typedef struct rg_poi {
    char    name[64];
    char    kind[24];
    ns_aabb bounds;
    float   anchor[3];
} rg_poi;

/* Un obstacle : le nom sert au message d'erreur, pas au format de sortie.
 *
 * Le genre décide de la sévérité. Deux bornes qui se croisent sont une faute —
 * on ne peut pas jouer sur une borne encastrée dans sa voisine. Deux props qui
 * se croisent sont souvent voulus : un tabouret glissé sous un comptoir, une
 * affiche plaquée contre un mur, une flaque posée sur le sol. En faire une
 * erreur obligerait à déclarer des exceptions partout, et une exception qu'on
 * écrit dix fois cesse d'être lue. */
typedef enum rg_solid_kind {
    RG_SOLID_BOX = 0,
    RG_SOLID_CABINET,
    RG_SOLID_PROP
} rg_solid_kind;

/* Quatre noms d'assemblage par objet. Une poutre repose sur deux familles de
 * piliers ; au-delà de quatre, ce n'est plus un assemblage, c'est un objet mal
 * placé qui déclare tout ce qu'il touche. */
#define RG_MAX_TRAVERSE 4

typedef struct rg_solid {
    char          name[64];
    ns_aabb       bounds;
    rg_solid_kind kind;

    /*
     * De quoi retrouver la GÉOMÉTRIE de l'objet, et pas seulement sa boîte.
     *
     * `emit_object` a déjà tout écrit dans les deux réservoirs partagés du
     * constructeur — les sommets dans `verts`, les indices dans `prim_blocks` —
     * et n'en gardait aucune trace par objet. On garde ici les bornes, ce qui
     * ne coûte rien et rend deux contrôles possibles : `check_inside_shell`
     * peut nommer LE SOMMET fautif, et `check_solid_overlaps` peut confirmer un
     * recouvrement AU TRIANGLE.
     *
     * La différence est mesurée, pas supposée : sur les huit paires que l'audit
     * de placement a retestées au triangle, DEUX ne se pénétraient pas du tout
     * (canapé/fauteuil, 0,3202 m³ de boîtes communes ; canapé/table basse,
     * 0,0892 m³). Un contrôle qui crie faux une fois sur quatre finit lu en
     * diagonale.
     */
    size_t vert_begin, vert_end;   /* intervalle dans `b->verts` */
    size_t prim_block;             /* index dans `b->prim_blocks` */
    size_t tri_count;

    /* Assume d'être posé HORS du bâtiment. Voir `check_inside_shell`. */
    bool outside_ok;

    /*
     * Les assemblages VOULUS, déclarés par le nom de l'autre pièce :
     *
     *     { "name": "poutre", ..., "traverse": ["pilier_ouest", "pilier_est"] }
     *
     * C'est toute la différence avec la tolérance d'avant, qui était une
     * CATÉGORIE (« tout sauf les bornes ») : une nouvelle intersection ne peut
     * plus se glisser dans une catégorie préexistante, il faut l'écrire.
     */
    char   traverse[RG_MAX_TRAVERSE][64];
    size_t traverse_count;

    /*
     * Sur quoi l'objet repose, déclaré. Vide = non déclaré, et le contrôle de
     * pose ne dit alors rien de cet objet — voir `check_grounded`.
     */
    char pose[16];       /* "sol", "meuble", "mur", "suspendu", "libre" */
    char pose_sur[64];   /* le meuble nommé, quand `pose` vaut "meuble" */
} rg_solid;

/* Un sol déclaré, gardé pour savoir sur quoi un objet est censé poser. Les
 * sols ne sont pas des solides — on marche dessus, ils ne bouchent rien — mais
 * ils sont le support par défaut de tout ce qui est posé. */
#define RG_MAX_FLOORS 16
typedef struct rg_floor {
    char  name[64];
    float centre[2], size[2], y;
} rg_floor;

/*
 * Un mur, en PLAN — sa ligne médiane et son épaisseur.
 *
 * Gardé parce que trois contrôles ont besoin de savoir où sont les parements,
 * et qu'aucun ne peut le redéduire : `geo_wall_run` a déjà transformé le plan
 * en triangles, et une boîte englobante de mur ne dit pas de quel côté est la
 * salle. C'est la même raison qui fait garder les ancres de borne dans
 * `rg_cab_anchors` : la donnée existe une fois, à l'endroit où elle est écrite.
 */
#define RG_MAX_WALLS 8
/* Remonté ici pour la même raison que `RG_MAX_WALL_POINTS` : les baies font
 * désormais partie du plan gardé, et le plan est déclaré avant les murs. */
#define RG_MAX_WALL_OPENINGS 16
typedef struct rg_wall_plan {
    char   name[64];
    ns_v2  points[RG_MAX_WALL_POINTS];
    size_t count;
    bool   closed;
    float  thickness;
    float  height;

    /*
     * LES BAIES, gardées avec le plan.
     *
     * Elles ne servaient à aucun contrôle, et c'est exactement ce qui a laissé
     * partir un joueur enfermé : un mur sans ses baies est un mur plein, et un
     * contrôle qui ne connaît que les pleins ne peut pas dire par où l'on passe.
     * Le format est celui que `geo_wall_run` consomme — même structure, mêmes
     * abscisses cumulées le long de la polyligne — parce que deux lectures d'une
     * même donnée finissent par diverger d'un décalage.
     */
    geo_opening openings[RG_MAX_WALL_OPENINGS];
    size_t      opening_count;
} rg_wall_plan;

#define RG_MAX_SHELLS 8

typedef struct rg_builder {
    tool_vec verts;         /* gltf_vertex — un seul pool, partagé (cf. gltf_write.h) */
    tool_vec meshes;        /* gltf_mesh */
    tool_vec prim_blocks;   /* geo_primitives, gardés vivants jusqu'à l'écriture */

    gltf_material materials[RG_MAX_MATERIALS];
    char          material_names[RG_MAX_MATERIALS][64];
    float         material_uv[RG_MAX_MATERIALS];
    bool          material_fit[RG_MAX_MATERIALS];
    /* Classe de pas, déclarée par le matériau. Vide = on ne marche pas dessus. */
    char          material_footstep[RG_MAX_MATERIALS][24];
    size_t        material_count;

    /* Racine des assets, pour résoudre `"model": "cc0/models/..."`. Elle est
     * donnée en ligne de commande plutôt que déduite du chemin du fichier de
     * salle : le fichier de salle est une SOURCE de build, et rien ne garantit
     * qu'il soit rangé au même endroit que les assets qu'il désigne. */
    char asset_root[512];
    size_t imported_triangles;

    const char *textures[RG_MAX_TEXTURES];
    char        texture_storage[RG_MAX_TEXTURES][128];
    size_t      texture_count;

    rg_light lights[RG_MAX_LIGHTS];
    size_t   light_count;

    rg_cabinet cabinets[RG_MAX_CABINETS];
    size_t     cabinet_count;

    rg_poi pois[RG_MAX_POIS];
    size_t poi_count;

    /*
     * Emprise des objets SOLIDES — bornes, caisses, mobilier. Sert à refuser un
     * point de vue posé dedans.
     *
     * Ce n'est pas un raffinement : le point de vue `allee` s'est retrouvé dans
     * une borne en A4 et rendait un cadre noir, puis `plafond` a été avalé par la
     * borne de classement du même palier — deux fois le même défaut, découvert
     * deux fois sur une capture. Les points de vue sont de la donnée qui se périme
     * quand la salle change ; l'outil qui connaît les deux doit le dire.
     *
     * Seuls les solides comptent. La coquille est un objet unique dont la boîte
     * englobante couvre toute la salle : l'y inclure déclarerait chaque caméra
     * « dans un mur ».
     */
    rg_solid solids[RG_MAX_SOLIDS];
    size_t   solid_count;

    /*
     * LES MURS EN PLAN, et parmi eux LA COQUILLE.
     *
     * La coquille est le seul contour FERMÉ de la description — les deux
     * cloisons sont des polylignes ouvertes — donc l'enveloppe du bâtiment. Le
     * parement intérieur s'en déduit en rentrant de `thickness / 2` ; on garde
     * la médiane plutôt que le polygone rentré parce que rentrer un polygone
     * concave demande de traiter les angles rentrants, alors que la distance
     * signée à la médiane, elle, est exacte partout.
     *
     * Quatre objets se sont retrouvés SUR LE TROTTOIR faute de ce contrôle —
     * le comptoir d'accueil, une poubelle, une applique et son ampoule — et
     * deux sources de lumière éclairaient la rue. Rien ne le disait : un objet
     * dehors ne produit ni erreur, ni avertissement ; il manque simplement là
     * où il devait être.
     */
    rg_wall_plan wall_plans[RG_MAX_WALLS];
    size_t       wall_plan_count;
    int          shell_index;    /* -1 tant qu'aucun contour fermé n'a été lu */
    /* Les contours fermés, tous : un objet est DANS le bâtiment s'il est dans
     * l'un d'eux. Le hall, le bloc sanitaire et le sas en sont trois. */
    int          shells[RG_MAX_SHELLS];
    size_t       shell_count;

    rg_floor floors[RG_MAX_FLOORS];
    size_t   floor_count;

    /*
     * La géométrie des LUMINAIRES, gardée à part pour un seul contrôle : une
     * lumière posée à l'intérieur d'un solide fermé s'éteint elle-même.
     *
     * Ce n'est pas une hypothèse. Le lancer de rayons ne connaît pas les
     * matériaux émissifs : un rayon d'ombre partant d'une surface vers la
     * source doit traverser le verre de l'ampoule ou les barreaux de la cage
     * qui l'entourent, et la lumière est intégralement occultée. Le défaut a
     * coûté DEUX fois dans ce projet — la suspension du billard est restée
     * ainsi pendant tout le développement, et l'applique du salon a refait la
     * même chose trois heures après sa correction.
     *
     * Le symptôme est caractéristique et trompeur : monter l'intensité ne
     * change RIEN. On croit alors à une lampe faible, et on cherche du côté de
     * l'éclairement.
     */
    geo_mesh props_mesh;

    size_t triangle_count;
    ns_aabb bounds;
    /* Emprise du dernier objet émis. Sert aux props qui déclarent un point
     * d'intérêt : son volume est celui de sa géométrie, pas une boîte réécrite à
     * la main dans la description — c'est précisément le genre de doublon qui
     * finit par mentir. */
    ns_aabb last_bounds;
    /* Et où sa géométrie est rangée, pour que `record_solid` puisse la
     * retrouver. Même raison que `last_bounds` : l'émission sait, l'appelant
     * ne saurait pas le recalculer sans recopier la moitié d'`emit_object`. */
    size_t  last_vert_begin, last_vert_end;
    size_t  last_prim_block, last_tri_count;

    /*
     * Les répertoires où chercher une texture nommée. Il y en a deux : les 58
     * images de 2020, et les albédos CC0 rapportés là où le modèle d'origine
     * n'avait qu'un aplat de couleur. `--textures=` est donc répétable plutôt
     * que d'exiger de tout entasser dans un seul dossier — mélanger l'art
     * d'origine et l'art rapporté rendrait impossible de dire, plus tard, d'où
     * vient quoi.
     */
    const char *texture_dirs[RG_MAX_TEXTURE_DIRS];
    size_t      texture_dir_count;
    int expect_textures;        /* < 0 : pas de contrôle de couverture */
    size_t      retired_count;  /* images déclarées volontairement inemployées */
} rg_builder;

/*
 * Enregistre le dernier objet émis comme obstacle. Appelé explicitement par les
 * sections qui produisent du volume plein, jamais par les autres.
 *
 * `e` est l'entrée JSON dont l'objet sort : c'est là que se lisent les
 * échappatoires déclaratives (`outsideOk`, `traverse`) et la pose. Les faire
 * transiter par ici plutôt que par trois lectures recopiées dans `parse_boxes`,
 * `parse_cabinets` et `parse_props` garde une seule définition de chaque clé —
 * trois copies d'une même lecture finissent par diverger d'un défaut.
 */
static void record_solid(rg_builder *b, const char *name, rg_solid_kind kind,
                         const tool_json *doc, const tool_json_value *e)
{
    if (b->solid_count >= RG_MAX_SOLIDS) return;   /* le contrôle n'est pas critique */
    rg_solid *s = &b->solids[b->solid_count++];
    memset(s, 0, sizeof *s);
    /* Tronqué sciemment : ce nom ne sert qu'aux messages, pas à un appariement. */
    snprintf(s->name, sizeof s->name, "%.63s", name);
    s->bounds = b->last_bounds;
    s->kind = kind;

    s->vert_begin = b->last_vert_begin;
    s->vert_end   = b->last_vert_end;
    s->prim_block = b->last_prim_block;
    s->tri_count  = b->last_tri_count;

    if (!e) return;
    s->outside_ok = tool_json_get_bool(doc, e, "outsideOk", false);

    const tool_json_value *tr = tool_json_get(doc, e, "traverse");
    const int tr_count = tool_json_array_count(doc, tr);
    if (tr_count > RG_MAX_TRAVERSE) {
        tool_fatalf("« %s » déclare %d assemblages « traverse », %d au maximum.\n"
                    "  Un objet qui doit déclarer plus de quatre pièces traversées "
                    "n'est pas assemblé : il est mal placé.",
                    name, tr_count, RG_MAX_TRAVERSE);
    }
    for (int i = 0; i < tr_count; ++i) {
        tool_json_string_at(doc, tr, i, s->traverse[s->traverse_count],
                            sizeof s->traverse[0]);
        if (s->traverse[s->traverse_count][0]) s->traverse_count++;
    }

    tool_json_get_string(doc, e, "pose", s->pose, sizeof s->pose);
    tool_json_get_string(doc, e, "poseSur", s->pose_sur, sizeof s->pose_sur);
}

/*
 * Deux meubles ne doivent pas occuper le même volume, et c'est à l'outil de le
 * dire.
 *
 * Ce contrôle existe parce que le défaut s'est produit : en redimensionnant la
 * salle j'ai multiplié les POSITIONS par un facteur sans toucher aux TAILLES —
 * correct pour un meuble, faux pour tout ce qui dépend d'un écart. L'entraxe des
 * bornes est passé de 0,80 m à 0,69 m pour un caisson de 0,72 m : les douze
 * bornes de l'îlot se sont encastrées les unes dans les autres, et personne ne
 * l'a vu avant que le joueur ne le signale.
 *
 * La tolérance vaut 5 mm : deux meubles bord à bord sont voulus, deux meubles à
 * un centimètre l'un dans l'autre sont une faute de frappe.
 */
#define RG_OVERLAP_TOLERANCE 0.005f

static float overlap_1d(float amin, float amax, float bmin, float bmax)
{
    const float lo = amin > bmin ? amin : bmin;
    const float hi = amax < bmax ? amax : bmax;
    return hi - lo;
}

/*
 * Un luminaire doit se trouver dans le lieu dont il porte le nom.
 *
 * Ce contrôle vient d'une bévue réelle, et de deux occurrences plutôt qu'une :
 * en réécrivant la disposition j'ai déplacé les lieux sans renommer les
 * lumières. `plafonnier_toilettes` s'est retrouvé à **17 m** du bloc sanitaire,
 * dans le sas d'entrée ; `plafonnier_bar` à **12,6 m** du comptoir. Les
 * toilettes n'avaient donc plus aucun éclairage, et le seul néon qui grésille de
 * la salle portait le nom d'une pièce où il n'était pas.
 *
 * Rien ne l'aurait signalé : une lumière mal placée éclaire quelque chose, donc
 * l'image reste plausible. C'est le pire cas de figure — un défaut qui produit
 * un résultat, et qu'on ne trouve qu'en mesurant.
 *
 * La règle est volontairement grossière : un nom se terminant par `_<lieu>` doit
 * être à moins de `RG_FIXTURE_RADIUS` de l'ancre de ce lieu. Elle n'attrape pas
 * un décalage d'un mètre, et ce n'est pas le but : elle attrape le nom qui ment.
 */
#define RG_FIXTURE_RADIUS 6.0f

static void check_fixture_naming(const rg_builder *b)
{
    size_t checked = 0;
    for (size_t i = 0; i < b->light_count; ++i) {
        const rg_light *l = &b->lights[i];

        /* Une déclaration répétée numérote ses exemplaires : `plafonnier_bar`
         * devient `plafonnier_bar_1`, `_2`… Sans retirer ce suffixe le contrôle
         * ne voyait qu'un luminaire sur quatorze — et laissait justement passer
         * les deux qu'il devait attraper. */
        char stem[64];
        snprintf(stem, sizeof stem, "%s", l->name);
        char *last = strrchr(stem, '_');
        if (last) {
            bool all_digits = last[1] != '\0';
            for (const char *c = last + 1; *c; ++c) {
                if (*c < '0' || *c > '9') { all_digits = false; break; }
            }
            if (all_digits) *last = '\0';
        }

        const char *tail = strrchr(stem, '_');
        if (!tail) continue;
        tail++;

        for (size_t j = 0; j < b->poi_count; ++j) {
            const rg_poi *poi = &b->pois[j];
            if (strcmp(tail, poi->kind) != 0) continue;

            const float dx = l->position[0] - poi->anchor[0];
            const float dz = l->position[2] - poi->anchor[2];
            const float d = sqrtf(dx * dx + dz * dz);
            checked++;
            if (d > RG_FIXTURE_RADIUS) {
                tool_fatalf("« %s » est à %.1f m de « %s » (%s), qu'il prétend éclairer.\n"
                            "  luminaire  (%.2f, %.2f, %.2f)\n"
                            "  lieu       (%.2f, %.2f, %.2f)\n"
                            "  Soit le luminaire est mal placé, soit il porte le nom d'un "
                            "autre lieu. Les deux se corrigent ici ; aucun ne se voit sur "
                            "une capture.",
                            l->name, (double)d, poi->name, poi->kind,
                            (double)l->position[0], (double)l->position[1],
                            (double)l->position[2],
                            (double)poi->anchor[0], (double)poi->anchor[1],
                            (double)poi->anchor[2]);
            }
        }
    }
    if (checked) printf("  %zu luminaire(s) confronté(s) au lieu qu'ils nomment\n", checked);
}

/*
 * Une lumière est-elle ENFERMÉE dans un solide ?
 *
 * Test de parité : on tire un rayon depuis la source et on compte les triangles
 * qu'il traverse. Un nombre IMPAIR de croisements veut dire qu'on est à
 * l'intérieur d'un volume fermé — c'est le théorème de Jordan, et il ne demande
 * ni normales cohérentes ni maillage convexe.
 *
 * SEPT directions, et la majorité l'emporte. Un maillage de décor n'est pas
 * toujours étanche : une cage a des ouvertures, un abat-jour est ouvert par le
 * bas, et un rayon unique qui sortirait par un trou dirait « dehors » à tort.
 * Sept rayons non alignés sur les axes rendent ce hasard-là très improbable
 * dans les deux sens.
 *
 * Ce contrôle existe parce que le défaut a coûté DEUX fois dans cette seule
 * séance, et qu'il est indétectable à la lecture : la lumière est déclarée, sa
 * couleur est juste, son intensité est juste, et elle n'éclaire rien.
 */
/*
 * Le cœur du test, sur une soupe de triangles quelconque.
 *
 * Il est séparé de `light_is_enclosed` parce que TROIS contrôles s'en servent
 * désormais, sur deux rangements d'indices différents : les luminaires
 * accumulés dans un `geo_mesh` (des `geo_tri`, quatre mots par triangle, le
 * quatrième portant le matériau) et les objets rangés dans un bloc de
 * primitives (trois mots par triangle). D'où `stride`, en mots de 32 bits :
 * une seule routine plutôt qu'une copie par rangement, et une copie d'un
 * lancer de rayons est une copie qui finit par diverger d'un epsilon.
 *
 * `need` est le nombre de rayons impairs qu'il faut atteindre. Passé > 0, la
 * boucle s'arrête dès que le verdict est acquis dans un sens ou dans l'autre —
 * un point DEHORS ne décroche en général aucun rayon impair et se règle en
 * quatre rayons sur sept. Passé à 0, les sept rayons sont tirés quoi qu'il
 * arrive et `near_out` porte alors le minimum sur les sept : c'est ce dont
 * `check_lights_not_enclosed` a besoin, sa seconde condition portant sur cette
 * distance-là.
 */
#define RG_PARITY_RAYS 7

static int parity_odd_rays(const gltf_vertex *v, const uint32_t *idx, size_t stride,
                           size_t tri_count, ns_v3 p, int need, float *near_out)
{
    /* Sept directions non alignées sur les axes : un rayon axial longe trop
     * souvent une arête de boîte, et donne alors un compte au hasard. */
    static const ns_v3 dirs[RG_PARITY_RAYS] = {
        { 0.7137f,  0.4472f,  0.5395f }, { -0.6325f,  0.5477f,  0.5477f },
        { 0.5164f, -0.7746f,  0.3651f }, { -0.4082f, -0.4082f,  0.8165f },
        { 0.9129f,  0.2582f, -0.3162f }, { -0.3015f,  0.9045f, -0.3015f },
        { 0.2673f, -0.5345f, -0.8018f },
    };

    int odd = 0;
    float nearest = 1e9f;

    for (int d = 0; d < RG_PARITY_RAYS; ++d) {
        if (need > 0) {
            if (odd >= need) break;                          /* acquis */
            if (odd + (RG_PARITY_RAYS - d) < need) break;    /* hors d'atteinte */
        }
        int hits = 0;
        for (size_t k = 0; k < tri_count; ++k) {
            const uint32_t *t = idx + k * stride;
            const ns_v3 a = ns_v3_make(v[t[0]].position[0], v[t[0]].position[1],
                                       v[t[0]].position[2]);
            const ns_v3 bb = ns_v3_make(v[t[1]].position[0], v[t[1]].position[1],
                                        v[t[1]].position[2]);
            const ns_v3 c = ns_v3_make(v[t[2]].position[0], v[t[2]].position[1],
                                       v[t[2]].position[2]);
            float dist = 0.0f, bu = 0.0f, bv = 0.0f;
            if (ns_ray_triangle(p, dirs[d], a, bb, c, 1e9f, &dist, &bu, &bv)) {
                hits++;
                if (dist < nearest) nearest = dist;
            }
        }
        if (hits & 1) odd++;
    }
    if (near_out) *near_out = nearest;
    return odd;
}

static bool light_is_enclosed(const geo_mesh *m, ns_v3 p,
                              int *odd_out, float *near_out)
{
    const gltf_vertex *v = (const gltf_vertex *)m->verts.data;
    /* Un `geo_tri` est trois indices puis son matériau : quatre mots. */
    const uint32_t *idx = (const uint32_t *)m->tris.data;

    float nearest = 1e9f;
    const int odd = parity_odd_rays(v, idx, sizeof(geo_tri) / sizeof(uint32_t),
                                    m->tris.count, p, 0, &nearest);
    if (odd_out) *odd_out = odd;
    if (near_out) *near_out = nearest;

    /*
     * DEUX conditions, et il faut les deux.
     *
     * La parité seule ne suffit pas : le maillage d'un décor n'est pas étanche.
     * Une ampoule de dix segments sur six anneaux a des pôles ouverts, une cage
     * a des barreaux — mesuré sur la suspension du salon, une source posée
     * exactement au centre de son ampoule donne sept comptes de 1, 1, 2, 0, 0,
     * 1 et 4. Une majorité simple l'aurait déclarée DEHORS.
     *
     * On demande donc deux rayons impairs sur sept — le signe qu'on est dans
     * quelque chose — ET qu'il y ait de la matière à moins de 25 cm, ce qui est
     * la condition physique réelle : une source n'est occultée par son
     * luminaire que si le luminaire est là. Un point isolé au milieu de la
     * salle peut décrocher un compte impair par accident ; il n'aura pas de
     * triangle à vingt-cinq centimètres.
     */
    return odd >= 2 && nearest < 0.25f;
}

/*
 * Le contrôle, sur toutes les lumières.
 *
 * Il ARRÊTE le build. Un avertissement se noierait dans le journal, et le
 * symptôme à l'exécution ne ressemble pas à sa cause : monter l'intensité d'une
 * source enfermée ne change RIEN, ce qui envoie chercher du côté de
 * l'éclairement. Ce défaut a coûté DEUX fois dans ce projet — la suspension du
 * billard est restée ainsi tout le développement, et l'applique du salon a
 * refait la même chose trois heures après sa correction.
 *
 * L'échappatoire est explicite : `"insideOk": true` pour qui sait ce qu'il
 * fait. Mais il faut l'écrire.
 */
static void check_lights_not_enclosed(const rg_builder *b)
{
    if (b->props_mesh.tris.count == 0) return;
    size_t checked = 0;
    for (size_t i = 0; i < b->light_count; ++i) {
        const rg_light *l = &b->lights[i];
        if (l->inside_ok) continue;
        checked++;
        const ns_v3 p = ns_v3_make(l->position[0], l->position[1], l->position[2]);
        int odd = 0;
        float nearest = 0.0f;
        if (light_is_enclosed(&b->props_mesh, p, &odd, &nearest)) {
            tool_fatalf("la lumière « %s » est posée À L'INTÉRIEUR d'un luminaire "
                        "(%.2f, %.2f, %.2f ; %d rayons sur 7 en parité impaire, "
                        "matière à %.0f mm) — elle s'éteindra elle-même.\n"
                        "  Le lancer de rayons ne connaît pas les matériaux "
                        "émissifs : chaque rayon d'ombre devra traverser le verre "
                        "ou la cage qui l'entoure.\n"
                        "  Symptôme à l'exécution : monter l'intensité ne change "
                        "RIEN. Poser la source SOUS ou DEVANT le luminaire.\n"
                        "  Si c'est voulu, l'écrire : \"insideOk\": true.",
                        l->name, (double)p.x, (double)p.y, (double)p.z,
                        odd, (double)(nearest * 1000.0f));
        }
    }
    if (checked) printf("  %zu lumière(s) vérifiée(s) hors de leur luminaire\n", checked);
}

/* ========================================================================== */
/* C-01 — rien ne sort du bâtiment                                            */
/* ========================================================================== */

/*
 * Point dans le polygone, par parité de croisements — le même théorème de
 * Jordan que `parity_odd_rays`, en plan et donc sans vote : un contour de plan
 * est fermé par construction, là où un maillage de décor ne l'est pas.
 *
 * Les coordonnées de plan suivent la convention de tout le fichier : `.x` est
 * X, `.y` est **Z**.
 */
/*
 * Les deux vivent dans `reach_grid.c`, et pas ici, pour une raison qui n'est pas
 * de rangement : la règle du DEHORS s'en sert, elle a produit un faux positif
 * réel — deux pièces mitoyennes déclarées incommunicables — et elle doit donc
 * pouvoir s'éprouver sur des pièces fabriquées, sans salle et sans asset. Les
 * réécrire ici en ferait une seconde version du théorème de Jordan, qui
 * divergerait de l'autre sur un cas limite le jour où l'une des deux serait
 * corrigée.
 */
static bool plan_inside(const ns_v2 *poly, size_t n, float x, float z)
{
    return reach_point_in_polygon(poly, n, x, z);
}

static float plan_segment_distance(ns_v2 a, ns_v2 c, float x, float z)
{
    return reach_point_segment_distance(a, c, x, z);
}

/*
 * Distance SIGNÉE d'un point à la ligne médiane de la coquille : positive
 * dedans, négative dehors. `seg_out` reçoit le segment le plus proche, pour que
 * le message d'erreur puisse montrer le pan de mur concerné plutôt qu'un
 * chiffre seul.
 *
 * On mesure à la médiane et on décale ensuite de `thickness / 2`, plutôt que de
 * construire le polygone rentré : rentrer un polygone concave demande de
 * traiter les angles rentrants (le pan coupé en est un cas limite), alors que
 * la distance à la médiane est exacte partout et ne dépend d'aucun cas.
 */
static float shell_signed_distance(const rg_wall_plan *sh, float x, float z, size_t *seg_out)
{
    float best = 1e9f;
    size_t seg = 0;
    for (size_t i = 0; i < sh->count; ++i) {
        const ns_v2 a = sh->points[i];
        const ns_v2 c = sh->points[(i + 1) % sh->count];
        const float d = plan_segment_distance(a, c, x, z);
        if (d < best) { best = d; seg = i; }
    }
    if (seg_out) *seg_out = seg;
    return plan_inside(sh->points, sh->count, x, z) ? best : -best;
}

/*
 * Distance d'un point au PAREMENT le plus proche, tous murs confondus.
 *
 * La médiane est à `thickness / 2` de chacune de ses deux faces : la distance
 * au parement est donc |distance à la médiane − demi-épaisseur|, et cette
 * écriture vaut aussi bien pour la coquille, où l'on est d'un seul côté, que
 * pour une cloison, qui a deux faces également légitimes.
 */
static float nearest_parement(const rg_builder *b, float x, float z, const char **wall_out)
{
    float best = 1e9f;
    for (size_t w = 0; w < b->wall_plan_count; ++w) {
        const rg_wall_plan *p = &b->wall_plans[w];
        const size_t segs = p->closed ? p->count : (p->count > 0 ? p->count - 1 : 0);
        for (size_t i = 0; i < segs; ++i) {
            const float d = plan_segment_distance(p->points[i],
                                                  p->points[(i + 1) % p->count], x, z);
            const float to_face = fabsf(d - p->thickness * 0.5f);
            if (to_face < best) { best = to_face; if (wall_out) *wall_out = p->name; }
        }
    }
    return best;
}

/*
 * LA DETTE DE PLACEMENT, mesurée, nommée, et qui ne peut que diminuer.
 *
 * Deux objets de la salle sont AUJOURD'HUI hors du bâtiment, et ce n'est pas
 * une découverte de ce contrôle : l'audit les a mesurés (P-14, P-23) et le plan
 * les porte en toutes lettres dans « Ce qui reste ouvert ». Leur correction est
 * une modification de la DESCRIPTION, qui ne m'appartient pas ici.
 *
 * Le choix est donc entre trois états, et deux sont mauvais :
 *
 *   - ne pas écrire le contrôle : c'est ce qui a mis quatre objets sur le
 *     trottoir ;
 *   - l'écrire et casser le build : le contrôle serait retiré dans l'heure par
 *     celui qu'il bloque, et on retomberait sur le premier état ;
 *   - l'écrire, et nommer ici les deux défauts connus AVEC leur mesure.
 *
 * Le troisième laisse le contrôle fatal pour tout le reste — c'est-à-dire pour
 * le prochain objet qui sortira. La tolérance ne porte que sur un nom ET une
 * cote : un objet de la liste qui s'éloignerait davantage redevient fatal, et
 * la liste avertit dès que le défaut est corrigé, pour qu'on retire la ligne.
 * Elle ne peut donc ni s'étendre en silence, ni survivre à sa raison d'être.
 *
 * Sa place définitive est la description : `"outsideOk": true` si c'est voulu,
 * une coordonnée corrigée sinon. Cette liste est un passage, pas une adresse.
 */
typedef struct rg_outside_debt {
    const char *name;
    float       beyond;   /* mètres au-delà du parement intérieur, mesurés */
    const char *why;
} rg_outside_debt;

/*
 * VIDE, et c'est le but de la table plutôt que sa fin. Elle a porté deux
 * dettes — la cinquième poutre qui ressortait par le pan coupé (P-14) et le
 * tapis technique qui traversait le mur ouest (P-23) — jusqu'à ce que le plan
 * de 2020 soit rétabli et qu'elles n'aient plus lieu d'être. C'est le contrôle
 * lui-même qui a demandé le retrait de leurs lignes, une fois qu'il a mesuré
 * que les deux objets étaient rentrés.
 *
 * On garde la mécanique : la prochaine dette s'écrira ici, et le contrôle
 * réclamera son retrait le jour où elle sera payée. On ne garde pas une dette
 * imaginaire pour faire tenir un tableau — d'où le pointeur nul plutôt qu'une
 * entrée sentinelle qu'il faudrait penser à sauter.
 */
static const rg_outside_debt *const RG_OUTSIDE_DEBT = NULL;
#define RG_OUTSIDE_DEBT_COUNT 0u

/*
 * C-01 — chaque sommet de chaque objet est dans le bâtiment.
 *
 * La règle : un sommet doit être à l'intérieur du parement intérieur, ou à
 * moins de `wallThickness` derrière lui. Cette marge n'est pas une commodité,
 * c'est le cas normal : une affiche mord dans son mur, un tableau électrique
 * s'y encastre, un about de poutre s'y appuie. Au-delà du parement EXTÉRIEUR,
 * en revanche, il n'y a plus de mur : il y a la rue.
 *
 * Ce contrôle est le plus rentable du lot. À lui seul il aurait attrapé six des
 * vingt-trois défauts de l'audit de placement, dont quatre des cinq bloquants —
 * le comptoir d'accueil à 1,04 m dehors, la poubelle à 0,80 m, l'applique et
 * son ampoule sur la face extérieure du mur de brique, et le lavabo dont les
 * axes intervertis faisaient sortir le plan de 15 cm.
 *
 * Ce qu'il NE regarde pas : les sols et le plafond. Ils débordent de 1,55 m à
 * l'est, c'est mesuré, et ce n'est pas un défaut de placement — personne ne va
 * là-bas. Il ne porte donc que sur les solides : boîtes, bornes et props.
 */
#define RG_SHELL_SLACK 0.001f   /* 1 mm : le bruit du flottant, pas une tolérance */

static void check_inside_shell(const rg_builder *b)
{
    if (b->shell_index < 0) {
        tool_warnf("aucun contour fermé dans « walls » : le contrôle « rien ne sort "
                   "du bâtiment » n'a rien vérifié. C'est le pire état d'un contrôle.");
        return;
    }
    const rg_wall_plan *sh = &b->wall_plans[b->shell_index];

    const gltf_vertex *verts = (const gltf_vertex *)b->verts.data;
    const float t = sh->thickness;
    size_t checked = 0, tolerated = 0;
    bool debt_seen[RG_OUTSIDE_DEBT_COUNT + 1u];   /* + 1 : un tableau de zéro élément est interdit en C */
    memset(debt_seen, 0, sizeof debt_seen);

    for (size_t i = 0; i < b->solid_count; ++i) {
        const rg_solid *s = &b->solids[i];
        if (s->outside_ok) continue;
        checked++;

        /*
         * On retient le contour qui accueille le MIEUX l'objet, c'est-à-dire
         * celui dont le pire débord est le plus faible. Un objet du bloc
         * sanitaire est très loin hors du hall, et parfaitement dedans chez
         * lui : lui reprocher le hall n'aurait aucun sens.
         */
        float worst = 1e9f;
        ns_v3 worst_p = ns_v3_zero();
        size_t worst_seg = 0;
        const rg_wall_plan *sh_used = sh;
        for (size_t c = 0; c < b->shell_count; ++c) {
            const rg_wall_plan *cand = &b->wall_plans[b->shells[c]];
            float w = -1e9f;
            ns_v3 wp = ns_v3_zero();
            size_t wseg = 0;
            for (size_t k = s->vert_begin; k < s->vert_end; ++k) {
                const float x = verts[k].position[0], z = verts[k].position[2];
                size_t seg = 0;
                /* `beyond` compte depuis le PAREMENT INTÉRIEUR, qui est à
                 * `thickness / 2` de la médiane, du côté de la salle. */
                const float beyond = cand->thickness * 0.5f
                                   - shell_signed_distance(cand, x, z, &seg);
                if (beyond > w) {
                    w = beyond;
                    wp = ns_v3_make(verts[k].position[0], verts[k].position[1],
                                    verts[k].position[2]);
                    wseg = seg;
                }
            }
            if (w < worst) { worst = w; worst_p = wp; worst_seg = wseg; sh_used = cand; }
        }
        if (worst > 1e8f) continue;
        if (worst <= sh_used->thickness + RG_SHELL_SLACK) continue;

        /* Le pan de parement le plus proche, rentré de la demi-épaisseur, pour
         * que le message montre le mur dont il parle. */
        const ns_v2 a = sh_used->points[worst_seg];
        const ns_v2 c = sh_used->points[(worst_seg + 1) % sh_used->count];
        float nx = c.y - a.y, nz = -(c.x - a.x);
        const float nl = sqrtf(nx * nx + nz * nz);
        if (nl > 1e-9f) { nx /= nl; nz /= nl; }
        const float mx = (a.x + c.x) * 0.5f, mz = (a.y + c.y) * 0.5f;
        if (!plan_inside(sh_used->points, sh_used->count, mx + nx * 0.01f, mz + nz * 0.01f)) {
            nx = -nx; nz = -nz;
        }
        const float th = sh_used->thickness;
        const float in_ax = a.x + nx * th * 0.5f, in_az = a.y + nz * th * 0.5f;
        const float in_cx = c.x + nx * th * 0.5f, in_cz = c.y + nz * th * 0.5f;

        const rg_outside_debt *debt = NULL;
        for (size_t d = 0; d < RG_OUTSIDE_DEBT_COUNT; ++d) {
            if (strcmp(RG_OUTSIDE_DEBT[d].name, s->name) != 0) continue;
            debt_seen[d] = true;
            if (worst <= RG_OUTSIDE_DEBT[d].beyond + RG_SHELL_SLACK) debt = &RG_OUTSIDE_DEBT[d];
            break;
        }
        if (debt) {
            tolerated++;
            tool_warnf("« %s » est à %.3f m au-delà du parement intérieur "
                       "(%.2f m dehors) — dette connue : %s",
                       s->name, (double)worst, (double)(worst - t), debt->why);
            continue;
        }

        tool_fatalf("« %s » est à %.2f m AU-DELÀ du parement intérieur (%.2f m dehors).\n"
                    "  pire sommet   (%.2f, %.2f, %.2f)\n"
                    "  parement le plus proche : de (%.2f, %.2f) à (%.2f, %.2f)\n"
                    "  Un objet dehors n'est pas invisible : il se voit depuis la rue, "
                    "et il MANQUE là où il devait être.\n"
                    "  Un objet a le droit de mordre dans son mur — %.2f m ici — mais "
                    "pas de le traverser.\n"
                    "  Si c'est voulu, l'écrire : \"outsideOk\": true.",
                    s->name, (double)worst, (double)(worst - t),
                    (double)worst_p.x, (double)worst_p.y, (double)worst_p.z,
                    (double)in_ax, (double)in_az, (double)in_cx, (double)in_cz,
                    (double)t);
    }

    for (size_t d = 0; d < RG_OUTSIDE_DEBT_COUNT; ++d) {
        if (debt_seen[d]) continue;
        tool_warnf("« %s » ne sort plus du bâtiment : la dette est payée, retirer "
                   "sa ligne de `RG_OUTSIDE_DEBT` dans tools/roomgen.c (%s)",
                   RG_OUTSIDE_DEBT[d].name, RG_OUTSIDE_DEBT[d].why);
    }
    printf("  %zu objet(s) confronté(s) au parement intérieur, %zu toléré(s) par la "
           "dette déclarée\n", checked, tolerated);
}

/* ========================================================================== */
/* C-02 — deux solides ne se pénètrent pas                                    */
/* ========================================================================== */

/*
 * Un nom déclaré vaut pour toute sa série. « pilier_ouest » désigne les cinq
 * `pilier_ouest_1..5` : `instance_name` numérote les exemplaires d'une
 * déclaration répétée, et exiger d'écrire les cinq noms ferait d'une
 * déclaration juste une liste qu'on oublie de rallonger le jour où le sixième
 * pilier arrive. Un nom exact reste évidemment accepté.
 */
static bool name_matches_series(const char *declared, const char *instance)
{
    if (strcmp(declared, instance) == 0) return true;
    const size_t n = strlen(declared);
    if (strncmp(declared, instance, n) != 0 || instance[n] != '_') return false;
    for (const char *c = instance + n + 1; *c; ++c) {
        if (*c < '0' || *c > '9') return false;
    }
    return instance[n + 1] != '\0';
}

static bool pair_is_declared(const rg_solid *A, const rg_solid *B)
{
    for (size_t i = 0; i < A->traverse_count; ++i) {
        if (name_matches_series(A->traverse[i], B->name)) return true;
    }
    for (size_t i = 0; i < B->traverse_count; ++i) {
        if (name_matches_series(B->traverse[i], A->name)) return true;
    }
    return false;
}

/*
 * Un point est-il dans le solide ? Même parité que pour les luminaires, avec
 * DEUX différences, et chacune a sa raison.
 *
 * La majorité, 4 rayons sur 7 : c'est le critère avec lequel l'audit de
 * placement a mesuré ses pénétrations, et le reprendre tel quel rend ses
 * chiffres reproductibles ici. Pour un luminaire on descend à 2 sur 7, parce
 * qu'un abat-jour ouvert par le bas ne renvoie pas de majorité.
 *
 * Pas de condition de distance : un sommet planté au milieu d'un canapé de
 * 2,73 m est loin de toute surface, et la condition des 25 cm — qui vaut pour
 * une source, occultée seulement si le verre est là — l'aurait déclaré dehors.
 */
static bool solid_contains_point(const rg_builder *b, const rg_solid *s, ns_v3 p)
{
    if (s->tri_count == 0) return false;
    if (p.x < s->bounds.min.x || p.x > s->bounds.max.x
     || p.y < s->bounds.min.y || p.y > s->bounds.max.y
     || p.z < s->bounds.min.z || p.z > s->bounds.max.z) return false;

    const geo_primitives *g = &TOOL_VEC_AT(&b->prim_blocks, geo_primitives, s->prim_block);
    const gltf_vertex *v = (const gltf_vertex *)b->verts.data;
    return parity_odd_rays(v, g->storage, 3, s->tri_count, p, 4, NULL) >= 4;
}

/* Le premier sommet de A qui est dans le solide de B, s'il y en a un. On
 * s'arrête au premier : le verdict est acquis, et compter les suivants coûte
 * un lancer de rayons par sommet pour un chiffre que le message n'emploie pas. */
static bool first_vertex_inside(const rg_builder *b, const rg_solid *A, const rg_solid *B,
                                ns_v3 *out)
{
    const gltf_vertex *verts = (const gltf_vertex *)b->verts.data;
    for (size_t k = A->vert_begin; k < A->vert_end; ++k) {
        const ns_v3 p = ns_v3_make(verts[k].position[0], verts[k].position[1],
                                   verts[k].position[2]);
        if (solid_contains_point(b, B, p)) { if (out) *out = p; return true; }
    }
    return false;
}

/*
 * LES PAIRES DÉJÀ CONNUES, sur le modèle de `RG_OUTSIDE_DEBT` et pour la même
 * raison : le contrôle s'écrit ici, la description se corrige ailleurs.
 *
 * Deux natures, et elles ne se confondent pas :
 *
 *   - les ASSEMBLAGES, qui sont JUSTES et n'attendent que leur déclaration
 *     `"traverse"` dans la description. Une ampoule est dans sa douille, un
 *     robinet est monté dans sa vasque, une enseigne est fixée à son comptoir.
 *     Ces six-là ne sont pas une dette : ce sont des `"traverse"` qui n'ont pas
 *     encore été écrits, et le jour où ils le seront, ces lignes disparaîtront
 *     sans que rien ne change au verdict.
 *   - les DÉFAUTS, mesurés, ouverts, et qu'on ne masque pas : deux viennent de
 *     l'audit de placement (P-16, P-18), DEUX ONT ÉTÉ TROUVÉS PAR CE CONTRÔLE
 *     et sont nés de la correction d'autres défauts — la poubelle sortie de la
 *     rue est entrée dans un pilier, le bureau sorti de la rue est entré dans
 *     le battant de la porte. C'est la démonstration la plus courte de ce que
 *     vaut un contrôle : deux corrections faites à la main, deux nouveaux
 *     défauts, aucun signalement.
 *
 * Les deux tables nomment des PAIRES, jamais des catégories : c'est exactement
 * le reproche fait au contrôle d'avant, dont la tolérance « tout sauf les
 * bornes » laissait passer un canapé empalé sur un poteau de béton.
 */
typedef struct rg_named_pair {
    const char *a, *b;
    const char *why;
} rg_named_pair;

/* Rendue générique parce que DEUX contrôles s'en servent : les paires qui se
 * pénètrent, et la borne devant laquelle un objet se tient. La forme est la
 * même — deux noms et une raison écrite — et la dupliquer produirait deux
 * routines d'appariement qui divergeraient sur le suffixe de série. */

static const rg_named_pair RG_OVERLAP_ASSEMBLY[] = {
    { "lavabo_toilettes", "robinet_toilettes",
      "le robinet est monté dans la vasque (216 sommets sur 288)" },
    { "suspension_comptoir", "ampoule_comptoir",
      "l'ampoule est dans sa douille (0,00041 m³)" },
    { "suspension_salon", "ampoule_salon",
      "l'ampoule est dans sa douille (0,00041 m³)" },
};
#define RG_OVERLAP_ASSEMBLY_COUNT \
    (sizeof RG_OVERLAP_ASSEMBLY / sizeof RG_OVERLAP_ASSEMBLY[0])

/*
 * VIDE — même histoire que `RG_OUTSIDE_DEBT`. Le boombox enfoncé dans le
 * plateau du comptoir (P-16) et le comptoir d'accueil planté dans le
 * débattement du battant d'entrée sont l'un et l'autre séparés depuis que le
 * mobilier a repris ses places de 2020, et le contrôle a demandé le retrait de
 * leurs deux lignes.
 */
static const rg_named_pair *const RG_OVERLAP_DEBT = NULL;
#define RG_OVERLAP_DEBT_COUNT 0u

static const rg_named_pair *pair_in_table(const rg_named_pair *table, size_t count,
                                            const rg_solid *A, const rg_solid *B)
{
    for (size_t i = 0; i < count; ++i) {
        const rg_named_pair *d = &table[i];
        if ((name_matches_series(d->a, A->name) && name_matches_series(d->b, B->name))
         || (name_matches_series(d->a, B->name) && name_matches_series(d->b, A->name))) {
            return d;
        }
    }
    return NULL;
}

/*
 * C-02 — la boîte englobante PRÉ-FILTRE, le triangle TRANCHE.
 *
 * Ce contrôle existait, et il faisait deux choses discutables. Il ne comparait
 * que des boîtes englobantes : sur les huit paires que l'audit a retestées au
 * triangle, DEUX ne se pénétraient pas du tout — le canapé et le fauteuil
 * partagent 0,3202 m³ de boîtes sans qu'aucun de leurs 2 759 sommets candidats
 * ne soit dans l'autre. Et il n'était fatal que si une BORNE était en cause,
 * ce qui a laissé passer un canapé empalé sur un poteau de béton, 0,0412 m³
 * d'intersection réelle, en simple avertissement pendant tout un palier.
 *
 * Donc : la boîte reste le pré-filtre, parce que c'est ce qu'elle sait faire et
 * que c'est gratuit ; toute paire qui la passe est confirmée au triangle par la
 * parité de `parity_odd_rays` ; et une pénétration confirmée est FATALE, sauf
 * si la paire est déclarée par son nom.
 *
 * CE QU'IL NE VOIT PAS, et c'est mesuré plutôt que supposé. Le critère est
 * « un SOMMET de A dans le solide de B » : il ne voit donc pas deux volumes qui
 * se croisent sans qu'aucun sommet ne tombe dans l'autre. Le cas existe dans
 * cette salle — les cinq poutres traversent leurs dix piliers sur
 * 0,48 x 0,20 x 0,42 m, et AUCUNE des dix paires n'est confirmée : les sommets
 * de la poutre sont à ses deux bouts, à neuf mètres de là, et le haut du pilier
 * (2,92 m) passe au-dessus du dessus de la poutre (2,90 m). C'est le prix du
 * critère qui supprime les fausses alertes, et il se paierait en testant aussi
 * les ARÊTES contre les triangles. Il n'y a aucun sous-entendu ici : les dix
 * paires poutre/pilier passent, et ce n'est pas parce qu'elles sont tolérées.
 */
static void check_solid_overlaps(const rg_builder *b)
{
    size_t boxes = 0, declared = 0, assembled = 0, tolerated = 0, clear = 0;
    bool debt_seen[RG_OVERLAP_DEBT_COUNT + 1u];   /* + 1 : un tableau de zéro élément est interdit en C */
    bool asm_seen[RG_OVERLAP_ASSEMBLY_COUNT];
    memset(debt_seen, 0, sizeof debt_seen);
    memset(asm_seen, 0, sizeof asm_seen);

    for (size_t i = 0; i < b->solid_count; ++i) {
        for (size_t j = i + 1; j < b->solid_count; ++j) {
            const rg_solid *A = &b->solids[i], *B = &b->solids[j];

            const float ox = overlap_1d(A->bounds.min.x, A->bounds.max.x,
                                        B->bounds.min.x, B->bounds.max.x);
            const float oy = overlap_1d(A->bounds.min.y, A->bounds.max.y,
                                        B->bounds.min.y, B->bounds.max.y);
            const float oz = overlap_1d(A->bounds.min.z, A->bounds.max.z,
                                        B->bounds.min.z, B->bounds.max.z);
            if (ox <= RG_OVERLAP_TOLERANCE || oy <= RG_OVERLAP_TOLERANCE
             || oz <= RG_OVERLAP_TOLERANCE) {
                continue;                       /* disjoints sur au moins un axe */
            }
            boxes++;

            /* La déclaration passe AVANT tout le reste, y compris avant la
             * règle des bornes : une échappatoire qui ne vaut que pour certains
             * genres d'objets est une échappatoire dont il faut se rappeler la
             * portée, et on ne s'en rappelle pas. */
            if (pair_is_declared(A, B)) { declared++; continue; }

            /*
             * La BORNE reste jugée à la boîte, et c'est le seul cas.
             *
             * Ce n'est pas la même propriété que la pénétration : une borne doit
             * se tenir dans du VIDE — on ne joue pas sur un caisson dont le
             * voisin recouvre la façade, même sans un triangle en commun. Le
             * contrôle d'avant avait raison sur ce point-là, on le garde tel
             * quel plutôt que de l'affaiblir en passant tout au triangle.
             */
            if (A->kind == RG_SOLID_CABINET || B->kind == RG_SOLID_CABINET) {
                tool_fatalf("« %s » et « %s » occupent le même volume : "
                            "recouvrement de %.3f x %.3f x %.3f m\n"
                            "  %s : [%.2f %.2f %.2f] - [%.2f %.2f %.2f]\n"
                            "  %s : [%.2f %.2f %.2f] - [%.2f %.2f %.2f]",
                            A->name, B->name, (double)ox, (double)oy, (double)oz,
                            A->name,
                            (double)A->bounds.min.x, (double)A->bounds.min.y, (double)A->bounds.min.z,
                            (double)A->bounds.max.x, (double)A->bounds.max.y, (double)A->bounds.max.z,
                            B->name,
                            (double)B->bounds.min.x, (double)B->bounds.min.y, (double)B->bounds.min.z,
                            (double)B->bounds.max.x, (double)B->bounds.max.y, (double)B->bounds.max.z);
            }

            ns_v3 hit = ns_v3_zero();
            const rg_solid *in = NULL, *of = NULL;
            if (first_vertex_inside(b, A, B, &hit))      { in = A; of = B; }
            else if (first_vertex_inside(b, B, A, &hit))  { in = B; of = A; }
            if (!in) { clear++; continue; }

            const rg_named_pair *asmb = pair_in_table(RG_OVERLAP_ASSEMBLY,
                                                        RG_OVERLAP_ASSEMBLY_COUNT, A, B);
            if (asmb) {
                asm_seen[(size_t)(asmb - RG_OVERLAP_ASSEMBLY)] = true;
                assembled++;
                continue;
            }

            const rg_named_pair *debt = pair_in_table(RG_OVERLAP_DEBT,
                                                        RG_OVERLAP_DEBT_COUNT, A, B);
            if (debt) {
                const size_t k = (size_t)(debt - RG_OVERLAP_DEBT);
                if (!debt_seen[k]) {
                    tool_warnf("« %s » et « %s » se pénètrent (%.3f x %.3f x %.3f m "
                               "de boîtes communes) — défaut connu, non corrigé : %s",
                               A->name, B->name, (double)ox, (double)oy, (double)oz,
                               debt->why);
                    debt_seen[k] = true;
                }
                tolerated++;
                continue;
            }

            tool_fatalf("« %s » PÉNÈTRE « %s » : le sommet (%.3f, %.3f, %.3f) de "
                        "« %s » est dans le solide de « %s ».\n"
                        "  boîtes communes : %.3f x %.3f x %.3f m\n"
                        "  Confirmé au triangle, pas à la boîte : la parité de sept "
                        "rayons donne au moins quatre comptes impairs.\n"
                        "  Deux meubles bord à bord sont voulus ; un meuble DANS un "
                        "autre est une faute de frappe sur une coordonnée.\n"
                        "  Si l'assemblage est voulu — une poutre sur son pilier — "
                        "le déclarer par son nom : \"traverse\": [\"%s\"].",
                        in->name, of->name,
                        (double)hit.x, (double)hit.y, (double)hit.z,
                        in->name, of->name,
                        (double)ox, (double)oy, (double)oz, of->name);
        }
    }

    /* Une ligne de tolérance qui ne sert plus est une tolérance qui, un jour,
     * couvrira autre chose que ce pour quoi elle a été écrite. Les deux tables
     * le disent d'elles-mêmes. */
    for (size_t k = 0; k < RG_OVERLAP_DEBT_COUNT; ++k) {
        if (debt_seen[k]) continue;
        tool_warnf("« %s » et « %s » ne se pénètrent plus : retirer leur ligne de "
                   "`RG_OVERLAP_DEBT` dans tools/roomgen.c",
                   RG_OVERLAP_DEBT[k].a, RG_OVERLAP_DEBT[k].b);
    }
    for (size_t k = 0; k < RG_OVERLAP_ASSEMBLY_COUNT; ++k) {
        if (asm_seen[k]) continue;
        tool_warnf("« %s » et « %s » ne se pénètrent plus : retirer leur ligne de "
                   "`RG_OVERLAP_ASSEMBLY` dans tools/roomgen.c",
                   RG_OVERLAP_ASSEMBLY[k].a, RG_OVERLAP_ASSEMBLY[k].b);
    }
    printf("  %zu paire(s) de boîtes communes : %zu sans pénétration réelle, "
           "%zu assemblage(s) (%zu déclaré(s) dans la description), %zu défaut(s) "
           "connu(s) non corrigé(s)\n",
           boxes, clear, assembled + declared, declared, tolerated);
}

/* ========================================================================== */
/* C-03 — ce qui est posé touche son support                                  */
/* ========================================================================== */

/*
 * Le fond du problème : la description déclare tout — le matériau, le son de
 * pas, le pavage, le point d'intérêt — SAUF sur quoi l'objet repose. Il
 * faudrait donc le déduire, et toute déduction se trompe quelque part. C'est le
 * raisonnement qui a fondé la réécriture entière de la salle, appliqué à la
 * pose : on ne devine pas, on déclare.
 *
 *     "pose": "sol"                          la base touche le sol dessous
 *     "pose": "meuble", "poseSur": "<nom>"   la base touche le dessus du meuble
 *     "pose": "mur"                          quelque chose touche un parement
 *     "pose": "suspendu"                     rien ici — c'est l'affaire de C-06
 *     "pose": "libre"                        rien, mais il faut l'écrire
 *
 * POURQUOI LA CLÉ EST FACULTATIVE, et ce n'est pas un renoncement. L'audit la
 * demande obligatoire, et il a raison sur le fond : une clé facultative que
 * personne n'écrit est un contrôle qui ne vérifie rien. Mais aucun des 61 props
 * de la description ne la porte aujourd'hui, et la rendre obligatoire refuserait
 * la salle entière — le contrôle serait retiré avant d'avoir servi une fois.
 * L'outil dit donc, à chaque build, COMBIEN d'objets ne l'ont pas encore : le
 * trou est chiffré, il se comble objet par objet, et le jour où le compte tombe
 * à zéro la clé peut devenir obligatoire en une ligne.
 *
 * Les tolérances sont celles de l'audit : 10 mm sur un sol — c'est l'épaisseur
 * d'un jeu de pose qu'on ne voit pas — et 5 mm sur un meuble, parce qu'un objet
 * posé sur une table est regardé de plus près.
 */
#define RG_POSE_SOL_TOLERANCE    0.010f
#define RG_POSE_MEUBLE_TOLERANCE 0.005f
#define RG_POSE_MUR_TOLERANCE    0.030f

static bool xz_covers(float cx, float cz, float minx, float maxx, float minz, float maxz)
{
    return cx >= minx && cx <= maxx && cz >= minz && cz <= maxz;
}

/*
 * Le DESSUS d'un meuble sous une empreinte donnée, par un rayon vertical.
 *
 * Prendre `bounds.max.y` serait faux, et la première version le faisait : le
 * comptoir du bar porte une enseigne qui monte à 2,05 m, si bien que le boombox
 * posé sur son plateau à 1,048 m était annoncé « enfoncé de 102,6 cm ». Un
 * message faux d'un mètre est pire qu'un contrôle absent — on cherche l'erreur
 * là où elle n'est pas.
 *
 * Le rayon part 50 cm au-dessus de la base de l'objet et descend : la première
 * surface rencontrée est celle sur laquelle il est censé reposer. Cinquante
 * centimètres, parce qu'un objet enfoncé part de plus bas que son support et
 * qu'il faut donc viser au-dessus des deux.
 */
static bool solid_top_under(const rg_builder *b, const rg_solid *host,
                            float cx, float cz, float from_y, float *out_y)
{
    if (host->tri_count == 0) return false;
    const geo_primitives *g = &TOOL_VEC_AT(&b->prim_blocks, geo_primitives, host->prim_block);
    const gltf_vertex *v = (const gltf_vertex *)b->verts.data;
    const ns_v3 origin = ns_v3_make(cx, from_y, cz);
    const ns_v3 down = ns_v3_make(0.0f, -1.0f, 0.0f);

    float nearest = 1e9f;
    for (size_t k = 0; k < host->tri_count; ++k) {
        const uint32_t *t = g->storage + k * 3;
        const ns_v3 a = ns_v3_make(v[t[0]].position[0], v[t[0]].position[1], v[t[0]].position[2]);
        const ns_v3 c = ns_v3_make(v[t[1]].position[0], v[t[1]].position[1], v[t[1]].position[2]);
        const ns_v3 e = ns_v3_make(v[t[2]].position[0], v[t[2]].position[1], v[t[2]].position[2]);
        float dist = 0.0f, bu = 0.0f, bv = 0.0f;
        if (ns_ray_triangle(origin, down, a, c, e, 1e9f, &dist, &bu, &bv) && dist < nearest) {
            nearest = dist;
        }
    }
    if (nearest > 1e8f) return false;
    *out_y = from_y - nearest;
    return true;
}

static void check_grounded(const rg_builder *b)
{
    const gltf_vertex *verts = (const gltf_vertex *)b->verts.data;
    size_t declared = 0, missing = 0, hanging = 0, free_form = 0;

    for (size_t i = 0; i < b->solid_count; ++i) {
        const rg_solid *s = &b->solids[i];
        if (!s->pose[0]) { missing++; continue; }
        declared++;

        const float cx = (s->bounds.min.x + s->bounds.max.x) * 0.5f;
        const float cz = (s->bounds.min.z + s->bounds.max.z) * 0.5f;
        const float base = s->bounds.min.y;

        if (strcmp(s->pose, "libre") == 0) { free_form++; continue; }

        if (strcmp(s->pose, "suspendu") == 0) {
            /* C-06 n'est pas écrit. On le dit plutôt que de compter cet objet
             * comme vérifié : un contrôle qui laisse croire qu'il a regardé est
             * pire que celui qui avoue ne pas l'avoir fait. */
            hanging++;
            continue;
        }

        if (strcmp(s->pose, "sol") == 0) {
            float support = -1e9f;
            const char *support_name = NULL;
            for (size_t f = 0; f < b->floor_count; ++f) {
                const rg_floor *fl = &b->floors[f];
                if (!xz_covers(cx, cz,
                               fl->centre[0] - fl->size[0] * 0.5f,
                               fl->centre[0] + fl->size[0] * 0.5f,
                               fl->centre[1] - fl->size[1] * 0.5f,
                               fl->centre[1] + fl->size[1] * 0.5f)) continue;
                if (fl->y > support) { support = fl->y; support_name = fl->name; }
            }
            /* Une plate-forme est une BOÎTE, et l'estrade est le seul cas de la
             * salle. On ne retient que celles dont le dessus passe SOUS l'objet :
             * un pilier couvre aussi l'empreinte d'un meuble adossé, et son
             * sommet à 2,92 m n'a jamais servi de sol à personne. */
            for (size_t k = 0; k < b->solid_count; ++k) {
                if (k == i || b->solids[k].kind != RG_SOLID_BOX) continue;
                const rg_solid *p = &b->solids[k];
                if (!xz_covers(cx, cz, p->bounds.min.x, p->bounds.max.x,
                               p->bounds.min.z, p->bounds.max.z)) continue;
                if (p->bounds.max.y > base + RG_POSE_SOL_TOLERANCE) continue;
                if (p->bounds.max.y > support) { support = p->bounds.max.y; support_name = p->name; }
            }
            if (!support_name) {
                tool_fatalf("« %s » est déclaré posé au sol, et il n'y a AUCUN sol "
                            "sous lui.\n"
                            "  empreinte      (%.2f, %.2f)\n"
                            "  Soit l'objet est ailleurs qu'où on le croit, soit un "
                            "rectangle de « floors » manque.",
                            s->name, (double)cx, (double)cz);
            }
            const float gap = base - support;
            if (fabsf(gap) > RG_POSE_SOL_TOLERANCE) {
                tool_fatalf("« %s » est déclaré posé au sol et %s de %.1f cm.\n"
                            "  base mesurée   y = %.3f\n"
                            "  sol dessous    y = %.3f (« %s »)\n"
                            "  Un modèle importé porte son propre décalage : le "
                            "corriger avec \"modelOffset\", pas avec \"at\" — « at » "
                            "déplace aussi tout ce qui est calé dessus.\n"
                            "  Si la pose est autre, l'écrire : \"pose\": \"libre\".",
                            s->name, gap > 0.0f ? "FLOTTE" : "S'ENFONCE",
                            (double)(fabsf(gap) * 100.0f), (double)base,
                            (double)support, support_name);
            }
            continue;
        }

        if (strcmp(s->pose, "meuble") == 0) {
            if (!s->pose_sur[0]) {
                tool_fatalf("« %s » est déclaré posé sur un meuble sans dire lequel. "
                            "Écrire \"poseSur\": \"<nom>\".", s->name);
            }
            const rg_solid *host = NULL;
            for (size_t k = 0; k < b->solid_count; ++k) {
                if (k != i && strcmp(b->solids[k].name, s->pose_sur) == 0) {
                    host = &b->solids[k];
                    break;
                }
            }
            if (!host) {
                tool_fatalf("« %s » est déclaré posé sur « %s », qui n'existe pas.\n"
                            "  Un nom de support qui ne désigne rien est un contrôle "
                            "qui ne vérifie rien, en silence.",
                            s->name, s->pose_sur);
            }
            if (!xz_covers(cx, cz, host->bounds.min.x, host->bounds.max.x,
                           host->bounds.min.z, host->bounds.max.z)) {
                tool_fatalf("« %s » est déclaré posé sur « %s », mais son empreinte "
                            "(%.2f, %.2f) tombe HORS de celle du meuble "
                            "([%.2f %.2f] - [%.2f %.2f]).",
                            s->name, host->name, (double)cx, (double)cz,
                            (double)host->bounds.min.x, (double)host->bounds.min.z,
                            (double)host->bounds.max.x, (double)host->bounds.max.z);
            }
            float top = 0.0f;
            if (!solid_top_under(b, host, cx, cz, base + 0.5f, &top)) {
                tool_fatalf("« %s » est déclaré posé sur « %s », dont AUCUNE surface "
                            "ne passe sous son empreinte (%.2f, %.2f).\n"
                            "  Le meuble est peut-être le bon et l'objet à côté.",
                            s->name, host->name, (double)cx, (double)cz);
            }
            const float gap = base - top;
            if (fabsf(gap) > RG_POSE_MEUBLE_TOLERANCE) {
                tool_fatalf("« %s » est déclaré posé sur « %s » et %s de %.1f cm.\n"
                            "  base mesurée   y = %.3f\n"
                            "  dessus de « %s » sous l'empreinte  y = %.3f\n"
                            "  Un objet posé sur un meuble est regardé de près : "
                            "cinq millimètres de jour se voient.",
                            s->name, host->name, gap > 0.0f ? "FLOTTE" : "S'ENFONCE",
                            (double)(fabsf(gap) * 100.0f), (double)base,
                            host->name, (double)top);
            }
            continue;
        }

        if (strcmp(s->pose, "mur") == 0) {
            float best = 1e9f;
            const char *wall = NULL;
            for (size_t k = s->vert_begin; k < s->vert_end; ++k) {
                const char *w = NULL;
                const float d = nearest_parement(b, verts[k].position[0],
                                                 verts[k].position[2], &w);
                if (d < best) { best = d; wall = w; }
            }
            if (best > RG_POSE_MUR_TOLERANCE) {
                tool_fatalf("« %s » est déclaré adossé à un mur et son point le plus "
                            "proche est à %.1f cm du parement le plus proche "
                            "(« %s »).\n"
                            "  Une applique décollée de trois centimètres se voit à "
                            "contre-jour, et une cible de fléchettes décollée ne tient "
                            "à rien.",
                            s->name, (double)(best * 100.0f), wall ? wall : "?");
            }
            continue;
        }

        tool_fatalf("« %s » déclare \"pose\": \"%s\", qui n'est pas une pose "
                    "(sol, meuble, mur, suspendu, libre).\n"
                    "  Une faute de frappe ici désactiverait le contrôle en silence, "
                    "ce qui est exactement ce qu'il existe pour empêcher.",
                    s->name, s->pose);
    }

    printf("  pose : %zu objet(s) déclaré(s) (%zu suspendu(s), non vérifié(s) — C-06 "
           "n'est pas écrit ; %zu libre(s)), %zu SANS clé « pose »\n",
           declared, hanging, free_form, missing);
}

/* ========================================================================== */
/* LE CORPS DU JOUEUR, en un seul endroit                                     */
/* ========================================================================== */

/*
 * Les cotes du personnage, recopiées de `room/room_camera.c` (lignes 58 à 63) où
 * elles sont les DÉFAUTS de `personnage.rayon`, `personnage.taille` et
 * `personnage.marche`.
 *
 * Pourquoi une copie plutôt qu'un partage : `tools/` ne dépend ni de SDL ni du
 * moteur, c'est écrit en tête de `tools/CMakeLists.txt` et c'est ce qui permet de
 * rejouer une conversion depuis n'importe quelle machine. Inclure
 * `room_camera.h` ferait entrer SDL dans la chaîne d'assets pour trois flottants.
 *
 * Ce que la copie coûte, et il faut le dire : `nineteen.env` peut ÉLARGIR le
 * joueur à l'exécution, et ces contrôles ne le sauront pas. Ils mesurent le
 * joueur PAR DÉFAUT — celui avec lequel le jeu est livré, et le seul dont on
 * puisse répondre au build. Un `personnage.rayon` porté à 40 cm est une décision
 * de réglage, pas un état livrable.
 *
 * Deux contrôles s'en servent, et ce n'est pas un hasard : le couloir devant une
 * borne (C-09) et le chemin qui y mène (C-10) mesurent le MÊME corps. Ils l'ont
 * mesuré chacun de son côté pendant un palier, avec deux littéraux 0,32 — c'est
 * ainsi qu'ils se seraient mis à parler de deux joueurs différents.
 */
#define RG_BODY_RADIUS 0.32f   /* personnage.rayon */
#define RG_BODY_HEIGHT 1.82f   /* personnage.taille — le crâne, pas les yeux */
#define RG_BODY_STEP   0.35f   /* personnage.marche — l'obstacle gravi sans saut */

/* ========================================================================== */
/* C-09 — on peut se tenir devant une borne                                   */
/* ========================================================================== */

/*
 * L'audit l'annonçait comme n'attrapant rien — « et c'est la raison de
 * l'ajouter ». Il attrape UN cas, apparu depuis : les dix-neuf bornes tenaient
 * la règle avec deux fois la marge au moment de la mesure (le plus juste,
 * `borne_arcade_18`, à 1,64 m), et `poubelle_ilot` a été posée depuis, à 51 cm
 * devant `borne_arcade_9`. C'est exactement le mouvement annoncé : « l'îlot
 * central est manipulé à chaque fois qu'on ajoute un jeu ».
 *
 * La règle : 80 cm de vide devant chaque borne, le long de sa direction de
 * regard, sur la largeur du CORPS DU JOUEUR — 32 cm de rayon, la même cote que
 * `write_scene_json` emploie déjà pour refuser un point de vue posé dans un
 * meuble. On ignore ce qui est plus bas que 30 cm — un tapis technique
 * n'empêche pas de jouer — et ce qui est plus haut que 1,80 m : une poutre à
 * 2,70 m passe au-dessus de la tête, et la compter ferait refuser cinq bornes
 * pour un obstacle qui n'en est pas un.
 *
 * SUR LES SOMMETS, PAS SUR LES BOÎTES. Une boîte englobante déborde un objet
 * rond de la moitié de sa largeur, et ce contrôle refuse une borne : il doit
 * donc mesurer l'objet, pas sa boîte. Sur cette salle les deux versions
 * s'accordent — `borne_arcade_9` donne 0,51 m dans les deux cas, la poubelle
 * est vraiment dans le couloir — et c'est justement pourquoi le changement se
 * fait maintenant : le jour où il séparera un vrai obstacle d'un coin de boîte,
 * il n'y aura plus personne pour se souvenir que la question se posait.
 */
#define RG_CLEARANCE_NEEDED 0.80f
#define RG_CLEARANCE_HALF   RG_BODY_RADIUS   /* le corps, pas un nombre à part */
#define RG_CLEARANCE_LOW    0.30f
#define RG_CLEARANCE_HIGH   1.80f

/*
 * Et il attrape quelque chose, finalement — un défaut né APRÈS l'audit.
 *
 * L'audit mesurait 1,64 m de dégagement minimum sur les dix-neuf bornes, à un
 * moment où `poubelle_ilot` n'existait pas : elle a été posée en meublant le
 * hall, à 51 cm devant `borne_arcade_9` et dans l'axe où le joueur se plante.
 * C'est exactement le cas de figure annoncé — « l'îlot central est manipulé à
 * chaque fois qu'on ajoute un jeu » — arrivé entre-temps.
 */
/*
 * VIDE. La poubelle de l'îlot ne se tient plus dans les 0,32 m où le joueur se
 * plante devant `borne_arcade_9` ; le contrôle a demandé le retrait de sa ligne.
 */
static const rg_named_pair *const RG_CLEARANCE_DEBT = NULL;
#define RG_CLEARANCE_DEBT_COUNT 0u

static void check_cabinet_clearance(const rg_builder *b)
{
    if (b->shell_index < 0) return;
    const rg_wall_plan *sh = &b->wall_plans[b->shell_index];
    const gltf_vertex *verts = (const gltf_vertex *)b->verts.data;

    size_t tolerated = 0;
    bool debt_seen[RG_CLEARANCE_DEBT_COUNT + 1u];   /* + 1 : un tableau de zéro élément est interdit en C */
    memset(debt_seen, 0, sizeof debt_seen);

    for (size_t i = 0; i < b->cabinet_count; ++i) {
        const rg_cabinet *cab = &b->cabinets[i];

        /* La direction de regard est celle du MEUBLE, pas celle de la dalle :
         * la normale de l'écran penche de 10° vers le haut depuis B14, et sa
         * projection au sol est exactement (sin lacet, cos lacet). */
        float dx = cab->screen_normal[0], dz = cab->screen_normal[2];
        const float dl = sqrtf(dx * dx + dz * dz);
        if (dl < 1e-6f) continue;
        dx /= dl; dz /= dl;
        const float rx = dz, rz = -dx;    /* la droite du couloir */

        const float cx = (cab->bounds_min[0] + cab->bounds_max[0]) * 0.5f;
        const float cz = (cab->bounds_min[2] + cab->bounds_max[2]) * 0.5f;

        /* On part de la FAÇADE, pas du centre : le dégagement se compte depuis
         * la tôle que le joueur a devant lui. */
        float front = 0.0f;
        for (int k = 0; k < 4; ++k) {
            const float px = (k & 1) ? cab->bounds_max[0] : cab->bounds_min[0];
            const float pz = (k & 2) ? cab->bounds_max[2] : cab->bounds_min[2];
            const float proj = (px - cx) * dx + (pz - cz) * dz;
            if (proj > front) front = proj;
        }
        const float ox = cx + dx * front, oz = cz + dz * front;

        float gap = RG_CLEARANCE_NEEDED;
        const char *blocker = NULL;

        /* Le parement, d'abord : un mur n'a pas de sommet dans le couloir, il
         * en est la fin. Trois lignes — l'axe et les deux bords — suffisent,
         * un pan de mur étant droit. */
        for (int side = -1; side <= 1; ++side) {
            const float sx = ox + rx * RG_CLEARANCE_HALF * (float)side;
            const float sz = oz + rz * RG_CLEARANCE_HALF * (float)side;
            for (float d = 0.01f; d <= gap; d += 0.01f) {
                if (shell_signed_distance(sh, sx + dx * d, sz + dz * d, NULL)
                        >= sh->thickness * 0.5f) continue;
                gap = d; blocker = "le parement de la coquille";
                break;
            }
        }

        for (size_t k = 0; k < b->solid_count; ++k) {
            const rg_solid *s = &b->solids[k];
            if (strcmp(s->name, cab->name) == 0) continue;
            if (s->bounds.max.y <= RG_CLEARANCE_LOW) continue;   /* un tapis */
            if (s->bounds.min.y >= RG_CLEARANCE_HIGH) continue;  /* une poutre */
            for (size_t v = s->vert_begin; v < s->vert_end; ++v) {
                const float y = verts[v].position[1];
                if (y <= RG_CLEARANCE_LOW || y >= RG_CLEARANCE_HIGH) continue;
                const float ex = verts[v].position[0] - ox, ez = verts[v].position[2] - oz;
                const float along = ex * dx + ez * dz;
                if (along <= 0.0f || along >= gap) continue;
                if (fabsf(ex * rx + ez * rz) > RG_CLEARANCE_HALF) continue;
                gap = along; blocker = s->name;
            }
        }

        if (blocker) {
            const rg_named_pair *debt = NULL;
            for (size_t k = 0; k < RG_CLEARANCE_DEBT_COUNT; ++k) {
                if (strcmp(RG_CLEARANCE_DEBT[k].a, cab->name) == 0
                 && strcmp(RG_CLEARANCE_DEBT[k].b, blocker) == 0) {
                    debt = &RG_CLEARANCE_DEBT[k];
                    debt_seen[k] = true;
                    break;
                }
            }
            if (debt) {
                tool_warnf("on ne peut pas se tenir devant « %s » : %.2f m de "
                           "dégagement pour %.2f m demandés, « %s » est dans le "
                           "couloir — défaut connu, non corrigé : %s",
                           cab->name, (double)gap, (double)RG_CLEARANCE_NEEDED,
                           blocker, debt->why);
                tolerated++;
                continue;
            }
            tool_fatalf("on ne peut pas se tenir devant « %s » : %.2f m de "
                        "dégagement, %.2f m demandés.\n"
                        "  ce qui bouche : %s\n"
                        "  direction de regard (%.3f, %.3f), couloir de %.2f m de "
                        "demi-largeur, entre %.2f et %.2f m de haut\n"
                        "  Une borne devant laquelle on ne peut pas se planter n'est "
                        "pas une borne : c'est un meuble qui montre une image.",
                        cab->name, (double)gap, (double)RG_CLEARANCE_NEEDED,
                        blocker, (double)dx, (double)dz,
                        (double)RG_CLEARANCE_HALF,
                        (double)RG_CLEARANCE_LOW, (double)RG_CLEARANCE_HIGH);
        }
    }
    for (size_t k = 0; k < RG_CLEARANCE_DEBT_COUNT; ++k) {
        if (debt_seen[k]) continue;
        tool_warnf("on peut de nouveau se tenir devant « %s » : retirer sa ligne de "
                   "`RG_CLEARANCE_DEBT` dans tools/roomgen.c", RG_CLEARANCE_DEBT[k].a);
    }
    printf("  %zu borne(s) : %.2f m de couloir libre devant chacune, %zu défaut(s) "
           "connu(s) non corrigé(s)\n",
           b->cabinet_count, (double)RG_CLEARANCE_NEEDED, tolerated);
}

/* ========================================================================== */
/* C-10 — on peut ALLER quelque part                                          */
/* ========================================================================== */

/*
 * LE CONTRÔLE QUI MANQUAIT.
 *
 * La salle a été livrée avec son joueur ENFERMÉ. La description déclarait le sas
 * d'entrée comme un contour FERMÉ, sans la moindre ouverture vers le hall ;
 * `playerStart` était dedans. Le build a réussi, les trente-six tests sont
 * passés, le paquet est parti, et le joueur a démarré scellé dans une boîte de
 * 1,67 x 9,09 m avec dix-neuf bornes de l'autre côté de la brique.
 *
 * Aucun contrôle ne pouvait le voir, et pas par malchance : les neuf premiers
 * regardent tous un OBJET — est-il dedans (C-01), se pénètre-t-il (C-02), est-il
 * posé (C-05), a-t-il son couloir (C-09). Pas un ne regarde le VIDE entre eux,
 * qui est pourtant la seule chose que le joueur habite. Une salle est un graphe
 * de pièces avant d'être une liste de meubles.
 *
 * LA MESURE. Une grille en plan au pas de 5 cm, un remplissage par diffusion
 * depuis `playerStart`, et une cellule n'est franchissable que si le CORPS y
 * tient — c'est-à-dire si aucun obstacle n'est à moins de son rayon. C'est la
 * collision du jeu réduite à deux dimensions : `room_camera_tick` glisse une
 * capsule de `personnage.rayon`, rien de plus. Ce qui compte est ce qui BARRE :
 *
 *   - les murs GÉNÉRÉS, avec leurs baies. Une baie n'est un passage que si l'on
 *     y entre debout — allège sous la hauteur de marche, linteau au-dessus du
 *     crâne. Une fenêtre à 90 cm d'allège n'est pas une porte, et une trémie
 *     qu'il faut franchir accroupi est une décision qui doit s'écrire ;
 *   - les SOLIDES — boîtes, bornes, props — au triangle et non à la boîte
 *     englobante, sur la seule tranche de hauteur que le corps occupe. Un tapis
 *     de 8 mm ne barre rien, une poutre à 2,70 m passe au-dessus de la tête ;
 *   - le DEHORS. Sans lui, le remplissage sortirait par la porte extérieure et
 *     inonderait le trottoir : l'aire mesurée deviendrait celle du cadre de la
 *     grille, et le contrôle rendrait un chiffre rassurant qui ne veut rien dire.
 *     Est « dans le bâtiment » ce qui tombe dans l'un des contours fermés — le
 *     hall, le bloc sanitaire, le sas.
 *
 * CE QU'IL NE VOIT PAS, dit ici plutôt que supposé. Il travaille en PLAN : une
 * salle à deux niveaux reliés par un escalier lui apparaîtrait comme un seul
 * plancher, et il déclarerait atteignable un étage qu'on ne peut pas monter. La
 * salle n'a qu'un niveau — l'estrade se gravit d'un pas — donc la question ne se
 * pose pas encore ; le jour où elle se posera, c'est ce paragraphe qu'il faudra
 * venir contredire.
 */

/* Le pas. Cinq centimètres : la grille de la salle fait alors 200 000 cellules,
 * ce qui se remplit en quelques dizaines de millisecondes, et un passage d'une
 * porte standard y tient en une quinzaine de cellules. Au pas de 10 cm une porte
 * de 64 cm n'en ferait plus que six, et le verdict deviendrait sensible à
 * l'endroit où la grille tombe. */
#define RG_REACH_CELL 0.05f

/* La tranche de hauteur qu'occupe le corps. Sous la hauteur de marche on
 * enjambe, au-dessus du crâne on passe dessous. Ce sont les cotes du joueur, pas
 * des seuils choisis : voir le bloc « LE CORPS DU JOUEUR ». */
#define RG_REACH_LOW  RG_BODY_STEP
#define RG_REACH_HIGH RG_BODY_HEIGHT

/*
 * LE SEUIL D'AIRE, et sa justification — qui est une MESURE, pas une intuition.
 *
 * `room.playable` est une BOÎTE, et une boîte englobe beaucoup plus que le sol
 * praticable : les deux pans coupés en retranchent près de 10 m², le sas et le
 * bloc sanitaire l'étirent bien au-delà du hall, et le corps du joueur perd
 * encore une bande de 32 cm le long de chaque mur et de chaque meuble. Une salle
 * SAINE ne remplit donc jamais sa boîte, et un seuil haut serait absurde.
 *
 * Les trois chiffres qui fixent celui-ci ont été relevés sur CETTE salle, pour
 * une boîte jouable de 373,1 m² :
 *
 *     salle réparée, hall + couloir + toilettes ...... 124,8 m²   33,5 %
 *     enfermé dans le bloc sanitaire seul ............  18,0 m²    4,8 %
 *     enfermé dans le sas — le défaut livré ..........   4,7 m²    1,3 %
 *
 * Dix pour cent tombe entre les deux mondes et au bord d'aucun : trois fois sous
 * la salle saine, deux fois au-dessus du plus grand enfermement possible ici,
 * huit fois au-dessus de celui qui est parti en production. Le seuil ne prétend
 * pas distinguer une salle bien meublée d'une salle vide — il n'en a pas les
 * moyens et ce n'est pas son travail. Il sépare « on circule » de « on est
 * enfermé », et ces deux-là ne sont pas voisins.
 */
#define RG_REACH_MIN_SHARE 0.10f

/*
 * L'ÉCHAPPATOIRE, et pourquoi c'est une variable d'environnement.
 *
 * Une option de ligne de commande se pose dans `assets/CMakeLists.txt`, c'est-à-
 * dire dans un fichier VERSIONNÉ : elle se commet un soir de transition et
 * désarme le contrôle pour tout le monde, sans que personne ne s'en aperçoive —
 * ce qui est exactement l'état d'avant, avec un fichier de plus. Une variable
 * d'environnement ne peut pas être commise. Elle débloque celui qui en a besoin,
 * sur sa machine, pendant qu'il répare, et elle disparaît avec son terminal.
 *
 * Une valeur inconnue ARRÊTE l'outil plutôt que de retomber sur le défaut : un
 * garde-fou qui s'arme sur une faute de frappe ne garde rien.
 */
static bool reach_warn_only(void)
{
    const char *value = getenv("NINETEEN_ACCESSIBILITE");
    if (!value || !value[0]) return false;
    if (strcmp(value, "avertissement") == 0) {
        tool_warnf("NINETEEN_ACCESSIBILITE=avertissement : C-10 ne casse PAS le "
                   "build. Le défaut est fatal ; ceci est une transition, pas un "
                   "réglage.");
        return true;
    }
    tool_fatalf("NINETEEN_ACCESSIBILITE vaut « %s », qui ne veut rien dire.\n"
                "  La seule valeur acceptée est « avertissement ». Une variable "
                "mal orthographiée qui retomberait en silence sur le défaut "
                "laisserait croire qu'elle a été prise en compte.", value);
    return false;
}

/* Une cible à atteindre : ce qui n'a aucun intérêt si l'on ne peut pas y aller. */
typedef struct rg_reach_target {
    const char *kind;    /* « borne », « point de vue », « zone sonore » */
    char        name[64];
    float       x, z;
    /* Les zones sonores sont des VOLUMES : on n'exige pas un point précis, mais
     * qu'une partie de la zone soit foulable. */
    bool        is_box;
    float       min_x, min_z, max_x, max_z;
} rg_reach_target;

#define RG_MAX_REACH_TARGETS (RG_MAX_CABINETS + 64)

/* Les murs, avec leurs baies, versés dans la grille. Chaque mur se compose dans
 * SON masque avant d'être versé : deux déclarations de mur suivent le même tracé
 * dans cette salle, et percer directement dans la grille ouvrirait la baie de
 * l'une dans le plein de l'autre. */
static void reach_add_walls(reach_grid *g, const rg_builder *b, size_t *blind_openings)
{
    for (size_t w = 0; w < b->wall_plan_count; ++w) {
        const rg_wall_plan *p = &b->wall_plans[w];
        if (p->count < 2) continue;
        const size_t nseg = p->closed ? p->count : p->count - 1;
        const float half = p->thickness * 0.5f;

        unsigned char *mask = reach_mask_new(g);

        float cum[RG_MAX_WALL_POINTS + 1];
        cum[0] = 0.0f;
        for (size_t i = 0; i < nseg; ++i) {
            const ns_v2 a = p->points[i];
            const ns_v2 c = p->points[(i + 1) % p->count];
            cum[i + 1] = cum[i] + ns_v2_len(ns_v2_sub(c, a));

            /* On prolonge d'une demi-épaisseur aux JOINTURES, où le mur a un coin
             * d'onglet plein, et de rien du tout aux extrémités libres — où il
             * s'arrête vraiment, et où prolonger rétrécirait un passage de dix
             * centimètres pour rien. */
            const float cap_a = (p->closed || i > 0)        ? half : 0.0f;
            const float cap_b = (p->closed || i + 1 < nseg) ? half : 0.0f;
            reach_mask_segment(g, mask, a, c, half, cap_a, cap_b);
        }

        for (size_t k = 0; k < p->opening_count; ++k) {
            const geo_opening *o = &p->openings[k];

            /* Une baie n'est un passage que si l'on y entre DEBOUT. Le reste est
             * une fenêtre, une trémie ou un passe-plat : cela laisse voir et
             * entendre, pas circuler. */
            if (o->sill > RG_REACH_LOW + 1e-4f || o->head < RG_REACH_HIGH - 1e-4f) {
                if (blind_openings) (*blind_openings)++;
                continue;
            }

            for (size_t i = 0; i < nseg; ++i) {
                if (o->offset < cum[i] - 1e-4f
                 || o->offset + o->width > cum[i + 1] + 1e-4f) continue;
                const ns_v2 a = p->points[i];
                const ns_v2 c = p->points[(i + 1) % p->count];
                const ns_v2 dir = ns_v2_norm(ns_v2_sub(c, a));
                const float local = o->offset - cum[i];
                /* En travers, large : on est dans le masque de CE mur, et
                 * déborder n'y atteint rien d'autre. Dans la longueur, juste. */
                reach_mask_carve(g, mask,
                                 ns_v2_add(a, ns_v2_scale(dir, local)),
                                 ns_v2_add(a, ns_v2_scale(dir, local + o->width)),
                                 half + g->cell);
                break;
            }
        }

        reach_grid_add(g, mask);
        reach_mask_free(mask);
    }
}

/* Les solides, au TRIANGLE, sur la seule tranche que le corps occupe. La boîte
 * englobante aurait suffi pour un caisson droit ; elle enfle un meuble pivoté de
 * la moitié de sa diagonale, et un couloir se referme vite comme ça. */
static void reach_add_solids(reach_grid *g, const rg_builder *b)
{
    const gltf_vertex *verts = (const gltf_vertex *)b->verts.data;

    for (size_t i = 0; i < b->solid_count; ++i) {
        const rg_solid *s = &b->solids[i];
        if (s->tri_count == 0) continue;
        if (s->bounds.max.y <= RG_REACH_LOW)  continue;   /* un tapis, une flaque */
        if (s->bounds.min.y >= RG_REACH_HIGH) continue;   /* une poutre, un néon */

        const geo_primitives *prim =
            &TOOL_VEC_AT(&b->prim_blocks, geo_primitives, s->prim_block);
        for (size_t k = 0; k < s->tri_count; ++k) {
            const uint32_t *t = prim->storage + k * 3;
            const float y0 = verts[t[0]].position[1];
            const float y1 = verts[t[1]].position[1];
            const float y2 = verts[t[2]].position[1];
            float lo = y0, hi = y0;
            if (y1 < lo) lo = y1;
            if (y2 < lo) lo = y2;
            if (y1 > hi) hi = y1;
            if (y2 > hi) hi = y2;
            if (hi <= RG_REACH_LOW || lo >= RG_REACH_HIGH) continue;

            reach_grid_block_triangle(g,
                ns_v2_make(verts[t[0]].position[0], verts[t[0]].position[2]),
                ns_v2_make(verts[t[1]].position[0], verts[t[1]].position[2]),
                ns_v2_make(verts[t[2]].position[0], verts[t[2]].position[2]));
        }
    }
}

/* Hors des contours fermés, il n'y a pas de sol : il y a la rue. Sans cette
 * passe, le remplissage sortirait par la porte extérieure et mesurerait le
 * trottoir — un grand chiffre parfaitement faux. */
/*
 * « DANS LE BÂTIMENT » N'EST PAS « DANS UN CONTOUR », et la différence a coûté
 * un faux positif à ce contrôle.
 *
 * Le hall et le bloc sanitaire s'aboutent, et leurs lignes MÉDIANES ne
 * coïncident pas : celle du hall est à x = 7,215, celle du bloc à x = 7,279. La
 * bande de 6,4 cm entre les deux — une cellule et demie au pas de 5 cm — n'est
 * strictement dans aucun des deux polygones. Bouchée, elle refermait la baie de
 * 3,42 m qui relie les deux pièces, et C-10 déclarait le bloc sanitaire
 * inatteignable alors qu'on y entre de plain-pied. Deux pièces mitoyennes ne
 * pouvaient structurellement jamais communiquer.
 *
 * La règle juste vit dans `reach_grid_close_outside`, où elle s'éprouve sur deux
 * pièces fabriquées ; ici il ne reste qu'à lui passer les contours fermés.
 */
static void reach_close_outside(reach_grid *g, const rg_builder *b)
{
    reach_contour contours[RG_MAX_SHELLS];
    for (size_t c = 0; c < b->shell_count; ++c) {
        const rg_wall_plan *sh = &b->wall_plans[b->shells[c]];
        contours[c].points = sh->points;
        contours[c].count = sh->count;
        contours[c].thickness = sh->thickness;
    }
    reach_grid_close_outside(g, contours, b->shell_count);
}

static void check_reachable(const tool_json *doc, const tool_json_value *root,
                            const rg_builder *b)
{
    const tool_json_value *start = tool_json_get(doc, root, "playerStart");
    if (!start) {
        tool_warnf("la description ne déclare pas « playerStart » : le contrôle "
                   "d'accessibilité n'a rien vérifié. C'est le pire état d'un "
                   "contrôle.");
        return;
    }
    if (b->shell_count == 0) {
        tool_warnf("aucun contour fermé dans « walls » : le contrôle "
                   "d'accessibilité ne saurait pas où s'arrête le bâtiment, et "
                   "mesurerait la rue. Rien n'a été vérifié.");
        return;
    }

    float pmin[3], pmax[3];
    const tool_json_value *playable = tool_json_get(doc, tool_json_get(doc, root, "room"),
                                                    "playable");
    if (!playable) tool_fatalf("la description ne déclare pas room.playable");
    tool_json_get_vec3(doc, playable, "min", pmin, 0.0f);
    tool_json_get_vec3(doc, playable, "max", pmax, 0.0f);

    /* L'emprise de la grille : l'emprise jouable, tous les plans de murs et tous
     * les solides. Large plutôt que juste — un mur laissé hors du cadre serait un
     * mur qui ne barre rien, et la fuite passerait par là. */
    float mnx = pmin[0], mxx = pmax[0], mnz = pmin[2], mxz = pmax[2];
    for (size_t w = 0; w < b->wall_plan_count; ++w) {
        const rg_wall_plan *p = &b->wall_plans[w];
        for (size_t i = 0; i < p->count; ++i) {
            if (p->points[i].x - p->thickness < mnx) mnx = p->points[i].x - p->thickness;
            if (p->points[i].x + p->thickness > mxx) mxx = p->points[i].x + p->thickness;
            if (p->points[i].y - p->thickness < mnz) mnz = p->points[i].y - p->thickness;
            if (p->points[i].y + p->thickness > mxz) mxz = p->points[i].y + p->thickness;
        }
    }
    for (size_t i = 0; i < b->solid_count; ++i) {
        const ns_aabb *bb = &b->solids[i].bounds;
        if (bb->min.x < mnx) mnx = bb->min.x;
        if (bb->max.x > mxx) mxx = bb->max.x;
        if (bb->min.z < mnz) mnz = bb->min.z;
        if (bb->max.z > mxz) mxz = bb->max.z;
    }

    reach_grid grid;
    reach_grid_init(&grid, mnx - 0.5f, mnz - 0.5f, mxx + 0.5f, mxz + 0.5f, RG_REACH_CELL);

    size_t blind_openings = 0;
    reach_add_walls(&grid, b, &blind_openings);
    reach_add_solids(&grid, b);
    reach_close_outside(&grid, b);
    reach_grid_solve(&grid, RG_BODY_RADIUS);

    /*
     * LE PLAN, sur demande.
     *
     * « 33 cibles inatteignables » dit qu'il y a un trou, pas où il est. Trente
     * secondes de regard sur un plan des poches valent une heure de lecture de
     * coordonnées — c'est ainsi que la géométrie de ce contrôle a été vérifiée
     * avant d'être crue. Un PGM parce que c'est six lignes et aucune dépendance ;
     * la moindre visionneuse l'ouvre.
     */
    {
        const char *dump = getenv("NINETEEN_ACCESSIBILITE_PGM");
        FILE *img = (dump && dump[0]) ? fopen(dump, "wb") : NULL;
        if (dump && dump[0] && !img) tool_warnf("impossible d'écrire %s", dump);
        if (img) {
            fprintf(img, "P2\n%d %d\n255\n", grid.nx, grid.nz);
            /* Z décroissant : le nord en haut, comme sur le plan de 2020. */
            for (int iz = grid.nz - 1; iz >= 0; --iz) {
                for (int ix = 0; ix < grid.nx; ++ix) {
                    const size_t k = reach_index(&grid, ix, iz);
                    int shade = 0;                                   /* plein */
                    if (!grid.solid[k]) {
                        shade = grid.walkable[k]
                              ? 60 + (grid.label[k] * 47) % 190      /* une teinte par poche */
                              : 30;                                  /* le vide où le corps ne tient pas */
                    }
                    fprintf(img, "%d ", shade);
                }
                fprintf(img, "\n");
            }
            fclose(img);
            tool_infof("plan des poches écrit dans %s", dump);
        }
    }

    /* ------------------------------------------------------------------ */
    /* Le départ                                                          */
    /* ------------------------------------------------------------------ */
    float sp[3];
    tool_json_get_vec3(doc, start, "position", sp, 0.0f);
    const int home = reach_grid_label_at(&grid, sp[0], sp[2]);
    if (home < 0) {
        tool_fatalf("« playerStart » est posé là où le CORPS du joueur ne tient "
                    "pas (%.3f, %.3f).\n"
                    "  Il faut %.2f m de dégagement autour du point de départ — "
                    "c'est le rayon de la capsule que `room_camera_tick` fait "
                    "glisser contre la géométrie.\n"
                    "  Le joueur naîtrait dans un mur, ou coincé contre lui : la "
                    "première image du jeu serait une texture vue de trop près.",
                    (double)sp[0], (double)sp[2], (double)RG_BODY_RADIUS);
    }

    /* ------------------------------------------------------------------ */
    /* Les cibles                                                         */
    /* ------------------------------------------------------------------ */
    rg_reach_target targets[RG_MAX_REACH_TARGETS];
    size_t target_count = 0;

    for (size_t i = 0; i < b->cabinet_count && target_count < RG_MAX_REACH_TARGETS; ++i) {
        rg_reach_target *t = &targets[target_count++];
        memset(t, 0, sizeof *t);
        t->kind = "borne";
        snprintf(t->name, sizeof t->name, "%.63s", b->cabinets[i].name);
        /* Le POINT DE JEU, pas le meuble : c'est là que la collision arrête le
         * joueur, et c'est le seul endroit d'où la borne se joue. Une borne dont
         * la façade est atteignable mais pas la place devant elle est un meuble
         * qui montre une image. */
        t->x = b->cabinets[i].player_anchor[0];
        t->z = b->cabinets[i].player_anchor[2];
    }

    const tool_json_value *views = tool_json_get(doc, root, "captures");
    const int view_count = tool_json_array_count(doc, views);
    for (int i = 0; i < view_count && target_count < RG_MAX_REACH_TARGETS; ++i) {
        const tool_json_value *e = tool_json_at(doc, views, i);
        /* Une orbite tourne AUTOUR d'un centre et ne s'y tient jamais : celle de
         * la salle est calée sur la borne centrale, donc dans un meuble. */
        if (tool_json_get_bool(doc, e, "orbit", false)) continue;
        rg_reach_target *t = &targets[target_count++];
        memset(t, 0, sizeof *t);
        t->kind = "point de vue";
        tool_json_get_string(doc, e, "name", t->name, sizeof t->name);
        float p[3];
        tool_json_get_vec3(doc, e, "position", p, 0.0f);
        t->x = p[0];
        t->z = p[2];
    }

    const tool_json_value *zones = tool_json_get(doc, root, "soundZones");
    const int zone_count = tool_json_array_count(doc, zones);
    for (int i = 0; i < zone_count && target_count < RG_MAX_REACH_TARGETS; ++i) {
        const tool_json_value *e = tool_json_at(doc, zones, i);
        rg_reach_target *t = &targets[target_count++];
        memset(t, 0, sizeof *t);
        t->kind = "zone sonore";
        tool_json_get_string(doc, e, "name", t->name, sizeof t->name);
        float mn[3], mx[3];
        tool_json_get_vec3(doc, e, "min", mn, 0.0f);
        tool_json_get_vec3(doc, e, "max", mx, 0.0f);
        t->is_box = true;
        t->min_x = mn[0]; t->min_z = mn[2];
        t->max_x = mx[0]; t->max_z = mx[2];
        t->x = (mn[0] + mx[0]) * 0.5f;
        t->z = (mn[2] + mx[2]) * 0.5f;
    }

    size_t unreachable = 0;
    for (size_t i = 0; i < target_count; ++i) {
        const rg_reach_target *t = &targets[i];
        float hx = t->x, hz = t->z;
        const int label = t->is_box
            ? reach_grid_label_in_box(&grid, t->min_x, t->min_z, t->max_x, t->max_z, &hx, &hz)
            : reach_grid_label_at(&grid, t->x, t->z);
        if (label == home) continue;

        unreachable++;
        if (label < 0) {
            fprintf(stderr, "  ✗ %s « %s » : AUCUNE cellule foulable %s "
                            "(%.2f, %.2f)%s\n",
                    t->kind, t->name[0] ? t->name : "?",
                    t->is_box ? "dans la zone" : "à ce point",
                    (double)t->x, (double)t->z,
                    t->is_box ? " au centre de la zone" : "");
        } else {
            float e_min_x, e_min_z, e_max_x, e_max_z;
            reach_grid_extent(&grid, label, &e_min_x, &e_min_z, &e_max_x, &e_max_z);
            fprintf(stderr, "  ✗ %s « %s » : foulable en (%.2f, %.2f), mais dans "
                            "une AUTRE poche que le départ — %.1f m², de "
                            "(%.2f, %.2f) à (%.2f, %.2f)\n",
                    t->kind, t->name[0] ? t->name : "?", (double)hx, (double)hz,
                    (double)reach_grid_area(&grid, label),
                    (double)e_min_x, (double)e_min_z, (double)e_max_x, (double)e_max_z);
        }
    }

    /* ------------------------------------------------------------------ */
    /* Ce qui n'est pas une erreur, et qui aurait fait gagner une journée  */
    /* ------------------------------------------------------------------ */
    const float home_area = reach_grid_area(&grid, home);
    const float playable_area = (pmax[0] - pmin[0]) * (pmax[2] - pmin[2]);
    const float share = playable_area > 1e-3f ? home_area / playable_area : 0.0f;

    printf("  accessibilité : %.1f m² atteignables depuis le départ, soit %.1f %% "
           "de l'emprise jouable déclarée (%.1f m²)\n",
           (double)home_area, (double)(share * 100.0f), (double)playable_area);
    printf("  %d poche(s) foulable(s) dans le bâtiment", grid.components);
    if (grid.components > 1) {
        printf(" — le départ est dans la n° %d :\n", home + 1);
        for (int c = 0; c < grid.components; ++c) {
            const float area = reach_grid_area(&grid, c);
            /* Sous un demi-mètre carré, c'est le creux derrière un canapé, pas
             * une pièce : les lister toutes noierait celle qui compte. */
            if (area < 0.5f && c != home) continue;
            float e_min_x, e_min_z, e_max_x, e_max_z;
            reach_grid_extent(&grid, c, &e_min_x, &e_min_z, &e_max_x, &e_max_z);
            printf("      n° %d%s : %6.1f m², de (%.2f, %.2f) à (%.2f, %.2f)\n",
                   c + 1, c == home ? " (départ)" : "        ", (double)area,
                   (double)e_min_x, (double)e_min_z, (double)e_max_x, (double)e_max_z);
        }
    } else {
        printf("\n");
    }
    if (blind_openings) {
        printf("  %zu baie(s) ne sont pas des passages (allège trop haute ou "
               "linteau trop bas pour le corps) — vues, pas franchies\n",
               blind_openings);
    }

    /* ------------------------------------------------------------------ */
    /* Verdict                                                            */
    /* ------------------------------------------------------------------ */
    const bool starved = share < RG_REACH_MIN_SHARE;
    if (starved) {
        fprintf(stderr, "  ✗ l'aire atteignable ne fait que %.1f %% de l'emprise "
                        "jouable, pour %.0f %% attendus au minimum\n",
                (double)(share * 100.0f), (double)(RG_REACH_MIN_SHARE * 100.0f));
    }

    if (unreachable || starved) {
        /* Le grief, dit dans les termes de ce qui a été constaté — et jamais
         * « 0 cible(s) inatteignable(s) », qui est ce qu'écrit un message
         * assemblé au lieu d'être choisi. Une salle peut être un cul-de-sac sans
         * qu'aucune cible n'y soit : c'est précisément le sas, qui ne contenait
         * ni borne, ni point de vue, ni rien à atteindre. */
        char grief[192];
        if (unreachable && starved) {
            snprintf(grief, sizeof grief,
                     "%zu cible(s) inatteignable(s) depuis « playerStart », et "
                     "l'aire atteignable est indigente", unreachable);
        } else if (unreachable) {
            snprintf(grief, sizeof grief,
                     "%zu cible(s) inatteignable(s) depuis « playerStart »",
                     unreachable);
        } else {
            snprintf(grief, sizeof grief,
                     "le joueur est dans un cul-de-sac : %.1f m² atteignables, "
                     "soit %.1f %% de l'emprise jouable",
                     (double)home_area, (double)(share * 100.0f));
        }

        const char *plea =
            "Une salle où l'on ne peut pas ALLER n'est pas une salle : c'est une "
            "boîte avec des images dessus.\n"
            "  Ce qui manque est presque toujours une BAIE — un contour fermé sans "
            "ouverture est un mur tout autour, et rien d'autre ne le dit.";
        if (reach_warn_only()) {
            tool_warnf("%s — non fatal par NINETEEN_ACCESSIBILITE. %s", grief, plea);
        } else {
            tool_fatalf("%s.\n  %s\n"
                        "  Pour voir OÙ sont les poches plutôt que de le déduire : "
                        "NINETEEN_ACCESSIBILITE_PGM=/tmp/salle.pgm\n"
                        "  Pendant une transition, et sur SA machine seulement : "
                        "NINETEEN_ACCESSIBILITE=avertissement.",
                        grief, plea);
        }
    } else {
        printf("  %zu cible(s) atteignable(s) à pied depuis le départ : "
               "%zu borne(s), les points de vue et les zones sonores\n",
               target_count, b->cabinet_count);
    }

    reach_grid_release(&grid);
}

/* Comme `material_index`, mais rend −1 au lieu d'arrêter l'outil : pour les
 * matériaux facultatifs, dont l'absence a un repli sensé. */
static int material_index_opt(const rg_builder *b, const char *name)
{
    if (!name || !name[0]) return -1;
    for (size_t i = 0; i < b->material_count; ++i) {
        if (strcmp(b->material_names[i], name) == 0) return (int)i;
    }
    return -1;
}

static int material_index(const rg_builder *b, const char *name, const char *used_by)
{
    if (!name || !name[0]) return -1;
    for (size_t i = 0; i < b->material_count; ++i) {
        if (strcmp(b->material_names[i], name) == 0) return (int)i;
    }
    tool_fatalf("« %s » réclame le matériau « %s », qui n'est pas déclaré",
                used_by, name);
    return -1;
}

/* Le paramétrage d'UV suit le matériau, pas l'objet : c'est une propriété de la
 * texture (une moquette boucle tous les 1,5 m, une dalle tous les 0,60 m), et la
 * répéter sur chaque objet finirait par diverger. Un objet peut malgré tout la
 * surcharger quand sa géométrie l'exige. */
static geo_uv material_uv(const rg_builder *b, int mat)
{
    if (mat < 0 || (size_t)mat >= b->material_count) return geo_uv_tile(1.0f);
    if (b->material_fit[mat]) return geo_uv_fit();
    return geo_uv_tile(b->material_uv[mat]);
}

/* ========================================================================== */
/* Émission d'un objet                                                        */
/* ========================================================================== */

/*
 * Un objet nommé de la description devient un maillage glTF, et donc un
 * `ns_scene_object` côté moteur. La finition se fait **par objet** et pas sur le
 * pool global : après soudure, un mur et sa plinthe partagent des positions, et
 * un lissage global moyennerait leurs normales de part et d'autre de la frontière
 * de matériau — la plinthe cesserait de se détacher du mur.
 */
static void emit_object(rg_builder *b, const char *name, geo_mesh *m, float smooth_angle)
{
    if (geo_mesh_tri_count(m) == 0) {
        tool_fatalf("l'objet « %s » ne produit aucun triangle — `cgltf_validate` "
                    "refuserait l'accesseur vide au moment du BVH, dans un message "
                    "parlant d'octets", name);
    }

    geo_weld(m, 1e-4f);
    geo_smooth_normals(m, smooth_angle);
    geo_generate_tangents(m);

    const int degenerate = geo_check_degenerate(m, 1e-9f);
    if (degenerate > 0) {
        tool_fatalf("l'objet « %s » contient %d triangle(s) dégénéré(s) : normale et "
                    "tangente y sont indéfinies", name, degenerate);
    }

    const size_t base = b->verts.count;
    if (base + geo_mesh_vertex_count(m) > 0xFFFFFFFFu) {
        tool_fatalf("plus de 2^32 sommets — les indices sont en uint32");
    }
    tool_vec_reserve(&b->verts, base + geo_mesh_vertex_count(m));
    for (size_t i = 0; i < geo_mesh_vertex_count(m); ++i) {
        *(gltf_vertex *)tool_vec_push(&b->verts) = TOOL_VEC_AT(&m->verts, gltf_vertex, i);
    }

    geo_primitives prims;
    geo_build_primitives(m, &prims);
    for (size_t i = 0; i < geo_mesh_tri_count(m) * 3; ++i) prims.storage[i] += (uint32_t)base;
    *(geo_primitives *)tool_vec_push(&b->prim_blocks) = prims;

    gltf_mesh *mesh = (gltf_mesh *)tool_vec_push(&b->meshes);
    memset(mesh, 0, sizeof *mesh);
    snprintf(mesh->name, sizeof mesh->name, "%s", name);
    mesh->prims = prims.prims;
    mesh->prim_count = prims.count;

    b->triangle_count += geo_mesh_tri_count(m);
    b->last_vert_begin = base;
    b->last_vert_end   = b->verts.count;
    b->last_prim_block = b->prim_blocks.count - 1;
    b->last_tri_count  = geo_mesh_tri_count(m);
    b->last_bounds = geo_mesh_bounds(m);
    b->bounds = ns_aabb_union(b->bounds, b->last_bounds);

    if (b->meshes.count > RG_MAX_OBJECTS) {
        tool_fatalf("plus de %d objets : le moteur en tient %d (NS_MAX_OBJECTS)",
                    RG_MAX_OBJECTS, RG_MAX_OBJECTS);
    }
    if (b->triangle_count > RG_MAX_TRIANGLES) {
        tool_fatalf("budget de triangles dépassé à l'objet « %s » : %zu > %d",
                    name, b->triangle_count, RG_MAX_TRIANGLES);
    }

    geo_mesh_free(m);
}

/* ========================================================================== */
/* Matériaux                                                                  */
/* ========================================================================== */

static int texture_index(rg_builder *b, const char *file, const char *used_by)
{
    if (!file || !file[0]) return -1;
    for (size_t i = 0; i < b->texture_count; ++i) {
        if (strcmp(b->texture_storage[i], file) == 0) return (int)i;
    }
    if (b->texture_count >= RG_MAX_TEXTURES) {
        tool_fatalf("plus de %d textures distinctes", RG_MAX_TEXTURES);
    }

    /* Une texture nommée mais absente donnerait, à l'exécution, un substitut
     * procédural et un avertissement noyé dans le journal. La faute de frappe se
     * paie ici, au build, avec le nom du matériau fautif. */
    if (b->texture_dir_count) {
        bool found = false;
        for (size_t d = 0; d < b->texture_dir_count && !found; ++d) {
            char path[512];
            snprintf(path, sizeof path, "%s/%s", b->texture_dirs[d], file);
            FILE *f = fopen(path, "rb");
            if (f) { fclose(f); found = true; }
        }
        if (!found) {
            char dirs[1024] = { 0 };
            for (size_t d = 0; d < b->texture_dir_count; ++d) {
                snprintf(dirs + strlen(dirs), sizeof dirs - strlen(dirs),
                         "%s%s", d ? ", " : "", b->texture_dirs[d]);
            }
            tool_fatalf("le matériau « %s » réclame la texture « %s », absente de %s",
                        used_by, file, dirs);
        }
    }

    const size_t idx = b->texture_count++;
    snprintf(b->texture_storage[idx], sizeof b->texture_storage[idx], "%s", file);
    b->textures[idx] = b->texture_storage[idx];
    return (int)idx;
}

static void parse_materials(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "materials");
    const int count = tool_json_array_count(doc, list);
    if (count <= 0) tool_fatalf("la description ne déclare aucun matériau");

    for (int i = 0; i < count; ++i) {
        if (b->material_count >= RG_MAX_MATERIALS) {
            tool_fatalf("plus de %d matériaux", RG_MAX_MATERIALS);
        }
        const tool_json_value *e = tool_json_at(doc, list, i);
        const size_t slot = b->material_count++;

        /* Le nom transite par un tampon local : il est écrit à deux endroits du
         * même `rg_builder`, et le copier de l'un vers l'autre ferait croire à un
         * recouvrement (`-Wrestrict`) que le compilateur ne peut pas écarter. */
        char mat_name[64];
        tool_json_get_string(doc, e, "name", mat_name, sizeof mat_name);
        if (!mat_name[0]) tool_fatalf("le matériau numéro %d n'a pas de nom", i);
        for (size_t k = 0; k < slot; ++k) {
            if (strcmp(b->material_names[k], mat_name) == 0) {
                tool_fatalf("deux matériaux nommés « %s »", mat_name);
            }
        }
        snprintf(b->material_names[slot], sizeof b->material_names[slot], "%s", mat_name);

        gltf_material *m = &b->materials[slot];
        memset(m, 0, sizeof *m);
        snprintf(m->name, sizeof m->name, "%s", mat_name);

        char texture[128];
        tool_json_get_string(doc, e, "texture", texture, sizeof texture);
        m->texture = texture_index(b, texture, mat_name);

        tool_json_get_vec4(doc, e, "baseColor", m->base_color, 1.0f);
        m->metallic  = tool_json_get_float(doc, e, "metallic", 0.0f);
        m->roughness = tool_json_get_float(doc, e, "roughness", 0.85f);
        tool_json_get_vec3(doc, e, "emissive", m->emissive, 0.0f);
        m->emissive_strength = tool_json_get_float(doc, e, "emissiveStrength", 0.0f);

        b->material_uv[slot]  = tool_json_get_float(doc, e, "uvMetres", 1.0f);
        /* Le mode de pavage est **déclaré**, jamais deviné : `marbre_toilettes.jpg`
         * et les affiches sont des photos, pas des motifs, et les paver produirait
         * une couture visible tous les mètres. C'est exactement l'erreur que cette
         * reconstruction supprime. */
        b->material_fit[slot] = tool_json_get_bool(doc, e, "fit", false);
        tool_json_get_string(doc, e, "footstep", b->material_footstep[slot],
                             sizeof b->material_footstep[slot]);
    }
}

/* ========================================================================== */
/* Lecture des placements                                                     */
/* ========================================================================== */

typedef struct rg_repeat {
    int   count;
    ns_v3 step;
    float yaw_step;
} rg_repeat;

static rg_repeat read_repeat(const tool_json *doc, const tool_json_value *e)
{
    rg_repeat r = { 1, { 0, 0, 0 }, 0.0f };
    const tool_json_value *rep = tool_json_get(doc, e, "repeat");
    if (!rep) return r;

    r.count = (int)tool_json_get_float(doc, rep, "count", 1.0f);
    if (r.count < 1) r.count = 1;
    float step[3];
    tool_json_get_vec3(doc, rep, "step", step, 0.0f);
    r.step = ns_v3_make(step[0], step[1], step[2]);
    r.yaw_step = tool_json_get_float(doc, rep, "yawStep", 0.0f);
    return r;
}

/* Nom d'un exemplaire : « pilier » seul quand il est unique, « pilier_3 » quand
 * la série en compte plusieurs. Chaque exemplaire est un objet distinct côté
 * moteur — c'est ce qui permettra de désigner *cette* borne-là. */
static void instance_name(char *out, size_t out_size, const char *base, int index, int count)
{
    if (count <= 1) snprintf(out, out_size, "%s", base);
    else            snprintf(out, out_size, "%s_%d", base, index + 1);
}

/* ========================================================================== */
/* Sols                                                                       */
/* ========================================================================== */

static void parse_floors(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "floors");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char name[GLTF_MAX_NAME];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        if (!name[0]) tool_fatalf("un sol sans nom (entrée %d)", i);

        char mat_name[64];
        tool_json_get_string(doc, e, "material", mat_name, sizeof mat_name);
        const int mat = material_index(b, mat_name, name);

        float centre[2], size[2];
        tool_json_get_vec2(doc, e, "centre", centre, 0.0f);
        if (tool_json_get_floats(doc, e, "size", size, 2, 0.0f) != 2) {
            tool_fatalf("le sol « %s » n'a pas de taille [x, z]", name);
        }
        const float y = tool_json_get_float(doc, e, "y", 0.0f);
        const bool up = tool_json_get_bool(doc, e, "faceUp", true);

        /* Une subdivision tous les deux mètres par défaut. Elle ne sert pas à la
         * silhouette mais à l'éclairage : le volumétrique et la remontée
         * bilatérale (A5) échantillonnent par sommet, et un sol de 22 m en deux
         * triangles ne leur donne aucun point d'appui. */
        const float pitch = tool_json_get_float(doc, e, "subdivMetres", 2.0f);
        const int nx = (int)ceilf(size[0] / ns_maxf(pitch, 0.1f));
        const int nz = (int)ceilf(size[1] / ns_maxf(pitch, 0.1f));

        geo_uv uv = material_uv(b, mat);
        const float over = tool_json_get_float(doc, e, "uvMetres", 0.0f);
        if (over > 0.0f) uv = geo_uv_tile(over);
        /* Le décalage d'UV suit le centre pour que deux sols voisins d'un même
         * matériau restent alignés — sinon la moquette a une couture à chaque
         * changement de dalle. */
        uv.offset_u = centre[0];
        uv.offset_v = centre[1];

        /* Gardé pour `check_grounded` : un objet posé au sol touche CE sol-là,
         * et l'outil est le seul à connaître les deux. */
        if (b->floor_count < RG_MAX_FLOORS && up) {
            rg_floor *f = &b->floors[b->floor_count++];
            snprintf(f->name, sizeof f->name, "%.63s", name);
            f->centre[0] = centre[0]; f->centre[1] = centre[1];
            f->size[0] = size[0]; f->size[1] = size[1];
            f->y = y;
        }

        geo_mesh m; geo_mesh_init(&m);
        geo_plane(&m, size[0], size[1], nx, nz, up, &uv, mat);

        geo_xform x = GEO_XFORM_IDENTITY;
        x.origin = ns_v3_make(centre[0], y, centre[1]);
        geo_mesh placed; geo_mesh_init(&placed);
        geo_mesh_append(&placed, &m, &x, -1);
        geo_mesh_free(&m);

        emit_object(b, name, &placed, RG_SMOOTH_HARD);
    }
}

/* ========================================================================== */
/* Murs                                                                       */
/* ========================================================================== */

static size_t read_plan_points(const tool_json *doc, const tool_json_value *e,
                               const char *key, ns_v2 *out, size_t max,
                               const char *owner)
{
    const tool_json_value *arr = tool_json_get(doc, e, key);
    const int count = tool_json_array_count(doc, arr);
    if (count <= 0) tool_fatalf("« %s » : pas de « %s »", owner, key);
    if ((size_t)count > max) {
        tool_fatalf("« %s » : %d points, maximum %zu", owner, count, max);
    }
    for (int i = 0; i < count; ++i) {
        const tool_json_value *p = tool_json_at(doc, arr, i);
        if (tool_json_array_count(doc, p) < 2) {
            tool_fatalf("« %s » : le point %d n'est pas un couple [x, z]", owner, i);
        }
        out[i] = ns_v2_make(tool_json_value_float(doc, tool_json_at(doc, p, 0), 0.0f),
                            tool_json_value_float(doc, tool_json_at(doc, p, 1), 0.0f));
    }
    return (size_t)count;
}

static void parse_walls(rg_builder *b, const tool_json *doc, const tool_json_value *root,
                        float default_height, float default_thickness)
{
    const tool_json_value *list = tool_json_get(doc, root, "walls");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char name[GLTF_MAX_NAME];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        if (!name[0]) tool_fatalf("un mur sans nom (entrée %d)", i);

        ns_v2 points[RG_MAX_WALL_POINTS];
        const size_t point_count = read_plan_points(doc, e, "points", points,
                                                    RG_MAX_WALL_POINTS, name);

        geo_opening openings[RG_MAX_WALL_OPENINGS];
        memset(openings, 0, sizeof openings);
        const tool_json_value *ops = tool_json_get(doc, e, "openings");
        const int op_count = tool_json_array_count(doc, ops);
        if (op_count > RG_MAX_WALL_OPENINGS) {
            tool_fatalf("le mur « %s » a %d baies, maximum %d",
                        name, op_count, RG_MAX_WALL_OPENINGS);
        }
        for (int k = 0; k < op_count; ++k) {
            const tool_json_value *o = tool_json_at(doc, ops, k);
            tool_json_get_string(doc, o, "name", openings[k].name, sizeof openings[k].name);
            openings[k].offset = tool_json_get_float(doc, o, "offset", 0.0f);
            openings[k].width  = tool_json_get_float(doc, o, "width", 0.0f);
            openings[k].sill   = tool_json_get_float(doc, o, "sill", 0.0f);
            openings[k].head   = tool_json_get_float(doc, o, "head", 2.10f);
        }

        char inner[64], outer[64], reveal[64];
        tool_json_get_string(doc, e, "materialInner", inner, sizeof inner);
        tool_json_get_string(doc, e, "materialOuter", outer, sizeof outer);
        tool_json_get_string(doc, e, "materialReveal", reveal, sizeof reveal);
        if (!outer[0])  snprintf(outer, sizeof outer, "%s", inner);
        if (!reveal[0]) snprintf(reveal, sizeof reveal, "%s", inner);

        geo_wall_desc d;
        memset(&d, 0, sizeof d);
        d.name = name;
        d.points = points;
        d.point_count = point_count;
        d.closed = tool_json_get_bool(doc, e, "closed", false);
        d.height = tool_json_get_float(doc, e, "height", default_height);
        d.thickness = tool_json_get_float(doc, e, "thickness", default_thickness);
        d.openings = openings;
        d.opening_count = (size_t)op_count;
        d.material_inner  = material_index(b, inner, name);
        d.material_outer  = material_index(b, outer, name);
        d.material_reveal = material_index(b, reveal, name);
        d.cap_top  = tool_json_get_bool(doc, e, "capTop", false);
        d.cap_ends = tool_json_get_bool(doc, e, "capEnds", true);

        d.uv = material_uv(b, d.material_inner);
        const float over = tool_json_get_float(doc, e, "uvMetres", 0.0f);
        if (over > 0.0f) d.uv = geo_uv_tile(over);

        /*
         * Le plan du mur, gardé pour les contrôles de placement.
         *
         * Le premier contour FERMÉ est l'enveloppe du bâtiment : les cloisons
         * intérieures sont des polylignes ouvertes, elles ne séparent pas un
         * dedans d'un dehors. On reconnaît donc la coquille à sa fermeture
         * plutôt qu'à son nom — un contrôle qui dépend d'un nom cesse de
         * fonctionner le jour où quelqu'un renomme.
         */
        if (b->wall_plan_count < RG_MAX_WALLS) {
            rg_wall_plan *w = &b->wall_plans[b->wall_plan_count];
            snprintf(w->name, sizeof w->name, "%.63s", name);
            for (size_t k = 0; k < point_count; ++k) w->points[k] = points[k];
            w->count = point_count;
            w->closed = d.closed;
            w->thickness = d.thickness;
            w->height = d.height;
            for (int k = 0; k < op_count; ++k) w->openings[k] = openings[k];
            w->opening_count = (size_t)op_count;
            if (d.closed && point_count >= 3) {
                /*
                 * TOUS les contours fermés comptent, pas seulement le premier.
                 *
                 * Il n'y en avait qu'un tant que le bâtiment était une seule
                 * pièce. Le plan de 2020 en a trois — le hall, le bloc sanitaire
                 * à l'est, le sas au nord-est — et un bâtiment a le droit d'avoir
                 * des annexes. Avec un seul contour retenu, le contrôle mesurait
                 * les poutres du hall contre le couloir du sas et les déclarait
                 * « 14,48 m dehors » : un verdict absurde, et le genre de faux
                 * positif qui fait désactiver un contrôle.
                 */
                if (b->shell_index < 0) b->shell_index = (int)b->wall_plan_count;
                if (b->shell_count < RG_MAX_SHELLS) {
                    b->shells[b->shell_count++] = (int)b->wall_plan_count;
                }
            }
            b->wall_plan_count++;
        }

        geo_mesh m; geo_mesh_init(&m);
        geo_wall_run(&m, &d);
        emit_object(b, name, &m, RG_SMOOTH_HARD);
    }
}

/* ========================================================================== */
/* Boîtes : piliers, poutres, plates-formes, socles                           */
/* ========================================================================== */

static void parse_boxes(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "boxes");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char base_name[96];
        tool_json_get_string(doc, e, "name", base_name, sizeof base_name);
        if (!base_name[0]) tool_fatalf("une boîte sans nom (entrée %d)", i);

        char mat_name[64];
        tool_json_get_string(doc, e, "material", mat_name, sizeof mat_name);
        const int mat = material_index(b, mat_name, base_name);

        float at[3], size[3];
        tool_json_get_vec3(doc, e, "at", at, 0.0f);
        if (tool_json_get_floats(doc, e, "size", size, 3, 0.0f) != 3) {
            tool_fatalf("la boîte « %s » n'a pas de taille [x, y, z]", base_name);
        }
        /* 12 mm par défaut : assez pour qu'un liseré se forme sur une arête vue de
         * près, assez peu pour ne pas arrondir la silhouette. */
        const float chamfer = tool_json_get_float(doc, e, "chamfer", 0.012f);
        const float yaw = tool_json_get_float(doc, e, "yaw", 0.0f);

        uint32_t faces = GEO_FACE_ALL;
        char mask[32];
        tool_json_get_string(doc, e, "faces", mask, sizeof mask);
        if (strcmp(mask, "sides") == 0)          faces = GEO_FACE_SIDES;
        else if (strcmp(mask, "noBottom") == 0)  faces = GEO_FACE_NO_BOTTOM;
        else if (mask[0] && strcmp(mask, "all") != 0) {
            tool_fatalf("la boîte « %s » demande le masque « %s », inconnu "
                        "(all, sides, noBottom)", base_name, mask);
        }

        geo_uv uv = material_uv(b, mat);
        const float over = tool_json_get_float(doc, e, "uvMetres", 0.0f);
        if (over > 0.0f) uv = geo_uv_tile(over);

        const rg_repeat rep = read_repeat(doc, e);
        for (int k = 0; k < rep.count; ++k) {
            geo_mesh m; geo_mesh_init(&m);
            geo_box(&m, ns_v3_make(size[0], size[1], size[2]), chamfer, faces, &uv, mat);

            geo_xform x = GEO_XFORM_IDENTITY;
            x.origin = ns_v3_make(at[0] + rep.step.x * (float)k,
                                  at[1] + rep.step.y * (float)k,
                                  at[2] + rep.step.z * (float)k);
            x.yaw = (yaw + rep.yaw_step * (float)k) * NS_DEG2RAD;

            geo_mesh placed; geo_mesh_init(&placed);
            geo_mesh_append(&placed, &m, &x, -1);
            geo_mesh_free(&m);

            char name[GLTF_MAX_NAME];
            instance_name(name, sizeof name, base_name, k, rep.count);
            emit_object(b, name, &placed, RG_SMOOTH_HARD);
            record_solid(b, name, RG_SOLID_BOX, doc, e);
        }
    }
}

/* ========================================================================== */
/* Moulures                                                                   */
/* ========================================================================== */

/*
 * Profils nommés plutôt que listes de points dans le JSON : une plinthe se décrit
 * par sa saillie et sa hauteur, pas par six coordonnées qu'il faudrait relire
 * pour comprendre de quoi il s'agit. Chacun est paramétré par [saillie, hauteur].
 *
 * L'axe x du profil pointe vers l'intérieur de la pièce — c'est `cross(+Y, T)`,
 * qui coïncide avec le « gauche » de `geo_wall_run`. Un chemin qui suit l'ordre
 * des points du mur pose donc la moulure du bon côté sans réglage.
 */
static size_t named_profile(const char *kind, float depth, float height,
                            ns_v2 *out, size_t max, const char *owner)
{
    size_t n = 0;
    #define P(px, py) do { if (n >= max) tool_fatalf("profil trop long"); \
                           out[n++] = ns_v2_make((px), (py)); } while (0)

    if (strcmp(kind, "plinthe") == 0) {
        /* Plinthe : pied droit, petit congé, arase. */
        P(0.0f, 0.0f);
        P(depth, 0.0f);
        P(depth, height * 0.78f);
        P(depth * 0.55f, height * 0.92f);
        P(depth * 0.55f, height);
        P(0.0f, height);
    } else if (strcmp(kind, "corniche") == 0) {
        /* Corniche : la hauteur se compte **vers le bas** depuis le chemin, qui
         * court au niveau du plafond. */
        P(0.0f, 0.0f);
        P(0.0f, -height);
        P(depth * 0.30f, -height * 0.86f);
        P(depth * 0.78f, -height * 0.34f);
        P(depth, 0.0f);
    } else if (strcmp(kind, "cimaise") == 0) {
        P(0.0f, 0.0f);
        P(depth, height * 0.22f);
        P(depth, height * 0.72f);
        P(depth * 0.42f, height);
        P(0.0f, height);
    } else if (strcmp(kind, "nez") == 0) {
        /* Nez de marche : déborde de la plate-forme et retombe. */
        P(0.0f, 0.0f);
        P(depth, 0.0f);
        P(depth, height * 0.55f);
        P(depth * 0.45f, height);
        P(0.0f, height);
    } else {
        tool_fatalf("« %s » demande le profil « %s », inconnu "
                    "(plinthe, corniche, cimaise, nez)", owner, kind);
    }
    #undef P
    return n;
}

/* Le sens d'un profil décide de celui de ses normales. Plutôt que d'exiger de
 * l'auteur du profil qu'il le liste dans le bon ordre — une règle qu'on oublie et
 * dont l'erreur ne se voit pas, le rendu n'éliminant pas les faces arrière — on
 * mesure l'aire signée et on retourne si besoin. */
static void ensure_ccw(ns_v2 *pts, size_t count)
{
    double area = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const ns_v2 a = pts[i], b = pts[(i + 1) % count];
        area += (double)a.x * b.y - (double)b.x * a.y;
    }
    if (area >= 0.0) return;
    for (size_t i = 0; i < count / 2; ++i) {
        const ns_v2 tmp = pts[i];
        pts[i] = pts[count - 1 - i];
        pts[count - 1 - i] = tmp;
    }
}

#define RG_MAX_PROFILE_POINTS 32

static void parse_mouldings(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "mouldings");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char name[GLTF_MAX_NAME];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        if (!name[0]) tool_fatalf("une moulure sans nom (entrée %d)", i);

        char mat_name[64];
        tool_json_get_string(doc, e, "material", mat_name, sizeof mat_name);
        const int mat = material_index(b, mat_name, name);

        char kind[32];
        tool_json_get_string(doc, e, "profile", kind, sizeof kind);
        float section[2];
        if (tool_json_get_floats(doc, e, "section", section, 2, 0.0f) != 2) {
            tool_fatalf("la moulure « %s » n'a pas de section [saillie, hauteur]", name);
        }

        ns_v2 profile[RG_MAX_PROFILE_POINTS];
        size_t profile_count = named_profile(kind, section[0], section[1],
                                             profile, RG_MAX_PROFILE_POINTS, name);
        ensure_ccw(profile, profile_count);

        ns_v2 plan[RG_MAX_WALL_POINTS];
        const size_t plan_count = read_plan_points(doc, e, "points", plan,
                                                   RG_MAX_WALL_POINTS, name);
        const bool closed = tool_json_get_bool(doc, e, "closed", false);
        const float y = tool_json_get_float(doc, e, "y", 0.0f);
        /* Décalage perpendiculaire, positif vers la gauche de la marche — donc
         * vers l'intérieur quand le chemin reprend les points du mur. La demi-
         * épaisseur du mur pose la moulure exactement sur sa face. */
        const float offset = tool_json_get_float(doc, e, "offset", 0.0f);

        if (plan_count < 2 || (closed && plan_count < 3)) {
            tool_fatalf("la moulure « %s » : %zu point(s)", name, plan_count);
        }

        ns_v3 path[RG_MAX_WALL_POINTS];
        for (size_t k = 0; k < plan_count; ++k) {
            /* Le décalage se prend sur la direction sortante du point, ce qui
             * suffit : `geo_profile_extrude` remitre ensuite le repère. */
            const size_t next = (k + 1 < plan_count) ? k + 1 : (closed ? 0 : k);
            const size_t from = (next == k) ? k - 1 : k;
            const size_t to   = (next == k) ? k     : next;
            ns_v2 dir = ns_v2_norm(ns_v2_sub(plan[to], plan[from]));
            const ns_v2 left = ns_v2_make(dir.y, -dir.x);
            const ns_v2 p = ns_v2_add(plan[k], ns_v2_scale(left, offset));
            path[k] = ns_v3_make(p.x, y, p.y);
        }

        geo_uv uv = material_uv(b, mat);
        const float over = tool_json_get_float(doc, e, "uvMetres", 0.0f);
        if (over > 0.0f) uv = geo_uv_tile(over);

        geo_mesh m; geo_mesh_init(&m);
        geo_profile_extrude(&m, profile, profile_count, true,
                            path, plan_count, closed, &uv, mat);
        emit_object(b, name, &m, RG_SMOOTH_PROFILE);
    }
}

/* ========================================================================== */
/* Bornes d'arcade                                                            */
/* ========================================================================== */

/*
 * La borne mérite un générateur à elle : elle est répétée dix-neuf fois, elle
 * porte l'écran — la seule surface du décor dont la géométrie doit être exacte,
 * puisqu'un jeu s'y affichera — et sa silhouette est ce qui fait lire la salle
 * comme une salle d'arcade.
 *
 * **Cotes réelles**, et c'est un changement visible qu'il vaut mieux annoncer que
 * laisser découvrir : 0,72 x 1,86 x 0,88 m. Le modèle de 2020 mélangeait deux
 * échelles — ses bornes faisaient 2,39 m dans une salle de 2,92 m, soit des
 * bornes de géant ou un plafond de cave. On garde l'échelle de l'architecture et
 * on redimensionne les bornes ; elles paraîtront donc plus petites par rapport à
 * la salle qu'à l'origine, et le hall y gagne le dégagement demandé.
 *
 * Repère local : la borne regarde +Z, son socle est en Y = 0, et elle est centrée
 * en X. Le placement se fait ensuite par un lacet, comme pour tout le reste.
 */
#define RG_CAB_W  0.72f
#define RG_CAB_H  1.88f

/*
 * LES HAUTEURS DE LA BORNE, et pourquoi elles ont été RÉÉCRITES une par une.
 *
 * Trois versions, dont deux fausses. Elles sont toutes racontées ici parce que
 * la deuxième erreur vient directement de la façon dont j'avais corrigé la
 * première, et que la table seule ne le dirait pas.
 *
 * 1. A4 ramène les bornes de 2,39 m — la cote du modèle de 2020 — à 1,86 m, au
 *    motif que c'était la cote « réelle ». Le plan annonçait la conséquence
 *    (« les bornes paraîtront plus petites ») ; je l'ai écrite, livrée, et
 *    jamais mesurée.
 *
 * 2. Constat manette en main : l'œil est à 1,70 m, le centre de la dalle à
 *    1,26 m, et l'on se tient à 68 cm — soit 33° SOUS l'horizontale. Le champ
 *    de vision vertical vaut 62°, donc ±31° : le centre de la dalle tombait
 *    DEHORS, sous le bord bas de l'image. D'où « je suis obligé de
 *    m'accroupir » : à 1,31 m d'œil, l'écran revient au centre.
 *
 * 3. Ma première correction a été un facteur unique de 1,18 appliqué à toutes
 *    les cotes au-dessus du socle. Ça ramenait bien la plongée à 19°, et c'est
 *    FAUX quand même : la borne montait à 2,18 m — plus haute que n'importe
 *    quelle borne d'arcade ayant existé, les plus grandes plafonnant vers
 *    1,88 m — et le panneau de commande partait à 1,12 m, soit vingt
 *    centimètres au-dessus d'un plan de travail. On jouait sur une étagère.
 *    Corriger « on doit s'accroupir » en aggravant « les bornes ne ressemblent
 *    à rien » n'est pas une correction, c'est un échange.
 *
 * Ce que fait cette table-ci : elle garde des cotes de VRAIE borne — 1,88 m
 * hors tout, panneau de commande à 0,96 m (les uprights des années 80 sont
 * entre 0,91 et 0,97 m) — et remonte la DALLE À L'INTÉRIEUR du caisson, de
 * 1,26 m à 1,38 m. C'est la proportion des bornes réelles, où le centre de
 * l'écran est vers 73 % de la hauteur, contre 68 % chez nous.
 *
 * Ce que ça donne : 25° de plongée au lieu de 33°, donc la dalle est DANS le
 * cadre au lieu d'être sous lui, et avec les 15° d'inclinaison il reste 10°
 * hors axe. Ce n'est pas 0° : une borne d'arcade se regarde en baissant les
 * yeux, et c'est pour ça que la caméra vient se poser sur la dalle quand la
 * partie démarre (`room/main.c`) — c'est le mouvement de tête qu'on fait
 * devant une vraie borne, et c'est lui qui ferme la question, pas un caisson
 * qu'on étire.
 */

/* Le socle touche le sol : c'est du mobilier posé. */
#define RG_CAB_BASE 0.10f
#define RG_CAB_D  0.88f

/*
 * La dalle d'une borne a son PROPRE matériau, même quand deux bornes affichent
 * le même jeu.
 *
 * Sans ça, `ecran_snake` était un seul matériau partagé par les deux bornes
 * Snake — et comme le moteur allume un écran vivant en surchargeant un MATÉRIAU
 * (`ns_renderer_set_screen`), jouer sur l'une faisait apparaître la partie sur
 * l'autre, à l'autre bout de la salle. Sept matériaux d'écran sur dix-neuf
 * bornes étaient dans ce cas ; le défaut ne se voyait pas tant qu'un seul jeu
 * était porté, parce qu'il fallait deux bornes du même jeu dans le même cadre.
 *
 * Le clone garde la texture et les réglages : c'est la même image d'attente, ce
 * n'est plus la même surface.
 */
static int clone_screen_material(rg_builder *b, int src, const char *cabinet)
{
    if (src < 0) return src;
    if (b->material_count >= RG_MAX_MATERIALS) {
        tool_fatalf("plus de %d matériaux (clone d'écran pour « %s »)",
                    RG_MAX_MATERIALS, cabinet);
    }
    /* On copie la source AVANT d'écrire la destination : les deux vivent dans le
     * même tableau, et `snprintf` d'un tampon vers un autre du même objet est un
     * chevauchement que le compilateur signale à juste titre. */
    char src_name[64];
    snprintf(src_name, sizeof src_name, "%s", b->material_names[src]);
    char src_step[24];
    snprintf(src_step, sizeof src_step, "%s", b->material_footstep[src]);

    const size_t slot = b->material_count++;
    b->materials[slot] = b->materials[src];
    b->material_uv[slot] = b->material_uv[src];
    b->material_fit[slot] = b->material_fit[src];
    snprintf(b->material_footstep[slot], sizeof b->material_footstep[slot], "%s", src_step);
    snprintf(b->material_names[slot], sizeof b->material_names[slot], "%.40s@%.20s",
             src_name, cabinet);
    return (int)slot;
}

/* ======================================================================
 * La borne : un modèle importé, et quatre ancres qui lui survivent
 * ======================================================================
 * La borne était une extrusion de profil à neuf gradins, montée ici même. Elle
 * avait deux défauts que les vues de référence disent sans ambiguïté :
 *
 *   - **aucun jonc de chant.** Le T-molding — le bourrelet vif qui court sur
 *     tous les chants du caisson, l'arête avant, l'arête arrière, le pourtour
 *     du marquee — est le détail auquel on reconnaît une borne d'arcade avant
 *     même d'en lire la couleur. Il manquait entièrement.
 *   - **un décrochement droit sous le panneau de commande**, là où la
 *     référence descend en doucine : une gorge concave qui se redresse.
 *
 * Le reste — deux postes de jeu au lieu d'un, la porte à monnaie en saillie,
 * les grilles de haut-parleur en cercles concentriques — demandait des
 * primitives que `geo_shapes` n'a pas et n'a aucune raison d'avoir.
 *
 * La carrosserie vient donc de `assets/blender/borne.py`, exportée en glTF.
 * `roomgen` garde ce qui ne peut pas venir d'un modèle :
 *
 *   - **la DALLE**, parce qu'elle reçoit la texture de la partie au runtime et
 *     que son matériau est CLONÉ par borne — dix-neuf bornes, dix-neuf écrans ;
 *   - **les quatre ANCRES** (centre d'écran, grappe de boutons, fente à jetons,
 *     sommet du manche), dont dépendent l'IK des bras et le placement des
 *     mini-jeux ;
 *   - **la table des matériaux**, pour que les dix-neuf bornes gardent leur
 *     teinte par jeu avec un seul maillage.
 *
 * Ancres et dalle sont LUES dans `borne.ancres.json`, écrit par le même script
 * qui produit le glTF, depuis les mêmes cotes. Les recopier ici les ferait
 * dériver de la géométrie à la première retouche du modèle — c'est exactement
 * ce qui avait laissé un second monnayeur flotter 13 cm devant la borne.
 */
#define RG_BORNE_MODELE "models/borne/borne.gltf"
#define RG_BORNE_ANCRES "models/borne/borne.ancres.json"
#define RG_BORNE_MAT_MAX 16

typedef struct rg_borne_modele {
    bool  charge;
    int   material_count;
    char  material[RG_BORNE_MAT_MAX][64];
    float screen[3], screen_size[2], screen_tilt;
    float panel[3], coin[3], stick[3];
} rg_borne_modele;

/* Le modèle est le même pour les dix-neuf bornes : on le lit une fois. */
static const rg_borne_modele *borne_modele(const rg_builder *b, const char *owner)
{
    static rg_borne_modele m;
    if (m.charge) return &m;

    if (!b->asset_root[0]) {
        tool_fatalf("« %s » : la borne est un modèle importé et --assets= n'a "
                    "pas été donné — « %s » ne peut pas être résolu",
                    owner, RG_BORNE_ANCRES);
    }

    char path[768];
    snprintf(path, sizeof path, "%s/%s", b->asset_root, RG_BORNE_ANCRES);

    size_t size = 0;
    char *text = tool_read_file(path, &size);
    if (!text) {
        tool_fatalf("« %s » : %s introuvable. Le modèle se reconstruit par "
                    "« Blender --background --python assets/blender/borne.py "
                    "-- --out assets/models/borne/borne.gltf »", owner, path);
    }

    tool_json doc;
    tool_json_parse(&doc, text, size, path);
    const tool_json_value *root = tool_json_root(&doc);
    if (!root) tool_fatalf("%s : document vide", path);

    tool_json_get_vec3(&doc, root, "screen", m.screen, 0.0f);
    tool_json_get_vec2(&doc, root, "screenSize", m.screen_size, 0.0f);
    m.screen_tilt = tool_json_get_float(&doc, root, "screenTilt", 0.0f);
    tool_json_get_vec3(&doc, root, "panel", m.panel, 0.0f);
    tool_json_get_vec3(&doc, root, "coin", m.coin, 0.0f);
    tool_json_get_vec3(&doc, root, "stick", m.stick, 0.0f);

    if (m.screen_size[0] <= 0.0f || m.screen_size[1] <= 0.0f) {
        tool_fatalf("%s : dalle de %.3f x %.3f m — le modèle n'a pas déclaré "
                    "sa taille d'écran", path,
                    (double)m.screen_size[0], (double)m.screen_size[1]);
    }

    const tool_json_value *mats = tool_json_get(&doc, root, "materials");
    m.material_count = tool_json_array_count(&doc, mats);
    if (m.material_count <= 0 || m.material_count > RG_BORNE_MAT_MAX) {
        tool_fatalf("%s : %d matériaux déclarés, entre 1 et %d attendus",
                    path, m.material_count, RG_BORNE_MAT_MAX);
    }
    for (int i = 0; i < m.material_count; ++i) {
        tool_json_string_at(&doc, mats, i, m.material[i], sizeof m.material[i]);
    }

    tool_json_free(&doc);
    free(text);
    m.charge = true;
    return &m;
}

static void build_cabinet(rg_builder *b, geo_mesh *out, const tool_json *doc,
                          const tool_json_value *e, const char *owner,
                          rg_cab_anchors *anchors)
{
    const rg_borne_modele *mod = borne_modele(b, owner);

    char m_body[64], m_screen[64], m_marquee[64], m_panel[64], m_trim[64], m_side[64];
    tool_json_get_string(doc, e, "materialBody", m_body, sizeof m_body);
    tool_json_get_string(doc, e, "materialSide", m_side, sizeof m_side);
    tool_json_get_string(doc, e, "screen", m_screen, sizeof m_screen);
    tool_json_get_string(doc, e, "marquee", m_marquee, sizeof m_marquee);
    tool_json_get_string(doc, e, "materialPanel", m_panel, sizeof m_panel);
    tool_json_get_string(doc, e, "materialTrim", m_trim, sizeof m_trim);

    const int body   = material_index(b, m_body, owner);
    const int screen = clone_screen_material(b, material_index(b, m_screen, owner), owner);
    anchors->screen_material = screen;
    const int marq   = material_index(b, m_marquee[0] ? m_marquee : m_body, owner);
    const int panel  = material_index(b, m_panel[0] ? m_panel : m_body, owner);
    const int trim   = material_index(b, m_trim[0] ? m_trim : m_body, owner);
    /*
     * Le flanc porte la sérigraphie — le dégradé et la trame en losanges des
     * vues de référence — que le caisson n'a pas. Facultatif : sans
     * `materialSide`, il reprend la peinture du caisson.
     */
    const int flank  = material_index(b, m_side[0] ? m_side : m_body, owner);

    /*
     * Les matériaux facultatifs. Chacun retombe sur un voisin plausible : une
     * salle qui n'en déclare aucun donne une borne d'une seule teinte, laide
     * mais juste. C'est ce qui permet de les ajouter un par un sans casser une
     * description qui ne les connaît pas.
     */
    const int dark_i   = material_index_opt(b, "borne_noir");
    const int dark     = (dark_i >= 0) ? dark_i : trim;
    const int grille_i = material_index_opt(b, "borne_grille");
    const int grille   = (grille_i >= 0) ? grille_i : dark;
    /*
     * Le jonc de chant. Il retombe sur le CADRE et non sur le caisson : un
     * T-molding de la couleur du meuble n'est pas un T-molding, c'est une arête
     * — et l'arête est précisément ce qu'on cherchait à supprimer.
     */
    const int tmold_i  = material_index_opt(b, "borne_tmolding");
    const int tmold    = (tmold_i >= 0) ? tmold_i : trim;
    const int bleu_i   = material_index_opt(b, "borne_manche_bleu");
    const int rouge_i  = material_index_opt(b, "borne_manche_rouge");
    /*
     * Le CHROME, distinct du métal des grilles, et pour une raison mesurée :
     * une grille de haut-parleur est un métal SOMBRE — c'est ce qui la fait
     * lire comme une trame — et la tige du manche prenait ce même noir. Dans la
     * pénombre de l'allée elle disparaissait sous sa boule. Une tige de manche
     * est un tube d'acier poli ; elle doit accrocher la lumière.
     */
    const int chrome_i = material_index_opt(b, "borne_chrome");
    const int chrome   = (chrome_i >= 0) ? chrome_i : grille;

    /*
     * Les boutons sont VIFS, et pas du matériau du panneau. Ils l'étaient :
     * quatre pastilles bordeaux sur un panneau bordeaux, donc invisibles. Un
     * bouton d'arcade est en plastique brillant, et c'est exactement sa
     * fonction : se trouver sans être cherché.
     */
    const int btn_a = material_index(b, "bouton_rouge", owner);
    const int btn_b = material_index(b, "bouton_jaune", owner);

    /*
     * La correspondance modèle -> salle, PAR NOM et non par rang.
     *
     * `geo_import_gltf` associe les primitives par indice, et le script Blender
     * vérifie à l'export que l'ordre du fichier est bien celui qu'il annonce.
     * Ici on relit quand même les noms : un tableau d'indices muet se décale en
     * silence, et un jonc de chant couleur bouton ne casse aucun build — il
     * rend juste la salle fausse. Un nom inconnu, lui, arrête l'outil.
     */
    int32_t by_index[RG_BORNE_MAT_MAX];
    for (int i = 0; i < mod->material_count; ++i) {
        const char *n = mod->material[i];
        int32_t mi;
        if      (!strcmp(n, "caisson"))      mi = body;
        else if (!strcmp(n, "flanc"))        mi = flank;
        else if (!strcmp(n, "tmolding"))     mi = tmold;
        else if (!strcmp(n, "noir"))         mi = dark;
        else if (!strcmp(n, "grille"))       mi = grille;
        else if (!strcmp(n, "panneau"))      mi = panel;
        else if (!strcmp(n, "marquee"))      mi = marq;
        else if (!strcmp(n, "monnayeur"))    mi = dark;
        /* Les inserts rouges de la porte a monnaie. Ils partageaient
         * `bouton_a` : peindre les boutons d'action repeignait la porte a
         * monnaie. Facultatif, et il retombe sur le bouton d'action — ce qui
         * est exactement l'ancien comportement pour une salle qui ne le
         * declare pas. */
        else if (!strcmp(n, "insert")) {
            const int ins = material_index_opt(b, "borne_insert");
            mi = (ins >= 0) ? ins : btn_a;
        }
        else if (!strcmp(n, "bouton_a"))     mi = btn_a;
        else if (!strcmp(n, "bouton_b"))     mi = btn_b;
        else if (!strcmp(n, "manche_bleu"))  mi = (bleu_i  >= 0) ? bleu_i  : btn_b;
        else if (!strcmp(n, "manche_rouge")) mi = (rouge_i >= 0) ? rouge_i : btn_a;
        else if (!strcmp(n, "chrome"))       mi = chrome;
        else {
            tool_fatalf("« %s » : le modèle de borne déclare le matériau « %s », "
                        "que roomgen ne sait pas placer. Les deux listes sont "
                        "dans assets/blender/borne.py et ici même.", owner, n);
            return;
        }
        by_index[i] = mi;
    }

    char path[768];
    snprintf(path, sizeof path, "%s/%s", b->asset_root, RG_BORNE_MODELE);

    geo_xform mx = GEO_XFORM_IDENTITY;
    const size_t tri = geo_import_gltf(out, path, &mx, body, by_index,
                                       (size_t)mod->material_count, owner);
    b->imported_triangles += tri;

    /* --- la dalle ---------------------------------------------------------
     *
     * **La seule surface dont la géométrie doit être juste.** Elle est déclarée
     * au moteur, coordonnées comprises, et c'est elle qui reçoit l'image de la
     * partie en cours. Elle reste ICI, et non dans le glTF, pour deux raisons
     * qui n'ont rien d'esthétique : son matériau est cloné par borne — sans
     * quoi les dix-neuf écrans afficheraient la même chose — et sa position
     * doit être annoncée au moteur, ce qu'un maillage importé ne fait pas.
     *
     * 16:9, comme la cible de rendu 512 x 288 : l'image remplit la dalle
     * exactement, sans bande ni étirement.
     *
     * Les cotes viennent du modèle, pas d'ici : le script Blender les calcule
     * sur la face du cadre qu'il vient de construire, marge d'inclinaison
     * comprise. Écrites deux fois, elles finiraient par ne plus désigner le
     * même endroit.
     */
    const ns_v3 snormal = ns_v3_make(0.0f, sinf(mod->screen_tilt),
                                     cosf(mod->screen_tilt));
    geo_panel(out, ns_v3_make(mod->screen[0], mod->screen[1], mod->screen[2]),
              snormal, ns_v3_make(1, 0, 0),
              mod->screen_size[0], mod->screen_size[1],
              0.0f, 0.0f, 1.0f, 1.0f, screen);

    /* --- les ancres -------------------------------------------------------
     *
     * Quatre points qu'on TOUCHE, et qui ont tous la même sémantique : le
     * sommet de la boule, le dessus des pastilles, la fente, le centre de la
     * dalle. La pose des bras y ajoute la paume — c'est elle qui sait de quelle
     * longueur est une main. Il a fallu qu'ils n'aient pas cette sémantique
     * pour qu'on s'en aperçoive : une version donnait ici le point où se pose
     * le POIGNET, et la main gauche refermait ses doigts trois centimètres
     * au-dessus du manche.
     */
    anchors->screen_tilt = mod->screen_tilt;
    memcpy(anchors->screen,      mod->screen,      sizeof anchors->screen);
    memcpy(anchors->screen_size, mod->screen_size, sizeof anchors->screen_size);
    memcpy(anchors->panel,       mod->panel,       sizeof anchors->panel);
    memcpy(anchors->coin,        mod->coin,        sizeof anchors->coin);
    memcpy(anchors->stick,       mod->stick,       sizeof anchors->stick);
}

static void parse_cabinets(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "cabinets");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        /* 64 octets, la taille de `ns_cabinet.name` : un nom tronqué ici ne
         * correspondrait plus à celui du maillage glTF, et le moteur ne pourrait
         * plus relier la borne à sa géométrie — en silence. */
        char name[64];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        if (!name[0]) tool_fatalf("une borne sans nom (entrée %d)", i);
        if (b->cabinet_count >= RG_MAX_CABINETS) {
            tool_fatalf("plus de %d bornes : le moteur en tient %d (NS_MAX_CABINETS)",
                        RG_MAX_CABINETS, RG_MAX_CABINETS);
        }

        float at[3];
        tool_json_get_vec3(doc, e, "at", at, 0.0f);
        const float yaw_deg = tool_json_get_float(doc, e, "yaw", 0.0f);
        const float yaw = yaw_deg * NS_DEG2RAD;

        rg_cab_anchors anchors; memset(&anchors, 0, sizeof anchors);
        geo_mesh local; geo_mesh_init(&local);
        build_cabinet(b, &local, doc, e, name, &anchors);

        geo_xform x = GEO_XFORM_IDENTITY;
        x.origin = ns_v3_make(at[0], at[1], at[2]);
        x.yaw = yaw;

        geo_mesh placed; geo_mesh_init(&placed);
        geo_mesh_append(&placed, &local, &x, -1);
        geo_mesh_free(&local);
        emit_object(b, name, &placed, RG_SMOOTH_HARD);
        record_solid(b, name, RG_SOLID_CABINET, doc, e);

        /* Report du repère local vers le monde. Le lacet suit la convention de
         * `geo_xform` : X' = X cos + Z sin, Z' = -X sin + Z cos. */
        const float c = cosf(yaw), s = sinf(yaw);
        rg_cabinet *cab = &b->cabinets[b->cabinet_count];
        memset(cab, 0, sizeof *cab);
        snprintf(cab->name, sizeof cab->name, "%s", name);
        tool_json_get_string(doc, e, "game", cab->game, sizeof cab->game);
        tool_json_get_string(doc, e, "difficulty", cab->difficulty, sizeof cab->difficulty);
        if (!cab->game[0]) {
            tool_fatalf("la borne « %s » ne dit pas à quoi elle joue — c'est "
                        "précisément ce que le moteur devinait de travers", name);
        }
        cab->slot = (int)tool_json_get_float(doc, e, "slot", (float)b->cabinet_count);
        cab->attract = tool_json_get_bool(doc, e, "attract", true);

        cab->bounds_min[0] = b->last_bounds.min.x;
        cab->bounds_min[1] = b->last_bounds.min.y;
        cab->bounds_min[2] = b->last_bounds.min.z;
        cab->bounds_max[0] = b->last_bounds.max.x;
        cab->bounds_max[1] = b->last_bounds.max.y;
        cab->bounds_max[2] = b->last_bounds.max.z;

#define RG_TO_WORLD(dst, src)                                              \
        do {                                                               \
            (dst)[0] = at[0] + (src)[0] * c + (src)[2] * s;                \
            (dst)[1] = at[1] + (src)[1];                                   \
            (dst)[2] = at[2] - (src)[0] * s + (src)[2] * c;                \
        } while (0)

        RG_TO_WORLD(cab->screen_center, anchors.screen);
        RG_TO_WORLD(cab->panel_centre,  anchors.panel);
        RG_TO_WORLD(cab->coin_slot,     anchors.coin);
        RG_TO_WORLD(cab->stick_top,     anchors.stick);
#undef RG_TO_WORLD

        /*
         * LA NORMALE EXPORTÉE DOIT ÊTRE CELLE DE LA GÉOMÉTRIE.
         *
         * Elle était forcée à l'horizontale, alors que la dalle bascule de 10°
         * vers l'arrière depuis B14. La donnée annonçait donc un écran vertical
         * que le maillage n'a jamais eu — et tout ce qui s'appuie dessus (viser
         * la dalle, orienter un reflet, placer un joueur) travaillait sur un
         * plan qui n'existe pas.
         */
        const float ct = cosf(anchors.screen_tilt), st = sinf(anchors.screen_tilt);
        cab->screen_normal[0] = s * ct;
        cab->screen_normal[1] = st;
        cab->screen_normal[2] = c * ct;
        cab->screen_material = anchors.screen_material;
        cab->screen_size[0] = anchors.screen_size[0];
        cab->screen_size[1] = anchors.screen_size[1];

        /*
         * Où se plante le joueur : 46 cm devant le PANNEAU DE COMMANDE.
         *
         * L'ancre était dérivée de l'ÉCRAN, ce qui marchait tant que le caisson
         * était une boîte et que l'écran et le panneau étaient à la même
         * profondeur. Avec le profil en gradins, l'écran recule de 32 cm quand
         * le panneau n'en recule que 12 : dériver du premier plantait le joueur
         * beaucoup trop près, le nez dans la tôle.
         *
         * Le panneau est de toute façon la bonne référence : c'est ce qu'on
         * doit ATTEINDRE, et c'est lui qui décide de la distance à laquelle on
         * se tient. L'écran, lui, se regarde d'où l'on est.
         */
        /* Le joueur se plante à l'HORIZONTALE : on prend la direction du meuble
         * (s, c) et pas la normale de la dalle, qui pointe maintenant un peu
         * vers le haut et le reculerait de quelques centimètres. */
        /*
         * L'ABSCISSE vient de la borne, la DISTANCE vient du panneau.
         *
         * Les deux venaient du panneau, et c'était juste tant que `panel`
         * désignait le barycentre de six pastilles réparties autour de l'axe.
         * `panel` désigne maintenant le dessus du bouton d'ACTION, qui est à
         * 45 mm à droite : le joueur se serait planté 45 mm hors de l'axe de
         * l'écran qu'il regarde, pour être en face du bouton qu'il presse.
         *
         * On se plante devant une borne, pas devant son bouton. La distance,
         * elle, reste celle du panneau : c'est lui qu'il faut atteindre.
         */
        const float reach_z = cab->panel_centre[0] * s + cab->panel_centre[2] * c;
        const float axis_z  = at[0] * s + at[2] * c;
        const float depth   = reach_z - axis_z;   /* avancée du panneau sur l'axe */
        cab->player_anchor[0] = at[0] + s * (depth + 0.46f);
        cab->player_anchor[1] = at[1];
        cab->player_anchor[2] = at[2] + c * (depth + 0.46f);

        b->cabinet_count++;
    }
}

/* ========================================================================== */
/* Objets composés : mobilier, agencements, décor                             */
/* ========================================================================== */

/*
 * Un « prop » est un objet nommé fait de plusieurs morceaux, placé et pivoté
 * d'un bloc. Billard, comptoir, bureau, canapés, cabines de toilettes,
 * distributeur, jukebox, affiches, enseignes : tout cela est une composition de
 * boîtes et de panneaux, et n'a pas besoin d'un générateur dédié par meuble.
 *
 * Le compromis est assumé : un générateur par meuble donnerait de plus belles
 * formes, mais vingt générateurs paramétrés une seule fois chacun sont vingt
 * fois plus de code que de résultat. Les formes qui méritent vraiment un
 * générateur — la borne d'arcade, le plafond — en ont un.
 *
 * Les coordonnées des morceaux sont LOCALES au prop : on décrit le meuble à
 * l'origine, une fois, puis on le pose. C'est ce qui rend une description
 * relisable, et ce qui permet de déplacer un meuble sans recalculer dix lignes.
 */
static void emit_prop_parts(rg_builder *b, const tool_json *doc, const tool_json_value *e,
                            geo_mesh *out, const char *owner)
{
    /*
     * Un objet est SOIT un empilement de morceaux paramétriques, SOIT un modèle
     * importé — jamais les deux.
     *
     * Les deux voies partagent tout le reste : le nommage des instances, la
     * répétition, le point d'intérêt déclaré, l'enregistrement du solide et le
     * contrôle de chevauchement. C'est ce qui justifie de brancher ici plutôt
     * que d'ajouter une liste « models » à côté : le mobilier importé est du
     * mobilier, il n'a aucune raison d'avoir sa propre plomberie.
     */
    char model_src[192];
    tool_json_get_string(doc, e, "model", model_src, sizeof model_src);
    if (model_src[0]) {
        char mat_name[64];
        tool_json_get_string(doc, e, "material", mat_name, sizeof mat_name);
        if (!mat_name[0]) {
            tool_fatalf("« %s » : un modèle importé doit déclarer son \"material\" — "
                        "le glTF apporte sa géométrie et ses UV, la salle décide "
                        "de la matière", owner);
        }
        const int mat = material_index(b, mat_name, owner);

        /*
         * La correspondance de matériaux, primitive par primitive. Un poste de
         * radio a un corps et des haut-parleurs, un extincteur un corps, un verre
         * et une étiquette : sans cette table, tout l'objet prendrait la même
         * matière et l'on perdrait précisément ce qui distingue un vrai modèle
         * d'une boîte peinte.
         */
        int32_t by_index[8];
        size_t  by_index_count = 0;
        const tool_json_value *mats = tool_json_get(doc, e, "materials");
        const int mats_count = tool_json_array_count(doc, mats);
        if (mats_count > (int)(sizeof by_index / sizeof by_index[0])) {
            tool_fatalf("« %s » : %d matériaux déclarés, %zu au maximum",
                        owner, mats_count, sizeof by_index / sizeof by_index[0]);
        }
        for (int i = 0; i < mats_count; ++i) {
            char n[64];
            tool_json_string_at(doc, mats, i, n, sizeof n);
            by_index[by_index_count++] = n[0] ? material_index(b, n, owner) : -1;
        }

        if (!b->asset_root[0]) {
            tool_fatalf("« %s » : un modèle est demandé mais --assets= n'a pas été "
                        "donné — le chemin « %s » ne peut pas être résolu",
                        owner, model_src);
        }
        char path[768];
        snprintf(path, sizeof path, "%s/%s", b->asset_root, model_src);

        geo_xform mx = GEO_XFORM_IDENTITY;
        mx.scale = tool_json_get_float(doc, e, "scale", 1.0f);
        if (mx.scale <= 0.0f) {
            tool_fatalf("« %s » : échelle %.4f — nulle ou négative", owner, (double)mx.scale);
        }
        /* Le modèle peut avoir besoin de son propre calage vertical : les
         * modèles de Poly Haven posent leur origine au sol, mais un extincteur
         * s'accroche au mur. */
        float lift[3];
        tool_json_get_vec3(doc, e, "modelOffset", lift, 0.0f);
        mx.origin = ns_v3_make(lift[0], lift[1], lift[2]);
        mx.pitch = tool_json_get_float(doc, e, "modelPitch", 0.0f) * NS_DEG2RAD;
        mx.roll  = tool_json_get_float(doc, e, "modelRoll", 0.0f) * NS_DEG2RAD;

        const size_t tri = geo_import_gltf(out, path, &mx, mat,
                                           by_index_count ? by_index : NULL,
                                           by_index_count, owner);
        b->imported_triangles += tri;
        return;
    }

    const tool_json_value *parts = tool_json_get(doc, e, "parts");
    const int count = tool_json_array_count(doc, parts);
    if (count <= 0) {
        tool_fatalf("« %s » n'a ni morceaux (\"parts\") ni modèle (\"model\")", owner);
    }

    for (int i = 0; i < count; ++i) {
        const tool_json_value *p = tool_json_at(doc, parts, i);

        char mat_name[64];
        tool_json_get_string(doc, p, "material", mat_name, sizeof mat_name);
        const int mat = material_index(b, mat_name, owner);

        char type[16];
        tool_json_get_string(doc, p, "type", type, sizeof type);
        if (!type[0]) snprintf(type, sizeof type, "box");

        float at[3];
        tool_json_get_vec3(doc, p, "at", at, 0.0f);
        const float yaw   = tool_json_get_float(doc, p, "yaw", 0.0f) * NS_DEG2RAD;
        const float pitch = tool_json_get_float(doc, p, "pitch", 0.0f) * NS_DEG2RAD;
        const float roll  = tool_json_get_float(doc, p, "roll", 0.0f) * NS_DEG2RAD;

        geo_uv uv = material_uv(b, mat);
        const float over = tool_json_get_float(doc, p, "uvMetres", 0.0f);
        if (over > 0.0f) uv = geo_uv_tile(over);

        geo_mesh piece; geo_mesh_init(&piece);

        if (strcmp(type, "box") == 0) {
            float size[3];
            if (tool_json_get_floats(doc, p, "size", size, 3, 0.0f) != 3) {
                tool_fatalf("« %s », morceau %d : pas de taille [x, y, z]", owner, i);
            }
            uint32_t faces = GEO_FACE_ALL;
            char mask[16];
            tool_json_get_string(doc, p, "faces", mask, sizeof mask);
            if (strcmp(mask, "sides") == 0)         faces = GEO_FACE_SIDES;
            else if (strcmp(mask, "noBottom") == 0) faces = GEO_FACE_NO_BOTTOM;
            geo_box(&piece, ns_v3_make(size[0], size[1], size[2]),
                    tool_json_get_float(doc, p, "chamfer", 0.008f), faces, &uv, mat);
        } else if (strcmp(type, "panel") == 0) {
            float size[2];
            if (tool_json_get_floats(doc, p, "size", size, 2, 0.0f) != 2) {
                tool_fatalf("« %s », morceau %d : pas de taille [largeur, hauteur]", owner, i);
            }
            /* Le panneau est décrit dans son plan local (normale +Z, droite +X)
             * puis orienté par la transformation, comme tout le reste. Son UV est
             * explicite : une affiche n'a pas droit à la répétition.
             *
             * `tool_json_get_floats` garnit TOUTES les composantes du repli avant
             * de chercher la clé — c'est ce qui permet de distinguer « absent » de
             * « présent mais court ». Écrire directement dans `rect` écrasait donc
             * le rectangle par défaut (0,0,1,1) par (0,0,0,0) dès que la clé
             * manquait, et les huit affiches échantillonnaient un unique texel :
             * elles sortaient en aplats pâles, ce que la cible d'albédo a montré
             * du premier coup. */
            float rect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            float given[4];
            if (tool_json_get_floats(doc, p, "uvRect", given, 4, 0.0f) == 4) {
                memcpy(rect, given, sizeof rect);
            }
            geo_panel(&piece, ns_v3_zero(), ns_v3_make(0, 0, 1), ns_v3_make(1, 0, 0),
                      size[0], size[1], rect[0], rect[1], rect[2], rect[3], mat);
        } else if (strcmp(type, "cylinder") == 0) {
            /*
             * Le cylindre et le cône, enfin disponibles pour un prop.
             *
             * Ils manquaient, et ça se voyait à un endroit précis : la
             * suspension du billard. La salle déclarait sa LUMIÈRE — chaude,
             * basse, avec un commentaire expliquant qu'« une table de billard a
             * sa propre lampe » — et aucun LUMINAIRE. Une source sans objet
             * visible est exactement ce que le plan s'interdit : c'est ce qui
             * fait qu'une pièce paraît éclairée par magie. On ne pouvait pas
             * l'ajouter parce qu'un prop ne savait faire que des boîtes.
             */
            const float r0 = tool_json_get_float(doc, p, "radius", 0.0f);
            const float r1 = tool_json_get_float(doc, p, "radiusTop", r0);
            const float hh = tool_json_get_float(doc, p, "height", 0.0f);
            if (r0 <= 0.0f || hh <= 0.0f) {
                tool_fatalf("« %s », morceau %d : cylindre sans « radius » ou « height »",
                            owner, i);
            }
            const int sides = (int)tool_json_get_float(doc, p, "sides", 16.0f);
            geo_cylinder(&piece, r0, r1, hh, sides,
                         tool_json_get_bool(doc, p, "capBottom", true),
                         tool_json_get_bool(doc, p, "capTop", true), &uv, mat);
        } else if (strcmp(type, "sphere") == 0) {
            /* Une sphère par révolution : les billes, les ampoules, les boules.
             * Le profil est un demi-cercle, donc la primitive suffit et il n'y a
             * pas de générateur de sphère à écrire. */
            const float r0 = tool_json_get_float(doc, p, "radius", 0.0f);
            if (r0 <= 0.0f) {
                tool_fatalf("« %s », morceau %d : sphère sans « radius »", owner, i);
            }
            const int sides = (int)tool_json_get_float(doc, p, "sides", 12.0f);
            const int rings = (int)tool_json_get_float(doc, p, "rings", 7.0f);
            if (rings < 3 || rings > 33) {
                tool_fatalf("« %s », morceau %d : « rings » = %d, attendu 3..33",
                            owner, i, rings);
            }
            float prof[33 * 2];
            for (int k = 0; k < rings; ++k) {
                const float t = (float)k / (float)(rings - 1);
                const float a = -NS_PI * 0.5f + t * NS_PI;
                prof[k * 2]     = cosf(a) * r0;
                prof[k * 2 + 1] = sinf(a) * r0 + r0;   /* posée sur Y = 0 */
            }
            geo_revolve(&piece, prof, rings, sides, &uv, mat);
        } else {
            tool_fatalf("« %s », morceau %d : type « %s » inconnu "
                        "(box, panel, cylinder, sphere)", owner, i, type);
        }

        geo_xform x = GEO_XFORM_IDENTITY;
        x.origin = ns_v3_make(at[0], at[1], at[2]);
        x.yaw = yaw; x.pitch = pitch; x.roll = roll;
        geo_mesh_append(out, &piece, &x, -1);
        geo_mesh_free(&piece);
    }
}

static void parse_props(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "props");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        /* 60 octets : `instance_name` peut y ajouter un suffixe « _12 », et le
         * tout doit tenir dans les 64 de `ns_poi.name` sans être tronqué. */
        char base_name[56];
        tool_json_get_string(doc, e, "name", base_name, sizeof base_name);
        if (!base_name[0]) tool_fatalf("un objet sans nom (entrée %d de \"props\")", i);

        float at[3];
        tool_json_get_vec3(doc, e, "at", at, 0.0f);
        const float yaw = tool_json_get_float(doc, e, "yaw", 0.0f);

        const rg_repeat rep = read_repeat(doc, e);
        for (int k = 0; k < rep.count; ++k) {
            geo_mesh local; geo_mesh_init(&local);
            emit_prop_parts(b, doc, e, &local, base_name);

            geo_xform x = GEO_XFORM_IDENTITY;
            x.origin = ns_v3_make(at[0] + rep.step.x * (float)k,
                                  at[1] + rep.step.y * (float)k,
                                  at[2] + rep.step.z * (float)k);
            x.yaw = (yaw + rep.yaw_step * (float)k) * NS_DEG2RAD;

            geo_mesh placed; geo_mesh_init(&placed);
            geo_mesh_append(&placed, &local, &x, -1);
            geo_mesh_free(&local);

            /* 64 octets, comme `ns_poi.name` : un nom tronqué ici désignerait un
             * lieu que le moteur ne saurait plus rattacher à sa géométrie. */
            char name[64];
            instance_name(name, sizeof name, base_name, k, rep.count);
            /* Copiée dans l'accumulateur AVANT `emit_object`, qui LIBÈRE le
             * maillage qu'on lui passe. La première version copiait après :
             * l'accumulateur restait vide et le contrôle des lumières enfermées
             * ne vérifiait rien — le pire état possible pour un contrôle, et
             * c'est un `tool_infof` temporaire qui l'a montré. */
            {
                const geo_xform id = GEO_XFORM_IDENTITY;
                geo_mesh_append(&b->props_mesh, &placed, &id, -1);
            }
            emit_object(b, name, &placed, RG_SMOOTH_HARD);
            record_solid(b, name, RG_SOLID_PROP, doc, e);

            /* Un point d'intérêt déclaré : le moteur cessera de repérer le
             * billard et le canapé en cherchant des sous-chaînes dans les noms de
             * nœuds du glTF. */
            char poi_kind[24];
            tool_json_get_string(doc, e, "poi", poi_kind, sizeof poi_kind);
            if (poi_kind[0]) {
                if (b->poi_count >= RG_MAX_POIS) {
                    tool_fatalf("plus de %d points d'intérêt (NS_MAX_POI)", RG_MAX_POIS);
                }
                rg_poi *poi = &b->pois[b->poi_count++];
                memset(poi, 0, sizeof *poi);
                snprintf(poi->name, sizeof poi->name, "%s", name);
                snprintf(poi->kind, sizeof poi->kind, "%s", poi_kind);
                float anchor[3];
                tool_json_get_vec3(doc, e, "anchor", anchor, 0.0f);
                /* L'ancre est locale au meuble, comme ses morceaux. */
                const float c = cosf(x.yaw), s = sinf(x.yaw);
                poi->anchor[0] = x.origin.x + anchor[0] * c + anchor[2] * s;
                poi->anchor[1] = x.origin.y + anchor[1];
                poi->anchor[2] = x.origin.z - anchor[0] * s + anchor[2] * c;
                poi->bounds = b->last_bounds;
            }
        }
    }
}

/* ========================================================================== */
/* Plafond en dalles                                                          */
/* ========================================================================== */

/*
 * Le générateur au meilleur rapport valeur/ligne du palier : il corrige d'un coup
 * l'éclairage **et** l'aspect.
 *
 * L'ancien plafond était une dalle unique de 55 × 44 unités dont les UV bouclaient
 * une fois tous les 17,9 m — 0,056 répétition par mètre, ce qui se lit comme un
 * aplat quelle que soit la texture. Et comme son matériau portait `Ke 1 1 1`,
 * `obj2gltf` en tirait 16 lumières ponctuelles posées sur la boîte englobante de
 * la dalle, dont **7 tombaient hors des murs**.
 *
 * Ici : une dalle de 0,60 m, donc une répétition par dalle, soit 30 fois la
 * densité de texels ; de vrais rails en T ; et des panneaux lumineux **là où des
 * lumières sont déclarées**, pas ailleurs. La correspondance est calculée depuis
 * la liste des lumières plutôt que recopiée à la main : deux listes à tenir
 * d'accord divergent, une seule ne peut pas.
 */
static void parse_ceilings(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "ceilings");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char name[GLTF_MAX_NAME];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        if (!name[0]) tool_fatalf("un plafond sans nom (entrée %d)", i);

        char tile_mat[64], rail_mat[64], panel_mat[64], void_mat[64];
        tool_json_get_string(doc, e, "material", tile_mat, sizeof tile_mat);
        tool_json_get_string(doc, e, "railMaterial", rail_mat, sizeof rail_mat);
        tool_json_get_string(doc, e, "panelMaterial", panel_mat, sizeof panel_mat);
        tool_json_get_string(doc, e, "voidMaterial", void_mat, sizeof void_mat);
        const int m_tile  = material_index(b, tile_mat, name);
        const int m_rail  = material_index(b, rail_mat[0] ? rail_mat : tile_mat, name);
        const int m_panel = material_index(b, panel_mat[0] ? panel_mat : tile_mat, name);
        const int m_void  = material_index(b, void_mat[0] ? void_mat : rail_mat, name);

        float centre[2], size[2];
        tool_json_get_vec2(doc, e, "centre", centre, 0.0f);
        if (tool_json_get_floats(doc, e, "size", size, 2, 0.0f) != 2) {
            tool_fatalf("le plafond « %s » n'a pas de taille [x, z]", name);
        }
        const float y = tool_json_get_float(doc, e, "y", 3.10f);
        const float tile = ns_maxf(tool_json_get_float(doc, e, "tile", 0.60f), 0.10f);
        const float rail_w = tool_json_get_float(doc, e, "railWidth", 0.024f);
        const float rail_h = tool_json_get_float(doc, e, "railDrop", 0.045f);
        const float plenum = tool_json_get_float(doc, e, "plenum", 0.38f);

        /* Le pas est ajusté pour que la trame tombe juste sur l'emprise : une
         * demi-dalle au bord se verrait immédiatement. */
        const int nx = (int)ns_maxf(1.0f, floorf(size[0] / tile + 0.5f));
        const int nz = (int)ns_maxf(1.0f, floorf(size[1] / tile + 0.5f));
        const float sx = size[0] / (float)nx;
        const float sz = size[1] / (float)nz;
        const float x0 = centre[0] - size[0] * 0.5f;
        const float z0 = centre[1] - size[1] * 0.5f;

        /* Dalles manquantes, en indices de trame. Un indice hors trame est une
         * faute de frappe, pas une dalle qu'on renonce à retirer. */
        const tool_json_value *missing = tool_json_get(doc, e, "missing");
        const int missing_count = tool_json_array_count(doc, missing);
        int holes[16][2];
        if (missing_count > 16) tool_fatalf("le plafond « %s » : trop de dalles retirées", name);
        for (int k = 0; k < missing_count; ++k) {
            const tool_json_value *p = tool_json_at(doc, missing, k);
            holes[k][0] = (int)tool_json_value_float(doc, tool_json_at(doc, p, 0), -1.0f);
            holes[k][1] = (int)tool_json_value_float(doc, tool_json_at(doc, p, 1), -1.0f);
            if (holes[k][0] < 0 || holes[k][0] >= nx || holes[k][1] < 0 || holes[k][1] >= nz) {
                tool_fatalf("le plafond « %s » : la dalle retirée (%d, %d) est hors de la "
                            "trame %d x %d", name, holes[k][0], holes[k][1], nx, nz);
            }
        }

        geo_mesh m; geo_mesh_init(&m);
        geo_mesh_reserve(&m, (size_t)nx * (size_t)nz * 4, (size_t)nx * (size_t)nz * 2);

        const geo_uv rail_uv = geo_uv_tile(0.60f);
        int panels_placed = 0;

        for (int iz = 0; iz < nz; ++iz) {
            for (int ix = 0; ix < nx; ++ix) {
                const float cx = x0 + ((float)ix + 0.5f) * sx;
                const float cz = z0 + ((float)iz + 0.5f) * sz;

                bool hole = false;
                for (int k = 0; k < missing_count; ++k) {
                    if (holes[k][0] == ix && holes[k][1] == iz) hole = true;
                }

                /*
                 * Une dalle devient panneau lumineux quand son **centre** tombe
                 * dans l'emprise d'un luminaire déclaré. Tester le centre plutôt
                 * qu'un recouvrement évite l'égalité douteuse au bord : un
                 * luminaire de 1,20 m posé sur un joint prend exactement les deux
                 * dalles qu'il couvre, jamais trois.
                 */
                bool lit = false;
                for (size_t l = 0; l < b->light_count; ++l) {
                    const rg_light *lg = &b->lights[l];
                    if (!lg->ceiling_panel) continue;
                    if (fabsf(lg->position[1] - y) > 0.50f) continue;
                    if (fabsf(lg->position[0] - cx) >= lg->panel_size[0] * 0.5f - 1e-3f) continue;
                    if (fabsf(lg->position[2] - cz) >= lg->panel_size[1] * 0.5f - 1e-3f) continue;
                    lit = true;
                    break;
                }
                if (lit) panels_placed++;

                if (hole) {
                    /* Le vide au-dessus, vu par en dessous : les faces internes du
                     * plénum. Le rendu n'élimine pas les faces arrière et retourne
                     * la normale, donc une boîte vue de l'intérieur s'éclaire
                     * correctement — c'est ce qui rend ce trou possible sans
                     * générateur dédié. */
                    geo_mesh box; geo_mesh_init(&box);
                    const geo_uv vuv = geo_uv_tile(0.5f);
                    geo_box(&box, ns_v3_make(sx - rail_w, plenum, sz - rail_w), 0.0f,
                            GEO_FACE_SIDES | GEO_FACE_PY, &vuv, m_void);
                    geo_xform x = GEO_XFORM_IDENTITY;
                    x.origin = ns_v3_make(cx, y, cz);
                    geo_mesh_append(&m, &box, &x, -1);
                    geo_mesh_free(&box);

                    /* Et le câble qui pend, qui est ce qui fait lire le trou comme
                     * un trou plutôt que comme une dalle noire. */
                    geo_mesh cable; geo_mesh_init(&cable);
                    geo_box(&cable, ns_v3_make(0.018f, plenum * 0.75f, 0.018f), 0.0f,
                            GEO_FACE_SIDES, &vuv, m_void);
                    geo_xform xc = GEO_XFORM_IDENTITY;
                    xc.origin = ns_v3_make(cx + sx * 0.18f, y - plenum * 0.30f, cz - sz * 0.12f);
                    xc.roll = 0.22f;
                    geo_mesh_append(&m, &cable, &xc, -1);
                    geo_mesh_free(&cable);
                    continue;
                }

                geo_panel(&m, ns_v3_make(cx, y, cz),
                          ns_v3_make(0.0f, -1.0f, 0.0f), ns_v3_make(1.0f, 0.0f, 0.0f),
                          sx - rail_w, sz - rail_w,
                          0.0f, 0.0f, 1.0f, 1.0f, lit ? m_panel : m_tile);
            }
        }

        /* Les rails en T, dans les deux sens. Ils descendent sous le plan des
         * dalles : c'est cette ombre portée de quelques millimètres qui donne au
         * plafond son relief, et qu'une simple texture ne peut pas imiter. */
        for (int ix = 0; ix <= nx; ++ix) {
            geo_mesh rail; geo_mesh_init(&rail);
            geo_box(&rail, ns_v3_make(rail_w, rail_h, size[1]), 0.0f,
                    GEO_FACE_ALL & ~GEO_FACE_PY, &rail_uv, m_rail);
            geo_xform x = GEO_XFORM_IDENTITY;
            x.origin = ns_v3_make(x0 + (float)ix * sx, y - rail_h, centre[1]);
            geo_mesh_append(&m, &rail, &x, -1);
            geo_mesh_free(&rail);
        }
        for (int iz = 0; iz <= nz; ++iz) {
            geo_mesh rail; geo_mesh_init(&rail);
            geo_box(&rail, ns_v3_make(size[0], rail_h, rail_w), 0.0f,
                    GEO_FACE_ALL & ~GEO_FACE_PY, &rail_uv, m_rail);
            geo_xform x = GEO_XFORM_IDENTITY;
            x.origin = ns_v3_make(centre[0], y - rail_h, z0 + (float)iz * sz);
            geo_mesh_append(&m, &rail, &x, -1);
            geo_mesh_free(&rail);
        }

        tool_infof("plafond « %s » : %d x %d dalles de %.3f x %.3f m, %d panneau(x) "
                   "lumineux, %d dalle(s) retirée(s)",
                   name, nx, nz, (double)sx, (double)sz, panels_placed, missing_count);

        emit_object(b, name, &m, RG_SMOOTH_HARD);
    }
}

/* ========================================================================== */
/* Lumières                                                                   */
/* ========================================================================== */

static void parse_lights(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "lights");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char base_name[64];
        tool_json_get_string(doc, e, "name", base_name, sizeof base_name);
        if (!base_name[0]) tool_fatalf("une lumière sans nom (entrée %d)", i);

        /* La liste EXHAUSTIVE de ce qu'une lumière peut déclarer. Elle existe
         * parce que six des seize lumières de cette salle écrivaient « colour »
         * pour « color » : elles s'affichaient en blanc pur, et rien ne le
         * disait. Une clé mal orthographiée casse maintenant le build. */
        static const char *const light_keys[] = {
            "name", "at", "color", "intensity", "range", "radius",
            "flicker", "ceilingPanel", "panelSize", "repeat", "insideOk", NULL
        };
        char what[128];
        snprintf(what, sizeof what, "lumière « %s »", base_name);
        tool_json_reject_unknown_keys(doc, e, light_keys, what);

        float at[3], color[3];
        tool_json_get_vec3(doc, e, "at", at, 0.0f);
        tool_json_get_vec3(doc, e, "color", color, 1.0f);
        const float intensity = tool_json_get_float(doc, e, "intensity", 120.0f);
        const float range = tool_json_get_float(doc, e, "range", 9.0f);
        /* Rayon apparent de la source. 0,22 m est l'ampoule d'applique pour
         * laquelle le shader avait une constante globale ; un pavé de faux
         * plafond de 1,20 x 0,60 m se déclare à 0,45. Sans ce champ, un luminaire
         * étendu brûle tout ce qui est à moins d'un mètre de lui. */
        const float radius = tool_json_get_float(doc, e, "radius", 0.22f);
        const bool flicker = tool_json_get_bool(doc, e, "flicker", false);
        const bool panel = tool_json_get_bool(doc, e, "ceilingPanel", false);
        const bool inside_ok = tool_json_get_bool(doc, e, "insideOk", false);
        float panel_size[2] = { 0.60f, 0.60f };
        tool_json_get_vec2(doc, e, "panelSize", panel_size, 0.60f);

        const rg_repeat rep = read_repeat(doc, e);
        for (int k = 0; k < rep.count; ++k) {
            if (b->light_count >= RG_MAX_LIGHTS) {
                tool_fatalf("plus de %d lumières : le moteur en tient %d "
                            "(NS_MAX_LIGHTS), et les écrans de bornes en ajouteront "
                            "dix-neuf", RG_MAX_LIGHTS, RG_MAX_LIGHTS);
            }
            rg_light *l = &b->lights[b->light_count++];
            memset(l, 0, sizeof *l);
            instance_name(l->name, sizeof l->name, base_name, k, rep.count);
            l->position[0] = at[0] + rep.step.x * (float)k;
            l->position[1] = at[1] + rep.step.y * (float)k;
            l->position[2] = at[2] + rep.step.z * (float)k;
            memcpy(l->color, color, sizeof color);
            l->intensity = intensity;
            l->range = range;
            l->source_radius = radius;
            l->flicker = flicker;
            l->ceiling_panel = panel;
            l->inside_ok = inside_ok;
            l->panel_size[0] = panel_size[0];
            l->panel_size[1] = panel_size[1];
        }
    }
}

/* ========================================================================== */
/* Fichiers annexes                                                           */
/* ========================================================================== */

static void write_lights_json(const rg_builder *b, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) tool_fatalf("impossible d'écrire %s", path);

    fprintf(f, "{\n");
    fprintf(f, "  \"_comment\": \"Généré par roomgen depuis salle.room.json. "
               "Contrairement au fichier que produisait obj2gltf, rien n'est ici "
               "déduit d'une aire émissive : chaque lumière est déclarée, avec son "
               "nom, sa place et sa portée.\",\n");
    fprintf(f, "  \"bounds\": { \"min\": [%.4f, %.4f, %.4f], \"max\": [%.4f, %.4f, %.4f] },\n",
            (double)b->bounds.min.x, (double)b->bounds.min.y, (double)b->bounds.min.z,
            (double)b->bounds.max.x, (double)b->bounds.max.y, (double)b->bounds.max.z);
    fprintf(f, "  \"lights\": [\n");
    for (size_t i = 0; i < b->light_count; ++i) {
        const rg_light *l = &b->lights[i];
        fprintf(f, "    { \"name\": \"%s\", \"type\": \"point\", "
                   "\"position\": [%.4f, %.4f, %.4f], \"color\": [%.4f, %.4f, %.4f], "
                   "\"intensity\": %.3f, \"range\": %.3f, \"radius\": %.3f, "
                   "\"flicker\": %s }%s\n",
                l->name,
                (double)l->position[0], (double)l->position[1], (double)l->position[2],
                (double)l->color[0], (double)l->color[1], (double)l->color[2],
                (double)l->intensity, (double)l->range, (double)l->source_radius,
                l->flicker ? "true" : "false",
                (i + 1 < b->light_count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
}

static void write_scene_json(const tool_json *doc, const tool_json_value *root,
                             const rg_builder *b, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) tool_fatalf("impossible d'écrire %s", path);

    float pmin[3], pmax[3];
    const tool_json_value *playable = tool_json_get(doc, tool_json_get(doc, root, "room"),
                                                    "playable");
    if (!playable) tool_fatalf("la description ne déclare pas room.playable");
    tool_json_get_vec3(doc, playable, "min", pmin, 0.0f);
    tool_json_get_vec3(doc, playable, "max", pmax, 0.0f);

    fprintf(f, "{\n");
    fprintf(f, "  \"_comment\": \"Généré par roomgen. La salle reconstruite est en "
               "mètres, donc unitsPerMetre vaut 1 — contre 2,06 pour le modèle de "
               "2020, dont l'auteur avait écrit HAUTEUR_CAMERA_DEBOUT 3.5F.\",\n");
    fprintf(f, "  \"unitsPerMetre\": 1.0,\n");
    fprintf(f, "  \"roomBounds\": { \"min\": [%.3f, %.3f, %.3f], "
               "\"max\": [%.3f, %.3f, %.3f] },\n",
            (double)pmin[0], (double)pmin[1], (double)pmin[2],
            (double)pmax[0], (double)pmax[1], (double)pmax[2]);

    /*
     * LE TABLEAU DU BAR, resolu ici en indice de materiau.
     *
     * La salle le nomme, roomgen le resout, le moteur le pilote — la meme
     * chaine que pour la dalle d'une borne. Un materiau nomme mais inconnu
     * ARRETE l'outil : un tableau qui ne s'allume pas est exactement le genre
     * de panne muette que ce fichier existe pour rendre impossible.
     */
    {
        char board[64] = { 0 };
        tool_json_get_string(doc, root, "scoreboard", board, sizeof board);
        if (board[0]) {
            const int mi = material_index(b, board, "scoreboard");
            fprintf(f, "  \"scoreboard\": %d,\n", mi);
        }
    }

    const tool_json_value *start = tool_json_get(doc, root, "playerStart");
    if (start) {
        float p[3];
        tool_json_get_vec3(doc, start, "position", p, 0.0f);
        fprintf(f, "  \"playerStart\": { \"position\": [%.3f, %.3f, %.3f], "
                   "\"yaw\": %.2f },\n",
                (double)p[0], (double)p[1], (double)p[2],
                (double)tool_json_get_float(doc, start, "yaw", -90.0f));
    }

    /* Les points de vue sont recopiés tels quels : ils portent les **mêmes noms**
     * que ceux de `salle-legacy.scene.json`, exprimés ici en mètres. C'est ce qui
     * rend `--view=allee` comparable entre deux salles d'échelles différentes,
     * là où `--pos=` désignerait deux endroits sans rapport. */
    const tool_json_value *views = tool_json_get(doc, root, "captures");
    const int view_count = tool_json_array_count(doc, views);
    fprintf(f, "  \"captures\": [\n");
    for (int i = 0; i < view_count; ++i) {
        const tool_json_value *e = tool_json_at(doc, views, i);
        char name[32];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        float p[3];
        tool_json_get_vec3(doc, e, "position", p, 0.0f);
        const bool orbit = tool_json_get_bool(doc, e, "orbit", false);

        /*
         * Un point de vue posé dans un meuble rend un cadre noir, et rien ne le
         * dit : ni le build, ni le journal d'exécution. C'est arrivé deux fois,
         * aux mêmes captures, quand A4 a meublé la salle sous des points de vue
         * écrits pour la salle vide. L'outil connaît les deux : il refuse.
         *
         * La marge correspond au rayon du corps du joueur (32 cm) : une caméra
         * qui frôle une borne de trois centimètres a déjà la face avant du
         * meuble en plein cadre.
         *
         * Les orbites sont exclues : leur `position` est un CENTRE de rotation,
         * pas un point où la caméra se tient. Celle de la salle tourne
         * précisément autour de la borne centrale.
         */
        if (!orbit) {
            const float margin = 0.32f;
            for (size_t k = 0; k < b->solid_count; ++k) {
                const ns_aabb *bb = &b->solids[k].bounds;
                if (p[0] > bb->min.x - margin && p[0] < bb->max.x + margin
                 && p[1] > bb->min.y - margin && p[1] < bb->max.y + margin
                 && p[2] > bb->min.z - margin && p[2] < bb->max.z + margin) {
                    tool_fatalf("le point de vue « %s » est posé dans « %s » "
                                "(%.2f, %.2f, %.2f dans [%.2f %.2f %.2f]-[%.2f %.2f %.2f]) — "
                                "il rendrait un cadre noir",
                                name, b->solids[k].name,
                                (double)p[0], (double)p[1], (double)p[2],
                                (double)bb->min.x, (double)bb->min.y, (double)bb->min.z,
                                (double)bb->max.x, (double)bb->max.y, (double)bb->max.z);
                }
            }
        }

        fprintf(f, "    { \"name\": \"%s\", \"position\": [%.3f, %.3f, %.3f]",
                name, (double)p[0], (double)p[1], (double)p[2]);
        if (orbit) {
            fprintf(f, ", \"orbit\": true, \"radius\": %.3f, \"height\": %.3f",
                    (double)tool_json_get_float(doc, e, "radius", 8.0f),
                    (double)tool_json_get_float(doc, e, "height", 4.0f));
        } else {
            fprintf(f, ", \"yaw\": %.2f, \"pitch\": %.2f",
                    (double)tool_json_get_float(doc, e, "yaw", 0.0f),
                    (double)tool_json_get_float(doc, e, "pitch", 0.0f));
        }
        fprintf(f, " }%s\n", (i + 1 < view_count) ? "," : "");
    }
    fprintf(f, "  ],\n");

    /*
     * Les bornes, DÉCLARÉES. C'est le bloc qui supprime trois heuristiques d'un
     * coup : les fractions inventées de la boîte englobante pour situer l'écran,
     * le barycentre du troupeau pour deviner vers où une borne regarde, et le tri
     * en X puis Z pour affecter les jeux. Aucune ne pouvait être juste, parce
     * qu'aucune n'avait l'information.
     */
    fprintf(f, "  \"cabinets\": [\n");
    for (size_t i = 0; i < b->cabinet_count; ++i) {
        const rg_cabinet *c = &b->cabinets[i];
        fprintf(f, "    { \"name\": \"%s\", \"slot\": %d, \"game\": \"%s\", "
                   "\"difficulty\": \"%s\", \"attract\": %s,\n",
                c->name, c->slot, c->game, c->difficulty[0] ? c->difficulty : "normal",
                c->attract ? "true" : "false");
        fprintf(f, "      \"bboxMin\": [%.4f, %.4f, %.4f], "
                   "\"bboxMax\": [%.4f, %.4f, %.4f],\n",
                (double)c->bounds_min[0], (double)c->bounds_min[1], (double)c->bounds_min[2],
                (double)c->bounds_max[0], (double)c->bounds_max[1], (double)c->bounds_max[2]);
        fprintf(f, "      \"screenCenter\": [%.4f, %.4f, %.4f], "
                   "\"screenNormal\": [%.4f, %.4f, %.4f], "
                   "\"screenWidth\": %.4f, \"screenHeight\": %.4f,\n",
                (double)c->screen_center[0], (double)c->screen_center[1],
                (double)c->screen_center[2],
                (double)c->screen_normal[0], (double)c->screen_normal[1],
                (double)c->screen_normal[2],
                (double)c->screen_size[0], (double)c->screen_size[1]);
        fprintf(f, "      \"playerAnchor\": [%.4f, %.4f, %.4f],\n",
                (double)c->player_anchor[0], (double)c->player_anchor[1],
                (double)c->player_anchor[2]);
        /* Les deux points que la main vise. Ils sortent d'ici pour la même raison
         * que le centre d'écran : ils sont écrits à trois lignes des boîtes
         * qu'ils désignent, et une seconde copie dériverait. */
        /* L'index du matériau de la dalle. C'est ce qui permet d'y faire tourner
         * un jeu : le moteur remplace la texture de CE matériau-là, sans avoir à
         * deviner lequel des lots d'une borne est son écran. */
        fprintf(f, "      \"screenMaterial\": %d,\n", c->screen_material);
        fprintf(f, "      \"panelCentre\": [%.4f, %.4f, %.4f],\n"
                   "      \"coinSlot\": [%.4f, %.4f, %.4f],\n"
                   "      \"stickTop\": [%.4f, %.4f, %.4f] }%s\n",
                (double)c->panel_centre[0], (double)c->panel_centre[1],
                (double)c->panel_centre[2],
                (double)c->coin_slot[0], (double)c->coin_slot[1],
                (double)c->coin_slot[2],
                (double)c->stick_top[0], (double)c->stick_top[1],
                (double)c->stick_top[2],
                (i + 1 < b->cabinet_count) ? "," : "");
    }
    fprintf(f, "  ],\n");

    /* Les lieux du décor, déclarés eux aussi. Le moteur les repérait en cherchant
     * des sous-chaînes dans les noms de nœuds du glTF — une seconde analyse du
     * fichier, dont le champ `bounds` n'était d'ailleurs jamais rempli. */
    /*
     * La classe de pas de chaque matériau, dans l'ORDRE DES MATÉRIAUX du glTF.
     *
     * C'est l'ordre qui fait tout le travail : `ns_bvh_move_capsule` renvoie
     * l'index de matériau du triangle sous les pieds, et cet index désigne la
     * même entrée ici. Écrire un nom de matériau à la place obligerait le moteur
     * à faire une recherche par chaîne à chaque pas, pour retrouver un index
     * qu'il avait déjà.
     */
    /*
     * Les zones de poussière, recopiées telles quelles depuis la description.
     *
     * `roomgen` ne les transforme pas — il les VALIDE et les transmet. La
     * validation compte : une boîte vide ou inversée donnerait zéro grain sans
     * le moindre message, et on chercherait du côté du rendu.
     */
    {
        const tool_json_value *dust = tool_json_get(doc, root, "dust");
        const int n = tool_json_array_count(doc, dust);
        fprintf(f, "  \"dust\": [\n");
        for (int i = 0; i < n; ++i) {
            const tool_json_value *e = tool_json_at(doc, dust, i);
            char name[64];
            tool_json_get_string(doc, e, "name", name, sizeof name);

            float mn[3], mx[3], col[3];
            tool_json_get_vec3(doc, e, "min", mn, 0.0f);
            tool_json_get_vec3(doc, e, "max", mx, 0.0f);
            tool_json_get_vec3(doc, e, "color", col, 1.0f);
            for (int k = 0; k < 3; ++k) {
                if (mx[k] <= mn[k]) {
                    tool_fatalf("zone de poussière « %s » : la boîte est vide ou inversée sur "
                                "l'axe %d (%.3f à %.3f). Elle ne produirait aucun grain, en "
                                "silence.", name[0] ? name : "?", k,
                                (double)mn[k], (double)mx[k]);
                }
            }
            float drift[3];
            tool_json_get_vec3(doc, e, "drift", drift, 0.0f);

            fprintf(f, "    { \"name\": \"%s\", \"min\": [%.3f, %.3f, %.3f], "
                       "\"max\": [%.3f, %.3f, %.3f], \"density\": %.3f, "
                       "\"drift\": [%.4f, %.4f, %.4f], \"size\": %.4f, "
                       "\"color\": [%.3f, %.3f, %.3f], \"brightness\": %.3f }%s\n",
                    name, (double)mn[0], (double)mn[1], (double)mn[2],
                    (double)mx[0], (double)mx[1], (double)mx[2],
                    (double)tool_json_get_float(doc, e, "density", 0.5f),
                    (double)drift[0], (double)drift[1], (double)drift[2],
                    (double)tool_json_get_float(doc, e, "size", 0.02f),
                    (double)col[0], (double)col[1], (double)col[2],
                    (double)tool_json_get_float(doc, e, "brightness", 0.5f),
                    (i + 1 < n) ? "," : "");
        }
        fprintf(f, "  ],\n");
        if (n) printf("  %d zone(s) de poussière\n", n);
    }

    /*
     * Les zones de réverbération. Même forme que la poussière, et pour la même
     * raison : une boîte nommée, déclarée par la salle, que le moteur applique
     * sans rien deviner. Ce qu'elles portent est ce qu'on ENTEND quand on est
     * dedans — un carrelage rend, une moquette avale.
     */
    {
        const tool_json_value *zones = tool_json_get(doc, root, "soundZones");
        const int n = tool_json_array_count(doc, zones);
        fprintf(f, "  \"soundZones\": [\n");
        for (int i = 0; i < n; ++i) {
            const tool_json_value *e = tool_json_at(doc, zones, i);
            char name[64];
            tool_json_get_string(doc, e, "name", name, sizeof name);

            float mn[3], mx[3];
            tool_json_get_vec3(doc, e, "min", mn, 0.0f);
            tool_json_get_vec3(doc, e, "max", mx, 0.0f);
            for (int k = 0; k < 3; ++k) {
                if (mx[k] <= mn[k]) {
                    tool_fatalf("zone sonore « %s » : la boîte est vide ou inversée sur "
                                "l'axe %d (%.3f à %.3f). On n'y entrerait jamais, en "
                                "silence.", name[0] ? name : "?", k,
                                (double)mn[k], (double)mx[k]);
                }
            }
            const float wet = tool_json_get_float(doc, e, "wet", 0.0f);
            const float decay = tool_json_get_float(doc, e, "decay", 0.0f);
            if (wet < 0.0f || wet > 0.9f || decay < 0.0f || decay > 0.85f) {
                tool_fatalf("zone sonore « %s » : wet %.2f et decay %.2f doivent tenir "
                            "dans [0 ; 0,9] et [0 ; 0,85] — au-delà, la contre-réaction "
                            "s'emballe et la queue ne s'éteint plus",
                            name[0] ? name : "?", (double)wet, (double)decay);
            }
            fprintf(f, "    { \"name\": \"%s\", \"min\": [%.3f, %.3f, %.3f], "
                       "\"max\": [%.3f, %.3f, %.3f], \"wet\": %.3f, \"decay\": %.3f }%s\n",
                    name, (double)mn[0], (double)mn[1], (double)mn[2],
                    (double)mx[0], (double)mx[1], (double)mx[2],
                    (double)wet, (double)decay, (i + 1 < n) ? "," : "");
        }
        fprintf(f, "  ],\n");
        if (n) printf("  %d zone(s) sonore(s)\n", n);
    }

    fprintf(f, "  \"materialFootsteps\": [\n");
    for (size_t i = 0; i < b->material_count; ++i) {
        fprintf(f, "    \"%s\"%s\n",
                b->material_footstep[i][0] ? b->material_footstep[i] : "",
                (i + 1 < b->material_count) ? "," : "");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"pois\": [\n");
    for (size_t i = 0; i < b->poi_count; ++i) {
        const rg_poi *p = &b->pois[i];
        fprintf(f, "    { \"name\": \"%s\", \"kind\": \"%s\", "
                   "\"boundsMin\": [%.3f, %.3f, %.3f], "
                   "\"boundsMax\": [%.3f, %.3f, %.3f], "
                   "\"anchor\": [%.3f, %.3f, %.3f] }%s\n",
                p->name, p->kind,
                (double)p->bounds.min.x, (double)p->bounds.min.y, (double)p->bounds.min.z,
                (double)p->bounds.max.x, (double)p->bounds.max.y, (double)p->bounds.max.z,
                (double)p->anchor[0], (double)p->anchor[1], (double)p->anchor[2],
                (i + 1 < b->poi_count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
}

/* ========================================================================== */
/* Programme                                                                  */
/* ========================================================================== */

static void usage(void)
{
    fprintf(stderr,
        "usage : roomgen <salle.room.json> <sortie.gltf> [--textures=REP]\n"
        "                [--expect-textures=N] [--assets=REP]\n"
        "\n"
        "  --textures=REP        vérifie que chaque texture nommée existe dans REP.\n"
        "                        Sans cette option, une faute de frappe se découvre\n"
        "                        à l'exécution, en substitut procédural.\n"
        "  --expect-textures=N   exige que la salle en référence exactement N.\n"
        "                        CMake y met le nombre de fichiers réellement\n"
        "                        présents : une texture d'origine qui cesserait\n"
        "                        d'être employée casse alors le build au lieu de\n"
        "                        disparaître du décor sans un mot.\n"
        "  --assets=REP          racine à laquelle sont résolus les chemins de\n"
        "                        modèles (\"model\": \"cc0/models/...\"). Donnée\n"
        "                        plutôt que déduite : le fichier de salle est une\n"
        "                        source de build, rien ne le range à côté de ses\n"
        "                        assets.\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *in_path = NULL, *out_path = NULL;
    const char *texture_dirs[RG_MAX_TEXTURE_DIRS];
    size_t texture_dir_count = 0;
    int expect_textures = -1;
    const char *asset_root = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--textures=", 11) == 0) {
            if (texture_dir_count >= RG_MAX_TEXTURE_DIRS) {
                tool_fatalf("plus de %d répertoires de textures", RG_MAX_TEXTURE_DIRS);
            }
            texture_dirs[texture_dir_count++] = argv[i] + 11;
        }
        else if (strncmp(argv[i], "--expect-textures=", 18) == 0) {
            expect_textures = atoi(argv[i] + 18);
        }
        else if (strncmp(argv[i], "--assets=", 9) == 0) {
            asset_root = argv[i] + 9;
        }
        else if (argv[i][0] == '-') usage();
        else if (!in_path) in_path = argv[i];
        else if (!out_path) out_path = argv[i];
        else usage();
    }
    if (!in_path || !out_path) usage();

    size_t size = 0;
    char *text = tool_read_file(in_path, &size);
    if (!text) tool_fatalf("description introuvable : %s", in_path);

    tool_json doc;
    tool_json_parse(&doc, text, size, in_path);
    const tool_json_value *root = tool_json_root(&doc);
    if (!root) tool_fatalf("%s : document vide", in_path);

    char units[16];
    tool_json_get_string(&doc, root, "units", units, sizeof units);
    if (strcmp(units, "metres") != 0) {
        tool_fatalf("%s : \"units\" doit valoir \"metres\" — c'est la moitié de "
                    "l'intérêt de la reconstruction", in_path);
    }

    rg_builder b;
    memset(&b, 0, sizeof b);
    /* Aucun contour fermé lu pour l'instant. Zéro serait un index valide. */
    b.shell_index = -1;
    b.shell_count = 0;
    tool_vec_init(&b.verts, sizeof(gltf_vertex));
    tool_vec_init(&b.meshes, sizeof(gltf_mesh));
    tool_vec_init(&b.prim_blocks, sizeof(geo_primitives));
    /* L'accumulateur des luminaires : sans cette ligne, son tableau reste vide
     * et le controle des lumieres enfermees ne verifie rien — ce qui est le
     * pire etat possible pour un controle. */
    geo_mesh_init(&b.props_mesh);
    b.bounds = ns_aabb_empty();
    for (size_t d = 0; d < texture_dir_count; ++d) b.texture_dirs[d] = texture_dirs[d];
    b.texture_dir_count = texture_dir_count;
    b.expect_textures = expect_textures;
    if (asset_root) snprintf(b.asset_root, sizeof b.asset_root, "%s", asset_root);

    const tool_json_value *room = tool_json_get(&doc, root, "room");
    if (!room) tool_fatalf("%s : pas de bloc \"room\"", in_path);
    const float height = tool_json_get_float(&doc, room, "height", 3.10f);
    const float thickness = tool_json_get_float(&doc, room, "wallThickness", 0.20f);

    /*
     * Les images volontairement inemployées. Chacune porte son motif, et le motif
     * est lu par un humain, pas par l'outil : ce qui compte est qu'il soit écrit.
     * L'outil vérifie seulement que le fichier EXISTE — retirer une image qui
     * n'est plus là ne prouve rien.
     */
    {
        const tool_json_value *retired = tool_json_get(&doc, root, "retiredTextures");
        const int n = tool_json_array_count(&doc, retired);
        for (int i = 0; i < n; ++i) {
            const tool_json_value *e = tool_json_at(&doc, retired, i);
            char file[128];
            tool_json_get_string(&doc, e, "file", file, sizeof file);
            if (!file[0]) tool_fatalf("`retiredTextures[%d]` n'a pas de clé « file »", i);

            char why[8];
            tool_json_get_string(&doc, e, "why", why, sizeof why);
            if (!why[0]) {
                tool_fatalf("« %s » est déclarée retirée sans motif. Le motif est le seul "
                            "intérêt de cette liste : sans lui, elle ne fait que masquer "
                            "le contrôle qu'elle est censée rendre plus fin.", file);
            }

            bool exists = false;
            for (size_t d = 0; d < b.texture_dir_count && !exists; ++d) {
                char path[512];
                snprintf(path, sizeof path, "%s/%s", b.texture_dirs[d], file);
                FILE *f = fopen(path, "rb");
                if (f) { fclose(f); exists = true; }
            }
            if (!exists) {
                tool_fatalf("« %s » est déclarée retirée, mais aucun dossier d'art ne la "
                            "contient : la ligne ne retire rien et fausse le compte", file);
            }
            b.retired_count++;
        }
    }

    parse_materials(&b, &doc, root);
    /* Les lumières d'abord : le plafond s'en sert pour savoir où poser ses
     * panneaux lumineux, ce qui évite d'avoir deux listes à tenir d'accord. */
    parse_lights(&b, &doc, root);

    parse_floors(&b, &doc, root);
    parse_walls(&b, &doc, root, height, thickness);
    parse_ceilings(&b, &doc, root);
    parse_boxes(&b, &doc, root);
    parse_mouldings(&b, &doc, root);
    parse_cabinets(&b, &doc, root);
    parse_props(&b, &doc, root);

    /* Après tout le mobilier, avant d'écrire quoi que ce soit. */
    check_inside_shell(&b);
    check_solid_overlaps(&b);
    check_grounded(&b);
    check_cabinet_clearance(&b);
    /* Après C-09, et ce n'est pas indifférent : C-09 dit qu'on tient DEVANT une
     * borne, C-10 dit qu'on peut y ARRIVER. Le second sans le premier laisserait
     * croire qu'une borne encastrée dans sa voisine est jouable. */
    check_reachable(&doc, root, &b);
    check_lights_not_enclosed(&b);
    check_fixture_naming(&b);

    if (b.meshes.count == 0) tool_fatalf("%s : la description ne produit aucun objet", in_path);

    /*
     * Couverture des textures d'origine.
     *
     * Les 58 images de 2020 sont ce qui garde la même salle : c'est le seul
     * élément du décor qui n'a pas été réécrit. En référencer 57 signifierait
     * qu'un pan du décor a disparu — et c'est exactement le genre de perte qui ne
     * se remarque pas sur une capture, puisqu'il n'y a rien à voir là où il n'y a
     * plus rien.
     */
    /*
     * Couverture des textures : chaque image doit être soit EMPLOYÉE, soit
     * RETIRÉE explicitement, avec sa raison.
     *
     * Le contrôle ne comptait auparavant que les employées, et exigeait le total.
     * C'était juste tant que les 58 images de 2020 formaient tout le budget d'art
     * — une image qui cessait de servir était forcément une régression. Ça ne
     * l'est plus : huit de ces images sont des aplats de couleur de 24 x 24 px
     * (`bordeau_uni.jpg` fait 1 x 1), et les retirer est une correction, pas une
     * perte. Un simple ajustement du nombre attendu aurait fait disparaître ce
     * fait du dépôt ; la liste `retiredTextures` le grave, avec le motif.
     */
    if (b.expect_textures >= 0) {
        const int accounted = (int)(b.texture_count + b.retired_count);
        if (accounted != b.expect_textures) {
            tool_fatalf("%zu texture(s) employée(s) + %zu retirée(s) = %d, pour %d "
                        "présente(s) dans les dossiers d'art.\n"
                        "  Une image a donc cessé d'être employée sans être déclarée dans "
                        "`retiredTextures`, ou une a été ajoutée sans être placée.\n"
                        "  Retirer une image est légitime — la retirer en silence ne l'est "
                        "pas : c'est ainsi qu'un décor se vide sans que personne ne le voie.",
                        b.texture_count, b.retired_count, accounted, b.expect_textures);
        }
        printf("  textures : %zu employée(s), %zu retirée(s) et déclarée(s)\n",
               b.texture_count, b.retired_count);
    }

    gltf_scene scene;
    memset(&scene, 0, sizeof scene);
    scene.verts = (const gltf_vertex *)b.verts.data;
    scene.vert_count = b.verts.count;
    scene.meshes = (const gltf_mesh *)b.meshes.data;
    scene.mesh_count = b.meshes.count;
    scene.materials = b.materials;
    scene.material_count = b.material_count;
    scene.textures = b.textures;
    scene.texture_count = b.texture_count;
    scene.generator = "Nineteen V15 roomgen";
    scene.scene_name = "salle";

    const size_t bin_size = gltf_write(&scene, out_path);

    char base[256];
    tool_basename_noext(out_path, base, sizeof base);
    char dir[512];
    tool_dirname(out_path, dir, sizeof dir);

    char annex[800];
    snprintf(annex, sizeof annex, "%s%s.lights.json", dir, base);
    write_lights_json(&b, annex);
    snprintf(annex, sizeof annex, "%s%s.scene.json", dir, base);
    write_scene_json(&doc, root, &b, annex);

    tool_infof("salle : %zu objets, %zu triangles, %zu sommets, %zu matériaux, "
               "%zu textures, %zu lumières, %zu bornes, %zu lieux",
               b.meshes.count, b.triangle_count, b.verts.count,
               b.material_count, b.texture_count, b.light_count,
               b.cabinet_count, b.poi_count);
    tool_infof("emprise : (%.2f %.2f %.2f) à (%.2f %.2f %.2f) m, binaire %zu octets",
               (double)b.bounds.min.x, (double)b.bounds.min.y, (double)b.bounds.min.z,
               (double)b.bounds.max.x, (double)b.bounds.max.y, (double)b.bounds.max.z,
               bin_size);

    for (size_t i = 0; i < b.prim_blocks.count; ++i) {
        geo_primitives_free(&TOOL_VEC_AT(&b.prim_blocks, geo_primitives, i));
    }
    tool_vec_free(&b.prim_blocks);
    tool_vec_free(&b.meshes);
    tool_vec_free(&b.verts);
    tool_json_free(&doc);
    free(text);
    return 0;
}
