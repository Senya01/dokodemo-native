#pragma once
#include "string_native.h"
#include <cstdint>
#include <vector>

namespace dokodemo::recovered {
enum class Service39Space : uint32_t { Stack, Locals, Globals, External };
// A symbolic reference carries its actual table identity. External references
// remain external; no table or global fallback is inferred from an address.
struct Service39Address {
    Service39Space space = Service39Space::External;
    uint32_t table_base = 0;
    int32_t byte_offset = 0;
};
struct Service39Mapping {
    uint32_t address = 0;
    std::vector<uint8_t> bytes;
    bool writable = false; // Source only reads this window; readonly is valid.
};
struct Service39Input {
    bool enabled = false;
    bool reference_readable = false;
    bool router_context_readable = false;
    Service39Address reference;
    // Only zero is admitted: an explicitly distinct abstract writable result
    // cell. The caller proves its source backing disjoint from every heap,
    // controller, table, mapped input/output window and other global state.
    // Every nonzero physical location refuses; this API does not own or write
    // complete external result storage. Generic fixture disjointness is not
    // evidence for this result-cell obligation.
    uint32_t result_address = 0;
    std::vector<Service39Mapping> mappings;
};
struct Service39Normalized {
    std::vector<uint8_t> bytes; // Includes the final NUL, without artificial pad.
    uint32_t source_bytes_read = 0;
    bool all_converted = true; // Original14F90 v0; handler39 ignores this value.
};
struct Service39Observation {
    Service39Address reference;
    uint32_t reference_address = 0;
    uint32_t reference_word = 0;
    uint32_t source_pointer = 0;
    uint32_t prologue_result = 0x10000000;
    uint32_t result_word = 0;
    uint32_t cursor_before = 0, cursor_after = 0;
    Service39Normalized normalized;
    StringArenaSummary arena_before, arena_after;
};

// Original14E44 scalar conversion, including literal FFF7DB3F digit addend.
int32_t Service39Character(uint32_t code);
// Original14F90 finite readable-buffer domain. Output must fit the original
// 1024-byte router local including NUL; missing reads/overflow refuse.
Service39Normalized Service39Normalize(const std::vector<uint8_t>& source,
                                       bool filter_unconverted = true);
// Connected original1C184 prologue +1C7B4 +75778 +14F90 +7515C +753E0.
// Current tag5/6 getter domain, real StringArena allocator/storage/cursor.
// Result admission requires result_address==0 and the explicit caller proof
// above. All physical result locations, including valid disjoint ones, refuse.
// Makes a COW candidate, committing arena/result only after complete success.
// Original temporal partial faults, numeric getter/format, concurrent memory,
// stack overwrite and result/reference physical aliases remain outside domain.
// F0 argument cleanup and TEMP->ctx30->TEMPfree belong to the caller.
Service39Observation Service39Apply(StringArena& arena, const Service39Input& input,
                                    uint32_t& result);
} // namespace dokodemo::recovered
