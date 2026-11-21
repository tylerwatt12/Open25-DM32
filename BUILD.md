<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Open25-DM32 Contributors -->

# Building Classic r14

The [project README](README.md) describes the editions, installation, programming
and current release status. This file covers source preparation and building.

Classic is a small, explicit source overlay on the public RadioFoundry revision
pinned in [sources.lock.json](sources.lock.json). The preparation helper fetches
that exact upstream tree and FreeRTOS revision, verifies their identities, and
applies the reviewed Classic product preset, band policy, retained repairs and
tests. Source preparation does not copy private Next implementation into the
public tree. Generated source, dependencies, binaries, logs and test images stay
under ignored `build/`; build and test commands do not connect to a radio.

Use Python 3.11+, Git, GNU Make, a host C compiler and an existing CK803S C-SKY
toolchain. Preparing sources requires access to the two public Git repositories
pinned in `sources.lock.json`.

```sh
$(tools/rfoundry_python.sh) tools/prepare_upstream.py
$(tools/rfoundry_python.sh) tools/build_release.py --cross /path/to/csky-elf- --stack-check --verify-rebuild
```

The prepared source is `build/upstream`. The application, deterministic ZIP,
build receipt and logs are under `build/upstream/build/classic-test`. The wrapper
uses the Classic profile from the reconstructed source and pins version,
`RFOUNDRY_GIT_TIME`, `SOURCE_DATE_EPOCH` and `RELEASE=` independently of local Git
dates or tags. The recorded epoch is 1795197378. It refuses compiler warnings,
checks the application size/hash, optionally checks stacks and an identical
forced rebuild, then checks the ZIP against the reviewed package hash.

Use `--print-command` to see the exact Make command before compiling:

```sh
$(tools/rfoundry_python.sh) tools/build_release.py --cross /path/to/csky-elf- --print-command
```

This prepares or verifies the source tree, prints the command as JSON and exits.

The exact release compiler is GCC 14.3.0 with binutils 2.45.1 and newlib
4.5.0.20241231. The linked archive fingerprints are recorded in
`sources.lock.json`. The tested newlib used CK803S, little endian, soft float,
no high registers, `-Os`, nano malloc, small reentrancy and no supplied syscalls.
Different toolchains or library builds can change the resulting bytes. The
upstream toolchain bootstrap uses other versions and is not an exact recipe for
this release. Toolchain executables and libraries are not bundled here.

Source preparation checks every overlay hash before copying. Repeating it
checks the prepared upstream commit, source changes, overlay and FreeRTOS. A
changed manifest or unexpected source edit causes refusal; preserve that working
directory and prepare a separate one with `--out` rather than deleting work.
For an already available source cache, `--upstream-local /path/to/git-source` and
`--freertos-local /path/to/git-source` fetch the same pinned commits from those
repositories. They do not change the required identities.

Run the firmware's host regressions and Classic policy/UI/package checks:

```sh
make -C build/upstream/tests/host
$(tools/rfoundry_python.sh) build/upstream/tools/test_classic_bands.py
$(tools/rfoundry_python.sh) build/upstream/tools/test_classic_product.py
```

Use Clang for the Classic sanitizer checks. The complete upstream host suite
retains its documented optional tool requirements. A separate sanitized host run
requires a clean host-test output because its binaries do not track sanitizer
flags; preserve existing test evidence before changing that output. These tests
use software fixtures and do not qualify RF interoperability or speech quality.

The source overlay is deliberately separate from installation. Loading the
application alone does not convert r13 programming; use migration-aware Flasher
v3 for that operation. The application ZIP contains the original license and
author notices, platform/MBE attribution, compiled font notices, the FreeRTOS
license and the linked newlib notice. It includes no proprietary codec. Do not
publish generated codec-bearing images, full-radio backups or recordings.
