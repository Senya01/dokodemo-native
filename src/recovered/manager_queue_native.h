#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo::recovered {
// An arena identity and a relative address label, never a PSP or host pointer.
// All source pointer words in Snapshot use the corresponding relative labels.
struct ManagerToken {
    uint64_t arena = 0;
    uint32_t relative = 0;
    explicit operator bool() const { return arena != 0 && relative != 0; }
    bool operator==(const ManagerToken& x) const { return arena == x.arena && relative == x.relative; }
};
struct ManagerCallbackDescriptor { uint32_t function = 0, adjustment = 0; };
enum class ManagerStopKind {
    None, NativeDomain, NativeAllocationFailure, RegistryFullErrorCall,
    UnsupportedCommand, DirectCallback, VirtualCallback, ObserverCallback, Terminal
};
struct ManagerBoundary {
    ManagerStopKind kind = ManagerStopKind::None;
    uint32_t source_pc = 0;
    ManagerToken target, owner;
    ManagerCallbackDescriptor descriptor;
    // sra(adjustment,1), before original 32-bit address addition.
    int32_t signed_this_adjustment = 0;
    uint32_t adjusted_this_relative = 0; // Original modulo32 owner+sra(flags,1).
    std::string detail;
    explicit operator bool() const { return kind != ManagerStopKind::None; }
};
struct ManagerAllocationPlan {
    // Native allocation policy; -1 allows real new, 0 fails the next request.
    // This does not prove the PSP allocator, global, or kernel availability.
    int32_t successful_allocations_before_failure = -1;
};
struct ManagerAllocation {
    uint32_t relative = 0, bytes = 0;
    std::string purpose;
};
struct ManagerMemoryBlock {
    uint32_t relative = 0;
    std::vector<uint32_t> words;
    // Unwritten allocator bytes are omitted from semantic equality.
    std::vector<uint8_t> initialized_words;
};
struct ManagerRegistrySnapshot {
    ManagerToken token;
    uint32_t capacity = 0, occupied = 0;
    std::vector<ManagerToken> entries;
};
struct ManagerSnapshot {
    uint64_t arena = 0;
    ManagerToken manager;
    uint32_t anchor_count = 0, command_capacity = 0;
    uint32_t command_count = 0, queue_cursor_bytes = 0, allocated_bytes = 0;
    bool terminal = false;
    ManagerBoundary callback_boundary;
    std::vector<ManagerAllocation> allocations;
    std::vector<ManagerMemoryBlock> memory;
    std::vector<ManagerRegistrySnapshot> registries;
    // Source next-pointer order, including all dual-ended anchor sentinels.
    std::vector<ManagerToken> event_list_order, registry_list_order;
};
struct ManagerMutation {
    bool accepted = false, enqueued = false, overflow_dropped = false;
    ManagerToken token;
    ManagerBoundary boundary;
    // On native allocation failure, these allocations were released by RAII.
    std::vector<ManagerAllocation> released_allocations;
    std::string source_failure_effect;
};
struct ManagerDrainResult {
    uint32_t commands_processed = 0, commands_skipped = 0;
    bool cursor_reset = false;
    ManagerBoundary boundary;
};
class OwnedManagerQueue;
struct ManagerCreateResult {
    std::unique_ptr<OwnedManagerQueue> manager;
    ManagerBoundary boundary;
    std::vector<ManagerAllocation> released_allocations;
    // Original NULL effects, distinct from native cleanup/validation policy.
    std::string source_failure_effect;
};

// Closed portable arena: fixed allocations; no external observers or callbacks.
// No per-registry destruction/reallocation API; links survive vector growth.
// Whole-arena destruction is native policy, not PSP destructor equivalence.
class OwnedManagerQueue final {
public:
    static ManagerCreateResult Create(int32_t anchor_count = 15,
                                     int32_t command_capacity = 32,
                                     ManagerAllocationPlan plan = {});
    ~OwnedManagerQueue();
    OwnedManagerQueue(const OwnedManagerQueue&) = delete;
    OwnedManagerQueue& operator=(const OwnedManagerQueue&) = delete;
    OwnedManagerQueue(OwnedManagerQueue&&) = delete;
    OwnedManagerQueue& operator=(OwnedManagerQueue&&) = delete;

    // Models 923C8 with the explicitly created local manager as its manager
    // identity; does not install/read PSP global1D7EE8 or enable host fixtures.
    ManagerMutation CreateRegistry(int32_t capacity, uint32_t mask = 0,
                                  ManagerAllocationPlan plan = {});
    // 926EC: first slot with owner==0; descriptor/mask/owner/state writes occur
    // before enqueue6. A full queue still leaves that entry occupied/unlinked.
    ManagerMutation RegisterCallback(ManagerToken registry, uint32_t event_index,
                                     ManagerCallbackDescriptor descriptor,
                                     uint32_t mask);
    ManagerMutation Enqueue6(ManagerToken entry, uint32_t event_index);
    // Source commands8/9: change entry state when consumed, not at enqueue.
    ManagerMutation EnqueueEntryState(ManagerToken entry, bool paused);
    // Implements closed command0/6/8/9 effects and callback scan through the
    // first real external boundary. Unknown callbacks cannot be resumed here.
    ManagerDrainResult Drain();
    ManagerSnapshot Snapshot() const;
    ManagerToken ManagerIdentity() const;

private:
    struct Impl;
    explicit OwnedManagerQueue(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
const char* ManagerStopName(ManagerStopKind kind);
} // namespace dokodemo::recovered
