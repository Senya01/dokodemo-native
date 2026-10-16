#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo {
// Isolated Windows adaptation. These are NEVER PSP UIDs, interrupt flags or
// historical PSP addresses. No module BSS/caller/nesting state is installed.
enum class StartupHostCode {
    Ok, Closed, InvalidRequest, UnsupportedAbi, SourceRange, SourceOverlap,
    ForeignHandle, StaleHandle, Borrowed, UnknownBytes, HostFailure,
    UnexpectedGeometry, Timeout, Abandoned, ForeignThread, InvalidToken, Busy, ShortRead
};
struct StartupHostStatus {
    StartupHostCode code = StartupHostCode::Ok;
    uint32_t win32_error = 0, wait_result = 0;
    explicit operator bool() const { return code == StartupHostCode::Ok; }
};
struct StartupNativeBlock {
    uint64_t owner = 0, generation = 0, serial = 0;
    explicit operator bool() const { return owner && generation && serial; }
};
struct StartupPartitionRequest {
    int32_t partition = 2, type = 0;
    std::string name = "block";
    uint32_t bytes = 0x13C8000, source_base = 0;
    uint64_t address_argument = 0;
};
struct StartupNativeDescription {
    StartupHostStatus status;
    StartupNativeBlock handle;
    uint64_t host_address = 0, committed_bytes = 0;
    uint32_t requested_bytes = 0, source_base = 0, page_bytes = 0;
    uint32_t host_state = 0, host_protection = 0;
    uint64_t known_bytes = 0;
    bool live = false, readable = false, writable = false;
};
struct StartupRawBorrowSpan {
    // The payload points DIRECTLY at the one VirtualAlloc allocation.
    // The separate knowledge vector is not another payload buffer.
    uint8_t* bytes = nullptr;
    uint8_t* initialized = nullptr;
    uint64_t size = 0, host_address = 0;
    uint32_t source_base = 0;
    StartupNativeBlock handle;
    bool readable = false, writable = false;
};
class WindowsStartupBackend;
class StartupAllocationBorrow {
public:
    StartupAllocationBorrow() = default;
    StartupHostStatus Span(StartupRawBorrowSpan& out) const;
    void Reset();
private:
    friend class WindowsStartupBackend;
    struct Storage;
    std::shared_ptr<Storage> storage_;
    StartupNativeBlock handle_;
};
struct StartupNativeBytes {
    StartupHostStatus status;
    std::vector<uint8_t> bytes, initialized;
};
struct StartupNativeSerializationToken {
    uint64_t owner = 0, generation = 0, acquisition = 0, thread_lifetime = 0;
    uint32_t cookie = 0, thread = 0;
    bool restores_acquisition = false;
    explicit operator bool() const { return owner && generation && acquisition && cookie; }
};
struct StartupNativeSerializationResult {
    StartupHostStatus status;
    StartupNativeSerializationToken token;
    bool reused_held_acquisition = false;
};
struct StartupHostCall {
    uint64_t ordinal = 0, address = 0, size = 0, result = 0;
    uint32_t thread = 0, win32_error = 0;
    std::string operation;
};
struct StartupNativeFile {
    uint64_t owner = 0, generation = 0, serial = 0;
    explicit operator bool() const { return owner && generation && serial; }
};
struct StartupNativeFileResult {
    StartupHostStatus status;
    StartupNativeFile handle;
    uint64_t host_handle = 0, file_size = 0;
};
struct StartupNativeSeekResult {
    StartupHostStatus status;
    int64_t position = 0;
};
struct StartupNativeReadResult {
    StartupHostStatus status;
    uint64_t file_position = 0, destination_host_address = 0;
    uint32_t requested = 0, transferred = 0;
    bool host_result = false;
};

class WindowsStartupBackend final {
public:
    static std::unique_ptr<WindowsStartupBackend> Create(StartupHostStatus& status);
    ~WindowsStartupBackend();
    WindowsStartupBackend(const WindowsStartupBackend&) = delete;
    WindowsStartupBackend& operator=(const WindowsStartupBackend&) = delete;
    StartupNativeDescription Allocate(const StartupPartitionRequest& request);
    StartupNativeDescription Head(StartupNativeBlock handle);
    StartupHostStatus Free(StartupNativeBlock handle);
    StartupAllocationBorrow Borrow(StartupNativeBlock handle, StartupHostStatus& status);
    StartupNativeBytes Observe(StartupNativeBlock handle, uint32_t offset, uint32_t count);
    StartupNativeBytes ReadKnown(StartupNativeBlock handle, uint32_t offset, uint32_t count);
    StartupHostStatus WriteKnown(StartupNativeBlock handle, uint32_t offset,
                                const std::vector<uint8_t>& bytes);
    StartupNativeFileResult OpenRead(const std::wstring& native_path, uint32_t source_flags = 1, uint32_t source_mode = 0);
    StartupNativeSeekResult Seek64(StartupNativeFile handle, int64_t offset, uint32_t whence);
    StartupNativeReadResult ReadInto(StartupNativeFile file, StartupNativeBlock destination,
                                    uint32_t destination_offset, uint32_t count);
    StartupHostStatus CloseFile(StartupNativeFile handle);
    // CB108 is called at EVERY libc nesting depth; only the outer saved token
    // is restored at C30E0. Nested suspend must NOT recursively lock the mutex.
    // Nested tokens are nonrestoring and can be discarded by that source client.
    // Other original import clients are not covered by this finite adaptation.
    StartupNativeSerializationResult SuspendSerialization(uint32_t timeout_ms = 0);
    StartupHostStatus RestoreSerialization(StartupNativeSerializationToken token);
    StartupHostStatus Close();
    std::vector<StartupHostCall> HostCalls() const;
private:
    struct Impl;
    explicit WindowsStartupBackend(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
const char* StartupHostCodeName(StartupHostCode code);
} // namespace dokodemo
