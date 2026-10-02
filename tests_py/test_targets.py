"""Hardware-target selection (BHIHKH_TARGET) and the per-target facts the
post-build scripts use.

The configure tests stop at cmake/bhihkh_target.cmake, before the Pico SDK
is imported, so they need CMake but neither the SDK nor a cross-compiler.
"""
from __future__ import annotations

import contextlib
import io
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))

import bhihkh_targets  # noqa: E402
import memory_report  # noqa: E402
import uf2  # noqa: E402
import verify_artifacts  # noqa: E402


@unittest.skipUnless(shutil.which("cmake"), "cmake not installed")
class TargetSelection(unittest.TestCase):
    def configure(self, *args):
        with tempfile.TemporaryDirectory() as d:
            res = subprocess.run(["cmake", "-S", str(ROOT), "-B", d, *args], capture_output=True, text=True)
            generated = sorted(p.name for p in Path(d).iterdir() if p.name in ("build.ninja", "Makefile"))
        return res.returncode, " ".join((res.stdout + res.stderr).split()), generated

    def test_planned_badger2350_fails_instead_of_building_classic(self):
        rc, out, generated = self.configure("-DBHIHKH_TARGET=badger2350")
        self.assertNotEqual(rc, 0)
        self.assertIn("BHIHKH_TARGET=badger2350 is planned but not implemented yet", out)
        self.assertIn("the only buildable target is badger2040", out)
        self.assertNotIn("BHIHKH! target:", out)
        self.assertEqual(generated, [])

    def test_unknown_and_empty_targets_fail(self):
        for value in ("pico2", "Badger2040", ""):
            with self.subTest(value=value):
                rc, out, generated = self.configure(f"-DBHIHKH_TARGET={value}")
                self.assertNotEqual(rc, 0)
                self.assertIn(f"unknown BHIHKH_TARGET '{value}'", out)
                self.assertIn("implemented: badger2040; planned: badger2350", out)
                self.assertEqual(generated, [])

    def test_conflicting_board_or_platform_is_refused(self):
        for arg, msg in (("-DPICO_BOARD=pimoroni_badger2350", "PICO_BOARD=pimoroni_badger2350 conflicts"),
                         ("-DPICO_PLATFORM=rp2350", "PICO_PLATFORM=rp2350 conflicts")):
            with self.subTest(arg=arg):
                rc, out, generated = self.configure(arg)
                self.assertNotEqual(rc, 0)
                self.assertIn(msg, out)
                self.assertIn("with BHIHKH_TARGET=badger2040", out)
                self.assertEqual(generated, [])


class TargetFacts(unittest.TestCase):
    def test_default_is_the_only_implemented_target(self):
        self.assertEqual(bhihkh_targets.DEFAULT, "badger2040")
        self.assertEqual(sorted(bhihkh_targets.TARGETS), ["badger2040"])
        cmake = (ROOT / "cmake/bhihkh_target.cmake").read_text()
        self.assertIn("set(BHIHKH_TARGETS_IMPLEMENTED badger2040)", cmake)
        self.assertIn("set(BHIHKH_TARGET badger2040 CACHE STRING", cmake)

    def test_every_implemented_target_has_a_backend(self):
        cmake = (ROOT / "cmake/bhihkh_target.cmake").read_text()
        implemented = re.search(r"set\(BHIHKH_TARGETS_IMPLEMENTED ([^)]*)\)", cmake).group(1).split()
        self.assertEqual(sorted(implemented), sorted(bhihkh_targets.TARGETS))
        for name in implemented:
            self.assertTrue((ROOT / f"firmware/platform/{name}/target.cmake").is_file(), name)
        planned = re.search(r"set\(BHIHKH_TARGETS_PLANNED ([^)]*)\)", cmake).group(1).split()
        for name in planned:
            self.assertFalse((ROOT / f"firmware/platform/{name}").exists(), f"{name} is planned, not implemented")

    def test_badger2040_facts_match_the_firmware(self):
        t = bhihkh_targets.TARGETS["badger2040"]
        layout = (ROOT / t.flash_layout_header).read_text()
        self.assertIn("constexpr uint32_t kFlashSize = 2u * 1024 * 1024;", layout)
        self.assertEqual(t.flash_size, 2 * 1024 * 1024)
        self.assertIn(f"kAssetOffset = 0x{t.asset_off:X};", layout)
        self.assertIn(f"kAssetSize = 0x{t.asset_size:X};", layout)
        self.assertIn(f"kSettingsOffset = 0x{t.settings_off:X};", layout)
        self.assertEqual(t.settings_off + t.settings_size, t.flash_size)
        self.assertEqual(t.uf2_family, uf2.RP2040_FAMILY_ID)
        tc = (ROOT / "firmware/platform/badger2040/target.cmake").read_text()
        self.assertIn("set(BHIHKH_PICO_BOARD pimoroni_badger2040)", tc)
        self.assertIn("set(BHIHKH_PICO_PLATFORM rp2040)", tc)
        self.assertIn(f'set(BHIHKH_TARGET_DESCRIPTION "{t.description}")', tc)
        self.assertIn(f"PICO_FLASH_SIZE_BYTES={t.flash_size}", tc)

    def test_scripts_refuse_targets_they_have_no_facts_for(self):
        for main, args in ((verify_artifacts.main, ["build/fw"]),
                           (memory_report.main, ["--elf", "x.elf", "--out", "x.txt"])):
            err = io.StringIO()
            with self.subTest(script=main.__module__), self.assertRaises(SystemExit) as cm, \
                    contextlib.redirect_stderr(err):
                main([*args, "--target", "badger2350"])
            self.assertEqual(cm.exception.code, 2)
            self.assertIn("invalid choice: 'badger2350'", err.getvalue())


if __name__ == "__main__":
    unittest.main()
