/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "secure_frame.h"
#include "p25_runtime.h"

int p25_secure_header(const p25_call *call, const p25_encryption_sync *sync,
                      uint8_t out[P25_HDU_BYTES])
{
    p25cai_frame frame = {.nac = call->nac, .duid = P25_DUID_HDU, .status = call->status_symbol};
    p25cai_header header = {
        .manufacturer = call->manufacturer_id, .algorithm = sync->algorithm,
        .key = sync->kid, .group = call->talkgroup,
    };
    memcpy(header.indicator, sync->mi, sizeof header.indicator);
    if (p25cai_header_encode(&header, frame.metadata)) return -1;
    return p25cai_frame_encode(&frame, out, P25_HDU_BYTES) < 0 ? -1 : 0;
}

int p25_secure_ldu(const p25_call *call, const p25_encryption_sync *sync,
                   uint8_t duid, const uint16_t imbe[9][8],
                   const uint8_t lsd[2], uint8_t out[P25_LDU_BYTES])
{
    p25cai_frame frame = {.nac = call->nac, .duid = duid, .status = call->status_symbol};
    if (duid == P25_DUID_LDU1) {
        p25cai_link_control lc = {
            .source = call->source_id, .group = call->talkgroup,
            .manufacturer = call->manufacturer_id, .service = call->service_options | 0x40u,
        };
        if (p25cai_lc_encode(&lc, frame.metadata)) return -1;
    } else if (duid == P25_DUID_LDU2) {
        memcpy(frame.metadata, sync->mi, sizeof sync->mi);
        frame.metadata[9] = sync->algorithm;
        frame.metadata[10] = (uint8_t)(sync->kid >> 8);
        frame.metadata[11] = (uint8_t)sync->kid;
    } else {
        return -1;
    }
    memcpy(frame.voice, imbe, sizeof frame.voice);
    memcpy(frame.low_speed, lsd, sizeof frame.low_speed);
    return p25cai_frame_encode(&frame, out, P25_LDU_BYTES) < 0 ? -1 : 0;
}
