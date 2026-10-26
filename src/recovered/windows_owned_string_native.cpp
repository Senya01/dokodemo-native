#include "windows_owned_string_native.h"
#include "string_native.h"
#include "../core_hash.h"
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace dokodemo::recovered {
namespace {
void Need(bool yes,const char* error){if(!yes)throw std::runtime_error(error);}
std::atomic<uint64_t> next_owner{0};
constexpr uint32_t kNativeScratchAddress=0x00800000;
std::vector<uint8_t> Padded(const std::string& text) {
    Need(text.find('\0')==std::string::npos && text.size()<0x400000,"Owned Inline8E text outside native NUL/capacity domain");
    std::vector<uint8_t> bytes((text.size()+4)&~size_t(3),0);
    std::copy(text.begin(),text.end(),bytes.begin());return bytes;
}
}
struct WindowsStringState {
    std::mutex mutex;
    StringArena arena;
    std::thread::id thread;
    bool active=true,failed=false;
    uint64_t owner=0,epoch=0,next_serial=0,allocations=0,copies=0,resizes=0,releases=0;
    struct Record {uint32_t word=0,bytes=0;std::array<uint8_t,32> sha{};};
    std::map<uint64_t,Record> records;
    void Release(uint64_t serial) {
        const auto it=records.find(serial);if(it==records.end())return;
        auto word=it->second.word;arena.release7515C(word);records.erase(it);++releases;
    }
};
struct WindowsStringLease {
    std::weak_ptr<WindowsStringState> state;
    uint64_t owner=0,epoch=0,serial=0;
    ~WindowsStringLease(){if(const auto s=state.lock()){
        std::lock_guard<std::mutex> lock(s->mutex);
        if(s->active&&!s->failed)try{s->Release(serial);}catch(...){s->failed=true;}
    }}
};
namespace {
void Current(const std::shared_ptr<WindowsStringState>& s) {
    Need(s&&s->active&&!s->failed&&s->thread==std::this_thread::get_id(),"Owned String arena unavailable/retired/foreign-thread");
}
const WindowsStringState::Record& Check(const std::shared_ptr<WindowsStringState>& s,
    const std::shared_ptr<WindowsStringLease>& lease,const std::string& text) {
    Current(s);Need(lease&&lease->state.lock()==s&&lease->owner==s->owner&&lease->epoch==s->epoch,
        "Owned String binding unbound/stale/foreign epoch");
    const auto it=s->records.find(lease->serial);
    Need(it!=s->records.end(),"Owned String allocation retired");
    const auto& r=it->second;const auto address=s->arena.pointer(r.word);
    Need(r.bytes==text.size()&&s->arena.owned_word(address)==r.word,"Owned String encoded allocation/extent mismatch");
    const auto bytes=s->arena.bytes(address,r.bytes+1);
    Need(bytes.back()==0&&std::equal(text.begin(),text.end(),bytes.begin(),
        [](char a,uint8_t b){return uint8_t(a)==b;}),"Owned String current bytes mismatch");
    return r;
}
std::shared_ptr<WindowsStringLease> Register(const std::shared_ptr<WindowsStringState>& s,
    uint32_t word,const std::string& text) {
    auto lease=std::make_shared<WindowsStringLease>();
    lease->owner=s->owner;lease->epoch=s->epoch;lease->serial=++s->next_serial;
    WindowsStringState::Record r;r.word=word;r.bytes=uint32_t(text.size());
    r.sha=core::sha256(reinterpret_cast<const uint8_t*>(text.data()),text.size());
    s->records.emplace(lease->serial,r);lease->state=s;return lease;
}
}
WindowsOwnedStringArena::~WindowsOwnedStringArena(){Restart();}
void WindowsOwnedStringArena::Restart()noexcept{
    if(state_){std::lock_guard<std::mutex> lock(state_->mutex);state_->active=false;}
    state_.reset();++epoch_;
}
void WindowsOwnedStringArena::Initialize() {
    if(state_)return;
    Need(policy_.heap_base>=0x01000000&&policy_.heap_base<=0x07000000,"Windows logical heap overlaps scratch/outside native range");
    auto s=std::make_shared<WindowsStringState>();
    s->arena=StringArena::WindowsOwned(policy_.heap_base,policy_.capacity,policy_.common_base);
    s->owner=++next_owner;s->epoch=epoch_;s->thread=std::this_thread::get_id();
    state_=std::move(s);
}
OwnedStringBinding WindowsOwnedStringArena::Create(const std::string& text) {
    const auto bytes=Padded(text);Initialize();auto s=state_;std::lock_guard<std::mutex> lock(s->mutex);Current(s);
    uint32_t word=0;
    try{s->arena.assign_mapped753E0(word,bytes,kNativeScratchAddress);OwnedStringBinding b;
        b.lease_=Register(s,word,text);++s->allocations;return b;
    }catch(...){s->failed=true;throw;}
}
OwnedStringBinding WindowsOwnedStringArena::Copy(const OwnedStringBinding& source,const std::string& text)const {
    return Assign({},std::string(),source,text);
}
OwnedStringBinding WindowsOwnedStringArena::Assign(const OwnedStringBinding& destination,const std::string& old_text,
    const OwnedStringBinding& source,const std::string& text)const {
    auto s=state_;Need(bool(s),"Owned String copy has no current arena");std::lock_guard<std::mutex> lock(s->mutex);
    const auto source_word=Check(s,source.lease_,text).word;
    uint32_t word=destination.bound()?Check(s,destination.lease_,old_text).word:0;
    try{s->arena.copy7581C(word,source_word);OwnedStringBinding out;out.lease_=Register(s,word,text);
        if(destination.bound())s->records.erase(destination.lease_->serial);
        ++s->copies;if(destination.bound())++s->resizes;else ++s->allocations;return out;
    }catch(...){s->failed=true;throw;}
}
OwnedStringBinding WindowsOwnedStringArena::Resize(const OwnedStringBinding& destination,const std::string& old_text,const std::string& text)const {
    const auto bytes=Padded(text);auto s=state_;Need(bool(s),"Owned String resize has no current arena");
    std::lock_guard<std::mutex> lock(s->mutex);uint32_t word=Check(s,destination.lease_,old_text).word;
    try{s->arena.assign_mapped753E0(word,bytes,kNativeScratchAddress);OwnedStringBinding out;out.lease_=Register(s,word,text);
        s->records.erase(destination.lease_->serial);++s->resizes;return out;
    }catch(...){s->failed=true;throw;}
}
OwnedStringReceipt WindowsOwnedStringArena::Observe(const OwnedStringBinding& binding,const std::string& text)const {
    auto s=state_;Need(bool(s),"Owned String condition has no current arena");std::lock_guard<std::mutex> lock(s->mutex);
    const auto& r=Check(s,binding.lease_,text);OwnedStringReceipt out;
    out.live=true;out.owner=s->owner;out.epoch=s->epoch;out.serial=binding.lease_->serial;
    out.address=s->arena.pointer(r.word);out.common_base=s->arena.common_base();out.encoded_word=r.word;
    out.bytes=r.bytes;out.sha256=r.sha;return out;
}
void WindowsOwnedStringArena::Release(OwnedStringBinding& binding)const {
    auto s=state_;Need(bool(s),"Owned String release has no current arena");
    {std::lock_guard<std::mutex> lock(s->mutex);Current(s);
        Need(binding.lease_&&binding.lease_->state.lock()==s&&binding.lease_->owner==s->owner
            &&binding.lease_->epoch==s->epoch&&s->records.count(binding.lease_->serial),"Owned String release stale/foreign");
        try{s->Release(binding.lease_->serial);}catch(...){s->failed=true;throw;}}
    binding.lease_.reset();
}
OwnedStringArenaReceipt WindowsOwnedStringArena::Snapshot()const {
    OwnedStringArenaReceipt out;out.epoch=epoch_;auto s=state_;if(!s)return out;
    std::lock_guard<std::mutex> lock(s->mutex);out.initialized=s->active;out.failed=s->failed;
    out.owner=s->owner;out.epoch=s->epoch;out.allocations=s->allocations;out.copies=s->copies;out.resizes=s->resizes;out.releases=s->releases;
    out.heap_base=policy_.heap_base;out.capacity=policy_.capacity;out.common_base=policy_.common_base;out.live_allocations=uint32_t(s->records.size());return out;
}
} // namespace dokodemo::recovered
