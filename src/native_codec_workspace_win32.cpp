#include "native_codec_workspace_win32.h"
#include "startup_memory_backend_win32.h"
#include <windows.h>
#include <array>
#include <cerrno>
#include <limits>
#include <new>
#include <utility>
#ifdef DOKODEMO_FFMPEG_ROOT
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/version.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/version.h>
}
#endif

namespace dokodemo::recovered {
#ifdef DOKODEMO_FFMPEG_ROOT
namespace {
struct CodecApi {
    HMODULE util = nullptr, swr = nullptr, codec = nullptr;
    decltype(&avcodec_alloc_context3) alloc = nullptr;
    decltype(&avcodec_free_context) free = nullptr;
    decltype(&avcodec_find_decoder) find = nullptr;
    decltype(&avcodec_open2) open = nullptr;
    decltype(&av_channel_layout_default) layout = nullptr;
    uint32_t codec_version = 0, util_version = 0;
    ~CodecApi() { if(codec) FreeLibrary(codec); if(swr) FreeLibrary(swr); if(util) FreeLibrary(util); }
};
bool LoadedPath(HMODULE module, const std::wstring& expected) {
    std::array<wchar_t,32768> actual{}, canonical{};
    const DWORD n=GetModuleFileNameW(module,actual.data(),DWORD(actual.size()));
    const DWORD m=GetFullPathNameW(expected.c_str(),DWORD(canonical.size()),canonical.data(),nullptr);
    return n && n<actual.size() && m && m<canonical.size() &&
        CompareStringOrdinal(actual.data(),int(n),canonical.data(),int(m),TRUE)==CSTR_EQUAL;
}
template<class T> T Symbol(HMODULE m,const char* name) noexcept { return reinterpret_cast<T>(GetProcAddress(m,name)); }
// This is the actual placement object. Its context and DLL pin move together;
// sizeof this object is the EXTERNAL envelope requirement, not decoder total.
struct NativeCodecWorkspace {
    std::shared_ptr<CodecApi> api;
    AVCodecContext* context = nullptr;
    NativeCodecWorkspace(std::shared_ptr<CodecApi>&& a,AVCodecContext*& c) noexcept
        : api(std::move(a)),context(std::exchange(c,nullptr)) {}
    ~NativeCodecWorkspace() noexcept { if(context) api->free(&context); }
};
static_assert(alignof(NativeCodecWorkspace)<=64);
static_assert(sizeof(NativeCodecWorkspace)<=UINT32_MAX-126u);
struct WorkspaceSlot {
    NativeCodecWorkspaceSnapshot data;
    std::shared_ptr<CodecApi> api;
    std::shared_ptr<void> provider_input,consumer_input;
    AVCodecContext* provisional = nullptr;
    StartupNativeBlock block;
    StartupAllocationBorrow borrow;
    NativeCodecWorkspace* object = nullptr;
    ~WorkspaceSlot() { if(provisional) api->free(&provisional); }
};
bool KeyValid(const NativeCodecWorkspaceKey& k) noexcept {
    if(k.record_index>=4) return false;
    for(auto x:k.provider_module) if(!x) return false;
    for(auto x:k.consumer_module) if(!x) return false;
    for(const auto* tuple:{&k.provider,&k.provider_input,&k.consumer_input,&k.state})
        for(auto x:*tuple) if(!x) return false;
    return k.provider_module!=k.consumer_module;
}
}
#endif
struct WindowsCodecWorkspace final : NativeCodecWorkspaceOperations {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WindowsCodecWorkspace(std::unique_ptr<Impl>) noexcept;
    ~WindowsCodecWorkspace() override;
    bool Ready() const noexcept override;
    bool HasResources() const noexcept override;
    NativeCodecBackendStatus Close() noexcept override;
    NativeCodecBackendStatus Prepare(const NativeCodecWorkspaceKey&,uint8_t,uint8_t,std::shared_ptr<void>,std::shared_ptr<void>,NativeCodecWorkspaceSnapshot&) noexcept override;
    NativeCodecBackendStatus PrepareUnconfigured(const NativeCodecWorkspaceKey&,std::shared_ptr<void>,std::shared_ptr<void>,NativeCodecWorkspaceSnapshot&) noexcept override;
    NativeCodecBackendStatus Allocate(const NativeCodecWorkspaceKey&,uint32_t,NativeCodecWorkspaceSnapshot&) noexcept override;
    NativeCodecBackendStatus Retire(const NativeCodecWorkspaceKey&,NativeCodecWorkspaceSnapshot&) noexcept override;
    NativeCodecBackendStatus Release(const NativeCodecWorkspaceKey&,uint32_t,uint32_t,uint32_t,NativeCodecWorkspaceSnapshot&) noexcept override;
};
struct WindowsCodecWorkspace::Impl {
    std::shared_ptr<WindowsStartupBackend> backend;
    std::shared_ptr<NativeCodecSessionState> session;
#ifdef DOKODEMO_FFMPEG_ROOT
    std::shared_ptr<CodecApi> api;
    std::array<WorkspaceSlot,4> slots;
#endif
    std::unique_ptr<Impl> next_deferred;
};
namespace {
struct DeferredCodecCleanup {
    SRWLOCK mutex=SRWLOCK_INIT;
    std::unique_ptr<WindowsCodecWorkspace::Impl> head;
};
DeferredCodecCleanup& DeferredCleanup() { static DeferredCodecCleanup value;return value; }
uint32_t DeferredCount() noexcept {
    auto& list=DeferredCleanup();AcquireSRWLockExclusive(&list.mutex);
    uint32_t n=0;for(auto* p=list.head.get();p;p=p->next_deferred.get()) ++n;
    ReleaseSRWLockExclusive(&list.mutex);return n;
}
}
WindowsCodecWorkspace::WindowsCodecWorkspace(std::unique_ptr<Impl> p) noexcept : impl_(std::move(p)) {}
bool WindowsCodecWorkspace::Ready() const noexcept {
#ifdef DOKODEMO_FFMPEG_ROOT
    return impl_ && impl_->api && impl_->backend && impl_->session;
#else
    return false;
#endif
}
bool WindowsCodecWorkspace::HasResources() const noexcept {
#ifdef DOKODEMO_FFMPEG_ROOT
    if(impl_) for(const auto& s:impl_->slots) if(s.provisional || s.block || s.object) return true;
#endif
    return false;
}
WindowsCodecWorkspace::~WindowsCodecWorkspace() {
#ifdef DOKODEMO_FFMPEG_ROOT
    if(!impl_) return;
    bool pending=false;
    for(size_t i=impl_->slots.size();i;--i) {
        auto& s=impl_->slots[i-1];
        if(s.data.state==NativeCodecWorkspaceState::Unissued || s.data.state==NativeCodecWorkspaceState::Retired) continue;
        NativeCodecWorkspaceSnapshot ignored;
        if(Retire(s.data.key,ignored)!=NativeCodecBackendStatus::Ok) pending=true;
    }
    if(pending) {
        // Intrusive unique ownership: no allocation is needed during destructor
        // fallback, every earlier slot was attempted, and public retry can drain.
        auto& list=DeferredCleanup();AcquireSRWLockExclusive(&list.mutex);
        impl_->next_deferred=std::move(list.head);list.head=std::move(impl_);
        ReleaseSRWLockExclusive(&list.mutex);
    }
#endif
}
NativeCodecBackendStatus WindowsCodecWorkspace::Prepare(const NativeCodecWorkspaceKey& key,uint8_t b1,uint8_t b2,
                                                            std::shared_ptr<void> provider_input,std::shared_ptr<void> consumer_input,
                                                            NativeCodecWorkspaceSnapshot& out) noexcept {
#ifdef DOKODEMO_FFMPEG_ROOT
    if(!Ready() || !KeyValid(key) || key.record_index>=2 || !provider_input || !consumer_input) return NativeCodecBackendStatus::Refused;
    auto& s=impl_->slots[key.record_index];
    if(s.data.state!=NativeCodecWorkspaceState::Unissued) return NativeCodecBackendStatus::Refused;
    if(impl_->session->cancelled.load()) return NativeCodecBackendStatus::Cancelled;
    auto& d=s.data; d.key=key; d.record_index=key.record_index; d.codec=0x1000;
    d.format_known=true;
    d.format_b1=b1; d.format_b2=b2; d.channels=(b1>>2)&7;
    d.max_input_bytes=(((b1&3u)<<8)|b2)*8u+8u;
    d.api_codec_version=impl_->api->codec_version; d.api_util_version=impl_->api->util_version;
    d.envelope_bytes=uint32_t(sizeof(NativeCodecWorkspace)); d.envelope_alignment=uint32_t(alignof(NativeCodecWorkspace));
    d.state=NativeCodecWorkspaceState::Preparing; s.api=impl_->api;
    s.provider_input=std::move(provider_input);s.consumer_input=std::move(consumer_input);
    if(d.channels!=2 || b1!=0x28 || b2!=0x5C) { d.state=NativeCodecWorkspaceState::Failed; out=d; return NativeCodecBackendStatus::Refused; }
    d.native_decoder_find_entered=true;
    const AVCodec* codec=s.api->find(AV_CODEC_ID_ATRAC3P);
    d.native_decoder_find_returned=true;d.native_decoder_found=codec!=nullptr;
    if(!codec) { d.state=NativeCodecWorkspaceState::Failed; out=d; return NativeCodecBackendStatus::Unsupported; }
    d.native_decoder_id=uint32_t(codec->id);
    if(codec->id!=AV_CODEC_ID_ATRAC3P || codec->type!=AVMEDIA_TYPE_AUDIO) { d.state=NativeCodecWorkspaceState::Failed;out=d;return NativeCodecBackendStatus::Refused; }
    d.native_prepare_entered=true;
    d.native_alloc_entered=true;
    s.provisional=s.api->alloc(codec);
    d.native_alloc_returned=true;
    if(!s.provisional) {
        d.native_prepare_returned=true; d.native_error_class=1;
        d.native_policy_return=true; d.native_policy_result=-2;
        d.state=NativeCodecWorkspaceState::Failed; out=d; return NativeCodecBackendStatus::NativeFailure;
    }
    ++d.context_allocations; d.provisional_live=true;d.context_allocated=true;
    s.api->layout(&s.provisional->ch_layout,int(d.channels));
    s.provisional->block_align=int(d.max_input_bytes);
    s.provisional->thread_count=1; // explicit single-thread Windows prepare policy
    d.native_open_entered=true;
    d.native_api_result=s.api->open(s.provisional,codec,nullptr);
    d.native_open_returned=true;
    d.decoder_opened=d.native_api_result==0;
    d.native_prepare_returned=true;
    if(impl_->session->cancelled.load()) { out=d; return NativeCodecBackendStatus::Cancelled; }
    d.native_policy_return=true;
    if(d.native_api_result<0) {
        d.native_error_class=d.native_api_result==AVERROR(ENOMEM)?1:2;
        d.native_policy_result=d.native_error_class==1?-2:-3;
        s.api->free(&s.provisional); ++d.context_frees; d.provisional_live=false; d.context_destroyed=true;
        d.state=NativeCodecWorkspaceState::Failed; out=d; return NativeCodecBackendStatus::NativeFailure;
    }
    if(d.native_api_result!=0) { d.native_policy_return=false; out=d; return NativeCodecBackendStatus::Refused; }
    d.decoder_ready=true;
    d.native_policy_result=0; d.state=NativeCodecWorkspaceState::Prepared; out=d; return NativeCodecBackendStatus::Ok;
#else
    (void)key;(void)b1;(void)b2;(void)provider_input;(void)consumer_input;(void)out;return NativeCodecBackendStatus::Unsupported;
#endif
}
NativeCodecBackendStatus WindowsCodecWorkspace::PrepareUnconfigured(const NativeCodecWorkspaceKey& key,
        std::shared_ptr<void> provider_input,std::shared_ptr<void> consumer_input,NativeCodecWorkspaceSnapshot& out) noexcept {
#ifdef DOKODEMO_FFMPEG_ROOT
    if(!Ready() || !KeyValid(key) || key.record_index<2 || !provider_input || !consumer_input)
        return NativeCodecBackendStatus::Refused;
    auto& s=impl_->slots[key.record_index];
    if(s.data.state!=NativeCodecWorkspaceState::Unissued) return NativeCodecBackendStatus::Refused;
    if(impl_->session->cancelled.load()) return NativeCodecBackendStatus::Cancelled;
    auto& d=s.data;d.key=key;d.record_index=key.record_index;d.codec=0x1001;
    d.format_pending=true;d.state=NativeCodecWorkspaceState::Preparing;
    d.api_codec_version=impl_->api->codec_version;d.api_util_version=impl_->api->util_version;
    d.envelope_bytes=uint32_t(sizeof(NativeCodecWorkspace));d.envelope_alignment=uint32_t(alignof(NativeCodecWorkspace));
    s.api=impl_->api;s.provider_input=std::move(provider_input);s.consumer_input=std::move(consumer_input);
    d.native_prepare_entered=true;
    d.native_decoder_find_entered=true;
    const AVCodec* codec=s.api->find(AV_CODEC_ID_ATRAC3);
    d.native_decoder_find_returned=true;d.native_decoder_found=codec!=nullptr;
    if(!codec) { d.native_prepare_returned=true;d.state=NativeCodecWorkspaceState::Failed;out=d;return NativeCodecBackendStatus::Unsupported; }
    d.native_decoder_id=uint32_t(codec->id);
    if(codec->id!=AV_CODEC_ID_ATRAC3 || codec->type!=AVMEDIA_TYPE_AUDIO) { d.native_prepare_returned=true;d.state=NativeCodecWorkspaceState::Failed;out=d;return NativeCodecBackendStatus::Refused; }
    // Pinned options.c allocates/defaults context and private ownership only.
    // ATRAC3 init requires later format/extradata. No format byte, AVCodecContext
    // format field, init/open or decode is touched on this allocation-only path.
    d.native_alloc_entered=true;s.provisional=s.api->alloc(codec);d.native_alloc_returned=true;
    d.native_prepare_returned=true;
    if(!s.provisional) {
        d.native_error_class=1;d.native_policy_return=true;d.native_policy_result=-2;
        d.state=NativeCodecWorkspaceState::Failed;out=d;return NativeCodecBackendStatus::NativeFailure;
    }
    ++d.context_allocations;d.context_allocated=true;d.provisional_live=true;
    d.state=NativeCodecWorkspaceState::AllocatedFormatPending;
    if(impl_->session->cancelled.load()) { out=d;return NativeCodecBackendStatus::Cancelled; }
    d.native_policy_return=true;d.native_policy_result=0;out=d;return NativeCodecBackendStatus::Ok;
#else
    (void)key;(void)provider_input;(void)consumer_input;(void)out;return NativeCodecBackendStatus::Unsupported;
#endif
}
NativeCodecBackendStatus WindowsCodecWorkspace::Allocate(const NativeCodecWorkspaceKey& key,uint32_t need,
                                                             NativeCodecWorkspaceSnapshot& out) noexcept {
#ifdef DOKODEMO_FFMPEG_ROOT
    if(!Ready() || !KeyValid(key)) return NativeCodecBackendStatus::Refused;
    auto& s=impl_->slots[key.record_index]; auto& d=s.data;
    if(!(d.key==key) || (d.state!=NativeCodecWorkspaceState::Prepared &&
        d.state!=NativeCodecWorkspaceState::AllocatedFormatPending) || !s.provisional || need!=d.envelope_bytes ||
        d.allocation_attempted || s.block || s.object)
        return NativeCodecBackendStatus::Refused;
    if(impl_->session->cancelled.load()) return NativeCodecBackendStatus::Cancelled;
    try {
        const uint32_t request=(need+63u)|63u;
        d.requested_bytes=request;
        SYSTEM_INFO info{}; GetSystemInfo(&info); const uint64_t page=info.dwPageSize;
        const uint64_t committed=page?((uint64_t(request)+page-1)/page)*page:0;
        uint64_t raw=impl_->session->next_coordinate.load();
        if(!page || !committed || raw%page || !raw || raw==0x00200000 || raw>UINT32_MAX ||
            committed>(uint64_t(1)<<32) || raw>(uint64_t(1)<<32)-committed)
            return NativeCodecBackendStatus::Refused;
        if(!impl_->session->next_coordinate.compare_exchange_strong(raw,raw+committed)) return NativeCodecBackendStatus::Refused;
        d.allocation_attempted=true; // one acquisition; a retained block can NEVER be overwritten on retry
        StartupPartitionRequest q; q.bytes=request; q.source_base=uint32_t(raw);
        d.backend_allocate_entered=true;
        const auto allocated=impl_->backend->Allocate(q);
        d.backend_allocate_returned=true;d.backend_allocate_code=static_cast<int32_t>(allocated.status.code);
        if(!allocated.status) {
            d.win32_error=allocated.status.win32_error;
            // This API also reports Describe/VirtualQuery rollback failures as
            // HostFailure. Without exact origin it cannot mean allocator raw0.
            out=d;return NativeCodecBackendStatus::Refused;
        }
        s.block=allocated.handle; // actual primitive owner BEFORE any fallible work
        d.allocation_owner=s.block.owner;d.allocation_generation=s.block.generation;d.allocation_serial=s.block.serial;
        d.requested_bytes=request; d.committed_bytes=allocated.committed_bytes;
        StartupHostStatus status; s.borrow=impl_->backend->Borrow(s.block,status);
        StartupRawBorrowSpan span;
        const uint64_t aligned=(raw+63u)&~uint64_t(63u),offset=aligned-raw;
        if(!status || !s.borrow.Span(span) || !span.readable || !span.writable ||
            span.handle.owner!=s.block.owner || span.handle.generation!=s.block.generation || span.handle.serial!=s.block.serial ||
            span.source_base!=raw || span.size!=request || allocated.source_base!=raw ||
            !allocated.live || !allocated.readable || !allocated.writable || allocated.requested_bytes!=request ||
            allocated.committed_bytes!=committed || allocated.page_bytes!=page ||
            span.host_address!=allocated.host_address || span.host_address!=uint64_t(reinterpret_cast<uintptr_t>(span.bytes)) ||
            offset>span.size || need>span.size-offset || span.host_address>UINT64_MAX-span.size ||
            aligned>UINT32_MAX || aligned%64 || (span.host_address+offset)%64 ||
            (span.host_address+offset)%alignof(NativeCodecWorkspace)) { out=d; return NativeCodecBackendStatus::Refused; }
        if(impl_->session->cancelled.load()) { out=d; return NativeCodecBackendStatus::Cancelled; }
        s.object=new(span.bytes+offset) NativeCodecWorkspace(std::move(s.api),s.provisional);
        d.provisional_live=false; d.placement_constructed=true; d.context_transferred=true;
        d.host_address=span.host_address+offset; d.raw_coordinate=uint32_t(raw);d.aligned_coordinate=uint32_t(aligned);
        d.allocation_returned=true; d.allocation_succeeded=true; d.state=NativeCodecWorkspaceState::WorkspaceLive;
        out=d; return NativeCodecBackendStatus::Ok;
    } catch(...) { out=d; return NativeCodecBackendStatus::Refused; }
#else
    (void)key;(void)need;(void)out;return NativeCodecBackendStatus::Unsupported;
#endif
}
NativeCodecBackendStatus WindowsCodecWorkspace::Retire(const NativeCodecWorkspaceKey& key,NativeCodecWorkspaceSnapshot& out) noexcept {
#ifdef DOKODEMO_FFMPEG_ROOT
    if(!Ready() || !KeyValid(key)) return NativeCodecBackendStatus::Refused;
    auto& s=impl_->slots[key.record_index]; auto& d=s.data;
    if(!(d.key==key)) return NativeCodecBackendStatus::Refused;
    try {
        if(s.object) {
            const auto head=impl_->backend->Head(s.block);
            StartupRawBorrowSpan span;
            const uint64_t offset=uint64_t(d.aligned_coordinate)-d.raw_coordinate;
            if(!head.status || !head.live || !head.readable || !head.writable || !s.borrow.Span(span) ||
                head.handle.owner!=d.allocation_owner || head.handle.generation!=d.allocation_generation || head.handle.serial!=d.allocation_serial ||
                head.source_base!=d.raw_coordinate || head.requested_bytes!=d.requested_bytes || head.committed_bytes!=d.committed_bytes ||
                span.handle.owner!=head.handle.owner || span.handle.generation!=head.handle.generation || span.handle.serial!=head.handle.serial ||
                span.source_base!=head.source_base || span.host_address!=head.host_address || span.size!=d.requested_bytes ||
                offset>span.size || d.envelope_bytes>span.size-offset || span.host_address>UINT64_MAX-offset ||
                span.host_address+offset!=d.host_address || d.host_address!=uint64_t(reinterpret_cast<uintptr_t>(s.object)) || d.host_address%64) {
                out=d;return NativeCodecBackendStatus::CleanupFailure;
            }
            if(d.windows_release_capability_applied) d.release_map_validated=true;
            s.object->~NativeCodecWorkspace();s.object=nullptr;++d.context_frees;
            d.context_destroyed=true;d.decoder_ready=false;d.state=NativeCodecWorkspaceState::DestroyedPendingFree;
        } else if(s.block && d.context_transferred) {
            // A failed Free has already destroyed the context and reset its
            // borrow. Retry must validate the retained map again, without
            // reconstructing/destroying the placement object a second time.
            const auto head=impl_->backend->Head(s.block);
            StartupHostStatus status;s.borrow=impl_->backend->Borrow(s.block,status);
            StartupRawBorrowSpan span;const uint64_t offset=uint64_t(d.aligned_coordinate)-d.raw_coordinate;
            if(!head.status || !head.live || !head.readable || !head.writable || !status || !s.borrow.Span(span) ||
                head.handle.owner!=d.allocation_owner || head.handle.generation!=d.allocation_generation || head.handle.serial!=d.allocation_serial ||
                head.source_base!=d.raw_coordinate || head.requested_bytes!=d.requested_bytes || head.committed_bytes!=d.committed_bytes ||
                span.handle.owner!=head.handle.owner || span.handle.generation!=head.handle.generation || span.handle.serial!=head.handle.serial ||
                span.source_base!=head.source_base || span.host_address!=head.host_address || span.size!=d.requested_bytes ||
                offset>span.size || d.envelope_bytes>span.size-offset || span.host_address>UINT64_MAX-offset ||
                span.host_address+offset!=d.host_address || !d.context_destroyed || d.host_address%64) {
                out=d;return NativeCodecBackendStatus::CleanupFailure;
            }
            if(d.windows_release_capability_applied) d.release_map_validated=true;
        }
        if(s.provisional) { s.api->free(&s.provisional); ++d.context_frees; d.provisional_live=false; d.context_destroyed=true;d.decoder_ready=false; }
        s.borrow.Reset();
        if(s.block) {
            const auto status=impl_->backend->Free(s.block); d.win32_error=status.win32_error;
            if(!status) { d.state=NativeCodecWorkspaceState::DestroyedPendingFree; out=d; return NativeCodecBackendStatus::CleanupFailure; }
            s.block={}; d.storage_freed=true;
        }
        s.api.reset();s.provider_input.reset();s.consumer_input.reset();
        d.state=NativeCodecWorkspaceState::Retired; out=d; return NativeCodecBackendStatus::Ok;
    } catch(...) { out=d;return NativeCodecBackendStatus::CleanupFailure; }
#else
    (void)key;(void)out;return NativeCodecBackendStatus::Unsupported;
#endif
}
NativeCodecBackendStatus WindowsCodecWorkspace::Release(const NativeCodecWorkspaceKey& key,uint32_t aligned,
        uint32_t need,uint32_t raw,NativeCodecWorkspaceSnapshot& out) noexcept {
#ifdef DOKODEMO_FFMPEG_ROOT
    if(!Ready() || !KeyValid(key)) return NativeCodecBackendStatus::Refused;
    auto& s=impl_->slots[key.record_index];auto& d=s.data;
    if(!(d.key==key) || (d.state!=NativeCodecWorkspaceState::WorkspaceLive &&
        d.state!=NativeCodecWorkspaceState::DestroyedPendingFree) || !s.block || !d.context_transferred ||
        !d.allocation_succeeded || raw!=d.raw_coordinate || aligned!=d.aligned_coordinate || need!=d.envelope_bytes ||
        !raw || ((raw+63u)&~63u)!=aligned || d.release_native_attempts==UINT32_MAX)
        return NativeCodecBackendStatus::Refused;
    d.original_low29_guard_passed=(raw&0x1FFFFFFFu)<=0x001FFFFFu;
    // Explicit Windows capability substitutes PSP physical-address admission.
    // Do not move native coordinates to make the original low29 test pass.
    d.windows_release_capability_applied=true;
    ++d.release_native_attempts;
    const auto result=Retire(key,out);
    ++d.release_native_returns;d.release_native_code=static_cast<int32_t>(result);
    d.release_native_completed=result==NativeCodecBackendStatus::Ok;
    out=d;return result;
#else
    (void)key;(void)aligned;(void)need;(void)raw;(void)out;return NativeCodecBackendStatus::Unsupported;
#endif
}
NativeCodecBackendStatus WindowsCodecWorkspace::Close() noexcept {
    if(HasResources()) return NativeCodecBackendStatus::CleanupFailure;
#ifdef DOKODEMO_FFMPEG_ROOT
    if(impl_) {
        // Even a failed preflight can retain input/API pins without a context.
        for(auto& s:impl_->slots) { s.api.reset();s.provider_input.reset();s.consumer_input.reset();s.data.state=NativeCodecWorkspaceState::Retired; }
        impl_->api.reset();
    }
#endif
    if(impl_) { impl_->backend.reset();impl_->session.reset(); }
    return NativeCodecBackendStatus::Ok;
}
struct NativeCodecWindowsFactory {
    static std::shared_ptr<NativeCodecWorkspaceBackend> Create(std::shared_ptr<WindowsStartupBackend> backend,
        std::shared_ptr<NativeCodecSessionState> session,const std::wstring& root,NativeCodecBackendStatus& status) {
        status=NativeCodecBackendStatus::Unsupported;
#ifdef DOKODEMO_FFMPEG_ROOT
        if(!backend || !session || root.empty()) return {};
        auto p=std::make_unique<WindowsCodecWorkspace::Impl>(); p->backend=std::move(backend);p->session=std::move(session);
        auto api=std::make_shared<CodecApi>();
        const std::wstring util=root+L"/bin/avutil-59.dll",swr=root+L"/bin/swresample-5.dll",codec=root+L"/bin/avcodec-61.dll";
        const auto existing_ok=[](const wchar_t* name,const std::wstring& path) {
            const HMODULE existing=GetModuleHandleW(name);return !existing || LoadedPath(existing,path);
        };
        if(!existing_ok(L"avutil-59.dll",util) || !existing_ok(L"swresample-5.dll",swr) || !existing_ok(L"avcodec-61.dll",codec)) return {};
        constexpr DWORD flags=LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32;
        api->util=LoadLibraryExW(util.c_str(),nullptr,flags);
        if(!api->util || !LoadedPath(api->util,util)) return {};
        api->swr=LoadLibraryExW(swr.c_str(),nullptr,flags);
        if(!api->swr || !LoadedPath(api->swr,swr)) return {};
        api->codec=LoadLibraryExW(codec.c_str(),nullptr,flags);
        if(!api->codec || !LoadedPath(api->codec,codec)) return {};
        const auto cv=Symbol<decltype(&avcodec_version)>(api->codec,"avcodec_version");
        const auto uv=Symbol<decltype(&avutil_version)>(api->util,"avutil_version");
        if(!cv || !uv) return {};
        api->codec_version=cv();api->util_version=uv();
        if(api->codec_version!=LIBAVCODEC_VERSION_INT || api->util_version!=LIBAVUTIL_VERSION_INT ||
            LIBAVCODEC_VERSION_INT!=((61u<<16)|(19u<<8)|101u) || LIBAVUTIL_VERSION_INT!=((59u<<16)|(39u<<8)|100u)) return {};
        api->alloc=Symbol<decltype(api->alloc)>(api->codec,"avcodec_alloc_context3");
        api->free=Symbol<decltype(api->free)>(api->codec,"avcodec_free_context");
        api->find=Symbol<decltype(api->find)>(api->codec,"avcodec_find_decoder");
        api->open=Symbol<decltype(api->open)>(api->codec,"avcodec_open2");
        api->layout=Symbol<decltype(api->layout)>(api->util,"av_channel_layout_default");
        if(!api->alloc || !api->free || !api->find || !api->open || !api->layout) return {};
        p->api=std::move(api); status=NativeCodecBackendStatus::Ok;
        return std::shared_ptr<NativeCodecWorkspaceBackend>(new NativeCodecWorkspaceBackend(std::make_unique<WindowsCodecWorkspace>(std::move(p))));
#else
        (void)backend;(void)session;(void)root;return {};
#endif
    }
};
NativeCodecBackendStatus RetryDeferredNativeCodecCleanup() noexcept {
    std::unique_ptr<WindowsCodecWorkspace::Impl> batch;
    auto& list=DeferredCleanup();AcquireSRWLockExclusive(&list.mutex);
    batch=std::move(list.head);ReleaseSRWLockExclusive(&list.mutex);
    while(batch) {
        auto p=std::move(batch);batch=std::move(p->next_deferred);
        { WindowsCodecWorkspace retry(std::move(p)); } // retries all slots; failures return to owned list
    }
    return DeferredCount()?NativeCodecBackendStatus::CleanupFailure:NativeCodecBackendStatus::Ok;
}
} // namespace dokodemo::recovered
namespace dokodemo {
std::shared_ptr<recovered::NativeCodecWorkspaceBackend> CreateWindowsNativeCodecWorkspaceBackend(
    std::shared_ptr<WindowsStartupBackend> backend,std::shared_ptr<recovered::NativeCodecSessionState> session,
    const std::wstring& root,recovered::NativeCodecBackendStatus& status) {
    return recovered::NativeCodecWindowsFactory::Create(std::move(backend),std::move(session),root,status);
}
uint32_t PendingWindowsNativeCodecWorkspaceCleanup() noexcept { return recovered::DeferredCount(); }
recovered::NativeCodecBackendStatus RetryWindowsNativeCodecWorkspaceCleanup() noexcept { return recovered::RetryDeferredNativeCodecCleanup(); }
}
