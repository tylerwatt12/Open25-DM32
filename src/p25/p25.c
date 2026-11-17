/* SPDX-License-Identifier: MIT
 * Existing Open25-DM32 conventional callers require their public C interface
 * and structure layouts. This binding owns only that interface conversion
 * and its conventional clear-group policy. Core coding/framing and receiver
 * state remain owned by p25cai; there is no second FEC or receiver engine. */
#include "p25.h"
#include <string.h>

static int call_valid(const p25_call *call)
{
    return call && call->nac <= 4095 && call->source_id <= 0xffffff &&
        call->manufacturer_id <= 1 && call->status_symbol <= 3 &&
        !(call->service_options & ~0x87u);
}

uint64_t p25_nid_encode(uint16_t nac, uint8_t duid)
{
    uint64_t word = 0;
    (void)p25cai_nid_encode(nac,duid,&word);
    return word;
}

int p25_nid_decode(uint64_t word, p25_nid *result)
{
    if (!result) return -1;
    p25_nid decoded;
    int corrections = p25cai_nid_decode(word,&decoded.nac,&decoded.duid);
    if (corrections < 0 || !p25cai_frame_size(decoded.duid)) return -1;
    decoded.corrected_bits = (uint8_t)corrections;
    *result = decoded;
    return 0;
}

size_t p25_frame_bytes(uint8_t duid) { return p25cai_frame_size(duid); }

static int encode(const p25_call *call, uint8_t duid, const uint16_t imbe[9][8],
                   const uint8_t *lsd, const uint8_t *alias_lc, uint8_t *out)
{
    if (!call_valid(call) || !out) return -1;
    p25cai_frame frame = {0};
    frame.nac = call->nac;
    frame.duid = duid;
    frame.status = call->status_symbol;
    if (duid == P25_DUID_HDU) {
        p25cai_header header = {0};
        header.manufacturer = call->manufacturer_id;
        header.algorithm = 0x80;
        header.group = call->talkgroup;
        if (p25cai_header_encode(&header,frame.metadata)) return -1;
    } else if (duid == P25_DUID_LDU2) frame.metadata[9] = 0x80;
    else if (alias_lc) memcpy(frame.metadata, alias_lc, 9);
    else if (duid != P25_DUID_TDU) {
        p25cai_link_control lc = {0};
        lc.source = call->source_id;
        lc.group = call->talkgroup;
        lc.manufacturer = call->manufacturer_id;
        lc.service = call->service_options;
        if (p25cai_lc_encode(&lc,frame.metadata)) return -1;
    }
    if (duid == P25_DUID_LDU1 || duid == P25_DUID_LDU2) {
        if (!imbe) return -1;
        memcpy(frame.voice,imbe,sizeof frame.voice);
        for (unsigned i = 0; i < 9; ++i) {
            /* Validate before changing the sync bit, including its input width. */
            if (frame.voice[i][7] > 127) return -1;
            frame.voice[i][7] = (uint16_t)((frame.voice[i][7] & 126) |
                ((i + (duid == P25_DUID_LDU2 ? 9 : 0)) & 1));
        }
        if (lsd) memcpy(frame.low_speed,lsd,2);
    }
    return p25cai_frame_encode(&frame,out,p25cai_frame_size(duid)) < 0 ? -1 : 0;
}

int p25_encode_hdu(const p25_call *call, uint8_t out[99])
{ return encode(call,P25_DUID_HDU,NULL,NULL,NULL,out); }
int p25_encode_tdu(const p25_call *call, uint8_t out[18])
{ return encode(call,P25_DUID_TDU,NULL,NULL,NULL,out); }
int p25_encode_tdulc(const p25_call *call, uint8_t out[54])
{ return encode(call,P25_DUID_TDULC,NULL,NULL,NULL,out); }
int p25_encode_ldu(const p25_call *call, uint8_t duid,
                   const uint16_t imbe[9][8], const uint8_t low_speed[2], uint8_t out[216])
{
    if (duid != P25_DUID_LDU1 && duid != P25_DUID_LDU2) return -1;
    return encode(call,duid,imbe,low_speed,NULL,out);
}

int p25_alias_opcode(uint8_t manufacturer, uint8_t format)
{
    return manufacturer == P25_ALIAS_MFID && format >= P25_ALIAS_FIRST && format <= P25_ALIAS_LAST;
}

int p25_encode_alias_ldu(const p25_call *call, const uint16_t imbe[9][8],
                         uint8_t opcode, const char fragment[7], uint8_t out[216])
{
    if (!fragment || !p25_alias_opcode(P25_ALIAS_MFID, opcode)) return -1;
    uint8_t lc[9] = {opcode, P25_ALIAS_MFID};
    memcpy(lc + 2, fragment, P25_ALIAS_FRAGMENT);
    return encode(call, P25_DUID_LDU1, imbe, NULL, lc, out);
}

int p25_decode_frame(const uint8_t *air, size_t bytes, p25_decoded *result)
{
    if (!result) return -1;
    p25cai_frame frame;
    p25cai_quality q;
    if (p25cai_frame_decode_ex(air,bytes,4,&frame,&q) < 0) return -1;
    p25_decoded decoded = {0};
    decoded.nid.nac = frame.nac;
    decoded.nid.duid = frame.duid;
    decoded.nid.corrected_bits = (uint8_t)q.nid_corrected;
    decoded.metadata_corrected_bits = q.metadata_inner_corrected < 0 ? UINT16_MAX : (uint16_t)q.metadata_inner_corrected;
    decoded.metadata_corrected_symbols = q.metadata_corrected < 0 ? 0 : (uint8_t)q.metadata_corrected;
    decoded.metadata_valid = q.metadata_valid;
    if (frame.duid == P25_DUID_TDU) {
        decoded.metadata_valid = 1;
    } else if (q.metadata_valid && frame.duid == P25_DUID_HDU) {
        p25cai_header header;
        /* The existing caller inspects raw MFID for admission itself. Use
         * the common field layout without imposing a new MFID policy here. */
        uint8_t fields[15];
        memcpy(fields,frame.metadata,sizeof fields);
        fields[9] = 0;
        if (p25cai_header_decode(fields,&header) < 0) decoded.metadata_valid = 0;
        else {
            decoded.manufacturer_id = frame.metadata[9];
            decoded.algorithm_id = header.algorithm;
            decoded.key_id = header.key;
            decoded.talkgroup = header.group;
            memcpy(decoded.message_indicator,header.indicator,9);
            decoded.clear_voice = header.algorithm == 0x80;
        }
    } else if (q.metadata_valid && frame.duid == P25_DUID_LDU2) {
        p25cai_header header = {0};
        memcpy(header.indicator,frame.metadata,9);
        header.algorithm = frame.metadata[9];
        header.key = (uint16_t)((frame.metadata[10] << 8) | frame.metadata[11]);
        decoded.algorithm_id = header.algorithm;
        decoded.key_id = header.key;
        memcpy(decoded.message_indicator,header.indicator,9);
        decoded.clear_voice = header.algorithm == 0x80;
    } else if (q.metadata_valid) {
        p25cai_link_control lc;
        decoded.link_control_format = frame.metadata[0];
        decoded.manufacturer_id = frame.metadata[1];
        decoded.service_options = frame.metadata[2];
        if (frame.duid == P25_DUID_LDU1 && p25_alias_opcode(frame.metadata[1], frame.metadata[0])) {
            /* LDU1 has no encryption sync. Use the unused metadata bytes. */
            memcpy(decoded.message_indicator, frame.metadata + 2, P25_ALIAS_FRAGMENT);
        } else if (p25cai_lc_decode(frame.metadata,&lc) < 0 || lc.format != 0) decoded.metadata_valid = 0;
        else {
            decoded.manufacturer_id = lc.manufacturer;
            decoded.service_options = lc.service;
            decoded.link_control_format = lc.format;
            decoded.talkgroup = lc.group;
            decoded.source_id = lc.source;
            decoded.clear_voice = !(lc.service & 0x40);
        }
    }
    if (frame.duid == P25_DUID_LDU1 || frame.duid == P25_DUID_LDU2) {
        memcpy(decoded.imbe,frame.voice,sizeof decoded.imbe);
        memcpy(decoded.low_speed_data,frame.low_speed,2);
        for (unsigned i = 0; i < 9; ++i) {
            decoded.voice_corrected_bits[i] = q.voice_corrected[i] < 0 ? UINT8_MAX : (uint8_t)q.voice_corrected[i];
            decoded.voice_u0_corrected_bits[i] = q.voice_u0_corrected[i];
        }
        for (unsigned i = 0; i < 2; ++i)
            decoded.low_speed_valid[i] = (uint8_t)((q.low_speed_valid >> i) & 1);
    }
    *result = decoded;
    return 0;
}

int p25_imbe_encode(const uint16_t u[8], uint8_t serialized[18])
{ return p25cai_voice_encode_serial(u,serialized) < 0 ? -1 : 0; }
int p25_imbe_decode(const uint8_t serialized[18], uint16_t u[8], uint8_t *u0_errors)
{
    if (!u0_errors) return -1;
    int result = p25cai_voice_decode_serial(serialized,u,u0_errors);
    return result < 0 ? -1 : result;
}
int p25_rs_encode(const uint8_t *data, size_t count, unsigned parity, uint8_t *out)
{
    if (count > 36 || parity > 36 || count+parity > 36) return -1;
    return p25cai_rs_encode(data,(unsigned)count+parity,(unsigned)count,out) < 0 ? -1 : 0;
}
int p25_rs_decode(uint8_t *word, size_t count, unsigned parity)
{
    if (count > 36 || count < parity) return -1;
    int result = p25cai_rs_decode(word,(unsigned)count,(unsigned)count-parity);
    return result < 0 ? -1 : result;
}
void p25_rx_init(p25_rx *rx, uint16_t nac, uint8_t sync_tolerance)
{
    if (!rx) return;
    if (p25cai_receiver_init_ex(rx,nac,sync_tolerance > 4 ? 4 : sync_tolerance,1) < 0) {
        memset(rx,0,sizeof *rx);
        rx->expected_nac = nac;
    }
}
int p25_rx_push_dibit(p25_rx *rx, uint8_t dibit)
{
    if (!rx) return -1;
    if (dibit > 3) { rx->symbols = 0; return -1; }
    size_t bytes;
    return p25cai_receiver_push(rx,dibit,rx->frame,&bytes);
}
