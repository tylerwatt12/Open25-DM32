/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef OPEN25_SECURE_FRAME_H
#define OPEN25_SECURE_FRAME_H
#include "p25.h"
enum { P25_ALG_CLEAR = 0x80, P25_ALG_DES = 0x81, P25_ALG_AES256 = 0x84, P25_ALG_ADP = 0xaa };
typedef struct {
    uint8_t algorithm;
    uint16_t kid;
    uint8_t mi[9];
} p25_encryption_sync;

int p25_secure_header(const p25_call *call, const p25_encryption_sync *sync,
                      uint8_t out[P25_HDU_BYTES]);
int p25_secure_ldu(const p25_call *call, const p25_encryption_sync *sync,
                   uint8_t duid, const uint16_t imbe[9][8],
                   const uint8_t lsd[2], uint8_t out[P25_LDU_BYTES]);
#endif
