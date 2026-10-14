// SPDX-License-Identifier: MIT
// Portable headless integration checks. No platform API headers or link libraries.
#include "core_hash.h"
#include "core/cp932.h"
#include "script_vm.h"
#include "startup_probe.h"
#include "model_formats.hpp"
#include "recovered/game_primitives.h"
#include "recovered/menu_primitives.h"
#include "recovered/calendar_native.h"
#include "recovered/service47_native.h"
#include "recovered/bulk_array_native.h"
#include "recovered/manager_queue_native.h"
#include "recovered/string_native.h"
#include "recovered/service48_native.h"
#include "recovered/context_owner_native.h"
#include "recovered/clock_native.h"
#include "recovered/service39_native.h"
#include "recovered/producer_native.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
struct HashVector { std::size_t size; std::uint32_t seed; const char* expected; };
#include "core_hash_vectors.inc"
void require(bool condition, const std::string& text) {
    if (!condition) throw std::runtime_error(text);
}
std::uint32_t random_step(std::uint32_t value) {
    value ^= value << 13; value ^= value >> 17; value ^= value << 5; return value;
}
Bytes binary(std::size_t size, std::uint32_t seed) {
    Bytes result(size);
    for (auto& byte : result) { seed = random_step(seed); byte = std::uint8_t(seed); }
    return result;
}
Bytes read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot open core fixture: " + path.string());
    return Bytes(std::istreambuf_iterator<char>(input), {});
}
std::size_t checks = 0;
void hash_check(const Bytes& bytes, const std::string& expected) {
    using namespace dokodemo::core;
    require(sha256_hex(bytes.data(), bytes.size()) == expected, "One-shot hash differs from external expectation");
    ++checks;
    for (const std::size_t chunk : {1u,7u,55u,56u,63u,64u,65u,127u,128u,129u,4096u}) {
        Sha256 hash;
        hash.update(nullptr, 0);
        for (std::size_t at = 0; at < bytes.size();) {
            const auto count = std::min(chunk, bytes.size() - at);
            hash.update(bytes.data() + at, count); at += count;
        }
        require(hex_digest(hash.digest()) == expected, "Chunked hash differs from external expectation");
        require(hex_digest(hash.digest()) == expected, "Digest snapshot changed the hash");
        ++checks;
    }
}
void hash_tests() {
    const auto text = [](const char* bytes) { const std::string s(bytes); return Bytes(s.begin(), s.end()); };
    hash_check({}, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    hash_check(text("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    hash_check(text("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    hash_check(text("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                    "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    hash_check(Bytes(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    for (const auto& vector : hash_vectors) hash_check(binary(vector.size, vector.seed), vector.expected);
    dokodemo::core::Sha256 snapshot;
    const std::uint8_t abc[] = {'a','b','c'};
    snapshot.update(abc, 1); const auto first = snapshot.digest(); snapshot.update(abc + 1, 2);
    require(first == dokodemo::core::sha256(abc, 1), "Digest snapshot is wrong");
    require(snapshot.digest() == dokodemo::core::sha256(abc, 3), "Append after snapshot is wrong");
    bool rejected = false;
    try { snapshot.update(nullptr, 1); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && snapshot.digest() == dokodemo::core::sha256(abc, 3), "Null input changed hash state");
    if constexpr (std::numeric_limits<std::size_t>::digits >= 64) {
        rejected = false;
        try { snapshot.update(abc, std::numeric_limits<std::size_t>::max()); }
        catch (const std::length_error&) { rejected = true; }
        require(rejected && snapshot.digest() == dokodemo::core::sha256(abc, 3), "Overflow input changed hash state");
    }
    std::cout << "{\"test\":\"sha256\",\"known_vectors\":5,\"external_binary_vectors\":"
              << std::size(hash_vectors) << ",\"lengths_0_255\":256,\"random_binary_inputs\":256,"
              << "\"hash_comparisons\":" << checks << ",\"chunk_sizes\":11,\"passed\":true}\n";
}
void cp932_tests() {
    using dokodemo::core::count_cp932;
    require(count_cp932({}).valid && count_cp932({}).code_units == 0, "Empty CP932");
    const std::string mixed = std::string("A\x82\xa0\xb1", 4) + std::string(1, '\0');
    require(count_cp932(mixed).valid && count_cp932(mixed).code_units == 4, "Mixed CP932 including NUL");
    require(!count_cp932(std::string("\x82", 1)).valid, "Dangling CP932 lead accepted");
    require(!count_cp932(std::string("A\x81\x7f", 3)).valid &&
            count_cp932(std::string("A\x81\x7f", 3)).error_offset == 1, "Bad CP932 error offset");
    require(!count_cp932(std::string("\xfd", 1)).valid, "Invalid CP932 single accepted");
    std::cout << "{\"test\":\"cp932_bounded_adapter\",\"checks\":5,\"passed\":true}\n";
}
void program_tests(const std::filesystem::path& assets) {
    using namespace dokodemo::script;
    const char* names[] = {"INU", "KAERU", "NEKO", "ROBO", "USAGI"};
    const char* ksc[] = {
        "89882ec234e9bb3cb204bbabb6b1cdc190242275450179c2619c85aea33066b2",
        "16ee223dcb98c962648d4b62ffd14e06d19f9ea6b28a2d39d2c5a59e2c607a24",
        "c64773090b1c331d043b264b291c8948a8b99787f541ddc44d8e1d027958c93b",
        "1a0c7a42970951d46254aaaccff5b5b5a4b551b1bcf63b9fb0ca4a9ac3021405",
        "d94768138b839591a81f4a7d3eebe1887877a799f9c73f287a253431d90a7ec3"};
    const char* dic[] = {
        "07c48c07a20ad3c063e9e0a9d686990dc66f81bc2cc45c98ea5deea255b66791",
        "24e01626f5b7a89013beae6fed336d58710774d5c0ca0167002426588b2d9e73",
        "1ff6a6b41c5034bec862d1d59eff76d09f41afda4bfed9c48be23a4d76d91b9a",
        "ed1ec9af20b770d52d32edbc84c51a32d48d9add1d71c207f218ccb641f14cd5",
        "f07dd6d7de3b1136710f2dbfe528ab1eb9f9346c0482f1964f2a4744077d0086"};
    const std::size_t counts[] = {102682,97493,131126,105451,97807};
    std::size_t command_count = 0, dictionary_count = 0, script_bytes = 0, dic_bytes = 0;
    for (unsigned i = 0; i < 5; ++i) {
        const auto dir = assets / "scripts/original";
        const auto path = dir / (std::string(names[i]) + ".KSC");
        auto bytes = read(path); script_bytes += bytes.size();
        hash_check(bytes, ksc[i]);
        Program program; Diagnostic diagnostic;
        const auto loaded = program.load_original_bytes(bytes, path, diagnostic);
        require(loaded, diagnostic.text());
        require(program.source().sha256 == ksc[i] && program.commands().size() == counts[i], "Program pin/count mismatch");
        command_count += program.commands().size();
        Dictionary dictionary;
        const auto dic_path = dir / (std::string(names[i]) + ".DIC");
        auto dic_data = read(dic_path); dic_bytes += dic_data.size(); hash_check(dic_data, dic[i]);
        const auto dictionary_loaded = dictionary.load_original_bytes(dic_data, dic_path, diagnostic);
        require(dictionary_loaded, diagnostic.text());
        require(dictionary.source().sha256 == dic[i] && dictionary.records().size() == 162, "Dictionary pin/count mismatch");
        dictionary_count += dictionary.records().size();
        dic_data.back() ^= 1;
        require(!dictionary.load_original_bytes(std::move(dic_data), dic_path, diagnostic) &&
                dictionary.records().empty() && dictionary.source().sha256.empty(), "Unpinned DIC accepted or retained records");
        require(diagnostic.reason == "Unverified DIC SHA256", "Unpinned DIC stopped for unexpected reason");
        require(!dictionary.load_original(dir / "missing-core-fixture.DIC", diagnostic) &&
                dictionary.records().empty() && dictionary.source().sha256.empty() &&
                diagnostic.reason == "Cannot open script resource", "Dictionary file adapter changed failure behavior");
        // Three proved startup instructions only; the other child's advancing
        // original-prefix work must not change these first steps. No host time.
        for (const std::uint8_t host : {std::uint8_t(0), std::uint8_t(1)}) {
            dokodemo::startup::StartupProbe probe(program, host);
            const auto trace = probe.run(3);
            require(trace.executed == 3 && trace.status == dokodemo::startup::Status::BudgetExhausted,
                    "Core startup prefix did not exhaust the explicit three-instruction budget");
        }
        // A one-byte mutation must never acquire an accepted source identity.
        // Mutating the opaque header word keeps the structural size equation.
        bytes.at(20) ^= 1;
        require(!program.load_original_bytes(std::move(bytes), path, diagnostic) && !program.loaded(), "Unpinned KSC accepted");
        require(diagnostic.reason == "Unverified KSC SHA256; execution refused", "Unpinned KSC stopped for unexpected reason");
    }
    std::cout << "{\"test\":\"original_programs\",\"programs_accepted\":5,\"programs_denominator\":5,"
              << "\"dictionaries_accepted\":5,\"dictionaries_denominator\":5,\"ksc_bytes\":" << script_bytes
              << ",\"dic_bytes\":" << dic_bytes << ",\"structural_commands\":" << command_count
              << ",\"dictionary_records\":" << dictionary_count
              << ",\"mutated_programs_rejected\":5,\"mutated_dictionaries_rejected\":5,"
              << "\"missing_dictionary_files_rejected\":5,\"budgeted_startup_probes\":10,\"passed\":true}\n";
}
void model_tests(const std::filesystem::path& assets) {
    const auto base = assets / "models/source/NEKO.BPM/NEKO";
    using namespace dokodemo::model;
    const auto dvs = parseDvs(read(base / "TORO.DVS"));
    const auto dak = parseDak(read(base / "TORO_TALK09.DAK"));
    const auto asc = parseAsc(read(base / "NEKO.ASC"));
    validateBinding(dvs, dak);
    require(dvs.nodes.size() == 22 && dak.objects.size() == 17 && asc.names.size() == 207 && asc.blocks.size() == 1294,
            "Core original model fixtures changed");
    std::size_t faces = 0;
    for (const auto& node : dvs.nodes) if (node.hasGeometry) faces += triangles(node.geometry).size();
    require(faces > 0, "Original geometry yielded no triangles");
    std::cout << "{\"test\":\"models\",\"fixtures\":3,\"dvs_nodes\":22,\"dak_objects\":17,"
              << "\"asc_names\":207,\"asc_blocks\":1294,\"local_triangles\":" << faces << ",\"passed\":true}\n";
}
void primitive_tests() {
    using namespace dokodemo::recovered;
    require(integer_equals(0x2fffffff, -1) && !integer_equals(0x3fffffff, -1), "Integer equality primitive");
    require(increment_value(0x2fffffff) == 0x20000000 && increment_value(0x10000000) == 0x10000001, "Increment primitive");
    require(decode_glyph(0x82, 0xa0).index == 0x1a2, "Glyph primitive");
    Glyph unchanged{17, 9}; const std::uint8_t lead = 0x82;
    require(!read_glyph(&lead, 1, unchanged) && unchanged.index == 17 && unchanged.consumed == 9, "Bounded glyph adapter");
    std::cout << "{\"test\":\"recovered_primitives_smoke\",\"checks\":4,\"passed\":true}\n";
}
void menu_primitive_tests() {
    using namespace dokodemo::recovered;
    uint32_t value=7;
    require(menu_repeat_94f90(0,10,0,value)==MenuStatus::ok && value==1,
        "Counter zero must return before DIV/BREAK");
    value=7;
    require(menu_repeat_94f90(10,10,0,value)==MenuStatus::division_zero_barrier && value==7,
        "Reached division-zero guard must retain output");
    std::array<uint8_t,2> bits{{0x54,0x80}};
    require(menu_bitset_4184({bits.data(),bits.size()},1,0xfffffff8u,1)==MenuStatus::ok && bits[0]==0x55,
        "Mapped negative profile index");
    const auto before=bits;
    require(menu_bitset_4184({bits.data(),bits.size()},0,0xfffffff8u,1)!=MenuStatus::ok && bits==before,
        "Unmapped profile write must be atomic");
    std::array<uint8_t,0x2c> host{};
    host[0x20]=2;host[0x24]=4;
    require(menu_commit_17b84_requests3_4({host.data(),host.size()})==MenuStatus::ok && host[0x1c]==2 && host[0x20]==4 && host[0x28]==6,
        "Call-free requested4 fixture commit");
    require(menu_request_19aa8({host.data(),host.size()},0x12345678,0xab,host.data()+0x24)==MenuStatus::ok && host[0x24]==0x78 && host[0x28]==2,
        "Event/host alias final store order");
    std::cout << "{\"test\":\"recovered_menu_smoke\",\"checks\":6,\"original_menu_route\":false,\"passed\":true}\n";
}
void calendar_primitive_tests() {
    using namespace dokodemo::recovered;
    auto record=CalendarRecord689B8({2004,11,16,0,0});
    require(record==CalendarRecord{{0xd4,0x07,11,16,0,0,0}}, "Original narrow calendar record layout");
    require(CalendarScalar7C660(record)==0x00272220u, "Original startup tuple scalar");
    const auto value=CalendarScalar7C580(record);
    record[6]=0xff;
    require(CalendarScalar7C580(record)==value, "Calendar arithmetic must not consume caller byte6");
    const auto narrowed=CalendarRecord689B8({-1,256,-1,255,256});
    require(narrowed==CalendarRecord{{0xff,0xff,0,0xff,0xff,0,0}}, "Calendar field widths are not civil-date normalization");
    std::cout << "{\"test\":\"recovered_calendar_smoke\",\"checks\":4,\"host_clock_substituted\":false,\"passed\":true}\n";
}
class Service47SmokeCells final : public dokodemo::recovered::Service47Cells {
public:
    std::vector<dokodemo::recovered::Service47Cell> slots = std::vector<dokodemo::recovered::Service47Cell>(4);
    dokodemo::recovered::Service47Cell load(dokodemo::recovered::Service47Address a) override {
        require(a.table==dokodemo::recovered::Service47Address::Table::Stack, "Unmapped service47 smoke table");
        return slots.at(a.index);
    }
    void store(dokodemo::recovered::Service47Address a, dokodemo::recovered::Service47Cell v) override {
        require(a.table==dokodemo::recovered::Service47Address::Table::Stack, "Unmapped service47 smoke table");
        slots.at(a.index)=v;
    }
};
void service47_primitive_tests() {
    using namespace dokodemo::recovered;
    Service47SmokeCells cells;
    const Service47Address dst{Service47Address::Table::Stack,0};
    Service47Fixture empty;
    empty.enabled=empty.control_cell_mapped=empty.guard_cell_mapped=empty.state_cell_mapped=empty.memory_disjoint=true;
    auto state=Service47InitialState(empty);
    cells.slots[0].word=0x2000002A;
    Service47Host1C958(cells,dst,empty,state);
    require(state.calls==1 && state.guard==0 && state.state==0xFFFF && !state.format_called
        && cells.slots[0].word==0x2000002A, "Mapped NULL-control service47 cleanup must not touch destination");
    Service47Fixture ready=empty;
    ready.control_present=ready.control_word_mapped=ready.source_present=ready.source_header_mapped=true;
    ready.status_cell_mapped=ready.format_window_mapped=true;
    ready.source_magic=0x0011076A; ready.status=0; ready.guard=1;
    ready.format_bytes={1,0,0,0x90,0x20,0x22,0x27,0x30};
    cells.slots[0].word=0x90000001; cells.slots[1].word=0x20000001;
    state=Service47InitialState(ready);
    Service47Host1C958(cells,dst,ready,state);
    require(state.calls==1 && state.format_called && state.format_return==0 && state.cursor_bytes==8
        && state.decoded_words==2 && state.guard==0 && state.state==0xFFFF && cells.slots[1].word==0x30272220,
        "Mapped array/scalar cursor must preserve tagged word and clear guard/state after decode");
    Service47Fixture mismatch=ready;
    mismatch.format_bytes={0x2A,0,0,0x20}; cells.slots[0].word=0x90000001;
    state=Service47InitialState(mismatch);
    Service47Host1C958(cells,dst,mismatch,state);
    require(state.format_called && state.format_return==-1 && state.calls==1 && cells.slots[0].word==0x90000001
        && state.state==0xFFFF, "Original cursor -1 is discarded by host while cleanup still occurs");
    bool barrier=false;
    state=Service47InitialState(ready);
    Service47Fixture owned=ready; owned.format_bytes={1,0,0,0x60}; cells.slots[0].word=0x20000001;
    try { Service47Host1C958(cells,dst,owned,state); }
    catch (const std::runtime_error&) { barrier=true; }
    require(barrier && cells.slots[0].word==0x20000001 && state.calls==0,
        "Unproved owned source must raise a native barrier rather than fake copy/release");
    std::cout << "{\"test\":\"recovered_service47_smoke\",\"checks\":4,\"actual_buffer_available\":false,\"passed\":true}\n";
}
class BulkSmokeCells final : public dokodemo::recovered::BulkCells {
public:
    std::vector<dokodemo::recovered::BulkCell> stack = std::vector<dokodemo::recovered::BulkCell>(4);
    std::vector<dokodemo::recovered::BulkCell> globals = std::vector<dokodemo::recovered::BulkCell>(12);
    std::vector<dokodemo::recovered::BulkCell> constants = std::vector<dokodemo::recovered::BulkCell>(4);
    dokodemo::recovered::BulkCell load(dokodemo::recovered::BulkAddress address) override {
        using Table=dokodemo::recovered::BulkAddress::Table;
        if(address.table==Table::Stack) return stack.at(address.index);
        if(address.table==Table::Global) return globals.at(address.index);
        if(address.table==Table::Ctx28) return constants.at(address.index);
        throw std::runtime_error("Unmapped bulk smoke table");
    }
    void store(dokodemo::recovered::BulkAddress address,dokodemo::recovered::BulkCell value) override {
        using Table=dokodemo::recovered::BulkAddress::Table;
        if(address.table==Table::Stack) stack.at(address.index)=value;
        else if(address.table==Table::Global) globals.at(address.index)=value;
        else throw std::runtime_error("Readonly or unmapped bulk smoke table");
    }
};
void bulk_primitive_tests() {
    using namespace dokodemo::recovered;
    using Table=BulkAddress::Table;
    BulkSmokeCells cells;
    cells.stack[2].word=0x2000000D;
    BulkScalar7627C(cells,{Table::Stack,2},5,2);
    require(cells.stack[2].word==0x20000005,"Source AND scalar writes exact integer tag/payload");
    BulkCell norm; norm.word=0x40000000u|(7646u<<14)|5259u;
    require(BulkNumeric(norm)==9280,"Source float-rounded packed norm differs from integer sqrt");
    cells.globals[0].word=0x90000002;
    cells.globals[1]={0xB0000000,true,{Table::Global,4}};
    cells.globals[2]={0xB0000000,true,{Table::Global,8}};
    cells.globals[4].word=cells.globals[8].word=0x90000002;
    cells.globals[5].word=0x20000003;cells.globals[6].word=0x2000000D;
    cells.globals[9].word=0x20000007;cells.globals[10].word=0x2000000F;
    cells.constants[0].word=0x90000003;cells.constants[1].word=0x20000001;
    cells.constants[2].word=0x20000002;cells.constants[3].word=0x20000005;
    cells.stack[0]={0xB0000000,true,{Table::Global,0}};
    cells.stack[1]={0xB0000000,true,{Table::Ctx28,0}};
    require(BulkArray7773C(cells,{Table::Stack,0},{Table::Stack,1})==0
        &&cells.globals[6].word==0x20000005&&cells.globals[10].word==0x20000005
        &&cells.globals[5].word==0x20000003&&cells.globals[9].word==0x20000007
        &&cells.constants[2].word==0x20000002,"Whole mapped target array uses readonly triple without unrelated writes");
    cells.constants[2].word=0x20000000;cells.globals[6].word=0x60000001;
    require(BulkArray7773C(cells,{Table::Stack,0},{Table::Stack,1})==0&&cells.globals[6].word==0x20000005,
        "Copy replaces old owned-tag destination without invented release");
    cells.constants[2].word=0x20000002;cells.globals[6].word=0x60000001;
    bool cut=false;
    try {BulkArray7773C(cells,{Table::Stack,0},{Table::Stack,1});}
    catch(const std::runtime_error&){cut=true;}
    require(cut&&cells.globals[6].word==0x60000001,"Unproved string conversion cuts before scalar store");
    std::cout<<"{\"test\":\"recovered_bulk_smoke\",\"checks\":5,\"actual_host_available\":false,\"passed\":true}\n";
}
void manager_queue_tests() {
    using namespace dokodemo::recovered;
    auto made=OwnedManagerQueue::Create(15,32);
    require(made.manager&&!made.boundary,"Source-layout manager owns real native allocations");
    auto& manager=*made.manager;
    const auto initial=manager.Snapshot();
    uint32_t bytes=0;
    for(const auto& allocation:initial.allocations) bytes+=allocation.bytes;
    require(bytes==initial.allocated_bytes&&bytes==944&&initial.anchor_count==15
        &&initial.command_capacity==32&&initial.queue_cursor_bytes==0,"Initialized manager storage sizes and empty linear queue");
    auto registry=manager.CreateRegistry(3);
    auto first=manager.RegisterCallback(registry.token,1,{0x23424,0},0x202);
    auto second=manager.RegisterCallback(registry.token,1,{0x23424,0},0x202);
    require(registry.accepted&&registry.enqueued&&first.accepted&&first.enqueued&&second.accepted&&second.enqueued,
        "Registry and callback commands are enqueued in owned stable storage");
    const auto drained=manager.Drain();
    const auto linked=manager.Snapshot();
    require(drained.cursor_reset&&drained.commands_processed==3
        &&drained.boundary.kind==ManagerStopKind::DirectCallback&&drained.boundary.source_pc==0x930D0
        &&linked.terminal&&linked.queue_cursor_bytes==0
        &&linked.registries.at(0).occupied==2,"Closed command6 effects complete before explicit callback boundary");
    const auto a=std::find(linked.event_list_order.begin(),linked.event_list_order.end(),first.token);
    const auto b=std::find(linked.event_list_order.begin(),linked.event_list_order.end(),second.token);
    require(a!=linked.event_list_order.end()&&b!=linked.event_list_order.end()&&a<b,
        "Same-event command6 insertion is FIFO through reread successor prev");
    require(!manager.Enqueue6(first.token,1).accepted,"Unknown callback boundary is terminal without invented return");
    auto tiny=OwnedManagerQueue::Create(15,1);
    require(bool(tiny.manager),"Small bounded command buffer allocated");
    auto tiny_registry=tiny.manager->CreateRegistry(1);
    auto dropped=tiny.manager->RegisterCallback(tiny_registry.token,1,{0x23424,0},0x202);
    require(dropped.accepted&&dropped.overflow_dropped&&!dropped.enqueued
        &&tiny.manager->Snapshot().registries.at(0).occupied==1,
        "Silent queue overflow preserves already occupied registration slot");
    auto failed=OwnedManagerQueue::Create(15,32,{0});
    require(!failed.manager&&failed.boundary.kind==ManagerStopKind::NativeAllocationFailure,
        "Native allocation failure is a structured boundary rather than PSP null-store equivalence");
    std::cout<<"{\"test\":\"owned_manager_queue_smoke\",\"checks\":9,\"actual_psp_manager_available\":false,\"passed\":true}\n";
}
void owned_string_tests() {
    using namespace dokodemo::recovered;
    StringArena unavailable;
    require(!unavailable.ready(), "Default storage does not imply original string availability");
    StringFixture fixture;
    fixture.enabled=fixture.bss_window_mapped=fixture.heap_ready=true;
    fixture.memory_disjoint=fixture.allocator_policy_known=true;
    fixture.initial_heap.resize(fixture.heap_size,0xCC);
    fixture.bss_window.resize(16,0);
    fixture.bss_window[0]='a'; fixture.bss_window[1]='b'; fixture.bss_window[2]='c';
    StringArena arena(fixture);
    uint32_t result=0;
    StringService20Selector0(arena,fixture,0,result);
    const auto pointer=arena.pointer(result);
    require((result>>28)==6 && arena.bytes(pointer,4)==Bytes({'a','b','c',0})
        && arena.live_blocks().size()==1, "Joined selector0 owns exact NUL-terminated source bytes");
    StringArena preserved=arena;
    const auto digest=preserved.summary().sha256;
    arena.release7515C(result);
    require(result==0 && arena.live_blocks().empty() && preserved.live_blocks().size()==1
        && preserved.summary().sha256==digest,
        "Protective candidate storage has isolated copy-on-write lifetime");
    uint32_t borrowed=0x50000001;
    const auto live=preserved.live_blocks().size();
    preserved.release7515C(borrowed);
    require(borrowed==0 && preserved.live_blocks().size()==live,
        "Borrowed tag5 clear does not free unrelated owned storage");
    StringArena aliased(fixture);
    const auto original=aliased.allocate(64);
    Bytes text(64,'x');text.back()=0;aliased.store(original,text);
    auto value=aliased.owned_word(original);
    aliased.assign753E0(value,original+32);
    require(aliased.pointer(value)==original &&aliased.allocation_size(original)==32
        &&aliased.bytes(original,2)==Bytes({0x20,0}) &&aliased.bytes(original+2,1)==Bytes({'x'}),
        "Interior alias reads realloc's current free-header bytes before strcpy, rather than a captured suffix");
    std::cout<<"{\"test\":\"owned_string_smoke\",\"checks\":5,\"actual_source_string_available\":false,\"passed\":true}\n";
}
void context_owner_tests() {
    using namespace dokodemo::recovered;
    auto created=OwnedContextOwner::Create();
    require(created.owner && created.boundary.kind==ContextOwnerStop::InitializationCall
        &&created.boundary.source_pc==0x2318C &&created.boundary.callee==0x1B188,
        "Real callback owner remains stopped before unresolved initialization");
    auto& owner=*created.owner;
    const auto initial=owner.Snapshot();
    require(initial.blocks.size()==2 && initial.blocks[0].bytes.size()==96
        &&initial.blocks[1].bytes.size()==164 &&initial.blocks[0].initialized.at(0x2C)==0,
        "96-byte callback owner is distinct from registry28 and has an unknown pre-tail budget");
    ContextOwnerCallbackInputs inputs; inputs.gate_segment1_10fd4={true,0};
    inputs.kernel_context_segment1_e5880={true,0};
    require(owner.InvokeCallback23424(inputs).kind==ContextOwnerStop::UninitializedField,
        "Unknown owner budget is not silently supplied");
    require(owner.ApplyInitializationTailFromCheckpoint().kind==ContextOwnerStop::None,
        "Explicit post-callee checkpoint applies only the proved initialization suffix");
    auto made=OwnedManagerQueue::Create(15,32);
    auto registry=made.manager->CreateRegistry(5,2);
    for(const auto& record:OwnedContextOwner::CallbackRecords()) {
        auto entry=made.manager->RegisterCallback(registry.token,record.event_index,record.descriptor,record.mask);
        require(entry.accepted&&entry.enqueued,"Five exact source records are enqueued");
    }
    const auto drain=made.manager->Drain();
    require(owner.BindManagerRegistry(*made.manager,registry.token).kind==ContextOwnerStop::None
        &&owner.Snapshot().bound_records_match,
        "Owned96 identity binds an exact distinct live base projection");
    auto wrong=drain.boundary; wrong.descriptor.function=0x2345C;
    require(owner.ContinueManagerCallback(*made.manager,wrong,inputs).kind==ContextOwnerStop::NativeDomain,
        "Logical identity binding rejects a different callback");
    const auto kernel=owner.ContinueManagerCallback(*made.manager,drain.boundary,inputs);
    require(kernel.kind==ContextOwnerStop::KernelCall &&kernel.source_pc==0x74DBC
        &&kernel.callee==0xCB288 &&kernel.scheduler_budget_low==2000 &&kernel.scheduler_budget_high==0
        &&made.manager->Snapshot().allocated_bytes==1136,
        "Bound callback preserves the manager and reaches the actual kernel cut");
    owner.Close();
    require(owner.Snapshot().blocks.empty() &&owner.InvokeCallback23424(inputs).kind==ContextOwnerStop::Closed,
        "Closed native owner cannot be used through a stale logical binding");
    std::cout<<"{\"test\":\"owned_context_owner_smoke\",\"checks\":8,\"actual_psp_owner_available\":false,\"passed\":true}\n";
}
void service48_owned_tests() {
    using namespace dokodemo::recovered;
    StringFixture heap;
    heap.enabled=heap.heap_ready=heap.memory_disjoint=heap.allocator_policy_known=true;
    heap.heap_base=0x600000;
    heap.initial_heap.assign(heap.heap_size,0xCC);
    StringArena arena(heap);
    auto record=OwnedService48Record::Create(arena);
    require(arena.word(record.pointer())==0 && arena.word(record.pointer()+4)==0xFFFFFFFF,
        "Owned nonNULL record has the exact two source constructor fields");
    record.SupplyLoadedPayload(arena,{0x2A,0,0,0x20});
    const auto preserved=arena;
    Service47SmokeCells cells;
    cells.slots[0].word=0x20000001;
    Service48Fixture fixture;
    fixture.enabled=fixture.manager_cell_mapped=fixture.manager_record_mapped=true;
    fixture.singleton_mapped=fixture.arena_pointer_mapped=fixture.memory_disjoint=true;
    fixture.manager_stable_during_call=fixture.pool_pointer_cell_mapped=true;
    Service48State state;state.manager_record=record.pointer();
    const auto completed=Service48Host1C9E4(cells,{Service47Address::Table::Stack,0},fixture,state,arena);
    require(completed.completed && !completed.boundary && cells.slots[0].word==0x2000002A
        &&state.completed_calls==1 &&state.heap_release_calls==2 &&state.manager_record==0
        &&state.payload_clears==1 &&state.record_releases==1 &&state.manager_clears==1
        &&arena.live_blocks().empty(),
        "NonNULL service48 executes parser and releases both actual owned allocations");
    require(preserved.live_blocks().size()==2 &&preserved.word(record.pointer())!=0,
        "Joined record cleanup leaves a COW pre-call arena intact");
    const auto again=Service48Host1C9E4(cells,{Service47Address::Table::Stack,0},fixture,state,arena);
    require(again.completed &&state.completed_calls==2 &&state.heap_release_calls==2,
        "Repeated call observes cleared current manager rather than a fresh record");
    auto refused=preserved;state={};state.manager_record=record.pointer();fixture.pool_pointer_cell_mapped=false;
    const auto boundary=Service48Host1C9E4(cells,{Service47Address::Table::Stack,0},fixture,state,refused);
    require(!boundary.completed &&boundary.boundary.kind==Service48Boundary::Kind::NativeDomain
        &&state.destination_registered &&refused.live_blocks().size()==2,
        "Required pool-cell mapping refusal preserves supported prefix effects separately");
    std::cout<<"{\"test\":\"service48_owned_smoke\",\"checks\":5,\"actual_source_record_available\":false,\"passed\":true}\n";
}
void service39_current_storage_tests() {
    using namespace dokodemo::recovered;
    StringFixture fixture;
    fixture.enabled=fixture.heap_ready=fixture.memory_disjoint=fixture.allocator_policy_known=true;
    fixture.initial_heap.assign(fixture.heap_size,0xCC);
    StringArena arena(fixture);
    uint32_t source_word=0;
    arena.assign_mapped753E0(source_word,{'T','O','R','O',0,0,0,0});
    const auto preserved=arena;
    const auto original_heap=arena.heap_bytes(),original_control=arena.control_bytes();
    Service39Input input;
    input.enabled=input.reference_readable=input.router_context_readable=true;
    input.reference={Service39Space::Stack,0x200000,8};
    Service39Mapping reference;reference.address=0x200008;
    for(unsigned i=0;i<4;++i) reference.bytes.push_back(static_cast<uint8_t>(source_word>>(i*8)));
    input.mappings.push_back(reference);
    uint32_t result=0;
    const auto observation=Service39Apply(arena,input,result);
    require(observation.reference.space==Service39Space::Stack&&observation.reference_word==source_word
        &&result>>28==6&&arena.bytes(arena.pointer(result),5)==Bytes({'T','O','R','O',0}),
        "Service39 resolves a current table-aware owned string into real independent TEMP storage");
    require(preserved.heap_bytes()==original_heap&&preserved.control_bytes()==original_control,
        "Service39 COW mutation preserves the earlier live physical heap");
    arena.release7515C(result);
    require(result==0&&arena.live_blocks().size()==1
        &&arena.bytes(arena.pointer(source_word),5)==Bytes({'T','O','R','O',0}),
        "Releasing service39 TEMP retains the original owned input storage");
    const auto guarded_heap=arena.heap_bytes(),guarded_control=arena.control_bytes();
    result=0x2000007B;input.result_address=fixture.heap_base;
    bool refused=false;
    try { Service39Apply(arena,input,result); } catch(const std::runtime_error&) { refused=true; }
    require(refused&&result==0x2000007B&&arena.heap_bytes()==guarded_heap&&arena.control_bytes()==guarded_control,
        "Physical result addresses refuse before mutation under the precise service39 result domain");
    std::cout<<"{\"test\":\"service39_current_storage_smoke\",\"checks\":4,\"actual_producer_available\":false,\"passed\":true}\n";
}
void clock_boundary_tests() {
    using namespace dokodemo::recovered;
    ClockDependency dependency;
    require(!dependency.available(),"Clock callback mapping is not inferred from construction");
    unsigned reads=0;
    dependency.provider=[&reads]() { ++reads; return ClockCallbackReturn{true,UINT64_C(63082281600000000)}; };
    require(!dependency.available(),"Supplying a callback alone does not install source getter premises");
    dependency.callback_slot_mapped=dependency.installed_getter_returns_cache=true;
    const auto cached=dependency.provider();
    require(dependency.available()&&cached.returned&&reads==1&&ClockWord689B8(cached.cached_psp_tick_us)==0x30000000,
        "Clock uses already shifted cached tick and source minute2000 anchor");
    require(ClockWord689B8(cached.cached_psp_tick_us-1)==0x3FFFFFFF
        &&ClockWord689B8(cached.cached_psp_tick_us+60000000)==0x30000001,
        "Clock division/unsigned wrap/tag low28 preserves minute boundary");
    require(ClockScalar7C694(0)==0xC15558A0&&ClockWord689B8(0)==0x315558A0,
        "Literal raw tick0 is a boundary input, not a fabricated current date");
    std::cout<<"{\"test\":\"cached_clock_boundary_smoke\",\"checks\":5,\"RTC_epoch_firmware_verified\":false,\"passed\":true}\n";
}
void producer_backing_tests() {
    using namespace dokodemo::recovered;
    auto unavailable=OwnedProducer::Create({});
    require(unavailable.owner&&unavailable.owner->Snapshot().live_blocks.empty()
        &&unavailable.initialization.boundary.kind==ProducerStop::MissingInput,
        "Producer does not invent readable F6980 input");
    auto made=OwnedProducer::Create({true,0});
    require(made.owner&&made.initialization.completed&&made.owner->Snapshot().live_blocks.size()==1
        &&made.owner->Snapshot().live_blocks[0].bytes.size()==108,
        "Concrete1B188 payload has actual108-byte owned backing distinct from96-byte callback owner");
    auto& producer=*made.owner;
    bool selected=false;uint8_t flag=0;
    require(producer.SelectionIsZero1B214(selected)&&selected&&producer.SelectionFlag1B228(flag)&&flag==0,
        "Fresh producer initializes source selection and byte flag");
    require(producer.SetSelection1B2DC(5,0xA5).completed&&producer.SelectionIsZero1B214(selected)&&!selected
        &&producer.SelectionFlag1B228(flag)&&flag==0xA5,
        "Source selection setters/getters are linked to current backing");
    ProducerAllocationPlan repeat;repeat.source_address=0x10000200;
    require(producer.Initialize1B188(repeat).completed&&producer.Snapshot().live_blocks.size()==2,
        "Repeat source initialization preserves the earlier orphan until native cleanup");
    require(producer.Release1B578().completed&&producer.Snapshot().live_blocks.size()==1
        &&!producer.SelectionIsZero1B214(selected),
        "Source freshchild0 release clears publication but retains the original orphan");
    producer.Close();
    require(producer.Snapshot().closed&&producer.Snapshot().live_blocks.empty(),
        "Native RAII closes remaining source orphans without claiming PSP destructor equivalence");
    std::cout<<"{\"test\":\"owned_producer_backing_smoke\",\"checks\":7,\"actual_game_singleton_available\":false,\"passed\":true}\n";
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: core-tests <assets/native directory>");
        hash_tests(); cp932_tests(); program_tests(argv[1]); model_tests(argv[1]); primitive_tests(); menu_primitive_tests(); calendar_primitive_tests(); service47_primitive_tests(); bulk_primitive_tests(); manager_queue_tests(); owned_string_tests(); context_owner_tests(); service48_owned_tests(); service39_current_storage_tests(); clock_boundary_tests(); producer_backing_tests();
        std::cout << "{\"test\":\"portable_core\",\"passed\":true}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
