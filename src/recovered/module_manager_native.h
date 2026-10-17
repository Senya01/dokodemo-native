#pragma once
#include "audiocodec_module_native.h"
#include "atrac3plus_module_native.h"
#include "native_codec_workspace.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo::recovered {
// Windows adaptation, not a reconstruction of the unknown original firmware.
// Calls and mutations are serialized by the host. Borrowed calls retain both
// module input and callable owner; no callback may mutate the manager reentrantly.
enum class NativeModuleCode {
    Ok, InvalidInput, IdentityMismatch, AlreadyLoaded, InvalidToken, WrongState,
    Exhausted, NotLinked, UnimplementedExport, IncompatibleProvider,
    DuplicateProvider, InvalidProvider, Busy, SyntheticProviderForbidden,
    UnsupportedEntry, UnsupportedPolicy, InputOverlap, HostCapabilityRefused, DependencyBlocked
};
enum class NativeModuleState { Loaded, Started, Stopped, EntryBlocked, InitializationAborted };
enum class NativeModuleDomain { Production, SyntheticControls };
struct NativeModuleToken {
    uint64_t owner = 0, generation = 0, serial = 0;
    int32_t uid = 0;
};
struct NativeModuleInputView {
    const uint8_t* bytes = nullptr;
    const uint8_t* initialized = nullptr;
    uint64_t owner = 0, generation = 0, serial = 0, host_address = 0;
    uint32_t source_coordinate = 0, size = 0;
};
struct NativeModuleProviderToken { uint64_t owner = 0, generation = 0, serial = 0; };
// Values identify an exact manager-issued view, never a writable host pointer.
// Invocation compares every field with the live pinned consumer/private state.
struct NativeCheckNeedMemBufferCapability {
    NativeModuleToken consumer;
    uint64_t input_owner = 0, input_generation = 0, input_serial = 0;
    uint64_t state_owner = 0, state_generation = 0, state_serial = 0, state_address = 0;
    uint32_t record_offset = 0, bytes = 0, writes_before = 0, known_before = 0;
};
struct NativeCheckNeedMemSnapshot {
    bool present = false, partial_callable = false;
    NativeModuleToken provider_module, consumer_module;
    NativeModuleProviderToken provider;
    uint64_t provider_input_owner = 0, provider_input_generation = 0, provider_input_serial = 0;
    uint64_t consumer_input_owner = 0, consumer_input_generation = 0, consumer_input_serial = 0;
    uint64_t buffer_native_address = 0;
    uint32_t caller_source_pc = 0x1684, buffer_record_offset = 0x44, buffer_bytes = 0x68;
    uint32_t window_known_before = 0, window_known_after = 0;
    AudiocodecCheckNeedMemSnapshot body;
};
class NativeModuleInputLease {
public:
    virtual ~NativeModuleInputLease() = default;
    virtual bool View(NativeModuleInputView&) const noexcept = 0;
};
struct NativeModuleResult {
    NativeModuleCode code = NativeModuleCode::Ok;
    bool source_return = false;
    int32_t source_result = 0; // meaningful ONLY when source_return
    bool entry_return = false;
    int32_t entry_result = 0; // entry result and host service result are separate
    uint32_t entry_source_pc = 0, frontier_source_pc = 0, frontier_import_index = 0;
    NativeModuleToken dependency_module; // namespace for frontier_source_pc
    uint32_t frontier_nid = 0, frontier_stub_va = 0;
    bool check_need_mem_blocked = false;
    NativeModuleToken token;
    explicit operator bool() const { return code == NativeModuleCode::Ok; }
};
struct NativeModuleImportSnapshot {
    NativeModuleToken module;
    std::string library, abi;
    uint16_t version = 0, attributes = 0;
    uint32_t nid = 0, source_stub_va = 0;
    bool resolved = false, partial_callable = false;
    uint64_t provider_serial = 0;
    uint64_t provider_owner = 0, provider_generation = 0;
    NativeModuleToken provider_module;
    uint64_t binding_generation = 1;
};
struct NativeModuleExportSnapshot {
    NativeModuleToken module;
    std::string library, kind;
    uint16_t version = 0, attributes = 0;
    uint32_t nid = 0, relative_va = 0;
    bool lifecycle_callable = false, partial_callable = false;
};
struct NativeModuleSnapshot {
    NativeModuleToken token;
    NativeModuleState state = NativeModuleState::Loaded;
    NativeModuleDomain domain = NativeModuleDomain::Production;
    std::string name, encoded_sha256, plaintext_sha256, implementation;
    uint32_t encoded_bytes = 0, plaintext_bytes = 0;
    NativeModuleInputView input;
    bool input_live = false;
    uint32_t active_calls = 0;
    uint32_t owned_native_codec_resources = 0;
    uint32_t owned_providers = 0, incoming_bindings = 0;
    uint32_t start_source_va = 0, stop_source_va = 0;
    uint32_t entry_frontier_source_pc = 0;
    std::string policy, entry_frontier;
    bool partial_start_supported = false;
    Atrac3plusInitializationSnapshot initialization;
    NativeCheckNeedMemSnapshot check_need_mem;
    NativePreparedEnvelopeSnapshot prepared_envelope;
    std::vector<NativeModuleImportSnapshot> imports;
    std::vector<NativeModuleExportSnapshot> exports;
};
struct NativeModuleCallResult {
    NativeModuleCode code = NativeModuleCode::NotLinked;
    bool source_return = false, synthetic = false;
    int32_t source_result = 0;
};
class NativeModuleCallable {
public:
    virtual ~NativeModuleCallable() = default;
    virtual NativeModuleCallResult Call(const std::array<uint32_t, 4>&) = 0;
};
struct NativeModuleProvider {
    std::string library, abi;
    uint16_t version = 0;
    uint32_t nid = 0;
    bool synthetic_only = false;
    std::shared_ptr<NativeModuleCallable> callable;
    // Optional REAL registry owner in the isolated synthetic ABI. No export
    // metadata auto-registers a provider. A provider pins this owner when called.
    NativeModuleToken module;
};
struct NativeModuleProviderResult {
    NativeModuleCode code = NativeModuleCode::Ok;
    NativeModuleProviderToken token;
};
struct NativeModuleProviderSnapshot {
    NativeModuleProviderToken token;
    std::string library, abi;
    uint16_t version = 0;
    uint32_t nid = 0;
    bool synthetic_only = false;
    NativeModuleToken module;
    bool partial_callable = false;
    bool prepared_envelope_callable = false;
};
struct NativeCheckNeedMemCallResult {
    NativeModuleCode code = NativeModuleCode::HostCapabilityRefused;
    bool source_return = false;
    int32_t source_result = 0;
    NativeCheckNeedMemSnapshot snapshot;
};
class NativeCheckNeedMemBorrow final {
public:
    NativeCheckNeedMemBorrow() noexcept;
    ~NativeCheckNeedMemBorrow();
    NativeCheckNeedMemBorrow(NativeCheckNeedMemBorrow&&) noexcept;
    NativeCheckNeedMemBorrow& operator=(NativeCheckNeedMemBorrow&&) noexcept;
    NativeCheckNeedMemBorrow(const NativeCheckNeedMemBorrow&) = delete;
    NativeCheckNeedMemBorrow& operator=(const NativeCheckNeedMemBorrow&) = delete;
    NativeCheckNeedMemBufferCapability Buffer() const noexcept;
    NativeCheckNeedMemCallResult Invoke(NativeCheckNeedMemBufferCapability, uint32_t codec) noexcept;
    NativeCheckNeedMemCallResult InvokeSourceNull(uint32_t codec) noexcept;
    void Reset() noexcept;
    explicit operator bool() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    NativeCheckNeedMemCallResult InvokeImpl(const NativeCheckNeedMemBufferCapability*, uint32_t) noexcept;
    friend class NativeModuleManager;
};
class NativeModuleCallBorrow final {
public:
    NativeModuleCallBorrow() noexcept;
    ~NativeModuleCallBorrow();
    NativeModuleCallBorrow(NativeModuleCallBorrow&&) noexcept;
    NativeModuleCallBorrow& operator=(NativeModuleCallBorrow&&) noexcept;
    NativeModuleCallBorrow(const NativeModuleCallBorrow&) = delete;
    NativeModuleCallBorrow& operator=(const NativeModuleCallBorrow&) = delete;
    NativeModuleCallResult Invoke(const std::array<uint32_t, 4>&);
    void Reset() noexcept;
    explicit operator bool() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    friend class NativeModuleManager;
};
class NativeModuleManager final {
public:
    explicit NativeModuleManager(NativeModuleDomain domain = NativeModuleDomain::Production);
    ~NativeModuleManager();
    NativeModuleManager(const NativeModuleManager&) = delete;
    NativeModuleManager& operator=(const NativeModuleManager&) = delete;
    NativeModuleResult LoadAudiocodec(std::shared_ptr<NativeModuleInputLease>);
    NativeModuleResult Load(std::shared_ptr<NativeModuleInputLease>);
    NativeModuleResult Start(NativeModuleToken, uint32_t bytes, const void* args,
                             int32_t* status, const void* options);
    NativeModuleResult Stop(NativeModuleToken); // host policy calls exact AE0 body
    NativeModuleResult AbortInitialization(NativeModuleToken); // host-only partial init, no source Stop
    NativeModuleResult Unload(NativeModuleToken); // host policy; fails while started/borrowed
    NativeModuleResult AdvanceGeneration(); // only with no modules/providers
    NativeModuleProviderResult RegisterProvider(NativeModuleProvider);
    // Built-in exact partial body only, from a genuine Started audiocodec owner.
    NativeModuleProviderResult RegisterCheckNeedMemProvider(NativeModuleToken);
    // Separate production domain; never upgrades the Stage18 prefix provider.
    NativeModuleCode ConfigurePreparedEnvelopeBackend(std::shared_ptr<NativeCodecWorkspaceBackend>);
    NativeModuleProviderResult RegisterPreparedEnvelopeProvider(NativeModuleToken);
    NativeModuleCode UnregisterProvider(NativeModuleProviderToken);
    NativeModuleCode BorrowImport(NativeModuleToken, uint32_t index, NativeModuleCallBorrow&);
    NativeModuleCode BorrowCheckNeedMem(NativeModuleToken, uint32_t index, NativeCheckNeedMemBorrow&);
    NativeModuleCallResult CallExport(NativeModuleToken, uint32_t index);
    bool Snapshot(NativeModuleToken, NativeModuleSnapshot&) const;
    bool InitializationSnapshot(NativeModuleToken, Atrac3plusInitializationSnapshot&) const noexcept;
    bool CheckNeedMemSnapshot(NativeModuleToken, NativeCheckNeedMemSnapshot&) const noexcept;
    bool PreparedEnvelopeSnapshot(NativeModuleToken, NativePreparedEnvelopeSnapshot&) const noexcept;
    // Live records in publication order. All data/mask pointers are sanitized.
    std::vector<NativeModuleSnapshot> Snapshots() const;
    std::vector<NativeModuleProviderSnapshot> Providers() const;
    uint64_t Owner() const noexcept;
    uint64_t Generation() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
const char* NativeModuleCodeName(NativeModuleCode) noexcept;
const char* NativeModuleStateName(NativeModuleState) noexcept;
} // namespace dokodemo::recovered
