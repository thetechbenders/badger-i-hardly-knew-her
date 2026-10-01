#!/usr/bin/env python3
"""Convert a portrait photo into a 1-bit bitmap for the Badger 2040 panel.

The original photo is never modified. The tool crops, scales and converts the
image at the panel's native resolution and can emit a comparison sheet of the
supported conversion methods so they can be judged side by side.

Only geometric crop/scale plus global tone operations (levels, gamma, mild
unsharp mask) are applied. Nothing is synthesised or retouched.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps

METHODS = ("threshold", "bayer8", "floyd", "atkinson")

_BAYER8 = np.array([
    [0, 32, 8, 40, 2, 34, 10, 42],
    [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41],
    [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37],
    [63, 31, 55, 23, 61, 29, 53, 21],
], dtype=np.float64)


def load_gray(path: Path, crop: tuple[int, int, int, int], size: tuple[int, int],
              gamma: float, black_pct: float, white_pct: float,
              sharpen: float) -> np.ndarray:
    """Return a float array in [0,1] (1 = white) at the target size."""
    im = ImageOps.exif_transpose(Image.open(path)).convert("RGB")
    x, y, w, h = crop
    if x < 0 or y < 0 or x + w > im.width or y + h > im.height:
        raise SystemExit(f"crop {crop} outside image {im.size}")
    im = im.crop((x, y, x + w, y + h)).convert("L")
    # Scale in linear light so thin dark features (glasses frames) keep weight.
    lin = (np.asarray(im, dtype=np.float64) / 255.0) ** 2.2
    lin_im = Image.fromarray((lin * 65535).astype(np.uint16).astype(np.int32), mode="I")
    lin_small = np.asarray(lin_im.resize(size, Image.Resampling.LANCZOS), dtype=np.float64) / 65535
    g = np.clip(lin_small, 0, 1) ** (1 / 2.2)
    if sharpen > 0:
        g8 = Image.fromarray((g * 255).round().astype(np.uint8))
        g8 = g8.filter(ImageFilter.UnsharpMask(radius=1.0, percent=int(sharpen * 100), threshold=1))
        g = np.asarray(g8, dtype=np.float64) / 255
    lo, hi = np.percentile(g, [black_pct, 100 - white_pct])
    if hi - lo < 1e-3:
        raise SystemExit("image has no tonal range after levels")
    g = np.clip((g - lo) / (hi - lo), 0, 1) ** gamma
    return g


def otsu(g: np.ndarray) -> float:
    hist, edges = np.histogram(g, bins=256, range=(0, 1))
    p = hist / hist.sum()
    omega = np.cumsum(p)
    mu = np.cumsum(p * np.arange(256))
    mu_t = mu[-1]
    with np.errstate(divide="ignore", invalid="ignore"):
        sb = (mu_t * omega - mu) ** 2 / (omega * (1 - omega))
    return float(np.nanargmax(sb)) / 255


def error_diffuse(g: np.ndarray, kernel: list[tuple[int, int, float]]) -> np.ndarray:
    a = g.copy()
    h, w = a.shape
    out = np.zeros_like(a, dtype=bool)
    for y in range(h):
        for x in range(w):
            old = a[y, x]
            new = 1.0 if old >= 0.5 else 0.0
            out[y, x] = new > 0
            err = old - new
            for dx, dy, k in kernel:
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and yy < h:
                    a[yy, xx] += err * k
    return out


def convert(g: np.ndarray, method: str) -> np.ndarray:
    """Return a bool array, True = white pixel."""
    if method == "threshold":
        return g >= otsu(g)
    if method == "bayer8":
        h, w = g.shape
        t = (np.tile(_BAYER8, (h // 8 + 1, w // 8 + 1))[:h, :w] + 0.5) / 64
        return g >= t
    if method == "floyd":
        return error_diffuse(g, [(1, 0, 7 / 16), (-1, 1, 3 / 16), (0, 1, 5 / 16), (1, 1, 1 / 16)])
    if method == "atkinson":
        k = 1 / 8
        return error_diffuse(g, [(1, 0, k), (2, 0, k), (-1, 1, k), (0, 1, k), (1, 1, k), (0, 2, k)])
    raise SystemExit(f"unknown method {method}")


def to_image(white: np.ndarray) -> Image.Image:
    return Image.fromarray((white * 255).astype(np.uint8)).convert("1", dither=Image.Dither.NONE)


def comparison_sheet(results: dict[str, Image.Image], scale: int) -> Image.Image:
    w, h = next(iter(results.values())).size
    pad, label = 8, 14
    sheet = Image.new("L", ((w * scale + pad) * len(results) + pad, h * scale + label + 2 * pad), 255)
    dr = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(results.items()):
        x = pad + i * (w * scale + pad)
        sheet.paste(im.convert("L").resize((w * scale, h * scale), Image.Resampling.NEAREST), (x, pad + label))
        dr.text((x, pad), name, fill=0)
    return sheet


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", type=Path)
    ap.add_argument("--settings", type=Path, help="JSON with crop/size/tone/method keys")
    ap.add_argument("--crop", help="x,y,w,h in source pixels (default: the largest centred area)")
    ap.add_argument("--size", help="WxH output size")
    ap.add_argument("--method", choices=METHODS)
    ap.add_argument("--out", type=Path, help="1-bit PNG to write for the chosen method")
    ap.add_argument("--compare", type=Path, help="directory for per-method PNGs and a comparison sheet")
    args = ap.parse_args(argv)

    cfg = {"crop": None, "size": [104, 128], "gamma": 1.0,
           "black_pct": 1.0, "white_pct": 2.0, "sharpen": 0.6, "method": "atkinson"}
    if args.settings:
        cfg.update(json.loads(args.settings.read_text()))
    if args.crop:
        cfg["crop"] = [int(v) for v in args.crop.split(",")]
    if args.size:
        cfg["size"] = [int(v) for v in args.size.lower().split("x")]
    if args.method:
        cfg["method"] = args.method
    w, h = cfg["size"]
    if not (8 <= w <= 296 and 8 <= h <= 128):
        raise SystemExit(f"size {w}x{h} does not fit the 296x128 panel")
    if cfg["crop"] is None:  # default: the largest centred area at the output aspect
        with Image.open(args.source) as src:
            W, H = ImageOps.exif_transpose(src).size
        cw, ch = W, round(W * h / w)
        if ch > H:
            cw, ch = round(H * w / h), H
        cfg["crop"] = [(W - cw) // 2, (H - ch) // 2, cw, ch]

    g = load_gray(args.source, tuple(cfg["crop"]), (w, h), cfg["gamma"],
                  cfg["black_pct"], cfg["white_pct"], cfg["sharpen"])
    if args.compare:
        args.compare.mkdir(parents=True, exist_ok=True)
        results = {m: to_image(convert(g, m)) for m in METHODS}
        for m, im in results.items():
            im.save(args.compare / f"portrait_{m}.png")
        gray = Image.fromarray((g * 255).round().astype(np.uint8))
        gray.save(args.compare / "portrait_gray_reference.png")
        comparison_sheet({"gray (ref)": gray, **results}, 1).save(args.compare / "compare_native.png")
        comparison_sheet({"gray (ref)": gray, **results}, 3).save(args.compare / "compare_x3.png")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        to_image(convert(g, cfg["method"])).save(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
