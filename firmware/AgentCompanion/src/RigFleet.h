#pragma once
#include "CharacterMotion.h"
#include <cstddef>
#include <cstdint>
#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace copilot {
// The fleet view: one column per rig. A daemon with OpenRig following enabled sends one
// packet (=label:state,...) with every rig's chief-of-staff state; an empty packet
// returns the device to the single-character view.
constexpr size_t kMaxRigSlots = 4;
constexpr size_t kRigLabelMax = 12;
// The agent runtime behind each rig's chief of staff (claude, codex, pi, …), as the daemon
// names it; empty when the daemon did not say. The renderer shows that agent's badge icon.
constexpr size_t kRigAgentMax = 16;

// The wire letters for rig states; modeForRigState is the single mapping, the daemon
// sends the same letters.
enum class RigStateLetter : char {
  Idle = 'i', Working = 'w', Complete = 'c', Attention = 'a', Surprise = 's', Away = '-',
};

CharacterMode modeForRigState(char letter);
bool isRigStateLetter(char letter);

// The fleet compositor, host-testable. blitRigColumn scales the 412-wide character art
// into one fleet column of the frame (nearest neighbor; fullBrightness false halves each
// channel); drawRigLabel writes a lowercase 5x7 label into the frame, one pixel cell per
// set bit. Unknown characters skip a cell rather than failing.
void blitRigColumn(const uint16_t* art, int artWidth, int artHeight, uint16_t* frame,
                   int frameWidth, int x0, int y0, int width, int height, int fullBrightness,
                   bool swapBytes = false);
// Writes are clipped to the frame; an over-long label is truncated.
void drawRigLabel(uint16_t* frame, int frameWidth, int frameHeight, int x, int y,
                  const char* label, uint16_t color);

struct RigSlot {
  char label[kRigLabelMax + 1] = {};
  char agent[kRigAgentMax + 1] = {};
  CharacterMode mode = CharacterMode::Idle;
  bool present = false;
  bool used = false;
};

struct RigFleetSnapshot {
  RigSlot slots[kMaxRigSlots] = {};
  uint8_t count = 0;
  uint32_t revision = 0;
  // Bumps only when the label set or order changes; a state change alone leaves it,
  // so the renderer retargets the motions instead of repainting the layout.
  uint32_t layoutRevision = 0;
};

class RigFleet {
 public:
  // The packet payload after the '=' prefix; an empty payload clears the fleet.
  bool setPacket(const char* payload);
  RigFleetSnapshot snapshot() const;
  uint8_t count() const { return count_; }
  bool fleetMode() const { return count_ > 0; }
  const char* error() const { return error_; }
#if defined(ARDUINO)
  using Lock = portMUX_TYPE;
#else
  struct Lock {};
#endif

 private:
  void fail(const char* message) { error_ = message; }

  RigSlot slots_[kMaxRigSlots] = {};
  uint8_t count_ = 0;
  uint32_t revision_ = 0;
  uint32_t layoutRevision_ = 0;
  const char* error_ = nullptr;
#if defined(ARDUINO)
  mutable Lock lock_ = portMUX_INITIALIZER_UNLOCKED;
#else
  mutable Lock lock_;
#endif
};
}
