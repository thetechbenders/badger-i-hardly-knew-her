#!/usr/bin/env python3
"""Generate compiled bitmap fonts for the badge from DejaVu TrueType fonts.

Glyphs are rasterised with FreeType's monochrome hinting (Pillow fontmode "1")
at the exact pixel size used on the panel, so the host previews and the device
share one set of bitmaps. The output is deterministic for a given Pillow
version (FreeType is bundled in the Pillow wheel); tools/requirements.txt pins it.

Usage: tools/fontgen.py --fonts-dir <dir containing DejaVu ttf files> --out firmware/generated/fonts.cpp
"""
from __future__ import annotations

import argparse
import hashlib
import sys
import zlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# (C identifier, TTF file, pixel size)
FONTS = [
    ("sans_bold_24", "DejaVuSans-Bold.ttf", 24),
    ("sans_bold_20", "DejaVuSans-Bold.ttf", 20),
    ("sans_bold_17", "DejaVuSansCondensed-Bold.ttf", 17),
    ("sans_bold_14", "DejaVuSans-Bold.ttf", 14),
    ("sans_bold_12", "DejaVuSans-Bold.ttf", 12),
    ("sans_12", "DejaVuSans.ttf", 12),
    ("sans_11", "DejaVuSans.ttf", 11),
    ("sans_10", "DejaVuSans.ttf", 10),
    ("sans_bold_10", "DejaVuSans-Bold.ttf", 10),
]

# Printable ASCII, Latin-1 supplement and a few typographic extras.
CODEPOINTS = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) +
              [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x20AC, 0x2192])

# Pinned upstream source for the TTFs (fonts are not committed).
DEJAVU_URL = "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2"
DEJAVU_SHA256 = "fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7"


def fetch_dejavu(dest: Path) -> Path:
    import io
    import tarfile
    import urllib.request
    dest.mkdir(parents=True, exist_ok=True)
    marker = dest / "DejaVuSans.ttf"
    if marker.exists():
        return dest
    data = urllib.request.urlopen(DEJAVU_URL, timeout=60).read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != DEJAVU_SHA256:
        raise SystemExit(f"DejaVu archive checksum mismatch: {digest}")
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:bz2") as tf:
        for m in tf.getmembers():
            name = Path(m.name).name
            if name.endswith(".ttf") or name == "LICENSE":
                f = tf.extractfile(m)
                if f:
                    (dest / name).write_bytes(f.read())
    return dest


def render_font(ttf: Path, size: int):
    font = ImageFont.truetype(str(ttf), size, layout_engine=ImageFont.Layout.BASIC)
    ascent, descent = font.getmetrics()
    glyphs, bitmap = [], bytearray()
    for cp in CODEPOINTS:
        ch = chr(cp)
        # Skip codepoints the TTF lacks (Pillow would render .notdef).
        if cp != 0x20 and font.getmask(ch).getbbox() is None and not ch.isspace() and cp != 0xA0:
            continue
        advance = int(round(font.getlength(ch)))
        bbox = font.getbbox(ch, anchor="ls")  # relative to (pen x, baseline)
        x0, y0, x1, y1 = bbox
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        rows = []
        if w and h:
            im = Image.new("L", (w, h), 0)
            dr = ImageDraw.Draw(im)
            dr.fontmode = "1"
            dr.text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
            px = im.load()
            # Trim empty rows/cols so the bounding box is tight.
            cols = [x for x in range(w) if any(px[x, y] for y in range(h))]
            rws = [y for y in range(h) if any(px[x, y] for x in range(w))]
            if cols and rws:
                cx0, cx1, ry0, ry1 = cols[0], cols[-1] + 1, rws[0], rws[-1] + 1
                x0 += cx0
                y0 += ry0
                w, h = cx1 - cx0, ry1 - ry0
                stride = (w + 7) // 8
                for y in range(ry0, ry1):
                    row = bytearray(stride)
                    for x in range(cx0, cx1):
                        if px[x, y]:
                            row[(x - cx0) >> 3] |= 0x80 >> ((x - cx0) & 7)
                    rows.append(bytes(row))
            else:
                w = h = 0
        else:
            w = h = 0
        if not (0 <= w <= 255 and 0 <= h <= 255 and -128 <= x0 <= 127 and 0 <= advance <= 255):
            raise SystemExit(f"glyph U+{cp:04X} out of range in {ttf.name}@{size}")
        y_top = ascent + y0  # y0 is negative above the baseline
        if not -128 <= y_top <= 127:
            raise SystemExit(f"glyph U+{cp:04X} vertical offset out of range")
        glyphs.append((cp, w, h, x0, y_top, advance, len(bitmap)))
        for r in rows:
            bitmap += r
    cap = font.getbbox("H", anchor="ls")
    return {
        "ascent": ascent, "descent": descent, "line_height": ascent + descent,
        "cap_height": -cap[1], "glyphs": glyphs, "bitmap": bytes(bitmap),
    }


def glyph_record_bytes(g) -> bytes:
    cp, w, h, xo, yo, adv, off = g
    return (cp.to_bytes(2, "little") + bytes([w, h, xo & 0xFF, yo & 0xFF, adv, 0]) + off.to_bytes(4, "little"))


def emit(fonts: list[tuple[str, str, int, dict]], pillow_version: str) -> str:
    out = [
        "// GENERATED by tools/fontgen.py - do not edit.",
        f"// Rasterised from DejaVu fonts 2.37 with Pillow {pillow_version} (FreeType mono hinting).",
        "// DejaVu fonts: Bitstream Vera / Arev font license, see docs/LICENSES.md.",
        '#include "../core/font.hpp"',
        "",
        "namespace badge {",
        "namespace {",
    ]
    for ident, ttf, size, d in fonts:
        out.append(f"// {ttf} @ {size}px: {len(d['glyphs'])} glyphs, {len(d['bitmap'])} bitmap bytes")
        out.append(f"const Glyph {ident}_glyphs[] = {{")
        for cp, w, h, xo, yo, adv, off in d["glyphs"]:
            out.append(f"  {{0x{cp:04X}, {w}, {h}, {xo}, {yo}, {adv}, 0, {off}}},")
        out.append("};")
        out.append(f"const uint8_t {ident}_bitmap[] = {{")
        bm = d["bitmap"]
        for i in range(0, len(bm), 16):
            out.append("  " + ", ".join(f"0x{b:02X}" for b in bm[i:i + 16]) + ",")
        if not bm:
            out.append("  0x00,")
        out.append("};")
    out.append("}  // namespace")
    out.append("")
    out.append("namespace fonts {")
    for ident, ttf, size, d in fonts:
        crc = zlib.crc32(b"".join(glyph_record_bytes(g) for g in d["glyphs"]) + d["bitmap"]) & 0xFFFFFFFF
        out.append(
            f'const Font {ident} = {{"{ident}", {d["line_height"]}, {d["ascent"]}, {d["descent"]}, '
            f'{d["cap_height"]}, {len(d["glyphs"])}, {ident}_glyphs, {ident}_bitmap, '
            f'{len(d["bitmap"])}, 0x{crc:08X}u}};')
    out.append("const Font *const all[] = {" + ", ".join(f"&{i}" for i, *_ in fonts) + "};")
    out.append(f"const int count = {len(fonts)};")
    out.append("}  // namespace fonts")
    out.append("}  // namespace badge")
    out.append("")
    return "\n".join(out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fonts-dir", type=Path, default=Path("build/fonts/dejavu"))
    ap.add_argument("--fetch", action="store_true", help="download the pinned DejaVu release if missing")
    ap.add_argument("--out", type=Path, default=Path("firmware/generated/fonts.cpp"))
    ap.add_argument("--check", action="store_true", help="fail if --out differs from regenerated output")
    args = ap.parse_args(argv)
    if args.fetch:
        fetch_dejavu(args.fonts_dir)
    import PIL
    rendered = [(i, t, s, render_font(args.fonts_dir / t, s)) for i, t, s in FONTS]
    text = emit(rendered, PIL.__version__)
    if args.check:
        cur = args.out.read_text() if args.out.exists() else ""
        if cur != text:
            print(f"{args.out} is stale; rerun tools/fontgen.py", file=sys.stderr)
            return 1
        print(f"{args.out} up to date")
        return 0
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text)
    total = sum(len(d["bitmap"]) + 12 * len(d["glyphs"]) for *_, d in rendered)
    print(f"wrote {args.out} ({len(rendered)} fonts, ~{total} bytes of font data)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
