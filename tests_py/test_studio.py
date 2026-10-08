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
        self.client = TestClient(create_app(Path(self.temp.name) / "workspace"))
        home = self.client.get("/")
        self.assertEqual(home.status_code, 200)
        import re
        self.token = re.search(r'name="studio-token" content="([^"]+)"', home.text).group(1)
        self.auth = {"X-Studio-Token": self.token}
        self.form = self.client.get("/api/v1/workspace", headers=self.auth).json()

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
        r=self.client.put("/api/v1/workspace",headers=self.auth,json={"target":"badger2040","toml":"A"*130000})
        self.assertEqual(r.status_code,413)


if __name__ == "__main__":
    unittest.main()
