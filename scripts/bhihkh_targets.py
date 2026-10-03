"""Per-target facts the post-build scripts and content tools check against.

One entry per implemented BHIHKH_TARGET (cmake/bhihkh_targets_list.cmake).
The flash map mirrors firmware/platform/<target>/flash_layout.hpp and the
display/portrait sizes its display_target.hpp (tests cross-check both); the
rest comes from the pinned SDK's linker script and board header for that
chip, and from picotool (UF2 family, RP2350-E10 block).
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Target:
    name: str             # BHIHKH_TARGET value
    board: str            # human name, used in reports
    chip: str
    description: str      # pico_set_program_description (target.cmake)
    artifact: str         # artifact file name prefix (BHIHKH_TARGET_ARTIFACT_PREFIX)
    uf2_family: int
    xip_base: int
    flash_size: int
    asset_off: int
    asset_size: int
    settings_off: int
    settings_size: int
    sram_base: int
    sram_size: int        # main SRAM plus both scratch banks
    scratch_x: int        # core 1 stack (memmap_default SCRATCH_X)
    scratch_y: int        # core 0 stack (SCRATCH_Y)
    stack_size: int
    display_w: int
    display_h: int
    portrait_w: int       # designed portrait size (display_target.hpp kPortrait*)
    portrait_h: int
    portrait_max_w: int   # widest portrait the form accepts (the text keeps its room)
    # RP2350-E10 workaround: picotool puts one "absolute"-family ignore
    # block at this address first in every flash UF2 (None: not used).
    uf2_abs_block: int | None = None

    @property
    def flash_layout_header(self) -> str:
        return f"firmware/platform/{self.name}/flash_layout.hpp"

    @property
    def display_header(self) -> str:
        return f"firmware/platform/{self.name}/display_target.hpp"

    @property
    def default_build_dir(self) -> str:
        """scripts/build-firmware.sh's directory for this target."""
        return "build/fw" if self.name == DEFAULT else f"build/fw-{self.name}"


RP2040_FAMILY_ID = 0xE48BFF56
RP2350_ARM_S_FAMILY_ID = 0xE48BFF59  # pico-sdk boot/uf2.h
ABSOLUTE_FAMILY_ID = 0xE48BFF57

TARGETS = {
    "badger2040": Target(
        name="badger2040",
        board="Badger 2040",
        chip="RP2040",
        description="Photo badge, business card and portfolio for the original Badger 2040",
        artifact="badger_badge",
        uf2_family=RP2040_FAMILY_ID,
        xip_base=0x10000000,
        flash_size=0x200000,    # 2 MiB W25Q16
        asset_off=0x1E0000, asset_size=0x10000,
        settings_off=0x1FC000, settings_size=0x4000,  # 2 x 8 KiB slots (legacy 4 KiB slots inside B)
        sram_base=0x20000000, sram_size=264 * 1024,
        scratch_x=0x20040000, scratch_y=0x20041000, stack_size=0x1000,
        display_w=296, display_h=128, portrait_w=104, portrait_h=128, portrait_max_w=148,
    ),
    "badger2350": Target(
        name="badger2350",
        board="Badger 2350",
        chip="RP2350A",
        description="Photo badge, business card and portfolio for the Badger 2350",
        artifact="badger2350_badge",
        uf2_family=RP2350_ARM_S_FAMILY_ID,
        xip_base=0x10000000,
        flash_size=0x1000000,   # 16 MiB (boards/pimoroni_badger2350.h)
        asset_off=0xFE0000, asset_size=0x10000,
        settings_off=0xFFC000, settings_size=0x4000,
        sram_base=0x20000000, sram_size=520 * 1024,  # 512 KiB + SCRATCH_X/Y (rp2350 default_locations.ld)
        scratch_x=0x20080000, scratch_y=0x20081000, stack_size=0x1000,
        display_w=264, display_h=176, portrait_w=104, portrait_h=176, portrait_max_w=132,
        uf2_abs_block=0x10FFFF00,  # picotool's default: the last page of 16 MiB
    ),
}
DEFAULT = "badger2040"
