// Battery facts of the original Badger 2040 for the board-neutral core.
//
// Portable: no SDK headers. The core (settings, battery_pack) includes this
// through the target's include directory, in the firmware and in the host
// tests built for BHIHKH_TARGET=badger2040.
#pragma once

namespace badge::target {

// The board has no charger, so besides a single-cell LiPo it can run from
// Pimoroni's 2xAAA holder (alkaline or NiMH): battery.pack may select it.
constexpr bool kTwoAaaPackSupported = true;

}  // namespace badge::target
