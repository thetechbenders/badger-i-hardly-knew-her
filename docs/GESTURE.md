# APDS-9960 gesture mode

## Hardware and connection

- **Sensor:** any APDS-9960 breakout with a Qwiic / STEMMA QT connector, on
  the Badger 2040's Qwiic port. That port is **I2C0: SDA GPIO4, SCL GPIO5**
  (Pico SDK `boards/pimoroni_badger2040.h`), 3V3 and GND. The firmware uses
  I2C at 400 kHz, address 0x39, with weak internal pull-ups; breakouts
  normally add their own.
- **No interrupt line:** the four-wire Qwiic cable carries no interrupt, so
  the gesture FIFO is **polled** every 10 ms, and only while gesture mode is
  on. (The SDK board header lists an "INT" pin, GPIO3, but it is not part of
  the Qwiic connector and is not used.)
- **ID check:** the chip ID must read 0xAB (APDS-9960), or 0xA8 / 0x9C for
  known compatible parts. Otherwise nothing is written to the device.

## Power behaviour

- **Sensor off:** when gesture mode is off (the default after boot), the
  sensor is kept at `ENABLE = 0x00` (PON = 0). The gesture engine,
  proximity engine and IR LED driver are all off. The firmware forces this
  state at every boot, because a soft reset of the RP2040 on USB power does
  not power-cycle the sensor.
- **Sensor on:** in gesture mode the firmware sets
  `ENABLE = PON | WEN | PEN | GEN`. The IR LED then pulses (100 mA drive,
  300 % boost, 10 × 32 µs pulses per cycle). This is the mode that costs
  battery; measure it (docs/HARDWARE_SMOKE_TEST.md, section I).
- **Automatic off:** gesture mode switches itself off after
  `gesture.timeout_s` without user activity (default 300 s), before power-off
  or emulated sleep, and in safe mode.
- **No gesture wake:** gestures cannot wake a powered-off badge.
  - With the power latch released there is no running MCU to poll the
    sensor.
  - The only wake path on the original Badger 2040 is the front buttons
    enabling the regulator through diodes.
  - There is no interrupt line on Qwiic, and no RTC.
- **Unverified: is Qwiic 3V3 switched?** It is expected to be the switched
  3V3 rail, i.e. unpowered when the badge is off. Confirm with a multimeter
  (section I of the smoke test), since the schematic was not available to
  this project's tooling. On USB the rail stays up, which is why the firmware
  explicitly powers the sensor down.

## Controls

| Input | Action |
|---|---|
| **UP long press** | Gesture mode on/off (session only; set `gesture.default_on` to start with it on) |
| Swipe right / left | Next / previous screen: badge → card → projects → badge |
| Swipe up | Business card; swipe up again on the card: full-screen QR |
| Swipe down | Photo badge |

- All physical buttons keep working unchanged in gesture mode.
- A swipe that would not change the screen does nothing (no refresh).
- **Status indicator** (top right, next to the battery):
  - `↔` gesture mode on;
  - `↔` struck through: gesture mode on, but the sensor is missing or
    faulty;
  - nothing: gesture mode off.
- CLI: `gesture on|off`, and `diag gesture` (state, chip ID, counts, the
  last raw decode with its deltas, I2C errors and bus recoveries).

## One swipe = one screen change

1. **Session decoding:** each entry and exit of the gesture engine produces
   at most one direction. The first and last frames with all four
   photodiodes above the noise floor are compared.
   - Sessions with fewer than 4 usable frames are rejected.
   - A change smaller than `gesture.sensitivity` is rejected.
   - A diagonal (neither axis 1.5× the other) is rejected.
   - A session longer than 1.5 s (a hovering hand) is rejected.
2. **Cooldown:** after an accepted swipe, further swipes are ignored for
   `gesture.cooldown_ms` (default 700 ms). This swallows the return stroke
   of the hand.
3. **Refresh coalescing:** the renderer waits until the current e-paper
   refresh finishes. However many screen requests arrive during a refresh,
   only the newest view is drawn next. A host test drives three swipes
   during one 4.5 s refresh and checks for exactly one further refresh.

## Mounting orientation

Directions are first decoded in the sensor's own frame (the SparkFun
APDS-9960 library's convention), then mapped:

- `gesture.rotation` 0–3: sensor rotated 0/90/180/270° clockwise relative to
  the badge.
- `gesture.mirror`: swap left/right after rotation (e.g. the sensor faces
  the other way).

Calibrate on the real mount:

1. `gesture on`
2. Swipe physically left-to-right across the badge.
3. Check `diag gesture`: `last raw ... -> <result>`.
4. Adjust `gesture.rotation` / `gesture.mirror` until right, left, up and
   down all map correctly.
5. `commit`

## Optical aperture

The sensor needs an **open** aperture. Any cover, including clear plastic,
reflects the sensor's own IR back into its photodiodes (crosstalk).
`hardware/gesture_sensor/apds9960_aperture.scad` is a parametric window and
PCB pocket:

- an open, outward-flared aperture sized from the 3.94 × 2.36 mm package;
- the package face 0.2 mm behind a thin wall;
- matte, opaque, dark material.

Measure your breakout (PCB size, sensor offset) and wall thickness before
printing. The model has been compiled to STL with OpenSCAD but **not printed
or tested**. Enclosure effects must be measured (smoke test, section I).

## Faults and missing sensor

- Every I2C transaction has a bounded timeout (~1 ms plus 0.1 ms per byte).
  No sensor or bus state can stall the badge.
- **Missing at boot:** gestures are unavailable. Probing is retried with
  exponential backoff (1 s → 30 s), only while gesture mode is requested.
  Hot-plugging is picked up.
- **Three consecutive errors:** the sensor enters Fault. The firmware makes a
  best-effort power-down, clears the bus (9 SCL pulses + STOP, open-drain),
  and re-probes with backoff. Buttons, display, USB and power management are
  unaffected.
