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
  --out artifacts/firmware/Open25-DM32-Classic-0.1.0-rc1-r11-rfkv-application.zip
```

The packager verifies the application and linker-map hashes against the build
manifest, checks the reserved codec layout, and writes only `firmware.json` and
`application.bin`. The manifest records `codecTablesEnd` from the verified
linker map; older ZIPs without that field retain the old table limit. It refuses
to replace an existing ZIP. The runtime version is
`open25-classic-0.1.0-rc1-r11`; `r11` identifies this catalog/package revision.
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

RX NAC `F7E` accepts any NAC. Inherited TX uses the receive NAC, or `293` when
RX is wildcard `F7E`; a numeric override remains explicit and fixed. TX NAC
`000` is valid; explicit TX `F7E`/`F7F` and RX `F7F` are rejected.
Transmit still requires a nonzero talkgroup and radio ID from 1 to 16777214.

RFKV mode records are the only supported Classic P25 programming format.
Retired A5/D/E channel records are refused without conversion. If such records
are present, normal editing uses the existing foreign-codeplug gate and requires
an explicit **Start new** choice; boot and detection never erase them. The
matching Classic CPS uses the same canonical format and refuses older firmware.


Selected-channel P25 is supported; P25 scanning and VFO are not in this candidate.
FM/DMR scanning skips P25 entries. Normal radio protections remain active.

Legacy development flags remain available for regression work. `--first-boot`
blocks ordinary TX/writes; `--tx-bench` additionally requires `--dev-update` and
uses fixed bench settings. Neither is the Classic release preset, and those
flags cannot be combined with `--classic`.

## RFKV programming release

Classic r9 and the matching Classic CPS use RadioFoundry's RFKV v1 mode records
and the existing journaled continuation mechanism. There is no reader, writer
or automatic conversion for retired Classic A5/D/E P25 programming. Recreate old
programming in the current CPS or use the radio's explicit Start new workflow.
The underlying standard FM/DMR base records remain unchanged.

The firmware reports `codeplug=rfkv1` and `codeplug_chain=1`; the editor checks
both before programming. Update firmware and CPS together. No build, package or
host-test command flashes or programs a physical radio.

New standard P25 channels select Normal (all groups on the RX NAC) or Selective
(the saved talkgroup). Explicit TX NAC `000` and fixed TX equal to RX remain
distinct from inheritance. Conventional unit IDs use the upstream 24-bit range
through `0xFFFFFE`; the selected codec and voice bitstream are unchanged.

## RX-only defaults in r10

The radio editor and matching Classic CPS default new/copied channels outside
144–148 and 420–450 MHz to RX-only. A committed edit that changes actual TX to
an outside frequency restores RX-only, including RX-offset and Talkaround
changes. Existing RFKV records and imported programming retain their saved
choices; the encoder and storage format are unchanged.

Disabling RX-only outside those ranges opens the existing confirmation owner.
Keep RX-only is selected by default; explicit Enable TX changes the draft's
existing flag and normal Save persists it. No acknowledgement flag or per-PTT
prompt is added. The independent P25 transmit-frequency restriction and other
admission checks remain active.

The full UI regression covers new/copy defaults, effective TX edits, confirmation
accept/cancel, in-band behavior, existing-record preservation and display fit.
Run the normal full host suite and target stack/ABI/identical-rebuild checks
above before packaging a release.

## P25 capability advertisement in r11

The shared firmware feature list advertises `P25` only when
`CONFIG_P25_CHANNELS` is enabled. `make classic-test` compiles the real VERSION
handler, formatter and feature list for disabled, lab-only and Classic builds.
It checks the returned capability tokens and RFKV programming markers. The
matching CPS regression parses the same C handler response and checks the form
and table mode choices without relying on an existing P25 channel.

## P25 busy channel lockout

Enable **Busy channel lockout** in a conventional P25 channel to check its
receive frequency before PTT. The radio blocks on RF carrier or decoded
voice, including a carrier the NAC or talkgroup filters would mute. A denied
press shows **Channel busy** and plays the existing TX denial tone at the
configured beep level. Release PTT and press again when the channel clears.
The option also blocks a press while the receiver is starting.
Existing channels start with the channel option off. This is a local
carrier-sense option. Repeater status-symbol access is described in
TIA-102.BAAD-B section 4.1.
