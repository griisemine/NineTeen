/*
 * test_comptoir.c — le comptoir, sans écran et sans serveur.
 *
 * CE QU'ON VÉRIFIE, ET POURQUOI CE N'EST PAS LE DESSIN. Un écran d'accueil qui
 * dessine mal se remarque au premier coup d'œil ; un écran d'accueil qui NAVIGUE
 * mal envoie le joueur sur une page qu'il ne peut pas quitter, ou fabrique deux
 * salons pour un double clic, et ça ne se remarque qu'une fois chez le joueur.
 *
 * Le comptoir a été écrit pour rendre ça vérifiable : il ne connaît ni SDL, ni
 * les sockets. On lui repose un état de réseau à la main, on lui envoie des
 * actions, on lit les demandes qui en sortent. C'est toute la raison du
 * découpage en trois modules.
 */
#include "ns_core.h"
#include "room_comptoir.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            printf("ÉCHEC %s:%d — ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* Raccourcis : le comptoir ne se pilote qu'avec sept actions. */
static void haut(room_comptoir *c)    { room_comptoir_agir(c, ROOM_CT_HAUT); }
static void bas(room_comptoir *c)     { room_comptoir_agir(c, ROOM_CT_BAS); }
static void gauche(room_comptoir *c)  { room_comptoir_agir(c, ROOM_CT_GAUCHE); }
static void droite(room_comptoir *c)  { room_comptoir_agir(c, ROOM_CT_DROITE); }
static void entree(room_comptoir *c)  { room_comptoir_agir(c, ROOM_CT_VALIDER); }
static void echap(room_comptoir *c)   { room_comptoir_agir(c, ROOM_CT_ANNULER); }

static void anonyme(room_comptoir *c)
{
    room_comptoir_poser_reseau(c, NS_COMPTE_ANONYME, false, "", "pas encore connecté");
}

static void connecte(room_comptoir *c, const char *pseudo)
{
    room_comptoir_poser_reseau(c, NS_COMPTE_CONNECTE, false, pseudo, "connecté");
}

/* ==========================================================================
 * Sans serveur, rien ne doit être proposé
 * ========================================================================== */

static void test_eteint(void)
{
    room_comptoir c;
    room_comptoir_init(&c);
    room_comptoir_poser_reseau(&c, NS_COMPTE_ETEINT, false, "", "hors ligne");

    /*
     * La page éteinte n'offre qu'une sortie. Le piège serait d'offrir « SE
     * CONNECTER » quand aucun serveur n'est configuré : le joueur taperait son
     * mot de passe pour rien, et le refus n'aurait aucun rapport avec lui.
     */
    entree(&c);
    CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_FERMER,
          "sans serveur, la seule action est de retourner dans la salle");
    CHECK(c.page == ROOM_CT_ACCUEIL, "et l'on ne quitte pas la page d'accueil");

    /* Ni descendre ni monter ne doit poser le curseur sur une ligne de texte.
     * Les deux sens comptent : la page commence par DEUX lignes de texte, donc
     * remonter depuis le bouton doit faire tout le tour et revenir dessus. */
    for (int i = 0; i < 8; ++i) {
        bas(&c);
        entree(&c);
        CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_FERMER,
              "en descendant, toute validation reste la sortie");
    }
    for (int i = 0; i < 8; ++i) {
        haut(&c);
        entree(&c);
        CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_FERMER,
              "en remontant aussi : le curseur n'atteint aucune ligne de texte");
    }
}

/* ==========================================================================
 * Le chemin d'un joueur qui arrive
 * ========================================================================== */

static void test_inscription(void)
{
    room_comptoir c;
    room_comptoir_init(&c);
    anonyme(&c);

    /* Accueil : SE CONNECTER, CREER UN COMPTE, JOUER SANS COMPTE. */
    bas(&c);
    entree(&c);
    CHECK(c.page == ROOM_CT_INSCRIPTION, "la deuxième ligne mène à l'inscription");
    CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_RIEN,
          "changer de page ne demande rien au serveur");

    /* Champs vides : on refuse AVANT le réseau, et l'on dit lequel manque.
     * Deux « bas » et non trois : la ligne de consigne se saute toute seule,
     * ce qui est précisément ce que la navigation doit faire. */
    bas(&c); bas(&c);
    entree(&c);
    CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_RIEN,
          "on n'envoie pas une inscription vide");
    CHECK(c.note[0] != '\0', "et l'on dit pourquoi plutôt que de ne rien faire");

    /* Le curseur revient sur le pseudo et l'on tape. */
    ns_saisie *ch = NULL;
    c.ligne = 0;
    ch = room_comptoir_champ(&c);
    CHECK(ch == &c.pseudo, "la première ligne du formulaire est le pseudo");
    ns_saisie_ecrire(ch, "Zoe");

    /* Entrée dans un champ passe au suivant : c'est ce que font les doigts. */
    entree(&c);
    ch = room_comptoir_champ(&c);
    CHECK(ch == &c.mdp, "entrée dans un champ passe au champ suivant");
    ns_saisie_ecrire(ch, "un-mot-de-passe-assez-long");

    /* Puis le bouton. La ligne de consigne se saute toute seule. */
    entree(&c);
    entree(&c);
    CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_INSCRIRE,
          "pseudo et mot de passe remplis, l'inscription part");
    CHECK(strcmp(room_comptoir_lu_pseudo(&c), "Zoe") == 0,
          "et l'appelant lit le pseudo tapé");
}

/* ==========================================================================
 * Ce qui doit arriver quand le serveur dit oui
 * ========================================================================== */

static void test_connexion_avance_la_page(void)
{
    room_comptoir c;
    room_comptoir_init(&c);
    anonyme(&c);

    c.page = ROOM_CT_CONNEXION;
    c.ligne = 0;
    ns_saisie_ecrire(&c.pseudo, "Zoe");
    ns_saisie_ecrire(&c.mdp, "un-mot-de-passe-assez-long");
    CHECK(c.mdp.texte[0] != '\0', "le mot de passe est bien dans le champ");

    connecte(&c, "Zoe");
    CHECK(c.page == ROOM_CT_SALONS,
          "le serveur dit oui : on avance, au lieu de laisser le joueur devant son formulaire");
    CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_LISTER,
          "et l'on demande la liste, pour ne pas arriver sur une page vide");

    /*
     * LE POINT QUI COMPTE. Le mot de passe ne survit pas à la connexion. Il est
     * masqué à l'écran, donc le garder ne se verrait pas — c'est exactement ce
     * qui fait qu'on l'oublierait.
     */
    CHECK(c.mdp.texte[0] == '\0', "le mot de passe est effacé dès que la connexion a pris");

    /* Et la déconnexion recule, au lieu d'afficher des boutons qui seront tous
     * refusés. */
    anonyme(&c);
    CHECK(c.page == ROOM_CT_ACCUEIL, "se déconnecter ramène à l'accueil");
}

static void test_echap_oublie_le_mot_de_passe(void)
{
    room_comptoir c;
    room_comptoir_init(&c);
    anonyme(&c);
    c.page = ROOM_CT_CONNEXION;
    ns_saisie_ecrire(&c.mdp, "un-mot-de-passe-assez-long");

    echap(&c);
    CHECK(c.page == ROOM_CT_ACCUEIL, "échap sort du formulaire");
    CHECK(c.mdp.texte[0] == '\0',
          "et le mot de passe ne reste pas derrière soi dans un champ masqué");
}

/* ==========================================================================
 * Les réglages de création
 * ========================================================================== */

static void test_bornes_creation(void)
{
    room_comptoir c;
    room_comptoir_init(&c);
    connecte(&c, "Zoe");
    (void)room_comptoir_prendre(&c);

    c.page = ROOM_CT_CREATION;
    c.ligne = 1;   /* PLACES */

    CHECK(c.places == NS_SALON_MAX_PLACES, "un salon s'ouvre par défaut au maximum de places");

    /* On fait un tour complet dans chaque sens et l'on vérifie qu'aucune valeur
     * ne sort. Une place à 1 donnerait une manche que son unique joueur gagne ;
     * une place à 9 déborderait les tableaux de huit du mode. */
    for (int i = 0; i < 24; ++i) {
        droite(&c);
        CHECK(c.places >= 2 && c.places <= NS_SALON_MAX_PLACES,
              "les places restent entre 2 et %d (vu %d)", NS_SALON_MAX_PLACES, c.places);
    }
    for (int i = 0; i < 24; ++i) {
        gauche(&c);
        CHECK(c.places >= 2 && c.places <= NS_SALON_MAX_PLACES,
              "les places restent bornées dans l'autre sens (vu %d)", c.places);
    }

    c.ligne = 2;   /* CAMPS */
    droite(&c);
    CHECK(c.camps == 1 || c.camps == 2, "les camps valent un ou deux, jamais autre chose");
    droite(&c);
    CHECK(c.camps == 1 || c.camps == 2, "et ça tient au deuxième tour");

    c.ligne = 3;   /* PRIVE */
    const bool avant = c.prive;
    droite(&c);
    CHECK(c.prive != avant, "la bascule privé change bien d'état");
    gauche(&c);
    CHECK(c.prive == avant, "et revient");
}

/* ==========================================================================
 * Le salon
 * ========================================================================== */

static ns_salon un_salon(int occupants, int ma_place)
{
    ns_salon s;
    memset(&s, 0, sizeof s);
    SDL_snprintf(s.code, sizeof s.code, "K7M3QP");
    SDL_snprintf(s.nom, sizeof s.nom, "LA MANCHE DU SOIR");
    s.places = 8;
    s.camps = 2;
    s.place = ma_place;
    s.occupants = occupants;
    for (int i = 0; i < occupants; ++i) {
        s.occupant[i].place = i;
        SDL_snprintf(s.occupant[i].pseudo, sizeof s.occupant[i].pseudo, "J%d", i + 1);
    }
    return s;
}

static void test_salon(void)
{
    room_comptoir c;
    room_comptoir_init(&c);
    connecte(&c, "Zoe");
    (void)room_comptoir_prendre(&c);

    /* Entrer dans un salon OUVRE sa page : sans ça, on créerait un salon et
     * l'on resterait devant le formulaire sans savoir que ça a marché. */
    c.page = ROOM_CT_CREATION;
    ns_salon s = un_salon(1, 0);
    room_comptoir_poser_salon(&c, &s);
    CHECK(c.page == ROOM_CT_SALON, "créer un salon ouvre la page du salon");

    /*
     * Seul, on ne peut pas lancer. Le mode oppose des joueurs : une manche à
     * une place se gagne toute seule, et elle irait quand même au carnet.
     */
    entree(&c);
    CHECK(room_comptoir_prendre(&c) != ROOM_CT_D_LANCER,
          "on ne lance pas une manche à un seul joueur");

    /* À deux, l'hôte peut lancer. */
    s = un_salon(2, 0);
    room_comptoir_poser_salon(&c, &s);
    c.ligne = 0;
    entree(&c);
    CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_LANCER,
          "à deux, l'hôte lance la manche");

    /* Un invité ne lance pas : la première ligne est un texte d'attente. */
    s = un_salon(3, 1);
    room_comptoir_poser_salon(&c, &s);
    c.ligne = 0;
    entree(&c);
    CHECK(room_comptoir_prendre(&c) != ROOM_CT_D_LANCER,
          "un joueur qui n'est pas l'hôte ne lance pas la manche");

    /*
     * ÉCHAP NE QUITTE PAS LE SALON. C'est le piège qu'on veut éviter : sortir
     * quelqu'un de sa manche sur une touche d'annulation serait une perte sèche
     * qu'il n'a pas demandée. On retourne dans la salle en y restant.
     */
    echap(&c);
    CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_FERMER,
          "échap dans un salon referme le comptoir");
    CHECK(c.dans_un_salon, "et l'on est toujours dans le salon");
}

static void test_double_clic(void)
{
    room_comptoir c;
    room_comptoir_init(&c);
    connecte(&c, "Zoe");
    (void)room_comptoir_prendre(&c);

    c.page = ROOM_CT_CODE;
    c.ligne = 0;
    ns_saisie_ecrire(&c.code, "K7M3QP");
    c.ligne = 1;   /* REJOINDRE */

    entree(&c);
    CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_REJOINDRE, "la demande part une fois");

    /*
     * Le serveur répond encore. Un deuxième appui ne doit RIEN enfiler : sur la
     * page de création, deux appuis créeraient deux salons, et la file de
     * `ns_compte` ne peut pas le rattraper à notre place — les deux requêtes
     * sont légitimes vues d'en bas.
     */
    room_comptoir_poser_reseau(&c, NS_COMPTE_CONNECTE, true, "Zoe", "…");
    entree(&c);
    CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_RIEN,
          "tant que le serveur répond, un deuxième appui n'enfile rien");
    CHECK(c.note[0] != '\0', "et l'on dit pourquoi rien ne se passe");
}

static void test_liste_qui_raccourcit(void)
{
    room_comptoir c;
    room_comptoir_init(&c);
    connecte(&c, "Zoe");
    (void)room_comptoir_prendre(&c);
    c.page = ROOM_CT_SALONS;

    ns_salon_resume liste[4];
    memset(liste, 0, sizeof liste);
    for (int i = 0; i < 4; ++i) {
        /* L'alphabet du serveur exclut I, O, 0 et 1 pour qu'un code se dicte
         * sans ambiguïté. Un code de test qui les contiendrait ne serait pas
         * un code que le serveur peut tirer — et le champ le refuserait, à
         * juste titre. */
        SDL_snprintf(liste[i].code, sizeof liste[i].code, "AAA%c%c%c",
                     "KMPQ"[i], "23456789"[i], "RSTV"[i]);
        SDL_snprintf(liste[i].nom, sizeof liste[i].nom, "SALON %d", i);
        liste[i].places = 8;
        liste[i].occupes = 1 + i;
    }
    room_comptoir_poser_salons(&c, liste, 4, 0);

    c.ligne = 3;   /* le quatrième salon */
    entree(&c);
    CHECK(room_comptoir_prendre(&c) == ROOM_CT_D_REJOINDRE,
          "valider un salon de la liste le rejoint");
    CHECK(strcmp(room_comptoir_lu_code(&c), "AAAQ5V") == 0,
          "et c'est le code de CE salon qui part, pas un autre");

    /*
     * La liste raccourcit sous le curseur — c'est le cas normal, un salon
     * ferme toutes les quelques minutes. Le curseur laissé hors des lignes
     * rendrait la validation suivante imprévisible : on validerait ce qui se
     * trouve là après reconstruction, c'est-à-dire n'importe quoi.
     */
    c.ligne = 3;
    room_comptoir_poser_salons(&c, liste, 0, 0);
    entree(&c);
    const room_comptoir_demande d = room_comptoir_prendre(&c);
    CHECK(d != ROOM_CT_D_REJOINDRE,
          "la liste vidée, le curseur ne rejoint plus un salon qui n'existe pas");
}

/* ==========================================================================
 * Ce que la police sait dessiner
 * ========================================================================== */

/*
 * LE DEFAUT QU'ON GARDE FERME. La fonte du jeu porte 95 glyphes ASCII et rien
 * d'autre ; un octet qu'elle ignore DISPARAIT à l'écran, sans bruit. Mesuré
 * avant correction : « pas encore connecté » s'affichait « pas encore
 * connect ». Les phrases viennent du serveur, en français — « le nom doit
 * faire entre 3 et 24 caractères » est écrite dans `auth.ValidateUsername` —
 * et les recopier sans accent de ce côté-ci les ferait diverger.
 */
static void test_plier_accents(void)
{
    char out[128];

    room_comptoir_plier_accents("pas encore connecté", out, sizeof out);
    CHECK(strcmp(out, "pas encore connecte") == 0,
          "l'accent est replié, pas perdu : « %s »", out);

    room_comptoir_plier_accents("le nom doit faire entre 3 et 24 caractères",
                                out, sizeof out);
    CHECK(strcmp(out, "le nom doit faire entre 3 et 24 caracteres") == 0,
          "une phrase entière du serveur passe : « %s »", out);

    room_comptoir_plier_accents("àâäçéèêëîïôöùûüÿ", out, sizeof out);
    CHECK(strcmp(out, "aaaceeeeiioouuuy") == 0,
          "les seize minuscules accentuées du français : « %s »", out);
    room_comptoir_plier_accents("ÀÂÄÇÉÈÊËÎÏÔÖÙÛÜ", out, sizeof out);
    CHECK(strcmp(out, "AAACEEEEIIOOUUU") == 0,
          "et leurs capitales : « %s »", out);

    /* Un caractère qu'on ne sait pas replier devient un point d'interrogation.
     * Rien du tout laisserait croire que la phrase est finie. */
    room_comptoir_plier_accents("un σ perdu", out, sizeof out);
    CHECK(strcmp(out, "un ? perdu") == 0,
          "l'inconnu se voit au lieu de disparaître : « %s »", out);

    /* Toute la sortie doit être dessinable : c'est le seul but de la fonction.
     * On le vérifie au lieu de le supposer. */
    room_comptoir_plier_accents("Zoé a joué à Démineur — 12 €", out, sizeof out);
    bool tout_ascii = true;
    for (size_t i = 0; out[i]; ++i)
        if ((unsigned char)out[i] < 0x20 || (unsigned char)out[i] > 0x7E) tout_ascii = false;
    CHECK(tout_ascii, "rien ne sort qui ne soit dessinable : « %s »", out);

    /* Un tampon trop court tronque proprement et termine toujours. */
    char petit[6];
    room_comptoir_plier_accents("étéétéété", petit, sizeof petit);
    CHECK(strlen(petit) < sizeof petit, "la troncature respecte le tampon");

    /* Une séquence UTF-8 coupée en deux ne doit pas faire boucler la lecture. */
    const char coupe[] = { (char)0xC3, '\0' };
    room_comptoir_plier_accents(coupe, out, sizeof out);
    CHECK(strcmp(out, "?") == 0, "une séquence tronquée rend un point d'interrogation");
}

int main(void)
{
    test_eteint();
    test_inscription();
    test_connexion_avance_la_page();
    test_echap_oublie_le_mot_de_passe();
    test_bornes_creation();
    test_salon();
    test_double_clic();
    test_liste_qui_raccourcit();
    test_plier_accents();
    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
