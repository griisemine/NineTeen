/*
 * ns_saisie_sdl.c — la traduction, et rien qu'elle.
 *
 * Pas une règle d'édition ici : tout ce qui décide de ce qui entre dans le
 * tampon est dans `ns_saisie.c`, qui se teste sans fenêtre. Ce fichier fait la
 * seule chose qu'on ne peut pas tester sans clavier — reconnaître une touche —
 * et il la fait en une page, ce qui est aussi la meilleure défense contre le
 * fait qu'elle ne soit pas testée.
 */
#include "ns_saisie_sdl.h"

#include "ns_core.h"

bool ns_saisie_sdl_ouvrir(SDL_Window *fenetre)
{
    if (!fenetre) return false;
    if (!SDL_StartTextInput(fenetre)) {
        NS_WARN("saisie : SDL refuse d'ouvrir le texte (%s)", SDL_GetError());
        return false;
    }
    return true;
}

void ns_saisie_sdl_fermer(SDL_Window *fenetre)
{
    if (fenetre) SDL_StopTextInput(fenetre);
}

bool ns_saisie_sdl_active(SDL_Window *fenetre)
{
    return fenetre && SDL_TextInputActive(fenetre);
}

void ns_saisie_sdl_zone(SDL_Window *fenetre, int x, int y, int w, int h, int curseur)
{
    if (!fenetre) return;
    const SDL_Rect r = { x, y, w, h };
    SDL_SetTextInputArea(fenetre, &r, curseur);
}

/*
 * Le modificateur du collage : Ctrl OU Cmd, sur toutes les plateformes.
 *
 * macOS colle avec Cmd et le reste du monde avec Ctrl, et accepter les deux
 * partout ne coûte rien : aucune des deux combinaisons ne veut dire autre chose
 * dans un champ de texte. Un Mac branché sur un clavier PC, ou l'inverse, est
 * un cas assez fréquent pour qu'on ne demande à personne de s'en souvenir.
 */
static bool modificateur_de_collage(SDL_Keymod mod)
{
    return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

bool ns_saisie_sdl_evenement(ns_saisie *s, const SDL_Event *ev)
{
    if (!s || !ev) return false;

    if (ev->type == SDL_EVENT_TEXT_INPUT) {
        /*
         * C'est SDL qui décode la disposition, les touches mortes et les
         * méthodes d'entrée ; ce qui arrive ici est de l'UTF-8 déjà composé.
         * On n'écoute donc PAS les touches de lettres : le faire écrirait « ^ »
         * puis « e » là où le joueur a tapé « ê », et doublerait tout le reste.
         */
        ns_saisie_ecrire(s, ev->text.text);
        return true;
    }

    if (ev->type != SDL_EVENT_KEY_DOWN) return false;

    /*
     * Les répétitions passent comme les frappes : garder « retour arrière »
     * enfoncé doit vider le champ, sinon on efface un pseudo de vingt lettres
     * en vingt appuis.
     */
    switch (ev->key.key) {
    case SDLK_BACKSPACE: ns_saisie_effacer(s);   return true;
    case SDLK_DELETE:    ns_saisie_supprimer(s); return true;
    case SDLK_LEFT:      ns_saisie_gauche(s);    return true;
    case SDLK_RIGHT:     ns_saisie_droite(s);    return true;
    case SDLK_HOME:      ns_saisie_debut(s);     return true;
    case SDLK_END:       ns_saisie_fin(s);       return true;

    case SDLK_V:
        /*
         * Le CODE de touche et non le code physique, contrairement au
         * déplacement dans la salle qui vise les positions QWERTY (`main.c`).
         * Ici c'est bien la LETTRE V qui compte : « Ctrl+V » est ce que le
         * joueur lit dans son navigateur et sur son système, quelle que soit sa
         * disposition.
         */
        if (modificateur_de_collage(ev->key.mod)) {
            ns_saisie_sdl_coller(s);
            return true;
        }
        return false;

    default:
        return false;
    }
}

bool ns_saisie_sdl_coller(ns_saisie *s)
{
    if (!s) return false;

    /*
     * SDL rend une chaîne à LIBÉRER, et une chaîne vide plutôt que NULL quand
     * il n'y a rien ou que la copie a manqué de mémoire. On teste quand même le
     * pointeur : ce qui sort d'une bibliothèque n'est pas ce qu'on suppose,
     * c'est ce qu'on vérifie.
     */
    char *texte = SDL_GetClipboardText();
    if (!texte) return false;
    const bool tout = ns_saisie_ecrire(s, texte);
    SDL_free(texte);
    return tout;
}
