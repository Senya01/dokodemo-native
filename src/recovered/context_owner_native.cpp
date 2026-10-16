#include "context_owner_native.h"
#include "producer_native.h"
#include <algorithm>
#include <atomic>
#include <new>
#include <stdexcept>
#include <utility>

namespace dokodemo::recovered {
namespace {
std::atomic<uint64_t> next_owner_arena{0x8000000000000000ULL};
constexpr uint32_t owner_relative = 0x1000, entries_relative = 0x1060;
ContextOwnerBoundary Boundary(ContextOwnerStop kind, uint32_t pc, const char* text) {
    ContextOwnerBoundary b; b.kind = kind; b.source_pc = pc; b.detail = text; return b;
}
const ManagerMemoryBlock* FindBlock(const ManagerSnapshot& s, uint32_t relative) {
    for (const auto& b : s.memory)
        if (relative >= b.relative && relative < b.relative + b.words.size() * 4) return &b;
    return nullptr;
}
bool ManagerWord(const ManagerSnapshot& s, uint32_t address, uint32_t& value) {
    const auto* b = FindBlock(s, address);
    if (!b || (address - b->relative) % 4 != 0) return false;
    const size_t i = (address - b->relative) / 4;
    if (!b->initialized_words[i]) return false;
    value = b->words[i]; return true;
}
}
struct OwnedContextOwner::Impl {
    uint64_t arena = next_owner_arena.fetch_add(1);
    std::unique_ptr<uint8_t[]> owner_storage, entry_storage;
    std::array<uint8_t, 96> owner_mask{};
    std::array<uint8_t, 164> entry_mask{};
    bool closed = false, tail = false, bound = false, records_match = false, kernel = false;
    ManagerToken projection, first_projected_entry;
    std::vector<ManagerAllocation> allocations;
    ContextOwnerSuffixSnapshot suffix;
    ContextOwnerConstructionSnapshot construction;
    uint64_t construction_serial = 0;
    std::vector<std::weak_ptr<ContextOwnerStorageImage>> prior_native_fork_images;
    uint64_t suffix_serial = 0;
    uint64_t generation = 0;
    ManagerToken Token(uint32_t relative) const { return {arena, relative}; }
    void Store(uint32_t off, uint32_t v, bool entry = false) {
        ++generation;
        auto* p = entry ? entry_storage.get() : owner_storage.get();
        for (uint32_t i = 0; i < 4; ++i) { p[off + i] = uint8_t(v >> (i * 8)); if (entry) entry_mask[off + i] = 1; else owner_mask[off + i] = 1; }
    }
    void Byte(uint32_t off, uint8_t v) { ++generation; owner_storage[off] = v; owner_mask[off] = 1; }
    bool Known(uint32_t off) const { for (uint32_t i = 0; i < 4; ++i) if (!owner_mask[off + i]) return false; return true; }
    uint32_t Load(uint32_t off) const {
        return uint32_t(owner_storage[off]) | (uint32_t(owner_storage[off+1]) << 8) |
            (uint32_t(owner_storage[off+2]) << 16) | (uint32_t(owner_storage[off+3]) << 24);
    }
    ContextOwnerBoundary Live() const {
        if (closed) return Boundary(ContextOwnerStop::Closed, 0, "native component lifetime has ended");
        if (kernel) return Boundary(ContextOwnerStop::KernelCall, 0x74DBC, "stopped at unresolved kernel call; continuation is unavailable");
        return {};
    }
};
std::array<ContextOwnerRecord, 5> OwnedContextOwner::CallbackRecords() {
    return {{{1, 0x202, 0xD97A4, {0x23424, 0}}, {2, 0x202, 0xD97AC, {0x2345C, 0}},
        {6, 0x202, 0xD97B4, {0x23A70, 0}}, {9, 0x402, 0xD97BC, {0x23DA0, 0}},
        {10, 0x402, 0xD97C4, {0x23E08, 0}}}};
}
namespace {
bool SuffixOverlap(uint32_t a, size_t n, uint32_t b, size_t m);
bool ConstructionAddress(uint32_t a, size_t n) {
    return a != 0 && uint64_t(a)+n <= 0x100000000ULL &&
        !SuffixOverlap(a,n,0,932728) && !SuffixOverlap(a,n,932736,1096652);
}
bool ZeroByte(uint32_t w) { return ((w+0xFEFEFEFFu) & ~w & 0x80808080u) != 0; }
}
ContextOwnerPathResult SplitConstructionPath20DC(const ContextOwnerPathInput& in) {
    ContextOwnerPathResult out; out.local = in.local; out.local_mask = in.local_mask;
    if (!ConstructionAddress(in.input_address,256) || !ConstructionAddress(in.local_address,256) ||
        SuffixOverlap(in.input_address,256,in.local_address,256) ||
        std::any_of(in.input_mask.begin(),in.input_mask.end(),[](uint8_t m){return m!=1;}) ||
        std::any_of(in.local_mask.begin(),in.local_mask.end(),[](uint8_t m){return m>1;}) ||
        std::find(in.input.begin(),in.input.end(),uint8_t(0)) == in.input.end()) {
        out.boundary = Boundary(ContextOwnerStop::NativeDomain,0x20DC,
            "bounded disjoint readable input256 and physical local256/masks required; native preflight"); return out;
    }
    auto read = [&](uint32_t pc,bool local,uint32_t off,uint32_t n) {
        uint32_t v=0; const auto& bs=local?out.local:in.input;
        for (uint32_t i=0;i<n;++i) v |= uint32_t(bs[off+i])<<(8*i);
        out.effects.push_back({pc,(local?in.local_address:in.input_address)+off,n,v,"read"});return v;
    };
    auto write = [&](uint32_t pc,uint32_t off,uint32_t n,uint32_t v) {
        out.effects.push_back({pc,in.local_address+off,n,n==1?v&255:v,"write"});
        for(uint32_t i=0;i<n;++i){out.local[off+i]=uint8_t(v>>(8*i));out.local_mask[off+i]=1;}
    };
    uint32_t i=0;
    if (((in.local_address|in.input_address)&3)==0) {
        uint32_t word=read(0xC3A08,false,0,4);
        while(!ZeroByte(word)) {
            write(0xC3A44,i,4,word); i+=4;
            word=read(0xC3A4C,false,i,4);
        }
    }
    for(;;++i) {const uint32_t c=read(0xC3A70,false,i,1);write(0xC3A78,i,1,c);if(c==0)break;}
    auto find = [&](uint32_t character) {
        uint32_t off=0;
        if ((in.local_address&3)==0) {
            const uint32_t repeated=character*0x01010101u;
            uint32_t w=read(0xC38AC,true,0,4);
            while(!ZeroByte(w) && !ZeroByte(w^repeated)) {off+=4;w=read(0xC3900,true,off,4);}
        }
        uint32_t c=read(0xC3938,true,off,1);
        while(c && c!=character){++off;c=read(0xC3954,true,off,1);}
        return c==character?in.local_address+off:0u;
    };
    uint32_t match=find(92);if(!match)match=find(47);
    if(match){write(0x2108,match-in.local_address,1,0);out.return_value=match+1;}
    return out;
}
ContextOwnerFile64518Result ExecuteConstructionFile64518FromAbiEvidence(
        const ContextOwnerFile64518Input& in) {
    ContextOwnerFile64518Result out;
    if ((in.object_address!=0xF6B98 && !ConstructionAddress(in.object_address,4)) || (in.object_address&3) ||
        !ConstructionAddress(in.path_address,1) ||
        (in.size_output_address && (!ConstructionAddress(in.size_output_address,4) || (in.size_output_address&3))) ||
        in.read_bytes.size()!=in.read_mask.size() || in.read_bytes.size()>1048576 ||
        std::any_of(in.read_mask.begin(),in.read_mask.end(),[](uint8_t m){return m>1;})) {
        out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x64518,"invalid explicit conditional file ABI domain");return out;
    }
    size_t cursor=0;
    auto read=[&](uint32_t pc,uint32_t address,uint32_t v){out.effects.push_back({pc,address,4,v,"read"});};
    auto call=[&](uint32_t pc,uint32_t callee,std::array<uint32_t,4> args,uint32_t& value) {
        if(cursor==in.evidence.size()) {
            out.boundary=Boundary(ContextOwnerStop::InitializationCall,pc,"explicit imported/virtual return evidence absent; no return synthesized");
            out.boundary.callee=callee;out.boundary.arguments=args;return false;
        }
        const auto& e=in.evidence[cursor];
        if(e.call_pc!=pc || e.callee!=callee || e.arguments!=args) {
            out.boundary=Boundary(ContextOwnerStop::NativeDomain,pc,"exact ordered ABI tuple mismatch; prior effects are retained");return false;
        }
        ++cursor;out.consumed.push_back(e);value=e.result;return true;
    };
    auto finish=[&](uint32_t value) {
        out.return_value=value;
        if(cursor!=in.evidence.size())out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x645E0,"unused ABI tuple; no implicit extra calls");
        else out.completed=true;
    };
    const uint32_t obj=in.object_address,path=in.path_address,vt=0xDFAB8;
    uint32_t value=0,size=0,pointer=0;
    read(0x64540,obj,vt);read(0x6454C,vt+0x24,0x642C8);
    if(!call(0x64550,0x642C8,{obj,path,1,0xFFFFFFFF},value))return out;
    if(value==0xFFFFFFFF){finish(0);return out;}
    read(0x64560,obj,vt);read(0x64564,vt+0x3C,0x644FC);
    if(!call(0x64568,0x644FC,{obj,path,1,0xFFFFFFFF},size))return out;
    auto close=[&](bool loaded) {
        read(loaded?0x645B4:0x645F8,obj,vt);read(loaded?0x645B8:0x645FC,vt+0x40,0x644A4);
        return call(loaded?0x645BC:0x64600,0x644A4,loaded?std::array<uint32_t,4>{obj,pointer,size,1}:std::array<uint32_t,4>{obj,path,1,0xFFFFFFFF},value);
    };
    if(size==0xFFFFFFFF){if(close(false))finish(0);return out;}
    if(in.size_output_address){out.size_written=true;out.size_value=size;out.effects.push_back({0x6457C,in.size_output_address,4,size,"write"});}
    if(!call(0x64580,0x63D54,{size,path,1,0xFFFFFFFF},pointer))return out;
    if(!pointer){if(close(false))finish(0);return out;}
    if(pointer!=in.allocation_address || size==0 || size>1048576 || in.read_bytes.size()!=size ||
        !ConstructionAddress(pointer,size) || (pointer&3) || SuffixOverlap(pointer,size,obj,4) ||
        SuffixOverlap(pointer,size,path,256) || (in.size_output_address&&SuffixOverlap(pointer,size,in.size_output_address,4))) {
        out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x64588,"fresh bounded disjoint allocation and exact read-byte/mask premise required");return out;
    }
    out.image=std::make_shared<ContextOwnerStorageImage>();out.image->source_address=pointer;out.image->live=true;
    out.image->identity=next_owner_arena.fetch_add(1);
    out.image->bytes.assign(size,in.allocator_fill);out.image->initialized.assign(size,0);
    read(0x64590,obj,vt);read(0x6459C,vt+0x2C,0x64408);
    if(!call(0x645A4,0x64408,{obj,pointer,size,1},value))return out;
    for(uint32_t j=0;j<size;++j)if(in.read_mask[j]) {
        out.image->bytes[j]=in.read_bytes[j];out.image->initialized[j]=1;
        out.effects.push_back({0x64408,pointer+j,1,in.read_bytes[j],"write"});
    }
    if(value==0xFFFFFFFF) {
        if(!call(0x645E8,0x63F04,{pointer,pointer,size,1},value))return out;
        out.allocation_released=true;out.image->live=false;
        if(close(true))finish(0);
        return out;
    }
    // Any non-minus-one count, including short/zero, follows original success.
    if(close(true))finish(pointer);
    return out;
}
ContextOwnerHeapResult AllocateConstructionVm63D14(const ContextOwnerHeapCheckpoint& in,uint32_t requested) {
    ContextOwnerHeapResult out;out.blocks=in.blocks;
    struct Cut { uint32_t pc; };
    if((in.controller!=0x1C93D4 && !ConstructionAddress(in.controller,64)) || requested==0 || requested>0x100000 || out.blocks.empty()) {
        out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x63D14,"bounded positive direction1 heap checkpoint required");return out;
    }
    for(size_t i=0;i<out.blocks.size();++i) {
        const auto& b=out.blocks[i];
        if(b.bytes.empty() || b.bytes.size()>1048576 || b.bytes.size()!=b.initialized.size() || !b.writable ||
            ((b.address!=in.controller || b.bytes.size()>64) && !ConstructionAddress(b.address,b.bytes.size())) ||
            std::any_of(b.initialized.begin(),b.initialized.end(),[](uint8_t m){return m>1;})) {
            out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x63D14,"invalid explicit writable heap backing/masks");return out;
        }
        for(size_t j=0;j<i;++j)if(SuffixOverlap(b.address,b.bytes.size(),out.blocks[j].address,out.blocks[j].bytes.size())) {
            out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x63D14,"heap aliases excluded before source effects");return out;
        }
    }
    auto block=[&](uint32_t pc,uint32_t a,uint32_t n)->ContextOwnerPreparedBlock& {
        for(auto& b:out.blocks)if(a>=b.address && uint64_t(a)+n<=uint64_t(b.address)+b.bytes.size())return b;
        throw Cut{pc};
    };
    auto rd=[&](uint32_t pc,uint32_t a,uint32_t n) {
        auto& b=block(pc,a,n);uint32_t v=0;
        for(uint32_t j=0;j<n;++j){if(!b.initialized[a-b.address+j])throw Cut{pc};v|=uint32_t(b.bytes[a-b.address+j])<<(8*j);}
        out.effects.push_back({pc,a,n,v,"read"});return v;
    };
    auto wr=[&](uint32_t pc,uint32_t a,uint32_t n,uint32_t v) {
        auto& b=block(pc,a,n);out.effects.push_back({pc,a,n,n==1?v&255:n==2?v&65535:v,"write"});
        for(uint32_t j=0;j<n;++j){b.bytes[a-b.address+j]=uint8_t(v>>(8*j));b.initialized[a-b.address+j]=1;}
    };
    try {
        const uint32_t h=in.controller;
        if(rd(0x63324,h+16,1)==0)throw Cut{0x63368};
        const uint32_t rounded=(requested+15)&0xFFFFFFF0u;
        const uint32_t begin=rd(0x62FD0,h+4,4);
        const uint32_t node=rd(0x62FD4,begin+20,4);
        if(!node)throw Cut{0x63004};
        const uint32_t capacity=rd(0x62FE4,node+24,4);
        if(capacity<rounded)throw Cut{0x62FF4};
        // This finite source domain excludes the non-splitting removal path.
        if(capacity<rounded+32)throw Cut{0x630D8};
        wr(0x63030,node+14,1,1);
        const uint32_t tag=rd(0x63038,h+12,2);
        wr(0x6303C,node+8,4,requested);wr(0x63044,node+12,2,tag);
        uint32_t r3=rd(0x63048,node+20,4),r2=rd(0x6304C,node+4,4);
        const uint32_t sum=node+rounded,newnode=sum+16;
        if(newnode<node)throw Cut{0x63054};
        wr(0x63058,newnode+20,4,r3);wr(0x6305C,newnode+4,4,r2);
        r3=rd(0x63060,node+16,4);r2=rd(0x63064,node+20,4);
        const uint32_t next=rd(0x63068,node+4,4);
        wr(0x6306C,node+4,4,newnode);wr(0x63070,newnode+16,4,r3);wr(0x63074,r2+16,4,newnode);
        r3=rd(0x6307C,newnode+4,4);const uint32_t previous=rd(0x63080,node+16,4);
        wr(0x63084,sum+16,4,node);wr(0x63090,newnode+24,4,r3-newnode-16);
        wr(0x63094,newnode+15,1,0x79);wr(0x63098,next,4,newnode);wr(0x6309C,previous+20,4,newnode);
        wr(0x630A0,newnode+14,1,0);wr(0x630A4,newnode+12,2,0);wr(0x630A8,newnode+8,4,0);
        (void)rd(0x630AC,node+12,2);
        //5156C is the actual compiled no-op variadic logger: only ABI spills.
        wr(0x63340,h+12,4,0);
        out.return_value=node+16;out.payload_allocator_initialized.assign(requested,0);out.completed=true;
    } catch(const Cut& c) {out.boundary=Boundary(ContextOwnerStop::NativeDomain,c.pc,"finite heap-domain cut; prior source effects retained, no allocator success fabricated");}
    return out;
}
ContextOwnerConstructorResult ExecuteConstruction6656CToCallback(ContextOwnerConstructorDomain in) {
    ContextOwnerConstructorResult out;out.context=std::move(in.context);out.globals=std::move(in.globals);
    out.dynamic_heap=std::move(in.dynamic_heap);out.registry_heap=std::move(in.registry_heap);
    const uint32_t ctx=out.context.address;
    out.context_source_written.assign(0x47C,0);
    auto& d=in.loader;
    struct Cut {uint32_t pc;};
    auto get=[&](uint32_t pc,uint32_t a,uint32_t n)->ContextOwnerPreparedBlock& {
        for(auto* b:{&out.context,&out.globals})if(a>=b->address && uint64_t(a)+n<=uint64_t(b->address)+b->bytes.size())return *b;
        for(auto* h:{&out.dynamic_heap,&out.registry_heap})for(auto& b:h->blocks)
            if(a>=b.address && uint64_t(a)+n<=uint64_t(b.address)+b.bytes.size())return b;
        throw Cut{pc};
    };
    auto rd=[&](uint32_t pc,uint32_t a,uint32_t n){auto& b=get(pc,a,n);uint32_t v=0;for(uint32_t j=0;j<n;++j){if(b.initialized[a-b.address+j]!=1)throw Cut{pc};v|=uint32_t(b.bytes[a-b.address+j])<<(8*j);}out.prefix_effects.push_back({pc,a,n,v,"read"});return v;};
    auto store=[&](ContextOwnerPreparedBlock& b,uint32_t a,uint32_t n,uint32_t v){for(uint32_t j=0;j<n;++j){b.bytes[a-b.address+j]=uint8_t(v>>(8*j));b.initialized[a-b.address+j]=1;}};
    auto wr=[&](uint32_t pc,uint32_t a,uint32_t n,uint32_t v){auto& b=get(pc,a,n);if(!b.writable)throw Cut{pc};store(b,a,n,v);out.prefix_effects.push_back({pc,a,n,n==1?v&255:n==2?v&65535:v,"write"});
        // A VM payload carved from registry_heap is the SAME physical bytes.
        if(a>=ctx && uint64_t(a)+n<=uint64_t(ctx)+out.context.bytes.size()) {
            for(uint32_t j=0;j<n;++j)out.context_source_written[a-ctx+j]=1;
            for(auto& x:out.registry_heap.blocks)if(a>=x.address&&uint64_t(a)+n<=uint64_t(x.address)+x.bytes.size())store(x,a,n,v);
        }
    };
    auto allocation=[&](ContextOwnerHeapCheckpoint& h,uint32_t size,uint32_t wrapper){
        if(wrapper==0x7C714 || wrapper==0x7C754)out.prefix_effects.push_back({wrapper==0x7C714?0x7C728u:0x7C768u,0x1CE8E8,4,h.controller,"read"});
        auto r=AllocateConstructionVm63D14(h,size);out.prefix_effects.insert(out.prefix_effects.end(),r.effects.begin(),r.effects.end());
        h.blocks=std::move(r.blocks);if(!r.completed)throw Cut{r.boundary.source_pc};return r.return_value;
    };
    auto region=[&](uint32_t address,uint32_t n,uint64_t identity){CurrentTablesRegion r;r.base=address;r.identity=identity;r.live=true;
        auto& b=get(0x66FDC,address,n);const uint32_t off=address-b.address;
        r.bytes.assign(b.bytes.begin()+off,b.bytes.begin()+off+n);r.readable.assign(b.initialized.begin()+off,b.initialized.begin()+off+n);r.writable.assign(n,1);return r;};
    try {
        if(!ConstructionAddress(ctx,0x47C) || out.context.bytes.size()!=0x47C || out.context.initialized.size()!=0x47C || !out.context.writable ||
           out.globals.address!=0x1C9454 || out.globals.bytes.size()<0x50 || out.globals.bytes.size()!=out.globals.initialized.size() || !out.globals.writable ||
           !d.borrowed_image || !d.borrowed_image->live || d.mapping.module_base!=0 || d.mapping.callback!=0 ||
           !d.mapping.allocator_boundary_available || !d.mapping.borrowed_image_lifetime_available)throw Cut{0x6656C};
        for(auto& b:out.registry_heap.blocks)if(ctx>=b.address && uint64_t(ctx)+0x47C<=uint64_t(b.address)+b.bytes.size()) {
            if(!std::equal(out.context.bytes.begin(),out.context.bytes.end(),b.bytes.begin()+(ctx-b.address)))throw Cut{0x6656C};
        }
        wr(0x66580,ctx+0x30,4,0);wr(0x66584,ctx+0x48,2,16);
        const uint32_t vector=allocation(out.dynamic_heap,260,0x7C754),entries=vector+4;
        wr(0x665C0,vector,4,16);
        for(uint32_t j=0;j<16;++j){wr(0x665D4,entries+16*j+8,4,0);wr(0x665DC,entries+16*j+12,4,0);}
        wr(0x665E8,ctx+0x44,4,entries);wr(0x665F4,ctx+0x4A,2,0);wr(0x665FC,ctx+0x4C,2,0);
        uint32_t slot=0;for(;slot<8;++slot)if(rd(0x65C3C,0x1C947C+4*slot,4)==0)break;
        if(slot==8)throw Cut{0x65C5C};
        wr(0x65C78,0x1C947C+4*slot,4,ctx);wr(0x66018,ctx+0x50,2,slot);
        constexpr uint32_t zeros[]={0,4,8,12,16,20,24,28};
        for(uint32_t j=0;j<8;++j)wr(0x6601C+4*j,ctx+zeros[j],4,0);
        wr(0x6603C,ctx+0x64,2,0);wr(0x66044,ctx+0x24,4,0);
        const uint32_t args=allocation(out.dynamic_heap,68,0x7C754);wr(0x66058,args,4,16);
        for(uint32_t j=0;j<16;++j)wr(0x6606C,args+4+4*j,4,0);
        wr(0x66080,ctx+0x2C,4,args+4);wr(0x66090,ctx+0x5A,2,512);
        const uint32_t stack=allocation(out.dynamic_heap,2052,0x7C754);wr(0x660A0,stack,4,512);
        for(uint32_t j=0;j<512;++j)wr(0x660B8,stack+4+4*j,4,0);
        wr(0x660D0,ctx+0x20,4,stack+4);wr(0x660D4,ctx+0x5E,2,0xFFFF);wr(0x660D8,ctx+0x54,2,1);
        wr(0x660DC,ctx+0x478,4,0);wr(0x660E0,ctx+0x5C,2,0);wr(0x660E4,ctx+0x62,2,0);wr(0x660E8,ctx+0x60,2,0);
        wr(0x660EC,ctx+0x34,4,0);wr(0x660F0,ctx+0x38,1,0);wr(0x660F4,ctx+0x56,2,0);wr(0x660F8,ctx+0x58,2,1);
        wr(0x660FC,ctx+0x46E,1,0);wr(0x66100,ctx+0x68,2,0);wr(0x66104,ctx+0x6A,2,0);wr(0x66108,ctx+0x52,2,0);wr(0x6610C,ctx+0x6C,2,0);
        const uint32_t node=allocation(out.registry_heap,12,0x63D14);
        const uint32_t previous=rd(0x66614,0x1C9474,4);
        wr(0x66618,node+8,4,ctx);wr(0x6661C,node+4,4,previous);
        (void)rd(0x66620,0x1C9474,4);wr(0x66628,node,4,0x1C9470);
        if(previous)wr(0x6662C,previous,4,node);
        wr(0x66630,0x1C9474,4,node);
        out.reached_loader=true;
        auto preview=AllocateConstructionVm63D14(out.dynamic_heap,8);if(!preview.completed)throw Cut{0x67024};
        const uint32_t code=preview.return_value;
        const auto& bytes=d.borrowed_image->bytes;if(bytes.size()<28)throw Cut{0x66FDC};
        const uint32_t locals=uint32_t(bytes[22])|(uint32_t(bytes[23])<<8);
        ContextOwnerHeapCheckpoint preview_heap=out.dynamic_heap;preview_heap.blocks=std::move(preview.blocks);
        preview=AllocateConstructionVm63D14(preview_heap,4+4*locals);if(!preview.completed)throw Cut{0x6706C};
        const uint32_t local=preview.return_value;
        d.mapping.context_base=ctx;d.mapping.context_identity=ctx;d.mapping.stack_base=stack+4;d.mapping.stack_identity=stack;
        d.mapping.stack_count=512;d.mapping.frame_pointer=-1;d.mapping.image_base=d.borrowed_image->source_address;d.mapping.image_identity=d.borrowed_image->identity;
        d.mapping.code_object_base=code;d.mapping.code_identity=code;d.mapping.local_allocation_base=local;d.mapping.local_identity=local;
        d.context=region(ctx,0x47C,ctx);d.stack=region(stack+4,2048,stack);d.image=d.borrowed_image->bytes;
        d.allocation_hook=[&](uint32_t pc,uint32_t wrapper,uint32_t size,uint32_t expected){const uint32_t actual=allocation(out.dynamic_heap,size,wrapper);if(actual!=expected)throw Cut{pc};return region(actual,size,actual);};
        out.tables=std::make_shared<CurrentTablesOwner>(CurrentTablesOwner::Load(d));
        out.loader_completed=out.tables->available();
        if(out.loader_completed) {
            for(const auto& e:out.tables->effects())if(e.kind==CurrentTablesEffect::Kind::Write && e.address>=ctx && uint64_t(e.address)+e.width<=uint64_t(ctx)+0x47C)
                for(uint32_t j=0;j<e.width;++j)out.context_source_written[e.address-ctx+j]=1;
            // Merge the disjoint loader-owned payload views into the actual
            // allocator backing for the final full physical checkpoint.
            for(const auto& r:out.tables->memory().Capture()) {
                for(auto* h:{&out.dynamic_heap,&out.registry_heap})for(auto& b:h->blocks)
                    if(r.base>=b.address && uint64_t(r.base)+r.bytes.size()<=uint64_t(b.address)+b.bytes.size())
                        for(uint32_t j=0;j<r.bytes.size();++j){b.bytes[r.base-b.address+j]=r.bytes[j];b.initialized[r.base-b.address+j]=r.readable[j];}
                if(r.base>=out.globals.address && uint64_t(r.base)+r.bytes.size()<=uint64_t(out.globals.address)+out.globals.bytes.size())
                    for(uint32_t j=0;j<r.bytes.size();++j){out.globals.bytes[r.base-out.globals.address+j]=r.bytes[j];out.globals.initialized[r.base-out.globals.address+j]=r.readable[j];}
            }
            out.context.bytes=out.tables->memory().Bytes(ctx);out.context.initialized=out.tables->memory().Readable(ctx);
            out.callback_pointer=rd(0x66648,0x1C9454,4);
            out.boundary=Boundary(ContextOwnerStop::InitializationCall,0x6664C,"actual constructor prefix/loader reached unknown callback; no callback or constructor return");
            out.boundary.callee=out.callback_pointer;
        }
    } catch(const Cut& c){out.boundary=Boundary(ContextOwnerStop::NativeDomain,c.pc,"constructor finite-domain cut; ordered prior effects retained");}
      catch(const std::exception& e){out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x66FDC,e.what());}
    return out;
}
OwnedContextOwner::OwnedContextOwner(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OwnedContextOwner::~OwnedContextOwner() {
    try { Close(); } catch(...) {
        // Lifetime invalidation cannot depend on allocating diagnostic history.
        if(impl_->construction.file.image)impl_->construction.file.image->live=false;
        for(auto& weak:impl_->prior_native_fork_images)if(auto image=weak.lock())image->live=false;
    }
}
ContextOwnerCreateResult OwnedContextOwner::Create(ManagerAllocationPlan plan) {
    ContextOwnerCreateResult out;
    if (plan.successful_allocations_before_failure < -1) {
        out.boundary = Boundary(ContextOwnerStop::NativeDomain, 0, "invalid native allocation plan"); return out;
    }
    std::unique_ptr<Impl> s;
    uint32_t stage = 0;
    try {
        s = std::make_unique<Impl>(); s->allocations.reserve(2);
        auto allocate = [&](uint32_t bytes, uint32_t relative, const char* purpose) {
            if (plan.successful_allocations_before_failure == 0) throw std::bad_alloc();
            auto p = std::unique_ptr<uint8_t[]>(new uint8_t[bytes]);
            s->allocations.push_back({relative, bytes, purpose});
            if (plan.successful_allocations_before_failure > 0) --plan.successful_allocations_before_failure;
            ++stage; return p;
        };
        s->owner_storage = allocate(96, owner_relative, "CKingKai96 callback owner;18380 allocator request");
        //923C8 fixed five-entry base construction inside the96-byte allocation.
        s->Store(0, 0xE1800); s->Store(4, 0); s->Store(8, 0); s->Store(12, 0);
        s->Store(16, 2); s->Store(20, 5);
        s->entry_storage = allocate(164, entries_relative, "923C8 cookie4 + five32-byte entries");
        s->Store(0, 5, true);
        for (uint32_t i = 0; i < 5; ++i) {
            s->Store(4 + i * 32, 0xE1818, true);
            for (uint32_t f = 4; f < 32; f += 4) s->Store(4 + i * 32 + f, 0, true);
        }
        s->Store(24, entries_relative + 4); s->Store(0, 0xD98E0); //230D8 derived vtable.
        //230EC first five926EC success writes; local registry is the true owner.
        //Queue effects belong to an explicitly bound manager, not this storage.
        const auto records = CallbackRecords();
        for (uint32_t i = 0; i < 5; ++i) {
            const uint32_t off = 4 + i * 32;
            s->Store(off + 16, records[i].mask, true); s->Store(off + 20, owner_relative, true);
            s->Store(off + 12, 0, true); s->Store(off + 24, records[i].descriptor.function, true);
            s->Store(off + 28, records[i].descriptor.adjustment, true);
        }
        out.owner.reset(new OwnedContextOwner(std::move(s)));
        out.boundary = Boundary(ContextOwnerStop::InitializationCall, 0x2318C,
            "230EC initialization stops before1B188; portable payload/records are live, tail fields remain unknown");
        out.boundary.callee = 0x1B188; return out;
    } catch (const std::bad_alloc&) {
        if (s) out.released_allocations = s->allocations;
        out.boundary = Boundary(ContextOwnerStop::NativeAllocationFailure, stage == 1 ? 0x92418 : 0x18388,
            "native transactional RAII refusal; no dangling payload published");
        out.original_failure_effect = stage == 0 ?
            "18388 NULL skips230AC then18394 uses preexisting singleton; its availability is unknown" :
            "923C8 array NULL adjusts entries to4, may enqueue0/publish singleton;230EC registration can fault at address24";
        return out;
    }
}
ContextOwnerBoundary OwnedContextOwner::ApplyInitializationTailFromCheckpoint() {
    auto& s = *impl_; if (auto b = s.Live(); b.kind != ContextOwnerStop::None) return b;
    //23210..2324C after explicitly supplied external checkpoint, not fresh start.
    s.Store(0x50, 0xFFFFFFFF); s.Store(0x2C, 2000); s.Store(0x5C, 0);
    s.Byte(0x40, 0); s.Byte(0x48, 0); s.Store(0x24, 0); s.Store(0x28, 0);
    s.Store(0x54, 0xFFFFFFFF); s.Store(0x58, 0); s.Store(0x1C, 0); s.Store(0x20, 0);
    s.tail = true; return {};
}
namespace {
bool SuffixOverlap(uint32_t a, size_t n, uint32_t b, size_t m) {
    return uint64_t(a) < uint64_t(b) + m && uint64_t(b) < uint64_t(a) + n;
}
bool SuffixDynamicAddress(uint32_t a, size_t n) {
    if (!a || (a & 3) || uint64_t(a) + n > 0x100000000ULL) return false;
    // Original BOOT program ranges, including BSS. Explicit globals are separate.
    return !SuffixOverlap(a,n,0,932728) && !SuffixOverlap(a,n,932736,1096652);
}
uint32_t SuffixWord(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
bool SuffixKnown(const uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) if (p[i] != 1) return false;
    return true;
}
ContextOwnerPreparedBlock* SuffixBlock(std::vector<ContextOwnerPreparedBlock>& blocks,
                                     uint32_t address, uint32_t width) {
    for (auto& b : blocks)
        if (address >= b.address && uint64_t(address) + width <= uint64_t(b.address) + b.bytes.size()) return &b;
    return nullptr;
}
bool SuffixRead(std::vector<ContextOwnerPreparedBlock>& blocks, uint32_t address,
                uint32_t width, uint32_t& value) {
    auto* b = SuffixBlock(blocks,address,width);
    if (!b || !SuffixKnown(b->initialized.data() + (address-b->address),width)) return false;
    value = 0;
    for (uint32_t i = 0; i < width; ++i) value |= uint32_t(b->bytes[address-b->address+i]) << (8*i);
    return true;
}
}
ContextOwnerPrefixResult OwnedContextOwner::ContinueInitialization23194AfterProducer(
        const OwnedProducer& producer, const ContextOwnerLoadCheckpoint& load) const {
    ContextOwnerPrefixResult out;
    const auto& s = *impl_;
    if (auto b = s.Live(); b.kind != ContextOwnerStop::None) { out.boundary = b; return out; }
    const auto p = producer.Snapshot();
    const auto& g = p.globals;
    const auto found = std::find_if(p.live_blocks.begin(),p.live_blocks.end(),[&](const auto& b) {
        return g[5].initialized && b.address == g[5].value;
    });
    // Check actual component storage/history rather than a caller-forged bool
    // claiming1B188 returned. This does not prove the allocator implementation.
    bool live = !p.closed && found != p.live_blocks.end() &&
        g[0].initialized && g[1].initialized && g[1].value == g[0].value &&
        g[2].initialized && g[2].value == 0x1C0FC &&
        g[3].initialized && g[3].value == 0x1C184 &&
        g[4].initialized && g[4].value == 0x1DF00 &&
        !p.checkpoints.empty() && p.checkpoints.back().source_pc == 0x1B208 &&
        p.checkpoints.back().address == g[5].value+0x5C;
    if (live) {
        live = SuffixKnown(found->initialized.data(),0x6C);
        for (uint32_t i = 0; live && i < 0x6C; ++i) {
            const bool sentinel = (i >= 0x64 && i < 0x68) || (i >= 0x4C && i < 0x50) || (i >= 0x5C && i < 0x60);
            live = found->bytes[i] == (sentinel ? 0xFF : 0);
        }
    }
    if (!live) {
        out.boundary = Boundary(ContextOwnerStop::NativeDomain,0x2318C,
            "requires actual live producer immediately after successful1B188; no synthetic return accepted");
        out.boundary.callee = 0x1B188; return out;
    }
    if (!SuffixDynamicAddress(load.source_address,0x4F4) ||
        !SuffixDynamicAddress(load.source_stack_address,0x140) ||
        SuffixOverlap(load.source_address,0x4F4,load.source_stack_address,0x140) ||
        SuffixOverlap(load.source_address,0x4F4,found->address,0x6C) ||
        SuffixOverlap(load.source_stack_address,0x140,found->address,0x6C) ||
        !SuffixKnown(load.initialized.data()+0x58,4)) {
        out.boundary = Boundary(ContextOwnerStop::NativeDomain,0x23194,
            "explicit disjoint readable CLoadMan4F4 and writable source stack checkpoint required"); return out;
    }
    const uint32_t index = SuffixWord(load.bytes.data()+0x58);
    if (index >= 5) {
        out.boundary = Boundary(ContextOwnerStop::NativeDomain,0x18D74,
            "bounded original five-name table domain; source out-of-range effects unmodeled"); return out;
    }
    constexpr const char* directories[] = {"KSNEKO","KSROBO","KSUSAGI","KSINU","KSKAERU"};
    constexpr const char* filenames[] = {"neko.ksc","robo.ksc","usagi.ksc","inu.ksc","kaeru.ksc"};
    constexpr uint32_t names[] = {0xD8E4C,0xD8E58,0xD8E64,0xD8E70,0xD8E78};
    out.entry_pc = 0x23194; out.producer_return_pc = 0x23194;
    out.directory_address = 0xE7F44 + 20*(index+3); out.filename_address = names[index];
    out.directory = directories[index]; out.filename = filenames[index];
    out.transcript.reserve(20);
    out.transcript.push_back({0x23194,0x10CDC4,4,load.source_address,"read-global"});
    out.transcript.push_back({0x23198,load.source_address+0x58,4,index,"read-loadman"});
    out.transcript.push_back({0x2319C,0x25438,0,index+3,"call"});
    out.transcript.push_back({0x2544C,0x231A4,0,out.directory_address,"return"});
    out.transcript.push_back({0x231A4,0x10CDC4,4,load.source_address,"read-global"});
    out.transcript.push_back({0x231AC,0x18D48,0,load.source_stack_address+0x100,"call"});
    out.transcript.push_back({0x18D60,load.source_address+0x58,4,index,"read-loadman"});
    out.transcript.push_back({0x18D74,0xD8E84+4*index,4,names[index],"read-name-table"});
    for (uint32_t i = 0; i <= out.filename.size(); ++i) {
        const uint8_t c = i == out.filename.size() ? 0 : uint8_t(out.filename[i]);
        out.filename_scratch[i] = c; out.filename_scratch_mask[i] = 1;
    }
    out.transcript.push_back({0x18D84,0x231B4,0,load.source_stack_address+0x100,"return"});
    out.boundary = Boundary(ContextOwnerStop::InitializationCall,0x231C4,
        "actual23194 prefix reached C36C0 formatter; load2E7C, VM constructor6656C and callback remain unexecuted");
    out.boundary.callee = 0xC36C0;
    out.boundary.arguments = {load.source_stack_address,0xD97CC,out.directory_address,load.source_stack_address+0x100};
    return out;
}
ContextOwnerConstructionResult OwnedContextOwner::BeginActualConstruction23194(
        const OwnedProducer& producer,const ContextOwnerLoadCheckpoint& load,
        const ContextOwnerConstructionInputs& in) {
    auto& s=*impl_;ContextOwnerConstructionResult out;
    if(auto b=s.Live();b.kind!=ContextOwnerStop::None){out.boundary=b;return out;}
    auto prefix=ContinueInitialization23194AfterProducer(producer,load);
    if(prefix.boundary.source_pc!=0x231C4 || prefix.boundary.callee!=0xC36C0){out.boundary=prefix.boundary;return out;}
    if(s.construction.started || s.tail || s.suffix.prepared || !ConstructionAddress(in.source_owner_address,96) ||
        !ConstructionAddress(in.manager_address,288) || in.manager_count!=0 ||
        !in.initial_storage_pointer.available || !in.initial_storage_index.available ||
        load.source_stack_address<0x140 ||
        !ConstructionAddress(load.source_stack_address-0x130,0x270)) {
        out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x23194,"fresh owner and explicit empty-directory/physical storage checkpoint required");return out;
    }
    if(in.initial_storage_pointer.value) {
        const auto& image=in.preexisting_image;
        if(!image || !image->live || image->source_address!=in.initial_storage_pointer.value || !image->identity ||
            image->bytes.empty() || image->bytes.size()!=image->initialized.size() ||
            !ConstructionAddress(image->source_address,image->bytes.size()) ||
            SuffixOverlap(image->source_address,image->bytes.size(),load.source_stack_address-0x130,0x270) ||
            std::any_of(image->initialized.begin(),image->initialized.end(),[](uint8_t m){return m>1;})) {
            out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x2EEC,"exact live preexisting shared source image required; no fresh-load inference");return out;
        }
    } else if(!in.file_object_lazy_flag.available || in.file_object_lazy_flag.value!=1 ||
              in.file.object_address!=0xF6B98 || in.file.path_address!=load.source_stack_address || in.file.size_output_address!=0) {
        out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x2754,"explicit initialized file-object byte1 and exact64518 input domain required");return out;
    }
    auto& q=s.construction;q.started=true;q.storage_pointer=in.initial_storage_pointer.value;
    q.storage_index=in.initial_storage_index.value;q.effects=prefix.transcript;
    q.formatted_path=prefix.directory+"\\"+prefix.filename;
    // Bounded actual format %s\\%s: same memcpy data widths/order. Formatter
    // parsing/FILE scratch/ABI spills are not claimed by this projection.
    uint32_t output_offset=0;
    auto copy=[&](const std::string& text,uint32_t from) {
        uint32_t j=0;const uint32_t to=load.source_stack_address+output_offset;
        if(text.size()>=8) {
            for(;j+4<=text.size();j+=4) {
                uint32_t word=0;for(uint32_t k=0;k<4;++k)word|=uint32_t(uint8_t(text[j+k]))<<(8*k);
                if(((from|to)&3)==0) {
                    q.effects.push_back({0xC2E80,from+j,4,word,"read"});q.effects.push_back({0xC2E8C,to+j,4,word,"write"});
                } else {
                    constexpr uint32_t reads[]={0xC2F44,0xC2F4C,0xC2F58,0xC2F64};
                    constexpr uint32_t writes[]={0xC2F70,0xC2F78,0xC2F80,0xC2F88};
                    for(uint32_t k=0;k<4;++k)q.effects.push_back({reads[k],from+j+3-k,1,uint8_t(text[j+3-k]),"read"});
                    for(uint32_t k=0;k<4;++k)q.effects.push_back({writes[k],to+j+k,1,uint8_t(text[j+k]),"write"});
                }
            }
        }
        for(;j<text.size();++j){q.effects.push_back({0xC2EC0,from+j,1,uint8_t(text[j]),"read"});q.effects.push_back({0xC2ECC,to+j,1,uint8_t(text[j]),"write"});}
        output_offset+=static_cast<uint32_t>(text.size());
    };
    copy(prefix.directory,prefix.directory_address);copy("\\",0xD97CE);copy(prefix.filename,load.source_stack_address+0x100);
    q.effects.push_back({0xC372C,load.source_stack_address+output_offset,1,0,"write"});
    ContextOwnerPathInput path;path.input_address=load.source_stack_address;path.local_address=load.source_stack_address-0x130;
    path.input.fill(0xA5);path.input_mask.fill(1);path.local.fill(0xA5);path.local_mask.fill(1);
    std::copy(q.formatted_path.begin(),q.formatted_path.end(),path.input.begin());path.input[q.formatted_path.size()]=0;
    q.split=SplitConstructionPath20DC(path);
    if(q.split.boundary.kind!=ContextOwnerStop::None){out.boundary=q.split.boundary;return out;}
    q.effects.insert(q.effects.end(),q.split.effects.begin(),q.split.effects.end());
    q.effects.push_back({0x2EC0,0xF6980,4,in.manager_address,"read"});
    q.effects.push_back({0x2D44,in.manager_address+0x118,4,0,"read"});
    q.storage_index=0xFFFFFFFF;q.effects.push_back({0x2EDC,0x10E450,4,q.storage_index,"write"});
    q.effects.push_back({0x2EEC,0x10E44C,4,q.storage_pointer,"read"});
    if(q.storage_pointer) {q.preexisting_storage_reused=true;q.file.image=in.preexisting_image;}
    else {
        q.effects.push_back({0x2F24,0x10E450,4,q.storage_index,"read"});
        q.effects.push_back({0x2F2C,0xF6980,4,in.manager_address,"read"});
        q.effects.push_back({0x2F38,0x10E450,4,0xFFFFFFFF,"write"});
        q.effects.push_back({0x2754,0xF6B90,1,1,"read"});
        q.file=ExecuteConstructionFile64518FromAbiEvidence(in.file);
        q.effects.insert(q.effects.end(),q.file.effects.begin(),q.file.effects.end());
        if(!q.file.completed){out.boundary=q.file.boundary;return out;}
        q.storage_pointer=q.file.return_value;q.effects.push_back({0x2F40,0x10E44C,4,q.storage_pointer,"write"});
    }
    q.heap=AllocateConstructionVm63D14(in.heap,0x47C);
    q.effects.insert(q.effects.end(),q.heap.effects.begin(),q.heap.effects.end());
    if(!q.heap.completed){out.boundary=q.heap.boundary;return out;}
    q.source_vm_address=q.heap.return_value;
    q.effects.push_back({0x23270,0x10E44C,4,q.storage_pointer,"read"});
    out.boundary=Boundary(ContextOwnerStop::InitializationCall,0x23274,"actual earlier construction reached6656C; constructor/callback have not returned");
    out.boundary.callee=0x6656C;out.boundary.arguments={q.source_vm_address,q.storage_pointer,0,512};
    out.reached_constructor_entry=true;++s.construction_serial;
    out.constructor_entry.owner_=OwnerIdentity();out.constructor_entry.serial_=s.construction_serial;
    return out;
}
std::shared_ptr<ContextOwnerStorageImage> OwnedContextOwner::ConsumeConstructionEntry(const ContextOwnerConstructionToken& token) {
    auto& s=*impl_;auto& q=s.construction;
    if(s.closed || !token || !(token.owner_==OwnerIdentity()) || token.serial_!=s.construction_serial ||
        !q.source_vm_address || q.constructor_entry_consumed || !q.file.image || !q.file.image->live)return {};
    q.constructor_entry_consumed=true;return q.file.image;
}
bool OwnedContextOwner::RebindConstructionImageAfterNativeFork(std::shared_ptr<ContextOwnerStorageImage> image) {
    auto& s=*impl_;auto& q=s.construction;
    if(s.closed || !q.file.image || !image || !image->live || !q.constructor_entry_consumed ||
        image->source_address!=q.storage_pointer || image->identity!=q.file.image->identity ||
        image->bytes.size()!=q.file.image->bytes.size() || image->initialized.size()!=image->bytes.size())return false;
    s.prior_native_fork_images.push_back(q.file.image);
    q.file.image=std::move(image);return true;
}
ContextOwnerConstructionSnapshot OwnedContextOwner::ConstructionSnapshot() const {return impl_->construction;}
ContextOwnerConstructorResult OwnedContextOwner::ContinueActualConstruction6656C(
        const ContextOwnerConstructionToken& token,ContextOwnerConstructorDomain domain) {
    auto& q=impl_->construction;ContextOwnerConstructorResult out;
    auto image=ConsumeConstructionEntry(token);
    if(!image || (domain.loader.borrowed_image && domain.loader.borrowed_image!=image)) {
        out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x6656C,"exact live owner-bound unused construction token/shared image required");return out;
    }
    domain.context.address=q.source_vm_address;domain.context.writable=true;
    for(const auto& b:q.heap.blocks)if(q.source_vm_address>=b.address && uint64_t(q.source_vm_address)+0x47C<=uint64_t(b.address)+b.bytes.size()) {
        const uint32_t off=q.source_vm_address-b.address;
        domain.context.bytes.assign(b.bytes.begin()+off,b.bytes.begin()+off+0x47C);
        domain.context.initialized.assign(b.initialized.begin()+off,b.initialized.begin()+off+0x47C);
    }
    domain.registry_heap.controller=0x1C93D4;domain.registry_heap.blocks=q.heap.blocks;
    domain.loader.borrowed_image=image;
    out=ExecuteConstruction6656CToCallback(std::move(domain));
    q.constructor=std::make_shared<ContextOwnerConstructorResult>(out);
    q.heap.blocks=out.registry_heap.blocks;
    if(out.tables && out.tables->shared_image()!=image) {
        out.boundary=Boundary(ContextOwnerStop::NativeDomain,0x66FDC,"unjoined native fork: explicit construction alias rebind required");
    }
    return out;
}
ContextOwnerActivationResult OwnedContextOwner::PrepareNonzero2327CActivationAfterUnverifiedLoadAndConstructor(
        ContextOwnerPreparedNonzero2327C p) {
    auto& s = *impl_; ContextOwnerActivationResult out;
    if (auto b = s.Live(); b.kind != ContextOwnerStop::None) { out.boundary = b; return out; }
    auto refuse = [&](const char* reason) {
        out.boundary = Boundary(ContextOwnerStop::NativeDomain,0x2327C,reason); return out;
    };
    if (s.tail || s.suffix.prepared || !p.acknowledges_unverified_prior_load_constructor_and_callback ||
        p.entry_pc != 0x2327C || !p.loadman_pointer_10cdc4.available ||
        !p.cached_activation_word_1c93fc.available || !p.prior_vm_publication_10e448.available ||
        p.blocks.size() != 5 || !SuffixDynamicAddress(p.source_owner_address,96) ||
        !p.source_vm_address)
        return refuse("explicit fresh nonzero2327C prepared entry and all prior conditions/global availability required");
    for (size_t i = 0; i < p.blocks.size(); ++i) {
        const auto& b = p.blocks[i];
        if (b.bytes.empty() || b.bytes.size() > 4096 || b.bytes.size() != b.initialized.size() ||
            !SuffixDynamicAddress(b.address,b.bytes.size()) ||
            SuffixOverlap(b.address,b.bytes.size(),p.source_owner_address,96) ||
            std::any_of(b.initialized.begin(),b.initialized.end(),[](uint8_t m) { return m > 1; }))
            return refuse("exact bounded source-shaped backing/masks disjoint from owner and BOOT required");
        for (size_t j = 0; j < i; ++j)
            if (SuffixOverlap(b.address,b.bytes.size(),p.blocks[j].address,p.blocks[j].bytes.size()))
                return refuse("source live aliases/partial effects excluded; native preflight refuses overlap");
    }
    auto* vm = SuffixBlock(p.blocks,p.source_vm_address,0x47C);
    auto* load = SuffixBlock(p.blocks,p.loadman_pointer_10cdc4.value,0x4F4);
    uint32_t cells = 0, ksc = 0, frame = 0, ignored = 0;
    if (!vm || vm->address != p.source_vm_address || vm->bytes.size() != 0x47C || !vm->writable ||
        !load || load->address != p.loadman_pointer_10cdc4.value || load->bytes.size() != 0x4F4 ||
        !SuffixRead(p.blocks,p.source_vm_address+0x20,4,cells) ||
        !SuffixRead(p.blocks,p.source_vm_address+0x0C,4,ksc) ||
        !SuffixRead(p.blocks,p.source_vm_address+0x10,4,frame) ||
        !SuffixRead(p.blocks,load->address+0x4C8,1,ignored))
        return refuse("VM47C and distinct CLoadMan4F4 fields/pointer availability required");
    auto* args = SuffixBlock(p.blocks,cells,12);
    auto* script = SuffixBlock(p.blocks,ksc,28);
    auto* context = SuffixBlock(p.blocks,frame,8);
    if (!args || args->address != cells || !args->writable || !script || script->address != ksc ||
        !context || context->address != frame || !context->writable ||
        args == vm || args == load || script == vm || script == load || context == vm || context == load ||
        args == script || args == context || script == context ||
        !SuffixRead(p.blocks,cells+4,4,ignored) || !SuffixRead(p.blocks,cells+8,4,ignored) ||
        !SuffixRead(p.blocks,ksc+8,4,ignored))
        return refuse("distinct writable argument/frame and readable KSC entry backing required; aliases are cuts");
    try { s.suffix.transcript.reserve(48); s.suffix.native_released_backing.reserve(5); }
    catch (const std::bad_alloc&) {
        out.boundary = Boundary(ContextOwnerStop::NativeAllocationFailure,0x2327C,"native metadata refusal before source effects"); return out;
    }
    s.suffix.prepared = true; s.suffix.source_entry_pc = 0x2327C;
    s.suffix.reached_call_pc = 0x23208; s.suffix.callee = 0x67FD4; s.suffix.return_pc = 0x23210;
    s.suffix.source_owner_address = p.source_owner_address; s.suffix.source_vm_address = p.source_vm_address;
    s.suffix.vm_publication_10e448 = p.prior_vm_publication_10e448;
    s.suffix.blocks = std::move(p.blocks);
    auto& t = s.suffix.transcript;
    t.push_back({0x23280,0x10CDC4,4,p.loadman_pointer_10cdc4.value,"read-global"});
    t.push_back({0x23200,p.loadman_pointer_10cdc4.value+0x4C8,1,load->bytes[0x4C8],"read-loadman"});
    // Keep explicit call inputs in the transcript; no earlier constructor or
    // ignored formatter/load return is invented.
    t.push_back({0x23208,0x67FD4,0,0x23210,"call"});
    t.push_back({0x67F24,0x1C93FC,0,p.cached_activation_word_1c93fc.value,"prepared-global-availability"});
    ++s.suffix_serial;
    out.continuation.owner_ = OwnerIdentity(); out.continuation.serial_ = s.suffix_serial;
    out.continuation.owner_generation_ = s.generation;
    out.boundary = Boundary(ContextOwnerStop::InitializationCall,0x23208,
        "prepared conditional nonzero branch reached67FD4; private token executes actual activation then tail");
    out.boundary.callee = 0x67FD4;
    out.boundary.arguments = {p.source_vm_address,2,uint32_t(load->bytes[0x4C8]==0),0};
    return out;
}
ContextOwnerActivationResult OwnedContextOwner::ContinuePreparedNonzero2327CActivation(
        const ContextOwnerActivationToken& token) {
    auto& s = *impl_; ContextOwnerActivationResult out;
    if (auto b = s.Live(); b.kind != ContextOwnerStop::None) { out.boundary = b; return out; }
    if (!token || !(token.owner_ == OwnerIdentity()) || token.serial_ != s.suffix_serial ||
        token.owner_generation_ != s.generation || !s.suffix.prepared || s.suffix.completed || s.tail) {
        out.boundary = Boundary(ContextOwnerStop::NativeDomain,0x23208,
            "exact live owner-bound unused activation token required; stale/foreign/checkpoint tokens refused"); return out;
    }
    auto& q = s.suffix; auto& bs = q.blocks; auto& t = q.transcript;
    auto read = [&](uint32_t pc,uint32_t address,uint32_t width) {
        uint32_t v = 0; SuffixRead(bs,address,width,v);
        t.push_back({pc,address,width,v,"read-backing"}); return v;
    };
    auto write = [&](uint32_t pc,uint32_t address,uint32_t width,uint32_t value) {
        auto* b = SuffixBlock(bs,address,width);
        for (uint32_t i = 0; i < width; ++i) { b->bytes[address-b->address+i] = uint8_t(value>>(8*i)); b->initialized[address-b->address+i] = 1; }
        t.push_back({pc,address,width,value,"store-backing"});
    };
    // Preflight and private owned storage guarantee these exact accesses. The
    // LIVE +20 load is retained on BOTH iterations, even in this disjoint domain.
    const uint32_t vm = q.source_vm_address;
    const uint32_t boolean = t[1].value == 0 ? 1u : 0u;
    const uint32_t cache = t[3].value;
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t cells = read(0x68018,vm+0x20,4);
        (void)read(0x68028,cells+4+4*i,4);
        write(0x68048,cells+4+4*i,4,0x20000000u | (i==0 ? boolean : 0u));
    }
    t.push_back({0x68054,0x67F1C,0,0x6805C,"call"});
    const uint32_t ksc = read(0x67F1C,vm+0x0C,4);
    t.push_back({0x67F24,0x1C93FC,4,cache,"read-global"});
    const uint32_t entry = read(0x67F28,ksc+8,4);
    const uint32_t frame = read(0x67F2C,vm+0x10,4);
    write(0x67F34,vm+0x38,1,1); write(0x67F38,frame+4,4,entry);
    write(0x67F44,vm+0x34,4,2); write(0x67F48,vm+0x18,4,cache);
    write(0x67F4C,vm+0x1C,4,0); write(0x67F50,vm+0x5C,2,0); write(0x67F58,vm+0x3A,1,0);
    t.push_back({0x67F54,0x6805C,0,0,"return"});
    t.push_back({0x68060,0x23210,0,0,"return"});
    q.vm_publication_10e448 = {true,vm};
    t.push_back({0x2321C,0x10E448,4,vm,"store-global"});
    auto owner_word = [&](uint32_t pc,uint32_t off,uint32_t value) { s.Store(off,value); t.push_back({pc,q.source_owner_address+off,4,value,"store-owner"}); };
    auto owner_byte = [&](uint32_t pc,uint32_t off) { s.Byte(off,0); t.push_back({pc,q.source_owner_address+off,1,0,"store-owner"}); };
    owner_word(0x23224,0x50,0xFFFFFFFF); owner_word(0x23228,0x2C,2000); owner_word(0x2322C,0x5C,0);
    owner_byte(0x23230,0x40); owner_byte(0x23234,0x48); owner_word(0x23238,0x24,0); owner_word(0x2323C,0x28,0);
    owner_word(0x23240,0x54,0xFFFFFFFF); owner_word(0x23244,0x58,0); owner_word(0x23248,0x1C,0); owner_word(0x2324C,0x20,0);
    t.push_back({0x23268,0,0,1,"caller-return"});
    s.tail = true; q.completed = true;
    out.completed = true; out.source_return_known = true; out.source_return_value = 1;
    return out;
}
ContextOwnerSuffixSnapshot OwnedContextOwner::SuffixSnapshot() const { return impl_->suffix; }
ContextOwnerBoundary OwnedContextOwner::SetDelayFromCheckpoint(int32_t source_word) {
    auto& s = *impl_; if (auto b = s.Live(); b.kind != ContextOwnerStop::None) return b;
    s.Store(0x2C, static_cast<uint32_t>(source_word)); return {};
}
ContextOwnerBoundary OwnedContextOwner::BindManagerRegistry(const OwnedManagerQueue& manager, ManagerToken registry) {
    auto& s = *impl_; if (auto b = s.Live(); b.kind != ContextOwnerStop::None) return b;
    const auto snap = manager.Snapshot();
    const auto found = std::find_if(snap.registries.begin(), snap.registries.end(),
        [&](const auto& r) { return r.token == registry; });
    uint32_t mask = 0, count = 0, vt = 0;
    if (!registry || registry.arena != snap.arena || found == snap.registries.end() || found->capacity != 5 ||
        !ManagerWord(snap, registry.relative + 16, mask) || mask != 2 ||
        !ManagerWord(snap, registry.relative + 20, count) || count != 5 ||
        !ManagerWord(snap, registry.relative, vt) || vt != 0xE1800)
        return Boundary(ContextOwnerStop::NativeDomain, 0x923C8, "exact live manager base projection must have capacity5/mask2; it remains28 bytes");
    if (s.bound && !(s.projection == registry))
        return Boundary(ContextOwnerStop::NativeDomain, 0, "stable owner binding cannot be reassigned");
    bool match = found->occupied == 5 && found->entries.size() == 5;
    const auto records = CallbackRecords();
    for (size_t i = 0; match && i < 5; ++i) {
        const auto token = found->entries[i]; uint32_t v = 0;
        match = ManagerWord(snap, token.relative + 16, v) && v == records[i].mask &&
            ManagerWord(snap, token.relative + 20, v) && v == registry.relative &&
            ManagerWord(snap, token.relative + 24, v) && v == records[i].descriptor.function &&
            ManagerWord(snap, token.relative + 28, v) && v == 0;
    }
    s.projection = registry; s.bound = true; s.records_match = match;
    s.first_projected_entry = match ? found->entries[0] : ManagerToken{};
    return {};
}
ContextOwnerBoundary OwnedContextOwner::InvokeCallback23424(ContextOwnerCallbackInputs inputs) {
    auto& s = *impl_; if (auto b = s.Live(); b.kind != ContextOwnerStop::None) return b;
    if (!inputs.gate_segment1_10fd4.available)
        return Boundary(ContextOwnerStop::MissingGlobal, 0x23430, "gate word availability must be supplied explicitly");
    if (inputs.gate_segment1_10fd4.value == 1)
        return Boundary(ContextOwnerStop::GateSkipped, 0x23454, "original gate1 returns without reading owner2C or kernel context");
    if (!s.Known(0x2C)) return Boundary(ContextOwnerStop::UninitializedField, 0x23440, "real96-byte owner exists but delay2C is not source-initialized at this checkpoint");
    if (!inputs.kernel_context_segment1_e5880.available)
        return Boundary(ContextOwnerStop::MissingGlobal, 0x74DA0, "kernel context producer/readiness remains external");
    auto b = Boundary(ContextOwnerStop::KernelCall, 0x74DBC, "23424 joins74D84 and stops before unresolved CB288; no kernel return is fabricated");
    b.callee = 0xCB288;
    //74D9C/a0→s2 retains low delay;2344C sign-extends it into a1→s3.
    //Raw a0..a3 preserve residual a1 while zeroing a2/a3. This does not
    //establish which registers belong to the typed ThreadMan import ABI.
    const uint32_t delay = s.Load(0x2C);
    b.arguments = {inputs.kernel_context_segment1_e5880.value, (delay & 0x80000000) ? 0xFFFFFFFFu : 0u, 0, 0};
    b.scheduler_budget_low = delay; b.scheduler_budget_high = b.arguments[1];
    s.kernel = true; return b;
}
ContextOwnerBoundary OwnedContextOwner::ContinueManagerCallback(const OwnedManagerQueue& manager, const ManagerBoundary& boundary, ContextOwnerCallbackInputs inputs) {
    auto& s = *impl_; if (auto b = s.Live(); b.kind != ContextOwnerStop::None) return b;
    if (!s.bound) return Boundary(ContextOwnerStop::NativeDomain, 0x930D0, "no manager binding");
    if (auto b = BindManagerRegistry(manager, s.projection); b.kind != ContextOwnerStop::None) return b;
    if (!s.bound || !s.records_match || boundary.kind != ManagerStopKind::DirectCallback ||
        !(boundary.owner == s.projection) || !(boundary.target == s.first_projected_entry) ||
        boundary.descriptor.function != 0x23424 || boundary.descriptor.adjustment != 0 ||
        boundary.signed_this_adjustment != 0 || boundary.adjusted_this_relative != s.projection.relative)
        return Boundary(ContextOwnerStop::NativeDomain, 0x930D0, "manager callback must match verified first record and exact base-projection binding");
    return InvokeCallback23424(inputs);
}
ContextOwnerSnapshot OwnedContextOwner::Snapshot() const {
    const auto& s = *impl_; ContextOwnerSnapshot out;
    out.owner = s.Token(owner_relative); out.closed = s.closed; out.initialization_tail_applied = s.tail;
    out.bound = s.bound; out.bound_records_match = s.records_match; out.stopped_at_kernel = s.kernel;
    out.manager_registry_projection = s.projection; out.allocation_transcript = s.allocations;
    out.records = CallbackRecords();
    out.initialization_cut = Boundary(ContextOwnerStop::InitializationCall, 0x2318C, "unexecuted external230EC setup; checkpoint suffix is separate");
    out.initialization_cut.callee = 0x1B188;
    if (!s.closed) {
        for (bool entry : {false, true}) {
            ContextOwnerBlock b; b.token = s.Token(entry ? entries_relative : owner_relative);
            const uint32_t n = entry ? 164u : 96u; b.bytes.resize(n); b.initialized.resize(n);
            for (uint32_t i = 0; i < n; ++i) {
                b.initialized[i] = entry ? s.entry_mask[i] : s.owner_mask[i];
                b.bytes[i] = b.initialized[i] ? (entry ? s.entry_storage[i] : s.owner_storage[i]) : 0;
            }
            out.blocks.push_back(std::move(b));
        }
    }
    return out;
}
ManagerToken OwnedContextOwner::OwnerIdentity() const { return impl_->Token(owner_relative); }
void OwnedContextOwner::Close() {
    auto& s = *impl_; s.closed = true; s.owner_storage.reset(); s.entry_storage.reset();
    if(!s.construction.closed) {
        auto& q=s.construction;
        for(auto& weak:s.prior_native_fork_images)if(auto image=weak.lock())image->live=false;
        if(q.file.image && q.file.image->live){q.native_released_backing.push_back({q.file.image->source_address,static_cast<uint32_t>(q.file.image->bytes.size()),"native storage lease lifetime end; not PSP free"});q.file.image->live=false;}
        if(q.source_vm_address)q.native_released_backing.push_back({q.source_vm_address,0x47C,"native VM checkpoint backing lifetime end; no source destructor claim"});
        q.closed=true;++s.construction_serial;
    }
    if (!s.suffix.closed) {
        for (const auto& b : s.suffix.blocks)
            s.suffix.native_released_backing.push_back({b.address,static_cast<uint32_t>(b.bytes.size()),"native prepared backing RAII; not source destructor"});
        s.suffix.blocks.clear(); s.suffix.closed = true; ++s.suffix_serial;
    }
    s.bound = false; s.records_match = false; s.projection = {}; s.first_projected_entry = {};
}
const char* ContextOwnerStopName(ContextOwnerStop k) {
    switch (k) {
    case ContextOwnerStop::None: return "none";
    case ContextOwnerStop::NativeDomain: return "native_domain";
    case ContextOwnerStop::NativeAllocationFailure: return "native_allocation_failure";
    case ContextOwnerStop::InitializationCall: return "initialization_call";
    case ContextOwnerStop::MissingGlobal: return "missing_global";
    case ContextOwnerStop::UninitializedField: return "uninitialized_field";
    case ContextOwnerStop::KernelCall: return "kernel_call";
    case ContextOwnerStop::GateSkipped: return "gate_skipped";
    case ContextOwnerStop::Closed: return "closed";
    } return "unknown";
}
} // namespace dokodemo::recovered
