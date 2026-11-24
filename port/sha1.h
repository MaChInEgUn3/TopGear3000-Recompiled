// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_SHA1_H
#define TG3000_SHA1_H

#include <stddef.h>
#include <stdint.h>

void sha1(const uint8_t* data, size_t len, uint8_t out[20]);

#endif
