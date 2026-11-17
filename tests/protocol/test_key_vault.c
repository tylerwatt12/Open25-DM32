/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "key_vault.h"
#include "vault_pin.h"
#include "secure_frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independently generated using cryptography's AES-SIV and AES-CMAC. */
static const uint8_t expected[] = {
    0x07, 0xab, 0x35, 0x73, 0x9b, 0x4f, 0x18, 0x20, 0xe2, 0xac, 0x79, 0x5d,
    0x4c, 0xb7, 0x51, 0x8d, 0x28, 0xef, 0x35, 0xa9, 0xc9, 0x6b, 0x50, 0x90,
    0x06, 0xa4, 0xf8, 0xbf, 0xea, 0x05, 0x0e, 0x03, 0x09, 0x70, 0x05, 0x94,
    0x27, 0x77, 0x97, 0x60, 0x06, 0xf4, 0x26, 0xa5, 0xd8, 0x57, 0x54, 0xd5,
    0x74, 0x17, 0xc2,
};

int main(void)
{
    uint8_t master[32], wrong[32] = {0}, salt[16] = {1};
    static const uint8_t reference_master[32] = {0x8b, 0x01, 0x58, 0x58, 0x28, 0x56, 0xa3, 0x49, 0xa3, 0x26, 0x7c, 0x34, 0x41, 0x6e, 0x38, 0x68, 0x65, 0x96, 0xdc, 0x00, 0x80, 0x77, 0xe3, 0xe6, 0x70, 0xfc, 0xf1, 0xf4, 0x54, 0x79, 0xa6, 0x0e};
    p25_pin_derive("24681357", 8, salt, master, NULL, NULL);
    assert(!memcmp(master, reference_master, sizeof master));
    p25_vault_image image;
    p25_key_entry key = {.algorithm=P25_ALG_AES256, .kid={0x34, 0x12}}, read;
    for (unsigned i = 0; i < 32; i++) {
        master[i] = (uint8_t)i;
        key.data[i] = (uint8_t)(32 + i);
    }
    p25_vault_create(&image, salt, master);
    assert(p25_vault_matches(&image, master) && !p25_vault_matches(&image, wrong));
    assert(!p25_vault_open(&image, master, 3, &read));
    assert(p25_vault_seal(&image, master, 3, &key));
    assert(!memcmp(image.slots[2].tag, expected, 16));
    assert(!memcmp(image.slots[2].sealed, expected + 16, sizeof key));
    assert(!p25_vault_open(&image, wrong, 3, &read));
    assert(p25_vault_open(&image, master, 3, &read) && !memcmp(&key, &read, sizeof key));
    image.slots[1] = image.slots[2];
    assert(!p25_vault_open(&image, master, 2, &read));
    image.slots[2].sealed[0] ^= 1;
    assert(!p25_vault_open(&image, master, 3, &read));
    for (unsigned i = 0; i < sizeof read; i++) assert(((const uint8_t *)&read)[i] == 0);
    puts("PASS P25 vault AES-SIV reference, wrong PIN-derived master, slot binding and tamper rejection");
    return 0;
}
