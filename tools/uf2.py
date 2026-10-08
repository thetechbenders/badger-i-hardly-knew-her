"""Minimal UF2 writer for RP2040 / RP2350 flash images (https://github.com/microsoft/uf2)."""
from __future__ import annotations

import struct

UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
UF2_FLAG_EXTENSION_FLAGS = 0x00008000
UF2_EXTENSION_RP2_IGNORE_BLOCK = 0x9957E304
RP2040_FAMILY_ID = 0xE48BFF56
ABSOLUTE_FAMILY_ID = 0xE48BFF57
RP2350_ARM_S_FAMILY_ID = 0xE48BFF59
FLASH_BASE = 0x10000000
PAGE = 256


def abs_block(address: int) -> bytes:
    """The RP2350-E10 workaround block, byte for byte as picotool 2.3.1 writes
    it first in a flash UF2 (elf2uf2.cpp gen_abs_block): family "absolute",
    payload 0xEF, flagged RP2_IGNORE_BLOCK so the boot ROM never writes it."""
    hdr = struct.pack("<8I", UF2_MAGIC0, UF2_MAGIC1, UF2_FLAG_FAMILY_ID | UF2_FLAG_EXTENSION_FLAGS, address,
                      PAGE, 0, 2, ABSOLUTE_FAMILY_ID)
    data = b"\xef" * PAGE + struct.pack("<I", UF2_EXTENSION_RP2_IGNORE_BLOCK)
    block = hdr + data + b"\x00" * (476 - len(data)) + struct.pack("<I", UF2_MAGIC_END)
    assert len(block) == 512
    return block


def is_abs_block(block: bytes) -> bool:
    """picotool's check_abs_block()."""
    m0, m1, flags, _addr, size, no, n, fam = struct.unpack("<8I", block[:32])
    (mend,) = struct.unpack("<I", block[508:512])
    ext = struct.unpack("<I", block[32 + PAGE:36 + PAGE])[0]
    return (block[32:32 + PAGE] == b"\xef" * PAGE and (m0, m1, mend) == (UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END)
            and flags & ~UF2_FLAG_EXTENSION_FLAGS == UF2_FLAG_FAMILY_ID and size == PAGE and n == 2
            and fam == ABSOLUTE_FAMILY_ID and no == 0
            and not (flags & UF2_FLAG_EXTENSION_FLAGS and ext != UF2_EXTENSION_RP2_IGNORE_BLOCK))


def to_uf2(data: bytes, address: int, family: int = RP2040_FAMILY_ID, abs_block_at: int | None = None) -> bytes:
    if address % PAGE:
        raise ValueError("UF2 target address must be 256-byte aligned")
    if len(data) % PAGE:
        data = data + b"\xff" * (PAGE - len(data) % PAGE)
    n = len(data) // PAGE
    out = bytearray(abs_block(abs_block_at) if abs_block_at is not None else b"")
    for i in range(n):
        chunk = data[i * PAGE:(i + 1) * PAGE]
        hdr = struct.pack("<8I", UF2_MAGIC0, UF2_MAGIC1, UF2_FLAG_FAMILY_ID, address + i * PAGE,
                          PAGE, i, n, family)
        block = hdr + chunk + b"\x00" * (476 - PAGE) + struct.pack("<I", UF2_MAGIC_END)
        assert len(block) == 512
        out += block
    return bytes(out)


def from_uf2(blob: bytes) -> dict[int, bytes]:
    """Parse UF2 back into {address: 256-byte page} (used by tests). An
    RP2350-E10 block is skipped: the boot ROM does not write it."""
    pages = {}
    for off in range(0, len(blob), 512):
        b = blob[off:off + 512]
        m0, m1, flags, addr, size, _no, _n, fam = struct.unpack("<8I", b[:32])
        (mend,) = struct.unpack("<I", b[508:512])
        if (m0, m1, mend) != (UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END):
            raise ValueError(f"bad UF2 block at {off}")
        if is_abs_block(b):
            continue
        pages[addr] = b[32:32 + size]
    return pages
