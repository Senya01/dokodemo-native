#pragma once
#include "recovered/module_manager_native.h"
#include "recovered/startup_flow_native.h"
#include "recovered/game_boot_native.h"
#include "game73_pcm_output_win32.h"
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
    bool prepared_codec_envelope = false; // explicit NEW domain, legacy finite prefix stays default
    bool test_controller_input = false; // explicit injected-frame test domain, never physical-input credit
    // Explicit window-fixture civil-clock step AFTER the intro's first clock
    // read. Zero in normal play;not elapsed-time or real-RTC timing evidence.
    uint32_t test_actor_calendar_step_minutes = 0;
    // Explicit captured-seed replay for injected-input regression sessions.
    // Off in ordinary play; this never changes source RNG arithmetic.
    bool test_rng_seed_replay = false;
    uint32_t test_rng_seed_word = 0;
    std::wstring native_save_root;
    bool storage_unavailable=false,storage_read_only=false;
    bool test_missing_device_prompt=false; // test-only negative IoDevctl path before real native autoload
    recovered::NativeTitleLayout native_title_layout=recovered::NativeTitleLayout::CharacterEntrance;
};
inline constexpr uint32_t StartupNoCurrentModule = UINT32_MAX;
struct StartupRuntimeEvent {
    uint64_t ordinal = 0, native_object = 0, owner = 0;
    uint32_t source_pc = 0, thread = 0, win32_error = 0, wait_result = 0;
    int crt_errno = 0;
    unsigned long dos_errno = 0;
    int64_t value = 0;
    bool source_return = false;
    std::string operation, origin, detail;
    std::string source_pc_domain;
    uint32_t module_request_index = StartupNoCurrentModule;
    recovered::NativeModuleToken module;
    recovered::NativeModuleToken source_module; // PC namespace; module remains consumer/request
};
struct StartupModuleObservation {
    // Zero-based host request ordinal, NOT a PSP module index. Retired entries
    // remain diagnostics; only StartupObservation::module_records is live registry.
    uint32_t request_index = StartupNoCurrentModule, wrapper_callsite = 0;
    uint32_t repeated_record_index = StartupNoCurrentModule;
    std::string path, frontier;
    uint32_t frontier_source_pc = 0, flags = 0;
    bool frontier_module_relative = false;
    recovered::NativeModuleToken frontier_source_module;
    std::vector<uint8_t> options; //20 source request bytes; never a module payload
    uint64_t request_owner = 0;
    bool pending = false;
    int32_t source_fd = -1;
    uint64_t file_native_object = 0, file_backend_owner = 0;
    uint64_t file_backend_generation = 0, file_backend_serial = 0, encoded_file_bytes = 0;
    bool file_live = false;
    uint64_t staging_owner = 0, staging_generation = 0, staging_serial = 0;
    uint64_t staging_host_address = 0, staging_known_bytes = 0;
    uint64_t staging_committed_bytes = 0;
    uint32_t staging_page_bytes = 0;
    uint32_t staging_source_coordinate = 0, staging_requested = 0, staging_transferred = 0;
    uint32_t staging_read_error = 0;
    int64_t staging_file_position = -1;
    bool staging_live = false, staging_borrow_live = false, staging_host_read = false;
    bool staging_full_read = false, staging_hash_matches = false, staging_freed_after_joins = false;
    std::string staging_sha256;
    recovered::NativeModuleToken token;
    bool module_uid_assigned = false, module_load_returned = false, module_input_live = false;
    bool module_start_called = false, module_start_returned = false, module_start_entry_returned = false;
    int32_t module_load_result = 0, module_start_result = 0, module_start_entry_result = 0;
    bool source_close_called = false, source_close_returned = false;
    int32_t source_close_result = 0;
    bool module_file_closed_while_input_live = false;
    bool module_stop_called = false, module_stop_returned = false, module_unloaded = false;
    int32_t module_stop_result = 0;
    bool module_input_released = false, host_file_closed = false;
    bool module_initialization_blocked = false, module_initialization_abort_called = false, module_initialization_aborted = false;
    bool record_live = false; // false after unload; record is then retired diagnostic metadata
    recovered::NativeModuleSnapshot record;
};
struct StartupObservation {
    std::string domain = "WindowsNativeEntry1184";
    std::string caller_state = "NotStarted", worker_state = "NotStarted";
    std::string barrier;
    uint32_t barrier_source_pc = 0;
    bool barrier_module_relative = false;
    recovered::NativeModuleToken barrier_source_module;
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
    uint64_t encoded_file_bytes = 0, staging_owner = 0, staging_generation = 0, staging_serial = 0;
    uint64_t staging_host_address = 0, staging_known_bytes = 0;
    int64_t staging_file_position = -1;
    uint32_t staging_source_coordinate = 0, staging_requested = 0, staging_transferred = 0;
    uint32_t staging_read_error = 0;
    bool staging_live = false, staging_borrow_live = false, staging_host_read = false;
    bool staging_full_read = false, staging_hash_matches = false, staging_freed_after_joins = false;
    bool module_uid_assigned = false, module_load_returned = false, module_start_called = false;
    int32_t module_uid = 0;
    // Additive Stage16 API. Source fd and owned registry/input lifetimes differ.
    uint64_t module_manager_owner = 0, module_generation = 0, module_serial = 0;
    bool module_input_live = false, module_file_closed_while_input_live = false;
    bool module_stop_called = false, module_unloaded = false, module_input_released = false;
    bool module_start_returned = false, module_stop_returned = false;
    int32_t module_start_result = 0, module_stop_result = 0;
    std::string module_state;
    recovered::NativeModuleSnapshot module_record;
    std::vector<recovered::NativeModuleProviderSnapshot> module_providers;
    std::string staging_sha256, module_binding_frontier;
    // All legacy single-module fields explicitly alias request 0, never last.
    std::vector<StartupModuleObservation> module_observations;
    recovered::StartupRepeatedModuleSnapshot repeated_modules;
    recovered::GameBootObservation game_boot;
    audio::Game73PcmOutputSnapshot native_audio;
    uint32_t native_audio_creation_error=0;
    std::vector<recovered::NativeModuleSnapshot> module_records; //live manager only
    uint32_t current_module_index = StartupNoCurrentModule;
    std::string current_frontier;
    uint32_t current_frontier_source_pc = 0;
    bool current_frontier_module_relative = false;
    recovered::NativeModuleToken current_frontier_source_module;
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
    // Real keyboard polling for the native game input adapter. An unfocused
    // window releases buttons; no synthetic key events are sent to Windows.
    recovered::ControllerSample PollControllerInput(bool window_focused);
    StartupRuntimeStatus InjectTestControllerInput(uint32_t buttons,bool focused);
    StartupRuntimeStatus PostGameText(const recovered::GameTextEvent& event);
    bool CopyGameFrame(recovered::GameGraphicsFrame& out) const;
    bool RecordGameFramePresented(uint64_t generation, uint64_t sequence);
    bool DrawGraphicsDiagnosticFrame(); // explicit window-smoke image, not gameplay
    StartupRuntimeStatus RequestExit();
    // Timeout retains the live owner. Do not destroy it until Close succeeds.
    StartupRuntimeStatus Close(uint32_t timeout_ms = 5000);
    static uint32_t PendingDeferredCodecCleanup() noexcept;
    static StartupRuntimeStatus RetryDeferredCodecCleanup(uint32_t timeout_ms = 5000);
private:
    struct Impl;
    explicit StartupCoordinator(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
    friend struct StartupRuntimeTestAccess;
};
const char* StartupRuntimeCodeName(StartupRuntimeCode code);
} // namespace dokodemo
