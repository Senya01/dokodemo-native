#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dokodemo::recovered {
struct PspnetImportLibrary {
    const char* name;
    uint16_t version, attributes;
    uint8_t words, variables;
    uint16_t first_function, function_count;
};
struct PspnetImportFunction { uint32_t nid, stub_va; };
struct PspnetExport {
    const char* library;
    bool variable;
    uint16_t version, attributes;
    uint32_t nid, source_va;
};
struct PspnetModuleDescriptor {
    const char* name;
    uint16_t attributes, version;
    uint32_t input_bytes; // already ELF; no runtime transform
    const char* input_sha256;
    std::array<PspnetImportLibrary, 3> imports;
    std::array<PspnetImportFunction, 42> functions;
    std::array<PspnetExport, 107> exports;
    uint32_t source_elf_entry, start_source_va, stop_source_va;
    std::array<uint32_t, 2> start_words, stop_words;
};
const PspnetModuleDescriptor* FindPspnetModuleDescriptor(std::string_view, uint32_t) noexcept;
bool PspnetImportPolicyAllows(const PspnetImportLibrary&) noexcept;
// Actual anonymous exports, independently bound; not import placeholders.
int32_t PspnetStart7B78() noexcept;
int32_t PspnetStop7B80() noexcept;
struct PspnetPrivateDataSnapshot {
    bool present = false;
    uint64_t owner = 0, generation = 0, serial = 0, native_state_address = 0;
    uint32_t source_segment = 1, source_segment_va = 0xA090, private_bytes = 6848;
    uint32_t initial_source_known_bytes = 192, known_bytes = 0, writes_count = 0;
    // Physical host zeros do not establish BSS. Only known[i]==1 gives source meaning.
    // The reached Start/Stop neither consume nor write these diagnostic bytes.
    std::array<uint8_t, 6848> bytes{}, known{};
};
void InitializePspnetPrivateData(PspnetPrivateDataSnapshot&) noexcept;
} // namespace dokodemo::recovered
