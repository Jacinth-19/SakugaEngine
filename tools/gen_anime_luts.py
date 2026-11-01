#!/usr/bin/env python3
"""Generates the anime-oriented 3D LUTs (.cube, 32^3, sRGB display space) in assets/luts/. Original work, CC0."""
import numpy as np, pathlib
N = 32
root = pathlib.Path(__file__).resolve().parent.parent / "assets/luts"

def grid():
    g = np.linspace(0, 1, N)
    b, gg, r = np.meshgrid(g, g, g, indexing="ij")          # data index = r + g*N + b*N*N (red fastest)
    return np.stack([r, gg, b], -1).reshape(-1, 3)

def luma(c): return (c * np.array([0.2126, 0.7152, 0.0722])).sum(-1, keepdims=True)
def sat(c, s): l = luma(c); return l + (c - l) * s
def smooth(a, b, x): t = np.clip((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)
def scurve(c, k): return np.clip(c + k * (c - 0.5) * (1 - np.abs(2 * c - 1)) , 0, 1)

def shinkai(c):      # saturated cyan/blue skies, teal shadows, pink-gold highlights, slightly lifted blacks
    l = luma(c)
    c = sat(c, 1.22)
    blue = np.clip(c[..., 2:3] - np.maximum(c[..., 0:1], 0) * 0.6, 0, 1)
    c = c + np.concatenate([-0.02 * blue, 0.02 * blue, 0.07 * blue], -1)            # push blue / cyan dominated colours towards blue
    c = c * (1 - 0.0) + (1 - smooth(0.0, 0.5, l)) * np.array([0.020, 0.045, 0.085])  # teal-blue lifted shadows
    c = c * (1 + smooth(0.55, 1.0, l) * np.array([0.05, -0.005, -0.06]))             # warm highlights
    c = c + smooth(0.75, 1.0, l) * np.array([0.02, 0.0, 0.015])
    return np.clip(scurve(c, 0.10), 0, 1)

def ghibli(c):       # warm, gentle, painterly: lower contrast, greenish-teal shadows, cream highlights
    l = luma(c)
    c = sat(c, 1.12)
    c = c * 0.94 + 0.03
    c = c + (1 - smooth(0.0, 0.55, l)) * np.array([0.00, 0.028, 0.030])
    c = c * (1 + smooth(0.4, 1.0, l) * np.array([0.04, 0.02, -0.05]))
    return np.clip(c, 0, 1)

def pastel(c):       # bright, airy, low contrast with lavender shadows (slice of life / idol anime)
    l = luma(c)
    c = sat(c, 1.05)
    c = 0.10 + c * 0.90
    c = c + (1 - smooth(0.0, 0.6, l)) * np.array([0.045, 0.015, 0.06])
    c = np.clip(c, 0, 1) ** np.array([0.93, 0.94, 0.95])
    return np.clip(c, 0, 1)

def punchy(c):       # TV-anime: strong colour, crisp contrast, no tint
    c = sat(c, 1.28)
    return np.clip(scurve(c, 0.22), 0, 1)

for name, fn in [("Anime Shinkai", shinkai), ("Anime Ghibli", ghibli), ("Anime Pastel", pastel), ("Anime Punchy", punchy)]:
    out = fn(grid())
    with open(root / f"{name}.cube", "w") as f:
        f.write(f'TITLE "{name}"\nLUT_3D_SIZE {N}\nDOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n')
        for r, g, b in out: f.write(f"{r:.6f} {g:.6f} {b:.6f}\n")
    print("wrote", name)
