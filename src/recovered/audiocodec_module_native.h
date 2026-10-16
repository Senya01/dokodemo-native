#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dokodemo::recovered {
// Saved source-table facts only. These addresses are unrelocated source VAs;
// none is a native callable pointer or an assigned module address.
struct AudiocodecImportFunction {
    uint32_t nid;
    const char* name; // nullptr when the saved primary-source naming is unknown
    uint32_t source_stub_va;
    std::array<uint32_t, 2> source_stub_words;
};
struct AudiocodecImportLibrary {
    const char* name;
    uint16_t version, attributes;
    uint8_t entry_words, variable_count;
    uint16_t first_function, function_count;
};
struct AudiocodecEntryMetadata {
    uint32_t nid, source_va;
    std::array<uint32_t, 2> source_words;
};
struct AudiocodecModuleDescriptor {
    const char* name;
    uint16_t attributes;
    std::array<uint8_t, 2> version;
    uint32_t encoded_bytes, plaintext_bytes;
    const char* encoded_sha256;
    const char* plaintext_sha256; // offline validated relation, not runtime decode
    std::array<AudiocodecImportLibrary, 3> imports;
    std::array<AudiocodecImportFunction, 8> functions;
    AudiocodecEntryMetadata module_start, module_stop;
    uint32_t source_elf_entry;
    // Later 6.60 documentation calls bit 0x8 weak. Applicability to the
    // original firmware, and its import binding policy, remain unknown.
    uint16_t later_660_weak_import_bit;
    bool original_firmware_binding_policy_known;
};

// Exact input identity selects metadata, never a Loaded record, UID or native
// implementation. The caller hashes the actual fully known input itself.
const AudiocodecModuleDescriptor* FindAudiocodecModuleDescriptor(
    std::string_view encoded_sha256, uint32_t encoded_bytes) noexcept;
} // namespace dokodemo::recovered
