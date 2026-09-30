# USB serial command line

The badge enumerates as a USB CDC serial port (Pico SDK stdio, VID:PID
`2E8A:000A`). Any baud rate works. Setting **1200 baud** reboots into
BOOTSEL (Pico SDK convention).

## Protocol

- One command per line; CR, LF or CRLF.
- Every command ends with a final line that is exactly `OK` or begins with
  `ERR `. Scripts should read until one of these.
- Echo is on by default for interactive terminals. `echo off` disables it
  (badgerctl does this).
- Lines longer than 480 bytes are rejected whole. Backspace and Ctrl-C work.
- Values: everything after `set <key> ` up to the end of the line (trailing
  spaces trimmed). Wrap the value in `"..."` to keep leading or trailing
  spaces. Escapes: `\n` (line break), `\"`, `\\`. Text must be UTF-8 without
  control characters. Glyphs cover ASCII, Latin-1 (ä ö ü ß é …) and a few
  typographic marks (– — ‘ ’ “ ” • … € →); anything else renders as `?`.

## Commands

| Command | Description |
|---|---|
| `help` | Command summary |
| `version` | Firmware version (git describe), build type, commit date, SDK/library/compiler versions |
| `status` | Current screen, power source, VSYS, idle time, unsaved-changes flag, display state |
| `screen badge\|card\|projects [n]\|qr\|info` | Show a screen (`n` = 1-based project). Unavailable screens return `ERR`. |
| `project next\|prev\|<n>` | Navigate projects |
| `fields` | Every key with type, limits and help |
| `get [key]` | Staged value(s), quoted and escaped |
| `set <key> <value>` | Validate and stage a change; the display updates immediately |
| `clear <key>` | Stage an empty text value (hides the optional line) |
| `commit` | Write staged settings to flash (A/B slot, read back and verified) |
| `revert` | Discard staged changes |
| `defaults` | Stage the factory defaults (compiled-in profile); needs `commit` |
| `export` | Print `set ...` lines that recreate the staged settings |
| `refresh [clean]` | Redraw; `clean` forces a full refresh with the slow OTP waveform |
| `diag [all\|reset\|mem\|flash\|settings\|assets\|display\|qr\|battery\|gesture]` | Diagnostics; `diag battery` takes a fresh reading and shows raw ADC counts |
| `selftest` | Verify font tables, built-in assets, settings validity and queue health |
| `gesture on\|off` | Gesture mode (not saved; see `gesture.default_on`) |
| `echo on\|off` | Terminal echo |
| `sleep` | Power off (battery) or emulated sleep (USB), after the current refresh |
| `reboot [bootsel]` | Reboot, optionally into the USB bootloader |

## Keys

Text limits are in UTF-8 bytes (one less than the storage size).

| Key | Type / limit | Notes |
|---|---|---|
| `name` | text 47 | Shown large; auto-sized 24 → 20 → 17 px, then ellipsized |
| `title` | text 55 | Role / subtitle, e.g. "Engineer + Maker" |
| `affiliation` | text 55 | Optional |
| `interests` | text 111 | Optional, wraps to 3 lines on the badge |
| `event` | text 31 | Optional event strip, e.g. "Formnext 2026" |
| `contact1..6.label` | text 15 | e.g. Work, Email, Phone, GitHub |
| `contact1..6.value` | text 71 | Empty value hides the whole line (label included); lines that do not fit the card are omitted, caption first |
| `qr.payload` | text 383 | `https://…` URL (recommended) or `BEGIN:VCARD…`; empty = "QR NOT CONFIGURED" placeholder |
| `qr.caption` | text 39 | Shown next to the code |
| `project1..4.title` | text 39 | Empty hides the project |
| `project1..4.tagline` | text 63 | |
| `project1..4.body` | text 199 | Wrapped; `\n` for line breaks |
| `project1..4.link` | text 71 | Short reference text |
| `layout` | 0..1 | 0 = portrait left (default), 1 = portrait right |
| `refresh.speed` | 0..3 | 0 = OTP waveform ~4.5 s, 1 = ~2 s (default), 2 = ~0.8 s, 3 = ~0.25 s (more ghosting) |
| `refresh.partial` | bool | Allow partial refresh for changes under 40 % of the screen |
| `refresh.max_partials` | 0..20 | Partial refreshes before a forced full refresh (default 5) |
| `sleep.timeout_s` | 0, 15..3600 | Battery auto power-off; 0 = never (default 120) |
| `sleep.screen` | 0..1 | 1 = show the photo badge before power-off (default) |
| `wake.selects_screen` | bool | Waking with B/C opens card/projects (default true) |
| `diag.single_core` | bool | Run the display service on core 0 (takes effect at next boot) |
| `led.level` | 0..255 | Activity LED while refreshing (default 24; 0 = off) |
| `battery.low_mv` | 0..4500 | Show `LOW` below this (default 3500; 0 = off; must be ≤ bar1) |
| `battery.bar1_mv`..`bar4_mv` | 3000..4500 | 1–4 bars at or above (defaults 3600/3700/3800/3950; strictly increasing) |
| `battery.hyst_mv` | 0..300 | Hysteresis around thresholds (default 40) |
| `battery.cal_permille` | 900..1100 | Multimeter calibration factor (default 1000) |
| `gesture.default_on` | bool | Gesture mode on after boot (default false) |
| `gesture.rotation` | 0..3 | Sensor mounting rotation × 90° clockwise |
| `gesture.mirror` | bool | Swap left/right after rotation |
| `gesture.sensitivity` | 10..90 | Minimum swipe strength (default 30) |
| `gesture.timeout_s` | 0..3600 | Gesture mode auto-off after idle (default 300; 0 = never) |
| `gesture.cooldown_ms` | 200..3000 | One swipe per interval (default 700) |

## Example session

```
> set title Embedded Engineer
staged (not saved; run 'commit')
OK
> set qr.payload https://example.com/dan
staged (not saved; run 'commit')
OK
> diag qr
qr: ok, version 2, 3 px/module, 99 px incl. quiet zone
OK
> commit
saved to slot B seq 4 (48210 us)
OK
```

(For a 23-byte URL: version 2, 25 modules + 2 × 4 quiet modules at 3 px = 99 px; 0.68 mm modules.)
