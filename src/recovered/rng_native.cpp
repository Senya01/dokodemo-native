#include "rng_native.h"
#include <utility>

namespace dokodemo::recovered {
namespace {
constexpr uint32_t ScratchBase = 0x1ca8dc, CandidateBase = 0x1cc8dc;
constexpr uint32_t Cursor = 0x1ce8dc;
int32_t Signed28(uint32_t x) {
    x &= 0x0fffffff;
    return x < 0x08000000 ? static_cast<int32_t>(x)
        : static_cast<int32_t>(static_cast<int64_t>(x)-0x10000000);
}
int32_t Signed14(uint32_t x) {
    x &= 0x3fff;
    return x < 0x2000 ? static_cast<int32_t>(x)
        : static_cast<int32_t>(x)-0x4000;
}
uint32_t Bucket(uint32_t x) {
    x = static_cast<uint32_t>(Signed28(x));
    return (x*(x*x*0x3d73u+0xc0ae5u))&31u;
}
size_t Offset(uint32_t va, size_t size, uint32_t pc) {
    if (va < ScratchBase || static_cast<uint64_t>(va)-ScratchBase+size > 0x4004u
        || va%size != 0)
        throw RngDomainError(pc,"scratch mapping/alignment outside proved owner");
    return static_cast<size_t>(va-ScratchBase);
}
uint32_t ListWord(RngListView v, uint32_t n, uint32_t pc) {
    if (!v.words || n >= v.word_count)
        throw RngDomainError(pc,"list read outside supplied mapped backing");
    return v.words[n];
}
uint32_t ExclusionWord(RngExclusionView v, uint32_t n, uint32_t pc) {
    if (!v.words || n >= v.word_count)
        throw RngDomainError(pc,"exclusion read outside supplied mapped backing");
    return v.words[n];
}
}
RngDomainError::RngDomainError(uint32_t pc, const char* why)
    : std::runtime_error(why), source_pc(pc) {}
bool RngNativeOwner::Available() const noexcept {
    return storage_ && storage_->seed_available;
}
RngSnapshot RngNativeOwner::Snapshot() const {
    return storage_ ? *storage_ : RngSnapshot{};
}
void RngNativeOwner::Detach() {
    if (!storage_) storage_ = std::make_shared<RngSnapshot>();
    else if (storage_.use_count()!=1) storage_ = std::make_shared<RngSnapshot>(*storage_);
}
void RngNativeOwner::ImportCurrentState64(uint64_t state, uint64_t identity,
                                         std::string provenance) {
    if (provenance.empty()) throw RngDomainError(0xc3234,"missing state provenance");
    Detach();
    storage_->seed_available = true;
    storage_->seed = state;
    storage_->scenario_identity = identity;
    storage_->origin = RngSeedOrigin::VerifiedCurrentState64;
    storage_->provenance = std::move(provenance);
}
void RngNativeOwner::ApplyInitializerTimeT32(uint32_t word, uint64_t identity,
                                          std::string provenance) {
    ImportCurrentState64(word,identity,std::move(provenance));
    storage_->origin = RngSeedOrigin::InitializerTimeT32;
}
void RngNativeOwner::ImportInitialImageModel(uint64_t identity, std::string provenance) {
    ImportCurrentState64(1,identity,std::move(provenance));
    storage_->origin = RngSeedOrigin::InitialImageModel;
}
void RngNativeOwner::ImportScratch(const std::array<uint8_t,0x4004>& bytes) {
    Detach();
    storage_->scratch = bytes;
    storage_->scratch_available = true;
}
uint32_t RngNativeOwner::NextC3234() {
    if (!Available()) throw RngDomainError(0xc3234,"current RNG state unavailable");
    Detach();
    storage_->seed = storage_->seed*UINT64_C(0x5851f42d4c957f2d)+1u;
    return static_cast<uint32_t>(storage_->seed>>32)&0x7fffffffu;
}
uint32_t RngNativeOwner::ScratchWord(uint32_t va) const {
    if (!storage_ || !storage_->scratch_available)
        throw RngDomainError(0x773dc,"current scratch backing unavailable");
    const size_t o=Offset(va,4,0x773dc);
    const auto& b=storage_->scratch;
    return static_cast<uint32_t>(b[o]) | static_cast<uint32_t>(b[o+1])<<8
        | static_cast<uint32_t>(b[o+2])<<16 | static_cast<uint32_t>(b[o+3])<<24;
}
void RngNativeOwner::ScratchStore(uint32_t va,uint32_t word,uint32_t pc) {
    if (!storage_ || !storage_->scratch_available)
        throw RngDomainError(pc,"current scratch backing unavailable");
    const size_t o=Offset(va,4,pc);
    Detach();
    for (size_t i=0;i<4;++i) storage_->scratch[o+i]=static_cast<uint8_t>(word>>(8*i));
}
void RngNativeOwner::ScratchHalfStore(uint32_t va,uint16_t word,uint32_t pc) {
    if (!storage_ || !storage_->scratch_available)
        throw RngDomainError(pc,"current scratch backing unavailable");
    const size_t o=Offset(va,2,pc);
    Detach();
    storage_->scratch[o]=static_cast<uint8_t>(word);
    storage_->scratch[o+1]=static_cast<uint8_t>(word>>8);
}
uint32_t RngSelect77404(RngNativeOwner& owner,RngListView list,RngExclusionView ring) {
    const uint32_t header=ListWord(list,0,0x77434);
    if ((header>>28)!=9) throw RngDomainError(0x77434,"non-list numeric/string branch unclosed");
    const uint32_t count=header&0xfffu;
    uint32_t eligible=0;
    if (ring.words) {
        owner.ScratchStore(Cursor,ScratchBase,0x773d4);
        const int32_t capacity=Signed14(ExclusionWord(ring,0,0x77474));
        const uint32_t buckets=owner.ScratchWord(Cursor);
        owner.ScratchStore(Cursor,buckets+512u,0x77400);
        for (uint32_t n=0;n<32;++n) owner.ScratchStore(buckets+4*n,0,0x774ac);
        for (int32_t n=1;n<=capacity;++n) {
            const uint32_t raw=ExclusionWord(ring,static_cast<uint32_t>(n),0x774d4);
            const uint32_t value=static_cast<uint32_t>(Signed28(raw));
            const uint32_t node=owner.ScratchWord(Cursor);
            owner.ScratchStore(Cursor,node+32u,0x773e8);
            const uint32_t bucket=buckets+4*Bucket(raw);
            owner.ScratchStore(node,owner.ScratchWord(bucket),0x77524);
            owner.ScratchStore(node+4,value,0x77528);
            owner.ScratchStore(bucket,node,0x77530);
        }
        for (uint32_t n=1;n<=count;++n) {
            const uint32_t raw=ListWord(list,n,0x77564);
            const uint32_t value=static_cast<uint32_t>(Signed28(raw));
            uint32_t node=owner.ScratchWord(buckets+4*Bucket(raw));
            bool excluded=false;
            uint32_t visited=0;
            while (node) {
                if (++visited>513u) throw RngDomainError(0x775a4,"hash-chain cycle/unclosed alias");
                if (owner.ScratchWord(node+4)==value) {excluded=true;break;}
                node=owner.ScratchWord(node);
            }
            if (!excluded) {
                owner.ScratchHalfStore(CandidateBase+2*eligible,static_cast<uint16_t>(n),0x775bc);
                ++eligible;
            }
        }
    }
    const uint32_t random=owner.NextC3234();
    if (!count) throw RngDomainError(ring.words?0x7765c:0x77684,"source division break after seed store");
    uint32_t selected=random%count+1u;
    if (ring.words && eligible) {
        const uint32_t address=CandidateBase+2*(random%eligible);
        const auto bytes=owner.Snapshot().scratch;
        const size_t off=Offset(address,2,0x77600);
        const uint32_t half=static_cast<uint32_t>(bytes[off])|static_cast<uint32_t>(bytes[off+1])<<8;
        if (half>=0x8000u) throw RngDomainError(0x77600,"negative source candidate index outside list domain");
        selected=half;
    }
    if (ring.words) {
        const int32_t capacity=Signed14(ExclusionWord(ring,0,0x77364));
        const int32_t cursor=Signed14(ExclusionWord(ring,0,0x77364)>>14);
        const int64_t element=static_cast<int64_t>(cursor)+1;
        if (element<0 || static_cast<uint64_t>(element)>=ring.word_count)
            throw RngDomainError(0x7739c,"ring store outside supplied writable backing");
        ring.words[static_cast<size_t>(element)]=ListWord(list,selected,0x77398);
        if (!capacity) throw RngDomainError(0x773a4,"source zero-capacity break after seed/element store");
        const int32_t next=(cursor+1)%capacity;
        ring.words[0]=0x90000000u|(((static_cast<uint32_t>(next)<<14)
                    |static_cast<uint32_t>(capacity))&0x0fffffffu);
    }
    return selected;
}
} // namespace dokodemo::recovered
