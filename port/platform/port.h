// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_PORT_H
#define TG3000_PORT_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

int port_run(SDL_Renderer* r, const char* screen, unsigned long frame_limit);
void port_fatal(const char* what);
void port_input_event(const SDL_Event* event);
const uint8_t* port_sound_last_message(void);

/* Test only: buttons held by a script, OR'ed into controller 1. */
void port_input_set_test_mask(uint16_t mask);
/* Test only: hold A+B for 6 frames from press_frame (0 = never); expect_return makes
   port_run return 2 if the screen function has not returned when the frame limit ends. */
void port_set_test_options(unsigned long press_frame, bool expect_return);

#endif
