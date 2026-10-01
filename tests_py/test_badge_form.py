"""Tests for the fill-in personalisation form (tools/badge_form.py).

Run: python3 -m unittest discover -s tests_py -v   (render checks need build/host)
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import badge_form as bf  # noqa: E402
import badge_profile  # noqa: E402

PREVIEW = Path(os.environ.get("BADGER_PREVIEW", ROOT / "build/host/badger_preview"))
SAMPLE_JSON = ROOT / "config/sample-profile.json"
PLACEHOLDER = ROOT / "assets/sample/portrait_placeholder.png"

BASE = f'''form = 1
[person]
name = "Alex Example"
title = "Engineer"
[portrait]
processed = "{PLACEHOLDER.as_posix()}"
'''


CANONICAL = "https://github.com/thetechbenders/badger-i-hardly-knew-her"


def public_destinations(text: str) -> set[str]:
    """Links and e-mail addresses that are neither example.com/.org nor
    this repository: public samples must use fictional destinations only."""
    import re
    found = set(re.findall(r"https?://[^\s\"'<>)]+", text)) | set(re.findall(r"[\w.+-]+@[\w-]+(?:\.[\w-]+)+", text))
    return {f for f in found if not re.search(r"(^|[/@.])example\.(com|org)\b", f) and not f.startswith(CANONICAL)}


def sample_doc() -> dict:
    """The public sample profile (fictional) with its QR configured, so the
    caption has a code to describe (the form refuses a caption without one)."""
    doc = json.loads(SAMPLE_JSON.read_text(encoding="utf-8"))
    p = doc["profile"]
    p["qr"]["payload"] = "https://example.com/alex"
    return doc


class FormCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, text: str, name="badge.toml", raw: bytes | None = None) -> Path:
        p = self.dir / name
        p.write_bytes(raw if raw is not None else text.encode("utf-8"))
        return p

    def load(self, text: str, **kw) -> bf.Form:
        return bf.load_form(self.write(text, **kw))

    def problems(self, text: str) -> str:
        with self.assertRaises(bf.FormError) as cm:
            self.load(text)
        return str(cm.exception)

    def pairs(self, text: str) -> dict:
        return dict(badge_profile.flatten(self.load(text).doc))


class Parsing(FormCase):
    def test_template_is_valid_generic_and_ordered(self):
        form = bf.load_form(bf.TEMPLATE)
        p = form.doc["profile"]
        self.assertEqual(p["name"], "Alex Example")
        self.assertEqual(public_destinations(bf.TEMPLATE.read_text(encoding="utf-8")), set())
        self.assertEqual([x["title"] for x in p["projects"]],
                         ["Example Printer Mod", "Secret Project", "Weather Station"])
        teaser = p["projects"][1]
        self.assertEqual(teaser, {"title": "Secret Project", "banner": "TOP SECRET - COMING SOON"})
        self.assertEqual(p["projects"][0]["body"],
                         "Printable duct with a 5015 blower.\nMeasured 6 dB quieter at the same cooling.")
        self.assertNotIn("\n", p["projects"][2]["body"])  # backslash continuation joins lines
        self.assertEqual(p["contacts"][2], {"label": "", "value": "alex-example", "type": "github"})
        self.assertEqual(form.doc["prefs"], {"layout": 0, "refresh.speed": 1, "sleep.timeout_s": 120})

    def test_public_samples_use_fictional_destinations(self):
        for path in (SAMPLE_JSON, bf.TEMPLATE):
            self.assertEqual(public_destinations(path.read_text(encoding="utf-8")), set(), path)
        report = ROOT / "docs/previews/qr_report_example.json"
        self.assertEqual(public_destinations(report.read_text(encoding="utf-8")), set())

    def test_toml_profile_loads_through_badge_profile(self):
        pairs = badge_profile.load(bf.TEMPLATE)
        self.assertEqual(pairs, badge_profile.flatten(bf.load_doc(bf.TEMPLATE)))

    def test_equivalent_form_and_json_give_the_same_pairs(self):
        doc = {"format": 1, "profile": {
            "name": "Zoë Ørsted", "title": "Ingénieure · R&D", "affiliation": "Example Lab",
            "interests": "e-paper · sensors → firmware", "event": "Expo 2026",
            "contacts": [{"label": "Mail", "value": "zoe@example.com", "type": "email"},
                         {"label": "", "value": "zoe-o", "type": "github"},
                         {"label": "", "value": "zoe.o", "type": "discord"}],
            "qr": {"payload": "https://example.com/zoë", "caption": "Scan me"},
            "projects": [{"title": "Zeta", "body": "Line one\nLine two – done €5", "link": "https://github.com/z/z"},
                         {"title": "Alpha", "banner": "SOON"}]},
            "prefs": {"layout": 1, "wake.selects_screen": False}}
        form = f'''form = 1
[person]
name = "Zoë Ørsted"
title = "Ingénieure · R&D"
affiliation = "Example Lab"
event = "Expo 2026"
interests = ["e-paper", "sensors → firmware"]
[[contacts]]
type = "email"
label = "Mail"
value = "zoe@example.com"
[[contacts]]
type = "github"
value = "zoe-o"
[[contacts]]
type = "discord"
value = "zoe.o"
[qr]
show = "link"
link = "https://example.com/zoë"
caption = "Scan me"
[portrait]
processed = "{PLACEHOLDER.as_posix()}"
[[projects]]
title = "Zeta"
description = """
Line one
Line two – done €5
"""
link = "https://github.com/z/z"
[[projects]]
title = "Alpha"
banner = "SOON"
[preferences]
layout = 1
wake.selects_screen = false
'''
        self.assertEqual(badge_profile.flatten(self.load(form).doc), badge_profile.flatten(doc))

    def test_export_round_trip_of_the_sample_json(self):
        doc = sample_doc()
        text = bf.export_form(doc, [f'processed = "{PLACEHOLDER.as_posix()}"'], "sample-profile.json")
        self.assertEqual(badge_profile.flatten(self.load(text).doc), badge_profile.flatten(doc))

    def test_export_keeps_an_unused_caption_as_a_comment(self):
        doc = json.loads(SAMPLE_JSON.read_text(encoding="utf-8"))  # caption, no QR payload
        text = bf.export_form(doc, [f'processed = "{PLACEHOLDER.as_posix()}"'], "sample-profile.json")
        self.assertIn('# caption = "Scan for my website"', text)
        pairs = dict(badge_profile.flatten(self.load(text).doc))
        want = dict(badge_profile.flatten(doc))
        self.assertEqual({k: v for k, v in pairs.items() if k != "qr.caption"},
                         {k: v for k, v in want.items() if k != "qr.caption"})
        self.assertEqual(pairs["qr.caption"], "")  # identical screens: a caption without a code is never drawn

    def test_windows_editors_crlf_and_bom(self):
        text = (BASE + '[[projects]]\ntitle = "P"\ndescription = """\none\ntwo\n"""\n').replace("\n", "\r\n")
        form = self.load("", raw=b"\xef\xbb\xbf" + text.encode())
        self.assertEqual(form.doc["profile"]["projects"][0]["body"], "one\ntwo")

    def test_blank_optional_fields_disappear(self):
        p = self.pairs(BASE + '''
[[contacts]]
type = "email"
label = ""
value = ""
[[contacts]]
type = "phone"
label = "Phone"
value = "+1 555 0100"
[qr]
show = "none"
caption = ""
[[projects]]
title = ""
[[projects]]
title = "Only a title"
tagline = ""
status = ""
link = ""
''')
        self.assertEqual(p["affiliation"], "")
        self.assertEqual(p["interests"], "")
        self.assertEqual(p["contact1.value"], "+1 555 0100")  # blank block left out, not a gap
        self.assertEqual(p["contact2.value"], "")
        self.assertEqual(p["project1.title"], "Only a title")
        self.assertEqual([p[f"project1.{f}"] for f in ("tagline", "status", "link", "body", "banner")], [""] * 5)
        self.assertEqual(p["project2.title"], "")
        self.assertEqual(p["qr.payload"], "")

    def test_blank_and_omitted_are_the_same_profile(self):
        omitted = self.pairs(BASE + '[[projects]]\ntitle = "X"\n')
        blank = self.pairs(BASE.replace('title = "Engineer"', 'title = "Engineer"\naffiliation = ""\nevent = ""')
                           + '[qr]\nshow = "none"\n[[projects]]\ntitle = "X"\nstatus = ""\nlink = ""\n')
        self.assertEqual(omitted, blank)

    def test_project_order_is_kept(self):
        names = ["Zulu", "alpha", "Mike", "Last on purpose"]
        p = self.pairs(BASE + "".join(f'[[projects]]\ntitle = "{n}"\n' for n in names))
        self.assertEqual([p[f"project{i}.title"] for i in range(1, 5)], names)

    def test_exact_limits_are_kept_whole(self):
        name = "é" * 23 + "x"  # 47 bytes: the limit, accepted and never shortened
        p = self.pairs(BASE.replace('"Alex Example"', f'"{name}"'))
        self.assertEqual(p["name"], name)
        msg = self.problems(BASE.replace('"Alex Example"', f'"{name}y"'))
        self.assertIn("person.name: 48 bytes, the limit is 47: shorten it by 1 bytes", msg)

    def test_vcard_inline_file_and_generated(self):
        vc = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Alex Example\r\nEND:VCARD\r\n"
        (self.dir / "me.vcf").write_bytes(vc.encode())
        p = self.pairs(BASE + '[qr]\nshow = "vcard"\nvcard_file = "me.vcf"\n')
        self.assertEqual(p["qr.payload"], "BEGIN:VCARD\nVERSION:3.0\nFN:Alex Example\nEND:VCARD")
        p2 = self.pairs(BASE + '[qr]\nshow = "vcard"\nvcard = """\nBEGIN:VCARD\nVERSION:3.0\n'
                               'FN:Alex Example\nEND:VCARD\n"""\n')
        self.assertEqual(p["qr.payload"], p2["qr.payload"])
        p3 = self.pairs(BASE + '[[contacts]]\ntype = "email"\nvalue = "a@example.com"\n'
                               '[[contacts]]\ntype = "github"\nvalue = "octocat"\n[qr]\nshow = "vcard-from-contacts"\n')
        self.assertEqual(p3["qr.payload"].split("\n"),
                         ["BEGIN:VCARD", "VERSION:3.0", "N:Example;Alex;;;", "FN:Alex Example", "TITLE:Engineer",
                          "EMAIL:a@example.com", "URL:https://github.com/octocat", "END:VCARD"])


class Rejections(FormCase):
    def assertProblem(self, text: str, *needles: str):
        msg = self.problems(text)
        for n in needles:
            self.assertIn(n, msg)

    def test_syntax_error_names_the_line(self):
        self.assertProblem(BASE + 'oops = "unterminated\n', "line 7")

    def test_unknown_fields_with_suggestions(self):
        self.assertProblem(BASE + '[[projects]]\ntitle = "P"\ndesciption = "x"\n',
                           'projects #1 (P): unknown field "desciption". Did you mean "description"?')
        self.assertProblem(BASE + '[[projects]]\ntitle = "P"\nbody = "x"\n', 'unknown field "body"')
        self.assertProblem(BASE + '[persn]\n', 'top level: unknown field "persn". Did you mean "person"?')
        self.assertProblem(BASE + '[preferences]\nlayuot = 1\n', 'preferences.layuot: unknown setting. Did you mean "layout"?')
        self.assertProblem(BASE.replace("form = 1", "form = 2"), "expected `form = 1`")

    def test_wrong_types(self):
        self.assertProblem(BASE.replace('"Engineer"', "5"), "person.title: expected text in quotes, got a number 5")
        self.assertProblem(BASE + "[preferences]\nlayout = true\n", "preferences.layout: expected a whole number 0..1")
        self.assertProblem(BASE + "[preferences]\nwake.selects_screen = 1\n", "expected true or false")
        self.assertProblem(BASE + "[preferences]\nsleep.timeout_s = 5\n", "at least 15")
        self.assertProblem(BASE + "[preferences]\nlayout = 2\n", "preferences.layout: 2 is outside 0..1")
        self.assertProblem(BASE.replace("[person]", 'contacts = "x"\n[person]'),
                           "contacts: write each entry as a [[contacts]] block")

    def test_required_fields(self):
        self.assertProblem(BASE.replace('name = "Alex Example"', 'name = ""'), "person.name: required")
        self.assertProblem(BASE + '[[projects]]\ntagline = "no title"\n', "projects #1.title: required")
        self.assertProblem(BASE + '[[contacts]]\ntype = "email"\nlabel = "Email"\nvalue = ""\n',
                           "contacts #1 (Email).value: empty")

    def test_contact_types_and_values(self):
        c = BASE + '[[contacts]]\ntype = "{}"\nvalue = "{}"\n'
        self.assertProblem(c.format("twitter", "x"), "'twitter' is not a contact type")
        self.assertProblem(c.format("github", "https://github.com/octocat"), "write only the GitHub user name")
        self.assertProblem(c.format("github", "bad--name"), "is not a GitHub user name")
        self.assertProblem(c.format("discord", "@alex.example"), "without the @")
        self.assertProblem(c.format("email", "not-an-email"), "is not an email address")
        self.assertProblem(c.format("phone", "call me"), "is not a phone number")
        for t, v in (("github", "octo-cat"), ("discord", "alex.example_1"), ("phone", "+1 (555) 010-0"),
                     ("web", "example.com/me"), ("text", "Booth 4.2")):
            self.load(c.format(t, v))

    def test_too_many_contacts_and_projects(self):
        c = '[[contacts]]\ntype = "text"\nvalue = "x"\n'
        self.assertProblem(BASE + c * 7, "contacts: 7 [[contacts]] blocks, the badge holds at most 6; remove 1")
        p = '[[projects]]\ntitle = "P"\n'
        self.assertProblem(BASE + p * 13, "projects: 13 [[projects]] blocks, the badge holds at most 12; remove 1")
        self.load(BASE + p * 12)

    def test_links_and_qr_choices(self):
        self.assertProblem(BASE + '[[projects]]\ntitle = "P"\nlink = "http://example.com"\n',
                           "projects #1 (P).link: 'http://example.com' must be an https:// address")
        self.assertProblem(BASE + '[[projects]]\ntitle = "P"\nlink = "https://exa mple.com"\n', "without spaces")
        self.assertProblem(BASE + '[qr]\nshow = "link"\nlink = "ftp://x"\n', "qr.link: 'ftp://x' must be an https://")
        self.assertProblem(BASE + '[qr]\nshow = "link"\n', "qr.link: required")
        self.assertProblem(BASE + '[qr]\nshow = "website"\n', "qr.show: 'website' is not a choice")
        self.assertProblem(BASE + '[qr]\nshow = "none"\ncaption = "Scan"\n', "qr.caption: there is no QR code")
        self.assertProblem(BASE + '[qr]\nshow = "none"\nlink = "https://a.b"\n', 'qr.link: set, but qr.show is "none"')
        self.assertProblem(BASE + '[qr]\nshow = "vcard"\nvcard = "hello"\n', "starts with a BEGIN:VCARD line")
        self.assertProblem(BASE + '[qr]\nshow = "vcard"\nvcard_file = "nope.vcf"\n', "nope.vcf not found")
        self.assertProblem(BASE + f'[qr]\nshow = "link"\nlink = "https://example.com/{"x" * 400}"\n',
                           "qr.link: 420 bytes, the limit is 383")

    def test_text_the_badge_cannot_draw(self):
        self.assertProblem(BASE.replace("Alex Example", "Łukasz 🙂"),
                           "person.name: the badge font cannot draw 'Ł' (U+0141), '\U0001f642' (U+1F642)")
        self.assertProblem(BASE.replace('"Engineer"', '"""Engi\nneer"""'), "person.title: must be one line")
        self.assertProblem(BASE.replace('"Engineer"', '"Engi\\tneer"'), "control character U+0009")
        self.assertProblem(BASE.replace('title = "Engineer"', 'title = "Engineer"\ninterests = ["ok", "Łódź"]'),
                           "person.interests.item 2: the badge font cannot draw 'Ł' (U+0141)")

    def test_problems_are_reported_together(self):
        msg = self.problems(BASE.replace('name = "Alex Example"', 'name = ""')
                            + '[[projects]]\ntitle = "P"\nlink = "http://x"\n[preferences]\nlayout = 9\n')
        self.assertEqual(len(msg.splitlines()), 3, msg)


class Portraits(FormCase):
    def photo(self, size=(600, 800), name="photo.jpg") -> Path:
        from PIL import Image
        im = Image.linear_gradient("L").resize(size).convert("RGB")
        p = self.dir / name
        im.save(p, quality=95)
        return p

    def form_with(self, portrait: str) -> str:
        return BASE.split("[portrait]")[0] + "[portrait]\n" + portrait + "\n"

    def test_photo_relative_to_form_is_converted_and_left_intact(self):
        src = self.photo()
        before = hashlib.sha256(src.read_bytes()).hexdigest()
        sub = self.dir / "forms"
        sub.mkdir()
        form = bf.load_form(self.write(self.form_with('photo = "../photo.jpg"\nmethod = "floyd"'), name="forms/b.toml"))
        self.assertEqual(form.portrait.path.resolve(), src.resolve())
        self.assertEqual(form.portrait.settings["crop"], [0, 31, 600, 738])  # largest centred 104:128 area
        out = self.dir / "out"
        out.mkdir()
        dest = bf.prepare_portrait(form, out)
        from PIL import Image
        with Image.open(dest) as im:
            self.assertEqual((im.mode, im.size), ("1", (104, 128)))
        self.assertTrue((out / "portrait-methods_x3.png").exists())
        self.assertEqual(hashlib.sha256(src.read_bytes()).hexdigest(), before)

    def test_existing_portrait_settings_file(self):
        self.photo((1254, 1254))
        (self.dir / "portrait.json").write_text(json.dumps(
            {"crop": [250, 10, 800, 985], "size": [104, 128], "white_pct": 14, "black_pct": 2,
             "gamma": 0.6, "sharpen": 1.0, "method": "atkinson"}))
        form = self.load(self.form_with('photo = "photo.jpg"\nsettings = "portrait.json"'))
        self.assertEqual(form.portrait.settings["crop"], [250, 10, 800, 985])
        self.assertEqual(form.portrait.settings["gamma"], 0.6)

    def test_unsuitable_portraits_rejected(self):
        from PIL import Image
        self.photo()
        r = self.form_with
        self.assertIn("portrait: missing", str(self.problems(BASE.split("[portrait]")[0])))
        self.assertIn("set exactly one of", self.problems(r(f'photo = "photo.jpg"\nprocessed = "{PLACEHOLDER.as_posix()}"')))
        self.assertIn("missing.jpg not found", self.problems(r('photo = "missing.jpg"')))
        (self.dir / "notes.jpg").write_text("not a picture")
        self.assertIn("is not a picture Pillow can read", self.problems(r('photo = "notes.jpg"')))
        self.assertIn("would stretch the face", self.problems(r('photo = "photo.jpg"\ncrop = [0, 0, 300, 300]')))
        self.assertIn("reaches outside", self.problems(r('photo = "photo.jpg"\ncrop = [400, 0, 260, 320]')))
        self.photo((80, 100), "tiny.jpg")
        self.assertIn("pictures are never enlarged", self.problems(r('photo = "tiny.jpg"')))
        self.assertIn("method: 'sketch'", self.problems(r('photo = "photo.jpg"\nmethod = "sketch"')))
        self.assertIn("only applies to photo", self.problems(r(f'processed = "{PLACEHOLDER.as_posix()}"\ngamma = 0.8')))
        Image.linear_gradient("L").resize((104, 128)).save(self.dir / "grey.png")
        self.assertIn("grey or transparent pixels", self.problems(r('processed = "grey.png"')))
        Image.new("1", (296, 128), 1).save(self.dir / "wide.png")
        self.assertIn("at most 148x128", self.problems(r('processed = "wide.png"')))

    def test_placeholder_is_noted(self):
        form = self.load(BASE)
        self.assertTrue(any("sample silhouette" in n for n in form.notes))


class Outputs(FormCase):
    def test_generated_profile_never_replaces_a_hand_written_one(self):
        form = self.load(BASE)
        out = self.dir / "out"
        out.mkdir()
        mine = '{"format": 1, "profile": {"name": "mine"}}'
        (out / "profile.json").write_text(mine)
        with self.assertRaises(bf.FormError) as cm:
            bf.write_profile(form, out)
        self.assertIn("was not generated from a form", str(cm.exception))
        self.assertEqual((out / "profile.json").read_text(), mine)
        (out / "profile.json").unlink()
        p = bf.write_profile(form, out)
        self.assertEqual(badge_profile.load(p), badge_profile.flatten(form.doc))
        bf.write_profile(form, out)  # its own output is refreshed

    def test_new_and_export_never_overwrite(self):
        dest = self.dir / "badge.toml"
        self.assertEqual(bf.main(["new", str(dest)]), 0)
        self.assertEqual(dest.read_bytes(), bf.TEMPLATE.read_bytes())
        dest.write_text("my edits")
        self.assertEqual(bf.main(["new", str(dest)]), 1)
        self.assertEqual(bf.main(["export", str(SAMPLE_JSON), "--form", str(dest)]), 1)
        self.assertEqual(dest.read_text(), "my edits")

    def test_glyph_table_matches_the_font_generator(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location("fontgen", ROOT / "tools/fontgen.py")
        fontgen = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fontgen)
        self.assertEqual(set(fontgen.CODEPOINTS), set(badge_profile.GLYPHS))


def tree_hashes(top: Path) -> dict:
    """Every file (and link) under `top` -> SHA-256 of its bytes (or link target)."""
    out = {}
    for p in sorted(top.rglob("*")):
        if p.is_symlink():
            out[str(p.relative_to(top))] = "link:" + os.readlink(p)
        elif p.is_file():
            out[str(p.relative_to(top))] = hashlib.sha256(p.read_bytes()).hexdigest()
    return out


class Review(FormCase):
    """Regressions for the review findings on PR #3 (forms, outputs, backups, CMake)."""

    # --- 7. export keeps hidden (label-only) contacts in their slot
    def test_export_keeps_a_hidden_contact_between_populated_ones(self):
        doc = sample_doc()
        doc["profile"]["contacts"].insert(1, {"label": "Fax", "value": "", "type": "phone"})
        doc["profile"]["contacts"].append({})  # trailing empty slot: the same as none
        text = bf.export_form(doc, [f'processed = "{PLACEHOLDER.as_posix()}"'], "p.json")
        self.assertIn('label = "Fax"\nvalue = ""\nhidden = true', text)
        form = self.load(text)  # the exported form loads
        self.assertEqual(badge_profile.flatten(form.doc), badge_profile.flatten(doc))  # exact: slots kept
        self.assertEqual(form.doc["profile"]["contacts"][1], {"label": "Fax", "value": "", "type": "phone"})
        src = self.dir / "p.json"
        src.write_text(json.dumps(doc), encoding="utf-8")
        import contextlib, io
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            self.assertEqual(bf.main(["export", str(src), "--form", str(self.dir / "out.toml")]), 0)
        self.assertIn("['Fax'] have no value: kept as hidden = true", buf.getvalue())

    def test_hidden_contact_rules(self):
        c = BASE + '[[contacts]]\ntype = "text"\nlabel = "Booth"\nvalue = "{}"\nhidden = {}\n'
        self.assertEqual(self.load(c.format("", "true")).doc["profile"]["contacts"],
                         [{"label": "Booth", "value": "", "type": "text"}])
        self.assertIn("hidden = true needs an empty value", self.problems(c.format("4.2", "true")))
        self.assertIn("contacts #1.hidden: expected true or false", self.problems(c.format("", '"yes"')))
        self.assertIn("set hidden = true to keep the line", self.problems(c.format("", "false")))

    def test_export_reports_compacted_project_slots(self):
        doc = sample_doc()
        doc["profile"]["projects"].insert(2, {})
        src = self.dir / "p.json"
        src.write_text(json.dumps(doc), encoding="utf-8")
        import contextlib, io
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            self.assertEqual(bf.main(["export", str(src), "--form", str(self.dir / "out.toml"),
                                      "--processed", str(PLACEHOLDER)]), 0)
        self.assertIn("empty project entries [3] were left out", buf.getvalue())
        titles = lambda d: [p["title"] for p in d["profile"]["projects"] if p.get("title")]
        self.assertEqual(titles(bf.load_form(self.dir / "out.toml").doc), titles(doc))

    # --- 1. output ownership
    def foreign_dir(self) -> Path:
        d = self.dir / "docs"
        (d / "previews").mkdir(parents=True)
        (d / "previews" / "keep.png").write_bytes(b"mine")
        (d / "portrait.png").write_bytes(b"mine too")
        (d / "notes.txt").write_text("unrelated")
        return d

    def assertRefusedUnchanged(self, form, out: Path, needle: str, top: Path | None = None):
        top = top or out
        before = tree_hashes(top)
        with self.assertRaises(bf.FormError) as cm:
            bf.preview(form, out)
        self.assertIn(needle, str(cm.exception))
        self.assertEqual(tree_hashes(top), before)  # nothing written, deleted or replaced

    def test_foreign_output_directory_is_left_byte_identical(self):
        form = self.load(BASE)
        self.assertRefusedUnchanged(form, self.foreign_dir(), "already holds files this tool did not write")

    def test_unlisted_output_in_an_owned_directory_is_refused(self):
        form = self.load(BASE)
        out = self.dir / "out"
        bf.claim_out(out, form)
        (out / "previews").mkdir()
        (out / "previews" / "keep.png").write_bytes(b"mine")  # not in the manifest
        self.assertRefusedUnchanged(form, out, "previews exists and was not written by this tool")

    def test_symlinked_outputs_are_refused(self):
        form = self.load(BASE)
        victim = self.dir / "victim"
        victim.mkdir()
        (victim / "important.png").write_bytes(b"precious")
        out = self.dir / "out"
        bf.claim_out(out, form)
        bf._own(out, "portrait.png")
        (out / "portrait.png").symlink_to(victim / "important.png")
        self.assertRefusedUnchanged(form, out, "is a symbolic link; writing there would change another file",
                                    top=self.dir)
        (out / "portrait.png").unlink()
        link = self.dir / "outlink"
        link.symlink_to(victim, target_is_directory=True)
        self.assertRefusedUnchanged(form, link, "is a symbolic link; give the real directory", top=self.dir)

    def test_output_paths_colliding_with_inputs_are_refused(self):
        from PIL import Image
        out = self.dir / "out"
        out.mkdir()
        Image.new("1", (104, 128), 1).save(out / "portrait.png")  # the form's own input
        form = bf.load_form(self.write(BASE.split("[portrait]")[0] + '[portrait]\nprocessed = "out/portrait.png"\n'))
        bf._own(out, bf.OUT_MARK)  # even an owned directory: the input must not be overwritten
        bf._own(out, "portrait.png")
        self.assertRefusedUnchanged(form, out, "is one of the form's inputs but also an output path", top=self.dir)

    def test_output_of_the_first_version_is_adopted(self):
        form = self.load(BASE)
        out = self.dir / "out"
        (out / "previews").mkdir(parents=True)
        (out / "profile.json").write_text(json.dumps({bf.GENERATED_MARK: "tools/badge_form.py"}))
        bf.claim_out(out, form)  # no manifest yet, but its own generated profile.json
        self.assertEqual(set(json.loads((out / bf.OUT_MARK).read_text())["owned"]),
                         {bf.OUT_MARK, "profile.json", "previews"})
        other = self.dir / "other"
        other.mkdir()
        (other / "profile.json").write_text('{"format": 1}')  # a hand-written profile: not adopted
        self.assertRefusedUnchanged(form, other, "already holds files this tool did not write")

    def test_output_directory_with_spaces(self):
        if not PREVIEW.exists():
            self.skipTest("build host/ first")
        out = self.dir / "out dir" / "my badge"
        res = bf.preview(self.load(BASE, name="my badge.toml"), out)
        self.assertTrue((out / "previews/native/card.png").is_file())
        self.assertFalse([s for s, r in res["report"]["screens"].items() if r["ok"] is False])

    def test_owned_directory_is_reused_and_manifest_lists_outputs(self):
        if not PREVIEW.exists():
            self.skipTest("build host/ first")
        form = self.load(BASE)
        out = self.dir / "out"
        bf.preview(form, out)
        bf.preview(form, out)  # second run: replaces its own outputs
        owned = json.loads((out / bf.OUT_MARK).read_text())["owned"]
        self.assertTrue({"profile.json", "portrait.png", "previews", "assets.bin"} <= set(owned), owned)

    # --- 2./3. backups: archived, relocatable, no symlinks
    def make_local(self) -> Path:
        from PIL import Image
        root = self.dir / "repo"
        local = root / "local"
        for d in ("backups", "out/badge/previews", "out/badge/fw", "pics"):
            (local / d).mkdir(parents=True)
        for rel in ("photo.png", "pics/photo.png", "backups/photo.png", "out/badge/previews/photo.png",
                    "out/badge/fw/photo.png", "out/badge/fw-build.png"):
            Image.new("1", (104, 128), 1).save(local / rel)
        return root

    def backup_form(self, root: Path, ref: str, name="badge.toml"):
        f = root / "local" / name
        f.write_text(BASE.split("[portrait]")[0] + f'[portrait]\nprocessed = "{ref}"\n')
        return bf.load_form(f)

    def test_backup_rejects_excluded_outside_and_absolute_inputs(self):
        root = self.make_local()
        problems = lambda ref: "\n".join(bf.backup_problems(self.backup_form(root, ref), root))
        self.assertEqual(problems("photo.png"), "")
        self.assertEqual(problems("pics/../photo.png"), "")
        self.assertIn("inside local/backups/", problems("backups/photo.png"))
        self.assertIn("inside local/out/*/previews/", problems("out/badge/previews/photo.png"))
        self.assertIn("inside local/out/*/fw/", problems("out/badge/fw/photo.png"))
        self.assertIn("inside local/out/*/fw-", problems("out/badge/fw-build.png"))
        (self.dir / "outside.png").write_bytes((root / "local/photo.png").read_bytes())
        self.assertIn("outside local/", problems("../../outside.png"))
        self.assertIn("is an absolute path; write it relative to the form",
                      problems((root / "local/photo.png").as_posix()))
        f = root / "local/backups/badge.toml"  # the form itself in an excluded directory
        f.write_text(BASE.split("[portrait]")[0] + '[portrait]\nprocessed = "../photo.png"\n')
        self.assertIn("badge.toml: inside local/backups/", "\n".join(bf.backup_problems(bf.load_form(f), root)))

    def test_backup_rejects_inputs_reached_through_symlinks(self):
        root = self.make_local()
        local = root / "local"
        (local / "linked").symlink_to(local / "backups", target_is_directory=True)
        msg = "\n".join(bf.backup_problems(self.backup_form(root, "linked/photo.png"), root))
        self.assertIn("reached through a symbolic link", msg)
        (local / "alias.png").symlink_to(local / "photo.png")  # a link to an archived file is still a link
        msg = "\n".join(bf.backup_problems(self.backup_form(root, "alias.png"), root))
        self.assertIn("reached through a symbolic link", msg)
        (self.dir / "elsewhere.png").write_bytes((local / "photo.png").read_bytes())
        (local / "away.png").symlink_to(self.dir / "elsewhere.png")
        self.assertIn("outside local/", "\n".join(bf.backup_problems(self.backup_form(root, "away.png"), root)))

    def test_restored_form_reads_only_the_restored_copy(self):
        root = self.make_local()
        (root / "local/me.vcf").write_text("BEGIN:VCARD\nVERSION:3.0\nFN:Alex Example\nEND:VCARD\n")
        f = root / "local/forms/badge.toml"
        f.parent.mkdir()
        f.write_text(BASE.split("[portrait]")[0] + '[qr]\nshow = "vcard"\nvcard_file = "../me.vcf"\n'
                     '[portrait]\nprocessed = "../pics/photo.png"\n')
        self.assertEqual(bf.backup_problems(bf.load_form(f), root), [])
        restored = self.dir / "restored"
        shutil.copytree(root / "local", restored / "local", symlinks=True)
        shutil.rmtree(root)  # the original inputs are gone
        form = bf.load_form(restored / "local/forms/badge.toml")
        for p in form.inputs:
            self.assertTrue(p.resolve().is_relative_to(restored.resolve()), p)
        self.assertEqual(bf.backup_problems(form, restored), [])
        self.assertIn("FN:Alex Example", form.doc["profile"]["qr"]["payload"])

    # --- 4. structured backup metadata
    def archive(self, buildinfo: str, inputs: dict | None = None) -> Path:
        d = self.dir / f"a{len(list(self.dir.iterdir()))}"
        d.mkdir()
        (d / "BUILDINFO").write_text(buildinfo)
        if inputs is not None:
            (d / "INPUTS.json").write_text(json.dumps(inputs))
        return d

    def test_backup_metadata_with_spaces_and_older_archives(self):
        d = self.archive("commit abc\ninputs form\n")
        bf.write_backup_inputs(d / "INPUTS.json", "local/my badge.toml", "local/out/my badge")
        self.assertEqual(bf.read_backup_inputs(d), {"mode": "form", "form": "local/my badge.toml",
                                                    "out": "local/out/my badge"})
        self.assertEqual(bf.read_backup_inputs(self.archive("commit abc\n")), {"mode": "json"})  # pre-form
        self.assertEqual(bf.read_backup_inputs(self.archive("inputs json\n")), {"mode": "json"})
        old = self.archive("inputs form local/my badge.toml local/out/my badge\n")  # first form backups
        self.assertEqual(bf.read_backup_inputs(old)["form"], "local/my badge.toml")
        self.assertEqual(bf.read_backup_inputs(old)["out"], "local/out/my badge")
        with self.assertRaises(ValueError):  # not the default output directory: ambiguous, refused
            bf.read_backup_inputs(self.archive("inputs form local/a b.toml local/out/c d\n"))
        script = ROOT / "scripts/private-backup.sh"
        out = subprocess.run([sys.executable, str(ROOT / "tools/badge_form.py"), "backup-meta", str(d), "out"],
                             capture_output=True, text=True, check=True).stdout
        self.assertEqual(out, "local/out/my badge\n")
        self.assertIn('backup-meta --write "$stage/INPUTS.json" "$form" "$out"', script.read_text())

    def test_backup_excludes_match_the_script(self):
        import re
        script = (ROOT / "scripts/private-backup.sh").read_text()
        line = next(l for l in script.splitlines() if "rsync -a" in l)
        self.assertEqual(tuple(re.findall(r"--exclude '?([^' ]+)'?", line)), bf.BACKUP_EXCLUDES)

    # --- 6. block numbers
    def test_qr_failure_names_the_form_block(self):
        text = (BASE + '[[projects]]\ntitle = ""\n[[projects]]\ntitle = "Kept"\nlink = "https://example.com/a"\n')
        form = self.load(text)
        self.assertEqual(bf.qr_failures(form, ["project-qr"]), ["projects #2 (Kept).link: its QR code did not "
                                                                 "decode back to the link"])

    # --- 5. CMake with a form as BADGER_PROFILE
    @unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja"), "needs cmake and ninja")
    def test_cmake_form_profile_requires_a_portrait_and_tracks_its_files(self):
        vcf = self.dir / "me.vcf"
        vcf.write_text("BEGIN:VCARD\nVERSION:3.0\nFN:Alex Example\nEND:VCARD\n")
        form = self.write(BASE + '[qr]\nshow = "vcard"\nvcard_file = "me.vcf"\n')
        cfg = lambda b, *extra: subprocess.run(
            ["cmake", "-S", str(ROOT / "host"), "-B", str(b), "-G", "Ninja", f"-DPython3_EXECUTABLE={sys.executable}",
             f"-DBADGER_PROFILE={form}", *extra], capture_output=True, text=True)
        res = cfg(self.dir / "b1")
        self.assertNotEqual(res.returncode, 0)  # no silent default portrait
        self.assertIn("BADGER_PROFILE is a form", res.stderr)
        build = self.dir / "b2"
        res = cfg(build, f"-DBADGER_PORTRAIT={PLACEHOLDER}")
        self.assertEqual(res.returncode, 0, res.stderr[-2000:])
        gen = build / "generated/profile_defaults.cpp"
        ninja = lambda: subprocess.run(["ninja", "-C", str(build), "generated/profile_defaults.cpp"],
                                       capture_output=True, text=True, check=True)
        ninja()
        self.assertIn("FN:Alex Example", gen.read_text())
        import time
        time.sleep(1.1)  # coarse file timestamps
        vcf.write_text("BEGIN:VCARD\nVERSION:3.0\nFN:Sam Sample\nEND:VCARD\n")
        ninja()  # an edit to the vCard file regenerates the profile
        self.assertIn("FN:Sam Sample", gen.read_text())


@unittest.skipUnless(PREVIEW.exists(), "build host/ first")
class Rendered(FormCase):
    def test_form_and_json_render_identical_screens(self):
        import render_previews
        doc = sample_doc()
        json_path = self.dir / "sample.json"
        json_path.write_text(json.dumps(doc, ensure_ascii=False), encoding="utf-8")
        form_path = self.write(bf.export_form(doc, [f'processed = "{PLACEHOLDER.as_posix()}"'], "sample"))
        outs = {}
        for name, profile in (("json", json_path), ("form", form_path)):
            out = self.dir / name
            rc = render_previews.main(["--preview", str(PREVIEW), "--out", str(out), "--profile", str(profile)])
            self.assertEqual(rc, 0)
            outs[name] = {p.name: p.read_bytes() for p in sorted((out / "native").glob("*.png"))}
        self.assertGreater(len(outs["json"]), 20)
        self.assertEqual(outs["json"], outs["form"])

    def test_template_preview_passes_every_gate(self):
        form = bf.load_form(bf.TEMPLATE)
        res = bf.preview(form, self.dir / "out")
        self.assertTrue(res["sheet"].exists())
        self.assertTrue(all(r["ok"] for r in res["report"]["screens"].values()), res["report"]["screens"])
        self.assertTrue((self.dir / "out/previews/native/project-qr_1.png").exists())
        self.assertFalse((self.dir / "out/previews/native/project-qr_2.png").exists())  # teaser: no QR

    def test_text_that_would_be_cut_names_the_form_field(self):
        cases = [
            (BASE.replace("Alex Example", "Wilhelmina Featherstonehaugh-Cholmondeley"), "person.name: does not fit on"),
            (BASE + '[[contacts]]\ntype = "email"\nlabel = "Email"\n'
                    'value = "a.very.long.address.that.cannot.fit@subdomain.example.com"\n',
             "contacts #1 (Email).value: does not fit on the business card"),
            (BASE + '[[projects]]\ntitle = "P"\ntagline = "A tagline that takes up a whole line here"\n'
                    'status = "Work in progress"\nlink = "https://github.com/a/b"\n'
                    'description = "' + "Far too long for the page. " * 7 + '"\n',
             "projects #1 (P).description: does not fit on the project page"),
            (BASE + '[[projects]]\ntitle = "WWWWWWWWWWWWWWWWW"\nlink = "https://github.com/a/b"\n',
             "projects #1 (WWWWWWWWWWWWWWWWW).title: does not fit on the repository QR page"),
        ]
        for text, needle in cases:
            form = self.load(text)
            with self.assertRaises(bf.FormError) as cm:
                bf.preview(form, self.dir / "out")
            self.assertIn(needle, str(cm.exception))
            self.assertFalse((self.dir / "out/previews").exists())  # stopped before rendering

    def test_fit_errors_use_the_form_block_number(self):
        text = (BASE + '[[contacts]]\ntype = "email"\nlabel = ""\nvalue = ""\n'
                '[[contacts]]\ntype = "email"\nlabel = "Email"\n'
                'value = "a.very.long.address.that.cannot.fit@subdomain.example.com"\n'
                '[[projects]]\ntitle = ""\n[[projects]]\ntitle = "WWWWWWWWWWWWWWWWW"\nlink = "https://example.com/a"\n')
        form = self.load(text)
        self.assertEqual(form.blocks, {"contact": [2], "project": [2]})
        with self.assertRaises(bf.FormError) as cm:
            bf.preview(form, self.dir / "out")
        msg = str(cm.exception)
        self.assertIn("contacts #2 (Email).value: does not fit", msg)
        self.assertIn("projects #2 (WWWWWWWWWWWWWWWWW).title: does not fit", msg)
        self.assertNotIn("#1", msg)

    def test_render_previews_gate_covers_json_profiles_too(self):
        import render_previews
        doc = json.loads(SAMPLE_JSON.read_text(encoding="utf-8"))
        for change, needle in ((("name", "Wilhelmina Featherstonehaugh-Cholmondeley"), "name on the photo badge"),
                               (("title", "Engineer\nMaker"), "line break in a single-line field"),
                               (("title", "Łódź"), "no glyph")):
            d = json.loads(json.dumps(doc))
            d["profile"][change[0]] = change[1]
            p = self.dir / "p.json"
            p.write_text(json.dumps(d, ensure_ascii=False))
            with self.assertRaises(SystemExit) as cm:
                render_previews.main(["--preview", str(PREVIEW), "--out", str(self.dir / "pv"), "--profile", str(p),
                                      "--screens", "badge"])
            self.assertIn(needle, str(cm.exception))


if __name__ == "__main__":
    unittest.main()
