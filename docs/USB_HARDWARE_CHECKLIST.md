# USB-only hardware checklist

For a Badger 2040 on USB power only, with **no battery and no APDS-9960**
fitted. Every step records pass/fail, the firmware `version` line and notes.
Battery and sensor checks are listed at the end as **pending** until the parts
arrive; the full procedures for those are in
[HARDWARE_SMOKE_TEST.md](HARDWARE_SMOKE_TEST.md) §D, §I and §J.

Conventions: `>` lines are CLI commands (a terminal on the badge's USB serial
port, or `python3 tools/badgerctl.py cmd "…"`). Every command ends in `OK` or
`ERR …`. "Refresh" means one visible e-paper update.

## Test record

### 2026-10-01: first USB run, local build `023b3c6` (reported by the owner)

These results were reported by the badge owner from a physical test. They
were not observed or reproduced by the tooling that maintains this file.

- **Build:** personalized firmware reported as `023b3c6`
  (`badger_badge.uf2` SHA-256
  `8215a6e292ceb341fbba60d4eda1a284cbecbabdbb8fc5f856eaf4ab1904cd96` in the
  delivered firmware ZIP). On 2026-10-01 that commit and its four
  predecessors (`abddac0`…`023b3c6`) existed **only in the local
  checkout**. They were not on the remote branch, whose head (and PR #1's)
  was still `766aede`. These results therefore apply to that local build and
  do **not** validate the remote head. The `> version` line that would
  confirm the flashed commit on the badge is still pending (1.2).
  *Update, later on 2026-10-01:* these commits were then pushed unchanged to
  `feature/formnext-2026-badge` (fast-forward to `7e26f30`, which adds only
  this record). The results still apply to the firmware built from
  `023b3c6`; later commits are not covered by them.
- **Passed (reported):**
  - Personalized firmware installed (1.1); startup time not measured.
  - Both badge layouts, the updated portrait and title, and the GitHub and
    Discord icons beside their usernames (2.1, 10.8).
  - All seven projects in order, with BHIHKH! last, and CatScan-MS showing
    its banner with no QR hint (2.2, 10.6).
  - Buttons and portfolio navigation, project QR entry and return, and the
    remembered project position (2.1, 2.2, 10.5, 10.6).
  - Rapid navigation during a refresh (3.1, visual part).
  - Scans of the contact vCard and all six repository QRs, each opening the
    expected destination (10.6, 10.7). This does **not** establish the
    two-phone test in §8.
  - USB unplugged and reconnected: settings survived and the unpowered
    display kept its image.
- **Not confirmed:** `badgerctl.py push local/profile.json` (10.3) was not
  separately confirmed. Do not infer it from the content seen on the badge.
- **Still pending (needs a desk session):**
  - `version`, `selftest`, `diag all`, `diag mem` and `diag refresh` output.
  - Malformed CLI input (§4).
  - Controlled crashes and watchdog recovery, safe mode and real reset
    classification (§9).
  - Interrupted-commit recovery (5.4) and measured refresh timings (10.9).
  - Two-phone QR testing (§8).
  - Battery and APDS-9960 tests (sections at the end).

### Project index / quiet browsing (branch `feature/project-index-quiet-browsing`)

**Not yet tested on hardware.** This firmware changes the C button (short C
now waits ~0.35 s for a possible double press; long C now opens project 1;
diagnostics moved to USR short) and adds the project index (§11). The
`023b3c6` results above do not cover it. Items 2.1 and 2.3 changed
meaning, so re-run them along with §11 on the first build of this branch.

### Before first flash (September 2026)

Status before the first flash: **all steps pending**; 0.1 done read-only.
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
- [x] 0.3 **Whole-flash image**: BOOTSEL (hold BOOT/USR, tap RST), then
      `picotool save -a badger-2040-full-backup.bin` and
      `picotool info -a > badger-2040-info.txt`. Keep the image with its SHA-256.
      *Done before the first flash (owner): original BadgerOS full-flash
      backup, 2,097,152 bytes, SHA-256
      `22404407fadf23eb97984afb2db98251c5ed8912bc85583619a91d6034ba6943`
      (verified). Kept privately, outside this repository. Restoring it to
      the badge has not been tested.*
- [ ] 0.4 Verify the artifacts about to be flashed: `scripts/verify_artifacts.py build/fw --require-clean`
      (or check `SHA256SUMS` of the CI artifact). Record the commit.

Flashing (step 1) replaces MicroPython and needs the owner's explicit go-ahead.

## 1. Flash, startup and identity

- [x] 1.1 BOOTSEL, copy `badger_badge.uf2` to `RPI-RP2`. **Expect**: one full
      refresh to the photo badge (layout **A**, portrait left) within ~3 s
      (estimate; record the measured time).
      *2026-10-01, local `023b3c6`: installed and started (reported); time
      not measured.*
- [ ] 1.2 Port enumerates as `2E8A:000A`. `> version` shows `BHIHKH!`, the commit
      (`git describe`, 12 hex digits) and `pico-sdk 2.2.0`.
- [ ] 1.3 `> selftest` → fonts ok, built-in assets ok, settings ok, event
      overflows 0, **display panel ok**, `OK`.
- [ ] 1.4 `> diag all`: reset `power-on` (or `reset button`), settings slots
      `erased` on first install, stacks `min free` > 1000 B on both cores
      (record both values), `flash image` matches `memory-report.txt`.
- [ ] 1.5 `> status` → `power usb`.

## 2. Buttons, screens and layout selection

- [x] 2.1 A / B / C short: badge / card / projects. One refresh each, no
      double refresh. *2026-10-01, local `023b3c6`: passed (reported).*
- [x] 2.2 Projects: DOWN/UP step through `PROJECT 1/7` … `7/7` and wrap, in
      the configured order (Jump Jet, DragonBreath, DragonSniff, DragonBench,
      dragon-core, CatScan-MS, BHIHKH! last). Jump Jet's tag reads "Work in
      progress"; CatScan-MS shows only its banner, with no link or QR hint.
      (Before the BHIHKH! update this step checked a two-page portfolio.)
      *2026-10-01, local `023b3c6`: passed (reported).*
- [ ] 2.3 Long presses: A = slow clean refresh; B = full-screen QR (card if no
      QR); C = project 1 (since the project index; was diagnostics, now USR
      short); USR = layout **B** (portrait right) for this session, USR
      again = A. USR short: diagnostics.
      *2026-10-01, local `023b3c6`: both layouts and the long-B QR screens
      seen (reported); A long, C long and the USR toggle not individually
      reported. Still open.*
- [ ] 2.4 `> set layout 1`, `> commit`, `> reboot`: boots in B. `> set layout 0`,
      `> commit`: back to A (the default).
- [ ] 2.5 Glancing tap (< 20 ms): no action. A+B held together, released in
      turn: badge, then card.
- [ ] 2.6 Card with missing fields: `> clear contact2.value` removes that
      line entirely (no orphan label); `> clear qr.payload` leaves **no** QR,
      box or placeholder text. The text uses the full width. `> revert`.

## 3. Rapid navigation and refresh behaviour

- [ ] 3.1 `> set refresh.speed 0` (refreshes estimated at ≈4.5 s). During one refresh press
      B, C, A, B, C quickly. **Expect**: exactly one further refresh, showing
      projects. `> diag display`: `coalesced` grew, `event overflow 0`,
      `panel ok`.
      *2026-10-01, local `023b3c6`: rapid navigation during a refresh behaved
      correctly (reported, visual). The `diag display` counters are pending.*
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
      *2026-10-01, local `023b3c6`: after unplugging and reconnecting USB,
      the settings in use survived and the unpowered display kept its image
      (reported). The `commit` output and this exact procedure are still open.*
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

*2026-10-01, local `023b3c6`: the contact vCard and all six repository QRs
were scanned successfully and opened the expected destinations (reported).
This was not the two-phone matrix below, which is still open.*

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

## 10. BHIHKH! portfolio update (format-2 settings, icons, project QR)

- [ ] 10.1 Before flashing the update: `badgerctl.py backup local/settings-before-update.txt`.
- [ ] 10.2 Flash the new UF2. **Expect**: the old name/contacts/projects are
      still shown; `> status` shows settings `v1 (migrate on commit)`;
      `> diag settings` names the legacy slot; contacts show text labels (no icons yet).
- [ ] 10.3 `badgerctl.py push local/profile.json`. *(Not confirmed as of
      2026-10-01: do not infer from the content on the badge.)* **Expect**: commit to
      **slot A**, sequence continues; `> reboot`: the new 7-entry portfolio and
      the GitHub/Discord icons persist; `> status` shows `slot A`.
- [ ] 10.4 Two more commits alternate B/A; reboot after each loads the newest.
- [ ] 10.5 C opens `PROJECT 1/7`; DOWN ×3, A, C: reopens at `PROJECT 4/7`.
      UP from 1/7 wraps to 7/7. No flash write while browsing (`> diag settings`
      commit count unchanged).
      *2026-10-01, local `023b3c6`: navigation, wrap and the remembered project
      position passed (reported). The commit-count check is pending.*
- [ ] 10.6 Long B on each linked project: the QR shows that repository; scan
      with both phones and compare against the footer URL. Long B again and UP/DOWN
      return to the same project. CatScan-MS (6/7): banner only, long B does nothing.
      *2026-10-01, local `023b3c6`: QR entry and return, and all six
      repository QRs scanned to the right destination (reported). The
      CatScan-MS banner was shown with no QR hint. Still open: the two-phone
      comparison and confirming that long B on CatScan-MS does nothing.*
- [ ] 10.7 Short B from a project: contact card; long B there: the contact QR
      (unchanged payload), not a project QR.
      *2026-10-01, local `023b3c6`: the contact vCard scanned successfully
      (reported); the short-B-from-a-project path was not individually
      reported.*
- [x] 10.8 Icons at arm's length: GitHub and Discord marks recognisable,
      aligned with their usernames; no "GitHub"/"Discord" words beside them.
      *2026-10-01, local `023b3c6`: icons checked on the panel (reported).*
- [ ] 10.9 Browse 5 screens, then `> diag refresh`: one entry per press, no
      unrequested entries. Record the measured `busy` time at speed 1 (estimated
      ≈ 2.5 s, see REFRESH.md) and update REFRESH.md with it. Record a
      video alongside and compare the phase timing.
- [ ] 10.10 Optional: `> set refresh.speed 2`, browse the portfolio, judge
      ghosting versus the shorter flash; `> revert`.

## 11. Project index and quiet browsing

Run on the first build of `feature/project-index-quiet-browsing`; record its
`> version` line. The detailed expectations are in
[HARDWARE_SMOKE_TEST.md](HARDWARE_SMOKE_TEST.md) §B2.

- [ ] 11.1 Short C from badge/card: the remembered project after ~0.35 s, one
      refresh. Double C: the index directly (no project page first), with
      the remembered project highlighted and `n/7` in the header.
- [ ] 11.2 Readability at arm's length: names, highlight bar, `n/N`, hint
      line; the battery/USB status top right is untouched.
- [ ] 11.3 Single DOWN taps: one refresh per tap, about 0.3 s after release.
      Four quick taps: one refresh at the end. `> status` during browsing
      shows `index highlight n (drawn m)`.
- [ ] 11.4 Hold DOWN ~2.5 s: no refresh while held, no power-off; stops at
      7/7. Hold UP: back to 1/7, gesture indicator unchanged.
- [ ] 11.4a Hold DOWN ≥ 3 s in the index: emulated sleep on USB (badge drawn,
      `power off` printed); no project opened, no highlight drawn. A button
      wakes it; short C opens the previously remembered project.
- [ ] 11.5 C confirms at once and opens only the selected project; C from the
      card then reopens that project. A cancels to the previous screen
      (try from a project QR: back to the same QR) and keeps the remembered
      project.
- [ ] 11.6 Long C on project 4 and on a project QR: project 1. Nothing else on
      release. Inside the index: also project 1.
- [ ] 11.7 `> set refresh.speed 0`; double C, tap DOWN several times during the
      index refresh, then C. **Expect**: the index refresh finishes, then one
      refresh to the final project, no intermediate frame. `> diag refresh`
      lists exactly those frames. `> revert`.
- [ ] 11.8 Pending C, at `> set refresh.speed 0`: tap C, then within 0.35 s
      hold B → one refresh to the remembered project's QR (scan: that
      project's link, not the vCard); C then A → one refresh to the badge,
      no project page; C then `> screen badge` → badge, nothing later.
      `> diag refresh` shows one frame each. `> revert`.
- [ ] 11.8a USR long on the badge: layout toggles with one refresh, no
      diagnostics frame. USR short: diagnostics.
- [ ] 11.9 No flash writes while browsing: `> diag settings` commit count
      unchanged after a session in the index.
- [ ] 11.10 `> screen index 5`, `> project next`, `> status`: the index with 6
      highlighted; `screen index 9` → `ERR` (only 7 projects).
- [ ] 11.11 CatScan-MS from the index: teaser page with "TOP SECRET - COMING
      SOON", no QR hint; long B there does nothing.
- [ ] 11.12 `> sleep` from the index (USB emulated sleep), then a button: the
      wake screen, no delayed index or C action.

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
