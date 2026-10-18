#pragma once
#include "native_codec_workspace.h" // shared host session only; no codec operation
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace dokodemo::recovered {
inline constexpr const char* NativeSascoreAbi = "windows-owned-sas-lifecycle-v1";
struct SascoreImportLibrary {
    const char* name;
    uint16_t version, attributes;
    uint8_t words, variables;
    uint16_t first_function, function_count;
};
struct SascoreImportFunction { uint32_t nid, stub_va; };
struct SascoreExport { const char* library; bool variable; uint16_t attributes; uint32_t nid, source_va; };
struct SascoreModuleDescriptor {
    const char* name;
    uint16_t attributes, version;
    uint32_t encoded_bytes, plaintext_bytes;
    const char* encoded_sha256;
    const char* plaintext_sha256;
    std::array<SascoreImportLibrary, 4> imports;
    std::array<SascoreImportFunction, 10> functions;
    std::array<SascoreExport, 25> exports;
};
const SascoreModuleDescriptor* FindSascoreModuleDescriptor(std::string_view, uint32_t) noexcept;
bool SascoreImportPolicyAllows(const SascoreImportLibrary&) noexcept;
enum class NativeSascoreCode { Ok, Refused, Cancelled, DependencyBlocked, CleanupFailure };
struct NativeSascoreKey {
    std::array<uint64_t, 4> module{};
    std::array<uint64_t, 3> input{}, state{};
    bool operator==(const NativeSascoreKey& b) const noexcept {
        return module == b.module && input == b.input && state == b.state;
    }
};
struct NativeSascoreResources {
    NativeSascoreKey key;
    const char* policy_abi = NativeSascoreAbi;
    bool issued = false, retired = false, input_pinned = false;
    bool allocation_entered = false, allocation_returned = false;
    int32_t allocation_backend_code = 0;
    uint32_t allocation_error = 0;
    bool memory_live = false, memory_map_validated = false, calloc_source_returned = false;
    uint64_t allocation_owner = 0, allocation_generation = 0, allocation_serial = 0;
    uint64_t memory_host_address = 0, committed_bytes = 0;
    uint32_t requested_bytes = 0, source_coordinate = 0, page_bytes = 0, zeroed_bytes = 0;
    bool semaphore_create_entered = false, semaphore_create_returned = false;
    bool semaphore_source_returned = false, semaphore_live = false;
    int32_t semaphore_uid = 0;
    uint64_t semaphore_handle = 0;
    uint32_t semaphore_create_error = 0;
    uint32_t allocations = 0, frees = 0, semaphore_creates = 0, semaphore_closes = 0;
    uint32_t free_attempts = 0, free_returns = 0, delete_attempts = 0, delete_returns = 0;
    int32_t free_backend_code = 0;
    uint32_t free_error = 0, delete_error = 0;
    bool delete_native_succeeded = false;
    bool memory_freed = false, semaphore_closed = false;
    bool free_source_returned = false, delete_source_returned = false;
    int32_t delete_source_result = 0;
    bool retirement_entered = false, retirement_returned = false;
};
struct NativeSascoreOperations {
    virtual ~NativeSascoreOperations() = default;
    virtual bool Ready() const noexcept = 0;
    virtual NativeSascoreCode Allocate(const NativeSascoreKey&, std::shared_ptr<void>, NativeSascoreResources&) noexcept = 0;
    virtual NativeSascoreCode CreatePrivateSemaphore(const NativeSascoreKey&, NativeSascoreResources&) noexcept = 0;
    virtual NativeSascoreCode FreeMemory(const NativeSascoreKey&, uint32_t, bool, NativeSascoreResources&) noexcept = 0;
    virtual NativeSascoreCode DeleteSemaphore(const NativeSascoreKey&, int32_t, bool, NativeSascoreResources&) noexcept = 0;
    virtual NativeSascoreCode Retire(const NativeSascoreKey&, NativeSascoreResources&) noexcept = 0;
    virtual bool HasObligations() const noexcept = 0;
    virtual NativeSascoreCode Close() noexcept = 0;
};
// No claimed ABI string or caller-supplied callback can construct this facade.
// Inline dispatch keeps platform symbols out of the portable core link.
class NativeSascoreBackend final {
public:
    ~NativeSascoreBackend() = default;
    bool Ready() const noexcept { return impl_ && impl_->Ready(); }
    NativeSascoreCode Allocate(const NativeSascoreKey& k, std::shared_ptr<void> pin, NativeSascoreResources& s) noexcept {
        return impl_->Allocate(k, std::move(pin), s);
    }
    NativeSascoreCode CreatePrivateSemaphore(const NativeSascoreKey& k, NativeSascoreResources& s) noexcept { return impl_->CreatePrivateSemaphore(k, s); }
    NativeSascoreCode FreeMemory(const NativeSascoreKey& k, uint32_t word, bool source, NativeSascoreResources& s) noexcept { return impl_->FreeMemory(k, word, source, s); }
    NativeSascoreCode DeleteSemaphore(const NativeSascoreKey& k, int32_t uid, bool source, NativeSascoreResources& s) noexcept { return impl_->DeleteSemaphore(k, uid, source, s); }
    NativeSascoreCode Retire(const NativeSascoreKey& k, NativeSascoreResources& s) noexcept { return impl_->Retire(k, s); }
    bool HasObligations() const noexcept { return impl_ && impl_->HasObligations(); }
    NativeSascoreCode Close() noexcept { return impl_->Close(); }
private:
    std::unique_ptr<NativeSascoreOperations> impl_;
    explicit NativeSascoreBackend(std::unique_ptr<NativeSascoreOperations> p) noexcept : impl_(std::move(p)) {}
    friend struct NativeSascoreWindowsFactory;
};
struct SascoreWrite {
    uint32_t ordinal = 0, source_pc = 0, offset = 0, value = 0;
    const char* value_origin = "NativePlatform";
};
struct SascoreInitializationSnapshot {
    bool present = false, entry_entered = false, entry_returned = false, dependency_blocked = false;
    int32_t entry_result = 0;
    bool host_abort_required = false, host_abort_entered = false, host_aborted = false;
    bool stop_domain_ready = false, source_stop_entered = false, source_stop_returned = false;
    uint32_t stop_progress = 0;
    uint64_t owner = 0, generation = 0, serial = 0, native_state_address = 0;
    uint32_t source_segment = 1, source_segment_va = 0x1DD0, private_bytes = 20;
    uint32_t initial_source_known_bytes = 4, known_bytes = 0, writes_count = 0;
    std::array<uint8_t, 20> bytes{}, known{};
    std::array<SascoreWrite, 2> writes{};
    uint32_t frontier_source_pc = 0, frontier_stub_va = 0, frontier_nid = 0, frontier_import_index = 0;
    uint32_t calloc_count = 46644, calloc_size = 1;
    const char* semaphore_name = "SceSas";
    uint32_t semaphore_attributes = 0, semaphore_initial = 1, semaphore_maximum = 1;
    bool semaphore_options_null = true;
    uint32_t calloc_calls_entered = 0, calloc_calls_returned = 0;
    uint32_t create_calls_entered = 0, create_calls_returned = 0;
    uint32_t free_calls_entered = 0, free_calls_returned = 0, delete_calls_entered = 0, delete_calls_returned = 0;
    NativeSascoreResources resources;
};
void InitializeSascoreState(SascoreInitializationSnapshot&) noexcept;
NativeSascoreCode RunSascoreStart(SascoreInitializationSnapshot&, NativeSascoreBackend*, std::shared_ptr<void>) noexcept;
NativeSascoreCode RunSascoreStop(SascoreInitializationSnapshot&, NativeSascoreBackend*) noexcept;
NativeSascoreCode AbortSascoreInitialization(SascoreInitializationSnapshot&, NativeSascoreBackend*) noexcept;
} // namespace dokodemo::recovered
