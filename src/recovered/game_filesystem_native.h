#pragma once
#include "package_storage_native.h"
#include <array>
#include <memory>

namespace dokodemo::recovered {
enum class GameFsCode { Ok, Unavailable, MissingFile, IoFailure, ShortRead, MalformedDirectory, Bounds, NotFound, Busy };
struct GameFsStatus {
    GameFsCode code = GameFsCode::Ok;
    uint32_t native_error = 0;
    std::string detail;
    explicit operator bool() const { return code == GameFsCode::Ok; }
};
struct GameFileIoSnapshot {
    bool file_live = false;
    uint64_t file_bytes = 0, transferred_bytes = 0;
    uint32_t read_calls = 0, live_allocations = 0;
};
// Platform source owns its open file and each direct read allocation. Views
// borrow checked leases; Close is called only after all reader threads join.
class GameFileSource {
public:
    virtual ~GameFileSource() = default;
    virtual GameFsStatus Open() = 0;
    virtual GameFsStatus Read(uint64_t offset, uint32_t bytes, PackageView& out) = 0;
    virtual GameFsStatus Close() = 0;
    virtual GameFileIoSnapshot Snapshot() const = 0;
};
struct GameFilesystemSnapshot {
    bool mounted = false, closed = false;
    GameFsStatus status;
    GameFileIoSnapshot io;
    uint64_t directory_bytes = 0;
    uint32_t directory_slots = 0, child_capacity = 128, loaded_members = 0;
};
class GameFilesystem final {
public:
    // Readable nonmatching3F0/21A0/2200: construct first, publish only after
    // actual file open/extent/read and checked root-directory parse succeed.
    GameFsStatus Mount(std::unique_ptr<GameFileSource> source);
    GameFsStatus Lookup(const std::string& name, BpDirectoryEntry& out);
    // This performs real bounded distant I/O. BpDirectory::Open(prefix) is
    // deliberately not used to manufacture an unavailable member payload.
    GameFsStatus LoadMember(const std::string& name, PackageView& out);
    GameFilesystemSnapshot Snapshot() const { return state_; }
    GameFsStatus Close();
private:
    GameFsStatus Record(GameFsStatus status);
    std::unique_ptr<GameFileSource> source_;
    PackageView root_prefix_;
    BpDirectory directory_;
    std::array<PackageView, 128> children_{};
    GameFilesystemSnapshot state_;
};
const char* GameFsCodeName(GameFsCode code);
} // namespace dokodemo::recovered
