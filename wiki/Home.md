# BHIHKH! Wiki

**Badger? I hardly knew her!** (BHIHKH!, sometimes BadgHer) is a native C/C++ conference badge, digital business card and project portfolio for the **Pimoroni Badger 2040 and Badger 2350**. It runs on the Pico SDK, not MicroPython or Arduino.

This wiki is the **task-oriented starting point**. The detailed engineering contracts, measurements and historical test records remain in the repository's [docs](../docs/ARCHITECTURE.md). Where a wiki summary and an implementation reference disagree, investigate against the current source and hardware. Do not treat an old test record as certification of a new firmware build.

## Choose your path

| I want to… | Start here |
|---|---|
| Understand the two boards and what has actually been tested | [Hardware and support](Hardware-and-Support.md) |
| Preserve stock firmware, then install BHIHKH! | [Installation and recovery](Installation-and-Recovery.md) |
| Edit my own name, photo, QR, contacts and portfolio | [Personalization](Personalization.md) |
| Use the local browser designer | [Studio](Studio.md) |
| Learn the physical buttons and navigation | [Controls and screens](Controls-and-Screens.md) |
| Select LiPo versus 2×AAA on the original Badger 2040 | [Power and batteries](Power-and-Batteries.md) |
| Change settings and collect diagnostics over USB | [USB command line](USB-Command-Line.md) |
| Build and check firmware before flashing | [Builds and artifacts](Builds-and-Artifacts.md) |
| Understand the internals, flash layout or extensibility | [Architecture and formats](Architecture-and-Formats.md) |
| Run physical tests or contribute evidence | [Testing and validation](Testing-and-Validation.md) |
| Resolve a boot, serial, rendering or Studio problem | [Troubleshooting](Troubleshooting.md) |
| Keep personal inputs and backups private | [Privacy and backups](Privacy-and-Backups.md) |

## Product boundaries

- **No automatic sharing.** Personal information stays in local inputs; public example data uses a fictional person and example domains.
- **No automatic wireless integration on the documented `main` firmware.** The Badger 2350's wireless silicon, RTC, PSRAM and rear lights are not product features on this baseline. Unmerged RF Cryptid PR #13 experiments with Wi-Fi; update this statement if that functionality reaches `main`.
- **Studio is a designer, not a flasher.** Studio's browser can validate/edit TOML, compare portraits and render screens; exporting firmware and flashing a device are separate workflows.
- **Flash is not a scratchpad.** USB `set` stages changes; `commit` persists them. Navigation does not continuously write flash.
- **Hardware matters.** Both boards have different UF2 targets, panel controllers and power circuits. Select the correct target explicitly.

## What “tested” means

Automated host tests, renderer/QR checks and cross-builds are distinct from physical-device tests. [Recorded Badger 2350 core parity](../docs/test-records/2026-10-08-badger2350-parity-b3125aa-dc20680.md) passed its owner-reported scope on identified older builds, while a later binary still needs its own smoke record. The 2×AAA preset thresholds remain first estimates pending discharge characterization.

### Navigation

Use the [_Sidebar](_Sidebar.md), or see [FAQ](FAQ.md), [contributing](Contributing.md) and [source/documentation policy](Publishing-the-Wiki.md).
