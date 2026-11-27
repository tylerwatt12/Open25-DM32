#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 Open25-DM32 Contributors
"""Check Classic's actual band table and board RX/TX admission without hardware."""
from pathlib import Path
import argparse
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/classic-bands-test"


def function(source, name):
    match = re.search(r"^static [^\n]*\b" + name + r"\([^;\n]*\)\n\{.*?^\}", source, re.M | re.S)
    if not match:
        raise ValueError("Missing production function: " + name)
    return match.group()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-source", type=Path, default=ROOT / "boards/dm32/board.c")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    descriptor = args.board_source.read_text()
    table = re.search(r"^static const band_t bands\[\] = \{.*?^\};", descriptor, re.M | re.S).group()
    extension = re.search(r"#if CONFIG_RX_EXT\n\s*\.rx_ext_count.*?#endif", descriptor, re.S).group()
    radio = (ROOT / "boards/dm32/radio_dm32.c").read_text()
    menus = (ROOT / "core/ui/ui_menus.c").read_text()
    constants = "\n".join(re.findall(r"^#define (?:UHF_FROM_HZ|FD_REG_TX_AUX|FD_TX_AUX_\w+|FD_REG_CSS_[AB]|FD_REG40_BASE)\s+[^\n]+", radio, re.M))
    (OUT / "classic_band_table.inc").write_text(
        "#if CONFIG_RX_EXT\nstatic bool rx_ext_on;\n#endif\n" + table +
        "\nconst board_t board = {.bands=bands,.band_count=sizeof bands/sizeof bands[0],\n" +
        extension + "\n.power_count=5};\n" + constants + "\n")
    (OUT / "classic_band_functions.inc").write_text("\n\n".join(
        function(radio, name) for name in ("fm_bw_reg", "css_words", "rx_tune", "rx", "tx")) + "\n" +
        "\n\n".join(function(menus, name) for name in ("ced_is_p25", "ced_is_nxdn", "ced_rx_band", "ced_problem")))
    features = {key: 0 for key in re.findall(r"^#ifndef (CONFIG_\w+)", (ROOT / "core/config.h").read_text(), re.M)}
    features.update(CONFIG_FM=1, CONFIG_DMR=1, CONFIG_P25=1, CONFIG_NXDN=1, CONFIG_AIR=1,
                    CONFIG_CHANNEL_LIST=1, CONFIG_CHANNEL_EDIT=1, CONFIG_PRODUCT_CLASSIC=1)
    results = []
    for extension in (0, 1):
        binary = OUT / ("bands-" + str(extension))
        command = ["clang", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                   "-Wshadow", "-Wno-unused-parameter", "-fsanitize=address,undefined",
                   "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
                   *["-D" + key + "=" + str(value) for key, value in sorted({**features, "CONFIG_RX_EXT": extension}.items())],
                   "-I" + str(ROOT / "core"), "-I" + str(ROOT / "hal"),
                   "-I" + str(ROOT / "drivers"), "-I" + str(OUT),
                   str(ROOT / "tests/host/classicBandsTest.c"), "-o", str(binary)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([binary], cwd=ROOT, check=True)
        results.append({"rx_ext": extension, "passed": True, "command": command})
    for classic in (0, 1):
        binary = OUT / ("chip-registers-" + str(classic))
        fixture = "classicChipRegTest.c" if classic else "debuglinkChipRegTest.c"
        flags = {**features, "CONFIG_PRODUCT_CLASSIC": classic, "CONFIG_DEBUGLINK": 1, "CONFIG_HOSTLINK": 1}
        command = ["clang", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                   "-Wshadow", "-Wno-unused-parameter", "-fsanitize=address,undefined",
                   "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
                   *["-D" + key + "=" + str(value) for key, value in sorted(flags.items())],
                   "-I" + str(ROOT / "core"), "-I" + str(ROOT / "hal"),
                   "-I" + str(ROOT / "tests/host/watchdog_stub"),
                   str(ROOT / "tests/host" / fixture),
                   str(ROOT / "core/debuglink/debuglink_chipreg.c"), "-o", str(binary)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([binary], cwd=ROOT, check=True)
        results.append({"product_classic": classic, "chip_registers_passed": True, "command": command})
    (OUT / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
