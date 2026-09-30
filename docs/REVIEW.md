# Code review and hardening (September 2026)

This is a review of the actual code at `4756d0e` (after CI went green) against the
pinned Pico SDK 2.2.0, pimoroni-pico v1.29.0-2 and the linked ELF. No
battery, gesture sensor or badge hardware was used: everything below was
established on the host, in the RP2040 cross-build, or by reading SDK and
driver sources. Hardware checks are in
[USB_HARDWARE_CHECKLIST.md](USB_HARDWARE_CHECKLIST.md) (pending).

## Defects found and fixed

| # | Area | Defect | Fix | Regression test |
|---|---|---|---|---|
| 1 | Reset classification | `boot()` tested `CHIP_RESET.HAD_POR` before the watchdog. That register describes the last *chip-level* reset and is not rewritten by a watchdog reset, so after a normal power-on, watchdog/panic/fault reboots could be classified as power-on. The crash streak would then never build and safe mode would never trigger. | `WATCHDOG.REASON` (cleared by every chip reset) decides watchdog vs chip reset first; the record is trusted only for watchdog resets. Logic moved to `core/crash_record.*`. | `crash_record_watchdog_resets_after_power_on_build_a_streak` (fails on the old order) |
| 2 | Crash record integrity | A sealed record's `pending` value was cast straight to the 8-bit enum, so an out-of-range value could truncate into a valid reason. A valid-looking record left in SRAM across a short power dip was accepted. | Range check before the cast; after a POR the record is always discarded. | `crash_record_integrity_failures_are_not_trusted`, `crash_record_valid_record_after_power_on_is_discarded` |
| 3 | Stuck BUSY | `UC8151_Legacy::reset()`/`setup()` spin on BUSY with no timeout. The 15 s busy-timeout recovery called `init()`, which spins forever when BUSY is held low, the very condition being handled. The watchdog then loops. Safe mode also initialises the panel before its main loop, so it cannot break the loop: USB CLI unreachable. | `Uc8151Panel` pulses RESET itself and waits for BUSY with a 500 ms bound before handing the controller to the driver. `DisplayService` has a `PanelFault` state: jobs are released immediately, heartbeat continues, re-init backs off 2 → 60 s, and recovery forces one clean redraw. `diag display` / `selftest` report it. | `pipeline_dead_panel_never_blocks_...` (both speeds), `pipeline_panel_unresponsive_at_boot_...`, `pipeline_failed_speed_change_is_a_panel_fault_not_a_hang` |
| 4 | Core-1 hang attribution | A stalled display core led to an unannounced watchdog reset, indistinguishable from a core-0 hang. | Core 0 records "core1 display heartbeat stalled" when it stops feeding (and withdraws it if the heartbeat resumes). Heartbeat is now an atomic read across cores. | covered by 1; hardware: checklist §9 |
| 5 | Flapping sensor | Every successful re-probe reset the retry backoff to 1 s. A marginal Qwiic cable that probes fine but fails once active cycled ok ↔ fault about every second, and each flip changed the status indicator: a continuous e-paper refresh loop. | Backoff keeps growing (1 → 30 s) and resets only after 60 s without a fault. | `apds_flapping_sensor_is_rate_limited` (fails on the old reset) |
| 6 | CLI malformed input | A NUL byte truncated the line silently (`set name Ada<NUL>X` stored `Ada`); ESC sequences and other control bytes reached the parser. | Any control byte except TAB rejects the whole line with `ERR control character 0x..`. | `cli_rejects_control_bytes_without_side_effects` |
| 7 | CLI recovery | A partial line left by a dead session was glued onto the next command. | A partial line idle for 30 s is dropped; `badgerctl.py` sends Ctrl-C before talking. | `cli_stale_partial_line_is_dropped_after_idle` (incl. timer wrap) |
| 8 | Unconfigured QR | An empty `qr.payload` drew a dashed "QR NOT CONFIGURED" box on the public card, and an empty contact list printed "Contact details not configured". | Nothing is drawn; the text column takes the full width. The too-long box stays (a configuration error). | `render_unconfigured_qr_leaves_no_trace`, `render_card_without_contacts_is_clean`, Python `test_unconfigured_qr_draws_no_symbol` |
| 9 | Project taglines | A tagline wider than one line was ellipsized: the Dragon-family tagline showed as "…validati…". | Taglines wrap onto two lines of bold 10 when one line is not enough. | `render_project_page_shows_full_tagline_and_body` |
| 10 | Checkout portability | With Git's `core.autocrlf=true` (Git for Windows default), every script became CRLF and would not run under bash/WSL; build inputs differed per checkout. | `.gitattributes`: `* text=auto eol=lf`, binaries marked. | CI (Linux) + local WSL build from a Windows checkout |
| 11 | Reproducibility | Absolute source paths could enter the image. The `git describe` abbreviation grows with the object count, so a fresh clone or bundle can embed a different version string. | `-ffile-prefix-map=<src>=.`, `--abbrev=12`. | CI rebuilds from a second checkout path and `cmp`s UF2/BIN/ELF; `verify_artifacts.py` rejects absolute paths |

Every new regression test was checked against the old behaviour. Each one
was re-introduced into a scratch copy, and the corresponding test failed
(1, 3, 5, 6, 7, 8 and the speed-0 dead-panel case). One mutation survived:
dropping the hash reset on `PanelReset`. That reset is deliberately
redundant with the clean-refresh flag, which already bypasses suppression.

## Reviewed and found correct

- **Queues and ownership.** `SpscQueue` uses only single-writer
  acquire/release word atomics, so it is lock-free on the M0+. With the
  one-pending-job rule the event queue cannot overflow (`event_overflows` is
  checked). Buffers are owned by whoever holds the job/event. Core 0 owns
  every `Settings` object, and the CLI, renderer and commits all run in the
  core-0 loop. Diagnostic reads of `DisplayStats` from core 0 are word-sized
  and informational only.
- **Refresh coalescing.** The scheduler renders only when the previous frame
  is done, so any burst yields exactly one more frame showing the newest
  view. Stale intermediate screens are never rendered. A clean refresh
  bypasses hash suppression. After a timeout or panel reset the scheduler
  forgets the last hash.
- **Settings A/B.** A commit writes the non-active slot. The header CRC covers
  the payload CRC, so a torn page fails. Every single-bit flip is rejected
  (exhaustive existing test). Serial-number sequence comparison wraps. The
  struct has no padding, so the read-back `memcmp` is exact.
- **Flash with core 1 and USB active.** `flash_safe_execute()` parks core 1 in
  the SDK's RAM lockout handler and disables IRQs on core 0 for one sector
  (tens of ms). USB is serviced afterwards. If core 1 never registered, the
  commit fails with `PICO_ERROR_NOT_PERMITTED` instead of corrupting XIP.
  Staging buffer in RAM.
- **Stack high-water marks.** The measured regions are what the SDK really
  uses, confirmed from `multicore_launch_core1()` (stack at
  `__StackOneBottom`) and the linked ELF: core 0 is `0x20041000–0x20042000`,
  core 1 is `0x20040000–0x20041000`, and `core1_stack == __StackOneBottom`.
  Each core paints only its own stack, below its SP − 64, before going deep.
  An interrupt on the same core cannot be overwritten, because the paint
  loop does not run while the ISR does.
- **No-init crash record.** It sits in `.uninitialized_data`, which is NOBITS
  and outside `.data`/`.bss`, so crt0 neither copies nor zeroes it
  (`verify_artifacts.py` checks this in every CI build).
- **Sensor absent.** A probe is one bounded I2C read (≈1 ms timeout). Retries
  happen only while gesture mode is wanted, with backoff. Buttons are
  independent. The indicator shows the struck-through state once, without
  repeated redraws.

## Remaining limitations (not fixed)

- If the UC8151 dies *during* the LUT upload that follows a good reset, the
  driver can still spin. The watchdog recovers, and the core-1 case is now
  attributed. Removing this needs a patched driver.
- A hard fault caused by stack overflow runs its handler on the overflowed
  stack. A double fault locks up the core, and the watchdog reports an
  unannounced timeout. There is no MPU stack guard (`PICO_USE_STACK_GUARDS`
  off), so core 0 overflowing its 4 KiB would run into core 1's stack. The
  current high-water marks can only be read on hardware (`diag mem`).
- A core-0 hang is recorded without detail (unannounced watchdog).
- `SettingsStore::erase_all()` (tests only; no CLI path) marks the store
  "defaults" even if one erase failed.
- Frame suppression uses a 32-bit hash: an unrelated frame with a colliding
  hash (≈ 2⁻³² per change) would not be sent.
- Gesture decoding is validated with synthetic FIFO data only. No recorded
  sensor data exists yet.
