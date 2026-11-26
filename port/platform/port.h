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
/* Test only: report a standard controller in port 2 (e.g. during --movie replay). */
void port_input_set_pad2_present(bool present);
/* Test only: hold A+B for 6 frames from press_frame (0 = never); expect_return makes
   port_run return 2 if the screen function has not returned when the frame limit ends. */
void port_set_test_options(unsigned long press_frame, bool expect_return);
/* Test only: save the 256x224 framebuffer as a 24-bit BMP when frame_count reaches frame. */
void port_set_shot(unsigned long frame, const char* path);
/* Test only: start the game from a saved 128 KB game memory instead of zeroed memory. */
void port_set_start_wram(const char* path);
/* Test only: replay controller-1 input from a bench movie file with frame offset. */
void port_set_movie(const char* path, long offset);
/* Test only: switch movie frame offset to offset from port frame onwards. */
void port_set_movie_offset_from(unsigned long frame, long offset);
/* Test only: write the 128 KB game memory to path at frame (before frame code runs). */
void port_set_wram_out(unsigned long frame, const char* path);
/* Test only: disable 60.0988 fps frame pacing when fast is true. */
void port_set_fast(bool fast);
/* The port's frame counter (frames since port_run started). */
unsigned long port_frame_number(void);
/* Test only: at port frame `frame`, the menus' sound effect request `id` (as the menu code
   writes it). */
void port_add_test_effect(unsigned long frame, uint8_t id);
/* Print a summary of the currently loaded sound set (slots, sizes, total PCM samples). */
void port_sound_print_set_summary(void);
/* Two-task split state: whether the split is active, and which task half (0=top, 1=bottom) is running. */
bool port_split_active(void);
int port_current_task_half(void);

#endif
