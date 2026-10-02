"""Badger 2350 (BHIHKH_TARGET=badger2350) rendered output and tooling.

The rendered-output tests use the badger_preview of the badger2350 host
build (scripts/run-host-tests.sh builds it in build/host-badger2350);
BADGER_PREVIEW_BADGER2350 overrides its path.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))

import assetpack  # noqa: E402
import badge_form as bf  # noqa: E402
import badge_profile  # noqa: E402
import uf2  # noqa: E402
from bhihkh_targets import TARGETS  # noqa: E402
from render_previews import decode_qr  # noqa: E402

PREVIEW = Path(os.environ.get("BADGER_PREVIEW_BADGER2350", ROOT / "build/host-badger2350/badger_preview"))
T = TARGETS["badger2350"]


def render(screen: str, sets: dict[str, str], extra=()):
    from PIL import Image
    with tempfile.TemporaryDirectory() as d:
        cmd = [str(PREVIEW), "--out", d, "--screens", screen, *extra]
        for k, v in sets.items():
            cmd += ["--set", f"{k}={v}"]
        out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.split()
        return [Image.open(p).convert("1").copy() for p in out]


def framed(im, scale=1):
    from PIL import Image
    f = Image.new("L", (im.width + 40, im.height + 40), 255)
    f.paste(im.convert("L"), (20, 20))
    return f.resize((f.width * scale, f.height * scale), Image.Resampling.NEAREST) if scale > 1 else f


@unittest.skipUnless(PREVIEW.exists(), "build host/ for badger2350 first (scripts/run-host-tests.sh)")
class RenderedOutput(unittest.TestCase):
    PAYLOADS = [
        "https://example.com/alex",
        "https://example.org/contact/alex-example?src=expo2026&lang=en",
        "BEGIN:VCARD\\nVERSION:3.0\\nN:Example;Alex;;;\\nFN:Alex Example\\nTITLE:Mechanical Engineer\\n"
        "EMAIL:alex@example.com\\nURL:https://example.com\\nEND:VCARD",
    ]

    def test_native_size_and_qr_decodes_from_final_bitmaps(self):
        for payload in self.PAYLOADS:
            want = payload.replace("\\n", "\n")
            for screen in ("card", "qr"):
                (im,) = render(screen, {"qr.payload": payload})
                self.assertEqual(im.size, (264, 176))
                for scale in (1, 3):
                    self.assertEqual(decode_qr(framed(im, scale)), [want], f"{screen} x{scale} {payload[:30]}")

    def test_qr_is_pure_black_and_white(self):
        (im,) = render("qr", {"qr.payload": self.PAYLOADS[0]})
        self.assertEqual(set(im.convert("L").getdata()) <= {0, 255}, True)

    def test_unconfigured_qr_draws_no_symbol(self):
        for screen in ("card", "qr"):
            (im,) = render(screen, {"qr.payload": ""})
            self.assertEqual(decode_qr(framed(im, 3)), [])
            self.assertEqual(im.crop((180, 12, 264, 176)).getextrema(), (255, 255))

    def test_sample_previews_fit_and_decode(self):
        import render_previews
        with tempfile.TemporaryDirectory() as d:
            rc = render_previews.main(["--preview", str(PREVIEW), "--out", d, "--qr", "https://example.com/alex",
                                       "--profile", str(ROOT / "config/sample-profile.json")])
            self.assertEqual(rc, 0)
            report = json.loads((Path(d) / "qr_report.json").read_text())
            self.assertTrue(report["screens"] and all(r["ok"] for r in report["screens"].values()), report)
            names = {p.stem for p in (Path(d) / "native").glob("*.png")}
            self.assertTrue({"badge_layoutA", "badge_layoutB", "card", "qr", "info", "recovery", "projects_1",
                             "project-qr_1", "index_1"} <= names, names)
            from PIL import Image
            for p in (Path(d) / "native").glob("*.png"):
                self.assertEqual(Image.open(p).size, (264, 176), p.name)

    def test_diagnostic_max_sample_renders_every_screen_and_decodes(self):
        import render_previews
        with tempfile.TemporaryDirectory() as d:
            rc = render_previews.main(["--preview", str(PREVIEW), "--out", d, "--diagnostic-max",
                                       "--preview-arg=--faults", "--preview-arg=--battery", "--preview-arg=low",
                                       "--preview-arg=--gesture", "--preview-arg=fault"])
            self.assertEqual(rc, 0)
            report = json.loads((Path(d) / "qr_report.json").read_text())
            self.assertTrue(all(r["ok"] for r in report["screens"].values()))
            pqr = {k: v for k, v in report["screens"].items() if k.startswith("project-qr")}
            self.assertEqual(len(pqr), badge_profile.MAX_PROJECTS)
            self.assertEqual(len({r["expected"] for r in pqr.values()}), badge_profile.MAX_PROJECTS)

    def test_fit_gate_rejects_cut_portfolio_text(self):
        import render_previews
        doc = json.loads((ROOT / "config/sample-profile.json").read_text())
        screens, fits = render_previews.fit_reports(PREVIEW, badge_profile.flatten(doc))
        self.assertEqual(render_previews.fit_problems(screens, fits, badge_profile.flatten(doc)), [])
        doc["profile"]["projects"][0]["body"] = "WWWW " * 39  # 195 bytes of the widest glyphs
        with tempfile.TemporaryDirectory() as d:
            prof = Path(d) / "p.json"
            prof.write_text(json.dumps(doc))
            with self.assertRaises(SystemExit) as cm:
                render_previews.main(["--preview", str(PREVIEW), "--out", d, "--profile", str(prof),
                                      "--screens", "projects"])
            self.assertIn("does not fit", str(cm.exception))

    def test_index_of_twelve(self):
        import render_previews
        with tempfile.TemporaryDirectory() as d:
            rc = render_previews.main(["--preview", str(PREVIEW), "--out", d, "--example-projects", "12",
                                       "--profile", str(ROOT / "config/sample-profile.json"),
                                       "--screens", "index,projects"])
            self.assertEqual(rc, 0)
            self.assertEqual(len(list((Path(d) / "native").glob("index_*.png"))), 12)


class AssetPack(unittest.TestCase):
    def test_placeholder_and_pack_for_the_badger2350(self):
        img = assetpack.placeholder(T.portrait_w, T.portrait_h)
        self.assertEqual(img.size, (104, 176))
        with tempfile.TemporaryDirectory() as d:
            ref = Path(d) / "ph.png"
            self.assertEqual(assetpack.main(["placeholder", "--target", "badger2350", "--out", str(ref)]), 0)
            from PIL import Image, ImageChops
            sample = Image.open(ROOT / "assets/sample/portrait_placeholder_badger2350.png").convert("L")
            self.assertIsNone(ImageChops.difference(sample, Image.open(ref).convert("L")).getbbox())
            w, h, data = assetpack.pack_mono1(img, "badger2350")
            blob = assetpack.build_pack([(1, 1, w, h, data)])
            self.assertEqual(assetpack.parse_pack(blob, "badger2350")[0]["height"], 176)
            with self.assertRaises(ValueError):  # too tall for the Badger 2040
                assetpack.parse_pack(blob)
            out = Path(d) / "a.bin"
            u = Path(d) / "a.uf2"
            self.assertEqual(assetpack.main(["build", "--target", "badger2350", "--portrait", str(ref),
                                             "--out", str(out), "--uf2", str(u)]), 0)
            raw = u.read_bytes()
            self.assertTrue(uf2.is_abs_block(raw[:512]))  # RP2350-E10 block first, as picotool writes
            pages = uf2.from_uf2(raw)
            self.assertEqual(min(pages), 0x10FE0000)
            self.assertTrue(all(0x10FE0000 <= a < 0x10FF0000 for a in pages))
            import struct
            fams = {struct.unpack("<I", raw[o + 28:o + 32])[0] for o in range(512, len(raw), 512)}
            self.assertEqual(fams, {uf2.RP2350_ARM_S_FAMILY_ID})

    def test_badger2040_pack_is_unchanged(self):
        # Defaults stay the Badger 2040's: same region, family, no E10 block.
        with tempfile.TemporaryDirectory() as d:
            out, u = Path(d) / "a.bin", Path(d) / "a.uf2"
            self.assertEqual(assetpack.main(["build", "--portrait", str(ROOT / "assets/sample/portrait_placeholder.png"),
                                             "--out", str(out), "--uf2", str(u)]), 0)
            raw = u.read_bytes()
            self.assertFalse(uf2.is_abs_block(raw[:512]))
            self.assertEqual(min(uf2.from_uf2(raw)), 0x101E0000)


class Form(unittest.TestCase):
    def test_template_for_the_badger2350(self):
        form = bf.load_form(bf.TEMPLATE, target="badger2350")
        self.assertEqual(form.target, "badger2350")
        # The template's sample silhouette is used at this badge's size.
        self.assertEqual(form.portrait.path.name, "portrait_placeholder_badger2350.png")
        self.assertEqual(bf.load_form(bf.TEMPLATE).portrait.path.name, "portrait_placeholder.png")
        self.assertEqual(bf.default_out(Path("local/badge.toml"), "badger2350").name, "badge-badger2350")
        self.assertEqual(bf.default_out(Path("local/badge.toml")).name, "badge")
        self.assertEqual(bf.placeholder("badger2350").name, "portrait_placeholder_badger2350.png")

    def test_photo_portrait_size_follows_the_target(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as d:
            Image.new("RGB", (600, 800), "white").save(Path(d) / "photo.jpg")
            text = bf.TEMPLATE.read_text(encoding="utf-8")
            text = text[:text.index("\n[portrait]\n")] + '\n[portrait]\nphoto = "photo.jpg"\n\n' + \
                text[text.index("\n[[projects]]\n") + 1:]
            f = Path(d) / "badge.toml"
            f.write_text(text, encoding="utf-8")
            classic = bf.load_form(f)
            neo = bf.load_form(f, target="badger2350")
            self.assertEqual(classic.portrait.settings["size"], [104, 128])
            self.assertEqual(neo.portrait.settings["size"], [104, 176])
            w, h = neo.portrait.settings["crop"][2:]
            self.assertAlmostEqual(w / h, 104 / 176, places=2)

    def test_processed_limits_follow_the_target(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as d:
            Image.new("1", (104, 176), 1).save(Path(d) / "tall.png")
            problems: list[str] = []
            bf._check_processed(problems, Path(d) / "tall.png", "badger2350")
            self.assertEqual(problems, [])
            bf._check_processed(problems, Path(d) / "tall.png")
            self.assertIn("at most 148x128", problems[0])


if __name__ == "__main__":
    unittest.main()
