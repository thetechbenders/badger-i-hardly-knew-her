# USB serial command line

Both targets expose Pico SDK **USB CDC** serial. The documented VID:PID is `2E8A:000A`; port names vary by OS (for example Linux `/dev/ttyACM*`, Windows `COMx`, macOS `/dev/cu.usbmodem*`). Baud rate is ignored by USB CDC; **1200 baud intentionally enters BOOTSEL**.

Use an interactive serial terminal or the helper:

```bash
python3 tools/badgerctl.py cmd "version"
python3 tools/badgerctl.py cmd "status"
python3 tools/badgerctl.py cmd "fields"
python3 tools/badgerctl.py cmd "diag all"
python3 tools/badgerctl.py backup local/settings-backup.txt
```

## Staging versus committing

```text
> set title Embedded Engineer
staged (not saved; run 'commit')
OK
> set qr.payload https://example.com/alex
staged (not saved; run 'commit')
OK
> diag qr
...
OK
> commit
...
OK
```

`set` and `clear` modify the staged settings. `get` reads staged values; `revert` abandons them; `defaults` stages compiled defaults; **`commit`** verifies and persists to an A/B flash slot. In safe mode a commit without explicitly requesting defaults is refused, to avoid blindly replacing stored data.

## Command groups

| Commands | Intended use |
|---|---|
| `help`, `version`, `status` | Identify running firmware and current state |
| `fields`, `get [key]`, `set <key> <value>`, `clear <key>` | Inspect/stage configuration |
| `commit`, `revert`, `defaults`, `export` | Persistence and configuration recovery |
| `screen badge|card|projects|index|qr|info` | Force a screen over USB |
| `project next|prev|<n>` | Navigate portfolio or highlighted index entry |
| `refresh [clean]` | Redraw display; optionally force clean full refresh |
| `diag [all|reset|mem|flash|settings|assets|display|refresh|qr|battery|gesture]` | Collect evidence and measurements |
| `selftest`, `reboot`, `reboot bootsel` | Check and recover |

The full command table includes additional qualified screen forms and specific error behavior; use [USB_CLI.md](../docs/USB_CLI.md#commands), not this abbreviated cheat sheet, when scripting.

## Protocol and scripts

Each newline-terminated command ends with exactly `OK` or `ERR ...`. A partial line idle for 30 s is abandoned, and lines over 480 bytes or containing forbidden control characters are rejected without applying the command. Quote values that need preserved outer spaces. Supported glyphs are intentionally limited by the bitmap fonts; an unsupported character will appear as `?` if it passes an unvalidated path.

The settings namespace covers up to six contacts, twelve projects, QR modes, display layout/refresh, sleep/wake, diagnostics, battery type/thresholds and gesture sensitivity. **Badger 2350 rejects AAA pack selection**. Query `fields` on the actual device and consult the canonical [key table](../docs/USB_CLI.md#keys).

See [troubleshooting](Troubleshooting.md) for missing port, token confusion (Studio uses HTTP, not USB), and settings recovery.
