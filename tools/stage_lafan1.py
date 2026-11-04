#!/usr/bin/env python3
"""Verify and stage the complete, unchanged LAFAN1 set (no automatic download)."""
import argparse
import hashlib
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / 'tools/lafan1.sha256'


def entries(manifest=MANIFEST):
    result = []
    for line in manifest.read_text(encoding='utf-8').splitlines():
        digest, name = line.split('  ', 1)
        if len(digest) != 64 or Path(name).name != name or not name.endswith('.bvh'):
            raise ValueError('Invalid clip manifest entry')
        result.append((name, digest))
    if len(result) != 77 or len({name for name, _ in result}) != 77:
        raise ValueError('The manifest must name exactly 77 unique clips')
    return result


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def stage(source, dest, manifest=MANIFEST):
    source, dest = Path(source), Path(dest)
    if source.resolve() == dest.resolve():
        raise ValueError('Source and destination must be different directories')
    clips = entries(manifest)
    # Validate the entire source before touching the destination.
    for name, expected in clips:
        path = source / name
        if not path.is_file() or digest(path) != expected:
            raise ValueError(f'Missing or modified LAFAN1 clip: {path}; fetch the complete unchanged set')
    dest.mkdir(parents=True, exist_ok=True)
    unexpected = [p.name for p in dest.glob('*.bvh') if p.name not in {n for n, _ in clips}]
    if unexpected:
        raise ValueError(f'Destination has unexpected BVH files: {unexpected}; use a clean staging directory')
    for name, expected in clips:
        target = dest / name
        if not target.is_file() or digest(target) != expected:
            shutil.copyfile(source / name, target)
        if digest(target) != expected:
            raise ValueError(f'Failed to copy {name} intact')
    print(f'Staged {len(clips)} verified LAFAN1 clips to {dest}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--dest', type=Path, required=True)
    args = parser.parse_args()
    try:
        stage(args.source, args.dest)
    except (OSError, ValueError) as exc:
        print(f'LAFAN1 staging failed: {exc}', file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
