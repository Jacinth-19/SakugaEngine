#!/usr/bin/env python3
"""GPU-free installer screenshot and generated artwork checks."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('installer_art', ROOT / 'tools/gen_installer_art.py')
art = importlib.util.module_from_spec(spec)
spec.loader.exec_module(art)


class InstallerArtTest(unittest.TestCase):
    def test_preview_and_sidebars_are_current(self):
        standing = art.read_screenshot(ROOT / 'docs/screenshots/vrm_standing.png')
        posed = art.read_screenshot(ROOT / 'docs/screenshots/lab_npr.png')
        self.assertEqual(standing[:2], (900, 900))
        self.assertEqual(posed[:2], (900, 900))
        items = [
            ('avatar_preview', art.avatar_preview(standing, posed)),
            ('side_light', art.avatar_sidebar(posed, (202, 338, 496, 900))),
            ('side_dark', art.avatar_sidebar(standing, (220, 145, 614, 900))),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            for name, (w, h, rgb) in items:
                self.assertEqual(len(rgb), w * h * 3)
                output = Path(tmp) / (name + '.png')
                art.write_png(output, w, h, rgb)
                self.assertEqual(output.read_bytes(), (ROOT / 'installer/art' / output.name).read_bytes())
                if name != 'side_dark':
                    output = Path(tmp) / (name + '.bmp')
                    art.write_bmp24(output, w, h, rgb)
                    self.assertEqual(output.read_bytes(), (ROOT / 'installer/art' / output.name).read_bytes())

    def test_invalid_input_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'bad.png'
            path.write_bytes(b'not a png')
            with self.assertRaises(ValueError):
                art.read_screenshot(path)
        with self.assertRaises(ValueError):
            art.place_crop(bytearray(3), 1, 1, (1, 1, b'\0\0\0\xff'), (0, 0, 2, 1), (0, 0, 1, 1))


if __name__ == '__main__':
    unittest.main()
