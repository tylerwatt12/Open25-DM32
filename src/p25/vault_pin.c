/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "vault_pin.h"
#include "key_vault.h"
#include <string.h>

/* PBKDF2-HMAC-SHA-256, one 32-byte block (RFC 8018 and RFC 6234). */
static const uint32_t initial[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};
static const uint32_t constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t rotr(uint32_t word, unsigned bits)
{
    return (word >> bits) | (word << (32u - bits));
}

static void compress(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16; i++) {
        const uint8_t *p = block + 4 * i;
        w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | p[3];
    }
    for (unsigned i = 16; i < 64; i++) {
        uint32_t a = w[i - 15], b = w[i - 2];
        uint32_t s0 = rotr(a, 7) ^ rotr(a, 18) ^ (a >> 3);
        uint32_t s1 = rotr(b, 17) ^ rotr(b, 19) ^ (b >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (unsigned i = 0; i < 64; i++) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + choose + constants[i] + w[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + s0 + majority;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    p25_vault_erase(w, sizeof w);
}

/* All PBKDF2 messages here are 20 or 32 bytes after the 64-byte HMAC pad. */
static void digest(const uint32_t prefix[8], const uint8_t *msg, unsigned len, uint8_t out[32])
{
    uint32_t state[8];
    uint8_t block[64] = {0};
    memcpy(state, prefix, sizeof state);
    memcpy(block, msg, len);
    block[len] = 0x80;
    block[62] = (uint8_t)((64u + len) >> 5);
    block[63] = (uint8_t)((64u + len) << 3);
    compress(state, block);
    for (unsigned i = 0; i < 8; i++) {
        uint32_t word = state[i];
        out[i * 4] = (uint8_t)(word >> 24);
        out[i * 4 + 1] = (uint8_t)(word >> 16);
        out[i * 4 + 2] = (uint8_t)(word >> 8);
        out[i * 4 + 3] = (uint8_t)word;
    }
    p25_vault_erase(block, sizeof block);
    p25_vault_erase(state, sizeof state);
}

void p25_pin_derive(const char *pin, size_t len, const uint8_t salt[16],
                    uint8_t master[32], void (*yield)(void),
                    volatile uint8_t *progress)
{
    uint8_t pad[64] = {0}, input[20] = {0}, u[32];
    uint32_t inner[8], outer[8];
    memcpy(pad, pin, len);
    for (unsigned i = 0; i < sizeof pad; i++) pad[i] ^= 0x36;
    memcpy(inner, initial, sizeof inner);
    compress(inner, pad);
    for (unsigned i = 0; i < sizeof pad; i++) pad[i] ^= 0x36 ^ 0x5c;
    memcpy(outer, initial, sizeof outer);
    compress(outer, pad);
    p25_vault_erase(pad, sizeof pad);

    memcpy(input, salt, 16);
    input[19] = 1;
    digest(inner, input, sizeof input, u);
    digest(outer, u, sizeof u, u);
    memcpy(master, u, sizeof u);
    for (unsigned round = 1; round < 600000; round++) {
        digest(inner, u, sizeof u, u);
        digest(outer, u, sizeof u, u);
        for (unsigned i = 0; i < sizeof u; i++) master[i] ^= u[i];
        if ((round & 255u) == 0) {
            if (progress) *progress = (uint8_t)(round / 6000u);
            if (yield) yield();
        }
    }
    p25_vault_erase(u, sizeof u);
    p25_vault_erase(inner, sizeof inner);
    p25_vault_erase(outer, sizeof outer);
}
