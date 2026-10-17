#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace dokodemo::recovered {
// Version2 adds codec1001 discovery/allocation without format/open and an
// exact owned-release capability. It is never a generic production ME ABI.
inline constexpr const char* NativePreparedEnvelopeAbi = "windows-owned-atrac-query-envelope-v2";
enum class NativeCodecBackendStatus { Ok, Unsupported, Refused, Cancelled, NativeFailure, AllocationFailed, CleanupFailure };
enum class NativeCodecWorkspaceState { Unissued, Preparing, Prepared, WorkspaceLive, Failed, DestroyedPendingFree, Retired, AllocatedFormatPending };
struct NativeCodecSessionState {
    std::atomic<bool> cancelled{false};
    std::atomic<uint64_t> next_coordinate{0x61000000};
};
struct NativeCodecWorkspaceKey {
    // Module tuples are owner/generation/serial/positive uid. Input/state tuples
    // are owner/generation/serial. No tuple is an address or caller authority.
    std::array<uint64_t,4> provider_module{}, consumer_module{};
    std::array<uint64_t,3> provider{}, provider_input{}, consumer_input{}, state{};
    uint32_t record_index = 0;
    bool operator==(const NativeCodecWorkspaceKey& b) const noexcept {
        return provider_module == b.provider_module && consumer_module == b.consumer_module &&
            provider == b.provider && provider_input == b.provider_input &&
            consumer_input == b.consumer_input && state == b.state && record_index == b.record_index;
    }
};
struct NativeCodecWorkspaceSnapshot {
    NativeCodecWorkspaceKey key;
    NativeCodecWorkspaceState state = NativeCodecWorkspaceState::Unissued;
    const char* policy_abi = NativePreparedEnvelopeAbi;
    bool original_equivalence_known = false;
    uint32_t record_index = 0, codec = 0, format_b1 = 0, format_b2 = 0, channels = 0, max_input_bytes = 0;
    uint32_t api_codec_version = 0, api_util_version = 0;
    bool native_decoder_find_entered = false, native_decoder_find_returned = false, native_decoder_found = false;
    uint32_t native_decoder_id = 0;
    // Format numbers are absent unless format_known. A library's initialized
    // defaults are not source producers. decoder_opened is historical;
    // decoder_ready is current ownership/readiness; no decode API exists here.
    bool context_allocated = false, format_known = false, format_pending = false;
    bool decoder_opened = false, decoder_ready = false, decode_capability = false;
    bool native_prepare_entered = false, native_prepare_returned = false, native_policy_return = false;
    bool native_alloc_entered = false, native_alloc_returned = false, native_open_entered = false, native_open_returned = false;
    int32_t native_api_result = 0, native_error_class = 0, native_policy_result = 0;
    uint32_t envelope_bytes = 0, envelope_alignment = 0;
    bool allocation_attempted = false;
    bool backend_allocate_entered = false, backend_allocate_returned = false;
    int32_t backend_allocate_code = 0;
    bool provisional_live = false, allocation_returned = false, allocation_succeeded = false;
    uint64_t allocation_owner = 0, allocation_generation = 0, allocation_serial = 0;
    uint32_t requested_bytes = 0, raw_coordinate = 0, aligned_coordinate = 0;
    uint64_t committed_bytes = 0, host_address = 0;
    bool placement_constructed = false, context_transferred = false, context_destroyed = false, storage_freed = false;
    uint32_t context_allocations = 0, context_frees = 0, win32_error = 0;
    bool query_source_return = false, get_edram_source_return = false;
    int32_t query_source_result = 0, get_edram_source_result = 0;
    bool release_source_entered = false, release_source_return = false;
    int32_t release_source_result = 0;
    bool original_low29_guard_passed = false, windows_release_capability_applied = false, release_map_validated = false;
    uint32_t release_native_attempts = 0, release_native_returns = 0;
    int32_t release_native_code = 0;
    bool release_native_completed = false;
    bool host_retirement_entered = false, host_retirement_returned = false;
};
struct NativePreparedEnvelopeSnapshot {
    bool present = false, native1001_blocked = false;
    // Codec Init/decode, distinct from ATRAC module Start/Stop below.
    bool source_init_returned = false, source_decode_returned = false;
    bool source_start_returned = false, source_stop_entered = false, source_stop_returned = false;
    bool source_stop_pending = false;
    uint32_t source_stop_next_record = 0, source_stop_records_completed = 0;
    uint32_t source_release_calls_entered = 0, source_release_calls_returned = 0;
    std::array<NativeCodecWorkspaceSnapshot,4> records{};
};
// Only the private final facade's trusted factory can install operations.
// Inline dispatch leaves the portable manager with no platform link symbols.
struct NativeCodecWorkspaceOperations {
    virtual ~NativeCodecWorkspaceOperations() = default;
    virtual bool Ready() const noexcept = 0;
    virtual NativeCodecBackendStatus Prepare(const NativeCodecWorkspaceKey&,uint8_t,uint8_t,
        std::shared_ptr<void>,std::shared_ptr<void>,NativeCodecWorkspaceSnapshot&) noexcept = 0;
    virtual NativeCodecBackendStatus PrepareUnconfigured(const NativeCodecWorkspaceKey&,
        std::shared_ptr<void>,std::shared_ptr<void>,NativeCodecWorkspaceSnapshot&) noexcept = 0;
    virtual NativeCodecBackendStatus Allocate(const NativeCodecWorkspaceKey&,uint32_t,NativeCodecWorkspaceSnapshot&) noexcept = 0;
    virtual NativeCodecBackendStatus Retire(const NativeCodecWorkspaceKey&,NativeCodecWorkspaceSnapshot&) noexcept = 0;
    virtual NativeCodecBackendStatus Release(const NativeCodecWorkspaceKey&,uint32_t,uint32_t,uint32_t,
        NativeCodecWorkspaceSnapshot&) noexcept = 0;
    virtual bool HasResources() const noexcept = 0;
    virtual NativeCodecBackendStatus Close() noexcept = 0;
};
// A caller-supplied operations object or claimed ABI string cannot construct
// this sealed facade. No Windows/FFmpeg types leak into the portable core.
class NativeCodecWorkspaceBackend final {
public:
    ~NativeCodecWorkspaceBackend() = default;
    bool Ready() const noexcept { return impl_ && impl_->Ready(); }
    NativeCodecBackendStatus Prepare(const NativeCodecWorkspaceKey& k,uint8_t a,uint8_t b,
        std::shared_ptr<void> p,std::shared_ptr<void> c,NativeCodecWorkspaceSnapshot& out) noexcept {
        return impl_->Prepare(k,a,b,std::move(p),std::move(c),out);
    }
    NativeCodecBackendStatus PrepareUnconfigured(const NativeCodecWorkspaceKey& k,
        std::shared_ptr<void> p,std::shared_ptr<void> c,NativeCodecWorkspaceSnapshot& out) noexcept {
        return impl_->PrepareUnconfigured(k,std::move(p),std::move(c),out);
    }
    NativeCodecBackendStatus Allocate(const NativeCodecWorkspaceKey& k,uint32_t n,NativeCodecWorkspaceSnapshot& out) noexcept { return impl_->Allocate(k,n,out); }
    NativeCodecBackendStatus Retire(const NativeCodecWorkspaceKey& k,NativeCodecWorkspaceSnapshot& out) noexcept { return impl_->Retire(k,out); }
    NativeCodecBackendStatus Release(const NativeCodecWorkspaceKey& k,uint32_t aligned,uint32_t need,uint32_t raw,
        NativeCodecWorkspaceSnapshot& out) noexcept { return impl_->Release(k,aligned,need,raw,out); }
    bool HasResources() const noexcept { return impl_ && impl_->HasResources(); }
    NativeCodecBackendStatus Close() noexcept { return impl_->Close(); }
private:
    std::unique_ptr<NativeCodecWorkspaceOperations> impl_;
    explicit NativeCodecWorkspaceBackend(std::unique_ptr<NativeCodecWorkspaceOperations> p) noexcept : impl_(std::move(p)) {}
    friend struct NativeCodecWindowsFactory;
};
} // namespace dokodemo::recovered
