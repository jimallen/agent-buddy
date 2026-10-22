#include "../firmware/AgentCompanion/src/RigFleet.h"
#include "../firmware/AgentCompanion/src/Rgb565.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace copilot;

int main() {
  RigFleet fleet;

  // Empty means off: no characters, no columns.
  assert(fleet.count() == 0);
  assert(!fleet.fleetMode());

  // One packet defines the whole fleet view; states use the mode letters, '-' is away.
  assert(fleet.setPacket("eztrack:w,jims-team:i"));
  auto snap = fleet.snapshot();
  assert(snap.count == 2);
  assert(std::strcmp(snap.slots[0].label, "eztrack") == 0);
  assert(snap.slots[0].mode == CharacterMode::Working);
  assert(snap.slots[0].present);
  assert(std::strcmp(snap.slots[1].label, "jims-team") == 0);
  assert(snap.slots[1].mode == CharacterMode::Idle);
  assert(snap.slots[1].present);
  assert(fleet.fleetMode());

  // An away rig is dimmed but still gets a column.
  assert(fleet.setPacket("eztrack:w,away-rig:-"));
  snap = fleet.snapshot();
  assert(snap.count == 2);
  assert(!snap.slots[1].present);
  assert(snap.slots[1].mode == CharacterMode::Idle);

  // Every mode letter round-trips.
  for (const char* state : {"i", "w", "c", "a", "s"}) {
    std::string packet = std::string("one:") + state;
    assert(fleet.setPacket(packet.c_str()));
    assert(fleet.snapshot().slots[0].mode == modeForRigState(state[0]));
  }

  // A new packet replaces the fleet, and it reports a bump.
  const uint32_t revision = fleet.snapshot().revision;
  assert(fleet.setPacket("other:c"));
  snap = fleet.snapshot();
  assert(snap.count == 1);
  assert(std::strcmp(snap.slots[0].label, "other") == 0);
  assert(snap.revision > revision);

  // The daemon names the chief-of-staff runtime as an optional third field, so each column can
  // show that agent's icon (the flashed character art is one pack for every rig).
  assert(fleet.setPacket("eztrack:w:claude,jims-team:-:pi,third:i:codex"));
  snap = fleet.snapshot();
  assert(snap.count == 3);
  assert(std::strcmp(snap.slots[0].agent, "claude") == 0);
  assert(std::strcmp(snap.slots[1].agent, "pi") == 0);
  assert(std::strcmp(snap.slots[2].agent, "codex") == 0);

  // The field is optional: a two-field entry leaves the agent empty rather than failing.
  assert(fleet.setPacket("eztrack:w,jims-team:i:pi"));
  snap = fleet.snapshot();
  assert(snap.slots[0].agent[0] == '\0');
  assert(std::strcmp(snap.slots[1].agent, "pi") == 0);

  // A malformed agent field is refused whole, like every other bad packet.
  assert(!fleet.setPacket("eztrack:w:"));
  assert(!fleet.setPacket("eztrack:w:has_underscore"));
  assert(!fleet.setPacket("eztrack:w:waaaaay-too-long-agent"));
  assert(fleet.snapshot().count == 2);

  // Back to the plain two-field packets the rest of this test uses.
  assert(fleet.setPacket("one:w"));

  // Bad packets are refused whole and keep the previous view.
  for (const char* bad : {"nocolon", ":w", "one:x", "one:w,gap::w", "toolonglabelnam:w"}) {
    assert(!fleet.setPacket(bad));
    assert(fleet.error() != nullptr);
    assert(fleet.snapshot().count == 1);
  }

  // The fourth+ rig does not fit.
  assert(!fleet.setPacket("a:w,b:w,c:w,d:w,e:w"));
  assert(fleet.snapshot().count == 1);

  // An empty packet leaves fleet mode.
  assert(fleet.setPacket(""));
  assert(fleet.count() == 0);
  assert(!fleet.fleetMode());

  // The column blit maps the character art into a column and keeps the label band clear.
  // Source: 8x4 art where every pixel encodes its row (row r → value r+1).
  uint16_t art[8 * 4];
  for (size_t px = 0; px < 8 * 4; ++px) art[px] = static_cast<uint16_t>(px / 8 + 1);
  uint16_t frame[16 * 16];
  std::memset(frame, 0, sizeof(frame));
  // Two columns, each 8 wide; art downscales 2x2 → into the top 8 rows of the column.
  blitRigColumn(art, 8, 4, frame, 16, 0, 0, 8, 8, 1);
  assert(frame[0] == 1 && frame[7] == 1);           // row 0: sampled from art row 0
  assert(frame[6 * 16] == 4 && frame[7 * 16] == 4); // bottom of column reaches art row 3
  blitRigColumn(art, 8, 4, frame, 16, 8, 0, 8, 8, 1);
  assert(frame[8 + 1] == 1);
  assert(frame[7 * 16 + 8 + 1] == 4);               // second column writes its own span
  assert(frame[7 * 16 + 7] == 4);                   // first column stays

  // Dimmed columns halve each RGB channel of packed 565 values (big-endian in frame memory).
  uint16_t dimmed[2 * 2] = {0};
  const uint16_t solid[2 * 2] = {swapBytes(rgb565(200, 120, 40)), swapBytes(rgb565(200, 120, 40)),
                                 swapBytes(rgb565(200, 120, 40)), swapBytes(rgb565(200, 120, 40))};
  blitRigColumn(solid, 2, 2, dimmed, 2, 0, 0, 2, 2, 0);
  assert(swapBytes(dimmed[0]) == rgb565(100, 60, 20));

  // Labels draw 5x7 lowercase; unknown characters skip a cell instead of aborting.
  uint16_t label[64 * 16];
  std::memset(label, 0, sizeof(label));
  drawRigLabel(label, 64, 16, 4, 4, "eztrack", 0x07E0);
  bool litTop = false;
  for (int x = 4; x < 4 + 6 * 6; ++x) litTop = litTop || label[4 * 64 + x] == 0x07E0;
  assert(litTop);
  drawRigLabel(label, 64, 16, 4, 12, "UP_per_case", 0x07E0);  // uppercase/unknown: skipped, no crash
  std::cout << "rig fleet ok\n";
  return 0;
}
