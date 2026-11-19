#include "service47_native.h"
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
int32_t signed28(uint32_t word) {
    const uint32_t p = word & 0x0FFFFFFF;
    return p & 0x08000000 ? static_cast<int32_t>(p) - 0x10000000 : static_cast<int32_t>(p);
}
struct Cursor7CBF4 {
    const Service47Fixture& fixture;
    Service47Cells& cells;
    uint32_t words = 0;
    uint32_t read(uint32_t& current) {
        require(words < fixture.word_limit, "Service47 native decoded-word cap (original has no cap)");
        require(current <= fixture.format_bytes.size() && fixture.format_bytes.size() - current >= 4,
                "Service47 source cursor read outside explicit mapped window");
        uint32_t word = 0;
        for (uint32_t i = 0; i < 4; ++i) word |= uint32_t(fixture.format_bytes[current + i]) << (8 * i);
        current += 4; ++words; //7CC40 stores cursor before dispatch/destination access.
        return word;
    }
    int32_t decode(Service47Address destination, uint32_t& current, uint32_t depth) {
        require(depth < fixture.depth_limit, "Service47 native recursion cap (original has no cap)");
        const uint32_t source = read(current), tag = source >> 28;
        if (tag == 8 || tag == 10 || tag >= 12) return 0; //No destination read.
        require(tag != 6, "Service47 source tag6 string allocation/copy is unproved at753E0");
        const Service47Cell old = cells.load(destination);
        const uint32_t old_tag = old.is_reference ? 11 : old.word >> 28;
        if (tag == 9) {
            if (old_tag != 9) return -1;
            cells.store(destination, {source, false, {}}); //7CD68 BEFORE child reads.
            int32_t count = static_cast<int32_t>(source & 0x3FFF);
            if (count & 0x2000) count -= 0x4000;
            int32_t errors = 0;
            for (int32_t i = 0; i < count; ++i) {
                require(destination.index < UINT32_MAX, "Service47 native destination index overflow");
                ++destination.index;
                errors |= decode(destination, current, depth + 1); //Continue after original -1.
            }
            return errors;
        }
        if (tag == 11) {
            if (old_tag != 11) return -1;
            require(old.is_reference, "Service47 raw destination tagB lacks mapped native identity");
            const int32_t offset = signed28(source);
            require(offset >= 0, "Service47 reference cursor outside mapped source base window");
            uint32_t nested = static_cast<uint32_t>(offset);
            (void)decode(old.reference, nested, depth + 1); //7CDF8 discards recursive return.
            return 0;
        }
        if (old_tag == 9 || old_tag == 11) return -1;
        require(old_tag != 6 && old_tag != 10,
                "Service47 old destination owned release at75238 is unproved");
        //75238 clears destination, then7CCF4 stores the exact source word.
        cells.store(destination, {});
        cells.store(destination, {source, false, {}});
        return 0;
    }
};
}
bool Service47State::operator==(const Service47State& o) const {
    return known == o.known && control_present == o.control_present && source_present == o.source_present &&
        guard == o.guard && state == o.state && calls == o.calls && cursor_bytes == o.cursor_bytes &&
        decoded_words == o.decoded_words && format_called == o.format_called && format_return == o.format_return &&
        null_free_calls == o.null_free_calls && released_entities == o.released_entities;
}
Service47State Service47InitialState(const Service47Fixture& f) {
    Service47State s; s.known = f.enabled; s.control_present = f.control_present;
    s.source_present = f.source_present; s.guard = f.guard; s.state = f.state; return s;
}
uint32_t Service47Magic25078(uint32_t word) { return word == 0x0011076A ? 1u : 0u; }
uint32_t Service47Status4E2FC(uint32_t status) { return status == 0x80110307 ? 1u : 0u; }
uint32_t Service47Status4E890(uint32_t status) { return (~status) >> 31; }
int32_t Service47Format7CE00(Service47Cells& cells, Service47Address destination,
                           const Service47Fixture& f, Service47State& s) {
    require(f.format_window_mapped && f.format_source_offset == 0x10B50 && f.memory_disjoint,
            "Service47 format source+10B50 window/disjoint mapping is unavailable");
    Cursor7CBF4 cursor{f, cells}; uint32_t current = 0;
    const int32_t result = cursor.decode(destination, current, 0);
    s.cursor_bytes = current; s.decoded_words = cursor.words; s.format_called = true; s.format_return = result;
    return result;
}
void Service47Host1C958(Service47Cells& cells, Service47Address destination,
                      const Service47Fixture& f, Service47State& s) {
    require(f.enabled && s.known && f.control_cell_mapped && f.guard_cell_mapped &&
            f.state_cell_mapped && f.memory_disjoint,
            "Service47 needs explicit canonical control/guard/state/disjoint availability");
    bool target = false;
    if (s.control_present) {
        require(f.control_word_mapped, "Service47 control[0] mapping unavailable at4DA98");
        target = s.source_present;
    }
    bool ready = false;
    if (target) {
        require(f.source_header_mapped, "Service47 source header mapping unavailable at4E6F8");
        ready = Service47Magic25078(f.source_magic) != 0;
    }
    s.format_called = false; s.cursor_bytes = 0; s.decoded_words = 0; s.format_return = 0;
    if (ready) {
        require(f.status_cell_mapped, "Service47 status1BA498 mapping unavailable at4E320");
        if (Service47Status4E890(f.status) == 1 && Service47Status4E2FC(f.status) != 1 && !(f.status & 0x80000000))
            (void)Service47Format7CE00(cells, destination, f, s);
    }
    //4E224 guard!=0 skips4DB54. Guard0/controlNULL returns without frees.
    //NonNULL releases are not owned no-ops:63378 allocator mutations remain a cut.
    if (!s.guard && s.control_present) {
        if (!target) ++s.null_free_calls; //63C5C/63378(NULL), v0 unspecified/unused.
        throw std::runtime_error("Service47 nonNULL heap release at63378 is outside recovered allocator domain");
    }
    s.guard = 0; s.state = 0xFFFF; ++s.calls;
}
} // namespace dokodemo::recovered
