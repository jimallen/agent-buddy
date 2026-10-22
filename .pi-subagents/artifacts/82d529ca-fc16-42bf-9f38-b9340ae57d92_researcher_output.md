# Research: Waveshare ESP32-S3-Touch-LCD-7 — hardware pin-level brief for an Arduino_GFX firmware port

## Summary

The ESP32-S3-Touch-LCD-7 is an ESP32-S3-WROOM-1-N16R8 (16 MB QIO flash + 8 MB **octal** PSRAM) board driving a raw 800×480 RGB565 (ST7262-class, controller-less) panel over 20 GPIOs, with a GT911 touch, a **CH422G I2C IO-expander** handling backlight/panel-reset/panel-power/SD-CS/USB-CAN-mux, plus RS485, CAN, TF slot, two USB-C ports (native USB-OTG **and** a CH343P USB-UART bridge), and a 3.7 V battery charger. Waveshare's official demos do **not** use Arduino_GFX — they use ESP-IDF's esp_lcd RGB panel (either directly via ESP-IDF or via the ESP32_Display_Panel `BusRGB` + `LCD_ST7262` wrapper in Arduino). All timings/pins below map 1:1 onto `Arduino_ESP32RGBPanel` + `Arduino_RGB_Display`.

## Findings

### 1. RGB LCD panel

Panel is a 50-pin RGB glass with an **ST7262** driver (works "raw" — no SPI init commands, no D/C). 16-bit RGB565 bus on the upper bits of an RGB888 interface (B3–B7, G2–G7, R3–R7). Source: Waveshare wiki pinout table + schematic J1 connector + demo code.

**Sync/clock pins** ([wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7), [demo header](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7/blob/main/examples/Arduino/examples/08_DrawColorBar/waveshare_lcd_port.h)):

| Signal | GPIO |
|---|---|
| DE | 5 |
| VSYNC | 3 |
| HSYNC | 46 |
| PCLK | 7 |
| DISP / backlight | CH422G EXIO2 (not a GPIO) |
| LCD_RST | CH422G EXIO3 (not a GPIO) |

**16 data pins** — shown both as panel color bits and in `Arduino_ESP32RGBPanel` R0..R4/G0..G5/B0..B4 order, and in esp_lcd D0..D15 order (all three sources agree: [wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7), [arduino-esp32 variant pins_arduino.h](https://github.com/espressif/arduino-esp32/blob/3.3.10/variants/waveshare_esp32_s3_touch_lcd_7/pins_arduino.h), [ESP32_Display_Panel board config](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7/blob/main/examples/Arduino/libraries/ESP32_Display_Panel/src/board/supported/waveshare/BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7.h)):

| Panel bit | GPIO | Arduino_GFX arg | esp_lcd D# |
|---|---|---|---|
| B3 | 14 | b0 | 0 |
| B4 | 38 | b1 | 1 |
| B5 | 18 | b2 | 2 |
| B6 | 17 | b3 | 3 |
| B7 | 10 | b4 | 4 |
| G2 | 39 | g0 | 5 |
| G3 | 0 | g1 | 6 |
| G4 | 45 | g2 | 7 |
| G5 | 48 | g3 | 8 |
| G6 | 47 | g4 | 9 |
| G7 | 21 | g5 | 10 |
| R3 | 1 | r0 | 11 |
| R4 | 2 | r1 | 12 |
| R5 | 42 | r2 | 13 |
| R6 | 41 | r3 | 14 |
| R7 | 40 | r4 | 15 |

**Timing used in demos** (08_DrawColorBar + board config + wiki code review):
- PCLK = **16 MHz**; Bounce buffer = width×10 px (8000) to avoid screen drift; single frame buffer in PSRAM.
- HPW=4, HBP=8, HFP=8, VPW=4, VBP=8, VFP=8 (same values in both the Arduino demo and the ESP-IDF/LVGL board config).
- Polarities: demos leave esp_lcd defaults, i.e. `hsync_idle_low=0, vsync_idle_low=0, de_idle_high=0` (syncs idle **high** / pulses active low). Verified in `esp_panel_bus_rgb.cpp` `convertPartialToFull()`.
- PCLK edge: the LVGL/board-config path sets `PCLK_ACTIVE_NEG=1` (panel samples on falling edge). The positional-ctor path in 08_DrawColorBar leaves it 0 — the ST7262 tolerates both at this clock; use **1** to match Waveshare's board config.
- ESP-IDF LVGL benchmark note in the wiki mentions PCLK 21 MHz ≈ 51 Hz refresh (26 fps LVGL benchmark single-core). At 16 MHz refresh ≈ 39 Hz (16e6 / (820×500)).
- Resulting ~820 px/line, ~500 lines/frame.

**Arduino_GFX class mapping** — the demos are NOT Arduino_GFX; the matching classes are `Arduino_ESP32RGBPanel` + `Arduino_RGB_Display` (confirmed working by [LCD Wizard's board preset](https://lcdwizard.com/boards/waveshare-s3-7), which uses GFX Library for Arduino). Verified against Arduino_GFX source (`hsync_polarity==1 → hsync_idle_low=0`):

```cpp
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    5 /*DE*/, 3 /*VSYNC*/, 46 /*HSYNC*/, 7 /*PCLK*/,
    /*r0..r4*/ 1, 2, 42, 41, 40,
    /*g0..g5*/ 39, 0, 45, 48, 47, 21,
    /*b0..b4*/ 14, 38, 18, 17, 10,
    /*hsync_polarity*/1, /*hfp*/8, /*hpw*/4, /*hbp*/8,
    /*vsync_polarity*/1, /*vfp*/8, /*vpw*/4, /*vbp*/8,
    /*pclk_active_neg*/1, /*prefer_speed*/16000000L);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, rgbpanel);
```

**Backlight: NOT a GPIO, NOT PWM.** It is CH422G **EXIO2** (net `DISP` → BL_EN of an **AP3032** boost LED driver on the LCD FPC VLED± rails), on/off only, active high. Source: wiki pinout table + [ESP32_Display_Panel board config](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7) (`BACKLIGHT_TYPE = SWITCH_EXPANDER, IO=2, ON_LEVEL=1`) + schematic (U3 AP3032). There is no PWM-capable route to the backlight without rewiring. Also note the panel supply (AVDD/VGH/VGL ~9.85 V via AP3012 + charge pumps) is gated by CH422G **EXIO6** (`LCD_VDD_EN`) — drive it high too. (wiki docs mention these three expander lines are hardware-pulled so the screen may light without driving them; the demos still drive them explicitly.)

### 2. Touch controller

**Goodix GT911** (capacitive, 5-point). Source: [ESP32_Display_Panel board config](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7) (`TOUCH_CONTROLLER GT911`), [LCD Wizard](https://lcdwizard.com/boards/waveshare-s3-7), wiki I2C scan table.

- I2C: **SDA=GPIO8, SCL=GPIO9**, 400 kHz (shared bus with CH422G and the external I2C header).
- Address: **0x5D** (default; 0x14 if INT is high at reset). The board-config reset sequence drives INT low during reset to select 0x5D; wiki scan confirms 0x5d occupied.
- **INT = GPIO4** (direct SoC pin, active low/falling). **RST = CH422G EXIO1** (`TP_RST`, via expander). Reset sequence from board config: INT output low 10 ms → EXIO1 low 100 ms → EXIO1 high → 200 ms → INT back to input.

### 3. IO expander — yes, CH422G

CH422G (16-pin, I2C) on the GPIO8/9 bus. Nominal "address" 0x24 in ESP32_IO_Expander which **ignores it** — CH422G uses fixed command addresses, so the nominal value differs by library (0x24 in `esp_io_expander_ch422g.h`, 0x20 in the ESP32_Display_Panel board conf); the wiki scan table shows rows 0x20–0x27 and 0x30–0x3F occupied. Demos construct `esp_expander::CH422G(scl=9, sda=8, ESP_IO_EXPANDER_I2C_CH422G_ADDRESS)` and call `enableAllIO_Output()`. Assignment (source: wiki pinout tables + `waveshare_lcd_port.h` / `waveshare_sd_card.h` + schematic net names + LCD Wizard):

| CH422G pin | Net | Function |
|---|---|---|
| EXIO1 | TP_RST / CTP_RST | GT911 reset |
| EXIO2 | DISP | Backlight enable (active high) |
| EXIO3 | LCD_RST | Panel reset (pulse low ≥10 ms) |
| EXIO4 | SD_CS | TF card chip-select, active low (demos just hold it low — card permanently selected, SPI bus exclusive to it) |
| EXIO5 | USB_SEL / CAN_SEL | GPIO19/20 routing mux (FSUSB42UMX): **LOW → native USB**, **HIGH → CAN**. State survives MCU reset (expander is separately powered) |
| EXIO6 | LCD_VDD_EN | Panel power enable (drive high) |

EXIO0/EXIO7 and OC0–OC3 appear unused.

### 4. Audio — none

Schematic chip list: ESP32-S3-WROOM-1, CH422G, CH343P, TJA1051 (CAN), SP3485 (RS485), 2× FSUSB42UMX (USB muxes), CS8501 (battery), SGM2212 (LDO), AP3032/AP3012 (backlight/panel power), NDC7002N (I2C level shift). **No audio codec, no I2S amplifier, no speaker/mic header.** (Source: [schematic PDF](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf), wiki feature list.)

### 5. IMU / accelerometer — none

No IMU on board and nothing wired to I2C besides GT911 + CH422G. (Source: schematic, wiki I2C address table.)

### 6. Flash / PSRAM / USB

- **ESP32-S3-WROOM-1-N16R8: 16 MB flash + 8 MB PSRAM.** PSRAM is **octal/OPI at 120 MHz**; flash QIO 120 MHz (wiki ESP-IDF config block: `CONFIG_SPIRAM_MODE_OCT=y`, `SPIRAM_SPEED_120M`, `ESPTOOLPY_FLASHMODE_QIO=y`, `FLASHFREQ_120M`). In Arduino IDE: *PSRAM: "OPI PSRAM"*, *Flash Mode: "QIO 120MHz"*. Caveat: wiki's Specifications table says "8 MB Flash" and the schematic U8 label prints "N8R8" — older/inconsistent docs; Features/Onboard-resources/product page all say 16 MB N16R8.
- **Two USB-C ports:**
  - **"USB" port** = ESP32-S3 native USB-OTG on **GPIO19 (D-) / GPIO20 (D+)**, through the FSUSB42UMX mux (EXIO5 LOW). Used by demos for flashing; enter boot mode (hold BOOT) if the port isn't recognized.
  - **"UART" port** = **CH343P** USB-UART bridge (VIO=3.3 V) with auto-download circuit (DTR/RTS → BOOT/EN transistors) onto UART0: **TX=GPIO43, RX=GPIO44**. A slide switch (SW1, "UART selection") routes UART0 either to the CH343P or to the external UART PH2.0 terminal. (Source: schematic U1/U13 + variant header `TX=43, RX=44` + wiki onboard resources.)

### 7. Battery / power

- **PH2.0 3.7 V single-cell Li-ion connector**, charge + 5 V boost via **CS8501**. Default charge current **580 mA** (set by PROG resistor; schematic table: 1.2 kΩ→580 mA, 0.75 kΩ→900 mA, 0.68 kΩ→1000 mA, 5 kΩ→160 mA — wiki says change R45 to adjust). CHG + DONE LEDs, PWR LED. Recommended ≤2000 mAh cell. (Source: schematic U4/J3 + wiki.)
- System rails: 5 V (USB or battery boost) → SGM2212 3.3 V LDO. I2C header has an NDC7002N level translator with a 3 V/5 V selector (H2).

### 8. Other peripherals / GPIO availability

- **TF (microSD) slot:** SPI — MOSI=**11**, SCK=**12**, MISO=**13**, CS=CH422G **EXIO4** (held low; bus exclusive to the card).
- **RS485:** SP3485 (5 V, auto-direction — no DE pin, "RTCN/RTCP" nets are the auto-direction circuit). **ESP32 TX = GPIO16, RX = GPIO15** per Waveshare's own `02_RS485_Test` demo and LCD Wizard — note the wiki table and the arduino-esp32 variant header print them swapped (RXD=16/TXD=15); pins are 15/16 either way. 120 Ω termination jumper J4 (fitted by default).
- **CAN (TWAI):** TJA1051T/3 (VIO=3.3 V), **TX=GPIO20, RX=GPIO19**, shares those pins with native USB via FSUSB42UMX (EXIO5 HIGH = CAN, takes the native USB port away until EXIO5 returns low — survives reset). 120 Ω termination jumper J5. Demos: 50 kbit/s example config.
- **I2C header (PH2.0):** SDA=8/SCL=9, 3.3 V or 5 V selectable (level shifter). Avoid slave addresses 0x20–0x27, 0x30–0x3F (CH422G), 0x5D (GT911).
- **"Sensor AD" terminal:** **GPIO6** analog input (demo: 12-bit ADC, `analogReadMilliVolts`; header 3V3/AD/GND with divider/protection network).
- **No RTC chip, no audio, no IMU** on board.
- **Free GPIOs: effectively none.** The RGB panel eats 20 pins; touch/I2C 8/9, IRQ 4, SD 11–13, RS485 15/16, USB/CAN 19/20, UART0 43/44, ADC 6 (on the sensor header). GPIO0/45/46 are strapping pins *and* LCD data/sync lines (works because the panel holds them via 10 k pulls at boot, per schematic). External expansion = I2C header + GPIO6 ADC + optional UART header + CAN/RS485 terminals. BOOT and RESET buttons onboard.

## Sources

- Kept: [Waveshare wiki — ESP32-S3-Touch-LCD-7](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7) — pinout tables, CH422G mapping, IDF memory config, I2C address map, battery notes
- Kept: [Schematic PDF (V1.2)](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf) — definitive chip list (CH343P, TJA1051, SP3485, FSUSB42UMX ×2, CS8501, AP3032/AP3012), EXIO6=LCD_VDD_EN, muxes, battery resistor table; confirms absence of audio/IMU/RTC
- Kept: [waveshareteam/ESP32-S3-Touch-LCD-7 GitHub repo](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7) — 08_DrawColorBar (`waveshare_lcd_port.h`: 16 MHz, 4/8/8/4/8/8 timing, data pin order), 03_SD_Test (CH422G EXIO1–5 demo use), 02_RS485_Test (TX16/RX15), 04_Sensor_AD (GPIO6), bundled `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7.h` (GT911 0x5D/400 kHz, PCLK_ACTIVE_NEG=1, backlight SWITCH_EXPANDER EXIO2, expander nominal 0x20)
- Kept: [arduino-esp32 variant pins_arduino.h (3.3.10)](https://github.com/espressif/arduino-esp32/blob/3.3.10/variants/waveshare_esp32_s3_touch_lcd_7/pins_arduino.h) — board is a first-class Arduino core target ("Waveshare ESP32-S3-Touch-LCD-7", USB PID 0x8234), confirms all LCD/touch/CH422G pins, UART0 43/44
- Kept: [LCD Wizard — waveshare-s3-7](https://lcdwizard.com/boards/waveshare-s3-7) — independent community verification incl. Arduino_GFX (GFX Library for Arduino) pin order, EXIO6 panel-enable requirement, USB/CAN mux reset persistence, RS485 direction note
- Kept: [Arduino_ESP32RGBPanel.cpp (Arduino_GFX master)](https://github.com/moononournation/Arduino_GFX/blob/master/src/databus/Arduino_ESP32RGBPanel.cpp) — polarity semantics (`polarity==1` → idle high), fb_in_psram hardcoded, data-pin ordering (B first, then G, R — matches D0..D15 table above), default speed 12 MHz on OPI
- Kept: [ESP32_IO_Expander — esp_io_expander_ch422g.h](https://github.com/esp-arduino-libs/ESP32_IO_Expander/blob/master/src/port/esp_io_expander_ch422g.h) — `ESP_IO_EXPANDER_I2C_CH422G_ADDRESS=0x24` (ignored by chip protocol)
- Kept: [Waveshare docs — Section 07 Driving RGB565 Screen](https://docs.waveshare.com/docs/ESP32/ESP32-S3/ESP32-S3-Touch-LCD-7/Arduino/Arduino-RGB-LCD-Demo) — ST7262 nature, EXIO2 DISP note, code walkthrough
- Dropped: Home Assistant community thread on brightness — anecdotal, suggests GPIO6 PWM rewiring; not authoritative
- Dropped: NuttX board docs — restate wiki table, nothing extra

## Gaps

- **RS485 TX/RX labeling conflicts** between Waveshare's demo (TX=16/RX=15, matches SP3485 DI/RO wiring) and the wiki table / arduino-esp32 variant (TX=15/RX=16). Pins are 15/16 regardless — swap in software if the link is dead.
- **pclk_active_neg** differs between Waveshare's own paths (board config=1, positional-ctor demo=0); both reportedly work. Recommend 1.
- The exact analog **input voltage range** of the Sensor AD divider (LCD Wizard says 0–3.1 V; the 200 kΩ/100 kΩ network suggests a ÷3 divider → ~0–9 V external). Confirm on hardware before scaling.
- Schematic text shows module marked "N8R8" and the wiki spec table "8 MB Flash"; all other sources say N16R8/16 MB. Verify with `esptool.py flash_id` on the actual unit.
- ESP-IDF demo package (separate from the Arduino repo) was not fetched; its LVGL benchmark note (21 MHz) is from the wiki. Not needed for an Arduino_GFX port.

## Port checklist (agent-buddy firmware)

1. Board target: arduino-esp32 ≥3.0.6 has "Waveshare ESP32-S3-Touch-LCD-7"; else ESP32-S3 Dev Module with PSRAM=OPI, Flash=QIO 120 MHz, USB CDC if logging via native USB.
2. Init I2C (SDA=8, SCL=9, 400 k) → CH422G: all outputs; EXIO6=1 (panel VDD), EXIO3 pulse (LCD reset), EXIO2=1 (backlight), EXIO5=0 (keep native USB for the app/daemon link), EXIO4 low only if SD used.
3. `Arduino_ESP32RGBPanel` + `Arduino_RGB_Display` as in the snippet above; framebuffer 800×480×2 = 768 KB from octal PSRAM.
4. Touch: GT911 @0x5D on the same Wire, INT=4 (input), RST=EXIO1 with the GT911 INT-low address-select sequence.
5. Backlight is on/off only — brightness control would need AP3032 FB-circuit mod; do not promise PWM dimming.