#include "string_native.h"
#include "../core_hash.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void need(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
int32_t signed28(uint32_t word) {
    const uint32_t p=word&0x0FFFFFFFu;
    return p&0x08000000u ? static_cast<int32_t>(p)-0x10000000 : static_cast<int32_t>(p);
}
uint32_t length(const std::vector<uint8_t>& source) {
    const auto it=std::find(source.begin(),source.end(),uint8_t(0));
    need(it!=source.end(),"String strlenC3A8C crosses mapped readable window");
    return static_cast<uint32_t>(it-source.begin());
}
}
struct StringArena::Storage {
    uint32_t base=0, common=0, size=0;
    bool forward=true;
    uint32_t cursor=0;
    std::vector<uint8_t> heap, control;
    std::map<uint32_t,uint32_t> live;
    uint32_t allocations=0,reallocations=0,releases=0,copies=0;
    mutable std::string digest;
};
bool StringArenaSummary::operator==(const StringArenaSummary& o) const {
    return known==o.known && allocations==o.allocations && reallocations==o.reallocations &&
        releases==o.releases && copies==o.copies && live_allocations==o.live_allocations && conversion_cursor==o.conversion_cursor && sha256==o.sha256;
}
StringArena::StringArena(const StringFixture& f) {
    if(!f.enabled)return;
    need(f.heap_ready && f.memory_disjoint && f.allocator_policy_known,
         "String arena requires explicit mapped readiness/disjoint/allocator policy premises");
    initialize(f.heap_base,f.heap_size,f.common_base,f.forward_positive,f.initial_heap,f.conversion_cursor);
}
StringArena StringArena::WindowsOwned(uint32_t base,uint32_t size,uint32_t common) {
    need(size>=112 && size<=0x400000,"Windows string storage capacity outside native policy");
    StringArena arena;arena.initialize(base,size,common,true,std::vector<uint8_t>(size,0),0);
    const auto control=arena.control_bytes();
    need(control.size()==32 && control[16]==1 && arena.read(base+4)==base+32
         && arena.read(base+32+4)==base+size-32 && arena.read(base+size-32)==base+32
         && arena.read(base+32+15,1)==0x79 && arena.live_blocks().empty(),
         "Windows string storage initialization readback failed");
    return arena;
}
void StringArena::initialize(uint32_t base,uint32_t size,uint32_t common,bool forward,
                            const std::vector<uint8_t>& initial_heap,uint32_t cursor) {
    need(base && base%16==0 && size>=112 && size%16==0 &&
         size<=0x400000 && base<=UINT32_MAX-size,
         "String arena639CC bounded aligned size/base domain");
    need(initial_heap.size()==size,"String arena initial readable heap bytes not explicitly mapped");
    need(static_cast<int64_t>(base)-common>=-0x08000000LL &&
         static_cast<int64_t>(base)+size-common<0x08000000LL,
         "String arena signed28 offset range");
    storage_=std::make_shared<Storage>(); auto& s=*storage_;
    s.base=base;s.common=common;s.size=size;s.forward=forward;
    need(cursor<4,"String conversion cursor outside mapped modulo4 domain");s.cursor=cursor;
    s.heap=initial_heap;s.control.assign(32,0);
    const auto ctl=[&](uint32_t at,uint32_t v,unsigned n=4) {
        for(unsigned i=0;i<n;++i)s.control[at+i]=static_cast<uint8_t>(v>>(8*i));
    };
    ctl(0,s.size);ctl(4,s.base);ctl(8,s.base+s.size-32);ctl(16,s.forward?1u:0u,1);
    //639CC strncpy14 "KS:Varstr" and name terminator; policy+16 is separate6395C.
    const char name[]="KS:Varstr";
    std::copy_n(reinterpret_cast<const uint8_t*>(name),sizeof(name),s.control.begin()+17);
    const uint32_t first=s.base, free=first+32, last=first+s.size-32;
    write(first+14,3,1);write(first+15,0x79,1);write(first+8,0);write(first+24,0);write(first+12,0,2);
    write(first+4,free);write(first+16,0);write(first+20,free);write(first,0);
    write(free,first);write(free+4,last);write(free+8,0);write(free+12,0,2);write(free+14,0,1);write(free+15,0x79,1);
    write(free+16,first);write(free+20,last);write(free+24,last-free-16);
    write(last,free);write(last+4,0);write(last+8,0);write(last+12,0,2);write(last+14,2,1);write(last+15,0x79,1);
    write(last+16,free);write(last+20,0);write(last+24,0);
}
bool StringArena::ready() const { return bool(storage_); }
StringArena StringArena::FromMappedStorage(const StringFixture& f,const std::vector<uint8_t>& ctl,const std::vector<uint8_t>& heap) {
    StringArena arena(f);need(arena.ready() && ctl.size()==32 && heap.size()==f.heap_size,"Mapped string arena current byte sizes");
    auto& s=*arena.storage_;s.control=ctl;s.heap=heap;s.forward=ctl[16]!=0;s.live.clear();
    const auto cword=[&](unsigned at) {uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(ctl[at+i])<<(8*i);return v;};
    need(cword(0)==f.heap_size && cword(4)==f.heap_base && cword(8)==f.heap_base+f.heap_size-32,"Mapped current arena manager identity mismatch");
    uint32_t h=arena.read(f.heap_base+4),prev=f.heap_base;
    for(uint32_t count=0;h!=f.heap_base+f.heap_size-32;++count) {
        need(count<f.heap_size/16 && h>prev && arena.read(h)==prev && arena.read(h+15,1)==0x79,
             "Mapped current arena invalid acyclic physical ownership chain");
        const uint32_t next=arena.read(h+4);need(next>h && next<=f.heap_base+f.heap_size-32,"Mapped physical next outside arena");
        if(arena.read(h+14,1)==1)s.live[h+16]=arena.read(h+8);
        else need(arena.read(h+14,1)==0,"Mapped nonsentinel block invalid used flag");
        prev=h;h=next;
    }
    need(arena.read(h)==prev,"Mapped current arena tail predecessor mismatch");return arena;
}
uint32_t StringArena::common_base() const { need(ready(),"String arena unavailable");return storage_->common; }
void StringArena::unique() {
    need(ready(),"Owned/string allocator source availability not supplied");
    if(storage_.use_count()!=1)storage_=std::make_shared<Storage>(*storage_);
    storage_->digest.clear();
}
uint32_t StringArena::read(uint32_t a,unsigned n) const {
    need(ready() && n && n<=4 && a>=storage_->base && a-storage_->base<=storage_->size-n && a%n==0,
         "String arena unmapped/unaligned read");
    uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(storage_->heap[a-storage_->base+i])<<(8*i);return v;
}
void StringArena::write(uint32_t a,uint32_t v,unsigned n) {
    need(ready() && n && n<=4 && a>=storage_->base && a-storage_->base<=storage_->size-n && a%n==0,
         "String arena unmapped/unaligned store");
    unique();for(unsigned i=0;i<n;++i)storage_->heap[a-storage_->base+i]=static_cast<uint8_t>(v>>(8*i));
}
uint32_t StringArena::rounded(uint32_t n) const {
    need(n<=0x007FFFF0u,"Allocator request overflow/outside bounded native success domain");
    return n ? (n+15)&~15u : 16;
}
uint32_t StringArena::allocation_id() const {return uint32_t(storage_->control[12])|(uint32_t(storage_->control[13])<<8);}
uint32_t StringArena::allocate_forward(uint32_t n) {
    const uint32_t size=rounded(n);uint32_t h=read(storage_->base+20);
    for(uint32_t count=0;h && count<storage_->size/16;++count) {
        const uint32_t free_size=read(h+24);
        if(free_size<size){h=read(h+20);continue;}
        write(h+14,1,1);write(h+8,n);write(h+12,allocation_id(),2);
        if(free_size>=size+32) {
            const uint32_t tail=h+size+16, next_free=read(h+20), next=read(h+4),prev_free=read(h+16);
            write(tail+20,next_free);write(tail+4,next);write(tail+16,prev_free);
            write(next_free+16,tail);write(h+4,tail);write(tail,h);
            write(tail+24,next-tail-16);write(tail+15,0x79,1);write(next,tail);write(prev_free+20,tail);
            write(tail+14,0,1);write(tail+12,0,2);write(tail+8,0);
        } else {
            const uint32_t p=read(h+16),q=read(h+20);write(p+20,q);write(q+16,p);
        }
        return h+16;
    }
    throw std::runtime_error("Allocator62FA8 no fitting free block; source assertion/break has partial effects");
}
uint32_t StringArena::allocate_backward(uint32_t n) {
    const uint32_t size=rounded(n);uint32_t h=read(storage_->base+storage_->size-32+16);
    for(uint32_t count=0;h && count<storage_->size/16;++count) {
        const uint32_t free_size=read(h+24);
        if(free_size<size){h=read(h+16);continue;}
        uint32_t target=h;
        if(free_size>=size+32) {
            const uint32_t next=read(h+4);target=h+free_size-size;
            write(target+4,next);write(target,h);write(h+24,target-h-16);write(next,target);write(h+4,target);
        } else {
            const uint32_t p=read(h+16),q=read(h+20);write(p+20,q);write(q+16,p);
        }
        write(target+15,0x79,1);write(target+14,1,1);write(target+12,allocation_id(),2);write(target+8,n);
        return target+16;
    }
    throw std::runtime_error("Allocator630F0 no fitting free block; source assertion/break remains unimplemented");
}
uint32_t StringArena::allocate(int32_t n) {
    need(n!=std::numeric_limits<int32_t>::min(),"Allocator signed request INT_MIN outside valid bounded domain");
    unique();const uint32_t size=static_cast<uint32_t>(n<0?-n:n);
    const bool front=storage_->forward ? n>0 : n<0;
    const uint32_t pointer=front?allocate_forward(size):allocate_backward(size);
    std::fill(storage_->control.begin()+12,storage_->control.begin()+16,uint8_t(0));
    storage_->live[pointer]=size;++storage_->allocations;return pointer;
}
void StringArena::validate_owned(uint32_t p) const {
    need(ready() && p && p%16==0 && p>=storage_->base+16 && p<=storage_->base+storage_->size-32,
         "Allocator63378 pointer outside mapped manager bounds");
    need(storage_->live.count(p)!=0 && read(p-1,1)==0x79 && read(p-2,1)==1,
         "Allocator63378 bad magic/freed/unowned pointer");
}
uint32_t StringArena::previous_free(uint32_t h) const {
    uint32_t p=read(h);
    for(uint32_t n=0;n<storage_->size/16;++n) {
        const uint32_t prev=read(p);
        if(!prev || read(p+14,1)==0)return p;
        p=prev;
    }
    throw std::runtime_error("Allocator physical predecessor cycle outside bounded ownership domain");
}
void StringArena::free(uint32_t p) {
    if(!p)return;
    validate_owned(p);unique();
    const uint32_t h=p-16,prev=read(h),next=read(h+4);
    const bool prev_free=read(prev+14,1)==0,next_free=read(next+14,1)==0;
    write(h+14,0,1);write(h+8,0);uint32_t free_header=h;
    if(prev_free && next_free) {
        const uint32_t next_next=read(next+4),q=read(next+20);
        write(prev+20,q);write(prev+4,next_next);write(q+16,prev);write(next_next,prev);free_header=prev;
    } else if(prev_free) {
        write(next,prev);write(prev+4,next);free_header=prev;
    } else if(next_free) {
        const uint32_t a=read(next+16),b=read(next+20),next_next=read(next+4);
        write(h+16,a);write(h+20,b);write(h+4,next_next);write(a+20,h);write(next_next,h);write(b+16,h);
    } else {
        const uint32_t a=previous_free(h),b=read(a+20);
        write(h+16,a);write(h+20,b);write(a+20,h);write(b+16,h);
    }
    write(free_header+24,read(free_header+4)-free_header-16);
    storage_->live.erase(p);++storage_->releases;
}
void StringArena::init_header(uint32_t h,uint32_t prev,uint32_t next) {
    write(h+15,0x79,1);write(h+14,0,1);write(h+4,next);write(h,prev);write(h+12,allocation_id(),2);
    write(prev+4,h);write(next,h);write(h+8,0);
}
void StringArena::insert_free(uint32_t h) {
    const uint32_t prev=previous_free(h),next=read(prev+20);
    write(h+16,prev);write(h+20,next);write(h+24,read(h+4)-h-16);write(next+16,h);write(prev+20,h);
}
void StringArena::move_free(uint32_t old,uint32_t target) {
    const uint32_t prev=read(old),next=read(old+4),a=read(old+16),b=read(old+20);
    init_header(target,prev,next);write(target+16,a);write(target+20,b);write(a+20,target);write(b+16,target);
    write(target+24,next-target-16);
}
uint32_t StringArena::reallocate(uint32_t p,uint32_t n) {
    need(n<=0x007FFFF0u,"Allocator63680 request outside bounded success domain");
    if(!p) {const uint32_t r=allocate_forward(n);unique();storage_->live[r]=n;++storage_->reallocations;return r;}
    validate_owned(p);unique();const uint32_t h=p-16,next=read(h+4),size=rounded(n);
    const uint32_t capacity=next-h-16;
    const bool next_free=read(next+14,1)==0;
    const uint32_t available=capacity+(next_free?read(next+24)+16:0);
    uint32_t result=p;
    if(capacity<size) {
        if(available<size) {
            // Ordinary16-byte aligned block path (source63214 aligned128 cut excluded).
            need((p&127u)!=0,"Allocator63680 aligned128 relocation needs63214 ownership path, outside ordinary block domain");
            const uint32_t fresh=allocate_forward(n);const uint32_t old_size=read(h+8);
            const auto copied=bytes(p,old_size);store(fresh,copied);storage_->live[fresh]=n;
            free(p);result=fresh;
        } else if(size+16<available) {
            move_free(next,h+size+16);
        } else {
            const uint32_t a=read(next+16),b=read(next+20),q=read(next+4);
            write(h+4,q);write(a+20,b);write(q,h);write(b+16,a);
        }
    } else if(size+16<capacity) {
        const uint32_t tail=h+size+16;
        if(next_free)move_free(next,tail);
        else {init_header(tail,h,next);insert_free(tail);}
    } else {
        write(h+12,allocation_id(),2);std::fill(storage_->control.begin()+12,storage_->control.begin()+16,uint8_t(0));
    }
    write(result-8,n);storage_->live[result]=n;++storage_->reallocations;return result;
}
uint32_t StringArena::allocation_size(uint32_t p) const {validate_owned(p);return read(p-8);}
std::vector<uint8_t> StringArena::bytes(uint32_t p,uint32_t n) const {
    need(ready() && p>=storage_->base && n<=storage_->size && p-storage_->base<=storage_->size-n,
         "String readable byte window outside mapped heap");
    const auto a=storage_->heap.begin()+(p-storage_->base);return {a,a+n};
}
void StringArena::store(uint32_t p,const std::vector<uint8_t>& b) {
    need(b.size()<=UINT32_MAX,"String store size overflow");
    const uint32_t n=static_cast<uint32_t>(b.size());
    need(ready() && p>=storage_->base && n<=storage_->size && p-storage_->base<=storage_->size-n,
         "String store outside mapped heap");
    unique();std::copy(b.begin(),b.end(),storage_->heap.begin()+(p-storage_->base));
}
uint32_t StringArena::word(uint32_t p) const {return read(p);}
void StringArena::store_word(uint32_t p,uint32_t v) {write(p,v);}
uint32_t StringArena::pointer(uint32_t w) const {
    need(ready() && (w>>28==5 || w>>28==6),"String pointer requires mapped tag5/6");
    return common_base()+static_cast<uint32_t>(signed28(w));
}
uint32_t StringArena::owned_word(uint32_t p) const {
    validate_owned(p);return 0x60000000u|((p-common_base())&0x0FFFFFFFu);
}
uint32_t StringArena::resolve75778(uint32_t w) {
    need(ready() && (w>>28==5 || w>>28==6),"String75778 nonstring conversion outside closed tag5/6 domain");
    unique();storage_->cursor=(storage_->cursor+1)&3u;return pointer(w);
}
uint32_t StringArena::conversion_cursor() const {need(ready(),"String arena unavailable");return storage_->cursor;}
void StringArena::release7515C(uint32_t& w) {if(w>>28==6)free(pointer(w));w=0;}
void StringArena::assign(uint32_t& w,const std::vector<uint8_t>& source,uint32_t source_address,
                         bool source_owned,uint32_t source_pointer) {
    const uint32_t len=length(source),n=len+1;
    //C39F4 aligned path reads complete words including bytes after the NUL.
    //Destination returned by ordinary allocator is16 aligned.
    if(source_address%4==0)need(source.size()>=((n+3)&~3u),"String strcpyC39F4 aligned last-word readable padding missing");
    const bool owned=w>>28==6;
    const uint32_t old=owned?pointer(w):0;
    if(source_owned && old==source_pointer) {
        validate_owned(old);need(n<=read(old-12)-(old-16)-16,"String self-alias moving lifetime outside proved domain");
    }
    //The source writes tag6 before allocation and pointer before strcpy.
    //StartupProbe commits these only when the whole operation succeeds.
    if(!owned)w=0x60000000u|(w&0x0FFFFFFFu);
    const uint32_t p=owned?reallocate(old,n):allocate(static_cast<int32_t>(n));
    w=owned_word(p);
    if(source_pointer) {
        // strlen precedes realloc, but C39F4 reads the CURRENT source bytes
        // afterwards. An interior alias can now name a newly written free
        // header; snapshotting its old suffix changes a successful result.
        uint32_t from=source_pointer,to=p;
        for(uint32_t copied=0;;) {
            need(copied<storage_->size,"String strcpy overlap/nontermination outside bounded mapped heap");
            if((from|to)%4==0) {
                const uint32_t value=read(from);
                bool zero=false;
                for(unsigned byte=0;byte<4;++byte) zero=zero||((value>>(8*byte))&0xFFu)==0;
                if(!zero) {
                    write(to,value);from+=4;to+=4;copied+=4;
                    continue;
                }
                //C3A08/C3A4C first fetch the aligned word. Once it contains
                //NUL, C3A70..C3A80 reread live bytes and store sequentially.
                //Keep that order rather than using cached terminal bytes.
                for(;;) {
                    need(copied<storage_->size,"String strcpy overlap/nontermination outside bounded mapped heap");
                    const uint32_t ch=read(from++,1);
                    write(to++,ch,1);++copied;
                    if(!ch) { unique();++storage_->copies;return; }
                }
            } else {
                const uint32_t ch=read(from++,1);
                write(to++,ch,1);++copied;
                if(!ch) { unique();++storage_->copies;return; }
            }
        }
    }
    store(p,{source.begin(),source.begin()+n});unique();++storage_->copies;
}
void StringArena::assign_mapped753E0(uint32_t& w,const std::vector<uint8_t>& b,uint32_t a) {assign(w,b,a,false,0);}
void StringArena::assign753E0(uint32_t& w,uint32_t source) {
    need(ready() && source>=storage_->base && source<storage_->base+storage_->size,"String source is unmapped");
    //The ordinary owned/borrowed heap view must remain inside one live payload
    //and its mapped padding. Reading allocator headers/another object's bytes
    //as a string is a separate alias/lifetime domain, not an owning copy.
    uint32_t end=0;
    for(const auto& live:storage_->live) {
        const uint32_t next=read(live.first-12);
        if(source>=live.first && source<next){end=next;break;}
    }
    need(end!=0,"String source has no live payload/padding lifetime");
    assign(w,bytes(source,end-source),source,storage_->live.count(source)!=0,source);
}
void StringArena::copy7581C(uint32_t& dest,uint32_t src) {
    if(src>>28!=6){dest=src;return;}
    const uint32_t p=resolve75778(src);validate_owned(p);assign753E0(dest,p);
}
StringArenaSummary StringArena::summary() const {
    StringArenaSummary r;if(!ready())return r;r.known=true;
    r.allocations=storage_->allocations;r.reallocations=storage_->reallocations;r.releases=storage_->releases;r.copies=storage_->copies;
    r.live_allocations=static_cast<uint32_t>(storage_->live.size());
    r.conversion_cursor=storage_->cursor;
    if(storage_->digest.empty()) {
        core::Sha256 h;h.update(storage_->control.data(),storage_->control.size());h.update(storage_->heap.data(),storage_->heap.size());
        const uint8_t cursor[]={static_cast<uint8_t>(storage_->cursor),0,0,0};h.update(cursor,4);
        storage_->digest=core::hex_digest(h.digest());
    }
    r.sha256=storage_->digest;return r;
}
std::vector<uint8_t> StringArena::control_bytes() const {need(ready(),"String arena unavailable");return storage_->control;}
std::vector<uint8_t> StringArena::heap_bytes() const {need(ready(),"String arena unavailable");return storage_->heap;}
const std::map<uint32_t,uint32_t>& StringArena::live_blocks() const {need(ready(),"String arena unavailable");return storage_->live;}
void StringService20Selector0(StringArena& a,const StringFixture& f,uint32_t selector,uint32_t& r) {
    need(f.enabled && f.bss_window_mapped && f.memory_disjoint && a.ready(),
         "Service20 requires explicit mapped BSS10E490 string and initialized owned storage; producer/readiness unproved");
    need(selector==0,"Service20 selector outside recovered selector0 domain");
    need(f.bss_window.size()==16,"Service20 canonical BSS10E490 readable16-byte window missing");
    a.release7515C(r);a.assign_mapped753E0(r,f.bss_window);
}
} // namespace dokodemo::recovered
