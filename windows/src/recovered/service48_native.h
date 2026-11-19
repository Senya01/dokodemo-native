#pragma once
#include "service47_native.h"
#include <cstdint>
#include <vector>
#include <string>

namespace dokodemo::recovered {
class StringArena;
// Stable virtual pointer labels in a caller-shared StringArena. No host pointer
// reinterpretation and no implication that the PSP producer ran.
class OwnedService48Record final {
public:
    // Source8-byte allocation policy through caller arena, then2E14 +4=-1,
    // +0=0 in original order. NULL allocation is an explicit native refusal.
    static OwnedService48Record Create(StringArena& arena);
    // Explicit post-loader fixture bytes; allocates actual owned payload and
    // fills record fields. Does not claim2E7C producer/availability recovery.
    void SupplyLoadedPayload(StringArena& arena, const std::vector<uint8_t>& bytes,
                             int32_t loader_index = -1);
    uint32_t pointer() const { return pointer_; }
private:
    uint32_t pointer_ = 0;
};
struct Service48Fixture {
    bool enabled = false, manager_cell_mapped = false, manager_record_mapped = false;
    bool singleton_mapped = false, arena_pointer_mapped = false;
    bool memory_disjoint = false, manager_stable_during_call = false;
    bool pool_pointer_cell_mapped = false;
    // Arena pointer and the host's s1. Modulo32 addition precedes +58 store.
    uint32_t arena_base = 0, destination_offset = 0, original_return_word = 0;
    // Positive-index pool virtual callbacks are unknown; no NULL shortcut.
    uint32_t depth_limit = 64, word_limit = 4096;
};
struct Service48State {
    // Current manager+4CC input. Never automatically copied from fresh BSS.
    uint32_t manager_record = 0;
    bool destination_registered = false, format_called = false;
    Service47Address destination;
    uint32_t registered_word = 0, cursor_bytes = 0, decoded_words = 0;
    int32_t format_return = 0;
    uint32_t payload_clears = 0, record_releases = 0, manager_clears = 0, completed_calls = 0;
    uint32_t heap_release_calls = 0, null_release_calls = 0;
};
struct Service48Boundary {
    enum class Kind { None, NativeDomain, PoolCallback, FormatOwnedValue, FormatDomain, HeapDomain };
    Kind kind = Kind::None;
    uint32_t source_pc = 0, target = 0;
    std::string detail;
    explicit operator bool() const { return kind != Kind::None; }
};
struct Service48Result {
    bool completed = false;
    uint32_t return_word = 0;
    Service48Boundary boundary;
};
// Read/write current table cells and caller-shared owned arena directly.
// Effects preceding a supported-prefix boundary remain visible. Native free
// validation and caps are protective refusals, not PSP partial faults.
// StartupProbe may copy arena/state/cells and commit success only; that is a
// separate transaction policy. Original parser -1 is ignored by host48.
Service48Result Service48Host1C9E4(Service47Cells& cells, Service47Address destination,
                                 const Service48Fixture& fixture, Service48State& state,
                                 StringArena& arena);
const char* Service48BoundaryName(Service48Boundary::Kind kind);
} // namespace dokodemo::recovered
