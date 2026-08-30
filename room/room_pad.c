#include "room_pad.h"

#include "ns_core.h"
#include "ns_env.h"
#include "ns_math.h"

#include <math.h>

/* --------------------------------------------------------------------------
 * Les réglages
 * -------------------------------------------------------------------------- */

void room_pad_read_env(room_pad_tuning *t)
{
    if (!t) return;
    /*
     * 0,25 est la valeur que SDL emploie lui-même pour ses propres seuils
     * (`SDL_GAMEPAD_AXIS_MAX / 4`), et elle est mesurée sur du matériel réel :
     * une manette usée dérive couramment jusqu'à 0,15 au repos. Descendre sous
     * 0,10 laisse passer cette dérive ; monter au-delà de 0,40 mange la moitié
     * de la course utile.
     */
    t->deadzone      = ns_clampf(ns_env_float("manette.zoneMorte", 0.25f), 0.02f, 0.80f);
    t->look_deadzone = ns_clampf(ns_env_float("manette.vueZoneMorte", 0.20f), 0.02f, 0.80f);
    /* Le fichier parle en DEGRÉS par seconde, comme tous les angles qu'il porte ;
     * le reste du moteur travaille en radians. 180 °/s fait un demi-tour en une
     * seconde à fond de course, ce qui est la cadence d'un jeu à la première
     * personne à la manette. */
    t->look_speed = ns_clampf(ns_env_float("manette.vueSensibilite", 180.0f), 10.0f, 720.0f)
                  * NS_DEG2RAD;
}

/* --------------------------------------------------------------------------
 * Le masque de boutons — l'octet que le réseau et le journal transportent
 * -------------------------------------------------------------------------- */

uint8_t room_pad_mask(const room_pad_state *s, const room_pad_tuning *t)
{
    if (!s || !t) return 0;

    uint8_t m = 0;

    /* La croix, d'abord : elle est déjà binaire, il n'y a rien à seuiller. */
    for (int b = 0; b < 4; ++b) {
        if (s->dpad[b]) m |= (uint8_t)(1u << b);
    }

    /*
     * Puis le stick, par AXE et non par direction dominante.
     *
     * Par direction dominante, une poussée en diagonale ne rendrait qu'un seul
     * bit — et le joueur qui vise le coin en haut à droite dans Pac-Man
     * n'obtiendrait jamais les deux. Le clavier, lui, rend bien les deux quand
     * on tient deux flèches : c'est cette équivalence-là qu'on doit tenir, et
     * c'est elle que `tests/test_pad.c` vérifie.
     */
    if (s->move_y < -t->deadzone) m |= (uint8_t)(1u << NS_GAME_UP);
    if (s->move_y >  t->deadzone) m |= (uint8_t)(1u << NS_GAME_DOWN);
    if (s->move_x < -t->deadzone) m |= (uint8_t)(1u << NS_GAME_LEFT);
    if (s->move_x >  t->deadzone) m |= (uint8_t)(1u << NS_GAME_RIGHT);

    if (s->action) m |= (uint8_t)(1u << NS_GAME_ACTION);
    return m;
}

uint8_t room_keys_mask(const bool *keys)
{
    if (!keys) return 0;
    uint8_t m = 0;
    /*
     * Les SCANCODES désignent une position physique : `SDL_SCANCODE_W` est la
     * touche qui porte un W en QWERTY et un Z en AZERTY. ZQSD et WASD sont donc
     * la même chose, et il n'y a rien à ajouter pour l'un ou pour l'autre.
     */
    if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) m |= 1u << NS_GAME_UP;
    if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) m |= 1u << NS_GAME_DOWN;
    if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) m |= 1u << NS_GAME_LEFT;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) m |= 1u << NS_GAME_RIGHT;
    if (keys[SDL_SCANCODE_SPACE]) m |= 1u << NS_GAME_ACTION;
    return m;
}

/* --------------------------------------------------------------------------
 * Les deux sticks, en analogique
 * -------------------------------------------------------------------------- */

/*
 * Zone morte RADIALE, puis rééchelonnement de ce qui reste sur [0, 1].
 *
 * Radiale et non par axe : une zone morte appliquée à x et à y séparément
 * laisse un CARRÉ mort au centre, et le stick poussé en diagonale sort de la
 * zone bien avant de sortir dans l'axe — la manette répond alors plus tôt en
 * diagonale qu'en ligne droite, ce qui se sent sans qu'on sache le nommer.
 *
 * Le rééchelonnement est ce qui évite la marche d'escalier : sans lui, la
 * vitesse saute de 0 à `zone morte` dès qu'on quitte le repos.
 */
static void stick(float x, float y, float dz, float *out_x, float *out_y)
{
    const float mag = sqrtf(x * x + y * y);
    if (mag <= dz || mag <= 1e-6f) {
        *out_x = *out_y = 0.0f;
        return;
    }
    /* Le stick peut dépasser 1 en diagonale (les deux axes à fond) : on borne,
     * sinon la course diagonale serait 41 % plus rapide que la course droite. */
    const float clamped = ns_minf(mag, 1.0f);
    const float scaled  = (clamped - dz) / (1.0f - dz);
    const float k = scaled / mag;
    *out_x = x * k;
    *out_y = y * k;
}

void room_pad_move(const room_pad_state *s, const room_pad_tuning *t,
                   float *forward, float *strafe)
{
    float fx = 0.0f, fy = 0.0f;
    if (s && t) stick(s->move_x, s->move_y, t->deadzone, &fx, &fy);
    /* SDL pousse +y vers le BAS ; avancer, c'est pousser vers le HAUT. */
    if (forward) *forward = -fy;
    if (strafe)  *strafe  =  fx;
}

void room_pad_look(const room_pad_state *s, const room_pad_tuning *t, float dt,
                   float *dyaw, float *dpitch)
{
    float lx = 0.0f, ly = 0.0f;
    if (s && t) stick(s->look_x, s->look_y, t->look_deadzone, &lx, &ly);
    const float rate = (t ? t->look_speed : 0.0f) * dt;
    if (dyaw)   *dyaw   =  lx * rate;
    /* Pousser le stick vers le bas fait baisser les yeux : même convention que
     * la souris, dont `room_camera` soustrait déjà le déplacement vertical. */
    if (dpitch) *dpitch = -ly * rate;
}

/* --------------------------------------------------------------------------
 * Le côté SDL
 * -------------------------------------------------------------------------- */

/* SDL rend les axes sur un entier signé 16 bits. La borne NÉGATIVE vaut −32768
 * et la positive 32767 : diviser par 32767 ferait sortir −1,00003 à fond de
 * course vers le haut, que la zone morte laisserait passer et qui deviendrait
 * une vitesse supérieure à 1. On borne. */
static float axis(SDL_Gamepad *g, SDL_GamepadAxis a)
{
    const float v = (float)SDL_GetGamepadAxis(g, a) / 32767.0f;
    return ns_clampf(v, -1.0f, 1.0f);
}

void room_pad_sample(SDL_Gamepad *g, room_pad_state *out)
{
    if (!out) return;
    SDL_zerop(out);
    if (!g) return;

    out->dpad[NS_GAME_UP]    = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP);
    out->dpad[NS_GAME_DOWN]  = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    out->dpad[NS_GAME_LEFT]  = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    out->dpad[NS_GAME_RIGHT] = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

    /*
     * SUD et EST, pas A et B.
     *
     * SDL nomme les boutons par leur POSITION depuis SDL3, précisément parce
     * qu'une manette Nintendo porte le A là où une manette Xbox porte le B. Le
     * bouton du bas est celui sous le pouce au repos sur les deux, et c'est
     * celui qu'un joueur d'arcade cherche.
     */
    out->action = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH);
    out->menu   = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START);

    /* Sauter, s'accroupir, courir — les trois que le clavier a et que la
     * manette n'avait pas. Le bouton du BAS reste l'action : c'est celui du
     * manche d'une borne, et le deplacer pour y mettre le saut ferait
     * apprendre deux choses au lieu d'une. */
    out->jump   = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_WEST);
    out->crouch = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST);
    out->run    = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);

    out->move_x = axis(g, SDL_GAMEPAD_AXIS_LEFTX);
    out->move_y = axis(g, SDL_GAMEPAD_AXIS_LEFTY);
    out->look_x = axis(g, SDL_GAMEPAD_AXIS_RIGHTX);
    out->look_y = axis(g, SDL_GAMEPAD_AXIS_RIGHTY);
}
