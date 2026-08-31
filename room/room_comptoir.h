/*
 * room_comptoir.h — le comptoir : s'inscrire, se connecter, trouver une manche.
 *
 * CE QU'IL REMPLACE
 * -----------------
 * Rien. Il n'y avait pas d'écran d'accueil, et il ne pouvait pas y en avoir :
 * `SDL_StartTextInput` n'existait nulle part dans le dépôt, donc rien n'était
 * saisissable. Se connecter voulait dire coller une clé de session à la main
 * dans `settings.cfg`, et jouer une manche en ligne voulait dire convenir d'un
 * numéro de salon et d'un numéro de place hors du jeu, puis les passer en
 * ligne de commande. C'était utilisable par une personne : celle qui a écrit
 * le code.
 *
 * LA SÉPARATION, ET POURQUOI ELLE EST CELLE-LÀ
 * ---------------------------------------------
 * Ce module ne connaît ni SDL, ni le RHI, ni les sockets. Il reçoit des
 * ACTIONS, il rend des DEMANDES, et l'appelant lui repose l'état du réseau à
 * chaque image. C'est exactement le partage de `room_menu`, et pour la même
 * raison : c'est ce qui permet de le dérouler en entier dans un test, sans
 * fenêtre et sans serveur.
 *
 * Concrètement, trois modules pour trois responsabilités :
 *
 *     ns_saisie      ce qu'on tape          (moteur, pur)
 *     ns_compte      ce qui part au serveur (moteur, un fil de travail)
 *     room_comptoir  ce qu'on voit et où l'on va  (ici, pur)
 *
 * LES PAGES
 * ---------
 * Une seule à la fois, et l'on ne peut jamais atterrir sur une page qui ne
 * mène nulle part : les salons ne s'ouvrent pas sans compte, la manche ne se
 * lance pas sans salon. C'est vérifié par les transitions elles-mêmes plutôt
 * que par des boutons grisés, parce qu'un bouton grisé n'explique rien.
 */
#ifndef NS_ROOM_COMPTOIR_H
#define NS_ROOM_COMPTOIR_H

#include "ns_compte.h"
#include "ns_render.h"
#include "ns_saisie.h"
#include "ns_sprite.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Les mêmes six actions que le menu. En ajouter d'autres obligerait l'appelant
 * à savoir quelle page est ouverte — exactement ce que `room_menu.h` explique
 * avoir évité. */
typedef enum room_comptoir_action {
    ROOM_CT_HAUT = 0,
    ROOM_CT_BAS,
    ROOM_CT_GAUCHE,
    ROOM_CT_DROITE,
    ROOM_CT_VALIDER,
    ROOM_CT_ANNULER,
    /* La tabulation. Elle vaut BAS dans une liste, mais dans un formulaire à
     * deux champs c'est le geste qu'ont les doigts, et le faire manquer serait
     * une petite trahison à chaque connexion. */
    ROOM_CT_SUIVANT
} room_comptoir_action;

typedef enum room_comptoir_page {
    ROOM_CT_ACCUEIL = 0,
    ROOM_CT_CONNEXION,
    ROOM_CT_INSCRIPTION,
    ROOM_CT_SALONS,
    ROOM_CT_CREATION,
    ROOM_CT_CODE,
    ROOM_CT_SALON
} room_comptoir_page;

/*
 * Ce que le comptoir DEMANDE. Il ne l'exécute pas : il ne sait pas parler au
 * réseau, et c'est volontaire — une demande est une valeur, donc elle se
 * vérifie dans un test.
 */
typedef enum room_comptoir_demande {
    ROOM_CT_D_RIEN = 0,
    ROOM_CT_D_INSCRIRE,
    ROOM_CT_D_CONNECTER,
    ROOM_CT_D_DECONNECTER,
    ROOM_CT_D_LISTER,
    ROOM_CT_D_CREER,
    ROOM_CT_D_REJOINDRE,
    ROOM_CT_D_QUITTER,
    ROOM_CT_D_SUPPRIMER,
    ROOM_CT_D_LANCER,   /* le propriétaire ouvre la manche */
    ROOM_CT_D_FERMER    /* on referme le comptoir et l'on retourne dans la salle */
} room_comptoir_demande;

#define ROOM_CT_MAX_LIGNES 10

typedef struct room_comptoir {
    room_comptoir_page page;
    int                ligne;        /* la ligne choisie sur la page courante */

    /* Les champs. Ils vivent ici et pas sur la page, parce qu'un pseudo tapé
     * pour l'inscription doit se retrouver dans la connexion : quelqu'un qui
     * se trompe d'onglet ne doit pas retaper son nom. */
    ns_saisie pseudo;
    ns_saisie mdp;
    ns_saisie nom_salon;
    ns_saisie code;

    /* Les réglages de création. Bornés ici, voir `room_comptoir.c`. */
    int  places;
    int  camps;
    bool prive;

    /* L'état du réseau, reposé par l'appelant à chaque image. */
    ns_compte_etat  etat;
    bool            occupe;
    char            pseudo_connecte[NS_COMPTE_PSEUDO_MAX];
    char            message[NS_COMPTE_MESSAGE_MAX];

    ns_salon_resume salons[NS_SALON_MAX_LISTE];
    int             salons_n;
    uint32_t        salons_age_ms;

    ns_salon salon;
    bool     dans_un_salon;

    /* La demande en attente, consommée par `room_comptoir_prendre`. */
    room_comptoir_demande demande;

    /* Ce que le comptoir a lui-même à dire, quand il refuse avant le réseau.
     * Séparé de `message` : celui-là vient du serveur, celui-ci de nous, et les
     * mélanger ferait attribuer au serveur des phrases qu'il n'a pas dites. */
    char note[NS_COMPTE_MESSAGE_MAX];
} room_comptoir;

void room_comptoir_init(room_comptoir *c);

/* L'appelant repose l'état à chaque image, avant d'agir et avant de dessiner. */
void room_comptoir_poser_reseau(room_comptoir *c, ns_compte_etat etat, bool occupe,
                                const char *pseudo, const char *message);
void room_comptoir_poser_salons(room_comptoir *c, const ns_salon_resume *liste,
                                int n, uint32_t age_ms);
void room_comptoir_poser_salon(room_comptoir *c, const ns_salon *salon);

void room_comptoir_agir(room_comptoir *c, room_comptoir_action a);

/*
 * Le champ qui a le curseur, ou NULL. C'est par là que l'appelant route les
 * évènements de texte de SDL — le comptoir n'a pas à les connaître.
 */
ns_saisie *room_comptoir_champ(room_comptoir *c);

/* Rend la demande en attente et la consomme. `ROOM_CT_D_RIEN` s'il n'y en a
 * pas. */
room_comptoir_demande room_comptoir_prendre(room_comptoir *c);

/* Ce que l'appelant doit lire pour honorer une demande. */
const char *room_comptoir_lu_pseudo(const room_comptoir *c);
const char *room_comptoir_lu_mdp(const room_comptoir *c);
const char *room_comptoir_lu_code(const room_comptoir *c);
const char *room_comptoir_lu_nom(const room_comptoir *c);

/*
 * Efface le mot de passe tapé.
 *
 * À appeler dès que la demande a été honorée. Le garder à l'écran n'aurait
 * aucune utilité — il est masqué — et le garder en mémoire dans une structure
 * qui vit toute la partie en aurait encore moins.
 */
void room_comptoir_oublier_mdp(room_comptoir *c);

void room_comptoir_draw(ns_sprite *s, float w, float h, const room_comptoir *c,
                        uint32_t temps_ms);

/*
 * Replie les accents sur l'ASCII, pour le DESSIN seulement.
 *
 * Publique parce qu'elle se teste, et elle a besoin de se tester : la fonte du
 * jeu ne porte que 95 glyphes ASCII, les phrases affichées viennent du serveur
 * en français, et un octet qu'elle ignore disparaît sans bruit. « pas encore
 * connecté » sortait « pas encore connect » à l'écran, et un mot amputé se lit
 * comme une phrase coupée. La chaîne d'origine n'est jamais modifiée : c'est
 * elle qu'on renverrait au serveur.
 */
void room_comptoir_plier_accents(const char *src, char *out, size_t taille);

#endif /* NS_ROOM_COMPTOIR_H */
