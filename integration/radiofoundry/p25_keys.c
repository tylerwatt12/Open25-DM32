/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#if CONFIG_P25_ENCRYPTION
#include "p25_keys.h"
#include "crypto_voice.h"
#include "hal_time.h"
#include "hal_system.h"
#include "storage/storage.h"
#include "debuglink/dl_frame.h"
#include <string.h>

/* DM32 gap between the settings journal and the CPS flash image. */
#define VAULT_FIRST 0x71b000u
#define VAULT_LAST_SECTOR 0x71f000u
#define VAULT_BLOCK 512u
#define MI_RESERVATION (1u << 24)

typedef struct {
    uint8_t next[8];
    uint8_t tag[16];
} mi_record;
_Static_assert(sizeof(p25_vault_image) + sizeof(mi_record) <= VAULT_BLOCK,
               "Vault and MI record must fit in one erased block");
/* 18 and 19 were cable-supplied wrapping keys and are retired. */
enum { INFO = 17, PUT = 20, REMOVE = 21, LOCK = 22, STAGE = 23, FINISH = 24 };
#define STAGE_MS 300000u
#define LOAD_MS 120000u

static uint8_t staged_salt[16];
static char pending_pin[P25_PIN_MAX];
static size_t pending_len;
static int creating, cable_requested, local_salt;
static volatile uint8_t pin_progress;
static uint32_t mi_remaining;
static volatile p25_pin_status pin_state;
static volatile uint32_t pin_since;
static uint8_t master[32], next_mi[9];
static p25_key_entry entries[P25_KEY_SLOTS];
static int unlocked;

static int layout_ok(void)
{
    const hal_flash_map_t *map = storage_map();
    return storage_ready() && map && map->journal_addr == 0x719000u &&
           map->openfw_flash == 0x720000u && board.flash && board.flash->read;
}

static int block_blank(uint32_t addr)
{
    uint8_t data[64];
    for (unsigned offset = 0; offset < VAULT_BLOCK; offset += sizeof data) {
        if (!board.flash->read(addr + offset, data, sizeof data)) return 0;
        for (unsigned i = 0; i < sizeof data; i++) if (data[i] != 0xff) return 0;
    }
    return 1;
}

/* Check all aligned blocks for an existing vault before using a blank block.
 * This includes the original sector-aligned vaults. The journal preserves the
 * surrounding bytes when it patches the selected block. */
static uint32_t locate(p25_vault_image *image, int *exists, int find_blank)
{
    uint32_t blank = 0;
    for (uint32_t sector = VAULT_LAST_SECTOR; sector >= VAULT_FIRST; sector -= STORAGE_SECTOR) {
        for (unsigned offset = 0; offset < STORAGE_SECTOR; offset += VAULT_BLOCK) {
            uint32_t addr = sector + offset;
            if (!board.flash->read(addr, image->magic, sizeof image->magic)) return 0;
            if (p25_vault_format(image)) {
                if (!board.flash->read(addr, image, sizeof *image)) return 0;
                if (p25_vault_format(image)) {
                    *exists = 1;
                    return addr;
                }
            }
            if (find_blank && !blank && block_blank(addr)) blank = addr;
        }
    }
    *exists = 0;
    return blank;
}

static void lock(void)
{
    unlocked = 0;
    mi_remaining = 0;
    p25_vault_erase(master, sizeof master);
    p25_vault_erase(next_mi, sizeof next_mi);
    p25_vault_erase(entries, sizeof entries);
}

const p25_key_entry *p25_keys_get(unsigned slot)
{
    return unlocked && slot && slot <= P25_KEY_SLOTS && entries[slot - 1].algorithm ?
        &entries[slot - 1] : NULL;
}

int p25_keys_tx_ready(void)
{
    return unlocked && mi_remaining != 0;
}

int p25_keys_next_mi(uint8_t mi[9])
{
    if (!p25_keys_tx_ready()) return 0;
    mi_remaining--;
    memcpy(mi, next_mi, sizeof next_mi);
    p25_aes_next_mi(next_mi);
    return 1;
}

static int nonzero_seed(const uint8_t *seed)
{
    uint8_t any = 0;
    for (unsigned i = 0; i < 8; i++) any |= seed[i];
    return any != 0;
}

/* T is the linear MI step in p25_aes_next_mi. Reducing T^(2^24)
 * in the basis {1, T, ..., T^63} gives this polynomial. */
static void reserve_end(uint8_t next[8], const uint8_t start[8])
{
    const uint64_t polynomial = UINT64_C(0x2944400000ed1302);
    uint8_t state[9] = {0};
    memcpy(state, start, 8);
    memset(next, 0, 8);
    for (unsigned bit = 0; bit < 64; bit++) {
        if ((polynomial >> bit) & 1u) {
            for (unsigned i = 0; i < 8; i++) next[i] ^= state[i];
        }
        p25_aes_next_mi(state);
    }
}

static int reserve(uint32_t addr, const p25_vault_image *image,
                   const uint8_t key[32], uint8_t start[8])
{
    mi_record record;
    uint32_t record_addr = addr + sizeof *image;
    if (!board.flash->read(record_addr, &record, sizeof record)) return 0;
    const uint8_t *bytes = (const uint8_t *)&record;
    uint8_t erased = 0xff;
    for (unsigned i = 0; i < sizeof record; i++) erased &= bytes[i];
    if (erased == 0xff) {
        /* Legacy vaults have an erased reservation record. */
        p25_vault_mi_seed(start, image, key);
        if (!nonzero_seed(start)) start[7] = 1;
    } else {
        uint8_t expected[16];
        p25_vault_mi_tag(expected, record.next, key);
        if (!nonzero_seed(record.next) || memcmp(expected, record.tag, sizeof expected))
            return 0;
        memcpy(start, record.next, 8);
    }

    reserve_end(record.next, start);
    p25_vault_mi_tag(record.tag, record.next, key);
    /* Commit the next range before exposing the current one. A reboot can
     * discard unused MIs, but may never resume an already allocated range. */
    return storage_write(record_addr, &record, sizeof record);
}

static int unlock(uint32_t addr, const p25_vault_image *image, const uint8_t key[32])
{
    uint8_t seed[8];
    if (!p25_vault_matches(image, key) || !reserve(addr, image, key, seed)) return 0;
    lock();
    memcpy(master, key, sizeof master);
    memcpy(next_mi, seed, sizeof seed);
    for (unsigned i = 0; i < P25_KEY_SLOTS; i++)
        p25_vault_open(image, master, i + 1, entries + i);
    mi_remaining = MI_RESERVATION;
    unlocked = 1;
    return 1;
}

p25_pin_status p25_keys_pin_status(void)
{
    p25_pin_status current = pin_state;
    if ((current == P25_PIN_NEW || current == P25_PIN_ENTER) &&
        hal_millis() - pin_since >= STAGE_MS)
        return unlocked ? P25_PIN_UNLOCKED : P25_PIN_IDLE;
    if (current == P25_PIN_READY && hal_millis() - pin_since >= LOAD_MS)
        return P25_PIN_UNLOCKED;
    return current;
}

static void prepare(const uint8_t salt[16], int new_vault, int cable)
{
    memcpy(staged_salt, salt, sizeof staged_salt);
    local_salt = 0;
    creating = new_vault;
    cable_requested = cable;
    pin_since = hal_millis();
    pin_state = new_vault ? P25_PIN_NEW : P25_PIN_ENTER;
}

void p25_keys_pin_prepare(void)
{
    p25_pin_status status = p25_keys_pin_status();
    if (status == P25_PIN_NEW || status == P25_PIN_ENTER ||
        status == P25_PIN_WORKING || status == P25_PIN_READY) return;
    if (p25_keys_tx_ready()) {
        pin_state = P25_PIN_UNLOCKED;
        return;
    }
    if (!layout_ok()) return;
    storage_lock();
    p25_vault_image image;
    int exists = 0;
    uint32_t addr = locate(&image, &exists, 1);
    if (addr && exists) {
        prepare(image.salt, 0, 0);
    } else if (addr) {
        /* The flash UID makes salts distinct across radios. The timestamp at
         * confirmation varies the salt when creating a new vault on this radio. */
        hal_chip_t chip;
        if (board.system && board.system->chip && board.system->chip(&chip)) {
            uint8_t salt[16] = {0};
            memcpy(salt, chip.uid, sizeof chip.uid);
            prepare(salt, 1, 1);
            local_salt = 1;
        } else {
            pin_state = P25_PIN_NO_ID;
        }
    } else {
        pin_state = P25_PIN_NO_SPACE;
    }
    storage_unlock();
}

int p25_keys_pin_submit(const char *pin, size_t len)
{
    p25_pin_status current = p25_keys_pin_status();
    if ((current != P25_PIN_NEW && current != P25_PIN_ENTER) ||
        len < P25_PIN_MIN || len > P25_PIN_MAX) return 0;
    for (size_t i = 0; i < len; i++) {
        if (pin[i] < '0' || pin[i] > '9') return 0;
    }
    if (local_salt) {
        uint32_t time = hal_micros();
        memcpy(staged_salt + 12, &time, sizeof time);
    }
    pin_progress = 0;
    memcpy(pending_pin, pin, len);
    pending_len = len;
    pin_state = P25_PIN_WORKING;
    return 1;
}

uint8_t p25_keys_pin_progress(void)
{
    return pin_progress;
}

void p25_keys_pin_run(void (*yield)(void))
{
    uint8_t key[32];
    p25_pin_derive(pending_pin, pending_len, staged_salt, key, yield, &pin_progress);
    p25_vault_erase(pending_pin, sizeof pending_pin);
    pending_len = 0;

    int ok = 0;
    if (layout_ok()) {
        storage_lock();
        p25_vault_image image;
        int exists = 0;
        uint32_t addr = locate(&image, &exists, creating);
        if (addr && creating && !exists) {
            p25_vault_create(&image, staged_salt, key);
            ok = storage_write(addr, &image, sizeof image) && unlock(addr, &image, key);
        } else if (addr && !creating && exists &&
                   memcmp(image.salt, staged_salt, sizeof staged_salt) == 0) {
            ok = unlock(addr, &image, key);
        }
        storage_unlock();
    }
    p25_vault_erase(key, sizeof key);
    pin_since = hal_millis();
    pin_state = ok ? (cable_requested ? P25_PIN_READY : P25_PIN_UNLOCKED) : P25_PIN_FAILED;
}

int p25_keys_command(const uint8_t *req, uint16_t len, uint8_t *out, size_t cap)
{
    if ((req[0] == LOCK || req[0] == FINISH) && len == 1) {
        if (req[0] == LOCK) lock();
        pin_state = unlocked ? P25_PIN_UNLOCKED : P25_PIN_IDLE;
        out[0] = DL_ST_OK;
        return 1;
    }
    out[0] = DL_ST_REFUSED;
    if (!layout_ok()) return 1;
    storage_lock();
    p25_vault_image image;
    int exists = 0;
    uint32_t addr = locate(&image, &exists, req[0] == STAGE);
    int count = 1;
    if (req[0] == INFO && len == 1 && cap >= 4 + P25_VAULT_SALT + 3 * P25_KEY_SLOTS) {
        out[0] = DL_ST_OK;
        out[1] = (uint8_t)exists;
        out[2] = (uint8_t)unlocked;
        memset(out + 3, 0, P25_VAULT_SALT + 3 * P25_KEY_SLOTS);
        if (exists) memcpy(out + 3, image.salt, P25_VAULT_SALT);
        for (unsigned i = 0; i < P25_KEY_SLOTS; i++) {
            const p25_key_entry *entry = p25_keys_get(i + 1);
            if (entry) {
                uint8_t *slot = out + 3 + P25_VAULT_SALT + i * 3;
                slot[0] = entry->algorithm;
                memcpy(slot + 1, entry->kid, 2);
            }
        }
        count = 3 + P25_VAULT_SALT + 3 * P25_KEY_SLOTS;
        out[count++] = (uint8_t)p25_keys_pin_status();
    } else if (req[0] == STAGE && (len == 17 || len == 25)) {
        p25_pin_status status = p25_keys_pin_status();
        if (addr) {
            if ((status == P25_PIN_NEW || status == P25_PIN_ENTER) && cable_requested)
                out[0] = DL_ST_OK;
            else {
                prepare(exists ? image.salt : req + 1, !exists, 1);
                out[0] = DL_ST_OK;
            }
        } else {
            pin_state = P25_PIN_NO_SPACE;
        }
    } else if ((req[0] == PUT || req[0] == REMOVE) && len >= 2 && exists && unlocked &&
               p25_keys_pin_status() == P25_PIN_READY &&
               req[1] && req[1] <= P25_KEY_SLOTS) {
        unsigned slot = req[1] - 1u;
        p25_key_entry entry = {0};
        int changed = 0;
        if (req[0] == REMOVE && len == 2) {
            memset(&image.slots[slot], 0, sizeof image.slots[slot]);
            changed = 1;
        } else if (req[0] == PUT && len >= 5) {
            unsigned length = p25_crypto_key_bytes(req[2]);
            if (length && len == 5 + length) {
                entry.algorithm = req[2];
                memcpy(entry.kid, req + 3, 2);
                memcpy(entry.data, req + 5, length);
                changed = p25_vault_seal(&image, master, slot + 1, &entry);
            }
        }
        if (changed && storage_write(addr, &image, sizeof image)) {
            p25_vault_erase(entries + slot, sizeof entry);
            entries[slot] = entry;
            out[0] = DL_ST_OK;
        }
        p25_vault_erase(&entry, sizeof entry);
    }
    storage_unlock();
    return count;
}
#endif
