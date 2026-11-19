#pragma once
#include <cstdint>
#include <vector>

namespace dokodemo::recovered {
// Native object identities, never invented PSP addresses. References carry
// their original table identity; ctx+30 is a separate disjoint object.
struct Service47Address {
    enum class Table : uint8_t { Stack, Global, Local };
    Table table = Table::Stack;
    uint32_t index = 0;
};
struct Service47Cell {
    uint32_t word = 0;
    bool is_reference = false;
    Service47Address reference;
};
class Service47Cells {
public:
    virtual ~Service47Cells() = default;
    virtual Service47Cell load(Service47Address address) = 0;
    virtual void store(Service47Address address, Service47Cell value) = 0;
};
struct Service47Fixture {
    // All premises are opt-in. Fresh BSS zeros are not current availability.
    bool enabled = false, control_cell_mapped = false, guard_cell_mapped = false;
    bool state_cell_mapped = false, memory_disjoint = false;
    bool control_present = false, control_word_mapped = false;
    bool source_present = false, source_header_mapped = false;
    bool status_cell_mapped = false, format_window_mapped = false;
    uint32_t source_magic = 0, status = 0, format_source_offset = 0x10B50;
    uint8_t guard = 0;
    uint16_t state = 0;
    // Bytes mapped at source+10B50; this is a window, not a copy length.
    std::vector<uint8_t> format_bytes;
    // Protective native caps, absent from the original recursion.
    uint32_t depth_limit = 64, word_limit = 4096;
};
struct Service47State {
    bool known = false, control_present = false, source_present = false;
    uint8_t guard = 0;
    uint16_t state = 0;
    uint32_t calls = 0, cursor_bytes = 0, decoded_words = 0;
    bool format_called = false;
    int32_t format_return = 0; // Original7CE00 return, discarded by host47.
    uint32_t null_free_calls = 0, released_entities = 0;
    bool operator==(const Service47State& other) const;
};
Service47State Service47InitialState(const Service47Fixture& fixture);
uint32_t Service47Magic25078(uint32_t word);
uint32_t Service47Status4E2FC(uint32_t status);
uint32_t Service47Status4E890(uint32_t status);
// The helper preserves original partial cell writes when it throws. The VM
// runs it on a candidate context and commits only a fully supported call.
// A native barrier is not an original return/fault, and gets no recovery credit.
int32_t Service47Format7CE00(Service47Cells& cells, Service47Address destination,
                           const Service47Fixture& fixture, Service47State& state);
// Singleton54 registration/50+54 clearing and F0 argument/result effects are
// performed by StartupProbe in original order around this joined host route.
void Service47Host1C958(Service47Cells& cells, Service47Address destination,
                      const Service47Fixture& fixture, Service47State& state);
} // namespace dokodemo::recovered
