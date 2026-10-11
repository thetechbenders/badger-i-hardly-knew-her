# Builds, dependencies and artifacts

The project uses native C/C++, **Raspberry Pi Pico SDK 2.3.1**, `pimoroni-pico` v1.29.0-2 for the Badger 2040 UC8151 driver, and pinned picotool 2.3.1. Exact revisions live in [deps.lock](../deps.lock); the fetch script checks pinned commits.

## Host prerequisites

Linux/WSL (Ubuntu package names from the repository's build guide):

```bash
sudo apt install cmake ninja-build python3 python3-pip git \
    gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
    build-essential libzbar0
python3 -m pip install --user -r tools/requirements.txt
```

A virtual environment is an alternative to `pip --user`. macOS requires a suitable Arm GNU toolchain and Homebrew prerequisites; see [BUILD.md](../docs/BUILD.md) for toolchain PATH, zbar, libusb and documented platform warnings.

## Run the gates

```bash
scripts/fetch-deps.sh
scripts/run-host-tests.sh
scripts/build-firmware.sh
scripts/build-firmware.sh -DBHIHKH_TARGET=badger2350
```

`scripts/run-host-tests.sh` exercises portable logic, rendering and QR checks for both target dimensions. Cross-builds use target-specific SDK board definitions. For private content, prefer [form build](Personalization.md) rather than editing generated C++.

## Find the outputs

| Target | Output directory | Primary UF2 |
|---|---|---|
| 2040 | `build/fw/` | `badger_badge.uf2` |
| 2350 | `build/fw-badger2350/` | `badger2350_badge.uf2` |
| 2040 filled-in TOML form | `local/out/<form-name>/fw/` | `badger_badge.uf2` |
| 2350 filled-in TOML form | `local/out/<form-name>-badger2350/fw/` (default form output) | `badger2350_badge.uf2` |

Build outputs also include a separately flashable `*-assets.uf2`, ELF and map files, `memory-report.txt` and `SHA256SUMS`. The post-build check rejects overlap into reserved flash and insufficient heap headroom.

## Verify and package

```bash
python3 scripts/verify_artifacts.py build/fw --target badger2040
python3 scripts/verify_artifacts.py build/fw-badger2350 --target badger2350
python3 scripts/package_firmware.py build/fw badger2040-package.zip
python3 scripts/package_firmware.py --verify badger2040-package.zip
```

Do not distribute a **private** personalized UF2 by accident: profile fields and portrait data may be built into the binary. CI builds public sample content; a successful CI result does not verify a personalized build. Re-check the specific local artifact and hashes.

Full flags, output guarantees and macOS notes: [BUILD.md](../docs/BUILD.md), [FORMATS.md](../docs/FORMATS.md), [test records](Testing-and-Validation.md).
