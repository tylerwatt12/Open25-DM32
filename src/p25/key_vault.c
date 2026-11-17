/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "key_vault.h"
#include "crypto_voice.h"
#include "p25_runtime.h"

static const uint8_t magic[4] = {'P', '2', 'K', 1};

void p25_vault_erase(void *data, unsigned length)
{
    volatile uint8_t *bytes = data;
    while (length--) *bytes++ = 0;
}

static int equal(const uint8_t *a, const uint8_t *b, unsigned n)
{
    unsigned difference = 0;
    for (unsigned i = 0; i < n; i++) difference |= a[i] ^ b[i];
    return difference == 0;
}

static void xor_bytes(uint8_t *dst, const uint8_t *src, unsigned count)
{
    for (unsigned i = 0; i < count; i++) dst[i] ^= src[i];
}

static void double_block(uint8_t b[16])
{
    unsigned carry = b[0] >> 7;
    for (unsigned i = 0; i < 15; i++) b[i] = (uint8_t)((b[i] << 1) | (b[i + 1] >> 7));
    b[15] = (uint8_t)((b[15] << 1) ^ (0x87u & (0u - carry)));
}

/* SP 800-38B AES-CMAC. */
static void cmac(uint8_t tag[16], const uint8_t key[32], const uint8_t *data, unsigned length)
{
    uint8_t subkey[16] = {0};
    p25_aes256_block(subkey, key);
    double_block(subkey);
    memset(tag, 0, 16);
    while (length > 16) {
        xor_bytes(tag, data, 16);
        p25_aes256_block(tag, key);
        length -= 16;
        data += 16;
    }
    xor_bytes(tag, data, length);
    if (length < 16) {
        tag[length] ^= 0x80;
        double_block(subkey);
    }
    xor_bytes(tag, subkey, 16);
    p25_aes256_block(tag, key);
    p25_vault_erase(subkey, sizeof subkey);
}

/* SP 800-108 CMAC counter KDF, 32-bit counter and length, distinct labels. */
static void subkeys(const uint8_t master[32], uint8_t label, uint8_t result[32])
{
    uint8_t message[13] = {0, 0, 0, 1, 'P', '2', 'K', label, 0, 0, 0, 1, 0};
    cmac(result, master, message, sizeof message);
    message[3] = 2;
    cmac(result + 16, master, message, sizeof message);
}

static void verifier(uint8_t tag[16], const uint8_t salt[16], const uint8_t master[32])
{
    uint8_t key[32];
    subkeys(master, 'V', key);
    cmac(tag, key, salt, 16);
    p25_vault_erase(key, sizeof key);
}

int p25_vault_format(const p25_vault_image *image)
{
    return equal(image->magic, magic, sizeof magic);
}

void p25_vault_create(p25_vault_image *image, const uint8_t salt[16], const uint8_t master[32])
{
    memset(image, 0, sizeof *image);
    memcpy(image->magic, magic, sizeof magic);
    memcpy(image->salt, salt, 16);
    verifier(image->verifier, salt, master);
}

int p25_vault_matches(const p25_vault_image *image, const uint8_t master[32])
{
    if (!p25_vault_format(image)) return 0;
    uint8_t tag[16];
    verifier(tag, image->salt, master);
    return equal(image->verifier, tag, sizeof tag);
}

/* RFC 5297 S2V. One associated-data string binds the format and slot. */
static void s2v(uint8_t tag[16], unsigned slot, const uint8_t key[32],
                const p25_key_entry *entry)
{
    const uint8_t zero[16] = {0};
    const uint8_t aad[] = {'P', '2', 'K', 1, (uint8_t)slot};
    uint8_t d[16], associated[16], message[sizeof *entry];
    cmac(d, key, zero, sizeof zero);
    double_block(d);
    cmac(associated, key, aad, sizeof aad);
    xor_bytes(d, associated, 16);
    memcpy(message, entry, sizeof message);
    xor_bytes(message + sizeof message - 16, d, 16);
    cmac(tag, key, message, sizeof message);
    p25_vault_erase(message, sizeof message);
}

static void crypt(uint8_t *bytes, const uint8_t tag[16], const uint8_t key[32])
{
    uint8_t counter[16], stream[16];
    memcpy(counter, tag, 16);
    counter[8] &= 0x7f;
    counter[12] &= 0x7f;
    for (unsigned i = 0; i < sizeof(p25_key_entry); i += 16) {
        memcpy(stream, counter, 16);
        p25_aes256_block(stream, key);
        unsigned left = sizeof(p25_key_entry) - i;
        xor_bytes(bytes + i, stream, left < 16 ? left : 16);
        for (unsigned j = 16; j && ++counter[j - 1] == 0; j--) {}
    }
    p25_vault_erase(stream, sizeof stream);
}

int p25_vault_seal(p25_vault_image *image, const uint8_t master[32], unsigned slot,
                   const p25_key_entry *entry)
{
    if (!slot || slot > P25_KEY_SLOTS || !p25_crypto_key_bytes(entry->algorithm)) return 0;
    unsigned i = slot - 1;
    uint8_t key[32];
    subkeys(master, 'M', key);
    s2v(image->slots[i].tag, slot, key, entry);
    subkeys(master, 'E', key);
    memcpy(image->slots[i].sealed, entry, sizeof *entry);
    crypt(image->slots[i].sealed, image->slots[i].tag, key);
    p25_vault_erase(key, sizeof key);
    return 1;
}

int p25_vault_open(const p25_vault_image *image, const uint8_t master[32], unsigned slot,
                   p25_key_entry *entry)
{
    memset(entry, 0, sizeof *entry);
    if (!slot || slot > P25_KEY_SLOTS) return 0;
    unsigned i = slot - 1;
    uint8_t key[32], tag[16];
    subkeys(master, 'E', key);
    memcpy(entry, image->slots[i].sealed, sizeof *entry);
    crypt((uint8_t *)entry, image->slots[i].tag, key);
    subkeys(master, 'M', key);
    s2v(tag, slot, key, entry);
    p25_vault_erase(key, sizeof key);
    if (equal(tag, image->slots[i].tag, 16) && p25_crypto_key_bytes(entry->algorithm)) return 1;
    p25_vault_erase(entry, sizeof *entry);
    return 0;
}

void p25_vault_mi_seed(uint8_t seed[8], const p25_vault_image *image,
                       const uint8_t master[32])
{
    uint8_t key[32], block[16];
    subkeys(master, 'I', key);
    cmac(block, key, image->salt, sizeof image->salt);
    memcpy(seed, block, 8);
    p25_vault_erase(block, sizeof block);
    p25_vault_erase(key, sizeof key);
}

void p25_vault_mi_tag(uint8_t tag[16], const uint8_t next[8],
                      const uint8_t master[32])
{
    uint8_t key[32];
    subkeys(master, 'R', key);
    cmac(tag, key, next, 8);
    p25_vault_erase(key, sizeof key);
}
