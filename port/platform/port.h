// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_PORT_H
#define TG3000_PORT_H

#include <SDL3/SDL.h>
#include <stdint.h>

int port_run(SDL_Renderer* r, const char* screen, unsigned long frame_limit);
void port_fatal(const char* what);
void port_input_event(const SDL_Event* event);
const uint8_t* port_sound_last_message(void);

#endif
