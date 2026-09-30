# Battery meter (single-cell LiPo)

## Circuit and what was verified

| Signal | GPIO / ADC | Source |
|---|---|---|
| Battery sense, divider gain 1/3 | GPIO29 / ADC3 | Pico SDK `pimoroni_badger2040.h` (`BADGER2040_BAT_SENSE_PIN`); Pimoroni battery example and launcher (`* 3`, "a gain, not rounding of 3.3V") |
| 1.24 V reference output | GPIO28 / ADC2 | SDK header (`BADGER2040_1V2_REF_PIN`); Pimoroni code (`vdd = 1.24 * 65535 / ref`) |
| Reference enable | GPIO27, output, high while measuring | SDK header (`BADGER2040_VREF_POWER_PIN`); Pimoroni code |
| VBUS detect | GPIO24 | SDK header, Pimoroni library |

- Pimoroni's upstream code (pimoroni-pico commits `c9bf201` and `64cbee7`)
  maps 3.2–4.0 V to its bars. That is the stock range for the AAA pack. This
  firmware replaces it with LiPo thresholds.
- **Not verified here:** the schematic PDF is hosted on Pimoroni's CDN, which
  the build sandbox could not reach. Two things still need confirming:
  - whether the sense point is the battery connector itself or after the
    board's power-path/protection element (a diode drop would read low);
  - the reference's tolerance.

  Both are handled by measurement and `battery.cal_permille`, below.
- **No charger:** the original Badger 2040 has none. The firmware never shows
  "charging". On USB it shows **USB**, because on USB the sense point is
  driven from VBUS and says nothing about the cell.

## Measurement (native Pico SDK ADC)

`board::read_battery_raw()`:

1. Drive GPIO27 high and wait 1 ms for the reference to settle.
2. `adc_select_input(2)`, discard one conversion, and average 32 readings.
3. Do the same on input 3.
4. Drive GPIO27 low.

Conversion (integer):

```
vdd_mV  = 1240 * 4095 / ref_counts                      (true ADC supply; the 3V3 rail sags on a low cell)
vbat_mV = 3 * 1240 * bat_counts / ref_counts * cal_permille / 1000
```

- **Invalid** (shown as `?`): the reference reads 0 or full scale; the
  derived supply is outside 1.8–3.7 V; or the battery reads outside
  2.5–4.6 V while not on USB.
- **Filtering:** an exponential moving average (α = 1/4), seeded by the first
  valid reading and reset after USB power or an invalid reading.
- **Hysteresis:** `battery.hyst_mv` (default 40 mV). A bar is gained only
  when the filtered voltage exceeds the next threshold by the hysteresis, and
  lost only when it drops the hysteresis below the current level's threshold.
  The same rule applies to LOW.

## Thresholds (defaults, all configurable)

| State | Filtered voltage | Rough meaning (resting LiPo, approximate) |
|---|---|---|
| 4 bars | ≥ 3950 mV (`battery.bar4_mv`) | roughly 70–100 % |
| 3 bars | ≥ 3800 mV (`battery.bar3_mv`) | roughly 45–70 % |
| 2 bars | ≥ 3700 mV (`battery.bar2_mv`) | roughly 25–45 % |
| 1 bar | ≥ 3600 mV (`battery.bar1_mv`) | roughly 10–25 % |
| 0 bars | below bar 1 | nearly empty |
| LOW | < 3500 mV (`battery.low_mv`, 0 = off) | recharge now |

- The percentages are indicative only. A LiPo's voltage-to-charge curve
  depends on the cell, its age, temperature and load, so the icon is a
  coarse indicator.
- Thresholds must be strictly increasing, with LOW ≤ bar 1. `commit` refuses
  anything else.

## When it samples (it never keeps the badge awake)

- **At boot**, before the first frame.
- **Right before a planned refresh:** a screen change, a CLI screen or
  content change, or the pre-power-off frame. The powered-off e-paper
  therefore shows the last measurement.
- **Every 60 s while awake**, without a refresh of its own. The icon updates
  with the next planned refresh.
- **Not while the panel is refreshing:** the load of a refresh would depress
  the reading. The sample is skipped and the previous value kept.

Sampling does not reset the idle timer and never delays power-off.

## Validating against a multimeter

1. Run on battery with USB disconnected. A USB connection powers the sense
   point from VBUS, so `diag battery` over USB cannot validate the cell.
   Open the Info screen (C long press). It shows the reading, the filtered
   value and the bars, and redraws every 15 s. Measure the cell at the JST
   connector with the multimeter at the same moment. (Set
   `sleep.timeout_s 600` first so the badge stays on long enough.)
2. Record the multimeter value against the badge's reading at several
   charge levels (e.g. 4.15 V, 3.9 V, 3.7 V, 3.5 V).
3. If there is a consistent ratio error, set
   `battery.cal_permille = 1000 * multimeter / badge` (range 900–1100), then
   `commit`.
4. If there is a constant **offset** instead (e.g. a diode drop in the sense
   path), lower the thresholds by that offset rather than using calibration,
   and note it here.
5. `diag battery` also prints the raw ADC counts and the derived ADC supply,
   for checking the reference.
