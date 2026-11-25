// SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL3/SDL.h>

#include "platform.h"
#include "port.h"

static SDL_Gamepad* g_gamepad = NULL;

void port_input_event(const SDL_Event* event) {
    if (event->type == SDL_EVENT_GAMEPAD_ADDED) {
        if (g_gamepad == NULL) {
            g_gamepad = SDL_OpenGamepad(event->gdevice.which);
        }
    } else if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
        if (g_gamepad != NULL && SDL_GetGamepadID(g_gamepad) == event->gdevice.which) {
            SDL_CloseGamepad(g_gamepad);
            g_gamepad = NULL;
        }
    }
}

uint16_t input_pad1(void) {
    uint16_t bits = 0;

    int numkeys = 0;
    const bool* keys = SDL_GetKeyboardState(&numkeys);
    if (keys != NULL) {
        if (SDL_SCANCODE_Z < numkeys && keys[SDL_SCANCODE_Z]) bits |= 0x8000u;           /* B */
        if (SDL_SCANCODE_A < numkeys && keys[SDL_SCANCODE_A]) bits |= 0x4000u;           /* Y */
        if (SDL_SCANCODE_RSHIFT < numkeys && keys[SDL_SCANCODE_RSHIFT]) bits |= 0x2000u; /* Select */
        if (SDL_SCANCODE_RETURN < numkeys && keys[SDL_SCANCODE_RETURN]) bits |= 0x1000u; /* Start */
        if (SDL_SCANCODE_UP < numkeys && keys[SDL_SCANCODE_UP]) bits |= 0x0800u;         /* Up */
        if (SDL_SCANCODE_DOWN < numkeys && keys[SDL_SCANCODE_DOWN]) bits |= 0x0400u;     /* Down */
        if (SDL_SCANCODE_LEFT < numkeys && keys[SDL_SCANCODE_LEFT]) bits |= 0x0200u;     /* Left */
        if (SDL_SCANCODE_RIGHT < numkeys && keys[SDL_SCANCODE_RIGHT]) bits |= 0x0100u;   /* Right */
        if (SDL_SCANCODE_X < numkeys && keys[SDL_SCANCODE_X]) bits |= 0x0080u;           /* A */
        if (SDL_SCANCODE_S < numkeys && keys[SDL_SCANCODE_S]) bits |= 0x0040u;           /* X */
        if (SDL_SCANCODE_Q < numkeys && keys[SDL_SCANCODE_Q]) bits |= 0x0020u;           /* L */
        if (SDL_SCANCODE_W < numkeys && keys[SDL_SCANCODE_W]) bits |= 0x0010u;           /* R */
    }

    if (g_gamepad != NULL) {
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) bits |= 0x8000u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_WEST)) bits |= 0x4000u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_BACK)) bits |= 0x2000u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_START)) bits |= 0x1000u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) bits |= 0x0800u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) bits |= 0x0400u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) bits |= 0x0200u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) bits |= 0x0100u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_EAST)) bits |= 0x0080u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_NORTH)) bits |= 0x0040u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) bits |= 0x0020u;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) bits |= 0x0010u;

        const Sint16 deadzone = 16384;
        Sint16 lx = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        Sint16 ly = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        if (lx < -deadzone) bits |= 0x0200u;
        if (lx > deadzone) bits |= 0x0100u;
        if (ly < -deadzone) bits |= 0x0800u;
        if (ly > deadzone) bits |= 0x0400u;
    }

    return bits;
}

bool input_port1_data(void) {
    return true;
}

void input_multitap_pair(int pair, uint16_t* d0, uint16_t* d1, uint8_t* tail) {
    (void)pair;
    *d0 = 0;
    *d1 = 0;
    *tail = 0;
}
