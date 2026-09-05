/* room_sound.c — voir room_sound.h pour le raisonnement. */
#include "room_sound.h"

#include "ns_config.h"
#include "ns_core.h"
#include "ns_env.h"

#include <math.h>
#include <string.h>

/*
 * Un pas tous les DEMI-PAS de foulée : une foulée est un cycle complet, donc
 * deux pas.
 *
 * La foulée elle-même vient de la caméra, par `room_camera_stride`. Elle était
 * recopiée ici, en dur, à 1,55 m ; elle ne peut plus l'être, parce que
 * `personnage.foulee` la règle sans recompiler et qu'une copie ferait sonner les
 * pas à contretemps des jambes. La valeur par défaut n'a pas changé.
 */
#define RS_STEP_FRACTION 0.5f

/* Portées. Une borne ne s'entend pas d'un bout à l'autre de la salle : c'est ce
 * qui permet d'en avoir dix-neuf sans que ça devienne une bouillie. */
#define RS_CABINET_RADIUS 0.9f
#define RS_CABINET_MAX    5.5f

/* L'extracteur des toilettes et la rue au sas. Deux portées très différentes, et
 * pour une raison : l'extracteur est un objet dans une pièce — on doit le perdre
 * dès qu'on en sort —, la rue est un dehors, qui déborde par la baie. */
#define RS_FAN_RADIUS     1.1f
#define RS_FAN_MAX        6.0f
#define RS_STREET_RADIUS  2.4f
#define RS_STREET_MAX    16.0f

/*
 * La chasse d'eau, et la porte.
 *
 * Deux portées franchement différentes, et c'est délibéré. La chasse est un
 * ÉVÉNEMENT dans une pièce voisine : on doit l'entendre depuis le hall, sinon
 * elle ne raconte rien — d'où douze mètres. La porte est une mécanique qu'on
 * déclenche soi-même en s'approchant : on est forcément à moins de deux mètres
 * quand elle part, et la porter plus loin ferait grincer une porte que personne
 * n'a ouverte. Les 8 unités de `reglageVolume` en 2020 valent 3,88 m à 2,06
 * unités par mètre ; c'est cette cote-là qu'on garde pour la porte.
 */
/*
 * LE JETON. La portée la plus courte de tout ce fichier, et c'est voulu : une
 * pièce qui tombe est un petit objet à cinquante centimètres des yeux. Un jeton
 * qu'on entendrait du bar dirait à tout le monde que quelqu'un vient de payer,
 * ce qui n'a aucun sens dans une salle où l'on paie en permanence.
 *
 * Six mètres et non les quatorze du coup de poing : cogner une borne est un
 * ÉVÉNEMENT — on se retourne —, mettre une pièce n'en est pas un.
 */
#define RS_COIN_RADIUS    0.6f
#define RS_COIN_MAX       6.0f

/*
 * LE COUPERET. Deux portées seulement, parce que trois des cinq sons sont
 * placés et deux ne le sont pas du tout — voir `room_sound.h`, la question y est
 * tranchée et argumentée.
 *
 * LA COUPURE porte loin : c'est la deuxième plus longue portée du fichier après
 * la chasse d'eau. Une borne qu'on éteint doit faire se retourner, comme un coup
 * de poing, et pour une raison de plus que lui : la partie de la victime est
 * annulée, donc ce son est la SEULE occasion qu'ont les autres d'apprendre qui
 * vient de frapper qui. Douze mètres et non les quatorze du coup, parce qu'une
 * alimentation qui tombe n'est pas un impact — elle est moins forte à la source.
 *
 * LE BLINDAGE ET LE RENVOI portent court, et c'est l'inverse du raisonnement
 * ci-dessus. Ils répondent à une attaque dirigée contre une borne précise, donc
 * ils regardent deux joueurs — celui qui a tenu et celui qui a payé. À huit
 * joueurs qui achètent des actions en permanence, des tintements métalliques
 * audibles de partout deviendraient la TEXTURE du mode au lieu d'en être la
 * ponctuation. Huit mètres : de quoi entendre la borne d'à côté encaisser, pas
 * de quoi entendre celle du fond.
 */
#define RS_CP_COUPURE_RADIUS  1.0f
#define RS_CP_COUPURE_MAX    12.0f
#define RS_CP_PLAQUE_RADIUS   0.9f
#define RS_CP_PLAQUE_MAX      8.0f

/* Les dix dernières secondes. Une par tic, dix tics : voir `room_sound.h`. */
#define RS_CP_TIC_DEPUIS     10.0f

#define RS_FLUSH_RADIUS   1.6f
#define RS_FLUSH_MAX     12.0f
#define RS_DOOR_RADIUS    0.8f
#define RS_DOOR_MAX       3.88f

/* --------------------------------------------------------------------------
 * Les deux niveaux réglables
 * --------------------------------------------------------------------------
 * Au niveau du module, comme les volumes de bus dans `ns_audio.c`, et pour la
 * même raison : le menu les bouge sans avoir à tenir l'instance de la salle.
 * Les valeurs par défaut sont celles qu'on obtient sans `settings.cfg`.
 */
static float g_level[ROOM_LEVEL_COUNT] = { 0.85f, 0.70f };

void room_sound_set_level(room_sound_level k, float v)
{
    if (k < 0 || k >= ROOM_LEVEL_COUNT) return;
    g_level[k] = ns_clampf(v, 0.0f, 1.0f);
}

float room_sound_get_level(room_sound_level k)
{
    if (k < 0 || k >= ROOM_LEVEL_COUNT) return 0.0f;
    return g_level[k];
}

/* --------------------------------------------------------------------------
 * Les pas
 * -------------------------------------------------------------------------- */

/*
 * Ce qui reste du réglage par matériau maintenant qu'il y a une banque.
 *
 * Presque rien, et c'est voulu : la différence entre la moquette et le carrelage
 * est DANS les fichiers — 12 dB d'écart de pic et un rapport de quatre sur la
 * brillance, mesurés à la sortie de `stepgen`. La ré-appliquer ici la compterait
 * deux fois, et la moquette deviendrait inaudible.
 *
 * Il reste donc un gain quasi plat et une plage de hauteur ÉTROITE. Étroite
 * parce qu'elle ne sert plus à distinguer les matériaux mais seulement à
 * distinguer deux foulées : au-delà de ±6 %, on entend une bande qui accélère.
 */
typedef struct step_voice {
    float gain_min, gain_max;
    float pitch_min, pitch_max;
} step_voice;

static const step_voice g_step_voice[NS_STEP_COUNT] = {
    /* NONE      */ { 0.00f, 0.00f, 1.00f, 1.00f },
    /* MOQUETTE  */ { 0.94f, 1.06f, 0.95f, 1.05f },
    /* CARRELAGE */ { 0.90f, 1.02f, 0.96f, 1.05f },
    /* BOIS      */ { 0.94f, 1.06f, 0.95f, 1.06f },
    /* BETON     */ { 0.92f, 1.04f, 0.96f, 1.05f },
    /* ESTRADE   */ { 0.94f, 1.08f, 0.94f, 1.04f },
};

/*
 * Le repli de B14, gardé mot pour mot.
 *
 * Il ne sert QUE si la banque manque. Ses valeurs sont celles qui distinguaient
 * les matériaux par la hauteur, ce qui n'a plus lieu d'être quand la banque est
 * là — mais qui vaut mieux que marcher en silence quand elle ne l'est pas.
 */
static const step_voice g_legacy_voice[NS_STEP_COUNT] = {
    /* NONE      */ { 0.00f, 0.00f, 1.00f, 1.00f },
    /* MOQUETTE  */ { 0.20f, 0.28f, 0.82f, 0.92f },
    /* CARRELAGE */ { 0.42f, 0.54f, 1.18f, 1.34f },
    /* BOIS      */ { 0.34f, 0.44f, 1.00f, 1.12f },
    /* BETON     */ { 0.38f, 0.48f, 1.06f, 1.20f },
    /* ESTRADE   */ { 0.44f, 0.56f, 0.92f, 1.04f },
};

/*
 * Les quatre allures.
 *
 * Ce ne sont pas quatre banques : c'est la même, jouée autrement. Marcher,
 * courir et se déplacer accroupi ne changent pas le sol, ils changent la force
 * du contact et la façon dont le pied se pose. Un pas accroupi est posé — plus
 * faible et plus sourd ; un pas de course est jeté — plus fort, plus clair, et
 * la semelle ripe un peu plus haut.
 *
 * L'atterrissage, lui, n'est pas une allure mais un ÉVÉNEMENT : deux fois le
 * poids d'un pas, une hauteur nettement plus basse, et une intensité qui suit la
 * vitesse de chute (`bob.land`).
 */
typedef enum rs_gait { RS_GAIT_CROUCH = 0, RS_GAIT_WALK, RS_GAIT_RUN, RS_GAIT_LAND } rs_gait;

static const struct { float gain, pitch; } g_gait[4] = {
    /* ACCROUPI */ { 0.34f, 0.93f },
    /* MARCHE   */ { 1.00f, 1.00f },
    /* COURSE   */ { 1.42f, 1.06f },
    /* CHUTE    */ { 1.90f, 0.87f },
};

static float rand_range(ns_rng *r, float lo, float hi)
{
    const float t = (float)(ns_rng_u32(r) >> 8) / (float)(1u << 24);
    return lo + (hi - lo) * t;
}

/* --------------------------------------------------------------------------
 * Mise en place
 * -------------------------------------------------------------------------- */

/* Le centre d'une zone sonore déclarée, si elle existe. C'est de là que sortent
 * les positions de l'extracteur et de la rue : les zones portent déjà les bonnes
 * boîtes, et en écrire une seconde description serait deux vérités à tenir
 * d'accord. */
static bool zone_centre(const ns_scene *scene, const char *name, ns_v3 *out, float height)
{
    for (uint32_t i = 0; i < scene->sound_zone_count; ++i) {
        if (SDL_strcasecmp(scene->sound_zone[i].name, name) != 0) continue;
        const ns_aabb b = scene->sound_zone[i].bounds;
        out->x = (b.min.x + b.max.x) * 0.5f;
        out->z = (b.min.z + b.max.z) * 0.5f;
        /* En hauteur, on ne prend PAS le milieu : un extracteur est en haut d'un
         * mur, une rue est au niveau de la rue. La cote est donnée par
         * l'appelant, mesurée depuis le plancher de la zone. */
        out->y = b.min.y + height;
        return true;
    }
    return false;
}

void room_sound_init(room_sound *s, const ns_scene *scene)
{
    memset(s, 0, sizeof *s);
    s->clip_walk = s->clip_ambience = NS_AUDIO_INVALID;
    s->clip_tone = s->clip_fan = s->clip_street = NS_AUDIO_INVALID;
    s->clip_door_open = s->clip_door_close = NS_AUDIO_INVALID;
    s->clip_flush = NS_AUDIO_INVALID;
    s->clip_jeton_insere = s->clip_jeton_refuse = NS_AUDIO_INVALID;
    s->clip_jeton_bac = NS_AUDIO_INVALID;
    /* Explicitement, et pas par le `memset` : 0 est un identifiant de clip
     * VALIDE, donc un champ laissé à zéro ferait jouer le premier son chargé de
     * la salle à chaque couperet. */
    s->clip_cp_tic = s->clip_cp_lame = NS_AUDIO_INVALID;
    s->clip_cp_coupure = s->clip_cp_blindage = NS_AUDIO_INVALID;
    s->clip_cp_renvoi = NS_AUDIO_INVALID;
    for (int i = 0; i < 3; ++i) s->clip_cabinet[i] = NS_AUDIO_INVALID;
    for (int k = 0; k < NS_STEP_COUNT; ++k) {
        for (int v = 0; v < ROOM_STEP_VARIANTS; ++v) s->clip_step[k][v] = NS_AUDIO_INVALID;
        s->last_variant[k] = -1;
    }
    s->voice_ambience = s->voice_tone = NS_AUDIO_INVALID;
    s->voice_fan = s->voice_street = NS_AUDIO_INVALID;
    for (int i = 0; i < NS_MAX_CABINETS; ++i) s->voice_cabinet[i] = NS_AUDIO_INVALID;
    s->left_foot = true;
    s->was_grounded = true;

    if (!ns_audio_ready()) return;

    /* Les réglages persistants sont relus ICI et pas dans `room/main.c` : ce
     * sont deux clés de la salle, écrites par le menu de la salle. Les faire
     * transiter par le programme principal n'ajouterait qu'un intermédiaire. */
    room_sound_set_level(ROOM_LEVEL_STEPS,
                         ns_config_get_float(ROOM_CFG_VOL_STEPS, g_level[ROOM_LEVEL_STEPS]));
    room_sound_set_level(ROOM_LEVEL_TONE,
                         ns_config_get_float(ROOM_CFG_VOL_TONE, g_level[ROOM_LEVEL_TONE]));

    s->clip_walk       = ns_audio_load("sounds/walk.wav");
    s->clip_ambience   = ns_audio_load("sounds/background.wav");
    s->clip_cabinet[0] = ns_audio_load("sounds/borne1.wav");
    s->clip_cabinet[1] = ns_audio_load("sounds/borne2.wav");
    s->clip_cabinet[2] = ns_audio_load("sounds/borne3.wav");
    s->clip_door_open  = ns_audio_load("sounds/SF-ouvport.wav");
    s->clip_door_close = ns_audio_load("sounds/SF-fermport.wav");
    s->clip_flush      = ns_audio_load("sounds/chasse_eau.wav");
    s->clip_coup       = ns_audio_load("sounds/coup_borne.wav");
    s->clip_jeton_insere = ns_audio_load("sounds/jeton_insere.wav");
    s->clip_jeton_refuse = ns_audio_load("sounds/jeton_refuse.wav");
    s->clip_jeton_bac    = ns_audio_load("sounds/jeton_bac.wav");
    s->clip_cp_tic      = ns_audio_load("sounds/couperet_tic.wav");
    s->clip_cp_lame     = ns_audio_load("sounds/couperet_lame.wav");
    s->clip_cp_coupure  = ns_audio_load("sounds/couperet_coupure.wav");
    s->clip_cp_blindage = ns_audio_load("sounds/couperet_blindage.wav");
    s->clip_cp_renvoi   = ns_audio_load("sounds/couperet_renvoi.wav");

    /* La banque de `tools/stepgen`. Le nom du matériau vient de
     * `ns_footstep_label` — le MÊME que celui que `salle.room.json` écrit et que
     * `ns_scene` relit. Un troisième jeu de chaînes ici finirait par diverger. */
    int loaded = 0;
    for (int k = 1; k < NS_STEP_COUNT; ++k) {
        for (int v = 0; v < ROOM_STEP_VARIANTS; ++v) {
            char logical[96];
            SDL_snprintf(logical, sizeof logical, "sounds/pas_%s_%d.wav",
                         ns_footstep_label((ns_footstep)k), v + 1);
            s->clip_step[k][v] = ns_audio_load(logical);
            if (s->clip_step[k][v] >= 0) loaded++;
        }
    }
    /* Tout ou rien : une banque à moitié chargée ferait alterner des pas de deux
     * origines, ce qui s'entend bien plus mal qu'un seul son répété. */
    s->bank_ready = (loaded == (NS_STEP_COUNT - 1) * ROOM_STEP_VARIANTS);

    s->clip_tone   = ns_audio_load("sounds/amb_neon.wav");
    s->clip_fan    = ns_audio_load("sounds/amb_ventilo.wav");
    s->clip_street = ns_audio_load("sounds/amb_rue.wav");

    /*
     * La nappe d'ambiance n'est PAS spatialisée : elle n'a pas de position, elle
     * est la salle. La spatialiser reviendrait à la faire venir d'un point, et à
     * la voir tourner quand le joueur tourne la tête.
     */
    if (s->clip_ambience >= 0) {
        s->voice_ambience = ns_audio_loop(s->clip_ambience, NS_BUS_AMBIENCE, 0.34f);
    }

    /*
     * Le fond de salle, non spatialisé pour la même raison — et c'est ce qui
     * manquait le plus. Entre deux pas, la salle de B14 était un VIDE numérique :
     * les bornes se taisent à cinq mètres, la nappe de 2020 est une boucle
     * courte, et il n'y avait rien d'autre. Un hall avec dix-neuf tubes et onze
     * luminaires a un plancher continu, et son absence s'entend même quand on ne
     * sait pas dire ce qui manque.
     */
    if (s->clip_tone >= 0) {
        s->voice_tone = ns_audio_loop(s->clip_tone, NS_BUS_AMBIENCE,
                                      0.42f * g_level[ROOM_LEVEL_TONE]);
    }

    if (scene) {
        /* L'extracteur, DERRIÈRE la porte des toilettes. Placé là et pas ailleurs
         * parce que c'est la seule source d'ambiance de la salle qui soit
         * occultée par un mur : elle rend l'occlusion du BVH audible en marchant,
         * ce qu'une source au milieu de l'allée ne ferait jamais. 2,35 m — en
         * haut du mur, sous un plafond à 2,90. */
        if (s->clip_fan >= 0 && zone_centre(scene, "toilettes", &s->fan_position, 2.35f)) {
            s->has_fan = true;
            /* 0,44 et pas 0,30. Mesuré : à 0,30, un extracteur sous lequel on se
             * tient ressortait à 0,21 une fois la source centrée entre les deux
             * canaux — c'est-à-dire SOUS le fond de salle, qui vaut 0,29 et
             * n'est nulle part. Une source qu'on a au-dessus de la tête et qui
             * s'entend moins fort qu'un ronflement lointain est une source qu'on
             * ne croit pas. Le maximum est resserré à six mètres en échange :
             * plus présente dedans, mais pas plus loin dehors. */
            s->voice_fan = ns_audio_loop_3d(s->clip_fan, NS_BUS_AMBIENCE, s->fan_position,
                                            0.44f, 1.0f, RS_FAN_RADIUS, RS_FAN_MAX);
        }
        /* La rue, au sas. 1,20 m : la hauteur d'une baie, pas celle d'une tête —
         * ce qui traverse, ce sont des roues sur du bitume. */
        if (s->clip_street >= 0 && zone_centre(scene, "sas_entree", &s->street_position, 1.20f)) {
            s->has_street = true;
            s->voice_street = ns_audio_loop_3d(s->clip_street, NS_BUS_AMBIENCE,
                                               s->street_position,
                                               0.34f, 1.0f, RS_STREET_RADIUS, RS_STREET_MAX);
        }

        /*
         * Les cabines, retrouvées PAR LEUR NOM dans la scène.
         *
         * Et non écrites en dur ici, alors qu'on connaît leurs coordonnées :
         * `cabine_toilettes_1` et `_2` sont déclarées dans `salle.room.json`,
         * elles y portent déjà leurs cotes, et une seconde copie de ces cotes
         * dans un fichier de son ferait deux vérités à tenir d'accord — c'est
         * exactement l'argument qui a fait déduire l'extracteur et la rue de
         * leurs zones sonores plutôt que de les poser à la main.
         *
         * La hauteur est celle du réservoir, pas celle de la cuvette : c'est de
         * là que vient l'essentiel du bruit, et surtout tout le remplissage.
         */
        for (uint32_t i = 0; i < ROOM_MAX_STALLS; ++i) {
            char name[32];
            SDL_snprintf(name, sizeof name, "cabine_toilettes_%u", i + 1);
            const ns_scene_object *o = ns_scene_find_object(scene, name);
            if (!o) continue;
            ns_v3 p = ns_aabb_center(o->bounds);
            p.y = o->bounds.min.y + 0.95f;      /* hauteur de réservoir */
            s->stall_position[s->stall_count++] = p;
        }
    }

    /*
     * La cadence des chasses.
     *
     * Quarante à cent vingt secondes. Le bas de la fourchette n'est pas « ce
     * qu'on trouve joli », c'est le seuil au-delà duquel deux chasses ne se
     * lisent plus comme un mécanisme : à dix secondes d'intervalle on entend une
     * boucle, à quarante on entend quelqu'un. L'intervalle est TIRÉ à chaque
     * fois plutôt que fixe, pour la même raison qui fait tirer les variantes de
     * pas — la régularité est ce qui trahit une machine.
     *
     * Le premier tirage part d'un compteur déjà entamé : sans ça, toutes les
     * parties commencent par le même silence de quarante secondes, ce qui est
     * une régularité de plus.
     */
    s->flush_min = ns_env_float("chasse.intervalleMin", 40.0f);
    s->flush_max = ns_env_float("chasse.intervalleMax", 120.0f);
    if (s->flush_min < 5.0f) s->flush_min = 5.0f;
    if (s->flush_max < s->flush_min) s->flush_max = s->flush_min;
    s->flush_last_stall = UINT32_MAX;
    s->rng = 0x9E37u;
    {
        ns_rng r; ns_rng_seed(&r, 0x0EA0C4A55Eull, 0u);
        s->flush_countdown = rand_range(&r, 0.25f * s->flush_min, s->flush_max);
    }

    /*
     * Une boucle par borne, à sa position déclarée, en alternant les trois
     * enregistrements. Sans l'alternance, dix-neuf sources jouant le même son en
     * phase produisent un peigne : les mêmes fréquences s'annulent et se
     * renforcent selon l'endroit où l'on se tient, et ça s'entend comme un
     * sifflement qui suit le joueur.
     */
    if (scene) {
        for (uint32_t i = 0; i < scene->cabinet_count && i < NS_MAX_CABINETS; ++i) {
            const int clip = s->clip_cabinet[i % 3];
            if (clip < 0) continue;
            const ns_cabinet *c = &scene->cabinets[i];
            s->voice_cabinet[i] = ns_audio_loop_3d(
                clip, NS_BUS_SFX, c->screen_center,
                0.30f, 0.94f + 0.04f * (float)(i % 4),
                RS_CABINET_RADIUS, RS_CABINET_MAX);
            if (s->voice_cabinet[i] >= 0) s->cabinet_voices++;
        }
    }

    s->ready = true;
    NS_INFO("son : %d borne(s) sonorisée(s), pas %s, fond %s, extracteur %s, rue %s",
            s->cabinet_voices,
            s->bank_ready ? "par banque (5 matériaux x 4)" : "sur walk.wav (banque absente)",
            s->voice_tone >= 0 ? "en place" : "absent",
            s->has_fan ? "aux toilettes" : "absent",
            s->has_street ? "au sas" : "absente");
    /* Séparé, parce que c'est la ligne qui dit si les chasses vont se faire
     * entendre : sans cabine trouvée ou sans extrait, elles se taisent en
     * silence, et un silence qui s'explique vaut mieux qu'un silence. */
    NS_INFO("son : chasse %s, %u cabine(s), toutes les %.0f à %.0f s ; "
            "porte %s à l'ouverture, %s à la fermeture",
            s->clip_flush >= 0 ? "chargée" : "ABSENTE",
            s->stall_count, (double)s->flush_min, (double)s->flush_max,
            s->clip_door_open  >= 0 ? "SF-ouvport"  : "ABSENT",
            s->clip_door_close >= 0 ? "SF-fermport" : "ABSENT");
    /* Les trois jetons ont leur ligne, pour la raison qui a fait donner la
     * sienne à la chasse : sans elle, une banque incomplète rend les gestes
     * muets EN SILENCE, et un silence qui s'explique vaut mieux qu'un silence. */
    NS_INFO("son : jeton %s à l'insertion, %s au refus, %s au godet",
            s->clip_jeton_insere >= 0 ? "chargé" : "ABSENT",
            s->clip_jeton_refuse >= 0 ? "chargé" : "ABSENT",
            s->clip_jeton_bac    >= 0 ? "chargé" : "ABSENT");
    /* Sa ligne aussi, et pour la même raison : le Couperet est le seul mode dont
     * la règle passe par le son. Une banque incomplète le rendrait muet EN
     * SILENCE, et un joueur attribuerait au mode ce qui est un défaut de build. */
    NS_INFO("son : couperet — tic %s, lame %s, coupure %s, blindage %s, renvoi %s",
            s->clip_cp_tic      >= 0 ? "chargé" : "ABSENT",
            s->clip_cp_lame     >= 0 ? "chargé" : "ABSENT",
            s->clip_cp_coupure  >= 0 ? "chargé" : "ABSENT",
            s->clip_cp_blindage >= 0 ? "chargé" : "ABSENT",
            s->clip_cp_renvoi   >= 0 ? "chargé" : "ABSENT");
}

void room_sound_shutdown(room_sound *s)
{
    if (!s->ready) return;
    ns_audio_stop(s->voice_ambience);
    ns_audio_stop(s->voice_tone);
    ns_audio_stop(s->voice_fan);
    ns_audio_stop(s->voice_street);
    for (int i = 0; i < NS_MAX_CABINETS; ++i) ns_audio_stop(s->voice_cabinet[i]);
    memset(s, 0, sizeof *s);
}

/* --------------------------------------------------------------------------
 * Par image
 * -------------------------------------------------------------------------- */

static ns_footstep step_under_feet(const ns_scene *scene, const room_camera *cam)
{
    if (!scene->material_footstep) return NS_STEP_MOQUETTE;
    if (cam->ground_material >= scene->material_count) return NS_STEP_MOQUETTE;
    const ns_footstep k = scene->material_footstep[cam->ground_material];
    /* Un matériau sans classe déclarée n'est pas une erreur : on marche dessus
     * comme sur de la moquette, et la salle de 2020 n'en déclare aucune. */
    return (k == NS_STEP_NONE) ? NS_STEP_MOQUETTE : k;
}

/*
 * Un pas, joué.
 *
 * Toute la variation est ICI, et elle porte sur trois grandeurs à la fois, parce
 * qu'une seule ne suffit pas : changer la hauteur seule fait entendre une bande
 * qui accélère, changer le gain seul fait entendre un curseur de volume. Il faut
 * l'ÉCHANTILLON en plus, et c'est ce que la banque apporte.
 */
static void play_step(room_sound *s, const ns_scene *scene, const room_camera *cam,
                      ns_v3 feet, rs_gait gait, float intensity)
{
    const ns_footstep k = step_under_feet(scene, cam);
    if (k <= NS_STEP_NONE || k >= NS_STEP_COUNT) return;

    ns_rng rng;
    ns_rng_seed(&rng, (uint64_t)(s->last_step_distance * 1000.0f), s->rng++);

    /* Le pied gauche et le pied droit ne sonnent pas pareil : deux chaussures,
     * deux appuis. Un décalage constant de timbre suffit à ce que l'oreille
     * entende une marche plutôt qu'une répétition. */
    const float foot = s->left_foot ? 0.97f : 1.03f;

    int clip;
    const step_voice *v;
    if (s->bank_ready) {
        v = &g_step_voice[k];
        /* La variante précédente est INTERDITE. C'est la règle qui compte : deux
         * pas identiques consécutifs s'entendent, deux pas identiques à cinq pas
         * d'intervalle non. */
        int pick = (int)(ns_rng_u32(&rng) % ROOM_STEP_VARIANTS);
        if (pick == s->last_variant[k]) pick = (pick + 1) % ROOM_STEP_VARIANTS;
        s->last_variant[k] = pick;
        clip = s->clip_step[k][pick];
    } else {
        v = &g_legacy_voice[k];
        clip = s->clip_walk;
    }
    if (clip < 0) return;

    const float gain = rand_range(&rng, v->gain_min, v->gain_max)
                     * g_gait[gait].gain * intensity
                     * g_level[ROOM_LEVEL_STEPS];
    const float pitch = rand_range(&rng, v->pitch_min, v->pitch_max)
                      * g_gait[gait].pitch * foot;

    /* Le pas vient des PIEDS, pas des yeux : à 1,70 m au-dessus, il sonnerait
     * comme si l'on marchait sur les mains. */
    ns_audio_play_3d(clip, NS_BUS_SFX, feet, gain, pitch, 0.6f, 8.0f);
}

/* L'allure, déduite de la caméra. Rien n'est ajouté à `room_camera` pour ça : la
 * hauteur d'œil dit l'accroupissement, et l'amplitude d'oscillation — qui vaut
 * la vitesse réelle rapportée à la marche — dit la course. */
static rs_gait gait_of(const room_camera *cam, float bob_amount)
{
    if (cam->eye_height < cam->eye_height_stand - 0.05f) return RS_GAIT_CROUCH;
    /* `speed_run / speed_walk` vaut 3,3 / 1,4 = 2,36, borné à 1,6 par
     * `bob_tick`. Le seuil est posé au-dessus de la marche rapide et bien en
     * dessous du plafond, pour qu'il ne dépende pas de la borne. */
    return (bob_amount > 1.25f) ? RS_GAIT_RUN : RS_GAIT_WALK;
}

/* --------------------------------------------------------------------------
 * Les toilettes : la porte et les chasses
 * -------------------------------------------------------------------------- */

/*
 * Les deux extraits de porte de 2020, joués pour la première fois.
 *
 * `SF-ouvport.wav` et `SF-fermport.wav` sont chargés par ce fichier depuis
 * toujours et n'ont jamais été joués par personne : le chargement était mort. La
 * machine à états vit dans `room_door.c`, qui lève un drapeau ; on le consomme
 * ici parce que c'est ici qu'on sait ce qui a été chargé.
 *
 * La source est le CENTRE DU VANTAIL, à sa position vivante, et pas la baie : un
 * panneau qui coulisse emmène son bruit avec lui, et sur 1,05 m de course
 * l'écart s'entend au casque.
 */
static void update_doors(room_sound *s, room_doors *doors)
{
    if (!doors) return;
    for (uint32_t i = 0; i < doors->count; ++i) {
        room_door *p = &doors->door[i];
        if (room_door_take_open_sound(p) && s->clip_door_open >= 0) {
            ns_audio_play_3d(s->clip_door_open, NS_BUS_SFX, p->sound_position,
                             0.70f, 1.0f, RS_DOOR_RADIUS, RS_DOOR_MAX);
        }
        if (room_door_take_close_sound(p) && s->clip_door_close >= 0) {
            ns_audio_play_3d(s->clip_door_close, NS_BUS_SFX, p->sound_position,
                             0.70f, 1.0f, RS_DOOR_RADIUS, RS_DOOR_MAX);
        }
    }
}

static void update_flush(room_sound *s, float dt)
{
    if (s->clip_flush < 0 || s->stall_count == 0) return;

    s->flush_countdown -= dt;
    if (s->flush_countdown > 0.0f) return;

    ns_rng r;
    ns_rng_seed(&r, (uint64_t)s->rng++, 0x0EA0u);
    s->flush_countdown = rand_range(&r, s->flush_min, s->flush_max);

    /* Jamais deux fois la même cabine d'affilée : avec deux cabines, la même qui
     * tire deux fois de suite s'entend comme une seule cabine — donc comme un
     * son placé, et non comme un bloc sanitaire qui vit. */
    uint32_t pick = ns_rng_u32(&r) % s->stall_count;
    if (s->stall_count > 1 && pick == s->flush_last_stall) {
        pick = (pick + 1) % s->stall_count;
    }
    s->flush_last_stall = pick;

    /* Un léger écart de hauteur d'une fois sur l'autre : deux réservoirs ne se
     * remplissent jamais tout à fait sur la même note, et c'est ce qui empêche
     * d'entendre le même fichier. */
    ns_audio_play_3d(s->clip_flush, NS_BUS_SFX, s->stall_position[pick],
                     0.55f, rand_range(&r, 0.94f, 1.07f),
                     RS_FLUSH_RADIUS, RS_FLUSH_MAX);
}

/* --------------------------------------------------------------------------
 * Le jeton
 * -------------------------------------------------------------------------- */

/*
 * Les trois gestes du jeton, joués par UNE fonction et trois jeux de bornes.
 *
 * Trois copies de `room_sound_frappe` diraient la même chose trois fois et
 * laisseraient trois endroits où corriger une portée. Ce qui diffère
 * réellement d'un geste à l'autre tient dans quatre nombres — la plage de
 * hauteur et celle de gain —, et c'est ce que les trois appelants passent.
 *
 * La graine avance à chaque appel (`s->rng++`) comme pour le coup et les
 * chasses : c'est ce qui rend la suite reproductible d'une partie à l'autre
 * sans être identique d'un jeton au suivant.
 */
static void play_jeton(room_sound *s, int clip, ns_v3 at,
                       float pitch_lo, float pitch_hi,
                       float gain_lo, float gain_hi)
{
    /* MUET plutôt qu'emprunté : voir `room_sound.h`. */
    if (!s->ready || clip < 0) return;

    ns_rng r;
    ns_rng_seed(&r, (uint64_t)s->rng++, 0x0EC0u);

    ns_audio_play_3d(clip, NS_BUS_SFX, at,
                     rand_range(&r, gain_lo, gain_hi),
                     rand_range(&r, pitch_lo, pitch_hi),
                     RS_COIN_RADIUS, RS_COIN_MAX);
}

void room_sound_jeton_insere(room_sound *s, ns_v3 position)
{
    /* ±5 % : la hauteur d'une pièce est son diamètre, et les dix-neuf bornes
     * prennent le même jeton. La plage ne sert donc qu'à casser la répétition
     * exacte d'une forme d'onde, pas à faire croire à plusieurs pièces. */
    play_jeton(s, s->clip_jeton_insere, position, 0.95f, 1.05f, 0.80f, 0.95f);
}

void room_sound_jeton_refuse(room_sound *s, ns_v3 position)
{
    /* Plus discret que les deux autres, et c'est une décision et non un
     * réglage : un refus n'est pas un événement dont la salle doit s'apercevoir.
     * On l'entend parce qu'on est devant le monnayeur. */
    play_jeton(s, s->clip_jeton_refuse, position, 0.96f, 1.04f, 0.66f, 0.80f);
}

/* Une pièce dans le godet. La plage de hauteur est la plus large des trois —
 * ±8 % — parce que c'est le seul des trois qu'on joue plusieurs fois de suite à
 * quelques dizaines de millisecondes d'intervalle. C'est l'endroit de toute la
 * bande-son où une répétition exacte s'entendrait le plus, et cinq pièces qui
 * tombent ne touchent pas le godet du même angle. */
static void play_jeton_bac(room_sound *s, ns_v3 at)
{
    play_jeton(s, s->clip_jeton_bac, at, 0.92f, 1.09f, 0.72f, 0.92f);
}

void room_sound_jeton_bac(room_sound *s, ns_v3 position, int nombre)
{
    /* Le clip manquant sort AVANT la mise en file : sans ça, on ferait tourner
     * un compte à rebours pendant une demi-seconde pour ne rien jouer. */
    if (!s->ready || s->clip_jeton_bac < 0 || nombre <= 0) return;

    /* La file est REMPLACÉE, pas allongée : voir `room_sound.h`. */
    s->coin_at    = position;
    s->coin_left  = nombre - 1;
    s->coin_delay = 0.0f;
    play_jeton_bac(s, position);
}

/*
 * La rafale, une pièce par échéance.
 *
 * L'intervalle est TIRÉ entre 52 et 84 ms, et l'argument est celui des
 * glouglous de la chasse et des tripes du coup de poing : un intervalle
 * parfaitement régulier s'entend comme un moteur. Le mécanisme, lui, l'est —
 * mais ce n'est pas le mécanisme qu'on entend, c'est la CHUTE, et cinq pièces
 * ne rebondissent pas de la même façon.
 *
 * La fourchette est étroite en valeur absolue et large en proportion : à 52 ms
 * on entend une rafale, à 84 on entend encore une suite. Au-delà de la
 * centaine, cinq jetons deviennent cinq événements et le joueur compte au lieu
 * d'entendre.
 *
 * Une seule pièce par image au plus : à 120 Hz, un pas fait 8,3 ms et la
 * fourchette la plus serrée en vaut six. Boucler ici pour rattraper un pas long
 * ne servirait qu'à faire tomber d'un coup ce qu'on vient d'espacer.
 */
static void update_coin_burst(room_sound *s, float dt)
{
    if (s->coin_left <= 0) return;

    s->coin_delay -= dt;
    if (s->coin_delay > 0.0f) return;

    ns_rng r;
    ns_rng_seed(&r, (uint64_t)s->rng++, 0x0EC1u);
    s->coin_delay = rand_range(&r, 0.052f, 0.084f);
    s->coin_left--;
    play_jeton_bac(s, s->coin_at);
}

void room_sound_update(room_sound *s, const ns_scene *scene, const room_camera *cam,
                       room_doors *doors, float dt)
{
    if (!s->ready || !ns_audio_ready()) return;

    /* L'auditeur suit l'œil. `cam->position` EST l'œil depuis A6. */
    const ns_camera view = room_camera_resolve(cam, NULL, 1.0f);
    ns_audio_set_listener(view.position, view.forward, ns_v3_make(0.0f, 1.0f, 0.0f));

    /*
     * L'espace où l'on se tient. La première zone qui contient l'auditeur gagne
     * — pas la plus petite, pas une moyenne : deux zones sonores qui se
     * chevauchent sont une faute de description, et la moyenner reviendrait à
     * la cacher.
     *
     * Le passage d'une pièce à l'autre est AMORTI. Sans ça, franchir la porte
     * des toilettes fait apparaître la queue d'un coup, et l'oreille entend un
     * effet qui s'allume au lieu d'une pièce qui change. Une seconde de
     * constante : c'est le temps qu'on met à passer une porte.
     */
    float want_wet = 0.0f, want_decay = 0.0f;
    for (uint32_t i = 0; i < scene->sound_zone_count; ++i) {
        const ns_sound_zone *z = &scene->sound_zone[i];
        if (!ns_aabb_contains(z->bounds, view.position)) continue;
        want_wet = z->wet;
        want_decay = z->decay;
        break;
    }
    s->space_wet = ns_damp(s->space_wet, want_wet, 3.0f, dt);
    s->space_decay = ns_damp(s->space_decay, want_decay, 3.0f, dt);
    ns_audio_set_space(s->space_wet, s->space_decay);

    /* Le fond de salle suit son curseur, sans coupure : `ns_audio_voice_gain`
     * change le gain d'une boucle en cours. Régler « FOND DE SALLE » pendant
     * qu'on écoute est le seul moyen honnête de le régler. */
    if (s->voice_tone >= 0) {
        ns_audio_voice_gain(s->voice_tone, 0.42f * g_level[ROOM_LEVEL_TONE]);
    }

    /* --- les toilettes -------------------------------------------------- */
    update_doors(s, doors);
    update_flush(s, dt);
    update_coin_burst(s, dt);

    /* --- les pas ------------------------------------------------------- */
    const room_view_bob bob = room_camera_bob(cam, 1.0f);
    if (cam->mode == ROOM_CAM_PLAYER) {
        /* Le pas vient des PIEDS, pas des yeux. */
        ns_v3 feet = view.position;
        feet.y -= cam->eye_height;

        /* L'ATTERRISSAGE d'abord : c'est un front, et il doit passer avant le
         * seuil de distance — sinon un saut vers l'avant produit un pas ordinaire
         * au moment exact où l'on encaisse. */
        const bool just_landed = (!s->was_grounded && cam->grounded);
        if (just_landed) {
            /* `bob.land` vaut la vitesse de chute rapportée à 6 m/s, bornée à 1.
             * Une chute de vingt centimètres ne doit pas sonner comme une chute
             * d'un étage : l'intensité SUIT la chute, avec un plancher pour que
             * poser le pied s'entende quand même. */
            const float intensity = 0.55f + 0.95f * ns_clampf(bob.land, 0.0f, 1.0f);
            play_step(s, scene, cam, feet, RS_GAIT_LAND, intensity);
            s->left_foot = !s->left_foot;
            /* On réarme : après un saut, le pas suivant doit être à une foulée
             * entière, pas au reliquat de distance d'avant le saut. */
            s->last_step_distance = bob.distance;
        } else if (cam->grounded && bob.amount > 0.12f) {
            if (bob.distance - s->last_step_distance
                    >= room_camera_stride(cam) * RS_STEP_FRACTION) {
                s->last_step_distance = bob.distance;
                s->left_foot = !s->left_foot;
                const rs_gait gait = gait_of(cam, bob.amount);
                /* L'intensité suit l'amplitude réelle : plaqué contre un mur,
                 * `bob.amount` retombe et les pas s'éteignent avec lui. */
                play_step(s, scene, cam, feet, gait, ns_clampf(bob.amount, 0.3f, 1.0f));
            }
        } else if (bob.amount <= 0.12f) {
            /* À l'arrêt, on réarme : repartir ne doit pas attendre un demi-pas. */
            s->last_step_distance = bob.distance;
        }
        s->was_grounded = cam->grounded;
    } else {
        /* En vol libre ou en orbite, il n'y a pas de pieds. Sans ce réarmement,
         * reprendre la main en mode joueur déclencherait une salve de pas pour
         * rattraper la distance parcourue en volant. */
        s->last_step_distance = bob.distance;
        s->was_grounded = true;
    }

    /* --- occlusion, une source par image -------------------------------- */
    /*
     * `ns_bvh_occlusion_factor` lance trois rayons. Vingt et une sources par
     * image en feraient soixante-trois, pour une grandeur qui ne change qu'à la
     * vitesse où l'on marche. Un tourniquet suffit : chaque source est réévaluée
     * cinq fois par seconde à 60 images, et l'amortissement du mixeur lisse le
     * reste.
     *
     * L'extracteur et la rue sont DANS le tourniquet, et c'est le point : ce sont
     * les deux seules sources d'ambiance derrière un mur, donc les deux seules
     * où l'occlusion s'entend vraiment.
     */
    if (scene->bvh.loaded) {
        const uint32_t cabinets = (scene->cabinet_count < NS_MAX_CABINETS)
                                ? scene->cabinet_count : NS_MAX_CABINETS;
        const uint32_t n = cabinets + 2;
        const uint32_t i = s->occlusion_cursor % n;
        s->occlusion_cursor++;

        int   voice = NS_AUDIO_INVALID;
        ns_v3 at = ns_v3_zero();
        bool  live = false;

        if (i < cabinets) {
            voice = s->voice_cabinet[i];
            at = scene->cabinets[i].screen_center;
            live = true;
        } else if (i == cabinets && s->has_fan) {
            voice = s->voice_fan; at = s->fan_position; live = true;
        } else if (i == cabinets + 1 && s->has_street) {
            voice = s->voice_street; at = s->street_position; live = true;
        }

        if (live && voice >= 0) {
            ns_audio_voice_occlusion(voice,
                ns_bvh_occlusion_factor(&scene->bvh, view.position, at));
        }
    }

    ns_audio_update(dt);
}

/* --------------------------------------------------------------------------
 * Le Couperet
 * --------------------------------------------------------------------------
 * Le raisonnement complet — pourquoi cinq sons, lesquels sont placés dans la
 * salle et lesquels ne le sont pas, pourquoi deux d'entre eux ne varient ni en
 * hauteur ni en gain — est dans `room_sound.h`. Ce qui suit ne fait que
 * l'appliquer.
 */

/*
 * Les trois qui sont PLACÉS, par une fonction et trois jeux de bornes — même
 * construction que `play_jeton`, et pour la même raison : ce qui diffère
 * réellement d'un geste à l'autre tient dans quelques nombres, et trois copies
 * laisseraient trois endroits où corriger une portée.
 */
static void play_couperet(room_sound *s, int clip, ns_v3 at,
                          float radius, float max_distance,
                          float pitch_lo, float pitch_hi,
                          float gain_lo, float gain_hi)
{
    /* MUET plutôt qu'emprunté : voir `room_sound.h`. */
    if (!s->ready || clip < 0) return;

    ns_rng r;
    ns_rng_seed(&r, (uint64_t)s->rng++, 0x0CAFu);

    ns_audio_play_3d(clip, NS_BUS_SFX, at,
                     rand_range(&r, gain_lo, gain_hi),
                     rand_range(&r, pitch_lo, pitch_hi),
                     radius, max_distance);
}

void room_sound_couperet_tic(room_sound *s, float prochain)
{
    if (!s->ready) return;

    /*
     * Au-delà de dix secondes — et pendant le salon, où `prochain` vaut la
     * période entière —, on ne bat pas, ET ON RÉARME. Ce réarmement est ce qui
     * fait que la manche suivante recommence à dix : sans lui, le compteur
     * resterait sur « 1 » et le couperet d'après serait muet.
     */
    if (!(prochain > 0.0f) || prochain > RS_CP_TIC_DEPUIS) {
        s->cp_tic_seconde = 0;
        return;
    }

    /*
     * `ceilf` et non une troncature : à 9,99 s il reste « dix secondes » à
     * annoncer, pas neuf. La suite des valeurs prises est donc exactement
     * 10, 9, … 1 quand le compte descend de 10 à 0, soit dix tics.
     *
     * Ce test attrape aussi l'image longue : si le compte saute de 3,4 à 1,2, la
     * valeur passe de 4 à 2 et un seul tic part. Deux clips lancés à la même
     * image ne s'entendraient pas comme deux secondes.
     */
    const int seconde = (int)ceilf(prochain);
    if (seconde == s->cp_tic_seconde) return;
    s->cp_tic_seconde = seconde;

    if (s->clip_cp_tic < 0) return;

    /*
     * NI POSITION, NI HAUTEUR, NI GAIN TIRÉS AU SORT — les trois décisions sont
     * argumentées dans `room_sound.h`, et les trois vont dans le même sens : ce
     * son est une HORLOGE. Ce qu'il transmet est un compte, et un compte se
     * transmet en étant dix fois identique.
     *
     * Le gain est celui d'un son qui doit passer par-dessus la borne qu'on est
     * en train de jouer sans couvrir ce qu'elle raconte. Il ne suit PAS
     * `ROOM_LEVEL_STEPS` ni `ROOM_LEVEL_TONE` : ces deux réglages existent pour
     * baisser du décor, et le tic n'est pas du décor — c'est une règle du jeu.
     */
    ns_audio_play(s->clip_cp_tic, NS_BUS_SFX, 0.34f, 1.0f);
}

void room_sound_couperet_lame(room_sound *s)
{
    if (!s->ready || s->clip_cp_lame < 0) return;

    /* Le seul son du mode qui ait le droit d'être gros, et le seul qui parvienne
     * à l'identique aux huit joueurs. Ni position, ni variation : voir
     * `room_sound.h`. */
    ns_audio_play(s->clip_cp_lame, NS_BUS_SFX, 0.95f, 1.0f);
}

void room_sound_couperet_coupure(room_sound *s, ns_v3 position)
{
    /* Le plus fort des trois qui sont placés : c'est une borne qui meurt, et la
     * partie annulée avec elle. */
    play_couperet(s, s->clip_cp_coupure, position,
                  RS_CP_COUPURE_RADIUS, RS_CP_COUPURE_MAX,
                  0.96f, 1.04f, 0.85f, 1.00f);
}

void room_sound_couperet_blindage(room_sound *s, ns_v3 position)
{
    /* Le plus discret des trois, et c'est une décision et non un réglage : une
     * attaque encaissée est une bonne nouvelle, et une bonne nouvelle est un
     * accusé de réception. Elle n'a pas à traverser la salle. */
    play_couperet(s, s->clip_cp_blindage, position,
                  RS_CP_PLAQUE_RADIUS, RS_CP_PLAQUE_MAX,
                  0.95f, 1.05f, 0.70f, 0.85f);
}

void room_sound_couperet_renvoi(room_sound *s, ns_v3 position)
{
    /* Entre les deux : plus fort que le blindage parce qu'il annonce que
     * l'attaque REPART, moins que la coupure parce qu'il ne détruit rien. */
    play_couperet(s, s->clip_cp_renvoi, position,
                  RS_CP_PLAQUE_RADIUS, RS_CP_PLAQUE_MAX,
                  0.96f, 1.04f, 0.80f, 0.95f);
}

void room_sound_frappe(room_sound *s, ns_v3 position)
{
    if (!s->ready || s->clip_coup < 0) return;

    ns_rng r;
    ns_rng_seed(&r, (uint64_t)s->rng++, 0xC0DEu);

    /*
     * La plage de hauteur est ÉTROITE — sept pour cent de part et d'autre.
     * Au-delà, on n'entend plus la même borne d'un coup à l'autre : une
     * transposition de plus d'un demi-ton change la taille apparente du meuble,
     * et c'est précisément ce que la banque de pas reproche à la méthode de
     * 2020. Ici elle ne sert qu'à casser la répétition exacte.
     */
    const float pitch = rand_range(&r, 0.93f, 1.07f);
    const float gain  = rand_range(&r, 0.82f, 1.00f);

    /*
     * Portée courte : 1 à 14 m. Un coup sur une borne n'est pas un événement de
     * salle — il est fort là où on est et il ne porte pas jusqu'au bar. C'est
     * l'inverse de la chasse d'eau, qui doit s'entendre à travers une cloison.
     */
    ns_audio_play_3d(s->clip_coup, NS_BUS_SFX, position, gain, pitch, 1.0f, 14.0f);
}

