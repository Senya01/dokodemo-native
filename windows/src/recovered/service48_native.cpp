#include "service48_native.h"
#include "string_native.h"
#include <stdexcept>
#include <limits>

namespace dokodemo::recovered {
namespace {
struct Stop { Service48Boundary boundary; };
void require48(bool condition, uint32_t pc, Service48Boundary::Kind kind, const char* detail, uint32_t target = 0) {
    if (!condition) throw Stop{{kind, pc, target, detail}};
}
int32_t signed_payload(uint32_t word) {
    const uint32_t p = word & 0x0FFFFFFF;
    return p & 0x08000000 ? static_cast<int32_t>(p) - 0x10000000 : static_cast<int32_t>(p);
}
bool negative_word(uint32_t word) { return (word & 0x80000000) != 0; }
struct Cursor48 {
    Service47Cells& cells;
    const Service48Fixture& fixture;
    Service48State& state;
    const std::vector<uint8_t>& bytes;
    uint32_t read(uint32_t& cursor) {
        require48(state.decoded_words < fixture.word_limit, 0x7CC3C,
                  Service48Boundary::Kind::FormatDomain, "Native decoded-word cap; original has no cap");
        require48(cursor <= bytes.size() && bytes.size() - cursor >= 4,
                  0x7CC3C, Service48Boundary::Kind::FormatDomain, "Source cursor outside explicit owned payload window", cursor);
        uint32_t value = 0;
        for (uint32_t i = 0; i < 4; ++i) value |= uint32_t(bytes[cursor + i]) << (8 * i);
        cursor += 4; ++state.decoded_words;
        return value;
    }
    int32_t decode(Service47Address destination, uint32_t& cursor, uint32_t depth) {
        require48(depth < fixture.depth_limit, 0x7CBF4, Service48Boundary::Kind::FormatDomain,
                  "Native recursion cap; original has no cap");
        const uint32_t source = read(cursor), tag = source >> 28;
        if (tag == 8 || tag == 10 || tag >= 12) return 0;
        require48(tag != 6, 0x7CD0C, Service48Boundary::Kind::FormatOwnedValue,
                  "Source tag6 assignment753E0 requires separately recovered string mapping");
        const Service47Cell old = cells.load(destination);
        const uint32_t old_tag = old.is_reference ? 11 : old.word >> 28;
        if (tag == 9) {
            if (old_tag != 9) return -1;
            cells.store(destination, {source, false, {}}); //7CD68 BEFORE child reads.
            int32_t count = static_cast<int32_t>(source & 0x3FFF);
            if (count & 0x2000) count -= 0x4000;
            int32_t errors = 0;
            for (int32_t i = 0; i < count; ++i) {
                require48(destination.index != UINT32_MAX, 0x7CD88, Service48Boundary::Kind::FormatDomain,
                          "Native destination index overflow");
                ++destination.index;
                errors |= decode(destination, cursor, depth + 1);
            }
            return errors;
        }
        if (tag == 11) {
            if (old_tag != 11) return -1;
            require48(old.is_reference, 0x7CDE4, Service48Boundary::Kind::FormatDomain,
                      "Raw destination tagB has no mapped native identity");
            const int32_t offset = signed_payload(source);
            require48(offset >= 0, 0x7CDEC, Service48Boundary::Kind::FormatDomain,
                      "Reference offset outside explicit payload window");
            uint32_t nested = static_cast<uint32_t>(offset);
            (void)decode(old.reference, nested, depth + 1); //7CDF8 discards return.
            return 0;
        }
        if (old_tag == 9 || old_tag == 11) return -1;
        require48(old_tag != 6 && old_tag != 10, 0x7CCEC, Service48Boundary::Kind::FormatOwnedValue,
                  "Old destination owned6/A release75238 is outside service48 scalar domain");
        cells.store(destination, {}); //75238 scalar/null release.
        cells.store(destination, {source, false, {}}); //7CCF4 exact source word.
        return 0;
    }
};
void release48(StringArena& arena, uint32_t pointer, uint32_t pc, Service48State& state) {
    ++state.heap_release_calls;
    if (!pointer) ++state.null_release_calls;
    try { arena.free(pointer); }
    catch (const std::exception& e) { throw Stop{{Service48Boundary::Kind::HeapDomain, pc, pointer, e.what()}}; }
}
}

OwnedService48Record OwnedService48Record::Create(StringArena& arena) {
    OwnedService48Record record;
    record.pointer_ = arena.allocate(8); //1AF5C/63D14 with8-byte request.
    if (!record.pointer_) throw std::runtime_error("Service48 native record allocation failed; original NULL producer later dereferences NULL");
    arena.store_word(record.pointer_ + 4, 0xFFFFFFFF); //2E18
    arena.store_word(record.pointer_, 0); //2E20 delay slot.
    return record;
}
void OwnedService48Record::SupplyLoadedPayload(StringArena& arena, const std::vector<uint8_t>& bytes, int32_t index) {
    if (!pointer_ || arena.allocation_size(pointer_) < 8 || arena.word(pointer_))
        throw std::runtime_error("Service48 explicit payload supply requires live initialized empty record");
    if (bytes.empty() || bytes.size() > static_cast<size_t>(INT32_MAX))
        throw std::runtime_error("Service48 explicit payload bytes require positive signed allocation size");
    const uint32_t payload = arena.allocate(static_cast<int32_t>(bytes.size()));
    if (!payload) throw std::runtime_error("Service48 native fixture payload allocation failed");
    arena.store(payload, bytes);
    arena.store_word(pointer_ + 4, static_cast<uint32_t>(index));
    arena.store_word(pointer_, payload);
}

Service48Result Service48Host1C9E4(Service47Cells& cells, Service47Address destination,
                                 const Service48Fixture& f, Service48State& s, StringArena& arena) {
    Service48Result result;
    try {
        require48(f.enabled && f.manager_cell_mapped && f.manager_record_mapped,
                  0x1C9E8, Service48Boundary::Kind::NativeDomain, "Current mapped manager/+4CC premise unavailable");
        const uint32_t record = s.manager_record; //1C9EC; CURRENT state, not constructor zeros.
        if (!record) {
            result.completed = true; result.return_word = f.original_return_word;
            ++s.completed_calls; return result; //1C9F0→1C220, no singleton access.
        }
        require48(f.singleton_mapped && f.arena_pointer_mapped && f.memory_disjoint && arena.ready(),
                  0x1C9FC, Service48Boundary::Kind::NativeDomain, "NonNULL service48 singleton/base/owned arena/disjoint premise unavailable");
        uint32_t payload = 0;
        try { payload = arena.word(record); }
        catch (const std::exception& e) { throw Stop{{Service48Boundary::Kind::HeapDomain, 0x1CA04, record, e.what()}}; }
        s.destination = destination; s.registered_word = f.arena_base + f.destination_offset;
        s.destination_registered = true; //1CA10 CALL DELAY, before parser.
        s.format_called = false; s.cursor_bytes = 0; s.decoded_words = 0; s.format_return = 0;
        std::vector<uint8_t> bytes;
        try { bytes = arena.bytes(payload, arena.allocation_size(payload)); }
        catch (const std::exception& e) { throw Stop{{Service48Boundary::Kind::HeapDomain, 0x7CC3C, payload, e.what()}}; }
        Cursor48 parser{cells, f, s, bytes}; uint32_t cursor = 0;
        try { s.format_return = parser.decode(destination, cursor, 0); }
        catch (...) { s.cursor_bytes = cursor; throw; }
        s.cursor_bytes = cursor; s.format_called = true; //7CE00 return; host48 ignores -1.
        uint32_t index = 0;
        try { index = arena.word(record + 4); payload = arena.word(record); }
        catch (const std::exception& e) { throw Stop{{Service48Boundary::Kind::HeapDomain, 0x2F58, record, e.what()}}; }
        require48(f.pool_pointer_cell_mapped, 0x2F64, Service48Boundary::Kind::NativeDomain,
                  "Pool singleton cell is read even for a negative loader index; mapping unavailable");
        require48(negative_word(index), 0x281C, Service48Boundary::Kind::PoolCallback,
                  "Nonnegative loader index requires actual pool table and virtual1F90 target", index);
        if (payload) release48(arena, payload, 0x2868, s); //281C negative, NULL skips call.
        arena.store_word(record, 0); ++s.payload_clears; //2F68 only AFTER free.
        //2E50 reloads +0,+4 and calls281C. Current payload0 skips heap call.
        index = arena.word(record + 4); payload = arena.word(record);
        require48(negative_word(index), 0x281C, Service48Boundary::Kind::PoolCallback,
                  "Second release observes changed nonnegative loader index", index);
        if (payload) release48(arena, payload, 0x2868, s);
        release48(arena, record, 0x63ED4, s); ++s.record_releases;
        require48(f.manager_stable_during_call, 0x1CA2C, Service48Boundary::Kind::NativeDomain,
                  "Reloaded CURRENT manager identity/lifetime must be explicitly established");
        s.manager_record = 0; ++s.manager_clears; //1CA34 jump delay after manager reload.
        result.completed = true; result.return_word = f.original_return_word;
        ++s.completed_calls;
    } catch (const Stop& stop) { result.boundary = stop.boundary; }
      catch (const std::exception& e) { result.boundary = {Service48Boundary::Kind::FormatDomain, 0x7CBF4, 0, e.what()}; }
    return result;
}
const char* Service48BoundaryName(Service48Boundary::Kind k) {
    switch (k) {
    case Service48Boundary::Kind::None: return "none";
    case Service48Boundary::Kind::NativeDomain: return "native_domain";
    case Service48Boundary::Kind::PoolCallback: return "pool_callback";
    case Service48Boundary::Kind::FormatOwnedValue: return "format_owned_value";
    case Service48Boundary::Kind::FormatDomain: return "format_domain";
    case Service48Boundary::Kind::HeapDomain: return "heap_domain";
    }
    return "unknown";
}
} // namespace dokodemo::recovered
