#!/usr/bin/env python3
"""Render every badge screen with the firmware renderer (host build) and
write native-resolution and nearest-neighbour enlarged PNGs, a contact
sheet, and a QR decode report.

  tools/render_previews.py --preview build/host/badger_preview --out previews \
      [--profile config/sample-profile.json] [--pack pack.bin] [--scale 3] \
      [--qr https://example.com/x]

QR verification decodes the final full-screen bitmap of the card and QR
screens (as sent to the panel) with zbar at native resolution and enlarged,
and checks the decoded text equals the configured payload.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import badge_profile as prof  # noqa: E402


def decode_qr(img) -> list[str]:
    """Decode QR codes with zbar (pyzbar, else the zbarimg CLI)."""
    try:
        from pyzbar import pyzbar
        return [r.data.decode("utf-8") for r in pyzbar.decode(img.convert("L"))
                if r.type == "QRCODE"]
    except ImportError:
        import tempfile
        with tempfile.NamedTemporaryFile(suffix=".png") as f:
            img.convert("L").save(f.name)
            p = subprocess.run(["zbarimg", "--quiet", "--raw", "-Sdisable", "-Sqrcode.enable", f.name],
                               capture_output=True, text=True)
            return [p.stdout.rstrip("\n")] if p.returncode == 0 and p.stdout else []


def render(preview: Path, out: Path, pairs, pack: Path | None, screens: str, extra=()) -> list[Path]:
    cmd = [str(preview), "--out", str(out), "--screens", screens]
    if pack:
        cmd += ["--pack", str(pack)]
    for k, v in pairs:
        cmd += ["--set", f"{k}={v.replace(chr(10), chr(92) + 'n')}"]
    cmd += list(extra)
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode:
        raise SystemExit(f"badger_preview failed: {res.stderr}")
    if res.stderr:
        print(res.stderr.strip(), file=sys.stderr)
    return [Path(line) for line in res.stdout.split()]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preview", type=Path, default=Path("build/host/badger_preview"))
    ap.add_argument("--out", type=Path, default=Path("build/previews"))
    ap.add_argument("--profile", type=Path)
    ap.add_argument("--pack", type=Path)
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--qr", help="override qr.payload (e.g. to exercise the QR screens)")
    ap.add_argument("--screens", default="badge,card,projects,qr,info,recovery")
    args = ap.parse_args(argv)

    from PIL import Image, ImageDraw
    pairs = prof.load(args.profile) if args.profile else []
    if args.qr is not None:
        pairs = [(k, v) for k, v in pairs if k != "qr.payload"] + [("qr.payload", args.qr)]
    payload = dict(pairs).get("qr.payload", "")

    native = args.out / "native"
    big = args.out / f"x{args.scale}"
    native.mkdir(parents=True, exist_ok=True)
    big.mkdir(parents=True, exist_ok=True)
    pbms = render(args.preview, native, pairs, args.pack, args.screens)
    report = {"payload": payload, "screens": {}}
    pngs = []
    for pbm in pbms:
        im = Image.open(pbm).convert("1")
        if im.size != (296, 128):
            raise SystemExit(f"{pbm}: unexpected size {im.size}")
        png = native / (pbm.stem + ".png")
        im.save(png)
        pbm.unlink()
        up = im.resize((296 * args.scale, 128 * args.scale), Image.Resampling.NEAREST)
        up.save(big / png.name)
        pngs.append((png.stem, im))
        if pbm.stem in ("card", "qr"):
            # zbar needs some margin around the panel image.
            framed = Image.new("L", (296 + 40, 128 + 40), 255)
            framed.paste(im.convert("L"), (20, 20))
            res = {"native": decode_qr(framed),
                   f"x{args.scale}": decode_qr(framed.resize((framed.width * args.scale, framed.height * args.scale),
                                                             Image.Resampling.NEAREST))}
            ok = bool(payload) and all(r == [payload] for r in res.values())
            report["screens"][pbm.stem] = {"decoded": res, "ok": ok if payload else None}

    # Contact sheet: every screen at the enlarged size, labelled.
    pad, label = 12, 16
    sheet = Image.new("L", (296 * args.scale + 2 * pad, len(pngs) * (128 * args.scale + label + pad) + pad), 235)
    d = ImageDraw.Draw(sheet)
    y = pad
    for name, im in pngs:
        d.text((pad, y), name, fill=0)
        y += label
        sheet.paste(im.convert("L").resize((296 * args.scale, 128 * args.scale), Image.Resampling.NEAREST), (pad, y))
        y += 128 * args.scale + pad
    sheet.save(args.out / "contact_sheet.png")
    (args.out / "qr_report.json").write_text(json.dumps(report, indent=2) + "\n")

    failed = [s for s, r in report["screens"].items() if r["ok"] is False]
    for s, r in report["screens"].items():
        status = "not configured (placeholder shown)" if r["ok"] is None else ("DECODED OK" if r["ok"] else "FAILED")
        print(f"QR {s:5s}: {status}")
    print(f"wrote {len(pngs)} screens to {args.out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
