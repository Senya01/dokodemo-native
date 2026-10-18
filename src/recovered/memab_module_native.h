#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace dokodemo::recovered {
inline constexpr const char* NativeMemabAbi = "windows-owned-memab-private-semaphore-v1";
struct MemabImportLibrary {
    const char* name;
    uint16_t version, attributes;
    uint8_t words, variables;
    uint16_t first_function, function_count;
};
struct MemabImportFunction { uint32_t nid, stub_va; };
struct MemabExport { const char* library; bool variable; uint16_t version, attributes; uint32_t nid, source_va; };
struct MemabModuleDescriptor {
    const char* name;
    uint16_t attributes, version;
    uint32_t encoded_bytes, plaintext_bytes;
    const char* encoded_sha256;
    const char* plaintext_sha256;
    std::array<MemabImportLibrary, 3> imports;
    std::array<MemabImportFunction, 6> functions;
    std::array<MemabExport, 14> exports;
    uint32_t source_elf_entry, start_source_va, stop_source_va;
    std::array<uint32_t, 16> start_words;
    std::array<uint32_t, 9> stop_words;
};
const MemabModuleDescriptor* FindMemabModuleDescriptor(std::string_view, uint32_t) noexcept;
bool MemabImportPolicyAllows(const MemabImportLibrary&) noexcept;
enum class NativeMemabCode { Ok, Refused, Cancelled, CleanupFailure, Exhausted };
struct NativeMemabKey {
    std::array<uint64_t, 4> module{};
    std::array<uint64_t, 3> input{}, state{};
    bool operator==(const NativeMemabKey& b) const noexcept {
        return module == b.module && input == b.input && state == b.state;
    }
};
struct NativeMemabResources {
    NativeMemabKey key;
    const char* policy_abi = NativeMemabAbi;
    uint64_t backend_owner = 0, backend_generation = 0, backend_serial = 0;
    bool issued = false, retired = false, input_pinned = false;
    bool create_entered = false, create_returned = false, create_source_returned = false;
    int32_t create_source_result = 0;
    uint32_t create_error = 0;
    int32_t semaphore_uid = 0;
    uint64_t semaphore_handle = 0;
    bool semaphore_live = false, semaphore_closed = false;
    uint32_t create_attempts = 0, create_returns = 0, semaphore_closes = 0;
    uint32_t delete_attempts = 0, delete_returns = 0, delete_error = 0;
    bool delete_native_succeeded = false, delete_source_returned = false;
    int32_t delete_source_result = 0;
    bool retirement_entered = false, retirement_returned = false;
    uint32_t host_retire_attempts = 0;
    uint32_t host_retire_error = 0;
};
struct NativeMemabOperations {
    virtual ~NativeMemabOperations() = default;
    virtual bool Ready() const noexcept = 0;
    virtual NativeMemabCode Create(const NativeMemabKey&, std::shared_ptr<void>, NativeMemabResources&) noexcept = 0;
    virtual NativeMemabCode Delete(const NativeMemabKey&, int32_t, bool, NativeMemabResources&) noexcept = 0;
    virtual NativeMemabCode Retire(const NativeMemabKey&, NativeMemabResources&) noexcept = 0;
    virtual bool HasObligations() const noexcept = 0;
    virtual NativeMemabCode Close() noexcept = 0;
};
// Only the trusted Windows factory constructs this narrow typed facade.
// It is not a generic ThreadMan provider or an arbitrary callback admission.
class NativeMemabBackend final {
public:
    bool Ready() const noexcept { return impl_ && impl_->Ready(); }
    NativeMemabCode Create(const NativeMemabKey& k, std::shared_ptr<void> pin, NativeMemabResources& s) noexcept {
        return impl_->Create(k, std::move(pin), s);
    }
    NativeMemabCode Delete(const NativeMemabKey& k, int32_t uid, bool source, NativeMemabResources& s) noexcept {
        return impl_->Delete(k, uid, source, s);
    }
    NativeMemabCode Retire(const NativeMemabKey& k, NativeMemabResources& s) noexcept { return impl_->Retire(k, s); }
    bool HasObligations() const noexcept { return impl_ && impl_->HasObligations(); }
    NativeMemabCode Close() noexcept { return impl_->Close(); }
private:
    std::unique_ptr<NativeMemabOperations> impl_;
    explicit NativeMemabBackend(std::unique_ptr<NativeMemabOperations> p) noexcept : impl_(std::move(p)) {}
    friend struct NativeMemabWindowsFactory;
};
struct MemabWrite { uint32_t ordinal = 0, source_pc = 0, offset = 0, value = 0; };
struct MemabInitializationSnapshot {
    bool present = false, entry_entered = false, entry_returned = false;
    int32_t entry_result = 0;
    bool host_abort_required = false, host_abort_entered = false, host_aborted = false;
    bool stop_domain_ready = false, source_stop_entered = false, source_stop_returned = false;
    uint64_t owner = 0, generation = 0, serial = 0, native_state_address = 0;
    uint32_t source_segment = 1, source_segment_va = 0x2870, private_bytes = 3152;
    uint32_t initial_source_known_bytes = 0, known_bytes = 0, writes_count = 0;
    std::array<uint8_t, 3152> bytes{}, known{};
    // Encoded R32 addends are metadata ONLY, not relocated known bytes.
    std::array<uint8_t, 8> encoded_file_literal{{0x90,0x01,0,0,0x10,0x03,0,0}};
    std::array<uint32_t, 2> file_pointer_addends{{0x190,0x310}};
    std::array<MemabWrite, 1> writes{};
    uint32_t frontier_source_pc = 0, frontier_stub_va = 0, frontier_nid = 0, frontier_import_index = 0;
    NativeMemabResources resources;
};
void InitializeMemabState(MemabInitializationSnapshot&) noexcept;
int32_t MemabEntryResultForCreate(int32_t) noexcept;
NativeMemabCode RunMemabStart(MemabInitializationSnapshot&, NativeMemabBackend*, std::shared_ptr<void>) noexcept;
NativeMemabCode RunMemabStop(MemabInitializationSnapshot&, NativeMemabBackend*) noexcept;
NativeMemabCode AbortMemabInitialization(MemabInitializationSnapshot&, NativeMemabBackend*) noexcept;
} // namespace dokodemo::recovered
