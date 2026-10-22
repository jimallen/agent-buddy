# Porting to the Waveshare ESP32-S3-Touch-LCD-7

Target: the 7-inch 800x480 RGB board, so the fleet view can give each rig its own
character instead of sharing the one flashed pack.

This page is the working plan. Everything below was checked against Waveshare's
wiki, the board schematic, their demo repo, and `esp-arduino-libs`; the parts not
yet proven on hardware are marked **verify on arrival**.

## Why this board is a different port, not another entry in `boards.json`

The two supported boards push pixels to a small panel over SPI/QSPI: the firmware
renders a frame into a buffer and `DisplayDma` clocks it out. The 7-inch board is
a controller-less RGB panel: the LCD_CAM peripheral scans a framebuffer in PSRAM
continuously, and there is no "send a frame" step at all. That is a different
display path, not a different pin map, which is why it needs its own work.

## Hardware facts

| Item | Value |
| --- | --- |
| Module | ESP32-S3-WROOM-1-N16R8, 16 MB QIO flash + 8 MB **octal** PSRAM |
| Panel | 800x480, RGB565 on 16 data lines, ST7262 class, no init commands |
| Sync/clock | DE 5, VSYNC 3, HSYNC 46, PCLK 7 |
| Data (R0-R4) | 1, 2, 42, 41, 40 |
| Data (G0-G5) | 39, 0, 45, 48, 47, 21 |
| Data (B0-B4) | 14, 38, 18, 17, 10 |
| Timing used by Waveshare | PCLK 16 MHz; HFP 8, HPW 4, HBP 8; VFP 8, VPW 4, VBP 8; pclk_active_neg 1 |
| Touch | Goodix GT911 at 0x5D on I2C (SDA 8, SCL 9, 400 kHz), INT 4, reset on expander |
| IO expander | CH422G on the same I2C bus (see `src/boards/drivers/Ch422g.h`) |
| Audio | **none** |
| IMU | **none** — the orientation features must degrade, not crash |
| SD card | SPI MOSI 11, SCK 12, MISO 13, CS on CH422G EXIO4 |
| Native USB | GPIO19/20 through a mux; CH422G EXIO5 low keeps native USB for the daemon |

## Expander sequencing (done)

`src/boards/drivers/Ch422g.h` implements the chip with a host-tested bus seam:

1. `begin()`: `WR_SET = IO_OE`, `WR_OC = 0`, all levels low.
2. `set(PanelPower, true)` — raise LCD_VDD_EN.
3. `pulsePanelReset()` — drive LCD_RST low, then high.
4. `set(TouchReset, true)`, then the GT911 address-select sequence.
5. `set(Backlight, true)` last, once the panel is configured.

The backlight is a plain enable (AP3032 boost), **not PWM**: there is no dimming
path on this board, so `setDisplayBrightness` becomes on/off.

## Scanout compositor (done)

`src/RgbScanout.h` owns the panel framebuffer and composites our square content into it:
centred, margins black, rotation through `ScreenTransform`, and the display-order to
native-RGB565 conversion in one place. It is host-tested (`tests/test_rgb_scanout.cpp`,
run by `tools/test_device_inputs.sh`) against the real 800x480 geometry, so the centring,
margin and byte-order rules are settled before any hardware exists.

Driving it on the device still needs the panel itself brought up (item 2 below).

## Work items, in order

1. **Board target** — add `waveshare-lcd-70` to `firmware/boards.json` with its own
   define and FQBN, plus `src/boards/waveshare_lcd_70/{BoardConfig.h,BoardDrivers.h}`.
   Do this last, once it compiles, because CI builds every listed board.
2. **RGB scanout** — bring up `Arduino_ESP32RGBPanel` with the framebuffer
   `RgbScanout` composites into, and hand that panel's begin/backlight to the board
   layer. `RgbScanout` is done; what remains is the peripheral setup (timing, pin
   order, `prefer_speed`) and proving the framebuffer byte order on glass.
3. **Touch** — drive the GT911 (`TouchDrvGT911.hpp` ships in the vendored SensorLib)
   with INT 4 and reset on EXIO1.
4. **No IMU** — `OrientationSensor` must report "unavailable" rather than fail;
   rotation then comes only from the settings menu.
5. **No audio** — `audioOutput()` needs a silent implementation on this board.
6. **SD character packs** — the reason for the port: stream packs from the card so a
   rig's harness can have its own character. Needs a pack loader that can read from
   SD, and a `pack` field in the fleet packet (`label:state:agent:pack`).

## Open questions to settle on the bench

- Whether `pclk_active_neg` wants 1 (Waveshare's board config) or 0 (their positional
  constructor example). Both are reported to work; start with 1.
- The GT911 address-select timing (INT low across reset selects 0x5D).
- RS485 TX/RX is labelled 15/16 or 16/15 depending on the source.
- Confirm the flash is really 16 MB (`esptool flash_id`); the schematic silkscreen
  and some docs disagree.


## Verified on hardware (and what it cost to learn)

The board is running this firmware: panel, CH422G sequencing, character pack and render
loop all work. Confirmed by `esptool flash-id` (ESP32-S3 rev v0.2, **16 MB** flash, 8 MB
**OPI** PSRAM) and by `BOARD id=waveshare-lcd-70` in the device's own INFO reply.

### The timing that the vendor actually ships

Read verbatim from `ESP32_Display_Panel`'
s `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7.h`,
not from a summary:

| Setting | Vendor value |
| --- | --- |
| PCLK | 16 MHz |
| HBP / HFP | 8 / 8 |
| VPW / VBP / VFP | 4 / 8 / 8 |
| `PCLK_ACTIVE_NEG` | 1 (falling edge) |
| Bounce buffer | `width * 10` pixels, "used to avoid screen drift for ESP32-S3" |

Sync polarities are **not** overridden by the vendor: esp_lcd's defaults apply, i.e.
`hsync_idle_low = 0` and `vsync_idle_low = 0`. Arduino_GFX expresses the opposite way
round, so both `Arduino_ESP32RGBPanel` polarity arguments must be **1**. Getting that
mapping backwards idles the syncs low and the picture jumps vertically.

Two other traps worth remembering:

- ESP-IDF allocates **two** bounce buffers (ping-pong). Ten lines is 32 KB of internal
  RAM; asking for thirty dropped `free_internal` from 187 KB to 103 KB and would have
  starved Wi-Fi.
- `Arduino_ESP32RGBPanel`'s `useBigEndian` argument changes the data-lane order, not the
  pixel byte order. The framebuffer itself must be **native RGB565** while the renderer's
  frames are big-endian, so `kPanelSwappedBytes` applies the swap on the way in.

### Diagnosing without a camera

The host can read the framebuffer the panel scans (`s`), which separates "what we draw"
from "what the panel shows". Three commands make the panel itself observable:

| Command | Effect |
| --- | --- |
| `k` | Draw a calibration pattern (border, 8 colour bars, ruler every 40 rows) and freeze the loop |
| `f` | Cycle fleet animation rate: 4 / 8 / 15 / 30 fps, then static |
| `s` | Capture; in fleet mode this returns the whole 800x480 panel buffer |

The calibration pattern is the important one: a static pattern that still moves rules out
tearing and points at sync timing, while a pattern that is steady points at our writes.

### Known-good fleet behaviour

The fleet composes straight into the scanned framebuffer in row-sized writes (no staging
copy: staging doubled the work and left the panel reading a half-written buffer), clears
each redraw (the scaled art does not cover its column, so older poses otherwise accumulate
into a smeared second character), and runs its own `CharacterEffects` over a second scratch
pair so the orbiting tokens appear per column. Two columns cost ~41 ms per update, which
caps the loop near 16 fps, so the default animation rate is 8 fps.

## Previewing without the board

There is no usable emulator for this panel: QEMU's ESP32-S3 target and Wokwi both emulate the
CPU and Wi-Fi, but neither emulates LCD_CAM scanning a framebuffer, so the display stays black.

The repo's own host renderer is the substitute, and for the part that matters it is exact: only
`RgbScanout` decides where content lands on the panel, so running it on the host shows the
truth. `tools/rgb_panel_preview.py` builds it and renders a PNG:

```bash
python3 tools/rgb_panel_preview.py --pattern                  # centring and margins
python3 tools/rgb_panel_preview.py --in frame.raw             # a device capture
python3 tools/rgb_panel_preview.py --pattern --angle 90       # rotation
```

Add `--fleet N` to tile N characters across the panel's full width, which is the layout the
fleet should use on the wide board:

```bash
python3 tools/rgb_panel_preview.py --in frame.raw --fleet 3
```

`frame.raw` is the raw RGB565 from a device capture (`CAPTURE` / the `s` command), which is
exactly what the firmware hands the scanout.

`src/FleetLayout.h` is the arithmetic behind that: divide the panel width by the rig count,
then fit each character inside its column without distorting it, anchored above the nameplate.
Two rigs on 800x480 give two ~400px columns with each character about 452px tall, so they read
at nearly the single-character size instead of huddling in the middle of the panel. Verified at 800x480: content at (167, 7), the
412-wide character frame placed at its own offset inside the 466 content, margins black.

## Testing without the board

The expander is host-tested (`tests/test_ch422g.cpp`, run by
`tools/test_device_inputs.sh`). The same seam pattern works for the panel: keep the
Arduino_GFX call behind `displayPanel()` so host tests only see our own types.
