# Frequently asked questions

### Will this run on both Badger 2040 and Badger 2350?

Yes, with **separate target builds**. The Badger 2040 defaults to RP2040 / 296×128; the 2350 is RP2350A / 264×176. UF2 images are not interchangeable. See [hardware](Hardware-and-Support.md).

### Does Studio build or flash my badge?

**No.** Studio is a local visual editor and screen preview. Firmware export, USB flashing and file browsing are not implemented in the currently documented Studio phase. Use [personalization](Personalization.md) to produce UF2s.

### If I refresh Studio, why doesn't the Portrait Workshop show my original photo?

The uploaded source photo isn't retained. **Use selected portrait** writes a managed processed portrait to the workspace and updates the in-memory TOML; **Validate & save** persists the TOML reference. The workshop isn't a persistent photo editor. Check the saved `[portrait].processed` field rather than expecting an upload to reappear.

### Why does changing from Badger 2040 to 2350 reset the portrait?

The current Studio target-switch flow selects that target's sample portrait. It does not yet maintain independent remembered custom portraits. See [Studio](Studio.md).

### Do I need a battery?

No. The firmware runs over USB. Battery operation, idle timeout, wake and status behavior differ by badge model. See [power](Power-and-Batteries.md).

### Will a fresh pair of AAAs be identified automatically?

Not as AAA. The Badger 2040 deliberately refuses to infer 2×AAA from voltage because a depleted LiPo can overlap. Set alkaline or NiMH manually. A proven high-voltage LiPo can be detected automatically from an AAA selection. Do **not** connect AAA packs to the Badger 2350.

### Does the Badger 2040 charge a LiPo from USB?

No. Use proper off-board charging and check JST polarity. The Badger 2350 has its own charger, but this firmware does not show actual charging status.

### Does changing fields over USB write flash immediately?

No. `set` stages, `commit` persists, `revert` discards. The Badger 2040 Info-screen battery pack choice is an intentional special case that saves its own pack fields.

### Is the QR generated in the browser?

The complete screen-preview path invokes the existing **C++ host renderer** and QR/text-fit checks. Photo conversion in Studio also uses the established Python portrait converter. Previewing is not the same as physical QR scanning.

### Can APDS-9960 gestures wake a powered-off badge?

No. The optional gesture sensor is polled while awake. Use front buttons to wake; see [gesture sensor](Gesture-Sensor.md).

### Can I return to the factory firmware?

Yes, with the correct factory software image or a verified prior full-flash backup. On the Badger 2040, settings/assets may overwrite the former filesystem, so back up **before** installing. See [recovery](Installation-and-Recovery.md).

### Is the project open source?

The dependency licenses are documented, but the repository's own code **has no declared license** according to [LICENSES.md](../docs/LICENSES.md). Do not assume you have redistribution permissions beyond those explicitly granted. The owner needs to choose a project license.
