#!/usr/bin/env python3
"""Rasterise monochrome SVG icons (single <path>, 24x24 viewBox) into 1-bit
bitmaps for the badge, and emit firmware/generated/icons.cpp.

Deterministic and dependency-light: a small SVG path parser (M L H V C S Q T A Z,
absolute and relative), curve/arc flattening, and a supersampled non-zero
winding fill in NumPy. No system SVG library is involved, so the output only
depends on the pinned input files.

  tools/iconsgen.py --out firmware/generated/icons.cpp            (write)
  tools/iconsgen.py --out firmware/generated/icons.cpp --check    (CI: stale?)
  tools/iconsgen.py --preview build/icons_preview.png             (x8 sheet)
"""
from __future__ import annotations

import argparse
import hashlib
import math
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "third_party" / "simple-icons"
# (C identifier, SVG file, pixel size). Sizes chosen for 14 px contact rows.
ICONS = [("github", "github.svg", 12), ("discord", "discord.svg", 12)]
SUPERSAMPLE = 16
THRESHOLD = 0.5  # pixel is ink when at least half of its area is covered

_TOKEN = re.compile(r"[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def _arc_flags(tokens, i):
    # Arc flags may be written without separators ("a.5.5 0 01..."): split them.
    out = []
    for _ in range(2):
        t = tokens[i]
        if t in ("0", "1"):
            out.append(int(t)); i += 1
        elif re.fullmatch(r"[01][0-9.].*", t):
            out.append(int(t[0])); tokens[i] = t[1:]
        else:
            out.append(int(float(t))); i += 1
    return out, i


def parse_path(d: str) -> list[list[tuple[float, float]]]:
    """Return a list of closed polygons (flattened subpaths)."""
    raw = _TOKEN.findall(d)
    # Re-split numbers like "0.0741.0741" -> "0.0741", ".0741".
    tokens = []
    for t in raw:
        if t[0].isalpha():
            tokens.append(t)
            continue
        while t.count(".") > 1:
            k = t.index(".", t.index(".") + 1)
            tokens.append(t[:k]); t = t[k:]
        tokens.append(t)
    polys, cur = [], []
    x = y = sx = sy = 0.0
    lcx = lcy = None  # last control point for S/T
    cmd = None
    i = 0

    def num():
        nonlocal i
        v = float(tokens[i]); i += 1
        return v

    def cubic(p0, p1, p2, p3, n=16):
        for k in range(1, n + 1):
            t = k / n
            mt = 1 - t
            cur.append((mt**3 * p0[0] + 3 * mt * mt * t * p1[0] + 3 * mt * t * t * p2[0] + t**3 * p3[0],
                        mt**3 * p0[1] + 3 * mt * mt * t * p1[1] + 3 * mt * t * t * p2[1] + t**3 * p3[1]))

    def quad(p0, p1, p2, n=12):
        for k in range(1, n + 1):
            t = k / n
            mt = 1 - t
            cur.append((mt * mt * p0[0] + 2 * mt * t * p1[0] + t * t * p2[0],
                        mt * mt * p0[1] + 2 * mt * t * p1[1] + t * t * p2[1]))

    def arc(x1, y1, rx, ry, phi, fa, fs, x2, y2):
        # SVG implementation notes F.6 (endpoint -> centre parameterisation).
        if rx == 0 or ry == 0:
            cur.append((x2, y2)); return
        rx, ry = abs(rx), abs(ry)
        c, s = math.cos(math.radians(phi)), math.sin(math.radians(phi))
        dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
        x1p, y1p = c * dx + s * dy, -s * dx + c * dy
        lam = x1p**2 / rx**2 + y1p**2 / ry**2
        if lam > 1:
            rx *= math.sqrt(lam); ry *= math.sqrt(lam)
        num_ = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
        den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
        co = math.sqrt(max(0.0, num_ / den)) if den else 0.0
        if fa == fs:
            co = -co
        cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
        cx = c * cxp - s * cyp + (x1 + x2) / 2
        cy = s * cxp + c * cyp + (y1 + y2) / 2

        def ang(ux, uy, vx, vy):
            a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
            return a
        t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
        dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
        if not fs and dt > 0:
            dt -= 2 * math.pi
        elif fs and dt < 0:
            dt += 2 * math.pi
        n = max(4, int(abs(dt) / (math.pi / 16)) + 1)
        for k in range(1, n + 1):
            t = t1 + dt * k / n
            px, py = rx * math.cos(t), ry * math.sin(t)
            cur.append((c * px - s * py + cx, s * px + c * py + cy))

    while i < len(tokens):
        if tokens[i][0].isalpha():
            cmd = tokens[i]; i += 1
            if cmd in "Zz":
                if cur:
                    polys.append(cur)
                cur = []
                x, y = sx, sy
                lcx = lcy = None
                continue
        rel = cmd.islower()
        C = cmd.upper()
        ox, oy = (x, y) if rel else (0.0, 0.0)
        if C == "M":
            if cur:
                polys.append(cur)
            x, y = ox + num(), oy + num()
            sx, sy = x, y
            cur = [(x, y)]
            cmd = "l" if rel else "L"  # implicit lineto for further pairs
            lcx = lcy = None
        elif C == "L":
            x, y = ox + num(), oy + num(); cur.append((x, y)); lcx = lcy = None
        elif C == "H":
            x = ox + num(); cur.append((x, y)); lcx = lcy = None
        elif C == "V":
            y = oy + num(); cur.append((x, y)); lcx = lcy = None
        elif C == "C":
            p1 = (ox + num(), oy + num()); p2 = (ox + num(), oy + num()); p3 = (ox + num(), oy + num())
            cubic((x, y), p1, p2, p3); x, y = p3; lcx, lcy = p2
        elif C == "S":
            p1 = (2 * x - lcx, 2 * y - lcy) if lcx is not None else (x, y)
            p2 = (ox + num(), oy + num()); p3 = (ox + num(), oy + num())
            cubic((x, y), p1, p2, p3); x, y = p3; lcx, lcy = p2
        elif C == "Q":
            p1 = (ox + num(), oy + num()); p2 = (ox + num(), oy + num())
            quad((x, y), p1, p2); x, y = p2; lcx, lcy = p1
        elif C == "T":
            p1 = (2 * x - lcx, 2 * y - lcy) if lcx is not None else (x, y)
            p2 = (ox + num(), oy + num())
            quad((x, y), p1, p2); x, y = p2; lcx, lcy = p1
        elif C == "A":
            rx, ry, phi = num(), num(), num()
            (fa, fs), i = _arc_flags(tokens, i)
            x2, y2 = ox + num(), oy + num()
            arc(x, y, rx, ry, phi, fa, fs, x2, y2); x, y = x2, y2; lcx = lcy = None
        else:
            raise ValueError(f"unsupported path command {cmd}")
    if cur:
        polys.append(cur)
    return polys


def rasterise(polys, size: int, view: float = 24.0) -> np.ndarray:
    """Non-zero winding coverage on a supersampled grid -> bool[size, size] (True = ink)."""
    n = size * SUPERSAMPLE
    coords = (np.arange(n) + 0.5) * view / n
    px, py = np.meshgrid(coords, coords)
    wind = np.zeros((n, n), dtype=np.int32)
    for poly in polys:
        pts = poly + [poly[0]]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            if y0 == y1:
                continue
            up = y0 <= py
            cross = (up != (y1 <= py))
            t = (py - y0) / (y1 - y0)
            xi = x0 + t * (x1 - x0)
            hit = cross & (xi > px)
            wind += np.where(hit, 1 if y1 > y0 else -1, 0)
    ink = wind != 0
    cov = ink.reshape(size, SUPERSAMPLE, size, SUPERSAMPLE).mean(axis=(1, 3))
    return cov >= THRESHOLD


def load_icon(svg: Path):
    text = svg.read_text()
    m = re.search(r'viewBox="0 0 24 24"', text)
    if not m:
        raise SystemExit(f"{svg}: expected a 24x24 viewBox")
    d = re.search(r'<path[^>]* d="([^"]+)"', text).group(1)
    return d, hashlib.sha256(svg.read_bytes()).hexdigest()


def pack(bits: np.ndarray) -> bytes:
    h, w = bits.shape
    stride = (w + 7) // 8
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if bits[y, x]:
                out[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def emit(icons) -> str:
    lines = ["// GENERATED by tools/iconsgen.py - do not edit.",
             "// Source: Simple Icons 16.33.0 (CC0-1.0), third_party/simple-icons/.",
             "// Trademarks belong to their owners; see docs/LICENSES.md.",
             '#include "../core/icons.hpp"', "", "namespace badge {", "namespace {"]
    for ident, fname, size, sha, bits in icons:
        lines.append(f"// {fname} sha256 {sha}, {size}x{size}:")
        for row in bits:
            lines.append("//   " + "".join("#" if v else "." for v in row))
        data = pack(bits)
        lines.append(f"const uint8_t {ident}_bits[] = {{" + ", ".join(f"0x{b:02X}" for b in data) + "};")
    lines.append("}  // namespace")
    lines.append("namespace icons {")
    for ident, fname, size, sha, bits in icons:
        lines.append(f"const Icon {ident} = {{{size}, {size}, {(size + 7) // 8}, {ident}_bits}};")
    lines += ["}  // namespace icons", "}  // namespace badge", ""]
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=ROOT / "firmware/generated/icons.cpp")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--preview", type=Path)
    ap.add_argument("--sizes", default="", help="comma list of extra sizes for --preview, e.g. 9,10,11,12")
    a = ap.parse_args(argv)
    built = []
    for ident, fname, size in ICONS:
        d, sha = load_icon(SRC / fname)
        built.append((ident, fname, size, sha, rasterise(parse_path(d), size)))
    if a.preview:
        from PIL import Image, ImageDraw
        sizes = [int(s) for s in a.sizes.split(",") if s] or [ICONS[0][2]]
        cell = max(sizes) * 8 + 16
        sheet = Image.new("L", (cell * len(sizes) * 2 + 16, len(ICONS) * (cell + 40) + 16), 255)
        dr = ImageDraw.Draw(sheet)
        for r, (ident, fname, _size) in enumerate(ICONS):
            d, _ = load_icon(SRC / fname)
            polys = parse_path(d)
            for c, s in enumerate(sizes):
                b = rasterise(polys, s)
                im = Image.fromarray(np.where(b, 0, 255).astype(np.uint8))
                x0, y0 = 16 + c * 2 * cell, 16 + r * (cell + 40)
                sheet.paste(im.resize((s * 8, s * 8), Image.Resampling.NEAREST), (x0, y0 + 14))
                sheet.paste(im, (x0 + s * 8 + 8, y0 + 14))
                dr.text((x0, y0), f"{ident} {s}px", fill=0)
        a.preview.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(a.preview)
        return 0
    text = emit(built)
    if a.check:
        if not a.out.exists() or a.out.read_text() != text:
            print(f"{a.out} is stale; rerun tools/iconsgen.py", file=sys.stderr)
            return 1
        print(f"{a.out} up to date")
        return 0
    a.out.write_text(text)
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
