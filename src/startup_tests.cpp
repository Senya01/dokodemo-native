#include "startup_probe.h"
#include "recovered/calendar_native.h"
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
    static Snapshot chain_initial(StartupProbe& probe, uint32_t pc, int32_t sp, int32_t fp,
                                  const std::vector<Value>& slots, Value result,
                                  const std::map<uint32_t, Value>& globals,
                                  const std::map<uint32_t, Value>& locals,
                                  const std::map<uint32_t, Value>& bindings) {
        iterator_initial(probe, pc, sp, fp, slots, result, globals);
        probe.context_.local_writes = locals; probe.context_.host_reference_bindings = bindings;
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
                      value.kind == startup::Value::Kind::LocalReference ? "local" : "global")
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
        if (i) std::cout << ','; value_json(snapshot.stack_window[i]);
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
        if (!binding_first) std::cout << ','; binding_first = false;
        std::cout << '"' << entry.first << "\":"; value_json(entry.second);
    }
    std::cout << "}},\"global_writes_complete\":" << (snapshot.global_writes_complete ? "true" : "false")
              << ",\"global_writes_count\":" << snapshot.global_writes_count
              << ",\"global_writes_sha256\":\"" << snapshot.global_writes_sha256 << "\",\"global_writes\":{";
    bool first = true;
    for (const auto& entry : snapshot.global_writes) {
        if (!first) std::cout << ','; first = false;
        std::cout << '"' << entry.first << "\":"; value_json(entry.second);
    }
    std::cout << "},\"local_writes\":{";
    first = true;
    for (const auto& entry : snapshot.local_writes) {
        if (!first) std::cout << ','; first = false;
        std::cout << '"' << entry.first << "\":"; value_json(entry.second);
    }
    std::cout << "}}";
}
void builtin_json(const startup::BuiltinCall& builtin) {
    if (!builtin.valid) { std::cout << "null"; return; }
    std::cout << "{\"pc\":\"" << hexadecimal(builtin.pc) << "\",\"id\":" << unsigned(builtin.id)
              << ",\"argc\":" << unsigned(builtin.argc) << ",\"arguments\":[";
    for (size_t i = 0; i < builtin.arguments.size(); ++i) {
        if (i) std::cout << ','; value_json(builtin.arguments[i]);
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
            if (!first) std::cout << ','; first = false;
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
        a.host_reference_bindings == b.host_reference_bindings &&
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
    require(std::string(magic, 6) == "DCH4V1" && expected_count > 0, "Chain fixture schema");
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
            startup::Value result; const uint32_t kind = next(); require(kind <= 3, "Chain invalid kind");
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
        const uint32_t boundary = next(), expected_pc = next();
        const int32_t expected_sp = static_cast<int32_t>(next()), expected_fp = static_cast<int32_t>(next());
        const auto expected_result = value(); const auto expected_slots = slots();
        const auto expected_globals = writes(), expected_locals = writes(), expected_bindings = writes();
        require(at == fields.size() && source < programs.size() && host <= 1 && cap <= 512, "Chain fields");
        // This verification path explicitly supplies the five ABI premises.
        // Default production callers do not get this conditional projection.
        auto projection = conditional_abi; projection.selected_record_index = count % 2;
        startup::StartupProbe probe(programs[source], static_cast<uint8_t>(host), static_cast<uint16_t>(cap), projection);
        const auto before = startup::StartupTestAccess::chain_initial(probe, pc, sp, fp, input_slots, prior, globals, locals, bindings);
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

int main(int argc, char** argv) {
    try {
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
