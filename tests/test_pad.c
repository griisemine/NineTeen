/*
 * test_pad.c — la manette rend EXACTEMENT le masque du clavier.
 *
 * Ce que ce test attrape réellement
 * ---------------------------------
 * `room_pad.h` nomme le piège en toutes lettres, et ce fichier est la moitié qui
 * le referme. `hmask` n'est pas un détail d'implémentation : c'est l'octet que
 * `--journal-entrees` ÉCRIT, que `--rejouer` relit, que `tests/test_replay`
 * éprouve et qu'un duel en lockstep PUBLIE SUR LE RÉSEAU. Une manette qui
 * produirait un masque seulement *presque* égal à celui du clavier donnerait :
 *
 *   - des parties à la manette non reproductibles — le journal rejoué diverge du
 *     journal enregistré, sans erreur ni message ;
 *   - des duels qui se séparent en cours de route, avec pour seul symptôme
 *     « l'autre joueur ne voit pas la même chose ».
 *
 * Aucune de ces deux pannes ne se voit sur la machine qui joue. C'est pour ça
 * qu'elles se vérifient ici, sur les 32 gestes possibles, et pas à la main.
 *
 * Pourquoi un test PEUT exister alors que personne n'a de manette
 * ---------------------------------------------------------------
 * Parce que `room_pad.c` a été écrit en deux moitiés : `room_pad_sample`
 * interroge un vrai périphérique — inatteignable sans matériel — et tout ce qui
 * DÉCIDE travaille sur une `room_pad_state` qu'on remplit soi-même. Ce fichier
 * remplit donc les états à la main. Ce qu'il ne couvre pas est dit à la fin.
 */
#include "ns_core.h"
#include "room_pad.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_failures;                                                     \
            printf("ÉCHEC %s:%d — ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* Le réglage de référence : les valeurs par défaut de `room_pad_read_env`,
 * posées à la main pour que le test ne dépende pas d'un `nineteen.env` que
 * quelqu'un aurait modifié. */
static const room_pad_tuning REF = { 0.25f, 0.20f, 3.1416f };

/* Les cinq boutons, dans l'ordre de `ns_game_button`. */
#define NB_BOUTONS 5

static const char *nom_bouton(int b)
{
    switch (b) {
        case NS_GAME_UP:     return "HAUT";
        case NS_GAME_DOWN:   return "BAS";
        case NS_GAME_LEFT:   return "GAUCHE";
        case NS_GAME_RIGHT:  return "DROITE";
        case NS_GAME_ACTION: return "ACTION";
        default:             return "?";
    }
}

/* Le clavier tenu dans la position `combo`, un bit par bouton. On emploie les
 * flèches ; `room_keys_mask` accepte aussi ZQSD/WASD, éprouvé à part. */
static void clavier(bool *keys, unsigned combo)
{
    memset(keys, 0, (size_t)SDL_SCANCODE_COUNT * sizeof *keys);
    if (combo & (1u << NS_GAME_UP))     keys[SDL_SCANCODE_UP]    = true;
    if (combo & (1u << NS_GAME_DOWN))   keys[SDL_SCANCODE_DOWN]  = true;
    if (combo & (1u << NS_GAME_LEFT))   keys[SDL_SCANCODE_LEFT]  = true;
    if (combo & (1u << NS_GAME_RIGHT))  keys[SDL_SCANCODE_RIGHT] = true;
    if (combo & (1u << NS_GAME_ACTION)) keys[SDL_SCANCODE_SPACE] = true;
}

/* La même position, tenue à la CROIX directionnelle. */
static room_pad_state croix(unsigned combo)
{
    room_pad_state s;
    SDL_zero(s);
    for (int b = 0; b < 4; ++b) s.dpad[b] = (combo & (1u << b)) != 0;
    s.action = (combo & (1u << NS_GAME_ACTION)) != 0;
    return s;
}

/* La même position, tenue au STICK GAUCHE poussé à fond. `+y` vers le bas,
 * comme SDL — c'est la convention que `room_pad.h` refuse de traduire deux
 * fois, et un test qui la retraduirait ne prouverait rien. */
static room_pad_state stick_gauche(unsigned combo)
{
    room_pad_state s;
    SDL_zero(s);
    if (combo & (1u << NS_GAME_UP))     s.move_y -= 1.0f;
    if (combo & (1u << NS_GAME_DOWN))   s.move_y += 1.0f;
    if (combo & (1u << NS_GAME_LEFT))   s.move_x -= 1.0f;
    if (combo & (1u << NS_GAME_RIGHT))  s.move_x += 1.0f;
    s.action = (combo & (1u << NS_GAME_ACTION)) != 0;
    return s;
}

int main(void)
{
    bool keys[SDL_SCANCODE_COUNT];

    /* ==================================================================== */
    /* LA PROPRIÉTÉ QUI PROTÈGE LE REJEU ET LES DUELS                       */
    /* ==================================================================== */
    /*
     * Les 32 positions possibles, deux fois : à la croix et au stick. Pas un
     * échantillon — l'exhaustif, parce qu'il coûte 32 itérations et qu'un
     * échantillon laisserait passer exactement la combinaison qu'on n'a pas
     * pensé à écrire.
     *
     * HAUT+BAS et GAUCHE+DROITE en font partie : un clavier les rend tous les
     * deux si on tient les deux touches, et le masque doit donc les rendre tous
     * les deux aussi. Le stick, lui, ne PEUT pas les produire — un axe n'a
     * qu'un signe — et ces quatre positions sont traitées à part plus bas.
     */
    for (unsigned combo = 0; combo < (1u << NB_BOUTONS); ++combo) {
        clavier(keys, combo);
        const uint8_t m_clavier = room_keys_mask(keys);

        CHECK(m_clavier == (uint8_t)combo,
              "le clavier rend %02x pour la position %02x", m_clavier, combo);

        const room_pad_state d = croix(combo);
        const uint8_t m_croix = room_pad_mask(&d, &REF);
        CHECK(m_croix == m_clavier,
              "croix et clavier divergent sur la position %02x : %02x contre %02x",
              combo, m_croix, m_clavier);

        /* Le stick ne peut pas tenir deux directions opposées sur le même axe :
         * ces positions-là n'ont pas d'équivalent et sont exclues ici. */
        const bool axe_impossible =
            ((combo & (1u << NS_GAME_UP))   && (combo & (1u << NS_GAME_DOWN))) ||
            ((combo & (1u << NS_GAME_LEFT)) && (combo & (1u << NS_GAME_RIGHT)));
        if (axe_impossible) continue;

        const room_pad_state g = stick_gauche(combo);
        const uint8_t m_stick = room_pad_mask(&g, &REF);
        CHECK(m_stick == m_clavier,
              "stick et clavier divergent sur la position %02x : %02x contre %02x",
              combo, m_stick, m_clavier);
    }

    /* Les quatre positions que le stick ne peut pas tenir : on vérifie que le
     * CLAVIER les rend bien, parce que c'est lui qui les produira, et que rien
     * dans la table ne les avale. */
    {
        const unsigned opposees[] = {
            (1u << NS_GAME_UP)   | (1u << NS_GAME_DOWN),
            (1u << NS_GAME_LEFT) | (1u << NS_GAME_RIGHT),
        };
        for (size_t i = 0; i < sizeof opposees / sizeof opposees[0]; ++i) {
            clavier(keys, opposees[i]);
            CHECK(room_keys_mask(keys) == (uint8_t)opposees[i],
                  "le clavier tient deux directions opposées (%02x)", opposees[i]);
        }
    }

    /* La DIAGONALE, nommément : c'est le cas que `room_pad.c` cite en commentaire
     * — « le joueur qui vise le coin en haut à droite dans Pac-Man ». Un masque
     * par direction DOMINANTE n'en rendrait qu'un des deux bits. */
    {
        room_pad_state s;
        SDL_zero(s);
        s.move_x =  0.70f;
        s.move_y = -0.70f;
        const uint8_t m = room_pad_mask(&s, &REF);
        CHECK((m & (1u << NS_GAME_UP)) && (m & (1u << NS_GAME_RIGHT)),
              "une poussée en diagonale rend les DEUX bits (masque %02x)", m);
    }

    /* ==================================================================== */
    /* LA ZONE MORTE                                                        */
    /* ==================================================================== */
    {
        /* Au repos, tout est nul. Une manette qui rend autre chose fait tourner
         * la vue toute seule pendant qu'on lit le menu. */
        room_pad_state s;
        SDL_zero(s);
        float f = 9.0f, st = 9.0f, dy = 9.0f, dp = 9.0f;
        CHECK(room_pad_mask(&s, &REF) == 0, "manette au repos : masque nul");
        room_pad_move(&s, &REF, &f, &st);
        room_pad_look(&s, &REF, 1.0f / 120.0f, &dy, &dp);
        CHECK(f == 0.0f && st == 0.0f, "manette au repos : aucun déplacement");
        CHECK(dy == 0.0f && dp == 0.0f, "manette au repos : aucun regard");

        /* Une dérive DANS la zone morte ne bouge rien. 0,24 est sous les 0,25
         * du réglage de référence. */
        s.move_x = 0.24f;
        room_pad_move(&s, &REF, &f, &st);
        CHECK(f == 0.0f && st == 0.0f,
              "une dérive de %.2f reste dans la zone morte (strafe %.4f)",
              0.24, (double)st);
        CHECK(room_pad_mask(&s, &REF) == 0,
              "une dérive dans la zone morte n'appuie sur aucun bouton");

        /* Juste AU-DELÀ, ça bouge — mais à peine. C'est le rééchelonnement : sans
         * lui la vitesse sauterait de 0 à 0,25 d'un coup, et la marche d'escalier
         * se sent immédiatement. */
        s.move_x = 0.26f;
        room_pad_move(&s, &REF, &f, &st);
        CHECK(st > 0.0f, "juste au-delà de la zone morte, le personnage avance");
        CHECK(st < 0.05f,
              "juste au-delà de la zone morte, il avance LENTEMENT (%.4f) — sans "
              "rééchelonnement il partirait à 0,26", (double)st);
    }

    /* La zone morte est RADIALE et non par axe : deux axes chacun sous le seuil
     * mais dont la longueur le dépasse doivent passer. Une zone morte par axe
     * laisserait un carré mort au centre et refuserait ce geste. */
    {
        room_pad_state s;
        SDL_zero(s);
        s.move_x = 0.20f;
        s.move_y = -0.20f;          /* longueur 0,283 > 0,25 */
        float f = 0.0f, st = 0.0f;
        room_pad_move(&s, &REF, &f, &st);
        CHECK(f > 0.0f && st > 0.0f,
              "zone morte radiale : (0,20 ; 0,20) a une longueur de %.3f et passe "
              "(avant %.4f, côté %.4f)",
              (double)sqrt(0.08), (double)f, (double)st);
    }

    /* La diagonale à fond n'est pas plus rapide que la ligne droite. Sans
     * bornage, les deux axes à 1 donneraient une longueur de 1,414 : on
     * courrait 41 % plus vite en biais, ce que tout joueur finit par exploiter. */
    {
        room_pad_state droit, biais;
        SDL_zero(droit); SDL_zero(biais);
        droit.move_y = -1.0f;
        biais.move_x =  1.0f; biais.move_y = -1.0f;
        float fd = 0.0f, sd = 0.0f, fb = 0.0f, sb = 0.0f;
        room_pad_move(&droit, &REF, &fd, &sd);
        room_pad_move(&biais, &REF, &fb, &sb);
        const float len_droit = sqrtf(fd * fd + sd * sd);
        const float len_biais = sqrtf(fb * fb + sb * sb);
        CHECK(fabsf(len_droit - 1.0f) < 1e-4f,
              "à fond dans l'axe, la longueur vaut 1 (%.4f)", (double)len_droit);
        CHECK(len_biais <= len_droit + 1e-4f,
              "à fond en biais, on ne va pas plus vite (%.4f contre %.4f)",
              (double)len_biais, (double)len_droit);
    }

    /* ==================================================================== */
    /* LES SIGNES — la faute qui fait reculer quand on pousse               */
    /* ==================================================================== */
    {
        room_pad_state s;
        SDL_zero(s);
        float f = 0.0f, st = 0.0f;

        s.move_y = -1.0f;                    /* SDL : vers le HAUT */
        room_pad_move(&s, &REF, &f, &st);
        CHECK(f > 0.0f, "pousser le stick vers le haut fait AVANCER (%.3f)", (double)f);

        SDL_zero(s);
        s.move_x = 1.0f;
        room_pad_move(&s, &REF, &f, &st);
        CHECK(st > 0.0f, "pousser le stick à droite déporte à DROITE (%.3f)", (double)st);

        float dy = 0.0f, dp = 0.0f;
        SDL_zero(s);
        s.look_x = 1.0f;
        room_pad_look(&s, &REF, 1.0f / 120.0f, &dy, &dp);
        CHECK(dy > 0.0f, "pousser le stick droit à droite tourne la vue à droite");

        SDL_zero(s);
        s.look_y = 1.0f;                     /* SDL : vers le BAS */
        room_pad_look(&s, &REF, 1.0f / 120.0f, &dy, &dp);
        CHECK(dp < 0.0f,
              "pousser le stick droit vers le bas BAISSE les yeux (%.4f) — même "
              "convention que la souris", (double)dp);
    }

    /* Le regard est une VITESSE : deux fois le pas, deux fois l'angle. Un regard
     * exprimé par image tournerait deux fois plus vite sur une machine deux fois
     * plus rapide. */
    {
        room_pad_state s;
        SDL_zero(s);
        s.look_x = 1.0f;
        float a = 0.0f, b = 0.0f, ignore = 0.0f;
        room_pad_look(&s, &REF, 1.0f / 120.0f, &a, &ignore);
        room_pad_look(&s, &REF, 2.0f / 120.0f, &b, &ignore);
        CHECK(fabsf(b - 2.0f * a) < 1e-5f,
              "le regard suit le pas de temps (%.6f puis %.6f)", (double)a, (double)b);
    }

    /* ==================================================================== */
    /* AUCUNE MANETTE BRANCHÉE — le cas ordinaire                           */
    /* ==================================================================== */
    {
        /* C'est l'état de toutes les machines qui construisent ce dépôt, et de
         * la plupart des joueurs : `room_pad_sample(NULL, …)` doit rendre un
         * repos complet, sinon le jeu dérive tout seul sans manette. */
        room_pad_state s;
        memset(&s, 0x5A, sizeof s);          /* sali exprès */
        room_pad_sample(NULL, &s);
        CHECK(room_pad_mask(&s, &REF) == 0,
              "sans manette, le masque est nul");
        CHECK(s.move_x == 0.0f && s.move_y == 0.0f
           && s.look_x == 0.0f && s.look_y == 0.0f,
              "sans manette, les quatre axes sont au repos");
        CHECK(!s.action && !s.menu, "sans manette, aucun bouton n'est tenu");
        for (int b = 0; b < 4; ++b) {
            CHECK(!s.dpad[b], "sans manette, la croix est au repos (%s)", nom_bouton(b));
        }

        /* Et le masque du clavier reste seul maître : l'OU de `main.c` avec un
         * masque nul doit rendre le clavier inchangé. */
        clavier(keys, (1u << NS_GAME_LEFT) | (1u << NS_GAME_ACTION));
        const uint8_t seul = room_keys_mask(keys);
        CHECK((uint8_t)(seul | room_pad_mask(&s, &REF)) == seul,
              "sans manette, l'octet du clavier passe intact");
    }

    /* Les pointeurs nuls : `main.c` appelle ces fonctions à chaque image, et une
     * manette débranchée en cours de partie ne doit pas coûter un plantage. */
    {
        float a = 9.0f, b = 9.0f;
        CHECK(room_pad_mask(NULL, &REF) == 0, "masque d'un état nul");
        CHECK(room_pad_mask(NULL, NULL) == 0, "masque sans état ni réglage");
        room_pad_move(NULL, NULL, &a, &b);
        CHECK(a == 0.0f && b == 0.0f, "déplacement sans état : nul");
        a = b = 9.0f;
        room_pad_look(NULL, NULL, 1.0f / 120.0f, &a, &b);
        CHECK(a == 0.0f && b == 0.0f, "regard sans état : nul");
    }

    /* ==================================================================== */
    /* LES RÉGLAGES SONT BORNÉS                                             */
    /* ==================================================================== */
    /*
     * `room_pad_read_env` borne ce qu'il lit. Une zone morte à zéro rend le
     * personnage ivre ; une zone morte à 1 rend la manette inerte. On ne vérifie
     * pas les valeurs exactes — elles se règlent — mais qu'elles restent dans un
     * domaine où la manette fonctionne encore.
     */
    {
        room_pad_tuning t;
        room_pad_read_env(&t);
        CHECK(t.deadzone >= 0.02f && t.deadzone <= 0.80f,
              "la zone morte lue est bornée (%.3f)", (double)t.deadzone);
        CHECK(t.look_deadzone >= 0.02f && t.look_deadzone <= 0.80f,
              "la zone morte du regard est bornée (%.3f)", (double)t.look_deadzone);
        CHECK(t.look_speed > 0.0f,
              "la vitesse de regard est positive (%.3f rad/s)", (double)t.look_speed);
    }

    /*
     * CE QUE CE TEST NE COUVRE PAS, écrit plutôt que découvert :
     *   - `room_pad_sample` sur un VRAI périphérique. Aucune machine de
     *     construction n'a de manette ; la correspondance entre les axes SDL et
     *     les champs de `room_pad_state` reste à éprouver à la main, une fois,
     *     par quelqu'un qui en branche une ;
     *   - le confort. Une zone morte peut être juste et désagréable, une vitesse
     *     de regard correcte et trop lente. Cela se règle en jouant, et c'est
     *     précisément pourquoi les trois valeurs sont dans `nineteen.env` ;
     *   - le branchement à chaud, qui est de l'événementiel SDL dans `main.c`.
     */
    printf("%d vérification(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
