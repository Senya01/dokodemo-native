#include "game_storage_win32.h"
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <utility>

namespace dokodemo {
namespace {
using namespace recovered;
namespace fs = std::filesystem;
std::atomic<uint64_t> next_temporary{1};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    bool Close() {
        if (value == INVALID_HANDLE_VALUE)
            return true;
        if (!CloseHandle(value))
            return false;
        value = INVALID_HANDLE_VALUE;
        return true;
    }
};
bool Inside(const fs::path &root, const fs::path &candidate) {
    auto r = root.begin(), c = candidate.begin();
    for (; r != root.end(); ++r, ++c) {
        if (c == candidate.end())
            return false;
        const auto a = r->native(), b = c->native();
        if (CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) != CSTR_EQUAL)
            return false;
    }
    return true;
}
GameStorageStatus Error(DWORD error, const char *detail) {
    auto code = GameStorageCode::IoFailure;
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        code = GameStorageCode::Missing;
    else if (error == ERROR_ACCESS_DENIED || error == ERROR_WRITE_PROTECT)
        code = GameStorageCode::ReadOnly;
    else if (error == ERROR_DISK_FULL || error == ERROR_HANDLE_DISK_FULL)
        code = GameStorageCode::Full;
    else if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
        code = GameStorageCode::Exists;
    else if (error == ERROR_NOT_READY || error == ERROR_INVALID_DRIVE)
        code = GameStorageCode::Unavailable;
    return {code, error, detail};
}
class WindowsGameStorage final : public GameStorage {
  public:
    explicit WindowsGameStorage(WindowsGameStorageOptions options) : options_(std::move(options)) {}
    GameStorageStatus Probe() override {
        ++state_.probe_calls;
        state_.available = state_.writable = false;
        if (options_.unavailable || options_.root.empty())
            return Record({GameStorageCode::Unavailable, 0, "named native storage unavailable policy"});
        std::error_code ec;
        root_ = fs::weakly_canonical(fs::absolute(options_.root), ec);
        if (ec)
            return Record(
                {GameStorageCode::Unavailable, uint32_t(ec.value()), "native storage root resolution"});
        for (const auto &forbidden : options_.forbidden_roots) {
            const auto protected_root = fs::weakly_canonical(fs::absolute(forbidden), ec);
            if (ec)
                return Record(
                    {GameStorageCode::Unavailable, uint32_t(ec.value()), "protected root resolution"});
            if (Inside(protected_root, root_))
                return Record({GameStorageCode::ReadOnly, ERROR_ACCESS_DENIED,
                               "save root lies inside protected assets/media root"});
        }
        if (!options_.read_only)
            fs::create_directories(root_, ec);
        if (ec)
            return Record(Error(DWORD(ec.value()), "native save root creation failed"));
        if (!fs::is_directory(root_, ec))
            return Record({GameStorageCode::Unavailable, uint32_t(ec.value()),
                           "native save root is not an existing directory"});
        ULARGE_INTEGER caller{}, total{}, free{};
        if (!GetDiskFreeSpaceExW(root_.c_str(), &caller, &total, &free))
            return Record(Error(GetLastError(), "native free-space query failed"));
        state_.available = true;
        state_.writable = !options_.read_only;
        state_.free_bytes = caller.QuadPart;
        slot_ = root_ / L"UCJS10002" / L"000";
        native_file_ = slot_ / L"DATA.NONMATCHING.V1";
        return Record({});
    }
    GameStorageStatus LoadNative(std::vector<uint8_t> &out) override {
        ++state_.load_calls;
        if (!state_.available) {
            auto status = Probe();
            if (!status)
                return status;
        }
        // PSP encrypted/utility DATA.BIN is not the native envelope. Never
        // interpret or overwrite it as a successful original savedata load.
        const auto original = slot_ / L"DATA.BIN";
        const auto attributes = GetFileAttributesW(original.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
            return Record({GameStorageCode::UnsupportedOriginal, 0,
                           "PSP DATA.BIN requires an unrecovered import/decryption/field contract"});
        const auto original_error = GetLastError();
        if (original_error != ERROR_FILE_NOT_FOUND && original_error != ERROR_PATH_NOT_FOUND)
            return Record(Error(original_error, "original-save existence check failed"));
        Handle file;
        file.value = CreateFileW(native_file_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.value == INVALID_HANDLE_VALUE)
            return Record(Error(GetLastError(), "native autoload open"));
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.value, &size))
            return Record(Error(GetLastError(), "native save extent"));
        if (size.QuadPart < 16 || size.QuadPart > 0x48010)
            return Record({GameStorageCode::Corrupt, 0, "native save extent outside envelope domain"});
        std::vector<uint8_t> bytes(size_t(size.QuadPart));
        DWORD count = 0;
        if (!ReadFile(file.value, bytes.data(), DWORD(bytes.size()), &count, nullptr))
            return Record(Error(GetLastError(), "native save read"));
        state_.read_bytes += count;
        if (count != bytes.size())
            return Record({GameStorageCode::Corrupt, 0, "native save changed/truncated during read"});
        if (!file.Close())
            return Record(Error(GetLastError(), "native save read handle close"));
        auto status = DecodeNativeSave(bytes, out);
        if (status)
            state_.native_record = state_.opaque_record_read = true;
        return Record(status);
    }
    GameStorageStatus PublishNative(const std::vector<uint8_t> &payload, bool replace) override {
        ++state_.publish_calls;
        if (!state_.available) {
            auto status = Probe();
            if (!status)
                return status;
        }
        if (options_.read_only)
            return Record({GameStorageCode::ReadOnly, 0, "native read-only policy"});
        if (payload.size() > 0x48000)
            return Record({GameStorageCode::Bounds, 0, "native save capacity"});
        auto bytes = EncodeNativeSave(payload);
        if (bytes.size() > state_.free_bytes)
            return Record({GameStorageCode::Full, ERROR_DISK_FULL, "native save lacks available space"});
        std::error_code ec;
        fs::create_directories(slot_, ec);
        if (ec)
            return Record(Error(DWORD(ec.value()), "native slot directory creation"));
        const auto original = slot_ / L"DATA.BIN";
        if (GetFileAttributesW(original.c_str()) != INVALID_FILE_ATTRIBUTES)
            return Record({GameStorageCode::UnsupportedOriginal, 0, "existing original DATA.BIN preserved"});
        const auto original_error = GetLastError();
        if (original_error != ERROR_FILE_NOT_FOUND && original_error != ERROR_PATH_NOT_FOUND)
            return Record(Error(original_error, "original-save publication existence check failed"));
        const auto temporary = slot_ / (L".native-write-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                        std::to_wstring(next_temporary.fetch_add(1)) + L".tmp");
        Handle file;
        file.value = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.value == INVALID_HANDLE_VALUE)
            return Record(Error(GetLastError(), "native temporary save creation"));
        DWORD written = 0;
        GameStorageStatus status;
        if (!WriteFile(file.value, bytes.data(), DWORD(bytes.size()), &written, nullptr))
            status = Error(GetLastError(), "native save write failed");
        else if (written != bytes.size())
            status = {GameStorageCode::IoFailure, ERROR_WRITE_FAULT, "native short write"};
        else if (!FlushFileBuffers(file.value))
            status = Error(GetLastError(), "native save flush failed");
        if (!file.Close() && status)
            status = Error(GetLastError(), "native save write handle close");
        if (status && !MoveFileExW(temporary.c_str(), native_file_.c_str(),
                                   MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)))
            status = Error(GetLastError(), "native completed-write publication failed");
        if (!status) {
            DeleteFileW(temporary.c_str());
            return Record(status);
        }
        state_.written_bytes += written;
        state_.native_record = true;
        return Record({});
    }
    GameStorageSnapshot Snapshot() const override {
        return state_;
    }

  private:
    GameStorageStatus Record(GameStorageStatus status) {
        state_.status = status;
        return status;
    }
    WindowsGameStorageOptions options_;
    fs::path root_, slot_, native_file_;
    GameStorageSnapshot state_;
};
} // namespace
std::unique_ptr<recovered::GameStorage> CreateWindowsGameStorage(WindowsGameStorageOptions options) {
    return std::make_unique<WindowsGameStorage>(std::move(options));
}
} // namespace dokodemo
