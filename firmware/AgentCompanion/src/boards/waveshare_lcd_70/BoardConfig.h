#pragma once
// Waveshare ESP32-S3-Touch-LCD-7: an 800x480 RGB panel (controller-less ST7262 class) driven by
// the LCD_CAM peripheral, GT911 touch, and a CH422G IO expander for panel power, reset and
// backlight. No IMU and no audio codec on this board.
// Pin and timing source: Waveshare's wiki, schematic and demo repo, plus
// esp-arduino-libs/ESP32_IO_Expander for the expander protocol.
// Plain constants only: native tests include this file without the vendor libraries.
#include <cstdint>

class Arduino_RGB_Display;

namespace copilot {
namespace board {
// Declared here so display code can name the panel without the vendor library.
using DisplayPanel = ::Arduino_RGB_Display;
constexpr char kId[] = "waveshare-lcd-70";
constexpr char kName[] = "Waveshare ESP32-S3-Touch-LCD-7";
constexpr char kDisplayPanelName[] = "ST7262";
constexpr char kTouchControllerName[] = "GT911";
// This board has no accelerometer; orientation comes from the settings menu only.
constexpr char kImuName[] = "none";
}  // namespace board

// The firmware draws 466x466 content. The panel is 800x480, so the content is centred with black
// margins (see RgbScanout.h); kPanelSize stays at the content size so the existing scaling rules
// and the settings layout keep working unchanged.
constexpr int kDisplaySize = 466;
constexpr int kPanelSize = 466;
constexpr int kRgbPanelWidth = 800;
constexpr int kRgbPanelHeight = 480;

// RGB panel sync and clock lines.
constexpr int kRgbDePin = 5;
constexpr int kRgbVsyncPin = 3;
constexpr int kRgbHsyncPin = 46;
constexpr int kRgbPclkPin = 7;
// 16 data lines, in the order Arduino_ESP32RGBPanel takes them (R0-4, G0-5, B0-4).
constexpr int kRgbR0 = 1, kRgbR1 = 2, kRgbR2 = 42, kRgbR3 = 41, kRgbR4 = 40;
constexpr int kRgbG0 = 39, kRgbG1 = 0, kRgbG2 = 45, kRgbG3 = 48, kRgbG4 = 47, kRgbG5 = 21;
constexpr int kRgbB0 = 14, kRgbB1 = 38, kRgbB2 = 18, kRgbB3 = 17, kRgbB4 = 10;
// Waveshare's demo timing. pclk_active_neg 1 samples on the falling edge; if the image tears or
// shows a one-pixel shift on the bench, this is the first value to flip.
constexpr int kRgbPclkFrequency = 16000000;
constexpr int kRgbHsyncPolarity = 1, kRgbHfrontPorch = 8, kRgbHpulseWidth = 4, kRgbHbackPorch = 8;
// Vertical jumping on glass means the panel is not latching VSYNC where we think. Polarity 0
// makes the sync idle low and pulse high; the demo code that ships with this panel uses the
// opposite of its own board config on this one argument, so it is the first thing to try.
constexpr int kRgbVsyncPolarity = 0, kRgbVfrontPorch = 8, kRgbVpulseWidth = 4, kRgbVbackPorch = 8;
constexpr int kRgbPclkActiveNeg = 1;
// Ten lines staged in internal RAM; the vendor demo value, and the difference between a
// stable picture and a shimmering one on this class of panel.
constexpr int kRgbBounceLines = 10;
// The backlight is a plain enable on the CH422G (an AP3032 boost driver), so brightness is on/off.
// The LCD_CAM reads the framebuffer as native RGB565, while the renderer's frame buffers hold
// big-endian words (the pack encoding is word-up-be). So a byte swap is needed on the way in.
constexpr bool kPanelSwappedBytes = true;
constexpr int kBrightness = 255;
// The RGB panel takes no bus speed; -1 means "use the driver default".
constexpr int kDisplaySpeed = -1;

// Touch and the IO expander share one I2C bus (Arduino Wire, I2C port 0).
constexpr unsigned kI2cPort = 0;
constexpr uint32_t kI2cFrequency = 400000;
constexpr int kTouchSda = 8;
constexpr int kTouchScl = 9;
constexpr int kTouchInterrupt = 4;
// The GT911 reset is on CH422G EXIO1, not a GPIO.
constexpr int kTouchReset = -1;
constexpr uint8_t kTouchI2cAddress = 0x5D;
constexpr bool kTouchMirrorX = false;
constexpr bool kTouchMirrorY = false;

// The BOOT button. Held at power-on it starts the ROM downloader; after that it is free to read.
constexpr int kBootButtonPin = 0;
}  // namespace copilot
