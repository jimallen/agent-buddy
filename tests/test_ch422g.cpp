#include "../firmware/AgentCompanion/src/boards/drivers/Ch422g.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace copilot;

namespace {
// Records every byte the driver puts on the bus, and can be told to fail.
struct FakeBus : Ch422gBus {
  struct Write {
    uint8_t command;
    uint8_t value;
  };
  std::vector<Write> writes;
  uint8_t latch = 0;
  bool failWrites = false;
  bool failReads = false;

  bool write(uint8_t command, uint8_t value) override {
    if (failWrites) return false;
    writes.push_back({command, value});
    if (command == kCh422gWriteIo) latch = value;
    return true;
  }
  bool read(uint8_t command, uint8_t& value) override {
    if (failReads) return false;
    assert(command == kCh422gReadIo);
    value = latch;
    return true;
  }
};
}

int main() {
  // begin() enables the outputs and starts every line low.
  {
    FakeBus bus;
    Ch422g expander(bus);
    assert(expander.begin());
    assert(bus.writes.size() == 3);
    assert(bus.writes[0].command == kCh422gWriteSet && bus.writes[0].value == kCh422gOutputEnable);
    assert(bus.writes[1].command == kCh422gWriteOc && bus.writes[1].value == 0x00);
    assert(bus.writes[2].command == kCh422gWriteIo && bus.writes[2].value == 0x00);
    assert(expander.levels() == 0x00);
    assert(expander.error() == nullptr);
  }

  // Each line maps to its documented bit, and setting one leaves the others alone.
  {
    FakeBus bus;
    Ch422g expander(bus);
    assert(expander.begin());
    assert(expander.set(Ch422gLine::PanelPower, true));
    assert(expander.levels() == (1u << 6));
    assert(expander.set(Ch422gLine::Backlight, true));
    assert(expander.levels() == ((1u << 6) | (1u << 2)));
    assert(expander.get(Ch422gLine::Backlight));
    assert(!expander.get(Ch422gLine::PanelReset));
    assert(expander.set(Ch422gLine::Backlight, false));
    assert(expander.levels() == (1u << 6));
    // Only the changed byte is written: an unchanged set is a no-op on the bus.
    const size_t before = bus.writes.size();
    assert(expander.set(Ch422gLine::Backlight, false));
    assert(bus.writes.size() == before);
  }

  // Panel reset pulses low then high, in that order.
  {
    FakeBus bus;
    Ch422g expander(bus);
    assert(expander.begin());
    bus.writes.clear();
    assert(expander.pulsePanelReset());
    assert(bus.writes.size() == 2);
    assert(bus.writes[0].value == 0x00);
    assert(bus.writes[1].value == (1u << 3));
  }

  // The touch reset and the USB/CAN mux use the same path as everything else.
  {
    FakeBus bus;
    Ch422g expander(bus);
    assert(expander.begin());
    assert(expander.set(Ch422gLine::TouchReset, false));
    assert(expander.set(Ch422gLine::UsbOrCan, false));   // low keeps native USB for the daemon
    assert(expander.levels() == 0x00);
    assert(expander.set(Ch422gLine::UsbOrCan, true));
    assert(expander.levels() == (1u << 5));
  }

  // Reading the latch back proves the expander answered.
  {
    FakeBus bus;
    Ch422g expander(bus);
    assert(expander.begin());
    assert(expander.set(Ch422gLine::PanelPower, true));
    uint8_t levels = 0;
    assert(expander.verify(levels));
    assert(levels == (1u << 6));
  }

  // A missing expander is reported instead of pretending the panel came up.
  {
    FakeBus bus;
    Ch422g expander(bus);
    bus.failWrites = true;
    assert(!expander.begin());
    assert(std::strcmp(expander.error(), "CH422G did not answer on the I2C bus.") == 0);
  }
  {
    FakeBus bus;
    Ch422g expander(bus);
    assert(expander.begin());
    bus.failReads = true;
    uint8_t levels = 0;
    assert(!expander.verify(levels));
    assert(expander.error() != nullptr);
  }

  std::cout << "ch422g ok\n";
  return 0;
}
