/*
 * room_viewmodel.c — où sont les mains, image par image.
 *
 * Aucun appel GPU ici, et c'est voulu : ce fichier se compile et se raisonne
 * sans périphérique de rendu, et les mêmes fonctions servent au jeu et aux
 * captures scriptées.
 *
 * Le repère de travail
 * --------------------
 * Tout ce qui décrit le corps est écrit en **espace caméra**, avec la convention
 * la plus bête possible :
 *
 *     monde = œil + droite · x + haut · y + avant · z
 *
 * donc x positif à droite, y positif en haut, z positif **devant**. Ce n'est pas
 * la convention OpenGL (où l'on regarde vers −Z) et c'est délibéré : ici on
 * n'écrit pas une matrice de projection, on décrit une épaule. « Vingt
 * centimètres devant » doit s'écrire `z = 0.20`, pas `z = -0.20`.
 */
#include "room_viewmodel.h"

#include "ns_env.h"

#include "ns_core.h"
#include "ns_ik.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Cotes du corps, en mètres
 * -------------------------------------------------------------------------- */

/*
 * Humérus 32 cm, avant-bras 27 cm, main 13,5 cm du poignet au bout du doigt.
 *
 * LA MAIN VALAIT 10 cm sous un commentaire qui annonçait « bras d'adulte ».
 * Dix centimètres du poignet au bout du doigt, c'est la PAUME seule d'un
 * adulte, ou la main entière d'un enfant de quatre ans — d'où deux petites
 * mains de poupée posées sur des commandes à l'échelle, ce qu'on voyait sans
 * savoir le nommer.
 *
 * 13,5 ET PAS 18,5, et c'est un compromis assumé plutôt qu'une cote anatomique.
 * `VM_HAND` sert partout comme distance poignet -> bout du doigt, y compris
 * dans la seconde passe d'IK qui POSE LE DOIGT sur sa cible : allonger la main
 * recule donc le poignet d'autant. Essayé à 18,5 — la vraie cote — le poignet
 * reculait de 8,5 cm et les deux mains sortaient par le bas d'un champ de
 * vision de 58°, dont la demi-ouverture verticale ne fait que 29°. Le résultat
 * était anatomiquement juste et visuellement pire : deux paquets de doigts au
 * ras du bord. Les viewmodels de jeu à la première personne sont
 * volontairement raccourcis pour cette raison exacte ; celui-ci l'est aussi,
 * et maintenant il le DIT au lieu de prétendre le contraire.
 */
/*
 * Elles viennent de `ns_viewmodel.h`, et ce n'est pas de la coquetterie : le
 * maillage de la main est modélisé en millimètres puis divisé par SA longueur,
 * et le shader remultiplie par CELLE-CI. Le produit doit être l'identité, sans
 * quoi les normales d'un doigt replié — qui, elles, ne sont pas rééchelonnées —
 * mentiraient sur l'orientation de la surface. Deux constantes recopiées
 * finissent toujours par diverger ; celles-ci ne le peuvent plus.
 */
/*
 * Le manche et l'avant-bras sont RÉGLABLES par `nineteen.env` ; la main NON, et
 * il faut dire pourquoi.
 *
 * Le maillage est modelé sur une longueur de 1 et le shader l'étire en Z par
 * `length[]`. Sur un tube c'est sans danger : ses normales sont radiales, donc
 * un étirement en Z ne les touche pas. Sur la MAIN, qui est modelée doigts
 * repliés, les normales ont une composante en Z — les étirer les fausserait, et
 * il faudrait une inverse transposée que le shader n'a pas.
 *
 * Une main se redimensionnerait donc UNIFORMÉMENT, ce que le pipeline ne sait
 * pas faire aujourd'hui. Plutôt qu'un réglage qui abîmerait l'éclairage sans le
 * dire, il n'y a pas de réglage. C'est écrit ici et dans `nineteen.env`.
 */
/*
 * Lues UNE FOIS, au démarrage, et gardées.
 *
 * Pas par économie — un `strcmp` sur quinze clés ne se mesure pas — mais parce
 * qu'une longueur de bras doit être STABLE pendant toute la session : elle
 * entre dans `VM_REACH`, dont on se sert pour décider qu'une commande est hors
 * d'atteinte et pour le dire dans le journal. Une valeur relue à chaque image
 * serait la même à chaque image, mais rien ne le garantirait.
 *
 * Et il y a une seconde raison, trouvée en mesurant : lues paresseusement,
 * elles ne l'étaient QUE si des bras étaient posés. Un rendu sans bras — une
 * capture en caméra libre — laissait donc le contrôle de réglages annoncer
 * « personne ne lit personnage.bras », ce qui est faux et ce qui aurait fini
 * par faire ignorer un avertissement juste.
 */
static float g_vm_upper = NS_VM_UPPER_M;
static float g_vm_fore  = NS_VM_FORE_M;

void room_viewmodel_read_env(void)
{
    g_vm_upper = ns_env_float("personnage.bras",      NS_VM_UPPER_M);
    g_vm_fore  = ns_env_float("personnage.avantBras", NS_VM_FORE_M);
}

#define VM_UPPER   g_vm_upper
#define VM_FORE    g_vm_fore
#define VM_HAND    NS_VM_HAND_M
#define VM_REACH   (VM_UPPER + VM_FORE)

/* Épaules, sous l'œil et un peu en arrière. */
#define VM_SHOULDER_X   0.185f
#define VM_SHOULDER_Y  (-0.260f)
#define VM_SHOULDER_Z  (-0.075f)

/*
 * Champ de vision du viewmodel, en degrés — plus étroit que celui de la scène.
 *
 * C'est ce qui empêche une main de s'étirer en approchant du bord du cadre, et
 * c'est aussi ce qui décide si on la voit tout court : à 58° la demi-ouverture
 * verticale est de 29°, donc tout ce qui descend de plus de 29° sous l'axe du
 * regard sort de l'image. Le premier jet posait les poignets à 55° sous l'axe —
 * anatomiquement juste pour des bras qui pendent, et parfaitement invisible.
 */
#define VM_FOV_Y  58.0f

/*
 * LE CHAMP PENDANT UNE PARTIE, ET POURQUOI IL EN FAUT UN SECOND.
 *
 * Le défaut, mesuré, et il coûtait cher : PENDANT UNE PARTIE ON NE VOYAIT PLUS
 * UNE SEULE MAIN. Sur `borne_arcade_3`, réglages livrés, le sommet du manche
 * est à **32,0 degrés** sous l'axe du regard, pour 29 de demi-ouverture à 58°.
 * Les deux mains tombaient donc trois degrés sous le bord bas du cadre — la
 * capture le montre sans discussion : la dalle joue toute seule, le panneau est
 * là, et personne n'a de mains. C'est exactement le défaut que `ROOM_VM_PLAY`
 * avait été ajouté pour corriger, revenu par une autre porte.
 *
 * La porte, la voici, et elle explique le chiffre : le POSTE DE JEU avance
 * l'œil de 27,7 cm vers la dalle dès qu'une partie tourne (`room_poste.h`). Le
 * corps ne bouge pas et les mains restent sur les commandes ; ce sont donc les
 * commandes qui descendent dans le champ — de 23,3° debout sur l'ancre à 32,0°
 * au poste. Le viewmodel n'a jamais su que le poste existait.
 *
 * 70° : 35 de demi-ouverture, soit **trois degrés de marge** sur les 32,0
 * mesurés. La marge n'est pas décorative — `poste.partHauteur` se règle dans
 * `nineteen.env`, et un poste plus profond descend encore les commandes.
 *
 * ELLE A DÉJÀ SERVI POUR UN DEGRÉ, et il faut le savoir avant d'en dépenser un
 * autre. La main gauche ne vise plus le sommet de la boule mais son CENTRE, un
 * rayon plus bas, parce qu'on la tient au lieu de la toucher : sur
 * `borne_arcade_3`, réglages livrés, la cible est passée de 32,04° à 33,02°
 * sous l'axe du regard. Il reste donc deux degrés, pas trois. Les deux mains
 * sont toujours entièrement dans le cadre — vérifié en capture, ce n'est pas
 * une déduction — mais le bout des doigts gauches touche le bord bas, et c'est
 * la marge qu'il faudra rouvrir si les commandes redescendent.
 *
 * POURQUOI SEULEMENT EN PARTIE, et pas partout. Le viewmodel est projeté par
 * SON champ pendant que la salle l'est par celui de la caméra : une main que
 * l'IK pose exactement sur un bouton n'atterrit pas sur ce bouton à l'écran, et
 * l'écart croît avec la différence des deux champs. Élargir en permanence
 * ferait passer cet écart de quatre degrés à huit — sur la fente et sur le
 * bouton, c'est-à-dire précisément là où l'on REGARDE si le doigt touche. En
 * partie, le manche et les boutons sont hors cadre de toute façon (le poste
 * resserre la salle à 46°, donc 23 de demi-ouverture, quand les commandes sont
 * à 32) : il n'y a rien à côté de quoi la main puisse tomber. L'élargissement
 * appartient donc à l'état qui crée le problème, et à lui seul.
 *
 * 8 par seconde : le poste s'établit en 0,55 s, et à cette vitesse il ne reste
 * que 1,2 % de l'écart au bout de ce temps-là. Le champ arrive donc AVEC le
 * poste. Un basculement franc de douze degrés se verrait comme un défaut de
 * rendu, ce que le poste s'est justement donné du mal à ne pas être.
 */
#define VM_FOV_JEU   70.0f
#define VM_FOV_DAMP  8.0f

/* Durées, en secondes. Elles sont courtes : un geste d'arcade est sec. */
#define VM_T_REACH   0.42f
#define VM_T_INSERT  0.55f
#define VM_T_PRESS   0.34f
#define VM_T_RETURN  0.45f

/*
 * LE COUP : 110 ms d'armé, 90 ms d'aller, 300 ms de retour. 500 en tout.
 *
 * L'ARMÉ EST UNE PHASE À PART ENTIÈRE, et il a fallu le mesurer pour s'en
 * apercevoir. Sans lui, le coup partait de la position de repos — laquelle
 * porte DÉJÀ les mains en avant, à 44 cm de l'œil pour 59 de portée. Le poing
 * n'avait donc que sept centimètres à gagner, et mesuré au poignet il n'en
 * gagnait que CINQ MILLIMÈTRES : le geste existait dans le code et ne se voyait
 * pas à l'écran.
 *
 * Avec l'armé, le poing recule d'abord à dix centimètres de l'œil, puis
 * traverse 49,3 cm. Les trois durées sont mesurées de la même façon, par la
 * distance sur le temps :
 *
 *   ARMÉ    34,6 cm en 110 ms = 3,1 m/s. On ramène le poing moins vite qu'on
 *           ne le lance : c'est un geste de préparation, pas un geste de force.
 *   ALLER   49,3 cm en 90 ms = 5,5 m/s, la plage d'un vrai coup de poing, que
 *           la littérature situe entre 5 et 9 m/s à l'impact selon
 *           l'entraînement. À 150 ms on tomberait à 3,3 m/s, la vitesse d'un
 *           geste qu'on POSE.
 *   RETOUR  300 ms, trois fois et demie l'aller. C'est ce qui donne le poids :
 *           un retour aussi vif que l'aller se lit comme un ressort.
 */
#define VM_T_HIT_ARME  0.110f
#define VM_T_HIT_OUT   0.090f
#define VM_T_HIT_BACK  0.300f
#define VM_T_HIT       (VM_T_HIT_ARME + VM_T_HIT_OUT + VM_T_HIT_BACK)

/*
 * L'INSTANT OÙ LE JETON BASCULE, dans la séquence complète.
 *
 * `insert_push` monte jusqu'à 55 % de `ROOM_VM_INSERT` puis redescend : c'est
 * là que la pièce cesse d'avancer dans la fente et que la main se retire. 55 %
 * de 550 ms font 302 ms après l'entrée dans l'état, soit 722 ms après le début
 * du geste — le chiffre est écrit ici pour qu'on n'ait pas à le recomposer
 * ailleurs, et surtout pour qu'il n'y ait qu'UN endroit à changer si la forme
 * de la poussée bouge.
 */
#define VM_INSERT_SOMMET  0.55f
#define VM_T_INSERT_DROP  (VM_T_INSERT * VM_INSERT_SOMMET)

/*
 * LA RELANCE : 220 ms d'aller, 180 de retour. 400 en tout.
 *
 * Les deux durées sont mesurées comme celles du coup, par la distance sur le
 * temps. Le poignet droit part du dessus du panneau — `panel_centre` relevé
 * d'un tiers de paume — et va à la fente, `coin_slot` plus une paume le long de
 * la normale : **40,8 cm**, et la cote est la même sur les dix-neuf bornes,
 * leurs ancres étant dérivées d'une seule géométrie.
 *
 *   ALLER   40,8 cm en 220 ms = **1,85 m/s**. Le geste d'une main qui sait où
 *           est la fente : franc, mais on ne jette pas une pièce.
 *   RETOUR  40,8 cm en 180 ms = **2,27 m/s**, donc PLUS VITE que l'aller.
 *
 * C'est l'exact contraire du coup de poing, dont le retour dure trois fois et
 * demie l'aller, et la raison est la même dans les deux cas : un coup s'ACHÈVE,
 * donc il retombe ; une insertion est le DÉBUT d'autre chose, et la partie
 * tourne déjà quand la main revient. Un retour traînant ici se lirait comme une
 * hésitation devant un jeu qu'on vient de relancer.
 *
 * Et surtout : 400 ms contre les 1 310 de `REACH` + `INSERT` + `PRESS`. Le
 * raisonnement complet est dans `room_viewmodel.h`, à `ROOM_VM_RELANCE`.
 */
#define VM_T_RELANCE_OUT   0.220f
#define VM_T_RELANCE_BACK  0.180f
#define VM_T_RELANCE       (VM_T_RELANCE_OUT + VM_T_RELANCE_BACK)
#define VM_RELANCE_SOMMET  (VM_T_RELANCE_OUT / VM_T_RELANCE)

/*
 * L'ARMÉ et le COUP PORTÉ, en espace caméra.
 *
 * L'armé est le poing ramené près des côtes, décalé vers la droite et bas :
 * c'est de là qu'un coup part. Le coup porté ramène la main vers l'AXE du
 * regard — on frappe ce qu'on regarde, pas ce qui est à côté — et la relève de
 * onze centimètres, ce qui l'amène à six sous l'œil : la hauteur d'une dalle de
 * borne quand on est planté devant.
 *
 * 55 cm devant, et le chiffre a une limite dure : `VM_REACH` vaut 59 cm depuis
 * l'épaule, laquelle est 7,5 cm DERRIÈRE l'œil. Viser plus loin ferait tendre
 * la chaîne à fond et donnerait le tube rectiligne que `rest_wrist` explique
 * déjà comment éviter.
 */
#define VM_HIT_ARME    ns_v3_make( 0.300f, -0.170f, 0.100f)
#define VM_HIT_PORTE   ns_v3_make( 0.130f, -0.060f, 0.550f)

/*
 * Les deux vitesses de rattrapage du poignet PENDANT le coup, et pourquoi elles
 * ne peuvent pas être `VM_DAMP`.
 *
 * `ns_damp` laisse un reliquat de `exp(-k t)`. À 16 par seconde — le rattrapage
 * ordinaire — il reste 24 % du chemin au bout des 90 ms de l'aller : le poing
 * s'arrêterait à trois quarts de course, c'est-à-dire douze centimètres avant
 * la machine, et le coup ne toucherait rien.
 *
 * LE COUP EST LE SEUL GESTE DE CE FICHIER QUI NE SOIT PAS LISSÉ DU TOUT, et
 * c'est la raison même pour laquelle le lissage existe qui l'exclut : il est là
 * « parce que la machine à états donne une cible par étape, jamais une
 * trajectoire ». Le coup, lui, EST une trajectoire — armé, aller, retour, avec
 * sa courbe et ses durées mesurées. Lui appliquer un rattrapage exponentiel ne
 * l'arrondit pas, ça le retarde.
 *
 * Et le retard n'est pas cosmétique. La cible file à dix mètres par seconde à
 * l'instant de l'impact ; un lissage à 60 par seconde laisse donc un retard
 * permanent de v/k, soit SEIZE CENTIMÈTRES. Mesuré au poignet, le poing
 * n'atteignait jamais sa cible : il culminait exactement sur la position de
 * repos, et le coup ne dépassait pas les mains qu'on porte déjà en avant.
 *
 * LES TROIS PHASES SONT DONC POSÉES, retour compris — et le retour l'est pour
 * une seconde raison, découverte par la même mesure : la fin de l'aller tombe
 * ENTRE deux pas de simulation, donc le poing ne touchait jamais sa cible. Voir
 * le commentaire du retour dans `room_viewmodel_tick`.
 *
 * Ce que le retour garde du lissage, c'est sa FORME : sa courbe est un
 * `smoothstep`, qui part et arrive à vitesse nulle. Ça donne le poids d'un bras
 * qui retombe, et non le rappel d'un ressort — c'est ce qu'on cherchait, sans
 * le retard qu'un amortissement aurait apporté.
 */

/* Décroissance du choc. 9 par seconde : 5 % au bout de 330 ms, donc l'écran
 * s'est calmé peu après que le bras est revenu. L'ordre compte — un écran qui
 * tremble encore quand le bras est au repos se lit comme une panne, pas comme
 * un coup. */
#define VM_CHOC_DAMP  9.0f

/* Vitesse de rattrapage des poignets. Assez haute pour que le bras suive la
 * cible, assez basse pour que le départ et l'arrivée soient ronds. */
#define VM_DAMP  16.0f

/*
 * LA FOULÉE vient maintenant de la caméra, par `room_camera_stride`.
 *
 * Elle était recopiée ici — 1,55 m, en dur — pour mettre le contre-balancement
 * des bras sur les pas de la caméra, et la duplication était assumée. Elle ne
 * peut plus l'être : `personnage.foulee` règle cette valeur sans recompiler, et
 * une copie en dur ferait balancer les bras à contretemps des jambes dès que
 * quelqu'un touche au réglage. La valeur par défaut n'a pas changé, donc le
 * comportement non plus. */

enum {
    VM_FORCE_NONE = -1,
    VM_FORCE_IDLE = 0,
    VM_FORCE_WALK,
    VM_FORCE_REACH,
    VM_FORCE_INSERT,
    VM_FORCE_PRESS,
    VM_FORCE_HIT
};

/* --------------------------------------------------------------------------
 * Outils
 * -------------------------------------------------------------------------- */

static float smoothstep01(float t)
{
    t = ns_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float state_duration(room_vm_state s)
{
    switch (s) {
        case ROOM_VM_REACH:  return VM_T_REACH;
        case ROOM_VM_INSERT: return VM_T_INSERT;
        case ROOM_VM_PRESS:  return VM_T_PRESS;
        /* ROOM_VM_PLAY n'a pas de durée : il dure la partie. Renvoyer 0 le fait
         * tomber dans la branche « pas d'avancement » de `tick`, ce qui est
         * exactement le comportement voulu. */
        case ROOM_VM_RETURN: return VM_T_RETURN;
        case ROOM_VM_HIT:    return VM_T_HIT;
        case ROOM_VM_RELANCE: return VM_T_RELANCE;
        default:             return 0.0f;
    }
}

/* Le repère caméra, reconstruit depuis la caméra résolue. Le « haut » vient de
 * `cam.up`, pas de l'axe du monde : il porte déjà le roulis en virage, et les
 * bras doivent rouler avec le corps. */
typedef struct vm_basis { ns_v3 eye, right, up, fwd; } vm_basis;

static vm_basis basis_of(const ns_camera *cam)
{
    vm_basis b;
    b.eye = cam->position;
    b.fwd = ns_v3_norm(cam->forward);
    b.right = ns_v3_norm(ns_v3_cross(b.fwd, cam->up));
    b.up = ns_v3_cross(b.right, b.fwd);
    return b;
}

static ns_v3 to_world(const vm_basis *b, ns_v3 p)
{
    ns_v3 w = b->eye;
    w = ns_v3_add(w, ns_v3_scale(b->right, p.x));
    w = ns_v3_add(w, ns_v3_scale(b->up, p.y));
    w = ns_v3_add(w, ns_v3_scale(b->fwd, p.z));
    return w;
}

static ns_v3 to_camera(const vm_basis *b, ns_v3 w)
{
    const ns_v3 d = ns_v3_sub(w, b->eye);
    return ns_v3_make(ns_v3_dot(d, b->right), ns_v3_dot(d, b->up), ns_v3_dot(d, b->fwd));
}

/*
 * Ramène une cible dans la sphère d'atteinte de l'épaule.
 *
 * Sans ce garde-fou l'IK renvoie un bras tendu vers un point qu'il n'atteint
 * pas — ce qui est correct, mais donne une main figée à mi-course pendant que la
 * machine à états croit le geste terminé. Avec, la main s'arrête franchement à
 * bout de bras, et l'écart restant se lit comme un joueur qui ne se penche pas
 * assez : c'est faux mais lisible, là où l'autre est faux et illisible.
 */
static ns_v3 clamp_reach(ns_v3 shoulder, ns_v3 target, float max_len)
{
    const ns_v3 d = ns_v3_sub(target, shoulder);
    const float len = ns_v3_len(d);
    if (len <= max_len || len < 1e-5f) return target;
    return ns_v3_add(shoulder, ns_v3_scale(d, max_len / len));
}

/* --------------------------------------------------------------------------
 * Cycle de vie
 * -------------------------------------------------------------------------- */

void room_viewmodel_init(room_viewmodel *vm)
{
    memset(vm, 0, sizeof *vm);
    vm->state = ROOM_VM_IDLE;
    vm->forced_pose = VM_FORCE_NONE;
    vm->token_visible = false;
    /* Le champ part à sa valeur de repos et non à zéro : un `memset` le mettrait
     * à 0, et le rattrapage exponentiel ferait alors s'ouvrir le cadre depuis
     * rien pendant la première demi-seconde de jeu. */
    vm->fov = vm->prev_fov = VM_FOV_Y;
}

bool room_viewmodel_set_forced_pose(room_viewmodel *vm, const char *name)
{
    static const struct { const char *name; int value; } table[] = {
        { "idle",   VM_FORCE_IDLE   },
        { "repos",  VM_FORCE_IDLE   },
        { "walk",   VM_FORCE_WALK   },
        { "marche", VM_FORCE_WALK   },
        { "reach",  VM_FORCE_REACH  },
        { "insert", VM_FORCE_INSERT },
        { "press",  VM_FORCE_PRESS  },
        /* Le coup, sous ses deux noms : ce dépôt est en français, mais les cinq
         * poses qui précèdent sont nommées en anglais depuis A7 et des captures
         * de référence portent ces noms-là. On ajoute donc les deux plutôt que
         * de choisir, ce qui ne coûte qu'une ligne. */
        { "frappe", VM_FORCE_HIT    },
        { "hit",    VM_FORCE_HIT    },
    };
    if (!name || !name[0]) return false;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; ++i) {
        if (SDL_strcasecmp(name, table[i].name) == 0) {
            vm->forced_pose = table[i].value;
            return true;
        }
    }
    return false;
}

const ns_cabinet *room_viewmodel_target(const ns_scene *scene, const room_camera *cam)
{
    if (!scene || !cam || cam->mode != ROOM_CAM_PLAYER) return NULL;
    /*
     * `ns_scene_nearest_cabinet` est écrite depuis M4 et n'avait jamais été
     * appelée. Elle compare à `player_anchor`, qui est à 70 cm de l'écran : un
     * rayon de 1,1 m couvre donc le joueur planté devant la borne et lui seul,
     * sans attraper la voisine dos à dos, qui est à 1,7 m de là.
     */
    ns_v3 feet = cam->position;
    feet.y -= cam->eye_height;
    return ns_scene_nearest_cabinet(scene, feet, 1.10f);
}

bool room_viewmodel_interact(room_viewmodel *vm, const ns_cabinet *cab)
{
    if (!vm || !cab) return false;
    if (vm->state != ROOM_VM_IDLE) return false;   /* un geste à la fois */

    vm->target_coin   = cab->coin_slot;
    vm->target_panel  = cab->panel_centre;
    vm->target_stick  = cab->stick_top;
    vm->target_normal = cab->screen_normal;
    vm->has_target    = true;
    vm->state         = ROOM_VM_REACH;
    vm->elapsed       = 0.0f;
    vm->token_visible = true;
    return true;
}

void room_viewmodel_start_playing(room_viewmodel *vm, const ns_cabinet *cab)
{
    if (!vm || !cab) return;
    vm->target_coin   = cab->coin_slot;
    vm->target_panel  = cab->panel_centre;
    vm->target_stick  = cab->stick_top;
    vm->target_normal = cab->screen_normal;
    vm->has_target    = true;
    vm->state         = ROOM_VM_PLAY;
    vm->elapsed       = 0.0f;
    vm->token_visible = false;   /* le jeton est dans la machine */
    vm->reach_checked = false;
}

void room_viewmodel_stop_playing(room_viewmodel *vm)
{
    if (!vm || vm->state != ROOM_VM_PLAY) return;
    vm->state   = ROOM_VM_RETURN;
    vm->elapsed = 0.0f;
    vm->tap     = 0.0f;
}

void room_viewmodel_tap(room_viewmodel *vm)
{
    if (vm && vm->state == ROOM_VM_PLAY) vm->tap = 1.0f;
}

bool room_viewmodel_frappe(room_viewmodel *vm, const ns_cabinet *cab)
{
    if (!vm) return false;
    /*
     * DEUX états seulement autorisent le coup, et la liste est courte exprès :
     * les mains vides, ou en pleine partie. Frapper au milieu de la séquence du
     * jeton demanderait au même poignet de viser la fente et la tôle, et le
     * lissage ferait la moyenne des deux — une main qui part en diagonale vers
     * un point qui n'existe pas.
     */
    if (vm->state != ROOM_VM_IDLE && vm->state != ROOM_VM_PLAY) return false;

    vm->hit_from_play = (vm->state == ROOM_VM_PLAY);
    vm->hit_impact    = false;
    vm->hit_material  = cab ? cab->screen_material : -1;
    vm->hit_point     = cab ? cab->screen_center : ns_v3_zero();
    vm->state         = ROOM_VM_HIT;
    vm->elapsed       = 0.0f;
    /* Le jeton disparaît : on ne cogne pas une machine une pièce à la main. En
     * pratique il n'est jamais visible ici — les deux états qui l'affichent
     * sont justement ceux qu'on vient de refuser — mais laisser l'état
     * dépendre de ce raisonnement-là serait le laisser dépendre d'un autre
     * fichier. */
    vm->token_visible = false;
    return true;
}

bool room_viewmodel_is_hitting(const room_viewmodel *vm)
{
    return vm && vm->state == ROOM_VM_HIT;
}

bool room_viewmodel_relance(room_viewmodel *vm)
{
    /*
     * UNIQUEMENT depuis `ROOM_VM_PLAY`, et la liste est courte exprès : ce
     * geste part des commandes et y revient. Le déclencher les mains vides
     * ferait viser une fente à une main qui n'est nulle part, et le déclencher
     * pendant la séquence complète insérerait deux pièces pour un jeton.
     *
     * `has_target` est exigé pour la même raison que le reste du fichier : la
     * scène peut avoir été rechargée sous nos pieds.
     */
    if (!vm || vm->state != ROOM_VM_PLAY || !vm->has_target) return false;

    vm->state         = ROOM_VM_RELANCE;
    vm->elapsed       = 0.0f;
    vm->prev_elapsed  = 0.0f;
    vm->token_drop    = false;
    vm->token_visible = true;   /* la pièce réapparaît dans la main droite */
    vm->tap           = 0.0f;   /* l'index lâche les boutons le temps du geste */
    return true;
}

bool room_viewmodel_take_impact(room_viewmodel *vm, ns_v3 *point, int32_t *material)
{
    if (!vm || !vm->hit_impact) return false;
    vm->hit_impact = false;
    if (point)    *point = vm->hit_point;
    if (material) *material = vm->hit_material;
    return true;
}

bool room_viewmodel_take_token(room_viewmodel *vm, ns_v3 *at)
{
    if (!vm || !vm->token_drop) return false;
    vm->token_drop = false;
    if (at) *at = vm->target_coin;
    return true;
}

float room_viewmodel_choc(const room_viewmodel *vm, float alpha)
{
    if (!vm) return 0.0f;
    return ns_clampf(ns_lerpf(vm->prev_choc, vm->choc, alpha), 0.0f, 1.0f);
}

/*
 * L'avancement du coup, en une seule expression parce qu'il n'a pas d'état à
 * lui : il se relit entièrement depuis `elapsed`.
 *
 * L'aller est en `k * k` et non linéaire : un poing ACCÉLÈRE et s'arrête net
 * sur la tôle. Sa vitesse est donc maximale à l'impact, ce qui est la seule
 * façon qu'un coup se lise comme un coup. Le retour, lui, est en cosinus
 * adouci : rien ne le porte, il retombe.
 */
static float hit_amount(const room_viewmodel *vm, float elapsed)
{
    if (!vm || vm->state != ROOM_VM_HIT) return 0.0f;
    /*
     * L'ARMÉ vaut ZÉRO ici, et ce n'est pas un oubli : cette valeur mesure
     * l'EXTENSION du bras, pas l'avancement du geste. Pendant l'armé le bras se
     * replie, il ne s'étend pas.
     *
     * Ce que ça donne en troisième personne est exactement ce qu'on veut sans
     * qu'on ait rien à écrire : `ns_skin_allure.frappe` plie le coude en
     * `sin(pi f)`, donc l'armé du personnage vu de dos se fabrique tout seul
     * pendant la montée de l'aller. Le poing de la première personne, lui, a sa
     * propre courbe, parce que lui doit vraiment reculer avant de partir.
     */
    if (elapsed < VM_T_HIT_ARME) return 0.0f;
    const float e = elapsed - VM_T_HIT_ARME;
    if (e < VM_T_HIT_OUT) {
        const float k = ns_clampf(e / VM_T_HIT_OUT, 0.0f, 1.0f);
        return k * k;
    }
    const float k = ns_clampf((e - VM_T_HIT_OUT) / VM_T_HIT_BACK, 0.0f, 1.0f);
    return 1.0f - smoothstep01(k);
}

/*
 * LA POUSSÉE D'ÉPAULE DU COUP — une fonction PURE, jamais mémorisée.
 *
 * Sans elle, seul l'avant-bras se déplie et le coup se lit comme une gifle : un
 * corps qui frappe entre dans le geste, et c'est ce qui distingue frapper de
 * montrer du doigt.
 *
 * ELLE A D'ABORD ÉTÉ ÉCRITE DANS `vm->lean`, ET C'ÉTAIT FAUX. `lean` est
 * AMORTI vers sa cible à chaque pas : ajouter quoi que ce soit APRÈS
 * l'amortissement laisse la valeur ajoutée dans l'état, où le pas suivant la
 * reprend comme point de départ et la ré-amortit. Le terme ne s'ajoute donc pas
 * une fois, il s'INTÈGRE.
 *
 * Ce n'est pas une subtilité théorique, c'est un facteur neuf : à 9 par seconde
 * et 120 Hz, un pas ne mange que 7,2 % de l'excès, et les quarante-sept pas du
 * geste empilent 7,5 cm de poussée en SOIXANTE-CINQ CENTIMÈTRES d'épaule
 * projetée en avant. Le joueur voyait son propre bras partir hors du cadre.
 *
 * La forme corrigée ne peut plus l'être : elle ne lit rien, n'écrit rien, et se
 * recalcule entièrement depuis l'avancement du geste au moment de poser.
 */
static ns_v3 hit_push(float k)
{
    /* Vers l'avant surtout, un peu vers le haut, et un rien vers l'intérieur :
     * une épaule qui part en avant rentre aussi vers l'axe du corps. */
    return ns_v3_scale(ns_v3_make(-0.020f, 0.010f, 0.075f), k);
}

float room_viewmodel_frappe_amount(const room_viewmodel *vm, float alpha)
{
    if (!vm || vm->state != ROOM_VM_HIT) return 0.0f;
    /*
     * L'INTERPOLATION SE FAIT SUR LE TEMPS, pas sur la valeur.
     *
     * Interpoler entre `hit_amount(elapsed - dt)` et `hit_amount(elapsed)`
     * donnerait la corde de la courbe au lieu de la courbe, ce qui rabote
     * précisément le sommet — c'est-à-dire l'instant de l'impact, le seul qui
     * compte. C'est le même argument que celui qui a fait sortir l'oscillation
     * de la vue du pas de simulation ; voir `room_camera_resolve`.
     */
    return hit_amount(vm, ns_lerpf(vm->prev_elapsed, vm->elapsed,
                                   ns_clampf(alpha, 0.0f, 1.0f)));
}

bool room_viewmodel_is_playing(const room_viewmodel *vm)
{
    return vm && vm->state == ROOM_VM_PLAY;
}

/* --------------------------------------------------------------------------
 * Cibles, par état
 * -------------------------------------------------------------------------- */

/*
 * Se pencher. Le joueur est arrêté à 1,08 m du centre de la borne — la collision
 * en capsule ne le laisse pas approcher davantage, le panneau de commande
 * débordant de 25 cm. À cette distance le bouton est à 72 cm de l'épaule et la
 * fente à 84 cm, pour un bras de 67 cm : sans se pencher, on n'atteint ni l'un
 * ni l'autre.
 *
 * On penche donc l'épaule et non la caméra. C'est une simplification, et je la
 * dis : un vrai jeu inclinerait aussi la vue de quelques degrés. La faire bouger
 * ici demanderait que la caméra connaisse la machine à états, ce qui est
 * exactement la dépendance que ce découpage évite — et une vue qui plonge
 * pendant qu'on ne l'a pas demandé est une des rares choses qui donnent mal au
 * cœur.
 */
static ns_v3 lean_for(room_vm_state s, float t)
{
    const float k = smoothstep01(t);
    switch (s) {
        case ROOM_VM_REACH:  return ns_v3_scale(ns_v3_make(0.0f, -0.10f, 0.14f), k);
        case ROOM_VM_INSERT: return ns_v3_make(0.0f, -0.28f, 0.34f);
        case ROOM_VM_PRESS:  return ns_v3_make(0.0f, -0.10f, 0.16f);
        /* Penché en avant, et il y reste : c'est la posture de quelqu'un qui
         * joue, et c'est aussi ce qui met les commandes à portée. Sans ce
         * décalage d'épaules, les deux poignets butent sur la limite de 57 cm et
         * les bras restent tendus comme deux barres. */
        /*
         * 42 cm en avant, et le chiffre est mesuré, pas choisi : à 32 cm le
         * contrôle de portée annonçait « boutons à 0,61 m pour 0,57 m de
         * portée » — quatre centimètres de trop, donc une main droite tendue
         * juste au-dessus des boutons sans jamais les toucher. C'est aussi la
         * posture réelle : on se penche SUR un panneau d'arcade, on ne joue pas
         * bras tendus.
         */
        case ROOM_VM_PLAY:   return ns_v3_make(0.0f, -0.16f, 0.42f);
        /* LA MÊME que celle du jeu, et c'est tout l'intérêt du geste court : le
         * corps ne se redresse pas pour remettre une pièce. Lui donner sa
         * propre inclinaison ferait osciller les épaules à chaque relance, ce
         * qui est précisément l'attente qu'on cherche à supprimer. */
        case ROOM_VM_RELANCE: return ns_v3_make(0.0f, -0.16f, 0.42f);
        case ROOM_VM_RETURN: return ns_v3_scale(ns_v3_make(0.0f, -0.10f, 0.16f), 1.0f - k);
        default:             return ns_v3_zero();
    }
}

/*
 * Le poignet au repos, en espace caméra.
 *
 * Mains **relevées et portées en avant**, pas des bras qui pendent le long du
 * corps. Ce n'est pas un arrangement pour la caméra : c'est la posture de
 * quelqu'un qui marche dans une salle d'arcade en s'apprêtant à jouer, et c'est
 * aussi la seule qui tienne dans le cadre — à 22° sous l'axe du regard, les
 * poignets entrent par le bas de l'image et les avant-bras par les coins, ce qui
 * est exactement la lecture qu'on attend d'une vue subjective.
 *
 * La distance à l'épaule compte autant que l'angle : à 0,61 m pour un bras de
 * 0,57 m, l'IK tend la chaîne à fond et les deux bras deviennent des tubes
 * rectilignes. 0,52 m laisse le coude fléchi, et c'est le coude qui fait lire
 * un bras comme un bras.
 *
 * Des bras pendants (le premier jet : y −0,455 pour z 0,315, soit 55° sous
 * l'axe) sont plus justes anatomiquement et totalement hors champ. Entre les
 * deux, c'est la posture qui cède, pas le cadre.
 */
static ns_v3 rest_wrist(bool right)
{
    return ns_v3_make(right ? 0.246f : -0.236f, -0.200f, 0.440f);
}

/*
 * Le contre-balancement de marche.
 *
 * La phase vient de `bob.distance` et non du temps — c'est le même chiffre qui
 * pilote l'oscillation de la vue, donc les bras tombent sur les pas par
 * construction plutôt que par réglage. À vitesse moitié, deux fois moins de
 * balancements dans la même durée, et personne n'a à s'en occuper.
 */
static ns_v3 walk_swing(bool right, const room_view_bob *bob, float stride)
{
    const float phase = (bob->distance / ns_maxf(stride, 1e-3f)) * NS_TAU;
    const float s = sinf(phase) * (right ? 1.0f : -1.0f);
    const float a = ns_clampf(bob->amount, 0.0f, 1.0f);

    /* En avant/en arrière surtout, un peu de haut/bas, un rien vers l'intérieur
     * quand le bras part en avant : un bras qui balance dans un plan strict est
     * un bras de pantin. */
    return ns_v3_make(-s * 0.035f * a, fabsf(s) * 0.022f * a, s * 0.115f * a);
}

/* La cible du poignet droit pendant la séquence, en espace **monde**. */
static ns_v3 sequence_wrist(const room_viewmodel *vm, ns_v3 shoulder_world, float t)
{
    const float k = smoothstep01(t);
    const ns_v3 n = ns_v3_norm(vm->target_normal);

    switch (vm->state) {
        case ROOM_VM_REACH: {
            /* De la position de repos vers 12 cm devant la fente : on approche,
             * on ne vise pas encore. Le repos est déjà porté par le lissage, donc
             * il suffit de donner le point d'arrivée et de le pondérer. */
            const ns_v3 hold = ns_v3_add(vm->target_coin, ns_v3_scale(n, 0.12f + VM_HAND));
            return ns_v3_lerp(shoulder_world, hold, 0.30f + 0.70f * k);
        }
        case ROOM_VM_INSERT: {
            /* Trois temps dans un seul état : présenter, pousser, retirer. Un
             * état par temps ne donnerait rien de plus qu'un fichier plus long. */
            const float push = (k < 0.55f) ? (k / 0.55f) : (1.0f - (k - 0.55f) / 0.45f);
            const float gap = 0.10f - 0.085f * ns_clampf(push, 0.0f, 1.0f);
            return ns_v3_add(vm->target_coin, ns_v3_scale(n, gap + VM_HAND));
        }
        case ROOM_VM_PRESS: {
            /* Le doigt descend sur le bouton. `press_depth` fait le dernier
             * centimètre ; ici on amène la main au-dessus. */
            const ns_v3 above = ns_v3_add(vm->target_panel,
                                          ns_v3_make(0.0f, VM_HAND + 0.02f, 0.0f));
            return ns_v3_add(above, ns_v3_scale(n, 0.02f));
        }
        case ROOM_VM_RELANCE: {
            /*
             * L'aller-retour de la relance, entre les DEUX points exacts où la
             * main droite se tient déjà par ailleurs : sa place de jeu
             * au-dessus des boutons, et la fente. Les recopier ici plutôt que
             * d'inventer un troisième point est ce qui garantit que le geste
             * part de là où la main est et y revient — sans quoi le lissage
             * ferait un saut visible à l'entrée et à la sortie de l'état.
             *
             * Le sommet est à `VM_RELANCE_SOMMET` et non à 0,5 : l'aller dure
             * 220 ms et le retour 180, donc le point de rebroussement n'est pas
             * au milieu du temps.
             */
            const ns_v3 up    = ns_v3_make(0.0f, VM_HAND * 0.35f, 0.0f);
            const ns_v3 above = ns_v3_add(vm->target_panel, up);
            const ns_v3 slot  = ns_v3_add(vm->target_coin,
                                          ns_v3_scale(n, 0.015f + VM_HAND));
            const float p = (t < VM_RELANCE_SOMMET)
                          ? (t / VM_RELANCE_SOMMET)
                          : (1.0f - (t - VM_RELANCE_SOMMET) / (1.0f - VM_RELANCE_SOMMET));
            return ns_v3_lerp(above, slot, smoothstep01(ns_clampf(p, 0.0f, 1.0f)));
        }
        default:
            return shoulder_world;
    }
}

/* --------------------------------------------------------------------------
 * Pas de simulation
 * -------------------------------------------------------------------------- */

void room_viewmodel_tick(room_viewmodel *vm, const room_camera *cam, float dt)
{
    if (!vm || !cam) return;

    vm->prev_wrist_l = vm->wrist_l;
    vm->prev_wrist_r = vm->wrist_r;
    vm->prev_lean = vm->lean;
    vm->prev_press_depth = vm->press_depth;
    vm->prev_tap = vm->tap;
    vm->prev_insert_push = vm->insert_push;
    vm->prev_choc = vm->choc;
    vm->prev_elapsed = vm->elapsed;
    vm->prev_fov = vm->fov;
    vm->clock += dt;

    /* Le choc retombe tout seul, qu'on soit encore en train de frapper ou non :
     * il survit au geste, et c'est voulu — la dalle continue de dérailler une
     * fraction de seconde après que le poing est reparti. */
    vm->choc = ns_damp(vm->choc, 0.0f, VM_CHOC_DAMP, dt);

    if (vm->forced_pose != VM_FORCE_NONE) {
        /*
         * Pose figée pour les captures. On se place à 82 % de l'animation, pas à
         * 100 % : la fin d'un geste est déjà le début du retour, et une capture
         * prise là montre une main en train de partir. 82 % est le moment où le
         * geste est le plus lisible.
         */
        switch (vm->forced_pose) {
            case VM_FORCE_REACH:  vm->state = ROOM_VM_REACH;  break;
            case VM_FORCE_INSERT: vm->state = ROOM_VM_INSERT; break;
            case VM_FORCE_PRESS:  vm->state = ROOM_VM_PRESS;  break;
            case VM_FORCE_HIT:    vm->state = ROOM_VM_HIT;    break;
            default:              vm->state = ROOM_VM_IDLE;   break;
        }
        /*
         * 82 % DE LA DURÉE, SAUF POUR LE COUP.
         *
         * Les quatre autres gestes sont à peu près symétriques : leur instant
         * le plus lisible est vers la fin, avant que le retour ne commence. Le
         * coup ne l'est pas du tout — 110 ms d'armé, 90 d'aller, 300 de retour
         * — et 82 % de sa durée tombe au MILIEU DU RETOUR, bras à moitié
         * revenu. Une capture prise là ne montre pas un coup de poing, elle
         * montre quelqu'un qui range son bras.
         *
         * On le fige donc à l'IMPACT, qui est le seul instant qu'on veuille
         * photographier d'un geste asymétrique.
         */
        vm->elapsed = (vm->state == ROOM_VM_HIT)
            ? (VM_T_HIT_ARME + VM_T_HIT_OUT)
            : state_duration(vm->state) * 0.82f;
        vm->token_visible = (vm->state == ROOM_VM_REACH || vm->state == ROOM_VM_INSERT);
    } else if (vm->state == ROOM_VM_PLAY) {
        /* `elapsed` avance aussi en jeu — il ne déclenche simplement aucune
         * transition. Il sert au contrôle de portée, qui doit attendre que le
         * penchement amorti soit arrivé : mesuré au premier pas, il donnerait la
         * distance de la posture debout et crierait au loup à chaque partie. */
        vm->elapsed += dt;
    } else if (vm->state != ROOM_VM_IDLE) {
        /*
         * `ROOM_VM_PLAY` est explicitement exclu, et l'oubli coûtait cher : sa
         * durée valant zéro, `elapsed >= dur` est vrai dès le premier pas, la
         * branche par défaut du `switch` le renvoyait à `IDLE` en effaçant sa
         * cible, et les mains repartaient le long du corps avant d'avoir touché
         * une commande. Un état sans durée doit être écarté de l'avancement, pas
         * lui être soumis avec une durée nulle.
         */
        vm->elapsed += dt;

        /*
         * L'IMPACT : le pas de simulation qui FRANCHIT la fin de l'aller.
         *
         * Un front et non un seuil, et la différence n'est pas théorique : à
         * 120 Hz un pas fait 8,3 ms, l'aller en fait 90, donc onze pas
         * remplissent la condition « elapsed >= 90 ms ». Testé comme un seuil,
         * le son partirait onze fois.
         */
        if (vm->state == ROOM_VM_HIT
            && vm->prev_elapsed < (VM_T_HIT_ARME + VM_T_HIT_OUT)
            && vm->elapsed >= (VM_T_HIT_ARME + VM_T_HIT_OUT)) {
            vm->hit_impact = true;
            vm->choc = 1.0f;
        }

        /*
         * LE JETON QUI BASCULE, et c'est le même mécanisme que l'impact : le
         * pas de simulation qui FRANCHIT le sommet de la poussée, pas un seuil
         * testé à chaque image. À 120 Hz, `elapsed >= 302 ms` est vrai pendant
         * les 248 ms qui restent de l'état, soit trente pas — le son partirait
         * trente fois.
         *
         * Les deux gestes qui insèrent une pièce le lèvent, et eux seuls. Le
         * sommet de chacun est ailleurs parce que leurs courbes sont
         * différentes : 55 % pour l'insertion complète, 220 ms sur 400 pour la
         * relance. Ce sont les deux mêmes constantes qui décident de la forme
         * du geste, donc l'image et le son ne peuvent pas diverger.
         *
         * Le jeton DISPARAÎT ici et non à la fin de l'état. Il tombe dans le
         * mécanisme à cet instant précis ; le laisser dans les doigts pendant
         * que la main se retire montrerait une pièce qu'on vient d'entendre
         * tomber.
         */
        {
            const float sommet = (vm->state == ROOM_VM_INSERT)  ? VM_T_INSERT_DROP
                               : (vm->state == ROOM_VM_RELANCE) ? VM_T_RELANCE_OUT
                               : -1.0f;
            if (sommet > 0.0f && vm->prev_elapsed < sommet && vm->elapsed >= sommet) {
                vm->token_drop    = true;
                vm->token_visible = false;
            }
        }

        const float dur = state_duration(vm->state);
        if (vm->elapsed >= dur) {
            vm->elapsed -= dur;
            switch (vm->state) {
                case ROOM_VM_REACH:  vm->state = ROOM_VM_INSERT; break;
                /* `token_visible` est déjà faux ici : il est retombé au sommet
                 * de la poussée, avec le front. */
                case ROOM_VM_INSERT: vm->state = ROOM_VM_PRESS;  break;
                case ROOM_VM_PRESS:  vm->state = ROOM_VM_RETURN; break;
                /*
                 * Le coup rend les mains AUX COMMANDES s'il les y a prises.
                 * Retomber sur `ROOM_VM_IDLE` laisserait la partie tourner
                 * devant des bras le long du corps — le défaut que
                 * `ROOM_VM_PLAY` avait justement été ajouté pour corriger, et
                 * qu'un état de plus rouvrirait par la bande.
                 */
                /*
                 * La relance rend les mains AUX COMMANDES, comme le coup quand
                 * il les y avait prises — et sans repasser par le contrôle de
                 * portée, qui a déjà eu lieu à l'entrée en jeu. Sans
                 * `has_target`, il ne reste rien à viser : on retombe au repos
                 * plutôt que de jouer devant une borne qui n'existe plus.
                 */
                case ROOM_VM_RELANCE:
                    if (vm->has_target) {
                        vm->state = ROOM_VM_PLAY;
                        vm->reach_checked = true;
                    } else {
                        vm->state = ROOM_VM_IDLE;
                    }
                    vm->token_visible = false;
                    vm->elapsed = 0.0f;
                    break;
                case ROOM_VM_HIT:
                    if (vm->hit_from_play && vm->has_target) {
                        vm->state = ROOM_VM_PLAY;
                        vm->reach_checked = true;   /* déjà contrôlé à l'entrée */
                    } else {
                        vm->state = ROOM_VM_IDLE;
                        vm->has_target = false;
                    }
                    vm->hit_from_play = false;
                    vm->elapsed = 0.0f;
                    break;
                default:             vm->state = ROOM_VM_IDLE;
                                     vm->has_target = false;
                                     vm->elapsed = 0.0f;         break;
            }
        }
    }

    const float dur = state_duration(vm->state);
    const float t = (dur > 0.0f) ? ns_clampf(vm->elapsed / dur, 0.0f, 1.0f) : 0.0f;

    /* L'enfoncement du bouton : une descente franche, une remontée plus lente,
     * comme un ressort. */
    float press = 0.0f;
    if (vm->state == ROOM_VM_PRESS) {
        press = (t < 0.35f) ? (t / 0.35f) : ns_maxf(0.0f, 1.0f - (t - 0.35f) / 0.65f);
    } else if (vm->state == ROOM_VM_PLAY) {
        /* Pendant la partie, c'est le jeu qui commande l'index : chaque battement
         * met `tap` à 1 et il retombe en un dixième de seconde. Le décrément est
         * linéaire et non amorti — un amortissement exponentiel laisserait une
         * traîne, donc un doigt jamais tout à fait revenu, donc pas d'appui
         * visible au battement suivant. */
        vm->tap = ns_maxf(0.0f, vm->tap - dt * 10.0f);
        press = vm->tap;
    }
    vm->press_depth = ns_damp(vm->press_depth, press, 24.0f, dt);

    /* L'avancée du jeton dans la fente : présenter, pousser, retirer la main.
     * Mémorisée plutôt que recalculée dans la pose, parce que la pose n'a pas le
     * droit d'écrire dans l'état et que la recalculer depuis `elapsed` — non
     * interpolé — ferait avancer le jeton par paliers de pas de simulation. */
    float push = 0.0f;
    if (vm->state == ROOM_VM_INSERT) {
        push = (t < VM_INSERT_SOMMET)
             ? (t / VM_INSERT_SOMMET)
             : ns_maxf(0.0f, 1.0f - (t - VM_INSERT_SOMMET) / (1.0f - VM_INSERT_SOMMET));
    } else if (vm->state == ROOM_VM_RELANCE) {
        /* La même grandeur pour le même objet : c'est elle qui amène le bout du
         * doigt sur la fente dans `room_viewmodel_pose`. Sa courbe diffère
         * seulement par la place de son sommet. */
        push = (t < VM_RELANCE_SOMMET)
             ? (t / VM_RELANCE_SOMMET)
             : ns_maxf(0.0f, 1.0f - (t - VM_RELANCE_SOMMET) / (1.0f - VM_RELANCE_SOMMET));
    }
    vm->insert_push = ns_damp(vm->insert_push, push, 20.0f, dt);

    const ns_v3 lean_target = lean_for(vm->state, t);
    vm->lean = ns_v3_make(ns_damp(vm->lean.x, lean_target.x, 9.0f, dt),
                          ns_damp(vm->lean.y, lean_target.y, 9.0f, dt),
                          ns_damp(vm->lean.z, lean_target.z, 9.0f, dt));

    /*
     * LE CHAMP SUIT L'ÉTAT — voir `VM_FOV_JEU` pour les degrés mesurés.
     *
     * Le coup et la relance comptent comme « en partie » quand ils en viennent,
     * et c'est tout ce qu'ils ont à dire ici : ils INTERROMPENT `ROOM_VM_PLAY`
     * et y reviennent, le poste ne se lâche pas pour autant, et un cadre qui se
     * refermerait le temps d'un coup de poing pour se rouvrir aussitôt serait le
     * seul endroit du jeu où frapper une borne déplacerait la caméra.
     */
    const bool en_partie = (vm->state == ROOM_VM_PLAY)
                        || (vm->state == ROOM_VM_RELANCE)
                        || (vm->state == ROOM_VM_HIT && vm->hit_from_play);
    vm->fov = ns_damp(vm->fov, en_partie ? VM_FOV_JEU : VM_FOV_Y, VM_FOV_DAMP, dt);

    /* --- cibles des poignets, en espace caméra --------------------------- */
    const ns_camera resolved = room_camera_resolve(cam, NULL, 1.0f);
    const vm_basis b = basis_of(&resolved);
    const room_view_bob bob = room_camera_bob(cam, 1.0f);

    room_view_bob swing_bob = bob;
    if (vm->forced_pose == VM_FORCE_WALK) {
        swing_bob.amount = 1.0f;
        swing_bob.distance = vm->clock * 1.4f;   /* la vitesse de marche */
    }

    const float stride = room_camera_stride(cam);
    ns_v3 want_l = ns_v3_add(rest_wrist(false), walk_swing(false, &swing_bob, stride));
    ns_v3 want_r = ns_v3_add(rest_wrist(true), walk_swing(true, &swing_bob, stride));

    /* Respiration : quelques millimètres, seulement à l'arrêt. Ce n'est pas de
     * la décoration — c'est ce qui empêche une main immobile de se lire comme
     * une image fixe collée sur le cadre. */
    const float rest = 1.0f - ns_clampf(swing_bob.amount, 0.0f, 1.0f);
    const float breath = sinf(bob.breath * 1.9f) * 0.008f * rest;
    want_l.y += breath; want_r.y += breath;

    if ((vm->state == ROOM_VM_PLAY || vm->state == ROOM_VM_RELANCE) && vm->has_target) {
        /*
         * Les DEUX mains, et chacune sur sa commande : la gauche sur le manche,
         * la droite sur les boutons. C'est le seul endroit où la main gauche a
         * une cible — partout ailleurs elle suit le corps.
         *
         * LA RELANCE PASSE PAR ICI ET NON PAR LA SÉQUENCE, et c'est tout ce qui
         * la distingue : elle emprunte la main gauche du jeu et remplace la
         * seule main droite juste en dessous. Passer par la branche de la
         * séquence retirerait la gauche du manche et reculerait les épaules —
         * c'est-à-dire referait le geste complet, celui qu'on a mesuré à
         * 1,31 s et écarté.
         *
         * Chaque épaule reçoit son propre décalage latéral : viser le manche
         * depuis l'épaule DROITE croiserait les bras devant le torse, ce qui se
         * voit immédiatement et n'a aucune raison d'être.
         */
        const ns_v3 lean = vm->lean;
        const ns_v3 sh_l_cam = ns_v3_add(ns_v3_make(-VM_SHOULDER_X, VM_SHOULDER_Y, VM_SHOULDER_Z), lean);
        const ns_v3 sh_r_cam = ns_v3_add(ns_v3_make( VM_SHOULDER_X, VM_SHOULDER_Y, VM_SHOULDER_Z), lean);
        const ns_v3 sh_l = to_world(&b, sh_l_cam);
        const ns_v3 sh_r = to_world(&b, sh_r_cam);

        /* Le poignet se pose une paume au-dessus de la commande : les ancres
         * désignent la surface touchée, pas l'articulation. */
        const ns_v3 up = ns_v3_make(0.0f, VM_HAND * 0.35f, 0.0f);
        const ns_v3 grip  = ns_v3_add(vm->target_stick, up);
        const ns_v3 above = ns_v3_add(vm->target_panel, up);

        want_l = to_camera(&b, clamp_reach(sh_l, grip,  VM_REACH));
        want_r = to_camera(&b, clamp_reach(sh_r, above, VM_REACH));

        /* La main droite SEULE quitte les boutons le temps du jeton. La gauche
         * garde la cible qu'on vient de lui donner. */
        if (vm->state == ROOM_VM_RELANCE) {
            want_r = to_camera(&b, clamp_reach(sh_r, sequence_wrist(vm, sh_r, t),
                                               VM_REACH));
        }

        /*
         * Si les commandes sont hors d'atteinte, on le DIT.
         *
         * Le contrôle tourne aussi sous `--pose=` : il n'y tournait pas, et
         * c'est ce silence qui a laissé le point de vue nommé « borne » rester
         * 25 cm derrière l'ancre déclarée pendant tout le projet. La capture
         * montrait deux mains à hauteur de monnayeur, l'avertissement qui
         * l'aurait expliqué était gardé pour la seule entrée en jeu.
         *
         * `clamp_reach` ne peut pas échouer : elle pose le poignet à 57 cm dans
         * la bonne direction et rend un bras parfaitement tendu vers un point
         * qu'il ne touche pas. À l'image, ce sont deux tubes qui pointent vers
         * rien — et rien n'indique pourquoi. C'est exactement le silence qu'on
         * a passé le projet à retirer : la géométrie d'une borne, la position
         * d'arrêt de la capsule et la longueur d'un bras sont trois cotes
         * réglées dans trois fichiers différents, et il n'y a qu'ici qu'on peut
         * constater qu'elles ne sont plus d'accord.
         *
         * Une fois par entrée en jeu, pas une fois par image.
         */
        if (!vm->reach_checked && (vm->elapsed > 0.8f || vm->forced_pose != VM_FORCE_NONE)) {
            vm->reach_checked = true;
            const float dl = ns_v3_dist(sh_l, grip), dr = ns_v3_dist(sh_r, above);
            const float worst = ns_maxf(dl, dr);
            if (worst > VM_REACH + 0.02f) {
                NS_WARN("bras : commandes à %.2f m d'une épaule pour %.2f m de portée — "
                        "les mains resteront tendues sans les toucher "
                        "(manche %.2f m, boutons %.2f m)",
                        (double)worst, (double)VM_REACH, (double)dl, (double)dr);
            }
        }

        /* Le tremblement du jeu, sur les deux mains : quelques millimètres, à
         * une fréquence assez haute pour se lire comme de la tension et pas
         * comme une respiration. Sans lui, deux mains posées sur des commandes
         * pendant une partie entière se lisent comme une image fixe. */
        const float j = sinf(vm->clock * 11.0f) * 0.0035f;
        want_l.y += j; want_r.y -= j * 0.7f;
    } else if (vm->has_target && vm->state != ROOM_VM_IDLE && vm->state != ROOM_VM_RETURN) {
        const ns_v3 shoulder_cam = ns_v3_add(
            ns_v3_make(VM_SHOULDER_X, VM_SHOULDER_Y, VM_SHOULDER_Z), vm->lean);
        const ns_v3 shoulder_world = to_world(&b, shoulder_cam);
        const ns_v3 wrist_world = sequence_wrist(vm, shoulder_world, t);
        want_r = to_camera(&b, clamp_reach(shoulder_world, wrist_world, VM_REACH));

        /* La main gauche ne reste pas plantée là : elle recule légèrement, comme
         * celle de quelqu'un qui se penche en avant. */
        want_l = ns_v3_add(want_l, ns_v3_make(-0.03f, -0.02f, -0.05f));
    }

    /*
     * LE COUP, qui écrase tout ce qui précède pour la main droite.
     *
     * Il est écrit APRÈS les autres cibles et non dans leur chaîne de `else`,
     * et c'est délibéré : la main GAUCHE doit garder ce que l'état précédent
     * lui donnait. On cogne d'une main ; l'autre reste sur le manche si elle y
     * était, et le long du corps sinon. Une machine à états qui rendrait les
     * deux mains à chaque geste ferait lâcher le manche pour taper sur la
     * vitre, ce qui n'est pas ce qu'on fait quand on rage.
     */
    float damp_r = VM_DAMP;
    bool  pose_directe = false;   /* le coup EST une trajectoire : on la pose */
    if (vm->state == ROOM_VM_HIT) {
        if (vm->elapsed < VM_T_HIT_ARME) {
            /* L'ARMÉ : le poing recule vers les côtes. En `smoothstep` et non
             * linéaire — un retrait part et s'arrête, il ne file pas à vitesse
             * constante pour claquer sur sa fin. */
            const float k = smoothstep01(vm->elapsed / VM_T_HIT_ARME);
            want_r = ns_v3_lerp(rest_wrist(true), VM_HIT_ARME, k);
            pose_directe = true;
        } else if (vm->elapsed < VM_T_HIT_ARME + VM_T_HIT_OUT) {
            /* L'ALLER : la courbe accélère, et on la POSE — voir le
             * commentaire de `VM_DAMP_HIT_BACK` pour les seize centimètres que
             * le lissage coûtait ici. */
            want_r = ns_v3_lerp(VM_HIT_ARME, VM_HIT_PORTE,
                                ns_clampf(hit_amount(vm, vm->elapsed), 0.0f, 1.0f));
            pose_directe = true;
        } else {
            /*
             * LE RETOUR PART DU COUP PORTÉ, ET C'EST CE QUI GARANTIT QUE LE
             * POING Y ARRIVE.
             *
             * Il a d'abord été écrit comme un simple ralentissement du
             * rattrapage, et la mesure a montré le défaut : la fin de l'aller
             * tombe ENTRE deux pas de simulation. Le dernier échantillon de la
             * phase valait 91 % de sa durée, donc 82 % de la course — le poing
             * culminait trois centimètres devant la position de repos et
             * repartait sans avoir touché sa cible. Un geste dont le point
             * d'arrivée n'est jamais échantillonné n'arrive pas.
             *
             * En partant EXPLICITEMENT du coup porté, le premier pas du retour
             * pose le poing exactement là où il devait aller, quel que soit
             * l'endroit où la grille des pas est tombée. Et l'arrivée est tout
             * aussi exacte : on interpole vers `want_r`, c'est-à-dire vers ce
             * que le repos ou la partie en cours demandaient déjà — donc les
             * mains reviennent sur les commandes sans qu'il y ait une seconde
             * trajectoire à écrire.
             */
            const float k = smoothstep01(
                (vm->elapsed - VM_T_HIT_ARME - VM_T_HIT_OUT) / VM_T_HIT_BACK);
            want_r = ns_v3_lerp(VM_HIT_PORTE, want_r, k);
            pose_directe = true;
        }

        /* L'épaule, elle, N'EST PAS TOUCHÉE ICI : voir `hit_push` et le
         * commentaire qui l'accompagne. Une poussée ajoutée à `vm->lean` après
         * son amortissement s'accumule d'un pas sur l'autre. */
    }

    if (!vm->primed) {
        vm->wrist_l = vm->prev_wrist_l = want_l;
        vm->wrist_r = vm->prev_wrist_r = want_r;
        vm->primed = true;
        return;
    }

    vm->wrist_l = ns_v3_make(ns_damp(vm->wrist_l.x, want_l.x, VM_DAMP, dt),
                             ns_damp(vm->wrist_l.y, want_l.y, VM_DAMP, dt),
                             ns_damp(vm->wrist_l.z, want_l.z, VM_DAMP, dt));
    vm->wrist_r = pose_directe
        ? want_r
        : ns_v3_make(ns_damp(vm->wrist_r.x, want_r.x, damp_r, dt),
                     ns_damp(vm->wrist_r.y, want_r.y, damp_r, dt),
                     ns_damp(vm->wrist_r.z, want_r.z, damp_r, dt));
}

/* --------------------------------------------------------------------------
 * Pose de rendu
 * -------------------------------------------------------------------------- */

/* Une matrice qui envoie le −Z local sur `dir`, translatée en `origin`. Les sept
 * segments sont modélisés le long de −Z sur une longueur de 1 : c'est le shader
 * qui applique la longueur, précisément pour que cette matrice reste une
 * rotation pure et que les normales se transforment sans inverse transposée. */
static ns_m4 segment_matrix(ns_v3 origin, ns_v3 dir)
{
    const float len = ns_v3_len(dir);
    const ns_v3 d = (len > 1e-6f) ? ns_v3_scale(dir, 1.0f / len)
                                  : ns_v3_make(0.0f, 0.0f, -1.0f);
    const ns_quat q = ns_quat_from_to(ns_v3_make(0.0f, 0.0f, -1.0f), d);
    return ns_m4_trs(origin, q, ns_v3_make(1.0f, 1.0f, 1.0f));
}

/*
 * La matrice de la main, et son ROULIS — ce que `segment_matrix` ne pouvait pas
 * donner.
 *
 * `segment_matrix` construit la rotation MINIMALE qui amène −Z sur la
 * direction voulue. Elle est parfaite pour un tube, dont la rotation autour de
 * son axe ne se voit pas. Une main, si : elle a un dos et une paume, et la
 * rotation minimale les oriente n'importe où — au hasard de la position de
 * l'épaule. Sur les captures, la main gauche présentait le tranchant à la
 * boule qu'elle était censée empoigner.
 *
 * On impose donc les deux axes qui comptent : −Z suit les doigts, −Y est la
 * paume, et la paume regarde le PANNEAU. C'est vrai de toutes les poses de ce
 * jeu — une main sur une boule d'arcade, une main au-dessus des boutons et une
 * main qui glisse un jeton ont toutes la paume tournée vers le meuble.
 */
static ns_m4 hand_matrix(ns_v3 origin, ns_v3 dir, ns_v3 up)
{
    const float len = ns_v3_len(dir);
    const ns_v3 f = (len > 1e-6f) ? ns_v3_scale(dir, 1.0f / len)
                                  : ns_v3_make(0.0f, 0.0f, -1.0f);

    /* La paume vers le bas, débarrassée de sa composante le long des doigts :
     * sans cette orthogonalisation la base serait oblique et la main cisaillée. */
    ns_v3 palm = ns_v3_scale(up, -1.0f);
    palm = ns_v3_sub(palm, ns_v3_scale(f, ns_v3_dot(palm, f)));
    if (ns_v3_len(palm) < 1e-4f) {
        /* Doigts verticaux : « vers le bas » ne dit plus rien. On retombe sur
         * une perpendiculaire quelconque, ce qui vaut mieux qu'une base nulle. */
        palm = ns_v3_sub(ns_v3_make(0.0f, 0.0f, -1.0f),
                         ns_v3_scale(f, f.z * -1.0f));
        if (ns_v3_len(palm) < 1e-4f) palm = ns_v3_make(1.0f, 0.0f, 0.0f);
    }
    palm = ns_v3_norm(palm);

    /* Colonnes : image des axes locaux. −Z sur les doigts, −Y sur la paume. */
    const ns_v3 zc = ns_v3_scale(f, -1.0f);
    const ns_v3 yc = ns_v3_scale(palm, -1.0f);
    const ns_v3 xc = ns_v3_cross(yc, zc);

    /* Colonne-majeure, m[colonne][ligne] : chaque colonne EST l'image d'un axe
     * local, ce qui rend la construction lisible telle quelle. */
    ns_m4 m = ns_m4_identity();
    m.m[0][0] = xc.x; m.m[0][1] = xc.y; m.m[0][2] = xc.z;
    m.m[1][0] = yc.x; m.m[1][1] = yc.y; m.m[1][2] = yc.z;
    m.m[2][0] = zc.x; m.m[2][1] = zc.y; m.m[2][2] = zc.z;
    m.m[3][0] = origin.x; m.m[3][1] = origin.y; m.m[3][2] = origin.z;
    return m;
}

/* Le point local amené en monde par une matrice SANS échelle ni perspective.
 * `ns_m4_project` ferait la même chose en divisant par un w qu'on sait valoir
 * un : autant ne pas le calculer, et surtout ne pas laisser croire qu'on
 * pourrait passer ici une matrice de projection. */
static ns_v3 hand_point(ns_m4 m, ns_v3 p)
{
    return ns_v3_make(
        m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z + m.m[3][0],
        m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z + m.m[3][1],
        m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z + m.m[3][2]);
}

/*
 * La direction de la main : elle prolonge l'avant-bras, fléchie vers le bas d'un
 * angle qui croît avec `finger_curl`. C'est ce qui fait descendre l'index sur le
 * bouton sans bouger le poignet, exactement comme on appuie.
 */
static ns_v3 hand_direction(const vm_basis *b, const ns_ik2 *ik, float finger_curl)
{
    ns_v3 dir = ns_v3_sub(ik->end, ik->joint);
    dir = (ns_v3_len(dir) > 1e-6f) ? ns_v3_norm(dir) : b->fwd;

    const ns_v3 axis = ns_v3_cross(dir, b->up);
    if (ns_v3_len(axis) > 1e-4f) {
        const ns_quat bend = ns_quat_from_axis(ns_v3_norm(axis),
                                               (0.22f + 0.55f * finger_curl));
        dir = ns_quat_rotate(bend, dir);
    }
    return dir;
}

/*
 * `cible`, si elle est fournie, est le point du monde sur lequel il faut amener
 * `ancre` — un point de la MAIN, dans le repère de la main.
 *
 * L'ancre était forcément le bout du majeur, et c'est ce qui a laissé la main
 * gauche à côté de sa boule pendant tout le projet. Un bout de doigt est le bon
 * point pour une fente et pour un bouton : on les touche. Une boule de manche,
 * on la TIENT — l'ancre est alors le creux de la paume, et la cible le centre
 * de la boule. La différence n'est pas un réglage : sur une boule de 42 mm elle
 * vaut 6,1 cm de paume — mesurée par `test_ik`, 8,6 cm du centre contre 2,5 —
 * et c'est exactement l'écart entre une main qui empoigne et une main qui tâte.
 *
 * Sans elle, la machine à états ne peut viser qu'avec le poignet, en soustrayant
 * une longueur de main devinée le long d'une direction qu'elle ne connaît pas
 * encore : la main est fléchie d'un angle qui dépend de la solution de l'IK,
 * laquelle dépend de la cible. Mesuré, ce serpent qui se mord la queue laissait
 * le doigt à **12 cm de la fente** — le jeton flottait dans le vide, ce qui est
 * précisément le genre de défaut qu'aucun réglage de constante ne rattrape.
 *
 * Une seule itération correctrice suffit et coûte une résolution analytique de
 * plus : on résout, on regarde où le doigt est tombé, on décale le poignet du
 * reliquat, on résout à nouveau. La flexion de la main varie peu entre les deux
 * passes, donc la correction converge d'un coup. Itérer davantage ne
 * gagnerait que du bruit.
 */
static void pose_arm(ns_viewmodel_pose *out, const vm_basis *b, bool right,
                     ns_v3 shoulder_cam, ns_v3 wrist_cam, float finger_curl,
                     const ns_v3 *cible, ns_v3 ancre)
{
    const int sleeve  = right ? NS_VM_SLEEVE_R  : NS_VM_SLEEVE_L;
    const int forearm = right ? NS_VM_FOREARM_R : NS_VM_FOREARM_L;
    const int hand    = right ? NS_VM_HAND_R    : NS_VM_HAND_L;

    const ns_v3 shoulder = to_world(b, shoulder_cam);
    ns_v3 wrist = to_world(b, wrist_cam);

    /* Le pôle tient le coude vers l'extérieur et vers le bas — c'est-à-dire là
     * où il est chez un humain. Sans lui le coude tourne librement autour de
     * l'axe épaule-poignet et finit régulièrement dans le torse. */
    const ns_v3 pole = to_world(b, ns_v3_add(shoulder_cam,
        ns_v3_make(right ? 0.42f : -0.42f, -0.40f, -0.06f)));

    ns_ik2 ik = ns_ik_two_bone(shoulder, wrist, pole, VM_UPPER, VM_FORE);
    ns_v3  dir = hand_direction(b, &ik, finger_curl);

    /*
     * QUATRE itérations correctrices — c'en était deux, et il a fallu doubler.
     *
     * Chacune déplace le poignet du vecteur qui sépare le bout du doigt de sa
     * cible, puis re-résout — mais la re-résolution change la direction de la
     * main, donc corrige un peu à côté. C'est un point fixe.
     *
     * Sa vitesse de convergence dépend de la DISTANCE du bout du doigt à l'axe
     * du poignet, et c'est elle qui a changé : tant que la main était plate, le
     * bout du majeur était sur l'axe, la correction était presque un
     * déplacement pur et deux tours suffisaient. Une main fléchie porte son
     * bout de doigt six centimètres et demi hors de l'axe : la même correction
     * fait maintenant TOURNER la main autant qu'elle la déplace, et le point
     * fixe met deux tours de plus.
     *
     * Mesuré par `test_ik` sur la pose de jeu, à deux tours puis à quatre :
     * le manche passe de 3,2 cm à 0,2 cm, les boutons de 3,9 cm à 1,3 cm, la
     * fente de 3,1 cm à 1,3 cm. Les trois seuils du test étaient franchis à
     * deux tours et sont tenus à quatre — et les chiffres sont meilleurs que
     * ceux de la main plate, qui plafonnait à 3,0 cm du manche.
     */
    for (int pass = 0; cible && pass < 4; ++pass) {
        const ns_m4 h = hand_matrix(ik.end, dir, b->up);
        const ns_v3 at = hand_point(h, ancre);
        wrist = clamp_reach(shoulder, ns_v3_add(wrist, ns_v3_sub(*cible, at)),
                            VM_REACH);
        ik = ns_ik_two_bone(shoulder, wrist, pole, VM_UPPER, VM_FORE);
        dir = hand_direction(b, &ik, finger_curl);
    }

    out->segment[sleeve]  = segment_matrix(ik.root, ns_v3_sub(ik.joint, ik.root));
    out->length[sleeve]   = VM_UPPER;
    out->segment[forearm] = segment_matrix(ik.joint, ns_v3_sub(ik.end, ik.joint));
    out->length[forearm]  = VM_FORE;
    out->segment[hand]    = hand_matrix(ik.end, dir, b->up);
    out->length[hand]     = VM_HAND;

    out->draw[sleeve] = out->draw[forearm] = out->draw[hand] = true;
}

void room_viewmodel_pose(const room_viewmodel *vm, const room_camera *cam,
                         float alpha, ns_viewmodel_pose *out)
{
    ns_viewmodel_pose_clear(out);
    if (!vm || !cam || cam->mode != ROOM_CAM_PLAYER || !vm->primed) return;

    const ns_camera resolved = room_camera_resolve(cam, NULL, alpha);
    const vm_basis b = basis_of(&resolved);

    ns_v3 lean = ns_v3_lerp(vm->prev_lean, vm->lean, alpha);

    /* La poussée d'épaule du coup, ajoutée ICI et nulle part ailleurs : c'est
     * une fonction de l'avancement du geste, pas un état. Voir `hit_push`. */
    lean = ns_v3_add(lean, hit_push(room_viewmodel_frappe_amount(vm, alpha)));
    const ns_v3 wl = ns_v3_lerp(vm->prev_wrist_l, vm->wrist_l, alpha);
    const ns_v3 wr = ns_v3_lerp(vm->prev_wrist_r, vm->wrist_r, alpha);
    const float press = ns_lerpf(vm->prev_press_depth, vm->press_depth, alpha);

    const ns_v3 base = ns_v3_make(VM_SHOULDER_X, VM_SHOULDER_Y, VM_SHOULDER_Z);
    const ns_v3 sr = ns_v3_add(base, lean);
    const ns_v3 sl = ns_v3_add(ns_v3_make(-base.x, base.y, base.z), lean);

    /*
     * Où le doigt doit atterrir, quand la machine à états le sait. La main gauche
     * ne vise jamais rien : elle accompagne.
     */
    const float push = ns_lerpf(vm->prev_insert_push, vm->insert_push, alpha);
    ns_v3 tip_target;
    const ns_v3 *tip = NULL;
    if (vm->has_target && (vm->state == ROOM_VM_INSERT || vm->state == ROOM_VM_RELANCE)) {
        /* Le jeton entre : le doigt part à 9 cm de la fente et vient la toucher.
         * Les deux gestes d'insertion partagent cette ligne parce qu'ils
         * partagent `insert_push`, qui est ce que le doigt suit. */
        tip_target = ns_v3_add(vm->target_coin,
                               ns_v3_scale(ns_v3_norm(vm->target_normal),
                                           0.090f * (1.0f - push)));
        tip = &tip_target;
    } else if (vm->has_target && vm->state == ROOM_VM_PRESS) {
        /* Le doigt se pose sur le bouton et l'enfonce de 12 mm. */
        tip_target = ns_v3_add(vm->target_panel,
                               ns_v3_make(0.0f, 0.012f * (1.0f - press), 0.0f));
        tip = &tip_target;
    }

    /*
     * Sur le manche, les DEUX mains visent — et c'est le seul moment où la
     * gauche vise.
     *
     * Sans ça, `hand_direction` prolongeait simplement l'avant-bras avec une
     * flexion fixe : les mains arrivaient au-dessus des commandes, doigts
     * tendus vers l'avant, à survoler un manche qu'elles ne touchaient pas. Le
     * mécanisme pour les poser existait depuis A7 — la seconde résolution
     * corrective qui amène un point de la main sur un point du monde — et
     * n'était employé que pour la fente et le bouton.
     *
     * ELLE VISE AVEC LA PAUME, ET C'EST TOUT LE SUJET. Amener le bout du majeur
     * sur le sommet de la boule — ce qui était écrit ici — donne un doigt qui
     * touche et une boule qui reste DEHORS : la paume finit à 8,6 cm du centre
     * pour 2,1 de rayon, le poing se referme à côté, et sur une capture rapprochée
     * la main flotte contre le manche au lieu de le tenir. Ce n'était pas un
     * réglage à retoucher, c'était la mauvaise ancre : 5,6 cm séparent le creux
     * de la paume du bout du majeur, et aucune prise sur une boule de 42 mm ne
     * peut mettre les deux au même endroit.
     *
     * La paume sur le centre plus un rayon, donc, et la boule passe sous les
     * têtes métacarpiennes avec les doigts refermés devant — vérifié en image,
     * et tenu par `test_ik` sur trois faits : paume posée, majeur passé de
     * l'autre côté du centre, boule non traversée.
     *
     * LA RELANCE EST DU VOYAGE, et elle n'y était pas. Le poignet gauche, lui,
     * l'était déjà — la branche des cibles ci-dessus traite les deux états
     * ensemble — mais la main gauche perdait son ancre en entrant en relance et
     * la retrouvait en sortant. Elle sautait donc de 4,5 cm à chaque bout d'un
     * geste de 500 ms, dans les deux sens, sur la main qu'on venait justement
     * de décider de NE PAS bouger. C'est le genre de défaut qui ne se voit
     * qu'en jeu et qu'on met sur le compte du hasard.
     */
    ns_v3 tip_l_target;
    const ns_v3 *tip_l = NULL;
    if ((vm->state == ROOM_VM_PLAY || vm->state == ROOM_VM_RELANCE) && vm->has_target) {
        /* L'ancre désigne le sommet de la boule ; on empoigne son CENTRE. */
        tip_l_target = ns_v3_sub(vm->target_stick,
                                 ns_v3_make(0.0f, ROOM_VM_BALL_R, 0.0f));
        tip_l = &tip_l_target;
    }
    if (vm->state == ROOM_VM_PLAY && vm->has_target) {
        /* Le doigt droit se pose sur les boutons et les enfonce au battement. */
        tip_target = ns_v3_add(vm->target_panel,
                               ns_v3_make(0.0f, 0.010f * (1.0f - press), 0.0f));
        tip = &tip_target;
    }

    pose_arm(out, &b, false, sl, wl, 0.0f, tip_l, ns_viewmodel_grip(ROOM_VM_BALL_R));
    pose_arm(out, &b, true,  sr, wr, press, tip, ns_viewmodel_fingertip(true));

    /*
     * Le jeton, pincé au bout de la main droite, sa tranche vers la fente. Le
     * disque est modélisé dans le plan XY local : on envoie donc son axe sur le
     * côté de la main, ce qui met sa face à la verticale — l'orientation qu'il
     * faut pour entrer dans une fente, qui est toujours verticale.
     */
    if (vm->token_visible) {
        /* `ns_m4` est colonne-majeure et indexée `m[colonne][ligne]` : la
         * troisième colonne porte l'axe Z local, la quatrième la translation. Le
         * maillage étant modelé le long de −Z, la direction de la main est
         * l'opposé de cette colonne. */
        const ns_m4 hand = out->segment[NS_VM_HAND_R];
        const ns_v3 hand_dir = ns_v3_make(-hand.m[2][0], -hand.m[2][1], -hand.m[2][2]);
        const ns_v3 token_at = ns_v3_add(ns_v3_make(hand.m[3][0], hand.m[3][1], hand.m[3][2]),
                                         ns_v3_scale(hand_dir, VM_HAND * 0.92f));

        ns_v3 edge = ns_v3_cross(hand_dir, b.up);
        edge = (ns_v3_len(edge) > 1e-4f) ? ns_v3_norm(edge) : b.right;

        out->segment[NS_VM_TOKEN] = segment_matrix(token_at, edge);
        out->length[NS_VM_TOKEN]  = 1.0f;   /* le jeton est à sa vraie taille */
        out->draw[NS_VM_TOKEN]    = true;
    }

    out->fov_y_degrees = ns_lerpf(vm->prev_fov, vm->fov, alpha);
}
