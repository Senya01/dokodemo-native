#include "sascore_module_native.h"

namespace dokodemo::recovered {
namespace {
// Exact ELF_STRUCTURE.v1.json 1294bd72... and SOURCE_SUPPLEMENT.v1.json;
// offline CMAC/gzip association, no runtime decode or relocation application.
constexpr SascoreModuleDescriptor Descriptor{
    "sceSAScore", 0x1006, 0x0101, 5920, 11184,
    "f4cbbc90733c6b48b5a6b0d0c8b827f3478333b2a2d0c87b845231ab1916a0f5",
    "a6adccd8faa74a131ec34a2efa5e0d827204f2993bc7e55ad3b3a8f98d4b3a34",
    {{{"sceMeCore_driver",0,9,5,0,0,1}, {"sceMeMemory_driver",0,9,5,0,1,2},
      {"UtilsForKernel",0,9,5,0,3,3}, {"ThreadManForKernel",0,1,5,0,6,4}}},
    {{{0x3D5F109C,0x1B54}, {0xC86500E3,0x1B5C}, {0x5AD660FA,0x1B64},
      {0xB435DEC5,0x1B3C}, {0x34B9FA9E,0x1B44}, {0xBFA98062,0x1B4C},
      {0xD6DA4BA1,0x1B1C}, {0x28B6489C,0x1B24}, {0x3F53E640,0x1B2C}, {0x4E3A1105,0x1B34}}},
    {{{"",false,0x8000,0xD632ACDB,0x1A68}, {"",false,0x8000,0xCEE8593C,0x1ACC},
      {"",true,0x8000,0xF01D73A7,0x1C00},
      {"sceSasCore_driver",false,1,0xB0F9F98F,0x1A68}, {"sceSasCore_driver",false,1,0xE143A1EA,0x1ACC},
      {"sceSasCore",false,0x4001,0xA3589D81,0}, {"sceSasCore",false,0x4001,0x68A46B95,824},
      {"sceSasCore",false,0x4001,0x440CA7D8,1380}, {"sceSasCore",false,0x4001,0xAD84D37F,1660},
      {"sceSasCore",false,0x4001,0x99944089,1888}, {"sceSasCore",false,0x4001,0xB7660A23,2168},
      {"sceSasCore",false,0x4001,0xA232CBE6,2472}, {"sceSasCore",false,0x4001,0xD5EBBBCD,2740},
      {"sceSasCore",false,0x4001,0x019B25EB,3008}, {"sceSasCore",false,0x4001,0x9EC3676A,3372},
      {"sceSasCore",false,0x4001,0x5F9529F6,3828}, {"sceSasCore",false,0x4001,0x74AE582A,4060},
      {"sceSasCore",false,0x4001,0xCBCD4F79,4156}, {"sceSasCore",false,0x4001,0x42778A9F,5172},
      {"sceSasCore",false,0x4001,0xA0CF2FA4,1132}, {"sceSasCore",false,0x4001,0x76F01ACA,884},
      {"sceSasCore",false,0x4001,0xF983B186,5860}, {"sceSasCore",false,0x4001,0xD5A229C9,6068},
      {"sceSasCore",false,0x4001,0x33D4AB37,6312}, {"sceSasCore",false,0x4001,0x267A6DD2,6524}}}
};
void Frontier(SascoreInitializationSnapshot& s, uint32_t pc, uint32_t stub, uint32_t nid, uint32_t index) noexcept {
    s.frontier_source_pc=pc;s.frontier_stub_va=stub;s.frontier_nid=nid;s.frontier_import_index=index;
}
bool Read(const SascoreInitializationSnapshot& s, uint32_t offset, uint32_t& value) noexcept {
    value=0;
    for(uint32_t i=0;i<4;++i) { if(s.known[offset+i]!=1) return false;value|=uint32_t(s.bytes[offset+i])<<(8*i); }
    return true;
}
void Store(SascoreInitializationSnapshot& s, uint32_t pc, uint32_t offset, uint32_t value) noexcept {
    const auto ordinal=s.writes_count++;
    s.writes[ordinal]={ordinal,pc,offset,value};
    for(uint32_t i=0;i<4;++i) { s.known_bytes+=s.known[offset+i]!=1;s.known[offset+i]=1;s.bytes[offset+i]=uint8_t(value>>(8*i)); }
}
NativeSascoreCode Block(SascoreInitializationSnapshot& s, NativeSascoreCode code) noexcept {
    s.dependency_blocked=true;s.host_abort_required=true;return code;
}
}
const SascoreModuleDescriptor* FindSascoreModuleDescriptor(std::string_view hash, uint32_t size) noexcept {
    return size==Descriptor.encoded_bytes && hash==Descriptor.encoded_sha256 ? &Descriptor : nullptr;
}
bool SascoreImportPolicyAllows(const SascoreImportLibrary& row) noexcept {
    if(!row.name) return false;
    for(const auto& expected:Descriptor.imports) if(std::string_view(row.name)==expected.name)
        return row.version==expected.version && row.attributes==expected.attributes && row.words==5 &&
            row.variables==0 && row.first_function==expected.first_function && row.function_count==expected.function_count;
    return false;
}
void InitializeSascoreState(SascoreInitializationSnapshot& s) noexcept {
    s.present=true;s.known_bytes=4;
    for(uint32_t i=0;i<4;++i) { s.bytes[i]=0xFF;s.known[i]=1; }
}
NativeSascoreCode RunSascoreStart(SascoreInitializationSnapshot& s, NativeSascoreBackend* backend, std::shared_ptr<void> pin) noexcept {
    if(!s.present || s.entry_entered || s.known_bytes!=4 || s.writes_count || !s.owner || !s.generation || !s.serial)
        return NativeSascoreCode::Refused;
    s.entry_entered=true;s.host_abort_required=true;
    Frontier(s,0x1A74,0x1B5C,0xC86500E3,1);
    if(!backend || !backend->Ready()) return Block(s,NativeSascoreCode::DependencyBlocked);
    ++s.calloc_calls_entered;
    const auto memory=backend->Allocate(s.resources.key,std::move(pin),s.resources);
    if(memory!=NativeSascoreCode::Ok || !s.resources.calloc_source_returned)
        return Block(s,memory==NativeSascoreCode::Ok?NativeSascoreCode::Refused:memory);
    ++s.calloc_calls_returned;
    Store(s,0x1A98,0x10,s.resources.source_coordinate);
    if(!s.resources.source_coordinate) { s.entry_returned=true;s.entry_result=1;return NativeSascoreCode::Ok; }
    Frontier(s,0x1AB4,0x1B1C,0xD6DA4BA1,6);
    ++s.create_calls_entered;
    const auto semaphore=backend->CreatePrivateSemaphore(s.resources.key,s.resources);
    if(semaphore!=NativeSascoreCode::Ok || !s.resources.semaphore_source_returned)
        return Block(s,semaphore==NativeSascoreCode::Ok?NativeSascoreCode::Refused:semaphore);
    ++s.create_calls_returned;
    Store(s,0x1AC0,0,static_cast<uint32_t>(s.resources.semaphore_uid));
    s.entry_returned=true;s.entry_result=s.resources.semaphore_uid<0?1:0;
    if(!s.entry_result) { s.stop_domain_ready=true;s.host_abort_required=false;Frontier(s,0,0,0,0); }
    return NativeSascoreCode::Ok;
}
NativeSascoreCode RunSascoreStop(SascoreInitializationSnapshot& s, NativeSascoreBackend* backend) noexcept {
    if(!backend || !s.stop_domain_ready || !s.entry_returned || s.entry_result || s.host_aborted || s.source_stop_returned || s.stop_progress>2)
        return NativeSascoreCode::Refused;
    s.source_stop_entered=true;
    if(s.stop_progress==0) {
        uint32_t memory=0;if(!Read(s,0x10,memory)) return NativeSascoreCode::Refused;
        if(memory) {
            Frontier(s,0x1B0C,0x1B64,0x5AD660FA,2);
            if(!s.free_calls_entered) ++s.free_calls_entered;
            const auto freed=backend->FreeMemory(s.resources.key,memory,true,s.resources);
            if(freed!=NativeSascoreCode::Ok || !s.resources.free_source_returned) return freed;
            ++s.free_calls_returned;
        }
        s.stop_progress=1;
    }
    if(s.stop_progress==1) {
        uint32_t word=0;if(!Read(s,0,word)) return NativeSascoreCode::Refused;
        if((word&0x80000000u)==0) {
            Frontier(s,0x1AF4,0x1B24,0x28B6489C,7);
            if(!s.delete_calls_entered) ++s.delete_calls_entered;
            const auto deleted=backend->DeleteSemaphore(s.resources.key,static_cast<int32_t>(word),true,s.resources);
            if(deleted!=NativeSascoreCode::Ok || !s.resources.delete_source_returned) return deleted;
            ++s.delete_calls_returned;
        }
        s.stop_progress=2;
    }
    s.source_stop_returned=true;Frontier(s,0,0,0,0);return NativeSascoreCode::Ok;
}
NativeSascoreCode AbortSascoreInitialization(SascoreInitializationSnapshot& s, NativeSascoreBackend* backend) noexcept {
    if(!s.present || !s.entry_entered || !s.host_abort_required || s.source_stop_entered || s.host_aborted)
        return NativeSascoreCode::Refused;
    s.host_abort_entered=true;
    if(s.resources.issued) {
        if(!backend) return NativeSascoreCode::CleanupFailure;
        const auto retired=backend->Retire(s.resources.key,s.resources);
        if(retired!=NativeSascoreCode::Ok) return retired;
    }
    s.host_aborted=true;s.host_abort_required=false;return NativeSascoreCode::Ok;
}
} // namespace dokodemo::recovered
