"""Versioned badge-package round-trip and hostile archive tests (fictional input only)."""
from __future__ import annotations

import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import badgerpack  # noqa: E402


def fixture(target):
    form = (ROOT / "config" / "badge-form.toml").read_text(encoding="utf-8")
    form = form.replace('processed = "../assets/sample/portrait_placeholder.png"',
                        f'processed = "sample-{target}.png"')
    asset = ROOT / "assets" / "sample" / (
        "portrait_placeholder.png" if target == "badger2040" else "portrait_placeholder_badger2350.png")
    return form.encode(), asset.read_bytes()


class PackageTests(unittest.TestCase):
    def test_shell_dispatcher_parses_and_reaches_package_cli(self):
        import subprocess
        script = ROOT / "scripts" / "build-badge.sh"
        syntax = subprocess.run(["bash", "-n", str(script)], capture_output=True, text=True)
        self.assertEqual(syntax.returncode, 0, syntax.stderr)
        cli = subprocess.run([str(script), "--package", "--help"],
                             capture_output=True, text=True)
        self.assertEqual(cli.returncode, 0, cli.stderr)
        self.assertIn("package", cli.stdout)
        self.assertIn("--out", cli.stdout)

    def test_roundtrip_both_boards_and_canonical_validation(self):
        for target in ("badger2040", "badger2350"):
            with self.subTest(target=target):
                form, portrait = fixture(target)
                packaged = badgerpack.make_package(form, portrait, target)
                out_target, members = badgerpack.read_package(packaged)
                self.assertEqual(out_target, target)
                self.assertEqual(members["badge.toml"], form)
                self.assertEqual(members[f"sample-{target}.png"], portrait)
                with tempfile.TemporaryDirectory() as tmp:
                    for name, content in members.items():
                        (Path(tmp) / name).write_bytes(content)
                    validated = badgerpack.badge_form.load_form(Path(tmp) / "badge.toml", target=target)
                    self.assertEqual(validated.target, target)

    def test_reject_tampering_extra_members_and_wrong_target(self):
        form, portrait = fixture("badger2350")
        valid = badgerpack.make_package(form, portrait, "badger2350")
        self.assertRaises(badgerpack.PackageError, badgerpack.make_package, form, portrait, "badger2040")
        self.assertRaises(badgerpack.PackageError, badgerpack.read_package, valid + b"x" * (badgerpack.MAX_PACKAGE + 1))
        def rewritten(replacements=None, extra=None):
            with zipfile.ZipFile(io.BytesIO(valid)) as source:
                members = {name: source.read(name) for name in source.namelist()}
            members.update(replacements or {})
            if extra:
                members[extra] = b"evil"
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, "w") as dest:
                for name, data in members.items():
                    dest.writestr(name, data)
            return stream.getvalue()
        for corrupted in (
            rewritten({"badge.toml": form + b"\n# tamper"}),
            rewritten(extra="../escape"),
            rewritten(extra="admission.toml"),
            rewritten({"manifest.json": b"not-json"}),
        ):
            with self.subTest(size=len(corrupted)):
                self.assertRaises(badgerpack.PackageError, badgerpack.read_package, corrupted)

    def test_reject_duplicate_and_symlink_members(self):
        form, portrait = fixture("badger2040")
        valid = badgerpack.make_package(form, portrait, "badger2040")
        with zipfile.ZipFile(io.BytesIO(valid)) as source:
            members = [(name, source.read(name)) for name in source.namelist()]
        for duplicate in (True, False):
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, "w") as dest:
                for name, data in members:
                    if not duplicate and name == "badge.toml":
                        info = zipfile.ZipInfo(name)
                        info.create_system = 3
                        info.external_attr = (0o120777 << 16)
                        dest.writestr(info, data)
                    else:
                        dest.writestr(name, data)
                if duplicate:
                    dest.writestr("badge.toml", form)
            with self.subTest(duplicate=duplicate):
                self.assertRaises(badgerpack.PackageError, badgerpack.read_package, stream.getvalue())

    def test_private_external_paths_rejected(self):
        form, portrait = fixture("badger2040")
        for text in ('processed = "../secret.png"', 'photo = "../../private.jpg"',
                     'vcard_file = "private.txt"'):
            if text.startswith("vcard"):
                candidate = form.replace(b'[qr]', b'[qr]\n' + text.encode())
            else:
                candidate = form.replace(b'processed = "sample-badger2040.png"', text.encode())
            with self.subTest(text=text):
                self.assertRaises(badgerpack.PackageError, badgerpack.make_package,
                                  candidate, portrait, "badger2040")


if __name__ == "__main__":
    unittest.main()
