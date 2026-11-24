// SPDX-License-Identifier: GPL-3.0-or-later

#include "sha1.h"

#include <string.h>

static uint32_t rotl32(uint32_t v, unsigned int n) {
    return (v << n) | (v >> (32U - n));
}

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static void sha1_block(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    for (unsigned int i = 0; i < 16; ++i) {
        w[i] = read_be32(block + i * 4U);
    }
    for (unsigned int i = 16; i < 80; ++i) {
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1U);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (unsigned int i = 0; i < 80; ++i) {
        uint32_t f;
        uint32_t k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        uint32_t temp = rotl32(a, 5U) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl32(b, 30U);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint32_t state[5] = {
        0x67452301U,
        0xEFCDAB89U,
        0x98BADCFEU,
        0x10325476U,
        0xC3D2E1F0U
    };

    size_t offset = 0;
    while (len - offset >= 64U) {
        sha1_block(state, data + offset);
        offset += 64U;
    }

    uint8_t tail[128];
    memset(tail, 0, sizeof(tail));

    size_t rem = len - offset;
    if (rem > 0U && data != NULL) {
        memcpy(tail, data + offset, rem);
    }
    tail[rem] = 0x80U;

    size_t total_blocks_bytes = (rem + 1U + 8U <= 64U) ? 64U : 128U;
    uint64_t bit_len = (uint64_t)len * 8ULL;
    write_be32(tail + total_blocks_bytes - 8U, (uint32_t)(bit_len >> 32));
    write_be32(tail + total_blocks_bytes - 4U, (uint32_t)(bit_len & 0xFFFFFFFFULL));

    sha1_block(state, tail);
    if (total_blocks_bytes == 128U) {
        sha1_block(state, tail + 64U);
    }

    for (unsigned int i = 0; i < 5; ++i) {
        write_be32(out + i * 4U, state[i]);
    }
}
