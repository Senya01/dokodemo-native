#include "startup_runtime_win32.h"
#include "startup_memory_backend_win32.h"
#include "recovered/startup_flow_native.h"
#include "recovered/audiocodec_module_native.h"
#include "core_hash.h"
#include <windows.h>
#include <process.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <deque>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace dokodemo {
namespace {
std::atomic<uint64_t> next_runtime_owner{1}, next_source_identity{1};
uint64_t Address(HANDLE h) { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h)); }
int32_t NewSourceIdentity() {
    const uint64_t value = next_source_identity.fetch_add(1);
    return value <= uint64_t((std::numeric_limits<int32_t>::max)()) ? static_cast<int32_t>(value) : -1;
}
StartupRuntimeStatus Failure(StartupRuntimeCode code, DWORD error = 0, DWORD wait = 0) {
    StartupRuntimeStatus out; out.code = code; out.win32_error = error; out.wait_result = wait; return out;
}
struct NativeCancelled {};
struct NativeFailure {};
// Adapter return encoding: -1 means an actual native operation/registry validation
// failed; 0 means success for status calls. Exact PSP error numbers are unknown.
constexpr int32_t AdapterError = -1;
constexpr uint32_t EncodedModuleBytes = 3232, HostStagingCoordinate = 0x61000000;
}

struct StartupCoordinator::Impl final : recovered::StartupFlowServices {
    struct ThreadContext { Impl* runtime; bool worker; };
    struct CallbackRecord {
        uint64_t owner = 0;
        int32_t uid = 0;
        DWORD thread = 0;
        HANDLE notification = nullptr;
        int (*function)(int, int, void*) = nullptr;
        void* common = nullptr;
        std::string name;
        bool registered = false, dispatching = false;
        ~CallbackRecord() { if (notification) (void)CloseHandle(notification); }
    };
    static thread_local ThreadContext* current_context;
    mutable std::mutex metadata;
    StartupRuntimeOptions options;
    StartupObservation state;
    std::deque<StartupRuntimeEvent> events;
    std::unique_ptr<WindowsStartupBackend> backend;
    std::unique_ptr<CallbackRecord> callback;
    ThreadContext caller_context{this, false}, worker_context{this, true};
    HANDLE caller = nullptr, worker = nullptr, cancel = nullptr, exit_event = nullptr;
    HANDLE media = INVALID_HANDLE_VALUE;
    DWORD tls_index = TLS_OUT_OF_INDEXES;
    bool started = false, caller_resumed = false, worker_resumed = false, close_in_progress = false;
    bool media_activated = false;
    uint32_t notifications = 0;
    int32_t worker_uid = 0;
    StartupNativeFile file;
    uint64_t file_native = 0;
    uint64_t file_runtime_owner = 0;
    uint64_t encoded_file_size = 0;
    std::string source_file_path;
    struct PendingModuleRequest {
        bool live = false;
        int32_t fd = -1;
        uint32_t flags = 0;
        uint64_t owner = 0;
        StartupNativeFile backend_file;
        recovered::StartupModuleOptions options{};
        std::string path;
        StartupNativeBlock staging;
        StartupAllocationBorrow staging_borrow;
    } pending_module;
    std::atomic<bool> cancelled{false};

    explicit Impl(const StartupRuntimeOptions& input) : options(input) {
        state.owner = next_runtime_owner.fetch_add(1);
    }
    void EmitLocked(const char* operation, const char* origin, uint32_t pc = 0,
                    int64_t value = 0, bool returned = false, uint64_t object = 0,
                    DWORD error = 0, DWORD wait = 0, std::string_view detail = {},
                    int crt_error = 0, unsigned long dos_error = 0) noexcept {
        try {
        StartupRuntimeEvent event;
        event.ordinal = ++state.last_event; event.owner = state.owner;
        event.operation = operation; event.origin = origin; event.source_pc = pc;
        event.thread = GetCurrentThreadId(); event.value = value;
        event.source_return = returned; event.native_object = object;
        event.win32_error = error; event.wait_result = wait;
        if (!detail.empty()) event.detail.assign(detail.data(), detail.size());
        event.crt_errno = crt_error; event.dos_errno = dos_error;
        if (events.size() == 256) { events.pop_front(); ++state.dropped_events; }
        events.push_back(std::move(event));
        } catch (...) {
            // Telemetry must never change native ownership or control flow.
            ++state.dropped_events;
        }
    }
    void Emit(const char* operation, const char* origin, uint32_t pc = 0,
              int64_t value = 0, bool returned = false, uint64_t object = 0,
              DWORD error = 0, DWORD wait = 0, std::string_view detail = {}) noexcept {
        try {
        std::lock_guard<std::mutex> lock(metadata);
        EmitLocked(operation, origin, pc, value, returned, object, error, wait, detail);
        } catch (...) {} //No native control/ownership consequence of telemetry.
    }
    void FailLocked(StartupRuntimeStatus status) { state.status = status; }
    bool IsCurrent(bool is_worker) const {
        return current_context && current_context->runtime == this &&
               current_context->worker == is_worker && tls_index != TLS_OUT_OF_INDEXES &&
               TlsGetValue(tls_index) == current_context;
    }
    bool Enter(ThreadContext& context) {
        const BOOL set = TlsSetValue(tls_index, &context);
        const DWORD error = set ? 0 : GetLastError();
        std::lock_guard<std::mutex> lock(metadata);
        EmitLocked("TlsSetValue-enter", "HostTLS", 0, set, false, tls_index, error,
                   0, context.worker ? "worker" : "caller");
        if (!set) { FailLocked(Failure(StartupRuntimeCode::HostFailure, error)); return false; }
        current_context = &context;
        (context.worker ? state.worker_state : state.caller_state) = "Running";
        return true;
    }
    void Leave(ThreadContext& context) {
        const BOOL set = TlsSetValue(tls_index, nullptr);
        const DWORD error = set ? 0 : GetLastError();
        current_context = nullptr;
        std::lock_guard<std::mutex> lock(metadata);
        EmitLocked("TlsSetValue-leave", "HostTLS", 0, set, false, tls_index, error,
                   0, context.worker ? "worker" : "caller");
        if (!set) FailLocked(Failure(StartupRuntimeCode::HostFailure, error));
    }
    static unsigned __stdcall CallerThunk(void* opaque) {
        auto& context = *static_cast<ThreadContext*>(opaque);
        auto& self = *context.runtime;
        bool entered = false;
        try {
            entered = self.Enter(context);
            if (entered) {
                if (self.HostCancelled()) throw NativeCancelled{};
                self.Emit("NativeEntry1184", "NativeEntry", 0x1184);
                const auto stop = recovered::RunStartupCaller1184(self);
                self.Emit("CallerFlowStop", "HostLifecycle", 0, static_cast<int64_t>(stop), false,
                          0, 0, 0, "No claimed source function v0;15D8 discards caller result");
            }
        } catch (const NativeCancelled&) {
            self.Emit("CallerHostCancelled", "HostLifecycle");
        } catch (...) {
            std::lock_guard<std::mutex> lock(self.metadata);
            if (self.state.status) self.FailLocked(Failure(StartupRuntimeCode::HostFailure));
            self.EmitLocked("CallerException", "HostLifecycle");
        }
        if (entered) self.Leave(context);
        { std::lock_guard<std::mutex> lock(self.metadata); self.state.caller_state = "Exited"; }
        return 0; //native CRT thread exit code, not a recovered PSP return.
    }
    static unsigned __stdcall WorkerThunk(void* opaque) {
        auto& context = *static_cast<ThreadContext*>(opaque);
        auto& self = *context.runtime;
        bool entered = false;
        try {
            entered = self.Enter(context);
            if (entered) {
                if (self.HostCancelled()) throw NativeCancelled{};
                self.Emit("WorkerEntryF7C", "NativeEntry", 0xF7C);
                const auto stop = recovered::RunStartupWorkerF7C(self);
                if (stop == recovered::StartupFlowStop::Unsupported) self.BarrierPump();
                else self.Emit("WorkerFlowStop", "HostLifecycle", 0, static_cast<int64_t>(stop));
            }
        } catch (const NativeCancelled&) {
            self.Emit("WorkerHostCancelled", "HostLifecycle");
        } catch (...) {
            std::lock_guard<std::mutex> lock(self.metadata);
            if (self.state.status) self.FailLocked(Failure(StartupRuntimeCode::HostFailure));
            self.EmitLocked("WorkerException", "HostLifecycle");
        }
        if (entered) self.Leave(context);
        { std::lock_guard<std::mutex> lock(self.metadata); self.state.worker_state = "Exited"; }
        return 0;
    }
    uintptr_t Begin(bool is_worker, unsigned stack, unsigned& thread_id) {
        const uintptr_t result = _beginthreadex(nullptr, stack,
            is_worker ? &WorkerThunk : &CallerThunk,
            is_worker ? static_cast<void*>(&worker_context) : static_cast<void*>(&caller_context),
            CREATE_SUSPENDED, &thread_id);
        // FIRST action after creation: publish the primitive owning handle. No
        // allocation/logging may intervene while a suspended native object exists.
        (is_worker ? worker : caller) = reinterpret_cast<HANDLE>(result);
        (is_worker ? state.worker_thread_id : state.caller_thread_id) = thread_id;
        const int crt_error = result ? 0 : errno;
        unsigned long dos_error = 0;
        if (!result) (void)_get_doserrno(&dos_error);
        // Callers hold metadata, so the suspended object is published atomically.
        EmitLocked("_beginthreadex", "HostThread", is_worker ? 0x11AC : 0,
                   result ? 1 : 0, false, static_cast<uint64_t>(result), 0, 0,
                   is_worker ? "worker;source priority16/stack11000/attr4000;commit request11000"
                             : "native caller scheduling;default host stack;not source CreateThread",
                   crt_error, dos_error);
        if (!result) {
            auto failure = Failure(StartupRuntimeCode::HostFailure);
            failure.crt_errno = crt_error; failure.dos_errno = dos_error; FailLocked(failure);
        }
        return result;
    }
    int32_t CreateWorker(const recovered::StartupWorkerRequest& request) override {
        std::lock_guard<std::mutex> lock(metadata);
        if (state.closing) throw NativeCancelled{};
        if (!IsCurrent(false) || worker || !request.name || std::string(request.name) != "main" ||
            request.entry != 0xF7C || request.priority != 0x10 || request.stack_bytes != 0x11000 ||
            request.attributes != 0x4000 || request.options) {
            EmitLocked("CreateThreadAdapter", "SourceAdapter", 0x11AC, AdapterError, true,
                       0, ERROR_INVALID_PARAMETER, 0, "actual request/owner validation failure");
            return AdapterError;
        }
        // Reserve source ID before creating an OS thread so exhaustion cannot orphan one.
        const int32_t identity = NewSourceIdentity();
        if (identity <= 0) {
            EmitLocked("CreateThreadAdapter", "SourceAdapter", 0x11AC, AdapterError, true,
                       0, ERROR_NO_SYSTEM_RESOURCES, 0, "source identity space exhausted");
            return AdapterError;
        }
        unsigned id = 0;
        const uintptr_t native = Begin(true, request.stack_bytes, id);
        if (!native) {
            EmitLocked("CreateThreadAdapter", "SourceAdapter", 0x11AC, AdapterError, true);
            return AdapterError;
        }
        worker = reinterpret_cast<HANDLE>(native); worker_uid = identity;
        state.worker_thread_id = id; state.worker_state = "Suspended";
        EmitLocked("CreateThreadAdapter", "SourceAdapter", 0x11AC, identity, true,
                   Address(worker), 0, 0, "main;native positive registry identity;not HANDLE cast");
        return identity;
    }
    void StoreCreatedWorkerUid(int32_t uid) override {
        std::lock_guard<std::mutex> lock(metadata);
        state.stored_thread_uid = uid; state.uid_slot_written = true;
        EmitLocked("StoreCreatedThreadUid", "SourceEffect", 0x11C8, uid, false, 0, 0, 0,
                   "typed native field;source slot module+F5D7C;no previous slot read/RAM claim");
    }
    int32_t StartWorker(int32_t uid, uint32_t bytes, const void* args) override {
        std::lock_guard<std::mutex> lock(metadata);
        if (state.closing) throw NativeCancelled{};
        if (!IsCurrent(false) || uid != worker_uid || uid <= 0 || !worker || worker_resumed || bytes || args) {
            EmitLocked("StartThreadAdapter", "SourceAdapter", 0x11CC, AdapterError, true,
                       0, ERROR_INVALID_PARAMETER); return AdapterError;
        }
        const DWORD previous = ResumeThread(worker);
        const DWORD error = previous == DWORD(-1) ? GetLastError() : 0;
        EmitLocked("ResumeThread", "HostThread", 0x11CC, previous, false, Address(worker), error);
        if (previous == DWORD(-1)) {
            FailLocked(Failure(StartupRuntimeCode::HostFailure, error));
            EmitLocked("StartThreadAdapter", "SourceAdapter", 0x11CC, AdapterError, true, Address(worker), error);
            return AdapterError;
        }
        worker_resumed = true;
        EmitLocked("StartThreadAdapter", "SourceAdapter", 0x11CC, 0, true, Address(worker));
        return 0;
    }
    recovered::StartupFlowStop SleepCaller() override {
        if (!IsCurrent(false)) throw NativeFailure{};
        { std::lock_guard<std::mutex> lock(metadata);
          state.caller_state = "SourceSleeping";
          EmitLocked("SleepThread-enter", "SourceCall", 0x1210, 0, false, Address(cancel)); }
        const DWORD wait = WaitForSingleObject(cancel, INFINITE);
        const DWORD error = wait == WAIT_FAILED ? GetLastError() : 0;
        Emit("CallerCancellationWait", "HostLifecycle", 0x1210, 0, false, Address(cancel), error, wait,
             "No source SleepThread return; native caller lifetime cancelled");
        if (wait != WAIT_OBJECT_0) throw NativeFailure{};
        return recovered::StartupFlowStop::HostCancelled;
    }
    static int ExitThunk(int arg1, int arg2, void* common) {
        auto* context = current_context;
        if (!context || !context->worker || !context->runtime->IsCurrent(true)) throw NativeFailure{};
        return recovered::ExitCallback1564(*context->runtime, arg1, arg2, common);
    }
    int32_t CreateExitCallback(const recovered::StartupCallbackRequest& request) override {
        std::lock_guard<std::mutex> lock(metadata);
        if (state.closing) throw NativeCancelled{};
        if (!IsCurrent(true) || callback || !request.name || std::string(request.name) != "gameexit" ||
            request.entry != 0x1564 || request.common) {
            state.callback_create_result = AdapterError;
            EmitLocked("CreateCallbackAdapter", "SourceAdapter", 0x159C, AdapterError, true,
                       0, ERROR_INVALID_PARAMETER); return AdapterError;
        }
        std::unique_ptr<CallbackRecord> record(new (std::nothrow) CallbackRecord);
        if (!record) {
            state.callback_create_result = AdapterError;
            EmitLocked("CreateCallbackAdapter", "SourceAdapter", 0x159C, AdapterError, true,
                       0, ERROR_NOT_ENOUGH_MEMORY); return AdapterError;
        }
        try { record->name = request.name; }
        catch (const std::bad_alloc&) {
            state.callback_create_result = AdapterError;
            EmitLocked("CreateCallbackAdapter", "SourceAdapter", 0x159C, AdapterError, true,
                       0, ERROR_NOT_ENOUGH_MEMORY); return AdapterError;
        }
        record->uid = NewSourceIdentity(); record->owner = state.owner;
        if (record->uid <= 0) {
            state.callback_create_result = AdapterError;
            EmitLocked("CreateCallbackAdapter", "SourceAdapter", 0x159C, AdapterError, true,
                       0, ERROR_NO_SYSTEM_RESOURCES); return AdapterError;
        }
        record->notification = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const DWORD error = record->notification ? 0 : GetLastError();
        if (!record->notification) {
            EmitLocked("CreateEventW-callback", "HostCallback", 0, 0, false, 0, error);
            state.callback_create_result = AdapterError;
            EmitLocked("CreateCallbackAdapter", "SourceAdapter", 0x159C, AdapterError, true, 0, error);
            return AdapterError;
        }
        record->thread = GetCurrentThreadId(); record->function = &ExitThunk;
        record->common = request.common;
        state.callback_uid = record->uid; state.callback_live = true;
        state.callback_create_result = record->uid;
        callback = std::move(record);
        EmitLocked("CreateEventW-callback", "HostCallback", 0, 1,
                   false, Address(callback->notification), error);
        EmitLocked("CreateCallbackAdapter", "SourceAdapter", 0x159C, callback->uid, true,
                   Address(callback->notification), 0, 0, "gameexit;source1564;commonNULL;worker-owned");
        return callback->uid;
    }
    int32_t RegisterExitCallback(int32_t uid) override {
        std::lock_guard<std::mutex> lock(metadata);
        const bool valid = IsCurrent(true) && callback && uid == callback->uid &&
            callback->owner == state.owner && callback->thread == GetCurrentThreadId() && !state.resources_closed;
        const int32_t result = valid ? 0 : AdapterError;
        if (valid) {
            callback->registered = true;
            if (notifications) {
                const BOOL signaled = SetEvent(callback->notification);
                const DWORD error = signaled ? 0 : GetLastError();
                EmitLocked("SetEvent-pending-callback", "HostNotification", 0, signaled, false,
                           Address(callback->notification), error);
                if (!signaled) { FailLocked(Failure(StartupRuntimeCode::HostFailure, error)); }
            }
        }
        state.callback_register_result = result; state.callback_setup_returned = true;
        EmitLocked("RegisterExitCallbackAdapter", "SourceAdapter", 0x15A4, result, true,
                   valid ? Address(callback->notification) : 0, valid ? 0 : ERROR_INVALID_HANDLE,
                   0, "argument=" + std::to_string(uid));
        return result;
    }
    int Dispatch(recovered::StartupDispatchOrigin origin) {
        if (!IsCurrent(true)) throw NativeFailure{};
        int (*function)(int, int, void*) = nullptr;
        void* common = nullptr;
        uint32_t count = 0;
        {
            std::lock_guard<std::mutex> lock(metadata);
            if (!callback || !callback->registered || callback->owner != state.owner ||
                callback->thread != GetCurrentThreadId() || !notifications || callback->dispatching) return 0;
            count = notifications; notifications = 0;
            callback->dispatching = true; function = callback->function; common = callback->common;
            const BOOL reset = ResetEvent(callback->notification);
            const DWORD error = reset ? 0 : GetLastError();
            EmitLocked("ResetEvent-callback", "HostNotification", 0, reset, false,
                       Address(callback->notification), error);
            EmitLocked("CallbackDispatch-enter", origin == recovered::StartupDispatchOrigin::SourceCheckCallback
                       ? "SourceCheckCallback" : "HostBarrierPump", 0x1564, count);
        }
        const int result = function(static_cast<int>(count), 0, common); //normal typed C++ ABI
        {
            std::lock_guard<std::mutex> lock(metadata);
            callback->dispatching = false;
            ++state.callback_dispatch_count; state.callback_dispatch_thread = GetCurrentThreadId();
            EmitLocked("CallbackDispatch-return", origin == recovered::StartupDispatchOrigin::SourceCheckCallback
                       ? "SourceCheckCallback" : "HostBarrierPump", 0x1574, result, true, 0, 0, 0,
                       "Native ExitGame returned; no claim about original PSP return");
        }
        return 1;
    }
    int32_t CheckCallback(uint32_t source_callsite) override {
        const int dispatched = Dispatch(recovered::StartupDispatchOrigin::SourceCheckCallback);
        Emit("CheckCallbackAdapter", "SourceAdapter", source_callsite, dispatched, true);
        // Source contains no delay. This bounded native pacing limits failed-open
        // service log growth and is separately labelled; no PSP timing is claimed.
        if (!HostCancelled()) {
            const DWORD wait = WaitForSingleObject(cancel, 25);
            const DWORD error = wait == WAIT_FAILED ? GetLastError() : 0;
            Emit("HostRetryPacing", "HostAdaptation", 0, 0, false, Address(cancel), error, wait);
            if (wait == WAIT_FAILED) throw NativeFailure{};
        }
        return dispatched;
    }
    void ExitGame() override {
        if (!IsCurrent(true)) throw NativeFailure{};
        cancelled.store(true);
        const BOOL signaled = SetEvent(cancel);
        const DWORD error = signaled ? 0 : GetLastError();
        std::lock_guard<std::mutex> lock(metadata);
        state.exit_requested = true;
        EmitLocked("ExitGameAdapter", "SourceAdapter", 0x156C, 0, false, Address(cancel), error,
                   0, "void native session-exit request;returns to native callback;no ExitProcess");
        if (!signaled) FailLocked(Failure(StartupRuntimeCode::HostFailure, error));
    }
    int32_t CheckMedium() override {
        if (!IsCurrent(true)) throw NativeFailure{};
        DWORD error = 0;
        if (media == INVALID_HANDLE_VALUE) {
            media = CreateFileW(options.media_root.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            error = media == INVALID_HANDLE_VALUE ? GetLastError() : 0;
            Emit("CreateFileW-media", "HostMedia", 0, media != INVALID_HANDLE_VALUE, false, Address(media), error);
        }
        BY_HANDLE_FILE_INFORMATION information{};
        const BOOL inspected = media != INVALID_HANDLE_VALUE && GetFileInformationByHandle(media, &information);
        if (!inspected && !error) error = GetLastError();
        const bool ready = inspected && (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        if (media != INVALID_HANDLE_VALUE && !ready) {
            const BOOL closed = CloseHandle(media); const DWORD close_error = closed ? 0 : GetLastError();
            Emit("CloseRejectedMedia", "HostMedia", 0, closed, false, Address(media), close_error);
            if (closed) media = INVALID_HANDLE_VALUE;
        }
        std::lock_guard<std::mutex> lock(metadata);
        state.media_live = ready;
        if (ready) {
            state.media_volume = information.dwVolumeSerialNumber;
            state.media_file_index = (uint64_t(information.nFileIndexHigh) << 32) | information.nFileIndexLow;
        }
        EmitLocked("CheckMediumAdapter", "SourceAdapter", 0x1458, ready ? 1 : 0, true,
                   Address(media), error, 0, "native accessible-directory capability;not physical PSP UMD;volume=" +
                   std::to_string(state.media_volume) + ";fileIndex=" + std::to_string(state.media_file_index));
        return ready ? 1 : 0;
    }
    int32_t Activate(uint32_t mode, const char* device) override {
        if (!IsCurrent(true)) throw NativeFailure{};
        BY_HANDLE_FILE_INFORMATION information{};
        const bool request_valid = mode == 1 && device && std::string(device) == "disc0:";
        BOOL inspected = FALSE;
        DWORD error = !request_valid ? ERROR_INVALID_PARAMETER :
                      media == INVALID_HANDLE_VALUE ? ERROR_INVALID_HANDLE : 0;
        if (!error) {
            inspected = GetFileInformationByHandle(media, &information);
            error = inspected ? 0 : GetLastError();
        }
        media_activated = inspected && (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        const int32_t result = media_activated ? 0 : AdapterError;
        Emit("UmdActivateAdapter", "SourceAdapter", 0x147C, result, true, Address(media), error,
             0, "native disc0 mapping activation;source ignores result");
        return result;
    }
    int32_t WaitDriveStat(uint32_t mask) override {
        if (!IsCurrent(true)) throw NativeFailure{};
        BY_HANDLE_FILE_INFORMATION information{};
        BOOL inspected = FALSE;
        DWORD error = mask != 0x22 ? ERROR_INVALID_PARAMETER :
                      !media_activated || media == INVALID_HANDLE_VALUE ? ERROR_INVALID_HANDLE : 0;
        if (!error) {
            inspected = GetFileInformationByHandle(media, &information);
            error = inspected ? 0 : GetLastError();
        }
        const int32_t result = inspected && (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : AdapterError;
        Emit("UmdWaitDriveStatAdapter", "SourceAdapter", 0x1484, result, true, Address(media), error,
             0, "ordinary non-CB native ready-state check;source ignores result;no callback dispatch");
        return result;
    }
    int32_t OpenRead(const char* path, uint32_t flags, uint32_t mode) override {
        if (!IsCurrent(true)) throw NativeFailure{};
        if (!path || std::string(path) != "disc0:/PSP_GAME/USRDIR/prx/audiocodec.prx" || flags != 1 || mode != 0 || !media_activated) {
            Emit("IoOpenAdapter", "SourceAdapter", 0x4F514, AdapterError, true, 0, ERROR_INVALID_PARAMETER);
            return AdapterError;
        }
        std::wstring native = options.media_root;
        if (!native.empty() && native.back() != L'/' && native.back() != L'\\') native += L'\\';
        native += L"PSP_GAME\\USRDIR\\prx\\audiocodec.prx";
        const auto opened = backend->OpenRead(native, flags, mode);
        int32_t fd = AdapterError;
        if (opened.status) {
            fd = NewSourceIdentity();
            if (fd <= 0) {
                const auto closed = backend->CloseFile(opened.handle);
                Emit("CloseRejectedIdentityFile", "HostLifecycle", 0, closed ? 1 : 0,
                     false, opened.host_handle, closed.win32_error);
            } else {
                file = opened.handle; file_native = opened.host_handle;
                file_runtime_owner = state.owner; encoded_file_size = opened.file_size;
                source_file_path = path;
            }
        }
        std::lock_guard<std::mutex> lock(metadata);
        if (fd >= 0) {
            state.source_fd = fd; state.file_live = true;
            state.file_native_object = opened.host_handle;
            state.file_backend_owner = file.owner;
            state.file_backend_generation = file.generation;
            state.file_backend_serial = file.serial;
            state.encoded_file_bytes = encoded_file_size;
        }
        EmitLocked("IoOpenAdapter", "SourceAdapter", 0x4F514, fd, true, opened.host_handle,
                   opened.status.win32_error, 0, "accepted WindowsStartupBackend::OpenRead;flags1/mode0");
        return fd;
    }
    recovered::StartupModuleOutcome ModuleStopResult(recovered::StartupModuleStop stop,
        const char* frontier, DWORD error = 0, uint32_t source_pc = 0x4F53C) {
        std::lock_guard<std::mutex> lock(metadata);
        state.module_binding_frontier = frontier;
        if (stop == recovered::StartupModuleStop::Unsupported) {
            state.barrier_source_pc = source_pc; state.worker_state = "Blocked";
            state.barrier = frontier;
        } else if (stop == recovered::StartupModuleStop::HostFailure) {
            FailLocked(Failure(StartupRuntimeCode::HostFailure, error));
        }
        EmitLocked("ModuleCallStop", stop == recovered::StartupModuleStop::Unsupported ? "Unsupported" : "HostLifecycle",
                   source_pc, 0, false, file_native, error, 0, frontier);
        return stop; //no source service result exists in this alternative
    }
    recovered::StartupModuleOutcome LoadModuleById(int32_t fd, uint32_t flags,
                               const recovered::StartupModuleOptions& option) override {
        using Stop = recovered::StartupModuleStop;
        if (HostCancelled()) return Stop::HostCancelled;
        {
        std::lock_guard<std::mutex> lock(metadata);
        if (!IsCurrent(true) || !file || fd < 0 || fd != state.source_fd ||
            !state.file_live || file_runtime_owner != state.owner ||
            file.owner != state.file_backend_owner || file.generation != state.file_backend_generation ||
            file.serial != state.file_backend_serial || flags != 0 ||
            option != recovered::StartupOptions4F59C(1) || pending_module.staging) throw NativeFailure{};
        pending_module.live = true; pending_module.fd = fd; pending_module.flags = flags;
        pending_module.owner = state.owner; pending_module.backend_file = file;
        pending_module.options = option; pending_module.path = source_file_path;
        state.pending_module_owner = pending_module.owner; state.pending_module_live = true;
        state.pending_module_flags = pending_module.flags; state.pending_module_path = pending_module.path;
        state.pending_module_options.assign(pending_module.options.begin(), pending_module.options.end());
        EmitLocked("LoadModuleByID-enter", "SourceCall", 0x4F53C, fd, false, file_native, 0, 0,
                   "flags0;owned pending options20/path;no completed source Load result");
        }
        if (encoded_file_size != EncodedModuleBytes)
            return ModuleStopResult(Stop::Unsupported, "Encoded audiocodec file extent differs from the validated input");
        const auto position = backend->Seek64(file, 0, 1);
        Emit("ModuleObserveFilePosition", "HostModuleInput", 0x4F53C, position.position,
             false, file_native, position.status.win32_error, 0, "same accepted-backend fd;whence1;no rewind");
        if (!position.status)
            return ModuleStopResult(Stop::HostFailure, "Module file-position observation failed", position.status.win32_error);
        { std::lock_guard<std::mutex> lock(metadata); state.staging_file_position = position.position; }
        if (position.position != 0)
            return ModuleStopResult(Stop::Unsupported, "Only actual fresh-open position0 is supported for module input");
        if (HostCancelled()) return Stop::HostCancelled;
        StartupPartitionRequest request;
        request.bytes = EncodedModuleBytes; request.source_base = HostStagingCoordinate;
        const auto allocation = backend->Allocate(request);
        if (!allocation.status)
            return ModuleStopResult(Stop::HostFailure, "Module staging allocation failed", allocation.status.win32_error);
        // Publish primitive ownership immediately. Backend remains the allocation owner.
        pending_module.staging = allocation.handle;
        {
            std::lock_guard<std::mutex> lock(metadata);
            state.staging_live = true; state.staging_owner = allocation.handle.owner;
            state.staging_generation = allocation.handle.generation; state.staging_serial = allocation.handle.serial;
            state.staging_host_address = allocation.host_address;
            state.staging_source_coordinate = allocation.source_base;
            EmitLocked("ModuleStagingAllocated", "HostModuleInput", 0, allocation.requested_bytes,
                       false, allocation.host_address, 0, 0, "61000000 is host staging coordinate;not PSP/module placement");
        }
        if (allocation.requested_bytes != EncodedModuleBytes || allocation.source_base != HostStagingCoordinate ||
            !allocation.live || !allocation.readable || !allocation.writable)
            return ModuleStopResult(Stop::HostFailure, "Module staging allocation geometry mismatch");
        StartupHostStatus borrowed;
        pending_module.staging_borrow = backend->Borrow(pending_module.staging, borrowed);
        if (!borrowed)
            return ModuleStopResult(Stop::HostFailure, "Module staging borrow failed", borrowed.win32_error);
        { std::lock_guard<std::mutex> lock(metadata); state.staging_borrow_live = true; }
        StartupRawBorrowSpan span;
        auto access = pending_module.staging_borrow.Span(span);
        if (!access || span.size != EncodedModuleBytes || !span.bytes || !span.initialized ||
            !span.readable || !span.writable || span.source_base != HostStagingCoordinate ||
            span.handle.owner != pending_module.staging.owner || span.handle.generation != pending_module.staging.generation ||
            span.handle.serial != pending_module.staging.serial)
            return ModuleStopResult(Stop::HostFailure, "Module staging lease geometry/identity failed", access.win32_error);
        if (HostCancelled()) return Stop::HostCancelled;
        const auto read = backend->ReadInto(file, pending_module.staging, 0, EncodedModuleBytes);
        {
            std::lock_guard<std::mutex> lock(metadata);
            state.staging_requested = read.requested; state.staging_transferred = read.transferred;
            state.staging_host_read = read.host_result; state.staging_read_error = read.status.win32_error;
            EmitLocked("ModuleReadInto", "HostModuleInput", 0x4F53C, read.transferred, false,
                       read.destination_host_address, read.status.win32_error, 0, StartupHostCodeName(read.status.code));
        }
        access = pending_module.staging_borrow.Span(span); //revalidate before every consumed range
        if (!access || span.size != EncodedModuleBytes || !span.bytes || !span.initialized ||
            !span.readable || span.source_base != HostStagingCoordinate ||
            span.host_address != allocation.host_address || span.handle.owner != allocation.handle.owner ||
            span.handle.generation != allocation.handle.generation || span.handle.serial != allocation.handle.serial)
            return ModuleStopResult(Stop::HostFailure, "Module lease changed after input read", access.win32_error);
        uint32_t known = 0;
        for (uint32_t i = 0; i < EncodedModuleBytes; ++i) if (span.initialized[i] == 1) ++known;
        { std::lock_guard<std::mutex> lock(metadata); state.staging_known_bytes = known; }
        if (!read.status || !read.host_result || read.requested != EncodedModuleBytes ||
            read.transferred != EncodedModuleBytes || read.file_position != 0 ||
            read.destination_host_address != span.host_address)
            return ModuleStopResult(Stop::HostFailure, "Module read failed/short/position or destination mismatch", read.status.win32_error);
        if (known != EncodedModuleBytes)
            return ModuleStopResult(Stop::HostFailure, "Module input contains unknown bytes");
        { std::lock_guard<std::mutex> lock(metadata); state.staging_full_read = true; }
        if (HostCancelled()) return Stop::HostCancelled;
        core::Sha256 hash;
        hash.update(span.bytes, EncodedModuleBytes); //direct one owned allocation;no payload mirror
        const auto digest = core::hex_digest(hash.digest());
        const auto* descriptor = recovered::FindAudiocodecModuleDescriptor(digest, EncodedModuleBytes);
        const bool identity_matches = descriptor != nullptr;
        {
            std::lock_guard<std::mutex> lock(metadata);
            state.staging_sha256 = digest;
            state.staging_hash_matches = identity_matches;
            EmitLocked("ModuleEncodedIdentity", "HostModuleInput", 0x4F53C, identity_matches ? 1 : 0,
                       false, span.host_address, 0, 0, digest);
        }
        if (!identity_matches)
            return ModuleStopResult(Stop::Unsupported, "Actual encoded module identity has no supported native definition");
        if (HostCancelled()) return Stop::HostCancelled;
        // Exact identity selects recovered metadata only. No runtime decode,
        // mapping, relocation, import binding or module entry has occurred.
        const auto& first_library = descriptor->imports.front();
        const auto& first_function = descriptor->functions[first_library.first_function];
        Emit("ModuleRecoveredDescriptor", "RecoveredMetadata", 0x4F53C,
             static_cast<int64_t>(descriptor->functions.size()), false, 0, 0, 0, descriptor->name);
        Emit("ModulePlaintextIdentityMetadata", "RecoveredMetadata", 0x4F53C,
             descriptor->plaintext_bytes, false, 0, 0, 0, descriptor->plaintext_sha256);
        Emit("ModuleFirstRecordedImport", "RecoveredMetadata", 0x4F53C,
             first_function.nid, false, 0, 0, 0, first_library.name);
        Emit("ModuleRecordedImportAttributes", "RecoveredMetadata", 0x4F53C,
             first_library.attributes, false, 0, 0, 0,
             "all3 records:version0/words5/variables0;later660 weak bit8;original firmware policy unknown");
        // Table order does not establish first execution or PSP eager failure.
        // The actual pending fd/options/borrow stay owned across this barrier.
        return ModuleStopResult(Stop::Unsupported,
            "Native audiocodec binding policy/providers not implemented;recorded sceMeAudio_driver/C00D873B;attributes0x0009;8 imports;original PSP policy unknown");
    }
    recovered::StartupModuleOutcome StartModule(int32_t uid, uint32_t bytes, const void* args,
        int32_t* status, const void* options_arg) override {
        if (HostCancelled()) return recovered::StartupModuleStop::HostCancelled;
        {
            std::lock_guard<std::mutex> lock(metadata);
            state.module_start_called = true;
            if (!IsCurrent(true) || bytes || args || status || options_arg ||
                !state.module_uid_assigned || uid != state.module_uid)
                throw NativeFailure{};
        }
        return ModuleStopResult(recovered::StartupModuleStop::Unsupported,
            "No supported native module Start/entry contract", 0, 0x4F560);
    }
    recovered::StartupModuleOutcome CloseModuleFile(int32_t fd) override {
        if (HostCancelled()) return recovered::StartupModuleStop::HostCancelled;
        {
            std::lock_guard<std::mutex> lock(metadata);
            if (!IsCurrent(true) || !file || !state.file_live || fd != state.source_fd ||
                file_runtime_owner != state.owner || file.owner != state.file_backend_owner ||
                file.generation != state.file_backend_generation || file.serial != state.file_backend_serial)
                throw NativeFailure{};
        }
        const auto closed = backend->CloseFile(file);
        if (closed) { file = {}; std::lock_guard<std::mutex> lock(metadata); state.file_live = false; }
        Emit("SourceCloseModuleFile", "SourceAdapter", 0x4F570, closed ? 0 : AdapterError,
             true, file_native, closed.win32_error, 0, "source ignores returned close value;module resources independent");
        return recovered::StartupModuleReturned{closed ? 0 : AdapterError};
    }
    void UnsupportedFollowingModule() override {
        (void)ModuleStopResult(recovered::StartupModuleStop::Unsupported,
            "Next libatrac3plus native module operation is independently unsupported", 0, 0x4F640);
    }
    bool HostCancelled() const override { return cancelled.load(); }
    void BarrierPump() {
        HANDLE notification = nullptr;
        { std::lock_guard<std::mutex> lock(metadata); if (callback) notification = callback->notification; }
        for (;;) {
            // Exit notification precedes cancellation if both were signalled by Close.
            HANDLE handles[2] = {notification ? notification : exit_event, cancel};
            const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            const DWORD error = wait == WAIT_FAILED ? GetLastError() : 0;
            Emit("WorkerBarrierWait", "HostBarrierPump", 0, 0, false, 0, error, wait);
            if (wait == WAIT_OBJECT_0) {
                (void)Dispatch(recovered::StartupDispatchOrigin::HostBarrierPump);
                if (HostCancelled()) return;
                if (!notification) { //no registered callback; an exit request still cancels host lifetime.
                    cancelled.store(true); (void)SetEvent(cancel); return;
                }
            } else if (wait == WAIT_OBJECT_0 + 1) return;
            else throw NativeFailure{};
        }
    }
    StartupRuntimeStatus QueueExitLocked() {
        state.exit_requested = true;
        notifications = (std::min)(notifications + 1u, uint32_t((std::numeric_limits<int>::max)()));
        const BOOL signaled = SetEvent(exit_event); DWORD error = signaled ? 0 : GetLastError();
        EmitLocked("SetEvent-exit", "HostNotification", 0, signaled, false, Address(exit_event), error,
                   0, "coalesced session-exit notifications;arg2=0;commonNULL;no PSP timing claim");
        if (callback && callback->registered) {
            const BOOL notified = SetEvent(callback->notification); const DWORD notify_error = notified ? 0 : GetLastError();
            EmitLocked("SetEvent-callback", "HostNotification", 0, notified, false,
                       Address(callback->notification), notify_error);
            if (!notified) error = notify_error;
        }
        return error ? Failure(StartupRuntimeCode::HostFailure, error) : StartupRuntimeStatus{};
    }
    StartupRuntimeStatus RequestExit() {
        std::lock_guard<std::mutex> lock(metadata);
        if (state.resources_closed || state.closing) return Failure(StartupRuntimeCode::Closed);
        if (!started) return Failure(StartupRuntimeCode::NotStarted);
        return QueueExitLocked();
    }
    bool CloseHandleOwned(HANDLE& handle, const char* operation, bool invalid_sentinel = false) {
        if (!handle || handle == INVALID_HANDLE_VALUE) return true;
        const HANDLE original = handle;
        const BOOL closed = CloseHandle(original); const DWORD error = closed ? 0 : GetLastError();
        if (closed) handle = invalid_sentinel ? INVALID_HANDLE_VALUE : nullptr;
        Emit(operation, "HostLifecycle", 0, closed, false, Address(original), error);
        return closed != FALSE;
    }
    StartupRuntimeStatus Close(uint32_t timeout) {
        {
            std::lock_guard<std::mutex> lock(metadata);
            if (state.resources_closed) return {};
            if (close_in_progress) return Failure(StartupRuntimeCode::Busy);
            if (GetCurrentThreadId() == state.caller_thread_id || GetCurrentThreadId() == state.worker_thread_id)
                return Failure(StartupRuntimeCode::WrongThread);
            close_in_progress = true;
            if (started) (void)QueueExitLocked();
            state.closing = true;
        }
        struct Admission {
            Impl& self;
            ~Admission() { std::lock_guard<std::mutex> lock(self.metadata); self.close_in_progress = false; }
        } admission{*this};
        cancelled.store(true);
        if (cancel) {
            const BOOL signaled = SetEvent(cancel); const DWORD error = signaled ? 0 : GetLastError();
            Emit("SetEvent-cancel", "HostLifecycle", 0, signaled, false, Address(cancel), error);
            if (!signaled) return Failure(StartupRuntimeCode::HostFailure, error);
        }
        // A source StartThread failure can leave a suspended real object. Resume
        // solely for native cancellation; thunk checks cancel BEFORE source entry.
        for (unsigned i = 0; i != 2; ++i) {
            HANDLE handle = i ? worker : caller;
            bool& resumed = i ? worker_resumed : caller_resumed;
            if (handle && !resumed) {
                const DWORD previous = ResumeThread(handle); const DWORD error = previous == DWORD(-1) ? GetLastError() : 0;
                Emit("ResumeForHostCancellation", "HostLifecycle", 0, previous, false, Address(handle), error);
                if (previous == DWORD(-1)) return Failure(StartupRuntimeCode::HostFailure, error);
                resumed = true;
            }
        }
        HANDLE handles[2]; DWORD count = 0;
        if (caller) handles[count++] = caller;
        if (worker) handles[count++] = worker;
        if (count) {
            const DWORD wait = WaitForMultipleObjects(count, handles, TRUE, timeout);
            const DWORD error = wait == WAIT_FAILED ? GetLastError() : 0;
            Emit("JoinCallerAndWorker", "HostLifecycle", 0, count, false, 0, error, wait);
            if (wait != WAIT_OBJECT_0) {
                const auto status = Failure(wait == WAIT_TIMEOUT ? StartupRuntimeCode::Timeout : StartupRuntimeCode::HostFailure, error, wait);
                std::lock_guard<std::mutex> lock(metadata); state.status = status; return status;
            }
            std::lock_guard<std::mutex> lock(metadata);
            if (caller) state.caller_joined = true;
            if (worker) state.worker_joined = true;
        }
        // Both contexts have ended. No thread can now access or wait on these objects.
        bool ok = true;
        { std::lock_guard<std::mutex> lock(metadata);
          pending_module.live = false; state.pending_module_live = false; }
        if (pending_module.staging) {
            // Both actual contexts joined before releasing the retained input lease.
            pending_module.staging_borrow.Reset();
            { std::lock_guard<std::mutex> lock(metadata); state.staging_borrow_live = false; }
            Emit("ModuleStagingBorrowReset", "HostLifecycle", 0, 1, false, state.staging_host_address);
            const auto released = backend->Free(pending_module.staging);
            if (released) pending_module.staging = {}; //primitive retirement before telemetry
            {
                std::lock_guard<std::mutex> lock(metadata);
                if (released) {
                    state.staging_live = false;
                    state.staging_freed_after_joins = state.caller_joined && state.worker_joined;
                } else ok = false;
                EmitLocked("FreeModuleStaging", "HostLifecycle", 0, released ? 1 : 0, false,
                           state.staging_host_address, released.win32_error, 0, StartupHostCodeName(released.code));
            }
        }
        if (file) {
            const auto closed = backend->CloseFile(file);
            if (closed) { file = {}; std::lock_guard<std::mutex> lock(metadata); state.file_live = false; }
            else ok = false;
            Emit("HostCloseSourceFile", "HostLifecycle", 0, closed ? 1 : 0, false, file_native, closed.win32_error,
                 0, "owner teardown;source4F570 was never reached");
        }
        if (callback) {
            { std::lock_guard<std::mutex> lock(metadata); callback->registered = false; notifications = 0; }
            if (!CloseHandleOwned(callback->notification, "CloseCallbackEvent")) ok = false;
            if (!callback->notification) {
                std::lock_guard<std::mutex> lock(metadata); callback.reset(); state.callback_live = false;
                EmitLocked("RetireCallbackRegistry", "HostLifecycle", 0, state.callback_uid, false, 0, 0, 0,
                           "native cleanup only;no source DeleteCallback/unregister");
            }
        }
        if (!CloseHandleOwned(media, "CloseMediaDirectory", true)) ok = false;
        { std::lock_guard<std::mutex> lock(metadata); state.media_live = media != INVALID_HANDLE_VALUE; }
        if (!CloseHandleOwned(caller, "CloseCallerThread")) ok = false;
        if (!CloseHandleOwned(worker, "CloseWorkerThread")) ok = false;
        if (!CloseHandleOwned(exit_event, "CloseExitEvent")) ok = false;
        if (!CloseHandleOwned(cancel, "CloseCancelEvent")) ok = false;
        if (tls_index != TLS_OUT_OF_INDEXES) {
            const BOOL freed = TlsFree(tls_index); const DWORD error = freed ? 0 : GetLastError();
            Emit("TlsFree", "HostTLS", 0, freed, false, tls_index, error);
            if (freed) tls_index = TLS_OUT_OF_INDEXES; else ok = false;
        }
        if (backend && !pending_module.staging) {
            const auto closed = backend->Close();
            Emit("CloseAcceptedBackend", "HostLifecycle", 0, closed ? 1 : 0, false, 0, closed.win32_error);
            if (!closed) ok = false;
        }
        std::lock_guard<std::mutex> lock(metadata);
        state.resources_closed = ok;
        state.status = ok ? StartupRuntimeStatus{} : Failure(StartupRuntimeCode::HostFailure);
        EmitLocked("RuntimeClose", "HostLifecycle", 0, ok ? 1 : 0);
        return state.status;
    }
};
thread_local StartupCoordinator::Impl::ThreadContext* StartupCoordinator::Impl::current_context = nullptr;

StartupCoordinator::StartupCoordinator(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
StartupCoordinator::~StartupCoordinator() {
    if (!impl_) return;
    try { if (impl_->Close(INFINITE)) return; } catch (...) {}
    // Hard OS wait/resume/cleanup failure cannot authorize freeing a live context.
    // Retain the outstanding owner. Normal timeout destruction cooperatively joins.
    (void)impl_.release();
}
std::unique_ptr<StartupCoordinator> StartupCoordinator::Create(const StartupRuntimeOptions& options,
                                                             StartupRuntimeStatus& status) {
    status = {};
    // Allocate the RAII coordinator BEFORE acquiring any native resources.
    auto owner = std::unique_ptr<StartupCoordinator>(
        new StartupCoordinator(std::make_unique<Impl>(options)));
    auto& impl = owner->impl_;
    StartupHostStatus backend_status;
    impl->backend = WindowsStartupBackend::Create(backend_status);
    if (!impl->backend) { status = Failure(StartupRuntimeCode::HostFailure, backend_status.win32_error); return {}; }
    impl->tls_index = TlsAlloc();
    const DWORD tls_error = impl->tls_index == TLS_OUT_OF_INDEXES ? GetLastError() : 0;
    impl->Emit("TlsAlloc", "HostTLS", 0, impl->tls_index != TLS_OUT_OF_INDEXES, false, impl->tls_index,
               tls_error);
    if (impl->tls_index == TLS_OUT_OF_INDEXES) {
        status = Failure(StartupRuntimeCode::HostFailure, tls_error); (void)impl->Close(INFINITE); return {};
    }
    impl->cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD error = impl->cancel ? 0 : GetLastError();
    impl->Emit("CreateEventW-cancel", "HostLifecycle", 0, impl->cancel != nullptr, false, Address(impl->cancel), error);
    if (impl->cancel) {
        impl->exit_event = CreateEventW(nullptr, TRUE, FALSE, nullptr); error = impl->exit_event ? 0 : GetLastError();
        impl->Emit("CreateEventW-exit", "HostLifecycle", 0, impl->exit_event != nullptr, false, Address(impl->exit_event), error);
    }
    if (!impl->cancel || !impl->exit_event) {
        status = Failure(StartupRuntimeCode::HostFailure, error); (void)impl->Close(INFINITE); return {};
    }
    return owner;
}
StartupRuntimeStatus StartupCoordinator::Start() {
    auto& self = *impl_;
    std::lock_guard<std::mutex> lock(self.metadata);
    if (self.state.resources_closed || self.state.closing) return Failure(StartupRuntimeCode::Closed);
    if (self.started) return Failure(StartupRuntimeCode::AlreadyStarted);
    self.started = true;
    unsigned id = 0; const uintptr_t native = self.Begin(false, 0, id);
    if (!native) return self.state.status;
    self.caller = reinterpret_cast<HANDLE>(native); self.state.caller_thread_id = id;
    self.state.caller_state = "Suspended";
    const DWORD previous = ResumeThread(self.caller); const DWORD error = previous == DWORD(-1) ? GetLastError() : 0;
    self.EmitLocked("ResumeNativeCaller", "HostThread", 0, previous, false, Address(self.caller), error);
    if (previous == DWORD(-1)) { self.FailLocked(Failure(StartupRuntimeCode::HostFailure, error)); return self.state.status; }
    self.caller_resumed = true; return {};
}
StartupObservation StartupCoordinator::Observe(uint64_t after_event) const {
    const auto& self = *impl_;
    std::lock_guard<std::mutex> lock(self.metadata);
    auto out = self.state;
    for (const auto& event : self.events) if (event.ordinal > after_event) out.events.push_back(event);
    return out;
}
StartupRuntimeStatus StartupCoordinator::RequestExit() { return impl_->RequestExit(); }
StartupRuntimeStatus StartupCoordinator::Close(uint32_t timeout_ms) { return impl_->Close(timeout_ms); }
const char* StartupRuntimeCodeName(StartupRuntimeCode code) {
    switch (code) {
    case StartupRuntimeCode::Ok: return "Ok";
    case StartupRuntimeCode::AlreadyStarted: return "AlreadyStarted";
    case StartupRuntimeCode::NotStarted: return "NotStarted";
    case StartupRuntimeCode::Closed: return "Closed";
    case StartupRuntimeCode::HostFailure: return "HostFailure";
    case StartupRuntimeCode::Timeout: return "Timeout";
    case StartupRuntimeCode::InvalidUid: return "InvalidUid";
    case StartupRuntimeCode::WrongThread: return "WrongThread";
    case StartupRuntimeCode::Busy: return "Busy";
    }
    return "Unknown";
}
} // namespace dokodemo
