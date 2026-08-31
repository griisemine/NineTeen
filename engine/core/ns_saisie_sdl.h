/*
 * ns_saisie_sdl.h — le pont entre SDL et `ns_saisie`.
 *
 * POURQUOI C'EST UN FICHIER À PART, ET POURQUOI ICI
 * ------------------------------------------------
 * À part, parce que la séparation ne vaut que si elle est VÉRIFIABLE : c'est le
 * seul fichier du lot qui inclut SDL, et `tests/test_saisie.c` exerce tout le
 * reste sans jamais l'ouvrir. Une déclaration de plus dans `ns_saisie.h`, même
 * bien commentée, aurait suffi à faire entrer SDL partout.
 *
 * Ici, dans `engine/core/`, parce que le noyau parle DÉJÀ à SDL — l'horloge et
 * les chemins n'ont pas d'autre dépendance (`ns_core.h`) — et parce que le pont
 * et le champ qu'il alimente se lisent mieux côte à côte qu'à deux étages
 * différents. Il n'a rien à faire dans `room/` : c'est une brique de moteur, et
 * l'écran qui s'en servira n'est pas encore écrit.
 *
 * CE QU'IL FAUT SAVOIR DE SDL3 ICI
 * --------------------------------
 * `SDL_StartTextInput` et `SDL_StopTextInput` prennent la FENÊTRE en argument,
 * ce qui n'était pas le cas en SDL2 ; et la saisie de texte est ÉTEINTE par
 * défaut, donc `SDL_EVENT_TEXT_INPUT` n'arrive jamais tant qu'on ne l'a pas
 * allumée. Vérifié dans les sources vendorisées, `SDL_keyboard.h` :
 *
 *     bool SDL_StartTextInput(SDL_Window *window);
 *     bool SDL_StopTextInput(SDL_Window *window);
 *     bool SDL_TextInputActive(SDL_Window *window);
 *
 * L'ALLUMER A UN COÛT, et c'est ce qui empêche de le laisser allumé en
 * permanence. La documentation de SDL le dit à cet endroit précis : « on some
 * platforms using this function shows the screen keyboard and/or activates an
 * IME, which can prevent some key press events from being passed through ». Un
 * clavier logiciel par-dessus la salle, et des touches de jeu qui n'arrivent
 * plus : on l'allume quand un champ prend le focus, on l'éteint quand il le
 * perd.
 */
#ifndef NS_SAISIE_SDL_H
#define NS_SAISIE_SDL_H

#include "ns_saisie.h"

#include <SDL3/SDL.h>

/* Allume et éteint la saisie pour cette fenêtre. `ouvrir` rend `false` et
 * journalise si SDL refuse — un champ qui ne recevra jamais rien doit se voir
 * dans le journal, pas se deviner devant un écran muet. */
bool ns_saisie_sdl_ouvrir(SDL_Window *fenetre);
void ns_saisie_sdl_fermer(SDL_Window *fenetre);
bool ns_saisie_sdl_active(SDL_Window *fenetre);

/*
 * Où se trouve le champ à l'écran, en PIXELS de la fenêtre, et à quelle distance
 * de son bord gauche se tient le curseur.
 *
 * C'est ce qui place la fenêtre de composition d'une méthode d'entrée (japonais,
 * chinois, coréen) et le retour du clavier virtuel. Sans cet appel, la
 * composition s'affiche là où le système veut — c'est-à-dire souvent par-dessus
 * ce que le joueur est en train de taper.
 */
void ns_saisie_sdl_zone(SDL_Window *fenetre, int x, int y, int w, int h, int curseur);

/*
 * Traduit UN événement. Rend `true` si l'événement a été CONSOMMÉ.
 *
 * Ce retour est ce qui compte pour l'appelant : tant qu'un champ a le focus, un
 * « W » est une lettre et pas un pas en avant, et une flèche déplace le curseur
 * et pas la caméra. Sans lui, taper son pseudo ferait traverser la salle.
 *
 * Ne sont PAS consommés — délibérément — l'entrée, l'échappement et la
 * tabulation : valider, annuler et changer de champ sont des décisions d'écran,
 * pas de champ de texte. Un champ qui avalerait « Échap » enfermerait le joueur
 * dedans.
 */
bool ns_saisie_sdl_evenement(ns_saisie *s, const SDL_Event *ev);

/*
 * Colle le presse-papier au curseur. Rend `true` si tout a été accepté.
 *
 * Le dépôt ne lisait le presse-papier NULLE PART jusqu'ici. C'est pourtant avec
 * ça qu'on entre un code de salon reçu par message : le recopier à la main est
 * précisément ce que les six caractères de `salons.CodeLong` cherchent à rendre
 * possible, pas ce qu'on veut imposer.
 *
 * Le filtre fait le ménage au passage : un code copié avec ses espaces et son
 * retour à la ligne entre quand même, parce que ce qui n'est pas dans
 * l'alphabet tombe au lieu de faire échouer le collage entier.
 */
bool ns_saisie_sdl_coller(ns_saisie *s);

#endif /* NS_SAISIE_SDL_H */
