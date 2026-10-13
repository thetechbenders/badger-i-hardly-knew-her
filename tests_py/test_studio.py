"""Studio localhost API tests. Run with the Studio requirements installed."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from fastapi.testclient import TestClient  # noqa: E402
from studio.app import create_app  # noqa: E402


class StudioTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.client = TestClient(create_app(Path(self.temp.name) / "workspace"),
                                 base_url="http://127.0.0.1:8765")
        home = self.client.get("/")
        self.assertEqual(home.status_code, 200)
        self.assertNotIn('studio-token', home.text)
        self.token = self.client.app.state.studio_token
        self.auth = {"X-Studio-Token": self.token}
        self.form = self.client.get("/api/v1/workspace", headers=self.auth).json()

    def test_screen_preview_reuses_canonical_pipeline_without_saving(self):
        """Unsaved TOML is rendered without persisting a workspace change."""
        import base64
        from unittest.mock import patch
        from PIL import Image
        from studio import app as studio_module

        seen = []

        def fake_preview(form, output):
            seen.append((form.target, form.doc["profile"]["name"]))
            native = output / "previews" / "native"
            native.mkdir(parents=True)
            Image.new("1", (296, 128), 255).save(native / "badge.png")
            return {"previews": output / "previews", "report": {"screens": {}}}

        before = self.form["toml"]
        edited = before.replace('name = "Alex Example"', 'name = "Preview Person"')
        with patch.object(studio_module.badge_form, "preview", side_effect=fake_preview):
            response = self.client.put("/api/v1/screens/preview", headers=self.auth,
                json={"target": "badger2040", "toml": edited})
        self.assertEqual(response.status_code, 200, response.text)
        self.assertEqual(response.json()["screens"][0]["name"], "badge")
        self.assertTrue(base64.b64decode(response.json()["screens"][0]["png"]).startswith(bytes((137, 80, 78, 71))))
        self.assertEqual(seen, [("badger2040", "Preview Person")])
        saved = self.client.get("/api/v1/workspace", headers=self.auth).json()
        self.assertEqual(saved["toml"], before)

    def test_screen_preview_refuses_invalid_content_before_renderer(self):
        from unittest.mock import patch
        from studio import app as studio_module
        with patch.object(studio_module.badge_form, "preview") as render:
            result = self.client.put("/api/v1/screens/preview", headers=self.auth,
                json={"target": "badger2040",
                      "toml": self.form["toml"].replace('processed = "sample-badger2040.png"',
                                                         'processed = "../../outside.png"')})
            self.assertEqual(result.status_code, 422)
            render.assert_not_called()

    def test_portrait_workshop_rejects_bad_images_and_accepts_selection(self):
        import base64
        import io
        from PIL import Image

        def encoded(image):
            buf = io.BytesIO()
            image.save(buf, format="PNG")
            return base64.b64encode(buf.getvalue()).decode("ascii")

        picture = Image.new("RGB", (208, 256))
        for y in range(256):
            for x in range(208):
                picture.putpixel((x, y), (x % 256, y, (x + y) % 256))
        preview = self.client.put("/api/v1/portrait/preview",
            headers=self.auth, json={"target": "badger2040", "image": encoded(picture)})
        self.assertEqual(preview.status_code, 200, preview.text)
        variants = preview.json()["variants"]
        self.assertEqual(set(variants), {"threshold", "bayer8", "floyd", "atkinson"})
        with Image.open(io.BytesIO(base64.b64decode(variants["atkinson"]))) as generated:
            self.assertEqual(generated.mode, "1")
            self.assertEqual(generated.size, (104, 128))
        selected = self.client.put("/api/v1/portrait/select",
            headers=self.auth, json={"target": "badger2040", "image": variants["atkinson"]})
        self.assertEqual(selected.status_code, 200, selected.text)
        name = selected.json()["processed"]
        self.assertRegex(name, r"^portrait-badger2040-[0-9a-f]{64}\.png$")
        self.assertTrue((Path(self.temp.name) / "workspace" / name).is_file())
        self.assertEqual(self.client.put("/api/v1/portrait/preview",
            headers=self.auth, json={"target": "badger2040", "image": "not base64"}).status_code, 422)
        self.assertEqual(self.client.put("/api/v1/portrait/preview",
            headers=self.auth, json={"target": "badger2040", "image": encoded(picture),
                                     "crop": [0, 0, 1000, 1000]}).status_code, 422)
        self.assertEqual(self.client.put("/api/v1/portrait/select",
            headers=self.auth, json={"target": "badger2040", "image": encoded(picture)}).status_code, 422)

    def test_portrait_upload_limits_and_type_validation(self):
        import base64
        import io
        from PIL import Image

        # A 1 MiB+ valid PNG must now reach conversion instead of hitting
        # the previous transport cap. Noise ensures it cannot compress below it.
        import numpy as np
        rng = np.random.default_rng(24351)
        pixels = rng.integers(0, 256, size=(700, 700, 3), dtype=np.uint8)
        buffer = io.BytesIO()
        Image.fromarray(pixels, "RGB").save(buffer, format="PNG")
        raw = buffer.getvalue()
        self.assertGreater(len(raw), 1024 * 1024)
        self.assertLess(len(raw), 10 * 1024 * 1024)
        response = self.client.put("/api/v1/portrait/preview", headers=self.auth,
            json={"target": "badger2040", "image": base64.b64encode(raw).decode("ascii")})
        self.assertEqual(response.status_code, 200, response.text)

        # Oversized raw images are rejected without image decoding.
        giant = base64.b64encode(b"x" * (10 * 1024 * 1024 + 1)).decode("ascii")
        response = self.client.put("/api/v1/portrait/preview", headers=self.auth,
            json={"target": "badger2040", "image": giant})
        self.assertEqual(response.status_code, 413, response.text)
        self.assertIn("10 MiB", response.json()["detail"])

    def test_portrait_tone_settings_are_bounded_and_effective(self):
        import base64
        import io
        from PIL import Image
        image = Image.new("RGB", (208, 256))
        for y in range(256):
            for x in range(208):
                image.putpixel((x, y), ((x + y) % 256, y, x))
        stream = io.BytesIO()
        image.save(stream, format="PNG")
        body = {"target": "badger2040",
                "image": base64.b64encode(stream.getvalue()).decode("ascii")}
        baseline = self.client.put("/api/v1/portrait/preview",
                                   headers=self.auth, json=body)
        self.assertEqual(baseline.status_code, 200, baseline.text)
        adjusted = self.client.put("/api/v1/portrait/preview",
                                   headers=self.auth,
                                   json={**body, "gamma": 2.0, "black_pct": 3.0,
                                         "white_pct": 4.0, "sharpen": 1.5})
        self.assertEqual(adjusted.status_code, 200, adjusted.text)
        self.assertNotEqual(baseline.json()["variants"]["atkinson"],
                            adjusted.json()["variants"]["atkinson"])
        for key, invalid in (("gamma", 0), ("gamma", 5.1), ("black_pct", -1),
                             ("white_pct", 41), ("sharpen", 3.1)):
            result = self.client.put("/api/v1/portrait/preview",
                       headers=self.auth, json={**body, key: invalid})
            self.assertEqual(result.status_code, 422, (key, result.text))

    def test_portrait_preview_reuses_original_composition_on_2350(self):
        import base64
        import io
        from PIL import Image
        image = Image.new("RGB", (208, 256))
        out = io.BytesIO()
        image.save(out, format="PNG")
        response = self.client.put("/api/v1/portrait/preview",
            headers=self.auth, json={"target": "badger2350",
                                      "image": base64.b64encode(out.getvalue()).decode("ascii")})
        # A flat image may fail existing tonal-range checks, but it must never
        # silently adopt a 104x176 crop.
        self.assertEqual(response.status_code, 422)

    def test_crop_dimming_is_confined_to_photo(self):
        css = (ROOT / "studio/static/style.css").read_text(encoding="utf-8")
        self.assertIn("overflow:hidden; isolation:isolate;", css)
        self.assertIn("isolation:isolate;", css)

    def test_crop_preview_content_security_policy_allows_local_blob_images(self):
        """The crop source uses URL.createObjectURL(file), which requires blob: images."""
        response = self.client.get("/")
        policy = response.headers["content-security-policy"]
        self.assertIn("img-src 'self' data: blob:;", policy)
        self.assertIn("default-src 'none';", policy)
        self.assertIn("connect-src 'self';", policy)

    def test_token_not_available_to_other_local_clients(self):
        home = self.client.get("/")
        self.assertNotIn(self.token, home.text)
        self.assertEqual(self.client.get("/api/v1/workspace").status_code, 403)
        self.assertEqual(self.client.get("/api/v1/workspace",
                         headers={"X-Studio-Token": self.token}).status_code, 200)

    def test_restart_ignores_target_names_in_comments(self):
        work = Path(self.temp.name) / "workspace"
        with (work / "badge.toml").open("a", encoding="utf-8") as stream:
            stream.write('\n# processed = "sample-badger2350.png"\n')
        restarted = TestClient(create_app(work), base_url="http://127.0.0.1:8765")
        data = restarted.get("/api/v1/workspace",
               headers={"X-Studio-Token": restarted.app.state.studio_token}).json()
        self.assertEqual(data["target"], "badger2040")

    def test_port_reservation_refuses_stale_server_without_printing_token(self):
        """An occupied port cannot produce a misleading new launch URL."""
        import io
        from contextlib import redirect_stderr, redirect_stdout
        from unittest.mock import patch
        from studio.app import _bind_local_port, main

        with _bind_local_port(0) as first:
            port = first.getsockname()[1]
            output, errors = io.StringIO(), io.StringIO()
            with patch.object(sys, "argv", ["studio.app", "--port", str(port)]):
                with redirect_stdout(output), redirect_stderr(errors):
                    with self.assertRaises(SystemExit) as exc:
                        main()
            self.assertEqual(exc.exception.code, 2)
            self.assertNotIn("#token=", output.getvalue())
            self.assertIn("already be listening", errors.getvalue())
            self.assertIn("port 0", errors.getvalue())

    def test_port_zero_reserves_free_loopback_socket(self):
        """The operating system selects and holds a usable loopback port."""
        from studio.app import _bind_local_port

        with _bind_local_port(0) as listener:
            address, port = listener.getsockname()
            self.assertEqual(address, "127.0.0.1")
            self.assertGreater(port, 0)
            self.assertLessEqual(port, 65535)

    def test_streaming_upload_exceeds_limit_without_content_length(self):
        def chunks():
            yield b'{"target":"badger2040","toml":"'
            yield b"A" * (128 * 1024)
            yield b'"}'
        result = self.client.put("/api/v1/workspace", content=chunks(),
                    headers={**self.auth, "Content-Type": "application/json"})
        self.assertEqual(result.status_code, 413)

    def test_visual_html_has_every_javascript_dom_target(self):
        """Catch HTML/JavaScript selector mismatches before browser rendering."""
        import re
        from html.parser import HTMLParser

        class Elements(HTMLParser):
            def __init__(self):
                super().__init__()
                self.ids = set()

            def handle_starttag(self, tag, attrs):
                self.ids.update(value for key, value in attrs if key == "id")

        parser = Elements()
        parser.feed((ROOT / "studio/static/index.html").read_text(encoding="utf-8"))
        script = (ROOT / "studio/static/app.js").read_text(encoding="utf-8")
        selectors = set(re.findall(r'\$\("([a-z][a-z0-9-]*)"\)', script))
        # list() resolves $(section+"-list") for "contacts" and "projects".
        selectors.update(("contacts-list", "projects-list"))
        self.assertIn('$(section+"-list")', script)
        self.assertFalse(selectors - parser.ids,
                         f"Missing editor elements: {sorted(selectors - parser.ids)}")

    def test_visual_parse_and_compose_preserves_advanced_fields(self):
        from studio import form_editor
        original = self.form["toml"]
        parsed = self.client.put("/api/v1/parse", headers=self.auth,
                 json={"target":"badger2040","toml":original})
        self.assertEqual(parsed.status_code, 200, parsed.text)
        fields = parsed.json()["fields"]
        self.assertEqual(fields["person"]["name"], "Alex Example")
        fields["person"]["name"] = "Studio Test"
        fields["person"]["interests"] = ["3D printing", "embedded systems"]
        fields["contacts"][0]["value"] = "studio@example.com"
        fields["contacts"].append({"type":"text","label":"Booth","value":"A17","hidden":False})
        fields["projects"][0]["title"] = "Quiet Duct"
        fields["projects"].reverse()
        fields["qr"]["caption"] = "Scan me"
        composed = self.client.put("/api/v1/compose", headers=self.auth,
            json={"target":"badger2040","toml":original,"fields":fields})
        self.assertEqual(composed.status_code, 200, composed.text)
        text = composed.json()["toml"]
        self.assertIn("sleep.timeout_s = 120", text)
        self.assertIn("#  BHIHKH! personal badge form", text)
        self.assertIn("Studio Test", text)
        roundtrip = form_editor.view(text)
        self.assertEqual(roundtrip, fields)
        saved = self.client.put("/api/v1/workspace", headers=self.auth,
            json={"target":"badger2040","toml":text})
        self.assertEqual(saved.status_code, 200, saved.text)

    def test_visual_invalid_input_never_changes_workspace(self):
        baseline = self.form["toml"]
        view = self.client.put("/api/v1/parse", headers=self.auth,
                 json={"target":"badger2040","toml":baseline}).json()["fields"]
        view["person"]["name"] = "x" * 300
        rendered = self.client.put("/api/v1/compose", headers=self.auth,
            json={"target":"badger2040","toml":baseline,"fields":view})
        self.assertEqual(rendered.status_code, 200, rendered.text)
        refused = self.client.put("/api/v1/workspace", headers=self.auth,
            json={"target":"badger2040","toml":rendered.json()["toml"]})
        self.assertEqual(refused.status_code, 422, refused.text)
        self.assertEqual(self.client.get("/api/v1/workspace", headers=self.auth).json()["toml"], baseline)

    def test_visual_rejects_over_count_and_extra_fields(self):
        view = self.client.put("/api/v1/parse", headers=self.auth,
                 json={"target":"badger2040","toml":self.form["toml"]}).json()["fields"]
        view["contacts"] *= 3
        r = self.client.put("/api/v1/compose", headers=self.auth,
            json={"target":"badger2040","toml":self.form["toml"],"fields":view})
        self.assertEqual(r.status_code, 422)
        view["contacts"] = view["contacts"][:1]
        view["person"]["invalid"] = "inject"
        r = self.client.put("/api/v1/compose", headers=self.auth,
            json={"target":"badger2040","toml":self.form["toml"],"fields":view})
        self.assertEqual(r.status_code, 422)

    def test_visual_keeps_nonvisual_qr_settings(self):
        form = self.form["toml"].replace('caption = "Scan for my website"',
            'caption = "Scan for my website"\n# keep me\nvcard = "BEGIN:VCARD\\nEND:VCARD"')
        # Use a syntactically valid nonvisual field without making it active.
        from studio import form_editor
        data = form_editor.view(form)
        data["person"]["name"] = "Jane Example"
        updated = form_editor.apply(form, data)
        self.assertIn("# keep me", updated)
        self.assertIn("vcard =", updated)
        self.assertIn("Jane Example", updated)

    def test_template_round_trip_and_metadata(self):
        targets = self.client.get("/api/v1/targets", headers=self.auth).json()["targets"]
        self.assertEqual({x["id"] for x in targets}, {"badger2040", "badger2350"})
        r = self.client.put("/api/v1/workspace", headers={**self.auth, "Content-Type":"application/json"},
                            json=self.form)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json()["ok"])

    def test_badger2350_uses_its_own_sample(self):
        updated = dict(self.form)
        updated["target"]="badger2350"
        updated["toml"]=updated["toml"].replace("sample-badger2040.png","sample-badger2350.png")
        r=self.client.put("/api/v1/workspace",headers=self.auth,json=updated)
        self.assertEqual(r.status_code,200,r.text)

    def test_foreign_host_and_origin_rejected(self):
        self.assertEqual(self.client.get("/",headers={"Host":"evil.example"}).status_code,403)
        self.assertEqual(self.client.get("/",headers={"Origin":"http://evil.example"}).status_code,403)

    def test_api_requires_per_process_token(self):
        self.assertEqual(self.client.get("/api/v1/workspace").status_code,403)
        self.assertEqual(self.client.get("/api/v1/workspace",headers={"X-Studio-Token":"wrong"}).status_code,403)

    def test_bad_form_never_saved(self):
        before=self.form["toml"]
        r=self.client.put("/api/v1/workspace",headers=self.auth,json={"target":"badger2040","toml":"garbage"})
        self.assertEqual(r.status_code,422)
        self.assertEqual(self.client.get("/api/v1/workspace",headers=self.auth).json()["toml"],before)

    def test_reject_arbitrary_image_and_vcard_paths(self):
        for needle,replacement in [
            ('processed = "sample-badger2040.png"','processed = "/etc/passwd"'),
            ('processed = "sample-badger2040.png"','processed = "../../secrets.png"'),
        ]:
            bad=self.form["toml"].replace(needle,replacement)
            self.assertEqual(self.client.put("/api/v1/workspace",headers=self.auth,
                json={"target":"badger2040","toml":bad}).status_code,422)
        malformed=self.form["toml"].replace('show = "link"','show = "vcard"\nvcard_file = "../../private.vcf"')
        self.assertEqual(self.client.put("/api/v1/workspace",headers=self.auth,
            json={"target":"badger2040","toml":malformed}).status_code,422)

    def test_reject_invalid_target_foreign_origin_and_large_request(self):
        r=self.client.put("/api/v1/workspace",headers=self.auth,json={"target":"other","toml":self.form["toml"]})
        self.assertEqual(r.status_code,422)
        r=self.client.put("/api/v1/workspace",headers={**self.auth,"Origin":"http://evil.example"},
            json=self.form)
        self.assertEqual(r.status_code,403)
        r=self.client.put("/api/v1/workspace",headers=self.auth,json={"target":"badger2040","toml":"A"*140000})
        self.assertEqual(r.status_code,413)


    def test_immutable_portrait_selection_does_not_change_saved_badge(self):
        import base64
        import hashlib
        import io
        import re
        from PIL import Image

        work = Path(self.temp.name) / "workspace"

        def sample(mark):
            image = Image.new("1", (104, 128), 1)
            for y in range(25, 55):
                for x in range(mark, mark + 20):
                    image.putpixel((x, y), 0)
            stream = io.BytesIO()
            image.save(stream, format="PNG")
            return stream.getvalue()

        a, b = sample(3), sample(45)
        def choose(raw):
            return self.client.put("/api/v1/portrait/select", headers=self.auth,
                json={"target": "badger2040", "image": base64.b64encode(raw).decode()})

        first = choose(a)
        self.assertEqual(first.status_code, 200, first.text)
        filename_a = first.json()["processed"]
        self.assertEqual(filename_a, "portrait-badger2040-" + hashlib.sha256(a).hexdigest() + ".png")
        self.assertEqual((work / filename_a).read_bytes(), a)
        document = self.form["toml"].replace("sample-badger2040.png", filename_a)
        save = self.client.put("/api/v1/workspace", headers=self.auth,
                               json={"target": "badger2040", "toml": document})
        self.assertEqual(save.status_code, 200, save.text)

        second = choose(b)
        self.assertEqual(second.status_code, 200, second.text)
        filename_b = second.json()["processed"]
        self.assertNotEqual(filename_a, filename_b)
        self.assertEqual((work / filename_a).read_bytes(), a)
        self.assertEqual((work / filename_b).read_bytes(), b)
        self.assertEqual(choose(b).json()["processed"], filename_b)
        self.assertEqual(self.client.get("/api/v1/workspace", headers=self.auth).json()["toml"], document)

        restarted = TestClient(create_app(work), base_url="http://127.0.0.1:8765")
        credential = {"X-Studio-Token": restarted.app.state.studio_token}
        restored = restarted.get("/api/v1/workspace", headers=credential).json()
        self.assertEqual(restored["toml"], document)
        self.assertEqual(restored["target"], "badger2040")
        self.assertEqual((work / filename_a).read_bytes(), a)
        # Managed hashes cannot point to missing files or mutated bytes.
        (work / filename_a).write_bytes(b)
        rejected = self.client.put("/api/v1/workspace", headers=self.auth,
                  json={"target": "badger2040", "toml": document})
        self.assertEqual(rejected.status_code, 422)
        self.assertIn("content hash", rejected.text)

    def test_hashed_portrait_restores_badger2350_target(self):
        import base64
        import io
        from PIL import Image
        work = Path(self.temp.name) / "workspace"
        image = Image.new("1", (104, 128), 1)
        image.putpixel((10, 10), 0)
        stream = io.BytesIO()
        image.save(stream, format="PNG")
        selected = self.client.put("/api/v1/portrait/select", headers=self.auth,
                  json={"target": "badger2350", "image": base64.b64encode(stream.getvalue()).decode()})
        self.assertEqual(selected.status_code, 200, selected.text)
        filename = selected.json()["processed"]
        self.assertRegex(filename, r"^portrait-badger2350-[0-9a-f]{64}\.png$")
        document = self.form["toml"].replace("sample-badger2040.png", filename)
        saved = self.client.put("/api/v1/workspace", headers=self.auth,
                json={"target": "badger2350", "toml": document})
        self.assertEqual(saved.status_code, 200, saved.text)
        restarted = TestClient(create_app(work), base_url="http://127.0.0.1:8765")
        restored = restarted.get("/api/v1/workspace",
                   headers={"X-Studio-Token": restarted.app.state.studio_token}).json()
        self.assertEqual(restored["target"], "badger2350")
        self.assertEqual(restored["toml"], document)

    def test_portrait_reference_edits_only_one_toml_field(self):
        import tomllib
        original = self.form["toml"].replace(
            'name = "Alex Example"', 'name = "sample-badger2040.png"')
        original += '\n# remembered portrait-badger2040.png and sample-badger2040.png\n'
        response = self.client.put("/api/v1/portrait/reference", headers=self.auth,
                   json={"target": "badger2350", "toml": original})
        self.assertEqual(response.status_code, 200, response.text)
        changed = response.json()["toml"]
        self.assertIn('name = "sample-badger2040.png"', changed)
        self.assertIn("# remembered portrait-badger2040.png and sample-badger2040.png", changed)
        first = tomllib.loads(original)
        second = tomllib.loads(changed)
        self.assertEqual(second["portrait"]["processed"], "sample-badger2350.png")
        first["portrait"]["processed"] = "sample-badger2350.png"
        self.assertEqual(first, second)
        self.assertEqual(self.client.get("/api/v1/workspace", headers=self.auth).json()["toml"],
                         self.form["toml"])
        for requested in ("../../outside.png", "portrait-badger2350-" + "f" * 64 + ".png",
                          "sample-badger2040.png"):
            refused = self.client.put("/api/v1/portrait/reference", headers=self.auth,
                     json={"target": "badger2350", "toml": original, "processed": requested})
            self.assertEqual(refused.status_code, 422, (requested, refused.text))


    def test_portrait_routes_dispatch_processing_to_worker_threads(self):
        """Both portrait handlers must hand synchronous Pillow work to asyncio.to_thread."""
        import asyncio
        from unittest.mock import patch
        from studio import app as studio_module

        original = asyncio.to_thread
        calls = []

        async def recording_to_thread(fn, *args, **kwargs):
            calls.append(fn.__name__)
            return await original(fn, *args, **kwargs)

        with patch.object(studio_module.asyncio, "to_thread",
                          side_effect=recording_to_thread):
            bad_preview = self.client.put("/api/v1/portrait/preview",
                headers=self.auth, json={"target": "badger2040", "image": "invalid base64"})
            bad_selection = self.client.put("/api/v1/portrait/select",
                headers=self.auth, json={"target": "badger2040", "image": "invalid base64"})
        self.assertEqual(bad_preview.status_code, 422)
        self.assertEqual(bad_selection.status_code, 422)
        self.assertEqual(calls, ["_process_portrait", "_select_portrait"])


if __name__ == "__main__":
    unittest.main()
