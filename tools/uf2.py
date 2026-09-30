"""Minimal UF2 writer for RP2040 flash images (https://github.com/microsoft/uf2)."""
from __future__ import annotations

import struct

UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
FLASH_BASE = 0x10000000
PAGE = 256


def to_uf2(data: bytes, address: int, family: int = RP2040_FAMILY_ID) -> bytes:
    if address % PAGE:
        raise ValueError("UF2 target address must be 256-byte aligned")
    if len(data) % PAGE:
        data = data + b"\xff" * (PAGE - len(data) % PAGE)
    n = len(data) // PAGE
    out = bytearray()
    for i in range(n):
        chunk = data[i * PAGE:(i + 1) * PAGE]
        hdr = struct.pack("<8I", UF2_MAGIC0, UF2_MAGIC1, UF2_FLAG_FAMILY_ID, address + i * PAGE,
                          PAGE, i, n, family)
        block = hdr + chunk + b"\x00" * (476 - PAGE) + struct.pack("<I", UF2_MAGIC_END)
        assert len(block) == 512
        out += block
    return bytes(out)


def from_uf2(blob: bytes) -> dict[int, bytes]:
    """Parse UF2 back into {address: 256-byte page} (used by tests)."""
    pages = {}
    for off in range(0, len(blob), 512):
        b = blob[off:off + 512]
        m0, m1, flags, addr, size, _no, _n, fam = struct.unpack("<8I", b[:32])
        (mend,) = struct.unpack("<I", b[508:512])
        if (m0, m1, mend) != (UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END):
            raise ValueError(f"bad UF2 block at {off}")
        pages[addr] = b[32:32 + size]
    return pages
