# Installation and recovery

**Back up first.** These instructions replace the factory software. Avoid a flash-nuke UF2 unless you actually intend to erase the badge. Flashing the firmware may leave portions of the old filesystem physically present, but later asset or settings writes make it unusable.

## Preserve the factory installation

For the Badger 2040, while BadgerOS still runs, copy personal files (for example `badges/`, `images/`, `books/`) using Thonny or `mpremote`. A complete flash image is the more faithful recovery option.

With a compatible `picotool` and the device in BOOTSEL mode:

```bash
picotool save -a badger-2040-full-backup.bin
# Or, on the Badger 2350:
picotool save -a badger-2350-full-backup.bin
```

Keep these private backups out of Git. The 2350 has 16 MiB of flash, so its full image is much larger.

## Flash the right image

**Badger 2040:** connect USB-C; hold **BOOT/USR**, tap **RST**, release. Copy `badger_badge.uf2` to **RPI-RP2**. If you created a separate asset pack, reenter BOOTSEL and copy `badger_badge-assets.uf2` separately.

**Badger 2350:** on the back, hold **BOOT**, tap **RESET**, release. Copy `badger2350_badge.uf2` to **RP2350**. Use `badger2350_badge-assets.uf2` only for this board.

The optional asset UF2 carries a different flash region. A separate installed asset pack can override the portrait bundled into firmware. Verify artifacts before flashing; see [builds and artifacts](Builds-and-Artifacts.md).

## Configure and verify

```bash
python3 tools/badgerctl.py cmd "version"
python3 tools/badgerctl.py cmd "status"
python3 tools/badgerctl.py cmd "selftest"
python3 tools/badgerctl.py backup local/settings-backup.txt
```

USB `set` changes are staged and visible before persistence. Run `commit` explicitly to save, or use `revert` to abandon them. A `push` of a validated form or profile stages and commits it.

## Recovery map

| Symptom | Documented recovery |
|---|---|
| Settings write interrupted | A/B CRC-checked settings normally retain the prior valid slot; inspect `diag settings`. |
| Unknown or corrupt settings | Fallback to compiled defaults; use `defaults` then `commit` if appropriate. |
| Three consecutive crashes | Safe mode disables the stored settings and uses a conservative display path; repair over USB. |
| Need safe mode explicitly | Hold **A + C** during reset/wake. |
| Panel reports not responding | Driver times out and retries; use `diag display`; CLI may remain available. |
| Bad portrait asset pack | Built-in portrait fallback or portrait-free layout; inspect `diag assets`. |
| Firmware cannot boot | Use hardware BOOTSEL and reflash the correct verified UF2, or restore a saved stock image. |

A Badger 2040 whole-flash restoration uses `picotool load badger-2040-full-backup.bin -o 0x10000000` (or `-t bin`), **only after verifying the file, board and tool command**. Returning to factory software can also use Pimoroni's appropriate *with filesystem/BadgerOS* image. The 2350 stock recovery image differs.

**Caution:** Format-1 settings from older project-portfolio firmware are read-only migrated to format 2 on the next `commit`. Existing settings override new built-in defaults until you explicitly choose otherwise.

Canonical, board-specific detail: [INSTALL.md](../docs/INSTALL.md), [FORMATS.md](../docs/FORMATS.md). Do not rely on this summary alone for a destructive restore.
