#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "asset_store.h"
#include "save_native.h"
#include "save_time.h"
#include "clock_backend_win32.h"
#include "rng_backend_win32.h"
#include "script_vm.h"
#include "audio_native.h"
#include "game_font.h"
#include "game_text_win32.h"
#include <imm.h>
#include "model_formats.hpp"
#include "startup_probe.h"
#include "startup_runtime_win32.h"
#include "sascore_runtime_win32.h"
#include "memab_runtime_win32.h"
#include "recovered/menu_primitives.h"
#include "recovered/calendar_native.h"
#include "recovered/service47_native.h"
#include "recovered/bulk_array_native.h"
#include "recovered/manager_queue_native.h"
#include "recovered/context_owner_native.h"
#include "recovered/context_callback_native.h"
#include "recovered/producer_native.h"
#include "audio_catalog.h"
#include <shellapi.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string_view>
#include <chrono>
#include <iostream>
#include <memory>
#include <utility>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

using namespace native_port;
namespace {
struct Options {
    bool selfTest = false, smoke = false, constructionSmoke = false;
    bool connectedStartupTest = false, connectedStartupWindowTest = false;
    bool graphicsSmoke = false,storageWindowTest=false,storageDirectTest=false,storageUnavailable=false,titleWindowTest=false;
    bool nativeTitleMenu=false,testRngSeedReplay=false;
    uint32_t testRngSeedWord=0;
    bool titleMenuWindowTest=false;
    bool softkeyWindowTest=false,actorWindowTest=false,gameLoopWindowTest=false,actorChannelWindowTest=false,sceneClockWindowTest=false;
    bool actorPositionWindowTest=false,cameraWindowTest=false,conversationWindowFixture=false,inventoryWindowFixture=false,game71WindowFixture=false,manager9WindowFixture=false,post17WindowFixture=false,post19WindowFixture=false,post21WindowFixture=false,post24WindowFixture=false,post25WindowFixture=false,post26WindowFixture=false,post28WindowFixture=false,activationWindowFixture=false,repeatWindowFixture=false,playWindowFixture=false,soundWindowFixture=false,inventoryActionWindowFixture=false,decorateWindowFixture=false,putawayWindowFixture=false,discardWindowFixture=false,kanaWindowFixture=false,gameplayWindowFixture=false,continuationWindowFixture=false,mainMenuWindowFixture=false,menuInventoryWindowFixture=false,menuMarkWindowFixture=false,menuPlaceWindowFixture=false,menuRoomWindowFixture=false,menuRoomOwnerDiagnostic=false,notebookWindowFixture=false,flow5WindowFixture=false,notePreviewWindowFixture=false,noteSelfWindowFixture=false;
    std::filesystem::path assets, evidence, clockSample, rngSample, startupMedia,nativeSaveRoot,fixtureChoiceHistory;
};
Options ParseOptions() {
    Options options;
    int count = 0; auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; ++i) {
        std::wstring arg = args[i];
        if (arg == L"--self-test") options.selfTest = true;
        else if (arg == L"--connected-startup-test") options.connectedStartupTest = true;
        else if (arg == L"--connected-startup-window-test") options.connectedStartupWindowTest = true;
        else if (arg == L"--graphics-smoke-test") { options.graphicsSmoke = true; options.connectedStartupWindowTest = true; }
        else if(arg==L"--storage-window-test"){options.storageWindowTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--storage-direct-window-test"){options.storageWindowTest=options.storageDirectTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--title-window-test"){options.titleWindowTest=options.storageWindowTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--title-menu-window-test"){options.titleMenuWindowTest=options.titleWindowTest=options.storageWindowTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--softkey-window-test"){options.softkeyWindowTest=options.titleWindowTest=options.storageWindowTest=options.storageDirectTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--actor-window-test"){options.actorWindowTest=options.softkeyWindowTest=options.titleWindowTest=options.storageWindowTest=options.storageDirectTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--game-loop-window-test"){options.gameLoopWindowTest=options.softkeyWindowTest=options.titleWindowTest=options.storageWindowTest=options.storageDirectTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--scene-clock-window-test"){options.sceneClockWindowTest=options.actorChannelWindowTest=options.gameLoopWindowTest=options.softkeyWindowTest=options.titleWindowTest=options.storageWindowTest=options.storageDirectTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--actor-position-window-test"){options.actorPositionWindowTest=options.sceneClockWindowTest=options.actorChannelWindowTest=options.gameLoopWindowTest=options.softkeyWindowTest=options.titleWindowTest=options.storageWindowTest=options.storageDirectTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--notebook-self-window-fixture"||arg==L"--notebook-preview-window-fixture"||arg==L"--flow5-window-fixture"||arg==L"--notebook-window-fixture"||arg==L"--menu-room-owner-diagnostic"||arg==L"--menu-room-window-fixture"||arg==L"--menu-place-window-fixture"||arg==L"--menu-mark-window-fixture"||arg==L"--menu-inventory-window-fixture"||arg==L"--main-menu-window-fixture"||arg==L"--continue-gameplay-window-fixture"||arg==L"--gameplay-window-fixture"||arg==L"--kana-word-window-fixture"||arg==L"--conversation-window-fixture"||arg==L"--inventory-window-fixture"||arg==L"--inventory-actions-window-fixture"||arg==L"--decorate-window-fixture"||arg==L"--putaway-window-fixture"||arg==L"--discard-window-fixture"||arg==L"--game71-window-fixture"||arg==L"--manager9-window-fixture"||arg==L"--post17-window-fixture"||arg==L"--post19-window-fixture"||arg==L"--post21-window-fixture"||arg==L"--post24-window-fixture"||arg==L"--post25-window-fixture"||arg==L"--post26-window-fixture"||arg==L"--post28-window-fixture"||(arg==L"--conversation-activation-window-fixture"||arg==L"--repeat-conversation-window-fixture"||arg==L"--play-conversation-window-fixture"||arg==L"--sound-conversation-window-fixture")){options.noteSelfWindowFixture=arg==L"--notebook-self-window-fixture";options.notePreviewWindowFixture=arg==L"--notebook-preview-window-fixture";options.flow5WindowFixture=arg==L"--flow5-window-fixture";options.notebookWindowFixture=options.noteSelfWindowFixture||options.notePreviewWindowFixture||options.flow5WindowFixture||arg==L"--notebook-window-fixture";options.menuRoomOwnerDiagnostic=arg==L"--menu-room-owner-diagnostic";options.menuRoomWindowFixture=options.menuRoomOwnerDiagnostic||arg==L"--menu-room-window-fixture";options.menuPlaceWindowFixture=options.menuRoomWindowFixture||arg==L"--menu-place-window-fixture";options.menuMarkWindowFixture=arg==L"--menu-mark-window-fixture";options.menuInventoryWindowFixture=options.menuPlaceWindowFixture||options.menuMarkWindowFixture||arg==L"--menu-inventory-window-fixture";options.mainMenuWindowFixture=options.notebookWindowFixture||arg==L"--main-menu-window-fixture"||options.menuInventoryWindowFixture;options.continuationWindowFixture=arg==L"--continue-gameplay-window-fixture"||options.mainMenuWindowFixture;options.gameplayWindowFixture=arg==L"--gameplay-window-fixture"||options.continuationWindowFixture;options.kanaWindowFixture=arg==L"--kana-word-window-fixture"||options.gameplayWindowFixture;options.discardWindowFixture=arg==L"--discard-window-fixture";options.putawayWindowFixture=arg==L"--putaway-window-fixture"||options.discardWindowFixture;options.decorateWindowFixture=arg==L"--decorate-window-fixture"||options.putawayWindowFixture;options.inventoryActionWindowFixture=arg==L"--inventory-actions-window-fixture"||options.decorateWindowFixture;options.soundWindowFixture=arg==L"--sound-conversation-window-fixture";options.playWindowFixture=arg==L"--play-conversation-window-fixture"||options.soundWindowFixture;options.repeatWindowFixture=arg==L"--repeat-conversation-window-fixture"||options.playWindowFixture;options.activationWindowFixture=arg==L"--conversation-activation-window-fixture"||options.repeatWindowFixture;options.post28WindowFixture=arg==L"--post28-window-fixture"||options.activationWindowFixture||options.kanaWindowFixture;options.post26WindowFixture=arg==L"--post26-window-fixture"||options.post28WindowFixture;options.post25WindowFixture=arg==L"--post25-window-fixture"||options.post26WindowFixture;options.post24WindowFixture=arg==L"--post24-window-fixture"||options.post25WindowFixture;options.post21WindowFixture=arg==L"--post21-window-fixture"||options.post24WindowFixture;options.post19WindowFixture=arg==L"--post19-window-fixture"||options.post21WindowFixture;options.post17WindowFixture=arg==L"--post17-window-fixture"||options.post19WindowFixture;options.manager9WindowFixture=arg==L"--manager9-window-fixture"||options.post17WindowFixture;options.game71WindowFixture=arg==L"--game71-window-fixture"||options.manager9WindowFixture||options.inventoryActionWindowFixture;options.inventoryWindowFixture=arg!=L"--conversation-window-fixture";options.conversationWindowFixture=options.cameraWindowTest=options.actorPositionWindowTest=options.sceneClockWindowTest=options.actorChannelWindowTest=options.gameLoopWindowTest=options.softkeyWindowTest=options.titleWindowTest=options.storageWindowTest=options.storageDirectTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--camera-message-window-test"){options.cameraWindowTest=options.actorPositionWindowTest=options.sceneClockWindowTest=options.actorChannelWindowTest=options.gameLoopWindowTest=options.softkeyWindowTest=options.titleWindowTest=options.storageWindowTest=options.storageDirectTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--actor-channel-window-test"){options.actorChannelWindowTest=options.gameLoopWindowTest=options.softkeyWindowTest=options.titleWindowTest=options.storageWindowTest=options.storageDirectTest=true;options.connectedStartupWindowTest=true;}
        else if(arg==L"--native-title-layout"&&i+1<count){std::wstring policy=args[++i];if(policy!=L"characters"&&policy!=L"menu")throw std::runtime_error("native title layout must be characters or menu");options.nativeTitleMenu=policy==L"menu";}
        else if(arg==L"--native-save-root" && i+1<count)options.nativeSaveRoot=args[++i];
        else if(arg==L"--fixture-choice-history"&&i+1<count)options.fixtureChoiceHistory=args[++i];
        else if(arg==L"--test-game-rng-seed"&&i+1<count){
            const std::wstring raw=args[++i];size_t used=0;const auto word=std::stoull(raw,&used,10);
            if(used!=raw.size()||raw.empty()||raw.front()==L'-'||word>UINT32_MAX)throw std::runtime_error("Fixture RNG seed must be uint32 decimal");
            options.testRngSeedReplay=true;options.testRngSeedWord=uint32_t(word);
        }
        else if(arg==L"--storage-unavailable")options.storageUnavailable=true;
        else if (arg == L"--smoke-test") options.smoke = true;
        else if (arg == L"--construction-smoke-test") { options.smoke = true; options.constructionSmoke = true; }
        else if (arg == L"--assets" && i + 1 < count) options.assets = args[++i];
        else if (arg == L"--evidence" && i + 1 < count) options.evidence = args[++i];
        else if (arg == L"--clock-sample" && i + 1 < count) options.clockSample = args[++i];
        else if (arg == L"--rng-sample" && i + 1 < count) options.rngSample = args[++i];
        else if (arg == L"--startup-media-root" && i + 1 < count) options.startupMedia = args[++i];
        else { LocalFree(args); throw std::runtime_error("Unknown or incomplete command line argument"); }
    }
    LocalFree(args);
    if(options.testRngSeedReplay&&!options.post21WindowFixture)throw std::runtime_error("Captured RNG seed replay requires explicit post21 window fixture");
    if(!options.fixtureChoiceHistory.empty()&&!options.continuationWindowFixture)throw std::runtime_error("Choice history requires explicit continuation fixture");
    options.assets = ResolveAssets(options.assets);
    if (options.startupMedia.empty()) options.startupMedia = options.assets.parent_path() / L"iso";
    options.startupMedia = std::filesystem::absolute(options.startupMedia);
    if (options.evidence.empty()) options.evidence = std::filesystem::current_path() / L"evidence";
    options.evidence = std::filesystem::absolute(options.evidence);
    if(options.clockSample.empty()&&(options.selfTest||options.smoke)) {
        const auto cwd=std::filesystem::current_path();
        const auto relative=L"research/verification/stage9/clock-sample/actual-sample.json";
        for(const auto& base:{options.assets.parent_path().parent_path(),cwd,cwd.parent_path(),cwd.parent_path().parent_path()}) {
            const auto candidate=base/relative;
            if(std::filesystem::is_regular_file(candidate)) {options.clockSample=candidate;break;}
        }
        if(options.clockSample.empty()) throw std::runtime_error("Self-test requires the captured Windows input via --clock-sample");
    }
    if(options.rngSample.empty()&&(options.selfTest||options.smoke)) {
        const auto cwd=std::filesystem::current_path();
        const auto relative=L"research/verification/stage10/rng-sample/actual-sample.json";
        for(const auto& base:{options.assets.parent_path().parent_path(),cwd,cwd.parent_path(),cwd.parent_path().parent_path()}) {
            const auto candidate=base/relative;
            if(std::filesystem::is_regular_file(candidate)) {options.rngSample=candidate;break;}
        }
        if(options.rngSample.empty()) throw std::runtime_error("Self-test requires captured initialization input via --rng-sample");
    }
    return options;
}
uint64_t ClockSampleInteger(const std::string& json,const char* field) {
    const std::string token=std::string("\"")+field+'"';
    const auto key=json.find(token);
    const auto colon=key==std::string::npos?key:json.find(':',key+token.size());
    if(colon==std::string::npos) throw std::runtime_error("Missing captured clock sample field");
    auto start=json.find_first_not_of(" \t\r\n",colon+1);
    if(start==std::string::npos) throw std::runtime_error("Missing captured clock integer");
    if(json[start]=='"') ++start;
    const auto end=json.find_first_not_of("0123456789",start);
    if(end==start) throw std::runtime_error("Captured clock field is not an unsigned integer");
    const auto value=json.substr(start,end-start);
    size_t consumed=0;
    const auto result=std::stoull(value,&consumed,10);
    if(consumed!=value.size()) throw std::runtime_error("Invalid captured clock integer");
    return result;
}
dokodemo::recovered::ClockCallbackReturn ReadCapturedClockSample(const std::filesystem::path& path) {
    if(path.extension()!=L".json") throw std::runtime_error("Captured clock input must be a JSON sample");
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot read captured native Windows clock sample");
    std::array<char,16385> buffer{};
    input.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
    const auto count=input.gcount();
    if(count==static_cast<std::streamsize>(buffer.size())) throw std::runtime_error("Captured clock sample exceeds size limit");
    const std::string json(buffer.data(),static_cast<size_t>(count));
    const auto filetime=ClockSampleInteger(json,"filetime_100ns");
    const auto cached=ClockSampleInteger(json,"cached_psp_tick_us");
    uint64_t converted=0;
    if(!dokodemo::platform::FileTimeUtcToCachedPspTick(filetime,converted)||converted!=cached
        ||dokodemo::recovered::ClockWord689B8(cached)!=ClockSampleInteger(json,"source_word"))
        throw std::runtime_error("Captured sample does not match the explicit Windows/RTC ABI conversion");
    return {true,cached};
}
// This reader accepts the flat captured-input schema, including metadata arrays.
// Capture success is a required premise; arithmetic consistency alone is insufficient.
std::map<std::string,std::string> CapturedRngFields(const std::string& json) {
    size_t at=0;
    if(json.compare(0,3,"\xEF\xBB\xBF")==0) at=3;
    const auto whitespace=[&]() {
        at=json.find_first_not_of(" \t\r\n",at);
        if(at==std::string::npos) throw std::runtime_error("Incomplete RNG sample object");
    };
    whitespace();
    if(json[at++]!='{') throw std::runtime_error("RNG sample must be an object");
    std::map<std::string,std::string> fields;
    whitespace();
    if(json[at]=='}') throw std::runtime_error("Empty RNG sample object");
    for(;;) {
        whitespace();
        if(json[at++]!='"') throw std::runtime_error("Invalid RNG sample field");
        const auto key_start=at;
        while(at<json.size()&&json[at]!='"') {
            if(json[at]=='\\'||static_cast<unsigned char>(json[at])<0x20)
                throw std::runtime_error("RNG sample keys must be plain schema names");
            ++at;
        }
        if(at==json.size()) throw std::runtime_error("Incomplete RNG sample key");
        const auto key=json.substr(key_start,at++-key_start);
        whitespace();
        if(json[at++]!=':') throw std::runtime_error("Missing RNG sample field separator");
        whitespace();
        const auto value_start=at;
        bool quoted=false,escaped=false;
        unsigned arrays=0;
        for(;at<json.size();++at) {
            const auto ch=json[at];
            if(quoted) {
                if(escaped) escaped=false;
                else if(ch=='\\') escaped=true;
                else if(ch=='"') quoted=false;
            } else if(ch=='"') quoted=true;
            else if(ch=='[') ++arrays;
            else if(ch==']') {
                if(!arrays) throw std::runtime_error("Invalid RNG sample array");
                --arrays;
            } else if(ch=='{') throw std::runtime_error("Nested RNG sample object is unsupported");
            else if((ch==','||ch=='}')&&!arrays) break;
        }
        if(at==json.size()||quoted||arrays) throw std::runtime_error("Incomplete RNG sample value");
        const auto value_end=json.find_last_not_of(" \t\r\n",at-1);
        if(value_end==std::string::npos||value_end<value_start
           ||!fields.emplace(key,json.substr(value_start,value_end-value_start+1)).second)
            throw std::runtime_error("Empty or duplicate RNG sample field");
        if(json[at++]=='}') break;
    }
    if(json.find_first_not_of(" \t\r\n",at)!=std::string::npos)
        throw std::runtime_error("Trailing RNG sample data");
    return fields;
}
uint64_t CapturedRngInteger(const std::map<std::string,std::string>& fields,
                            const std::string& key,bool quoted=false) {
    auto text=fields.at(key);
    if(quoted&&text.size()>=2&&text.front()=='"'&&text.back()=='"')
        text=text.substr(1,text.size()-2);
    if(text.empty()||text.find_first_not_of("0123456789")!=std::string::npos
       ||(text.size()>1&&text.front()=='0'))
        throw std::runtime_error("Invalid RNG sample unsigned integer");
    return std::stoull(text,nullptr,10);
}
struct CapturedRngSeed {
    uint64_t filetime_100ns=0;
    int32_t offset_minutes=0;
    dokodemo::platform::RngSeedConversion conversion;
};
CapturedRngSeed ReadCapturedRngSeed(const std::filesystem::path& path) {
    if(path.extension()!=L".json") throw std::runtime_error("RNG input must be a JSON sample");
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot read captured RNG initialization input");
    std::array<char,16385> buffer{};input.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
    const auto count=input.gcount();
    if(count==static_cast<std::streamsize>(buffer.size())) throw std::runtime_error("RNG sample exceeds size limit");
    const auto fields=CapturedRngFields(std::string(buffer.data(),static_cast<size_t>(count)));
    if(fields.at("schema")!="\"dokodemo.stage10.actual-rng-initialization-sample.v1\"")
        throw std::runtime_error("Unsupported RNG capture schema");
    for(const auto* field:{"available","precise_sample_observed","utc_date_returned",
                           "timezone_returned","local_date_returned","civil_filetime_returned"})
        if(fields.at(field)!="true") throw std::runtime_error("RNG capture did not return successful input");
    if(CapturedRngInteger(fields,"timezone_query_result")>2||CapturedRngInteger(fields,"last_error")!=0)
        throw std::runtime_error("RNG capture reports an API error");
    const auto& offset_text=fields.at("effective_offset_minutes");
    const auto digits=offset_text.size()&&offset_text.front()=='-'?size_t{1}:size_t{0};
    if(digits==offset_text.size()||offset_text.find_first_not_of("0123456789",digits)!=std::string::npos
       ||(offset_text.size()-digits>1&&offset_text[digits]=='0'))
        throw std::runtime_error("Invalid RNG timezone offset");
    const auto offset=std::stoll(offset_text,nullptr,10);
    if(offset < -1440||offset>1440) throw std::runtime_error("Invalid RNG timezone offset");
    CapturedRngSeed seed;seed.filetime_100ns=CapturedRngInteger(fields,"utc_filetime_100ns",true);
    seed.offset_minutes=static_cast<int32_t>(offset);
    if(!dokodemo::platform::RngSeedFromUtcFileTimeAndOffset(seed.filetime_100ns,seed.offset_minutes,seed.conversion)
       ||seed.conversion.local_civil_epoch_seconds!=CapturedRngInteger(fields,"seed_u32")
       ||CapturedRngInteger(fields,"seed_high_u32")!=0
       ||seed.conversion.utc_tick_us!=CapturedRngInteger(fields,"utc_rtc_tick_us",true)
       ||seed.conversion.local_tick_us!=CapturedRngInteger(fields,"local_rtc_tick_us",true))
        throw std::runtime_error("RNG sample contradicts the explicit successful RTC initialization ABI");
    constexpr char hex[]="0123456789abcdef";
    std::string output="\"";
    for(const auto byte:seed.conversion.source_sp20_bytes) {
        output+=hex[byte>>4];output+=hex[byte&15];
    }
    output+='"';
    if(fields.at("source_sp20_hex")!=output) throw std::runtime_error("RNG initializer output bytes disagree");
    return seed;
}
void Output(const std::string& text) {
    DWORD written = 0; auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle && handle != INVALID_HANDLE_VALUE) WriteFile(handle, text.data(), (DWORD)text.size(), &written, nullptr);
    OutputDebugStringW(Utf8(text).c_str());
}
bool VerifiedStartupPrefix(const dokodemo::startup::Trace& trace, const std::string& source) {
    // Expected endpoints come from the bounded original-word/KSC traces. The
    // execution budget is independent of the observed command count.
    const std::map<std::string,uint32_t> stops = {{"INU",0x6C057},{"KAERU",0x6BE6A},
        {"NEKO",0x8E0DE},{"ROBO",0x6BF9F},{"USAGI",0x67E82}};
    return trace.status==dokodemo::startup::Status::Halted && trace.executed==5633
        && trace.pc==stops.at(source) && trace.sp==8 && trace.fp==5 && trace.diagnostic.opcode==0x8B
        && trace.first_builtin.valid && trace.first_builtin.id==0xF0 && trace.first_builtin.host_service==78
        && trace.context.host_flag_104_known && trace.context.host_flag_104==0
        && trace.result.kind==dokodemo::startup::Value::Kind::StackReference && trace.result.index==12
        && trace.context.global_writes_complete && trace.context.global_writes.size()==700
        && trace.context.local_writes.size()==2 && trace.context.host_reference_bindings.empty();
}
bool VerifiedProjectedStartup(const dokodemo::startup::Trace& trace, const std::string& source) {
    const std::map<std::string,uint32_t> stops = {{"INU",0x6B317},{"KAERU",0x6B128},
        {"NEKO",0x8D39A},{"ROBO",0x6B25F},{"USAGI",0x67140}};
    return trace.status==dokodemo::startup::Status::Halted && trace.executed==6189
        && trace.pc==stops.at(source) && trace.sp==38 && trace.fp==8 && trace.diagnostic.opcode==0x8B
        && trace.result.kind==dokodemo::startup::Value::Kind::StackReference && trace.result.index==39
        && trace.context.global_writes_complete && trace.context.global_writes.size()==700
        && trace.context.local_writes.size()==2 && trace.context.host_reference_bindings.size()==5;
}
dokodemo::recovered::Service47Fixture ConditionalService47Fixture() {
    // Explicit mapped synthetic cursor input; actual game buffer availability
    // and format contents have not been established by this probe.
    dokodemo::recovered::Service47Fixture f;
    f.enabled=f.control_cell_mapped=f.guard_cell_mapped=f.state_cell_mapped=f.memory_disjoint=true;
    f.control_present=f.control_word_mapped=f.source_present=f.source_header_mapped=true;
    f.status_cell_mapped=f.format_window_mapped=true;
    f.source_magic=0x0011076A; f.guard=1;
    const auto append=[&](uint32_t word) {
        for(unsigned i=0;i<4;++i) f.format_bytes.push_back(static_cast<uint8_t>(word>>(8*i)));
    };
    append(0x9000001B); append(0x30272220);
    for(uint32_t i=0;i<26;++i) append(0xB0000000|(112+4*i));
    for(uint32_t i=0;i<26;++i) append(0x80000000);
    return f;
}
bool VerifiedService47Startup(const dokodemo::startup::Trace& trace, const std::string& source,
                             const dokodemo::script::Program& program) {
    const std::map<std::string,uint32_t> stops={{"INU",0x6B320},{"KAERU",0x6B131},
        {"NEKO",0x8D3A3},{"ROBO",0x6B268},{"USAGI",0x67149}};
    const auto& state=trace.context.service47;
    const auto& slots=trace.context.stack_window;
    const bool frontier=(trace.status==dokodemo::startup::Status::Halted&&trace.diagnostic.opcode==0x8B)
        ||trace.status==dokodemo::startup::Status::BudgetExhausted;
    return frontier && trace.executed==6192
        && trace.pc==stops.at(source) && trace.sp==38 && trace.fp==8
        && trace.result.kind==dokodemo::startup::Value::Kind::Word && trace.result.word==0x10000000
        && state.known && state.calls==1 && state.format_called && state.format_return==0
        && state.cursor_bytes==112 && state.decoded_words==54 && state.guard==0 && state.state==0xFFFF
        && trace.context.global_writes_complete && trace.context.global_writes.size()==700
        && trace.context.local_writes.size()==2 && trace.context.host_reference_bindings.size()==7
        && trace.context.host_reference_bindings.at(80)==dokodemo::startup::Value{}
        && trace.context.host_reference_bindings.at(84)==dokodemo::startup::Value{}
        && slots.size()>37 && slots[37].kind==dokodemo::startup::Value::Kind::Ctx28Reference
        && slots[37].index<program.header().ctx28_count
        && slots[37].source_file_offset==program.header().ctx28_start()+4*slots[37].index;
}
dokodemo::startup::Trace RunToBoundary(dokodemo::startup::StartupProbe& probe) {
    auto trace=probe.run(10000);
    while(trace.status==dokodemo::startup::Status::BudgetExhausted) trace=probe.run(10000);
    return trace;
}
bool VerifiedBulkStartup(const dokodemo::startup::Trace& trace,const std::string& source,uint8_t host) {
    if(host>1) return false;
    // Validation constants come from the linked original-word/native endpoint
    // receipts. They do not provide values to the executing game context.
    const std::map<std::string,std::array<uint32_t,3>> stops={
        {"INU",{0x6B35B,0x192B,15018}}, {"KAERU",{0x6B16C,0x192B,14108}},
        {"NEKO",{0x8D3DE,0x1916,15088}}, {"ROBO",{0x6B2A3,0x1916,13513}},
        {"USAGI",{0x67184,0x192B,14563}}};
    const std::map<std::string,std::string> hashes={
        {"INU","cd651bf5aababb72258ffdba6496f465e3287dcd4d6defc29c2c1bce86e4325a"},
        {"KAERU","025a7094390a11df718c9052524fd18bbf5731e8923863b09af3475676d616d0"},
        {"NEKO","6517f69e86f15009abd4400dca1807c3d7d4523c0af94145f6f94a71f3521b84"},
        {"ROBO","3bfe1917304d04827043d1e6922422235c10fe4a9c09b38d281b8899200692fe"},
        {"USAGI","058212425fad33e182f24496a8f7beebb258264588cb0f06dcf7bf64aa4c536c"}};
    const auto& endpoint=stops.at(source);
    const auto& context=trace.context;
    const auto& state=context.service47;
    return trace.status==dokodemo::startup::Status::Halted
        &&trace.executed==(host?endpoint[2]:8479u)&&trace.pc==endpoint[host]
        &&trace.sp==(host?12:175)&&trace.fp==8&&trace.diagnostic.opcode==0x8B
        &&trace.result.kind==dokodemo::startup::Value::Kind::Word&&trace.result.word==0
        &&context.stack_window.size()>=static_cast<size_t>(trace.sp)
        &&context.stack_window[static_cast<size_t>(trace.sp-2)].word==(host?0x20000014u:0x20000030u)
        &&context.global_writes_complete&&context.global_writes.size()==1400
        &&context.global_writes_sha256==hashes.at(source)
        &&context.local_writes.size()==2&&context.host_reference_bindings.size()==7
        &&state.known&&state.calls==1&&state.cursor_bytes==112&&state.decoded_words==54
        &&state.guard==0&&state.state==0xFFFF&&state.released_entities==0;
}
dokodemo::recovered::StringFixture ConditionalStringFixture() {
    dokodemo::recovered::StringFixture fixture;
    fixture.enabled=fixture.bss_window_mapped=fixture.heap_ready=true;
    fixture.memory_disjoint=fixture.allocator_policy_known=true;
    fixture.initial_heap.assign(fixture.heap_size,0xCC);
    // BOOT D8AD4 literal copied into an explicit mapped BSS window. Actual
    // producer execution/readiness is a premise, rather than inferred BSS data.
    fixture.bss_window={0x83,0x41,0x83,0x43,0x83,0x52,0,0,0x30,0x34,0x32,0x35,0x34,0x30,0x33,0x33};
    return fixture;
}
dokodemo::startup::StringService20Projection ConditionalStringGetters() {
    dokodemo::startup::StringService20Projection getters;
    getters.enabled=getters.memory_disjoint=getters.date_record_mapped=getters.shared_global_table_mapped=true;
    const auto padded=[](const char* text) {
        std::vector<uint8_t> bytes(16,0);
        std::copy_n(reinterpret_cast<const uint8_t*>(text),std::char_traits<char>::length(text),bytes.begin());
        return bytes;
    };
    getters.string_windows={{1,padded("TORO")},{5,padded("PORTABLE")},{7,padded("Tokyo")},{8,padded("BIRTHDAY")}};
    getters.scalar_words={{2,3},{4,7},{6,11},{9,14},{10,1}};
    getters.date_words={0,0,23,9,14,10,2026};
    return getters;
}
dokodemo::startup::Service48Projection ConditionalService48Projection() {
    dokodemo::startup::Service48Projection projection;
    auto& fixture=projection.fixture;
    fixture.enabled=fixture.manager_cell_mapped=fixture.manager_record_mapped=true;
    fixture.singleton_mapped=fixture.arena_pointer_mapped=fixture.memory_disjoint=true;
    fixture.manager_stable_during_call=fixture.pool_pointer_cell_mapped=true;
    fixture.original_return_word=0x10000000;
    projection.general_heap=ConditionalStringFixture();
    projection.general_heap.bss_window_mapped=false;
    projection.general_heap.bss_window.clear();
    projection.general_heap.heap_base=0x600000;
    const auto append=[&](uint32_t word) {
        for(unsigned i=0;i<4;++i) projection.prepared_payload.push_back(static_cast<uint8_t>(word>>(8*i)));
    };
    // Explicit loaded NONNULL record; actual producer/loading is still unknown.
    append(0x900000A2);
    for(unsigned i=0;i<162;++i) append(0x80000000);
    return projection;
}
dokodemo::startup::Trace StorageStartup(const dokodemo::script::Program& program,uint8_t host) {
    const auto record=host?dokodemo::startup::Service48Projection{}:ConditionalService48Projection();
    dokodemo::startup::StartupProbe probe(program,host,512,{true,true,true,true,true,0},
        ConditionalService47Fixture(),ConditionalStringFixture(),ConditionalStringGetters(),record);
    return RunToBoundary(probe);
}
bool VerifiedStorageStartup(const dokodemo::startup::Trace& trace,const std::string& source,uint8_t host,
                            const dokodemo::script::Program& program) {
    const std::map<std::string,uint32_t> host1_counts={
        {"INU",15207},{"KAERU",14297},{"NEKO",15277},{"ROBO",13702},{"USAGI",14752}};
    const auto& context=trace.context;
    const auto& old=context.service47;
    const auto& bytes=program.bytes();
    const size_t offset=28u+trace.pc;
    if(offset+2>=bytes.size() ||bytes[offset]!=0x8B
        ||bytes[offset+1]!=(host?0xF0:9) ||(!host&&bytes[offset+2]!=0)) return false;
    if(host>1 || trace.status!=dokodemo::startup::Status::Halted ||trace.diagnostic.opcode!=0x8B
        ||trace.executed!=(host?host1_counts.at(source):8504u) ||trace.fp!=(host?12:10) ||trace.sp!=(host?15:13)
        ||!context.global_writes_complete ||context.local_writes.size()!=2
        ||context.global_writes.size()!=(host?1414u:1404u) ||!old.known ||old.calls!=1
        ||old.cursor_bytes!=112 ||old.decoded_words!=54 ||old.guard!=0 ||old.state!=0xFFFF) return false;
    if(host) return context.string_arena.known &&context.string_arena.live_allocations==7
        &&context.string_arena.allocations==17 &&context.string_arena.releases==10 &&context.string_arena.copies==17
        &&context.string_arena.conversion_cursor==0
        &&context.string_arena.sha256=="142e94eb9e9be9d0e759e1314a6ed628a4b2d74a0f374e38440e3a76914d62b7"
        &&context.host_reference_bindings.size()==7 &&bytes[offset+2]==2
        &&context.stack_window.at(13).word==0x20000027
        &&context.stack_window.at(14).kind==dokodemo::startup::Value::Kind::StackReference
        &&context.stack_window.at(14).index==12 &&trace.result.word==0;
    return context.general_arena.known &&context.general_arena.live_allocations==0
        &&context.general_arena.allocations==2 &&context.general_arena.releases==2
        &&context.general_arena.sha256=="40ad7e5e5b312cc629b2939128b5759ddd9a3218bc7d6449c44b3423e6ef21a1"
        &&context.service48.completed_calls==1 &&context.service48.heap_release_calls==2
        &&context.service48.cursor_bytes==652 &&context.service48.decoded_words==163
        &&context.service48.manager_record==0 &&context.service48.manager_clears==1
        &&context.host_reference_bindings.size()==8 &&trace.result.word==0x10000000;
}
dokodemo::startup::Service39Projection ConditionalService39Projection() {
    dokodemo::startup::Service39Projection projection;
    projection.enabled=projection.router_context_mapped=projection.reference_tables_mapped=true;
    // Strong caller premise: the abstract writable TEMP backing is distinct
    // from all heaps/controllers/tables/input/output/global state. No physical
    // result address is admitted or inferred from generic memory readability.
    projection.memory_disjoint=true;
    return projection;
}
std::unique_ptr<dokodemo::startup::StartupProbe> ClockStorageProbe(
    const dokodemo::script::Program& program,uint8_t host,dokodemo::recovered::ClockDependency clock) {
    const auto record=host?dokodemo::startup::Service48Projection{}:ConditionalService48Projection();
    return std::make_unique<dokodemo::startup::StartupProbe>(program,host,uint16_t{512},
        dokodemo::startup::HostAbiProjection{true,true,true,true,true,0},
        ConditionalService47Fixture(),ConditionalStringFixture(),ConditionalStringGetters(),record,
        std::move(clock),ConditionalService39Projection());
}
std::unique_ptr<dokodemo::startup::StartupProbe> CurrentRngProbe(
    const dokodemo::script::Program& program,uint8_t host,dokodemo::recovered::ClockDependency clock,
    uint32_t initializer_output_word) {
    dokodemo::startup::RngProjection rng;
    rng.enabled=true;
    rng.current.ApplyInitializerTimeT32(initializer_output_word,uint64_t(host)+1,
        "Captured GetTime_t output; separate scenario, untraced pre-KSC callback history unknown");
    const auto record=host?dokodemo::startup::Service48Projection{}:ConditionalService48Projection();
    return std::make_unique<dokodemo::startup::StartupProbe>(program,host,uint16_t{512},
        dokodemo::startup::HostAbiProjection{true,true,true,true,true,0},
        ConditionalService47Fixture(),ConditionalStringFixture(),ConditionalStringGetters(),record,
        std::move(clock),ConditionalService39Projection(),dokodemo::startup::Array98Projection{true},std::move(rng));
}
bool ReachedCurrentRngOwnershipBoundary(const dokodemo::startup::Trace& trace,uint8_t host) {
    // Endpoint-only application assertion. Exact commands/full-state proof is
    // separate; readonly CURRENT ctx28 is a fixture policy, not a PSP fact.
    return host<2&&trace.status==dokodemo::startup::Status::Halted
        &&trace.pc==0xEB0&&trace.diagnostic.opcode==0x51
        &&trace.context.rng&&trace.context.rng->seed_available
        &&trace.context.rng->origin==dokodemo::recovered::RngSeedOrigin::InitializerTimeT32
        &&!trace.context.rng->scratch_available&&trace.context.global_writes_complete
        &&trace.context.local_writes.size()==2
        &&trace.context.host_reference_bindings.size()==(host?7u:8u);
}
bool VerifiedClockStorageStartup(const dokodemo::startup::Trace& trace,const std::string& source,
                                uint8_t host,const dokodemo::script::Program& program) {
    // These are source-linked captured-sample validation constants, never
    // input values for the executing VM. Live Windows input is separately
    // reported as observed execution, rather than this exact fixture verdict.
    const std::map<std::string,uint32_t> host1_counts={
        {"INU",15365},{"KAERU",14455},{"NEKO",15435},{"ROBO",13860},{"USAGI",14910}};
    const std::map<std::string,std::array<const char*,2>> hashes={
        {"INU",{"7a364c3e5550457d0fd8c8eaa880036d0c33170f3aebb80ecdb97b8c23422d10","f20fdbb5e3306c0b36b22d24a63c401c0fdc07739fc9e5f57ac06b068cabdd7d"}},
        {"KAERU",{"10936e0bbe87939c146d78fc0f9dc25c7152498769988f070597338bba29e95c","2197fd53b7cac444bd58bf4af98a439186caf272641b91729b0ebbd4263c7533"}},
        {"NEKO",{"32ad8fe928f475718d795813fff4805b90ac15cb7c0998eeec70c22e9ab57588","e99229d38532f6e5f83e086a25309b6b80841f9d95e2fc8411c9b8b17b785bce"}},
        {"ROBO",{"942511e5cbb5d4816c4866f1b12377860c47aa1690cac437fec372ff95204065","5e829d60a2ece5f6cefb3b25f2e480efc26d099a586e6a4e24c3dde2073ae370"}},
        {"USAGI",{"d3b64718f1a9ee25400b99a9a7f2a45a7cc0d39cc4b5244ec4b3bed84b7c9148","742fb9a48a85964556f55abc2db9ea50905fa707c16ffde228b8dfe597ec4789"}}};
    if(host>1||trace.status!=dokodemo::startup::Status::Halted) return false;
    const auto& context=trace.context;
    const auto& old=context.service47;
    const auto& bytes=program.bytes();
    const auto offset=28u+trace.pc;
    if(offset+2>=bytes.size()||trace.executed!=(host?host1_counts.at(source):10650u)
        ||trace.sp!=16||trace.fp!=(host?11:10)||trace.result.kind!=dokodemo::startup::Value::Kind::Word
        ||trace.result.word!=0||!context.global_writes_complete
        ||context.global_writes.size()!=(host?1427u:1565u)
        ||context.global_writes_sha256!=hashes.at(source)[host]
        ||context.local_writes.size()!=2||context.host_reference_bindings.size()!=(host?7u:8u)
        ||!old.known||old.calls!=1||old.cursor_bytes!=112||old.decoded_words!=54
        ||old.guard!=0||old.state!=0xFFFF) return false;
    if(host) return trace.pc==0x17A6&&bytes[offset]==0xF6
        &&context.string_arena.known&&context.string_arena.allocations==29
        &&context.string_arena.releases==18&&context.string_arena.copies==29
        &&context.string_arena.live_allocations==11&&context.string_arena.conversion_cursor==0
        &&context.string_arena.sha256=="65f39eec7c302a27a3aee2471ce9e3b651fa370a1608892d9ce77813ec073777";
    return trace.pc==((source=="NEKO"||source=="ROBO")?0x180Au:0x181Fu)
        &&bytes[offset]==0x8B&&bytes[offset+1]==98&&bytes[offset+2]==3
        &&context.stack_window.at(13).kind==dokodemo::startup::Value::Kind::GlobalReference
        &&context.stack_window.at(14).kind==dokodemo::startup::Value::Kind::Ctx28Reference
        &&context.stack_window.at(15).kind==dokodemo::startup::Value::Kind::Ctx28Reference
        &&context.general_arena.known&&context.general_arena.live_allocations==0
        &&context.general_arena.sha256=="40ad7e5e5b312cc629b2939128b5759ddd9a3218bc7d6449c44b3423e6ef21a1";
}
class Service47TestCells final : public dokodemo::recovered::Service47Cells {
public:
    std::array<dokodemo::recovered::Service47Cell,4> slots{};
    dokodemo::recovered::Service47Cell load(dokodemo::recovered::Service47Address a) override {
        if(a.table!=dokodemo::recovered::Service47Address::Table::Stack) throw std::runtime_error("Unmapped test table");
        return slots.at(a.index);
    }
    void store(dokodemo::recovered::Service47Address a,dokodemo::recovered::Service47Cell value) override {
        if(a.table!=dokodemo::recovered::Service47Address::Table::Stack) throw std::runtime_error("Unmapped test table");
        slots.at(a.index)=value;
    }
};
struct OwnedManagerPrefix {
    std::unique_ptr<dokodemo::recovered::OwnedManagerQueue> manager;
    std::unique_ptr<dokodemo::recovered::OwnedContextOwner> callbackOwner;
    dokodemo::recovered::ManagerDrainResult drain;
    dokodemo::recovered::ManagerSnapshot snapshot;
    dokodemo::recovered::ContextOwnerSnapshot ownerSnapshot;
    dokodemo::recovered::ContextOwnerBoundary initializationCut, callbackCut;
    bool ownerCheckpoint=false, ownerVerified=false;
    bool verified=false;
};
OwnedManagerPrefix ConstructManagerPrefix(bool ownerCheckpoint=false) {
    using namespace dokodemo::recovered;
    OwnedManagerPrefix result;
    auto made=OwnedManagerQueue::Create(15,32);
    if(!made.manager) throw std::runtime_error("Owned manager allocation failed");
    result.manager=std::move(made.manager);
    const auto registry=result.manager->CreateRegistry(5,2);
    if(!registry.accepted||!registry.enqueued) throw std::runtime_error("Owned registry construction failed");
    const std::array<uint32_t,5> events{{1,2,6,9,10}};
    const std::array<uint32_t,5> callbacks{{0x23424,0x2345C,0x23A70,0x23DA0,0x23E08}};
    for(size_t i=0;i<events.size();++i) {
        const auto entry=result.manager->RegisterCallback(registry.token,events[i],{callbacks[i],0},i<3?0x202u:0x402u);
        if(!entry.accepted||!entry.enqueued) throw std::runtime_error("Owned source registration failed");
    }
    result.drain=result.manager->Drain();
    result.snapshot=result.manager->Snapshot();
    result.verified=result.drain.commands_processed==6&&result.drain.cursor_reset
        &&result.drain.boundary.kind==ManagerStopKind::DirectCallback
        &&result.drain.boundary.source_pc==0x930D0&&result.drain.boundary.descriptor.function==0x23424
        &&result.snapshot.terminal&&result.snapshot.allocated_bytes==1136
        &&result.snapshot.queue_cursor_bytes==0&&result.snapshot.registries.size()==1
        &&result.snapshot.registries[0].occupied==5;
    // The callback owner is an independently owned96-byte CKingKai payload,
    // separate from the manager's28-byte registry projection and CLoadMan4F4.
    auto owner=OwnedContextOwner::Create();
    if(!owner.owner) throw std::runtime_error("Owned callback payload allocation failed");
    result.initializationCut=owner.boundary;
    result.callbackOwner=std::move(owner.owner);
    const auto binding=result.callbackOwner->BindManagerRegistry(*result.manager,registry.token);
    result.ownerCheckpoint=ownerCheckpoint;
    if(ownerCheckpoint) {
        // Explicit post-external-setup fixture; unresolved1B188 is not executed
        // or reported as successful. These mapped words do not enable host flags.
        const auto tail=result.callbackOwner->ApplyInitializationTailFromCheckpoint();
        if(tail.kind!=ContextOwnerStop::None) throw std::runtime_error("Owned suffix checkpoint refused");
        ContextOwnerCallbackInputs inputs;
        inputs.gate_segment1_10fd4={true,0};
        inputs.kernel_context_segment1_e5880={true,0};
        result.callbackCut=result.callbackOwner->ContinueManagerCallback(*result.manager,result.drain.boundary,inputs);
    }
    result.ownerSnapshot=result.callbackOwner->Snapshot();
    result.ownerVerified=binding.kind==ContextOwnerStop::None&&result.ownerSnapshot.bound_records_match
        &&result.ownerSnapshot.blocks.size()==2&&result.ownerSnapshot.blocks[0].bytes.size()==96
        &&result.ownerSnapshot.blocks[1].bytes.size()==164
        &&result.initializationCut.kind==ContextOwnerStop::InitializationCall
        &&(!ownerCheckpoint||(result.callbackCut.kind==ContextOwnerStop::KernelCall
            &&result.callbackCut.source_pc==0x74DBC&&result.callbackCut.callee==0xCB288
            &&result.callbackCut.scheduler_budget_low==2000&&result.callbackCut.scheduler_budget_high==0));
    return result;
}
struct OwnedProducerPrefix {
    OwnedManagerPrefix prefix;
    std::unique_ptr<dokodemo::recovered::OwnedProducer> producer;
    dokodemo::recovered::ProducerMutation initialization;
    dokodemo::recovered::ProducerSnapshot snapshot;
    bool verified=false;
};
OwnedProducerPrefix ConstructProducerPrefix(dokodemo::recovered::ProducerInput input,
        dokodemo::recovered::ProducerAllocationPlan allocation = {}) {
    using namespace dokodemo::recovered;
    OwnedProducerPrefix result;
    result.prefix=ConstructManagerPrefix();
    // The96-byte owner stops at its actual call to1B188. Close that callee
    // with separately owned108-byte backing and explicit source-input and
    // allocator-return premises. Do not apply the later owner checkpoint or
    // infer completion of the caller's remaining initialization instructions.
    if(result.prefix.initializationCut.kind!=ContextOwnerStop::InitializationCall
        ||result.prefix.initializationCut.source_pc!=0x2318C
        ||result.prefix.initializationCut.callee!=0x1B188) return result;
    auto made=OwnedProducer::Create(input,std::move(allocation));
    result.initialization=made.initialization;
    result.producer=std::move(made.owner);
    if(!result.producer) return result;
    result.snapshot=result.producer->Snapshot();
    result.verified=result.prefix.verified&&result.prefix.ownerVerified
        &&result.initialization.completed
        &&result.snapshot.live_blocks.size()==1
        &&result.snapshot.live_blocks[0].bytes.size()==108
        &&std::all_of(result.snapshot.live_blocks[0].initialized.begin(),
            result.snapshot.live_blocks[0].initialized.end(),[](uint8_t known){return known==1;})
        &&!result.prefix.ownerSnapshot.initialization_tail_applied
        &&result.prefix.ownerSnapshot.blocks[0].initialized.at(0x2C)==0;
    return result;
}
struct OwnedSuffixPrefix {
    OwnedProducerPrefix initialized;
    dokodemo::recovered::ContextOwnerPrefixResult suffix;
    bool verified=false;
};
dokodemo::recovered::ContextOwnerLoadCheckpoint ConditionalLoadCheckpoint(uint32_t character_index) {
    // Explicit source checkpoint, not a constructed/installed PSP CLoadMan.
    dokodemo::recovered::ContextOwnerLoadCheckpoint load;
    load.source_address=0x500000;load.source_stack_address=0x700000;
    for(unsigned i=0;i<4;++i) {
        load.bytes[0x58+i]=static_cast<uint8_t>(character_index>>(8*i));
        load.initialized[0x58+i]=1;
    }
    return load;
}
OwnedSuffixPrefix ConstructOwnerSuffixPrefix(dokodemo::recovered::ProducerInput input,
        const dokodemo::recovered::ContextOwnerLoadCheckpoint& load) {
    using namespace dokodemo::recovered;
    OwnedSuffixPrefix result;result.initialized=ConstructProducerPrefix(input);
    auto& initialized=result.initialized;
    if(!initialized.verified||!initialized.producer||!initialized.prefix.callbackOwner) return result;
    result.suffix=initialized.prefix.callbackOwner->ContinueInitialization23194AfterProducer(*initialized.producer,load);
    const auto owner=initialized.prefix.callbackOwner->Snapshot();
    result.verified=result.suffix.entry_pc==0x23194
        &&result.suffix.boundary.kind==ContextOwnerStop::InitializationCall
        &&result.suffix.boundary.source_pc==0x231C4&&result.suffix.boundary.callee==0xC36C0
        &&!owner.initialization_tail_applied&&owner.blocks.at(0).initialized.at(0x2C)==0;
    return result;
}
dokodemo::recovered::ContextOwnerPreparedNonzero2327C ConditionalPreparedActivation(
        const dokodemo::script::Program& program,uint32_t cache_word) {
    using namespace dokodemo::recovered;
    if(!program.loaded()||program.bytes().size()<28) return {};
    ContextOwnerPreparedNonzero2327C p;
    p.acknowledges_unverified_prior_load_constructor_and_callback=true;
    p.source_owner_address=0x600000;p.source_vm_address=0x610000;
    p.loadman_pointer_10cdc4={true,0x620000};
    p.cached_activation_word_1c93fc={true,cache_word};
    p.prior_vm_publication_10e448={true,0};
    const auto add=[&](uint32_t address,size_t bytes,bool writable) {
        ContextOwnerPreparedBlock b;b.address=address;b.bytes.assign(bytes,0xA5);
        b.initialized.assign(bytes,0);b.writable=writable;p.blocks.push_back(std::move(b));
    };
    add(0x610000,0x47C,true);add(0x620000,0x4F4,false);
    add(0x630000,28,false);add(0x640000,12,true);add(0x650000,8,true);
    const auto word=[&](size_t block,size_t off,uint32_t value) {
        for(unsigned i=0;i<4;++i){p.blocks.at(block).bytes.at(off+i)=static_cast<uint8_t>(value>>(8*i));p.blocks.at(block).initialized.at(off+i)=1;}
    };
    word(0,0x0C,0x630000);word(0,0x10,0x650000);word(0,0x20,0x640000);
    p.blocks[1].bytes[0x4C8]=0;p.blocks[1].initialized[0x4C8]=1;
    std::copy_n(program.bytes().begin(),28,p.blocks[2].bytes.begin());
    std::fill(p.blocks[2].initialized.begin(),p.blocks[2].initialized.end(),uint8_t{1});
    word(3,4,0);word(3,8,0);
    return p;
}
// Finite source checkpoint, shared by the five sealed construction joins.
// It does not assert that the game's heap, directory manager or file loader
// reached this state. The KSC bytes themselves come from the verified Program.
dokodemo::recovered::ContextOwnerPreparedBlock ConstructionDomainBlock(
        uint32_t address,uint32_t size,uint8_t fill) {
    dokodemo::recovered::ContextOwnerPreparedBlock b;
    b.address=address;b.bytes.assign(size,fill);b.initialized.assign(size,1);b.writable=true;
    return b;
}
dokodemo::recovered::ContextOwnerHeapCheckpoint ConstructionDomainHeap(
        uint32_t controller,uint32_t arena,uint32_t size) {
    using namespace dokodemo::recovered;
    ContextOwnerHeapCheckpoint h;h.controller=controller;
    h.blocks={ConstructionDomainBlock(controller,64,0),ConstructionDomainBlock(arena,size,0xCC)};
    const auto put=[](ContextOwnerPreparedBlock& b,uint32_t offset,uint32_t width,uint32_t value) {
        for(uint32_t j=0;j<width;++j)b.bytes.at(offset+j)=static_cast<uint8_t>(value>>(8*j));
    };
    auto& c=h.blocks[0];auto& b=h.blocks[1];const uint32_t first=arena+32,end=arena+size-32;
    put(c,4,4,arena);put(c,8,4,end);put(c,12,4,7);put(c,16,1,1);
    constexpr uint8_t name[]={0x46,0x49,0x4E,0x49,0x54,0x45,0};
    std::copy(std::begin(name),std::end(name),c.bytes.begin()+17);
    for(uint32_t off:{0u,32u,size-32}) {
        std::fill_n(b.bytes.begin()+off,32,uint8_t{0});put(b,off+12,2,7);put(b,off+15,1,0x79);
    }
    put(b,4,4,first);put(b,20,4,first);put(b,14,1,1);
    put(b,32,4,arena);put(b,36,4,end);put(b,44,2,7);
    put(b,48,4,arena);put(b,52,4,end);put(b,56,4,end-first-16);
    put(b,size-32,4,first);put(b,size-18,1,1);put(b,size-16,4,first);
    return h;
}
struct OwnedActualConstructionPrefix {
    OwnedProducerPrefix initialized;
    dokodemo::recovered::ContextOwnerConstructionResult entry;
    dokodemo::recovered::ContextOwnerConstructorResult constructor;
    dokodemo::recovered::ContextCallbackResult callback;
    dokodemo::recovered::ContextOwnerConstructionSnapshot snapshot;
    bool verified=false;
};
OwnedActualConstructionPrefix ConstructActualConstructionPrefix(
        const dokodemo::script::Program& program,uint32_t character_index) {
    using namespace dokodemo::recovered;
    OwnedActualConstructionPrefix out;
    constexpr const char* hashes[]={
        "c64773090b1c331d043b264b291c8948a8b99787f541ddc44d8e1d027958c93b",
        "1a0c7a42970951d46254aaaccff5b5b5a4b551b1bcf63b9fb0ca4a9ac3021405",
        "d94768138b839591a81f4a7d3eebe1887877a799f9c73f287a253431d90a7ec3",
        "89882ec234e9bb3cb204bbabb6b1cdc190242275450179c2619c85aea33066b2",
        "16ee223dcb98c962648d4b62ffd14e06d19f9ea6b28a2d39d2c5a59e2c607a24"};
    // Refuse unloaded, failed-load or mismatched script inputs BEFORE creating
    // owners, storage or checkpoint heaps. This preserves the OwnerR2 guard.
    if(!program.loaded()||program.bytes().size()<28||character_index>=5
        ||program.source().sha256!=hashes[character_index]) return out;
    auto staticHeap=std::make_shared<ContextOwnerHeapCheckpoint>(
        ConstructionDomainHeap(0x1C93D4,0x202000,0x7020));
    ProducerAllocationPlan allocation;allocation.source_address=0x202030;
    allocation.mapped_allocator=[staticHeap](uint32_t call_pc,uint32_t bytes) {
        std::shared_ptr<CurrentTablesStorageImage> backing;
        if(call_pc!=0x63D24||bytes!=108) return backing;
        auto allocated=AllocateConstructionVm63D14(*staticHeap,bytes);
        if(!allocated.completed||allocated.return_value!=0x202030) return backing;
        staticHeap->blocks=std::move(allocated.blocks);
        for(const auto& block:staticHeap->blocks) {
            if(allocated.return_value<block.address
                ||uint64_t(allocated.return_value)+bytes>uint64_t(block.address)+block.bytes.size()) continue;
            const auto offset=allocated.return_value-block.address;
            backing=std::make_shared<CurrentTablesStorageImage>();
            backing->source_address=allocated.return_value;backing->live=true;
            backing->bytes.assign(block.bytes.begin()+offset,block.bytes.begin()+offset+bytes);
            backing->initialized.assign(block.initialized.begin()+offset,
                block.initialized.begin()+offset+bytes);
            break;
        }
        return backing;
    };
    out.initialized=ConstructProducerPrefix({true,0x200A00},std::move(allocation));
    if(!out.initialized.verified||!out.initialized.producer
        ||!out.initialized.prefix.callbackOwner) return out;
    auto registration=out.initialized.producer->BorrowCallbackBacking();
    const auto producerBacking=registration.backing;
    const bool registered=bool(registration.token)&&producerBacking&&producerBacking->live
        &&registration.singleton.initialized&&registration.singleton.value==0x202030
        &&registration.registered_callback.initialized&&registration.registered_callback.value==0x1C0FC;
    const auto cancelled=out.initialized.producer->CancelCallbackBorrow(registration.token);
    if(!registered||cancelled.kind!=ProducerStop::None) return out;
    // Administrative component seam: mirror the actual shared initializer
    // stores into its aggregate finite heap, never a detached Snapshot copy.
    for(auto& block:staticHeap->blocks) {
        if(producerBacking->source_address<block.address
            ||uint64_t(producerBacking->source_address)+producerBacking->bytes.size()
                >uint64_t(block.address)+block.bytes.size()) continue;
        const auto offset=producerBacking->source_address-block.address;
        std::copy(producerBacking->bytes.begin(),producerBacking->bytes.end(),block.bytes.begin()+offset);
        std::copy(producerBacking->initialized.begin(),producerBacking->initialized.end(),
            block.initialized.begin()+offset);
    }
    ContextOwnerLoadCheckpoint load;load.source_address=0x200000;
    load.source_stack_address=0x22F000;load.initialized.fill(1);
    load.bytes[0x58]=static_cast<uint8_t>(character_index);
    ContextOwnerConstructionInputs input;input.source_owner_address=0x200600;
    // Storage directory-manager checkpoint; distinct from the GM queue above.
    input.manager_address=0x200A00;input.manager_count=0;
    input.initial_storage_pointer={true,0x600000};input.initial_storage_index={true,0xFFFFFFFF};
    input.preexisting_image=std::make_shared<ContextOwnerStorageImage>();
    auto& image=*input.preexisting_image;
    image.source_address=0x600000;image.identity=0x600000;image.live=true;
    image.bytes=program.bytes();image.initialized.assign(image.bytes.size(),1);
    input.heap=*staticHeap;
    auto& owner=*out.initialized.prefix.callbackOwner;
    out.entry=owner.BeginActualConstruction23194(*out.initialized.producer,load,input);
    if(!out.entry.reached_constructor_entry) {out.snapshot=owner.ConstructionSnapshot();return out;}
    ContextOwnerConstructorDomain domain;
    domain.globals=ConstructionDomainBlock(0x1C9454,0x50,0);
    for(uint32_t i=0;i<4;++i)domain.globals.bytes[i]=
        static_cast<uint8_t>(registration.registered_callback.value>>(8*i));
    domain.dynamic_heap=ConstructionDomainHeap(0x240000,0x400000,0x20000);
    domain.loader.borrowed_image=input.preexisting_image;
    domain.loader.source_sha256=program.source().sha256;
    domain.loader.readable.assign(image.bytes.size(),1);domain.loader.writable.assign(image.bytes.size(),1);
    // These finite premises do not establish a storage producer or historical
    // allocator readiness. The callback slot comes from the live initializer;
    // the distinct loader mapping.callback argument remains zero.
    domain.loader.mapping.allocator_boundary_available=true;
    domain.loader.mapping.borrowed_image_lifetime_available=true;
    out.constructor=owner.ContinueActualConstruction6656C(out.entry.constructor_entry,std::move(domain));
    if(out.constructor.loader_completed&&out.constructor.callback_pointer==0x1C0FC
        &&out.constructor.callback_continuation)
        out.callback=owner.ContinueActualCallback6664C(out.constructor.callback_continuation,
            *out.initialized.producer);
    out.snapshot=owner.ConstructionSnapshot();
    out.initialized.snapshot=out.initialized.producer->Snapshot();
    out.verified=out.constructor.reached_loader&&out.constructor.loader_completed
        &&out.constructor.boundary.source_pc==0x6664C&&out.constructor.callback_pointer==0x1C0FC
        &&out.callback.completed&&out.callback.producer_adopted&&out.callback.return_pc==0x66654
        &&out.callback.node==0x202560&&out.callback.return_residue==out.callback.node+0x30
        &&out.constructor.tables&&out.constructor.tables->shared_image()==out.snapshot.file.image
        &&out.snapshot.file.image->bytes.at(2)==0x44&&!owner.Snapshot().initialization_tail_applied;
    return out;
}
int SelfTest(const Options& options) {
    std::filesystem::create_directories(options.evidence);
    std::ostringstream report;
    int failures = 0, passed = 0;
    auto check = [&](bool ok, const char* name) { report << (ok ? "PASS " : "FAIL ") << name << '\n'; if (ok) ++passed; else ++failures; };
    const auto capturedClock=ReadCapturedClockSample(options.clockSample);
    check(capturedClock.returned,"captured real Windows sample validates FILETIME/cached RTC ABI conversion");
    const auto capturedRng=ReadCapturedRngSeed(options.rngSample);
    check(capturedRng.conversion.local_civil_epoch_seconds!=0,
        "captured UTC and actual timezone bind explicit source initializer output without fixed9h reuse");
    dokodemo::platform::WindowsRngInitializationSample liveInitialization;
    check(dokodemo::platform::CaptureWindowsRngInitialization(liveInitialization)&&liveInitialization.available,
        "real Windows local civil initialization backend is linked without seeding game globals");
    dokodemo::platform::WindowsClockCache liveClock;
    check(!liveClock.ReadCached().returned,"Windows clock cache stays unavailable before actual sampling");
    const bool refreshed=liveClock.RefreshFromWindows();
    const auto currentClock=liveClock.ReadCached();
    check(refreshed&&currentClock.returned&&liveClock.Snapshot().updates==1
        &&liveClock.ReadCached().cached_psp_tick_us==currentClock.cached_psp_tick_us,
        "actual Windows clock refresh and stable cached getter are linked in the application");
    check(Utf8(Utf8(std::string(u8"トロ / Игрок"))) == std::string(u8"トロ / Игрок"), "Japanese/Russian UTF-8 round trip");
    check(std::filesystem::is_directory(options.assets), "native asset directory exists");
    const auto calendar = dokodemo::recovered::CalendarRecord689B8({2004,11,16,0,0});
    check(calendar==dokodemo::recovered::CalendarRecord{0xD4,0x07,11,16,0,0,0}
        && dokodemo::recovered::CalendarScalar7C660(calendar)==0x00272220u,
        "original startup calendar record widths and minute scalar");
    check(dokodemo::recovered::CalendarScalar7C660(dokodemo::recovered::CalendarRecord689B8({2000,1,1,0,0}))==0u,
        "source calendar anchor 2000-01-01 yields zero without host clock");
    {
        using namespace dokodemo::recovered;
        auto owned=ConstructManagerPrefix();
        check(owned.verified,"owned manager/registry/command6 source prefix reaches explicit callback cut");
        uint32_t allocated=0;
        for(const auto& block:owned.snapshot.allocations) allocated+=block.bytes;
        check(allocated==1136&&allocated==owned.snapshot.allocated_bytes,
            "owned fixed allocations retain exact initialized source-layout storage");
        check(owned.ownerVerified&&!owned.ownerSnapshot.initialization_tail_applied
            &&owned.ownerSnapshot.blocks[0].initialized.at(0x2C)==0,
            "owned CKingKai payload preserves unknown pre-initialization budget");
        auto checkpoint=ConstructManagerPrefix(true);
        check(checkpoint.verified&&checkpoint.ownerVerified&&checkpoint.ownerCheckpoint
            &&checkpoint.callbackCut.arguments==std::array<uint32_t,4>{{0,0,0,0}},
            "explicit owner checkpoint joins callback23424 to actual unresolved kernel boundary");
        auto tiny=OwnedManagerQueue::Create(15,1);
        auto registry=tiny.manager->CreateRegistry(1);
        auto dropped=tiny.manager->RegisterCallback(registry.token,1,{0x23424,0},0x202);
        const auto drained=tiny.manager->Drain();
        check(dropped.accepted&&dropped.overflow_dropped&&!dropped.enqueued
            &&tiny.manager->Snapshot().registries.at(0).occupied==1&&!drained.boundary&&drained.cursor_reset,
            "source silent command overflow preserves occupied unlinked entry");
        auto failure=OwnedManagerQueue::Create(15,32,{0});
        check(!failure.manager&&failure.boundary.kind==ManagerStopKind::NativeAllocationFailure,
            "native owned allocation failure remains separate from original null-pointer effects");
        auto initialized=ConstructProducerPrefix({true,0});
        check(initialized.verified&&initialized.snapshot.allocator_transcript.size()==1
            &&initialized.snapshot.allocator_transcript[0].callee==0x63318
            &&initialized.snapshot.allocator_transcript[0].bytes==108,
            "owner initialization call joins real distinct108-byte producer backing under explicit allocator premise");
        check(initialized.prefix.snapshot.allocated_bytes==1136
            &&initialized.prefix.ownerSnapshot.blocks[0].bytes.size()==96
            &&initialized.prefix.ownerSnapshot.blocks[1].bytes.size()==164
            &&initialized.prefix.ownerSnapshot.blocks[0].initialized.at(0x2C)==0,
            "producer completion preserves manager and CKingKai identities and unknown caller suffix");
        auto unavailable=ConstructProducerPrefix({});
        check(!unavailable.verified&&unavailable.initialization.boundary.kind==ProducerStop::MissingInput
            &&unavailable.producer&&unavailable.snapshot.live_blocks.empty(),
            "linked producer remains unavailable without explicit mapped source input");
        auto suffix=ConstructOwnerSuffixPrefix({true,0},ConditionalLoadCheckpoint(0));
        check(suffix.verified&&suffix.suffix.directory=="KSNEKO"&&suffix.suffix.filename=="neko.ksc",
            "actual owned producer joins23194 source directory and filename prefix before formatter cut");
        auto missingLoad=ConstructOwnerSuffixPrefix({true,0},{});
        check(!missingLoad.verified&&missingLoad.suffix.boundary.source_pc==0x23194,
            "missing CLoadMan checkpoint leaves real initialization suffix unavailable");
    }
    {
        using namespace dokodemo::recovered;
        Service47TestCells cells;
        const Service47Address dst{Service47Address::Table::Stack,0};
        Service47Fixture f;
        f.enabled=f.control_cell_mapped=f.guard_cell_mapped=f.state_cell_mapped=f.memory_disjoint=true;
        cells.slots[0].word=0x2000002A;
        auto state=Service47InitialState(f);
        Service47Host1C958(cells,dst,f,state);
        check(state.calls==1 && state.guard==0 && state.state==0xFFFF && !state.format_called
            && cells.slots[0].word==0x2000002A,"mapped NULL-control service47 cleanup preserves destination");
        f.control_present=f.control_word_mapped=f.source_present=f.source_header_mapped=true;
        f.status_cell_mapped=f.format_window_mapped=true; f.source_magic=0x0011076A; f.guard=1;
        f.format_bytes={1,0,0,0x90,0x20,0x22,0x27,0x30};
        cells.slots[0].word=0x90000001; cells.slots[1].word=0x20000001;
        state=Service47InitialState(f); Service47Host1C958(cells,dst,f,state);
        check(state.calls==1 && state.format_return==0 && state.cursor_bytes==8 && state.decoded_words==2
            && state.state==0xFFFF && state.guard==0 && cells.slots[1].word==0x30272220,
            "mapped array/scalar cursor retains exact tagged word and cleanup order");
        f.format_bytes={0x2A,0,0,0x20}; cells.slots[0].word=0x90000001;
        state=Service47InitialState(f); Service47Host1C958(cells,dst,f,state);
        check(state.format_return==-1 && state.calls==1 && state.state==0xFFFF && cells.slots[0].word==0x90000001,
            "original format mismatch return is discarded while host cleanup completes");
        f.format_bytes={1,0,0,0x60}; cells.slots[0].word=0x20000001;
        state=Service47InitialState(f); bool barrier=false;
        try { Service47Host1C958(cells,dst,f,state); } catch(const std::runtime_error&) { barrier=true; }
        check(barrier && state.calls==0 && cells.slots[0].word==0x20000001,
            "unproved owned format input remains a native barrier");
    }
    auto files = FindImages(options.assets);
    check(!files.empty(), "original decoded BMP resource exists");
    size_t decoded = 0;
    for (const auto& path : files) {
        Image image; std::wstring error;
        if (LoadBmp(path, image, error)) ++decoded;
        else { report << "FAIL BMP " << Utf8(path.wstring()) << ": " << Utf8(error) << '\n'; ++failures; }
    }
    report << "Decoded original resources: " << decoded << '\n';
    GameFont gameFont; std::wstring fontError; Image fontImage;
    check(gameFont.Load(options.assets / L"graphics/font",fontError), "original XFT loaded with recovered BOOT indexing");
    check(gameFont.RasterizePlainCp932(std::string("\x83\x67\x83\x8d"),fontImage,fontError), "original Toro glyphs rendered natively");
    check(!gameFont.RasterizePlainCp932(std::string("\x83"),fontImage,fontError), "truncated CP932 font input rejected");
    check(gameFont.RasterizePlainCp932(std::string(226,'\n'),fontImage,fontError) && fontImage.height==4084
        && !gameFont.RasterizePlainCp932(std::string(227,'\n'),fontImage,fontError)
        && !gameFont.RasterizePlainCp932(std::string(1000,'\n'),fontImage,fontError), "newline-only XFT input obeys the dimension budget");
    // Reject truncated/malicious files rather than relying on LoadImage's permissive behavior.
    auto bad = options.evidence / L"truncated.bmp";
    { std::ofstream out(bad, std::ios::binary); out << "BM"; }
    Image image; std::wstring error;
    check(!LoadBmp(bad, image, error), "truncated BMP rejected");
    std::filesystem::remove(bad);
    dokodemo::save::FoundationState state;
    state.player_name_utf8 = u8"Игрок / プレイヤー";
    state.character_key_utf8 = "NEKO";
    state.last_observed_utc_ms = dokodemo::save::ReadSystemClock().utc_ms;
    auto saved = options.evidence / L"self-test-foundation.dns";
    auto saveResult = dokodemo::save::SaveAtomic(saved, state);
    dokodemo::save::FoundationState restored;
    auto loadResult = dokodemo::save::Load(saved, restored);
    check(bool(saveResult) && bool(loadResult) && restored.player_name_utf8 == state.player_name_utf8
          && restored.last_observed_utc_ms == state.last_observed_utc_ms, "integrated atomic native save/load with Unicode");
    std::filesystem::remove(saved);
    {
        // Explicit synthetic byte fixtures exercise linked recovered helpers;
        // they do not supply values to the actual title/menu or host objects.
        using namespace dokodemo::recovered;
        uint32_t result=123;
        bool repeat=menu_repeat_94f90(0,10,0,result)==MenuStatus::ok && result==1;
        repeat=repeat && menu_repeat_94f90(8,10,0,result)==MenuStatus::ok && result==0;
        result=123;
        repeat=repeat && menu_repeat_94f90(10,10,0,result)==MenuStatus::division_zero_barrier && result==123;
        repeat=repeat && menu_repeat_94f90(12,10,2,result)==MenuStatus::ok && result==2;
        check(repeat,"recovered repeat predicate: early returns and reached division barrier");
        std::array<uint8_t,2> profile{{0x54,0x80}};
        bool bits=menu_bitset_4184({profile.data(),profile.size()},1,0xfffffff8u,1)==MenuStatus::ok && profile[0]==0x55;
        const auto unchanged=profile;
        bits=bits && menu_bitset_4184({profile.data(),profile.size()},0,0xfffffff8u,1)!=MenuStatus::ok && profile==unchanged;
        check(bits,"recovered bit setter: mapped negative index and atomic unmapped barrier");
        std::array<uint8_t,0x2c> host{};
        host[0x20]=2; host[0x24]=4;
        bool commit=menu_commit_17b84_requests3_4({host.data(),host.size()})==MenuStatus::ok
            && host[0x1c]==2 && host[0x20]==4 && host[0x28]==6;
        check(commit,"recovered bounded phase commit writes explicit fixture fields");
        bool alias=menu_request_19aa8({host.data(),host.size()},0x12345678,0xab,host.data()+0x24)==MenuStatus::ok
            && host[0x24]==0x78 && host[0x25]==0x56 && host[0x28]==2;
        check(alias,"recovered request/event alias obeys final original write order");
    }
    size_t scripts = 0, commands = 0, dictionaryRecords = 0;
    for (const char* name : {"INU", "KAERU", "NEKO", "ROBO", "USAGI"}) {
        dokodemo::script::Program program; dokodemo::script::Dictionary dictionary; dokodemo::script::Diagnostic diagnostic;
        auto base = options.assets / L"scripts/original";
        bool loaded = program.load_original(base / (std::string(name) + ".KSC"), diagnostic);
        check(loaded, (std::string("original KSC verified: ") + name).c_str());
        if (loaded) {
            ++scripts; commands += program.commands().size();
            for(uint8_t hostByte : {uint8_t(0),uint8_t(1)}) {
                auto trace=dokodemo::startup::run(program,hostByte,10000);
                check(VerifiedStartupPrefix(trace,name),
                    (std::string("original startup prefix / host context ") + name + " / " + std::to_string(hostByte)).c_str());
                // An explicit mapped/disjoint test domain, not observed host availability.
                dokodemo::startup::StartupProbe projected(program,hostByte,512,{true,true,true,true,true,0});
                check(VerifiedProjectedStartup(projected.run(10000),name),
                    (std::string("conditional host ABI projection / ") + name + " / " + std::to_string(hostByte)).c_str());
                dokodemo::startup::StartupProbe service(program,hostByte,512,{true,true,true,true,true,0},ConditionalService47Fixture());
                // Retain the exact accepted service47 prefix even as builtin97 advances.
                check(VerifiedService47Startup(service.run(6192),name,program),
                    (std::string("conditional mapped service47 and ctx28 startup / ")+name+" / "+std::to_string(hostByte)).c_str());
                check(VerifiedBulkStartup(RunToBoundary(service),name,hostByte),
                    (std::string("joined builtin97 and resumed original branch / ")+name+" / "+std::to_string(hostByte)).c_str());
                check(VerifiedStorageStartup(StorageStartup(program,hostByte),name,hostByte,program),
                    (std::string("joined owned strings or NONNULL record lifecycle / ")+name+" / "+std::to_string(hostByte)).c_str());
                dokodemo::recovered::ClockDependency dependency;
                dependency.callback_slot_mapped=dependency.installed_getter_returns_cache=true;
                dependency.provider=[capturedClock]() {return capturedClock;};
                auto advanced=ClockStorageProbe(program,hostByte,dependency);
                check(VerifiedClockStorageStartup(RunToBoundary(*advanced),name,hostByte,program),
                    (std::string("captured source clock and service39 ownership continuation / ")+name+" / "+std::to_string(hostByte)).c_str());
                auto current=CurrentRngProbe(program,hostByte,std::move(dependency),capturedRng.conversion.local_civil_epoch_seconds);
                check(ReachedCurrentRngOwnershipBoundary(RunToBoundary(*current),hostByte),
                    (std::string("conditional array98 and persistent RNG reach readonly table ownership boundary / ")+name+" / "+std::to_string(hostByte)).c_str());
            }
        }
        bool dicLoaded = dictionary.load_original(base / (std::string(name) + ".DIC"), diagnostic);
        check(dicLoaded, (std::string("original DIC verified: ") + name).c_str());
        if (dicLoaded) dictionaryRecords += dictionary.records().size();
    }
    report << "Original script identities: " << scripts << "; commands/metadata: " << commands << "; DIC records: " << dictionaryRecords << '\n';
    {
        dokodemo::script::Program program; dokodemo::script::Diagnostic diagnostic;
        bool loaded=program.load_original(options.assets / L"scripts/original/NEKO.KSC",diagnostic);
        if (loaded) {
            using namespace dokodemo::recovered;
            auto suffix=ConstructOwnerSuffixPrefix({true,0},ConditionalLoadCheckpoint(0));
            auto& owner=*suffix.initialized.prefix.callbackOwner;
            auto prepared=owner.PrepareNonzero2327CActivationAfterUnverifiedLoadAndConstructor(
                ConditionalPreparedActivation(program,0));
            const auto activated=owner.ContinuePreparedNonzero2327CActivation(prepared.continuation);
            const auto activationOwnerState=owner.Snapshot();
            const auto backing=owner.SuffixSnapshot();
            check(activated.completed&&activated.source_return_known&&activated.source_return_value==1
                &&backing.completed&&backing.blocks.size()==5&&backing.vm_publication_10e448.value==0x610000
                &&activationOwnerState.initialization_tail_applied&&activationOwnerState.blocks.at(0).bytes.at(0x2C)==0xD0
                &&activationOwnerState.blocks.at(0).bytes.at(0x2D)==7,
                "explicit prepared nonzero VM activation performs actual source tail and owner budget stores");
            check(!owner.ContinuePreparedNonzero2327CActivation(prepared.continuation).completed,
                "source activation token is owner-bound and one-shot without promoting prior load or constructor");
            dokodemo::script::Vm vm(program);
            auto event=vm.start_at(0xC373-28,{dokodemo::script::Value::integer(10),dokodemo::script::Value::integer(14)});
            if(event.status==dokodemo::script::Status::Running) event=vm.run(1000);
            check(event.status==dokodemo::script::Status::Message && event.opcode==0x48 && event.pc==0xC359 && event.display_state==6 && event.display_count==1,
                "isolated original KSC function executes message opcode48 (fixture, no gameplay route claim)");
            check(gameFont.RasterizePlainCp932(event.message_cp932,fontImage,fontError), "executed original KSC message rendered using original XFT glyphs");
        } else check(false,"isolated original KSC function loaded");
    }
    try {
        auto modelPath = options.assets / L"models/source/NEKO.BPM/NEKO/TORO.DVS";
        std::ifstream in(modelPath,std::ios::binary);
        dokodemo::model::Bytes data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        auto model = dokodemo::model::parseDvs(data);
        size_t vertices=0,triangles=0;
        for (const auto& node:model.nodes) if(node.hasGeometry) {
            vertices+=node.geometry.positions.size();
            if (!node.geometry.primitiveStreams.empty()) triangles+=dokodemo::model::triangles(node.geometry).size();
        }
        check(vertices==830 && triangles==1442, "original Toro DVS geometry decoded in native C++ (local space)");
    } catch (const std::exception& error) { check(false,"original Toro DVS geometry decoded in native C++"); report << error.what() << '\n'; }
    std::shared_ptr<const dokodemo::audio::PcmClip> clip; std::string audioError;
    bool audioLoaded = dokodemo::audio::LoadPcm16Wave(options.assets / L"audio/pcm/BGM_TITLE.wav",clip,audioError);
    check(audioLoaded && clip->FrameCount()==8921024 && clip->sample_rate==44100 && clip->channels==2, "original title music PCM verified in native C++");
    report << "Tests passed: " << passed << "; failures: " << failures << '\n';
    auto text = report.str(); Output(text);
    std::ofstream(options.evidence / L"self-test.txt", std::ios::binary) << text;
    return failures ? 1 : 0;
}
void WriteModuleTokenValue(std::ostream& out, const dokodemo::recovered::NativeModuleToken& value) {
    out << "{\"owner\":" << value.owner << ",\"generation\":" << value.generation
        << ",\"serial\":" << value.serial << ",\"uid\":" << value.uid << '}';
}
std::string StartupHexBytes(const uint8_t* bytes, size_t count) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
        result += hex[bytes[i] >> 4]; result += hex[bytes[i] & 15];
    }
    return result;
}
void WriteSascoreValue(std::ostream& out,
    const dokodemo::recovered::SascoreInitializationSnapshot& s) {
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    out << "{\"present\":" << boolean(s.present);
    if (!s.present) { out << '}'; return; }
    const auto number = [&](const char* name, auto value) { out << ',' << JsonString(name) << ':' << value; };
    const auto flag = [&](const char* name, bool value) { out << ',' << JsonString(name) << ':' << boolean(value); };
    const auto nullable = [&](const char* name, bool present, auto value) {
        out << ',' << JsonString(name) << ':';
        if (present) out << value; else out << "null";
    };
    const auto tuple = [&](const char* name, const auto& values) {
        out << ',' << JsonString(name) << ":[";
        bool first = true;
        for (const auto value : values) { if (!first) out << ','; first = false; out << value; }
        out << ']';
    };
    flag("entry_entered",s.entry_entered); flag("entry_returned",s.entry_returned);
    nullable("entry_result",s.entry_returned,s.entry_result); flag("dependency_blocked",s.dependency_blocked);
    flag("host_abort_required",s.host_abort_required); flag("host_abort_entered",s.host_abort_entered);
    flag("host_aborted",s.host_aborted); flag("stop_domain_ready",s.stop_domain_ready);
    flag("source_stop_entered",s.source_stop_entered); flag("source_stop_returned",s.source_stop_returned);
    number("stop_progress",s.stop_progress); number("owner",s.owner); number("generation",s.generation);
    number("serial",s.serial); number("native_state_address",s.native_state_address);
    number("source_segment",s.source_segment); number("source_segment_va",s.source_segment_va);
    number("private_bytes",s.private_bytes); number("initial_source_known_bytes",s.initial_source_known_bytes);
    number("known_bytes",s.known_bytes); number("writes_count",s.writes_count);
    out << ",\"bytes_hex\":" << JsonString(StartupHexBytes(s.bytes.data(),s.bytes.size()))
        << ",\"known_hex\":" << JsonString(StartupHexBytes(s.known.data(),s.known.size()))
        << ",\"bytes_meaning_requires_known_one\":true,\"writes\":[";
    for (size_t i=0;i<(std::min)(size_t(s.writes_count),s.writes.size());++i) {
        if (i) out << ',';
        const auto& w=s.writes[i];
        out << "{\"ordinal\":" << w.ordinal << ",\"source_pc\":" << w.source_pc
            << ",\"offset\":" << w.offset << ",\"width\":4,\"value\":" << w.value
            << ",\"value_origin\":" << JsonString(w.value_origin ? w.value_origin : "") << '}';
    }
    out << ']';
    number("frontier_source_pc",s.frontier_source_pc); number("frontier_stub_va",s.frontier_stub_va);
    number("frontier_nid",s.frontier_nid); number("frontier_import_index",s.frontier_import_index);
    number("calloc_count",s.calloc_count); number("calloc_size",s.calloc_size);
    out << ",\"semaphore_name\":" << JsonString(s.semaphore_name ? s.semaphore_name : "");
    number("semaphore_attributes",s.semaphore_attributes); number("semaphore_initial",s.semaphore_initial);
    number("semaphore_maximum",s.semaphore_maximum); flag("semaphore_options_null",s.semaphore_options_null);
    number("calloc_calls_entered",s.calloc_calls_entered); number("calloc_calls_returned",s.calloc_calls_returned);
    number("create_calls_entered",s.create_calls_entered); number("create_calls_returned",s.create_calls_returned);
    number("free_calls_entered",s.free_calls_entered); number("free_calls_returned",s.free_calls_returned);
    number("delete_calls_entered",s.delete_calls_entered); number("delete_calls_returned",s.delete_calls_returned);
    out << ",\"resources\":{\"policy_abi\":" << JsonString(s.resources.policy_abi ? s.resources.policy_abi : "")
        << ",\"original_ME_equivalence_known\":false,\"key\":{\"module\":[";
    for(size_t i=0;i<s.resources.key.module.size();++i) { if(i) out << ',';out << s.resources.key.module[i]; }
    out << ']';
    tuple("input",s.resources.key.input); tuple("state",s.resources.key.state); out << '}';
    const auto& r=s.resources;
    flag("issued",r.issued); flag("retired",r.retired); flag("input_pinned",r.input_pinned);
    flag("allocation_entered",r.allocation_entered); flag("allocation_returned",r.allocation_returned);
    nullable("allocation_backend_code",r.allocation_returned,r.allocation_backend_code);
    nullable("allocation_error",r.allocation_returned,r.allocation_error);
    flag("memory_live",r.memory_live); flag("memory_map_validated",r.memory_map_validated);
    flag("calloc_source_returned",r.calloc_source_returned);
    nullable("calloc_source_value",r.calloc_source_returned,r.source_coordinate);
    number("allocation_owner",r.allocation_owner); number("allocation_generation",r.allocation_generation);
    number("allocation_serial",r.allocation_serial); number("memory_host_address",r.memory_host_address);
    number("committed_bytes",r.committed_bytes); number("requested_bytes",r.requested_bytes);
    number("source_coordinate",r.source_coordinate); number("page_bytes",r.page_bytes); number("zeroed_bytes",r.zeroed_bytes);
    flag("semaphore_create_entered",r.semaphore_create_entered); flag("semaphore_create_returned",r.semaphore_create_returned);
    flag("semaphore_source_returned",r.semaphore_source_returned); flag("semaphore_live",r.semaphore_live);
    nullable("semaphore_uid",r.semaphore_source_returned,r.semaphore_uid);
    nullable("semaphore_handle",r.semaphore_create_returned,r.semaphore_handle);
    nullable("semaphore_create_error",r.semaphore_create_returned,r.semaphore_create_error);
    number("allocations",r.allocations); number("frees",r.frees);
    number("semaphore_creates",r.semaphore_creates); number("semaphore_closes",r.semaphore_closes);
    number("free_attempts",r.free_attempts); number("free_returns",r.free_returns);
    number("delete_attempts",r.delete_attempts); number("delete_returns",r.delete_returns);
    nullable("free_backend_code",r.free_returns!=0,r.free_backend_code);
    nullable("free_error",r.free_returns!=0,r.free_error); nullable("delete_error",r.delete_returns!=0,r.delete_error);
    nullable("delete_native_succeeded",r.delete_returns!=0,boolean(r.delete_native_succeeded)); flag("memory_freed",r.memory_freed);
    flag("semaphore_closed",r.semaphore_closed); flag("free_source_returned",r.free_source_returned);
    flag("delete_source_returned",r.delete_source_returned);
    nullable("delete_source_result",r.delete_source_returned,r.delete_source_result);
    flag("retirement_entered",r.retirement_entered); flag("retirement_returned",r.retirement_returned);
    out << "}}";
}

void WriteCheckNeedMemValue(std::ostream& out,
    const dokodemo::recovered::NativeCheckNeedMemSnapshot& value) {
    const auto boolean = [](bool item) { return item ? "true" : "false"; };
    out << "{\"present\":" << boolean(value.present);
    if (!value.present) { out << '}'; return; }
    out << ",\"partial_callable\":" << boolean(value.partial_callable) << ",\"provider_module\":";
    WriteModuleTokenValue(out, value.provider_module);
    out << ",\"consumer_module\":"; WriteModuleTokenValue(out, value.consumer_module);
    out << ",\"provider\":{\"owner\":" << value.provider.owner
        << ",\"generation\":" << value.provider.generation << ",\"serial\":" << value.provider.serial << '}'
        << ",\"provider_input_owner\":" << value.provider_input_owner
        << ",\"provider_input_generation\":" << value.provider_input_generation
        << ",\"provider_input_serial\":" << value.provider_input_serial
        << ",\"consumer_input_owner\":" << value.consumer_input_owner
        << ",\"consumer_input_generation\":" << value.consumer_input_generation
        << ",\"consumer_input_serial\":" << value.consumer_input_serial
        << ",\"buffer_native_address\":" << value.buffer_native_address
        << ",\"caller_source_pc\":" << value.caller_source_pc
        << ",\"buffer_record_offset\":" << value.buffer_record_offset << ",\"buffer_bytes\":" << value.buffer_bytes
        << ",\"window_known_before\":" << value.window_known_before << ",\"window_known_after\":" << value.window_known_after;
    const auto& body = value.body;
    out << ",\"body\":{\"entered\":" << boolean(body.entered)
        << ",\"host_policy_applied\":" << boolean(body.host_policy_applied)
        << ",\"source_null\":" << boolean(body.source_null)
        << ",\"dependency_blocked\":" << boolean(body.dependency_blocked)
        << ",\"dependency_dispatched\":" << boolean(body.dependency_dispatched)
        << ",\"source_return\":" << boolean(body.source_return) << ",\"source_result\":";
    if (body.source_return) out << body.source_result; else out << "null";
    out << ",\"codec\":" << body.codec << ",\"buffer_reads\":" << body.buffer_reads << ",\"writes\":" << body.writes
        << ",\"entry_source_pc\":" << body.entry_source_pc << ",\"write_source_pc\":" << body.write_source_pc
        << ",\"frontier_source_pc\":" << body.frontier_source_pc << ",\"frontier_delay_pc\":" << body.frontier_delay_pc
        << ",\"frontier_stub_va\":" << body.frontier_stub_va << ",\"frontier_nid\":" << body.frontier_nid << "}}";
}
void WritePreparedEnvelopeValue(std::ostream& out,
    const dokodemo::recovered::NativePreparedEnvelopeSnapshot& value) {
    using dokodemo::recovered::NativeCodecWorkspaceState;
    const auto boolean = [](bool item) { return item ? "true" : "false"; };
    const auto tuple = [&out](const auto& items) {
        out << '[';
        for (size_t i = 0; i < items.size(); ++i) { if (i) out << ','; out << items[i]; }
        out << ']';
    };
    const auto stateName = [](NativeCodecWorkspaceState state) {
        switch (state) {
        case NativeCodecWorkspaceState::Unissued: return "Unissued";
        case NativeCodecWorkspaceState::Preparing: return "Preparing";
        case NativeCodecWorkspaceState::Prepared: return "Prepared";
        case NativeCodecWorkspaceState::AllocatedFormatPending: return "AllocatedFormatPending";
        case NativeCodecWorkspaceState::WorkspaceLive: return "WorkspaceLive";
        case NativeCodecWorkspaceState::Failed: return "Failed";
        case NativeCodecWorkspaceState::DestroyedPendingFree: return "DestroyedPendingFree";
        case NativeCodecWorkspaceState::Retired: return "Retired";
        }
        return "Unknown";
    };
    out << "{\"present\":" << boolean(value.present);
    if (!value.present) { out << '}'; return; }
    out << ",\"native1001_blocked\":" << boolean(value.native1001_blocked)
        << ",\"source_init_returned\":" << boolean(value.source_init_returned)
        << ",\"source_decode_returned\":" << boolean(value.source_decode_returned)
        << ",\"source_start_returned\":" << boolean(value.source_start_returned)
        << ",\"source_stop_entered\":" << boolean(value.source_stop_entered)
        << ",\"source_stop_returned\":" << boolean(value.source_stop_returned)
        << ",\"source_stop_pending\":" << boolean(value.source_stop_pending)
        << ",\"source_stop_next_record\":" << value.source_stop_next_record
        << ",\"source_stop_records_completed\":" << value.source_stop_records_completed
        << ",\"source_release_calls_entered\":" << value.source_release_calls_entered
        << ",\"source_release_calls_returned\":" << value.source_release_calls_returned << ",\"records\":[";
    for (size_t i = 0; i < value.records.size(); ++i) {
        const auto& item = value.records[i];
        if (i) out << ',';
        out << "{\"key\":{\"provider_module\":"; tuple(item.key.provider_module);
        out << ",\"consumer_module\":"; tuple(item.key.consumer_module);
        out << ",\"provider\":"; tuple(item.key.provider);
        out << ",\"provider_input\":"; tuple(item.key.provider_input);
        out << ",\"consumer_input\":"; tuple(item.key.consumer_input);
        out << ",\"state\":"; tuple(item.key.state);
        out << ",\"record_index\":" << item.key.record_index << '}'
            << ",\"state\":" << JsonString(stateName(item.state))
            << ",\"policy_abi\":" << JsonString(item.policy_abi ? item.policy_abi : "")
            << ",\"original_equivalence_known\":" << boolean(item.original_equivalence_known)
            << ",\"record_index\":" << item.record_index << ",\"codec\":" << item.codec
            << ",\"context_allocated\":" << boolean(item.context_allocated)
            << ",\"format_known\":" << boolean(item.format_known)
            << ",\"format_pending\":" << boolean(item.format_pending)
            << ",\"decoder_opened\":" << boolean(item.decoder_opened)
            << ",\"decoder_ready\":" << boolean(item.decoder_ready)
            << ",\"decode_capability\":" << boolean(item.decode_capability)
            << ",\"format_b1\":";
        if (item.format_known) out << item.format_b1; else out << "null";
        out << ",\"format_b2\":"; if (item.format_known) out << item.format_b2; else out << "null";
        out << ",\"channels\":"; if (item.format_known) out << item.channels; else out << "null";
        out << ",\"max_input_bytes\":"; if (item.format_known) out << item.max_input_bytes; else out << "null";
        out << ",\"native_decoder_find_entered\":" << boolean(item.native_decoder_find_entered)
            << ",\"native_decoder_find_returned\":" << boolean(item.native_decoder_find_returned)
            << ",\"native_decoder_found\":" << boolean(item.native_decoder_found)
            << ",\"native_decoder_id\":";
        if (item.native_decoder_find_returned && item.native_decoder_found) out << item.native_decoder_id; else out << "null";
        out << ",\"api_codec_version\":" << item.api_codec_version << ",\"api_util_version\":" << item.api_util_version
            << ",\"native_prepare_entered\":" << boolean(item.native_prepare_entered)
            << ",\"native_prepare_returned\":" << boolean(item.native_prepare_returned)
            << ",\"native_alloc_entered\":" << boolean(item.native_alloc_entered)
            << ",\"native_alloc_returned\":" << boolean(item.native_alloc_returned)
            << ",\"native_open_entered\":" << boolean(item.native_open_entered)
            << ",\"native_open_returned\":" << boolean(item.native_open_returned)
            << ",\"native_api_result\":";
        if (item.native_open_returned) out << item.native_api_result; else out << "null";
        out << ",\"native_error_class\":" << item.native_error_class
            << ",\"native_policy_return\":" << boolean(item.native_policy_return) << ",\"native_policy_result\":";
        if (item.native_policy_return) out << item.native_policy_result; else out << "null";
        out << ",\"envelope_bytes\":" << item.envelope_bytes << ",\"envelope_alignment\":" << item.envelope_alignment
            << ",\"provisional_live\":" << boolean(item.provisional_live)
            << ",\"allocation_attempted\":" << boolean(item.allocation_attempted)
            << ",\"backend_allocate_entered\":" << boolean(item.backend_allocate_entered)
            << ",\"backend_allocate_returned\":" << boolean(item.backend_allocate_returned)
            << ",\"backend_allocate_code\":";
        if (item.backend_allocate_returned) out << item.backend_allocate_code; else out << "null";
        out
            << ",\"allocation_returned\":" << boolean(item.allocation_returned)
            << ",\"allocation_succeeded\":" << boolean(item.allocation_succeeded)
            << ",\"allocation_owner\":" << item.allocation_owner
            << ",\"allocation_generation\":" << item.allocation_generation
            << ",\"allocation_serial\":" << item.allocation_serial
            << ",\"requested_bytes\":" << item.requested_bytes << ",\"committed_bytes\":" << item.committed_bytes
            << ",\"raw_coordinate\":" << item.raw_coordinate << ",\"aligned_coordinate\":" << item.aligned_coordinate
            << ",\"host_address\":" << item.host_address
            << ",\"placement_constructed\":" << boolean(item.placement_constructed)
            << ",\"context_transferred\":" << boolean(item.context_transferred)
            << ",\"context_destroyed\":" << boolean(item.context_destroyed)
            << ",\"storage_freed\":" << boolean(item.storage_freed)
            << ",\"context_allocations\":" << item.context_allocations << ",\"context_frees\":" << item.context_frees
            << ",\"win32_error\":" << item.win32_error
            << ",\"query_source_return\":" << boolean(item.query_source_return) << ",\"query_source_result\":";
        if (item.query_source_return) out << item.query_source_result; else out << "null";
        out << ",\"get_edram_source_return\":" << boolean(item.get_edram_source_return) << ",\"get_edram_source_result\":";
        if (item.get_edram_source_return) out << item.get_edram_source_result; else out << "null";
        out << ",\"release_source_entered\":" << boolean(item.release_source_entered)
            << ",\"release_source_return\":" << boolean(item.release_source_return) << ",\"release_source_result\":";
        if (item.release_source_return) out << item.release_source_result; else out << "null";
        out << ",\"original_low29_guard_passed\":";
        if (item.release_source_entered) out << boolean(item.original_low29_guard_passed); else out << "null";
        out << ",\"windows_release_capability_applied\":" << boolean(item.windows_release_capability_applied)
            << ",\"release_map_validated\":" << boolean(item.release_map_validated)
            << ",\"release_native_attempts\":" << item.release_native_attempts
            << ",\"release_native_returns\":" << item.release_native_returns << ",\"release_native_code\":";
        if (item.release_native_returns) out << item.release_native_code; else out << "null";
        out << ",\"release_native_completed\":" << boolean(item.release_native_completed);
        out << ",\"host_retirement_entered\":" << boolean(item.host_retirement_entered)
            << ",\"host_retirement_returned\":" << boolean(item.host_retirement_returned);
        out << '}';
    }
    out << "]}";
}
void WriteModuleInitializationValue(std::ostream& out,
    const dokodemo::recovered::Atrac3plusInitializationSnapshot& value) {
    const auto boolean = [](bool item) { return item ? "true" : "false"; };
    out << "{\"present\":" << boolean(value.present);
    if (!value.present) { out << '}'; return; }
    out << ",\"prefix_entered\":" << boolean(value.prefix_entered)
        << ",\"dependency_blocked\":" << boolean(value.dependency_blocked)
        << ",\"aborted\":" << boolean(value.aborted)
        << ",\"prepared_envelope_policy\":" << boolean(value.prepared_envelope_policy)
        << ",\"owner\":" << value.owner << ",\"generation\":" << value.generation
        << ",\"serial\":" << value.serial << ",\"native_state_address\":" << value.native_state_address
        << ",\"source_segment\":" << value.source_segment << ",\"source_segment_offset\":" << value.source_segment_offset
        << ",\"record_count\":" << value.record_count << ",\"record_stride\":" << value.record_stride
        << ",\"record_bytes\":" << value.record_bytes << ",\"initial_source_known_bytes\":" << value.initial_source_known_bytes
        << ",\"known_bytes\":" << value.known_bytes << ",\"writes_count\":" << value.writes_count
        << ",\"bytes_hex\":" << JsonString(StartupHexBytes(value.bytes.data(), value.bytes.size()))
        << ",\"known_hex\":" << JsonString(StartupHexBytes(value.known.data(), value.known.size()))
        << ",\"frontier_source_pc\":" << value.frontier_source_pc
        << ",\"frontier_delay_pc\":" << value.frontier_delay_pc << ",\"frontier_stub_va\":" << value.frontier_stub_va
        << ",\"frontier_import_index\":" << value.frontier_import_index << ",\"frontier_nid\":" << value.frontier_nid
        << ",\"arg0_record_index\":" << value.arg0_record_index << ",\"arg0_record_offset\":" << value.arg0_record_offset
        << ",\"arg1\":" << value.arg1 << ",\"frontier_arguments_prepared\":" << boolean(value.frontier_arguments_prepared)
        << ",\"helper_calls_entered\":" << value.helper_calls_entered << ",\"loop_iterations_completed\":" << value.loop_iterations_completed
        << ",\"external_services_dispatched\":" << value.external_services_dispatched
        << ",\"external_resources_acquired\":" << value.external_resources_acquired << ",\"writes\":[";
    bool first = true;
    for (size_t i = 0; i < value.writes_count && i < value.writes.size(); ++i) {
        const auto& item = value.writes[i];
        if (!first) out << ',';
        first = false;
        out << "{\"ordinal\":" << item.ordinal << ",\"source_pc\":" << item.source_pc
            << ",\"source_module\":" << JsonString(item.source_module == dokodemo::recovered::Atrac3plusWriteSource::Audiocodec ? "Audiocodec"
                : item.source_module == dokodemo::recovered::Atrac3plusWriteSource::NativePlatform ? "NativePlatform" : "Atrac3plus")
            << ",\"offset\":" << item.offset << ",\"width\":" << item.width << ",\"value\":" << item.value << '}';
    }
    out << "]}";
}
void WriteIfhandleValue(std::ostream& out,
    const dokodemo::recovered::IfhandlePrivateDataSnapshot& s) {
    out << "{\"present\":" << (s.present ? "true" : "false");
    if (!s.present) { out << '}'; return; }
    out << ",\"owner\":" << s.owner << ",\"generation\":" << s.generation
        << ",\"serial\":" << s.serial << ",\"native_state_address\":" << s.native_state_address
        << ",\"source_segment\":" << s.source_segment << ",\"source_segment_va\":" << s.source_segment_va
        << ",\"private_bytes\":" << s.private_bytes << ",\"initial_source_known_bytes\":" << s.initial_source_known_bytes
        << ",\"known_bytes\":" << s.known_bytes << ",\"writes_count\":" << s.writes_count
        << ",\"bytes_hex\":" << JsonString(StartupHexBytes(s.bytes.data(), s.bytes.size()))
        << ",\"known_hex\":" << JsonString(StartupHexBytes(s.known.data(), s.known.size())) << '}';
}

void WritePspnetValue(std::ostream& out,
    const dokodemo::recovered::PspnetPrivateDataSnapshot& s) {
    out << "{\"present\":" << (s.present ? "true" : "false");
    if (!s.present) { out << '}'; return; }
    out << ",\"owner\":" << s.owner << ",\"generation\":" << s.generation
        << ",\"serial\":" << s.serial << ",\"native_state_address\":" << s.native_state_address
        << ",\"source_segment\":" << s.source_segment << ",\"source_segment_va\":" << s.source_segment_va
        << ",\"private_bytes\":" << s.private_bytes << ",\"initial_source_known_bytes\":" << s.initial_source_known_bytes
        << ",\"known_bytes\":" << s.known_bytes << ",\"writes_count\":" << s.writes_count
        << ",\"bytes_hex\":" << JsonString(StartupHexBytes(s.bytes.data(), s.bytes.size()))
        << ",\"known_hex\":" << JsonString(StartupHexBytes(s.known.data(), s.known.size())) << '}';
}
void WriteAdhocctlValue(std::ostream& out,
    const dokodemo::recovered::AdhocctlPrivateDataSnapshot& s) {
    out << "{\"present\":" << (s.present ? "true" : "false");
    if (!s.present) { out << '}'; return; }
    out << ",\"owner\":" << s.owner << ",\"generation\":" << s.generation
        << ",\"serial\":" << s.serial << ",\"native_state_address\":" << s.native_state_address
        << ",\"source_segment\":" << s.source_segment << ",\"source_segment_va\":" << s.source_segment_va
        << ",\"source_file_offset\":" << s.source_file_offset << ",\"source_file_bytes\":" << s.source_file_bytes
        << ",\"private_bytes\":" << s.private_bytes << ",\"initial_source_known_bytes\":" << s.initial_source_known_bytes
        << ",\"known_bytes\":" << s.known_bytes << ",\"writes_count\":" << s.writes_count
        << ",\"initial_known_origin\":\"AdmittedUnrelocatedFileBytesCopiedByNativeInitializer\""
        << ",\"bytes_meaning_requires_known_one\":true"
        << ",\"bytes_hex\":" << JsonString(StartupHexBytes(s.bytes.data(), s.bytes.size()))
        << ",\"known_hex\":" << JsonString(StartupHexBytes(s.known.data(), s.known.size())) << '}';
}

void WriteNextGameCallValue(std::ostream& out, const dokodemo::StartupObservation& o) {
    const bool reached=(o.game_boot.controller_setup_entered || o.barrier_source_pc==0x1004) && !o.barrier_module_relative
        && o.repeated_modules.source_gate_returned && o.repeated_modules.source_gate_result==1;
    out << "{\"present\":" << (reached ? "true" : "false");
    if(!reached) { out << '}'; return; }
    out << ",\"source_callsite\":" << 0x1004 << ",\"target\":" << 0x52D58
        << ",\"delay_source_pc\":" << 0x1008 << ",\"delay_word_hex\":\"3C110001\""
        << ",\"body_admitted\":" << (o.game_boot.controller_setup_entered ? "true" : "false")
        << ",\"result\":" << (o.game_boot.controller_setup_returned ? std::to_string(o.game_boot.controller_setup_result) : "null")
        << ",\"returned\":" << (o.game_boot.controller_setup_returned ? "true" : "false")
        << ",\"result_domain\":\"WindowsInputAdapter\",\"later_100C_reached\":" << (o.reached_100C ? "true" : "false") << '}';
}
void WriteGameTextValue(std::ostream& out,const dokodemo::recovered::GameNameEntrySnapshot& e){
    const auto flag=[](bool v){return v?"true":"false";};
    const auto hex=[](const std::string& v){return StartupHexBytes(reinterpret_cast<const uint8_t*>(v.data()),v.size());};
    out<<"{\"live\":"<<flag(e.live)<<",\"focused\":"<<flag(e.focused)<<",\"composing\":"<<flag(e.composing)
        <<",\"source_state\":"<<e.source_state<<",\"context\":"<<e.context<<",\"maximum_units\":"<<e.maximum_units
        <<",\"units\":"<<e.units<<",\"native_caret_units\":"<<e.native_caret_units
        <<",\"edits\":"<<e.edits<<",\"rejections\":"<<e.rejections<<",\"confirmations\":"<<e.confirmations
        <<",\"cancellations\":"<<e.cancellations<<",\"accepted\":"<<flag(e.accepted)<<",\"cancelled\":"<<flag(e.cancelled)
        <<",\"result_ready\":"<<flag(e.result_ready)<<",\"initial_cp932_hex\":"<<JsonString(hex(e.initial))
        <<",\"staged_cp932_hex\":"<<JsonString(hex(e.staged))<<",\"source120_cp932_hex\":"<<JsonString(hex(e.source120))
        <<",\"result_cp932_hex\":"<<JsonString(hex(e.result))<<",\"error\":"<<JsonString(e.error)<<'}';
}
void WriteGameProfileValue(std::ostream& out,const dokodemo::recovered::GameProfileSnapshot& p){
    const auto hex=[](const std::string& v){return StartupHexBytes(reinterpret_cast<const uint8_t*>(v.data()),v.size());};
    out<<"{\"nickname_cp932_hex\":"<<JsonString(hex(p.nickname))<<",\"player_cp932_hex\":"<<JsonString(hex(p.player))
        <<",\"comment_cp932_hex\":"<<JsonString(hex(p.comment))<<",\"gender\":"<<p.gender<<",\"year\":"<<p.year
        <<",\"month\":"<<p.month<<",\"day\":"<<p.day<<",\"zodiac\":"<<p.zodiac<<",\"blood\":"<<p.blood
        <<",\"present_count_E6\":"<<p.present<<",\"editor_field\":"<<p.editor_field
        <<",\"accepted_results\":"<<p.accepted_results<<",\"cancelled_results\":"<<p.cancelled_results
        <<",\"confirmed\":"<<(p.confirmed?"true":"false")<<",\"applied\":"<<(p.applied?"true":"false")
        <<",\"profile_loaded_marker\":false,\"psp_savedata_serialized\":false,\"stored\":{\"live\":"<<(p.stored.live?"true":"false")
        <<",\"actor\":"<<p.stored.actor<<",\"gender\":"<<p.stored.gender<<",\"packed_date\":"<<p.stored.date<<",\"blood\":"<<p.stored.blood
        <<",\"nickname_cp932_hex\":"<<JsonString(hex(p.stored.nickname))<<",\"player_cp932_hex\":"<<JsonString(hex(p.stored.player))
        <<",\"comment_cp932_hex\":"<<JsonString(hex(p.stored.comment))
        <<",\"raw16_comment_hex\":"<<JsonString(StartupHexBytes(p.stored.source_comment.data(),16))<<",\"calendar\":[";
    for(size_t i=0;i<7;++i){if(i)out<<',';out<<p.stored.calendar[i];}out<<"]}}";
}
void WriteRoomModelValue(std::ostream& out,const dokodemo::recovered::GameRoomModelSlotSnapshot& m){
    out<<"{\"active\":"<<(m.active?"true":"false")<<",\"retirement\":"<<m.retirement<<",\"generation\":"<<m.generation
       <<",\"resource\":"<<m.resource<<",\"category\":"<<m.category<<",\"selector\":"<<m.selector<<",\"source_x\":"<<m.source_x<<",\"anchor_node\":"<<m.anchor_node
       <<",\"asset_identity\":"<<m.asset_identity<<",\"clone_identity\":"<<m.clone_identity<<",\"draw_frames\":"<<m.draw_frames<<",\"draw_pixel_writes\":"<<m.draw_pixel_writes<<",\"native_root\":[";
    for(size_t i=0;i<m.native_root.size();++i){if(i)out<<',';out<<m.native_root[i];}out<<"]}";
}
void WriteGameInventoryValue(std::ostream& out,const dokodemo::recovered::GameInventorySnapshot& s){
    const auto b=[](bool v){return v?"true":"false";};
    const auto hex=[](const std::string& v){return StartupHexBytes(reinterpret_cast<const uint8_t*>(v.data()),v.size());};
    out<<"{\"live\":"<<b(s.live)<<",\"visible\":"<<b(s.visible)<<",\"actions\":"<<b(s.actions)
       <<",\"requested\":"<<b(s.requested)<<",\"request14\":"<<b(s.request14)<<",\"owner480\":"<<b(s.owner480)
       <<",\"origin\":"<<unsigned(s.origin)<<",\"retired_generation\":"<<s.retired_generation<<",\"unsupported_actions\":"<<s.unsupported_actions<<",\"generation\":"<<s.generation<<",\"phase\":"<<s.phase<<",\"stateB2C\":"<<s.stateB2C<<",\"stateB30\":"<<s.stateB30
       <<",\"filter\":"<<s.filter<<",\"count\":"<<s.count<<",\"selection\":"<<s.selection<<",\"selected_row\":"<<s.selected_row<<",\"selected_id\":"<<s.selected_id
       <<",\"selected_name_cp932_hex\":"<<JsonString(hex(s.selected_name))<<",\"selected_description_cp932_hex\":"<<JsonString(hex(s.selected_description))
       <<",\"loader464\":"<<s.loader464<<",\"loader468\":"<<s.loader468<<",\"loader469\":"<<s.loader469<<",\"loader498\":"<<s.loader498
       <<",\"requests\":"<<s.requests<<",\"opens\":"<<s.opens<<",\"close_requests\":"<<s.close_requests<<",\"close_completions\":"<<s.close_completions
       <<",\"pending_polls\":"<<s.pending_polls<<",\"completed_polls\":"<<s.completed_polls<<",\"retirement_requests\":"<<s.retirement_requests<<",\"retirements\":"<<s.retirements
       <<",\"navigation\":"<<s.navigation<<",\"action_opens\":"<<s.action_opens<<",\"action_cancels\":"<<s.action_cancels<<",\"select_result\":"<<s.select_result
       <<",\"draw_frames\":"<<s.draw_frames<<",\"glyphs\":"<<s.glyphs<<",\"quads\":"<<s.quads<<",\"resource_nodes\":"<<s.resource_nodes<<",\"resource_clips\":"<<s.resource_clips<<",\"texture_count\":"<<s.texture_count
       <<",\"action_pending\":"<<b(s.action_pending)<<",\"action_receipt\":"<<s.action_receipt<<",\"action_queue_result\":"<<s.action_queue_result
       <<",\"discard_confirmation\":"<<b(s.discard_confirmation)<<",\"discard_prompts\":"<<s.discard_prompts<<",\"discard_cancelled\":"<<s.discard_cancelled<<",\"discard_accepted\":"<<s.discard_accepted
       <<",\"discard_dialog_state\":"<<s.discard_dialog_state<<",\"discard_result\":"<<s.discard_result<<",\"discard_row\":"<<s.discard_row<<",\"discard_item_id\":"<<s.discard_item_id
       <<",\"action_event\":"<<s.action_event<<",\"action_row\":"<<s.action_row<<",\"action_events\":"<<s.action_events<<",\"action_completions\":"<<s.action_completions
       <<",\"placement_enabled\":"<<b(s.placement_enabled)<<",\"item_actions_implemented\":"<<b(true)<<",\"item_action_events\":"<<(s.origin==dokodemo::recovered::GameInventoryOrigin::Script41?"[13,14,15,16]":s.placement_enabled?"[13,14,15]":"[14,15]")
       <<",\"action_frontier\":"<<s.action_frontier<<",\"item_award_implemented\":false,\"layout_policy\":\"NativeListLayout;Script41_Actions13_14_15_16;MainMenu5_NIL6Mark14_15OrHomeGateReceiptBoundPlacement13Placed14;OtherActionsNonfatal;NativeInventoryTRCXYesZNo;OriginalCB2A_GIM_FONT\",\"items\":[";
    for(size_t i=0;i<s.items.size();++i){if(i)out<<',';const auto& v=s.items[i];out<<"{\"row\":"<<v.row<<",\"id\":"<<v.id<<",\"valid\":"<<b(v.valid)<<",\"category\":"<<v.category<<",\"date_word\":"<<v.date_word
        <<",\"name_cp932_hex\":"<<JsonString(hex(v.name))<<",\"description_cp932_hex\":"<<JsonString(hex(v.description));
        if(v.valid)out<<",\"source5_7_words\":["<<v.source_words[0]<<','<<v.source_words[1]<<','<<v.source_words[2]<<"],\"source5_7_kinds\":["<<unsigned(v.source_kinds[0])<<','<<unsigned(v.source_kinds[1])<<','<<unsigned(v.source_kinds[2])<<']';out<<'}';}
    out<<"]}";
}
void WriteGameRecordValue(std::ostream& out,const dokodemo::recovered::GameRecordStoreSnapshot& s){
    out<<"{\"live\":"<<(s.live?"true":"false")<<",\"generation\":"<<s.generation<<",\"calls\":"<<s.calls<<",\"writes\":"<<s.writes
       <<",\"selected\":"<<s.selected<<",\"last_copied\":"<<(s.last_copied?"true":"false")<<",\"last_protected\":"<<(s.last_protected?"true":"false")
       <<",\"protected_calls\":"<<s.protected_calls<<",\"capacity_errors\":"<<s.capacity_errors
       <<",\"date\":["<<s.date[0]<<','<<s.date[1]<<','<<s.date[2]<<"],\"calendar_policy\":\"NativeCivilCalendarFromSameLiveClockWord;OriginalCalendarWriterUnclosed\""
       <<",\"copy_policy\":\"NativeOwnedFixed200ZeroTail;SourceBlindCopyNoTerminator\",\"original_save_import_export\":false,\"bank0\":[";
    for(size_t i=0;i<s.bank0.size();++i){if(i)out<<',';const auto& v=s.bank0[i];
        out<<"{\"flag\":"<<unsigned(v.flag)<<",\"value\":"<<v.value<<",\"date\":["<<v.date[0]<<','<<v.date[1]<<','<<v.date[2]
           <<"],\"source_known_payload_bytes\":"<<v.source_known_payload_bytes<<",\"native_payload_hex\":"<<JsonString(StartupHexBytes(v.payload.data(),v.payload.size()))<<'}';}
    out<<"]}";
}
void WriteVmValueWitness(std::ostream& out,const dokodemo::script::ValueWitness& v){
    out<<"{\"kind\":"<<uint32_t(v.kind)<<",\"word\":"<<v.word<<",\"table\":"<<v.table<<",\"index\":"<<v.index
       <<",\"cp932_bytes\":"<<v.cp932_bytes<<",\"cp932_sha256\":"<<JsonString(StartupHexBytes(v.cp932_sha256.data(),v.cp932_sha256.size()))<<'}';
}
void WriteVmStateWitness(std::ostream& out,const dokodemo::script::VmStateWitness& w){
    const auto flag=[](bool b){return b?"true":"false";};
    out<<"{\"valid\":"<<flag(w.valid)<<",\"run_id\":"<<w.run_id<<",\"frame_id\":"<<w.frame_id<<",\"executed\":"<<w.executed
       <<",\"current_pc\":"<<w.current_pc<<",\"code_entry\":"<<w.code_entry<<",\"code_sha256\":"<<JsonString(std::string(w.code_sha256.data()))
       <<",\"context_registered\":"<<flag(w.context_registered)<<",\"context_word\":"<<w.context_word<<",\"stopped\":"<<flag(w.stopped)
       <<",\"event_envelope_present\":"<<flag(w.event_envelope_present)<<",\"event_scope_depth\":"<<w.event_scope_depth<<",\"queued_callbacks\":"<<w.queued_callbacks
       <<",\"call_depth\":"<<w.call_depth<<",\"active_blocks\":"<<w.active_blocks<<",\"clock_tick\":"<<w.clock_tick<<",\"deadline_tick\":"<<w.deadline_tick
       <<",\"relative_deadline23\":"<<w.relative_deadline23<<",\"caller_entry\":"<<w.caller_entry<<",\"caller_call_pc\":"<<w.caller_call_pc
       <<",\"caller_return_pc\":"<<w.caller_return_pc<<",\"caller_frame_id\":"<<w.caller_frame_id<<",\"caller_stack_base\":"<<w.caller_stack_base
       <<",\"frame_cells\":"<<w.frame_cells<<",\"operand_cells\":"<<w.operand_cells<<",\"frame_captured\":"<<w.frame_captured<<",\"operands_captured\":"<<w.operands_captured
       <<",\"capture_policy\":\"Prefix16;TypedRawNoReferenceResolution;StringSHA256NotText\",\"result\":";
    WriteVmValueWitness(out,w.result);out<<",\"frame_values\":[";
    for(uint32_t i=0;i<w.frame_captured;++i){if(i)out<<',';WriteVmValueWitness(out,w.frame_values[i]);}out<<"],\"operands\":[";
    for(uint32_t i=0;i<w.operands_captured;++i){if(i)out<<',';WriteVmValueWitness(out,w.operands[i]);}out<<"]}";
}
void WriteNormalReturnWitness(std::ostream& out,const dokodemo::script::NormalReturnWitness& w){
    out<<"{\"valid\":"<<(w.valid?"true":"false")<<",\"sequence\":"<<w.sequence<<",\"opcode_pc\":"<<w.opcode_pc<<",\"before\":";
    WriteVmStateWitness(out,w.before);out<<",\"after\":";WriteVmStateWitness(out,w.after);out<<'}';
}

void WriteFlow5Event(std::ostream& out,const dokodemo::recovered::GameFlow5EventSnapshot& e){
    const auto b=[](bool v){return v?"true":"false";};
    out<<"{\"accepted\":"<<b(e.accepted)<<",\"queued\":"<<b(e.queued)<<",\"returned\":"<<b(e.returned)<<",\"restored_exact\":"<<b(e.restored_exact)
       <<",\"enabled_before\":"<<b(e.enabled_before)<<",\"enabled_after\":"<<b(e.enabled_after)<<",\"guards_valid\":"<<b(e.guards_valid)
       <<",\"manager9_before\":"<<b(e.manager9_before)<<",\"manager9_after\":"<<b(e.manager9_after)
       <<",\"generation\":"<<e.generation<<",\"receipt\":"<<e.receipt<<",\"callback_frame\":"<<e.callback_frame
       <<",\"attempts\":"<<e.attempts<<",\"unobserved\":"<<e.unobserved<<",\"callback_word\":"<<e.callback_word
       <<",\"queue_result\":"<<e.queue_result<<",\"return_pc\":"<<e.return_pc<<",\"return_opcode\":"<<e.return_opcode<<",\"calls\":[";
    for(uint32_t i=0;i<e.calls;++i){if(i)out<<',';out<<"{\"pc\":"<<e.call_pcs[i]<<",\"entry\":"<<e.call_entries[i]<<'}';}
    const auto guards=[&](const char* name,const auto& v){out<<"],\""<<name<<"\":[";for(size_t i=0;i<v.size();++i){if(i)out<<',';WriteVmValueWitness(out,v[i]);}};
    guards("guards_before",e.guards_before);guards("guards_after",e.guards_after);
    out<<"],\"suspended\":";WriteVmStateWitness(out,e.suspended);out<<",\"entered\":";WriteVmStateWitness(out,e.entered);
    out<<",\"restored\":";WriteVmStateWitness(out,e.restored);out<<'}';
}
void WriteGameBootValue(std::ostream& out, const dokodemo::recovered::GameBootObservation& boot) {
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    out << "{\"controller_setup_entered\":" << boolean(boot.controller_setup_entered)
        << ",\"controller_setup_returned\":" << boolean(boot.controller_setup_returned)
        << ",\"sampling_mode\":" << boot.sampling_mode << ",\"sampling_cycle\":" << boot.sampling_cycle
        << ",\"memory_initialization_entered\":" << boolean(boot.memory_initialization_entered)
        << ",\"memory_initialization_returned\":" << boolean(boot.memory_initialization_returned)
        << ",\"memory_initialization_succeeded\":" << boolean(boot.memory_initialization_succeeded)
        << ",\"allocator_policy\":\"NonMatchingNativeFreeList\",\"default_arena\":";
    if (boot.default_arena == UINT32_MAX) out << "null"; else out << boot.default_arena;
    out << ",\"arenas\":[";
    for (size_t i = 0; i < boot.arenas.size(); ++i) {
        const auto& arena = boot.arenas[i];
        if (i) out << ',';
        out << "{\"live\":" << boolean(arena.live) << ",\"requested_bytes\":" << arena.requested_bytes
            << ",\"source_coordinate\":" << arena.source_coordinate << ",\"host_address\":" << arena.host_address
            << ",\"committed_bytes\":" << arena.committed_bytes << '}';
    }
    out << "],\"graphics_setup_entered\":" << boolean(boot.graphics_setup_entered)
        << ",\"graphics_setup_returned\":" << boolean(boot.graphics_setup_returned)
        << ",\"renderer\":{\"backend\":\"NativeSoftwareDib\",\"live\":" << boolean(boot.graphics.live)
        << ",\"initialized\":" << boolean(boot.graphics.initialized)
        << ",\"diagnostic_frame\":" << boolean(boot.graphics.diagnostic_frame)
        << ",\"generation\":" << boot.graphics.generation << ",\"frame_sequence\":" << boot.graphics.frame_sequence
        << ",\"present_count\":" << boot.graphics.present_count
        << ",\"last_presented_sequence\":" << boot.graphics.last_presented_sequence
        << ",\"draw_calls\":" << boot.graphics.draw_calls << ",\"color_bytes\":" << boot.graphics.color_bytes
        << ",\"mesh_triangles\":" << boot.graphics.mesh_triangles
        << ",\"clipped_triangles\":" << boot.graphics.clipped_triangles
        << ",\"textured_triangles\":" << boot.graphics.textured_triangles
        << ",\"mesh_pixels\":" << boot.graphics.mesh_pixels
        << ",\"depth_bytes\":" << boot.graphics.depth_bytes
        << ",\"matrix_bytes\":" << boot.graphics.matrix_bytes << ",\"matrix_mode\":" << boot.graphics.matrix_mode
        << ",\"matrix_capacities\":[" << boot.graphics.config.matrix_capacities[0] << ',' << boot.graphics.config.matrix_capacities[1]
        << ',' << boot.graphics.config.matrix_capacities[2] << ',' << boot.graphics.config.matrix_capacities[3] << ']'
        << ",\"width\":" << boot.graphics.config.width << ",\"height\":" << boot.graphics.config.height
        << ",\"stride\":" << boot.graphics.config.stride
        << ",\"source_pixel_format\":" << boot.graphics.config.source_pixel_format
        << ",\"draw_buffer_index\":" << boot.graphics.draw_buffer_index
        << ",\"display_buffer_index\":" << boot.graphics.display_buffer_index
        << ",\"enabled_state_mask\":" << boot.graphics.config.enabled_state_mask
        << ",\"depth_write\":" << boolean(boot.graphics.config.depth_write)
        << ",\"cull_face\":" << boolean(boot.graphics.config.cull_face)
        << ",\"front_face_ccw\":" << boolean(boot.graphics.config.front_face_counter_clockwise)
        << ",\"smooth_shading\":" << boolean(boot.graphics.config.smooth_shading)
        << ",\"viewport\":[" << boot.graphics.config.viewport_x << ',' << boot.graphics.config.viewport_y << ','
        << boot.graphics.config.width << ',' << boot.graphics.config.height << ']'
        << ",\"offset\":[" << boot.graphics.config.offset_x << ',' << boot.graphics.config.offset_y << ']'
        << ",\"depth_range\":[" << boot.graphics.config.depth_near << ',' << boot.graphics.config.depth_far << ']'
        << ",\"source_buffer_offsets\":[" << boot.graphics.config.source_buffer_offsets[0] << ','
        << boot.graphics.config.source_buffer_offsets[1] << ',' << boot.graphics.config.source_buffer_offsets[2]
        << "],\"source_offsets_are_host_pointers\":false,\"game_content\":" << boolean(boot.content.title.frame_drawn) << '}'
        << ",\"rtc_initialized\":" << boolean(boot.rtc_initialized) << ",\"rtc_error\":" << boot.rtc_error
        << ",\"utc_tick_us\":" << boot.utc_tick_us << ",\"local_tick_us\":" << boot.local_tick_us
        << ",\"effective_offset_minutes\":" << boot.effective_offset_minutes << ",\"calendar_words\":[";
    for (size_t i = 0; i < boot.calendar_words.size(); ++i) { if (i) out << ','; out << boot.calendar_words[i]; }
    out << "],\"rng_seed_available\":" << boolean(boot.rng_seed_available)
        << ",\"rng_seed_fixture\":"<<boolean(boot.rng_seed_fixture)<<",\"rng_live_seed_word\":"<<boot.rng_live_seed_word
        << ",\"rng_seed_word\":" << boot.rng_seed_word << ",\"rng_seed\":" << boot.rng_seed
        << ",\"runtime_reset_returned\":" << boolean(boot.runtime_reset_returned)
        << ",\"primary_reset_bytes\":" << boot.primary_reset_bytes
        << ",\"secondary_reset_bytes\":" << boot.secondary_reset_bytes
        << ",\"filesystem_initialization_entered\":" << boolean(boot.filesystem_initialization_entered)
        << ",\"filesystem\":{\"mounted\":" << boolean(boot.filesystem.mounted)
        << ",\"closed\":" << boolean(boot.filesystem.closed)
        << ",\"status\":" << JsonString(dokodemo::recovered::GameFsCodeName(boot.filesystem.status.code))
        << ",\"native_error\":" << boot.filesystem.status.native_error
        << ",\"detail\":" << JsonString(boot.filesystem.status.detail)
        << ",\"file_live\":" << boolean(boot.filesystem.io.file_live)
        << ",\"file_bytes\":" << boot.filesystem.io.file_bytes
        << ",\"transferred_bytes\":" << boot.filesystem.io.transferred_bytes
        << ",\"read_calls\":" << boot.filesystem.io.read_calls
        << ",\"live_allocations\":" << boot.filesystem.io.live_allocations
        << ",\"directory_bytes\":" << boot.filesystem.directory_bytes
        << ",\"directory_slots\":" << boot.filesystem.directory_slots
        << ",\"child_capacity\":" << boot.filesystem.child_capacity
        << ",\"loaded_members\":" << boot.filesystem.loaded_members << '}'
        << ",\"scene\":{\"workspaces_ready\":" << boolean(boot.scene.workspaces_ready)
        << ",\"pools_ready\":" << boolean(boot.scene.pools_ready)
        << ",\"filesystem_bound\":" << boolean(boot.scene.filesystem_bound)
        << ",\"resource_prefix_empty\":" << boolean(boot.scene.resource_prefix_empty)
        << ",\"resource_lookup_bound\":" << boolean(boot.scene.resource_lookup_bound)
        << ",\"registry_ready\":" << boolean(boot.scene.registry_ready)
        << ",\"registered_subsystems\":" << boot.scene.registered_subsystems
        << ",\"camera_ready\":" << boolean(boot.scene.camera_ready)
        << ",\"returned_9dc\":" << boolean(boot.scene.returned_9dc)
        << ",\"group_count\":" << boot.scene.group_count << ",\"active_context\":" << boot.scene.active_context
        << ",\"finished_contexts\":" << boot.scene.finished_contexts
        << ",\"context_buffer_bytes\":" << boot.scene.context_buffer_bytes
        << ",\"pool_bytes\":" << boot.scene.pool_bytes
        << ",\"native_clear_count\":" << boot.scene.native_clear_count
        << ",\"light_count\":" << boot.scene.light_count << ",\"scene_flags\":" << boot.scene.scene_flags
        << ",\"camera_y\":" << boot.scene.camera_y << ",\"camera_near\":" << boot.scene.camera_near
        << ",\"camera_far\":" << boot.scene.camera_far << ",\"camera_angle\":" << boot.scene.camera_angle
        << ",\"math_policy\":\"NonMatchingStdTanLookAtProjection\",\"game_content\":false}"
        << ",\"vm\":{\"initialized\":" << boolean(boot.vm.initialized)
        << ",\"arena_live\":" << boolean(boot.vm.arena_live) << ",\"arena_bytes\":" << boot.vm.arena_bytes
        << ",\"timer_live\":" << boolean(boot.vm.timer_live) << ",\"timer_started\":" << boolean(boot.vm.timer_started)
        << ",\"timer_elapsed_us\":" << boot.vm.timer_elapsed_us
        << ",\"timer_policy\":\"NativeMonotonicStopwatchNoScheduledHandler\""
        << ",\"singletons_reset\":" << boolean(boot.vm.singletons_reset)
        << ",\"manager_ready\":" << boolean(boot.vm.manager_ready) << ",\"manager_slots\":" << boot.vm.manager_slots
        << ",\"workspace_capacity\":" << boot.vm.workspace_capacity << '}'
        << ",\"source_callback_stored\":" << boolean(boot.source_callback_stored)
        << ",\"source_callback_callable\":" << boolean(boot.source_callback_callable)
        << ",\"source_audio_ready\":" << boolean(boot.source_audio_ready)
        << ",\"source_audio_initialization_result\":" << boot.source_audio_initialization_result
        << ",\"b0c_returned\":" << boolean(boot.b0c_returned)
        << ",\"content\":{\"vsync_owner\":" << boolean(boot.content.vsync_owner)
        << ",\"loadman_owner\":" << boolean(boot.content.loadman_owner)
        << ",\"bb8_returned\":" << boolean(boot.content.bb8_returned)
        << ",\"state_reset\":" << boolean(boot.content.state_reset)
        << ",\"import_callback_configured\":" << boolean(boot.content.import_callback_configured)
        << ",\"import_callback_registered\":" << boolean(boot.content.import_callback_registered)
        << ",\"import_callback_delivery\":\"UnavailableSgxNoSyntheticInterrupt\",\"import_registration_source_result\":null"
        << ",\"driver_policy\":\"NativeColdLoadingAndFade\",\"general_game_loop\":false"
        << ",\"registered_records\":" << boot.content.registered_records
        << ",\"loading_ticks\":" << boot.content.loading_ticks << ",\"load_state\":" << boot.content.load_state
        << ",\"mount_state\":" << boot.content.mount_state
        << ",\"previous_scene\":" << boot.content.previous_scene << ",\"current_scene\":" << boot.content.current_scene
        << ",\"next_scene\":" << boot.content.next_scene << ",\"requested_bits\":" << boot.content.requested_bits
        << ",\"owned_bits\":" << boot.content.owned_bits << ",\"completed_bits\":" << boot.content.completed_bits
        << ",\"package_mounted\":" << boolean(boot.content.package_mounted)
        << ",\"package_bytes\":" << boot.content.package_bytes << ",\"package_slots\":" << boot.content.package_slots
        << ",\"font_ready\":" << boolean(boot.content.font_ready) << ",\"blocked\":" << boolean(boot.content.blocked)
        << ",\"closed\":" << boolean(boot.content.closed) << ",\"frontier_pc\":" << boot.content.frontier_pc
        << ",\"status\":" << JsonString(dokodemo::recovered::GameFsCodeName(boot.content.status.code))
        << ",\"detail\":" << JsonString(boot.content.status.detail)
        << ",\"scene_substate\":" << boot.content.scene_substate
        << ",\"opened_resources\":" << boot.content.opened_resources
        << ",\"title_mounted\":" << boolean(boot.content.title_mounted)
        << ",\"title_ui_ready\":" << boolean(boot.content.title_ui_ready)
        << ",\"title_model_read\":" << boolean(boot.content.title_model_read)
        << ",\"title_constructor_returned\":" << boolean(boot.content.title_constructor_returned)
        << ",\"title_bytes\":" << boot.content.title_bytes << ",\"title_slots\":" << boot.content.title_slots
        << ",\"title_nodes\":" << boot.content.title_nodes << ",\"title_clips\":" << boot.content.title_clips
        << ",\"title_textures\":" << boot.content.title_textures
        << ",\"title_model_bytes\":" << boot.content.title_model_bytes
        << ",\"title_model_nodes\":" << boot.content.title_model_nodes
        << ",\"title_model_materials\":" << boot.content.title_model_materials
        << ",\"title\":{\"live\":" << boolean(boot.content.title.live)
        << ",\"constructor_returned\":" << boolean(boot.content.title.constructor_returned)
        << ",\"camera_ready\":" << boolean(boot.content.title.camera_ready)
        << ",\"frame_drawn\":" << boolean(boot.content.title.frame_drawn)
        << ",\"initial_layout_policy\":\"ExplicitNativeChoiceNotPSP7E65CObservation\""
        << ",\"layout\":" << boot.content.title.layout
        << ",\"state\":" << boot.content.title.state
        << ",\"updates\":" << boot.content.title.updates
        << ",\"frames\":" << boot.content.title.frames
        << ",\"visible_actors\":" << boot.content.title.visible_actors
        << ",\"model_assets\":" << boot.content.title.model_assets
        << ",\"motions\":" << boot.content.title.motions
        << ",\"clones\":" << boot.content.title.clones
        << ",\"vertices\":" << boot.content.title.vertices
        << ",\"triangles\":" << boot.content.title.triangles
        << ",\"pixel_writes\":" << boot.content.title.pixel_writes
        << ",\"native_tick_units\":" << boot.content.title.native_tick_units
        << ",\"approximate_curves\":" << boot.content.title.approximate_curves
        << ",\"menu_input_implemented\":" << boolean(boot.content.title.menu_input_implemented)
        << ",\"scene3_completed\":" << boolean(boot.content.title.scene3_completed)
        << ",\"arrival_branch_pending\":" << boolean(boot.content.title.arrival_branch_pending)
        << ",\"primary_arrivals\":" << boot.content.title.primary_arrivals
        << ",\"primary_restarts\":" << boot.content.title.primary_restarts
        << ",\"kuro_selections\":" << boot.content.title.kuro_selections
        << ",\"rng_calls\":" << boot.content.title.rng_calls
        << ",\"arrival_countdown\":" << boot.content.title.arrival_countdown
        << ",\"media_adapter\":\"UnsupportedPSPRemovableDeviceReturnsUnavailable0;NativeSaveRootSeparate\""
        << ",\"attract_flag_policy\":\"NativeDisabledNotPSP2A950Observation\""
        << ",\"menu_row\":" << boot.content.title.menu_row
        << ",\"selected_actor\":" << boot.content.title.selected_actor
        << ",\"pending_root\":" << boot.content.title.pending_root
        << ",\"confirmations\":" << boot.content.title.confirmations
        << ",\"navigation_events\":" << boot.content.title.navigation_events
        << ",\"ui_quads\":" << boot.content.title.ui_quads
        << ",\"ui_callbacks\":" << boot.content.title.ui_callbacks
        << ",\"visible_roots\":" << boot.content.title.visible_roots
        << ",\"model_pixel_writes\":" << boot.content.title.model_pixel_writes
        << ",\"selection_moving\":" << boolean(boot.content.title.selection_moving)
        << ",\"selection_motion_complete\":" << boolean(boot.content.title.selection_motion_complete)
        << ",\"softkey_requested\":" << boolean(boot.content.title.softkey_requested)
        << ",\"camera_position\":[0,-4400,560],\"camera_lookpoint\":[0,-4399,560]"
        << ",\"field_of_view\":" << boot.content.title.field_of_view
        << ",\"near_plane\":" << boot.content.title.near_plane
        << ",\"far_plane\":" << boot.content.title.far_plane
        << ",\"actors\":[";
    for(size_t i=0;i<boot.content.title.actors.size();++i) {
        if(i)out<<',';const auto& actor=boot.content.title.actors[i];
        out<<"{\"type\":"<<actor.type<<",\"animation_time\":"<<actor.animation_time
            <<",\"motion\":"<<actor.motion
            <<",\"movement_counter\":"<<actor.movement_counter<<",\"local_pose_digest\":"<<actor.local_pose_digest
            <<",\"desired_visible\":"<<boolean(actor.desired_visible)<<",\"visible\":"<<boolean(actor.visible)
            <<",\"moving\":"<<boolean(actor.moving)<<",\"position\":["<<actor.x<<','<<actor.y<<','<<actor.z<<']'
            <<",\"target\":";
        if(actor.target_produced)out<<'['<<actor.target_x<<','<<actor.target_y<<']';else out<<"null";
        out<<'}';
    }
    out<<"],\"profile\":";WriteGameProfileValue(out,boot.content.title.profile);out<<'}'
        << ",\"next_resource\":" << JsonString(boot.content.next_resource)
        << ",\"inventory\":";WriteGameInventoryValue(out,boot.content.inventory);out
        << ",\"actor_dictionary_requested\":"<<boolean(boot.content.actor_dictionary_requested)
        << ",\"actor_message_contexts\":"<<boot.content.actor_message_contexts
        << ",\"actor_message_stale_inputs\":"<<boot.content.actor_message_stale_inputs
        << ",\"actor_message_input_armed\":"<<boolean(boot.content.actor_message_input_armed)
        << ",\"actor_message_draw_frames\":"<<boot.content.actor_message_draw_frames<<",\"actor_message_glyphs\":"<<boot.content.actor_message_glyphs
        << ",\"actor_message_ink_pixels\":"<<boot.content.actor_message_ink_pixels
        << ",\"actor_dictionary_ready\":"<<boolean(boot.content.actor_dictionary_ready)
        << ",\"actor_dictionary_live\":"<<boolean(boot.content.actor_dictionary_live)
        << ",\"actor_package_bytes\":"<<boot.content.actor_package_bytes
        << ",\"actor_dictionary_bytes\":"<<boot.content.actor_dictionary_bytes
        << ",\"actor_dictionary_path\":"<<JsonString(boot.content.actor_dictionary_path)
        << ",\"actor_dictionary_prefix_hex\":"<<JsonString(StartupHexBytes(boot.content.actor_dictionary_prefix.data(),16))
        << ",\"source_loader469\":"<<boolean(boot.content.source_loader469)
        << ",\"texture_cache\":{\"policy\":\"NativeRetireCacheOnlyOnPressure;Caps512And64MiB;LiveImagesPreserved\",\"entries\":"<<boot.content.texture_cache_entries<<",\"pixels\":"<<boot.content.texture_cache_pixels<<",\"evictions\":"<<boot.content.texture_cache_evictions<<'}'
        << ",\"main_flow\":{\"live\":"<<boolean(boot.content.main_flow.live)
        <<",\"generation\":"<<boot.content.main_flow.generation<<",\"phase1C\":"<<boot.content.main_flow.phase1C
        <<",\"return20\":"<<boot.content.main_flow.return20<<",\"substate24\":"<<boot.content.main_flow.substate24
        <<",\"steps\":"<<boot.content.main_flow.steps<<",\"last_pc\":"<<boot.content.main_flow.last_pc
        <<",\"phase5_entries\":"<<boot.content.main_flow.phase5_entries<<",\"request8_clears\":"<<boot.content.main_flow.request8_clears
        <<",\"request9_clears\":"<<boot.content.main_flow.request9_clears<<",\"request9_consumed\":"<<boot.content.main_flow.request9_consumed
        <<",\"phase1_request9_consumed\":"<<boot.content.main_flow.phase1_request9_consumed<<",\"outer_returns\":"<<boot.content.main_flow.outer_returns
        <<",\"busy_blocks\":"<<boot.content.main_flow.busy_blocks<<",\"loader_blocks\":"<<boot.content.main_flow.loader_blocks
        <<",\"circle_source_blocks\":"<<boot.content.main_flow.circle_source_blocks
        <<",\"events\":"<<boot.content.main_flow.events<<",\"last_event\":"<<boot.content.main_flow.last_event<<",\"last_event_argument\":"<<boot.content.main_flow.last_event_argument
        <<",\"last_extension_busy\":"<<boolean(boot.content.main_flow.last_extension_busy)<<",\"last_loader469\":"<<boolean(boot.content.main_flow.last_loader469)
        <<",\"circle_counter\":"<<boot.content.main_flow.circle_counter<<",\"menu58\":"<<boot.content.main_flow.menu58<<",\"menu_result50\":"<<boot.content.main_flow.menu_result50<<",\"menu_result54\":"<<boot.content.main_flow.menu_result54<<"}"
        << ",\"post_modal\":{\"policy\":\"WindowsPostModalInteraction;NotOriginal469ClearOrPost32Wake\""
        <<",\"live\":"<<boolean(boot.content.post_modal.live)<<",\"eligible\":"<<boolean(boot.content.post_modal.eligible)
        <<",\"armed\":"<<boolean(boot.content.post_modal.armed)<<",\"source_gate_blocked\":"<<boolean(boot.content.post_modal.source_gate_blocked)
        <<",\"fresh_input\":"<<boolean(boot.content.post_modal.fresh_input)<<",\"event_posted\":"<<boolean(boot.content.post_modal.event_posted)
        <<",\"callback_returned\":"<<boolean(boot.content.post_modal.callback_returned)<<",\"generation\":"<<boot.content.post_modal.generation
        <<",\"spent_history\":"<<boolean(boot.content.post_modal.spent_history)<<",\"inventory_generation\":"<<boot.content.post_modal.inventory_generation<<",\"script_generation\":"<<boot.content.post_modal.script_generation
        <<",\"last_sequence\":"<<boot.content.post_modal.last_sequence<<",\"context_word\":"<<boot.content.post_modal.context_word
        <<",\"callback_word\":"<<boot.content.post_modal.callback_word<<",\"posted\":"<<boot.content.post_modal.posted
        <<",\"returned\":"<<boot.content.post_modal.returned<<",\"rejected\":"<<boot.content.post_modal.rejected
        <<",\"error\":"<<JsonString(boot.content.post_modal.error)<<"}"
        << ",\"returned_interaction\":{\"policy\":\"WindowsReturnedConversationInteraction;NotOriginal469ClearOrWake\""
        <<",\"live\":"<<boolean(boot.content.returned_interaction.live)<<",\"candidate\":"<<boolean(boot.content.returned_interaction.candidate)
        <<",\"returned\":"<<boolean(boot.content.returned_interaction.returned)<<",\"admitted\":"<<boolean(boot.content.returned_interaction.admitted)
        <<",\"eligible\":"<<boolean(boot.content.returned_interaction.eligible)<<",\"armed\":"<<boolean(boot.content.returned_interaction.armed)
        <<",\"event_posted\":"<<boolean(boot.content.returned_interaction.event_posted)<<",\"callback_returned\":"<<boolean(boot.content.returned_interaction.callback_returned)
        <<",\"revoked\":"<<boolean(boot.content.returned_interaction.revoked)<<",\"source_gate_blocked\":"<<boolean(boot.content.returned_interaction.source_gate_blocked)
        <<",\"owner\":"<<boot.content.returned_interaction.owner<<",\"generation\":"<<boot.content.returned_interaction.generation
        <<",\"script_generation\":"<<boot.content.returned_interaction.script_generation<<",\"run_id\":"<<boot.content.returned_interaction.run_id
        <<",\"message_context\":"<<boot.content.returned_interaction.message_context<<",\"message_frame\":"<<boot.content.returned_interaction.message_frame
        <<",\"caller_frame\":"<<boot.content.returned_interaction.caller_frame<<",\"prior_sequence\":"<<boot.content.returned_interaction.prior_sequence
        <<",\"return_sequence\":"<<boot.content.returned_interaction.return_sequence<<",\"retained_sequence\":"<<boot.content.returned_interaction.retained_sequence
        <<",\"last_sequence\":"<<boot.content.returned_interaction.last_sequence<<",\"message\":"<<boot.content.returned_interaction.message
        <<",\"caller_entry\":"<<boot.content.returned_interaction.caller_entry<<",\"caller_pc\":"<<boot.content.returned_interaction.caller_pc
        <<",\"game3_baseline\":"<<boot.content.returned_interaction.game3_baseline<<",\"game3_calls\":"<<boot.content.returned_interaction.game3_calls
        <<",\"game3_pc\":"<<boot.content.returned_interaction.game3_pc<<",\"context_word\":"<<boot.content.returned_interaction.context_word
        <<",\"callback_word\":"<<boot.content.returned_interaction.callback_word<<",\"posted\":"<<boot.content.returned_interaction.posted
        <<",\"rejected\":"<<boot.content.returned_interaction.rejected<<",\"returned_callbacks\":"<<boot.content.returned_interaction.returned_callbacks
        <<",\"queue_result\":"<<boot.content.returned_interaction.queue_result<<",\"code_sha256\":"<<JsonString(boot.content.returned_interaction.code_sha256)
        <<",\"error\":"<<JsonString(boot.content.returned_interaction.error)
        <<",\"activation\":{\"kind\":"<<uint32_t(boot.content.returned_interaction.activation.kind)
        <<",\"id\":"<<boot.content.returned_interaction.activation.id
        <<",\"live\":"<<boolean(boot.content.returned_interaction.activation.live)
        <<",\"body_bound\":"<<boolean(boot.content.returned_interaction.activation.body_bound)
        <<",\"body_completed\":"<<boolean(boot.content.returned_interaction.activation.body_completed)
        <<",\"farewell_bound\":"<<boolean(boot.content.returned_interaction.activation.farewell_bound)
        <<",\"returned\":"<<boolean(boot.content.returned_interaction.activation.returned)
        <<",\"main_consumed\":"<<boolean(boot.content.returned_interaction.activation.main_consumed)
        <<",\"consumed\":"<<boolean(boot.content.returned_interaction.activation.consumed)
        <<",\"revoked\":"<<boolean(boot.content.returned_interaction.activation.revoked)
        <<",\"script_generation\":"<<boot.content.returned_interaction.activation.script_generation
        <<",\"run_id\":"<<boot.content.returned_interaction.activation.run_id
        <<",\"context_word\":"<<boot.content.returned_interaction.activation.context_word
        <<",\"body_frame\":"<<boot.content.returned_interaction.activation.body_frame
        <<",\"body_caller_frame\":"<<boot.content.returned_interaction.activation.body_caller_frame
        <<",\"body_message\":"<<boot.content.returned_interaction.activation.body_message
        <<",\"body_ack\":"<<boot.content.returned_interaction.activation.body_ack
        <<",\"body_pc\":"<<boot.content.returned_interaction.activation.body_pc
        <<",\"body_caller_entry\":"<<boot.content.returned_interaction.activation.body_caller_entry
        <<",\"body_caller_pc\":"<<boot.content.returned_interaction.activation.body_caller_pc
        <<",\"body_prior_sequence\":"<<boot.content.returned_interaction.activation.body_prior_sequence
        <<",\"body_sequence\":"<<boot.content.returned_interaction.activation.body_sequence
        <<",\"message_frame\":"<<boot.content.returned_interaction.activation.message_frame
        <<",\"farewell_pc\":"<<boot.content.returned_interaction.activation.farewell_pc
        <<",\"farewell_current_pc\":"<<boot.content.returned_interaction.activation.farewell_current_pc
        <<",\"owned_receipt\":"<<boot.content.returned_interaction.activation.owned_receipt
        <<",\"origin_activation\":"<<boot.content.returned_interaction.activation.origin_activation
        <<",\"error\":"<<JsonString(boot.content.returned_interaction.activation.error)<<"}}"
        << ",\"actor_script\":{\"live\":"<<boolean(boot.content.actor_script.live)
        <<",\"generation\":"<<boot.content.actor_script.generation
        <<",\"scene4_actor_geometry_implemented\":"<<boolean(boot.content.actor_script.runtime.actor_draw_frames>0)<<",\"source182F4_complete\":false"
        <<",\"inventory_requested\":"<<boolean(boot.content.actor_script.inventory_requested)<<",\"inventory_requests\":"<<boot.content.actor_script.inventory_requests
        <<",\"inventory_gui_generation\":"<<boot.content.actor_script.inventory_gui_generation<<",\"menu_inventory_live\":"<<boolean(boot.content.actor_script.menu_inventory_live)<<",\"menu_inventory_requests\":"<<boot.content.actor_script.menu_inventory_requests<<",\"menu_inventory_completions\":"<<boot.content.actor_script.menu_inventory_completions<<",\"inventory_completions\":"<<boot.content.actor_script.inventory_completions<<",\"inventory_setter_result_word\":"<<boot.content.actor_script.inventory_setter_result_word
        <<",\"game71_calls\":"<<boot.content.actor_script.game71_calls<<",\"game71_result_word\":"<<boot.content.actor_script.game71_result_word
        <<",\"record_clock_word\":"<<boot.content.actor_script.record_clock_word<<",\"record_cell0_writes\":"<<boot.content.actor_script.record_cell0_writes
        <<",\"record_cell0_before\":"<<boot.content.actor_script.record_cell0_before<<",\"record_cell0_after\":"<<boot.content.actor_script.record_cell0_after
        <<",\"record_input_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.record_input_text.data()),boot.content.actor_script.record_input_text.size()))
        <<",\"game3_calls\":"<<boot.content.actor_script.game3_calls<<",\"game3_result_word\":"<<boot.content.actor_script.game3_result_word
        <<",\"owner_event_calls\":"<<boot.content.actor_script.owner_event_calls<<",\"owner_event_result\":"<<boot.content.actor_script.owner_event_result
        <<",\"sound73_requests\":"<<boot.content.actor_script.sound73_requests<<",\"sound73_result_word\":"<<boot.content.actor_script.sound73_result_word
        <<",\"sound73_playback_result\":"<<boot.content.actor_script.sound73_playback_result
        <<",\"owner_event_callback_word\":"<<boot.content.actor_script.owner_event_callback_word<<",\"context_word\":"<<boot.content.actor_script.context_word
        <<",\"callback_depth\":"<<boot.content.actor_script.callback_depth<<",\"queued_callbacks\":"<<boot.content.actor_script.queued_callbacks
        <<",\"operand_count\":"<<boot.content.actor_script.operand_count<<",\"deadline_tick\":"<<boot.content.actor_script.deadline_tick
        <<",\"records\":";WriteGameRecordValue(out,boot.content.actor_script.records);out
        <<",\"context_enabled\":"<<boolean(boot.content.actor_script.context_enabled)<<",\"callbacks_posted\":"<<boot.content.actor_script.callbacks_posted<<",\"callbacks_returned\":"<<boot.content.actor_script.callbacks_returned
        <<",\"started\":"<<boolean(boot.content.actor_script.started)<<",\"completed\":"<<boolean(boot.content.actor_script.completed)
        <<",\"blocked\":"<<boolean(boot.content.actor_script.blocked)<<",\"awaiting_message\":"<<boolean(boot.content.actor_script.awaiting_message)
        <<",\"bytes\":"<<boot.content.actor_script.bytes<<",\"path\":"<<JsonString(boot.content.actor_script.path)
        <<",\"sha256\":"<<JsonString(boot.content.actor_script.sha256)<<",\"entry_pc\":"<<boot.content.actor_script.entry_pc
        <<",\"pc\":"<<boot.content.actor_script.pc<<",\"last_opcode\":"<<boot.content.actor_script.last_opcode
        <<",\"ticks\":"<<boot.content.actor_script.ticks<<",\"yields\":"<<boot.content.actor_script.yields
        <<",\"native_clock_ticks\":"<<boot.content.actor_script.native_clock_ticks
        <<",\"instructions\":"<<boot.content.actor_script.instructions<<",\"messages\":"<<boot.content.actor_script.messages
        <<",\"root_functions_returned\":"<<boot.content.actor_script.root_functions_returned
        <<",\"control_blocks_entered\":"<<boot.content.actor_script.control_blocks_entered
        <<",\"control_blocks_closed\":"<<boot.content.actor_script.control_blocks_closed
        <<",\"active_control_blocks\":"<<boot.content.actor_script.active_control_blocks
        <<",\"switches\":"<<boot.content.actor_script.switches
        <<",\"native_budget_yields\":"<<boot.content.actor_script.native_budget_yields
        <<",\"waiting_for_event\":"<<boolean(boot.content.actor_script.waiting_for_event)
        <<",\"event_signals\":"<<boot.content.actor_script.event_signals
        <<",\"event_repeat_signals\":"<<boot.content.actor_script.event_repeat_signals
        <<",\"source_timestamp_writes\":"<<boot.content.actor_script.source_timestamp_writes
        <<",\"source_deadline_writes\":"<<boot.content.actor_script.source_deadline_writes
        <<",\"waiting_deadline\":"<<boolean(boot.content.actor_script.waiting_deadline)
        <<",\"opcode41_slice_policy\":\"NativeTimestampSliceAdapter;NotSourceBudgetClear\""
        <<",\"random_selections\":"<<boot.content.actor_script.random_selections<<",\"selected_value_word\":"<<boot.content.actor_script.selected_value_word
        <<",\"message_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.message.data()),boot.content.actor_script.message.size()))
        <<",\"error\":"<<JsonString(boot.content.actor_script.error)
        <<",\"builtin_calls\":"<<boot.content.actor_script.builtin_calls<<",\"game_service_calls\":"<<boot.content.actor_script.game_service_calls
        <<",\"rng17\":{\"calls\":"<<boot.content.actor_script.rng17_calls<<",\"pc\":"<<boot.content.actor_script.rng17_pc
        <<",\"bound\":"<<boot.content.actor_script.rng17_bound<<",\"raw\":"<<boot.content.actor_script.rng17_raw<<",\"result_word\":"<<boot.content.actor_script.rng17_result_word<<"}"
        <<",\"actor_request22\":{\"calls\":"<<boot.content.actor_script.actor22_calls<<",\"pc\":"<<boot.content.actor_script.actor22_pc
        <<",\"command_word\":"<<boot.content.actor_script.actor22_command_word<<",\"parameter_word\":"<<boot.content.actor_script.actor22_parameter_word<<"}"
        <<",\"dictionary_game_reads\":"<<boot.content.actor_script.dictionary_game_reads<<",\"dictionary_game_writes\":"<<boot.content.actor_script.dictionary_game_writes
        <<",\"array_query_calls\":"<<boot.content.actor_script.array_query_calls<<",\"array_query_matches\":"<<boot.content.actor_script.array_query_matches<<",\"array_actions\":"<<boot.content.actor_script.array_actions
        <<",\"optional_fields_policy\":\"NativeNewProfileAbsent;OriginalProducersUnrecovered\""
        <<",\"last_normalized_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.last_normalized.data()),boot.content.actor_script.last_normalized.size()))
        <<",\"source_flag2A980\":"<<boolean(boot.content.actor_script.source_flag2A980)
        <<",\"scene_flag_queries\":"<<boot.content.actor_script.scene_flag_queries
        <<",\"actor_flag_queries\":"<<boot.content.actor_script.actor_flag_queries
        <<",\"scene_period_queries\":"<<boot.content.actor_script.scene_period_queries
        <<",\"scene_period_clock_word\":"<<boot.content.actor_script.scene_period_clock_word
        <<",\"scene_period_result_word\":"<<boot.content.actor_script.scene_period_result_word
        <<",\"position_result_word\":"<<boot.content.actor_script.position_result_word
        <<",\"game28_queries\":"<<boot.content.actor_script.game28_queries<<",\"game28_result_word\":"<<boot.content.actor_script.game28_result_word
        <<",\"camera_setter_returns\":"<<boot.content.actor_script.camera_setter_returns<<",\"camera_setter_result_word\":"<<boot.content.actor_script.camera_setter_result_word
        <<",\"local_config_writes\":"<<boot.content.actor_script.local_config_writes
        <<",\"message_local_words\":["<<boot.content.actor_script.message_local_words[0]<<','<<boot.content.actor_script.message_local_words[1]<<','<<boot.content.actor_script.message_local_words[2]<<','<<boot.content.actor_script.message_local_words[3]<<','<<boot.content.actor_script.message_local_words[4]<<']'
        <<",\"message_acknowledgements\":"<<boot.content.actor_script.message_acknowledgements
        <<",\"awaiting_choice\":"<<boolean(boot.content.actor_script.awaiting_choice)
        <<",\"awaiting_input\":"<<boolean(boot.content.actor_script.awaiting_input)
        <<",\"input_requests\":"<<boot.content.actor_script.input_requests<<",\"input_completions\":"<<boot.content.actor_script.input_completions
        <<",\"input_mode\":"<<boot.content.actor_script.input_mode<<",\"input_timed_out\":"<<boolean(boot.content.actor_script.input_timed_out)
        <<",\"input_initial_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.input_initial.data()),boot.content.actor_script.input_initial.size()))
        <<",\"input_text_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.input_text.data()),boot.content.actor_script.input_text.size()))
        <<",\"input_auxiliary_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.input_auxiliary.data()),boot.content.actor_script.input_auxiliary.size()))
        <<",\"input_remaining\":"<<boot.content.actor_input_remaining<<",\"input_active_updates\":"<<boot.content.actor_input_active_updates
        <<",\"source_main_input_suppressed48\":"<<boolean(boot.content.actor_script.source_main_input_suppressed48)
        <<",\"choice_requests\":"<<boot.content.actor_script.choice_requests<<",\"choice_completions\":"<<boot.content.actor_script.choice_completions
        <<",\"choice_result_word\":"<<boot.content.actor_script.choice_result_word
        <<",\"b4_calls\":"<<boot.content.actor_script.b4_calls<<",\"b4_operand_word\":"<<boot.content.actor_script.b4_operand_word<<",\"b4_result_word\":"<<boot.content.actor_script.b4_result_word
        <<",\"e2_calls\":"<<boot.content.actor_script.e2_calls<<",\"e2_left_word\":"<<boot.content.actor_script.e2_left_word<<",\"e2_right_word\":"<<boot.content.actor_script.e2_right_word<<",\"e2_result_word\":"<<boot.content.actor_script.e2_result_word
        <<",\"d0_calls\":"<<boot.content.actor_script.d0_calls<<",\"d0_left_word\":"<<boot.content.actor_script.d0_left_word<<",\"d0_right_word\":"<<boot.content.actor_script.d0_right_word<<",\"d0_result_word\":"<<boot.content.actor_script.d0_result_word
        <<",\"native_reserved_local_nil\":"<<(boot.content.actor_script.native_reserved_local_nil?"true":"false");
    const auto write_word_row=[&](const dokodemo::recovered::GameWordRowObservation& word_row){
    out<<"{\"valid\":"<<boolean(word_row.valid)<<",\"generation\":"<<word_row.generation<<",\"run_id\":"<<word_row.run_id
       <<",\"row\":"<<word_row.row<<",\"table\":"<<word_row.table<<",\"index\":"<<word_row.index<<",\"global_header_word\":"<<word_row.global_header_word
       <<",\"record_header_word\":"<<word_row.record_header_word<<",\"error_code\":"<<word_row.error_code<<",\"code_sha256\":"<<JsonString(std::string(word_row.code_sha256.data()))
       <<",\"capture_policy\":\"NineteenTypedCells;StringLengthSHA256Only;NoReferenceOwnerLease\",\"fields\":[";
    for(size_t i=0;i<word_row.fields.size();++i){if(i)out<<',';WriteVmValueWitness(out,word_row.fields[i]);}out<<"]}";};
    out<<",\"candidate_word_row\":";write_word_row(boot.content.actor_script.candidate_word_row);
    out<<",\"word_consumer_row\":";write_word_row(boot.content.actor_script.word_consumer_row);
    out<<",\"word_clear_writes\":"<<boot.content.actor_script.word_clear_writes<<",\"word_clear_row\":";write_word_row(boot.content.actor_script.word_clear_row);
    out<<",\"word_selection_returns\":"<<boot.content.actor_script.word_selection_returns<<",\"word_selection_return\":";WriteNormalReturnWitness(out,boot.content.actor_script.word_selection_return);
    out<<",\"word_accepted_return\":";WriteNormalReturnWitness(out,boot.content.actor_script.word_accepted_return);
    out<<",\"word_accepted_row\":";write_word_row(boot.content.actor_script.word_accepted_row);
    out<<",\"word_consumer_source_pc\":"<<boot.content.actor_script.word_consumer_source_pc
       <<",\"word_consumer_source_entry\":"<<boot.content.actor_script.word_consumer_source_entry
       <<",\"word_consumer_source_frame_id\":"<<boot.content.actor_script.word_consumer_source_frame_id;
    out<<",\"d5_calls\":"<<boot.content.actor_script.d5_calls<<",\"d5_first_pc\":"<<boot.content.actor_script.d5_first_pc
       <<",\"d5_first_left_word\":"<<boot.content.actor_script.d5_first_left_word<<",\"d5_first_right_word\":"<<boot.content.actor_script.d5_first_right_word
       <<",\"d5_first_result_word\":"<<boot.content.actor_script.d5_first_result_word;
    out<<",\"op55_calls\":"<<boot.content.actor_script.op55_calls<<",\"op55_first_pc\":"<<boot.content.actor_script.op55_first_pc<<",\"op55_first_before\":"<<boot.content.actor_script.op55_first_before<<",\"op55_first_after\":"<<boot.content.actor_script.op55_first_after<<",\"op55_first_table\":"<<boot.content.actor_script.op55_first_table<<",\"op55_first_index\":"<<boot.content.actor_script.op55_first_index;
    out<<",\"ctx_reference_branches\":"<<boot.content.actor_script.ctx_reference_branches
       <<",\"ctx_reference_first_pc\":"<<boot.content.actor_script.ctx_reference_first_pc
       <<",\"ctx_reference_first_index\":"<<boot.content.actor_script.ctx_reference_first_index
       <<",\"ctx_reference_policy\":\"NativeOwnedCtx28ReferencePresence;NoPSPAddressOrOtherDomainClaim\"";
    out<<",\"string_length53_calls\":"<<boot.content.actor_script.string_length53_calls<<",\"string_length53_pc\":"<<boot.content.actor_script.string_length53_pc
       <<",\"string_length53_extended_calls\":"<<boot.content.actor_script.string_length53_extended_calls
       <<",\"string_length53_extended_pc\":"<<boot.content.actor_script.string_length53_extended_pc
       <<",\"string_length53_extended_result\":"<<boot.content.actor_script.string_length53_extended_result
       <<",\"string_length53_extended_input_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.string_length53_extended_input.data()),boot.content.actor_script.string_length53_extended_input.size()))
       <<",\"string_length53_result_word\":"<<boot.content.actor_script.string_length53_result_word<<",\"string_length53_input_cp932_hex\":"
       <<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.string_length53_input.data()),boot.content.actor_script.string_length53_input.size()));
    out<<",\"string_prefix71_calls\":"<<boot.content.actor_script.string_prefix71_calls<<",\"string_prefix71_pc\":"<<boot.content.actor_script.string_prefix71_pc
       <<",\"string_prefix71_result_word\":"<<boot.content.actor_script.string_prefix71_result_word<<",\"string_prefix71_result_kind\":"<<boot.content.actor_script.string_prefix71_result_kind
       <<",\"string_prefix71_input_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.string_prefix71_input.data()),boot.content.actor_script.string_prefix71_input.size()));
    const auto& note=boot.content.notebook;
    const auto& nr=boot.content.notebook_observed;
    out<<",\"notebook_slot0_after_valid\":"<<boolean(boot.content.actor_script.notebook_after.slot0.valid)
       <<",\"notebook_slot0_after_sha256\":"<<JsonString(StartupHexBytes(boot.content.actor_script.notebook_after.slot0.sha.data(),boot.content.actor_script.notebook_after.slot0.sha.size()));
    out<<",\"notebook_registrations\":"<<boot.content.actor_script.notebook_registrations<<",\"notebook_slot0_stores_dropped\":"<<boot.content.actor_script.notebook_slot0_stores_dropped<<",\"notebook_slot0_stores\":[";
    {bool first=true;for(const auto& x:boot.content.actor_script.notebook_slot0_stores){if(!first)out<<',';first=false;
       out<<"{\"field\":"<<x.field<<",\"pc\":"<<x.pc<<",\"entry\":"<<x.entry<<",\"opcode\":"<<x.opcode<<",\"table\":"<<x.table<<",\"index\":"<<x.index
          <<",\"executed\":"<<x.executed<<",\"frame\":"<<x.frame<<",\"run_id\":"<<x.run_id<<",\"generation\":"<<x.generation
          <<",\"row_index\":"<<x.row_index<<",\"row_header\":"<<x.row_header<<",\"registered_table\":"<<x.registered_table<<",\"registered_index\":"<<x.registered_index
          <<",\"registration\":"<<x.registration<<",\"value\":";WriteVmValueWitness(out,x.value);out<<'}';}}out<<']';
    {const auto& p=nr.slot0;out<<",\"notebook_slot0_probe\":{\"valid\":"<<boolean(p.valid)<<",\"error\":"<<p.error<<",\"index\":"<<p.index
       <<",\"generation\":"<<p.script_generation<<",\"run_id\":"<<p.run_id<<",\"row_table\":"<<p.row_table<<",\"row_index\":"<<p.row_index
       <<",\"row_header\":"<<p.row_header<<",\"cells\":"<<p.cells<<",\"sha256\":"<<JsonString(StartupHexBytes(p.sha.data(),p.sha.size()))<<",\"first\":[";
       for(size_t i=0;i<p.first.size();++i){if(i)out<<',';WriteVmValueWitness(out,p.first[i]);}out<<"]}";}
    out<<",\"notebook\":{\"policy\":\"NativeTypedGAME11ArrayReference;ActualEmptyOnly;NativeSharedTransition131F0;NativeDeferredPlus4;NativeActualClosedNotebookWake\""
       <<",\"live\":"<<boolean(note.live)<<",\"visible\":"<<boolean(note.visible)<<",\"request9\":"<<boolean(note.request9)<<",\"owner46C\":"<<boolean(note.owner46C)
       <<",\"generation\":"<<note.generation<<",\"retired_generation\":"<<note.retired_generation<<",\"parent_generation\":"<<note.lease.parent_generation
       <<",\"phase\":"<<note.phase<<",\"state23C\":"<<note.state23C<<",\"input_epoch\":"<<note.input_epoch<<",\"last_sequence\":"<<note.last_sequence
       <<",\"state26C\":"<<note.state26C<<",\"ordinary270\":"<<note.ordinary270<<",\"requested274\":"<<note.requested274<<",\"variant278\":"<<note.variant278
       <<",\"preview_generation\":"<<note.preview_generation<<",\"input_boundaries\":"<<note.input_boundaries
       <<",\"preview_requests\":"<<note.preview_requests<<",\"preview_opens\":"<<note.preview_opens<<",\"preview_back_requests\":"<<note.preview_back_requests<<",\"preview_returns\":"<<note.preview_returns
       <<",\"preview_draws\":"<<note.preview_draws<<",\"preview_glyphs\":"<<note.preview_glyphs<<",\"preview_callback_mask\":"<<note.preview_callback_mask
       <<",\"preview_emissions\":"<<note.preview_emissions<<",\"preview_empty_emissions\":"<<note.preview_empty_emissions<<",\"preview_guarded_callbacks\":"<<note.preview_guarded_callbacks
       <<",\"producer_returns\":"<<note.producer_returns<<",\"rng_calls\":"<<note.rng_calls<<",\"rng_raw\":"<<note.rng_raw<<",\"rng_seed_before\":"<<note.rng_seed_before<<",\"rng_seed_after\":"<<note.rng_seed_after
       <<",\"preview_frame\":"<<note.preview_frame<<",\"preview_record_valid\":"<<boolean(note.preview_record.valid)
       <<",\"preview_record_sha256\":"<<JsonString(StartupHexBytes(note.preview_record.sha.data(),note.preview_record.sha.size()))
       <<",\"self_before_valid\":"<<boolean(boot.content.actor_script.notebook_self_before.valid)
       <<",\"self_before_sha256\":"<<JsonString(StartupHexBytes(boot.content.actor_script.notebook_self_before.sha.data(),boot.content.actor_script.notebook_self_before.sha.size()))
       <<",\"self_after_valid\":"<<boolean(boot.content.actor_script.notebook_self_after.valid)
       <<",\"self_after_sha256\":"<<JsonString(StartupHexBytes(boot.content.actor_script.notebook_self_after.sha.data(),boot.content.actor_script.notebook_self_after.sha.size()))
       <<",\"selector90\":"<<note.preview.selector90<<",\"year94\":"<<note.preview.year94<<",\"month98\":"<<note.preview.month98<<",\"day9C\":"<<note.preview.day9C
       <<",\"valueA0\":"<<note.preview.valueA0<<",\"valueA4\":"<<note.preview.valueA4<<",\"valueA8\":"<<note.preview.valueA8
       <<",\"loader498\":"<<note.loader498<<",\"loader469\":"<<note.loader469<<",\"requests\":"<<note.requests<<",\"opens\":"<<note.opens
       <<",\"close_requests\":"<<note.close_requests<<",\"close_completions\":"<<note.close_completions<<",\"polls\":"<<note.polls
       <<",\"retirement_requests\":"<<note.retirement_requests<<",\"retirements\":"<<note.retirements<<",\"retirement_pending\":"<<boolean(note.retirement_pending)
       <<",\"draw_frames\":"<<note.draw_frames<<",\"quads\":"<<note.quads<<",\"font_resets\":"<<note.font_resets<<",\"font_flushes\":"<<note.font_flushes
       <<",\"callback_reaches\":"<<note.callback_reaches<<",\"rejected\":"<<note.rejected<<",\"unsupported\":"<<note.unsupported<<",\"error\":"<<JsonString(note.error)
       <<",\"callback_empty_returns\":"<<note.callback_empty_returns<<",\"text_owner\":"<<boolean(note.text_owner)
       <<",\"text_capacity\":"<<note.text_capacity<<",\"text_used\":"<<note.text_used<<",\"text_setup_stage\":"<<note.text_setup_stage
       <<",\"text_scale_x\":"<<note.text_scale_x<<",\"text_scale_y\":"<<note.text_scale_y<<",\"text_flush_selector\":"<<note.text_flush_selector
       <<",\"scissor_top\":"<<note.scissor_top<<",\"scissor_bottom\":"<<note.scissor_bottom
       <<",\"observed_valid\":"<<boolean(nr.valid)<<",\"records\":"<<nr.records<<",\"populated\":"<<nr.populated<<",\"cells\":"<<nr.cells
       <<",\"root_table\":"<<nr.root_table<<",\"root_index\":"<<nr.root_index<<",\"root_header\":"<<nr.root_header
       <<",\"run_id\":"<<nr.run_id<<",\"script_generation\":"<<nr.script_generation<<",\"code_sha256\":"<<JsonString(nr.code_sha.data())
       <<",\"sha256\":"<<JsonString(StartupHexBytes(nr.sha.data(),nr.sha.size()))
       <<",\"host_requests\":"<<boot.content.main_menu_notebook_requests<<",\"host_completions\":"<<boot.content.main_menu_notebook_completions
       <<",\"after_valid\":"<<boolean(boot.content.actor_script.notebook_after.valid)
       <<",\"after_sha256\":"<<JsonString(StartupHexBytes(boot.content.actor_script.notebook_after.sha.data(),boot.content.actor_script.notebook_after.sha.size()))
       <<",\"source469\":"<<boolean(boot.content.source_loader469)<<",\"source469_resource\":"<<boot.content.source469_resource
       <<",\"source469_generation\":"<<boot.content.source469_generation<<",\"source469_poll\":"<<boot.content.source469_poll<<'}';
    const auto& menu=boot.content.main_menu;
    out<<",\"main_menu\":{\"policy\":\"WindowsAvailableMainMenu;StableTransition0Or2;BorrowExistingAvailable;No469ClearOrTokenConsume;FullModeInventoryNotebookOrCancel;NativeSourceZeroMenuBorrow;NativeSteadyHighlight\""
       <<",\"inventory_requests\":"<<boot.content.main_menu_inventory_requests<<",\"inventory_completions\":"<<boot.content.main_menu_inventory_completions<<",\"consumed_script_requests\":"<<boot.content.consumed_script_inventory_requests<<",\"request_phase\":"<<boot.content.main_menu_inventory_request_phase<<",\"request498\":"<<boot.content.main_menu_inventory_request498<<",\"request_clock\":"<<boot.content.main_menu_inventory_request_clock<<",\"child_clock\":"<<boot.content.main_menu_inventory_child_clock<<",\"retire_clock\":"<<boot.content.main_menu_inventory_retire_clock<<",\"child_ready\":"<<boolean(boot.content.main_menu_child_ready)<<",\"phase\":"<<boot.content.main_menu_phase<<",\"returns\":"<<boot.content.main_menu_returns<<",\"input_epoch\":"<<boot.content.main_menu_input_epoch
       <<",\"available\":"<<(boot.content.main_menu_available?"true":"false")<<",\"original_right_blocked\":"<<(boot.content.main_menu_source_right_blocked?"true":"false")
       <<",\"live\":"<<(menu.live?"true":"false")<<",\"generation\":"<<menu.generation<<",\"state78\":"<<menu.state78<<",\"full\":"<<(menu.full?"true":"false")
       <<",\"opens\":"<<menu.opens<<",\"cancels\":"<<menu.cancels<<",\"closed\":"<<menu.closed<<",\"rejected\":"<<menu.rejected<<",\"unsupported\":"<<menu.unsupported
       <<",\"render_live\":"<<boolean(menu.render_live)<<",\"render_retirements\":"<<menu.render_retirements
       <<",\"substate7C\":"<<menu.substate7C<<",\"other_profiles\":"<<boolean(menu.other_profiles)<<",\"notebook_enabled\":"<<boolean(menu.notebook_enabled)<<",\"row\":"<<menu.row<<",\"column\":"<<menu.column<<",\"result\":"<<menu.result<<",\"frame\":"<<menu.frame<<",\"clip\":"<<menu.clip<<",\"mode\":"<<menu.mode
       <<",\"draw_frames\":"<<menu.draw_frames<<",\"quads\":"<<menu.quads<<",\"script_generation\":"<<menu.lease.script_generation<<",\"run_id\":"<<menu.lease.run_id
       <<",\"available_owner\":"<<menu.lease.available_owner<<",\"available_generation\":"<<menu.lease.available_generation<<",\"message_context\":"<<menu.lease.message_context<<",\"lease_epoch\":"<<menu.lease.input_epoch<<'}';
    out<<",\"manager_flag68\":"<<(boot.content.actor_script.manager_flag68?"true":"false");
    const auto menu_callback=[&](const char* name,const dokodemo::recovered::GameMenuCallbackSnapshot& m){
        out<<",\""<<name<<"\":{\"pending\":"<<(m.pending?"true":"false")<<",\"returned\":"<<(m.returned?"true":"false")<<",\"failed\":"<<(m.failed?"true":"false")
           <<",\"enabled_before\":"<<boolean(m.enabled_before)<<",\"enabled_after\":"<<boolean(m.enabled_after)<<",\"event\":"<<m.event<<",\"generation\":"<<m.menu_generation<<",\"receipt\":"<<m.receipt<<",\"callback_frame\":"<<m.callback_frame
           <<",\"flag_before\":"<<m.flag_before<<",\"flag_after\":"<<m.flag_after<<",\"kind_before\":"<<unsigned(m.kind_before)<<",\"kind_after\":"<<unsigned(m.kind_after)
           <<",\"suspended\":";WriteVmStateWitness(out,m.suspended);out<<",\"restored\":";WriteVmStateWitness(out,m.restored);out<<'}';
    };
    menu_callback("menu_open_callback",boot.content.actor_script.menu_open_return);
    menu_callback("menu_close_callback",boot.content.actor_script.menu_close_return);
    menu_callback("menu_pause_callback",boot.content.actor_script.menu_pause_return);
    const auto inventory_rows=[&](const char* name,const dokodemo::recovered::GameInventoryRowsObservation& v){
        out<<",\""<<name<<"\":{\"valid\":"<<boolean(v.valid)<<",\"generation\":"<<v.generation<<",\"run_id\":"<<v.run_id
           <<",\"table\":"<<v.table<<",\"index\":"<<v.index<<",\"header\":"<<v.header<<",\"rows\":"<<v.rows<<",\"cells\":"<<v.cells
           <<",\"valid_items\":"<<v.valid_items<<",\"code_sha256\":"<<JsonString(v.code_sha256.data())<<",\"sha256\":"<<JsonString(StartupHexBytes(v.sha256.data(),v.sha256.size()))<<'}';
    };
    inventory_rows("menu_inventory_before",boot.content.actor_script.menu_inventory_before);
    inventory_rows("menu_inventory_after",boot.content.actor_script.menu_inventory_after);
    inventory_rows("menu_inventory_verified",boot.content.actor_script.menu_inventory_verified);
    const auto& mark=boot.content.actor_script.menu_mark;
    out<<",\"menu_mark\":{\"live\":"<<boolean(mark.live)<<",\"completions\":"<<mark.completions
       <<",\"event\":"<<mark.event<<",\"row\":"<<mark.row<<",\"receipt\":"<<mark.receipt<<",\"store_pc\":"<<mark.store_pc
       <<",\"initial_sha256\":"<<JsonString(StartupHexBytes(mark.initial_sha256.data(),mark.initial_sha256.size()))
       <<",\"verified_sha256\":"<<JsonString(StartupHexBytes(mark.verified_sha256.data(),mark.verified_sha256.size()))<<'}';
    const auto& placement=boot.content.actor_script.menu_placement;
    out<<",\"menu_placement\":{\"bound\":"<<boolean(placement.bound)<<",\"pending\":"<<boolean(placement.pending)<<",\"returned\":"<<boolean(placement.returned)
       <<",\"flag_returned\":"<<boolean(placement.flag_returned)<<",\"callback_returned\":"<<boolean(placement.callback_returned)<<",\"event\":"<<placement.event<<",\"row\":"<<placement.row
       <<",\"source_return_pc\":"<<placement.source_return_pc<<",\"source_return_executed\":"<<placement.source_return_executed<<",\"receipt\":"<<placement.receipt
       <<",\"stores\":"<<placement.stores<<",\"auxiliary_stores\":"<<placement.auxiliary_stores<<",\"services\":"<<placement.services<<",\"completions\":"<<placement.completions<<",\"source_stores\":[";
    {bool first=true;for(const auto& x:boot.content.actor_script.menu_source_stores){if(!first)out<<',';first=false;
        out<<"{\"row\":"<<x.row<<",\"field\":"<<x.field<<",\"pc\":"<<x.pc<<",\"entry\":"<<x.entry<<",\"word\":"<<x.word<<",\"executed\":"<<x.executed<<'}';}}
    out<<"],\"model_effects\":[";
    {bool first=true;for(const auto& x:boot.content.actor_script.menu_model_effects){if(!first)out<<',';first=false;
        out<<"{\"service\":"<<x.service<<",\"pc\":"<<x.pc<<",\"entry\":"<<x.entry<<",\"executed\":"<<x.executed<<",\"receipt\":"<<x.receipt<<",\"frame\":"<<x.frame
           <<",\"result_word\":"<<x.result_word<<",\"result_stored\":"<<boolean(x.result_stored)<<",\"row_table\":"<<x.row_table<<",\"row_index\":"<<x.row_index
           <<",\"slot\":"<<x.slot<<",\"resource\":"<<x.resource<<",\"category\":"<<x.category<<",\"selector\":"<<x.selector<<",\"x\":"<<x.x<<",\"generation\":"<<x.generation
           <<",\"retirement_request\":"<<x.retirement_request<<",\"helper_return_executed\":"<<x.helper_return_executed<<",\"helper_return_pc\":"<<x.helper_return_pc
           <<",\"argument_kind\":"<<x.argument_kind<<",\"argument_word\":"<<x.argument_word<<",\"argument_table\":"<<x.argument_table<<",\"argument_index\":"<<x.argument_index
           <<",\"runtime_epoch\":"<<x.runtime_epoch<<",\"owner_phase\":"<<x.owner_phase<<",\"before_counts\":[";
        for(size_t i=0;i<x.before_counts.size();++i){if(i)out<<',';out<<x.before_counts[i];}out<<"],\"after_counts\":[";
        for(size_t i=0;i<x.after_counts.size();++i){if(i)out<<',';out<<x.after_counts[i];}out<<"],\"before\":";WriteRoomModelValue(out,x.before);out<<",\"after\":";WriteRoomModelValue(out,x.after);out<<'}';}}
    out<<"]}";
    const auto& controls=boot.content.actor_script.ordinary_controls;
    out<<",\"ordinary_controls\":{\"valid\":"<<(controls.valid?"true":"false")
       <<",\"generation\":"<<controls.generation<<",\"run_id\":"<<controls.run_id<<",\"error_code\":"<<controls.error_code<<",\"values\":[";
    for(size_t i=0;i<controls.values.size();++i){if(i)out<<',';const auto& v=controls.values[i];out<<"{\"kind\":"<<unsigned(v.kind)<<",\"word\":"<<v.word<<",\"table\":"<<v.table<<",\"index\":"<<v.index<<'}';}
    out<<"]}";
    const auto& kana=boot.content.actor_script;
    out<<",\"string_prefix71_result_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(kana.string_prefix71_result_text.data()),kana.string_prefix71_result_text.size()))
       <<",\"string_suffix72_calls\":"<<kana.string_suffix72_calls<<",\"string_suffix72_pc\":"<<kana.string_suffix72_pc
       <<",\"string_suffix72_result_word\":"<<kana.string_suffix72_result_word<<",\"string_suffix72_result_kind\":"<<kana.string_suffix72_result_kind
       <<",\"string_suffix72_input_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(kana.string_suffix72_input.data()),kana.string_suffix72_input.size()))
       <<",\"string_suffix72_result_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(kana.string_suffix72_result_text.data()),kana.string_suffix72_result_text.size()));
    out<<",\"nil_code0d_calls\":"<<boot.content.actor_script.nil_code0d_calls<<",\"nil_code0d_pc\":"<<boot.content.actor_script.nil_code0d_pc
       <<",\"nil_code0d_result_word\":"<<boot.content.actor_script.nil_code0d_result_word;
    out<<",\"ascii_class70_live\":"<<(boot.content.actor_script.ascii_class70_live?"true":"false")
       <<",\"ascii_class70_generation\":"<<boot.content.actor_script.ascii_class70_generation
       <<",\"ascii_class70_current_word\":"<<boot.content.actor_script.ascii_class70_current_word
       <<",\"ascii_class70_other_word\":"<<boot.content.actor_script.ascii_class70_other_word
       <<",\"ascii_class70_policy\":\"NativeScriptLifetimeOwner;Source76A50ConstructorDefaults;LatinKana229;NoLaterAliasWriterClaim\""
       <<",\"ascii_class70_calls\":"<<boot.content.actor_script.ascii_class70_calls<<",\"ascii_class70_pc\":"<<boot.content.actor_script.ascii_class70_pc
       <<",\"ascii_class70_result_word\":"<<boot.content.actor_script.ascii_class70_result_word
       <<",\"ascii_class70_input_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.ascii_class70_input.data()),boot.content.actor_script.ascii_class70_input.size()));
    {const auto& a=boot.content.actor_script.inventory_action;
        out<<",\"inventory_action\":{\"pending\":"<<boolean(a.pending)<<",\"returned\":"<<boolean(a.returned)<<",\"completed\":"<<boolean(a.completed)<<",\"failed\":"<<boolean(a.failed)
           <<",\"placement\":"<<boolean(a.placement)<<",\"proof_error\":"<<JsonString(a.proof_error)<<",\"menu_origin\":"<<boolean(a.menu_origin)<<",\"flag_store_count\":"<<a.flag_store_count<<",\"flag_store_pc\":"<<a.flag_store_pc<<",\"flag_store_word\":"<<a.flag_store_word
           <<",\"nil6_returns\":"<<a.nil6_returns<<",\"flag_returns\":"<<a.flag_returns<<",\"protected_writes\":"<<a.protected_writes
           <<",\"nil6_return_executed\":"<<a.nil6_return_executed<<",\"flag_store_executed\":"<<a.flag_store_executed<<",\"flag_return_executed\":"<<a.flag_return_executed
           <<",\"generation\":"<<a.generation<<",\"receipt\":"<<a.receipt<<",\"callback_frame\":"<<a.callback_frame<<",\"event\":"<<a.event<<",\"row\":"<<a.row<<",\"item_id\":"<<a.item_id
           <<",\"completions\":"<<a.completions<<",\"row_table\":"<<a.row_table<<",\"row_index\":"<<a.row_index<<",\"row_header\":"<<a.row_header;
        const auto values=[&](const char* name,const auto& v){out<<",\""<<name<<"\":["<<unsigned(v[0])<<','<<unsigned(v[1])<<','<<unsigned(v[2])<<']';};
        values("before_words",a.before_words);values("after_words",a.after_words);values("before_kinds",a.before_kinds);values("after_kinds",a.after_kinds);
        values("before_controls",a.before_controls);values("after_controls",a.after_controls);values("before_control_kinds",a.before_control_kinds);values("after_control_kinds",a.after_control_kinds);
        const auto row_values=[&](const char* name,const auto& v){out<<",\""<<name<<"\":[";for(uint32_t i=0;i<a.row_cells;++i){if(i)out<<',';out<<unsigned(v[i]);}out<<']';};
        out<<",\"row_cells\":"<<a.row_cells;row_values("before_row_words",a.before_row_words);row_values("after_row_words",a.after_row_words);row_values("before_row_kinds",a.before_row_kinds);row_values("after_row_kinds",a.after_row_kinds);
        out<<",\"gate_word\":"<<a.gate_word<<",\"definition\":{\"table\":"<<a.definition_table<<",\"index\":"<<a.definition_index<<",\"header\":"<<a.definition_header<<",\"mask_word\":"<<a.definition_mask<<",\"width_word\":"<<a.definition_width<<'}'
           <<",\"layout\":{\"table\":"<<a.layout_table<<",\"index\":"<<a.layout_index<<",\"header\":"<<a.layout_header<<",\"words\":["<<a.layout_words[0]<<','<<a.layout_words[1]<<"]}";
        out<<",\"model_services\":[";for(uint32_t i=0;i<a.model_service_count;++i){const auto& m=a.model_services[i];if(i)out<<',';
            out<<"{\"returned\":"<<boolean(m.returned)<<",\"service\":"<<m.service<<",\"pc\":"<<m.pc<<",\"entry\":"<<m.entry<<",\"receipt\":"<<m.receipt<<",\"frame\":"<<m.frame
               <<",\"slot\":"<<m.slot<<",\"resource\":"<<m.resource<<",\"category\":"<<m.category<<",\"selector\":"<<m.selector<<",\"x\":"<<m.x<<",\"result_word\":"<<m.result_word<<'}';}out<<']';
        out<<",\"suspended\":";WriteVmStateWitness(out,a.suspended);out<<",\"restored_in_run\":";WriteVmStateWitness(out,a.restored);out<<'}';}
    out<<",\"flow5_event\":";WriteFlow5Event(out,boot.content.actor_script.flow5_event);
    out<<",\"flow5_guards_valid\":"<<boolean(boot.content.actor_script.flow5_guards_valid)<<",\"flow5_guards\":[";
    for(size_t i=0;i<boot.content.actor_script.flow5_guards.size();++i){if(i)out<<',';WriteVmValueWitness(out,boot.content.actor_script.flow5_guards[i]);}out<<']';
    out<<",\"vm_current\":";WriteVmStateWitness(out,boot.content.actor_script.vm_current);
    out<<",\"game3_execution\":";WriteVmStateWitness(out,boot.content.actor_script.game3_execution);
    {const auto& o=boot.content.actor_script.owned_conversation;out<<",\"owned_conversation\":{\"policy\":\"AcceptedEvent0;SourceCommonOwner;OneSuccessor;NotPSP1CUnwind\"";
        out<<",\"observation_refusals\":"<<boot.content.actor_script.post_modal_observation_refusals;
        out<<",\"origin\":"<<unsigned(o.origin)<<",\"post_modal_generation\":"<<o.post_modal_generation;
        out<<",\"inventory_generation\":"<<o.inventory_generation<<",\"input_sequence\":"<<o.input_sequence;
        out<<",\"receipt\":"<<o.receipt;
        out<<",\"origin_activation\":"<<o.origin_activation;
        out<<",\"generation\":"<<o.generation;
        out<<",\"run_id\":"<<o.run_id;
        out<<",\"main_frame\":"<<o.main_frame;
        out<<",\"suspended_frame\":"<<o.suspended_frame;
        out<<",\"callback_frame\":"<<o.callback_frame;
        out<<",\"setter_frame\":"<<o.setter_frame;
        out<<",\"owner_frame\":"<<o.owner_frame;
        out<<",\"cleanup_frame\":"<<o.cleanup_frame;
        out<<",\"accepted_executed\":"<<o.accepted_executed;
        out<<",\"callback_executed\":"<<o.callback_executed;
        out<<",\"owner_executed\":"<<o.owner_executed;
        out<<",\"game3_executed\":"<<o.game3_executed;
        out<<",\"completed_executed\":"<<o.completed_executed;
        out<<",\"context_word\":"<<o.context_word;
        out<<",\"owner_return_pc\":"<<o.owner_return_pc;
        out<<",\"cleanup_return_pc\":"<<o.cleanup_return_pc;
        out<<",\"accepted\":"<<(o.accepted?"true":"false");
        out<<",\"failed\":"<<(o.failed?"true":"false");
        out<<",\"phase_written\":"<<(o.phase_written?"true":"false");
        out<<",\"callback_returned\":"<<(o.callback_returned?"true":"false");
        out<<",\"owner_started\":"<<(o.owner_started?"true":"false");
        out<<",\"owner_returned\":"<<(o.owner_returned?"true":"false");
        out<<",\"cleanup_started\":"<<(o.cleanup_started?"true":"false");
        out<<",\"game3\":"<<(o.game3?"true":"false");
        out<<",\"completed\":"<<(o.completed?"true":"false");
        out<<",\"main_live\":"<<(o.main_live?"true":"false");
        out<<",\"dispatch_ready\":"<<(o.dispatch_ready?"true":"false");
        out<<",\"owner_result\":";WriteVmValueWitness(out,o.owner_result);out<<"}";}

    out<<",\"normal_return\":";WriteNormalReturnWitness(out,boot.content.actor_script.normal_return);
    out<<",\"farewell_return\":";WriteNormalReturnWitness(out,boot.content.actor_script.farewell_return);
    out<<",\"b3_calls\":"<<boot.content.actor_script.b3_calls<<",\"b3_pc\":"<<boot.content.actor_script.b3_pc
       <<",\"b3_operand_kind\":"<<boot.content.actor_script.b3_operand_kind<<",\"b3_operand_word\":"<<boot.content.actor_script.b3_operand_word
       <<",\"b3_result_word\":"<<boot.content.actor_script.b3_result_word;
    out
        <<",\"choice_state\":"<<boot.content.actor_choice.state<<",\"choice_row\":"<<boot.content.actor_choice.row
        <<",\"choice_remaining\":"<<boot.content.actor_choice.remaining<<",\"choice_active_updates\":"<<boot.content.actor_choice.active_updates
        <<",\"choice_confirmations\":"<<boot.content.actor_choice.confirmations<<",\"choice_timed_out\":"<<boolean(boot.content.actor_choice.timed_out)
        <<",\"choice_context\":"<<boot.content.actor_choice.context<<",\"choice_rejected_inputs\":"<<boot.content.actor_choice.rejected_inputs
        <<",\"intro_caller_words\":["<<boot.content.actor_script.intro_caller_words[0]<<','<<boot.content.actor_script.intro_caller_words[1]<<','<<boot.content.actor_script.intro_caller_words[2]<<']'
        <<",\"clock_calls\":"<<boot.content.actor_script.clock_calls<<",\"last_clock_word\":"<<boot.content.actor_script.last_clock_word
        <<",\"intro_clock_origin\":"<<boot.content.actor_script.intro_clock_origin<<",\"intro_clock_expired\":"<<boot.content.actor_script.intro_clock_expired
        <<",\"scene_period\":"<<boot.content.actor_script.scene_period
        <<",\"last_service_argument_word\":"<<boot.content.actor_script.last_service_argument_word
        <<",\"last_service_result_word\":"<<boot.content.actor_script.last_service_result_word
        <<",\"last_service_returned\":"<<boolean(boot.content.actor_script.last_service_returned)
        <<",\"string_replace_calls\":"<<boot.content.actor_script.string_replace_calls
        <<",\"call_depth\":"<<boot.content.actor_script.call_depth
        <<",\"last_replacement_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(boot.content.actor_script.last_replacement.data()),boot.content.actor_script.last_replacement.size()))
        <<",\"last_service\":"<<boot.content.actor_script.last_service;
    out<<",\"choice_options_cp932_hex\":[";
    for(size_t i=0;i<boot.content.actor_script.choice_options.size();++i){
        if(i)out<<',';const auto& text=boot.content.actor_script.choice_options[i];
        out<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(text.data()),text.size()));
    }
    out<<"],\"message_history\":[";
    for(size_t i=0;i<boot.content.actor_script.message_history.size();++i){
        if(i)out<<',';const auto& text=boot.content.actor_script.message_history[i];
        out<<"{\"pc\":"<<boot.content.actor_script.message_pcs.at(i)<<",\"actor_command\":"<<boot.content.actor_script.message_actor_commands.at(i)
           <<",\"cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(text.data()),text.size()))<<'}';
    }
    out<<"],\"replacement_results_cp932_hex\":[";
    for(size_t i=0;i<boot.content.actor_script.replacement_results.size();++i){
        if(i)out<<',';const auto& text=boot.content.actor_script.replacement_results[i];
        out<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(text.data()),text.size()));
    }
    out<<"],\"service_ids\":[";
    for(size_t i=0;i<boot.content.actor_script.service_ids.size();++i){if(i)out<<',';out<<boot.content.actor_script.service_ids[i];}
    const auto& dictionary=boot.content.actor_script.dictionary;
    const auto& runtime=boot.content.actor_script.runtime;
    out<<"],\"runtime\":{\"live\":"<<boolean(runtime.live)
       <<",\"camera_follows_actor\":"<<boolean(runtime.camera_follows_actor)<<",\"camera_bindings\":"<<runtime.camera_bindings
       <<",\"camera_mode\":"<<runtime.camera_mode<<",\"transition_requests\":"<<runtime.transition_requests
       <<",\"loader_interval\":"<<runtime.loader_interval<<",\"active_command\":"<<runtime.active_command
       <<",\"command_complete\":"<<boolean(runtime.command_complete)<<",\"command_ready\":"<<boolean(runtime.command_ready)
       <<",\"command_repeat_ready258\":"<<boolean(runtime.command_repeat_ready258)
       <<",\"manager_request9\":"<<boolean(runtime.manager_request9)<<",\"manager_request8\":"<<boolean(runtime.manager_request8)
       <<",\"manager_camera_request4C\":"<<runtime.manager_camera_request4C<<",\"camera_requests\":"<<runtime.camera_requests
       <<",\"camera_requests_consumed\":"<<runtime.camera_requests_consumed<<",\"camera_manager_state28\":"<<runtime.camera_manager_state28
       <<",\"camera_transition_mode1C\":"<<runtime.camera_transition_mode1C<<",\"camera_complete2C\":"<<boolean(runtime.camera_complete2C)
       <<",\"camera_transition_updates\":"<<runtime.camera_transition_updates<<",\"camera_completions\":"<<runtime.camera_completions
       <<",\"camera_fov_degrees\":"<<runtime.camera_fov_degrees<<",\"camera_target_fov\":"<<runtime.camera_target_fov<<",\"camera_ratio\":"<<runtime.camera_ratio
       <<",\"camera_transition_policy\":\"SourceRequestConsumeThenThresholdCompletion;NativeFixedGameTickScalar2;NativeDirectSceneCurve;NativeFollowActualRootXYColdActorZ\""
       <<",\"command_loop_depth\":"<<runtime.command_loop_depth<<",\"command_loop_repeats\":"<<runtime.command_loop_repeats
       <<",\"intro_live\":"<<boolean(runtime.intro_live)<<",\"intro_requests\":"<<runtime.intro_requests
       <<",\"intro_closes\":"<<runtime.intro_closes
       <<",\"intro_state34\":"<<runtime.intro_state34<<",\"intro_state38\":"<<runtime.intro_state38
       <<",\"intro_nodes\":"<<runtime.intro_nodes<<",\"intro_quads\":"<<runtime.intro_quads<<",\"intro_updates\":"<<runtime.intro_updates
       <<",\"intro_controls\":[";
    for(size_t i=0;i<runtime.intro_controls.size();++i){if(i)out<<',';const auto& c=runtime.intro_controls[i];out<<"{\"clip\":"<<c.clip<<",\"frame\":"<<c.frame<<",\"mode\":"<<unsigned(c.mode)<<",\"active\":"<<unsigned(c.active)<<'}';}
    out<<']'
       <<",\"actor_model_live\":"<<boolean(runtime.actor_model_live)<<",\"actor_model_nodes\":"<<runtime.actor_model_nodes
       <<",\"actor_model_vertices\":"<<runtime.actor_model_vertices<<",\"actor_model_triangles\":"<<runtime.actor_model_triangles
       <<",\"actor_request_pending\":"<<boolean(runtime.actor_request_pending)
       <<",\"selected_extension_pending\":"<<boolean(runtime.selected_extension_pending)
       <<",\"event_requests\":"<<runtime.event_requests<<",\"event_pending_replacements\":"<<runtime.event_pending_replacements<<",\"requested_command\":"<<runtime.requested_command
       <<",\"asc_executor_implemented\":false,\"asc_partial_executor_implemented\":true,\"actor_model_drawn\":"<<boolean(runtime.actor_draw_frames>0)
       <<",\"command_blocked\":"<<boolean(runtime.command_blocked)<<",\"command_pc\":"<<runtime.command_pc
       <<",\"command_effects\":"<<runtime.command_effects<<",\"command_frontier\":"<<runtime.command_frontier
       <<",\"command_error\":"<<JsonString(runtime.command_error)
       <<",\"pending_logical_motion\":"<<runtime.pending_logical_motion
       <<",\"channel_admissions\":"<<runtime.channel_admissions<<",\"first_channel_motion\":"<<runtime.first_channel_motion
       <<",\"actor_draw_frames\":"<<runtime.actor_draw_frames<<",\"actor_pixel_writes\":"<<runtime.actor_pixel_writes
       <<",\"actor_visible_pixel_writes_last_frame\":"<<runtime.actor_visible_pixels<<",\"motion_samples\":"<<runtime.motion_samples
       <<",\"actor_control_ready\":"<<boolean(runtime.actor_control_ready)<<",\"command_wait_motion\":"<<boolean(runtime.command_wait_motion)
       <<",\"angular_pending\":"<<boolean(runtime.angular_pending)<<",\"angular_updates\":"<<runtime.angular_updates
       <<",\"angular_completions\":"<<runtime.angular_completions<<",\"command_completions\":"<<runtime.command_completions
       <<",\"asc176_ready\":"<<boolean(runtime.asc176_ready)<<",\"asc176_complete\":"<<boolean(runtime.asc176_complete)
       <<",\"motion123_admissions\":"<<runtime.motion123_admissions<<",\"motion123_completions\":"<<runtime.motion123_completions
       <<",\"scene_model_live\":"<<boolean(runtime.scene_model_live)<<",\"scene_controls\":"<<runtime.scene_controls
       <<",\"scene_clock_ready\":"<<boolean(runtime.scene_clock_ready)
       <<",\"manager_scene14\":"<<runtime.manager_scene14<<",\"scene_period_table52\":"<<runtime.scene_period_table52
       <<",\"region_owners_live\":"<<boolean(runtime.region_owners_live)
       <<",\"region_counts\":["<<runtime.region_counts[0]<<','<<runtime.region_counts[1]<<','<<runtime.region_counts[2]<<']'
       <<",\"position_queries\":"<<runtime.position_queries<<",\"position_fallbacks\":"<<runtime.position_fallbacks
       <<",\"source_flag2A97C\":"<<runtime.source_flag2A97C
       <<",\"position_result3C0\":"<<boolean(runtime.position_result3C0)
       <<",\"position3B0\":["<<runtime.position3B0[0]<<','<<runtime.position3B0[1]<<','<<runtime.position3B0[2]<<','<<runtime.position3B0[3]<<']'
       <<",\"season_controls\":"<<runtime.season_controls<<",\"light_on_control_present\":"<<boolean(runtime.light_on_control_present)
       <<",\"scene_light_nodes\":"<<runtime.scene_light_nodes<<",\"scene_light_color14\":"<<runtime.scene_light_color14
       <<",\"scene_light_color_writes\":"<<runtime.scene_light_color_writes
       <<",\"scene_light_scalar_writes\":"<<runtime.scene_light_scalar_writes<<",\"scene_light_vector_writes\":"<<runtime.scene_light_vector_writes
       <<",\"light_toggle_calls\":"<<runtime.light_toggle_calls<<",\"source_flag2A980\":"<<runtime.source_flag2A980
       <<",\"light_parameter_shader_implemented\":false,\"curve_policy\":\"NativeLinearForFiniteTangents;ExactScalarOutgoing7FC00000Step\""
       <<",\"scene_clock_policy\":\"CapturedNativeCivilCalendarForSeasonSeed;FreshNativeMinuteForGAME36;NoExtraTimezoneShift\""
       <<",\"movement_policy\":\"NativeMax256UnitsPerPhase;ActualRootDistanceUnder2;NativePointArrivalClearsMathMode\""
       <<",\"movement_pending\":"<<boolean(runtime.movement_pending)<<",\"movement_updates\":"<<runtime.movement_updates
       <<",\"movement_completions\":"<<runtime.movement_completions<<",\"last_completed_command\":"<<runtime.last_completed_command
       <<",\"movement_target\":["<<runtime.movement_target[0]<<','<<runtime.movement_target[1]<<']'
       <<",\"scene_pair_calls\":"<<runtime.scene_pair_calls<<",\"scene_pair3_calls\":"<<runtime.scene_pair3_calls
       <<",\"scene_frames\":"<<runtime.scene_frames<<",\"scene_last_pair\":["<<runtime.scene_last_pair[0]<<','<<runtime.scene_last_pair[1]<<']'
       <<",\"scene_nodes\":"<<runtime.scene_nodes<<",\"scene_vertices\":"<<runtime.scene_vertices<<",\"scene_triangles\":"<<runtime.scene_triangles
       <<",\"scene_particle_emitters\":"<<runtime.scene_particle_emitters<<",\"scene_secondary_textures\":"<<runtime.scene_secondary_textures
       <<",\"scene_deferred_tracks\":"<<runtime.scene_deferred_tracks<<",\"particle_emission_implemented\":false"
       <<",\"shading_policy\":\"NativeDiffuseOnly;SecondaryImagesOwnedUnshaded\""
       <<",\"actor_channel_policy\":\"TwoOwnedFullPoseChannels;NativeCrossfade;160UnitsPerFrame\""
       <<",\"angular_policy\":\"NativeRootRotationMax10Degrees;ActualOneDegreeThreshold\""
       <<",\"actor_root\":["<<runtime.actor_root[0]<<','<<runtime.actor_root[1]<<','<<runtime.actor_root[2]<<','<<runtime.actor_root[3]<<']'
       <<",\"channel_motion\":["<<runtime.channel_motion[0]<<','<<runtime.channel_motion[1]<<']'
       <<",\"channels\":[";
    for(size_t i=0;i<runtime.channels.size();++i){if(i)out<<',';const auto& c=runtime.channels[i];
        out<<"{\"state\":"<<c.state<<",\"time\":"<<c.time<<",\"sampled_time\":"<<c.sampled_time
           <<",\"duration\":"<<c.duration<<",\"loops\":"<<c.loops<<",\"samples\":"<<c.samples<<",\"loop\":"<<boolean(c.loop)<<'}';}
    out<<"],\"scene_playback\":[";
    for(size_t i=0;i<runtime.scene_playback.size();++i){if(i)out<<',';const auto& c=runtime.scene_playback[i];
        out<<"{\"state\":"<<c.state<<",\"time\":"<<c.time<<",\"duration\":"<<c.duration<<",\"samples\":"<<c.samples<<'}';}
    out<<"],\"season_playback\":[";
    for(size_t i=0;i<runtime.season_playback.size();++i){if(i)out<<',';const auto& c=runtime.season_playback[i];
        out<<"{\"present\":"<<boolean(runtime.season_present[i])<<",\"state\":"<<c.state<<",\"time\":"<<c.time
           <<",\"sampled_time\":"<<c.sampled_time<<",\"speed\":"<<c.speed<<",\"duration\":"<<c.duration<<",\"samples\":"<<c.samples<<'}';}
    const auto& lc=runtime.light_on_playback;
    out<<"],\"light_on_playback\":{\"state\":"<<lc.state<<",\"time\":"<<lc.time<<",\"samples\":"<<lc.samples<<'}';
    out<<",\"first_completed_commands\":[";
    for(size_t i=0;i<runtime.first_completed_commands.size();++i){if(i)out<<',';out<<runtime.first_completed_commands[i];}
    out<<"],\"effect_order\":[";
    for(size_t i=0;i<runtime.effect_order.size();++i){if(i)out<<',';out<<runtime.effect_order[i];}
    out<<']'
       <<",\"logical_xy\":["<<runtime.logical_xy[0]<<','<<runtime.logical_xy[1]<<']'
       <<",\"heading_known\":"<<boolean(runtime.heading_known)<<",\"logical_heading\":"<<runtime.logical_heading
       <<",\"actor_math_mode\":"<<runtime.actor_math_mode<<",\"actor_phases\":"<<runtime.actor_phases
       <<",\"source_field58\":"<<runtime.source_field58<<",\"command_flag25A\":"<<boolean(runtime.command_flag25A)
       <<",\"actor_dynamic_followpoint_producer_implemented\":false"
       <<",\"actor_position\":["<<runtime.actor_position[0]<<','<<runtime.actor_position[1]<<','<<runtime.actor_position[2]<<','<<runtime.actor_position[3]<<']'
       <<",\"native_camera_position\":["<<runtime.camera_position[0]<<','<<runtime.camera_position[1]<<','<<runtime.camera_position[2]<<','<<runtime.camera_position[3]<<']'
       <<",\"native_camera_look\":["<<runtime.camera_look[0]<<','<<runtime.camera_look[1]<<','<<runtime.camera_look[2]<<','<<runtime.camera_look[3]<<']'
       <<",\"source_field5C\":"<<runtime.source_field5C<<",\"source_field50\":"<<runtime.source_field50<<",\"source_field54\":"<<runtime.source_field54
       <<",\"model_slots\":"<<runtime.model_slots<<",\"model_nodes\":"<<runtime.model_nodes
       <<",\"model_vertices\":"<<runtime.model_vertices<<",\"model_triangles\":"<<runtime.model_triangles
       <<",\"motion_templates\":"<<runtime.motion_templates<<",\"model_frames\":"<<runtime.model_frames
       <<",\"model_pixel_writes\":"<<runtime.model_pixel_writes<<",\"visible_pixels\":"<<runtime.visible_pixels
       <<",\"last_model_slot\":"<<runtime.last_model_slot<<",\"last_model_path\":"<<JsonString(runtime.last_model_path)
       <<",\"model_publications\":"<<runtime.model_publications<<",\"model_updates\":"<<runtime.model_updates
       <<",\"owner_epoch\":"<<runtime.owner_epoch<<",\"model_asset_acquisitions\":"<<runtime.model_asset_acquisitions<<",\"model_clone_initializations\":"<<runtime.model_clone_initializations
       <<",\"model_retire_requests\":"<<runtime.model_retire_requests<<",\"model_retirements\":"<<runtime.model_retirements
       <<",\"room_models\":[";
    {bool first=true;for(size_t i=0;i<runtime.room_models.size();++i){const auto& m=runtime.room_models[i];if(!m.generation)continue;if(!first)out<<',';first=false;
        out<<"{\"slot\":"<<i<<",\"active\":"<<boolean(m.active)<<",\"retirement\":"<<m.retirement<<",\"generation\":"<<m.generation
           <<",\"resource\":"<<m.resource<<",\"category\":"<<m.category<<",\"selector\":"<<m.selector<<",\"source_x\":"<<m.source_x<<",\"anchor_node\":"<<m.anchor_node
           <<",\"asset_identity\":"<<m.asset_identity<<",\"clone_identity\":"<<m.clone_identity<<",\"draw_frames\":"<<m.draw_frames<<",\"draw_pixel_writes\":"<<m.draw_pixel_writes<<",\"native_root\":[";
        for(size_t j=0;j<m.native_root.size();++j){if(j)out<<',';out<<m.native_root[j];}out<<"]}";}}out<<']'
       <<",\"room_owner_phases\":"<<runtime.room_owner_phases<<",\"model_retirement_history_dropped\":"<<runtime.model_retirement_history_dropped
       <<",\"model_retirement_history\":[";
    for(size_t i=0;i<runtime.model_retirement_history.size();++i){const auto& r=runtime.model_retirement_history[i];if(i)out<<',';
       out<<"{\"request\":"<<r.request<<",\"generation\":"<<r.generation<<",\"slot\":"<<r.slot<<",\"resource\":"<<r.resource
          <<",\"requested_phase\":"<<r.requested_phase<<",\"disabled_phase\":"<<r.disabled_phase<<",\"destroyed_phase\":"<<r.destroyed_phase
          <<",\"nodes_at_disable\":"<<r.nodes_at_disable<<",\"motions_at_disable\":"<<r.motions_at_disable
          <<",\"assets_retained_at_disable\":"<<boolean(r.assets_retained_at_disable)<<",\"clone_retained_at_disable\":"<<boolean(r.clone_retained_at_disable)
          <<",\"asset_owner_expired\":"<<boolean(r.asset_owner_expired)<<",\"clone_owner_cleared\":"<<boolean(r.clone_owner_cleared)<<'}';}out<<']'
       <<",\"room_world_basis_policy\":\"NativeSceneAnchorLocalX;AuthoredSceneWorldTimesLocalSourceX;Selector0IdentityTimesLocalX;OriginalParent10ViewSpaceAndScheduleUnproved\""
       <<",\"camera_policy\":\"NativeFollowActualRootXY;ColdEyeOffset0Minus4400_980LookPlusYFov30Near100Far10400;SourceActor40ProducerUnrecovered\"}"
       <<",\"last_query_indices\":[";
    for(size_t i=0;i<boot.content.actor_script.last_query_indices.size();++i){if(i)out<<',';out<<boot.content.actor_script.last_query_indices[i];}
    out<<"],\"dictionary\":{\"imported\":"<<boolean(dictionary.imported)<<",\"source_live\":"<<boolean(dictionary.source_live)
        <<",\"closed\":"<<boolean(dictionary.closed)<<",\"records\":"<<dictionary.records<<",\"bytes\":"<<dictionary.bytes
        <<",\"copied_values\":"<<dictionary.copied_values<<",\"copied_strings\":"<<dictionary.copied_strings
        <<",\"destination_index\":"<<dictionary.destination_index<<",\"queries\":"<<dictionary.queries
        <<",\"first_written_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(dictionary.first_written.data()),dictionary.first_written.size()))
        <<",\"first_reading_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(dictionary.first_reading.data()),dictionary.first_reading.size()))<<",\"current_records\":[";
    for(size_t i=0;i<dictionary.current_records.size();++i){
        if(i)out<<',';const auto& row=dictionary.current_records[i];out<<"{\"index\":"<<row.index<<",\"numeric_fields0_5_6_7_8_17\":[";
        for(size_t j=0;j<row.numeric.size();++j){if(j)out<<',';out<<row.numeric[j];}
        out<<"],\"field14_word\":"<<row.fields14_15[0]<<",\"field15_word\":"<<row.fields14_15[1]<<",\"written_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(row.written.data()),row.written.size()))
           <<",\"reading_cp932_hex\":"<<JsonString(StartupHexBytes(reinterpret_cast<const uint8_t*>(row.reading.data()),row.reading.size()))<<'}';
    }
    out<<"]}},\"title_material_resources\":[";
    for(size_t i=0;i<boot.content.title_material_resources.size();++i) {
        if(i)out<<',';out<<JsonString(boot.content.title_material_resources[i]);
    }
    out<<"],\"autoload\":{\"phase\":" << int(boot.content.autoload.phase)
        << ",\"mode\":" << boot.content.autoload.mode << ",\"parameter_bytes\":" << boot.content.autoload.parameter_bytes
        << ",\"buffer_capacity\":" << boot.content.autoload.buffer_capacity
        << ",\"buffer_live\":" << boolean(boot.content.autoload.buffer_live)
        << ",\"source_fields_applied\":" << boolean(boot.content.autoload.source_fields_applied)
        << ",\"status\":" << JsonString(dokodemo::recovered::GameStorageCodeName(boot.content.autoload.status.code))
        << ",\"detail\":" << JsonString(boot.content.autoload.status.detail)
        << ",\"available\":" << boolean(boot.content.autoload.storage.available)
        << ",\"writable\":" << boolean(boot.content.autoload.storage.writable)
        << ",\"free_bytes\":" << boot.content.autoload.storage.free_bytes
        << ",\"read_bytes\":" << boot.content.autoload.storage.read_bytes
        << ",\"written_bytes\":" << boot.content.autoload.storage.written_bytes
        << ",\"load_calls\":" << boot.content.autoload.storage.load_calls
        << ",\"publish_calls\":" << boot.content.autoload.storage.publish_calls << "}"

        << ",\"scene_ui\":{\"live\":" << boolean(boot.content.scene_ui.live)
        << ",\"constructors_returned\":" << boolean(boot.content.scene_ui.constructors_returned)
        << ",\"scene1_returned\":" << boolean(boot.content.scene_ui.scene1_returned)
        << ",\"owners\":" << boot.content.scene_ui.owners << ",\"records\":" << boot.content.scene_ui.records
        << ",\"ui_resources\":" << boot.content.scene_ui.ui_resources << ",\"ui_nodes\":" << boot.content.scene_ui.ui_nodes
        << ",\"ui_clips\":" << boot.content.scene_ui.ui_clips << ",\"textures\":" << boot.content.scene_ui.textures
        << ",\"texture_pixels\":" << boot.content.scene_ui.texture_pixels
        << ",\"text_capacity_bytes\":" << boot.content.scene_ui.text_capacity_bytes
        << ",\"borrowed_dictionaries\":" << boot.content.scene_ui.borrowed_dictionaries
        << ",\"mutable_dictionaries\":" << boot.content.scene_ui.mutable_dictionaries
        << ",\"visible_roots\":" << boot.content.scene_ui.visible_roots << ",\"native_frames\":" << boot.content.scene_ui.native_frames
        << ",\"ui_render_implemented\":false,\"text_drawn\":" << boolean(boot.content.scene_ui.text_drawn) << ",\"mixer_implemented\":false"
        << ",\"storage_dialog\":" << boolean(boot.content.scene_ui.storage_dialog)
        << ",\"dialog_renderer\":\"NativeCenteredPanelAndSourceXftAdapter\""
        << ",\"confirmation_implemented\":true,\"awaiting_confirmation\":" << boolean(boot.content.scene_ui.awaiting_confirmation)
        << ",\"storage_error\":" << boot.content.scene_ui.storage_error << ",\"dialog_glyphs\":" << boot.content.scene_ui.dialog_glyphs
        << ",\"dialog_progress\":" << boot.content.scene_ui.dialog_progress
        << ",\"dialog_state\":" << boot.content.scene_ui.dialog_state
        << ",\"dialog_result\":" << boot.content.scene_ui.dialog_result
        << ",\"confirmations\":" << boot.content.scene_ui.confirmations
        << ",\"fade_state\":" << boot.content.scene_ui.transition.state
        << ",\"fade_radius\":" << boot.content.scene_ui.transition.radius
        << ",\"editor_label\":" << JsonString(boot.content.scene_ui.editor_label)
        << ",\"select_adapter\":"<<boolean(boot.content.scene_ui.select_adapter)
        << ",\"input32_text_panel\":"<<boolean(boot.content.scene_ui.input32_text_panel)<<",\"editor\":";
    WriteGameTextValue(out,boot.content.scene_ui.editor);out<<'}'
        << ",\"sound_bank\":{\"metadata_ready\":" << boolean(boot.content.sound_bank.metadata_ready)
        << ",\"mixer_ready\":false,\"image_bytes\":" << boot.content.sound_bank.image_bytes
        << ",\"chunks\":" << boot.content.sound_bank.chunks
        << ",\"native_metadata_slot\":" << boot.content.sound_bank.native_metadata_slot
        << ",\"pool_capacity\":" << boot.content.sound_bank.pool_capacity
        << ",\"playback_requests\":"<<boot.content.sound_bank.playback_requests<<",\"last_sound\":"<<boot.content.sound_bank.last_sound
        << ",\"last_channel\":"<<boot.content.sound_bank.last_channel<<",\"last_volume\":"<<boot.content.sound_bank.last_volume
        << ",\"last_playback_result\":"<<boot.content.sound_bank.last_playback_result
        << ",\"native_request_id\":"<<boot.content.sound_bank.native_request_id<<",\"native_queue_code\":"<<boot.content.sound_bank.native_queue_code<<",\"profile_gain\":"<<boot.content.sound_bank.profile_gain
        << ",\"finite_windows_output\":"<<boolean(boot.content.sound_bank.finite_windows_output)
        << ",\"regions\":" << boolean(boot.content.sound_bank.regions) << ",\"waves\":" << boolean(boot.content.sound_bank.waves)
        << ",\"sequences\":" << boolean(boot.content.sound_bank.sequences) << ",\"names\":" << boolean(boot.content.sound_bank.names) << '}'
        << ",\"direct_file_io\":{\"file_live\":" << boolean(boot.scene_resource_io.file_live)
        << ",\"file_bytes\":" << boot.scene_resource_io.file_bytes << ",\"read_calls\":" << boot.scene_resource_io.read_calls
        << ",\"transferred_bytes\":" << boot.scene_resource_io.transferred_bytes << ",\"live_allocations\":" << boot.scene_resource_io.live_allocations << '}'
        << ",\"font\":{\"ready\":" << boolean(boot.content.font.ready)
        << ",\"image_bytes\":" << boot.content.font.image_bytes << ",\"texture_count\":" << boot.content.font.texture_count
        << ",\"texture_bytes\":" << boot.content.font.texture_bytes << ",\"auxiliary_div8\":" << boot.content.font.auxiliary_div8
        << ",\"auxiliary_offset\":" << boot.content.font.auxiliary_offset
        << ",\"glyph_width\":" << boot.content.font.glyph_width << ",\"glyph_height\":" << boot.content.font.glyph_height
        << ",\"scale\":[" << boot.content.font.scale_x << ',' << boot.content.font.scale_y << ']'
        << ",\"spacing\":[" << boot.content.font.spacing_x << ',' << boot.content.font.spacing_y
        << "],\"rendered_game_text\":" << boolean(boot.content.scene_ui.text_drawn) << "},\"title_complete\":false,\"gameplay\":false}}";
}

void WriteAdhocValue(std::ostream& out,
    const dokodemo::recovered::AdhocPrivateDataSnapshot& s) {
    out << "{\"present\":" << (s.present ? "true" : "false");
    if (!s.present) { out << '}'; return; }
    out << ",\"owner\":" << s.owner << ",\"generation\":" << s.generation
        << ",\"serial\":" << s.serial << ",\"native_state_address\":" << s.native_state_address
        << ",\"source_segment\":" << s.source_segment << ",\"source_segment_va\":" << s.source_segment_va
        << ",\"source_file_offset\":" << s.source_file_offset << ",\"source_file_bytes\":" << s.source_file_bytes
        << ",\"private_bytes\":" << s.private_bytes << ",\"initial_source_known_bytes\":" << s.initial_source_known_bytes
        << ",\"known_bytes\":" << s.known_bytes << ",\"writes_count\":" << s.writes_count
        << ",\"initial_known_origin\":\"AdmittedUnrelocatedFileBytesCopiedByNativeInitializer\""
        << ",\"bytes_meaning_requires_known_one\":true"
        << ",\"bytes_hex\":" << JsonString(StartupHexBytes(s.bytes.data(), s.bytes.size()))
        << ",\"known_hex\":" << JsonString(StartupHexBytes(s.known.data(), s.known.size())) << '}';
}

void WriteAdhocAuthValue(std::ostream& out,
    const dokodemo::recovered::AdhocAuthPrivateDataSnapshot& s) {
    out << "{\"present\":" << (s.present ? "true" : "false");
    if (!s.present) { out << '}'; return; }
    out << ",\"owner\":" << s.owner << ",\"generation\":" << s.generation
        << ",\"serial\":" << s.serial << ",\"native_state_address\":" << s.native_state_address
        << ",\"source_segment\":" << s.source_segment << ",\"source_segment_va\":" << s.source_segment_va
        << ",\"source_file_offset\":" << s.source_file_offset << ",\"source_file_bytes\":" << s.source_file_bytes
        << ",\"private_bytes\":" << s.private_bytes << ",\"initial_source_known_bytes\":" << s.initial_source_known_bytes
        << ",\"known_bytes\":" << s.known_bytes << ",\"writes_count\":" << s.writes_count
        << ",\"initial_known_origin\":\"AdmittedUnrelocatedFileBytesCopiedByNativeInitializer\""
        << ",\"bytes_meaning_requires_known_one\":true"
        << ",\"bytes_hex\":" << JsonString(StartupHexBytes(s.bytes.data(), s.bytes.size()))
        << ",\"known_hex\":" << JsonString(StartupHexBytes(s.known.data(), s.known.size())) << '}';
}

void WriteMemabValue(std::ostream& out,
    const dokodemo::recovered::MemabInitializationSnapshot& s) {
    const auto boolean=[](bool v){return v?"true":"false";};
    out << "{\"present\":" << boolean(s.present);
    if(!s.present) {out << '}';return;}
    const auto number=[&](const char* name,auto value){out << ',' << JsonString(name) << ':' << value;};
    const auto flag=[&](const char* name,bool value){out << ',' << JsonString(name) << ':' << boolean(value);};
    const auto nullable=[&](const char* name,bool present,auto value){
        out << ',' << JsonString(name) << ':';if(present) out << value;else out << "null";
    };
    flag("entry_entered",s.entry_entered);flag("entry_returned",s.entry_returned);
    nullable("entry_result",s.entry_returned,s.entry_result);
    flag("host_abort_required",s.host_abort_required);flag("host_abort_entered",s.host_abort_entered);
    flag("host_aborted",s.host_aborted);flag("stop_domain_ready",s.stop_domain_ready);
    flag("source_stop_entered",s.source_stop_entered);flag("source_stop_returned",s.source_stop_returned);
    number("owner",s.owner);number("generation",s.generation);number("serial",s.serial);
    number("native_state_address",s.native_state_address);number("source_segment",s.source_segment);
    number("source_segment_va",s.source_segment_va);number("private_bytes",s.private_bytes);
    number("initial_source_known_bytes",s.initial_source_known_bytes);number("known_bytes",s.known_bytes);
    number("writes_count",s.writes_count);
    out << ",\"bytes_hex\":" << JsonString(StartupHexBytes(s.bytes.data(),s.bytes.size()))
        << ",\"known_hex\":" << JsonString(StartupHexBytes(s.known.data(),s.known.size()))
        << ",\"bytes_meaning_requires_known_one\":true,\"encoded_file_literal_hex\":"
        << JsonString(StartupHexBytes(s.encoded_file_literal.data(),s.encoded_file_literal.size()))
        << ",\"file_pointer_addends\":[" << s.file_pointer_addends[0] << ',' << s.file_pointer_addends[1]
        << "],\"file_pointer_values_relocated\":false,\"writes\":[";
    for(size_t i=0;i<(std::min)(size_t(s.writes_count),s.writes.size());++i) {
        if(i) out << ',';const auto& w=s.writes[i];
        out << "{\"ordinal\":" << w.ordinal << ",\"source_pc\":" << w.source_pc
            << ",\"offset\":" << w.offset << ",\"width\":4,\"value\":" << w.value
            << ",\"value_origin\":\"NativePlatform\"}";
    }
    out << ']';
    number("frontier_source_pc",s.frontier_source_pc);number("frontier_stub_va",s.frontier_stub_va);
    number("frontier_nid",s.frontier_nid);number("frontier_import_index",s.frontier_import_index);
    const auto& r=s.resources;
    out << ",\"resources\":{\"policy_abi\":" << JsonString(r.policy_abi?r.policy_abi:"")
        << ",\"original_firmware_equivalence_known\":false,\"key\":{\"module\":[";
    for(size_t i=0;i<r.key.module.size();++i){if(i)out << ',';out << r.key.module[i];}
    out << "],\"input\":[";
    for(size_t i=0;i<r.key.input.size();++i){if(i)out << ',';out << r.key.input[i];}
    out << "],\"state\":[";
    for(size_t i=0;i<r.key.state.size();++i){if(i)out << ',';out << r.key.state[i];}
    out << "]}";
    number("backend_owner",r.backend_owner);number("backend_generation",r.backend_generation);
    number("backend_serial",r.backend_serial);flag("issued",r.issued);flag("retired",r.retired);
    flag("input_pinned",r.input_pinned);flag("create_entered",r.create_entered);flag("create_returned",r.create_returned);
    flag("create_source_returned",r.create_source_returned);
    nullable("create_source_result",r.create_source_returned,r.create_source_result);
    nullable("create_error",r.create_returned,r.create_error);
    nullable("create_native_succeeded",r.create_returned,boolean(r.semaphore_handle!=0));
    nullable("semaphore_uid",r.create_returned,r.semaphore_uid);
    nullable("semaphore_handle",r.create_returned,r.semaphore_handle);
    flag("semaphore_live",r.semaphore_live);flag("semaphore_closed",r.semaphore_closed);
    number("create_attempts",r.create_attempts);number("create_returns",r.create_returns);
    number("semaphore_closes",r.semaphore_closes);number("delete_attempts",r.delete_attempts);
    number("delete_returns",r.delete_returns);
    // These are persistent SOURCE Delete observations; host-only Close attempts
    // deliberately never overwrite them and do not make absent values numeric.
    nullable("delete_error",r.delete_source_returned,r.delete_error);
    nullable("delete_native_succeeded",r.delete_source_returned,boolean(r.delete_native_succeeded));
    flag("delete_source_returned",r.delete_source_returned);
    nullable("delete_source_result",r.delete_source_returned,r.delete_source_result);
    flag("retirement_entered",r.retirement_entered);flag("retirement_returned",r.retirement_returned);
    number("host_retire_attempts",r.host_retire_attempts);
    const uint32_t sourceDeletes=r.delete_source_returned?1u:0u;
    nullable("host_retire_error",r.delete_returns>sourceDeletes,r.host_retire_error);
    out << "}}";
}
void WriteRepeatedModulesValue(std::ostream& out,
    const dokodemo::recovered::StartupRepeatedModuleSnapshot& s) {
    const auto boolean=[](bool v){return v?"true":"false";};
    out << "{\"present\":" << boolean(s.present) << ",\"counter_known\":" << boolean(s.counter_known)
        << ",\"counter\":";
    if(s.counter_known) out << s.counter;else out << "null";
    out << ",\"current_record_index\":";
    if(s.present) out << s.current_record_index;else out << "null";
    out << ",\"source_gate_returned\":" << boolean(s.source_gate_returned) << ",\"source_gate_result\":";
    if(s.source_gate_returned) out << s.source_gate_result;else out << "null";
    out << ",\"storage_domain\":\"WindowsSymbolicRepeatedModuleTableV1\""
        << ",\"source_memory_placement_known\":false,\"path_pointer_relocations_applied\":false"
        << ",\"uid_value_origin_policy\":\"RetainedRawSentinelOrActualWindowsBackedWrapperReturn\""
        << ",\"wrapper_success_origin\":\"RetainedLoadUid\",\"wrapper_minus_one_origin\":\"NegativeLoadOrStart\""
        << ",\"symbolic_path_references\":[{\"table_offset\":8,\"referenced_segment\":0,\"addend\":" << 0xDD530
        << "},{\"table_offset\":20,\"referenced_segment\":0,\"addend\":" << 0xDD558 << '}';
    if(s.known[32] && s.known[33] && s.known[34] && s.known[35])
        out << ",{\"table_offset\":32,\"referenced_segment\":0,\"addend\":" << 0xDD58C << '}';
    if(s.known[44] && s.known[45] && s.known[46] && s.known[47])
        out << ",{\"table_offset\":44,\"referenced_segment\":0,\"addend\":" << 0xDD5B8 << '}';
    out << "]"
        << ",\"source_segment\":" << s.source_segment << ",\"source_table_offset\":" << s.source_table_offset
        << ",\"source_table_va\":" << s.source_table_va << ",\"table_bytes\":" << s.table_bytes
        << ",\"known_bytes\":" << s.known_bytes << ",\"writes_count\":" << s.writes_count
        << ",\"bytes_hex\":" << JsonString(StartupHexBytes(s.bytes.data(),s.bytes.size()))
        << ",\"known_hex\":" << JsonString(StartupHexBytes(s.known.data(),s.known.size()))
        << ",\"bytes_meaning_requires_known_one\":true,\"writes\":[";
    for(size_t i=0;i<(std::min)(size_t(s.writes_count),s.writes.size());++i){
        if(i)out << ',';const auto& w=s.writes[i];
        out << "{\"ordinal\":" << w.ordinal << ",\"source_pc\":" << w.source_pc
            << ",\"record_index\":" << w.record_index << ",\"table_offset\":" << w.table_offset
            << ",\"width\":4,\"value\":" << w.value << ",\"value_origin\":\"ActualWindowsBackedWrapperReturn\"}";
    }
    out << "]}";
}

void WriteModuleRecordValue(std::ostream& out, const dokodemo::recovered::NativeModuleSnapshot& record) {
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    out << "{\"token\":"; WriteModuleTokenValue(out, record.token);
    out << ",\"state\":" << JsonString(dokodemo::recovered::NativeModuleStateName(record.state))
        << ",\"production_domain\":" << boolean(record.domain == dokodemo::recovered::NativeModuleDomain::Production)
        << ",\"name\":" << JsonString(record.name) << ",\"encoded_sha256\":" << JsonString(record.encoded_sha256)
        << ",\"plaintext_sha256\":" << JsonString(record.plaintext_sha256)
        << ",\"implementation\":" << JsonString(record.implementation)
        << ",\"policy\":" << JsonString(record.policy)
        << ",\"encoded_bytes\":" << record.encoded_bytes << ",\"plaintext_bytes\":" << record.plaintext_bytes
        << ",\"input_live\":" << boolean(record.input_live) << ",\"active_calls\":" << record.active_calls
        << ",\"owned_native_codec_resources\":" << record.owned_native_codec_resources
        << ",\"owned_providers\":" << record.owned_providers << ",\"incoming_bindings\":" << record.incoming_bindings
        << ",\"start_source_va\":" << record.start_source_va << ",\"stop_source_va\":" << record.stop_source_va
        << ",\"entry_frontier_source_pc\":" << record.entry_frontier_source_pc
        << ",\"entry_frontier\":" << JsonString(record.entry_frontier)
        << ",\"partial_start_supported\":" << boolean(record.partial_start_supported)
        << ",\"input\":{\"owner\":" << record.input.owner << ",\"generation\":" << record.input.generation
        << ",\"serial\":" << record.input.serial << ",\"host_address\":" << record.input.host_address
        << ",\"source_coordinate\":" << record.input.source_coordinate << ",\"size\":" << record.input.size << "},\"imports\":[";
    bool first = true;
    for (const auto& item : record.imports) {
        if (!first) out << ',';
        first = false;
        out << "{\"module\":"; WriteModuleTokenValue(out, item.module);
        out << ",\"library\":" << JsonString(item.library) << ",\"abi\":" << JsonString(item.abi)
            << ",\"version\":" << item.version << ",\"attributes\":" << item.attributes
            << ",\"nid\":" << item.nid << ",\"source_stub_va\":" << item.source_stub_va
            << ",\"resolved\":" << boolean(item.resolved) << ",\"partial_callable\":" << boolean(item.partial_callable)
            << ",\"provider_serial\":" << item.provider_serial
            << ",\"provider_owner\":" << item.provider_owner << ",\"provider_generation\":" << item.provider_generation
            << ",\"provider_module\":"; WriteModuleTokenValue(out, item.provider_module);
        out << ",\"binding_generation\":" << item.binding_generation << '}';
    }
    out << "],\"exports\":["; first = true;
    for (const auto& item : record.exports) {
        if (!first) out << ',';
        first = false;
        out << "{\"module\":"; WriteModuleTokenValue(out, item.module);
        out << ",\"library\":" << JsonString(item.library) << ",\"kind\":" << JsonString(item.kind)
            << ",\"version\":" << item.version << ",\"attributes\":" << item.attributes
            << ",\"nid\":" << item.nid << ",\"relative_va\":" << item.relative_va
            << ",\"lifecycle_callable\":" << boolean(item.lifecycle_callable)
            << ",\"partial_callable\":" << boolean(item.partial_callable) << '}';
    }
    out << "],\"initialization\":";
    WriteModuleInitializationValue(out, record.initialization);
    out << ",\"check_need_mem\":"; WriteCheckNeedMemValue(out, record.check_need_mem);
    out << ",\"prepared_envelope\":"; WritePreparedEnvelopeValue(out, record.prepared_envelope);
    out << ",\"sascore\":"; WriteSascoreValue(out, record.sascore);
    out << ",\"ifhandle\":"; WriteIfhandleValue(out, record.ifhandle);
    out << ",\"pspnet\":"; WritePspnetValue(out, record.pspnet);
    out << ",\"memab\":"; WriteMemabValue(out, record.memab);
    out << ",\"adhoc_auth\":"; WriteAdhocAuthValue(out, record.adhoc_auth);
    out << ",\"adhoc\":"; WriteAdhocValue(out, record.adhoc);
    out << ",\"adhocctl\":"; WriteAdhocctlValue(out, record.adhocctl);
    out << '}';
}
void WriteModuleRequestValue(std::ostream& out, const dokodemo::StartupModuleObservation& item) {
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    out << "{\"request_index\":" << item.request_index << ",\"wrapper_callsite\":" << item.wrapper_callsite
        << ",\"repeated_record_index\":" << (item.repeated_record_index==dokodemo::StartupNoCurrentModule ? "null" : std::to_string(item.repeated_record_index))
        << ",\"path\":" << JsonString(item.path) << ",\"flags\":" << item.flags
        << ",\"options_hex\":" << JsonString(StartupHexBytes(item.options.data(), item.options.size()))
        << ",\"request_owner\":" << item.request_owner << ",\"pending\":" << boolean(item.pending)
        << ",\"frontier\":" << JsonString(item.frontier) << ",\"frontier_source_pc\":" << item.frontier_source_pc
        << ",\"frontier_module_relative\":" << boolean(item.frontier_module_relative)
        << ",\"frontier_source_module\":"; WriteModuleTokenValue(out, item.frontier_source_module);
    out << ",\"source_fd\":" << item.source_fd << ",\"file_native_object\":" << item.file_native_object
        << ",\"file_backend_owner\":" << item.file_backend_owner << ",\"file_backend_generation\":" << item.file_backend_generation
        << ",\"file_backend_serial\":" << item.file_backend_serial << ",\"encoded_file_bytes\":" << item.encoded_file_bytes
        << ",\"file_live\":" << boolean(item.file_live) << ",\"staging_owner\":" << item.staging_owner
        << ",\"staging_generation\":" << item.staging_generation << ",\"staging_serial\":" << item.staging_serial
        << ",\"staging_host_address\":" << item.staging_host_address << ",\"staging_known_bytes\":" << item.staging_known_bytes
        << ",\"staging_committed_bytes\":" << item.staging_committed_bytes << ",\"staging_page_bytes\":" << item.staging_page_bytes
        << ",\"staging_source_coordinate\":" << item.staging_source_coordinate << ",\"staging_requested\":" << item.staging_requested
        << ",\"staging_transferred\":" << item.staging_transferred << ",\"staging_read_error\":" << item.staging_read_error
        << ",\"staging_file_position\":" << item.staging_file_position << ",\"staging_live\":" << boolean(item.staging_live)
        << ",\"staging_borrow_live\":" << boolean(item.staging_borrow_live) << ",\"staging_host_read\":" << boolean(item.staging_host_read)
        << ",\"staging_full_read\":" << boolean(item.staging_full_read) << ",\"staging_hash_matches\":" << boolean(item.staging_hash_matches)
        << ",\"staging_sha256\":" << JsonString(item.staging_sha256) << ",\"staging_freed_after_joins\":" << boolean(item.staging_freed_after_joins)
        << ",\"token\":"; WriteModuleTokenValue(out, item.token);
    out << ",\"module_uid_assigned\":" << boolean(item.module_uid_assigned)
        << ",\"module_load_returned\":" << boolean(item.module_load_returned)
        << ",\"module_load_result\":" << (item.module_load_returned ? std::to_string(item.module_load_result) : "null")
        << ",\"module_input_live\":" << boolean(item.module_input_live) << ",\"module_start_called\":" << boolean(item.module_start_called)
        << ",\"module_start_returned\":" << boolean(item.module_start_returned)
        << ",\"module_start_result\":" << (item.module_start_returned ? std::to_string(item.module_start_result) : "null")
        << ",\"module_start_entry_returned\":" << boolean(item.module_start_entry_returned)
        << ",\"module_start_entry_result\":" << (item.module_start_entry_returned ? std::to_string(item.module_start_entry_result) : "null")
        << ",\"source_close_called\":" << boolean(item.source_close_called) << ",\"source_close_returned\":" << boolean(item.source_close_returned)
        << ",\"source_close_result\":" << (item.source_close_returned ? std::to_string(item.source_close_result) : "null")
        << ",\"module_file_closed_while_input_live\":" << boolean(item.module_file_closed_while_input_live)
        << ",\"module_stop_called\":" << boolean(item.module_stop_called) << ",\"module_stop_returned\":" << boolean(item.module_stop_returned)
        << ",\"module_stop_result\":" << (item.module_stop_returned ? std::to_string(item.module_stop_result) : "null")
        << ",\"module_unloaded\":" << boolean(item.module_unloaded)
        << ",\"module_input_released\":" << boolean(item.module_input_released) << ",\"host_file_closed\":" << boolean(item.host_file_closed)
        << ",\"module_initialization_blocked\":" << boolean(item.module_initialization_blocked)
        << ",\"module_initialization_abort_called\":" << boolean(item.module_initialization_abort_called)
        << ",\"module_initialization_aborted\":" << boolean(item.module_initialization_aborted)
        << ",\"record_live\":" << boolean(item.record_live) << ",\"record\":";
    WriteModuleRecordValue(out, item.record);
    out << '}';
}
void WriteModuleObservation(std::ostream& out, const dokodemo::StartupObservation& observation) {
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    out << ",\"module_manager_owner\":" << observation.module_manager_owner
        << ",\"module_generation\":" << observation.module_generation
        << ",\"module_serial\":" << observation.module_serial
        << ",\"module_state\":" << JsonString(observation.module_state)
        << ",\"module_input_live\":" << boolean(observation.module_input_live)
        << ",\"module_file_closed_while_input_live\":" << boolean(observation.module_file_closed_while_input_live)
        << ",\"module_start_returned\":" << boolean(observation.module_start_returned)
        << ",\"module_start_result\":" << observation.module_start_result
        << ",\"module_stop_called\":" << boolean(observation.module_stop_called)
        << ",\"module_stop_returned\":" << boolean(observation.module_stop_returned)
        << ",\"module_stop_result\":" << observation.module_stop_result
        << ",\"module_unloaded\":" << boolean(observation.module_unloaded)
        << ",\"module_input_released\":" << boolean(observation.module_input_released)
        << ",\"module_record\":";
    WriteModuleRecordValue(out, observation.module_record);
    out << ",\"module_providers\":["; bool first = true;
    for (const auto& item : observation.module_providers) {
        if (!first) out << ',';
        first = false;
        out << "{\"owner\":" << item.token.owner << ",\"generation\":" << item.token.generation
            << ",\"serial\":" << item.token.serial << ",\"library\":" << JsonString(item.library)
            << ",\"abi\":" << JsonString(item.abi) << ",\"version\":" << item.version
            << ",\"nid\":" << item.nid << ",\"synthetic_only\":" << boolean(item.synthetic_only)
            << ",\"partial_callable\":" << boolean(item.partial_callable)
            << ",\"prepared_envelope_callable\":" << boolean(item.prepared_envelope_callable)
            << ",\"module\":"; WriteModuleTokenValue(out, item.module); out << '}';
    }
    out << ']';
}
void WriteGame73Output(std::ostream& out,const dokodemo::audio::Game73PcmOutputSnapshot& a){
    const auto flag=[](bool x){return x?"true":"false";};
    const auto& w=a.last_rendered;const auto& t=w.trace;const auto& b=w.bank_identity;
    out<<"{\"policy\":\"FiniteSelectedSESQ;ReferenceADSR80FF5FC0;REF-RAW-KEYON32;NativeHalfLinear;NotFirmwareMatching\""
        <<",\"stage\":"<<int(a.stage)<<",\"failed_operation\":"<<int(a.failed_operation)<<",\"hresult\":"<<a.hresult<<",\"native_error\":"<<a.native_error
        <<",\"request_id\":"<<a.request_id<<",\"group\":"<<a.group<<",\"entry\":"<<a.entry<<",\"volume\":"<<a.volume<<",\"profile_gain\":"<<a.profile_gain
        <<",\"master_left\":"<<a.master_left<<",\"master_right\":"<<a.master_right<<",\"selected_requests\":"<<a.selected_requests<<",\"rendered_requests\":"<<a.rendered_requests
        <<",\"queued_requests\":"<<a.queued_requests<<",\"submitted_frames\":"<<a.submitted_frames<<",\"started_requests\":"<<a.started_requests<<",\"completed_client_drain\":"<<a.completed_client_drain
        <<",\"failed_requests\":"<<a.failed_requests<<",\"cancelled_requests\":"<<a.cancelled_requests<<",\"render_code\":"<<int(a.render_code)
        <<",\"last_started_request_id\":"<<a.last_started_request_id<<",\"last_drained_request_id\":"<<a.last_drained_request_id
        <<",\"accepting\":"<<flag(a.accepting)<<",\"actor_thread\":"<<a.actor_thread<<",\"actor_exited\":"<<flag(a.actor_exited)<<",\"actor_joined\":"<<flag(a.actor_joined)
        <<",\"handles_closed\":"<<flag(a.handles_closed)<<",\"com_references_released\":"<<flag(a.com_references_released)<<",\"synthetic_device\":"<<flag(a.synthetic_device)
        <<",\"last_rendered\":{\"request_id\":"<<w.request_id<<",\"group\":"<<t.group<<",\"entry\":"<<t.entry<<",\"volume\":"<<t.volume<<",\"profile_gain\":"<<t.profile_gain
        <<",\"master_left\":"<<t.master_left<<",\"master_right\":"<<t.master_right<<",\"effective_parameter\":"<<t.effective_parameter
        <<",\"bank_owner\":"<<b.owner<<",\"bank_generation\":"<<b.generation<<",\"bank_serial\":"<<b.serial<<",\"bank_host_address\":"<<b.host_address<<",\"bank_offset\":"<<b.offset<<",\"bank_bytes\":"<<b.size
        <<",\"sequence_offset\":"<<t.sequence_offset<<",\"command_bytes\":"<<t.command_bytes<<",\"events\":"<<t.events<<",\"region_index\":"<<t.region_index<<",\"wave_index\":"<<t.wave_index
        <<",\"wave_offset\":"<<t.wave_offset<<",\"wave_bytes\":"<<t.wave_bytes<<",\"pitch4096\":"<<t.pitch4096<<",\"combined_volume\":"<<t.combined_volume<<",\"base_gain\":"<<t.base_gain
        <<",\"dry_left\":"<<t.dry_left<<",\"dry_right\":"<<t.dry_right<<",\"envelope1\":"<<t.envelope1<<",\"envelope2\":"<<t.envelope2
        <<",\"first_end_block\":"<<t.first_end_block<<",\"storage_frames\":"<<t.storage_frames<<",\"input_frames\":"<<t.input_frames<<",\"output_frames\":"<<t.output_frames
        <<",\"sequence_ended\":"<<flag(t.sequence_ended)<<",\"key_off_at_ff\":"<<flag(t.key_off_at_ff)<<",\"reference_flag_policy\":"<<flag(t.reference_flag_policy)
        <<",\"wave_sha256\":"<<JsonString(StartupHexBytes(w.wave_sha256.data(),w.wave_sha256.size()))
        <<",\"pcm_sha256\":"<<JsonString(StartupHexBytes(w.pcm_sha256.data(),w.pcm_sha256.size()))<<"},\"audible\":\"UNVERIFIED\"}";
}
void WriteStartupObservation(const std::filesystem::path& path, const dokodemo::StartupObservation& observation) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write native startup observation");
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    constexpr char hex[] = "0123456789abcdef";
    std::string pendingOptions;
    for (uint8_t byte : observation.pending_module_options) {
        pendingOptions += hex[byte >> 4]; pendingOptions += hex[byte & 15];
    }
    out << "{\"native_audio\":";WriteGame73Output(out,observation.native_audio);
    out << ",\"native_audio_creation_error\":"<<observation.native_audio_creation_error<<",\"domain\":" << JsonString(observation.domain)
        << ",\"pending_deferred_codec_cleanup\":" << dokodemo::StartupCoordinator::PendingDeferredCodecCleanup()
        << ",\"pending_deferred_memab_cleanup\":" << dokodemo::PendingWindowsMemabCleanup()
        << ",\"caller_state\":" << JsonString(observation.caller_state)
        << ",\"worker_state\":" << JsonString(observation.worker_state)
        << ",\"barrier\":" << JsonString(observation.barrier)
        << ",\"barrier_source_pc\":" << observation.barrier_source_pc
        << ",\"barrier_module_relative\":" << boolean(observation.barrier_module_relative)
        << ",\"barrier_source_module\":"; WriteModuleTokenValue(out, observation.barrier_source_module);
    out << ",\"owner\":" << observation.owner << ",\"last_event\":" << observation.last_event
        << ",\"dropped_events\":" << observation.dropped_events
        << ",\"caller_thread_id\":" << observation.caller_thread_id
        << ",\"worker_thread_id\":" << observation.worker_thread_id
        << ",\"stored_thread_uid\":" << observation.stored_thread_uid
        << ",\"uid_slot_written\":" << boolean(observation.uid_slot_written)
        << ",\"callback_uid\":" << observation.callback_uid
        << ",\"callback_create_result\":" << observation.callback_create_result
        << ",\"callback_register_result\":" << observation.callback_register_result
        << ",\"callback_setup_returned\":" << boolean(observation.callback_setup_returned)
        << ",\"callback_dispatch_count\":" << observation.callback_dispatch_count
        << ",\"callback_dispatch_thread\":" << observation.callback_dispatch_thread
        << ",\"source_fd\":" << observation.source_fd
        << ",\"media_live\":" << boolean(observation.media_live)
        << ",\"file_live\":" << boolean(observation.file_live)
        << ",\"callback_live\":" << boolean(observation.callback_live)
        << ",\"caller_joined\":" << boolean(observation.caller_joined)
        << ",\"worker_joined\":" << boolean(observation.worker_joined)
        << ",\"resources_closed\":" << boolean(observation.resources_closed)
        << ",\"exit_requested\":" << boolean(observation.exit_requested)
        << ",\"media_volume\":" << observation.media_volume
        << ",\"media_file_index\":" << observation.media_file_index
        << ",\"file_native_object\":" << observation.file_native_object
        << ",\"file_backend_owner\":" << observation.file_backend_owner
        << ",\"file_backend_generation\":" << observation.file_backend_generation
        << ",\"file_backend_serial\":" << observation.file_backend_serial
        << ",\"pending_module_live\":" << boolean(observation.pending_module_live)
        << ",\"pending_module_owner\":" << observation.pending_module_owner
        << ",\"pending_module_flags\":" << observation.pending_module_flags
        << ",\"pending_module_path\":" << JsonString(observation.pending_module_path)
        << ",\"pending_module_options_hex\":" << JsonString(pendingOptions)
        << ",\"encoded_file_bytes\":" << observation.encoded_file_bytes
        << ",\"staging_owner\":" << observation.staging_owner
        << ",\"staging_generation\":" << observation.staging_generation
        << ",\"staging_serial\":" << observation.staging_serial
        << ",\"staging_host_address\":" << observation.staging_host_address
        << ",\"staging_known_bytes\":" << observation.staging_known_bytes
        << ",\"staging_file_position\":" << observation.staging_file_position
        << ",\"staging_source_coordinate\":" << observation.staging_source_coordinate
        << ",\"staging_requested\":" << observation.staging_requested
        << ",\"staging_transferred\":" << observation.staging_transferred
        << ",\"staging_read_error\":" << observation.staging_read_error
        << ",\"staging_live\":" << boolean(observation.staging_live)
        << ",\"staging_borrow_live\":" << boolean(observation.staging_borrow_live)
        << ",\"staging_host_read\":" << boolean(observation.staging_host_read)
        << ",\"staging_full_read\":" << boolean(observation.staging_full_read)
        << ",\"staging_hash_matches\":" << boolean(observation.staging_hash_matches)
        << ",\"staging_sha256\":" << JsonString(observation.staging_sha256)
        << ",\"staging_freed_after_joins\":" << boolean(observation.staging_freed_after_joins)
        << ",\"module_uid_assigned\":" << boolean(observation.module_uid_assigned)
        << ",\"module_load_returned\":" << boolean(observation.module_load_returned)
        << ",\"module_start_called\":" << boolean(observation.module_start_called)
        << ",\"module_uid\":" << observation.module_uid
        << ",\"module_binding_frontier\":" << JsonString(observation.module_binding_frontier);
    WriteModuleObservation(out, observation);
    out << ",\"repeated_modules\":"; WriteRepeatedModulesValue(out, observation.repeated_modules);
    out << ",\"next_game_call\":"; WriteNextGameCallValue(out, observation);
    out << ",\"current_module_index\":" << observation.current_module_index
        << ",\"no_current_module_sentinel\":" << dokodemo::StartupNoCurrentModule
        << ",\"current_frontier\":" << JsonString(observation.current_frontier)
        << ",\"current_frontier_source_pc\":" << observation.current_frontier_source_pc
        << ",\"current_frontier_module_relative\":" << boolean(observation.current_frontier_module_relative)
        << ",\"current_frontier_source_module\":"; WriteModuleTokenValue(out, observation.current_frontier_source_module);
    out << ",\"module_observations\":[";
    bool firstModule = true;
    for (const auto& item : observation.module_observations) {
        if (!firstModule) out << ',';
        firstModule = false;
        WriteModuleRequestValue(out, item);
    }
    out << "],\"module_records\":["; firstModule = true;
    for (const auto& item : observation.module_records) {
        if (!firstModule) out << ',';
        firstModule = false;
        WriteModuleRecordValue(out, item);
    }
    out << ']';
    out << ",\"status\":" << JsonString(dokodemo::StartupRuntimeCodeName(observation.status.code))
        << ",\"preceding_psp_history\":" << boolean(observation.preceding_psp_history)
        << ",\"constructors_established\":" << boolean(observation.constructors_established)
        << ",\"reached_100C\":" << boolean(observation.reached_100C)
        << ",\"full_game\":" << boolean(observation.full_game) << ",\"game_boot\":";
    WriteGameBootValue(out, observation.game_boot);
    out << ",\"events\":[";
    bool first = true;
    for (const auto& event : observation.events) {
        if (!first) out << ',';
        first = false;
        out << "{\"ordinal\":" << event.ordinal << ",\"operation\":" << JsonString(event.operation)
            << ",\"origin\":" << JsonString(event.origin) << ",\"detail\":" << JsonString(event.detail)
            << ",\"source_pc\":" << event.source_pc << ",\"source_pc_domain\":" << JsonString(event.source_pc_domain)
            << ",\"module_request_index\":" << event.module_request_index << ",\"module\":";
        WriteModuleTokenValue(out, event.module);
        out << ",\"source_module\":"; WriteModuleTokenValue(out, event.source_module);
        out << ",\"thread\":" << event.thread
            << ",\"native_object\":" << event.native_object << ",\"owner\":" << event.owner
            << ",\"value\":" << event.value << ",\"source_return\":" << boolean(event.source_return)
            << ",\"win32_error\":" << event.win32_error << ",\"wait_result\":" << event.wait_result
            << ",\"crt_errno\":" << event.crt_errno << ",\"dos_errno\":" << event.dos_errno << '}';
    }
    out << "]}\n";
    if (!out) throw std::runtime_error("Native startup observation write failed");
}
bool SameNativeModuleToken(const dokodemo::recovered::NativeModuleToken& a,
    const dokodemo::recovered::NativeModuleToken& b) {
    return a.owner == b.owner && a.generation == b.generation && a.serial == b.serial && a.uid == b.uid;
}
bool FirstNativeModuleRetained(const dokodemo::StartupObservation& observation) {
    const auto& module = observation.module_record;
    const bool prepared = observation.module_providers.size() == 1
        && observation.module_providers[0].prepared_envelope_callable;
    return observation.domain == "WindowsNativeEntry1184"
        && observation.caller_state == "SourceSleeping"
        && (observation.worker_state == "Blocked" || (observation.worker_state == "Interactive"
            && observation.game_boot.content.vsync_owner && !observation.game_boot.content.closed))
        && observation.uid_slot_written && observation.stored_thread_uid > 0
        && observation.callback_uid > 0 && observation.callback_setup_returned
        && observation.callback_create_result == observation.callback_uid
        && observation.callback_register_result == 0 && observation.source_fd >= 0
        && observation.media_live && !observation.file_live && observation.callback_live
        && !observation.pending_module_live && observation.pending_module_owner == observation.owner
        && observation.pending_module_flags == 0
        && observation.pending_module_path == "disc0:/PSP_GAME/USRDIR/prx/audiocodec.prx"
        && observation.pending_module_options == std::vector<uint8_t>{20,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0}
        && observation.file_native_object && observation.file_backend_owner
        && observation.file_backend_generation && observation.file_backend_serial
        && observation.encoded_file_bytes == 3232 && observation.staging_requested == 3232
        && observation.staging_transferred == 3232 && observation.staging_known_bytes == 3232
        && observation.staging_file_position == 0 && observation.staging_source_coordinate == 0x61000000
        && observation.staging_owner == observation.file_backend_owner
        && observation.staging_generation == observation.file_backend_generation
        && observation.staging_serial && observation.staging_host_address
        && observation.staging_live && observation.staging_borrow_live && observation.staging_host_read
        && observation.staging_full_read && observation.staging_hash_matches && observation.staging_read_error == 0
        && observation.staging_sha256 == "cea81b33d4eab3607da188b0435a8582ffc7c208337c05f7e3139f0133bcb318"
        && observation.module_uid_assigned && observation.module_uid > 0
        && observation.module_load_returned && observation.module_start_called
        && observation.module_start_returned && observation.module_start_result == 0
        && observation.module_input_live && observation.module_file_closed_while_input_live
        && !observation.module_stop_called && !observation.module_unloaded
        && observation.module_manager_owner == module.token.owner
        && observation.module_generation == module.token.generation
        && observation.module_serial == module.token.serial && observation.module_uid == module.token.uid
        && module.domain == dokodemo::recovered::NativeModuleDomain::Production
        && module.state == dokodemo::recovered::NativeModuleState::Started
        && module.input_live && module.active_calls == 0 && observation.module_providers.size() == 1
        && observation.module_providers[0].partial_callable && !observation.module_providers[0].synthetic_only
        && observation.module_providers[0].library == "sceAudiocodec"
        && observation.module_providers[0].abi == (prepared ? dokodemo::recovered::NativePreparedEnvelopeAbi
            : "windows-owned-masked-checkneedmem-prefix-v1")
        && observation.module_providers[0].version == 0 && observation.module_providers[0].nid == 0x9D3F790C
        && observation.module_providers[0].token.owner == module.token.owner
        && observation.module_providers[0].token.generation == module.token.generation
        && observation.module_providers[0].token.serial != 0
        && SameNativeModuleToken(observation.module_providers[0].module, module.token)
        && module.input.owner == observation.staging_owner
        && module.input.generation == observation.staging_generation
        && module.input.serial == observation.staging_serial
        && module.input.host_address == observation.staging_host_address && module.input.size == 3232
        && module.imports.size() == 8
        && std::all_of(module.imports.begin(), module.imports.end(), [](const auto& item) {
            return item.attributes == 9 && !item.resolved && !item.partial_callable && item.provider_serial == 0;
        })
        && std::count_if(module.exports.begin(), module.exports.end(), [](const auto& item) { return item.partial_callable; }) == (prepared ? 3 : 1)
        && std::any_of(module.exports.begin(), module.exports.end(), [](const auto& item) {
            return item.nid == 0x9D3F790C && item.relative_va == 0 && item.partial_callable && !item.lifecycle_callable;
        })
        && observation.caller_thread_id && observation.worker_thread_id
        && observation.caller_thread_id != observation.worker_thread_id
        && !observation.preceding_psp_history && !observation.constructors_established
        && !observation.full_game && observation.dropped_events == 0;
}
bool AtracPrefixAtDependency(const dokodemo::recovered::Atrac3plusInitializationSnapshot& value) {
    if (!value.present || !value.prefix_entered || !value.dependency_blocked || value.aborted
        || !value.owner || !value.generation || !value.serial || !value.native_state_address
        || value.source_segment != 1 || value.source_segment_offset != 0xA0
        || value.record_count != 4 || value.record_stride != 0x148 || value.record_bytes != 1312
        || value.initial_source_known_bytes != 0 || value.known_bytes != 14 || value.writes_count != 5
        || value.frontier_source_pc != 0x1684 || value.frontier_delay_pc != 0x1688
        || value.frontier_stub_va != 0x3640 || value.frontier_import_index != 2 || value.frontier_nid != 0x9D3F790C
        || value.arg0_record_index != 0 || value.arg0_record_offset != 0x44 || value.arg1 != 0x1000
        || !value.frontier_arguments_prepared || value.helper_calls_entered != 1 || value.loop_iterations_completed != 0
        || value.external_services_dispatched != 1 || value.external_resources_acquired != 0) return false;
    for (size_t i = 0; i < value.known.size(); ++i) {
        const bool expectedKnown = i < 4 || (i >= 0x44 && i < 0x48) || i == 0x6C || i == 0x6D || (i >= 0xAC && i < 0xB0);
        if (value.known[i] != static_cast<uint8_t>(expectedKnown)) return false;
        // Unmarked physical bytes have no source initialization authority.
    }
    if (value.bytes[0] != 1 || value.bytes[1] || value.bytes[2] || value.bytes[3]
        || value.bytes[0x6C] != 0x28 || value.bytes[0x6D] != 0x5C
        || value.bytes[0xAC] || value.bytes[0xAD] || value.bytes[0xAE] != 0x20 || value.bytes[0xAF]
        || value.bytes[0x44] != 1 || value.bytes[0x45] != 7 || value.bytes[0x46] != 9 || value.bytes[0x47] != 4) return false;
    constexpr std::array<uint32_t, 5> pcs{0x1678,0x1680,0x16D4,0x16E0,0x80};
    constexpr std::array<uint32_t, 5> offsets{0xAC,0,0x6C,0x6D,0x44};
    constexpr std::array<uint32_t, 5> widths{4,4,1,1,4};
    constexpr std::array<uint32_t, 5> values{0x00200000,1,0x28,0x5C,0x04090701};
    for (size_t i = 0; i < value.writes_count; ++i) {
        const auto& item = value.writes[i];
        if (item.ordinal != i || item.source_pc != pcs[i] || item.offset != offsets[i]
            || item.width != widths[i] || item.value != values[i]
            || item.source_module != (i == 4 ? dokodemo::recovered::Atrac3plusWriteSource::Audiocodec
                                             : dokodemo::recovered::Atrac3plusWriteSource::Atrac3plus)) return false;
    }
    return true;
}
[[maybe_unused]] bool AtLegacyNativeModuleSequenceBoundary(const dokodemo::StartupObservation& observation) {
    if (!FirstNativeModuleRetained(observation) || observation.module_observations.size() != 2
        || observation.module_records.size() != 2 || observation.current_module_index != 1
        || !observation.barrier_module_relative || !observation.current_frontier_module_relative
        || observation.barrier_source_pc != 0x84 || observation.current_frontier_source_pc != 0x84) return false;
    const auto& first = observation.module_observations[0];
    const auto& second = observation.module_observations[1];
    const auto& record = second.record;
    const auto& call = record.check_need_mem;
    const auto& body = call.body;
    const auto& provider = observation.module_providers[0];
    if (!SameNativeModuleToken(observation.barrier_source_module, first.token)
        || !SameNativeModuleToken(observation.current_frontier_source_module, first.token)
        || !SameNativeModuleToken(second.frontier_source_module, first.token)
        || second.frontier_source_pc != 0x84
        || !call.present || !call.partial_callable || !SameNativeModuleToken(call.provider_module, first.token)
        || !SameNativeModuleToken(call.consumer_module, second.token)
        || call.provider.owner != provider.token.owner || call.provider.generation != provider.token.generation
        || call.provider.serial != provider.token.serial
        || call.provider_input_owner != first.staging_owner || call.provider_input_generation != first.staging_generation
        || call.provider_input_serial != first.staging_serial || call.consumer_input_owner != second.staging_owner
        || call.consumer_input_generation != second.staging_generation || call.consumer_input_serial != second.staging_serial
        || call.caller_source_pc != 0x1684 || call.buffer_record_offset != 0x44 || call.buffer_bytes != 0x68
        || call.window_known_before != 2 || call.window_known_after != 6
        || call.buffer_native_address != record.initialization.native_state_address
            + offsetof(dokodemo::recovered::Atrac3plusInitializationSnapshot, bytes) + 0x44
        || !body.entered || !body.host_policy_applied || body.source_null || !body.dependency_blocked
        || body.dependency_dispatched || body.source_return || body.codec != 0x1000 || body.buffer_reads != 0 || body.writes != 1
        || body.entry_source_pc != 0 || body.write_source_pc != 0x80 || body.frontier_source_pc != 0x84
        || body.frontier_delay_pc != 0x88 || body.frontier_stub_va != 0xAF8 || body.frontier_nid != 0xC00D873B) return false;
    if (first.request_index != 0 || second.request_index != 1 || second.wrapper_callsite != 0x4F640
        || first.token.uid != observation.module_uid || second.token.uid <= 0 || first.token.uid == second.token.uid
        || first.token.owner != second.token.owner || first.token.generation != second.token.generation
        || first.token.serial == second.token.serial || !first.record_live || !second.record_live
        || !first.module_input_live || !first.source_close_called || !first.source_close_returned || first.file_live
        || first.record.state != dokodemo::recovered::NativeModuleState::Started
        || second.path != "disc0:/PSP_GAME/USRDIR/prx/libatrac3plus.prx" || second.flags != 0
        || second.options != std::vector<uint8_t>{20,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1,0,0}
        || second.request_owner != observation.owner || second.source_fd < 0 || !second.file_live
        || !second.module_uid_assigned || !second.module_load_returned || second.module_load_result != second.token.uid
        || !second.module_start_called || second.module_start_returned || second.module_start_entry_returned
        || !second.module_initialization_blocked || second.module_initialization_aborted || second.module_stop_called
        || second.source_close_called || second.source_close_returned || !second.module_input_live
        || record.state != dokodemo::recovered::NativeModuleState::EntryBlocked || !record.input_live
        || !record.partial_start_supported || record.imports.size() != 7 || record.exports.size() != 28
        || record.policy != "WindowsAtrac3plusExact0001_4001PartialInitV1"
        || record.token.uid != second.token.uid || record.token.serial != second.token.serial
        || second.encoded_file_bytes != 19524 || second.staging_requested != 19524 || second.staging_transferred != 19524
        || second.staging_known_bytes != 19524 || second.staging_file_position != 0 || second.staging_read_error != 0
        || !second.staging_live || !second.staging_borrow_live || !second.staging_host_read
        || !second.staging_full_read || !second.staging_hash_matches
        || second.staging_sha256 != "edfc95810a5881f7f752eee255693e75ba1ade6862b557126c5236c2b47bbca4"
        || record.input.size != 19524 || record.input.host_address != second.staging_host_address
        || record.input.owner != second.staging_owner || record.input.generation != second.staging_generation
        || record.input.serial != second.staging_serial || record.input.source_coordinate != second.staging_source_coordinate
        || first.staging_owner != second.staging_owner || first.staging_generation != second.staging_generation
        || first.staging_serial == second.staging_serial || first.staging_host_address == second.staging_host_address
        || !first.staging_page_bytes || !second.staging_page_bytes
        || uint64_t(first.staging_source_coordinate) + first.staging_committed_bytes > second.staging_source_coordinate
        || !AtracPrefixAtDependency(record.initialization)) return false;
    for (size_t i = 0; i < record.imports.size(); ++i) {
        const auto& item = record.imports[i];
        if (item.version != 0 || item.attributes != (i < 2 ? 1 : 0x4001) || item.resolved) return false;
        if (i == 2) {
            if (!item.partial_callable || item.provider_serial != provider.token.serial
                || item.provider_owner != provider.token.owner || item.provider_generation != provider.token.generation
                || !SameNativeModuleToken(item.provider_module, first.token)) return false;
        } else if (item.partial_callable || item.provider_serial) return false;
    }
    return std::none_of(record.exports.begin(), record.exports.end(), [](const auto& item) { return item.lifecycle_callable; });
}
bool QueryEnvelopeEffectsExact(const dokodemo::recovered::Atrac3plusInitializationSnapshot& state,
    const dokodemo::recovered::NativePreparedEnvelopeSnapshot& envelope, bool released) {
    using namespace dokodemo::recovered;
    if (!state.present || !state.prefix_entered || state.dependency_blocked || state.aborted
        || !state.prepared_envelope_policy || !state.owner || !state.generation || !state.serial
        || !state.native_state_address || state.source_segment != 1 || state.source_segment_offset != 0xA0
        || state.record_count != 4 || state.record_stride != 0x148 || state.record_bytes != 1312
        || state.initial_source_known_bytes || state.known_bytes != 100
        || state.writes_count != (released ? 44u : 36u) || state.helper_calls_entered != 4
        || state.loop_iterations_completed != 4 || state.external_resources_acquired != 4
        || state.external_services_dispatched != 4) return false;
    std::array<Atrac3plusNativeWrite, 64> writes{};
    size_t count = 0;
    const auto append = [&](uint32_t pc, uint32_t offset, uint32_t width, uint32_t value, Atrac3plusWriteSource source) {
        writes[count] = {static_cast<uint32_t>(count), pc, offset, width, value, source}; ++count;
    };
    for (size_t i = 0; i < 4; ++i) {
        const uint32_t base = static_cast<uint32_t>(i) * 0x148;
        const auto& native = envelope.records[i];
        append(0x1678, base + 0xAC, 4, 0x00200000, Atrac3plusWriteSource::Atrac3plus);
        append(0x1680, base, 4, 1, Atrac3plusWriteSource::Atrac3plus);
        if (i < 2) {
            append(0x16D4, base + 0x6C, 1, 0x28, Atrac3plusWriteSource::Atrac3plus);
            append(0x16E0, base + 0x6D, 1, 0x5C, Atrac3plusWriteSource::Atrac3plus);
        }
        append(0x80, base + 0x44, 4, 0x04090701, Atrac3plusWriteSource::Audiocodec);
        append(0, base + 0x54, 4, native.envelope_bytes, Atrac3plusWriteSource::NativePlatform);
        append(0x6C0, base + 0x50, 4, native.aligned_coordinate, Atrac3plusWriteSource::Audiocodec);
        append(0x6C8, base + 0xAC, 4, native.raw_coordinate, Atrac3plusWriteSource::Audiocodec);
        append(0x16A8, base + 0xF0, 4, i < 2 ? 0x1000u : 0x1001u, Atrac3plusWriteSource::Atrac3plus);
        append(0x16AC, base, 4, 0, Atrac3plusWriteSource::Atrac3plus);
    }
    if (released) for (uint32_t i = 0; i < 4; ++i) {
        append(0x7A0, i * 0x148 + 0x50, 4, 0, Atrac3plusWriteSource::Audiocodec);
        append(0x7A8, i * 0x148 + 0xAC, 4, 0, Atrac3plusWriteSource::Audiocodec);
    }
    if (count != state.writes_count || count > state.writes.size()) return false;
    std::array<uint8_t, 1312> known{}, bytes{};
    for (size_t n = 0; n < count; ++n) {
        const auto& a = state.writes[n]; const auto& b = writes[n];
        if (a.ordinal != b.ordinal || a.source_pc != b.source_pc || a.offset != b.offset
            || a.width != b.width || a.value != b.value || a.source_module != b.source_module) return false;
        if (a.offset > bytes.size() || a.width > bytes.size() - a.offset) return false;
        for (uint32_t j = 0; j < a.width; ++j) {
            known[a.offset + j] = 1; bytes[a.offset + j] = static_cast<uint8_t>(a.value >> (j * 8));
        }
    }
    for (size_t i = 0; i < known.size(); ++i)
        if (state.known[i] != known[i] || (known[i] && state.bytes[i] != bytes[i])) return false;
    return true;
}
[[maybe_unused]] bool PreparedEnvelopeModulesReady(const dokodemo::StartupObservation& observation) {
    using namespace dokodemo::recovered;
    if (!FirstNativeModuleRetained(observation) || observation.module_observations.size() < 2
        || observation.module_records.size() < 2) return false;
    const auto& first = observation.module_observations[0];
    const auto& second = observation.module_observations[1];
    const auto& record = second.record;
    const auto& init = record.initialization;
    const auto& envelope = record.prepared_envelope;
    const auto& provider = observation.module_providers[0];
    if (first.record.owned_native_codec_resources != 4 || record.owned_native_codec_resources != 4
        || second.request_index != 1 || second.wrapper_callsite != 0x4F640
        || second.path != "disc0:/PSP_GAME/USRDIR/prx/libatrac3plus.prx"
        || second.options != std::vector<uint8_t>{20,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1,0,0}
        || !second.record_live || !second.module_input_live || !record.input_live || record.active_calls
        || record.state != NativeModuleState::Started || record.imports.size() != 7 || record.exports.size() != 28
        || !second.module_uid_assigned || !second.module_load_returned || second.module_load_result != second.token.uid
        || !second.module_start_called || !second.module_start_returned || second.module_start_result
        || !second.module_start_entry_returned || second.module_start_entry_result
        || !second.source_close_called || !second.source_close_returned || second.file_live
        || !second.module_file_closed_while_input_live || second.module_stop_called
        || second.module_initialization_blocked || second.module_initialization_abort_called || second.module_initialization_aborted
        || first.token.uid <= 0 || second.token.uid <= 0 || first.token.uid == second.token.uid
        || first.token.owner != second.token.owner || first.token.generation != second.token.generation
        || first.token.serial == second.token.serial || !SameNativeModuleToken(record.token, second.token)
        || !second.staging_full_read || !second.staging_hash_matches || !second.staging_borrow_live
        || second.staging_known_bytes != 19524 || second.staging_transferred != 19524
        || second.staging_sha256 != "edfc95810a5881f7f752eee255693e75ba1ade6862b557126c5236c2b47bbca4"
        || record.input.host_address != second.staging_host_address || record.input.size != 19524
        || record.input.owner != second.staging_owner || record.input.generation != second.staging_generation
        || record.input.serial != second.staging_serial || record.input.source_coordinate != second.staging_source_coordinate)
        return false;
    if (!envelope.present || envelope.native1001_blocked || !envelope.source_start_returned
        || envelope.source_init_returned || envelope.source_decode_returned || envelope.source_stop_entered
        || envelope.source_stop_returned || envelope.source_stop_pending || envelope.source_release_calls_entered
        || envelope.source_release_calls_returned || !QueryEnvelopeEffectsExact(init, envelope, false)) return false;
    for (size_t i = 0; i < 4; ++i) {
        const auto& n = envelope.records[i];
        if (n.state != NativeCodecWorkspaceState::WorkspaceLive || n.original_equivalence_known
            || !n.policy_abi || std::string_view(n.policy_abi) != NativePreparedEnvelopeAbi
            || n.record_index != i || n.key.record_index != i || n.codec != (i < 2 ? 0x1000u : 0x1001u)
            || !n.native_decoder_find_entered || !n.native_decoder_find_returned || !n.native_decoder_found || !n.native_decoder_id
            || !n.context_allocated || !n.native_prepare_entered || !n.native_prepare_returned
            || !n.native_alloc_entered || !n.native_alloc_returned || n.decode_capability
            || n.api_codec_version != ((61u << 16) | (19u << 8) | 101u)
            || n.api_util_version != ((59u << 16) | (39u << 8) | 100u)
            || !n.native_policy_return || n.native_policy_result || n.native_error_class
            || !n.query_source_return || n.query_source_result || !n.get_edram_source_return || n.get_edram_source_result
            || !n.envelope_bytes || !n.envelope_alignment || n.envelope_alignment > 64
            || n.provisional_live || !n.allocation_attempted || !n.allocation_returned || !n.allocation_succeeded
            || !n.backend_allocate_entered || !n.backend_allocate_returned || n.backend_allocate_code
            || n.allocation_owner != second.staging_owner || n.allocation_generation != second.staging_generation
            || !n.allocation_serial || n.allocation_serial == second.staging_serial || n.host_address == second.staging_host_address
            || n.requested_bytes != ((n.envelope_bytes + 63u) | 63u) || n.committed_bytes < n.requested_bytes
            || !n.raw_coordinate || n.raw_coordinate == 0x00200000 || !n.aligned_coordinate
            || ((uint64_t(n.raw_coordinate) + 63u) & ~uint64_t(63u)) != n.aligned_coordinate
            || n.aligned_coordinate % 64 || !n.host_address || n.host_address % 64
            || !n.placement_constructed || !n.context_transferred || n.context_destroyed || n.storage_freed
            || n.context_allocations != 1 || n.context_frees || n.win32_error || n.release_source_entered
            || n.key.provider_module != std::array<uint64_t,4>{first.token.owner,first.token.generation,first.token.serial,uint64_t(first.token.uid)}
            || n.key.consumer_module != std::array<uint64_t,4>{second.token.owner,second.token.generation,second.token.serial,uint64_t(second.token.uid)}
            || n.key.provider != std::array<uint64_t,3>{provider.token.owner,provider.token.generation,provider.token.serial}
            || n.key.provider_input != std::array<uint64_t,3>{first.staging_owner,first.staging_generation,first.staging_serial}
            || n.key.consumer_input != std::array<uint64_t,3>{second.staging_owner,second.staging_generation,second.staging_serial}
            || n.key.state != std::array<uint64_t,3>{init.owner,init.generation,init.serial}) return false;
        if (uint64_t(second.staging_source_coordinate) + second.staging_committed_bytes > n.raw_coordinate) return false;
        if (i < 2) {
            if (!n.format_known || n.format_pending || !n.native_open_entered || !n.native_open_returned
                || n.native_api_result || !n.decoder_opened || !n.decoder_ready
                || n.format_b1 != 0x28 || n.format_b2 != 0x5C || n.channels != 2 || n.max_input_bytes != 744) return false;
        } else if (n.format_known || !n.format_pending || n.native_open_entered || n.native_open_returned
            || n.decoder_opened || n.decoder_ready || n.format_b1 || n.format_b2 || n.channels || n.max_input_bytes) return false;
        if (i && uint64_t(envelope.records[i-1].raw_coordinate) + envelope.records[i-1].committed_bytes > n.raw_coordinate) return false;
        if (i && envelope.records[i-1].allocation_serial == n.allocation_serial) return false;
    }
    if (envelope.records[0].native_decoder_id != envelope.records[1].native_decoder_id
        || envelope.records[2].native_decoder_id != envelope.records[3].native_decoder_id
        || envelope.records[0].native_decoder_id == envelope.records[2].native_decoder_id) return false;
    for (size_t i = 0; i < record.imports.size(); ++i) {
        const auto& item = record.imports[i]; const bool partial = i == 2 || i == 5 || i == 6;
        if (item.resolved || item.attributes != (i < 2 ? 1 : 0x4001) || item.version
            || item.partial_callable != partial) return false;
        if (partial && (!SameNativeModuleToken(item.provider_module, first.token)
            || item.provider_owner != provider.token.owner || item.provider_generation != provider.token.generation
            || item.provider_serial != provider.token.serial)) return false;
    }
    return true;
}
[[maybe_unused]] bool AtPreparedEnvelopeBoundary(const dokodemo::StartupObservation& observation) {
    if (observation.module_observations.size() != 3 || observation.module_records.size() != 2
        || observation.current_module_index != 2 || observation.barrier_source_pc != 0x4F53C
        || observation.current_frontier_source_pc != 0x4F53C || observation.barrier_module_relative
        || observation.current_frontier_module_relative) return false;
    const auto& third = observation.module_observations[2];
    if (third.request_index != 2 || third.wrapper_callsite != 0x4F660
        || third.path != "disc0:/PSP_GAME/USRDIR/prx/sc_sascore.prx" || third.flags
        || third.options != std::vector<uint8_t>{20,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0}
        || third.request_owner != observation.owner || !third.pending || third.source_fd < 0 || !third.file_live
        || !third.file_native_object || !third.file_backend_owner || !third.file_backend_generation || !third.file_backend_serial
        || third.token.uid || third.module_uid_assigned || third.module_load_returned || third.module_start_called
        || third.record_live || third.module_input_live || third.staging_host_read || third.staging_live
        || third.staging_borrow_live || third.staging_transferred || third.staging_known_bytes || third.source_close_called) return false;
    return PreparedEnvelopeModulesReady(observation);
}
bool QueryEnvelopeModulesClosed(const dokodemo::StartupObservation& observation,bool require_complete_events=true) {
    using namespace dokodemo::recovered;
    if (observation.module_observations.size() < 2 || !observation.module_records.empty()
        || !observation.module_providers.empty() || !observation.caller_joined || !observation.worker_joined
        || !observation.resources_closed || observation.callback_dispatch_count != 1
        || observation.callback_dispatch_thread != observation.worker_thread_id || (require_complete_events&&observation.dropped_events)
        || dokodemo::StartupCoordinator::PendingDeferredCodecCleanup()) return false;
    const auto& first = observation.module_observations[0];
    const auto& second = observation.module_observations[1];
    for (size_t i = 0; i < 2; ++i) {
        const auto& item = observation.module_observations[i];
        if (item.record_live || item.file_live || item.staging_live || item.staging_borrow_live || item.module_input_live
            || !item.module_stop_called || !item.module_stop_returned || item.module_stop_result
            || !item.module_unloaded || !item.module_input_released || !item.staging_freed_after_joins
            || item.module_initialization_abort_called || item.module_initialization_aborted
            || !item.source_close_called || !item.source_close_returned || item.record.owned_native_codec_resources) return false;
    }
    const auto& e = second.record.prepared_envelope;
    if (!e.present || !e.source_start_returned || !e.source_stop_entered || !e.source_stop_returned
        || e.source_stop_pending || e.source_stop_next_record != 4 || e.source_stop_records_completed != 4
        || e.source_init_returned || e.source_decode_returned || e.source_release_calls_entered != 4
        || e.source_release_calls_returned != 4 || !QueryEnvelopeEffectsExact(second.record.initialization,e,true)
        || first.record.owned_native_codec_resources) return false;
    for (const auto& n : e.records) {
        if (n.state != NativeCodecWorkspaceState::Retired || n.provisional_live || !n.context_destroyed || !n.storage_freed
            || n.context_allocations != 1 || n.context_frees != 1 || n.decoder_ready || n.decode_capability
            || !n.release_source_entered || !n.release_source_return || n.release_source_result
            || !n.windows_release_capability_applied || !n.release_map_validated || !n.release_native_completed
            || !n.release_native_attempts || !n.release_native_returns || n.release_native_code
            || n.original_low29_guard_passed != ((n.raw_coordinate & 0x1FFFFFFFu) <= 0x001FFFFFu)) return false;
    }
    return true;
}

bool QueryEnvelopeSequenceClosed(const dokodemo::StartupObservation& observation) {
    if (observation.module_observations.size() != 3 || !QueryEnvelopeModulesClosed(observation)) return false;
    const auto& third = observation.module_observations[2];
    return third.host_file_closed && !third.file_live && !third.pending && !third.record_live
        && !third.module_uid_assigned && !third.module_load_returned && !third.module_start_called
        && !third.module_stop_called && !third.source_close_called && !third.module_input_live
        && !third.staging_live && !third.staging_borrow_live && !third.staging_host_read;
}

bool SascoreStateExact(const dokodemo::recovered::NativeModuleSnapshot& record, bool closed) {
    using namespace dokodemo::recovered;
    const auto& s=record.sascore; const auto& r=s.resources; const auto& t=record.token;
    if (!s.present || !s.entry_entered || !s.entry_returned || s.entry_result || s.dependency_blocked
        || s.host_abort_required || s.host_abort_entered || s.host_aborted || !s.stop_domain_ready
        || s.source_stop_entered!=closed || s.source_stop_returned!=closed || s.stop_progress!=(closed?2u:0u)
        || !s.owner || s.owner!=t.owner || s.generation!=t.generation || s.serial!=t.serial || !s.native_state_address
        || s.source_segment!=1 || s.source_segment_va!=0x1DD0 || s.private_bytes!=20
        || s.initial_source_known_bytes!=4 || s.known_bytes!=8 || s.writes_count!=2
        || s.frontier_source_pc || s.frontier_stub_va || s.frontier_nid || s.frontier_import_index
        || s.calloc_count!=46644 || s.calloc_size!=1 || !s.semaphore_name || std::string_view(s.semaphore_name)!="SceSas"
        || s.semaphore_attributes || s.semaphore_initial!=1 || s.semaphore_maximum!=1 || !s.semaphore_options_null
        || s.calloc_calls_entered!=1 || s.calloc_calls_returned!=1 || s.create_calls_entered!=1 || s.create_calls_returned!=1
        || s.free_calls_entered!=(closed?1u:0u) || s.free_calls_returned!=(closed?1u:0u)
        || s.delete_calls_entered!=(closed?1u:0u) || s.delete_calls_returned!=(closed?1u:0u)) return false;
    if (!r.policy_abi || std::string_view(r.policy_abi)!=NativeSascoreAbi || !r.issued || r.retired!=closed || r.input_pinned==closed
        || r.key.module!=std::array<uint64_t,4>{t.owner,t.generation,t.serial,uint64_t(t.uid)}
        || r.key.input!=std::array<uint64_t,3>{record.input.owner,record.input.generation,record.input.serial}
        || r.key.state!=std::array<uint64_t,3>{s.owner,s.generation,s.serial}
        || !r.allocation_entered || !r.allocation_returned || r.allocation_backend_code || r.allocation_error
        || r.memory_live==closed || !r.memory_map_validated || !r.calloc_source_returned
        || !r.allocation_owner || r.allocation_owner!=record.input.owner || r.allocation_generation!=record.input.generation
        || !r.allocation_serial || r.allocation_serial==record.input.serial || !r.memory_host_address
        || r.memory_host_address==record.input.host_address || r.requested_bytes!=46644 || r.zeroed_bytes!=46644
        || !r.page_bytes || r.committed_bytes<r.requested_bytes || r.committed_bytes%r.page_bytes
        || !r.source_coordinate || r.source_coordinate%r.page_bytes || r.memory_host_address%r.page_bytes
        || uint64_t(r.source_coordinate)+r.committed_bytes>(uint64_t(1)<<32)
        || uint64_t(record.input.source_coordinate)+record.input.size>r.source_coordinate
        || !r.semaphore_create_entered || !r.semaphore_create_returned || !r.semaphore_source_returned
        || r.semaphore_live==closed || r.semaphore_uid<=0 || !r.semaphore_handle || r.semaphore_create_error
        || r.allocations!=1 || r.frees!=(closed?1u:0u) || r.semaphore_creates!=1 || r.semaphore_closes!=(closed?1u:0u)
        || r.free_attempts!=(closed?1u:0u) || r.free_returns!=(closed?1u:0u)
        || r.delete_attempts!=(closed?1u:0u) || r.delete_returns!=(closed?1u:0u)
        || r.free_backend_code || r.free_error || r.delete_error || r.delete_native_succeeded!=closed
        || r.memory_freed!=closed || r.semaphore_closed!=closed || r.free_source_returned!=closed
        || r.delete_source_returned!=closed || (closed && r.delete_source_result)
        || r.retirement_entered!=closed || r.retirement_returned!=closed) return false;
    const std::array<uint32_t,2> pcs{0x1A98,0x1AC0}, offsets{16,0}, values{r.source_coordinate,uint32_t(r.semaphore_uid)};
    for(size_t i=0;i<s.writes.size();++i) {
        const auto& w=s.writes[i];
        if(w.ordinal!=i || w.source_pc!=pcs[i] || w.offset!=offsets[i] || w.value!=values[i]
            || !w.value_origin || std::string_view(w.value_origin)!="NativePlatform") return false;
    }
    for(size_t i=0;i<s.known.size();++i) {
        const bool known=i<4 || i>=16;
        if(s.known[i]!=(known?1:0)) return false;
        if(known) {
            const uint32_t value=i<4?uint32_t(r.semaphore_uid):r.source_coordinate;
            const size_t shift=i<4?i:i-16;
            if(s.bytes[i]!=uint8_t(value>>(shift*8))) return false;
        }
    }
    return true;
}
bool SascoreRequestExact(const dokodemo::StartupModuleObservation& item, bool closed) {
    using namespace dokodemo::recovered;
    const auto& record=item.record;
    if(item.request_index!=2 || item.wrapper_callsite!=0x4F660 || item.path!="disc0:/PSP_GAME/USRDIR/prx/sc_sascore.prx"
        || item.flags || item.pending || item.options!=std::vector<uint8_t>{20,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0}
        || item.source_fd<0 || !item.file_native_object || !item.file_backend_owner || !item.file_backend_generation || !item.file_backend_serial
        || item.encoded_file_bytes!=5920 || item.file_live || !item.module_uid_assigned || !item.module_load_returned
        || item.token.uid<=0 || item.module_load_result!=item.token.uid || !item.module_start_called || !item.module_start_returned
        || item.module_start_result || !item.module_start_entry_returned || item.module_start_entry_result
        || !item.source_close_called || !item.source_close_returned || !item.module_file_closed_while_input_live
        || item.module_initialization_blocked || item.module_initialization_abort_called || item.module_initialization_aborted
        || item.record_live==closed || item.module_input_live==closed || item.staging_live==closed || item.staging_borrow_live==closed
        || !item.staging_host_read || !item.staging_full_read || !item.staging_hash_matches || item.staging_read_error
        || item.staging_file_position || item.staging_requested!=5920 || item.staging_transferred!=5920 || item.staging_known_bytes!=5920
        || item.staging_sha256!="f4cbbc90733c6b48b5a6b0d0c8b827f3478333b2a2d0c87b845231ab1916a0f5"
        || item.staging_owner!=item.file_backend_owner || item.staging_generation!=item.file_backend_generation
        || !item.staging_serial || !item.staging_host_address || !item.staging_page_bytes
        || item.staging_committed_bytes<5920 || item.staging_committed_bytes%item.staging_page_bytes
        || uint64_t(item.staging_source_coordinate)+item.staging_committed_bytes>record.sascore.resources.source_coordinate
        || item.module_stop_called!=closed || item.module_stop_returned!=closed
        || (closed && item.module_stop_result) || item.module_unloaded!=closed || item.module_input_released!=closed
        || item.staging_freed_after_joins!=closed || record.domain!=NativeModuleDomain::Production
        || record.state!=(closed?NativeModuleState::Stopped:NativeModuleState::Started) || record.input_live==closed
        || record.name!="sceSAScore" || record.encoded_bytes!=5920 || record.plaintext_bytes!=11184
        || record.encoded_sha256!=item.staging_sha256
        || record.plaintext_sha256!="a6adccd8faa74a131ec34a2efa5e0d827204f2993bc7e55ad3b3a8f98d4b3a34"
        || record.policy!="WindowsSascoreExact0009_0001LifecycleV1" || record.start_source_va!=0x1A68 || record.stop_source_va!=0x1ACC
        || !SameNativeModuleToken(record.token,item.token) || record.active_calls || record.owned_native_codec_resources
        || record.input.owner!=item.staging_owner || record.input.generation!=item.staging_generation || record.input.serial!=item.staging_serial
        || record.input.host_address!=item.staging_host_address || record.input.source_coordinate!=item.staging_source_coordinate
        || record.input.size!=5920 || record.imports.size()!=10 || record.exports.size()!=25 || !SascoreStateExact(record,closed)) return false;
    constexpr std::array<uint32_t,10> nids{0x3D5F109C,0xC86500E3,0x5AD660FA,0xB435DEC5,0x34B9FA9E,0xBFA98062,0xD6DA4BA1,0x28B6489C,0x3F53E640,0x4E3A1105};
    constexpr std::array<uint32_t,10> stubs{0x1B54,0x1B5C,0x1B64,0x1B3C,0x1B44,0x1B4C,0x1B1C,0x1B24,0x1B2C,0x1B34};
    for(size_t i=0;i<nids.size();++i) {
        const auto& import=record.imports[i];
        const char* library=i==0?"sceMeCore_driver":i<3?"sceMeMemory_driver":i<6?"UtilsForKernel":"ThreadManForKernel";
        if(import.nid!=nids[i] || import.source_stub_va!=stubs[i] || import.library!=library || import.version
            || import.attributes!=(i<6?9:1) || import.resolved || import.partial_callable || import.provider_serial
            || import.provider_owner || import.provider_generation) return false;
    }
    if(std::count_if(record.exports.begin(),record.exports.end(),[](const auto& e){return e.lifecycle_callable;})!=2) return false;
    for(const auto& e:record.exports) {
        const bool callable=e.library.empty() && e.kind=="function" &&
            ((e.nid==0xD632ACDB && e.relative_va==0x1A68) || (e.nid==0xCEE8593C && e.relative_va==0x1ACC));
        if(e.lifecycle_callable!=callable || e.partial_callable) return false;
    }
    return true;
}
[[maybe_unused]] bool AtSascoreBoundary(const dokodemo::StartupObservation& observation) {
    if(observation.module_observations.size()!=4 || observation.module_records.size()!=3 || observation.current_module_index!=3
        || observation.barrier_source_pc!=0x4F53C || observation.current_frontier_source_pc!=0x4F53C
        || observation.barrier_module_relative || observation.current_frontier_module_relative
        || !PreparedEnvelopeModulesReady(observation) || !SascoreRequestExact(observation.module_observations[2],false)
        || dokodemo::PendingWindowsSascoreCleanup()) return false;
    const auto& third=observation.module_observations[2]; const auto& fourth=observation.module_observations[3];
    if(third.request_owner!=observation.owner || fourth.request_owner!=observation.owner || fourth.request_index!=3
        || fourth.wrapper_callsite!=0x4F680 || fourth.path!="disc0:/PSP_GAME/USRDIR/prx/ifhandle.prx" || fourth.flags
        || fourth.options!=std::vector<uint8_t>{20,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0}
        || !fourth.pending || fourth.source_fd<0 || !fourth.file_live || !fourth.file_native_object
        || !fourth.file_backend_owner || !fourth.file_backend_generation || !fourth.file_backend_serial
        || fourth.frontier_source_pc!=0x4F53C || fourth.frontier_module_relative || fourth.module_uid_assigned
        || fourth.module_load_returned || fourth.module_start_called || fourth.source_close_called || fourth.record_live
        || fourth.module_input_live || fourth.staging_host_read || fourth.staging_live || fourth.staging_borrow_live
        || fourth.staging_transferred || fourth.staging_known_bytes) return false;
    for(size_t i=0;i<3;++i) {
        const auto& item=observation.module_observations[i];
        if(!SameNativeModuleToken(item.token,observation.module_records[i].token)) return false;
        for(size_t j=i+1;j<3;++j) {
            const auto& later=observation.module_observations[j];
            if(item.token.uid==later.token.uid || item.token.serial==later.token.serial
                || item.token.owner!=later.token.owner || item.token.generation!=later.token.generation
                || uint64_t(item.staging_source_coordinate)+item.staging_committed_bytes>later.staging_source_coordinate) return false;
        }
    }
    return true;
}
bool SascoreSequenceClosed(const dokodemo::StartupObservation& observation) {
    if(observation.module_observations.size()!=4 || !QueryEnvelopeModulesClosed(observation)
        || !SascoreRequestExact(observation.module_observations[2],true) || dokodemo::PendingWindowsSascoreCleanup()) return false;
    const auto& fourth=observation.module_observations[3];
    return fourth.host_file_closed && !fourth.file_live && !fourth.pending && !fourth.record_live
        && !fourth.module_uid_assigned && !fourth.module_load_returned && !fourth.module_start_called
        && !fourth.module_stop_called && !fourth.source_close_called && !fourth.module_input_live
        && !fourth.staging_live && !fourth.staging_borrow_live && !fourth.staging_host_read;
}

bool IfhandleRequestExact(const dokodemo::StartupModuleObservation& item, bool closed) {
    using namespace dokodemo::recovered;
    const auto& record=item.record; const auto& s=record.ifhandle;
    if(item.request_index!=3 || item.wrapper_callsite!=0x4F680 || item.path!="disc0:/PSP_GAME/USRDIR/prx/ifhandle.prx"
        || item.flags || item.pending || item.options!=std::vector<uint8_t>{20,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0}
        || item.source_fd<0 || !item.file_native_object || !item.file_backend_owner || !item.file_backend_generation || !item.file_backend_serial
        || item.encoded_file_bytes!=10832 || item.file_live || !item.module_uid_assigned || !item.module_load_returned
        || item.token.uid<=0 || item.module_load_result!=item.token.uid || !item.module_start_called || !item.module_start_returned
        || item.module_start_result || !item.module_start_entry_returned || item.module_start_entry_result
        || !item.source_close_called || !item.source_close_returned || item.source_close_result || !item.module_file_closed_while_input_live
        || item.module_initialization_blocked || item.module_initialization_abort_called || item.module_initialization_aborted
        || item.record_live==closed || item.module_input_live==closed || item.staging_live==closed || item.staging_borrow_live==closed
        || !item.staging_host_read || !item.staging_full_read || !item.staging_hash_matches || item.staging_read_error
        || item.staging_file_position || item.staging_requested!=10832 || item.staging_transferred!=10832 || item.staging_known_bytes!=10832
        || item.staging_sha256!="c2803caab3ad288f91e03f5ac7cc5c5c05d8f945f92cfc55ca42da60d6fd916e"
        || item.staging_owner!=item.file_backend_owner || item.staging_generation!=item.file_backend_generation
        || !item.staging_serial || !item.staging_host_address || !item.staging_page_bytes
        || item.staging_committed_bytes<10832 || item.staging_committed_bytes%item.staging_page_bytes
        || item.module_stop_called!=closed || item.module_stop_returned!=closed
        || (closed && item.module_stop_result) || item.module_unloaded!=closed || item.module_input_released!=closed
        || item.staging_freed_after_joins!=closed || record.domain!=NativeModuleDomain::Production
        || record.state!=(closed?NativeModuleState::Stopped:NativeModuleState::Started) || record.input_live==closed
        || record.name!="sceNetIfhandle_Service" || record.encoded_bytes!=10832 || record.plaintext_bytes!=21824
        || record.encoded_sha256!=item.staging_sha256
        || record.plaintext_sha256!="355458da4d29a98808ce3fb52423a0f6de5b8a450039220f885f3b1c2102f12b"
        || record.policy!="WindowsIfhandleExact0001_0009LifecycleV1" || record.start_source_va!=0x3454 || record.stop_source_va!=0x345C
        || !SameNativeModuleToken(record.token,item.token) || record.active_calls || record.owned_native_codec_resources
        || record.owned_providers || record.incoming_bindings || record.partial_start_supported || record.entry_frontier_source_pc
        || record.input.owner!=item.staging_owner || record.input.generation!=item.staging_generation || record.input.serial!=item.staging_serial
        || record.input.host_address!=item.staging_host_address || record.input.source_coordinate!=item.staging_source_coordinate
        || record.input.size!=10832 || record.imports.size()!=20 || record.exports.size()!=92
        || !s.present || s.owner!=item.token.owner || s.generation!=item.token.generation || s.serial!=item.token.serial
        || !s.native_state_address || s.native_state_address==record.input.host_address || s.source_segment!=1 || s.source_segment_va!=0x3B20
        || s.private_bytes!=2240 || s.initial_source_known_bytes!=16 || s.known_bytes!=16 || s.writes_count) return false;
    constexpr std::array<uint8_t,16> file{255,255,255,255,255,255,0,0,255,255,255,255,255,255,255,255};
    for(size_t i=0;i<s.known.size();++i) {
        if(s.known[i]!=(i<16?1:0) || (i<16 && s.bytes[i]!=file[i])) return false;
    }
    constexpr std::array<uint32_t,20> nids{0xC0AB8932,0xB49A7697,0xAB7592FF,0x10F3BB61,0x81D0D1F7,
        0xD6DA4BA1,0x28B6489C,0x3F53E640,0x4E3A1105,0x58B1F937,0x1FB15A32,0x56C039B5,0x89B3D48C,0xAF36D708,0xB736E9FF,0x39810265,0x369ED59D,
        0x092968F4,0x5F10D406,0x84F370BC};
    for(size_t i=0;i<nids.size();++i) {
        const auto& imp=record.imports[i];
        const char* lib=i<5?"SysclibForKernel":i<17?"ThreadManForKernel":i<19?"InterruptManagerForKernel":"KDebugForKernel";
        const uint32_t stub=i<17?uint32_t(0x347C+8*i):i<19?uint32_t(0x3464+8*(i-17)):0x3474;
        if(imp.library!=lib || imp.version || imp.attributes!=(i>=17&&i<19?9:1) || imp.nid!=nids[i] || imp.source_stub_va!=stub
            || imp.resolved || imp.partial_callable || imp.provider_serial || imp.provider_owner || imp.provider_generation) return false;
    }
    if(std::count_if(record.exports.begin(),record.exports.end(),[](const auto& e){return e.lifecycle_callable;})!=2) return false;
    for(const auto& e:record.exports) {
        const bool callable=e.library.empty() && e.kind=="function" &&
            ((e.nid==0xD632ACDB && e.relative_va==0x3454) || (e.nid==0xCEE8593C && e.relative_va==0x345C));
        if(e.lifecycle_callable!=callable || e.partial_callable) return false;
    }
    return true;
}
[[maybe_unused]] bool AtIfhandleBoundary(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=5 || o.module_records.size()!=4 || o.current_module_index!=4
        || o.barrier_source_pc!=0x4F53C || o.current_frontier_source_pc!=0x4F53C
        || o.barrier_module_relative || o.current_frontier_module_relative
        || !PreparedEnvelopeModulesReady(o) || !SascoreRequestExact(o.module_observations[2],false)
        || !IfhandleRequestExact(o.module_observations[3],false) || dokodemo::PendingWindowsSascoreCleanup()) return false;
    const auto& next=o.module_observations[4];
    if(next.request_owner!=o.owner || next.request_index!=4 || next.wrapper_callsite!=0x4F6A0
        || next.path!="disc0:/PSP_GAME/USRDIR/prx/pspnet.prx" || next.flags
        || next.options!=std::vector<uint8_t>{20,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1,0,0}
        || !next.pending || next.source_fd<0 || !next.file_live || !next.file_native_object
        || !next.file_backend_owner || !next.file_backend_generation || !next.file_backend_serial
        || next.frontier_source_pc!=0x4F53C || next.frontier_module_relative || next.module_uid_assigned
        || next.module_load_returned || next.module_start_called || next.source_close_called || next.record_live
        || next.module_input_live || next.staging_host_read || next.staging_live || next.staging_borrow_live
        || next.staging_transferred || next.staging_known_bytes) return false;
    const auto& sas=o.module_observations[2].record.sascore.resources;
    if(uint64_t(sas.source_coordinate)+sas.committed_bytes>o.module_observations[3].staging_source_coordinate) return false;
    for(size_t i=0;i<4;++i) {
        const auto& item=o.module_observations[i];
        if(item.request_owner!=o.owner || !SameNativeModuleToken(item.token,o.module_records[i].token)) return false;
        for(size_t j=i+1;j<4;++j) {
            const auto& later=o.module_observations[j];
            if(item.token.uid==later.token.uid || item.token.serial==later.token.serial
                || item.token.owner!=later.token.owner || item.token.generation!=later.token.generation
                || uint64_t(item.staging_source_coordinate)+item.staging_committed_bytes>later.staging_source_coordinate) return false;
        }
    }
    return true;
}
bool IfhandleSequenceClosed(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=5 || !QueryEnvelopeModulesClosed(o)
        || !SascoreRequestExact(o.module_observations[2],true) || !IfhandleRequestExact(o.module_observations[3],true)
        || dokodemo::PendingWindowsSascoreCleanup()) return false;
    const auto& next=o.module_observations[4];
    return next.host_file_closed && !next.file_live && !next.pending && !next.record_live
        && !next.module_uid_assigned && !next.module_load_returned && !next.module_start_called && !next.module_stop_called
        && !next.source_close_called && !next.module_input_live && !next.staging_live && !next.staging_borrow_live && !next.staging_host_read;
}

bool PspnetRequestExact(const dokodemo::StartupModuleObservation& item, bool closed) {
    using namespace dokodemo::recovered;
    const auto& record=item.record; const auto& s=record.pspnet;
    if(item.request_index!=4 || item.wrapper_callsite!=0x4F6A0 || item.path!="disc0:/PSP_GAME/USRDIR/prx/pspnet.prx"
        || item.flags || item.pending || item.options!=std::vector<uint8_t>{20,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1,0,0}
        || item.source_fd<0 || !item.file_native_object || !item.file_backend_owner || !item.file_backend_generation || !item.file_backend_serial
        || item.encoded_file_bytes!=60256 || item.file_live || !item.module_uid_assigned || !item.module_load_returned
        || item.token.uid<=0 || item.module_load_result!=item.token.uid || !item.module_start_called || !item.module_start_returned
        || item.module_start_result || !item.module_start_entry_returned || item.module_start_entry_result
        || !item.source_close_called || !item.source_close_returned || item.source_close_result || !item.module_file_closed_while_input_live
        || item.module_initialization_blocked || item.module_initialization_abort_called || item.module_initialization_aborted
        || item.record_live==closed || item.module_input_live==closed || item.staging_live==closed || item.staging_borrow_live==closed
        || !item.staging_host_read || !item.staging_full_read || !item.staging_hash_matches || item.staging_read_error
        || item.staging_file_position || item.staging_requested!=60256 || item.staging_transferred!=60256 || item.staging_known_bytes!=60256
        || item.staging_sha256!="ab8b28e24f1d17c6bfffec47b6d9717665fab78b0de11d3d2711ad25b5c65404"
        || item.staging_owner!=item.file_backend_owner || item.staging_generation!=item.file_backend_generation
        || !item.staging_serial || !item.staging_host_address || !item.staging_page_bytes
        || item.staging_committed_bytes<60256 || item.staging_committed_bytes%item.staging_page_bytes
        || item.module_stop_called!=closed || item.module_stop_returned!=closed
        || (closed && item.module_stop_result) || item.module_unloaded!=closed || item.module_input_released!=closed
        || item.staging_freed_after_joins!=closed || record.domain!=NativeModuleDomain::Production
        || record.state!=(closed?NativeModuleState::Stopped:NativeModuleState::Started) || record.input_live==closed
        || record.name!="sceNet_Library" || record.encoded_bytes!=60256 || record.plaintext_bytes!=60256
        || record.encoded_sha256!=item.staging_sha256
        || record.plaintext_sha256!="ab8b28e24f1d17c6bfffec47b6d9717665fab78b0de11d3d2711ad25b5c65404"
        || record.policy!="WindowsPspnetExact0001_4001LifecycleV1" || record.start_source_va!=0x7B78 || record.stop_source_va!=0x7B80
        || !SameNativeModuleToken(record.token,item.token) || record.active_calls || record.owned_native_codec_resources
        || record.owned_providers || record.incoming_bindings || record.partial_start_supported || record.entry_frontier_source_pc
        || record.input.owner!=item.staging_owner || record.input.generation!=item.staging_generation || record.input.serial!=item.staging_serial
        || record.input.host_address!=item.staging_host_address || record.input.source_coordinate!=item.staging_source_coordinate
        || record.input.size!=60256 || record.imports.size()!=42 || record.exports.size()!=107
        || !s.present || s.owner!=item.token.owner || s.generation!=item.token.generation || s.serial!=item.token.serial
        || !s.native_state_address || s.native_state_address==record.input.host_address || s.source_segment!=1 || s.source_segment_va!=0xA090
        || s.private_bytes!=6848 || s.initial_source_known_bytes!=192 || s.known_bytes!=192 || s.writes_count) return false;
    constexpr std::array<uint8_t,192> file{255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,0,32,0,0,50,0,0,0,255,255,255,255,255,255,0,0,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,0,0,0,0,0,32,32,32,32,32,32,32,32,32,8,8,8,8,8,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,24,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,4,4,4,4,4,4,4,4,4,4,16,16,16,16,16,16,16,65,65,65,65,65,65,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,16,16,16,16,16,16,66,66,66,66,66,66,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,16,16,16,16,32,0,0,0};
    for(size_t i=0;i<s.known.size();++i) {
        if(s.known[i]!=(i<192?1:0) || (i<192 && s.bytes[i]!=file[i])) return false;
    }
    struct ExpectedImport { const char* library; uint16_t version,attributes; uint32_t nid,stub; };
    constexpr std::array<ExpectedImport,42> imports{{
        {"sceNetIfhandle_lib",0,0x4001,0x30F69334,0x93A4},
        {"sceNetIfhandle_lib",0,0x4001,0xB9096E48,0x93AC},
        {"sceNetIfhandle_lib",0,0x4001,0xB8188F96,0x93B4},
        {"sceNetIfhandle_lib",0,0x4001,0x4FB43BCE,0x93BC},
        {"sceNetIfhandle_lib",0,0x4001,0xB1F5BB87,0x93C4},
        {"sceNetIfhandle_lib",0,0x4001,0x8FCB05A1,0x93CC},
        {"sceNetIfhandle_lib",0,0x4001,0xEAD3A759,0x93D4},
        {"sceNetIfhandle_lib",0,0x4001,0x0296C7D6,0x93DC},
        {"sceNetIfhandle_lib",0,0x4001,0xE440A7D8,0x93E4},
        {"sceNetIfhandle_lib",0,0x4001,0x30602CE9,0x93EC},
        {"sceNetIfhandle_lib",0,0x4001,0xD5DA7B3C,0x93F4},
        {"sceNetIfhandle_lib",0,0x4001,0x2162EE67,0x93FC},
        {"sceNetIfhandle_lib",0,0x4001,0x0C391E9F,0x9404},
        {"sceNetIfhandle_lib",0,0x4001,0x29ED84C5,0x940C},
        {"sceNetIfhandle_lib",0,0x4001,0x15CFE3C0,0x9414},
        {"sceNetIfhandle_lib",0,0x4001,0x76BAD213,0x941C},
        {"sceNetIfhandle_lib",0,0x4001,0x4C2886CB,0x9424},
        {"sceNetIfhandle_lib",0,0x4001,0x0FB8AE0D,0x942C},
        {"sceNetIfhandle_lib",0,0x4001,0xBFF3CEA5,0x9434},
        {"sceNetIfhandle_lib",0,0x4001,0xF56FAC82,0x943C},
        {"Kernel_Library",0,0x0001,0x092968F4,0x92F4},
        {"Kernel_Library",0,0x0001,0x5F10D406,0x92FC},
        {"ThreadManForUser",0,0x4001,0x446D8DE6,0x9304},
        {"ThreadManForUser",0,0x4001,0x9FA03CD3,0x930C},
        {"ThreadManForUser",0,0x4001,0xF475845D,0x9314},
        {"ThreadManForUser",0,0x4001,0x616403BA,0x931C},
        {"ThreadManForUser",0,0x4001,0x293B45B8,0x9324},
        {"ThreadManForUser",0,0x4001,0xCEADEB47,0x932C},
        {"ThreadManForUser",0,0x4001,0xD6DA4BA1,0x9334},
        {"ThreadManForUser",0,0x4001,0x28B6489C,0x933C},
        {"ThreadManForUser",0,0x4001,0x3F53E640,0x9344},
        {"ThreadManForUser",0,0x4001,0x4E3A1105,0x934C},
        {"ThreadManForUser",0,0x4001,0x55C20A00,0x9354},
        {"ThreadManForUser",0,0x4001,0xEF9E4C70,0x935C},
        {"ThreadManForUser",0,0x4001,0x1FB15A32,0x9364},
        {"ThreadManForUser",0,0x4001,0x402FCF22,0x936C},
        {"ThreadManForUser",0,0x4001,0xBA6B92E2,0x9374},
        {"ThreadManForUser",0,0x4001,0xDB738F35,0x937C},
        {"ThreadManForUser",0,0x4001,0x82BC5777,0x9384},
        {"ThreadManForUser",0,0x4001,0x369ED59D,0x938C},
        {"ThreadManForUser",0,0x4001,0x6652B8CA,0x9394},
        {"ThreadManForUser",0,0x4001,0x7E65B999,0x939C},
    }};
    for(size_t i=0;i<imports.size();++i) {
        const auto& actual=record.imports[i]; const auto& expected=imports[i];
        if(actual.library!=expected.library || actual.version!=expected.version || actual.attributes!=expected.attributes
            || actual.nid!=expected.nid || actual.source_stub_va!=expected.stub || actual.resolved || actual.partial_callable
            || actual.provider_serial || actual.provider_owner || actual.provider_generation) return false;
    }
    if(std::count_if(record.exports.begin(),record.exports.end(),[](const auto& e){return e.lifecycle_callable;})!=2) return false;
    for(const auto& e:record.exports) {
        const bool callable=e.library.empty() && e.kind=="function" &&
            ((e.nid==0xD632ACDB && e.relative_va==0x7B78) || (e.nid==0xCEE8593C && e.relative_va==0x7B80));
        if(e.lifecycle_callable!=callable || e.partial_callable) return false;
    }
    return true;
}
[[maybe_unused]] bool AtPspnetBoundary(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=6 || o.module_records.size()!=5 || o.current_module_index!=5
        || o.barrier_source_pc!=0x4F53C || o.current_frontier_source_pc!=0x4F53C
        || o.barrier_module_relative || o.current_frontier_module_relative
        || !PreparedEnvelopeModulesReady(o) || !SascoreRequestExact(o.module_observations[2],false)
        || !IfhandleRequestExact(o.module_observations[3],false) || !PspnetRequestExact(o.module_observations[4],false)
        || dokodemo::PendingWindowsSascoreCleanup()) return false;
    const auto& next=o.module_observations[5];
    if(next.request_owner!=o.owner || next.request_index!=5 || next.wrapper_callsite!=0x4F71C
        || next.path!="disc0:/PSP_GAME/USRDIR/prx/memab.prx" || next.flags
        || next.options!=std::vector<uint8_t>{20,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0}
        || !next.pending || next.source_fd<0 || !next.file_live || !next.file_native_object
        || !next.file_backend_owner || !next.file_backend_generation || !next.file_backend_serial
        || next.frontier_source_pc!=0x4F53C || next.frontier_module_relative || next.module_uid_assigned
        || next.module_load_returned || next.module_start_called || next.source_close_called || next.record_live
        || next.module_input_live || next.staging_host_read || next.staging_live || next.staging_borrow_live
        || next.staging_transferred || next.staging_known_bytes) return false;
    const auto& sas=o.module_observations[2].record.sascore.resources;
    if(uint64_t(sas.source_coordinate)+sas.committed_bytes>o.module_observations[3].staging_source_coordinate) return false;
    for(size_t i=0;i<5;++i) {
        const auto& item=o.module_observations[i];
        if(item.request_owner!=o.owner || !SameNativeModuleToken(item.token,o.module_records[i].token)) return false;
        for(size_t j=i+1;j<5;++j) {
            const auto& later=o.module_observations[j];
            if(item.token.uid==later.token.uid || item.token.serial==later.token.serial
                || item.token.owner!=later.token.owner || item.token.generation!=later.token.generation
                || uint64_t(item.staging_source_coordinate)+item.staging_committed_bytes>later.staging_source_coordinate) return false;
        }
    }
    return true;
}
bool PspnetSequenceClosed(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=6 || !QueryEnvelopeModulesClosed(o)
        || !SascoreRequestExact(o.module_observations[2],true) || !IfhandleRequestExact(o.module_observations[3],true)
        || !PspnetRequestExact(o.module_observations[4],true) || dokodemo::PendingWindowsSascoreCleanup()) return false;
    const auto& next=o.module_observations[5];
    return next.host_file_closed && !next.file_live && !next.pending && !next.record_live
        && !next.module_uid_assigned && !next.module_load_returned && !next.module_start_called && !next.module_stop_called
        && !next.source_close_called && !next.module_input_live && !next.staging_live && !next.staging_borrow_live && !next.staging_host_read;
}
bool MemabRequestExact(const dokodemo::StartupModuleObservation& item,bool closed) {
    using namespace dokodemo::recovered;
    const auto& record=item.record;const auto& s=record.memab;const auto& r=s.resources;
    if(item.request_index!=5 || item.repeated_record_index!=0 || item.wrapper_callsite!=0x4F71C
        || item.path!="disc0:/PSP_GAME/USRDIR/prx/memab.prx" || item.flags || item.pending
        || item.options!=std::vector<uint8_t>{20,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0}
        || item.source_fd<0 || !item.file_native_object || !item.file_backend_owner || !item.file_backend_generation
        || !item.file_backend_serial || item.file_live || item.encoded_file_bytes!=15216
        || !item.module_uid_assigned || !item.module_load_returned || item.token.uid<=0
        || item.module_load_result!=item.token.uid || !item.module_start_called || !item.module_start_returned
        || item.module_start_result || !item.module_start_entry_returned || item.module_start_entry_result
        || !item.source_close_called || !item.source_close_returned || item.source_close_result
        || !item.module_file_closed_while_input_live || item.module_initialization_blocked
        || item.module_initialization_abort_called || item.module_initialization_aborted
        || item.record_live==closed || item.module_input_live==closed || item.staging_live==closed
        || item.staging_borrow_live==closed || !item.staging_host_read || !item.staging_full_read
        || !item.staging_hash_matches || item.staging_read_error || item.staging_file_position
        || item.staging_requested!=15216 || item.staging_transferred!=15216 || item.staging_known_bytes!=15216
        || item.staging_sha256!="07f07637b64268df841892adf9e21ccc418c5c8641e60e44447ba74b1c31a1a8"
        || item.staging_owner!=item.file_backend_owner || item.staging_generation!=item.file_backend_generation
        || !item.staging_serial || !item.staging_host_address || !item.staging_page_bytes
        || item.staging_committed_bytes<15216 || item.staging_committed_bytes%item.staging_page_bytes
        || item.module_stop_called!=closed || item.module_stop_returned!=closed || (closed && item.module_stop_result)
        || item.module_unloaded!=closed || item.module_input_released!=closed || item.staging_freed_after_joins!=closed
        || record.domain!=NativeModuleDomain::Production
        || record.state!=(closed?NativeModuleState::Stopped:NativeModuleState::Started) || record.input_live==closed
        || record.name!="sceMemab" || record.encoded_bytes!=15216 || record.plaintext_bytes!=14880
        || record.encoded_sha256!=item.staging_sha256
        || record.plaintext_sha256!="a471025d3e3ecfd5a2ba4bcd3157fe54e5d620174e4bfa63d3d3de50c035b7c8"
        || record.policy!="WindowsMemabExact0001PrivateSemaphoreV1" || record.start_source_va!=0x9F4 || record.stop_source_va!=0xA34
        || !SameNativeModuleToken(record.token,item.token) || record.active_calls || record.owned_native_codec_resources
        || record.owned_providers || record.incoming_bindings || !record.partial_start_supported || record.entry_frontier_source_pc
        || record.input.owner!=item.staging_owner || record.input.generation!=item.staging_generation
        || record.input.serial!=item.staging_serial || record.input.host_address!=item.staging_host_address
        || record.input.source_coordinate!=item.staging_source_coordinate || record.input.size!=15216
        || record.imports.size()!=6 || record.exports.size()!=14 || !s.present
        || s.owner!=item.token.owner || s.generation!=item.token.generation || s.serial!=item.token.serial
        || !s.native_state_address || s.native_state_address==record.input.host_address
        || s.source_segment!=1 || s.source_segment_va!=0x2870 || s.private_bytes!=3152
        || s.initial_source_known_bytes || s.known_bytes!=4 || s.writes_count!=1
        || !s.entry_entered || !s.entry_returned || s.entry_result || !s.stop_domain_ready
        || s.host_abort_required || s.host_abort_entered || s.host_aborted
        || s.source_stop_entered!=closed || s.source_stop_returned!=closed
        || s.frontier_source_pc || s.frontier_stub_va || s.frontier_nid || s.frontier_import_index
        || !r.issued || !r.backend_owner || !r.backend_generation || !r.backend_serial
        || r.retired!=closed || r.input_pinned==closed || !r.create_entered || !r.create_returned
        || !r.create_source_returned || r.create_source_result<=0 || r.create_source_result!=r.semaphore_uid
        || r.semaphore_uid==item.token.uid || !r.semaphore_handle || r.create_error
        || r.create_attempts!=1 || r.create_returns!=1 || r.semaphore_live==closed || r.semaphore_closed!=closed
        || r.delete_source_returned!=closed || r.retirement_entered!=closed || r.retirement_returned!=closed) return false;
    if(r.key.module!=std::array<uint64_t,4>{{item.token.owner,item.token.generation,item.token.serial,uint64_t(item.token.uid)}}
        || r.key.input!=std::array<uint64_t,3>{{record.input.owner,record.input.generation,record.input.serial}}
        || r.key.state!=std::array<uint64_t,3>{{s.owner,s.generation,s.serial}}) return false;
    if(closed) {
        if(!r.delete_attempts || r.delete_attempts!=r.delete_returns || r.semaphore_closes!=1 || !r.host_retire_attempts
            || r.delete_source_result!=(r.delete_native_succeeded?0:-1)
            || (r.delete_native_succeeded && r.delete_error)) return false;
    } else if(r.delete_attempts || r.delete_returns || r.semaphore_closes || r.host_retire_attempts) return false;
    const auto value=static_cast<uint32_t>(r.create_source_result);
    for(size_t i=0;i<s.known.size();++i) {
        const bool known=i>=0x70 && i<0x74;
        if(s.known[i]!=(known?1:0) || (known && s.bytes[i]!=uint8_t(value>>(8*(i-0x70))))) return false;
    }
    if(s.encoded_file_literal!=std::array<uint8_t,8>{{0x90,1,0,0,0x10,3,0,0}}
        || s.file_pointer_addends!=std::array<uint32_t,2>{{0x190,0x310}}
        || s.writes[0].ordinal || s.writes[0].source_pc!=0xA24 || s.writes[0].offset!=0x70 || s.writes[0].value!=value) return false;
    constexpr std::array<uint32_t,6> nids{{0xD6DA4BA1,0x28B6489C,0x3F53E640,0x4E3A1105,0x6FE062D1,0x4C537C72}};
    for(size_t i=0;i<nids.size();++i) {
        const auto& x=record.imports[i];const char* library=i<4?"ThreadManForKernel":i==4?"sceIdStorage_driver":"semaphore";
        if(!SameNativeModuleToken(x.module,item.token) || x.library!=library || x.version || x.attributes!=1
            || x.nid!=nids[i] || x.source_stub_va!=0x26C0+8*i || x.abi!="unrecovered-psp32-memab-import-v1"
            || x.resolved || x.partial_callable || x.provider_serial || x.provider_owner || x.provider_generation) return false;
    }
    for(const auto& x:record.exports) {
        const bool lifecycle=x.library.empty() && x.kind=="function" &&
            ((x.nid==0xD632ACDB && x.relative_va==0x9F4) || (x.nid==0xCEE8593C && x.relative_va==0xA34));
        if(!SameNativeModuleToken(x.module,item.token) || x.lifecycle_callable!=lifecycle || x.partial_callable || x.version) return false;
    }
    return std::count_if(record.exports.begin(),record.exports.end(),[](const auto& x){return x.lifecycle_callable;})==2;
}
bool RepeatedMemabContinuationExact(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=7) return false;
    const auto& s=o.repeated_modules;
    if(!s.present || !s.counter_known || s.counter!=1 || s.current_record_index!=1 || s.source_gate_returned
        || s.source_segment!=1 || s.source_table_offset!=0x6950 || s.source_table_va!=0xEA4D0
        || s.table_bytes!=48 || s.known_bytes!=24 || s.writes_count!=1) return false;
    std::array<uint8_t,24> expected{{255,255,255,255,1,0,0,0,0x30,0xD5,0x0D,0,255,255,255,255,1,0,0,0,0x58,0xD5,0x0D,0}};
    const auto uid=static_cast<uint32_t>(o.module_observations[5].module_load_result);
    for(uint32_t i=0;i<4;++i) expected[i]=uint8_t(uid>>(8*i));
    for(size_t i=0;i<s.bytes.size();++i)
        if(s.known[i]!=(i<24?1:0) || (i<24 && s.bytes[i]!=expected[i])) return false;
    const auto& w=s.writes[0];
    if(w.ordinal || w.source_pc!=0x4F728 || w.record_index || w.table_offset || w.value!=uid) return false;
    for(size_t i=0;i<5;++i)
        if(o.module_observations[i].repeated_record_index!=dokodemo::StartupNoCurrentModule) return false;
    return o.module_observations[5].repeated_record_index==0 && o.module_observations[6].repeated_record_index==1;
}
[[maybe_unused]] bool AtMemabBoundary(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=7 || o.module_records.size()!=6 || o.current_module_index!=6
        || o.barrier_source_pc!=0x4F53C || o.current_frontier_source_pc!=0x4F53C
        || o.barrier_module_relative || o.current_frontier_module_relative
        || !PreparedEnvelopeModulesReady(o) || !SascoreRequestExact(o.module_observations[2],false)
        || !IfhandleRequestExact(o.module_observations[3],false) || !PspnetRequestExact(o.module_observations[4],false)
        || !MemabRequestExact(o.module_observations[5],false) || !RepeatedMemabContinuationExact(o)
        || dokodemo::PendingWindowsSascoreCleanup() || dokodemo::PendingWindowsMemabCleanup()) return false;
    const auto& next=o.module_observations[6];
    if(next.request_owner!=o.owner || next.request_index!=6 || next.wrapper_callsite!=0x4F71C
        || next.path!="disc0:/PSP_GAME/USRDIR/prx/pspnet_adhoc_auth.prx" || next.flags
        || next.options!=std::vector<uint8_t>{20,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0}
        || !next.pending || next.source_fd<0 || !next.file_live || !next.file_native_object
        || !next.file_backend_owner || !next.file_backend_generation || !next.file_backend_serial
        || next.frontier_source_pc!=0x4F53C || next.frontier_module_relative || next.module_uid_assigned
        || next.module_load_returned || next.module_start_called || next.source_close_called || next.record_live
        || next.module_input_live || next.staging_host_read || next.staging_live || next.staging_borrow_live
        || next.staging_transferred || next.staging_known_bytes) return false;
    const auto& sas=o.module_observations[2].record.sascore.resources;
    if(uint64_t(sas.source_coordinate)+sas.committed_bytes>o.module_observations[3].staging_source_coordinate) return false;
    for(size_t i=0;i<6;++i) {
        const auto& x=o.module_observations[i];
        if(x.request_owner!=o.owner || !SameNativeModuleToken(x.token,o.module_records[i].token)) return false;
        for(size_t j=i+1;j<6;++j) {
            const auto& y=o.module_observations[j];
            if(x.token.uid==y.token.uid || x.token.serial==y.token.serial || x.token.owner!=y.token.owner
                || x.token.generation!=y.token.generation
                || uint64_t(x.staging_source_coordinate)+x.staging_committed_bytes>y.staging_source_coordinate) return false;
        }
    }
    return true;
}
bool MemabSequenceClosed(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=7 || !QueryEnvelopeModulesClosed(o)
        || !SascoreRequestExact(o.module_observations[2],true) || !IfhandleRequestExact(o.module_observations[3],true)
        || !PspnetRequestExact(o.module_observations[4],true) || !MemabRequestExact(o.module_observations[5],true)
        || !RepeatedMemabContinuationExact(o) || dokodemo::PendingWindowsSascoreCleanup()
        || dokodemo::PendingWindowsMemabCleanup()) return false;
    const auto& next=o.module_observations[6];
    return next.host_file_closed && !next.file_live && !next.pending && !next.record_live
        && !next.module_uid_assigned && !next.module_load_returned && !next.module_start_called && !next.module_stop_called
        && !next.source_close_called && !next.module_input_live && !next.staging_live && !next.staging_borrow_live && !next.staging_host_read;
}

bool AdhocAuthRequestExact(const dokodemo::StartupModuleObservation& item, bool closed) {
    using namespace dokodemo::recovered;
    const auto& record=item.record; const auto& s=record.adhoc_auth;
    if(item.request_index!=6 || item.repeated_record_index!=1 || item.wrapper_callsite!=0x4F71C || item.path!="disc0:/PSP_GAME/USRDIR/prx/pspnet_adhoc_auth.prx"
        || item.flags || item.pending || item.options!=std::vector<uint8_t>{20,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0}
        || item.source_fd<0 || !item.file_native_object || !item.file_backend_owner || !item.file_backend_generation || !item.file_backend_serial
        || item.encoded_file_bytes!=10816 || item.file_live || !item.module_uid_assigned || !item.module_load_returned
        || item.token.uid<=0 || item.module_load_result!=item.token.uid || !item.module_start_called || !item.module_start_returned
        || item.module_start_result || !item.module_start_entry_returned || item.module_start_entry_result
        || !item.source_close_called || !item.source_close_returned || item.source_close_result || !item.module_file_closed_while_input_live
        || item.module_initialization_blocked || item.module_initialization_abort_called || item.module_initialization_aborted
        || item.record_live==closed || item.module_input_live==closed || item.staging_live==closed || item.staging_borrow_live==closed
        || !item.staging_host_read || !item.staging_full_read || !item.staging_hash_matches || item.staging_read_error
        || item.staging_file_position || item.staging_requested!=10816 || item.staging_transferred!=10816 || item.staging_known_bytes!=10816
        || item.staging_sha256!="c67770d8199bdd510fd932546285f1fe87674afd6426a5f9f2212b1901443f7f"
        || item.staging_owner!=item.file_backend_owner || item.staging_generation!=item.file_backend_generation
        || !item.staging_serial || !item.staging_host_address || !item.staging_page_bytes
        || item.staging_committed_bytes<10816 || item.staging_committed_bytes%item.staging_page_bytes
        || item.module_stop_called!=closed || item.module_stop_returned!=closed
        || (closed && item.module_stop_result) || item.module_unloaded!=closed || item.module_input_released!=closed
        || item.staging_freed_after_joins!=closed || record.domain!=NativeModuleDomain::Production
        || record.state!=(closed?NativeModuleState::Stopped:NativeModuleState::Started) || record.input_live==closed
        || record.name!="sceNetAdhocAuth_Service" || record.encoded_bytes!=10816 || record.plaintext_bytes!=23448
        || record.encoded_sha256!=item.staging_sha256
        || record.plaintext_sha256!="06af9b2a1283ad09042fb09d342ebc546471955b856a434a39e42ca6e1f619b7"
        || record.policy!="WindowsAdhocAuthExact0001_0009LifecycleV1" || record.start_source_va!=0x3A00 || record.stop_source_va!=0x3A08
        || !SameNativeModuleToken(record.token,item.token) || record.active_calls || record.owned_native_codec_resources
        || record.owned_providers || record.incoming_bindings || record.partial_start_supported || record.entry_frontier_source_pc
        || record.input.owner!=item.staging_owner || record.input.generation!=item.staging_generation || record.input.serial!=item.staging_serial
        || record.input.host_address!=item.staging_host_address || record.input.source_coordinate!=item.staging_source_coordinate
        || record.input.size!=10816 || record.imports.size()!=42 || record.exports.size()!=21
        || !s.present || s.owner!=item.token.owner || s.generation!=item.token.generation || s.serial!=item.token.serial
        || !s.native_state_address || s.native_state_address==record.input.host_address || s.source_segment!=1 || s.source_segment_va!=0x3ED0
        || s.private_bytes!=412 || s.initial_source_known_bytes!=8 || s.known_bytes!=8 || s.writes_count) return false;
    if(s.source_file_offset!=0x3F50 || s.source_file_bytes!=8) return false;
    for(size_t i=0;i<s.known.size();++i)
        if(s.known[i]!=(i<8?1:0) || (i<8 && s.bytes[i]!=255)) return false;
    struct ExpectedImport { const char* library; uint16_t version,attributes; uint32_t nid,stub; };
    constexpr std::array<ExpectedImport,42> imports{{
        {"sceNetIfhandle_driver",0,0x0009,0x15CFE3C0,0x3B10},
        {"sceNetIfhandle_driver",0,0x0009,0x76BAD213,0x3B18},
        {"sceNetIfhandle_driver",0,0x0009,0x1560F143,0x3B20},
        {"sceNetIfhandle_driver",0,0x0009,0x9A6261EC,0x3B28},
        {"sceNetIfhandle_driver",0,0x0009,0xF8825DC4,0x3B30},
        {"sceNetIfhandle_driver",0,0x0009,0xF56FAC82,0x3B38},
        {"sceNetIfhandle_driver",0,0x0009,0x0542835F,0x3B40},
        {"sceNetIfhandle_driver",0,0x0009,0x773FC77C,0x3B48},
        {"sceNetIfhandle_driver",0,0x0009,0x9CBA24D4,0x3B50},
        {"sceNetIfhandle_driver",0,0x0009,0xC5623112,0x3B58},
        {"sceMemab_driver",0,0x0001,0x01FD1BA0,0x3AB8},
        {"sceMemab_driver",0,0x0001,0x0C134D3F,0x3AC0},
        {"sceMemab_driver",0,0x0001,0xA4720EED,0x3AC8},
        {"sceMemab_driver",0,0x0001,0xDC598EF3,0x3AD0},
        {"sceMemab_driver",0,0x0001,0x8DAC2343,0x3AD8},
        {"sceMemab_driver",0,0x0001,0xDAE8A629,0x3AE0},
        {"sceMemab_driver",0,0x0001,0xCF49BCBB,0x3AE8},
        {"sceMemab_driver",0,0x0001,0xE6D3ACE2,0x3AF0},
        {"sceMemab_driver",0,0x0001,0xB54E1AA7,0x3AF8},
        {"sceMemab_driver",0,0x0001,0xF1D2B9B0,0x3B00},
        {"sceMemab_driver",0,0x0001,0x4DF566D3,0x3B08},
        {"SysclibForKernel",0,0x0001,0xAB7592FF,0x3A20},
        {"SysclibForKernel",0,0x0001,0x10F3BB61,0x3A28},
        {"SysclibForKernel",0,0x0001,0x81D0D1F7,0x3A30},
        {"ThreadManForKernel",0,0x0001,0x278C0DF5,0x3A38},
        {"ThreadManForKernel",0,0x0001,0xD6DA4BA1,0x3A40},
        {"ThreadManForKernel",0,0x0001,0x28B6489C,0x3A48},
        {"ThreadManForKernel",0,0x0001,0x3F53E640,0x3A50},
        {"ThreadManForKernel",0,0x0001,0x4E3A1105,0x3A58},
        {"ThreadManForKernel",0,0x0001,0x8125221D,0x3A60},
        {"ThreadManForKernel",0,0x0001,0x86255ADA,0x3A68},
        {"ThreadManForKernel",0,0x0001,0xE9B3061E,0x3A70},
        {"ThreadManForKernel",0,0x0001,0x18260574,0x3A78},
        {"ThreadManForKernel",0,0x0001,0x6652B8CA,0x3A80},
        {"ThreadManForKernel",0,0x0001,0x7E65B999,0x3A88},
        {"ThreadManForKernel",0,0x0001,0x446D8DE6,0x3A90},
        {"ThreadManForKernel",0,0x0001,0x9FA03CD3,0x3A98},
        {"ThreadManForKernel",0,0x0001,0xF475845D,0x3AA0},
        {"ThreadManForKernel",0,0x0001,0x616403BA,0x3AA8},
        {"ThreadManForKernel",0,0x0001,0x293B45B8,0x3AB0},
        {"InterruptManagerForKernel",0,0x0009,0x092968F4,0x3A10},
        {"InterruptManagerForKernel",0,0x0009,0x5F10D406,0x3A18},
    }};
    for(size_t i=0;i<imports.size();++i) {
        const auto& actual=record.imports[i]; const auto& expected=imports[i];
        if(!SameNativeModuleToken(actual.module,item.token) || actual.library!=expected.library
            || actual.version!=expected.version || actual.attributes!=expected.attributes
            || actual.nid!=expected.nid || actual.source_stub_va!=expected.stub
            || actual.abi!="unrecovered-psp32-adhoc-auth-import-v1" || actual.resolved || actual.partial_callable
            || actual.provider_serial || actual.provider_owner || actual.provider_generation) return false;
    }
    struct ExpectedExport { const char* library; const char* kind; uint16_t version,attributes; uint32_t nid,va; };
    constexpr std::array<ExpectedExport,21> exports{{
        {"","function",0,0x8000,0xD632ACDB,0x3A00},
        {"","function",0,0x8000,0xCEE8593C,0x3A08},
        {"","variable",0,0x8000,0xF01D73A7,0x3C04},
        {"sceNetAdhocAuth_lib","function",0,0x4001,0x86004235,0x164},
        {"sceNetAdhocAuth_lib","function",0,0x4001,0x6074D8F1,0x228},
        {"sceNetAdhocAuth_lib","function",0,0x4001,0x89F2A732,0x35C},
        {"sceNetAdhocAuth_lib","function",0,0x4001,0x2E6AA271,0x6E4},
        {"sceNetAdhocAuth_lib","function",0,0x4001,0x6CE209A3,0x870},
        {"sceNetAdhocAuth_lib","function",0,0x4001,0x2AD8C677,0x9D4},
        {"sceNetAdhocAuth_lib","function",0,0x4001,0xBD144DA6,0xB94},
        {"sceNetAdhocAuth_lib","function",0,0x4001,0x1F9A90B8,0xD38},
        {"sceNetAdhocAuth_lib","function",0,0x4001,0x76F26AB0,0xF48},
        {"sceNetAdhocAuth_driver","function",0,0x0001,0x86004235,0x164},
        {"sceNetAdhocAuth_driver","function",0,0x0001,0x6074D8F1,0x228},
        {"sceNetAdhocAuth_driver","function",0,0x0001,0x89F2A732,0x35C},
        {"sceNetAdhocAuth_driver","function",0,0x0001,0x2E6AA271,0x6E4},
        {"sceNetAdhocAuth_driver","function",0,0x0001,0x6CE209A3,0x870},
        {"sceNetAdhocAuth_driver","function",0,0x0001,0x2AD8C677,0x9D4},
        {"sceNetAdhocAuth_driver","function",0,0x0001,0xBD144DA6,0xB94},
        {"sceNetAdhocAuth_driver","function",0,0x0001,0x1F9A90B8,0xD38},
        {"sceNetAdhocAuth_driver","function",0,0x0001,0x76F26AB0,0xF48},
    }};
    for(size_t i=0;i<exports.size();++i) {
        const auto& actual=record.exports[i];const auto& expected=exports[i];
        const bool callable=expected.library[0]=='\0' && std::string_view(expected.kind)=="function"
            && ((expected.nid==0xD632ACDB && expected.va==0x3A00) || (expected.nid==0xCEE8593C && expected.va==0x3A08));
        if(!SameNativeModuleToken(actual.module,item.token) || actual.library!=expected.library || actual.kind!=expected.kind
            || actual.version!=expected.version || actual.attributes!=expected.attributes || actual.nid!=expected.nid
            || actual.relative_va!=expected.va || actual.lifecycle_callable!=callable || actual.partial_callable) return false;
    }
    return true;
}
bool RepeatedAuthContinuationExact(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=8) return false;
    const auto& s=o.repeated_modules;
    if(!s.present || !s.counter_known || s.counter!=2 || s.current_record_index!=2 || s.source_gate_returned
        || s.source_segment!=1 || s.source_table_offset!=0x6950 || s.source_table_va!=0xEA4D0
        || s.table_bytes!=48 || s.known_bytes!=36 || s.writes_count!=2) return false;
    std::array<uint8_t,36> expected{{255,255,255,255,1,0,0,0,0x30,0xD5,0x0D,0,
        255,255,255,255,1,0,0,0,0x58,0xD5,0x0D,0,255,255,255,255,0,0,0,0,0x8C,0xD5,0x0D,0}};
    for(size_t row=0;row<2;++row) {
        const auto uid=static_cast<uint32_t>(o.module_observations[5+row].module_load_result);
        for(size_t i=0;i<4;++i) expected[12*row+i]=uint8_t(uid>>(8*i));
        const auto& w=s.writes[row];
        if(w.ordinal!=row || w.source_pc!=0x4F728 || w.record_index!=row || w.table_offset!=12*row || w.value!=uid) return false;
    }
    for(size_t i=0;i<s.bytes.size();++i)
        if(s.known[i]!=(i<36?1:0) || (i<36 && s.bytes[i]!=expected[i])) return false;
    for(size_t i=0;i<5;++i)
        if(o.module_observations[i].repeated_record_index!=dokodemo::StartupNoCurrentModule) return false;
    return o.module_observations[5].repeated_record_index==0 && o.module_observations[6].repeated_record_index==1
        && o.module_observations[7].repeated_record_index==2;
}
[[maybe_unused]] bool AtAuthBoundary(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=8 || o.module_records.size()!=7 || o.current_module_index!=7
        || o.barrier_source_pc!=0x4F53C || o.current_frontier_source_pc!=0x4F53C
        || o.barrier_module_relative || o.current_frontier_module_relative
        || !PreparedEnvelopeModulesReady(o) || !SascoreRequestExact(o.module_observations[2],false)
        || !IfhandleRequestExact(o.module_observations[3],false) || !PspnetRequestExact(o.module_observations[4],false)
        || !MemabRequestExact(o.module_observations[5],false) || !AdhocAuthRequestExact(o.module_observations[6],false)
        || !RepeatedAuthContinuationExact(o) || dokodemo::PendingWindowsSascoreCleanup()
        || dokodemo::PendingWindowsMemabCleanup()) return false;
    const auto& next=o.module_observations[7];
    if(next.request_owner!=o.owner || next.request_index!=7 || next.wrapper_callsite!=0x4F71C
        || next.path!="disc0:/PSP_GAME/USRDIR/prx/pspnet_adhoc.prx" || next.flags
        || next.options!=std::vector<uint8_t>{20,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1,0,0}
        || !next.pending || next.source_fd<0 || !next.file_live || !next.file_native_object
        || !next.file_backend_owner || !next.file_backend_generation || !next.file_backend_serial
        || next.frontier_source_pc!=0x4F53C || next.frontier_module_relative || next.module_uid_assigned
        || next.module_load_returned || next.module_start_called || next.source_close_called || next.record_live
        || next.module_input_live || next.staging_host_read || next.staging_live || next.staging_borrow_live
        || next.staging_transferred || next.staging_known_bytes) return false;
    const auto& sas=o.module_observations[2].record.sascore.resources;
    if(uint64_t(sas.source_coordinate)+sas.committed_bytes>o.module_observations[3].staging_source_coordinate) return false;
    for(size_t i=0;i<7;++i) {
        const auto& x=o.module_observations[i];
        if(x.request_owner!=o.owner || !SameNativeModuleToken(x.token,o.module_records[i].token)) return false;
        for(size_t j=i+1;j<7;++j) {
            const auto& y=o.module_observations[j];
            if(x.token.uid==y.token.uid || x.token.serial==y.token.serial || x.token.owner!=y.token.owner
                || x.token.generation!=y.token.generation
                || uint64_t(x.staging_source_coordinate)+x.staging_committed_bytes>y.staging_source_coordinate) return false;
        }
    }
    return true;
}
bool AuthSequenceClosed(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=8 || !QueryEnvelopeModulesClosed(o)
        || !SascoreRequestExact(o.module_observations[2],true) || !IfhandleRequestExact(o.module_observations[3],true)
        || !PspnetRequestExact(o.module_observations[4],true) || !MemabRequestExact(o.module_observations[5],true)
        || !AdhocAuthRequestExact(o.module_observations[6],true) || !RepeatedAuthContinuationExact(o)
        || dokodemo::PendingWindowsSascoreCleanup() || dokodemo::PendingWindowsMemabCleanup()) return false;
    const auto& next=o.module_observations[7];
    return next.host_file_closed && !next.file_live && !next.pending && !next.record_live
        && !next.module_uid_assigned && !next.module_load_returned && !next.module_start_called && !next.module_stop_called
        && !next.source_close_called && !next.module_input_live && !next.staging_live && !next.staging_borrow_live && !next.staging_host_read;
}

bool AdhocRequestExact(const dokodemo::StartupModuleObservation& item, bool closed) {
    using namespace dokodemo::recovered;
    const auto& record=item.record; const auto& s=record.adhoc;
    if(item.request_index!=7 || item.repeated_record_index!=2 || item.wrapper_callsite!=0x4F71C || item.path!="disc0:/PSP_GAME/USRDIR/prx/pspnet_adhoc.prx"
        || item.flags || item.pending || item.options!=std::vector<uint8_t>{20,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1,0,0}
        || item.source_fd<0 || !item.file_native_object || !item.file_backend_owner || !item.file_backend_generation || !item.file_backend_serial
        || item.encoded_file_bytes!=40140 || item.file_live || !item.module_uid_assigned || !item.module_load_returned
        || item.token.uid<=0 || item.module_load_result!=item.token.uid || !item.module_start_called || !item.module_start_returned
        || item.module_start_result || !item.module_start_entry_returned || item.module_start_entry_result
        || !item.source_close_called || !item.source_close_returned || item.source_close_result || !item.module_file_closed_while_input_live
        || item.module_initialization_blocked || item.module_initialization_abort_called || item.module_initialization_aborted
        || item.record_live==closed || item.module_input_live==closed || item.staging_live==closed || item.staging_borrow_live==closed
        || !item.staging_host_read || !item.staging_full_read || !item.staging_hash_matches || item.staging_read_error
        || item.staging_file_position || item.staging_requested!=40140 || item.staging_transferred!=40140 || item.staging_known_bytes!=40140
        || item.staging_sha256!="47c98edb5d62e069bed77eb5afd69e3877ae3991a163876ea6012536a223826f"
        || item.staging_owner!=item.file_backend_owner || item.staging_generation!=item.file_backend_generation
        || !item.staging_serial || !item.staging_host_address || !item.staging_page_bytes
        || item.staging_committed_bytes<40140 || item.staging_committed_bytes%item.staging_page_bytes
        || item.module_stop_called!=closed || item.module_stop_returned!=closed
        || (closed && item.module_stop_result) || item.module_unloaded!=closed || item.module_input_released!=closed
        || item.staging_freed_after_joins!=closed || record.domain!=NativeModuleDomain::Production
        || record.state!=(closed?NativeModuleState::Stopped:NativeModuleState::Started) || record.input_live==closed
        || record.name!="sceNetAdhoc_Library" || record.encoded_bytes!=40140 || record.plaintext_bytes!=40140
        || record.encoded_sha256!=item.staging_sha256
        || record.plaintext_sha256!="47c98edb5d62e069bed77eb5afd69e3877ae3991a163876ea6012536a223826f"
        || record.policy!="WindowsAdhocExact0001_4001LifecycleV1" || record.start_source_va!=0x6D5C || record.stop_source_va!=0x6D64
        || !SameNativeModuleToken(record.token,item.token) || record.active_calls || record.owned_native_codec_resources
        || record.owned_providers || record.incoming_bindings || record.partial_start_supported || record.entry_frontier_source_pc
        || record.input.owner!=item.staging_owner || record.input.generation!=item.staging_generation || record.input.serial!=item.staging_serial
        || record.input.host_address!=item.staging_host_address || record.input.source_coordinate!=item.staging_source_coordinate
        || record.input.size!=40140 || record.imports.size()!=34 || record.exports.size()!=32
        || !s.present || s.owner!=item.token.owner || s.generation!=item.token.generation || s.serial!=item.token.serial
        || !s.native_state_address || s.native_state_address==record.input.host_address || s.source_segment!=1 || s.source_segment_va!=0x73F0
        || s.private_bytes!=144 || s.initial_source_known_bytes!=4 || s.known_bytes!=4 || s.writes_count) return false;
    if(s.source_file_offset!=0x7470 || s.source_file_bytes!=4) return false;
    for(size_t i=0;i<s.known.size();++i)
        if(s.known[i]!=(i<4?1:0) || (i<4 && s.bytes[i]!=255)) return false;
    struct ExpectedImport { const char* library; uint16_t version,attributes; uint32_t nid,stub; };
    constexpr std::array<ExpectedImport,34> imports{{
        {"sceNetIfhandle_lib",0,0x4001,0x15CFE3C0,0x6F5C},
        {"sceNetIfhandle_lib",0,0x4001,0x76BAD213,0x6F64},
        {"sceNetIfhandle_lib",0,0x4001,0xBFF3CEA5,0x6F6C},
        {"sceNetIfhandle_lib",0,0x4001,0x3E8DD3F8,0x6F74},
        {"sceNetIfhandle_lib",0,0x4001,0x9A6261EC,0x6F7C},
        {"sceNetIfhandle_lib",0,0x4001,0xF8825DC4,0x6F84},
        {"sceNetIfhandle_lib",0,0x4001,0xF56FAC82,0x6F8C},
        {"sceNetIfhandle_lib",0,0x4001,0x49EDBB18,0x6F94},
        {"sceWlanDrv_lib",0,0x4001,0x5E7C8D94,0x7034},
        {"sceWlanDrv_lib",0,0x4001,0x5ED4049A,0x703C},
        {"sceWlanDrv_lib",0,0x4001,0xB4D7CB74,0x7044},
        {"sceWlanDrv_lib",0,0x4001,0xA447103A,0x704C},
        {"sceNet",0,0x0001,0x0BF0A3AE,0x6F54},
        {"sceNet_lib",0,0x0001,0x1858883D,0x6F9C},
        {"sceNet_lib",0,0x0001,0x25CC373A,0x6FA4},
        {"sceNet_lib",0,0x0001,0xA8B6205A,0x6FAC},
        {"sceNet_lib",0,0x0001,0xA93A93E9,0x6FB4},
        {"sceNet_lib",0,0x0001,0x6B294EE4,0x6FBC},
        {"sceNet_lib",0,0x0001,0xF6DB0A0B,0x6FC4},
        {"sceNet_lib",0,0x0001,0x7574FDA1,0x6FCC},
        {"sceNet_lib",0,0x0001,0xCA3CF5EB,0x6FD4},
        {"sceNet_lib",0,0x0001,0x757085B0,0x6FDC},
        {"sceNet_lib",0,0x0001,0x94B44F26,0x6FE4},
        {"sceNet_lib",0,0x0001,0x515B2F33,0x6FEC},
        {"sceNet_lib",0,0x0001,0x05D525E4,0x6FF4},
        {"sceNet_lib",0,0x0001,0xD60225A3,0x6FFC},
        {"sceNet_lib",0,0x0001,0x2E005032,0x7004},
        {"sceNet_lib",0,0x0001,0x33B230BD,0x700C},
        {"sceNet_lib",0,0x0001,0x1F94AFD9,0x7014},
        {"sceNet_lib",0,0x0001,0x0A5A8751,0x701C},
        {"sceNet_lib",0,0x0001,0xB3A48B7F,0x7024},
        {"sceNet_lib",0,0x0001,0x949F1FBB,0x702C},
        {"ThreadManForUser",0,0x4001,0x3F53E640,0x6F44},
        {"ThreadManForUser",0,0x4001,0x82BC5777,0x6F4C},
    }};
    for(size_t i=0;i<imports.size();++i) {
        const auto& actual=record.imports[i]; const auto& expected=imports[i];
        if(!SameNativeModuleToken(actual.module,item.token) || actual.library!=expected.library
            || actual.version!=expected.version || actual.attributes!=expected.attributes
            || actual.nid!=expected.nid || actual.source_stub_va!=expected.stub
            || actual.abi!="unrecovered-psp32-adhoc-import-v1" || actual.resolved || actual.partial_callable
            || actual.provider_serial || actual.provider_owner || actual.provider_generation) return false;
    }
    struct ExpectedExport { const char* library; const char* kind; uint16_t version,attributes; uint32_t nid,va; };
    constexpr std::array<ExpectedExport,32> exports{{
        {"","function",0,0x8000,0xD632ACDB,0x6D5C},
        {"","function",0,0x8000,0xCEE8593C,0x6D64},
        {"","variable",0,0x8000,0xF01D73A7,0x7104},
        {"sceNetAdhoc","function",0,0x0001,0xE1D621D7,0x0},
        {"sceNetAdhoc","function",0,0x0001,0xA62C6F57,0x124},
        {"sceNetAdhoc","function",0,0x0001,0x7A662D6B,0x460},
        {"sceNetAdhoc","function",0,0x0001,0x73BFD52D,0x9E4},
        {"sceNetAdhoc","function",0,0x0001,0x4D2CE199,0xBE4},
        {"sceNetAdhoc","function",0,0x0001,0x6F92741B,0x10A8},
        {"sceNetAdhoc","function",0,0x0001,0xABED3790,0x11D4},
        {"sceNetAdhoc","function",0,0x0001,0xDFE53E03,0x1EA4},
        {"sceNetAdhoc","function",0,0x0001,0x7F27BB5E,0x13C4},
        {"sceNetAdhoc","function",0,0x0001,0xC7C1FC57,0x28A0},
        {"sceNetAdhoc","function",0,0x0001,0x877F6D66,0x2CDC},
        {"sceNetAdhoc","function",0,0x0001,0xFC6FC07B,0x2DF0},
        {"sceNetAdhoc","function",0,0x0001,0xE08BDAC1,0x2F98},
        {"sceNetAdhoc","function",0,0x0001,0x9DF81198,0x3094},
        {"sceNetAdhoc","function",0,0x0001,0x4DA4C788,0x325C},
        {"sceNetAdhoc","function",0,0x0001,0x8BEA2B3E,0x3448},
        {"sceNetAdhoc","function",0,0x0001,0x9AC2EEAC,0x3640},
        {"sceNetAdhoc","function",0,0x0001,0x157E6225,0x37FC},
        {"sceNetAdhoc","function",0,0x0001,0xB9685118,0x5238},
        {"sceNetAdhoc","function",0,0x0001,0x7F75C338,0x66FC},
        {"sceNetAdhoc","function",0,0x0001,0x3278AB0C,0x68FC},
        {"sceNetAdhoc","function",0,0x0001,0x98C204C8,0x67B8},
        {"sceNetAdhoc","function",0,0x0001,0xFA324B4E,0x69AC},
        {"sceNetAdhoc","function",0,0x0001,0xA0229362,0x69E4},
        {"sceNetAdhoc","function",0,0x0001,0x0B2228E9,0x6A58},
        {"sceNetAdhoc_lib","function",0,0x0001,0x67346A2A,0x1EDC},
        {"sceNetAdhoc_lib","function",0,0x0001,0x62B875A8,0x47AC},
        {"sceNetAdhoc_lib","function",0,0x0001,0x2CA12790,0x47E8},
        {"sceNetAdhoc_lib","function",0,0x0001,0xAF6DFB15,0x69C8},
    }};
    for(size_t i=0;i<exports.size();++i) {
        const auto& actual=record.exports[i];const auto& expected=exports[i];
        const bool callable=expected.library[0]=='\0' && std::string_view(expected.kind)=="function"
            && ((expected.nid==0xD632ACDB && expected.va==0x6D5C) || (expected.nid==0xCEE8593C && expected.va==0x6D64));
        if(!SameNativeModuleToken(actual.module,item.token) || actual.library!=expected.library || actual.kind!=expected.kind
            || actual.version!=expected.version || actual.attributes!=expected.attributes || actual.nid!=expected.nid
            || actual.relative_va!=expected.va || actual.lifecycle_callable!=callable || actual.partial_callable) return false;
    }
    return true;
}
bool RepeatedAdhocContinuationExact(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=9) return false;
    const auto& s=o.repeated_modules;
    if(!s.present || !s.counter_known || s.counter!=3 || s.current_record_index!=3 || s.source_gate_returned
        || s.source_segment!=1 || s.source_table_offset!=0x6950 || s.source_table_va!=0xEA4D0
        || s.table_bytes!=48 || s.known_bytes!=48 || s.writes_count!=3) return false;
    std::array<uint8_t,48> expected{{255,255,255,255,1,0,0,0,48,213,13,0,255,255,255,255,1,0,0,0,88,213,13,0,255,255,255,255,0,0,0,0,140,213,13,0,255,255,255,255,0,0,0,0,184,213,13,0}};
    for(size_t row=0;row<3;++row) {
        const auto uid=static_cast<uint32_t>(o.module_observations[5+row].module_load_result);
        for(size_t i=0;i<4;++i) expected[12*row+i]=uint8_t(uid>>(8*i));
        const auto& w=s.writes[row];
        if(w.ordinal!=row || w.source_pc!=0x4F728 || w.record_index!=row || w.table_offset!=12*row || w.value!=uid) return false;
    }
    for(size_t i=0;i<s.bytes.size();++i)
        if(s.known[i]!=1 || s.bytes[i]!=expected[i]) return false;
    for(size_t i=0;i<5;++i)
        if(o.module_observations[i].repeated_record_index!=dokodemo::StartupNoCurrentModule) return false;
    for(size_t row=0;row<4;++row)
        if(o.module_observations[5+row].repeated_record_index!=row) return false;
    return true;
}
[[maybe_unused]] bool AtAdhocBoundary(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=9 || o.module_records.size()!=8 || o.current_module_index!=8
        || o.barrier_source_pc!=0x4F53C || o.current_frontier_source_pc!=0x4F53C
        || o.barrier_module_relative || o.current_frontier_module_relative
        || !PreparedEnvelopeModulesReady(o) || !SascoreRequestExact(o.module_observations[2],false)
        || !IfhandleRequestExact(o.module_observations[3],false) || !PspnetRequestExact(o.module_observations[4],false)
        || !MemabRequestExact(o.module_observations[5],false) || !AdhocAuthRequestExact(o.module_observations[6],false)
        || !AdhocRequestExact(o.module_observations[7],false)
        || !RepeatedAdhocContinuationExact(o) || dokodemo::PendingWindowsSascoreCleanup()
        || dokodemo::PendingWindowsMemabCleanup()) return false;
    const auto& next=o.module_observations[8];
    if(next.request_owner!=o.owner || next.request_index!=8 || next.wrapper_callsite!=0x4F71C
        || next.path!="disc0:/PSP_GAME/USRDIR/prx/pspnet_adhocctl.prx" || next.flags
        || next.options!=std::vector<uint8_t>{20,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1,0,0}
        || !next.pending || next.source_fd<0 || !next.file_live || !next.file_native_object
        || !next.file_backend_owner || !next.file_backend_generation || !next.file_backend_serial
        || next.frontier_source_pc!=0x4F53C || next.frontier_module_relative || next.module_uid_assigned
        || next.module_load_returned || next.module_start_called || next.source_close_called || next.record_live
        || next.module_input_live || next.staging_host_read || next.staging_live || next.staging_borrow_live
        || next.staging_transferred || next.staging_known_bytes) return false;
    const auto& sas=o.module_observations[2].record.sascore.resources;
    if(uint64_t(sas.source_coordinate)+sas.committed_bytes>o.module_observations[3].staging_source_coordinate) return false;
    for(size_t i=0;i<8;++i) {
        const auto& x=o.module_observations[i];
        if(x.request_owner!=o.owner || !SameNativeModuleToken(x.token,o.module_records[i].token)) return false;
        for(size_t j=i+1;j<8;++j) {
            const auto& y=o.module_observations[j];
            if(x.token.uid==y.token.uid || x.token.serial==y.token.serial || x.token.owner!=y.token.owner
                || x.token.generation!=y.token.generation
                || uint64_t(x.staging_source_coordinate)+x.staging_committed_bytes>y.staging_source_coordinate) return false;
        }
    }
    return true;
}
bool AdhocSequenceClosed(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=9 || !QueryEnvelopeModulesClosed(o)
        || !SascoreRequestExact(o.module_observations[2],true) || !IfhandleRequestExact(o.module_observations[3],true)
        || !PspnetRequestExact(o.module_observations[4],true) || !MemabRequestExact(o.module_observations[5],true)
        || !AdhocAuthRequestExact(o.module_observations[6],true) || !AdhocRequestExact(o.module_observations[7],true) || !RepeatedAdhocContinuationExact(o)
        || dokodemo::PendingWindowsSascoreCleanup() || dokodemo::PendingWindowsMemabCleanup()) return false;
    const auto& next=o.module_observations[8];
    return next.host_file_closed && !next.file_live && !next.pending && !next.record_live
        && !next.module_uid_assigned && !next.module_load_returned && !next.module_start_called && !next.module_stop_called
        && !next.source_close_called && !next.module_input_live && !next.staging_live && !next.staging_borrow_live && !next.staging_host_read;
}

bool AdhocctlRequestExact(const dokodemo::StartupModuleObservation& item, bool closed) {
    using namespace dokodemo::recovered;
    const auto& record=item.record; const auto& s=record.adhocctl;
    if(item.request_index!=8 || item.repeated_record_index!=3 || item.wrapper_callsite!=0x4F71C || item.path!="disc0:/PSP_GAME/USRDIR/prx/pspnet_adhocctl.prx"
        || item.flags || item.pending || item.options!=std::vector<uint8_t>{20,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1,0,0}
        || item.source_fd<0 || !item.file_native_object || !item.file_backend_owner || !item.file_backend_generation || !item.file_backend_serial
        || item.encoded_file_bytes!=36416 || item.file_live || !item.module_uid_assigned || !item.module_load_returned
        || item.token.uid<=0 || item.module_load_result!=item.token.uid || !item.module_start_called || !item.module_start_returned
        || item.module_start_result || !item.module_start_entry_returned || item.module_start_entry_result
        || !item.source_close_called || !item.source_close_returned || item.source_close_result || !item.module_file_closed_while_input_live
        || item.module_initialization_blocked || item.module_initialization_abort_called || item.module_initialization_aborted
        || item.record_live==closed || item.module_input_live==closed || item.staging_live==closed || item.staging_borrow_live==closed
        || !item.staging_host_read || !item.staging_full_read || !item.staging_hash_matches || item.staging_read_error
        || item.staging_file_position || item.staging_requested!=36416 || item.staging_transferred!=36416 || item.staging_known_bytes!=36416
        || item.staging_sha256!="3ac8e2211a9b66fe5161a3eac61ec09e258b474068d25bed4adefd5a24bc0f22"
        || item.staging_owner!=item.file_backend_owner || item.staging_generation!=item.file_backend_generation
        || !item.staging_serial || !item.staging_host_address || !item.staging_page_bytes
        || item.staging_committed_bytes<36416 || item.staging_committed_bytes%item.staging_page_bytes
        || item.module_stop_called!=closed || item.module_stop_returned!=closed
        || (closed && item.module_stop_result) || item.module_unloaded!=closed || item.module_input_released!=closed
        || item.staging_freed_after_joins!=closed || record.domain!=NativeModuleDomain::Production
        || record.state!=(closed?NativeModuleState::Stopped:NativeModuleState::Started) || record.input_live==closed
        || record.name!="sceNetAdhocctl_Library" || record.encoded_bytes!=36416 || record.plaintext_bytes!=36416
        || record.encoded_sha256!=item.staging_sha256
        || record.plaintext_sha256!="3ac8e2211a9b66fe5161a3eac61ec09e258b474068d25bed4adefd5a24bc0f22"
        || record.policy!="WindowsAdhocctlExact0001_4001LifecycleV1" || record.start_source_va!=0x4D54 || record.stop_source_va!=0x4D5C
        || !SameNativeModuleToken(record.token,item.token) || record.active_calls || record.owned_native_codec_resources
        || record.owned_providers || record.incoming_bindings || record.partial_start_supported || record.entry_frontier_source_pc
        || record.input.owner!=item.staging_owner || record.input.generation!=item.staging_generation || record.input.serial!=item.staging_serial
        || record.input.host_address!=item.staging_host_address || record.input.source_coordinate!=item.staging_source_coordinate
        || record.input.size!=36416 || record.imports.size()!=54 || record.exports.size()!=27
        || !s.present || s.owner!=item.token.owner || s.generation!=item.token.generation || s.serial!=item.token.serial
        || !s.native_state_address || s.native_state_address==record.input.host_address || s.source_segment!=1 || s.source_segment_va!=0x6810
        || s.private_bytes!=5892 || s.initial_source_known_bytes!=132 || s.known_bytes!=132 || s.writes_count) return false;
    if(s.source_file_offset!=0x6890 || s.source_file_bytes!=132) return false;
    constexpr std::array<uint8_t,132> initial{{0,32,32,32,32,32,32,32,32,32,8,8,8,8,8,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,24,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,4,4,4,4,4,4,4,4,4,4,16,16,16,16,16,16,16,65,65,65,65,65,65,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,16,16,16,16,16,16,66,66,66,66,66,66,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,16,16,16,16,32,0,0,0}};
    for(size_t i=0;i<s.known.size();++i)
        if(s.known[i]!=(i<initial.size()?1:0) || (i<initial.size() && s.bytes[i]!=initial[i])) return false;
    struct ExpectedImport { const char* library; uint16_t version,attributes; uint32_t nid,stub; };
    constexpr std::array<ExpectedImport,54> imports{{
        {"sceUtility",0,0x4001,0xA5DA2406,0x6328},
        {"sceUtility",0,0x4001,0x34B78343,0x6330},
        {"sceNetAdhocAuth_lib",0,0x4001,0x86004235,0x6280},
        {"sceNetAdhocAuth_lib",0,0x4001,0x6074D8F1,0x6288},
        {"sceNetAdhocAuth_lib",0,0x4001,0x89F2A732,0x6290},
        {"sceNetAdhocAuth_lib",0,0x4001,0x2E6AA271,0x6298},
        {"sceNetAdhocAuth_lib",0,0x4001,0x6CE209A3,0x62A0},
        {"sceNetAdhocAuth_lib",0,0x4001,0xBD144DA6,0x62A8},
        {"sceNetAdhocAuth_lib",0,0x4001,0x1F9A90B8,0x62B0},
        {"sceNetAdhocAuth_lib",0,0x4001,0x76F26AB0,0x62B8},
        {"sceWlanDrv_lib",0,0x4001,0x482CAE9A,0x6340},
        {"sceWlanDrv_lib",0,0x4001,0xC9A8CAB7,0x6348},
        {"sceWlanDrv_lib",0,0x4001,0x5BAA1FE5,0x6350},
        {"sceWlanDrv_lib",0,0x4001,0x2D0FAE4E,0x6358},
        {"sceWlanDrv_lib",0,0x4001,0x56F467CA,0x6360},
        {"sceWlanDrv",0,0x4001,0xD7763699,0x6338},
        {"sceNet",0,0x0001,0x0BF0A3AE,0x6238},
        {"sceNet_lib",0,0x0001,0x5216CBF5,0x62C0},
        {"sceNet_lib",0,0x0001,0xD2422E4D,0x62C8},
        {"sceNet_lib",0,0x0001,0xD1BE2CE9,0x62D0},
        {"sceNet_lib",0,0x0001,0xAB7DD9A5,0x62D8},
        {"sceNet_lib",0,0x0001,0x7BA3ED91,0x62E0},
        {"sceNet_lib",0,0x0001,0x03164B12,0x62E8},
        {"sceNet_lib",0,0x0001,0x389728AB,0x62F0},
        {"sceNet_lib",0,0x0001,0xD5B64E37,0x62F8},
        {"sceNet_lib",0,0x0001,0xDA02F383,0x6300},
        {"sceNet_lib",0,0x0001,0xAFA11338,0x6308},
        {"sceNet_lib",0,0x0001,0xB20F84F8,0x6310},
        {"sceNet_lib",0,0x0001,0xA55C914F,0x6318},
        {"sceNet_lib",0,0x0001,0x0D633F53,0x6320},
        {"sceNetAdhoc",0,0x0001,0x877F6D66,0x6240},
        {"sceNetAdhoc",0,0x0001,0xFC6FC07B,0x6248},
        {"sceNetAdhoc",0,0x0001,0xE08BDAC1,0x6250},
        {"sceNetAdhoc",0,0x0001,0x9DF81198,0x6258},
        {"sceNetAdhoc",0,0x0001,0x4DA4C788,0x6260},
        {"sceNetAdhoc",0,0x0001,0x8BEA2B3E,0x6268},
        {"sceNetAdhoc",0,0x0001,0x9AC2EEAC,0x6270},
        {"sceNetAdhoc",0,0x0001,0x157E6225,0x6278},
        {"ThreadManForUser",0,0x4001,0x446D8DE6,0x61B8},
        {"ThreadManForUser",0,0x4001,0x9FA03CD3,0x61C0},
        {"ThreadManForUser",0,0x4001,0xF475845D,0x61C8},
        {"ThreadManForUser",0,0x4001,0x616403BA,0x61D0},
        {"ThreadManForUser",0,0x4001,0xCEADEB47,0x61D8},
        {"ThreadManForUser",0,0x4001,0xD6DA4BA1,0x61E0},
        {"ThreadManForUser",0,0x4001,0x28B6489C,0x61E8},
        {"ThreadManForUser",0,0x4001,0x3F53E640,0x61F0},
        {"ThreadManForUser",0,0x4001,0x4E3A1105,0x61F8},
        {"ThreadManForUser",0,0x4001,0x55C20A00,0x6200},
        {"ThreadManForUser",0,0x4001,0xEF9E4C70,0x6208},
        {"ThreadManForUser",0,0x4001,0x1FB15A32,0x6210},
        {"ThreadManForUser",0,0x4001,0x812346E4,0x6218},
        {"ThreadManForUser",0,0x4001,0x402FCF22,0x6220},
        {"ThreadManForUser",0,0x4001,0x30FD48F0,0x6228},
        {"ThreadManForUser",0,0x4001,0x82BC5777,0x6230},
    }};
    for(size_t i=0;i<imports.size();++i) {
        const auto& actual=record.imports[i]; const auto& expected=imports[i];
        if(!SameNativeModuleToken(actual.module,item.token) || actual.library!=expected.library
            || actual.version!=expected.version || actual.attributes!=expected.attributes
            || actual.nid!=expected.nid || actual.source_stub_va!=expected.stub
            || actual.abi!="unrecovered-psp32-adhocctl-import-v1" || actual.resolved || actual.partial_callable
            || actual.provider_serial || actual.provider_owner || actual.provider_generation) return false;
    }
    struct ExpectedExport { const char* library; const char* kind; uint16_t version,attributes; uint32_t nid,va; };
    constexpr std::array<ExpectedExport,27> exports{{
        {"","function",0,0x8000,0xD632ACDB,0x4D54},
        {"","function",0,0x8000,0xCEE8593C,0x4D5C},
        {"","variable",0,0x8000,0xF01D73A7,0x6450},
        {"sceNetAdhocctl","function",0,0x0001,0xE26F226E,0x0},
        {"sceNetAdhocctl","function",0,0x0001,0x9D689E13,0x1C0},
        {"sceNetAdhocctl","function",0,0x0001,0x0AD043ED,0x208},
        {"sceNetAdhocctl","function",0,0x0001,0xEC0635C1,0x28C},
        {"sceNetAdhocctl","function",0,0x0001,0x5E7F79C9,0x310},
        {"sceNetAdhocctl","function",0,0x0001,0x08FFF7A0,0x450},
        {"sceNetAdhocctl","function",0,0x0001,0x34401D65,0x480},
        {"sceNetAdhocctl","function",0,0x0001,0x20B317A0,0x4E0},
        {"sceNetAdhocctl","function",0,0x0001,0x6402490B,0x528},
        {"sceNetAdhocctl","function",0,0x0001,0x75ECD386,0x558},
        {"sceNetAdhocctl","function",0,0x0001,0x362CBE8F,0x594},
        {"sceNetAdhocctl","function",0,0x0001,0xE162CB14,0x5D0},
        {"sceNetAdhocctl","function",0,0x0001,0x99560ABE,0x624},
        {"sceNetAdhocctl","function",0,0x0001,0x8916C003,0x684},
        {"sceNetAdhocctl","function",0,0x0001,0xDED9D28E,0x6D4},
        {"sceNetAdhocctl","function",0,0x0001,0x81AEE1BE,0x710},
        {"sceNetAdhocctl","function",0,0x0001,0xA5C055CE,0xBC},
        {"sceNetAdhocctl","function",0,0x0001,0x1FF89745,0x3A8},
        {"sceNetAdhocctl","function",0,0x0001,0xCF8E084D,0x4B0},
        {"sceNetAdhocctl","function",0,0x0001,0x5A014CE0,0x740},
        {"sceNetAdhocctl_lib","function",0,0x0001,0xEFA6AEAF,0x77C},
        {"sceNetAdhocctl_lib","function",0,0x0001,0x1593C05D,0x7E8},
        {"sceNetAdhocctl_lib","function",0,0x0001,0xF8BABD85,0x854},
        {"sceNetAdhocctl_lib","function",0,0x0001,0x1C679240,0x890},
    }};
    for(size_t i=0;i<exports.size();++i) {
        const auto& actual=record.exports[i];const auto& expected=exports[i];
        const bool callable=expected.library[0]=='\0' && std::string_view(expected.kind)=="function"
            && ((expected.nid==0xD632ACDB && expected.va==0x4D54) || (expected.nid==0xCEE8593C && expected.va==0x4D5C));
        if(!SameNativeModuleToken(actual.module,item.token) || actual.library!=expected.library || actual.kind!=expected.kind
            || actual.version!=expected.version || actual.attributes!=expected.attributes || actual.nid!=expected.nid
            || actual.relative_va!=expected.va || actual.lifecycle_callable!=callable || actual.partial_callable) return false;
    }
    return true;
}
bool RepeatedAdhocctlContinuationExact(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=9) return false;
    const auto& s=o.repeated_modules;
    if(!s.present || !s.counter_known || s.counter!=4 || s.current_record_index!=4 || !s.source_gate_returned || s.source_gate_result!=1
        || s.source_segment!=1 || s.source_table_offset!=0x6950 || s.source_table_va!=0xEA4D0
        || s.table_bytes!=48 || s.known_bytes!=48 || s.writes_count!=4) return false;
    std::array<uint8_t,48> expected{{255,255,255,255,1,0,0,0,48,213,13,0,255,255,255,255,1,0,0,0,88,213,13,0,255,255,255,255,0,0,0,0,140,213,13,0,255,255,255,255,0,0,0,0,184,213,13,0}};
    for(size_t row=0;row<4;++row) {
        const auto uid=static_cast<uint32_t>(o.module_observations[5+row].module_load_result);
        for(size_t i=0;i<4;++i) expected[12*row+i]=uint8_t(uid>>(8*i));
        const auto& w=s.writes[row];
        if(w.ordinal!=row || w.source_pc!=0x4F728 || w.record_index!=row || w.table_offset!=12*row || w.value!=uid) return false;
    }
    for(size_t i=0;i<s.bytes.size();++i)
        if(s.known[i]!=1 || s.bytes[i]!=expected[i]) return false;
    for(size_t i=0;i<5;++i)
        if(o.module_observations[i].repeated_record_index!=dokodemo::StartupNoCurrentModule) return false;
    for(size_t row=0;row<4;++row)
        if(o.module_observations[5+row].repeated_record_index!=row) return false;
    return true;
}
bool NineModuleOwnersReady(const dokodemo::StartupObservation& o) {
    if(o.module_observations.size()!=9 || o.module_records.size()!=9 || o.current_module_index!=8
        || !PreparedEnvelopeModulesReady(o) || !SascoreRequestExact(o.module_observations[2],false)
        || !IfhandleRequestExact(o.module_observations[3],false) || !PspnetRequestExact(o.module_observations[4],false)
        || !MemabRequestExact(o.module_observations[5],false) || !AdhocAuthRequestExact(o.module_observations[6],false)
        || !AdhocRequestExact(o.module_observations[7],false) || !AdhocctlRequestExact(o.module_observations[8],false)
        || !RepeatedAdhocctlContinuationExact(o) || dokodemo::PendingWindowsSascoreCleanup()
        || dokodemo::PendingWindowsMemabCleanup()) return false;
    const auto& sas=o.module_observations[2].record.sascore.resources;
    if(uint64_t(sas.source_coordinate)+sas.committed_bytes>o.module_observations[3].staging_source_coordinate) return false;
    for(size_t i=0;i<9;++i) {
        const auto& x=o.module_observations[i];
        if(x.request_owner!=o.owner || !SameNativeModuleToken(x.token,o.module_records[i].token)) return false;
        for(size_t j=i+1;j<9;++j) {
            const auto& y=o.module_observations[j];
            if(x.token.uid==y.token.uid || x.token.serial==y.token.serial || x.token.owner!=y.token.owner
                || x.token.generation!=y.token.generation
                || uint64_t(x.staging_source_coordinate)+x.staging_committed_bytes>y.staging_source_coordinate) return false;
        }
    }
    return true;
}
bool SelectedGameSoundDrained(const dokodemo::StartupObservation& o) {
    const auto& a=o.native_audio;const auto& w=a.last_rendered;const auto& t=w.trace;
    return w.request_id&&a.last_drained_request_id==w.request_id&&a.started_requests&&a.completed_client_drain
        &&!a.synthetic_device&&t.group==0&&t.entry==4&&t.volume==128&&t.profile_gain==3&&t.effective_parameter==96
        &&t.master_left==128&&t.master_right==128&&w.bank_identity.owner&&w.bank_identity.generation&&w.bank_identity.serial
        &&t.region_index==34&&t.wave_index==32&&t.wave_offset==0x31440&&t.wave_bytes==2816
        &&t.command_bytes==23&&t.events==4&&t.pitch4096==2048&&t.combined_volume==3520&&t.base_gain==1939
        &&t.dry_left==1028&&t.dry_right==1028&&t.envelope1==0x80FF&&t.envelope2==0x5FC0
        &&t.storage_frames==4928&&t.input_frames==4900&&t.output_frames==9800&&t.first_end_block==175
        &&t.sequence_ended&&!t.key_off_at_ff&&t.reference_flag_policy
        &&StartupHexBytes(w.wave_sha256.data(),32)=="8ff5dc28e2f111a6bec52d27c14437532f611622de975ba9ef11de96c1ae8d05";
}
bool AtGameBootBoundary(const dokodemo::StartupObservation& o) {
    const auto& b=o.game_boot;
    if(!NineModuleOwnersReady(o)||!o.reached_100C||!b.memory_initialization_succeeded
        ||!b.graphics.live||!b.graphics.frame_sequence||!b.filesystem.mounted
        ||!b.scene.returned_9dc||!b.vm.manager_ready||!b.content.font_ready
        ||!b.content.scene_ui.live||!b.content.scene_ui.scene1_returned||b.content.closed)return false;
    for(const auto& arena:b.arenas)if(!arena.live||!arena.host_address)return false;
    return true;
}
// A running game's completion is not the immutable boot-resource oracle.
// Input admission remains in the existing conversation owner, not this fixture.
bool AtOwnedGameplayBoundary(const dokodemo::StartupObservation& o,bool initial_completed=false,bool notebook_completed=false){
    const auto& b=o.game_boot;const auto& c=b.content;const auto& a=c.actor_script;
    const auto& r=c.returned_interaction;const auto& own=a.owned_conversation;
    const auto& activation=r.activation;
    const bool initial=initial_completed&&r.admitted&&r.returned&&activation.live&&!activation.revoked&&!activation.consumed
        &&activation.body_completed&&activation.returned&&activation.main_consumed
        &&(activation.kind==dokodemo::recovered::GameConversationProofKind::Legacy85438
           ||activation.kind==dokodemo::recovered::GameConversationProofKind::Topic2B483);
    const bool owned=own.completed&&!own.failed&&own.main_live&&own.dispatch_ready
        &&activation.kind==dokodemo::recovered::GameConversationProofKind::OwnedMain&&activation.owned_receipt==own.receipt;
    const auto& n=c.notebook;
    const auto& flow=c.main_flow;
    const bool notebook_flow=flow.live&&flow.return20==0&&((flow.phase1C==0&&flow.substate24==0)
        ||(flow.phase1C==5&&flow.substate24==4&&flow.last_pc==0x2360C&&flow.phase5_entries>1
            &&flow.request8_clears>1&&!a.runtime.manager_request8&&!a.runtime.manager_request9));
    // A genuine resource9 poll clears469 and disarms returned Circle. Validate
    // the completed NOTE owner instead of requiring that old input eligibility.
    // This fixture predicate neither admits input nor changes the activation.
    const bool closed_note=notebook_completed&&r.admitted&&activation.live&&!activation.revoked&&!activation.consumed
        &&c.main_menu_notebook_requests==1&&c.main_menu_notebook_completions==1&&!c.main_menu_phase&&notebook_flow
        &&c.source469_resource==9&&!c.source_loader469&&c.source469_generation==n.generation&&c.source469_poll==n.polls
        &&n.generation&&n.retired_generation==n.generation&&n.phase==7&&n.state23C==3
        &&!n.live&&!n.visible&&!n.owner46C&&!n.request9&&!n.retirement_pending&&!n.text_owner
        &&n.opens==1&&n.close_requests==1&&n.close_completions==1&&n.polls==1&&n.retirements==1
        &&n.rows.valid&&n.rows.populated==0&&n.rows.run_id==a.vm_current.run_id&&n.rows.script_generation==a.generation
        &&a.notebook_after.valid&&a.notebook_after.sha==a.notebook_rows.sha&&!a.menu_notebook_live
        &&a.context_enabled&&!a.callback_depth&&!a.queued_callbacks&&a.callbacks_posted==a.callbacks_returned;
    if(!o.status||!o.reached_100C||!b.memory_initialization_succeeded||!b.graphics.live||!b.graphics.frame_sequence
       ||!b.filesystem.mounted||!b.scene.returned_9dc||!b.vm.manager_ready||!c.font_ready||c.closed
       ||!c.scene_ui.live||!c.scene_ui.scene1_returned||!c.status||c.blocked||!a.live||a.blocked
       ||!a.vm_current.valid||!a.vm_current.context_registered||a.vm_current.stopped
       ||!r.live||(!r.eligible&&!closed_note)||(!initial_completed&&r.armed)||r.event_posted||(!initial&&!owned))return false;
    if(o.module_records.size()!=9||o.module_observations.size()!=9)return false;
    for(size_t i=0;i<9;++i){const auto& record=o.module_records[i];const auto& item=o.module_observations[i];
        if(record.state!=dokodemo::recovered::NativeModuleState::Started||!record.input_live||!item.record_live
           ||!item.module_input_live||!item.staging_live||!item.staging_borrow_live
           ||!SameNativeModuleToken(record.token,item.token))return false;
    }
    for(const auto& arena:b.arenas)if(!arena.live||!arena.host_address)return false;
    return true;
}
bool AdhocctlSequenceClosed(const dokodemo::StartupObservation& o,bool require_complete_events=true) {
    return o.module_observations.size()==9 && QueryEnvelopeModulesClosed(o,require_complete_events)
        && SascoreRequestExact(o.module_observations[2],true) && IfhandleRequestExact(o.module_observations[3],true)
        && PspnetRequestExact(o.module_observations[4],true) && MemabRequestExact(o.module_observations[5],true)
        && AdhocAuthRequestExact(o.module_observations[6],true) && AdhocRequestExact(o.module_observations[7],true)
        && AdhocctlRequestExact(o.module_observations[8],true) && RepeatedAdhocctlContinuationExact(o)
        && !dokodemo::PendingWindowsSascoreCleanup() && !dokodemo::PendingWindowsMemabCleanup();
}

bool AtNativeModuleSequenceBoundary(const dokodemo::StartupObservation& observation) {
#ifdef DOKODEMO_FFMPEG_ROOT
    return AtGameBootBoundary(observation);
#else
    return AtLegacyNativeModuleSequenceBoundary(observation);
#endif
}
bool NativeModuleSequenceClosed(const dokodemo::StartupObservation& observation,bool require_complete_events=true) {
    if(observation.game_boot.content.actor_script.live||observation.game_boot.content.actor_dictionary_live
       ||observation.game_boot.content.actor_script.dictionary.imported||observation.game_boot.content.actor_script.dictionary.source_live
       ||observation.game_boot.content.actor_script.runtime.live||observation.game_boot.content.actor_script.runtime.model_slots
       ||observation.game_boot.content.actor_script.runtime.actor_model_live||observation.game_boot.content.actor_script.runtime.actor_request_pending
       ||observation.game_boot.content.actor_script.runtime.selected_extension_pending
       ||observation.game_boot.content.actor_script.runtime.active_command!=-1
       ||observation.game_boot.content.actor_script.runtime.pending_logical_motion!=-1
       ||observation.game_boot.content.actor_script.runtime.scene_model_live||observation.game_boot.content.actor_script.runtime.scene_controls
       ||observation.game_boot.content.actor_script.runtime.channel_motion[0]!=-1
       ||observation.game_boot.content.actor_script.runtime.channel_motion[1]!=-1)return false;
    if (observation.game_boot.content.vsync_owner || observation.game_boot.content.loadman_owner
        || observation.game_boot.content.font_ready || observation.game_boot.content.package_mounted
        || observation.game_boot.content.registered_records || !observation.game_boot.content.closed
        || observation.game_boot.content.title_mounted || observation.game_boot.content.title_ui_ready
        || observation.game_boot.content.autoload.buffer_live) return false;
    if(observation.game_boot.content.scene_ui.live || observation.game_boot.content.scene_ui.owners
        || observation.game_boot.content.sound_bank.metadata_ready || observation.game_boot.scene_resource_io.file_live
        || observation.game_boot.scene_resource_io.live_allocations)return false;
    if (observation.game_boot.vm.arena_live || observation.game_boot.vm.arena_bytes
        || observation.game_boot.vm.timer_live || observation.game_boot.vm.timer_started || observation.game_boot.vm.manager_ready) return false;
    if (observation.game_boot.scene.workspaces_ready || observation.game_boot.scene.pools_ready
        || observation.game_boot.scene.context_buffer_bytes || observation.game_boot.scene.pool_bytes) return false;
    if (observation.game_boot.filesystem.mounted || observation.game_boot.filesystem.io.file_live
        || observation.game_boot.filesystem.io.live_allocations) return false;
    if (observation.game_boot.graphics.live || observation.game_boot.graphics.color_bytes
        || observation.game_boot.graphics.depth_bytes || observation.game_boot.graphics.matrix_bytes) return false;
    for (const auto& arena : observation.game_boot.arenas) if (arena.live) return false;
    if (observation.module_observations.size() == 9 && observation.module_observations[8].module_uid_assigned) return AdhocctlSequenceClosed(observation,require_complete_events);
    if (observation.module_observations.size() == 9) return AdhocSequenceClosed(observation);
    if (observation.module_observations.size() == 8) return AuthSequenceClosed(observation);
    if (observation.module_observations.size() == 7) return MemabSequenceClosed(observation);
    if (observation.module_observations.size() == 6) return PspnetSequenceClosed(observation);
    if (observation.module_observations.size() == 5) return IfhandleSequenceClosed(observation);
    if (observation.module_observations.size() == 4) return SascoreSequenceClosed(observation);
    if (observation.module_observations.size() == 3) return QueryEnvelopeSequenceClosed(observation);
    if (observation.module_observations.size() != 2 || !observation.module_records.empty()
        || !observation.caller_joined || !observation.worker_joined || !observation.resources_closed
        || observation.callback_dispatch_count != 1 || observation.callback_dispatch_thread != observation.worker_thread_id
        || observation.dropped_events || observation.module_input_live || observation.staging_live || observation.staging_borrow_live
        || !observation.module_providers.empty() || dokodemo::StartupCoordinator::PendingDeferredCodecCleanup()) return false;
    for (const auto& item : observation.module_observations) {
        if (item.record_live || item.file_live || item.staging_live || item.staging_borrow_live || item.module_input_live
            || !item.module_unloaded || !item.module_input_released || !item.staging_freed_after_joins) return false;
    }
    const auto& first = observation.module_observations[0];
    const auto& second = observation.module_observations[1];
    if (second.record.prepared_envelope.present) {
        const auto& envelope = second.record.prepared_envelope;
        if (!envelope.native1001_blocked || envelope.source_init_returned || envelope.source_decode_returned
            || second.record.owned_native_codec_resources || first.record.owned_native_codec_resources
            || second.record.initialization.external_resources_acquired != 2
            || second.record.initialization.known_bytes != 64 || second.record.initialization.writes_count != 23) return false;
        for (const auto& native : envelope.records)
            if (native.state != dokodemo::recovered::NativeCodecWorkspaceState::Retired
                || native.provisional_live || !native.context_destroyed || !native.storage_freed
                || native.context_allocations != 1 || native.context_frees != 1) return false;
    }
    return first.module_stop_called && first.module_stop_returned && first.module_stop_result == 0
        && !first.module_initialization_abort_called && second.module_initialization_abort_called
        && second.module_initialization_aborted && !second.module_stop_called && !second.module_stop_returned
        && !second.module_start_returned && !second.module_start_entry_returned
        && !second.source_close_called && !second.source_close_returned && second.host_file_closed
        && second.record.initialization.aborted;
}
int ConnectedStartupTest(const Options& options) {
    std::filesystem::create_directories(options.evidence);
    dokodemo::StartupRuntimeStatus createStatus;
    dokodemo::StartupRuntimeOptions startupOptions{options.startupMedia.wstring()};
    startupOptions.native_save_root=options.nativeSaveRoot.wstring();
    startupOptions.native_title_layout=options.nativeTitleMenu?dokodemo::recovered::NativeTitleLayout::Menu5:dokodemo::recovered::NativeTitleLayout::CharacterEntrance;
    startupOptions.storage_unavailable=options.storageUnavailable;
#ifdef DOKODEMO_FFMPEG_ROOT
    startupOptions.prepared_codec_envelope = true;
#endif
    auto startup = dokodemo::StartupCoordinator::Create(startupOptions, createStatus);
    if (!startup || !createStatus) throw std::runtime_error("Cannot create native startup coordinator");
    const auto started = startup->Start();
    const auto duplicate = startup->Start();
    auto boundary = startup->Observe();
    const auto deadline = GetTickCount64() + 5000;
    while (!AtNativeModuleSequenceBoundary(boundary) && GetTickCount64() < deadline && boundary.status) {
        Sleep(1);
        boundary = startup->Observe();
    }
    WriteStartupObservation(options.evidence / L"startup-boundary.json", boundary);
    const bool reached = AtNativeModuleSequenceBoundary(boundary);
    const auto requested = startup->RequestExit();
    const auto exitDeadline = GetTickCount64() + 2000;
    auto dispatched = startup->Observe();
    while (!dispatched.callback_dispatch_count && GetTickCount64() < exitDeadline && dispatched.status) {
        Sleep(1);
        dispatched = startup->Observe();
    }
    WriteStartupObservation(options.evidence / L"startup-exit-dispatch.json", dispatched);
    const auto closed = startup->Close(5000);
    const auto closedAgain = startup->Close(5000);
    const auto after = startup->Observe();
    WriteStartupObservation(options.evidence / L"startup-after-close.json", after);
    const bool ok = started && duplicate.code == dokodemo::StartupRuntimeCode::AlreadyStarted
        && reached && requested && dispatched.callback_dispatch_count == 1
        && dispatched.callback_dispatch_thread == boundary.worker_thread_id
        && closed && closedAgain && after.caller_joined && after.worker_joined && after.resources_closed
        && !after.callback_live && !after.file_live && !after.media_live && !after.pending_module_live
        && !after.staging_live && !after.staging_borrow_live && after.staging_freed_after_joins
        && after.module_stop_called && after.module_stop_returned && after.module_stop_result == 0
        && after.module_unloaded && after.module_input_released && !after.module_input_live
        && NativeModuleSequenceClosed(after) && after.dropped_events == 0;
    std::ofstream summary(options.evidence / L"connected-startup-test.json", std::ios::binary);
    const auto* call = boundary.module_observations.size() > 1
        ? &boundary.module_observations[1].record.check_need_mem : nullptr;
    const auto* envelope = boundary.module_observations.size() > 1
        ? &boundary.module_observations[1].record.prepared_envelope : nullptr;
    uint32_t contextsPrepared = 0, contextsAllocated = 0, formatsPending = 0, queriesReturned = 0, allocationsReturned = 0;
    if (envelope && envelope->present) for (const auto& native : envelope->records) {
        if (native.native_open_returned && native.native_api_result == 0) ++contextsPrepared;
        if (native.context_allocated) ++contextsAllocated;
        if (native.format_pending) ++formatsPending;
        if (native.query_source_return) ++queriesReturned;
        if (native.get_edram_source_return) ++allocationsReturned;
    }
    summary << "{\"same_application_coordinator\":true,\"passed\":" << (ok ? "true" : "false")
        << ",\"multi_module_boundary\":" << (reached ? "true" : "false")
        << ",\"actual_encoded_staging\":" << (boundary.staging_full_read && boundary.staging_hash_matches ? "true" : "false")
        << ",\"native_module_load_start_close\":" << (reached ? "true" : "false")
        << ",\"native_lifecycle_and_partial_export\":true,\"all_audio_exports_implemented\":false"
        << ",\"check_need_mem_partial_provider\":" << (reached ? "true" : "false")
        << ",\"check_need_mem_returned\":" << (call && call->present ? (call->body.source_return ? "true" : "false") : "null")
        << ",\"ME_service_dispatched\":" << (call && call->present ? (call->body.dependency_dispatched ? "true" : "false") : "null")
        << ",\"native_prepared_envelope_policy\":" << (envelope && envelope->present ? "true" : "false")
        << ",\"native_codec_contexts_prepared\":" << contextsPrepared
        << ",\"native_codec_contexts_allocated\":" << contextsAllocated
        << ",\"native_codec_formats_pending\":" << formatsPending
        << ",\"second_source_start_returned\":" << (envelope && envelope->source_start_returned ? "true" : "false")
        << ",\"second_source_stop_returned\":" << (after.module_observations.size() > 1 && after.module_observations[1].record.prepared_envelope.source_stop_returned ? "true" : "false")
        << ",\"third_module_open_returned\":" << (boundary.module_observations.size() > 2 && boundary.module_observations[2].source_fd >= 0 ? "true" : "false")
        << ",\"third_module_body_read\":" << (boundary.module_observations.size() > 2 && boundary.module_observations[2].staging_host_read ? "true" : "false")
        << ",\"third_module_load_returned\":" << (boundary.module_observations.size() > 2 && boundary.module_observations[2].module_load_returned ? "true" : "false")
        << ",\"fourth_module_load_returned\":" << (boundary.module_observations.size()>3 && boundary.module_observations[3].module_load_returned ? "true" : "false")
        << ",\"fourth_module_start_returned\":" << (boundary.module_observations.size()>3 && boundary.module_observations[3].module_start_returned ? "true" : "false")
        << ",\"fourth_module_stop_returned\":" << (after.module_observations.size()>3 && after.module_observations[3].module_stop_returned ? "true" : "false")
        << ",\"fifth_module_open_returned\":" << (boundary.module_observations.size()>4 && boundary.module_observations[4].source_fd>=0 ? "true" : "false")
        << ",\"fifth_module_body_read\":" << (boundary.module_observations.size()>4 && boundary.module_observations[4].staging_host_read ? "true" : "false")
        << ",\"fifth_module_load_returned\":" << (boundary.module_observations.size()>4 && boundary.module_observations[4].module_load_returned ? "true" : "false")
        << ",\"fifth_module_start_returned\":" << (boundary.module_observations.size()>4 && boundary.module_observations[4].module_start_returned ? "true" : "false")
        << ",\"fifth_module_stop_returned\":" << (after.module_observations.size()>4 && after.module_observations[4].module_stop_returned ? "true" : "false")
        << ",\"sixth_module_open_returned\":" << (boundary.module_observations.size()>5 && boundary.module_observations[5].source_fd>=0 ? "true" : "false")
        << ",\"sixth_module_body_read\":" << (boundary.module_observations.size()>5 && boundary.module_observations[5].staging_host_read ? "true" : "false")
        << ",\"sixth_module_load_returned\":" << (boundary.module_observations.size()>5 && boundary.module_observations[5].module_load_returned ? "true" : "false")
        << ",\"sixth_module_start_returned\":" << (boundary.module_observations.size()>5 && boundary.module_observations[5].module_start_returned ? "true" : "false")
        << ",\"sixth_module_stop_returned\":" << (after.module_observations.size()>5 && after.module_observations[5].module_stop_returned ? "true" : "false")
        << ",\"seventh_module_open_returned\":" << (boundary.module_observations.size()>6 && boundary.module_observations[6].source_fd>=0 ? "true" : "false")
        << ",\"seventh_module_body_read\":" << (boundary.module_observations.size()>6 && boundary.module_observations[6].staging_host_read ? "true" : "false")
        << ",\"seventh_module_load_returned\":" << (boundary.module_observations.size()>6 && boundary.module_observations[6].module_load_returned ? "true" : "false")
        << ",\"seventh_module_start_returned\":" << (boundary.module_observations.size()>6 && boundary.module_observations[6].module_start_returned ? "true" : "false")
        << ",\"seventh_module_stop_returned\":" << (after.module_observations.size()>6 && after.module_observations[6].module_stop_returned ? "true" : "false")
        << ",\"eighth_module_open_returned\":" << (boundary.module_observations.size()>7 && boundary.module_observations[7].source_fd>=0 ? "true" : "false")
        << ",\"eighth_module_body_read\":" << (boundary.module_observations.size()>7 && boundary.module_observations[7].staging_host_read ? "true" : "false")
        << ",\"eighth_module_load_returned\":" << (boundary.module_observations.size()>7 && boundary.module_observations[7].module_load_returned ? "true" : "false")
        << ",\"eighth_module_start_returned\":" << (boundary.module_observations.size()>7 && boundary.module_observations[7].module_start_returned ? "true" : "false")
        << ",\"eighth_module_stop_returned\":" << (after.module_observations.size()>7 && after.module_observations[7].module_stop_returned ? "true" : "false")
        << ",\"ninth_module_open_returned\":" << (boundary.module_observations.size()>8 && boundary.module_observations[8].source_fd>=0 ? "true" : "false")
        << ",\"ninth_module_body_read\":" << (boundary.module_observations.size()>8 && boundary.module_observations[8].staging_host_read ? "true" : "false")
        << ",\"ninth_module_load_returned\":" << (boundary.module_observations.size()>8 && boundary.module_observations[8].module_load_returned ? "true" : "false")
        << ",\"ninth_module_start_returned\":" << (boundary.module_observations.size()>8 && boundary.module_observations[8].module_start_returned ? "true" : "false")
        << ",\"ninth_module_stop_returned\":" << (after.module_observations.size()>8 && after.module_observations[8].module_stop_returned ? "true" : "false")
        << ",\"repeated_counter\":" << (boundary.repeated_modules.counter_known ? std::to_string(boundary.repeated_modules.counter) : "null")
        << ",\"repeated_uid_writes\":" << boundary.repeated_modules.writes_count
        << ",\"repeated_gate_returned\":" << (boundary.repeated_modules.source_gate_returned ? "true" : "false")
        << ",\"next_game_call\":"; WriteNextGameCallValue(summary, boundary);
    summary << ",\"game_boot\":"; WriteGameBootValue(summary, boundary.game_boot);
    summary << ",\"source_queries_returned\":" << queriesReturned
        << ",\"source_GetEDRAM_returned\":" << allocationsReturned
        << ",\"original_ME_equivalence_known\":false,\"source_decode_returned\":false"
        << ",\"owned_modules\":" << boundary.module_records.size()
        << ",\"second_partial_start_without_return\":" << (boundary.module_observations.size() > 1 && !boundary.module_observations[1].module_start_returned ? "true" : "false")
        << ",\"partial_init_aborted_after_joins\":" << (after.module_observations.size() > 1 && after.module_observations[1].module_initialization_aborted ? "true" : "false")
        << ",\"duplicate_start_rejected\":" << (duplicate.code == dokodemo::StartupRuntimeCode::AlreadyStarted ? "true" : "false")
        << ",\"preceding_psp_history\":false,\"PRX_bodies_implemented\":0,\"required_PRX_bodies\":9"
        << ",\"old_ECB_advanced\":0,\"coverage_delta\":0,\"full_game\":false}\n";
    Output(std::string("CONNECTED_STARTUP ") + (ok ? "PASS" : "FAIL") + "\n");
    return ok ? 0 : 1;
}
struct Application {
    Options options; HWND window = nullptr; HFONT font = nullptr, titleFont = nullptr;
    std::unique_ptr<dokodemo::StartupCoordinator> nativeStartup;
    dokodemo::StartupObservation startupObservation;
    dokodemo::recovered::ControllerSample controllerInput;
    uint64_t renderedFrameGeneration = 0, renderedFrameSequence = 0;
    bool graphicsPatternDrawn = false;
    bool graphicsSmokeResized = false;
    uint64_t presentsBeforeResize = 0;
    std::wstring startupStatus = L"Нативный запуск ещё не начат.";
    bool startupWindowCloseSent = false;
    bool startupWindowBoundaryReached=false;
    unsigned storageInputStep=0;uint64_t storageInputStart=0,titleReadyFrame=0;
    bool heldIgnored=false,unfocusedIgnored=false,closingObserved=false,storageBoundaryPassed=false;
    bool titleEarlyCaptured=false,titleAnimationObserved=false;
    unsigned titleMenuStep=0;uint32_t titleMenuWait=0;
    bool titleArrivalObserved=false,titleMenuSelectionObserved=false;
    dokodemo::recovered::GameTitleSnapshot titleEarly;
    dokodemo::recovered::GameTextKeyGate textGate;
    bool imeComposing=false;
    wchar_t highSurrogate=0;
    uint64_t imeContext=0;
    uint32_t softkeyStep=0,softkeyWait=0;
    uint64_t cancelledTextContext=0;
    bool textCancelObserved=false,textCloseObserved=false,staleTextRefused=false,playerCancelObserved=false;
    uint64_t testedEditorContext=0;
    bool birthdayBackObserved=false,dayValidationPending=false,dayRejectedObserved=false;
    unsigned messageTestStep=0;uint32_t messageTestWait=0;
    bool messageHeldRefused=false,messageFocusRefused=false,messageFreshAccepted=false;
    uint32_t conversationNextMessage=2,conversationInputStage=0,conversationWait=0;
    uint32_t inventoryTestStep=0,inventoryTestWait=0;bool inventoryFilterEmpty=false,inventoryCancelObserved=false,inventoryPendingSaved=false,inventoryClosingSaved=false;
    bool discardPassed=false,discardCancelPassed=false,discardHeldPassed=false;uint64_t discardCancelExecuted=0;
    bool modelRemovalPassed=false;int32_t putawaySlot=-1;uint64_t putawayGeneration=0,putawayReceipt=0;
    uint32_t putawaySlotsBefore=0,putawayPublicationsBefore=0,putawayRetirementsBefore=0,putawayRetireRequestsBefore=0;
    std::vector<std::pair<int32_t,dokodemo::recovered::GameRoomModelSlotSnapshot>> putawayOtherModels;
    bool inventoryActionPassed=false,decorationPassed=false;uint32_t decoratePublicationsBefore=0;uint64_t itemMarkReceipt=0;int32_t itemActionRow=-1,itemActionId=-1;
    bool managerHeldRefused=false,managerFocusRefused=false,managerNextHeldRefused=false;
    bool post17HeldRefused=false,choiceHeldRefused=false,choiceCancelRefused=false,choiceClosingObserved=false,postChoiceHeldRefused=false;
    bool post19HeldRefused=false,post20HeldRefused=false,post21HeldRefused=false,secondChoiceHeldRefused=false,secondChoiceClosingObserved=false,reply22HeldRefused=false;
    bool post19FirstUnsupportedSaved=false;
    bool post19LinearRoute=false;
    bool post23HeldRefused=false,post24HeldRefused=false,post21BranchMismatch=false;
    uint64_t post25ExpectedFrame=0,post25PriorReturnSequence=0;
    uint32_t post25SourceEventBaseline=0;
    bool post25TriangleEventObserved=false,post25RTriggerEventObserved=false,post25ReturnedHeldRefused=false,post25ReturnedPosted=false;
    bool post27ChoiceHeldRefused=false,post27ChoiceClosingObserved=false;
    bool post27InputHeldRefused=false,post27InputClosingObserved=false;
    bool post28ChoiceHeldRefused=false,post28ChoiceClosingObserved=false,post28NextHeldRefused=false;
    unsigned post28AlternateStep=0,post28AlternateCuts=0;
    uint64_t post28AlternateWait=0;
    uint32_t post28AlternateAck=0,post28AlternateChoices=0;
    uint32_t post28AlternateMessage=0;
    bool activationBodySaved=false,activationFarewellSaved=false,activationPublished=false,activationPosted=false;
    bool activationHeldRefused=false;
    uint64_t activationWait=0;uint32_t activationChoicesAtPost=0;
    dokodemo::script::VmStateWitness naturalLoopWait;
    uint64_t naturalLoopGeneration=0;unsigned naturalLoopCycles=0;
    bool naturalLoopPassed=false;
    unsigned repeatStep=0;uint64_t repeatWait=0,repeatGeneration=0,repeatFirstReceipt=0,repeatActivation=0;
    uint32_t repeatPostedBase=0;bool repeatPassed=false;
    unsigned repeatEditorStep=0;uint32_t repeatEditorBaseline=0;uint64_t repeatEditorWait=0;
    unsigned playStep=0,playCycles=0;uint64_t playGeneration=0,playOrigin=0,playReceipt=0,playWait=0;
    uint32_t playPosted=0;bool playPassed=false;
    uint32_t mainMenuFixtureStep=0,mainMenuFixtureBaseline=0,mainMenuFixtureReturns=0;uint64_t mainMenuFixtureWait=0,mainMenuFixtureOwner=0,mainMenuFixtureGeneration=0;
    bool mainMenuFixturePassed=false;uint64_t mainMenuFixtureExecuted=0;
    unsigned noteStep=0,noteNav=0;bool notePassed=false,noteHeld=false,noteFocus=false,flow5Passed=false,noteSelfPassed=false;
    uint64_t noteSelfEpoch=0,noteSelfDraws=0;
    uint32_t flow5Attempts=0,flow5Callbacks=0,flow5Events=0;
    uint64_t noteWait=0,noteOwner=0,noteAvailableGen=0,noteActivation=0,noteExecuted=0,noteArmSequence=0;
    uint32_t noteCallbacks=0,noteAcks=0,noteScriptRequests=0;int32_t noteRow=0;
    unsigned miStep=0,miNav=0;uint64_t miWait=0,miOwner=0,miGeneration=0,miActivation=0,miExecuted=0;
    uint32_t miCallbacks=0,miScriptRequests=0,miScriptCompletions=0,miRetirements=0,miInventoryRequests=0,miAcks=0;int32_t miRow=0;
    uint64_t miInventoryGeneration=0;bool miHeldRefused=false,miFocusRefused=false,miReturnHeldRefused=false;

    unsigned mmNav=0;int32_t mmRow=-1,mmId=-1;uint32_t mmInitialFlag=0,mmActions=0;uint64_t mmMarkReceipt=0;bool mmMarkPassed=false;
    bool mpPassed=false,mpInputGuardsDone=false;int32_t mpRow=-1,mpId=-1,mpSlot=-1;uint64_t mpReceipt=0,mpGeneration=0;unsigned mpActions=0,mpNav=0;
    // Fixture-only continuation: the first child retires with its model still live.
    bool mrPassed=false,mrOwnershipPassed=false,mrRoomWrites=false;unsigned mrStage=0;uint64_t mrEpoch=0,mrAsset=0,mrClone=0,mrMenuGeneration=0,mrGuiGeneration=0;
    std::array<uint8_t,32> mrPlacedImage{};uint64_t mrDrawFrames=0,mrDrawPixels=0;
    unsigned gameplayUi=0,gameplayPosts=0,gameplayCuts=0;uint64_t gameplayWait=0,gameplayGeneration=0;uint32_t gameplayPosted=0,gameplayBaseline=0;bool gameplayPassed=false,gameplayKnownInput=false,gameplayCorrectedInput=false,gameplayWordRow=false,gameplayEcho=false;
    std::vector<std::pair<uint32_t,std::string>> fixtureChoiceHistory;
    std::vector<std::string> fixtureChoiceOptions;
    uint64_t fixtureChoiceContext=0,fixtureChoiceOwner=0;
    uint32_t fixtureChoicePC=0,fixtureChoiceTarget=0,fixtureChoicePrevious=0,fixtureChoiceRequest=0;
    uint32_t fixtureChoiceImported=0,fixtureChoiceCompleted=0,fixtureChoiceNovel=0;
    std::string fixtureInputExpected;
    uint64_t fixtureInputContext=0,fixtureInputGeneration=0;
    uint32_t fixtureInputRequest=0,fixtureInputPC=0,fixtureInputScriptContext=0,fixtureGreetingAnswers=0;
    bool fixtureGreetingObserved=false;
    unsigned kanaStep=0,kanaUiStep=0,kanaSeekStep=0,kanaSeekCount=0;uint64_t kanaWait=0;
    uint64_t kanaSeekGeneration=0;uint32_t kanaSeekPosted=0;
    uint32_t kanaInputBefore=0,kanaAckBefore=0,kanaChoiceBefore=0;
    bool kanaPassed=false,kanaHeldRefused=false,kanaClosing=false,kanaWordPassed=false,kanaEchoSeen=false;
    bool soundRenderedCaptured=false,soundDrainedCaptured=false;
    bool post29ChoiceHeldRefused=false,post29ChoiceClosingObserved=false,post29NextHeldRefused=false;
    bool profileNoObserved=false;
    std::vector<std::filesystem::path> images; size_t selected = 0; Image image;
    std::vector<unsigned char> displayPixels;
    std::wstring status; uint64_t frames = 0, inputEvents = 0; bool imageLoaded = false, smokeInputSent = false;
    unsigned constructionSmokePasses = 0;
    LARGE_INTEGER frequency{}, start{}, last{}; double elapsed = 0;
    dokodemo::save::FoundationState state;
    dokodemo::save::ClockTracker clock;
    dokodemo::script::Program program;
    std::unique_ptr<dokodemo::script::Vm> vm;
    dokodemo::audio::Win32Player player;
    GameFont gameFont;
    Image messageImage;
    bool messageVisible=false;
    bool savedSuccessfully=false, loadedSuccessfully=false, startupPrefixVerified=false, service47Verified=false, bulkVerified=false;
    std::unique_ptr<dokodemo::recovered::OwnedManagerQueue> managerQueue;
    std::unique_ptr<dokodemo::recovered::OwnedContextOwner> callbackOwner;
    std::unique_ptr<dokodemo::recovered::OwnedProducer> producer;
    bool managerVerified=false, storageVerified=false, clockStorageVerified=false;
    std::array<std::unique_ptr<dokodemo::startup::StartupProbe>,2> storageProbes;
    std::array<std::unique_ptr<dokodemo::startup::StartupProbe>,2> clockStorageProbes;
    std::shared_ptr<dokodemo::platform::WindowsClockCache> sourceClockCache=
        std::make_shared<dokodemo::platform::WindowsClockCache>();
    int width = 1080, height = 760; HDC bufferDC = nullptr; HBITMAP buffer = nullptr; HGDIOBJ oldBuffer = nullptr; void* pixels = nullptr;
    void LoadFixtureChoiceHistory(){
        if(options.fixtureChoiceHistory.empty())return;
        if(std::filesystem::file_size(options.fixtureChoiceHistory)>65536)throw std::runtime_error("Fixture choice journal size bound");
        std::ifstream in(options.fixtureChoiceHistory,std::ios::binary);std::string line;
        if(!std::getline(in,line)||line!="DOKODEMO_UI_CHOICES_V1")throw std::runtime_error("Fixture choice journal header");
        while(std::getline(in,line)){
            std::istringstream fields(line);uint32_t pc=0;std::string text,extra;
            if(!(fields>>pc>>text)||fields>>extra||text.size()>8192||text.empty()||text.size()%2
               ||!std::all_of(text.begin(),text.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))
                throw std::runtime_error("Fixture choice journal record");
            const auto key=std::make_pair(pc,text);
            if(std::find(fixtureChoiceHistory.begin(),fixtureChoiceHistory.end(),key)==fixtureChoiceHistory.end())fixtureChoiceHistory.push_back(key);
            if(fixtureChoiceHistory.size()>128)throw std::runtime_error("Fixture choice journal count bound");
        }
        if(in.bad())throw std::runtime_error("Fixture choice journal read");
        fixtureChoiceImported=uint32_t(fixtureChoiceHistory.size());
    }
    bool FixtureChoiceSeen(uint32_t pc,const std::string& text)const{
        return std::find(fixtureChoiceHistory.begin(),fixtureChoiceHistory.end(),std::make_pair(pc,StartupHexBytes(reinterpret_cast<const uint8_t*>(text.data()),text.size())))!=fixtureChoiceHistory.end();
    }
    bool RecordFixtureChoice(){
        const auto& text=fixtureChoiceOptions.at(fixtureChoiceTarget);
        const bool novel=!FixtureChoiceSeen(fixtureChoicePC,text);
        auto next=fixtureChoiceHistory;
        if(novel){
            if(next.size()>=128)return false;
            next.emplace_back(fixtureChoicePC,StartupHexBytes(reinterpret_cast<const uint8_t*>(text.data()),text.size()));
        }
        std::ostringstream serialized;serialized<<"DOKODEMO_UI_CHOICES_V1\n";
        for(const auto& entry:next)serialized<<entry.first<<' '<<entry.second<<'\n';
        const auto bytes=serialized.str();if(bytes.size()>65536)return false;
        std::ofstream out(options.evidence/L"fixture-choice-history.txt",std::ios::binary|std::ios::trunc);
        out.write(bytes.data(),std::streamsize(bytes.size()));out.flush();out.close();
        if(!out)return false;
        fixtureChoiceHistory=std::move(next);++fixtureChoiceCompleted;if(novel)++fixtureChoiceNovel;
        return true;
    }
    explicit Application(Options value) : options(std::move(value)) {
        LoadFixtureChoiceHistory();
        images = FindImages(options.assets);
        // Prefer a prepared background/full screen to glyphs or tiny atlas members.
        for (size_t i = 0; i < images.size(); ++i) {
            auto name = images[i].wstring();
            std::transform(name.begin(), name.end(), name.begin(), towlower);
            if (name.find(L"title.bpm") != std::wstring::npos && name.find(L"alert.bmp") != std::wstring::npos) { selected = i; break; }
        }
        QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start); last = start;
        state.character_key_utf8 = "NEKO";
        font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Meiryo");
        titleFont = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        LoadSelected();
        dokodemo::script::Diagnostic diagnostic;
        if (program.load_original(options.assets / L"scripts/original/NEKO.KSC", diagnostic)) vm = std::make_unique<dokodemo::script::Vm>(program);
        std::wstring fontError; gameFont.Load(options.assets / L"graphics/font",fontError);
    }
    ~Application() {
        if (nativeStartup) (void)nativeStartup->Close();
        ReleaseBuffer(); DeleteObject(font); DeleteObject(titleFont);
    }
    void BeginNativeStartup() {
        dokodemo::StartupRuntimeStatus created;
        dokodemo::StartupRuntimeOptions startupOptions{options.startupMedia.wstring()};
#ifdef DOKODEMO_FFMPEG_ROOT
        startupOptions.prepared_codec_envelope = true;
#endif
        startupOptions.native_save_root=options.nativeSaveRoot.wstring();
    startupOptions.native_title_layout=options.nativeTitleMenu?dokodemo::recovered::NativeTitleLayout::Menu5:dokodemo::recovered::NativeTitleLayout::CharacterEntrance;
        startupOptions.test_controller_input=options.storageWindowTest;
        startupOptions.test_actor_calendar_step_minutes=options.conversationWindowFixture?5:0;
        startupOptions.test_rng_seed_replay=options.testRngSeedReplay;startupOptions.test_rng_seed_word=options.testRngSeedWord;
        startupOptions.test_missing_device_prompt=options.storageWindowTest&&!options.storageDirectTest;
        startupOptions.storage_unavailable=options.storageUnavailable;
        nativeStartup = dokodemo::StartupCoordinator::Create(startupOptions, created);
        if (!nativeStartup || !created) {
            startupStatus = L"Нативный запуск: не удалось создать владельца Windows-сервисов.";
            return;
        }
        const auto started = nativeStartup->Start();
        if (!started) startupStatus = L"Нативный запуск: ошибка создания потоков Windows.";
        else RefreshNativeStartup();
    }
    void RefreshNativeStartup() {
        if (!nativeStartup) return;
        startupObservation = nativeStartup->Observe();
        if(options.soundWindowFixture&&!startupObservation.resources_closed){
            if(!soundRenderedCaptured&&startupObservation.native_audio.last_rendered.request_id){
                soundRenderedCaptured=true;WriteStartupObservation(options.evidence/L"sound-rendered.json",startupObservation);
            }
            if(!soundDrainedCaptured&&SelectedGameSoundDrained(startupObservation)){
                soundDrainedCaptured=true;WriteStartupObservation(options.evidence/L"sound-drained.json",startupObservation);
            }
        }
        SyncTextContext();
        if (startupObservation.resources_closed) startupStatus = L"Нативный запуск: потоки завершены, ресурсы освобождены.";
        else if (!startupObservation.status) startupStatus = L"Нативный запуск: ошибка Windows-сервиса; подробности в журнале.";
        else if (AtGameBootBoundary(startupObservation)) {
            const auto& c=startupObservation.game_boot.content;
            if(c.actor_script.started){
                startupStatus=L"Сцена игры: оригинальная комната и actor/motion owners — функциональная Windows-сборка.";
                status=c.actor_script.awaiting_message?L"X = ○: новое нажатие для продолжения сообщения.":Utf8(c.status.detail);
            }else if(c.title.frame_drawn) {
                startupStatus=L"TITLE: игровое меню и персонажи — промежуточная Windows-сборка.";
                status=c.title.softkey_requested?L"Windows text/IME → CP932: вводите текст, Enter — принять, Esc — отменить. Стрелки/Home/End/Delete — native caret; NJ engine пока не реализован."
                    :L"X = ○ подтвердить; Z = × назад; стрелки — меню и выбор персонажа. Звуковой движок пока недоступен.";
                if(c.scene_ui.editor.live&&c.scene_ui.editor.source_state!=3){
                    status=Utf8(c.scene_ui.editor_label)+L"  "+Utf8(c.scene_ui.editor.error);
                }
            } else if(c.title_constructor_returned) {
                startupStatus=L"TITLE: модели/анимации загружены; исходная fade-цепочка, ожидание видимости.";
                status=L"Явная native character-entry policy; неизвестный 7E65C не считается наблюдённым PSP значением.";
            } else if(c.title_model_read) {
                startupStatus=L"TITLE.BPM / TITLE.CB2A / toro.DVS прочитаны; model consumer7F68C/80DA8 ещё впереди.";
                status=L"Title/gameplay ещё не отрисованы. SGX mixer недоступен.";
            } else if(c.scene_ui.storage_dialog) {
                startupStatus=L"Circle (X): отпустите и нажмите. TRC закрывается естественно; затем native AUTOLOAD.";
                status=L"Игровые строки/FONT.XFT; native panel. PSP savedata не импортируется как native формат.";
            } else {startupStatus=L"Native storage/loading: точный frontier/status в evidence.";status=Utf8(c.status.detail);}
        }
        else if (AtNativeModuleSequenceBoundary(startupObservation))
            startupStatus = L"Запущено модулей: " + std::to_wstring(std::count_if(
                startupObservation.module_records.begin(), startupObservation.module_records.end(),
                [](const auto& module) { return module.state == dokodemo::recovered::NativeModuleState::Started; }))
                + L". Следующий модуль пока не поддерживается.";
        else if (startupObservation.barrier_source_pc == 0x4F53C && startupObservation.staging_full_read)
            startupStatus = L"Нативный запуск: файл модуля проверен; достигнута невосстановленная функция.";
        else if (startupObservation.barrier_source_pc == 0x4F53C)
            startupStatus = L"Нативный запуск: проверяется текущий модуль.";
        else startupStatus = L"Нативный запуск: рабочий поток проверяет игровой носитель.";
    }
    bool CloseNativeStartup() {
        if (!nativeStartup) return true;
        (void)nativeStartup->RequestExit();
        const auto deadline = GetTickCount64() + 250;
        RefreshNativeStartup();
        while (startupObservation.callback_live && !startupObservation.callback_dispatch_count && GetTickCount64() < deadline) {
            Sleep(1);
            RefreshNativeStartup();
        }
        const auto closed = nativeStartup->Close(5000);
        RefreshNativeStartup();
        if (!closed) startupStatus = L"Нативный запуск: ожидается безопасное завершение потоков.";
        return bool(closed);
    }
    void RequestWindowClose() {
        if (CloseNativeStartup()) DestroyWindow(window);
        else InvalidateRect(window, nullptr, FALSE);
    }
    void ReleaseBuffer() {
        if (bufferDC && oldBuffer) SelectObject(bufferDC, oldBuffer);
        if (buffer) DeleteObject(buffer);
        if (bufferDC) DeleteDC(bufferDC);
        bufferDC = nullptr; buffer = nullptr; oldBuffer = nullptr; pixels = nullptr;
    }
    void LoadSelected() {
        messageVisible=false;
        imageLoaded = false;
        if (images.empty()) { status = L"Данные не подготовлены. Выполните scripts/bootstrap-assets.ps1."; return; }
        std::wstring error;
        imageLoaded = LoadBmp(images[selected], image, error);
        if(imageLoaded) {
            displayPixels=image.bgra;
            // Explicit black diagnostic matte for straight-alpha source pixels.
            for(size_t p=0;p<displayPixels.size();p+=4) {
                for(int c=0;c<3;++c) displayPixels[p+c]=static_cast<unsigned char>(unsigned(displayPixels[p+c])*displayPixels[p+3]/255);
                displayPixels[p+3]=255;
            }
        }
        status = imageLoaded ? L"Оригинальный ресурс декодирован; сценарная сцена ещё не восстановлена." : error;
    }
    void Text(HDC dc, const std::wstring& text, RECT rect, COLORREF color, UINT flags = DT_LEFT | DT_TOP | DT_WORDBREAK) {
        SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT); DrawTextW(dc, text.c_str(), -1, &rect, flags);
    }
    void Fill(HDC dc, RECT rect, COLORREF color) { auto brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush); }
    void Render(HDC dc) {
        renderedFrameGeneration = renderedFrameSequence = 0;
        Fill(dc, {0,0,width,height}, RGB(18,23,30));
        SelectObject(dc, titleFont);
        Text(dc, L"Doko Demo Issyo · native recovery", {24,18,width-24,58}, RGB(241,244,246));
        SelectObject(dc, font);
        Text(dc, L"Промежуточная сборка восстановления. Полная игра пока не реализована.", {24,58,width-24,88}, RGB(255,198,109));
        Text(dc, startupStatus, {24,88,width-24,118}, RGB(181,205,221), DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_SINGLELINE);
        int top = 128, bottom = std::max(top + 100, height - 178);
        int availableW = width - 48, availableH = bottom - top;
        double scale = std::min(double(availableW) / 480.0, double(availableH) / 272.0);
        int vw = int(480 * scale), vh = int(272 * scale), vx = (width-vw)/2, vy = top+(availableH-vh)/2;
        Fill(dc, {vx,vy,vx+vw,vy+vh}, RGB(0,0,0));
        dokodemo::recovered::GameGraphicsFrame gameFrame;
        const bool gameFrameReady = nativeStartup && nativeStartup->CopyGameFrame(gameFrame);
        if (gameFrameReady) {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(gameFrame.stride);
            info.bmiHeader.biHeight = -static_cast<LONG>(gameFrame.height);
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            const auto result = StretchDIBits(dc, vx, vy, vw, vh, 0, 0, static_cast<int>(gameFrame.width),
                static_cast<int>(gameFrame.height), gameFrame.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            if (result && result != static_cast<int>(GDI_ERROR)) {
                renderedFrameGeneration = gameFrame.generation; renderedFrameSequence = gameFrame.sequence;
            }
            if(gameFrame.diagnostic)Text(dc,L"RENDERER TEST FRAME — не игровой контент",
                {vx + 12, vy + vh - 30, vx + vw - 12, vy + vh - 6}, RGB(255,255,255), DT_LEFT | DT_TOP | DT_SINGLELINE);
        } else if (messageVisible) {
            // Isolated bytecode fixture on a neutral surface, with original XFT
            // pixels. It is never attached to an invented gameplay background.
            auto raster=messageImage.bgra;
            for(size_t p=0;p<raster.size();p+=4) {
                for(int c=0;c<3;++c) raster[p+c]=static_cast<unsigned char>(unsigned(raster[p+c])*raster[p+3]/255);
                raster[p+3]=255;
            }
            BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=messageImage.width;
            info.bmiHeader.biHeight=-messageImage.height; info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            int drawScale=std::max(1,int(scale*2));
            StretchDIBits(dc,vx+int(16*scale),vy+int(80*scale),messageImage.width*drawScale,messageImage.height*drawScale,
                0,0,messageImage.width,messageImage.height,raster.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        } else if (imageLoaded) {
            // Preserve resource aspect ratio. A texture does not become an invented game scene.
            double imageScale = std::min(double(vw)/image.width, double(vh)/image.height);
            int iw = int(image.width*imageScale), ih = int(image.height*imageScale);
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = image.width;
            info.bmiHeader.biHeight = -image.height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, vx+(vw-iw)/2, vy+(vh-ih)/2, iw, ih, 0,0,image.width,image.height,displayPixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        } else Text(dc, status, {vx+20,vy+20,vx+vw-20,vy+vh-20}, RGB(239,239,239));
        int y = bottom + 16;
        auto source = gameFrameReady ? L"C0C · native480×272 framebuffer · nonmatching Windows renderer" : messageVisible ? L"NEKO.KSC · файл 0xC373 · изолированная функция · аргументы 10, 14" : imageLoaded ? std::filesystem::relative(image.path, options.assets).wstring() : L"нет ресурса";
        Text(dc, L"Исходный ресурс: " + source, {24,y,width-24,y+28}, RGB(181,205,221), DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_SINGLELINE);
        Text(dc, gameFrameReady && gameFrame.diagnostic
            ? L"Диагностика native renderer: синтетические примитивы, не декодированный игровой ресурс." : status,
            {24,y+30,width-24,y+78}, RGB(211,219,223));
        Text(dc,nativeStartup&&!options.smoke?L"Стрелки — выбор · X — ○ · Z — × · Esc — выход · F12 — снимок"
            :L"←/→ ресурсы  F1/F4 KSC  F2 текст  F3 звук  F6 проба  F7 очередь  F8 storage  F10 clock  F5/F9 save/load", {24,height-62,width-24,height-32}, RGB(167,181,192));
        std::wostringstream metrics; metrics << L"Win32 · " << frames << L" кадров · " << inputEvents << L" событий ввода · " << images.size() << L" ресурсов";
        Text(dc, metrics.str(), {24,height-31,width-24,height-8}, RGB(125,149,163));
    }
    void EnsureBuffer() {
        if (bufferDC) return;
        auto dc = GetDC(window); bufferDC = CreateCompatibleDC(dc);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        buffer = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0); ReleaseDC(window, dc);
        if (!buffer || !bufferDC) throw std::runtime_error("Cannot create native rendering buffer");
        oldBuffer = SelectObject(bufferDC, buffer);
    }
    void Paint() {
        EnsureBuffer(); Render(bufferDC);
        PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint);
        const auto shown = BitBlt(dc,0,0,width,height,bufferDC,0,0,SRCCOPY);
        EndPaint(window,&paint);
        if (shown && nativeStartup && renderedFrameSequence)
            (void)nativeStartup->RecordGameFramePresented(renderedFrameGeneration, renderedFrameSequence);
    }
    void Capture() {
        if(options.post17WindowFixture&&(IsIconic(window)||width<480||height<272)){
            ShowWindow(window,SW_RESTORE);
            SetWindowPos(window,nullptr,0,0,1100,800,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
            RECT client{};GetClientRect(window,&client);width=client.right-client.left;height=client.bottom-client.top;ReleaseBuffer();
        }
        EnsureBuffer(); Render(bufferDC); std::wstring error;
        GdiFlush(); //complete GDI writes to the DIB before CPU BMP serialization
        if (WriteBmp(options.evidence / L"native-window.bmp", width,height,pixels,error)) status = L"Снимок записан в " + options.evidence.wstring();
        else status = error;
    }
    void Audio() {
        if(player.IsPlaying()) {player.Stop();status=L"Нативное воспроизведение остановлено.";return;}
        const auto* catalog=dokodemo::audio::FindOriginalClip("BGM_TITLE");
        if(!catalog) {status=L"Исходный BGM_TITLE не проиндексирован.";return;}
        auto path=options.assets / std::filesystem::path(catalog->path).lexically_relative(L"assets/native");
        std::shared_ptr<const dokodemo::audio::PcmClip> clip; std::string error;
        if(!dokodemo::audio::LoadPcm16Wave(path,clip,error)) {status=Utf8(error);return;}
        if(clip->FrameCount()!=catalog->frames || clip->sample_rate!=catalog->rate || clip->channels!=catalog->channels) {status=L"PCM не совпадает с оригинальным аудиокаталогом.";return;}
        std::optional<dokodemo::audio::SampleLoop> loop;
        if(catalog->loop_begin>=0 && catalog->loop_end>catalog->loop_begin) loop=dokodemo::audio::SampleLoop{static_cast<uint64_t>(catalog->loop_begin),static_cast<uint64_t>(catalog->loop_end)};
        bool ok=player.Play(clip,loop,error);
        status=ok ? L"Оригинальный BGM_TITLE: нативный WinMM, intro + исходный sample loop. F3 остановит звук." : Utf8(error);
    }
    bool PostText(dokodemo::recovered::GameTextKind kind,uint64_t context=0,std::string_view value={}){
        if(!nativeStartup)return false;
        dokodemo::recovered::GameTextEvent event;
        event.context=context?context:textGate.Id();event.kind=kind;event.test_injected=options.softkeyWindowTest;
        if(value.size()>event.text.size())return false;
        event.bytes=uint16_t(value.size());std::copy(value.begin(),value.end(),event.text.begin());
        return bool(nativeStartup->PostGameText(event));
    }
    void SyncTextContext(){
        const auto& e=startupObservation.game_boot.content.scene_ui.editor;
        const auto context=e.live&&e.source_state!=3?e.context:0;
        if(textGate.Id()==context)return;
        CancelIme();highSurrogate=0;
        const bool focused=options.softkeyWindowTest||GetForegroundWindow()==window;
        textGate.Context(context,focused);
        if(context)PostText(focused?dokodemo::recovered::GameTextKind::FocusGained:dokodemo::recovered::GameTextKind::FocusLost);
    }
    void TextFocus(bool focused){
        if(!focused)CancelIme();
        textGate.Focus(focused);highSurrogate=0;
        if(textGate.Id())PostText(focused?dokodemo::recovered::GameTextKind::FocusGained:dokodemo::recovered::GameTextKind::FocusLost);
    }
    void CancelIme(){
        const bool pending=imeComposing;imeComposing=false;imeContext=0;
        if(pending){const auto context=ImmGetContext(window);if(context){ImmNotifyIME(context,NI_COMPOSITIONSTR,CPS_CANCEL,0);ImmReleaseContext(window,context);}}
    }
    bool CommitUnicode(std::wstring_view value,uint64_t context=0){
        std::string encoded,error;
        if(!dokodemo::EncodeGameText932(value,encoded,error)){
            PostText(dokodemo::recovered::GameTextKind::InvalidEncoding,context);return false;
        }
        return PostText(dokodemo::recovered::GameTextKind::Commit,context,encoded);
    }
    void Character(wchar_t value,LPARAM info){
        if(!textGate.Id()||imeComposing)return;
        const auto key=MapVirtualKeyW(uint32_t((uint64_t(info)>>16)&255),MAPVK_VSC_TO_VK);
        if(!textGate.Character(key)||value<32||value==127)return;
        if(value>=0xD800&&value<=0xDBFF){highSurrogate=value;return;}
        std::wstring text;
        if(highSurrogate){text.push_back(highSurrogate);highSurrogate=0;}
        text.push_back(value);CommitUnicode(text);
    }
    void ImeStart(){
        if(!textGate.Id())return;
        imeComposing=true;imeContext=textGate.Id();highSurrogate=0;
        PostText(dokodemo::recovered::GameTextKind::CompositionBegin,imeContext);
    }
    bool ImeComposition(LPARAM flags){
        if(!(flags&GCS_RESULTSTR))return false;
        if(!imeComposing||imeContext!=textGate.Id())return true; //discard retired/unowned IME result
        const auto context=ImmGetContext(window);if(!context)return false;
        const auto bytes=ImmGetCompositionStringW(context,GCS_RESULTSTR,nullptr,0);
        if(bytes<0||bytes>512||(bytes&1)){
            ImmReleaseContext(window,context);PostText(dokodemo::recovered::GameTextKind::InvalidEncoding,imeContext);return true;
        }
        std::wstring value(size_t(bytes)/2,L'\0');
        const auto read=ImmGetCompositionStringW(context,GCS_RESULTSTR,value.data(),DWORD(bytes));
        ImmReleaseContext(window,context);
        if(read!=bytes)PostText(dokodemo::recovered::GameTextKind::InvalidEncoding,imeContext);
        else CommitUnicode(value,imeContext);
        return true; //result handled;DefWindowProc must not emit duplicate WM_CHAR
    }
    void ImeEnd(){
        if(imeContext&&imeContext==textGate.Id())PostText(dokodemo::recovered::GameTextKind::CompositionEnd,imeContext);
        imeComposing=false;imeContext=0;textGate.Focus(GetForegroundWindow()==window);
    }
    void Key(WPARAM key,LPARAM info=0) {
        ++inputEvents;
        if(textGate.Id()){
            using dokodemo::recovered::GameTextKind;
            const bool repeat=(uint64_t(info)&(1ull<<30))!=0;
            if(!textGate.KeyDown(uint32_t(key),repeat))return;
            if(imeComposing)return; //Enter finishes host composition only
            switch(key){
                case VK_RETURN:if(!repeat)PostText(GameTextKind::Confirm);break;
                case VK_ESCAPE:if(!repeat)PostText(GameTextKind::Cancel);break;
                case VK_BACK:PostText(GameTextKind::Backspace);break;
                case VK_DELETE:PostText(GameTextKind::Delete);break;
                case VK_LEFT:PostText(GameTextKind::Left);break;
                case VK_RIGHT:PostText(GameTextKind::Right);break;
                case VK_HOME:PostText(GameTextKind::Home);break;
                case VK_END:PostText(GameTextKind::End);break;
                case VK_F12:Capture();break;
            }
            return;
        }
        //Game controls are sampled through the synchronized worker mailbox.
        //They must not simultaneously browse assets or run isolated probes.
        if(nativeStartup&&!options.smoke){
            if(key==VK_F12)Capture();else if(key==VK_ESCAPE)RequestWindowClose();
            InvalidateRect(window,nullptr,FALSE);return;
        }
        if ((key == VK_RIGHT || key == VK_LEFT) && !images.empty()) {
            selected = key == VK_RIGHT ? (selected+1)%images.size() : (selected+images.size()-1)%images.size(); LoadSelected();
        } else if (key == VK_F1) RunScript();
        else if (key == VK_F2) MessageFixture();
        else if (key == VK_F3) Audio();
        else if (key == VK_F4) StartupProbe();
        else if (key == VK_F5) Save();
        else if (key == VK_F6) Service47Probe();
        else if (key == VK_F7) ManagerProbe();
        else if (key == VK_F8) StorageProbe();
        else if (key == VK_F9) Load();
        else if (key == VK_F10) ClockStorageProbe();
        else if (key == VK_F12) Capture();
        else if (key == VK_ESCAPE) RequestWindowClose();
        InvalidateRect(window,nullptr,FALSE);
    }
    void SoftkeyTestTick(const dokodemo::recovered::GameContentSnapshot& c){
        using namespace dokodemo::recovered;
        const auto& title=c.title;const auto& e=c.scene_ui.editor;
        const auto save=[&](const wchar_t* name){
            WriteStartupObservation(options.evidence/(std::wstring(name)+L".json"),startupObservation);
            Capture();std::filesystem::copy_file(options.evidence/L"native-window.bmp",options.evidence/(std::wstring(name)+L".bmp"),std::filesystem::copy_options::overwrite_existing);
        };
        const auto button=[&](uint32_t value,bool focused=true){(void)nativeStartup->InjectTestControllerInput(value,focused);};
        // A reached unsupported consumer is a failed changed session. Preserve
        // its first live state and use normal joined teardown immediately;
        // waiting for the global timeout supplies no additional evidence.
        if((options.post19WindowFixture||options.inventoryActionWindowFixture)&&c.actor_script.started&&c.actor_script.blocked){
            if(!post19FirstUnsupportedSaved){save(options.post28WindowFixture?L"post28-first-unsupported":options.post26WindowFixture?L"post26-first-unsupported":options.post25WindowFixture?L"post25-first-unsupported":L"post19-first-unsupported");post19FirstUnsupportedSaved=true;}
            storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
        }
        if((options.post28WindowFixture||options.inventoryActionWindowFixture)&&c.actor_script.started&&c.blocked&&!c.status){
            save(L"post28-first-content-failure");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
        }
        // Bounded exploratory input, through the same real UI and owning route.
        // Completion means only a healthy observation cut, never a new feature.
        if(options.gameplayWindowFixture&&c.post_modal.event_posted){
            const auto& a=c.actor_script;const auto& r=c.returned_interaction;const auto phase=a.runtime.actor_phases;
            const auto cut=[&](const wchar_t* label){
                const auto name=std::wstring(L"explore-")+std::to_wstring(++gameplayCuts)+L"-"+label;
                if(!options.continuationWindowFixture||std::wstring_view(label)==L"choice-target"||std::wstring_view(label)==L"available"||std::wstring_view(label)==L"input-typed"
                   ||std::wstring_view(label)==L"greeting-prompt"||std::wstring_view(label)==L"greeting-word-observation")save(name.c_str());
                else WriteStartupObservation(options.evidence/(name+L".json"),startupObservation);
            };
            const auto fail=[&](const wchar_t* label){save(label);storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);};
            if(gameplayPassed){storageBoundaryPassed=true;return;}
            const std::string corrected_word="\x82\xBD\x82\xEB\x82\xA4";
            const auto& row=a.word_consumer_row;
            const std::array<uint8_t,32> corrected_sha{0x93,0x64,0x0C,0x51,0x66,0x97,0x74,0x29,0x9F,0xD6,0x15,0xB7,0xAD,0xD0,0x47,0x5C,0xDF,0x85,0xD7,0x8B,0x27,0xAF,0xE2,0x32,0xFF,0x82,0x53,0x65,0x25,0x79,0x30,0x08};
            if(row.valid&&row.generation==a.generation&&row.run_id==a.vm_current.run_id&&row.code_sha256==a.vm_current.code_sha256
               &&row.fields[1].kind==dokodemo::script::Value::Kind::String&&row.fields[2].kind==dokodemo::script::Value::Kind::String
               &&row.fields[1].cp932_bytes==6&&row.fields[2].cp932_bytes==6
               &&row.fields[1].cp932_sha256==corrected_sha&&row.fields[2].cp932_sha256==corrected_sha
               &&row.fields[3].kind==dokodemo::script::Value::Kind::Scalar&&row.fields[3].word==0x20000003u
               &&row.fields[18].kind==dokodemo::script::Value::Kind::Scalar&&row.fields[18].word==0x2000BD82u&&!gameplayWordRow){
                gameplayWordRow=true;cut(L"corrected-word-row");
            }
            if(a.awaiting_message&&c.actor_message_glyphs&&a.message.find(corrected_word)!=std::string::npos&&!gameplayEcho){
                gameplayEcho=true;cut(L"corrected-word-font");
            }
            if(!fixtureGreetingObserved&&fixtureGreetingAnswers&&a.word_accepted_return.valid&&a.awaiting_message&&c.actor_message_glyphs
               &&a.message.find("\x82\xA8\x82\xC1\x82\xB7")!=std::string::npos){
                fixtureGreetingObserved=true;cut(L"greeting-word-observation"); // Evidence cut; not an acceptance predicate.
            }
            const auto& cleared=a.word_clear_row;
            const bool cleared_proof=cleared.valid&&cleared.generation==a.generation&&cleared.run_id==a.vm_current.run_id
                &&cleared.code_sha256==a.vm_current.code_sha256&&cleared.record_header_word==0x90000013u&&a.word_clear_writes==19
                &&cleared.row==row.row&&cleared.table==row.table&&cleared.index==row.index&&cleared.global_header_word==row.global_header_word
                &&std::all_of(cleared.fields.begin(),cleared.fields.end(),[](const auto& field){return field.kind==dokodemo::script::Value::Kind::Scalar&&field.word==0;});
            if(r.revoked){fail(L"owner-revoked");return;}

            if(options.notebookWindowFixture&&(noteStep||r.eligible)){
                const auto& menu=c.main_menu;const auto& note=c.notebook;
                const auto json=[&](const wchar_t* label){WriteStartupObservation(options.evidence/(std::wstring(label)+L".json"),startupObservation);};
                if(noteStep&&(r.owner!=noteOwner||r.generation!=noteAvailableGen||r.activation.id!=noteActivation||r.posted||r.activation.consumed)){fail(L"note-borrowed-owner-changed");return;}
                switch(noteStep){
                case 0:
                    if(!c.main_menu_available){fail(L"note-entry-availability");return;}
                    noteOwner=r.owner;noteAvailableGen=r.generation;noteActivation=r.activation.id;
                    noteCallbacks=a.callbacks_returned;noteAcks=a.message_acknowledgements;noteScriptRequests=a.inventory_requests;
                    json(L"note-available-before");button(0);noteWait=phase+3;noteStep=1;break;
                case 1:if(phase>=noteWait){button(Right);noteStep=2;}break;
                case 2:if(menu.state78==1&&menu.mode!=1){button(0);noteWait=phase+3;noteStep=3;}break;
                case 3:if(phase>=noteWait){
                    if(menu.row==3){save(L"note-main-row3");button(0);noteWait=phase+3;noteStep=5;}
                    else{if(++noteNav>12){fail(L"note-row-navigation");return;}noteRow=menu.row;button(menu.row<3?Down:Up);noteStep=4;}
                }break;
                case 4:if(menu.row!=noteRow){button(0);noteWait=phase+3;noteStep=3;}break;
                case 5:if(phase>=noteWait){button(Circle);noteStep=6;}break;
                case 6:if(menu.substate7C==1&&menu.mode!=1){
                    if(menu.column!=0){fail(L"note-secondary-default");return;}
                    save(L"note-secondary0-offered");button(0);noteWait=phase+3;noteStep=7;
                }break;
                case 7:if(phase>=noteWait){button(Circle);noteStep=8;}break;
                case 8:if(menu.state78==2){json(L"note-menu-closing");button(Cross);noteStep=9;}break;
                case 9:if(c.main_menu_child_ready&&note.phase==3){
                    if(menu.result!=3||c.main_flow.menu_result50!=3||!note.request9||!note.owner46C||note.rows.populated||!note.rows.valid
                       ||!a.menu_notebook_live||a.context_enabled||!a.menu_pause_return.returned||a.menu_pause_return.enabled_after
                       ||a.menu_pause_return.receipt<=a.menu_close_return.receipt||a.menu_close_return.receipt<=a.menu_open_return.receipt
                       ||a.callbacks_returned!=noteCallbacks+3||a.inventory_requests!=noteScriptRequests){fail(L"note-open-owner-chain");return;}
                    if(!note.draw_frames||!note.quads||note.font_resets!=note.draw_frames||note.font_flushes!=note.draw_frames
                       ||!note.text_owner||note.text_capacity!=1024||note.text_used||note.text_setup_stage!=5
                       ||note.text_scale_x!=1||note.text_scale_y!=1||note.text_flush_selector!=-1
                       ||note.callback_reaches!=note.callback_empty_returns){fail(L"note-empty-render-consumer");return;}
                    save(L"note-open");noteExecuted=a.instructions;noteWait=phase+4;noteStep=10;
                }break;
                case 10:if(phase>=noteWait){
                    if(note.phase!=3||note.close_requests||a.instructions!=noteExecuted){fail(L"note-held-opening-cross");return;}
                    noteHeld=true;noteArmSequence=note.last_sequence;button(0);noteWait=phase+3;noteStep=110;
                }break;
                case 110:if(phase>=noteWait){
                    if(note.phase!=3||note.last_sequence<=noteArmSequence){fail(L"note-focused-release-not-observed");return;}
                    json(L"note-focused-release");button(0,false);noteWait=phase+2;noteStep=11;
                }break;
                case 11:if(phase>=noteWait){button(Cross,false);noteWait=phase+2;noteStep=12;}break;
                case 12:if(phase>=noteWait){button(Cross,true);noteWait=phase+3;noteStep=13;}break;
                case 13:if(phase>=noteWait){
                    if(note.phase!=3||note.close_requests||a.instructions!=noteExecuted){fail(L"note-refocus-held-cross");return;}
                    noteFocus=true;json(L"note-input-guards");button(0);noteWait=phase+3;noteStep=14;
                }break;
                case 14:if(phase>=noteWait){
                    if(options.noteSelfWindowFixture){
                        if(note.mode244||!a.notebook_self_before.valid){fail(L"note-self-current-record-owner-cut");return;}
                        save(L"note-self-offered");noteSelfEpoch=note.input_epoch;button(Triangle);noteStep=220;
                    }else if(options.notePreviewWindowFixture){
                        if(note.mode244||!c.notebook_observed.slot0.valid){fail(L"note-preview-current-index0-probe-cut");return;}
                        save(L"note-preview-offered");flow5Events=note.unsupported;button(Triangle);noteWait=phase+3;noteStep=200;
                    }else{button(Cross);noteStep=15;}
                }break;
                case 200:if(phase>=noteWait){
                    if(note.unsupported!=flow5Events+1||note.phase!=3||note.close_requests){fail(L"note-preview-refusal-route");return;}
                    if(note.error.empty()){fail(L"note-preview-missing-literal-refusal");return;}
                    save(L"note-preview-refused");button(0);noteWait=phase+3;noteStep=201;
                }break;
                case 201:if(phase>=noteWait){button(Cross);noteStep=15;}break;
                case 220:if(note.preview_opens==1&&note.preview_draws>=3){
                    if(note.phase!=3||note.state23C!=1||note.state26C!=3||note.ordinary270!=1||note.close_requests
                       ||note.preview_generation!=1||note.producer_returns!=1||note.rng_calls!=1||!note.preview_glyphs
                       ||!(note.preview_callback_mask&1)||note.input_epoch<=noteSelfEpoch
                       ||!note.preview_record.valid||note.preview_record.sha!=a.notebook_self_before.sha
                       ||a.context_enabled||a.callback_depth||a.queued_callbacks||a.instructions!=noteExecuted){fail(L"note-self-open-consumers");return;}
                    save(L"note-self-open");noteSelfEpoch=note.input_epoch;noteSelfDraws=note.preview_draws;
                    button(Cross);noteWait=phase+4;noteStep=221;
                }else if(note.unsupported){fail(L"note-self-explicit-consumer-refusal");return;}break;
                case 221:if(phase>=noteWait){
                    if(note.state26C!=3||note.preview_back_requests||note.close_requests){fail(L"note-self-opening-held-cross");return;}
                    noteArmSequence=note.last_sequence;button(0);noteWait=phase+3;noteStep=222;
                }break;
                case 222:if(phase>=noteWait){
                    if(note.last_sequence<=noteArmSequence){fail(L"note-self-neutral-not-observed");return;}
                    button(0,false);noteWait=phase+2;noteStep=223;
                }break;
                case 223:if(phase>=noteWait){button(Cross,false);noteWait=phase+2;noteStep=224;}break;
                case 224:if(phase>=noteWait){button(Cross,true);noteWait=phase+3;noteStep=225;}break;
                case 225:if(phase>=noteWait){
                    if(note.state26C!=3||note.preview_back_requests||note.close_requests||note.preview_draws<=noteSelfDraws){fail(L"note-self-focus-held-cross");return;}
                    save(L"note-self-input-guards");button(0);noteWait=phase+3;noteStep=226;
                }break;
                case 226:if(phase>=noteWait){button(Cross);noteStep=227;}break;
                case 227:if(note.state26C==1&&note.ordinary270==3){
                    if(note.preview_back_requests!=1||note.preview_returns!=1||note.input_epoch<=noteSelfEpoch
                       ||note.phase!=3||note.state23C!=1||note.close_requests||!note.live||!note.request9||!note.owner46C
                       ||a.context_enabled||a.instructions!=noteExecuted||note.producer_returns!=1||note.rng_calls!=1){fail(L"note-self-back-parent-owner");return;}
                    save(L"note-self-parent-back");noteWait=phase+4;noteStep=228;
                }break;
                case 228:if(phase>=noteWait){
                    if(note.close_requests||note.phase!=3){fail(L"note-self-back-held-cross-migrated");return;}
                    button(0);noteWait=phase+3;noteStep=229;
                }break;
                case 229:if(phase>=noteWait){button(Cross);noteStep=15;}break;
                case 15:if(note.phase==4){save(L"note-closing");button(0);noteStep=16;}break;
                case 16:if(!c.main_menu_phase&&c.main_menu_notebook_completions==1){
                    if(!noteHeld||!noteFocus||note.phase!=7||note.live||note.request9||note.owner46C||note.retirement_pending
                       ||note.retired_generation!=note.generation||note.polls!=1||note.retirements!=1||note.opens!=1||note.close_requests!=1
                       ||note.close_completions!=1||!a.context_enabled||a.menu_notebook_live||a.inventory_requests!=noteScriptRequests
                       ||!a.notebook_after.valid||a.notebook_after.sha!=c.notebook_observed.sha
                       ||a.message_acknowledgements!=noteAcks||c.source_loader469||c.source469_resource!=9){fail(L"note-currentgen-drain-return");return;}
                    if(options.noteSelfWindowFixture&&(!a.notebook_self_after.valid||a.notebook_self_after.sha!=a.notebook_self_before.sha
                       ||a.notebook_self_after.sha!=note.preview_record.sha||note.preview_opens!=1||note.preview_returns!=1
                       ||note.producer_returns!=1||note.rng_calls!=1||!note.preview_glyphs)){fail(L"note-self-final-image-drain");return;}
                    json(L"note-drained");noteWait=phase+5;noteStep=17;
                }break;
                case 17:if(phase>=noteWait){
                    if(c.source_loader469||c.source469_resource!=9||a.instructions<=noteExecuted||r.posted||r.activation.consumed){fail(L"note-current-loader-resume");return;}
                    save(L"note-returned-room");notePassed=true;button(0);
                    if(options.notePreviewWindowFixture){
                        const auto& before=c.notebook_observed.slot0;const auto& after=a.notebook_after.slot0;
                        if(!before.valid||!after.valid||before.sha!=after.sha||before.row_index!=after.row_index||before.row_header!=after.row_header
                           ||a.notebook_slot0_stores_dropped){fail(L"note-preview-refusal-changed-record");return;}
                        fail(L"note-preview-source-producer2C91C-cut");noteStep=202;return;
                    }
                    if(options.flow5WindowFixture){
                        if(c.main_flow.phase1C!=5||c.main_flow.substate24!=4||c.main_flow.return20||a.awaiting_message||a.awaiting_choice||a.awaiting_input
                           ||a.source_main_input_suppressed48||!a.context_enabled||a.vm_current.event_envelope_present||a.queued_callbacks
                           ||!a.flow5_guards_valid||a.owner_event_callback_word!=0x7008DA28u){fail(L"flow5-current-source-admission-cut");return;}
                        flow5Attempts=a.flow5_event.attempts;flow5Callbacks=a.callbacks_returned;flow5Events=c.main_flow.events;
                        json(L"flow5-before-input");noteWait=phase+3;noteStep=18;
                    }else{
                        if(options.noteSelfWindowFixture)noteSelfPassed=true;
                        mainMenuFixturePassed=gameplayPassed=storageBoundaryPassed=true;mainMenuFixtureReturns=1;
                    }
                }break;
                case 18:if(phase>=noteWait){
                    if(a.awaiting_message||a.awaiting_choice||a.awaiting_input){fail(L"flow5-source-became-busy");return;}
                    button(Circle);noteWait=phase+3;noteStep=19;
                }break;
                case 19:if(phase>=noteWait){
                    const auto& flow_event=a.flow5_event;
                    if(flow_event.attempts!=flow5Attempts+1||!flow_event.accepted||flow_event.queue_result||!flow_event.receipt||c.main_flow.events!=flow5Events+1
                       ||c.main_flow.last_event!=12||c.main_flow.last_event_argument!=5){fail(L"flow5-existing-scheduler-post");return;}
                    if(flow_event.returned){
                        if(!flow_event.restored_exact||flow_event.queued||flow_event.unobserved||a.callbacks_returned!=flow5Callbacks+1){fail(L"flow5-callback-restoration");return;}
                        save(L"flow5-callback-return");button(0);noteWait=phase+5;noteStep=20;
                    }
                }break;
                case 20:if(phase>=noteWait){
                    save(L"flow5-reached-continuation");
                    // This first run measures the reached consumer. No quiet
                    // callback or new FONT/choice is promoted to feature PASS.
                    fail(L"flow5-observed-consumer-cut");noteStep=21;
                }break;
                }return;
            }
            if(options.menuInventoryWindowFixture&&(miStep||r.eligible)){
                const auto& menu=c.main_menu;const auto& inv=c.inventory;
                if(miStep&&(r.owner!=miOwner||r.generation!=miGeneration||r.activation.id!=miActivation||r.posted||r.activation.consumed)){
                    fail(L"mi-borrowed-activation-changed");return;
                }
                const auto json=[&](const wchar_t* name){WriteStartupObservation(options.evidence/(std::wstring(name)+L".json"),startupObservation);};
                switch(miStep){
                case 0:
                    if(!c.main_menu_available||inv.phase!=7||inv.retired_generation!=inv.generation){fail(L"mi-entry-owner");return;}
                    miOwner=r.owner;miGeneration=r.generation;miActivation=r.activation.id;miCallbacks=a.callbacks_returned;
                    miScriptRequests=a.inventory_requests;miScriptCompletions=a.inventory_completions;miRetirements=inv.retirements;
                    miInventoryGeneration=inv.generation;miInventoryRequests=inv.requests;miAcks=a.message_acknowledgements;
                    json(L"mi-available-before");button(0);miWait=phase+3;miStep=1;break;
                case 1:if(phase>=miWait){button(Right);miStep=2;}break;
                case 2:
                    if(menu.state78==1&&a.menu_open_return.returned&&a.menu_open_return.menu_generation==menu.generation){
                        if(a.callbacks_returned!=miCallbacks+1){fail(L"mi-open-callback");return;}
                        json(L"mi-menu-open");button(0);miWait=phase+3;miStep=3;
                    }break;
                case 3:
                    if(phase>=miWait){
                        if(menu.state78!=1){fail(L"mi-navigation-owner");return;}
                        if(menu.row==5){save(L"mi-menu-row5");button(0);miWait=phase+3;miStep=5;}
                        else{if(++miNav>20){fail(L"mi-navigation-bound");return;}miRow=menu.row;button(menu.row<5?Down:Up);miStep=4;}
                    }break;
                case 4:
                    if(menu.row!=miRow){button(0);miWait=phase+3;miStep=3;}break;
                case 5:if(phase>=miWait){button(Circle);miStep=6;}break;
                case 6:
                    if(menu.state78==2){
                        if(menu.row!=5||menu.result!=-1||a.callbacks_returned!=miCallbacks+1){fail(L"mi-premature-result");return;}
                        json(L"mi-menu-closing");button(0);miStep=7;
                    }break;
                case 7:
                    if(c.main_menu_child_ready&&inv.phase==3){
                        if(menu.result!=5||menu.state78!=3||c.main_flow.menu_result50!=5||c.main_flow.phase1C||c.main_flow.menu58
                           ||unsigned(inv.origin)!=1||inv.generation!=miInventoryGeneration+1||inv.loader498
                           ||c.main_menu_inventory_request_phase!=1||c.main_menu_inventory_request498||!a.menu_inventory_live
                           ||!a.menu_pause_return.returned||a.menu_pause_return.event!=18||a.menu_pause_return.enabled_after||a.context_enabled
                           ||a.callbacks_returned!=miCallbacks+3||a.menu_close_return.receipt!=a.menu_open_return.receipt+1
                           ||a.menu_pause_return.receipt!=a.menu_close_return.receipt+1||a.inventory_requests!=miScriptRequests
                           ||c.consumed_script_inventory_requests!=miScriptRequests||!a.menu_inventory_before.valid
                           ||a.menu_inventory_before.rows!=170||!c.post_modal.spent_history||!c.source_loader469){fail(L"mi-child-source-chain");return;}
                        if(!(c.main_menu_inventory_request_clock<a.menu_close_return.suspended.clock_tick
                             &&a.menu_close_return.restored.clock_tick<=a.menu_pause_return.suspended.clock_tick
                             &&a.menu_pause_return.restored.clock_tick<c.main_menu_inventory_child_clock)){fail(L"mi-owner-phase-order");return;}
                        save(L"mi-inventory-open");miExecuted=a.instructions;
                        if(options.menuRoomWindowFixture&&mrStage==1){
                            const auto& model=a.runtime.room_models[size_t(mpSlot)];
                            if(menu.generation<=mrMenuGeneration||inv.generation<=mrGuiGeneration||!inv.placement_enabled
                               ||a.menu_inventory_before.sha256!=mrPlacedImage||a.runtime.owner_epoch!=mrEpoch
                               ||!model.active||model.retirement||model.generation!=mpGeneration||model.resource!=mpId
                               ||model.asset_identity!=mrAsset||model.clone_identity!=mrClone
                               ||size_t(mpRow)>=inv.items.size()||inv.items[size_t(mpRow)].source_words[1]!=(0x20000000u|uint32_t(mpSlot))){fail(L"mr-rebind-publication");return;}
                            mpActions=inv.action_completions;json(L"mr-new-child-same-publication");button(0);miWait=phase+3;miStep=44;
                        }else if(options.menuPlaceWindowFixture){
                            if(!inv.placement_enabled||inv.selected_row<0||size_t(inv.selected_row)>=inv.items.size()){fail(L"mp-no-offered-placement");return;}
                            const auto& item=inv.items[size_t(inv.selected_row)];
                            if(!item.valid||item.source_kinds[1]!=1||item.source_words[1]!=0){fail(L"mp-offered-item-not-NIL6");return;}
                            mpRow=inv.selected_row;mpId=item.id;mpActions=inv.action_completions;
                            button(Cross);miWait=phase+4;miStep=8;
                        }else if(options.menuMarkWindowFixture){
                            if(inv.selected_row<0||size_t(inv.selected_row)>=inv.items.size()){fail(L"mm-no-offered-item");return;}
                            const auto& item=inv.items[size_t(inv.selected_row)];
                            if(!item.valid||item.source_kinds[1]!=1||item.source_words[1]!=0){fail(L"mm-offered-item-not-NIL6");return;}
                            mmRow=inv.selected_row;mmId=item.id;mmInitialFlag=item.source_words[0];mmActions=inv.action_completions;
                            button(0);miWait=phase+3;miStep=20;
                        }else{button(Cross);miWait=phase+4;miStep=8;}
                    }break;
                case 8:
                    if(phase>=miWait){
                        if(inv.phase!=3||inv.close_requests||a.instructions!=miExecuted){fail(L"mi-inherited-cross");return;}
                        miHeldRefused=true;json(L"mi-held-cross-refused");button(0,false);miWait=phase+2;miStep=9;
                    }break;
                case 9:if(phase>=miWait){button(Cross,false);miWait=phase+2;miStep=10;}break;
                case 10:if(phase>=miWait){button(Cross,true);miWait=phase+3;miStep=11;}break;
                case 11:
                    if(phase>=miWait){
                        if(inv.phase!=3||inv.close_requests||a.instructions!=miExecuted){fail(L"mi-refocused-held-cross");return;}
                        miFocusRefused=true;json(L"mi-focus-cross-refused");button(0);miWait=phase+3;miStep=12;
                    }break;
                case 12:if(phase>=miWait){if(options.menuPlaceWindowFixture&&!mpInputGuardsDone){mpInputGuardsDone=true;button(0);miWait=phase+3;miStep=40;}else{button(Cross);miStep=13;}}break;
                case 13:
                    if(inv.phase==4){save(L"mi-inventory-closing");button(0);miStep=14;}break;
                case 14:
                    if(!c.main_menu_phase&&r.eligible&&c.main_menu_inventory_completions==(options.menuRoomWindowFixture?2u:1u)){
                        const auto& before=a.menu_inventory_before;const auto& after=a.menu_inventory_after;
                        const auto callbacks=miCallbacks+(options.menuRoomWindowFixture?4u:(options.menuMarkWindowFixture||options.menuPlaceWindowFixture)?5u:3u);
                        const bool validRAM=(options.menuMarkWindowFixture||options.menuPlaceWindowFixture)
                            ?((options.menuPlaceWindowFixture?mpPassed:mmMarkPassed)&&a.menu_mark.completions==(options.menuRoomWindowFixture?1u:2u)&&a.menu_mark.verified_sha256==after.sha256)
                            :before.sha256==after.sha256;
                        if(!miHeldRefused||!miFocusRefused||inv.phase!=7||inv.live||inv.owner480||inv.request14||inv.retirement_pending
                           ||inv.retired_generation!=inv.generation||inv.generation!=miInventoryGeneration+1||inv.retirements!=miRetirements+1
                           ||inv.requests!=miInventoryRequests+1||!inv.close_completions||!inv.completed_polls||!inv.retirement_requests
                           ||!before.valid||!after.valid||before.generation!=after.generation||before.run_id!=after.run_id
                           ||before.code_sha256!=after.code_sha256||before.table!=after.table||before.index!=after.index
                           ||before.header!=after.header||before.rows!=after.rows||before.cells!=after.cells||!validRAM
                           ||a.menu_inventory_live||a.menu_inventory_completions!=(options.menuRoomWindowFixture?2u:1u)||!a.context_enabled
                           ||a.inventory_requests!=miScriptRequests||a.inventory_completions!=miScriptCompletions
                           ||a.callbacks_returned!=callbacks||a.instructions!=miExecuted||a.message_acknowledgements!=miAcks
                           ||!c.source_loader469||c.main_flow.menu_result50!=5||c.main_menu_returns!=(options.menuRoomWindowFixture?2u:1u)){fail(L"mi-child-retirement-return");return;}
                        json(L"mi-child-retired");button(Circle);miWait=phase+4;miStep=15;
                    }break;
                case 15:
                    if(phase>=miWait){
                        if(r.posted||r.activation.consumed||c.main_menu_phase||a.callbacks_returned!=miCallbacks+(options.menuRoomWindowFixture?4u:(options.menuMarkWindowFixture||options.menuPlaceWindowFixture)?5u:3u)
                           ||a.message_acknowledgements!=miAcks){fail(L"mi-return-held-circle");return;}
                        miReturnHeldRefused=true;mrOwnershipPassed=options.menuRoomWindowFixture;mrPassed=options.menuRoomWindowFixture&&mrRoomWrites;save(options.menuRoomWindowFixture?L"mr-room-without-item":L"mi-returned");
                        if(options.menuRoomWindowFixture&&!mrPassed){fail(L"mr-selected-model-visibility-unproved");return;}mainMenuFixturePassed=gameplayPassed=storageBoundaryPassed=true;
                        mainMenuFixtureReturns=options.menuRoomWindowFixture?2u:1u;button(0);return;
                    }break;
                case 20:if(phase>=miWait){button(Circle);miStep=21;}break;
                case 21:
                    if(inv.actions){button(0);miWait=phase+3;mmNav=0;miStep=22;}break;
                case 22:
                    if(phase>=miWait){
                        if(!inv.actions||inv.selected_row!=mmRow||inv.selected_id!=mmId){fail(L"mm-action-owner");return;}
                        if(inv.action_selection==(inv.placement_enabled?2u:0u)){save(L"mm-mark-choice");button(0);miWait=phase+3;miStep=24;}
                        else{if(++mmNav>6){fail(L"mm-action-navigation");return;}miRow=int32_t(inv.action_selection);button(Down);miStep=23;}
                    }break;
                case 23:if(int32_t(inv.action_selection)!=miRow){button(0);miWait=phase+3;miStep=22;}break;
                case 24:if(phase>=miWait){button(Circle);miStep=25;}break;
                case 25:
                    if(inv.action_completions==mmActions+1){
                        const auto& action=a.inventory_action;
                        if(inv.actions||inv.action_pending||!action.menu_origin||action.event!=15||!action.completed||!action.returned||action.failed
                           ||int32_t(action.row)!=mmRow||action.item_id!=mmId||action.flag_store_count!=1||action.flag_store_pc!=0x2FDA
                           ||action.before_words[0]!=mmInitialFlag
                           ||action.flag_store_word!=0x10000001||action.after_words[0]!=0x10000001||action.model_service_count
                           ||a.context_enabled||a.menu_mark.completions!=1||a.callbacks_returned!=miCallbacks+4
                           ||inv.items[size_t(mmRow)].category!=2){fail(L"mm-source-mark");return;}
                        mmMarkReceipt=action.receipt;json(L"mm-marked");button(0);miWait=phase+3;mmNav=0;miStep=26;
                    }break;
                case 26:
                    if(phase>=miWait){
                        if(inv.filter==4){if(inv.selected_row!=mmRow||inv.selected_id!=mmId||!inv.count){fail(L"mm-gift-filter-owner");return;}
                            save(L"mm-gift-filter");button(0);miWait=phase+3;miStep=28;}
                        else{if(++mmNav>5){fail(L"mm-filter-navigation");return;}miRow=int32_t(inv.filter);button(Right);miStep=27;}
                    }break;
                case 27:if(int32_t(inv.filter)!=miRow){button(0);miWait=phase+3;miStep=26;}break;
                case 28:if(phase>=miWait){button(Circle);miStep=29;}break;
                case 29:if(inv.actions){button(0);miWait=phase+3;mmNav=0;miStep=30;}break;
                case 30:
                    if(phase>=miWait){
                        if(inv.action_selection==1){json(L"mm-unmark-choice");button(0);miWait=phase+3;miStep=32;}
                        else{if(++mmNav>5){fail(L"mm-unmark-navigation");return;}miRow=int32_t(inv.action_selection);button(Down);miStep=31;}
                    }break;
                case 31:if(int32_t(inv.action_selection)!=miRow){button(0);miWait=phase+3;miStep=30;}break;
                case 32:if(phase>=miWait){button(Circle);miStep=33;}break;
                case 33:
                    if(inv.action_completions==mmActions+2){
                        const auto& action=a.inventory_action;
                        if(inv.action_pending||action.event!=14||!action.menu_origin||!action.completed||!action.returned||action.failed
                           ||int32_t(action.row)!=mmRow||action.item_id!=mmId||action.receipt<=mmMarkReceipt
                           ||action.flag_store_count!=1||action.flag_store_pc!=0x2FDA||action.flag_store_word!=0x10000000
                           ||action.before_words[0]!=0x10000001||action.after_words[0]!=0x10000000||action.model_service_count
                           ||inv.filter!=4||inv.count||inv.selected_row!=-1||a.context_enabled||a.menu_mark.completions!=2
                           ||a.callbacks_returned!=miCallbacks+5){fail(L"mm-source-unmark");return;}
                        save(L"mm-unmarked-empty-gift");mmMarkPassed=true;miExecuted=a.instructions;
                        button(Cross);miWait=phase+4;miStep=8;
                    }break;
                case 40:if(phase>=miWait){button(Circle);miStep=41;}break;
                case 41:if(inv.actions){button(0);miWait=phase+3;miStep=42;}break;
                case 42:
                    if(phase>=miWait){
                        if(!inv.actions||!inv.placement_enabled||inv.selected_row!=mpRow||inv.selected_id!=mpId||inv.action_selection!=0){fail(L"mp-decorate-owner");return;}
                        save(L"mp-decorate-offered");button(Circle);miStep=43;
                    }break;
                case 43:
                    if(inv.action_completions==mpActions+1){
                        const auto& action=a.inventory_action;const auto& proof=a.menu_placement;
                        mpSlot=int32_t(action.after_words[1]&0xFFFFFFF);
                        if(!action.placement||!action.menu_origin||!action.completed||action.failed||!action.returned||action.event!=13
                           ||int32_t(action.row)!=mpRow||action.item_id!=mpId||action.before_words[1]!=0||action.after_words[1]>>28!=2
                           ||mpSlot<0||mpSlot>=100||!proof.returned||!proof.flag_returned||!proof.callback_returned||proof.completions!=1
                           ||proof.source_return_pc!=0x38D9||a.menu_mark.completions!=1||a.context_enabled||a.callbacks_returned!=miCallbacks+4
                           ||inv.items[size_t(mpRow)].category!=0){fail(L"mp-source-decorate");return;}
                        const auto& model=a.runtime.room_models[size_t(mpSlot)];
                        if(!model.active||model.retirement||model.resource!=mpId||!model.asset_identity||!model.clone_identity){fail(L"mp-model-publication");return;}
                        mpGeneration=model.generation;mpReceipt=action.receipt;json(L"mp-decorated-source");button(0);mpNav=0;miWait=phase+4;miStep=options.menuRoomWindowFixture?60u:44u;
                        if(options.menuRoomWindowFixture){mrEpoch=a.runtime.owner_epoch;mrAsset=model.asset_identity;mrClone=model.clone_identity;
                            mrMenuGeneration=menu.generation;mrGuiGeneration=inv.generation;mrPlacedImage=a.menu_mark.verified_sha256;
                            mrDrawFrames=model.draw_frames;mrDrawPixels=model.draw_pixel_writes;}
                    }break;
                case 44:
                    if(phase>=miWait){
                        const auto& model=a.runtime.room_models[size_t(mpSlot)];
                        if(!model.active||model.generation!=mpGeneration||!model.draw_frames||(!model.draw_pixel_writes&&!options.menuRoomOwnerDiagnostic)){fail(L"mp-model-presentation");return;}
                        if(inv.filter==2){button(0);mpNav=0;miWait=phase+3;miStep=46;}
                        else{if(++mpNav>5){fail(L"mp-placed-filter-navigation");return;}miRow=int32_t(inv.filter);button(Right);miStep=45;}
                    }break;
                case 45:if(int32_t(inv.filter)!=miRow){button(0);miWait=phase+3;miStep=44;}break;
                case 46:
                    if(phase>=miWait){
                        if(!inv.count){fail(L"mp-placed-filter-empty");return;}
                        if(inv.selected_row==mpRow){if(inv.selected_id!=mpId){fail(L"mp-placed-row-identity");return;}save(L"mp-placed-filter");button(0);miWait=phase+3;miStep=48;}
                        else{if(++mpNav>170){fail(L"mp-placed-row-navigation");return;}miRow=inv.selected_row;button(Down);miStep=47;}
                    }break;
                case 47:if(inv.selected_row!=miRow){button(0);miWait=phase+3;miStep=46;}break;
                case 48:if(phase>=miWait){button(Circle);miStep=49;}break;
                case 49:if(inv.actions){button(0);mpNav=0;miWait=phase+3;miStep=50;}break;
                case 50:
                    if(phase>=miWait){
                        if(!inv.actions||inv.selected_row!=mpRow||inv.selected_id!=mpId){fail(L"mp-putaway-owner");return;}
                        if(inv.action_selection==1){json(L"mp-putaway-offered");button(0);miWait=phase+3;miStep=52;}
                        else{if(++mpNav>5){fail(L"mp-putaway-navigation");return;}miRow=int32_t(inv.action_selection);button(Down);miStep=51;}
                    }break;
                case 51:if(int32_t(inv.action_selection)!=miRow){button(0);miWait=phase+3;miStep=50;}break;
                case 52:if(phase>=miWait){button(Circle);miStep=53;}break;
                case 53:
                    if(inv.action_completions==mpActions+(options.menuRoomWindowFixture?1u:2u)){
                        const auto& action=a.inventory_action;const auto& proof=a.menu_placement;
                        if(!action.placement||!action.menu_origin||!action.completed||!action.returned||action.failed||action.event!=14||action.receipt<=mpReceipt
                           ||int32_t(action.row)!=mpRow||action.item_id!=mpId||action.before_words[1]!=(0x20000000u|uint32_t(mpSlot))
                           ||action.after_words[1]||action.after_words[2]||action.after_words[0]!=0x10000000||action.before_controls[1]!=action.after_controls[1]
                           ||!proof.flag_returned||!proof.callback_returned||proof.completions!=(options.menuRoomWindowFixture?1u:2u)||proof.source_return_pc!=0x393B
                           ||a.menu_mark.completions!=(options.menuRoomWindowFixture?1u:2u)||a.context_enabled||a.callbacks_returned!=miCallbacks+(options.menuRoomWindowFixture?4u:5u)||inv.items[size_t(mpRow)].category!=1){fail(L"mp-source-putaway");return;}
                        json(L"mp-putaway-source");button(0);mpNav=0;miWait=phase+3;miStep=54;
                    }break;
                case 54:
                    if(phase>=miWait){
                        const auto& room=a.runtime;const auto it=std::find_if(room.model_retirement_history.begin(),room.model_retirement_history.end(),[&](const auto& x){return x.slot==mpSlot&&x.generation==mpGeneration;});
                        if(it==room.model_retirement_history.end()||!it->destroyed_phase)break;
                        const auto& model=room.room_models[size_t(mpSlot)];
                        if(!(it->requested_phase<it->disabled_phase&&it->disabled_phase<it->destroyed_phase)||!it->assets_retained_at_disable||!it->clone_retained_at_disable
                           ||!it->asset_owner_expired||!it->clone_owner_cleared||model.active||model.asset_identity||model.clone_identity||model.generation!=mpGeneration){fail(L"mp-natural-model-retirement");return;}
                        if(inv.filter==3){button(0);mpNav=0;miWait=phase+3;miStep=56;}
                        else{if(++mpNav>5){fail(L"mp-stored-filter-navigation");return;}miRow=int32_t(inv.filter);button(Right);miStep=55;}
                    }break;
                case 55:if(int32_t(inv.filter)!=miRow){button(0);miWait=phase+3;miStep=54;}break;
                case 56:
                    if(phase>=miWait){
                        if(!inv.count){fail(L"mp-stored-filter-empty");return;}
                        if(inv.selected_row==mpRow){if(inv.selected_id!=mpId){fail(L"mp-stored-row-identity");return;}save(L"mp-stored-filter");mpPassed=true;miExecuted=a.instructions;button(Cross);miStep=13;}
                        else{if(++mpNav>170){fail(L"mp-stored-row-navigation");return;}miRow=inv.selected_row;button(Down);miStep=57;}
                    }break;
                case 57:if(inv.selected_row!=miRow){button(0);miWait=phase+3;miStep=56;}break;
                case 60:if(phase>=miWait){miExecuted=a.instructions;button(Cross);miStep=61;}break;
                case 61:if(inv.phase==4){save(L"mr-first-child-closing");button(0);miStep=62;}break;
                case 62:
                    if(!c.main_menu_phase&&r.eligible&&c.main_menu_inventory_completions==1){
                        const auto& model=a.runtime.room_models[size_t(mpSlot)];
                        if(inv.phase!=7||inv.live||inv.retired_generation!=mrGuiGeneration||a.menu_inventory_live||!a.context_enabled
                           ||a.menu_inventory_completions!=1||a.menu_inventory_after.sha256!=mrPlacedImage
                           ||a.menu_mark.completions!=1||a.callbacks_returned!=miCallbacks+4||a.instructions!=miExecuted
                           ||a.message_acknowledgements!=miAcks||!c.source_loader469||c.main_menu_returns!=1
                           ||!model.active||model.retirement||model.generation!=mpGeneration||model.resource!=mpId
                           ||a.runtime.owner_epoch!=mrEpoch||model.asset_identity!=mrAsset||model.clone_identity!=mrClone){fail(L"mr-close-preserved-model");return;}
                        // Compare only returned-room draws, not writes accumulated
                        // while the inventory was still open or closing.
                        mrDrawFrames=model.draw_frames;mrDrawPixels=model.draw_pixel_writes;
                        json(L"mr-first-child-drained");button(0);miWait=phase+5;miStep=63;
                    }break;
                case 63:
                    if(phase>=miWait){
                        const auto& model=a.runtime.room_models[size_t(mpSlot)];
                        if(c.main_menu_phase||inv.live||!model.active||model.retirement||model.generation!=mpGeneration
                           ||model.draw_frames<=mrDrawFrames){fail(L"mr-room-model-draw");return;}
                        mrRoomWrites=model.draw_pixel_writes>mrDrawPixels;
                        if(!mrRoomWrites&&!options.menuRoomOwnerDiagnostic){fail(L"mr-room-model-draw");return;}
                        save(L"mr-room-with-item");mrStage=1;miCallbacks=a.callbacks_returned;miInventoryGeneration=inv.generation;
                        miInventoryRequests=inv.requests;miRetirements=inv.retirements;miNav=0;button(0);miWait=phase+3;miStep=1;
                    }break;
                default:fail(L"mi-fixture-step");return;
                }
                return;
            }
            if(!options.menuInventoryWindowFixture&&options.mainMenuWindowFixture&&(mainMenuFixtureStep||r.eligible)){
                const auto& menu=c.main_menu;
                if(mainMenuFixtureStep&& (r.owner!=mainMenuFixtureOwner||r.generation!=mainMenuFixtureGeneration||r.posted!=0)){
                    fail(L"menu-borrowed-owner-changed");return;
                }
                switch(mainMenuFixtureStep){
                case 0:
                    if(!c.main_menu_available){fail(L"menu-availability-domain");return;}
                    mainMenuFixtureOwner=r.owner;mainMenuFixtureGeneration=r.generation;mainMenuFixtureBaseline=a.callbacks_returned;
                    save(L"menu-available-before");button(0);mainMenuFixtureWait=phase+3;mainMenuFixtureStep=1;break;
                case 1:
                    if(phase>=mainMenuFixtureWait){button(Right);mainMenuFixtureStep=2;}break;
                case 2:
                    if(c.main_menu_phase){WriteStartupObservation(options.evidence/(mainMenuFixtureReturns?L"menu-requested-2.json":L"menu-requested-1.json"),startupObservation);button(0);mainMenuFixtureStep=3;}break;
                case 3:
                    if(menu.state78==1&&a.menu_open_return.returned&&a.menu_open_return.menu_generation==menu.generation){
                        if(a.callbacks_returned!=mainMenuFixtureBaseline+1||a.menu_open_return.kind_after!=uint8_t(dokodemo::script::Value::Kind::Scalar)
                           ||a.menu_open_return.flag_after!=0x10000001u||!c.source_loader469||c.main_flow.phase1C!=7){fail(L"menu-open-source-proof");return;}
                        if(mainMenuFixtureReturns)WriteStartupObservation(options.evidence/L"menu-reopened.json",startupObservation);else save(L"menu-open");
                        mainMenuFixtureExecuted=a.instructions;button(0);mainMenuFixtureWait=phase+5;mainMenuFixtureStep=4;
                    }break;
                case 4:
                    if(phase>=mainMenuFixtureWait){
                        if(a.instructions!=mainMenuFixtureExecuted){fail(L"menu-main-vm-not-paused");return;}
                        button(mainMenuFixtureReturns?Left:Cross);mainMenuFixtureStep=5;
                    }break;
                case 5:
                    if(menu.state78==2){
                        if(a.callbacks_returned!=mainMenuFixtureBaseline+1||a.menu_close_return.menu_generation==menu.generation){fail(L"menu-premature-callback11");return;}
                        if(mainMenuFixtureReturns)WriteStartupObservation(options.evidence/L"menu-closing-2.json",startupObservation);else save(L"menu-closing-1");button(0);mainMenuFixtureStep=6;
                    }break;
                case 6:
                    if(!c.main_menu_phase&&r.eligible&&c.main_menu_returns==mainMenuFixtureReturns+1){
                        if(menu.state78!=3||menu.result!=-1||a.callbacks_returned!=mainMenuFixtureBaseline+2
                           ||!a.menu_close_return.returned||a.menu_close_return.menu_generation!=menu.generation
                           ||a.menu_close_return.kind_after!=uint8_t(dokodemo::script::Value::Kind::Scalar)||a.menu_close_return.flag_after!=0x10000000u
                           ||c.main_flow.phase1C||c.main_flow.menu58||!c.source_loader469||!r.activation.id||r.activation.consumed){fail(L"menu-cancel-return-proof");return;}
                        ++mainMenuFixtureReturns;if(mainMenuFixtureReturns==2)WriteStartupObservation(options.evidence/L"menu-returned-2.json",startupObservation);else save(L"menu-returned-1");
                        if(mainMenuFixtureReturns==2){mainMenuFixturePassed=gameplayPassed=storageBoundaryPassed=true;button(0);return;}
                        mainMenuFixtureBaseline=a.callbacks_returned;button(Right);mainMenuFixtureWait=phase+4;mainMenuFixtureStep=7;
                    }break;
                case 7:
                    if(phase>=mainMenuFixtureWait){
                        if(c.main_menu_phase||menu.opens!=1){fail(L"menu-held-right-reopened");return;}
                        button(0);mainMenuFixtureWait=phase+3;mainMenuFixtureStep=1;
                    }break;
                default:fail(L"menu-fixture-state");return;
                }
                return;
            }
            if(!gameplayUi&&r.eligible&&!a.awaiting_message&&!a.awaiting_choice&&!a.awaiting_input){
                cut(L"available");
                if(options.continuationWindowFixture&&gameplayPosts==3&&r.generation==gameplayGeneration+1&&r.posted==gameplayPosted+1
                   &&a.owned_conversation.completed&&a.owned_conversation.dispatch_ready){gameplayPassed=storageBoundaryPassed=true;button(0);return;}
                if(!options.continuationWindowFixture&&gameplayPosts==1&&r.generation==gameplayGeneration+1&&r.posted==gameplayPosted+1&&a.owned_conversation.completed&&a.owned_conversation.dispatch_ready&&a.op55_calls&&a.op55_first_pc==0xDE3&&a.op55_first_before==0x20000012&&a.op55_first_after==0x20000011&&cleared_proof&&gameplayKnownInput&&gameplayCorrectedInput&&gameplayWordRow&&gameplayEcho){gameplayPassed=storageBoundaryPassed=true;button(0);return;}
                if(!options.continuationWindowFixture&&gameplayPosts&&a.op55_calls){fail(L"word-correction-proof-boundary");return;}
                if(gameplayPosts>=4){fail(L"supported-exploration-boundary");return;}
                gameplayGeneration=r.generation;gameplayPosted=r.posted;button(0);gameplayWait=phase+3;gameplayUi=1;
            }else if(gameplayUi==1&&phase>=gameplayWait&&r.armed){
                if(!r.eligible||r.generation!=gameplayGeneration||r.posted!=gameplayPosted){fail(L"owner-changed");return;}
                button(Circle);gameplayWait=phase+300;gameplayUi=2;
            }else if(gameplayUi==2&&r.posted==gameplayPosted+1){
                if(r.generation!=gameplayGeneration||!r.event_posted||r.queue_result!=0){fail(L"post-refused");return;}
                ++gameplayPosts;cut(L"posted");button(0);gameplayUi=0;gameplayWait=phase+300;
            }else if(!gameplayUi&&a.awaiting_message&&c.actor_message_glyphs){
                cut(options.continuationWindowFixture&&a.message.find("\x82\xC8\x82\xA9\x82\xE6\x82\xB5\x93\xAF\x8E\x6D\x82\xCC")!=std::string::npos?L"greeting-prompt":L"message");
                gameplayBaseline=a.message_acknowledgements;button(0);gameplayWait=phase+3;gameplayUi=3;
            }else if(gameplayUi==3&&phase>=gameplayWait&&c.actor_message_input_armed){
                button(Circle);gameplayWait=phase+300;gameplayUi=4;
            }else if(gameplayUi==4&&a.message_acknowledgements==gameplayBaseline+1){
                button(0);gameplayUi=0;gameplayWait=phase+300;
            }else if(!gameplayUi&&a.awaiting_choice&&c.actor_choice.state==1){
                cut(L"choice");gameplayBaseline=a.choice_completions;button(0);gameplayWait=phase+3;gameplayUi=5;
                if(options.continuationWindowFixture){
                    fixtureChoiceOptions=c.actor_choice.options;fixtureChoicePC=a.pc;fixtureChoiceRequest=a.choice_requests;
                    fixtureChoiceContext=c.actor_choice.context;fixtureChoiceOwner=c.actor_choice.owner_generation;
                    if(fixtureChoiceOptions.empty()||fixtureChoiceOptions.size()>32||c.actor_choice.row>=fixtureChoiceOptions.size()
                       ||std::any_of(fixtureChoiceOptions.begin(),fixtureChoiceOptions.end(),[](const auto& text){return text.empty();})){fail(L"fixture-choice-domain");return;}
                    fixtureChoiceTarget=c.actor_choice.row;
                    for(uint32_t i=uint32_t(fixtureChoiceOptions.size());i-->0;)if(!FixtureChoiceSeen(a.pc,fixtureChoiceOptions[i])){fixtureChoiceTarget=i;break;}
                }
            }else if((gameplayUi==5||gameplayUi==9)&&options.continuationWindowFixture){
                if(!a.awaiting_choice||a.pc!=fixtureChoicePC||a.choice_requests!=fixtureChoiceRequest||c.actor_choice.state!=1
                   ||c.actor_choice.context!=fixtureChoiceContext||c.actor_choice.owner_generation!=fixtureChoiceOwner||c.actor_choice.options!=fixtureChoiceOptions){fail(L"fixture-choice-owner-changed");return;}
                if(gameplayUi==9){
                    if(c.actor_choice.row!=fixtureChoicePrevious){button(0);gameplayWait=phase+3;gameplayUi=5;}
                    else if(phase>=gameplayWait){fail(L"fixture-choice-arrow-not-consumed");return;}
                }else if(phase>=gameplayWait){
                    if(c.actor_choice.row!=fixtureChoiceTarget){fixtureChoicePrevious=c.actor_choice.row;button(c.actor_choice.row<fixtureChoiceTarget?Down:Up);gameplayWait=phase+8;gameplayUi=9;}
                    else {cut(L"choice-target");button(Circle);gameplayWait=phase+300;gameplayUi=6;}
                }
            }else if(gameplayUi==5&&phase>=gameplayWait){
                button(Circle);gameplayWait=phase+300;gameplayUi=6;
            }else if(gameplayUi==6&&!a.awaiting_choice&&a.choice_completions==gameplayBaseline+1){
                if(options.continuationWindowFixture){
                    if(c.actor_choice.context!=fixtureChoiceContext||c.actor_choice.owner_generation!=fixtureChoiceOwner||c.actor_choice.state!=3
                       ||c.actor_choice.timed_out||c.actor_choice.row!=fixtureChoiceTarget||c.actor_choice.result!=fixtureChoiceTarget+1
                       ||a.choice_result_word!=(0x20000000u|(fixtureChoiceTarget+1))){fail(L"fixture-choice-result-mismatch");return;}
                    if(!RecordFixtureChoice()){fail(L"fixture-choice-journal-unavailable");return;}
                }
                cut(L"choice-result");button(0);gameplayUi=0;gameplayWait=phase+300;
            }else if(!gameplayUi&&a.awaiting_input&&e.live&&e.source_state==1&&e.focused){
                cut(L"input");if(a.input_mode!=0){fail(L"input-domain");return;}
                // Test text answers an actually rendered source question. This
                // selects no source result/category and creates no input route.
                const bool greeting=options.continuationWindowFixture
                    &&a.message.find("\x82\xC8\x82\xA9\x82\xE6\x82\xB5\x93\xAF\x8E\x6D\x82\xCC")!=std::string::npos;
                fixtureInputExpected=options.continuationWindowFixture?(greeting?"\x82\xA8\x82\xC1\x82\xB7":"\x82\xA0\x82\xAB")
                    :a.input_completions?corrected_word:"\x82\xCB\x82\xB1";
                fixtureInputContext=e.context;fixtureInputGeneration=a.generation;fixtureInputRequest=a.input_requests;
                fixtureInputPC=a.pc;fixtureInputScriptContext=a.context_word;
                if(!fixtureInputContext||textGate.Id()!=fixtureInputContext){fail(L"fixture-input-owner");return;}
                gameplayBaseline=a.input_completions;button(0);
                if(!PostText(GameTextKind::Home,fixtureInputContext)){fail(L"fixture-input-edit-refused");return;}
                for(unsigned i=0;i<8;++i)if(!PostText(GameTextKind::Delete,fixtureInputContext)){fail(L"fixture-input-edit-refused");return;}
                if(!CommitUnicode(options.continuationWindowFixture?(greeting?L"\u304A\u3063\u3059":L"\u3042\u304D")
                    :a.input_completions?L"\u305F\u308D\u3046":L"\u306D\u3053")){fail(L"fixture-input-text-refused");return;}
                gameplayWait=phase+3;gameplayUi=7;
            }else if(gameplayUi==7&&phase>=gameplayWait&&e.staged==fixtureInputExpected){
                if(!a.awaiting_input||a.generation!=fixtureInputGeneration||a.input_requests!=fixtureInputRequest||a.pc!=fixtureInputPC
                   ||a.context_word!=fixtureInputScriptContext||!e.live||e.source_state!=1||!e.focused||e.context!=fixtureInputContext){fail(L"fixture-input-owner-changed");return;}
                cut(L"input-typed");if(!PostText(GameTextKind::Confirm,fixtureInputContext)){fail(L"confirm-refused");return;}
                gameplayWait=phase+300;gameplayUi=8;
            }else if(gameplayUi==8&&!a.awaiting_input&&a.input_completions==gameplayBaseline+1){
                if(a.generation!=fixtureInputGeneration||a.input_requests!=fixtureInputRequest||a.context_word!=fixtureInputScriptContext
                   ||a.input_timed_out||a.input_text!=fixtureInputExpected||a.input_auxiliary!=fixtureInputExpected){fail(L"input-result-mismatch");return;}
                if(fixtureInputExpected=="\x82\xA8\x82\xC1\x82\xB7")++fixtureGreetingAnswers;
                if(!gameplayBaseline)gameplayKnownInput=true;else gameplayCorrectedInput=true;
                cut(L"input-result");button(0);gameplayUi=0;gameplayWait=phase+300;
            }
            if(!gameplayWait)gameplayWait=phase+300;
            if(phase>=gameplayWait+300){fail(L"observation-boundary");return;}
            return;
        }
        //Feature automation uses the existing editor/message/SELECT owners.
        if(options.kanaWindowFixture&&c.post_modal.event_posted){
            const auto& a=c.actor_script;const auto phase=a.runtime.actor_phases;
            const std::string word="\x82\xD0\x82\xB4"; // hiza
            const auto fail=[&](const wchar_t* n){save(n);storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);};
            if(!kanaStep&&a.awaiting_input){kanaInputBefore=a.input_completions;kanaStep=1;kanaUiStep=0;kanaWait=phase+180;}
            if(kanaPassed){storageBoundaryPassed=true;return;}
            if(!kanaStep){
                const auto& r=c.returned_interaction;
                if(r.revoked){fail(L"kana-initial-owner-refused");return;}
                if(!kanaSeekStep&&r.eligible){
                    kanaSeekGeneration=r.generation;kanaSeekPosted=r.posted;++kanaSeekCount;
                    if(kanaSeekCount>8){fail(L"kana-offered-input-observation-limit");return;}
                    save(L"kana-source-ready");button(0);kanaWait=phase+3;kanaSeekStep=1;kanaUiStep=0;
                }else if(kanaSeekStep==1&&phase>=kanaWait&&r.armed){
                    if(!r.eligible||r.generation!=kanaSeekGeneration||r.posted!=kanaSeekPosted){fail(L"kana-seek-owner-changed");return;}
                    button(Circle);kanaWait=phase+300;kanaSeekStep=2;
                }else if(kanaSeekStep==2&&r.posted==kanaSeekPosted+1){
                    if(r.generation!=kanaSeekGeneration||!r.event_posted||r.queue_result!=0){fail(L"kana-seek-post-refused");return;}
                    save(L"kana-source-posted");button(0);kanaSeekStep=0;kanaWait=phase+300;
                }
                if(!kanaSeekStep&&!kanaUiStep&&a.awaiting_message&&c.actor_message_glyphs){
                    kanaAckBefore=a.message_acknowledgements;button(0);kanaWait=phase+3;kanaUiStep=1;
                }else if(kanaUiStep==1&&phase>=kanaWait&&c.actor_message_input_armed){
                    button(Circle);kanaWait=phase+300;kanaUiStep=2;
                }else if(kanaUiStep==2&&a.message_acknowledgements==kanaAckBefore+1){
                    button(0);kanaUiStep=0;kanaWait=phase+300;
                }else if(!kanaSeekStep&&!kanaUiStep&&a.awaiting_choice&&c.actor_choice.state==1){
                    kanaChoiceBefore=a.choice_completions;button(0);kanaWait=phase+3;kanaUiStep=3;
                }else if(kanaUiStep==3&&phase>=kanaWait){
                    button(Circle);kanaWait=phase+300;kanaUiStep=4;
                }else if(kanaUiStep==4&&!a.awaiting_choice&&a.choice_completions==kanaChoiceBefore+1){
                    button(0);kanaUiStep=0;kanaWait=phase+300;
                }
                if(!kanaWait)kanaWait=phase+300;
                if(phase>=kanaWait+300){fail(L"kana-next-offered-consumer-frontier");return;}
                return;
            }
            if(a.awaiting_input&&a.input_mode!=0){fail(L"kana-input-mode-frontier");return;}
            if(kanaStep==1&&e.live&&e.source_state==1&&e.focused){
                save(L"kana-input-open");button(Circle);kanaWait=phase+5;kanaStep=2;
            }else if(kanaStep==2&&phase>=kanaWait){
                kanaHeldRefused=a.awaiting_input&&a.input_completions==kanaInputBefore&&e.staged==a.input_initial;
                save(L"kana-input-held");if(!kanaHeldRefused){fail(L"kana-input-held-refused");return;}
                button(0);PostText(GameTextKind::Home);for(unsigned i=0;i<8;++i)PostText(GameTextKind::Delete);
                CommitUnicode(L"\u3072\u3056");kanaWait=phase+3;kanaStep=3;
            }else if(kanaStep==3&&phase>=kanaWait&&e.staged==word){
                save(L"kana-input-typed");if(!PostText(GameTextKind::Confirm)){fail(L"kana-confirm-refused");return;}
                kanaWait=phase+180;kanaStep=4;
            }else if(kanaStep==4){
                if(e.source_state==2&&!kanaClosing){kanaClosing=true;save(L"kana-input-closing");}
                if(!a.awaiting_input&&a.input_completions==kanaInputBefore+1){
                    if(a.input_text!=word||a.input_auxiliary!=word||a.input_timed_out){fail(L"kana-input-result-refused");return;}
                    save(L"kana-input-result");button(0);kanaWait=phase+300;kanaStep=5;
                }
            }
            if(kanaStep==5){
                const auto& row=a.word_consumer_row;
                const std::array<uint8_t,32> expected_word_sha{0x70,0x8F,0xF6,0xF6,0x68,0x52,0xA8,0xD6,0xCC,0x6D,0xA0,0xD0,0x23,0x3B,0xB8,0x89,0x8E,0xC1,0x72,0x4B,0xC1,0xB2,0xD4,0xAD,0x58,0x97,0x6E,0xD4,0x19,0x38,0x5F,0x8B};
                const bool proof=row.valid&&row.generation==a.generation&&row.run_id==a.vm_current.run_id&&row.code_sha256==a.vm_current.code_sha256
                    &&row.fields[1].kind==dokodemo::script::Value::Kind::String&&row.fields[2].kind==dokodemo::script::Value::Kind::String
                    &&row.fields[1].cp932_bytes==word.size()&&row.fields[2].cp932_bytes==word.size()
                    &&row.fields[1].cp932_sha256==expected_word_sha&&row.fields[2].cp932_sha256==expected_word_sha
                    &&row.fields[3].kind==dokodemo::script::Value::Kind::Scalar&&row.fields[3].word==0x20000002u
                    &&row.fields[18].kind==dokodemo::script::Value::Kind::Scalar&&row.fields[18].word==0x2000D082u
                    &&row.fields[0].kind==dokodemo::script::Value::Kind::Scalar&&row.fields[0].word>>28==2&&(row.fields[0].word&0x5000u)==0x5000u
                    &&a.string_prefix71_result_text=="\x82\xD0"&&a.string_suffix72_result_text=="\x82\xB4"
                    &&a.string_suffix72_input==word&&a.ascii_class70_result_word==0x10000001u;
                if(proof&&!kanaWordPassed){kanaWordPassed=true;save(L"kana-word-row");}
                if(c.returned_interaction.eligible&&!a.awaiting_message&&!a.awaiting_choice&&!a.awaiting_input){
                    if(!kanaWordPassed||!kanaEchoSeen||!kanaHeldRefused||!kanaClosing){fail(L"kana-owner-consumer-frontier");return;}
                    kanaPassed=storageBoundaryPassed=true;save(L"kana-available-idle");button(0);return;
                }
                if(!kanaUiStep&&a.awaiting_message&&c.actor_message_glyphs){
                    if(a.message.find(word)!=std::string::npos){kanaEchoSeen=true;save(L"kana-word-font");}
                    kanaAckBefore=a.message_acknowledgements;button(0);kanaWait=phase+3;kanaUiStep=1;
                }else if(kanaUiStep==1&&phase>=kanaWait&&c.actor_message_input_armed){
                    button(Circle);kanaWait=phase+300;kanaUiStep=2;
                }else if(kanaUiStep==2&&a.message_acknowledgements==kanaAckBefore+1){
                    button(0);kanaUiStep=0;kanaWait=phase+300;
                }else if(!kanaUiStep&&a.awaiting_choice&&c.actor_choice.state==1){
                    save(L"kana-choice-menu");kanaChoiceBefore=a.choice_completions;button(0);kanaWait=phase+3;kanaUiStep=3;
                }else if(kanaUiStep==3&&phase>=kanaWait){
                    button(Circle);kanaWait=phase+300;kanaUiStep=4;
                }else if(kanaUiStep==4&&!a.awaiting_choice&&a.choice_completions==kanaChoiceBefore+1){
                    save(L"kana-choice-result");button(0);kanaUiStep=0;kanaWait=phase+300;
                }
                if(a.awaiting_input){kanaInputBefore=a.input_completions;kanaStep=1;kanaUiStep=0;kanaWait=phase+180;}
            }
            if(phase>=kanaWait+300){fail(L"kana-feature-observation-limit");return;}
            return;
        }
        //A live RNG/clock branch may reach a supported SELECT before the old
        //numbered route. Explore that actual UI with fresh edges; never give
        //it the numbered post28 PASS or inject a script result/old event0.
        if(options.post28WindowFixture&&post28AlternateStep){
            const auto& a=c.actor_script;
            const auto phase=a.runtime.actor_phases;
            if(options.activationWindowFixture){
                const auto& domain=c.returned_interaction.activation;
                if(options.repeatWindowFixture){
                    const auto& r=c.returned_interaction;const auto& own=a.owned_conversation;
                    if(!repeatStep&&r.generation==2&&r.eligible&&own.completed&&own.dispatch_ready
                       &&r.posted==1&&domain.kind==GameConversationProofKind::OwnedMain
                       &&domain.owned_receipt==own.receipt&&domain.origin_activation==own.origin_activation
                       &&domain.id==own.origin_activation+1){
                        save(L"repeat-ready-2");repeatGeneration=r.generation;repeatPostedBase=r.posted;
                        repeatFirstReceipt=own.receipt;repeatActivation=domain.id;button(Circle);repeatWait=phase+5;repeatStep=1;
                    }else if(repeatStep==1&&phase>=repeatWait){
                        if(r.generation!=repeatGeneration||domain.id!=repeatActivation||own.receipt!=repeatFirstReceipt
                           ||r.posted!=repeatPostedBase||r.event_posted||r.armed){save(L"repeat-held-failure");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;}
                        save(L"repeat-held-2");button(0);repeatWait=phase+3;repeatStep=2;
                    }else if(repeatStep==2&&phase>=repeatWait&&r.armed&&r.generation==repeatGeneration
                             &&domain.id==repeatActivation&&own.receipt==repeatFirstReceipt){
                        save(L"repeat-release-2");button(Circle);repeatStep=3;repeatWait=phase+120;
                    }else if(repeatStep==3&&r.event_posted&&r.posted==repeatPostedBase+1){
                        save(L"repeat-posted-2");repeatStep=4;post28AlternateStep=5;post28AlternateWait=phase+300;
                    }else if(repeatStep==4&&r.generation==repeatGeneration+1&&r.eligible&&own.completed
                             &&own.receipt>repeatFirstReceipt&&own.dispatch_ready
                             &&domain.kind==GameConversationProofKind::OwnedMain&&domain.id==repeatActivation+1
                             &&domain.origin_activation==repeatActivation&&own.origin_activation==repeatActivation
                             &&domain.owned_receipt==own.receipt){
                        save(L"repeat-ready-3");button(Circle);repeatWait=phase+5;repeatStep=5;
                    }else if(repeatStep==5&&phase>=repeatWait){
                        repeatPassed=r.generation==repeatGeneration+1&&domain.id==repeatActivation+1
                            &&r.posted==repeatPostedBase+1&&!r.event_posted&&!r.armed&&r.eligible;
                        save(L"repeat-held-3");storageBoundaryPassed=repeatPassed;
                        if(!repeatPassed){PostMessageW(window,WM_CLOSE,0,0);return;}
                        if(options.playWindowFixture){
                            storageBoundaryPassed=false;repeatStep=6;playGeneration=r.generation;playOrigin=domain.id;
                            playReceipt=own.receipt;playPosted=r.posted;button(0);playWait=phase+3;playStep=1;
                        }else return;
                    }
                    // Continue from generation3 through the SAME input owner.
                    // This is test UI automation, never a new product admission.
                    if(options.playWindowFixture&&repeatStep==6){
                        const auto playSave=[&](const wchar_t* cut){const auto name=std::wstring(cut)+L"-"+std::to_wstring(playGeneration);save(name.c_str());};
                        if(playStep==1&&phase>=playWait&&r.armed){
                            if(r.generation!=playGeneration||domain.id!=playOrigin||r.posted!=playPosted||r.event_posted
                               ||domain.kind!=GameConversationProofKind::OwnedMain||domain.owned_receipt!=playReceipt||!own.dispatch_ready){
                                playSave(L"play-release-refused");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
                            }
                            playSave(L"play-release");button(Circle);playWait=phase+120;playStep=2;
                        }else if(playStep==2&&r.event_posted){
                            if(r.generation!=playGeneration||r.posted!=playPosted+1||r.queue_result!=0
                               ||own.receipt<=playReceipt||own.origin_activation!=playOrigin){
                                playSave(L"play-post-refused");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
                            }
                            ++playPosted;playReceipt=own.receipt;playSave(L"play-posted");playStep=3;
                            post28AlternateStep=5;post28AlternateWait=phase+300;
                        }else if(playStep==3&&r.eligible){
                            if(r.generation!=playGeneration+1||domain.id!=playOrigin+1||r.posted!=playPosted||r.event_posted
                               ||domain.kind!=GameConversationProofKind::OwnedMain||domain.owned_receipt!=playReceipt
                               ||domain.origin_activation!=playOrigin||own.receipt!=playReceipt||!own.completed||!own.dispatch_ready){
                                playSave(L"play-successor-refused");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
                            }
                            ++playGeneration;++playOrigin;++playCycles;playSave(L"play-ready");
                            button(Circle);playWait=phase+5;playStep=4;
                        }else if(playStep==4&&phase>=playWait){
                            if(r.generation!=playGeneration||domain.id!=playOrigin||r.posted!=playPosted||r.event_posted||r.armed){
                                playSave(L"play-held-refused");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
                            }
                            playSave(L"play-held");
                            if(playCycles==3){playPassed=storageBoundaryPassed=true;playStep=5;return;}
                            button(0);playWait=phase+3;playStep=1;
                        }
                        if(playPassed){storageBoundaryPassed=true;return;}
                        if(playStep!=3){
                            if(phase>=playWait+300){playSave(L"play-input-timeout");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);}
                            return;
                        }
                    }
                    // Continue the already supported finite Input32 through
                    // its real editor, rather than calling an open UI a frontier.
                    // Text and confirmation use the same queued host APIs as typing.
                    if(repeatStep==4||(options.playWindowFixture&&repeatStep==6&&playStep==3)){
                        const auto editorSave=[&](const wchar_t* cut){
                            if(repeatStep==4)save(cut);else{const auto name=std::wstring(cut)+L"-"+std::to_wstring(playGeneration);save(name.c_str());}
                        };
                        const auto typed=repeatStep==4?L"shirt":L"book";
                        const auto typedCp932=repeatStep==4?"shirt":"book";
                        if(!repeatEditorStep&&a.awaiting_input&&e.live&&e.source_state==1&&e.focused){
                            if(a.input_mode!=0){editorSave(L"repeat-input-unsupported-mode");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;}
                            repeatEditorBaseline=a.input_completions;editorSave(L"repeat-input-open");
                            button(Circle);repeatEditorWait=phase+3;repeatEditorStep=1;
                        }else if(repeatEditorStep==1&&phase>=repeatEditorWait){
                            if(!a.awaiting_input||e.source_state!=1||a.input_completions!=repeatEditorBaseline){editorSave(L"repeat-input-held-failure");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;}
                            editorSave(L"repeat-input-held");if(!e.staged.empty()){PostText(GameTextKind::Home);for(unsigned n=0;n<8;++n)PostText(GameTextKind::Delete);}
                            CommitUnicode(typed);repeatEditorWait=phase+3;repeatEditorStep=2;
                        }else if(repeatEditorStep==2&&phase>=repeatEditorWait&&e.staged==typedCp932){
                            editorSave(L"repeat-input-typed");
                            if(!PostText(GameTextKind::Confirm)){editorSave(L"repeat-input-confirm-refused");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;}
                            repeatEditorWait=phase+120;repeatEditorStep=3;
                        }else if(repeatEditorStep==3&&e.source_state==2){
                            editorSave(L"repeat-input-closing");repeatEditorStep=4;
                        }else if(repeatEditorStep==4&&a.input_completions==repeatEditorBaseline+1&&!a.awaiting_input){
                            editorSave(L"repeat-input-result");repeatEditorStep=0;post28AlternateWait=phase+300;
                        }
                        if(repeatEditorStep){
                            if(phase>=repeatEditorWait+120){editorSave(L"repeat-input-editor-timeout");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);}
                            return;
                        }
                    }
                    if(repeatPassed&&!options.playWindowFixture){storageBoundaryPassed=true;return;}
                    if((repeatStep>0&&repeatStep<4)||repeatStep==5){
                        if(phase>=repeatWait+300){save(L"repeat-input-timeout");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);}
                        return;
                    }
                }
                if(domain.revoked){save(L"activation-owner-refused");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;}
                if(domain.body_completed&&!activationBodySaved){save(L"activation-body-return");activationBodySaved=true;}
                if(domain.returned&&!activationFarewellSaved){save(L"activation-farewell-return");activationFarewellSaved=true;}
                if(c.returned_interaction.admitted&&!activationPublished){
                    save(L"activation-published");activationPublished=true;button(Circle);activationWait=phase+5;post28AlternateStep=9;
                }
                if(post28AlternateStep==9&&phase>=activationWait){
                    activationHeldRefused=!c.returned_interaction.event_posted&&c.returned_interaction.posted==0;
                    save(L"activation-held");button(0);activationWait=phase+3;post28AlternateStep=10;
                }else if(post28AlternateStep==10&&phase>=activationWait&&c.returned_interaction.armed){
                    save(L"activation-release");button(Circle);activationWait=phase+120;post28AlternateStep=11;
                }else if(post28AlternateStep==11&&c.returned_interaction.event_posted){
                    save(L"activation-posted");activationPosted=true;activationChoicesAtPost=a.choice_completions;post28AlternateStep=12;
                }else if(post28AlternateStep==12&&c.returned_interaction.callback_returned){
                    save(L"activation-callback-return");post28AlternateStep=5;post28AlternateWait=phase+300;
                }
                // Observe ordinary deadline expiry and re-entry, without a
                // wake, event, game-state write or additional input token.
                // last_service is historical and cannot diagnose a barrier.
                const auto& v=a.vm_current;
                const bool timedIdle=activationPosted&&c.returned_interaction.callback_returned
                    &&a.choice_completions>activationChoicesAtPost
                    &&a.farewell_return.sequence>domain.return_sequence
                    &&!a.awaiting_message&&!a.awaiting_choice&&!a.awaiting_input
                    &&!a.blocked&&!v.stopped&&!a.waiting_for_event
                    &&v.valid&&v.context_registered&&!v.event_envelope_present
                    &&v.code_entry==0x1383D&&v.current_pc==0x13856
                    &&v.caller_entry==0x1387B&&v.caller_call_pc==0x138A5&&v.caller_return_pc==0x138A9
                    &&v.relative_deadline23>0;
                if(timedIdle&&!options.repeatWindowFixture){
                    if(!naturalLoopWait.valid){
                        naturalLoopWait=v;naturalLoopGeneration=a.generation;
                        save(L"natural-wait-before");
                    }else if(v.frame_id!=naturalLoopWait.frame_id&&v.executed>naturalLoopWait.executed
                             &&v.clock_tick>=naturalLoopWait.deadline_tick){
                        const bool sameOwner=a.generation==naturalLoopGeneration
                            &&v.run_id==naturalLoopWait.run_id&&v.code_sha256==naturalLoopWait.code_sha256
                            &&v.context_word==naturalLoopWait.context_word
                            &&v.call_depth==naturalLoopWait.call_depth&&v.active_blocks==naturalLoopWait.active_blocks
                            &&v.event_scope_depth==naturalLoopWait.event_scope_depth
                            &&c.returned_interaction.event_posted&&c.returned_interaction.posted==1
                            &&domain.consumed&&!c.returned_interaction.eligible&&!c.returned_interaction.armed;
                        if(!sameOwner){save(L"natural-owner-mismatch");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;}
                        ++naturalLoopCycles;
                        save(naturalLoopCycles==1?L"natural-loop-after":L"natural-idle");
                        naturalLoopWait=v;post28AlternateWait=phase+300;
                        if(naturalLoopCycles==2)naturalLoopPassed=true;
                    }
                }
                if(naturalLoopPassed){storageBoundaryPassed=true;return;}
                if(post28AlternateStep>=9){
                    if(phase>=activationWait+300){save(L"activation-next-frontier");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);}
                    return;
                }
            }
            if(post28AlternateStep==1&&phase>=post28AlternateWait){
                save(L"alternate-choice-held");button(0);post28AlternateWait=phase+3;post28AlternateStep=2;
            }else if(post28AlternateStep==2&&phase>=post28AlternateWait){
                save(L"alternate-choice-release");button(Circle);post28AlternateWait=phase+300;post28AlternateStep=3;
            }else if(post28AlternateStep==3&&c.actor_choice.state==2){
                save(L"alternate-choice-closing");post28AlternateStep=4;
            }else if((post28AlternateStep==3||post28AlternateStep==4)&&a.choice_completions>post28AlternateChoices&&!a.awaiting_choice){
                save(L"alternate-choice-result");post28AlternateStep=5;post28AlternateWait=phase+300;
            }
            if((post28AlternateStep==5||post28AlternateStep==8)&&a.awaiting_message&&c.actor_message_glyphs>0&&a.messages>a.message_acknowledgements&&a.messages>post28AlternateMessage){
                const auto name=L"alternate-message-"+std::to_wstring(a.messages);save(name.c_str());
                post28AlternateMessage=a.messages;post28AlternateAck=a.message_acknowledgements;button(Circle);post28AlternateWait=phase+5;post28AlternateStep=6;++post28AlternateCuts;
            }else if(post28AlternateStep==6&&phase>=post28AlternateWait){
                const auto name=L"alternate-held-"+std::to_wstring(a.messages);save(name.c_str());
                if(a.message_acknowledgements!=post28AlternateAck){storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;}
                button(0);post28AlternateWait=phase+3;post28AlternateStep=7;
            }else if(post28AlternateStep==7&&phase>=post28AlternateWait&&c.actor_message_input_armed){
                button(Circle);post28AlternateWait=phase+300;post28AlternateStep=8;
            }else if((post28AlternateStep==5||post28AlternateStep==8)&&a.awaiting_choice&&c.actor_choice.state==1){
                save(L"alternate-choice-menu");post28AlternateChoices=a.choice_completions;
                button(Circle);post28AlternateWait=phase+5;post28AlternateStep=1;
            }
            if((a.awaiting_input&&!options.repeatWindowFixture)||post28AlternateCuts>=(options.repeatWindowFixture?64u:32u)||((post28AlternateStep==5||post28AlternateStep==8)&&phase>=post28AlternateWait)){
                save(L"alternate-owned-frontier");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);
            }
            return;
        }
        const auto command=[&](GameTextKind kind){return PostText(kind);};
        //The inherited profile fixture explicitly injects focus, as its text
        //events already do. Recover only this test prefix after a real window
        //focus loss; never override the gameplay ownership/focus gates.
        if(options.post28WindowFixture&&softkeyStep<18&&e.live&&e.source_state==1&&!e.focused){
            command(GameTextKind::FocusGained);return;
        }
        const auto replace=[&](std::wstring_view value){
            command(GameTextKind::Home);for(unsigned n=0;n<40;++n)command(GameTextKind::Delete);
            CommitUnicode(value);
        };
        if(softkeyStep==0&&title.state==1&&title.layout==0&&title.frames>=40){
            button(0);softkeyWait=title.updates+3;softkeyStep=1;
        }else if(softkeyStep==1&&title.updates>=softkeyWait){button(Circle);softkeyStep=2;
        }else if(softkeyStep==2&&title.layout==2&&title.state==1){button(0);softkeyWait=title.updates+3;softkeyStep=3;
        }else if(softkeyStep==3&&title.updates>=softkeyWait){button(Circle);softkeyStep=4;
        }else if(softkeyStep==4&&title.layout==3&&e.source_state==1&&e.focused){
            save(L"softkey-open");
            const auto scan=LPARAM(MapVirtualKeyW('X',MAPVK_VK_TO_VSC))<<16;
            SendMessageW(window,WM_KEYDOWN,'X',scan);SendMessageW(window,WM_CHAR,'x',scan);SendMessageW(window,WM_KEYUP,'X',scan);
            button(Circle);softkeyWait=title.updates+3;softkeyStep=5;
        }else if(softkeyStep==5&&title.updates>=softkeyWait&&e.units==3&&title.confirmations==2){
            replace(L"トロAB");CommitUnicode(L"C");CommitUnicode(L"\U0001F600");
            command(GameTextKind::CompositionBegin);command(GameTextKind::Confirm);command(GameTextKind::CompositionEnd);
            command(GameTextKind::FocusLost);command(GameTextKind::Confirm);command(GameTextKind::FocusGained);
            softkeyWait=title.updates+3;softkeyStep=6;
        }else if(softkeyStep==6&&title.updates>=softkeyWait&&e.units==4&&e.rejections>=4){
            save(L"softkey-edited");cancelledTextContext=e.context;command(GameTextKind::Cancel);softkeyStep=7;
        }else if(softkeyStep==7){
            if(e.source_state==2&&!e.result_ready){textCloseObserved=true;save(L"softkey-closing");}
            if(title.layout==2&&title.state==1&&e.source_state==3&&e.cancelled){
                textCancelObserved=true;save(L"softkey-cancelled");button(0);softkeyWait=title.updates+3;softkeyStep=8;
            }
        }else if(softkeyStep==8&&title.updates>=softkeyWait){button(Circle);softkeyStep=9;
        }else if(softkeyStep==9&&title.layout==3&&e.source_state==1&&e.focused&&e.context!=cancelledTextContext){
            staleTextRefused=!PostText(GameTextKind::Confirm,cancelledTextContext);
            replace(L"トロA");command(GameTextKind::Confirm);softkeyStep=10;
        }else if(softkeyStep==10&&title.layout==4&&title.profile.editor_field==uint32_t(GameProfileField::Player)&&e.source_state==1&&e.focused){
            save(L"nickname-result");testedEditorContext=e.context;command(GameTextKind::Cancel);softkeyStep=11;
        }else if(softkeyStep==11&&title.profile.editor_field==uint32_t(GameProfileField::Player)&&e.context!=testedEditorContext&&e.source_state==1&&e.focused){
            playerCancelObserved=title.profile.player.empty()&&title.profile.present==1;
            save(L"player-cancelled");replace(L"Sen");command(GameTextKind::Confirm);softkeyStep=12;
        }else if(softkeyStep==12&&e.source_state==1&&e.focused){
            const auto field=GameProfileField(title.profile.editor_field);
            if(dayValidationPending&&field==GameProfileField::Day&&!e.error.empty()){
                dayRejectedObserved=true;dayValidationPending=false;save(L"birthday-invalid");replace(L"29");command(GameTextKind::Confirm);
            }else if(e.context!=testedEditorContext){
                testedEditorContext=e.context;
                switch(field){
                    case GameProfileField::Gender:save(L"profile-gender");replace(L"2");command(GameTextKind::Confirm);break;
                    case GameProfileField::Year:command(GameTextKind::Cancel);replace(L"2000");command(GameTextKind::Confirm);break;
                    case GameProfileField::Month:
                        if(!birthdayBackObserved){birthdayBackObserved=true;command(GameTextKind::Cancel);}
                        else {replace(L"2");command(GameTextKind::Confirm);}break;
                    case GameProfileField::Day:replace(L"31");command(GameTextKind::Confirm);dayValidationPending=true;break;
                    case GameProfileField::Blood:replace(L"3");command(GameTextKind::Confirm);break;
                    case GameProfileField::Comment:save(L"profile-comment");replace(L"あいうえおかきくけこさしす");command(GameTextKind::Confirm);break;
                    default:break;
                }
            }
        }
        if(softkeyStep==12&&title.profile.present==7&&c.scene_ui.awaiting_confirmation){
            save(L"profile-confirmation");button(0);softkeyWait=title.updates+3;softkeyStep=13;
        }else if(softkeyStep==13&&title.updates>=softkeyWait){button(Cross);softkeyStep=14;
        }else if(softkeyStep==14&&c.scene_ui.dialog_state==3&&!title.profile.applied){
            profileNoObserved=true;save(L"profile-no");button(0);softkeyWait=title.updates+3;softkeyStep=15;
        }else if(softkeyStep==15&&title.updates>=softkeyWait){button(0);softkeyWait=title.updates+3;softkeyStep=19;
        }else if(softkeyStep==19&&title.updates>=softkeyWait){button(Circle);softkeyStep=16;
        }else if(softkeyStep==16&&c.scene_ui.awaiting_confirmation){button(0);softkeyWait=title.updates+3;softkeyStep=17;
        }else if(softkeyStep==17&&title.updates>=softkeyWait){button(Circle);softkeyStep=18;}
        if(options.cameraWindowTest&&softkeyStep==18){
            const auto& s=c.actor_script;const auto phase=s.runtime.actor_phases;
            if(messageTestStep==0&&s.awaiting_message&&s.messages==1){save(L"game-first-message");messageTestWait=phase+8;messageTestStep=1;}
            else if(messageTestStep==1&&phase>=messageTestWait&&s.messages==1&&s.message_acknowledgements==0){
                messageHeldRefused=true;(void)nativeStartup->InjectTestControllerInput(0,false);
                (void)nativeStartup->InjectTestControllerInput(Circle,false);messageTestWait=phase+5;messageTestStep=2;
            }else if(messageTestStep==2&&phase>=messageTestWait&&s.messages==1&&s.message_acknowledgements==0){
                messageFocusRefused=true;button(Circle);messageTestWait=phase+5;messageTestStep=3;
            }else if(messageTestStep==3&&phase>=messageTestWait&&s.messages==1&&s.message_acknowledgements==0){
                save(L"game-message-held");button(0);messageTestWait=phase+3;messageTestStep=4;
            }else if(messageTestStep==4&&phase>=messageTestWait){button(Circle);messageTestStep=5;}
            else if(messageTestStep==5&&s.awaiting_message&&s.messages==2&&s.message_acknowledgements==1){
                messageFreshAccepted=true;save(L"game-second-message");button(0);messageTestWait=phase+3;messageTestStep=6;
            }
            if(options.conversationWindowFixture&&messageTestStep==6&&s.awaiting_message&&s.messages==conversationNextMessage&&s.messages<=15){
                if(conversationInputStage==0){
                    const auto name=L"conversation-message-"+std::to_wstring(s.messages);save(name.c_str());
                    //A held first edge must not consume the following context.
                    if(s.messages==2){button(0);conversationWait=phase+3;conversationInputStage=2;}
                    else{conversationWait=phase+4;conversationInputStage=1;}
                }else if(conversationInputStage==1&&phase>=conversationWait&&s.message_acknowledgements==s.messages-1){
                    button(0);conversationWait=phase+3;conversationInputStage=2;
                }else if(conversationInputStage==2&&phase>=conversationWait&&c.actor_message_input_armed){
                    if(s.messages==15){save(L"conversation-gift-offer");conversationInputStage=3;if(options.inventoryWindowFixture){button(Circle);inventoryTestStep=1;}}
                    else{button(Circle);++conversationNextMessage;conversationInputStage=0;}
                }
            }
        }
        if(options.inventoryWindowFixture&&inventoryTestStep){
            const auto& g=c.inventory;const auto phase=c.actor_script.runtime.actor_phases;
            if(g.phase==2&&!inventoryPendingSaved){save(L"inventory-open-pending");inventoryPendingSaved=true;}
            if(g.phase==4&&!inventoryClosingSaved){save(L"inventory-close-pending");inventoryClosingSaved=true;}
            if(options.decorateWindowFixture&&inventoryTestStep>=100){
                const auto& action=c.actor_script.inventory_action;
                const auto failed=[&]{save(L"decoration-mismatch");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);};
                if(inventoryTestStep==100&&phase>=inventoryTestWait){button(Circle);inventoryTestWait=phase+120;inventoryTestStep=101;}
                else if(inventoryTestStep==101&&action.completed&&g.action_completions==1){
                    if(action.event!=13||int32_t(action.row)!=itemActionRow||action.item_id!=itemActionId||action.before_words[1]!=0
                       ||(action.after_words[1]>>28)!=2||(action.after_words[2]>>28)!=2||!action.receipt||g.filter!=3||g.count!=0){failed();return;}
                    itemMarkReceipt=action.receipt;save(L"item-decorated");button(0);inventoryTestWait=phase+3;inventoryTestStep=102;
                }else if(inventoryTestStep==102&&phase>=inventoryTestWait){button(Left);inventoryTestStep=103;}
                else if(inventoryTestStep==103&&g.filter==2&&g.count&&g.selected_row==itemActionRow&&g.selected_id==itemActionId){
                    const auto slot=int32_t(action.after_words[1]&0x0FFFFFFF);const auto& room=c.actor_script.runtime;
                    decorationPassed=slot>=0&&size_t(slot)<room.room_models.size()&&room.room_models[size_t(slot)].active
                        &&!room.room_models[size_t(slot)].retirement&&room.room_models[size_t(slot)].resource==itemActionId
                        &&room.model_publications==decoratePublicationsBefore+1&&g.action_events==1&&g.action_completions==1&&!g.action_pending;
                    inventoryActionPassed=decorationPassed;save(L"item-room-list");if(!decorationPassed){failed();return;}
                    button(0);inventoryTestWait=phase+3;inventoryTestStep=104;
                }else if(inventoryTestStep==104&&phase>=inventoryTestWait){
                    if(!options.putawayWindowFixture){button(Cross);inventoryTestStep=11;}
                    else{const auto& room=c.actor_script.runtime;putawaySlot=int32_t(action.after_words[1]&0x0FFFFFFF);
                        putawayGeneration=room.room_models[size_t(putawaySlot)].generation;putawaySlotsBefore=room.model_slots;
                        putawayPublicationsBefore=room.model_publications;putawayRetirementsBefore=room.model_retirements;putawayRetireRequestsBefore=room.model_retire_requests;
                        for(size_t i=0;i<room.room_models.size();++i)if(room.room_models[i].active&&int32_t(i)!=putawaySlot)putawayOtherModels.emplace_back(int32_t(i),room.room_models[i]);
                        button(Circle);inventoryTestStep=options.discardWindowFixture?200u:105u;}
                }else if(inventoryTestStep==105&&g.actions&&g.action_selection==0){button(0);inventoryTestWait=phase+3;inventoryTestStep=106;}
                else if(inventoryTestStep==106&&phase>=inventoryTestWait){button(Down);inventoryTestStep=107;}
                else if(inventoryTestStep==107&&g.actions&&g.action_selection==1){save(L"item-putaway-menu");button(0);inventoryTestWait=phase+3;inventoryTestStep=108;}
                else if(inventoryTestStep==108&&phase>=inventoryTestWait){button(Circle);inventoryTestWait=phase+120;inventoryTestStep=109;}
                else if(inventoryTestStep==109&&action.completed&&g.action_completions==2){
                    if(action.event!=14||action.receipt<=itemMarkReceipt||action.row!=uint32_t(itemActionRow)||action.item_id!=itemActionId
                       ||action.before_words[1]!=(0x20000000u|uint32_t(putawaySlot))||action.after_words[1]!=0||action.after_words[2]!=0
                       ||action.after_controls[2]!=0x20000000u||action.gate_word!=0x20000000u||g.action_events!=2||g.action_pending){failed();return;}
                    putawayReceipt=action.receipt;save(L"item-putaway-callback");button(0);inventoryTestStep=110;
                }else if(inventoryTestStep==110){
                    const auto& room=c.actor_script.runtime;
                    const auto r=std::find_if(room.model_retirement_history.begin(),room.model_retirement_history.end(),[&](const auto& w){return w.slot==putawaySlot&&w.generation==putawayGeneration;});
                    if(r!=room.model_retirement_history.end()&&r->destroyed_phase){
                        uint32_t creates=0,retires=0;for(uint32_t i=0;i<action.model_service_count&&i<action.model_services.size();++i){const auto& m=action.model_services[i];if(!m.returned){failed();return;}if(m.service==31&&m.slot>=0)++creates;if(m.service==32&&m.slot==putawaySlot)++retires;}
                        modelRemovalPassed=retires==1&&!room.model_retirement_history_dropped&&r->requested_phase<r->disabled_phase&&r->disabled_phase<r->destroyed_phase
                            &&r->assets_retained_at_disable&&r->clone_retained_at_disable&&r->asset_owner_expired&&r->clone_owner_cleared&&r->resource==itemActionId
                            &&!room.room_models[size_t(putawaySlot)].active&&room.room_models[size_t(putawaySlot)].generation==putawayGeneration
                            &&room.model_slots==putawaySlotsBefore+creates-1&&room.model_publications==putawayPublicationsBefore+creates&&room.model_retirements==putawayRetirementsBefore+1;
                        for(const auto& other:putawayOtherModels){const auto& now=room.room_models[size_t(other.first)];modelRemovalPassed=modelRemovalPassed&&now.active&&now.generation==other.second.generation&&now.resource==other.second.resource;}
                        save(options.discardWindowFixture?L"item-discard-destroyed":L"item-putaway-destroyed");if(!modelRemovalPassed){failed();return;}button(Right);inventoryTestStep=options.discardWindowFixture?216u:111u;
                    }
                }else if(inventoryTestStep==111&&g.filter==3&&g.count&&g.selected_row==itemActionRow&&g.selected_id==itemActionId){save(L"item-stored-list");button(0);inventoryTestWait=phase+3;inventoryTestStep=112;}
                else if(inventoryTestStep==112&&phase>=inventoryTestWait){button(Cross);inventoryTestStep=11;}
                if(options.discardWindowFixture&&inventoryTestStep>=200){
                    const auto unchanged=[&]{return action.completed&&action.event==13&&action.receipt==itemMarkReceipt&&g.action_events==1&&g.action_completions==1&&!g.action_pending
                        &&c.actor_script.vm_current.executed==discardCancelExecuted&&c.actor_script.callbacks_returned==2
                        &&c.actor_script.runtime.room_models[size_t(putawaySlot)].active&&c.actor_script.runtime.room_models[size_t(putawaySlot)].generation==putawayGeneration
                        &&c.actor_script.runtime.model_retire_requests==putawayRetireRequestsBefore&&g.items[size_t(itemActionRow)].valid&&g.items[size_t(itemActionRow)].id==itemActionId;};
                    if(inventoryTestStep==200&&g.actions&&g.action_selection==0){button(0);inventoryTestWait=phase+3;inventoryTestStep=201;}
                    else if(inventoryTestStep==201&&phase>=inventoryTestWait){button(Down);inventoryTestStep=202;}
                    else if(inventoryTestStep==202&&g.action_selection==1){button(0);inventoryTestWait=phase+3;inventoryTestStep=203;}
                    else if(inventoryTestStep==203&&phase>=inventoryTestWait){button(Down);inventoryTestStep=204;}
                    else if(inventoryTestStep==204&&g.action_selection==2){button(0);inventoryTestWait=phase+3;inventoryTestStep=205;}
                    else if(inventoryTestStep==205&&phase>=inventoryTestWait){button(Down);inventoryTestStep=206;}
                    else if(inventoryTestStep==206&&g.action_selection==3){save(L"item-discard-menu");discardCancelExecuted=c.actor_script.vm_current.executed;button(0);inventoryTestWait=phase+3;inventoryTestStep=207;}
                    else if(inventoryTestStep==207&&phase>=inventoryTestWait){button(Circle);inventoryTestStep=208;}
                    else if(inventoryTestStep==208&&g.discard_confirmation&&g.discard_dialog_state==1){save(L"discard-prompt");inventoryTestWait=phase+8;inventoryTestStep=209;}
                    else if(inventoryTestStep==209&&phase>=inventoryTestWait){discardHeldPassed=unchanged()&&g.discard_prompts==1&&!g.discard_accepted&&!g.discard_cancelled&&g.discard_dialog_state==1;save(L"discard-held");if(!discardHeldPassed){failed();return;}button(0);inventoryTestWait=phase+3;inventoryTestStep=210;}
                    else if(inventoryTestStep==210&&phase>=inventoryTestWait){button(Cross);inventoryTestStep=211;}
                    else if(inventoryTestStep==211&&!g.discard_confirmation&&g.actions&&g.discard_cancelled==1){discardCancelPassed=unchanged()&&g.discard_result!=1&&!g.discard_accepted;save(L"discard-cancelled");if(!discardCancelPassed){failed();return;}button(0);inventoryTestWait=phase+3;inventoryTestStep=212;}
                    else if(inventoryTestStep==212&&phase>=inventoryTestWait){button(Circle);inventoryTestStep=213;}
                    else if(inventoryTestStep==213&&g.discard_confirmation&&g.discard_dialog_state==1&&g.discard_prompts==2){save(L"discard-prompt-again");button(0);inventoryTestWait=phase+3;inventoryTestStep=214;}
                    else if(inventoryTestStep==214&&phase>=inventoryTestWait){button(Circle);inventoryTestWait=phase+120;inventoryTestStep=215;}
                    else if(inventoryTestStep==215&&action.completed&&g.action_completions==2){
                        if(action.event!=16||action.receipt<=itemMarkReceipt||action.row!=uint32_t(itemActionRow)||action.item_id!=itemActionId||action.row_cells!=10||g.action_events!=2||g.action_pending
                           ||action.before_words[1]!=(0x20000000u|uint32_t(putawaySlot))||g.discard_result!=1||g.discard_accepted!=1||g.discard_cancelled!=1){failed();return;}
                        for(uint32_t i=0;i<action.row_cells;++i)if(action.after_row_words[i]!=0||action.after_row_kinds[i]!=1){failed();return;}
                        putawayReceipt=action.receipt;save(L"item-discard-callback");button(0);inventoryTestStep=110;
                    }else if(inventoryTestStep==216&&g.filter==3&&!g.count&&!g.items[size_t(itemActionRow)].valid){save(L"discard-empty-stored");button(0);inventoryTestWait=phase+3;inventoryTestStep=217;}
                    else if(inventoryTestStep==217&&phase>=inventoryTestWait){button(Left);inventoryTestStep=218;}
                    else if(inventoryTestStep==218&&g.filter==2&&!g.count){button(0);inventoryTestWait=phase+3;inventoryTestStep=219;}
                    else if(inventoryTestStep==219&&phase>=inventoryTestWait){button(Left);inventoryTestStep=220;}
                    else if(inventoryTestStep==220&&g.filter==1&&!g.count&&g.selected_row==-1){discardPassed=modelRemovalPassed&&discardHeldPassed&&discardCancelPassed&&g.items.size()==170&&action.event==16&&action.receipt==putawayReceipt;save(L"discard-empty-all");if(!discardPassed){failed();return;}button(0);inventoryTestWait=phase+3;inventoryTestStep=221;}
                    else if(inventoryTestStep==221&&phase>=inventoryTestWait){button(Cross);inventoryTestStep=11;}
                }
                if(phase>=inventoryTestWait+300){failed();return;}
            }
            if(options.inventoryActionWindowFixture&&!options.decorateWindowFixture&&inventoryTestStep>=80){
                const auto& action=c.actor_script.inventory_action;
                const auto failed=[&]{save(L"item-action-fixture-refused");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);};
                if(inventoryTestStep==80&&g.action_selection==1){button(0);inventoryTestWait=phase+3;inventoryTestStep=81;}
                else if(inventoryTestStep==81&&phase>=inventoryTestWait){button(Down);inventoryTestStep=82;}
                else if(inventoryTestStep==82&&g.action_selection==2){save(L"item-mark-menu");button(0);inventoryTestWait=phase+3;inventoryTestStep=83;}
                else if(inventoryTestStep==83&&phase>=inventoryTestWait){button(Circle);inventoryTestWait=phase+120;inventoryTestStep=84;}
                else if(inventoryTestStep==84&&action.completed&&g.action_completions==1){
                    if(action.event!=15||int32_t(action.row)!=itemActionRow||action.item_id!=itemActionId||action.after_words[0]!=0x10000001
                       ||action.before_words[1]!=0||action.after_words[1]!=0||!action.receipt||g.count!=0||g.filter!=3){failed();return;}
                    itemMarkReceipt=action.receipt;save(L"item-marked");button(0);inventoryTestWait=phase+3;inventoryTestStep=85;
                }else if(inventoryTestStep==85&&phase>=inventoryTestWait){button(Right);inventoryTestStep=86;}
                else if(inventoryTestStep==86&&g.filter==4&&g.count&&g.selected_row==itemActionRow&&g.selected_id==itemActionId){
                    save(L"item-exchange-list");button(0);inventoryTestWait=phase+3;inventoryTestStep=87;
                }else if(inventoryTestStep==87&&phase>=inventoryTestWait){button(Circle);inventoryTestStep=88;}
                else if(inventoryTestStep==88&&g.actions){button(0);inventoryTestWait=phase+3;inventoryTestStep=89;}
                else if(inventoryTestStep==89&&phase>=inventoryTestWait){button(Down);inventoryTestStep=90;}
                else if(inventoryTestStep==90&&g.action_selection==1){save(L"item-putaway-menu");button(0);inventoryTestWait=phase+3;inventoryTestStep=91;}
                else if(inventoryTestStep==91&&phase>=inventoryTestWait){button(Circle);inventoryTestWait=phase+120;inventoryTestStep=92;}
                else if(inventoryTestStep==92&&action.completed&&g.action_completions==2){
                    if(action.event!=14||int32_t(action.row)!=itemActionRow||action.item_id!=itemActionId||action.receipt<=itemMarkReceipt
                       ||action.before_words[0]!=0x10000001||action.after_words[0]!=0x10000000||g.filter!=4||g.count!=0){failed();return;}
                    save(L"item-putaway");button(0);inventoryTestWait=phase+3;inventoryTestStep=93;
                }else if(inventoryTestStep==93&&phase>=inventoryTestWait){button(Left);inventoryTestStep=94;}
                else if(inventoryTestStep==94&&g.filter==3&&g.count&&g.selected_row==itemActionRow&&g.selected_id==itemActionId){
                    inventoryActionPassed=g.action_events==2&&g.action_completions==2&&!g.action_pending&&g.action_cancels==0;
                    save(L"item-stored-list");if(!inventoryActionPassed){failed();return;}
                    button(0);inventoryTestWait=phase+3;inventoryTestStep=95;
                }else if(inventoryTestStep==95&&phase>=inventoryTestWait){button(Cross);inventoryTestStep=11;}
                if(phase>=inventoryTestWait+300){failed();return;}
            }
            if(inventoryTestStep==1&&g.phase==3){save(L"inventory-open");button(0);inventoryTestWait=phase+4;inventoryTestStep=2;}
            else if(inventoryTestStep==2&&phase>=inventoryTestWait){button(Right);inventoryTestStep=3;}
            else if(inventoryTestStep==3&&g.filter==2){inventoryFilterEmpty=g.count==0;save(L"inventory-empty-filter");button(0);inventoryTestWait=phase+3;inventoryTestStep=4;}
            else if(inventoryTestStep==4&&phase>=inventoryTestWait){button(Right);inventoryTestStep=5;}
            else if(inventoryTestStep==5&&g.filter==3&&g.count>0){button(0);inventoryTestWait=phase+3;inventoryTestStep=6;}
            else if(inventoryTestStep==6&&phase>=inventoryTestWait){button(Circle);inventoryTestStep=7;}
            else if(inventoryTestStep==7&&g.actions){save(L"inventory-actions");button(0);inventoryTestWait=phase+3;inventoryTestStep=8;}
            else if(inventoryTestStep==8&&phase>=inventoryTestWait){
                if(options.decorateWindowFixture){itemActionRow=g.selected_row;itemActionId=g.selected_id;decoratePublicationsBefore=c.actor_script.runtime.model_publications;save(L"item-decorate-menu");button(0);inventoryTestWait=phase+3;inventoryTestStep=100;}
                else if(options.inventoryActionWindowFixture){itemActionRow=g.selected_row;itemActionId=g.selected_id;button(Down);inventoryTestStep=80;}
                else{button(Cross);inventoryTestStep=9;}
            }
            else if(inventoryTestStep==9&&!g.actions&&g.action_cancels==1){inventoryCancelObserved=g.phase==3&&g.stateB2C==1;save(L"inventory-action-cancel");button(0);inventoryTestWait=phase+3;inventoryTestStep=10;}
            else if(inventoryTestStep==10&&phase>=inventoryTestWait){button(Cross);inventoryTestStep=11;}
            else if(inventoryTestStep==11&&g.phase==7&&c.actor_script.inventory_completions==1&&
                    (options.game71WindowFixture?(c.actor_script.awaiting_message&&c.actor_script.messages==16):c.actor_script.blocked)){
                save(options.discardWindowFixture?L"discard-room":options.putawayWindowFixture?L"putaway-room":options.decorateWindowFixture?L"decoration-room":options.inventoryActionWindowFixture?L"item-next-message":options.game71WindowFixture?L"game71-record-message":L"inventory-next-consumer");button(0);inventoryTestWait=phase+4;inventoryTestStep=12;
            }else if(options.game71WindowFixture&&!options.inventoryActionWindowFixture&&inventoryTestStep==12&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestStep=13;
            }else if(options.game71WindowFixture&&inventoryTestStep==13&&c.actor_script.game3_calls==1&&c.actor_script.runtime.camera_completions==2
                    &&c.actor_script.runtime.camera_manager_state28==0&&c.actor_script.last_service==28&&c.actor_script.waiting_deadline){
                save(L"game71-after-bye");if(!options.manager9WindowFixture)button(0);inventoryTestStep=14;
            }else if(options.manager9WindowFixture&&inventoryTestStep==14&&c.post_modal.eligible){
                button(Circle);inventoryTestWait=phase+5;inventoryTestStep=15;
            }else if(options.manager9WindowFixture&&inventoryTestStep==15&&phase>=inventoryTestWait){
                managerHeldRefused=!c.post_modal.event_posted&&c.actor_script.messages==16;
                save(L"manager9-source-blocked");button(Circle,false);inventoryTestStep=16;
            }else if(options.manager9WindowFixture&&inventoryTestStep==16){
                button(Circle,true);inventoryTestWait=phase+5;inventoryTestStep=17;
            }else if(options.manager9WindowFixture&&inventoryTestStep==17&&phase>=inventoryTestWait){
                managerFocusRefused=!c.post_modal.event_posted&&c.actor_script.messages==16;
                button(0);inventoryTestStep=18;
            }else if(options.manager9WindowFixture&&inventoryTestStep==18&&c.post_modal.armed){
                save(L"manager9-native-armed");button(Circle);inventoryTestStep=19;
            }else if(options.manager9WindowFixture&&inventoryTestStep==19&&c.actor_script.awaiting_message&&c.actor_script.messages==17
                     &&c.post_modal.callback_returned&&c.actor_message_glyphs>0){
                save(L"manager9-next-message");inventoryTestWait=phase+5;inventoryTestStep=20;
            }else if(options.manager9WindowFixture&&inventoryTestStep==20&&phase>=inventoryTestWait){
                managerNextHeldRefused=c.actor_script.messages==17&&c.actor_script.message_acknowledgements==16
                    &&c.actor_script.awaiting_message&&c.post_modal.posted==1;
                inventoryTestStep=21;
            }else if(options.post17WindowFixture&&inventoryTestStep==21){
                button(0);inventoryTestWait=phase+3;inventoryTestStep=22;
            }else if(options.post17WindowFixture&&inventoryTestStep==22&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestStep=23;
            }else if(options.post17WindowFixture&&inventoryTestStep==23&&c.actor_script.messages==18&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post17-question");inventoryTestWait=phase+5;inventoryTestStep=24;
            }else if(options.post17WindowFixture&&inventoryTestStep==24&&phase>=inventoryTestWait){
                post17HeldRefused=c.actor_script.message_acknowledgements==17&&c.actor_script.awaiting_message;
                button(0);inventoryTestWait=phase+3;inventoryTestStep=25;
            }else if(options.post17WindowFixture&&inventoryTestStep==25&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestStep=26;
            }else if(options.post17WindowFixture&&inventoryTestStep==26&&c.actor_script.awaiting_choice&&c.actor_choice.state==1){
                save(L"post17-three-options");inventoryTestWait=phase+5;inventoryTestStep=27;
            }else if(options.post19WindowFixture&&inventoryTestStep==26&&c.actor_script.awaiting_message&&c.actor_script.messages==19&&c.actor_script.choice_requests==0&&c.actor_message_glyphs>0){
                post19LinearRoute=true;save(L"post19-baseline");inventoryTestWait=phase+5;inventoryTestStep=34;
            }else if(options.post17WindowFixture&&inventoryTestStep==27&&phase>=inventoryTestWait){
                choiceHeldRefused=c.actor_script.awaiting_choice&&c.actor_script.choice_completions==0&&c.actor_choice.state==1;
                button(0);inventoryTestWait=phase+3;inventoryTestStep=28;
            }else if(options.post17WindowFixture&&inventoryTestStep==28&&phase>=inventoryTestWait){
                button(Cross);inventoryTestWait=phase+3;inventoryTestStep=29;
            }else if(options.post17WindowFixture&&inventoryTestStep==29&&phase>=inventoryTestWait){
                choiceCancelRefused=c.actor_script.awaiting_choice&&c.actor_choice.state==1&&c.actor_script.choice_completions==0;
                button(0);inventoryTestWait=phase+3;inventoryTestStep=30;
            }else if(options.post17WindowFixture&&inventoryTestStep==30&&phase>=inventoryTestWait){
                button(Down);inventoryTestStep=31;
            }else if(options.post17WindowFixture&&inventoryTestStep==31&&c.actor_choice.row==1){
                save(L"post17-second-option");button(0);inventoryTestWait=phase+3;inventoryTestStep=32;
            }else if(options.post17WindowFixture&&inventoryTestStep==32&&phase>=inventoryTestWait){
                button(Circle);inventoryTestStep=33;
            }else if(options.post17WindowFixture&&inventoryTestStep==33){
                if(c.actor_choice.state==2&&!choiceClosingObserved){save(L"post17-choice-closing");choiceClosingObserved=true;}
                if(c.actor_script.messages==19&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                    save(L"post17-choice-reply");if(options.post19WindowFixture)save(L"post19-baseline");inventoryTestWait=phase+5;inventoryTestStep=34;
                }
            }else if(options.post17WindowFixture&&inventoryTestStep==34&&phase>=inventoryTestWait){
                postChoiceHeldRefused=c.actor_script.message_acknowledgements==18&&c.actor_script.awaiting_message;
                inventoryTestStep=35;
            }else if(options.post19WindowFixture&&inventoryTestStep==35){
                button(0);inventoryTestWait=phase+3;inventoryTestStep=36;
            }else if(options.post19WindowFixture&&inventoryTestStep==36&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestStep=37;
            }else if(options.post28WindowFixture&&inventoryTestStep==37&&c.actor_script.awaiting_choice&&c.actor_choice.state==1){
                save(L"alternate-choice-menu");post28AlternateChoices=c.actor_script.choice_completions;
                post28AlternateWait=phase+5;post28AlternateStep=1;
            }else if(options.activationWindowFixture&&inventoryTestStep==37&&c.actor_script.awaiting_message&&c.actor_script.messages==20){
                save(L"activation-route-mismatch");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
            }else if(options.post19WindowFixture&&inventoryTestStep==37&&c.actor_script.messages==20&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"after19-message20");inventoryTestWait=phase+5;inventoryTestStep=38;
            }else if(options.post19WindowFixture&&inventoryTestStep==38&&phase>=inventoryTestWait){
                post19HeldRefused=c.actor_script.message_acknowledgements==19&&c.actor_script.awaiting_message;
                button(0);inventoryTestWait=phase+3;inventoryTestStep=39;
            }else if(options.post19WindowFixture&&inventoryTestStep==39&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestStep=40;
            }else if(options.post21WindowFixture&&inventoryTestStep==40&&c.actor_script.messages==20&&c.actor_script.message_acknowledgements==20
                     &&!c.actor_script.awaiting_message&&!c.actor_script.awaiting_choice&&c.actor_script.pc==0x13856){
                post21BranchMismatch=true;save(L"post21-alternate-branch");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
            }else if(options.post19WindowFixture&&inventoryTestStep==40&&c.actor_script.messages==21&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(post19LinearRoute?L"after20-message21":L"after20-question21");inventoryTestWait=phase+5;inventoryTestStep=41;
            }else if(options.post19WindowFixture&&inventoryTestStep==41&&phase>=inventoryTestWait){
                post20HeldRefused=c.actor_script.message_acknowledgements==20&&c.actor_script.awaiting_message;
                if(post19LinearRoute&&!options.post21WindowFixture){save(L"post19-held-message21");inventoryTestStep=48;}
                else{if(options.post21WindowFixture)save(L"post21-baseline");button(0);inventoryTestWait=phase+3;inventoryTestStep=42;}
            }else if(options.post19WindowFixture&&inventoryTestStep==42&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestStep=43;
            }else if(options.post19WindowFixture&&inventoryTestStep==43&&c.actor_script.awaiting_choice&&c.actor_choice.state==1&&(options.post21WindowFixture||c.actor_script.choice_requests==2)){
                save(L"after21-options");inventoryTestWait=phase+5;inventoryTestStep=44;
            }else if(options.post19WindowFixture&&inventoryTestStep==44&&phase>=inventoryTestWait){
                post21HeldRefused=c.actor_script.message_acknowledgements==21&&c.actor_script.messages==21;
                secondChoiceHeldRefused=c.actor_script.choice_requests==c.actor_script.choice_completions+1&&c.actor_choice.confirmations==0&&c.actor_choice.state==1;
                button(0);inventoryTestWait=phase+3;inventoryTestStep=45;
            }else if(options.post19WindowFixture&&inventoryTestStep==45&&phase>=inventoryTestWait){
                button(Circle);inventoryTestStep=46;
            }else if(options.post19WindowFixture&&inventoryTestStep==46){
                if(c.actor_choice.state==2&&!secondChoiceClosingObserved){save(L"second-choice-closing");secondChoiceClosingObserved=true;}
                if(c.actor_script.messages==22&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                    save(L"second-choice-reply");inventoryTestWait=phase+5;inventoryTestStep=47;
                }
            }else if(options.post19WindowFixture&&inventoryTestStep==47&&phase>=inventoryTestWait){
                reply22HeldRefused=c.actor_script.message_acknowledgements==21&&c.actor_script.awaiting_message;
                if(options.post21WindowFixture){button(0);inventoryTestWait=phase+3;inventoryTestStep=49;}else inventoryTestStep=48;
            }else if(options.post21WindowFixture&&inventoryTestStep==49&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestStep=50;
            }else if(options.post21WindowFixture&&inventoryTestStep==50&&c.actor_script.messages==23&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post21-response23");inventoryTestWait=phase+5;inventoryTestStep=51;
            }else if(options.post21WindowFixture&&inventoryTestStep==51&&phase>=inventoryTestWait){
                post23HeldRefused=c.actor_script.messages==23&&c.actor_script.message_acknowledgements==22&&c.actor_script.awaiting_message;button(0);inventoryTestWait=phase+3;inventoryTestStep=52;
            }else if(options.post21WindowFixture&&inventoryTestStep==52&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestStep=53;
            }else if(options.post21WindowFixture&&inventoryTestStep==53&&c.actor_script.messages==24&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post21-response24");inventoryTestWait=phase+5;inventoryTestStep=54;
            }else if(options.post21WindowFixture&&inventoryTestStep==54&&phase>=inventoryTestWait){
                post24HeldRefused=c.actor_script.messages==24&&c.actor_script.message_acknowledgements==23&&c.actor_script.awaiting_message;
                if(options.post24WindowFixture){save(L"post24-before-ack");button(0);inventoryTestWait=phase+3;inventoryTestStep=56;}else inventoryTestStep=55;
            }else if(options.post24WindowFixture&&inventoryTestStep==56&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestStep=57;
            }else if(options.post24WindowFixture&&inventoryTestStep==57&&c.actor_script.normal_return.valid){
                save(L"post24-return");inventoryTestStep=58;
            }else if(options.post24WindowFixture&&inventoryTestStep==58&&c.actor_script.messages==25&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post24-next-consumer");inventoryTestWait=phase+5;inventoryTestStep=59;
            }else if(options.post24WindowFixture&&inventoryTestStep==59&&phase>=inventoryTestWait){
                save(L"post24-held-next");inventoryTestStep=60;
                if(options.post25WindowFixture){
                    post25ExpectedFrame=c.actor_script.vm_current.frame_id;post25PriorReturnSequence=c.actor_script.farewell_return.sequence;
                    post25SourceEventBaseline=c.main_flow.events;
                    save(L"post25-before-ack");button(0);inventoryTestWait=phase+3;inventoryTestStep=61;
                }
            }else if(options.post25WindowFixture&&inventoryTestStep==61&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestStep=62;
            }else if(options.post25WindowFixture&&inventoryTestStep==62&&c.actor_script.message_acknowledgements==25&&c.actor_script.farewell_return.valid
                    &&c.actor_script.farewell_return.before.frame_id==post25ExpectedFrame
                    &&c.actor_script.farewell_return.sequence>post25PriorReturnSequence){
                save(L"post25-return");inventoryTestWait=phase+90;inventoryTestStep=63;
            }else if(options.post25WindowFixture&&inventoryTestStep==63&&c.actor_script.messages>25&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post25-next-consumer");button(Circle);inventoryTestWait=phase+5;inventoryTestStep=64;
            }else if(options.post25WindowFixture&&inventoryTestStep==64&&phase>=inventoryTestWait){
                save(L"post25-held-next");inventoryTestStep=65;
                if(options.post26WindowFixture){save(L"post26-before-ack");button(0);inventoryTestWait=phase+3;inventoryTestStep=79;}
             }else if(options.post25WindowFixture&&inventoryTestStep==63&&c.actor_script.vm_current.code_entry==0x8DDB5
                    &&c.actor_script.last_service==3&&!c.actor_script.awaiting_message&&!c.actor_script.waiting_for_event
                    &&c.main_flow.phase1C==0&&!c.main_flow.input_suppressed48){
                save(L"post25-main-loop");button(0);inventoryTestWait=phase+3;inventoryTestStep=66;
            }else if(options.post25WindowFixture&&inventoryTestStep==66&&phase>=inventoryTestWait){
                button(Triangle);inventoryTestWait=phase+90;inventoryTestStep=67;
            }else if(options.post25WindowFixture&&inventoryTestStep==67&&c.main_flow.events==post25SourceEventBaseline+1
                    &&c.main_flow.last_event==12&&c.main_flow.last_event_argument==1){
                post25TriangleEventObserved=true;save(L"post25-triangle-event");button(0);inventoryTestWait=phase+3;inventoryTestStep=72;
            }else if(options.post25WindowFixture&&inventoryTestStep==72&&phase>=inventoryTestWait&&!c.actor_script.vm_current.event_envelope_present){
                button(RTrigger);inventoryTestWait=phase+90;inventoryTestStep=73;
            }else if(options.post25WindowFixture&&inventoryTestStep==73&&c.main_flow.events==post25SourceEventBaseline+2
                    &&c.main_flow.last_event==12&&c.main_flow.last_event_argument==4){
                post25RTriggerEventObserved=true;save(L"post25-rtrigger-event");inventoryTestWait=phase+45;inventoryTestStep=74;
            }else if(options.post25WindowFixture&&inventoryTestStep==74&&c.returned_interaction.eligible){
                save(L"post25-returned-admitted");button(Circle,false);inventoryTestWait=phase+2;inventoryTestStep=75;
            }else if(options.post25WindowFixture&&inventoryTestStep==75&&phase>=inventoryTestWait){
                button(Circle,true);inventoryTestWait=phase+3;inventoryTestStep=76;
            }else if(options.post25WindowFixture&&inventoryTestStep==76&&phase>=inventoryTestWait){
                post25ReturnedHeldRefused=c.returned_interaction.posted==0&&!c.returned_interaction.armed;
                save(L"post25-returned-held-refused");button(0);inventoryTestWait=phase+3;inventoryTestStep=77;
            }else if(options.post25WindowFixture&&inventoryTestStep==77&&phase>=inventoryTestWait&&c.returned_interaction.armed){
                save(L"post25-returned-armed");button(Circle);inventoryTestWait=phase+60;inventoryTestStep=78;
            }else if(options.post25WindowFixture&&inventoryTestStep==78&&c.returned_interaction.posted==1){
                post25ReturnedPosted=true;save(L"post25-returned-posted");inventoryTestWait=phase+120;inventoryTestStep=68;
            }else if(options.post25WindowFixture&&inventoryTestStep==68&&c.actor_script.messages>25&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post25-next-consumer");button(Circle);inventoryTestWait=phase+5;inventoryTestStep=64;
            }else if(options.post25WindowFixture&&inventoryTestStep==68&&c.actor_script.awaiting_choice&&c.actor_choice.state==1){
                inventoryTestWait=phase+3;inventoryTestStep=69;
            }else if(options.post25WindowFixture&&inventoryTestStep==69&&phase>=inventoryTestWait){
                save(L"post25-next-choice");button(Circle);inventoryTestWait=phase+5;inventoryTestStep=71;
            }else if(options.post25WindowFixture&&inventoryTestStep==71&&phase>=inventoryTestWait){
                save(L"post25-held-choice");inventoryTestStep=70;
            }else if(options.post25WindowFixture&&(inventoryTestStep==67||inventoryTestStep==68||inventoryTestStep==73||inventoryTestStep==74||inventoryTestStep==78)&&phase>=inventoryTestWait){
                save(L"post25-triangle-observation");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
            }else if(options.post26WindowFixture&&inventoryTestStep==79&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                button(Circle);inventoryTestWait=phase+120;inventoryTestStep=80;
            }else if(options.post26WindowFixture&&inventoryTestStep==80&&c.actor_script.message_acknowledgements==26
                    &&!c.actor_script.awaiting_message&&c.actor_script.vm_current.current_pc==0x3F42){
                save(L"post26-deadline");inventoryTestStep=81;
            }else if(options.post26WindowFixture&&(inventoryTestStep==80||inventoryTestStep==81)
                    &&c.actor_script.messages>26&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post26-next-consumer");button(Circle);inventoryTestWait=phase+5;inventoryTestStep=82;
            }else if(options.post26WindowFixture&&inventoryTestStep==82&&phase>=inventoryTestWait){
                save(L"post26-held-next");button(0);inventoryTestWait=phase+3;inventoryTestStep=84;
            }else if(options.post26WindowFixture&&inventoryTestStep==84&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                save(L"post27-before-ack");button(Circle);inventoryTestWait=phase+150;inventoryTestStep=85;
            }else if(options.post26WindowFixture&&inventoryTestStep==85&&c.actor_script.messages>27&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post27-next-consumer");button(Circle);inventoryTestWait=phase+5;inventoryTestStep=86;
            }else if(options.post26WindowFixture&&inventoryTestStep==86&&phase>=inventoryTestWait){
                save(L"post27-held-next");inventoryTestStep=83;
            }else if(options.post26WindowFixture&&inventoryTestStep==85&&c.actor_script.awaiting_choice&&c.actor_choice.state==1){
                save(L"post27-choice-menu");button(Circle);inventoryTestWait=phase+3;inventoryTestStep=87;
            }else if(options.post26WindowFixture&&inventoryTestStep==87&&phase>=inventoryTestWait){
                post27ChoiceHeldRefused=c.actor_script.awaiting_choice&&c.actor_choice.confirmations==0;
                save(L"post27-choice-held");button(0);inventoryTestWait=phase+3;inventoryTestStep=88;
            }else if(options.post26WindowFixture&&inventoryTestStep==88&&phase>=inventoryTestWait){
                save(L"post27-choice-release");button(Circle);inventoryTestWait=phase+90;inventoryTestStep=89;
            }else if(options.post26WindowFixture&&inventoryTestStep==89&&c.actor_choice.state==2){
                post27ChoiceClosingObserved=true;save(L"post27-choice-closing");inventoryTestStep=90;
            }else if(options.post26WindowFixture&&(inventoryTestStep==89||inventoryTestStep==90)&&!c.actor_script.awaiting_choice&&c.actor_script.choice_completions==2){
                save(L"post27-choice-result");inventoryTestWait=phase+120;inventoryTestStep=91;
            }else if(options.post26WindowFixture&&inventoryTestStep==91&&c.actor_script.awaiting_input&&e.source_state==1&&e.focused){
                save(L"post27-input-open");button(Circle);inventoryTestWait=phase+3;inventoryTestStep=94;
            }else if(options.post26WindowFixture&&inventoryTestStep==94&&phase>=inventoryTestWait){
                post27InputHeldRefused=c.actor_script.awaiting_input&&c.actor_script.input_completions==0&&e.source_state==1;
                save(L"post27-input-held");CommitUnicode(L"shirt");inventoryTestWait=phase+3;inventoryTestStep=95;
            }else if(options.post26WindowFixture&&inventoryTestStep==95&&phase>=inventoryTestWait&&e.staged=="shirt"){
                save(L"post27-input-typed");command(GameTextKind::Confirm);inventoryTestWait=phase+120;inventoryTestStep=96;
            }else if(options.post26WindowFixture&&inventoryTestStep==96&&e.source_state==2){
                post27InputClosingObserved=true;save(L"post27-input-closing");inventoryTestStep=97;
            }else if(options.post26WindowFixture&&(inventoryTestStep==96||inventoryTestStep==97)&&c.actor_script.input_completions==1&&!c.actor_script.awaiting_input){
                save(L"post27-input-result");inventoryTestWait=phase+120;inventoryTestStep=98;
            }else if(options.post26WindowFixture&&inventoryTestStep==98&&c.actor_script.messages>27&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post27-input-next");button(Circle);inventoryTestWait=phase+5;inventoryTestStep=99;
            }else if(options.post26WindowFixture&&inventoryTestStep==99&&phase>=inventoryTestWait){
                save(L"post27-input-held-next");
                if(options.post28WindowFixture){save(L"post28-before-ack");button(0);inventoryTestWait=phase+3;inventoryTestStep=101;}
                else inventoryTestStep=100;
            }else if(options.post26WindowFixture&&inventoryTestStep>=94&&inventoryTestStep<=98&&phase>=inventoryTestWait+120){
                save(L"post27-input-first-observation");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
            }else if(options.post26WindowFixture&&inventoryTestStep==91&&c.actor_script.messages>27&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post27-choice-next");button(Circle);inventoryTestWait=phase+5;inventoryTestStep=92;
            }else if(options.post26WindowFixture&&inventoryTestStep==92&&phase>=inventoryTestWait){
                save(L"post27-choice-held-next");inventoryTestStep=93;
            }else if(options.post26WindowFixture&&(inventoryTestStep==89||inventoryTestStep==90||inventoryTestStep==91)&&phase>=inventoryTestWait){
                save(L"post27-choice-first-observation");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
            }else if(options.post26WindowFixture&&inventoryTestStep==85&&phase>=inventoryTestWait){
                save(L"post27-first-observation");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
            }else if(options.post26WindowFixture&&(inventoryTestStep==80||inventoryTestStep==81)&&phase>=inventoryTestWait){
                save(L"post26-first-observation");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
            }else if(options.post25WindowFixture&&inventoryTestStep==63&&phase>=inventoryTestWait){
                save(L"post25-first-observation");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
            }
            if(options.post28WindowFixture&&inventoryTestStep==101&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                save(L"post28-release");button(Circle);inventoryTestWait=phase+120;inventoryTestStep=102;
            }else if(options.post28WindowFixture&&inventoryTestStep==102&&c.actor_script.message_acknowledgements==28&&c.actor_script.awaiting_choice&&c.actor_choice.state==1){
                save(L"post28-choice-menu");button(Circle);inventoryTestWait=phase+3;inventoryTestStep=103;
            }else if(options.post28WindowFixture&&inventoryTestStep==103&&phase>=inventoryTestWait){
                post28ChoiceHeldRefused=c.actor_script.awaiting_choice&&c.actor_choice.confirmations==0;
                save(L"post28-choice-held");button(0);inventoryTestWait=phase+3;inventoryTestStep=104;
            }else if(options.post28WindowFixture&&inventoryTestStep==104&&phase>=inventoryTestWait){
                save(L"post28-choice-release");button(Circle);inventoryTestWait=phase+120;inventoryTestStep=105;
            }else if(options.post28WindowFixture&&inventoryTestStep==105&&c.actor_choice.state==2){
                post28ChoiceClosingObserved=true;save(L"post28-choice-closing");inventoryTestStep=106;
            }else if(options.post28WindowFixture&&(inventoryTestStep==105||inventoryTestStep==106)&&c.actor_script.choice_completions==3&&!c.actor_script.awaiting_choice){
                save(L"post28-choice-result");inventoryTestWait=phase+120;inventoryTestStep=107;
            }else if(options.post28WindowFixture&&inventoryTestStep==107&&c.actor_script.messages>28&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post28-next-consumer");button(Circle);inventoryTestWait=phase+5;inventoryTestStep=108;
            }else if(options.post28WindowFixture&&inventoryTestStep==108&&phase>=inventoryTestWait){
                post28NextHeldRefused=c.actor_script.message_acknowledgements==28&&c.actor_script.awaiting_message;
                save(L"post28-held-next");button(0);inventoryTestWait=phase+3;inventoryTestStep=110;
            }else if(options.post28WindowFixture&&inventoryTestStep==110&&phase>=inventoryTestWait&&c.actor_message_input_armed){
                save(L"post29-release");button(Circle);inventoryTestWait=phase+120;inventoryTestStep=111;
            }else if(options.post28WindowFixture&&inventoryTestStep==111&&c.actor_script.message_acknowledgements==29&&c.actor_script.awaiting_choice&&c.actor_choice.state==1){
                save(L"post29-choice-menu");button(Circle);inventoryTestWait=phase+3;inventoryTestStep=112;
            }else if(options.post28WindowFixture&&inventoryTestStep==112&&phase>=inventoryTestWait){
                post29ChoiceHeldRefused=c.actor_script.awaiting_choice&&c.actor_choice.confirmations==0;
                save(L"post29-choice-held");button(0);inventoryTestWait=phase+3;inventoryTestStep=113;
            }else if(options.post28WindowFixture&&inventoryTestStep==113&&phase>=inventoryTestWait){
                save(L"post29-choice-release");button(Circle);inventoryTestWait=phase+120;inventoryTestStep=114;
            }else if(options.post28WindowFixture&&inventoryTestStep==114&&c.actor_choice.state==2){
                post29ChoiceClosingObserved=true;save(L"post29-choice-closing");inventoryTestStep=115;
            }else if(options.post28WindowFixture&&(inventoryTestStep==114||inventoryTestStep==115)&&c.actor_script.choice_completions==4&&!c.actor_script.awaiting_choice){
                save(L"post29-choice-result");inventoryTestWait=phase+120;inventoryTestStep=116;
            }else if(options.post28WindowFixture&&inventoryTestStep==116&&c.actor_script.messages>29&&c.actor_script.awaiting_message&&c.actor_message_glyphs>0){
                save(L"post29-next-consumer");button(Circle);inventoryTestWait=phase+5;inventoryTestStep=117;
            }else if(options.post28WindowFixture&&inventoryTestStep==117&&phase>=inventoryTestWait){
                post29NextHeldRefused=c.actor_script.message_acknowledgements==29&&c.actor_script.awaiting_message;
                save(L"post29-held-next");inventoryTestStep=118;
            }else if(options.post28WindowFixture&&(inventoryTestStep==111||inventoryTestStep==114||inventoryTestStep==115||inventoryTestStep==116)&&phase>=inventoryTestWait){
                save(L"post29-first-observation");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
            }else if(options.post28WindowFixture&&(inventoryTestStep==102||inventoryTestStep==105||inventoryTestStep==106||inventoryTestStep==107)&&phase>=inventoryTestWait){
                save(L"post28-first-observation");storageBoundaryPassed=false;PostMessageW(window,WM_CLOSE,0,0);return;
            }
        }
        storageBoundaryPassed=softkeyStep==18&&profileNoObserved&&textCancelObserved&&textCloseObserved&&staleTextRefused&&playerCancelObserved
            &&birthdayBackObserved&&dayRejectedObserved&&title.scene3_completed&&title.state==3
            &&title.profile.present==7&&title.profile.applied&&title.profile.confirmed
            &&title.profile.gender==1&&title.profile.year==2000&&title.profile.month==2&&title.profile.day==29
            &&title.profile.blood==2&&title.profile.comment.size()==26&&c.next_scene==4&&c.current_scene==4
            &&c.actor_dictionary_ready&&c.actor_dictionary_bytes>0
            &&c.actor_script.started&&c.actor_script.dictionary.imported&&!c.actor_dictionary_live
            &&!c.actor_script.dictionary.source_live&&c.actor_script.root_functions_returned==1
            &&c.actor_script.array_query_calls>0&&c.actor_script.array_query_matches>0
            &&c.actor_script.dictionary_game_reads>0&&c.actor_script.dictionary_game_writes>0
            &&c.actor_script.random_selections>0
            &&(options.cameraWindowTest?
                (messageTestStep==6&&messageHeldRefused&&messageFocusRefused&&messageFreshAccepted
                 &&(options.conversationWindowFixture?
                    (c.actor_script.messages==15&&c.actor_script.pc==0x892E3&&c.actor_script.runtime.requested_command==38
                     &&c.actor_script.string_replace_calls==2&&c.actor_script.call_depth>0&&c.actor_message_contexts==15
                     &&conversationInputStage==3&&c.actor_message_input_armed):
                    (c.actor_message_contexts==2&&c.actor_message_input_armed))
                 &&!c.blocked&&c.status.code==GameFsCode::Ok&&c.actor_message_glyphs>0&&c.actor_message_ink_pixels>0
                 &&c.actor_script.awaiting_message&&(options.conversationWindowFixture?
                    c.actor_script.message_acknowledgements==14:(c.actor_script.messages==2&&c.actor_script.message_acknowledgements==1))
                 &&!c.actor_script.blocked&&!c.actor_script.runtime.command_blocked
                 &&c.actor_script.camera_setter_returns==1&&c.actor_script.camera_setter_result_word==0x10000000
                 &&c.actor_script.runtime.camera_completions==1&&c.actor_script.runtime.camera_manager_state28==0
                 &&c.actor_script.runtime.camera_fov_degrees==19.5f&&c.actor_script.runtime.actor_visible_pixels>100):
              options.actorPositionWindowTest?
                (c.actor_script.position_result_word==0x10000001u&&c.actor_script.runtime.position_queries>=2
                 &&c.actor_script.runtime.region_owners_live&&c.actor_script.runtime.region_counts==std::array<uint32_t,3>{{1,2,14}}
                 &&c.actor_script.runtime.position_result3C0&&c.actor_script.runtime.position_fallbacks==0
                 &&c.actor_script.runtime.position3B0==std::array<float,4>{{600,-300,0,1}}
                 &&c.actor_script.game28_queries>0&&c.actor_script.game28_result_word==0x10000000u
                 &&c.actor_script.runtime.asc176_complete&&c.actor_script.runtime.last_completed_command==178
                 &&c.actor_script.runtime.command_complete&&c.actor_script.runtime.angular_completions==2+c.actor_script.runtime.light_toggle_calls
                 &&c.actor_script.runtime.actor_visible_pixels>100&&c.actor_script.runtime.actor_draw_frames>100
                 &&c.actor_script.runtime.model_slots==std::count(c.actor_script.service_ids.begin(),c.actor_script.service_ids.end(),31u)
                 &&c.actor_script.event_signals>=3&&!c.actor_script.waiting_for_event&&!c.actor_script.runtime.command_blocked
                 &&c.actor_script.blocked&&c.actor_script.last_service==44&&c.actor_script.pc==0x297C
                 &&!c.actor_script.last_service_returned):
              options.sceneClockWindowTest?
                (c.actor_script.scene_period_queries>0&&c.actor_script.runtime.scene_clock_ready
                 &&c.actor_script.runtime.season_controls==7&&c.actor_script.runtime.scene_light_nodes==4
                 &&c.actor_script.runtime.scene_light_color_writes>10
                 &&c.actor_script.runtime.scene_light_scalar_writes>0&&c.actor_script.runtime.scene_light_vector_writes>0
                 &&c.actor_script.runtime.asc176_ready&&c.actor_script.runtime.asc176_complete
                 &&c.actor_script.runtime.first_completed_commands[0]==176
                 &&c.actor_script.runtime.last_completed_command==157&&c.actor_script.runtime.command_complete
                 &&c.actor_script.runtime.movement_completions>0&&!c.actor_script.runtime.movement_pending
                 &&c.actor_script.runtime.actor_root==std::array<float,4>{{600,-300,0,1}}
                 &&c.actor_script.runtime.actor_visible_pixels>100&&c.actor_script.runtime.actor_draw_frames>100
                 &&c.actor_script.runtime.model_slots==std::count(c.actor_script.service_ids.begin(),c.actor_script.service_ids.end(),31u)
                 &&c.actor_script.event_signals>=2&&!c.actor_script.waiting_for_event
                 &&!c.actor_script.runtime.command_blocked&&!c.actor_script.runtime.actor_request_pending
                 &&c.actor_script.blocked&&c.actor_script.last_service==19&&c.actor_script.pc==0x8D8D0):
              options.actorChannelWindowTest?
                (c.actor_script.runtime.first_channel_motion==8&&c.actor_script.runtime.channel_admissions>=2
                 &&c.actor_script.runtime.actor_draw_frames>=60&&c.actor_script.runtime.actor_pixel_writes>1000
                 &&c.actor_script.runtime.motion_samples>=60&&c.actor_script.runtime.command_pc>20
                 &&c.actor_script.runtime.angular_completions>0
                 &&c.actor_script.runtime.actor_model_live&&c.actor_script.runtime.model_slots>0
                 &&c.actor_script.runtime.model_slots==std::count(c.actor_script.service_ids.begin(),c.actor_script.service_ids.end(),31u)
                 &&c.actor_script.runtime.scene_controls==14&&c.actor_script.runtime.scene_pair3_calls==1
                 &&c.actor_script.runtime.motion123_completions==1&&c.actor_script.runtime.asc176_ready&&c.actor_script.runtime.asc176_complete
                 &&c.actor_script.runtime.command_effects==11&&c.actor_script.runtime.command_pc==108
                 &&!c.actor_script.runtime.command_blocked&&c.actor_script.runtime.command_complete
                 &&c.actor_script.event_signals==1&&!c.actor_script.waiting_for_event&&c.actor_script.blocked
                 &&c.actor_script.last_service==36&&c.actor_script.pc==0x8D7FB):
              options.gameLoopWindowTest?
                (c.actor_script.control_blocks_entered>0&&c.actor_script.switches>0
                 &&c.actor_script.instructions>20000&&c.actor_script.pc!=0x8DDB7
                 &&c.actor_script.runtime.live&&c.actor_script.runtime.camera_follows_actor
                 &&c.actor_script.runtime.model_slots==7&&c.actor_script.runtime.model_frames>=45
                 &&c.actor_script.runtime.transition_requests==1&&c.actor_script.runtime.visible_pixels>1000
                 &&c.actor_script.waiting_for_event&&c.actor_script.event_signals==0&&c.actor_script.pc==0x8DE3D
                 &&c.actor_script.runtime.actor_model_live&&!c.actor_script.runtime.actor_request_pending
                 &&!c.actor_script.runtime.selected_extension_pending&&c.actor_script.runtime.requested_command==176
                 &&c.actor_script.runtime.command_blocked&&c.actor_script.runtime.command_pc==20
                 &&c.actor_script.runtime.command_effects==2&&c.actor_script.runtime.actor_math_mode==1
                 &&c.actor_script.runtime.pending_logical_motion==9
                 &&c.actor_script.runtime.logical_xy==std::array<float,2>{{1830,-27}}
                 &&c.actor_script.runtime.event_requests==1&&!c.actor_script.blocked&&c.blocked&&c.status.code==GameFsCode::Unavailable):
                (c.actor_script.last_opcode==0x18&&c.actor_script.pc==0x8DDB7&&c.blocked&&c.status.code==GameFsCode::Unavailable));
        if(options.inventoryWindowFixture)storageBoundaryPassed=inventoryTestStep==12&&inventoryFilterEmpty&&inventoryCancelObserved&&inventoryPendingSaved&&inventoryClosingSaved
            &&c.inventory.phase==7&&!c.inventory.live&&!c.inventory.owner480&&!c.inventory.request14&&c.inventory.stateB2C==3
            &&c.inventory.loader468==1&&c.inventory.loader469==1&&c.inventory.loader498==0&&c.inventory.retirements==1
            &&c.inventory.action_cancels==1&&c.inventory.action_frontier==0&&c.inventory.glyphs>0
            &&c.actor_script.inventory_completions==1&&c.actor_script.context_enabled&&c.actor_script.callbacks_returned==1
            &&c.actor_script.inventory_setter_result_word==0x10000000&&c.actor_script.message_acknowledgements==15
            &&c.actor_script.blocked&&!c.actor_script.last_service_returned&&c.actor_script.last_service==71&&c.actor_script.pc==0xA1FE;
        if(options.game71WindowFixture)storageBoundaryPassed=inventoryTestStep==14&&inventoryFilterEmpty&&(options.inventoryActionWindowFixture?inventoryActionPassed:inventoryCancelObserved)
            &&c.inventory.phase==7&&c.inventory.retirements==1&&c.actor_script.inventory_completions==1
            &&c.actor_script.game71_calls==1&&c.actor_script.game71_result_word==0x10000000&&c.actor_script.records.live
            &&c.actor_script.records.writes==1&&c.actor_script.records.bank0[0].value==12&&c.actor_script.record_cell0_writes==1
            &&c.actor_script.context_enabled&&c.actor_script.callbacks_returned==(options.inventoryActionWindowFixture?3u:1u)&&c.actor_script.messages==16
            &&c.actor_script.message_acknowledgements==16&&!c.actor_script.awaiting_message&&!c.actor_script.blocked
            &&c.actor_script.game3_calls==1&&c.actor_script.game3_result_word==0x10000000&&c.actor_script.runtime.manager_request9
            &&c.actor_script.runtime.camera_completions==2&&c.actor_script.runtime.camera_transition_mode1C==0
            &&c.actor_script.runtime.camera_fov_degrees==30&&c.actor_script.last_service==28&&c.status.code==GameFsCode::Ok;
        if(options.inventoryActionWindowFixture){
            const auto& action=c.actor_script.inventory_action;
            storageBoundaryPassed=inventoryActionPassed&&inventoryTestStep==12&&inventoryFilterEmpty&&inventoryPendingSaved&&inventoryClosingSaved
                &&c.inventory.phase==7&&!c.inventory.live&&!c.inventory.owner480&&!c.inventory.request14&&!c.inventory.retirement_pending
                &&c.inventory.retirements==1&&c.inventory.action_events==2&&c.inventory.action_completions==2&&!c.inventory.action_pending
                &&c.actor_script.inventory_completions==1&&c.actor_script.context_enabled&&c.actor_script.callbacks_returned==3
                &&action.completed&&!action.failed&&!action.pending&&action.event==14&&action.receipt>itemMarkReceipt&&itemMarkReceipt>0
                &&int32_t(action.row)==itemActionRow&&action.item_id==itemActionId&&action.before_words[0]==0x10000001&&action.after_words[0]==0x10000000
                &&c.actor_script.awaiting_message&&c.actor_script.messages==16&&c.actor_script.message_acknowledgements==15&&c.actor_message_glyphs>0
                &&!c.actor_script.blocked&&!c.blocked&&bool(c.status);
        }
        if(options.decorateWindowFixture){
            const auto& action=c.actor_script.inventory_action;const auto& g=c.inventory;
            const auto slot=int32_t(action.after_words[1]&0x0FFFFFFF);const auto& room=c.actor_script.runtime;
            storageBoundaryPassed=decorationPassed&&inventoryActionPassed&&inventoryTestStep==12&&inventoryFilterEmpty&&inventoryPendingSaved&&inventoryClosingSaved
                &&g.phase==7&&!g.live&&!g.request14&&!g.owner480&&!g.retirement_pending&&g.retirements==1&&g.action_events==1&&g.action_completions==1&&!g.action_pending
                &&action.completed&&!action.failed&&action.event==13&&action.receipt==itemMarkReceipt&&action.before_words[1]==0&&action.after_words[0]==0x10000000u
                &&int32_t(action.row)==itemActionRow&&action.item_id==itemActionId&&slot>=0&&size_t(slot)<room.room_models.size()
                &&room.room_models[size_t(slot)].active&&!room.room_models[size_t(slot)].retirement&&room.room_models[size_t(slot)].resource==itemActionId
                &&room.model_publications==decoratePublicationsBefore+1&&c.actor_script.inventory_completions==1&&c.actor_script.callbacks_returned==2
                &&c.actor_script.context_enabled&&c.actor_script.awaiting_message&&c.actor_script.messages==16&&c.actor_script.message_acknowledgements==15
                &&c.actor_message_glyphs>0&&!c.actor_script.blocked&&!c.blocked&&bool(c.status);
        }
        if(options.putawayWindowFixture){
            const auto& a=c.actor_script.inventory_action;const auto& g=c.inventory;const auto& room=c.actor_script.runtime;
            storageBoundaryPassed=modelRemovalPassed&&decorationPassed&&inventoryActionPassed&&inventoryTestStep==12&&inventoryPendingSaved&&inventoryClosingSaved
                &&g.phase==7&&!g.live&&!g.request14&&!g.owner480&&!g.retirement_pending&&g.retirements==1&&g.action_events==2&&g.action_completions==2&&!g.action_pending
                &&a.completed&&!a.failed&&a.event==14&&a.receipt==putawayReceipt&&a.receipt>itemMarkReceipt&&a.row==uint32_t(itemActionRow)&&a.item_id==itemActionId
                &&a.before_words[1]==(0x20000000u|uint32_t(putawaySlot))&&a.after_words[0]==0x10000000u&&a.after_words[1]==0&&a.after_words[2]==0
                &&!room.room_models[size_t(putawaySlot)].active&&room.room_models[size_t(putawaySlot)].generation==putawayGeneration
                &&c.actor_script.inventory_completions==1&&c.actor_script.callbacks_returned==3&&c.actor_script.context_enabled&&c.actor_script.awaiting_message
                &&c.actor_script.messages==16&&c.actor_script.message_acknowledgements==15&&c.actor_message_glyphs>0&&!c.actor_script.blocked&&!c.blocked&&bool(c.status);
        }
        if(options.discardWindowFixture){
            const auto& a=c.actor_script.inventory_action;const auto& g=c.inventory;const auto& room=c.actor_script.runtime;
            storageBoundaryPassed=discardPassed&&discardCancelPassed&&discardHeldPassed&&modelRemovalPassed&&decorationPassed&&inventoryActionPassed&&inventoryTestStep==12
                &&g.phase==7&&!g.live&&!g.owner480&&!g.request14&&!g.retirement_pending&&g.retirements==1&&g.action_events==2&&g.action_completions==2&&!g.action_pending
                &&g.discard_prompts==2&&g.discard_cancelled==1&&g.discard_accepted==1&&g.discard_result==1&&!g.items[size_t(itemActionRow)].valid&&g.items.size()==170
                &&a.completed&&!a.failed&&a.event==16&&a.receipt==putawayReceipt&&a.receipt>itemMarkReceipt&&a.row==uint32_t(itemActionRow)&&a.item_id==itemActionId&&a.row_cells==10
                &&!room.room_models[size_t(putawaySlot)].active&&room.room_models[size_t(putawaySlot)].generation==putawayGeneration
                &&c.actor_script.inventory_completions==1&&c.actor_script.callbacks_returned==3&&c.actor_script.context_enabled&&c.actor_script.awaiting_message
                &&c.actor_script.messages==16&&c.actor_script.message_acknowledgements==15&&c.actor_message_glyphs>0&&!c.actor_script.blocked&&!c.blocked&&bool(c.status);
        }
        if(options.manager9WindowFixture)storageBoundaryPassed=inventoryTestStep==21&&managerHeldRefused&&managerFocusRefused&&managerNextHeldRefused
            &&c.source_loader469&&c.main_flow.phase5_entries==1&&c.main_flow.request9_consumed==1&&c.main_flow.outer_returns==1
            &&c.main_flow.phase1C==0&&c.main_flow.return20==0&&c.main_flow.substate24==0&&c.main_flow.circle_source_blocks>0&&c.main_flow.events==0
            &&c.inventory.phase==7&&c.inventory.retirements==1&&!c.inventory.owner480&&!c.inventory.request14&&c.inventory.loader469==1
            &&c.post_modal.source_gate_blocked&&c.post_modal.fresh_input&&c.post_modal.posted==1&&c.post_modal.callback_returned
            &&c.post_modal.context_word==0x80000000&&c.post_modal.callback_word==0x7008DA28&&c.post_modal.error.empty()
            &&c.actor_script.owner_event_calls==1&&c.actor_script.owner_event_result==0&&c.actor_script.callbacks_returned==2
            &&c.actor_script.messages==17&&c.actor_script.message_acknowledgements==16&&c.actor_script.awaiting_message&&!c.actor_script.blocked
            &&c.actor_script.last_opcode==0x48&&c.actor_script.sound73_requests==1&&c.actor_script.sound73_playback_result==-1
            &&!c.actor_script.runtime.manager_request9&&c.actor_script.runtime.camera_completions>=2&&c.status.code==GameFsCode::Ok;

        if(options.post17WindowFixture)storageBoundaryPassed=inventoryTestStep==35&&post17HeldRefused&&choiceHeldRefused&&choiceCancelRefused&&choiceClosingObserved&&postChoiceHeldRefused
            &&managerHeldRefused&&managerFocusRefused&&managerNextHeldRefused&&c.source_loader469&&c.main_flow.phase1C==0&&c.main_flow.circle_source_blocks>0&&c.main_flow.events==0
            &&c.main_flow.request9_consumed==1&&c.main_flow.outer_returns==1&&c.inventory.phase==7&&c.inventory.retirements==1
            &&c.post_modal.posted==1&&c.post_modal.callback_returned&&c.actor_script.owner_event_calls==1&&c.actor_script.callbacks_returned==2
            &&c.actor_script.choice_requests==1&&c.actor_script.choice_completions==1&&c.actor_script.choice_result_word==0x20000002
            &&c.actor_choice.state==3&&c.actor_choice.row==1&&c.actor_choice.confirmations==1&&!c.actor_choice.timed_out
            &&c.actor_script.messages==19&&c.actor_script.message_acknowledgements==18&&c.actor_script.awaiting_message&&!c.actor_script.awaiting_choice&&!c.actor_script.blocked
            &&c.actor_script.pc!=c.actor_script.message_pcs.at(17)&&uint32_t(c.actor_script.runtime.requested_command)!=c.actor_script.message_actor_commands.at(17)&&c.actor_script.last_opcode==0x48
            &&c.actor_script.context_word==0x80000000&&c.actor_script.call_depth>0&&c.actor_message_glyphs>0&&c.actor_message_ink_pixels>0
            &&c.status.code==GameFsCode::Ok;

        if(options.post19WindowFixture)storageBoundaryPassed=inventoryTestStep==48&&postChoiceHeldRefused&&post19HeldRefused&&post20HeldRefused&&post21HeldRefused&&secondChoiceHeldRefused&&secondChoiceClosingObserved&&reply22HeldRefused
            &&c.source_loader469&&c.main_flow.phase1C==0&&c.main_flow.circle_source_blocks>0&&c.main_flow.events==0
            &&c.main_flow.request9_consumed==1&&c.main_flow.outer_returns==1&&c.inventory.phase==7&&c.inventory.retirements==1
            &&c.post_modal.posted==1&&c.post_modal.callback_returned&&c.actor_script.owner_event_calls==1&&c.actor_script.callbacks_returned==2
            &&c.actor_script.choice_requests==2&&c.actor_script.choice_completions==2&&c.actor_script.choice_result_word==0x20000001
            &&c.actor_choice.state==3&&c.actor_choice.row==0&&c.actor_choice.confirmations==1&&!c.actor_choice.timed_out
            &&c.actor_script.messages==22&&c.actor_script.message_acknowledgements==21&&c.actor_script.awaiting_message&&!c.actor_script.awaiting_choice&&!c.actor_script.blocked
            &&c.actor_script.pc!=c.actor_script.message_pcs.at(20)&&c.actor_script.last_opcode==0x48
            &&c.actor_script.context_word==0x80000000&&c.actor_script.call_depth>0&&c.actor_message_glyphs>0&&c.actor_message_ink_pixels>0&&c.status.code==GameFsCode::Ok;
        if(options.post19WindowFixture&&post19LinearRoute&&!options.post21WindowFixture)storageBoundaryPassed=inventoryTestStep==48&&postChoiceHeldRefused&&post19HeldRefused&&post20HeldRefused
            &&c.source_loader469&&c.main_flow.phase1C==0&&c.main_flow.circle_source_blocks>0&&c.main_flow.events==0
            &&c.main_flow.request9_consumed==1&&c.main_flow.outer_returns==1&&c.inventory.phase==7&&c.inventory.retirements==1
            &&c.post_modal.posted==1&&c.post_modal.callback_returned&&c.actor_script.owner_event_calls==1&&c.actor_script.callbacks_returned==2
            &&c.actor_script.choice_requests==0&&c.actor_script.choice_completions==0&&!c.actor_script.awaiting_choice
            &&c.actor_script.messages==21&&c.actor_script.message_acknowledgements==20&&c.actor_script.awaiting_message&&!c.actor_script.blocked
            &&c.actor_script.pc!=c.actor_script.message_pcs.at(18)&&c.actor_script.last_opcode==0x48
            &&c.actor_script.context_word==0x80000000&&c.actor_script.call_depth>0&&c.actor_message_glyphs>0&&c.actor_message_ink_pixels>0&&c.status.code==GameFsCode::Ok;

        if(options.post21WindowFixture)storageBoundaryPassed=inventoryTestStep==55&&!post21BranchMismatch&&post23HeldRefused&&post24HeldRefused&&post20HeldRefused&&post21HeldRefused&&secondChoiceHeldRefused&&secondChoiceClosingObserved&&reply22HeldRefused
            &&c.source_loader469&&c.main_flow.phase1C==0&&c.main_flow.events==0&&c.post_modal.posted==1&&c.post_modal.callback_returned
            &&c.actor_script.owner_event_calls==1&&c.actor_script.callbacks_returned==2&&c.actor_script.context_word==0x80000000
            &&c.actor_script.choice_requests>0&&c.actor_script.choice_requests==c.actor_script.choice_completions&&c.actor_script.choice_result_word==0x20000001
            &&c.actor_choice.state==3&&c.actor_choice.row==0&&c.actor_choice.confirmations==1&&!c.actor_choice.timed_out
            &&c.actor_script.messages==24&&c.actor_script.message_acknowledgements==23&&c.actor_script.awaiting_message&&!c.actor_script.awaiting_choice&&!c.actor_script.blocked
            &&c.actor_script.call_depth>0&&c.actor_message_glyphs>0&&c.actor_message_ink_pixels>0&&c.status.code==GameFsCode::Ok;

        if(options.post24WindowFixture)storageBoundaryPassed=inventoryTestStep==60&&c.actor_script.normal_return.valid&&c.actor_script.normal_return.opcode_pc==0x85438
            &&c.actor_script.normal_return.before.result.word==0x10000001&&c.actor_script.normal_return.after.result.word==0x10000001
            &&c.actor_script.messages==25&&c.actor_script.message_acknowledgements==24&&c.actor_script.awaiting_message&&!c.actor_script.blocked
            &&c.source_loader469&&c.main_flow.events==0&&c.post_modal.posted==1&&c.actor_script.callbacks_returned==2&&c.status.code==GameFsCode::Ok;

        if(options.post25WindowFixture)storageBoundaryPassed=(inventoryTestStep==65||inventoryTestStep==70)&&post25TriangleEventObserved
            &&c.actor_script.farewell_return.valid&&c.actor_script.farewell_return.before.frame_id==post25ExpectedFrame
            &&c.actor_script.farewell_return.sequence>post25PriorReturnSequence
            &&c.actor_script.farewell_return.opcode_pc==0x7D628&&c.actor_script.farewell_return.after.current_pc==0x89A21
            &&c.actor_script.message_acknowledgements==25&&!c.actor_script.blocked
            &&((inventoryTestStep==65&&c.actor_script.messages>25&&c.actor_script.awaiting_message)
               ||(inventoryTestStep==70&&c.actor_script.awaiting_choice&&c.actor_choice.state==1))
            &&post25ReturnedHeldRefused&&post25ReturnedPosted&&c.returned_interaction.posted==1&&c.returned_interaction.queue_result==0
            &&post25RTriggerEventObserved&&c.source_loader469&&c.main_flow.events==post25SourceEventBaseline+2&&c.main_flow.last_event==12
            &&c.main_flow.last_event_argument==4&&c.post_modal.posted==1&&c.status.code==GameFsCode::Ok;

        if(options.post26WindowFixture)storageBoundaryPassed=inventoryTestStep==100&&post27ChoiceHeldRefused&&post27ChoiceClosingObserved&&post27InputHeldRefused&&post27InputClosingObserved&&c.actor_script.messages>27
            &&c.actor_script.message_acknowledgements==27&&c.actor_script.awaiting_message&&!c.actor_script.blocked
            &&c.actor_script.input_completions==1&&c.actor_script.input_text=="shirt"&&c.actor_script.input_auxiliary=="SHIRT"&&!c.actor_script.input_timed_out
            &&c.returned_interaction.posted==1&&c.returned_interaction.event_posted&&!c.returned_interaction.eligible
            &&c.post_modal.posted==1&&c.source_loader469&&c.actor_script.callbacks_posted==5&&c.actor_script.callbacks_returned==5
            &&c.status.code==GameFsCode::Ok;

        if(options.post28WindowFixture)storageBoundaryPassed=inventoryTestStep==118&&post28ChoiceHeldRefused&&post28ChoiceClosingObserved&&post28NextHeldRefused
            &&post29ChoiceHeldRefused&&post29ChoiceClosingObserved&&post29NextHeldRefused
            &&c.actor_script.message_acknowledgements==29&&c.actor_script.messages>29&&c.actor_script.awaiting_message&&!c.actor_script.blocked
            &&c.actor_script.choice_completions==4&&c.actor_script.choice_result_word==0x20000001
            &&c.actor_script.d5_calls>0&&c.actor_script.d5_first_pc==0x156E&&c.actor_script.d5_first_left_word==0x20000020
            &&c.actor_script.d5_first_right_word==0x20000004&&c.actor_script.d5_first_result_word==0x20000024
            &&c.actor_script.word_consumer_row.valid&&c.actor_script.word_consumer_row.row==163&&c.actor_script.word_consumer_row.fields[12].word==0x20000084
            &&c.actor_script.input_completions==1&&c.actor_script.input_text=="shirt"&&c.actor_script.input_auxiliary=="SHIRT"
            &&c.returned_interaction.posted==1&&c.returned_interaction.event_posted&&!c.returned_interaction.eligible
            &&c.post_modal.posted==1&&c.source_loader469&&c.actor_script.callbacks_posted==5&&c.actor_script.callbacks_returned==5
            &&c.status.code==GameFsCode::Ok;

    }
    void StartupProbe() {
        messageVisible=false;
        if(!program.loaded()) {status=L"NEKO.KSC не загружен.";return;}
        // Explicit probe context, not a guessed value for a complete game start.
        auto trace=dokodemo::startup::run(program,0,10000);
        startupPrefixVerified=VerifiedStartupPrefix(trace,"NEKO");
        status=L"Проба исходного старта (host byte = 0): " + std::to_wstring(trace.executed) + L" команд; " + Utf8(trace.diagnostic.text()) + L". Реальное начальное значение host-поля ещё не доказано.";
        std::filesystem::create_directories(options.evidence);
        std::ofstream report(options.evidence / L"startup-prefix.json",std::ios::binary);
        report << "{\"probe\":true,\"host_byte\":0,\"executed\":" << trace.executed << ",\"pc\":" << trace.pc << ",\"sp\":" << trace.sp << ",\"fp\":" << trace.fp
               << ",\"opcode\":" << trace.diagnostic.opcode << ",\"first_host_service\":" << trace.first_builtin.host_service
               << ",\"global_writes\":" << trace.context.global_writes.size() << ",\"result_stack_reference\":" << trace.result.index
               << ",\"local_writes\":" << trace.context.local_writes.size() << ",\"host_reference_bindings\":" << trace.context.host_reference_bindings.size()
               << ",\"host_abi_projection\":false"
               << ",\"host_initialization_observed\":false,\"full_game\":false}\n";
    }
    void RunScript() {
        messageVisible=false;
        if (!vm) { status = L"Оригинальный NEKO.KSC отсутствует или не прошёл проверку."; return; }
        // The original caller's input slots are unknown; no invented values are supplied.
        auto event = vm->start();
        if (event.status == dokodemo::script::Status::Running) event = vm->run(1000);
        if (event.status == dokodemo::script::Status::Stopped) status = L"Исходный старт остановлен: " + Utf8(event.diagnostic.text());
        else if (event.status == dokodemo::script::Status::Message) status = L"Подтверждённая команда сообщения; графический контекст сцены ещё не восстановлен.";
        else status = L"KSC достиг лимита исполнения; продолжение требует проверки контекста.";
        std::filesystem::create_directories(options.evidence);
        std::ofstream(options.evidence / L"startup-diagnostic.txt", std::ios::binary) << Utf8(status) << '\n';
    }
    void Service47Probe() {
        messageVisible=false;
        if(!program.loaded()) {status=L"NEKO.KSC не загружен.";return;}
        dokodemo::startup::StartupProbe probe(program,0,512,{true,true,true,true,true,0},ConditionalService47Fixture());
        const auto prefix=probe.run(6192);
        service47Verified=VerifiedService47Startup(prefix,"NEKO",program);
        const auto trace=RunToBoundary(probe);
        bulkVerified=VerifiedBulkStartup(trace,"NEKO",0);
        status=L"Условная проба с тестовым buffer: "+std::to_wstring(trace.executed)
            +L" команд, service47 и builtin97 выполнены; следующий барьер service48. Исходный buffer не подтверждён.";
        std::filesystem::create_directories(options.evidence);
        const auto& state47=prefix.context.service47;
        std::ofstream report(options.evidence/L"service47-probe.json",std::ios::binary);
        report<<"{\"conditional_probe\":true,\"host_byte\":0,\"executed\":"<<prefix.executed
            <<",\"pc\":"<<prefix.pc<<",\"sp\":"<<prefix.sp<<",\"fp\":"<<prefix.fp
            <<",\"result_raw\":"<<prefix.result.word<<",\"service47_calls\":"<<state47.calls
            <<",\"cursor_bytes\":"<<state47.cursor_bytes<<",\"decoded_words\":"<<state47.decoded_words
            <<",\"guard\":"<<unsigned(state47.guard)<<",\"state_u16\":"<<state47.state
            <<",\"globals\":"<<prefix.context.global_writes.size()<<",\"locals\":"<<prefix.context.local_writes.size()
            <<",\"bindings\":"<<prefix.context.host_reference_bindings.size()
            <<",\"verified\":"<<(service47Verified?"true":"false")
            <<",\"actual_buffer_available\":false,\"host_initialization_observed\":false,\"full_game\":false}\n";
        std::ofstream full(options.evidence/L"bulk-startup-probe.json",std::ios::binary);
        full<<"{\"conditional_probe\":true,\"host_byte\":0,\"executed\":"<<trace.executed
            <<",\"pc\":"<<trace.pc<<",\"sp\":"<<trace.sp<<",\"fp\":"<<trace.fp
            <<",\"result_raw\":"<<trace.result.word<<",\"globals\":"<<trace.context.global_writes.size()
            <<",\"locals\":"<<trace.context.local_writes.size()<<",\"bindings\":"<<trace.context.host_reference_bindings.size()
            <<",\"next_service\":48,\"verified\":"<<(bulkVerified?"true":"false")
            <<",\"actual_buffer_available\":false,\"full_game\":false}\n";
    }
    void ManagerProbe() {
        messageVisible=false;
        auto extended=ConstructActualConstructionPrefix(program,0);
        auto& initialized=extended.initialized;
        auto& built=initialized.prefix;
        managerVerified=extended.verified;
        if(!built.manager||!built.callbackOwner||!initialized.producer) {
            status=L"Для условного создания VM нужен проверенный NEKO.KSC и живой owner.";
            return;
        }
        managerQueue=std::move(built.manager); // Keep real owned storage alive in the application.
        callbackOwner=std::move(built.callbackOwner);
        producer=std::move(initialized.producer);
        status=managerVerified
            ?L"Путь VM: producer и callback выполнены; внешний запуск ещё не замкнут."
            :L"Путь VM остановлен на проверке; подробности в manager-queue-probe.json.";
        std::filesystem::create_directories(options.evidence);
        std::ofstream report(options.evidence/L"manager-queue-probe.json",std::ios::binary);
        report<<"{\"native_owned_storage\":true,\"allocated_bytes\":"<<built.snapshot.allocated_bytes
            <<",\"occupied_entries\":"<<built.snapshot.registries.at(0).occupied
            <<",\"commands_processed\":"<<built.drain.commands_processed
            <<",\"queue_cursor_bytes\":"<<built.snapshot.queue_cursor_bytes
            <<",\"boundary_pc\":"<<built.drain.boundary.source_pc
            <<",\"callback_symbol\":"<<built.drain.boundary.descriptor.function
            <<",\"terminal\":"<<(built.snapshot.terminal?"true":"false")
            <<",\"callback_owner_bytes\":260,\"callback_owner_type\":\"CKingKai96\""
            <<",\"owner_base_projection_distinct\":true,\"owner_verified\":"<<(built.ownerVerified?"true":"false")
            <<",\"initialization_call_pc\":"<<built.initializationCut.source_pc
            <<",\"initialization_callee\":"<<built.initializationCut.callee
            <<",\"initialization_subcall_closed\":"<<(initialized.initialization.completed?"true":"false")
            <<",\"producer_payload_bytes\":108,\"producer_live_allocations\":"<<initialized.snapshot.live_blocks.size()
            <<",\"producer_source_input_explicit\":true,\"allocator_return_explicit\":true"
            <<",\"conditional_owner_checkpoint\":false,\"owner_remaining_initialization_unknown\":true"
            <<",\"owner_budget_initialized\":false"
            <<",\"suffix_entry_pc\":"<<uint32_t{0x23194}
            <<",\"constructor_entry_reached\":"<<(extended.entry.reached_constructor_entry?"true":"false")
            <<",\"constructor_boundary_pc\":"<<extended.constructor.boundary.source_pc
            <<",\"constructor_callback_pointer\":"<<extended.constructor.callback_pointer
            <<",\"source_loader_returned\":"<<(extended.constructor.loader_completed?"true":"false")
            <<",\"actual_callback_completed\":"<<(extended.callback.completed?"true":"false")
            <<",\"producer_callback_adopted\":"<<(extended.callback.producer_adopted?"true":"false")
            <<",\"callback_return_pc\":"<<extended.callback.return_pc
            <<",\"callback_host_address\":"<<extended.callback.node
            <<",\"callback_return_residue\":"<<extended.callback.return_residue
            <<",\"whole_constructor_returned\":false"
            <<",\"manual_heap_and_loader_globals_premise\":true"
            <<",\"native_global_initialized_bytes\":80,\"source_global_known_initial_bytes\":44,\"source_global_known_after_loader_bytes\":46"
            <<",\"source_loader_unknown_preimage_bytes\":2,\"initial_masks_identical\":false"
            <<",\"source_prior_storage_index\":123,\"native_prior_storage_index\":"<<uint32_t{0xFFFFFFFF}
            <<",\"storage_index_dead_preimage_refinement\":true"
            <<",\"static_call_boundary_composition\":true,\"native_global_clock_measured\":false"
            <<",\"actual_CKingKai_caller_receiver_created\":false,\"CURRENT_ECB_header_established\":false"
            <<",\"preexisting_storage_origin_unverified\":true"
            <<",\"shared_image_bytes\":"<<(extended.snapshot.file.image?extended.snapshot.file.image->bytes.size():0)
            <<",\"source_vm_address\":"<<extended.snapshot.source_vm_address
            <<",\"CLoadMan_checkpoint_explicit\":true,\"actual_CLoadMan_created\":false"
            <<",\"kernel_return_observed\":false"
            <<",\"verified\":"<<(managerVerified?"true":"false")
            <<",\"actual_psp_manager_available\":false,\"host_flags_enabled\":false,\"full_game\":false}\n";
    }
    void StorageProbe() {
        messageVisible=false;
        if(!program.loaded()) {status=L"NEKO.KSC не загружен.";return;}
        std::filesystem::create_directories(options.evidence);
        std::ofstream report(options.evidence/L"owned-storage-probe.json",std::ios::binary);
        report<<"{\"conditional_sources\":true,\"actual_producers_observed\":false,\"full_game\":false,\"routes\":[";
        storageVerified=true;
        for(uint8_t host:{uint8_t(0),uint8_t(1)}) {
            const auto record=host?dokodemo::startup::Service48Projection{}:ConditionalService48Projection();
            storageProbes[host]=std::make_unique<dokodemo::startup::StartupProbe>(program,host,uint16_t{512},
                dokodemo::startup::HostAbiProjection{true,true,true,true,true,0},
                ConditionalService47Fixture(),ConditionalStringFixture(),ConditionalStringGetters(),record);
            // Retain the actual arenas in the application at the next ABI cut;
            // report summaries do not stand in for owned live string storage.
            const auto trace=RunToBoundary(*storageProbes[host]);
            const bool ok=VerifiedStorageStartup(trace,"NEKO",host,program);
            storageVerified=storageVerified&&ok;
            if(host) report<<',';
            const auto& context=trace.context;
            const auto& heap=host?context.string_arena:context.general_arena;
            const auto& bytes=program.bytes();
            const size_t commandOffset=28u+trace.pc;
            const uint8_t nextId=bytes.at(commandOffset+1),nextArgc=bytes.at(commandOffset+2);
            report<<"{\"host\":"<<unsigned(host)<<",\"executed\":"<<trace.executed
                <<",\"pc\":"<<trace.pc<<",\"sp\":"<<trace.sp<<",\"fp\":"<<trace.fp
                <<",\"result_raw\":"<<trace.result.word<<",\"globals\":"<<context.global_writes.size()
                <<",\"globals_sha256\":"<<JsonString(context.global_writes_sha256)
                <<",\"locals\":"<<context.local_writes.size()<<",\"bindings\":"<<context.host_reference_bindings.size()
                <<",\"heap_sha256\":"<<JsonString(heap.sha256)<<",\"allocations\":"<<heap.allocations
                <<",\"releases\":"<<heap.releases<<",\"live_allocations\":"<<heap.live_allocations
                <<",\"service48_calls\":"<<context.service48.completed_calls
                <<",\"next_builtin\":"<<unsigned(nextId)<<",\"next_argc\":"<<unsigned(nextArgc);
            if(nextId==0xF0 &&trace.sp>=nextArgc &&nextArgc>0) {
                const auto& service=context.stack_window.at(static_cast<size_t>(trace.sp-nextArgc));
                report<<",\"next_host_service_word\":"<<service.word;
            }
            report<<",\"verified\":"<<(ok?"true":"false")<<",\"barrier\":"<<JsonString(trace.diagnostic.text())<<'}';
        }
        report<<"],\"persistent_owned_contexts\":2,\"verified\":"<<(storageVerified?"true":"false")<<"}\n";
        status=L"Условные storage-пробы: строки и ненулевая запись выполнены; следующие ABI остаются барьерами. Исходные producers не подтверждены.";
    }
    void ClockStorageProbe() {
        messageVisible=false;
        if(!program.loaded()) {status=L"NEKO.KSC не загружен.";return;}
        const bool captured=!options.clockSample.empty();
        dokodemo::recovered::ClockDependency dependency;
        if(captured) {
            const auto sample=ReadCapturedClockSample(options.clockSample);
            dependency.provider=[sample]() {return sample;};
        } else {
            if(!sourceClockCache->RefreshFromWindows()) {
                status=L"Windows clock не предоставил допустимый cached tick; выполнение остановлено.";
                return;
            }
            dependency.provider=[cache=sourceClockCache]() {return cache->ReadCached();};
        }
        // Explicit narrow native cache/getter mapping; no PSP callback slot or
        // broad host readiness is inferred from refreshing the Windows clock.
        dependency.callback_slot_mapped=dependency.installed_getter_returns_cache=true;
        const auto cached=dependency.provider();
        std::filesystem::create_directories(options.evidence);
        std::ofstream report(options.evidence/L"clock-service39-probe.json",std::ios::binary);
        report<<"{\"conditional_sources\":true,\"captured_windows_sample\":"<<(captured?"true":"false")
            <<",\"live_windows_cache\":"<<(!captured?"true":"false")
            <<",\"cached_psp_tick_us\":"<<cached.cached_psp_tick_us
            <<",\"RTC_epoch_firmware_verified\":false,\"actual_producers_observed\":false,\"full_game\":false,\"routes\":[";
        clockStorageVerified=captured;
        for(uint8_t host:{uint8_t(0),uint8_t(1)}) {
            clockStorageProbes[host]=::ClockStorageProbe(program,host,dependency);
            const auto trace=RunToBoundary(*clockStorageProbes[host]);
            const bool ok=captured&&VerifiedClockStorageStartup(trace,"NEKO",host,program);
            clockStorageVerified=clockStorageVerified&&ok;
            if(host) report<<',';
            const auto& context=trace.context;
            report<<"{\"host\":"<<unsigned(host)<<",\"executed\":"<<trace.executed
                <<",\"pc\":"<<trace.pc<<",\"sp\":"<<trace.sp<<",\"fp\":"<<trace.fp
                <<",\"result_raw\":"<<trace.result.word<<",\"next_opcode\":"<<trace.diagnostic.opcode
                <<",\"globals\":"<<context.global_writes.size()
                <<",\"globals_sha256\":"<<JsonString(context.global_writes_sha256)
                <<",\"locals\":"<<context.local_writes.size()<<",\"bindings\":"<<context.host_reference_bindings.size()
                <<",\"STRING_sha256\":"<<JsonString(context.string_arena.sha256)
                <<",\"GENERAL_sha256\":"<<JsonString(context.general_arena.sha256)
                <<",\"source_fixture_verified\":"<<(ok?"true":"false")
                <<",\"barrier\":"<<JsonString(trace.diagnostic.text())<<'}';
        }
        report<<"],\"persistent_owned_contexts\":2,\"source_fixture_verified\":"
            <<(clockStorageVerified?"true":"false")<<"}\n";
        status=captured
            ?L"Связанные clock/string-пробы достигли array/RNG-барьеров; проверяется сохранённый Windows sample. Реальная готовность игры неизвестна."
            :L"Windows cached clock и строковое владение выполнены; достигнутые array/RNG-вызовы остаются барьерами. Это условная проба.";
    }
    void MessageFixture() {
        messageVisible=false;
        if(!vm) {status=L"NEKO.KSC не загружен.";return;}
        auto event=vm->start_at(0xC373-28,{dokodemo::script::Value::integer(10),dokodemo::script::Value::integer(14)});
        if(event.status==dokodemo::script::Status::Running) event=vm->run(1000);
        std::wstring error;
        if(event.status==dokodemo::script::Status::Message && gameFont.RasterizePlainCp932(event.message_cp932,messageImage,error)) {
            messageVisible=true;
            status=L"Выполнена исходная opcode 0x48, PC 0xC359, display state 6. Изолированная проверка; маршрут игры и тайминг не восстановлены.";
            std::filesystem::create_directories(options.evidence);
            std::ofstream report(options.evidence / L"message-fixture.json",std::ios::binary);
            report << "{\"source_sha256\":" << JsonString(program.source().sha256) << ",\"fixture\":true,\"gameplay_route_verified\":false,\"pc\":"
                   << event.pc << ",\"opcode\":" << event.opcode << ",\"display_state\":" << event.display_state << ",\"original_xft\":true}\n";
        } else status=event.status==dokodemo::script::Status::Stopped ? Utf8(event.diagnostic.text()) : error;
    }
    std::filesystem::path SavePath() { return options.evidence / L"saves/recovery-state.dns"; }
    void Save() {
        std::filesystem::create_directories(SavePath().parent_path());
        auto result = dokodemo::save::SaveAtomic(SavePath(), state);
        savedSuccessfully=bool(result);
        status = result ? L"Состояние нативного стенда сохранено (собственный формат, не PSP savedata)." : Utf8(result.message);
    }
    void Load() {
        auto result = dokodemo::save::Load(SavePath(), state);
        loadedSuccessfully=bool(result);
        if (result) { clock.Reset(); status = L"Состояние нативного стенда загружено. Игровая семантика времени ещё не восстановлена."; }
        else status = Utf8(result.message);
    }
    void Tick() {
        RefreshNativeStartup();
        if (nativeStartup) controllerInput = nativeStartup->PollControllerInput(GetForegroundWindow() == window);
        if (options.graphicsSmoke && !graphicsPatternDrawn && AtGameBootBoundary(startupObservation)) {
            graphicsPatternDrawn = nativeStartup->DrawGraphicsDiagnosticFrame();
            InvalidateRect(window, nullptr, FALSE);
        }
        if (options.graphicsSmoke && graphicsPatternDrawn && !graphicsSmokeResized
            && startupObservation.game_boot.graphics.present_count) {
            presentsBeforeResize = startupObservation.game_boot.graphics.present_count;
            graphicsSmokeResized = SetWindowPos(window, nullptr, 0, 0, 1000, 720,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
        }
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        elapsed = double(now.QuadPart-start.QuadPart)/frequency.QuadPart;
        auto reading = dokodemo::save::ReadSystemClock();
        auto observation = clock.Observe(reading);
        state.observed_session_ms += observation.elapsed_monotonic_ms;
        state.last_observed_utc_ms = reading.utc_ms;
        state.last_observed_utc_offset_minutes = reading.utc_offset_minutes;
        if (double(now.QuadPart-last.QuadPart)/frequency.QuadPart >= 1.0/60.0) {
            ++frames; last = now; InvalidateRect(window,nullptr,FALSE);
        }
        if(options.storageWindowTest && nativeStartup && !startupWindowCloseSent) {
            const auto& c=startupObservation.game_boot.content;
            if(!options.storageDirectTest && storageInputStep==0 && c.scene_ui.awaiting_confirmation) {
                std::filesystem::create_directories(options.evidence);
                storageInputStart=frames;storageInputStep=1;
                WriteStartupObservation(options.evidence/L"storage-prompt.json",startupObservation);
                Capture();std::filesystem::copy_file(options.evidence/L"native-window.bmp",options.evidence/L"storage-prompt.bmp",std::filesystem::copy_options::overwrite_existing);
                (void)nativeStartup->InjectTestControllerInput(dokodemo::recovered::Circle,true);
            } else if(storageInputStep==1 && frames>=storageInputStart+3) {
                heldIgnored=c.scene_ui.confirmations==0&&c.scene_ui.dialog_state==1;
                (void)nativeStartup->InjectTestControllerInput(0,false);
                (void)nativeStartup->InjectTestControllerInput(dokodemo::recovered::Circle,true);storageInputStep=2;
            } else if(storageInputStep==2 && frames>=storageInputStart+6) {
                unfocusedIgnored=c.scene_ui.confirmations==0&&c.scene_ui.dialog_state==1;
                (void)nativeStartup->InjectTestControllerInput(0,true);storageInputStep=3;
            } else if(storageInputStep==3 && frames>=storageInputStart+9) {
                (void)nativeStartup->InjectTestControllerInput(dokodemo::recovered::Circle,true);storageInputStep=4;
            }
            if(storageInputStep==4 && c.scene_ui.dialog_state==2 && c.scene_ui.dialog_progress<1 && c.scene_ui.dialog_progress>0) {
                closingObserved=true;
                WriteStartupObservation(options.evidence/L"storage-closing.json",startupObservation);
            }
            if((options.titleWindowTest?c.title.frame_drawn:c.title_model_read) && !titleReadyFrame)titleReadyFrame=frames;
            if(options.titleWindowTest&&c.title.frame_drawn&&c.title.state==1&&c.title.frames>=40&&!titleEarlyCaptured) {
                titleEarly=c.title;titleEarlyCaptured=true;
                WriteStartupObservation(options.evidence/L"title-animation-first.json",startupObservation);
                Capture();std::filesystem::copy_file(options.evidence/L"native-window.bmp",options.evidence/L"title-animation-first.bmp",std::filesystem::copy_options::overwrite_existing);
            }
            if(options.titleWindowTest&&titleEarlyCaptured&&c.title.frames>=100) {
                titleAnimationObserved=c.title.actors[0].x<titleEarly.actors[0].x
                    &&c.title.actors[0].local_pose_digest!=titleEarly.actors[0].local_pose_digest
                    &&c.title.actors[0].animation_time!=titleEarly.actors[0].animation_time;
            }
            storageBoundaryPassed=(options.storageDirectTest||(heldIgnored&&unfocusedIgnored&&closingObserved))
                &&c.scene_ui.confirmations==(options.storageDirectTest?0u:1u)
                &&c.scene_ui.dialog_state==3&&!c.scene_ui.storage_dialog&&c.title_mounted&&c.title_ui_ready
                &&bool(c.status)&&c.title_model_read
                &&(options.titleWindowTest?(titleAnimationObserved&&c.title_constructor_returned&&c.title.frame_drawn&&c.title.live&&c.title.camera_ready&&c.title.state==1&&c.title.frames>=100&&c.title.visible_actors>0&&c.title.pixel_writes>1000&&c.title.clones==10&&c.title.motions==21&&!c.title.scene3_completed&&!c.title.arrival_branch_pending):c.title_constructor_returned)
                &&c.autoload.phase==dokodemo::recovered::GameAutoloadPhase::TitleRequested
                &&!c.autoload.source_fields_applied&&c.autoload.storage.load_calls==1&&c.autoload.storage.publish_calls==0;
            if(options.titleMenuWindowTest) {
                using namespace dokodemo::recovered;
                const auto& title=c.title;
                const auto save=[&](const wchar_t* name){
                    WriteStartupObservation(options.evidence/(std::wstring(name)+L".json"),startupObservation);
                    Capture();std::filesystem::copy_file(options.evidence/L"native-window.bmp",options.evidence/(std::wstring(name)+L".bmp"),std::filesystem::copy_options::overwrite_existing);
                };
                if(titleMenuStep==0&&title.state==1&&title.layout==0&&title.frames>=300&&title.arrival_countdown<=3&&title.primary_restarts>=3) {
                    titleArrivalObserved=true;save(L"title-attract-arrival");
                    (void)nativeStartup->InjectTestControllerInput(0,true);titleMenuWait=title.updates+3;titleMenuStep=1;
                }else if(titleMenuStep==1&&title.updates>=titleMenuWait){
                    (void)nativeStartup->InjectTestControllerInput(Down,true);titleMenuWait=title.updates+12;titleMenuStep=2;
                }else if(titleMenuStep==2&&title.updates>=titleMenuWait&&title.menu_row==1&&!title.confirmations){
                    save(L"title-main-menu");(void)nativeStartup->InjectTestControllerInput(0,true);titleMenuWait=title.updates+3;titleMenuStep=3;
                }else if(titleMenuStep==3&&title.updates>=titleMenuWait){
                    (void)nativeStartup->InjectTestControllerInput(Circle,true);titleMenuStep=4;
                }else if(titleMenuStep==4&&title.layout==2&&title.state==1&&title.confirmations==1){
                    save(L"title-character-menu");(void)nativeStartup->InjectTestControllerInput(0,true);titleMenuWait=title.updates+3;titleMenuStep=5;
                }else if(titleMenuStep==5&&title.updates>=titleMenuWait){
                    (void)nativeStartup->InjectTestControllerInput(Right,true);titleMenuStep=6;
                }else if(titleMenuStep==6&&title.selected_actor==1){
                    (void)nativeStartup->InjectTestControllerInput(0,true);titleMenuStep=7;
                }else if(titleMenuStep==7&&title.selected_actor==1&&!title.selection_moving){
                    titleMenuSelectionObserved=true;save(L"title-character-selected");
                    (void)nativeStartup->InjectTestControllerInput(Cross,true);titleMenuStep=10;
                }else if(titleMenuStep==10&&title.layout==0&&title.state==1){
                    save(L"title-main-menu-returned");(void)nativeStartup->InjectTestControllerInput(0,true);
                    titleMenuWait=title.updates+3;titleMenuStep=11;
                }else if(titleMenuStep==11&&title.updates>=titleMenuWait){
                    (void)nativeStartup->InjectTestControllerInput(Circle,true);titleMenuStep=12;
                }else if(titleMenuStep==12&&title.layout==2&&title.state==1&&title.confirmations==2){
                    (void)nativeStartup->InjectTestControllerInput(0,true);titleMenuWait=title.updates+3;titleMenuStep=13;
                }else if(titleMenuStep==13&&title.updates>=titleMenuWait){
                    (void)nativeStartup->InjectTestControllerInput(Circle,true);titleMenuStep=8;
                }else if(titleMenuStep==8&&title.confirmations==3){
                    (void)nativeStartup->InjectTestControllerInput(0,true);titleMenuStep=9;
                }
                storageBoundaryPassed=titleArrivalObserved&&titleMenuSelectionObserved&&titleMenuStep==9&&title.softkey_requested
                    &&title.selection_motion_complete&&title.confirmations==3&&title.menu_input_implemented
                    &&!title.scene3_completed&&!title.arrival_branch_pending&&c.blocked&&c.frontier_pc==0x1FD80
                    &&c.status.code==GameFsCode::Unavailable&&title.layout==3&&title.state==1;
            }
            if(options.softkeyWindowTest)SoftkeyTestTick(c);
        }
        if (options.connectedStartupWindowTest && frames >= 6 && !startupWindowCloseSent
            && ((options.playWindowFixture||options.kanaWindowFixture)?AtOwnedGameplayBoundary(startupObservation,options.kanaWindowFixture,options.notebookWindowFixture&&notePassed):AtNativeModuleSequenceBoundary(startupObservation))
            && (!options.inventoryActionWindowFixture || inventoryActionPassed)
            && (!options.decorateWindowFixture || decorationPassed)
            && (!options.putawayWindowFixture || modelRemovalPassed)
            && (!options.discardWindowFixture || discardPassed)
            && (!options.kanaWindowFixture || (options.gameplayWindowFixture?gameplayPassed:kanaPassed))
            && (!options.mainMenuWindowFixture || mainMenuFixturePassed) && (!options.notebookWindowFixture || notePassed) && (!options.flow5WindowFixture || flow5Passed) && (!options.noteSelfWindowFixture || noteSelfPassed) && (!options.menuMarkWindowFixture || mmMarkPassed) && (!options.menuPlaceWindowFixture || mpPassed) && (!options.menuRoomWindowFixture || mrPassed)
            && (!options.repeatWindowFixture || repeatPassed)
            && (!options.playWindowFixture || playPassed)
            && (!options.soundWindowFixture || SelectedGameSoundDrained(startupObservation))
            && (!options.storageWindowTest || (storageBoundaryPassed && titleReadyFrame && frames>=titleReadyFrame+3))
            && (!options.graphicsSmoke || (graphicsPatternDrawn && graphicsSmokeResized
                && startupObservation.game_boot.graphics.present_count >= presentsBeforeResize + 3))) {
            if(options.playWindowFixture){
                std::ofstream gate(options.evidence/L"play-finish-gates.json",std::ios::binary);
                gate<<"{\"boot_oracle\":"<<(AtNativeModuleSequenceBoundary(startupObservation)?"true":"false")
                    <<",\"live_owned_game_boundary\":"<<(AtOwnedGameplayBoundary(startupObservation)?"true":"false")
                    <<",\"title_ready_frame\":"<<titleReadyFrame<<",\"frame\":"<<frames<<"}\n";
            }
            startupWindowCloseSent = true;
            startupWindowBoundaryReached=true;
            std::filesystem::create_directories(options.evidence);
            WriteStartupObservation(options.evidence / L"window-startup-boundary.json", startupObservation);
            Capture();
            PostMessageW(window, WM_CLOSE, 0, 0); // actual app shutdown route, no old F-key sequence
        }
        if(options.soundWindowFixture&&playPassed&&!SelectedGameSoundDrained(startupObservation)&&!startupWindowCloseSent){
            startupWindowCloseSent=true;WriteStartupObservation(options.evidence/L"sound-route-not-passed.json",startupObservation);
            PostMessageW(window,WM_CLOSE,0,0);
        }
        if (options.connectedStartupWindowTest && elapsed > (options.gameplayWindowFixture?240.0:options.playWindowFixture?300.0:options.repeatWindowFixture?180.0:options.conversationWindowFixture?120.0:options.cameraWindowTest?390.0:(options.softkeyWindowTest?90.0:(options.titleMenuWindowTest?75.0:(options.titleWindowTest?20.0:5.0)))) && !startupWindowCloseSent) {
            startupWindowCloseSent = true;
            std::filesystem::create_directories(options.evidence);
            WriteStartupObservation(options.evidence / L"window-startup-unexpected.json", startupObservation);
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        if (options.smoke && frames >= 30 && !smokeInputSent) {
            smokeInputSent=true;
            if(options.constructionSmoke) {
                for(unsigned i=0;i<2;++i) {
                    SendMessageW(window,WM_KEYDOWN,VK_F7,0);
                    if(managerVerified) ++constructionSmokePasses;
                }
            } else {
                for(auto key : {VK_RIGHT,VK_LEFT,VK_F5,VK_F9,VK_F1,VK_F4,VK_F6,VK_F7,VK_F8,VK_F10,VK_F2}) SendMessageW(window,WM_KEYDOWN,key,0);
            }
        }
        if (options.smoke && elapsed > 2.0) {
            Capture();
            std::ofstream report(options.evidence / L"smoke-test.json", std::ios::binary);
            report << "{\n  \"native_win32_loop\": true,\n  \"frames\": " << frames << ",\n  \"input_events\": " << inputEvents
                << ",\n  \"image_loaded\": " << (imageLoaded ? "true" : "false") << ",\n  \"asset_count\": " << images.size()
                << ",\n  \"input_method\": \"WM_KEYDOWN\",\n  \"native_save_ok\": " << (savedSuccessfully ? "true" : "false")
                << ",\n  \"construction_only\": " << (options.constructionSmoke ? "true" : "false")
                << ",\n  \"construction_passes\": " << constructionSmokePasses
                << ",\n  \"legacy_smoke_sequence_run\": " << (options.constructionSmoke ? "false" : "true")
                << ",\n  \"native_load_ok\": " << (loadedSuccessfully ? "true" : "false")
                << ",\n  \"startup_prefix_verified\": " << (startupPrefixVerified ? "true" : "false")
                << ",\n  \"conditional_service47_verified\": " << (service47Verified ? "true" : "false")
                << ",\n  \"conditional_bulk_startup_verified\": " << (bulkVerified ? "true" : "false")
                << ",\n  \"owned_manager_queue_verified\": " << (managerVerified ? "true" : "false")
                << ",\n  \"conditional_owned_storage_verified\": " << (storageVerified ? "true" : "false")
                << ",\n  \"conditional_clock_service39_verified\": " << (clockStorageVerified ? "true" : "false")
                << ",\n  \"producer_backing_owned\": " << (producer ? "true" : "false")
                << ",\n  \"actual_buffer_available\": false"
                << ",\n  \"original_message_rendered\": " << (messageVisible ? "true" : "false")
                << ",\n  \"full_game\": false,\n  \"source\": " << JsonString(messageVisible ? "scripts/original/NEKO.KSC" : Utf8(image.path.wstring())) << "\n}\n";
            DestroyWindow(window);
        }
    }
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { app = reinterpret_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); app->window = window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    if (!app) return DefWindowProcW(window,message,wParam,lParam);
    switch (message) {
        case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {640,480}; return 0;
        case WM_SIZE: app->width = std::max(1,int(LOWORD(lParam))); app->height = std::max(1,int(HIWORD(lParam))); app->ReleaseBuffer(); return 0;
        case WM_PAINT: app->Paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_KEYDOWN: app->Key(wParam,lParam); return 0;
        case WM_KEYUP: app->textGate.KeyUp(uint32_t(wParam));return 0;
        case WM_CHAR: app->Character(wchar_t(wParam),lParam);return 0;
        case WM_SETFOCUS:app->TextFocus(true);break;
        case WM_KILLFOCUS:app->TextFocus(false);break;
        case WM_IME_STARTCOMPOSITION:app->ImeStart();break;
        case WM_IME_COMPOSITION:if(app->ImeComposition(lParam))return 0;break;
        case WM_IME_ENDCOMPOSITION:app->ImeEnd();break;
        case WM_CLOSE: app->RequestWindowClose(); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        SetProcessDPIAware(); auto options = ParseOptions();
        if (options.connectedStartupTest) return ConnectedStartupTest(options);
        if (options.selfTest) return SelfTest(options);
        Application app(options);
        WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.lpszClassName = L"DokodemoNativeRecovery"; wc.hCursor = LoadCursorW(nullptr,IDC_ARROW);
        if (!RegisterClassW(&wc)) throw std::runtime_error("Cannot register Win32 window");
        auto window = CreateWindowExW(0,wc.lpszClassName,L"Doko Demo Issyo — native recovery (incomplete)",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1100,800,nullptr,nullptr,instance,&app);
        if (!window) throw std::runtime_error("Cannot create Win32 window");
        app.BeginNativeStartup();
        ShowWindow(window, options.post17WindowFixture?SW_SHOWNORMAL:show); UpdateWindow(window);
        MSG message{}; bool running = true;
        while (running) {
            while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if (message.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if (running) { app.Tick(); MsgWaitForMultipleObjects(0,nullptr,FALSE,2,QS_ALLINPUT); }
        }
        if (options.connectedStartupWindowTest) {
            app.RefreshNativeStartup();
            WriteStartupObservation(options.evidence / L"window-startup-after-close.json", app.startupObservation);
            const auto& graphics = app.startupObservation.game_boot.graphics;
            const bool graphicsOk = !options.graphicsSmoke || (app.graphicsPatternDrawn && app.graphicsSmokeResized
                && graphics.initialized && graphics.diagnostic_frame && graphics.present_count >= 3
                && graphics.present_count >= app.presentsBeforeResize + 3
                && graphics.last_presented_sequence == graphics.frame_sequence
                && !graphics.live && !graphics.color_bytes && !graphics.depth_bytes && !graphics.matrix_bytes);
            const bool retiredInputRefused=!options.storageWindowTest||
                app.nativeStartup->InjectTestControllerInput(dokodemo::recovered::Circle,true).code==dokodemo::StartupRuntimeCode::Closed;
            dokodemo::recovered::GameTextEvent retiredText;
            retiredText.context=app.cancelledTextContext;retiredText.kind=dokodemo::recovered::GameTextKind::Confirm;retiredText.test_injected=true;
            const bool retiredTextRefused=!options.softkeyWindowTest||app.nativeStartup->PostGameText(retiredText).code==dokodemo::StartupRuntimeCode::Closed;
            const bool ok = graphicsOk && retiredInputRefused && retiredTextRefused && app.startupWindowBoundaryReached
                && (!options.inventoryActionWindowFixture || app.inventoryActionPassed)
                && (!options.decorateWindowFixture || app.decorationPassed)
                && (!options.putawayWindowFixture || app.modelRemovalPassed)
                && (!options.discardWindowFixture || app.discardPassed)
                && (!options.kanaWindowFixture || (options.gameplayWindowFixture?app.gameplayPassed:app.kanaPassed))
                && (!options.mainMenuWindowFixture || app.mainMenuFixturePassed) && (!options.notebookWindowFixture || app.notePassed) && (!options.flow5WindowFixture || app.flow5Passed) && (!options.noteSelfWindowFixture || app.noteSelfPassed) && (!options.menuMarkWindowFixture || app.mmMarkPassed) && (!options.menuPlaceWindowFixture || app.mpPassed) && (!options.menuRoomWindowFixture || app.mrPassed)
                && (!options.repeatWindowFixture || app.repeatPassed)
                && (!options.playWindowFixture || app.playPassed)
                && (!options.soundWindowFixture || (SelectedGameSoundDrained(app.startupObservation)&&app.startupObservation.native_audio.actor_joined&&app.startupObservation.native_audio.handles_closed&&app.startupObservation.native_audio.com_references_released))
                && (!options.storageWindowTest || app.storageBoundaryPassed) && NativeModuleSequenceClosed(app.startupObservation,!(options.playWindowFixture||options.kanaWindowFixture))
                && app.startupObservation.module_uid > 0
                && app.startupObservation.module_load_returned && app.startupObservation.module_start_returned
                && app.startupObservation.module_start_result == 0
                && app.startupObservation.module_file_closed_while_input_live
                && app.startupObservation.module_stop_called && app.startupObservation.module_stop_returned
                && app.startupObservation.module_stop_result == 0 && app.startupObservation.module_unloaded
                && app.startupObservation.module_input_released && !app.startupObservation.module_input_live
                && !app.startupObservation.staging_live && !app.startupObservation.staging_borrow_live
                && app.startupObservation.callback_dispatch_count == 1
                && app.startupObservation.callback_dispatch_thread == app.startupObservation.worker_thread_id
                && app.startupObservation.caller_joined && app.startupObservation.worker_joined
                && app.startupObservation.resources_closed && app.frames >= 6 && (options.softkeyWindowTest?app.inputEvents>0:app.inputEvents==0);
            std::ofstream report(options.evidence / L"connected-window-test.json", std::ios::binary);
            report << "{\"passed\":" << (ok ? "true" : "false") << ",\"frames\":" << app.frames
                << ",\"title_window_test\":" << (options.titleWindowTest?"true":"false")
                << ",\"title_animation_observed\":" << (app.titleAnimationObserved?"true":"false")
                << ",\"storage_input_test\":" << (options.storageWindowTest?"true":"false")
                << ",\"boundary_snapshot_reached\":" << (app.startupWindowBoundaryReached?"true":"false")
                << ",\"storage_direct_test\":" << (options.storageDirectTest?"true":"false")
                << ",\"retired_input_refused\":" << (retiredInputRefused?"true":"false")
                << ",\"input_provenance\":" << JsonString(options.softkeyWindowTest?"ExplicitTestInjectedFramesAndWindowTextPath;NotPhysicalHardwareOrInstalledIME":options.storageDirectTest?"NoInputUsed":"ExplicitTestInjectedNativeFramesNotPhysicalHardware")
                << ",\"retired_text_refused\":"<<(retiredTextRefused?"true":"false")
                << ",\"held_ignored\":" << (app.heldIgnored?"true":"false")
                << ",\"unfocused_ignored\":" << (app.unfocusedIgnored?"true":"false")
                << ",\"natural_closing_observed\":" << (app.closingObserved?"true":"false")
                << ",\"storage_boundary_passed\":" << (app.storageBoundaryPassed?"true":"false")
                << ",\"natural_loop_cycles\":" << app.naturalLoopCycles
                << ",\"natural_loop_passed\":" << (app.naturalLoopPassed?"true":"false")
                << ",\"repeat_interaction_passed\":" << (app.repeatPassed?"true":"false")
                << ",\"play_interaction_passed\":" << (app.playPassed?"true":"false")
                << ",\"play_completed_cycles\":" << app.playCycles
                << ",\"requires_complete_diagnostic_history\":" << ((options.playWindowFixture||options.kanaWindowFixture)?"false":"true")
                << ",\"diagnostic_history_dropped\":" << app.startupObservation.dropped_events
                << ",\"title_menu_window_test\":" << (options.titleMenuWindowTest?"true":"false")
                << ",\"softkey_window_test\":"<<(options.softkeyWindowTest?"true":"false")
                << ",\"actor_window_test\":"<<(options.actorWindowTest?"true":"false")
                << ",\"game_loop_window_test\":"<<(options.gameLoopWindowTest?"true":"false")
                << ",\"scene_clock_window_test\":"<<(options.sceneClockWindowTest?"true":"false")
                << ",\"actor_position_window_test\":"<<(options.actorPositionWindowTest?"true":"false")
                << ",\"game71_window_fixture\":"<<(options.game71WindowFixture?"true":"false")
                << ",\"post17_window_fixture\":"<<(options.post17WindowFixture?"true":"false")
                << ",\"post19_window_fixture\":"<<(options.post19WindowFixture?"true":"false")
                << ",\"post25_rtrigger_event_observed\":"<<(app.post25RTriggerEventObserved?"true":"false")
                << ",\"post25_returned_held_refused\":"<<(app.post25ReturnedHeldRefused?"true":"false")
                << ",\"post25_returned_posted\":"<<(app.post25ReturnedPosted?"true":"false")
                << ",\"post25_triangle_event_observed\":"<<(app.post25TriangleEventObserved?"true":"false")
                << ",\"post25_window_fixture\":"<<(options.post25WindowFixture?"true":"false")
                << ",\"post26_window_fixture\":"<<(options.post26WindowFixture?"true":"false")
                << ",\"post28_window_fixture\":"<<(options.post28WindowFixture?"true":"false")
                << ",\"post28_alternate_route_observed\":"<<(app.post28AlternateStep?"true":"false")
                << ",\"post28_alternate_ui_cuts\":"<<app.post28AlternateCuts
                << ",\"post28_choice_held_refused\":"<<(app.post28ChoiceHeldRefused?"true":"false")
                << ",\"post28_choice_closing_observed\":"<<(app.post28ChoiceClosingObserved?"true":"false")
                << ",\"post28_next_held_refused\":"<<(app.post28NextHeldRefused?"true":"false")
                << ",\"post29_choice_held_refused\":"<<(app.post29ChoiceHeldRefused?"true":"false")
                << ",\"post29_choice_closing_observed\":"<<(app.post29ChoiceClosingObserved?"true":"false")
                << ",\"post29_next_held_refused\":"<<(app.post29NextHeldRefused?"true":"false")
                << ",\"post27_choice_held_refused\":"<<(app.post27ChoiceHeldRefused?"true":"false")
                << ",\"post27_choice_closing_observed\":"<<(app.post27ChoiceClosingObserved?"true":"false")
                << ",\"post27_input_held_refused\":"<<(app.post27InputHeldRefused?"true":"false")
                << ",\"post27_input_closing_observed\":"<<(app.post27InputClosingObserved?"true":"false")
                << ",\"post24_window_fixture\":"<<(options.post24WindowFixture?"true":"false")
                << ",\"post21_window_fixture\":"<<(options.post21WindowFixture?"true":"false")
                << ",\"post23_held_refused\":"<<(app.post23HeldRefused?"true":"false")
                << ",\"post24_held_refused\":"<<(app.post24HeldRefused?"true":"false")
                << ",\"post21_branch_mismatch\":"<<(app.post21BranchMismatch?"true":"false")
                << ",\"post19_linear_route\":"<<(app.post19LinearRoute?"true":"false")
                << ",\"post19_held_refused\":"<<(app.post19HeldRefused?"true":"false")
                << ",\"post20_held_refused\":"<<(app.post20HeldRefused?"true":"false")
                << ",\"post21_held_refused\":"<<(app.post21HeldRefused?"true":"false")
                << ",\"second_choice_held_refused\":"<<(app.secondChoiceHeldRefused?"true":"false")
                << ",\"second_choice_closing_observed\":"<<(app.secondChoiceClosingObserved?"true":"false")
                << ",\"reply22_held_refused\":"<<(app.reply22HeldRefused?"true":"false")
                << ",\"post17_message_held_refused\":"<<(app.post17HeldRefused?"true":"false")
                << ",\"choice_held_refused\":"<<(app.choiceHeldRefused?"true":"false")
                << ",\"choice_cancel_refused\":"<<(app.choiceCancelRefused?"true":"false")
                << ",\"choice_closing_observed\":"<<(app.choiceClosingObserved?"true":"false")
                << ",\"post_choice_held_refused\":"<<(app.postChoiceHeldRefused?"true":"false")
                << ",\"manager9_window_fixture\":"<<(options.manager9WindowFixture?"true":"false")
                << ",\"manager9_held_refused\":"<<(app.managerHeldRefused?"true":"false")
                << ",\"manager9_refocused_held_refused\":"<<(app.managerFocusRefused?"true":"false")
                << ",\"manager9_next_message_held_refused\":"<<(app.managerNextHeldRefused?"true":"false")
                << ",\"inventory_actions_window_fixture\":"<<(options.inventoryActionWindowFixture?"true":"false")
                << ",\"inventory_actions_passed\":"<<(app.inventoryActionPassed?"true":"false")
                << ",\"fixture_choice_imported\":"<<app.fixtureChoiceImported<<",\"fixture_choice_completed\":"<<app.fixtureChoiceCompleted<<",\"fixture_choice_novel\":"<<app.fixtureChoiceNovel
                << ",\"fixture_greeting_answers\":"<<app.fixtureGreetingAnswers
                << ",\"fixture_text_policy\":\"ActualRenderedGreetingPromptOssuOtherwiseAki;StrictReactionFixtureNekoTarou;TestTextOnlyNoCategoryOrResultInjection\""
                << ",\"continuation_window_fixture\":"<<(options.continuationWindowFixture?"true":"false")
                << ",\"menu_inventory_fixture\":"<<(options.menuInventoryWindowFixture?"true":"false")<<",\"menu_inventory_held_refused\":"<<(app.miHeldRefused?"true":"false")<<",\"menu_inventory_focus_refused\":"<<(app.miFocusRefused?"true":"false")<<",\"menu_inventory_return_held_refused\":"<<(app.miReturnHeldRefused?"true":"false")
                << ",\"note_preview_fixture\":"<<(options.notePreviewWindowFixture?"true":"false")<< ",\"flow5_fixture\":"<<(options.flow5WindowFixture?"true":"false")<<",\"flow5_passed\":"<<(app.flow5Passed?"true":"false")<< ",\"notebook_fixture\":"<<(options.notebookWindowFixture?"true":"false")<<",\"notebook_passed\":"<<(app.notePassed?"true":"false")<<",\"notebook_step\":"<<app.noteStep<<",\"notebook_held_refused\":"<<(app.noteHeld?"true":"false")<<",\"notebook_focus_refused\":"<<(app.noteFocus?"true":"false")
                << ",\"note_self_fixture\":"<<(options.noteSelfWindowFixture?"true":"false")<<",\"note_self_passed\":"<<(app.noteSelfPassed?"true":"false")
                << ",\"menu_room_owner_diagnostic\":"<<(options.menuRoomOwnerDiagnostic?"true":"false")<<",\"menu_room_ownership_passed\":"<<(app.mrOwnershipPassed?"true":"false")<<",\"menu_room_interval_mesh_writes\":"<<(app.mrRoomWrites?"true":"false")
                << ",\"menu_room_fixture\":"<<(options.menuRoomWindowFixture?"true":"false")<<",\"menu_room_passed\":"<<(app.mrPassed?"true":"false")<<",\"menu_room_stage\":"<<app.mrStage<<",\"menu_room_first_parent_generation\":"<<app.mrMenuGeneration<<",\"menu_room_first_gui_generation\":"<<app.mrGuiGeneration<<",\"menu_room_owner_epoch\":"<<app.mrEpoch<<",\"menu_room_placed_image\":"<<JsonString(StartupHexBytes(app.mrPlacedImage.data(),app.mrPlacedImage.size()))
                << ",\"menu_place_fixture\":"<<(options.menuPlaceWindowFixture?"true":"false")<<",\"menu_place_passed\":"<<(app.mpPassed?"true":"false")<<",\"menu_place_row\":"<<app.mpRow<<",\"menu_place_item_id\":"<<app.mpId<<",\"menu_place_slot\":"<<app.mpSlot<<",\"menu_place_generation\":"<<app.mpGeneration
                << ",\"menu_mark_fixture\":"<<(options.menuMarkWindowFixture?"true":"false")<<",\"menu_mark_passed\":"<<(app.mmMarkPassed?"true":"false")
                << ",\"main_menu_fixture\":"<<(options.mainMenuWindowFixture?"true":"false")<<",\"main_menu_fixture_passed\":"<<(app.mainMenuFixturePassed?"true":"false")
                << ",\"gameplay_exploration_fixture\":"<<(options.gameplayWindowFixture?"true":"false")<<",\"gameplay_exploration_passed\":"<<(app.gameplayPassed?"true":"false")<<",\"gameplay_posts\":"<<app.gameplayPosts<<",\"gameplay_known_input\":"<<(app.gameplayKnownInput?"true":"false")<<",\"gameplay_corrected_input\":"<<(app.gameplayCorrectedInput?"true":"false")<<",\"gameplay_word_row\":"<<(app.gameplayWordRow?"true":"false")<<",\"gameplay_echo\":"<<(app.gameplayEcho?"true":"false")
                << ",\"kana_word_window_fixture\":"<<(options.kanaWindowFixture?"true":"false")<<",\"kana_word_passed\":"<<(app.kanaPassed?"true":"false")<<",\"kana_held_refused\":"<<(app.kanaHeldRefused?"true":"false")<<",\"kana_closing_observed\":"<<(app.kanaClosing?"true":"false")<<",\"kana_consumer_passed\":"<<(app.kanaWordPassed?"true":"false")<<",\"kana_echo_seen\":"<<(app.kanaEchoSeen?"true":"false")<<",\"kana_offered_interactions\":"<<app.kanaSeekCount
                << ",\"discard_window_fixture\":"<<(options.discardWindowFixture?"true":"false")<<",\"discard_passed\":"<<(app.discardPassed?"true":"false")<<",\"discard_cancel_passed\":"<<(app.discardCancelPassed?"true":"false")<<",\"discard_held_passed\":"<<(app.discardHeldPassed?"true":"false")
                << ",\"putaway_window_fixture\":"<<(options.putawayWindowFixture?"true":"false")<<",\"model_removal_passed\":"<<(app.modelRemovalPassed?"true":"false")
                << ",\"decoration_window_fixture\":"<<(options.decorateWindowFixture?"true":"false")<<",\"decoration_passed\":"<<(app.decorationPassed?"true":"false")
                << ",\"inventory_window_fixture\":"<<(options.inventoryWindowFixture?"true":"false")
                << ",\"conversation_window_fixture\":"<<(options.conversationWindowFixture?"true":"false")
                << ",\"calendar_provenance\":"<<JsonString(options.conversationWindowFixture?"ExplicitTestCalendarStepPlus5MinutesAfterIntroOrigin;NotElapsedRTCProof":"WindowsLocalCivilClock")
                << ",\"camera_message_window_test\":"<<(options.cameraWindowTest?"true":"false")
                << ",\"game_message_held_refused\":"<<(app.messageHeldRefused?"true":"false")
                << ",\"game_message_focus_refused\":"<<(app.messageFocusRefused?"true":"false")
                << ",\"game_message_fresh_accepted\":"<<(app.messageFreshAccepted?"true":"false")
                << ",\"actor_channel_window_test\":"<<(options.actorChannelWindowTest?"true":"false")
                << ",\"text_cancel_observed\":"<<(app.textCancelObserved?"true":"false")
                << ",\"text_natural_close_observed\":"<<(app.textCloseObserved?"true":"false")
                << ",\"stale_text_refused\":"<<(app.staleTextRefused?"true":"false")
                << ",\"player_cancel_observed\":"<<(app.playerCancelObserved?"true":"false")
                << ",\"birthday_back_observed\":"<<(app.birthdayBackObserved?"true":"false")
                << ",\"invalid_day_rejected\":"<<(app.dayRejectedObserved?"true":"false")
                << ",\"profile_no_preserved\":"<<(app.profileNoObserved?"true":"false")
                << ",\"title_arrival_observed\":" << (app.titleArrivalObserved?"true":"false")
                << ",\"title_menu_selection_observed\":" << (app.titleMenuSelectionObserved?"true":"false")
                << ",\"validation_scope\":" << JsonString(options.menuRoomWindowFixture?"ActualMainMenu5Source13AcrossChildCloseAndNewGUIBindThenPlaced14;SameModelPublication;UnobscuredRoomFramesRequireVisualInspection;NotSaveSceneOrFullGame":options.menuPlaceWindowFixture?"ActualMainMenu5Source13Placed14;ExactReceiptApprovedRAMAndAuxiliary;ActualModelOwnerAndNaturalRetirement;Disabled32;CurrentChildDrain;NativeSceneAnchorLocalX;NotOriginalSchedulingSaveOrFullGame":options.menuMarkWindowFixture?"ActualMainMenu5NIL6Mark15Unmark14;ReceiptQualified2FDAStores;ExactOtherTypedRAM;DisabledCallback32;ActualChildClosePollDrain;NotSaveOrScene":options.menuInventoryWindowFixture?"ActualAuthoredMenuRow5Resource14;Source11Then18Disable;RealInventoryClosePollRetirement;SameBorrowedActivation;NotGAME41SaveOrSceneTransfer":options.noteSelfWindowFixture?"ActualNOTEIndex0SelfDetails;ImmutableTypedRecord;SharedRNGOnceAfterProducer;OwnedRoot6BackThenRealOrdinaryClosePollDrain;NativeCp932AtlasAndViewspace;NotFilledFriendsPreviewOtherModesSaveOrFullGame":options.notebookWindowFixture?"ActualAuthoredNotebookResource9EmptyOpenCancel;GAME11TypedRegistration;Real11Then18;SourcePoll469Zero;CurrentGenerationDeferredDrain;NamedNativeResume;CurrentSourceFlow0Or5Sub4;NotPopulatedPreviewSaveOrFullGame":options.mainMenuWindowFixture?"ActualAuthoredMainMenuOpenCancel;BorrowAvailableOwner;Source10And11;No469ClearSaveOrSceneTransfer":options.continuationWindowFixture?"BoundedActualOfferedGameplayWithSharedOwnedRoute;ExplicitInvocationRNGPolicy;NotNewFeatureOrFullGame":options.gameplayWindowFixture?"ActualKnownWordReactionAndFreshCorrection;Opcode55DescendingConsumer;TypedRowAndFONTThenOwnedReturn;ExplicitInvocationRNGPolicy;NotSaveOrFullGame":options.kanaWindowFixture?"ActualInput32Kana229;SourceWordClassPrefixSuffixCountAndVMRow;FreshOwnerReturn;NotNJSaveOrMatching":options.discardWindowFixture?"ActualPlacedDiscard16AfterTRCCancelAndFreshYes;AllVMRowCellsNIL;NaturalModelRetirement;NotFilesOrSave":options.putawayWindowFixture?"ActualPlacedEvent14AfterReal13;VMReadback;NaturalModelDisableRetainThenDestruction;NotSaveOrFullGame":options.decorateWindowFixture?"ActualEvent13SourcePlacement;OwnedRoomModel;VMWriteReadback;RealInventoryRetirement;NotSaveOrFullGame":options.inventoryActionWindowFixture?"ActualItemMarkAndUnmarkViaOwnedEvent15And14;VMField5;DisplayReadback;NoAwardOrNetwork;NormalInventoryRetirement;NotFullGame":options.soundWindowFixture?"SelectedGAME73BankSequenceWave;ReferenceADSR;NativeOutputDrain;NotAudibleOrFirmwareMatching":options.playWindowFixture?"ContinuedOwnedConversationsFromGeneration3;RealUI;No469Clear;NotFullGame":options.repeatWindowFixture?"SourceOwnedCommonConversation;TwoAcceptedEvent0CyclesAndFreshSuccessor;SafePhase1Dispatch;No469Clear;NotFullGame":options.activationWindowFixture?"SourceQualifiedTopicConversationLifecycle;FreshBodyFarewellMainConsumer;WindowsOwnedEvent0ThenActualConversation;NaturalDeadlineResumeAndRepeatedOwnedLoop;TimedIdle;NoSourceWakeOrTokenRearm;NotFullGame":options.post28WindowFixture&&app.post28AlternateStep?"INTERMEDIATE_ALTERNATE_ROUTE;ActualSELECTAndFreshMessages;NoNewQualifiedInputToken;NotPost28Pass;NoSceneStatSaveClaim":options.post28WindowFixture?"FreshAck28And29;ActualTwoAndFiveOptionSELECTCloseResult1;ObservedOwnedWordContinuation;ConsumedPoliciesNotRearmed;NoSceneStatSaveClaim":options.post26WindowFixture?"FreshAck26And27;ActualSELECT1ThenWindowsInput32Ascii8NativeTextPanel;RealCloseOwnedTextAuxArray57AndObservedFONT28;ConsumedPoliciesNotRearmed;NoSceneStatOrSaveClaim":options.post25WindowFixture?"FreshAck25;ActualFarewellNormal31AndCallerMainLoop;SourceTriangle12Arg1ThenRTrigger12Arg4;ObservedNextConsumer;NoSceneCompletionOrEvent0Rearm":options.post24WindowFixture?"FreshAck24;ActualTRUEAndNormal31CallerWitness;NextObservedConsumer;NoSceneOrFriendshipMutationClaim":options.post21WindowFixture?"ActualFreshAck21;TwoSourceOptionsRealCloseResult1;OriginalFONTResponses22_23_24WithIndependentFreshEdges;Held24Pending;NoFriendshipMutationOrSceneCompletion":options.post19WindowFixture?(app.post19LinearRoute?"ActualLinearRngRouteFreshAck19And20ToOriginalFONTReplies20And21;Held21Refused;NotSceneCompletion":"FreshMessage19AckThenActualRepeatedQuestionAndSecondSELECTRealCloseResult1Response22;OwnedDictionaryFields14And15;NotSceneCompletion"):options.post17WindowFixture?"ActualFreshMessage17Ack;SELECT33ActualOwnedOptionsRealClipCloseResult2;ActualBranchResponse19AndActorUpdate;NotSceneCompletion":options.manager9WindowFixture?"ActualManager9OrderedOuterReturn;Source469CircleBlocked;WindowsPostModalOwnedEvent0;ActualCallbackReturnAndNextFONTMessage;NotOriginalLoadManSceneChange":options.game71WindowFixture?"ActualGAME71RecordMutationThenByeMessageFreshAckAndCameraOut;Manager9RequestPendingNotSceneReturn":options.inventoryWindowFixture?"ActualGAME41False;MOCHIMONOListFilterInputCancelFadeClosePollDeferredRetirement;NativeActualCloseOnlyWake;NextUnimplementedGAME71":options.conversationWindowFixture?"ActualKscIntroductionAndSeparateFreshMessageEdgesThenStringResult;ExplicitCalendarFixture;NotFullTextEngineOrGame":options.cameraWindowTest?"ImmediateGAME44FalseAndIndependentCameraCurveCompletion;ActualCGuiIntroAndMinuteClockWait;KSCMessagesFONTAndFreshInput;NotFullTextEngineOrGame":options.actorPositionWindowTest?"ActualType12RegionOwnersAndGAME19Boolean;GAME28AndASC178ActualTurn;GAME44CameraRequestUnimplemented":options.sceneClockWindowTest?"ActualGAME36CivilClockAndSevenSeasonalControls;ActualMode3MovementToCoord16AndEligibleVMResume;GAME19PositionCheckUnimplemented":options.actorChannelWindowTest?"ActualMotion8ThenScenePair3ThenMotion123End;ASC176ReadyAndUnwind;EligibleVM54ResumeToUnimplementedGame36;ActorRoomDraw":options.gameLoopWindowTest?"ActualKscMainControlSwitchRoomFrame;LiveActorEventWaitingAtASC176;ASCExecutionAndActorDrawNotImplemented":options.softkeyWindowTest?"ActualKscStartupReturnsAfterDictionaryImportQueriesMutations;NativeProfileClockRng;MainLoopControlBoundaryNotGameplay":options.titleMenuWindowTest?"SourceTitleMenuAndMotionToExplicitUnavailableSoftkey;NotKeyboardOrScene3Completion":"LegacySelectedBoundary")
                << ",\"graphics_smoke\":" << (options.graphicsSmoke ? "true" : "false")
                << ",\"diagnostic_frame\":" << (graphics.diagnostic_frame ? "true" : "false")
                << ",\"actual_gdi_present_count\":" << graphics.present_count
                << ",\"window_resized\":" << (app.graphicsSmokeResized ? "true" : "false")
                << ",\"renderer_buffers_closed\":" << (!graphics.live && !graphics.color_bytes && !graphics.depth_bytes && !graphics.matrix_bytes ? "true" : "false")
                << ",\"old_key_sequence_executed\":false,\"automatic_native_startup\":true"
                << ",\"native_module_lifecycle\":true,\"full_audio_body\":false"
                << ",\"multi_module_partial_init\":true,\"second_Start_returned\":"
                << (app.startupObservation.module_observations.size() > 1
                    && app.startupObservation.module_observations[1].module_start_returned ? "true" : "false")
                << ",\"old_ECB_advanced\":0,\"coverage_delta\":0,\"full_game\":false}\n";
            return ok ? 0 : 1;
        }
        if(options.constructionSmoke) {
            return !app.imageLoaded || app.frames < 30 || app.inputEvents != 2
                || app.constructionSmokePasses != 2 || !app.managerVerified || !app.producer
                || app.savedSuccessfully || app.loadedSuccessfully || app.startupPrefixVerified
                || app.service47Verified || app.bulkVerified || app.storageVerified
                || app.clockStorageVerified || app.messageVisible ? 1 : 0;
        }
        return options.smoke && (!app.imageLoaded || app.frames < 30 || app.inputEvents < 9 || !app.savedSuccessfully || !app.loadedSuccessfully
            || !app.startupPrefixVerified || !app.service47Verified || !app.bulkVerified || !app.managerVerified
            || !app.storageVerified || !app.clockStorageVerified || !app.producer || !app.messageVisible) ? 1 : 0;
    } catch (const std::exception& error) { Output(std::string("ERROR: ")+error.what()+"\n"); return 2; }
}
