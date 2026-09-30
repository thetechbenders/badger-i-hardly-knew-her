# Building

## Pinned versions

All versions are recorded in `deps.lock`. `scripts/fetch-deps.sh` checks out
each dependency at its pinned tag and **fails if the commit differs**.

| Component | Version | Commit |
|---|---|---|
| Raspberry Pi Pico SDK | 2.2.0 | `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` |
| TinyUSB (SDK submodule) | as pinned by the SDK | `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` |
| pimoroni-pico (UC8151 driver only) | v1.29.0-2 | `39b017a30717ed68e5ff15163bac89489acd09df` |
| picotool (UF2 generation) | 2.2.0 | `a7eb3988f0645239185fadb4e25d8279478c2dbb` |
| Arm GNU toolchain | 13.2.Rel1 (Ubuntu 24.04 `gcc-arm-none-eabi 15:13.2.rel1-2`) | – |
| Pillow (font rasterisation) | 12.3.0 | – |

Pimoroni's own CI builds pimoroni-pico v1.29.0-2 against pico-sdk 2.2.0, so
the pair is known to be compatible. The board definition used is the SDK's
`pimoroni_badger2040` (RP2040, 2 MiB W25Q16 flash).

## Linux (Ubuntu 24.04) or WSL2

```bash
sudo apt install cmake ninja-build python3 python3-pip git \
    gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
    build-essential libzbar0
python3 -m pip install --user -r tools/requirements.txt   # or use a venv

scripts/fetch-deps.sh        # ~150 MB into deps/ (git-ignored); also builds picotool
scripts/run-host-tests.sh    # host tests + previews in build/previews/
scripts/build-firmware.sh    # firmware in build/fw/
```

To build picotool with USB support (for `picotool save`, `info` and
`reboot` on a live device), install `libusb-1.0-0-dev` and `pkg-config`
before running `fetch-deps.sh`.

### Windows notes

- Build inside **WSL2** (Ubuntu 24.04) with the commands above. Keep the
  clone on the Linux filesystem (`~/src/...`), not `/mnt/c`; it is much faster.
- Copy the UF2 to Windows through `\\wsl$\Ubuntu-24.04\home\<you>\...`, or with
  `cp build/fw/badger_badge.uf2 /mnt/c/Users/<you>/Downloads/`, then drag it
  onto the `RPI-RP2` drive in Explorer. Flashing needs no USB passthrough.
- For the serial CLI from Windows, either run `tools/badgerctl.py` with
  Windows Python (`pip install pyserial`, port `COMx`), or attach the badge to
  WSL with [usbipd-win](https://github.com/dorssel/usbipd-win)
  (`usbipd attach --wsl --busid <id>`); it then appears as `/dev/ttyACM0`.
- For `picotool save` backups on Windows, use the prebuilt picotool from the
  official `raspberrypi/pico-sdk-tools` releases, or usbipd + WSL.

## Content selection at build time

| CMake option | Default |
|---|---|
| `-DBADGER_PROFILE=<json>` | `local/profile.json` if present, else `config/sample-profile.json` |
| `-DBADGER_PORTRAIT=<png>` | `local/portrait.png` if present, else `assets/sample/portrait_placeholder.png`; `none` = no built-in portrait |

CMake prints which files were used. The profile only supplies **factory
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

Builds are **reproducible across checkout locations** (`-ffile-prefix-map`,
fixed `--abbrev=12` in the version string). CI rebuilds from a second clone
and compares UF2/BIN/ELF byte for byte. The `.elf.map` lists absolute object
paths and is not expected to match.

Current sample build: 177.6 KiB flash (of 1920 KiB available before the
asset region), 59.7 KiB static RAM of 264 KiB, 4 KiB stack per core.

## CI

`.github/workflows/ci.yml` runs on every push and PR:

1. **host**: the C++ unit tests (ASan/UBSan), Python tool tests, zbar QR
   decoding of the rendered screens, a font reproducibility check and the
   previews (uploaded as an artifact).
2. **firmware**: a real RP2040 cross-build with the pinned toolchain and
   dependencies, using the public sample content. It then runs
   `verify_artifacts.py --require-clean`, runs the second-checkout
   reproducibility check, and uploads the UF2, ELF, map, bin, memory report
   and SHA256SUMS.

## Regenerating assets

```bash
python3 tools/fontgen.py --fetch            # fetches DejaVu 2.37 (SHA-256 pinned), writes firmware/generated/fonts.cpp
python3 tools/fontgen.py --check            # CI: fails if the committed fonts are stale
python3 tools/portrait.py <photo> --settings local/private/portrait.json --out local/portrait.png --compare local/previews/portrait
python3 tools/assetpack.py build --portrait local/portrait.png --out local/build/assets.bin --uf2 local/build/assets.uf2
python3 tools/render_previews.py --profile local/profile.json --pack local/build/assets.bin --out local/previews/screens
```
