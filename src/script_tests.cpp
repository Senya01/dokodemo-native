#include "script_vm.h"
#ifdef DOKODEMO_SCRIPT_SELF_TEST
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <iomanip>
#include <sstream>

using namespace dokodemo::script;
namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
std::string hex_bytes(const std::string& bytes) {
    std::ostringstream stream; stream << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) stream << std::setw(2) << unsigned(byte);
    return stream.str();
}
void event_line(const char* name, const Event& event) {
    std::cout << "{\"fixture\":\"" << name << "\",\"pc\":" << event.pc << ",\"file_offset\":" << event.file_offset
        << ",\"opcode\":" << event.opcode << ",\"next_pc\":" << event.next_pc
        << ",\"display_state\":" << event.display_state << ",\"display_count\":" << event.display_count
        << ",\"message_hex\":\"" << hex_bytes(event.message_cp932) << "\"}\n";
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Supply workspace-local original resource directory");
        std::filesystem::path base = argv[1];
        const char* stems[] = {"INU", "KAERU", "NEKO", "ROBO", "USAGI"};
        const uint32_t command_counts[] = {102682,97493,131126,105451,97807};
        const uint32_t reference_counts[] = {16177,15597,22329,18118,16647};
        uint32_t total_commands = 0, total_refs = 0;
        for (unsigned i = 0; i < 5; ++i) {
            Program p; Dictionary d; Diagnostic error;
            require(p.load_original(base / (std::string(stems[i]) + ".KSC"), error), error.text());
            require(d.load_original(base / (std::string(stems[i]) + ".DIC"), error), error.text());
            require(p.commands().size() == command_counts[i], "C++ command count differs from structural reference");
            require(p.references().size() == reference_counts[i], "C++ reference count differs from structural reference");
            require(d.records().size() == 162, "DIC record count");
            total_commands += static_cast<uint32_t>(p.commands().size());
            total_refs += static_cast<uint32_t>(p.references().size());
            Vm vm(p); require(vm.start().status == Status::Running, "Original entry prologue not accepted");
            require(vm.step().status == Status::Running, "Original frame reservation failed");
            Event stop = vm.step();
            require(stop.status == Status::Stopped && stop.opcode == 0x90 && stop.file_offset == p.header().entrypoint + 30,
                    "No-argument original startup must stop on unresolved frame argument");
            require(vm.stack().empty(), "Unresolved read changed operand stack");
            uint32_t pc = vm.pc(); require(vm.step().status == Status::Stopped && vm.pc() == pc, "Stopped VM advanced");
            std::cout << "{\"file\":\"" << stems[i] << "\",\"sha256\":\"" << p.source().sha256
                << "\",\"commands\":" << p.commands().size() << ",\"references\":" << p.references().size()
                << ",\"dictionary_records\":" << d.records().size() << ",\"startup_stop_pc\":" << stop.pc
                << ",\"startup_stop_opcode\":" << stop.opcode << "}\n";
        }
        require(total_commands == 534559 && total_refs == 88868, "Original aggregate crosscheck failed");
        Program neko; Diagnostic error;
        require(neko.load_original(base / "NEKO.KSC", error), error.text());
        Vm message(neko);
        require(message.start_at(0xC373 - 28, {Value::integer(10), Value::integer(14)}).status == Status::Running, "Fixture start");
        require(message.step().opcode == 0x30, "Fixture prologue");
        Event shown = message.step();
        require(shown.status == Status::Message && shown.opcode == 0x48 && shown.file_offset == 0xC375,
                "Original opcode48 did not produce message");
        require(shown.pc == 0xC359 && shown.next_pc == 0xC370, "Fixture message address");
        require(shown.display_state == 6 && shown.display_count == 1, "Opcode48 display state not reproduced");
        require(hex_bytes(shown.message_cp932) == "31308c8e313493fa2082c882cc836a838381480a", "Month/day/global23 expansion changed");
        event_line("NEKO_original_C373_message", shown);
        Event end = message.step();
        require(end.status == Status::Stopped && end.opcode == 0x31 && end.file_offset == 0xC38C, "Return ABI must stop explicitly");
        for (int32_t condition : {0, 1}) {
            Vm branch(neko);
            require(branch.start_at(0xC682 - 28, {Value::integer(7), Value::integer(condition)}).status == Status::Running, "Branch fixture start");
            require(branch.step().status == Status::Running, "Branch frame");
            require(branch.step().status == Status::Running, "Branch condition load");
            Event selected = branch.step();
            require(selected.opcode == 0x11 && selected.has_branch_decision && selected.branch_taken == (condition == 0), "Opcode11 polarity");
            require(selected.next_pc + 28 == (condition == 0 ? 0xC6ADu : 0xC68Bu), "Opcode11 exact original destination");
            require(branch.stack().empty(), "Conditional branch must consume condition");
            event_line(condition == 0 ? "NEKO_original_C682_false" : "NEKO_original_C682_true", selected);
            Event stop = branch.run();
            require(stop.status == Status::Stopped && stop.opcode == 0x84, "Relocated null constant must remain unsupported");
        }
        Vm startup(neko);
        require(startup.start({Value::integer(0)}).status == Status::Running, "Explicit argument startup fixture");
        Event call = startup.run();
        require(call.status == Status::Stopped && call.opcode == 0x20 && call.file_offset == 0x8E1BC, "Call ABI blocker changed");
        require(startup.stack().size() == 1, "Unsupported call consumed argument");
        // A contiguous original setter, not a fabricated bytecode instruction.
        // Parameter/domain meaning of global[0x59AE] remains unnamed.
        Vm state(neko);
        require(state.start_at(0x2BFD - 28, {Value::integer(37)}).status == Status::Running, "Original setter fixture start");
        require(state.read_slot(Table::Global, 0x59AE).signed_integer() == 0, "Original setter initial value");
        for (int expected : {0x30, 0x95, 0x90, 0x51}) {
            Event event = state.step();
            require(event.status == Status::Running && event.opcode == expected, "Original setter sequence");
        }
        require(state.read_slot(Table::Global, 0x59AE).signed_integer() == 37 && state.stack().empty(), "Original setter did not update runtime state");
        require(neko.initial_word(Table::Global, 0x59AE) == 0x20000000, "Runtime setter mutated original program data");
        Event state_end = state.step();
        require(state_end.status == Status::Stopped && state_end.opcode == 0x31 && state_end.file_offset == 0x2C06, "Setter return must remain blocked");
        std::cout << "{\"fixture\":\"NEKO_original_2BFD_state\",\"global_index\":22958,\"initial_integer\":0,\"final_integer\":37,\"source_bytes_unchanged\":true}\n";
        Vm invalid(neko); require(invalid.start_at(0xC374 - 28, {}).status == Status::Stopped, "Operand byte admitted as start");
        auto mutated = neko.bytes(); mutated[28] = 0xFE;
        Program rejected;
        require(!rejected.load_original_bytes(mutated, "unaudited-opcode", error), "Unknown opcode accepted");
        require(!rejected.loaded() && error.file_offset == 28 && error.opcode == 0xFE, "Unknown opcode diagnostic lacks exact location");
        mutated = neko.bytes(); mutated.back() ^= 1;
        require(!rejected.load_original_bytes(mutated, "unverified-source", error), "Different source identity accepted");
        require(error.reason.find("SHA256") != std::string::npos, "Unverified source reason");
        mutated = neko.bytes(); mutated.resize(20);
        require(!rejected.load_original_bytes(mutated, "truncated", error), "Truncated file accepted");
        std::cout << "{\"result\":\"passed\",\"original_scripts\":5,\"original_dictionaries\":5,\"commands\":"
            << total_commands << ",\"references\":" << total_refs << "}\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr << "FAIL: " << ex.what() << '\n'; return 1; }
}
#endif
