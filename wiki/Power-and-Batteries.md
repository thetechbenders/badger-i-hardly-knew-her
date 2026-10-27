# Power, batteries and meter

These two badges use **different power electronics**. Do not transfer battery assumptions, connector polarity or firmware artifacts from one to the other.

## Badger 2040

The original Badger 2040 has a JST battery connection, but **no charger**. Use only an appropriately protected single-cell LiPo with correct connector polarity and appropriate off-board charging, or a compatible 2×AAA holder.

On battery, a front button turns the board on; GPIO10 latches power. After the configured idle period (default `sleep.timeout_s=120`), the firmware finishes drawing its sleep image and releases the latch. The next wake is a cold boot. USB power instead emulates this off state; it cannot remove bus power itself.

### Battery type and meter

| Selected pack | Four bars at (mV) | LOW below | Valid from |
|---|---|---|---|
| Single-cell LiPo | configurable, defaults **3600 / 3700 / 3800 / 3950** | 3500 | 2500 |
| 2×AAA alkaline | **2300 / 2500 / 2700 / 2900** | 2200 | 1800 |
| 2×AAA NiMH | **2300 / 2400 / 2500 / 2650** | 2200 | 1800 |

These are firmware thresholds, **not a calibrated percentage of remaining energy**. The AAA presets are first estimates. NiMH's flat discharge curve makes its bar readout especially coarse.

**How to select:** On the Badger 2040 Info screen (short USR), use short UP/DOWN to cycle through LiPo → AAA alkaline → AAA NiMH. Or issue the appropriate `set battery.pack` and `set battery.aaa_cells` over USB, then `commit`.

`battery.auto` (default on) can switch **from an AAA selection to LiPo only** after three stable readings at or above 3.60 V (within 50 mV). A low LiPo is electrically ambiguous with AAA; the firmware never automatically reclassifies it as AAA and never infers alkaline versus NiMH. When fitting AAA after LiPo, choose the type manually once. A pack-change save updates battery fields independently of other unsaved USB edits. Automatic pack switching is disabled in safe mode.

## Badger 2350

This model has an onboard LiPo charger and only accepts the **LiPo** pack choice. It has a different ADC sense divider. The firmware may display `USB` when VBUS is present, but cannot report actual charge status from the unused wireless chip. No AAA mode is supported.

## Measuring rather than guessing

Compare the Info-screen battery value with a multimeter **on battery, USB disconnected**. `diag battery` over USB cannot prove the actual cell voltage. Battery sampling occurs at boot, around planned refreshes and periodically while awake; it does not force a refresh or keep the badge powered indefinitely. Calibration uses `battery.cal_permille` (900–1100); read the complete method before adjusting thresholds.

[Detailed circuitry, calculations, settings and unresolved validation](../docs/BATTERY.md). [USB CLI battery keys](../docs/USB_CLI.md#keys). [Physical checklists](Testing-and-Validation.md).
