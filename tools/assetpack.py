#!/usr/bin/env python3
"""Build, inspect and package badge asset packs (format: firmware/core/assetpack.hpp).

  assetpack.py build --portrait portrait.png --out pack.bin [--cpp pack.cpp] [--uf2 pack.uf2] [--target T]
  assetpack.py inspect pack.bin [--target T]
  assetpack.py placeholder --out sample_portrait.png [--target T]   # generic, non-personal silhouette

--target is the BHIHKH_TARGET (default badger2040): it sets the panel size a
bitmap must fit, the placeholder size, and the asset region and UF2 family
of --uf2 (Badger 2040: 0x101E0000, RP2040; Badger 2350: 0x10FE0000,
RP2350 Arm with picotool's RP2350-E10 block first). The UF2 targets the
reserved asset region so a private portrait can be flashed separately from a
public firmware image.
"""
from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import uf2  # noqa: E402
from bhihkh_targets import DEFAULT, TARGETS  # noqa: E402

MAGIC = 0x4B504142
VERSION = 1
HEADER = 32
ENTRY = 24
ID_PORTRAIT = 1
FMT_MONO1 = 1
FMT_GRAY2 = 2
MAX_SIZE = 64 * 1024
# Badger 2040 values, the defaults (other targets: bhihkh_targets.py).
PANEL_W, PANEL_H = TARGETS[DEFAULT].display_w, TARGETS[DEFAULT].display_h
ASSET_REGION = TARGETS[DEFAULT].xip_base + TARGETS[DEFAULT].asset_off


def asset_region(target: str = DEFAULT) -> int:
    t = TARGETS[target]
    return t.xip_base + t.asset_off


def pack_mono1(img, target: str = DEFAULT) -> tuple[int, int, bytes]:
    """PIL image -> MONO1 bytes (row-major, MSB first, 1 = black)."""
    t = TARGETS[target]
    im = img.convert("1", dither=None) if img.mode != "1" else img
    w, h = im.size
    if not (0 < w <= t.display_w and 0 < h <= t.display_h):
        raise SystemExit(f"bitmap {w}x{h} exceeds the {t.display_w}x{t.display_h} panel ({t.board})")
    stride = (w + 7) // 8
    px = im.load()
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:  # black in PIL "1" mode
                out[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    return w, h, bytes(out)


def build_pack(entries: list[tuple[int, int, int, int, bytes]]) -> bytes:
    """entries: (id, format, width, height, data)."""
    data_start = HEADER + ENTRY * len(entries)
    blobs, table, off = [], b"", (data_start + 3) & ~3
    pad0 = off - data_start
    for eid, fmt, w, h, data in entries:
        table += struct.pack("<HBBHHIIII", eid, fmt, 0, w, h, off, len(data), zlib.crc32(data) & 0xFFFFFFFF, 0)
        blob = data + b"\x00" * ((-len(data)) % 4)
        blobs.append(blob)
        off += len(blob)
    body = table + b"\x00" * pad0 + b"".join(blobs)
    total = HEADER + len(body)
    if total > MAX_SIZE:
        raise SystemExit(f"asset pack {total} bytes exceeds {MAX_SIZE}")
    head = struct.pack("<IHHHHII", MAGIC, VERSION, HEADER, len(entries), 0, total, zlib.crc32(body) & 0xFFFFFFFF)
    head += struct.pack("<I", zlib.crc32(head) & 0xFFFFFFFF) + b"\x00" * 8
    assert len(head) == HEADER
    return head + body


def pack_gray2(img, target: str) -> tuple[int, int, bytes]:
    """Explicit opaque four-level image -> GRAY2; never auto-convert a photo."""
    if target != "badger2350":
        raise SystemExit("four-tone portraits require badger2350")
    t = TARGETS[target]
    w, h = img.size
    if not (0 < w <= t.display_w and 0 < h <= t.display_h):
        raise SystemExit("four-tone bitmap exceeds target panel")
    px = img.convert("RGBA").load()
    levels = {255: 0, 170: 1, 85: 2, 0: 3}
    stride = (w + 3) // 4
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a != 255 or r != g or r != b or r not in levels:
                raise SystemExit("four-tone input must be opaque neutral levels 0, 85, 170, 255")
            out[y * stride + x // 4] |= levels[r] << (6 - 2 * (x & 3))
    return w, h, bytes(out)


def parse_pack(blob: bytes, target: str = DEFAULT) -> list[dict]:
    """Validate like the firmware and return entry descriptions (raises ValueError)."""
    t = TARGETS[target]
    if len(blob) < HEADER:
        raise ValueError("too short")
    magic, ver, hsize, count, _flags, total, dcrc, hcrc = struct.unpack("<IHHHHIII", blob[:24])
    if magic != MAGIC:
        raise ValueError("bad magic")
    if zlib.crc32(blob[:20]) & 0xFFFFFFFF != hcrc:
        raise ValueError("bad header crc")
    if ver != VERSION or hsize != HEADER or any(blob[24:32]):
        raise ValueError("unsupported version")
    if count > 16 or total > MAX_SIZE or total > len(blob) or total < HEADER + ENTRY * count:
        raise ValueError("bad size")
    if zlib.crc32(blob[HEADER:total]) & 0xFFFFFFFF != dcrc:
        raise ValueError("bad data crc")
    out = []
    for i in range(count):
        eid, fmt, _r, w, h, off, ln, crc, _r2 = struct.unpack_from("<HBBHHIIII", blob, HEADER + i * ENTRY)
        if off < HEADER + ENTRY * count or off % 4 or off + ln > total:
            raise ValueError(f"entry {i}: bad offset")
        if fmt not in (FMT_MONO1, FMT_GRAY2) or (fmt == FMT_GRAY2 and target != "badger2350"):
            raise ValueError(f"entry {i}: unsupported format")
        stride = (w + (7 if fmt == FMT_MONO1 else 3)) // (8 if fmt == FMT_MONO1 else 4)
        if not (0 < w <= t.display_w and 0 < h <= t.display_h) or ln != stride * h:
            raise ValueError(f"entry {i}: bad bitmap geometry")
        if fmt == FMT_GRAY2 and w % 4:
            mask = (1 << (2 * (4 - w % 4))) - 1
            if any(blob[off + (y + 1) * stride - 1] & mask for y in range(h)):
                raise ValueError(f"entry {i}: bad bitmap padding")
        if zlib.crc32(blob[off:off + ln]) & 0xFFFFFFFF != crc:
            raise ValueError(f"entry {i}: bad crc")
        out.append({"id": eid, "format": fmt, "width": w, "height": h, "offset": off, "length": ln, "crc": crc})
    return out


def emit_cpp(blob: bytes, symbol: str) -> str:
    lines = ["// GENERATED by tools/assetpack.py - do not edit.", "#include <cstddef>", "#include <cstdint>",
             "namespace badge {", f"extern const uint8_t {symbol}[];", f"extern const size_t {symbol}_size;",
             f"alignas(4) const uint8_t {symbol}[] = {{"]
    for i in range(0, len(blob), 16):
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in blob[i:i + 16]) + ",")
    lines += ["};", f"const size_t {symbol}_size = {len(blob)};", "}  // namespace badge", ""]
    return "\n".join(lines)


def placeholder(w: int = 104, h: int = 128):
    """A plain head-and-shoulders outline (shapes only): clearly not a photo.
    Drawn for 128 rows; a taller frame centres the same figure."""
    from PIL import Image, ImageDraw
    im = Image.new("1", (w, h), 1)
    d = ImageDraw.Draw(im)
    cx = w // 2
    dy = (h - 128) // 2
    d.rectangle([0, 0, w - 1, h - 1], outline=0)
    d.ellipse([cx - 22, 18 + dy, cx + 22, 70 + dy], outline=0, width=2)
    d.chord([cx - 46, 78 + dy, cx + 46, 170 + dy], 180, 360, outline=0, width=2)
    d.line([cx - 45, 124 + dy, cx + 45, 124 + dy], fill=0, width=2)
    return im


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--portrait", type=Path)
    b.add_argument("--four-tone", action="store_true", help="explicit GRAY2 portrait (badger2350 only)")
    b.add_argument("--out", type=Path, required=True)
    b.add_argument("--cpp", type=Path)
    b.add_argument("--symbol", default="kBuiltinAssetPack")
    b.add_argument("--uf2", type=Path)
    b.add_argument("--address", type=lambda s: int(s, 0), help="UF2 address (default: the target's asset region)")
    i = sub.add_parser("inspect")
    i.add_argument("pack", type=Path)
    p = sub.add_parser("placeholder")
    p.add_argument("--out", type=Path, required=True)
    for s in (b, i, p):
        s.add_argument("--target", choices=sorted(TARGETS), default=DEFAULT, help=f"BHIHKH_TARGET (default {DEFAULT})")
    args = ap.parse_args(argv)
    t = TARGETS[args.target]

    if args.cmd == "placeholder":
        args.out.parent.mkdir(parents=True, exist_ok=True)
        placeholder(t.portrait_w, t.portrait_h).save(args.out)
        return 0
    if args.cmd == "inspect":
        try:
            for e in parse_pack(args.pack.read_bytes(), args.target):
                print(e)
        except ValueError as e:
            print(f"invalid: {e}", file=sys.stderr)
            return 1
        return 0

    from PIL import Image
    entries = []
    if args.four_tone and (args.target != "badger2350" or not args.portrait):
        raise SystemExit("--four-tone requires --target badger2350 and --portrait")
    if args.portrait:
        with Image.open(args.portrait) as image:
            w, h, data = (pack_gray2 if args.four_tone else pack_mono1)(image, args.target)
        entries.append((ID_PORTRAIT, FMT_GRAY2 if args.four_tone else FMT_MONO1, w, h, data))
    blob = build_pack(entries)
    parse_pack(blob, args.target)  # self-check
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    if args.cpp:
        text = emit_cpp(blob, args.symbol)
        if not args.cpp.exists() or args.cpp.read_text() != text:
            args.cpp.write_text(text)
    if args.uf2:
        address = asset_region(args.target) if args.address is None else args.address
        if not (t.xip_base <= address and address + len(blob) <= t.xip_base + t.flash_size):
            raise SystemExit(f"address outside the {t.flash_size // (1024 * 1024)} MiB flash window")
        args.uf2.write_bytes(uf2.to_uf2(blob, address, t.uf2_family, t.uf2_abs_block))
    return 0


if __name__ == "__main__":
    sys.exit(main())
