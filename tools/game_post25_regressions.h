#pragma once
#include "../src/script_vm.h"
#include "recovered/game_actor_runtime_native.h"
#include "recovered/game_input_native.h"
#include "game_return_witness_regressions.h" // reuse value-only evidence comparison, not its test runner
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Synthetic linked API checks only. Opcode48 extracts an owned Event; the
// test host admits the next run through GameMessageInput. No VM until-ACK
// barrier, original bytecode lineage, source469 clear or event0 rearm claim.
namespace dokodemo::post25_regression_detail {
inline void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("post25 regression: " + message);
}
inline void StatusIs(const script::Event& event, script::Status expected, const char* where) {
    Require(event.status==expected,std::string(where)+": "+event.diagnostic.text());
}
inline void Put(std::vector<uint8_t>& bytes, size_t at, uint32_t value, unsigned width=4) {
    for (unsigned i=0;i<width;++i) bytes.at(at+i)=uint8_t(value>>(8*i));
}
inline void Append(std::vector<uint8_t>& code, std::initializer_list<uint8_t> bytes) {
    code.insert(code.end(),bytes.begin(),bytes.end());
}
inline std::string OwnerText() { return std::string(165,'p')+"\x83\x67\x83\x8D"; }
inline std::string SavedResult() { return std::string(175,'r')+"\x83\x8D"; }
inline void Message(std::vector<uint8_t>& code, const char* prefix) {
    code.push_back(0x48);
    while (*prefix) code.push_back(uint8_t(*prefix++));
    Append(code,{0x14,0,0,0});
}
struct Fixture {
    script::Program program;
    bool historical_prefix=false;
    uint32_t call_pc=0,caller_pc=0,callee_pc=0,message_pc=0,wait_pc=0;
    uint32_t after_wait_pc=0,after_deadline_pc=0,return_pc=0,unknown_pc=0;
    uint32_t second_callee_pc=0,second_return_pc=0;
};
inline Fixture Build(bool historical_prefix=false) {
    // Two real caller operands remain below the single relocated argument.
    std::vector<uint8_t> code{0x30,1,0x91,0,0,0x8B,0x7A,1};
    Fixture f; f.historical_prefix=historical_prefix;
    size_t old_first_call=0,old_second_call=0;
    if (historical_prefix) {
        Append(code,{0x90,0,0}); old_first_call=code.size();
        Append(code,{0x20,1,0,0,0,0x89,0x8B,0x7F,1,0x90,0,0});
        old_second_call=code.size(); Append(code,{0x20,1,0,0,0,0x89,0x8B,0x7F,1});
    }
    Append(code,{0x91,0,0,0x80,57,0x90,0,0});
    f.call_pc=uint32_t(code.size()); Append(code,{0x20,1,0,0,0});
    f.caller_pc=uint32_t(code.size()); Append(code,{0x89,0x8B,0x7D,3});
    Message(code,"successor:");
    f.unknown_pc=uint32_t(code.size()); code.push_back(0x00);
    f.callee_pc=uint32_t(code.size()); Put(code,f.call_pc+2,f.callee_pc,3);
    if (historical_prefix) Put(code,old_first_call+2,f.callee_pc,3);
    Append(code,{0x30,1,0x91,0,0,0x8B,0x7B,1,0x80,55});
    f.message_pc=uint32_t(code.size()); Message(code,"callee:");
    f.wait_pc=uint32_t(code.size()); Append(code,{0x8B,0x23,0});
    f.after_wait_pc=uint32_t(code.size()); Append(code,{0x80,30,0x44});
    f.after_deadline_pc=uint32_t(code.size()); Append(code,{0x80,41,0x70});
    f.return_pc=uint32_t(code.size()); code.push_back(0x31);
    if (historical_prefix) {
        f.second_callee_pc=uint32_t(code.size());
        Put(code,old_second_call+2,f.second_callee_pc,3);
        Append(code,{0x30,1,0x91,0,0,0x8B,0x7C,1,0x80,73,0x70});
        f.second_return_pc=uint32_t(code.size()); code.push_back(0x31);
    }
    std::vector<uint8_t> bytes(28+code.size());
    Put(bytes,0,0x0043534B); Put(bytes,4,0x20030204); Put(bytes,12,uint32_t(code.size()));
    std::copy(code.begin(),code.end(),bytes.begin()+28);
    script::Diagnostic error;
    const bool loaded=f.program.load_test_fixture(std::move(bytes),error);
    Require(loaded,"load linked synthetic fixture: "+error.text());
    return f;
}
inline bool Same(const script::Value& a, const script::Value& b) {
    return a.kind==b.kind && a.word==b.word && a.table==b.table && a.index==b.index && a.cp932==b.cp932
        && !a.frame_owner.owner_before(b.frame_owner) && !b.frame_owner.owner_before(a.frame_owner)
        && !a.context_owner.owner_before(b.context_owner) && !b.context_owner.owner_before(a.context_owner);
}
struct Continuation {
    uint32_t pc=0,calls=0,blocks=0,scope=0,writes=0,timestamps=0;
    uint64_t deadline=0,executed=0,run=0,frame=0;
    bool stopped=false,waiting=false;
    script::Value result,frame0;
    std::vector<script::Value> stack;
};
inline Continuation Capture(const script::Vm& vm) {
    const auto witness=vm.current_witness();
    return {vm.pc(),vm.call_depth(),vm.active_control_blocks(),vm.event_scope_depth(),
            vm.deadline_writes(),vm.timestamp_writes(),vm.deadline_tick(),vm.executed(),
            witness.run_id,witness.frame_id,vm.stopped(),vm.waiting_for_event(),
            vm.result(),vm.read_slot(script::Table::Frame,0),vm.stack()};
}
inline void Unchanged(const script::Vm& vm, const Continuation& before, const char* where) {
    const auto after=Capture(vm);
    Require(after.pc==before.pc && after.calls==before.calls && after.blocks==before.blocks
            && after.scope==before.scope && after.writes==before.writes && after.timestamps==before.timestamps
            && after.deadline==before.deadline && after.executed==before.executed
            && after.run==before.run && after.frame==before.frame && after.stopped==before.stopped
            && after.waiting==before.waiting && Same(after.result,before.result)
            && Same(after.frame0,before.frame0) && after.stack.size()==before.stack.size(),where);
    for (size_t i=0;i<before.stack.size();++i) Require(Same(after.stack[i],before.stack[i]),where);
}
inline recovered::GameInputFrame Frame(uint32_t buttons, bool focused, uint64_t sequence,
                                       uint64_t context=101, uint64_t owner=201) {
    recovered::GameInputFrame frame;
    frame.buttons=buttons; frame.focused=focused; frame.sequence=sequence;
    frame.message_context=context; frame.message_owner_generation=owner;
    return frame;
}
inline void Rejected(recovered::GameMessageInput& input, script::Vm& vm,
                     const recovered::GameInputFrame& frame, const char* where) {
    const auto before=Capture(vm);
    Require(!input.Observe(frame),where);
    Unchanged(vm,before,"denied host input cannot mutate or run the suspended continuation");
}
inline void FreshAck(recovered::GameMessageInput& input, script::Vm& vm) {
    const auto before=Capture(vm); input.Begin(101,201);
    Require(!input.Observe(Frame(0,true,1)) && input.Armed()
            && input.Observe(Frame(recovered::Circle,true,2)),"host admits a focused release then fresh Circle");
    Unchanged(vm,before,"accepted host ACK alone neither resumes nor completes the VM");
}
struct HostState {
    script::Value root_reference,callee_reference,second_reference,historical_reference;
    uint32_t waits=0,actions=0,historical_actions=0;
    uint64_t fresh_frame=0,fresh_run=0;
    script::NormalReturnWitness first_return,historical_first,historical_second;
};
inline void Bind(script::Vm& vm, HostState& host, const Fixture& f) {
    vm.host_service([&vm,&host,&f](uint8_t id,const std::vector<script::Value>& arguments) {
        if (id==0x7A || id==0x7B || id==0x7C) {
            Require(arguments.size()==1 && arguments[0].kind==script::Value::Kind::Reference
                    && vm.peek_reference(arguments[0]).cp932==OwnerText(),"host captures actual owned frame reference");
            if (id==0x7A) host.root_reference=arguments[0];
            if (id==0x7B) {
                host.callee_reference=arguments[0];
                if (f.historical_prefix && host.historical_actions==0) host.historical_reference=arguments[0];
            }
            if (id==0x7C) {
                host.second_reference=arguments[0];
                Require(f.historical_prefix && host.historical_actions==1
                        && host.historical_reference.frame_owner.expired()
                        && return_witness_regression_detail::Same(host.historical_first,vm.last_normal_return(0))
                        && !vm.last_normal_return(1).valid,
                        "historical second target starts after the earlier same-target trace exists");
            }
            return script::Value::string(SavedResult());
        }
        if (id==0x23) {
            Require(arguments.empty() && host.waits==uint32_t(f.historical_prefix && host.historical_actions==2),
                    "actual builtin23 has argc0 and executes once per admitted invocation");
            ++host.waits; vm.wait_for_event();
            script::Value nil; nil.kind=script::Value::Kind::Scalar; return nil;
        }
        if (id==0x7F) {
            Require(f.historical_prefix && host.historical_actions<2 && arguments.size()==1
                    && arguments[0].signed_integer()==(host.historical_actions==0 ? 41 : 73)
                    && vm.call_depth()==0 && host.actions==0,
                    "historical caller consumes the real earlier result without treating it as the current return");
            if (host.historical_actions==0) {
                host.historical_first=vm.last_normal_return(0);
                Require(host.historical_first.valid && host.historical_first.opcode_pc==f.return_pc
                        && host.historical_reference.frame_owner.expired(),"historical same-target return actually retired");
            } else {
                host.historical_second=vm.last_normal_return(1);
                Require(host.historical_second.valid && host.historical_second.opcode_pc==f.second_return_pc
                        && host.historical_second.sequence>host.historical_first.sequence
                        && host.second_reference.frame_owner.expired()
                        && return_witness_regression_detail::Same(host.historical_first,vm.last_normal_return(0)),
                        "different historical target retains the earlier trace with a later global sequence");
            }
            ++host.historical_actions; return script::Value::integer(9);
        }
        if (id==0x7D) {
            Require(arguments.size()==3 && Same(arguments[0],host.root_reference)
                    && vm.peek_reference(arguments[0]).cp932==OwnerText()
                    && arguments[1].signed_integer()==57 && arguments[2].signed_integer()==41
                    && host.waits==1+uint32_t(f.historical_prefix) && host.actions==0 && vm.call_depth()==0
                    && host.callee_reference.frame_owner.expired() && !vm.deadline_waiting(),
                    "caller action consumes preserved operands and callee result once after its real deadline");
            host.first_return=vm.last_normal_return(0);
            Require(host.first_return.valid && host.first_return.opcode_pc==f.return_pc
                    && host.first_return.before.current_pc==f.return_pc
                    && host.first_return.after.current_pc==f.caller_pc
                    && host.first_return.before.call_depth==1 && host.first_return.after.call_depth==0
                    && host.first_return.before.result.kind==script::Value::Kind::Scalar
                    && host.first_return.after.result.kind==script::Value::Kind::Scalar
                    && host.first_return.before.result.word==0x20000029
                    && host.first_return.after.result.word==0x20000029
                    && host.first_return.before.deadline_tick==vm.deadline_tick()
                    && host.first_return.after.deadline_tick==vm.deadline_tick(),
                    "first action follows the exact ordinary31, retaining its result and deadline evidence");
            if (f.historical_prefix) {
                Require(host.historical_actions==2 && host.first_return.sequence>host.historical_first.sequence
                        && host.first_return.sequence>host.historical_second.sequence
                        && host.first_return.before.frame_id==host.fresh_frame
                        && host.first_return.before.run_id==host.fresh_run
                        && host.first_return.before.frame_id!=host.historical_first.before.frame_id
                        && return_witness_regression_detail::Same(host.historical_second,vm.last_normal_return(1)),
                        "current action requires fresh31 from the captured message frame,not an already-valid historical hit");
            }
            ++host.actions; return script::Value::integer(99);
        }
        throw std::runtime_error("post25 regression: unexpected synthetic host service "+std::to_string(id));
    });
}
inline script::Event Start(script::Vm& vm, const Fixture& f, bool clear_signal, bool second_target=false) {
    vm.trace_normal_return_at(f.return_pc,0);
    if (second_target) vm.trace_normal_return_at(f.second_return_pc,1);
    std::vector<script::Value> arguments{script::Value::string(OwnerText())};
    StatusIs(vm.start(arguments),script::Status::Running,"same-VM linked call start");
    arguments[0].cp932="changed caller temporary";
    if (clear_signal) vm.clear_event_signal();
    const auto first=vm.run(100);
    StatusIs(first,script::Status::Message,"callee owns extracted message");
    Require(first.pc==f.message_pc && first.next_pc==f.wait_pc && vm.pc()==f.wait_pc
            && first.message_cp932=="callee:"+OwnerText() && vm.call_depth()==1
            && vm.result().cp932==SavedResult() && vm.read_slot(script::Table::Frame,0).cp932==OwnerText()
            && vm.stack().size()==(f.historical_prefix ? 1u : 3u)
            && (f.historical_prefix || vm.stack()[1].signed_integer()==57)
            && vm.stack().back().signed_integer()==55 && vm.deadline_writes()==0
            && !vm.last_normal_return(0).valid,"message extraction precedes wait,deadline,return and caller action");
    return first;
}
inline void AtDeadline(script::Vm& vm, HostState& host, const Fixture& f, uint64_t deadline) {
    const bool historical=f.historical_prefix && host.historical_actions==0;
    const auto visits=1+uint32_t(f.historical_prefix && host.historical_actions==2);
    Require(vm.pc()==f.after_deadline_pc && vm.deadline_tick()==deadline && vm.deadline_waiting()
            && vm.deadline_writes()==visits && vm.timestamp_writes()==0 && host.waits==visits && host.actions==0
            && vm.result().kind==script::Value::Kind::Scalar && vm.result().word==0
            && vm.call_depth()==1 && vm.read_slot(script::Table::Frame,0).cp932==OwnerText()
            && vm.stack().size()==(historical ? 1u : 3u)
            && (historical || vm.stack()[1].signed_integer()==57)
            && vm.stack().back().signed_integer()==55 && !host.callee_reference.frame_owner.expired(),
            "real44 owns all30 ticks while preserving nil result,callee frame and caller operands");
    const auto pending=Capture(vm); vm.clock_tick(deadline-1);
    StatusIs(vm.run(100),script::Status::Yield,"one tick before deadline");
    Unchanged(vm,pending,"pending deadline executes nothing and preserves the exact continuation");
    Require(host.waits==visits && host.actions==0,
            "pending deadline cannot reissue wait or caller action");
}
inline script::Event Successor(script::Vm& vm, HostState& host, const Fixture& f) {
    vm.clock_tick(vm.deadline_tick());
    const auto next=vm.run(100); StatusIs(next,script::Status::Message,"actual caller successor");
    Require(next.message_cp932=="successor:"+OwnerText() && vm.pc()==f.unknown_pc && !vm.stopped()
            && host.actions==1 && host.waits==1+uint32_t(f.historical_prefix) && vm.result().signed_integer()==99
            && vm.call_depth()==0 && vm.stack().empty() && vm.display_count()==2+uint32_t(f.historical_prefix)
            && vm.callbacks_posted()==0 && vm.callbacks_returned()==0
            && vm.peek_reference(host.root_reference).cp932==OwnerText()
            && return_witness_regression_detail::Same(host.first_return,vm.last_normal_return(0)),
            "caller action reaches owned successor once without replaying message or inventing callback completion");
    return next;
}

inline void ClearedEventWaitDeadlineReturnAndUnknownSuccessor() {
    auto f=Build(); script::Vm vm(f.program); HostState host; Bind(vm,host,f); vm.clock_tick(100);
    const auto first=Start(vm,f,true); recovered::GameMessageInput input; input.Begin(101,201);
    Rejected(input,vm,Frame(recovered::Circle,true,1),"held Circle at message creation cannot acknowledge");
    Rejected(input,vm,Frame(0,true,2,100),"stale message release cannot arm");
    Rejected(input,vm,Frame(0,true,3,101,200),"stale owner release cannot arm");
    Rejected(input,vm,Frame(0,false,4),"unfocused release cannot arm");
    Rejected(input,vm,Frame(recovered::Circle,true,5),"held refocus still needs focused release");
    Rejected(input,vm,Frame(0,true,6),"fresh release arms without ACK");
    Require(input.Armed(),"message owns a focused release");
    Rejected(input,vm,Frame(recovered::Circle,true,6),"duplicate sample cannot acknowledge");
    const auto before_ack=Capture(vm);
    Require(input.Observe(Frame(recovered::Circle,true,7)),"fresh message ACK admitted once");
    Unchanged(vm,before_ack,"message ACK does not itself signal or run the VM");
    StatusIs(vm.run(100),script::Status::Waiting,"cleared builtin23 waits at its true postcall PC");
    Require(vm.pc()==f.after_wait_pc && vm.waiting_for_event() && host.waits==1 && host.actions==0
            && vm.deadline_writes()==0 && vm.deadline_tick()==0 && vm.result().word==0,
            "host ACK cannot be mistaken for the distinct VM event producer or a deadline write");
    const auto waiting=Capture(vm);
    Rejected(input,vm,Frame(recovered::Circle,true,8),"consumed host edge cannot acknowledge twice");
    vm.clock_tick(160); StatusIs(vm.run(100),script::Status::Waiting,"time alone cannot produce the missing event");
    Unchanged(vm,waiting,"event wait preserves caller,callee,result and operands without executing");
    vm.signal_event(); StatusIs(vm.run(100),script::Status::Yield,"event producer releases real30-tick deadline");
    AtDeadline(vm,host,f,190); const auto successor=Successor(vm,host,f);
    Require(first.message_cp932=="callee:"+OwnerText() && successor.message_cp932=="successor:"+OwnerText(),
            "both owned Events survive normal frame retirement");

    // The successor has a distinct published host context. It must earn its
    // own edge before the host attempts the deliberately unsupported byte00.
    input.Begin(102,201);
    Rejected(input,vm,Frame(0,true,9),"old context release cannot arm successor");
    Rejected(input,vm,Frame(recovered::Circle,true,10),"old queued press cannot consume successor");
    Rejected(input,vm,Frame(0,true,11,102,200),"retired owner cannot arm successor");
    Rejected(input,vm,Frame(recovered::Circle,true,12,102),"held successor Circle cannot acknowledge");
    Rejected(input,vm,Frame(0,false,13,102),"unfocused successor release cannot arm");
    Rejected(input,vm,Frame(recovered::Circle,true,14,102),"refocus still requires successor release");
    Rejected(input,vm,Frame(0,true,15,102),"successor release arms without ACK");
    Require(input.Armed(),"successor owns a separate release");
    Rejected(input,vm,Frame(recovered::Circle,true,15,102),"duplicate successor sequence cannot acknowledge");
    const auto before_cut=Capture(vm);
    Require(input.Observe(Frame(recovered::Circle,true,16,102)),"successor admits its distinct fresh ACK");
    Unchanged(vm,before_cut,"successor ACK alone preserves actual returned caller state");
    const auto cut=vm.run(100);
    Require(cut.status==script::Status::Stopped && vm.stopped() && cut.pc==f.unknown_pc
            && cut.opcode==0x00 && cut.file_offset==f.unknown_pc+28
            && cut.diagnostic.pc==cut.pc && cut.diagnostic.file_offset==cut.file_offset
            && cut.diagnostic.opcode==0x00 && cut.diagnostic.reason=="Unsupported opcode semantics"
            && vm.executed()==before_cut.executed && host.actions==1,
            "first unsupported successor stays an exact error rather than fabricated completion");
    const auto stopped=Capture(vm);
    const auto repeated=vm.run(100);
    Require(repeated.status==script::Status::Stopped && repeated.pc==cut.pc && repeated.opcode==cut.opcode
            && repeated.file_offset==cut.file_offset && repeated.diagnostic.reason==cut.diagnostic.reason
            && repeated.diagnostic.pc==cut.diagnostic.pc && repeated.diagnostic.opcode==cut.diagnostic.opcode
            && repeated.diagnostic.file_offset==cut.diagnostic.file_offset,
            "later run retains the original unsupported diagnostic");
    Unchanged(vm,stopped,"first error prevents every later instruction or consumer");
    Require(return_witness_regression_detail::Same(host.first_return,vm.last_normal_return(0))
            && host.callee_reference.frame_owner.expired(),"first error cannot rewrite a prior real return or revive its frame");
}
inline void SignalledWaitStillNeedsThirtyTicksAfterHostResume() {
    auto f=Build(); script::Vm vm(f.program); HostState host; Bind(vm,host,f); vm.clock_tick(200);
    (void)Start(vm,f,false); recovered::GameMessageInput input; FreshAck(input,vm);
    vm.clock_tick(260);
    StatusIs(vm.run(100),script::Status::Yield,"already-signalled builtin23 proceeds to positive44");
    Require(!vm.waiting_for_event(),"cold event signal skips only the event wait");
    AtDeadline(vm,host,f,290); (void)Successor(vm,host,f);
    Require(host.actions==1 && vm.deadline_tick()==290 && vm.deadline_writes()==1,
            "deadline starts at actual host continuation,not earlier message extraction");
}
inline void HistoricalSameTargetNeedsFreshFrameAndRetainsOtherTrace() {
    auto f=Build(true); script::Vm vm(f.program); HostState host; Bind(vm,host,f); vm.clock_tick(400);
    (void)Start(vm,f,true,true); recovered::GameMessageInput input; FreshAck(input,vm);
    StatusIs(vm.run(100),script::Status::Waiting,"historical first target waits for actual event");
    vm.signal_event(); StatusIs(vm.run(100),script::Status::Yield,"historical first target owns deadline");
    AtDeadline(vm,host,f,430); vm.clock_tick(430);
    const auto current_message=vm.run(100);
    StatusIs(current_message,script::Status::Message,"same target's later actual message boundary");
    const auto old_same=vm.last_normal_return(0),other=vm.last_normal_return(1);
    const auto boundary=vm.current_witness(); host.fresh_frame=boundary.frame_id; host.fresh_run=boundary.run_id;
    Require(host.historical_actions==2 && host.actions==0 && host.waits==1
            && current_message.pc==f.message_pc && vm.pc()==f.wait_pc
            && old_same.valid && other.valid && old_same.sequence<other.sequence
            && old_same.opcode_pc==f.return_pc && other.opcode_pc==f.second_return_pc
            && boundary.code_entry==f.callee_pc && boundary.frame_id!=old_same.before.frame_id
            && boundary.run_id==old_same.before.run_id && boundary.run_id==other.before.run_id
            && boundary.frame_id!=other.before.frame_id
            && host.historical_reference.frame_owner.expired() && host.second_reference.frame_owner.expired()
            && !host.callee_reference.frame_owner.expired(),
            "valid historical same-target evidence is present before the current invocation has returned");
    vm.clear_event_signal(); input.Begin(102,201);
    Rejected(input,vm,Frame(recovered::Circle,true,3),"historical message press cannot consume current invocation");
    Rejected(input,vm,Frame(recovered::Circle,true,4,102),"held current press cannot manufacture fresh return evidence");
    Rejected(input,vm,Frame(0,true,5,102),"current invocation owns its release before ACK");
    Require(input.Armed() && input.Observe(Frame(recovered::Circle,true,6,102)),"current invocation accepts its fresh edge");
    StatusIs(vm.run(100),script::Status::Waiting,"current invocation still requires its own event producer");
    Require(return_witness_regression_detail::Same(old_same,vm.last_normal_return(0))
            && return_witness_regression_detail::Same(other,vm.last_normal_return(1)) && host.actions==0,
            "historical valid bit cannot count as the current return while its callee waits");
    vm.clock_tick(500); vm.signal_event();
    StatusIs(vm.run(100),script::Status::Yield,"current invocation owns a new positive deadline");
    AtDeadline(vm,host,f,530);
    Require(return_witness_regression_detail::Same(old_same,vm.last_normal_return(0))
            && return_witness_regression_detail::Same(other,vm.last_normal_return(1)) && host.actions==0,
            "deadline cannot make the historical hit fresh before the current actual31");
    (void)Successor(vm,host,f);
    const auto fresh=vm.last_normal_return(0);
    Require(fresh.valid && fresh.sequence>old_same.sequence && fresh.sequence>other.sequence
            && fresh.opcode_pc==old_same.opcode_pc && fresh.before.frame_id==boundary.frame_id
            && fresh.before.run_id==boundary.run_id && fresh.before.current_pc==f.return_pc
            && fresh.before.frame_id!=old_same.before.frame_id
            && fresh.after.frame_id==other.after.frame_id
            && return_witness_regression_detail::Same(other,vm.last_normal_return(1))
            && return_witness_regression_detail::Same(fresh,host.first_return)
            && host.callee_reference.frame_owner.expired(),
            "later fresh same-target31 replaces only its slot and retains the other target's full historical trace");
    return_witness_regression_detail::Expired(vm,host.historical_reference,"historical same-target frame is retired");
    return_witness_regression_detail::Expired(vm,host.second_reference,"other historical target frame is retired");
    return_witness_regression_detail::Expired(vm,host.callee_reference,"fresh same-target frame is retired");
    Require(return_witness_regression_detail::Same(fresh,vm.last_normal_return(0))
            && return_witness_regression_detail::Same(other,vm.last_normal_return(1)),
            "retired-reference refusal cannot mutate either independently retained return trace");
}
inline void ActorLoop8021NestedFiniteAndInfiniteReadiness() {
    using recovered::AdvanceActorLoop8021;
    using recovered::GameActorLoopFrame;
    // These are pure owning-loop transitions. The caller supplies the actual
    // motion-ready gate; this helper has no command-complete/25E/VM54 API.
    // set_ready258 is an action request: false is not a clear of prior258.
    std::vector<GameActorLoopFrame> nested{{0x140,1},{0x240,1}};
    const auto inner=AdvanceActorLoop8021(nested,0x2A0);
    Require(inner.pc==0x2A4 && inner.depth==1 && !inner.set_ready258 && !inner.repeated
            && nested.size()==1 && nested[0].start==0x140 && nested[0].remaining==1,
            "inner count1 pop advances four bytes and cannot publish root readiness");
    const auto root=AdvanceActorLoop8021(nested,0x1A0);
    Require(root.pc==0x1A4 && root.depth==0 && root.set_ready258 && !root.repeated && nested.empty(),
            "final count1 root pop publishes258 while advancing four bytes");

    std::vector<GameActorLoopFrame> finite{{0x340,3}};
    const auto first=AdvanceActorLoop8021(finite,0x3A0);
    Require(first.pc==0x340 && first.depth==1 && !first.set_ready258 && first.repeated
            && finite.size()==1 && finite[0].start==0x340 && finite[0].remaining==2,
            "count3 repeats its own start and decrements exactly once without258");
    const auto second=AdvanceActorLoop8021(finite,0x3A0);
    Require(second.pc==0x340 && second.depth==1 && !second.set_ready258 && second.repeated
            && finite.size()==1 && finite[0].remaining==1,
            "count2 still repeats without premature finite-loop readiness");
    const auto final=AdvanceActorLoop8021(finite,0x3A0);
    Require(final.pc==0x3A4 && final.depth==0 && final.set_ready258 && !final.repeated && finite.empty(),
            "only final finite root iteration pops and publishes258");

    std::vector<GameActorLoopFrame> mixed{{0x440,2},{0x540,0}};
    for (unsigned i=0;i<2;++i) {
        const auto forever=AdvanceActorLoop8021(mixed,0x5A0);
        Require(forever.pc==0x540 && forever.depth==2 && forever.set_ready258 && forever.repeated
                && mixed.size()==2 && mixed[0].start==0x440 && mixed[0].remaining==2
                && mixed[1].start==0x540 && mixed[1].remaining==0,
                "nested count0 repeats with258 without decrement,pop or corrupting the outer finite loop");
    }
    std::vector<GameActorLoopFrame> empty;
    bool refused=false;
    try { (void)AdvanceActorLoop8021(empty,0x6A0); }
    catch (const std::runtime_error& error) {
        refused=std::string(error.what())=="ASC8021 without live owning loop frame";
    }
    Require(refused && empty.empty(),"empty owning frame throws the exact error without manufacturing a frame or readiness");
}
} // namespace dokodemo::post25_regression_detail

inline void GamePost25RegressionChecks() {
    dokodemo::post25_regression_detail::ClearedEventWaitDeadlineReturnAndUnknownSuccessor();
    dokodemo::post25_regression_detail::SignalledWaitStillNeedsThirtyTicksAfterHostResume();
    dokodemo::post25_regression_detail::HistoricalSameTargetNeedsFreshFrameAndRetainsOtherTrace();
    dokodemo::post25_regression_detail::ActorLoop8021NestedFiniteAndInfiniteReadiness();
}
