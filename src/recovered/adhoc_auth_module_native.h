#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dokodemo::recovered {
struct AdhocAuthImportLibrary {
    const char* name;
    uint16_t version, attributes;
    uint8_t words, variables;
    uint16_t first_function, function_count;
};
struct AdhocAuthImportFunction { uint32_t nid, stub_va; };
struct AdhocAuthExport { const char* library; bool variable; uint16_t version, attributes; uint32_t nid, source_va; };
struct AdhocAuthModuleDescriptor {
    const char* name;
    uint16_t attributes, version;
    uint32_t encoded_bytes, plaintext_bytes;
    const char* encoded_sha256;
    const char* plaintext_sha256;
    std::array<AdhocAuthImportLibrary, 5> imports;
    std::array<AdhocAuthImportFunction, 42> functions;
    std::array<AdhocAuthExport, 21> exports;
    uint32_t source_elf_entry, start_source_va, stop_source_va;
    std::array<uint32_t, 2> start_words, stop_words;
};
const AdhocAuthModuleDescriptor* FindAdhocAuthModuleDescriptor(std::string_view, uint32_t) noexcept;
bool AdhocAuthImportPolicyAllows(const AdhocAuthImportLibrary&) noexcept;
struct AdhocAuthPrivateDataSnapshot {
    bool present = false;
    uint64_t owner = 0, generation = 0, serial = 0, native_state_address = 0;
    uint32_t source_segment = 1, source_segment_va = 0x3ED0, private_bytes = 412;
    uint32_t source_file_offset = 0x3F50, source_file_bytes = 8;
    uint32_t initial_source_known_bytes = 8, known_bytes = 0, writes_count = 0;
    std::array<uint8_t, 412> bytes{}, known{};
};
void InitializeAdhocAuthPrivateData(AdhocAuthPrivateDataSnapshot&) noexcept;
int32_t AdhocAuthStart3A00() noexcept;
int32_t AdhocAuthStop3A08() noexcept;
} // namespace dokodemo::recovered
