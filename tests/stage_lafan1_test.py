#!/usr/bin/env python3
import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('stage_lafan1', ROOT / 'tools/stage_lafan1.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class StageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        lines = []
        for i in range(77):
            name = f'clip{i:02}.bvh'
            data = f'HIERARCHY fake {i}'.encode()
            (self.source / name).write_bytes(data)
            lines.append(f'{hashlib.sha256(data).hexdigest()}  {name}\n')
        self.manifest = self.root / 'manifest'
        self.manifest.write_text(''.join(lines))
        self.dest = self.root / 'dest'

    def stage(self):
        module.stage(self.source, self.dest, self.manifest)

    def test_all_verified_and_unchanged(self):
        self.stage()
        self.assertEqual(len(list(self.dest.glob('*.bvh'))), 77)
        for f in self.source.glob('*.bvh'):
            self.assertEqual(f.read_bytes(), (self.dest / f.name).read_bytes())
        self.stage()  # idempotent

    def test_missing_or_modified_does_not_touch_existing_output(self):
        self.stage()
        before = (self.dest / 'clip00.bvh').read_bytes()
        (self.source / 'clip01.bvh').write_bytes(b'bad')
        with self.assertRaisesRegex(ValueError, 'Missing or modified'): self.stage()
        self.assertEqual((self.dest / 'clip00.bvh').read_bytes(), before)
        (self.source / 'clip01.bvh').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing or modified'): self.stage()

    def test_rejects_unexpected_output_and_aliasing(self):
        self.dest.mkdir()
        (self.dest / 'other.bvh').write_text('preserve me')
        with self.assertRaisesRegex(ValueError, 'unexpected'): self.stage()
        self.assertTrue((self.dest / 'other.bvh').is_file())
        with self.assertRaisesRegex(ValueError, 'different'): module.stage(self.source, self.source, self.manifest)

    def test_rejects_bad_manifest(self):
        self.manifest.write_text('bad  ../outside.bvh\n')
        with self.assertRaises(ValueError): self.stage()

if __name__ == '__main__': unittest.main()
