# Badger 2350 case LEDs: isolated research notebook

Status: **research only**, no firmware implementation or hardware operations.

## Verified hardware findings (2026-11-17)

**Target:** retail Pimoroni Badgeware Badger 2350 (WARE001), using the board mapping actually selected by this repository: `PICO_BOARD=pimoroni_badger2350`, Pico SDK **2.3.1**. Do not confuse this with the GitHub Universe derivative or Tufty.

### Manufacturer-backed evidence

1. [Pimoroni product specification](https://shop.pimoroni.com/products/badger-2350) confirms four-zone monochrome case lighting.
2. [Pimoroni Badgeware API, Lighting](https://github.com/pimoroni/badgeware-docs/blob/main/api/badge.md#lighting) documents four independently settable rear white LEDs and `badge.caselights(level1, level2, level3, level4)`, where individual levels are floats in **0..1**. This is **MicroPython's API**, not a callable interface in our native C++ firmware.
3. [Pimoroni's C++ Badger board header](https://github.com/pimoroni/badgeware-cpp/blob/main/board/badger/pimoroni_badger2350.h) defines `BW_LED_0..3` as **GPIO0, GPIO1, GPIO2, GPIO3**, respectively.
4. More directly for our target, [Raspberry Pi Pico SDK 2.3.1 stock board header](https://github.com/raspberrypi/pico-sdk/blob/2.3.1/src/boards/include/boards/pimoroni_badger2350.h) defines `BADGER2350_LED_0_PIN=0`, `LED_1_PIN=1`, `LED_2_PIN=2`, `LED_3_PIN=3`. This is the header already selected in `firmware/platform/badger2350/target.cmake`.
5. [Pimoroni's release notes](https://github.com/pimoroni/badger2350/releases) say `badge.caselights()` replaced `set_caselights()` and uses gamma correction in vendor MicroPython. A native C++ implementation must **not** assume that gamma correction happens automatically.
6. [Pimoroni's introduction](https://github.com/pimoroni/badgeware-docs/blob/main/introduction/meet-your-badge.md#backlights) confirms four white LEDs behind the translucent case.

| Logical case zone | Native SDK macro | GPIO | Confidence |
| --- | --- | ---: | --- |
| 0 | `BADGER2350_LED_0_PIN` | 0 | Pico SDK v2.3.1 board header + Pimoroni C++ header |
| 1 | `BADGER2350_LED_1_PIN` | 1 | Same |
| 2 | `BADGER2350_LED_2_PIN` | 2 | Same |
| 3 | `BADGER2350_LED_3_PIN` | 3 | Same |

**Repository integration:** `firmware/platform/badger2350/board.hpp` explicitly says the rear LEDs are not used; `board.cpp` implements `void led(uint8_t) {}` and the target currently only links the board, display, ADC, I2C, flash, diagnostics and other existing facilities. The `power_off()` path changes **all GPIOs to SIO input with pulls** before deep power-down, including GPIO0..3, so hardware LED integration must explicitly turn the four channels off **before** entering power-down and must not override the existing pad-restoration sequence. The front display uses SPI GPIO16..21, not the case LED GPIOs, per the Pico SDK board header.

**Unverified and not to be guessed:**
- Electrical topology, drive polarity, current-limiting resistance, current per LED, and whether a direct RP2350 PWM duty of 0 is electrically off. A retail WARE001 schematic was not located in this research pass.
- Physical left-to-right/corner order of zone indices 0..3 behind the installed case. Labels are logical, not established physical positions.
- Hardware PWM resource/slice allocation and clock impact in this firmware, and measured USB/battery power draw. Per-LED brightness range is a documented vendor **API behavior**, not proof of a specific electrical driver topology.
- Whether gamma correction should be implemented in the effect engine, driver, or deliberately omitted, pending visual/electrical tests.

**Next safe gate:** use the four verified stock-board GPIO macros, but only after a separate authorized implementation scope; start with a low-duty USB-powered fixture, verify polarity and ordering empirically before showing an animation. Do not transplant pins from the GitHub Universe derivative or unrelated RP2350 boards.

## Research ideas to prototype

| Experiment | Four-zone effect | Trigger | Validation goal |
| --- | --- | --- | --- |
| Soft heartbeat | Gentle global pulse with bounded brightness | User-controlled opt-in | No conspicuous strobe; consistent power use |
| Scanner | Single moving highlight across four zones, bounce at ends | Demo mode | Nonblocking timing and no ghosted zone |
| Comet | Bright lead plus dim fading trailing zones | Demo mode | Per-zone intensity and smooth step timing |
| Badge handshake | Two quick pulses, pause, confirmation pulse | Successful in-app action | Clear finite event feedback; no misleading success |
| Battery whisper | Subtle one-shot level cue, never constant bright drain | Explicit button request | Works on battery; low-voltage policy |
| USB dock glow | Slow gentle pulse while USB is present | USB-connected state | Does not interfere with CDC/USB timing |
| Secret sequence | A four-zone animated easter egg | Explicit button pattern | Does not conflict with navigation/hold shortcuts |
| Minimal Morse | Short finite patterns for a user-selected short message | Deliberate demo command | Bounded runtime; not a noisy persistent notifier |
| Idle breathing | Low-brightness, infrequent pulse | Explicit enabled idle mode | Honors existing sleep and power policy |

These are **hypotheses / design sketches**, not measured capabilities or finished features. No sound, accelerometer, touch sensor, or wireless trigger should be assumed present or usable without repository and hardware confirmation.

## Proposed implementation architecture (unapproved)

1. Inspect the native target board abstraction and identify any already-supported case LED driver. Map the four logical zones to actual GPIO only after checking the **stock Badger 2350** schematic or authoritative board header.
2. Provide a host-testable pure effect engine: `tick(now_ms, state) -> four bounded brightness values + next_due_ms`. Keep state size small, do not allocate in ISR, and never block the main loop.
3. Keep the physical driver behind a 2350-only interface; on the Badger 2040, compile out the feature or provide an inert stub. Preserve existing e-paper refresh and power-cut invariants.
4. Add sensible brightness caps, finite animation durations, an immediate all-off path, and an explicit opt-in. Default LEDs to **off**, including startup, USB attach, error paths, and sleep, until safe power behavior is established.
5. Treat USB `Can't continue xfer on inactive ep 80` as an **existing independent unresolved issue**. Avoid introducing periodic USB traffic or coupling animations to TinyUSB callbacks in early prototypes.
6. Host-test sequences, wraparound timing, low-battery fallback, all-off behavior, cancellation, and accidental rapid retriggering. The test harness should simulate LED values without touching hardware.
7. Only after independent pin/electrical verification and explicit permission, construct a separately authorized device test with USB power first, low duty cycle, measured current, then battery testing.

## Host-only scanner prototype

The first isolated experiment is implemented in
[`research/badger2350_led_scanner.py`](../../research/badger2350_led_scanner.py)
with [`tests_py/test_led_scanner_research.py`](../../tests_py/test_led_scanner_research.py).

Run without special dependencies or connected hardware:

```bash
python3 -m unittest tests_py.test_led_scanner_research -v
python3 research/badger2350_led_scanner.py --trail 45 --peak 35 --step-ms 180 --duration-ms 5400 --frames 18
python3 research/badger2350_led_scanner.py --trail 0 --frames 12   # no trail
python3 research/badger2350_led_scanner.py --trail 80 --frames 12  # long visible tail
```

The preview prints four logical LED brightness channels and never touches a board.
`frame(now_ms, start_ms, config, enabled=True)` is a deterministic function
returning four integer brightness percentages, an unsigned 32-bit next tick
deadline, and an active flag. It follows a bouncing `0→1→2→3→2→1` sweep,
blends up to three previous positions with configurable integer attenuation,
enforces a 35% default/maximum brightness, and stops at 5.4 seconds by default
(30 seconds maximum). It can immediately return all-off for `cancelled=True`,
`low_battery=True`, or the default `enabled=False`. Repeated calls at the
same timestamp return identical output; caller state (not this module) decides
if an event can retrigger.

These brightness values represent **simulated duty percentages**, not actual
validated PWM duty cycles. No electrical or optical calibration has occurred.
No pin definitions, SDK hooks, settings changes, firmware target, or USB operations
are introduced here. Hardware integration remains a separate authorization gate.

## Acceptance questions

- What are the verified GPIOs, active levels, drive characteristics, and whether PWM is safe on all four zones?
- Is there an existing firmware LED abstraction or are GPIOs currently used for something else?
- What current draw and wake/sleep impact do representative intensities have?
- Which animations look distinct through the *actual enclosure*?
- Do LED updates impact e-paper, USB, battery measurement, or power-down?
- How will a user enable/disable an effect without persisting a potentially distracting default?

## Research boundaries and separation

- Keep all research on `research/badger2350-case-led-effects`, branched from `main`, not PR #28.
- No firmware image, UF2, flashing, USB commands, device tests, or persistent configuration changes authorized by this notebook.
- Avoid modifying currently working badges or replacing settings defaults.
- No PR/merge/release implied. A later implementation requires a separately scoped decision, tests, and hardware authorization.

## Next research tasks

1. **GPIO pin mapping verified** in manufacturer C++ and Pico SDK 2.3.1 board headers; acquire the retail WARE001 schematic or otherwise verify electrical polarity/current before a hardware driver.
2. Inventory existing code around board setup, GPIO ownership, power management, and periodic ticks.
3. Rank effects by visible appeal, battery overhead, and ease of test. Start with scanner and heartbeat in a host-only simulated prototype.
