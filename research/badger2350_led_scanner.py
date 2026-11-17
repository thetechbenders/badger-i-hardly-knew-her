"""Host-only four-zone Badger 2350 scanner experiment.

No GPIO, SDK, USB, firmware, device discovery, or blocking sleeps are used.
The four outputs are *logical zones*, not verified physical pin assignments.
"""
from __future__ import annotations

from dataclasses import dataclass

MASK32 = 0xFFFFFFFF
_ZONE_COUNT = 4
_SWEEP = (0, 1, 2, 3, 2, 1)


@dataclass(frozen=True)
class ScanConfig:
    """Finite, deliberately conservative research defaults.

    trail_percent controls how much light survives per step behind the head:
    0 = no tail, 100 = equally bright history. Up to three prior steps.
    """

    step_ms: int = 180
    duration_ms: int = 5400
    peak_percent: int = 35
    trail_percent: int = 45

    def __post_init__(self) -> None:
        for name, lo, hi in (
            ("step_ms", 60, 1000),
            ("duration_ms", 0, 30000),
            ("peak_percent", 0, 35),
            ("trail_percent", 0, 100),
        ):
            value = getattr(self, name)
            if type(value) is not int or not lo <= value <= hi:
                raise ValueError(f"{name} must be an integer in {lo}..{hi}")


@dataclass(frozen=True)
class ScanFrame:
    # Percent brightness for four *logical* zones.
    levels: tuple[int, int, int, int]
    # Unsigned 32-bit clock deadline; None means inactive, no further ticks.
    next_due_ms: int | None
    active: bool


OFF = ScanFrame((0, 0, 0, 0), None, False)


def _zone(step: int) -> int:
    return _SWEEP[step % len(_SWEEP)]


def frame(
    now_ms: int,
    start_ms: int,
    config: ScanConfig = ScanConfig(),
    *,
    enabled: bool = False,
    low_battery: bool = False,
    cancelled: bool = False,
) -> ScanFrame:
    """Evaluate a bounded effect without modifying external state.

    Caller must opt in for every invocation. Power/sleep policy should force
    enabled=False, or cancelled=True, at any unsafe transition. The API is
    pure: polling or cancellation never performs a device operation.

    now_ms and start_ms are unsigned 32-bit millisecond counters. The duration
    is < 2^31 ms so normal wraparound within the run is unambiguous.
    """
    if any(type(x) is not int or not 0 <= x <= MASK32 for x in (now_ms, start_ms)):
        raise ValueError("clock values must be unsigned 32-bit integers")
    if not isinstance(config, ScanConfig):
        raise TypeError("config must be ScanConfig")
    if not enabled or low_battery or cancelled:
        return OFF
    elapsed = (now_ms - start_ms) & MASK32
    if elapsed >= config.duration_ms:
        return OFF

    step = elapsed // config.step_ms
    levels = [0] * _ZONE_COUNT
    for lag in range(min(step, 3) + 1):
        # Integer rounded successive attenuation, capped by peak.
        brightness = config.peak_percent
        for _ in range(lag):
            brightness = (brightness * config.trail_percent + 50) // 100
        zone = _zone(step - lag)
        levels[zone] = max(levels[zone], brightness)

    until_step = config.step_ms - elapsed % config.step_ms
    until_end = config.duration_ms - elapsed
    due = (now_ms + min(until_step, until_end)) & MASK32
    return ScanFrame(tuple(levels), due, True)


def text_preview(config: ScanConfig = ScanConfig(), *, frames: int = 18) -> str:
    """Deterministic text-only preview, never sleeps or talks to a device."""
    if type(frames) is not int or not 1 <= frames <= 128:
        raise ValueError("frames must be in 1..128")
    rows = [" ms   zone0    zone1    zone2    zone3   (% brightness)"]
    for n in range(frames):
        now = n * config.step_ms
        state = frame(now, 0, config, enabled=True)
        bars = ["#" * ((level * 8 + 17) // 35) if level else "."
                for level in state.levels]
        rows.append(f"{now:4d}   " + " ".join(f"{b:<8}" for b in bars)
                    + "  " + "/".join(f"{v:02d}" for v in state.levels))
    return "\n".join(rows)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Host-only four-zone LED scanner preview")
    parser.add_argument("--trail", type=int, default=45, help="per-step tail retention 0..100")
    parser.add_argument("--peak", type=int, default=35, help="peak brightness 0..35 percent")
    parser.add_argument("--step-ms", type=int, default=180, help="step duration 60..1000")
    parser.add_argument("--duration-ms", type=int, default=5400, help="maximum duration 0..30000")
    parser.add_argument("--frames", type=int, default=18, help="preview samples 1..128")
    args = parser.parse_args()
    config = ScanConfig(args.step_ms, args.duration_ms, args.peak, args.trail)
    print(text_preview(config, frames=args.frames))
