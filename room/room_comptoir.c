/* room_comptoir.c — voir room_comptoir.h pour le partage des responsabilités. */
#include "room_comptoir.h"

#include <SDL3/SDL_stdinc.h>

/* ==========================================================================
 * Les lignes d'une page
 * ========================================================================== */

/*
 * Les pages ne sont pas décrites deux fois.
 *
 * `agir` et `draw` appellent la MÊME fonction pour savoir ce qu'il y a sur la
 * page courante. C'est la seule façon d'être certain que la flèche du bas et
 * le texte dessiné parlent de la même ligne : deux descriptions d'une même
 * chose finissent toujours par se contredire, et ici la contradiction
 * s'appellerait « le bouton ne fait pas ce qui est écrit dessus ».
 */

typedef enum genre_ligne {
    L_TEXTE = 0,   /* affiché, jamais choisi */
    L_CHAMP,
    L_NOMBRE,
    L_BASCULE,
    L_BOUTON,
    L_SALON        /* une ligne de la liste, qu'on rejoint en validant */
} genre_ligne;

enum { CH_AUCUN = -1, CH_PSEUDO, CH_MDP, CH_NOM, CH_CODE };
enum { RG_AUCUN = -1, RG_PLACES, RG_CAMPS, RG_PRIVE };

typedef struct ligne {
    genre_ligne genre;
    const char *titre;
    int         champ;      /* CH_* */
    int         reglage;    /* RG_* */
    room_comptoir_demande demande;
    room_comptoir_page    vers;
    bool        navigue;    /* le bouton change de page au lieu de demander */
    int         salon;      /* index dans `c->salons` */
} ligne;

static ligne bouton(const char *titre, room_comptoir_demande d)
{
    ligne l = { L_BOUTON, titre, CH_AUCUN, RG_AUCUN, d, ROOM_CT_ACCUEIL, false, -1 };
    return l;
}

static ligne aller(const char *titre, room_comptoir_page vers)
{
    ligne l = { L_BOUTON, titre, CH_AUCUN, RG_AUCUN, ROOM_CT_D_RIEN, vers, true, -1 };
    return l;
}

static ligne champ(const char *titre, int ch)
{
    ligne l = { L_CHAMP, titre, ch, RG_AUCUN, ROOM_CT_D_RIEN, ROOM_CT_ACCUEIL, false, -1 };
    return l;
}

static ligne nombre(const char *titre, int rg)
{
    ligne l = { L_NOMBRE, titre, CH_AUCUN, rg, ROOM_CT_D_RIEN, ROOM_CT_ACCUEIL, false, -1 };
    return l;
}

static ligne bascule(const char *titre, int rg)
{
    ligne l = { L_BASCULE, titre, CH_AUCUN, rg, ROOM_CT_D_RIEN, ROOM_CT_ACCUEIL, false, -1 };
    return l;
}

static ligne texte(const char *titre)
{
    ligne l = { L_TEXTE, titre, CH_AUCUN, RG_AUCUN, ROOM_CT_D_RIEN, ROOM_CT_ACCUEIL, false, -1 };
    return l;
}

static int construire(const room_comptoir *c, ligne *out, int max)
{
    int n = 0;
#define POSER(x) do { if (n < max) out[n++] = (x); } while (0)

    switch (c->page) {
    case ROOM_CT_ACCUEIL:
        if (c->etat == NS_COMPTE_ETEINT) {
            /*
             * Aucun serveur configuré. On ne montre pas des boutons qui ne
             * peuvent rien faire : on dit ce qui manque et où le mettre. Un
             * bouton grisé n'apprend rien à personne.
             */
            POSER(texte("AUCUN SERVEUR CONFIGURE"));
            POSER(texte("--server=http://... OU network.serverUrl"));
            POSER(bouton("RETOURNER DANS LA SALLE", ROOM_CT_D_FERMER));
        } else if (c->etat == NS_COMPTE_CONNECTE) {
            POSER(aller("LES SALONS", ROOM_CT_SALONS));
            POSER(bouton("SE DECONNECTER", ROOM_CT_D_DECONNECTER));
            POSER(bouton("RETOURNER DANS LA SALLE", ROOM_CT_D_FERMER));
        } else {
            POSER(aller("SE CONNECTER", ROOM_CT_CONNEXION));
            POSER(aller("CREER UN COMPTE", ROOM_CT_INSCRIPTION));
            POSER(bouton("JOUER SANS COMPTE", ROOM_CT_D_FERMER));
        }
        break;

    case ROOM_CT_CONNEXION:
        POSER(champ("PSEUDO", CH_PSEUDO));
        POSER(champ("MOT DE PASSE", CH_MDP));
        POSER(bouton("SE CONNECTER", ROOM_CT_D_CONNECTER));
        POSER(aller("RETOUR", ROOM_CT_ACCUEIL));
        break;

    case ROOM_CT_INSCRIPTION:
        POSER(champ("PSEUDO", CH_PSEUDO));
        POSER(champ("MOT DE PASSE", CH_MDP));
        /*
         * La règle du serveur, dite AVANT le refus. Elle vient de
         * `auth.ValidatePassword` et elle est stricte — douze caractères — donc
         * quelqu'un qui l'ignore se fait refuser sans comprendre. La rappeler
         * ici est le seul endroit où la répétition se justifie : c'est une
         * consigne à l'utilisateur, pas une deuxième implémentation de la règle.
         */
        POSER(texte("12 CARACTERES MINIMUM, 5 DIFFERENTS"));
        POSER(bouton("CREER LE COMPTE", ROOM_CT_D_INSCRIRE));
        POSER(aller("RETOUR", ROOM_CT_ACCUEIL));
        break;

    case ROOM_CT_SALONS:
        for (int i = 0; i < c->salons_n && n < max - 4; ++i) {
            ligne l = { L_SALON, c->salons[i].nom, CH_AUCUN, RG_AUCUN,
                        ROOM_CT_D_REJOINDRE, ROOM_CT_ACCUEIL, false, i };
            POSER(l);
        }
        if (c->salons_n == 0) POSER(texte("AUCUN SALON PUBLIC OUVERT"));
        POSER(aller("CREER UN SALON", ROOM_CT_CREATION));
        POSER(aller("REJOINDRE PAR CODE", ROOM_CT_CODE));
        POSER(bouton("RAFRAICHIR", ROOM_CT_D_LISTER));
        POSER(aller("RETOUR", ROOM_CT_ACCUEIL));
        break;

    case ROOM_CT_CREATION:
        POSER(champ("NOM DU SALON", CH_NOM));
        POSER(nombre("PLACES", RG_PLACES));
        POSER(nombre("CAMPS", RG_CAMPS));
        POSER(bascule("PRIVE", RG_PRIVE));
        POSER(bouton("CREER", ROOM_CT_D_CREER));
        POSER(aller("RETOUR", ROOM_CT_SALONS));
        break;

    case ROOM_CT_CODE:
        POSER(champ("CODE", CH_CODE));
        POSER(bouton("REJOINDRE", ROOM_CT_D_REJOINDRE));
        POSER(aller("RETOUR", ROOM_CT_SALONS));
        break;

    case ROOM_CT_SALON:
        /*
         * On ne propose de LANCER que si la manche a de quoi être une manche.
         * Le mode oppose des joueurs ; l'ouvrir seul donnerait une manche que
         * son unique joueur gagne, ce qui n'apprend rien et abîme le carnet.
         */
        if (c->dans_un_salon && c->salon.place == 0 && c->salon.occupants >= 2)
            POSER(bouton("LANCER LA MANCHE", ROOM_CT_D_LANCER));
        else if (c->dans_un_salon && c->salon.place == 0)
            POSER(texte("EN ATTENTE D'UN DEUXIEME JOUEUR"));
        else
            POSER(texte("EN ATTENTE DE L'HOTE"));

        POSER(bouton("QUITTER LE SALON", ROOM_CT_D_QUITTER));
        if (c->dans_un_salon && c->salon.place == 0)
            POSER(bouton("SUPPRIMER LE SALON", ROOM_CT_D_SUPPRIMER));
        POSER(bouton("RETOURNER DANS LA SALLE", ROOM_CT_D_FERMER));
        break;
    }
#undef POSER
    return n;
}

/* La première ligne qu'on peut choisir, dans le sens donné. Une page qui
 * commence par du texte ne doit pas laisser le curseur dessus. */
static int choisissable(const ligne *l, int n, int depart, int sens)
{
    for (int k = 0; k < n; ++k) {
        const int i = ((depart + k * sens) % n + n) % n;
        if (l[i].genre != L_TEXTE) return i;
    }
    return 0;
}

/*
 * Remet le curseur sur une ligne qui répond.
 *
 * À appeler chaque fois que les lignes ont pu changer sous lui — changement de
 * page, liste qui raccourcit, connexion qui transforme l'accueil. Sans ça, le
 * curseur reste posé sur une ligne de texte et valider ne fait RIEN : c'est le
 * pire des comportements, parce que le joueur appuie, l'écran ne bouge pas, et
 * rien ne lui dit s'il a mal visé ou si le jeu est bloqué.
 *
 * La page éteinte est le cas où ça se produit dès l'ouverture : elle commence
 * par deux lignes de texte qui expliquent ce qui manque.
 */
static void recaler(room_comptoir *c)
{
    ligne l[ROOM_CT_MAX_LIGNES];
    const int n = construire(c, l, ROOM_CT_MAX_LIGNES);
    if (n <= 0) { c->ligne = 0; return; }
    if (c->ligne < 0 || c->ligne >= n) c->ligne = 0;
    if (l[c->ligne].genre == L_TEXTE) c->ligne = choisissable(l, n, c->ligne, +1);
}

/* ==========================================================================
 * Vie du comptoir
 * ========================================================================== */

/*
 * Les bornes des champs suivent CELLES DU SERVEUR, et pas l'inverse.
 *
 *   - pseudo : 24 caractères (auth.ValidateUsername) ;
 *   - mot de passe : 256 (auth.ValidatePassword). Le borner plus court ici
 *     rendrait impossible la connexion d'un compte créé sur le site avec un
 *     mot de passe long — un échec sans message, le pire des cas ;
 *   - code : la longueur fixée par le serveur, voir `NS_SAISIE_CODE_LONG` ;
 *   - nom de salon : la borne du transport, `NS_SALON_NOM_MAX`.
 */
void room_comptoir_init(room_comptoir *c)
{
    SDL_zerop(c);
    ns_saisie_init(&c->pseudo,    NS_SAISIE_PSEUDO, 24, false);
    ns_saisie_init(&c->mdp,       NS_SAISIE_TEXTE, 256, true);
    ns_saisie_init(&c->nom_salon, NS_SAISIE_TEXTE, NS_SALON_NOM_MAX - 1, false);
    ns_saisie_init(&c->code,      NS_SAISIE_CODE, NS_SAISIE_CODE_LONG, false);

    /* Huit places et deux camps : le maximum du mode, parce que c'est ce qu'on
     * veut quand on ouvre un salon public. Deux camps parce que le mode ne
     * devient intéressant qu'avec des alliances — voir room_couperet.h. */
    c->places = NS_SALON_MAX_PLACES;
    c->camps  = 2;
    c->prive  = false;
    c->page   = ROOM_CT_ACCUEIL;
    c->etat   = NS_COMPTE_ETEINT;
    c->salons_age_ms = UINT32_MAX;
    recaler(c);
}

void room_comptoir_poser_reseau(room_comptoir *c, ns_compte_etat etat, bool occupe,
                                const char *pseudo, const char *message)
{
    const ns_compte_etat avant = c->etat;

    /*
     * L'ACCUEIL CHANGE DE SENS AVEC L'ÉTAT DU COMPTE, ligne par ligne : la
     * troisième vaut « RETOURNER DANS LA SALLE » hors ligne et « JOUER SANS
     * COMPTE » une fois le serveur joignable. Garder l'index laisserait donc la
     * surbrillance sur une action qui n'est plus celle qu'on regardait — et
     * quelqu'un qui appuie à cet instant déclenche ce qu'il n'a pas choisi.
     * On remonte en tête plutôt que de garder un numéro qui ne veut plus dire
     * la même chose.
     */
    if (avant != etat) c->ligne = 0;

    c->etat   = etat;
    c->occupe = occupe;
    SDL_snprintf(c->pseudo_connecte, sizeof c->pseudo_connecte, "%s", pseudo ? pseudo : "");
    SDL_snprintf(c->message, sizeof c->message, "%s", message ? message : "");

    /*
     * La connexion FAIT AVANCER la page. C'est le seul mouvement automatique du
     * comptoir, et il est là parce que l'inverse serait une petite cruauté :
     * on tape son mot de passe, le serveur répond oui, et il ne se passe rien
     * de visible à l'écran sinon une ligne de message.
     */
    if (avant != NS_COMPTE_CONNECTE && etat == NS_COMPTE_CONNECTE) {
        room_comptoir_oublier_mdp(c);
        if (c->page == ROOM_CT_CONNEXION || c->page == ROOM_CT_INSCRIPTION) {
            c->page  = ROOM_CT_SALONS;
            c->ligne = 0;
            /* On ouvre la liste en la demandant : arriver sur une page vide
             * ferait croire qu'il n'y a personne. */
            c->demande = ROOM_CT_D_LISTER;
        }
    }
    /*
     * Et la déconnexion RECULE. Rester sur la page des salons sans compte
     * afficherait des boutons dont chacun se ferait refuser.
     */
    if (avant == NS_COMPTE_CONNECTE && etat != NS_COMPTE_CONNECTE) {
        c->page  = ROOM_CT_ACCUEIL;
        c->ligne = 0;
        c->dans_un_salon = false;
    }
    recaler(c);
}

void room_comptoir_poser_salons(room_comptoir *c, const ns_salon_resume *liste,
                                int n, uint32_t age_ms)
{
    if (n < 0) n = 0;
    if (n > NS_SALON_MAX_LISTE) n = NS_SALON_MAX_LISTE;
    for (int i = 0; i < n; ++i) c->salons[i] = liste[i];
    c->salons_n = n;
    c->salons_age_ms = age_ms;

    /* La liste a pu raccourcir sous le curseur — c'est le cas normal, un salon
     * ferme toutes les quelques minutes. Le laisser hors des lignes rendrait la
     * validation imprévisible : on validerait ce qui se trouve là après
     * reconstruction, c'est-à-dire n'importe quoi. */
    recaler(c);
}

void room_comptoir_poser_salon(room_comptoir *c, const ns_salon *salon)
{
    const bool avant = c->dans_un_salon;
    if (salon) {
        c->salon = *salon;
        c->dans_un_salon = true;
        /* Entrer dans un salon ouvre sa page. Sans ça, on créerait un salon
         * depuis la page de création et l'on resterait devant le formulaire,
         * sans savoir que ça a marché. */
        if (!avant) {
            c->page  = ROOM_CT_SALON;
            c->ligne = 0;
        }
    } else {
        c->dans_un_salon = false;
        SDL_zero(c->salon);
        if (avant && c->page == ROOM_CT_SALON) {
            c->page  = ROOM_CT_SALONS;
            c->ligne = 0;
        }
    }
    recaler(c);
}

ns_saisie *room_comptoir_champ(room_comptoir *c)
{
    ligne l[ROOM_CT_MAX_LIGNES];
    const int n = construire(c, l, ROOM_CT_MAX_LIGNES);
    if (c->ligne < 0 || c->ligne >= n) return NULL;
    switch (l[c->ligne].champ) {
    case CH_PSEUDO: return &c->pseudo;
    case CH_MDP:    return &c->mdp;
    case CH_NOM:    return &c->nom_salon;
    case CH_CODE:   return &c->code;
    default:        return NULL;
    }
}

room_comptoir_demande room_comptoir_prendre(room_comptoir *c)
{
    const room_comptoir_demande d = c->demande;
    c->demande = ROOM_CT_D_RIEN;
    return d;
}

const char *room_comptoir_lu_pseudo(const room_comptoir *c) { return c->pseudo.texte; }
const char *room_comptoir_lu_mdp(const room_comptoir *c)    { return c->mdp.texte; }
const char *room_comptoir_lu_code(const room_comptoir *c)   { return c->code.texte; }
const char *room_comptoir_lu_nom(const room_comptoir *c)    { return c->nom_salon.texte; }

void room_comptoir_oublier_mdp(room_comptoir *c)
{
    ns_saisie_vider(&c->mdp);
}

/* ==========================================================================
 * Les actions
 * ========================================================================== */

static void note(room_comptoir *c, const char *quoi)
{
    SDL_snprintf(c->note, sizeof c->note, "%s", quoi);
}

/* Valide ce qu'on peut valider SANS réseau, et dit non tout de suite.
 *
 * On ne double pas les règles du serveur ici — elles resteraient en arrière le
 * jour où elles changent. On ne refuse que le vide, qui n'est pas une règle
 * mais l'absence de saisie, et qui ferait partir un aller-retour pour rien. */
static bool pret(room_comptoir *c, room_comptoir_demande d)
{
    switch (d) {
    case ROOM_CT_D_CONNECTER:
    case ROOM_CT_D_INSCRIRE:
        if (!c->pseudo.texte[0]) { note(c, "il manque le pseudo"); return false; }
        if (!c->mdp.texte[0])    { note(c, "il manque le mot de passe"); return false; }
        return true;
    case ROOM_CT_D_REJOINDRE:
        if (!c->code.texte[0])   { note(c, "il manque le code"); return false; }
        return true;
    default:
        return true;
    }
}

void room_comptoir_agir(room_comptoir *c, room_comptoir_action a)
{
    recaler(c);

    ligne l[ROOM_CT_MAX_LIGNES];
    const int n = construire(c, l, ROOM_CT_MAX_LIGNES);
    if (n <= 0) return;

    switch (a) {
    case ROOM_CT_HAUT:
        c->ligne = choisissable(l, n, c->ligne - 1, -1);
        c->note[0] = '\0';
        return;

    case ROOM_CT_BAS:
    case ROOM_CT_SUIVANT:
        c->ligne = choisissable(l, n, c->ligne + 1, +1);
        c->note[0] = '\0';
        return;

    case ROOM_CT_GAUCHE:
    case ROOM_CT_DROITE: {
        const int pas = (a == ROOM_CT_DROITE) ? +1 : -1;
        const ligne *cur = &l[c->ligne];
        if (cur->genre == L_NOMBRE) {
            if (cur->reglage == RG_PLACES) {
                c->places += pas;
                if (c->places < 2) c->places = NS_SALON_MAX_PLACES;
                if (c->places > NS_SALON_MAX_PLACES) c->places = 2;
            } else if (cur->reglage == RG_CAMPS) {
                /* Un ou deux, et rien d'autre : c'est ce que le mode sait
                 * arbitrer, voir le classement par camp de room_couperet.h. */
                c->camps = (c->camps == 1) ? 2 : 1;
            }
        } else if (cur->genre == L_BASCULE && cur->reglage == RG_PRIVE) {
            c->prive = !c->prive;
        }
        return;
    }

    case ROOM_CT_ANNULER:
        c->note[0] = '\0';
        switch (c->page) {
        case ROOM_CT_ACCUEIL:
            c->demande = ROOM_CT_D_FERMER;
            break;
        case ROOM_CT_CONNEXION:
        case ROOM_CT_INSCRIPTION:
            /* On sort d'un formulaire en OUBLIANT le mot de passe. Le laisser
             * derrière soi dans un champ masqué serait invisible et faux. */
            room_comptoir_oublier_mdp(c);
            c->page = ROOM_CT_ACCUEIL;
            break;
        case ROOM_CT_CREATION:
        case ROOM_CT_CODE:
            c->page = ROOM_CT_SALONS;
            break;
        case ROOM_CT_SALONS:
            c->page = ROOM_CT_ACCUEIL;
            break;
        case ROOM_CT_SALON:
            /* Échap ne quitte PAS le salon : on retourne dans la salle en y
             * restant. Faire sortir quelqu'un de sa manche sur une touche
             * d'annulation serait une perte sèche qu'il n'a pas demandée. */
            c->demande = ROOM_CT_D_FERMER;
            break;
        }
        c->ligne = 0;
        recaler(c);
        return;

    case ROOM_CT_VALIDER: {
        const ligne *cur = &l[c->ligne];
        c->note[0] = '\0';

        /* Une requête est déjà en vol. Enfiler la même deux fois créerait deux
         * salons pour un double clic — c'est précisément ce que la file de
         * `ns_compte` ne peut pas rattraper à sa place. */
        if (c->occupe && cur->genre != L_CHAMP) {
            note(c, "un instant, le serveur répond");
            return;
        }

        switch (cur->genre) {
        case L_CHAMP:
            /* Valider dans un champ passe au suivant. C'est ce que fait la
             * touche Entrée dans tout formulaire, et le seul endroit où elle
             * ne peut pas signifier « envoyer » sans surprendre. */
            c->ligne = choisissable(l, n, c->ligne + 1, +1);
            return;

        case L_SALON:
            if (cur->salon >= 0 && cur->salon < c->salons_n) {
                ns_saisie_poser(&c->code, c->salons[cur->salon].code);
                c->demande = ROOM_CT_D_REJOINDRE;
            }
            return;

        case L_BOUTON:
            if (cur->navigue) {
                c->page  = cur->vers;
                c->ligne = 0;
                recaler(c);
                /* Ouvrir la liste la demande. Une liste vieille de plusieurs
                 * minutes montrerait des salons fermés. */
                if (cur->vers == ROOM_CT_SALONS) c->demande = ROOM_CT_D_LISTER;
                return;
            }
            if (pret(c, cur->demande)) c->demande = cur->demande;
            return;

        default:
            return;
        }
    }
    }
}

/* ==========================================================================
 * Le dessin
 * ========================================================================== */

/*
 * Les couleurs et l'échelle sont celles du menu — c'est le même écran de
 * dessus, et deux gammes différentes dans le même jeu se remarquent. L'échelle
 * 2.0 donne des glyphes de 14 px de haut avec la fonte 5 x 7, au-dessus du
 * seuil de lisibilité que le dépôt s'est fixé à 11 px.
 */
static const float AMBRE[4] = { 1.00f, 0.76f, 0.28f, 1.00f };
static const float PALE[4]  = { 0.85f, 0.88f, 0.95f, 1.00f };
static const float SOURD[4] = { 0.48f, 0.53f, 0.62f, 1.00f };
static const float ROUGE[4] = { 1.00f, 0.42f, 0.38f, 1.00f };
static const float VERT[4]  = { 0.55f, 0.95f, 0.65f, 1.00f };

/* Voir `room_comptoir.h` pour la raison. Ici, seulement comment. */
void room_comptoir_plier_accents(const char *src, char *out, size_t taille)
{
    static const struct { unsigned char b; const char *vers; } TABLE[] = {
        /* Les séquences UTF-8 à deux octets qui commencent par 0xC3 : c'est
         * tout le latin-1 accenté, donc tout ce que le français produit. */
        { 0xA0, "a" }, { 0xA2, "a" }, { 0xA4, "a" }, { 0xA7, "c" },
        { 0xA8, "e" }, { 0xA9, "e" }, { 0xAA, "e" }, { 0xAB, "e" },
        { 0xAE, "i" }, { 0xAF, "i" }, { 0xB4, "o" }, { 0xB6, "o" },
        { 0xB9, "u" }, { 0xBB, "u" }, { 0xBC, "u" }, { 0xBF, "y" },
        { 0x80, "A" }, { 0x82, "A" }, { 0x84, "A" }, { 0x87, "C" },
        { 0x88, "E" }, { 0x89, "E" }, { 0x8A, "E" }, { 0x8B, "E" },
        { 0x8E, "I" }, { 0x8F, "I" }, { 0x94, "O" }, { 0x96, "O" },
        { 0x99, "U" }, { 0x9B, "U" }, { 0x9C, "U" },
    };

    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 1 < taille; ) {
        const unsigned char c = (unsigned char)src[i];
        if (c < 0x80) { out[j++] = src[i++]; continue; }

        if (c == 0xC3 && src[i + 1]) {
            const unsigned char d = (unsigned char)src[i + 1];
            const char *vers = NULL;
            for (size_t k = 0; k < sizeof TABLE / sizeof TABLE[0]; ++k)
                if (TABLE[k].b == d) { vers = TABLE[k].vers; break; }
            if (vers) { out[j++] = vers[0]; i += 2; continue; }
        }

        /*
         * Tout le reste — une lettre non latine, un emoji — devient un point
         * d'interrogation plutôt que rien. Un caractère absent laisse croire
         * que la phrase est finie ; un point d'interrogation dit qu'il y avait
         * quelque chose là, et que le jeu ne sait pas le montrer.
         */
        out[j++] = '?';

        /*
         * On avance du nombre d'octets RÉELLEMENT PRÉSENTS, jamais de celui
         * qu'annonce le premier. Sauter les quatre octets d'une séquence
         * tronquée passe PAR-DESSUS le terminateur et continue de lire dans ce
         * qui suit le tampon — c'est exactement ce que faisait la première
         * version, et c'est le test de la séquence coupée qui l'a montré.
         */
        size_t pas = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        for (size_t k = 1; k < pas; ++k) {
            if (!src[i + k]) { pas = k; break; }
        }
        i += pas;
    }
    out[j] = '\0';
}

static const char *titre_page(const room_comptoir *c)
{
    switch (c->page) {
    case ROOM_CT_ACCUEIL:     return "LE COMPTOIR";
    case ROOM_CT_CONNEXION:   return "SE CONNECTER";
    case ROOM_CT_INSCRIPTION: return "CREER UN COMPTE";
    case ROOM_CT_SALONS:      return "LES SALONS";
    case ROOM_CT_CREATION:    return "CREER UN SALON";
    case ROOM_CT_CODE:        return "REJOINDRE PAR CODE";
    case ROOM_CT_SALON:       return "SALON";
    }
    return "LE COMPTOIR";
}

void room_comptoir_draw(ns_sprite *s, float w, float h, const room_comptoir *c,
                        uint32_t temps_ms)
{
    ligne l[ROOM_CT_MAX_LIGNES];
    const int n = construire(c, l, ROOM_CT_MAX_LIGNES);

    const float veil[4] = { 0.02f, 0.03f, 0.05f, 0.88f };
    ns_sprite_rect(s, 0, 0, w, h, veil);

    const float pw = 720.0f;
    const float rang = 34.0f;
    const float ph = 190.0f + rang * (float)(n > 0 ? n : 1);
    const float px = (w - pw) * 0.5f;
    const float py = (h - ph) * 0.5f;

    const float bord[4] = { 0.35f, 0.28f, 0.14f, 1.00f };
    const float fond[4] = { 0.05f, 0.06f, 0.09f, 0.97f };
    ns_sprite_rect(s, px - 3, py - 3, pw + 6, ph + 6, bord);
    ns_sprite_rect(s, px, py, pw, ph, fond);

    /* Le titre. */
    const char *t = titre_page(c);
    ns_sprite_text(s, px + (pw - ns_sprite_text_width(t, 3.0f)) * 0.5f,
                   py + 22.0f, 3.0f, AMBRE, t);

    /* Qui l'on est. C'est la première chose qu'on cherche sur un écran de
     * compte, et l'absence de réponse est elle-même une réponse. */
    char qui[96];
    if (c->etat == NS_COMPTE_CONNECTE) {
        /* Le pseudo vient du serveur, qui accepte les lettres accentuées même
         * si le champ de saisie du jeu les refuse : un compte créé sur le site
         * peut en porter. */
        char nom[NS_COMPTE_PSEUDO_MAX * 2];
        room_comptoir_plier_accents(c->pseudo_connecte, nom, sizeof nom);
        SDL_snprintf(qui, sizeof qui, "CONNECTE : %s", nom);
    }
    else if (c->etat == NS_COMPTE_ETEINT)
        SDL_snprintf(qui, sizeof qui, "HORS LIGNE");
    else
        SDL_snprintf(qui, sizeof qui, "PAS CONNECTE");
    ns_sprite_text(s, px + (pw - ns_sprite_text_width(qui, 1.6f)) * 0.5f,
                   py + 62.0f, 1.6f,
                   c->etat == NS_COMPTE_CONNECTE ? VERT : SOURD, qui);

    /* Le code du salon, en grand, quand on y est : c'est ce qu'on lit à voix
     * haute à quelqu'un, et le chercher dans une ligne de tableau serait une
     * gêne à chaque fois. */
    float y = py + 96.0f;
    if (c->page == ROOM_CT_SALON && c->dans_un_salon) {
        char code[64];
        SDL_snprintf(code, sizeof code, "CODE  %s", c->salon.code);
        ns_sprite_text(s, px + (pw - ns_sprite_text_width(code, 3.5f)) * 0.5f,
                       y, 3.5f, AMBRE, code);
        y += 46.0f;

        for (int i = 0; i < c->salon.occupants; ++i) {
            const ns_salon_occupant *o = &c->salon.occupant[i];
            char rangee[128];
            SDL_snprintf(rangee, sizeof rangee, "%d  %-16s %s",
                         o->place + 1, o->pseudo,
                         o->place == c->salon.place ? "(VOUS)" : "");
            ns_sprite_text(s, px + 40.0f, y, 1.8f,
                           o->place == c->salon.place ? AMBRE : PALE, rangee);
            y += 24.0f;
        }
        y += 10.0f;
    }

    /* Les lignes. */
    for (int i = 0; i < n; ++i) {
        const bool choisi = (i == c->ligne) && l[i].genre != L_TEXTE;
        if (choisi) {
            const float hl[4] = { 0.16f, 0.13f, 0.05f, 1.0f };
            ns_sprite_rect(s, px + 24.0f, y - 6.0f, pw - 48.0f, rang - 6.0f, hl);
            ns_sprite_text(s, px + 10.0f, y, 2.0f, AMBRE, ">");
        }
        const float *col = l[i].genre == L_TEXTE ? SOURD : (choisi ? AMBRE : PALE);
        ns_sprite_text(s, px + 40.0f, y, 2.0f, col, l[i].titre);

        const float droite = px + pw - 40.0f;
        char val[NS_SAISIE_OCTETS + 8];
        val[0] = '\0';

        switch (l[i].genre) {
        case L_CHAMP: {
            const ns_saisie *ch =
                l[i].champ == CH_PSEUDO ? &c->pseudo :
                l[i].champ == CH_MDP    ? &c->mdp :
                l[i].champ == CH_NOM    ? &c->nom_salon : &c->code;
            char vu[NS_SAISIE_OCTETS + 1];
            ns_saisie_affichage(ch, vu, sizeof vu);

            /*
             * Le curseur clignote à 1,5 Hz — deux tiers de seconde allumé, un
             * tiers éteint. Sans curseur, un champ vide et un champ inactif se
             * ressemblent exactement, et l'on tape sans savoir où ça va.
             */
            const bool actif = choisi && ((temps_ms % 1000u) < 660u);
            SDL_snprintf(val, sizeof val, "%s%s", vu, actif ? "_" : " ");
            break;
        }
        case L_NOMBRE:
            if (l[i].reglage == RG_PLACES) SDL_snprintf(val, sizeof val, "< %d >", c->places);
            else                           SDL_snprintf(val, sizeof val, "< %d >", c->camps);
            break;
        case L_BASCULE:
            SDL_snprintf(val, sizeof val, "< %s >", c->prive ? "OUI" : "NON");
            break;
        case L_SALON: {
            const ns_salon_resume *r = &c->salons[l[i].salon];
            SDL_snprintf(val, sizeof val, "%d/%d  %s", r->occupes, r->places, r->code);
            break;
        }
        default:
            break;
        }

        if (val[0]) {
            const float vw = ns_sprite_text_width(val, 2.0f);
            ns_sprite_text(s, droite - vw, y, 2.0f, choisi ? AMBRE : PALE, val);
        }
        y += rang;
    }

    /* En bas : ce que NOUS disons, puis ce que le SERVEUR dit. Les deux ne se
     * mélangent pas — attribuer au serveur une phrase qu'il n'a pas dite
     * enverrait chercher un défaut du mauvais côté. */
    const float bas = py + ph - 52.0f;
    char phrase[NS_COMPTE_MESSAGE_MAX * 2];
    if (c->note[0]) {
        room_comptoir_plier_accents(c->note, phrase, sizeof phrase);
        ns_sprite_text(s, px + (pw - ns_sprite_text_width(phrase, 1.6f)) * 0.5f,
                       bas, 1.6f, ROUGE, phrase);
    } else if (c->message[0]) {
        room_comptoir_plier_accents(c->message, phrase, sizeof phrase);
        ns_sprite_text(s, px + (pw - ns_sprite_text_width(phrase, 1.6f)) * 0.5f,
                       bas, 1.6f, c->occupe ? SOURD : PALE, phrase);
    }

    const char *aide = "FLECHES DEPLACER   ENTREE VALIDER   ECHAP RETOUR";
    ns_sprite_text(s, px + (pw - ns_sprite_text_width(aide, 1.4f)) * 0.5f,
                   py + ph - 26.0f, 1.4f, SOURD, aide);
}
