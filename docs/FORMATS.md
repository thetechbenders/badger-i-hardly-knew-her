# Data formats

All multi-byte values are little endian. CRC32 is IEEE 802.3 (Python
`zlib.crc32`).

## Flash map (2 MiB)

| Offset | Size | Contents | Written by |
|---|---|---|---|
| 0x000000 | ≤ 0x1E0000 | Firmware image (build fails if larger) | UF2 / picotool |
| 0x1E0000 | 64 KiB | Asset pack | `badger_badge-assets.uf2` only (never at runtime) |
| 0x1F0000 | 56 KiB | Unused guard | – |
| 0x1FE000 | 4 KiB | Settings slot A | `commit` |
| 0x1FF000 | 4 KiB | Settings slot B | `commit` |

## Settings record (one per 4 KiB slot)

```
off size field
 0   4   magic          0x53474442 ("BDGS")
 4   2   format_version 1  (other versions are rejected -> defaults)
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

Byte `x * 16 + y / 8`, bit `7 - y % 8`, 1 = black. This is identical to
`UC8151_Legacy::pixel()`, and a host test compares against a transcription of
that function.

## Profile JSON (`config/sample-profile.json`)

```json
{
  "format": 1,
  "profile": {
    "name": "", "title": "", "affiliation": "", "interests": "", "event": "",
    "contacts": [{"label": "Email", "value": "..."}],
    "qr": {"payload": "https://...", "caption": "..."},
    "projects": [{"title": "", "tagline": "", "body": "", "link": ""}]
  },
  "prefs": {"layout": 0, "refresh.speed": 1, "sleep.timeout_s": 120}
}
```

`tools/badge_profile.py` validates it with the same limits as the firmware; a
host test fails if the two tables drift apart. It is compiled into firmware
defaults (`profilegen.py`) or pushed over USB (`badgerctl.py push`).
