/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef P25_KEY_VAULT_H
#define P25_KEY_VAULT_H
#include <stdint.h>

enum { P25_KEY_SLOTS = 8, P25_VAULT_SALT = 16, P25_VAULT_MASTER = 32 };

typedef struct {
    uint8_t algorithm;
    uint8_t kid[2]; /* little-endian */
    uint8_t data[32];
} p25_key_entry;

typedef struct {
    uint8_t magic[4];
    uint8_t salt[16];
    uint8_t verifier[16];
    struct {
        uint8_t tag[16];
        uint8_t sealed[sizeof(p25_key_entry)];
    } slots[P25_KEY_SLOTS];
} p25_vault_image;

void p25_vault_create(p25_vault_image *image, const uint8_t salt[16], const uint8_t master[32]);
int p25_vault_matches(const p25_vault_image *image, const uint8_t master[32]);
int p25_vault_format(const p25_vault_image *image);
int p25_vault_seal(p25_vault_image *image, const uint8_t master[32], unsigned slot,
                   const p25_key_entry *entry);
int p25_vault_open(const p25_vault_image *image, const uint8_t master[32], unsigned slot,
                   p25_key_entry *entry);
/* Separate authentication domain for the persistent TX MI reservation. */
void p25_vault_mi_seed(uint8_t seed[8], const p25_vault_image *image,
                       const uint8_t master[32]);
void p25_vault_mi_tag(uint8_t tag[16], const uint8_t next[8],
                      const uint8_t master[32]);
void p25_vault_erase(void *data, unsigned length);
#endif
