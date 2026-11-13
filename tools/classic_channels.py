# SPDX-License-Identifier: GPL-3.0-or-later
"""Conventional P25 fields in RadioFoundry's existing channel store/editor."""


def prepare(transform):
    transform('core/codeplug/codeplug.h', [
        ('typedef struct {\n\tchar name[CODEPLUG_NAME_LEN + 1];\n\tuint32_t rx_hz;', '''#if CONFIG_P25_CHANNELS
enum { RADIO_MODE_P25 = 2 };
#endif
typedef struct {
\tchar name[CODEPLUG_NAME_LEN + 1];
\tuint32_t rx_hz;'''),
        ('\tuint16_t contact;\t// talk target contact index, 0 = none\n\tuint16_t listen_list;\t// RX group list index, 0 = none\n\tuint32_t dmr_id;\t// own DMR ID for this channel, 0 = the radio\'s', '''#if CONFIG_P25_CHANNELS
\tunion {
\t\tstruct { uint16_t contact, listen_list; uint32_t dmr_id; };
\t\tstruct { uint16_t p25_tg, p25_nac; uint32_t p25_id; };
\t};
#else
\tuint16_t contact, listen_list;
\tuint32_t dmr_id;
#endif'''),
        ('} channel_t;', '''} channel_t;

static inline bool codeplug_mode_p25(uint8_t mode)
{
#if CONFIG_P25_CHANNELS
\treturn mode == RADIO_MODE_P25;
#else
\t(void)mode;
\treturn false;
#endif
}
#if CONFIG_P25_CHANNELS
static inline bool codeplug_p25_valid(const channel_t *ch)
{
\treturn ch->bw == RADIO_BW_12K5 && ch->p25_nac <= 0xFFFu &&
\t       ch->p25_nac != 0xF7Fu && ch->p25_id <= 9999999u &&
\t       (ch->rx_only || (ch->p25_nac != 0xF7Eu && ch->p25_tg && ch->p25_id));
}
#endif'''),
    ])
    transform('core/codeplug/codeplug.c', [
        ('\tmemset(out, 0, sizeof(*out));\n\tif (!freq(&r[CH_RX]', '\tmemset(out, 0, sizeof(*out));\n#if CONFIG_P25_CHANNELS\n\tif (r[CH_MODE] != 0 && r[CH_MODE] != 1 && r[CH_MODE] != 0xA5u) return false;\n#endif\n\tif (!freq(&r[CH_RX]'),
        ('\tout->alias_tx = (uint8_t)', '''#if CONFIG_P25_CHANNELS
\tif (r[CH_MODE] == 0xA5u) {
\t\tout->mode = RADIO_MODE_P25;
\t\tout->p25_nac = (uint16_t)(((r[CH_CC] & 15u) << 8) | r[CH_RX_GROUP]);
\t\tout->dmo = out->roam = out->vox = false;
\t\treturn (r[CH_CC] & 0xF0u) == 0xD0u && codeplug_p25_valid(out);
\t}
#endif
\tout->alias_tx = (uint8_t)'''),
        ('\tr[CH_MODE] = ch->mode == RADIO_MODE_DMR ? 1 : 0;', '\tr[CH_MODE] = codeplug_mode_p25(ch->mode) ? 0xA5u : ch->mode == RADIO_MODE_DMR ? 1 : 0;'),
        ('\tr[CH_CC] = ch->color_code & 0x0Fu;', '''\tr[CH_CC] = ch->color_code & 0x0Fu;
#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(ch->mode))
\t\tr[CH_CC] = (uint8_t)(0xD0u | (ch->p25_nac >> 8));
#endif'''),
        ('\tr[CH_RX_GROUP] = (uint8_t)(ch->listen_list <= 0xFFu ? ch->listen_list : 0);', '''\tr[CH_RX_GROUP] = (uint8_t)(ch->listen_list <= 0xFFu ? ch->listen_list : 0);
#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(ch->mode)) r[CH_RX_GROUP] = (uint8_t)ch->p25_nac;
#endif'''),
        ('\tstorage_lock();\n\tbool ok = materialize() && flash_read(channel_addr(number), r, sizeof(r));', '''#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(ch->mode) && !codeplug_p25_valid(ch)) return false;
#endif
\tstorage_lock();
\tbool ok = materialize() && flash_read(channel_addr(number), r, sizeof(r));'''),
    ])
    transform('core/ui/ui_menus.c', [
        ('static void contacts_item(uint16_t i, ui_item_t *it)', 'static void __attribute__((unused)) contacts_item(uint16_t i, ui_item_t *it)'),
        ('CE_ZONES, CE_GO, CE_SHARE, CE_SAVE, CE_SAVE_NEW, CE_COUNT', '#if CONFIG_P25_CHANNELS\n       CE_P25_TG, CE_P25_NAC, CE_P25_ID,\n#endif\n       CE_ZONES, CE_GO, CE_SHARE, CE_SAVE, CE_SAVE_NEW, CE_COUNT'),
        ('[CE_SCAN] = CE_FIELD(scan_skip), [CE_ECO] = CE_FIELD(no_eco),', '[CE_SCAN] = CE_FIELD(scan_skip), [CE_ECO] = CE_FIELD(no_eco),\n#if CONFIG_P25_CHANNELS\n\t[CE_P25_TG] = CE_FIELD(p25_tg), [CE_P25_NAC] = CE_FIELD(p25_nac), [CE_P25_ID] = CE_FIELD(p25_id),\n#endif'),
        ('\t\tif ((fm_only && dmr) || (dmr_only && !dmr) || (r == CE_MODE && !CONFIG_DMR))', '#if CONFIG_P25_CHANNELS\n\t\tbool p25 = codeplug_mode_p25(ced_ch.mode);\n\t\tbool p25_only = r >= CE_P25_TG && r <= CE_P25_ID;\n\t\tif ((p25_only && !p25) || (p25 && (fm_only || dmr_only || r == CE_ECO))) continue;\n#endif\n\t\tif ((fm_only && dmr) || (dmr_only && !dmr) || (r == CE_MODE && !CONFIG_DMR))'),
        ('TRN("Battery saver"), TRN("Zones"), TRN("Tune to this ch"),', 'TRN("Battery saver"),\n#if CONFIG_P25_CHANNELS\n\t\tTRN("Talkgroup"), "NAC", TRN("P25 Radio ID"),\n#endif\n\t\tTRN("Zones"), TRN("Tune to this ch"),'),
        ('sb_str(&wb, ch->mode == RADIO_MODE_DMR ? "DMR" : radio_am_channel(ch) ? "AM" : "FM");', 'sb_str(&wb, codeplug_mode_p25(ch->mode) ? "P25" : ch->mode == RADIO_MODE_DMR ? "DMR" : radio_am_channel(ch) ? "AM" : "FM");'),
        ('sb_str(&b, ch->mode == RADIO_MODE_DMR ? "DMR" : "FM");', 'sb_str(&b, codeplug_mode_p25(ch->mode) ? "P25" : ch->mode == RADIO_MODE_DMR ? "DMR" : "FM");'),
        ('if (editing && (r == CE_RX || r == CE_TX || r == CE_ID)) {', 'if (editing && (r == CE_RX || r == CE_TX || r == CE_ID\n#if CONFIG_P25_CHANNELS\n\t\t|| r == CE_P25_TG || r == CE_P25_ID\n#endif\n\t\t)) {'),
        ('if (r != CE_ID && k == UI_ENTRY_MHZ)', 'if ((r == CE_RX || r == CE_TX) && k == UI_ENTRY_MHZ)'),
        ('\tcase CE_NUMBER:\n\t\tsb_u32(&b, ced_num, 1);', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_TG: sb_u32(&b, ch->p25_tg, 1); break;\n\tcase CE_P25_ID: sb_u32(&b, ch->p25_id, 1); break;\n\tcase CE_P25_NAC: {\n\t\tunsigned value = ch->p25_nac, cursor = 3;\n#if CONFIG_CHANNEL_EDIT\n\t\tif (editing) { value = (unsigned)ced_val; cursor = ced_len; }\n#endif\n\t\tsb_chr(&b, \'$\');\n\t\tfor (unsigned digit = 0; digit < 3; digit++) {\n\t\t\tif (digit == cursor) sb_chr(&b, \'[\');\n\t\t\tsb_chr(&b, "0123456789ABCDEF"[(value >> (4 * (2 - digit))) & 15u]);\n\t\t\tif (digit == cursor) sb_chr(&b, \']\');\n\t\t}\n\t\tbreak;\n\t}\n#endif\n\tcase CE_NUMBER:\n\t\tsb_u32(&b, ced_num, 1);'),
        ('\tif (CONFIG_DMR && c->mode == RADIO_MODE_DMR && c->contact == 0)', '#if CONFIG_P25_CHANNELS\n\tif (codeplug_mode_p25(c->mode) && !codeplug_p25_valid(c))\n\t\treturn TR("Check P25 NAC, talkgroup and ID");\n#endif\n\tif (CONFIG_DMR && c->mode == RADIO_MODE_DMR && c->contact == 0)'),
        ('\tcase CE_CC:\n\t\tced_val = (ced_val + dir + 16) % 16;', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC: {\n\t\tunsigned shift = 4u * (2u - ced_len);\n\t\tunsigned value = (((unsigned)ced_val >> shift) + (unsigned)dir) & 15u;\n\t\tced_val = (ced_val & ~(15u << shift)) | (value << shift);\n\t\tbreak;\n\t}\n#endif\n\tcase CE_CC:\n\t\tced_val = (ced_val + dir + 16) % 16;'),
        ('\tcase CE_ID:\n\t\tif (ced_len >= CE_DIGITS)', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC: {\n\t\tunsigned shift = 4u * (2u - ced_len);\n\t\tced_val = (ced_val & ~(15u << shift)) | ((unsigned)(d - \'0\') << shift);\n\t\tced_len = (uint8_t)((ced_len + 1) % 3);\n\t\tbreak;\n\t}\n\tcase CE_P25_TG:\n\tcase CE_P25_ID:\n\t\tif (v > (ced_edit == CE_P25_TG ? 65535 : 9999999)) {\n\t\t\tui_toast(TR("Value too large")); break;\n\t\t}\n        /* fall through */\n        __attribute__((fallthrough));\n#endif\n\tcase CE_ID:\n\t\tif (ced_len >= CE_DIGITS)'),
        ('\tcase CE_ID:\n\t\tif (ced_len > 0)', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_TG: if (ced_len) c->p25_tg = (uint16_t)ced_val; break;\n\tcase CE_P25_ID: if (ced_len) c->p25_id = (uint32_t)ced_val; break;\n\tcase CE_P25_NAC: c->p25_nac = (uint16_t)ced_val; break;\n#endif\n\tcase CE_ID:\n\t\tif (ced_len > 0)'),
        ('static bool chinfo_key(const input_event_t *ev)\n{\n\tbool press = ev->gesture == GESTURE_PRESS || ev->gesture == GESTURE_LONG;\n\tif (ced_edit == CE_NONE) {\n\t\tif (ev->action == ACT_BACK && ev->gesture == GESTURE_PRESS && ced_num != 0 &&\n\t\t\t\t(ced_new || ced_changed(CE_COUNT))) {\n\t\t\tdiscard_contact = false;\n\t\t\tui_push(SCR_DISCARD, 0);\n\t\t\treturn true;\n\t\t}\n\t\treturn false;\n\t}\n\tint dir = ev->action == ACT_NAV_UP ? 1 : ev->action == ACT_NAV_DOWN ? -1 :\n\t\tev->action == ACT_ENC_STEP ? (ev->value > 0 ? 1 : -1) : 0;\n\tif (dir != 0 && ev->gesture != GESTURE_UP) {', 'static bool chinfo_key(const input_event_t *ev)\n{\n\tbool press = ev->gesture == GESTURE_PRESS || ev->gesture == GESTURE_LONG;\n\tif (ced_edit == CE_NONE) {\n\t\tif (ev->action == ACT_BACK && ev->gesture == GESTURE_PRESS && ced_num != 0 &&\n\t\t\t\t(ced_new || ced_changed(CE_COUNT))) {\n\t\t\tdiscard_contact = false;\n\t\t\tui_push(SCR_DISCARD, 0);\n\t\t\treturn true;\n\t\t}\n\t\treturn false;\n\t}\n\tint dir = ev->action == ACT_NAV_UP ? 1 : ev->action == ACT_NAV_DOWN ? -1 :\n\t\tev->action == ACT_ENC_STEP ? (ev->value > 0 ? 1 : -1) : 0;\n#if CONFIG_P25_CHANNELS\n    if (ced_edit == CE_P25_NAC && dir && ev->gesture != GESTURE_UP && ev->action != ACT_ENC_STEP) {\n        ced_len = (uint8_t)((ced_len + dir + 3) % 3);\n        ui_dirty(); return true;\n    }\n#endif\n\tif (dir != 0 && ev->gesture != GESTURE_UP) {'),
        ('if (ced_edit != CE_RX && ced_edit != CE_TX && ced_edit != CE_ID)', 'if (ced_edit != CE_RX && ced_edit != CE_TX && ced_edit != CE_ID\n#if CONFIG_P25_CHANNELS\n\t\t\t&& ced_edit != CE_P25_TG && ced_edit != CE_P25_ID\n#endif\n\t\t\t)'),
        ('\tcase CE_MODE: c->mode = c->mode == RADIO_MODE_DMR ? RADIO_MODE_FM : RADIO_MODE_DMR; break;', '\tcase CE_MODE:\n#if CONFIG_P25_CHANNELS\n\t\tif (c->mode == RADIO_MODE_DMR) {\n\t\t\tc->mode = RADIO_MODE_P25; c->p25_nac = 0x293;\n\t\t\tc->p25_tg = 1; c->p25_id = 1; c->bw = RADIO_BW_12K5;\n\t\t\tc->dmo = c->roam = c->vox = false;\n\t\t} else if (codeplug_mode_p25(c->mode)) {\n\t\t\tc->mode = RADIO_MODE_FM; c->contact = c->listen_list = c->dmr_id = 0;\n\t\t} else\n#endif\n\t\t\tc->mode = c->mode == RADIO_MODE_DMR ? RADIO_MODE_FM : RADIO_MODE_DMR;\n\t\tbreak;\n#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC: ced_edit = r; ced_val = c->p25_nac; ced_len = 0; break;\n\tcase CE_P25_TG:\n\tcase CE_P25_ID:\n#endif'),
        ('ced_edit == CE_TX_TONE) && ced_len > 0)', 'ced_edit == CE_TX_TONE\n#if CONFIG_P25_CHANNELS\n             || ced_edit == CE_P25_TG || ced_edit == CE_P25_ID\n#endif\n             ) && ced_len > 0)'),
    ])
    transform('core/ui/ui_screens.c', [
        ('return v->ch.mode == RADIO_MODE_DMR ? "DMR" : v->am ? "AM" : "FM";', 'return codeplug_mode_p25(v->ch.mode) ? "P25" : v->ch.mode == RADIO_MODE_DMR ? "DMR" : v->am ? "AM" : "FM";'),
    ])
    transform('core/radio/radio_sm.h', [
        ('void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms);', '''void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms);
uint8_t radio_sm_tx_check(const radio_sm_t *sm);
#if CONFIG_P25_CHANNELS
void radio_sm_native_maintenance(radio_sm_t *sm, uint32_t now_ms);
#endif'''),
    ])
    transform('core/radio/radio_sm.c', [
        ('static uint8_t tx_check(const radio_sm_t *sm)', 'uint8_t radio_sm_tx_check(const radio_sm_t *sm)'),
        ('tx_check(sm)', 'radio_sm_tx_check(sm)', 4),
        ('\tcase RREQ_PTT:\n#if CONFIG_FM_TX_TONES', '\tcase RREQ_PTT:\n\t\t/* P25 PTT is handled by the native owner before this state machine. */\n\t\tif (codeplug_mode_p25(sm->view.ch.mode)) break;\n#if CONFIG_FM_TX_TONES'),
        ('case RREQ_MODE:\n\t\tif (busy ||', 'case RREQ_MODE:\n\t\tif (codeplug_mode_p25(sm->view.ch.mode)) break;\n\t\tif (busy ||'),
        ('codeplug_channel(sm->zone.channels[pos], &ch) && !ch.scan_skip;', 'codeplug_channel(sm->zone.channels[pos], &ch) && !ch.scan_skip && !codeplug_mode_p25(ch.mode);'),
        ('static void scan_request(radio_sm_t *sm, const radio_req_t *req, uint32_t now_ms)\n{', 'static void scan_request(radio_sm_t *sm, const radio_req_t *req, uint32_t now_ms)\n{\n\t/* P25 currently supports selected-channel receive, not scan dwell. */\n\tif (codeplug_mode_p25(sm->view.ch.mode)) return;'),
        ('\tcase RSCAN_DUAL: {\n', '\tcase RSCAN_DUAL: {\n\t\tchannel_t other;\n\t\tif (req->arg2 >= sm->zone.count || !codeplug_channel(sm->zone.channels[req->arg2], &other) || codeplug_mode_p25(other.mode)) break;\n'),
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
\tbeep_service(false);
#endif
}
#endif
void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms)'''),
    ])
    transform('core/debuglink/debuglink_view.c', [
        ('v->ch.mode == RADIO_MODE_DMR ? "dmr" : "fm"', 'codeplug_mode_p25(v->ch.mode) ? "p25" : v->ch.mode == RADIO_MODE_DMR ? "dmr" : "fm"'),
        ('c->mode == RADIO_MODE_DMR ? "dmr" : "fm"', 'codeplug_mode_p25(c->mode) ? "p25" : c->mode == RADIO_MODE_DMR ? "dmr" : "fm"'),
    ])
