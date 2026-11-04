#!/usr/bin/env python3
"""Verify that an APK/portable ZIP contains exactly the pinned, unmodified LAFAN1 BVHs."""
import argparse
import hashlib
import sys
import zipfile
from stage_lafan1 import entries


def verify(archive, prefix):
    prefix = prefix.strip('/') + '/'
    expected = dict(entries())
    with zipfile.ZipFile(archive) as z:
        names = {name.replace('\\', '/'): name for name in z.namelist()}
        if len(names) != len(z.namelist()):
            raise ValueError('Duplicate archive entries')
        found = {name[len(prefix):] for name in names if name.startswith(prefix) and name.endswith('.bvh')}
        if found != set(expected):
            raise ValueError(f'Clip list mismatch: missing {sorted(set(expected)-found)}, extra {sorted(found-set(expected))}')
        for name, wanted in expected.items():
            h = hashlib.sha256()
            with z.open(names[prefix + name]) as f:
                for block in iter(lambda: f.read(1024 * 1024), b''):
                    h.update(block)
            if h.hexdigest() != wanted:
                raise ValueError(f'Clip is modified: {name}')
    print(f'Verified all {len(expected)} LAFAN1 clips in {archive}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('archive')
    p.add_argument('--prefix', default='assets/motion')
    args = p.parse_args()
    try:
        verify(args.archive, args.prefix)
    except (OSError, ValueError, zipfile.BadZipFile) as e:
        print(f'Archive verification failed: {e}', file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__': sys.exit(main())
