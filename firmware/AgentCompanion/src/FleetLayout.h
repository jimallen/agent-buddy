#pragma once
#include <algorithm>
#include <cstdint>

namespace copilot {
// Where each rig's character sits in the fleet view.
//
// The small boards split a square content frame into columns, which is fine at 466 pixels.
// A wide panel (800x480) should instead tile its whole width, so each character stays close to
// full height and all of them read at a similar size. This is the arithmetic for both cases:
// divide the target width by the rig count, then fit the art inside its column without
// distorting it, anchored above the nameplate band.
constexpr int kFleetLabelBand = 26;

struct FleetColumn {
  int x = 0;               // column's left edge in the target frame
  int width = 0;           // column width, including any remainder given to the last column
  int artX = 0;            // scaled character art inside the column
  int artY = 0;
  int artWidth = 0;
  int artHeight = 0;
  int labelY = 0;          // top of the nameplate band
  int labelWidth = 0;      // usable width for the name and harness icon
};

// index < count; artWidth/artHeight are the source art's pixel size.
inline FleetColumn fleetColumn(int index, int count, int targetWidth, int targetHeight,
                               int artWidth, int artHeight) {
  FleetColumn column;
  if (count <= 0 || index < 0 || index >= count || targetWidth <= 0 || targetHeight <= 0
      || artWidth <= 0 || artHeight <= 0) return column;
  const int base = targetWidth / count;
  column.x = base * index;
  // The last column takes the rounding remainder so the columns span the frame exactly.
  column.width = index == count - 1 ? targetWidth - column.x : base;
  const int available = std::max(1, targetHeight - kFleetLabelBand);
  // Fit inside the column, preserving the art's aspect ratio: the smaller of the two scales.
  // Integer maths keeps it identical on device and host.
  const int byWidth = column.width * artHeight / artWidth;   // height if width-bound
  if (byWidth <= available) {
    column.artWidth = column.width;
    column.artHeight = byWidth;
  } else {
    column.artHeight = available;
    column.artWidth = available * artWidth / artHeight;
  }
  column.artWidth = std::max(1, std::min(column.artWidth, column.width));
  column.artHeight = std::max(1, std::min(column.artHeight, available));
  column.artX = column.x + (column.width - column.artWidth) / 2;
  column.artY = available - column.artHeight;   // sit on the nameplate band
  column.labelY = targetHeight - 16;
  column.labelWidth = column.width;
  return column;
}
}
