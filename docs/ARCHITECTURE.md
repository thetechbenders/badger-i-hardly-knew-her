# Architecture

## Hardware facts this design relies on

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
                      full-screen contact and project QR, info, recovery
  icons               generated 12 px GitHub/Discord bitmaps (Simple Icons, CC0)
  input               5 ms sampling debouncer, short/long gestures, wake suppression
  app                 screen state machine and actions (redraw, clean refresh, sleep)
  settings            content/prefs model and field table (keys, IDs, limits)
  settings_store      versioned TLV records, CRCs, A/B slots over a FlashBackend,
                      read-only migration from format-1 records
  assetpack           validated bitmap container (built-in or flashed)
  spsc_queue          lock-free bounded single-producer/single-consumer ring
  display_pipeline    RenderScheduler (app side) + DisplayService (panel side)
  battery             LiPo meter: conversion, EMA filter, hysteresis, USB/invalid states
  gesture, apds9960   swipe decoding, orientation, cooldown; sensor driver/service over I2cBus
firmware/cli/         USB CLI parser and dispatcher over a CliHost interface
firmware/platform/    RP2040 only: board (incl. ADC battery sampling), I2C0 bus, UC8151 panel adapter, flash backend,
                      diagnostics (reset/fault/watchdog/memory), main loop
host/                 unit tests, simulated panel, preview renderer
tools/                portrait, fonts, asset packs, profile compiler, previews, badgerctl
```

Everything with behaviour (state machine, debouncer, refresh policy,
coalescing, settings recovery, CLI validation, rendering) is in `core/` or
`cli/` and is tested on the host. `platform/` is kept thin.

## Execution contexts and ownership

```
core 0 ─ main loop (event driven: WFE with 10 ms timeout)
  ├─ drains ButtonEvents        ◄─ SpscQueue<16> ◄─ 5 ms timer IRQ (debouncer)
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

Sample build: 178 KiB flash; 59.7 KiB static RAM. The two render buffers,
the shown image, the driver buffer (4.6 KiB each) and three Settings copies
(2.6 KiB each) dominate. There are 4 KiB stacks per core; large objects are
static, never on the stack. Each core paints its own stack below its
current SP at start-up, and `diag mem` reports the minimum free stack seen.
The measured regions are the ones the SDK uses: core 0
`0x20041000–0x20042000` (SCRATCH_Y) and core 1 `0x20040000–0x20041000`
(SCRATCH_X, where `multicore_launch_core1()` places `core1_stack`).
`scripts/verify_artifacts.py` checks this in the linked ELF.
