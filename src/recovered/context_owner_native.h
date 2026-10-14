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
class OwnedProducer;

// Separate caller-provided CLoadMan4F4 checkpoint. No fields are copied to
// CKingKai96, producer108, or the manager base projection.
struct ContextOwnerLoadCheckpoint {
    uint32_t source_address = 0;
    uint32_t source_stack_address = 0;
    std::array<uint8_t, 0x4F4> bytes{}, initialized{};
};
struct ContextOwnerSuffixAccess {
    uint32_t source_pc = 0, address = 0, width = 0, value = 0;
    const char* operation = "";
};
struct ContextOwnerPrefixResult {
    ContextOwnerBoundary boundary;
    uint32_t entry_pc = 0, producer_return_pc = 0;
    uint32_t directory_address = 0, filename_address = 0;
    std::string directory, filename;
    std::array<uint8_t, 32> filename_scratch{}, filename_scratch_mask{};
    std::vector<ContextOwnerSuffixAccess> transcript;
};
struct ContextOwnerPreparedBlock {
    uint32_t address = 0;
    std::vector<uint8_t> bytes, initialized;
    bool writable = false;
};
struct ContextOwnerPreparedNonzero2327C {
    // This is an explicit conditional entry after the unresolved formatted
    // load, allocation,6656C constructor and its callback have returned.
    // Supplying it proves none of those earlier operations or PSP readiness.
    bool acknowledges_unverified_prior_load_constructor_and_callback = false;
    uint32_t entry_pc = 0x2327C;
    uint32_t source_owner_address = 0, source_vm_address = 0;
    ContextOwnerGlobalWord loadman_pointer_10cdc4;
    ContextOwnerGlobalWord cached_activation_word_1c93fc;
    ContextOwnerGlobalWord prior_vm_publication_10e448;
    // Owned copies of exact disjoint VM47C, CLoadMan4F4, KSC, argument-array
    // and frame backing. Source read masks are checked before any mutation.
    std::vector<ContextOwnerPreparedBlock> blocks;
};
class ContextOwnerActivationToken {
public:
    ContextOwnerActivationToken() = default;
    explicit operator bool() const { return serial_ != 0; }
    uint32_t SourceEntryPc() const { return 0x2327C; }
    uint32_t SourceCallPc() const { return 0x23208; }
    uint32_t SourceCallee() const { return 0x67FD4; }
    uint32_t SourceReturnPc() const { return 0x23210; }
    ManagerToken Owner() const { return owner_; }
private:
    friend class OwnedContextOwner;
    ManagerToken owner_;
    uint64_t serial_ = 0;
    uint64_t owner_generation_ = 0;
};
struct ContextOwnerActivationResult {
    ContextOwnerBoundary boundary;
    ContextOwnerActivationToken continuation;
    bool completed = false, source_return_known = false;
    uint32_t source_return_value = 0;
};
struct ContextOwnerSuffixSnapshot {
    bool prepared = false, completed = false, closed = false;
    uint32_t source_entry_pc = 0, reached_call_pc = 0, callee = 0, return_pc = 0;
    uint32_t source_owner_address = 0, source_vm_address = 0;
    ContextOwnerGlobalWord vm_publication_10e448;
    std::vector<ContextOwnerPreparedBlock> blocks;
    std::vector<ContextOwnerSuffixAccess> transcript;
    // Native Close releases these prepared copies, not a PSP destructor.
    std::vector<ManagerAllocation> native_released_backing;
};
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
    // Composes an actually live completed1B188 producer; executes23194's
    // directory/name prefix and stops BEFORE actual C36C0. No return is faked.
    ContextOwnerPrefixResult ContinueInitialization23194AfterProducer(
        const OwnedProducer& producer, const ContextOwnerLoadCheckpoint& load) const;
    // Explicit prepared nonzero branch2327C; never NULL branch231F4.
    // Returns a private, owner-bound one-shot token for reached call23208.
    ContextOwnerActivationResult PrepareNonzero2327CActivationAfterUnverifiedLoadAndConstructor(
        ContextOwnerPreparedNonzero2327C prepared);
    // Executes67FD4→67F1C, then publication2321C and actual23210..2326C
    // tail/return1. Does not call the checkpoint fixture above.
    ContextOwnerActivationResult ContinuePreparedNonzero2327CActivation(
        const ContextOwnerActivationToken& token);
    ContextOwnerSuffixSnapshot SuffixSnapshot() const;
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
