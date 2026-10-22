// Renders what the seven-inch RGB panel will show, using the firmware's own scanout code.
//
// The host cannot emulate LCD_CAM, but it does not need to: RgbScanout is the only thing that
// decides where content lands on the panel, so running the real class here shows the truth.
//
// Input is a raw RGB565 frame (the same bytes a device capture writes) placed at (x, y) inside
// the square content area. Output is a binary PPM that tools/rgb_panel_preview.py turns into a
// PNG. With no input, a test pattern is generated so the centring and margins are visible.
#include "../firmware/AgentCompanion/src/FleetLayout.h"
#include "../firmware/AgentCompanion/src/RgbScanout.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace copilot;

namespace {
struct Options {
  std::string input;
  std::string output = "rgb-panel.ppm";
  std::string order = "display";
  int width = 412, height = 466, x = 27, y = 0;
  int content = 466, panelWidth = 800, panelHeight = 480;
  float angleDegrees = 0.0f;
  bool pattern = false;
  int fleet = 1;
};

[[noreturn]] void usage(const char* why) {
  std::fprintf(stderr,
      "rgb-panel-preview: %s\n"
      "  --in <raw RGB565> | --pattern\n"
      "  [--width N] [--height N] [--x N] [--y N]      frame size and where it sits in content\n"
      "  [--content N] [--panel-width N] [--panel-height N]\n"
      "  [--order display|native] [--angle DEGREES] [--out FILE.ppm]\n",
      why);
  std::exit(2);
}
}

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    auto value = [&]() -> const char* {
      if (index + 1 >= argc) usage(("missing value for " + flag).c_str());
      return argv[++index];
    };
    if (flag == "--in") options.input = value();
    else if (flag == "--pattern") options.pattern = true;
    else if (flag == "--fleet") options.fleet = std::atoi(value());
    else if (flag == "--width") options.width = std::atoi(value());
    else if (flag == "--height") options.height = std::atoi(value());
    else if (flag == "--x") options.x = std::atoi(value());
    else if (flag == "--y") options.y = std::atoi(value());
    else if (flag == "--content") options.content = std::atoi(value());
    else if (flag == "--panel-width") options.panelWidth = std::atoi(value());
    else if (flag == "--panel-height") options.panelHeight = std::atoi(value());
    else if (flag == "--order") options.order = value();
    else if (flag == "--angle") options.angleDegrees = static_cast<float>(std::atof(value()));
    else if (flag == "--out") options.output = value();
    else usage(("unknown option " + flag).c_str());
  }

  std::vector<uint16_t> content(static_cast<size_t>(options.content) * options.content, 0);
  std::vector<uint16_t> frame(static_cast<size_t>(options.width) * options.height, 0);
  if (options.pattern) {
    // Colour bars, a 1px border around the frame area, and a marker in the top-left corner.
    for (int y = 0; y < options.height; ++y) {
      for (int x = 0; x < options.width; ++x) {
        uint16_t value = 0;
        if (x < 2 || y < 2 || x >= options.width - 2 || y >= options.height - 2) value = 0xFFFF;
        else value = static_cast<uint16_t>(((x * 8) / options.width) * 0x0841);
        frame[static_cast<size_t>(y) * options.width + x] = value;
      }
    }
    for (int y = 0; y < 12; ++y)
      for (int x = 0; x < 12; ++x) frame[static_cast<size_t>(y) * options.width + x] = 0xF800;
  } else {
    if (options.input.empty()) usage("need --in <raw RGB565> or --pattern");
    std::FILE* in = std::fopen(options.input.c_str(), "rb");
    if (!in) { std::fprintf(stderr, "cannot open %s\n", options.input.c_str()); return 1; }
    const size_t want = frame.size() * sizeof(uint16_t);
    const size_t got = std::fread(frame.data(), 1, want, in);
    std::fclose(in);
    if (got != want) {
      std::fprintf(stderr, "%s holds %zu bytes, need %zu for %dx%d\n",
                   options.input.c_str(), got, want, options.width, options.height);
      return 1;
    }
  }

  std::vector<uint16_t> panel(static_cast<size_t>(options.panelWidth) * options.panelHeight, 0);
  if (options.fleet > 1) {
    // One column per rig across the panel's full width; each character keeps its aspect, so
    // they all read at a similar size instead of huddling in the middle.
    for (int index = 0; index < options.fleet; ++index) {
      const FleetColumn column = fleetColumn(index, options.fleet, options.panelWidth,
                                             options.panelHeight, options.width, options.height);
      if (column.width <= 0) { std::fprintf(stderr, "bad fleet layout\n"); return 1; }
      for (int y = 0; y < column.artHeight; ++y) {
        const int sourceY = y * options.height / column.artHeight;
        for (int x = 0; x < column.artWidth; ++x) {
          const int sourceX = x * options.width / column.artWidth;
          uint16_t pixel = frame[static_cast<size_t>(sourceY) * options.width + sourceX];
          // The panel framebuffer is native RGB565; a device capture is in display order.
          if (options.order != "native") pixel = swapBytes(pixel);
          panel[static_cast<size_t>(column.artY + y) * options.panelWidth + column.artX + x] = pixel;
        }
      }
    }
    std::printf("%dx%d panel, %d column(s) of art %dx%d\n",
                options.panelWidth, options.panelHeight, options.fleet,
                options.width, options.height);
  } else {
    // Single-character view: the firmware's own centring and byte-order rules.
    std::vector<uint16_t> scratch(static_cast<size_t>(options.content) * options.content, 0);
    RgbScanout scanout(panel.data(), options.panelWidth, options.panelHeight, options.content,
                       scratch.data());
    if (!scanout.begin()) { std::fprintf(stderr, "%s\n", scanout.error()); return 1; }
    if (std::fabs(options.angleDegrees) > 0.001f)
      scanout.setAngle(options.angleDegrees * 3.14159265358979f / 180.0f);
    const Rgb565Order order = options.order == "native" ? Rgb565Order::Native : Rgb565Order::Display;
    if (!scanout.compose({frame.data(), options.x, options.y, options.width, options.height, order})) {
      std::fprintf(stderr, "%s\n", scanout.error());
      return 1;
    }
    std::printf("%dx%d panel, content %d at (%d, %d)\n",
                options.panelWidth, options.panelHeight, options.content,
                scanout.contentX(), scanout.contentY());
  }

  std::FILE* out = std::fopen(options.output.c_str(), "wb");
  if (!out) { std::fprintf(stderr, "cannot write %s\n", options.output.c_str()); return 1; }
  std::fprintf(out, "P6\n%d %d\n255\n", options.panelWidth, options.panelHeight);
  std::vector<uint8_t> rgb(static_cast<size_t>(options.panelWidth) * options.panelHeight * 3);
  for (size_t index = 0; index < panel.size(); ++index) {
    const uint16_t pixel = panel[index];
    const unsigned r = ((pixel >> 11) & 0x1F) * 255 / 31;
    const unsigned g = ((pixel >> 5) & 0x3F) * 255 / 63;
    const unsigned b = (pixel & 0x1F) * 255 / 31;
    rgb[index * 3 + 0] = static_cast<uint8_t>(r);
    rgb[index * 3 + 1] = static_cast<uint8_t>(g);
    rgb[index * 3 + 2] = static_cast<uint8_t>(b);
  }
  std::fwrite(rgb.data(), 1, rgb.size(), out);
  std::fclose(out);
  return 0;
}
