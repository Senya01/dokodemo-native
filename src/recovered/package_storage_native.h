#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>

namespace dokodemo::recovered {

// Portable lease boundary. A platform adapter OWNS the actual allocation borrow.
// Span must check its owner/generation/serial on EVERY call. Payload is external;
// this component never owns a second vector containing a package or member.
struct PackageStorageSpan {
    uint8_t* bytes = nullptr;
    uint8_t* initialized = nullptr;
    uint64_t size = 0, host_address = 0;
    uint64_t owner = 0, generation = 0, serial = 0;
    uint32_t source_base = 0;
    bool readable = false, writable = false;
};
class PackageStorageLease {
public:
    virtual ~PackageStorageLease() = default;
    virtual bool Span(PackageStorageSpan& out, std::string& error) const = 0;
};

enum class PackageStorageCode {
    Unavailable, InvalidLease, GeometryChanged, Bounds, UnknownBytes, ReadOnly,
    MalformedDirectory, NotFound, NonAscii
};
class PackageStorageError : public std::runtime_error {
public:
    PackageStorageError(PackageStorageCode code, const std::string& message);
    PackageStorageCode code() const { return code_; }
private:
    PackageStorageCode code_;
};
struct PackageViewDescription {
    uint64_t owner = 0, generation = 0, serial = 0;
    uint64_t allocation_bytes = 0, host_address = 0;
    uint64_t offset = 0, size = 0, source_address = 0;
    uint32_t allocation_source_base = 0;
    bool readable = false, writable = false;
};

class PackageStorage;
class PackageView {
public:
    PackageView() = default; // unavailable, never an implicit empty allocation
    PackageViewDescription Describe() const;
    PackageView Subview(uint64_t offset, uint64_t size) const;
    bool SharesBacking(const PackageView& other) const;
    uint8_t ReadU8(uint64_t offset) const;
    uint16_t ReadLE16(uint64_t offset) const;
    uint32_t ReadLE32(uint64_t offset) const;
    // Hash the current initialized view through one validated lease borrow.
    // No payload copy; detects writes through aliases and knowledge loss.
    std::array<uint8_t,32> InitializedDigest() const;
    void WriteU8(uint64_t offset, uint8_t value) const;
    void WriteLE16(uint64_t offset, uint16_t value) const;
    void WriteLE32(uint64_t offset, uint32_t value) const;
    void Reset();
private:
    friend class PackageStorage;
    struct State;
    std::shared_ptr<const State> state_;
    uint64_t offset_ = 0, size_ = 0;
    PackageStorageSpan Access() const;
    uint32_t ReadWord(uint64_t offset, uint32_t width) const;
    void WriteWord(uint64_t offset, uint32_t width, uint32_t value) const;
};
class PackageStorage {
public:
    // logical_bytes excludes rounded disk padding; the lease still owns the
    // whole requested allocation. Unknown bytes remain unknown until written.
    static PackageView Attach(std::shared_ptr<const PackageStorageLease> lease,
                              uint64_t logical_bytes);
};

struct BpDirectoryHeader {
    uint32_t modulus = 0, slots = 0, names_bytes = 0, payload_base = 0;
    uint64_t directory_bytes = 0;
};
struct BpDirectoryEntry {
    uint32_t slot = 0, name_offset = 0, link = 0;
    uint64_t offset = 0, size = 0;
    bool inactive = false;
    std::string name; // directory metadata, never a copied member payload
};

// 64710 ASCII toupper + uint32 hash*31; locale and non-ASCII are outside this
// finite source-derived contract. Native bounds/cycle checks are adaptations.
uint32_t BpAsciiUpperHash64710(const std::string& name);
uint32_t BpRoundedRead64E20(uint64_t logical_bytes);

class BpDirectory {
public:
    BpDirectory() = default;
    // prefix may contain only the root directory, while container_bytes is the
    // actual DATA.BP file extent. No whole archive mapping/read is required.
    static BpDirectory Parse(PackageView prefix, uint64_t container_bytes);
    BpDirectoryHeader Header() const;
    BpDirectoryEntry Resolve(const std::string& name) const;
    // Valid only when the actual member lies inside prefix's package view.
    // Root file entries must instead be read into a real platform allocation.
    PackageView Open(const std::string& name) const;
    void Reset();
private:
    PackageView prefix_;
    uint64_t container_bytes_ = 0;
    BpDirectoryHeader ReadHeader() const;
    BpDirectoryEntry ReadEntry(const BpDirectoryHeader& header, uint32_t slot) const;
    BpDirectoryHeader Validate() const;
};
} // namespace dokodemo::recovered
