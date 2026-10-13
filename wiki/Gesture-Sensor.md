# Optional APDS-9960 gesture sensor

Gesture control is an optional accessory; **the badge works without it**. The documented sensor is an **APDS-9960** attached to the four-wire Qwiic / STEMMA QT I²C bus at address `0x39`. The Badger 2040's Qwiic I²C0 uses SDA GPIO4 and SCL GPIO5, 3V3 and GND.

## Enable and use

Long UP toggles gesture mode for the current session; `gesture.default_on` can change the next boot's default. With a sensor connected:

- Swipe **right/left** to step between screens.
- Swipe **up** to the contact card, and again to the full-screen contact QR.
- Swipe **down** to the photo badge.

Physical buttons remain available. The project index is modal and ignores swipes until dismissed. **Gesture mode automatically turns off after `gesture.timeout_s` of inactivity (default 300 seconds; 0 disables the timeout).** `gesture on|off` and `diag gesture` expose status and diagnostics over USB.

## Power and input constraints

The gesture detector and IR LED are off when the feature is disabled (its normal default). Polling occurs while active; the Qwiic connector has no interrupt wire. **A gesture cannot wake a powered-off badge.**

Firmware validates compatible chip IDs, decodes at most one directional swipe per observation session, rejects weak/ambiguous/long motions and applies a cooldown (`gesture.cooldown_ms`, default 700 ms). Mounting options include `gesture.rotation`, `gesture.mirror` and `gesture.sensitivity`.

If the sensor is absent, the badge still works and shows the appropriate faulted-gesture indicator only if gesture mode is enabled. Inspect `diag gesture` and the physical cable before adjusting sensitivity.

The original project's Qwiic rail behavior after power-off and optical aperture placement require physical checks. For electrical facts, timing details, mounting orientation and known limitations see [GESTURE.md](../docs/GESTURE.md) and [hardware tests](Testing-and-Validation.md).
