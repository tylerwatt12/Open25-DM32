/* SPDX-License-Identifier: MIT AND GPL-3.0-or-later */
/* Original clear voice path remains under MIT. Secure voice additions retain GPL-3.0-or-later. */
#include "voice.h"
#include "p25_runtime.h"

static void admission_reset(p25_voice *v)
{
    v->crypto_clear = v->have_group = 0;
    v->active_nac = P25_NAC_ANY;
    v->active_group = 0;
    v->active_source = 0;
#if CONFIG_P25_ENCRYPTION
    v->crypto_ready = v->expected_duid = v->encrypted_lc = 0;
    v->crypto_sync_mismatch = v->crypto_warning_sent = 0;
    v->last_frame_end = 0;
    memset(v->crypto_stream, 0, sizeof v->crypto_stream);
#endif
    dm32_imbe_init(&v->codec);
}

void p25_voice_rx_reset(p25_voice *v)
{
    /* Stale receive records must not reset the shared active TX codec. */
    if (v->transmitting) return;
    p25_rx_init(&v->framer, v->framer.expected_nac, 2);
    admission_reset(v);
}

void p25_voice_init(p25_voice *v, uint16_t nac, uint16_t group,
                    p25_pcm_sink sink, void *ctx)
{
    memset(v, 0, sizeof *v);
    v->sink = sink;
    v->sink_ctx = ctx;
    v->group_filter = group;
    p25_rx_init(&v->framer, nac == 0xf7e ? P25_NAC_ANY : nac, 2);
    admission_reset(v);
}

void p25_voice_rx_dibit(p25_voice *v, uint8_t dibit)
{
    if (v->transmitting || !p25_rx_push_dibit(&v->framer, dibit)) return;
    p25_decoded d;
    uint32_t at=v->micros?v->micros():0;
    int decoded=p25_decode_frame(v->framer.frame, p25_frame_bytes(v->framer.nid.duid), &d);
    uint32_t elapsed=v->micros?v->micros()-at:0;
    if (elapsed>v->fec_max_us)v->fec_max_us=elapsed;
    if (decoded) {
        admission_reset(v);
        return;
    }
    v->rx_frames++;
    if (v->active_nac != d.nid.nac) {
        admission_reset(v);
        v->active_nac = d.nid.nac;
    }
    if (d.nid.duid == P25_DUID_TDU || d.nid.duid == P25_DUID_TDULC) {
        admission_reset(v);
        return;
    }
#if CONFIG_P25_ENCRYPTION
    int sync_matches = d.algorithm_id == v->crypto_algorithm &&
        d.key_id == v->crypto_key_id && p25_crypto_key_bytes(d.algorithm_id);
    if (v->crypto_ready && (d.nid.duid != v->expected_duid ||
        v->framer.symbols_seen - v->last_frame_end != P25_LDU_BYTES * 4u))
        v->crypto_ready = 0;
    v->last_frame_end = v->framer.symbols_seen;
    if (!d.metadata_valid || (d.nid.duid == P25_DUID_HDU && d.manufacturer_id > 1)) {
        v->crypto_clear = v->have_group = v->crypto_ready = v->crypto_sync_mismatch = 0;
    } else if (d.nid.duid == P25_DUID_HDU) {
        admission_reset(v);
        v->active_nac = d.nid.nac;
        v->last_frame_end = v->framer.symbols_seen;
        v->active_group = d.talkgroup;
        v->have_group = d.talkgroup != 0;
        v->crypto_clear = d.clear_voice;
        v->crypto_sync_mismatch = !d.clear_voice && !sync_matches;
        if (sync_matches) {
            p25_crypto_stream(v->crypto_stream, v->crypto_key, d.message_indicator, d.algorithm_id);
            v->crypto_ready = 1;
            v->expected_duid = P25_DUID_LDU1;
        }
    } else if (d.nid.duid == P25_DUID_LDU1) {
        if ((v->have_group && v->active_group != d.talkgroup) ||
            (v->active_source && v->active_source != d.source_id)) {
            v->crypto_clear = v->crypto_ready = v->crypto_sync_mismatch = 0;
            v->crypto_warning_sent = 0;
        }
        v->active_group = d.talkgroup;
        v->active_source = d.source_id;
        v->have_group = d.talkgroup && d.source_id && d.source_id <= 9999999u;
        v->encrypted_lc = !d.clear_voice;
        if (v->encrypted_lc) v->crypto_clear = 0;
        else v->crypto_ready = v->crypto_sync_mismatch = 0;
        v->expected_duid = P25_DUID_LDU2;
    } else if (d.nid.duid == P25_DUID_LDU2) {
        v->crypto_clear = d.clear_voice;
        v->crypto_sync_mismatch = !d.clear_voice && !sync_matches;
        if (!sync_matches) v->crypto_ready = 0;
    }
    if (d.nid.duid != P25_DUID_LDU1 && d.nid.duid != P25_DUID_LDU2) return;
    int addressed = v->have_group && v->active_source &&
        (!v->group_filter || v->active_group == v->group_filter || v->active_group == 0xffff);
    if (addressed && v->encrypted_lc && v->crypto_sync_mismatch && !v->crypto_warning_sent) {
        v->rx_crypto_mismatch++;
        v->crypto_warning_sent = 1;
    }
    int admitted = addressed && (v->encrypted_lc ? v->crypto_ready : v->crypto_clear);
    if (admitted && v->crypto_ready)
        p25_crypto_voice(v->crypto_stream, v->crypto_algorithm, d.nid.duid, d.imbe, d.low_speed_data);
    if (d.nid.duid == P25_DUID_LDU2 && d.metadata_valid && sync_matches) {
        p25_crypto_stream(v->crypto_stream, v->crypto_key, d.message_indicator, d.algorithm_id);
        v->crypto_ready = 1;
        v->expected_duid = P25_DUID_LDU1;
    }
#else
    if (!d.metadata_valid || (d.nid.duid == P25_DUID_HDU && d.manufacturer_id > 1)) {
        /* Do not reuse permission from a previous good header over damage. */
        v->crypto_clear = v->have_group = 0;
    } else if (d.nid.duid == P25_DUID_HDU) {
        admission_reset(v);
        v->active_nac = d.nid.nac;
        v->crypto_clear = d.clear_voice;
        v->active_group = d.talkgroup;
        v->have_group = d.talkgroup != 0;
    } else if (d.nid.duid == P25_DUID_LDU1) {
        if ((v->have_group && v->active_group != d.talkgroup) ||
            (v->active_source && v->active_source != d.source_id))
            v->crypto_clear = 0;
        v->active_group = d.talkgroup;
        v->active_source = d.source_id;
        v->have_group = d.talkgroup && d.source_id && d.source_id <= 9999999u;
        if (!d.clear_voice) v->crypto_clear = 0;
        /* LC's unprotected bit alone cannot override an encrypted ALGID.
         * Late entry waits for a valid LDU2 encryption sync. */
    } else if (d.nid.duid == P25_DUID_LDU2) {
        v->crypto_clear = d.clear_voice;
    }
    if (d.nid.duid != P25_DUID_LDU1 && d.nid.duid != P25_DUID_LDU2) return;
    int admitted = v->crypto_clear && v->have_group &&
        (!v->group_filter || v->active_group == v->group_filter || v->active_group == 0xffff);
#endif
    for (unsigned i = 0; i < 9; i++) {
        int16_t pcm[160] = {0};
        if (!admitted) v->rx_muted++;
        /* Conservative implementation policy, not a TIA concealment rule.
         * Golay/Hamming can miscorrect beyond t; no audio-quality claim. */
        else {
            at=v->micros?v->micros():0;
            int ok=d.voice_u0_corrected_bits[i] <= 3 && d.voice_corrected_bits[i] <= 10 &&
                   dm32_imbe_decode(&v->codec, d.imbe[i], pcm);
            elapsed=v->micros?v->micros()-at:0;
            if(elapsed>v->imbe_max_us)v->imbe_max_us=elapsed;
            if(!ok) {
                memset(pcm, 0, sizeof pcm);
                v->rx_erasures++;
                dm32_imbe_init(&v->codec);
            } else v->rx_voice++;
        }
        if (v->sink) v->sink(v->sink_ctx, pcm);
    }
}

static p25_voice_packet *next_packet(p25_voice *v)
{
    return v->count < P25_VOICE_QUEUE ? &v->queue[v->head] : NULL;
}

static void commit_packet(p25_voice *v, uint16_t bytes)
{
    v->queue[v->head].bytes = bytes;
    v->head = (uint8_t)((v->head + 1) % P25_VOICE_QUEUE);
    v->count++;
    v->tx_frames++;
}

void p25_voice_tx_abort(p25_voice *v)
{
    v->transmitting = v->started = v->stopping = v->terminator_pending = 0;
    v->count = v->head = v->tail = v->voice_count = v->flush_remaining = 0;
    v->packet_symbol = 0;
#if CONFIG_P25_ENCRYPTION
    memset(&v->tx_sync, 0, sizeof v->tx_sync);
#endif
    p25_voice_rx_reset(v);
}

static int fail(p25_voice *v)
{
    p25_voice_tx_abort(v);
    v->fault = 1;
    return 0;
}

#if CONFIG_P25_ENCRYPTION
int p25_voice_tx_begin_secure(p25_voice *v, const p25_call *call, const p25_encryption_sync *sync)
{
    if (!call || v->transmitting || !call->talkgroup ||
        !call->source_id || call->source_id > 9999999u) return 0;
    p25_voice_tx_abort(v);
    v->fault = 0;
    if (sync) {
        uint8_t any = 0;
        for (unsigned i = 0; i < 8; i++) any |= sync->mi[i];
        if (!any || !p25_crypto_key_bytes(sync->algorithm) || sync->algorithm != v->crypto_algorithm ||
            sync->kid != v->crypto_key_id || p25_secure_header(call, sync, v->queue[0].data))
            return 0;
        v->tx_sync = *sync;
        p25_crypto_stream(v->crypto_stream, v->crypto_key, sync->mi, sync->algorithm);
    } else if (p25_encode_hdu(call, v->queue[0].data)) return 0;
    v->call = *call;
    v->transmitting = 1;
    v->next_duid = P25_DUID_LDU1;
    commit_packet(v, P25_HDU_BYTES);
    return 1;
}

int p25_voice_tx_begin(p25_voice *v, const p25_call *call)
{
    return p25_voice_tx_begin_secure(v, call, NULL);
}
#else
int p25_voice_tx_begin(p25_voice *v, const p25_call *call)
{
    if (!call || v->transmitting || !call->talkgroup ||
        !call->source_id || call->source_id > 9999999u) return 0;
    p25_voice_tx_abort(v);
    v->fault = 0;
    if (p25_encode_hdu(call, v->queue[0].data)) return 0;
    v->call = *call;
    v->transmitting = 1;
    v->next_duid = P25_DUID_LDU1;
    commit_packet(v, P25_HDU_BYTES);
    return 1;
}

#endif

static int encode_frame(p25_voice *v, int16_t pcm[160])
{
    if (!dm32_imbe_encode(&v->codec, pcm, v->voice[v->voice_count])) return fail(v);
    if (++v->voice_count < 9) return 1;
    p25_voice_packet *p = next_packet(v);
    uint8_t lsd[2] = {0, 0};
    if (!p) return fail(v);
#if CONFIG_P25_ENCRYPTION
    if (v->tx_sync.algorithm) {
        for (unsigned i = 0; i < 9; i++)
            v->voice[i][7] = (uint16_t)((v->voice[i][7] & ~1u) |
                ((i + (v->next_duid == P25_DUID_LDU2)) & 1u));
        p25_crypto_voice(v->crypto_stream, v->tx_sync.algorithm, v->next_duid, v->voice, lsd);
        if (v->next_duid == P25_DUID_LDU2) {
            p25_aes_next_mi(v->tx_sync.mi);
            uint8_t allocated[9];
            if (!v->advance_mi || !v->advance_mi(allocated) ||
                memcmp(allocated, v->tx_sync.mi, sizeof allocated)) return fail(v);
        }
        if (p25_secure_ldu(&v->call, &v->tx_sync, v->next_duid, v->voice, lsd, p->data)) return fail(v);
        if (v->next_duid == P25_DUID_LDU2)
            p25_crypto_stream(v->crypto_stream, v->crypto_key, v->tx_sync.mi, v->tx_sync.algorithm);
    } else
#endif
    if (p25_encode_ldu(&v->call, v->next_duid, v->voice, lsd, p->data)) return fail(v);
    commit_packet(v, P25_LDU_BYTES);
    v->voice_count = 0;
    v->next_duid ^= P25_DUID_LDU1 ^ P25_DUID_LDU2;
    v->started = 1;
    return 1;
}

int p25_voice_tx_pcm(p25_voice *v, int16_t pcm[160])
{
    if (!v->transmitting || v->stopping || !pcm) return 0;
    return encode_frame(v, pcm);
}

static void append_terminator(p25_voice *v)
{
    p25_voice_packet *p = next_packet(v);
    if (v->terminator_pending && !v->flush_remaining && !v->voice_count && p) {
        if (p25_encode_tdu(&v->call, p->data)) { fail(v); return; }
        commit_packet(v, P25_TDU_BYTES);
        v->terminator_pending = 0;
    }
}

/* Finishing owns the remaining codec tail and LDU padding. A full queue is
 * backpressure here: wait before the ninth encode, so neither codec history
 * nor the remaining-tail count advances until its LDU can be committed. */
static void finish_call(p25_voice *v)
{
    if (!v->stopping) return;
    while (v->flush_remaining || v->voice_count) {
        if (v->voice_count == 8 && !next_packet(v)) return;
        int16_t silence[160] = {0};
        if (!encode_frame(v, silence)) return;
        if (v->flush_remaining) --v->flush_remaining;
    }
    append_terminator(v);
}

int p25_voice_tx_end(p25_voice *v)
{
    if (!v->transmitting || v->stopping) return 0;
    if (!v->started && !v->voice_count) { p25_voice_tx_abort(v); return 1; }
    v->stopping = 1;
    v->flush_remaining = DM32_IMBE_ENCODER_FLUSH_FRAMES;
    v->terminator_pending = 1;
    finish_call(v);
    return !v->fault;
}

size_t p25_voice_tx_read(p25_voice *v, uint8_t *out, size_t capacity)
{
    if (!v->transmitting || !v->started || !out || !capacity) return 0;
    size_t n = 0;
    while (n < capacity) {
        finish_call(v);
        if (!v->transmitting) break;
        if (!v->count) {
            if (v->stopping) { p25_voice_tx_abort(v); break; }
            v->tx_underruns++;
            fail(v);
            break;
        }
        p25_voice_packet *p = &v->queue[v->tail];
        unsigned bit = v->packet_symbol * 2u;
        out[n++] = (uint8_t)((p->data[bit / 8] >> (6 - bit % 8)) & 3u);
        if (++v->packet_symbol == p->bytes * 4u) {
            v->packet_symbol = 0;
            v->tail = (uint8_t)((v->tail + 1) % P25_VOICE_QUEUE);
            v->count--;
        }
    }
    /* A read ending exactly at the TDU boundary is done immediately. */
    if (v->transmitting && v->stopping && !v->count && !v->terminator_pending &&
        !v->flush_remaining && !v->voice_count)
        p25_voice_tx_abort(v);
    return n;
}

int p25_voice_tx_done(const p25_voice *v) { return !v->transmitting; }
