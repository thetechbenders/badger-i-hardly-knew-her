# Hardware smoke test (physical badge)

Host tests and the cross-build cannot prove the behaviours below; they need
the real board. Record results (pass/fail, notes, firmware `version`) for
each run. Keep a serial terminal open on USB where noted (`status`, `diag all`).

## A. USB power

1. Flash `badger_badge.uf2` (see INSTALL.md). **Expect**: one full refresh to
   the photo badge within about 3 s (estimate; record the measured time). The LED glows during the refresh.
2. `version`, `diag all`, `selftest`. **Expect**: `reset: power-on` or
   `reset button`, flash image as in the build's `memory-report.txt`, settings `erased` (first boot), and
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
   UP = gesture mode on/off (indicator appears / disappears);
   USR long = layout B, then A again; DOWN = power-off sequence (step D).
4. Tap a button very briefly (< 20 ms, a glancing touch). **Expect**: no action.
5. Hold A and B together, release A, then B. **Expect**: badge, then card.

## C. Busy-panel behaviour

1. `set refresh.speed 0` (refreshes estimated at 4.5 s). Press B, then press C, A, B, C
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
   else, record it. The classifier trusts WATCHDOG.REASON first and reads
   CHIP_RESET only for chip resets; this needs confirming on the board.)
2. `reboot`. **Expect**: `reboot command`.
3. Hold A + C and press RST. **Expect**: the SAFE MODE screen, stored settings
   not applied, and the CLI still works. `reboot` returns to normal.
4. Pull USB during a refresh (no battery). **Expect**: on reconnect, it boots
   normally. The panel may show a partial image until the first refresh.
5. Crash paths: `crashtest panic|fault0|fault1|hang0|hang1 confirm`. See
   USB_HARDWARE_CHECKLIST.md §9 for the expected `diag reset` output and the
   safe-mode sequence.

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

## I. Gesture sensor (APDS-9960 on Qwiic)

Record the breakout model, the mounting and the enclosure for every run.

1. **Missing sensor.** Boot without the sensor.
   - **Expect**: normal operation. `diag gesture` shows `not detected`.
   - UP long shows the struck-through indicator; buttons still work.
   - Plug the sensor in: within ~30 s `diag gesture` shows `standby` or
     `active`, and the indicator becomes plain.
2. **Fault while active.** Pull the Qwiic cable in gesture mode.
   - **Expect**: the badge keeps working; after 3 errors the state goes to
     `fault` and the indicator is redrawn struck through.
   - Replug: it recovers automatically.
3. **IR emitter off when inactive.** Use a phone camera without an IR filter
   (most front cameras) or an IR viewer.
   - Gesture mode off: no IR glow at the sensor. On: faint pulsing glow.
   - After `gesture off`, after the `gesture.timeout_s` auto-off, and after a
     USB emulated sleep: the glow is gone.
4. **Orientation calibration.**
   - Swipe left-to-right across the badge 5 times and check `diag gesture`
     (last raw → result).
   - Set `gesture.rotation` / `gesture.mirror` until all four directions map
     correctly, then `commit`.
5. **Reliability.** Mounted as it will be worn. For each direction do 20
   swipes at ~3 cm, ~6 cm and ~10 cm, at a normal hand speed, and record:
   - correct / wrong direction / missed;
   - the `diag gesture` counters (sessions, rejected, cooldown-suppressed);
   - the conditions: hall-style overhead light, near a window (sunlight IR),
     and dim light.
   Target: ≥ 90 % correct at the intended distance and no wrong-direction
   screen changes. Tune `gesture.sensitivity` if needed.
6. **One swipe, one change.**
   - Swipe right once firmly and then return the hand: exactly one screen
     change.
   - Swipe 3 times during a slow (`refresh.speed 0`) refresh: one further
     refresh, showing the final screen.
7. **Enclosure effects.** Repeat step 5 (one distance, all directions) in
   each condition:
   - bare sensor;
   - behind the printed aperture plate (`hardware/gesture_sensor`);
   - in the final enclosure.
   If the enclosure increases `rejected` counts or wrong directions, suspect
   IR crosstalk: use a darker or more opaque material, reduce the wall
   thickness or `face_gap`, and never put a cover over the aperture.
8. **Power.** Measure in series with the battery, averaged over ≥ 10 s:
   - awake, gesture mode off;
   - awake, gesture mode on (sensor active, idle);
   - awake, gesture mode on during continuous swiping;
   - powered off.
   Also measure the Qwiic connector's 3V3 pin while powered off. **Expect**
   0 V, meaning the rail is switched and gestures cannot wake the badge.
   If it reads 3.3 V, the sensor stays powered: record this and report it.
9. **No gesture wake.** With the badge powered off, swipe over the sensor.
   **Expect**: nothing happens (only the front buttons wake it).

## J. Battery meter (single-cell LiPo)

1. **USB.** On USB the icon reads `USB`, never bars and never "charging".
2. **Battery.** On battery, compare the Info screen (C long; it redraws every
   15 s with a fresh sample) against a multimeter at the JST connector, at
   ≥ 3 charge levels. Record badge mV, multimeter mV and the difference.
   Set `battery.cal_permille` for a ratio error; lower the thresholds for a
   constant offset (see BATTERY.md).
3. **Stability.** Leave it on for 10 minutes with occasional button presses.
   **Expect**: no bar flicker between adjacent levels.
4. **LOW.** Run a cell down (or use a bench supply on the battery input,
   stepping down slowly). Check that LOW appears below `battery.low_mv` and
   goes away only above it plus the hysteresis. Record where the
   board/regulator browns out.
5. **Invalid.** If a reading ever shows `?`, capture `diag battery` (raw
   counts) over a VBUS-blocking data-only USB cable.
6. **Last measurement kept.** Power off. **Expect**: the e-paper keeps the
   last icon. Wake: a fresh sample appears on the first frame.
7. **No keep-awake.** Confirm auto power-off still happens after
   `sleep.timeout_s`: the meter never keeps the badge awake.
