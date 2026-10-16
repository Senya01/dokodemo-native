#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "startup_memory_backend_win32.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

namespace dokodemo {
namespace {
std::atomic<uint64_t> next_backend_owner{1};
std::atomic<uint64_t> next_thread_lifetime{1};
// A new native thread gets a new identity even if Windows reuses its DWORD ID.
// uint64 counter exhaustion and concurrent owner destruction are out of scope.
thread_local const uint64_t this_thread_lifetime = next_thread_lifetime.fetch_add(1);
StartupHostStatus Error(StartupHostCode code, DWORD error = 0, DWORD wait = 0) {
    return {code, error, wait};
}
bool Same(StartupNativeBlock a, StartupNativeBlock b) {
    return a.owner == b.owner && a.generation == b.generation && a.serial == b.serial;
}
uint64_t Address(const void* p) { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p)); }
}

struct StartupAllocationBorrow::Storage {
    StartupNativeBlock handle;
    void* allocation = nullptr;
    uint64_t committed = 0;
    uint32_t requested = 0, source = 0, page = 0;
    std::vector<uint8_t> initialized;
    bool live = true;
    ~Storage() { if (allocation) (void)VirtualFree(allocation, 0, MEM_RELEASE); }
};
StartupHostStatus StartupAllocationBorrow::Span(StartupRawBorrowSpan& out) const {
    out = {};
    if (!storage_ || !storage_->live || !Same(handle_, storage_->handle))
        return Error(StartupHostCode::StaleHandle);
    out.bytes = static_cast<uint8_t*>(storage_->allocation);
    out.initialized = storage_->initialized.data();
    out.size = storage_->requested;
    out.host_address = Address(storage_->allocation);
    out.source_base = storage_->source;
    out.handle = handle_;
    out.readable = true;
    out.writable = true;
    return {};
}
void StartupAllocationBorrow::Reset() { storage_.reset(); handle_ = {}; }

struct WindowsStartupBackend::Impl {
    mutable std::mutex metadata;
    uint64_t owner = next_backend_owner.fetch_add(1), generation = 1, serial = 0;
    uint64_t acquisition = 0, serialization_generation = 1, holding_lifetime = 0;
    uint32_t cookie = 0, holding_thread = 0, waiting = 0;
    HANDLE serialization = nullptr;
    bool closed = false, poisoned = false;
    std::vector<std::shared_ptr<StartupAllocationBorrow::Storage>> blocks;
    struct File { StartupNativeFile handle; HANDLE native = nullptr; };
    std::vector<File> files;
    std::vector<StartupNativeSerializationToken> tokens;
    std::vector<StartupHostCall> calls;
    void Log(const char* op, const void* address, uint64_t size, uint64_t result, DWORD error = 0) {
        calls.push_back({calls.size(), Address(address), size, result, GetCurrentThreadId(), error, op});
    }
    StartupHostStatus Find(StartupNativeBlock handle, size_t& index) const {
        if (closed) return Error(StartupHostCode::Closed);
        if (handle.owner != owner) return Error(StartupHostCode::ForeignHandle);
        if (handle.generation != generation || !handle.serial) return Error(StartupHostCode::StaleHandle);
        for (size_t i = 0; i < blocks.size(); ++i) if (Same(blocks[i]->handle, handle)) { index = i; return {}; }
        return Error(StartupHostCode::StaleHandle);
    }
    StartupHostStatus FindFile(StartupNativeFile handle, size_t& index) const {
        if (closed) return Error(StartupHostCode::Closed);
        if (handle.owner != owner) return Error(StartupHostCode::ForeignHandle);
        if (handle.generation != generation || !handle.serial) return Error(StartupHostCode::StaleHandle);
        for (size_t i = 0; i < files.size(); ++i) if (files[i].handle.serial == handle.serial) { index = i; return {}; }
        return Error(StartupHostCode::StaleHandle);
    }
    StartupNativeDescription Describe(const std::shared_ptr<StartupAllocationBorrow::Storage>& b) {
        StartupNativeDescription out;
        MEMORY_BASIC_INFORMATION info{};
        const SIZE_T result = VirtualQuery(b->allocation, &info, sizeof(info));
        const DWORD error = result ? 0 : GetLastError();
        Log("VirtualQuery", b->allocation, sizeof(info), result, error);
        if (!result) { out.status = Error(StartupHostCode::HostFailure, error); return out; }
        if (info.AllocationBase != b->allocation || info.BaseAddress != b->allocation ||
            info.State != MEM_COMMIT || info.Protect != PAGE_READWRITE || info.RegionSize < b->committed) {
            out.status = Error(StartupHostCode::UnexpectedGeometry); return out;
        }
        out.handle = b->handle;
        out.host_address = Address(b->allocation);
        out.committed_bytes = b->committed;
        out.requested_bytes = b->requested;
        out.source_base = b->source;
        out.page_bytes = b->page;
        out.host_state = info.State;
        out.host_protection = info.Protect;
        out.known_bytes = static_cast<uint64_t>(std::count(b->initialized.begin(), b->initialized.end(), uint8_t(1)));
        out.live = b->live; out.readable = true; out.writable = true;
        return out;
    }
};
WindowsStartupBackend::WindowsStartupBackend(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
std::unique_ptr<WindowsStartupBackend> WindowsStartupBackend::Create(StartupHostStatus& status) {
    auto impl = std::make_unique<Impl>();
    impl->serialization = CreateMutexW(nullptr, FALSE, nullptr);
    const DWORD error = impl->serialization ? 0 : GetLastError();
    impl->Log("CreateMutexW", nullptr, 0, Address(impl->serialization), error);
    if (!impl->serialization) { status = Error(StartupHostCode::HostFailure, error); return {}; }
    status = {};
    return std::unique_ptr<WindowsStartupBackend>(new WindowsStartupBackend(std::move(impl)));
}
WindowsStartupBackend::~WindowsStartupBackend() {
    // Destruction must be quiescent. Explicit Close reports busy borrows/tokens.
    // If a caller destroys an owner with a borrow, invalidate the lease but let
    // its shared storage retain the host reservation until the borrow dies.
    if (!impl_) return;
    (void)Close();
    for (auto& block : impl_->blocks) block->live = false;
    for (const auto& file : impl_->files) (void)CloseHandle(file.native);
    if (impl_->serialization) {
        if (impl_->holding_thread == GetCurrentThreadId() && impl_->holding_lifetime == this_thread_lifetime)
            (void)ReleaseMutex(impl_->serialization);
        (void)CloseHandle(impl_->serialization);
    }
}
StartupNativeDescription WindowsStartupBackend::Allocate(const StartupPartitionRequest& request) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata);
    StartupNativeDescription out;
    if (s.closed) { out.status = Error(StartupHostCode::Closed); return out; }
    if (request.partition != 2 || request.type != 0 || request.name != "block" || request.address_argument != 0) {
        out.status = Error(StartupHostCode::UnsupportedAbi); return out;
    }
    if (!request.bytes || !request.source_base || s.serial == std::numeric_limits<uint64_t>::max()) {
        out.status = Error(StartupHostCode::InvalidRequest); return out;
    }
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    s.Log("GetSystemInfo", nullptr, 0, info.dwPageSize);
    const uint64_t page = info.dwPageSize;
    if (!page) { out.status = Error(StartupHostCode::UnexpectedGeometry); return out; }
    const uint64_t committed = ((uint64_t(request.bytes) + page - 1) / page) * page;
    if (request.source_base % page || uint64_t(request.source_base) + committed > 0x100000000ULL) {
        out.status = Error(StartupHostCode::SourceRange); return out;
    }
    for (const auto& b : s.blocks) if (uint64_t(request.source_base) < uint64_t(b->source) + b->committed &&
                                       uint64_t(b->source) < uint64_t(request.source_base) + committed) {
        out.status = Error(StartupHostCode::SourceOverlap); return out;
    }
    auto b = std::make_shared<StartupAllocationBorrow::Storage>();
    b->initialized.resize(request.bytes, 0); // ONLY knowledge; payload is VirtualAlloc.
    b->allocation = VirtualAlloc(nullptr, request.bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    const DWORD error = b->allocation ? 0 : GetLastError();
    s.Log("VirtualAlloc", nullptr, request.bytes, Address(b->allocation), error);
    if (!b->allocation) { out.status = Error(StartupHostCode::HostFailure, error); return out; }
    b->handle = {s.owner, s.generation, ++s.serial};
    b->committed = committed; b->requested = request.bytes; b->source = request.source_base; b->page = info.dwPageSize;
    out = s.Describe(b);
    if (!out.status) {
        const BOOL released = VirtualFree(b->allocation, 0, MEM_RELEASE);
        const DWORD free_error = released ? 0 : GetLastError();
        s.Log("VirtualFreeRejectedGeometry", b->allocation, 0, static_cast<uint64_t>(released), free_error);
        if (released) b->allocation = nullptr;
        return out;
    }
    s.blocks.push_back(std::move(b));
    return out;
}
StartupNativeDescription WindowsStartupBackend::Head(StartupNativeBlock handle) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata);
    size_t i = 0; StartupNativeDescription out; out.status = s.Find(handle, i);
    return out.status ? s.Describe(s.blocks[i]) : out;
}
StartupHostStatus WindowsStartupBackend::Free(StartupNativeBlock handle) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata);
    size_t i = 0; auto status = s.Find(handle, i); if (!status) return status;
    auto& b = s.blocks[i];
    if (b.use_count() != 1) return Error(StartupHostCode::Borrowed);
    const BOOL result = VirtualFree(b->allocation, 0, MEM_RELEASE);
    const DWORD error = result ? 0 : GetLastError();
    s.Log("VirtualFree", b->allocation, 0, static_cast<uint64_t>(result), error);
    if (!result) return Error(StartupHostCode::HostFailure, error);
    b->allocation = nullptr; b->live = false;
    s.blocks.erase(s.blocks.begin() + static_cast<std::ptrdiff_t>(i));
    return {};
}
StartupAllocationBorrow WindowsStartupBackend::Borrow(StartupNativeBlock handle, StartupHostStatus& status) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata);
    size_t i = 0; StartupAllocationBorrow out; status = s.Find(handle, i);
    if (status) { out.storage_ = s.blocks[i]; out.handle_ = handle; }
    return out;
}
StartupNativeBytes WindowsStartupBackend::Observe(StartupNativeBlock handle, uint32_t offset, uint32_t count) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata);
    size_t i = 0; StartupNativeBytes out; out.status = s.Find(handle, i); if (!out.status) return out;
    const auto& b = s.blocks[i];
    if (!count || uint64_t(offset) + count > b->requested) { out.status = Error(StartupHostCode::SourceRange); return out; }
    const auto* bytes = static_cast<const uint8_t*>(b->allocation);
    out.bytes.assign(bytes + offset, bytes + offset + count);
    out.initialized.assign(b->initialized.begin() + offset, b->initialized.begin() + offset + count);
    return out;
}
StartupNativeBytes WindowsStartupBackend::ReadKnown(StartupNativeBlock handle, uint32_t offset, uint32_t count) {
    auto out = Observe(handle, offset, count);
    if (out.status && std::any_of(out.initialized.begin(), out.initialized.end(), [](uint8_t x) { return x != 1; })) {
        out.status = Error(StartupHostCode::UnknownBytes); out.bytes.clear();
    }
    return out;
}
StartupHostStatus WindowsStartupBackend::WriteKnown(StartupNativeBlock handle, uint32_t offset, const std::vector<uint8_t>& bytes) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata);
    size_t i = 0; auto status = s.Find(handle, i); if (!status) return status;
    const auto& b = s.blocks[i];
    if (bytes.empty() || uint64_t(offset) + bytes.size() > b->requested) return Error(StartupHostCode::SourceRange);
    std::memcpy(static_cast<uint8_t*>(b->allocation) + offset, bytes.data(), bytes.size());
    std::fill(b->initialized.begin() + offset, b->initialized.begin() + offset + bytes.size(), uint8_t(1));
    return {};
}
StartupNativeFileResult WindowsStartupBackend::OpenRead(const std::wstring& path, uint32_t flags, uint32_t mode) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata); StartupNativeFileResult out;
    if (s.closed) { out.status = Error(StartupHostCode::Closed); return out; }
    if (flags != 1 || mode != 0) { out.status = Error(StartupHostCode::UnsupportedAbi); return out; }
    if (path.empty() || path.find(L'\0') != std::wstring::npos || s.serial == std::numeric_limits<uint64_t>::max()) {
        out.status = Error(StartupHostCode::InvalidRequest); return out;
    }
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD error = file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    s.Log("CreateFileW-read", nullptr, 0, Address(file), error);
    if (file == INVALID_HANDLE_VALUE) { out.status = Error(StartupHostCode::HostFailure, error); return out; }
    LARGE_INTEGER size{};
    const BOOL result = GetFileSizeEx(file, &size);
    const DWORD size_error = result ? 0 : GetLastError();
    s.Log("GetFileSizeEx", file, 0, result ? static_cast<uint64_t>(size.QuadPart) : 0, size_error);
    if (!result || size.QuadPart < 0) {
        const BOOL closed = CloseHandle(file);
        const DWORD close_error = closed ? 0 : GetLastError();
        s.Log("CloseRejectedFile", file, 0, static_cast<uint64_t>(closed), close_error);
        out.status = Error(StartupHostCode::HostFailure, size_error); return out;
    }
    out.handle = {s.owner, s.generation, ++s.serial};
    out.host_handle = Address(file); out.file_size = static_cast<uint64_t>(size.QuadPart);
    s.files.push_back({out.handle, file}); return out;
}
StartupNativeSeekResult WindowsStartupBackend::Seek64(StartupNativeFile handle, int64_t offset, uint32_t whence) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata); StartupNativeSeekResult out;
    size_t i = 0; out.status = s.FindFile(handle, i); if (!out.status) return out;
    if (whence > 2) { out.status = Error(StartupHostCode::UnsupportedAbi); return out; }
    LARGE_INTEGER move{}, position{}; move.QuadPart = offset;
    const BOOL result = SetFilePointerEx(s.files[i].native, move, &position, whence);
    const DWORD error = result ? 0 : GetLastError();
    s.Log("SetFilePointerEx", s.files[i].native, static_cast<uint64_t>(offset), result ? static_cast<uint64_t>(position.QuadPart) : 0, error);
    if (!result) { out.status = Error(StartupHostCode::HostFailure, error); return out; }
    out.position = position.QuadPart; return out;
}
StartupNativeReadResult WindowsStartupBackend::ReadInto(StartupNativeFile file, StartupNativeBlock destination, uint32_t offset, uint32_t count) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata); StartupNativeReadResult out; out.requested = count;
    size_t i = 0, j = 0; out.status = s.FindFile(file, i); if (!out.status) return out;
    out.status = s.Find(destination, j); if (!out.status) return out;
    auto& b = s.blocks[j];
    if (!count || uint64_t(offset) + count > b->requested) { out.status = Error(StartupHostCode::SourceRange); return out; }
    LARGE_INTEGER zero{}, position{};
    const BOOL queried = SetFilePointerEx(s.files[i].native, zero, &position, FILE_CURRENT);
    const DWORD query_error = queried ? 0 : GetLastError();
    s.Log("ReadPosition", s.files[i].native, 0, queried ? static_cast<uint64_t>(position.QuadPart) : 0, query_error);
    if (!queried) { out.status = Error(StartupHostCode::HostFailure, query_error); return out; }
    out.file_position = static_cast<uint64_t>(position.QuadPart);
    void* target = static_cast<uint8_t*>(b->allocation) + offset;
    out.destination_host_address = Address(target);
    DWORD transferred = 0;
    const BOOL result = ReadFile(s.files[i].native, target, count, &transferred, nullptr);
    const DWORD error = result ? 0 : GetLastError();
    out.host_result = result != FALSE; out.transferred = transferred;
    s.Log("ReadFile-transferred", target, count, transferred, error);
    if (transferred > count) { out.status = Error(StartupHostCode::UnexpectedGeometry); return out; }
    std::fill(b->initialized.begin() + offset, b->initialized.begin() + offset + transferred, uint8_t(1));
    if (!result) out.status = Error(StartupHostCode::HostFailure, error);
    else if (transferred != count) out.status = Error(StartupHostCode::ShortRead);
    return out;
}
StartupHostStatus WindowsStartupBackend::CloseFile(StartupNativeFile handle) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata);
    size_t i = 0; auto status = s.FindFile(handle, i); if (!status) return status;
    const HANDLE file = s.files[i].native; const BOOL result = CloseHandle(file);
    const DWORD error = result ? 0 : GetLastError(); s.Log("CloseHandleFile", file, 0, static_cast<uint64_t>(result), error);
    if (!result) return Error(StartupHostCode::HostFailure, error);
    s.files.erase(s.files.begin() + static_cast<std::ptrdiff_t>(i)); return {};
}
StartupNativeSerializationResult WindowsStartupBackend::SuspendSerialization(uint32_t timeout_ms) {
    auto& s = *impl_; StartupNativeSerializationResult out;
    const DWORD thread = GetCurrentThreadId();
    {
        std::lock_guard<std::mutex> guard(s.metadata);
        if (s.closed) { out.status = Error(StartupHostCode::Closed); return out; }
        if (s.poisoned) { out.status = Error(StartupHostCode::Abandoned); return out; }
        if (s.cookie == std::numeric_limits<uint32_t>::max()) { out.status = Error(StartupHostCode::InvalidToken); return out; }
        if (s.holding_thread == thread && s.holding_lifetime == this_thread_lifetime) {
            out.token = {s.owner, s.serialization_generation, s.acquisition, this_thread_lifetime, ++s.cookie, thread, false};
            s.tokens.push_back(out.token); out.reused_held_acquisition = true; return out;
        }
        ++s.waiting;
    }
    const DWORD result = WaitForSingleObject(s.serialization, timeout_ms);
    const DWORD error = result == WAIT_FAILED ? GetLastError() : 0;
    std::lock_guard<std::mutex> guard(s.metadata);
    --s.waiting; s.Log("WaitForSingleObject", s.serialization, timeout_ms, result, error);
    if (result != WAIT_OBJECT_0) {
        if (result == WAIT_ABANDONED) {
            const BOOL released = ReleaseMutex(s.serialization);
            const DWORD release_error = released ? 0 : GetLastError();
            s.Log("ReleaseAbandonedMutex", s.serialization, 0, static_cast<uint64_t>(released), release_error);
            // An abandoned acquisition is never a successful source return.
            // Invalidate every token of the dead native owner immediately.
            s.holding_thread = 0; s.holding_lifetime = 0; s.tokens.clear();
            ++s.serialization_generation;
            s.poisoned = released == FALSE;
            out.status = Error(StartupHostCode::Abandoned, release_error, result);
            return out;
        }
        out.status = Error(result == WAIT_TIMEOUT ? StartupHostCode::Timeout :
                           result == WAIT_ABANDONED ? StartupHostCode::Abandoned : StartupHostCode::HostFailure, error, result);
        return out;
    }
    s.holding_thread = thread; s.holding_lifetime = this_thread_lifetime; ++s.acquisition;
    out.token = {s.owner, s.serialization_generation, s.acquisition, this_thread_lifetime, ++s.cookie, thread, true};
    s.tokens.push_back(out.token); return out;
}
StartupHostStatus WindowsStartupBackend::RestoreSerialization(StartupNativeSerializationToken token) {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata);
    if (s.closed) return Error(StartupHostCode::Closed);
    if (token.owner != s.owner || token.generation != s.serialization_generation || !token) return Error(StartupHostCode::InvalidToken);
    if (token.thread != GetCurrentThreadId() || token.thread_lifetime != this_thread_lifetime) return Error(StartupHostCode::ForeignThread);
    const auto it = std::find_if(s.tokens.begin(), s.tokens.end(), [&](const auto& t) {
        return t.acquisition == token.acquisition && t.cookie == token.cookie &&
               t.thread == token.thread && t.thread_lifetime == token.thread_lifetime && t.restores_acquisition == token.restores_acquisition;
    });
    if (it == s.tokens.end() || s.holding_thread != token.thread || token.acquisition != s.acquisition)
        return Error(StartupHostCode::InvalidToken);
    if (!token.restores_acquisition) { s.tokens.erase(it); return {}; }
    const BOOL result = ReleaseMutex(s.serialization);
    const DWORD error = result ? 0 : GetLastError();
    s.Log("ReleaseMutex", s.serialization, 0, static_cast<uint64_t>(result), error);
    if (!result) return Error(StartupHostCode::HostFailure, error);
    s.holding_thread = 0; s.holding_lifetime = 0; s.tokens.clear(); return {};
}
StartupHostStatus WindowsStartupBackend::Close() {
    auto& s = *impl_; std::lock_guard<std::mutex> guard(s.metadata);
    if (s.closed) return Error(StartupHostCode::Closed);
    if (s.holding_thread || s.waiting) return Error(StartupHostCode::Busy);
    for (const auto& b : s.blocks) if (b.use_count() != 1) return Error(StartupHostCode::Borrowed);
    while (!s.files.empty()) {
        const HANDLE file = s.files.front().native; const BOOL closed = CloseHandle(file);
        const DWORD error = closed ? 0 : GetLastError(); s.Log("CloseFileOwner", file, 0, static_cast<uint64_t>(closed), error);
        if (!closed) return Error(StartupHostCode::HostFailure, error);
        s.files.erase(s.files.begin());
    }
    while (!s.blocks.empty()) {
        auto& b = s.blocks.front();
        const BOOL result = VirtualFree(b->allocation, 0, MEM_RELEASE);
        const DWORD error = result ? 0 : GetLastError();
        s.Log("VirtualFreeClose", b->allocation, 0, static_cast<uint64_t>(result), error);
        if (!result) return Error(StartupHostCode::HostFailure, error);
        b->allocation = nullptr; b->live = false;
        s.blocks.erase(s.blocks.begin()); // Successful prefix stays released on retry.
    }
    const BOOL result = CloseHandle(s.serialization);
    const DWORD error = result ? 0 : GetLastError();
    s.Log("CloseHandleMutex", s.serialization, 0, static_cast<uint64_t>(result), error);
    if (!result) return Error(StartupHostCode::HostFailure, error);
    s.serialization = nullptr; s.closed = true; ++s.generation; return {};
}
std::vector<StartupHostCall> WindowsStartupBackend::HostCalls() const {
    std::lock_guard<std::mutex> guard(impl_->metadata); return impl_->calls;
}
const char* StartupHostCodeName(StartupHostCode code) {
    switch (code) {
    case StartupHostCode::Ok: return "ok";
    case StartupHostCode::Closed: return "closed";
    case StartupHostCode::InvalidRequest: return "invalid-request";
    case StartupHostCode::UnsupportedAbi: return "unsupported-abi";
    case StartupHostCode::SourceRange: return "source-range";
    case StartupHostCode::SourceOverlap: return "source-overlap";
    case StartupHostCode::ForeignHandle: return "foreign-handle";
    case StartupHostCode::StaleHandle: return "stale-handle";
    case StartupHostCode::Borrowed: return "borrowed";
    case StartupHostCode::UnknownBytes: return "unknown-bytes";
    case StartupHostCode::HostFailure: return "host-failure";
    case StartupHostCode::UnexpectedGeometry: return "unexpected-geometry";
    case StartupHostCode::Timeout: return "timeout";
    case StartupHostCode::Abandoned: return "abandoned";
    case StartupHostCode::ForeignThread: return "foreign-thread";
    case StartupHostCode::InvalidToken: return "invalid-token";
    case StartupHostCode::Busy: return "busy";
    case StartupHostCode::ShortRead: return "short-read";
    }
    return "invalid-code";
}
} // namespace dokodemo
