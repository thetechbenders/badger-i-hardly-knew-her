# Badger 2350: proposed post-parity slice

Status: proposal only. This draft changes documentation, not firmware.
Physical validation of [PR #5](https://github.com/thetechbenders/badger-i-hardly-knew-her/pull/5)
is an explicit hypothetical prerequisite, not a completed result.

## Dependency and merge gates

This plan starts at parity commit `fc9cfd2451d8adc5369422a1f13868bc0cdb2729`.
Its branch is `feature/badger2350-four-tone-plan`; its PR base is
`feature/badger2350-parity`, so the review contains only this plan.
Do not merge this draft ahead of parity. Once parity has passed its physical
smoke test and is merged, retarget this PR to `main` and verify that the diff
contains only intended follow-up changes. If parity is squash-merged, transplant
only the follow-up commit(s) onto the resulting main history rather than
reintroducing parity commits. Any history rewrite needs its own authorization.
If parity changes first, review the dependency delta before continuing.

## Evidence and candidate feature set

There were no repository issues (open or closed) at inspection on 2026-10-02.
There is no formal roadmap. The following are documented candidates, not an
approved implementation order:

| Candidate | Repository evidence | Proposed disposition |
|---|---|---|
| Four-tone rendering | ARCHITECTURE.md display seam; PR #4 remaining display seams; PR #5 deferred list | First focused feature: opt-in portraits only |
| RTC and timed wake | ARCHITECTURE.md hardware table; PR #5 deferred list | Separate power/wake design after measured parity sleep/wake |
| Charging indication | BATTERY.md and PR #5; status is on CYW43 | Separate wireless-chip/power integration; USB alone must not mean charging |
| Rear lights | ARCHITECTURE.md GPIO 0-3; PR #5 deferred list | Separate feature with brightness and sleep-current policy |
| Wi-Fi/Bluetooth | PR #5 deferred list | Separate connectivity design with power budget and credentials policy |
| USB mass storage | PR #5 deferred list | Separate storage ownership and corruption/recovery design |
| PSRAM | ARCHITECTURE.md; present, unused | Enable only for a demonstrated memory requirement |
| OTA and web editor | PR #5 deferred list | Later projects requiring validated transport, update/recovery and input boundaries |

This prioritization is an engineering proposal. The repository does not establish
that every deferred capability is a committed product requirement.

## Next feature: opt-in four-tone portraits

Render the existing 104 x 176 portrait on Badger 2350 using four discrete tones.
Keep the default mono workflow and existing mono packs usable. Keep all text,
icons, status, QR modules and QR quiet zones strictly black/white. Cover both
badge layouts; other screens and navigation keep their existing behavior.
Do not add runtime tone settings or migrate settings/NVS in this slice.

Preserve native Pico SDK C++ and current dependency pins. No wireless, RTC,
timed wake, charging indicator, rear LEDs, PSRAM, mass storage, OTA, web editor,
partial refresh, waveform tuning, layout redesign or branding change.

## Implementation boundaries

1. **Portable display model:** extend the existing framebuffer abstraction to
   represent four tones for the capable target, with explicit portable target
   capability metadata. Keep the Classic 1-bpp allocation, mono ink semantics
   and previews unchanged. Define logical tone values independently of panel
   RAM bits; existing `Ink::Black` remains black. Hash, equality, copy, clipping
   and diff bounds must include changes between gray tones. Carry the complete
   frame through RenderScheduler, queues, shown-frame tracking and SimPanel;
   a separate mutable portrait overlay would bypass change suppression.
2. **Assets:** add a precisely specified packed 2-bpp format and a typed bitmap
   accessor alongside MONO1. Define value ordering, row stride, padding, byte
   order, dimensions, payload length and CRC checks in FORMATS.md before code.
   Inspect current unknown-format behavior: validation can accept unknown
   formats, while `asset_pack_bitmap` only retrieves MONO1. Specify explicit
   unsupported-format handling and a mono fallback for dual-target inputs;
   do not silently reinterpret gray data as mono. Continue reading existing
   version-1 MONO1 packs. Decide whether a new format ID suffices or a container
   version change is necessary based on compatibility tests, not convenience.
3. **Personalisation:** add an explicit Badger 2350 four-tone portrait option
   through portrait conversion, asset generation and the existing form/profile
   build flow. Reject that option for Classic. Preserve cropping, linear-light
   processing and source-image immutability; define deterministic quantization.
4. **Renderer/previews:** blit four-tone portraits into the same framebuffer;
   previews consume that framebuffer, not a separate approximation. Do not
   recolor text or QRs. Document that preview gray values illustrate logical
   tones and cannot establish the physical panel's measured luminance.
5. **SSD1680 backend:** pack the two RAM planes independently. Confirm tone
   ordering against pinned Pimoroni reference `7f2fa36732eb8160ec91c8499afbb0935dff63ec`
   before implementation, then check it on hardware. Keep the current LUT,
   voltages, bounded BUSY waits, boolean start failure propagation, backoff and
   forced-redraw behavior. Full refresh only; no partial capability claim.

## Memory budget to verify

At 264 x 176, the current 1-bpp frame and each panel plane are 5,808 bytes.
A packed 2-bpp frame is 11,616 bytes: +5,808 bytes per retained framebuffer.
Audit all retained frames/queue copies and stack allocations before settling
the representation. A 104 x 176 portrait is 4,576 bytes at 2 bpp versus 2,288
at 1 bpp, excluding pack metadata. Two retained controller planes add 5,808
bytes versus the current single plane; alternatively reuse a scratch plane
and pack sequentially. Choose from measured link/stack usage. This slice does
not need PSRAM merely to store one portrait.

## Required evidence for a later implementation

- Both target host suites with sanitizers and warnings treated as defects.
- Framebuffer tests: every tone, clip edges, padding, copies, hash/equality,
  gray-only differences, unchanged-frame suppression and queued redraws.
- Independent SSD1680 reference vectors: all four tones, alternating patterns,
  corners, orientation and both RAM planes. Mono output stays byte-identical.
- Asset/parser negatives: corrupt CRCs, dimensions, lengths, truncation,
  unsupported formats, compatibility and target refusal. Python generation and
  firmware decoding agree on deterministic fixtures.
- Both portrait layouts and all screens; unchanged Classic previews byte for
  byte. Decode all QR fixtures at native and enlarged scale; gray may never
  enter QR modules or quiet zones.
- Preserve refused-start regression, timeout/backoff recovery and redraw tests.
- Clean firmware cross-builds for both targets with pinned tools; zero warnings,
  artifact and cross-board refusal checks, packaging verification, reproducible
  rebuilds, and recorded flash/static RAM/stack implications.
- After parity physical validation, separately authorized four-tone hardware
  testing: tone ladder/orientation, portraits, mono screens and phone QR scans,
  repeated refresh/recovery and sleep/wake regression. Measure refresh duration,
  ghosting and power implications; do not infer them from host previews.

## Current draft validation

Documentation-only diff. No feature implementation or new tests exist yet.
Check whitespace, cited repository paths, dependency head and PR base before
publishing. Existing parity CI is evidence about parity, not this proposal or
an implemented four-tone feature. No merge, release or flashing is authorized
by this plan.
