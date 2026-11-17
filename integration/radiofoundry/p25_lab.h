/* SPDX-License-Identifier: MIT AND GPL-3.0-or-later */
/* Original code retains MIT terms. Encryption additions retain GPL-3.0-or-later. */
#ifndef P25_LAB_H
#define P25_LAB_H
#include <stddef.h>
#include <stdint.h>
void p25_lab_start(void);
int p25_lab_busy(void);
size_t p25_lab_command(const uint8_t *req, uint16_t len, uint8_t *out, size_t cap);
#if defined(CONFIG_DEV_TX) && CONFIG_DEV_TX
int p25_lab_live_begin(uint32_t session);
int p25_lab_channel_rx(uint32_t frequency_hz);
void p25_lab_live_up(void);
/* Native RF-switch edge, ISR-safe; only marks the corresponding mic block. */
void p25_lab_live_key(void);
#if CONFIG_P25_ENCRYPTION
void p25_lab_set_key_slot(uint8_t slot, uint8_t algorithm);
int p25_lab_encryption_selected(uint8_t slot);
int p25_lab_encryption_on(void);
int p25_lab_toggle_encryption(void);
int p25_lab_key_ready(uint8_t slot, uint8_t algorithm);
int p25_lab_channel_mismatch(void);
int p25_lab_pin_prepare(void);
int p25_lab_pin_submit(const char *pin, size_t len);
#endif
#if CONFIG_P25_CHANNELS
/* Worker input snapshot; changed only after the previous worker has stopped. */
int p25_lab_set_channel(uint16_t rx_nac, uint16_t tx_nac, uint16_t talkgroup, uint32_t radio_id);
void p25_lab_channel_stop(void);
int p25_lab_channel_receiving(void);
#endif
#endif
#endif
