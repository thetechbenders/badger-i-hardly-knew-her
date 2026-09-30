# Installing, configuring and recovering

## What replacing the stock firmware does

The Badger 2040 ships with MicroPython plus **BadgerOS**: the launcher,
apps, badge text and images live as files in a LittleFS filesystem in the
top 1408 KiB of the 2 MiB flash (0x0A0000–0x1FFFFF).

- Flashing `badger_badge.uf2` overwrites only the sectors the image covers
  (0x000000–0x029100 today). MicroPython stops working at once.
- The BadgerOS files are **not** erased by flashing, but this firmware keeps
  its settings at 0x1FE000–0x1FFFFF and its asset pack at 0x1E0000–0x1EFFFF,
  both inside the old filesystem area. The first `commit` or asset flash
  corrupts that filesystem. Treat the BadgerOS files as lost unless you
  back them up.
- To go back, flash Pimoroni's `…-micropython-with-badger-os.uf2` release. It
  rewrites the firmware *and* the filesystem, so it recreates a stock BadgerOS
  and ignores anything this firmware left behind.

## 1. Back up (do this first)

Either or both of these:

- **Files only**: with the stock firmware running, copy your BadgerOS files
  (e.g. `badges/badge.txt`, `badges/badge.jpg`, `images/`, `books/`) with
  Thonny or `mpremote cp -r :/ ./badger-backup/`.
- **Whole flash image** (restores exactly): put the badge in BOOTSEL mode
  (hold **BOOT/USR**, tap **RST**), then run
  `picotool save -a badger-2040-full-backup.bin`. This needs picotool built
  with USB support (see BUILD.md). Restore later with
  `picotool load badger-2040-full-backup.bin -o 0x10000000` (or `-t bin`).

Never run a "flash nuke" UF2 unless you intend to wipe everything.

## 2. Flash

1. Connect USB-C to the computer.
2. Hold **BOOT/USR**, tap **RST**, release BOOT/USR. The `RPI-RP2` drive appears.
3. Copy `badger_badge.uf2` to the drive. The badge reboots and draws the
   photo badge (a full refresh takes about 2 s at the default speed).
4. Optional: to install the portrait separately from the firmware, enter
   BOOTSEL again and copy `badger_badge-assets.uf2`. It writes only the asset
   region. A valid flashed pack takes precedence over the portrait built into
   the firmware.

With picotool: `picotool load -x badger_badge.uf2` (and
`picotool load badger_badge-assets.uf2`). The asset UF2 carries its own
target address.

## 3. Configure

Configure from the USB serial port (`/dev/ttyACM*`, `COMx`, 115200 baud, any
terminal), or with `tools/badgerctl.py`:

```bash
cp config/sample-profile.json local/profile.json    # edit: name, title, contacts, qr.payload, projects
python3 tools/badgerctl.py push local/profile.json   # validates locally, stages, commits
python3 tools/badgerctl.py backup local/settings-backup.txt
python3 tools/badgerctl.py cmd "diag qr"
```

Changes made with `set` appear on the display immediately but are **not
saved** until `commit`. Flash is written only on `commit` (never on button
presses), and each commit is verified by reading it back.

## 4. Battery

- The original Badger 2040 has a JST battery input and **no charger**. This
  firmware does not charge anything. Use the battery types Pimoroni
  specifies for the board, and recharge or replace them off-board.
- **On battery**, a front button press powers the board and the firmware
  latches power (GPIO10) within milliseconds of boot. After
  `sleep.timeout_s` without input, or a long press of DOWN, the badge draws
  the photo badge, waits for the refresh to finish, and releases the latch.
  The RP2040 is then fully unpowered. The e-paper keeps its image.
- **Waking is a cold boot.** RAM is lost. Settings come from flash and the
  wake button picks the first screen.
- **On USB** the board cannot switch itself off. "Power off" is emulated: the
  image stays, the CPU idles until all buttons are released and one is pressed
  again, then it reboots, which mimics a battery wake. USB serial commands are
  not processed during emulated sleep.
- Auto power-off never runs on USB, in safe mode, or while a refresh is in
  progress. `sleep.timeout_s 0` disables it.
- `battery.low_mv` (default 0 = off) shows a small battery mark on the badge
  and card when VSYS falls below the threshold. Pick a value that suits your
  battery; readings on USB power reflect VBUS, not the battery.

## 5. Recovery

| Situation | What happens / what to do |
|---|---|
| Power lost during a `commit` | The write goes to the *other* A/B slot. The previous record keeps a valid CRC and loads on the next boot. `diag settings` reports `recovered`. |
| Settings corrupt or from an unknown format | Invalid records are ignored and factory defaults are used. Individually invalid fields fall back to their defaults. |
| Asset pack missing or corrupt | The built-in pack is used; if there is none, the layouts drop the portrait and use the full width. `diag assets` shows the reason. |
| Firmware hangs or crashes | The watchdog (5 s) reboots. The reason (watchdog / panic message / hard-fault PC) survives the reboot and shows in `diag reset` and on the info screen. |
| 3 crashes in a row | **Safe mode**: stored settings are not applied, the display runs single-core, and a SAFE MODE screen is shown. Fix it over USB (`defaults`, `commit`, `reboot`). |
| Force safe mode | Hold **A + C** while pressing RST (USB) or while waking (battery). |
| Anything else | BOOTSEL (hold BOOT/USR, tap RST) always works; re-flash the UF2. `reboot bootsel` does the same from the CLI. |
| Return to stock | Flash Pimoroni's `with-badger-os` UF2, or restore your `picotool save` image. |
