# Badger 2350 case LEDs: isolated research notebook

Status: **research only**, no firmware implementation or hardware operations.

## Hardware baseline

The stock Pimoroni **Badger 2350** has **four independently controllable zones of white rear case lighting**, visible through its translucent case. These are **not RGB/NeoPixel LEDs**; animations must use intensity, timing, and zone sequencing rather than color. Confirmed from the manufacturer's Badger 2350 product page and Badgeware family documentation:

- https://shop.pimoroni.com/products/badger-2350
- https://github.com/pimoroni/badgeware-docs/blob/main/introduction/meet-your-badge.md
- https://github.com/pimoroni/badger2350

**Unknown / not yet verified for this repository:** exact stock Badger 2350 GPIO assignments, drive polarity, PWM implementation, electrical current limits, how existing native C++ firmware initializes these outputs, and available low-power hooks. **Do not import pins from Tufty 2350, the GitHub Universe custom badge, Blinky, or other RP2350 boards.** Obtain the matching Pimoroni schematic/board definitions and confirm integration before coding hardware drivers.

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

1. Locate/verify Pimoroni's **stock Badger 2350** schematic or board support files specifying LED pins and polarity.
2. Inventory existing code around board setup, GPIO ownership, power management, and periodic ticks.
3. Rank effects by visible appeal, battery overhead, and ease of test. Start with scanner and heartbeat in a host-only simulated prototype.
