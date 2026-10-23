#pragma once
#include "ScreenTransform.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace copilot {
// Scanout for a controller-less RGB panel (the Waveshare ESP32-S3-Touch-LCD-7 and boards like it).
//
// Those panels have no "send a frame" step: the LCD_CAM peripheral continuously scans a
// framebuffer out of PSRAM. So this class owns that framebuffer and composites our square
// content into it, centred, painting the margins black. It replaces DisplayDma for such boards
// rather than running beside it.
//
// The content is kDisplaySize square; the panel is typically wider (800x480), so the content is
// centred and the extra area stays black. Rotation still goes through ScreenTransform, which
// emits display-order pixels, while the panel framebuffer holds native RGB565; the conversion
// between the two lives here and in one place only.
class RgbScanout {
 public:
  // panelSwapped flips the byte order written into the framebuffer. The RGB peripheral reads
  // native RGB565, so this stays false; it exists so a wiring surprise is a flag, not a rewrite.
  RgbScanout(uint16_t* framebuffer, int panelWidth, int panelHeight, int contentSize,
              uint16_t* scratch = nullptr, bool panelSwapped = false)
      : panel_(framebuffer), panelWidth_(panelWidth), panelHeight_(panelHeight),
        content_(contentSize), scratch_(scratch), swapped_(panelSwapped) {
    valid_ = framebuffer != nullptr && panelWidth > 0 && panelHeight > 0
             && contentSize > 0 && contentSize <= panelWidth && contentSize <= panelHeight;
    if (!valid_) error_ = "Invalid RGB panel geometry or framebuffer.";
  }

  // Paints the whole panel black once, so the margins are defined before the first frame.
  bool begin() {
    error_ = nullptr;
    if (!valid_) return false;
    std::memset(panel_, 0, static_cast<size_t>(panelWidth_) * panelHeight_ * sizeof(uint16_t));
    painted_ = true;
    return true;
  }

  bool setAngle(float radians) {
    if (!std::isfinite(radians)) {
      error_ = "Display angle must be finite.";
      return false;
    }
    if (radians == angle_) return false;
    angle_ = radians;
    return true;
  }
  float angle() const { return angle_; }

  // Composites one content frame. A rotated frame needs the scratch buffer supplied at
  // construction (contentSize squared pixels); an unrotated frame copies straight across.
  bool compose(const ScreenPixels& source) {
    error_ = nullptr;
    if (!valid_) return false;
    if (!source.pixels || source.x < 0 || source.y < 0 || source.width <= 0 || source.height <= 0
        || source.x >= content_ || source.y >= content_
        || source.width > content_ - source.x || source.height > content_ - source.y) {
      error_ = "Invalid RGB scanout source.";
      return false;
    }
    const bool rotate = angle_ != 0.0f;
    if (rotate && !scratch_) {
      error_ = "Rotated RGB scanout needs a scratch buffer.";
      return false;
    }
    const int originX = contentX();
    const int originY = contentY();
    if (rotate) {
      // Rotation needs the whole content frame, and ScreenTransform emits display order.
      const ScreenTransform transform(content_, content_, angle_);
      if (!transform.renderRows(source, 0, content_, scratch_,
                                static_cast<size_t>(content_) * content_)) {
        error_ = "RGB scanout rotation failed.";
        return false;
      }
      for (int y = 0; y < content_; ++y) {
        const uint16_t* from = scratch_ + static_cast<size_t>(y) * content_;
        uint16_t* to = panel_ + static_cast<size_t>(originY + y) * panelWidth_ + originX;
        for (int x = 0; x < content_; ++x) to[x] = swapBytes(from[x]);
      }
      painted_ = true;
      return true;
    }
    // Unrotated: source is a window of the content, packed at its own width (the device's
    // character frame is 412 wide inside 466 content), placed where it belongs in the panel.
    const bool convert = (source.order == Rgb565Order::Native) == swapped_;
    for (int y = 0; y < source.height; ++y) {
      if (yield_ && bandRows_ > 0 && y > 0 && y % bandRows_ == 0) yield_();
      const uint16_t* from = source.pixels + static_cast<size_t>(y) * source.width;
      uint16_t* to = panel_ + static_cast<size_t>(originY + source.y + y) * panelWidth_
                     + originX + source.x;
      if (convert) {
        // Two pixels per 32-bit store: the byte swap then costs one BSWAP instead of two
        // read-modify-writes per pixel, which was 40 ms for a full frame and left the panel
        // being rewritten almost continuously.
        int x = 0;
        for (; x + 1 < source.width; x += 2) {
          uint32_t pair;
          std::memcpy(&pair, from + x, sizeof(pair));
          pair = __builtin_bswap32(pair);
          std::memcpy(to + x, &pair, sizeof(pair));
        }
        for (; x < source.width; ++x) to[x] = swapBytes(from[x]);
      } else {
        std::memcpy(to, from, static_cast<size_t>(source.width) * sizeof(uint16_t));
      }
    }
    painted_ = true;
    return true;
  }

  // Where the content sits inside the panel. Odd differences leave the spare pixel on the right
  // and bottom, which keeps the content at a stable position for every frame.
  int contentX() const { return (panelWidth_ - content_) / 2; }
  int contentY() const { return (panelHeight_ - content_) / 2; }

  // A full-frame compose is hundreds of KB of PSRAM traffic. The panel fetches its scanout from
  // the same memory, so a single long burst starves it and the picture jumps. Yielding every few
  // rows lets the peripheral be refilled in between.
  void setPacing(int rowsPerBand, void (*yield)()) {
    bandRows_ = rowsPerBand > 0 ? rowsPerBand : 0;
    yield_ = yield;
  }

  uint16_t* framebuffer() const { return panel_; }
  bool painted() const { return painted_; }
  const char* error() const { return error_; }

 private:
  uint16_t* panel_;
  int panelWidth_, panelHeight_, content_;
  uint16_t* scratch_;
  bool swapped_;
  bool valid_ = false;
  bool painted_ = false;
  float angle_ = 0.0f;
  int bandRows_ = 0;
  void (*yield_)() = nullptr;
  const char* error_ = nullptr;
};
}
