"""Four-tone opt-in, exact format fixtures and previews from firmware frames."""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import assetpack
import badge_form as bf
import portrait
from render_previews import decode_qr

PREVIEW = Path(os.environ.get("BADGER_PREVIEW_BADGER2350", ROOT / "build/host-badger2350/badger_preview"))


class FourTone(unittest.TestCase):
    def test_exact_packed_values_and_rows(self):
        im = Image.new("L", (4, 2))
        im.putdata([255, 170, 85, 0, 0, 85, 170, 255])
        w, h, data = assetpack.pack_gray2(im, "badger2350")
        self.assertEqual((w, h, data), (4, 2, b"\x1b\xe4"))
        blob = assetpack.build_pack([(1, assetpack.FMT_GRAY2, w, h, data)])
        entries = assetpack.parse_pack(blob, "badger2350")
        self.assertEqual(entries[0]["length"], 2)
        self.assertEqual(entries[0]["format"], 2)
        self.assertEqual(blob[56:], b"\x1b\xe4\x00\x00")
        with self.assertRaisesRegex(ValueError, "unsupported format"):
            assetpack.parse_pack(blob, "badger2040")
        with self.assertRaises(SystemExit):
            assetpack.pack_gray2(im, "badger2040")

    def test_padding_geometry_unknown_and_corrupt_assets(self):
        good = assetpack.build_pack([(1, 2, 3, 1, b"\x18")])
        self.assertEqual(assetpack.parse_pack(good, "badger2350")[0]["width"], 3)
        for w, h, data in [(3, 1, b"\x1b"), (4, 2, b"\x1b"), (0, 1, b"\x00"),
                            (265, 1, bytes(67))]:
            with self.assertRaises(ValueError):
                assetpack.parse_pack(assetpack.build_pack([(1, 2, w, h, data)]), "badger2350")
        with self.assertRaisesRegex(ValueError, "unsupported format"):
            assetpack.parse_pack(assetpack.build_pack([(1, 99, 4, 1, b"\x1b")]), "badger2350")
        for off in (20, 35, 56):
            b = bytearray(good); b[off] ^= 1
            with self.assertRaises(ValueError):
                assetpack.parse_pack(bytes(b), "badger2350")
        b = bytearray(good); b[56] ^= 4
        struct.pack_into("<I", b, 16, zlib.crc32(b[32:])); struct.pack_into("<I", b, 20, zlib.crc32(b[:20]))
        with self.assertRaisesRegex(ValueError, "bad crc"):
            assetpack.parse_pack(bytes(b), "badger2350")
        with self.assertRaises(ValueError):
            assetpack.parse_pack(good[:-1], "badger2350")

    def test_processed_input_is_strict(self):
        for color in [(42, 42, 42, 255), (85, 84, 85, 255), (85, 85, 85, 254)]:
            with self.assertRaises(SystemExit):
                assetpack.pack_gray2(Image.new("RGBA", (4, 1), color), "badger2350")

    def test_quantization_boundaries(self):
        g = np.array([[0, 1 / 6 - 1e-8, 1 / 6, 0.5 - 1e-8, 0.5, 5 / 6 - 1e-8, 5 / 6, 1]])
        q = portrait.convert(g, "four-tone")
        self.assertEqual(q.tolist(), [[0, 0, 85, 85, 170, 170, 255, 255]])
        self.assertEqual(portrait.to_image(q).mode, "L")

    def test_mono_generation_unchanged(self):
        im = Image.new("1", (4, 2)); im.putdata([255, 0, 0, 255, 0, 255, 255, 0])
        self.assertEqual(assetpack.pack_mono1(im, "badger2350"), (4, 2, b"\x60\x90"))
        for target in ("badger2040", "badger2350"):
            self.assertEqual(assetpack.parse_pack(assetpack.build_pack([(1, 1, 4, 2, b"\x60\x90")]), target)[0]["format"], 1)

    def test_photo_cli_preserves_source_and_refuses_classic(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d); source = d / "source.png"; out = d / "portrait.png"
            Image.fromarray(np.tile(np.arange(256, dtype=np.uint8), (256, 1))).save(source)
            before = hashlib.sha256(source.read_bytes()).digest()
            cmd = [sys.executable, str(ROOT / "tools/portrait.py"), str(source), "--four-tone", "--out", str(out)]
            result = subprocess.run(cmd, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            subprocess.run(cmd + ["--target", "badger2350"], check=True, capture_output=True)
            with Image.open(out) as image:
                self.assertEqual(image.size, (104, 176))
                self.assertEqual(set(np.asarray(image).flat), {0, 85, 170, 255})
            self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)

    def test_form_opt_in_and_target_rejection(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d); source = d / "levels.png"
            im = Image.new("L", (104, 176)); im.putdata([255, 170, 85, 0] * (104 * 176 // 4)); im.save(source)
            text = 'form = 1\n[person]\nname = "Alex Example"\ntitle = "Engineer"\n[portrait]\nprocessed = "levels.png"\nfour_tone = true\n'
            form = bf.load_form(d / "badge.toml", text, "badger2350")
            self.assertTrue(form.portrait.four_tone)
            bf.prepare_portrait(form, d)
            with Image.open(d / "portrait.png") as image:
                self.assertEqual(set(np.asarray(image).flat), {0, 85, 170, 255})
            if PREVIEW.exists():
                (d / "badge.toml").write_text(text)
                prepared = bf.preview(form, d / "out")
                self.assertTrue(prepared["four_tone"])
                self.assertEqual(assetpack.parse_pack(prepared["pack"].read_bytes(), "badger2350")[0]["format"], 2)
            with self.assertRaises(bf.FormError):
                bf.load_form(d / "badge.toml", text, "badger2040")
            with self.assertRaises(bf.FormError):
                bf.load_form(d / "badge.toml", text.replace("four_tone = true", "four_tone = false"), "badger2350")
            photo_text = text.replace('processed = "levels.png"', 'photo = "levels.png"')
            form = bf.load_form(d / "badge.toml", photo_text, "badger2350")
            self.assertEqual(form.portrait.settings["method"], "four-tone")

    @unittest.skipUnless(PREVIEW.exists(), "2350 host build required")
    def test_firmware_preview_opt_in_and_qr_decode(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d); pack = d / "assets.bin"
            payload = "https://example.com/alex"
            pack.write_bytes(assetpack.build_pack([(1, 2, 104, 176, bytes([0x1b]) * 4576)]))
            cmd = [str(PREVIEW), "--pack", str(pack), "--out", str(d), "--set", f"qr.payload={payload}"]
            for layout in (0, 1):
                result = subprocess.run(cmd + ["--screens", "badge,card,qr,projects,index,info", "--layout", str(layout)],
                                        check=True, capture_output=True, text=True)
                for p in result.stdout.splitlines():
                    with Image.open(p) as source:
                        im = source.convert("L")
                    levels = set(np.asarray(im).flat)
                    if Path(p).stem.startswith("badge"):
                        self.assertEqual(levels, {0, 85, 170, 255})
                    else:
                        self.assertTrue(levels <= {0, 255})
                    if Path(p).stem in ("card", "qr"):
                        for scale in (1, 3):
                            self.assertEqual(decode_qr(im.resize((264 * scale, 176 * scale), Image.Resampling.NEAREST)), [payload])


if __name__ == "__main__":
    unittest.main()
