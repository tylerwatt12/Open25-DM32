#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch or verify the pinned Classic build and regression dependencies."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('fetch', 'verify'))
    args = parser.parse_args()
    fetch = args.command == 'fetch'
    lock = json.loads((ROOT / 'sources.lock.json').read_text())
    for record in lock['repositories']:
        path = ROOT / record['path']
        if not path.exists() and fetch:
            path.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(['git', 'init', str(path)], check=True, stdout=subprocess.DEVNULL)
            subprocess.run(['git', '-C', str(path), 'remote', 'add', 'origin', record['url']], check=True)
            subprocess.run(['git', '-C', str(path), 'fetch', '--depth=1', 'origin', record['commit']], check=True)
            subprocess.run(['git', '-C', str(path), 'checkout', '--detach', record['commit']], check=True)
        if git(path, 'rev-parse', 'HEAD').decode().strip() != record['commit']:
            raise ValueError('Dependency commit mismatch: ' + record['path'])
        if git(path, 'status', '--porcelain', '--untracked-files=no'):
            raise ValueError('Preserving changed dependency: ' + record['path'])
        if hashlib.sha256(git(path, 'archive', '--format=tar', record['commit'])).hexdigest() != record['git_archive_sha256']:
            raise ValueError('Dependency source hash mismatch: ' + record['path'])
        print('verified', record['path'])
    for record in lock['files']:
        path = ROOT / record['path']
        if not path.exists() and fetch:
            path.parent.mkdir(parents=True, exist_ok=True)
            partial = path.with_suffix(path.suffix + '.partial')
            with urllib.request.urlopen(record['url']) as source, partial.open('wb') as out:
                while block := source.read(1024 * 1024):
                    out.write(block)
            if partial.stat().st_size != record['bytes'] or digest(partial) != record['sha256']:
                raise ValueError('Download hash mismatch; partial preserved: ' + record['path'])
            partial.rename(path)
        if path.stat().st_size != record['bytes'] or digest(path) != record['sha256']:
            raise ValueError('Dependency archive mismatch: ' + record['path'])
        print('verified', record['path'])


if __name__ == '__main__':
    main()
