# Refresh phases: why the panel flashes

When a screen changes, the panel briefly shows an inverted or near-black
image before the new screen appears.

> **All durations here are estimates** derived from the driver's waveform
> tables (frame counts ÷ the 100 Hz frame rate it configures). None has been
> measured on the physical badge yet; `diag refresh` (below) is how to
> measure them, and this page should be updated with the results. This note explains where that comes
from, what the firmware does and does not add, and how to measure it on a
badge.

## Evidence from the firmware (host-verified)

- **One refresh per navigation step.** The scheduler renders only after the
  previous frame is finished on the panel. It coalesces requests that arrive
  during a refresh and suppresses unchanged frames. A scripted session in
  `host/tests/test_pipeline.cpp`
  (`pipeline_refresh_trace_reasons_and_one_refresh_per_step`) issues 7
  navigation requests and sees exactly 7 panel refreshes. There is no extra
  clear or blank frame between screens.
- **Mode per step.** A frame is a partial refresh only if the changed
  bounding box covers less than 40 % of the screen. Measured on the rendered
  sample pages, every step changes 85–96 % of the screen:

  | Step | Changed area | Mode |
  |---|---|---|
  | badge → card | 100 % | full |
  | card → project 1 | 96 % | full |
  | project n → n+1 | 85–91 % | full |
  | project → its QR | 96 % | full |

  Each step is therefore a single full refresh, which is expected. Lowering
  the threshold would not help: a partial window that covers 90 % of the
  screen runs the same waveform over almost the same area.
- **Clean refresh** (A long press, `refresh clean`, recovery after a panel
  fault) uses the controller's OTP waveform (speed 0, estimated at about
  4.5 s). It runs
  only when asked for, or once after a fault.

## Where the flash comes from: the waveform

The firmware uses the pinned pimoroni-pico `UC8151_Legacy` LUTs.
`setup()` always ends with `PLL HZ_100`, so every speed runs at 100 frames
per second. That includes fast and turbo: their LUT functions set 200 Hz
first, but `setup()` overrides it. Each LUT row has four phase lengths and a
repeat count:

| Speed | Row 1 | Row 2 | Row 3 | Frames | Estimated time at 100 Hz |
|---|---|---|---|---|---|
| 1 medium (default) | 22+22+13 = 57 × 1 | 35+35 = 70 × 2 | 57 × 1 | 254 | ≈ 2.5 s |
| 2 fast | 4+4+7 = 15 × 1 | 12+12 = 24 × 2 | 15 × 2 | 93 | ≈ 0.9 s |
| 3 turbo | 1+1+2 = 4 × 1 | 2+2 = 4 × 2 | 7 × 2 | 26 | ≈ 0.26 s |

For speed 1, the three rows are what you see (durations estimated):

1. **Row 1, about 0.57 s.** Each pixel is driven *away* from its target
   colour (`LUT_WW`/`LUT_BW` 0x54, `LUT_WB`/`LUT_BB` 0xA8: opposite
   polarities). The screen shows a roughly inverted image.
2. **Row 2, about 1.4 s.** Every pixel is driven alternately to both rails
   (0x60), twice. The screen goes dark or patchy: this is the "blank"
   phase.
3. **Row 3, about 0.57 s.** Each pixel is driven to its target colour, and
   the new screen appears.

Rows 1 and 2 are not decoration. They keep the drive DC-balanced, which
protects the panel's long-term health. They also shake the particles loose
from their previous position, which removes ghosting. Skipping them is what
makes turbo refreshes ghost. The firmware therefore does **not** remove or
shorten them, and it keeps the clean (OTP) refresh and the post-fault clean
redraw.

## Choices that are available without code changes

- `refresh.speed 2` (fast): one full refresh is estimated at about 0.9 s, with
  shorter flash phases. Ghosting is more likely, and a periodic A long press
  (clean refresh) removes it.
- `refresh.speed 1` (default): estimated at about 2.5 s. It is the default
  because it suits a mostly static badge; whether it is the best balance on
  this panel is still to be judged on the badge.

The choice should be made on the physical badge, since ghosting depends on
the panel and the temperature.

## Measuring on the badge (pending: physical test)

`diag refresh` prints the last 16 frames: sequence, request time, wait
(request → submit), panel BUSY time, speed, mode and the reason the display
service chose that mode:

```
> diag refresh
refresh: last 3 frames (ms since boot; wait = request->submit, busy = panel BUSY time)
refresh: #1 req 412 wait 0 busy 2561 speed 1 full (first frame)
refresh: #2 req 9050 wait 3 busy 2558 speed 1 full (large change)
refresh: #3 req 15102 wait 2 busy 2559 speed 1 full (large change)
```

The values above are illustrative, not measured. To compare against a video
of the badge, browse a few screens, then run `diag refresh`. Expect one entry
per press, `busy` close to the frame count above, and no entries between
presses. An entry nobody asked for, or a `wait` longer than one refresh,
would point to a scheduler problem. The host tests show neither.
