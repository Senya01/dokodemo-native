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

enum class AudiocodecCheckNeedMemCode { HostRefused, Returned, DependencyBlocked };
struct AudiocodecCheckNeedMemSnapshot {
    bool entered = false, host_policy_applied = false, source_null = false;
    bool dependency_blocked = false, dependency_dispatched = false, source_return = false;
    int32_t source_result = 0;
    uint32_t codec = 0, buffer_reads = 0, writes = 0;
    uint32_t entry_source_pc = 0, write_source_pc = 0;
    uint32_t frontier_source_pc = 0, frontier_delay_pc = 0, frontier_stub_va = 0, frontier_nid = 0;
};
// Only the manager's typed borrow can construct a writable span. There is no
// public raw-pointer constructor or caller-supplied production service callback.
class AudiocodecCheckNeedMemBuffer final {
public:
    AudiocodecCheckNeedMemBuffer() noexcept = default; // explicit source NULL
private:
    uint8_t* bytes_ = nullptr;
    uint8_t* known_ = nullptr;
    uint32_t size_ = 0;
    friend class NativeCheckNeedMemBorrow;
    friend class NativeModuleManager;
    friend AudiocodecCheckNeedMemCode RunAudiocodecCheckNeedMemPrefix(
        AudiocodecCheckNeedMemBuffer, uint32_t, AudiocodecCheckNeedMemSnapshot&) noexcept;
};
// Source-informed Windows adaptation: validated owner/bounds/mask capability
// replaces the ORIGINAL PSP k1/address check, whose context remains unknown.
// No PSP k1/pointer value or ME result is supplied by this native body.
AudiocodecCheckNeedMemCode RunAudiocodecCheckNeedMemPrefix(
    AudiocodecCheckNeedMemBuffer, uint32_t codec, AudiocodecCheckNeedMemSnapshot&) noexcept;
// Applies recovered8C..D0 ONLY after the distinct native provider produced an
// actual classified service outcome. No buffer access or firmware claim.
bool CompleteAudiocodecNativePrepareResult(AudiocodecCheckNeedMemSnapshot&, int32_t) noexcept;
// Deliberately DISTINCT108B record capability, including the raw-allocation
// field beyond the104B CheckNeedMem window. Only the manager can mint it.
class AudiocodecGetEDRAMBuffer final {
    const uint8_t* bytes_ = nullptr;
    const uint8_t* known_ = nullptr;
    uint32_t size_ = 0;
    friend class NativeModuleManager;
    friend bool ReadAudiocodecGetEDRAMNeed(AudiocodecGetEDRAMBuffer,uint32_t&,uint32_t&) noexcept;
};
bool ReadAudiocodecGetEDRAMNeed(AudiocodecGetEDRAMBuffer,uint32_t& need,uint32_t& request) noexcept;
} // namespace dokodemo::recovered
