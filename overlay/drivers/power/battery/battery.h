// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Open25-DM32 Contributors
// Battery voltage for display (hal_power_t.battery_mv, battery_pct): a running average of the board's readings, so TX
// load and ADC noise don't make the number jump, and a charge estimate between an empty and a full voltage.
//
// battery_avg_mv() takes a new reading at most every BATTERY_AVG_MS and averages over about 2^BATTERY_AVG_SHIFT of
// them; the first reading fills the average. Header only: the board keeps one battery_avg_t and its own scale.
// battery_avg_hold_mv() is the same for a board that knows when its transmitter loads the pack.
#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>
#include <stdbool.h>

#define BATTERY_AVG_MS		500u
#define BATTERY_AVG_SHIFT	3u

typedef struct {
	uint32_t sum;		// the average << BATTERY_AVG_SHIFT
	uint32_t ms;		// when the last reading was taken
	bool valid;
} battery_avg_t;

static inline uint16_t battery_avg_mv(battery_avg_t *a, uint32_t now_ms, uint32_t (*read_mv)(void))
{
	if (!a->valid || now_ms - a->ms >= BATTERY_AVG_MS) {
		uint32_t mv = read_mv();
		if (!a->valid) {
			a->sum = mv << BATTERY_AVG_SHIFT;
			a->valid = true;
		} else {
			a->sum += mv - (a->sum >> BATTERY_AVG_SHIFT);
		}
		a->ms = now_ms;
	}
	return (uint16_t)(a->sum >> BATTERY_AVG_SHIFT);
}

// A transmission is a load of seconds, not noise: the average follows the pack down and the charge shown falls with
// it, then comes back after (a 2S pack at high power: 100 % to under 80 %). The radio core's monitor takes no reading
// while keyed or for 3 s after (battery_poll), but the UI reads the charge by itself, into the same average. So the
// board says when it is loaded, and no reading is taken then or for BATTERY_LOAD_QUIET_MS after, while the pack
// recovers: the average stays as it was. The first reading is taken in any case (there is no average to keep)
#define BATTERY_LOAD_QUIET_MS	3000u

typedef struct {
	uint32_t ms;		// when the load was last seen
	bool seen;
} battery_hold_t;

static inline uint16_t battery_avg_hold_mv(battery_avg_t *a, battery_hold_t *h, uint32_t now_ms, bool loaded,
					   uint32_t (*read_mv)(void))
{
	if (loaded) {
		h->ms = now_ms;
		h->seen = true;
	}
	if (a->valid && h->seen) {
		if (now_ms - h->ms < BATTERY_LOAD_QUIET_MS) {
			return (uint16_t)(a->sum >> BATTERY_AVG_SHIFT);
		}
		h->seen = false;	// over: a load from before the millisecond counter wrapped must not hold again
	}
	return battery_avg_mv(a, now_ms, read_mv);
}

// 0 at or below empty_mv, 100 at or above full_mv, linear between
static inline uint8_t battery_linear_pct(uint32_t mv, uint32_t empty_mv, uint32_t full_mv)
{
	if (mv <= empty_mv) {
		return 0;
	}
	if (mv >= full_mv) {
		return 100;
	}
	return (uint8_t)((mv - empty_mv) * 100u / (full_mv - empty_mv));
}

#endif
