#include "audiocodec_module_native.h"
#include "atrac3plus_module_native.h"

namespace dokodemo::recovered {
namespace {
// MODULE_TABLES.v1.json SHA256:
// 38911b88c152a0fb37872ba88db5ccdb48e77ef9f016f9d7d7392c828dd80634
// TABLES_RESULT.v1.json binds the 5888-byte plaintext below. Its source stub
// pairs are unbound JR RA/NOP, not implementations of the imported services.
constexpr AudiocodecModuleDescriptor Descriptor{
    "sceAudiocodec_Driver", 0x1006, {{1, 1}}, 3232, 5888,
    "cea81b33d4eab3607da188b0435a8582ffc7c208337c05f7e3139f0133bcb318",
    "10b0e173fcdcb21dbca1e1180cf186588f62e530fd7df04be55aa88aaf9fd2a9",
    {{
        {"sceMeAudio_driver", 0, 0x0009, 5, 0, 0, 4},
        {"sceMeMemory_driver", 0, 0x0009, 5, 0, 4, 2},
        {"UtilsForKernel", 0, 0x0009, 5, 0, 6, 2}
    }},
    {{
        {0xC00D873B, nullptr, 0xAF8, {{0x03E00008, 0x00000000}}},
        {0xFEE0A97D, nullptr, 0xB00, {{0x03E00008, 0x00000000}}},
        {0xBAC7C2BE, nullptr, 0xB08, {{0x03E00008, 0x00000000}}},
        {0x39F06790, nullptr, 0xB10, {{0x03E00008, 0x00000000}}},
        {0xA177798D, nullptr, 0xB18, {{0x03E00008, 0x00000000}}},
        {0x5AD660FA, nullptr, 0xB20, {{0x03E00008, 0x00000000}}},
        {0x3EE30821, "sceKernelDcacheWritebackRange", 0xAE8, {{0x03E00008, 0x00000000}}},
        {0x34B9FA9E, "sceKernelDcacheWritebackInvalidateRange", 0xAF0, {{0x03E00008, 0x00000000}}}
    }},
    {0xD632ACDB, 0xAD8, {{0x03E00008, 0x00001021}}},
    {0xCEE8593C, 0xAE0, {{0x03E00008, 0x00001021}}},
    0xAD8, 0x0008, false
};
// The two entry word pairs statically return zero when called under the source
// ABI. They are metadata here; neither entry is executed or used to waive Load.
} // namespace

const AudiocodecModuleDescriptor* FindAudiocodecModuleDescriptor(
    std::string_view encoded_sha256, uint32_t encoded_bytes) noexcept {
    if (encoded_bytes != Descriptor.encoded_bytes || encoded_sha256 != Descriptor.encoded_sha256)
        return nullptr;
    return &Descriptor;
}

bool EnterAudiocodecCheckNeedMem(Atrac3plusInitializationSnapshot& state,
    int32_t started_audio_uid) noexcept {
    if (started_audio_uid <= 0 || !state.present || !state.owner || !state.serial || !state.generation ||
        state.native_state_address != reinterpret_cast<uintptr_t>(&state) ||
        !state.prefix_entered || !state.dependency_blocked || state.aborted || state.check_need_mem.entered ||
        !state.frontier_arguments_prepared || state.frontier_nid != 0x9D3F790C ||
        state.arg0_record_index != 0 || state.arg0_record_offset != 0x44) return false;

    // Canonical audiocodec plaintext VA 0x20..0x54 checks a0, k1/address
    // domain (including buffer+0x68), and unsigned codec-0x1000 < 5.
    // Native calls carry a bounded offset inside the same live record. Host
    // pointer bits cannot substitute for a PSP address or a PSP k1 value.
    const uint32_t offset = state.arg0_record_offset;
    constexpr uint32_t record_size = 0x148;
    if (state.record_stride != record_size || offset > record_size - 0x6C ||
        state.arg1 - 0x1000u >= 5u) return false;
    static_assert(record_size <= std::tuple_size<decltype(state.bytes)>::value);

    // VA 0x54,0x7C,0x80: sw 0x04090701,0(workarea). No preimage
    // read is needed. Only this word becomes known; the rest retains its mask.
    constexpr uint32_t signature = 0x04090701;
    for (unsigned i = 0; i < 4; ++i) {
        state.bytes[offset + i] = static_cast<uint8_t>(signature >> (i * 8));
        if (!state.known[offset + i]) ++state.known_bytes;
        state.known[offset + i] = 1;
    }
    // VA 0x84 and its delay slot prepare a0=codec,a1=workarea for the
    // exact ME import. It is unavailable, so there is no result to map.
    state.check_need_mem = {true, started_audio_uid, state.arg1, offset, 0x84, 0xAF8, 0xC00D873B};
    ++state.external_services_dispatched;
    return true;
}
} // namespace dokodemo::recovered
