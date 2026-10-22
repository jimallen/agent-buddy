#include "../firmware/AgentCompanion/src/FleetLayout.h"
#include <cassert>
#include <iostream>

using namespace copilot;

int main() {
  // Art is the device's 412x466 character frame.
  constexpr int kArtWidth = 412, kArtHeight = 466;

  // Two rigs on the 800x480 panel: each column is half the panel, and each character keeps
  // nearly full height, so they read at a similar size to the single-character view.
  {
    const FleetColumn left = fleetColumn(0, 2, 800, 480, kArtWidth, kArtHeight);
    const FleetColumn right = fleetColumn(1, 2, 800, 480, kArtWidth, kArtHeight);
    assert(left.x == 0 && left.width == 400);
    assert(right.x == 400 && right.width == 400);
    assert(left.artHeight == 400 * kArtHeight / kArtWidth);   // width-bound
    assert(left.artWidth == 400);
    assert(left.artHeight > 400);                             // close to full height
    assert(left.artX == 0);
    assert(left.artY == (480 - kFleetLabelBand) - left.artHeight);
    assert(right.artX == 400);
  }

  // Three rigs: narrower columns, same rules, and the columns still tile the panel exactly.
  {
    const FleetColumn first = fleetColumn(0, 3, 800, 480, kArtWidth, kArtHeight);
    const FleetColumn last = fleetColumn(2, 3, 800, 480, kArtWidth, kArtHeight);
    assert(first.x == 0);
    assert(first.width == 266);
    // 800/3 = 266, so the last column takes the remaining 268.
    assert(last.x == 532 && last.width == 268);
    assert(first.artWidth <= first.width);
    assert(first.artHeight <= 480 - kFleetLabelBand);
    // The columns tile the panel exactly, with no gap and no overlap.
    int cursor = 0;
    for (int index = 0; index < 3; ++index) {
      const FleetColumn column = fleetColumn(index, 3, 800, 480, kArtWidth, kArtHeight);
      assert(column.x == cursor);
      cursor += column.width;
    }
    assert(cursor == 800);
  }

  // A narrow column is width-bound: the art fills the width and keeps its aspect.
  {
    const FleetColumn column = fleetColumn(0, 1, 200, 800, kArtWidth, kArtHeight);
    assert(column.artWidth == 200);
    assert(column.artHeight == 200 * kArtHeight / kArtWidth);
    assert(column.artX == 0);
  }

  // A short, wide frame is height-bound: the art fills the height instead.
  {
    const FleetColumn column = fleetColumn(0, 1, 800, 200, kArtWidth, kArtHeight);
    const int available = 200 - kFleetLabelBand;
    assert(column.artHeight == available);
    assert(column.artWidth == available * kArtWidth / kArtHeight);
    assert(column.artX == (800 - column.artWidth) / 2);
    assert(column.artY == 0);
  }

  // One rig in the small boards' square content frame: the nameplate band makes it
  // height-bound, so the art shrinks slightly and stays centred.
  {
    const FleetColumn column = fleetColumn(0, 1, 412, 466, kArtWidth, kArtHeight);
    assert(column.x == 0 && column.width == 412);
    const int available = 466 - kFleetLabelBand;
    assert(column.artHeight == available);
    assert(column.artWidth == available * kArtWidth / kArtHeight);
    assert(column.artX == (412 - column.artWidth) / 2);
    assert(column.artY == 0);
    assert(column.artY + column.artHeight == available);
  }

  // Nothing is drawn for nonsensical input rather than dividing by zero.
  {
    const FleetColumn none = fleetColumn(0, 0, 800, 480, kArtWidth, kArtHeight);
    assert(none.width == 0 && none.artWidth == 0);
    const FleetColumn outOfRange = fleetColumn(5, 2, 800, 480, kArtWidth, kArtHeight);
    assert(outOfRange.width == 0);
    const FleetColumn noArt = fleetColumn(0, 1, 800, 480, 0, 0);
    assert(noArt.width == 0);
  }

  std::cout << "fleet layout ok\n";
  return 0;
}
