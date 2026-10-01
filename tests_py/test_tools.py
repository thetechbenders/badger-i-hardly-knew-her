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
            # Nothing QR-like: the right third of the card stays white.
            self.assertEqual(im.crop((200, 12, 296, 128)).getextrema(), (255, 255))

    def test_diagnostic_max_sample_renders_every_screen_and_decodes(self):
        import render_previews
        with tempfile.TemporaryDirectory() as d:
            rc = render_previews.main(["--preview", str(PREVIEW), "--out", d, "--diagnostic-max",
                                       "--preview-arg=--faults", "--preview-arg=--battery", "--preview-arg=low",
                                       "--preview-arg=--gesture", "--preview-arg=fault"])
            self.assertEqual(rc, 0)
            report = json.loads((Path(d) / "qr_report.json").read_text())
            self.assertTrue(all(r["ok"] for r in report["screens"].values()))
            names = {p.stem for p in (Path(d) / "native").glob("*.png")}
            self.assertTrue({"badge_layoutA", "badge_layoutB", "card", "qr", "info", "recovery",
                             "projects_1", "projects_4"} <= names, names)
            # Every linked project's QR decodes to its own link (no stale QR).
            pqr = {k: v for k, v in report["screens"].items() if k.startswith("project-qr")}
            self.assertEqual(len(pqr), badge_profile.MAX_PROJECTS)
            self.assertEqual(len({r["expected"] for r in pqr.values()}), badge_profile.MAX_PROJECTS)
        for key, value in render_previews.diagnostic_max_pairs():
            typ, cap = badge_profile.FIELD_LIMITS[key]
            if key.endswith(".type"):
                self.assertIn(value, badge_profile.CONTACT_TYPES, key)
                continue
            if key != "qr.payload" and not key.endswith(".label"):
                self.assertEqual(len(value.encode()), cap - 1, key)
            if key.endswith(".link"):
                self.assertTrue(badge_profile.valid_link(value), key)
                continue
            # Labelled on screen (15-byte contact labels hold "HOST DIAGNOSTIC").
            self.assertTrue(value.startswith("HOST DIAGNOSTIC") if key != "qr.payload"
                            else "host-diagnostic-sample" in value, key)

    def test_project_qr_decodes_its_own_link(self):
        import render_previews
        pairs = dict(badge_profile.load(ROOT / "config/sample-profile.json"))
        links = [pairs[f"project{i}.link"] for i in range(1, 13) if pairs.get(f"project{i}.title")]
        with tempfile.TemporaryDirectory() as d:
            rc = render_previews.main(["--preview", str(PREVIEW), "--out", d, "--screens", "projects,project-qr",
                                       "--profile", str(ROOT / "config/sample-profile.json")])
            self.assertEqual(rc, 0)
            report = json.loads((Path(d) / "qr_report.json").read_text())
            names = {p.stem for p in (Path(d) / "native").glob("*.png")}
        for n, link in enumerate(links, 1):
            self.assertIn(f"projects_{n}", names)
            key = f"project-qr_{n}"
            if link:
                self.assertEqual(report["screens"][key]["expected"], link)
                self.assertTrue(report["screens"][key]["ok"], key)
            else:  # e.g. the teaser project: no QR screen at all
                self.assertNotIn(key, names)
        self.assertIn("", links)  # the sample keeps one unlinked project

    def test_fit_gate_rejects_cut_portfolio_text(self):
        import render_previews
        doc = json.loads((ROOT / "config/sample-profile.json").read_text())
        fits = render_previews.fit_report(PREVIEW, badge_profile.flatten(doc))
        self.assertTrue(all(f[k] for f in fits for k in ("title", "tagline", "status", "body", "link")), fits)
        self.assertEqual(fits[-1]["name"], "BHIHKH!")
        doc["profile"]["projects"][0]["body"] = "Far too long for the page. " * 7
        with tempfile.TemporaryDirectory() as d:
            prof = Path(d) / "p.json"
            prof.write_text(json.dumps(doc))
            with self.assertRaises(SystemExit) as cm:
                render_previews.main(["--preview", str(PREVIEW), "--out", d, "--profile", str(prof),
                                      "--screens", "projects"])
            self.assertIn("does not fit", str(cm.exception))

    EMPTY = {f"project{i}.{f}": "" for i in range(1, 13) for f in badge_profile.PROJECT_FIELDS}

    def test_project_page_without_link_shows_no_qr(self):
        sets = dict(self.EMPTY, **{"project1.title": "Secret", "project1.banner": "TOP SECRET - COMING SOON"})
        (im,) = render("projects", sets)
        self.assertEqual(decode_qr(framed(im, 3)), [])
        self.assertEqual(render("project-qr", sets), [])  # no link: the QR screen is not offered

    def test_empty_and_single_project_lists(self):
        ims = render("projects,project-qr", self.EMPTY)
        self.assertEqual(len(ims), 1)  # one placeholder page, no QR
        self.assertEqual(decode_qr(framed(ims[0], 3)), [])
        one = dict(self.EMPTY, **{"project1.title": "Only", "project1.link": "https://example.com/only"})
        page, qr = render("projects,project-qr", one)
        self.assertEqual(decode_qr(framed(page, 3)), [])
        self.assertEqual(decode_qr(framed(qr, 3)), ["https://example.com/only"])


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
            ("prefs", "refresh.speed", 1.9),     # not truncated to 1
            ("prefs", "refresh.speed", "abc"),   # ProfileError, not ValueError
            ("prefs", "refresh.speed", None),
        ]
        for section, key, value in bad:
            doc = json.loads(json.dumps(base))
            doc[section][key] = value
            with self.assertRaises(badge_profile.ProfileError, msg=f"{key}={value!r}"):
                badge_profile.flatten(doc)

    def test_contact_types_and_projects_validated(self):
        base = json.loads((ROOT / "config/sample-profile.json").read_text())

        def check(mutate, ok):
            doc = json.loads(json.dumps(base))
            mutate(doc["profile"])
            if ok:
                return dict(badge_profile.flatten(doc))
            with self.assertRaises(badge_profile.ProfileError):
                badge_profile.flatten(doc)

        check(lambda p: p["contacts"][0].update(type="carrier-pigeon"), False)
        check(lambda p: p["contacts"][0].update(type="GitHub"), False)   # explicit, exact names
        d = check(lambda p: p["contacts"][0].update(type="github"), True)
        self.assertEqual(d["contact1.type"], "github")
        d = check(lambda p: p["contacts"][0].pop("type", None), True)    # old profiles: untyped is fine
        self.assertEqual(d["contact1.type"], "")
        for link in ("http://example.com", "https://", "https:///x", "https://a b", "ftp://x"):
            check(lambda p, l=link: p["projects"][0].update(link=l), False)
        check(lambda p: p["projects"][0].update(colour="red"), False)    # unknown field
        check(lambda p: p["projects"][0].update(title=""), False)        # content without a title
        check(lambda p: p["projects"][0].update(status="x" * 48), False)  # over the byte limit
        twelve = [{"title": f"P{i}"} for i in range(12)]
        d = check(lambda p: p.update(projects=twelve), True)
        self.assertEqual(d["project12.title"], "P11")
        check(lambda p: p.update(projects=twelve + [{"title": "P12"}]), False)
        d = check(lambda p: p.update(projects=[]), True)
        self.assertTrue(all(d[f"project{i}.title"] == "" for i in range(1, 13)))


class Icons(unittest.TestCase):
    def test_generated_icons_are_reproducible(self):
        r = subprocess.run([sys.executable, str(ROOT / "tools/iconsgen.py"), "--out",
                            str(ROOT / "firmware/generated/icons.cpp"), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_icon_licence_notices_present(self):
        d = ROOT / "third_party/simple-icons"
        for f in ("LICENSE.md", "DISCLAIMER.md", "README.md", "github.svg", "discord.svg"):
            self.assertTrue((d / f).exists(), f)
        self.assertIn("CC0", (d / "LICENSE.md").read_text())


@unittest.skipUnless((ROOT / "build/fonts/dejavu/DejaVuSans.ttf").exists(), "run tools/fontgen.py --fetch first")
class Fonts(unittest.TestCase):
    def test_generated_fonts_are_reproducible(self):
        r = subprocess.run([sys.executable, str(ROOT / "tools/fontgen.py"), "--fonts-dir",
                            str(ROOT / "build/fonts/dejavu"), "--out", str(ROOT / "firmware/generated/fonts.cpp"),
                            "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == "__main__":
    unittest.main()
