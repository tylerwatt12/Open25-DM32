// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Open25-DM32 Contributors
// The battery average and charge estimate (drivers/power/battery): the first reading fills the average, readings come
// at most every BATTERY_AVG_MS, a step moves the average by 1/8 per reading, and the percent's ends and middle; no
// reading while the board is loaded (a transmission) or just after
#include <stdio.h>
#include "power/battery/battery.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static uint32_t reading = 8000, reads;

static uint32_t read_mv(void)
{
	reads++;
	return reading;
}

int main(void)
{
	battery_avg_t a = { 0 };
	CHECK(battery_avg_mv(&a, 0, read_mv) == 8000 && reads == 1);
	reading = 7200;
	CHECK(battery_avg_mv(&a, BATTERY_AVG_MS - 1, read_mv) == 8000 && reads == 1);	// too soon: no new reading
	CHECK(battery_avg_mv(&a, BATTERY_AVG_MS, read_mv) == 7900 && reads == 2);	// 8000 - 800 / 8
	uint32_t t = BATTERY_AVG_MS;
	for (int i = 0; i < 60; i++) {
		t += BATTERY_AVG_MS;
		battery_avg_mv(&a, t, read_mv);
	}
	CHECK(battery_avg_mv(&a, t, read_mv) >= 7200 && battery_avg_mv(&a, t, read_mv) <= 7208);
	// the millisecond counter wraps between two readings
	battery_avg_t w = { 0 };
	reading = 7000;
	battery_avg_mv(&w, 0xFFFFFF00u, read_mv);
	reading = 7800;
	CHECK(battery_avg_mv(&w, 0xFFFFFF00u + BATTERY_AVG_MS, read_mv) == 7100);

	CHECK(battery_linear_pct(6000, 6400, 8200) == 0 && battery_linear_pct(6400, 6400, 8200) == 0);
	CHECK(battery_linear_pct(7300, 6400, 8200) == 50 && battery_linear_pct(8200, 6400, 8200) == 100);
	CHECK(battery_linear_pct(9000, 6400, 8200) == 100);

	// A transmission: a full 2S pack reads 400 mV low while the PA loads it. The status strip asks for the charge all
	// through the transmission, so the plain average takes a reading every BATTERY_AVG_MS and follows the pack down:
	// the charge shown fell from 100 % to under 80 % within seconds of keying and came back after
	battery_avg_t p = { 0 };
	reading = 8200;
	t = 0;
	CHECK(battery_linear_pct(battery_avg_mv(&p, t, read_mv), 6400, 8200) == 100);
	reading = 7800;
	for (int i = 0; i < 20; i++) {	// 10 s keyed
		t += BATTERY_AVG_MS;
		battery_avg_mv(&p, t, read_mv);
	}
	CHECK(battery_linear_pct(battery_avg_mv(&p, t, read_mv), 6400, 8200) < 80);

	// The hold: no reading while loaded, the average as it was
	battery_avg_t a2 = { 0 };
	battery_hold_t h = { 0 };
	reading = 8200;
	t = 1000;
	CHECK(battery_avg_hold_mv(&a2, &h, t, false, read_mv) == 8200);
	reading = 7800;
	uint32_t n = reads;
	for (int i = 0; i < 20; i++) {
		t += BATTERY_AVG_MS;
		CHECK(battery_avg_hold_mv(&a2, &h, t, true, read_mv) == 8200);
	}
	CHECK(reads == n && battery_linear_pct(battery_avg_hold_mv(&a2, &h, t, true, read_mv), 6400, 8200) == 100);
	// unkeyed, the pack still coming back: none for the quiet time either
	reading = 8000;
	CHECK(battery_avg_hold_mv(&a2, &h, t + BATTERY_LOAD_QUIET_MS - 1, false, read_mv) == 8200 && reads == n);
	// then readings again, at the average's own pace
	reading = 8120;
	t += BATTERY_LOAD_QUIET_MS;
	CHECK(battery_avg_hold_mv(&a2, &h, t, false, read_mv) == 8190 && reads == n + 1);	// 8200 - 80 / 8
	CHECK(battery_avg_hold_mv(&a2, &h, t + BATTERY_AVG_MS - 1, false, read_mv) == 8190 && reads == n + 1);
	CHECK(battery_avg_hold_mv(&a2, &h, t + BATTERY_AVG_MS, false, read_mv) == 8181 && reads == n + 2);
	// keyed again inside the quiet time: the hold starts over from the last time the load was seen
	reading = 7800;
	t += BATTERY_AVG_MS;
	CHECK(battery_avg_hold_mv(&a2, &h, t + 100, true, read_mv) == 8181);
	CHECK(battery_avg_hold_mv(&a2, &h, t + 100 + BATTERY_LOAD_QUIET_MS - 1, false, read_mv) == 8181 && reads == n + 2);

	// The first reading is taken even while loaded: there is no average to keep
	battery_avg_t f = { 0 };
	battery_hold_t fh = { 0 };
	reading = 7700;
	CHECK(battery_avg_hold_mv(&f, &fh, 5, true, read_mv) == 7700);
	reading = 8100;
	CHECK(battery_avg_hold_mv(&f, &fh, 5 + BATTERY_AVG_MS, true, read_mv) == 7700);	// held from here on

	// The millisecond counter wraps: a load seen just before the wrap holds across it, and one long over doesn't
	// hold again when the counter comes back round to its time
	battery_avg_t wa = { 0 };
	battery_hold_t wh = { 0 };
	reading = 8000;
	battery_avg_hold_mv(&wa, &wh, 0xFFFFF000u, false, read_mv);
	reading = 7600;
	CHECK(battery_avg_hold_mv(&wa, &wh, 0xFFFFFF00u, true, read_mv) == 8000);
	CHECK(battery_avg_hold_mv(&wa, &wh, 0xFFFFFF00u + BATTERY_LOAD_QUIET_MS - 1, false, read_mv) == 8000);	// past 0
	reading = 8000;
	CHECK(battery_avg_hold_mv(&wa, &wh, 0xFFFFFF00u + BATTERY_LOAD_QUIET_MS, false, read_mv) == 8000 && !wh.seen);
	reading = 7200;
	n = reads;
	CHECK(battery_avg_hold_mv(&wa, &wh, 0xFFFFFF00u + 10, false, read_mv) == 7900 && reads == n + 1);	// 49.7 days on
	if (failures) {
		printf("batteryTest: %d failures\n", failures);
		return 1;
	}
	printf("batteryTest: all passed\n");
	return 0;
}
