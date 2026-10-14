#include "startup_probe.h"
#include "recovered/calendar_native.h"
#include "recovered/bulk_array_native.h"
#include "core_hash.h"
#ifdef DOKODEMO_STARTUP_VERIFY_MAIN
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace dokodemo;
namespace dokodemo::startup {
// Only the verification executable can supply synthetic state. Production
// startup callers retain the immutable-original Program/host input contract.
struct StartupTestAccess {
    static Snapshot initial(StartupProbe& probe, uint32_t pc, int32_t sp,
                            const std::array<Value, 12>& slots, Value result) {
        probe.context_ = {}; probe.context_.pc = pc; probe.context_.sp = sp;
        probe.context_.result = result;
        std::copy(slots.begin(), slots.end(), probe.context_.slots.begin());
        probe.status_ = Status::Running; probe.events_.clear(); probe.first_builtin_ = {};
        return probe.snapshot(probe.context_);
    }
    static Snapshot set_top(StartupProbe& probe, Value left, Value right) {
        if (probe.context_.sp >= 2) {
            probe.context_.slots[static_cast<size_t>(probe.context_.sp - 2)] = left;
            probe.context_.slots[static_cast<size_t>(probe.context_.sp - 1)] = right;
        }
        return probe.snapshot(probe.context_);
    }
    static std::array<Value, 12> slots(const StartupProbe& probe) {
        std::array<Value, 12> result;
        std::copy_n(probe.context_.slots.begin(), result.size(), result.begin());
        return result;
    }
    static Snapshot iterator_initial(StartupProbe& probe, uint32_t pc, int32_t sp, int32_t fp,
                                     const std::vector<Value>& slots, Value result,
                                     const std::map<uint32_t, Value>& writes) {
        probe.context_ = {}; probe.context_.pc = pc; probe.context_.sp = sp; probe.context_.fp = fp;
        probe.context_.result = result; probe.context_.global_writes = writes;
        std::copy(slots.begin(), slots.end(), probe.context_.slots.begin());
        probe.status_ = Status::Running; probe.events_.clear(); probe.first_builtin_ = {};
        return probe.snapshot(probe.context_);
    }
    static const std::array<Value, 512>& all_slots(const StartupProbe& probe) { return probe.context_.slots; }
    static void set_strings(StartupProbe& probe,recovered::StringArena arena) {probe.context_.strings=std::move(arena);}
    static void discard_events(StartupProbe& probe) {probe.events_.clear();}
    static Snapshot set_service(StartupProbe& probe, recovered::Service47State state) {
        probe.context_.service47=state;return probe.snapshot(probe.context_);
    }
    static Snapshot repeat47(StartupProbe& probe, uint32_t pc) {
        probe.context_.pc = pc; probe.context_.sp = 38;
        probe.context_.slots[36] = Value::integer(47); probe.context_.slots[37] = Value::stack_reference(8);
        probe.status_ = Status::Running;
        return probe.snapshot(probe.context_);
    }
    static Snapshot chain_initial(StartupProbe& probe, uint32_t pc, int32_t sp, int32_t fp,
                                  const std::vector<Value>& slots, Value result,
                                  const std::map<uint32_t, Value>& globals,
                                  const std::map<uint32_t, Value>& locals,
                                  const std::map<uint32_t, Value>& bindings) {
        iterator_initial(probe, pc, sp, fp, slots, result, globals);
        probe.context_.local_writes = locals; probe.context_.host_reference_bindings = bindings;
        probe.context_.service47 = recovered::Service47InitialState(probe.service47_fixture_);
        return probe.snapshot(probe.context_);
    }
    static bool malformed_branch_halts(StartupProbe& probe, uint32_t pc, uint32_t size) {
        auto candidate = probe.context_;
        candidate.pc = pc; candidate.sp = 1; candidate.slots[0] = {};
        script::Command command; command.pc = pc; command.size = size; command.opcode = 0x11;
        BuiltinCall builtin;
        try { probe.execute(candidate, command, builtin); }
        catch (...) { return true; } // run()'s candidate is never committed.
        return false;
    }
};
}

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::string hexadecimal(uint32_t number, unsigned width = 0) {
    std::ostringstream out; out << "0x" << std::uppercase << std::hex << std::setfill('0') <<
        std::setw(width) << number; return out.str();
}
int32_t signed_payload(uint32_t word) {
    uint32_t value = word & 0x0FFFFFFF;
    return value & 0x08000000 ? static_cast<int32_t>(value) - 0x10000000 : static_cast<int32_t>(value);
}
void value_json(const startup::Value& value) {
    if (value.kind == startup::Value::Kind::Word) {
        std::cout << "{\"raw\":\"" << hexadecimal(value.word, 8) << "\",\"tag\":" << (value.word >> 28)
                  << ",\"signed_payload\":" << signed_payload(value.word) << "}";
    } else {
        std::cout << "{\"tag\":11,\"symbolic_native_reference\":true,\"table\":\""
                  << (value.kind == startup::Value::Kind::StackReference ? "stack" :
                      value.kind == startup::Value::Kind::LocalReference ? "local" :
                      value.kind == startup::Value::Kind::Ctx28Reference ? "ctx28" : "global")
                  << "\",\"index\":" << value.index << ",\"source_file_offset\":";
        if (value.source_file_offset == UINT32_MAX) std::cout << "null";
        else std::cout << value.source_file_offset;
        std::cout << "}";
    }
}
void snapshot_json(const startup::Snapshot& snapshot) {
    std::cout << "{\"pc\":\"" << hexadecimal(snapshot.pc) << "\",\"sp\":" << snapshot.sp << ",\"fp\":" << snapshot.fp
              << ",\"saved_fp\":" << snapshot.saved_fp << ",\"saved_sp\":" << snapshot.saved_sp
              << ",\"state\":" << snapshot.state << ",\"result\":";
    value_json(snapshot.result);
    std::cout << ",\"stack_window\":[";
    for (size_t i = 0; i < snapshot.stack_window.size(); ++i) {
        if (i) std::cout << ',';
        value_json(snapshot.stack_window[i]);
    }
    std::cout << "],\"timer_base_set_at_pc\":";
    if (snapshot.timer_base_set) std::cout << '"' << hexadecimal(snapshot.timer_base_pc) << '"';
    else std::cout << "null";
    std::cout << ",\"host_state\":{\"callback_F0_service_78_flag_offset_104\":";
    if (snapshot.host_flag_104_known) std::cout << unsigned(snapshot.host_flag_104);
    else std::cout << "null";
    std::cout << ",\"reference_bindings\":{";
    bool binding_first = true;
    for (const auto& entry : snapshot.host_reference_bindings) {
        if (!binding_first) std::cout << ',';
        binding_first = false;
        std::cout << '"' << entry.first << "\":"; value_json(entry.second);
    }
    std::cout << '}';
    if (snapshot.service47.calls) {
        const auto& s = snapshot.service47;
        std::cout << ",\"service47\":{\"known\":true,\"control_present\":" << (s.control_present ? "true" : "false")
                  << ",\"source_present\":" << (s.source_present ? "true" : "false") << ",\"guard\":" << unsigned(s.guard)
                  << ",\"state\":" << s.state << ",\"calls\":" << s.calls << ",\"cursor_bytes\":" << s.cursor_bytes
                  << ",\"decoded_words\":" << s.decoded_words << ",\"format_called\":" << (s.format_called ? "true" : "false")
                  << ",\"format_return\":" << s.format_return << ",\"null_free_calls\":" << s.null_free_calls
                  << ",\"released_entities\":" << s.released_entities << '}';
    }
    if(snapshot.string_arena.known) {
        const auto& s=snapshot.string_arena;
        std::cout<<",\"string_arena\":{\"known\":true,\"allocations\":"<<s.allocations
            <<",\"reallocations\":"<<s.reallocations<<",\"releases\":"<<s.releases<<",\"copies\":"<<s.copies
            <<",\"live_allocations\":"<<s.live_allocations<<",\"conversion_cursor\":"<<s.conversion_cursor
            <<",\"sha256\":\""<<s.sha256<<"\"}";
    }
    if(snapshot.general_arena.known) {
        const auto& a=snapshot.general_arena;const auto& s=snapshot.service48;
        std::cout<<",\"general_arena\":{\"known\":true,\"allocations\":"<<a.allocations<<",\"reallocations\":"<<a.reallocations
            <<",\"releases\":"<<a.releases<<",\"copies\":"<<a.copies<<",\"live_allocations\":"<<a.live_allocations
            <<",\"conversion_cursor\":"<<a.conversion_cursor<<",\"sha256\":\""<<a.sha256<<"\"}";
        std::cout<<",\"service48\":{\"manager_record\":"<<s.manager_record<<",\"destination_registered\":"<<(s.destination_registered?"true":"false")
            <<",\"registered_word\":"<<s.registered_word<<",\"format_called\":"<<(s.format_called?"true":"false")
            <<",\"cursor_bytes\":"<<s.cursor_bytes<<",\"decoded_words\":"<<s.decoded_words<<",\"format_return\":"<<s.format_return
            <<",\"payload_clears\":"<<s.payload_clears<<",\"record_releases\":"<<s.record_releases<<",\"manager_clears\":"<<s.manager_clears
            <<",\"completed_calls\":"<<s.completed_calls<<",\"heap_release_calls\":"<<s.heap_release_calls<<",\"null_release_calls\":"<<s.null_release_calls<<"}";
    }
    std::cout << "},\"global_writes_complete\":" << (snapshot.global_writes_complete ? "true" : "false")
              << ",\"global_writes_count\":" << snapshot.global_writes_count
              << ",\"global_writes_sha256\":\"" << snapshot.global_writes_sha256 << "\",\"global_writes\":{";
    bool first = true;
    for (const auto& entry : snapshot.global_writes) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '"' << entry.first << "\":"; value_json(entry.second);
    }
    std::cout << "},\"local_writes\":{";
    first = true;
    for (const auto& entry : snapshot.local_writes) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '"' << entry.first << "\":"; value_json(entry.second);
    }
    std::cout << "}}";
}
void builtin_json(const startup::BuiltinCall& builtin) {
    if (!builtin.valid) { std::cout << "null"; return; }
    std::cout << "{\"pc\":\"" << hexadecimal(builtin.pc) << "\",\"id\":" << unsigned(builtin.id)
              << ",\"argc\":" << unsigned(builtin.argc) << ",\"arguments\":[";
    for (size_t i = 0; i < builtin.arguments.size(); ++i) {
        if (i) std::cout << ',';
        value_json(builtin.arguments[i]);
    }
    std::cout << "],\"boot_handler_va\":\"" << hexadecimal(builtin.handler_va)
              << "\",\"host_service\":" << builtin.host_service << ",\"host_handler_va\":\""
              << hexadecimal(builtin.host_handler_va) << "\",\"result\":";
    value_json(builtin.result); std::cout << "}";
}
bool stream_events = false;
const startup::HostAbiProjection conditional_abi{true, true, true, true, true, 0};
void output(const char* name, const script::Program& program, const startup::Trace& trace) {
    std::cout << "{\"source_name\":\"" << name << ".KSC\",\"source_sha256\":\"" << program.source().sha256
              << "\",\"host_byte\":" << unsigned(trace.host_byte) << ",\"status\":\"" << startup::status_name(trace.status)
              << "\",\"executed_instructions\":" << trace.executed << ",\"halt\":";
    snapshot_json(trace.context);
    std::cout << ",\"halt_opcode\":" << trace.diagnostic.opcode << ",\"first_builtin\":";
    builtin_json(trace.first_builtin);
    std::cout << (stream_events ? "}\n" : ",\"events\":[");
    for (size_t i = 0; i < trace.events.size(); ++i) {
        if (i && !stream_events) std::cout << ',';
        const auto& event = trace.events[i];
        std::cout << "{\"step\":" << i << ",\"code_offset\":\"" << hexadecimal(event.pc)
                  << "\",\"file_offset\":\"" << hexadecimal(event.file_offset)
                  << "\",\"opcode\":\"" << hexadecimal(static_cast<uint32_t>(event.opcode), 2)
                  << "\",\"boot_handler_va\":\"" << hexadecimal(event.handler_va) << "\",\"raw_hex\":\"";
        const auto* command = program.command(event.pc);
        std::ostringstream bytes; bytes << std::hex << std::setfill('0');
        for (uint32_t at = 28 + event.pc; at < 28 + event.pc + command->size; ++at)
            bytes << std::setw(2) << unsigned(program.bytes()[at]);
        std::cout << bytes.str() << "\",\"before\":"; snapshot_json(event.before);
        std::cout << ",\"after\":"; snapshot_json(event.after);
        std::cout << ",\"builtin\":"; builtin_json(event.builtin);
        std::cout << ",\"global_write_delta\":{";
        bool first = true;
        for (const auto& entry : event.global_write_delta) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << '"' << entry.first << "\":"; value_json(entry.second);
        }
        std::cout << "}}";
        if (stream_events) std::cout << '\n';
    }
    if (!stream_events) std::cout << "]}\n";
}
bool same_context(const startup::Snapshot& a, const startup::Snapshot& b) {
    return a.pc == b.pc && a.sp == b.sp && a.fp == b.fp && a.saved_fp == b.saved_fp && a.saved_sp == b.saved_sp &&
        a.state == b.state && a.result == b.result && a.stack_window == b.stack_window &&
        a.timer_base_set == b.timer_base_set && a.timer_base_pc == b.timer_base_pc &&
        a.host_flag_104_known == b.host_flag_104_known && a.host_flag_104 == b.host_flag_104 &&
        a.host_reference_bindings == b.host_reference_bindings && a.service47 == b.service47 &&
        a.string_arena==b.string_arena && a.general_arena==b.general_arena &&
        a.service48.manager_record==b.service48.manager_record && a.service48.completed_calls==b.service48.completed_calls &&
        a.global_writes == b.global_writes && a.local_writes == b.local_writes && a.global_writes_complete == b.global_writes_complete &&
        a.global_writes_count == b.global_writes_count && a.global_writes_sha256 == b.global_writes_sha256;
}
startup::Value raw_value(uint32_t word) { startup::Value value; value.word = word; return value; }
uint32_t fixture_word(const startup::Value& value) {
    if (value.kind == startup::Value::Kind::Word) return value.word;
    require(value.kind == startup::Value::Kind::StackReference,
            "Unexpected fixture arena reference kind=" + std::to_string(static_cast<unsigned>(value.kind)) +
            " index=" + std::to_string(value.index));
    return 0xB0000000 | value.index * 4;
}
void original_oracle_checks(const std::filesystem::path& vectors, const script::Program& program) {
    std::ifstream stream(vectors, std::ios::binary);
    require(bool(stream), "Original oracle vectors missing");
    std::array<uint32_t, 24> record{};
    uint32_t count = 0, success = 0, barriers = 0;
    std::map<uint32_t, uint32_t> counts;
    while (stream.read(reinterpret_cast<char*>(record.data()), sizeof(record))) {
        const uint32_t op = record[0]; ++count; ++counts[op];
        uint32_t pc = op == 0x14 ? 0x1701 : op == 0xF5 ? 0x1708 : op == 0x53 ? 0x1710 : op == 0x51 ? 0x16FA :
            op == 0x11 ? 0x10 : 0x3E2;
        std::array<startup::Value, 12> slots{};
        slots[2] = raw_value(record[5]); slots[3] = raw_value(record[3]);
        slots[4] = raw_value(record[4]); slots[8] = startup::Value::integer(-9);
        if (op == 0x14 || op == 0xF5 || op == 0x51) {
            slots[6] = raw_value(record[1]); slots[7] = raw_value(record[2]);
            if (record[1] >> 28 == 0xB)
                slots[6] = startup::Value::stack_reference(op == 0xF5 ? 3 : record[6]);
            if (op == 0x14 && record[2] >> 28 == 0xB) slots[7] = startup::Value::stack_reference(3);
        } else {
            slots[7] = raw_value(record[1]);
            if (op == 0x53 && record[1] >> 28 == 0xB) slots[7] = startup::Value::stack_reference(2);
        }
        startup::StartupProbe probe(program, 0);
        const auto before = startup::StartupTestAccess::initial(probe, pc, 8, slots, raw_value(record[7]));
        const auto actual = probe.run(1);
        if (record[8]) {
            ++barriers;
            require(actual.status == startup::Status::Halted && actual.executed == 0 &&
                    same_context(before, actual.context), "Native oracle barrier changed state/advanced");
        } else {
            ++success;
            require(actual.status == startup::Status::BudgetExhausted && actual.executed == 1,
                    "Native stopped on successful original oracle vector");
            require(actual.pc == record[9] && static_cast<uint32_t>(actual.sp) == record[10] &&
                    fixture_word(actual.result) == record[11], "Native PC/SP/result != original machine words");
            const auto after_slots = startup::StartupTestAccess::slots(probe);
            for (size_t i = 0; i < 12; ++i)
                require(fixture_word(after_slots[i]) == record[12 + i],
                        "Native slot != original machine words");
        }
    }
    require(stream.eof() && stream.gcount() == 0, "Truncated oracle vector record");
    require(count == 10798 && success == 9547 && barriers == 1251, "Missing original vector cases");
    std::cout << "{\"original_oracle_checks\":\"pass\",\"vectors\":" << count << ",\"successes\":"
              << success << ",\"barriers\":" << barriers << "}\n";
}
void iterator_oracle_checks(const std::filesystem::path& vectors, const std::filesystem::path& base) {
    std::ifstream stream(vectors, std::ios::binary);
    require(bool(stream), "Iterator original-word vectors missing");
    char magic[8]{}; uint32_t expected_count = 0;
    stream.read(magic, 8); stream.read(reinterpret_cast<char*>(&expected_count), 4);
    require(std::string(magic, 7) == "DIT15V1" && expected_count > 0, "Iterator fixture schema");
    const char* names[] = {"INU", "KAERU", "NEKO", "ROBO", "USAGI"};
    std::array<script::Program, 5> programs;
    script::Diagnostic error;
    for (size_t i = 0; i < programs.size(); ++i)
        require(programs[i].load_original(base / (std::string(names[i]) + ".KSC"), error), error.text());
    uint32_t count = 0, success = 0, barriers = 0;
    for (; count < expected_count; ++count) {
        uint32_t bytes = 0; stream.read(reinterpret_cast<char*>(&bytes), 4);
        require(bool(stream) && bytes % 4 == 0 && bytes <= 65536, "Iterator record length");
        std::vector<uint32_t> fields(bytes / 4);
        stream.read(reinterpret_cast<char*>(fields.data()), bytes);
        require(bool(stream), "Iterator truncated record");
        size_t at = 0;
        const auto next = [&]() { require(at < fields.size(), "Iterator truncated field"); return fields[at++]; };
        const auto value = [&]() {
            startup::Value result; const uint32_t kind = next();
            require(kind <= 2, "Iterator invalid value kind"); result.kind = static_cast<startup::Value::Kind>(kind);
            result.word = next(); result.index = next(); result.source_file_offset = next(); return result;
        };
        const auto slots = [&]() {
            const uint32_t size = next(); require(size == 32, "Iterator slot fixture length");
            std::vector<startup::Value> result; for (uint32_t i = 0; i < size; ++i) result.push_back(value()); return result;
        };
        const auto writes = [&]() {
            const uint32_t size = next(); require(size <= 2048, "Iterator write fixture cap");
            std::map<uint32_t, startup::Value> result;
            for (uint32_t i = 0; i < size; ++i) { const uint32_t key = next(); result[key] = value(); }
            require(result.size() == size, "Iterator duplicate write keys"); return result;
        };
        const uint32_t source = next(), host = next(), pc = next();
        const int32_t sp = static_cast<int32_t>(next()), fp = static_cast<int32_t>(next());
        const uint32_t cap = next(); const auto prior = value(); const auto input_slots = slots(); const auto input_writes = writes();
        const uint32_t boundary = next(), expected_pc = next(); const int32_t expected_sp = static_cast<int32_t>(next());
        const auto expected_result = value(); const auto expected_slots = slots(); const auto expected_writes = writes();
        require(at == fields.size() && source < programs.size() && host <= 1 && cap <= 512, "Iterator record fields");
        startup::StartupProbe probe(programs[source], static_cast<uint8_t>(host), static_cast<uint16_t>(cap));
        const auto before = startup::StartupTestAccess::iterator_initial(probe, pc, sp, fp, input_slots, prior, input_writes);
        const auto before_all_slots = startup::StartupTestAccess::all_slots(probe);
        const auto actual = probe.run(1);
        const std::string label = "Iterator vector" + std::to_string(count);
        if (boundary) {
            ++barriers;
            require(actual.status == startup::Status::Halted && actual.executed == 0 &&
                    same_context(before, actual.context) && before_all_slots == startup::StartupTestAccess::all_slots(probe),
                    label + " barrier committed partial state");
        } else {
            ++success;
            require(actual.status == startup::Status::BudgetExhausted && actual.executed == 1,
                    label + " halted on original success: " + actual.diagnostic.reason);
            require(actual.pc == expected_pc && actual.sp == expected_sp && actual.fp == fp &&
                    actual.result == expected_result && actual.context.global_writes == expected_writes,
                    label + " PC/SP/FP/result/globals differ from original words");
            const auto& after = startup::StartupTestAccess::all_slots(probe);
            require(std::equal(expected_slots.begin(), expected_slots.end(), after.begin()), label + " stack differs");
            require(std::all_of(after.begin() + 32, after.end(), [](const startup::Value& v) { return v == startup::Value{}; }),
                    label + " wrote outside fixture slots");
        }
    }
    char extra = 0; require(!stream.read(&extra, 1) && stream.eof(), "Iterator extra fixture records");
    std::cout << "{\"iterator_original_word_checks\":\"pass\",\"vectors\":" << count
              << ",\"successes\":" << success << ",\"barriers\":" << barriers << "}\n";
}
void chain_oracle_checks(const std::filesystem::path& vectors, const std::filesystem::path& base, const char* check_name = "chain_original_word_checks") {
    std::ifstream stream(vectors, std::ios::binary); require(bool(stream), "Chain vectors missing");
    char magic[8]{}; uint32_t expected_count = 0;
    stream.read(magic, 8); stream.read(reinterpret_cast<char*>(&expected_count), 4);
    const bool bulk_vectors = std::string(magic, 6) == "DBK7V1";
    const bool service47_vectors = std::string(magic, 6) == "DS47V1" || bulk_vectors;
    require((std::string(magic, 6) == "DCH4V1" || service47_vectors) && expected_count > 0, "Chain fixture schema");
    const char* names[] = {"INU", "KAERU", "NEKO", "ROBO", "USAGI"};
    std::array<script::Program, 5> programs; script::Diagnostic error;
    for (size_t i = 0; i < programs.size(); ++i)
        require(programs[i].load_original(base / (std::string(names[i]) + ".KSC"), error), error.text());
    uint32_t success = 0, barriers = 0;
    for (uint32_t count = 0; count < expected_count; ++count) {
        uint32_t bytes = 0; stream.read(reinterpret_cast<char*>(&bytes), 4);
        require(bool(stream) && bytes % 4 == 0 && bytes <= 131072, "Chain record length");
        std::vector<uint32_t> fields(bytes / 4); stream.read(reinterpret_cast<char*>(fields.data()), bytes);
        require(bool(stream), "Chain truncated record"); size_t at = 0;
        const auto next = [&]() { require(at < fields.size(), "Chain truncated field"); return fields[at++]; };
        const auto value = [&]() {
            startup::Value result; const uint32_t kind = next(); require(kind <= (service47_vectors ? 4u : 3u), "Chain invalid kind");
            result.kind = static_cast<startup::Value::Kind>(kind); result.word = next();
            result.index = next(); result.source_file_offset = next(); return result;
        };
        const auto slots = [&]() {
            const uint32_t size = next(); require(size <= 512, "Chain slots cap");
            std::vector<startup::Value> result; for (uint32_t i = 0; i < size; ++i) result.push_back(value()); return result;
        };
        const auto writes = [&]() {
            const uint32_t size = next(); require(size <= 2048, "Chain writes cap");
            std::map<uint32_t, startup::Value> result;
            for (uint32_t i = 0; i < size; ++i) { const uint32_t key = next(); result[key] = value(); }
            require(result.size() == size, "Chain duplicate map key"); return result;
        };
        const uint32_t source = next(), host = next(), pc = next();
        const int32_t sp = static_cast<int32_t>(next()), fp = static_cast<int32_t>(next());
        const uint32_t cap = next(); const auto prior = value(); const auto input_slots = slots();
        const auto globals = writes(), locals = writes(), bindings = writes();
        recovered::Service47Fixture service_fixture;
        if (service47_vectors) {
            service_fixture.enabled = next() != 0; service_fixture.control_cell_mapped = next() != 0;
            service_fixture.guard_cell_mapped = next() != 0; service_fixture.state_cell_mapped = next() != 0;
            service_fixture.memory_disjoint = next() != 0; service_fixture.control_present = next() != 0;
            service_fixture.control_word_mapped = next() != 0; service_fixture.source_present = next() != 0;
            service_fixture.source_header_mapped = next() != 0; service_fixture.status_cell_mapped = next() != 0;
            service_fixture.format_window_mapped = next() != 0;
            service_fixture.source_magic = next(); service_fixture.status = next(); service_fixture.format_source_offset = next();
            service_fixture.guard = static_cast<uint8_t>(next()); service_fixture.state = static_cast<uint16_t>(next());
            service_fixture.depth_limit = next(); service_fixture.word_limit = next();
            const uint32_t length = next(); require(length <= 65536, "Service47 source fixture cap");
            for (uint32_t i = 0; i < length; ++i) service_fixture.format_bytes.push_back(static_cast<uint8_t>(next()));
        }
        const auto read_service = [&]() {
            recovered::Service47State s;
            s.known=next()!=0;s.control_present=next()!=0;s.source_present=next()!=0;s.guard=static_cast<uint8_t>(next());
            s.state=static_cast<uint16_t>(next());s.calls=next();s.cursor_bytes=next();s.decoded_words=next();s.format_called=next()!=0;
            s.format_return=static_cast<int32_t>(next());s.null_free_calls=next();s.released_entities=next();return s;
        };
        recovered::Service47State input_service;
        if(bulk_vectors)input_service=read_service();
        const uint32_t boundary = next(), expected_pc = next();
        const int32_t expected_sp = static_cast<int32_t>(next()), expected_fp = static_cast<int32_t>(next());
        const auto expected_result = value(); const auto expected_slots = slots();
        const auto expected_globals = writes(), expected_locals = writes(), expected_bindings = writes();
        recovered::Service47State expected_service;
        if (service47_vectors) {
            expected_service=read_service();
        }
        require(at == fields.size() && source < programs.size() && host <= 1 && cap <= 512, "Chain fields");
        // This verification path explicitly supplies the five ABI premises.
        // Default production callers do not get this conditional projection.
        auto projection = conditional_abi; projection.selected_record_index = count % 2;
        startup::StartupProbe probe(programs[source], static_cast<uint8_t>(host), static_cast<uint16_t>(cap), projection, service_fixture);
        auto before = startup::StartupTestAccess::chain_initial(probe, pc, sp, fp, input_slots, prior, globals, locals, bindings);
        if(bulk_vectors)before=startup::StartupTestAccess::set_service(probe,input_service);
        const auto all_before = startup::StartupTestAccess::all_slots(probe);
        const auto actual = probe.run(1); const auto& after = startup::StartupTestAccess::all_slots(probe);
        const std::string label = "Chain vector" + std::to_string(count);
        if (boundary) {
            ++barriers;
            require(actual.status == startup::Status::Halted && actual.executed == 0 && same_context(before, actual.context) && all_before == after,
                    label + " barrier committed partial state: " + actual.diagnostic.reason);
        } else {
            ++success;
            require(actual.status == startup::Status::BudgetExhausted && actual.executed == 1,
                    label + " halted on original success: " + actual.diagnostic.reason);
            require(actual.pc == expected_pc && actual.sp == expected_sp && actual.fp == expected_fp && actual.result == expected_result &&
                    actual.context.global_writes == expected_globals && actual.context.local_writes == expected_locals &&
                    actual.context.host_reference_bindings == expected_bindings && std::equal(expected_slots.begin(), expected_slots.end(), after.begin()),
                    label + " context differs from original words");
            if (service47_vectors) require(actual.context.service47 == expected_service, label + " service47 state differs");
            require(std::all_of(after.begin() + expected_slots.size(), after.end(), [](const startup::Value& v) { return v == startup::Value{}; }),
                    label + " wrote outside recorded stack");
        }
    }
    char extra = 0; require(!stream.read(&extra, 1) && stream.eof(), "Chain extra fixture data");
    std::cout << "{\"" << check_name << "\":\"pass\",\"vectors\":" << expected_count
              << ",\"successes\":" << success << ",\"barriers\":" << barriers << "}\n";
}
void opcode_boundary_checks(const script::Program& program) {
    std::array<startup::Value, 12> slots{};
    slots[3] = raw_value(0x90000001); slots[4] = startup::Value::integer(4);
    slots[6] = startup::Value::stack_reference(2); slots[7] = startup::Value::stack_reference(3);
    const auto halt = [&](uint32_t pc, int32_t sp, std::array<startup::Value, 12> input, uint16_t cap = 512) {
        startup::StartupProbe probe(program, 0, cap);
        const auto before = startup::StartupTestAccess::initial(probe, pc, sp, input, {});
        const auto after = probe.run(1);
        require(after.status == startup::Status::Halted && after.executed == 0 &&
                same_context(before, after.context), "Opcode boundary failure committed state");
    };
    halt(0x1701, 0, slots); halt(0x1701, 1, slots);
    auto bad = slots; bad[6] = startup::Value::stack_reference(512); halt(0x1701, 8, bad);
    bad = slots; bad[7] = startup::Value::stack_reference(511); bad[3] = {};
    // Header at511 is zero: explicit non-array error, not an inferred value.
    halt(0x1701, 8, bad);
    bad = slots; bad[7] = startup::Value::global_reference(program.header().global_count, 0);
    halt(0x1701, 8, bad);
    bad = slots; bad[6] = raw_value(0xB0000008); halt(0x1701, 8, bad);
    bad = slots; bad[4] = raw_value(0x60000001); halt(0x1701, 8, bad);
    halt(0x1710, 0, slots); halt(0x1708, 1, slots); halt(0x10, 0, slots);
    // Destination write would precede push overflow in original handler14.
    // Native rollback includes the global arena write map.
    for (bool global : {false, true}) {
        startup::StartupProbe probe(program, 0);
        startup::StartupTestAccess::initial(probe, 0x1701, 512, slots, {});
        const auto before = startup::StartupTestAccess::set_top(probe,
            global ? startup::Value::global_reference(0, program.header().global_start()) : startup::Value::stack_reference(2),
            startup::Value::stack_reference(3));
        const auto after = probe.run(1);
        require(after.status == startup::Status::Halted && after.executed == 0 &&
                same_context(before, after.context), "Push overflow committed destination/global mutation");
    }
    startup::StartupProbe probe(program, 0);
    require(startup::StartupTestAccess::malformed_branch_halts(probe, 0x1702, 4),
            "Branch accepted target outside command boundary");
    require(startup::StartupTestAccess::malformed_branch_halts(probe, 0x1702, 2),
            "Branch accepted truncated u24 operand");
    std::cout << "{\"opcode_boundary_checks\":\"pass\",\"cases\":14}\n";
}
}

void calendar_scalar_checks(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary); require(bool(stream), "Calendar scalar fixture missing");
    char magic[8]{}; uint32_t count=0;
    stream.read(magic,8);stream.read(reinterpret_cast<char*>(&count),4);
    require(std::string(magic,6)=="CAL5V1" && count>0,"Calendar scalar schema");
    for (uint32_t i=0;i<count;++i) {
        std::array<uint32_t,10> row{};
        stream.read(reinterpret_cast<char*>(row.data()),sizeof(row));require(bool(stream),"Calendar scalar truncated");
        recovered::CalendarRecord record{};
        for (size_t j=0;j<record.size();++j)record[j]=static_cast<uint8_t>(row[5+j/4]>>(8*(j%4)));
        if (row[9]) {
            std::array<int32_t,5> fields{};
            for (size_t j=0;j<fields.size();++j)fields[j]=row[j]&0x80000000u ?
                static_cast<int32_t>(static_cast<int64_t>(row[j])-0x100000000LL) : static_cast<int32_t>(row[j]);
            require(recovered::CalendarRecord689B8(fields)==record,"Calendar caller truncation differs");
        }
        require(recovered::CalendarScalar7C580(record)==row[7] && recovered::CalendarScalar7C660(record)==row[8],
                "Calendar scalar differs from original words vector"+std::to_string(i));
    }
    char extra=0;require(!stream.read(&extra,1) && stream.eof(),"Calendar scalar extra data");
    std::cout<<"{\"calendar_scalar_checks\":\"pass\",\"vectors\":"<<count<<"}\n";
}

recovered::Service47Fixture natural_service47_fixture() {
    recovered::Service47Fixture f;
    f.enabled = f.control_cell_mapped = f.guard_cell_mapped = f.state_cell_mapped = f.memory_disjoint = true;
    f.control_present = f.control_word_mapped = f.source_present = f.source_header_mapped = true;
    f.status_cell_mapped = f.format_window_mapped = true; f.source_magic = 0x11076A; f.guard = 1;
    const auto append = [&](uint32_t word) {
        for (unsigned i = 0; i < 4; ++i) f.format_bytes.push_back(static_cast<uint8_t>(word >> (8 * i)));
    };
    append(0x9000001B); append(0x30272220);
    for (uint32_t i = 0; i < 26; ++i) append(0xB0000000 | (112 + 4 * i));
    for (uint32_t i = 0; i < 26; ++i) append(0x80000000);
    return f; //Synthetic mapped conditional stream, not an observed save/blob.
}
void service47_traces(const std::filesystem::path& base, bool full_slots = false) {
    stream_events = true;
    script::Program program; script::Diagnostic error;
    for (const char* name : {"INU", "KAERU", "NEKO", "ROBO", "USAGI"}) {
        require(program.load_original(base / (std::string(name) + ".KSC"), error), error.text());
        for (uint8_t host : {uint8_t(0), uint8_t(1)}) {
            startup::StartupProbe probe(program, host, 512, conditional_abi, natural_service47_fixture());
            startup::Trace trace;
            do { trace = probe.run(10000); } while (trace.status == startup::Status::BudgetExhausted);
            require(trace.status == startup::Status::Halted && trace.executed > 6189 && trace.context.service47.calls == 1 &&
                    trace.context.service47.format_called && trace.context.service47.format_return == 0,
                    "Connected service47 did not naturally advance: " + trace.diagnostic.reason);
            require(same_context(trace.context, probe.run(1).context), "Service47 repeated halt changed state");
            output(name, program, trace);
            if(full_slots) {
                std::cout<<"{\"all512_final\":true,\"source\":\""<<name<<".KSC\",\"host\":"<<unsigned(host)<<",\"slots\":[";
                const auto& slots=startup::StartupTestAccess::all_slots(probe);
                for(size_t i=0;i<slots.size();++i){if(i)std::cout<<',';value_json(slots[i]);}
                std::cout<<"]}\n";
            }
            startup::StartupProbe resumed(program, host, 512, conditional_abi, natural_service47_fixture());
            startup::Trace other;
            do { other = resumed.run(127); } while (other.status == startup::Status::BudgetExhausted);
            require(same_context(trace.context, other.context), "Service47 resumable trace differs");
            startup::StartupProbe repeated(program, host, 512, conditional_abi, natural_service47_fixture());
            const auto prefix = repeated.run(6189); const uint32_t service_pc = prefix.pc;
            const auto first = repeated.run(1);
            require(first.executed == 6190 && first.context.service47.calls == 1, "First47 lifecycle call failed");
            const auto before_repeat = startup::StartupTestAccess::repeat47(repeated, service_pc);
            const auto before_slots = startup::StartupTestAccess::all_slots(repeated);
            const auto second = repeated.run(1);
            require(second.status == startup::Status::Halted && same_context(before_repeat, second.context) &&
                    before_slots == startup::StartupTestAccess::all_slots(repeated) &&
                    second.diagnostic.reason.find("63378") != std::string::npos,
                    "Second47 reused cleared guard/prepared buffer or committed partial state");
            auto null_fixture = natural_service47_fixture(); null_fixture.control_present = false; null_fixture.guard = 0;
            startup::StartupProbe null_probe(program, host, 512, conditional_abi, null_fixture);
            null_probe.run(6189); require(null_probe.run(1).context.service47.calls == 1, "NULLcontrol47 failed");
            startup::StartupTestAccess::repeat47(null_probe, service_pc);
            require(null_probe.run(1).context.service47.calls == 2, "Repeated NULLcontrol47 failed");
        }
    }
    std::cout << "{\"service47_connected_resume_checks\":\"pass\",\"traces\":10,\"repeat_ready_barriers\":10,\"repeat_null_successes\":10}\n";
}
recovered::StringFixture natural_string_fixture() {
    recovered::StringFixture f;f.enabled=f.bss_window_mapped=f.heap_ready=f.memory_disjoint=f.allocator_policy_known=true;
    f.initial_heap.assign(f.heap_size,0xCC);
    //Explicit source-proved literal D8AD4 as supplied BSS window. The producer
    //route/readiness is a premise; no zero BSS or observed PSP heap is inferred.
    f.bss_window={0x83,0x41,0x83,0x43,0x83,0x52,0,0,0x30,0x34,0x32,0x35,0x34,0x30,0x33,0x33};
    return f;
}
startup::StringService20Projection natural_string_getters() {
    startup::StringService20Projection g;g.enabled=g.memory_disjoint=g.date_record_mapped=g.shared_global_table_mapped=true;
    const auto padded=[](const char* s) {std::vector<uint8_t> b(16,0);std::copy_n(reinterpret_cast<const uint8_t*>(s),std::char_traits<char>::length(s),b.begin());return b;};
    g.string_windows={{1,padded("TORO")},{5,padded("PORTABLE")},{7,padded("Tokyo")},{8,padded("BIRTHDAY")}};
    g.scalar_words={{2,3},{4,7},{6,11},{9,14},{10,1}};
    g.date_words={0,0,23,9,14,10,2026};return g;
}
startup::Service48Projection natural_service48_projection() {
    startup::Service48Projection p;auto& f=p.fixture;
    f.enabled=f.manager_cell_mapped=f.manager_record_mapped=f.singleton_mapped=f.arena_pointer_mapped=true;
    f.memory_disjoint=f.manager_stable_during_call=f.pool_pointer_cell_mapped=true;f.original_return_word=0x10000000;
    p.general_heap=natural_string_fixture();p.general_heap.bss_window_mapped=false;p.general_heap.bss_window.clear();p.general_heap.heap_base=0x600000;
    const auto append=[&](uint32_t w) {for(unsigned i=0;i<4;++i)p.prepared_payload.push_back(static_cast<uint8_t>(w>>(8*i)));};
    append(0x900000A2);for(uint32_t i=0;i<162;++i)append(0x80000000);return p;
}
void string_traces(const std::filesystem::path& base,bool all_hosts=false) {
    stream_events=true;script::Program program;script::Diagnostic error;
    for(const char* name:{"INU","KAERU","NEKO","ROBO","USAGI"}) {
        require(program.load_original(base/(std::string(name)+".KSC"),error),error.text());
        for(uint8_t host:{uint8_t(0),uint8_t(1)}) {
        if(!all_hosts && host==0)continue;
        const auto loaded48=host==0?natural_service48_projection():startup::Service48Projection{};
        startup::StartupProbe probe(program,host,512,conditional_abi,natural_service47_fixture(),natural_string_fixture(),natural_string_getters(),loaded48);
        startup::Trace trace;do {trace=probe.run(10000);} while(trace.status==startup::Status::BudgetExhausted);
        require(trace.status==startup::Status::Halted && (host==1 ? trace.context.string_arena.known && trace.context.string_arena.copies>0 : trace.context.service48.completed_calls==1),
                "Stage8 continuation failed: "+trace.diagnostic.reason);
        require(same_context(trace.context,probe.run(1).context),"String repeated halt changed state");
        output(name,program,trace);
        std::cout<<"{\"all512_final\":true,\"source\":\""<<name<<".KSC\",\"host\":"<<unsigned(host)<<",\"slots\":[";
        const auto& slots=startup::StartupTestAccess::all_slots(probe);
        for(size_t i=0;i<slots.size();++i){if(i)std::cout<<',';value_json(slots[i]);}std::cout<<"]}\n";
        startup::StartupProbe resumed(program,host,512,conditional_abi,natural_service47_fixture(),natural_string_fixture(),natural_string_getters(),loaded48);
        startup::Trace other;do {other=resumed.run(127);} while(other.status==startup::Status::BudgetExhausted);
        require(same_context(trace.context,other.context) && slots==startup::StartupTestAccess::all_slots(resumed),"String resumed full512/context differs");
        }
    }
    std::cout<<"{\"string_connected_resume_checks\":\"pass\",\"traces\":"<<(all_hosts?10:5)<<",\"full512\":true}\n";
}
void string_component_vectors(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);require(bool(in),"String vectors missing");
    const auto u32=[&]() {uint32_t v=0;in.read(reinterpret_cast<char*>(&v),4);require(bool(in),"String vector truncated");return v;};
    const auto bytes=[&](uint32_t n) {std::vector<uint8_t> v(n);if(n)in.read(reinterpret_cast<char*>(v.data()),n);require(bool(in),"String bytes truncated");return v;};
    require(u32()==0x38525453u,"String vector magic");const uint32_t count=u32();
    for(uint32_t i=0;i<count;++i) {
        const uint32_t entry=u32(),arg0=u32(),arg1=u32(),arg2=u32(),common=u32(),heapbase=u32(),size=u32();
        const uint32_t cursor=u32(),expected_cursor=u32(),expected_return=u32(),prior=u32(),sourceword=u32(),expected_word=u32();
        const uint32_t source_address=u32(),source_count=u32();
        const auto ctl=bytes(32),heap=bytes(size),source=bytes(source_count),expected_ctl=bytes(32),expected_heap=bytes(size);
        recovered::StringFixture f;f.enabled=f.bss_window_mapped=f.heap_ready=f.memory_disjoint=f.allocator_policy_known=true;
        f.common_base=common;f.heap_base=heapbase;f.heap_size=size;f.initial_heap=heap;f.forward_positive=ctl[16]!=0;f.conversion_cursor=cursor;
        recovered::StringArena arena=recovered::StringArena::FromMappedStorage(f,ctl,heap);
        uint32_t word=prior,result=0;
        if(entry==0x63318)result=arena.allocate(static_cast<int32_t>(arg1));
        else if(entry==0x63378)arena.free(arg1);
        else if(entry==0x63680)result=arena.reallocate(arg1,arg2);
        else if(entry==0x753E0) {
            if(source_address>=heapbase && source_address<heapbase+size)arena.assign753E0(word,source_address);
            else arena.assign_mapped753E0(word,source,source_address);
        } else if(entry==0x7581C)arena.copy7581C(word,sourceword);
        else if(entry==0x7515C)arena.release7515C(word);
        else if(entry==0x75778)result=arena.resolve75778(prior);
        else if(entry==0x1D43C || entry==0x1C184) {
            f.bss_window=source;if(entry==0x1C184)word=0x10000000u;
            recovered::StringService20Selector0(arena,f,0,word);
        } else throw std::runtime_error("String vector unsupported entry "+hexadecimal(entry));
        const std::string label="String vector"+std::to_string(i)+" "+hexadecimal(entry);
        require(arena.control_bytes()==expected_ctl && arena.heap_bytes()==expected_heap,label+" full source manager/heap bytes differ");
        require(arena.conversion_cursor()==expected_cursor,label+" cursor differs");
        if(entry==0x63318 || entry==0x63680 || entry==0x75778)require(result==expected_return,label+" returned pointer differs");
        if(entry==0x753E0 || entry==0x7581C || entry==0x7515C || entry==0x1D43C || entry==0x1C184)require(word==expected_word,label+" output word differs");
        (void)arg0;
    }
    require(in.peek()==EOF,"String vectors trailing bytes");
    std::cout<<"{\"string_component_original_word_checks\":\"pass\",\"cases\":"<<count<<",\"comparison\":\"full current heap/control bytes, pointer, tag word, conversion cursor\"}\n";
}
void string_connected_vectors(const std::filesystem::path& path,const std::filesystem::path& base) {
    std::ifstream in(path,std::ios::binary);require(bool(in),"String connected vectors missing");
    const auto u32=[&]() {uint32_t v=0;in.read(reinterpret_cast<char*>(&v),4);require(bool(in),"String connected vector truncated");return v;};
    const auto digest=[&]() {core::Sha256Digest v{};in.read(reinterpret_cast<char*>(v.data()),32);require(bool(in),"String digest truncated");return core::hex_digest(v);};
    const auto value=[&]() {startup::Value v;v.kind=static_cast<startup::Value::Kind>(u32());v.word=u32();v.index=u32();v.source_file_offset=u32();return v;};
    const auto mapping=[&]() {std::map<uint32_t,startup::Value> map;const auto n=u32();for(uint32_t i=0;i<n;++i){const uint32_t k=u32();map[k]=value();}return map;};
    const uint32_t magic=u32();const bool all_hosts=magic==0x38585653u;
    require(all_hosts || magic==0x384D5653u,"String connected schema");const uint32_t runs=u32();uint32_t total=0;
    const std::array<const char*,5> names{"INU","KAERU","NEKO","ROBO","USAGI"};
    for(uint32_t run=0;run<runs;++run) {
        const uint32_t index=u32();const uint8_t host=all_hosts?static_cast<uint8_t>(u32()):1;
        const uint32_t accepted=u32(),count=u32();require(index<names.size() && host<=1,"String run name index");
        script::Program program;script::Diagnostic error;require(program.load_original(base/(std::string(names[index])+".KSC"),error),error.text());
        const auto loaded48=host==0?natural_service48_projection():startup::Service48Projection{};
        startup::StartupProbe probe(program,host,512,conditional_abi,natural_service47_fixture(),natural_string_fixture(),natural_string_getters(),loaded48);
        uint32_t remain=accepted;while(remain){const auto n=std::min(remain,10000u);require(probe.run(n).status==startup::Status::BudgetExhausted,"Connected prefix halted");remain-=n;}
        for(uint32_t step=0;step<count;++step) {
            const uint32_t pc=u32();const int32_t sp=static_cast<int32_t>(u32()),fp=static_cast<int32_t>(u32());const auto result=value();
            const uint32_t slot_count=u32();require(slot_count==512,"Connected all512 input missing");std::array<startup::Value,512> slots{};for(auto& v:slots)v=value();
            const uint32_t global_count=u32();const auto globals_sha=digest();const auto locals=mapping(),bindings=mapping();
            recovered::Service47State service;
            service.known=u32()!=0;service.control_present=u32()!=0;service.source_present=u32()!=0;
            service.guard=static_cast<uint8_t>(u32());service.state=static_cast<uint16_t>(u32());service.calls=u32();service.cursor_bytes=u32();service.decoded_words=u32();
            service.format_called=u32()!=0;service.format_return=static_cast<int32_t>(u32());service.null_free_calls=u32();service.released_entities=u32();
            recovered::StringArenaSummary strings;strings.known=u32()!=0;
            if(strings.known){strings.allocations=u32();strings.reallocations=u32();strings.releases=u32();strings.copies=u32();strings.live_allocations=u32();strings.conversion_cursor=u32();strings.sha256=digest();}
            recovered::StringArenaSummary general;std::array<uint32_t,13> state48{};
            if(all_hosts) {
                general.known=u32()!=0;
                if(general.known){general.allocations=u32();general.reallocations=u32();general.releases=u32();general.copies=u32();general.live_allocations=u32();general.conversion_cursor=u32();general.sha256=digest();for(auto& v:state48)v=u32();}
            }
            //Only verification discards historical event objects; VM state is
            //unchanged, and literal run(1) retains candidate/commit behavior.
            startup::StartupTestAccess::discard_events(probe);const auto actual=probe.run(1);
            const auto& c=actual.context;const std::string label=std::string(names[index])+" step"+std::to_string(step);
            require(actual.events.size()==1 && c.pc==pc && c.sp==sp && c.fp==fp && c.result==result,label+" source VM scalar state differs");
            require(startup::StartupTestAccess::all_slots(probe)==slots,label+" original full512 differs");
            require(c.global_writes_count==global_count && c.global_writes_sha256==globals_sha && c.local_writes==locals && c.host_reference_bindings==bindings,label+" original CURRENT full map differs");
            require(c.service47==service && c.string_arena==strings,label+" original lifecycle/heap/cursor differs");++total;
            if(all_hosts) {
                require(c.general_arena==general,label+" original GENERAL1C93D4 heap differs");
                if(general.known) {
                    const auto& s=c.service48;
                    const std::array<uint32_t,13> actual48{s.manager_record,uint32_t(s.destination_registered),s.registered_word,uint32_t(s.format_called),s.cursor_bytes,s.decoded_words,
                        static_cast<uint32_t>(s.format_return),s.payload_clears,s.record_releases,s.manager_clears,s.completed_calls,s.heap_release_calls,s.null_release_calls};
                    require(actual48==state48,label+" original service48 CURRENT state differs");
                }
            }
        }
        const auto before=probe.run(1);require(before.status==startup::Status::Halted && before.events.size()==1,"Connected next ABI cut missing");
        require(same_context(before.context,probe.run(1).context),"Connected ABI repeat changed state");
    }
    require(in.peek()==EOF,"Connected vector trailing bytes");std::cout<<"{\"string_full_vm_original_word_checks\":\"pass\",\"runs\":"<<runs<<",\"commands\":"<<total<<",\"all512_and_maps\":true}\n";
}
void string_vm_fixtures(const std::filesystem::path& path,const std::filesystem::path& base) {
    std::ifstream in(path,std::ios::binary);require(bool(in),"String VM fixtures missing");
    const auto u32=[&]() {uint32_t v=0;in.read(reinterpret_cast<char*>(&v),4);require(bool(in),"VM fixture truncated");return v;};
    const auto bytes=[&](uint32_t n) {std::vector<uint8_t> b(n);if(n)in.read(reinterpret_cast<char*>(b.data()),n);require(bool(in),"VM bytes truncated");return b;};
    const auto value=[&]() {startup::Value v;v.kind=static_cast<startup::Value::Kind>(u32());v.word=u32();v.index=u32();v.source_file_offset=u32();return v;};
    const auto slots=[&]() {const auto n=u32();require(n==512,"VM fixture needs512slots");std::vector<startup::Value> s(n);for(auto& v:s)v=value();return s;};
    const auto mapping=[&]() {std::map<uint32_t,startup::Value> map;const auto n=u32();for(uint32_t i=0;i<n;++i){const auto k=u32();map[k]=value();}return map;};
    const auto read_service=[&]() {recovered::Service47State s;s.known=u32()!=0;s.control_present=u32()!=0;s.source_present=u32()!=0;s.guard=static_cast<uint8_t>(u32());s.state=static_cast<uint16_t>(u32());s.calls=u32();s.cursor_bytes=u32();s.decoded_words=u32();s.format_called=u32()!=0;s.format_return=static_cast<int32_t>(u32());s.null_free_calls=u32();s.released_entities=u32();return s;};
    const auto digest=[&]() {core::Sha256Digest d{};in.read(reinterpret_cast<char*>(d.data()),32);require(bool(in),"VM digest truncated");return core::hex_digest(d);};
    const auto summary=[&]() {recovered::StringArenaSummary s;s.known=u32()!=0;if(s.known){s.allocations=u32();s.reallocations=u32();s.releases=u32();s.copies=u32();s.live_allocations=u32();s.conversion_cursor=u32();s.sha256=digest();}return s;};
    require(u32()==0x38584653u,"VM fixture schema");const auto count=u32();uint32_t successes=0,barriers=0;
    const std::array<const char*,5> names{"INU","KAERU","NEKO","ROBO","USAGI"};
    for(uint32_t row=0;row<count;++row) {
        const auto length=u32();const auto start=in.tellg();const auto index=u32(),pc=u32();const int32_t sp=static_cast<int32_t>(u32()),fp=static_cast<int32_t>(u32());const auto prior=value();const auto input=slots();
        const auto globals=mapping(),locals=mapping(),bindings=mapping();const auto service=read_service();
        auto f=natural_string_fixture();auto getters=natural_string_getters();
        f.enabled=u32()!=0;f.bss_window_mapped=u32()!=0;f.heap_ready=u32()!=0;f.memory_disjoint=u32()!=0;f.allocator_policy_known=u32()!=0;
        getters.enabled=u32()!=0;getters.shared_global_table_mapped=u32()!=0;f.heap_size=u32();const auto bss_count=u32();f.bss_window=bytes(bss_count);f.initial_heap.assign(f.heap_size,0xCC);
        const bool imported=u32()!=0;recovered::StringArena arena;
        if(imported){auto current=f;current.heap_base=u32();current.heap_size=u32();current.conversion_cursor=u32();const auto ctl=bytes(32),heap=bytes(current.heap_size);current.initial_heap=heap;arena=recovered::StringArena::FromMappedStorage(current,ctl,heap);}
        const bool barrier=u32()!=0;const auto expected_pc=u32();const int32_t expected_sp=static_cast<int32_t>(u32()),expected_fp=static_cast<int32_t>(u32());const auto result=value();const auto expected_slots=slots();
        const auto globals_count=u32();const auto globals_hash=digest();const auto expected_locals=mapping(),expected_bindings=mapping();const auto expected_service=read_service();const auto expected_strings=summary(),expected_general=summary();
        require(!expected_general.known,"General arena is outside this string VM fixture schema");
        require(in.tellg()-start==static_cast<std::streamoff>(length),"VM record byte length differs");require(index<names.size(),"VM fixture source index");
        script::Program program;script::Diagnostic error;require(program.load_original(base/(std::string(names[index])+".KSC"),error),error.text());
        startup::StartupProbe probe(program,1,512,conditional_abi,natural_service47_fixture(),f,getters);
        startup::StartupTestAccess::chain_initial(probe,pc,sp,fp,input,prior,globals,locals,bindings);startup::StartupTestAccess::set_service(probe,service);if(imported)startup::StartupTestAccess::set_strings(probe,arena);
        const auto before=probe.run(0).context;const auto before_slots=startup::StartupTestAccess::all_slots(probe);const auto actual=probe.run(1);const auto& c=actual.context;
        const std::string label="String VM fixture"+std::to_string(row);
        if(barrier){++barriers;require(actual.status==startup::Status::Halted && actual.events.empty() && same_context(before,c) && before_slots==startup::StartupTestAccess::all_slots(probe),label+" protective refusal changed state");}
        else {++successes;require(actual.events.size()==1,label+" unexpectedly refused: "+actual.diagnostic.reason);}
        require(c.pc==expected_pc && c.sp==expected_sp && c.fp==expected_fp && c.result==result && std::equal(expected_slots.begin(),expected_slots.end(),startup::StartupTestAccess::all_slots(probe).begin()),label+" full512/scalar result differs");
        require(c.global_writes_count==globals_count && c.global_writes_sha256==globals_hash && c.local_writes==expected_locals && c.host_reference_bindings==expected_bindings && c.service47==expected_service,label+" current maps/lifecycle differs");
        require(c.string_arena==expected_strings,label+" source physical heap/cursor/copy/release differs");
    }
    require(in.peek()==EOF,"VM fixture trailing bytes");std::cout<<"{\"string_vm_fixture_checks\":\"pass\",\"cases\":"<<count<<",\"successes\":"<<successes<<",\"protective_barriers\":"<<barriers<<"}\n";
}
// Independent research corpus backend: finite raw address maps are test data.
// This backend is never used by StartupProbe or a production host allocator.
class RawBulkCells final : public recovered::BulkCells {
public:
    std::map<uint32_t, uint8_t> memory;
    std::vector<std::pair<uint32_t,uint32_t>> readonly;
    uint32_t read(uint32_t address, unsigned width = 4) const {
        require(address % width == 0, "Raw bulk unaligned read"); uint32_t value = 0;
        for (unsigned i=0;i<width;++i) {
            const auto it=memory.find(address+i);require(it!=memory.end(),"Raw bulk unmapped read");
            value |= static_cast<uint32_t>(it->second) << (8*i);
        }
        return value;
    }
    void write(uint32_t address, uint32_t value, unsigned width=4) {
        require(address % width == 0,"Raw bulk unaligned write");
        for (unsigned i=0;i<width;++i) {
            require(memory.count(address+i)!=0,"Raw bulk unmapped write");
            for (const auto& r:readonly)require(address+i<r.first || address+i>=r.second,"Raw bulk read-only guard");
        }
        for (unsigned i=0;i<width;++i)memory[address+i]=static_cast<uint8_t>(value>>(8*i));
    }
    recovered::BulkCell load(recovered::BulkAddress a) override {
        recovered::BulkCell v;v.word=read(a.index*4);return v;
    }
    void store(recovered::BulkAddress a,recovered::BulkCell v) override {
        require(!v.is_reference,"Unexpected raw test symbolic value");write(a.index*4,v.word);
    }
    recovered::BulkAddress resolve_offset(int32_t offset) override {
        const uint32_t address=read(0x1C93F8)+static_cast<uint32_t>(offset);
        require(address%4==0,"Raw bulk offset alignment guard");
        return {recovered::BulkAddress::Table::Stack,address/4};
    }
};
void bulk_raw_checks(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);require(bool(input),"Bulk raw vectors missing");
    const auto next=[&](){uint32_t n=0;input.read(reinterpret_cast<char*>(&n),4);require(bool(input),"Bulk raw truncated");return n;};
    char magic[8]{};input.read(magic,8);require(std::string(magic,8)=="BUL7RAW1","Bulk raw schema");
    const uint32_t count=next();uint32_t successes=0,cuts=0;
    for(uint32_t row=0;row<count;++row) {
        RawBulkCells cells;const uint32_t blocks=next();require(blocks<32,"Bulk block count");
        for(uint32_t block=0;block<blocks;++block) {
            const uint32_t start=next(),size=next();require(size<=2000000,"Bulk block size");
            for(uint32_t i=0;i<size;++i){char c=0;input.get(c);require(bool(input),"Bulk bytes truncated");cells.memory[start+i]=static_cast<uint8_t>(c);}
        }
        const uint32_t ranges=next();
        for(uint32_t i=0;i<ranges;++i){const uint32_t start=next(),end=next();cells.readonly.push_back({start,end});}
        const bool expected_cut=next()!=0;auto expected=cells.memory;const uint32_t changes=next();
        for(uint32_t i=0;i<changes;++i) {
            const uint32_t address=next(),before=next(),after=next();require(cells.read(address)==before,"Bulk original corpus identity");
            for(unsigned j=0;j<4;++j)expected[address+j]=static_cast<uint8_t>(after>>(8*j));
        }
        bool cut=false;
        try {
            const uint32_t ctx=0x1200000,vm=cells.read(ctx+16),pc=cells.read(vm+4),code=cells.read(vm);
            require(cells.read(code+pc,1)==97 && cells.read(code+pc+1,1)==2,"Bulk raw invocation");
            const uint32_t raw_sp=cells.read(ctx+92,2);
            const int32_t sp=raw_sp&0x8000 ? static_cast<int32_t>(raw_sp)-65536 : static_cast<int32_t>(raw_sp);
            const uint32_t first=cells.read(ctx+32)+4*static_cast<uint32_t>(sp-2);
            cells.write(vm+4,pc+2);
            (void)recovered::BulkBuiltin97(cells,{recovered::BulkAddress::Table::Stack,first/4},
                {recovered::BulkAddress::Table::Stack,(ctx+48)/4});
            cells.write(ctx+92,static_cast<uint32_t>(sp-2),2);
        } catch(const std::exception&) { cut=true; }
        require(cut==expected_cut,"Bulk raw success/cut differs at case"+std::to_string(row));
        if(!cut){++successes;require(cells.memory==expected,"Bulk full final-memory differs at case"+std::to_string(row));}
        else ++cuts; //Earlier protective/owned cuts do not assert raw partial faults.
    }
    char extra=0;require(!input.get(extra) && input.eof(),"Bulk raw extra data");
    std::cout<<"{\"bulk_independent_native_checks\":\"pass\",\"cases\":"<<count
        <<",\"success_full_memory\":"<<successes<<",\"explicit_cut_cases\":"<<cuts<<"}\n";
}
int main(int argc, char** argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--string-component-vectors"){string_component_vectors(argv[2]);return 0;}
        if(argc==4 && std::string(argv[1])=="--string-connected-vectors"){string_connected_vectors(argv[3],argv[2]);return 0;}
        if(argc==4 && std::string(argv[1])=="--string-vm-fixtures"){string_vm_fixtures(argv[3],argv[2]);return 0;}
        if(argc==3 && std::string(argv[1])=="--string-traces"){string_traces(argv[2]);return 0;}
        if(argc==3 && std::string(argv[1])=="--stage8-traces"){string_traces(argv[2],true);return 0;}
        if(argc==3 && std::string(argv[1])=="--bulk-raw-vectors"){bulk_raw_checks(argv[2]);return 0;}
        if(argc==3 && std::string(argv[1])=="--bulk-traces"){service47_traces(argv[2],true);return 0;}
        if(argc==4 && std::string(argv[1])=="--bulk-vectors"){chain_oracle_checks(argv[3],argv[2],"bulk_original_word_checks");return 0;}
        if (argc==3 && std::string(argv[1])=="--service47-traces") { service47_traces(argv[2]);return 0; }
        if (argc==4 && std::string(argv[1])=="--service47-vectors") { chain_oracle_checks(argv[3],argv[2],"service47_original_word_checks");return 0; }
        if (argc==3 && std::string(argv[1])=="--calendar-scalar-vectors") { calendar_scalar_checks(argv[2]);return 0; }
        require(argc == 4 || argc == 6 || argc == 7, "Supply original KSC directory, stage2 vectors, iterator vectors [chain vectors --stream]");
        stream_events = argc >= 6 && std::string(argv[5]) == "--stream";
        std::filesystem::path base = argv[1];
        script::Program program; script::Diagnostic error;
        for (const char* name : {"INU", "KAERU", "NEKO", "ROBO", "USAGI"}) {
            require(program.load_original(base / (std::string(name) + ".KSC"), error), error.text());
            for (uint8_t host_byte : {uint8_t(0), uint8_t(1)}) {
                startup::StartupProbe probe(program, host_byte, 512, conditional_abi);
                const auto prefix = probe.run(6027);
                require(prefix.status == startup::Status::BudgetExhausted && prefix.sp == 46 && prefix.fp == 38 &&
                        prefix.result == startup::Value::stack_reference(39) && prefix.context.global_writes.size() == 700,
                        "Accepted stage4 input prefix differs");
                startup::Trace trace;
                do { trace = probe.run(10000); } while (trace.status == startup::Status::BudgetExhausted);
                const std::map<std::string, uint32_t> stops = {{"INU",0x6B317},{"KAERU",0x6B128},
                    {"NEKO",0x8D39A},{"ROBO",0x6B25F},{"USAGI",0x67140}};
                require(trace.status == startup::Status::Halted && trace.executed == 6189 && trace.pc == stops.at(name) &&
                        trace.sp == 38 && trace.fp == 8 && trace.diagnostic.opcode == 0x8B,
                        "Original exact halt context differs");
                require(trace.first_builtin.valid && trace.first_builtin.id == 0xF0 &&
                        trace.first_builtin.host_service == 78 && trace.first_builtin.result.word == 0x10000000 &&
                        trace.result == startup::Value::stack_reference(39) &&
                        trace.context.global_writes.size() == 700 &&
                        trace.context.local_writes.size() == 2 && trace.context.host_reference_bindings.size() == 5 &&
                        trace.context.host_flag_104_known && trace.context.host_flag_104 == 0,
                        "First original builtin differs");
                require(same_context(trace.context, probe.run(256).context), "Unknown halt mutated context");
                output(name, program, trace);

                startup::StartupProbe default_probe(program, host_byte);
                const auto default_halt = default_probe.run(10000);
                const std::map<std::string, uint32_t> default_stops = {{"INU",0x6C057},{"KAERU",0x6BE6A},
                    {"NEKO",0x8E0DE},{"ROBO",0x6BF9F},{"USAGI",0x67E82}};
                require(default_halt.status == startup::Status::Halted && default_halt.executed == 5633 &&
                        default_halt.pc == default_stops.at(name) && default_halt.sp == 8 && default_halt.fp == 5 &&
                        default_halt.diagnostic.opcode == 0x8B && default_halt.result == startup::Value::stack_reference(12) &&
                        default_halt.context.local_writes.size() == 2 && default_halt.context.host_reference_bindings.empty() &&
                        same_context(default_halt.context, default_probe.run(1).context), "Default startup domain differs");
                std::cout << "{\"default_startup_snapshot\":true,\"source\":\"" << name << ".KSC\",\"host_byte\":" << unsigned(host_byte)
                          << ",\"executed\":5633,\"diagnostic_opcode\":139,\"builtin_id\":240,\"argc\":2,\"service\":10,\"context\":";
                snapshot_json(default_halt.context); std::cout << "}\n";

                startup::StartupProbe resumed(program, host_byte, 512, conditional_abi);
                require(resumed.run(1).executed == 1, "One-instruction budget pause");
                startup::Trace resumed_halt;
                do { resumed_halt = resumed.run(1024); } while (resumed_halt.status == startup::Status::BudgetExhausted);
                require(resumed_halt.status == startup::Status::Halted && same_context(trace.context, resumed_halt.context),
                        "Budget continuation differs from whole-prefix trace");
            }
        }
        startup::StartupProbe bad_host(program, 2);
        require(bad_host.run(256).status == startup::Status::InvalidInput && bad_host.run(256).executed == 0,
                "Malformed host byte accepted");
        startup::StartupProbe extreme_host(program, 255);
        require(extreme_host.run(256).status == startup::Status::InvalidInput, "Host byte255 accepted");
        startup::StartupProbe budget(program, 0);
        auto invalid_budget = budget.run(0);
        require(invalid_budget.status == startup::Status::InvalidInput && invalid_budget.executed == 0 && invalid_budget.sp == 0,
                "Zero budget mutated context");
        require(budget.run(10001).status == startup::Status::InvalidInput && budget.run(1).executed == 1,
                "Out-of-range budget poisoned a resumable context");

        startup::StartupProbe small_cap(program, 0, 3);
        const auto cap_before = small_cap.run(1);
        const auto cap_halt = small_cap.run(1);
        require(cap_halt.status == startup::Status::Halted && cap_halt.diagnostic.opcode == 0x90 &&
                cap_halt.executed == 1 && same_context(cap_before.context, cap_halt.context),
                "Read/push bounds failure committed partial mutation");
        startup::StartupProbe call_cap(program, 0, 5);
        const auto call_before = call_cap.run(2);
        const auto call_halt = call_cap.run(1);
        require(call_halt.status == startup::Status::Halted && call_halt.diagnostic.opcode == 0x20 &&
                call_halt.executed == 2 && same_context(call_before.context, call_halt.context),
                "Call bounds failure committed partial mutation");
        for (uint16_t limit : {uint16_t(0), uint16_t(2), uint16_t(513)})
            require(startup::StartupProbe(program, 0, limit).run(1).status == startup::Status::InvalidInput,
                    "Malformed resource cap accepted");

        script::Program unloaded;
        require(startup::run(unloaded, 0).status == startup::Status::InvalidInput, "Unloaded Program accepted");
        auto changed = program.bytes(); changed[8] ^= 1;
        require(!unloaded.load_original_bytes(changed, base / "mutated.KSC", error) && !unloaded.loaded(),
                "Mutated input bounds/header bypassed Program identity check");
        require(startup::run(unloaded, 0).executed == 0, "Malformed original advanced startup");
        changed.resize(12);
        require(!unloaded.load_original_bytes(changed, base / "truncated.KSC", error), "Truncated original accepted");
        require(program.load_original(base / "NEKO.KSC", error), error.text());
        // Missing any new ABI premise must stop on service10 atomically.
        // The ordinary API default has exactly this narrower startup domain.
        for (int missing = -1; missing < 9; ++missing) {
            auto projection = conditional_abi;
            if (missing < 0) projection = {};
            else if (missing == 0) projection.linked_context_mapped = false;
            else if (missing == 1) projection.selected_record_mapped = false;
            else if (missing == 2) projection.singleton_mapped = false;
            else if (missing == 3) projection.arena_mapping_disjoint = false;
            else if (missing == 4) projection.constants_initialized = false;
            else if (missing == 5) projection.selected_record_index = 2;
            else if (missing == 6) projection.selected_record_index = UINT32_MAX;
            else if (missing == 7) projection.selected_record_index = 0x40000000;
            else projection.selected_record_index = 3;
            startup::StartupProbe guarded(program, 0, 512, projection);
            const auto before = guarded.run(5633); const auto halt = guarded.run(1);
            require(before.status == startup::Status::BudgetExhausted && halt.status == startup::Status::Halted &&
                    halt.executed == 5633 && halt.pc == 0x8E0DE && halt.sp == 8 && halt.fp == 5 &&
                    halt.context.local_writes.size() == 2 && halt.context.host_reference_bindings.empty() &&
                    same_context(before.context, halt.context) && same_context(halt.context, guarded.run(1).context),
                    "Missing ABI premise accepted host registration or mutated context");
        }
        std::cout << "{\"host_projection_guard_checks\":\"pass\",\"cases\":10,\"default_executed\":5633,\"default_next_service\":10}\n";
        original_oracle_checks(argv[2], program);
        iterator_oracle_checks(argv[3], base);
        if (argc >= 6) chain_oracle_checks(argv[4], base);
        if (argc == 7) chain_oracle_checks(argv[6], base, "calendar_original_word_checks");
        opcode_boundary_checks(program);
        std::cout << "{\"negative_checks\":\"pass\",\"cases\":[\"host2/255\",\"budget0/10001\","
                     "\"caps0/2/513\",\"read/push cap3 atomic halt\",\"call cap5 atomic halt\","
                     "\"unloaded/mutated/truncated Program\",\"budget continuation\",\"repeated unknown halt\"]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
#endif
