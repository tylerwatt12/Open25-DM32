#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual firmware slot validator with the generated DM32 limit."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/radiofoundry-p25'
limit = int(re.search(r'__codec_tab_hi = (0x[0-9A-Fa-f]+);',
                     (BUILD / 'boards/dm32/memory.ld').read_text())[1], 16)
out = ROOT / 'build/codec-slot-test'
subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', '-I' + str(BUILD / 'platform/hrc7000'),
                '-DCODEC_TABLE_END=' + str(limit), str(ROOT / 'tests/codec/test_slot.c'), '-o', str(out)], check=True)
subprocess.run([str(out)], check=True)
