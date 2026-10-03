# Installing, configuring and recovering

## What replacing the stock firmware does

The Badger 2040 ships with MicroPython plus **BadgerOS**: the launcher,
apps, badge text and images live as files in a LittleFS filesystem at the
top of the 2 MiB flash. Its size depends on the release: the 2022 BadgerOS
(MicroPython v1.18, `os.uname()` machine "Pimoroni Badger2040 2MB with
RP2040") uses the top 1 MiB (0x100000–0x1FFFFF); later releases use the top
1408 KiB (0x0A0000–0x1FFFFF). Either way it covers the regions below.

- Flashing `badger_badge.uf2` overwrites only the sectors the image covers
  (0x000000–0x02C6FF today). MicroPython stops working at once.
- The BadgerOS files are **not** erased by flashing, but this firmware keeps
  its settings at 0x1FC000–0x1FFFFF and its asset pack at 0x1E0000–0x1EFFFF,
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
   photo badge (a full refresh is estimated at about 2.5 s at the default
   speed; not yet measured on a badge).
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
python3 tools/badgerctl.py push local/badge.toml     # a filled-in form (docs/PERSONALIZE.md), or
python3 tools/badgerctl.py push local/profile.json   # a JSON profile; both are validated locally, staged, committed
python3 tools/badgerctl.py backup local/settings-backup.txt
python3 tools/badgerctl.py cmd "diag qr"
```

### Updating from firmware before the project portfolio

BHIHKH! firmware with the project portfolio stores settings in format 2
(2 × 8 KiB slots at 0x1FC000). On the first boot after the update it finds
the old format-1 record and keeps using it: `status` shows
`v1 (migrate on commit)`. Nothing is written until the next `commit`, which
goes to a slot that does not overlap the old record (see FORMATS.md).

The old record contains the old project list (up to 4 entries) and untyped
contacts. Its values win over the new built-in defaults, so the new
portfolio and the contact icons do **not** appear on their own. To adopt
them, after flashing, either:

```bash
python3 tools/badgerctl.py backup local/settings-before-update.txt   # optional, keeps the old values
python3 tools/badgerctl.py push local/profile.json                   # new portfolio + contact types, committed
```

or, to return to the defaults compiled into the firmware:
`badgerctl.py cmd defaults` followed by `badgerctl.py cmd commit`.

Changes made with `set` appear on the display immediately but are **not
saved** until `commit`. Flash is written only on `commit` (never on button
presses), and each commit is verified by reading it back.

## 4. Battery

- The original Badger 2040 has a JST battery input and **no charger**. This
  firmware does not charge anything. It is configured for a single-cell
  LiPo, which must be protected and charged off-board. Check the cell's
  connector polarity against the board before plugging it in.
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
- Every screen shows a four-bar meter tuned for a **single-cell LiPo**, plus
  `LOW`, `?` (invalid reading) or `USB`. Validate it against a multimeter
  and calibrate it as described in [BATTERY.md](BATTERY.md). A powered-off
  badge shows the last measurement.

## 5. Recovery

| Situation | What happens / what to do |
|---|---|
| Power lost during a `commit` | The write goes to the *other* A/B slot. The previous record keeps a valid CRC and loads on the next boot. `diag settings` reports `recovered`. |
| Settings corrupt or from an unknown format | Invalid records are ignored and factory defaults are used. Individually invalid fields fall back to their defaults. |
| Asset pack missing or corrupt | The built-in pack is used; if there is none, the layouts drop the portrait and use the full width. `diag assets` shows the reason. |
| Panel not responding (BUSY held low) | Resets are bounded: the badge keeps running without the display, retries every 2–60 s and redraws once the controller answers. `diag display` shows `panel NOT RESPONDING`. |
| Firmware hangs or crashes | The watchdog (5 s) reboots. The reason (watchdog / panic message / hard-fault PC) survives the reboot and shows in `diag reset` and on the info screen. |
| 3 crashes in a row | **Safe mode**: stored settings are not applied, the display runs single-core, and a SAFE MODE screen is shown. Fix it over USB (`defaults`, `commit`, `reboot`). |
| Force safe mode | Hold **A + C** while pressing RST (USB) or while waking (battery). |
| Anything else | BOOTSEL (hold BOOT/USR, tap RST) always works; re-flash the UF2. `reboot bootsel` does the same from the CLI. |
| Return to stock | Flash Pimoroni's `with-badger-os` UF2, or restore your `picotool save` image. |

## Badger 2350

Everything above describes the original Badger 2040. On the **Badger 2350**
(`badger2350_badge.uf2`, built with `-DBHIHKH_TARGET=badger2350`) the
product is the same; the differences are below. **This target has not yet
been validated on a physical badge**: treat these steps as the plan for
[BADGER2350_SMOKE_TEST.md](BADGER2350_SMOKE_TEST.md), not as tested
instructions.

- **What it replaces.** The badge ships with Pimoroni's BadgeWare
  (MicroPython): 2 MiB firmware, a 1 MiB ROMFS, a 12 MiB FAT filesystem with
  the apps (the "Badger2350" drive) and 1 MiB reserved at the top. Flashing
  this firmware replaces the firmware region. Its asset pack
  (0xFE0000–0xFEFFFF) and settings (0xFFC000–0xFFFFFF) live in the reserved
  top megabyte; the FAT data stays in place but is not used.
- **Back up**: in BOOTSEL mode, `picotool save -a badger-2350-full-backup.bin`
  (16 MiB; picotool with USB support).
- **Flash**: connect USB-C; on the back, hold **BOOT**, tap **RESET**,
  release BOOT. The `RP2350` drive appears (Pimoroni's procedure). Copy
  `badger2350_badge.uf2`; optionally, in BOOTSEL again,
  `badger2350_badge-assets.uf2`. Use only the files named for your badge:
  they carry the RP2350 UF2 family, `scripts/verify_artifacts.py --target
  badger2350` checks them, and the Badger 2040's `badger_badge*.uf2` fail
  that check.
- **Buttons**: A, B, C, UP and DOWN as on the Badger 2040; **HOME** takes
  the USR button's role (diagnostics, layout toggle). Safe mode: hold
  **A + C** while pressing RESET or while waking the badge.
- **Power off / sleep**: no power latch. On battery the chip powers down
  after the sleep image has finished; any front button wakes it, which
  boots the firmware again (as a Badger 2040 battery wake). On USB, sleep is
  emulated as on the Badger 2040.
- **Battery**: the badge has a charger. The status area shows `USB` while
  VBUS is present, never "charging" (the charge-status line is on the
  wireless chip, which this firmware does not use). The meter's conversion
  is BadgeWare's (BATTERY.md); its accuracy is unvalidated.
- **Display**: full refreshes only (the reference SSD1680 driver has one
  waveform and no partial update), so `refresh.speed` and `refresh.partial`
  have no effect; `A long` still requests a clean full refresh. Black and
  white only.
- **Not used**: Wi-Fi/Bluetooth, the RTC, the rear lights and PSRAM.
- **Return to stock**: flash Pimoroni's
  `badger-vX.X.X-micropython-with-filesystem.uf2` (it rewrites the firmware
  and the apps), or restore your `picotool save` image.
