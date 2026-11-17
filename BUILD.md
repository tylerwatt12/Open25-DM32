# Building Classic

Use Python 3.10+, a host C/C++ compiler, Git, Make and the C-SKY toolchain.
Keep active builds on internal storage.

```sh
make sources
make toolchain
sh tools/bootstrap_newlib.sh
make radiofoundry
```

`make radiofoundry` selects Classic: original RadioFoundry UI, FM, DMR and
saved conventional P25 channels. It enables normal PTT, storage, channel editing
and CPS. It omits the first-boot restrictions and bench replay controls.
The embedded full-rate IMBE codec uses fixed-point C and caller-owned storage;
no Rust toolchain, trunking or newer UI source is needed. Dependencies and codec
snapshots are pinned in `sources.lock.json` and the build recipe.

For a release candidate, also verify a forced rebuild:

```sh
python3 tools/build_radiofoundry.py --classic --stack-check --verify-rebuild
make test
```

The builder rejects target compiler warnings, checks the ABI and records source,
generated-source and output hashes. `make test` covers the standalone CAI
library with sanitizers, conventional protocol/FEC, codec,
audio, native modem ownership, the FM/DMR state machine, P25 channel persistence
and the original UI's P25 editor. Host checks do not establish RF interoperability.

Phase 1 wire coding and framing use the pinned library in
`third_party/p25cai`; the existing conventional `p25.h` interface is retained.
`sources.lock.json` owns the snapshot revision, exact file hashes and the two
caller-binding copies in `src/p25`. Builds and host tests reject a changed
snapshot or binding. To refresh, select a reviewed library revision, replace
the snapshot and both bindings together, update its lock record, and run
`python3 tools/sources.py verify-vendored` followed by the full tests and
target checks. Keep library changes with its source owner and refresh these
copies from that revision. The codec and application policy keep their
existing interfaces and notices.

The same snapshot rule applies to `third_party/imbe`: its source owner supplies
the copied `src/codec/dm32_imbe.h` and `src/runtime/memory.c`. The lock verifies
all three together. Refresh Classic and Next from the same reviewed library
revision. A context/layout change requires complete application RAM/stack checks.
The target memory implementation is compiled with the measured `-O2` per-file
preset; other application files retain their existing build optimization.

Output is under `build/radiofoundry-p25/build/dm32-p25-classic/`.
The application has no proprietary AMBE codec. To make the same application-only
ZIP offered by Flasher v3:

```sh
python3 tools/prepare_web_firmware.py \
  --image build/radiofoundry-p25/build/dm32-p25-classic/rfoundry_dm32.bin \
  --out artifacts/firmware/Open25-DM32-Classic-0.1.0-rc1-r8-mit-application.zip
```

The packager verifies the application and linker-map hashes against the build
manifest, checks the reserved codec layout, and writes only `firmware.json` and
`application.bin`. The manifest records `codecTablesEnd` from the verified
linker map; older ZIPs without that field retain the old table limit. It refuses
to replace an existing ZIP. The runtime version is
`open25-classic-0.1.0-rc1-r8`; `r8` identifies this catalog/package revision.
Firmware binaries and ZIPs stay in ignored output directories and are never
committed to GitHub. No build or packaging command opens or installs a radio.

The installer preserves a valid compatible DMR codec in its reserved location.
For first installation from RadioFoundry-recognized DM32 OEM codecs (.032
through NRFB .050, including .046 HR and NRF/fanti .049), it
prepares that radio's own codec in the reserved slot after validating its code
and tables independently. It preserves the
bootloader and data chip. Full backup/restore covers both complete chips,
including the codec area.
Local `--with-local-stock` unit images contain proprietary material and must stay
private; they are not inputs to this public packager.

In the channel editor, choose P25 mode and set Talkgroup, RX NAC, TX NAC and
P25 Radio ID. NAC values have three hexadecimal digits. TX NAC initially shows
**Same as RX**; confirming it unchanged keeps that inheritance. Typing a number
creates an explicit TX NAC, even if it equals RX. In the TX editor, Down from
the first digit selects **Same as RX** again. A numeric override stays fixed
when RX NAC changes. Up/Down moves between digits and the knob changes a digit.

RX NAC `F7E` accepts any NAC; transmitting from that channel requires a concrete
TX override. TX NAC `000` is valid; TX `F7E`/`F7F` and RX `F7F` are rejected.
Transmit still requires a nonzero talkgroup and radio ID from 1 to 9999999.

Legacy single-NAC records retain their encoding. Explicit TX NAC uses the public
Classic r8 format: high nibble `E` at byte `0x2C`, with TX NAC little-endian at
`0x22`. Inherited TX uses tag `D`. Older Classic firmware skips tag `E` records;
use a split-NAC-aware build to read them. No second channel store is introduced.

Selected-channel P25 is supported; P25 scanning and VFO are not in this candidate.
FM/DMR scanning skips P25 entries. Normal radio protections remain active.

Legacy development flags remain available for regression work. `--first-boot`
blocks ordinary TX/writes; `--tx-bench` additionally requires `--dev-update` and
uses fixed bench settings. Neither is the Classic release preset, and those
flags cannot be combined with `--classic`.


## Experimental quad-band Classic firmware

Build with the standard Classic prerequisites above and enable the extra
tuning ranges explicitly.

```sh
python3 tools/build_radiofoundry.py --classic --quad-band --stack-check
```

FM and conventional P25 accept these inclusive frequencies:

| Band | Tuning range (MHz) |
| --- | --- |
| VHF | 136�174 |
| UHF | 380�520 |
| 700 | 764�776, 794�805 |
| 800 | 806�825, 851�870 |

The firmware accepts the contiguous interval from 794 through 825 MHz.
Extended-band transmit uses the lowest calibrated UHF drive setting. The
normal Classic build retains its existing limits. DMR transmit retains the
stock DM-32UV VHF and UHF limits in the quad-band build as well.

Use saved FM or conventional P25 channels with the intended RX and TX
frequencies. Establish a low-power baseline through a dummy load or
conducted test setup, then measure carrier frequency, deviation or digital
modulation, output power, and spectral purity at each band edge before
treating the extended bands as usable. The extended limits come from the
APX 8000 frequency plan. They describe firmware tuning, with DM-32UV RF
performance still to be qualified. [Motorola APX 8000 data sheet](https://www.motorolasolutions.com/content/dam/msi/docs/en-xl/mot_apx_8000_datasheet_en.pdf)
