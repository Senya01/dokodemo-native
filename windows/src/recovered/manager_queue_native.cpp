#include "manager_queue_native.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace dokodemo::recovered {
namespace {
constexpr uint32_t manager_table = 0xE17E8, registry_table = 0xE1800;
constexpr uint32_t entry_table = 0xE1818, null_word = 0, sentinel_state = 0xFFFFFFFF;
std::atomic<uint64_t> next_arena{1};
struct Block {
    uint32_t relative = 0, bytes = 0;
    std::string purpose;
    std::unique_ptr<uint32_t[]> words;
    std::vector<uint8_t> initialized;
    Block() = default;
    Block(uint32_t address, uint32_t size, const char* name, ManagerAllocationPlan& plan)
        : relative(address), bytes(size), purpose(name) {
        if (plan.successful_allocations_before_failure == 0) throw std::bad_alloc();
        words.reset(new uint32_t[size / 4]); // Exactly the mapped source byte size.
        initialized.resize(size / 4, 0);
        if (plan.successful_allocations_before_failure > 0)
            --plan.successful_allocations_before_failure;
    }
    void Store(uint32_t byte_offset, uint32_t word) {
        words[byte_offset / 4] = word;
        initialized[byte_offset / 4] = 1;
    }
    uint32_t Load(uint32_t byte_offset) const {
        if (byte_offset % 4 != 0 || byte_offset >= bytes || initialized[byte_offset / 4] == 0)
            throw std::logic_error("uninitialized/outside owned source-layout word");
        return words[byte_offset / 4];
    }
    ManagerAllocation Allocation() const { return {relative, bytes, purpose}; }
    ManagerMemoryBlock Snapshot() const {
        ManagerMemoryBlock out;
        out.relative = relative;
        out.initialized_words = initialized;
        out.words.reserve(initialized.size());
        for (size_t i = 0; i < initialized.size(); ++i)
            out.words.push_back(initialized[i] ? words[i] : 0);
        return out;
    }
};
void InitializeEntries(Block& block, uint32_t count) {
    block.Store(0, count); // Source allocation cookie precedes adjusted pointer.
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t off = 4 + i * 32;
        block.Store(off, entry_table);
        for (uint32_t f = 4; f < 32; f += 4) block.Store(off + f, 0);
    }
}
int32_t SignedHalf(uint32_t adjustment) {
    // Exact sra by one, without implementation-defined signed right shift.
    const uint32_t shifted = (adjustment >> 1) | (adjustment & 0x80000000);
    const int64_t signed_value = shifted >= 0x80000000 ? int64_t(shifted) - 0x100000000LL : shifted;
    return static_cast<int32_t>(signed_value);
}
ManagerBoundary Domain(const char* detail, uint32_t pc = 0) {
    ManagerBoundary b;
    b.kind = ManagerStopKind::NativeDomain;
    b.source_pc = pc;
    b.detail = detail;
    return b;
}
} // namespace

struct OwnedManagerQueue::Impl {
    struct Registry { Block object, entries; uint32_t capacity = 0; };
    uint64_t arena = next_arena.fetch_add(1);
    uint32_t next_relative = 0x1000, anchor_count = 0, command_capacity = 0;
    Block manager, anchors, queue;
    std::vector<std::unique_ptr<Registry>> registries;
    bool terminal = false;
    ManagerBoundary stop;

    uint32_t Reserve(uint32_t bytes) {
        const uint32_t address = next_relative;
        next_relative += (bytes + 15u) & ~15u;
        // Zero-sized source allocations still get distinct native identities.
        if (bytes == 0) next_relative += 16;
        return address;
    }
    ManagerToken Token(uint32_t address) const { return address ? ManagerToken{arena, address} : ManagerToken{}; }
    bool ValidArena(ManagerToken token) const { return token.arena == arena && token.relative != 0; }
    Registry* FindRegistry(uint32_t address) const {
        for (const auto& r : registries) if (r->object.relative == address) return r.get();
        return nullptr;
    }
    bool IsEntry(uint32_t address) const {
        for (const auto& r : registries) {
            const uint32_t begin = r->entries.relative + 4;
            if (address >= begin && address < begin + r->capacity * 32 && (address - begin) % 32 == 0)
                return true;
        }
        return false;
    }
    bool IsAnchor(uint32_t address) const {
        const uint32_t begin = anchors.relative + 4;
        return address >= begin && address < begin + (anchor_count + 1) * 32 && (address - begin) % 32 == 0;
    }
    Block* FindBlock(uint32_t address) {
        auto has = [address](const Block& b) { return address >= b.relative && address < b.relative + b.bytes; };
        if (has(manager)) return &manager;
        if (has(anchors)) return &anchors;
        if (has(queue)) return &queue;
        for (const auto& r : registries) {
            if (has(r->object)) return &r->object;
            if (has(r->entries)) return &r->entries;
        }
        throw std::logic_error("relative identity outside owned arena");
    }
    uint32_t Load(uint32_t address, uint32_t offset = 0) {
        Block* b = FindBlock(address + offset);
        return b->Load(address + offset - b->relative);
    }
    void Store(uint32_t address, uint32_t offset, uint32_t value) {
        Block* b = FindBlock(address + offset);
        b->Store(address + offset - b->relative, value);
    }
    void InsertAfter(uint32_t target, uint32_t predecessor) {
        // 927C0/9304C/93290, disjoint live owned nodes. Source does not unlink.
        const uint32_t successor = Load(predecessor, 8);
        if (successor != null_word) {
            Store(target, 8, successor);
            Store(successor, 4, target);
        }
        Store(target, 4, predecessor);
        Store(predecessor, 8, target);
    }
    ManagerMutation Preflight() const {
        ManagerMutation r;
        if (terminal) {
            r.boundary.kind = ManagerStopKind::Terminal;
            r.boundary.source_pc = stop.source_pc;
            r.boundary.detail = "arena stopped at an unimplemented external boundary";
        }
        return r;
    }
    ManagerMutation Append(uint32_t opcode, uint32_t target, uint32_t argument) {
        ManagerMutation r = Preflight();
        if (r.boundary) return r;
        r.accepted = true;
        r.token = Token(target);
        const uint32_t cursor = manager.Load(36), end = manager.Load(32);
        // Unsigned pointer comparison, append-only storage; no ring/dequeue.
        if (cursor >= end) { r.overflow_dropped = true; return r; }
        Store(cursor, 4, target);
        Store(cursor, 0, opcode);
        manager.Store(36, cursor + 12);
        Store(cursor, 8, argument);
        r.enqueued = true;
        return r;
    }
    std::vector<ManagerAllocation> Allocations() const {
        std::vector<ManagerAllocation> a;
        if (manager.words) a.push_back(manager.Allocation());
        if (anchors.words) a.push_back(anchors.Allocation());
        if (queue.words) a.push_back(queue.Allocation());
        for (const auto& r : registries) {
            a.push_back(r->object.Allocation()); a.push_back(r->entries.Allocation());
        }
        return a;
    }
};

OwnedManagerQueue::OwnedManagerQueue(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OwnedManagerQueue::~OwnedManagerQueue() = default;

ManagerCreateResult OwnedManagerQueue::Create(int32_t anchor_count, int32_t command_capacity,
                                             ManagerAllocationPlan plan) {
    ManagerCreateResult out;
    if (anchor_count < 1 || anchor_count > 15 || command_capacity < 0 || command_capacity > 32 ||
        plan.successful_allocations_before_failure < -1) {
        out.boundary = Domain("closed native constructor domain: anchors1..15, commands0..32, valid failure plan", 0x92924);
        return out;
    }
    std::unique_ptr<Impl> impl;
    uint32_t allocation_stage = 0;
    try {
        impl = std::make_unique<Impl>();
        impl->anchor_count = static_cast<uint32_t>(anchor_count);
        impl->command_capacity = static_cast<uint32_t>(command_capacity);
        impl->manager = Block(impl->Reserve(44), 44, "manager44", plan);
        ++allocation_stage;
        impl->anchors = Block(impl->Reserve(4 + (impl->anchor_count + 1) * 32),
                              4 + (impl->anchor_count + 1) * 32, "manager-anchor-cookie+entries", plan);
        InitializeEntries(impl->anchors, impl->anchor_count + 1);
        ++allocation_stage;
        const uint32_t begin = impl->anchors.relative + 4;
        for (uint32_t i = 0; i < impl->anchor_count; ++i) {
            const uint32_t node = begin + i * 32;
            impl->InsertAfter(node + 32, node);
            impl->Store(node, 12, sentinel_state);
        }
        impl->Store(begin + impl->anchor_count * 32, 12, sentinel_state);
        impl->queue = Block(impl->Reserve(impl->command_capacity * 12), impl->command_capacity * 12,
                            "command12-buffer", plan);
        ++allocation_stage;
        const uint32_t fields[] = {manager_table, 0, 0, 0, 0, impl->anchor_count + 1,
            begin, impl->queue.relative, impl->queue.relative + impl->queue.bytes, impl->queue.relative, 0};
        for (uint32_t i = 0; i < 11; ++i) impl->manager.Store(i * 4, fields[i]);
        out.manager.reset(new OwnedManagerQueue(std::move(impl)));
        return out;
    } catch (const std::bad_alloc&) {
        out.boundary.kind = ManagerStopKind::NativeAllocationFailure;
        out.boundary.source_pc = allocation_stage == 0 ? 0x9343C : allocation_stage == 1 ? 0x92418 : allocation_stage == 2 ? 0x929D4 : 0;
        if (impl) out.released_allocations = impl->Allocations();
        if (allocation_stage == 0) out.source_failure_effect = "93404 outer allocation NULL: constructor skipped, global NULL published at93450";
        else if (allocation_stage == 1) out.source_failure_effect = "923C8 array allocation NULL: entries=4 published9245C; derived constructor accesses unbacked4/12, source fault cut";
        else if (allocation_stage == 2) out.source_failure_effect = "92924 queue allocation NULL: begin=cursor=0,end=capacity*12; positive-capacity enqueue attempts store at4, source fault cut";
        else out.source_failure_effect = "native ownership metadata allocation failed; no corresponding PSP failure effect claimed";
        out.boundary.detail = "native RAII releases mapped allocations; original partial failure has no claimed recovery";
        return out;
    }
}

ManagerToken OwnedManagerQueue::ManagerIdentity() const { return impl_->Token(impl_->manager.relative); }

ManagerMutation OwnedManagerQueue::CreateRegistry(int32_t capacity, uint32_t mask,
                                                 ManagerAllocationPlan plan) {
    auto& s = *impl_;
    ManagerMutation out = s.Preflight();
    if (out.boundary) return out;
    if (capacity < 0 || capacity > 32 || s.registries.size() >= 1024 || plan.successful_allocations_before_failure < -1) {
        out.boundary = Domain("closed native registry capacity0..32, at most1024 live registries", 0x923C8);
        return out;
    }
    std::unique_ptr<Impl::Registry> r;
    uint32_t allocation_stage = 0;
    try {
        r = std::make_unique<Impl::Registry>();
        r->capacity = static_cast<uint32_t>(capacity);
        r->object = Block(s.Reserve(28), 28, "registry28", plan);
        ++allocation_stage;
        r->entries = Block(s.Reserve(4 + r->capacity * 32), 4 + r->capacity * 32,
                           "registry-cookie+entries", plan);
        ++allocation_stage;
        InitializeEntries(r->entries, r->capacity);
        const uint32_t fields[] = {registry_table, 0, 0, 0, mask, r->capacity, r->entries.relative + 4};
        for (uint32_t i = 0; i < 7; ++i) r->object.Store(i * 4, fields[i]);
        const uint32_t address = r->object.relative;
        s.registries.push_back(std::move(r));
        // 92450 global lookup is mapped to this explicitly owned local manager.
        out = s.Append(0, address, 0);
        out.token = s.Token(address);
        return out;
    } catch (const std::bad_alloc&) {
        out.boundary.kind = ManagerStopKind::NativeAllocationFailure;
        out.boundary.source_pc = allocation_stage == 1 ? 0x92418 : 0;
        out.boundary.detail = "native registry cleanup; constructor never publishes a dangling owned registry";
        if (r && r->object.words) out.released_allocations.push_back(r->object.Allocation());
        if (r && r->entries.words) out.released_allocations.push_back(r->entries.Allocation());
        out.source_failure_effect = allocation_stage == 0 ? "native registry-object allocation; source caller allocation outside this subgraph" : allocation_stage == 1 ?
            "923C8 array NULL: source stores adjusted entries=4 then can enqueue0; native releases object and stops" :
            "native ownership-vector allocation failed; no corresponding PSP failure effect claimed";
        return out;
    }
}

ManagerMutation OwnedManagerQueue::RegisterCallback(ManagerToken registry, uint32_t event_index,
                                                    ManagerCallbackDescriptor descriptor, uint32_t mask) {
    auto& s = *impl_;
    ManagerMutation out = s.Preflight();
    if (out.boundary) return out;
    Impl::Registry* r = s.ValidArena(registry) ? s.FindRegistry(registry.relative) : nullptr;
    if (!r || event_index >= s.anchor_count) {
        out.boundary = Domain("owned registry token and event index0..anchor_count-1 required; source has no index check", 0x926EC);
        return out;
    }
    const uint32_t begin = r->entries.relative + 4;
    uint32_t entry = 0;
    for (uint32_t i = 0; i < r->capacity; ++i) {
        if (s.Load(begin + i * 32, 20) == 0) { entry = begin + i * 32; break; }
    }
    if (!entry) {
        out.boundary.kind = ManagerStopKind::RegistryFullErrorCall;
        out.boundary.source_pc = 0x9278C;
        out.boundary.detail = "original calls C1E58(0); return/error/kernel semantics unknown";
        s.terminal = true; s.stop = out.boundary;
        return out;
    }
    // 926EC takes copied two-word descriptor: raw aliasing inputs excluded.
    s.Store(entry, 16, mask);
    s.Store(entry, 20, registry.relative);
    s.Store(entry, 12, 0);
    s.Store(entry, 24, descriptor.function);
    s.Store(entry, 28, descriptor.adjustment);
    out = s.Append(6, entry, event_index);
    out.token = s.Token(entry);
    return out;
}

ManagerMutation OwnedManagerQueue::Enqueue6(ManagerToken entry, uint32_t event_index) {
    auto& s = *impl_;
    ManagerMutation out = s.Preflight();
    if (out.boundary) return out;
    if (!s.ValidArena(entry) || !s.IsEntry(entry.relative) || event_index >= s.anchor_count ||
        s.Load(entry.relative, 20) == 0 || s.Load(entry.relative, 4) != 0 || s.Load(entry.relative, 8) != 0) {
        out.boundary = Domain("command6 requires occupied unlinked owned entry and bounded event index", 0x92E4C);
        return out;
    }
    const uint32_t cursor = s.manager.Load(36), begin = s.manager.Load(28);
    for (uint32_t at = begin; at < cursor; at += 12) {
        if (s.Load(at) == 6 && s.Load(at, 4) == entry.relative) {
            out.boundary = Domain("duplicate pending command6 could corrupt original links", 0x92E4C);
            return out;
        }
    }
    return s.Append(6, entry.relative, event_index);
}

ManagerMutation OwnedManagerQueue::EnqueueEntryState(ManagerToken entry, bool paused) {
    auto& s = *impl_;
    ManagerMutation out = s.Preflight();
    if (out.boundary) return out;
    if (!s.ValidArena(entry) || !s.IsEntry(entry.relative) || s.Load(entry.relative, 20) == 0) {
        out.boundary = Domain("state command requires occupied owned entry", paused ? 0x92EE8 : 0x92EB4);
        return out;
    }
    return s.Append(paused ? 9 : 8, entry.relative, 0);
}

ManagerDrainResult OwnedManagerQueue::Drain() {
    auto& s = *impl_;
    ManagerDrainResult out;
    if (s.terminal) {
        out.boundary = s.stop;
        return out;
    }
    const uint32_t manager = s.manager.relative;
    uint32_t cursor = s.manager.Load(28);
    while (cursor < s.manager.Load(36)) {
        const uint32_t opcode = s.Load(cursor), target = s.Load(cursor, 4), argument = s.Load(cursor, 8);
        if (opcode == 0) {
            // Closed CreateRegistry domain: no duplicate/pending destruction.
            s.InsertAfter(target, manager);
            const uint32_t id = s.manager.Load(12);
            s.Store(target, 12, id);
            s.manager.Store(12, id + 1);
        } else if (opcode == 6) {
            // No explicit PSP index check; native producers enforce domain.
            const uint32_t slot = s.manager.Load(24) + argument * 32;
            const uint32_t predecessor = s.Load(slot, 36);
            if (predecessor != 0) s.InsertAfter(target, predecessor);
        } else if (opcode == 8 || opcode == 9) {
            s.Store(target, 12, opcode == 8 ? 0u : 1u);
        } else if (opcode >= 14) {
            ++out.commands_skipped;
        } else {
            out.boundary.kind = ManagerStopKind::UnsupportedCommand;
            out.boundary.source_pc = 0x9303C;
            out.boundary.detail = "command dispatch target outside the implemented owned subgraph";
            s.terminal = true; s.stop = out.boundary;
            return out;
        }
        ++out.commands_processed;
        cursor += 12;
    }
    // 9308C..94: reset only after full command loop, BEFORE callback scan.
    s.manager.Store(36, s.manager.Load(28));
    out.cursor_reset = true;
    uint32_t current = s.manager.Load(24);
    while (s.Load(current, 8) != 0) { // Source deliberately excludes final sentinel.
        const uint32_t state = s.Load(current, 12);
        if (state == 0) {
            out.boundary.target = s.Token(current);
            out.boundary.owner = s.Token(s.Load(current, 20));
            out.boundary.descriptor = {s.Load(current, 24), s.Load(current, 28)};
            out.boundary.signed_this_adjustment = SignedHalf(out.boundary.descriptor.adjustment);
            out.boundary.adjusted_this_relative = out.boundary.owner.relative + static_cast<uint32_t>(out.boundary.signed_this_adjustment);
            const bool is_virtual = (out.boundary.descriptor.adjustment & 1) != 0;
            out.boundary.kind = is_virtual ? ManagerStopKind::VirtualCallback : ManagerStopKind::DirectCallback;
            out.boundary.source_pc = is_virtual ? 0x93114 : 0x930D0;
            out.boundary.detail = is_virtual ? "stopped before unknown owner vtable read/virtual function resolution" :
                "stopped before jalr descriptor function; no callback return or owner context assumed";
            s.terminal = true; s.stop = out.boundary;
            return out;
        }
        if (state == sentinel_state && s.manager.Load(40) != 0) {
            out.boundary.kind = ManagerStopKind::ObserverCallback;
            out.boundary.source_pc = 0x93140;
            out.boundary.target = s.Token(current);
            out.boundary.detail = "external sentinel observer callback is unavailable";
            s.terminal = true; s.stop = out.boundary;
            return out;
        }
        current = s.Load(current, 8);
    }
    return out;
}

ManagerSnapshot OwnedManagerQueue::Snapshot() const {
    auto& s = *impl_;
    ManagerSnapshot out;
    out.arena = s.arena;
    out.manager = s.Token(s.manager.relative);
    out.anchor_count = s.anchor_count;
    out.command_capacity = s.command_capacity;
    out.queue_cursor_bytes = s.manager.Load(36) - s.manager.Load(28);
    out.command_count = out.queue_cursor_bytes / 12;
    out.terminal = s.terminal;
    out.callback_boundary = s.stop;
    out.allocations = s.Allocations();
    for (const auto& a : out.allocations) out.allocated_bytes += a.bytes;
    out.memory.push_back(s.manager.Snapshot());
    out.memory.push_back(s.anchors.Snapshot());
    out.memory.push_back(s.queue.Snapshot());
    for (const auto& r : s.registries) {
        out.memory.push_back(r->object.Snapshot()); out.memory.push_back(r->entries.Snapshot());
        ManagerRegistrySnapshot rs;
        rs.token = s.Token(r->object.relative); rs.capacity = r->capacity;
        for (uint32_t i = 0; i < r->capacity; ++i) {
            const uint32_t node = r->entries.relative + 4 + i * 32;
            rs.entries.push_back(s.Token(node));
            if (s.Load(node, 20) != 0) ++rs.occupied;
        }
        out.registries.push_back(std::move(rs));
    }
    uint32_t current = s.manager.Load(24);
    while (current != 0) {
        out.event_list_order.push_back(s.Token(current));
        current = s.Load(current, 8);
    }
    current = s.manager.Load(8);
    while (current != 0) {
        out.registry_list_order.push_back(s.Token(current));
        current = s.Load(current, 8);
    }
    return out;
}

const char* ManagerStopName(ManagerStopKind kind) {
    switch (kind) {
    case ManagerStopKind::None: return "none";
    case ManagerStopKind::NativeDomain: return "native-domain";
    case ManagerStopKind::NativeAllocationFailure: return "native-allocation-failure";
    case ManagerStopKind::RegistryFullErrorCall: return "registry-full-external-error";
    case ManagerStopKind::UnsupportedCommand: return "unsupported-command";
    case ManagerStopKind::DirectCallback: return "direct-callback";
    case ManagerStopKind::VirtualCallback: return "virtual-callback";
    case ManagerStopKind::ObserverCallback: return "observer-callback";
    case ManagerStopKind::Terminal: return "terminal";
    }
    return "unknown";
}
} // namespace dokodemo::recovered
