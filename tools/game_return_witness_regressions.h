#pragma once
#include "../src/script_vm.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Explicit synthetic fixtures for passive native diagnostics. No original
// resources or PSP execution; no source469, nonzero62 or opcode48UntilAck claim.
namespace dokodemo::return_witness_regression_detail {
static_assert(std::is_trivially_copyable_v<script::ValueWitness>, "value evidence must not own script values");
static_assert(std::is_trivially_copyable_v<script::VmStateWitness>, "state evidence must not own frames");
static_assert(std::is_trivially_copyable_v<script::NormalReturnWitness>, "return evidence must be value-only");
inline void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("return witness regression: " + message);
}
inline void StatusIs(const script::Event& event, script::Status expected, const char* where) {
    Require(event.status==expected,std::string(where)+": "+event.diagnostic.text());
}
inline void Put(std::vector<uint8_t>& bytes, size_t at, uint32_t word, unsigned width=4) {
    for (unsigned i=0;i<width;++i) bytes.at(at+i)=uint8_t(word>>(8*i));
}
inline void Append(std::vector<uint8_t>& code, std::initializer_list<uint8_t> bytes) {
    code.insert(code.end(),bytes.begin(),bytes.end());
}
inline script::Program Program(std::vector<uint8_t> code) {
    std::vector<uint8_t> bytes(28+code.size());
    Put(bytes,0,0x0043534B); Put(bytes,4,0x20030204); Put(bytes,12,uint32_t(code.size()));
    std::copy(code.begin(),code.end(),bytes.begin()+28);
    script::Program program; script::Diagnostic error;
    Require(program.load_test_fixture(std::move(bytes),error),"load explicit fixture: "+error.text());
    return program;
}
inline std::string OwnerText() { return std::string(170,'o')+"\x83\x67\x83\x8D"; }
inline script::Value Code(uint32_t pc) {
    script::Value value; value.kind=script::Value::Kind::Code; value.word=0x70000000u|pc; return value;
}
inline bool Same(const script::ValueWitness& a, const script::ValueWitness& b) {
    return a.kind==b.kind && a.word==b.word && a.table==b.table && a.index==b.index
        && a.cp932_bytes==b.cp932_bytes && a.cp932_sha256==b.cp932_sha256;
}
inline bool Same(const script::VmStateWitness& a, const script::VmStateWitness& b) {
    if (a.valid!=b.valid || a.context_registered!=b.context_registered
        || a.event_envelope_present!=b.event_envelope_present || a.stopped!=b.stopped
        || a.run_id!=b.run_id || a.frame_id!=b.frame_id || a.executed!=b.executed
        || a.clock_tick!=b.clock_tick || a.deadline_tick!=b.deadline_tick
        || a.relative_deadline23!=b.relative_deadline23 || a.current_pc!=b.current_pc
        || a.code_entry!=b.code_entry || a.context_word!=b.context_word
        || a.call_depth!=b.call_depth || a.active_blocks!=b.active_blocks
        || a.event_scope_depth!=b.event_scope_depth || a.queued_callbacks!=b.queued_callbacks
        || a.caller_entry!=b.caller_entry || a.caller_call_pc!=b.caller_call_pc
        || a.caller_return_pc!=b.caller_return_pc || a.caller_frame_id!=b.caller_frame_id
        || a.caller_stack_base!=b.caller_stack_base || a.frame_cells!=b.frame_cells
        || a.operand_cells!=b.operand_cells || a.frame_captured!=b.frame_captured
        || a.operands_captured!=b.operands_captured || a.code_sha256!=b.code_sha256
        || !Same(a.result,b.result)) return false;
    for (size_t i=0;i<a.frame_values.size();++i)
        if (!Same(a.frame_values[i],b.frame_values[i]) || !Same(a.operands[i],b.operands[i])) return false;
    return true;
}
inline bool Same(const script::NormalReturnWitness& a, const script::NormalReturnWitness& b) {
    return a.valid==b.valid && a.sequence==b.sequence && a.opcode_pc==b.opcode_pc
        && Same(a.before,b.before) && Same(a.after,b.after);
}
inline void Scalar(const script::ValueWitness& value, uint32_t word, const char* where) {
    Require(value.kind==script::Value::Kind::Scalar && value.word==word
            && value.cp932_bytes==0,where);
}
inline void Reference(const script::ValueWitness& value, const char* where) {
    Require(value.kind==script::Value::Kind::Reference && value.table==uint32_t(script::Table::Frame)
            && value.index==0 && value.cp932_bytes==0,where);
}
inline void Owner(const script::ValueWitness& value, const char* where) {
    // Independent .NET SHA256 pin of 170 ASCII 'o' bytes followed by 83 67 83 8D.
    constexpr std::array<uint8_t,32> digest{
        0xD1,0x88,0xEB,0x9E,0xB6,0x2C,0xE0,0xE1,0x3D,0x8F,0x2B,0xE5,0x9F,0x41,0x07,0x6C,
        0x95,0xC5,0x19,0x98,0x61,0xD0,0xF3,0xDB,0xAA,0xF4,0x6B,0x74,0x7D,0x33,0x9B,0x9F};
    Require(value.kind==script::Value::Kind::String && value.word==0x60000000
            && value.cp932_bytes==174 && value.cp932_sha256==digest,where);
}
inline void Identity(const script::VmStateWitness& state, const script::Program& program) {
    Require(state.valid && program.source().sha256.size()==64 && state.code_sha256[64]=='\0'
            && std::string(state.code_sha256.data())==program.source().sha256,
            "witness preserves the actual synthetic Program SHA256");
}
inline void Expired(script::Vm& vm, const script::Value& reference, const char* where) {
    Require(reference.frame_owner.expired(),where);
    bool refused=false;
    try { (void)vm.peek_reference(reference); }
    catch (const std::exception& error) {
        refused=std::string(error.what()).find("Retired native script frame reference")!=std::string::npos;
    }
    Require(refused,"retired reference is still refused while evidence copies survive");
}
inline void Passive(const script::Vm& vm, uint32_t& observed_accesses) {
    const auto pc=vm.pc(); const auto executed=vm.executed(); const auto accesses=observed_accesses;
    const auto first=vm.current_witness(); const auto trace=vm.last_normal_return();
    for (unsigned i=0;i<3;++i) Require(Same(first,vm.current_witness()),"repeated projections do not mutate state or IDs");
    Require(vm.pc()==pc && vm.executed()==executed && observed_accesses==accesses
            && Same(trace,vm.last_normal_return()),"projection does not execute, dereference or fabricate returns");
}
struct Fixture {
    script::Program program;
    uint32_t call_pc=0,argument_pc=0,continuation_pc=0,callee_pc=0,return_pc=0;
    uint32_t callback_pc=0,callback_return_pc=0,sleep_pc=0,sleep_continuation_pc=0;
    uint32_t nested_pc=0,nested_call_pc=0,nested_return_pc=0;
};
inline Fixture Build() {
    // Caller retains a real reference plus operand27; the call relocates only
    // its single argument. Callee's real reference and operand55 are discarded31.
    std::vector<uint8_t> code{0x30,1,0x91,0,0,0x8B,0x7A,1,0x91,0,0,0x80,27,0x80,7,0x44};
    Fixture f; f.argument_pc=uint32_t(code.size()); Append(code,{0x90,0,0});
    f.call_pc=uint32_t(code.size()); Append(code,{0x20,1,0,0,0});
    f.continuation_pc=uint32_t(code.size()); Append(code,{0x89,0x8B,0x7D,3,0x38});
    f.callee_pc=uint32_t(code.size()); Put(code,f.call_pc+2,f.callee_pc,3);
    Append(code,{0x30,1,0x91,0,0,0x8B,0x7B,1,0x91,0,0,0x80,55,0x87,0x70});
    f.return_pc=uint32_t(code.size()); code.push_back(0x31);
    f.callback_pc=uint32_t(code.size());
    Append(code,{0x30,3,0x91,0,0,0x8B,0x7C,1,0x80,99,0x70});
    f.callback_return_pc=uint32_t(code.size()); code.push_back(0x32);
    f.sleep_pc=uint32_t(code.size());
    Append(code,{0x30,1,0x91,0,0,0x8B,0x7E,1,0x91,0,0,0x80,27,0x80,9,0x70,0x80,7,0x44});
    f.sleep_continuation_pc=uint32_t(code.size()); Append(code,{0x8B,0x7F,2,0x38});
    f.nested_pc=uint32_t(code.size());
    Append(code,{0x30,3,0x91,0,0,0x8B,0x7C,1,0x91,0,0,0x80,41,0x90,0,0});
    f.nested_call_pc=uint32_t(code.size()); Append(code,{0x20,1,0,0,0});
    Put(code,f.nested_call_pc+2,f.callee_pc,3);
    f.nested_return_pc=uint32_t(code.size()); code.push_back(0x32);
    f.program=Program(std::move(code)); return f;
}
inline void TrueReturn(const script::NormalReturnWitness& trace, const Fixture& f,
                       uint64_t caller_id, uint64_t callee_id, uint32_t caller_entry,
                       uint32_t call_pc, uint32_t return_pc, uint32_t operand) {
    const auto& before=trace.before; const auto& after=trace.after;
    Require(trace.valid && trace.sequence>0 && trace.opcode_pc==f.return_pc,
            "only the armed exact ordinary31 creates a valid trace");
    Identity(before,f.program); Identity(after,f.program);
    Require(before.current_pc==f.return_pc && after.current_pc==return_pc
            && before.code_entry==f.callee_pc && after.code_entry==caller_entry
            && before.frame_id==callee_id && after.frame_id==caller_id && callee_id>caller_id
            && before.run_id==after.run_id && before.call_depth==1 && after.call_depth==0
            && before.caller_entry==caller_entry && before.caller_call_pc==call_pc
            && before.caller_return_pc==return_pc && before.caller_frame_id==caller_id
            && before.caller_stack_base==2 && after.caller_frame_id==0
            && after.caller_stack_base==0 && after.executed==before.executed+1,
            "31 records callee before pop and restored caller after PC publication");
    Require(before.frame_cells==1 && before.frame_captured==1 && after.frame_captured>0,
            "trace captures both actual frames");
    Owner(before.frame_values[0],"callee has the actual copied CP932 argument");
    Owner(after.frame_values[0],"restored caller retains its actual CP932 frame value");
    Require(before.operand_cells==4 && before.operands_captured==4
            && after.operand_cells==2 && after.operands_captured==2,
            "31 truncates callee operands to the actual saved caller base");
    Reference(before.operands[0],"caller reference retained below argument relocation");
    Scalar(before.operands[1],0x20000000u|operand,"caller scalar retained below argument relocation");
    Reference(before.operands[2],"callee reference is captured as value-only metadata");
    Scalar(before.operands[3],0x20000037,"callee extra operand55 exists before31");
    Require(Same(before.operands[0],after.operands[0]) && Same(before.operands[1],after.operands[1]),
            "caller operand prefix survives ordinary31 exactly");
    Scalar(before.result,0x10000001,"actual TRUE87/set_result70 before31");
    Scalar(after.result,0x10000001,"ordinary31 preserves exact TRUE kind and word");
    Require(before.clock_tick==after.clock_tick && before.deadline_tick==after.deadline_tick
            && before.relative_deadline23==after.relative_deadline23
            && before.event_scope_depth==after.event_scope_depth && !before.stopped && !after.stopped,
            "ordinary31 preserves deadline, clock and native execution scope");
}
inline void NormalCallTrueAndQueueBoundary(bool queue_callback) {
    auto f=Build(); script::Vm vm(f.program); uint32_t accesses=0,consumers=0,callbacks=0;
    script::Value root_reference,callee_reference,callback_reference;
    bool enqueue=queue_callback;
    vm.access_observer([&](const script::Value&,bool){++accesses;});
    vm.host_service([&](uint8_t id,const std::vector<script::Value>& arguments) {
        if (id==0x7A || id==0x7B || id==0x7C) {
            Require(arguments.size()==1 && arguments[0].kind==script::Value::Kind::Reference,
                    "capture a real live frame reference");
            if (id==0x7A) root_reference=arguments[0];
            if (id==0x7B) {
                callee_reference=arguments[0];
                if (enqueue) Require(vm.post_callback(vm.context_value(),Code(f.callback_pc),
                            script::Value::string(OwnerText()),script::Value::integer(73))==0
                        && vm.queued_callbacks()==1,"host service queues inside native run scope");
            }
            if (id==0x7C) { callback_reference=arguments[0]; ++callbacks; }
            return script::Value::integer(9);
        }
        Require(id==0x7D && arguments.size()==3 && arguments[0].kind==script::Value::Kind::Reference
                && vm.peek_reference(arguments[0]).cp932==OwnerText()
                && arguments[1].signed_integer()==27 && arguments[2].kind==script::Value::Kind::Scalar
                && arguments[2].word==0x10000001,"caller consumes its real operands and exact TRUE once");
        ++consumers; return script::Value::integer(91);
    });
    vm.trace_normal_return_at(f.return_pc); // start must preserve the armed target.
    vm.clock_tick(100); std::vector<script::Value> arguments{script::Value::string(OwnerText())};
    StatusIs(vm.start(arguments),script::Status::Running,"ordinary start"); arguments[0].cp932="changed external caller";
    const auto root=vm.current_witness(); Identity(root,f.program);
    Require(root.frame_id>0 && root.run_id>0 && root.code_entry==0 && !root.context_registered
            && !vm.last_normal_return().valid,"start creates a real frame but no normal-return trace");
    Passive(vm,accesses);
    Require(!vm.current_witness().context_registered,"reading witness does not register a context");
    StatusIs(vm.run(100),script::Status::Yield,"root writes actual44 deadline");
    const auto sleeping=vm.current_witness();
    Require(sleeping.current_pc==f.argument_pc && sleeping.frame_id==root.frame_id
            && sleeping.clock_tick==100 && sleeping.deadline_tick==107 && sleeping.relative_deadline23==7
            && sleeping.operand_cells==2,"root owns nonzero deadline and its two operands");
    Owner(sleeping.frame_values[0],"external caller mutation cannot change witness digest");
    vm.clock_tick(106); const auto executed=vm.executed();
    StatusIs(vm.run(1),script::Status::Yield,"pending deadline refuses instruction");
    Require(vm.executed()==executed && !vm.last_normal_return().valid,"waiting fabricates no instruction or return");
    vm.clock_tick(107);
    StatusIs(vm.step(),script::Status::Running,"load single call argument");
    StatusIs(vm.step(),script::Status::Running,"actual20 creates callee");
    const auto held_callee=vm.current_witness();
    Require(held_callee.code_entry==f.callee_pc && held_callee.frame_id>root.frame_id,
            "actual call creation receives a fresh frame identity");
    StatusIs(vm.step(),script::Status::Running,"callee30 reserves frame");
    StatusIs(vm.run(7),script::Status::HostYield,"slice ends exactly after ordinary31");
    const auto trace=vm.last_normal_return();
    TrueReturn(trace,f,root.frame_id,held_callee.frame_id,0,f.call_pc,f.continuation_pc,27);
    Require(trace.before.clock_tick==107 && trace.before.deadline_tick==107
            && trace.before.relative_deadline23==0 && trace.after.frame_cells==1
            && trace.before.queued_callbacks==uint32_t(queue_callback)
            && trace.after.queued_callbacks==uint32_t(queue_callback),"trace is captured before queue drain");
    Expired(vm,callee_reference,"retained trace/current copies must not keep returned callee alive");
    Require(!root_reference.frame_owner.expired() && vm.peek_reference(root_reference).cp932==OwnerText(),
            "caller ownership survives while callee ownership expires");
    const auto live=vm.current_witness();
    uint64_t latest_frame_id=held_callee.frame_id;
    if (queue_callback) {
        Require(live.current_pc==f.callback_pc && live.code_entry==f.callback_pc
                && live.frame_id>held_callee.frame_id && live.event_envelope_present
                && trace.after.current_pc==f.continuation_pc && trace.after.frame_id==root.frame_id
                && live.queued_callbacks==0 && consumers==0 && callbacks==0,
                "live callback PC is distinct from captured restored caller before drain");
        latest_frame_id=live.frame_id;
        StatusIs(vm.run(5),script::Status::HostYield,"callback body before32");
        const auto held_callback=vm.current_witness();
        Require(callbacks==1 && !callback_reference.frame_owner.expired()
                && held_callback.frame_id==live.frame_id && held_callback.current_pc==f.callback_return_pc,
                "callback body retains its own actual frame");
        Passive(vm,accesses);
        StatusIs(vm.run(1),script::Status::HostYield,"finite callback32 restoration");
        const auto restored=vm.current_witness();
        Require(restored.frame_id==root.frame_id && restored.code_entry==0
                && restored.current_pc==f.continuation_pc && !restored.event_envelope_present
                && vm.callbacks_returned()==1 && restored.operand_cells==2
                && Same(restored.result,trace.after.result),"32 restores caller identity and exact result");
        Expired(vm,callback_reference,"retained callback witness does not keep returned callback alive");
        Require(held_callback.frame_id==live.frame_id,"value-only callback copy remains usable after retirement");
    } else Require(live.current_pc==f.continuation_pc && live.frame_id==root.frame_id
                   && live.deadline_tick==107 && !live.event_envelope_present,
                   "uninterrupted caller observes published return PC and deadline");
    Require(Same(trace,vm.last_normal_return()),"callback32 never masquerades as ordinary31 or overwrites trace");
    StatusIs(vm.run(100),script::Status::Complete,"caller consumes returned TRUE");
    Require(consumers==1 && vm.result().signed_integer()==91 && accesses==0,
            "witness extraction never invokes script access observer");
    enqueue=false; vm.clock_tick(200);
    StatusIs(vm.start({script::Value::string(OwnerText())}),script::Status::Running,"restart same Vm");
    const auto restarted=vm.current_witness();
    Require(restarted.frame_id>latest_frame_id && restarted.run_id>root.run_id
            && !vm.last_normal_return().valid && vm.last_normal_return().sequence==0,
            "restart creates a fresh monotonic frame ID and clears prior evidence");
    Expired(vm,root_reference,"restart releases prior root even while earlier witnesses survive");
    StatusIs(vm.run(100),script::Status::Yield,"restarted root deadline"); vm.clock_tick(207);
    StatusIs(vm.run(100),script::Status::Complete,"restarted ordinary call");
    Require(vm.last_normal_return().valid && vm.last_normal_return().sequence>trace.sequence
            && vm.last_normal_return().opcode_pc==f.return_pc
            && vm.last_normal_return().before.frame_id>restarted.frame_id && consumers==2,
            "start preserves exact target and future creations and trace sequences stay monotonic");
    Require(held_callee.frame_id<restarted.frame_id && trace.before.result.word==0x10000001,
            "retained evidence remains a stable value after restart");
}
inline void NestedCallbackRestoresSleepingRoot() {
    auto f=Build(); script::Value root_reference,callee_reference,callback_reference,context;
    script::VmStateWitness held_root,held_callback; script::NormalReturnWitness held_trace;
    uint32_t consumers=0,accesses=0;
    {
        script::Vm vm(f.program); vm.access_observer([&](const script::Value&,bool){++accesses;});
        vm.host_service([&](uint8_t id,const std::vector<script::Value>& arguments) {
            if (id==0x7E || id==0x7B || id==0x7C) {
                Require(arguments.size()==1 && arguments[0].kind==script::Value::Kind::Reference,
                        "nested fixture captures actual owned frame reference");
                if (id==0x7E) root_reference=arguments[0];
                if (id==0x7B) callee_reference=arguments[0];
                if (id==0x7C) callback_reference=arguments[0];
                return script::Value::integer(9);
            }
            Require(id==0x7F && arguments.size()==2 && vm.peek_reference(arguments[0]).cp932==OwnerText()
                    && arguments[1].signed_integer()==27,"sleeping root consumes original owned operands once");
            ++consumers; return script::Value::integer(91);
        });
        vm.trace_normal_return_at(f.return_pc); vm.clock_tick(100);
        StatusIs(vm.start_at(f.sleep_pc,{script::Value::string(OwnerText())}),script::Status::Running,"sleeping root start");
        StatusIs(vm.run(100),script::Status::Yield,"sleeping root44"); held_root=vm.current_witness();
        Require(held_root.deadline_tick==107 && held_root.relative_deadline23==7
                && held_root.current_pc==f.sleep_continuation_pc,"root sleeps with seven real remaining ticks");
        context=vm.context_value();
        Require(vm.post_callback(context,Code(f.nested_pc),script::Value::string(OwnerText()),script::Value::integer(73))==0,
                "callback actually interrupts sleeping root");
        const auto entered=vm.current_witness();
        Require(entered.frame_id>held_root.frame_id && entered.code_entry==f.nested_pc
                && entered.event_envelope_present && entered.context_registered && entered.call_depth==0,
                "callback has a fresh identity rather than borrowing sleeping root ID");
        for (unsigned steps=0;vm.pc()!=f.return_pc && steps<40;++steps)
            StatusIs(vm.step(),script::Status::Running,"nested callback ordinary call setup");
        const auto before=vm.current_witness();
        Require(before.current_pc==f.return_pc && before.frame_id>entered.frame_id,
                "nested actual20 creates a third distinct frame");
        Passive(vm,accesses);
        StatusIs(vm.run(1),script::Status::HostYield,"nested ordinary31"); held_trace=vm.last_normal_return();
        TrueReturn(held_trace,f,entered.frame_id,before.frame_id,f.nested_pc,f.nested_call_pc,f.nested_return_pc,41);
        Require(held_trace.before.event_envelope_present && held_trace.after.event_envelope_present
                && held_trace.before.context_word==held_trace.after.context_word
                && held_trace.after.frame_cells==3,"normal31 preserves surrounding native event envelope");
        Expired(vm,callee_reference,"nested callee expires with before/after witnesses retained");
        held_callback=vm.current_witness(); vm.clock_tick(105);
        StatusIs(vm.run(1),script::Status::HostYield,"callback32 restores sleeping root");
        const auto restored=vm.current_witness();
        Require(restored.frame_id==held_root.frame_id && restored.code_entry==f.sleep_pc
                && restored.current_pc==held_root.current_pc && restored.clock_tick==105
                && restored.deadline_tick==112 && restored.relative_deadline23==7
                && restored.operand_cells==held_root.operand_cells && restored.frame_cells==held_root.frame_cells
                && Same(restored.result,held_root.result) && Same(restored.operands[0],held_root.operands[0])
                && Same(restored.operands[1],held_root.operands[1]) && !restored.event_envelope_present
                && vm.callbacks_returned()==1 && consumers==0,
                "32 restores root identity, result, operands and seven remaining ticks at clock105");
        Expired(vm,callback_reference,"callback frame expires with its diagnostic snapshot retained");
        Require(Same(held_trace,vm.last_normal_return()),"callback32 cannot replace nested normal31 record");
        vm.clock_tick(111); const auto executed=vm.executed();
        StatusIs(vm.run(100),script::Status::Yield,"restored deadline pending");
        Require(vm.executed()==executed && consumers==0,"root cannot execute before preserved deadline");
        vm.clock_tick(112); StatusIs(vm.run(100),script::Status::Complete,"restored deadline due");
        Require(consumers==1 && accesses==0,"native continuation executes once without diagnostic reads");
    }
    Require(root_reference.frame_owner.expired() && context.context_owner.expired()
            && held_trace.valid && held_root.frame_id>0 && held_callback.frame_id>held_root.frame_id,
            "retained fixed evidence holds neither root frame nor context after Vm destruction");
}
inline void ExpiredQueuedReferenceIsNotRescued() {
    auto f=Build(); script::Vm vm(f.program); script::Value retired; uint32_t consumers=0;
    vm.host_service([&](uint8_t id,const std::vector<script::Value>& arguments) {
        if (id==0x7B) {
            Require(arguments.size()==1,"capture reference for stale queued callback"); retired=arguments[0];
            Require(vm.post_callback(vm.context_value(),Code(f.callback_pc),retired,script::Value::integer(73))==0,
                    "still-live callee reference is admitted before actual return");
        } else if (id!=0x7A) ++consumers;
        return script::Value::integer(9);
    });
    vm.trace_normal_return_at(f.return_pc); vm.clock_tick(100);
    StatusIs(vm.start({script::Value::string(OwnerText())}),script::Status::Running,"stale queue start");
    StatusIs(vm.run(100),script::Status::Yield,"stale queue deadline"); vm.clock_tick(107);
    StatusIs(vm.step(),script::Status::Running,"stale queue argument");
    StatusIs(vm.step(),script::Status::Running,"stale queue call");
    StatusIs(vm.step(),script::Status::Running,"stale queue frame");
    const auto held=vm.current_witness(); const auto error=vm.run(7); const auto trace=vm.last_normal_return();
    Require(error.status==script::Status::Stopped && error.diagnostic.reason.find("frame reference expired before dispatch")!=std::string::npos
            && trace.valid && trace.after.current_pc==f.continuation_pc && trace.after.frame_id<held.frame_id
            && vm.callbacks_returned()==0 && consumers==0,
            "captured normal31 survives subsequent honest stale-reference dispatch failure");
    Expired(vm,retired,"passive return trace cannot rescue queued reference to returned frame");
    Require(trace.before.result.word==0x10000001,"before trace remains usable after failed queue drain");
}
inline void FixedCapacityReportsTotalsAndIsPassive() {
    std::vector<uint8_t> code{0x30,20};
    for (uint8_t i=0;i<20;++i) Append(code,{0x80,i});
    code.push_back(0x38); auto program=Program(std::move(code)); script::Vm vm(program);
    std::vector<script::Value> arguments;
    for (int32_t i=0;i<20;++i) arguments.push_back(script::Value::integer(i+40));
    uint32_t accesses=0; vm.access_observer([&](const script::Value&,bool){++accesses;});
    StatusIs(vm.start(arguments),script::Status::Running,"bounded evidence start");
    StatusIs(vm.run(21),script::Status::HostYield,"twenty actual operands");
    const auto state=vm.current_witness(); Identity(state,program);
    Require(state.frame_cells==20 && state.operand_cells==20 && state.frame_captured==16
            && state.operands_captured==16,"fixed capacity exposes exact totals and explicit captured counts");
    for (uint32_t i=0;i<16;++i) {
        Scalar(state.frame_values[i],0x20000000u|uint32_t(i+40),"captured frame prefix preserves order");
        Scalar(state.operands[i],0x20000000u|i,"captured operand prefix preserves order");
    }
    Passive(vm,accesses);
    Require(vm.stack().size()==20 && !vm.current_witness().context_registered && accesses==0,
            "bounded projection neither trims runtime operands nor creates a context");
}
inline void NestedOrdinaryCallsRestoreTopCaller() {
    std::vector<uint8_t> code{0x30,1,0x80,17,0x90,0,0};
    const auto root_call=uint32_t(code.size()); Append(code,{0x20,1,0,0,0});
    const auto root_return=uint32_t(code.size()); code.push_back(0x38);
    const auto middle=uint32_t(code.size()); Put(code,root_call+2,middle,3);
    Append(code,{0x30,1,0x80,27,0x90,0,0});
    const auto middle_call=uint32_t(code.size()); Append(code,{0x20,1,0,0,0});
    const auto middle_return=uint32_t(code.size()); code.push_back(0x31);
    const auto leaf=uint32_t(code.size()); Put(code,middle_call+2,leaf,3);
    Append(code,{0x30,1,0x80,55,0x87,0x70});
    const auto leaf_return=uint32_t(code.size()); code.push_back(0x31);
    auto program=Program(std::move(code)); script::Vm vm(program); vm.trace_normal_return_at(leaf_return);
    StatusIs(vm.start({script::Value::string(OwnerText())}),script::Status::Running,"nested ordinary start");
    const auto root=vm.current_witness();
    for (unsigned steps=0;vm.pc()!=leaf_return && steps<30;++steps)
        StatusIs(vm.step(),script::Status::Running,"nested ordinary setup");
    const auto before=vm.current_witness();
    Require(before.current_pc==leaf_return && before.call_depth==2 && before.code_entry==leaf
            && before.caller_entry==middle && before.caller_call_pc==middle_call
            && before.caller_return_pc==middle_return && before.caller_stack_base==2
            && before.caller_frame_id>root.frame_id && before.frame_id>before.caller_frame_id,
            "leaf identifies its actual immediate caller with a second saved caller below it");
    StatusIs(vm.step(),script::Status::Running,"leaf ordinary31"); const auto trace=vm.last_normal_return();
    Identity(trace.before,program); Identity(trace.after,program);
    Require(trace.valid && trace.before.call_depth==2 && trace.after.call_depth==1
            && trace.after.frame_id==before.caller_frame_id && trace.after.code_entry==middle
            && trace.after.current_pc==middle_return && trace.after.caller_entry==0
            && trace.after.caller_frame_id==root.frame_id && trace.after.caller_call_pc==root_call
            && trace.after.caller_return_pc==root_return && trace.after.caller_stack_base==1
            && trace.before.operand_cells==3 && trace.after.operand_cells==2,
            "31 restores both middle frame identity and its actual top caller metadata");
    Scalar(trace.after.operands[0],0x20000011,"grandparent operand17 survives leaf31");
    Scalar(trace.after.operands[1],0x2000001B,"parent operand27 survives leaf31");
    Scalar(trace.after.result,0x10000001,"nested ordinary return retains exact TRUE");
    StatusIs(vm.run(10),script::Status::Complete,"unarmed middle31 and root completion");
    Require(Same(trace,vm.last_normal_return()) && vm.current_witness().frame_id==root.frame_id
            && vm.stack().size()==1 && vm.stack()[0].signed_integer()==17,
            "later unmatched31 restores root without replacing the exact leaf return evidence");
}
inline void ErrorsAndOtherTargetsDoNotFabricateNormalReturn() {
    auto invalid=Program({0x30,0,0x00}); script::Vm first_error(invalid);
    first_error.trace_normal_return_at(2);
    StatusIs(first_error.start(),script::Status::Running,"first-error start");
    const auto error=first_error.run(10); const auto stopped=first_error.current_witness();
    Require(error.status==script::Status::Stopped && error.pc==2 && error.opcode==0x00
            && stopped.stopped && !first_error.last_normal_return().valid
            && first_error.last_normal_return().sequence==0,"first unsupported opcode fabricates no normal31 evidence");
    Require(first_error.step().diagnostic.reason==error.diagnostic.reason
            && Same(stopped,first_error.current_witness()),"first error remains stable under later passive inspection");
    StatusIs(first_error.start_at(2,{}),script::Status::Stopped,"failed start at non-prologue");
    Require(!first_error.last_normal_return().valid,"failed start fabricates no return trace");

    auto root_return=Program({0x30,0,0x31}); script::Vm root(root_return); root.trace_normal_return_at(2);
    StatusIs(root.start(),script::Status::Running,"root31 start");
    StatusIs(root.run(10),script::Status::Complete,"root31 completion without caller");
    Require(!root.last_normal_return().valid,"root completion without CallFrame is not an ordinary caller return");

    auto f=Build(); script::Vm wrong_target(f.program); wrong_target.trace_normal_return_at(f.callback_return_pc);
    wrong_target.host_service([](uint8_t,const std::vector<script::Value>&){return script::Value::integer(9);});
    wrong_target.clock_tick(100);
    StatusIs(wrong_target.start({script::Value::string(OwnerText())}),script::Status::Running,"wrong target start");
    StatusIs(wrong_target.run(100),script::Status::Yield,"wrong target deadline"); wrong_target.clock_tick(107);
    StatusIs(wrong_target.run(100),script::Status::Complete,"unarmed ordinary31 completes");
    Require(!wrong_target.last_normal_return().valid,"unmatched normal31 cannot fabricate evidence for target32");

    script::Vm only_callback(f.program); only_callback.trace_normal_return_at(f.callback_return_pc);
    only_callback.host_service([](uint8_t,const std::vector<script::Value>&){return script::Value::integer(9);});
    only_callback.clock_tick(100);
    StatusIs(only_callback.start_at(f.sleep_pc,{script::Value::string(OwnerText())}),
             script::Status::Running,"target32 sleeping root start");
    StatusIs(only_callback.run(100),script::Status::Yield,"target32 root deadline");
    Require(only_callback.post_callback(only_callback.context_value(),Code(f.callback_pc),
                script::Value::string(OwnerText()),script::Value::integer(73))==0,"target32 callback admission");
    StatusIs(only_callback.run(100),script::Status::Yield,"actual target32 restores sleeping root");
    Require(only_callback.callbacks_returned()==1 && !only_callback.last_normal_return().valid
            && only_callback.last_normal_return().sequence==0,
            "even an exact armed callback32 cannot create a fabricated ordinary31 witness");

    // Actual failed31 guard: the unresolved active-block return remains a cut.
    std::vector<uint8_t> code{0x30,0,0x21,7,0,0,0x38,0x30,0,0x18,0,0,0};
    const auto return_pc=uint32_t(code.size()); code.push_back(0x31);
    Put(code,10,uint32_t(code.size()),3); Append(code,{0x1A,0x31});
    auto blocked=Program(std::move(code)); script::Vm guard(blocked); guard.trace_normal_return_at(return_pc);
    StatusIs(guard.start(),script::Status::Running,"guarded31 start"); const auto cut=guard.run(100);
    Require(cut.status==script::Status::Stopped && cut.pc==return_pc && cut.opcode==0x31
            && cut.diagnostic.reason.find("active block")!=std::string::npos
            && guard.active_control_blocks()==1 && !guard.last_normal_return().valid
            && guard.last_normal_return().sequence==0,"failed31 guard cannot fabricate a successful normal return");
}
} // namespace dokodemo::return_witness_regression_detail

inline void GameReturnWitnessRegressionChecks() {
    dokodemo::return_witness_regression_detail::NormalCallTrueAndQueueBoundary(false);
    dokodemo::return_witness_regression_detail::NormalCallTrueAndQueueBoundary(true);
    dokodemo::return_witness_regression_detail::NestedCallbackRestoresSleepingRoot();
    dokodemo::return_witness_regression_detail::ExpiredQueuedReferenceIsNotRescued();
    dokodemo::return_witness_regression_detail::FixedCapacityReportsTotalsAndIsPassive();
    dokodemo::return_witness_regression_detail::NestedOrdinaryCallsRestoreTopCaller();
    dokodemo::return_witness_regression_detail::ErrorsAndOtherTargetsDoNotFabricateNormalReturn();
}
