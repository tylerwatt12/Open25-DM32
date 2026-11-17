# Third-party source notices

Preserve every original per-file notice and the supplied full license texts.
Build dependency source remains available at its pinned upstream location.

The baseline project, P25 CAI and full-rate IMBE codec retain their MIT terms.
The opt-in encryption preset includes original GPL-3.0-or-later prototype
components and ports thereof. Distribute that combined firmware under
GPL-3.0-or-later and preserve the original MIT notices. The full GPL text
is in [LICENSES/GPL-3.0-or-later.txt](LICENSES/GPL-3.0-or-later.txt).
The former GPL IMBE implementation, wrapper and DSP patch recipe are replaced.
Dependencies retain the terms listed below. Independent GPL/LGPL test references
are not linked into the application firmware.

| Component | Source / pin | Terms and use |
| --- | --- | --- |
| RadioFoundry | https://github.com/RadioFoundry/RadioFoundry — `dfbaf20c400c73e2ccf6edb80af7d1d255bee418` | MIT originals; full notice in `LICENSES/RadioFoundry-MIT.txt` covers source patch context, with upstream notices also retained in the generated derivative. |
| FreeRTOS-Kernel | RadioFoundry submodule — `3a22924e0a9ddbbc8b0758881c33b3422a5cc20d` | MIT, original notices retained. |
| Full-rate IMBE | Shared Open25 fixed-point C core in `third_party/imbe`; exact revision/files in `sources.lock.json` | MIT for the independently expressed code. Numeric standard datasets and generated constants are identified in its source provenance. |
| mbelib | `sources.lock.json` | ISC; notice in `LICENSES/mbelib-ISC.txt`. Independent host reference, not the embedded codec. |
| P25 CAI | `third_party/p25cai`, revision in `sources.lock.json` | MIT; full notice in `third_party/p25cai/LICENSE`. Conventional caller bindings in `src/p25/p25.c` and `p25.h` use the same pinned source. |
| OP25 | `sources.lock.json` | GPL/LGPL host regression references; original notices retained with the downloaded dependency. |
| newlib | `tools/bootstrap_newlib.sh` | Pinned source hash; original per-file terms and upstream COPYING.NEWLIB apply. |
| GNU binutils, GCC and libgcc | `sources.lock.json`, `tools/bootstrap_toolchain.sh` | Original GPL/LGPL terms and applicable GCC Runtime Library Exception apply. |

Proprietary AMBE material is supplied by the user locally. No stock firmware,
bootloader/helper image, radio dump, calibration or key material is distributed
in this source tree.

The DM32 codec-table SRAM limit is backported from RadioFoundry commit
`88283f6ce3aab4c5ff5c9995600de2acfa2c6f7b` (MIT), extending the exclusive
end from `0x18003E28` to `0x18003E38` for NRFB .050. The generated board
registry and package metadata derive this value from the linker symbol.
