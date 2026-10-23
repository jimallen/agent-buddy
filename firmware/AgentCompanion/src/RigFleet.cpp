#include "RigFleet.h"
#include "Rgb565.h"

namespace { inline uint16_t swapBytesValue(uint16_t v) { return copilot::swapBytes(v); } }
#include <cstring>

namespace copilot {
namespace {
// Holds the fleet lock for one scope. Host builds are single-threaded, so it does nothing there.
class CriticalSection {
 public:
#if defined(ARDUINO)
  explicit CriticalSection(portMUX_TYPE& lock) : lock_(lock) { portENTER_CRITICAL(&lock_); }
  ~CriticalSection() { portEXIT_CRITICAL(&lock_); }
#else
  explicit CriticalSection(const RigFleet::Lock&) {}
#endif
  CriticalSection(const CriticalSection&) = delete;
  CriticalSection& operator=(const CriticalSection&) = delete;

 private:
#if defined(ARDUINO)
  portMUX_TYPE& lock_;
#endif
};

bool validLabelChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

bool validAgentChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

// The optional third field of a rig entry: the agent runtime behind its chief of staff.
bool parseAgent(const char*& cursor, char* agent) {
  size_t length = 0;
  while (validAgentChar(*cursor)) {
    if (length == kRigAgentMax) return false;
    agent[length++] = *cursor++;
  }
  if (length == 0) return false;
  agent[length] = '\0';
  return true;
}

bool parseLabel(const char*& cursor, char* label) {
  size_t length = 0;
  while (validLabelChar(*cursor)) {
    if (length == kRigLabelMax) return false;
    label[length++] = *cursor++;
  }
  if (length == 0) return false;
  label[length] = '\0';
  return true;
}
}

CharacterMode modeForRigState(char letter) {
  switch (letter) {
    case 'w': return CharacterMode::Working;
    case 'c': return CharacterMode::Complete;
    case 'a': return CharacterMode::Attention;
    case 's': return CharacterMode::Surprise;
    default: return CharacterMode::Idle;
  }
}

bool isRigStateLetter(char letter) {
  return letter == 'i' || letter == 'w' || letter == 'c' || letter == 'a' || letter == 's'
      || letter == static_cast<char>(RigStateLetter::Away);
}

bool RigFleet::setPacket(const char* payload) {
  error_ = nullptr;
  if (!payload) {
    fail("Rig fleet packet is missing");
    return false;
  }
  RigSlot slots[kMaxRigSlots] = {};
  size_t count = 0;
  const char* cursor = payload;
  while (*cursor) {
    if (count == kMaxRigSlots) {
      fail("Rig fleet packet exceeds 4 rigs");
      return false;
    }
    RigSlot slot;
    if (!parseLabel(cursor, slot.label)) {
      fail("Rig label is missing or too long");
      return false;
    }
    if (*cursor != ':') {
      fail("Rig fleet packet needs label:state");
      return false;
    }
    ++cursor;
    if (!isRigStateLetter(*cursor)) {
      fail("Rig state must be one letter");
      return false;
    }
    const char letter = *cursor++;
    slot.present = letter != static_cast<char>(RigStateLetter::Away);
    slot.mode = modeForRigState(letter);
    // label:state, or label:state:agent when the daemon names the chief of staff's runtime.
    if (*cursor == ':') {
      ++cursor;
      if (!parseAgent(cursor, slot.agent)) {
        fail("Rig agent is missing or too long");
        return false;
      }
    }
    slot.used = true;
    slots[count++] = slot;
    if (*cursor == ',') {
      ++cursor;
      if (!*cursor) {
        fail("Rig fleet packet ends early");
        return false;
      }
    } else if (*cursor) {
      fail("Rig fleet packet has an unexpected character");
      return false;
    }
  }
  bool layoutChanged = count != count_;
  for (size_t index = 0; !layoutChanged && index < count; ++index)
    layoutChanged = std::strcmp(slots[index].label, slots_[index].label) != 0;
  CriticalSection guard(lock_);
  std::memcpy(slots_, slots, sizeof(slots_));
  count_ = static_cast<uint8_t>(count);
  ++revision_;
  if (layoutChanged) ++layoutRevision_;
  return true;
}

RigFleetSnapshot RigFleet::snapshot() const {
  CriticalSection guard(lock_);
  RigFleetSnapshot snap;
  for (size_t index = 0; index < kMaxRigSlots; ++index) {
    snap.slots[index] = slots_[index];
    // The label array stays printable even if a packet race truncated it.
    snap.slots[index].label[kRigLabelMax] = '\0';
  }
  snap.count = count_;
  snap.revision = revision_;
  snap.layoutRevision = layoutRevision_;
  return snap;
}

namespace {
// 5x7 lowercase glyphs, bit 4 the left column, for rig nameplates. Digits and the label
// punctuation the label sanitizer keeps ('-', '.') come along.
struct RigGlyph {
  char character;
  uint8_t rows[7];
};
constexpr RigGlyph kGlyphs[] = {
    {'a', {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}}, {'b', {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E}},
    {'c', {0x00, 0x00, 0x0E, 0x11, 0x10, 0x11, 0x0E}}, {'d', {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F}},
    {'e', {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E}}, {'f', {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08}},
    {'g', {0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E}}, {'h', {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11}},
    {'i', {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}}, {'j', {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}},
    {'k', {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}}, {'l', {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'m', {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15}}, {'n', {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}},
    {'o', {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}}, {'p', {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10}},
    {'q', {0x00, 0x00, 0x0D, 0x13, 0x0F, 0x01, 0x01}}, {'r', {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}},
    {'s', {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E}}, {'t', {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06}},
    {'u', {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}}, {'v', {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'w', {0x00, 0x00, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'x', {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}},
    {'y', {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E}}, {'z', {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}}, {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
};

const uint8_t* rigGlyph(char character) {
  for (const RigGlyph& glyph : kGlyphs)
    if (glyph.character == character) return glyph.rows;
  return nullptr;
}

// Half-strength packed RGB565, so away rigs stay visible but clearly dimmed. Frame memory
// is stored big-endian (the pack encoding is word-up-be), so unswap, halve, re-swap.
uint16_t dimRigPixel(uint16_t pixel) {
  const uint16_t value = swapBytes(pixel);
  const uint16_t r = (value >> 11) & 0x1F, g = (value >> 5) & 0x3F, b = value & 0x1F;
  return swapBytes(static_cast<uint16_t>((r >> 1) << 11 | (g >> 1) << 5 | (b >> 1)));
}

// Same dimming for art already in the panel's own byte order, as the fleet's pre-swapped frames
// are: swapping here as well would halve the wrong channel bits.
uint16_t dimRigPixelNative(uint16_t value) {
  const uint16_t r = (value >> 11) & 0x1F, g = (value >> 5) & 0x3F, b = value & 0x1F;
  return static_cast<uint16_t>((r >> 1) << 11 | (g >> 1) << 5 | (b >> 1));
}
}

void blitRigColumn(const uint16_t* art, int artWidth, int artHeight, uint16_t* frame,
                   int frameWidth, int x0, int y0, int width, int height, int fullBrightness,
                   bool swapBytes) {
  // Nearest-neighbour scaling by integer accumulation instead of a division per pixel: the
  // mapping is identical to floor(index * source / target), but one column of 400x454 costs
  // 360k divisions otherwise, which alone was several milliseconds of every frame.
  int sourceY = 0, rowError = 0;
  for (int y = 0; y < height; ++y) {
    const uint16_t* row = art + static_cast<size_t>(sourceY) * artWidth;
    uint16_t* out = frame + static_cast<size_t>(y0 + y) * frameWidth + x0;
    int sourceX = 0, columnError = 0;
    for (int x = 0; x < width; ++x) {
      uint16_t pixel = row[sourceX];
      if (swapBytes) pixel = swapBytesValue(pixel);
      out[x] = fullBrightness ? pixel : dimRigPixel(pixel);
      columnError += artWidth;
      while (columnError >= width) {
        columnError -= width;
        ++sourceX;
      }
    }
    rowError += artHeight;
    while (rowError >= height) {
      rowError -= height;
      ++sourceY;
    }
  }
}

void blitRigColumnNative(const uint16_t* art, int artWidth, int artHeight, uint16_t* frame,
                          int frameWidth, int x0, int y0, int width, int height,
                          int fullBrightness, int yieldRows, void (*yield)()) {
  int sourceY = 0, rowError = 0;
  for (int y = 0; y < height; ++y) {
    // Give the panel's own framebuffer fetch a window every few rows. Writing the scanned buffer
    // in one unbroken burst starves it, and the picture jumps.
    if (yield && yieldRows > 0 && y > 0 && y % yieldRows == 0) yield();
    const uint16_t* row = art + static_cast<size_t>(sourceY) * artWidth;
    uint16_t* out = frame + static_cast<size_t>(y0 + y) * frameWidth + x0;
    int sourceX = 0, columnError = 0;
    for (int x = 0; x < width; ++x) {
      const uint16_t pixel = row[sourceX];
      out[x] = fullBrightness ? pixel : dimRigPixelNative(pixel);
      columnError += artWidth;
      while (columnError >= width) {
        columnError -= width;
        ++sourceX;
      }
    }
    rowError += artHeight;
    while (rowError >= height) {
      rowError -= height;
      ++sourceY;
    }
  }
}

void drawRigLabel(uint16_t* frame, int frameWidth, int frameHeight, int x, int y,
                  const char* label, uint16_t color) {
  int cursor = x;
  for (const char* text = label; *text && cursor + 4 < frameWidth; ++text) {
    const uint8_t* rows = rigGlyph(*text);
    if (rows) {
      for (int row = 0; row < 7; ++row) {
        if (y + row >= frameHeight) break;
        for (int column = 0; column < 5; ++column)
          if (rows[row] & (0x10 >> column)) frame[(y + row) * frameWidth + cursor + column] = color;
      }
    }
    cursor += 6;
  }
}
}
