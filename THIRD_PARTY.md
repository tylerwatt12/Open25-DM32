# Third-party source notices

Preserve every original per-file notice and the supplied full license texts.
Build dependency source remains available at its pinned upstream location.

| Component | Source / pin | Terms and use |
| --- | --- | --- |
| RadioFoundry | https://github.com/RadioFoundry/RadioFoundry — `dfbaf20c400c73e2ccf6edb80af7d1d255bee418` | MIT originals, with notices retained in the generated derivative. |
| FreeRTOS-Kernel | RadioFoundry submodule — `3a22924e0a9ddbbc8b0758881c33b3422a5cc20d` | MIT, original notices retained. |
| nostar/imbe_vocoder | `sources.lock.json` | GPL-3.0-or-later per-file notices; full GPL text included locally. |
| imostlylurk/imbe_embedded | `b49c753b9d6a8b1221138fc29ac3e10fd49f7e8a` | GPL-3.0-or-later; selected fixed-point fast paths adapted in `src/codec/imbe-speed.patch`. |
| mbelib | `sources.lock.json` | ISC; notice in `LICENSES/mbelib-ISC.txt`. Independent host reference, not the embedded codec. |
| OP25 | `sources.lock.json` | p25craft GPL-2.0-or-later contributions used under GPL 3; separate GPL/LGPL helper headers serve host regression tests. |
| newlib | `tools/bootstrap_newlib.sh` | Pinned source hash; original per-file terms and upstream COPYING.NEWLIB apply. |
| GNU binutils, GCC and libgcc | `sources.lock.json`, `tools/bootstrap_toolchain.sh` | Original GPL/LGPL terms and applicable GCC Runtime Library Exception apply. |

Proprietary AMBE material is supplied by the user locally. No stock firmware,
bootloader/helper image, radio dump, calibration or key material is distributed
in this source tree.
