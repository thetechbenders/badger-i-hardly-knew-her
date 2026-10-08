# Data formats

All multi-byte values are little endian. CRC32 is IEEE 802.3 (Python
`zlib.crc32`).

## Flash map (2 MiB)

| Offset | Size | Contents | Written by |
|---|---|---|---|
| 0x000000 | ≤ 0x1E0000 | Firmware image (build fails if larger) | UF2 / picotool |
| 0x1E0000 | 64 KiB | Asset pack | `badger_badge-assets.uf2` only (never at runtime) |
| 0x1F0000 | 48 KiB | Unused guard | – |
| 0x1FC000 | 8 KiB | Settings slot A (format 2) | `commit` |
| 0x1FE000 | 8 KiB | Settings slot B (format 2) | `commit` |

Firmware before the project portfolio used format 1 in two 4 KiB slots at
0x1FE000 and 0x1FF000, which now lie inside format-2 slot B. See
[Migration from format 1](#migration-from-format-1).

### Badger 2350 (16 MiB)

The same arrangement at the top of the larger flash, and the same settings
and asset formats (`firmware/platform/badger2350/flash_layout.hpp`):

| Offset | Size | Contents | Written by |
|---|---|---|---|
| 0x000000 | ≤ 0xFE0000 | Firmware image (build fails if larger) | UF2 / picotool |
| 0xFE0000 | 64 KiB | Asset pack | `badger2350_badge-assets.uf2` only (never at runtime) |
| 0xFF0000 | 48 KiB | Unused guard | – |
| 0xFFC000 | 8 KiB | Settings slot A (format 2) | `commit` |
| 0xFFE000 | 8 KiB | Settings slot B (format 2) | `commit` |

There are no format-1 records on this board. No partition table: both UF2s
are absolute, in the RP2350 Arm Secure family, and each starts with
picotool's RP2350-E10 block (family "absolute", 0x10FFFF00, flagged
RP2_IGNORE_BLOCK, so the boot ROM never writes it).

## Settings record (one per 8 KiB slot)

```
off size field
 0   4   magic          0x53474442 ("BDGS")
 4   2   format_version 2  (1 is read only for migration; others -> defaults)
 6   2   header_size    24
 8   2   payload_len
10   2   flags          0
12   4   sequence       incremented per commit; newest valid slot wins
                        (serial-number comparison, wraps safely)
16   4   payload_crc    CRC32(payload)
20   4   header_crc     CRC32(bytes 0..19)
24   …   payload        TLV entries: u16 id, u16 len, len bytes
```

- **IDs are stable** and never reused (see `firmware/core/settings.cpp`).
  Unknown IDs are skipped (forward compatible). A known field that fails
  validation (length, UTF-8, control characters, numeric range) keeps its
  default and is counted as `rejected`.
- **Structural errors** (magic, CRCs, lengths, TLV overrun) invalidate the
  whole slot. Erased flash (all 0xFF) is reported as `erased`, not corrupt.
- **Commit**: encode, write the non-active slot (erase then program), read
  back, full decode and compare. Only then does the new slot become active.
  Losing power at any point leaves the old record valid.
- **No flash write on navigation.** Browsing screens or projects only changes
  RAM state; flash is written by `commit` (and `erase` / factory reset) only.

### Field IDs

| IDs | Fields |
|---|---|
| 0x001–0x0FF | identity (`name`, `title`, `affiliation`, `interests`, `event`, `qr.*`) |
| 0x100 + 2(n−1), +1 | `contactN.label`, `contactN.value` (n = 1..6) |
| 0x140 + (n−1) | `contactN.type`: empty, `email`, `phone`, `web`, `github`, `discord`, `text` |
| 0x200 + 8(n−1) + k | `projectN.*` (n = 1..12): k = 0 `title`, 1 `tagline`, 2 `body`, 3 `link`, 4 `status`, 5 `banner`; 6–7 reserved |
| 0x300– | preferences (`layout`, `refresh.*`, `sleep.*`, `battery.*`, `gesture.*`, …) |

`projectN.link` must be empty or an `https://` URL with a host and no
spaces. `contactN.type` must be one of the names above, exactly. Types are
never guessed from the label: an untyped line renders as before, with its
text label and no icon.

A worst-case record (every string at its byte limit) is 7331 bytes, about
7.2 KiB of the 8 KiB slot; a host test checks it fits. Raising a limit or
the project count needs that headroom rechecked.

### Migration from format 1

1. Boot looks for a valid format-2 record in slots A and B first. A format-1
   header at the start of slot B (the old slot A) is expected and is not
   reported as corruption.
2. If there is none, it reads the legacy 4 KiB slots and uses the newest valid
   one. `status` reports `v1 (migrate on commit)` and `diag settings` names
   the legacy slot. The
   sequence number continues from it.
3. A format-1 record always stored its project slots, so its project list is
   authoritative: it replaces the built-in default projects rather than
   being merged with them. Contacts keep their labels and values and become untyped: a default
   type from the new firmware is never attached to an old value.
4. Nothing is written until the next `commit`. That first commit goes to
   format-2 **slot A**, which does not overlap the legacy slots, so a torn
   first commit still falls back to the format-1 record. Later commits
   alternate A/B as usual and eventually overwrite the legacy data.

Host tests cover a v1 record round-trip, a torn first commit, leftover
legacy data next to a newer format-2 record, and the worst-case size.

## Asset pack

```
Header (32 bytes)
 0  u32 magic 'BAPK' (0x4B504142)   16 u32 data_crc   CRC32(bytes 32 .. total_size)
 4  u16 version 1                   20 u32 header_crc CRC32(bytes 0..19)
 6  u16 header_size 32              24 u8[8] reserved, must be zero
 8  u16 entry_count (<= 16)
10  u16 flags 0
12  u32 total_size (<= 64 KiB)
Entry (24 bytes each, after the header)
 0  u16 id (1 = portrait)            8 u32 offset (4-aligned, after the entry table)
 2  u8  format (1 = MONO1)          12 u32 length
 3  u8  reserved                    16 u32 crc32 of the entry data
 4  u16 width   6 u16 height        20 u32 reserved
```

**MONO1**: row-major, MSB first, bit 1 = black ink, stride = ceil(width/8),
width ≤ 296 and height ≤ 128, length = stride × height exactly.

The firmware validates every field (sizes, bounds, alignment, overlap with
the table, per-entry and whole-pack CRCs) before use. It prefers a valid
flashed pack, then the built-in pack, then no portrait.
`tools/assetpack.py inspect` applies the same rules on the host.

## Framebuffer / panel layout

The renderer's `Framebuffer` is board-neutral: row-major, byte
`y * stride + x / 8` (stride = width / 8), bit `7 - x % 8`, 1 = black, the
same packing as MONO1 assets and PBM previews. Each panel backend converts
it to its controller's RAM:

- Badger 2040 (UC8151, 296×128): byte `x * 16 + y / 8`, bit `7 - y % 8`,
  1 = black (`badger2040/uc8151_pack.hpp`). Identical to
  `UC8151_Legacy::pixel()`; a host test compares against a transcription of
  that function.
- Badger 2350 (SSD1680, 264×176): byte `x * 22 + y / 8`, bit `7 - y % 8`,
  1 = black, the same plane sent to both RAMs (0x26 and 0x24)
  (`badger2350/ssd1680_pack.hpp`); a host test compares against the plane
  loop of Pimoroni's reference driver.

## Profile JSON (`config/sample-profile.json`)

```json
{
  "format": 1,
  "profile": {
    "name": "", "title": "", "affiliation": "", "interests": "", "event": "",
    "contacts": [{"label": "Email", "value": "...", "type": "email"},
                 {"label": "GitHub", "value": "user", "type": "github"}],
    "qr": {"payload": "https://...", "caption": "..."},
    "projects": [{"title": "", "tagline": "", "body": "", "status": "",
                  "link": "https://github.com/owner/repo"},
                 {"title": "", "banner": "TOP SECRET - COMING SOON"}]
  },
  "prefs": {"layout": 0, "refresh.speed": 1, "sleep.timeout_s": 120}
}
```

`tools/badge_profile.py` validates it with the same limits as the firmware; a
host test fails if the two tables drift apart. It also rejects more than 12
projects, unknown project fields, a project with content but no title, a
non-https link and an unknown contact type. `type` is optional, so older
profiles stay valid. It is compiled into firmware
defaults (`profilegen.py`) or pushed over USB (`badgerctl.py push`).

## Fill-in form (`config/badge-form.toml`)

A TOML file with `form = 1` and the sections `[person]`, `[[contacts]]`,
`[qr]`, `[portrait]`, `[[projects]]` and `[preferences]`, documented field
by field in the template and in [PERSONALIZE.md](PERSONALIZE.md).
`tools/badge_form.py` validates it strictly (unknown fields, types, choices,
counts, links, glyphs, portrait files) and turns it into the profile JSON
above; it is not a second schema. `description` becomes `body`, a list of
`interests` is joined with " · ", `qr.show` selects the `qr.payload`
(link, vCard text or file, or a vCard built from the contacts), and every
identity key is always present (empty = not shown). `badge_profile.load()`
accepts a `.toml` path, so `profilegen.py`, `render_previews.py` and
`badgerctl.py push` read forms too. The generated `profile.json` carries a
`_generated_by` marker. Its output directory carries a `.badge-form-output`
manifest (JSON: the paths it wrote); only those are replaced. A
`[[contacts]]` block with `hidden = true` keeps its slot with an empty value
(never drawn), so label-only contacts of a JSON profile survive `export`.
Form backups record the form and output paths in `INPUTS.json`.
