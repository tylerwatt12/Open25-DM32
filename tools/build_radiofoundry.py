#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Portions Copyright (c) 2026 The RadioFoundry Authors; see LICENSES/RadioFoundry-MIT.txt.
"""Reproducible, local RadioFoundry conventional P25 integration.

Never opens a serial port, flashes a radio or modifies the pristine checkout.
Classic provides FM, DMR and saved conventional P25 channels.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools import codec_cache, classic_channels, classic_standard, sources
UP = ROOT / "research/upstream/radiofoundry"
PIN = "dfbaf20c400c73e2ccf6edb80af7d1d255bee418"
DEST = ROOT / "build/radiofoundry-p25"
OWNED_FILE = DEST / ".p25-owned.json"
OWNED = json.loads(OWNED_FILE.read_text()) if OWNED_FILE.exists() else {}


def run(args, **kw):
    return subprocess.run([str(a) for a in args], check=True, **kw)


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()


def write_owned(path, content, original=None):
    key = str(path.relative_to(DEST))
    if path.is_symlink():
        raise SystemExit(f"Preserving unexpected linked input: {path}")
    if (path.exists() and path.read_bytes() not in (content, original) and
        hashlib.sha256(path.read_bytes()).hexdigest() != OWNED.get(key)):
        raise SystemExit(f"Preserving unexpected local changes: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != content:
        path.write_bytes(content)
    OWNED[key] = hashlib.sha256(content).hexdigest()


def transform(relative, replacements):
    original = (UP / relative).read_bytes()
    content = original.decode()
    for entry in replacements:
        old,new=entry[:2]
        expected=entry[2] if len(entry)==3 else 1
        if content.count(old) != expected:
            raise SystemExit(f"Pinned patch mismatch: {relative}: {old}")
        content = content.replace(old, new)
    write_owned(DEST / relative, content.encode(), original)


# Selected RF-section support from RadioFoundry 9e1f7209 (MIT). Keep the
# existing base pin and application behavior; these checked replacements and
# fd6818_setup_b.c are derived together from that reviewed upstream snapshot.
NRFB_HEADER_PATCHES = [('\t// Band words for a frequency',
  '\t// Set-up B receiver power words; zero retains the original setup.\n'
  '\tuint16_t sleep_fm, sleep_dmr, sleep_wake;\n'
  '\t// Band words for a frequency'),
 ('extern const fd6818_variant_t fd6818_variant_default;',
  'extern const fd6818_variant_t fd6818_variant_default;\n'
  'extern const fd6818_variant_t fd6818_variant_b;\n'
  'extern const fd6818_variant_t fd6818_variant_b_dmr6943;'),
 ('#define FD6818_REG_BAND_A', '#define FD6818_CSS_PLL_LOCK\t0x0200u\n#define FD6818_REG_BAND_A'),
 ('// Analog TX preparation writes',
  '#define FD6818_LOCK_READS\t3u\n'
  '#define FD6818_DETECT_RETRY\t3u\n'
  'bool fd6818_pll_locked(fd6818_t *dev, uint32_t timeout_ms);\n'
  'uint8_t fd6818_detect(fd6818_t *dev, rfbus_t *bus, const fd6818_variant_t *const *v, uint8_t n, '
  'uint32_t hz, uint32_t timeout_ms);\n'
  '// Analog TX preparation writes')]

NRFB_DRIVER_PATCHES = [('void fd6818_set_bandwidth',
  'bool fd6818_pll_locked(fd6818_t *dev, uint32_t timeout_ms)\n'
  '{\n'
  '\t// The bit can read set once while the synthesizer settles: a lock is FD6818_LOCK_READS reads '
  'in a row, 1 ms apart\n'
  '\tuint8_t run = 0;\n'
  '\tfor (uint32_t ms = 0;; ms++) {\n'
  '\t\tif ((fd6818_read(dev, FD6818_REG_CSS_STATUS) & FD6818_CSS_PLL_LOCK) != 0u) {\n'
  '\t\t\tif (++run >= FD6818_LOCK_READS) {\n'
  '\t\t\t\treturn true;\n'
  '\t\t\t}\n'
  '\t\t} else {\n'
  '\t\t\trun = 0;\n'
  '\t\t\tif (ms >= timeout_ms) {\n'
  '\t\t\t\treturn false;\n'
  '\t\t\t}\n'
  '\t\t}\n'
  '\t\trfbus_delay_ms(dev->bus, 1);\n'
  '\t}\n'
  '}\n'
  '\n'
  'static bool detect_try(fd6818_t *dev, rfbus_t *bus, const fd6818_variant_t *v, uint32_t hz, '
  'uint32_t timeout_ms)\n'
  '{\n'
  '\tfd6818_attach_variant(dev, bus, v);\n'
  '\tfd6818_init(dev);\n'
  '\tfd6818_set_mode(dev, FD6818_MODE_FM);\n'
  '\tfd6818_set_freq(dev, hz);\n'
  '\tfd6818_set_state(dev, FD6818_STATE_RX);\n'
  '\treturn fd6818_pll_locked(dev, timeout_ms);\n'
  '}\n'
  '\n'
  'uint8_t fd6818_detect(fd6818_t *dev, rfbus_t *bus, const fd6818_variant_t *const *v, uint8_t n, '
  'uint32_t hz,\n'
  '\t\t      uint32_t timeout_ms)\n'
  '{\n'
  '\tfor (uint8_t i = 0; i < n; i++) {\n'
  '\t\tif (!detect_try(dev, bus, v[i], hz, timeout_ms)) {\n'
  '\t\t\tcontinue;\n'
  '\t\t}\n'
  '\t\t// A later set-up can lock a section an earlier one is right for: an earlier one that was '
  'only slow gets a\n'
  '\t\t// longer wait before the later one is taken\n'
  '\t\tfor (uint8_t j = 0; j < i; j++) {\n'
  '\t\t\tif (detect_try(dev, bus, v[j], hz, FD6818_DETECT_RETRY * timeout_ms)) {\n'
  '\t\t\t\treturn j;\n'
  '\t\t\t}\n'
  '\t\t}\n'
  '\t\tif (i > 0 && !detect_try(dev, bus, v[i], hz, timeout_ms)) {\n'
  '\t\t\tbreak;\t\t// locked once, not again: neither is trusted\n'
  '\t\t}\n'
  '\t\treturn i;\n'
  '\t}\n'
  '\tfd6818_attach_variant(dev, bus, v[0]);\n'
  '\tfd6818_init(dev);\n'
  '\tfd6818_set_mode(dev, FD6818_MODE_FM);\n'
  '\treturn n;\n'
  '}\n'
  '\n'
  'void fd6818_set_bandwidth'),
 ('void fd6818_rx_sleep(fd6818_t *dev, bool sleep, fd6818_mode_t mode)\n'
  '{\n'
  '\tif (sleep) {\n'
  '\t\tfd6818_write(dev, FD6818_REG_STATE, FD6818_STATE_IDLE);\n'
  '\t\tfd6818_write(dev, FD6818_REG_POWER, mode == FD6818_MODE_DMR ? 0x1D00 : 0x1D11);\n'
  '\t} else {\n'
  '\t\tfd6818_write(dev, FD6818_REG_POWER, 0x1F1F);\n'
  '\t\tfd6818_write(dev, FD6818_REG_POWER, 0x1F0F);\n'
  '\t}\n'
  '}',
  'void fd6818_rx_sleep(fd6818_t *dev, bool sleep, fd6818_mode_t mode)\n'
  '{\n'
  '\tconst fd6818_variant_t *v = dev->variant;\n'
  '\tif (sleep) {\n'
  '\t\tfd6818_write(dev, FD6818_REG_STATE, FD6818_STATE_IDLE);\n'
  '\t\tif (v->sleep_wake != 0u) {\n'
  '\t\t\tfd6818_write(dev, FD6818_REG_POWER, mode == FD6818_MODE_DMR ? v->sleep_dmr : '
  'v->sleep_fm);\n'
  '\t\t} else {\n'
  '\t\t\tfd6818_write(dev, FD6818_REG_POWER, mode == FD6818_MODE_DMR ? 0x1D00 : 0x1D11);\n'
  '\t\t}\n'
  '\t} else if (v->sleep_wake != 0u) {\n'
  '\t\tfd6818_write(dev, FD6818_REG_POWER, v->sleep_wake);\n'
  '\t} else {\n'
  '\t\tfd6818_write(dev, FD6818_REG_POWER, 0x1F1F);\n'
  '\t\tfd6818_write(dev, FD6818_REG_POWER, 0x1F0F);\n'
  '\t}\n'
  '}')]

NRFB_BOARD_PATCHES = [('#define FD_REG_RSSI\t\t0x67u',
  '#define FD_REG_RSSI\t\t0x67u\n'
  "// Two RF sections (hardware-dm32.md 8.10): the newer one's FD6818 doesn't lock on the default set-up "
  'and takes set-up B,\n'
  '// with these writes on top. Which one the radio has is found at start-up (rf_detect)\n'
  '#define RF_PROBE_HZ\t\t145000000u\t// in band on both sections\n'
  "#define RF_LOCK_MS\t\t20u\t\t// the probe's wait for the lock bit after the tune\n"
  '#define FD_REG_TX_AUX\t\t0x50u\t\t// set-up B: around each switch to TX\n'
  '#define FD_TX_AUX_FM_PRE\t0xBB20u\t\t// FM, before the switch\n'
  '#define FD_TX_AUX_FM\t\t0x3B20u\t\t// FM, after\n'
  '#define FD_TX_AUX_DMR\t\t0x3B18u\t\t// DMR burst, after\n'
  '#define FD_REG_CSS_A\t\t0x2Eu\t\t// set-up B: two words by the receive sub-audio type\n'
  '#define FD_REG_CSS_B\t\t0x2Fu\n'
  '#define FD_REG40_BASE\t\t0x3480u\t\t// set-up B, FM transmit: 0x40 = this + the calibration byte\n'
  '#define C7_RF_MODE_RX_INVERT\t(1u << 15)\t// HR_C7000 RF_MODE: RX IQ invert, off on the newer '
  'section\n'),
 ('static hrc7000_t modem;', 'static hrc7000_t modem;\nstatic bool rf_b;\nstatic bool rf_unlocked;'),
 ('return bw == RADIO_BW_25K ? FD6818_BW_FM_25K : FD6818_BW_FM_12K5;',
  'return bw == RADIO_BW_25K ? fd.variant->bw_fm_25k : fd.variant->bw_fm_12k5;'),
 ('static void write_mod_bias',
  'static void fd_rx_subtone(fd6818_tone_t type, uint16_t value)\n'
  '{\n'
  '\tfd6818_set_rx_tone(&fd, type, value);\n'
  '\tif (rf_b) {\n'
  '\t\tbool ctcss = type == FD6818_TONE_CTCSS;\n'
  '\t\tfd6818_write(&fd, FD_REG_CSS_A, ctcss ? 0x8608u : 0x9608u);\n'
  '\t\tfd6818_write(&fd, FD_REG_CSS_B, ctcss ? 0x1890u : 0x9890u);\n'
  '\t}\n'
  '}\n'
  '\n'
  'static void write_mod_bias'),
 ('static void dmr_rf_switch(bool tx)\n{',
  'static void dmr_rf_switch(bool tx)\n{\n\tif (tx && rf_unlocked) return;'),
 ('FD6818_BW_DMR', 'fd.variant->bw_dmr', 3),
 ('\tfd6818_dmr_burst_at(&fd, tx, fd.variant->bw_dmr, synth_hz(tx ? tx_hz : rx_hz));',
  '\tfd6818_dmr_burst_at(&fd, tx, fd.variant->bw_dmr, synth_hz(tx ? tx_hz : rx_hz));\n'
  '\tif (tx && rf_b) fd6818_write(&fd, FD_REG_TX_AUX, FD_TX_AUX_DMR);'),
 ('if (dmr_keyed) {', 'if (dmr_keyed && !rf_unlocked) {'),
 ('static void init(void)',
  '// Which RF section the radio has (hardware-dm32.md 8.10): the default set-up, tuned, locks the classic '
  "section's\n"
  "// synthesizer and never the newer one's; set-up B locks both, so the default goes first "
  '(fd6818_detect). Neither\n'
  '// locking leaves the default and blocks transmitting: on the wrong set-up the newer section '
  'transmitted 74 MHz off\n'
  'static void rf_detect(void)\n'
  '{\n'
  '\tstatic const fd6818_variant_t *const v[] = { &fd6818_variant_default, &fd6818_variant_b_dmr6943 };\n'
  '\tuint8_t i = fd6818_detect(&fd, &fd_bus.bus, v, 2, RF_PROBE_HZ, RF_LOCK_MS);\n'
  '\trf_b = i == 1;\n'
  '\trf_unlocked = i == 2;\n'
  '\tfd6818_set_state(&fd, FD6818_STATE_IDLE);\n'
  '\tif (rf_b) {\n'
  '\t\thrc7000_wr(&modem, C7_RF_MODE, hrc7000_rd(&modem, C7_RF_MODE) & ~C7_RF_MODE_RX_INVERT);\n'
  '\t}\n'
  '}\n'
  '\n'
  'static void init(void)'),
 ('\tfd6818_attach(&fd, &fd_bus.bus);\n\tfd6818_init(&fd);\n\tfd6818_set_mode(&fd, FD6818_MODE_FM);',
  '\trf_detect();'),
 ('(am && mode != RADIO_MODE_FM)', '(am && (mode != RADIO_MODE_FM || rf_b))'),
 ('fd6818_set_rx_tone(&fd, fd_tone(tone != NULL ? tone->type : TONE_NONE), tone != NULL ? tone->value : '
  '0);',
  'fd_rx_subtone(fd_tone(tone != NULL ? tone->type : TONE_NONE), tone != NULL ? tone->value : 0);'),
 ('if (!board_in_band(hz, true)) {', 'if (!board_in_band(hz, true) || rf_unlocked) {'),
 ('\t\tfd6818_fm_tx_prepare(&fd);',
  '\t\tfd6818_fm_tx_prepare(&fd);\n'
  '\t\tif (rf_b && bf_cal_valid()) fd6818_write(&fd, FD6818_REG_BAND_B,\n'
  '\t\t\t(uint16_t)(FD_REG40_BASE + bf_cal_tx_byte(hz, BF_ROW_REG40)));'),
 ('\t\tfd6818_set_state(&fd, FD6818_STATE_TX);',
  '\t\tif (rf_b) fd6818_write(&fd, FD_REG_TX_AUX, FD_TX_AUX_FM_PRE);\n'
  '\t\tfd6818_set_state(&fd, FD6818_STATE_TX);\n'
  '\t\tif (rf_b) { fd6818_write(&fd, FD_REG_TX_AUX, FD_TX_AUX_FM); hal_delay_ms(1); }')]


def prepare():
    sources.verify_vendored()
    if not UP.exists():
        run(["git", "clone", "--no-checkout", "https://github.com/RadioFoundry/RadioFoundry", UP])
        run(["git", "-C", UP, "checkout", "--detach", PIN])
    if git(UP, "rev-parse", "HEAD") != PIN or git(UP, "status", "--porcelain", "--untracked-files=no"):
        raise SystemExit("Refusing a changed pristine RadioFoundry pin")
    if not DEST.exists():
        run(["git", "clone", "--shared", "--no-hardlinks", UP, DEST])
    if git(DEST, "rev-parse", "HEAD") != PIN:
        raise SystemExit("Refusing incompatible derivative HEAD")
    run(["git", "-C", DEST, "submodule", "update", "--init", "third_party/FreeRTOS-Kernel"])
    # RadioFoundry 88283f6: NRFB .050 tables extend 16 bytes beyond NRF .049.
    transform("boards/dm32/memory.ld", [
        ("0x18003E28", "0x18003E38", 2),
    ])
    # Refresh the existing generated registry from its authoritative linker
    # symbol. The pinned host tools consume this JSON on Python 3.9 as well.
    registry_path = "tools/rfoundry/rfoundry/registry.json"
    original_registry = (UP / registry_path).read_bytes()
    registry = json.loads(original_registry)
    table_end = int(re.search(r"__codec_tab_hi = (0x[0-9A-Fa-f]+);",
                             (DEST / "boards/dm32/memory.ld").read_text())[1], 16)
    registry["boards"]["dm32"]["codec_tables"][1] = table_end
    write_owned(DEST / registry_path, (json.dumps(registry, indent=1) + "\n").encode(), original_registry)
    transform("config/features.mk", [
        ('# Dependencies\n', 'CONFIG_P25 ?= n\nCONFIG_P25_NATIVE ?= n\nCONFIG_FIRSTBOOT ?= n\nCONFIG_P25_BENCH ?= y\nCONFIG_CPU_PROFILE ?= n\nCONFIG_DEV_UPDATE ?= n\nCONFIG_DEV_TX ?= n\nCONFIG_P25_CHANNELS ?= n\nFEATURES += P25 P25_NATIVE FIRSTBOOT P25_BENCH CPU_PROFILE DEV_UPDATE DEV_TX P25_CHANNELS\nifeq ($(CONFIG_DEV_TX),y)\nifneq ($(CONFIG_P25_NATIVE),y)\n$(error DEV_TX requires P25_NATIVE=y)\nendif\nendif\nifeq ($(CONFIG_DEV_UPDATE),y)\nifneq ($(CONFIG_FIRSTBOOT)$(CONFIG_STUB_ENTRY),yy)\n$(error DEV_UPDATE requires FIRSTBOOT=y STUB_ENTRY=y)\nendif\nendif\nifeq ($(CONFIG_FIRSTBOOT),y)\nifneq ($(CONFIG_CPS_OPENFW)$(CONFIG_CPS_FAST),nn)\n$(error FIRSTBOOT requires CPS_OPENFW=n CPS_FAST=n)\nendif\nifneq ($(CONFIG_STUB_ENTRY),$(CONFIG_DEV_UPDATE))\n$(error FIRSTBOOT requires STUB_ENTRY to match DEV_UPDATE)\nendif\nendif\nifeq ($(CONFIG_P25_NATIVE),y)\nifneq ($(CONFIG_P25)$(CONFIG_DMR),yy)\n$(error P25_NATIVE needs P25=y and DMR=y)\nendif\nendif\n\n# Dependencies\n'),
    ])
    transform("core/config.h", [
        ('#define RFOUNDRY_CONFIG_H\n', '#define RFOUNDRY_CONFIG_H\n#ifndef CONFIG_P25_CHANNELS\n#define CONFIG_P25_CHANNELS 0\n#endif\n#ifndef CONFIG_P25\n#define CONFIG_P25 0\n#endif\n#ifndef CONFIG_P25_NATIVE\n#define CONFIG_P25_NATIVE 0\n#endif\n#ifndef CONFIG_P25_BENCH\n#define CONFIG_P25_BENCH 1\n#endif\n#ifndef CONFIG_DEV_UPDATE\n#define CONFIG_DEV_UPDATE 0\n#endif\n#ifndef CONFIG_DEV_TX\n#define CONFIG_DEV_TX 0\n#endif\n#ifndef CONFIG_CPU_PROFILE\n#define CONFIG_CPU_PROFILE 0\n#endif\n#ifndef CONFIG_FIRSTBOOT\n#define CONFIG_FIRSTBOOT 0\n#endif\n#if CONFIG_FIRSTBOOT\n#include "firstboot/firstboot.h"\n#endif\n'),
    ])
    # The CPS offers saved-channel modes from VERSION's shared feature list.
    # Lab-only P25 builds must not claim that they support saved P25 channels.
    transform("core/hostlink/build_info.c", [
        ('#if CONFIG_DMR\n\t"DMR,"\n#endif',
         '#if CONFIG_DMR\n\t"DMR,"\n#endif\n#if CONFIG_P25_CHANNELS\n\t"P25,"\n#endif'),
    ])
    transform("Makefile", [
        ("APP_DIRS := $(CORE_DIRS)", "ifeq ($(CONFIG_P25),y)\nCORE_DIRS += core/p25 core/p25/cai\nP25_LIB := core/p25/libdm32_imbe.a\nendif\nifeq ($(CONFIG_P25_NATIVE),y)\nCORE_DIRS += core/native_modem\nendif\nifeq ($(CONFIG_FIRSTBOOT),y)\nCORE_DIRS += core/firstboot\nendif\nAPP_DIRS := $(CORE_DIRS)"),
        ('$(BUILD)/core/hostlink/build_info.o: CFLAGS_APP +=',
         '$(BUILD)/core/p25/memory.o: CFLAGS_APP += -O2 -fno-builtin -fno-tree-loop-distribute-patterns -fno-lto\n$(TARGET).elf: LDFLAGS += -Wl,-z,noexecstack\n\n$(BUILD)/core/hostlink/build_info.o: CFLAGS_APP +='),
        ("INCLUDES := -Ihal", "INCLUDES := -Icore/p25 -Icore/p25/cai -Icore/native_modem -Ihal"),
        ("$(TARGET).elf: $(OBJ) $(LDSCRIPT)", "$(TARGET).elf: $(OBJ) $(P25_LIB) $(LDSCRIPT)"),
        ("-o $@ $(OBJ) -lm", "-o $@ $(OBJ) $(P25_LIB) -lm"),
    ])
    transform("core/app/app.c", [
        ('#include "config.h"', '#include "config.h"\n#if CONFIG_P25\n#include "p25/p25_lab.h"\n#endif'),
        ("\tradio_core_start();", "\tradio_core_start();\n#if CONFIG_P25\n\tp25_lab_start();\n#endif"),
    ])
    transform("core/debuglink/debuglink.c", [
        ('#include "config.h"', '#include "config.h"\n#if CONFIG_P25\n#include "p25/p25_lab.h"\n#endif\n#if CONFIG_P25_NATIVE\n#include "native_modem/native_lab.h"\n#endif'),
        ("\tswitch (cmd) {", "#if CONFIG_FIRSTBOOT\n\tif (!firstboot_debug_allowed(cmd)) return status_only(out, DL_ST_REFUSED);\n\tif (cmd==DL_CMD_REBOOT && !firstboot_reboot_allowed(req,len)) return status_only(out, DL_ST_REFUSED);\n#endif\n\tswitch (cmd) {"),
        ("case DL_CMD_PING:", "case 0x22: /* private derivative: immutable first-boot restrictions */\n#if CONFIG_FIRSTBOOT\n\t\treturn firstboot_command(req, len, out, cap);\n#else\n\t\treturn status_only(out, DL_ST_UNAVAILABLE);\n#endif\n\tcase 0x21: /* private derivative: bounded native PHY experiment */\n#if CONFIG_P25_NATIVE\n\t\treturn native_lab_command(req, len, out, cap);\n#else\n\t\treturn status_only(out, DL_ST_UNAVAILABLE);\n#endif\n\tcase 0x20: /* private derivative: offline P25 laboratory */\n#if CONFIG_P25\n\t\treturn p25_lab_command(req, len, out, cap);\n#else\n\t\treturn status_only(out, DL_ST_UNAVAILABLE);\n#endif\n\tcase DL_CMD_PING:"),
    ])
    transform("core/stubentry/stub_entry.c", [
        ('#include "config.h"', '#include "config.h"\n#if CONFIG_DEV_UPDATE || CONFIG_P25_CHANNELS\n#include "native_modem/native_lab.h"\n#include "p25/p25_lab.h"\n#endif'),
        ('size_t stub_entry_request(const uint8_t *req, uint16_t len, uint8_t *out, size_t cap)\n{', 'size_t stub_entry_request(const uint8_t *req, uint16_t len, uint8_t *out, size_t cap)\n{\n#if CONFIG_P25_CHANNELS\n\tif (native_lab_reserved() || p25_lab_busy()) { if (!cap) return 0; out[0] = DL_ST_BUSY; return 1; }\n#endif'),
        ('\tconst hal_system_t *s = sys();\n\n\tif (s == NULL)', '#if CONFIG_DEV_UPDATE\n\tif (cap < STUB_REPLY_MAX) { out[0]=DL_ST_BAD_ARG;return 1; }\n\tif (!firstboot_update_busy() && (native_lab_reserved() || p25_lab_busy())) { out[0]=DL_ST_BUSY;return 1; }\n\tif (!firstboot_update_request(req,len)) { out[0]=DL_ST_REFUSED;return 1; }\n#endif\n\tconst hal_system_t *s = sys();\n\n\tif (s == NULL)'),
        ('\tif (!ready) {', '#if CONFIG_DEV_UPDATE\n\tif (req && len && req[0]==STUB_OP_BEGIN) { uint32_t size=0;(void)s->stub_region(&size); }\n#endif\n\tif (!ready) {'),
        ('\tif (on_air()) {\t\t\t\t// PTT went down after the ENTER was accepted', '#if CONFIG_DEV_UPDATE\n\tif (!firstboot_update_busy())return;\n#endif\n#if CONFIG_P25_CHANNELS\n\tif (native_lab_reserved() || p25_lab_busy()) return;\n#endif\n\tif (on_air()) {\t\t\t\t// PTT went down after the ENTER was accepted'),
    ])
    transform("platform/hrc7000/system.c", [
        ('\t.enter_stub = enter_stub,', '\t.enter_stub = enter_stub,\n#if CONFIG_DEV_UPDATE\n\t.stub_image_ok = firstboot_stub_image_ok,\n#endif'),
    ])
    transform("platform/hrc7000/FreeRTOSConfig.h", [
        ("#define configUSE_STATS_FORMATTING_FUNCTIONS\t0", "#define configUSE_STATS_FORMATTING_FUNCTIONS\t0\n#define configGENERATE_RUN_TIME_STATS CONFIG_CPU_PROFILE\n#if CONFIG_CPU_PROFILE && !defined(__ASSEMBLER__)\nuint32_t hrc_micros(void);\n#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() ((void)0)\n#define portGET_RUN_TIME_COUNTER_VALUE() hrc_micros()\n#endif"),
    ])
    transform("hal/board.h", [("const hal_radio_t *radio;", "const struct nt_ops *native_modem; /* optional derivative native PHY test backend */\n\tvoid (*native_mic_route)(bool on);\n\tconst hal_radio_t *radio;")])
    transform("hal/hal_audio.h", [("bool (*spk_pending)(void);", "bool (*spk_pending)(void);\n\t/* Optional laboratory playback pacing and fixed read-only diagnostics. */\n\tvoid (*playback_pace)(bool on);\n\tvoid (*playback_stats)(uint32_t out[16]);")])
    transform("platform/hrc7000/audio_pcm.h", [
        ('void hrc_audio_pcm_hold(bool on);', 'void hrc_audio_pcm_hold(bool on);\n#if CONFIG_P25\nbool hrc_audio_pcm_amp_wanted(void);\n#endif'),
    ])
    # FIFO scheduling/queue fix from RadioFoundry RX audio PR #660 (MIT).
    transform("platform/hrc7000/platform.h", [
        ('#define TIMER_BOARD', '#define TIMER_PCM\t\t6\t// speaker refill independent of block callbacks\n#define TIMER_BOARD'),
    ])
    transform("platform/hrc7000/audio_pcm.c", [
        ('// the task pushes the queue to the prompt FIFO, 32 samples at a time while b31 says there is room, as stock does. The', '// a 2 ms timer pushes the queue to the prompt FIFO, 32 samples at a time while b31 says there is room. The'),
        ('#define SPK_BLOCKS\t\t3', '#define SPK_BLOCKS\t\t4\t// three queued blocks and one reserved for the producer'),
        ('static bool spk_routed;', 'static volatile bool spk_routed;'),
        ('// Push 32-sample chunks while the FIFO has room (AUDIO_CONTROL b31 clear). Task context: the task tops it up every\n// 2 ms while the speaker is routed, faster than a chunk (4 ms) plays', '// Timer context: refill every 2 ms even while a block callback occupies the audio task.'),
        ('static void audio_task(void *arg)', 'static void fifo_tick(void)\n{\n\thrc_timer_ack(TIMER_PCM);\n\tfifo_feed();\n}\n\nstatic void audio_task(void *arg)'),
        ('ulTaskNotifyTake(pdTRUE, spk_routed ? pdMS_TO_TICKS(2) : tx_gated ? pdMS_TO_TICKS(10) : portMAX_DELAY);', 'ulTaskNotifyTake(pdTRUE, tx_gated ? pdMS_TO_TICKS(10) : portMAX_DELAY);'),
        ('\t\tgate_check();\n\t\tfifo_feed();', '\t\tgate_check();'),
        ('\t\tif (next == spk_tail) {\n\t\t\tstatic int16_t discard[AUDIO_BLOCK_SAMPLES];\n\t\t\tcb(mic, discard, block_ctx);\t// the speaker side is full (PCM not routed): keep the mic side going\n\t\t} else {\n\t\t\tint16_t *q = spk_q[spk_head];\n\t\t\tuint8_t gen = spk_gen;\n\t\t\tcb(mic, q, block_ctx);\n\t\t\tbool loud = false;\n\t\t\tfor (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES && !loud; i++) {\n\t\t\t\tloud = q[i] != 0;\n\t\t\t}\n\t\t\t// A gate edge emptied the queue during cb() (another task): this block is as stale as the rest\n\t\t\ts = hrc_irq_save();\n\t\t\tif (gen == spk_gen) {\n\t\t\t\tspk_loud[spk_head] = loud;\n\t\t\t\tspk_head = next;\n\t\t\t}\n\t\t\thrc_irq_restore(s);\n\t\t}', '\t\t// The reserved head slot is safe to fill even when the queue is full.\n\t\tint16_t *q = spk_q[spk_head];\n\t\tuint8_t gen = spk_gen;\n\t\tcb(mic, q, block_ctx);\n\t\tbool loud = false;\n\t\tfor (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES && !loud; i++) {\n\t\t\tloud = q[i] != 0;\n\t\t}\n\t\t// The timer may have freed space during cb(); a gate edge makes this block stale.\n\t\ts = hrc_irq_save();\n\t\tif (gen == spk_gen && next != spk_tail) {\n\t\t\tspk_loud[spk_head] = loud;\n\t\t\tspk_head = next;\n\t\t}\n#if CONFIG_P25\n\t\telse play_diag[gen == spk_gen ? 8 : 9]++;\n#endif\n\t\thrc_irq_restore(s);'),
        ('\t\tblocks++;\n\t\tfifo_feed();', '\t\tblocks++;'),
        ('\t\thrc_irq_register(IRQ_PCM_WR, isr_pcm_wr);', '\t\thrc_irq_register(IRQ_PCM_WR, isr_pcm_wr);\n\t\thrc_irq_register(IRQ_TIMER6, fifo_tick);\n\t\thrc_irq_enable(IRQ_TIMER6, true);'),
        ('\tblock_ctx = ctx;', '\tuint32_t irq = hrc_irq_save();\n\tblock_ctx = ctx;'),
        ('\thrc7000_audio_bits(modem, C7_AUDIO_PCM_MIC, 0);\t// under the lock set at attach', '\thrc7000_audio_bits(modem, C7_AUDIO_PCM_MIC, 0);\t// under the lock set at attach\n\thrc_timer_periodic(TIMER_PCM, 2000u);\n\thrc_irq_restore(irq);'),
        ('\trunning = false;', '\trunning = false;\n\thrc_timer_stop(TIMER_PCM);'),
        ('static void speaker_pcm(bool on)\n{', 'static void speaker_pcm(bool on)\n{\n\tuint32_t irq = hrc_irq_save();'),
        ('\tif (speaker_pcm_cb != NULL) {\n\t\tspeaker_pcm_cb(on);\n\t}', '\tif (speaker_pcm_cb != NULL) {\n\t\tspeaker_pcm_cb(on);\n\t}\n\thrc_irq_restore(irq);'),

        ('\tfifo_held = on;', '\tfifo_held = on;\n#if CONFIG_P25\n\tplayback_amp_update();\n#endif'),
        ('\t\ttx_gated = true;', '\t\ttx_gated = true;\n#if CONFIG_P25\n\t\tplayback_amp_update();\n#endif'),
        ('static void gate_check(void);', 'static void gate_check(void);\n#include "pcm_playback.inc"'),
        ('static void fifo_feed(void)\n{', 'static void fifo_feed(void)\n{\n#if CONFIG_P25\n\tplayback_amp_update();\n#endif'),
        ('\tspk_routed = on;', '\tspk_routed = on;\n#if CONFIG_P25\n\tplay_amp_on = playback_amp_want();\n#endif'),
        ('hrc7000_prompt_push(modem, &spk_q[spk_tail][spk_pos])' , 'playback_push(&spk_q[spk_tail][spk_pos])'),
        ('\t\t\tspk_tail = (uint8_t)((spk_tail + 1u) % SPK_BLOCKS);\n\t\t}\n\t}', '\t\t\tspk_tail = (uint8_t)((spk_tail + 1u) % SPK_BLOCKS);\n\t\t}\n#if CONFIG_P25\n\t\tif (play_paced) break;\n#endif\n\t}'),
        ('\trunning = false;', '\trunning = false;\n#if CONFIG_P25\n\tplay_paced = false;\n#endif'),
        ('\t.spk_pending = hrc_audio_pcm_spk_pending,', '\t.spk_pending = hrc_audio_pcm_spk_pending,\n#if CONFIG_P25\n\t.playback_pace = playback_pace,\n\t.playback_stats = playback_stats,\n#endif'),
    ])
    write_owned(DEST / "platform/hrc7000/pcm_playback.inc", (ROOT / "integration/radiofoundry/pcm_playback.inc").read_bytes())
    transform("boards/dm32/board.c", [
        ('const board_t board = {', '#if CONFIG_P25_CHANNELS\nstatic uint32_t dm32_cps_type(void) { return 11; }\nstatic const hal_cps_t dm32_cps = { .radio_type = dm32_cps_type };\n#endif\nconst board_t board = {'),
        ('\t.system = &hrc_system,', '\t.system = &hrc_system,\n#if CONFIG_P25_CHANNELS\n\t.cps = &dm32_cps,\n#endif'),
        ('.radio = &dm32_radio,', '.radio = &dm32_radio,\n#if CONFIG_P25_NATIVE\n\t.native_modem = &dm32_native_ops,\n#endif\n#if CONFIG_DEV_TX\n\t.native_mic_route = dm32_native_mic_route,\n#endif'),
        ('#include "pins.h"', '#include "pins.h"\n#include "dm32_firstboot.inc"'),
        ('static const hrc_gps_cfg_t gps_cfg = {', '#if CONFIG_GPS\nstatic const hrc_gps_cfg_t gps_cfg = {'),
        ('\t.power_on_level = GPS_POWER_ON_LEVEL,\n};', '\t.power_on_level = GPS_POWER_ON_LEVEL,\n};\n#endif'),
    ])
    transform("boards/dm32/dm32.h", [("#include <stdbool.h>", "#include <stdbool.h>\n#if CONFIG_P25_NATIVE\n#include \"native_test.h\"\nextern nt_ops dm32_native_ops;\n#endif\n#if CONFIG_DEV_TX\nvoid dm32_native_mic_route(bool on);\n#endif")])
    transform("boards/dm32/radio_dm32.c", NRFB_BOARD_PATCHES + [
        # Own the microphone route before any preemptible TX setup writes.
        ('\thrc_dac_b_power(true);\n\tif (mode == RADIO_MODE_DMR) {',
         '\thrc_dac_b_power(true);\n\ttransmitting = true;\n\tif (mode == RADIO_MODE_DMR) {'),
        ('\ttransmitting = true;\n\tring_play();\n\treturn true;',
         '\tring_play();\n\treturn true;'),
        ('\t\tif (on) {\n\t\t\tamp(AMP_PCM, true);\n\t\t}\n\t}\n\thrc_irq_restore(s);',
         '\t\tif (on) {\n#if !CONFIG_P25\n\t\t\tamp(AMP_PCM, true);\n#endif\n\t\t}\n\t}\n#if CONFIG_P25\n\tif (on) amp(AMP_PCM, hrc_audio_pcm_amp_wanted());\n#endif\n\thrc_irq_restore(s);'),
        ("static void pa_key(void)\n{", "static void pa_key(void)\n{\n#if CONFIG_FIRSTBOOT\n\tif (!firstboot_tx_allowed(FB_PA_KEY)) { firstboot_deny(FB_PA_KEY); board_rf_safe(); return; }\n#endif"),
        ("static void dmr_rf_switch(bool tx)\n{", "static void dmr_rf_switch(bool tx)\n{\n#if CONFIG_FIRSTBOOT\n\tif (tx && !firstboot_tx_allowed(FB_RF_SWITCH)) { firstboot_deny(FB_RF_SWITCH); board_rf_safe(); return; }\n#endif"),
        ("static bool tx(uint32_t hz, radio_bw_t bw, uint8_t power, const subtone_t *tone)\n{", "static bool tx(uint32_t hz, radio_bw_t bw, uint8_t power, const subtone_t *tone)\n{\n#if CONFIG_FIRSTBOOT\n\tif (!firstboot_tx_allowed(FB_BOARD_TX) || hz!=145500000u || bw!=RADIO_BW_12K5 || power!=0 || mode!=RADIO_MODE_DMR) { firstboot_deny(FB_BOARD_TX); board_rf_safe(); return false; }\n#endif"),
        ("static void init(void)", "#if CONFIG_P25_NATIVE\nstatic void native_init(void);\n#endif\nstatic void init(void)"),
        ("hrc_fm_rx_attach(&modem);", "hrc_fm_rx_attach(&modem);\n#if CONFIG_P25_NATIVE\n\tnative_init();\n#endif"),
    ])
    # Append board glue while preserving ownership checks on generated files.
    board_path=DEST / "boards/dm32/radio_dm32.c"
    board_body=board_path.read_bytes()
    write_owned(board_path,board_body+b'\n#include "dm32_native.inc"\n',board_body)
    transform("platform/hrc7000/gpio.c", [
        ('#include "platform.h"', '#include "platform.h"\n#include "config.h"'),
        ('\tbank_rmw(hrc_gpio_bank(pin) + GPIO_DR, hrc_gpio_bit(pin), level);', '#if CONFIG_FIRSTBOOT\n\tlevel=board_firstboot_pin_level(pin,level);\n#endif\n\tbank_rmw(hrc_gpio_bank(pin) + GPIO_DR, hrc_gpio_bit(pin), level);')])
    transform("platform/hrc7000/dac.c", [
        ('#include "platform.h"', '#include "platform.h"\n#include "config.h"'),
        ('DAC_PD_CTRL = DAC_A_OFF;', 'DAC_PD_CTRL = DAC_A_OFF | (CONFIG_FIRSTBOOT ? DAC_B_OFF : 0);'),
        ('void hrc_dac_b_power(bool on)\n{', 'void hrc_dac_b_power(bool on)\n{\n#if CONFIG_FIRSTBOOT\n\tif (on && !firstboot_tx_allowed(FB_BOARD_TX))on=false;\n#endif')])
    transform("platform/hrc7000/flash_spi0.c", [
        ('#include "platform.h"', '#include "platform.h"\n#include "config.h"'),
        ('static bool wr(uint32_t addr, const void *buf, uint32_t len)\n{', 'static bool wr(uint32_t addr, const void *buf, uint32_t len)\n{\n#if CONFIG_FIRSTBOOT\n\treturn firstboot_deny(FB_FLASH_WRITE);\n#endif'),
        ('static bool er(uint32_t addr)\n{', 'static bool er(uint32_t addr)\n{\n#if CONFIG_FIRSTBOOT\n\treturn firstboot_deny(FB_FLASH_ERASE);\n#endif')])
    transform("drivers/flash/spi_nor/spi_nor.c", [
        ('#include "spi_nor.h"', '#include "spi_nor.h"\n#include "config.h"'),
        ('static bool write_enable(const spi_nor_t *nor)\n{', 'static bool write_enable(const spi_nor_t *nor)\n{\n#if CONFIG_FIRSTBOOT\n\treturn firstboot_deny(FB_NOR_WRITE_ENABLE);\n#endif')])
    transform("drivers/rf/fd6818/fd6818.h", NRFB_HEADER_PATCHES)
    write_owned(DEST / "drivers/rf/fd6818/fd6818_setup_b.c",
                (ROOT / "integration/radiofoundry/rf/fd6818_setup_b.c").read_bytes())
    transform("drivers/rf/fd6818/fd6818.c", NRFB_DRIVER_PATCHES + [
        # Preserve Classic's FM-tone to DMR handoff fix for the original setup.
        ('static const regval_t dmr_table[] = {', 'static const regval_t dmr_table[] = {\n\t{ 0x51, 0x0000 }, // Digital modulation comes from the C7000.'),
        ('#include "fd6818.h"', '#include "fd6818.h"\n#include "config.h"'),
        ('void fd6818_write(fd6818_t *dev, uint8_t reg, uint16_t val)\n{', 'void fd6818_write(fd6818_t *dev, uint8_t reg, uint16_t val)\n{\n#if CONFIG_FIRSTBOOT\n\tif (reg==FD6818_REG_STATE && val==FD6818_STATE_TX && !firstboot_tx_allowed(FB_FD_TX)) { firstboot_deny(FB_FD_TX); return; }\n#endif')])
    transform("core/storage/storage.c", [
        ('#include <string.h>', '#include <string.h>\n#include "config.h"'),
        ('bool storage_claim_request(void)\n{', 'bool storage_claim_request(void)\n{\n#if CONFIG_FIRSTBOOT\n\treturn firstboot_deny(FB_STORAGE_CLAIM);\n#endif'),
        ('bool storage_claim(void)\n{', 'bool storage_claim(void)\n{\n#if CONFIG_FIRSTBOOT\n\treturn firstboot_deny(FB_STORAGE_CLAIM);\n#endif')])
    transform("platform/hrc7000/common.ld", [
        ("*(.data .data.* .sdata .sdata.*)", "*(.data .data.* .sdata .sdata.* .ramdata .ramdata.*)"),
        ("*(EXCLUDE_FILE(*hrc7000_dmr.o *hrc7000_bs.o *dmr_glue.o) .text EXCLUDE_FILE(*hrc7000_dmr.o *hrc7000_bs.o *dmr_glue.o) .text.*)",
         "*(EXCLUDE_FILE(*hrc7000_dmr.o *hrc7000_bs.o *dmr_glue.o *native_test.o *hrc_native_chip.o *hrc_native_port.o) .text EXCLUDE_FILE(*hrc7000_dmr.o *hrc7000_bs.o *dmr_glue.o *native_test.o *hrc_native_chip.o *hrc_native_port.o) .text.*)"),
        ("*dmr_glue.o(.text .text.*)", "*dmr_glue.o(.text .text.*)\n\t\t*native_test.o(.text .text.*)\n\t\t*hrc_native_chip.o(.text .text.*)\n\t\t*hrc_native_port.o(.text .text.*)"),
    ])
    transform("core/input/input_task.c", [
        ('static void input_task(void *arg)', '#if CONFIG_DEV_TX\nstatic const keymap_entry_t *key_for(uint8_t action, bool long_action);\nstatic volatile bool physical_ptt;\nbool input_physical_ptt(void) { return physical_ptt; }\nbool input_ptt_down(void)\n{\n    uint32_t now=hal_millis();\n    taskENTER_CRITICAL();\n    bool down=!ptt_blocked && (physical_ptt || (ptt_held && !due(now,ptt_until)));\n    taskEXIT_CRITICAL();\n    return down;\n}\n#endif\n\nstatic void input_task(void *arg)'),
        ('\t\traw_keys = keys;', '\t\traw_keys = keys;\n#if CONFIG_DEV_TX\n\t\tconst keymap_entry_t *ptt = keys && !ptt_blocked ? key_for(ACT_PTT, false) : NULL;\n\t\tbool physical_down = ptt && (keys & (1u << ptt->bit));\n\t\tif (physical_down != physical_ptt) {\n\t\t\tphysical_ptt = physical_down;\n\t\t\tradio_req_physical_ptt(physical_down);\n\t\t}\n#endif'),
    ])
    transform("core/input/input.h", [("uint32_t input_raw_keys(void);", "uint32_t input_raw_keys(void);\n#if CONFIG_DEV_TX\nbool input_physical_ptt(void);\nbool input_ptt_down(void);\n#endif")])
    transform("core/radio/radio.h", [
        ("uint8_t tx_deny;", "#if CONFIG_P25_CHANNELS\n\tuint32_t p25_source;\n\tchar p25_alias[CODEPLUG_NAME_LEN + 1], p25_contact[CODEPLUG_NAME_LEN + 1];\n#endif\n\tuint8_t tx_deny;"),
        ("void radio_req_ptt(bool down);", "void radio_req_ptt(bool down);\n#if CONFIG_DEV_TX\nvoid radio_req_physical_ptt(bool down);\n#endif")])
    transform("core/radio/radio_task.c", [
        ('#include "config.h"', '#include "config.h"\n#if CONFIG_P25_NATIVE\n#include "native_modem/native_lab.h"\n#endif'),
        ('for (;;) {\n\t\tTickType_t due', 'radio_req_t req;\n    bool retained_request=false;\n\tfor (;;) {\n#if CONFIG_P25_NATIVE\n#if CONFIG_P25_CHANNELS\n        /* Saved edits use the existing retained receiver handoff. */\n        if (!retained_request && !sm.ptt && sm.view.state!=RS_TX &&\n            native_lab_owns() && codeplug_serial()!=sm.codeplug_serial) {\n            req=(radio_req_t){.type=RREQ_RELOAD};\n            retained_request=true;\n        }\n#endif\n\t\tnative_lab_service(&sm,hal_micros());\n#endif\n\t\tTickType_t due'),
        ('radio_req_t req;\n\t\tif (xQueueReceive(requests, &req, left > 0 ? (TickType_t)left : 0) == pdTRUE) {', '#if CONFIG_P25_NATIVE\n\t\tint32_t native_wait=(int32_t)native_lab_wait_ms();\n\t\tif (native_wait && left>native_wait) left=native_wait;\n#endif\n        if (retained_request || xQueueReceive(requests,&req,left>0?(TickType_t)left:0)==pdTRUE) {\n            retained_request=false;'),
        ('req_handled++;', 'req_handled++;\n#if CONFIG_P25_NATIVE\n                int handled=native_lab_request(&sm,&req);\n                if(handled==NATIVE_REQUEST_WAIT){retained_request=true;vTaskDelay(1);break;}\n                if(handled)continue;\n#endif'),
        ('TickType_t now = xTaskGetTickCount();', 'TickType_t now = xTaskGetTickCount();\n#if CONFIG_P25_NATIVE\n\t\tif (native_lab_owns()) {\n            next_tick=now+tick_period;\n#if CONFIG_P25_CHANNELS\n            if((int32_t)(now-next_poll)>=0){\n                radio_sm_native_maintenance(&sm,hal_millis());\n                next_poll=now+period;\n            }\n#else\n            next_poll=now+period;\n#endif\n            publish();continue;\n        }\n#endif'),
        ('radio_sm_init(&sm, hal_millis());', 'radio_sm_init(&sm, hal_millis());\n#if CONFIG_P25_NATIVE\n\tnative_lab_init();\n#endif'),
        ('void radio_req_ptt(bool down)', '#if CONFIG_DEV_TX\nvoid radio_req_physical_ptt(bool down)\n{ post(RREQ_PTT, down ? NATIVE_PHYSICAL_PTT_DOWN : NATIVE_PHYSICAL_PTT_UP, pdMS_TO_TICKS(50)); }\n#endif\nvoid radio_req_ptt(bool down)'),
    ])
    # Upstream intentionally bounds the CPS version to 15 bytes in a zeroed
    # buffer. memcpy makes that deliberate truncation explicit for long tags.
    transform("core/cps/cps_openfw.c", [
        ('strncpy((char *)&p[8], RFOUNDRY_GIT, 15);',
         'memcpy(&p[8], RFOUNDRY_GIT, sizeof(RFOUNDRY_GIT) - 1 < 15 ? sizeof(RFOUNDRY_GIT) - 1 : 15);'),
    ])
    # Reserve the worker's actual stack: 8 KiB bench / 4 KiB receive-only.
    # Keep the AMBE code/table windows and full-resolution framebuffer intact.
    transform("boards/dm32/freertos_board.h", [("#define configTOTAL_HEAP_SIZE\t(32 * 1024 + 512)",
        "#if CONFIG_P25 && (CONFIG_P25_BENCH || CONFIG_DEV_TX)\n#define configTOTAL_HEAP_SIZE (40 * 1024 + 512)\n#elif CONFIG_P25\n#define configTOTAL_HEAP_SIZE (36 * 1024 + 512)\n#else\n#define configTOTAL_HEAP_SIZE (32 * 1024 + 512)\n#endif")])
    # The C-SKY disassembler uses uppercase hex call operands and hexadecimal
    # SP adjustments. Preserve those edges/frames in the existing checker.
    transform('tools/stack_check.py', [
        ('([0-9a-f]+)\\b[^<]*<([^>+]+)>$")',
         '([0-9a-f]+)\\b[^<]*<([^>+]+)>$", re.IGNORECASE)'),
        ('m = re.match(r"^subi (?:sp|r14), (?:(?:sp|r14), )?(\\d+)$", ins)\n            if m:\n                cur[1] += int(m.group(1))',
         'm = re.match(r"^subi (?:sp|r14), (?:(?:sp|r14), )?(0x[0-9a-fA-F]+|\\d+)$", ins)\n            if m:\n                cur[1] += int(m.group(1), 0)'),
        ('subi      \\tr14, r14, 8', 'subi      \\tr14, r14, 0x8'),
        ('bsr      \\t0x304ed8c', 'bsr      \\t0x304ED8C'),
    ])
    classic_channels.prepare(transform)
    classic_standard.prepare(DEST,write_owned)
    # A pending shared beep remains owned by beep.c when digital PCM stops.
    # Reuse the existing own-stream startup and failure cleanup at handoff.
    beep_source=(UP/'core/radio/beep.c').read_text()
    own_begin=beep_source.index('\t\t// Always (re)start:')
    own_end=beep_source.index('\n\t}\n\tstats.played++;',own_begin)
    own_body=beep_source[own_begin:own_end]
    transform('core/radio/beep.c', [
        (own_body,'\t\tif (!start_own_stream()) return;'),
        ('void beep_play(beep_t b, uint8_t level, bool shared)',
         'static bool start_own_stream(void)\n{\n'+
         '\n'.join(line[1:] if line.startswith('\t') else line for line in
                   own_body.replace('\t\t\treturn;','\t\t\treturn false;').splitlines())+
         '\n    return true;\n}\n\nvoid beep_play(beep_t b, uint8_t level, bool shared)'),
        ('\tif (!own_stream) {\n\t\treturn;\n\t}', '''\tif (!own_stream) {
        if (!shared && beep_busy() && aud != NULL && aud->start != NULL)
            start_own_stream();
\t\treturn;
\t}'''),
    ])
    for p in sorted((ROOT / "src/p25").glob("*")):
        if p.suffix in (".c", ".h", ".inc"):
            write_owned(DEST / "core/p25" / p.name, p.read_bytes())
    # This generated table belonged to the retired implementation. Never
    # remove an unrecognized local input from an existing derivative tree.
    retired = DEST / "core/p25/golay_table.inc"
    if retired.exists():
        key = str(retired.relative_to(DEST))
        if hashlib.sha256(retired.read_bytes()).hexdigest() != OWNED.get(key):
            raise SystemExit(f"Preserving unexpected local changes: {retired}")
        retired.unlink()
        OWNED.pop(key)
    cai = ROOT / "third_party/p25cai"
    cai_inputs = [p for p in [*sorted((cai / "src").glob("*")), cai / "include/p25cai.h"]
                  if p.suffix in (".c", ".h", ".inc")]
    expected_cai = {p.name for p in cai_inputs}
    for p in sorted((DEST / "core/p25/cai").glob("*")):
        if p.name not in expected_cai:
            key = str(p.relative_to(DEST))
            if (not p.is_file() or p.is_symlink() or
                    hashlib.sha256(p.read_bytes()).hexdigest() != OWNED.get(key)):
                raise SystemExit(f"Preserving unexpected local changes: {p}")
            p.unlink()
            OWNED.pop(key)
    for p in cai_inputs:
        write_owned(DEST / "core/p25/cai" / p.name, p.read_bytes())
    write_owned(DEST / "core/p25/dm32_imbe.h", (ROOT / "src/codec/dm32_imbe.h").read_bytes())
    # Use the same measured memory implementation as the codec target tests.
    write_owned(DEST / "core/p25/memory.c", (ROOT / "src/runtime/memory.c").read_bytes())
    for p in sorted((ROOT / "integration/radiofoundry").glob("*")):
        if p.suffix in (".c", ".h"):
            write_owned(DEST / "core/p25" / p.name, p.read_bytes())
    for p in sorted((ROOT / "src/modem").glob("*")):
        if p.suffix in (".c", ".h"): write_owned(DEST / "core/native_modem" / p.name,p.read_bytes())
    for p in sorted((ROOT / "integration/radiofoundry/native").glob("*")):
        folder="boards/dm32" if p.suffix==".inc" else "platform/hrc7000" if p.name.startswith("hrc_") else "core/native_modem"
        write_owned(DEST / folder / p.name,p.read_bytes())
    for p in sorted((ROOT / "integration/radiofoundry/firstboot").glob("*")):
        folder="boards/dm32" if p.suffix==".inc" else "platform/hrc7000" if p.name=="firstboot_irq.c" else "core/firstboot"
        write_owned(DEST / folder / p.name,p.read_bytes())
    retired_license = DEST / "COPYING-P25"
    if retired_license.exists():
        key = str(retired_license.relative_to(DEST))
        if hashlib.sha256(retired_license.read_bytes()).hexdigest() != OWNED.get(key):
            raise SystemExit(f"Preserving unexpected local changes: {retired_license}")
        retired_license.unlink()
        OWNED.pop(key)
    write_owned(DEST / "COPYING-OPEN25", (ROOT / "LICENSE").read_bytes())
    write_owned(DEST / "COPYING-P25-CAI", (cai / "LICENSE").read_bytes())
    write_owned(DEST / "COPYING-P25-IMBE", (ROOT / "third_party/imbe/LICENSE").read_bytes())
    write_owned(DEST / "P25-DERIVATIVE.txt", (
        "Classic P25 integration. Preserve all per-file license notices.\n"
        "Open25 support, RadioFoundry and the pinned CAI library use MIT notices.\n"
        "The shared fixed-point C full-rate IMBE codec and Open25 interface use MIT notices.\n"
        "Preserve source provenance and notices in third_party/imbe.\n"
        "See COPYING-OPEN25, COPYING-P25-CAI, COPYING-P25-IMBE and the source manifest.\n"
        "Classic supports saved conventional P25 channels; final RF qualification is incomplete.\n"
    ).encode())
    OWNED_FILE.write_text(json.dumps(OWNED, indent=2) + "\n")
    changed=subprocess.check_output(["git","-C",str(DEST),"diff","--name-only","-z","HEAD"]).decode().split("\0")
    extra=subprocess.check_output(["git","-C",str(DEST),"ls-files","--others","--exclude-standard","-z"]).decode().split("\0")
    for key in filter(None,changed+extra):
        path=DEST/key
        if key in changed or path.suffix in (".c",".h",".inc",".ld",".mk"):
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=OWNED.get(key):
                raise SystemExit(f"Preserving unreviewed derivative input: {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--prepare-only", action="store_true")
    ap.add_argument("--first-boot", action="store_true", help="immutable TX/write inhibits; omit RAM loader and CPS")
    ap.add_argument("--disable", action="store_true", help="build same derivative with CONFIG_P25=n")
    ap.add_argument("--disable-native", action="store_true", help="keep offline P25 lab, omit native modem interface")
    ap.add_argument("--stack-check", action="store_true", help="include external codec call graphs in static stack report")
    ap.add_argument("--with-local-stock", action="store_true", help="merge recognized local stock AMBE via upstream tool; no redistribution or flashing")
    ap.add_argument("--verify-rebuild", action="store_true", help="force recompilation and require identical application bytes")
    ap.add_argument("--speed-codec", action="store_true", help="optimized full-rate IMBE build preset")
    ap.add_argument("--lean", action="store_true", help="omit replay, synthetic self-test and per-sample diagnostics")
    ap.add_argument("--cpu-profile", action="store_true", help="optional RTOS task runtime counters; excluded by default")
    ap.add_argument("--dev-update", action="store_true", help="approved RAM-helper cable entry; TX/write inhibits remain")
    ap.add_argument("--reuse-codec", action="store_true", help="reuse codec only when inputs/compiler/archive/call graphs match checked hashes")
    ap.add_argument("--tx-bench",action="store_true",help="bounded native TX on 145.500 MHz; retains ordinary PTT/storage guards")
    ap.add_argument('--classic', action='store_true', help='Classic FM/DMR/conventional P25 release preset')
    args = ap.parse_args()
    if args.classic:
        if args.first_boot or args.disable or args.disable_native or args.dev_update or args.tx_bench:
            ap.error('Classic is a separate normal-operation preset')
        args.speed_codec=args.lean=True
    if args.tx_bench and not (args.first_boot and args.dev_update):ap.error("tx-bench requires first-boot and dev-update")
    if args.dev_update and not args.first_boot:ap.error("dev-update needs first-boot restrictions")
    if args.disable and (args.lean or args.cpu_profile):
        ap.error("lean/CPU measurement require P25 enabled")
    if args.first_boot and (args.disable or args.disable_native or args.with_local_stock):
        ap.error("--first-boot requires native P25 and separately packaged exact-unit codec")
    prepare()
    if args.prepare_only:
        return
    if not (ROOT / "toolchain/csky-elf/lib/libc.a").exists():
        run(["sh", ROOT / "tools/bootstrap_newlib.sh"], cwd=ROOT)
    codec = ROOT / "build/radiofoundry-codec-c"
    reused_codec=False
    if not args.disable:
        cache_key=codec_cache.key(ROOT,args.speed_codec)
        reused_codec=args.reuse_codec and codec_cache.reusable(codec,cache_key)
        if not reused_codec:
            run(["python3", ROOT / "experiments/codec/build.py", "--target", "--out", codec,
                 *(["--speed"] if args.speed_codec else [])], cwd=ROOT)
            codec_cache.remember(codec,cache_key)
        source=codec/'libdm32_imbe.a';destination=DEST/'core/p25/libdm32_imbe.a'
        if not destination.exists() or destination.read_bytes()!=source.read_bytes():shutil.copyfile(source,destination)
        print('Codec cache: '+('verified reuse' if reused_codec else 'rebuilt and hashed'),flush=True)
    native=not args.disable and not args.disable_native
    suffix = "firstboot" if args.first_boot else "off" if args.disable else "native" if native else "lab"
    identity=suffix
    suffix += ("-lean" if args.lean else "") + ("-cpu" if args.cpu_profile else "") + ("-dev" if args.dev_update else "") + ("-tx" if args.tx_bench else "")
    if args.classic:
        suffix='classic'
        identity='classic-0.1.0-rc1-r12'
    b = f"build/dm32-p25-{suffix}"
    command = ["make", "-j8", "BOARD=dm32", f"CROSS={ROOT}/toolchain/bin/csky-elf-", f"BUILD={b}",
        "CONFIG_P25=" + ("n" if args.disable else "y"), "CONFIG_P25_NATIVE="+("y" if native else "n"), "CONFIG_DMR_IP_GATEWAY=n", "CONFIG_NET_PPP=n",
        "CONFIG_APRS=n", "CONFIG_SFR=n", "CONFIG_HOTSPOT=n", "CONFIG_ZONE_SHARE=n",
        'RFOUNDRY_VERSION='+('open25-'+identity if args.classic else 'dfbaf20c-p25-'+identity), f"RFOUNDRY_GIT_TIME={git(UP, 'show', '-s', '--format=%ct', 'HEAD')}",
        "RFOUNDRY_GIT_DIRTY=1",
        "CONFIG_FIRSTBOOT="+("y" if args.first_boot else "n"),
        "CONFIG_P25_BENCH="+("n" if args.lean else "y"),
        "CONFIG_CPU_PROFILE="+("y" if args.cpu_profile else "n"),
        "CONFIG_DEV_UPDATE="+("y" if args.dev_update else "n"),
        "CONFIG_DEV_TX="+("y" if args.tx_bench or args.classic else "n"),
        "CONFIG_P25_CHANNELS="+("y" if args.classic else "n"),
        f"OPT=-Os -ffile-prefix-map={DEST}=/radiofoundry -fdebug-prefix-map={DEST}=/radiofoundry "
        f"-DRFOUNDRY_SOURCE_EPOCH={git(UP, 'show', '-s', '--format=%ct', 'HEAD')}"]
    if args.classic:
        command += ['CONFIG_FM=y','CONFIG_DMR=y','CONFIG_CHANNEL_EDIT=y','CONFIG_CPS_OPENFW=y',
                    'CONFIG_CPS_FAST=y','CONFIG_STUB_ENTRY=y','CONFIG_DEBUGLINK=y',
                    *['CONFIG_'+name+'=n' for name in ('DMR_GPS','DMR_SMS','DMR_TMS','DMR_ARS',
                      'DMR_LRRP','DMR_SIGNALING','DMR_EMERGENCY','DMR_ROAMING','DMR_UDP_COMPRESS',
                      'DMR_CONFIRMED','CRASH_REPORT','BCAST_FM','AM_RX','DUAL_WATCH','VOX','GPS',
                      'LANG_ES','LANG_IT')]]
    if args.first_boot:
        command += ["CONFIG_STUB_ENTRY="+("y" if args.dev_update else "n"), "CONFIG_CPS_OPENFW=n", "CONFIG_CPS_FAST=n", "CONFIG_AUDIO_PROBE=n"]
    log = ROOT / f"logs/radiofoundry-p25-{suffix}.log"
    log.parent.mkdir(exist_ok=True)
    with log.open("w") as f:
        run(command, cwd=DEST, stdout=f, stderr=subprocess.STDOUT,
            env={**os.environ, "SOURCE_DATE_EPOCH": git(UP, "show", "-s", "--format=%ct", "HEAD")})
    # Full build command/output is retained. Reject compiler warnings too.
    if "warning:" in log.read_text():
        raise SystemExit(f"Build has warnings; review {log}")
    if args.stack_check:
        if not args.disable:
            ci = DEST / b / "p25-codec-ci"
            ci.mkdir(exist_ok=True)
            for p in ci.glob("*.ci"):p.unlink()
            graph = codec / "c-codec.ci"
            if not graph.is_file(): raise SystemExit("Missing the current C codec stack graph")
            shutil.copyfile(graph, ci / graph.name)
        stack_log = ROOT / f"logs/radiofoundry-stack-{suffix}.log"
        stack_args=[]
        if native:
            edges=DEST / b / "native-stack-edges.txt"
            extra="\n# Native interrupt entries use the separate 2048-byte IRQ stack.\n"
            extra+="task platform/hrc7000/audio_pcm.c:fifo_tick 512\n"
            extra+="".join(f"task platform/hrc7000/hrc_native_port.c:{fn} 512\n" for fn in ("sys","ts_tx","ts_rx","rftx","guard","pa"))
            # The source scanner does not follow the included board .inc.
            # Supply its actual function-pointer targets for conservative depth.
            for caller,targets in {"start":("native_prepare","native_irq_on"),
                "quiet":("native_off","native_irq_quiet"),"key":("native_key",),
                "pa":("native_pa",),"restore":("native_irq_restore","native_restore"),
                "lock":("native_lock",),"unlock":("native_unlock",),
                "native_lab_command":("snapshot",),
                "p25_voice_rx_dibit":("core/p25/p25_lab.c:sink","core/p25/selftest.c:sink"),
                "audio_task":("core/p25/p25_lab.c:audio_block","core/p25/p25_lab.c:mic_block")
                    if args.classic or args.tx_bench else ("core/p25/p25_lab.c:audio_block",),
                "start_audio":("playback_pace",),
                "p25_lab_command":("playback_stats",)}.items():
                extra+="call "+caller+" "+" ".join(targets)+"\n"
            extra+="call playback_amp_update dm32_speaker_pcm\n"
            edges.write_text((UP / "tools/stack_check_edges.txt").read_text()+extra)
            stack_args=[f"STACK_CHECK_ARGS=--edges {edges}"]
        with stack_log.open("w") as f:
            run([*command, *stack_args, "stack-check"], cwd=DEST, stdout=f, stderr=subprocess.STDOUT,
                env={**os.environ, "SOURCE_DATE_EPOCH": git(UP, "show", "-s", "--format=%ct", "HEAD")})
    prefix = DEST / b / "rfoundry_dm32"
    if args.verify_rebuild:
        before = prefix.with_suffix(".bin").read_bytes()
        rebuild_log=ROOT / f"logs/radiofoundry-rebuild-{suffix}.log"
        with rebuild_log.open("w") as f:
            run([*command, "-B"], cwd=DEST, stdout=f, stderr=subprocess.STDOUT,
                env={**os.environ, "SOURCE_DATE_EPOCH": git(UP, "show", "-s", "--format=%ct", "HEAD")})
        if "warning:" in rebuild_log.read_text():
            raise SystemExit(f"Forced rebuild has warnings; review {rebuild_log}")
        if prefix.with_suffix(".bin").read_bytes() != before:
            raise SystemExit("Application bytes differ on forced rebuild")
    unresolved = subprocess.check_output([str(ROOT / "toolchain/bin/csky-elf-nm"), "-u", str(prefix.with_suffix(".elf"))], text=True)
    disassembly = subprocess.check_output([str(ROOT / "toolchain/bin/csky-elf-objdump"), "-d", str(prefix.with_suffix(".elf"))], text=True)
    if unresolved.strip() or re.search(r"\br(?:1[6-9]|2[0-9]|3[01])\b", disassembly):
        raise SystemExit("ELF failed unresolved-symbol/high-register audit")
    firstboot_audits={}
    if args.first_boot:
        symbols=subprocess.check_output([str(ROOT/"toolchain/bin/csky-elf-nm"),str(prefix.with_suffix(".elf"))],text=True)
        names={line.split()[-1] for line in symbols.splitlines() if line.split()}
        forbidden={"spi_nor_program","spi_nor_erase_sector","write_enable","stub_entry_request",
                   "stub_entry_service","enter_stub","stub_region","cps_fast_cmd","cps_openfw_feed"}
        if args.dev_update:forbidden-={"stub_entry_request","stub_entry_service","enter_stub","stub_region"}
        if names & forbidden:raise SystemExit("First-boot image retained a forbidden write/loader symbol")
        objdump=str(ROOT/"toolchain/bin/csky-elf-objdump")
        tx=subprocess.check_output([objdump,"-dr","-j",".text.tx",str(DEST/b/"boards/dm32/radio_dm32.o")],text=True)
        calls=set(re.findall(r"R_CKCORE_\S+\s+(\w+)",tx))
        expected_calls={"firstboot_deny","board_rf_safe"}
        if args.tx_bench:
            if not {"firstboot_tx_allowed","firstboot_deny","board_rf_safe"}.issubset(calls):
                raise SystemExit("Native bench board TX entry lost its conditional grant guard")
        elif calls!=expected_calls or len(re.findall(r"movi\s+r0, 0",tx))!=2:
            raise SystemExit("First-boot board TX entry differs from the deny/off/false sequence")
        gpio=subprocess.check_output([objdump,"-r",str(DEST/b/"platform/hrc7000/gpio.o")],text=True)
        if "board_firstboot_pin_level" not in gpio:raise SystemExit("Missing last-stage PA GPIO guard")
        firstboot_audits={"forbidden_storage_write_symbols_absent":True,"runtime_loader_enabled":args.dev_update,
                          "forbidden_write_and_loader_symbols_absent":not args.dev_update,
                          "board_tx_only_deny_off_false":not args.tx_bench,"native_tx_scoped_grant":args.tx_bench,"gpio_guard_referenced":True}
    if args.with_local_stock:
        run(["python3", DEST / "tools/codec_auto.py", "--board", "dm32", "--image", prefix.with_suffix(".bin"),
             "--dir", ROOT / "research/originals", "-o", prefix.with_suffix(".codec.bin")], cwd=DEST)
        if not prefix.with_suffix(".codec.bin").exists():
            raise SystemExit("No recognized local stock codec; not claiming preserved DMR voice")
    inputs = [ROOT / "tools/build_radiofoundry.py", ROOT / "tools/codec_cache.py", ROOT / "tools/classic_channels.py", ROOT / "tools/classic_standard.py",
              ROOT / "tools/sources.py", ROOT / "sources.lock.json", ROOT / "LICENSE",
              ROOT / "src/runtime/memory.c", ROOT / "LICENSES/RadioFoundry-MIT.txt"] + [p for folder in
              (ROOT / "src/p25", ROOT / "src/modem", ROOT / "src/codec", ROOT / "integration/radiofoundry",
               ROOT / "third_party/p25cai", ROOT / "third_party/imbe")
              for p in folder.rglob("*") if p.is_file() and "__pycache__" not in p.parts]
    manifest = {"kind": "Open25-DM32 Classic release candidate; hardware qualification required" if args.classic else "RadioFoundry P25 laboratory image",
        "upstream_commit": PIN, "freertos_commit": git(DEST / "third_party/FreeRTOS-Kernel", "rev-parse", "HEAD"),
        "command": [str(x) for x in command], "p25_enabled": not args.disable, "native_interface_enabled":native,
        "firstboot_restrictions_enabled": args.first_boot,
        "codec_cache_reused":reused_codec,
        "audits": {"unresolved_symbols": [], "high_register_instructions": 0, "compiler_warnings": 0,**firstboot_audits},
        "forced_rebuild_identical": args.verify_rebuild,
        "codec_speed_build":args.speed_codec, "bench_enabled":not args.lean, "cpu_profile_enabled":args.cpu_profile, "dev_update_enabled":args.dev_update,
        "native_tx_bench_enabled":args.tx_bench, "classic_enabled":args.classic,
        "firstboot_flags":47 if args.tx_bench else 15 if args.dev_update else 31,
        "codec_slot": "recognized local stock merged separately" if args.with_local_stock else "reserved; no stock AMBE merged",
        "sources": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(inputs)},
        "generated_sources": {str((DEST/key).relative_to(ROOT)): digest for key,digest in sorted(OWNED.items())},
        "outputs": {str(prefix.with_suffix(ext).relative_to(ROOT)): hashlib.sha256(prefix.with_suffix(ext).read_bytes()).hexdigest()
                    for ext in ((".elf", ".bin", ".map", ".codec.bin") if args.with_local_stock else (".elf", ".bin", ".map"))}}
    prefix.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(prefix.with_suffix(".elf"))
    for line in log.read_text().splitlines():
        if "FLASH:" in line or "RAM:" in line: print(line)


if __name__ == "__main__":
    main()
