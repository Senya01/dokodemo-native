#include "atrac3plus_module_native.h"

namespace dokodemo::recovered {
namespace {
// SOURCE_EXPECTATIONS.v1.json, DATA_DEPENDENCIES.v1.json and LIFECYCLE_WORDS.v1.json
// under research/stage17/module-source bind this exact unencoded ELF. All VAs
// are module-relative source metadata. 386 relocation roles are NOT applied.
constexpr Atrac3plusModuleDescriptor Descriptor{
    "sceATRAC3plus_Library", 0x0006, 0x0101, 19524,
    "edfc95810a5881f7f752eee255693e75ba1ade6862b557126c5236c2b47bbca4",
    {{{"Kernel_Library",0,0x0001,5,0,0,2}, {"sceAudiocodec",0,0x4001,5,0,2,5}}},
    {{{0x092968F4,0x3630}, {0x5F10D406,0x3638}, {0x9D3F790C,0x3640},
      {0x5B37EB1D,0x3648}, {0x70A703F8,0x3650}, {0x3A20A200,0x3658}, {0x29681260,0x3660}}},
    {{{"",false,0x8000,0xD632ACDB,0x11A0}, {"",false,0x8000,0xCEE8593C,0x121C},
      {"",true,0x8000,0xF01D73A7,0x36C8},
      {"sceAtrac3plus",false,0x0001,0xD1F59FDB,4512},
      {"sceAtrac3plus",false,0x0001,0xD5C28CC0,4636},
      {"sceAtrac3plus",false,0x0001,0x780F88D1,0},
      {"sceAtrac3plus",false,0x0001,0x61EB33F5,240},
      {"sceAtrac3plus",false,0x0001,0x0E2A73AB,4736},
      {"sceAtrac3plus",false,0x0001,0x3F6E26B5,372},
      {"sceAtrac3plus",false,0x0001,0x7A20E7AF,516},
      {"sceAtrac3plus",false,0x0001,0x0FAE370E,848},
      {"sceAtrac3plus",false,0x0001,0x6A8C3CD5,1196},
      {"sceAtrac3plus",false,0x0001,0x9AE849A7,4876},
      {"sceAtrac3plus",false,0x0001,0x5D268707,1544},
      {"sceAtrac3plus",false,0x0001,0x7DB31251,1820},
      {"sceAtrac3plus",false,0x0001,0x83E85EA0,2080},
      {"sceAtrac3plus",false,0x0001,0x83BF7AFD,2284},
      {"sceAtrac3plus",false,0x0001,0xE23E3A35,2668},
      {"sceAtrac3plus",false,0x0001,0xA2BBA8BE,2520},
      {"sceAtrac3plus",false,0x0001,0x31668BAA,5004},
      {"sceAtrac3plus",false,0x0001,0xD6A5F2F7,5092},
      {"sceAtrac3plus",false,0x0001,0x36FAABFB,5220},
      {"sceAtrac3plus",false,0x0001,0xA554A158,5348},
      {"sceAtrac3plus",false,0x0001,0xFAA4F89B,5476},
      {"sceAtrac3plus",false,0x0001,0x868120B5,2816},
      {"sceAtrac3plus",false,0x0001,0xCA3CA3D2,2964},
      {"sceAtrac3plus",false,0x0001,0x644E5607,3136},
      {"sceAtrac3plus",false,0x0001,0xE88F759B,5608}}},
    0x11A0, 0x121C
};
void Store(Atrac3plusInitializationSnapshot& state, uint32_t pc, uint32_t offset,
           uint32_t width, uint32_t value) noexcept {
    auto& write = state.writes[state.writes_count];
    write = {state.writes_count, pc, offset, width, value};
    ++state.writes_count;
    for (uint32_t i = 0; i < width; ++i) {
        state.bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
        if (!state.known[offset + i]) ++state.known_bytes;
        state.known[offset + i] = 1;
    }
}
} // namespace
const Atrac3plusModuleDescriptor* FindAtrac3plusModuleDescriptor(std::string_view sha, uint32_t bytes) noexcept {
    return sha == Descriptor.input_sha256 && bytes == Descriptor.input_bytes ? &Descriptor : nullptr;
}
bool Atrac3plusImportPolicyAllows(std::string_view library, uint16_t version, uint16_t attributes,
    uint8_t words, uint8_t variables, uint16_t functions) noexcept {
    if (version || words != 5 || variables) return false;
    return (library == "Kernel_Library" && attributes == 0x0001 && functions == 2) ||
           (library == "sceAudiocodec" && attributes == 0x4001 && functions == 5);
}
bool RunAtrac3plusStartPrefix(Atrac3plusInitializationSnapshot& state) noexcept {
    if (!state.present || !state.owner || !state.generation || !state.serial || state.prefix_entered ||
        state.aborted || state.known_bytes || state.writes_count) return false;
    state.prefix_entered = true; state.helper_calls_entered = 1;
    // Source11A4/11B0 relocation roles select owned segment1+A0. The first
    // helper's a0 retains record0 even though caller11C8 advances its own s0.
    // No source global/data/BSS preimage is read before the first import.
    Store(state,0x1678,0xAC,4,0x00200000);
    Store(state,0x1680,0x00,4,1); // taken BEQ167C delay, codec1000
    Store(state,0x16D4,0x6C,1,0x28);
    Store(state,0x16E0,0x6D,1,0x5C); // J16DC delay
    state.frontier_source_pc = 0x1684; state.frontier_delay_pc = 0x1688;
    state.frontier_stub_va = 0x3640; state.frontier_import_index = 2; state.frontier_nid = 0x9D3F790C;
    state.arg0_record_index = 0; state.arg0_record_offset = 0x44; state.arg1 = 0x1000;
    state.frontier_arguments_prepared = true; state.dependency_blocked = true;
    return true; // host prefix completion, NEVER an entry/helper/service v0
}
bool AbortAtrac3plusInitialization(Atrac3plusInitializationSnapshot& state) noexcept {
    if (state.prepared_envelope_policy) {
        if (!state.prefix_entered || !state.dependency_blocked || state.aborted ||
            state.writes_count > state.writes.size() || state.helper_calls_entered > 3 || state.loop_iterations_completed > 2)
            return false;
        std::array<uint8_t,0x520> mask{}, values{};
        for (uint32_t n=0;n<state.writes_count;++n) {
            const auto& w=state.writes[n];
            if (w.ordinal!=n || (w.width!=1 && w.width!=4) || w.offset>mask.size() || w.width>mask.size()-w.offset) return false;
            for(uint32_t j=0;j<w.width;++j) { mask[w.offset+j]=1; values[w.offset+j]=uint8_t(w.value>>(8*j)); }
        }
        uint32_t count=0;
        for(uint32_t n=0;n<mask.size();++n) {
            if(mask[n]!=state.known[n] || (mask[n] && values[n]!=state.bytes[n])) return false;
            count+=mask[n];
        }
        if(count!=state.known_bytes) return false;
        state.aborted=true;state.dependency_blocked=false;return true;
    }
    if (!state.prefix_entered || !state.dependency_blocked || state.aborted ||
        state.external_resources_acquired ||
        (!Atrac3plusCheckNeedMemStateExact(state, false) &&
         !Atrac3plusCheckNeedMemStateExact(state, true))) return false;
    state.aborted = true; state.dependency_blocked = false;
    return true; // host abort retains exact partial effects; no source Stop121C
}
bool CommitAtrac3plusPreparedWrite(Atrac3plusInitializationSnapshot& s,uint32_t pc,uint32_t offset,
                                  uint32_t value,Atrac3plusWriteSource origin) noexcept {
    if(!s.prepared_envelope_policy || s.aborted || s.writes_count>=s.writes.size() || offset>s.bytes.size()-4) return false;
    const auto ordinal=s.writes_count; Store(s,pc,offset,4,value); s.writes[ordinal].source_module=origin;return true;
}
bool ContinueAtrac3plusPreparedRecord(Atrac3plusInitializationSnapshot& s,bool allocated) noexcept {
    if(!s.prepared_envelope_policy || s.aborted || !s.dependency_blocked || s.arg0_record_index>=2 ||
        s.loop_iterations_completed!=s.arg0_record_index || s.writes_count>s.writes.size()-6) return false;
    const uint32_t base=s.arg0_record_index*0x148;
    if(allocated) {
        Store(s,0x16A8,base+0xF0,4,0x1000);
        Store(s,0x16AC,base,4,0);
    }
    ++s.loop_iterations_completed;
    const uint32_t next=s.loop_iterations_completed, next_base=next*0x148;
    ++s.helper_calls_entered;
    Store(s,0x1678,next_base+0xAC,4,0x00200000);
    Store(s,0x1680,next_base,4,1);
    if(next<2) {
        Store(s,0x16D4,next_base+0x6C,1,0x28);
        Store(s,0x16E0,next_base+0x6D,1,0x5C);
    }
    s.arg0_record_index=next;s.arg0_record_offset=next_base+0x44;s.arg1=next<2?0x1000:0x1001;
    s.frontier_source_pc=0x1684;s.frontier_delay_pc=0x1688;s.frontier_stub_va=0x3640;
    s.frontier_import_index=2;s.frontier_nid=0x9D3F790C;s.frontier_arguments_prepared=true;return true;
}
bool Atrac3plusCheckNeedMemStateExact(const Atrac3plusInitializationSnapshot& state, bool after) noexcept {
    if (!state.present || !state.prefix_entered || !state.dependency_blocked || state.aborted ||
        !state.owner || !state.generation || !state.serial || state.source_segment != 1 ||
        state.source_segment_offset != 0xA0 || state.record_count != 4 || state.record_stride != 0x148 ||
        state.record_bytes != 0x520 || state.initial_source_known_bytes ||
        state.known_bytes != (after ? 14u : 10u) || state.writes_count != (after ? 5u : 4u) ||
        state.frontier_source_pc != 0x1684 || state.frontier_delay_pc != 0x1688 ||
        state.frontier_stub_va != 0x3640 || state.frontier_import_index != 2 || state.frontier_nid != 0x9D3F790C ||
        state.arg0_record_index || state.arg0_record_offset != 0x44 || state.arg1 != 0x1000 ||
        !state.frontier_arguments_prepared || state.helper_calls_entered != 1 ||
        state.loop_iterations_completed || state.external_resources_acquired ||
        state.external_services_dispatched > 1 || (after && state.external_services_dispatched != 1)) return false;
    constexpr std::array<Atrac3plusNativeWrite, 5> expected{{
        {0,0x1678,0xAC,4,0x00200000,Atrac3plusWriteSource::Atrac3plus},
        {1,0x1680,0,4,1,Atrac3plusWriteSource::Atrac3plus},
        {2,0x16D4,0x6C,1,0x28,Atrac3plusWriteSource::Atrac3plus},
        {3,0x16E0,0x6D,1,0x5C,Atrac3plusWriteSource::Atrac3plus},
        {4,0x80,0x44,4,0x04090701,Atrac3plusWriteSource::Audiocodec}
    }};
    std::array<uint8_t, 0x520> mask{}, values{};
    for (uint32_t i = 0; i < state.writes_count; ++i) {
        const auto& got = state.writes[i]; const auto& want = expected[i];
        if (got.ordinal != want.ordinal || got.source_pc != want.source_pc || got.offset != want.offset ||
            got.width != want.width || got.value != want.value || got.source_module != want.source_module) return false;
        for (uint32_t j = 0; j < want.width; ++j) {
            mask[want.offset + j] = 1;
            values[want.offset + j] = static_cast<uint8_t>(want.value >> (j * 8));
        }
    }
    for (uint32_t i = 0; i < state.record_bytes; ++i) {
        if (state.known[i] != mask[i]) return false;
        if (mask[i] && state.bytes[i] != values[i]) return false; // never read unknown bytes
    }
    return true;
}
} // namespace dokodemo::recovered
