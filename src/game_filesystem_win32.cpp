#include "game_filesystem_win32.h"
#include <windows.h>
#include <limits>
#include <utility>

namespace dokodemo {
namespace {
using namespace recovered;
GameFsStatus host_failure(StartupHostStatus status, const char* detail) {
    return {status.code == StartupHostCode::ShortRead ? GameFsCode::ShortRead : GameFsCode::IoFailure,
            status.win32_error, detail};
}
struct Buffer final : PackageStorageLease {
    StartupNativeBlock block;
    StartupAllocationBorrow borrow;
    bool active = false;
    bool Span(PackageStorageSpan& out, std::string& error) const override {
        StartupRawBorrowSpan span;
        if (!active || !borrow.Span(span)) { error = "file allocation retired"; return false; }
        out = {span.bytes, span.initialized, span.size, span.host_address,
               span.handle.owner, span.handle.generation, span.handle.serial, span.source_base,
               span.readable, span.writable};
        // Disk stays read-only. Owned RAM remains mutable for later in-place
        // package/KSC producers, without creating a second payload copy.
        return true;
    }
};
class WindowsGameFileSource final : public GameFileSource {
public:
    WindowsGameFileSource(std::shared_ptr<WindowsStartupBackend> backend, std::wstring path,uint32_t coordinate)
        : backend_(std::move(backend)), path_(std::move(path)),next_coordinate_(coordinate) { buffers_.reserve(130); }
    ~WindowsGameFileSource() override { (void)Close(); }
    GameFsStatus Open() override {
        if (!backend_ || file_) return {GameFsCode::Busy, 0, "invalid/already open file owner"};
        const auto opened = backend_->OpenRead(path_);
        if (!opened.status) {
            if (opened.status.win32_error == ERROR_FILE_NOT_FOUND || opened.status.win32_error == ERROR_PATH_NOT_FOUND)
                return {GameFsCode::MissingFile, opened.status.win32_error, "DATA.BP file not found"};
            return host_failure(opened.status, "DATA.BP open failed");
        }
        file_ = opened.handle; state_.file_live = true; state_.file_bytes = opened.file_size; return {};
    }
    GameFsStatus Read(uint64_t offset, uint32_t count, PackageView& out) override {
        if (!file_) return {GameFsCode::Unavailable, 0, "file closed"};
        if (buffers_.size() == 130) return {GameFsCode::Busy, 0, "header/root/128 member allocation slots occupied"};
        if (!count || offset > state_.file_bytes || count > state_.file_bytes - offset || count > 64 * 1024 * 1024
            || next_coordinate_ + count + 4095 > 0xF0000000ull)
            return {GameFsCode::Bounds, 0, "bounded file read exceeds actual extent/native cap"};
        const auto sought = backend_->Seek64(file_, int64_t(offset), 0);
        if (!sought.status) return host_failure(sought.status, "DATA.BP seek failed");
        if (sought.position != int64_t(offset)) return {GameFsCode::IoFailure, 0, "seek position mismatch"};
        auto buffer = std::make_shared<Buffer>();
        StartupPartitionRequest request; request.bytes = count; request.source_base = uint32_t(next_coordinate_);
        const auto allocation = backend_->Allocate(request);
        if (!allocation.status) return host_failure(allocation.status, "file buffer allocation failed");
        buffer->block = allocation.handle;
        // Reserved before file acquisition: this shared_ptr insertion cannot
        // allocate. Retain ownership before read/lease/publication can fail.
        buffers_.push_back(buffer);
        ++state_.live_allocations;
        next_coordinate_ += (uint64_t(count) + 4095) & ~uint64_t(4095);
        const auto read = backend_->ReadInto(file_, buffer->block, 0, count);
        ++state_.read_calls; state_.transferred_bytes += read.transferred;
        if (!read.status) return host_failure(read.status, "DATA.BP read failed/short");
        StartupHostStatus status;
        buffer->borrow = backend_->Borrow(buffer->block, status);
        if (!status) return host_failure(status, "file buffer borrow failed");
        buffer->active = true;
        out = PackageStorage::Attach(buffer, count); return {};
    }
    GameFsStatus Close() override {
        // Caller has joined readers. Stale external PackageViews reject Span,
        // rather than observing freed storage or fabricating an empty payload.
        for (auto at = buffers_.rbegin(); at != buffers_.rend(); ++at) {
            auto& buffer = *at;
            buffer->active = false; buffer->borrow.Reset();
            if (!buffer->block) continue;
            const auto status = backend_->Free(buffer->block);
            if (!status) return host_failure(status, "file allocation retirement failed");
            buffer->block = {}; --state_.live_allocations;
        }
        if (file_) {
            const auto status = backend_->CloseFile(file_);
            if (!status) return host_failure(status, "DATA.BP close failed");
            file_ = {}; state_.file_live = false;
        }
        buffers_.clear(); return {};
    }
    GameFileIoSnapshot Snapshot() const override { return state_; }
private:
    std::shared_ptr<WindowsStartupBackend> backend_;
    std::wstring path_;
    StartupNativeFile file_;
    std::vector<std::shared_ptr<Buffer>> buffers_;
    GameFileIoSnapshot state_;
    uint64_t next_coordinate_ = 0x74000000; // native logical coordinates, never PSP placement
};
}
std::unique_ptr<recovered::GameFileSource> CreateWindowsGameFileSource(
    std::shared_ptr<WindowsStartupBackend> backend, std::wstring path,uint32_t coordinate) {
    return std::make_unique<WindowsGameFileSource>(std::move(backend), std::move(path),coordinate);
}
} // namespace dokodemo
