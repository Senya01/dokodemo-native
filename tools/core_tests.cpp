// SPDX-License-Identifier: MIT
// Portable headless integration checks. No platform API headers or link libraries.
#include "core_hash.h"
#include "core/cp932.h"
#include "script_vm.h"
#include "startup_probe.h"
#include "model_formats.hpp"
#include "recovered/game_primitives.h"
#include "recovered/menu_primitives.h"
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
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: core-tests <assets/native directory>");
        hash_tests(); cp932_tests(); program_tests(argv[1]); model_tests(argv[1]); primitive_tests(); menu_primitive_tests();
        std::cout << "{\"test\":\"portable_core\",\"passed\":true}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
