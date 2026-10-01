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
- A line containing a raw control byte (NUL, ESC/arrow keys, …; TAB is
  allowed) is rejected whole: `ERR control character 0x.. in line`.
- A partial line left idle for 30 s is discarded silently, so a script
  starting later is never glued onto stale input. `badgerctl.py` also sends
  Ctrl-C first.
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
| `screen badge\|card\|projects [n]\|project-qr [n]\|index [n]\|qr\|info` | Show a screen (`n` = 1-based project). Unavailable screens (e.g. `project-qr` for a project without a link) return `ERR`. `index [n]` opens the project index with entry `n` highlighted (default: the remembered project); it does not change the remembered project. While the index is shown, `project next\|prev` moves the highlight, and `status` adds `index highlight n (drawn m)`. A USB screen change cancels a short C that is still waiting for its double-press window. |
| `project next\|prev\|<n>` | Navigate projects |
| `fields` | Every key with type, limits and help |
| `get [key]` | Staged value(s), quoted and escaped |
| `set <key> <value>` | Validate and stage a change; the display updates immediately |
| `clear <key>` | Stage an empty text value (hides the optional line) |
| `commit` | Write staged settings to flash (A/B slot, read back and verified). In safe mode the stored record is not loaded, so `commit` is refused unless `defaults` was run first (explicitly saving defaults); otherwise reboot normally to edit the stored settings. |
| `revert` | Discard staged changes |
| `defaults` | Stage the factory defaults (compiled-in profile); needs `commit` |
| `export` | Print `set ...` lines that recreate the staged settings |
| `refresh [clean]` | Redraw; `clean` forces a full refresh with the slow OTP waveform |
| `diag [all\|reset\|mem\|flash\|settings\|assets\|display\|refresh\|qr\|battery\|gesture]` | Diagnostics; `diag battery` takes a fresh reading and shows raw ADC counts; `diag refresh` lists the last 16 refreshes with timing, mode and reason ([REFRESH.md](REFRESH.md)) |
| `selftest` | Verify font tables, built-in assets, settings validity and queue health |
| `gesture on\|off` | Gesture mode (not saved; see `gesture.default_on`) |
| `echo on\|off` | Terminal echo |
| `sleep` | Power off (battery) or emulated sleep (USB), after the current refresh |
| `reboot [bootsel]` | Reboot, optionally into the USB bootloader |
| `crashtest hang0\|hang1\|panic\|fault0\|fault1 confirm` | Recovery test: fail on purpose (core-0 hang, core-1 stall, panic, hard fault on core 0/1). The watchdog reboots; `diag reset` shows what was caught. Three in a row enter safe mode. Core-1 kinds are refused in single-core/safe mode. |

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
| `contact1..6.type` | `email`, `phone`, `web`, `github`, `discord`, `text` or empty | Explicit, never guessed from the label. `github`/`discord` lines show the icon instead of the label. Empty = text label (older profiles) |
| `qr.payload` | text 383 | `https://…` URL or `BEGIN:VCARD…`; empty = no QR drawn at all (card text uses the full width) |
| `qr.caption` | text 39 | Shown next to the code |
| `project1..12.title` | text 39 | Empty hides the project; pages are numbered over the configured ones |
| `project1..12.tagline` | text 63 | Bold, up to 2 lines |
| `project1..12.body` | text 199 | Wrapped; `\n` for line breaks |
| `project1..12.status` | text 47 | Optional outlined tag, e.g. a version |
| `project1..12.link` | text 71 | Empty or `https://…` (no spaces). Shown without `https://` in the footer; long B shows it as a QR |
| `project1..12.banner` | text 39 | Optional black teaser band, e.g. `TOP SECRET - COMING SOON` |
| `layout` | 0..1 | 0 = portrait left (default), 1 = portrait right |
| `refresh.speed` | 0..3 | 0 = OTP waveform, 1 = medium (default), 2 = fast, 3 = turbo (more ghosting). Estimated full-refresh times, not yet measured: ~4.5 / ~2.5 / ~0.9 / ~0.26 s ([REFRESH.md](REFRESH.md)) |
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
