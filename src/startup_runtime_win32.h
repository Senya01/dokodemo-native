#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo {
enum class StartupRuntimeCode {
    Ok, AlreadyStarted, NotStarted, Closed, HostFailure, Timeout, InvalidUid, WrongThread, Busy
};
struct StartupRuntimeStatus {
    StartupRuntimeCode code = StartupRuntimeCode::Ok;
    uint32_t win32_error = 0, wait_result = 0;
    int crt_errno = 0;
    unsigned long dos_errno = 0;
    explicit operator bool() const { return code == StartupRuntimeCode::Ok; }
};
struct StartupRuntimeOptions {
    // Actual native directory bound to disc0:, e.g. <workspace>/assets/iso.
    std::wstring media_root;
};
struct StartupRuntimeEvent {
    uint64_t ordinal = 0, native_object = 0, owner = 0;
    uint32_t source_pc = 0, thread = 0, win32_error = 0, wait_result = 0;
    int crt_errno = 0;
    unsigned long dos_errno = 0;
    int64_t value = 0;
    bool source_return = false;
    std::string operation, origin, detail;
};
struct StartupObservation {
    std::string domain = "WindowsNativeEntry1184";
    std::string caller_state = "NotStarted", worker_state = "NotStarted";
    std::string barrier;
    uint32_t barrier_source_pc = 0;
    uint64_t owner = 0, last_event = 0, dropped_events = 0;
    uint32_t caller_thread_id = 0, worker_thread_id = 0, callback_dispatch_thread = 0;
    uint32_t callback_dispatch_count = 0;
    int32_t stored_thread_uid = 0, callback_uid = 0, source_fd = -1;
    int32_t callback_create_result = 0, callback_register_result = 0;
    bool uid_slot_written = false, callback_setup_returned = false;
    bool caller_joined = false, worker_joined = false;
    bool media_live = false, file_live = false, callback_live = false;
    bool closing = false, resources_closed = false, exit_requested = false;
    bool preceding_psp_history = false, constructors_established = false;
    bool reached_100C = false, full_game = false;
    uint32_t media_volume = 0, pending_module_flags = 0;
    uint64_t media_file_index = 0, file_native_object = 0;
    uint64_t file_backend_owner = 0, file_backend_generation = 0, file_backend_serial = 0;
    uint64_t pending_module_owner = 0;
    bool pending_module_live = false;
    std::string pending_module_path;
    std::vector<uint8_t> pending_module_options; //actual20B request copy, never a PRX payload
    StartupRuntimeStatus status;
    std::vector<StartupRuntimeEvent> events; // bounded metadata, never payloads
};
class StartupCoordinator final {
public:
    static std::unique_ptr<StartupCoordinator> Create(const StartupRuntimeOptions&,
                                                       StartupRuntimeStatus& status);
    ~StartupCoordinator();
    StartupCoordinator(const StartupCoordinator&) = delete;
    StartupCoordinator& operator=(const StartupCoordinator&) = delete;
    StartupRuntimeStatus Start();
    StartupObservation Observe(uint64_t after_event = 0) const;
    StartupRuntimeStatus RequestExit();
    // Timeout retains the live owner. Do not destroy it until Close succeeds.
    StartupRuntimeStatus Close(uint32_t timeout_ms = 5000);
private:
    struct Impl;
    explicit StartupCoordinator(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
    friend struct StartupRuntimeTestAccess;
};
const char* StartupRuntimeCodeName(StartupRuntimeCode code);
} // namespace dokodemo
