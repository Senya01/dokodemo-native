#include "startup_initialization_native.h"
#include <algorithm>
#include <cstddef>
#include <set>

namespace dokodemo::recovered {
namespace {
constexpr uint32_t PH1 = 0xE3B80, FileBytes = 0xF0C8, Tail = 0xF2C48, End = 0x1EF74C;
uint32_t Word(const std::vector<uint8_t>& data, uint32_t offset) {
    uint32_t out = 0;
    for (uint32_t i = 0; i < 4; ++i) out |= uint32_t(data[offset+i]) << (8*i);
    return out;
}
std::vector<uint8_t> Bytes(uint32_t word) {
    std::vector<uint8_t> out(4);
    for (uint32_t i = 0; i < 4; ++i) out[i] = uint8_t(word >> (8*i));
    return out;
}
}
FreshStartupModuleData::~FreshStartupModuleData() = default;
StartupModuleDataLoad FreshStartupModuleData::Load(const StartupModuleDataInput& input) {
    StartupModuleDataLoad out;
    if (!input.module_base || uint64_t(input.module_base) + End > 0x100000000ULL) {
        out.code = StartupModuleDataCode::Geometry; return out;
    }
    if (input.PH1_file_bytes.size() != FileBytes || input.relocations.size() != 3680 ||
        input.sparse_PH0_file_data.size() != 3) {
        out.code = StartupModuleDataCode::InputUnavailable; return out;
    }
    const uint32_t sparse_at[] = {0xD2A14, 0xD2A24, 0xE35E4};
    const uint32_t sparse_sizes[] = {8, 8, 6};
    auto expected_sparse = input.sparse_PH0_file_data;
    for (std::size_t i = 0; i < expected_sparse.size(); ++i) {
        if (expected_sparse[i].module_offset != sparse_at[i] || expected_sparse[i].bytes.size() != sparse_sizes[i]) {
            out.code = StartupModuleDataCode::Geometry; return out;
        }
    }
    std::set<uint32_t> seen;
    std::vector<uint8_t> expected = input.PH1_file_bytes;
    for (const auto& relocation : input.relocations) {
        const uint32_t absolute = relocation.location_segment == 1 ? PH1 + relocation.offset : relocation.offset;
        if (relocation.type != 2 || relocation.location_segment > 1 || relocation.target_segment > 1 ||
            relocation.offset % 4 || !seen.insert(absolute).second) {
            out.code = StartupModuleDataCode::UnsupportedRelocation; return out;
        }
        const bool sparse = relocation.location_segment == 0;
        if ((!sparse && uint64_t(relocation.offset) + 4 > FileBytes) ||
            (sparse && (relocation.target_segment != 0 ||
                        (relocation.offset != 0xD2A24 && relocation.offset != 0xD2A28)))) {
            out.code = StartupModuleDataCode::UnsupportedRelocation; return out;
        }
        const uint32_t before = sparse ? Word(input.sparse_PH0_file_data[1].bytes, relocation.offset - 0xD2A24) :
                                         Word(input.PH1_file_bytes, relocation.offset);
        const uint64_t value = uint64_t(before) + input.module_base +
                               (relocation.target_segment == 1 ? PH1 : 0);
        if (value > 0xFFFFFFFFULL) { out.code = StartupModuleDataCode::Geometry; return out; }
        const auto bytes = Bytes(static_cast<uint32_t>(value));
        if (sparse) std::copy(bytes.begin(), bytes.end(), expected_sparse[1].bytes.begin() + relocation.offset - 0xD2A24);
        else std::copy(bytes.begin(), bytes.end(), expected.begin() + relocation.offset);
    }
    auto owner = std::make_unique<FreshStartupModuleData>();
    owner->backend_ = WindowsStartupBackend::Create(out.host_status);
    if (!owner->backend_) { out.code = StartupModuleDataCode::NativeFailure; return out; }
    StartupPartitionRequest request; request.bytes = End; request.source_base = input.module_base;
    const auto allocation = owner->backend_->Allocate(request);
    out.host_status = allocation.status;
    if (!out.host_status) { out.code = StartupModuleDataCode::NativeFailure; return out; }
    owner->block_ = allocation.handle;
    out.host_status = owner->backend_->WriteKnown(owner->block_, PH1, input.PH1_file_bytes);
    if (!out.host_status) { out.code = StartupModuleDataCode::NativeFailure; return out; }
    owner->effects_.push_back({StartupModuleDataEffect::Origin::FileBytes, PH1, FileBytes, 0, 0});
    for (const auto& window : input.sparse_PH0_file_data) {
        out.host_status = owner->backend_->WriteKnown(owner->block_, window.module_offset, window.bytes);
        if (!out.host_status) { out.code = StartupModuleDataCode::NativeFailure; return out; }
        owner->effects_.push_back({StartupModuleDataEffect::Origin::FileBytes, window.module_offset,
                                  static_cast<uint32_t>(window.bytes.size()), 0, 0});
    }
    for (const auto& relocation : input.relocations) {
        const bool sparse = relocation.location_segment == 0;
        const uint32_t before = sparse ? Word(input.sparse_PH0_file_data[1].bytes, relocation.offset - 0xD2A24) :
                                         Word(input.PH1_file_bytes, relocation.offset);
        const uint32_t after = sparse ? Word(expected_sparse[1].bytes, relocation.offset - 0xD2A24) : Word(expected, relocation.offset);
        const uint32_t absolute = sparse ? relocation.offset : PH1 + relocation.offset;
        out.host_status = owner->backend_->WriteKnown(owner->block_, absolute, Bytes(after));
        if (!out.host_status) { out.code = StartupModuleDataCode::NativeFailure; return out; }
        owner->effects_.push_back({StartupModuleDataEffect::Origin::Relocation, absolute, 4, before, after});
    }
    // Explicit action and readback, independently of VirtualAlloc's zero bytes.
    out.host_status = owner->backend_->WriteKnown(owner->block_, Tail, std::vector<uint8_t>(End - Tail, 0));
    if (!out.host_status) { out.code = StartupModuleDataCode::NativeFailure; return out; }
    owner->effects_.push_back({StartupModuleDataEffect::Origin::ExplicitTailWrite, Tail, End - Tail, 0, 0});
    const auto file_check = owner->backend_->ReadKnown(owner->block_, PH1, FileBytes);
    const auto tail_check = owner->backend_->ReadKnown(owner->block_, Tail, End - Tail);
    if (!file_check.status || file_check.bytes != expected || !tail_check.status ||
        !std::all_of(tail_check.bytes.begin(), tail_check.bytes.end(), [](uint8_t b) { return b == 0; })) {
        out.code = StartupModuleDataCode::Verification; return out;
    }
    for (const auto& window : expected_sparse) {
        const auto checked = owner->backend_->ReadKnown(owner->block_, window.module_offset, static_cast<uint32_t>(window.bytes.size()));
        if (!checked.status || checked.bytes != window.bytes) { out.code = StartupModuleDataCode::Verification; return out; }
    }
    out.owner = std::move(owner); return out;
}
StartupAllocationBorrow FreshStartupModuleData::Borrow(StartupHostStatus& status) { return backend_->Borrow(block_, status); }
StartupNativeDescription FreshStartupModuleData::Description() { return backend_->Head(block_); }
StartupNativeBytes FreshStartupModuleData::ReadKnown(uint32_t offset, uint32_t count) { return backend_->ReadKnown(block_, offset, count); }
std::vector<StartupHostCall> FreshStartupModuleData::HostCalls() const { return backend_->HostCalls(); }
StartupHostStatus FreshStartupModuleData::Close() { return backend_->Close(); }
} // namespace dokodemo::recovered
