// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_ROM_H
#define TG3000_ROM_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ROM_OK,
    ROM_NOT_FOUND,
    ROM_READ_ERROR,
    ROM_BAD_SIZE,
    ROM_BAD_HASH
} rom_status;

rom_status rom_load(const char* path, uint8_t** out_data, size_t* out_size);
const char* rom_status_text(rom_status status);
void rom_free(uint8_t* data);

#endif
