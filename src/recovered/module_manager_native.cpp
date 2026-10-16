#include "module_manager_native.h"
#include "../core_hash.h"
#include <atomic>
#include <limits>
#include <utility>

namespace dokodemo::recovered {
namespace {
// Host envelopes, NOT recovered service ABIs. No complete production ME/cache
// capability is implemented. Only isolated controls have a callable test ABI.
constexpr const char* ImportAbi = "unrecovered-psp32-audiocodec-import-v1";
constexpr const char* SyntheticAbi = "synthetic-stage16-u32x4-v1";
// This namespace is a Windows registry namespace, not PSP kernel UID state.
std::atomic<uint64_t> next_manager_owner{1}, next_module_uid{0x10000000};
uint64_t TakeMonotonic(std::atomic<uint64_t>& source, uint64_t limit) noexcept {
    auto value = source.load();
    while (value && value <= limit) {
        if (source.compare_exchange_weak(value, value + 1)) return value;
    }
    return 0; // no wrap, signed UID reuse or fallback UID
}
bool Same(NativeModuleToken a, NativeModuleToken b) noexcept {
    return a.owner == b.owner && a.generation == b.generation &&
        a.serial == b.serial && a.uid == b.uid && a.uid > 0;
}
bool Same(NativeModuleProviderToken a, NativeModuleProviderToken b) noexcept {
    return a.owner == b.owner && a.generation == b.generation && a.serial == b.serial;
}
bool SameInput(const NativeModuleInputView& a, const NativeModuleInputView& b) noexcept {
    return a.bytes == b.bytes && a.initialized == b.initialized && a.owner == b.owner &&
        a.generation == b.generation && a.serial == b.serial && a.host_address == b.host_address &&
        a.source_coordinate == b.source_coordinate && a.size == b.size;
}
bool KnownInput(const NativeModuleInputView& view) noexcept {
    if (!view.bytes || !view.initialized || !view.owner || !view.generation || !view.serial ||
        !view.host_address || view.size != 3232) return false;
    for (uint32_t i = 0; i < view.size; ++i) if (view.initialized[i] != 1) return false;
    return true;
}
NativeModuleResult Result(NativeModuleCode code, NativeModuleToken token = {}) noexcept {
    NativeModuleResult out; out.code = code; out.token = token; return out;
}
NativeModuleResult Returned(NativeModuleToken token, int32_t value) noexcept {
    auto out = Result(NativeModuleCode::Ok, token);
    out.source_return = true; out.source_result = value; return out;
}
struct ProviderRecord {
    NativeModuleProviderToken token;
    NativeModuleProvider definition;
    bool registered = true;
};
struct CallGate { bool executing = false; };
struct ModuleRecord {
    NativeModuleSnapshot data;
    core::Sha256Digest digest{};
    std::shared_ptr<NativeModuleInputLease> input;
    std::array<std::shared_ptr<ProviderRecord>, 8> bindings{};
};
bool InputLive(const ModuleRecord& record) noexcept {
    NativeModuleInputView now;
    if (!record.input || !record.input->View(now) || !KnownInput(now) || !SameInput(now, record.data.input)) return false;
    try { return core::sha256(now.bytes, now.size) == record.digest; }
    catch (...) { return false; }
}
void Export(ModuleRecord& record, const char* library, const char* kind, uint16_t attributes,
            uint32_t nid, uint32_t va, bool lifecycle) {
    NativeModuleExportSnapshot entry;
    entry.library = library; entry.kind = kind; entry.attributes = attributes;
    entry.nid = nid; entry.relative_va = va; entry.lifecycle_callable = lifecycle;
    record.data.exports.push_back(std::move(entry));
}
// Exact saved pairs AD8/AE0: JR RA; ADDU V0,ZERO,ZERO. These
// compiled bodies make no imported call, source-memory access or GP use.
int32_t AudiocodecStartAD8() noexcept { return 0; }
int32_t AudiocodecStopAE0() noexcept { return 0; }
} // namespace

struct NativeModuleManager::Impl {
    uint64_t owner = 0, generation = 1, serial = 0;
    NativeModuleDomain domain;
    std::shared_ptr<CallGate> gate = std::make_shared<CallGate>();
    std::shared_ptr<ModuleRecord> module;
    std::vector<std::shared_ptr<ProviderRecord>> providers;
    explicit Impl(NativeModuleDomain selected) : domain(selected) {
        owner = TakeMonotonic(next_manager_owner, (std::numeric_limits<uint64_t>::max)() - 1);
    }
    ~Impl() { for (const auto& provider : providers) provider->registered = false; }
    bool Valid(NativeModuleToken token) const noexcept {
        return module && Same(token, module->data.token) && token.owner == owner && token.generation == generation;
    }
    void Bind() noexcept {
        if (!module) return;
        for (size_t i = 0; i < module->data.imports.size(); ++i) {
            auto& entry = module->data.imports[i];
            auto& binding = module->bindings[i];
            const uint64_t previous = binding ? binding->token.serial : 0;
            binding.reset();
            for (const auto& provider : providers) {
                const auto& definition = provider->definition;
                if (definition.library == entry.library && definition.version == entry.version &&
                    definition.nid == entry.nid && definition.abi == entry.abi) {
                    binding = provider; break;
                }
            }
            entry.resolved = bool(binding);
            entry.provider_serial = binding ? binding->token.serial : 0;
            if (previous != entry.provider_serial) entry.binding_generation = serial;
        }
    }
};

struct NativeModuleCallBorrow::Impl {
    std::shared_ptr<ModuleRecord> module;
    std::shared_ptr<ProviderRecord> provider;
    std::shared_ptr<CallGate> gate;
    bool consumed = false;
    ~Impl() { if (module) --module->data.active_calls; }
};
NativeModuleCallBorrow::NativeModuleCallBorrow() noexcept = default;
NativeModuleCallBorrow::~NativeModuleCallBorrow() = default;
NativeModuleCallBorrow::NativeModuleCallBorrow(NativeModuleCallBorrow&&) noexcept = default;
NativeModuleCallBorrow& NativeModuleCallBorrow::operator=(NativeModuleCallBorrow&&) noexcept = default;
NativeModuleCallBorrow::operator bool() const noexcept { return impl_ && !impl_->consumed; }
void NativeModuleCallBorrow::Reset() noexcept { impl_.reset(); }
NativeModuleCallResult NativeModuleCallBorrow::Invoke(const std::array<uint32_t, 4>& args) {
    const auto entered = impl_; // retain even if callback resets/moves this public borrow
    if (!entered || entered->consumed) return {NativeModuleCode::InvalidToken, false, false, 0};
    entered->consumed = true; // one invocation, including a throwing provider
    if (!entered->provider->registered) return {NativeModuleCode::InvalidToken, false, false, 0};
    if (entered->gate->executing) return {NativeModuleCode::Busy, false, false, 0};
    if (!InputLive(*entered->module)) return {NativeModuleCode::InvalidInput, false, false, 0};
    // Unregistration revokes acquired-but-not-entered calls. An executing call
    // pins module/input/provider until exit. Reentrant manager mutation is Busy.
    entered->gate->executing = true;
    struct Exit { CallGate& gate; ~Exit() { gate.executing = false; } } call_guard{*entered->gate};
    auto result = entered->provider->definition.callable->Call(args);
    result.synthetic = entered->provider->definition.synthetic_only;
    if (result.code != NativeModuleCode::Ok) result.source_return = false;
    return result;
}

NativeModuleManager::NativeModuleManager(NativeModuleDomain domain) : impl_(std::make_unique<Impl>(domain)) {}
NativeModuleManager::~NativeModuleManager() = default;
uint64_t NativeModuleManager::Owner() const noexcept { return impl_->owner; }
uint64_t NativeModuleManager::Generation() const noexcept { return impl_->generation; }
NativeModuleResult NativeModuleManager::LoadAudiocodec(std::shared_ptr<NativeModuleInputLease> input) {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    if (self.module) return Result(NativeModuleCode::AlreadyLoaded);
    if (!self.owner || self.serial == (std::numeric_limits<uint64_t>::max)()) return Result(NativeModuleCode::Exhausted);
    NativeModuleInputView view;
    if (!input || !input->View(view) || !KnownInput(view)) return Result(NativeModuleCode::InvalidInput);
    core::Sha256 hash; hash.update(view.bytes, view.size); // same physical span; no payload copy
    const auto digest = core::hex_digest(hash.digest());
    const auto* descriptor = FindAudiocodecModuleDescriptor(digest, view.size);
    if (!descriptor) return Result(NativeModuleCode::IdentityMismatch);
    NativeModuleInputView confirmed;
    if (!input->View(confirmed) || !KnownInput(confirmed) || !SameInput(view, confirmed))
        return Result(NativeModuleCode::InvalidInput);
    // All allocations and metadata preparation precede UID publication. The
    // canonical descriptor binds this exact encoded identity to the accepted
    // offline plaintext/source contract, then to these compiled lifecycle bodies.
    auto record = std::make_shared<ModuleRecord>();
    record->digest = hash.digest();
    record->input = std::move(input);
    record->data.domain = self.domain;
    record->data.input = view;
    record->data.name = descriptor->name;
    record->data.encoded_sha256 = digest;
    record->data.plaintext_sha256 = descriptor->plaintext_sha256;
    record->data.encoded_bytes = descriptor->encoded_bytes;
    record->data.plaintext_bytes = descriptor->plaintext_bytes;
    record->data.implementation = "WindowsDeferredExact0009V1/aot-audiocodec-lifecycle-AD8-AE0-v1";
    record->data.input_live = true;
    record->data.imports.reserve(8); record->data.exports.reserve(12);
    for (const auto& library : descriptor->imports) {
        if (library.attributes != 0x0009 || library.version != 0 || library.entry_words != 5 ||
            library.variable_count != 0 || uint32_t(library.first_function) + library.function_count > 8)
            return Result(NativeModuleCode::InvalidInput);
        for (uint16_t i = 0; i < library.function_count; ++i) {
            const auto& function = descriptor->functions[library.first_function + i];
            NativeModuleImportSnapshot entry;
            entry.library = library.name;
            entry.abi = self.domain == NativeModuleDomain::SyntheticControls ? SyntheticAbi : ImportAbi;
            entry.version = library.version; entry.attributes = library.attributes;
            entry.nid = function.nid; entry.source_stub_va = function.source_stub_va;
            record->data.imports.push_back(std::move(entry));
        }
    }
    if (record->data.imports.size() != 8 || descriptor->module_start.source_va != 0xAD8 ||
        descriptor->module_stop.source_va != 0xAE0 ||
        descriptor->module_start.source_words != std::array<uint32_t, 2>{{0x03E00008, 0x00001021}} ||
        descriptor->module_stop.source_words != std::array<uint32_t, 2>{{0x03E00008, 0x00001021}})
        return Result(NativeModuleCode::InvalidInput);
    Export(*record, "", "function", 0x8000, 0xD632ACDB, 0xAD8, true);
    Export(*record, "", "function", 0x8000, 0xCEE8593C, 0xAE0, true);
    Export(*record, "", "variable", 0x8000, 0xF01D73A7, 0xBAC, false);
    constexpr std::array<std::array<uint32_t, 2>, 8> audio{{
        {{0x9D3F790C, 0}}, {{0x5B37EB1D, 0xD4}}, {{0x70A703F8, 0x25C}}, {{0x8ACA11D5, 0x3F4}},
        {{0x6CD2A861, 0x528}}, {{0x59176A0F, 0x5A8}}, {{0x3A20A200, 0x628}}, {{0x29681260, 0x6E0}}
    }};
    for (const auto& entry : audio) Export(*record, "sceAudiocodec", "function", 0x4001, entry[0], entry[1], false);
    Export(*record, "sceAudiocodec_driver", "function", 0x0001, 0x62518B7E, 0xAD8, false);
    const auto uid = TakeMonotonic(next_module_uid, uint64_t((std::numeric_limits<int32_t>::max)()));
    if (!uid) return Result(NativeModuleCode::Exhausted);
    record->data.token = {self.owner, self.generation, ++self.serial, static_cast<int32_t>(uid)};
    for (auto& entry : record->data.imports) entry.module = record->data.token;
    for (auto& entry : record->data.exports) entry.module = record->data.token;
    self.module = std::move(record); // noexcept ownership publication before returned UID
    self.Bind(); // attrs0009 Windows deferred-call policy; original firmware UNKNOWN
    return Returned(self.module->data.token, self.module->data.token.uid);
}
NativeModuleResult NativeModuleManager::Start(NativeModuleToken token, uint32_t bytes,
        const void* args, int32_t* status, const void* options) {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    if (!self.Valid(token)) return Result(NativeModuleCode::InvalidToken);
    if (bytes || args || status || options) return Result(NativeModuleCode::InvalidInput, token);
    auto& record = *self.module;
    if (record.data.state != NativeModuleState::Loaded) return Result(NativeModuleCode::WrongState, token);
    if (!InputLive(record)) return Result(NativeModuleCode::InvalidInput, token);
    const int32_t entry_result = AudiocodecStartAD8();
    record.data.state = NativeModuleState::Started;
    // Explicit Windows Start-service policy: the exact entry's zero indicates
    // resident success, exposed as service success0. No original firmware result
    // is inferred. The source wrapper accepts any nonnegative service result.
    auto result = Returned(token, entry_result == 0 ? 0 : -1);
    result.entry_return = true; result.entry_result = entry_result; return result;
}
NativeModuleResult NativeModuleManager::Stop(NativeModuleToken token) {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    if (!self.Valid(token)) return Result(NativeModuleCode::InvalidToken);
    auto& record = *self.module;
    if (record.data.active_calls) return Result(NativeModuleCode::Busy, token);
    if (record.data.state != NativeModuleState::Started) return Result(NativeModuleCode::WrongState, token);
    if (!InputLive(record)) return Result(NativeModuleCode::InvalidInput, token);
    const int32_t entry_result = AudiocodecStopAE0();
    record.data.state = NativeModuleState::Stopped;
    auto result = Returned(token, entry_result); // host policy, not a game Stop import
    result.entry_return = true; result.entry_result = entry_result; return result;
}
NativeModuleResult NativeModuleManager::Unload(NativeModuleToken token) {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    if (!self.Valid(token)) return Result(NativeModuleCode::InvalidToken);
    if (self.module->data.active_calls) return Result(NativeModuleCode::Busy, token);
    if (self.module->data.state == NativeModuleState::Started) return Result(NativeModuleCode::WrongState, token);
    self.module.reset(); // exports/imports/bindings removed, then last owned input lease
    return Result(NativeModuleCode::Ok, token);
}
NativeModuleResult NativeModuleManager::AdvanceGeneration() {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    if (self.module || !self.providers.empty()) return Result(NativeModuleCode::Busy);
    if (self.generation == (std::numeric_limits<uint64_t>::max)()) return Result(NativeModuleCode::Exhausted);
    ++self.generation; return Result(NativeModuleCode::Ok);
}
NativeModuleProviderResult NativeModuleManager::RegisterProvider(NativeModuleProvider definition) {
    auto& self = *impl_;
    if (self.gate->executing) return {NativeModuleCode::Busy, {}};
    if (definition.library.empty() || definition.abi.empty() || !definition.callable || !definition.nid)
        return {NativeModuleCode::InvalidProvider, {}};
    if (definition.synthetic_only && self.domain != NativeModuleDomain::SyntheticControls)
        return {NativeModuleCode::SyntheticProviderForbidden, {}};
    // A generic function pointer/string cannot establish a real six-ME/cache
    // service ABI. Production registration stays closed until an actual adapter
    // with separately recovered semantics is implemented and reviewed.
    if (self.domain != NativeModuleDomain::SyntheticControls || !definition.synthetic_only ||
        definition.abi != SyntheticAbi) return {NativeModuleCode::IncompatibleProvider, {}};
    for (const auto& provider : self.providers) {
        const auto& old = provider->definition;
        if (old.library == definition.library && old.version == definition.version && old.nid == definition.nid)
            return {NativeModuleCode::DuplicateProvider, {}};
    }
    if (!self.owner || self.serial == (std::numeric_limits<uint64_t>::max)()) return {NativeModuleCode::Exhausted, {}};
    auto record = std::make_shared<ProviderRecord>(); record->definition = std::move(definition);
    record->token = {self.owner, self.generation, self.serial + 1};
    self.providers.push_back(record); // may throw; no registry mutation on failure
    ++self.serial; self.Bind(); return {NativeModuleCode::Ok, record->token};
}
NativeModuleCode NativeModuleManager::UnregisterProvider(NativeModuleProviderToken token) {
    auto& self = *impl_;
    if (self.gate->executing) return NativeModuleCode::Busy;
    if (token.owner != self.owner || token.generation != self.generation || !token.serial) return NativeModuleCode::InvalidToken;
    for (auto at = self.providers.begin(); at != self.providers.end(); ++at) {
        if (!Same(token, (*at)->token)) continue;
        if (self.serial == (std::numeric_limits<uint64_t>::max)()) return NativeModuleCode::Exhausted;
        ++self.serial; // one monotonic binding mutation identity, never wrap
        (*at)->registered = false; // revoke unentered call tokens before edge removal
        self.providers.erase(at); self.Bind(); return NativeModuleCode::Ok;
    }
    return NativeModuleCode::InvalidToken;
}
NativeModuleCode NativeModuleManager::BorrowImport(NativeModuleToken token, uint32_t index,
                                                  NativeModuleCallBorrow& out) {
    auto& self = *impl_;
    if (self.gate->executing) return NativeModuleCode::Busy;
    if (out) return NativeModuleCode::Busy;
    if (!self.Valid(token)) return NativeModuleCode::InvalidToken;
    if (index >= self.module->bindings.size()) return NativeModuleCode::InvalidInput;
    if (self.module->data.state != NativeModuleState::Started) return NativeModuleCode::WrongState;
    if (!InputLive(*self.module)) return NativeModuleCode::InvalidInput;
    if (!self.module->bindings[index]) return NativeModuleCode::NotLinked;
    if (self.module->data.active_calls == (std::numeric_limits<uint32_t>::max)()) return NativeModuleCode::Exhausted;
    auto borrow = std::make_shared<NativeModuleCallBorrow::Impl>();
    borrow->module = self.module; borrow->provider = self.module->bindings[index]; borrow->gate = self.gate;
    ++self.module->data.active_calls;
    out.impl_ = std::move(borrow); return NativeModuleCode::Ok;
}
NativeModuleCallResult NativeModuleManager::CallExport(NativeModuleToken token, uint32_t index) {
    if (!impl_->Valid(token)) return {NativeModuleCode::InvalidToken, false, false, 0};
    if (index >= impl_->module->data.exports.size()) return {NativeModuleCode::InvalidInput, false, false, 0};
    // Lifecycle entries have separate typed Start/Stop APIs; alias equality never
    // installs a provider. Audio branches, including local zero branches, are not ported.
    if (impl_->module->data.exports[index].lifecycle_callable)
        return {NativeModuleCode::WrongState, false, false, 0};
    return {NativeModuleCode::UnimplementedExport, false, false, 0};
}
bool NativeModuleManager::Snapshot(NativeModuleToken token, NativeModuleSnapshot& out) const {
    if (!impl_->Valid(token)) return false;
    auto snapshot = impl_->module->data; snapshot.input_live = InputLive(*impl_->module);
    // Diagnostics carry address identity, never a dereferenceable borrowed span.
    // A returned snapshot can outlive registry mutation without exposing a UAF.
    snapshot.input.bytes = nullptr; snapshot.input.initialized = nullptr;
    out = std::move(snapshot); return true;
}
std::vector<NativeModuleProviderSnapshot> NativeModuleManager::Providers() const {
    std::vector<NativeModuleProviderSnapshot> out; out.reserve(impl_->providers.size());
    for (const auto& provider : impl_->providers) {
        const auto& definition = provider->definition;
        out.push_back({provider->token, definition.library, definition.abi, definition.version,
                       definition.nid, definition.synthetic_only});
    }
    return out;
}
const char* NativeModuleCodeName(NativeModuleCode code) noexcept {
    switch (code) {
    case NativeModuleCode::Ok: return "Ok";
    case NativeModuleCode::InvalidInput: return "InvalidInput";
    case NativeModuleCode::IdentityMismatch: return "IdentityMismatch";
    case NativeModuleCode::AlreadyLoaded: return "AlreadyLoaded";
    case NativeModuleCode::InvalidToken: return "InvalidToken";
    case NativeModuleCode::WrongState: return "WrongState";
    case NativeModuleCode::Exhausted: return "Exhausted";
    case NativeModuleCode::NotLinked: return "NotLinked";
    case NativeModuleCode::UnimplementedExport: return "UnimplementedExport";
    case NativeModuleCode::IncompatibleProvider: return "IncompatibleProvider";
    case NativeModuleCode::DuplicateProvider: return "DuplicateProvider";
    case NativeModuleCode::InvalidProvider: return "InvalidProvider";
    case NativeModuleCode::Busy: return "Busy";
    case NativeModuleCode::SyntheticProviderForbidden: return "SyntheticProviderForbidden";
    }
    return "Unknown";
}
const char* NativeModuleStateName(NativeModuleState state) noexcept {
    switch (state) {
    case NativeModuleState::Loaded: return "Loaded";
    case NativeModuleState::Started: return "Started";
    case NativeModuleState::Stopped: return "Stopped";
    }
    return "Unknown";
}
} // namespace dokodemo::recovered
