# Testing, validation and evidence

**A green build is not a physical badge test.** BHIHKH! separates host checks, firmware cross-builds, rendered/decoded screen tests, artifact verification and real-device evidence.

## Validation levels

| Layer | What it can establish | What it cannot establish |
|---|---|---|
| Python and C++ host tests | Portable control logic, settings, validation, renderer behavior, migrations | Actual power, panel, ADC or USB bus behavior |
| JavaScript Studio tests | Local editor initialization, async response guarding, browser state transitions in the mocked DOM | All real browsers/WSL port-forwarding environments |
| C++ screen previews and QR decode | Pixel-level text fit and scannable QR data in generated bitmaps | Reflectance, ambient-light readability and phone scanning on real e-paper |
| Cross-build (2040/2350) | Correct SDK compilation, target-specific outputs and link viability | Boot, battery life or physical pin wiring |
| UF2/flash-artifact verifier | UF2 family, reserved flash boundaries, checksums and image layout | Whether the intended board actually boots and behaves correctly |
| Physical smoke test | Observed boot, controls, refresh, power, serial, reset and recovery on a **specified** firmware image | Other commits, other battery chemistries or untested peripherals |

## Run automated checks

```bash
scripts/run-host-tests.sh
scripts/build-firmware.sh
scripts/build-firmware.sh -DBHIHKH_TARGET=badger2350
node --test tests_js/test_studio_browser.cjs
python3 -m unittest tests_py.test_studio -v
```

The repository's CI host job runs the Studio browser test, while firmware jobs compile both boards. A failing test or warning promoted to an error is a defect to investigate, not a reason to skip the job.

## Choose a physical checklist

- [Badger 2040, USB-only](../docs/USB_HARDWARE_CHECKLIST.md): no battery or APDS hardware required; CLI, screens, settings, crashes and recovery.
- [Badger 2040, full hardware](../docs/HARDWARE_SMOKE_TEST.md): battery, navigation, gesture and additional fault paths.
- [Badger 2350](../docs/BADGER2350_SMOKE_TEST.md): full board-specific checklist, including low-power behavior, charger-related limits and optional APDS.
- [Test-record directory](../docs/test-records/README.md): dated, build-identified evidence, not blanket acceptance.

## What a record must contain

1. Board revision/model, power source, attached sensor and connected tools.
2. Firmware version and exact commit; ideally the tested UF2 SHA-256.
3. Which checklist and **individual steps** were performed, with pass/fail/skipped.
4. Measurements and observations, including any USB diagnostic captures needed for reproduction.
5. Provenance: who reported results, what was measured directly, and what is inference.
6. Explicit limitations and follow-up tests.

Do not copy an earlier record's ticks into a new test. Record private QR/contact data as **redacted**; don't paste personal identification, workspace tokens or raw private backups into the repository.

## Current evidence, correctly scoped

The [October 8 Badger 2350 record](../docs/test-records/2026-10-08-badger2350-parity-b3125aa-dc20680.md) reports a **core parity pass for its defined scope**, mostly on `b3125aa` with targeted `dc20680` retests. Optional gesture and power measurements were deferred. The Badger 2040 AAA preset curves are estimates until charge/discharge data validates them. Neither result guarantees the latest stacked PR firmware on physical hardware.
