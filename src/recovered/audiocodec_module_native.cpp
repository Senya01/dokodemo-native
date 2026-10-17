#include "audiocodec_module_native.h"

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
AudiocodecCheckNeedMemCode RunAudiocodecCheckNeedMemPrefix(
    AudiocodecCheckNeedMemBuffer buffer, uint32_t codec, AudiocodecCheckNeedMemSnapshot& out) noexcept {
    if (out.entered) return AudiocodecCheckNeedMemCode::HostRefused;
    // Literal source0..D4 is bound by Stage18 SOURCE_EXPECTATIONS.v1.json.
    // Source20 null guard precedes k1/address and codec guards.
    if (!buffer.bytes_ && !buffer.known_ && !buffer.size_) {
        out.entered = true; out.source_null = true; out.codec = codec;
        out.source_return = true;
        out.source_result = static_cast<int32_t>(int64_t(0x807F0001u) - 0x100000000LL);
        return AudiocodecCheckNeedMemCode::Returned;
    }
    if (!buffer.bytes_ || !buffer.known_ || buffer.size_ != 0x68) return AudiocodecCheckNeedMemCode::HostRefused;
    // HOST adaptation, not a numeric execution of the unknown original k1:
    // the manager admitted this exact live owner-relative writable masked range.
    // Original28..48 tests ((k1>>16)&18) and signed32(ptr|(ptr+68)).
    // A bad native capability is refused OUTSIDE this body, never mapped80000023.
    out.entered = true; out.host_policy_applied = true; out.codec = codec;
    if (uint32_t(codec - 0x1000u) >= 5u) {
        out.source_return = true;
        out.source_result = static_cast<int32_t>(int64_t(0x80000004u) - 0x100000000LL);
        return AudiocodecCheckNeedMemCode::Returned;
    }
    // Source7C/80 is the ONLY pre-dependency buffer access: no preimage read.
    constexpr uint32_t magic = 0x04090701;
    for (uint32_t i = 0; i < 4; ++i) {
        buffer.bytes_[i] = static_cast<uint8_t>(magic >> (8 * i)); buffer.known_[i] = 1;
    }
    out.writes = 1; out.write_source_pc = 0x80;
    out.dependency_blocked = true; out.frontier_source_pc = 0x84; out.frontier_delay_pc = 0x88;
    out.frontier_stub_va = 0xAF8; out.frontier_nid = 0xC00D873B;
    // The84 JAL target and88 delay prepare ME(a0=codec,a1=SAME buffer).
    // No exact ME service/resources exist. Stop BEFORE dispatch; source8C..D0
    // result mapping, CheckNeedMem return and caller continuation are unreached.
    return AudiocodecCheckNeedMemCode::DependencyBlocked;
}
bool CompleteAudiocodecNativePrepareResult(AudiocodecCheckNeedMemSnapshot& s,int32_t result) noexcept {
    if(!s.entered || !s.dependency_blocked || s.source_return || s.writes!=1) return false;
    s.dependency_blocked=false;
    // dependency_dispatched concerns original ME hardware, not the Windows API.
    s.source_return=true;
    const uint32_t bits=result==0?0u:result==-1?0x807F0001u:result==-2?0x80000002u:0x807F0002u;
    s.source_result=static_cast<int32_t>(int64_t(bits)-(bits&0x80000000u?0x100000000LL:0));
    s.frontier_source_pc=s.frontier_delay_pc=s.frontier_stub_va=s.frontier_nid=0;
    return true;
}
bool ReadAudiocodecGetEDRAMNeed(AudiocodecGetEDRAMBuffer b,uint32_t& need,uint32_t& request) noexcept {
    if(!b.bytes_ || !b.known_ || b.size_!=0x6C) return false;
    for(uint32_t j=0;j<4;++j) if(b.known_[0x10+j]!=1) return false;
    need=0;
    for(uint32_t j=0;j<4;++j) need|=uint32_t(b.bytes_[0x10+j])<<(8*j);
    request=(need+63u)|63u; // recovered67C/6A8/6B0; modulo32 arithmetic
    return true;
}
} // namespace dokodemo::recovered
