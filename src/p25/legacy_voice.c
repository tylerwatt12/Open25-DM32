/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "secure_frame.h"
#include "crypto_voice.h"
#include "p25_runtime.h"

/* DES bit selections and substitution tables from FIPS 46-3. */
static const uint8_t key_select[56] = {
    57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18, 10, 2,
    59, 51, 43, 35, 27, 19, 11, 3, 60, 52, 44, 36, 63, 55, 47, 39,
    31, 23, 15, 7, 62, 54, 46, 38, 30, 22, 14, 6, 61, 53, 45, 37,
    29, 21, 13, 5, 28, 20, 12, 4,
};

static const uint8_t round_select[48] = {
    14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10, 23, 19, 12, 4,
    26, 8, 16, 7, 27, 20, 13, 2, 41, 52, 31, 37, 47, 55, 30, 40,
    51, 45, 33, 48, 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32,
};

static const uint8_t rotations[16] = {
    1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1,
};

static const uint8_t initial_order[64] = {
    58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7,
};

static const uint8_t expansion[48] = {
    32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9, 8, 9, 10, 11,
    12, 13, 12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21,
    22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1,
};

static const uint8_t sboxes[512] = {
    14, 0, 4, 15, 13, 7, 1, 4, 2, 14, 15, 2, 11, 13, 8, 1,
    3, 10, 10, 6, 6, 12, 12, 11, 5, 9, 9, 5, 0, 3, 7, 8,
    4, 15, 1, 12, 14, 8, 8, 2, 13, 4, 6, 9, 2, 1, 11, 7,
    15, 5, 12, 11, 9, 3, 7, 14, 3, 10, 10, 0, 5, 6, 0, 13,
    15, 3, 1, 13, 8, 4, 14, 7, 6, 15, 11, 2, 3, 8, 4, 14,
    9, 12, 7, 0, 2, 1, 13, 10, 12, 6, 0, 9, 5, 11, 10, 5,
    0, 13, 14, 8, 7, 10, 11, 1, 10, 3, 4, 15, 13, 4, 1, 2,
    5, 11, 8, 6, 12, 7, 6, 12, 9, 0, 3, 5, 2, 14, 15, 9,
    10, 13, 0, 7, 9, 0, 14, 9, 6, 3, 3, 4, 15, 6, 5, 10,
    1, 2, 13, 8, 12, 5, 7, 14, 11, 12, 4, 11, 2, 15, 8, 1,
    13, 1, 6, 10, 4, 13, 9, 0, 8, 6, 15, 9, 3, 8, 0, 7,
    11, 4, 1, 15, 2, 14, 12, 3, 5, 11, 10, 5, 14, 2, 7, 12,
    7, 13, 13, 8, 14, 11, 3, 5, 0, 6, 6, 15, 9, 0, 10, 3,
    1, 4, 2, 7, 8, 2, 5, 12, 11, 1, 12, 10, 4, 14, 15, 9,
    10, 3, 6, 15, 9, 0, 0, 6, 12, 10, 11, 1, 7, 13, 13, 8,
    15, 9, 1, 4, 3, 5, 14, 11, 5, 12, 2, 7, 8, 2, 4, 14,
    2, 14, 12, 11, 4, 2, 1, 12, 7, 4, 10, 7, 11, 13, 6, 1,
    8, 5, 5, 0, 3, 15, 15, 10, 13, 3, 0, 9, 14, 8, 9, 6,
    4, 11, 2, 8, 1, 12, 11, 7, 10, 1, 13, 14, 7, 2, 8, 13,
    15, 6, 9, 15, 12, 0, 5, 9, 6, 10, 3, 4, 0, 5, 14, 3,
    12, 10, 1, 15, 10, 4, 15, 2, 9, 7, 2, 12, 6, 9, 8, 5,
    0, 6, 13, 1, 3, 13, 4, 14, 14, 0, 7, 11, 5, 3, 11, 8,
    9, 4, 14, 3, 15, 2, 5, 12, 2, 9, 8, 5, 12, 15, 3, 10,
    7, 11, 0, 14, 4, 1, 10, 7, 1, 6, 13, 0, 11, 8, 6, 13,
    4, 13, 11, 0, 2, 11, 14, 7, 15, 4, 0, 9, 8, 1, 13, 10,
    3, 14, 12, 3, 9, 5, 7, 12, 5, 2, 10, 15, 6, 8, 1, 6,
    1, 6, 4, 11, 11, 13, 13, 8, 12, 1, 3, 4, 7, 10, 14, 7,
    10, 9, 15, 5, 6, 0, 8, 15, 0, 14, 5, 2, 9, 3, 2, 12,
    13, 1, 2, 15, 8, 13, 4, 8, 6, 10, 15, 3, 11, 7, 1, 4,
    10, 12, 9, 5, 3, 6, 14, 11, 5, 0, 0, 14, 12, 9, 7, 2,
    7, 2, 11, 1, 4, 14, 1, 7, 9, 4, 12, 10, 14, 8, 2, 13,
    0, 15, 6, 12, 10, 9, 13, 0, 15, 3, 3, 5, 5, 6, 8, 11,
};

static const uint8_t round_order[32] = {
    16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25,
};

static const uint8_t final_order[64] = {
    40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25,
};

static uint64_t load64(const uint8_t bytes[8])
{
    uint64_t word = 0;
    for (unsigned i = 0; i < 8; i++) word = (word << 8) | bytes[i];
    return word;
}

static uint64_t select_bits(uint64_t word, unsigned width, const uint8_t *table, unsigned count)
{
    uint64_t result = 0;
    for (unsigned i = 0; i < count; i++)
        result = (result << 1) | ((word >> (width - table[i])) & 1);
    return result;
}

static uint32_t des_round(uint32_t right, uint64_t key)
{
    uint64_t expanded = select_bits(right, 32, expansion, sizeof expansion) ^ key;
    uint32_t substituted = 0;
    for (unsigned i = 0; i < 8; i++)
        substituted = (substituted << 4) | sboxes[i * 64 + ((expanded >> (42 - 6 * i)) & 63)];
    return (uint32_t)select_bits(substituted, 32, round_order, sizeof round_order);
}

void p25_des_stream(uint8_t stream[240], const uint8_t key[8], const uint8_t mi[9])
{
    uint64_t subkeys[16];
    uint64_t key56 = select_bits(load64(key), 64, key_select, sizeof key_select);
    uint32_t left_key = (uint32_t)(key56 >> 28);
    uint32_t right_key = (uint32_t)(key56 & 0xfffffff);
    for (unsigned i = 0; i < 16; i++) {
        unsigned n = rotations[i];
        left_key = ((left_key << n) | (left_key >> (28 - n))) & 0xfffffff;
        right_key = ((right_key << n) | (right_key >> (28 - n))) & 0xfffffff;
        key56 = ((uint64_t)left_key << 28) | right_key;
        subkeys[i] = select_bits(key56, 56, round_select, sizeof round_select);
    }
    uint64_t state = load64(mi);
    for (unsigned i = 0; i < 240; i += 8) {
        uint64_t block = select_bits(state, 64, initial_order, sizeof initial_order);
        uint32_t left = (uint32_t)(block >> 32);
        uint32_t right = (uint32_t)block;
        for (unsigned round = 0; round < 16; round++) {
            uint32_t next = left ^ des_round(right, subkeys[round]);
            left = right;
            right = next;
        }
        block = ((uint64_t)right << 32) | left;
        state = select_bits(block, 64, final_order, sizeof final_order);
        for (unsigned j = 0; j < 8; j++)
            stream[i + j] = (uint8_t)(state >> (56 - 8 * j));
    }
    volatile uint64_t *scratch = subkeys;
    for (unsigned i = 0; i < 16; i++) scratch[i] = 0;
}

static void swap(uint8_t *a, uint8_t *b)
{
    uint8_t t = *a;
    *a = *b;
    *b = t;
}

void p25_adp_stream(uint8_t stream[240], const uint8_t key[5], const uint8_t mi[9])
{
    uint8_t state[256], material[13];
    memcpy(material, key, 5);
    memcpy(material + 5, mi, 8);
    for (unsigned i = 0; i < 256; i++) state[i] = (uint8_t)i;
    uint8_t j = 0;
    for (unsigned i = 0; i < 256; i++) {
        j = (uint8_t)(j + state[i] + material[i % sizeof material]);
        swap(state + i, state + j);
    }
    uint8_t i = 0;
    j = 0;
    for (unsigned n = 0; n < 256 + 240; n++) {
        ++i;
        j = (uint8_t)(j + state[i]);
        swap(state + i, state + j);
        if (n >= 256) stream[n - 256] = state[(uint8_t)(state[i] + state[j])];
    }
    volatile uint8_t *scratch = material;
    for (unsigned n = 0; n < sizeof material; n++) scratch[n] = 0;
    scratch = state;
    for (unsigned n = 0; n < sizeof state; n++) scratch[n] = 0;
}
