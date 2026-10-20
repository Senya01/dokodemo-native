#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace dokodemo::recovered {
struct AdhocctlImportLibrary {
    const char* name;
    uint16_t version, attributes;
    uint8_t words, variables;
    uint16_t first_function, function_count;
};
struct AdhocctlImportFunction { uint32_t nid, stub_va; };
struct AdhocctlExport {
    const char* library;
    bool variable;
    uint16_t version, attributes;
    uint32_t nid, source_va;
};
struct AdhocctlModuleDescriptor {
    const char* name;
    uint16_t attributes, version;
    uint32_t encoded_bytes, plaintext_bytes;
    const char* encoded_sha256;
    const char* plaintext_sha256;
    std::array<AdhocctlImportLibrary, 8> imports;
    std::array<AdhocctlImportFunction, 54> functions;
    std::array<AdhocctlExport, 27> exports;
    uint32_t source_elf_entry, start_source_va, stop_source_va;
    std::array<uint32_t, 2> start_words, stop_words;
};
const AdhocctlModuleDescriptor* FindAdhocctlModuleDescriptor(
    std::string_view encoded_sha256, uint32_t encoded_bytes) noexcept;
bool AdhocctlImportPolicyAllows(const AdhocctlImportLibrary&) noexcept;
struct AdhocctlPrivateDataSnapshot {
    bool present = false;
    uint64_t owner = 0, generation = 0, serial = 0, native_state_address = 0;
    uint32_t source_segment = 1, source_segment_va = 0x6810, private_bytes = 5892;
    uint32_t source_file_offset = 0x6890, source_file_bytes = 132;
    uint32_t initial_source_known_bytes = 132, known_bytes = 0, writes_count = 0;
    std::array<uint8_t, 5892> bytes{}, known{};
};
void InitializeAdhocctlPrivateData(AdhocctlPrivateDataSnapshot&) noexcept;
int32_t AdhocctlStart4D54() noexcept;
int32_t AdhocctlStop4D5C() noexcept;
} // namespace dokodemo::recovered

