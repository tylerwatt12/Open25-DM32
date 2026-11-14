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
The embedded codec uses fixed-point fast paths; no trunking or newer UI source
is needed. Dependencies are pinned in `sources.lock.json` and the build recipe.

For a release candidate, also verify a forced rebuild:

```sh
python3 tools/build_radiofoundry.py --classic --stack-check --verify-rebuild
make test
```

The builder rejects target compiler warnings, checks the ABI and records source,
generated-source and output hashes. `make test` covers protocol/FEC, codec,
audio, native modem ownership, the FM/DMR state machine, P25 channel persistence
and the original UI's P25 editor. Host checks do not establish RF interoperability.

Output is under `build/radiofoundry-p25/build/dm32-p25-classic/`.
The application has no proprietary AMBE codec. To make the same application-only
ZIP offered by the Firmware Utility:

```sh
python3 tools/prepare_web_firmware.py \
  --image build/radiofoundry-p25/build/dm32-p25-classic/rfoundry_dm32.bin \
  --out artifacts/firmware/Open25-DM32-Classic-0.1.0-rc1-r6-application.zip
```

The packager verifies the application and linker-map hashes against the build
manifest, checks the reserved codec layout, and writes only `firmware.json` and
`application.bin`. The manifest records `codecTablesEnd` from the verified
linker map; older ZIPs without that field retain the old table limit. It refuses
to replace an existing ZIP. The runtime version is
`open25-classic-0.1.0-rc1-r6`; `r6` identifies this catalog/package revision.
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

In the channel editor, change Mode from DMR to P25, then set Talkgroup, NAC and
P25 Radio ID. NAC is three hexadecimal digits: Up/Down selects a digit, the knob
changes its value, and number keys enter 0–9. NAC F7E accepts any receive NAC on
an RX-only channel; F7F is unsupported. TX uses a concrete NAC, nonzero talkgroup
and radio ID 1–9999999. Existing FM/DMR records retain their original layout;
P25 adds a tagged use of the existing fixed-size record, without a second store.

Selected-channel P25 is supported; P25 scanning and VFO are not in this candidate.
FM/DMR scanning skips P25 entries. Normal radio protections remain active.

Legacy development flags remain available for regression work. `--first-boot`
blocks ordinary TX/writes; `--tx-bench` additionally requires `--dev-update` and
uses fixed bench settings. Neither is the Classic release preset, and those
flags cannot be combined with `--classic`.
