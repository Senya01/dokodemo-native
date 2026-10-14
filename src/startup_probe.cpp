#include "startup_probe.h"
#include "recovered/ksc_dispatch_addresses.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace dokodemo::startup {
namespace {
constexpr uint32_t payload_mask = 0x0FFFFFFF;
constexpr uint32_t boolean_false = 0x10000000;

int32_t signed28(uint32_t word) {
    uint32_t payload = word & payload_mask;
    return payload & 0x08000000 ? static_cast<int32_t>(payload) - 0x10000000 : static_cast<int32_t>(payload);
}
void require(bool condition, const std::string& reason) {
    if (!condition) throw std::runtime_error(reason);
}
void release_supported(const Value& value) {
    require(value.kind != Value::Kind::Word || value.word >> 28 != 6,
            "Owned tag6 string release/copy is outside this reference trace");
}
std::string hex(uint32_t value) {
    std::ostringstream out; out << "0x" << std::uppercase << std::hex << value; return out.str();
}
uint32_t handler_for(int opcode) {
    return opcode >= 0 && static_cast<size_t>(opcode) < recovered::ksc_handler_va.size() ?
        recovered::ksc_handler_va[static_cast<size_t>(opcode)] : 0;
}
}

Value Value::integer(int32_t value) {
    Value result; result.word = 0x20000000 | (static_cast<uint32_t>(value) & payload_mask); return result;
}
Value Value::stack_reference(uint32_t index) {
    Value result; result.kind = Kind::StackReference; result.index = index; return result;
}
Value Value::global_reference(uint32_t index, uint32_t file_offset) {
    Value result; result.kind = Kind::GlobalReference; result.index = index;
    result.source_file_offset = file_offset; return result;
}
bool Value::operator==(const Value& other) const {
    return kind == other.kind && word == other.word && index == other.index &&
        source_file_offset == other.source_file_offset;
}
const char* status_name(Status status) {
    switch (status) {
    case Status::Running: return "running";
    case Status::Halted: return "halted";
    case Status::BudgetExhausted: return "budget_exhausted";
    case Status::InvalidInput: return "invalid_input";
    case Status::Terminated: return "terminated";
    }
    return "invalid_status";
}

StartupProbe::StartupProbe(const script::Program& program, uint8_t host_byte,
                          uint16_t stack_slot_limit)
    : program_(program), host_byte_(host_byte), stack_slot_limit_(stack_slot_limit) {
    context_.pc = program.header().entrypoint;
    const auto* command = program.command(context_.pc);
    if (host_byte > 1 || !program.loaded() || stack_slot_limit < 3 || stack_slot_limit > 512 ||
        !command || command->opcode != 0x30) {
        status_ = Status::InvalidInput;
        diagnostic_ = {host_byte > 1 ? "Host byte must be explicitly 0 or 1" :
                       !program.loaded() ? "Verified original Program is not loaded" :
                       stack_slot_limit < 3 || stack_slot_limit > 512 ? "Stack resource cap must be 3..512" :
                       "Original entry misses a proved frame prologue",
                       context_.pc, context_.pc + 28, command ? command->opcode : -1};
        return;
    }
    // BOOT 665F8/65FF4: zero allocation, SP0/FP-1. 231FC..2320C and
    // 67FD4..68054 preload two integer words above SP, then set entrypoint.
    context_.slots[1] = Value::integer(host_byte == 0 ? 1 : 0);
    context_.slots[2] = Value::integer(0);
}

Value& StartupProbe::slot(Context& context, int32_t index) const {
    require(index >= 0 && index < stack_slot_limit_,
            "Stack slot outside original allocation/native resource cap: " + std::to_string(index));
    return context.slots[static_cast<size_t>(index)];
}
const Value& StartupProbe::slot(const Context& context, int32_t index) const {
    require(index >= 0 && index < stack_slot_limit_,
            "Stack slot outside original allocation/native resource cap: " + std::to_string(index));
    return context.slots[static_cast<size_t>(index)];
}
void StartupProbe::push(Context& context, Value value) const {
    slot(context, context.sp) = value; ++context.sp;
}
Value StartupProbe::pop(Context& context) const {
    require(context.sp > 0, "Operand stack underflow");
    --context.sp; Value value = slot(context, context.sp); slot(context, context.sp) = {}; return value;
}
void StartupProbe::require_target(uint32_t pc) const {
    const auto* destination = program_.command(pc);
    require(destination && destination->opcode >= 0,
            "Call target misses original command boundary: " + hex(pc));
}
Snapshot StartupProbe::snapshot(const Context& context) const {
    Snapshot result;
    result.pc = context.pc; result.sp = context.sp; result.fp = context.fp;
    result.saved_fp = context.saved_fp; result.saved_sp = context.saved_sp;
    result.state = context.state; result.result = context.result;
    result.timer_base_set = context.timer_base_set; result.timer_base_pc = context.timer_base_pc;
    result.host_flag_104_known = context.host_flag_104_known; result.host_flag_104 = context.host_flag_104;
    // The diagnostic window displays initialized argument words above SP too.
    const auto count = static_cast<size_t>(std::min<int32_t>(512, std::max<int32_t>(context.sp + 4, 8)));
    result.stack_window.assign(context.slots.begin(), context.slots.begin() + count);
    return result;
}
Trace StartupProbe::trace() const {
    Trace result;
    result.status = status_; result.host_byte = host_byte_;
    result.executed = static_cast<uint32_t>(events_.size());
    result.pc = context_.pc; result.sp = context_.sp; result.fp = context_.fp;
    result.result = context_.result; result.first_builtin = first_builtin_;
    result.context = snapshot(context_); result.diagnostic = diagnostic_; result.events = events_; return result;
}

void StartupProbe::execute(Context& candidate, const script::Command& command,
                           BuiltinCall& builtin) const {
    const int op = command.opcode;
    const auto operand = [&](uint32_t at, uint8_t width) { return program_.operand(command, at, width); };
    uint32_t next = command.pc + command.size;
    if (op == 0x03) {
        // Actual handler is the common 6B9B8 instruction-cost path.
    } else if (op == 0x30) {
        int32_t count = static_cast<int32_t>(operand(1, 1));
        slot(candidate, candidate.sp + count);
        push(candidate, Value::integer(candidate.fp));
        candidate.fp = candidate.sp; candidate.sp += count;
    } else if (op >= 0x80 && op <= 0x83) {
        uint8_t width = static_cast<uint8_t>(op - 0x7F);
        uint32_t raw = operand(1, width);
        int32_t value;
        if (width < 4 && raw & (1u << (width * 8 - 1)))
            value = static_cast<int32_t>(raw) - static_cast<int32_t>(1u << (width * 8));
        else value = static_cast<int32_t>(raw & payload_mask);
        push(candidate, Value::integer(value));
    } else if (op == 0x85) {
        push(candidate, Value::integer(0));
    } else if (op == 0x86) {
        Value value; value.word = boolean_false; push(candidate, value);
    } else if (op == 0x90) {
        int32_t index = candidate.fp + static_cast<int32_t>(operand(1, 2));
        Value value = slot(candidate, index); release_supported(value); push(candidate, value);
    } else if (op == 0x91) {
        int32_t index = candidate.fp + static_cast<int32_t>(operand(1, 2));
        slot(candidate, index); push(candidate, Value::stack_reference(static_cast<uint32_t>(index)));
    } else if (op == 0x95) {
        uint32_t index = operand(1, 2);
        require(index < program_.header().global_count, "Global reference outside original table");
        push(candidate, Value::global_reference(index, program_.header().global_start() + index * 4));
    } else if (op == 0x51) {
        require(candidate.sp >= 2, "Assignment requires two operands");
        Value reference = slot(candidate, candidate.sp - 2), value = slot(candidate, candidate.sp - 1);
        require(reference.kind == Value::Kind::StackReference,
                "Only proved stack-reference assignment is supported here");
        Value& destination = slot(candidate, static_cast<int32_t>(reference.index));
        release_supported(destination); destination = value; pop(candidate); pop(candidate);
    } else if (op == 0x41) {
        require(candidate.sp > 0, "Timer base needs an operand");
        Value value = slot(candidate, candidate.sp - 1);
        require(value.kind == Value::Kind::Word && value.word >> 28 <= 2,
                "Timer base conversion outside proven scalar path");
        require(signed28(value.word) == 0, "Only startup's zero timer-base offset is recorded");
        release_supported(value); pop(candidate);
        candidate.timer_base_set = true; candidate.timer_base_pc = command.pc;
    } else if (op == 0x20 || op == 0x21) {
        int32_t argc = op == 0x20 ? static_cast<int32_t>(operand(1, 1)) : 0;
        uint32_t destination = operand(op == 0x20 ? 2u : 1u, 3);
        require_target(destination);
        if (op == 0x20) {
            require(candidate.sp >= argc, "Call argc exceeds stack");
            slot(candidate, candidate.sp + 1);
            const int32_t old_sp = candidate.sp;
            // 674C0 copies backward and creates two holes below arguments.
            for (int32_t index = 0; index < argc; ++index)
                slot(candidate, old_sp + 1 - index) = slot(candidate, old_sp - 1 - index);
            candidate.sp -= argc;
            slot(candidate, candidate.sp) = {}; slot(candidate, candidate.sp + 1) = {};
        }
        push(candidate, Value::integer(static_cast<int32_t>(next))); next = destination;
    } else if (op == 0x31) {
        require(candidate.fp != candidate.saved_fp, "Extended host interrupt frame unwind is unsupported");
        require(candidate.fp >= 2 && candidate.sp >= candidate.fp, "Malformed normal frame");
        Value previous_fp = slot(candidate, candidate.fp - 1), return_pc = slot(candidate, candidate.fp - 2);
        require(previous_fp.kind == Value::Kind::Word && previous_fp.word >> 28 == 2 &&
                return_pc.kind == Value::Kind::Word && return_pc.word >> 28 == 2,
                "Only numeric saved FP/return-PC paths are supported");
        int32_t return_value = signed28(return_pc.word);
        require(return_value >= 0, "Negative return PC");
        if (return_value) require_target(static_cast<uint32_t>(return_value));
        for (int32_t index = candidate.fp; index < candidate.sp; ++index) {
            release_supported(slot(candidate, index)); slot(candidate, index) = {};
        }
        slot(candidate, candidate.fp - 1) = {};
        candidate.sp = candidate.fp - 1; candidate.fp = signed28(previous_fp.word);
        pop(candidate); next = static_cast<uint32_t>(return_value);
        if (!return_value) candidate.state = 0;
    } else if (op == 0x38) {
        candidate.state = 0;
    } else if (op == 0x8B) {
        uint32_t id = operand(1, 1), argc = operand(2, 1);
        require(candidate.sp >= static_cast<int32_t>(argc), "Builtin argc exceeds stack");
        require(id == 0xF0 && argc == 2, "Unimplemented builtin/argc: id=" + hex(id));
        Value first = slot(candidate, candidate.sp - 2), second = slot(candidate, candidate.sp - 1);
        require(first == Value::integer(78) && second.kind == Value::Kind::Word && second.word == boolean_false,
                "Unimplemented F0 host service/arguments; only proved (78,false) is supported");
        release_supported(candidate.result);
        builtin.valid = true; builtin.pc = command.pc; builtin.id = 0xF0; builtin.argc = 2;
        builtin.handler_va = 0x6AD88; builtin.host_service = 78; builtin.host_handler_va = 0x1D2FC;
        builtin.arguments = {first, second}; builtin.result.word = boolean_false;
        candidate.result = builtin.result; candidate.host_flag_104_known = true; candidate.host_flag_104 = 0;
        pop(candidate); pop(candidate);
    } else {
        throw std::runtime_error("Unknown semantics: opcode=" + hex(static_cast<uint32_t>(op)) +
                                 ", BOOT handler=" + hex(handler_for(op)));
    }
    candidate.pc = next;
}

Trace StartupProbe::run(uint32_t instruction_budget) {
    if (status_ == Status::InvalidInput || status_ == Status::Halted || status_ == Status::Terminated) return trace();
    if (instruction_budget < 1 || instruction_budget > 10000) {
        // An invalid budget does not poison a resumable probe or alter context.
        Trace result = trace(); result.status = Status::InvalidInput;
        const auto* command = program_.command(context_.pc);
        result.diagnostic = {"Instruction budget must be 1..10000", context_.pc, context_.pc + 28,
                             command ? command->opcode : -1}; return result;
    }
    status_ = Status::Running;
    for (uint32_t index = 0; index < instruction_budget; ++index) {
        const auto* command = program_.command(context_.pc);
        if (!command || command->opcode < 0) {
            status_ = Status::Halted; diagnostic_ = {"PC does not address a proved command",
                context_.pc, context_.pc + 28, command ? command->opcode : -1}; return trace();
        }
        Context candidate = context_; BuiltinCall builtin;
        try { execute(candidate, *command, builtin); }
        catch (const std::exception& error) {
            status_ = Status::Halted;
            diagnostic_ = {error.what(), context_.pc, context_.pc + 28, command->opcode}; return trace();
        }
        Event event; event.pc = command->pc; event.file_offset = command->pc + 28;
        event.opcode = command->opcode; event.handler_va = handler_for(command->opcode);
        event.next_pc = candidate.pc; event.before = snapshot(context_); event.after = snapshot(candidate);
        event.builtin = builtin;
        events_.push_back(std::move(event)); context_ = std::move(candidate);
        if (builtin.valid && !first_builtin_.valid) first_builtin_ = std::move(builtin);
        if (context_.state == 0) { status_ = Status::Terminated; return trace(); }
    }
    status_ = Status::BudgetExhausted; return trace();
}

Trace run(const script::Program& program, uint8_t host_byte, uint32_t instruction_budget) {
    StartupProbe probe(program, host_byte); return probe.run(instruction_budget);
}
} // namespace dokodemo::startup
