#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Open25-DM32 Contributors
"""Prepare the pinned public upstream and apply the reviewed Classic overlay.

Defaults to build/upstream and preserves existing directories on refusal.
Never opens a radio or publishes anything.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "sources.lock.json"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(directory, *args):
    return subprocess.check_output(["git", "-C", str(directory), *args], text=True).rstrip("\n")


def read_lock():
    raw = LOCK_PATH.read_bytes()
    lock = json.loads(raw)
    if lock.get("format") != "open25-classic-source-overlay" or lock.get("version") != 1:
        raise ValueError("Unsupported source manifest")
    expected = set(lock["overlay"])
    actual = {p.relative_to(ROOT / "overlay").as_posix() for p in (ROOT / "overlay").rglob("*") if p.is_file()}
    if actual != expected:
        raise ValueError("Overlay files differ from the reviewed manifest")
    for name, record in lock["overlay"].items():
        path = ROOT / "overlay" / name
        if path.is_symlink() or path.resolve().is_relative_to((ROOT / "overlay").resolve()) is False:
            raise ValueError("Unsafe overlay path: " + name)
        if record["mode"] not in ("100644", "100755") or digest(path.read_bytes()) != record["sha256"]:
            raise ValueError("Changed overlay file: " + name)
    return lock, digest(raw)


def checkout(destination, pin, source):
    subprocess.run(["git", "init", "-q", str(destination)], check=True)
    git(destination, "config", "core.autocrlf", "false")
    if sys.platform == "win32":
        git(destination, "config", "core.longpaths", "true")
    subprocess.run(["git", "-C", str(destination), "fetch", "--quiet", "--no-tags", "--depth=1",
                    source or pin["url"], pin["commit"]], check=True)
    subprocess.run(["git", "-C", str(destination), "checkout", "--quiet", "--detach", "FETCH_HEAD"], check=True)
    if git(destination, "rev-parse", "HEAD") != pin["commit"] or git(destination, "rev-parse", "HEAD^{tree}") != pin["tree"]:
        raise ValueError("Fetched source differs from its commit/tree pin")


def verify(prepared, lock, lock_hash):
    marker = prepared / ".open25-source.json"
    if not marker.is_file() or json.loads(marker.read_text()).get("manifest_sha256") != lock_hash:
        raise ValueError("Prepared source belongs to a different manifest; preserve it and select a fresh --out")
    if git(prepared, "rev-parse", "HEAD") != lock["upstream"]["commit"]:
        raise ValueError("Prepared upstream commit changed")
    allowed = set(lock["overlay"])
    for row in git(prepared, "status", "--porcelain", "--untracked-files=all").splitlines():
        # Paths in this reviewed snapshot contain no whitespace or rename pairs.
        name = row[3:]
        if name != ".open25-source.json" and name not in allowed:
            raise ValueError("Unexpected prepared source change: " + name)
    for name, record in lock["overlay"].items():
        path = prepared / name
        if path.is_symlink() or digest(path.read_bytes()) != record["sha256"]:
            raise ValueError("Prepared overlay changed: " + name)
    freertos = prepared / "third_party/FreeRTOS-Kernel"
    if git(freertos, "rev-parse", "HEAD") != lock["freertos"]["commit"] or git(freertos, "status", "--porcelain"):
        raise ValueError("Prepared FreeRTOS source changed")
    return prepared


def prepare(out=None, upstream_local=None, freertos_local=None):
    lock, lock_hash = read_lock()
    out = Path(out).resolve() if out else ROOT / "build/upstream"
    if out.exists():
        return verify(out, lock, lock_hash)
    out.parent.mkdir(parents=True, exist_ok=True)
    # A failed download remains separate from a previously prepared source.
    temporary = Path(tempfile.mkdtemp(prefix=".classic-source-", dir=out.parent))
    try:
        checkout(temporary, lock["upstream"], upstream_local)
        freertos = temporary / "third_party/FreeRTOS-Kernel"
        checkout(freertos, lock["freertos"], freertos_local)
        for name, record in lock["overlay"].items():
            target = temporary / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.is_symlink():
                raise ValueError("Refusing to replace an upstream symlink")
            shutil.copyfile(ROOT / "overlay" / name, target)
            target.chmod(int(record["mode"], 8) & 0o777)
        (temporary / ".open25-source.json").write_text(json.dumps({"manifest_sha256": lock_hash}, indent=2) + "\n")
        verify(temporary, lock, lock_hash)
        temporary.rename(out)
    except Exception:
        # Preserve failed preparation for inspection; never remove an existing checkout.
        raise
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--upstream-local", help="Optional existing Git source with the same pinned upstream commit")
    parser.add_argument("--freertos-local", help="Optional existing Git source with the same pinned FreeRTOS commit")
    args = parser.parse_args()
    print(prepare(args.out, args.upstream_local, args.freertos_local))


if __name__ == "__main__":
    main()
