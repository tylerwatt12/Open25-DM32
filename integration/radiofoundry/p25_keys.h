/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef P25_KEYS_H
#define P25_KEYS_H
#include <stddef.h>
#include "key_vault.h"
#include "vault_pin.h"

typedef enum {
    P25_PIN_IDLE, P25_PIN_NEW, P25_PIN_ENTER, P25_PIN_WORKING,
    P25_PIN_READY, P25_PIN_FAILED, P25_PIN_UNLOCKED, P25_PIN_NO_SPACE, P25_PIN_NO_ID
} p25_pin_status;

p25_pin_status p25_keys_pin_status(void);
void p25_keys_pin_prepare(void);
uint8_t p25_keys_pin_progress(void);
int p25_keys_pin_submit(const char *pin, size_t len);
void p25_keys_pin_run(void (*yield)(void));

int p25_keys_command(const uint8_t *req, uint16_t len, uint8_t *out, size_t cap);
const p25_key_entry *p25_keys_get(unsigned slot);
int p25_keys_tx_ready(void);
int p25_keys_next_mi(uint8_t mi[9]);
#endif
