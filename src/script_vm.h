#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include "recovered/current_tables_native.h"

// Restricted KSC v20030204 reader/interpreter. No PSP CPU, OS or GPU emulation.
// Source tables and strings are data; reading them never emits dialogue.
namespace dokodemo::script {
class Vm;
struct NativeScriptContext;

struct Diagnostic {
    std::string reason;
    uint32_t pc = 0;             // relative to code base
    uint32_t file_offset = 0;    // pc + 28 for bytecode
    int opcode = -1;
    std::string text() const;
};
struct Source {
    std::filesystem::path path;
    std::string sha256;
};
struct Header {
    uint32_t entrypoint = 0, code_size = 0, pool_size = 0;
    uint16_t field14 = 0, local_count = 0, global_count = 0, ctx28_count = 0;
    uint32_t pool_start() const { return 28 + code_size; }
    uint32_t pool_end() const { return pool_start() + pool_size; }
    uint32_t ctx28_start() const { return pool_end() + 4u * local_count; }
    uint32_t global_start() const { return ctx28_start() + 4u * ctx28_count; }
};
enum class Table : uint8_t { Frame, Local, Global, Ctx28, InputResult };
struct Reference {
    uint32_t file_offset = 0, target = 0;
    uint8_t width = 0;
    bool pool = false;
};
struct InlineControl { uint32_t file_offset = 0; uint16_t index = 0; uint8_t opcode = 0; };
struct Command {
    uint32_t pc = 0, size = 0;
    int opcode = -1;             // -1: linked switch metadata, not an opcode
    std::vector<Reference> references;
    std::vector<InlineControl> controls;
};

class Program {
public:
    // Only the five immutable original script identities are accepted.
    // Failed loads clear the program; error contains exact parser location.
    bool load_original(const std::filesystem::path& path, Diagnostic& error);
    bool load_original_bytes(std::vector<uint8_t> bytes, const std::filesystem::path& source,
                             Diagnostic& error);
    // Explicit synthetic format/VM fixture entrypoint for portable unit tests.
    // Game resource callers use load_original_bytes with original identity.
    bool load_test_fixture(std::vector<uint8_t> bytes, Diagnostic& error);
    const Header& header() const { return header_; }
    const Source& source() const { return source_; }
    const std::vector<uint8_t>& bytes() const { return bytes_; }
    const std::vector<Command>& commands() const { return commands_; }
    const std::vector<Reference>& references() const { return references_; }
    const Command* command(uint32_t pc) const;
    uint32_t operand(const Command& command, uint32_t relative, uint8_t width) const;
    uint32_t initial_word(Table table, uint16_t index) const;
    std::string pool_bytes(uint32_t relative) const;
    bool loaded() const { return !bytes_.empty(); }
    // Explicit source-loader domain. The caller supplies actual masks and
    // allocator/context/stack/lifetime mappings; default input is unavailable.
    // The original verified image and its identity always come from Program.
    recovered::CurrentTablesOwner load_current_tables(recovered::CurrentTablesInput input) const;
private:
    Header header_;
    Source source_;
    std::vector<uint8_t> bytes_;
    std::vector<Command> commands_;
    std::vector<Reference> references_;
    std::vector<uint32_t> command_index_;
    void parse();
};

struct DictionaryRecord {
    uint32_t index = 0, file_offset = 0, end = 0;
    std::array<uint32_t, 20> words{}; // opaque metadata is preserved
    std::array<uint32_t, 3> text_offsets{};
    std::string written_cp932, reading_cp932, alternate_cp932, initial_cp932;
};
class Dictionary {
public:
    bool load_original(const std::filesystem::path& path, Diagnostic& error);
    // Data-only counterpart to the file adapter; original five hash pins and
    // exact record validation still apply. source is provenance metadata.
    // Failure clears records/source and retains the diagnostic.
    bool load_original_bytes(std::vector<uint8_t> bytes, const std::filesystem::path& source,
                             Diagnostic& error);
    const Source& source() const { return source_; }
    const std::vector<DictionaryRecord>& records() const { return records_; }
private:
    Source source_;
    std::vector<DictionaryRecord> records_;
};

struct Value {
    enum class Kind { Unknown, Scalar, String, Code, Reference, ArrayHeader, Timestamp, Opaque, Context };
    Kind kind = Kind::Unknown;
    uint32_t word = 0;          // preserve original tag/payload for scalar/opaque
    Table table = Table::Frame;
    uint16_t index = 0;
    std::string cp932;
    // Native lifetime for references passed into a nested function. Global,
    // local and image-table references use the VM's stable typed tables.
    std::weak_ptr<std::vector<Value>> frame_owner;
    std::weak_ptr<NativeScriptContext> context_owner;
    static Value integer(int32_t value);
    static Value string(std::string cp932);
    int32_t signed_integer() const;
};

// Passive fixed-capacity evidence; contains no Value, pointer or owner lease.
struct ValueWitness {
    Value::Kind kind=Value::Kind::Unknown;
    uint32_t word=0,table=0,index=0;
    uint64_t cp932_bytes=0;
    std::array<uint8_t,32> cp932_sha256{};
};
struct VmStateWitness {
    bool valid=false,context_registered=false,event_envelope_present=false,stopped=false;
    uint64_t run_id=0,frame_id=0,executed=0,clock_tick=0,deadline_tick=0;
    int32_t relative_deadline23=0;
    uint32_t current_pc=0,code_entry=0,context_word=0,call_depth=0,active_blocks=0,event_scope_depth=0,queued_callbacks=0;
    uint32_t caller_entry=0,caller_call_pc=0,caller_return_pc=0;
    uint64_t caller_frame_id=0,caller_stack_base=0;
    uint64_t frame_cells=0,operand_cells=0;
    uint32_t frame_captured=0,operands_captured=0;
    std::array<char,65> code_sha256{};
    ValueWitness result;
    std::array<ValueWitness,16> frame_values{},operands{};
};
struct NormalReturnWitness {
    bool valid=false;
    uint64_t sequence=0;
    uint32_t opcode_pc=0;
    VmStateWitness before,after;
};

enum class Status { Running, Message, Choice, Yield, HostYield, Waiting, Complete, Stopped, Input };
struct Event {
    Status status = Status::Stopped;
    uint32_t pc = 0, next_pc = 0, file_offset = 0;
    int opcode = -1;
    bool branch_taken = false;
    bool has_branch_decision = false;
    std::string message_cp932;  // expanded request from executed opcode 48 only
    uint32_t display_state = 0; // observed ctx+34 = 6 for opcode 48
    uint16_t display_count = 0; // observed ctx+56 increments
    Diagnostic diagnostic;
};

class Vm {
public:
    explicit Vm(const Program& program) : program_(program) {}
    ~Vm();
    Vm(const Vm&)=delete;
    Vm& operator=(const Vm&)=delete;
    // No default argument values are invented. Unprovided slots are Unknown.
    // GameActorScript supplies the recovered230EC/67FD4 argc2 caller. Other
    // standalone probes must explicitly provide their own admitted arguments.
    Event start(const std::vector<Value>& arguments = {});
    // Isolated original function fixture, NOT a normal gameplay entry point.
    Event start_at(uint32_t code_pc, const std::vector<Value>& arguments);
    Event step();
    Event run(uint32_t instruction_budget = 1000);
    const std::vector<Value>& stack() const { return stack_; }
    uint32_t pc() const { return pc_; }
    VmStateWitness current_witness() const noexcept;
    void trace_normal_return_at(uint32_t pc, size_t slot=0,uint32_t alternate_pc=UINT32_MAX) noexcept {
        if(slot<normal_returns_.size()){return_probe_pcs_[slot]=pc;return_probe_alternate_pcs_[slot]=alternate_pc;normal_returns_[slot]={};}
    }
    const NormalReturnWitness& last_normal_return(size_t slot=0) const noexcept {
        return normal_returns_[slot<normal_returns_.size()?slot:0];
    }
    uint32_t b3_calls() const noexcept { return b3_calls_; }
    uint32_t b3_pc() const noexcept { return b3_pc_; }
    uint32_t b3_operand_kind() const noexcept { return b3_operand_kind_; }
    uint32_t b3_operand_word() const noexcept { return b3_operand_word_; }
    uint32_t b3_result_word() const noexcept { return b3_result_word_; }
    uint32_t display_state() const { return display_state_; }
    uint16_t display_count() const { return display_count_; }
    bool stopped() const { return stopped_; }
    // Windows owning-frame policy, explicitly enabled by GameActorScript.
    // Only additional RESERVED locals get canonical nil. Provided arguments,
    // including Unknown/opaque values, are never rewritten. Probes stay strict.
    void native_reserved_local_nil(bool enabled) noexcept { native_reserved_local_nil_=enabled; }
    bool native_reserved_local_nil() const noexcept { return native_reserved_local_nil_; }
    Value read_slot(Table table, uint16_t index) const;
    Value load_reference(const Value& reference) const;
    Value peek_reference(const Value& reference) const; // diagnostic capture;not an executed read
    void store_reference(const Value& reference, Value value);
    Value indexed_reference(const Value& reference, uint32_t index) const;
    // Native game services are explicit, checked callbacks. An unbound or
    // unsupported service stops with its real id/arguments,never success.
    using HostService = std::function<Value(uint8_t, const std::vector<Value>&)>;
    void host_service(HostService callback) { service_ = std::move(callback); }
    const Value& result() const noexcept { return result_; }
    void access_observer(std::function<void(const Value&,bool)> callback) { access_ = std::move(callback); }
    uint64_t executed() const noexcept { return executed_; }
    uint32_t call_depth() const noexcept { return uint32_t(calls_.size()); }
    Value context_value();
    int32_t post_callback(const Value& context,const Value& code,const Value& first,const Value& second);
    //1B4C0/6B4B8 owner event uses the same envelope/queue,callerID=-1:
    //two scalar arguments,without the context-token third argument.
    int32_t post_owner_event(const Value& code,int32_t event_id,int32_t argument);
    uint32_t callback_depth()const noexcept{return event_depth_;}
    uint32_t queued_callbacks()const noexcept{return uint32_t(pending_callbacks_.size());}
    uint64_t deadline_tick()const noexcept{return wait_until_;}
    bool context_enabled()const noexcept{return context_enabled_;}
    //697DC retains the arguments while state9 waits for the game SELECT
    //consumer. Conversion transfers their ownership once;683E8 publishes
    //the integer only after the real SELECT close producer reaches state3.
    std::vector<Value> take_choice_arguments();
    bool complete_choice(uint32_t result);
    bool choice_waiting()const noexcept{return choice_waiting_;}
    //697C0/state8 retains exactly two original arguments until682B8. The
    //native result buffer models ctx2C's fixed header+two owning cells; its
    //references remain weak and expire on restart/retirement,like frames.
    std::vector<Value> input_arguments() const;
    bool complete_input(std::string text,std::string auxiliary);
    bool input_waiting()const noexcept{return input_waiting_;}
    uint32_t b4_calls()const noexcept{return b4_calls_;}
    uint32_t b4_operand_word()const noexcept{return b4_operand_word_;}
    uint32_t b4_result_word()const noexcept{return b4_result_word_;}
    uint32_t e2_calls()const noexcept{return e2_calls_;}
    uint32_t e2_left_word()const noexcept{return e2_left_word_;}
    uint32_t e2_right_word()const noexcept{return e2_right_word_;}
    uint32_t e2_result_word()const noexcept{return e2_result_word_;}
    uint32_t d0_calls()const noexcept{return d0_calls_;}
    uint32_t d0_left_word()const noexcept{return d0_left_word_;}
    uint32_t d0_right_word()const noexcept{return d0_right_word_;}
    uint32_t d0_result_word()const noexcept{return d0_result_word_;}
    uint32_t d5_calls()const noexcept{return d5_calls_;}
    uint32_t d5_first_pc()const noexcept{return d5_first_pc_;}
    uint32_t d5_first_left_word()const noexcept{return d5_first_left_word_;}
    uint32_t d5_first_right_word()const noexcept{return d5_first_right_word_;}
    uint32_t d5_first_result_word()const noexcept{return d5_first_result_word_;}
    uint32_t ctx_reference_branches()const noexcept{return ctx_reference_branches_;}
    uint32_t ctx_reference_first_pc()const noexcept{return ctx_reference_first_pc_;}
    uint32_t ctx_reference_first_index()const noexcept{return ctx_reference_first_index_;}
    // Named native modal scheduler adapter. Called only after actual GUI close,
    // poll completion and teardown;original byte38 wake producer is unclosed.
    void resume_modal_context();
    uint32_t callbacks_posted()const noexcept{return callbacks_posted_;}
    uint32_t callbacks_returned()const noexcept{return callbacks_returned_;}
    void clock_tick(uint64_t value) noexcept { clock_tick_ = value; }
    void random_source(std::function<uint32_t()> callback){random_=std::move(callback);}
    uint64_t random_selections() const noexcept{return random_selections_;}
    uint32_t root_functions_returned() const noexcept{return root_returns_;}
    const Value& last_random_value() const noexcept{return last_random_;}
    uint32_t control_blocks_entered() const noexcept { return blocks_entered_; }
    uint32_t control_blocks_closed() const noexcept { return blocks_closed_; }
    uint32_t active_control_blocks() const noexcept { return uint32_t(blocks_.size()); }
    uint32_t switches_executed() const noexcept { return switches_; }
    void clear_event_signal() noexcept { event_signal_=false; }
    void signal_event() noexcept { event_signal_=true;waiting_event_=false; }
    void wait_for_event() noexcept { waiting_event_=!event_signal_; }
    bool waiting_for_event() const noexcept { return waiting_event_; }
    uint32_t event_scope_depth() const noexcept { return event_depth_; }
    uint32_t timestamp_writes() const noexcept { return timestamp_writes_; }
    uint32_t deadline_writes() const noexcept { return deadline_writes_; }
    bool deadline_waiting() const noexcept { return clock_tick_<wait_until_; }
private:
    const Program& program_; // program must outlive Vm and remain unchanged
    std::vector<Value> stack_, local_, global_, ctx28_;
    std::shared_ptr<std::vector<Value>> frame_;
    struct CallFrame {
        uint32_t return_pc = 0;
        size_t stack_base = 0;
        bool reserved = false;
        std::shared_ptr<std::vector<Value>> values;
        uint64_t frame_id=0;
        uint32_t code_entry=0,call_pc=0;
    };
    std::vector<CallFrame> calls_;
    // Source18's six administrative cells have typed native ownership rather
    // than PSP stack addresses. They are not script operands or fake values.
    struct ControlBlock {
        uint32_t close_pc = 0;
        size_t operand_base = 0, call_depth = 0;
        std::weak_ptr<std::vector<Value>> frame;
    };
    std::vector<ControlBlock> blocks_;
    struct PendingCallback {uint32_t pc=0;Value first,second,caller;bool include_caller=true;};
    struct EventEnvelope {
        uint32_t pc=0;bool reserved=false,enabled=true,waiting=false,signal=true;
        uint64_t frame_id=0;
        uint32_t code_entry=0;
        int32_t deadline_delta=0; //source packed signed23 relative deadline
        Value result;
        std::vector<Value> stack;
        std::shared_ptr<std::vector<Value>> frame;
        std::vector<CallFrame> calls;
        std::vector<ControlBlock> blocks;
    };
    std::shared_ptr<NativeScriptContext> context_;
    std::deque<PendingCallback> pending_callbacks_;
    std::unique_ptr<EventEnvelope> event_envelope_;
    bool context_enabled_=true,event_override_=false;
    uint32_t callbacks_posted_=0,callbacks_returned_=0;
    void enter_callback(PendingCallback callback);
    void drain_callback();
    uint32_t blocks_entered_ = 0, blocks_closed_ = 0, switches_ = 0;
    int32_t opcode_cost_ = 1; // named native instruction-cost policy
    uint32_t event_depth_=0;
    bool event_signal_=true,waiting_event_=false; // source65FF4 SH1,54
    uint32_t timestamp_writes_=0,deadline_writes_=0;
    HostService service_;
    std::function<void(const Value&,bool)> access_;
    std::function<uint32_t()> random_;
    uint64_t random_selections_=0;
    uint32_t root_returns_=0;
    Value last_random_;
    Value result_;
    std::vector<Value> choice_arguments_;
    bool choice_waiting_=false,choice_arguments_taken_=false;
    uint32_t choice_count_=0;
    std::vector<Value> input_arguments_;
    std::shared_ptr<std::vector<Value>> input_result_;
    bool input_waiting_=false;
    uint32_t b4_calls_=0,b4_operand_word_=0,b4_result_word_=0;
    uint32_t e2_calls_=0,e2_left_word_=0,e2_right_word_=0,e2_result_word_=0;
    uint32_t d0_calls_=0,d0_left_word_=0,d0_right_word_=0,d0_result_word_=0;
    uint32_t d5_calls_=0,d5_first_pc_=0,d5_first_left_word_=0,d5_first_right_word_=0,d5_first_result_word_=0;
    uint32_t ctx_reference_branches_=0,ctx_reference_first_pc_=0,ctx_reference_first_index_=0;
    uint64_t executed_ = 0;
    uint64_t clock_tick_ = 0, timer_base_ = 0, wait_until_ = 0;
    uint32_t pc_ = 0, display_state_ = 0;
    uint16_t display_count_ = 0;
    bool stopped_ = true, frame_reserved_ = false;
    bool native_reserved_local_nil_ = false;
    Diagnostic last_error_;
    uint64_t next_frame_id_=0,frame_id_=0,run_id_=0,return_sequence_=0;
    uint32_t code_entry_=0;
    std::array<uint32_t,2> return_probe_pcs_{{UINT32_MAX,UINT32_MAX}};
    std::array<uint32_t,2> return_probe_alternate_pcs_{{UINT32_MAX,UINT32_MAX}};
    std::array<NormalReturnWitness,2> normal_returns_{};
    uint32_t b3_calls_=0,b3_pc_=0,b3_operand_kind_=0,b3_operand_word_=0,b3_result_word_=0;
    Value decode(uint32_t word) const;
    std::vector<Value>& slots(Table table);
    std::string expand(const Command& command) const;
    Event stop(const Command* command, const std::string& reason);
};
} // namespace dokodemo::script
