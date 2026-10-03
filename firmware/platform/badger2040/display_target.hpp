// Display facts of the original Badger 2040 for the board-neutral core.
//
// Portable: no SDK headers. The core (framebuffer, renderer) includes this
// through the target's include directory, in the firmware and in the host
// tests/previews built for BHIHKH_TARGET=badger2040.
#pragma once

namespace badge::target {

// UC8151 2.9" e-paper, landscape: 296 x 128, black/white.
constexpr int kDisplayWidth = 296;
constexpr int kDisplayHeight = 128;
constexpr bool kFourTone = false;

// Renderer layout (renderer.cpp). These are the values the Badger 2040
// screens were designed with; changing one changes the Classic image.
constexpr int kPortraitWidth = 104;   // designed portrait size (tools/badge_form.py)
constexpr int kPortraitHeight = 128;
constexpr int kQrCardMaxPx = 128;     // business-card QR side incl. quiet zone
constexpr int kQrFullMaxPx = 128;     // full-screen contact / project QR
constexpr int kIndexRowH = 14;        // project index row pitch
constexpr int kBadgeInterestLines = 3;  // badge layout A: interests wrap limit
// QR pages: the text beside the code keeps to one line per item (title
// shrinks, then ellipsizes) instead of wrapping.
constexpr bool kQrPageWraps = false;

}  // namespace badge::target
