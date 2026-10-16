#pragma once
#include <array>
#include <cstdint>
#include <variant>

namespace dokodemo::recovered {
// Finite WindowsNativeEntry1184. Earlier PSP CRT/TLS/constructors are omitted.
// Source identities below are metadata, never native addresses or a PSP frame.
enum class StartupFlowStop { SourceReturned, HostCancelled, Unsupported, HostFailure };
enum class StartupDispatchOrigin { SourceCheckCallback, HostBarrierPump };
struct StartupWorkerRequest {
    const char* name = nullptr;
    uint32_t entry = 0, priority = 0, stack_bytes = 0, attributes = 0;
    const void* options = nullptr;
};
struct StartupCallbackRequest {
    const char* name = nullptr;
    uint32_t entry = 0;
    void* common = nullptr;
};
using StartupModuleOptions = std::array<uint8_t, 20>;
struct StartupModuleReturned {
    explicit StartupModuleReturned(int32_t result) : value(result) {}
    int32_t value;
};
enum class StartupModuleStop { Unsupported, HostCancelled, HostFailure };
// No default result: only the Returned alternative owns a source service v0.
using StartupModuleOutcome = std::variant<StartupModuleReturned, StartupModuleStop>;
class StartupFlowServices {
public:
    virtual ~StartupFlowServices() = default;
    virtual int32_t CreateWorker(const StartupWorkerRequest&) = 0;
    virtual void StoreCreatedWorkerUid(int32_t uid) = 0;
    virtual int32_t StartWorker(int32_t uid, uint32_t bytes, const void* args) = 0;
    virtual StartupFlowStop SleepCaller() = 0;
    virtual int32_t CreateExitCallback(const StartupCallbackRequest&) = 0;
    virtual int32_t RegisterExitCallback(int32_t uid) = 0;
    virtual int32_t CheckCallback(uint32_t source_callsite) = 0;
    virtual void ExitGame() = 0;
    virtual int32_t CheckMedium() = 0;
    virtual int32_t Activate(uint32_t mode, const char* device) = 0;
    virtual int32_t WaitDriveStat(uint32_t mask) = 0; // ordinary non-CB import
    virtual int32_t OpenRead(const char* path, uint32_t flags, uint32_t mode) = 0;
    virtual StartupModuleOutcome LoadModuleById(int32_t fd, uint32_t flags,
                                                const StartupModuleOptions&) = 0;
    virtual StartupModuleOutcome StartModule(int32_t uid, uint32_t bytes,
        const void* args, int32_t* status, const void* options) = 0;
    virtual StartupModuleOutcome CloseModuleFile(int32_t fd) = 0;
    // Native ownership/diagnostic boundary before a source wrapper invocation.
    // This is not a PSP import and has no source return value.
    virtual void BeginModuleRequest(uint32_t, const char*, const StartupModuleOptions&) {}
    virtual void UnsupportedFollowingModule() = 0;
    virtual bool HostCancelled() const = 0;
};
bool StartupStartResultSleeps1184(int32_t result);
StartupModuleOptions StartupOptions4F59C(int32_t kind);
StartupFlowStop RunStartupCaller1184(StartupFlowServices& services);
int32_t SetupStartupCallback1584(StartupFlowServices& services);
int ExitCallback1564(StartupFlowServices& services, int arg1, int arg2, void* common);
StartupModuleOutcome RunStartupModule4F4E8(StartupFlowServices& services,
    const char* path, const StartupModuleOptions& options);
StartupFlowStop RunStartupWorkerF7C(StartupFlowServices& services);
} // namespace dokodemo::recovered
