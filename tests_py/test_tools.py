"""Host-side tests for the asset tooling and the rendered output.

Run: python3 -m unittest discover -s tests_py -v   (after building host/)
BADGER_PREVIEW overrides the path to the badger_preview binary.
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

import assetpack  # noqa: E402
import badge_profile  # noqa: E402
import uf2  # noqa: E402
from render_previews import decode_qr  # noqa: E402

PREVIEW = Path(os.environ.get("BADGER_PREVIEW", ROOT / "build/host/badger_preview"))


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


@unittest.skipUnless(PREVIEW.exists(), "build host/ first")
class RenderedOutput(unittest.TestCase):
    PAYLOADS = [
        "https://example.com/dan",
        "https://example.org/contact/dan-brown?src=formnext2026&lang=en",
        "BEGIN:VCARD\\nVERSION:3.0\\nN:Brown;Dan;;;\\nFN:Dan Brown\\nTITLE:Engineer + Maker\\n"
        "EMAIL:dan@example.com\\nURL:https://example.com\\nEND:VCARD",
    ]

    def test_qr_decodes_from_final_bitmaps(self):
        for payload in self.PAYLOADS:
            want = payload.replace("\\n", "\n")
            for screen in ("card", "qr"):
                (im,) = render(screen, {"qr.payload": payload})
                self.assertEqual(im.size, (296, 128))
                for scale in (1, 3):
                    self.assertEqual(decode_qr(framed(im, scale)), [want], f"{screen} x{scale} {payload[:30]}")

    def test_unconfigured_qr_draws_no_symbol(self):
        for screen in ("card", "qr"):
            (im,) = render(screen, {"qr.payload": ""})
            self.assertEqual(decode_qr(framed(im, 3)), [])

    def test_field_table_matches_python(self):
        out = subprocess.run([str(PREVIEW), "--dump-fields"], capture_output=True, text=True, check=True).stdout
        cpp = {}
        for line in out.splitlines():
            key, typ, size, lo, hi = line.split()
            cpp[key] = (typ, int(size)) if typ == "str" else (typ, (int(lo), int(hi)))
        self.assertEqual(cpp, badge_profile.FIELD_LIMITS)

    def test_status_states_render_distinctly(self):
        seen = set()
        for batt in ("none", "usb", "invalid", "low", "0", "1", "2", "3", "4"):
            for gest in ("off", "on", "fault"):
                (im,) = render("badge", {}, ["--layout", "0", "--battery", batt, "--gesture", gest])
                seen.add(im.crop((296 - 56, 0, 296, 8)).tobytes())
        self.assertEqual(len(seen), 27)

    def test_both_layouts_and_every_screen_render(self):
        ims = render("badge,card,projects,qr,info,recovery", {})
        self.assertGreaterEqual(len(ims), 7)
        for im in ims:
            self.assertEqual(im.size, (296, 128))
            self.assertLess(im.histogram()[0], 296 * 128)  # not all black


class AssetPack(unittest.TestCase):
    def test_roundtrip_and_uf2(self):
        img = assetpack.placeholder()
        w, h, data = assetpack.pack_mono1(img)
        self.assertEqual(len(data), ((w + 7) // 8) * h)
        blob = assetpack.build_pack([(1, 1, w, h, data)])
        (e,) = assetpack.parse_pack(blob)
        self.assertEqual((e["width"], e["height"]), (w, h))
        pages = uf2.from_uf2(uf2.to_uf2(blob, assetpack.ASSET_REGION))
        self.assertEqual(min(pages), assetpack.ASSET_REGION)
        joined = b"".join(pages[a] for a in sorted(pages))
        self.assertEqual(joined[:len(blob)], blob)

    def test_corruption_rejected(self):
        blob = bytearray(assetpack.build_pack([(1, 1, *assetpack.pack_mono1(assetpack.placeholder()))]))
        for i in range(0, len(blob), 7):
            b = bytearray(blob)
            b[i] ^= 0x01
            with self.assertRaises(ValueError):
                assetpack.parse_pack(bytes(b))

    def test_oversize_bitmap_rejected(self):
        from PIL import Image
        with self.assertRaises(SystemExit):
            assetpack.pack_mono1(Image.new("1", (297, 10), 1))


class Profile(unittest.TestCase):
    def test_sample_profile_is_valid(self):
        pairs = dict(badge_profile.load(ROOT / "config/sample-profile.json"))
        self.assertEqual(pairs["name"], "Dan Brown")
        self.assertEqual(pairs["qr.payload"], "")  # unconfigured until the owner provides a destination

    def test_invalid_profiles_rejected(self):
        base = json.loads((ROOT / "config/sample-profile.json").read_text())
        bad = [
            ("profile", "name", "x" * 48),
            ("profile", "qr", {"payload": "http://plain.example"}),
            ("prefs", "refresh.speed", 9),
            ("prefs", "sleep.timeout_s", 5),
            ("prefs", "bogus", 1),
            ("profile", "title", "tab\there"),
            ("prefs", "battery.bar3_mv", 3650),     # not increasing
            ("prefs", "battery.low_mv", 3700),      # above one bar
            ("prefs", "gesture.rotation", 4),
            ("prefs", "gesture.cooldown_ms", 50),
        ]
        for section, key, value in bad:
            doc = json.loads(json.dumps(base))
            doc[section][key] = value
            with self.assertRaises(badge_profile.ProfileError, msg=f"{key}={value!r}"):
                badge_profile.flatten(doc)


@unittest.skipUnless((ROOT / "build/fonts/dejavu/DejaVuSans.ttf").exists(), "run tools/fontgen.py --fetch first")
class Fonts(unittest.TestCase):
    def test_generated_fonts_are_reproducible(self):
        r = subprocess.run([sys.executable, str(ROOT / "tools/fontgen.py"), "--fonts-dir",
                            str(ROOT / "build/fonts/dejavu"), "--out", str(ROOT / "firmware/generated/fonts.cpp"),
                            "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == "__main__":
    unittest.main()
