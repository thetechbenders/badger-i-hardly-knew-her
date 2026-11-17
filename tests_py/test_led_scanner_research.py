"""Pure host-side tests for the isolated four-zone scanner research module."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from research.badger2350_led_scanner import OFF, ScanConfig, frame, text_preview  # noqa: E402


class ScannerResearchTests(unittest.TestCase):
    def test_off_by_default_and_guarded_cancellation(self):
        cfg = ScanConfig()
        self.assertEqual(frame(0, 0, cfg), OFF)
        self.assertEqual(frame(180, 0, cfg, enabled=True, low_battery=True), OFF)
        self.assertEqual(frame(180, 0, cfg, enabled=True, cancelled=True), OFF)

    def test_bouncing_head_sequence_no_trail(self):
        cfg = ScanConfig(step_ms=100, duration_ms=1200, peak_percent=35,
                         trail_percent=0)
        positions = [0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1]
        for step, zone in enumerate(positions):
            with self.subTest(step=step):
                f = frame(step * 100, 0, cfg, enabled=True)
                self.assertTrue(f.active)
                self.assertEqual(f.levels[zone], 35)
                self.assertEqual(sum(f.levels), 35)
                self.assertEqual(f.next_due_ms, (step + 1) * 100)
        self.assertEqual(frame(1200, 0, cfg, enabled=True), OFF)

    def test_tail_dimming_and_three_step_limit(self):
        cfg = ScanConfig(step_ms=100, trail_percent=50, peak_percent=32)
        self.assertEqual(frame(0, 0, cfg, enabled=True).levels, (32, 0, 0, 0))
        self.assertEqual(frame(100, 0, cfg, enabled=True).levels, (16, 32, 0, 0))
        self.assertEqual(frame(200, 0, cfg, enabled=True).levels, (8, 16, 32, 0))
        self.assertEqual(frame(300, 0, cfg, enabled=True).levels, (4, 8, 16, 32))
        self.assertEqual(frame(400, 0, cfg, enabled=True).levels, (0, 4, 32, 16))

    def test_tail_is_adjustable_and_brightness_bounded(self):
        for tail in (0, 1, 45, 100):
            cfg = ScanConfig(trail_percent=tail)
            for ms in range(0, cfg.duration_ms + 400, 17):
                state = frame(ms, 0, cfg, enabled=True)
                self.assertEqual(len(state.levels), 4)
                self.assertTrue(all(isinstance(x, int) and 0 <= x <= 35
                                    for x in state.levels))
        high = frame(540, 0, ScanConfig(trail_percent=100), enabled=True)
        self.assertTrue(all(v == 35 for v in high.levels))

    def test_wraparound_deadline_and_expiry(self):
        cfg = ScanConfig(step_ms=100, duration_ms=250, trail_percent=0)
        start = 0xFFFFFFF0
        self.assertEqual(frame(start, start, cfg, enabled=True).levels,
                         (35, 0, 0, 0))
        self.assertEqual(frame(84, start, cfg, enabled=True).levels,
                         (0, 35, 0, 0))
        self.assertEqual(frame(84, start, cfg, enabled=True).next_due_ms, 184)
        self.assertEqual(frame(234, start, cfg, enabled=True), OFF)
        self.assertEqual(frame(start - 1, start, cfg, enabled=True), OFF)

    def test_exact_timeout_and_partial_last_step(self):
        cfg = ScanConfig(step_ms=180, duration_ms=190)
        self.assertEqual(frame(180, 0, cfg, enabled=True).next_due_ms, 190)
        self.assertEqual(frame(189, 0, cfg, enabled=True).next_due_ms, 190)
        self.assertEqual(frame(190, 0, cfg, enabled=True), OFF)
        self.assertEqual(frame(0, 0, ScanConfig(duration_ms=0), enabled=True), OFF)

    def test_invalid_config_and_clocks(self):
        for kwargs in ({"step_ms": 0}, {"step_ms": 1001},
                       {"duration_ms": 30001}, {"peak_percent": 36},
                       {"trail_percent": -1}, {"trail_percent": 101},
                       {"step_ms": True}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                ScanConfig(**kwargs)
        for ms in (-1, 0x100000000, 1.0, True):
            with self.subTest(ms=ms), self.assertRaises(ValueError):
                frame(ms, 0, enabled=True)
        with self.assertRaises(TypeError):
            frame(0, 0, config=None, enabled=True)

    def test_retrigger_and_nonblocking_polling(self):
        cfg = ScanConfig(step_ms=100, duration_ms=300)
        first = frame(100, 0, cfg, enabled=True)
        self.assertEqual(first, frame(100, 0, cfg, enabled=True))
        self.assertEqual(frame(300, 0, cfg, enabled=True), OFF)
        restarted = frame(300, 300, cfg, enabled=True)
        self.assertEqual(restarted.levels, (35, 0, 0, 0))
        self.assertEqual(frame(301, 300, cfg, enabled=True).levels, restarted.levels)

    def test_preview_pure_and_stable(self):
        preview = text_preview(ScanConfig(step_ms=100, trail_percent=0), frames=6)
        self.assertIn("zone0", preview)
        self.assertIn(" 500 ", preview)
        self.assertIn("35/00/00/00", preview)
        with self.assertRaises(ValueError):
            text_preview(frames=0)


if __name__ == "__main__":
    unittest.main()
