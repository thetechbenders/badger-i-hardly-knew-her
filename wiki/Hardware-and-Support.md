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
| Physical evidence | Longstanding owner hardware use and checklists | Owner-reported **core parity pass** on documented older builds; later heads not implied |

The portable application logic is shared; the target-specific platform layer handles pins, panel packing, power and ADC circuitry. A build refuses conflicting `PICO_BOARD` or `PICO_PLATFORM` and never silently falls back to the other device.

## Hardware limitations

- **Badger 2040 has no battery charging circuit.** A protected single-cell LiPo must be charged appropriately off-board; verify connector polarity. A 2×AAA holder requires choosing the correct type: [power and batteries](Power-and-Batteries.md).
- **Badger 2350 uses LiPo only.** It has a charger, but BHIHKH! does not read the wireless-chip charge-status signal. `USB` is not a “charging” indicator.
- The APDS-9960 sensor is optional and uses I²C. No sensor means the physical buttons still work. See [gesture sensor](Gesture-Sensor.md).
- The Badger 2350's Wi-Fi/Bluetooth, RTC, rear lights and PSRAM are not implemented for this product.
- Device measurements such as battery calibration, sleep current and ghosting depend on the hardware and firmware version. Estimates and older records are not universal specifications.

## Validation evidence

The [Badger 2350 checklist](../docs/BADGER2350_SMOKE_TEST.md) states **core parity passed for the recorded scope**, and links to the owner-reported [2026-10-08 record](../docs/test-records/2026-10-08-badger2350-parity-b3125aa-dc20680.md). Most checks used `b3125aa`, with only A2/B5/E2 rerun on `dc20680`. Optional APDS and sleep-current characterization remained outside that scope. Some older narrative docs still say physical validation is pending; read the dated record and checklist together rather than asserting that every current build was tested.

Reference: [architecture and pin/circuit facts](../docs/ARCHITECTURE.md), [battery measurement](../docs/BATTERY.md), [reusable checklists](Testing-and-Validation.md).
