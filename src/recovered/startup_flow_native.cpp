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
    // Retained4F698/delay69C -> call4F6A0/pathDD698, kind0. Only the
    // fourth wrapper's actual returned Load UID makes this fifth Open reachable.
    services.BeginModuleRequest(0x4F6A0, "disc0:/PSP_GAME/USRDIR/prx/pspnet.prx", kind0);
    module = RunStartupModule4F4E8(services,
        "disc0:/PSP_GAME/USRDIR/prx/pspnet.prx", kind0);
    if (const auto* stopped = std::get_if<StartupModuleStop>(&module)) {
        switch (*stopped) {
        case StartupModuleStop::Unsupported: return StartupFlowStop::Unsupported;
        case StartupModuleStop::HostCancelled: return StartupFlowStop::HostCancelled;
        case StartupModuleStop::HostFailure: return StartupFlowStop::HostFailure;
        }
    }
    // 4F6A8 NOR / 4F6AC SLTU / 4F6BC: gate4F5E4 returns1 iff the
    // actual fifth wrapper's returned bits differ from FFFFFFFF. Main FBC's
    // taken branch (FC0 delay restore) then calls table gate4F6C8 at FF4.
    const auto inverted_fifth_result = ~static_cast<uint32_t>(std::get<StartupModuleReturned>(module).value);
    const uint32_t initial_module_gate_result = uint32_t(0) < inverted_fifth_result ? 1u : 0u;
    if (!initial_module_gate_result) return StartupFlowStop::SourceReturned; //FC4..FF0
    // Exact retained SUPPLEMENT_RAW EA4D0 record prefix. Pointer words are
    // symbolic source addends, never executed relocations or host pointers.
    constexpr std::array<uint8_t,24> first_rows{{
        0xFF,0xFF,0xFF,0xFF,1,0,0,0,0x30,0xD5,0x0D,0,
        0xFF,0xFF,0xFF,0xFF,1,0,0,0,0x58,0xD5,0x0D,0}};
    StartupRepeatedModuleSnapshot repeated;
    repeated.present=true;repeated.counter_known=true; // register s1=0 at4F6E4
    for(uint32_t i=0;i<first_rows.size();++i) {repeated.bytes[i]=first_rows[i];repeated.known[i]=1;}
    repeated.known_bytes=24;
    services.ObserveRepeatedModules(repeated,0x4F700); // s0 symbolic table binding now established
    const auto read_word=[&](uint32_t offset,uint32_t& value) noexcept {
        if(offset>repeated.bytes.size()-4) return false;
        value=0;for(uint32_t i=0;i<4;++i) {
            if(repeated.known[offset+i]!=1) return false;
            value|=uint32_t(repeated.bytes[offset+i])<<(8*i);
        }
        return true;
    };
    uint32_t kind=0;
    if(!read_word(4,kind)) {services.UnsupportedFollowingModule();return StartupFlowStop::Unsupported;}
    services.ObserveRepeatedModules(repeated,0x4F704);
    for(;;) {
        const uint32_t index=repeated.current_record_index,base=index*12;
        uint32_t path_addend=0;
        if(index>=2 || !read_word(base+8,path_addend) || kind>1 ||
            path_addend!=(index==0?0xDD530u:0xDD558u)) {
            services.UnsupportedFollowingModule();return StartupFlowStop::Unsupported;
        }
        services.ObserveRepeatedModules(repeated,0x4F708);
        const char* path=index==0?"disc0:/PSP_GAME/USRDIR/prx/memab.prx":
            "disc0:/PSP_GAME/USRDIR/prx/pspnet_adhoc_auth.prx";
        const auto& options=kind==1?kind1:kind0; //4F710..720:20*kind; a2=0
        services.BeginModuleRequest(0x4F71C,path,options);
        module=RunStartupModule4F4E8(services,path,options);
        if(const auto* stopped=std::get_if<StartupModuleStop>(&module)) {
            switch(*stopped) {
            case StartupModuleStop::Unsupported:return StartupFlowStop::Unsupported;
            case StartupModuleStop::HostCancelled:return StartupFlowStop::HostCancelled;
            case StartupModuleStop::HostFailure:return StartupFlowStop::HostFailure;
            }
        }
        const auto returned=std::get<StartupModuleReturned>(module).value;
        const uint32_t bits=static_cast<uint32_t>(returned);
        ++repeated.counter;services.ObserveRepeatedModules(repeated,0x4F724); // register, NOT a memory store
        for(uint32_t i=0;i<4;++i) {repeated.bytes[base+i]=uint8_t(bits>>(8*i));repeated.known[base+i]=1;}
        repeated.writes[repeated.writes_count]={repeated.writes_count,0x4F728,index,base,bits};
        ++repeated.writes_count;services.ObserveRepeatedModules(repeated,0x4F728);
        const bool more=static_cast<int32_t>(repeated.counter)<4; //4F72C signed SLTI
        services.ObserveRepeatedModules(repeated,0x4F72C);
        ++repeated.current_record_index;services.ObserveRepeatedModules(repeated,0x4F730);
        services.ObserveRepeatedModules(repeated,0x4F734);
        services.ObserveRepeatedModules(repeated,0x4F738); //ordinary delay clears a0 even on failure
        if(bits==0xFFFFFFFFu) {
            repeated.source_gate_returned=true;repeated.source_gate_result=0;
            services.ObserveRepeatedModules(repeated,0x4F758);return StartupFlowStop::SourceReturned;
        }
        services.ObserveRepeatedModules(repeated,0x4F73C);
        if(more) {
            // BNEL's taken delay reads the next kind. Unknown bytes stop BEFORE read.
            if(!read_word(repeated.current_record_index*12+4,kind)) {
                services.UnsupportedFollowingModule();return StartupFlowStop::Unsupported;
            }
            services.ObserveRepeatedModules(repeated,0x4F740);
            continue; //4F708; no second4F704 load
        }
        // Branch-likely not taken annuls4F740;4F744 sets a0=1.
        repeated.source_gate_returned=true;repeated.source_gate_result=1;
        services.ObserveRepeatedModules(repeated,0x4F758);
        services.UnsupportedFollowingModule();return StartupFlowStop::Unsupported;
    }
}
} // namespace dokodemo::recovered
