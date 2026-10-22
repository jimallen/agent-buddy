#pragma once
#include <cstdint>

namespace copilot {
// The CH422G I2C IO expander on the Waveshare ESP32-S3-Touch-LCD-7. It owns everything the
// 7-inch board needs before the panel can show anything: panel power, panel reset, backlight
// enable, touch reset, the SD card's chip select, and the USB/CAN routing mux.
//
// The chip does not use a normal register-mapped protocol. Each I2C "address" is a fixed
// command byte (the 7-bit form of the datasheet's 8-bit write address), so a write is one
// command byte followed by one data byte:
//   0x48 >> 1 = 0x24  WR_SET   direction/flags, bit 0 = IO_OE (1 = outputs enabled)
//   0x46 >> 1 = 0x23  WR_OC    open-drain enables per line
//   0x70 >> 1 = 0x38  WR_IO    output levels, bit n = EXIOn
//   0x4D >> 1 = 0x26  RD_IO    read back the levels
// Source: esp-arduino-libs/ESP32_IO_Expander, src/port/esp_io_expander_ch422g.c.
//
// The board layer drives it through Ch422gBus so host tests can watch the exact bytes.
class Ch422gBus {
 public:
  virtual ~Ch422gBus() = default;
  virtual bool write(uint8_t command, uint8_t value) = 0;
  virtual bool read(uint8_t command, uint8_t& value) = 0;
};

constexpr uint8_t kCh422gWriteSet = 0x24;
constexpr uint8_t kCh422gWriteOc = 0x23;
constexpr uint8_t kCh422gWriteIo = 0x38;
constexpr uint8_t kCh422gReadIo = 0x26;
// IO_OE, the only WR_SET bit this board needs: enable the outputs.
constexpr uint8_t kCh422gOutputEnable = 0x01;

// The expander lines this board uses, as bit numbers in the WR_IO byte.
enum class Ch422gLine : uint8_t {
  TouchReset = 1,      // TP_RST, drives the GT911 reset
  Backlight = 2,       // DISP, enables the AP3032 backlight driver
  PanelReset = 3,      // LCD_RST
  SdChipSelect = 4,    // SD_CS, active low
  UsbOrCan = 5,        // USB_SEL: low routes GPIO19/20 to native USB, high to CAN
  PanelPower = 6,      // LCD_VDD_EN
};

constexpr uint8_t ch422gBit(Ch422gLine line) {
  return static_cast<uint8_t>(1u << static_cast<uint8_t>(line));
}

class Ch422g {
 public:
  explicit Ch422g(Ch422gBus& bus) : bus_(bus) {}

  // Puts every line low and enables the outputs. The caller then raises what it needs, in
  // the order the panel datasheet wants: power, then reset, then backlight last.
  bool begin() {
    if (!bus_.write(kCh422gWriteSet, kCh422gOutputEnable)) return fail();
    if (!bus_.write(kCh422gWriteOc, 0x00)) return fail();
    levels_ = 0;
    return writeLevels();
  }

  bool set(Ch422gLine line, bool high) {
    const uint8_t bit = ch422gBit(line);
    const uint8_t next = high ? static_cast<uint8_t>(levels_ | bit)
                              : static_cast<uint8_t>(levels_ & ~bit);
    if (next == levels_) return true;
    levels_ = next;
    return writeLevels();
  }

  bool get(Ch422gLine line) const { return (levels_ & ch422gBit(line)) != 0; }

  // The panel reset is active low. Drive it down and then release it even when it is already
  // low, so the panel always sees a defined low period rather than a bare rising edge.
  bool pulsePanelReset() {
    const uint8_t bit = ch422gBit(Ch422gLine::PanelReset);
    levels_ = static_cast<uint8_t>(levels_ & ~bit);
    if (!writeLevels()) return false;
    levels_ = static_cast<uint8_t>(levels_ | bit);
    return writeLevels();
  }

  // Reads the chip's own output latch back, which proves the expander answered.
  bool verify(uint8_t& levels) {
    if (!bus_.read(kCh422gReadIo, levels)) return fail();
    return true;
  }

  uint8_t levels() const { return levels_; }
  const char* error() const { return error_; }

 private:
  bool writeLevels() {
    if (!bus_.write(kCh422gWriteIo, levels_)) return fail();
    return true;
  }
  bool fail() {
    error_ = "CH422G did not answer on the I2C bus.";
    return false;
  }

  Ch422gBus& bus_;
  uint8_t levels_ = 0;
  const char* error_ = nullptr;
};
}
