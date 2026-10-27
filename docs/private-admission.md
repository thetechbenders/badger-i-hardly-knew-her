# Optional private admission (Badger 2350 only)

Disabled by default. This is a convenience display, not a secure lock against a determined physical attacker. Firmware containing a ticket is itself sensitive: flash, ELF, UF2, generated source, build caches and backups can expose it. Never upload those files to GitHub, Actions artifacts, Studio, public previews or support reports.

## Private build

Use a local checkout with the pinned dependencies (`scripts/fetch-deps.sh`) and Python tooling installed. Keep the official printable ticket and decoded bytes private under ignored `local/`. Use the PDF QR, not the Wallet pass's Aztec barcode. Decode the original QR locally with a byte-preserving decoder; keep the original phone/PDF ticket as backup. Do not copy text from a vCard viewer: it may alter escaping, order or newlines.

Create ignored `local/admission.toml` with exactly `payload`, `event`, `holder`, and `timeout_s`. Use a TOML basic string with explicit `\r\n` or `\n` escapes for each original newline; multiline TOML strings can normalize line endings. Example **fictional** schema:

```toml
payload = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Fictional Example\r\nNOTE:TEST ONLY - NOT VALID\r\nEND:VCARD\r\n"
event = "TEST"
holder = "Example"
timeout_s = 15
```

Payloads support 1–520 ASCII bytes, including CR and LF, without NUL or other controls. Labels support printable ASCII, at most 32 characters and 82 pixels in the compiled sans_11 font. Timeout is 1–15 seconds. Unsupported input is rejected without echoing values; never shorten or rewrite a real credential to make it fit. QR encoding uses Nayuki, integer scaling of at least two pixels per module and the complete four-module quiet zone. The renderer fails visibly if fitting fails.

Build from the repository root (bash/WSL):

```bash
BUILD_DIR="$PWD/build/private/admission-2350" scripts/build-firmware.sh \
  -DBHIHKH_TARGET=badger2350 \
  -DBADGER_ADMISSION_FILE=local/admission.toml
```

Use a separate build directory for this configuration. Private builds are rejected outside ignored `build/private/`, on Badger 2040, or in host builds. Generated credentials are linked only into that private firmware; ordinary settings, USB mutation commands and public host previews do not receive them. Do not upload the resulting image or use public CI for your real ticket. Build success is not scanner validation.

## Controls and concealment

From Info/Diagnostics (USR short), long A arms reveal for three seconds; long B within that window opens admission. Other input, expiry, screen changes and reboot cancel arming. The sequence is unavailable in safe mode and cannot be invoked by USB, gestures, the project index or normal navigation.

Any physical button press initiates concealment immediately. The screen automatically conceals after approximately 15 seconds (or the shorter configured timeout), measured from reveal request, including display latency. Concealment replaces the image with Info or the configured sleep image using a clean full refresh. Navigation stays blocked and queued input is discarded during replacement. Sleep and requested reboot wait for a successfully completed replacement, not merely an empty display queue. Display failure leaves the system awake and may retry; it does not claim successful concealment. Intentional crash-test commands are refused while a private image might remain visible.

E-paper retains its image without power. Sudden power loss, a watchdog crash or physical panel failure can leave the credential visible indefinitely. Firmware cannot guarantee concealment in these cases.

## Validation still required on hardware

CI uses only fictional vCards: exact CRLF bytes, pixel-level modules/quiet zone, native-size QR decoding, reveal/cancellation/timeout, dismissal, clean-refresh completion and fault paths; both boards' ordinary host and native firmware builds also run. Actual Badger 2350 scanner compatibility, panel timing, button behavior, power transitions and ghosting remain unverified until tested on the physical device. Keep the original ticket available during that testing.
