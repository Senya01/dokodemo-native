#include "startup_probe.h"
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
                  << (value.kind == startup::Value::Kind::StackReference ? "stack" : "global")
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
    std::cout << "},\"global_writes_complete\":" << (snapshot.global_writes_complete ? "true" : "false")
              << ",\"global_writes_count\":" << snapshot.global_writes_count
              << ",\"global_writes_sha256\":\"" << snapshot.global_writes_sha256 << "\",\"global_writes\":{";
    bool first = true;
    for (const auto& entry : snapshot.global_writes) {
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
void output(const char* name, const script::Program& program, const startup::Trace& trace) {
    std::cout << "{\"source_name\":\"" << name << ".KSC\",\"source_sha256\":\"" << program.source().sha256
              << "\",\"host_byte\":" << unsigned(trace.host_byte) << ",\"status\":\"" << startup::status_name(trace.status)
              << "\",\"executed_instructions\":" << trace.executed << ",\"halt\":";
    snapshot_json(trace.context);
    std::cout << ",\"halt_opcode\":" << trace.diagnostic.opcode << ",\"first_builtin\":";
    builtin_json(trace.first_builtin);
    std::cout << ",\"events\":[";
    for (size_t i = 0; i < trace.events.size(); ++i) {
        if (i) std::cout << ',';
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
    }
    std::cout << "]}\n";
}
bool same_context(const startup::Snapshot& a, const startup::Snapshot& b) {
    return a.pc == b.pc && a.sp == b.sp && a.fp == b.fp && a.saved_fp == b.saved_fp && a.saved_sp == b.saved_sp &&
        a.state == b.state && a.result == b.result && a.stack_window == b.stack_window &&
        a.timer_base_set == b.timer_base_set && a.timer_base_pc == b.timer_base_pc &&
        a.host_flag_104_known == b.host_flag_104_known && a.host_flag_104 == b.host_flag_104 &&
        a.global_writes == b.global_writes && a.global_writes_complete == b.global_writes_complete &&
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

int main(int argc, char** argv) {
    try {
        require(argc == 4, "Supply original KSC directory, stage2 oracle vectors and iterator vectors");
        std::filesystem::path base = argv[1];
        script::Program program; script::Diagnostic error;
        for (const char* name : {"INU", "KAERU", "NEKO", "ROBO", "USAGI"}) {
            require(program.load_original(base / (std::string(name) + ".KSC"), error), error.text());
            for (uint8_t host_byte : {uint8_t(0), uint8_t(1)}) {
                startup::StartupProbe probe(program, host_byte);
                const auto trace = probe.run(10000);
                const std::map<std::string, uint32_t> stops = {{"INU",0x6C042},{"KAERU",0x6BE55},
                    {"NEKO",0x8E0C9},{"ROBO",0x6BF8A},{"USAGI",0x67E6D}};
                require(trace.status == startup::Status::Halted && trace.executed == 5625 && trace.pc == stops.at(name) &&
                        trace.sp == 6 && trace.fp == 5 && trace.diagnostic.opcode == 0x93,
                        "Original exact halt context differs");
                require(trace.first_builtin.valid && trace.first_builtin.id == 0xF0 &&
                        trace.first_builtin.host_service == 78 && trace.first_builtin.result.word == 0x10000000 &&
                        trace.result == startup::Value::stack_reference(12) &&
                        trace.context.global_writes.size() == 700 &&
                        trace.context.host_flag_104_known && trace.context.host_flag_104 == 0,
                        "First original builtin differs");
                require(same_context(trace.context, probe.run(256).context), "Unknown halt mutated context");
                output(name, program, trace);

                startup::StartupProbe resumed(program, host_byte);
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
        original_oracle_checks(argv[2], program);
        iterator_oracle_checks(argv[3], base);
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
