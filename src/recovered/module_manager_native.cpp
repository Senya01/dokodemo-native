#include "module_manager_native.h"
#include "../core_hash.h"
#include <atomic>
#include <limits>
#include <utility>

namespace dokodemo::recovered {
namespace {
// Host envelopes, NOT recovered ME service ABIs. One dedicated source-informed
// partial CheckNeedMem ABI is separate from the isolated synthetic test ABI.
constexpr const char* ImportAbi = "unrecovered-psp32-audiocodec-import-v1";
constexpr const char* AtracImportAbi = "unrecovered-psp32-atrac3plus-import-v1";
constexpr const char* SyntheticAbi = "synthetic-stage16-u32x4-v1";
constexpr const char* CheckNeedMemAbi = "windows-owned-masked-checkneedmem-prefix-v1";
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
        !view.host_address || (view.size != 3232 && view.size != 19524) ||
        view.host_address != uint64_t(reinterpret_cast<uintptr_t>(view.bytes)) ||
        view.host_address > (std::numeric_limits<uint64_t>::max)() - view.size ||
        uint64_t(view.source_coordinate) + view.size > (uint64_t(1) << 32)) return false;
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
    bool check_need_mem = false;
    std::shared_ptr<struct ModuleRecord> module;
};
struct CallGate { bool executing = false; };
struct ModuleRecord {
    NativeModuleSnapshot data;
    core::Sha256Digest digest{};
    std::shared_ptr<NativeModuleInputLease> input;
    std::vector<std::shared_ptr<ProviderRecord>> bindings;
    int32_t (*start)() noexcept = nullptr;
    int32_t (*stop)() noexcept = nullptr;
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
    std::vector<std::shared_ptr<ModuleRecord>> modules;
    std::vector<std::shared_ptr<ProviderRecord>> providers;
    explicit Impl(NativeModuleDomain selected) : domain(selected) {
        owner = TakeMonotonic(next_manager_owner, (std::numeric_limits<uint64_t>::max)() - 1);
    }
    ~Impl() {
        for (const auto& provider : providers) provider->registered = false;
        // Detach the graph before containers retire. Borrow pins remain valid.
        for (const auto& module : modules) module->bindings.clear();
    }
    std::shared_ptr<ModuleRecord> Find(NativeModuleToken token) const noexcept {
        if (token.owner != owner || token.generation != generation) return {};
        for (const auto& module : modules) if (Same(token, module->data.token)) return module;
        return {};
    }
    void Bind() noexcept {
        for (const auto& module : modules) {
            module->data.owned_providers = 0; module->data.incoming_bindings = 0;
        }
        for (const auto& provider : providers) if (provider->module) ++provider->module->data.owned_providers;
        for (const auto& module : modules) {
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
            entry.partial_callable = binding && binding->check_need_mem;
            entry.resolved = binding && !entry.partial_callable;
            entry.provider_serial = binding ? binding->token.serial : 0;
            entry.provider_owner = binding ? binding->token.owner : 0;
            entry.provider_generation = binding ? binding->token.generation : 0;
            entry.provider_module = binding ? binding->definition.module : NativeModuleToken{};
            if (previous != entry.provider_serial) entry.binding_generation = serial;
            if (binding && binding->module) ++binding->module->data.incoming_bindings;
        }
        }
    }
};

struct NativeModuleCallBorrow::Impl {
    std::shared_ptr<ModuleRecord> module;
    std::shared_ptr<ProviderRecord> provider;
    std::shared_ptr<CallGate> gate;
    bool consumed = false;
    ~Impl() {
        if (module) --module->data.active_calls;
        if (provider && provider->module && provider->module != module) --provider->module->data.active_calls;
    }
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
    if (entered->provider->module && !InputLive(*entered->provider->module))
        return {NativeModuleCode::InvalidInput, false, false, 0};
    // Unregistration revokes acquired-but-not-entered calls. An executing call
    // pins module/input/provider until exit. Reentrant manager mutation is Busy.
    entered->gate->executing = true;
    struct Exit { CallGate& gate; ~Exit() { gate.executing = false; } } call_guard{*entered->gate};
    auto result = entered->provider->definition.callable->Call(args);
    result.synthetic = entered->provider->definition.synthetic_only;
    if (result.code != NativeModuleCode::Ok) result.source_return = false;
    return result;
}

struct NativeCheckNeedMemBorrow::Impl {
    std::shared_ptr<ModuleRecord> consumer;
    std::shared_ptr<ProviderRecord> provider;
    std::shared_ptr<CallGate> gate;
    NativeCheckNeedMemBufferCapability capability;
    bool consumed = false;
    ~Impl() {
        if (consumer) --consumer->data.active_calls;
        if (provider && provider->module && provider->module != consumer) --provider->module->data.active_calls;
    }
};
NativeCheckNeedMemBorrow::NativeCheckNeedMemBorrow() noexcept = default;
NativeCheckNeedMemBorrow::~NativeCheckNeedMemBorrow() = default;
NativeCheckNeedMemBorrow::NativeCheckNeedMemBorrow(NativeCheckNeedMemBorrow&&) noexcept = default;
NativeCheckNeedMemBorrow& NativeCheckNeedMemBorrow::operator=(NativeCheckNeedMemBorrow&&) noexcept = default;
NativeCheckNeedMemBorrow::operator bool() const noexcept { return impl_ && !impl_->consumed; }
void NativeCheckNeedMemBorrow::Reset() noexcept { impl_.reset(); }
NativeCheckNeedMemBufferCapability NativeCheckNeedMemBorrow::Buffer() const noexcept {
    return impl_ ? impl_->capability : NativeCheckNeedMemBufferCapability{};
}
NativeCheckNeedMemCallResult NativeCheckNeedMemBorrow::Invoke(NativeCheckNeedMemBufferCapability buffer,
                                                             uint32_t codec) noexcept {
    return InvokeImpl(&buffer, codec);
}
NativeCheckNeedMemCallResult NativeCheckNeedMemBorrow::InvokeSourceNull(uint32_t codec) noexcept {
    return InvokeImpl(nullptr, codec); // explicit source NULL, never a bad host capability
}
NativeCheckNeedMemCallResult NativeCheckNeedMemBorrow::InvokeImpl(
    const NativeCheckNeedMemBufferCapability* buffer, uint32_t codec) noexcept {
    NativeCheckNeedMemCallResult out;
    const auto entered = impl_; // pins survive reset/move of the public borrow in a lease's View
    if (!entered || entered->consumed) { out.code = NativeModuleCode::InvalidToken; return out; }
    entered->consumed = true;
    const auto provider = entered->provider;
    const auto consumer = entered->consumer;
    if (!provider->registered) { out.code = NativeModuleCode::InvalidToken; return out; }
    if (entered->gate->executing) { out.code = NativeModuleCode::Busy; return out; }
    entered->gate->executing = true;
    struct Exit { CallGate& gate; ~Exit() { gate.executing = false; } } guard{*entered->gate};
    const auto& expected = entered->capability;
    if (buffer && (!Same(buffer->consumer, expected.consumer) ||
        buffer->input_owner != expected.input_owner || buffer->input_generation != expected.input_generation ||
        buffer->input_serial != expected.input_serial || buffer->state_owner != expected.state_owner ||
        buffer->state_generation != expected.state_generation || buffer->state_serial != expected.state_serial ||
        buffer->state_address != expected.state_address || buffer->record_offset != expected.record_offset ||
        buffer->bytes != expected.bytes || buffer->writes_before != expected.writes_before ||
        buffer->known_before != expected.known_before)) return out;
    if (!provider->check_need_mem || !provider->module ||
        provider->module->data.state != NativeModuleState::Started ||
        consumer->data.state != NativeModuleState::EntryBlocked ||
        !InputLive(*provider->module) || !InputLive(*consumer) || !provider->registered ||
        consumer->data.initialization.external_services_dispatched ||
        !Atrac3plusCheckNeedMemStateExact(consumer->data.initialization, false)) return out;
    auto& state = consumer->data.initialization;
    if (state.owner != expected.state_owner || state.generation != expected.state_generation ||
        state.serial != expected.state_serial ||
        uint64_t(reinterpret_cast<uintptr_t>(&state)) != expected.state_address ||
        expected.record_offset != 0x44 || expected.bytes != 0x68 ||
        expected.record_offset > state.record_stride || expected.bytes > state.record_stride - expected.record_offset)
        return out;
    auto& snapshot = out.snapshot;
    snapshot.present = true; snapshot.partial_callable = true;
    snapshot.provider_module = provider->module->data.token; snapshot.consumer_module = consumer->data.token;
    snapshot.provider = provider->token;
    const auto& provider_input = provider->module->data.input;
    const auto& consumer_input = consumer->data.input;
    snapshot.provider_input_owner = provider_input.owner; snapshot.provider_input_generation = provider_input.generation;
    snapshot.provider_input_serial = provider_input.serial;
    snapshot.consumer_input_owner = consumer_input.owner; snapshot.consumer_input_generation = consumer_input.generation;
    snapshot.consumer_input_serial = consumer_input.serial;
    AudiocodecCheckNeedMemBuffer admitted; // default is explicit source NULL
    if (buffer) {
        admitted.bytes_ = state.bytes.data() + expected.record_offset;
        admitted.known_ = state.known.data() + expected.record_offset;
        admitted.size_ = expected.bytes;
        snapshot.buffer_native_address = uint64_t(reinterpret_cast<uintptr_t>(admitted.bytes_));
        for (uint32_t i = 0; i < expected.bytes; ++i) snapshot.window_known_before += admitted.known_[i] == 1;
    }
    const auto result = RunAudiocodecCheckNeedMemPrefix(admitted, codec, snapshot.body);
    if (snapshot.body.entered) state.external_services_dispatched = 1; // actual CheckNeedMem, NEVER ME
    snapshot.window_known_after = snapshot.window_known_before;
    if (snapshot.body.writes) {
        // The body committed exactly this write through the SAME owned span.
        // No fallible work can intervene before journal/mask-count publication.
        state.writes[4] = {4,0x80,0x44,4,0x04090701,Atrac3plusWriteSource::Audiocodec};
        state.writes_count = 5; state.known_bytes += 4;
        snapshot.window_known_after += 4;
    }
    out.source_return = snapshot.body.source_return; out.source_result = snapshot.body.source_result;
    out.code = result == AudiocodecCheckNeedMemCode::DependencyBlocked ? NativeModuleCode::DependencyBlocked :
        result == AudiocodecCheckNeedMemCode::Returned ? NativeModuleCode::Ok : NativeModuleCode::HostCapabilityRefused;
    consumer->data.check_need_mem = snapshot; // fixed, nonallocating; caller PC stays ATRAC1684
    return out;
}

NativeModuleManager::NativeModuleManager(NativeModuleDomain domain) : impl_(std::make_unique<Impl>(domain)) {}
NativeModuleManager::~NativeModuleManager() = default;
uint64_t NativeModuleManager::Owner() const noexcept { return impl_->owner; }
uint64_t NativeModuleManager::Generation() const noexcept { return impl_->generation; }
NativeModuleResult NativeModuleManager::LoadAudiocodec(std::shared_ptr<NativeModuleInputLease> input) {
    if (impl_->gate->executing) return Result(NativeModuleCode::Busy);
    NativeModuleInputView view;
    if (!input || !input->View(view) || view.size != 3232) return Result(NativeModuleCode::InvalidInput);
    return Load(std::move(input));
}
NativeModuleResult NativeModuleManager::Load(std::shared_ptr<NativeModuleInputLease> input) {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    if (!self.owner || self.serial == (std::numeric_limits<uint64_t>::max)()) return Result(NativeModuleCode::Exhausted);
    NativeModuleInputView view;
    if (!input || !input->View(view) || !KnownInput(view)) return Result(NativeModuleCode::InvalidInput);
    core::Sha256 hash; hash.update(view.bytes, view.size); // same physical span; no payload copy
    const auto digest = core::hex_digest(hash.digest());
    const auto* descriptor = FindAudiocodecModuleDescriptor(digest, view.size);
    const auto* atrac = FindAtrac3plusModuleDescriptor(digest, view.size);
    if (!descriptor && !atrac) return Result(NativeModuleCode::IdentityMismatch);
    const char* name = descriptor ? descriptor->name : atrac->name;
    for (const auto& live : self.modules) {
        if (live->data.name == name) return Result(NativeModuleCode::AlreadyLoaded);
        const auto& old = live->data.input;
        if ((old.owner == view.owner && old.generation == view.generation && old.serial == view.serial) ||
            (view.host_address < old.host_address + old.size && old.host_address < view.host_address + view.size) ||
            (uint64_t(view.source_coordinate) < uint64_t(old.source_coordinate) + old.size &&
             uint64_t(old.source_coordinate) < uint64_t(view.source_coordinate) + view.size))
            return Result(NativeModuleCode::InputOverlap);
    }
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
    record->data.name = name;
    record->data.encoded_sha256 = digest;
    record->data.input_live = true;
    if (descriptor) {
    record->data.plaintext_sha256 = descriptor->plaintext_sha256;
    record->data.encoded_bytes = descriptor->encoded_bytes;
    record->data.plaintext_bytes = descriptor->plaintext_bytes;
    record->data.implementation = "WindowsDeferredExact0009V1/aot-lifecycle-AD8-AE0/checkneedmem-to84-v1";
    record->data.policy = "WindowsDeferredExact0009V1";
    record->data.start_source_va = 0xAD8; record->data.stop_source_va = 0xAE0;
    record->start = AudiocodecStartAD8; record->stop = AudiocodecStopAE0;
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
    } else {
        // This exact unencoded ELF has its OWN Windows metadata/partial-init
        // policy. Exact0009V1 has not been broadened to0001/4001.
        if (atrac->attributes != 0x0006 || atrac->version != 0x0101 ||
            atrac->start_source_va != 0x11A0 || atrac->stop_source_va != 0x121C)
            return Result(NativeModuleCode::UnsupportedPolicy);
        record->data.plaintext_sha256 = atrac->input_sha256; // input already ELF; no transformation
        record->data.encoded_bytes = atrac->input_bytes; record->data.plaintext_bytes = atrac->input_bytes;
        record->data.policy = "WindowsAtrac3plusExact0001_4001PartialInitV1";
        record->data.implementation = "aot-atrac3plus-11A0-first-record/typed-checkneedmem-to-ME84-v1";
        record->data.start_source_va = atrac->start_source_va; record->data.stop_source_va = atrac->stop_source_va;
        record->data.partial_start_supported = true; record->data.initialization.present = true;
        record->data.initialization.native_state_address = uint64_t(reinterpret_cast<uintptr_t>(&record->data.initialization));
        record->data.entry_frontier = "ATRAC1684/stub3640 typed partial CheckNeedMem; if bound, audiocodec84/stubAF8 ME C00D873B";
        record->data.imports.reserve(7); record->data.exports.reserve(28);
        for (const auto& library : atrac->imports) {
            if (!Atrac3plusImportPolicyAllows(library.name, library.version, library.attributes,
                    library.entry_words, library.variable_count, library.function_count) ||
                uint32_t(library.first_function) + library.function_count > atrac->functions.size())
                return Result(NativeModuleCode::UnsupportedPolicy);
            for (uint16_t i = 0; i < library.function_count; ++i) {
                const auto& function = atrac->functions[library.first_function + i];
                NativeModuleImportSnapshot entry;
                entry.library = library.name; entry.version = library.version; entry.attributes = library.attributes;
                entry.abi = self.domain == NativeModuleDomain::SyntheticControls ? SyntheticAbi :
                    function.nid == 0x9D3F790C ? CheckNeedMemAbi : AtracImportAbi;
                entry.nid = function.nid; entry.source_stub_va = function.source_stub_va;
                record->data.imports.push_back(std::move(entry));
            }
        }
        if (record->data.imports.size() != 7) return Result(NativeModuleCode::UnsupportedPolicy);
        for (const auto& entry : atrac->exports)
            Export(*record, entry.library, entry.variable ? "variable" : "function", entry.attributes,
                   entry.nid, entry.source_va, false); // NO fully callable new export; aliases metadata-only
    }
    record->bindings.resize(record->data.imports.size());
    self.modules.reserve(self.modules.size() + 1); // final fallible registry allocation BEFORE UID publication
    const auto uid = TakeMonotonic(next_module_uid, uint64_t((std::numeric_limits<int32_t>::max)()));
    if (!uid) return Result(NativeModuleCode::Exhausted);
    record->data.token = {self.owner, self.generation, ++self.serial, static_cast<int32_t>(uid)};
    if (atrac) {
        auto& initialization = record->data.initialization;
        initialization.owner = self.owner; initialization.generation = self.generation; initialization.serial = self.serial;
    }
    for (auto& entry : record->data.imports) entry.module = record->data.token;
    for (auto& entry : record->data.exports) entry.module = record->data.token;
    if (descriptor && self.domain == NativeModuleDomain::Production)
        for (auto& entry : record->data.exports)
            if (entry.library == "sceAudiocodec" && entry.nid == 0x9D3F790C) entry.partial_callable = true;
    const auto token = record->data.token;
    self.modules.push_back(std::move(record)); // capacity reserved; noexcept ownership publication
    self.Bind(); // exact per-module policies; original firmware UNKNOWN
    return Returned(token, token.uid);
}
NativeModuleResult NativeModuleManager::Start(NativeModuleToken token, uint32_t bytes,
        const void* args, int32_t* status, const void* options) {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    const auto owned = self.Find(token);
    if (!owned) return Result(NativeModuleCode::InvalidToken);
    if (bytes || args || status || options) return Result(NativeModuleCode::InvalidInput, token);
    auto& record = *owned;
    if (record.data.state != NativeModuleState::Loaded) return Result(NativeModuleCode::WrongState, token);
    self.gate->executing = true; // admission begins BEFORE virtual consumer View/hash validation
    struct StartExit { CallGate& gate; ~StartExit() { gate.executing = false; } } admission{*self.gate};
    if (!InputLive(record)) return Result(NativeModuleCode::InvalidInput, token);
    if (record.data.partial_start_supported) {
        NativeCheckNeedMemBorrow borrow;
        const auto provider = self.domain == NativeModuleDomain::Production ? record.bindings[2] : nullptr;
        if (provider) {
            if (!provider->registered || !provider->check_need_mem || !provider->module ||
                provider->module->data.state != NativeModuleState::Started || !InputLive(*provider->module))
                return Result(NativeModuleCode::HostCapabilityRefused, token);
            if (record.data.active_calls == (std::numeric_limits<uint32_t>::max)() ||
                provider->module->data.active_calls == (std::numeric_limits<uint32_t>::max)())
                return Result(NativeModuleCode::Exhausted, token);
            // The only connected-call allocation precedes EVERY private effect.
            auto prepared = std::make_shared<NativeCheckNeedMemBorrow::Impl>();
            prepared->consumer = owned; prepared->provider = provider; prepared->gate = self.gate;
            ++record.data.active_calls; ++provider->module->data.active_calls;
            borrow.impl_ = std::move(prepared);
        }
        if (!RunAtrac3plusStartPrefix(record.data.initialization)) return Result(NativeModuleCode::InvalidInput, token);
        record.data.entry_frontier_source_pc = record.data.initialization.frontier_source_pc;
        record.data.state = NativeModuleState::EntryBlocked;
        auto out = Result(NativeModuleCode::UnsupportedEntry, token);
        out.frontier_source_pc = record.data.initialization.frontier_source_pc;
        out.frontier_import_index = record.data.initialization.frontier_import_index;
        out.frontier_nid = 0x9D3F790C; out.frontier_stub_va = 0x3640; out.dependency_module = token;
        if (borrow) {
            const auto& state = record.data.initialization;
            const auto& input = record.data.input;
            borrow.impl_->capability = {token,input.owner,input.generation,input.serial,state.owner,state.generation,state.serial,
                uint64_t(reinterpret_cast<uintptr_t>(&state)),0x44,0x68,state.writes_count,state.known_bytes};
            self.gate->executing = false; // synchronous handoff to Invoke's no-throw admission gate
            const auto called = borrow.Invoke(borrow.Buffer(), record.data.initialization.arg1);
            if (called.code == NativeModuleCode::DependencyBlocked) {
                out.dependency_module = called.snapshot.provider_module;
                out.frontier_source_pc = called.snapshot.body.frontier_source_pc;
                out.frontier_nid = called.snapshot.body.frontier_nid;
                out.frontier_stub_va = called.snapshot.body.frontier_stub_va;
                out.frontier_import_index = 0; // ME import in dependency_module; caller import stays initialization.index2
                out.check_need_mem_blocked = true;
            } else {
                // No initializer continuation is implemented for guard returns.
                // The exact connected arguments cannot take a guard branch.
                out.code = called.code == NativeModuleCode::Ok ? NativeModuleCode::UnsupportedEntry : called.code;
            }
        }
        return out; // NO entry/body/Start-service return
    }
    if (!record.start) return Result(NativeModuleCode::UnsupportedEntry, token);
    const int32_t entry_result = record.start();
    record.data.state = NativeModuleState::Started;
    // Explicit Windows Start-service policy: the exact entry's zero indicates
    // resident success, exposed as service success0. No original firmware result
    // is inferred. The source wrapper accepts any nonnegative service result.
    auto result = Returned(token, entry_result == 0 ? 0 : -1);
    result.entry_return = true; result.entry_result = entry_result;
    result.entry_source_pc = record.data.start_source_va; return result;
}
NativeModuleResult NativeModuleManager::Stop(NativeModuleToken token) {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    const auto owned = self.Find(token);
    if (!owned) return Result(NativeModuleCode::InvalidToken);
    auto& record = *owned;
    if (record.data.active_calls || record.data.owned_providers || record.data.incoming_bindings)
        return Result(NativeModuleCode::Busy, token);
    if (record.data.state != NativeModuleState::Started) return Result(NativeModuleCode::WrongState, token);
    if (!InputLive(record)) return Result(NativeModuleCode::InvalidInput, token);
    if (!record.stop) return Result(NativeModuleCode::UnsupportedEntry, token);
    const int32_t entry_result = record.stop();
    record.data.state = NativeModuleState::Stopped;
    auto result = Returned(token, entry_result); // host policy, not a game Stop import
    result.entry_return = true; result.entry_result = entry_result;
    result.entry_source_pc = record.data.stop_source_va; return result;
}
NativeModuleResult NativeModuleManager::AbortInitialization(NativeModuleToken token) {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    const auto owned = self.Find(token);
    if (!owned) return Result(NativeModuleCode::InvalidToken);
    auto& data = owned->data;
    if (data.active_calls || data.owned_providers || data.incoming_bindings) return Result(NativeModuleCode::Busy, token);
    if (data.state != NativeModuleState::EntryBlocked || !data.partial_start_supported)
        return Result(NativeModuleCode::WrongState, token);
    if (!AbortAtrac3plusInitialization(data.initialization)) return Result(NativeModuleCode::WrongState, token);
    data.state = NativeModuleState::InitializationAborted;
    return Result(NativeModuleCode::Ok, token); // host operation, NO source Stop/entry/service result
}
NativeModuleResult NativeModuleManager::Unload(NativeModuleToken token) {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    const auto owned = self.Find(token);
    if (!owned) return Result(NativeModuleCode::InvalidToken);
    if (owned->data.active_calls || owned->data.owned_providers || owned->data.incoming_bindings)
        return Result(NativeModuleCode::Busy, token);
    if (owned->data.state == NativeModuleState::Started || owned->data.state == NativeModuleState::EntryBlocked)
        return Result(NativeModuleCode::WrongState, token);
    for (auto at = self.modules.begin(); at != self.modules.end(); ++at) {
        if (*at != owned) continue;
        self.modules.erase(at); break; // no callbacks; stable owned object pin across vector mutation
    }
    self.Bind(); // detach this consumer's provider edges before last input lease retires
    return Result(NativeModuleCode::Ok, token);
}
NativeModuleResult NativeModuleManager::AdvanceGeneration() {
    auto& self = *impl_;
    if (self.gate->executing) return Result(NativeModuleCode::Busy);
    if (!self.modules.empty() || !self.providers.empty()) return Result(NativeModuleCode::Busy);
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
    std::shared_ptr<ModuleRecord> provider_module;
    if (definition.module.owner || definition.module.generation || definition.module.serial || definition.module.uid) {
        provider_module = self.Find(definition.module);
        if (!provider_module) return {NativeModuleCode::InvalidToken, {}};
        if (provider_module->data.state != NativeModuleState::Started || !InputLive(*provider_module))
            return {NativeModuleCode::WrongState, {}};
    }
    for (const auto& provider : self.providers) {
        const auto& old = provider->definition;
        if (old.library == definition.library && old.version == definition.version && old.nid == definition.nid)
            return {NativeModuleCode::DuplicateProvider, {}};
    }
    if (!self.owner || self.serial == (std::numeric_limits<uint64_t>::max)()) return {NativeModuleCode::Exhausted, {}};
    auto record = std::make_shared<ProviderRecord>(); record->definition = std::move(definition);
    record->module = std::move(provider_module);
    record->token = {self.owner, self.generation, self.serial + 1};
    self.providers.push_back(record); // may throw; no registry mutation on failure
    ++self.serial; self.Bind(); return {NativeModuleCode::Ok, record->token};
}
NativeModuleProviderResult NativeModuleManager::RegisterCheckNeedMemProvider(NativeModuleToken token) {
    auto& self = *impl_;
    if (self.gate->executing) return {NativeModuleCode::Busy, {}};
    if (self.domain != NativeModuleDomain::Production) return {NativeModuleCode::IncompatibleProvider, {}};
    const auto module = self.Find(token);
    if (!module) return {NativeModuleCode::InvalidToken, {}};
    if (module->data.state != NativeModuleState::Started) return {NativeModuleCode::WrongState, {}};
    self.gate->executing = true;
    struct Exit { CallGate& gate; ~Exit() { gate.executing = false; } } guard{*self.gate};
    if (module->data.name != "sceAudiocodec_Driver" || !InputLive(*module))
        return {NativeModuleCode::InvalidInput, {}};
    for (const auto& provider : self.providers)
        if (provider->definition.library == "sceAudiocodec" && provider->definition.version == 0 &&
            provider->definition.nid == 0x9D3F790C) return {NativeModuleCode::DuplicateProvider, {}};
    if (!self.owner || self.serial == (std::numeric_limits<uint64_t>::max)())
        return {NativeModuleCode::Exhausted, {}};
    // Own the complete built-in provider and its module/input BEFORE publication.
    // The generic registry still accepts no production callback/claimed ABI.
    auto provider = std::make_shared<ProviderRecord>();
    provider->module = module; provider->check_need_mem = true;
    provider->definition.library = "sceAudiocodec"; provider->definition.abi = CheckNeedMemAbi;
    provider->definition.nid = 0x9D3F790C; provider->definition.module = token;
    provider->token = {self.owner, self.generation, self.serial + 1};
    self.providers.push_back(provider); // if this throws, there was no publication
    ++self.serial; self.Bind(); return {NativeModuleCode::Ok, provider->token};
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
    const auto module = self.Find(token);
    if (!module) return NativeModuleCode::InvalidToken;
    if (index >= module->bindings.size()) return NativeModuleCode::InvalidInput;
    if (module->data.state != NativeModuleState::Started &&
        !(module->data.state == NativeModuleState::EntryBlocked &&
          index == module->data.initialization.frontier_import_index)) return NativeModuleCode::WrongState;
    if (!InputLive(*module)) return NativeModuleCode::InvalidInput;
    const auto provider = module->bindings[index];
    if (!provider) return NativeModuleCode::NotLinked;
    if (provider->check_need_mem) return NativeModuleCode::IncompatibleProvider; // dedicated typed ABI only
    if (module->data.active_calls == (std::numeric_limits<uint32_t>::max)() ||
        (provider->module && provider->module != module &&
         provider->module->data.active_calls == (std::numeric_limits<uint32_t>::max)()))
        return NativeModuleCode::Exhausted;
    auto borrow = std::make_shared<NativeModuleCallBorrow::Impl>();
    borrow->module = module; borrow->provider = provider; borrow->gate = self.gate;
    ++module->data.active_calls;
    if (provider->module && provider->module != module) ++provider->module->data.active_calls;
    out.impl_ = std::move(borrow); return NativeModuleCode::Ok;
}
NativeModuleCode NativeModuleManager::BorrowCheckNeedMem(NativeModuleToken token, uint32_t index,
                                                       NativeCheckNeedMemBorrow& out) {
    auto& self = *impl_;
    if (self.gate->executing || out) return NativeModuleCode::Busy;
    const auto consumer = self.Find(token);
    if (!consumer) return NativeModuleCode::InvalidToken;
    if (self.domain != NativeModuleDomain::Production || index != 2 || consumer->data.name != "sceATRAC3plus_Library")
        return NativeModuleCode::IncompatibleProvider;
    if (consumer->data.state != NativeModuleState::EntryBlocked) return NativeModuleCode::WrongState;
    const auto provider = consumer->bindings[index];
    if (!provider) return NativeModuleCode::NotLinked;
    if (!provider->registered || !provider->check_need_mem || !provider->module ||
        provider->module->data.state != NativeModuleState::Started) return NativeModuleCode::IncompatibleProvider;
    self.gate->executing = true;
    struct Exit { CallGate& gate; ~Exit() { gate.executing = false; } } guard{*self.gate};
    if (!InputLive(*consumer) || !InputLive(*provider->module) || consumer->data.initialization.external_services_dispatched ||
        !Atrac3plusCheckNeedMemStateExact(consumer->data.initialization, false))
        return NativeModuleCode::HostCapabilityRefused;
    if (consumer->data.active_calls == (std::numeric_limits<uint32_t>::max)() ||
        provider->module->data.active_calls == (std::numeric_limits<uint32_t>::max)()) return NativeModuleCode::Exhausted;
    auto borrow = std::make_shared<NativeCheckNeedMemBorrow::Impl>();
    borrow->consumer = consumer; borrow->provider = provider; borrow->gate = self.gate;
    const auto& state = consumer->data.initialization;
    const auto& input = consumer->data.input;
    borrow->capability = {token,input.owner,input.generation,input.serial,state.owner,state.generation,state.serial,
        uint64_t(reinterpret_cast<uintptr_t>(&state)),0x44,0x68,state.writes_count,state.known_bytes};
    ++consumer->data.active_calls; ++provider->module->data.active_calls;
    out.impl_ = std::move(borrow); return NativeModuleCode::Ok;
}
NativeModuleCallResult NativeModuleManager::CallExport(NativeModuleToken token, uint32_t index) {
    if (impl_->gate->executing) return {NativeModuleCode::Busy, false, false, 0};
    const auto module = impl_->Find(token);
    if (!module) return {NativeModuleCode::InvalidToken, false, false, 0};
    if (index >= module->data.exports.size()) return {NativeModuleCode::InvalidInput, false, false, 0};
    // Lifecycle entries have separate typed Start/Stop APIs; alias equality never
    // installs a provider. Audio branches, including local zero branches, are not ported.
    if (module->data.exports[index].lifecycle_callable)
        return {NativeModuleCode::WrongState, false, false, 0};
    return {NativeModuleCode::UnimplementedExport, false, false, 0};
}
bool NativeModuleManager::Snapshot(NativeModuleToken token, NativeModuleSnapshot& out) const {
    const auto module = impl_->Find(token);
    if (!module) return false;
    auto snapshot = module->data; snapshot.input_live = InputLive(*module);
    // Diagnostics carry address identity, never a dereferenceable borrowed span.
    // A returned snapshot can outlive registry mutation without exposing a UAF.
    snapshot.input.bytes = nullptr; snapshot.input.initialized = nullptr;
    out = std::move(snapshot); return true;
}
bool NativeModuleManager::InitializationSnapshot(NativeModuleToken token,
    Atrac3plusInitializationSnapshot& out) const noexcept {
    const auto module = impl_->Find(token);
    if (!module || !module->data.initialization.present) return false;
    out = module->data.initialization; return true; // fixed-size values; no allocation or span capability
}
bool NativeModuleManager::CheckNeedMemSnapshot(NativeModuleToken token, NativeCheckNeedMemSnapshot& out) const noexcept {
    const auto module = impl_->Find(token);
    if (!module || !module->data.check_need_mem.present) return false;
    out = module->data.check_need_mem; return true;
}
std::vector<NativeModuleSnapshot> NativeModuleManager::Snapshots() const {
    std::vector<NativeModuleSnapshot> out; out.reserve(impl_->modules.size());
    for (const auto& module : impl_->modules) {
        auto snapshot = module->data; snapshot.input_live = InputLive(*module);
        snapshot.input.bytes = nullptr; snapshot.input.initialized = nullptr;
        out.push_back(std::move(snapshot));
    }
    return out;
}
std::vector<NativeModuleProviderSnapshot> NativeModuleManager::Providers() const {
    std::vector<NativeModuleProviderSnapshot> out; out.reserve(impl_->providers.size());
    for (const auto& provider : impl_->providers) {
        const auto& definition = provider->definition;
        out.push_back({provider->token, definition.library, definition.abi, definition.version,
                       definition.nid, definition.synthetic_only, definition.module, provider->check_need_mem});
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
    case NativeModuleCode::UnsupportedEntry: return "UnsupportedEntry";
    case NativeModuleCode::UnsupportedPolicy: return "UnsupportedPolicy";
    case NativeModuleCode::InputOverlap: return "InputOverlap";
    case NativeModuleCode::HostCapabilityRefused: return "HostCapabilityRefused";
    case NativeModuleCode::DependencyBlocked: return "DependencyBlocked";
    }
    return "Unknown";
}
const char* NativeModuleStateName(NativeModuleState state) noexcept {
    switch (state) {
    case NativeModuleState::Loaded: return "Loaded";
    case NativeModuleState::Started: return "Started";
    case NativeModuleState::Stopped: return "Stopped";
    case NativeModuleState::EntryBlocked: return "EntryBlocked";
    case NativeModuleState::InitializationAborted: return "InitializationAborted";
    }
    return "Unknown";
}
} // namespace dokodemo::recovered
