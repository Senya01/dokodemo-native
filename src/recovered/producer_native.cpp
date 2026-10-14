#include "producer_native.h"
#include <algorithm>
#include <new>

namespace dokodemo::recovered {
namespace {
constexpr uint32_t Heap = 0x1C93D4, Singleton = 0x10CDC8, Size = 0x6C;
constexpr std::array<uint32_t, 6> Globals = {
    0xF6980, 0x1C9404, 0x1C9454, 0x1C9450, 0x1C9458, Singleton
};
bool Overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m) {
    return static_cast<uint64_t>(a) < static_cast<uint64_t>(b) + m &&
           static_cast<uint64_t>(b) < static_cast<uint64_t>(a) + n;
}
uint32_t Word(const ProducerBlock& block, uint32_t offset) {
    uint32_t value = 0;
    for (uint32_t i = 0; i != 4; ++i)
        value |= static_cast<uint32_t>(block.bytes[offset + i]) << (8 * i);
    return value;
}
ProducerMutation Stop(ProducerStop kind, uint32_t pc, uint32_t callee,
                      const char* detail, std::array<uint32_t, 4> arguments = {}) {
    ProducerMutation result;
    result.boundary = {kind, pc, callee, arguments, detail};
    return result;
}
ProducerMutation Complete(uint32_t source_return = 0, bool known = false) {
    ProducerMutation result;
    result.completed = true;
    result.source_return_known = known;
    result.source_return_value = source_return;
    return result;
}
} // namespace

struct OwnedProducer::Impl {
    bool closed = false;
    std::array<ProducerGlobal, 6> globals{};
    std::vector<std::unique_ptr<ProducerBlock>> blocks;
    std::vector<ProducerAllocation> allocations;
    std::vector<ProducerCheckpoint> checkpoints;

    explicit Impl(ProducerInput input) {
        for (size_t i = 0; i != Globals.size(); ++i) globals[i].address = Globals[i];
        globals[0].initialized = input.available;
        globals[0].value = input.value;
        // Initial singleton0 is an explicit empty private context premise.
        globals[5].initialized = true;
    }
    void StoreGlobal(size_t index, uint32_t value, uint32_t pc) {
        globals[index].value = value;
        globals[index].initialized = true;
        checkpoints.push_back({pc, globals[index].address, value, 4, "store-global"});
    }
    ProducerBlock* Published() {
        const uint32_t p = globals[5].value;
        const auto it = std::find_if(blocks.begin(), blocks.end(),
            [p](const auto& b) { return b->address == p; });
        return it == blocks.end() ? nullptr : it->get();
    }
    const ProducerBlock* Published() const {
        const uint32_t p = globals[5].value;
        const auto it = std::find_if(blocks.begin(), blocks.end(),
            [p](const auto& b) { return b->address == p; });
        return it == blocks.end() ? nullptr : it->get();
    }
    void Store(ProducerBlock& block, uint32_t offset, uint32_t width,
               uint32_t value, uint32_t pc) {
        for (uint32_t i = 0; i != width; ++i) {
            block.bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
            block.initialized[offset + i] = 1;
        }
        checkpoints.push_back({pc, block.address + offset, value, width, "store-payload"});
    }
};

OwnedProducer::OwnedProducer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OwnedProducer::~OwnedProducer() { Close(); }

ProducerCreateResult OwnedProducer::Create(ProducerInput input, ProducerAllocationPlan plan) {
    ProducerCreateResult result;
    try {
        result.owner = std::unique_ptr<OwnedProducer>(new OwnedProducer(std::make_unique<Impl>(input)));
        result.initialization = result.owner->Initialize1B188(plan);
    } catch (const std::bad_alloc&) {
        result.initialization = Stop(ProducerStop::NativeAllocationFailure, 0, 0,
                                    "Host metadata allocation failed; no source fault equivalence.");
    }
    return result;
}

ProducerMutation OwnedProducer::Initialize1B188(ProducerAllocationPlan plan) {
    auto& s = *impl_;
    if (s.closed) return Stop(ProducerStop::Closed, 0, 0, "Native context closed.");
    if (!s.globals[0].initialized)
        return Stop(ProducerStop::MissingInput, 0x1B1A0, 0x65AB0, "Explicit F6980 input missing.");
    const uint32_t p = plan.source_address;
    if (p != 0) {
        bool invalid = (p & 3) != 0 || static_cast<uint64_t>(p) + Size > 0x100000000ULL;
        // Exact BOOT load ranges, including BSS; code/data alias is outside
        // this native component's successful comparison domain.
        invalid = invalid || Overlap(p, Size, 0, 932728) ||
                  Overlap(p, Size, 932736, 1096652);
        for (uint32_t g : Globals) invalid = invalid || Overlap(p, Size, g, 4);
        invalid = invalid || Overlap(p, Size, Heap, 4);
        for (const auto& b : s.blocks) invalid = invalid || Overlap(p, Size, b->address, Size);
        if (invalid)
            return Stop(ProducerStop::NativeDomain, 0x1B1C8, 0x63D14,
                "Protective preflight: aligned nonwrapping disjoint fresh backing required; raw alias/fault order unmodeled.");
    }
    std::unique_ptr<ProducerBlock> block;
    try {
        // Reserve metadata before source effects; allocation failure here is
        // native-only and cannot leave a partly recorded source transcript.
        s.checkpoints.reserve(s.checkpoints.size() + 48);
        s.allocations.reserve(s.allocations.size() + 1);
        s.blocks.reserve(s.blocks.size() + 1);
        if (p != 0 && !plan.force_native_failure && plan.boundary_returns) {
            block = std::make_unique<ProducerBlock>();
            block->address = p;
            block->bytes.fill(plan.allocator_fill);
        }
    } catch (const std::bad_alloc&) {
        return Stop(ProducerStop::NativeAllocationFailure, 0x1B1C8, 0x63D14,
                    "Host allocation failed before source effects; no source temporal equivalence.");
    }
    if (plan.force_native_failure)
        return Stop(ProducerStop::NativeAllocationFailure, 0x1B1C8, 0x63D14,
                    "Injected host allocation refusal before source effects; original NULL handled separately.");
    s.checkpoints.push_back({0x1B1A0, Globals[0], s.globals[0].value, 4, "read-global"});
    s.StoreGlobal(1, s.globals[0].value, 0x65AB8);
    s.StoreGlobal(2, 0x1C0FC, 0x74F74);
    s.StoreGlobal(3, 0x1C184, 0x74F68);
    s.StoreGlobal(4, 0x1DF00, 0x74F80);
    if (!plan.boundary_returns) {
        s.checkpoints.push_back({0x63D24, Heap, Size, 0, "allocator-cut"});
        return Stop(ProducerStop::AllocatorCall, 0x63D24, 0x63318,
                    "Explicit allocator boundary did not return; registration prefix retained, no publication/backing.", {Heap, Size, 0, 0});
    }
    s.allocations.push_back({0x63D24, 0x63318, Heap, p, Size, false, false});
    s.checkpoints.push_back({0x63D24, Heap, Size, 0, "allocator-return"});
    if (block) s.blocks.push_back(std::move(block));
    // This store is the original jal C2FC0 delay slot, before memset.
    s.StoreGlobal(5, p, 0x1B1E0);
    if (p == 0)
        return Stop(ProducerStop::SourceNullAllocationFault, 0xC302C, 0xC2FC0,
                    "Raw would fault at first NULL memset sw after registration/publication. Native refuses the dereference.",
                    {0, 0, Size, 0});
    auto& payload = *s.Published();
    // Raw aligned C2FC0 zero loop stores two words per8, then one residual4.
    for (uint32_t offset = 0; offset != 104; offset += 8) {
        s.Store(payload, offset, 4, 0, 0xC302C);
        s.Store(payload, offset + 4, 4, 0, 0xC3030);
    }
    s.Store(payload, 104, 4, 0, 0xC3048);
    s.checkpoints.push_back({0x1B1E8, Singleton, s.globals[5].value, 4, "reload-singleton"});
    s.Store(payload, 0x64, 4, 0xFFFFFFFF, 0x1B1F4);
    s.Store(payload, 0x18, 4, 0, 0x1B1F8);
    s.Store(payload, 0x14, 4, 0, 0x1B1FC);
    s.Store(payload, 0x1C, 1, 0, 0x1B200);
    s.Store(payload, 0x4C, 4, 0xFFFFFFFF, 0x1B204);
    s.Store(payload, 0x5C, 4, 0xFFFFFFFF, 0x1B208);
    return Complete(p, true);
}

ProducerMutation OwnedProducer::Release1B578(ProducerReleasePlan plan) {
    auto& s = *impl_;
    if (s.closed) return Stop(ProducerStop::Closed, 0, 0, "Native context closed.");
    const uint32_t p = s.globals[5].value;
    if (!p) return Stop(ProducerStop::NullSingleton, 0x1B590, 0,
                        "Raw reads singleton+4 before its later NULL branch; native refuses.");
    auto* b = s.Published();
    if (!b) return Stop(ProducerStop::ForeignSingleton, 0x1B590, 0, "Published backing not owned.");
    const uint32_t child = Word(*b, 4);
    try {
        s.allocations.reserve(s.allocations.size() + 1);
        s.checkpoints.reserve(s.checkpoints.size() + 5);
    } catch (const std::bad_alloc&) {
        return Stop(ProducerStop::NativeAllocationFailure, 0x1B5C0, 0x63EC4, "Host transcript reservation failed.");
    }
    s.checkpoints.push_back({0x1B58C, Singleton, p, 4, "read-global"});
    s.checkpoints.push_back({0x1B590, p + 4, child, 4, "read-payload"});
    if (child) return Stop(ProducerStop::ChildCall, 0x1B5D0, 0x1C044,
                           "Nonzero child destructor unknown; no child or singleton free performed.", {child, 0, 0, 0});
    s.checkpoints.push_back({0x1B59C, Singleton, p, 4, "reload-singleton"});
    if (!plan.boundary_returns) {
        s.checkpoints.push_back({0x63ED4, p, Heap, 0, "free-cut"});
        return Stop(ProducerStop::AllocatorCall, 0x63ED4, 0x63378,
                    "Explicit free boundary did not return; payload and publication retained.", {Heap, p, 0, 0});
    }
    s.allocations.push_back({0x63ED4, 0x63378, Heap, p, Size, true, false});
    s.checkpoints.push_back({0x63ED4, p, Heap, 0, "allocator-free"});
    const auto it = std::find_if(s.blocks.begin(), s.blocks.end(),
                                [p](const auto& x) { return x->address == p; });
    s.blocks.erase(it); // Actual native payload lifetime ends before global clear.
    s.StoreGlobal(5, 0, 0x1B5CC);
    return Complete(plan.source_return_value, true);
}

ProducerMutation OwnedProducer::SetSelection1B2DC(uint32_t selection, uint8_t flag) {
    auto& s = *impl_;
    if (s.closed) return Stop(ProducerStop::Closed, 0, 0, "Native context closed.");
    auto* b = s.Published();
    if (!b) return Stop(ProducerStop::NullSingleton, 0x1B2E4, 0, "Source singleton not readable.");
    try { s.checkpoints.reserve(s.checkpoints.size() + 4); }
    catch (const std::bad_alloc&) {
        return Stop(ProducerStop::NativeAllocationFailure, 0x1B2E0, 0, "Host checkpoint reservation failed.");
    }
    s.checkpoints.push_back({0x1B2E0, Singleton, b->address, 4, "read-global"});
    s.Store(*b, 0x1C, 1, flag, 0x1B2E4);
    s.Store(*b, 0x14, 4, selection, 0x1B2E8);
    s.Store(*b, 0x18, 4, selection, 0x1B2F0);
    return Complete();
}
bool OwnedProducer::SelectionIsZero1B214(bool& value) const {
    const auto* b = impl_->Published();
    if (impl_->closed || !b) return false;
    value = Word(*b, 0x14) == 0;
    return true;
}
bool OwnedProducer::SelectionFlag1B228(uint8_t& value) const {
    const auto* b = impl_->Published();
    if (impl_->closed || !b) return false;
    value = b->bytes[0x1C];
    return true;
}
ProducerMutation OwnedProducer::SetChildFromCheckpoint(uint32_t child) {
    auto& s = *impl_;
    if (s.closed) return Stop(ProducerStop::Closed, 0, 0, "Native context closed.");
    auto* b = s.Published();
    if (!b) return Stop(ProducerStop::NullSingleton, 0, 0, "Checkpoint has no published payload.");
    s.Store(*b, 4, 4, child, 0);
    s.checkpoints.back().operation = "native-child-checkpoint";
    return Complete();
}
ProducerSnapshot OwnedProducer::Snapshot() const {
    ProducerSnapshot result;
    result.closed = impl_->closed;
    result.globals = impl_->globals;
    result.allocator_transcript = impl_->allocations;
    result.checkpoints = impl_->checkpoints;
    for (const auto& b : impl_->blocks) result.live_blocks.push_back(*b);
    return result;
}
void OwnedProducer::Close() {
    if (!impl_ || impl_->closed) return;
    // No source transcript entries: destructor must not allocate or pretend to
    // implement PSP release on a positive child/orphan/NULL singleton.
    impl_->blocks.clear();
    impl_->closed = true;
}
const char* ProducerStopName(ProducerStop kind) {
    switch (kind) {
    case ProducerStop::None: return "none";
    case ProducerStop::MissingInput: return "missing-input";
    case ProducerStop::NativeDomain: return "native-domain";
    case ProducerStop::NativeAllocationFailure: return "native-allocation-failure";
    case ProducerStop::SourceNullAllocationFault: return "source-null-allocation-fault";
    case ProducerStop::AllocatorCall: return "allocator-call";
    case ProducerStop::NullSingleton: return "null-singleton";
    case ProducerStop::ForeignSingleton: return "foreign-singleton";
    case ProducerStop::ChildCall: return "child-call";
    case ProducerStop::Closed: return "closed";
    }
    return "invalid";
}
} // namespace dokodemo::recovered
