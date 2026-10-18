#include "startup_flow_native.h"

namespace dokodemo::recovered {
bool StartupStartResultSleeps1184(int32_t result) {
    // Source11D4..1230 signed branch tree reduces to these four bit patterns.
    const auto bits = static_cast<uint32_t>(result);
    return bits != 0x80020197u && bits != 0x80020198u &&
           bits != 0x80020064u && bits != 0x800201A4u;
}
StartupModuleOptions StartupOptions4F59C(int32_t kind) {
    StartupModuleOptions out{};
    out[0] = 20;
    out[4] = out[8] = kind == 1 ? uint8_t(1) : uint8_t(2);
    out[17] = 1;
    return out;
}
StartupFlowStop RunStartupCaller1184(StartupFlowServices& services) {
    const StartupWorkerRequest request{"main", 0xF7C, 0x10, 0x11000, 0x4000, nullptr};
    const int32_t uid = services.CreateWorker(request);
    services.StoreCreatedWorkerUid(uid); //11C8 executes even for BLEZ taken.
    if (uid <= 0) return StartupFlowStop::SourceReturned;
    const int32_t result = services.StartWorker(uid, 0, nullptr);
    if (!StartupStartResultSleeps1184(result)) return StartupFlowStop::SourceReturned;
    return services.SleepCaller();
}
int32_t SetupStartupCallback1584(StartupFlowServices& services) {
    const int32_t uid = services.CreateExitCallback({"gameexit", 0x1564, nullptr});
    return services.RegisterExitCallback(uid); //15A8 unguarded exact signed v0.
}
int ExitCallback1564(StartupFlowServices& services, int, int, void*) {
    services.ExitGame();
    return 0; //1574 only after the chosen native ExitGame adaptation returned.
}
StartupModuleOutcome RunStartupModule4F4E8(StartupFlowServices& services,
    const char* path, const StartupModuleOptions& options) {
    int32_t fd;
    for (;;) {
        if (services.HostCancelled()) return StartupModuleStop::HostCancelled;
        fd = services.OpenRead(path, 1, 0);
        if (fd >= 0) break;
        (void)services.CheckCallback(0x4F524);
    }
    auto load = services.LoadModuleById(fd, 0, options);
    const auto* loaded = std::get_if<StartupModuleReturned>(&load);
    if (!loaded) return load;
    const int32_t uid = loaded->value;
    if (uid < 0) return StartupModuleReturned{-1}; //4F548; no source close/unload
    if (services.HostCancelled()) return StartupModuleStop::HostCancelled;
    auto start = services.StartModule(uid, 0, nullptr, nullptr, nullptr);
    const auto* started = std::get_if<StartupModuleReturned>(&start);
    if (!started) return start;
    if (started->value < 0) return StartupModuleReturned{-1}; //4F568; fd retained
    if (services.HostCancelled()) return StartupModuleStop::HostCancelled;
    auto close = services.CloseModuleFile(fd);
    if (!std::holds_alternative<StartupModuleReturned>(close)) return close;
    return StartupModuleReturned{uid}; //4F578; close value ignored, return LOAD UID
}
StartupFlowStop RunStartupWorkerF7C(StartupFlowServices& services) {
    (void)SetupStartupCallback1584(services); //F7C ignores registration result.
    for (;;) {
        if (services.HostCancelled()) return StartupFlowStop::HostCancelled;
        if (services.CheckMedium() == 1) break;
        (void)services.CheckCallback(0x1468);
    }
    (void)services.Activate(1, "disc0:");
    (void)services.WaitDriveStat(0x22); //non-CB; both return values unchecked.
    if (services.HostCancelled()) return StartupFlowStop::HostCancelled;
    const auto kind1 = StartupOptions4F59C(1);
    const auto kind0 = StartupOptions4F59C(0);
    services.BeginModuleRequest(0x4F61C, "disc0:/PSP_GAME/USRDIR/prx/audiocodec.prx", kind1);
    auto module = RunStartupModule4F4E8(services,
        "disc0:/PSP_GAME/USRDIR/prx/audiocodec.prx", kind1);
    if (const auto* stopped = std::get_if<StartupModuleStop>(&module)) {
        switch (*stopped) {
        case StartupModuleStop::Unsupported: return StartupFlowStop::Unsupported;
        case StartupModuleStop::HostCancelled: return StartupFlowStop::HostCancelled;
        case StartupModuleStop::HostFailure: return StartupFlowStop::HostFailure;
        }
    }
    if (std::get<StartupModuleReturned>(module).value == -1) return StartupFlowStop::SourceReturned;
    services.BeginModuleRequest(0x4F640, "disc0:/PSP_GAME/USRDIR/prx/libatrac3plus.prx", kind0);
    module = RunStartupModule4F4E8(services,
        "disc0:/PSP_GAME/USRDIR/prx/libatrac3plus.prx", kind0);
    if (const auto* stopped = std::get_if<StartupModuleStop>(&module)) {
        switch (*stopped) {
        case StartupModuleStop::Unsupported: return StartupFlowStop::Unsupported;
        case StartupModuleStop::HostCancelled: return StartupFlowStop::HostCancelled;
        case StartupModuleStop::HostFailure: return StartupFlowStop::HostFailure;
        }
    }
    if (std::get<StartupModuleReturned>(module).value == -1) return StartupFlowStop::SourceReturned;
    // Retained gate4F660/delay664: only reached after the second wrapper's
    // actual Start and sourceClose have returned and its UID is not -1.
    services.BeginModuleRequest(0x4F660, "disc0:/PSP_GAME/USRDIR/prx/sc_sascore.prx", kind1);
    module = RunStartupModule4F4E8(services,
        "disc0:/PSP_GAME/USRDIR/prx/sc_sascore.prx", kind1);
    if (const auto* stopped = std::get_if<StartupModuleStop>(&module)) {
        switch (*stopped) {
        case StartupModuleStop::Unsupported: return StartupFlowStop::Unsupported;
        case StartupModuleStop::HostCancelled: return StartupFlowStop::HostCancelled;
        case StartupModuleStop::HostFailure: return StartupFlowStop::HostFailure;
        }
    }
    if (std::get<StartupModuleReturned>(module).value == -1) return StartupFlowStop::SourceReturned;
    // Retained4F678/delay67C -> call4F680, same kind1 options. Only the
    // supported third wrapper's real return makes this next Open reachable.
    services.BeginModuleRequest(0x4F680, "disc0:/PSP_GAME/USRDIR/prx/ifhandle.prx", kind1);
    module = RunStartupModule4F4E8(services,
        "disc0:/PSP_GAME/USRDIR/prx/ifhandle.prx", kind1);
    if (const auto* stopped = std::get_if<StartupModuleStop>(&module)) {
        switch (*stopped) {
        case StartupModuleStop::Unsupported: return StartupFlowStop::Unsupported;
        case StartupModuleStop::HostCancelled: return StartupFlowStop::HostCancelled;
        case StartupModuleStop::HostFailure: return StartupFlowStop::HostFailure;
        }
    }
    if (std::get<StartupModuleReturned>(module).value == -1) return StartupFlowStop::SourceReturned;
    services.UnsupportedFollowingModule(); // no subsequent dependency assumed
    return StartupFlowStop::Unsupported;
}
} // namespace dokodemo::recovered
