#!/usr/bin/env python3
"""Renders the motion-matching debug video from the CSV that tools/mm_playback_dump writes.

The character on screen is the real retargeted LAFAN1 motion produced by the engine's MotionMatcher (the dump runs the
production matcher and records the 22 humanoid joint positions per frame); this script only draws it. The HUD is what the
video is for: how many clips the library holds, which clip is playing right now, how many clips have been used so far, and
how often the matcher switched.

Drawing is a small pure-Python rasterizer (no Pillow/numpy). Encoding uses ffmpeg when it is available - the
`imageio-ffmpeg` wheel ships a static build - and falls back to an animated GIF through ImageMagick:

    mm_playback_dump --out motion_debug.csv
    python3 tools/gen_motion_debug_video.py --csv motion_debug.csv --out docs/motion_debug.mp4
"""
import argparse
import math
import pathlib
import shutil
import struct
import subprocess
import tempfile
import zlib

# ----------------------------------------------------------------------------- 5x7 bitmap font
FONT = {
    ' ': ["00000"] * 7,
    'A': ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    'B': ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    'C': ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    'D': ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    'E': ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    'F': ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    'G': ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    'H': ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    'I': ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    'J': ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
    'K': ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    'L': ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    'N': ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    'O': ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    'P': ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    'Q': ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    'R': ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    'S': ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    'T': ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    'U': ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    'V': ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    'W': ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    'X': ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    'Y': ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    'Z': ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    '0': ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    '1': ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    '2': ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
    '3': ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    '4': ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    '5': ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    '6': ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    '7': ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    '8': ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    '9': ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    '.': ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    ',': ["00000", "00000", "00000", "00000", "01100", "00100", "01000"],
    ':': ["00000", "01100", "01100", "00000", "01100", "01100", "00000"],
    '-': ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    '_': ["00000", "00000", "00000", "00000", "00000", "00000", "11111"],
    '/': ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    '|': ["00100", "00100", "00100", "00100", "00100", "00100", "00100"],
    '%': ["11001", "11010", "00010", "00100", "01000", "01011", "10011"],
    '(': ["00010", "00100", "01000", "01000", "01000", "00100", "00010"],
    ')': ["01000", "00100", "00010", "00010", "00010", "00100", "01000"],
    '=': ["00000", "00000", "11111", "00000", "11111", "00000", "00000"],
    '+': ["00000", "00100", "00100", "11111", "00100", "00100", "00000"],
    '!': ["00100", "00100", "00100", "00100", "00100", "00000", "00100"],
    '#': ["01010", "01010", "11111", "01010", "11111", "01010", "01010"],
    '*': ["00000", "10101", "01110", "11111", "01110", "10101", "00000"],
    '>': ["01000", "00100", "00010", "00001", "00010", "00100", "01000"],
    '<': ["00010", "00100", "01000", "10000", "01000", "00100", "00010"],
}
GLYPH_W, GLYPH_H = 5, 7


class Frame:
    """RGB frame buffer with the handful of primitives this video needs."""

    def __init__(self, w, h, bg=(0, 0, 0)):
        self.w, self.h = w, h
        self.px = bytearray(bytes((int(bg[0]), int(bg[1]), int(bg[2]))) * (w * h))

    def copy(self):
        f = Frame(self.w, self.h)
        f.px = bytearray(self.px)
        return f

    def put(self, x, y, color):
        if x < 0 or y < 0 or x >= self.w or y >= self.h:
            return
        i = (y * self.w + x) * 3
        p = self.px
        p[i] = int(color[0])
        p[i + 1] = int(color[1])
        p[i + 2] = int(color[2])

    def _blend(self, x, y, color, a):
        if a <= 0.003 or x < 0 or y < 0 or x >= self.w or y >= self.h:
            return
        i = (y * self.w + x) * 3
        p = self.px
        p[i] = int(p[i] + (color[0] - p[i]) * a)
        p[i + 1] = int(p[i + 1] + (color[1] - p[i + 1]) * a)
        p[i + 2] = int(p[i + 2] + (color[2] - p[i + 2]) * a)

    def rect(self, x0, y0, x1, y1, color, a=1.0):
        x0, x1 = max(0, int(x0)), min(self.w, int(x1))
        y0, y1 = max(0, int(y0)), min(self.h, int(y1))
        if x1 <= x0 or y1 <= y0:
            return
        if a >= 0.999:
            row = bytes((int(color[0]), int(color[1]), int(color[2]))) * (x1 - x0)
            for y in range(y0, y1):
                i = (y * self.w + x0) * 3
                self.px[i:i + len(row)] = row
        else:
            for y in range(y0, y1):
                for x in range(x0, x1):
                    self._blend(x, y, color, a)

    def disc(self, cx, cy, r, color, a=1.0):
        for y in range(max(0, int(cy - r - 1)), min(self.h, int(cy + r + 2))):
            for x in range(max(0, int(cx - r - 1)), min(self.w, int(cx + r + 2))):
                d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
                cover = min(1.0, max(0.0, r - d + 0.5))
                if cover > 0:
                    self._blend(x, y, color, cover * a)

    def ring(self, cx, cy, r, thickness, color, a=1.0):
        outer = r + thickness + 1
        for y in range(max(0, int(cy - outer)), min(self.h, int(cy + outer + 1))):
            for x in range(max(0, int(cx - outer)), min(self.w, int(cx + outer + 1))):
                d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
                cover = min(1.0, max(0.0, thickness / 2 - abs(d - r) + 0.5))
                if cover > 0:
                    self._blend(x, y, color, cover * a)

    def line(self, x0, y0, x1, y1, width, color, a=1.0):
        """Thick, antialiased segment (coverage = distance to the segment)."""
        dx, dy = x1 - x0, y1 - y0
        length2 = dx * dx + dy * dy
        half = width * 0.5
        pad = half + 1.5
        for y in range(max(0, int(min(y0, y1) - pad)), min(self.h, int(max(y0, y1) + pad) + 1)):
            for x in range(max(0, int(min(x0, x1) - pad)), min(self.w, int(max(x0, x1) + pad) + 1)):
                px, py = x + 0.5 - x0, y + 0.5 - y0
                t = 0.0 if length2 <= 1e-9 else max(0.0, min(1.0, (px * dx + py * dy) / length2))
                d = math.hypot(px - dx * t, py - dy * t)
                cover = min(1.0, max(0.0, half - d + 0.5))
                if cover > 0:
                    if a >= 0.999 and cover > 0.98:
                        self.put(x, y, color)
                    else:
                        self._blend(x, y, color, cover * a)

    def thin_line(self, x0, y0, x1, y1, color):
        """1 px DDA line - DDA instead of the bounding-box loop, which would touch the whole rectangle for a diagonal."""
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        dx, dy = (x1 - x0) / n, (y1 - y0) / n
        for i in range(n):
            self.put(int(x0 + dx * i), int(y0 + dy * i), color)

    def text(self, x, y, s, color, scale=1, a=1.0):
        cx = int(x)
        for ch in s.upper():
            glyph = FONT.get(ch, FONT[' '])
            for gy, rowbits in enumerate(glyph):
                for gx, bit in enumerate(rowbits):
                    if bit == '1':
                        self.rect(cx + gx * scale, y + gy * scale, cx + (gx + 1) * scale,
                                  y + (gy + 1) * scale, color, a)
            cx += (GLYPH_W + 1) * scale
        return cx

    @staticmethod
    def text_width(s, scale=1):
        return len(s) * (GLYPH_W + 1) * scale

    def png(self, path):
        raw = b"".join(b"\x00" + bytes(self.px[y * self.w * 3:(y + 1) * self.w * 3]) for y in range(self.h))

        def chunk(tag, data):
            return (struct.pack(">I", len(data)) + tag + data +
                    struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        pathlib.Path(path).write_bytes(
            b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 4)) + chunk(b"IEND", b""))


# ----------------------------------------------------------------------------- look
BG_TOP = (10, 13, 19)
BG_BOTTOM = (17, 23, 33)
PANEL = (13, 17, 24)
PANEL_LINE = (34, 44, 58)
TEXT = (224, 234, 246)
TEXT_DIM = (124, 140, 158)
ACCENT = (95, 215, 250)
WARN = (255, 190, 90)
MAGENTA = (232, 120, 220)
GREEN = (120, 226, 160)
GRID = (26, 34, 46)
VIEW_BG = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * 0.35) for i in range(3))   # viewport, after shading

PHASE_COLOR = {"walk": ACCENT, "run": WARN, "stop": TEXT_DIM, "jump": (255, 240, 140),
               "turn": GREEN, "idle": (150, 170, 190), "sweep": MAGENTA}
PHASE_LABEL = {"walk": "WALK (MATCHED)", "run": "RUN (MATCHED)", "stop": "STOP", "jump": "JUMP REQUEST",
               "turn": "TURN", "idle": "IDLE", "sweep": "CLIP SWEEP: BRIEF SAMPLE OF EACH CLIP"}

# parent/child pairs over the 22 humanoid bones (order from engine/anim/Rig.h)
BONES = [(0, 1), (1, 2), (2, 3), (3, 4), (4, 5),
         (3, 6), (6, 7), (7, 8), (8, 9),
         (3, 10), (10, 11), (11, 12), (12, 13),
         (0, 14), (14, 15), (15, 16), (16, 17),
         (0, 18), (18, 19), (19, 20), (20, 21)]
LEFT_SIDE = {6, 7, 8, 9, 14, 15, 16, 17}

TOP_H, BOTTOM_H = 58, 34


def read_csv(path):
    rows = []
    with open(path, newline="") as fh:
        header = fh.readline().strip().split(",")
        jbase = header.index("j0_x")
        for line in fh:
            parts = line.rstrip("\n").split(",")
            if len(parts) < jbase + 66:
                continue
            rows.append({
                "frame": int(parts[0]), "t": float(parts[1]), "phase": parts[2],
                "pos": (float(parts[3]), float(parts[4]), float(parts[5])), "yaw": float(parts[6]),
                "clip": int(parts[7]), "clips": int(parts[8]), "clip_frames": int(parts[9]),
                "name": parts[10], "browsing": int(parts[11]), "transitions": int(parts[12]),
                "joints": [(float(parts[jbase + b * 3]), float(parts[jbase + b * 3 + 1]),
                            float(parts[jbase + b * 3 + 2])) for b in range(22)],
            })
    return rows


class View:
    """Fixed 3/4 camera that follows the character: world metres -> pixels."""

    def __init__(self, w, h, top, bottom, scale):
        self.w, self.h, self.scale = w, h, scale
        self.ground = bottom - 16          # pixel row the ground plane sits on
        self.origin = (w * 0.5, 0.0)       # camera looks at the character's root
        self.angle = 0.62

    def project(self, world, cam):
        x, y, z = world[0] - cam[0], world[1], world[2] - cam[1]
        sx = x * math.cos(self.angle) + z * math.sin(self.angle)
        sz = -x * math.sin(self.angle) + z * math.cos(self.angle)
        return (self.origin[0] + sx * self.scale,
                self.ground - y * self.scale + sz * self.scale * 0.26)


def rot_y(v, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c)


def draw_scene(f, row, view, trail):
    cam = (row["pos"][0], row["pos"][2])
    yaw = math.radians(row["yaw"][0] if isinstance(row["yaw"], (list, tuple)) else row["yaw"])
    colour = PHASE_COLOR.get(row["phase"], ACCENT)

    # ground grid, 1 m spacing, 9 m around the character
    bx, bz = round(cam[0]), round(cam[1])
    for i in range(-9, 10):
        for axis in (0, 1):
            a = (bx + i, 0.0, bz - 9.0) if axis == 0 else (bx - 9.0, 0.0, bz + i)
            b = (bx + i, 0.0, bz + 9.0) if axis == 0 else (bx + 9.0, 0.0, bz + i)
            pa, pb = view.project(a, cam), view.project(b, cam)
            near = 1.0 - min(1.0, abs(i) / 9.0)
            shade = 0.30 + 0.55 * near
            c = tuple(int(VIEW_BG[j] + (GRID[j] - VIEW_BG[j]) * shade) for j in range(3))
            f.thin_line(pa[0], pa[1], pb[0], pb[1], c)

    # the character: recorded joints are in character space, so rotate them by yaw and offset by the root position
    world = []
    for j in row["joints"]:
        local = rot_y(j, yaw)
        world.append((row["pos"][0] + local[0], local[1], row["pos"][2] + local[2]))
    sk = [view.project(wv, cam) for wv in world]

    # motion trail: last second of hip positions
    trail.append((row["pos"][0], row["pos"][2]))
    if len(trail) > 45:
        del trail[0]
    for i, (tx, tz) in enumerate(trail):
        p = view.project((tx, 0.02, tz), cam)
        f.disc(p[0], p[1], 2.6, colour, 0.10 + 0.5 * (i / max(1, len(trail) - 1)))

    # shadow + skeleton
    hx, hz = view.project((row["pos"][0], 0.0, row["pos"][2]), cam)
    f.disc(hx, hz, 26, (5, 7, 11), 0.55)
    f.ring(hx, hz, 30, 1.2, colour, 0.25)
    for a, b in BONES:
        f.line(sk[a][0], sk[a][1], sk[b][0], sk[b][1], 4.2 if a not in LEFT_SIDE else 3.6,
               colour, 1.0 if a not in LEFT_SIDE else 0.62)
    for i in range(22):
        f.disc(sk[i][0], sk[i][1], 3.4 if i in (0, 4, 5, 9, 13, 16, 17, 20, 21) else 2.4,
               (250, 253, 255), 0.95)
    f.ring(sk[5][0], sk[5][1], 8, 2.4, colour, 0.9)                  # head halo
    return colour


def draw_hud(f, row, w, h, stats):
    colour = PHASE_COLOR.get(row["phase"], ACCENT)
    used = stats["used"][row["frame"]]
    switches = stats["switches"][row["frame"]]
    dwell = stats["dwell"][row["frame"]]

    # ---------------- top panel
    f.rect(0, 0, w, TOP_H, PANEL)
    f.rect(0, TOP_H - 1, w, TOP_H, PANEL_LINE)
    f.disc(14, 13, 5, ACCENT)
    f.text(26, 9, "MOTION MATCHER", TEXT, 1)
    f.text(26, 21, f"{row['clips']} CLIPS IN LIBRARY", TEXT_DIM, 1)
    f.text(26, 40, f"USED {used}/{row['clips']}", GREEN if used >= row["clips"] else ACCENT, 1)
    idx_txt = f"{row['clip'] + 1}/{row['clips']}"
    f.text(w - 16 - Frame.text_width(idx_txt, 3), 6, idx_txt, colour, 3)
    name = row["name"]
    f.text(w - 16 - Frame.text_width(name, 1), 36, name, TEXT, 1)

    # one tick per clip: the clip playing now, the clips already used, and the rest
    bar_base = TOP_H - 3
    f.rect(16, bar_base - 7, w - 16, bar_base - 6, (28, 36, 48))
    step = (w - 32) / max(1, row["clips"])
    seen = stats["used_clips"][row["frame"]]
    for i in range(row["clips"]):
        x = 16 + i * step
        if i == row["clip"]:
            c, hgt = colour, 7
        elif i in seen:
            c, hgt = (78, 104, 132), 4
        else:
            c, hgt = (40, 49, 64), 3
        f.rect(x, bar_base - hgt, x + max(1.0, step - 0.5), bar_base, c)

    # phase label + colour key, right under the top panel
    f.text(16, TOP_H + 8, PHASE_LABEL.get(row["phase"], row["phase"].upper()), colour, 1)
    f.text(16, TOP_H + 20, "SKELETAL DEBUG VISUALIZATION - LAFAN1 / UBISOFT LA FORGE", TEXT_DIM, 1)

    # ---------------- bottom panel
    y0 = h - BOTTOM_H
    f.rect(0, y0, w, h, PANEL)
    f.rect(0, y0, w, y0 + 1, PANEL_LINE)
    y = y0 + 13
    f.text(16, y, f"FRAME {row['frame']:04d}", TEXT_DIM, 1)
    f.text(130, y, f"CLIP CHANGES {switches:02d}", TEXT, 1)
    f.text(300, y, f"ON THIS CLIP {dwell:.2f}S", TEXT, 1)
    right = f"BROWSING {row['name']}" if row["browsing"] else f"CLIPS USED {used}/{row['clips']}"
    f.text(w - 16 - Frame.text_width(right, 1), y, right, GREEN if used >= row["clips"] else TEXT, 1)


def build_stats(rows):
    switches, dwell, used, used_clips = {}, {}, {}, {}
    seen, count, last_clip, clip_start = set(), 0, None, 0.0
    for r in rows:
        if last_clip != r["clip"]:
            count += 1
            last_clip = r["clip"]
            clip_start = r["t"]
            seen.add(r["clip"])
        switches[r["frame"]] = max(0, count - 1)
        dwell[r["frame"]] = r["t"] - clip_start
        used[r["frame"]] = len(seen)
        used_clips[r["frame"]] = tuple(seen)
    return {"switches": switches, "dwell": dwell, "used": used, "used_clips": used_clips}


def background(w, h):
    f = Frame(w, h, BG_TOP)
    for y in range(h):
        t = y / (h - 1)
        c = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3))
        if TOP_H <= y < h - BOTTOM_H:                      # viewport: a touch darker than the panels
            c = tuple(int(c[i] * 0.78 + VIEW_BG[i] * 0.22) for i in range(3))
        f.rect(0, y, w, y + 1, c)
    return f


class Scene:
    """Everything a frame needs, so the frames can be rendered one at a time (flat memory for long takes)."""

    def __init__(self, rows, w, h):
        self.stats = build_stats(rows)          # computed over the whole take, not the sampled frames
        self.base = background(w, h)
        self.view = View(w, h, TOP_H, h - BOTTOM_H, h * 0.27)
        self.trail = []
        self.w, self.h = w, h

    def frame(self, row):
        f = self.base.copy()
        draw_scene(f, row, self.view, self.trail)
        draw_hud(f, row, self.w, self.h, self.stats)
        return f


def ffmpeg_exe():
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg                       # optional: pip install imageio-ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="motion_debug.csv")
    ap.add_argument("--out", default="docs/motion_debug.mp4")
    ap.add_argument("--width", type=int, default=960)
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--max-frames", type=int, default=1200)
    ap.add_argument("--format", choices=["auto", "mp4", "gif"], default="auto")
    args = ap.parse_args()

    rows = read_csv(args.csv)
    if len(rows) < 2:
        raise SystemExit("at least two rows required in " + args.csv)
    if args.width < 640 or args.width % 2 or args.fps <= 0 or args.max_frames < 2:
        raise SystemExit("width must be even and >= 640, fps positive, max-frames >= 2")
    src_hz = 1.0 / (rows[1]["t"] - rows[0]["t"]) if len(rows) > 1 else 30.0
    step = max(1, int(round(src_hz / args.fps)))
    chosen = rows[::step]
    if len(chosen) > args.max_frames:                      # even out a long take instead of truncating it
        stride = len(chosen) / args.max_frames
        chosen = [chosen[int(i * stride)] for i in range(args.max_frames)]

    w = args.width
    h = int(round(w * 9 / 16))
    scene = Scene(rows, w, h)                   # stats from all rows; frames from the sampled ones
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    fmt = args.format
    if fmt == "auto":
        fmt = "mp4" if ffmpeg_exe() else "gif"

    tmp = pathlib.Path(tempfile.mkdtemp(prefix="sakuga_video_"))
    for n, row in enumerate(chosen):
        scene.frame(row).png(tmp / f"f{n:05d}.png")
    delay_ms = 1000.0 / max(1.0, len(chosen) / max(1e-6, chosen[-1]["t"] - chosen[0]["t"]))

    if fmt == "mp4":
        exe = ffmpeg_exe()
        if not exe:
            raise SystemExit("no ffmpeg found (pip install imageio-ffmpeg, or apt install ffmpeg)")
        out = out.with_suffix(".mp4")
        cmd = [exe, "-y", "-loglevel", "error", "-framerate", f"{1000.0 / delay_ms:.3f}",
               "-i", str(tmp / "f%05d.png"), "-c:v", "libx264", "-preset", "slow", "-crf", "23",
               "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(out)]
        subprocess.run(cmd, check=True)
    else:
        out = out.with_suffix(".gif")
        if not shutil.which("convert"):
            print("no ImageMagick 'convert' and no ffmpeg: PNG frames left in", tmp)
            return
        subprocess.run(["convert", "-delay", str(max(1, int(round(delay_ms / 10)))), "-loop", "0",
                        str(tmp / "f*.png"), "-resize", "70%", "-layers", "Optimize",
                        "-colors", "128", str(out)], check=True)

    # poster still (README / store listing) from the last frame: the sweep has just played clip 77/77
    poster = out.with_name(out.stem + "_poster.png")
    scene.trail = []
    scene.frame(chosen[-1]).png(str(poster))

    shutil.rmtree(tmp, ignore_errors=True)
    print(f"{out}: {len(chosen)} frames, {len(chosen) * delay_ms / 1000:.1f} s, "
          f"{out.stat().st_size / 1048576:.2f} MB ({fmt})")
    print(f"{poster}: poster")


if __name__ == "__main__":
    main()
