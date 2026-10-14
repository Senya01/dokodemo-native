#include "startup_probe.h"
#include "core_hash.h"
#include "recovered/ksc_dispatch_addresses.h"
#include "recovered/game_primitives.h"
#include "recovered/calendar_native.h"
#include "recovered/bulk_array_native.h"
#include <algorithm>
#include <functional>
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
std::string hex(uint32_t value) {
    std::ostringstream out; out << "0x" << std::uppercase << std::hex << value; return out.str();
}
uint32_t handler_for(int opcode) {
    return opcode >= 0 && static_cast<size_t>(opcode) < recovered::ksc_handler_va.size() ?
        recovered::ksc_handler_va[static_cast<size_t>(opcode)] : 0;
}
Value service47_value(recovered::Service47Address address, const script::Program& program) {
    using Table = recovered::Service47Address::Table;
    if (address.table == Table::Stack) return Value::stack_reference(address.index);
    if (address.table == Table::Global)
        return Value::global_reference(address.index, program.header().global_start() + 4 * address.index);
    return Value::local_reference(address.index, program.header().pool_end() + 4 * address.index);
}
recovered::Service47Address service47_address(const Value& value) {
    using Table = recovered::Service47Address::Table;
    require(value.kind != Value::Kind::Word, "Service47 argument needs explicit mapped Arena reference");
    require(value.kind != Value::Kind::Ctx28Reference, "Service47 ctx28 destination mapping is outside recovered writable cells");
    return {value.kind == Value::Kind::StackReference ? Table::Stack :
            value.kind == Value::Kind::GlobalReference ? Table::Global : Table::Local, value.index};
}
class MappedService47Cells final : public recovered::Service47Cells {
public:
    std::function<recovered::Service47Cell(recovered::Service47Address)> reader;
    std::function<void(recovered::Service47Address, recovered::Service47Cell)> writer;
    recovered::Service47Cell load(recovered::Service47Address a) override { return reader(a); }
    void store(recovered::Service47Address a, recovered::Service47Cell v) override { writer(a, v); }
};
recovered::BulkAddress bulk_address(const Value& value) {
    using Table = recovered::BulkAddress::Table;
    require(value.kind != Value::Kind::Word, "Bulk argument needs an explicit mapped Arena identity");
    return {value.kind == Value::Kind::StackReference ? Table::Stack :
        value.kind == Value::Kind::GlobalReference ? Table::Global :
        value.kind == Value::Kind::LocalReference ? Table::Local : Table::Ctx28, value.index};
}
Value bulk_value(recovered::BulkAddress a, const script::Program& program) {
    using Table = recovered::BulkAddress::Table;
    if (a.table == Table::Stack) return Value::stack_reference(a.index);
    if (a.table == Table::Global) return Value::global_reference(a.index, program.header().global_start() + 4 * a.index);
    if (a.table == Table::Local) return Value::local_reference(a.index, program.header().pool_end() + 4 * a.index);
    return Value::ctx28_reference(a.index, program.header().ctx28_start() + 4 * a.index);
}
class MappedBulkCells final : public recovered::BulkCells {
public:
    std::function<recovered::BulkCell(recovered::BulkAddress)> reader;
    std::function<void(recovered::BulkAddress, recovered::BulkCell)> writer;
    recovered::BulkCell load(recovered::BulkAddress a) override { return reader(a); }
    void store(recovered::BulkAddress a, recovered::BulkCell v) override { writer(a, v); }
};
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
Value Value::ctx28_reference(uint32_t index, uint32_t file_offset) {
    Value result; result.kind = Kind::Ctx28Reference; result.index = index;
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
                          uint16_t stack_slot_limit, HostAbiProjection projection, recovered::Service47Fixture service47,
                          recovered::StringFixture strings,StringService20Projection string_getters,Service48Projection service48_projection)
    : program_(program), host_byte_(host_byte), stack_slot_limit_(stack_slot_limit), host_abi_projection_(projection),
      service47_fixture_(std::move(service47)), string_fixture_(std::move(strings)),string_getters_(std::move(string_getters)),
      service48_projection_(std::move(service48_projection)) {
    context_.pc = program.header().entrypoint;
    context_.service47 = recovered::Service47InitialState(service47_fixture_);
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
    if (reference.kind == Value::Kind::Ctx28Reference) {
        require(reference.index <= UINT32_MAX - following_words, "Ctx28 reference index overflow");
        const uint32_t index = reference.index + following_words;
        require(index < program_.header().ctx28_count, "Referenced ctx28 word outside original read-only table");
        Value value; value.word = program_.initial_word(script::Table::Ctx28, static_cast<uint16_t>(index));
        require(value.word >> 28 != 5 && value.word >> 28 != 11, "Ctx28 initial pool/reference rebasing is outside proved domain");
        return value;
    }
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
    result.service47 = context.service47;
    result.string_arena = context.strings.summary();
    result.general_arena = context.general_heap.summary();result.service48=context.service48;
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
    const auto release_supported = [&](const Value& value) {
        if(value.kind==Value::Kind::Word && value.word>>28==6) {
            require(candidate.strings.ready(),"Owned tag6 string release/copy is outside this reference trace without a mapped live string arena");
            uint32_t word=value.word;candidate.strings.release7515C(word);
        }
    };
    const auto copied_value = [&](Value destination,const Value& source) {
        if(source.kind!=Value::Kind::Word || source.word>>28!=6)return source;
        require(destination.kind==Value::Kind::Word,"Owned copy destination symbolic raw payload/reuse is outside bounded string domain");
        require(candidate.strings.ready(),"Owned tag6 string release/copy is outside this reference trace without a mapped live string arena");
        candidate.strings.copy7581C(destination.word,source.word);return destination;
    };
    const auto copy_push = [&](const Value& source) {push(candidate,copied_value(slot(candidate,candidate.sp),source));};
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
        // 7581C overwrites without releasing the old destination. Both operands
        // remain; 6BF50..6BF90 pushes exactly I(1), irrespective of array count.
        write_reference(candidate, left, copied_value(first.kind==Value::Kind::Word && first.word>>28==6 ? read_reference(candidate,left) : Value{},first));
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
            // No release of old destination, even when its tag is6. The source
            // is read before store; saved index controls update under aliasing.
            write_reference(candidate, destination, copied_value(copied.kind==Value::Kind::Word && copied.word>>28==6 ? read_reference(candidate,destination) : Value{},copied));
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
    } else if (op == 0x99) {
        //715F8..71680 pushes ctx+28 + unsigned16 index as Arena-relative
        //tagB. This constructs an identity; it does not read/rebase its word.
        //670DC..670EC bind ctx28 to the original source table after locals.
        const uint32_t index = operand(1, 2);
        require(index < program_.header().ctx28_count, "Opcode99 index outside original ctx28 mapping");
        push(candidate, Value::ctx28_reference(index, program_.header().ctx28_start() + 4 * index));
    } else if (op == 0x89) {
        //70EEC..70F20 copies ctx+30 into the old top (7581C performs no
        //destination release), then clears ctx+30 with7515C. Owned clone is
        //unproved. Arena slots/result are disjoint in the symbolic model.
        copy_push(candidate.result);release_supported(candidate.result);candidate.result = {};
    } else if (op == 0x84) {
        //70D90 copy of constant cellE5948, zeroed by initializer76B38.
        push(candidate, {});
    } else if (op == 0x85) {
        push(candidate, Value::integer(0));
    } else if (op == 0x86) {
        Value value; value.word = boolean_false; push(candidate, value);
    } else if (op == 0x90) {
        int32_t index = candidate.fp + static_cast<int32_t>(operand(1, 2));
        Value value = slot(candidate, index);copy_push(value);
    } else if (op == 0x91) {
        int32_t index = candidate.fp + static_cast<int32_t>(operand(1, 2));
        slot(candidate, index); push(candidate, Value::stack_reference(static_cast<uint32_t>(index)));
    } else if (op == 0x95) {
        uint32_t index = operand(1, 2);
        require(index < program_.header().global_count, "Global reference outside original table");
        push(candidate, Value::global_reference(index, program_.header().global_start() + index * 4));
    } else if (op == 0x94) {
        require(string_getters_.enabled && string_getters_.shared_global_table_mapped && host_abi_projection_.allows_direct_registrations(),
                "Opcode94 needs explicit shared cell1C949C/current global-table identity; producer mapping unproved");
        const uint32_t index=operand(1,2);
        require(index<program_.header().global_count,"Opcode94 shared global index outside mapped table");
        copy_push(read_reference(candidate,Value::global_reference(index,program_.header().global_start()+4*index)));
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
        if (op == 0x52) {copy_push(value);value = read_reference(candidate, reference);}
        if (value.kind == Value::Kind::Word && (value.word >> 28 == 1 || value.word >> 28 == 2 || value.word >> 28 == 3)) {
            value.word = recovered::increment_value(value.word);
            write_reference(candidate, reference, value);
        } //76990 performs no store for other tags, including mapped references.
    } else if (op == 0xD1 || op == 0xD2 || op == 0xE3 || op == 0xD4) {
        require(candidate.sp >= 2, "OpcodeD1 requires two operands");
        const auto scalar = [](const Value& v) {
            recovered::BulkCell cell; cell.word=v.word; cell.is_reference=v.kind!=Value::Kind::Word;
            return recovered::BulkNumeric(cell);
        };
        const Value right=slot(candidate,candidate.sp-1);
        const int32_t denominator=scalar(right);release_supported(right);pop(candidate);
        const Value left=slot(candidate,candidate.sp-1);
        const int32_t numerator=scalar(left);release_supported(left);pop(candidate);
        require((op!=0xD1 && op!=0xD2) || denominator!=0,"OpcodeD1/D2 division-zero6B3C0 original error service cut");
        const int64_t quotient=op==0xD4 ? static_cast<int64_t>((static_cast<uint32_t>(numerator)&static_cast<uint32_t>(denominator))&payload_mask) :
            op==0xE3 ? static_cast<int64_t>((static_cast<uint32_t>(numerator) << (static_cast<uint32_t>(denominator)&31))&payload_mask) :
            op==0xD1 ? static_cast<int64_t>(numerator)/denominator : static_cast<int64_t>(numerator)%denominator;
        push(candidate,Value::integer(static_cast<int32_t>(quotient)));
    } else if (op == 0x70) {
        require(candidate.sp>0,"Opcode70 needs one operand");release_supported(candidate.result);candidate.result={};
        candidate.result=pop(candidate); //708FC transfers ownership; no source copy/release.
    } else if (op == 0xB9) {
        require(candidate.sp>0,"OpcodeB9 needs one operand");const Value input=pop(candidate);
        const bool is_integer_zero=input.kind==Value::Kind::Word && input.word==0x20000000;
        Value result;result.word=boolean_false|static_cast<uint32_t>(!is_integer_zero);push(candidate,result);
        release_supported(input); // Original7515C temporary release follows output store.
    } else if (op == 0xC5) {
        require(candidate.sp>=2,"OpcodeC5 requires two operands");
        const Value right=pop(candidate),left=pop(candidate);
        const auto scalar=[](const Value& v) {
            recovered::BulkCell c;c.word=v.word;c.is_reference=v.kind!=Value::Kind::Word;return recovered::BulkNumeric(c);
        };
        const bool greater_equal=scalar(left)>=scalar(right);
        Value result;result.word=boolean_false|static_cast<uint32_t>(greater_equal);push(candidate,result);
        release_supported(left);release_supported(right);
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
        copy_push(value);
    } else if (op == 0xF4) {
        //74278 pops both raw operands before checking/dereferencing the array.
        //743E0 copies the element through7581C, retaining symbolic identity.
        require(candidate.sp >= 2, "OpcodeF4 requires reference and integer index");
        const Value index_value = pop(candidate), reference = pop(candidate);
        require(index_value.kind == Value::Kind::Word && index_value.word >> 28 == 2,
                "OpcodeF4 index/type original error path remains a cut");
        const int32_t index = signed28(index_value.word);
        const Value header = read_reference(candidate, reference);
        require(header.kind == Value::Kind::Word && header.word >> 28 == 9 &&
                index >= 0 && static_cast<uint32_t>(index) < (header.word & 0xFFF),
                "OpcodeF4 array/bounds original error path remains a cut");
        const Value value = read_reference(candidate, reference, static_cast<uint32_t>(index) + 1);
        copy_push(value);
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
        if (id == 0x09) {
            require(argc == 5,
                    "Builtin09 argc other than5 is outside tested native domain; argc0 needs external clock");
            //684DC..68550 releases ctx+30 before reading arguments. Production
            //symbolic references never alias the result subobject; raw address,
            //float/string conversion and owned release remain atomic barriers.
            release_supported(candidate.result);
            std::vector<Value> arguments;
            std::array<int32_t, 5> fields{};
            for (uint32_t i = 0; i < argc; ++i) {
                const Value value = slot(candidate, candidate.sp - static_cast<int32_t>(argc) + static_cast<int32_t>(i));
                require(value.kind == Value::Kind::Word,
                        "Builtin09 numeric reference payload is outside mapped scalar domain");
                const uint32_t tag = value.word >> 28;
                require(tag != 4 && tag != 5 && tag != 6 && tag != 11,
                        "Builtin09 float/string conversion or owned release is unproved");
                fields[i] = tag == 9 ? static_cast<int32_t>(value.word & 0xFFF) : signed28(value.word);
                arguments.push_back(value);
            }
            const uint32_t scalar = recovered::CalendarScalar7C660(recovered::CalendarRecord689B8(fields));
            Value result; result.word = 0x30000000u | (scalar & payload_mask);
            //Original7515C clears each argument immediately after its record
            //store, and68BAC writes result before685C4 commits signed SP.
            for (uint32_t i = 0; i < argc; ++i)
                slot(candidate, candidate.sp - static_cast<int32_t>(argc) + static_cast<int32_t>(i)) = {};
            candidate.sp -= static_cast<int32_t>(argc); candidate.result = result;
            builtin.valid = true; builtin.pc = command.pc; builtin.id = 9;
            builtin.argc = static_cast<uint8_t>(argc); builtin.handler_va = 0x689B8;
            builtin.arguments = std::move(arguments); builtin.result = result;
        } else if (id == 4) {
            require(argc==1,"Builtin04 argc outside reached scalar conversion contract");
            release_supported(candidate.result);candidate.result={};
            const Value argument=slot(candidate,candidate.sp-1);
            recovered::BulkCell cell;cell.word=argument.word;cell.is_reference=argument.kind!=Value::Kind::Word;
            const int32_t scalar=recovered::BulkNumeric(cell);
            release_supported(argument);pop(candidate);candidate.result=Value::integer(scalar);
            builtin.valid=true;builtin.pc=command.pc;builtin.id=4;builtin.argc=1;builtin.handler_va=0x6863C;
            builtin.arguments={argument};builtin.result=candidate.result;
        } else if (id == 97) {
            require(argc == 2, "Builtin97 argc outside original two-argument contract");
            const int32_t first_slot = candidate.sp - 2;
            const std::vector<Value> arguments{slot(candidate, first_slot), slot(candidate, first_slot + 1)};
            MappedBulkCells cells;
            cells.reader = [&](recovered::BulkAddress a) {
                const Value value = a.table == recovered::BulkAddress::Table::Result ? candidate.result :
                    read_reference(candidate, bulk_value(a, program_));
                recovered::BulkCell cell; cell.word = value.word;
                cell.is_reference = value.kind != Value::Kind::Word;
                if (cell.is_reference) cell.reference = bulk_address(value);
                return cell;
            };
            cells.writer = [&](recovered::BulkAddress a, recovered::BulkCell cell) {
                Value value; value.word = cell.word;
                if (cell.is_reference) value = bulk_value(cell.reference, program_);
                if (a.table == recovered::BulkAddress::Table::Result) candidate.result = value;
                else write_reference(candidate, bulk_value(a, program_), value);
            };
            (void)recovered::BulkBuiltin97(cells,
                {recovered::BulkAddress::Table::Stack, static_cast<uint32_t>(first_slot)},
                {recovered::BulkAddress::Table::Result, 0});
            candidate.sp = first_slot;
            builtin.valid = true; builtin.pc = command.pc; builtin.id = 97; builtin.argc = 2;
            builtin.handler_va = 0x6A6A8; builtin.arguments = arguments; builtin.result = candidate.result;
        } else {
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
            } else if (service == 47) {
                require(host_abi_projection_.allows_direct_registrations(),
                        "Service47 requires legacy mapped/disjoint/initialized host ABI premises and SLOT0/1");
                (void)service47_address(second);
                if (second.kind == Value::Kind::StackReference) slot(candidate, static_cast<int32_t>(second.index));
                else require(second.index < (second.kind == Value::Kind::GlobalReference ? program_.header().global_count :
                                             program_.header().local_count), "Service47 argument outside mapped table");
                require(service47_fixture_.enabled, "Service47 conditional buffer/control availability is not supplied");
            } else if (service == 20) {
                require(host_abi_projection_.allows_direct_registrations(),
                        "Service20 requires mapped linked host ABI premises; construction unproved");
                require(second.kind==Value::Kind::Word && second.word>>28==2 && signed28(second.word)>=0 && signed28(second.word)<=10,
                        "Service20 selector conversion/type outside recovered literal tag2 selector0..10 domain");
                require(string_fixture_.enabled,"Service20 source string availability/owned allocator readiness is not supplied");
                if(second.word!=0x20000000u)require(string_getters_.enabled && string_getters_.memory_disjoint,
                        "Service20 extra selector source window/record availability is not supplied");
            } else if(service==48) {
                require(host_abi_projection_.allows_direct_registrations(),"Service48 legacy linked host mapping premises unavailable");
                (void)service47_address(second);
                require(service48_projection_.fixture.enabled && service48_projection_.general_heap.enabled &&
                        !service48_projection_.prepared_payload.empty(),
                        "Service48 requires explicit NONNULL record/payload and separate GENERAL heap readiness; producerunknown");
            } else require(service == 78 && second.kind == Value::Kind::Word && second.word == boolean_false,
                           "Unimplemented F0 host service/arguments: service=" + std::to_string(service));
            release_supported(candidate.result);
            builtin.valid = true; builtin.pc = command.pc; builtin.id = 0xF0; builtin.argc = 2;
            builtin.handler_va = 0x6AD88; builtin.host_service = service;
            builtin.host_handler_va = service == 48 ? 0x1C9E4 : service == 20 ? 0x1C408 : service == 47 ? 0x1C958 : registration == registrations.end() ? 0x1D2FC : registration->second.second;
            builtin.arguments = {first, second}; builtin.result.word = boolean_false;
            //684DC releases prior result;6ADC4/6AE10 clear arguments before
            //the host handler. Destination/argument aliases see these clears.
            candidate.result = {};
            pop(candidate); pop(candidate);
            if (service == 20) {
                if(!candidate.strings.ready())candidate.strings=recovered::StringArena(string_fixture_);
                //Host1C184 first writes false into result; selector0's7515C
                //therefore sees false. F0 has already released prior ctx+30.
                uint32_t result_word=boolean_false;
                const uint32_t selector=static_cast<uint32_t>(signed28(second.word));
                if(selector==0)recovered::StringService20Selector0(candidate.strings,string_fixture_,0,result_word);
                else if(selector==1 || selector==5 || selector==7 || selector==8) {
                    const auto window=string_getters_.string_windows.find(selector);
                    require(window!=string_getters_.string_windows.end() && window->second.size()==16,
                            "Service20 selector string producer window not explicitly mapped");
                    candidate.strings.release7515C(result_word);
                    if((selector!=7 && selector!=8) || window->second[0]!=0) {
                        const uint32_t address=selector==1?0x10E480u:selector==5?0x10E4ACu:selector==7?0x10E4D0u:0x10E4C0u;
                        candidate.strings.assign_mapped753E0(result_word,window->second,address);
                    }
                } else if(selector==3) {
                    require(string_getters_.date_record_mapped,"Service20 date24F34(0) record producer/readability unavailable");
                    const auto& d=string_getters_.date_words;
                    require(d[24/4]<=65535 && d[20/4]<=255 && d[16/4]<=255 && d[12/4]<=255 && d[8/4]<=255,
                            "Service20 date1D354 bounded fields outside proved calendar arithmetic relation");
                    recovered::CalendarRecord record{static_cast<uint8_t>(d[6]),static_cast<uint8_t>(d[6]>>8),
                        static_cast<uint8_t>(d[5]),static_cast<uint8_t>(d[4]),static_cast<uint8_t>(d[3]),static_cast<uint8_t>(d[2]),0};
                    result_word=0x30000000u|(recovered::CalendarScalar7C660(record)&payload_mask);
                } else {
                    const auto scalar=string_getters_.scalar_words.find(selector);
                    require(scalar!=string_getters_.scalar_words.end(),"Service20 scalar producer cell not explicitly mapped");
                    require(selector!=10 || scalar->second<=255,"Service20 selector10 byte cell value outside mapped u8 domain");
                    if(selector==9) {
                        const auto w=string_getters_.string_windows.find(8);
                        require(w!=string_getters_.string_windows.end() && w->second.size()==16,"Service20 selector9 conditional string guard unavailable");
                        if(w->second[0]==0)result_word=0;
                        else result_word=0x20000000u|(scalar->second&payload_mask);
                    } else result_word=(selector==10?0x10000000u:0x20000000u)|(scalar->second&payload_mask);
                }
                //6AD88 host result lives in a stack TEMP. 6AE24 copies that
                //value through7581C into cleared ctx+30, then7515C releases
                //TEMP. Owned results therefore require two allocations and
                //one release here, before later opcode89 performs its copy.
                Value temporary;temporary.word=result_word;
                builtin.result=copied_value(candidate.result,temporary);
                release_supported(temporary);
            } else if(service==48) {
                if(!candidate.general_heap.ready()) {
                    candidate.general_heap=recovered::StringArena(service48_projection_.general_heap);
                    auto record=recovered::OwnedService48Record::Create(candidate.general_heap);
                    record.SupplyLoadedPayload(candidate.general_heap,service48_projection_.prepared_payload,-1);
                    candidate.service48.manager_record=record.pointer();
                }
                MappedService47Cells cells;
                cells.reader=[&](recovered::Service47Address a) {
                    const Value v=read_reference(candidate,service47_value(a,program_));
                    recovered::Service47Cell c;c.word=v.word;c.is_reference=v.kind!=Value::Kind::Word;
                    if(c.is_reference)c.reference=service47_address(v);
                    return c;
                };
                cells.writer=[&](recovered::Service47Address a,recovered::Service47Cell c) {
                    Value v;v.word=c.word;if(c.is_reference)v=service47_value(c.reference,program_);
                    write_reference(candidate,service47_value(a,program_),v);
                };
                auto fixture=service48_projection_.fixture;
                fixture.arena_base=candidate.general_heap.common_base();
                fixture.destination_offset=second.kind==Value::Kind::StackReference?second.index*4:
                    (second.kind==Value::Kind::GlobalReference?0x40000u:0x200000u)+4*second.index-fixture.arena_base;
                const auto result48=recovered::Service48Host1C9E4(cells,service47_address(second),fixture,candidate.service48,candidate.general_heap);
                require(result48.completed,"Service48 boundary "+hex(result48.boundary.source_pc)+" target"+hex(result48.boundary.target)+": "+result48.boundary.detail);
                candidate.host_reference_bindings[0x58]=second;
                builtin.result.word=result48.return_word;
            } else if (service == 47) {
                candidate.host_reference_bindings[0x54] = second; //1C970 JAL delay slot.
                MappedService47Cells cells;
                cells.reader = [&](recovered::Service47Address a) {
                    const Value v = read_reference(candidate, service47_value(a, program_));
                    recovered::Service47Cell cell; cell.word = v.word;
                    cell.is_reference = v.kind != Value::Kind::Word;
                    if (cell.is_reference) cell.reference = service47_address(v);
                    return cell;
                };
                cells.writer = [&](recovered::Service47Address a, recovered::Service47Cell cell) {
                    Value v; v.word = cell.word;
                    if (cell.is_reference) v = service47_value(cell.reference, program_);
                    write_reference(candidate, service47_value(a, program_), v);
                };
                recovered::Service47Host1C958(cells, service47_address(second), service47_fixture_, candidate.service47);
                candidate.host_reference_bindings[0x50] = {}; //1B334,1B354 delay stores.
                candidate.host_reference_bindings[0x54] = {};
            } else if (registration == registrations.end()) { candidate.host_flag_104_known = true; candidate.host_flag_104 = 0; }
            else candidate.host_reference_bindings[registration->second.first] = second;
            candidate.result = builtin.result;
        }
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
