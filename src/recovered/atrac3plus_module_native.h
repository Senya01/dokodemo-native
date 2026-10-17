#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dokodemo::recovered {
struct Atrac3plusImportLibrary {
    const char* name;
    uint16_t version, attributes;
    uint8_t entry_words, variable_count;
    uint16_t first_function, function_count;
};
struct Atrac3plusImportFunction { uint32_t nid, source_stub_va; };
struct Atrac3plusExport {
    const char* library;
    bool variable;
    uint16_t attributes;
    uint32_t nid, source_va;
};
struct Atrac3plusModuleDescriptor {
    const char* name;
    uint16_t attributes, version;
    uint32_t input_bytes;
    const char* input_sha256;
    std::array<Atrac3plusImportLibrary, 2> imports;
    std::array<Atrac3plusImportFunction, 7> functions;
    std::array<Atrac3plusExport, 28> exports;
    uint32_t start_source_va, stop_source_va;
};
const Atrac3plusModuleDescriptor* FindAtrac3plusModuleDescriptor(std::string_view, uint32_t) noexcept;
// Explicit NEW Windows adaptation. Original firmware policy is UNKNOWN.
// This validates only this canonical module's metadata shape, not a service ABI.
bool Atrac3plusImportPolicyAllows(std::string_view library, uint16_t version,
    uint16_t attributes, uint8_t entry_words, uint8_t variables, uint16_t functions) noexcept;

enum class Atrac3plusWriteSource { Atrac3plus, Audiocodec, NativePlatform };
struct Atrac3plusNativeWrite {
    uint32_t ordinal = 0, source_pc = 0, offset = 0, width = 0, value = 0;
    Atrac3plusWriteSource source_module = Atrac3plusWriteSource::Atrac3plus;
};
struct Atrac3plusInitializationSnapshot {
    bool present = false, prefix_entered = false, dependency_blocked = false, aborted = false;
    bool prepared_envelope_policy = false;
    uint64_t owner = 0, generation = 0, serial = 0, native_state_address = 0;
    uint32_t source_segment = 1, source_segment_offset = 0xA0;
    uint32_t record_count = 4, record_stride = 0x148, record_bytes = 0x520;
    uint32_t initial_source_known_bytes = 0, known_bytes = 0, writes_count = 0;
    // Private AOT state, NOT a copy/mapping of input bytes. bytes[i] has source
    // meaning ONLY when known[i]==1. Host value-initialization proves no BSS byte.
    std::array<uint8_t, 0x520> bytes{}, known{};
    std::array<Atrac3plusNativeWrite, 32> writes{};
    uint32_t frontier_source_pc = 0, frontier_delay_pc = 0, frontier_stub_va = 0;
    uint32_t frontier_import_index = 0, frontier_nid = 0;
    // a0 references this owned native state, record0+44, never a PSP/host integer ABI.
    uint32_t arg0_record_index = 0, arg0_record_offset = 0, arg1 = 0;
    bool frontier_arguments_prepared = false;
    uint32_t helper_calls_entered = 0, loop_iterations_completed = 0;
    // Counts the actual entered CheckNeedMem import; its downstream ME request
    // has a separate dependency_dispatched=false in the typed call snapshot.
    uint32_t external_services_dispatched = 0, external_resources_acquired = 0;
};
// Native translation of the closed no-preimage-read prefix. It stops BEFORE
// dispatching CheckNeedMem1684, after preparing the1688 delay-slot argument.
// No source RA/stack/GP/mapping or source/entry/service return is manufactured.
bool RunAtrac3plusStartPrefix(Atrac3plusInitializationSnapshot&) noexcept;
// Separate finite Windows envelope domain. These execute only the next literal
// helper prefix / continuation; the manager supplies actual service outcomes.
bool ContinueAtrac3plusPreparedRecord(Atrac3plusInitializationSnapshot&, bool allocation_succeeded) noexcept;
bool CommitAtrac3plusPreparedWrite(Atrac3plusInitializationSnapshot&, uint32_t source_pc,
    uint32_t offset, uint32_t value, Atrac3plusWriteSource) noexcept;
// Fixed no-preimage-read admission/abort shapes: four ATRAC writes, optionally
// followed by the single audiocodec CheckNeedMem write. Unknown bytes are unread.
bool Atrac3plusCheckNeedMemStateExact(const Atrac3plusInitializationSnapshot&, bool after_write) noexcept;
bool AbortAtrac3plusInitialization(Atrac3plusInitializationSnapshot&) noexcept;
} // namespace dokodemo::recovered
