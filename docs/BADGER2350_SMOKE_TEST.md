# Badger 2350 smoke test (reusable physical checklist)

**Core parity physical testing has passed for the documented scope.** The
owner-reported results are preserved in
[`test-records/2026-10-08-badger2350-parity-b3125aa-dc20680.md`](test-records/2026-10-08-badger2350-parity-b3125aa-dc20680.md):
most tests ran on `b3125aa`, with targeted A2/B5/E2 retests on `dc20680`.
This checklist remains reusable for other hardware and future firmware heads;
never treat an older test record as proof of a new binary.

Record the `version` output and each pass/fail in a new file under
`docs/test-records/`. Measure refresh timing, battery voltage and current
instead of assuming fixed values. Sleep current is not yet measured on the
owner's device and remains optional characterization.

Before starting: back up the stock flash (`picotool save -a
badger-2350-full-backup.bin` in BOOTSEL mode) and have SWD available if you
can (a Debug Probe on the SWD pads) for recovery from a boot failure.

## A. First boot over USB

1. BOOTSEL: on the back, hold BOOT, tap RESET. **Expect**: an `RP2350`
   drive. Copy `badger2350_badge.uf2`. **Expect**: the badge reboots and
   draws the photo badge (layout A) with one full refresh. Record the
   time from reset to a finished image.
2. Serial port enumerates (record the VID:PID). `version`. **Expect**:
   `target badger2350: Badger 2350, RP2350A, 16 MiB flash, 520 KiB SRAM,
   8 MiB PSRAM on board, not initialised or used, SSD1680 264x176`, and
   `pico-sdk 2.3.1, pimoroni-pico not used`.
3. `diag all`, `selftest`. **Expect**: `reset: power-on` or `reset button`,
   settings `erased` (first boot), `display: SSD1680 264x176 panel ok ... |
   no partial refresh on this panel | one waveform`, `selftest` OK.
4. The image is the right way up and not mirrored: name at the top left,
   portrait on the left, battery/USB status at the top right.

## B. Buttons and screens (same product as the Badger 2040)

Follow [HARDWARE_SMOKE_TEST.md](HARDWARE_SMOKE_TEST.md) sections B and B2
with **HOME in place of USR**, and check every step:

1. A, B, C short: badge, card, projects; one refresh each; C acts about
   0.35 s after release.
2. A long: clean (full) refresh. B long: full-screen contact QR; on a
   project page, that project's QR; again: back. C double: project index.
   C long: project 1. UP/DOWN short: previous/next project.
3. UP/DOWN held on the index: the highlight repeats and the index redraws
   once after release (quiet browsing). DOWN held 3 s on the index: power off.
4. UP long: gesture mode on/off. HOME short: diagnostics. HOME long:
   layout B, then A again.
5. Both badge layouts: name, title and interests readable; portrait
   rendered at its **intended asset geometry**, vertically centered and not
   clipped or distorted. The designed 2350 sample is 104×176, but valid
   smaller assets such as the owner's preferred 104×128 Classic one-bit
   portrait are intentionally supported. Do not fail only for blank margins.
6. Press buttons repeatedly **during** a refresh. **Expect**: exactly one more
   refresh with the newest screen afterwards (`diag refresh`: one entry per
   finished frame, reason `no partial on this panel` or `first frame`).
7. Confirm which physical button is HOME and whether it is the BOOT button
   used for BOOTSEL (the board header calls GPIO22 "AKA boot"); note it.

## C. Display behaviour

1. `diag refresh` after a few screens: speed `-`, mode `full` (or `clean`
   after A long), busy times recorded. Note the typical BUSY time.
2. Ghosting after 20 or so changes: visible? Does A long clear it?
3. Leave the badge on one screen for 10 minutes: no refreshes
   (`diag refresh` unchanged).

## D. Battery power and sleep

1. Unplug USB with a charged LiPo connected. **Expect**: the badge keeps
   running, and the next refresh (press a button) shows battery bars instead
   of `USB`.
2. `diag battery` over USB first, then compare the cell voltage with a
   multimeter at the battery connector: record both. (`USB` is shown while
   VBUS is present; the reading then reflects the cell on the charger.)
3. DOWN long on battery. **Expect**: the sleep image (badge by default) is
   drawn completely, then the badge powers down. The image stays.
4. Press each front button (A, B, C, UP, DOWN) in turn from sleep.
   **Expect**: the badge wakes and boots; with `wake.selects_screen` on
   (the default) the button chooses the first screen (B wakes to the card);
   holding the waking button does not also trigger its long-press action.
5. HOME from sleep. **Expect**: no wake (it is not on the wake line).
6. Repeat sleep and wake 20 times. **Expect**: every wake works.
7. Auto power-off: `set sleep.timeout_s 30`, `commit`, unplug USB, wait.
   **Expect**: power-off only after the panel has finished.
8. If you can, measure the sleep current from the battery. Record it.
9. Reconnect USB while asleep: record what happens (charging only, or a
   wake). DOWN long while on USB: emulated sleep (the badge keeps running
   and the next button press reboots it, `reset: wake from USB sleep`).

## E. Settings and persistence

1. `set name Test`, `commit`, `reboot`. **Expect**: the name persists;
   `diag settings` shows slot A, then B after another commit.
2. Pull the battery and USB, reconnect. **Expect**: the settings persist.

## F. Reset, watchdog and recovery

1. `crashtest hang0 confirm`, `crashtest fault1 confirm`, `crashtest panic confirm`. **Expect**: a
   watchdog reboot within about 5–8 s each, `diag reset` naming the cause.
2. Three crashes in a row: SAFE MODE screen; `defaults`, `commit`,
   `reboot` leave it.
3. Hold A + C while pressing RESET: safe mode.
4. `reboot bootsel`: the `RP2350` drive appears.
5. RESET button once: `reset: reset button`.

## G. QR codes

1. Card QR, full-screen contact QR (B long) and every project QR: scan each
   from at least two phones (record models), at arm's length and close.
   **Expect**: each opens its own link.

## H. Gesture sensor (optional; deferred from the core parity gate)

The owner has put APDS-9960 experimentation on the backburner. These checks
are optional and should be run **only if** an APDS-9960 is actually fitted;
do not require them for Badger 2350 core parity acceptance.

1. Boot with the sensor attached; UP long. **Expect**: the gesture indicator
   (not struck through); `diag gesture` shows the sensor on I2C0 SDA 4 SCL 5.
   If the first probe at boot reports `not detected`, note it: the sensor's
   supply is switched on during boot.
2. Swipes left/right/up/down change screens as on the Badger 2040.
3. Gesture mode off, then sleep: the sensor's IR LED is off.

## I. Recovery paths

1. Flash the Badger 2040's `badger_badge.uf2` onto the Badger 2350 (only if
   the stock backup is safe): record what the boot ROM does, then flash
   `badger2350_badge.uf2` again.
2. If a build ever fails to boot: BOOTSEL still works (boot ROM); SWD
   (`openocd` / Debug Probe) can load an image too.
3. Return to stock: Pimoroni's `…-with-filesystem.uf2` restores BadgeWare.
