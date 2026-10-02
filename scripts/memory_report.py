#!/usr/bin/env python3
"""Post-build flash/RAM report for the firmware image, with layout checks.

Fails the build if the firmware image reaches the asset region, or if static
RAM leaves too little room for the heap. The flash map and SRAM size come
from the target's entry in bhihkh_targets.py (Badger 2040: 264 KiB SRAM).
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from bhihkh_targets import DEFAULT, TARGETS

MIN_HEAP = 32 * 1024


def nm_symbols(nm: str, elf: str) -> dict[str, int]:
    out = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--map")
    ap.add_argument("--size", default="arm-none-eabi-size")
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--target", choices=sorted(TARGETS), default=DEFAULT)
    a = ap.parse_args(argv)
    t = TARGETS[a.target]
    FLASH_BASE, SRAM_BASE, SRAM_SIZE = t.xip_base, t.sram_base, t.sram_size
    ASSET_REGION = t.xip_base + t.asset_off
    syms = nm_symbols(a.nm, a.elf)
    flash_end = syms["__flash_binary_end"]
    bss_end = syms["__bss_end__"]
    heap_limit = syms.get("__StackLimit", SRAM_BASE + SRAM_SIZE - 8192)
    flash_used = flash_end - FLASH_BASE
    ram_static = bss_end - SRAM_BASE
    heap = heap_limit - bss_end
    sections = subprocess.run([a.size, "-A", "-x", a.elf], capture_output=True, text=True, check=True).stdout
    sized = subprocess.run([a.nm, "--size-sort", "-S", "-C", "-r", a.elf], capture_output=True, text=True,
                           check=True).stdout.splitlines()[:25]
    lines = [
        f"{t.board} firmware memory report",
        f"ELF: {Path(a.elf).name}",
        "",
        f"Flash image      {flash_used:8d} B  ({flash_used / 1024:.1f} KiB) of {ASSET_REGION - FLASH_BASE} B before the asset region",
        f"Static RAM       {ram_static:8d} B  ({ram_static / 1024:.1f} KiB) of {SRAM_SIZE} B",
        f"Heap available   {heap:8d} B  ({heap / 1024:.1f} KiB)",
        f"Stacks           core0 {syms['__StackTop'] - syms['__StackBottom']} B, core1 {syms['__StackOneTop'] - syms['__StackOneBottom']} B",
        "",
        f"Flash layout: firmware 0x000000.., assets 0x{t.asset_off:X} ({t.asset_size // 1024} KiB), "
        f"settings 0x{t.settings_off:X} (2 x {t.settings_size // 2048} KiB)",
        "",
        "Sections:",
        sections.strip(),
        "",
        "Largest symbols (size, type, name):",
        *sized,
    ]
    a.out.write_text("\n".join(lines) + "\n")
    print("\n".join(lines[3:7]))
    ok = True
    if flash_end > ASSET_REGION:
        print(f"error: firmware ends at 0x{flash_end:08x}, overlapping the asset region", file=sys.stderr)
        ok = False
    if heap < MIN_HEAP:
        print(f"error: only {heap} B left for the heap", file=sys.stderr)
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
