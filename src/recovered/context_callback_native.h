#pragma once
#include "context_owner_native.h"
#include "producer_native.h"

namespace dokodemo::recovered {
struct ContextCallbackInput;
struct ContextCallbackReleaseResult;
class ContextCallbackAllocationToken {
public:
    explicit operator bool() const { return serial_ != 0; }
    uint32_t SourceAddress() const { return node_; }
private:
    friend ContextCallbackResult ExecuteContextCallback1C0FC(OwnedProducer&,const ProducerCallbackBorrow&,CurrentTablesMemory&,const ContextCallbackInput&);
    friend ContextCallbackReleaseResult ExecuteContextCallbackRelease1B578(OwnedProducer&,const ProducerCallbackBorrow&,CurrentTablesMemory&,const ContextCallbackAllocationToken&);
    uint64_t serial_ = 0, owner_ = 0, producer_identity_ = 0;
    uint32_t node_ = 0;
};
// Explicit same-input finite callback domain. Arguments are raw source a0..a3;
// only a0 is a semantic parameter. The caller66644 domain also checks its
// measured volatile bindings. No constructor return2327C is implied.
struct ContextCallbackInput {
    uint32_t entry_pc = 0x66644, return_pc = 0x66654;
    uint32_t context = 0, loadman = 0;
    std::array<uint32_t,4> arguments{};
    std::shared_ptr<CurrentTablesStorageImage> image;
};
struct ContextCallbackResult {
    ContextOwnerBoundary boundary;
    bool completed = false, producer_adopted = false;
    uint32_t node = 0, return_residue = 0, return_pc = 0;
    uint64_t first_effect = 0, end_effect = 0;
    // Exact PC/address/width/value pairing for each SAME-memory data effect.
    // ABI spill-stack/FPR data effects are excluded, never credited implicitly.
    std::vector<ContextOwnerSuffixAccess> effects;
    std::shared_ptr<CurrentTablesStorageImage> producer_backing;
    ContextCallbackAllocationToken allocation;
};
struct ContextCallbackReleaseResult {
    ContextOwnerBoundary boundary;
    bool completed = false;
    uint32_t released_tail = 0, released_producer = 0;
    uint64_t first_effect = 0, end_effect = 0;
    std::vector<ContextOwnerSuffixAccess> effects;
};
// The caller maps the borrowed ACTUAL producer108, context and exact finite
// allocator slices before entry. No full overlapping arena is mapped.
// Unknown/positive CLoadMan children are native atomic protective cuts; the
// original source partial1C138 prefix remains a separate zero-credit case.
ContextCallbackResult ExecuteContextCallback1C0FC(OwnedProducer& producer,
    const ProducerCallbackBorrow& borrow, CurrentTablesMemory& memory,
    const ContextCallbackInput& input);
ContextCallbackReleaseResult ExecuteContextCallbackRelease1B578(OwnedProducer& producer,
    const ProducerCallbackBorrow& borrow,CurrentTablesMemory& memory,
    const ContextCallbackAllocationToken& allocation);
} // namespace dokodemo::recovered
