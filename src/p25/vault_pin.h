/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef P25_VAULT_PIN_H
#define P25_VAULT_PIN_H
#include <stddef.h>
#include <stdint.h>

#define P25_PIN_MIN 8
#define P25_PIN_MAX 32

void p25_pin_derive(const char *pin, size_t len, const uint8_t salt[16],
                    uint8_t master[32], void (*yield)(void),
                    volatile uint8_t *progress);
#endif
