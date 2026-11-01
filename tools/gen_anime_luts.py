#!/usr/bin/env python3
"""Generates the anime-oriented 3D LUTs (.cube, 32^3, sRGB display space) in assets/luts/. Original work, CC0.

Pure Python on purpose: this is the only generator the asset fetch needs, and it must not drag numpy into the build
(the engine is built from a C++ toolchain only; see tools/fetch_assets.sh).
"""
import pathlib

N = 32
root = pathlib.Path(__file__).resolve().parent.parent / "assets/luts"
step = 1.0 / (N - 1)


def clamp(x, lo=0.0, hi=1.0):
    return lo if x < lo else hi if x > hi else x


def grid():
    """Every (r, g, b) in the .cube order: red changes fastest, then green, then blue."""
    return [(k * step, j * step, i * step) for i in range(N) for j in range(N) for k in range(N)]


def luma(c):
    return c[0] * 0.2126 + c[1] * 0.7152 + c[2] * 0.0722


def sat(c, s):
    l = luma(c)
    return tuple(l + (v - l) * s for v in c)


def smooth(a, b, x):
    t = clamp((x - a) / (b - a))
    return t * t * (3.0 - 2.0 * t)


def scurve(c, k):
    return tuple(clamp(v + k * (v - 0.5) * (1.0 - abs(2.0 * v - 1.0))) for v in c)


def add(c, d, w=1.0):
    return tuple(v + w * d[i] for i, v in enumerate(c))


def mul(c, d):
    return tuple(v * d[i] for i, v in enumerate(c))


def shinkai(c):
    """Saturated cyan/blue skies, teal shadows, pink-gold highlights, slightly lifted blacks."""
    l = luma(c)
    c = sat(c, 1.22)
    blue = clamp(c[2] - max(c[0], 0.0) * 0.6)          # the red term is clamped first (a saturated pixel can be negative)
    c = add(c, (-0.02 * blue, 0.02 * blue, 0.07 * blue))                       # push blue/cyan towards blue
    c = add(mul(c, (1.0, 1.0, 1.0)), (0.020, 0.045, 0.085), 1.0 - smooth(0.0, 0.5, l))  # teal-blue lifted shadows
    c = mul(c, tuple(1.0 + smooth(0.55, 1.0, l) * k for k in (0.05, -0.005, -0.06)))     # warm highlights
    c = add(c, (0.02, 0.0, 0.015), smooth(0.75, 1.0, l))
    return tuple(clamp(v) for v in scurve(c, 0.10))


def ghibli(c):
    """Warm, gentle, painterly: lower contrast, greenish-teal shadows, cream highlights."""
    l = luma(c)
    c = sat(c, 1.12)
    c = tuple(v * 0.94 + 0.03 for v in c)
    c = add(c, (0.0, 0.028, 0.030), 1.0 - smooth(0.0, 0.55, l))
    c = mul(c, tuple(1.0 + smooth(0.4, 1.0, l) * k for k in (0.04, 0.02, -0.05)))
    return tuple(clamp(v) for v in c)


def pastel(c):
    """Bright, airy, low contrast with lavender shadows (slice of life / idol anime)."""
    l = luma(c)
    c = sat(c, 1.05)
    c = tuple(0.10 + v * 0.90 for v in c)
    c = add(c, (0.045, 0.015, 0.06), 1.0 - smooth(0.0, 0.6, l))
    return tuple(clamp(clamp(v) ** e) for v, e in zip(c, (0.93, 0.94, 0.95)))


def punchy(c):
    """TV-anime: strong colour, crisp contrast, no tint."""
    return tuple(clamp(v) for v in scurve(sat(c, 1.28), 0.22))


def main():
    root.mkdir(parents=True, exist_ok=True)
    points = grid()
    for name, fn in [("Anime Shinkai", shinkai), ("Anime Ghibli", ghibli), ("Anime Pastel", pastel),
                     ("Anime Punchy", punchy)]:
        out = [fn(p) for p in points]
        with open(root / f"{name}.cube", "w") as f:
            f.write(f'TITLE "{name}"\nLUT_3D_SIZE {N}\nDOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n')
            for r, g, b in out:
                f.write(f"{r:.6f} {g:.6f} {b:.6f}\n")
        print("wrote", name)


if __name__ == "__main__":
    main()
