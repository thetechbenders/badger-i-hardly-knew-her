#!/usr/bin/env python3
"""Verify a firmware build directory before anything is flashed.

  scripts/verify_artifacts.py build/fw [--require-clean]
  scripts/verify_artifacts.py build/fw-badger2350 --target badger2350

Checks, all offline (no device involved):
  - UF2 files: valid blocks, the target's family ID, 256-byte pages; the firmware
    UF2 covers exactly the .bin from XIP_BASE and ends before the asset
    region; the asset UF2 lies inside the asset region and matches the
    generated pack; neither touches the guard gap or the settings sectors.
    RP2350: each flash UF2 starts with picotool's RP2350-E10 block (family
    "absolute", flagged RP2_IGNORE_BLOCK, at the target's address), which the
    boot ROM never writes; it is checked and then left out of the regions.
  - Linker placement: both 4 KiB stacks in their scratch banks, core 1's
    stack array where multicore_launch_core1() puts the stack, and the crash
    record in the NOLOAD .uninitialized_data section, outside .data/.bss.
  - Build metadata: program name and `git describe` string embedded;
    optionally refuse a -dirty build.
  - memory-report.txt present and consistent; SHA256SUMS matches every file.

The facts checked come from the build's BHIHKH_TARGET entry in
bhihkh_targets.py; --target must match the build directory's CMake cache.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import uf2  # noqa: E402
from bhihkh_targets import DEFAULT, TARGETS  # noqa: E402

errors: list[str] = []


def check(cond: bool, msg: str) -> None:
    print(("ok    " if cond else "FAIL  ") + msg)
    if not cond:
        errors.append(msg)


def tool(name: str, override: str | None) -> str:
    t = override or shutil.which(name)
    if not t:
        raise SystemExit(f"{name} not found (pass --{name.split('-')[-1]})")
    return t


def uf2_blocks(path: Path, family: int, abs_block: int | None = None) -> list[tuple[int, int, int, bytes]]:
    blob = path.read_bytes()
    check(len(blob) % 512 == 0 and blob, f"{path.name}: whole 512-byte blocks")
    out = []
    first = 0
    if abs_block is not None:
        b = blob[:512]
        ok = len(b) == 512 and uf2.is_abs_block(b) and struct.unpack("<I", b[12:16])[0] == abs_block
        check(ok, f"{path.name}: starts with the RP2350-E10 ignore block at 0x{abs_block:08x}")
        first = 512 if ok else 0
    for off in range(first, len(blob) - 511, 512):
        b = blob[off:off + 512]
        m0, m1, flags, addr, size, _no, _n, fam = struct.unpack("<8I", b[:32])
        (mend,) = struct.unpack("<I", b[508:])
        if (m0, m1, mend) != (uf2.UF2_MAGIC0, uf2.UF2_MAGIC1, uf2.UF2_MAGIC_END):
            check(False, f"{path.name}: block {off // 512} magic")
            continue
        out.append((addr, size, flags, fam, b[32:32 + size]))
    check(all(f & uf2.UF2_FLAG_FAMILY_ID and fam == family for _, _, f, fam, _ in out),
          f"{path.name}: every block tagged family 0x{family:08x}")
    check(all(s == 256 and a % 256 == 0 for a, s, *_ in out), f"{path.name}: 256-byte aligned pages")
    return [(a, s, fam, d) for a, s, _f, fam, d in out]


def span(blocks) -> tuple[int, int]:
    return min(a for a, *_ in blocks), max(a + s for a, s, *_ in blocks)


def overlaps(lo: int, hi: int, rlo: int, rhi: int) -> bool:
    return lo < rhi and rlo < hi


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", type=Path)
    ap.add_argument("--name", help="artifact prefix (default: the target's, e.g. badger_badge, badger2350_badge)")
    ap.add_argument("--nm")
    ap.add_argument("--readelf")
    ap.add_argument("--require-clean", action="store_true", help="fail if the embedded version is -dirty")
    ap.add_argument("--target", choices=sorted(TARGETS), default=DEFAULT, help=f"BHIHKH_TARGET (default {DEFAULT})")
    a = ap.parse_args(argv)
    t = TARGETS[a.target]
    d, n = a.build, a.name or t.artifact
    XIP, FLASH_SIZE, STACK = t.xip_base, t.flash_size, t.stack_size
    ASSET_OFF, ASSET_SIZE = t.asset_off, t.asset_size
    SETTINGS_OFF, SETTINGS_SIZE = t.settings_off, t.settings_size
    SCRATCH_X, SCRATCH_Y = t.scratch_x, t.scratch_y

    # --- target -----------------------------------------------------------------
    cache = d / "CMakeCache.txt"
    if cache.exists():
        m = re.search(r"^BHIHKH_TARGET:\w+=(.*)$", cache.read_text(), re.M)
        built = m.group(1) if m else "badger2040"  # configured before BHIHKH_TARGET existed
        check(built == t.name, f"build directory configured for BHIHKH_TARGET={t.name} (CMake cache: {built})")
    else:
        print(f"skip  CMakeCache.txt (not a CMake build directory); checking as {t.name}")

    # --- UF2 / flash regions ------------------------------------------------
    fw = uf2_blocks(d / f"{n}.uf2", t.uf2_family, t.uf2_abs_block)
    lo, hi = span(fw)
    image = b"".join(p for _, _, _, p in sorted(fw))
    binary = (d / f"{n}.bin").read_bytes()
    check(lo == XIP, f"firmware UF2 starts at XIP_BASE (0x{lo:08x})")
    check(sorted(a_ for a_, *_ in fw) == list(range(lo, hi, 256)), "firmware UF2 pages are contiguous")
    check(image[:len(binary)] == binary and set(image[len(binary):]) <= {0xFF, 0x00},
          f"firmware UF2 payload == {n}.bin ({len(binary)} B)")
    check(hi <= XIP + ASSET_OFF, f"firmware ends at 0x{hi:08x}, before the asset region 0x{XIP + ASSET_OFF:08x}")
    assets = uf2_blocks(d / f"{n}-assets.uf2", t.uf2_family, t.uf2_abs_block)
    alo, ahi = span(assets)
    check(alo == XIP + ASSET_OFF and ahi <= XIP + ASSET_OFF + ASSET_SIZE,
          f"asset UF2 inside the asset region (0x{alo:08x}..0x{ahi:08x})")
    pack = (d / "generated" / "builtin_assets.bin").read_bytes()
    apayload = b"".join(p for _, _, _, p in sorted(assets))
    check(apayload[:len(pack)] == pack and pack[:4] == b"BAPK", "asset UF2 payload == generated asset pack")
    guard = (XIP + ASSET_OFF + ASSET_SIZE, XIP + SETTINGS_OFF)
    settings = (XIP + SETTINGS_OFF, XIP + SETTINGS_OFF + SETTINGS_SIZE)
    for label, (blo, bhi) in (("firmware", (lo, hi)), ("assets", (alo, ahi))):
        check(not overlaps(blo, bhi, *guard) and not overlaps(blo, bhi, *settings),
              f"{label} UF2 leaves the guard gap and settings sectors untouched")
    check(not overlaps(lo, hi, alo, ahi), "firmware and asset UF2s do not overlap")
    check(SETTINGS_OFF + SETTINGS_SIZE == FLASH_SIZE,
          f"settings are the top {SETTINGS_SIZE // 1024} KiB of {FLASH_SIZE // (1024 * 1024)} MiB")
    layout = (ROOT / t.flash_layout_header).read_text()
    check(f"kSettingsOffset = 0x{SETTINGS_OFF:X}" in layout and f"kAssetOffset = 0x{ASSET_OFF:X}" in layout,
          f"verifier flash map matches {t.flash_layout_header}")

    # --- linker placement ------------------------------------------------------
    elf = d / f"{n}.elf"
    nm = tool("arm-none-eabi-nm", a.nm)
    readelf = tool("arm-none-eabi-readelf", a.readelf)
    syms: dict[str, int] = {}
    for line in subprocess.run([nm, str(elf)], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms.setdefault(parts[2], int(parts[0], 16))
    check(syms.get("__StackBottom") == SCRATCH_Y and syms.get("__StackTop") == SCRATCH_Y + STACK,
          f"core 0 stack = SCRATCH_Y 0x{SCRATCH_Y:08x}..0x{SCRATCH_Y + STACK:08x} (what paint_stacks/memory() measure)")
    check(syms.get("__StackOneBottom") == SCRATCH_X and syms.get("__StackOneTop") == SCRATCH_X + STACK,
          f"core 1 stack = SCRATCH_X 0x{SCRATCH_X:08x}..0x{SCRATCH_X + STACK:08x}")
    check(syms.get("core1_stack") == syms.get("__StackOneBottom"),
          "multicore core1_stack[] sits at __StackOneBottom (stack used == stack measured)")
    sections = {}
    for line in subprocess.run([readelf, "-SW", str(elf)], capture_output=True, text=True, check=True).stdout.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)", line)
        if m:
            sections[m.group(1)] = (m.group(2), int(m.group(3), 16), int(m.group(4), 16))
    rec = [v for k, v in syms.items() if re.search(r"g_rec", k)]
    un = sections.get(".uninitialized_data")
    check(un is not None and un[0] == "NOBITS", ".uninitialized_data exists and is NOLOAD (NOBITS)")
    if un and rec:
        r = rec[0]
        inside = lambda s: s in sections and sections[s][1] <= r < sections[s][1] + sections[s][2]  # noqa: E731
        check(inside(".uninitialized_data") and not inside(".data") and not inside(".bss"),
              f"crash record at 0x{r:08x} in .uninitialized_data, not zeroed/copied by crt0")
    else:
        check(False, "crash record symbol found")

    # --- build metadata ----------------------------------------------------------
    blob = elf.read_bytes()
    # The program name and description (pico_set_program_*). The repository
    # name is not used: it only appears when a profile links the repository.
    check(b"BHIHKH!\0" in binary and t.description.encode() + b"\0" in binary,
          "program name and target description embedded")
    others = [o.name for o in TARGETS.values() if o.name != t.name and o.description.encode() in binary]
    check(f"\0{t.name}\0".encode() in binary and not others,
          f"image built for {t.name} (build_info target), no other board's description")
    try:
        desc = subprocess.run(["git", "-C", str(ROOT), "describe", "--always", "--dirty", "--tags", "--abbrev=12"],
                              capture_output=True, text=True, check=True).stdout.strip()
        check(desc.encode() in binary, f"git describe '{desc}' embedded")
        if a.require_clean:
            check(not desc.endswith("-dirty"), "built from a clean commit")
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("skip  git describe (no git)")
    check(str(ROOT).encode() not in blob, "no absolute checkout path in the ELF (reproducible across locations)")
    check(str(d.resolve()).encode() not in blob and (d.name == "fw" or f"/{d.name}/".encode() not in blob),
          "no build-directory path in the ELF (reproducible across build-directory names)")

    # --- memory report + checksums ----------------------------------------------------
    rep = (d / "memory-report.txt").read_text()
    m = re.search(r"Flash image\s+(\d+) B", rep)
    check(bool(m) and XIP + int(m.group(1)) <= XIP + ASSET_OFF, "memory report present, image below the asset region")
    check(f"settings 0x{SETTINGS_OFF:X} (2 x {SETTINGS_SIZE // 2048} KiB)" in rep,
          "memory report states the current settings layout")
    check(f"core0 {STACK} B, core1 {STACK} B" in rep, f"memory report: {STACK // 1024} KiB stack per core")
    sums = (d / "SHA256SUMS").read_text().split("\n")
    listed = 0
    for line in filter(None, sums):
        digest, name = line.split(maxsplit=1)
        listed += 1
        check(hashlib.sha256((d / name).read_bytes()).hexdigest() == digest, f"SHA256 {name}")
    check(listed >= 6, "SHA256SUMS lists every artifact")

    print(f"\n{'FAILED: ' + str(len(errors)) + ' check(s)' if errors else 'all checks passed'}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
