#!/usr/bin/env python3
"""Render every badge screen with the firmware renderer (host build) and
write native-resolution and nearest-neighbour enlarged PNGs, a contact
sheet, and a QR decode report.

  tools/render_previews.py --preview build/host/badger_preview --out previews \
      [--profile config/sample-profile.json] [--pack pack.bin] [--scale 3] \
      [--qr https://example.com/x] [--diagnostic-max] [--preview-arg=--faults ...]

--example-projects N pads the portfolio to N entries with labelled
placeholders ("Example project k") inserted before the last entry, so the
configured order is kept and the last entry (BHIHKH!) stays last. It shows
the project index at its maximum size; the placeholders are never content.

--diagnostic-max renders a HOST DIAGNOSTIC SAMPLE: every text field filled
to its byte limit with labelled filler (breakable, so titles wrap), all
contacts and projects used, and a long QR URL. It checks layout limits; it
is never a real profile.

QR verification decodes the final full-screen bitmap of the card and QR
screens (as sent to the panel) with zbar at native resolution and enlarged,
and checks the decoded text equals the configured payload.
"""
from __future__ import annotations

import argparse
import json
import re
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


def fit_report(preview: Path, pairs) -> list[dict]:
    """Per configured project: was each text part drawn without ellipsis?"""
    cmd = [str(preview), "--fit"]
    for k, v in pairs:
        cmd += ["--set", f"{k}={v.replace(chr(10), chr(92) + 'n')}"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode:
        raise SystemExit(f"badger_preview --fit failed: {res.stderr}")
    out = []
    keys = ("title", "tagline", "status", "body", "link", "body_px")
    pat = re.compile(r"project (\d+) " + " ".join(fr"{k}=(\d+)" for k in keys) + r" (.*)")
    for line in res.stdout.splitlines():
        m = pat.fullmatch(line)
        if not m:
            raise SystemExit(f"unexpected --fit line: {line!r}")
        out.append({"project": int(m[1]), **{k: int(m[i + 2]) for i, k in enumerate(keys)}, "name": m[8]})
    return out


def diagnostic_max_pairs() -> list[tuple[str, str]]:
    """Labelled worst-case content: each text field at its byte limit."""
    filler = "HOST DIAGNOSTIC SAMPLE Wide Wörds Überlong Titles Wrap WWW MMM "
    pairs = []
    types = [t for t in prof.CONTACT_TYPES if t]
    for key, (typ, lim) in prof.FIELD_LIMITS.items():
        if typ != "str" or key == "qr.payload":
            continue
        n = int("".join(ch for ch in key.split(".")[0] if ch.isdigit()) or 0)
        if key.endswith(".type"):  # cycle through every icon/type
            pairs.append((key, types[(n - 1) % len(types)]))
            continue
        if key.endswith(".link"):  # valid https URL at the byte limit
            base = f"https://github.com/host-diagnostic-sample/p{n}-"
            pairs.append((key, base + "x" * (lim - 1 - len(base))))
            continue
        if key.endswith(".banner") and n % 4 != 3:  # some teaser pages, not all
            continue
        out = ""
        while len((out + filler).encode()) < lim - 1:
            out += filler
        for ch in filler:  # top up to exactly lim - 1 bytes
            if len((out + ch).encode()) > lim - 1:
                break
            out += ch
        pairs.append((key, out.rstrip() if key.endswith(".label") else out))
    pairs.append(("qr.payload", "https://example.com/host-diagnostic-sample/" + "x" * 100))
    return pairs


def example_projects(pairs, n: int) -> list[tuple[str, str]]:
    """Pad the configured projects to n entries, placeholders before the last one."""
    d = dict(pairs)
    fields = prof.PROJECT_FIELDS
    entries = [{f: d.get(f"project{i}.{f}", "") for f in fields}
               for i in range(1, prof.MAX_PROJECTS + 1) if d.get(f"project{i}.title")]
    if not 0 < n <= prof.MAX_PROJECTS or n < len(entries):
        raise SystemExit(f"--example-projects {n}: need {len(entries)}..{prof.MAX_PROJECTS}")
    last = entries.pop() if entries else None
    k = len(entries)
    while len(entries) < n - (1 if last else 0):
        k += 1
        entries.append({**{f: "" for f in fields}, "title": f"Example project {k}",
                        "tagline": "Host preview placeholder entry"})
    if last:
        entries.append(last)
    out = [(key, v) for key, v in pairs if not key.startswith("project")]
    for i in range(1, prof.MAX_PROJECTS + 1):
        e = entries[i - 1] if i <= len(entries) else {}
        out += [(f"project{i}.{f}", e.get(f, "")) for f in fields]
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preview", type=Path, default=Path("build/host/badger_preview"))
    ap.add_argument("--out", type=Path, default=Path("build/previews"))
    ap.add_argument("--profile", type=Path)
    ap.add_argument("--pack", type=Path)
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--qr", help="override qr.payload (e.g. to exercise the QR screens)")
    ap.add_argument("--screens", default="badge,card,projects,project-qr,index,qr,info,recovery")
    ap.add_argument("--example-projects", type=int, metavar="N",
                    help="pad the portfolio to N labelled placeholder entries (index preview)")
    ap.add_argument("--diagnostic-max", action="store_true", help="labelled worst-case content (see above)")
    ap.add_argument("--preview-arg", action="append", default=[], help="extra badger_preview argument")
    args = ap.parse_args(argv)

    from PIL import Image, ImageDraw
    pairs = prof.load(args.profile) if args.profile else []
    if args.diagnostic_max:
        pairs = diagnostic_max_pairs()
    if args.example_projects:
        pairs = example_projects(pairs, args.example_projects)
    if args.qr is not None:
        pairs = [(k, v) for k, v in pairs if k != "qr.payload"] + [("qr.payload", args.qr)]
    payload = dict(pairs).get("qr.payload", "")

    native = args.out / "native"
    big = args.out / f"x{args.scale}"
    native.mkdir(parents=True, exist_ok=True)
    big.mkdir(parents=True, exist_ok=True)
    fits = fit_report(args.preview, pairs)
    cut = [f for f in fits if not all(f[k] for k in ("title", "tagline", "status", "body", "link"))]
    for f in fits:
        print(f"FIT project {f['project']}: {'complete' if f not in cut else 'CUT'} (body {f['body_px']} px) {f['name']}")
    if cut and not args.diagnostic_max:
        raise SystemExit(f"project text does not fit (ellipsized or dropped): {[f['name'] for f in cut]}")
    pbms = render(args.preview, native, pairs, args.pack, args.screens, args.preview_arg)
    report = {"payload": payload, "screens": {}, "fit": fits}
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
        expected = payload
        if pbm.stem.startswith("project-qr"):
            # n-th configured project (1-based in the file name) -> its link
            titles = [i for i in range(1, 13) if dict(pairs).get(f"project{i}.title")]
            n = int(pbm.stem.split("_")[1]) if "_" in pbm.stem else 1
            expected = dict(pairs).get(f"project{titles[n - 1]}.link", "") if titles else ""
        if pbm.stem in ("card", "qr") or pbm.stem.startswith("project-qr"):
            # zbar needs some margin around the panel image.
            framed = Image.new("L", (296 + 40, 128 + 40), 255)
            framed.paste(im.convert("L"), (20, 20))
            res = {"native": decode_qr(framed),
                   f"x{args.scale}": decode_qr(framed.resize((framed.width * args.scale, framed.height * args.scale),
                                                             Image.Resampling.NEAREST))}
            ok = bool(expected) and all(r == [expected] for r in res.values())
            report["screens"][pbm.stem] = {"expected": expected, "decoded": res, "ok": ok if expected else None}

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

    # Status-area states (top-right of the badge), enlarged 6x for review.
    states = [("4 bars", "4", "off"), ("3 bars", "3", "off"), ("2 bars", "2", "off"), ("1 bar", "1", "off"),
              ("0 bars", "0", "off"), ("LOW", "low", "off"), ("invalid", "invalid", "off"), ("USB", "usb", "off"),
              ("gesture on", "3", "on"), ("gesture fault", "3", "fault")]
    crops = []
    for label, batt, gest in states:
        (p,) = render(args.preview, native, pairs, args.pack, "badge", ["--layout", "0", "--battery", batt,
                                                                       "--gesture", gest, "--suffix", "_status"])
        im = Image.open(p).convert("L")
        p.unlink()
        crops.append((label, im.crop((296 - 60, 0, 296, 12)).resize((60 * 6, 12 * 6), Image.Resampling.NEAREST)))
    ss = Image.new("L", (60 * 6 + 140, len(crops) * (12 * 6 + 8) + 8), 235)
    d2 = ImageDraw.Draw(ss)
    for i, (label, im) in enumerate(crops):
        y0 = 8 + i * (12 * 6 + 8)
        d2.text((8, y0 + 30), label, fill=0)
        ss.paste(im, (132, y0))
    ss.save(args.out / "status_states_x6.png")

    failed = [s for s, r in report["screens"].items() if r["ok"] is False]
    for s, r in report["screens"].items():
        status = "not configured (nothing drawn)" if r["ok"] is None else ("DECODED OK" if r["ok"] else "FAILED")
        print(f"QR {s:5s}: {status}")
    print(f"wrote {len(pngs)} screens to {args.out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
