/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef P25_CRYPTO_VOICE_H
#define P25_CRYPTO_VOICE_H
#include <stdint.h>

enum { P25_CRYPTO_KEY_BYTES = 32, P25_CRYPTO_STREAM_BYTES = 240 };
void p25_aes256_block(uint8_t block[16], const uint8_t key[32]);
void p25_aes_next_mi(uint8_t mi[9]);
void p25_aes_stream(uint8_t stream[P25_CRYPTO_STREAM_BYTES],
                    const uint8_t key[P25_CRYPTO_KEY_BYTES], const uint8_t mi[9]);
void p25_des_stream(uint8_t stream[P25_CRYPTO_STREAM_BYTES], const uint8_t key[8], const uint8_t mi[9]);
/* ADP stream starts after its 256-byte discard. */
void p25_adp_stream(uint8_t stream[P25_CRYPTO_STREAM_BYTES], const uint8_t key[5], const uint8_t mi[9]);
unsigned p25_crypto_key_bytes(uint8_t algorithm);
void p25_crypto_stream(uint8_t stream[P25_CRYPTO_STREAM_BYTES], const uint8_t key[P25_CRYPTO_KEY_BYTES],
                       const uint8_t mi[9], uint8_t algorithm);
void p25_crypto_voice(const uint8_t stream[P25_CRYPTO_STREAM_BYTES], uint8_t algorithm, uint8_t duid,
                   uint16_t imbe[9][8], uint8_t lsd[2]);
#endif
