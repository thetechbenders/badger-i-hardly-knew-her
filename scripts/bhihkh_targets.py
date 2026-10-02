"""Per-target facts the post-build scripts check a firmware image against.

One entry per implemented BHIHKH_TARGET (cmake/bhihkh_target.cmake). The
flash map mirrors firmware/platform/<target>/flash_layout.hpp, which
verify_artifacts.py cross-checks; the rest comes from the SDK's linker
script and board header for that chip.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Target:
    name: str             # BHIHKH_TARGET value
    board: str            # human name, used in reports
    description: str      # pico_set_program_description (target.cmake)
    uf2_family: int
    xip_base: int
    flash_size: int
    asset_off: int
    asset_size: int
    settings_off: int
    settings_size: int
    sram_base: int
    sram_size: int
    scratch_x: int        # core 1 stack (memmap_default.ld SCRATCH_X)
    scratch_y: int        # core 0 stack (SCRATCH_Y)
    stack_size: int

    @property
    def flash_layout_header(self) -> str:
        return f"firmware/platform/{self.name}/flash_layout.hpp"


TARGETS = {
    "badger2040": Target(
        name="badger2040",
        board="Badger 2040",
        description="Photo badge, business card and portfolio for the original Badger 2040",
        uf2_family=0xE48BFF56,  # RP2040
        xip_base=0x10000000,
        flash_size=0x200000,    # 2 MiB W25Q16
        asset_off=0x1E0000, asset_size=0x10000,
        settings_off=0x1FC000, settings_size=0x4000,  # 2 x 8 KiB slots (legacy 4 KiB slots inside B)
        sram_base=0x20000000, sram_size=264 * 1024,
        scratch_x=0x20040000, scratch_y=0x20041000, stack_size=0x1000,
    ),
}
DEFAULT = "badger2040"
