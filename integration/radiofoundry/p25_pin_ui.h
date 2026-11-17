/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Included by the Classic menu implementation. */
#include "p25/p25_keys.h"
#include "p25/p25_lab.h"

static char pin_input[P25_PIN_MAX], pin_confirm[P25_PIN_MAX];
static uint8_t pin_length, pin_confirm_length;

void ui_p25_pin_clear(void)
{
    p25_vault_erase(pin_input, sizeof pin_input);
    p25_vault_erase(pin_confirm, sizeof pin_confirm);
    pin_length = pin_confirm_length = 0;
}

static const char *pin_body(void)
{
    switch (p25_keys_pin_status()) {
    case P25_PIN_NEW: return pin_confirm_length ? "Repeat new PIN" : "New PIN (8-32 digits)";
    case P25_PIN_ENTER: return "Enter vault PIN";
    case P25_PIN_WORKING: {
        static char progress[24];
        sbuf_t b;
        sb_init(&b, progress, sizeof progress);
        sb_str(&b, "Checking PIN ");
        sb_u32(&b, p25_keys_pin_progress(), 1);
        sb_chr(&b, '%');
        return progress;
    }
    case P25_PIN_READY: return "Unlocked. Load via cable.";
    case P25_PIN_FAILED: return "Unlock failed. Back to retry.";
    case P25_PIN_UNLOCKED: return "Vault unlocked";
    case P25_PIN_NO_SPACE: return "No erased vault block";
    case P25_PIN_NO_ID: return "No flash ID. Run start on PC.";
    default: return "To set up, run start on PC.";
    }
}

static uint16_t pin_count(void)
{
    p25_pin_status status = p25_keys_pin_status();
    return status == P25_PIN_NEW || status == P25_PIN_ENTER ? 1 : 0;
}

static void pin_item(uint16_t row, ui_item_t *it)
{
    (void)row;
    sbuf_t b;
    sb_init(&b, it->label, sizeof it->label);
    for (unsigned i = 0; i < pin_length && i < 12; i++) sb_chr(&b, '*');
    sb_chr(&b, ' ');
    sb_u32(&b, pin_length, 1);
    sb_str(&b, " digits");
}

static void pin_accept(p25_pin_status status)
{
    if (pin_length < P25_PIN_MIN) {
        ui_toast("Use at least 8 digits");
        return;
    }
    if (status == P25_PIN_NEW && !pin_confirm_length) {
        memcpy(pin_confirm, pin_input, pin_length);
        pin_confirm_length = pin_length;
        p25_vault_erase(pin_input, sizeof pin_input);
        pin_length = 0;
        return;
    }
    if (status == P25_PIN_NEW && (pin_length != pin_confirm_length ||
        memcmp(pin_input, pin_confirm, pin_length) != 0)) {
        ui_p25_pin_clear();
        ui_toast("PINs differ. Try again.");
        return;
    }
    int accepted = p25_lab_pin_submit(pin_input, pin_length);
    ui_p25_pin_clear();
    if (!accepted) ui_toast("Radio busy. Try again.");
}

static bool pin_key(const input_event_t *ev)
{
    if (ev->gesture != GESTURE_PRESS) return true;
    if (ev->action == ACT_BACK) {
        ui_pop();
        return true;
    }
    p25_pin_status status = p25_keys_pin_status();
    if (status != P25_PIN_NEW && status != P25_PIN_ENTER) return true;
    if (ev->action >= ACT_DIGIT_0 && ev->action <= ACT_DIGIT_9) {
        if (pin_length < P25_PIN_MAX)
            pin_input[pin_length++] = (char)('0' + ev->action - ACT_DIGIT_0);
    } else if (ev->action == ACT_QUICK || ev->action == ACT_NAV_LEFT) {
        if (pin_length) pin_input[--pin_length] = 0;
    } else if (ev->action == ACT_SELECT) {
        pin_accept(status);
    }
    ui_dirty();
    return true;
}

static void pin_enter(void)
{
    ui_p25_pin_clear();
    if (!p25_lab_pin_prepare()) ui_toast("Radio busy. Try again.");
}

static const ui_scr_def_t p25_pin_screen = {
    .name = "p25-pin", .flags = SF_LIST, .prefix = "P25 Keyloader",
    .hint = "* Delete  OK  Back", .body = pin_body, .count = pin_count,
    .item = pin_item, .key = pin_key, .enter = pin_enter,
};
