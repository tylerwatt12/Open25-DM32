// SPDX-License-Identifier: GPL-3.0-or-later
#include <assert.h>
#include <stdio.h>
#include "codec_slot.h"

static union { uint32_t alignment; uint8_t bytes[0x18000]; } storage;
static void checksum(codec_slot_t *s) {
    s->crc = codec_slot_crc32(storage.bytes + s->code_off, s->code_len, 0);
    s->crc = codec_slot_crc32(storage.bytes + s->tab_off, s->tab_len, s->crc);
    s->crc = codec_slot_crc32(storage.bytes + s->rel_off, 4 * s->rel_count, s->crc);
}
int main(void) {
    const uint32_t table_starts[] = {0x18000e48, 0x18000ea8, 0x18000eb0, 0x18000ec0};
    codec_windows_t current = {0x43a00, 0x58000, 0x18000e48, CODEC_TABLE_END};
    codec_windows_t legacy = current; legacy.tab_hi = 0x18003e28;
    assert(current.tab_hi == 0x18003e38);
    for (unsigned i = 0; i < sizeof table_starts / sizeof *table_starts; i++) {
        memset(&storage, 0, sizeof storage);
        codec_slot_t *s = (codec_slot_t *)storage.bytes;
        s->magic = CODEC_SLOT_MAGIC; s->version = CODEC_SLOT_V2;
        s->code_off = 128; s->code_len = 0x14600; s->code_ram = 0x16cdc;
        s->tab_off = s->code_off + s->code_len; s->tab_len = 0x2f78; s->tab_ram = table_starts[i];
        s->rel_off = s->tab_off + s->tab_len; s->rel_count = 1;
        uint32_t off = 8, pointer = s->code_ram + 12;
        memcpy(storage.bytes + s->rel_off, &off, 4);
        memcpy(storage.bytes + s->code_off + off, &pointer, 4);
        checksum(s); uint32_t run;
        assert(codec_slot_check(storage.bytes, sizeof storage.bytes, &current, &run));
        assert(run == current.run_lo);
        assert(codec_slot_check(storage.bytes, sizeof storage.bytes, &legacy, &run) == (i != 3));
        s->tab_ram = current.tab_hi - s->tab_len + 1;
        assert(!codec_slot_check(storage.bytes, sizeof storage.bytes, &current, &run));
        s->tab_ram = table_starts[i]; s->decode = s->code_len;
        assert(!codec_slot_check(storage.bytes, sizeof storage.bytes, &current, &run));
        s->decode = 0; pointer = 0; memcpy(storage.bytes + s->code_off + off, &pointer, 4); checksum(s);
        assert(!codec_slot_check(storage.bytes, sizeof storage.bytes, &current, &run));
        pointer = s->code_ram; memcpy(storage.bytes + s->code_off + off, &pointer, 4); checksum(s);
        assert(codec_slot_check(storage.bytes, sizeof storage.bytes, &current, &run));
        storage.bytes[s->tab_off] ^= 1;
        assert(!codec_slot_check(storage.bytes, sizeof storage.bytes, &current, &run));
    }
    puts("Codec slot: all DM32 table windows, NRFB .050 boundary, legacy refusal, entries, relocations and CRC PASS");
}
