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
#include "recovered/menu_primitives.h"
#include "recovered/calendar_native.h"
#include "recovered/service47_native.h"
#include "recovered/bulk_array_native.h"
#include "recovered/manager_queue_native.h"
#include "recovered/context_owner_native.h"
#include "recovered/producer_native.h"
#include "audio_catalog.h"
#include <shellapi.h>
#include <mmsystem.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iostream>
#include <memory>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

using namespace native_port;
namespace {
struct Options { bool selfTest = false, smoke = false; std::filesystem::path assets, evidence, clockSample, rngSample; };
Options ParseOptions() {
    Options options;
    int count = 0; auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; ++i) {
        std::wstring arg = args[i];
        if (arg == L"--self-test") options.selfTest = true;
        else if (arg == L"--smoke-test") options.smoke = true;
        else if (arg == L"--assets" && i + 1 < count) options.assets = args[++i];
        else if (arg == L"--evidence" && i + 1 < count) options.evidence = args[++i];
        else if (arg == L"--clock-sample" && i + 1 < count) options.clockSample = args[++i];
        else if (arg == L"--rng-sample" && i + 1 < count) options.rngSample = args[++i];
        else { LocalFree(args); throw std::runtime_error("Unknown or incomplete command line argument"); }
    }
    LocalFree(args);
    options.assets = ResolveAssets(options.assets);
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
    const std::string json(buffer.data(),static_cast<size_t>(count));
    const std::string key="\"effective_offset_minutes\"";
    const auto at=json.find(key),colon=at==std::string::npos?at:json.find(':',at+key.size());
    if(colon==std::string::npos) throw std::runtime_error("Missing RNG timezone offset");
    const auto begin=json.find_first_not_of(" \t\r\n",colon+1);
    if(begin==std::string::npos) throw std::runtime_error("Missing RNG offset integer");
    const auto end=json.find_first_not_of("-0123456789",begin);
    size_t used=0;const auto offset=std::stoll(json.substr(begin,end-begin),&used,10);
    if(used!=end-begin||offset < -1440||offset>1440) throw std::runtime_error("Invalid RNG timezone offset");
    CapturedRngSeed seed;seed.filetime_100ns=ClockSampleInteger(json,"utc_filetime_100ns");
    seed.offset_minutes=static_cast<int32_t>(offset);
    if(!dokodemo::platform::RngSeedFromUtcFileTimeAndOffset(seed.filetime_100ns,seed.offset_minutes,seed.conversion)
       ||seed.conversion.local_civil_epoch_seconds!=ClockSampleInteger(json,"seed_u32")
       ||ClockSampleInteger(json,"seed_high_u32")!=0
       ||seed.conversion.utc_tick_us!=ClockSampleInteger(json,"utc_rtc_tick_us")
       ||seed.conversion.local_tick_us!=ClockSampleInteger(json,"local_rtc_tick_us"))
        throw std::runtime_error("RNG sample contradicts the explicit successful RTC initialization ABI");
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
OwnedProducerPrefix ConstructProducerPrefix(dokodemo::recovered::ProducerInput input) {
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
    auto made=OwnedProducer::Create(input);
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
struct Application {
    Options options; HWND window = nullptr; HFONT font = nullptr, titleFont = nullptr;
    std::vector<std::filesystem::path> images; size_t selected = 0; Image image;
    std::vector<unsigned char> displayPixels;
    std::wstring status; uint64_t frames = 0, inputEvents = 0; bool imageLoaded = false, smokeInputSent = false;
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
    ~Application() { ReleaseBuffer(); DeleteObject(font); DeleteObject(titleFont); }
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
        int top = 104, bottom = std::max(top + 100, height - 178);
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
        else if (key == VK_ESCAPE) DestroyWindow(window);
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
        auto extended=ConstructOwnerSuffixPrefix({true,0},ConditionalLoadCheckpoint(0));
        auto& initialized=extended.initialized;
        auto& built=initialized.prefix;
        managerVerified=extended.verified;
        managerQueue=std::move(built.manager); // Keep real owned storage alive in the application.
        callbackOwner=std::move(built.callbackOwner);
        producer=std::move(initialized.producer);
        status=L"Условная инициализация продолжена до форматирования пути. Загрузка сценария и kernel остаются барьерами.";
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
            <<",\"suffix_entry_pc\":"<<extended.suffix.entry_pc
            <<",\"suffix_boundary_pc\":"<<extended.suffix.boundary.source_pc
            <<",\"suffix_callee\":"<<extended.suffix.boundary.callee
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
        if (options.smoke && frames >= 30 && !smokeInputSent) {
            smokeInputSent=true;
            for(auto key : {VK_RIGHT,VK_LEFT,VK_F5,VK_F9,VK_F1,VK_F4,VK_F6,VK_F7,VK_F8,VK_F10,VK_F2}) SendMessageW(window,WM_KEYDOWN,key,0);
        }
        if (options.smoke && elapsed > 2.0) {
            Capture();
            std::ofstream report(options.evidence / L"smoke-test.json", std::ios::binary);
            report << "{\n  \"native_win32_loop\": true,\n  \"frames\": " << frames << ",\n  \"input_events\": " << inputEvents
                << ",\n  \"image_loaded\": " << (imageLoaded ? "true" : "false") << ",\n  \"asset_count\": " << images.size()
                << ",\n  \"input_method\": \"WM_KEYDOWN\",\n  \"native_save_ok\": " << (savedSuccessfully ? "true" : "false")
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
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window,message,wParam,lParam);
    }
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        SetProcessDPIAware(); auto options = ParseOptions();
        if (options.selfTest) return SelfTest(options);
        Application app(options);
        WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.lpszClassName = L"DokodemoNativeRecovery"; wc.hCursor = LoadCursorW(nullptr,IDC_ARROW);
        if (!RegisterClassW(&wc)) throw std::runtime_error("Cannot register Win32 window");
        auto window = CreateWindowExW(0,wc.lpszClassName,L"Doko Demo Issyo — native recovery (incomplete)",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1100,800,nullptr,nullptr,instance,&app);
        if (!window) throw std::runtime_error("Cannot create Win32 window");
        ShowWindow(window, show); UpdateWindow(window);
        MSG message{}; bool running = true;
        while (running) {
            while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if (message.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if (running) { app.Tick(); MsgWaitForMultipleObjects(0,nullptr,FALSE,2,QS_ALLINPUT); }
        }
        return options.smoke && (!app.imageLoaded || app.frames < 30 || app.inputEvents < 9 || !app.savedSuccessfully || !app.loadedSuccessfully
            || !app.startupPrefixVerified || !app.service47Verified || !app.bulkVerified || !app.managerVerified
            || !app.storageVerified || !app.clockStorageVerified || !app.producer || !app.messageVisible) ? 1 : 0;
    } catch (const std::exception& error) { Output(std::string("ERROR: ")+error.what()+"\n"); return 2; }
}
