#pragma once
// The RGB panel for this board, created here instead of through Arduino_GFX.
//
// Arduino_GFX's Arduino_ESP32RGBPanel hardcodes num_fbs = 1, so every write lands in the one
// framebuffer the LCD_CAM peripheral is scanning, and the picture tears. Waveshare's own
// ESP32_Display_Panel library shows the supported way out: allocate two framebuffers and switch
// between them with esp_lcd_panel_draw_bitmap() - the call their switchFrameBufferTo() makes.
//
// So the panel is ours: two framebuffers, and the swap lands on a frame boundary. Drawing always
// happens in the back buffer, which the panel is not scanning.
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <cstdint>

namespace copilot {
class RgbPanel {
 public:
  // Argument order matches Arduino_ESP32RGBPanel so the board driver reads the same way.
  RgbPanel(int8_t de, int8_t vsync, int8_t hsync, int8_t pclk,
           int8_t r0, int8_t r1, int8_t r2, int8_t r3, int8_t r4,
           int8_t g0, int8_t g1, int8_t g2, int8_t g3, int8_t g4, int8_t g5,
           int8_t b0, int8_t b1, int8_t b2, int8_t b3, int8_t b4,
           uint16_t hsync_polarity, uint16_t hsync_front_porch, uint16_t hsync_pulse_width,
           uint16_t hsync_back_porch, uint16_t vsync_polarity, uint16_t vsync_front_porch,
           uint16_t vsync_pulse_width, uint16_t vsync_back_porch, uint16_t pclk_active_neg,
           int32_t prefer_speed, bool useBigEndian, uint16_t de_idle_high, uint16_t pclk_idle_high,
           size_t bounce_buffer_size_px, int width = 800, int height = 480)
      : de_(de), vsync_(vsync), hsync_(hsync), pclk_(pclk),
        r_{r0, r1, r2, r3, r4}, g_{g0, g1, g2, g3, g4, g5}, b_{b0, b1, b2, b3, b4},
        hsync_polarity_(hsync_polarity), hsync_front_porch_(hsync_front_porch),
        hsync_pulse_width_(hsync_pulse_width), hsync_back_porch_(hsync_back_porch),
        vsync_polarity_(vsync_polarity), vsync_front_porch_(vsync_front_porch),
        vsync_pulse_width_(vsync_pulse_width), vsync_back_porch_(vsync_back_porch),
        pclk_active_neg_(pclk_active_neg), prefer_speed_(prefer_speed),
        de_idle_high_(de_idle_high), pclk_idle_high_(pclk_idle_high),
        bounce_px_(bounce_buffer_size_px), width_(width), height_(height) {}

  // One framebuffer. Two halved the write window but forced a mirror copy of the whole panel on
  // every present, which is more PSRAM traffic than the panel's own scan can spare - that copy is
  // what starved the scan and froze the picture. Presenting only the changed region is the cheaper
  // way to keep the scan fed.
  static constexpr int kFrameBuffers = 1;

  bool begin(int32_t speed = -1) {
    error_ = nullptr;
    if (handle_) return true;
    esp_lcd_rgb_panel_config_t config = {};
    config.clk_src = LCD_CLK_SRC_DEFAULT;
    config.timings.pclk_hz = static_cast<uint32_t>(speed > 0 ? speed : prefer_speed_);
    config.timings.h_res = static_cast<uint32_t>(width_);
    config.timings.v_res = static_cast<uint32_t>(height_);
    config.timings.hsync_pulse_width = hsync_pulse_width_;
    config.timings.hsync_back_porch = hsync_back_porch_;
    config.timings.hsync_front_porch = hsync_front_porch_;
    config.timings.vsync_pulse_width = vsync_pulse_width_;
    config.timings.vsync_back_porch = vsync_back_porch_;
    config.timings.vsync_front_porch = vsync_front_porch_;
    config.timings.flags.hsync_idle_low = (hsync_polarity_ == 0) ? 1 : 0;
    config.timings.flags.vsync_idle_low = (vsync_polarity_ == 0) ? 1 : 0;
    config.timings.flags.de_idle_high = de_idle_high_;
    config.timings.flags.pclk_active_neg = pclk_active_neg_;
    config.timings.flags.pclk_idle_high = pclk_idle_high_;
    config.data_width = 16;
    config.bits_per_pixel = 16;
    config.num_fbs = kFrameBuffers;
    config.bounce_buffer_size_px = bounce_px_;
    config.sram_trans_align = 4;
    config.psram_trans_align = 64;
    config.hsync_gpio_num = hsync_;
    config.vsync_gpio_num = vsync_;
    config.de_gpio_num = de_;
    config.pclk_gpio_num = pclk_;
    config.disp_gpio_num = -1;
    // Same data-line order Arduino_GFX used for this board (not the big-endian permutation).
    const int8_t data[16] = {b_[0], b_[1], b_[2], b_[3], b_[4], g_[0], g_[1], g_[2],
                             g_[3], g_[4], g_[5], r_[0], r_[1], r_[2], r_[3], r_[4]};
    for (int index = 0; index < 16; ++index) config.data_gpio_nums[index] = data[index];
    config.flags.disp_active_low = true;
    config.flags.fb_in_psram = true;


    const esp_err_t created = esp_lcd_new_rgb_panel(&config, &handle_);
    if (created != ESP_OK) {
      handle_ = nullptr;
      error_ = "The RGB panel could not be created.";
      return false;
    }
    if (esp_lcd_panel_reset(handle_) != ESP_OK || esp_lcd_panel_init(handle_) != ESP_OK) {
      error_ = "The RGB panel did not start.";
      return false;
    }
    void* buffers[2] = {nullptr, nullptr};
    if (esp_lcd_rgb_panel_get_frame_buffer(handle_, kFrameBuffers, &buffers[0], &buffers[1]) != ESP_OK
        || !buffers[0]) {
      error_ = "The RGB panel reported no framebuffer.";
      return false;
    }
    for (int index = 0; index < kFrameBuffers; ++index) {
      fb_[index] = static_cast<uint16_t*>(buffers[index]);
    }
    return true;
  }

  uint16_t* framebuffer(int index) const { return fb_[index & 1]; }

  // Hand the panel a finished buffer. This is the call Waveshare's switchFrameBufferTo() makes; the
  // swap takes effect at a frame boundary, so a half-painted buffer is never scanned.
  bool present(int index) {
    return handle_ && esp_lcd_panel_draw_bitmap(handle_, 0, 0, width_, height_,
                                                fb_[index & 1]) == ESP_OK;
  }

  const char* error() const { return error_; }

 private:
  uint16_t* fb_[2] = {nullptr, nullptr};
  esp_lcd_panel_handle_t handle_ = nullptr;
  int8_t de_, vsync_, hsync_, pclk_;
  int8_t r_[5], g_[6], b_[5];
  uint16_t hsync_polarity_, hsync_front_porch_, hsync_pulse_width_, hsync_back_porch_;
  uint16_t vsync_polarity_, vsync_front_porch_, vsync_pulse_width_, vsync_back_porch_;
  uint16_t pclk_active_neg_;
  int32_t prefer_speed_;
  uint16_t de_idle_high_, pclk_idle_high_;
  size_t bounce_px_;
  int width_, height_;
  const char* error_ = nullptr;
};
}
