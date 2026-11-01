#!/usr/bin/env python3
"""Generates the installer art in installer/art/ - no dependencies, no fonts, no PIL.

Inno Setup will use these for the wizard sidebar, the small header logo, the optional wizard background (6.7+) and the
setup icon. They are *generated*, not committed: `python3 tools/gen_installer_art.py` (which .github/workflows/windows.yml
runs before ISCC) writes

    side_light.png / side_dark.png    164x314  large wizard image (modern style; moved/hidden in 6.7 background mode)
    small_light.png / small_dark.png  55x58    small wizard image (header logo)
    back_light.png / back_dark.png    640x480  wizard background image (Inno 6.7+), used with WizardBackImageOpacity
    side_light.bmp / small_light.bmp           BMP fallbacks for Inno < 6.6 (no PNG/alpha support)
    sakuga.ico                                multi-size icon (16..256, 32bpp)

The palette is the one in config/ui_theme.json: near-black blue-grey panels, a cyan accent, one white highlight.
The motif is a comet: a decaying motion trail, which is what the engine is about.
"""
import math
import pathlib
import struct
import zlib

OUT = pathlib.Path(__file__).resolve().parent.parent / "installer" / "art"

BG_TOP = (0.035, 0.043, 0.058)      # #0b0e14-ish
BG_BOTTOM = (0.075, 0.098, 0.130)
ACCENT = (0.372, 0.843, 0.98)       # cyan, close to config/ui_theme.json's CheckMark
ACCENT_DIM = (0.20, 0.52, 0.66)
WHITE = (0.93, 0.96, 1.0)
LIGHT_TOP = (0.965, 0.973, 0.984)
LIGHT_BOTTOM = (0.87, 0.90, 0.93)
LIGHT_ACCENT = (0.10, 0.45, 0.62)


class Canvas:
    """RGB float canvas with supersampling; everything is drawn with analytic coverage, then box-downsampled."""

    def __init__(self, w, h, ss=3):
        self.w, self.h, self.ss = w, h, ss
        self.W, self.H = w * ss, h * ss
        self.px = [0.0] * (self.W * self.H * 3)

    # ---------------------------------------------------------------- primitives
    def vgrad(self, top, bottom):
        for y in range(self.H):
            t = y / max(1, self.H - 1)
            r = top[0] + (bottom[0] - top[0]) * t
            g = top[1] + (bottom[1] - top[1]) * t
            b = top[2] + (bottom[2] - top[2]) * t
            base = y * self.W * 3
            row = [r, g, b] * self.W
            self.px[base:base + self.W * 3] = row

    def blend(self, x, y, color, a):
        if a <= 0.0 or x < 0 or y < 0 or x >= self.W or y >= self.H:
            return
        a = min(1.0, a)
        i = (y * self.W + x) * 3
        p = self.px
        p[i] += (color[0] - p[i]) * a
        p[i + 1] += (color[1] - p[i + 1]) * a
        p[i + 2] += (color[2] - p[i + 2]) * a

    def disc(self, cx, cy, radius, color, alpha=1.0, glow=0.0):
        """Filled circle; `glow` adds a soft falloff halo of the same colour."""
        reach = radius + glow
        x0, x1 = int(max(0, cx - reach - 2)), int(min(self.W - 1, cx + reach + 2))
        y0, y1 = int(max(0, cy - reach - 2)), int(min(self.H - 1, cy + reach + 2))
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
                a = min(1.0, max(0.0, radius - d + 0.5))                 # hard edge, 1px AA
                if glow > 0.0 and d > radius:
                    a = max(a, glow * max(0.0, 1.0 - (d - radius) / glow) ** 2)
                self.blend(x, y, color, a * alpha)

    def ring(self, cx, cy, radius, thickness, color, alpha=1.0, arc=None):
        """Circle outline; `arc` = (start, end) in radians, counter-clockwise, for partial rings."""
        outer = radius + thickness / 2 + 1
        x0, x1 = int(max(0, cx - outer)), int(min(self.W - 1, cx + outer))
        y0, y1 = int(max(0, cy - outer)), int(min(self.H - 1, cy + outer))
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                dx, dy = x + 0.5 - cx, y + 0.5 - cy
                d = math.hypot(dx, dy)
                a = min(1.0, max(0.0, thickness / 2 - abs(d - radius) + 0.5))
                if arc and a > 0.0:
                    ang = math.atan2(dy, dx) % (2 * math.pi)
                    s, e = arc[0] % (2 * math.pi), arc[1] % (2 * math.pi)
                    inside = (s <= ang <= e) if s <= e else (ang >= s or ang <= e)
                    if not inside:
                        a = 0.0
                self.blend(x, y, color, a * alpha)

    def comet(self, cx, cy, radius, turns=2.1, tilt=-0.35, colour=ACCENT, highlight=WHITE,
              start=0.55, tail=0.55, glow=0.35, head=0.085):
        """A motion trail: beads along a spiral that shrinks and fades towards the tail, with a bright head."""
        # enough beads for the trail to read as one tapered stroke at large sizes, few enough to stay cheap
        steps = max(24, int(radius * 2.2 * turns))
        for i in range(steps):
            t = i / (steps - 1)                                  # 0 = head, 1 = tail
            ang = start + tilt + turns * 2 * math.pi * t
            r = radius * (1.0 - tail * t)
            x = cx + math.cos(ang) * r
            y = cy + math.sin(ang) * r
            k = (1.0 - t) ** 1.5                                 # brightness/size falloff
            d = radius * (0.030 + 0.085 * k)
            c = tuple(colour[j] + (highlight[j] - colour[j]) * min(1.0, k * 1.3) for j in range(3))
            self.disc(x, y, d, c, alpha=0.22 + 0.78 * k, glow=d * glow)
        # the head: one crisp bead so the mark has a focal point
        x = cx + math.cos(start + tilt) * radius
        y = cy + math.sin(start + tilt) * radius
        self.disc(x, y, radius * head, highlight, glow=radius * 0.16)

    # ---------------------------------------------------------------- output
    def downsample(self):
        ss = self.ss
        out = bytearray(self.w * self.h * 3)
        n = ss * ss
        for y in range(self.h):
            for x in range(self.w):
                r = g = b = 0.0
                for sy in range(ss):
                    base = ((y * ss + sy) * self.W + x * ss) * 3
                    for sx in range(ss):
                        i = base + sx * 3
                        r += self.px[i]; g += self.px[i + 1]; b += self.px[i + 2]
                o = (y * self.w + x) * 3
                out[o] = int(max(0, min(255, round(r / n * 255))))
                out[o + 1] = int(max(0, min(255, round(g / n * 255))))
                out[o + 2] = int(max(0, min(255, round(b / n * 255))))
        return bytes(out)


# -------------------------------------------------------------------- writers
def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    path.write_bytes(png)


def write_bmp24(path, w, h, rgb):
    row = (w * 3 + 3) & ~3
    data = bytearray()
    for y in range(h - 1, -1, -1):                       # BMP rows are bottom-up
        line = bytearray()
        for x in range(w):
            i = (y * w + x) * 3
            line += bytes((rgb[i + 2], rgb[i + 1], rgb[i]))   # BGR
        line += b"\x00" * (row - len(line))
        data += line
    header = struct.pack("<2sIHHI", b"BM", 14 + 40 + len(data), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    path.write_bytes(header + info + bytes(data))


def write_ico(path, sizes_rgb):
    """Multi-size .ico with 32bpp DIB entries (top-down XOR bitmap + empty AND mask)."""
    entries, blobs = b"", []
    offset = 6 + 16 * len(sizes_rgb)
    for size, rgb in sizes_rgb:
        px = bytearray()
        for y in range(size - 1, -1, -1):
            for x in range(size):
                i = (y * size + x) * 3
                px += bytes((rgb[i + 2], rgb[i + 1], rgb[i], 255))
        mask_row = ((size + 31) // 32) * 4
        mask = b"\x00" * (mask_row * size)
        dib = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(px), 2835, 2835, 0, 0) + bytes(px) + mask
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(dib), offset)
        blobs.append(dib)
        offset += len(dib)
    path.write_bytes(struct.pack("<HHH", 0, 1, len(sizes_rgb)) + entries + b"".join(blobs))


# -------------------------------------------------------------------- the art
def sidebar(dark):
    w, h = 164, 314
    c = Canvas(w, h)
    c.vgrad(BG_TOP if dark else LIGHT_TOP, BG_BOTTOM if dark else LIGHT_BOTTOM)
    accent = ACCENT if dark else LIGHT_ACCENT
    # a vertical accent hairline on the left edge, fading out
    for y in range(c.H):
        t = y / c.H
        a = (1.0 - abs(t - 0.5) * 1.6) * 0.55
        for x in range(0, 2 * c.ss):
            c.blend(x, y, accent, max(0.0, a))
    # the mark
    c.comet(w * 0.5 * c.ss, h * 0.33 * c.ss, 47 * c.ss, turns=1.8, tilt=-0.15,
            colour=accent, highlight=WHITE if dark else (0.05, 0.26, 0.36), glow=0.45)
    # caption-ish tick marks (no fonts available, so: a small sequence of fading bars)
    for i in range(5):
        y = (222 + i * 9) * c.ss
        length = (26 - i * 4) * c.ss
        for x in range(int(119 * c.ss - length), int(119 * c.ss)):
            for dy in range(2 * c.ss):
                c.blend(x, y + dy, accent, 0.55 - i * 0.09)
    return w, h, c.downsample()


def small_logo(dark):
    w, h = 55, 58
    c = Canvas(w, h)
    c.vgrad(BG_TOP if dark else LIGHT_TOP, BG_BOTTOM if dark else LIGHT_BOTTOM)
    c.comet(w * 0.5 * c.ss, h * 0.5 * c.ss, 19 * c.ss, turns=1.95, tilt=-0.15,
            colour=ACCENT if dark else LIGHT_ACCENT,
            highlight=WHITE if dark else (0.05, 0.26, 0.36), glow=0.5)
    return w, h, c.downsample()


def background(dark):
    w, h = 640, 480
    c = Canvas(w, h, ss=2)
    c.vgrad(BG_TOP if dark else LIGHT_TOP, BG_BOTTOM if dark else LIGHT_BOTTOM)
    accent = ACCENT if dark else LIGHT_ACCENT
    # huge, faint comet off the bottom-right corner; WizardBackImageOpacity blends it into the page colour
    c.comet(w * 1.02 * c.ss, h * 1.05 * c.ss, 330 * c.ss, turns=1.6, tilt=-1.4,
            colour=accent, highlight=WHITE, glow=0.3)
    c.comet(w * 0.96 * c.ss, h * 0.98 * c.ss, 235 * c.ss, turns=2.0, tilt=-1.2,
            colour=accent, highlight=WHITE, glow=0.3)
    for i in range(3):                                   # concentric rings echoing the mark
        c.ring(w * 1.0 * c.ss, h * 1.02 * c.ss, (300 + i * 46) * c.ss, 2 * c.ss, accent, alpha=0.16)
    return w, h, c.downsample()


def icon(size):
    c = Canvas(size, size, ss=max(2, 384 // max(size, 1)))
    # rounded-square plate, drawn as a disc of the theme's background plus a subtle inner glow
    c1 = (0.055, 0.065, 0.085)
    for y in range(c.H):
        for x in range(c.W):
            c.blend(x, y, c1, 1.0)
    c.disc(size * 0.5 * c.ss, size * 0.5 * c.ss, size * 0.46 * c.ss, (0.08, 0.10, 0.135))
    c.ring(size * 0.5 * c.ss, size * 0.5 * c.ss, size * 0.44 * c.ss, max(1.0, size * 0.03) * c.ss, ACCENT, alpha=0.85)
    c.comet(size * 0.5 * c.ss, size * 0.5 * c.ss, size * 0.31 * c.ss, turns=1.95, tilt=-0.15,
            colour=ACCENT, highlight=WHITE, glow=0.4)
    return c.downsample()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for dark, tag in ((False, "light"), (True, "dark")):
        w, h, rgb = sidebar(dark)
        write_png(OUT / f"side_{tag}.png", w, h, rgb)
        if not dark:
            write_bmp24(OUT / f"side_{tag}.bmp", w, h, rgb)          # fallback for Inno < 6.6
        w, h, rgb = small_logo(dark)
        write_png(OUT / f"small_{tag}.png", w, h, rgb)
        if not dark:
            write_bmp24(OUT / f"small_{tag}.bmp", w, h, rgb)
        w, h, rgb = background(dark)
        write_png(OUT / f"back_{tag}.png", w, h, rgb)

    sizes = [(s, icon(s)) for s in (16, 32, 48, 64, 128, 256)]
    write_ico(OUT / "sakuga.ico", sizes)

    for f in sorted(OUT.iterdir()):
        print(f"  {f.name:<18} {f.stat().st_size / 1024:8.1f} KB")


if __name__ == "__main__":
    main()
