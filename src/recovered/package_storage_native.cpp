#include "package_storage_native.h"
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace dokodemo::recovered {
namespace {
[[noreturn]] void fail(PackageStorageCode code, const std::string& message) {
    throw PackageStorageError(code, message);
}
void check(bool condition, PackageStorageCode code, const char* message) {
    if (!condition) fail(code, message);
}
void range(uint64_t offset, uint64_t size, uint64_t limit) {
    check(offset <= limit && size <= limit - offset, PackageStorageCode::Bounds,
          "BP view range exceeds its logical member");
}
void geometry(const PackageStorageSpan& span) {
    check(span.bytes && span.initialized && span.size && span.owner &&
          span.generation && span.serial && span.host_address,
          PackageStorageCode::InvalidLease, "BP lease needs real payload, mask and identity");
    check(span.size <= std::numeric_limits<std::size_t>::max() &&
          span.size <= (uint64_t{1} << 32) - span.source_base,
          PackageStorageCode::Bounds, "BP allocation does not fit source32 address space");
    check(static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(span.bytes)) == span.host_address,
          PackageStorageCode::InvalidLease, "BP host address differs from direct payload");
}
PackageStorageSpan acquire(const std::shared_ptr<const PackageStorageLease>& lease) {
    check(static_cast<bool>(lease), PackageStorageCode::Unavailable, "BP view has no owning lease");
    PackageStorageSpan span;
    std::string error;
    if (!lease->Span(span, error))
        fail(PackageStorageCode::Unavailable, "BP backing lease unavailable: " + error);
    geometry(span);
    return span;
}
uint8_t upper(uint8_t value) {
    check(value != 0 && value < 128, PackageStorageCode::NonAscii,
          "BP finite name contract requires non-NUL ASCII");
    return value >= 'a' && value <= 'z' ? static_cast<uint8_t>(value - ('a' - 'A')) : value;
}
bool same_name(const std::string& query, const std::string& stored) {
    const std::size_t skip = !stored.empty() && stored.front() == '/' ? 1u : 0u;
    if (query.size() != stored.size() - skip) return false;
    for (std::size_t i = 0; i < query.size(); ++i)
        if (upper(static_cast<uint8_t>(query[i])) != upper(static_cast<uint8_t>(stored[i + skip]))) return false;
    return true;
}
}

PackageStorageError::PackageStorageError(PackageStorageCode code, const std::string& message)
    : std::runtime_error(message), code_(code) {}

struct PackageView::State {
    std::shared_ptr<const PackageStorageLease> lease;
    uint64_t owner = 0, generation = 0, serial = 0, size = 0, host_address = 0;
    uint32_t source_base = 0;
};
PackageStorageSpan PackageView::Access() const {
    check(static_cast<bool>(state_), PackageStorageCode::Unavailable, "BP view is unavailable");
    auto span = acquire(state_->lease);
    check(span.owner == state_->owner && span.generation == state_->generation &&
          span.serial == state_->serial && span.size == state_->size &&
          span.host_address == state_->host_address && span.source_base == state_->source_base,
          PackageStorageCode::GeometryChanged, "BP backing identity/generation/geometry changed");
    range(offset_, size_, span.size);
    return span;
}
PackageView PackageStorage::Attach(std::shared_ptr<const PackageStorageLease> lease,
                                   uint64_t logical_bytes) {
    const auto span = acquire(lease);
    check(logical_bytes > 0, PackageStorageCode::Bounds, "BP logical storage must be nonempty");
    range(0, logical_bytes, span.size);
    for (uint64_t i = 0; i < span.size; ++i)
        check(span.initialized[static_cast<std::size_t>(i)] <= 1, PackageStorageCode::InvalidLease,
              "BP initialized mask must contain0/1");
    auto state = std::make_shared<PackageView::State>();
    state->lease = std::move(lease); state->owner = span.owner;
    state->generation = span.generation; state->serial = span.serial;
    state->size = span.size; state->host_address = span.host_address; state->source_base = span.source_base;
    PackageView view; view.state_ = std::move(state); view.size_ = logical_bytes;
    return view;
}
PackageViewDescription PackageView::Describe() const {
    const auto span = Access();
    return {span.owner, span.generation, span.serial, span.size, span.host_address,
            offset_, size_, uint64_t(span.source_base) + offset_, span.source_base,
            span.readable, span.writable};
}
PackageView PackageView::Subview(uint64_t offset, uint64_t size) const {
    Access(); range(offset, size, size_);
    PackageView out = *this; out.offset_ += offset; out.size_ = size;
    return out;
}
bool PackageView::SharesBacking(const PackageView& other) const {
    const auto a = Access(), b = other.Access();
    return a.owner == b.owner && a.generation == b.generation && a.serial == b.serial &&
           a.host_address == b.host_address && a.source_base == b.source_base && a.size == b.size;
}
uint32_t PackageView::ReadWord(uint64_t offset, uint32_t width) const {
    const auto span = Access(); range(offset, width, size_);
    check(span.readable, PackageStorageCode::Unavailable, "BP backing is not readable");
    const auto at = static_cast<std::size_t>(offset_ + offset);
    uint32_t result = 0;
    for (uint32_t i = 0; i < width; ++i) {
        check(span.initialized[at + i] <= 1, PackageStorageCode::InvalidLease, "BP invalid knowledge bit");
        check(span.initialized[at + i] != 0, PackageStorageCode::UnknownBytes, "BP uninitialized input byte");
        result |= uint32_t(span.bytes[at + i]) << (8 * i);
    }
    return result;
}
void PackageView::WriteWord(uint64_t offset, uint32_t width, uint32_t value) const {
    const auto span = Access(); range(offset, width, size_);
    check(span.writable, PackageStorageCode::ReadOnly, "BP backing is not writable");
    const auto at = static_cast<std::size_t>(offset_ + offset);
    for (uint32_t i = 0; i < width; ++i)
        check(span.initialized[at + i] <= 1, PackageStorageCode::InvalidLease, "BP invalid knowledge bit");
    for (uint32_t i = 0; i < width; ++i) {
        span.bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
        span.initialized[at + i] = 1;
    }
}
uint8_t PackageView::ReadU8(uint64_t offset) const { return static_cast<uint8_t>(ReadWord(offset, 1)); }
uint16_t PackageView::ReadLE16(uint64_t offset) const { return static_cast<uint16_t>(ReadWord(offset, 2)); }
uint32_t PackageView::ReadLE32(uint64_t offset) const { return ReadWord(offset, 4); }
void PackageView::WriteU8(uint64_t offset, uint8_t value) const { WriteWord(offset, 1, value); }
void PackageView::WriteLE16(uint64_t offset, uint16_t value) const { WriteWord(offset, 2, value); }
void PackageView::WriteLE32(uint64_t offset, uint32_t value) const { WriteWord(offset, 4, value); }
void PackageView::Reset() { state_.reset(); offset_ = 0; size_ = 0; }

uint32_t BpAsciiUpperHash64710(const std::string& name) {
    check(!name.empty(), PackageStorageCode::NonAscii, "BP query name must be nonempty");
    uint32_t hash = 0;
    for (const unsigned char c : name) hash = hash * 31u + upper(c);
    return hash;
}
uint32_t BpRoundedRead64E20(uint64_t logical_bytes) {
    check(logical_bytes > 0 && logical_bytes <= uint64_t(INT32_MAX) - 2047u,
          PackageStorageCode::Bounds, "BP rounded read outside positive signed source domain");
    return static_cast<uint32_t>((logical_bytes + 2047u) & ~uint64_t{2047});
}
BpDirectoryHeader BpDirectory::ReadHeader() const {
    const auto description = prefix_.Describe();
    check(container_bytes_ > 0, PackageStorageCode::MalformedDirectory, "BP missing container extent");
    BpDirectoryHeader header;
    header.modulus = prefix_.ReadLE32(0); header.slots = prefix_.ReadLE32(4);
    header.names_bytes = prefix_.ReadLE32(8); header.payload_base = prefix_.ReadLE32(12);
    header.directory_bytes = 16u + uint64_t(header.slots) * 16u + header.names_bytes;
    check(header.modulus > 0 && header.modulus <= header.slots,
          PackageStorageCode::MalformedDirectory, "BP invalid modulus/slot count");
    check(header.directory_bytes <= description.size && header.directory_bytes <= header.payload_base &&
          header.payload_base <= container_bytes_, PackageStorageCode::MalformedDirectory,
          "BP directory/names/payload extent invalid");
    return header;
}
BpDirectoryEntry BpDirectory::ReadEntry(const BpDirectoryHeader& h, uint32_t slot) const {
    check(slot < h.slots, PackageStorageCode::MalformedDirectory, "BP slot outside directory");
    const uint64_t at = 16u + uint64_t(slot) * 16u;
    BpDirectoryEntry entry; entry.slot = slot; entry.name_offset = prefix_.ReadLE32(at);
    const uint32_t relative = prefix_.ReadLE32(at + 4);
    entry.size = prefix_.ReadLE32(at + 8); entry.link = prefix_.ReadLE32(at + 12);
    check(entry.link == UINT32_MAX || entry.link < h.slots, PackageStorageCode::MalformedDirectory, "BP link outside directory");
    entry.inactive = entry.name_offset == UINT32_MAX;
    if (entry.inactive) return entry;
    check(entry.name_offset < h.names_bytes, PackageStorageCode::MalformedDirectory, "BP name offset outside names");
    const uint64_t names = 16u + uint64_t(h.slots) * 16u;
    bool terminated = false;
    for (uint64_t i = entry.name_offset; i < h.names_bytes; ++i) {
        const uint8_t c = prefix_.ReadU8(names + i);
        if (c == 0) { terminated = true; break; }
        upper(c); entry.name.push_back(static_cast<char>(c));
    }
    check(terminated && !entry.name.empty(), PackageStorageCode::MalformedDirectory, "BP unterminated/empty name");
    entry.offset = uint64_t(h.payload_base) + relative;
    check(entry.offset <= UINT32_MAX && entry.offset <= container_bytes_ && entry.size <= container_bytes_ - entry.offset &&
          entry.size <= (uint64_t{1} << 32) - entry.offset,
          PackageStorageCode::MalformedDirectory, "BP payload member exceeds container/source32 bound");
    return entry;
}
BpDirectoryHeader BpDirectory::Validate() const {
    const auto header = ReadHeader();
    std::vector<uint32_t> links; links.reserve(header.slots);
    for (uint32_t i = 0; i < header.slots; ++i) links.push_back(ReadEntry(header, i).link);
    // Validate every chain, including an unreachable corrupt cycle. This is a
    // protective native parser check, not an emulated source fault behavior.
    std::vector<uint8_t> color(header.slots, 0);
    for (uint32_t start = 0; start < header.slots; ++start) {
        if (color[start] != 0) continue;
        std::vector<uint32_t> path;
        uint32_t current = start;
        while (current != UINT32_MAX && color[current] == 0) {
            color[current] = 1; path.push_back(current); current = links[current];
        }
        check(current == UINT32_MAX || color[current] != 1, PackageStorageCode::MalformedDirectory, "BP directory link cycle");
        for (const uint32_t slot : path) color[slot] = 2;
    }
    return header;
}
BpDirectory BpDirectory::Parse(PackageView prefix, uint64_t container_bytes) {
    BpDirectory directory; directory.prefix_ = std::move(prefix); directory.container_bytes_ = container_bytes;
    directory.Validate(); return directory;
}
BpDirectoryHeader BpDirectory::Header() const { return Validate(); }
BpDirectoryEntry BpDirectory::Resolve(const std::string& name) const {
    const auto header = Validate();
    uint32_t slot = BpAsciiUpperHash64710(name) % header.modulus;
    for (uint32_t visits = 0; slot != UINT32_MAX && visits < header.slots; ++visits) {
        auto entry = ReadEntry(header, slot);
        if (!entry.inactive && same_name(name, entry.name)) return entry;
        slot = entry.link;
    }
    fail(PackageStorageCode::NotFound, "BP directory member not found: " + name);
}
PackageView BpDirectory::Open(const std::string& name) const {
    const auto entry = Resolve(name);
    return prefix_.Subview(entry.offset, entry.size);
}
void BpDirectory::Reset() { prefix_.Reset(); container_bytes_ = 0; }
} // namespace dokodemo::recovered
