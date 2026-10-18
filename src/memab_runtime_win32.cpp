#include "memab_runtime_win32.h"
#include <windows.h>
#include <atomic>
#include <limits>

namespace dokodemo {
namespace {
using namespace recovered;
// Dedicated typed Windows service namespace, not global firmware ThreadMan.
std::atomic<uint64_t> next_memab_owner{1},next_memab_uid{0x30000000};
uint64_t Take(std::atomic<uint64_t>& source,uint64_t limit) noexcept {
    auto value=source.load();
    while(value && value<=limit) if(source.compare_exchange_weak(value,value+1)) return value;
    return 0;
}
bool Valid(const NativeMemabKey& key) noexcept {
    for(const auto value:key.module) if(!value) return false;
    for(const auto value:key.input) if(!value) return false;
    for(const auto value:key.state) if(!value) return false;
    return key.module[3]<=uint64_t(INT32_MAX) && key.state==std::array<uint64_t,3>{{key.module[0],key.module[1],key.module[2]}};
}
class WindowsMemab final : public NativeMemabOperations {
    struct Owner {
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::shared_ptr<void> input;
        NativeMemabResources data;
        HANDLE semaphore=nullptr;
        bool closed=false;
        std::unique_ptr<Owner> next;
        explicit Owner(std::shared_ptr<std::atomic<bool>> cancellation):cancelled(std::move(cancellation)) {
            data.backend_owner=Take(next_memab_owner,UINT64_MAX-1);data.backend_generation=1;
        }
        bool Match(const NativeMemabKey& key) const noexcept {
            return Valid(key) && data.issued && !data.retired && data.key==key;
        }
        NativeMemabCode Delete(int32_t uid,bool source) noexcept {
            if(!semaphore || !data.semaphore_live || uid<=0 || uid!=data.semaphore_uid ||
                (source && data.delete_source_returned)) return NativeMemabCode::Refused;
            ++data.delete_attempts;
            const BOOL done=CloseHandle(semaphore);
            const DWORD error=done?0:GetLastError();
            if(done) semaphore=nullptr; // primitive retirement BEFORE diagnostics
            ++data.delete_returns;
            if(source) {data.delete_error=error;data.delete_native_succeeded=done!=FALSE;}
            else data.host_retire_error=error;
            if(done) {data.semaphore_live=false;data.semaphore_closed=true;++data.semaphore_closes;}
            if(source) {
                data.delete_source_returned=true;data.delete_source_result=done?0:-1;
                return NativeMemabCode::Ok; // actual Windows service mapping; source ignores the number
            }
            return done?NativeMemabCode::Ok:NativeMemabCode::CleanupFailure;
        }
        NativeMemabCode Retire() noexcept {
            if(!data.issued || data.retired) return NativeMemabCode::Ok;
            data.retirement_entered=true;++data.host_retire_attempts;
            if(semaphore && Delete(data.semaphore_uid,false)!=NativeMemabCode::Ok) return NativeMemabCode::CleanupFailure;
            input.reset();data.input_pinned=false;data.retired=true;data.retirement_returned=true;
            return NativeMemabCode::Ok;
        }
    };
    std::unique_ptr<Owner> owner_;
    struct Deferred {SRWLOCK lock=SRWLOCK_INIT;std::unique_ptr<Owner> head;};
    static Deferred& Owners() {static Deferred owners;return owners;}
    static void Retain(std::unique_ptr<Owner> owner) noexcept {
        auto& owners=Owners();AcquireSRWLockExclusive(&owners.lock);
        owner->next=std::move(owners.head);owners.head=std::move(owner);
        ReleaseSRWLockExclusive(&owners.lock);
    }
public:
    explicit WindowsMemab(std::shared_ptr<std::atomic<bool>> cancelled):owner_(std::make_unique<Owner>(std::move(cancelled))) {}
    ~WindowsMemab() override {if(owner_ && Close()!=NativeMemabCode::Ok) Retain(std::move(owner_));}
    bool Ready() const noexcept override {return owner_ && !owner_->closed && owner_->cancelled && owner_->data.backend_owner;}
    NativeMemabCode Create(const NativeMemabKey& key,std::shared_ptr<void> input,NativeMemabResources& out) noexcept override {
        if(!Ready() || !Valid(key) || !input || owner_->data.issued) return NativeMemabCode::Refused;
        auto& s=*owner_;auto& d=s.data;
        if(s.cancelled->load()) return NativeMemabCode::Cancelled;
        const auto uid=Take(next_memab_uid,0x3FFFFFFF); // exhaustion BEFORE acquiring a HANDLE/input obligation
        if(!uid) return NativeMemabCode::Exhausted;
        d.key=key;d.issued=true;d.backend_serial=1;s.input=std::move(input);d.input_pinned=true;
        d.create_entered=true;++d.create_attempts;
        s.semaphore=CreateSemaphoreW(nullptr,1,1,nullptr); // source name SceMemab; Windows object deliberately unnamed
        const DWORD error=s.semaphore?0:GetLastError();
        d.create_returned=true;++d.create_returns;d.create_error=error;
        if(s.semaphore) {
            d.semaphore_live=true;d.semaphore_uid=static_cast<int32_t>(uid);
            d.semaphore_handle=uint64_t(reinterpret_cast<uintptr_t>(s.semaphore));
        } else d.semaphore_uid=-1;
        if(s.cancelled->load()) {out=d;return NativeMemabCode::Cancelled;}
        d.create_source_returned=true;d.create_source_result=d.semaphore_uid;out=d;
        return NativeMemabCode::Ok;
    }
    NativeMemabCode Delete(const NativeMemabKey& key,int32_t uid,bool source,NativeMemabResources& out) noexcept override {
        if(!Ready() || !owner_->Match(key)) return NativeMemabCode::Refused;
        const auto result=owner_->Delete(uid,source);out=owner_->data;return result;
    }
    NativeMemabCode Retire(const NativeMemabKey& key,NativeMemabResources& out) noexcept override {
        if(!Ready() || !owner_->Match(key)) return NativeMemabCode::Refused;
        const auto result=owner_->Retire();out=owner_->data;return result;
    }
    bool HasObligations() const noexcept override {return owner_ && owner_->data.issued && !owner_->data.retired;}
    NativeMemabCode Close() noexcept override {
        if(!owner_ || owner_->closed) return NativeMemabCode::Ok;
        const auto result=owner_->Retire();if(result==NativeMemabCode::Ok) owner_->closed=true;return result;
    }
    static uint32_t Pending() noexcept {
        auto& owners=Owners();AcquireSRWLockExclusive(&owners.lock);uint32_t n=0;
        for(auto* p=owners.head.get();p;p=p->next.get()) ++n;
        ReleaseSRWLockExclusive(&owners.lock);return n;
    }
    static NativeMemabCode Retry() noexcept {
        auto& owners=Owners();AcquireSRWLockExclusive(&owners.lock);auto batch=std::move(owners.head);ReleaseSRWLockExclusive(&owners.lock);
        bool failed=false;
        while(batch) {
            auto current=std::move(batch);batch=std::move(current->next);
            if(current->Retire()!=NativeMemabCode::Ok) {failed=true;Retain(std::move(current));}
        }
        return failed?NativeMemabCode::CleanupFailure:NativeMemabCode::Ok;
    }
};
}
namespace recovered {
struct NativeMemabWindowsFactory {
    static std::shared_ptr<NativeMemabBackend> Make(std::shared_ptr<std::atomic<bool>> cancelled) {
        return std::shared_ptr<NativeMemabBackend>(new NativeMemabBackend(std::make_unique<WindowsMemab>(std::move(cancelled))));
    }
};
}
std::shared_ptr<NativeMemabBackend> CreateWindowsMemabBackend(std::shared_ptr<std::atomic<bool>> cancelled,NativeMemabCode& status) {
    status=NativeMemabCode::Refused;if(!cancelled) return {};
    auto result=recovered::NativeMemabWindowsFactory::Make(std::move(cancelled));
    if(!result->Ready()) return {};
    status=NativeMemabCode::Ok;return result;
}
uint32_t PendingWindowsMemabCleanup() noexcept {return WindowsMemab::Pending();}
NativeMemabCode RetryWindowsMemabCleanup() noexcept {return WindowsMemab::Retry();}
} // namespace dokodemo
