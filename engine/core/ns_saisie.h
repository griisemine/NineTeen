/*
 * ns_saisie.h — un champ de saisie d'une ligne, sans clavier ni écran.
 *
 * CE QUI MANQUAIT, ET CE QUE ÇA EMPÊCHAIT
 * ---------------------------------------
 * `SDL_StartTextInput` n'apparaissait NULLE PART dans le dépôt. Pas un champ,
 * pas une lettre tapée : entrer dans le jeu sous un compte demandait de coller
 * à la main une clé de session dans `settings.cfg`. `ns_compte.h` fait le même
 * constat depuis l'autre moitié — il sait s'inscrire et rejoindre un salon, il
 * n'avait aucun moyen de DEMANDER un pseudo, un mot de passe ou un code.
 *
 * CE QUE CE FICHIER EST, ET CE QU'IL N'EST PAS
 * --------------------------------------------
 * Le champ, et rien d'autre : un tampon, un curseur, une borne, un filtre. Il
 * ne connaît ni SDL, ni le RHI, ni la police. Il reçoit du texte DÉJÀ DÉCODÉ
 * (de l'UTF-8) et des commandes d'édition ; `ns_saisie_sdl.h` traduit les
 * événements SDL, `ns_saisie_draw.h` le dessine. C'est la séparation de
 * `room_menu`, et pour la même raison : `tests/test_saisie.c` l'exerce
 * entièrement sans ouvrir de fenêtre, et n'inclut même pas SDL.
 *
 * Il n'y a ni sélection, ni plusieurs lignes, ni annulation. Ce sont des choses
 * qu'on écrit quand on sait de quoi on a besoin ; ce dont on a besoin
 * aujourd'hui, c'est d'un pseudo, d'un mot de passe et d'un code de salon.
 *
 * LA CONTRAINTE QUI DÉCIDE DE TOUT : LA POLICE
 * --------------------------------------------
 * La fonte du jeu est un bitmap 5 x 7 qui ne couvre que l'ASCII imprimable
 * (`ns_font5x7.h`). Le serveur, lui, accepte les accents dans un pseudo :
 * `auth.ValidateUsername` (server/internal/auth/password.go) laisse passer
 * toute lettre au sens d'`unicode.IsLetter`, quel que soit son alphabet, plus
 * les chiffres, le tiret et le souligné. Un joueur qui tape « Zoé » aurait donc
 * un compte qu'il ne pourrait PAS LIRE dans le jeu — ni sur son écran, ni dans
 * le tableau des places de l'arène, qui est le seul endroit d'où viennent les
 * pseudos des autres (`ns_arene.h`).
 *
 * Le filtre du pseudo refuse donc ce que la police ne sait pas dessiner, et
 * `refus` DIT POURQUOI : un refus muet laisse le joueur retaper la même lettre
 * jusqu'à croire que le clavier est cassé. C'est le pire des deux mondes, et
 * c'est celui qu'on obtient en ne faisant qu'une moitié du travail.
 *
 * L'accord entre le filtre et la police n'est écrit nulle part deux fois :
 * `tests/test_saisie.c` parcourt la table de `ns_font5x7.h` et fait échouer le
 * build si l'un accepte ce que l'autre ne dessine pas.
 */
#ifndef NS_SAISIE_H
#define NS_SAISIE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * LA CAPACITÉ EN OCTETS, et d'où sort ce nombre.
 *
 * `auth.ValidatePassword` accepte 256 caractères, accents et espaces compris.
 * Un caractère UTF-8 coûte jusqu'à quatre octets, donc 1024 est la seule taille
 * qui ne refuse pas un mot de passe que le serveur, lui, accepterait. Un champ
 * qui tronque en silence produit un mot de passe qui ne se retape jamais deux
 * fois pareil : l'inscription réussit, la connexion échoue, et rien ne dit
 * pourquoi.
 *
 * C'est une borne de TAMPON, pas une règle : chaque champ pose la sienne en
 * caractères avec `ns_saisie_init`.
 */
#define NS_SAISIE_OCTETS 1024

/*
 * LA LONGUEUR D'UN CODE DE SALON.
 *
 * C'est `salons.CodeLong` (server/internal/salons/salons.go), et l'alphabet qui
 * va avec vit dans `ns_saisie.c`. Les deux sont ici plutôt que chez l'appelant
 * parce que le filtre porte déjà la moitié difficile : séparer la longueur de
 * l'alphabet, c'est se réveiller un jour avec un champ de sept caractères qui
 * n'en accepte que six sortes.
 */
#define NS_SAISIE_CODE_LONG 6

/*
 * Ce que le champ laisse passer.
 *
 * Trois règles et pas une de plus : chacune correspond à un champ qui existe
 * pour de bon dans `ns_compte.h`. Un filtre « pour plus tard » est exactement ce
 * que `ns_config.h` reproche à ses deux clés réservées.
 */
typedef enum ns_saisie_filtre {
    /*
     * Tout ce que la police sait dessiner.
     *
     * MASQUÉ, il accepte en plus ce qu'elle ne sait pas : un mot de passe n'est
     * jamais dessiné, seulement compté. C'est ce qui rend un mot de passe
     * accentué saisissable ici alors qu'un pseudo accentué ne l'est pas — et la
     * différence n'est pas un caprice : le pseudo, lui, est REDIFFUSÉ aux
     * autres places, où il sera bel et bien dessiné.
     */
    NS_SAISIE_TEXTE = 0,

    /* Lettres ASCII, chiffres, tiret et souligné : `auth.ValidateUsername`
     * moins ce que la police ne dessine pas. */
    NS_SAISIE_PSEUDO,

    /* Les symboles d'un code de salon, mis en capitales à la volée. Le code se
     * dicte à voix haute et se recopie à la main : refuser « k7m3qp » pour la
     * casse serait un refus que rien ne justifie, et le serveur ne le fait pas
     * non plus (`salons.NormaliserCode`). */
    NS_SAISIE_CODE
} ns_saisie_filtre;

/*
 * POURQUOI un caractère n'est pas entré.
 *
 * L'appelant en a besoin pour le dire au joueur ; `ns_saisie_message` en donne
 * une phrase toute faite. Ces quatre raisons demandent quatre gestes différents
 * de la part du joueur, et c'est pour ça qu'elles ne sont pas une seule.
 */
typedef enum ns_saisie_refus {
    NS_SAISIE_ACCEPTE = 0,
    NS_SAISIE_PLEIN,        /* la borne du champ est atteinte */
    NS_SAISIE_HORS_POLICE,  /* la fonte 5 x 7 ne sait pas le dessiner */
    NS_SAISIE_INTERDIT,     /* le filtre du champ n'en veut pas */
    NS_SAISIE_MAL_FORME     /* l'UTF-8 reçu est tronqué ou invalide */
} ns_saisie_refus;

typedef struct ns_saisie {
    /* Toujours terminé par un NUL, et toujours de l'UTF-8 valide : c'est
     * l'invariant que l'insertion caractère par caractère existe pour tenir. */
    char   texte[NS_SAISIE_OCTETS + 1];
    size_t octets;    /* longueur utile, hors NUL */
    size_t curseur;   /* en OCTETS, toujours sur une frontière de caractère */

    size_t           borne;   /* nombre maximal de CARACTÈRES, pas d'octets */
    ns_saisie_filtre filtre;

    /*
     * MASQUÉ, et fixé une fois pour toutes à l'init.
     *
     * Il n'y a pas de bascule « montrer le mot de passe », et son absence est
     * délibérée : c'est le masquage qui autorise le champ à contenir des
     * caractères que la police ne dessine pas. Le démasquer d'un coup afficherait
     * des blancs à la place des accents — et fausserait le défilement, qui
     * compte un caractère par octet parce que le filtre le lui garantit.
     */
    bool   masque;

    /* La raison du PREMIER refus de la dernière écriture, ou `NS_SAISIE_ACCEPTE`.
     * Les déplacements et les effacements n'y touchent pas : elle décrit ce
     * qu'on a essayé d'écrire, pas l'état du champ. */
    ns_saisie_refus refus;
} ns_saisie;

/*
 * `borne` est en caractères et vaut au plus ce que le tampon peut tenir ; 0 la
 * porte au maximum. Un champ construit ici est vide et prêt.
 */
void ns_saisie_init(ns_saisie *s, ns_saisie_filtre filtre, size_t borne, bool masque);

/* Vide le texte et le curseur. Le filtre, la borne et le masquage restent. */
void ns_saisie_vider(ns_saisie *s);

/* Repart d'une chaîne existante — un pseudo déjà connu, un code collé par
 * l'appelant. Filtrage compris : ce qui ne passerait pas au clavier ne passe pas
 * ici non plus. Rend `true` si TOUT a été accepté. */
bool ns_saisie_poser(ns_saisie *s, const char *utf8);

/*
 * Insère au curseur ce qui passe le filtre. Rend `true` si TOUT a été accepté ;
 * sinon `refus` dit ce qui a bloqué en premier.
 *
 * ATOMIQUE PAR CARACTÈRE : une séquence multi-octets entre entière ou pas du
 * tout. SDL livre normalement des caractères complets, mais « normalement » est
 * ce qui laisse un tampon à moitié écrit le jour où ce n'est pas le cas — et un
 * demi-caractère dans une chaîne est un affichage cassé pour tout le monde.
 */
bool ns_saisie_ecrire(ns_saisie *s, const char *utf8);

/* Retour arrière et touche « suppr », au CARACTÈRE et non à l'octet : effacer un
 * « é » d'un mot de passe doit le faire disparaître, pas le couper en deux.
 * Rendent `false` s'il n'y avait rien à effacer. */
bool ns_saisie_effacer(ns_saisie *s);
bool ns_saisie_supprimer(ns_saisie *s);

/* Déplacements, au caractère eux aussi. Aux bornes, ils ne font rien. */
void ns_saisie_gauche(ns_saisie *s);
void ns_saisie_droite(ns_saisie *s);
void ns_saisie_debut(ns_saisie *s);
void ns_saisie_fin(ns_saisie *s);

/* Le contenu compté en caractères, et la position du curseur dans ce compte.
 * C'est ce que le dessin veut : la borne se lit en caractères, et le défilement
 * se calcule en colonnes. */
size_t ns_saisie_caracteres(const ns_saisie *s);
size_t ns_saisie_curseur_caractere(const ns_saisie *s);

/*
 * CE QU'IL FAUT DESSINER, masquage appliqué. Toujours terminé par un NUL,
 * tronqué sur une frontière de caractère si `cap` ne suffit pas. Rend le nombre
 * d'octets écrits, hors NUL.
 *
 * L'invariant qui rend cette sortie utilisable telle quelle : elle ne contient
 * QUE de l'ASCII imprimable, donc un octet par caractère dessiné. Masqué, c'est
 * une étoile par caractère ; non masqué, le filtre a déjà refusé tout ce qui
 * aurait coûté plus d'un octet. Sans cette garantie, `ns_sprite_text_width`
 * mesurerait des octets là où le défilement compte des colonnes.
 */
size_t ns_saisie_affichage(const ns_saisie *s, char *out, size_t cap);

/*
 * La phrase à afficher sous le champ, ou NULL si rien n'a été refusé.
 *
 * Elle dépend du filtre autant que de la raison : « pas d'accent » et « pas
 * d'espace » ne demandent pas le même geste au joueur. Toutes sont sans accent
 * — une explication qui s'afficherait en blancs parce que la police ne sait pas
 * la dessiner serait une plaisanterie coûteuse.
 */
const char *ns_saisie_message(const ns_saisie *s);

#endif /* NS_SAISIE_H */
