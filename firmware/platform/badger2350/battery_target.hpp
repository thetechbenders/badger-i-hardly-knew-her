// Battery facts of the Badger 2350 for the board-neutral core.
//
// Portable: no SDK headers. The core (settings, battery_pack) includes this
// through the target's include directory, in the firmware and in the host
// tests built for BHIHKH_TARGET=badger2350.
#pragma once

namespace badge::target {

// The board charges its LiPo from USB, so it only ever runs from a LiPo: a
// 2xAAA pack on its connector would be charged as one. battery.pack stays 0.
constexpr bool kTwoAaaPackSupported = false;

}  // namespace badge::target
