#pragma once
#include "script_vm.h"
#include "recovered/service47_native.h"
#include "recovered/string_native.h"
#include "recovered/service48_native.h"
#include "recovered/clock_native.h"
#include "recovered/service39_native.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// BOOT-proved startup prefix only. No PSP CPU/OS/GPU emulation, no dialogue
// fixture. Opcodes14/15 support mapped references and non-owned copies only.
// See research/stage5/startup-calendar/REPORT.md for connected calendar/89,
// original-word tests and atomic barriers; this is not a complete game VM.
// Legacy host78 still assumes a valid linked host, selected SLOT preread and
// live singleton. Compatibility acceptance does not prove their construction.
namespace dokodemo::startup {

struct Value {
    enum class Kind : uint8_t { Word, StackReference, GlobalReference, LocalReference, Ctx28Reference };
    Kind kind = Kind::Word;
    uint32_t word = 0, index = 0, source_file_offset = UINT32_MAX;
    static Value integer(int32_t value);
    static Value stack_reference(uint32_t index);
    static Value global_reference(uint32_t index, uint32_t file_offset);
    static Value local_reference(uint32_t index, uint32_t file_offset);
    static Value ctx28_reference(uint32_t index, uint32_t file_offset);
    bool operator==(const Value& other) const;
};

struct Snapshot {
    uint32_t pc = 0;
    int32_t sp = 0, fp = -1, saved_fp = 0, saved_sp = 0;
    uint32_t state = 2;
    Value result;
    std::vector<Value> stack_window; // Includes preloaded slots above SP.
    bool timer_base_set = false;
    uint32_t timer_base_pc = 0;      // Absolute scheduler time is external.
    bool host_flag_104_known = false;
    uint8_t host_flag_104 = 0;
    std::map<uint32_t, Value> global_writes;
    std::map<uint32_t, Value> local_writes; // Small original ctx+24 table, complete in every snapshot.
    std::map<uint32_t, Value> host_reference_bindings; // Singleton fields by byte offset; symbolic Arena pointer identity.
    recovered::Service47State service47;
    recovered::StringArenaSummary string_arena;
    recovered::StringArenaSummary general_arena;
    recovered::Service48State service48;
    // Final contexts keep the full map. Event snapshots carry a canonical
    // digest/count instead; replay Event.global_write_delta to reconstruct it.
    bool global_writes_complete = true;
    uint32_t global_writes_count = 0;
    std::string global_writes_sha256;
};

// BOOT 6BC20/6BCB8 tests the raw low 28 bits, without numeric conversion.
// This decision alone does not implement owned-string release or resolve an
// unknown reference address. The probe rejects those lifecycle/address paths.
bool branch_condition(uint8_t opcode, uint32_t word);

// Conditional ABI projection supplied explicitly by the caller. These are
// proof-domain premises, not observed constructor/PSP addresses or host values.
// The direct-registration paths require all premises; default callers stop.
struct HostAbiProjection {
    bool linked_context_mapped = false;
    bool selected_record_mapped = false; // Selected SLOT for1C1F0 load is valid; its value may be NULL.
    bool singleton_mapped = false;
    // Includes context/host/singleton/stack/local/global objects, with only
    // the declared ctx+30 result subobject; no cross-object aliases allowed.
    bool arena_mapping_disjoint = false;
    bool constants_initialized = false;
    uint32_t selected_record_index = UINT32_MAX; // Explicit supported0/1 domain, not a default host value.
    bool allows_direct_registrations() const {
        return linked_context_mapped && selected_record_mapped && singleton_mapped &&
               arena_mapping_disjoint && constants_initialized && selected_record_index <= 1;
    }
};

// Separately supplied mapped source getters, never implied by selector0 or
// native allocation. Actual profile/date producer and readiness remain unknown.
struct StringService20Projection {
    bool enabled = false, memory_disjoint = false, date_record_mapped = false;
    //Stronger than readability: independently mapped cell1C949C points to
    //THIS Program's global table with its CURRENT live overrides and bound.
    //No source initializer/alias relation is inferred from host/KSC metadata.
    bool shared_global_table_mapped = false;
    std::map<uint32_t,std::vector<uint8_t>> string_windows; //selectors1,5,7,8; exactly16 readable bytes.
    std::map<uint32_t,uint32_t> scalar_words; //selectors2,4,6,9,10; explicit raw source cells.
    std::array<uint32_t,7> date_words{}; //24F34(0): +8minute,+12hour,+16day,+20month,+24year.
};
struct Service48Projection {
    recovered::Service48Fixture fixture; //all source manager/singleton/pool availability defaultsfalse.
    //Separate GENERAL allocator identity1C93D4, distinct from STRING1C94A8.
    //Sharing the portable class does not alias the actual source managers.
    recovered::StringFixture general_heap;
    std::vector<uint8_t> prepared_payload; //explicit NONNULL post2E7C mode1 fixture, producerunknown.
};
struct Service39Projection {
    bool enabled = false, router_context_mapped = false;
    bool reference_tables_mapped = false, memory_disjoint = false;
    //memory_disjoint also proves the writable abstract TEMP backing distinct
    //from every heap/controller/table/input/output/global-state object. The
    //helper's result_address0 is this explicit premise, not a physical NULL.
    // Additional readonly borrowed string windows, with explicit addresses.
    // Reached H1 tag6 inputs use CURRENT StringArena live storage instead.
    std::vector<recovered::Service39Mapping> borrowed_windows;
};

struct BuiltinCall {
    bool valid = false;
    uint32_t pc = 0, handler_va = 0, host_handler_va = 0;
    uint8_t id = 0, argc = 0;
    uint32_t host_service = 0;
    std::vector<Value> arguments;
    Value result;
};

struct Event {
    uint32_t pc = 0, file_offset = 0, next_pc = 0, handler_va = 0;
    int opcode = -1;
    Snapshot before, after;
    BuiltinCall builtin;
    std::map<uint32_t, Value> global_write_delta;
};

enum class Status { Running, Halted, BudgetExhausted, InvalidInput, Terminated };
const char* status_name(Status status);
struct Trace {
    Status status = Status::InvalidInput;
    uint8_t host_byte = 0;
    uint32_t executed = 0, pc = 0;
    int32_t sp = 0, fp = -1;
    Value result;
    BuiltinCall first_builtin;
    Snapshot context;
    script::Diagnostic diagnostic;
    std::vector<Event> events;
};

class StartupProbe {
public:
    // Program must outlive the probe and remain unchanged. host_byte must be
    // explicitly supplied as 0 or 1; its actual game initial value is unproved.
    // stack_slot_limit is a native resource cap <= original 512-slot allocation,
    // exposed for bounded failure checks; ordinary callers use the default.
    StartupProbe(const script::Program& program, uint8_t host_byte,
                 uint16_t stack_slot_limit = 512, HostAbiProjection projection = {},
                 recovered::Service47Fixture service47 = {}, recovered::StringFixture strings = {},
                 StringService20Projection string_getters = {}, Service48Projection service48 = {},
                 recovered::ClockDependency clock = {}, Service39Projection service39 = {});
    // Per-call budget must be 1..10000. Returns cumulative executed/events.
    // Budget exhaustion can be resumed. Unknown opcode/ABI paths halt; repeated
    // run() calls preserve PC, SP, FP, slots, builtin result and host flag.
    Trace run(uint32_t instruction_budget);
private:
    friend struct StartupTestAccess;
    struct Context {
        std::array<Value, 512> slots{};
        uint32_t pc = 0, state = 2;
        int32_t sp = 0, fp = -1, saved_fp = 0, saved_sp = 0;
        Value result;
        bool timer_base_set = false, host_flag_104_known = false;
        uint32_t timer_base_pc = 0;
        uint8_t host_flag_104 = 0;
        std::map<uint32_t, Value> global_writes;
        std::map<uint32_t, Value> local_writes;
        std::map<uint32_t, Value> host_reference_bindings;
        recovered::Service47State service47;
        recovered::StringArena strings;
        recovered::StringArena general_heap;
        recovered::Service48State service48;
    };
    const script::Program& program_;
    uint8_t host_byte_ = 0;
    uint16_t stack_slot_limit_ = 512;
    HostAbiProjection host_abi_projection_;
    recovered::Service47Fixture service47_fixture_;
    recovered::StringFixture string_fixture_;
    StringService20Projection string_getters_;
    Service48Projection service48_projection_;
    recovered::ClockDependency clock_dependency_;
    Service39Projection service39_projection_;
    Context context_;
    Status status_ = Status::Running;
    script::Diagnostic diagnostic_;
    std::vector<Event> events_;
    BuiltinCall first_builtin_;
    Snapshot snapshot(const Context& context, bool include_globals = true) const;
    Trace trace() const;
    Value& slot(Context& context, int32_t index) const;
    const Value& slot(const Context& context, int32_t index) const;
    void push(Context& context, Value value) const;
    Value pop(Context& context) const;
    Value read_reference(const Context& context, const Value& reference,
                         uint32_t following_words = 0) const;
    void write_reference(Context& context, const Value& reference, Value value) const;
    void require_target(uint32_t pc) const;
    void execute(Context& candidate, const script::Command& command,
                 BuiltinCall& builtin) const;
};

Trace run(const script::Program& program, uint8_t host_byte,
          uint32_t instruction_budget = 256);
} // namespace dokodemo::startup
