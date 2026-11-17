/* SPDX-License-Identifier: MIT
 * Source and structure-layout interface for existing Open25-DM32 callers. */
#ifndef DM32_P25_H
#define DM32_P25_H
#include "p25cai.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {
    P25_DUID_HDU=0, P25_DUID_TDU=3, P25_DUID_LDU1=5, P25_DUID_LDU2=10,
    P25_DUID_TDULC=15, P25_HDU_BYTES=99, P25_LDU_BYTES=216,
    P25_TDU_BYTES=18, P25_TDULC_BYTES=54, P25_IMBE_FRAMES=9, P25_NAC_ANY=0xffff
};
typedef struct {
    uint16_t nac, talkgroup;
    uint32_t source_id;
    uint8_t manufacturer_id, service_options, status_symbol;
} p25_call;
typedef p25cai_nid p25_nid;
typedef struct {
    p25_nid nid;
    uint8_t metadata_valid, clear_voice, manufacturer_id, service_options;
    uint8_t link_control_format, algorithm_id;
    uint16_t key_id, talkgroup;
    uint32_t source_id;
    uint8_t message_indicator[9], low_speed_data[2], low_speed_valid[2];
    uint16_t metadata_corrected_bits;
    uint8_t metadata_corrected_symbols, voice_corrected_bits[9], voice_u0_corrected_bits[9];
    uint16_t imbe[9][8];
} p25_decoded;
typedef p25cai_receiver p25_rx;

uint64_t p25_nid_encode(uint16_t nac, uint8_t duid);
int p25_nid_decode(uint64_t word, p25_nid *result);
size_t p25_frame_bytes(uint8_t duid);
int p25_encode_hdu(const p25_call *call, uint8_t out[99]);
int p25_encode_ldu(const p25_call *call, uint8_t duid,
                   const uint16_t imbe[9][8], const uint8_t low_speed[2], uint8_t out[216]);
/* TIA-102.AABF-D-2 sections 7.3.35–36. */
enum { P25_ALIAS_LENGTH = 14, P25_ALIAS_FRAGMENT = 7,
       P25_ALIAS_MFID = 0xa4, P25_ALIAS_FIRST = 50, P25_ALIAS_LAST = 53 };
int p25_alias_opcode(uint8_t manufacturer, uint8_t format);
int p25_encode_alias_ldu(const p25_call *call, const uint16_t imbe[9][8],
                         uint8_t opcode, const char fragment[7], uint8_t out[216]);
int p25_encode_tdu(const p25_call *call, uint8_t out[18]);
int p25_encode_tdulc(const p25_call *call, uint8_t out[54]);
int p25_decode_frame(const uint8_t *air, size_t bytes, p25_decoded *result);
int p25_imbe_encode(const uint16_t u[8], uint8_t serialized[18]);
int p25_imbe_decode(const uint8_t serialized[18], uint16_t u[8], uint8_t *u0_errors);
int p25_rs_encode(const uint8_t *data, size_t count, unsigned parity, uint8_t *out);
int p25_rs_decode(uint8_t *word, size_t count, unsigned parity);
void p25_rx_init(p25_rx *rx, uint16_t nac, uint8_t sync_tolerance);
int p25_rx_push_dibit(p25_rx *rx, uint8_t dibit);
#ifdef __cplusplus
}
#endif
#endif
