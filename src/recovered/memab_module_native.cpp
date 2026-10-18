#include "memab_module_native.h"

namespace dokodemo::recovered {
namespace {
const MemabModuleDescriptor descriptor{
    "sceMemab",0x1006,0x0101,15216,14880,
    "07f07637b64268df841892adf9e21ccc418c5c8641e60e44447ba74b1c31a1a8",
    "a471025d3e3ecfd5a2ba4bcd3157fe54e5d620174e4bfa63d3d3de50c035b7c8",
    {{{"ThreadManForKernel",0,1,5,0,0,4},{"sceIdStorage_driver",0,1,5,0,4,1},{"semaphore",0,1,5,0,5,1}}},
    {{{0xD6DA4BA1,0x26C0},{0x28B6489C,0x26C8},{0x3F53E640,0x26D0},{0x4E3A1105,0x26D8},
      {0x6FE062D1,0x26E0},{0x4C537C72,0x26E8}}},
    {{{"",false,0,0x8000,0xD632ACDB,0x9F4},{"",false,0,0x8000,0xCEE8593C,0xA34},
      {"",true,0,0x8000,0xF01D73A7,0x2750},
      {"sceMemab_driver",false,0,1,0x01FD1BA0,0xA58},{"sceMemab_driver",false,0,1,0x0C134D3F,0xB10},
      {"sceMemab_driver",false,0,1,0xA4720EED,0xBB8},{"sceMemab_driver",false,0,1,0xDC598EF3,0xD24},
      {"sceMemab_driver",false,0,1,0x8DAC2343,0x1150},{"sceMemab_driver",false,0,1,0xDAE8A629,0x17C0},
      {"sceMemab_driver",false,0,1,0xCF49BCBB,0x1C54},{"sceMemab_driver",false,0,1,0xE6D3ACE2,0x1DCC},
      {"sceMemab_driver",false,0,1,0xB54E1AA7,0x1FE8},{"sceMemab_driver",false,0,1,0xF1D2B9B0,0x22CC},
      {"sceMemab_driver",false,0,1,0x4DF566D3,0x23DC}}},
    0x9F4,0x9F4,0xA34,
    {{0x3C040000,0x27BDFFF0,0x2484285C,0x00002821,0x24060001,0x24070001,0xAFBF0000,0x0C0009B0,
      0x00004021,0x28440001,0x8FBF0000,0x3C030000,0xAC620070,0x00801021,0x03E00008,0x27BD0010}},
    {{0x27BDFFF0,0xAFBF0000,0x3C020000,0x0C0009B2,0x8C440070,0x8FBF0000,0x00001021,0x03E00008,0x27BD0010}}
};
void Frontier(MemabInitializationSnapshot& s,uint32_t pc,uint32_t stub,uint32_t nid,uint32_t index) noexcept {
    s.frontier_source_pc=pc;s.frontier_stub_va=stub;s.frontier_nid=nid;s.frontier_import_index=index;
}
bool KeyMatches(const MemabInitializationSnapshot& s) noexcept {
    const auto& k=s.resources.key;
    return s.owner && s.generation && s.serial && k.module[0]==s.owner && k.module[1]==s.generation &&
        k.module[2]==s.serial && k.module[3] && k.module[3]<=uint64_t(INT32_MAX) &&
        k.state==std::array<uint64_t,3>{{s.owner,s.generation,s.serial}} &&
        k.input[0] && k.input[1] && k.input[2];
}
}
const MemabModuleDescriptor* FindMemabModuleDescriptor(std::string_view hash,uint32_t bytes) noexcept {
    return bytes==descriptor.encoded_bytes && hash==descriptor.encoded_sha256 ? &descriptor : nullptr;
}
bool MemabImportPolicyAllows(const MemabImportLibrary& library) noexcept {
    for(const auto& expected:descriptor.imports)
        if(std::string_view(library.name)==expected.name && library.version==expected.version &&
            library.attributes==expected.attributes && library.words==expected.words && library.variables==expected.variables &&
            library.first_function==expected.first_function && library.function_count==expected.function_count) return true;
    return false;
}
void InitializeMemabState(MemabInitializationSnapshot& s) noexcept { s={};s.present=true; }
int32_t MemabEntryResultForCreate(int32_t uid) noexcept { return uid<1 ? 1 : 0; } // A18 signed SLTI
NativeMemabCode RunMemabStart(MemabInitializationSnapshot& s,NativeMemabBackend* backend,std::shared_ptr<void> input) noexcept {
    if(!s.present || s.entry_entered || s.writes_count || s.known_bytes || !KeyMatches(s)) return NativeMemabCode::Refused;
    s.entry_entered=true;s.host_abort_required=true;
    Frontier(s,0xA10,0x26C0,0xD6DA4BA1,0);
    if(!backend || !backend->Ready() || !input) return NativeMemabCode::Refused;
    const auto key=s.resources.key;
    const auto result=backend->Create(key,std::move(input),s.resources);
    if(result!=NativeMemabCode::Ok || !s.resources.create_source_returned || !(s.resources.key==key) ||
        !s.resources.issued || s.resources.retired || !s.resources.input_pinned) return result==NativeMemabCode::Ok?NativeMemabCode::Refused:result;
    const int32_t uid=s.resources.create_source_result;
    if(uid>0 && (!s.resources.semaphore_live || s.resources.semaphore_uid!=uid || !s.resources.semaphore_handle)) return NativeMemabCode::Refused;
    // A24 is the ONLY producer: no relocated file pointers/BSS zeros are known.
    const uint32_t bits=static_cast<uint32_t>(uid);
    for(uint32_t i=0;i<4;++i) {s.bytes[0x70+i]=uint8_t(bits>>(8*i));s.known[0x70+i]=1;}
    s.known_bytes=4;s.writes[0]={0,0xA24,0x70,bits};s.writes_count=1;
    s.entry_result=MemabEntryResultForCreate(uid);s.entry_returned=true;
    s.stop_domain_ready=uid>0;s.host_abort_required=!s.stop_domain_ready;
    Frontier(s,0,0,0,0);return NativeMemabCode::Ok;
}
NativeMemabCode RunMemabStop(MemabInitializationSnapshot& s,NativeMemabBackend* backend) noexcept {
    if(!s.present || !s.entry_returned || s.entry_result || !s.stop_domain_ready || s.host_abort_required ||
        s.source_stop_returned || !KeyMatches(s) || !backend || !backend->Ready()) return NativeMemabCode::Refused;
    Frontier(s,0xA44,0x26C8,0x28B6489C,1);
    uint32_t bits=0;
    for(uint32_t i=0;i<4;++i) {if(s.known[0x70+i]!=1) return NativeMemabCode::Refused;bits|=uint32_t(s.bytes[0x70+i])<<(8*i);}
    if(!bits || bits>uint32_t(INT32_MAX) || int32_t(bits)!=s.resources.semaphore_uid ||
        !s.resources.create_source_returned || s.resources.create_source_result!=int32_t(bits)) return NativeMemabCode::Refused;
    s.source_stop_entered=true;Frontier(s,0xA40,0x26C8,0x28B6489C,1);
    const auto result=backend->Delete(s.resources.key,int32_t(bits),true,s.resources);
    if(result!=NativeMemabCode::Ok || !s.resources.delete_source_returned) return result==NativeMemabCode::Ok?NativeMemabCode::Refused:result;
    // A4C returns0 even for an ACTUALLY returned negative Delete result.
    s.source_stop_returned=true;Frontier(s,0,0,0,0);return NativeMemabCode::Ok;
}
NativeMemabCode AbortMemabInitialization(MemabInitializationSnapshot& s,NativeMemabBackend* backend) noexcept {
    if(!s.present || !s.entry_entered || !s.host_abort_required || s.source_stop_entered || s.host_aborted) return NativeMemabCode::Refused;
    s.host_abort_entered=true;
    if(s.resources.issued && !s.resources.retired) {
        if(!backend || !backend->Ready()) return NativeMemabCode::Refused;
        const auto result=backend->Retire(s.resources.key,s.resources);
        if(result!=NativeMemabCode::Ok || !s.resources.retired) return result==NativeMemabCode::Ok?NativeMemabCode::CleanupFailure:result;
    }
    s.host_aborted=true;s.host_abort_required=false;return NativeMemabCode::Ok; // no source Stop/store
}
} // namespace dokodemo::recovered
