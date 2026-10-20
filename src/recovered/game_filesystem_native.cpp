#include "game_filesystem_native.h"
#include <limits>
#include <utility>

namespace dokodemo::recovered {
namespace {
GameFsStatus translated(const PackageStorageError& error) {
    const auto code = error.code() == PackageStorageCode::NotFound ? GameFsCode::NotFound
        : error.code() == PackageStorageCode::Bounds ? GameFsCode::Bounds
        : error.code() == PackageStorageCode::Unavailable ? GameFsCode::Unavailable
        : GameFsCode::MalformedDirectory;
    return {code, 0, error.what()};
}
}
GameFsStatus GameFilesystem::Record(GameFsStatus status) {
    if (source_) state_.io = source_->Snapshot();
    state_.status = status; return status;
}
GameFsStatus GameFilesystem::Mount(std::unique_ptr<GameFileSource> source) {
    if (source_ || state_.mounted) return Record({GameFsCode::Busy, 0, "filesystem owner already acquired"});
    state_ = {}; source_ = std::move(source);
    if (!source_) return Record({GameFsCode::Unavailable, 0, "missing file source"});
    auto status = source_->Open();
    if (!status) return Record(status);
    const auto extent = source_->Snapshot().file_bytes;
    if (extent < 16) return Record({GameFsCode::MalformedDirectory, 0, "DATA.BP header truncated"});
    PackageView header;
    status = source_->Read(0, 16, header);
    if (!status) return Record(status);
    try {
        const auto modulus = header.ReadLE32(0), slots = header.ReadLE32(4);
        const auto names = header.ReadLE32(8), payload = header.ReadLE32(12);
        const uint64_t bytes = 16 + uint64_t(slots) * 16 + names;
        // Native allocation cap, not a recovered PSP format limit.
        if (!modulus || modulus > slots || bytes > 8 * 1024 * 1024 || bytes > payload || payload > extent)
            return Record({GameFsCode::MalformedDirectory, 0, "invalid/capped DATA.BP directory geometry"});
        PackageView prefix;
        status = source_->Read(0, uint32_t(bytes), prefix);
        if (!status) return Record(status);
        auto parsed = BpDirectory::Parse(prefix, extent);
        root_prefix_ = std::move(prefix); directory_ = std::move(parsed);
        state_.directory_bytes = bytes; state_.directory_slots = slots;
        state_.mounted = true; return Record({});
    } catch (const PackageStorageError& error) { return Record(translated(error)); }
}
GameFsStatus GameFilesystem::Lookup(const std::string& name, BpDirectoryEntry& out) {
    if (!state_.mounted) return Record({GameFsCode::Unavailable, 0, "DATA.BP not mounted"});
    try { out = directory_.Resolve(name); return Record({}); }
    catch (const PackageStorageError& error) { return Record(translated(error)); }
}
GameFsStatus GameFilesystem::LoadMember(const std::string& name, PackageView& out) {
    BpDirectoryEntry entry;
    auto status = Lookup(name, entry);
    if (!status) return status;
    if (!entry.size || entry.size > 64 * 1024 * 1024)
        return Record({GameFsCode::Bounds, 0, "member outside native nonempty64MiB read domain"});
    if (state_.loaded_members == children_.size())
        return Record({GameFsCode::Busy, 0, "128 native member owner slots occupied"});
    PackageView member;
    status = source_->Read(entry.offset, uint32_t(entry.size), member);
    if (!status) return Record(status);
    children_[state_.loaded_members++] = member;
    out = std::move(member); return Record({});
}
GameFsStatus GameFilesystem::Close() {
    // Consumers have stopped. Drop directory/child aliases before platform
    // leases and file. Failed platform retirement retains source_ for retry.
    for (auto& child : children_) child.Reset();
    directory_.Reset(); root_prefix_.Reset(); state_.mounted = false;
    if (source_) {
        auto status = source_->Close();
        if (!status) return Record(status);
        state_.io = source_->Snapshot(); source_.reset();
    }
    state_.closed = true; state_.loaded_members = 0; return Record({});
}
const char* GameFsCodeName(GameFsCode code) {
    switch (code) {
    case GameFsCode::Ok: return "ok";
    case GameFsCode::Unavailable: return "unavailable";
    case GameFsCode::MissingFile: return "missing-file";
    case GameFsCode::IoFailure: return "io-failure";
    case GameFsCode::ShortRead: return "short-read";
    case GameFsCode::MalformedDirectory: return "malformed-directory";
    case GameFsCode::Bounds: return "bounds";
    case GameFsCode::NotFound: return "not-found";
    case GameFsCode::Busy: return "busy";
    }
    return "invalid";
}
} // namespace dokodemo::recovered
