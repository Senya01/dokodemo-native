#include "sascore_runtime_win32.h"
#include "startup_memory_backend_win32.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace dokodemo {
namespace {
using namespace recovered;
std::atomic<uint64_t> next_semaphore_uid{0x20000000};
uint64_t TakeUid() noexcept {
    uint64_t uid=next_semaphore_uid.load();
    while(uid && uid<=uint64_t(INT32_MAX)) if(next_semaphore_uid.compare_exchange_weak(uid,uid+1)) return uid;
    return 0;
}
bool Valid(const NativeSascoreKey& key) noexcept {
    for(const auto n:key.module) if(!n) return false;
    for(const auto n:key.input) if(!n) return false;
    for(const auto n:key.state) if(!n) return false;
    return key.module[3]<=uint64_t(INT32_MAX) && key.state==std::array<uint64_t,3>{{key.module[0],key.module[1],key.module[2]}};
}
class WindowsSascore final : public NativeSascoreOperations {
    struct Impl {
        std::shared_ptr<WindowsStartupBackend> backend;
        std::shared_ptr<NativeCodecSessionState> session;
        std::shared_ptr<void> input;
        StartupNativeBlock block;
        StartupAllocationBorrow borrow;
        HANDLE semaphore=nullptr;
        NativeSascoreResources data;
        bool closed=false;
        std::unique_ptr<Impl> next;
        Impl(std::shared_ptr<WindowsStartupBackend> memory,std::shared_ptr<NativeCodecSessionState> state)
            :backend(std::move(memory)),session(std::move(state)){}
        bool Match(const NativeSascoreKey& key) const noexcept { return Valid(key) && data.issued && !data.retired && key==data.key; }
        bool Map() {
            if(!block || !data.memory_live) return false;
            const auto head=backend->Head(block);
            StartupHostStatus status;
            borrow=backend->Borrow(block,status);
            StartupRawBorrowSpan span;
            if(!head.status || !status || !borrow.Span(span) || !head.live || !head.readable || !head.writable ||
                !span.readable || !span.writable || head.handle.owner!=data.allocation_owner ||
                head.handle.generation!=data.allocation_generation || head.handle.serial!=data.allocation_serial ||
                span.handle.owner!=head.handle.owner || span.handle.generation!=head.handle.generation || span.handle.serial!=head.handle.serial ||
                head.source_base!=data.source_coordinate || span.source_base!=data.source_coordinate ||
                head.requested_bytes!=46644 || span.size!=46644 || head.committed_bytes!=data.committed_bytes ||
                head.page_bytes!=data.page_bytes || head.host_address!=data.memory_host_address ||
                span.host_address!=head.host_address || span.host_address!=uint64_t(reinterpret_cast<uintptr_t>(span.bytes)) ||
                !span.bytes || !span.initialized || span.host_address>UINT64_MAX-span.size) return false;
            data.memory_map_validated=true;return true;
        }
        NativeSascoreCode Free(uint32_t word,bool source) noexcept {
            try {
                if(!block || !data.memory_live || word!=data.source_coordinate || !Map()) return NativeSascoreCode::CleanupFailure;
                borrow.Reset();
                ++data.free_attempts;
                const auto freed=backend->Free(block);
                ++data.free_returns;data.free_backend_code=static_cast<int32_t>(freed.code);data.free_error=freed.win32_error;
                if(!freed) return NativeSascoreCode::CleanupFailure;
                block={};data.memory_live=false;data.memory_freed=true;++data.frees;
                if(source) data.free_source_returned=true; // sceMeFree is VOID: no numeric source result
                return NativeSascoreCode::Ok;
            } catch(...) { return NativeSascoreCode::CleanupFailure; }
        }
        NativeSascoreCode Delete(int32_t uid,bool source) noexcept {
            if(!semaphore || !data.semaphore_live || uid<=0 || uid!=data.semaphore_uid) return NativeSascoreCode::CleanupFailure;
            ++data.delete_attempts;
            const BOOL done=CloseHandle(semaphore);
            const DWORD error=done?0:GetLastError();
            if(done) semaphore=nullptr; // retire primitive before any telemetry
            ++data.delete_returns;data.delete_error=error;data.delete_native_succeeded=done!=FALSE;
            if(!done) return NativeSascoreCode::CleanupFailure;
            data.semaphore_live=false;data.semaphore_closed=true;++data.semaphore_closes;
            if(source) { data.delete_source_returned=true;data.delete_source_result=0; }
            return NativeSascoreCode::Ok;
        }
        NativeSascoreCode Retire() noexcept {
            if(!data.issued || data.retired) return NativeSascoreCode::Ok;
            data.retirement_entered=true;
            // Host abort is reverse primitive ownership, not recovered Stop.
            if(semaphore && Delete(data.semaphore_uid,false)!=NativeSascoreCode::Ok) return NativeSascoreCode::CleanupFailure;
            if(block && Free(data.source_coordinate,false)!=NativeSascoreCode::Ok) return NativeSascoreCode::CleanupFailure;
            borrow.Reset();input.reset();data.input_pinned=false;data.retired=true;data.retirement_returned=true;
            return NativeSascoreCode::Ok;
        }
    };
    std::unique_ptr<Impl> impl_;
    struct Deferred { SRWLOCK lock=SRWLOCK_INIT;std::unique_ptr<Impl> head; };
    static Deferred& Owners() { static Deferred owners;return owners; }
    static void Retain(std::unique_ptr<Impl> owner) noexcept {
        auto& owners=Owners();AcquireSRWLockExclusive(&owners.lock);
        owner->next=std::move(owners.head);owners.head=std::move(owner);
        ReleaseSRWLockExclusive(&owners.lock);
    }
public:
    WindowsSascore(std::shared_ptr<WindowsStartupBackend> memory,std::shared_ptr<NativeCodecSessionState> state)
        :impl_(std::make_unique<Impl>(std::move(memory),std::move(state))){}
    ~WindowsSascore() override { if(impl_ && Close()!=NativeSascoreCode::Ok) Retain(std::move(impl_)); }
    bool Ready() const noexcept override { return impl_ && !impl_->closed && impl_->backend && impl_->session; }
    NativeSascoreCode Allocate(const NativeSascoreKey& key,std::shared_ptr<void> input,NativeSascoreResources& out) noexcept override {
        if(!Ready() || !Valid(key) || !input || impl_->data.issued) return NativeSascoreCode::Refused;
        auto& s=*impl_;auto& d=s.data;
        if(s.session->cancelled.load()) return NativeSascoreCode::Cancelled;
        d.key=key;d.issued=true;s.input=std::move(input);d.input_pinned=true;
        try {
            SYSTEM_INFO info{};GetSystemInfo(&info);
            const uint64_t page=info.dwPageSize;
            const uint64_t committed=page?((uint64_t(46644)+page-1)/page)*page:0;
            uint64_t coordinate=s.session->next_coordinate.load();
            if(!page || !committed || !coordinate || coordinate%page || coordinate>UINT32_MAX ||
                committed>(uint64_t(1)<<32) || coordinate>(uint64_t(1)<<32)-committed ||
                !s.session->next_coordinate.compare_exchange_strong(coordinate,coordinate+committed)) { out=d;return NativeSascoreCode::Refused; }
            StartupPartitionRequest request;request.bytes=46644;request.source_base=uint32_t(coordinate);
            d.allocation_entered=true;
            const auto allocated=s.backend->Allocate(request);
            d.allocation_returned=true;d.allocation_backend_code=static_cast<int32_t>(allocated.status.code);d.allocation_error=allocated.status.win32_error;
            if(!allocated.status) { out=d;return NativeSascoreCode::Refused; } // ambiguous Allocate/Describe origin, NEVER source NULL
            s.block=allocated.handle; // primitive owner before fallible borrow/observations
            d.memory_live=true;++d.allocations;
            d.allocation_owner=s.block.owner;d.allocation_generation=s.block.generation;d.allocation_serial=s.block.serial;
            d.memory_host_address=allocated.host_address;d.committed_bytes=allocated.committed_bytes;d.page_bytes=allocated.page_bytes;
            d.requested_bytes=46644;d.source_coordinate=uint32_t(coordinate);
            if(allocated.source_base!=coordinate || allocated.requested_bytes!=46644 || allocated.committed_bytes!=committed ||
                allocated.page_bytes!=page || !allocated.live || !allocated.readable || !allocated.writable || !s.Map()) { out=d;return NativeSascoreCode::Refused; }
            StartupRawBorrowSpan span;
            if(!s.borrow.Span(span)) { out=d;return NativeSascoreCode::Refused; }
            // Admitted finite Windows calloc producer, distinct from module BSS.
            std::memset(span.bytes,0,46644);std::fill(span.initialized,span.initialized+46644,uint8_t(1));
            d.zeroed_bytes=46644;
            if(s.session->cancelled.load()) { out=d;return NativeSascoreCode::Cancelled; }
            d.calloc_source_returned=true;out=d;return NativeSascoreCode::Ok;
        } catch(...) { out=d;return NativeSascoreCode::Refused; }
    }
    NativeSascoreCode CreatePrivateSemaphore(const NativeSascoreKey& key,NativeSascoreResources& out) noexcept override {
        auto& s=*impl_;auto& d=s.data;
        if(!Ready() || !s.Match(key) || !d.calloc_source_returned || !d.memory_live || d.semaphore_create_entered) return NativeSascoreCode::Refused;
        if(s.session->cancelled.load()) { out=d;return NativeSascoreCode::Cancelled; }
        const auto uid=TakeUid();if(!uid) { out=d;return NativeSascoreCode::Refused; }
        d.semaphore_create_entered=true;
        s.semaphore=CreateSemaphoreW(nullptr,1,1,nullptr); // owned unnamed, non-inheritable; source diagnostic name SceSas
        const DWORD error=s.semaphore?0:GetLastError();
        d.semaphore_create_returned=true;d.semaphore_create_error=error;
        if(!s.semaphore) {
            d.semaphore_uid=-1;d.semaphore_source_returned=true;out=d;return NativeSascoreCode::Ok;
        }
        d.semaphore_live=true;++d.semaphore_creates;
        d.semaphore_handle=uint64_t(reinterpret_cast<uintptr_t>(s.semaphore));
        d.semaphore_uid=static_cast<int32_t>(uid); // independent source UID, no HANDLE truncation
        if(s.session->cancelled.load()) { out=d;return NativeSascoreCode::Cancelled; }
        d.semaphore_source_returned=true;out=d;return NativeSascoreCode::Ok;
    }
    NativeSascoreCode FreeMemory(const NativeSascoreKey& key,uint32_t word,bool source,NativeSascoreResources& out) noexcept override {
        if(!Ready() || !impl_->Match(key)) return NativeSascoreCode::Refused;
        const auto result=impl_->Free(word,source);out=impl_->data;return result;
    }
    NativeSascoreCode DeleteSemaphore(const NativeSascoreKey& key,int32_t uid,bool source,NativeSascoreResources& out) noexcept override {
        if(!Ready() || !impl_->Match(key)) return NativeSascoreCode::Refused;
        const auto result=impl_->Delete(uid,source);out=impl_->data;return result;
    }
    NativeSascoreCode Retire(const NativeSascoreKey& key,NativeSascoreResources& out) noexcept override {
        if(!Ready() || !impl_->Match(key)) return NativeSascoreCode::Refused;
        const auto result=impl_->Retire();out=impl_->data;return result;
    }
    bool HasObligations() const noexcept override { return impl_ && impl_->data.issued && !impl_->data.retired; }
    NativeSascoreCode Close() noexcept override {
        if(!impl_ || impl_->closed) return NativeSascoreCode::Ok;
        const auto result=impl_->Retire();if(result==NativeSascoreCode::Ok) impl_->closed=true;return result;
    }
    static uint32_t Pending() noexcept {
        auto& owners=Owners();AcquireSRWLockExclusive(&owners.lock);uint32_t count=0;
        for(auto* node=owners.head.get();node;node=node->next.get()) ++count;
        ReleaseSRWLockExclusive(&owners.lock);return count;
    }
    static NativeSascoreCode Retry() noexcept {
        auto& owners=Owners();AcquireSRWLockExclusive(&owners.lock);auto batch=std::move(owners.head);ReleaseSRWLockExclusive(&owners.lock);
        bool failed=false;
        while(batch) {
            auto current=std::move(batch);batch=std::move(current->next);
            if(current->Retire()!=NativeSascoreCode::Ok) { failed=true;Retain(std::move(current)); }
        }
        return failed?NativeSascoreCode::CleanupFailure:NativeSascoreCode::Ok;
    }
};
} // namespace
namespace recovered {
struct NativeSascoreWindowsFactory {
    static std::shared_ptr<NativeSascoreBackend> Make(std::shared_ptr<WindowsStartupBackend> memory,std::shared_ptr<NativeCodecSessionState> session) {
        return std::shared_ptr<NativeSascoreBackend>(new NativeSascoreBackend(std::make_unique<WindowsSascore>(std::move(memory),std::move(session))));
    }
};
}
std::shared_ptr<recovered::NativeSascoreBackend> CreateWindowsSascoreBackend(
    std::shared_ptr<WindowsStartupBackend> memory,std::shared_ptr<recovered::NativeCodecSessionState> session,recovered::NativeSascoreCode& status) {
    status=NativeSascoreCode::Refused;if(!memory || !session) return {};
    auto result=recovered::NativeSascoreWindowsFactory::Make(std::move(memory),std::move(session));
    if(!result->Ready()) return {};
    status=NativeSascoreCode::Ok;return result;
}
uint32_t PendingWindowsSascoreCleanup() noexcept { return WindowsSascore::Pending(); }
recovered::NativeSascoreCode RetryWindowsSascoreCleanup() noexcept { return WindowsSascore::Retry(); }
} // namespace dokodemo
