#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace dokodemo::recovered {
inline constexpr const char* NativePreparedEnvelopeAbi = "windows-owned-atrac3plus-prepared-envelope-v1";
enum class NativeCodecBackendStatus { Ok, Unsupported, Refused, Cancelled, NativeFailure, AllocationFailed, CleanupFailure };
enum class NativeCodecWorkspaceState { Unissued, Preparing, Prepared, WorkspaceLive, Failed, DestroyedPendingFree, Retired };
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
};
struct NativePreparedEnvelopeSnapshot {
    bool present = false, native1001_blocked = false;
    bool source_init_returned = false, source_decode_returned = false;
    std::array<NativeCodecWorkspaceSnapshot,2> records{};
};
// Only the private final facade's trusted factory can install operations.
// Inline dispatch leaves the portable manager with no platform link symbols.
struct NativeCodecWorkspaceOperations {
    virtual ~NativeCodecWorkspaceOperations() = default;
    virtual bool Ready() const noexcept = 0;
    virtual NativeCodecBackendStatus Prepare(const NativeCodecWorkspaceKey&,uint8_t,uint8_t,
        std::shared_ptr<void>,std::shared_ptr<void>,NativeCodecWorkspaceSnapshot&) noexcept = 0;
    virtual NativeCodecBackendStatus Allocate(const NativeCodecWorkspaceKey&,uint32_t,NativeCodecWorkspaceSnapshot&) noexcept = 0;
    virtual NativeCodecBackendStatus Retire(const NativeCodecWorkspaceKey&,NativeCodecWorkspaceSnapshot&) noexcept = 0;
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
    NativeCodecBackendStatus Allocate(const NativeCodecWorkspaceKey& k,uint32_t n,NativeCodecWorkspaceSnapshot& out) noexcept { return impl_->Allocate(k,n,out); }
    NativeCodecBackendStatus Retire(const NativeCodecWorkspaceKey& k,NativeCodecWorkspaceSnapshot& out) noexcept { return impl_->Retire(k,out); }
    bool HasResources() const noexcept { return impl_ && impl_->HasResources(); }
    NativeCodecBackendStatus Close() noexcept { return impl_->Close(); }
private:
    std::unique_ptr<NativeCodecWorkspaceOperations> impl_;
    explicit NativeCodecWorkspaceBackend(std::unique_ptr<NativeCodecWorkspaceOperations> p) noexcept : impl_(std::move(p)) {}
    friend struct NativeCodecWindowsFactory;
};
} // namespace dokodemo::recovered
