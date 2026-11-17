/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crypto_voice.h"
#include "secure_frame.h"
#include "p25_runtime.h"

/* FIPS 197 forward substitution table. */
static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static uint8_t xtime(uint8_t value)
{
    return (uint8_t)((value << 1) ^ (0x1b & (0u - (value >> 7))));
}

static void expand_key(uint8_t schedule[240], const uint8_t key[32])
{
    uint8_t rcon = 1;
    memcpy(schedule, key, 32);
    for (unsigned i = 32; i < 240; i += 4) {
        uint8_t word[4];
        memcpy(word, schedule + i - 4, 4);
        if (i % 32 == 0) {
            uint8_t first = word[0];
            for (unsigned j = 0; j < 3; j++) word[j] = sbox[word[j + 1]];
            word[3] = sbox[first];
            word[0] ^= rcon;
            rcon = xtime(rcon);
        } else if (i % 32 == 16) {
            for (unsigned j = 0; j < 4; j++) word[j] = sbox[word[j]];
        }
        for (unsigned j = 0; j < 4; j++) schedule[i + j] = schedule[i + j - 32] ^ word[j];
    }
}

static void encrypt_block(uint8_t block[16], const uint8_t schedule[240])
{
    for (unsigned i = 0; i < 16; i++) block[i] ^= schedule[i];
    for (unsigned round = 1; round <= 14; round++) {
        uint8_t shifted[16];
        for (unsigned col = 0; col < 4; col++)
            for (unsigned row = 0; row < 4; row++)
                shifted[4 * col + row] = sbox[block[4 * ((col + row) % 4) + row]];
        if (round < 14) {
            for (unsigned col = 0; col < 4; col++) {
                uint8_t *s = shifted + 4 * col;
                uint8_t sum = s[0] ^ s[1] ^ s[2] ^ s[3];
                uint8_t first = s[0];
                for (unsigned i = 0; i < 3; i++) s[i] ^= sum ^ xtime(s[i] ^ s[i + 1]);
                s[3] ^= sum ^ xtime(s[3] ^ first);
            }
        }
        for (unsigned i = 0; i < 16; i++) block[i] = shifted[i] ^ schedule[16 * round + i];
    }
}

void p25_aes256_block(uint8_t block[16], const uint8_t key[32])
{
    uint8_t schedule[240];
    expand_key(schedule, key);
    encrypt_block(block, schedule);
    volatile uint8_t *scratch = schedule;
    for (unsigned i = 0; i < sizeof schedule; i++) scratch[i] = 0;
}

void p25_aes_next_mi(uint8_t mi[9])
{
    uint64_t state = 0;
    for (unsigned i = 0; i < 8; i++) state = (state << 8) | mi[i];
    for (unsigned i = 0; i < 64; i++) {
        uint64_t feedback = (state >> 63) ^ (state >> 61) ^ (state >> 45) ^
                            (state >> 37) ^ (state >> 26) ^ (state >> 14);
        state = (state << 1) | (feedback & 1);
    }
    for (unsigned i = 8; i > 0; i--) {
        mi[i - 1] = (uint8_t)state;
        state >>= 8;
    }
    mi[8] = 0;
}

void p25_aes_stream(uint8_t stream[240], const uint8_t key[32], const uint8_t mi[9])
{
    uint8_t block[16], next[9], schedule[240];
    memcpy(next, mi, sizeof next);
    p25_aes_next_mi(next);
    memcpy(block, mi, 8);
    memcpy(block + 8, next, 8);
    expand_key(schedule, key);
    for (unsigned i = 0; i < 240; i += 16) {
        encrypt_block(block, schedule);
        memcpy(stream + i, block, 16);
    }
    volatile uint8_t *scratch = schedule;
    for (unsigned i = 0; i < sizeof schedule; i++) scratch[i] = 0;
}

unsigned p25_crypto_key_bytes(uint8_t algorithm)
{
    switch (algorithm) {
    case P25_ALG_AES256: return 32;
    case P25_ALG_DES: return 8;
    case P25_ALG_ADP: return 5;
    default: return 0;
    }
}

void p25_crypto_stream(uint8_t stream[240], const uint8_t key[32], const uint8_t mi[9], uint8_t algorithm)
{
    switch (algorithm) {
    case P25_ALG_AES256: p25_aes_stream(stream, key, mi); break;
    case P25_ALG_DES: p25_des_stream(stream, key, mi); break;
    case P25_ALG_ADP: p25_adp_stream(stream, key, mi); break;
    }
}

void p25_crypto_voice(const uint8_t stream[240], uint8_t algorithm, uint8_t duid, uint16_t imbe[9][8], uint8_t lsd[2])
{
    /* Skip the initial OFB block and 11 reserved/LC bytes, then two LSD
     * bytes immediately before the ninth codeword in each LDU. */
    unsigned discard = algorithm == P25_ALG_AES256 ? 16 : algorithm == P25_ALG_DES ? 8 : 0;
    const uint8_t *ks = stream + discard + 11 + (duid == P25_DUID_LDU2 ? 101 : 0);
    for (unsigned frame = 0; frame < 9; frame++) {
        if (frame == 8) {
            lsd[0] ^= *ks++;
            lsd[1] ^= *ks++;
        }
        unsigned bit = 0;
        for (unsigned word = 0; word < 8; word++) {
            unsigned width = word < 4 ? 12 : word < 7 ? 11 : 7;
            uint16_t mask = 0;
            for (unsigned i = 0; i < width; i++, bit++)
                mask = (uint16_t)((mask << 1) | ((ks[bit / 8] >> (7 - bit % 8)) & 1));
            imbe[frame][word] ^= mask;
        }
        ks += 11;
    }
}
