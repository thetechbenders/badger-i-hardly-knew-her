"""Hardware-target selection (BHIHKH_TARGET) and the per-target facts the
post-build scripts use.

The configure tests stop at cmake/bhihkh_target.cmake, before the Pico SDK
is imported, so they need CMake but neither the SDK nor a cross-compiler
(where a pinned SDK is present in deps/, a valid target configures fully).
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


def cmake_list(name: str) -> list[str]:
    text = (ROOT / "cmake/bhihkh_targets_list.cmake").read_text()
    return re.search(rf"set\({name} ([^)]*)\)", text).group(1).split()


@unittest.skipUnless(shutil.which("cmake"), "cmake not installed")
class TargetSelection(unittest.TestCase):
    def configure(self, *args, build: str | None = None):
        with tempfile.TemporaryDirectory() as d:
            res = subprocess.run(["cmake", "-S", str(ROOT), "-B", build or d, *args], capture_output=True, text=True)
            generated = sorted(p.name for p in Path(build or d).iterdir() if p.name in ("build.ninja", "Makefile"))
        return res.returncode, " ".join((res.stdout + res.stderr).split()), generated

    def test_badger2350_selects_the_rp2350_board(self):
        rc, out, _ = self.configure("-DBHIHKH_TARGET=badger2350")
        self.assertIn("BHIHKH! target: badger2350 (PICO_BOARD=pimoroni_badger2350, PICO_PLATFORM=rp2350-arm-s)", out)
        self.assertNotIn("PICO_BOARD=pimoroni_badger2040", out)

    def test_default_is_the_badger2040(self):
        rc, out, _ = self.configure()
        self.assertIn("BHIHKH! target: badger2040 (PICO_BOARD=pimoroni_badger2040, PICO_PLATFORM=rp2040)", out)

    def test_unknown_and_empty_targets_fail(self):
        for value in ("pico2", "Badger2040", "badger2350a", ""):
            with self.subTest(value=value):
                rc, out, generated = self.configure(f"-DBHIHKH_TARGET={value}")
                self.assertNotEqual(rc, 0)
                self.assertIn(f"unknown BHIHKH_TARGET '{value}'", out)
                self.assertIn("implemented: badger2040;badger2350", out)
                self.assertNotIn("BHIHKH! target:", out)  # no fallback to another board
                self.assertEqual(generated, [])

    def test_conflicting_board_or_platform_is_refused(self):
        cases = (("-DPICO_BOARD=pimoroni_badger2350", None, "PICO_BOARD=pimoroni_badger2350 conflicts",
                  "with BHIHKH_TARGET=badger2040"),
                 ("-DPICO_PLATFORM=rp2350", None, "PICO_PLATFORM=rp2350 conflicts", "with BHIHKH_TARGET=badger2040"),
                 ("-DPICO_BOARD=pimoroni_badger2040", "badger2350", "PICO_BOARD=pimoroni_badger2040 conflicts",
                  "with BHIHKH_TARGET=badger2350"))
        for arg, target, msg, which in cases:
            with self.subTest(arg=arg, target=target):
                extra = [f"-DBHIHKH_TARGET={target}"] if target else []
                rc, out, generated = self.configure(arg, *extra)
                self.assertNotEqual(rc, 0)
                self.assertIn(msg, out)
                self.assertIn(which, out)
                self.assertEqual(generated, [])

    def test_build_directory_cannot_switch_board(self):
        # A directory configured for one board refuses the other (simulated
        # with the cache entry the first configure leaves behind).
        rc, out, generated = self.configure("-DBHIHKH_TARGET=badger2350",
                                            "-DBHIHKH_TARGET_CONFIGURED:INTERNAL=badger2040")
        self.assertNotEqual(rc, 0)
        self.assertIn("this build directory is configured for BHIHKH_TARGET=badger2040", out)
        self.assertIn("build badger2350 in a separate directory", out)
        self.assertEqual(generated, [])


class TargetFacts(unittest.TestCase):
    def test_both_targets_implemented_default_unchanged(self):
        self.assertEqual(bhihkh_targets.DEFAULT, "badger2040")
        self.assertEqual(sorted(bhihkh_targets.TARGETS), ["badger2040", "badger2350"])
        self.assertEqual(sorted(cmake_list("BHIHKH_TARGETS_IMPLEMENTED")), sorted(bhihkh_targets.TARGETS))
        cmake = (ROOT / "cmake/bhihkh_target.cmake").read_text()
        self.assertIn("set(BHIHKH_TARGET badger2040 CACHE STRING", cmake)
        self.assertNotIn("PLANNED", cmake)
        host = (ROOT / "host/CMakeLists.txt").read_text()
        self.assertIn("set(BHIHKH_TARGET badger2040 CACHE STRING", host)

    def test_every_target_has_a_backend(self):
        for name in cmake_list("BHIHKH_TARGETS_IMPLEMENTED"):
            for f in ("target.cmake", "board.hpp", "board.cpp", "display_target.hpp", "flash_layout.hpp"):
                self.assertTrue((ROOT / f"firmware/platform/{name}/{f}").is_file(), f"{name}/{f}")

    def test_facts_match_the_firmware(self):
        for t in bhihkh_targets.TARGETS.values():
            with self.subTest(target=t.name):
                layout = (ROOT / t.flash_layout_header).read_text()
                mib = t.flash_size // (1024 * 1024)
                self.assertIn(f"constexpr uint32_t kFlashSize = {mib}u * 1024 * 1024;", layout)
                self.assertIn(f"kAssetOffset = 0x{t.asset_off:X};", layout)
                self.assertIn(f"kAssetSize = 0x{t.asset_size:X};", layout)
                self.assertIn(f"kSettingsOffset = 0x{t.settings_off:X};", layout)
                self.assertEqual(t.settings_off + t.settings_size, t.flash_size)
                display = (ROOT / t.display_header).read_text()
                self.assertIn(f"constexpr int kDisplayWidth = {t.display_w};", display)
                self.assertIn(f"constexpr int kDisplayHeight = {t.display_h};", display)
                self.assertRegex(display, rf"constexpr int kPortraitWidth = {t.portrait_w};")
                self.assertRegex(display, rf"constexpr int kPortraitHeight = {t.portrait_h};")
                tc = (ROOT / f"firmware/platform/{t.name}/target.cmake").read_text()
                self.assertIn(f'set(BHIHKH_TARGET_DESCRIPTION "{t.description}")', tc)
                self.assertIn(f"PICO_FLASH_SIZE_BYTES={t.flash_size}", tc)
                self.assertIn(f"set(BHIHKH_TARGET_ARTIFACT_PREFIX {t.artifact})", tc)
                build = (ROOT / "scripts/build-firmware.sh").read_text()
                self.assertIn(f"{t.name}) default_build=\"$root/{t.default_build_dir}\"; prefix={t.artifact} ;;", build)

    def test_badger2040_board_and_chip(self):
        t = bhihkh_targets.TARGETS["badger2040"]
        self.assertEqual(t.uf2_family, uf2.RP2040_FAMILY_ID)
        self.assertIsNone(t.uf2_abs_block)
        self.assertEqual(t.artifact, "badger_badge")  # unchanged since the first release
        tc = (ROOT / "firmware/platform/badger2040/target.cmake").read_text()
        self.assertIn("set(BHIHKH_PICO_BOARD pimoroni_badger2040)", tc)
        self.assertIn("set(BHIHKH_PICO_PLATFORM rp2040)", tc)

    def test_badger2350_board_and_chip(self):
        t = bhihkh_targets.TARGETS["badger2350"]
        self.assertEqual(t.chip, "RP2350A")
        self.assertEqual(t.uf2_family, uf2.RP2350_ARM_S_FAMILY_ID)
        self.assertEqual(t.uf2_abs_block, 0x10FFFF00)
        self.assertEqual((t.scratch_x, t.scratch_y), (0x20080000, 0x20081000))
        tc = (ROOT / "firmware/platform/badger2350/target.cmake").read_text()
        self.assertIn("set(BHIHKH_PICO_BOARD pimoroni_badger2350)", tc)
        self.assertIn("set(BHIHKH_PICO_PLATFORM rp2350-arm-s)", tc)
        self.assertNotEqual(t.artifact, bhihkh_targets.TARGETS["badger2040"].artifact)

    def test_sdk_pin_has_the_board_header(self):
        lock = (ROOT / "deps.lock").read_text()
        self.assertIn("PICO_SDK_TAG=2.3.1", lock)
        self.assertIn("PICO_SDK_COMMIT=079c6f39023649b154152db30f1d781e884879bc", lock)
        self.assertIn("PICOTOOL_TAG=2.3.1", lock)
        sdk = ROOT / "deps/pico-sdk/src/boards/include/boards/pimoroni_badger2350.h"
        if sdk.exists():  # fetched (firmware builds): the pinned SDK really has it
            text = sdk.read_text()
            for pin in ("BADGER2350_INKY_BUSY_PIN 16", "BADGER2350_SW_INT_PIN 15", "BADGER2350_VBUS_DETECT_PIN 12",
                        "BADGER2350_VBAT_SENSE_PIN 26", "BADGER2350_SENSE_1V1_PIN 28", "PICO_RP2350A 1"):
                self.assertIn(pin, text)

    def test_scripts_refuse_targets_they_have_no_facts_for(self):
        for main, args in ((verify_artifacts.main, ["build/fw"]),
                           (memory_report.main, ["--elf", "x.elf", "--out", "x.txt"])):
            err = io.StringIO()
            with self.subTest(script=main.__module__), self.assertRaises(SystemExit) as cm, \
                    contextlib.redirect_stderr(err):
                main([*args, "--target", "badger2350b"])
            self.assertEqual(cm.exception.code, 2)
            self.assertIn("invalid choice: 'badger2350b'", err.getvalue())


class Uf2Families(unittest.TestCase):
    def test_e10_block_matches_picotool(self):
        b = uf2.abs_block(0x10FFFF00)
        self.assertEqual(len(b), 512)
        self.assertTrue(uf2.is_abs_block(b))
        # Header fields as picotool 2.3.1 gen_abs_block(): flags, addr, size, block 0 of 2, family absolute.
        import struct
        self.assertEqual(struct.unpack("<8I", b[:32])[2:], (0x2000 | 0x8000, 0x10FFFF00, 256, 0, 2, 0xE48BFF57))
        self.assertEqual(b[32:288], b"\xef" * 256)
        self.assertEqual(struct.unpack("<I", b[288:292])[0], 0x9957E304)  # RP2_IGNORE_BLOCK

    def test_rp2350_uf2_skips_e10_block_when_parsed(self):
        blob = uf2.to_uf2(b"\x01" * 300, 0x10FE0000, uf2.RP2350_ARM_S_FAMILY_ID, 0x10FFFF00)
        self.assertEqual(len(blob), 3 * 512)
        self.assertTrue(uf2.is_abs_block(blob[:512]))
        self.assertEqual(sorted(uf2.from_uf2(blob)), [0x10FE0000, 0x10FE0100])
        self.assertFalse(uf2.is_abs_block(blob[512:1024]))


if __name__ == "__main__":
    unittest.main()
