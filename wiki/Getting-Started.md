# Getting started

## 1. Identify the badge

Read [hardware and support](Hardware-and-Support.md) before downloading a UF2. The Badger **2040** is RP2040, 296×128, and uses `badger_badge.uf2`. The Badger **2350** is RP2350A, 264×176, and uses `badger2350_badge.uf2`. The two images are **not interchangeable**.

## 2. Back up stock firmware **before** changing it

Installing BHIHKH! replaces the stock MicroPython firmware. On the Badger 2040, its settings and asset regions overlap the previous BadgerOS filesystem; on the 2350, stock BadgeWare's firmware is replaced. Back up important personal BadgerOS files and preferably the whole flash image while the original environment is intact.

See [installation and recovery](Installation-and-Recovery.md) for BOOTSEL and `picotool` instructions.

## 3. Choose how to prepare your content

**Text-based, full build:** edit a private TOML form, run all validation, previews and compiler checks, then flash a personalized UF2.

```bash
scripts/build-badge.sh --new
$EDITOR local/badge.toml
# Put your portrait in local/, and set [portrait] photo in the form.
scripts/build-badge.sh --preview local/badge.toml
scripts/build-badge.sh local/badge.toml
```

For a 2350, add `--target badger2350` to the preview and build commands. See [personalization](Personalization.md) and the authoritative [form guide](../docs/PERSONALIZE.md).

**Browser designer:** use [Studio](Studio.md) for a local visual editor and firmware-accurate screen previews. **Studio does not yet export firmware or flash hardware**. If you want to install a badge now, use the documented build/USB workflows rather than expecting a Studio download button.

## 4. Build the public sample (developers)

```bash
scripts/fetch-deps.sh
scripts/run-host-tests.sh
scripts/build-firmware.sh
scripts/build-firmware.sh -DBHIHKH_TARGET=badger2350
```

Install platform prerequisites first; see [builds and artifacts](Builds-and-Artifacts.md). The host test and cross-build steps make **no physical hardware claims**.

## 5. Flash, configure, confirm

Flash only the UF2 for your exact board in BOOTSEL mode, then use USB serial to run `version`, `status`, `selftest` and diagnostics. Badger 2350 requires an explicit `--port` with the current [`badgerctl.py` helper](USB-Command-Line.md); its USB ID is not auto-detected. Save USB settings with `commit`; first record the build's identity. For new or changed firmware, work through the matching [hardware checklist](Testing-and-Validation.md).

## 6. If it fails

Do not repeatedly overwrite flash at random. Check [troubleshooting](Troubleshooting.md), [recovery](Installation-and-Recovery.md), and your stock full-flash backup.
