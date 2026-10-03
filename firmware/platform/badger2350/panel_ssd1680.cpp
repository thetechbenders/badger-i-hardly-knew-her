#include "panel_ssd1680.hpp"

#include <cstddef>
#include <initializer_list>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "ssd1680_pack.hpp"

namespace badge {

namespace {
spi_inst_t *const kSpi = BADGER2350_INKY_SPI == 0 ? spi0 : spi1;
constexpr uint kCs = BADGER2350_INKY_CSN_PIN, kDc = BADGER2350_INKY_DC_PIN, kSck = BADGER2350_INKY_SCK_PIN,
               kMosi = BADGER2350_INKY_MOSI_PIN, kBusy = BADGER2350_INKY_BUSY_PIN, kReset = BADGER2350_INKY_RESET_PIN;

// SSD1680 commands used by the reference driver.
enum : uint8_t {
  kDriverOutput = 0x01,   // DOC: gate lines
  kGateVoltage = 0x03,    // GDVC
  kSourceVoltage = 0x04,  // SDVC
  kBoosterSoftStart = 0x0C,
  kDataEntryMode = 0x11,
  kSoftwareReset = 0x12,
  kActivate = 0x20,       // master activation: run the update sequence
  kUpdateControl2 = 0x22,
  kWriteRamBw = 0x24,
  kWriteRamRed = 0x26,
  kWriteVcom = 0x2C,
  kWriteLut = 0x32,
  kLutEndOption = 0x3F,
  kRamXRange = 0x44,
  kRamYRange = 0x45,
  kRamXCounter = 0x4E,
  kRamYCounter = 0x4F,
};

// RAM window: X 0..21 (176 / 8 bytes), Y from 263 (0x0107) down to 0.
constexpr uint8_t kXStart = 0x00, kXEnd = 0x15, kYStartL = 0x07, kYStartH = 0x01;

// Waveform of the reference driver, byte for byte: 5 x 12 voltage-select
// bytes, 12 groups of 7 timing bytes (3 used), frame-rate/XON config.
constexpr uint8_t kLut[153] = {
    0x40, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VS L0
    0xA0, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VS L1
    0xA8, 0x65, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VS L2
    0xAA, 0x65, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VS L3
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VS L4
    0x02, 0x00, 0x00, 0x05, 0x0A, 0x00, 0x00,  // group 0
    0x19, 0x19, 0x00, 0x02, 0x00, 0x00, 0x00,  // group 1
    0x05, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00,  // group 2
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // groups 3..11 unused
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x44, 0x42, 0x22, 0x22, 0x23, 0x32, 0x00, 0x00, 0x00,  // FR, XON
};

// Reuse one scratch plane; frames remain owned by DisplayService while packing.
uint8_t g_plane[ssd1680::kPlaneBytes];

void command(uint8_t reg, const uint8_t *data, size_t len) {
  gpio_put(kCs, 0);
  gpio_put(kDc, 0);  // command
  spi_write_blocking(kSpi, &reg, 1);
  if (len) {
    gpio_put(kDc, 1);  // data
    spi_write_blocking(kSpi, data, len);
  }
  gpio_put(kCs, 1);
}

void command(uint8_t reg) { command(reg, nullptr, 0); }

// Arrays and braced lists carry their length (no default length argument:
// it would win overload resolution over this template and send nothing).
template <size_t N>
void command(uint8_t reg, const uint8_t (&data)[N]) {
  command(reg, data, N);
}
}  // namespace

bool Ssd1680Panel::busy() { return gpio_get(kBusy); }  // high = busy

bool Ssd1680Panel::wait_idle(uint32_t timeout_ms) {
  const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  while (gpio_get(kBusy)) {
    if (time_reached(deadline)) return false;
    sleep_us(100);
  }
  return true;
}

// Reference setup(): reset pulse, software reset, gate count, data entry
// mode and RAM window. Every BUSY wait is bounded.
bool Ssd1680Panel::reset_and_setup() {
  gpio_put(kReset, 0);
  sleep_ms(10);
  gpio_put(kReset, 1);
  sleep_ms(10);
  if (!wait_idle(kResetTimeoutMs)) return false;
  command(kSoftwareReset);
  if (!wait_idle(kResetTimeoutMs)) return false;
  command(kDriverOutput, {kYStartL, kYStartH, 0x00});
  command(kDataEntryMode, {0x01});  // X increment, Y decrement
  command(kRamXRange, {kXStart, kXEnd});
  command(kRamYRange, {kYStartL, kYStartH, 0x00, 0x00});
  return wait_idle(kResetTimeoutMs);
}

// Reference write_luts(): waveform, LUT end option, gate/source/VCOM voltages.
bool Ssd1680Panel::write_waveform() {
  command(kWriteLut, kLut);
  command(kLutEndOption, {0x22});
  command(kGateVoltage, {0x17});
  command(kSourceVoltage, {0x41, 0xAE, 0x32});
  command(kWriteVcom, {0x28});
  return wait_idle(kResetTimeoutMs);
}

bool Ssd1680Panel::init(uint8_t speed) {
  speed_ = speed;
  spi_init(kSpi, kSpiHz);
  gpio_init(kDc);
  gpio_set_dir(kDc, GPIO_OUT);
  gpio_init(kCs);
  gpio_set_dir(kCs, GPIO_OUT);
  gpio_put(kCs, 1);
  gpio_init(kReset);
  gpio_set_dir(kReset, GPIO_OUT);
  gpio_put(kReset, 1);
  gpio_init(kBusy);
  gpio_set_dir(kBusy, GPIO_IN);
  gpio_set_function(kSck, GPIO_FUNC_SPI);
  gpio_set_function(kMosi, GPIO_FUNC_SPI);
  return reset_and_setup() && write_waveform();
}

// One waveform: nothing to change. Kept so the display service's speed and
// clean-refresh handling stays target-neutral.
bool Ssd1680Panel::set_speed(uint8_t speed) {
  speed_ = speed;
  return true;
}

// Reference update(), without its final wait: waveform, both RAM planes,
// update sequence 0xC7 (clock and analog on, display, both off again), then
// the trigger. DisplayService only calls this while not busy().
bool Ssd1680Panel::start_full(const Framebuffer &fb) {
  if (!write_waveform()) return false;  // no commands while BUSY: abort, nothing triggered
  for (uint8_t ram : {kWriteRamRed, kWriteRamBw}) {
    ssd1680::pack(fb, g_plane, ram == kWriteRamRed ? ssd1680::Plane::Red : ssd1680::Plane::Bw);
    command(kRamXCounter, {kXStart});
    command(kRamYCounter, {kYStartL, kYStartH});
    command(ram, g_plane, sizeof g_plane);
  }
  command(kBoosterSoftStart);
  command(kUpdateControl2, {0xC7});
  if (!wait_idle(kCommandTimeoutMs)) return false;  // not triggered
  command(kActivate);
  return true;
}

void Ssd1680Panel::start_partial(const Framebuffer &fb, Rect) { (void)start_full(fb); }

// The 0xC7 sequence already turns the booster and clock off at its end.
void Ssd1680Panel::finish() {}

}  // namespace badge
