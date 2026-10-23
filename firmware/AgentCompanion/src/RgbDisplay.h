#pragma once
// The display for a controller-less RGB panel. Same surface as OrientedDisplay so the sketch does
// not care which board it runs on, but the pixels go to a framebuffer the LCD_CAM peripheral scans
// continuously instead of being pushed over SPI/QSPI (see RgbScanout.h).
#include "Config.h"
#include "RgbScanout.h"
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <cmath>

namespace copilot {
class RgbDisplay : public Arduino_Canvas {
 public:
  RgbDisplay(board::DisplayPanel* panel, void (*setBrightness)(uint8_t))
      : Arduino_Canvas(kDisplaySize, kDisplaySize, nullptr), panel_(panel),
        setBrightness_(setBrightness) {}
  ~RgbDisplay() { delete scanout_; }

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    error_ = nullptr;
    if (!panel_->begin(speed)) {
      error_ = "RGB panel initialization failed.";
      return false;
    }
    uint16_t* framebuffer = panel_->framebuffer(0);
    if (!framebuffer) {
      error_ = "The RGB panel reported no framebuffer.";
      return false;
    }
    if (!_framebuffer) {
      _framebuffer = static_cast<uint16_t*>(heap_caps_aligned_alloc(
          16, static_cast<size_t>(kDisplaySize) * kDisplaySize * sizeof(uint16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!_framebuffer) {
      error_ = "Orientation canvas allocation failed.";
      return false;
    }
    if (!scratch_) {
      scratch_ = static_cast<uint16_t*>(heap_caps_aligned_alloc(
          16, static_cast<size_t>(kDisplaySize) * kDisplaySize * sizeof(uint16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!scratch_) {
      error_ = "Rotation scratch allocation failed.";
      return false;
    }
    delete scanout_;
    scanout_ = new RgbScanout(framebuffer, kRgbPanelWidth, kRgbPanelHeight,
                              kDisplaySize, scratch_, kPanelSwappedBytes);
    if (!scanout_->begin()) {
      error_ = scanout_->error();
      return false;
    }
    // Both buffers start black, so the first presented frame has defined margins.
    for (int index = 0; index < board::DisplayPanel::kFrameBuffers; ++index) {
      if (uint16_t* other = panel_->framebuffer(index)) {
        std::memset(other, 0, static_cast<size_t>(kRgbPanelWidth) * kRgbPanelHeight * sizeof(uint16_t));
      }
    }
    scanout_->setAngle(angle_ + trim_);
    // 32 rows is ~3 KB per band; a short pause between bands keeps the panel's fetch fed.
    scanout_->setPacing(32, [] { delayMicroseconds(150); });
    return true;
  }

  // The canvas itself is the content: flush() shows what the sketch drew.
  void flush(bool forceFlush = false) override {
    (void)forceFlush;
    compose({_framebuffer, 0, 0, kDisplaySize, kDisplaySize, Rgb565Order::Native});
  }

  // On this board the frame buffers already hold values the panel can read directly: the RGB
  // peripheral takes native RGB565, and the scanout does no swapping unless a board asks for it.
  // (The SPI boards push the same buffers as display order because their panel expects bytes.)
  bool flushFrame(const uint16_t* pixels, int x, int y, int width, int height) {
    return compose({pixels, x, y, width, height, Rgb565Order::Native});
  }

  bool setAngle(float radians) {
    if (!std::isfinite(radians)) return false;
    if (radians == angle_) return false;
    angle_ = radians;
    if (scanout_) scanout_->setAngle(angle_ + trim_);
    return true;
  }

  bool setTrim(float radians) {
    if (!std::isfinite(radians)) return false;
    if (radians == trim_) return false;
    trim_ = radians;
    if (scanout_) scanout_->setAngle(angle_ + trim_);
    return true;
  }

  float angle() const { return angle_ + trim_; }

  // Nothing is staged for DMA on this board; the LCD_CAM peripheral reads the framebuffer itself.
  // Accepted and ignored so the shared setup path does not need a special case.
  bool setTransferBuffer(uint8_t*, size_t) { return true; }

  void setBrightness(uint8_t brightness) { setBrightness_(brightness); }

  // The framebuffer the peripheral scans, for callers that want the whole panel (the fleet on a
  // wide board). Null before begin().
  uint16_t* panelFramebuffer(int& width, int& height) {
    width = kRgbPanelWidth;
    height = kRgbPanelHeight;
    return panel_ ? panel_->framebuffer(back_) : nullptr;
  }

  // Present a panel-sized surface composed elsewhere. The panel scans the other buffer throughout,
  // so this copy cannot tear the picture being shown.
  bool presentSurface(const uint16_t* pixels, int width, int height) {
    error_ = nullptr;
    uint16_t* target = panel_ ? panel_->framebuffer(back_) : nullptr;
    if (!target || !pixels || width != kRgbPanelWidth || height != kRgbPanelHeight) {
      error_ = "The RGB panel cannot present this surface.";
      return false;
    }
    const size_t bytes = static_cast<size_t>(width) * height * sizeof(uint16_t);
    uint16_t* painted = panel_->framebuffer(back_);
    if (!painted) return false;
    // Paint the buffer the panel is not scanning, hand it over, and only then bring the other one
    // up to date - by which time the panel has moved off it. Copying into both up front writes the
    // buffer being scanned, which is exactly what made the picture fail while the tokens animated.
    std::memcpy(painted, pixels, bytes);
    if (!panel_->present(back_)) {
      error_ = panel_->error();
      return false;
    }
    back_ ^= 1;
    if (uint16_t* spare = panel_->framebuffer(back_)) std::memcpy(spare, pixels, bytes);
    return true;
  }
  const char* error() const { return error_; }
  uint32_t composeUs() const { return composeUs_; }
  uint32_t writeUs() const { return writeUs_; }

 private:
  bool compose(const ScreenPixels& source) {
    error_ = nullptr;
    if (!scanout_) {
      error_ = "The RGB panel is not started.";
      return false;
    }
    const int64_t started = esp_timer_get_time();
    uint16_t* front = panel_->framebuffer(0);
    scanout_->setTarget(front);
    if (!scanout_->compose(source)) {
      error_ = scanout_->error();
      return false;
    }
    composeUs_ = esp_timer_get_time() - started;
    // Same order as presentSurface: hand the painted buffer over first, then catch the other up
    // while the panel is busy scanning the one just presented.
    if (!panel_->present(0)) {
      error_ = panel_->error();
      return false;
    }
    if (uint16_t* spare = panel_->framebuffer(1)) {
      std::memcpy(spare, front, static_cast<size_t>(kRgbPanelWidth) * kRgbPanelHeight * sizeof(uint16_t));
    }
    // The panel scans the framebuffer out on its own, so there is no transfer cost to report.
    writeUs_ = 0;
    return true;
  }

  board::DisplayPanel* panel_;
  void (*setBrightness_)(uint8_t);
  RgbScanout* scanout_ = nullptr;
  uint16_t* scratch_ = nullptr;
  float angle_ = 0.0f;
  float trim_ = 0.0f;
  const char* error_ = nullptr;
  uint32_t composeUs_ = 0;
  uint32_t writeUs_ = 0;
  // The buffer being painted; the panel scans the other one.
  int back_ = 0;
};
}
