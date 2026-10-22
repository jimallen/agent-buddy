# Progress: ESP32-S3-Touch-LCD-7 pin-level research

Status: complete.

Fetched and cross-checked: Waveshare wiki, schematic PDF (V1.2), waveshareteam/ESP32-S3-Touch-LCD-7 demo repo (cloned locally, read 05+ demo/lib files incl. bundled BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7.h and BusRGB source), arduino-esp32 3.3.10 variant pins_arduino.h, ESP32_IO_Expander CH422G header, LCD Wizard board preset, Arduino_GFX Arduino_ESP32RGBPanel source (polarity semantics).

All 8 task questions answered with concrete GPIOs. Final brief written to /tmp/lcd7-research.md.

Notable findings beyond the obvious:
- Backlight is CH422G EXIO2 on/off only (AP3032 boost); panel power gate is EXIO6 (LCD_VDD_EN) — must drive BOTH for a visible image.
- LCD reset EXIO3, touch reset EXIO1, SD CS EXIO4, USB/CAN mux EXIO5.
- GT911 @0x5D, I2C 8/9 @400 kHz, INT GPIO4; CH422G nominal 0x24/0x20 (chip ignores it).
- Timing: 16 MHz PCLK, 4/8/8 + 4/8/8 porches, syncs idle high, pclk_active_neg=1 (board conf).
- RS485 demo says TX16/RX15 while wiki/variant say TX15/RX16 (pins 15/16 either way).
- No audio, no IMU, no RTC. Battery: CS8501, PH2.0, 580 mA default.
- Dual USB-C: native GPIO19/20 (muxed with CAN) + CH343P → UART0 43/44.
- N16R8 = 16 MB QIO flash + 8 MB octal PSRAM @120 MHz (minor doc inconsistency: schematic shows N8R8 label).
