/*
 * test_place.c — les cinq contrôles de placement de `roomgen`, sur des pièces
 * fabriquées.
 *
 * CE QU'ILS EXISTENT POUR ATTRAPER. Vingt-trois défauts de placement mesurés
 * dans `docs/AUDIT-PLACEMENT.md`, et chacun avait en commun de ne RIEN produire
 * qui ressemble à sa cause : une radio murale tournée de 90° reste dans le
 * bâtiment, ne pénètre personne et touche son mur — les trois premiers contrôles
 * la voient passer. Un panneau enterré de deux centimètres ne rend aucun pixel,
 * ne lève aucune erreur, n'écrit rien dans le journal. Une suspension dont la
 * chaîne s'arrête à 1,20 m du plafond est une chaîne. Une source sans luminaire
 * éclaire. Un carrelage qui s'arrête à 3,15 m² de sa cloison est du sol.
 *
 * C-04 `check_facing` .............. une façade regarde la salle
 * C-05 `check_panel_visible` ....... un panneau d'épaisseur nulle n'est pas
 *                                    dans un mur
 * C-06 `check_hanging` ............. ce qui pend est accroché
 * C-07 `check_light_has_body` ...... une source a un luminaire
 * C-08 `check_floor_material` ...... une pièce a un sol, pas deux
 *
 * COMMENT C'EST ÉPROUVÉ, et pourquoi pas autrement. Les cinq contrôles vivent
 * dans `tools/roomgen.c`, qui est un programme et non une bibliothèque : ils
 * lisent un `rg_builder` que rien d'autre ne sait construire. On ne les appelle
 * donc pas — on APPELLE L'OUTIL, sur des descriptions de salle écrites ici, et
 * on lit ce qu'il refuse. Ce que ce test éprouve est donc exactement ce qui
 * tourne au build, messages compris : un contrôle qui refuserait au bon moment
 * en nommant le mauvais objet échouerait ici.
 *
 * LE CONTRE-CONTRÔLE COMPTE AUTANT QUE LE CONTRÔLE, et c'est la moitié des cas
 * ci-dessous. Un garde-fou qui refuse tout est un garde-fou qu'on retire dans
 * l'heure, et on retombe alors sur l'état d'avant — celui qui a mis quatre
 * objets sur le trottoir. Chaque refus est donc accompagné du cas voisin qui
 * doit PASSER : l'affiche tournée dans le bon sens, le vantail dans sa baie,
 * la tige qui touche le rail, la source dans son abat-jour, la pièce à deux
 * rectangles d'un même sol.
 *
 * Et chaque contrôle est éprouvé sur son SEUIL de part et d'autre, quand il en a
 * un : 4 cm de vide passent et 6 cm ne passent pas, 35 cm de luminaire passent
 * et 45 cm ne passent pas. Un seuil qu'on n'éprouve que d'un côté est un seuil
 * dont on ne sait pas s'il est là où on croit.
 *
 * Ni GPU, ni asset, ni salle : sept lignes de JSON et un plan carré.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define ns_popen  _popen
#  define ns_pclose _pclose
#else
#  define ns_popen  popen
#  define ns_pclose pclose
#endif

static int g_failures;
static int g_checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("  ÉCHEC %s:%d — ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

/* -------------------------------------------------------------------------- */
/* La pièce d'essai                                                            */
/* -------------------------------------------------------------------------- */

/*
 * Un carré de 6 x 4 m en ligne médiane, murs de 20 cm : le parement intérieur
 * est donc à x = ±2,90 et z = ±1,90. Le plafond pose ses dalles à 2,40 et
 * descend ses rails de 2 cm : la SOUS-FACE est à 2,38 — c'est la cote que C-06
 * mesure, et elle est écrite ici pour qu'on n'ait pas à la deviner en lisant les
 * cas.
 *
 * Le départ du joueur est à (−2,00 ; 0 ; 0), à l'ouest, et tous les objets
 * suspendus des cas ci-dessous sont à l'est : un obstacle posé sur le départ
 * ferait échouer C-10 avant que le contrôle éprouvé n'ait parlé, et le test
 * dirait alors quelque chose de vrai sur le mauvais contrôle.
 */
#define PAREMENT_NORD   1.90    /* z du parement intérieur du mur nord */
#define SOUS_FACE       2.38    /* y de la sous-face du plafond */

static const char *const MURS_CARRE =
    "{ \"name\": \"coquille\", \"closed\": true, \"material\": \"gris\","
    "  \"points\": [[-3,-2],[3,-2],[3,2],[-3,2]] }";

/* Le même carré, avec une porte de 1 m centrée sur x = 0 dans le mur NORD.
 * L'abscisse curviligne part du point 0 : 6 m sur le mur sud, 4 sur l'est, on
 * entre donc dans le mur nord à 10 et le milieu du pan tombe à 13. */
static const char *const MURS_AVEC_BAIE =
    "{ \"name\": \"coquille\", \"closed\": true, \"material\": \"gris\","
    "  \"points\": [[-3,-2],[3,-2],[3,2],[-3,2]],"
    "  \"openings\": [ { \"name\": \"porte\", \"offset\": 12.5, \"width\": 1.0,"
    "                    \"sill\": 0.0, \"head\": 2.0 } ] }";

/* Un plan à pan coupé : le coin nord-est est remplacé par une diagonale à 45°.
 * Il sert à vérifier qu'une affiche posée EN BIAIS sur un mur en biais donne
 * bien 1,00 — le contrôle mesure un angle relatif, pas une orientation absolue. */
static const char *const MURS_PAN_COUPE =
    "{ \"name\": \"coquille\", \"closed\": true, \"material\": \"gris\","
    "  \"points\": [[-3,-2],[3,-2],[3,0.5],[1.5,2],[-3,2]] }";

static const char *const SOL_UNI =
    "{ \"name\": \"sol\", \"material\": \"gris\", \"centre\": [0,0],"
    "  \"size\": [6.4,4.4], \"y\": 0.0 }";

static const char *const PLAFOND_UNI =
    "{ \"name\": \"plafond\", \"material\": \"gris\", \"centre\": [0,0],"
    "  \"size\": [6.4,4.4], \"y\": 2.40, \"tile\": 0.6, \"railDrop\": 0.02 }";

static const char *const ZONE_CENTRE =
    "{ \"name\": \"piece\", \"min\": [-2.5,0,-1.5], \"max\": [2.5,2.4,1.5],"
    "  \"wet\": 0.2, \"decay\": 0.2 }";

/* Un objet, pour que la description produise toujours de la géométrie de prop
 * même quand le cas éprouvé n'en demande pas. */
static const char *const PROP_NEUTRE =
    "{ \"name\": \"caisse\", \"at\": [-2.0, 0, -1.0], \"yaw\": 0,"
    "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
    "                 \"at\": [0,0,0], \"size\": [0.3,0.3,0.3] } ] }";

typedef struct piece {
    const char *sols;
    const char *murs;
    const char *plafonds;
    const char *props;
    const char *lumieres;
    const char *zones;
    const char *depart;
} piece;

static piece piece_base(void)
{
    piece p;
    p.sols     = SOL_UNI;
    p.murs     = MURS_CARRE;
    p.plafonds = PLAFOND_UNI;
    p.props    = PROP_NEUTRE;
    p.lumieres = "";
    p.zones    = ZONE_CENTRE;
    p.depart   = "[-2.0, 0, 0]";
    return p;
}

static const char *const MODELE =
    "{\n"
    "  \"units\": \"metres\",\n"
    "  \"name\": \"essai\",\n"
    "  \"room\": { \"height\": 2.5, \"wallThickness\": 0.20,\n"
    "              \"playable\": { \"min\": [-3,0,-2], \"max\": [3,2.5,2] } },\n"
    "  \"materials\": [ { \"name\": \"gris\", \"baseColor\": [0.5,0.5,0.5,1] },\n"
    "                   { \"name\": \"bleu\", \"baseColor\": [0.2,0.3,0.8,1] } ],\n"
    "  \"floors\": [ %s ],\n"
    "  \"walls\": [ %s ],\n"
    "  \"ceilings\": [ %s ],\n"
    "  \"props\": [ %s ],\n"
    "  \"lights\": [ %s ],\n"
    "  \"playerStart\": { \"position\": %s },\n"
    "  \"soundZones\": [ %s ]\n"
    "}\n";

static const char *g_roomgen;
static char g_out[1 << 16];

/*
 * Écrit la description, lance l'outil, garde tout ce qu'il a dit.
 *
 * On lit la SORTIE plutôt que le code de retour, et pour une raison : un
 * contrôle qui refuse est une chose, un contrôle qui refuse en NOMMANT l'objet
 * fautif au centimètre en est une autre, et c'est la seconde qui fait qu'on
 * corrige au lieu de chercher. Les deux se vérifient ici sur le même texte.
 */
static bool refuse(const piece *p)
{
    char json[8192];
    const int n = snprintf(json, sizeof json, MODELE, p->sols, p->murs, p->plafonds,
                           p->props, p->lumieres, p->depart, p->zones);
    if (n < 0 || (size_t)n >= sizeof json) {
        printf("  ÉCHEC : description tronquée (%d octets)\n", n);
        g_failures++;
        return false;
    }

    FILE *f = fopen("ns_test_place.room.json", "wb");
    if (!f) { printf("  ÉCHEC : impossible d'écrire la description\n"); g_failures++; return false; }
    fwrite(json, 1, (size_t)n, f);
    fclose(f);

    char cmd[2048];
    snprintf(cmd, sizeof cmd,
             "\"%s\" ns_test_place.room.json ns_test_place.gltf 2>&1", g_roomgen);

    g_out[0] = '\0';
    FILE *pp = ns_popen(cmd, "r");
    if (!pp) { printf("  ÉCHEC : impossible de lancer %s\n", g_roomgen); g_failures++; return false; }
    size_t used = 0;
    while (used + 1 < sizeof g_out) {
        const size_t got = fread(g_out + used, 1, sizeof g_out - used - 1, pp);
        if (got == 0) break;
        used += got;
    }
    g_out[used] = '\0';
    ns_pclose(pp);

    return strstr(g_out, "ERREUR") != NULL;
}

/* Le message contient-il ce mot ? Sert à vérifier que le refus DÉSIGNE. */
static bool dit(const char *needle)
{
    return strstr(g_out, needle) != NULL;
}

/* Le message est montré au premier échec : sans lui, « le contrôle n'a pas
 * refusé » n'aide personne à savoir pourquoi. */
static void montre(void)
{
    printf("      sortie de roomgen :\n%s\n", g_out);
}

/* -------------------------------------------------------------------------- */
/* Fabriques de morceaux                                                       */
/* -------------------------------------------------------------------------- */

static char g_props[4096];

/* Une affiche : un panneau d'épaisseur nulle, posé où l'on veut et tourné comme
 * on veut. C'est la pièce de base de C-04 et de C-05. */
static const char *affiche(double x, double y, double z, double yaw, double pitch)
{
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"affiche\", \"at\": [%.4f, %.4f, %.4f], \"yaw\": %.2f,"
             "  \"parts\": [ { \"type\": \"panel\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.5,0.4],"
             "                 \"pitch\": %.2f } ] }",
             PROP_NEUTRE, x, y, z, yaw, pitch);
    return g_props;
}

/* -------------------------------------------------------------------------- */
/* 1. C-04 — une façade regarde la salle                                       */
/* -------------------------------------------------------------------------- */

static void test_facing(void)
{
    printf("— C-04 : une façade regarde la salle\n");

    /* L'affiche est à 2 cm devant le parement nord et regarde le sud, donc la
     * salle. C'est le cas normal, et il doit passer. */
    piece p = piece_base();
    p.props = affiche(0.0, 1.20, PAREMENT_NORD - 0.02, 180.0, 0.0);
    bool ko = refuse(&p);
    CHECK(!ko, "une affiche tournée vers la salle doit passer");
    if (ko) montre();
    CHECK(dit("1 panneau(x) accroché(s) au mur"),
          "le contrôle doit DIRE qu'il a regardé un panneau — sans quoi il "
          "pourrait passer parce qu'il ne voit rien");

    /* LA MUTATION : le même objet, tourné d'un quart de tour. C'est P-07, la
     * radio murale qui se présentait de tranche. */
    p.props = affiche(0.0, 1.20, PAREMENT_NORD - 0.02, 90.0, 0.0);
    ko = refuse(&p);
    CHECK(ko, "une affiche tournée de 90° sur son mur doit être REFUSÉE");
    if (!ko) montre();
    CHECK(dit("affiche"), "le refus doit nommer l'objet fautif");
    CHECK(dit("0.00"), "le refus doit donner le produit scalaire mesuré (0,00)");

    /* Et retournée : elle regarde le mur. C'est P-04, le lavabo. */
    p.props = affiche(0.0, 1.20, PAREMENT_NORD - 0.02, 0.0, 0.0);
    ko = refuse(&p);
    CHECK(ko, "une affiche qui regarde son mur doit être REFUSÉE");
    CHECK(dit("-1.00"), "le refus doit donner le produit scalaire mesuré (−1,00)");

    /* CONTRE-CONTRÔLE : la même faute d'orientation, mais loin de tout mur. Le
     * contrôle ne juge que ce qui est ACCROCHÉ : une façade au milieu de la
     * pièce n'a pas de parement à regarder, et le lui reprocher reviendrait à
     * exiger que tout le mobilier soit tourné vers le mur le plus proche. */
    p.props = affiche(0.0, 1.20, 0.0, 90.0, 0.0);
    ko = refuse(&p);
    CHECK(!ko, "une façade au milieu de la pièce n'est pas jugée");
    if (ko) montre();
    CHECK(dit("0 panneau(x) accroché(s) au mur"),
          "et le contrôle doit dire qu'il n'a rien regardé, plutôt que de "
          "laisser croire qu'il a validé");

    /* LE SEUIL DE DISTANCE, des deux côtés. « Accroché » vaut 15 cm : à 14 cm
     * l'affiche est jugée, à 16 cm elle ne l'est plus. */
    p.props = affiche(0.0, 1.20, PAREMENT_NORD - 0.14, 90.0, 0.0);
    ko = refuse(&p);
    CHECK(ko, "à 14 cm du parement, l'affiche est encore accrochée : refus");
    p.props = affiche(0.0, 1.20, PAREMENT_NORD - 0.16, 90.0, 0.0);
    ko = refuse(&p);
    CHECK(!ko, "à 16 cm du parement, elle ne l'est plus : le contrôle passe");
    if (ko) montre();

    /* CONTRE-CONTRÔLE : un panneau COUCHÉ. La vasque d'un lavabo regarde le
     * ciel ; lui demander de regarder la salle n'a pas de sens. */
    p.props = affiche(0.0, 1.20, PAREMENT_NORD - 0.02, 0.0, -90.0);
    ko = refuse(&p);
    CHECK(!ko, "un panneau couché n'a pas de façade : il n'est pas jugé");
    if (ko) montre();

    /* LE PAN COUPÉ : l'affiche suit le mur en biais, donc sa normale aussi. Le
     * produit scalaire vaut 1,00 alors qu'aucune des deux directions n'est
     * alignée sur un axe. C'est ce qui distingue « regarde la salle » de
     * « regarde le nord ». */
    p = piece_base();
    p.murs = MURS_PAN_COUPE;
    /* Milieu du pan coupé, rentré de 12 cm le long de sa normale rentrante
     * (−1, −1)/√2 : (2,25 ; 1,25) devient (2,165 ; 1,165). Le lacet 225° donne
     * (sin, cos) = (−0,707 ; −0,707), qui est cette normale-là. */
    p.props = affiche(2.165, 1.20, 1.165, 225.0, 0.0);
    ko = refuse(&p);
    CHECK(!ko, "une affiche en biais sur un mur en biais doit passer");
    if (ko) montre();
    CHECK(dit("1 panneau(x) accroché(s) au mur"),
          "et elle doit bien avoir été jugée, pas ignorée");

    /* La même, tournée de 90° : elle longe le pan coupé. */
    p.props = affiche(2.165, 1.20, 1.165, 315.0, 0.0);
    ko = refuse(&p);
    CHECK(ko, "sur un mur en biais aussi, longer le mur est refusé");
    if (!ko) montre();

    /* LA SECONDE BRANCHE : un prop qui DÉCLARE « pose »: « mur ». Elle est
     * inerte sur la vraie salle, où aucun prop ne porte la clé ; c'est ici
     * qu'elle s'éprouve. */
    p = piece_base();
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"meuble\", \"at\": [0, 0, %.4f], \"yaw\": 180,"
             "  \"pose\": \"mur\","
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.8,1.0,0.2] } ] }",
             PROP_NEUTRE, PAREMENT_NORD - 0.10 + 0.001);
    p.props = g_props;
    ko = refuse(&p);
    CHECK(!ko, "un meuble déclaré adossé et tourné vers la salle doit passer");
    if (ko) montre();
    CHECK(dit("1 prop(s) déclaré(s) « mur »"),
          "et la branche « pose » doit dire qu'elle a servi");

    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"meuble\", \"at\": [0, 0, %.4f], \"yaw\": 90,"
             "  \"pose\": \"mur\","
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.2,1.0,0.8] } ] }",
             PROP_NEUTRE, PAREMENT_NORD - 0.10 + 0.001);
    p.props = g_props;
    ko = refuse(&p);
    CHECK(ko, "un meuble déclaré adossé qui longe son mur doit être REFUSÉ");
    if (!ko) montre();
    CHECK(dit("meuble"), "et le refus doit le nommer");
    CHECK(dit("lacet"), "et dire de quel lacet il s'agit — c'est ce qu'on corrige");
}

/* -------------------------------------------------------------------------- */
/* 2. C-05 — un panneau d'épaisseur nulle n'est pas dans un mur                */
/* -------------------------------------------------------------------------- */

static void test_panel_visible(void)
{
    printf("— C-05 : un panneau d'épaisseur nulle n'est pas dans un mur\n");

    /* Devant son parement : visible. */
    piece p = piece_base();
    p.props = affiche(0.0, 1.20, PAREMENT_NORD - 0.02, 180.0, 0.0);
    bool ko = refuse(&p);
    CHECK(!ko, "une affiche 2 cm devant son parement doit passer");
    if (ko) montre();
    CHECK(dit("panneau(x) : le plus juste"),
          "le contrôle doit dire combien de panneaux il a regardés");

    /* LA MUTATION : deux centimètres de l'autre côté. C'est P-08 mot pour mot —
     * « quatre centimètres suffisaient a le faire disparaitre de moitie ». */
    p.props = affiche(0.0, 1.20, PAREMENT_NORD + 0.02, 180.0, 0.0);
    ko = refuse(&p);
    CHECK(ko, "une affiche 2 cm DERRIÈRE son parement doit être REFUSÉE");
    if (!ko) montre();
    CHECK(dit("ENTERRÉ"), "le refus doit dire ce qui se passe");
    CHECK(dit("affiche"), "et nommer l'objet");
    CHECK(dit("2.0 cm"), "et donner la profondeur mesurée, au millimètre");

    /* Le seuil est le PAREMENT, pas une tolérance : 1 mm derrière suffit à
     * occulter, et c'est exactement le cas qu'a produit le tableau des scores de
     * la vraie salle. */
    p.props = affiche(0.0, 1.20, PAREMENT_NORD + 0.001, 180.0, 0.0);
    ko = refuse(&p);
    CHECK(ko, "1 mm derrière le parement suffit : le mur occulte le panneau");
    p.props = affiche(0.0, 1.20, PAREMENT_NORD - 0.001, 180.0, 0.0);
    ko = refuse(&p);
    CHECK(!ko, "1 mm devant le parement passe : le panneau est dans la pièce");
    if (ko) montre();

    /* CONTRE-CONTRÔLE : un vantail AU MILIEU du mur, mais dans une baie
     * déclarée. C'est là qu'une porte se met, et le refuser ferait refuser
     * toutes les portes de la salle. */
    p = piece_base();
    p.murs = MURS_AVEC_BAIE;
    p.props = affiche(0.0, 1.00, 2.00, 180.0, 0.0);
    ko = refuse(&p);
    CHECK(!ko, "un vantail dans sa baie doit passer, même sur la ligne médiane");
    if (ko) montre();

    /* Le même vantail, la baie retirée : le mur est plein, il est enterré. */
    p.murs = MURS_CARRE;
    ko = refuse(&p);
    CHECK(ko, "le même vantail sans baie est enterré : REFUS");
    if (!ko) montre();
    CHECK(dit("10.0 cm"), "et la profondeur est la demi-épaisseur : 10 cm");

    /* Et au-dessus du linteau : la baie s'arrête à 2,00 m, le panneau est à
     * 2,20 m, donc dans le plein. C'est P-15, le bloc SORTIE. */
    p.murs = MURS_AVEC_BAIE;
    p.props = affiche(0.0, 2.20, 2.00, 180.0, 0.0);
    ko = refuse(&p);
    CHECK(ko, "au-dessus du linteau, la baie n'excuse plus rien : REFUS");
    if (!ko) montre();

    /* Et à côté de la baie : même hauteur, mais décalé de 1,20 m en x, hors des
     * 1,00 m de la porte. */
    p.props = affiche(1.20, 1.00, 2.00, 180.0, 0.0);
    ko = refuse(&p);
    CHECK(ko, "à côté de la baie, le mur est plein : REFUS");
    if (!ko) montre();

    /* PLAQUÉ CONTRE UN AUTRE OBJET. La caisse neutre occupe x −2,15..−1,85 et
     * z −1,15..−0,85 ; le panneau se pose 2 mm devant sa face nord et regarde
     * dedans. */
    p = piece_base();
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"plaque\", \"at\": [-2.0, 0.15, -0.848], \"yaw\": 180,"
             "  \"parts\": [ { \"type\": \"panel\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.2,0.2] } ] }",
             PROP_NEUTRE);
    p.props = g_props;
    ko = refuse(&p);
    CHECK(ko, "un panneau plaqué 2 mm contre un AUTRE objet doit être REFUSÉ");
    if (!ko) montre();
    CHECK(dit("PLAQUÉ"), "le refus doit dire ce qui se passe");
    CHECK(dit("caisse"), "et nommer ce contre quoi il est plaqué");

    /* CONTRE-CONTRÔLE : le même écart de 2 mm, mais contre SON PROPRE caisson.
     * C'est ainsi que les façades de la vraie salle sont posées — « la meme
     * marge de deux millimetres que la vitrine a lots et le monnayeur » — et un
     * contrôle qui les refuserait serait retiré le jour même. */
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"borne\", \"at\": [-2.0, 0, 1.2], \"yaw\": 0,"
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.5,1.0,0.4] },"
             "               { \"type\": \"panel\", \"material\": \"bleu\","
             "                 \"at\": [0,0.5,0.202], \"size\": [0.3,0.3] } ] }",
             PROP_NEUTRE);
    p.props = g_props;
    ko = refuse(&p);
    CHECK(!ko, "une façade 2 mm devant SON caisson doit passer");
    if (ko) montre();

    /*
     * LE CAS QUI JUSTIFIE L'EXCEPTION, et sans lui elle ne serait pas éprouvée :
     * une glace posée 1 mm devant la planche qu'elle protège, les deux morceaux
     * du MÊME objet. C'est la vitrine à lots. Le rayon part de la planche, entre
     * aussitôt dans la glace — et n'a rien à en dire, puisque c'est le même
     * meuble.
     */
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"vitrine\", \"at\": [-2.0, 0, 1.2], \"yaw\": 0,"
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.5,1.0,0.4] },"
             "               { \"type\": \"panel\", \"material\": \"bleu\","
             "                 \"at\": [0,0.5,0.202], \"size\": [0.3,0.3] },"
             "               { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0.35,0.205], \"size\": [0.32,0.32,0.004],"
             "                 \"chamfer\": 0.001 } ] }",
             PROP_NEUTRE);
    p.props = g_props;
    ko = refuse(&p);
    CHECK(!ko, "une glace 1 mm devant SA planche, même objet, doit passer");
    if (ko) montre();

    /* LE CONTRE-CONTRÔLE DE L'EXCEPTION : la même glace, mais déclarée comme un
     * objet à part. Un millimètre entre deux MEUBLES n'est plus une marge de
     * fabrication : c'est un objet dans un autre, et la planche ne rendra rien. */
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"vitrine\", \"at\": [-2.0, 0, 1.2], \"yaw\": 0,"
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.5,1.0,0.4] },"
             "               { \"type\": \"panel\", \"material\": \"bleu\","
             "                 \"at\": [0,0.5,0.202], \"size\": [0.3,0.3] } ] },"
             "{ \"name\": \"glace\", \"at\": [-2.0, 0.35, 1.405], \"yaw\": 0,"
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.32,0.32,0.004],"
             "                 \"chamfer\": 0.001 } ] }",
             PROP_NEUTRE);
    p.props = g_props;
    ko = refuse(&p);
    CHECK(ko, "la même glace déclarée à part est un objet DANS un autre : REFUS");
    if (!ko) montre();
    CHECK(dit("PLAQUÉ") && dit("glace"),
          "et le refus doit nommer ce contre quoi la planche est plaquée");

    /* Et la même façade retournée, donc plaquée dans son propre caisson : le
     * rayon ignore l'objet dont il part, donc ce cas-là n'est PAS vu par C-05.
     * Il est écrit pour que la limite soit dans le dépôt et non dans une tête :
     * c'est C-04 qui l'attrape, si le caisson est adossé à un mur. */
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"borne\", \"at\": [-2.0, 0, 1.2], \"yaw\": 0,"
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.5,1.0,0.4] },"
             "               { \"type\": \"panel\", \"material\": \"bleu\","
             "                 \"at\": [0,0.5,0.198], \"yaw\": 180,"
             "                 \"size\": [0.3,0.3] } ] }",
             PROP_NEUTRE);
    p.props = g_props;
    ko = refuse(&p);
    CHECK(!ko, "une façade retournée DANS son propre caisson n'est pas vue par "
               "C-05 — limite écrite, pas oubliée");
    if (ko) montre();
}

/* -------------------------------------------------------------------------- */
/* 3. C-06 — ce qui pend est accroché                                          */
/* -------------------------------------------------------------------------- */

/* Une tige verticale, posée à l'est pour ne jamais gêner le départ du joueur. */
static const char *tige(double base, double hauteur, const char *pose,
                        const char *suspendu_par)
{
    char extra[128];
    extra[0] = '\0';
    if (suspendu_par && suspendu_par[0]) {
        snprintf(extra, sizeof extra, " \"suspenduPar\": \"%s\",", suspendu_par);
    }
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"tige\", \"at\": [2.0, %.4f, 1.0], \"yaw\": 0,"
             "  \"pose\": \"%s\",%s"
             "  \"parts\": [ { \"type\": \"cylinder\", \"material\": \"gris\","
             "                 \"radius\": 0.02, \"height\": %.4f, \"sides\": 8 } ] }",
             PROP_NEUTRE, base, pose, extra, hauteur);
    return g_props;
}

static void test_hanging(void)
{
    printf("— C-06 : ce qui pend est accroché\n");

    /* La tige monte jusqu'à la sous-face : rien ne pend. */
    piece p = piece_base();
    p.props = tige(1.50, SOUS_FACE - 1.50, "suspendu", NULL);
    bool ko = refuse(&p);
    CHECK(!ko, "une tige qui touche la sous-face du plafond doit passer");
    if (ko) montre();
    CHECK(dit("1 suspension(s) confrontée(s)"),
          "le contrôle doit DIRE qu'il a regardé — sur la vraie salle il ne "
          "regarde rien, et c'est le seul endroit où on peut le vérifier");

    /* LA MUTATION : on raccourcit de 20 cm. C'est P-10, la suspension du
     * comptoir dont la chaîne s'arrêtait à 1,20 m du plafond. */
    p.props = tige(1.50, SOUS_FACE - 1.50 - 0.20, "suspendu", NULL);
    ko = refuse(&p);
    CHECK(ko, "une tige 20 cm trop courte doit être REFUSÉE");
    if (!ko) montre();
    CHECK(dit("tige"), "le refus doit nommer l'objet");
    CHECK(dit("0.200"), "et donner le vide mesuré, au millimètre");
    CHECK(dit("pend dans le vide"), "et dire ce que ça veut dire");

    /* LE SEUIL, DES DEUX CÔTÉS. 5 cm est la limite : 4 cm passent, 6 cm non. */
    p.props = tige(1.50, SOUS_FACE - 1.50 - 0.04, "suspendu", NULL);
    ko = refuse(&p);
    CHECK(!ko, "4 cm de vide passent");
    if (ko) montre();
    p.props = tige(1.50, SOUS_FACE - 1.50 - 0.06, "suspendu", NULL);
    ko = refuse(&p);
    CHECK(ko, "6 cm de vide ne passent pas");
    if (!ko) montre();

    /* CONTRE-CONTRÔLE : la même tige trop courte, mais déclarée « libre ». Le
     * contrôle ne juge que ce qui se déclare suspendu, et il doit le dire. */
    p.props = tige(1.50, SOUS_FACE - 1.50 - 0.20, "libre", NULL);
    ko = refuse(&p);
    CHECK(!ko, "une tige déclarée « libre » n'est pas jugée");
    if (ko) montre();
    CHECK(dit("AUCUN objet ne déclare"),
          "et le contrôle doit avouer qu'il n'a rien vérifié, plutôt que de "
          "laisser croire qu'il a validé");

    /* LE CÂBLE. L'abat-jour n'a aucune raison de toucher le plafond ; il déclare
     * la tige qui l'y tient, et c'est ELLE qui est mesurée. */
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"lustre\", \"at\": [2.0, 1.60, 1.0], \"yaw\": 0,"
             "  \"pose\": \"suspendu\", \"suspenduPar\": \"cable\","
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.3,0.3,0.3] } ] },"
             "{ \"name\": \"cable\", \"at\": [2.0, 1.90, 1.0], \"yaw\": 0,"
             "  \"parts\": [ { \"type\": \"cylinder\", \"material\": \"gris\","
             "                 \"radius\": 0.015, \"height\": %.4f, \"sides\": 8 } ] }",
             PROP_NEUTRE, SOUS_FACE - 1.90);
    p.props = g_props;
    ko = refuse(&p);
    CHECK(!ko, "un lustre à 78 cm du plafond passe si son câble, lui, y monte");
    if (ko) montre();
    CHECK(dit("(1 par leur câble)"), "et le contrôle doit dire qu'il a suivi le câble");

    /* Le même câble, 15 cm trop court. */
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"lustre\", \"at\": [2.0, 1.60, 1.0], \"yaw\": 0,"
             "  \"pose\": \"suspendu\", \"suspenduPar\": \"cable\","
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.3,0.3,0.3] } ] },"
             "{ \"name\": \"cable\", \"at\": [2.0, 1.90, 1.0], \"yaw\": 0,"
             "  \"parts\": [ { \"type\": \"cylinder\", \"material\": \"gris\","
             "                 \"radius\": 0.015, \"height\": %.4f, \"sides\": 8 } ] }",
             PROP_NEUTRE, SOUS_FACE - 1.90 - 0.15);
    p.props = g_props;
    ko = refuse(&p);
    CHECK(ko, "un câble 15 cm trop court doit être REFUSÉ");
    if (!ko) montre();
    CHECK(dit("cable"), "et le refus doit nommer LE CÂBLE, pas seulement le lustre");
    CHECK(dit("0.150"), "et donner son vide mesuré");

    /* Un câble qui n'existe pas : le pire cas, parce qu'il éteint le contrôle
     * en silence si personne ne le vérifie. */
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"lustre\", \"at\": [2.0, 1.60, 1.0], \"yaw\": 0,"
             "  \"pose\": \"suspendu\", \"suspenduPar\": \"chaine_absente\","
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.3,0.3,0.3] } ] }",
             PROP_NEUTRE);
    p.props = g_props;
    ko = refuse(&p);
    CHECK(ko, "un « suspenduPar » qui ne désigne rien doit être REFUSÉ");
    if (!ko) montre();
    CHECK(dit("chaine_absente"), "et le refus doit citer le nom qui ne désigne rien");

    /* Suspendu là où il n'y a pas de plafond. C'est arrivé deux fois dans la
     * vraie salle — au bout du couloir et au-dessus des lavabos. */
    p = piece_base();
    p.plafonds = "{ \"name\": \"plafond\", \"material\": \"gris\","
                 "  \"centre\": [-1.5,0], \"size\": [3.0,4.4], \"y\": 2.40,"
                 "  \"tile\": 0.6, \"railDrop\": 0.02 }";
    p.props = tige(1.50, 0.88, "suspendu", NULL);
    ko = refuse(&p);
    CHECK(ko, "suspendu sous un trou de plafond : REFUS");
    if (!ko) montre();
    CHECK(dit("AUCUN plafond"), "et le refus doit dire que c'est le plafond qui manque");
}

/* -------------------------------------------------------------------------- */
/* 4. C-07 — une source de lumière a un luminaire                              */
/* -------------------------------------------------------------------------- */

/* Une lampe : un cube posé à hauteur voulue, et sa source dessous. */
static const char *lampe(double base)
{
    snprintf(g_props, sizeof g_props,
             "%s,"
             "{ \"name\": \"applique\", \"at\": [1.0, %.4f, 1.0], \"yaw\": 0,"
             "  \"parts\": [ { \"type\": \"box\", \"material\": \"gris\","
             "                 \"at\": [0,0,0], \"size\": [0.2,0.2,0.2] } ] }",
             PROP_NEUTRE, base);
    return g_props;
}

static void test_light_has_body(void)
{
    printf("— C-07 : une source de lumière a un luminaire\n");

    /* La source est 10 cm sous son luminaire : elle a une cause visible. */
    piece p = piece_base();
    p.props = lampe(0.90);
    p.lumieres = "{ \"name\": \"lampe\", \"at\": [1.0, 0.80, 1.0],"
                 "  \"intensity\": 40, \"range\": 3 }";
    bool ko = refuse(&p);
    CHECK(!ko, "une source 10 cm sous son luminaire doit passer");
    if (ko) montre();
    CHECK(dit("1 source(s) confrontée(s)"),
          "le contrôle doit dire combien de sources il a regardées");

    /* LA MUTATION : on retire le luminaire. C'est la moitié de la section
     * « Ce qui manque » de l'audit, et onze sources de la vraie salle. */
    p.props = PROP_NEUTRE;
    ko = refuse(&p);
    CHECK(ko, "une source sans luminaire doit être REFUSÉE");
    if (!ko) montre();
    CHECK(dit("AUCUN luminaire"), "le refus doit dire ce qui manque");
    CHECK(dit("lampe"), "et nommer la source");
    CHECK(dit("caisse"), "et nommer le prop le plus proche, avec sa distance");

    /* LE SEUIL, DES DEUX CÔTÉS. 40 cm : un luminaire à 35 cm passe, à 45 cm
     * non. Le seuil a été posé entre deux populations mesurées sur la vraie
     * salle — 18 cm au pire pour les sources équipées, 73 cm au mieux pour les
     * autres — et c'est ici qu'on vérifie qu'il est bien là où on croit. */
    p.props = lampe(0.80 + 0.35);
    ko = refuse(&p);
    CHECK(!ko, "un luminaire à 35 cm passe");
    if (ko) montre();
    p.props = lampe(0.80 + 0.45);
    ko = refuse(&p);
    CHECK(ko, "un luminaire à 45 cm ne passe pas");
    if (!ko) montre();

    /* L'ÉCHAPPATOIRE, et il faut l'écrire. */
    p.props = PROP_NEUTRE;
    p.lumieres = "{ \"name\": \"tube\", \"at\": [1.0, 0.80, 1.0],"
                 "  \"intensity\": 40, \"range\": 3, \"nu\": true }";
    ko = refuse(&p);
    CHECK(!ko, "une source déclarée « nu » passe sans luminaire");
    if (ko) montre();
    CHECK(dit("1 déclarée(s) nue(s)"), "et le contrôle doit le compter à part");

    /* Une dalle de faux plafond EST son luminaire : `parse_ceilings` la pose à
     * partir de cette liste-là, et lui demander un prop en plus ferait refuser
     * tout le plafond. */
    p.lumieres = "{ \"name\": \"plafonnier\", \"at\": [0.0, 2.30, 0.0],"
                 "  \"intensity\": 40, \"range\": 3, \"ceilingPanel\": true,"
                 "  \"panelSize\": [1.2, 0.6] }";
    ko = refuse(&p);
    CHECK(!ko, "une dalle de faux plafond n'a pas besoin de prop");
    if (ko) montre();
    CHECK(dit("0 source(s) confrontée(s)"),
          "et elle ne doit même pas être comptée dans les sources confrontées");
}

/* -------------------------------------------------------------------------- */
/* 5. C-08 — une pièce a un sol, pas deux                                      */
/* -------------------------------------------------------------------------- */

static void test_floor_material(void)
{
    printf("— C-08 : une pièce a un sol, pas deux\n");

    /* Un sol, un matériau, une zone : le cas normal. */
    piece p = piece_base();
    bool ko = refuse(&p);
    CHECK(!ko, "une pièce sur un sol uni doit passer");
    if (ko) montre();
    CHECK(dit("1 zone(s) confrontée(s) à leur matériau"),
          "le contrôle doit dire combien de zones il a regardées");

    /* LA MUTATION : deux rectangles, deux matériaux, une zone à cheval. C'est
     * P-09 — 28,5 % de moquette d'arcade dans un bloc sanitaire carrelé. */
    p.sols = "{ \"name\": \"sol_ouest\", \"material\": \"gris\","
             "  \"centre\": [-1.6,0], \"size\": [3.2,4.4], \"y\": 0.0 },"
             "{ \"name\": \"sol_est\", \"material\": \"bleu\","
             "  \"centre\": [1.6,0], \"size\": [3.2,4.4], \"y\": 0.0 }";
    ko = refuse(&p);
    CHECK(ko, "une zone à cheval sur deux matériaux doit être REFUSÉE");
    if (!ko) montre();
    CHECK(dit("mélange DEUX sols"), "le refus doit dire ce qui se passe");
    CHECK(dit("sol_ouest") && dit("sol_est"), "et nommer les DEUX rectangles");
    CHECK(dit("gris") && dit("bleu"), "et les DEUX matériaux");

    /* CONTRE-CONTRÔLE, et c'est le piège que l'audit décrit : DEUX rectangles
     * d'un MÊME matériau sont parfaitement légitimes. La zone du sas de la vraie
     * salle est ainsi couverte par « sol_hall » et « sol_sas », qui portent tous
     * deux la moquette. Un contrôle qui compterait les rectangles refuserait la
     * salle telle qu'elle est. */
    p.sols = "{ \"name\": \"sol_ouest\", \"material\": \"gris\","
             "  \"centre\": [-1.6,0], \"size\": [3.2,4.4], \"y\": 0.0 },"
             "{ \"name\": \"sol_est\", \"material\": \"gris\","
             "  \"centre\": [1.6,0], \"size\": [3.2,4.4], \"y\": 0.0 }";
    ko = refuse(&p);
    CHECK(!ko, "deux rectangles d'un MÊME matériau doivent passer");
    if (ko) montre();

    /* LA DÉCLARATION. Une zone peut dire le sol qu'elle accepte. */
    p.sols = "{ \"name\": \"sol_ouest\", \"material\": \"gris\","
             "  \"centre\": [-1.6,0], \"size\": [3.2,4.4], \"y\": 0.0 },"
             "{ \"name\": \"sol_est\", \"material\": \"bleu\","
             "  \"centre\": [1.6,0], \"size\": [3.2,4.4], \"y\": 0.0 }";
    p.zones = "{ \"name\": \"piece\", \"min\": [0.5,0,-1.5], \"max\": [2.5,2.4,1.5],"
              "  \"wet\": 0.2, \"decay\": 0.2, \"sol\": \"bleu\" }";
    ko = refuse(&p);
    CHECK(!ko, "une zone qui déclare son sol et l'obtient doit passer");
    if (ko) montre();
    CHECK(dit("1 déclarant le leur)"), "et le contrôle doit compter la déclaration");

    /* Et une déclaration qui ment est PIRE qu'une absence de déclaration :
     * elle éteint le contrôle en affirmant le contraire de la mesure. */
    p.zones = "{ \"name\": \"piece\", \"min\": [0.5,0,-1.5], \"max\": [2.5,2.4,1.5],"
              "  \"wet\": 0.2, \"decay\": 0.2, \"sol\": \"gris\" }";
    ko = refuse(&p);
    CHECK(ko, "une zone qui déclare un sol qu'elle n'a pas doit être REFUSÉE");
    if (!ko) montre();
    CHECK(dit("déclare le sol"), "et le refus doit citer la déclaration");

    /* UN TROU. Le sol s'arrête au milieu de la pièce. */
    p = piece_base();
    p.sols = "{ \"name\": \"sol\", \"material\": \"gris\","
             "  \"centre\": [-1.7,0], \"size\": [3.0,4.4], \"y\": 0.0 }";
    ko = refuse(&p);
    CHECK(ko, "un trou dans le sol foulable doit être REFUSÉ");
    if (!ko) montre();
    CHECK(dit("AUCUN sol sous"), "et le refus doit donner le point où ça manque");

    /* DEUX DALLES COPLANAIRES qui se recouvrent là où l'on marche. */
    p.sols = "{ \"name\": \"sol_ouest\", \"material\": \"gris\","
             "  \"centre\": [-1.35,0], \"size\": [3.7,4.4], \"y\": 0.0 },"
             "{ \"name\": \"sol_est\", \"material\": \"gris\","
             "  \"centre\": [1.35,0], \"size\": [3.7,4.4], \"y\": 0.0 }";
    ko = refuse(&p);
    CHECK(ko, "deux dalles coplanaires qui se recouvrent doivent être REFUSÉES");
    if (!ko) montre();
    CHECK(dit("COPLANAIRES"), "le refus doit dire ce qui se passe");
    CHECK(dit("tampon de profondeur"),
          "et dire pourquoi c'est un défaut qu'on ne voit qu'en mouvement");

    /* CONTRE-CONTRÔLE : le même recouvrement, mais SOUS un mur. Deux surfaces
     * coplanaires sous 20 cm de brique ne scintillent pas : elles ne se voient
     * pas. Le contrôle qui les refuserait serait retiré le jour même — c'est ce
     * que la vraie salle produit entre « sol_hall » et « sol_toilettes », sur
     * 0,157 m². */
    p.sols = "{ \"name\": \"sol_bord\", \"material\": \"gris\","
             "  \"centre\": [-3.075,0], \"size\": [0.25,4.4], \"y\": 0.0 },"
             "{ \"name\": \"sol_piece\", \"material\": \"gris\","
             "  \"centre\": [0.105,0], \"size\": [6.19,4.4], \"y\": 0.0 }";
    ko = refuse(&p);
    CHECK(!ko, "un recouvrement entièrement SOUS un mur doit passer");
    if (ko) montre();

    /* Une zone posée hors du bâtiment ne se déclenche jamais : elle règle la
     * réverbération d'un endroit où personne ne va. */
    p = piece_base();
    p.zones = "{ \"name\": \"ailleurs\", \"min\": [8,0,8], \"max\": [10,2.4,10],"
              "  \"wet\": 0.2, \"decay\": 0.2 }";
    ko = refuse(&p);
    CHECK(ko, "une zone sonore hors du bâtiment doit être REFUSÉE");
    if (!ko) montre();
    CHECK(dit("AUCUN sol foulable"), "et le refus doit dire pourquoi");
}

/* -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage : %s <chemin de roomgen>\n", argv[0]);
        return 2;
    }
    g_roomgen = argv[1];

    printf("test_place — placement : C-04 à C-08 (%s)\n", g_roomgen);
    test_facing();
    test_panel_visible();
    test_hanging();
    test_light_has_body();
    test_floor_material();

    remove("ns_test_place.room.json");
    remove("ns_test_place.gltf");
    remove("ns_test_place.bin");
    remove("ns_test_place.lights.json");
    remove("ns_test_place.scene.json");

    printf("%d contrôle(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
