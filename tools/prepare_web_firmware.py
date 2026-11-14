#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package a verified codec-free Classic application for the Firmware Utility."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from zipfile import ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    image, out = args.image.resolve(), args.out.resolve()
    if image.suffix != '.bin' or not image.is_relative_to(ROOT / 'build'):
        parser.error('Choose a raw application .bin from this checkout\'s build directory')
    if out.suffix != '.zip' or not any(out.is_relative_to(ROOT / folder) for folder in ('build', 'artifacts')):
        parser.error('Choose a new .zip under build/ or artifacts/')

    build = json.loads(image.with_suffix('.json').read_text())
    application = image.read_bytes()
    digest = hashlib.sha256(application).hexdigest()
    if not build.get('classic_enabled') or build['outputs'].get(str(image.relative_to(ROOT))) != digest:
        raise ValueError('Expected a Classic application matching its build manifest')
    release, = [arg.removeprefix('RFOUNDRY_VERSION=') for arg in build['command']
                if arg.startswith('RFOUNDRY_VERSION=')]
    if 'BOARD=dm32' not in build['command'] or not re.fullmatch(r'open25-classic-[a-zA-Z0-9._-]{1,65}', release):
        raise ValueError('Expected a DM32 Classic firmware version')
    if not application or len(application) > 0xe8000 - 0xc000:
        raise ValueError('The application must end before the reserved DMR codec slot')
    map_path = image.with_suffix('.map')
    map_bytes = map_path.read_bytes()
    if build['outputs'].get(str(map_path.relative_to(ROOT))) != hashlib.sha256(map_bytes).hexdigest():
        raise ValueError('Linker map differs from the build manifest')
    symbols = {name: int(value, 16) for value, name in
               re.findall(r'(0x[0-9a-fA-F]+)\s+(__codec_\w+)\s*=', map_bytes.decode())}
    expected = {'__codec_slot': 0x030e8000, '__codec_slot_end': 0x03100000,
                '__codec_run_lo': 0x43a00, '__codec_run_hi': 0x58000,
                '__codec_tab_lo': 0x18000e48, '__codec_tab_hi': 0x18003e38}
    if any(symbols.get(name) != value for name, value in expected.items()):
        raise ValueError('Linker layout differs from the reserved codec location')

    manifest = {'format': 'open25-dm32-firmware', 'version': 2, 'target': 'dm32-048',
                'release': release, 'applicationSha256': digest, 'codecTablesEnd': symbols['__codec_tab_hi']}
    out.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(out, 'x') as archive:
        archive.writestr(ZipInfo('firmware.json'), json.dumps(manifest, indent=2) + '\n')
        archive.writestr(ZipInfo('application.bin'), application)
    print(out)


if __name__ == '__main__':
    main()
