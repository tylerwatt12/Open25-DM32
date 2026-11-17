#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Load and unlock the DM32's persistent P25 vault over its local cable."""
import argparse
from getpass import getpass, GetPassWarning
import re
import secrets
import struct
import sys
import warnings

from pathlib import Path

ALGORITHMS = {"aes256": (0x84, 32), "des-ofb": (0x81, 8), "adp": (0xAA, 5)}
RADIOFOUNDRY_TOOLS = Path(__file__).resolve().parents[1] / "research/upstream/radiofoundry/tools"

P25_LAB = 0x20
INFO, PUT, REMOVE, LOCK, START, FINISH = 17, 20, 21, 22, 23, 24
NEW, ENTER, READY, NO_SPACE = 1, 2, 4, 7
SLOTS = 8


def password(label):
    # Abort if the terminal cannot hide the traffic key.
    with warnings.catch_warnings():
        warnings.simplefilter("error", GetPassWarning)
        return getpass(label)


def random_mi():
    while True:
        seed = secrets.token_bytes(8)
        if any(seed):
            return seed


def key_arg(value):
    kid = int(value, 0)
    if not 0 <= kid <= 0xffff:
        raise argparse.ArgumentTypeError("KID must be between 0 and 65535")
    return kid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    sub = parser.add_subparsers(dest="action", required=True)
    for command in ("start", "finish", "lock", "list"):
        sub.add_parser(command)
    put = sub.add_parser("load")
    put.add_argument("slot", type=int, choices=range(1, SLOTS + 1))
    put.add_argument("--algorithm", required=True, choices=ALGORITHMS)
    put.add_argument("--key-id", required=True, type=key_arg)
    remove = sub.add_parser("remove")
    remove.add_argument("slot", type=int, choices=range(1, SLOTS + 1))
    args = parser.parse_args()
    if ":" in args.port:
        parser.error("Specify a local serial port such as COM7")
    sys.path.insert(0, str(RADIOFOUNDRY_TOOLS))
    from rfoundry_debug import DebugLink, DebugLinkError

    try:
        with DebugLink(args.port) as radio:
            _, info = radio.request(P25_LAB, bytes([INFO]))
            if len(info) != 19 + SLOTS * 3:
                raise ValueError("Handset PIN firmware required")
            exists, unlocked = info[:2]
            status = info[-1]
            if args.action == "list":
                print("Vault unlocked" if unlocked else "Vault locked" if exists else "Vault not created")
                if status == NO_SPACE:
                    print("No erased vault block")
                elif status == READY:
                    print("Cable loading window open")
                elif status in (NEW, ENTER):
                    print("Waiting for PIN on radio")
                descriptions = {algid: name for name, (algid, _) in ALGORITHMS.items()}
                if unlocked:
                    for i in range(SLOTS):
                        algid, kid = struct.unpack_from("<BH", info, 18 + 3 * i)
                        if algid in descriptions:
                            print(f"Slot {i + 1}: {descriptions[algid]}, KID 0x{kid:04X}")
                return
            if args.action == "start":
                payload = bytes([START]) + secrets.token_bytes(16) + random_mi()
            elif args.action == "finish":
                payload = bytes([FINISH])
            elif args.action == "lock":
                payload = bytes([LOCK])
            elif status != READY:
                raise ValueError("Run start and enter the PIN on the radio first")
            elif args.action == "remove":
                payload = bytes([REMOVE, args.slot])
            else:
                algorithm, key_bytes = ALGORITHMS[args.algorithm]
                key = password(f"Traffic key ({2 * key_bytes} hex digits): ").strip()
                if not re.fullmatch(f"[0-9a-fA-F]{{{2 * key_bytes}}}", key):
                    raise ValueError("Traffic key length or characters are invalid")
                payload = struct.pack("<BBBH", PUT, args.slot, algorithm, args.key_id) + bytes.fromhex(key)
            radio.request(P25_LAB, payload)
        print({"start": "On the radio, open Menu > Tools > P25 Keyloader and enter the PIN",
               "finish": "Cable loading window closed",
               "lock": "Vault locked", "remove": "Key removed", "load": "Key stored"}[args.action])
    except (DebugLinkError, GetPassWarning, ValueError) as exc:
        parser.exit(1, f"{exc}\n")


if __name__ == "__main__":
    main()
