#include "script_vm.h"
#include "core_hash.h"
#include "core/cp932.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace dokodemo::script {
namespace {
constexpr uint32_t missing = UINT32_MAX;
struct LocatedError : std::runtime_error {
    Diagnostic location;
    explicit LocatedError(Diagnostic value) : std::runtime_error(value.text()), location(std::move(value)) {}
};
const char* ksc_hashes[] = {
    "89882ec234e9bb3cb204bbabb6b1cdc190242275450179c2619c85aea33066b2",
    "16ee223dcb98c962648d4b62ffd14e06d19f9ea6b28a2d39d2c5a59e2c607a24",
    "c64773090b1c331d043b264b291c8948a8b99787f541ddc44d8e1d027958c93b",
    "1a0c7a42970951d46254aaaccff5b5b5a4b551b1bcf63b9fb0ca4a9ac3021405",
    "d94768138b839591a81f4a7d3eebe1887877a799f9c73f287a253431d90a7ec3"};
const char* dic_hashes[] = {
    "07c48c07a20ad3c063e9e0a9d686990dc66f81bc2cc45c98ea5deea255b66791",
    "24e01626f5b7a89013beae6fed336d58710774d5c0ca0167002426588b2d9e73",
    "1ff6a6b41c5034bec862d1d59eff76d09f41afda4bfed9c48be23a4d76d91b9a",
    "ed1ec9af20b770d52d32edbc84c51a32d48d9add1d71c207f218ccb641f14cd5",
    "f07dd6d7de3b1136710f2dbfe528ab1eb9f9346c0482f1964f2a4744077d0086"};

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open script resource");
    auto length = input.tellg();
    if (length < 0 || length > 16 * 1024 * 1024) throw std::runtime_error("Invalid script resource size");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) throw std::runtime_error("Short script resource read");
    return bytes;
}
std::string hash_bytes(const std::vector<uint8_t>& bytes) {
    return core::sha256_hex(bytes.data(), bytes.size());
}
template<size_t N> bool known(const std::string& hash, const char* const (&hashes)[N]) {
    return std::any_of(std::begin(hashes), std::end(hashes), [&](const char* value) { return hash == value; });
}
uint32_t uint_at(const std::vector<uint8_t>& bytes, uint32_t at, uint8_t width, uint32_t limit) {
    if (!width || width > 4 || uint64_t(at) + width > limit || limit > bytes.size())
        throw std::runtime_error("Truncated operand");
    uint32_t value = 0;
    for (uint8_t index = 0; index < width; ++index) value |= uint32_t(bytes[at + index]) << (index * 8);
    return value;
}
bool inline_control(uint8_t byte) {
    return byte == 0x14 || byte == 0x15 || byte == 0x17 || (byte >= 0x18 && byte <= 0x1C);
}
int fixed_width(int op) {
    switch (op) {
    case 0x10: case 0x11: case 0x12: case 0x15: case 0x18: case 0x21: case 0x29:
    case 0x82: case 0x88: case 0x8A: return 3;
    case 0x20: case 0x28: case 0x4E: case 0x83: return 4;
    case 0x22: case 0x30: case 0x43: case 0x80: case 0xA0: return 1;
    case 0x71: case 0x72: case 0x73: case 0x75: case 0x81: case 0x8B:
    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95:
    case 0x96: case 0x97: case 0x98: case 0x99: return 2;
    default: return -1;
    }
}
bool no_immediate(int op) {
    static const std::unordered_set<int> ops = {
        0x00,0x03,0x08,0x0B,0x0C,0x0D,0x0E,0x14,0x1A,0x1B,0x1C,0x23,0x31,0x32,0x38,0x3A,0x3B,0x41,0x44,
        0x50,0x51,0x52,0x53,0x55,0x57,0x5B,0x5D,0x5F,0x61,0x64,0x65,0x67,0x6D,0x70,0x78,
        0x84,0x85,0x86,0x87,0x89,0x8C,0x8D,0xB0,0xB1,0xB2,0xB3,0xB4,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,
        0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC8,0xC9,0xD0,0xD1,0xD2,0xD4,0xD5,0xD6,0xE0,0xE1,0xE2,0xE3,0xF0,0xF4,0xF5};
    return ops.count(op) != 0;
}
std::string c_string(const std::vector<uint8_t>& bytes, uint32_t at, uint32_t end) {
    if (at >= end || end > bytes.size()) throw std::runtime_error("String address outside section");
    auto begin = bytes.begin() + at, limit = bytes.begin() + end;
    auto zero = std::find(begin, limit, 0);
    if (zero == limit) throw std::runtime_error("Unterminated data string");
    return std::string(begin, zero);
}
uint32_t cp932_length(const std::string& text) {
    const auto count = core::count_cp932(text);
    if (!count.valid) throw std::runtime_error("Invalid CP932 dictionary text");
    return static_cast<uint32_t>(count.code_units);
}
bool usable(const Value& value) { return value.kind != Value::Kind::Unknown && value.kind != Value::Kind::Opaque; }
} // namespace

std::string Diagnostic::text() const {
    std::ostringstream out;
    out << reason << "; pc=0x" << std::hex << pc << "; file=0x" << file_offset << "; opcode=";
    if (opcode < 0) out << opcode; else out << "0x" << opcode;
    return out.str();
}
const Command* Program::command(uint32_t pc) const {
    if (pc >= command_index_.size() || command_index_[pc] == missing) return nullptr;
    return &commands_[command_index_[pc]];
}
uint32_t Program::operand(const Command& cmd, uint32_t relative, uint8_t width) const {
    return uint_at(bytes_, 28 + cmd.pc + relative, width, 28 + cmd.pc + cmd.size);
}
uint32_t Program::initial_word(Table table, uint16_t index) const {
    uint32_t start = 0, count = 0;
    switch (table) {
    case Table::Local: start = header_.pool_end(); count = header_.local_count; break;
    case Table::Global: start = header_.global_start(); count = header_.global_count; break;
    case Table::Ctx28: start = header_.ctx28_start(); count = header_.ctx28_count; break;
    default: throw std::runtime_error("Frame is not an initial file table");
    }
    if (index >= count) throw std::runtime_error("Initial table index out of bounds");
    return uint_at(bytes_, start + 4 * index, 4, static_cast<uint32_t>(bytes_.size()));
}
std::string Program::pool_bytes(uint32_t relative) const {
    if (relative >= header_.pool_size) throw std::runtime_error("Pool pointer out of bounds");
    return c_string(bytes_, header_.pool_start() + relative, header_.pool_end());
}
bool Program::load_original(const std::filesystem::path& path, Diagnostic& error) {
    try { return load_original_bytes(read_file(path), path, error); }
    catch (const std::exception& ex) { *this = Program{}; error = {ex.what()}; return false; }
}
bool Program::load_original_bytes(std::vector<uint8_t> bytes, const std::filesystem::path& source, Diagnostic& error) {
    *this = Program{};
    error = {};
    try {
        source_ = {source, hash_bytes(bytes)};
        bytes_ = std::move(bytes);
        parse();
        if (!known(source_.sha256, ksc_hashes)) throw std::runtime_error("Unverified KSC SHA256; execution refused");
        error = {};
        return true;
    } catch (const LocatedError& ex) {
        error = ex.location;
        *this = Program{};
        return false;
    } catch (const std::exception& ex) {
        error.reason = ex.what();
        *this = Program{};
        return false;
    }
}
recovered::CurrentTablesOwner Program::load_current_tables(recovered::CurrentTablesInput input) const {
    if (!loaded()) throw std::runtime_error("Current source loader requires a verified original Program");
    if (input.readable.size() != bytes_.size() || input.writable.size() != bytes_.size())
        throw std::runtime_error("Current source loader requires exact explicit image byte masks");
    input.image = bytes_;
    input.source_sha256 = source_.sha256;
    return recovered::CurrentTablesOwner::Load(input);
}
void Program::parse() {
    auto size = static_cast<uint32_t>(bytes_.size());
    if (size < 28 || uint_at(bytes_, 0, 4, size) != 0x0043534B || uint_at(bytes_, 4, 4, size) != 0x20030204)
        throw std::runtime_error("Unsupported KSC header");
    header_ = {uint_at(bytes_, 8, 4, size), uint_at(bytes_, 12, 4, size), uint_at(bytes_, 16, 4, size),
               static_cast<uint16_t>(uint_at(bytes_, 20, 2, size)), static_cast<uint16_t>(uint_at(bytes_, 22, 2, size)),
               static_cast<uint16_t>(uint_at(bytes_, 24, 2, size)), static_cast<uint16_t>(uint_at(bytes_, 26, 2, size))};
    uint64_t equation = 28ull + header_.code_size + header_.pool_size +
        4ull * (header_.local_count + header_.ctx28_count + header_.global_count);
    if (equation != size || header_.entrypoint >= header_.code_size) throw std::runtime_error("KSC size equation/entry invalid");
    uint32_t pos = 28, limit = header_.pool_start();
    command_index_.assign(header_.code_size, missing);
    std::unordered_set<uint32_t> cases;
    auto parse_case = [&](uint32_t at, std::vector<Reference>& refs) -> uint32_t {
        if (at < 28) throw std::runtime_error("Switch header before code");
        uint32_t word = uint_at(bytes_, at, 4, limit), tag = word >> 28;
        if (tag == 5 || tag == 7) refs.push_back({at, word & 0x0FFFFFFF, 4, tag == 5});
        if (tag) refs.push_back({at + 4, uint_at(bytes_, at + 4, 3, limit), 3, false});
        return tag ? 7 : 4;
    };
    while (pos < limit) {
        uint32_t start = pos;
        Command cmd;
        cmd.pc = start - 28;
        cmd.opcode = bytes_[pos++];
        try {
            if (cases.count(start)) {
                cmd.opcode = -1;
                pos = start + parse_case(start, cmd.references);
            } else if (cmd.opcode == 0x48 || cmd.opcode == 0x49 || cmd.opcode == 0x4A || cmd.opcode == 0x8E) {
                bool ended = false;
                while (pos < limit) {
                    uint8_t byte = bytes_[pos];
                    if (!byte) { ++pos; ended = true; break; }
                    if (inline_control(byte)) {
                        auto index = static_cast<uint16_t>(uint_at(bytes_, pos + 1, 2, limit));
                        cmd.controls.push_back({pos, index, byte}); pos += 3;
                    } else ++pos;
                }
                if (!ended) throw std::runtime_error("Unterminated inline");
            } else if (cmd.opcode == 0xF6 || cmd.opcode == 0xF7) {
                pos += uint_at(bytes_, pos, 1, limit) ? 3 : 1;
            } else if (cmd.opcode == 0x24 || cmd.opcode == 0x25) {
                if (cmd.opcode == 0x24) ++pos;
                pos += uint_at(bytes_, pos + 2, 1, limit) ? 5 : 3;
            } else if (cmd.opcode == 0x13) {
                uint32_t at = pos, first_size = 0;
                std::unordered_set<uint32_t> seen;
                while (true) {
                    if (!seen.insert(at).second) throw std::runtime_error("Cyclic linked switch");
                    std::vector<Reference> refs;
                    uint32_t extent = parse_case(at, refs);
                    if (at == pos) { first_size = extent; cmd.references = refs; }
                    else cases.insert(at);
                    if (extent == 4) break;
                    at = 28 + refs.back().target;
                }
                pos += first_size;
            } else {
                int width = fixed_width(cmd.opcode);
                if (width < 0 && !no_immediate(cmd.opcode)) throw std::runtime_error("Unaudited opcode");
                pos += width < 0 ? 0 : static_cast<uint32_t>(width);
                if (cmd.opcode == 0x10 || cmd.opcode == 0x11 || cmd.opcode == 0x12 || cmd.opcode == 0x15 ||
                    cmd.opcode == 0x18 || cmd.opcode == 0x21 || cmd.opcode == 0x29 || cmd.opcode == 0x8A)
                    cmd.references.push_back({start + 1, uint_at(bytes_, start + 1, 3, limit), 3, false});
                else if (cmd.opcode == 0x20 || cmd.opcode == 0x28)
                    cmd.references.push_back({start + 2, uint_at(bytes_, start + 2, 3, limit), 3, false});
                else if (cmd.opcode == 0x88)
                    cmd.references.push_back({start + 1, uint_at(bytes_, start + 1, 3, limit), 3, true});
            }
            if (pos > limit) throw std::runtime_error("Command exceeds code");
        } catch (const std::exception& ex) {
            Diagnostic location{ex.what(), cmd.pc, start, cmd.opcode};
            throw LocatedError(std::move(location));
        }
        cmd.size = pos - start;
        command_index_[cmd.pc] = static_cast<uint32_t>(commands_.size());
        references_.insert(references_.end(), cmd.references.begin(), cmd.references.end());
        commands_.push_back(std::move(cmd));
    }
    for (uint32_t at : cases) if (!command(at - 28)) throw std::runtime_error("Switch overlaps command");
    if (!command(header_.entrypoint) || command(header_.entrypoint)->opcode < 0) throw std::runtime_error("Entry misses command boundary");
    for (uint32_t at = header_.pool_end(); at < size; at += 4) {
        uint32_t value = uint_at(bytes_, at, 4, size), tag = value >> 28;
        if (tag == 5 || tag == 7) references_.push_back({at, value & 0x0FFFFFFF, 4, tag == 5});
    }
    std::unordered_set<uint32_t> pools;
    for (uint32_t at = header_.pool_start(); at < header_.pool_end();) {
        pools.insert(at - header_.pool_start());
        at += static_cast<uint32_t>(c_string(bytes_, at, header_.pool_end()).size()) + 1;
    }
    for (const auto& ref : references_) {
        if (ref.pool ? !pools.count(ref.target) : !command(ref.target))
            throw std::runtime_error("Stored reference misses structural boundary");
    }
}

bool Dictionary::load_original(const std::filesystem::path& path, Diagnostic& error) {
    try { return load_original_bytes(read_file(path), path, error); }
    catch (const std::exception& ex) { source_ = {}; records_.clear(); error.reason = ex.what(); return false; }
}
bool Dictionary::load_original_bytes(std::vector<uint8_t> data, const std::filesystem::path& source,
                                     Diagnostic& error) {
    source_ = {}; records_.clear();
    try {
        source_ = {source, hash_bytes(data)};
        if (!known(source_.sha256, dic_hashes)) throw std::runtime_error("Unverified DIC SHA256");
        auto size = static_cast<uint32_t>(data.size());
        if (uint_at(data, 0, 4, size) != 0x900000A2) throw std::runtime_error("Unsupported DIC header");
        uint32_t previous = 652;
        for (uint32_t i = 0; i < 162; ++i) {
            uint32_t pointer = uint_at(data, 4 + 4 * i, 4, size);
            uint32_t start = pointer & 0xFFFFFF;
            uint32_t end = i == 161 ? size : uint_at(data, 8 + 4 * i, 4, size) & 0xFFFFFF;
            error = {"", 0, start, -1};
            if ((pointer >> 24) != 0xB0 || start != previous || start >= end || uint64_t(start) + 80 > end)
                throw std::runtime_error("Invalid DIC record extent");
            DictionaryRecord record; record.index = i; record.file_offset = start; record.end = end;
            for (uint32_t n = 0; n < 20; ++n) record.words[n] = uint_at(data, start + 4 * n, 4, end);
            if (record.words[0] != 0x90000013) throw std::runtime_error("Unsupported DIC record header");
            for (uint32_t off : {4u,0x10u,0x14u,0x20u,0x24u,0x30u,0x34u,0x4Cu})
                if ((record.words[off / 4] >> 24) != 0x20) throw std::runtime_error("Unsupported DIC integer tag");
            for (uint32_t off : {0x18u,0x1Cu})
                if ((record.words[off / 4] >> 24) != 0x30) throw std::runtime_error("Unsupported DIC field tag");
            for (uint32_t off : {0x28u,0x2Cu,0x3Cu,0x40u,0x44u})
                if (record.words[off / 4]) throw std::runtime_error("Unsupported DIC opaque field");
            if (record.words[18] && (record.words[18] >> 24) != 0x20) throw std::runtime_error("Unsupported DIC field48");
            uint32_t expected = start + 80;
            std::string* fields[] = {&record.written_cp932, &record.reading_cp932, &record.alternate_cp932};
            uint32_t offsets[] = {8,12,0x38};
            for (uint32_t f = 0; f < 3; ++f) {
                uint32_t word = record.words[offsets[f] / 4];
                if (f == 2 && !word) continue;
                uint32_t at = word & 0xFFFFFF;
                if ((word >> 24) != 0x60 || at != expected) throw std::runtime_error("Invalid packed DIC text pointer");
                *fields[f] = c_string(data, at, end); cp932_length(*fields[f]);
                record.text_offsets[f] = at;
                expected = at + static_cast<uint32_t>(fields[f]->size()) + 1;
            }
            record.initial_cp932 = std::string(data.begin() + start + 0x4C, data.begin() + start + 0x4E);
            if (expected != end || cp932_length(record.initial_cp932) != 1 ||
                record.reading_cp932.substr(0, 2) != record.initial_cp932 ||
                cp932_length(record.written_cp932) != (record.words[4] & 0xFFFFFF))
                throw std::runtime_error("DIC linked length/initial mismatch");
            records_.push_back(std::move(record)); previous = end;
        }
        error = {}; return true;
    } catch (const std::exception& ex) { error.reason = ex.what(); source_ = {}; records_.clear(); return false; }
}

Value Value::integer(int32_t integer) {
    Value value; value.kind = Kind::Scalar; value.word = 0x20000000 | (static_cast<uint32_t>(integer) & 0x0FFFFFFF); return value;
}
Value Value::string(std::string raw) {
    if (raw.find('\0') != std::string::npos) throw std::runtime_error("CP932 value must exclude the C-string terminator");
    Value value; value.kind = Kind::String; value.cp932 = std::move(raw); return value;
}
int32_t Value::signed_integer() const {
    if (kind != Kind::Scalar || (word >> 28) != 2) throw std::runtime_error("Expected tagged integer");
    uint32_t payload = word & 0x0FFFFFFF;
    return static_cast<int32_t>(payload & 0x08000000 ? payload | 0xF0000000 : payload);
}
Value Vm::decode(uint32_t word) const {
    Value result; result.word = word;
    switch (word >> 28) {
    case 0: case 1: case 2: result.kind = Value::Kind::Scalar; break;
    case 5: result.kind = Value::Kind::String; result.cp932 = program_.pool_bytes(word & 0x0FFFFFFF); break;
    case 7: result.kind = Value::Kind::Code; break;
    default: result.kind = Value::Kind::Opaque; break;
    }
    return result;
}
std::vector<Value>& Vm::slots(Table table) {
    switch (table) {
    case Table::Frame: return frame_;
    case Table::Local: return local_;
    case Table::Global: return global_;
    default: return ctx28_;
    }
}
Value Vm::read_slot(Table table, uint16_t index) const {
    const auto& values = table == Table::Frame ? frame_ : table == Table::Local ? local_ : table == Table::Global ? global_ : ctx28_;
    if (index >= values.size()) throw std::runtime_error("Runtime slot index out of bounds");
    return values[index];
}
Event Vm::stop(const Command* cmd, const std::string& reason) {
    stopped_ = true;
    last_error_ = {reason, pc_, pc_ + 28, cmd ? cmd->opcode : -1};
    Event event; event.pc = event.next_pc = pc_; event.file_offset = pc_ + 28;
    event.opcode = last_error_.opcode; event.diagnostic = last_error_; return event;
}
Event Vm::start(const std::vector<Value>& arguments) { return start_at(program_.header().entrypoint, arguments); }
Event Vm::start_at(uint32_t code_pc, const std::vector<Value>& arguments) {
    pc_ = code_pc; display_state_ = 0; display_count_ = 0; frame_reserved_ = false; stopped_ = false;
    stack_.clear(); frame_ = arguments; local_.clear(); global_.clear(); ctx28_.clear(); last_error_ = {};
    const auto* cmd = program_.command(pc_);
    if (!program_.loaded() || !cmd || cmd->opcode != 0x30) return stop(cmd, "Start must address a verified original frame prologue");
    try {
        for (Table table : {Table::Local, Table::Global, Table::Ctx28}) {
            uint32_t count = table == Table::Local ? program_.header().local_count : table == Table::Global ? program_.header().global_count : program_.header().ctx28_count;
            auto& values = slots(table); values.reserve(count);
            for (uint32_t i = 0; i < count; ++i) values.push_back(decode(program_.initial_word(table, static_cast<uint16_t>(i))));
        }
    } catch (const std::exception& ex) { return stop(cmd, ex.what()); }
    Event event; event.status = Status::Running; event.pc = event.next_pc = pc_; event.file_offset = pc_ + 28; event.opcode = cmd->opcode; return event;
}
std::string Vm::expand(const Command& cmd) const {
    std::string result;
    const auto& bytes = program_.bytes();
    uint32_t pos = 29 + cmd.pc, end = 28 + cmd.pc + cmd.size;
    while (pos < end) {
        uint8_t byte = bytes[pos++];
        if (!byte) return result;
        if (!inline_control(byte)) { result.push_back(static_cast<char>(byte)); continue; }
        if (byte == 0x18 || byte == 0x19 || byte == 0x1A || byte == 0x1C)
            throw std::runtime_error("Unsupported indexed formatting control");
        uint16_t index = static_cast<uint16_t>(uint_at(bytes, pos, 2, end)); pos += 2;
        Table table = byte == 0x14 ? Table::Frame : byte == 0x15 ? Table::Local : byte == 0x17 ? Table::Global : Table::Ctx28;
        Value value = read_slot(table, index);
        if (value.kind == Value::Kind::String) result += value.cp932;
        else if (value.kind == Value::Kind::Scalar && (value.word >> 28) == 2) result += std::to_string(value.signed_integer());
        else throw std::runtime_error("Unresolved inline substitution value/type");
        if (result.size() > 1048576) throw std::runtime_error("Expanded message budget exceeded");
    }
    throw std::runtime_error("Missing inline terminator");
}
Event Vm::step() {
    const Command* cmd = program_.command(pc_);
    if (stopped_) { Event event; event.pc = event.next_pc = pc_; event.file_offset = pc_ + 28; event.opcode = last_error_.opcode; event.diagnostic = last_error_; return event; }
    if (!cmd || cmd->opcode < 0) return stop(cmd, "PC is not an executable command boundary");
    Event event; event.status = Status::Running; event.pc = pc_; event.file_offset = pc_ + 28; event.opcode = cmd->opcode;
    uint32_t next = pc_ + cmd->size;
    try {
        int op = cmd->opcode;
        if (op == 0x30) {
            if (frame_reserved_) return stop(cmd, "Nested frame ABI unsupported");
            uint32_t count = program_.operand(*cmd, 1, 1);
            if (frame_.size() > count) return stop(cmd, "Fixture argument count exceeds reserved frame");
            frame_.resize(count); frame_reserved_ = true;
        } else if (op >= 0x80 && op <= 0x83) {
            uint8_t width = static_cast<uint8_t>(op - 0x7F);
            uint32_t raw = program_.operand(*cmd, 1, width);
            // BOOT 70BB0/70C18 sign-extend 80/81; 70C8C uses three LBU for unsigned 82.
            if (width < 3 && (raw & (1u << (width * 8 - 1)))) raw |= UINT32_MAX << (width * 8);
            stack_.push_back(Value::integer(static_cast<int32_t>(raw)));
        } else if (op == 0x85) stack_.push_back(Value::integer(0));
        else if (op == 0x88) stack_.push_back(Value::string(program_.pool_bytes(program_.operand(*cmd, 1, 3))));
        else if (op == 0x8A) {
            Value value; value.kind = Value::Kind::Code; value.word = 0x70000000 | program_.operand(*cmd, 1, 3); stack_.push_back(value);
        } else if ((op >= 0x90 && op <= 0x95) || op == 0x98 || op == 0x99) {
            uint16_t index = static_cast<uint16_t>(program_.operand(*cmd, 1, 2));
            Table table = op <= 0x91 ? Table::Frame : op <= 0x93 ? Table::Local : op <= 0x95 ? Table::Global : Table::Ctx28;
            Value value;
            if (op & 1) {
                if (index >= slots(table).size()) return stop(cmd, "Reference slot out of bounds");
                value.kind = Value::Kind::Reference; value.table = table; value.index = index;
            } else {
                value = read_slot(table, index);
                if (!usable(value)) return stop(cmd, "Uninitialized/opaque runtime slot; caller argument or service required");
            }
            stack_.push_back(std::move(value));
        } else if (op == 0x51) {
            if (stack_.size() < 2) return stop(cmd, "Assignment stack underflow");
            Value value = stack_.back(), ref = stack_[stack_.size() - 2];
            if (ref.kind != Value::Kind::Reference || !usable(value)) return stop(cmd, "Unsupported assignment reference/value");
            auto& target = slots(ref.table);
            if (ref.index >= target.size()) return stop(cmd, "Assignment slot out of bounds");
            target[ref.index] = value; stack_.resize(stack_.size() - 2);
        } else if (op == 0x10 || op == 0x11 || op == 0x12) {
            uint32_t target = program_.operand(*cmd, 1, 3);
            const Command* destination = program_.command(target);
            if (!destination || destination->opcode < 0) return stop(cmd, "Branch target is metadata or not a command");
            bool take = true;
            if (op != 0x10) {
                if (stack_.empty()) return stop(cmd, "Conditional branch stack underflow");
                const Value& condition = stack_.back();
                if (condition.kind != Value::Kind::Scalar) return stop(cmd, "Conditional payload for non-scalar unsupported");
                bool nonzero = (condition.word & 0x0FFFFFFF) != 0;
                take = op == 0x11 ? !nonzero : nonzero;
                stack_.pop_back();
            }
            event.has_branch_decision = true; event.branch_taken = take;
            if (take) next = target;
        } else if (op == 0x48 || op == 0x8E) {
            std::string text = expand(*cmd);
            if (op == 0x8E) stack_.push_back(Value::string(std::move(text)));
            else {
                display_state_ = 6; ++display_count_;
                event.status = Status::Message; event.message_cp932 = std::move(text);
                event.display_state = display_state_; event.display_count = display_count_;
            }
        } else {
            std::string reason = op == 0x8B ? "Unsupported builtin/host service; id=" + std::to_string(program_.operand(*cmd, 1, 1)) + ", argc=" + std::to_string(program_.operand(*cmd, 2, 1)) :
                op == 0x20 || op == 0x21 ? "Call/frame return ABI not implemented" :
                op == 0x31 ? "Return/frame unwind ABI not implemented" :
                op == 0x44 ? "Delay scheduler not implemented (opcode 44 is not stack discard)" : "Unsupported opcode semantics";
            return stop(cmd, reason);
        }
        if (stack_.size() > 32767) return stop(cmd, "Native operand stack budget exceeded");
        pc_ = next; event.next_pc = pc_; return event;
    } catch (const std::exception& ex) { return stop(cmd, ex.what()); }
}
Event Vm::run(uint32_t instruction_budget) {
    for (uint32_t i = 0; i < instruction_budget; ++i) {
        Event event = step();
        if (event.status != Status::Running) return event;
    }
    return stop(program_.command(pc_), "Instruction budget exceeded");
}
} // namespace dokodemo::script
