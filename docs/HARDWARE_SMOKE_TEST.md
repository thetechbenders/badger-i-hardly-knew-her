# Hardware smoke test (physical badge)

Host tests and the cross-build cannot prove the behaviours below; they need
the real board. Record results (pass/fail, notes, firmware `version`) for
each run. Keep a serial terminal open on USB where noted (`status`, `diag all`).

## A. USB power

1. Flash `badger_badge.uf2` (see INSTALL.md). **Expect**: one full refresh to
   the photo badge within about 3 s. The LED glows during the refresh.
2. `version`, `diag all`, `selftest`. **Expect**: `reset: power-on` or
   `reset button`, flash image ~164 KiB, settings `erased` (first boot), and
   `selftest` OK.
3. Serial port enumerates as `2E8A:000A`. `help` answers; `echo off` then
   `status` returns `OK`.

## B. Buttons and screens

1. A, B, C short: badge, card, projects. Each changes with one refresh and
   no double refresh.
2. On projects: DOWN/UP step through projects, with wrap-around and the
   arrow hints at the right edge.
3. Long-press each button (≥ 1 s): A = visibly slower clean refresh;
   B = full-screen QR (or card if unconfigured); C = diagnostics;
   UP = layout B, then A again; DOWN = power-off sequence (step D).
4. Tap a button very briefly (< 20 ms, a glancing touch). **Expect**: no action.
5. Hold A and B together, release A, then B. **Expect**: badge, then card.

## C. Busy-panel behaviour

1. `set refresh.speed 0` (4.5 s refreshes). Press B, then press C, A, B, C
   rapidly during the refresh. **Expect**: at most one further refresh, and
   the final screen is projects (the last press). `diag display` shows
   `dropped/coalesced` counts and `event overflow 0`.
2. Press A while already on the badge. **Expect**: no refresh; `renders
   suppressed` increments.
3. `refresh clean` while idle, and while busy. **Expect**: no hang, no garbled
   image.
4. Several small changes (`set title X`, `set title Y`, …). **Expect**:
   partial refreshes, then a full refresh after `refresh.max_partials`.
   Check ghosting is acceptable; disable with `set refresh.partial off` if not.
5. `revert`, then `set refresh.speed 1`.

## D. Battery power (USB disconnected)

1. Fit a battery Pimoroni specifies for the Badger 2040. Nothing happens
   until a button is pressed (the board is off).
2. Tap A briefly. **Expect**: it boots and stays on after release (the power
   latch works), then shows the badge.
3. Wake with B. **Expect**: the card is the first screen (cold boot).
4. Hold DOWN to wake and keep holding for 3 s. **Expect**: it does **not**
   power off (the wake button is suppressed until released).
5. Long-press DOWN from the card. **Expect**: the badge is redrawn, then the
   board turns off with the image intact. The LED stays off. After an hour it
   is still off and the image is unchanged.
6. Leave it idle for 120 s (default `sleep.timeout_s`). **Expect**: the same
   auto power-off.
7. Measure the current if possible: running ~tens of mA, off ≈ regulator
   leakage only. Note the values.
8. On the diagnostics screen, the VSYS reading is plausible for the battery.

## E. Emulated sleep on USB

1. On USB, run `sleep` (or long-press DOWN). **Expect**: the badge image is
   drawn; the serial console prints `power off`; the board keeps running.
2. Press any button. **Expect**: it reboots. `diag reset` shows
   `wake from USB sleep`, and the first screen follows the wake button.

## F. Reset and recovery

1. Press RST during a refresh. **Expect**: it reboots cleanly, then does one
   full refresh; `diag reset` shows `reset button`. (If it reports something
   else, record it: CHIP_RESET decoding still needs confirming on this board.)
2. `reboot`. **Expect**: `reboot command`.
3. Hold A + C and press RST. **Expect**: the SAFE MODE screen, stored settings
   not applied, and the CLI still works. `reboot` returns to normal.
4. Pull USB during a refresh (no battery). **Expect**: on reconnect, it boots
   normally. The panel may show a partial image until the first refresh.
5. Optional (debug build): temporarily add `BADGE_ASSERT(false)` behind a CLI
   command, trigger it three times, and confirm safe mode and the recorded
   message. Remove it again.

## G. Settings persistence

1. `set name Test Person`, `commit`. The output names a slot, a sequence and a
   duration of tens of ms. Unplug, replug. **Expect**: "Test Person".
2. Commit twice more. **Expect**: the slots alternate A/B and the sequence
   increases (`diag settings`).
3. Commit in a loop while pulling power during one commit (`badgerctl.py cmd
   commit` repeatedly). **Expect**: after reboot either the old or the new
   name, never defaults. `recovered` may be reported.
4. `defaults`, `commit`. **Expect**: the sample profile returns.
5. Flash `badger_badge-assets.uf2`. **Expect**: `diag assets` shows
   `flash pack: ok`, and the portrait is unchanged or updated. Flash a
   different firmware build. **Expect**: settings survive.

## H. QR scanning

1. `set qr.payload <approved https URL>`. Scan the card and the full-screen QR
   with two different phones (iOS camera, Android camera) at 15–40 cm, under
   hall-style lighting. Record the distance range that works.
2. If trying a vCard: repeat, and note whether 2 px modules scan reliably.
