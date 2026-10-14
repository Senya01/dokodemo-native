#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo::recovered {
enum class ProducerStop {
    None, MissingInput, NativeDomain, NativeAllocationFailure,
    SourceNullAllocationFault, AllocatorCall, NullSingleton, ForeignSingleton, ChildCall, Closed
};
struct ProducerBoundary {
    ProducerStop kind = ProducerStop::None;
    uint32_t source_pc = 0, callee = 0;
    std::array<uint32_t, 4> arguments{};
    std::string detail;
};
struct ProducerInput {
    // Explicit caller premise at F6980. No actual game initialization inferred.
    bool available = false;
    uint32_t value = 0;
};
struct ProducerAllocationPlan {
    // Logical source identity; never a host pointer. Zero models original NULL.
    uint32_t source_address = 0x10000000;
    uint8_t allocator_fill = 0xA5;
    bool force_native_failure = false;
    bool boundary_returns = true;
};
struct ProducerReleasePlan {
    bool boundary_returns = true;
    uint32_t source_return_value = 0;
};
struct ProducerGlobal {
    uint32_t address = 0, value = 0;
    bool initialized = false;
};
struct ProducerCheckpoint {
    uint32_t source_pc = 0, address = 0, value = 0, width = 0;
    const char* operation = "";
};
struct ProducerAllocation {
    uint32_t source_pc = 0, callee = 0, heap_control = 0, address = 0, bytes = 0;
    bool release = false, native_only = false;
};
struct ProducerBlock {
    uint32_t address = 0;
    std::array<uint8_t, 0x6C> bytes{}, initialized{};
};
struct ProducerSnapshot {
    bool closed = false;
    std::array<ProducerGlobal, 6> globals{};
    std::vector<ProducerBlock> live_blocks;
    std::vector<ProducerAllocation> allocator_transcript;
    std::vector<ProducerCheckpoint> checkpoints;
};
struct ProducerMutation {
    bool completed = false;
    bool source_return_known = false;
    uint32_t source_return_value = 0;
    ProducerBoundary boundary;
};
class OwnedProducer;
struct ProducerCreateResult {
    std::unique_ptr<OwnedProducer> owner;
    ProducerMutation initialization;
};

// Owns the actual distinct6C payload produced by1B188, not CKingKai96,
// CLoadMan4F4, or the28-byte manager registry projection. All global words are
// private model storage. Allocator63318/63378 are explicit transcript cuts.
// Success domain: available input, readable/writable disjoint global slots,
// aligned nonwrapping fresh backing, sequential execution without callbacks
// changing the singleton during memset. No HostAbi/service flags are installed.
class OwnedProducer final {
public:
    static ProducerCreateResult Create(ProducerInput input,
                                       ProducerAllocationPlan plan = {});
    ~OwnedProducer();
    OwnedProducer(const OwnedProducer&) = delete;
    OwnedProducer& operator=(const OwnedProducer&) = delete;
    OwnedProducer(OwnedProducer&&) = delete;
    OwnedProducer& operator=(OwnedProducer&&) = delete;

    // Repeated source initialization overwrites the singleton without freeing
    // the prior block. This component retains that orphan until native Close.
    ProducerMutation Initialize1B188(ProducerAllocationPlan plan = {});
    // Source child0 path only. NULL is refused before source's faulting +4 read;
    // positive child stops before1C044, whose semantics remain unknown.
    ProducerMutation Release1B578(ProducerReleasePlan plan = {});
    ProducerMutation SetSelection1B2DC(uint32_t selection, uint8_t flag);
    bool SelectionIsZero1B214(bool& value) const;
    bool SelectionFlag1B228(uint8_t& value) const;
    // Native checkpoint injection only; deliberately no source-byte credit.
    ProducerMutation SetChildFromCheckpoint(uint32_t child);
    ProducerSnapshot Snapshot() const;
    // Native RAII releases all remaining payloads, including source orphans.
    // It is not the PSP destructor or a proof of its temporal behavior.
    void Close();
private:
    struct Impl;
    explicit OwnedProducer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
const char* ProducerStopName(ProducerStop kind);
} // namespace dokodemo::recovered
