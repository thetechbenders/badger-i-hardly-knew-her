# Architecture

## Hardware targets

BHIHKH! is native C/C++ on the Raspberry Pi Pico SDK, and the build selects
one hardware target with `-DBHIHKH_TARGET=<name>` (`cmake/bhihkh_target.cmake`):

| `BHIHKH_TARGET` | Hardware | State |
|---|---|---|
| `badger2040` (default) | original Pimoroni Badger 2040 (RP2040) | implemented: everything in this document |
| `badger2350` | Pimoroni Badger 2350 (RP2350A), working name BadgHer™ NEO (name not final; the ™ is a joke, not a trademark claim) | planned, **not implemented** |

Selecting `badger2350`, or any name that is not implemented, fails at
configure time with a message saying so; it never falls back to building
Badger 2040 firmware. `PICO_BOARD` and `PICO_PLATFORM` follow from the
target, and a conflicting value is refused rather than ignored.

Each implemented target is a backend directory, `firmware/platform/<target>/`,
with a `target.cmake` naming its Pico SDK board and platform, sources,
compile definitions and SDK libraries. `scripts/bhihkh_targets.py` holds the
same target's flash map, UF2 family and linker facts for `memory_report.py`
and `verify_artifacts.py` (`--target`, default `badger2040`; the verifier also
checks the build directory's CMake cache). `core/` and `cli/` have no target
conditionals.

The Badger 2350 port will stay on the Pico SDK. Pimoroni's BadgeWare
(MicroPython) and `pimoroni/badger2350` sources are hardware reference
only, not the runtime. Seams the port has to open, deliberately left as they
are today:

- **Display.** `Framebuffer` is the UC8151's 296×128 1-bpp column-major
  layout, its `diff_bounds()` rounds to the UC8151 8-row partial window, and
  the renderer, layouts, portrait pipeline, asset packs, host previews and
  `tools/` (296×128, 104×128 portrait) are drawn for that panel. The
  Badger 2350's 264×176 four-tone SSD1680 needs its own framebuffer format,
  layouts and refresh policy: a display port, not a geometry constant.
- **Composition root.** `badger2040/main.cpp` wires the shared core to the
  RP2040 drivers. Much of it (CLI host, settings service, the main loop) will
  apply to the RP2350 too; it should be split when a second backend needs
  it, not before.
- **Power and battery.** `board` (power latch, VBUS, 1.24 V-referenced ADC
  battery sense) and `core/battery` (no charger) describe the original
  board. A charger, RTC, wake causes and the extra peripherals (rear lights,
  PSRAM, wireless) need real interfaces when they are implemented.
- **Flash and diagnostics.** `flash_layout.hpp` is the 2 MiB map;
  `diagnostics.cpp` reads RP2040 reset registers and scratch-bank stacks.
- **Board header / SDK pin.** The pinned pico-sdk 2.2.0 predates Badger 2350
  support. Current upstream pico-sdk now includes `pimoroni_badger2350` and
  identifies the MCU as RP2350A. The port should evaluate a controlled SDK
  pin update before maintaining a redundant local board header; verify the
  physical board during bring-up because one BadgeWare intro page currently
  says RP2350B.
- **Artifacts.** Artifact names (`badger_badge*.uf2`) and the build
  directory (`build/fw`) do not carry the target yet.

## Hardware facts this design relies on (`badger2040`)

Verified against the Pico SDK board header `boards/pimoroni_badger2040.h`
(SDK 2.2.0), `pimoroni-pico` v1.29.0-2 (`libraries/badger2040`,
`drivers/uc8151_legacy`) and `pimoroni/badger2040` (MicroPython `badger2040.py`,
`wakeup` module, launcher history).

| Function | GPIO | Notes |
|---|---|---|
| Buttons A, B, C, UP, DOWN | 12, 13, 14, 15, 11 | Active high, internal pull-downs. They also switch on the 3V3 regulator through diodes, which is how the board wakes on battery. |
| USR / BOOT | 23 | Active low (pull-up). Held at reset = USB bootloader, so it is not usable as a boot option. |
| 3V3 enable (power latch) | 10 | Must be driven high immediately after boot; low = power off on battery |
| E-paper (UC8151, 296×128) | SPI0: CS 17, SCK 18, MOSI 19, DC 20, RESET 21, BUSY 26 (low = busy) | |
| VBUS detect | 24 | High when USB power is present |
| Battery sense | 29 (ADC3, 1/3 divider) + 1.24 V ref on 28 (ADC2), enabled by 27 | Method from Pimoroni's launcher |
| White LED | 25 (PWM) | |
| Qwiic (I2C0) | SDA 4, SCL 5, 3V3, GND | Four-wire: no interrupt line. Optional APDS-9960 at 0x39, polled. |
| Flash | 2 MiB W25Q16, boot2 `w25q080` | |

There is no RTC (only the Badger 2040 W has the PCF85063A), so timed wake is
impossible. There is no battery charger. Only front-button wake exists.

## Module map

```
firmware/core/        portable C++17, no SDK headers; compiled for device and host
  framebuffer         1-bpp buffer in the UC8151 column-major layout; diff bounds
  font, text          compiled bitmap fonts; UTF-8, fit chains, ellipsis, wrapping
  qr                  qrcodegen wrapper: version/ECC/scale choice, quiet zone
  renderer            badge (layouts A/B), card (contact icons), project portfolio,
                      project index, full-screen contact and project QR, info, recovery
  icons               generated 12 px GitHub/Discord bitmaps (Simple Icons, CC0)
  input               5 ms sampling debouncer, press/short/long/repeat gestures, wake
                      suppression; ClickRecognizer (single/double/long C)
  app                 screen state machine and actions (redraw, clean refresh, sleep),
                      project index with quiet browsing
  settings            content/prefs model and field table (keys, IDs, limits)
  settings_store      versioned TLV records, CRCs, A/B slots over a FlashBackend,
                      read-only migration from format-1 records
  assetpack           validated bitmap container (built-in or flashed)
  spsc_queue          lock-free bounded single-producer/single-consumer ring
  display_pipeline    RenderScheduler (app side) + DisplayService (panel side)
  battery             LiPo meter: conversion, EMA filter, hysteresis, USB/invalid states
  gesture, apds9960   swipe decoding, orientation, cooldown; sensor driver/service over I2cBus
firmware/cli/         USB CLI parser and dispatcher over a CliHost interface
firmware/platform/    build_info (shared by targets); one backend directory per hardware target:
  badger2040/         RP2040 only: board (incl. ADC battery sampling), I2C0 bus, UC8151 panel adapter, flash backend,
                      diagnostics (reset/fault/watchdog/memory), main loop; target.cmake
host/                 unit tests, simulated panel, preview renderer
tools/                portrait, fonts, asset packs, profile compiler, previews, badgerctl
```

Everything with behaviour (state machine, debouncer, refresh policy,
coalescing, settings recovery, CLI validation, rendering) is in `core/` or
`cli/` and is tested on the host. `platform/` is kept thin.

## Execution contexts and ownership

```
core 0 ─ main loop (event driven: WFE with 10 ms timeout)
  ├─ drains ButtonEvents        ◄─ SpscQueue<32> ◄─ 5 ms timer IRQ (debouncer)
  ├─ App::on_poll every pass: deferred single C, quiet index redraw
  ├─ USB CDC CLI (stdio_usb)
  ├─ App state machine
  ├─ RenderScheduler: renders newest View into a free buffer
  │        ── FrameJob{buffer, seq, speed, clean} ─► JobQueue (SPSC, 4)
  │        ◄─ DisplayEvent{released, done, …} ───── EventQueue (SPSC, 8)
  ├─ APDS-9960 service on I2C0 (polled every 10 ms only in gesture mode)
  ├─ battery sampling (boot, before planned refreshes, every 60 s)
  ├─ Settings (staged + committed) and flash commits
  └─ watchdog feed (only while core 1's heartbeat advances), LED, VBUS/VSYS

core 1 ─ DisplayService: the only owner of the UC8151 driver, SPI0 and the
         panel GPIOs. It polls BUSY without blocking, then powers the booster
         off when a refresh finishes.
```

- **Buffer ownership** travels with the job. Core 0 may write a buffer only
  while it holds it; core 1 returns it with `BufferReleased` as soon as it
  has copied the frame. No buffer is ever shared.
- **Driver thread-safety**: `UC8151_Legacy` has no locks. It busy-waits in
  `reset()`/`setup()` and drives SPI0 and the GPIOs directly, so it is not
  thread-safe. It is only ever called from the DisplayService context.
- **Single-core diagnostic mode** (`diag.single_core true`, or automatically
  in safe mode) does not launch core 1. The same DisplayService is then
  polled from the core 0 loop, with identical queues and code paths.
- **Queues** are bounded, never block, and use only 32-bit acquire/release
  loads and stores (lock-free on the Cortex-M0+). Host tests exercise them
  across real threads under ASan/UBSan.

## Buttons, C gestures and the project index

Three layers, all pure logic in `core/` and host-tested:

1. **ButtonTracker** (timer IRQ, every 5 ms): 20 ms integrating debounce per
   button. It emits `Press` when a debounced press starts, `Short` on a
   release before 1 s (with the number of repeats the hold produced),
   `Long` once at 1 s while held (the release then emits nothing), and for
   UP/DOWN only `Repeat` after 500 ms and every 150 ms after that, also past
   the long threshold. DOWN also emits `Hold` once at `kHoldPowerOffMs`
   (3 s), which the index uses for power-off. A button held at boot (the
   wake button) emits nothing until it has been released once. One sample
   produces at most `kMaxEventsPerSample` (24) events; the queue holds 32 (a tap is now two
   events). A full queue drops events and `diag display` counts the drops.
2. **ClickRecognizer** (core 0, inside `App`): turns C's raw gestures into
   exactly one `Single`, `Double` or `Long` per interaction.
   - A second press that *begins* less than `kDoublePressMs` (350 ms) after
     the first release makes a double, which acts on the second release.
   - Outside the index a single acts only once that window has passed
     without a second press. A double therefore never shows the project page
     first.
   - Inside the index the single acts on release. Confirming does not wait.
   - A long press (1 s, on the first or second press) discards a pending
     single, and its release emits nothing.
   - **Context changes.** A USB screen or content change, power-off or boot
     cancels a pending single. A press that was already down when that
     happened is ignored on release, so a delayed action never fires on a
     screen it was not meant for.
   - **Other buttons inside the window.** When another button is *pressed*
     while a single waits, the window freezes (nothing is drawn yet). Its
     gesture then decides, in the same step, so at most one frame results:

     | Other gesture | Pending single C |
     |---|---|
     | B long | applies first: the remembered project's QR (its page if it has no link; never the contact QR) |
     | UP / DOWN short | applies first: the project after / before the remembered one |
     | A long | applies first: the project page with a clean refresh |
     | UP long, USR long | applies first, and gesture mode / layout still toggles |
     | A short, B short, USR short | dropped: that button's screen, the project page is never requested |
     | DOWN long | dropped: power-off |
     | any swipe | dropped: the swipe's screen, from where the visitor was |

     If the deciding gesture never arrives (lost events), the C is dropped
     after `kLongPressMs` + 250 ms rather than fired late. A press after
     the window has expired finds the C already acted on. Host tests:
     `pending_c_*`, `pipeline_rapid_c_then_a_never_submits_a_project_frame`,
     `pipeline_c_then_long_b_shows_the_remembered_projects_qr`.
   - `App::on_poll()` runs on every main-loop pass (at least every 10 ms)
     and fires an expired single. It uses signed time differences, so a
     clock sample taken just before an event was queued never looks like
     a far-future time.
3. **App**: screen state machine. Each recognised action changes the `View`
   and returns actions (redraw, clean refresh, sleep).

### Project index and quiet browsing

`Screen::Index` lists the configured project names in configured order
seven rows at a time, with `n/N`, a scrollbar
when it does not fit, and UP/DOWN triangles. All index state is
session-only RAM; nothing about browsing is ever written to flash, and the
settings format is unchanged.

- **Two selections.** `View::project` is the remembered (last-viewed)
  project. The index keeps its own candidate: the live `App::index_sel_`,
  and `View::index_sel` / `View::index_top`, which is what is drawn.
  Browsing and cancelling never touch `View::project`. Confirming sets it.
- **Quiet browsing.** UP/DOWN taps (wrapping) and repeats (stopping at
  the ends, since the hold is blind) move only the live candidate. Every
  UP/DOWN event, including the press, pushes back a quiet timer
  (`kIndexSettleMs`, 300 ms, longer than the repeat interval). When it
  expires, the candidate is copied into the `View` and one redraw is
  requested, but only if it differs from what is drawn. Taking several
  taps, or a hold, therefore gives one refresh after release. DOWN then UP
  gives none.
- **Confirm / cancel.** Confirm switches the `View` straight to the
  selected project page. Cancel (A) switches to the screen the index was
  opened from (re-validated, e.g. a project QR whose link was removed meanwhile
  becomes that project's page). A pending, undrawn highlight is discarded in
  both cases, so it is never shown.
- **Viewport.** `index_viewport_top()` moves the window only when the
  highlight would leave it (minimal scrolling). It clamps to the list, so
  a partial change usually stays a partial refresh.
- **Refreshes.** The index uses the normal pipeline. A highlight change
  still needs an e-paper refresh (partial when small, full when the window
  scrolls or the partial budget is used; necessary cleaning waveforms are
  unchanged). A refresh already running is never cut short. The
  RenderScheduler renders only the newest `View` once it finishes, and the
  DisplayService drops a queued job a newer one supersedes. Rapid input
  during a slow refresh therefore ends with exactly one more frame: the
  newest highlight, or the confirmed project, never an intermediate one
  (host test `pipeline_rapid_index_navigation_during_refresh_shows_only_the_final_project`).
- **Modal.** In the index only UP/DOWN, C, A short (cancel) and A long
  (clean refresh) act. B, USR and swipes are ignored. Holding UP or DOWN
  repeats; the 1 s long press is taken by scrolling, so UP does not toggle
  gesture mode there.
- **Power-off from the index.** Holding DOWN for `kHoldPowerOffMs` (3 s,
  the tracker's `Hold` event) runs the normal power-off sequence:
  sleep view, wait for the panel, release the latch. Scrolling all 12
  entries takes 2.15 s, so a scroll to the end alone does not power off.
  The scrolled highlight is discarded, never confirmed, and nothing queued
  is drawn: the only frame after it is the sleep view. That is the badge,
  or the index as last drawn when `sleep.screen` is "current". UP/DOWN
  holds that began before the index opened are ignored entirely (no scroll,
  no power-off). Auto power-off still applies.

## Refresh scheduling

1. A button or CLI action changes the `View`, or content changes; the
   scheduler marks the screen dirty. Further requests before rendering are
   **coalesced**: only the newest view is ever rendered.
2. The scheduler renders only when the previous frame is completely done on
   the panel (rendering takes milliseconds, a refresh takes seconds). A burst
   of presses or swipes during a slow refresh (speed 0, an estimated 4.5 s)
   therefore produces exactly one
   more refresh, showing the final state.
3. The frame hash is compared with the last submitted frame; **unchanged
   screens are suppressed** without touching the panel.
4. DisplayService takes the newest queued job (older ones are dropped) and
   picks the mode:
   - **clean**: full refresh with the OTP waveform (speed 0), requested by
     A-long or `refresh clean`.
   - **partial**: the diff bounding box, aligned to 8-pixel rows (the UC8151
     `PTL` window granularity the driver uses), if it covers < 40 % of the
     screen and fewer than `refresh.max_partials` partials have happened since
     the last full refresh.
   - **full** otherwise, and always for the first frame after boot or reset.
     The panel content is unknown until then (the e-paper still shows the
     previous image).
5. The panel is never commanded while BUSY. If BUSY stays low for 15 s the
   controller is reset and the next frame is a clean redraw.
6. Every controller reset is **bounded**. The Pimoroni driver's `busy_wait()`
   has no timeout, so `Uc8151Panel` pulses RESET itself and waits at most
   500 ms for BUSY before calling the driver. A controller that does not
   answer puts `DisplayService` into a *panel fault* state. Jobs are then
   released at once (so core 0, buttons, USB and sleep keep working), the
   heartbeat continues, and re-initialisation is retried with a 2 → 60 s
   backoff. Recovery forces one clean redraw. `diag display` and `selftest`
   report `panel NOT RESPONDING`.

Each submitted frame is recorded in a 16-entry trace (request, submit and
done times, panel BUSY time, mode and the reason for it) for `diag refresh`.
[REFRESH.md](REFRESH.md) explains the visible phases of a full refresh.

Refresh speeds use the driver's LUT sets (`update_speed` 0–3), and
`refresh.speed` selects them. Changing speed re-runs the controller setup,
which the driver does with a reset. The image is retained.

## Settings service

Content is staged in RAM (the display shows staged values) and written only
on `commit`, so button presses never write flash.

A record is written to the slot that does not hold the current record, then
read back and fully decoded before being trusted (see FORMATS.md).

Flash programming on the RP2040 stops XIP. With core 1 running, the store
uses `flash_safe_execute()`: core 1 is parked in a RAM-resident lockout
handler (registered with `flash_safe_execute_core_init()`), and IRQs on core 0
are disabled for the duration. In single-core mode core 1 is not running, so
disabling IRQs is enough. The SDK's erase/program routines run from RAM and
flush the XIP cache. One sector erase and program takes tens of
milliseconds, well inside the 5 s watchdog.

## Power, wake and reset

- A constructor with priority 101 latches GPIO10 and samples the buttons
  before `main()`, mirroring Pimoroni's `wakeup` module. A short tap is
  enough to wake.
- A battery wake is a **cold boot**. The button that woke the board picks the
  first screen, and it is ignored by the debouncer until released, so a long
  hold never triggers DOWN-long power-off.
- Power-off waits for the sleep view to be rendered **and** for the panel to
  finish, then releases the latch. The image is complete before power goes.
  If the CPU keeps running (USB, or a held button), sleep is emulated: it
  waits for release, then a new press, then reboots.
- Reset classification (`core/crash_record.*`, host-tested) first asks
  `WATCHDOG.REASON`, which every chip-level reset clears. Only for a chip
  reset does it read `CHIP_RESET` (power-on / RUN pin / debugger); that
  register still describes the original power-on after a watchdog reset.
  A CRC-protected record in `.uninitialized_data` survives watchdog and soft
  resets. It carries the reason and is discarded after any power-on, even if
  SRAM kept it intact. The SDK panic hook (`PICO_PANIC_FUNCTION`,
  which also catches `assert`), `BADGE_ASSERT` and a hard-fault handler record
  a message or PC before rebooting through the watchdog.
- Three abnormal resets in a row, without 60 s of healthy uptime between them,
  enter safe mode.
- If core 1's heartbeat stalls for 3 s, core 0 records "core1 display
  heartbeat stalled" and stops feeding the watchdog. A core-0 hang shows up
  as an unannounced watchdog timeout. `crashtest … confirm` exercises each
  path over USB (USB_HARDWARE_CHECKLIST.md §9).

## Memory

Sample build: 192.6 KiB flash; 86.7 KiB static RAM (the project index added
3.7 KiB of flash and 320 B of RAM, mostly the larger button queue). The two render buffers,
the shown image, the driver buffer (4.6 KiB each) and three Settings copies
(2.6 KiB each) dominate. There are 4 KiB stacks per core; large objects are
static, never on the stack. Each core paints its own stack below its
current SP at start-up, and `diag mem` reports the minimum free stack seen.
The measured regions are the ones the SDK uses: core 0
`0x20041000–0x20042000` (SCRATCH_Y) and core 1 `0x20040000–0x20041000`
(SCRATCH_X, where `multicore_launch_core1()` places `core1_stack`).
`scripts/verify_artifacts.py` checks this in the linked ELF.
