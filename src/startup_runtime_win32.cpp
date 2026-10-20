#include "startup_runtime_win32.h"
#include "startup_memory_backend_win32.h"
#include "native_codec_workspace_win32.h"
#include "sascore_runtime_win32.h"
#include "memab_runtime_win32.h"
#include "recovered/startup_flow_native.h"
#include "recovered/audiocodec_module_native.h"
#include "recovered/module_manager_native.h"
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
#include <type_traits>
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
constexpr uint32_t HostStagingCoordinate = 0x61000000;
// The manager retains this SAME direct borrow, not a vector copy or an opaque
// success proof. Primitive block ownership stays in each request for explicit
// fallible Free after manager unload and last borrow release. No destructor Free.
struct WindowsModuleInputLease final : recovered::NativeModuleInputLease {
    StartupAllocationBorrow borrow;
    StartupNativeBlock block;
    uint32_t bytes = 0, coordinate = 0;
    bool View(recovered::NativeModuleInputView& out) const noexcept override {
        try {
            StartupRawBorrowSpan span;
            if (!borrow.Span(span) || !span.readable || span.size != bytes ||
                span.source_base != coordinate || span.handle.owner != block.owner ||
                span.handle.generation != block.generation || span.handle.serial != block.serial)
                return false;
            out.bytes = span.bytes; out.initialized = span.initialized;
            out.owner = span.handle.owner; out.generation = span.handle.generation; out.serial = span.handle.serial;
            out.host_address = span.host_address; out.source_coordinate = span.source_base;
            out.size = bytes; return true;
        } catch (...) { return false; }
    }
};
}

struct StartupCoordinator::Impl final : recovered::StartupFlowServices,
    recovered::GameInputServices, recovered::GameMemoryServices {
    std::unique_ptr<Impl> next_deferred_codec_cleanup;
    struct JoinedCleanupOwners {
        SRWLOCK lock=SRWLOCK_INIT;
        std::unique_ptr<Impl> head;
    };
    static JoinedCleanupOwners& JoinedOwners() { static JoinedCleanupOwners owners;return owners; }
    static void RetainJoined(std::unique_ptr<Impl> owner) noexcept {
        auto& owners=JoinedOwners();AcquireSRWLockExclusive(&owners.lock);
        owner->next_deferred_codec_cleanup=std::move(owners.head);owners.head=std::move(owner);
        ReleaseSRWLockExclusive(&owners.lock);
    }
    static uint32_t PendingJoined() noexcept {
        auto& owners=JoinedOwners();AcquireSRWLockExclusive(&owners.lock);uint32_t n=0;
        for(auto* p=owners.head.get();p;p=p->next_deferred_codec_cleanup.get()) ++n;
        ReleaseSRWLockExclusive(&owners.lock);return n;
    }
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
    std::shared_ptr<WindowsStartupBackend> backend;
    std::shared_ptr<recovered::NativeCodecSessionState> codec_session=std::make_shared<recovered::NativeCodecSessionState>();
    std::shared_ptr<recovered::NativeCodecWorkspaceBackend> codec_backend;
    std::shared_ptr<recovered::NativeSascoreBackend> sascore_backend;
    std::shared_ptr<recovered::NativeMemabBackend> memab_backend;
    mutable SRWLOCK repeated_lock=SRWLOCK_INIT;
    recovered::StartupRepeatedModuleSnapshot repeated_state;
    recovered::ControllerState controller;
    std::array<StartupNativeBlock, 3> game_blocks{};
    std::array<StartupAllocationBorrow, 3> game_borrows{};
    std::array<recovered::GameArena, 3> game_arenas{};
    recovered::NativeModuleManager modules;
    std::unique_ptr<CallbackRecord> callback;
    ThreadContext caller_context{this, false}, worker_context{this, true};
    HANDLE caller = nullptr, worker = nullptr, cancel = nullptr, exit_event = nullptr;
    HANDLE media = INVALID_HANDLE_VALUE;
    DWORD tls_index = TLS_OUT_OF_INDEXES;
    bool started = false, caller_resumed = false, worker_resumed = false, close_in_progress = false;
    bool media_activated = false;
    uint32_t notifications = 0;
    int32_t worker_uid = 0;
    struct PendingModuleRequest {
        StartupModuleObservation data;
        StartupNativeFile backend_file;
        recovered::StartupModuleOptions options{};
        std::wstring native_path;
        uint32_t input_bytes = 0;
        StartupNativeBlock staging;
        StartupAllocationBorrow staging_borrow;
        std::weak_ptr<WindowsModuleInputLease> input;
        recovered::NativeModuleProviderToken check_need_mem_provider;
    };
    // Prepare and insert each owner BEFORE acquiring its file/allocation. The
    // vector can move unique_ptrs; callbacks never retain vector references.
    std::vector<std::unique_ptr<PendingModuleRequest>> module_requests;
    uint32_t event_module_index = StartupNoCurrentModule;

    explicit Impl(const StartupRuntimeOptions& input) : options(input) {
        state.owner = next_runtime_owner.fetch_add(1);
        state.module_manager_owner = modules.Owner();
        state.module_generation = modules.Generation();
        codec_session->next_coordinate.store(HostStagingCoordinate);
    }
    void EmitLocked(const char* operation, const char* origin, uint32_t pc = 0,
                    int64_t value = 0, bool returned = false, uint64_t object = 0,
                    DWORD error = 0, DWORD wait = 0, std::string_view detail = {},
                    int crt_error = 0, unsigned long dos_error = 0, bool module_relative = false,
                    recovered::NativeModuleToken source_module = {}) noexcept {
        try {
        StartupRuntimeEvent event;
        event.ordinal = ++state.last_event; event.owner = state.owner;
        event.operation = operation; event.origin = origin; event.source_pc = pc;
        event.thread = GetCurrentThreadId(); event.value = value;
        event.source_return = returned; event.native_object = object;
        event.source_pc_domain = !pc ? "host-only" : module_relative ? "module-relative" : "game-module-relative";
        if (event_module_index < module_requests.size()) {
            event.module_request_index = event_module_index;
            event.module = module_requests[event_module_index]->data.token;
        }
        event.source_module = source_module.owner ? source_module :
            module_relative ? event.module : recovered::NativeModuleToken{};
        event.win32_error = error; event.wait_result = wait;
        if (!detail.empty()) event.detail.assign(detail.data(), detail.size());
        event.crt_errno = crt_error; event.dos_errno = dos_error;
        if (events.size() == 1024) { events.pop_front(); ++state.dropped_events; }
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
        codec_session->cancelled.store(true);
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
    PendingModuleRequest* CurrentModule() const noexcept {
        const auto index = state.current_module_index;
        return index < module_requests.size() ? module_requests[index].get() : nullptr;
    }
    bool FileMatches(const PendingModuleRequest& request) const noexcept {
        const auto& data = request.data;
        return request.backend_file && data.file_live && data.request_owner == state.owner &&
            request.backend_file.owner == data.file_backend_owner &&
            request.backend_file.generation == data.file_backend_generation &&
            request.backend_file.serial == data.file_backend_serial;
    }
    recovered::StartupRepeatedModuleSnapshot RepeatedSnapshot() const noexcept {
        static_assert(std::is_nothrow_copy_constructible_v<recovered::StartupRepeatedModuleSnapshot>);
        AcquireSRWLockShared(&repeated_lock);
        auto copy=repeated_state;
        ReleaseSRWLockShared(&repeated_lock);return copy;
    }
    void ObserveRepeatedModules(const recovered::StartupRepeatedModuleSnapshot& value,uint32_t pc) noexcept override {
        static_assert(std::is_nothrow_copy_assignable_v<recovered::StartupRepeatedModuleSnapshot>);
        AcquireSRWLockExclusive(&repeated_lock);
        repeated_state=value; // authoritative fixed state BEFORE optional telemetry; no loss on contention
        ReleaseSRWLockExclusive(&repeated_lock);
        try {
            std::lock_guard<std::mutex> lock(metadata); // no dedicated lock held; no lock-order cycle
            const char* operation="RepeatedModuleSourceStep";int64_t observed=0;bool returned=false;
            switch(pc) {
            case 0x4F700:operation="RepeatedModuleTableBound";observed=value.known_bytes;break;
            case 0x4F704:case 0x4F740: {
                operation="RepeatedModuleKindRead";const auto offset=value.current_record_index*12+4;
                if(offset<value.bytes.size()) observed=value.bytes[offset];break;
            }
            case 0x4F708:operation="RepeatedModulePathRead";observed=value.current_record_index;break;
            case 0x4F724:operation="RepeatedModuleCounterIncrement";observed=value.counter;break;
            case 0x4F728:operation="RepeatedModuleUidStore";if(value.writes_count) observed=value.writes[value.writes_count-1].value;break;
            case 0x4F72C:operation="RepeatedModuleLimitCompare";observed=int32_t(value.counter)<4?1:0;break;
            case 0x4F730:operation="RepeatedModulePointerAdvance";observed=value.current_record_index;break;
            case 0x4F734:operation="RepeatedModuleFailureBranch";observed=value.writes_count && value.writes[value.writes_count-1].value==0xFFFFFFFFu?1:0;break;
            case 0x4F738:operation="RepeatedModuleFailureDelayClear";break;
            case 0x4F73C:operation="RepeatedModuleLoopBranchLikely";observed=int32_t(value.counter)<4?1:0;break;
            case 0x4F744:operation="RepeatedModuleSuccessFlag";observed=1;break;
            case 0x4F758:operation="RepeatedModuleGateReturn";observed=value.source_gate_result;returned=value.source_gate_returned;break;
            }
            EmitLocked(operation,"RecoveredGameCaller",pc,observed,returned,0,0,0,
                       "fixed source state retained separately;register counter distinct from table UID store;symbolic source coordinates");
        } catch(...) {} // telemetry cannot erase the persistent counter/table effects
    }
    void BeginModuleRequest(uint32_t callsite, const char* path,
                            const recovered::StartupModuleOptions& option) override {
        if (!IsCurrent(true) || !path) throw NativeFailure{};
        auto request = std::make_unique<PendingModuleRequest>();
        request->data.path = path;
        request->options = option;
        request->data.options.assign(option.begin(), option.end());
        request->data.wrapper_callsite = callsite;
        request->data.request_owner = state.owner;
        request->data.pending = true;
        // These exact path/size/request facts are source metadata. Entry/Load
        // support is separately decided by the manager's exact association.
        const std::string_view source_path(path);
        const wchar_t* relative = nullptr;
        if (source_path == "disc0:/PSP_GAME/USRDIR/prx/audiocodec.prx" && callsite == 0x4F61C &&
            option == recovered::StartupOptions4F59C(1)) {
            relative = L"PSP_GAME\\USRDIR\\prx\\audiocodec.prx"; request->input_bytes = 3232;
        } else if (source_path == "disc0:/PSP_GAME/USRDIR/prx/libatrac3plus.prx" && callsite == 0x4F640 &&
                   option == recovered::StartupOptions4F59C(0)) {
            relative = L"PSP_GAME\\USRDIR\\prx\\libatrac3plus.prx"; request->input_bytes = 19524;
        } else if (source_path == "disc0:/PSP_GAME/USRDIR/prx/sc_sascore.prx" && callsite == 0x4F660 &&
                   option == recovered::StartupOptions4F59C(1) && options.prepared_codec_envelope) {
            relative = L"PSP_GAME\\USRDIR\\prx\\sc_sascore.prx";
            request->input_bytes = 5920;
        } else if (source_path == "disc0:/PSP_GAME/USRDIR/prx/ifhandle.prx" && callsite == 0x4F680 &&
                   option == recovered::StartupOptions4F59C(1) && options.prepared_codec_envelope) {
            relative = L"PSP_GAME\\USRDIR\\prx\\ifhandle.prx";
            request->input_bytes = 10832;
        } else if (source_path == "disc0:/PSP_GAME/USRDIR/prx/pspnet.prx" && callsite == 0x4F6A0 &&
                   option == recovered::StartupOptions4F59C(0) && options.prepared_codec_envelope) {
            // Exact fifth input is already ELF; body association is manager-owned.
            relative = L"PSP_GAME\\USRDIR\\prx\\pspnet.prx";
            request->input_bytes = 60256;
        } else if (source_path == "disc0:/PSP_GAME/USRDIR/prx/memab.prx" && callsite == 0x4F71C &&
                   option == recovered::StartupOptions4F59C(1) && options.prepared_codec_envelope) {
            const auto repeated=RepeatedSnapshot();
            if(!repeated.present || !repeated.counter_known || repeated.counter || repeated.current_record_index || repeated.writes_count)
                throw NativeFailure{};
            relative = L"PSP_GAME\\USRDIR\\prx\\memab.prx";
            request->input_bytes=15216;request->data.repeated_record_index=0;
        } else if(source_path=="disc0:/PSP_GAME/USRDIR/prx/pspnet_adhoc_auth.prx" && callsite==0x4F71C &&
                  option==recovered::StartupOptions4F59C(1) && options.prepared_codec_envelope) {
            const auto repeated=RepeatedSnapshot();
            if(!repeated.present || !repeated.counter_known || repeated.counter!=1 || repeated.current_record_index!=1 ||
                repeated.writes_count!=1 || repeated.source_gate_returned) throw NativeFailure{};
            relative=L"PSP_GAME\\USRDIR\\prx\\pspnet_adhoc_auth.prx";
            request->input_bytes=10816;request->data.repeated_record_index=1;
        } else if(source_path=="disc0:/PSP_GAME/USRDIR/prx/pspnet_adhoc.prx" && callsite==0x4F71C &&
                  option==recovered::StartupOptions4F59C(0) && options.prepared_codec_envelope) {
            const auto repeated=RepeatedSnapshot();
            if(!repeated.present || !repeated.counter_known || repeated.counter!=2 || repeated.current_record_index!=2 ||
                repeated.writes_count!=2 || repeated.source_gate_returned || repeated.known_bytes!=36) throw NativeFailure{};
            relative=L"PSP_GAME\\USRDIR\\prx\\pspnet_adhoc.prx";
            request->input_bytes=40140;request->data.repeated_record_index=2;
        } else if(source_path=="disc0:/PSP_GAME/USRDIR/prx/pspnet_adhocctl.prx" && callsite==0x4F71C &&
                  option==recovered::StartupOptions4F59C(0) && options.prepared_codec_envelope) {
            const auto repeated=RepeatedSnapshot();
            if(!repeated.present || !repeated.counter_known || repeated.counter!=3 || repeated.current_record_index!=3 ||
                repeated.writes_count!=3 || repeated.source_gate_returned || repeated.known_bytes!=48) throw NativeFailure{};
            relative=L"PSP_GAME\\USRDIR\\prx\\pspnet_adhocctl.prx";
            request->input_bytes=36416;request->data.repeated_record_index=3;
        } else throw NativeFailure{};
        request->native_path = options.media_root;
        if (!request->native_path.empty() && request->native_path.back() != L'/' && request->native_path.back() != L'\\')
            request->native_path += L'\\';
        request->native_path += relative;
        std::lock_guard<std::mutex> lock(metadata);
        if (module_requests.size() >= StartupNoCurrentModule) throw NativeFailure{};
        request->data.request_index = static_cast<uint32_t>(module_requests.size());
        module_requests.push_back(std::move(request)); // before any OS acquisition
        state.current_module_index = static_cast<uint32_t>(module_requests.size() - 1);
        event_module_index = state.current_module_index;
        state.current_frontier_source_pc = callsite;
        state.current_frontier_module_relative = false;
        state.current_frontier_source_module = {};
        try { state.current_frontier = "Before source wrapper Open"; } catch (...) { ++state.dropped_events; }
        EmitLocked("ModuleRequestOwned", "HostModuleRequest", callsite, state.current_module_index,
                   false, state.owner, 0, 0, path);
    }
    int32_t OpenRead(const char* path, uint32_t flags, uint32_t mode) override {
        auto* request = CurrentModule();
        if (!IsCurrent(true) || !request || !path || request->data.path != path || flags != 1 || mode != 0 ||
            !media_activated || request->backend_file) throw NativeFailure{};
        // Exhaustion must not acquire a file needing an unowned corrective Close.
        const int32_t identity = NewSourceIdentity();
        if (identity <= 0) return AdapterError;
        const auto opened = backend->OpenRead(request->native_path, flags, mode);
        if (opened.status) request->backend_file = opened.handle; // first, nonthrowing primitive publication
        std::lock_guard<std::mutex> lock(metadata);
        auto& data = request->data;
        if (opened.status) {
            data.source_fd = identity; data.file_live = true;
            data.file_native_object = opened.host_handle; data.encoded_file_bytes = opened.file_size;
            data.file_backend_owner = opened.handle.owner;
            data.file_backend_generation = opened.handle.generation; data.file_backend_serial = opened.handle.serial;
        }
        EmitLocked("IoOpenAdapter", "SourceAdapter", 0x4F514, opened.status ? identity : AdapterError,
                   true, opened.host_handle, opened.status.win32_error, 0,
                   "accepted WindowsStartupBackend::OpenRead;separate request owner;flags1/mode0");
        return opened.status ? identity : AdapterError;
    }
    recovered::StartupModuleOutcome ModuleStopResult(recovered::StartupModuleStop stop,
        const char* frontier, DWORD error = 0, uint32_t source_pc = 0x4F53C, bool module_relative = false,
        recovered::NativeModuleToken source_module = {}) {
        std::lock_guard<std::mutex> lock(metadata);
        auto* request = CurrentModule();
        state.current_frontier_source_pc = source_pc;
        state.current_frontier_module_relative = module_relative;
        if (!source_module.owner && module_relative && request) source_module = request->data.token;
        state.current_frontier_source_module = source_module;
        if (request) {
            request->data.frontier_source_pc = source_pc;
            request->data.frontier_module_relative = module_relative;
            request->data.frontier_source_module = source_module;
        }
        if (stop == recovered::StartupModuleStop::Unsupported) {
            state.barrier_source_pc = source_pc; state.barrier_module_relative = module_relative;
            state.barrier_source_module = source_module;
        }
        else if (stop == recovered::StartupModuleStop::HostFailure)
            FailLocked(Failure(StartupRuntimeCode::HostFailure, error));
        try {
            state.current_frontier = frontier;
            if (request) request->data.frontier = frontier;
            // Legacy binding frontier aliases request0, not the active last request.
            if (request && request->data.request_index == 0) state.module_binding_frontier = frontier;
            if (stop == recovered::StartupModuleStop::Unsupported) {
                state.worker_state = "Blocked"; state.barrier = frontier;
            }
        } catch (...) { ++state.dropped_events; }
        EmitLocked("ModuleCallStop", stop == recovered::StartupModuleStop::Unsupported ? "Unsupported" : "HostLifecycle",
                   source_pc, 0, false, request ? request->data.file_native_object : 0, error, 0, frontier, 0, 0,
                   module_relative, source_module);
        return stop; // no source service v0 or entry result
    }
    recovered::StartupModuleOutcome LoadModuleById(int32_t fd, uint32_t flags,
                               const recovered::StartupModuleOptions& option) override {
        using Stop = recovered::StartupModuleStop;
        if (HostCancelled()) return Stop::HostCancelled;
        auto* request = CurrentModule();
        if (!IsCurrent(true) || !request || !FileMatches(*request) || fd < 0 || fd != request->data.source_fd || flags != 0 ||
            option != request->options || request->staging || request->data.module_uid_assigned) throw NativeFailure{};
        auto& data = request->data;
        { std::lock_guard<std::mutex> lock(metadata); data.flags = flags;
          EmitLocked("LoadModuleByID-enter", "SourceCall", 0x4F53C, fd, false, data.file_native_object,
                     0, 0, "flags0;owned pending options20/path;no completed source Load result"); }
        if(!request->input_bytes)
            return ModuleStopResult(Stop::Unsupported,"Connected next-module Load has no recovered descriptor/body;actual Open retained;no ReadInto or UID");
        if (data.encoded_file_bytes != request->input_bytes)
            return ModuleStopResult(Stop::Unsupported, "Module file extent differs from the validated exact input");
        const auto position = backend->Seek64(request->backend_file, 0, 1);
        Emit("ModuleObserveFilePosition", "HostModuleInput", 0x4F53C, position.position,
             false, data.file_native_object, position.status.win32_error, 0, "same accepted-backend fd;whence1;no rewind");
        if (!position.status)
            return ModuleStopResult(Stop::HostFailure, "Module file-position observation failed", position.status.win32_error);
        { std::lock_guard<std::mutex> lock(metadata); data.staging_file_position = position.position; }
        if (position.position != 0)
            return ModuleStopResult(Stop::Unsupported, "Only actual fresh-open position0 is supported for module input");
        if (HostCancelled()) return Stop::HostCancelled;
        SYSTEM_INFO system{}; GetSystemInfo(&system);
        const uint64_t page = system.dwPageSize;
        const uint64_t committed = page ? ((uint64_t(request->input_bytes) + page - 1) / page) * page : 0;
        uint64_t coordinate=codec_session->next_coordinate.load();
        if (!page || coordinate % page || !committed || coordinate>UINT32_MAX ||
            committed>(uint64_t(1)<<32) || coordinate>(uint64_t(1)<<32)-committed)
            return ModuleStopResult(Stop::HostFailure, "Host staging coordinate/page geometry exceeds32-bit range");
        if(!codec_session->next_coordinate.compare_exchange_strong(coordinate,coordinate+committed))
            return ModuleStopResult(Stop::HostFailure,"Native source-coordinate reservation raced");
        StartupPartitionRequest allocation_request;
        allocation_request.bytes = request->input_bytes;
        allocation_request.source_base = static_cast<uint32_t>(coordinate);
        const auto allocation = backend->Allocate(allocation_request);
        if (!allocation.status)
            return ModuleStopResult(Stop::HostFailure, "Module staging allocation failed", allocation.status.win32_error);
        request->staging = allocation.handle; // primitive owner BEFORE mutex/telemetry/text
        {
            std::lock_guard<std::mutex> lock(metadata);
            data.staging_live = true; data.staging_owner = allocation.handle.owner;
            data.staging_generation = allocation.handle.generation; data.staging_serial = allocation.handle.serial;
            data.staging_host_address = allocation.host_address; data.staging_source_coordinate = allocation.source_base;
            data.staging_committed_bytes = allocation.committed_bytes; data.staging_page_bytes = allocation.page_bytes;
            EmitLocked("ModuleStagingAllocated", "HostModuleInput", 0, allocation.requested_bytes, false,
                       allocation.host_address, 0, 0, "distinct host staging coordinate;NOT PSP module placement");
        }
        if (allocation.requested_bytes != request->input_bytes || allocation.source_base != allocation_request.source_base ||
            allocation.committed_bytes != committed || allocation.page_bytes != page || !allocation.live ||
            !allocation.readable || !allocation.writable || allocation.host_address % page ||
            allocation.host_address > (std::numeric_limits<uint64_t>::max)() - committed)
            return ModuleStopResult(Stop::HostFailure, "Module staging allocation geometry mismatch");
        for (const auto& other : module_requests) {
            if (other.get() == request || !other->staging) continue;
            const auto& old = other->data;
            if (old.staging_serial == data.staging_serial ||
                (data.staging_host_address < old.staging_host_address + old.staging_committed_bytes &&
                 old.staging_host_address < data.staging_host_address + committed) ||
                (uint64_t(data.staging_source_coordinate) < uint64_t(old.staging_source_coordinate) + old.staging_committed_bytes &&
                 uint64_t(old.staging_source_coordinate) < uint64_t(data.staging_source_coordinate) + committed))
                return ModuleStopResult(Stop::HostFailure, "Distinct module staging allocations overlap or alias");
        }
        StartupHostStatus borrowed;
        request->staging_borrow = backend->Borrow(request->staging, borrowed);
        if (!borrowed) return ModuleStopResult(Stop::HostFailure, "Module staging borrow failed", borrowed.win32_error);
        { std::lock_guard<std::mutex> lock(metadata); data.staging_borrow_live = true; }
        StartupRawBorrowSpan span;
        const auto same_span = [&]() {
            return span.size == request->input_bytes && span.bytes && span.initialized && span.readable &&
                span.host_address == allocation.host_address && span.source_base == allocation.source_base &&
                span.handle.owner == allocation.handle.owner && span.handle.generation == allocation.handle.generation &&
                span.handle.serial == allocation.handle.serial;
        };
        auto access = request->staging_borrow.Span(span);
        if (!access || !same_span() || !span.writable)
            return ModuleStopResult(Stop::HostFailure, "Module staging lease geometry/identity failed", access.win32_error);
        if (HostCancelled()) return Stop::HostCancelled;
        const auto read = backend->ReadInto(request->backend_file, request->staging, 0, request->input_bytes);
        {
            std::lock_guard<std::mutex> lock(metadata);
            data.staging_requested = read.requested; data.staging_transferred = read.transferred;
            data.staging_host_read = read.host_result; data.staging_read_error = read.status.win32_error;
            EmitLocked("ModuleReadInto", "HostModuleInput", 0x4F53C, read.transferred, false,
                       read.destination_host_address, read.status.win32_error, 0, StartupHostCodeName(read.status.code));
        }
        access = request->staging_borrow.Span(span);
        if (!access || !same_span())
            return ModuleStopResult(Stop::HostFailure, "Module lease changed after input read", access.win32_error);
        uint32_t known = 0;
        for (uint32_t i = 0; i < request->input_bytes; ++i) if (span.initialized[i] == 1) ++known;
        { std::lock_guard<std::mutex> lock(metadata); data.staging_known_bytes = known; }
        if (!read.status || !read.host_result || read.requested != request->input_bytes ||
            read.transferred != request->input_bytes || read.file_position != 0 || read.destination_host_address != span.host_address)
            return ModuleStopResult(Stop::HostFailure, "Module read failed/short/position or destination mismatch", read.status.win32_error);
        if (known != request->input_bytes)
            return ModuleStopResult(Stop::HostFailure, "Module input contains unknown bytes");
        { std::lock_guard<std::mutex> lock(metadata); data.staging_full_read = true; }
        if (HostCancelled()) return Stop::HostCancelled;
        const auto digest = core::sha256_hex(span.bytes, request->input_bytes); // same physical span; no payload mirror
        const bool exact = recovered::FindAudiocodecModuleDescriptor(digest, request->input_bytes) ||
            recovered::FindAtrac3plusModuleDescriptor(digest, request->input_bytes) ||
            recovered::FindSascoreModuleDescriptor(digest, request->input_bytes) ||
            recovered::FindIfhandleModuleDescriptor(digest, request->input_bytes) ||
            recovered::FindPspnetModuleDescriptor(digest, request->input_bytes) ||
            recovered::FindMemabModuleDescriptor(digest, request->input_bytes) ||
            recovered::FindAdhocAuthModuleDescriptor(digest, request->input_bytes) ||
            recovered::FindAdhocModuleDescriptor(digest, request->input_bytes) ||
            recovered::FindAdhocctlModuleDescriptor(digest, request->input_bytes);
        { std::lock_guard<std::mutex> lock(metadata);
          data.staging_hash_matches = exact; data.staging_sha256 = digest;
          EmitLocked("ModuleEncodedIdentity", "HostModuleInput", 0x4F53C, exact ? 1 : 0, false, span.host_address, 0, 0, digest); }
        if (!exact) return ModuleStopResult(Stop::Unsupported, "Actual module identity has no supported native definition");
        if (HostCancelled()) return Stop::HostCancelled;
        auto lease = std::make_shared<WindowsModuleInputLease>(); // allocations before moving exact borrow
        lease->block = request->staging; lease->bytes = request->input_bytes; lease->coordinate = allocation.source_base;
        lease->borrow = std::move(request->staging_borrow);
        request->input = lease; // weak; coordinator/backend still own primitive allocation
        recovered::NativeModuleResult loaded;
        {
            std::lock_guard<std::mutex> lock(metadata);
            loaded = modules.Load(std::move(lease));
            if (loaded) {
                data.token = loaded.token; // publish actual registry identity before allocating snapshots/text
                data.record.token = loaded.token;
                data.module_uid_assigned = true; data.module_load_returned = loaded.source_return;
                data.module_load_result = loaded.source_result; data.module_input_live = true;
                data.record_live = true; data.pending = false;
                EmitLocked("ModuleRegistryPublished", "WindowsModulePolicy", 0, loaded.token.uid, false,
                           loaded.token.serial, 0, 0, "owned UID;exact input/source/compiled association;entry NOT run at Load");
                try {
                    modules.Snapshot(loaded.token, data.record);
                    EmitLocked("ModuleRecoveredDescriptor", "RecoveredMetadata", 0x4F53C,
                               static_cast<int64_t>(data.record.imports.size()), false, 0, 0, 0, data.record.name);
                    EmitLocked("ModuleDeferredImportPolicy", "WindowsModulePolicy", 0,
                               static_cast<int64_t>(data.record.imports.size()), false, loaded.token.owner, 0, 0, data.record.policy);
                    for (const auto& entry : data.record.imports)
                        EmitLocked("ModuleImportUnresolved", "RecoveredMetadata", entry.source_stub_va,
                                   entry.nid, false, loaded.token.serial, 0, 0, entry.library, 0, 0, true);
                    EmitLocked("ModuleExportMetadataRegistered", "WindowsModulePolicy", 0,
                               static_cast<int64_t>(data.record.exports.size()), false, loaded.token.serial, 0, 0,
                               "metadata is not a provider or implemented audio service");
                } catch (...) { ++state.dropped_events; }
                EmitLocked("LoadModuleByID-return", "SourceAdapter", 0x4F53C, loaded.source_result, loaded.source_return,
                           loaded.token.serial, 0, 0, "actual registry UID;no PSP placement/GP/relocation claim");
            }
        }
        if (!loaded || !loaded.source_return)
            return ModuleStopResult(loaded.code == recovered::NativeModuleCode::IdentityMismatch ||
                loaded.code == recovered::NativeModuleCode::UnsupportedPolicy ? Stop::Unsupported : Stop::HostFailure,
                "Native module ownership/identity registry load unsupported or failed");
        return recovered::StartupModuleReturned{loaded.source_result};
    }
    recovered::StartupModuleOutcome StartModule(int32_t uid, uint32_t bytes, const void* args,
        int32_t* status, const void* options_arg) override {
        using Stop = recovered::StartupModuleStop;
        if (HostCancelled()) return Stop::HostCancelled;
        auto* request = CurrentModule();
        if (!IsCurrent(true) || !request || !request->data.module_uid_assigned || uid != request->data.token.uid)
            throw NativeFailure{};
        auto& data = request->data;
        recovered::NativeModuleResult started_module;
        uint32_t entry_frontier = 0;
        bool provider_failed = false;
        {
            std::lock_guard<std::mutex> lock(metadata);
            data.module_start_called = true;
            EmitLocked("StartModule-enter", "SourceCall", 0x4F560, uid, false, data.token.serial,
                       0, 0, "bytes0/argsNULL/statusNULL/optionsNULL");
            started_module = modules.Start(data.token, bytes, args, status, options_arg);
            // Primitive lifecycle outcome precedes fallible metadata.
            data.module_start_returned = started_module.source_return;
            data.module_start_result = started_module.source_result;
            data.module_start_entry_returned = started_module.entry_return;
            data.module_start_entry_result = started_module.entry_result;
            modules.InitializationSnapshot(data.token, data.record.initialization);
            modules.SascoreSnapshot(data.token, data.record.sascore);
            modules.IfhandleSnapshot(data.token, data.record.ifhandle);
            modules.PspnetSnapshot(data.token, data.record.pspnet);
            modules.MemabSnapshot(data.token, data.record.memab);
            modules.AdhocAuthSnapshot(data.token, data.record.adhoc_auth);
            modules.AdhocSnapshot(data.token, data.record.adhoc);
            modules.AdhocctlSnapshot(data.token, data.record.adhocctl);
            data.module_initialization_blocked = data.record.initialization.prefix_entered &&
                data.record.initialization.dependency_blocked && !data.record.initialization.aborted;
            data.module_initialization_blocked = data.module_initialization_blocked ||
                (data.record.sascore.entry_entered && data.record.sascore.host_abort_required && !data.record.sascore.host_aborted);
            data.module_initialization_blocked=data.module_initialization_blocked ||
                (data.record.memab.entry_entered && data.record.memab.host_abort_required && !data.record.memab.host_aborted);
            modules.CheckNeedMemSnapshot(data.token, data.record.check_need_mem); // nonallocating outcome/effects
            modules.PreparedEnvelopeSnapshot(data.token,data.record.prepared_envelope);
            entry_frontier = started_module.frontier_source_pc; // primitive outcome; independent of allocating Snapshot
            if (data.module_initialization_blocked) {
                data.record.state = recovered::NativeModuleState::EntryBlocked;
                modules.InitializationSnapshot(data.token, data.record.initialization); // fixed-size exact effects survive telemetry OOM
            }
            if (started_module.entry_return && started_module.source_return && started_module.entry_source_pc == 0xAD8) {
                const auto registered = options.prepared_codec_envelope ?
                    modules.RegisterPreparedEnvelopeProvider(data.token) : modules.RegisterCheckNeedMemProvider(data.token);
                request->check_need_mem_provider = registered.token; // member-owned BEFORE telemetry
                provider_failed = registered.code != recovered::NativeModuleCode::Ok;
                EmitLocked("CheckNeedMemProviderRegistered", "HostPartialCapability", 0, registered.token.serial,
                           false, data.token.serial, 0, 0, recovered::NativeModuleCodeName(registered.code));
            }
            try {
                modules.Snapshot(data.token, data.record);
                if(data.record.memab.present) {
                    const auto& memab=data.record.memab;
                    for(uint32_t i=0;i<memab.writes_count;++i) {
                        const auto& write=memab.writes[i];
                        EmitLocked("MemabGlobalWrite","RecoveredSourceStoreNativeServiceResult",write.source_pc,write.value,false,
                                   memab.native_state_address,0,0,"actual Create service bits;only private+70..74 known;no relocated pointers/BSS0",0,0,true,data.token);
                    }
                    if(memab.resources.create_returned)
                        EmitLocked("MemabNativeSemaphore","WindowsMemabPolicy",0,memab.resources.semaphore_live?1:0,false,
                                   memab.resources.semaphore_handle,memab.resources.create_error,0,
                                   "actual native Create returned;separate nullable typed source service UID;unnamed noninheritable1/1");
                }
                if(data.record.sascore.present) {
                    const auto& sas=data.record.sascore;
                    for(uint32_t i=0;i<sas.writes_count;++i) {
                        const auto& write=sas.writes[i];
                        EmitLocked("SascoreGlobalWrite","RecoveredSourceStoreNativeValue",write.source_pc,write.value,false,
                                   sas.native_state_address,0,0,"actual returned platform value;source store owner SAS;no BSS0",0,0,true,data.token);
                    }
                    if(sas.resources.allocation_returned)
                        EmitLocked("SascoreNativeAllocation","WindowsSascorePolicy",0,sas.resources.memory_live?1:0,false,
                                   sas.resources.memory_host_address,sas.resources.allocation_error,0,"accepted backend outcome;nullable source return separately;46644 host calloc zeros not module BSS");
                    if(sas.resources.semaphore_create_returned)
                        EmitLocked("SascoreNativeSemaphore","WindowsSascorePolicy",0,sas.resources.semaphore_live?1:0,false,
                                   sas.resources.semaphore_handle,sas.resources.semaphore_create_error,0,"owned unnamed noninheritable1/1;source SceSas diagnostic name;UID separate from HANDLE");
                }
                const auto& initialization = data.record.initialization;
                if (initialization.prefix_entered) for (uint32_t i = 0; i < initialization.writes_count; ++i) {
                    const auto& write = initialization.writes[i];
                    const auto write_module = write.source_module == recovered::Atrac3plusWriteSource::NativePlatform ?
                        recovered::NativeModuleToken{} : write.source_module == recovered::Atrac3plusWriteSource::Audiocodec ?
                        data.record.check_need_mem.provider_module : data.token;
                    EmitLocked("ModuleInitializationWrite", write.source_module==recovered::Atrac3plusWriteSource::NativePlatform ?
                               "WindowsNativeQueryOutput" : "RecoveredNativePrefix", write.source_pc, write.value,
                               false, initialization.native_state_address, 0, 0,
                               "actual private masked AOT record state;input PRX unchanged;ordered write in record snapshot",
                               0, 0, write.source_module!=recovered::Atrac3plusWriteSource::NativePlatform, write_module);
                }
            } catch (...) { ++state.dropped_events; }
            if(data.record.prepared_envelope.present) for(const auto& native:data.record.prepared_envelope.records) {
                if(native.native_alloc_returned)
                    EmitLocked("NativeCodecContextAllocated", "WindowsQueryEnvelopePolicyV2", 0, native.context_allocated?1:0,
                               false,native.record_index,0,0,native.format_pending ?
                               "actual discovery/allocation;format pending;open not entered;no decoder-ready/decode capability" :
                               "actual allocation;separate open result/readiness in snapshot");
                if(native.native_open_entered)
                    EmitLocked("NativeCodecOpen2", "WindowsPreparedEnvelopePolicy", 0, native.native_api_result,
                               false,native.record_index,0,0,"actual open2 return meaningful only when native_open_returned;original equivalence UNKNOWN");
                if(native.query_source_return)
                    EmitLocked("NativePreparedCheckNeedMem-return","WindowsPolicySourceMapping",0x8C,native.query_source_result,true,
                               native.record_index,0,0,"actual native result mapped by recovered8C..D0;not original firmware proof",0,0,true,data.record.check_need_mem.provider_module);
                if(native.get_edram_source_return)
                    EmitLocked("NativeEnvelopeGetEDRAM-return","WindowsPolicyRecoveredWrapper",0x688,native.get_edram_source_result,true,
                               native.host_address,native.win32_error,0,"real mapped placement ownership;108B span;no source Init/decode",0,0,true,data.record.check_need_mem.provider_module);
            }
            if (data.module_initialization_blocked)
                EmitLocked("ModuleEntryDependency", "UnsupportedEntry", started_module.frontier_source_pc,
                           started_module.frontier_nid, false, data.token.serial, 0, 0,
                           data.record.prepared_envelope.present ?
                           "finite Windows query-envelope-v2;actual phase/resources in per-record snapshot;originalME unresolved" : started_module.check_need_mem_blocked ?
                           "ATRAC caller1684->audiocodec CheckNeedMem;callee84->ME stubAF8/C00D873B;codec1000,same owned record0+44;NO ME dispatch/result" :
                           data.record.sascore.present ? "SAS exact lifecycle platform boundary;actual effects/body result retained;NO fabricated Start service result" :
                           data.record.memab.present ? (data.record.memab.entry_returned ?
                           "memab actual nonpositive Create service result;A24 store/body1 retained;NO Windows Start completion;host abort required,NO sourceStopA34" :
                           "memab A10/CreateSema incomplete;NO source service result/A24 store/body result;any issued HANDLE/input pins retained for host abort,NO sourceStopA34") :
                           "ATRAC1684->stub3640;a0=owned record0+44,a1=1000;NO CheckNeedMem dispatch/result",
                           0, 0, true, started_module.dependency_module);
            if (started_module.entry_return)
                EmitLocked(started_module.entry_source_pc == 0xAD8 ? "ModuleStartEntryAD8-return" : "ModuleStartEntry-return",
                           "RecoveredNativeEntry", started_module.entry_source_pc, started_module.entry_result, true,
                           data.token.serial, 0, 0, "entry/body result separate from Windows Start service", 0, 0, true);
            if (started_module.source_return)
                EmitLocked("StartModule-return", "SourceAdapter", 0x4F560, started_module.source_result, true,
                           data.token.serial, 0, 0, "explicit Windows service policy;original firmware result UNKNOWN");
        }
        if (provider_failed)
            return ModuleStopResult(Stop::HostFailure, "Exact native CheckNeedMem provider registration failed");
        if (started_module.code == recovered::NativeModuleCode::UnsupportedEntry ||
            started_module.code == recovered::NativeModuleCode::NotLinked)
            return ModuleStopResult(Stop::Unsupported, "Native module entry reached exact unsupported dependency;no body/service result",
                                    0, entry_frontier, true, started_module.dependency_module);
        if (!started_module || !started_module.source_return)
            return ModuleStopResult(Stop::HostFailure, "Native module Start validation failed", 0, 0x4F560);
        return recovered::StartupModuleReturned{started_module.source_result};
    }
    recovered::StartupModuleOutcome CloseModuleFile(int32_t fd) override {
        if (HostCancelled()) return recovered::StartupModuleStop::HostCancelled;
        auto* request = CurrentModule();
        if (!IsCurrent(true) || !request || !FileMatches(*request) || fd != request->data.source_fd) throw NativeFailure{};
        auto& data = request->data;
        { std::lock_guard<std::mutex> lock(metadata); data.source_close_called = true; }
        const auto closed = backend->CloseFile(request->backend_file);
        if (closed) request->backend_file = {}; // retire actual primitive BEFORE telemetry
        { std::lock_guard<std::mutex> lock(metadata);
          data.source_close_returned = true; data.source_close_result = closed ? 0 : AdapterError;
          if (closed) data.file_live = false;
          bool live = false;
          try {
              auto retained = request->input.lock();
              recovered::NativeModuleInputView view;
              live = retained && retained->View(view) && view.owner == request->staging.owner &&
                  view.generation == request->staging.generation && view.serial == request->staging.serial &&
                  view.host_address == data.staging_host_address && view.size == request->input_bytes &&
                  core::sha256_hex(view.bytes, view.size) == data.staging_sha256;
          } catch (...) { ++state.dropped_events; }
          data.module_input_live = live; data.module_file_closed_while_input_live = closed && live;
          if (closed) EmitLocked("SourceFileClosedWhileModuleInputLive", "WindowsModulePolicy", 0x4F570,
              live ? 1 : 0, false, data.staging_host_address, 0, 0, "same fd retired;same actual span/hash retained");
          EmitLocked("SourceCloseModuleFile", "SourceAdapter", 0x4F570, data.source_close_result,
                     true, data.file_native_object, closed.win32_error, 0, "close result ignored;wrapper returns actual Load UID"); }
        return recovered::StartupModuleReturned{closed ? 0 : AdapterError};
    }
    void UnsupportedFollowingModule() override {
        const auto repeated=RepeatedSnapshot();
        if(repeated.source_gate_returned && repeated.source_gate_result==1 && repeated.counter==4 &&
            repeated.current_record_index==4 && repeated.writes_count==4) {
            // Gate1 returns to main FFC;1000 ordinary delay restores saved RA.
            // Next is JAL52D58 at1004 with1008 LUI s1,1. No admitted callee
            // body/signature/result: the historical loop counter is not live s1.
            // This is a GAME frontier, not an incomplete ninth module call.
            std::lock_guard<std::mutex> lock(metadata);
            state.barrier_source_pc=0x1004;state.barrier_module_relative=false;state.barrier_source_module={};
            state.current_frontier_source_pc=0x1004;state.current_frontier_module_relative=false;
            state.current_frontier_source_module={};state.reached_100C=false;
            try {
                state.worker_state="Blocked";state.barrier="UnrecoveredGameCall52D58";
                state.current_frontier="UnrecoveredGameCall52D58";
            } catch(...) {++state.dropped_events;}
            EmitLocked("UnrecoveredGameCall52D58","RecoveredGameCaller",0x1004,0,false,0,0,0,
                "target52D58;delay1008 LUI s1,1;body/signature/sideeffects unknown;result absent;not returned;later100C/1234 unreached");
            return; // completed request8 Load/Start/Close stays intact; no tenth request
        }
        (void)ModuleStopResult(recovered::StartupModuleStop::Unsupported,
            "Repeated caller reached first unadmitted table kind/main continuation;no unknown read or fabricated source result",0,
            repeated.current_record_index>=4?0x100C:0x4F740);
    }
    recovered::GameServiceResult SetControllerSamplingMode(int32_t mode) override {
        if (HostCancelled()) return {};
        std::lock_guard<std::mutex> lock(metadata);
        const auto previous = controller.SetSamplingMode(mode);
        state.game_boot.sampling_mode = controller.SamplingMode();
        EmitLocked("SetControllerSamplingMode", "WindowsInputAdapter", 0x52D60, previous, true,
            0, 0, 0, "mode1;native input configuration;return previous mode;PSP hardware timing not emulated");
        return {true, previous};
    }
    recovered::GameServiceResult SetControllerSamplingCycle(int32_t cycle) override {
        if (HostCancelled()) return {};
        std::lock_guard<std::mutex> lock(metadata);
        const auto previous = controller.SetSamplingCycle(cycle);
        state.game_boot.sampling_cycle = controller.SamplingCycle();
        EmitLocked("SetControllerSamplingCycle", "WindowsInputAdapter", 0x52D68, previous, true,
            0, 0, 0, "cycle0;sampled by native frame loop;no PSP polling-frequency claim");
        return {true, previous};
    }
    recovered::GameServiceResult PrepareGameArena(uint32_t index, uint32_t bytes) override {
        if (HostCancelled()) return {};
        constexpr std::array<uint32_t, 3> sizes{{0xB94000, 0x74E000, 0x46000}};
        if (index >= sizes.size() || bytes != sizes[index] || game_blocks[index]) throw NativeFailure{};
        StartupPartitionRequest request;
        request.bytes = bytes;
        request.source_base = 0x70000000 + index * 0x01000000; // native logical coordinates, not PSP placement
        const auto allocation = backend->Allocate(request);
        if (!allocation.status) {
            Emit("AllocateGameArenaFailed", "WindowsGameMemoryAdapter", index ? 0x132C : 0x1298,
                0, true, 0, allocation.status.win32_error);
            return {true, 0};
        }
        game_blocks[index] = allocation.handle; // retain ownership before any fallible work
        {
            std::lock_guard<std::mutex> lock(metadata);
            auto& out = state.game_boot.arenas[index];
            out.live = true; out.requested_bytes = bytes; out.source_coordinate = request.source_base;
            out.host_address = allocation.host_address; out.committed_bytes = allocation.committed_bytes;
        }
        StartupHostStatus status;
        game_borrows[index] = backend->Borrow(game_blocks[index], status);
        StartupRawBorrowSpan span;
        if (!status || !game_borrows[index].Span(span) || !span.writable || span.size != bytes
            || !game_arenas[index].Initialize(span.bytes, bytes)) throw NativeFailure{};
        Emit("GameArenaReady", "WindowsGameMemoryAdapter", index ? 0x134C : 0x12E8,
            bytes, false, allocation.host_address, 0, 0,
            "Main/PPK/KS source sizes;native C++ free-list/controller replaces PSP libc/639CC layout;payload BSS knowledge not inferred");
        return {true, 1};
    }
    recovered::GameServiceResult SelectDefaultGameArena(uint32_t index) override {
        if (HostCancelled()) return {};
        if (index != 1 || !game_blocks[index] || !game_arenas[index].Capacity()) throw NativeFailure{};
        std::lock_guard<std::mutex> lock(metadata);
        state.game_boot.default_arena = index;
        EmitLocked("SelectDefaultGameArena", "RecoveredNativeGame", 0x7CE30, index, false,
            state.game_boot.arenas[index].host_address, 0, 0,
            "7CE28 setter stores the PPK controller;native typed reference,not a numeric PSP pointer");
        return {true, 0}; // setter result unused by1234
    }
    recovered::StartupFlowStop ContinueGameAfterModules() override {
        using recovered::StartupFlowStop;
        event_module_index = StartupNoCurrentModule;
        {
            std::lock_guard<std::mutex> lock(metadata);
            state.game_boot.controller_setup_entered = true;
        }
        Emit("GameControllerSetup-enter", "RecoveredGameCaller", 0x1004);
        const auto input = recovered::InitializeGameInput52D58(*this);
        if (!input.returned) return StartupFlowStop::HostCancelled;
        {
            std::lock_guard<std::mutex> lock(metadata);
            state.game_boot.controller_setup_returned = true;
            state.game_boot.controller_setup_result = input.value;
            state.reached_100C = true;
            state.game_boot.memory_initialization_entered = true;
        }
        Emit("GameControllerSetup-return", "RecoveredNativeGame", 0x52D74, input.value, true);
        Emit("GameMemoryInitialization-enter", "RecoveredGameCaller", 0x100C);
        const auto memory = recovered::InitializeGameMemory1234(*this);
        if (!memory.returned) return StartupFlowStop::HostCancelled;
        std::lock_guard<std::mutex> lock(metadata);
        state.game_boot.memory_initialization_returned = true;
        state.game_boot.memory_initialization_succeeded = memory.value != 0;
        EmitLocked("GameMemoryInitialization-return", "RecoveredNativeGame", 0x12D4, memory.value, true);
        if (!memory.value) {
            // The original main ignores the value. Native allocation failure
            // is a fatal host condition instead of running with null arenas.
            FailLocked(Failure(StartupRuntimeCode::HostFailure, ERROR_NOT_ENOUGH_MEMORY));
            return StartupFlowStop::HostFailure;
        }
        state.barrier_source_pc = state.current_frontier_source_pc = 0x1014;
        state.barrier_module_relative = state.current_frontier_module_relative = false;
        state.barrier_source_module = state.current_frontier_source_module = {};
        state.barrier = state.current_frontier = "UnrecoveredGraphicsSetupC0C";
        state.worker_state = "Blocked";
        EmitLocked("UnrecoveredGraphicsSetupC0C", "RecoveredGameCaller", 0x1014, 0, false,
            0, 0, 0, "controller and native arenas ready;next actual game graphics initialization;no C0C return/title/gameplay fabricated");
        return StartupFlowStop::Unsupported;
    }
    bool HostCancelled() const override { return codec_session->cancelled.load(); }
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
                    codec_session->cancelled.store(true); (void)SetEvent(cancel); return;
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
    StartupRuntimeStatus CleanupFailure(DWORD error = 0) {
        std::lock_guard<std::mutex> lock(metadata);
        state.status = Failure(StartupRuntimeCode::HostFailure, error);
        return state.status;
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
        codec_session->cancelled.store(true);
        if (cancel) {
            const BOOL signaled = SetEvent(cancel); const DWORD error = signaled ? 0 : GetLastError();
            Emit("SetEvent-cancel", "HostLifecycle", 0, signaled, false, Address(cancel), error);
            if (!signaled) return CleanupFailure(error);
        }
        // A source StartThread failure can leave a suspended real object. Resume
        // solely for native cancellation; thunk checks cancel BEFORE source entry.
        for (unsigned i = 0; i != 2; ++i) {
            HANDLE handle = i ? worker : caller;
            bool& resumed = i ? worker_resumed : caller_resumed;
            if (handle && !resumed) {
                const DWORD previous = ResumeThread(handle); const DWORD error = previous == DWORD(-1) ? GetLastError() : 0;
                Emit("ResumeForHostCancellation", "HostLifecycle", 0, previous, false, Address(handle), error);
                if (previous == DWORD(-1)) return CleanupFailure(error);
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
        // Game allocations were acquired after the modules. Retain failed
        // releases in this owner for the existing joined cleanup retry.
        for (size_t remaining = game_blocks.size(); remaining; --remaining) {
            const auto index = remaining - 1;
            if (!game_blocks[index]) continue;
            game_arenas[index].Reset();
            game_borrows[index].Reset();
            const auto released = backend->Free(game_blocks[index]);
            if (released) game_blocks[index] = {};
            {
                std::lock_guard<std::mutex> lock(metadata);
                if (released) state.game_boot.arenas[index].live = false;
                EmitLocked("FreeGameArena", "HostLifecycle", 0, released ? 1 : 0, false,
                    state.game_boot.arenas[index].host_address, released.win32_error);
            }
            if (!released) return CleanupFailure(released.win32_error);
        }
        // Reverse publication order; BOTH source contexts are joined. Failure
        // preserves this owner, earlier modules and backend for honest retry.
        for (size_t remaining = module_requests.size(); remaining; --remaining) {
            auto& request = *module_requests[remaining - 1];
            auto& data = request.data;
            struct EventModuleScope {
                Impl& runtime; uint32_t previous;
                ~EventModuleScope() { runtime.event_module_index = previous; }
            } event_scope{*this, event_module_index};
            event_module_index = data.request_index;
            { std::lock_guard<std::mutex> lock(metadata); data.pending = false; }
            if (data.module_uid_assigned && !data.module_unloaded) {
                std::lock_guard<std::mutex> lock(metadata);
                if (!state.caller_joined || !state.worker_joined) {
                    state.status = Failure(StartupRuntimeCode::HostFailure); return state.status;
                }
                if (request.check_need_mem_provider.serial) {
                    const auto provider = request.check_need_mem_provider;
                    const auto revoked = modules.UnregisterProvider(provider);
                    if (revoked != recovered::NativeModuleCode::Ok) {
                        state.status = Failure(StartupRuntimeCode::HostFailure); return state.status;
                    }
                    request.check_need_mem_provider = {}; // retire primitive token BEFORE no-throw telemetry
                    EmitLocked("CheckNeedMemProviderRevoked", "HostPartialCapability", 0, provider.serial,
                               false, data.token.serial, 0, 0, "after reverse second unload/lease/free/fdClose;before firstAE0");
                }
                if (data.module_start_returned && !data.module_stop_returned) {
                    data.module_stop_called = true;
                    modules.InitializationSnapshot(data.token,data.record.initialization);
                    const uint32_t prior_writes=data.record.initialization.writes_count;
                    EmitLocked("ModuleStopEntry-enter","HostPolicyRecoveredNativeEntry",data.record.stop_source_va,0,false,
                               data.token.serial,0,0,"after BOTH joins;entry body distinct from Windows ownership policy",0,0,true,data.token);
                    const auto stopped = modules.Stop(data.token);
                    modules.InitializationSnapshot(data.token,data.record.initialization);
                    modules.PreparedEnvelopeSnapshot(data.token,data.record.prepared_envelope);
                    modules.SascoreSnapshot(data.token,data.record.sascore);
                    modules.IfhandleSnapshot(data.token,data.record.ifhandle);
                    modules.PspnetSnapshot(data.token, data.record.pspnet);
                    modules.MemabSnapshot(data.token, data.record.memab);
                    modules.AdhocAuthSnapshot(data.token, data.record.adhoc_auth);
                    modules.AdhocSnapshot(data.token, data.record.adhoc);
                    modules.AdhocctlSnapshot(data.token, data.record.adhocctl);
                    for(uint32_t i=prior_writes;i<data.record.initialization.writes_count;++i) {
                        const auto& write=data.record.initialization.writes[i];
                        EmitLocked("ModuleStopReleaseWrite","RecoveredSourceAfterWindowsRelease",write.source_pc,write.value,false,
                                   data.record.initialization.native_state_address,0,0,
                                   "literal source store only after completed native owned release;Windows capability replaces PSP admission",0,0,true,
                                   data.record.check_need_mem.provider_module);
                    }
                    if (!stopped || !stopped.entry_return) {
                        EmitLocked("HostModuleStopFailed", "HostModulePolicy", 0, 0, false, data.token.serial,
                                   0, 0, recovered::NativeModuleCodeName(stopped.code));
                        state.status = Failure(StartupRuntimeCode::HostFailure); return state.status;
                    }
                    data.module_stop_returned = true; data.module_stop_result = stopped.entry_result;
                    if(data.record.memab.present && data.record.memab.resources.delete_source_returned) {
                        const auto& resource=data.record.memab.resources;
                        EmitLocked("MemabDeleteSema-return","WindowsMemabPolicySourceMapping",0xA40,resource.delete_source_result,true,
                                   resource.semaphore_handle,resource.delete_error,0,
                                   "actual native Close mapped0/-1;source A4C ignores number;live host obligation retained",0,0,true,data.token);
                    }
                    if(data.record.prepared_envelope.source_stop_returned) for(const auto& native:data.record.prepared_envelope.records) {
                        if(native.release_source_return)
                            EmitLocked("NativeOwnedReleaseEDRAM-return","WindowsOwnedReleaseCapabilityV2",0x77C,native.release_source_result,true,
                                       native.record_index,0,0,"actual retirement completed before literal7A0/7A8;original low29 predicate separately reported;no ME equivalence",0,0,true,
                                       data.record.check_need_mem.provider_module);
                    }
                    EmitLocked(stopped.entry_source_pc == 0xAE0 ? "ModuleStopEntryAE0-return" : "ModuleStopEntry-return",
                               "HostPolicyRecoveredNativeEntry", stopped.entry_source_pc, stopped.entry_result,
                               false, data.token.serial, 0, 0, "host cleanup after BOTH joins;no source Stop service result", 0, 0, true);
                }
                if (data.module_initialization_blocked && !data.module_initialization_aborted) {
                    data.module_initialization_abort_called = true;
                    const auto aborted = modules.AbortInitialization(data.token);
                    if (!aborted) {
                        EmitLocked("HostModuleAbortFailed", "HostModulePolicy", 0, 0, false, data.token.serial,
                                   0, 0, recovered::NativeModuleCodeName(aborted.code));
                        state.status = Failure(StartupRuntimeCode::HostFailure); return state.status;
                    }
                    data.module_initialization_aborted = true;
                    data.record.state = recovered::NativeModuleState::InitializationAborted;
                    modules.InitializationSnapshot(data.token, data.record.initialization);
                    modules.PreparedEnvelopeSnapshot(data.token,data.record.prepared_envelope);
                    modules.SascoreSnapshot(data.token,data.record.sascore);
                    modules.MemabSnapshot(data.token,data.record.memab);
                    EmitLocked("HostModuleInitializationAborted", "HostModulePolicy", 0, data.token.uid, false,
                               data.token.serial, 0, 0, data.record.prepared_envelope.present ?
                               "after BOTH joins;reverse native context destruction/borrowReset/Free completed;NO sourceStop121C" :
                               data.record.sascore.present ? "SAS reverse host abort completed;source globals/history retained;NO sourceStop1ACC" :
                               data.record.memab.present ? (data.record.memab.entry_returned ?
                               "memab host abort after BOTH joins;issued semaphore/input pins retired;actual A24 store/body1/history retained;NO sourceStopA34" :
                               "memab host abort after BOTH joins;any issued semaphore/input pins retired;incomplete Create produced no A24/body result;NO sourceStopA34") :
                               "libatrac partial init and optional CheckNeedMem write;no external ME resource;NO sourceStop121C");
                }
                if(data.module_stop_returned && data.record.sascore.present) {
                    const auto retired=modules.RetireStoppedSascore(data.token);
                    modules.SascoreSnapshot(data.token,data.record.sascore);
                    if(!retired) {
                        EmitLocked("HostStoppedSascoreRetirementPending","HostModulePolicy",0,0,false,data.token.serial,
                                   0,0,"issued resource/input-pin slot must retire before Unload/input expiry");
                        state.status=Failure(StartupRuntimeCode::HostFailure);return state.status;
                    }
                    EmitLocked("HostStoppedSascoreRetired","HostModulePolicy",0,1,false,data.token.serial,
                               0,0,"after BOTH joins/sourceStop;input pins retired;no global zero stores/history rewrite");
                }
                if(data.module_stop_returned && data.record.memab.present) {
                    const auto retired=modules.RetireStoppedMemab(data.token);
                    modules.MemabSnapshot(data.token,data.record.memab);
                    if(!retired) {
                        EmitLocked("HostStoppedMemabRetirementPending","HostModulePolicy",0,0,false,data.token.serial,
                                   0,0,"issued slot/input pin/live HANDLE must retire before Unload;no source replay");
                        state.status=Failure(StartupRuntimeCode::HostFailure);return state.status;
                    }
                    EmitLocked("HostStoppedMemabRetired","HostModulePolicy",0,1,false,data.token.serial,
                               0,0,"after BOTH joins/sourceStop;all issued obligations retired;source UID/mask/history retained");
                }
                if(data.module_stop_returned && data.record.prepared_envelope.present) {
                    const auto retired=modules.RetireStoppedCodecWorkspaces(data.token);
                    modules.PreparedEnvelopeSnapshot(data.token,data.record.prepared_envelope);
                    if(!retired) {
                        EmitLocked("HostStoppedCodecRetirementPending","HostModulePolicy",0,0,false,data.token.serial,
                                   0,0,"issued query slots may own input/API pins even without HasResources;retry before Unload");
                        state.status=Failure(StartupRuntimeCode::HostFailure);return state.status;
                    }
                    EmitLocked("HostStoppedCodecSlotsRetired","HostModulePolicy",0,1,false,data.token.serial,
                               0,0,"after BOTH joins/sourceStop;all issued slots retired including pins-only Failed;source histories unchanged");
                }
                try { modules.Snapshot(data.token, data.record); } catch (...) { ++state.dropped_events; }
                const auto unloaded = modules.Unload(data.token);
                if (!unloaded) {
                    EmitLocked("HostModuleUnloadFailed", "HostModulePolicy", 0, 0, false, data.token.serial,
                               0, 0, recovered::NativeModuleCodeName(unloaded.code));
                    state.status = Failure(StartupRuntimeCode::HostFailure); return state.status;
                }
                data.module_unloaded = true; data.record_live = false; data.record.input_live = false;
                data.module_input_released = request.input.expired();
                data.module_input_live = !data.module_input_released;
                EmitLocked("HostModuleRegistryUnloaded", "HostModulePolicy", 0, data.token.uid, false,
                           data.token.serial, 0, 0, "reverse host cleanup;imports/exports/UID removed;no source Unload");
                EmitLocked("ModuleInputLeaseReleased", "HostModulePolicy", 0,
                           data.module_input_released ? 1 : 0, false, data.staging_host_address);
                if (!data.module_input_released) {
                    state.status = Failure(StartupRuntimeCode::HostFailure); return state.status;
                }
            }
            if (!request.input.expired()) return CleanupFailure();
            if (request.staging) {
                request.staging_borrow.Reset();
                { std::lock_guard<std::mutex> lock(metadata); data.staging_borrow_live = false; }
                Emit("ModuleStagingBorrowReset", "HostLifecycle", 0, 1, false, data.staging_host_address);
                const auto released = backend->Free(request.staging);
                if (released) request.staging = {}; // primitive retirement before telemetry
                {
                    std::lock_guard<std::mutex> lock(metadata);
                    if (released) {
                        data.staging_live = false;
                        data.staging_freed_after_joins = state.caller_joined && state.worker_joined;
                    }
                    EmitLocked("FreeModuleStaging", "HostLifecycle", 0, released ? 1 : 0, false,
                               data.staging_host_address, released.win32_error, 0, StartupHostCodeName(released.code));
                }
                if (!released) return CleanupFailure(released.win32_error);
            }
            if (request.backend_file) {
                const auto closed = backend->CloseFile(request.backend_file);
                if (closed) request.backend_file = {};
                { std::lock_guard<std::mutex> lock(metadata);
                  if (closed) { data.file_live = false; data.host_file_closed = true; }
                  EmitLocked("HostCloseSourceFile", "HostLifecycle", 0, closed ? 1 : 0, false,
                             data.file_native_object, closed.win32_error, 0,
                             "still-open fd cleanup;distinct from source4F570;no invented sourceClose"); }
                if (!closed) return CleanupFailure(closed.win32_error);
            }
        }
        event_module_index = StartupNoCurrentModule;
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
        if (backend) {
            if(memab_backend) {
                const auto closed=memab_backend->Close();
                Emit("CloseMemabBackend","WindowsMemabPolicy",0,closed==recovered::NativeMemabCode::Ok?1:0);
                if(closed!=recovered::NativeMemabCode::Ok) return CleanupFailure();
            }
            if(sascore_backend) {
                const auto closed=sascore_backend->Close();
                Emit("CloseSascoreBackend","WindowsSascorePolicy",0,closed==recovered::NativeSascoreCode::Ok?1:0);
                if(closed!=recovered::NativeSascoreCode::Ok) return CleanupFailure();
            }
            if(codec_backend) {
                const auto closed=codec_backend->Close();
                Emit("CloseNativeCodecBackend","WindowsPreparedEnvelopePolicy",0,closed==recovered::NativeCodecBackendStatus::Ok?1:0);
                if(closed!=recovered::NativeCodecBackendStatus::Ok) return CleanupFailure();
            }
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
    if(impl_->options.prepared_codec_envelope && impl_->state.caller_joined && impl_->state.worker_joined) {
        Impl::RetainJoined(std::move(impl_));return;
    }
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
    if(options.prepared_codec_envelope) {
        std::wstring dependency_root;
#ifdef DOKODEMO_FFMPEG_ROOT
        const std::string configured=DOKODEMO_FFMPEG_ROOT;
        for(const unsigned char c:configured) if(c>=0x80) { status=Failure(StartupRuntimeCode::HostFailure);return {}; }
        dependency_root.assign(configured.begin(),configured.end());
#endif
        recovered::NativeCodecBackendStatus codec_status;
        impl->codec_backend=CreateWindowsNativeCodecWorkspaceBackend(impl->backend,impl->codec_session,dependency_root,codec_status);
        if(!impl->codec_backend || codec_status!=recovered::NativeCodecBackendStatus::Ok ||
            impl->modules.ConfigurePreparedEnvelopeBackend(impl->codec_backend)!=recovered::NativeModuleCode::Ok) {
            status=Failure(StartupRuntimeCode::HostFailure);return {};
        }
        impl->Emit("PreparedEnvelopeBackendReady","WindowsPreparedEnvelopePolicy",0,1,false,0,0,0,
                   recovered::NativePreparedEnvelopeAbi);
        recovered::NativeSascoreCode sascore_status;
        impl->sascore_backend=CreateWindowsSascoreBackend(impl->backend,impl->codec_session,sascore_status);
        if(!impl->sascore_backend || sascore_status!=recovered::NativeSascoreCode::Ok ||
            impl->modules.ConfigureSascoreBackend(impl->sascore_backend)!=recovered::NativeModuleCode::Ok) {
            status=Failure(StartupRuntimeCode::HostFailure);return {};
        }
        impl->Emit("SascoreBackendReady","WindowsSascorePolicy",0,1,false,0,0,0,recovered::NativeSascoreAbi);
        recovered::NativeMemabCode memab_status;
        auto cancellation=std::shared_ptr<std::atomic<bool>>(impl->codec_session,&impl->codec_session->cancelled);
        impl->memab_backend=CreateWindowsMemabBackend(std::move(cancellation),memab_status);
        if(!impl->memab_backend || memab_status!=recovered::NativeMemabCode::Ok ||
            impl->modules.ConfigureMemabBackend(impl->memab_backend)!=recovered::NativeModuleCode::Ok) {
            status=Failure(StartupRuntimeCode::HostFailure);return {};
        }
        impl->Emit("MemabBackendReady","WindowsMemabPolicy",0,1,false,0,0,0,recovered::NativeMemabAbi);
    }
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
    out.repeated_modules=self.RepeatedSnapshot();
    out.module_records = self.modules.Snapshots();
    out.module_providers = self.modules.Providers();
    out.module_observations.reserve(self.module_requests.size());
    for (const auto& request : self.module_requests) {
        auto data = request->data;
        if (data.module_uid_assigned && !data.module_unloaded) {
            data.record_live = self.modules.Snapshot(data.token, data.record);
            if (data.record_live) data.module_input_live = data.record.input_live;
        } else if (data.module_unloaded) {
            data.record_live = false; data.record.input_live = false;
        }
        out.module_observations.push_back(std::move(data));
    }
    if (!out.module_observations.empty()) {
        const auto& first = out.module_observations.front(); // EXPLICIT request0 aliases only
        out.source_fd = first.source_fd; out.file_live = first.file_live;
        out.file_native_object = first.file_native_object; out.file_backend_owner = first.file_backend_owner;
        out.file_backend_generation = first.file_backend_generation; out.file_backend_serial = first.file_backend_serial;
        out.pending_module_owner = first.request_owner; out.pending_module_live = first.pending;
        out.pending_module_path = first.path; out.pending_module_flags = first.flags; out.pending_module_options = first.options;
        out.encoded_file_bytes = first.encoded_file_bytes;
        out.staging_owner = first.staging_owner; out.staging_generation = first.staging_generation;
        out.staging_serial = first.staging_serial; out.staging_host_address = first.staging_host_address;
        out.staging_known_bytes = first.staging_known_bytes; out.staging_file_position = first.staging_file_position;
        out.staging_source_coordinate = first.staging_source_coordinate; out.staging_requested = first.staging_requested;
        out.staging_transferred = first.staging_transferred; out.staging_read_error = first.staging_read_error;
        out.staging_live = first.staging_live; out.staging_borrow_live = first.staging_borrow_live;
        out.staging_host_read = first.staging_host_read; out.staging_full_read = first.staging_full_read;
        out.staging_hash_matches = first.staging_hash_matches; out.staging_freed_after_joins = first.staging_freed_after_joins;
        out.staging_sha256 = first.staging_sha256;
        out.module_uid_assigned = first.module_uid_assigned; out.module_uid = first.token.uid;
        out.module_load_returned = first.module_load_returned; out.module_start_called = first.module_start_called;
        out.module_manager_owner = first.token.owner ? first.token.owner : self.modules.Owner();
        out.module_generation = first.token.generation ? first.token.generation : self.modules.Generation();
        out.module_serial = first.token.serial; out.module_input_live = first.module_input_live;
        out.module_file_closed_while_input_live = first.module_file_closed_while_input_live;
        out.module_stop_called = first.module_stop_called; out.module_unloaded = first.module_unloaded;
        out.module_input_released = first.module_input_released; out.module_start_returned = first.module_start_returned;
        out.module_stop_returned = first.module_stop_returned; out.module_start_result = first.module_start_result;
        out.module_stop_result = first.module_stop_result; out.module_record = first.record;
        out.module_state = first.record_live || first.module_unloaded ? recovered::NativeModuleStateName(first.record.state) : "";
        out.module_binding_frontier = first.frontier;
    }
    for (const auto& event : self.events) if (event.ordinal > after_event) out.events.push_back(event);
    return out;
}
recovered::ControllerSample StartupCoordinator::PollControllerInput(bool window_focused) {
    auto& self = *impl_;
    std::lock_guard<std::mutex> lock(self.metadata);
    if (!self.state.game_boot.controller_setup_returned || self.state.closing || self.state.resources_closed)
        return {};
    const auto down = [window_focused](int key) {
        return window_focused && (GetAsyncKeyState(key) & 0x8000) != 0;
    };
    using namespace recovered;
    uint32_t buttons = 0;
    const std::pair<int, uint32_t> keys[] = {
        {VK_UP, Up}, {VK_RIGHT, Right}, {VK_DOWN, Down}, {VK_LEFT, Left},
        {VK_RETURN, recovered::Start}, {VK_BACK, Select}, {'Q', LTrigger}, {'E', RTrigger},
        {'Z', Cross}, {'X', Circle}, {'A', Square}, {'S', Triangle}
    };
    for (const auto& key : keys) if (down(key.first)) buttons |= key.second;
    const auto x = uint8_t(128 + 127 * (int(down('L')) - int(down('J'))));
    const auto y = uint8_t(128 + 127 * (int(down('K')) - int(down('I'))));
    return self.controller.Update(buttons, x, y, GetTickCount64() * 1000);
}
StartupRuntimeStatus StartupCoordinator::RequestExit() { return impl_->RequestExit(); }
StartupRuntimeStatus StartupCoordinator::Close(uint32_t timeout_ms) { return impl_->Close(timeout_ms); }
uint32_t StartupCoordinator::PendingDeferredCodecCleanup() noexcept { return Impl::PendingJoined(); }
StartupRuntimeStatus StartupCoordinator::RetryDeferredCodecCleanup(uint32_t timeout_ms) {
    std::unique_ptr<Impl> batch;
    auto& owners=Impl::JoinedOwners();AcquireSRWLockExclusive(&owners.lock);
    batch=std::move(owners.head);ReleaseSRWLockExclusive(&owners.lock);
    while(batch) {
        auto current=std::move(batch);batch=std::move(current->next_deferred_codec_cleanup);
        bool done=false;try { done=bool(current->Close(timeout_ms)); } catch(...) {}
        if(!done) Impl::RetainJoined(std::move(current));
    }
    return Impl::PendingJoined()?Failure(StartupRuntimeCode::HostFailure):StartupRuntimeStatus{};
}
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
