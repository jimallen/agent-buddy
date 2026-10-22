#pragma once
// Device-only drivers for the Waveshare ESP32-S3-Touch-LCD-7 (see BoardConfig.h).
#include "BoardConfig.h"
#include "../drivers/Ch422g.h"
#include "../../RgbDisplay.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TouchDrvGT911.hpp>
#include <Wire.h>

namespace copilot {
namespace board {
using TouchController = TouchDrvGT911;
// The GT911 reports up to five points; the firmware only uses the first.
constexpr uint8_t kTouchPointCapacity = 5;
static_assert(kTouchI2cAddress == GT911_SLAVE_ADDRESS_L, "Touch address differs from the driver.");

enum class Variant : uint8_t { Standard };
constexpr const char* variantName(Variant) { return "standard"; }
inline Variant variant() { return Variant::Standard; }

// The CH422G speaks one byte per fixed command address, so the bus adapter is three lines.
class WireCh422gBus : public Ch422gBus {
 public:
  bool write(uint8_t command, uint8_t value) override {
    Wire.beginTransmission(command);
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }
  bool read(uint8_t command, uint8_t& value) override {
    if (Wire.requestFrom(command, static_cast<uint8_t>(1)) != 1) return false;
    value = static_cast<uint8_t>(Wire.read());
    return true;
  }
};

inline Ch422g& expander() {
  static WireCh422gBus bus;
  static Ch422g chip(bus);
  return chip;
}

// Runs first in setup(): brings up I2C, powers the panel, resets it, and leaves the backlight off
// until the first frame fades in. The USB/CAN mux stays on native USB so the daemon can reach us.
inline void beginBoard() {
  Wire.begin(kTouchSda, kTouchScl, kI2cFrequency);
  Ch422g& io = expander();
  io.begin();
  io.set(Ch422gLine::PanelPower, true);
  io.pulsePanelReset();
  io.set(Ch422gLine::TouchReset, true);
  io.set(Ch422gLine::UsbOrCan, false);
  io.set(Ch422gLine::Backlight, false);
}

inline DisplayPanel& displayPanel() {
  static Arduino_ESP32RGBPanel* rgbpanel = new Arduino_ESP32RGBPanel(
      kRgbDePin, kRgbVsyncPin, kRgbHsyncPin, kRgbPclkPin,
      kRgbR0, kRgbR1, kRgbR2, kRgbR3, kRgbR4,
      kRgbG0, kRgbG1, kRgbG2, kRgbG3, kRgbG4, kRgbG5,
      kRgbB0, kRgbB1, kRgbB2, kRgbB3, kRgbB4,
      kRgbHsyncPolarity, kRgbHfrontPorch, kRgbHpulseWidth, kRgbHbackPorch,
      kRgbVsyncPolarity, kRgbVfrontPorch, kRgbVpulseWidth, kRgbVbackPorch,
      kRgbPclkActiveNeg, kRgbPclkFrequency, /* useBigEndian */ false,
      /* de_idle_high */ 0, /* pclk_idle_high */ 0,
      // A bounce buffer is what stops a PSRAM-backed RGB panel from drifting and shimmering:
      // the peripheral stages a slice of the line in internal memory instead of racing the
      // external bus for every pixel. Waveshare's own demo uses ten lines' worth.
      static_cast<size_t>(kRgbPanelWidth) * kRgbBounceLines);
  static DisplayPanel panel(kRgbPanelWidth, kRgbPanelHeight, rgbpanel);
  return panel;
}

// On or off only: the backlight is an enable line, not a PWM dimmer.
inline void setDisplayBrightness(uint8_t brightness) {
  expander().set(Ch422gLine::Backlight, brightness >= 8);
}

// The GT911 needs its reset released before it answers, which beginBoard() does; nothing else.
inline void configureTouch(TouchController& touch) { (void)touch; }

// This board has no accelerometer. Reporting that honestly leaves orientation to the settings
// menu instead of failing at boot.
class AbsentAccelerometer {
 public:
  const char* begin() { return "This board has no orientation sensor."; }
  bool read(float&, float&, float&) { return false; }
};

inline AbsentAccelerometer& accelerometer() {
  static AbsentAccelerometer sensor;
  return sensor;
}

// This board has no audio codec: the same calls succeed and produce silence, so the sound
// settings and the cue scheduler keep working.
class SilentAudioOutput {
 public:
  const char* begin(unsigned) { return nullptr; }
  bool enable(uint8_t) { return true; }
  bool write(const uint8_t*, size_t) { return true; }
  void disable() {}
  int mclkPin() const { return -1; }
  void setVolume(int) {}
  void end() {}
};

inline SilentAudioOutput& audioOutput() {
  static SilentAudioOutput output;
  return output;
}

inline RgbDisplay& createDisplay() {
  static RgbDisplay display(&displayPanel(), setDisplayBrightness);
  return display;
}
}  // namespace board
}  // namespace copilot
