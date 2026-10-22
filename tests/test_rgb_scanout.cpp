#include "../firmware/AgentCompanion/src/RgbScanout.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace copilot;

int main() {
  // The 7-inch geometry: our 466px content centred in an 800x480 RGB panel.
  {
    std::vector<uint16_t> panel(800 * 480, 0xDEAD);
    RgbScanout scanout(panel.data(), 800, 480, 466);
    assert(scanout.contentX() == (800 - 466) / 2);
    assert(scanout.contentY() == (480 - 466) / 2);
    assert(scanout.begin());

    // begin() clears the whole panel, margins included.
    for (uint16_t pixel : panel) assert(pixel == 0x0000);

    // A frame of native pixels lands centred, pixel for pixel.
    std::vector<uint16_t> content(466 * 466, 0x07E0);
    assert(scanout.compose({content.data(), 0, 0, 466, 466, Rgb565Order::Native}));
    const int ox = scanout.contentX(), oy = scanout.contentY();
    assert(panel[oy * 800 + ox] == 0x07E0);
    assert(panel[(oy + 465) * 800 + ox + 465] == 0x07E0);
    // One pixel outside the content on each side is still the black margin.
    assert(panel[oy * 800 + ox - 1] == 0x0000);
    assert(panel[(oy - 1) * 800 + ox] == 0x0000);
    assert(panel[(oy + 466) * 800 + ox] == 0x0000);
    assert(scanout.painted());
    assert(scanout.error() == nullptr);
  }

  // Display-order content is un-swapped on the way in; the panel holds native RGB565.
  {
    std::vector<uint16_t> panel(320 * 240, 0);
    RgbScanout scanout(panel.data(), 320, 240, 100);
    assert(scanout.begin());
    const uint16_t displayed = swapBytes(0x1234);
    std::vector<uint16_t> content(100 * 100, displayed);
    assert(scanout.compose({content.data(), 0, 0, 100, 100, Rgb565Order::Display}));
    assert(panel[scanout.contentY() * 320 + scanout.contentX()] == 0x1234);

    // Native content is written untouched.
    std::vector<uint16_t> native(100 * 100, 0x1234);
    assert(scanout.compose({native.data(), 0, 0, 100, 100, Rgb565Order::Native}));
    assert(panel[scanout.contentY() * 320 + scanout.contentX()] == 0x1234);
  }

  // A window of the content is placed at its own offset, and its rows are packed at its own
  // width: the device's character frame is 412 wide inside 466 content.
  {
    std::vector<uint16_t> panel(200 * 200, 0);
    RgbScanout scanout(panel.data(), 200, 200, 64);
    assert(scanout.begin());
    // A 10x4 window whose rows are 10 pixels apart, filled with a per-row marker.
    std::vector<uint16_t> content(10 * 4);
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 10; ++x) content[y * 10 + x] = static_cast<uint16_t>(0x1000 + y);
    assert(scanout.compose({content.data(), 8, 16, 10, 4, Rgb565Order::Native}));
    const int ox = scanout.contentX(), oy = scanout.contentY();
    for (int y = 0; y < 4; ++y) {
      // Right place...
      assert(panel[(oy + 16 + y) * 200 + ox + 8] == static_cast<uint16_t>(0x1000 + y));
      assert(panel[(oy + 16 + y) * 200 + ox + 8 + 9] == static_cast<uint16_t>(0x1000 + y));
      // ...and nothing written to the neighbouring columns or rows.
      assert(panel[(oy + 16 + y) * 200 + ox + 7] == 0);
      assert(panel[(oy + 16 + y) * 200 + ox + 18] == 0);
    }
    assert(panel[(oy + 15) * 200 + ox + 8] == 0);
    assert(panel[(oy + 20) * 200 + ox + 8] == 0);
    assert(scanout.error() == nullptr);
  }

  // Rotation goes through ScreenTransform and needs scratch; the result stays inside the panel.
  {
    std::vector<uint16_t> panel(200 * 200, 0);
    std::vector<uint16_t> scratch(64 * 64, 0);
    std::vector<uint16_t> content(64 * 64, 0);
    // A single lit pixel at the content's top-left corner.
    content[0] = 0xFFFF;
    RgbScanout scanout(panel.data(), 200, 200, 64, scratch.data());
    assert(scanout.begin());

    // Without an angle it copies straight through.
    assert(scanout.compose({content.data(), 0, 0, 64, 64, Rgb565Order::Native}));
    assert(panel[scanout.contentY() * 200 + scanout.contentX()] != 0);

    // setAngle reports the change once.
    assert(scanout.setAngle(3.14159265f * 0.5f));
    assert(!scanout.setAngle(3.14159265f * 0.5f));
    assert(scanout.compose({content.data(), 0, 0, 64, 64, Rgb565Order::Native}));

    // The lit pixel moved (a 90-degree turn takes the top-left corner to the top-right).
    const int cx = scanout.contentX(), cy = scanout.contentY();
    int lit = 0, litX = -1, litY = -1;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x)
        if (panel[(cy + y) * 200 + cx + x] != 0) { ++lit; litX = x; litY = y; }
    assert(lit >= 1);
    assert(litX > 32 && litY < 32);
  }

  // Rotation without scratch is refused rather than silently unrotated.
  {
    std::vector<uint16_t> panel(200 * 200, 0);
    std::vector<uint16_t> content(64 * 64, 0);
    RgbScanout scanout(panel.data(), 200, 200, 64);
    assert(scanout.begin());
    assert(scanout.setAngle(1.0f));
    assert(!scanout.compose({content.data(), 0, 0, 64, 64, Rgb565Order::Native}));
    assert(scanout.error() != nullptr);
  }

  // Bad geometry and bad sources are reported, never drawn.
  {
    std::vector<uint16_t> panel(100 * 100, 0);
    RgbScanout tooBig(panel.data(), 100, 100, 200);
    assert(!tooBig.begin());
    RgbScanout noBuffer(nullptr, 100, 100, 64);
    assert(!noBuffer.begin());

    RgbScanout scanout(panel.data(), 100, 100, 64);
    assert(scanout.begin());
    std::vector<uint16_t> content(64 * 64, 0);
    assert(!scanout.compose({nullptr, 0, 0, 64, 64, Rgb565Order::Native}));
    assert(!scanout.compose({content.data(), -1, 0, 64, 64, Rgb565Order::Native}));
    assert(!scanout.compose({content.data(), 0, 0, 65, 64, Rgb565Order::Native}));
    assert(!scanout.compose({content.data(), 32, 0, 40, 64, Rgb565Order::Native}));
    assert(scanout.error() != nullptr);
    assert(!scanout.setAngle(NAN));
  }

  std::cout << "rgb scanout ok\n";
  return 0;
}
