# USB-only hardware checklist

For a Badger 2040 on USB power only, with **no battery and no APDS-9960**
fitted. Every step records pass/fail, the firmware `version` line and notes.
Battery and sensor checks are listed at the end as **pending** until the parts
arrive; the full procedures for those are in
[HARDWARE_SMOKE_TEST.md](HARDWARE_SMOKE_TEST.md) §D, §I and §J.

Conventions: `>` lines are CLI commands (a terminal on the badge's USB serial
port, or `python3 tools/badgerctl.py cmd "…"`). Every command ends in `OK` or
`ERR …`. "Refresh" means one visible e-paper update.

Status as of this revision: **all steps pending**; 0.1 done read-only.
The badge on COM19 identified itself as `Pimoroni Badger2040 2MB with RP2040`,
MicroPython v1.18 (2022-04-01) with BadgerOS,
filesystem 1 MiB with `badge.txt`, `main.py` and `state` at the top level.
(Identified by interrupting the badge app, querying `os.uname()` and the
file list, then a soft reset; nothing was written.)

## 0. Before anything is written to the badge

Nothing in 0.1–0.3 writes to the badge.

- [ ] 0.1 Identify the board. If it enumerates as `2E8A:0005` it is running
      MicroPython/BadgerOS; as `2E8A:000A` it is running this firmware.
- [ ] 0.2 **BadgerOS files** (MicroPython running; close Thonny first):
      `python -m pip install mpremote`, then
      `python -m mpremote connect COM19 fs cp :badge.txt :main.py badger-backup/` and
      `python -m mpremote connect COM19 fs cp -r :state badger-backup/` (read-only copy).
      Record the file list (`python -m mpremote connect COM19 fs ls -r` if supported).
- [ ] 0.3 **Whole-flash image**: BOOTSEL (hold BOOT/USR, tap RST), then
      `picotool save -a badger-2040-full-backup.bin` and
      `picotool info -a > badger-2040-info.txt`. Keep the image with its SHA-256.
- [ ] 0.4 Verify the artifacts about to be flashed: `scripts/verify_artifacts.py build/fw --require-clean`
      (or check `SHA256SUMS` of the CI artifact). Record the commit.

Flashing (step 1) replaces MicroPython and needs the owner's explicit go-ahead.

## 1. Flash, startup and identity

- [ ] 1.1 BOOTSEL, copy `badger_badge.uf2` to `RPI-RP2`. **Expect**: one full
      refresh to the photo badge (layout **A**, portrait left) within ~3 s.
- [ ] 1.2 Port enumerates as `2E8A:000A`. `> version` shows the commit
      (`git describe`, 12 hex digits) and `pico-sdk 2.2.0`.
- [ ] 1.3 `> selftest` → fonts ok, built-in assets ok, settings ok, event
      overflows 0, **display panel ok**, `OK`.
- [ ] 1.4 `> diag all`: reset `power-on` (or `reset button`), settings slots
      `erased` on first install, stacks `min free` > 1000 B on both cores
      (record both values), `flash image` matches `memory-report.txt`.
- [ ] 1.5 `> status` → `power usb`.

## 2. Buttons, screens and layout selection

- [ ] 2.1 A / B / C short: badge / card / projects. One refresh each, no
      double refresh.
- [ ] 2.2 Projects: DOWN/UP step 1/2 ↔ 2/2 and wrap. The Dragon-family page
      shows the tagline on two full lines and the body
      "DragonBreath · DragonSniff · DragonBench · dragon-core."; Jump Jet
      reads "Work in progress".
- [ ] 2.3 Long presses: A = slow clean refresh; B = full-screen QR (card if no
      QR); C = diagnostics; USR = layout **B** (portrait right) for this
      session, USR again = A.
- [ ] 2.4 `> set layout 1`, `> commit`, `> reboot`: boots in B. `> set layout 0`,
      `> commit`: back to A (the default).
- [ ] 2.5 Glancing tap (< 20 ms): no action. A+B held together, released in
      turn: badge, then card.
- [ ] 2.6 Card with missing fields: `> clear contact2.value` removes that
      line entirely (no orphan label); `> clear qr.payload` leaves **no** QR,
      box or placeholder text. The text uses the full width. `> revert`.

## 3. Rapid navigation and refresh behaviour

- [ ] 3.1 `> set refresh.speed 0` (≈4.5 s refreshes). During one refresh press
      B, C, A, B, C quickly. **Expect**: exactly one further refresh, showing
      projects. `> diag display`: `coalesced` grew, `event overflow 0`,
      `panel ok`.
- [ ] 3.2 A while on the badge: no refresh; `renders suppressed` +1.
- [ ] 3.3 `> refresh clean` idle and during a refresh: no hang, no garbled image.
- [ ] 3.4 `> set title X`, `> set title Y`, …: partial refreshes, then a full one
      after `refresh.max_partials` (5). Note the ghosting.
- [ ] 3.5 Hold UP/DOWN on projects for 10 s: one long-press action only, no
      runaway refreshes. `> revert`, `> set refresh.speed 1`.
- [ ] 3.6 LED lights only while refreshing.

## 4. CLI commands and malformed input

- [ ] 4.1 `> help`, `> fields`, `> get`, `> export`: complete, end in `OK`.
- [ ] 4.2 Errors end in `ERR` and change nothing (`> status` still `saved`):
      `bogus`, `set nope 1`, `set layout 7`, `set layout`, `set sleep.timeout_s 5`,
      `set qr.payload ftp://x`, `set name "unterminated`, `set name a\q`,
      `screen recovery`, `project 0`, `diag nonsense`, `refresh fast`.
- [ ] 4.3 A line over 480 bytes: `ERR line too long`, next command works.
- [ ] 4.4 Control bytes: send `set name A<NUL>B` and an arrow key (ESC `[A`)
      in a line → `ERR control character 0x00` / `0x1b`, name unchanged.
- [ ] 4.5 Backspace and Ctrl-C at an interactive terminal behave; after
      Ctrl-C the next line runs normally.
- [ ] 4.6 Type half a command, wait > 30 s, then send `status`: runs normally
      (stale partial line dropped).
- [ ] 4.7 UTF-8: `> set title Ingenieur – Möbel`, glyphs render; `> revert`.
- [ ] 4.8 `badgerctl.py cmd status` twice, and after killing a terminal
      mid-line: always a clean `OK`.

## 5. Settings save / reboot persistence

- [ ] 5.1 `> set name Test Person`, `> commit`: prints slot, sequence and a
      duration of tens of ms. Unplug, replug: "Test Person".
- [ ] 5.2 Two more commits: slots alternate A/B, sequence increases
      (`> diag settings`).
- [ ] 5.3 `> set name Other` (no commit), `> reboot`: old committed name.
- [ ] 5.4 Power cut during commit: loop `badgerctl.py cmd commit` and pull USB
      mid-loop, 5 times. **Expect**: old or new name, never defaults; `recovered`
      may appear.
- [ ] 5.5 `> commit` while a slow refresh runs (dual-core, flash lockout):
      commit succeeds, refresh completes, no garbled image.
- [ ] 5.6 `badgerctl.py backup local/settings-backup.txt`, then `> defaults`,
      `> commit`, then `badgerctl.py restore local/settings-backup.txt`: settings return.
- [ ] 5.7 Flash `badger_badge-assets.uf2`: `> diag assets` shows `flash pack: ok`;
      settings survive. Reflash the firmware UF2: settings survive.

## 6. Missing-sensor behaviour (no APDS-9960 fitted)

- [ ] 6.1 `> diag gesture`: `sensor not detected`, i2c errors small, probes 1.
- [ ] 6.2 UP long: gesture mode on; indicator appears **struck through** in one
      refresh; buttons still work; no further refreshes for 2 minutes (watch
      the panel and `diag display` counters).
- [ ] 6.3 `> diag gesture` over those 2 minutes: probes grow slowly (backoff up
      to 30 s), not every second.
- [ ] 6.4 UP long again: indicator gone; probing stops.

## 7. USB power indication

- [ ] 7.1 Status area shows `USB` on every screen. Never bars, never "charging".
- [ ] 7.2 `> diag battery`: `USB`, raw counts plausible; no bars claimed.
- [ ] 7.3 No auto power-off on USB after `sleep.timeout_s` (default 120 s).
- [ ] 7.4 `> sleep` (or DOWN long): badge image drawn, `power off` printed,
      board keeps running; any button reboots; `> diag reset` → `wake from USB sleep`.

## 8. QR scanning with two phones

Use the provisional local default (trimmed offline vCard) and the GitHub-link
alternative. Final QR selection is pending this test.

- [ ] 8.1 `> diag qr` for each payload: version, px/module, size.
- [ ] 8.2 For each payload × {card, full-screen QR} × {phone 1 (iOS camera),
      phone 2 (Android camera)}: scan at 15, 25, 40 cm under hall-like
      lighting. Record time-to-decode and the decoded text (exact match?).
- [ ] 8.3 Tilted ±30° and at arm's length. Record failures.
- [ ] 8.4 Decide: vCard (offline, 2 px modules) vs GitHub link (larger modules).
      Commit the choice to `local/profile.json`.

## 9. Controlled watchdog / crash recovery

`crashtest <kind> confirm` fails on purpose through the real handlers. Run
each once, reconnect the terminal after the reboot, then `> diag reset`.
After checking 9.1 and 9.3–9.6, run `> reboot` (clears the crash streak), so
that only 9.7 reaches the three-in-a-row safe-mode threshold.

- [ ] 9.1 `> crashtest panic confirm` → reboot; `panic/assert`, message
      `crashtest panic`, streak 1.
- [ ] 9.2 `> reboot` → `reboot command`, streak 0.
- [ ] 9.3 `> crashtest fault0 confirm` → `hard fault`, message `hard fault pc=… core0`.
- [ ] 9.4 `> crashtest fault1 confirm` → `hard fault … core1`.
- [ ] 9.5 `> crashtest hang1 confirm` → reboot after ~8 s; `watchdog timeout`,
      message `core1 display heartbeat stalled`.
- [ ] 9.6 `> crashtest hang0 confirm` → reboot after ~5 s; `watchdog timeout`, no message.
- [ ] 9.7 Three crash tests in a row (e.g. 9.1, 9.3, 9.6) without 60 s of
      uptime between: **SAFE MODE** screen, stored settings not applied,
      CLI works, `> crashtest hang1 confirm` refused (single-core). `> reboot`
      leaves safe mode.
- [ ] 9.8 RST during a refresh: `reset button`, streak unchanged, one full refresh.
- [ ] 9.9 A+C held while pressing RST: safe mode (forced).
- [ ] 9.10 Unplug USB for > 10 s, replug: `power-on`, boot count 1, no stale
      message (the crash record from 9.x is discarded).

## Pending: battery (LiPo not yet available)

- [ ] Battery-only cold start from a button tap; power latch holds after release.
- [ ] Wake button selects the first screen; DOWN held through wake does not power off.
- [ ] DOWN long / `sleep.timeout_s` power-off with the image intact; off-current.
- [ ] Voltage calibration against a multimeter (`battery.cal_permille`), bars, LOW,
      hysteresis, brown-out point.

## Pending: gesture sensor (APDS-9960 not yet available)

- [ ] Detection on hot-plug, fault on cable pull and automatic recovery.
- [ ] IR emitter off whenever gesture mode is off, after timeout and in sleep.
- [ ] Orientation calibration (`gesture.rotation` / `gesture.mirror`).
- [ ] Real swipe reliability at 3/6/10 cm, lighting conditions, enclosure effects.
- [ ] Qwiic 3V3 rail off when powered off; no gesture wake.
