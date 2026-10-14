#include "context_owner_native.h"
#include <algorithm>
#include <atomic>
#include <new>
#include <utility>

namespace dokodemo::recovered {
namespace {
std::atomic<uint64_t> next_owner_arena{0x8000000000000000ULL};
constexpr uint32_t owner_relative = 0x1000, entries_relative = 0x1060;
ContextOwnerBoundary Boundary(ContextOwnerStop kind, uint32_t pc, const char* text) {
    ContextOwnerBoundary b; b.kind = kind; b.source_pc = pc; b.detail = text; return b;
}
const ManagerMemoryBlock* FindBlock(const ManagerSnapshot& s, uint32_t relative) {
    for (const auto& b : s.memory)
        if (relative >= b.relative && relative < b.relative + b.words.size() * 4) return &b;
    return nullptr;
}
bool ManagerWord(const ManagerSnapshot& s, uint32_t address, uint32_t& value) {
    const auto* b = FindBlock(s, address);
    if (!b || (address - b->relative) % 4 != 0) return false;
    const size_t i = (address - b->relative) / 4;
    if (!b->initialized_words[i]) return false;
    value = b->words[i]; return true;
}
}
struct OwnedContextOwner::Impl {
    uint64_t arena = next_owner_arena.fetch_add(1);
    std::unique_ptr<uint8_t[]> owner_storage, entry_storage;
    std::array<uint8_t, 96> owner_mask{};
    std::array<uint8_t, 164> entry_mask{};
    bool closed = false, tail = false, bound = false, records_match = false, kernel = false;
    ManagerToken projection, first_projected_entry;
    std::vector<ManagerAllocation> allocations;
    ManagerToken Token(uint32_t relative) const { return {arena, relative}; }
    void Store(uint32_t off, uint32_t v, bool entry = false) {
        auto* p = entry ? entry_storage.get() : owner_storage.get();
        for (uint32_t i = 0; i < 4; ++i) { p[off + i] = uint8_t(v >> (i * 8)); if (entry) entry_mask[off + i] = 1; else owner_mask[off + i] = 1; }
    }
    void Byte(uint32_t off, uint8_t v) { owner_storage[off] = v; owner_mask[off] = 1; }
    bool Known(uint32_t off) const { for (uint32_t i = 0; i < 4; ++i) if (!owner_mask[off + i]) return false; return true; }
    uint32_t Load(uint32_t off) const {
        return uint32_t(owner_storage[off]) | (uint32_t(owner_storage[off+1]) << 8) |
            (uint32_t(owner_storage[off+2]) << 16) | (uint32_t(owner_storage[off+3]) << 24);
    }
    ContextOwnerBoundary Live() const {
        if (closed) return Boundary(ContextOwnerStop::Closed, 0, "native component lifetime has ended");
        if (kernel) return Boundary(ContextOwnerStop::KernelCall, 0x74DBC, "stopped at unresolved kernel call; continuation is unavailable");
        return {};
    }
};
std::array<ContextOwnerRecord, 5> OwnedContextOwner::CallbackRecords() {
    return {{{1, 0x202, 0xD97A4, {0x23424, 0}}, {2, 0x202, 0xD97AC, {0x2345C, 0}},
        {6, 0x202, 0xD97B4, {0x23A70, 0}}, {9, 0x402, 0xD97BC, {0x23DA0, 0}},
        {10, 0x402, 0xD97C4, {0x23E08, 0}}}};
}
OwnedContextOwner::OwnedContextOwner(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OwnedContextOwner::~OwnedContextOwner() = default;
ContextOwnerCreateResult OwnedContextOwner::Create(ManagerAllocationPlan plan) {
    ContextOwnerCreateResult out;
    if (plan.successful_allocations_before_failure < -1) {
        out.boundary = Boundary(ContextOwnerStop::NativeDomain, 0, "invalid native allocation plan"); return out;
    }
    std::unique_ptr<Impl> s;
    uint32_t stage = 0;
    try {
        s = std::make_unique<Impl>(); s->allocations.reserve(2);
        auto allocate = [&](uint32_t bytes, uint32_t relative, const char* purpose) {
            if (plan.successful_allocations_before_failure == 0) throw std::bad_alloc();
            auto p = std::unique_ptr<uint8_t[]>(new uint8_t[bytes]);
            s->allocations.push_back({relative, bytes, purpose});
            if (plan.successful_allocations_before_failure > 0) --plan.successful_allocations_before_failure;
            ++stage; return p;
        };
        s->owner_storage = allocate(96, owner_relative, "CKingKai96 callback owner;18380 allocator request");
        //923C8 fixed five-entry base construction inside the96-byte allocation.
        s->Store(0, 0xE1800); s->Store(4, 0); s->Store(8, 0); s->Store(12, 0);
        s->Store(16, 2); s->Store(20, 5);
        s->entry_storage = allocate(164, entries_relative, "923C8 cookie4 + five32-byte entries");
        s->Store(0, 5, true);
        for (uint32_t i = 0; i < 5; ++i) {
            s->Store(4 + i * 32, 0xE1818, true);
            for (uint32_t f = 4; f < 32; f += 4) s->Store(4 + i * 32 + f, 0, true);
        }
        s->Store(24, entries_relative + 4); s->Store(0, 0xD98E0); //230D8 derived vtable.
        //230EC first five926EC success writes; local registry is the true owner.
        //Queue effects belong to an explicitly bound manager, not this storage.
        const auto records = CallbackRecords();
        for (uint32_t i = 0; i < 5; ++i) {
            const uint32_t off = 4 + i * 32;
            s->Store(off + 16, records[i].mask, true); s->Store(off + 20, owner_relative, true);
            s->Store(off + 12, 0, true); s->Store(off + 24, records[i].descriptor.function, true);
            s->Store(off + 28, records[i].descriptor.adjustment, true);
        }
        out.owner.reset(new OwnedContextOwner(std::move(s)));
        out.boundary = Boundary(ContextOwnerStop::InitializationCall, 0x2318C,
            "230EC initialization stops before1B188; portable payload/records are live, tail fields remain unknown");
        out.boundary.callee = 0x1B188; return out;
    } catch (const std::bad_alloc&) {
        if (s) out.released_allocations = s->allocations;
        out.boundary = Boundary(ContextOwnerStop::NativeAllocationFailure, stage == 1 ? 0x92418 : 0x18388,
            "native transactional RAII refusal; no dangling payload published");
        out.original_failure_effect = stage == 0 ?
            "18388 NULL skips230AC then18394 uses preexisting singleton; its availability is unknown" :
            "923C8 array NULL adjusts entries to4, may enqueue0/publish singleton;230EC registration can fault at address24";
        return out;
    }
}
ContextOwnerBoundary OwnedContextOwner::ApplyInitializationTailFromCheckpoint() {
    auto& s = *impl_; if (auto b = s.Live(); b.kind != ContextOwnerStop::None) return b;
    //23210..2324C after explicitly supplied external checkpoint, not fresh start.
    s.Store(0x50, 0xFFFFFFFF); s.Store(0x2C, 2000); s.Store(0x5C, 0);
    s.Byte(0x40, 0); s.Byte(0x48, 0); s.Store(0x24, 0); s.Store(0x28, 0);
    s.Store(0x54, 0xFFFFFFFF); s.Store(0x58, 0); s.Store(0x1C, 0); s.Store(0x20, 0);
    s.tail = true; return {};
}
ContextOwnerBoundary OwnedContextOwner::SetDelayFromCheckpoint(int32_t source_word) {
    auto& s = *impl_; if (auto b = s.Live(); b.kind != ContextOwnerStop::None) return b;
    s.Store(0x2C, static_cast<uint32_t>(source_word)); return {};
}
ContextOwnerBoundary OwnedContextOwner::BindManagerRegistry(const OwnedManagerQueue& manager, ManagerToken registry) {
    auto& s = *impl_; if (auto b = s.Live(); b.kind != ContextOwnerStop::None) return b;
    const auto snap = manager.Snapshot();
    const auto found = std::find_if(snap.registries.begin(), snap.registries.end(),
        [&](const auto& r) { return r.token == registry; });
    uint32_t mask = 0, count = 0, vt = 0;
    if (!registry || registry.arena != snap.arena || found == snap.registries.end() || found->capacity != 5 ||
        !ManagerWord(snap, registry.relative + 16, mask) || mask != 2 ||
        !ManagerWord(snap, registry.relative + 20, count) || count != 5 ||
        !ManagerWord(snap, registry.relative, vt) || vt != 0xE1800)
        return Boundary(ContextOwnerStop::NativeDomain, 0x923C8, "exact live manager base projection must have capacity5/mask2; it remains28 bytes");
    if (s.bound && !(s.projection == registry))
        return Boundary(ContextOwnerStop::NativeDomain, 0, "stable owner binding cannot be reassigned");
    bool match = found->occupied == 5 && found->entries.size() == 5;
    const auto records = CallbackRecords();
    for (size_t i = 0; match && i < 5; ++i) {
        const auto token = found->entries[i]; uint32_t v = 0;
        match = ManagerWord(snap, token.relative + 16, v) && v == records[i].mask &&
            ManagerWord(snap, token.relative + 20, v) && v == registry.relative &&
            ManagerWord(snap, token.relative + 24, v) && v == records[i].descriptor.function &&
            ManagerWord(snap, token.relative + 28, v) && v == 0;
    }
    s.projection = registry; s.bound = true; s.records_match = match;
    s.first_projected_entry = match ? found->entries[0] : ManagerToken{};
    return {};
}
ContextOwnerBoundary OwnedContextOwner::InvokeCallback23424(ContextOwnerCallbackInputs inputs) {
    auto& s = *impl_; if (auto b = s.Live(); b.kind != ContextOwnerStop::None) return b;
    if (!inputs.gate_segment1_10fd4.available)
        return Boundary(ContextOwnerStop::MissingGlobal, 0x23430, "gate word availability must be supplied explicitly");
    if (inputs.gate_segment1_10fd4.value == 1)
        return Boundary(ContextOwnerStop::GateSkipped, 0x23454, "original gate1 returns without reading owner2C or kernel context");
    if (!s.Known(0x2C)) return Boundary(ContextOwnerStop::UninitializedField, 0x23440, "real96-byte owner exists but delay2C is not source-initialized at this checkpoint");
    if (!inputs.kernel_context_segment1_e5880.available)
        return Boundary(ContextOwnerStop::MissingGlobal, 0x74DA0, "kernel context producer/readiness remains external");
    auto b = Boundary(ContextOwnerStop::KernelCall, 0x74DBC, "23424 joins74D84 and stops before unresolved CB288; no kernel return is fabricated");
    b.callee = 0xCB288;
    //74D9C/a0→s2 retains low delay;2344C sign-extends it into a1→s3.
    //Raw a0..a3 preserve residual a1 while zeroing a2/a3. This does not
    //establish which registers belong to the typed ThreadMan import ABI.
    const uint32_t delay = s.Load(0x2C);
    b.arguments = {inputs.kernel_context_segment1_e5880.value, (delay & 0x80000000) ? 0xFFFFFFFFu : 0u, 0, 0};
    b.scheduler_budget_low = delay; b.scheduler_budget_high = b.arguments[1];
    s.kernel = true; return b;
}
ContextOwnerBoundary OwnedContextOwner::ContinueManagerCallback(const OwnedManagerQueue& manager, const ManagerBoundary& boundary, ContextOwnerCallbackInputs inputs) {
    auto& s = *impl_; if (auto b = s.Live(); b.kind != ContextOwnerStop::None) return b;
    if (!s.bound) return Boundary(ContextOwnerStop::NativeDomain, 0x930D0, "no manager binding");
    if (auto b = BindManagerRegistry(manager, s.projection); b.kind != ContextOwnerStop::None) return b;
    if (!s.bound || !s.records_match || boundary.kind != ManagerStopKind::DirectCallback ||
        !(boundary.owner == s.projection) || !(boundary.target == s.first_projected_entry) ||
        boundary.descriptor.function != 0x23424 || boundary.descriptor.adjustment != 0 ||
        boundary.signed_this_adjustment != 0 || boundary.adjusted_this_relative != s.projection.relative)
        return Boundary(ContextOwnerStop::NativeDomain, 0x930D0, "manager callback must match verified first record and exact base-projection binding");
    return InvokeCallback23424(inputs);
}
ContextOwnerSnapshot OwnedContextOwner::Snapshot() const {
    const auto& s = *impl_; ContextOwnerSnapshot out;
    out.owner = s.Token(owner_relative); out.closed = s.closed; out.initialization_tail_applied = s.tail;
    out.bound = s.bound; out.bound_records_match = s.records_match; out.stopped_at_kernel = s.kernel;
    out.manager_registry_projection = s.projection; out.allocation_transcript = s.allocations;
    out.records = CallbackRecords();
    out.initialization_cut = Boundary(ContextOwnerStop::InitializationCall, 0x2318C, "unexecuted external230EC setup; checkpoint suffix is separate");
    out.initialization_cut.callee = 0x1B188;
    if (!s.closed) {
        for (bool entry : {false, true}) {
            ContextOwnerBlock b; b.token = s.Token(entry ? entries_relative : owner_relative);
            const uint32_t n = entry ? 164u : 96u; b.bytes.resize(n); b.initialized.resize(n);
            for (uint32_t i = 0; i < n; ++i) {
                b.initialized[i] = entry ? s.entry_mask[i] : s.owner_mask[i];
                b.bytes[i] = b.initialized[i] ? (entry ? s.entry_storage[i] : s.owner_storage[i]) : 0;
            }
            out.blocks.push_back(std::move(b));
        }
    }
    return out;
}
ManagerToken OwnedContextOwner::OwnerIdentity() const { return impl_->Token(owner_relative); }
void OwnedContextOwner::Close() {
    auto& s = *impl_; s.closed = true; s.owner_storage.reset(); s.entry_storage.reset();
    s.bound = false; s.records_match = false; s.projection = {}; s.first_projected_entry = {};
}
const char* ContextOwnerStopName(ContextOwnerStop k) {
    switch (k) {
    case ContextOwnerStop::None: return "none";
    case ContextOwnerStop::NativeDomain: return "native_domain";
    case ContextOwnerStop::NativeAllocationFailure: return "native_allocation_failure";
    case ContextOwnerStop::InitializationCall: return "initialization_call";
    case ContextOwnerStop::MissingGlobal: return "missing_global";
    case ContextOwnerStop::UninitializedField: return "uninitialized_field";
    case ContextOwnerStop::KernelCall: return "kernel_call";
    case ContextOwnerStop::GateSkipped: return "gate_skipped";
    case ContextOwnerStop::Closed: return "closed";
    } return "unknown";
}
} // namespace dokodemo::recovered
