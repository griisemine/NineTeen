/*
 * ns_saisie.c — le champ, en C pur.
 *
 * Aucune inclusion d'en-tête moteur, aucune de SDL : c'est vérifiable d'un coup
 * d'œil sur les trois lignes qui suivent, et c'est ce qui permet à
 * `tests/test_saisie.c` d'exercer tout le module sans fenêtre.
 */
#include "ns_saisie.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * UTF-8
 * --------------------------------------------------------------------------
 * Un décodeur écrit ici plutôt qu'emprunté à SDL, et ce n'est pas de la fierté
 * mal placée : `SDL_StepUTF8` ferait entrer SDL dans le seul fichier du lot qui
 * doit s'en passer. Quarante lignes valent ce prix-là.
 * -------------------------------------------------------------------------- */

/* Une frontière de caractère : tout octet qui n'est pas une continuation. */
static bool debut_de_caractere(unsigned char o) { return (o & 0xC0u) != 0x80u; }

/*
 * Décode un caractère. Rend le nombre d'octets consommés, ou 0.
 *
 * Les trois refus du bas ne sont pas du zèle. Une forme surlongue encode « / »
 * en deux octets et traverse tout filtre qui ne regarde que le premier ;
 * les demi-codets et ce qui dépasse le plan 16 ne sont pas de l'UTF-8 et
 * ressortiraient tels quels dans une requête HTTP. Un décodeur qui les laisse
 * passer est un décodeur qui ment sur ce qu'il a lu.
 */
static size_t decoder(const char *p, size_t reste, uint32_t *code)
{
    static const uint32_t minimum[5] = { 0u, 0u, 0x80u, 0x800u, 0x10000u };
    const unsigned char *u = (const unsigned char *)p;
    size_t n;
    uint32_t v;

    if (reste == 0) return 0;

    if (u[0] < 0x80u)              { n = 1; v = u[0]; }
    else if ((u[0] & 0xE0u) == 0xC0u) { n = 2; v = u[0] & 0x1Fu; }
    else if ((u[0] & 0xF0u) == 0xE0u) { n = 3; v = u[0] & 0x0Fu; }
    else if ((u[0] & 0xF8u) == 0xF0u) { n = 4; v = u[0] & 0x07u; }
    else return 0;                 /* continuation isolée, ou 0xFE / 0xFF */

    if (n > reste) return 0;       /* tronquée : c'est le cas qui compte */
    for (size_t i = 1; i < n; ++i) {
        if (debut_de_caractere(u[i])) return 0;
        v = (v << 6) | (u[i] & 0x3Fu);
    }

    if (v < minimum[n]) return 0;                    /* forme surlongue */
    if (v >= 0xD800u && v <= 0xDFFFu) return 0;      /* demi-codet */
    if (v > 0x10FFFFu) return 0;                     /* au-delà du plan 16 */

    *code = v;
    return n;
}

/* Réencode. Le retour au codage est nécessaire parce que le filtre TRANSFORME :
 * un code de salon tapé en minuscules ressort en capitales. */
static size_t encoder(uint32_t v, char out[4])
{
    if (v < 0x80u) {
        out[0] = (char)v;
        return 1;
    }
    if (v < 0x800u) {
        out[0] = (char)(0xC0u | (v >> 6));
        out[1] = (char)(0x80u | (v & 0x3Fu));
        return 2;
    }
    if (v < 0x10000u) {
        out[0] = (char)(0xE0u | (v >> 12));
        out[1] = (char)(0x80u | ((v >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (v & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (v >> 18));
    out[1] = (char)(0x80u | ((v >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((v >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (v & 0x3Fu));
    return 4;
}

/* --------------------------------------------------------------------------
 * Les filtres
 * -------------------------------------------------------------------------- */

/*
 * Les bornes de ce que la fonte 5 x 7 dessine.
 *
 * Elles sont celles de `ns_font5x7.h` — cet en-tête n'est pas inclus ici, parce
 * qu'il ferait entrer une table de glyphes dans un module qui n'a rien à
 * dessiner. C'est `tests/test_saisie.c` qui tient les deux ensemble : il
 * parcourt la table et échoue si ce filtre accepte un code qu'elle ne couvre
 * pas, ou refuse un code qu'elle couvre.
 */
#define POLICE_PREMIER 0x20u
#define POLICE_DERNIER 0x7Eu

/*
 * L'alphabet d'un code de salon — la moitié C de `salons.Alphabet`
 * (server/internal/salons/salons.go), qui explique le choix : sont retirés les
 * couples que l'œil confond en recopiant un code dicté à voix haute, O et 0
 * puis I et 1.
 *
 * Refuser la frappe plutôt que la laisser partir au serveur est le bon moment :
 * un « 0 » tapé pour un « O » donne un code qui ne peut désigner AUCUN salon, et
 * l'apprendre après un aller-retour réseau, c'est l'apprendre trop tard.
 */
static const char g_alphabet_code[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

static bool est_lettre_ascii(uint32_t v)
{
    return (v >= 'A' && v <= 'Z') || (v >= 'a' && v <= 'z');
}

static bool est_chiffre(uint32_t v) { return v >= '0' && v <= '9'; }

static uint32_t en_capitale(uint32_t v)
{
    return (v >= 'a' && v <= 'z') ? v - 'a' + 'A' : v;
}

/*
 * Juge un caractère et rend celui qu'il faut RETENIR — le filtre du code met en
 * capitale, donc l'entrée et la sortie ne sont pas toujours le même caractère.
 */
static ns_saisie_refus juger(const ns_saisie *s, uint32_t v, uint32_t *retenu)
{
    /*
     * Les octets de commande, d'abord et pour tous les filtres. Ce sont ceux
     * avec lesquels un joueur écrit ce qu'il veut sur l'écran d'un autre : un
     * retour à la ligne dans un pseudo, et le tableau des places affiche autre
     * chose que des noms. Le relais les retire déjà de son côté
     * (`pseudoPropre`, server/internal/duel/relay.go) ; les retirer aussi ici,
     * c'est ne pas dépendre du fait que l'autre moitié n'oublie jamais.
     *
     * C'est aussi ce qui fait qu'un code collé avec son retour à la ligne
     * fonctionne : la fin de ligne tombe, le code entre.
     */
    if (v < 0x20u || v == 0x7Fu) return NS_SAISIE_INTERDIT;

    if (v > POLICE_DERNIER) {
        /*
         * Au-delà de l'ASCII imprimable. Seul un champ MASQUÉ en texte libre
         * l'accepte, et la raison est dans `ns_saisie.h` : ce qui n'est jamais
         * dessiné n'a pas à être dessinable.
         *
         * On rend « hors police » sans chercher à savoir si c'est une lettre :
         * ce module ne porte pas de table Unicode, et le message serait le même
         * de toute façon — le jeu ne sait pas l'afficher.
         */
        if (s->masque && s->filtre == NS_SAISIE_TEXTE) { *retenu = v; return NS_SAISIE_ACCEPTE; }
        return NS_SAISIE_HORS_POLICE;
    }

    switch (s->filtre) {
    case NS_SAISIE_PSEUDO:
        if (!est_lettre_ascii(v) && !est_chiffre(v) && v != '-' && v != '_')
            return NS_SAISIE_INTERDIT;
        break;

    case NS_SAISIE_CODE: {
        /* `v` est ici entre 0x20 et 0x7E, donc `haut` tient dans un `char` et
         * n'est jamais nul — `strchr` ne peut pas tomber sur le terminateur. */
        const uint32_t haut = en_capitale(v);
        if (!strchr(g_alphabet_code, (int)haut)) return NS_SAISIE_INTERDIT;
        *retenu = haut;
        return NS_SAISIE_ACCEPTE;
    }

    case NS_SAISIE_TEXTE:
    default:
        break;
    }

    *retenu = v;
    return NS_SAISIE_ACCEPTE;
}

/* --------------------------------------------------------------------------
 * Le tampon
 * -------------------------------------------------------------------------- */

static size_t reculer(const ns_saisie *s, size_t i)
{
    if (i == 0) return 0;
    size_t j = i - 1;
    while (j > 0 && !debut_de_caractere((unsigned char)s->texte[j])) j--;
    return j;
}

static size_t avancer(const ns_saisie *s, size_t i)
{
    if (i >= s->octets) return s->octets;
    size_t j = i + 1;
    while (j < s->octets && !debut_de_caractere((unsigned char)s->texte[j])) j++;
    return j;
}

/*
 * Insère un caractère au curseur, ENTIER ou PAS DU TOUT.
 *
 * Les deux bornes sont testées AVANT le moindre octet écrit : c'est là que se
 * joue la promesse de l'en-tête, et l'ordre inverse — écrire puis constater —
 * laisserait exactement le demi-caractère qu'on veut éviter.
 */
static bool inserer(ns_saisie *s, uint32_t v)
{
    char tampon[4];
    const size_t n = encoder(v, tampon);

    if (ns_saisie_caracteres(s) >= s->borne) return false;
    if (s->octets + n > NS_SAISIE_OCTETS) return false;

    memmove(s->texte + s->curseur + n, s->texte + s->curseur, s->octets - s->curseur);
    memcpy(s->texte + s->curseur, tampon, n);
    s->octets  += n;
    s->curseur += n;
    s->texte[s->octets] = '\0';
    return true;
}

/* --------------------------------------------------------------------------
 * La surface publique
 * -------------------------------------------------------------------------- */

void ns_saisie_init(ns_saisie *s, ns_saisie_filtre filtre, size_t borne, bool masque)
{
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->filtre = filtre;
    s->masque = masque;
    /* Une borne nulle vaut « tout ce qui tient » plutôt que « rien » : un champ
     * qu'on n'a pas su dimensionner doit rester utilisable, pas devenir muet. */
    s->borne = (borne == 0 || borne > NS_SAISIE_OCTETS) ? NS_SAISIE_OCTETS : borne;
}

void ns_saisie_vider(ns_saisie *s)
{
    if (!s) return;
    s->texte[0] = '\0';
    s->octets   = 0;
    s->curseur  = 0;
    s->refus    = NS_SAISIE_ACCEPTE;
}

bool ns_saisie_ecrire(ns_saisie *s, const char *utf8)
{
    if (!s) return false;
    s->refus = NS_SAISIE_ACCEPTE;
    if (!utf8) return true;

    const size_t total = strlen(utf8);
    size_t i = 0;
    bool tout = true;

    while (i < total) {
        uint32_t lu = 0;
        const size_t n = decoder(utf8 + i, total - i, &lu);
        if (n == 0) {
            /*
             * On S'ARRÊTE. Une séquence invalide n'a pas de longueur connue,
             * donc pas de point de reprise fiable : sauter « un octet » pour
             * continuer, c'est deviner, et deviner ici fabrique des caractères
             * que personne n'a tapés. Ce qui précède reste, et il est valide.
             */
            if (tout) s->refus = NS_SAISIE_MAL_FORME;
            return false;
        }

        uint32_t retenu = lu;
        const ns_saisie_refus r = juger(s, lu, &retenu);
        if (r != NS_SAISIE_ACCEPTE) {
            if (tout) { s->refus = r; tout = false; }
        } else if (!inserer(s, retenu)) {
            if (tout) { s->refus = NS_SAISIE_PLEIN; tout = false; }
        }
        i += n;
    }
    return tout;
}

bool ns_saisie_poser(ns_saisie *s, const char *utf8)
{
    if (!s) return false;
    ns_saisie_vider(s);
    return ns_saisie_ecrire(s, utf8);
}

bool ns_saisie_effacer(ns_saisie *s)
{
    if (!s || s->curseur == 0) return false;
    const size_t debut = reculer(s, s->curseur);
    memmove(s->texte + debut, s->texte + s->curseur, s->octets - s->curseur);
    s->octets -= s->curseur - debut;
    s->curseur = debut;
    s->texte[s->octets] = '\0';
    return true;
}

bool ns_saisie_supprimer(ns_saisie *s)
{
    if (!s || s->curseur >= s->octets) return false;
    const size_t fin = avancer(s, s->curseur);
    memmove(s->texte + s->curseur, s->texte + fin, s->octets - fin);
    s->octets -= fin - s->curseur;
    s->texte[s->octets] = '\0';
    return true;
}

void ns_saisie_gauche(ns_saisie *s) { if (s) s->curseur = reculer(s, s->curseur); }
void ns_saisie_droite(ns_saisie *s) { if (s) s->curseur = avancer(s, s->curseur); }
void ns_saisie_debut(ns_saisie *s)  { if (s) s->curseur = 0; }
void ns_saisie_fin(ns_saisie *s)    { if (s) s->curseur = s->octets; }

size_t ns_saisie_caracteres(const ns_saisie *s)
{
    if (!s) return 0;
    size_t n = 0;
    for (size_t i = 0; i < s->octets; ++i)
        if (debut_de_caractere((unsigned char)s->texte[i])) n++;
    return n;
}

size_t ns_saisie_curseur_caractere(const ns_saisie *s)
{
    if (!s) return 0;
    size_t n = 0;
    for (size_t i = 0; i < s->curseur; ++i)
        if (debut_de_caractere((unsigned char)s->texte[i])) n++;
    return n;
}

size_t ns_saisie_affichage(const ns_saisie *s, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!s) return 0;

    size_t ecrits = 0;
    if (s->masque) {
        /* Une étoile par CARACTÈRE, et non par octet : un mot de passe accentué
         * afficherait sinon deux étoiles pour un « é », ce qui trahit sa
         * composition à qui regarde par-dessus l'épaule. */
        const size_t n = ns_saisie_caracteres(s);
        while (ecrits < n && ecrits + 1 < cap) out[ecrits++] = '*';
    } else {
        /* Le filtre garantit que tout est de l'ASCII imprimable ; la troncature
         * sur frontière est là pour le cas où il ne le garantirait plus. */
        while (ecrits < s->octets && ecrits + 1 < cap) {
            out[ecrits] = s->texte[ecrits];
            ecrits++;
        }
        while (ecrits > 0 && !debut_de_caractere((unsigned char)out[ecrits - 1])) ecrits--;
    }
    out[ecrits] = '\0';
    return ecrits;
}

const char *ns_saisie_message(const ns_saisie *s)
{
    if (!s) return NULL;
    switch (s->refus) {
    case NS_SAISIE_ACCEPTE:
        return NULL;
    case NS_SAISIE_PLEIN:
        return "LONGUEUR MAXIMALE ATTEINTE";
    case NS_SAISIE_HORS_POLICE:
        /* La phrase nomme les accents parce que c'est ce qu'un joueur français
         * vient de taper neuf fois sur dix. Elle dit « ce jeu », pas « ce
         * nom » : le serveur, lui, les accepterait. */
        return s->filtre == NS_SAISIE_CODE
             ? "UN CODE DE SALON N'A NI ACCENT NI SYMBOLE"
             : "SANS ACCENT : LE JEU NE SAIT PAS LES DESSINER";
    case NS_SAISIE_INTERDIT:
        switch (s->filtre) {
        case NS_SAISIE_PSEUDO: return "LETTRES, CHIFFRES, TIRET ET SOULIGNE SEULEMENT";
        case NS_SAISIE_CODE:   return "LETTRES ET CHIFFRES, SANS I, O, 0 NI 1";
        default:               return "CE CARACTERE N'EST PAS ACCEPTE";
        }
    case NS_SAISIE_MAL_FORME:
    default:
        return "TEXTE MAL FORME, IGNORE";
    }
}
