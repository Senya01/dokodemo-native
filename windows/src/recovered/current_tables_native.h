#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo::recovered {

enum class CurrentTable : uint8_t { Stack, Local, Ctx28, Global, Image, Pool };
struct CurrentTablesAddress {
    CurrentTable table = CurrentTable::Image;
    uint32_t index = 0, physical = 0;
    uint64_t identity = 0;
};
struct CurrentTablesEffect {
    enum class Kind : uint8_t { Read, Write, LifetimeEnd };
    Kind kind = Kind::Read;
    uint32_t address = 0, width = 0, before = 0, value = 0;
    uint64_t identity = 0;
};
struct CurrentTablesRegion {
    uint32_t base = 0;
    uint64_t identity = 0;
    std::vector<uint8_t> bytes, readable, writable;
    bool live = true;
};
// New explicit-domain observer; historical Capture retains its exact schema.
// Initialized knowledge is independent of readable/writable permission.
struct CurrentTablesSnapshotRegion {
    uint32_t base = 0;
    uint64_t identity = 0;
    std::vector<uint8_t> bytes, readable, writable, initialized;
    bool live = true;
};
// Shared source storage lease: construction storage and ctx28 borrow this SAME
// mutable image. Native snapshot forks expose their new handle explicitly.
struct CurrentTablesStorageImage {
    uint32_t source_address = 0;
    uint64_t identity = 0;
    bool live = false;
    std::vector<uint8_t> bytes, initialized;
};

// Explicit physical byte/mask domain. Map rejects ambiguous region overlaps;
// different table views of ONE region retain their actual physical aliases.
// Copies are native transactional snapshots with COW, not PSP clone calls.
class CurrentTablesMemory {
public:
    CurrentTablesMemory();
    void Map(CurrentTablesRegion region);
    void MapSharedImage(std::shared_ptr<CurrentTablesStorageImage> image,
                        std::vector<uint8_t> readable, std::vector<uint8_t> writable);
    // Administrative adoption, not source stores. Exact owned base/identity/
    // bytes/initialized knowledge required; permissions/log stay unchanged.
    // A shared region accepts its SAME handle only; expired regions stay dead.
    void BindSharedImage(uint64_t identity, std::shared_ptr<CurrentTablesStorageImage> image);
    std::shared_ptr<CurrentTablesStorageImage> SharedImage(uint64_t identity) const;
    CurrentTablesMemory Fork() const;
    uint32_t Read(uint32_t address, uint32_t width);
    void Write(uint32_t address, uint32_t width, uint32_t value);
    uint32_t Peek(uint32_t address, uint32_t width) const;
    void EndLifetime(uint64_t identity); // native checked lifetime cut only
    bool Live(uint64_t identity) const;
    uint64_t Identity(uint32_t address, uint32_t width) const;
    const std::vector<uint8_t>& Bytes(uint64_t identity) const;
    const std::vector<uint8_t>& Readable(uint64_t identity) const;
    const std::vector<uint8_t>& Writable(uint64_t identity) const;
    const std::vector<uint8_t>& Initialized(uint64_t identity) const;
    std::vector<CurrentTablesRegion> Capture() const;
    std::vector<CurrentTablesSnapshotRegion> CaptureWithInitialized() const;
    std::vector<CurrentTablesEffect> effects() const;
    std::vector<CurrentTablesEffect> effects_from(uint64_t first) const;
    uint64_t effect_count() const;
private:
    struct State;
    std::shared_ptr<State> state_;
    void detach();
};

// Relocated BOOT66DD0 data effects, including repeated table/context/global reads.
// Original ABI register-save stack effects are outside this portable function.
// Mapped faults can leave partial effects; call on a COW candidate for a native
// checked transaction. Count <=0 still initializes1C93F8 when it was zero.
void CurrentTablesRebase66DD0(CurrentTablesMemory& memory, uint32_t context,
                             uint32_t words, int32_t count, uint32_t module_base = 0);
// C2A48 positive-length source copy, preserving block reads and byte order.
// Overlap is modeled exactly as this source routine, not host memcpy/memmove.
void CurrentTablesCopyC2A48(CurrentTablesMemory& memory, uint32_t destination,
                           uint32_t source, uint32_t count);

struct CurrentTablesMapping {
    uint32_t module_base = 0;
    uint32_t image_base = 0, local_allocation_base = 0, code_object_base = 0;
    uint32_t context_base = 0, stack_base = 0, stack_count = 0, callback = 0;
    int16_t frame_pointer = -1;
    uint32_t reference_base = 0, shared_globals_base = 0;
    uint16_t shared_globals_count = 0;
    uint64_t image_identity = 0, local_identity = 0, code_identity = 0;
    uint64_t context_identity = 0, stack_identity = 0, globals_identity = 0;
    // These are external domain premises, not recovered firmware readiness.
    bool allocator_boundary_available = false;
    bool borrowed_image_lifetime_available = false;
};
struct CurrentTablesInput {
    std::vector<uint8_t> image, readable, writable;
    std::string source_sha256;
    CurrentTablesMapping mapping;
    std::shared_ptr<CurrentTablesStorageImage> borrowed_image;
    std::shared_ptr<CurrentTablesStorageImage> retained_global_image;
    // Exact caller/allocator-domain byte states and masks. No default zero
    // constructor/stack/allocator contents are silently inferred by Load.
    CurrentTablesRegion context, stack, local_allocation, code_object;
    // Optional explicit source-backed allocator seam, invoked in source order.
    // Empty uses the named supplied-success allocation-domain regions above.
    std::function<CurrentTablesRegion(uint32_t call_pc, uint32_t wrapper,
                                      uint32_t size, uint32_t expected_address)> allocation_hook;
    // Empty only when E591C initially NULL: globals then borrow THIS image.
    // Existing singleton globals need their exact bytes, masks and identity.
    CurrentTablesRegion retained_globals;
};
struct CurrentTablesLayout {
    uint32_t image = 0, image_size = 0, code = 0, code_size = 0;
    uint32_t pool = 0, pool_size = 0, local = 0, ctx28 = 0, global = 0;
    uint16_t local_count = 0, ctx28_count = 0, global_count = 0;
};

class CurrentTablesOwner {
public:
    CurrentTablesOwner() = default; // unavailable; never installs a VM flag
    // Bounded successful66FDC/66DD0 domain. Original Program bytes stay intact.
    // 7C714/7C754 success is an explicit supplied boundary;
    // constructor65FF4/registry/callback58D4 are NOT proved by this result.
    static CurrentTablesOwner Load(const CurrentTablesInput& input);
    CurrentTablesOwner Fork() const; // explicit image-alias graph fork/rebind
    bool available() const;
    const std::string& source_sha256() const { return source_sha256_; }
    uint32_t source_size() const { return layout_.image_size; }
    const CurrentTablesLayout& layout() const { return layout_; }
    const CurrentTablesMapping& mapping() const { return mapping_; }
    uint32_t LoadWord(CurrentTable table, uint32_t index);
    void StoreWord(CurrentTable table, uint32_t index, uint32_t word);
    CurrentTablesAddress Address(CurrentTable table, uint32_t index) const;
    bool ResolvePhysical(uint32_t physical, CurrentTablesAddress& address) const;
    bool ResolveReference(uint32_t tag_b, CurrentTablesAddress& address) const;
    bool ResolvePoolReference(uint32_t tag_5, CurrentTablesAddress& address) const;
    uint32_t reference_base() const;
    CurrentTablesMemory& memory() { return memory_; }
    const CurrentTablesMemory& memory() const { return memory_; }
    std::vector<CurrentTablesEffect> effects() const { return memory_.effects(); }
    std::vector<CurrentTablesEffect> effects_from(uint64_t first) const { return memory_.effects_from(first); }
    const std::vector<uint8_t>& image_bytes() const;
    const std::vector<uint8_t>& image_readable() const;
    const std::vector<uint8_t>& image_writable() const;
    const std::vector<uint8_t>& local_bytes() const;
    std::shared_ptr<CurrentTablesStorageImage> shared_image() const;
    void EndBorrowedImageLifetime(); // native cut; not a PSP release routine
private:
    CurrentTablesMemory memory_;
    CurrentTablesMapping mapping_;
    CurrentTablesLayout layout_;
    std::string source_sha256_;
    bool loaded_ = false;
};
} // namespace dokodemo::recovered
