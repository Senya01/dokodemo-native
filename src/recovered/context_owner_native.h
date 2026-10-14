#pragma once
#include "manager_queue_native.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo::recovered {
enum class ContextOwnerStop {
    None, NativeDomain, NativeAllocationFailure, InitializationCall,
    MissingGlobal, UninitializedField, KernelCall, GateSkipped, Closed
};
struct ContextOwnerBoundary {
    ContextOwnerStop kind = ContextOwnerStop::None;
    uint32_t source_pc = 0, callee = 0;
    // Raw original a0..a3 at import entry. No typed kernel ABI signature is
    // claimed: a1 still contains the sign of the scheduler budget.
    std::array<uint32_t, 4> arguments{};
    uint32_t scheduler_budget_low = 0, scheduler_budget_high = 0;
    std::string detail;
};
struct ContextOwnerRecord {
    uint32_t event_index = 0, mask = 0, descriptor_va = 0;
    ManagerCallbackDescriptor descriptor;
};
struct ContextOwnerBlock {
    ManagerToken token;
    std::vector<uint8_t> bytes, initialized;
};
struct ContextOwnerSnapshot {
    ManagerToken owner;
    bool closed = false, initialization_tail_applied = false;
    bool bound = false, bound_records_match = false, stopped_at_kernel = false;
    ManagerToken manager_registry_projection;
    std::vector<ManagerAllocation> allocation_transcript;
    std::vector<ContextOwnerBlock> blocks;
    ContextOwnerBoundary initialization_cut;
    std::array<ContextOwnerRecord, 5> records;
};
struct ContextOwnerGlobalWord {
    // Explicit caller-supplied checkpoint word, never inferred from BSS.
    bool available = false;
    uint32_t value = 0;
};
struct ContextOwnerCallbackInputs {
    ContextOwnerGlobalWord gate_segment1_10fd4;
    ContextOwnerGlobalWord kernel_context_segment1_e5880;
};
class OwnedContextOwner;
struct ContextOwnerCreateResult {
    std::unique_ptr<OwnedContextOwner> owner;
    ContextOwnerBoundary boundary;
    std::vector<ManagerAllocation> released_allocations;
    std::string original_failure_effect;
};

// Actual portable owned96 payload + owned164 cookie/entries. It is not an
// installed PSP singleton and does not execute the unresolved230EC setup.
// Masked bytes distinguish source-written fields from allocator unknowns.
class OwnedContextOwner final {
public:
    static ContextOwnerCreateResult Create(ManagerAllocationPlan plan = {});
    ~OwnedContextOwner();
    OwnedContextOwner(const OwnedContextOwner&) = delete;
    OwnedContextOwner& operator=(const OwnedContextOwner&) = delete;
    OwnedContextOwner(OwnedContextOwner&&) = delete;
    OwnedContextOwner& operator=(OwnedContextOwner&&) = delete;

    // Explicit entry at23210, after unresolved calls. This is a conditional
    // checkpoint API, not evidence that the actual PSP producer is ready.
    ContextOwnerBoundary ApplyInitializationTailFromCheckpoint();
    // Explicit native checkpoint fixture, no source store equivalence claimed.
    ContextOwnerBoundary SetDelayFromCheckpoint(int32_t source_word);
    // Read-only binding. Existing28-byte manager registry remains a base
    // projection. The96-byte storage and token stay owned by this component.
    // A separate mapping relates identities, not literal PSP pointers/copies.
    ContextOwnerBoundary BindManagerRegistry(const OwnedManagerQueue& manager,
                                             ManagerToken registry);
    ContextOwnerBoundary InvokeCallback23424(ContextOwnerCallbackInputs inputs);
    ContextOwnerBoundary ContinueManagerCallback(const OwnedManagerQueue& manager,
                                               const ManagerBoundary& boundary,
                                               ContextOwnerCallbackInputs inputs);
    ContextOwnerSnapshot Snapshot() const;
    ManagerToken OwnerIdentity() const;
    static std::array<ContextOwnerRecord, 5> CallbackRecords();
    // Whole-component native RAII lifetime; no source destructor equivalence.
    void Close();
private:
    struct Impl;
    explicit OwnedContextOwner(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
const char* ContextOwnerStopName(ContextOwnerStop kind);
} // namespace dokodemo::recovered
