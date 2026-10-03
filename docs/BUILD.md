# Building

## Pinned versions

All versions are recorded in `deps.lock`. `scripts/fetch-deps.sh` checks out
each dependency at its pinned tag and **fails if the commit differs**.

| Component | Version | Commit |
|---|---|---|
| Raspberry Pi Pico SDK | 2.3.1 | `079c6f39023649b154152db30f1d781e884879bc` |
| TinyUSB (SDK submodule) | as pinned by the SDK | `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` |
| pimoroni-pico (UC8151 driver only) | v1.29.0-2 | `39b017a30717ed68e5ff15163bac89489acd09df` |
| picotool (UF2 generation) | 2.3.1 | `2041936441b48a3cc53ae3da9e805229fe8f4e18` |
| Arm GNU toolchain | 13.2.Rel1 (Ubuntu 24.04 `gcc-arm-none-eabi 15:13.2.rel1-2`) | – |
| Pillow (font rasterisation) | 12.3.0 | – |

pico-sdk 2.3.1 is the first tagged SDK release with the official
`pimoroni_badger2350` board header (upstream commit `8b0d07ed`); 2.2.0 and
2.3.0 do not have it. That is the only reason for the update from 2.2.0. Its
TinyUSB submodule is the same commit as 2.2.0's, and the SDK 2.3 build
requires picotool 2.3.x, so picotool moved to 2.3.1, released alongside
SDK 2.3.1. Nothing else changed (toolchain, pimoroni-pico, Pillow).
pimoroni-pico v1.29.0-2 is built against 2.2.0 by Pimoroni's own CI; only its
UC8151 driver is used here (Badger 2040 only), and it builds against 2.3.1.
Board definitions: the SDK's `pimoroni_badger2040` (RP2040, 2 MiB W25Q16
flash) and `pimoroni_badger2350` (RP2350A, 16 MiB flash).

## Linux (Ubuntu 24.04) or WSL2

```bash
sudo apt install cmake ninja-build python3 python3-pip git \
    gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
    build-essential libzbar0
python3 -m pip install --user -r tools/requirements.txt   # or use a venv

scripts/fetch-deps.sh        # ~150 MB into deps/ (git-ignored); also builds picotool
scripts/run-host-tests.sh    # host tests + previews in build/previews/ (Badger 2350: build/previews/badger2350/)
scripts/build-firmware.sh    # Badger 2040 firmware in build/fw/
scripts/build-firmware.sh -DBHIHKH_TARGET=badger2350   # Badger 2350 firmware in build/fw-badger2350/
```

To build picotool with USB support (for `picotool save`, `info` and
`reboot` on a live device), install `libusb-1.0-0-dev` and `pkg-config`
before running `fetch-deps.sh`.

### Windows notes

- Build inside **WSL2** (Ubuntu 24.04) with the commands above. Keep the
  clone on the Linux filesystem (`~/src/...`), not `/mnt/c`; it is much faster.
- A personalised build from a form works the same way:
  `scripts/build-badge.sh local/badge.toml` (see PERSONALIZE.md, Windows).
- Copy the UF2 to Windows through `\\wsl$\Ubuntu-24.04\home\<you>\...`, or with
  `cp build/fw/badger_badge.uf2 /mnt/c/Users/<you>/Downloads/`, then drag it
  onto the `RPI-RP2` drive in Explorer. Flashing needs no USB passthrough.
- For the serial CLI from Windows, either run `tools/badgerctl.py` with
  Windows Python (`pip install pyserial`, port `COMx`), or attach the badge to
  WSL with [usbipd-win](https://github.com/dorssel/usbipd-win)
  (`usbipd attach --wsl --busid <id>`); it then appears as `/dev/ttyACM0`.
- For `picotool save` backups on Windows, use the prebuilt picotool from the
  official `raspberrypi/pico-sdk-tools` releases, or usbipd + WSL.

## Hardware target

| CMake option | Default |
|---|---|
| `-DBHIHKH_TARGET=<name>` | `badger2040`, the original Badger 2040; or `badger2350`, the Badger 2350 (working name BadgHer™ NEO) |

| Target | Build directory (`build-firmware.sh`) | Artifacts | UF2 family |
|---|---|---|---|
| `badger2040` | `build/fw/` | `badger_badge.uf2`, `badger_badge-assets.uf2` (+ `.elf`, `.bin`, `.map`) | RP2040 |
| `badger2350` | `build/fw-badger2350/` | `badger2350_badge.uf2`, `badger2350_badge-assets.uf2` (+ `.elf`, `.bin`, `.map`) | RP2350 Arm Secure, after picotool's RP2350-E10 ignore block |

An unknown name stops the configure step with an error, and so does a
`PICO_BOARD`/`PICO_PLATFORM` that contradicts the target or a build
directory configured for the other board; nothing falls back to the other
badge. Verify a build with `scripts/verify_artifacts.py <dir> --target <name>`.
The Badger 2350 build is CI-tested but not yet validated on hardware (see
[BADGER2350_SMOKE_TEST.md](BADGER2350_SMOKE_TEST.md)). See
[ARCHITECTURE.md](ARCHITECTURE.md#hardware-targets).

## Content selection at build time

| CMake option | Default |
|---|---|
| `-DBADGER_PROFILE=<json>` | `local/profile.json` if present, else `config/sample-profile.json` (a `.toml` form needs `-DBADGER_PORTRAIT` too: configuring fails without it) |
| `-DBADGER_PORTRAIT=<png>` | `local/portrait.png` if present, else `assets/sample/portrait_placeholder.png` (Badger 2350: `local/badger2350/portrait.png`, else `assets/sample/portrait_placeholder_badger2350.png`, 104×176); `none` = no built-in portrait |

CMake prints which files were used. For a filled-in form, use
`scripts/build-badge.sh local/badge.toml` instead
([PERSONALIZE.md](PERSONALIZE.md)): it converts the photo, runs the fit and
QR gates on the previews, then calls `build-firmware.sh` with the generated
profile and portrait (build directory `local/out/<form name>/fw/`). The profile only supplies **factory
defaults**. Values committed over USB take precedence, and `defaults` + `commit`
returns to these defaults.

## Outputs (`build/fw/`)

| File | Purpose |
|---|---|
| `badger_badge.uf2` | Firmware for drag-and-drop flashing |
| `badger_badge-assets.uf2` | Asset pack (portrait) for the asset region at 0x101E0000 only |
| `badger_badge.elf`, `.elf.map`, `.bin`, `.dis`, `.hex` | Debugging, symbol and size analysis |
| `memory-report.txt` | Flash/RAM/stack/heap report and largest symbols |
| `SHA256SUMS` | Checksums of the artifacts above |

The post-build step **fails** if the image would overlap the asset region, or
if less than 32 KiB of RAM would remain for the heap.

`scripts/verify_artifacts.py build/fw [--require-clean]` checks a build before
flashing, offline:
- UF2 blocks carry the RP2040 family ID; the firmware UF2 equals the `.bin`
  and ends before the asset region; the asset UF2 lies inside
  0x1E0000–0x1EFFFF; nothing touches the guard gap or the settings sectors.
- Both stacks are where the SDK puts them, and the crash record is in NOLOAD
  `.uninitialized_data`.
- `git describe` is embedded, there are no absolute paths, and the memory
  report and `SHA256SUMS` are consistent.

To hand a build over, package it with
`scripts/package_firmware.py <build-dir> <name>.zip`. It first checks every
file in the build's `SHA256SUMS` exists and matches. It then writes a ZIP
of those artifacts with a `SHA256SUMS` generated for exactly the packaged
files (`--only FILE …` packages a subset with its own manifest) and
re-verifies the result. The ZIP is deterministic (sorted entries, fixed
times and modes). `scripts/package_firmware.py --verify <zip>` checks a
package both ways: every listed file is present and matches, and every
packaged file is listed. Duplicate ZIP members (including manifests and directory
entries) are refused before reading content. Creation and verification use the
same portable namespace: one flat directory, ASCII letters/digits/underscores/
hyphens/dots, no leading or trailing dot, Windows device names, or names differing
only by case. Paths, backslashes, drive prefixes and whitespace are refused.

Builds are **reproducible across checkout locations and build-directory
names** (`-ffile-prefix-map` for the source tree, the dependencies and the
build tree, plus a fixed `--abbrev=12` in the version string). Debug
information is kept in full. The compile directory reads as `.`, sources as
`./firmware/...` and `./deps/...`, and generated sources as
`./build/fw/generated/...`. From the repository root, `arm-none-eabi-gdb` and
`addr2line` therefore resolve every source file, and a default build's
generated files too (checked: `repo_label`, `stdio_init_all` and the three
generated `.cpp` files map to existing paths). A build in another directory
keeps its generated files elsewhere; point the debugger there with
`set substitute-path ./build/fw <dir>`. CI rebuilds from a
second clone into a differently named build directory and compares
UF2/BIN/ELF/asset UF2 byte for byte. The `.elf.map` lists absolute object
paths and is not expected to match; it is a size-analysis aid, not a
release artifact.

Current sample build: 192.6 KiB flash (of 1920 KiB available before the
asset region), 86.7 KiB static RAM of 264 KiB, 4 KiB stack per core. Each
copy of the settings (staged, committed, store work buffers) holds the
12-entry portfolio, which accounts for most of the RAM growth.

## CI

`.github/workflows/ci.yml` runs on every push and PR:

1. **host**: the C++ unit tests (ASan/UBSan), Python tool tests, zbar QR
   decoding of the rendered screens, a font reproducibility check and the
   previews (uploaded as an artifact).
2. **firmware**: a matrix over both targets, `badger2040` (RP2040) and
   `badger2350` (RP2350), each a real cross-build with the pinned toolchain
   and dependencies and the public sample content; any compiler warning
   fails the job. Each runs `verify_artifacts.py --require-clean --target
   <target>`, checks that a copy of its artifacts without the CMake cache is
   refused as the other board's (wrong UF2 family), packages and verifies
   the release ZIP, runs the second-checkout reproducibility check (the
   badger2040 one is configured without the option, so it also checks the
   default target), and uploads the UF2s, ELF, map, bin, memory report and
   SHA256SUMS.

## Regenerating assets

```bash
python3 tools/fontgen.py --fetch            # fetches DejaVu 2.37 (SHA-256 pinned), writes firmware/generated/fonts.cpp
python3 tools/fontgen.py --check            # CI: fails if the committed fonts are stale
python3 tools/iconsgen.py --out firmware/generated/icons.cpp [--check]   # Simple Icons SVGs -> 12 px bitmaps
python3 tools/portrait.py local/private/<photo> --settings local/private/portrait.json --out local/portrait.png --compare local/previews/portrait
python3 tools/assetpack.py build --portrait local/portrait.png --out local/build/assets.bin --uf2 local/build/assets.uf2
python3 tools/render_previews.py --profile local/profile.json --pack local/build/assets.bin --out local/previews/screens
scripts/build-badge.sh --preview local/badge.toml      # the same steps for a fill-in form, in one command
```
