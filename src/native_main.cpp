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
    std::filesystem::path assets, evidence, clockSample, rngSample, startupMedia;
};
Options ParseOptions() {
    Options options;
    int count = 0; auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; ++i) {
        std::wstring arg = args[i];
        if (arg == L"--self-test") options.selfTest = true;
        else if (arg == L"--connected-startup-test") options.connectedStartupTest = true;
        else if (arg == L"--connected-startup-window-test") options.connectedStartupWindowTest = true;
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
    out << "]}";
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
void WriteStartupObservation(const std::filesystem::path& path, const dokodemo::StartupObservation& observation) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write native startup observation");
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    constexpr char hex[] = "0123456789abcdef";
    std::string pendingOptions;
    for (uint8_t byte : observation.pending_module_options) {
        pendingOptions += hex[byte >> 4]; pendingOptions += hex[byte & 15];
    }
    out << "{\"domain\":" << JsonString(observation.domain)
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
        && observation.caller_state == "SourceSleeping" && observation.worker_state == "Blocked"
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
bool QueryEnvelopeModulesClosed(const dokodemo::StartupObservation& observation) {
    using namespace dokodemo::recovered;
    if (observation.module_observations.size() < 2 || !observation.module_records.empty()
        || !observation.module_providers.empty() || !observation.caller_joined || !observation.worker_joined
        || !observation.resources_closed || observation.callback_dispatch_count != 1
        || observation.callback_dispatch_thread != observation.worker_thread_id || observation.dropped_events
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
bool AtGameBootBoundary(const dokodemo::StartupObservation& o) {
    const auto& boot = o.game_boot;
    if (!NineModuleOwnersReady(o) || !o.reached_100C || !boot.controller_setup_returned
        || boot.sampling_mode != 1 || boot.sampling_cycle != 0 || !boot.memory_initialization_returned
        || !boot.memory_initialization_succeeded || boot.default_arena != 1
        || o.barrier_source_pc != 0x1014 || o.current_frontier_source_pc != 0x1014
        || o.barrier_module_relative || o.current_frontier_module_relative
        || o.barrier_source_module.owner || o.current_frontier_source_module.owner
        || o.barrier != "UnrecoveredGraphicsSetupC0C") return false;
    constexpr std::array<uint32_t, 3> sizes{{0xB94000, 0x74E000, 0x46000}};
    for (size_t i = 0; i < sizes.size(); ++i) {
        const auto& arena = boot.arenas[i];
        if (!arena.live || !arena.host_address || arena.requested_bytes != sizes[i]
            || arena.committed_bytes < sizes[i] || arena.source_coordinate != 0x70000000 + i * 0x01000000)
            return false;
    }
    return true;
}
bool AdhocctlSequenceClosed(const dokodemo::StartupObservation& o) {
    return o.module_observations.size()==9 && QueryEnvelopeModulesClosed(o)
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
bool NativeModuleSequenceClosed(const dokodemo::StartupObservation& observation) {
    for (const auto& arena : observation.game_boot.arenas) if (arena.live) return false;
    if (observation.module_observations.size() == 9 && observation.module_observations[8].module_uid_assigned) return AdhocctlSequenceClosed(observation);
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
    std::wstring startupStatus = L"Нативный запуск ещё не начат.";
    bool startupWindowCloseSent = false;
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
    explicit Application(Options value) : options(std::move(value)) {
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
        if (startupObservation.resources_closed) startupStatus = L"Нативный запуск: потоки завершены, ресурсы освобождены.";
        else if (!startupObservation.status) startupStatus = L"Нативный запуск: ошибка Windows-сервиса; подробности в журнале.";
        else if (AtGameBootBoundary(startupObservation))
            startupStatus = L"Ввод и три игровые арены готовы. Далее — восстановление игровой графики C0C.";
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
        if (messageVisible) {
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
        auto source = messageVisible ? L"NEKO.KSC · файл 0xC373 · изолированная функция · аргументы 10, 14" : imageLoaded ? std::filesystem::relative(image.path, options.assets).wstring() : L"нет ресурса";
        Text(dc, L"Исходный ресурс: " + source, {24,y,width-24,y+28}, RGB(181,205,221), DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_SINGLELINE);
        Text(dc, status, {24,y+30,width-24,y+78}, RGB(211,219,223));
        Text(dc, L"←/→ ресурсы  F1/F4 KSC  F2 текст  F3 звук  F6 проба  F7 очередь  F8 storage  F10 clock  F5/F9 save/load", {24,height-62,width-24,height-32}, RGB(167,181,192));
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
    void Paint() { EnsureBuffer(); Render(bufferDC); PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint); BitBlt(dc,0,0,width,height,bufferDC,0,0,SRCCOPY); EndPaint(window,&paint); }
    void Capture() {
        EnsureBuffer(); Render(bufferDC); std::wstring error;
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
    void Key(WPARAM key) {
        ++inputEvents;
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
        if (options.connectedStartupWindowTest && frames >= 6 && !startupWindowCloseSent
            && AtNativeModuleSequenceBoundary(startupObservation)) {
            startupWindowCloseSent = true;
            std::filesystem::create_directories(options.evidence);
            WriteStartupObservation(options.evidence / L"window-startup-boundary.json", startupObservation);
            Capture();
            PostMessageW(window, WM_CLOSE, 0, 0); // actual app shutdown route, no old F-key sequence
        }
        if (options.connectedStartupWindowTest && elapsed > 5.0 && !startupWindowCloseSent) {
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
        case WM_KEYDOWN: app->Key(wParam); return 0;
        case WM_CLOSE: app->RequestWindowClose(); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window,message,wParam,lParam);
    }
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
        ShowWindow(window, show); UpdateWindow(window);
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
            const bool ok = NativeModuleSequenceClosed(app.startupObservation)
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
                && app.startupObservation.resources_closed && app.frames >= 6 && app.inputEvents == 0;
            std::ofstream report(options.evidence / L"connected-window-test.json", std::ios::binary);
            report << "{\"passed\":" << (ok ? "true" : "false") << ",\"frames\":" << app.frames
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
