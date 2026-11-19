#include "context_callback_native.h"
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
std::atomic<uint64_t> next_callback_allocation{1};
struct DomainCut { uint32_t pc; const char* reason; };
ContextOwnerBoundary Cut(uint32_t pc,const char* reason) {
    ContextOwnerBoundary out;out.kind=ContextOwnerStop::NativeDomain;out.source_pc=pc;out.detail=reason;return out;
}
}
ContextCallbackResult ExecuteContextCallback1C0FC(OwnedProducer& producer,
        const ProducerCallbackBorrow& borrow,CurrentTablesMemory& m,const ContextCallbackInput& in) {
    ContextCallbackResult out;out.first_effect=m.effect_count();out.end_effect=out.first_effect;
    auto refuse=[&](uint32_t pc,const char* text){out.boundary=Cut(pc,text);};
    if(producer.ValidateCallbackBorrow(borrow.token).kind!=ProducerStop::None || !borrow.backing ||
       !borrow.backing->live || borrow.backing->bytes.size()!=108 || borrow.backing->initialized.size()!=108 ||
       !borrow.singleton.initialized || borrow.singleton.value!=borrow.backing->source_address ||
       !borrow.registered_callback.initialized || borrow.registered_callback.address!=0x1C9454 || borrow.registered_callback.value!=0x1C0FC) {
        refuse(0x6664C,"actual live producer108 registered1C0FC and unused owner-bound borrow required");return out;
    }
    auto rd=[&](uint32_t pc,uint32_t address,uint32_t width){const uint32_t v=m.Read(address,width);out.effects.push_back({pc,address,width,v,"read"});return v;};
    auto wr=[&](uint32_t pc,uint32_t address,uint32_t width,uint32_t value){m.Write(address,width,value);out.effects.push_back({pc,address,width,width==1?value&255:width==2?value&65535:value,"write"});};
    try {
        if((in.entry_pc!=0x66644 && in.entry_pc!=0x6664C && in.entry_pc!=0x1C0FC) ||
           in.return_pc!=(in.entry_pc==0x1C0FC?0x23FFF0u:0x66654u) ||
           (in.context!=0x2020B0 && in.context!=0x300000) || in.loadman!=0x200000 || in.arguments[0]!=in.context)
            throw DomainCut{0x6664C,"finite original caller/receiver/address binding required"};
        if(m.Peek(0x1C9454,4)!=0x1C0FC || m.Peek(0x10CDC8,4)!=borrow.singleton.value || m.Peek(0x10CDC4,4)!=in.loadman)
            throw DomainCut{0x66648,"mapped registration/singleton differs from actual producer"};
        const auto mapped=m.SharedImage(borrow.backing->identity);
        if(!mapped || !mapped->live || mapped->source_address!=borrow.backing->source_address || mapped->bytes!=borrow.backing->bytes ||
           mapped->initialized!=borrow.backing->initialized)
            throw DomainCut{0x1BF2C,"exact actual producer backing or explicit native fork required"};
        if(!in.image || !in.image->live || m.Peek(in.context+12,4)!=in.image->source_address ||
           !m.Live(in.image->identity) || m.SharedImage(in.image->identity)!=in.image)
            throw DomainCut{0x6664C,"same live source image lease required; callback does not install a new image"};
        if(in.entry_pc==0x66644 && (in.arguments[1]!=0x20000000 || in.arguments[2]!=m.Peek(in.context+0x66,2) || in.arguments[3]!=in.image->source_address))
            throw DomainCut{0x6664C,"raw original caller volatile argument binding mismatch"};
        // Native atomic gate only. Source unknown1C138 occurs AFTER real
        // allocation/list effects and is deliberately not equated with this.
        uint32_t child0=0,child1=0;
        try {child0=m.Peek(in.loadman+0x4E4,4);}catch(const std::exception&){throw DomainCut{0x1C138,"source-known CLoadMan child0 mask required; native preflight cut"};}
        try {child1=m.Peek(in.loadman+0x4E8,4);}catch(const std::exception&){throw DomainCut{0x1C140,"source-known CLoadMan child1 mask required; native preflight cut"};}
        if(child0 || child1)throw DomainCut{0x1E88C,"positive source child path unavailable; native preflight cut"};
        if(in.entry_pc==0x66644)(void)rd(0x66648,0x1C9454,4);
        constexpr uint32_t h=0x1C93D4,requested=52,rounded=64;
        if(rd(0x63324,h+16,1)==0)throw DomainCut{0x63368,"finite enabled positive allocator required"};
        const uint32_t begin=rd(0x62FD0,h+4,4),node=rd(0x62FD4,begin+20,4);
        if(!node)throw DomainCut{0x63004,"finite first-free allocator required"};
        const uint32_t capacity=rd(0x62FE4,node+24,4);
        if(capacity<rounded+32 || node>UINT32_MAX-rounded-16)throw DomainCut{0x630D8,"finite positive split allocator required"};
        wr(0x63030,node+14,1,1);const uint32_t tag=rd(0x63038,h+12,2);
        wr(0x6303C,node+8,4,requested);wr(0x63044,node+12,2,tag);
        uint32_t r3=rd(0x63048,node+20,4),r2=rd(0x6304C,node+4,4);
        const uint32_t sum=node+rounded,newnode=sum+16;
        wr(0x63058,newnode+20,4,r3);wr(0x6305C,newnode+4,4,r2);
        r3=rd(0x63060,node+16,4);r2=rd(0x63064,node+20,4);
        const uint32_t next=rd(0x63068,node+4,4);
        wr(0x6306C,node+4,4,newnode);wr(0x63070,newnode+16,4,r3);wr(0x63074,r2+16,4,newnode);
        r3=rd(0x6307C,newnode+4,4);const uint32_t previous=rd(0x63080,node+16,4);
        wr(0x63084,sum+16,4,node);wr(0x63090,newnode+24,4,r3-newnode-16);
        wr(0x63094,newnode+15,1,0x79);wr(0x63098,next,4,newnode);wr(0x6309C,previous+20,4,newnode);
        wr(0x630A0,newnode+14,1,0);wr(0x630A4,newnode+12,2,0);wr(0x630A8,newnode+8,4,0);
        (void)rd(0x630AC,node+12,2);wr(0x63340,h+12,4,0);
        out.node=node+16;
        const uint32_t actual=rd(0x1BF2C,0x10CDC8,4);
        wr(0x1BF30,in.context+0x40,4,out.node);
        const uint32_t tail=rd(0x1BF34,actual+4,4);
        wr(0x1BF38,out.node+4,4,0);wr(0x1BF3C,out.node,4,tail);
        const uint32_t head=rd(0x1BF40,actual,4);wr(0x1BF48,out.node+8,4,in.context);
        if(!head)wr(0x1BF4C,actual,4,out.node);
        const uint32_t previous_tail=rd(0x1BF50,actual+4,4);
        if(previous_tail)wr(0x1BF58,previous_tail+4,4,out.node);
        wr(0x1BF5C,actual+4,4,out.node);
        wr(0x1BF60,out.node+0x30,4,0);wr(0x1BF64,out.node+0x14,4,0);
        wr(0x1BF68,out.node+0x10,4,0);wr(0x1BF6C,out.node+0x0C,4,0);
        wr(0x1BF70,out.node+0x18,4,0);wr(0x1BF74,out.node+0x1C,4,0);
        wr(0x1BF78,out.node+0x20,4,0);wr(0x1BF7C,out.node+0x24,4,0);
        wr(0x1BF80,out.node+0x28,4,0);wr(0x1BF88,out.node+0x2C,4,0);
        const uint32_t load=rd(0x1C130,0x10CDC4,4);
        wr(0x1C13C,out.node+0x18,4,rd(0x1C138,load+0x4E4,4));
        wr(0x1C148,out.node+0x24,4,rd(0x1C140,load+0x4E8,4));
        (void)rd(0x1E8D0,out.node+0x18,4); // real zero-child early exit
        (void)rd(0x1C154,out.node+0x18,4);(void)rd(0x1C16C,out.node+0x24,4);
        out.return_residue=out.node+0x30;out.return_pc=in.return_pc;out.completed=true;
    } catch(const DomainCut& cut) {refuse(cut.pc,cut.reason);}
      catch(const std::exception& e) {out.boundary.kind=ContextOwnerStop::NativeDomain;out.boundary.source_pc=0x1C0FC;out.boundary.detail=e.what();}
    out.end_effect=m.effect_count();
    if(out.end_effect==out.first_effect) {
        (void)producer.CancelCallbackBorrow(borrow.token);
    } else {
        try {
            out.producer_backing=m.SharedImage(borrow.backing->identity);
            const auto adopted=producer.AdoptCallbackState(borrow.token,out.producer_backing);
            out.producer_adopted=adopted.kind==ProducerStop::None;
            if(!out.producer_adopted){out.completed=false;out.boundary=Cut(0x6664C,"producer callback adoption rejected; partial effects retained");}
            if(out.completed&&out.producer_adopted){out.allocation.serial_=next_callback_allocation.fetch_add(1);out.allocation.owner_=producer.NativeIdentity();out.allocation.producer_identity_=out.producer_backing->identity;out.allocation.node_=out.node;}
        }catch(const std::exception& e){out.completed=false;out.boundary.kind=ContextOwnerStop::NativeDomain;out.boundary.source_pc=0x6664C;out.boundary.detail=e.what();}
    }
    return out;
}
ContextCallbackReleaseResult ExecuteContextCallbackRelease1B578(OwnedProducer& producer,
        const ProducerCallbackBorrow& borrow,CurrentTablesMemory& m,const ContextCallbackAllocationToken& token) {
    ContextCallbackReleaseResult out;out.first_effect=m.effect_count();out.end_effect=out.first_effect;
    auto rd=[&](uint32_t pc,uint32_t a,uint32_t n){const uint32_t v=m.Read(a,n);out.effects.push_back({pc,a,n,v,"read"});return v;};
    auto wr=[&](uint32_t pc,uint32_t a,uint32_t n,uint32_t v){m.Write(a,n,v);out.effects.push_back({pc,a,n,n==1?v&255:n==2?v&65535:v,"write"});};
    try {
        if(producer.ValidateCallbackBorrow(borrow.token).kind!=ProducerStop::None || !token || token.owner_!=producer.NativeIdentity() ||
           !borrow.backing || token.producer_identity_!=borrow.backing->identity || m.Peek(0x10CDC8,4)!=borrow.singleton.value ||
           m.Peek(borrow.singleton.value+4,4)!=token.node_)
            throw DomainCut{0x1B590,"exact live callback allocation/tail token required; old foreign-child guard unchanged"};
        if(m.Peek(token.node_+0x1C,4) || m.Peek(token.node_+0x28,4) || m.Peek(token.node_+4,4))
            throw DomainCut{0x1C068,"only mapped zero-aux actual tail release is recovered"};
        const uint32_t p=rd(0x1B58C,0x10CDC8,4),tail=rd(0x1B590,p+4,4);
        (void)rd(0x1C068,tail+0x1C,4);(void)rd(0x1C068,tail+0x28,4);
        const uint32_t prev=rd(0x1C080,tail,4);uint32_t next=0;
        if(!prev)next=rd(0x1C088,tail+4,4);
        else {next=rd(0x1C08C,tail+4,4);wr(0x1C090,prev+4,4,next);next=rd(0x1C094,tail+4,4);}
        if(next)wr(0x1C09C,next,4,prev);
        const uint32_t actual=rd(0x1C0A4,0x10CDC8,4),head=rd(0x1C0A8,actual,4);
        if(head==tail)wr(0x1C0B0,actual,4,next);
        if(rd(0x1C0B4,actual+4,4)==tail)wr(0x1C0E0,actual+4,4,rd(0x1C0BC,tail,4));
        auto free=[&](uint32_t payload) {
            constexpr uint32_t h=0x1C93D4;
            const uint32_t begin=rd(0x6339C,h+4,4),end=rd(0x633AC,h+8,4),node=payload-16;
            if(payload<begin || payload>end)throw DomainCut{0x633BC,"finite free outside actual arena"};
            if(rd(0x633F0,node+15,1)!=0x79)throw DomainCut{0x6340C,"actual allocator marker required"};
            const uint32_t previous=rd(0x633F8,node,4),following=rd(0x633FC,node+4,4);
            (void)rd(0x63400,node+12,2);(void)rd(0x63408,node+8,4);
            if(rd(0x63428,node+14,1)!=1)throw DomainCut{0x63434,"actual allocated payload required"};
            wr(0x63434,node+14,1,0);const uint32_t previous_used=rd(0x63438,previous+14,1);wr(0x63440,node+8,4,0);
            if(!previous_used)throw DomainCut{0x63444,"previous-free coalescing outside measured finite release"};
            if(rd(0x634B0,following+14,1)==0) {
                const uint32_t a=rd(0x634CC,following+16,4),b=rd(0x634D0,following+20,4),physical=rd(0x634D4,following+4,4);
                wr(0x634D8,node+16,4,a);const uint32_t again=rd(0x634E0,following+16,4);
                wr(0x634E4,node+20,4,b);wr(0x634E8,node+4,4,physical);wr(0x634EC,again+20,4,node);
                const uint32_t physical_again=rd(0x634F0,following+4,4),free_next=rd(0x634F4,following+20,4);
                wr(0x634F8,physical_again,4,node);wr(0x63500,free_next+16,4,node);
            } else {
                const uint32_t before=rd(0x62F10,node,4);
                if(rd(0x62F14,before,4)!=0 || before!=begin)throw DomainCut{0x62F14,"finite insertion requires measured begin sentinel"};
                const uint32_t first=rd(0x6350C,before+20,4);wr(0x63510,node+16,4,before);wr(0x63518,node+20,4,first);
                const uint32_t again=rd(0x6351C,before+20,4);wr(0x63524,before+20,4,node);wr(0x63500,again+16,4,node);
            }
            const uint32_t following_after=rd(0x63470,node+4,4);wr(0x63484,node+24,4,following_after-node-16);
        };
        free(tail);const uint32_t owner=rd(0x1B5E0,0x10CDC8,4);wr(0x1B5E8,owner+4,4,0);
        const uint32_t singleton=rd(0x1B59C,0x10CDC8,4);free(singleton);wr(0x1B5CC,0x10CDC8,4,0);
        const auto adopted=producer.AdoptMappedCallbackRelease(borrow.token,m.SharedImage(borrow.backing->identity),tail);
        if(adopted.kind!=ProducerStop::None)throw DomainCut{0x1B5CC,"actual producer release adoption refused"};
        out.completed=true;out.released_tail=tail;out.released_producer=singleton;
        // Physical arena slices remain as captured bytes. Only these two
        // source allocations are released; earlier nodes/context+40 and
        // registration are deliberately NOT cleared or claimed freed.
    }catch(const DomainCut& cut){out.boundary=Cut(cut.pc,cut.reason);}
     catch(const std::exception& e){out.boundary=Cut(0x1B578,e.what());}
    out.end_effect=m.effect_count();
    if(out.end_effect==out.first_effect)(void)producer.CancelCallbackBorrow(borrow.token);
    return out;
}
} // namespace dokodemo::recovered
