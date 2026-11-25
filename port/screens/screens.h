// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_SCREENS_H
#define TG3000_SCREENS_H

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 224

bool title_card_active(void);
void title_card_draw(uint32_t* fb);

#endif
