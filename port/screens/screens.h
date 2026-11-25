// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_SCREENS_H
#define TG3000_SCREENS_H

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 224

int menu_screen_active(void);
void menu_screen_draw(int which, uint32_t* fb);
const char* menu_screen_name(int which);

#endif
