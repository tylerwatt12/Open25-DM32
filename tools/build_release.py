#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Open25-DM32 Contributors
"""Build the exact Classic release from pinned upstream and the source overlay.

Requires an existing C-SKY toolchain. Never installs or publishes firmware.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import os
import subprocess
import sys
import tempfile

from prepare_upstream import ROOT, digest, prepare, read_lock

if sys.version_info < (3, 11):
    raise SystemExit("Python 3.11+ is required; use $(tools/rfoundry_python.sh) tools/build_release.py")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cross", default="csky-elf-", help="Existing C-SKY compiler prefix")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--stack-check", action="store_true")
    parser.add_argument("--verify-rebuild", action="store_true")
    parser.add_argument("--print-command", action="store_true")
    parser.add_argument("--source", type=Path, help="Prepared source directory (default build/upstream)")
    args = parser.parse_args()
    lock, lock_hash = read_lock()
    source = prepare(args.source)
    release = lock["release"]
    command = json.loads(subprocess.check_output([sys.executable, str(source / "tools/build_classic.py"),
        "--cross", args.cross, "--jobs", str(args.jobs), "--print-command"], cwd=source, text=True))
    # Release metadata is independent of source checkout dates, tags and paths.
    command = [arg if not arg.startswith("RFOUNDRY_GIT_TIME=") else
               "RFOUNDRY_GIT_TIME=" + str(release["epoch"]) for arg in command]
    if "RFOUNDRY_VERSION=" + release["version"] not in command or "BOARD=" + release["board"] not in command:
        raise ValueError("Build profile differs from the release manifest")
    command += ["RELEASE="]
    if args.print_command:
        print(json.dumps(command, indent=2))
        return
    if not release.get("application_sha256") or not release.get("package_sha256") or not release.get("application_bytes"):
        raise ValueError("Release artifact pins are incomplete; finish review before building the release")
    version = subprocess.check_output([args.cross + "gcc", "-dumpfullversion"], text=True).strip()
    if version != lock["toolchain"]["gcc"]:
        raise ValueError("Exact release reproduction requires GCC " + lock["toolchain"]["gcc"])
    env = {**os.environ, "SOURCE_DATE_EPOCH": str(release["epoch"])}
    out = source / "build/classic-test"
    logs = out / "logs"
    logs.mkdir(parents=True, exist_ok=True)

    def run(extra, log):
        with (logs / log).open("w") as stream:
            subprocess.run([*command, *extra], cwd=source, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
        if "warning:" in (logs / log).read_text():
            raise ValueError("Compiler warning; inspect " + str(logs / log))

    run([], "release-build.log")
    application = out / ("rfoundry_" + release["board"] + ".bin")
    original = application.read_bytes()
    if len(original) != release["application_bytes"] or digest(original) != release["application_sha256"]:
        raise ValueError("Application differs from the reviewed release; inspect compiler/library versions and build logs")
    if args.stack_check:
        run(["stack-check"], "release-stack.log")
    if args.verify_rebuild:
        run(["-B"], "release-rebuild.log")
        if application.read_bytes() != original:
            raise ValueError("Forced rebuild changed the application")
    sys.path.insert(0, str(source / "tools"))
    spec = importlib.util.spec_from_file_location("classic_release_package", source / "tools/build_classic.py")
    package_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package_module)
    package_path = out / (release["version"] + "-application.zip")
    with tempfile.TemporaryDirectory(prefix=".classic-package-", dir=out) as staging:
        candidate = Path(staging) / package_path.name
        package = package_module.package_application(application, candidate,
            version=release["version"], newlib_notice=ROOT / "notices/newlib-COPYING.NEWLIB.txt")
        if package["sha256"] != release["package_sha256"]:
            raise ValueError("Application matched but package differs from the reviewed release")
        candidate.replace(package_path)
    package["path"] = str(package_path)
    receipt = {"release": release["version"], "source_manifest_sha256": lock_hash,
        "upstream_commit": lock["upstream"]["commit"], "freertos_commit": lock["freertos"]["commit"],
        "epoch": release["epoch"], "command": command, "application_bytes": len(original),
        "application_sha256": digest(original), "stack_check": args.stack_check,
        "identical_forced_rebuild": args.verify_rebuild, "package": package}
    (out / "release-build-manifest.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
