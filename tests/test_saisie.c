/*
 * test_saisie.c — le champ de saisie, sans fenêtre et sans clavier.
 *
 * CE QUE CE FICHIER N'INCLUT PAS, ET C'EST LE PREMIER RÉSULTAT
 * -----------------------------------------------------------
 * Pas de SDL. C'est la vérification la moins chère de la séparation annoncée
 * par `ns_saisie.h` : si le champ finissait par appeler SDL, ce test cesserait
 * de compiler. Tout ce qui a besoin de SDL vit dans `ns_saisie_sdl.c`, et ce qui
 * a besoin d'un GPU dans `ns_saisie_draw.c` — ni l'un ni l'autre n'est ici.
 *
 * CE QU'IL VÉRIFIE QU'AUCUNE RELECTURE NE VÉRIFIE
 * -----------------------------------------------
 * L'ACCORD ENTRE LE FILTRE ET LA POLICE. Le filtre du texte libre est censé
 * accepter exactement ce que la fonte 5 x 7 sait dessiner ; les deux sont écrits
 * dans deux fichiers qui ne se connaissent pas — délibérément, `ns_saisie.c`
 * n'inclut pas la table des glyphes. `test_police` les confronte code par code,
 * de sorte qu'étendre la fonte sans détendre le filtre, ou l'inverse, casse le
 * build. C'est la seule chose qui empêche les deux de diverger en silence.
 *
 * CE QU'IL NE PEUT PAS VÉRIFIER, ET IL FAUT LE DIRE
 * -------------------------------------------------
 * La frappe réelle et le collage. `SDL_EVENT_TEXT_INPUT` demande une fenêtre
 * avec le focus clavier, et `SDL_GetClipboardText` un serveur graphique : les
 * deux sortent de ce que la CI peut faire. Ce qui est testé ici est ce que le
 * pont APPELLE — insertion, effacement, déplacements, filtres — donc tout sauf
 * la reconnaissance des touches, qui tient en un `switch` d'une page.
 */
#include "ns_font5x7.h"
#include "ns_saisie.h"

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

/* Le texte affiché, pour comparer d'un coup. */
static const char *vu(const ns_saisie *s, char *out, size_t cap)
{
    ns_saisie_affichage(s, out, cap);
    return out;
}

/* --------------------------------------------------------------------------
 * L'édition
 * -------------------------------------------------------------------------- */

static void test_insertion(void)
{
    ns_saisie s;
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 16, false);

    CHECK(ns_saisie_ecrire(&s, "ABC"), "trois caracteres passent");
    CHECK(strcmp(s.texte, "ABC") == 0, "…dans l'ordre (%s)", s.texte);
    CHECK(s.curseur == 3, "le curseur suit ce qu'on tape (%zu)", s.curseur);

    /* Au MILIEU : c'est le cas qui distingue un champ d'un `strcat`. */
    ns_saisie_gauche(&s);
    CHECK(ns_saisie_ecrire(&s, "X"), "on insere avant le dernier");
    CHECK(strcmp(s.texte, "ABXC") == 0, "…et le reste se decale (%s)", s.texte);
    CHECK(s.curseur == 3, "le curseur reste derriere ce qu'on vient d'ecrire (%zu)",
          s.curseur);

    ns_saisie_debut(&s);
    CHECK(ns_saisie_ecrire(&s, "9"), "on insere en tete");
    CHECK(strcmp(s.texte, "9ABXC") == 0, "…sans rien perdre (%s)", s.texte);

    ns_saisie_vider(&s);
    CHECK(s.octets == 0 && s.curseur == 0 && s.texte[0] == '\0', "vider vide tout");
    CHECK(s.borne == 16 && s.filtre == NS_SAISIE_TEXTE,
          "…mais garde la borne et le filtre");
}

/*
 * L'EFFACEMENT D'UN CARACTÈRE MULTI-OCTETS.
 *
 * Le défaut classique, et il est silencieux : effacer un octet laisse la moitié
 * d'un « é » dans le tampon, la chaîne cesse d'être de l'UTF-8 valide, et c'est
 * le serveur qui le découvre. Le champ est masqué parce que c'est le seul cas où
 * un accent peut y entrer — voir `ns_saisie.h`.
 */
static void test_effacement_multioctet(void)
{
    ns_saisie s;
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 16, true);

    CHECK(ns_saisie_ecrire(&s, "AéB"), "un mot de passe accentue entre dans un champ masque");
    CHECK(s.octets == 4, "« é » coute deux octets (%zu au total)", s.octets);
    CHECK(ns_saisie_caracteres(&s) == 3, "…et reste UN caractere (%zu)",
          ns_saisie_caracteres(&s));

    CHECK(ns_saisie_effacer(&s), "retour arriere sur le B");
    CHECK(strcmp(s.texte, "Aé") == 0, "…qui part seul (%s)", s.texte);

    CHECK(ns_saisie_effacer(&s), "retour arriere sur le « é »");
    CHECK(s.octets == 1 && strcmp(s.texte, "A") == 0,
          "…qui part ENTIER, ses deux octets ensemble (%zu octet(s) : %s)",
          s.octets, s.texte);

    /* Et par l'autre bout : « suppr » depuis le début. */
    ns_saisie_poser(&s, "éB");
    ns_saisie_debut(&s);
    CHECK(ns_saisie_supprimer(&s), "suppr mange le caractere de droite");
    CHECK(s.octets == 1 && strcmp(s.texte, "B") == 0,
          "…entier lui aussi (%zu octet(s) : %s)", s.octets, s.texte);
}

static void test_curseur_aux_bornes(void)
{
    ns_saisie s;
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 16, true);
    ns_saisie_poser(&s, "aébc");

    ns_saisie_debut(&s);
    CHECK(s.curseur == 0, "debut pose le curseur en tete (%zu)", s.curseur);
    ns_saisie_gauche(&s);
    CHECK(s.curseur == 0, "a gauche de la premiere lettre il n'y a rien (%zu)", s.curseur);
    CHECK(!ns_saisie_effacer(&s), "…et rien a effacer non plus");

    /* Un pas vers la droite doit franchir le « é » d'un bloc. */
    ns_saisie_droite(&s);
    ns_saisie_droite(&s);
    CHECK(s.curseur == 3, "un pas franchit les deux octets du « é » (%zu)", s.curseur);
    CHECK(ns_saisie_curseur_caractere(&s) == 2,
          "…soit deux caracteres parcourus (%zu)", ns_saisie_curseur_caractere(&s));

    ns_saisie_fin(&s);
    CHECK(s.curseur == s.octets, "fin pose le curseur derriere tout (%zu)", s.curseur);
    ns_saisie_droite(&s);
    CHECK(s.curseur == s.octets, "a droite de la derniere lettre il n'y a rien (%zu)",
          s.curseur);
    CHECK(!ns_saisie_supprimer(&s), "…et rien a supprimer non plus");
}

/*
 * LA BORNE HAUTE, comptée en CARACTÈRES.
 *
 * Un champ qui compterait des octets refuserait un mot de passe de douze lettres
 * accentuées alors que le serveur en accepte 256 — et le refuserait sans que
 * personne comprenne pourquoi, puisque le joueur, lui, compte des lettres.
 */
static void test_borne_haute(void)
{
    ns_saisie s;
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 4, false);

    CHECK(!ns_saisie_ecrire(&s, "ABCDEF"), "six caracteres pour quatre : refuse");
    CHECK(s.refus == NS_SAISIE_PLEIN, "…et la raison est la borne (%d)", (int)s.refus);
    CHECK(strcmp(s.texte, "ABCD") == 0, "…mais les quatre premiers sont entres (%s)",
          s.texte);
    CHECK(ns_saisie_message(&s) != NULL, "…avec une phrase a montrer au joueur");

    /* Une borne en caractères et non en octets. */
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 3, true);
    CHECK(ns_saisie_ecrire(&s, "ééé"), "trois accents pour trois caracteres : ca passe");
    CHECK(s.octets == 6, "…et ils pesent six octets (%zu)", s.octets);
    CHECK(!ns_saisie_ecrire(&s, "é"), "le quatrieme ne passe pas");
    CHECK(s.octets == 6, "…et n'a rien ecrit du tout (%zu)", s.octets);

    /* Une borne nulle vaut « tout ce qui tient » : voir `ns_saisie_init`. */
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 0, false);
    CHECK(s.borne == NS_SAISIE_OCTETS, "une borne nulle ouvre le tampon en grand (%zu)",
          s.borne);
}

/* --------------------------------------------------------------------------
 * Les filtres
 * -------------------------------------------------------------------------- */

static void test_filtre_pseudo(void)
{
    ns_saisie s;
    ns_saisie_init(&s, NS_SAISIE_PSEUDO, 24, false);

    CHECK(ns_saisie_ecrire(&s, "Jo-nas_19"), "lettres, chiffres, tiret et souligne");
    CHECK(strcmp(s.texte, "Jo-nas_19") == 0, "…tels quels (%s)", s.texte);

    ns_saisie_vider(&s);
    CHECK(!ns_saisie_ecrire(&s, "jo nas"), "un espace ne passe pas dans un pseudo");
    CHECK(s.refus == NS_SAISIE_INTERDIT, "…et c'est le filtre qui le dit (%d)",
          (int)s.refus);
    CHECK(strcmp(s.texte, "jonas") == 0, "…le reste passe (%s)", s.texte);

    /*
     * LE CAS QUI JUSTIFIE TOUT LE FILTRE.
     *
     * « Zoé » est un pseudo que le serveur accepte : `auth.ValidateUsername` ne
     * demande qu'un `unicode.IsLetter`. Le laisser passer ici fabriquerait un
     * compte que son propre titulaire ne peut pas lire dans le jeu, et que
     * personne ne peut lire dans le tableau des places. Le refus doit donc
     * arriver AVANT l'inscription, et il doit se distinguer d'un refus de forme
     * — sinon le message ne peut rien dire d'utile.
     */
    ns_saisie_vider(&s);
    CHECK(!ns_saisie_ecrire(&s, "Zoé"), "un pseudo accentue est refuse");
    CHECK(s.refus == NS_SAISIE_HORS_POLICE,
          "…parce que la POLICE ne le dessine pas, pas parce que le nom serait mauvais (%d)",
          (int)s.refus);
    CHECK(strcmp(s.texte, "Zo") == 0, "…et ce qui precede reste (%s)", s.texte);

    /* Masquer un pseudo ne le rend PAS accentuable : il sera dessiné ailleurs. */
    ns_saisie_init(&s, NS_SAISIE_PSEUDO, 24, true);
    CHECK(!ns_saisie_ecrire(&s, "é"),
          "un pseudo masque refuse l'accent quand meme : les autres le verront");
    CHECK(s.refus == NS_SAISIE_HORS_POLICE, "…pour la meme raison (%d)", (int)s.refus);
}

static void test_filtre_code(void)
{
    ns_saisie s;
    ns_saisie_init(&s, NS_SAISIE_CODE, NS_SAISIE_CODE_LONG, false);

    /* LA MISE EN CAPITALES À LA VOLÉE. Un code se dicte à l'oral ; refuser
     * « k7m3qp » pour la casse serait un refus que rien ne justifie, et le
     * serveur ne le fait pas non plus (`salons.NormaliserCode`). */
    CHECK(ns_saisie_ecrire(&s, "k7m3qp"), "un code tape en minuscules passe");
    CHECK(strcmp(s.texte, "K7M3QP") == 0, "…et ressort en capitales (%s)", s.texte);
    CHECK(ns_saisie_caracteres(&s) == NS_SAISIE_CODE_LONG,
          "…sur la longueur du serveur (%zu)", ns_saisie_caracteres(&s));

    CHECK(!ns_saisie_ecrire(&s, "Z"), "un septieme caractere ne rentre pas");
    CHECK(s.refus == NS_SAISIE_PLEIN, "…parce que le champ est plein (%d)", (int)s.refus);

    /* Les quatre symboles que l'alphabet du serveur retire, parce que l'œil les
     * confond en recopiant un code dicté : O et 0, I et 1. */
    static const char *const confondus[] = { "O", "0", "I", "1" };
    for (size_t i = 0; i < sizeof confondus / sizeof confondus[0]; ++i) {
        ns_saisie_vider(&s);
        CHECK(!ns_saisie_ecrire(&s, confondus[i]),
              "« %s » n'est pas dans l'alphabet des codes", confondus[i]);
        CHECK(s.octets == 0, "…et n'entre pas (%zu octet(s))", s.octets);
    }

    /* Un code collé depuis un message arrive avec ses espaces et sa fin de
     * ligne : il doit entrer quand même, sinon le collage ne sert à rien. */
    ns_saisie_vider(&s);
    ns_saisie_ecrire(&s, "  k7m3qp\n");
    CHECK(strcmp(s.texte, "K7M3QP") == 0,
          "un code colle avec ses blancs entre proprement (%s)", s.texte);
}

static void test_filtre_texte(void)
{
    ns_saisie s;

    /* Non masqué : ce que la police ne dessine pas est refusé, même en texte
     * libre — un champ qui affiche des blancs vaut moins qu'un champ qui dit non. */
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 32, false);
    CHECK(ns_saisie_ecrire(&s, "Salut ! #19 (a-b)"), "l'ASCII imprimable passe en entier");
    ns_saisie_vider(&s);
    CHECK(!ns_saisie_ecrire(&s, "é"), "un accent ne passe pas dans un champ visible");
    CHECK(s.refus == NS_SAISIE_HORS_POLICE, "…et la raison est la police (%d)", (int)s.refus);

    /* Masqué : il passe, et c'est exactement ce que le serveur autorise dans un
     * mot de passe — « accents et espaces compris » (`auth.ValidatePassword`). */
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 32, true);
    CHECK(ns_saisie_ecrire(&s, "un été à Paris"),
          "un mot de passe accentue passe dans un champ masque");
    CHECK(ns_saisie_caracteres(&s) == 14, "…et compte ses caracteres, pas ses octets (%zu)",
          ns_saisie_caracteres(&s));

    /* Les octets de commande tombent partout : c'est avec eux qu'on écrit sur
     * l'écran des autres (voir `pseudoPropre`, relay.go). */
    ns_saisie_vider(&s);
    CHECK(!ns_saisie_ecrire(&s, "a\nb\tc"), "un retour a la ligne ne passe pas");
    CHECK(strcmp(s.texte, "abc") == 0, "…et le texte se recolle (%s)", s.texte);
}

static void test_poser(void)
{
    ns_saisie s;
    ns_saisie_init(&s, NS_SAISIE_PSEUDO, 24, false);
    ns_saisie_ecrire(&s, "ancien");

    CHECK(ns_saisie_poser(&s, "NOUVEAU"), "poser remplace tout");
    CHECK(strcmp(s.texte, "NOUVEAU") == 0, "…sans garder un octet de l'ancien (%s)", s.texte);
    CHECK(s.curseur == s.octets, "…et laisse le curseur au bout (%zu)", s.curseur);

    CHECK(!ns_saisie_poser(&s, "Zoé"), "poser filtre comme le clavier");
    CHECK(strcmp(s.texte, "Zo") == 0, "…et ne pose que ce qui passe (%s)", s.texte);
}

/* --------------------------------------------------------------------------
 * Le masquage
 * -------------------------------------------------------------------------- */

static void test_masquage(void)
{
    char aff[64];
    ns_saisie s;
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 32, true);
    ns_saisie_poser(&s, "secrét");

    CHECK(strcmp(s.texte, "secrét") == 0, "le champ garde le vrai texte (%s)", s.texte);
    CHECK(strcmp(vu(&s, aff, sizeof aff), "******") == 0,
          "…et n'en montre que des etoiles (%s)", aff);
    CHECK(strlen(aff) == ns_saisie_caracteres(&s),
          "UNE etoile par caractere, pas par octet — sinon la longueur du mot de "
          "passe se lit par-dessus l'epaule (%zu pour %zu)",
          strlen(aff), ns_saisie_caracteres(&s));

    /* Non masqué, l'affichage est le texte — et il tient en un octet par
     * caractère, ce dont le défilement du dessin dépend. */
    ns_saisie_init(&s, NS_SAISIE_PSEUDO, 32, false);
    ns_saisie_poser(&s, "jonas19");
    CHECK(strcmp(vu(&s, aff, sizeof aff), "jonas19") == 0,
          "un champ visible montre son texte (%s)", aff);
    CHECK(strlen(aff) == ns_saisie_caracteres(&s),
          "…un octet par caractere dessine (%zu pour %zu)",
          strlen(aff), ns_saisie_caracteres(&s));

    /* Un tampon trop court tronque et termine quand même. */
    char court[4];
    CHECK(ns_saisie_affichage(&s, court, sizeof court) == 3,
          "un tampon de quatre rend trois caracteres (%zu)",
          ns_saisie_affichage(&s, court, sizeof court));
    CHECK(strcmp(court, "jon") == 0, "…et reste terminé (%s)", court);
    CHECK(ns_saisie_affichage(&s, court, 0) == 0, "un tampon nul ne rend rien");
}

/* --------------------------------------------------------------------------
 * L'UTF-8 malmené
 * -------------------------------------------------------------------------- */

/*
 * CE QUI ARRIVE PAR LA SOCKET, PAR LE PRESSE-PAPIER, OU PAR UN SDL QUI TOUSSE.
 *
 * Les cinq formes ci-dessous sont les cinq façons de tromper un décodeur naïf.
 * La seule chose qui compte pour ce champ : aucune ne doit laisser le tampon
 * dans un état à moitié écrit, parce qu'une chaîne qui n'est plus de l'UTF-8
 * valide casse le serveur, le relais et l'affichage des autres joueurs — trois
 * endroits où personne ne pensera à revenir chercher la cause.
 */
static void test_utf8_malmene(void)
{
    static const struct { const char *quoi; const char *entree; const char *reste; } cas[] = {
        { "une sequence coupee en plein vol",      "AB\xC3",         "AB" },
        { "une continuation qui n'en est pas une", "AB\xC3\x28",     "AB" },
        { "un octet de continuation isole",        "AB\xA9",         "AB" },
        { "une forme surlongue (« / » en deux octets)", "AB\xC0\xAF", "AB" },
        { "un demi-codet UTF-16",                  "AB\xED\xA0\x80", "AB" },
    };

    for (size_t i = 0; i < sizeof cas / sizeof cas[0]; ++i) {
        ns_saisie s;
        ns_saisie_init(&s, NS_SAISIE_TEXTE, 32, true);
        CHECK(!ns_saisie_ecrire(&s, cas[i].entree), "%s est refusee", cas[i].quoi);
        CHECK(s.refus == NS_SAISIE_MAL_FORME, "…comme mal formee (%d)", (int)s.refus);
        CHECK(strcmp(s.texte, cas[i].reste) == 0,
              "…et ce qui precedait reste intact, sans demi-caractere (%s)", s.texte);
        CHECK(s.octets == strlen(s.texte),
              "…la longueur et le NUL restent d'accord (%zu / %zu)",
              s.octets, strlen(s.texte));
    }

    /* Et l'inverse : une séquence valide de quatre octets entre entière. */
    ns_saisie s;
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 32, true);
    CHECK(ns_saisie_ecrire(&s, "\xF0\x9F\x8E\xAE"), "un caractere de quatre octets passe");
    CHECK(s.octets == 4 && ns_saisie_caracteres(&s) == 1,
          "…d'un bloc (%zu octets pour %zu caractere)", s.octets, ns_saisie_caracteres(&s));
    CHECK(ns_saisie_effacer(&s) && s.octets == 0,
          "…et s'efface d'un bloc (%zu octet(s) restant(s))", s.octets);
}

/* --------------------------------------------------------------------------
 * L'accord avec la police
 * -------------------------------------------------------------------------- */

static void test_police(void)
{
    /*
     * LA COUVERTURE, MESURÉE dans la table plutôt que relue dans son
     * commentaire. Un glyphe entièrement vide serait un caractère que le filtre
     * accepte et que l'écran n'affiche pas — c'est-à-dire le défaut même qu'on
     * cherche à empêcher, mais du dedans.
     */
    int vides = 0;
    for (int g = 0; g < NS_FONT5X7_GLYPHS; ++g) {
        int allume = 0;
        for (int col = 0; col < NS_FONT5X7_COLS; ++col) allume |= ns_font5x7[g][col];
        if (!allume) vides++;
    }
    CHECK(vides == 1, "un seul glyphe vide dans la table, l'espace (%d)", vides);

    /*
     * L'ACCORD, code par code. `ns_saisie.c` n'inclut pas la table : c'est ce
     * test qui tient les deux ensemble, et lui seul.
     */
    int desaccords = 0, acceptes = 0;
    for (int code = 1; code <= 0xFF; ++code) {
        const char un[2] = { (char)code, '\0' };
        ns_saisie s;
        ns_saisie_init(&s, NS_SAISIE_TEXTE, 8, false);
        const bool pris = ns_saisie_ecrire(&s, un) && s.octets == 1;
        const bool dessine = (code >= NS_FONT5X7_FIRST
                           && code <  NS_FONT5X7_FIRST + NS_FONT5X7_GLYPHS);
        if (pris != dessine) {
            desaccords++;
            printf("  desaccord sur 0x%02X : filtre %s, police %s\n",
                   (unsigned)code, pris ? "oui" : "non", dessine ? "oui" : "non");
        }
        if (pris) acceptes++;
    }
    CHECK(desaccords == 0,
          "le filtre du texte visible accepte EXACTEMENT ce que la police dessine "
          "(%d desaccord(s))", desaccords);
    CHECK(acceptes == NS_FONT5X7_GLYPHS,
          "…soit les %d glyphes de la table (%d acceptes)", NS_FONT5X7_GLYPHS, acceptes);

    /*
     * LES MESSAGES DE REFUS SONT EUX-MÊMES DESSINABLES.
     *
     * Une explication qui s'afficherait en blancs parce qu'elle porte un accent
     * serait la plus chère des ironies : le joueur verrait un trou là où on lui
     * explique pourquoi il a un trou.
     */
    static const ns_saisie_filtre filtres[] = {
        NS_SAISIE_TEXTE, NS_SAISIE_PSEUDO, NS_SAISIE_CODE
    };
    static const ns_saisie_refus refus[] = {
        NS_SAISIE_PLEIN, NS_SAISIE_HORS_POLICE, NS_SAISIE_INTERDIT, NS_SAISIE_MAL_FORME
    };
    int illisibles = 0, phrases = 0;
    for (size_t f = 0; f < sizeof filtres / sizeof filtres[0]; ++f) {
        for (size_t r = 0; r < sizeof refus / sizeof refus[0]; ++r) {
            ns_saisie s;
            ns_saisie_init(&s, filtres[f], 8, false);
            s.refus = refus[r];
            const char *m = ns_saisie_message(&s);
            if (!m) { illisibles++; continue; }
            phrases++;
            for (const unsigned char *p = (const unsigned char *)m; *p; ++p) {
                if (*p < NS_FONT5X7_FIRST || *p >= NS_FONT5X7_FIRST + NS_FONT5X7_GLYPHS) {
                    illisibles++;
                    printf("  « %s » porte l'octet 0x%02X, que la police ne dessine pas\n",
                           m, (unsigned)*p);
                    break;
                }
            }
        }
    }
    CHECK(phrases == 12, "chaque couple filtre/refus a sa phrase (%d sur 12)", phrases);
    CHECK(illisibles == 0, "…et toutes se dessinent avec la fonte du jeu (%d fautive(s))",
          illisibles);

    ns_saisie s;
    ns_saisie_init(&s, NS_SAISIE_TEXTE, 8, false);
    CHECK(ns_saisie_message(&s) == NULL, "rien de refuse, rien a dire");

    printf("police 5x7 : %d glyphes, de 0x%02X a 0x%02X ; le filtre visible en "
           "accepte %d\n",
           NS_FONT5X7_GLYPHS, (unsigned)NS_FONT5X7_FIRST,
           (unsigned)(NS_FONT5X7_FIRST + NS_FONT5X7_GLYPHS - 1), acceptes);
}

int main(void)
{
    test_insertion();
    test_effacement_multioctet();
    test_curseur_aux_bornes();
    test_borne_haute();
    test_filtre_pseudo();
    test_filtre_code();
    test_filtre_texte();
    test_poser();
    test_masquage();
    test_utf8_malmene();
    test_police();
    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
