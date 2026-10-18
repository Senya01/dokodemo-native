#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dokodemo::recovered {
struct IfhandleImportLibrary {
    const char* name;
    uint16_t version, attributes;
    uint8_t words, variables;
    uint16_t first_function, function_count;
};
struct IfhandleImportFunction { uint32_t nid, stub_va; };
struct IfhandleExport {
    const char* library;
    bool variable;
    uint16_t version, attributes;
    uint32_t nid, source_va;
};
struct IfhandleModuleDescriptor {
    const char* name;
    uint16_t attributes, version;
    uint32_t encoded_bytes, plaintext_bytes;
    const char* encoded_sha256;
    const char* plaintext_sha256;
    std::array<IfhandleImportLibrary, 4> imports;
    std::array<IfhandleImportFunction, 20> functions;
    std::array<IfhandleExport, 92> exports;
    uint32_t source_elf_entry, start_source_va, stop_source_va;
    std::array<uint32_t, 2> start_words, stop_words;
};
const IfhandleModuleDescriptor* FindIfhandleModuleDescriptor(std::string_view, uint32_t) noexcept;
bool IfhandleImportPolicyAllows(const IfhandleImportLibrary&) noexcept;
// Independently bound anonymous exports, not the JR-RA/NOP import stubs.
int32_t IfhandleStart3454() noexcept;
int32_t IfhandleStop345C() noexcept;
struct IfhandlePrivateDataSnapshot {
    bool present = false;
    uint64_t owner = 0, generation = 0, serial = 0, native_state_address = 0;
    uint32_t source_segment = 1, source_segment_va = 0x3B20, private_bytes = 2240;
    uint32_t initial_source_known_bytes = 16, known_bytes = 0, writes_count = 0;
    // Host value initialization does not establish any unknown BSS byte.
    // bytes[i] has source meaning only when known[i]==1. Entry/Stop touch none.
    std::array<uint8_t, 2240> bytes{}, known{};
};
void InitializeIfhandlePrivateData(IfhandlePrivateDataSnapshot&) noexcept;
} // namespace dokodemo::recovered
