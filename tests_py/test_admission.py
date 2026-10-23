"""Fictional admission data only; never opens local/admission.toml."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("admissiongen", ROOT / "tools/admissiongen.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)
PAYLOAD = (b"BEGIN:VCARD\r\nVERSION:3.0\r\nN:Example;Fictional;;;\r\nFN:Fictional Example\r\n"
           b"ORG:Imaginary Exhibition 2026\r\nNOTE:TEST ONLY - NOT VALID FOR ADMISSION. Reference FAKE-00000000\r\n"
           b"URL:https://example.invalid/ticket/fictional-reference\r\nEND:VCARD\r\n")


def fixture(payload=PAYLOAD):
    import json
    return ('payload = ' + json.dumps(payload.decode("ascii")) + '\n'
            'event = "TEST"\nholder = "Example"\ntimeout_s = 15\n')


class AdmissionConfiguration(unittest.TestCase):
    def test_exact_bytes_and_newlines(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "fictional.toml"
            p.write_text(fixture())
            value = gen.load(p)
            self.assertEqual(value["payload"].encode("ascii"), PAYLOAD)
            source = gen.generate(value)
            self.assertIn(r"\015\012", source)
            self.assertNotIn("BEGIN:VCARD", source)

    def test_rejections_do_not_log_values(self):
        for payload in (b"", b"x" * 521, b"SECRET\x00", b"SECRET\x1b"):
            with tempfile.TemporaryDirectory() as d:
                p = Path(d) / "fictional.toml"
                p.write_text(fixture(payload))
                r = subprocess.run([os.sys.executable, str(ROOT / "tools/admissiongen.py"),
                                    str(p), "--out", str(Path(d) / "out.cpp")], capture_output=True)
                self.assertNotEqual(r.returncode, 0)
                self.assertNotIn(b"SECRET", r.stderr + r.stdout)
        for change in ('timeout_s = 16', 'holder = "WWWWWWWWWWWWWWWW"', 'holder = "\\u2603"'):
            with tempfile.TemporaryDirectory() as d:
                p = Path(d) / "fictional.toml"
                text = fixture()
                key = change.split(' = ')[0]
                text = '\n'.join(change if row.startswith(key + ' = ') else row for row in text.splitlines())
                p.write_text(text)
                with self.assertRaises(ValueError):
                    gen.load(p)

    def test_cmake_disabled_2040_and_build_location(self):
        with tempfile.TemporaryDirectory() as d:
            script = Path(d) / "test.cmake"
            script.write_text(f'include("{ROOT.as_posix()}/cmake/badger_content.cmake")\n'
                              'badger_generate_admission("${CMAKE_BINARY_DIR}/generated")\n')
            def run(*args):
                return subprocess.run(['cmake', *args, '-P', str(script)], cwd=d, capture_output=True)
            self.assertEqual(run().returncode, 0)
            rejected = run('-DBADGER_ADMISSION_FILE=fictional.toml', '-DBHIHKH_TARGET=badger2040')
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn(b'Admission requires Badger 2350', rejected.stderr)
            rejected = run('-DBADGER_ADMISSION_FILE=fictional.toml', '-DBHIHKH_TARGET=badger2350')
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn(b'build/private', rejected.stderr)

    def test_fictional_frame_decodes_exact_bytes(self):
        from PIL import Image
        from pyzbar.pyzbar import decode, ZBarSymbol
        binary = ROOT / 'build/host-badger2350/badger_tests'
        if not binary.exists():
            self.skipTest('build host tests first')
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'fictional.pbm'
            env = dict(os.environ, BADGER_ADMISSION_TEST_IMAGE=str(path))
            subprocess.run([str(binary), 'admission_pixels'], env=env, check=True, capture_output=True)
            image = Image.open(path)
            for scale in (1, 3):
                enlarged = image.resize((264 * scale, 176 * scale), Image.Resampling.NEAREST)
                self.assertEqual([x.data for x in decode(enlarged, symbols=[ZBarSymbol.QRCODE])], [PAYLOAD])
