// Display facts of the Badger 2350 for the board-neutral core.
//
// Portable: no SDK headers. The core (framebuffer, renderer) includes this
// through the target's include directory, in the firmware and in the host
// tests/previews built for BHIHKH_TARGET=badger2350.
#pragma once

namespace badge::target {

// SSD1680 e-paper, landscape: 264 x 176. The controller can show four
// tones; this firmware draws black and white only (panel_ssd1680.hpp).
constexpr int kDisplayWidth = 264;
constexpr int kDisplayHeight = 176;

// Renderer layout (renderer.cpp), designed for 264 x 176: 32 px narrower
// and 48 px taller than the Badger 2040.
constexpr int kPortraitWidth = 104;   // full-height portrait, same width as the Badger 2040's
constexpr int kPortraitHeight = 176;
constexpr int kQrCardMaxPx = 128;     // same symbol size as the Badger 2040 card, text keeps its width
constexpr int kQrFullMaxPx = 148;     // full-screen QR: larger modules, room for the name beside it
constexpr int kIndexRowH = 20;        // seven rows, as on the Badger 2040, at a taller pitch
constexpr int kBadgeInterestLines = 5;  // the taller column fits more wrapped interests
// QR pages: beside the larger code the column is narrow but tall, so a long
// project title and the "back" hint wrap onto two lines, the link onto three.
constexpr bool kQrPageWraps = true;

}  // namespace badge::target
