#include "startup_probe.h"
#include "core_hash.h"
#include "recovered/ksc_dispatch_addresses.h"
#include "recovered/game_primitives.h"
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
Value Value::local_reference(uint32_t index, uint32_t file_offset) {
    Value result; result.kind = Kind::LocalReference; result.index = index;
    result.source_file_offset = file_offset; return result;
}
bool Value::operator==(const Value& other) const {
    return kind == other.kind && word == other.word && index == other.index &&
        source_file_offset == other.source_file_offset;
}
bool branch_condition(uint8_t opcode, uint32_t word) {
    require(opcode == 0x11 || opcode == 0x12, "Unsupported conditional branch opcode");
    const bool nonzero = (word & payload_mask) != 0;
    return opcode == 0x11 ? !nonzero : nonzero;
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
                          uint16_t stack_slot_limit, HostAbiProjection projection)
    : program_(program), host_byte_(host_byte), stack_slot_limit_(stack_slot_limit), host_abi_projection_(projection) {
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
Value StartupProbe::read_reference(const Context& context, const Value& reference,
                                   uint32_t following_words) const {
    if (reference.kind == Value::Kind::StackReference) {
        require(reference.index <= UINT32_MAX - following_words, "Reference index overflow");
        const uint32_t index = reference.index + following_words;
        require(index < stack_slot_limit_, "Referenced stack word outside allocation/native cap");
        return slot(context, static_cast<int32_t>(index));
    }
    if (reference.kind == Value::Kind::LocalReference) {
        require(reference.index <= UINT32_MAX - following_words, "Local reference index overflow");
        const uint32_t index = reference.index + following_words;
        require(index < program_.header().local_count, "Referenced local word outside original ctx24 table");
        const auto written = context.local_writes.find(index);
        if (written != context.local_writes.end()) return written->second;
        const uint32_t word = program_.initial_word(script::Table::Local, static_cast<uint16_t>(index));
        require(word >> 28 != 5 && word >> 28 != 0xB,
                "Initial local pool/reference rebasing is outside the tested startup domain");
        Value value; value.word = word; return value;
    }
    require(reference.kind == Value::Kind::GlobalReference,
            "Unresolved raw value/reference arena address");
    require(reference.index <= UINT32_MAX - following_words, "Reference index overflow");
    const uint32_t index = reference.index + following_words;
    require(index < program_.header().global_count, "Referenced global word outside original table");
    const auto written = context.global_writes.find(index);
    if (written != context.global_writes.end()) return written->second;
    const uint32_t word = program_.initial_word(script::Table::Global, static_cast<uint16_t>(index));
    if (word >> 28 == 0xB) {
        // BOOT 66EA0..66F40: original selector4/index:u16 addresses the global
        // table. Keep exact slot identity, without inventing an arena base.
        const uint32_t selector = static_cast<uint32_t>(signed28(word) >> 16);
        require(selector == 4, "Initial tagB selector outside tested global rebasing domain");
        const uint32_t target = word & 0xFFFF;
        require(target < program_.header().global_count, "Initial global reference target outside table");
        return Value::global_reference(target, program_.header().global_start() + target * 4);
    }
    require(word >> 28 != 5, "Initial tag5 pool rebasing is outside this startup probe");
    Value value; value.word = word; return value;
}
void StartupProbe::write_reference(Context& context, const Value& reference, Value value) const {
    if (reference.kind == Value::Kind::StackReference) {
        require(reference.index < stack_slot_limit_, "Referenced stack destination outside allocation/native cap");
        slot(context, static_cast<int32_t>(reference.index)) = value;
    } else if (reference.kind == Value::Kind::LocalReference) {
        require(reference.index < program_.header().local_count, "Referenced local destination outside original ctx24 table");
        context.local_writes[reference.index] = value;
    } else {
        require(reference.kind == Value::Kind::GlobalReference,
                "Unresolved raw destination/reference arena address");
        require(reference.index < program_.header().global_count, "Referenced global destination outside table");
        context.global_writes[reference.index] = value;
    }
}
Snapshot StartupProbe::snapshot(const Context& context, bool include_globals) const {
    Snapshot result;
    result.pc = context.pc; result.sp = context.sp; result.fp = context.fp;
    result.saved_fp = context.saved_fp; result.saved_sp = context.saved_sp;
    result.state = context.state; result.result = context.result;
    result.timer_base_set = context.timer_base_set; result.timer_base_pc = context.timer_base_pc;
    result.host_flag_104_known = context.host_flag_104_known; result.host_flag_104 = context.host_flag_104;
    result.local_writes = context.local_writes;
    result.host_reference_bindings = context.host_reference_bindings;
    result.global_writes_complete = include_globals;
    result.global_writes_count = static_cast<uint32_t>(context.global_writes.size());
    core::Sha256 digest;
    for (const auto& entry : context.global_writes) {
        // Canonical little-endian (key,kind,word,index,source_file_offset).
        const uint32_t fields[] = {entry.first, static_cast<uint32_t>(entry.second.kind),
            entry.second.word, entry.second.index, entry.second.source_file_offset};
        for (const uint32_t field : fields) {
            const uint8_t bytes[] = {static_cast<uint8_t>(field), static_cast<uint8_t>(field >> 8),
                static_cast<uint8_t>(field >> 16), static_cast<uint8_t>(field >> 24)};
            digest.update(bytes, sizeof(bytes));
        }
    }
    result.global_writes_sha256 = core::hex_digest(digest.digest());
    if (include_globals) result.global_writes = context.global_writes;
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
    } else if (op == 0x10 || op == 0x11 || op == 0x12) {
        const uint32_t destination = operand(1, 3);
        bool take = true;
        if (op != 0x10) {
            require(candidate.sp > 0, "Conditional branch stack underflow");
            const Value value = slot(candidate, candidate.sp - 1);
            require(value.kind == Value::Kind::Word,
                    "Branch needs a known raw payload; symbolic/reference arena offsets are unproved");
            release_supported(candidate.result); release_supported(value);
            take = branch_condition(static_cast<uint8_t>(op), value.word);
            pop(candidate); candidate.result = {}; // 7515C clears transferred result.
        }
        if (take) { require_target(destination); next = destination; }
    } else if (op == 0x14) {
        require(candidate.sp >= 2, "Opcode14 requires two references");
        const Value left = slot(candidate, candidate.sp - 2), right = slot(candidate, candidate.sp - 1);
        const auto is_reference = [](const Value& value) {
            return value.kind != Value::Kind::Word || value.word >> 28 == 0xB;
        };
        require(is_reference(left) && is_reference(right), "Opcode14: not a reference value");
        // Original two tagB guards make its numeric/string conversion blocks
        // unreachable. Resolve mapped addresses, then test the referenced RHS.
        const Value header = read_reference(candidate, right);
        require(header.kind == Value::Kind::Word && header.word >> 28 == 9, "Opcode14: not an array");
        const Value first = read_reference(candidate, right, 1);
        release_supported(first); // 7581C copies all words except owned tag6.
        // 7581C overwrites without releasing the old destination. Both operands
        // remain; 6BF50..6BF90 pushes exactly I(1), irrespective of array count.
        write_reference(candidate, left, first);
        push(candidate, Value::integer(1));
    } else if (op == 0x15) {
        require(candidate.sp >= 3, "Opcode15 requires destination/source/index");
        const Value source = slot(candidate, candidate.sp - 2);
        // A symbolic reference is an established arena slot identity. A raw
        // numeric/tagB address cannot be mapped by inventing a PSP arena base.
        require(source.kind != Value::Kind::Word, "Opcode15 source address is unmapped");
        const auto scalar = [](const Value& value) {
            require(value.kind == Value::Kind::Word, "Opcode15 numeric reference payload is unproved");
            const uint32_t tag = value.word >> 28;
            require(tag != 4, "Opcode15 tag4 float conversion is unimplemented");
            require(tag != 5 && tag != 6, "Opcode15 string conversion callee C1E0C is unimplemented");
            return tag == 9 ? static_cast<int32_t>(value.word & 0xFFF) : signed28(value.word);
        };
        const int32_t count = scalar(read_reference(candidate, source));
        // BOOT 6C1EC..1F4 saves this value before an aliased destination copy.
        const int32_t index = scalar(slot(candidate, candidate.sp - 1));
        if (index < count) {
            const Value destination = slot(candidate, candidate.sp - 3);
            require(destination.kind != Value::Kind::Word, "Opcode15 destination address is unmapped");
            const int64_t element_index = static_cast<int64_t>(source.index) + index + 1;
            require(element_index >= 0 && element_index <= UINT32_MAX,
                    "Opcode15 element address outside mapped arena");
            Value element = source;
            element.index = static_cast<uint32_t>(element_index);
            if (element.kind == Value::Kind::GlobalReference)
                element.source_file_offset = program_.header().global_start() + element.index * 4;
            else if (element.kind == Value::Kind::LocalReference)
                element.source_file_offset = program_.header().pool_end() + element.index * 4;
            const Value copied = read_reference(candidate, element);
            release_supported(copied); // copy7581C: owned clone75778 is a barrier.
            // No release of old destination, even when its tag is6. The source
            // is read before store; saved index controls update under aliasing.
            write_reference(candidate, destination, copied);
            slot(candidate, candidate.sp - 1) = Value::integer(index + 1);
            const uint32_t destination_pc = operand(1, 3);
            require_target(destination_pc); next = destination_pc;
        } else {
            // 6C34C..3A0 directly zeros/pops three slots; no result/release call.
            pop(candidate); pop(candidate); pop(candidate);
        }
    } else if (op == 0x30) {
        int32_t count = static_cast<int32_t>(operand(1, 1));
        slot(candidate, candidate.sp + count);
        push(candidate, Value::integer(candidate.fp));
        candidate.fp = candidate.sp; candidate.sp += count;
    } else if (op == 0x4E) {
        // 6E84C +7534C: only retag/write the FP-relative header; no element
        // allocation, zeroing, release or SP/result change occurs.
        const int32_t index = candidate.fp + static_cast<int32_t>(operand(1, 2));
        Value header; header.word = 0x90000000 | operand(3, 2);
        slot(candidate, index) = header;
    } else if (op >= 0x80 && op <= 0x83) {
        uint8_t width = static_cast<uint8_t>(op - 0x7F);
        uint32_t raw = operand(1, width);
        int32_t value;
        if (width < 4 && raw & (1u << (width * 8 - 1)))
            value = static_cast<int32_t>(raw) - static_cast<int32_t>(1u << (width * 8));
        else value = static_cast<int32_t>(raw & payload_mask);
        push(candidate, Value::integer(value));
    } else if (op == 0x84) {
        //70D90 copy of constant cellE5948, zeroed by initializer76B38.
        push(candidate, {});
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
    } else if (op == 0x93) {
        // 71214..712A0: ctx+24 is the loader's copied local table, not ctx+2C globals.
        // No read/release of its target or of the overwritten stack word occurs.
        const uint32_t index = operand(1, 2);
        require(index < program_.header().local_count, "Opcode93 index outside original ctx24 table");
        push(candidate, Value::local_reference(index, program_.header().pool_end() + index * 4));
    } else if (op == 0x8A) {
        // 70F24..70F9C constructs tag7 from u24, including the final store delay slot.
        const uint32_t target = operand(1, 3);
        require_target(target); // Protective native boundary; original does not validate here.
        Value value; value.word = 0x70000000 | target; push(candidate, value);
    } else if (op == 0x51) {
        require(candidate.sp >= 2, "Assignment requires two operands");
        const Value reference = slot(candidate, candidate.sp - 2);
        const Value previous = read_reference(candidate, reference);
        release_supported(previous);
        // Original 7515C clears destination before the RHS is read. Preserve
        // alias behavior too; assignment transfers ownership rather than copy.
        write_reference(candidate, reference, {});
        const Value value = slot(candidate, candidate.sp - 1);
        write_reference(candidate, reference, value); pop(candidate); pop(candidate);
    } else if (op == 0x52 || op == 0x53) {
        require(candidate.sp > 0, "Opcode53 requires a reference");
        release_supported(candidate.result);
        const Value reference = pop(candidate);
        candidate.result = reference; // Original result retains consumed tagB.
        Value value = read_reference(candidate, reference);
        if (op == 0x52) { release_supported(value); push(candidate, value); value = read_reference(candidate, reference); }
        if (value.kind == Value::Kind::Word && (value.word >> 28 == 1 || value.word >> 28 == 2 || value.word >> 28 == 3)) {
            value.word = recovered::increment_value(value.word);
            write_reference(candidate, reference, value);
        } //76990 performs no store for other tags, including mapped references.
    } else if (op == 0xC2) {
        require(candidate.sp >= 2, "OpcodeC2 requires two scalar operands");
        const auto scalar = [](const Value& value) {
            require(value.kind == Value::Kind::Word, "OpcodeC2 symbolic reference conversion is unproved");
            const uint32_t tag = value.word >> 28;
            require(tag != 4 && tag != 5 && tag != 6, "OpcodeC2 float/string conversion is unimplemented");
            return tag == 9 ? static_cast<int32_t>(value.word & 0xFFF) : signed28(value.word);
        };
        const Value right = pop(candidate), left = pop(candidate);
        const bool less = scalar(left) < scalar(right);
        Value value; value.word = boolean_false | static_cast<uint32_t>(less); push(candidate, value);
    } else if (op == 0xF0) {
        require(candidate.sp > 0, "OpcodeF0 requires a reference");
        //740FC pops/clears the reference slot BEFORE copy7581C reads target.
        const Value reference = pop(candidate);
        const Value value = read_reference(candidate, reference);
        release_supported(value); push(candidate, value);
    } else if (op == 0xF5) {
        require(candidate.sp >= 2, "OpcodeF5 requires reference and integer index");
        const Value reference = slot(candidate, candidate.sp - 2), index_value = slot(candidate, candidate.sp - 1);
        require(index_value.kind == Value::Kind::Word && index_value.word >> 28 == 2,
                "OpcodeF5 index must be tag2; original error path is outside this probe");
        const int32_t index = signed28(index_value.word);
        const Value header = read_reference(candidate, reference);
        require(header.kind == Value::Kind::Word && header.word >> 28 == 9,
                "OpcodeF5 array-check diagnostic/error path is outside this probe");
        require(index >= 0 && static_cast<uint32_t>(index) < (header.word & 0xFFF),
                "OpcodeF5 array index outside original low12 count; error service is unimplemented");
        const uint32_t delta = static_cast<uint32_t>(index) + 1;
        require(reference.index <= UINT32_MAX - delta, "OpcodeF5 reference index overflow");
        const uint32_t target = reference.index + delta;
        Value element;
        if (reference.kind == Value::Kind::StackReference) {
            require(target < stack_slot_limit_, "OpcodeF5 element outside stack allocation/native cap");
            element = Value::stack_reference(target);
        } else if (reference.kind == Value::Kind::LocalReference) {
            require(target < program_.header().local_count, "OpcodeF5 element outside mapped local table");
            element = Value::local_reference(target, program_.header().pool_end() + target * 4);
        } else {
            require(reference.kind == Value::Kind::GlobalReference && target < program_.header().global_count,
                    "OpcodeF5 element outside mapped global table");
            element = Value::global_reference(target, program_.header().global_start() + target * 4);
        }
        pop(candidate); pop(candidate); push(candidate, element);
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
        const std::map<uint32_t, std::pair<uint32_t, uint32_t>> registrations = {
            {10,{0x0C,0x1C29C}}, {11,{0x10,0x1C2B8}}, {16,{0x20,0x1C36C}},
            {29,{0x44,0x1C5AC}}, {35,{0x48,0x1C698}}};
        require(first.kind == Value::Kind::Word && first.word >> 28 == 2,
                "Unimplemented F0 service conversion/type");
        const uint32_t service = static_cast<uint32_t>(signed28(first.word));
        const auto registration = registrations.find(service);
        if (registration != registrations.end()) {
            require(host_abi_projection_.allows_direct_registrations(),
                    "F0 direct registration needs explicit mapped/disjoint/initialized host ABI projection; constructor provenance is unproved");
            // Each original branch stores ArenaBase+signed28(arg) into the
            // singleton pointed to by relocated cell29248. Preserve pointer
            // identity; no callback object/array or default callback is fabricated.
            if (second.kind == Value::Kind::StackReference) slot(candidate, static_cast<int32_t>(second.index));
            else if (second.kind == Value::Kind::LocalReference)
                require(second.index < program_.header().local_count, "F0 local binding outside mapped table");
            else require(second.kind == Value::Kind::GlobalReference && second.index < program_.header().global_count,
                         "F0 registration requires a mapped Arena reference");
        } else require(service == 78 && second.kind == Value::Kind::Word && second.word == boolean_false,
                       "Unimplemented F0 host service/arguments: service=" + std::to_string(service));
        release_supported(candidate.result);
        builtin.valid = true; builtin.pc = command.pc; builtin.id = 0xF0; builtin.argc = 2;
        builtin.handler_va = 0x6AD88; builtin.host_service = service;
        builtin.host_handler_va = registration == registrations.end() ? 0x1D2FC : registration->second.second;
        builtin.arguments = {first, second}; builtin.result.word = boolean_false;
        candidate.result = builtin.result;
        if (registration == registrations.end()) { candidate.host_flag_104_known = true; candidate.host_flag_104 = 0; }
        else candidate.host_reference_bindings[registration->second.first] = second;
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
        event.next_pc = candidate.pc; event.before = snapshot(context_, false); event.after = snapshot(candidate, false);
        for (const auto& entry : candidate.global_writes) {
            const auto old = context_.global_writes.find(entry.first);
            if (old == context_.global_writes.end() || !(old->second == entry.second))
                event.global_write_delta.insert(entry);
        }
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
