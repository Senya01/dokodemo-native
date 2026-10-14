#include "startup_probe.h"
#ifdef DOKODEMO_STARTUP_VERIFY_MAIN
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace dokodemo;
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
        std::cout << ",\"builtin\":"; builtin_json(event.builtin); std::cout << "}";
    }
    std::cout << "]}\n";
}
bool same_context(const startup::Snapshot& a, const startup::Snapshot& b) {
    return a.pc == b.pc && a.sp == b.sp && a.fp == b.fp && a.saved_fp == b.saved_fp && a.saved_sp == b.saved_sp &&
        a.state == b.state && a.result == b.result && a.stack_window == b.stack_window &&
        a.timer_base_set == b.timer_base_set && a.timer_base_pc == b.timer_base_pc &&
        a.host_flag_104_known == b.host_flag_104_known && a.host_flag_104 == b.host_flag_104;
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Supply workspace original KSC directory");
        std::filesystem::path base = argv[1];
        script::Program program; script::Diagnostic error;
        for (const char* name : {"INU", "KAERU", "NEKO", "ROBO", "USAGI"}) {
            require(program.load_original(base / (std::string(name) + ".KSC"), error), error.text());
            for (uint8_t host_byte : {uint8_t(0), uint8_t(1)}) {
                startup::StartupProbe probe(program, host_byte);
                const auto trace = probe.run(256);
                require(trace.status == startup::Status::Halted && trace.executed == 20 && trace.pc == 0x1701 &&
                        trace.sp == 15 && trace.fp == 8 && trace.diagnostic.opcode == 0x14,
                        "Original exact halt context differs");
                require(trace.first_builtin.valid && trace.first_builtin.id == 0xF0 &&
                        trace.first_builtin.host_service == 78 && trace.result.word == 0x10000000 &&
                        trace.context.host_flag_104_known && trace.context.host_flag_104 == 0,
                        "First original builtin differs");
                require(same_context(trace.context, probe.run(256).context), "Unknown halt mutated context");
                output(name, program, trace);

                startup::StartupProbe resumed(program, host_byte);
                for (unsigned step = 1; step <= 20; ++step) {
                    auto paused = resumed.run(1);
                    require(paused.status == startup::Status::BudgetExhausted && paused.executed == step,
                            "One-instruction budget did not pause exactly");
                }
                const auto resumed_halt = resumed.run(1);
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
        std::cout << "{\"negative_checks\":\"pass\",\"cases\":[\"host2/255\",\"budget0/10001\","
                     "\"caps0/2/513\",\"read/push cap3 atomic halt\",\"call cap5 atomic halt\","
                     "\"unloaded/mutated/truncated Program\",\"budget continuation\",\"repeated unknown halt\"]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
#endif
