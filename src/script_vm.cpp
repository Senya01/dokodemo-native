#include "script_vm.h"
#include "core_hash.h"
#include "core/cp932.h"
#include "recovered/game_primitives.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <mutex>
#include <thread>

namespace dokodemo::script {
struct NativeScriptContext {
    Vm* vm=nullptr;
    uint16_t slot=0;
    std::thread::id thread;
};
namespace {
std::mutex context_mutex;
std::array<std::weak_ptr<NativeScriptContext>,8> context_registry;
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
bool Program::load_test_fixture(std::vector<uint8_t> bytes, Diagnostic& error) {
    *this=Program{};error={};
    try {
        source_={"ExplicitSyntheticNativeVmFixture",hash_bytes(bytes)};
        bytes_=std::move(bytes);parse();return true;
    } catch(const std::exception& e) {*this=Program{};error.reason=e.what();return false;}
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

VmStateWitness Vm::current_witness() const noexcept {
    VmStateWitness w;
    try {
        w.run_id=run_id_;w.frame_id=frame_id_;w.executed=executed_;
        w.current_pc=pc_;w.code_entry=code_entry_;w.stopped=stopped_;
        w.clock_tick=clock_tick_;w.deadline_tick=wait_until_;
        const auto delta=uint32_t(wait_until_-clock_tick_)&0x7FFFFFu;
        w.relative_deadline23=int32_t(delta&0x400000u?delta|0xFF800000u:delta);
        w.context_registered=bool(context_);w.context_word=context_?0x80000000u|context_->slot:0;
        w.event_envelope_present=bool(event_envelope_);w.event_scope_depth=event_depth_;
        w.call_depth=uint32_t(calls_.size());w.active_blocks=uint32_t(blocks_.size());w.queued_callbacks=uint32_t(pending_callbacks_.size());
        const auto& identity=program_.source().sha256;
        std::copy_n(identity.begin(),std::min<size_t>(64,identity.size()),w.code_sha256.begin());
        if(!calls_.empty()){
            const auto& c=calls_.back();w.caller_entry=c.code_entry;w.caller_call_pc=c.call_pc;
            w.caller_return_pc=c.return_pc;w.caller_frame_id=c.frame_id;w.caller_stack_base=c.stack_base;
        }
        const auto value=[](const Value& v){
            ValueWitness x;x.kind=v.kind;x.word=v.word;x.table=uint32_t(v.table);x.index=v.index;
            if(v.kind==Value::Kind::String){x.cp932_bytes=v.cp932.size();x.cp932_sha256=core::sha256(reinterpret_cast<const uint8_t*>(v.cp932.data()),v.cp932.size());}
            return x;
        };
        w.result=value(result_);w.frame_cells=frame_?frame_->size():0;w.operand_cells=stack_.size();
        w.frame_captured=uint32_t(std::min<size_t>(16,size_t(w.frame_cells)));
        w.operands_captured=uint32_t(std::min<size_t>(16,stack_.size()));
        for(uint32_t i=0;i<w.frame_captured;++i)w.frame_values[i]=value((*frame_)[i]);
        for(uint32_t i=0;i<w.operands_captured;++i)w.operands[i]=value(stack_[i]);
        w.valid=true;
    }catch(...){w.valid=false;} // evidence failure must never change VM semantics
    return w;
}
Vm::~Vm(){if(context_)context_->vm=nullptr;}
Value Vm::context_value(){
    if(!context_){
        std::lock_guard<std::mutex> lock(context_mutex);
        for(uint16_t i=0;i<context_registry.size();++i)if(context_registry[i].expired()){
            context_=std::make_shared<NativeScriptContext>();context_->vm=this;context_->slot=i;
            context_->thread=std::this_thread::get_id();context_registry[i]=context_;break;
        }
        if(!context_)throw std::runtime_error("Native KSC eight-context registry exhausted");
    }
    Value value;value.kind=Value::Kind::Context;value.word=0x80000000u|context_->slot;value.context_owner=context_;return value;
}
int32_t Vm::post_callback(const Value& target,const Value& code,const Value& first,const Value& second){
    if(target.kind!=Value::Kind::Context||target.word>>28!=8)throw std::runtime_error("Callback27 needs typed live context token");
    const auto owner=target.context_owner.lock();
    if(!owner||owner->thread!=std::this_thread::get_id()||!owner->vm)return -1;
    if((target.word&0x0FFFFFFF)!=owner->slot)throw std::runtime_error("Callback context token/registry identity mismatch");
    auto& vm=*owner->vm;
    if(vm.stopped_)return -1;
    const auto* command=vm.program_.command(code.word&0x0FFFFFFF);
    if(code.kind!=Value::Kind::Code||code.word>>28!=7||!command||command->opcode!=0x30)
        throw std::runtime_error("Callback27 target must be admitted frame prologue");
    for(const auto* arg:{&first,&second}){
        if(arg->kind==Value::Kind::Unknown)throw std::runtime_error("Uninitialized callback argument");
        if(arg->kind==Value::Kind::Reference){
            if(&vm!=this&&arg->table!=Table::Frame)throw std::runtime_error("Cross-context stable-table callback references not implemented");
            (void)peek_reference(*arg);
        }
    }
    PendingCallback callback{code.word&0x0FFFFFFF,first,second,context_value()};
    callback.receipt=vm.next_callback_receipt_+1;
    if(vm.event_depth_||vm.event_envelope_){
        if(vm.pending_callbacks_.size()==64)return 1; // checked native queue bound
        vm.pending_callbacks_.push_back(std::move(callback));
    }else vm.enter_callback(std::move(callback));
    ++vm.next_callback_receipt_;++vm.callbacks_posted_;return 0;
}
int32_t Vm::post_owner_event(const Value& code,int32_t event_id,int32_t argument){
    if(!context_||context_->vm!=this||stopped_)return -1;
    //6B4B8 has a real no-callback return1;it is not successful invocation.
    if(code.word>>28!=7)return 1;
    const auto* command=program_.command(code.word&0x0FFFFFFF);
    if(code.kind!=Value::Kind::Code||!command||command->opcode!=0x30)
        throw std::runtime_error("Owner event target needs admitted typed code/frame prologue");
    PendingCallback callback{code.word&0x0FFFFFFF,Value::integer(event_id),Value::integer(argument),{},false};
    callback.receipt=next_callback_receipt_+1;
    if(event_depth_||event_envelope_){
        if(pending_callbacks_.size()==64)return 1;
        pending_callbacks_.push_back(std::move(callback));
    }else enter_callback(std::move(callback));
    ++next_callback_receipt_;++callbacks_posted_;return 0;
}
void Vm::enter_callback(PendingCallback callback){
    if(event_envelope_)throw std::runtime_error("Nested immediate native event envelope");
    for(const auto* value:{&callback.first,&callback.second})
        if(value->kind==Value::Kind::Reference&&value->table==Table::Frame&&value->frame_owner.expired())
            throw std::runtime_error("Queued callback frame reference expired before dispatch");
    auto saved=std::make_unique<EventEnvelope>();saved->receipt=callback.receipt;saved->pc=pc_;saved->reserved=frame_reserved_;saved->frame_id=frame_id_;saved->code_entry=code_entry_;
    saved->enabled=context_enabled_;saved->waiting=waiting_event_;saved->signal=event_signal_;
    const auto delta=uint32_t(wait_until_-clock_tick_)&0x7FFFFF;
    saved->deadline_delta=int32_t(delta&0x400000?delta|0xFF800000:delta);
    saved->result=std::move(result_);saved->stack=std::move(stack_);saved->frame=std::move(frame_);
    // NativeFrameScopedEventEnvelope: suspend the caller's owned blocks with
    // its frames. The callback has its own native block scope. This preserves
    // the caller;it does not claim original ctx62 was zero or reconstruct the
    // unresolved nonzero62/pending-tail/67548 path of PSP1C.
    saved->calls=std::move(calls_);saved->blocks=std::move(blocks_);event_envelope_=std::move(saved);
    stack_.clear();calls_.clear();blocks_.clear();result_={};
    frame_=std::make_shared<std::vector<Value>>(std::initializer_list<Value>{callback.first,callback.second});
    frame_id_=++next_frame_id_;code_entry_=callback.pc;
    if(callback.include_caller)frame_->push_back(std::move(callback.caller));
    frame_reserved_=false;pc_=callback.pc;context_enabled_=true;event_override_=false;waiting_event_=false;wait_until_=0;
}
void Vm::drain_callback(){
    if(event_depth_||event_envelope_||pending_callbacks_.empty())return;
    auto callback=std::move(pending_callbacks_.front());pending_callbacks_.pop_front();enter_callback(std::move(callback));
}
void Vm::resume_modal_context(){
    if(!context_||context_->vm!=this||stopped_||event_envelope_||!pending_callbacks_.empty())
        throw std::runtime_error("Modal resume needs live context and returned callback");
    context_enabled_=true;
}
Value Value::integer(int32_t integer) {
    Value value; value.kind = Kind::Scalar; value.word = 0x20000000 | (static_cast<uint32_t>(integer) & 0x0FFFFFFF); return value;
}
Value Value::string(std::string raw) {
    if (raw.find('\0') != std::string::npos) throw std::runtime_error("CP932 value must exclude the C-string terminator");
    Value value; value.kind = Kind::String; value.word=0x60000000; value.cp932 = std::move(raw); return value;
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
    case 3: result.kind = Value::Kind::Timestamp; break;
    case 9: result.kind = Value::Kind::ArrayHeader; break;
    case 11: {
        result.kind = Value::Kind::Reference;
        const auto selector = (word & 0x0FFFFFFF) >> 16;
        result.table = selector == 1 ? Table::Frame : selector == 2 ? Table::Local :
                       selector == 3 ? Table::Ctx28 : Table::Global;
        result.index = uint16_t(word);
        if (result.table == Table::Frame) result.frame_owner = frame_;
        break;
    }
    default: result.kind = Value::Kind::Opaque; break;
    }
    return result;
}
std::vector<Value>& Vm::slots(Table table) {
    switch (table) {
    case Table::Frame:
        if (!frame_) throw std::runtime_error("No live native script frame");
        return *frame_;
    case Table::Local: return local_;
    case Table::Global: return global_;
    default: return ctx28_;
    }
}
Value Vm::read_slot(Table table, uint16_t index) const {
    if(table==Table::InputResult){
        if(!input_result_||index>=input_result_->size())throw std::runtime_error("Input result cell unavailable");
        return (*input_result_)[index];
    }
    if (table == Table::Frame && !frame_) throw std::runtime_error("No live native script frame");
    const auto& values = table == Table::Frame ? *frame_ : table == Table::Local ? local_ : table == Table::Global ? global_ : ctx28_;
    if (index >= values.size()) throw std::runtime_error("Runtime slot index out of bounds");
    return values[index];
}
Value Vm::load_reference(const Value& ref) const {
    auto value=peek_reference(ref);if(access_)access_(ref,false);return value;
}
Value Vm::peek_reference(const Value& ref) const {
    if (ref.kind != Value::Kind::Reference) throw std::runtime_error("Expected typed script reference");
    if (ref.table!=Table::Frame&&ref.table!=Table::InputResult) return read_slot(ref.table,ref.index);
    const auto owner=ref.frame_owner.lock();
    if(!owner)throw std::runtime_error("Retired native script frame reference");
    if (ref.index >= owner->size()) throw std::runtime_error("Frame reference out of bounds");
    return (*owner)[ref.index];
}
void Vm::store_reference(const Value& ref, Value value) {
    if (ref.kind != Value::Kind::Reference) throw std::runtime_error("Expected typed script destination");
    const auto owner=ref.frame_owner.lock();
    const bool weak_owner=ref.table==Table::Frame||ref.table==Table::InputResult;
    if(weak_owner&&!owner)throw std::runtime_error("Retired native script destination frame");
    auto& values = weak_owner ? *owner : slots(ref.table);
    if (ref.index >= values.size()) throw std::runtime_error("Script destination out of bounds");
    values[ref.index] = std::move(value);
    if(access_)access_(ref,true);
}
Value Vm::indexed_reference(const Value& reference, uint32_t index) const {
    const auto header = load_reference(reference);
    if (header.kind != Value::Kind::ArrayHeader || index >= (header.word & 0xFFF))
        throw std::runtime_error("Script array index/header out of bounds");
    const uint64_t at = uint64_t(reference.index) + 1 + index;
    if (at > UINT16_MAX) throw std::runtime_error("Script array reference exceeds index domain");
    Value out = reference; out.index = uint16_t(at);
    (void)load_reference(out); return out;
}
Event Vm::stop(const Command* cmd, const std::string& reason) {
    stopped_ = true;
    last_error_ = {reason, pc_, pc_ + 28, cmd ? cmd->opcode : -1};
    Event event; event.pc = event.next_pc = pc_; event.file_offset = pc_ + 28;
    event.opcode = last_error_.opcode; event.diagnostic = last_error_; return event;
}
Event Vm::start(const std::vector<Value>& arguments) { return start_at(program_.header().entrypoint, arguments); }
Event Vm::start_at(uint32_t code_pc, const std::vector<Value>& arguments) {
    choice_arguments_.clear();choice_waiting_=choice_arguments_taken_=false;choice_count_=0;
    input_arguments_.clear();input_waiting_=false;
    input_result_=std::make_shared<std::vector<Value>>(3);
    b4_calls_=b4_operand_word_=b4_result_word_=0;
    e2_calls_=e2_left_word_=e2_right_word_=e2_result_word_=0;
    d0_calls_=d0_left_word_=d0_right_word_=d0_result_word_=0;
    d5_calls_=d5_first_pc_=d5_first_left_word_=d5_first_right_word_=d5_first_result_word_=0;
    op55_calls_=op55_first_pc_=op55_first_before_=op55_first_after_=op55_first_table_=op55_first_index_=0;
    ctx_reference_branches_=ctx_reference_first_pc_=ctx_reference_first_index_=0;
    pc_ = code_pc; display_state_ = 0; display_count_ = 0; frame_reserved_ = false; stopped_ = false;
    stack_.clear(); frame_ = std::make_shared<std::vector<Value>>(arguments);
    frame_id_=++next_frame_id_;code_entry_=code_pc;++run_id_;normal_returns_={};
    b3_calls_=b3_pc_=b3_operand_kind_=b3_operand_word_=b3_result_word_=0;
    calls_.clear(); result_ = {}; executed_ = 0;
    blocks_.clear(); blocks_entered_ = blocks_closed_ = switches_ = 0;
    opcode_cost_ = 1;
    event_depth_=0;event_signal_=true;waiting_event_=false;
    pending_callbacks_.clear();event_envelope_.reset();context_enabled_=true;event_override_=false;
    callbacks_posted_=callbacks_returned_=0;
    timestamp_writes_=deadline_writes_=0;
    random_selections_=0;last_random_={};root_returns_=0;
    timer_base_ = clock_tick_; wait_until_ = 0;
    local_.clear(); global_.clear(); ctx28_.clear(); last_error_ = {};
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
bool Vm::owns_active_frame(uint64_t id,uint32_t entry)const noexcept{
    if(stopped_||event_envelope_||!context_||context_->vm!=this||context_->thread!=std::this_thread::get_id())return false;
    if(frame_id_==id&&code_entry_==entry)return true;
    for(const auto& f:calls_)if(f.frame_id==id&&f.code_entry==entry)return true;
    return false;
}
bool Vm::active_call_matches(uint64_t caller,uint32_t entry,uint32_t pc,uint32_t resume,
                            uint64_t callee,uint32_t callee_entry)const noexcept{
    if(!owns_active_frame(caller,entry))return false;
    for(size_t i=0;i<calls_.size();++i){
        const auto& f=calls_[i];
        const auto next_id=i+1<calls_.size()?calls_[i+1].frame_id:frame_id_;
        const auto next_entry=i+1<calls_.size()?calls_[i+1].code_entry:code_entry_;
        if(f.frame_id==caller&&f.code_entry==entry&&f.call_pc==pc&&f.return_pc==resume
           &&next_id==callee&&next_entry==callee_entry)return true;
    }
    return false;
}
uint64_t Vm::unique_active_frame(uint32_t entry)const noexcept{
    if(stopped_||event_envelope_||!context_||context_->vm!=this||context_->thread!=std::this_thread::get_id())return 0;
    uint64_t id=code_entry_==entry?frame_id_:0;
    for(const auto& f:calls_)if(f.code_entry==entry){if(id)return 0;id=f.frame_id;}
    return id;
}
Event Vm::step() {
    const Command* cmd = program_.command(pc_);
    if (stopped_) { Event event; event.pc = event.next_pc = pc_; event.file_offset = pc_ + 28; event.opcode = last_error_.opcode; event.diagnostic = last_error_; return event; }
    if (!cmd || cmd->opcode < 0) return stop(cmd, "PC is not an executable command boundary");
    if(!context_enabled_||choice_waiting_||input_waiting_){Event paused;paused.status=Status::Waiting;paused.pc=paused.next_pc=pc_;return paused;}
    if(waiting_event_){Event waiting;waiting.status=Status::Waiting;waiting.pc=waiting.next_pc=pc_;return waiting;}
    if (clock_tick_ < wait_until_) { Event waiting;waiting.status=Status::Yield;waiting.pc=waiting.next_pc=pc_;return waiting; }
    Event event; event.status = Status::Running; event.pc = pc_; event.file_offset = pc_ + 28; event.opcode = cmd->opcode;
    uint32_t next = pc_ + cmd->size;
    size_t captured_normal_return=normal_returns_.size();
    const bool observe_frame=bool(frame_observer_)&&((cmd->opcode>=0x20&&cmd->opcode<=0x23)
        ||cmd->opcode==0x31||cmd->opcode==0x32||cmd->opcode==0x1C);
    FrameTransition transition;
    if(observe_frame){transition.opcode=uint8_t(cmd->opcode);transition.before=current_witness();
        transition.callback_receipt=event_envelope_?event_envelope_->receipt:0;}
    try {
        int op = cmd->opcode;
        const auto pop = [&]() {
            if(stack_.empty())throw std::runtime_error("Script operand stack underflow");
            auto value=std::move(stack_.back());stack_.pop_back();return value;
        };
        const auto boolean = [](bool truth) { Value v;v.kind=Value::Kind::Scalar;v.word=0x10000000u|uint32_t(truth);return v; };
        const auto numeric = [](const Value& v) {
            if(v.kind==Value::Kind::ArrayHeader)return int32_t(v.word&0xFFF);
            if((v.kind!=Value::Kind::Scalar&&v.kind!=Value::Kind::Timestamp)||v.word>>28>3)throw std::runtime_error("Script numeric conversion outside scalar domain");
            const auto p=v.word&0x0FFFFFFF;return int32_t(p&0x08000000?p|0xF0000000:p);
        };
        if (op == 0x03) { // common6B9B8 instruction-cost/no-op path
        } else if(op==0x0B) {
            // 6BA04 saves ctx58 on the value stack and makes subsequent
            // source opcodes zero-cost. Native hard quotas remain separate.
            stack_.push_back(Value::integer(opcode_cost_));opcode_cost_=0;
        } else if(op==0x0C) {
            // 6BA54 restores the saved counter;not the result register.
            opcode_cost_=numeric(pop());
        } else if(op==0x0D) {
            // 6BB58 increments source56,deferring queued callbacks.
            if(event_depth_==UINT16_MAX)throw std::runtime_error("Event scope overflow");
            ++event_depth_;
        } else if(op==0x0E) {
            //6BB68 ->6B33C. Drain after publishing this instruction's PC so
            //the event envelope saves the actual following continuation.
            if(event_depth_)--event_depth_;
        } else if(op==0x3A){
            //6E43C: event return overrides restored enable38 with byte39=0.
            if(!event_envelope_)return stop(cmd,"3A requires the reached event envelope");
            event_override_=true;
        } else if(op==0x32||op==0x1C){
            //In the named native frame-scoped event ABI,use the6D95C saved
            //event return for1C/32. PSP6D944's nonzero62 path remains open;
            //callback-local active blocks are refused,not silently unwound.
            if(!event_envelope_||!calls_.empty()||!blocks_.empty())
                return stop(cmd,"32 needs finite callback frame without abnormal active unwind");
            auto saved=std::move(event_envelope_);
            const bool enabled=event_override_?false:saved->enabled;
            stack_=std::move(saved->stack);frame_=std::move(saved->frame);calls_=std::move(saved->calls);blocks_=std::move(saved->blocks);
            frame_id_=saved->frame_id;code_entry_=saved->code_entry;
            result_=std::move(saved->result);frame_reserved_=saved->reserved;waiting_event_=saved->waiting;event_signal_=saved->signal;
            wait_until_=saved->deadline_delta>0?clock_tick_+uint32_t(saved->deadline_delta):0;
            context_enabled_=enabled;event_override_=false;
            next=saved->pc;++callbacks_returned_;if(!enabled)event.status=Status::Yield;
        } else if (op == 0x30) {
            if (frame_reserved_) return stop(cmd, "Nested frame ABI unsupported");
            uint32_t count = program_.operand(*cmd, 1, 1);
            if (frame_->size() > count) return stop(cmd, "Caller argument count exceeds reserved frame");
            const auto arguments=frame_->size();frame_->resize(count);
            if(native_reserved_local_nil_){
                // NativeOwnedReservedLocalsNil: source30 reserves cells but
                // does not initialize them in its body. KSC85D7 uses local4
                // as an optional accumulator and can return it untouched.
                // Original persistent-stack clear history remains unclosed.
                for(size_t i=arguments;i<count;++i)(*frame_)[i].kind=Value::Kind::Scalar;
            }
            frame_reserved_ = true;
        } else if(op==0x20||op==0x21||op==0x22||op==0x23) {
            const auto argc=(op==0x20||op==0x22)?program_.operand(*cmd,1,1):0;
            uint32_t target=0;
            if(op==0x22){
                //6DD38/674C0: target is BELOW argc operands. The typed call
                //record owns the source return slot/gap;arguments keep their
                //order and earlier caller operands survive the return.
                if(argc>=stack_.size())return stop(cmd,"Indirect22 missing target/arguments");
                const auto index=stack_.size()-argc-1;const auto function=stack_[index];
                if(function.kind!=Value::Kind::Code||function.word>>28!=7)return stop(cmd,"Indirect22 requires actual tag7 callee");
                target=function.word&0x0FFFFFFF;stack_.erase(stack_.begin()+index);
            }else if(op==0x23){
                //6DE00 moves/pops one tag7 callee and replaces it with return
                //PC. Native call records own that returnPC. No674C0 argument
                //relocation occurs:existing caller operands stay below the
                //new frame and must survive this zero-relocation call.
                const auto function=pop();
                if(function.kind!=Value::Kind::Code||function.word>>28!=7)
                    return stop(cmd,"Indirect23 needs actual tag7 callee");
                target=function.word&0x0FFFFFFF;
            }else target=program_.operand(*cmd,op==0x20?2:1,3);
            const auto* callee=program_.command(target);
            if(!callee||callee->opcode!=0x30||argc>stack_.size()||calls_.size()>=128)
                return stop(cmd,"Invalid script call/frame/argument bound");
            const auto begin=stack_.size()-argc;
            auto arguments=std::make_shared<std::vector<Value>>(stack_.begin()+begin,stack_.end());
            stack_.resize(begin);calls_.push_back({next,begin,frame_reserved_,frame_,frame_id_,code_entry_,cmd->pc});
            frame_=std::move(arguments);frame_id_=++next_frame_id_;code_entry_=target;frame_reserved_=false;next=target;
        } else if(op==0x18) {
            // 6C578 saves the previous block context, target and pending
            // control cells,then CONTINUES. The target is not a branch here.
            const auto target=program_.operand(*cmd,1,3);
            const auto* close=program_.command(target);
            if(!close||close->opcode<0||blocks_.size()>=128)
                return stop(cmd,"Invalid structured control target/depth");
            blocks_.push_back({target,stack_.size(),calls_.size(),frame_});
            ++blocks_entered_;
        } else if(op==0x1A) {
            // 6CA08 releases the current administrative block and restores
            // its enclosing context. Refuse malformed operand imbalance.
            if(blocks_.empty()||blocks_.back().frame.lock()!=frame_||
               blocks_.back().call_depth!=calls_.size()||blocks_.back().operand_base!=stack_.size())
                return stop(cmd,"Unbalanced or foreign structured control close");
            blocks_.pop_back();++blocks_closed_;
        } else if(op==0x13) {
            // 6BCD8: pop/coerce selector; traverse raw tagged labels and u24
            // NEXT HEADER links. Retain the selected raw word in result30.
            const auto selector=numeric(pop());
            uint32_t at=cmd->pc+1;
            std::unordered_set<uint32_t> seen;
            for(;;) {
                if(!seen.insert(at).second)return stop(cmd,"Cyclic runtime switch links");
                if(at!=cmd->pc+1) {
                    const auto* metadata=program_.command(at);
                    if(!metadata||metadata->opcode!=-1)return stop(cmd,"Switch link misses metadata header");
                }
                const auto word=uint_at(program_.bytes(),28+at,4,program_.header().pool_start());
                result_=decode(word);
                if(!(word>>28)){next=at+4;event.branch_taken=false;break;}
                const auto link=uint_at(program_.bytes(),28+at+4,3,program_.header().pool_start());
                const auto payload=word&0x0FFFFFFF;
                const auto label=int32_t(payload&0x08000000?payload|0xF0000000:payload);
                if(label==selector){next=at+7;event.branch_taken=true;break;}
                at=link;
            }
            const auto* body=program_.command(next);
            if(!body||body->opcode<0)return stop(cmd,"Selected switch body is not executable");
            event.has_branch_decision=true;++switches_;
        } else if(op==0x31) {
            // Retained67548's abnormal active-block cleanup differs from
            // normal1A (five versus six cells). Refuse that unresolved path;
            // do not silently invent unwind. Caller-owned blocks survive.
            if(!blocks_.empty()&&blocks_.back().frame.lock()==frame_)
                return stop(cmd,"Return through active block: source67548 unwind unresolved");
            if(calls_.empty()) { stopped_=true;event.status=Status::Complete; }
            else {
                for(size_t i=0;i<normal_returns_.size();++i)if(cmd->pc==return_probe_pcs_[i]||cmd->pc==return_probe_alternate_pcs_[i]){
                    auto& trace=normal_returns_[i];trace={};trace.sequence=++return_sequence_;
                    trace.opcode_pc=cmd->pc;trace.before=current_witness();captured_normal_return=i;break;
                }
                if(calls_.size()==1)++root_returns_;
                auto saved=std::move(calls_.back());calls_.pop_back();
                stack_.resize(saved.stack_base);frame_=std::move(saved.values);
                frame_id_=saved.frame_id;code_entry_=saved.code_entry;
                frame_reserved_=saved.reserved;next=saved.return_pc;
            }
        } else if(op==0x38) { stopped_=true;event.status=Status::Complete;
        } else if(op==0x4E) {
            const auto index=program_.operand(*cmd,1,2),count=program_.operand(*cmd,3,2);
            if(index>=frame_->size()||uint64_t(index)+count+1>frame_->size())return stop(cmd,"Script frame array extent");
            Value value;value.kind=Value::Kind::ArrayHeader;value.word=0x90000000u|count;(*frame_)[index]=value;
        } else if (op >= 0x80 && op <= 0x83) {
            uint8_t width = static_cast<uint8_t>(op - 0x7F);
            uint32_t raw = program_.operand(*cmd, 1, width);
            // BOOT 70BB0/70C18 sign-extend 80/81; 70C8C uses three LBU for unsigned 82.
            if (width < 3 && (raw & (1u << (width * 8 - 1)))) raw |= UINT32_MAX << (width * 8);
            stack_.push_back(Value::integer(static_cast<int32_t>(raw)));
        } else if (op == 0x8D) {
            //70FF8: elapsed source tick since ctx18;ordinary opcode cost.
            stack_.push_back(Value::integer(int32_t(uint32_t(clock_tick_-timer_base_) & 0x0FFFFFFF)));
        } else if (op == 0x84) { Value nil;nil.kind=Value::Kind::Scalar;stack_.push_back(nil); }
        else if (op == 0x85) stack_.push_back(Value::integer(0));
        else if (op == 0x86||op==0x87) stack_.push_back(boolean(op==0x87));
        else if(op==0x89) { stack_.push_back(result_);result_={};result_.kind=Value::Kind::Scalar; }
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
                if (table == Table::Frame) value.frame_owner = frame_;
                if (table == Table::Ctx28) value.context_owner = context_;
            } else {
                value = read_slot(table, index);
                if (!usable(value)) return stop(cmd, "Uninitialized/opaque runtime slot; table="+std::to_string(uint32_t(table))+"; index="+std::to_string(index)+"; frame_cells="+std::to_string(frame_->size())+"; caller_return="+std::to_string(calls_.empty()?0:calls_.back().return_pc)+"; caller argument or service required");
            }
            stack_.push_back(std::move(value));
        } else if (op == 0x50 || op == 0x51) {
            if (stack_.size() < 2) return stop(cmd, "Assignment stack underflow");
            Value value = stack_.back(), ref = stack_[stack_.size() - 2];
            if (ref.kind != Value::Kind::Reference || !usable(value)) return stop(cmd, "Unsupported assignment reference/value");
            store_reference(ref,value); stack_.resize(stack_.size() - 2);
            if(op==0x50)stack_.push_back(value);
        } else if(op==0x57) {
            if(stack_.size()<2)throw std::runtime_error("Array assignment needs two references");
            const auto lhs=stack_[stack_.size()-2],rhs=stack_.back();
            const auto left=load_reference(lhs);
            if(left.kind!=Value::Kind::ArrayHeader||(left.word>>28)!=9)
                throw std::runtime_error("Opcode57 left reference requires array header");
            const auto right=load_reference(rhs);
            if(right.kind!=Value::Kind::ArrayHeader||(right.word>>28)!=9)
                throw std::runtime_error("Opcode57 right reference requires array header");
            const uint32_t dst=left.word&0xFFF,src=right.word&0xFFF;
            for(uint32_t i=0;i<std::min(dst,src);++i){
                const auto value=load_reference(indexed_reference(rhs,i));
                if(value.kind==Value::Kind::Unknown)throw std::runtime_error("Opcode57 unknown source cell");
                store_reference(indexed_reference(lhs,i),value);
            }
            //Raw6F134..144 repeatedly releases the SAME first remaining
            //destination cell;it does not advance to clear the whole tail.
            if(dst>src){Value nil;nil.kind=Value::Kind::Scalar;
                store_reference(indexed_reference(lhs,src),nil);}
            stack_.resize(stack_.size()-2);
        } else if(op==0x14) {
            if(stack_.size()<2)throw std::runtime_error("Foreach needs two references");
            const auto destination=stack_[stack_.size()-2],source=stack_.back();
            store_reference(destination,load_reference(indexed_reference(source,0)));
            stack_.push_back(Value::integer(1));
        } else if(op==0x15) {
            if(stack_.size()<3)throw std::runtime_error("Foreach-next stack underflow");
            const auto source=stack_[stack_.size()-2];const auto index=numeric(stack_.back());
            const auto count=numeric(load_reference(source));
            if(index<count) {
                if(index<0)throw std::runtime_error("Negative foreach index");
                store_reference(stack_[stack_.size()-3],load_reference(indexed_reference(source,uint32_t(index))));
                stack_.back()=Value::integer(index+1);next=program_.operand(*cmd,1,3);
                if(!program_.command(next))throw std::runtime_error("Foreach target outside script");
            } else stack_.resize(stack_.size()-3);
        } else if(op==0x55) {
            //6EE3C ->769F4: consume reference into result30; no value push.
            //The checked native boundary refuses malformed/retired operands.
            if(stack_.empty()||stack_.back().kind!=Value::Kind::Reference)
                return stop(cmd,"Opcode55 needs a live typed reference");
            const auto ref=stack_.back();
            if(ref.table==Table::Ctx28&&(!context_||ref.context_owner.lock()!=context_||context_->vm!=this
                ||context_->thread!=std::this_thread::get_id()))
                return stop(cmd,"Opcode55 needs its live context-table owner");
            const auto old=load_reference(ref);
            if(!usable(old))return stop(cmd,"Opcode55 unknown/opaque referent");
            auto value=old;const auto tag=old.word>>28;
            const bool numeric_cell=(old.kind==Value::Kind::Scalar&&tag<=3)
                ||(old.kind==Value::Kind::Timestamp&&tag==3);
            if(numeric_cell&&tag==1)value.word=0x10000000u;
            else if(numeric_cell&&(tag==2||tag==3))
                value.word=(old.word&0xF0000000u)|((old.word-1u)&0x0FFFFFFFu);
            if(numeric_cell&&tag)store_reference(ref,value);
            stack_.pop_back();result_=ref;
            if(!op55_calls_){op55_first_pc_=cmd->pc;op55_first_before_=old.word;op55_first_after_=value.word;
                op55_first_table_=uint32_t(ref.table);op55_first_index_=ref.index;}
            ++op55_calls_;
        } else if(op==0x52||op==0x53) {
            const auto ref=pop(),old=load_reference(ref);result_=ref;
            if(op==0x52)stack_.push_back(old);
            if((old.kind==Value::Kind::Scalar||old.kind==Value::Kind::Timestamp)&&(old.word>>28>=1&&old.word>>28<=3)) {
                auto value=old;
                value.word=recovered::increment_value(old.word);store_reference(ref,value);
            }
        } else if(op==0xF0) { stack_.push_back(load_reference(pop()));
        } else if(op==0xF4||op==0xF5) {
            const auto index=numeric(pop());const auto array=pop();
            if(index<0)throw std::runtime_error("Negative script array index");
            const auto ref=indexed_reference(array,uint32_t(index));
            stack_.push_back(op==0xF5?ref:load_reference(ref));
        } else if(op==0xF6) {
            const auto input=pop();const auto count=numeric(load_reference(input));
            if(count<=0||!random_)throw std::runtime_error("F6 needs live nonempty list and real RNG owner");
            const auto mode=program_.operand(*cmd,1,1);
            Value history;uint32_t capacity=0,cursor=0;std::vector<uint32_t> candidates;
            const auto raw_key=[](const Value& value){
                const bool scalar=(value.kind==Value::Kind::Scalar||value.kind==Value::Kind::Timestamp)&&value.word>>28<=3;
                const bool code=value.kind==Value::Kind::Code&&value.word>>28==7;
                if(!scalar&&!code)throw std::runtime_error("F6 exclusion raw-key domain is finite scalar/timestamp/code");
                return value.word&0x0FFFFFFF;
            };
            if(mode){
                if(mode>4)throw std::runtime_error("F6 history selector outside finite table domain");
                history.kind=Value::Kind::Reference;history.table=mode==1?Table::Frame:mode==2?Table::Local:mode==3?Table::Global:Table::Ctx28;
                history.index=uint16_t(program_.operand(*cmd,2,2));if(mode==1)history.frame_owner=frame_;
                const auto header=load_reference(history);
                capacity=header.word&0x3FFF;cursor=(header.word>>14)&0x3FFF;
                if(header.kind!=Value::Kind::ArrayHeader||!capacity||capacity>4095||cursor>=capacity)
                    throw std::runtime_error("F6 history packed capacity/cursor malformed");
                std::unordered_set<uint32_t> prior;
                for(uint32_t i=0;i<capacity;++i)prior.insert(raw_key(load_reference(indexed_reference(history,i))));
                for(uint32_t i=0;i<uint32_t(count);++i)
                    if(!prior.count(raw_key(load_reference(indexed_reference(input,i)))))candidates.push_back(i);
            }
            //77404 excludes history values,keeps source candidate order and
            //falls back to the whole list when every value is excluded.
            const auto chosen=random_()%(candidates.empty()?uint32_t(count):uint32_t(candidates.size()));
            const auto index=candidates.empty()?chosen:candidates[chosen];
            last_random_=load_reference(indexed_reference(input,index));++random_selections_;
            if(mode){
                store_reference(indexed_reference(history,cursor),last_random_);
                Value header;header.kind=Value::Kind::ArrayHeader;
                header.word=0x90000000u|(((cursor+1)%capacity)<<14)|capacity;store_reference(history,header);
            }
            stack_.push_back(last_random_);
        } else if(op==0x70) { result_=pop();
        } else if(op==0xB0) {
            //71814 tag2: SUBU then low28 mask;min signed28 wraps to itself.
            //Other source conversions remain outside this reached domain.
            const auto value=pop();
            if(value.kind!=Value::Kind::Scalar||(value.word>>28)!=2)
                throw std::runtime_error("B0 requires a tagged integer in the reached domain");
            stack_.push_back(Value::integer(int32_t((0u-uint32_t(value.signed_integer()))&0x0FFFFFFF)));
        } else if(op==0xB1) {
            const auto value=pop();
            if((value.kind!=Value::Kind::Scalar&&value.kind!=Value::Kind::Timestamp)||(value.word>>28)>3)
                throw std::runtime_error("B1 raw-payload truth outside finite scalar domain");
            stack_.push_back(boolean((value.word&0x0FFFFFFF)==0)); //71960;not ToNumber
        } else if(op==0xB4){
            //71AF0: unary bitwise complement,not Boolean negation.
            const auto value=pop();const auto scalar=numeric(value);
            auto value_out=Value::integer(int32_t((~uint32_t(scalar))&0x0FFFFFFF));
            ++b4_calls_;b4_operand_word_=value.word;b4_result_word_=value_out.word;
            stack_.push_back(value_out);
        } else if(op==0xB2||op==0xB3||op==0xB8||op==0xB9) {
            const auto value=pop();if(value.kind==Value::Kind::Unknown)throw std::runtime_error("Uninitialized truth operand");
            const bool nil=value.kind==Value::Kind::Scalar&&value.word>>28==0;
            const bool zero=value.kind==Value::Kind::Scalar&&value.word==0x20000000;
            if(op==0xB3){++b3_calls_;b3_pc_=cmd->pc;b3_operand_kind_=uint32_t(value.kind);b3_operand_word_=value.word;b3_result_word_=boolean(nil).word;}
            stack_.push_back(boolean(op==0xB2?!nil:op==0xB3?nil:op==0xB8?zero:!zero));
        } else if(op>=0xBA&&op<=0xBD) {
            const auto n=numeric(pop());
            stack_.push_back(boolean(op==0xBA?n>0:op==0xBB?n>=0:op==0xBC?n<0:n<=0));
        } else if(op==0xC8||op==0xC9) {
            const auto right=pop(),left=pop();
            const auto truth=[](const Value& v){
                if(v.kind!=Value::Kind::Scalar&&v.kind!=Value::Kind::Timestamp&&v.kind!=Value::Kind::ArrayHeader)
                    throw std::runtime_error("Logical payload truth of native pointer/string not bound");
                return (v.word&0x0FFFFFFF)!=0;
            };
            const bool a=truth(left),b=truth(right);stack_.push_back(boolean(op==0xC8?a&&b:a||b));
        } else if(op==0x41) {
            const auto delta=numeric(pop());
            if(delta<0)throw std::runtime_error("Negative script timer-base offset");
            // Source6E570 updates timestamp18 and returns to normal dispatch.
            // Ending this native slice here is an explicit host scheduling
            // adaptation;it is NOT a source instruction-budget clear.
            timer_base_=clock_tick_+uint32_t(delta);++timestamp_writes_;event.status=Status::Yield;
        } else if(op==0x44) {
            // 6E69C writes deadline1C and clears source budget3C. This is
            // distinct from41's timer-base18 and its native slice adapter.
            const auto delta=numeric(pop());
            if(delta<0)throw std::runtime_error("Negative source deadline domain not yet bound");
            wait_until_=clock_tick_+uint32_t(delta);++deadline_writes_;event.status=Status::Yield;
        } else if(op==0x8B) {
            const auto id=uint8_t(program_.operand(*cmd,1,1));const auto argc=program_.operand(*cmd,2,1);
            if(argc>stack_.size())return stop(cmd,"Builtin argument count;id="+std::to_string(id));
            if(id==0x20){
                if(argc!=2||event_depth_==UINT16_MAX)throw std::runtime_error("Input32 requires two arguments/live nesting");
                const auto& mode=stack_[stack_.size()-2];const auto& text=stack_.back();
                if(mode.kind!=Value::Kind::Scalar||(mode.word>>28)!=2||text.kind!=Value::Kind::String)
                    throw std::runtime_error("Input32 unsupported mode/text conversion domain");
                input_arguments_.assign(std::make_move_iterator(stack_.end()-2),std::make_move_iterator(stack_.end()));
                stack_.resize(stack_.size()-2);result_={};input_waiting_=true;
                ++event_depth_;display_state_=8;event.status=Status::Input;
            }else if(id==0x21){
                if(!argc||argc>32||event_depth_==UINT16_MAX)throw std::runtime_error("Native SELECT33 count/nesting budget");
                choice_arguments_.assign(std::make_move_iterator(stack_.end()-argc),std::make_move_iterator(stack_.end()));
                stack_.resize(stack_.size()-argc);result_={};
                choice_count_=argc;choice_waiting_=true;choice_arguments_taken_=false;
                ++event_depth_;display_state_=9;event.status=Status::Choice;
            }else{
                if(!service_)return stop(cmd,"Unbound builtin;id="+std::to_string(id));
                std::vector<Value> arguments(stack_.end()-argc,stack_.end());stack_.resize(stack_.size()-argc);
                result_=service_(id,arguments);
                if(waiting_event_)event.status=Status::Waiting;
            }
        } else if(op==0x65||op==0x67||op==0x6D) {
            const auto rhs=numeric(pop());const auto ref=pop();const auto lhs=numeric(load_reference(ref));
            const auto word=op==0x65?uint32_t(lhs)&uint32_t(rhs):op==0x67?uint32_t(lhs)|uint32_t(rhs):uint32_t(lhs)<<(uint32_t(rhs)&31);
            store_reference(ref,Value::integer(int32_t(word&0x0FFFFFFF)));
        } else if(op==0x5A||op==0x5B) {
            // Reached6FEAC tableE05FC[0] ->75874. Finite native scalar,
            // minute-tag and string/string domains;no guessed conversion.
            const auto right=pop(),ref=pop(),left=load_reference(ref);auto value=left;
            const bool right_nil=right.kind==Value::Kind::Scalar&&right.word>>28==0;
            if(!right_nil) {
                if(left.kind==Value::Kind::String&&right.kind==Value::Kind::String)value=Value::string(left.cp932+right.cp932);
                else if(left.kind==Value::Kind::Scalar&&left.word>>28==0)value=right;
                else {
                    const auto sum=uint32_t(numeric(left))+uint32_t(numeric(right));
                    value=Value::integer(int32_t(sum&0x0FFFFFFF));
                    if(left.kind==Value::Kind::Timestamp){value.kind=Value::Kind::Timestamp;value.word=(value.word&0x0FFFFFFF)|0x30000000;}
                }
                store_reference(ref,value);
            }
            if(op==0x5A)stack_.push_back(value);
        } else if(op==0xE0) {
            // 73B2C ->75874: finite source nil/Boolean/integer/timestamp
            // dispatch.75AC8 preserves a LEFT timestamp's tag3.
            const auto right=pop(),left=pop();auto value=left;
            const auto lt=left.word>>28,rt=right.word>>28;
            const auto scalar=[](const Value& v){return v.kind==Value::Kind::Scalar||v.kind==Value::Kind::Timestamp;};
            if(!scalar(left)||!scalar(right)||lt>3||rt>3)
                throw std::runtime_error("E0 addition domain not bound;left="+std::to_string(left.word)+";right="+std::to_string(right.word));
            if(rt==0) { /* RHS nil leaves LEFT unchanged,not a numeric zero. */ }
            else if(lt==0)value=right;
            else {
                value=Value::integer(int32_t((uint32_t(numeric(left))+uint32_t(numeric(right)))&0x0FFFFFFF));
                if(lt==3){value.kind=Value::Kind::Timestamp;value.word=(value.word&0x0FFFFFFF)|0x30000000;}
            }
            stack_.push_back(std::move(value));
        } else if(op==0xC0||op==0xC1||op==0xC2||op==0xC3||op==0xC4||op==0xC5) {
            const auto right=pop(),left=pop();bool test=false;
            if(op==0xC0||op==0xC1) {
                const auto tag=left.word>>28;
                if(left.kind==Value::Kind::String)test=right.kind==Value::Kind::String&&left.cp932==right.cp932;
                else if(left.kind==Value::Kind::Scalar&&tag==0)test=right.kind==Value::Kind::Scalar&&right.word>>28==0;
                else if(left.kind==Value::Kind::Scalar&&tag==1)test=right.kind==Value::Kind::Scalar&&right.word>>28==1&&(left.word&1)==(right.word&1);
                else if(left.kind!=Value::Kind::Reference&&tag!=9&&tag<11)test=left.kind==right.kind&&left.word==right.word;
                if(op==0xC1)test=!test;
            } else test=op==0xC2?numeric(left)<numeric(right):op==0xC3?numeric(left)<=numeric(right):op==0xC4?numeric(left)>numeric(right):numeric(left)>=numeric(right);
            stack_.push_back(boolean(test));
        } else if(op==0xD1||op==0xD2||op==0xD4||op==0xE3) {
            const auto rhs=numeric(pop()),lhs=numeric(pop());int64_t n=0;
            if(op==0xD1||op==0xD2) {if(!rhs)throw std::runtime_error("Script division by zero");n=op==0xD1?int64_t(lhs)/rhs:int64_t(lhs)%rhs;}
            else n=op==0xD4?uint32_t(lhs)&uint32_t(rhs):uint32_t(lhs)<<(uint32_t(rhs)&31);
            stack_.push_back(Value::integer(int32_t(uint32_t(n)&0x0FFFFFFF)));
        } else if(op==0xD5) {
            // E050C ->7345C;73544 OR uses signed low28 scalar conversions.
            // Typed native references with raw0 are not source tag0 values.
            const auto right=pop(),left=pop();
            const auto bits=[](const Value& v){
                if((v.kind!=Value::Kind::Scalar&&v.kind!=Value::Kind::Timestamp)||v.word>>28>3)
                    throw std::runtime_error("D5 bitwise OR conversion outside finite scalar domain");
                const auto p=v.word&0x0FFFFFFF;return p&0x08000000?p|0xF0000000:p;
            };
            const auto answer=Value::integer(int32_t((bits(left)|bits(right))&0x0FFFFFFF));
            if(!d5_calls_){d5_first_pc_=pc_;d5_first_left_word_=left.word;d5_first_right_word_=right.word;d5_first_result_word_=answer.word;}
            ++d5_calls_;stack_.push_back(answer);
        } else if(op==0xD0) {
            //72C24 ->75EC0: owned LEFT/RIGHT temporaries. Numeric signed
            // MULT/MFLO at75F80/75F9C yields tag2 low28. A nil RIGHT clears
            // LEFT to nil; a nil LEFT with numeric RIGHT multiplies as zero.
            const auto right=pop(),left=pop();
            const auto scalar=[](const Value& v){return (v.kind==Value::Kind::Scalar||v.kind==Value::Kind::Timestamp)&&v.word>>28<=3;};
            if(!scalar(left)||!scalar(right))throw std::runtime_error("D0 multiplication conversion outside finite scalar domain");
            Value answer;answer.kind=Value::Kind::Scalar;
            if(right.word>>28)answer=Value::integer(int32_t(uint32_t(int64_t(numeric(left))*numeric(right))&0x0FFFFFFF));
            d0_left_word_=left.word;d0_right_word_=right.word;d0_result_word_=answer.word;++d0_calls_;
            stack_.push_back(answer);
        } else if(op==0xE2) {
            // E0540 ->73C74. Source73D5C SRAV: signed LEFT shifted by
            // RIGHT&31, then low28 tagged as integer. Retain finite numeric
            // conversions only; vector/string/opaque conversion stays refused.
            const auto right=pop(),left=pop();
            const auto shift=uint32_t(numeric(right))&31;const auto value=numeric(left);
            uint32_t bits=uint32_t(value)>>shift;
            if(value<0&&shift)bits|=UINT32_MAX<<(32-shift);
            const auto answer=Value::integer(int32_t(bits&0x0FFFFFFF));
            e2_left_word_=left.word;e2_right_word_=right.word;e2_result_word_=answer.word;++e2_calls_;
            stack_.push_back(answer);
        } else if(op==0xE1) {
            // Reached73BD0 ->75C18: binary subtraction. Tag3 minute values
            // retain tag3;scalar-domain subtraction stores tag2. Source nil
            // RHS leaves the left value unchanged.
            const auto right=pop(),left=pop();auto value=left;
            if(!(right.kind==Value::Kind::Scalar&&right.word>>28==0)){
                const auto difference=uint32_t(numeric(left))-uint32_t(numeric(right));
                value=Value::integer(int32_t(difference&0x0FFFFFFF));
                if(left.kind==Value::Kind::Timestamp){value.kind=Value::Kind::Timestamp;value.word=(value.word&0x0FFFFFFF)|0x30000000;}
            }
            stack_.push_back(std::move(value));
        } else if (op == 0x10 || op == 0x11 || op == 0x12) {
            uint32_t target = program_.operand(*cmd, 1, 3);
            const Command* destination = program_.command(target);
            if (!destination || destination->opcode < 0) return stop(cmd, "Branch target is metadata or not a command");
            bool take = true;
            if (op != 0x10) {
                if (stack_.empty()) return stop(cmd, "Conditional branch stack underflow");
                const Value& condition = stack_.back();
                bool nonzero=false;
                if(condition.kind==Value::Kind::Scalar)nonzero=(condition.word&0x0FFFFFFF)!=0;
                else if(condition.kind==Value::Kind::Reference&&condition.table==Table::Ctx28){
                    //6BBF8 masks the original nonnull reference address;
                    //6BC20 tests its presence, without loading the referent.
                    //Native symbolic references can have raw0. Admit only
                    //the reached, bounded table in this live registered owner.
                    if(!context_||condition.context_owner.lock()!=context_||context_->vm!=this||context_->thread!=std::this_thread::get_id()||condition.index>=ctx28_.size())
                        return stop(cmd,"Conditional Ctx28 reference needs its live registered table owner");
                    nonzero=true;
                    if(!ctx_reference_branches_){ctx_reference_first_pc_=pc_;ctx_reference_first_index_=condition.index;}
                    ++ctx_reference_branches_;
                }else return stop(cmd,"Conditional payload for non-scalar unsupported");
                take = op == 0x11 ? !nonzero : nonzero;
                stack_.pop_back();
            }
            event.has_branch_decision = true; event.branch_taken = take;
            if (take) next = target;
        } else if(op==0x8C){stack_.push_back(context_value());
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
                op == 0x44 ? "Delay scheduler not implemented (opcode 44 is not stack discard)" : "Unsupported opcode semantics";
            return stop(cmd, reason);
        }
        if (stack_.size() > 32767) return stop(cmd, "Native operand stack budget exceeded");
        ++executed_; pc_ = next;
        if(captured_normal_return<normal_returns_.size()){
            auto& trace=normal_returns_[captured_normal_return];trace.after=current_witness();trace.valid=trace.before.valid&&trace.after.valid;
        }
        if(observe_frame){
            transition.after=current_witness();
            try{frame_observer_(transition);}catch(...){frame_observer_failed_=true;frame_observer_={};}
        }
        if(op==0x0E)drain_callback();
        event.next_pc = pc_; return event;
    } catch (const std::exception& ex) { return stop(cmd, ex.what()); }
}
Event Vm::run(uint32_t instruction_budget) {
    // Source74C84/74CA0 wrap interpreter execution in an event-drain scope.
    // The hard native quota below is separate from source ctx3C/ctx58 timing.
    ++event_depth_;
    try{
    for (uint32_t i = 0; i < instruction_budget; ++i) {
        Event event = step();
        if (event.status != Status::Running) {if(event_depth_)--event_depth_;drain_callback();return event;}
    }
    if(event_depth_)--event_depth_;
    drain_callback();
    Event event;event.status=Status::HostYield;event.pc=event.next_pc=pc_;
    event.file_offset=pc_+28;const auto* cmd=program_.command(pc_);event.opcode=cmd?cmd->opcode:-1;
    return event;
    }catch(const std::exception& error){return stop(program_.command(pc_),error.what());}
}
std::vector<Value> Vm::take_choice_arguments(){
    if(!choice_waiting_||choice_arguments_taken_)throw std::runtime_error("SELECT arguments unavailable/already consumed");
    choice_arguments_taken_=true;return std::move(choice_arguments_);
}
bool Vm::complete_choice(uint32_t result){
    if(stopped_||!choice_waiting_||!choice_arguments_taken_||result>choice_count_)return false;
    result_=Value::integer(int32_t(result));display_state_=2;choice_waiting_=false;
    if(event_depth_)--event_depth_;
    drain_callback();return true;
}
std::vector<Value> Vm::input_arguments() const{
    if(stopped_||!input_waiting_)throw std::runtime_error("Input32 arguments unavailable");
    return input_arguments_;
}
bool Vm::complete_input(std::string text,std::string auxiliary){
    if(stopped_||!input_waiting_||display_state_!=8||input_arguments_.size()!=2)return false;
    //Construct/validate owning strings before changing the suspended context.
    Value nil;nil.kind=Value::Kind::Scalar;
    std::vector<Value> cells(3,nil);cells[0].kind=Value::Kind::ArrayHeader;cells[0].word=0x90000002;
    if(!text.empty()){cells[1]=Value::string(std::move(text));cells[2]=Value::string(std::move(auxiliary));}
    input_arguments_.clear();result_={};
    *input_result_=std::move(cells);result_.kind=Value::Kind::Reference;
    result_.table=Table::InputResult;result_.frame_owner=input_result_;
    display_state_=2;input_waiting_=false;
    if(event_depth_)--event_depth_;
    drain_callback();return true;
}
} // namespace dokodemo::script
