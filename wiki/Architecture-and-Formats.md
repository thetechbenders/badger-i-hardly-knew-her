# Architecture, execution and data formats

BHIHKH! separates portable application logic from the Pico hardware drivers. Both targets use the same product model; **pin assignments and panel/power behavior live in target-specific code**.

## Code map

| Directory | Responsibility |
|---|---|
| `firmware/core/` | Screen state machine, renderer, text, QR, bitmap assets, settings, battery policy, input gestures, flash record model, display scheduler |
| `firmware/platform/pico/` | SDK composition root, USB CDC, flash, diagnostics, watch-dog/power services |
| `firmware/platform/badger2040/` | RP2040 board, UC8151, power latch and ADC circuit |
| `firmware/platform/badger2350/` | RP2350 board, SSD1680, power manager and ADC circuit |
| `firmware/cli/` | USB command parser and validation |
| `host/`, `tests_py/`, `tests_js/` | Host simulator/test harnesses and tooling regression tests |
| `tools/`, `scripts/` | Asset/profile generation, preview, build, verification and backup |
| `studio/` | Optional local web editor, operating on the validated TOML model |

The renderer builds the target's row-major 1-bit framebuffer. A panel backend translates it to the controller-specific memory packing and refresh protocol. The display scheduler coalesces requests; the portable settings model is isolated from flash erase/program specifics. The USB parser validates mutations and separates staged from committed values.

## Flash/storage contract

**Badger 2040** flash (2 MiB) has firmware below `0x1E0000`, a 64 KiB asset region at `0x1E0000`, a guard gap and **two 8 KiB settings slots** at `0x1FC000` and `0x1FE000`.

**Badger 2350** has 16 MiB; the asset and settings arrangement sits near the top: asset region `0xFE0000–0xFEFFFF`, settings `0xFFC000–0xFFFFFF`. Different UF2 families and target flash maps are guarded by build verification.

Settings use a versioned TLV format with CRC protection and A/B records. Existing format-1 settings can be read and later migrated on a successful commit. Battery type keys added in PR #12 have distinct TLV IDs rather than bumping the entire record version. **Never change a field ID or flash map casually.**

Asset packs contain validated prepared portrait bitmaps. TOML is the human authoring source; JSON is the portable profile used by tools/firmware. Advanced fields and vCard options must not be silently lost when editing through Studio.

## Defensive boundaries

Watchdog and crash records support reset attribution. After repeated resets, safe mode avoids applying stored settings and exposes recovery. The panel BUSY path has bounded retries rather than blocking USB forever. Flash writes are staged and read-back verified; only a distinct valid slot supersedes the previous record.

This wiki summarizes architectural intent, not an exhaustive proof. The authoritative references are [ARCHITECTURE.md](../docs/ARCHITECTURE.md), [FORMATS.md](../docs/FORMATS.md), [REVIEW.md](../docs/REVIEW.md) and [current source](../firmware/core/).
