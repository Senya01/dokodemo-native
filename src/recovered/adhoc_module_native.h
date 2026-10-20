#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dokodemo::recovered {
struct AdhocImportLibrary {
    const char* name;
    uint16_t version, attributes;
    uint8_t words, variables;
    uint16_t first_function, function_count;
};
struct AdhocImportFunction { uint32_t nid, stub_va; };
struct AdhocExport {
    const char* library;
    bool variable;
    uint16_t version, attributes;
    uint32_t nid, source_va;
};
struct AdhocModuleDescriptor {
    const char* name;
    uint16_t attributes, version;
    uint32_t encoded_bytes, plaintext_bytes;
    const char* encoded_sha256;
    const char* plaintext_sha256;
    std::array<AdhocImportLibrary, 5> imports;
    std::array<AdhocImportFunction, 34> functions;
    std::array<AdhocExport, 32> exports;
    uint32_t source_elf_entry, start_source_va, stop_source_va;
    std::array<uint32_t, 2> start_words, stop_words;
};
const AdhocModuleDescriptor* FindAdhocModuleDescriptor(
    std::string_view encoded_sha256, uint32_t encoded_bytes) noexcept;
bool AdhocImportPolicyAllows(const AdhocImportLibrary&) noexcept;
struct AdhocPrivateDataSnapshot {
    bool present = false;
    uint64_t owner = 0, generation = 0, serial = 0, native_state_address = 0;
    uint32_t source_segment = 1, source_segment_va = 0x73F0, private_bytes = 144;
    uint32_t source_file_offset = 0x7470, source_file_bytes = 4;
    uint32_t initial_source_known_bytes = 4, known_bytes = 0, writes_count = 0;
    std::array<uint8_t, 144> bytes{}, known{};
};
void InitializeAdhocPrivateData(AdhocPrivateDataSnapshot&) noexcept;
int32_t AdhocStart6D5C() noexcept;
int32_t AdhocStop6D64() noexcept;
} // namespace dokodemo::recovered
