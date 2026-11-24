// SPDX-License-Identifier: GPL-3.0-or-later

#include "rom.h"
#include "sha1.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TG3000_ROM_SIZE = 1048576 };

static const uint8_t TG3000_EXPECTED_SHA1[20] = {
    0x05, 0x8a, 0xad, 0xf1, 0xee, 0x71, 0x9c, 0xb9, 0xe0, 0xc7,
    0x33, 0x3e, 0x66, 0x57, 0x97, 0xf9, 0x90, 0xa6, 0x38, 0x1d
};

rom_status rom_load(const char* path, uint8_t** out_data, size_t* out_size) {
    if (out_data == NULL || out_size == NULL || path == NULL) {
        return ROM_READ_ERROR;
    }
    *out_data = NULL;
    *out_size = 0;

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return (errno == ENOENT) ? ROM_NOT_FOUND : ROM_READ_ERROR;
    }

    uint8_t* buf = (uint8_t*)malloc(TG3000_ROM_SIZE);
    if (buf == NULL) {
        fclose(f);
        return ROM_READ_ERROR;
    }

    size_t total_read = 0;
    while (total_read < TG3000_ROM_SIZE) {
        size_t n = fread(buf + total_read, 1, TG3000_ROM_SIZE - total_read, f);
        if (n == 0) {
            break;
        }
        total_read += n;
    }

    if (ferror(f)) {
        free(buf);
        fclose(f);
        return ROM_READ_ERROR;
    }

    int extra = fgetc(f);
    if (ferror(f)) {
        free(buf);
        fclose(f);
        return ROM_READ_ERROR;
    }
    fclose(f);

    if (total_read != TG3000_ROM_SIZE || extra != EOF) {
        free(buf);
        return ROM_BAD_SIZE;
    }

    uint8_t digest[20];
    sha1(buf, TG3000_ROM_SIZE, digest);
    if (memcmp(digest, TG3000_EXPECTED_SHA1, sizeof(digest)) != 0) {
        free(buf);
        return ROM_BAD_HASH;
    }

    *out_data = buf;
    *out_size = TG3000_ROM_SIZE;
    return ROM_OK;
}

const char* rom_status_text(rom_status status) {
    switch (status) {
        case ROM_OK:
            return "ROM verified";
        case ROM_NOT_FOUND:
            return "ROM file not found";
        case ROM_READ_ERROR:
            return "Failed to read ROM file";
        case ROM_BAD_SIZE:
            return "Invalid ROM size (expected exactly 1048576 bytes, no copier header)";
        case ROM_BAD_HASH:
            return "Invalid ROM SHA-1 (expected Top Gear 3000 (USA))";
        default:
            return "Unknown ROM error";
    }
}

void rom_free(uint8_t* data) {
    free(data);
}
