# Hardware and support

| | Original Badger 2040 | Badger 2350 |
|---|---|---|
| Microcontroller | RP2040 | RP2350A |
| Monochrome e-paper | UC8151, 296×128 | SSD1680, 264×176 |
| Flash | 2 MiB | 16 MiB |
| Firmware target | `badger2040` (default) | `badger2350` |
| Firmware image | `badger_badge.uf2` | `badger2350_badge.uf2` |
| BOOTSEL volume | `RPI-RP2` | `RP2350` |
| User diagnostics key | **USR** | **HOME** |
| Battery behavior | Single-cell LiPo with no onboard charger, or 2×AAA alkaline/NiMH | Single-cell LiPo with onboard charger |
| Battery sleep | MCU loses power when firmware releases board latch | RP2350 low-power state |
| Display update | Full or permitted partial refresh | Full refresh only |
| Physical evidence | Owner-reported **USB-only** test on `023b3c6` (2026-10-01); no battery or APDS fitted; no recorded physical validation of PR #12 AAA behavior | Owner-reported **core parity pass** on documented older builds; later heads not implied |

The portable application logic is shared; the target-specific platform layer handles pins, panel packing, power and ADC circuitry. A build refuses conflicting `PICO_BOARD` or `PICO_PLATFORM` and never silently falls back to the other device.

## Hardware limitations

- **Badger 2040 has no battery charging circuit.** A protected single-cell LiPo must be charged appropriately off-board; verify connector polarity. A 2×AAA holder requires choosing the correct type: [power and batteries](Power-and-Batteries.md).
- **Badger 2350 uses LiPo only.** It has a charger, but BHIHKH! does not read the wireless-chip charge-status signal. `USB` is not a “charging” indicator.
- The APDS-9960 sensor is optional and uses I²C. No sensor means the physical buttons still work. See [gesture sensor](Gesture-Sensor.md).
- On the `main` firmware as documented here (before RF Cryptid PR #13), Badger 2350 Wi-Fi/Bluetooth, RTC, rear lights and PSRAM are not implemented as product features. The separate unmerged RF Cryptid experiment changes the Wi-Fi part of that statement if merged.
- Device measurements such as battery calibration, sleep current and ghosting depend on the hardware and firmware version. Estimates and older records are not universal specifications.

## Validation evidence

The [Badger 2040 October 1 USB-only record](../docs/test-records/2026-10-01-usb-023b3c6.md) is owner-reported on personalized firmware `023b3c6` and USB power only. Battery, power-off, APDS gestures, and the later PR #12 battery-pack changes were **not physically recorded** by that test.

The [Badger 2350 checklist](../docs/BADGER2350_SMOKE_TEST.md) states **core parity passed for the recorded scope**, and links to the owner-reported [2026-10-08 record](../docs/test-records/2026-10-08-badger2350-parity-b3125aa-dc20680.md). Most checks used `b3125aa`, with only A2/B5/E2 rerun on `dc20680`. Optional APDS and sleep-current characterization remained outside that scope. Some older narrative docs still say physical validation is pending; read the dated record and checklist together rather than asserting that every current build was tested.

Reference: [architecture and pin/circuit facts](../docs/ARCHITECTURE.md), [battery measurement](../docs/BATTERY.md), [reusable checklists](Testing-and-Validation.md).
