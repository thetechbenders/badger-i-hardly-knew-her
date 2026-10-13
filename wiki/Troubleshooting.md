# Troubleshooting

Start by identifying the **hardware model**, the exact firmware version (`version`), power source, and whether the issue occurs in stock firmware, BHIHKH! on hardware, a host build, or Studio in a browser.

| Symptom | First checks |
|---|---|
| Badger stays blank after flashing | Confirm 2040 vs 2350 UF2, BOOTSEL volume, build verification, power and reset. See [installation](Installation-and-Recovery.md). |
| USB CLI not found | Check the data cable and identify the device with `python3 -m serial.tools.list_ports -v` (pyserial). The pinned SDK uses `2E8A:000A` for 2040, `2E8A:0009` for 2350; `badgerctl.py` auto-detects only the former. On 2350, pass `--port` before `cmd` as [shown here](USB-Command-Line.md). In one owner-reported Windows session, COM22 conflicted with an existing Qualcomm device; use the actual port/device path rather than assuming COM22. USB CDC differs from BOOTSEL storage. |
| USB command appears accepted but disappears after reboot | `set` is staged; `commit` is required. Check `diag settings` for A/B record status. |
| Safe mode or repeated watchdog resets | Inspect `diag reset`, `diag display`; hold A+C at reset to force safe mode; use `defaults` + `commit` deliberately if stored settings are suspect. |
| Portrait missing or wrong image | Check whether you built a portrait into firmware or flashed a separate, overriding asset pack. Run `diag assets`; check TOML and target portrait dimensions. |
| Incorrect QR | Run preview/decode checks and `diag qr`. Keep link/vCard within documented limits and use HTTPS links where required. |
| Image briefly goes dark/inverted during navigation | Usually the normal e-paper waveform, not a double-refresh. Compare `diag refresh` and [refresh phases](../docs/REFRESH.md). |
| Battery reads `?`, `LOW`, or empty with AAA | On the 2040, choose the fitted battery pack/chemistry. Validate with a multimeter on battery, not USB; see [power](Power-and-Batteries.md). |
| Badger 2350 shows USB but no charging status | Expected: the charger status signal is not read by this firmware. |
| Optional gesture sensor absent or doesn't respond | Check Qwiic cable, compatible chip ID, power and `diag gesture`; buttons should continue functioning. |
| Studio says invalid session token | The server likely restarted or you're reaching the wrong instance/port. Paste the **current** private launch URL into its reconnect form. |
| Studio cannot bind port | Inspect the process using that port; prefer `python -m studio.app --port 0`. Do not terminate someone else's process without confirming ownership. |
| Build fails due to board mismatch | Set `-DBHIHKH_TARGET` correctly and use separate build directories. Verify Pico SDK and picotool pins from `deps.lock`. |
| Badger 2350 tests differ from README status | Refer to dated physical test records; older prose can predate a later owner-run test. Do not extend that evidence to your new build. |

## Controlled diagnosis

On the 2350, select the port explicitly as shown in the [USB CLI examples](USB-Command-Line.md); the auto-detected commands below currently apply to the 2040.

```bash
python3 tools/badgerctl.py cmd "version"
python3 tools/badgerctl.py cmd "status"
python3 tools/badgerctl.py cmd "diag all"
python3 tools/badgerctl.py cmd "selftest"
```

**Do not paste private data into bug reports.** Collect numeric status and versions, redact personal QR payloads/contacts, and include the exact reproduction steps. For corrupt settings, check A/B recovery first. For stock restoration or whole-flash erase, see [installation and recovery](Installation-and-Recovery.md) and verify the board before writing.

References: [INSTALL.md](../docs/INSTALL.md), [STUDIO.md](../docs/STUDIO.md), [USB_CLI.md](../docs/USB_CLI.md), [HARDWARE_SMOKE_TEST.md](../docs/HARDWARE_SMOKE_TEST.md).
