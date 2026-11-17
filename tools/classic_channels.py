# SPDX-License-Identifier: MIT
# Portions Copyright (c) 2026 The RadioFoundry Authors; see LICENSES/RadioFoundry-MIT.txt.
"""Classic channel fields, UI identity and display defaults."""

from pathlib import Path


def prepare(transform):
    # Replace the original splash through the pinned source's existing entry point.
    screens=(Path(__file__).resolve().parents[1]/'research/upstream/radiofoundry/core/ui/ui_screens.c').read_text()
    boot=screens[screens.index('// --- boot splash ---'):screens.index('// --- Home ---')]
    transform('core/settings/settings.c', [
        ('s->brightness = 100;', 's->brightness = 100;\n\ts->theme = 1; // Classic defaults to dark text on white.'),
    ])
    transform('tests/host/fd6818Test.c', [
        ('static void test_rx_tones(void)', '''static void test_tone_to_dmr(void)
{
\tconst fd6818_tone_t tones[] = { FD6818_TONE_CTCSS, FD6818_TONE_DCS, FD6818_TONE_DCS_INV };
\tconst uint16_t values[] = { 1000, 023, 023 };
\tconst uint16_t enabled[] = { 0x9045, 0x8045, 0xA045 };
\tconst fd6818_variant_t *variants[] = { &fd6818_variant_default, &fd6818_variant_b_dmr6943 };
\tfor (unsigned v = 0; v < 2; v++) for (unsigned t = 0; t < sizeof tones / sizeof tones[0]; t++) {
\t\tfd6818_t dev;
\t\treset_chip();fd6818_attach_variant(&dev, bus(), variants[v]);
\t\tfd6818_set_mode(&dev, FD6818_MODE_FM);
\t\tfd6818_set_freq(&dev, 146520000u);
\t\tfd6818_set_rx_tone(&dev, tones[t], values[t]);
\t\tCHECK(chip.regs[FD6818_REG_SUBAUDIO] == enabled[t]);
\t\tuint16_t frequency = chip.regs[FD6818_REG_CTCSS_FREQ], code = chip.regs[FD6818_REG_DCS_CODE];
\t\tchip.regs[0x55] = 0xA55A;chip.regs[0x66] = 0x5AA5;
\t\tfd6818_set_mode(&dev, FD6818_MODE_DMR);
\t\tCHECK(chip.regs[FD6818_REG_SUBAUDIO] == 0);
\t\tCHECK(chip.regs[FD6818_REG_CTCSS_FREQ] == frequency && chip.regs[FD6818_REG_DCS_CODE] == code);
\t\tCHECK(chip.regs[0x55] == 0xA55A && chip.regs[0x66] == 0x5AA5 && dev.freq_10hz == 14652000u);
\t\tfor (unsigned burst = 0; burst < 3; burst++) {
\t\t\tfd6818_dmr_burst(&dev, true, FD6818_BW_DMR);
\t\t\tCHECK(dev.state == FD6818_STATE_TX && !chip.regs[FD6818_REG_SUBAUDIO]);
\t\t\tfd6818_dmr_burst(&dev, false, FD6818_BW_DMR);
\t\t\tCHECK(dev.state == FD6818_STATE_RX && !chip.regs[FD6818_REG_SUBAUDIO]);
\t\t}
\t\tfd6818_set_mode(&dev, FD6818_MODE_FM);
\t\tfd6818_set_rx_tone(&dev, tones[t], values[t]);
\t\tCHECK(chip.regs[FD6818_REG_SUBAUDIO] == enabled[t]);
\t\tCHECK(chip.regs[FD6818_REG_CTCSS_FREQ] == frequency && chip.regs[FD6818_REG_DCS_CODE] == code);
\t\tCHECK(!chip.protocol_errors && !chip.crit_depth);
\t}
}

static void test_rx_tones(void)'''),
        ('\ttest_rx_tones();', '\ttest_rx_tones();\n\ttest_tone_to_dmr();'),
    ])
    transform('drivers/modem/hrc7000/hrc7000.c', [
        ('\t\thrc7000_wr(d, C7_SUBVOICE_COEF, (d->subvoice & ~0xFF00u) | ((uint32_t)dev << 8));', '''\t\t// Subaudio and DTMF share gain: preserve the PL level and signed DCS polarity.
\t\tif (hrc7000_rd(d, C7_FM_MOD) & (FM_MOD_CTCSS | FM_MOD_CDCSS))
\t\t\tdev = (uint8_t)(d->subvoice >> 8);
\t\thrc7000_wr(d, C7_SUBVOICE_COEF, (d->subvoice & ~0xFF00u) | ((uint32_t)dev << 8));'''),
    ])
    transform('tests/host/hrc7000DmrTest.c', [
        ('static void test_subtone(void)', '''static void test_dtmf_with_subtone(void)
{
\tconst hrc7000_tone_t tones[] = { HRC7000_TONE_NONE, HRC7000_TONE_CTCSS,
\t\tHRC7000_TONE_DCS, HRC7000_TONE_DCS_INV };
\tfor (unsigned t = 0; t < sizeof tones / sizeof tones[0]; t++) {
\t\tfor (unsigned wide = 0; wide < 2; wide++) {
\t\t\tsetup();
\t\t\tdev.subaudio_dev = 0x28;
\t\t\tREG(C7_SUBVOICE_COEF) = 0x07D02830u;
\t\t\thrc7000_set_subtone(&dev, true, tones[t], t == 1 ? 1000 : 0023);
\t\t\thrc7000_fm_tx(&dev, wide != 0);
\t\t\tuint32_t saved = REG(C7_SUBVOICE_COEF), mod = REG(C7_FM_MOD);
\t\t\tuint8_t requested = hrc7000_dtmf_dev(146520000u, wide != 0);
\t\t\tuint32_t sounding = t ? saved : (saved & ~0xFF00u) | ((uint32_t)requested << 8);
\t\t\tCHECK(hrc7000_tx_dtmf(&dev, '5', requested));
\t\t\tCHECK(REG(C7_SUBVOICE_COEF) == sounding && REG(C7_FM_MOD) == (mod | 2u));
\t\t\tif (t >= 2) CHECK((REG(C7_SUBVOICE_COEF) & 0x8000u) == 0);
\t\t\tCHECK(hrc7000_tx_dtmf(&dev, '9', requested));
\t\t\tCHECK(REG(C7_SUBVOICE_COEF) == sounding);
\t\t\tCHECK(hrc7000_tx_dtmf(&dev, 0, 0));
\t\t\tCHECK(REG(C7_SUBVOICE_COEF) == saved && REG(C7_FM_MOD) == mod && !dev.tone_on);
\t\t}
\t}
}

static void test_subtone(void)'''),
        ('\ttest_tx_tone();\n\ttest_subtone();', '\ttest_tx_tone();\n\ttest_dtmf_with_subtone();\n\ttest_subtone();'),
    ])
    transform('core/codeplug/codeplug.h', [
        ('typedef struct {\n\tchar name[CODEPLUG_NAME_LEN + 1];\n\tuint32_t rx_hz;', '''#if CONFIG_P25_CHANNELS
enum { RADIO_MODE_P25 = 2 };
#endif
typedef struct {
\tchar name[CODEPLUG_NAME_LEN + 1];
\tuint32_t rx_hz;'''),
        ('\tuint16_t contact;\t// talk target contact index, 0 = none\n\tuint16_t listen_list;\t// RX group list index, 0 = none\n\tuint32_t dmr_id;\t// own DMR ID for this channel, 0 = the radio\'s', '''#if CONFIG_P25_CHANNELS
	uint16_t contact, listen_list;
	uint32_t dmr_id;
	uint16_t p25_tg, p25_nac;
	uint8_t p25_squelch;
	uint32_t p25_id;
	uint16_t p25_tx_nac_override; /* zero inherits RX; otherwise TX NAC + 1 */
#else
	uint16_t contact, listen_list;
	uint32_t dmr_id;
#endif'''),
        ('} channel_t;', '''} channel_t;

static inline bool codeplug_mode_p25(uint8_t mode)
{
#if CONFIG_P25_CHANNELS
	return mode == RADIO_MODE_P25;
#else
	(void)mode;
	return false;
#endif
}
#if CONFIG_P25_CHANNELS
static inline uint16_t codeplug_p25_tx_nac(const channel_t *ch)
{
    if (!ch->p25_tx_nac_override) return ch->p25_nac;
    return (uint16_t)(ch->p25_tx_nac_override - 1u);
}
static inline bool codeplug_p25_valid(const channel_t *ch)
{
	uint16_t tx_nac=codeplug_p25_tx_nac(ch);
	return ch->bw == RADIO_BW_12K5 && ch->p25_nac <= 0xFFFu &&
	       ch->p25_nac != 0xF7Fu && ch->p25_id <= 0xFFFFFEu &&
	       (!ch->p25_tx_nac_override || (tx_nac < 0x1000u && tx_nac != 0xF7Eu && tx_nac != 0xF7Fu)) &&
	       (ch->rx_only || (tx_nac != 0xF7Eu && ch->p25_tg && ch->p25_id));
}
#endif'''),
    ])
    transform('core/codeplug/codeplug.c', [
        ('\tmemset(out, 0, sizeof(*out));\n\tif (!freq(&r[CH_RX]', '\tmemset(out, 0, sizeof(*out));\n#if CONFIG_P25_CHANNELS\n\tif (r[CH_MODE] != 0 && r[CH_MODE] != 1) return false;\n#endif\n\tif (!freq(&r[CH_RX]'),
        ('\tstorage_lock();\n\tbool ok = materialize() && flash_read(channel_addr(number), r, sizeof(r));', '''#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(ch->mode) && !codeplug_p25_valid(ch)) return false;
#endif
\tstorage_lock();
\tbool ok = materialize() && flash_read(channel_addr(number), r, sizeof(r));'''),
    ])
    transform('core/ui/ui_menus.c', [
        ('static char t[400];', 'static char t[640];'),
        ('sb_str(&b, "RadioFoundry\\n");', 'sb_str(&b, "Open25-DM32 Classic\\n");\n\tsb_str(&b, "Issues:\\ngithub.com/tylerwatt12/\\nOpen25-DM32/issues\\n");'),
        ('sb_str(&b, "MIT, (c) 2026 The RadioFoundry Authors\\n");',
         'sb_str(&b, "MIT, (c) 2026 Open25-DM32 contributors\\n");\n\tsb_str(&b, "Based on RadioFoundry\\n");\n\tsb_str(&b, "MIT, (c) 2026 The RadioFoundry Authors\\n");'),

        ('ui_toast(i == UI_CP_NEW ? TR("New codeplug started") : TR("Old codeplug kept: edit again"));',
         'ui_toast(i == UI_CP_NEW ? TR("Creating codeplug") : TR("Old codeplug kept: edit again"));'),
        ('static void contacts_item(uint16_t i, ui_item_t *it)', 'static void __attribute__((unused)) contacts_item(uint16_t i, ui_item_t *it)'),
        ('CE_ZONES, CE_GO, CE_SHARE, CE_SAVE, CE_SAVE_NEW, CE_COUNT', '#if CONFIG_P25_CHANNELS\n       CE_P25_TG, CE_P25_NAC, CE_P25_TX_NAC, CE_P25_ID,\n#endif\n       CE_ZONES, CE_GO, CE_SHARE, CE_SAVE, CE_SAVE_NEW, CE_COUNT'),
        ('[CE_SCAN] = CE_FIELD(scan_skip), [CE_ECO] = CE_FIELD(no_eco),', '[CE_SCAN] = CE_FIELD(scan_skip), [CE_ECO] = CE_FIELD(no_eco),\n#if CONFIG_P25_CHANNELS\n\t[CE_P25_TG] = CE_FIELD(p25_tg), [CE_P25_NAC] = CE_FIELD(p25_nac), [CE_P25_TX_NAC] = CE_FIELD(p25_tx_nac_override), [CE_P25_ID] = CE_FIELD(p25_id),\n#endif'),
        ('\t\tif ((fm_only && dmr) || (dmr_only && !dmr) || (r == CE_MODE && !CONFIG_DMR))', '#if CONFIG_P25_CHANNELS\n\t\tbool p25 = codeplug_mode_p25(ced_ch.mode);\n\t\tbool p25_only = r >= CE_P25_TG && r <= CE_P25_ID;\n\t\tif ((p25_only && !p25) || (p25 && (fm_only || dmr_only || r == CE_ECO))) continue;\n#endif\n\t\tif ((fm_only && dmr) || (dmr_only && !dmr) || (r == CE_MODE && !CONFIG_DMR))'),
        ('TRN("Battery saver"), TRN("Zones"), TRN("Tune to this ch"),', 'TRN("Battery saver"),\n#if CONFIG_P25_CHANNELS\n\t\tTRN("Talkgroup"), "RX NAC", "TX NAC", TRN("P25 Radio ID"),\n#endif\n\t\tTRN("Zones"), TRN("Tune to this ch"),'),
        ('sb_str(&wb, ch->mode == RADIO_MODE_DMR ? "DMR" : radio_am_channel(ch) ? "AM" : "FM");', 'sb_str(&wb, codeplug_mode_p25(ch->mode) ? "P25" : ch->mode == RADIO_MODE_DMR ? "DMR" : radio_am_channel(ch) ? "AM" : "FM");'),
        ('sb_str(&b, ch->mode == RADIO_MODE_DMR ? "DMR" : "FM");', 'sb_str(&b, codeplug_mode_p25(ch->mode) ? "P25" : ch->mode == RADIO_MODE_DMR ? "DMR" : "FM");'),
        ('if (editing && (r == CE_RX || r == CE_TX || r == CE_ID)) {', 'if (editing && (r == CE_RX || r == CE_TX || r == CE_ID\n#if CONFIG_P25_CHANNELS\n\t\t|| r == CE_P25_TG || r == CE_P25_ID\n#endif\n\t\t)) {'),
        ('if (r != CE_ID && k == UI_ENTRY_MHZ)', 'if ((r == CE_RX || r == CE_TX) && k == UI_ENTRY_MHZ)'),
        ('\tcase CE_NUMBER:\n\t\tsb_u32(&b, ced_num, 1);', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_TG: sb_u32(&b, ch->p25_tg, 1); break;\n\tcase CE_P25_ID: sb_u32(&b, ch->p25_id, 1); break;\n\tcase CE_P25_NAC:\n\tcase CE_P25_TX_NAC: {\n\t\tunsigned value = r==CE_P25_NAC ? ch->p25_nac : codeplug_p25_tx_nac(ch), cursor = 3;\n#if CONFIG_CHANNEL_EDIT\n\t\tif (editing) { value = (unsigned)ced_val; cursor = ced_len; }\n#endif\n        if (r==CE_P25_TX_NAC && (editing ? cursor==3 : !ch->p25_tx_nac_override)) {\n            sb_str(&b,TR("Same as RX")); break;\n        }\n\t\tsb_chr(&b, \'$\');\n\t\tfor (unsigned digit = 0; digit < 3; digit++) {\n\t\t\tif (digit == cursor) sb_chr(&b, \'[\');\n\t\t\tsb_chr(&b, "0123456789ABCDEF"[(value >> (4 * (2 - digit))) & 15u]);\n\t\t\tif (digit == cursor) sb_chr(&b, \']\');\n\t\t}\n\t\tbreak;\n\t}\n#endif\n\tcase CE_NUMBER:\n\t\tsb_u32(&b, ced_num, 1);'),
        ('\tif (CONFIG_DMR && c->mode == RADIO_MODE_DMR && c->contact == 0)', '#if CONFIG_P25_CHANNELS\n\tif (codeplug_mode_p25(c->mode) && !codeplug_p25_valid(c))\n\t\treturn TR("Check P25 NAC, talkgroup and ID");\n#endif\n\tif (CONFIG_DMR && c->mode == RADIO_MODE_DMR && c->contact == 0)'),
        ('\tcase CE_CC:\n\t\tced_val = (ced_val + dir + 16) % 16;', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC:\n\tcase CE_P25_TX_NAC: {\n        if (ced_len==3) ced_len=0;\n\t\tunsigned shift = 4u * (2u - ced_len);\n\t\tunsigned value = (((unsigned)ced_val >> shift) + (unsigned)dir) & 15u;\n\t\tced_val = (ced_val & ~(15u << shift)) | (value << shift);\n\t\tbreak;\n\t}\n#endif\n\tcase CE_CC:\n\t\tced_val = (ced_val + dir + 16) % 16;'),
        ('\tcase CE_ID:\n\t\tif (ced_len >= CE_DIGITS)', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC:\n\tcase CE_P25_TX_NAC: {\n        if (ced_len==3) ced_len=0;\n\t\tunsigned shift = 4u * (2u - ced_len);\n\t\tced_val = (ced_val & ~(15u << shift)) | ((unsigned)(d - \'0\') << shift);\n\t\tced_len = (uint8_t)((ced_len + 1) % 3);\n\t\tbreak;\n\t}\n\tcase CE_P25_TG:\n\tcase CE_P25_ID:\n\t\tif (v > (ced_edit == CE_P25_TG ? 65535 : 16777214)) {\n\t\t\tui_toast(TR("Value too large")); break;\n\t\t}\n        /* fall through */\n        __attribute__((fallthrough));\n#endif\n\tcase CE_ID:\n\t\tif (ced_len >= CE_DIGITS)'),
        ('\tcase CE_ID:\n\t\tif (ced_len > 0)', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_TG: if (ced_len) c->p25_tg = (uint16_t)ced_val; break;\n\tcase CE_P25_ID: if (ced_len) c->p25_id = (uint32_t)ced_val; break;\n\tcase CE_P25_NAC: c->p25_nac = (uint16_t)ced_val; break;\n    case CE_P25_TX_NAC: c->p25_tx_nac_override=ced_len==3 ? 0 : (uint16_t)(ced_val+1); break;\n#endif\n\tcase CE_ID:\n\t\tif (ced_len > 0)'),
        ('static bool chinfo_key(const input_event_t *ev)\n{\n\tbool press = ev->gesture == GESTURE_PRESS || ev->gesture == GESTURE_LONG;\n\tif (ced_edit == CE_NONE) {\n\t\tif (ev->action == ACT_BACK && ev->gesture == GESTURE_PRESS && ced_num != 0 &&\n\t\t\t\t(ced_new || ced_changed(CE_COUNT))) {\n\t\t\tdiscard_contact = false;\n\t\t\tui_push(SCR_DISCARD, 0);\n\t\t\treturn true;\n\t\t}\n\t\treturn false;\n\t}\n\tint dir = ev->action == ACT_NAV_UP ? 1 : ev->action == ACT_NAV_DOWN ? -1 :\n\t\tev->action == ACT_ENC_STEP ? (ev->value > 0 ? 1 : -1) : 0;\n\tif (dir != 0 && ev->gesture != GESTURE_UP) {', 'static bool chinfo_key(const input_event_t *ev)\n{\n\tbool press = ev->gesture == GESTURE_PRESS || ev->gesture == GESTURE_LONG;\n\tif (ced_edit == CE_NONE) {\n\t\tif (ev->action == ACT_BACK && ev->gesture == GESTURE_PRESS && ced_num != 0 &&\n\t\t\t\t(ced_new || ced_changed(CE_COUNT))) {\n\t\t\tdiscard_contact = false;\n\t\t\tui_push(SCR_DISCARD, 0);\n\t\t\treturn true;\n\t\t}\n\t\treturn false;\n\t}\n\tint dir = ev->action == ACT_NAV_UP ? 1 : ev->action == ACT_NAV_DOWN ? -1 :\n\t\tev->action == ACT_ENC_STEP ? (ev->value > 0 ? 1 : -1) : 0;\n#if CONFIG_P25_CHANNELS\n    if ((ced_edit == CE_P25_NAC || ced_edit == CE_P25_TX_NAC) && dir && ev->gesture != GESTURE_UP && ev->action != ACT_ENC_STEP) {\n        int positions=ced_edit==CE_P25_TX_NAC ? 4 : 3;\n        ced_len = (uint8_t)((ced_len + dir + positions) % positions);\n        ui_dirty(); return true;\n    }\n#endif\n\tif (dir != 0 && ev->gesture != GESTURE_UP) {'),
        ('if (ced_edit != CE_RX && ced_edit != CE_TX && ced_edit != CE_ID)', 'if (ced_edit != CE_RX && ced_edit != CE_TX && ced_edit != CE_ID\n#if CONFIG_P25_CHANNELS\n\t\t\t&& ced_edit != CE_P25_TG && ced_edit != CE_P25_ID\n#endif\n\t\t\t)'),
        ('\tcase CE_MODE: c->mode = c->mode == RADIO_MODE_DMR ? RADIO_MODE_FM : RADIO_MODE_DMR; break;', '\tcase CE_MODE:\n#if CONFIG_P25_CHANNELS\n\t\tif (c->mode == RADIO_MODE_DMR) {\n\t\t\tc->mode = RADIO_MODE_P25; c->p25_nac = 0x293; c->p25_tx_nac_override=0;\n\t\t\tc->p25_tg = 1; c->p25_squelch=1; c->p25_id = 1; c->dmr_id = 1; c->bw = RADIO_BW_12K5;\n\t\t\tc->dmo = c->roam = c->vox = false;\n\t\t} else if (codeplug_mode_p25(c->mode)) {\n\t\t\tc->mode = RADIO_MODE_FM; c->contact = c->listen_list = c->dmr_id = 0;\n\t\t} else\n#endif\n\t\t\tc->mode = c->mode == RADIO_MODE_DMR ? RADIO_MODE_FM : RADIO_MODE_DMR;\n\t\tbreak;\n#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC: ced_edit = r; ced_val = c->p25_nac; ced_len = 0; break;\n    case CE_P25_TX_NAC: ced_edit=r; ced_val=codeplug_p25_tx_nac(c); ced_len=c->p25_tx_nac_override ? 0 : 3; break;\n\tcase CE_P25_TG:\n\tcase CE_P25_ID:\n#endif'),
        ('ced_edit == CE_TX_TONE) && ced_len > 0)', 'ced_edit == CE_TX_TONE\n#if CONFIG_P25_CHANNELS\n             || ced_edit == CE_P25_TG || ced_edit == CE_P25_ID\n#endif\n             ) && ced_len > 0)'),
        # Editor-only defaults/confirmation; saved/imported programming is unchanged.
        ('static channel_t ced_orig;', 'static char ced_msg[64];\t// channel editor toast/confirmation text\nstatic channel_t ced_orig;'),
        ('static void ced_rx_band(channel_t *c)', '''static uint32_t ced_tx_hz(const channel_t *c)
{
    return c->dmo || !c->tx_hz ? c->rx_hz : c->tx_hz;
}

static bool ced_outside_ham(const channel_t *c)
{
    uint32_t hz = ced_tx_hz(c);
    return !((hz >= 144000000u && hz <= 148000000u) ||
             (hz >= 420000000u && hz <= 450000000u));
}

static void ced_rx_band(channel_t *c)'''),
        ('\tced_rx_band(&ced_ch);\n}', '\tced_rx_band(&ced_ch);\n\tif (ced_outside_ham(&ced_ch)) ced_ch.rx_only = true;\n}'),
        ('\t\tced_ch.roam = false;\n\t\tchar name', '\t\tced_ch.roam = false;\n\t\tif (ced_outside_ham(&ced_ch)) ced_ch.rx_only = true;\n\t\tchar name'),
        ('static void ced_save(bool as_new)\n{\n\tstatic char msg[32];',
         'static void ced_save(bool as_new)\n{\n\tif (as_new && ced_outside_ham(&ced_ch)) ced_ch.rx_only = true;'),
        ('sb_init(&b, msg, sizeof msg);\n\tsb_str(&b, as_new', 'sb_init(&b, ced_msg, sizeof ced_msg);\n\tsb_str(&b, as_new'),
        ('\tui_toast(msg);\n}\n\n// The tone typed', '\tui_toast(ced_msg);\n}\n\n// The tone typed'),
        ('static void ced_commit(void)\n{\n\tchannel_t *c = &ced_ch;',
         'static void ced_commit(void)\n{\n\tchannel_t *c = &ced_ch;\n\tuint32_t before = ced_tx_hz(c);'),
        ('\tced_edit = CE_NONE;\n}\n\nstatic bool chinfo_key',
         '\tif (ced_tx_hz(c) != before && ced_outside_ham(c)) c->rx_only = true;\n\tced_edit = CE_NONE;\n}\n\nstatic bool chinfo_key'),
        ('\tuint8_t r = ced_rows[i];\n\tswitch (r)', '\tuint8_t r = ced_rows[i];\n\tuint32_t before = ced_tx_hz(c);\n\tswitch (r)'),
        ('case CE_RX_ONLY: c->rx_only = !c->rx_only; break;', '''case CE_RX_ONLY:
        if (!c->rx_only) c->rx_only = true;
        else if (ced_outside_ham(c)) ui_push(SCR_CONFIRM, 1);
        else c->rx_only = false;
        break;'''),
        ('\tui_dirty();\n#else\n\t(void)i;\n\t(void)hold;\n\tchlist_tune',
         '\tif (ced_tx_hz(c) != before && ced_outside_ham(c)) c->rx_only = true;\n\tui_dirty();\n#else\n\t(void)i;\n\t(void)hold;\n\tchlist_tune'),
        ('static void confirm_item(uint16_t i, ui_item_t *it)\n{', '''static void confirm_item(uint16_t i, ui_item_t *it)
{
#if CONFIG_CHANNEL_EDIT
    if (ui_frame()->arg == 1) {
        str_copy(it->label, sizeof it->label, i == 0 ? TR("Keep RX-only") : TR("Enable TX"));
        if (i == 1) it->flags |= UI_ITEM_WARN;
        return;
    }
#endif'''),
        ('static void confirm_select(uint16_t i, bool hold)\n{\n\t(void)hold;', '''static void confirm_select(uint16_t i, bool hold)
{
    (void)hold;
#if CONFIG_CHANNEL_EDIT
    if (ui_frame()->arg == 1) {
        if (i == 1) ced_ch.rx_only = false;
        ui_pop();
        return;
    }
#endif'''),
        ('static const char *confirm_body(void)\n{', '''static const char *confirm_body(void)
{
#if CONFIG_CHANNEL_EDIT
    if (ui_frame()->arg == 1) {
        sbuf_t b;
        sb_init(&b, ced_msg, sizeof ced_msg);
        sb_str(&b, TR("Outside ham bands")); sb_str(&b, "\\nTX: ");
        sb_freq(&b, ced_tx_hz(&ced_ch)); sb_str(&b, " MHz\\n");
        sb_str(&b, TR("PTT may transmit."));
        return ced_msg;
    }
#endif'''),
        ('static const ui_scr_def_t confirm_screen = {', '''static const char *confirm_title(void)
{
#if CONFIG_CHANNEL_EDIT
    if (ui_frame()->arg == 1) return TR("Enable TX?");
#endif
    return TR("Factory Reset");
}

static const ui_scr_def_t confirm_screen = {'''),
        ('.prefix = TRN("Factory Reset"), .hint', '.title = confirm_title, .hint'),
    ])
    transform('core/ui/ui_screens.c', [
        (boot, r'''// --- boot splash ---

// Open25's wordmark and compact numeric mark use the existing display fonts.
void ui_draw_splash(const char *board_name, const char *version, const char *line1, const char *line2)
{
    const ui_layout_t *L=&ui_layout;
    int w=gfx_width(),h=gfx_height(),gap=h>=200?10:2;
    const char *name="Open25-DM32 Classic";
    const font_t *wf=L->large;
    if(gfx_text_width(wf,name)>w-8)wf=L->medium;
    if(gfx_text_width(wf,name)>w-8)wf=L->small;
    if(gfx_text_width(wf,name)>w-8)wf=L->tiny;
    const char *lines[2]={line1?line1:"",line2?line2:""};
    int n_lines=(lines[0][0]!=0)+(lines[1][0]!=0);
    const font_t *lf=L->medium,*ff=L->tiny;
    for(unsigned i=0;i<2;i++)
        if(gfx_text_width(lf,lines[i])>w-4)lf=L->small;
    for(unsigned i=0;i<2;i++)
        if(gfx_text_width(lf,lines[i])>w-4)lf=L->tiny;
    char foot[48];
    static const char *const seps[]={"  "," ",NULL};
    for(unsigned i=0;i<3;i++){
        sbuf_t b;sb_init(&b,foot,sizeof foot);
        if(seps[i]){sb_str(&b,board_name);sb_str(&b,seps[i]);}
        sb_str(&b,version);
        if(gfx_text_width(ff,foot)<=w)break;
    }
    bool tall=h>=160;
    int foot_y=h-ff->h,limit=foot_y-gap-(tall?ff->h+gap:0);
    int text_h=wf->h+(tall?gap+L->small->h:0)+(n_lines?gap+n_lines*lf->h:0);
    int mark_h=L->large->h+16;
    bool mark=tall && limit-text_h>=mark_h+gap;
    int y=(limit-text_h-(mark?mark_h+gap:0))/2;
    if(y<0)y=0;
    gfx_clear(ui_col.bg);
    if(mark){
        int mw=gfx_text_width(L->large,"25")+24;
        color_t blue=gfx_bpp()==1?ui_col.fg:RGB565(0,80,160);
        color_t ink=gfx_bpp()==1?ui_col.bg:RGB565(255,255,255);
        gfx_round_fill((w-mw)/2,y,mw,mark_h,blue);
        gfx_text(L->large,(w-gfx_text_width(L->large,"25"))/2,y+8,"25",ink);
        y+=mark_h+gap;
    }
    gfx_text(wf,(w-gfx_text_width(wf,name))/2,y,name,ui_col.fg);
    y+=wf->h;
    if(tall){
        const char *modes="FM / DMR / P25";
        y+=gap;
        gfx_text(L->small,(w-gfx_text_width(L->small,modes))/2,y,modes,ui_col.dim);
        y+=L->small->h;
    }
    if(n_lines){
        y+=gap;
        for(unsigned i=0;i<2;i++)if(lines[i][0]){
            gfx_text(lf,(w-gfx_text_width(lf,lines[i]))/2,y,lines[i],ui_col.fg);
            y+=lf->h;
        }
    }
    if(tall){
        const char *support="kd8zmm.com/open25-dm32";
        gfx_text(ff,(w-gfx_text_width(ff,support))/2,foot_y-gap-ff->h,support,ui_col.dim);
    }
    gfx_text(ff,(w-gfx_text_width(ff,foot))/2,foot_y,foot,ui_col.dim);
}

'''),
        ('static uint8_t ui_theme;', 'static uint8_t ui_theme=1;'),
        ('TR("Add codec with rfoundry")', 'TR("Use Open25 Flasher v3")'),

        ('return v->ch.mode == RADIO_MODE_DMR ? "DMR" : v->am ? "AM" : "FM";', 'return codeplug_mode_p25(v->ch.mode) ? "P25" : v->ch.mode == RADIO_MODE_DMR ? "DMR" : v->am ? "AM" : "FM";'),
    ])
    transform('core/ui/ui_svc.h', [
        ('} ui_event_kind_t;', '\tUI_EV_CODEPLUG_STARTED,\t// Start new and its held edit finished successfully\n} ui_event_kind_t;'),
    ])
    transform('core/ui/ui_app.c', [
        ('\tcase UI_EV_CODEPLUG_FOREIGN:', '\tcase UI_EV_CODEPLUG_STARTED:\n\t\tui_toast(TR("New codeplug started"));\n\t\tbreak;\n\tcase UI_EV_CODEPLUG_FOREIGN:'),
    ])
    transform('core/ui/ui_svc_storage.c', [
        ('static void save_failed_text(const char *text)\n{',
         'static bool cp_write_failed;\t// result of the held edit, owned by the storage job\n\nstatic void save_failed_text(const char *text)\n{\n\tcp_write_failed = true;'),
        ('\tvoid (*fn)(void *ctx) = cp_jobs[job.held - 1u];', '''\tvoid (*fn)(void *ctx) = cp_jobs[job.held - 1u];
\t// Only the explicit Start new choice may replace another firmware's settings/journal.
\t// The settings owner attaches its live preferences through its existing claim path.
\tif ((uintptr_t)ctx == UI_CP_NEW && storage_region() == STORAGE_REGION_FOREIGN && storage_claim_request())
\t\tsettings_poll(hal_millis());'''),
        ('\t\tjob.held = 0;\n\t\tfn(NULL);', '''\t\tjob.held = 0;
\t\tcp_write_failed = false;
\t\tfn(NULL);
\t\tif (!cp_write_failed) {
\t\t\tui_event_t ev = { .kind = UI_EV_CODEPLUG_STARTED };
\t\t\tui_notify(&ev);
\t\t}'''),
    ])
    transform('core/radio/radio_sm.h', [
        ('\tscan_t scan;', '\tscan_t scan;\n#if CONFIG_P25_CHANNELS\n\tbool p25_scan_ready;\n#endif'),
        ('void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms);', '''void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms);
uint8_t radio_sm_tx_check(const radio_sm_t *sm);
#if CONFIG_P25_CHANNELS
void radio_sm_native_maintenance(radio_sm_t *sm, uint32_t now_ms);
#if CONFIG_SCAN
bool radio_sm_p25_scan_poll(radio_sm_t *sm, bool ready, uint32_t now_ms);
bool radio_sm_p25_scan_ptt(radio_sm_t *sm, uint32_t now_ms);
#endif
#endif'''),
    ])
    transform('core/radio/radio_sm.c', [
        ('#include "config.h"', '#include "config.h"\n#if CONFIG_P25_CHANNELS\n#include "p25/p25_lab.h"\n#endif'),
        ('\tshared = ambeVoiceStreaming();\n#endif', '\tshared = ambeVoiceStreaming();\n#endif\n#if CONFIG_P25_CHANNELS\n\tshared = shared || p25_lab_busy();\n#endif', 2),
        ('static uint8_t tx_check(const radio_sm_t *sm)', 'uint8_t radio_sm_tx_check(const radio_sm_t *sm)'),
        ('tx_check(sm)', 'radio_sm_tx_check(sm)', 4),
        ('\tcase RREQ_PTT:\n#if CONFIG_FM_TX_TONES', '\tcase RREQ_PTT:\n\t\t/* P25 PTT is handled by the native owner before this state machine. */\n\t\tif (codeplug_mode_p25(sm->view.ch.mode)) break;\n#if CONFIG_FM_TX_TONES'),
        ('case RREQ_MODE:\n\t\tif (busy ||', 'case RREQ_MODE:\n\t\tif (codeplug_mode_p25(sm->view.ch.mode)) break;\n\t\tif (busy ||'),
        ('\treturn pos < sm->zone.count && codeplug_channel(sm->zone.channels[pos], &ch) && !ch.scan_skip;', '''\tif (pos >= sm->zone.count || !codeplug_channel(sm->zone.channels[pos], &ch) || ch.scan_skip)
\t\treturn false;
#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(ch.mode))
\t\treturn board.native_modem && codeplug_p25_valid(&ch) && board_in_band(ch.rx_hz, false);
#endif
\treturn true;'''),
        ('codeplug_channel(z->channels[r->pos], ch) ? z : NULL;', 'codeplug_channel(z->channels[r->pos], ch) && !codeplug_mode_p25(ch->mode) ? z : NULL;'),
        ('\tscan_tuned(sc, now_ms);\n\tscan_publish(sm);\n\treturn true;\n}\n\nstatic void scan_config', '''#if CONFIG_P25_CHANNELS
\tsm->p25_scan_ready = false;
#endif
\tscan_tuned(sc, now_ms);
\tscan_publish(sm);
\treturn true;
}

static void scan_config'''),
        ('\tscan_pause(sc, sm->view.monitor, now_ms);', '''#if CONFIG_P25_CHANNELS
\t/* The native owner supplies P25 activity after the receiver starts. */
\tif (codeplug_mode_p25(sm->view.ch.mode)) return;
#endif
\tscan_pause(sc, sm->view.monitor, now_ms);'''),
        ('// A scan stop asked for (the debug link):', '''#if CONFIG_P25_CHANNELS
/* LDU1 repeats every 360 ms. Allow time to find sync and receive its link control. */
#define P25_SCAN_DWELL_MS 450u
#define P25_SCAN_START_LIMIT_MS 1500u

bool radio_sm_p25_scan_poll(radio_sm_t *sm, bool ready, uint32_t now_ms)
{
\tscan_t *sc = &sm->scan;
\tif (sc->kind != SCAN_ZONE || !codeplug_mode_p25(sm->view.ch.mode) ||
\t    sm->ptt || sm->power_off_req || sm->view.power_off) return false;
\tscan_pause(sc, sm->view.monitor, now_ms);
\tif (sc->phase == SCAN_PH_PAUSED) {
\t\tscan_publish(sm);
\t\treturn false;
\t}
\tif (!sm->p25_scan_ready) {
\t\tif (!ready) return now_ms - sc->tuned_ms >= P25_SCAN_START_LIMIT_MS;
\t\tsm->p25_scan_ready = true;
\t\tsc->tuned_ms = now_ms;
\t}
\t/* Only decoded audio admitted by this channel's NAC and talkgroup holds the scan. */
\tbool active = ready && p25_lab_channel_receiving();
\tuint16_t dwell = sc->cfg.dwell_ms;
\tif (dwell < P25_SCAN_DWELL_MS) sc->cfg.dwell_ms = P25_SCAN_DWELL_MS;
\tuint8_t was = sc->phase;
\tscan_act_t action = scan_poll(sc, now_ms, active, false, false);
\tsc->cfg.dwell_ms = dwell;
\tif (action == SCAN_ACT_STOP) scan_stop(sm, false, now_ms);
\tif (sc->phase == SCAN_PH_HOLD && was != SCAN_PH_HOLD && was != SCAN_PH_RESUME && sm->set.scan_beep)
\t\tbeep(sm, BEEP_SCAN_HOLD);
\tscan_publish(sm);
\treturn action == SCAN_ACT_NEXT;
}

bool radio_sm_p25_scan_ptt(radio_sm_t *sm, uint32_t now_ms)
{
\tif (sm->scan.kind != SCAN_ZONE) return false;
\tbool searching = sm->scan.phase != SCAN_PH_HOLD && sm->scan.phase != SCAN_PH_RESUME;
\tscan_stop(sm, false, now_ms);
\t/* On a searching stop the first press ends scan; a second press can transmit. */
\treturn searching;
}
#endif

// A scan stop asked for (the debug link):'''),
        ('\t\tscan_stop(sm, !on_signal, now_ms);\n\t}\n#endif', '''\t\tscan_stop(sm, !on_signal, now_ms);
#if CONFIG_P25_CHANNELS
\t\t/* Returning to a P25 origin requires a new press through the native owner. */
\t\tif (codeplug_mode_p25(sm->view.ch.mode)) return;
#endif
\t}
#endif'''),
        ('\tcase RSCAN_DUAL: {\n', '\tcase RSCAN_DUAL: {\n\t\tif (codeplug_mode_p25(sm->view.ch.mode)) break;\n'),
        ('\tcase RSCAN_NUISANCE:\n', '''#if CONFIG_P25_CHANNELS
\tcase RSCAN_P25_NEXT:
\t\tif (sm->scan.kind == SCAN_ZONE && codeplug_mode_p25(sm->view.ch.mode))
\t\t\tscan_step(sm, now_ms);
\t\tbreak;
#endif
\tcase RSCAN_NUISANCE:
'''),
        ('if (ch->mode == RADIO_MODE_DMR ? !CONFIG_VFO_DMR : !CONFIG_VFO_FM)', 'if (codeplug_mode_p25(ch->mode) || (ch->mode == RADIO_MODE_DMR ? !CONFIG_VFO_DMR : !CONFIG_VFO_FM))'),
        ('if (ch->mode == RADIO_MODE_DMR ? !CONFIG_DMR : !CONFIG_FM)', 'if (codeplug_mode_p25(ch->mode) ? !CONFIG_P25_NATIVE : (ch->mode == RADIO_MODE_DMR ? !CONFIG_DMR : !CONFIG_FM))'),
        ('\tif (ch->tx_hz == 0) {', '''#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(ch->mode) && !codeplug_p25_valid(ch)) return TXDENY_MODE;
#endif
\tif (ch->tx_hz == 0) {'''),
        ('\tif (!board_in_band(ch->tx_hz, true)) {', '''\tif (!board_in_band(ch->tx_hz, true)
#if CONFIG_P25_CHANNELS
\t    || (codeplug_mode_p25(ch->mode) &&
\t        !((ch->tx_hz > 144000000u && ch->tx_hz < 148000000u) ||
\t          (ch->tx_hz > 420000000u && ch->tx_hz < 450000000u)))
#endif
\t) {'''),
        ('\tif (sm->view.ch.mode != RADIO_MODE_FM || sm->set.bcl == RADIO_BCL_OFF) {', '''#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(sm->view.ch.mode))
\t\treturn sm->set.bcl != RADIO_BCL_OFF && sm->view.state == RS_RX;
#endif
\tif (sm->view.ch.mode != RADIO_MODE_FM || sm->set.bcl == RADIO_BCL_OFF) {'''),
        ('\tbool green = open;', '''\tbool green = open;
#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(sm->view.ch.mode)) { open = false; green = sm->view.state == RS_RX; }
#endif'''),
        ('void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms)', '''#if CONFIG_P25_CHANNELS
void radio_sm_native_maintenance(radio_sm_t *sm, uint32_t now_ms)
{
\tfollow_volume(sm);
\tbattery_poll(sm, now_ms);
\tif (sm->view.state == RS_TX) tx_timer(sm, now_ms);
\toutputs(sm);
#if CONFIG_BEEP
\tbeep_service(p25_lab_busy());
#endif
}
#endif
void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms)'''),
    ])
    transform('core/debuglink/debuglink_view.c', [
        ('v->ch.mode == RADIO_MODE_DMR ? "dmr" : "fm"', 'codeplug_mode_p25(v->ch.mode) ? "p25" : v->ch.mode == RADIO_MODE_DMR ? "dmr" : "fm"'),
        ('c->mode == RADIO_MODE_DMR ? "dmr" : "fm"', 'codeplug_mode_p25(c->mode) ? "p25" : c->mode == RADIO_MODE_DMR ? "dmr" : "fm"'),
    ])
