#pragma once
#include "../src/recovered/game_returned_interaction_native.h"
#include "game_return_witness_regressions.h" // comparison helpers only; do not run the passive suite again
#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Linked WindowsReturnedConversationInteraction policy checks. Every admission
// snapshot below is EXPLICIT SYNTHETIC METADATA, not a captured original trace.
// Its run/hash/caller identity is tied to a real owning synthetic Vm. Resolve,
// Local[0], post_owner_event, queue capacity and callback32 execute real APIs.
// No original resources, source469 clear, PSP wake, or matching claim is made.
namespace dokodemo::returned_interaction_regression_detail {
using recovered::Circle;
using recovered::GameActorScriptSnapshot;
using recovered::GameFsCode;
using recovered::GameFsStatus;
using recovered::GameInputFrame;
using recovered::GameMainFlowSnapshot;
using recovered::GameOwnerEventTarget;
using recovered::GameReturnedConversationInteraction;
using recovered::GameReturnedInteractionGate;
using recovered::GameReturnedInteractionSnapshot;

inline void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("returned interaction regression: " + message);
}
inline void StatusIs(const script::Event& event, script::Status expected, const char* where) {
    Require(event.status==expected,std::string(where)+": "+event.diagnostic.text());
}
inline void Good(const GameFsStatus& status, const char* where) {
    Require(bool(status),std::string(where)+": "+status.detail);
}
inline void Put(std::vector<uint8_t>& bytes, size_t at, uint32_t word, unsigned width=4) {
    for (unsigned i=0;i<width;++i) bytes.at(at+i)=uint8_t(word>>(8*i));
}
inline void Append(std::vector<uint8_t>& code, std::initializer_list<uint8_t> bytes) {
    code.insert(code.end(),bytes.begin(),bytes.end());
}
inline script::Value Code(uint32_t pc) {
    script::Value value; value.kind=script::Value::Kind::Code; value.word=0x70000000u|pc; return value;
}
inline script::Value True() {
    script::Value value; value.kind=script::Value::Kind::Scalar; value.word=0x10000001u; return value;
}
inline script::ValueWitness Scalar(uint32_t word) {
    script::ValueWitness value; value.kind=script::Value::Kind::Scalar; value.word=word; return value;
}
inline std::string OwnerText() { return std::string(161,'w')+"\x83\x67\x83\x8D"; }
inline std::string SavedResult() { return std::string(173,'r')+"\x83\x8D"; }
constexpr uint32_t CallbackPc=0x8DA28,MainPc=0x8DDB5;
constexpr uint32_t AlternateCallbackPc=CallbackPc+32;
constexpr uint64_t ScriptGeneration=201,MessageContext=301;
constexpr uint64_t RetainedSequence=42,FarewellBaseline=40;

inline void Pad(std::vector<uint8_t>& code, uint32_t end) {
    // One unreachable inline string rather than hundreds of thousands of
    // parsed no-ops. The fixed PCs are policy inputs, not original bytes.
    Require(code.size()+2<=end,"synthetic padding has space for opcode and terminator");
    const auto begin=code.size(); code.resize(end,uint8_t('p'));
    code[begin]=0x8E; code.back()=0;
}
inline const script::Program& Program() {
    static const script::Program program=[] {
        // Root passes its two owned arguments to the real main frame.
        std::vector<uint8_t> code{0x30,2,0x90,0,0,0x90,1,0,0x20,2,0,0,0,0x38};
        Put(code,10,MainPc,3);
        Pad(code,CallbackPc);
        Append(code,{0x30,2,0x90,0,0,0x90,1,0,0x8B,0x7C,2,0x32});
        Pad(code,AlternateCallbackPc);
        Append(code,{0x30,2,0x32});
        Pad(code,MainPc);
        // Capture a real frame reference, retain it and operand57, then sleep.
        Append(code,{0x30,2,0x91,0,0,0x8B,0x7A,1,0x91,0,0,0x80,57,0x80,7,0x44,
                     0x89,0x8B,0x7D,3,0x31});
        std::vector<uint8_t> bytes(28+code.size()+4);
        Put(bytes,0,0x0043534B); Put(bytes,4,0x20030204); Put(bytes,12,uint32_t(code.size()));
        Put(bytes,22,1,2); // actual Local[0], not a resolver-only callback stub
        std::copy(code.begin(),code.end(),bytes.begin()+28);
        Put(bytes,28+code.size(),0x70000000u|CallbackPc);
        script::Program result; script::Diagnostic error;
        Require(result.load_test_fixture(std::move(bytes),error),"load explicit linked fixture: "+error.text());
        return result;
    }();
    return program;
}
inline bool Same(const script::ValueWitness& a, const script::ValueWitness& b) {
    return return_witness_regression_detail::Same(a,b);
}
inline bool Same(const script::VmStateWitness& a, const script::VmStateWitness& b) {
    return return_witness_regression_detail::Same(a,b);
}

struct Fixture {
    std::unique_ptr<script::Vm> vm;
    GameReturnedConversationInteraction interaction;
    GameActorScriptSnapshot actor;
    GameMainFlowSnapshot flow;
    GameReturnedInteractionGate gate;
    script::Value retained_reference;
    uint64_t published_script_generation=ScriptGeneration,sequence=0;
    uint32_t resolves=0,dispatches=0,owner_callbacks=0,filler_callbacks=0,continuations=0;
    std::vector<int32_t> dispatch_results;

    Fixture() {
        NewVm();
        Good(interaction.Initialize([this] { ++resolves; return Target(); },
            [this](const GameOwnerEventTarget& expected,int32_t& result) {
                ++dispatches;
                if (!vm) return GameFsStatus{GameFsCode::Unavailable,0,"synthetic owner retired"};
                const auto now=Target();
                if (now.generation!=expected.generation || now.context.word!=expected.context.word
                    || now.context.context_owner.lock()!=expected.context.context_owner.lock()
                    || now.callback.kind!=expected.callback.kind || now.callback.word!=expected.callback.word)
                    return GameFsStatus{GameFsCode::Unavailable,0,"synthetic registry/Local0 changed"};
                result=vm->post_owner_event(vm->read_slot(script::Table::Local,0),0,0);
                dispatch_results.push_back(result); return GameFsStatus{};
            }),"initialize real owner-event adapter");
        flow.live=true; flow.outer_returns=1;
        gate.ui_idle=gate.inventory_retired=gate.old_post_modal_consumed=true;
        gate.source469=gate.manager9=true; gate.message_context=MessageContext;
        BeforeAck();
    }
    Fixture(const Fixture&)=delete;
    Fixture& operator=(const Fixture&)=delete;
    void NewVm() {
        vm=std::make_unique<script::Vm>(Program());
        vm->host_service([this](uint8_t id,const std::vector<script::Value>& arguments) {
            if (id==0x7A) {
                Require(arguments.size()==1 && arguments[0].kind==script::Value::Kind::Reference,
                        "capture actual main frame reference");
                retained_reference=arguments[0];
                Require(vm->peek_reference(retained_reference).cp932==OwnerText(),"main owns copied text");
                return script::Value::string(SavedResult());
            }
            if (id==0x7C) {
                const auto witness=vm->current_witness();
                Require(arguments.size()==2 && witness.frame_cells==2 && witness.frame_captured==2,
                        "owner event has exactly two scalar arguments; callerID=-1 adds no context slot");
                bool no_third=false;
                try { (void)vm->read_slot(script::Table::Frame,2); }
                catch (const std::exception&) { no_third=true; }
                Require(no_third && arguments[0].kind==script::Value::Kind::Scalar
                        && arguments[1].kind==script::Value::Kind::Scalar,"owner ABI omits third caller argument");
                const auto event=arguments[0].signed_integer(),argument=arguments[1].signed_integer();
                if (event==0 && argument==0) ++owner_callbacks;
                else {
                    Require(event==77 && argument==88,"only explicit queue-fill events are synthetic extras");
                    ++filler_callbacks;
                }
                Require(!retained_reference.frame_owner.expired()
                        && vm->peek_reference(retained_reference).cp932==OwnerText(),"envelope retains real main frame");
                return script::Value::string("temporary owning callback result");
            }
            Require(id==0x7D && arguments.size()==3 && arguments[0].kind==script::Value::Kind::Reference
                    && vm->peek_reference(arguments[0]).cp932==OwnerText()
                    && arguments[1].signed_integer()==57 && arguments[2].cp932==SavedResult(),
                    "original synthetic continuation consumes preserved operands and result");
            Require(vm->read_slot(script::Table::Frame,1).word==0x10000001u && vm->call_depth()==1,
                    "caller Frame[1] TRUE remains separate from callback result");
            ++continuations; return script::Value::integer(91);
        });
        vm->clock_tick(100);
        StatusIs(vm->start({script::Value::string(OwnerText()),True()}),script::Status::Running,"start owning main");
        StatusIs(vm->run(100),script::Status::Yield,"main owns positive deadline");
        const auto target=vm->context_value();
        Require(target.kind==script::Value::Kind::Context && target.word==0x80000000u,
                "run returned-interaction checks before another fixture retains registry slot0");
        const auto actual=vm->current_witness();
        Require(actual.code_entry==MainPc && actual.call_depth==1 && actual.context_registered
                && !actual.event_envelope_present && actual.deadline_tick==107 && actual.relative_deadline23==7,
                "real main is a registered,depth1,sleeping continuation");
    }
    GameOwnerEventTarget Target() {
        Require(bool(vm),"resolve only a live synthetic Vm");
        return {published_script_generation,vm->context_value(),vm->read_slot(script::Table::Local,0)};
    }
    void SetLocalCallback(script::Value value) {
        script::Value reference; reference.kind=script::Value::Kind::Reference;
        reference.table=script::Table::Local; reference.index=0;
        vm->store_reference(reference,std::move(value));
    }
    void BeforeAck() {
        actor={}; actor.live=actor.started=actor.context_enabled=true;
        actor.generation=ScriptGeneration; actor.sha256=Program().source().sha256;
        actor.awaiting_message=true; actor.messages=25; actor.message_acknowledgements=24; actor.pc=0x7D5FA;
        actor.game3_calls=4;
        const auto actual=vm->current_witness();
        auto message=actual;
        message.frame_id=actual.frame_id+1000; message.code_entry=0x7D548; message.current_pc=0x7D607;
        message.caller_frame_id=actual.frame_id; message.caller_entry=actual.code_entry;
        message.caller_return_pc=actual.current_pc; message.caller_call_pc=0x8DE00;
        message.call_depth=actual.call_depth+1; message.executed=actual.executed+10;
        actor.vm_current=message;
        actor.normal_return.valid=true; actor.normal_return.sequence=RetainedSequence;
        actor.normal_return.opcode_pc=0x85438; actor.normal_return.before=actual;
        actor.normal_return.after=actual; actor.normal_return.after.result=Scalar(0x10000001u);
        actor.farewell_return.valid=true; actor.farewell_return.sequence=FarewellBaseline;
        actor.farewell_return.opcode_pc=0x7D628;
        // These fields deliberately do not claim that this synthetic VM
        // executed any of the original 85438/7D5FA/7D628 instructions.
    }
    void AfterAck() {
        const auto message=actor.vm_current;
        actor.awaiting_message=false; actor.message_acknowledgements=25;
        actor.pc=vm->pc(); actor.vm_current=vm->current_witness();
        auto& farewell=actor.farewell_return;
        farewell.valid=true; farewell.sequence=RetainedSequence+1; farewell.opcode_pc=0x7D628;
        farewell.before=message; farewell.before.current_pc=0x7D628;
        farewell.before.executed=message.executed+4; farewell.before.result=Scalar(0);
        farewell.after=actor.vm_current; farewell.after.result=Scalar(0);
        farewell.after.executed=farewell.before.executed+1;
        farewell.after.clock_tick=farewell.before.clock_tick;
        farewell.after.deadline_tick=farewell.before.deadline_tick;
        farewell.after.relative_deadline23=farewell.before.relative_deadline23;
        farewell.after.frame_values[1]=Scalar(0x10000001u);
        actor.game3_calls=5; actor.game3_execution=actor.vm_current;
        // Actual API contract: game3 executes in helper8D932 called by the
        // main8DDB5 activation, rather than directly in the main frame.
        actor.game3_execution.code_entry=0x8D932; actor.game3_execution.current_pc=0x8D9B5;
        actor.game3_execution.call_depth=2; actor.game3_execution.frame_id=actor.vm_current.frame_id+2000;
        actor.game3_execution.caller_entry=MainPc;
        actor.game3_execution.caller_frame_id=actor.vm_current.frame_id;
        actor.game3_execution.caller_call_pc=0x8E024; actor.game3_execution.caller_return_pc=0x8E029;
        actor.game3_execution.executed=farewell.after.executed+1;
        RefreshActual();
    }
    void RefreshActual() {
        actor.vm_current=vm->current_witness(); actor.callback_depth=vm->callback_depth();
        actor.queued_callbacks=vm->queued_callbacks(); actor.callbacks_posted=vm->callbacks_posted();
        actor.callbacks_returned=vm->callbacks_returned(); actor.context_enabled=vm->context_enabled();
    }
    void Update() { Good(interaction.Update(actor,flow,gate),"update returned interaction"); }
    void Candidate() {
        Update(); const auto state=interaction.Snapshot();
        Require(state.live && state.candidate && !state.returned && !state.admitted && !state.eligible
                && state.message==25 && state.message_frame==actor.vm_current.frame_id
                && state.caller_frame==actor.vm_current.caller_frame_id && state.owner>0
                && state.script_generation==ScriptGeneration && state.run_id==actor.vm_current.run_id
                && state.prior_sequence==FarewellBaseline && state.retained_sequence==RetainedSequence
                && state.generation==0 && resolves==0,"BEFOREACK25 captures identity and old farewell baseline only");
    }
    void Admit() {
        Candidate(); AfterAck(); Update(); const auto state=interaction.Snapshot();
        Require(state.returned && state.admitted && state.eligible && !state.armed && !state.event_posted
                && state.generation==1 && state.context_word==0x80000000u
                && state.callback_word==(0x70000000u|CallbackPc) && state.return_sequence>state.prior_sequence
                && state.code_sha256==Program().source().sha256 && resolves==1,
                "fresh farewell NIL plus caller Frame[1] TRUE and new main game3 admit one token");
    }
    GameInputFrame Frame(uint32_t buttons=0,bool focused=true) {
        const auto state=interaction.Snapshot(); GameInputFrame frame;
        frame.buttons=buttons; frame.focused=focused; frame.sequence=++sequence;
        frame.message_context=state.message_context; frame.message_owner_generation=state.script_generation;
        frame.returned_owner=state.owner; frame.returned_generation=state.generation; return frame;
    }
    void Reject(const GameInputFrame& frame,const char* where) {
        const auto before=vm->current_witness(); const auto posted=vm->callbacks_posted(),returned=vm->callbacks_returned();
        const auto attempts=dispatches;
        Require(!interaction.Input(frame),where);
        Require(dispatches==attempts && vm->callbacks_posted()==posted && vm->callbacks_returned()==returned
                && Same(before,vm->current_witness()),std::string(where)+": no VM execution or queue dispatch");
    }
    void Release() {
        Reject(Frame(),"focused release arms without posting");
        Require(interaction.Snapshot().armed,"token owns a fresh focused release");
    }
    void Press() {
        Require(interaction.Input(Frame(Circle)),"fresh returned-token Circle posts actual event0,arg0");
        const auto state=interaction.Snapshot();
        Require(state.event_posted && state.posted==1 && !state.eligible && !state.armed
                && state.queue_result==0 && dispatch_results.back()==0,"only queue result0 consumes token");
    }
};

inline void FreshTokenActualCallbackRestoresAndNeverRearms() {
    Fixture f; f.Candidate();
    const auto unpublished=f.Frame();
    f.Reject(unpublished,"pre-boundary generation0 release cannot arm");
    f.Reject(f.Frame(Circle),"pre-boundary press cannot dispatch");
    const auto before=f.vm->current_witness();
    f.AfterAck(); f.Update();
    Require(Same(before,f.vm->current_witness()),"synthetic admission itself never executes the real VM");
    f.Reject(f.Frame(Circle),"held Circle across returned boundary cannot post");
    auto stale=unpublished; stale.sequence=++f.sequence;
    f.Reject(stale,"queued pre-boundary release cannot arm published generation1");
    Require(!f.interaction.Snapshot().armed,"stale release leaves new token unarmed");
    f.Release();
    const auto original=f.vm->current_witness();
    f.Press();
    Require(f.owner_callbacks==0 && f.vm->callbacks_posted()==1 && f.vm->callbacks_returned()==0
            && f.vm->current_witness().event_envelope_present && f.vm->pc()==CallbackPc,
            "accepted edge enters real owner envelope before executing callback body");
    f.vm->clock_tick(103);
    StatusIs(f.vm->run(5),script::Status::HostYield,"exact finite callback32 return");
    const auto restored=f.vm->current_witness();
    Require(f.owner_callbacks==1 && f.vm->callbacks_returned()==1 && !restored.event_envelope_present
            && f.vm->queued_callbacks()==0 && restored.run_id==original.run_id && restored.frame_id==original.frame_id
            && restored.code_entry==original.code_entry && restored.current_pc==original.current_pc
            && restored.call_depth==original.call_depth && restored.active_blocks==original.active_blocks
            && Same(restored.result,original.result) && restored.frame_cells==original.frame_cells
            && restored.operand_cells==original.operand_cells && restored.executed==original.executed+5
            && restored.deadline_tick==110 && restored.relative_deadline23==original.relative_deadline23
            && f.vm->deadline_writes()==1,"actual32 preserves frame,PC,result,operands and seven remaining deadline ticks");
    for (size_t i=0;i<original.frame_captured;++i)
        Require(Same(restored.frame_values[i],original.frame_values[i]),"callback restores every captured real frame cell");
    for (size_t i=0;i<original.operands_captured;++i)
        Require(Same(restored.operands[i],original.operands[i]),"callback restores every captured real operand");
    f.RefreshActual(); f.Update();
    Require(f.interaction.Snapshot().callback_returned && f.interaction.Snapshot().returned_callbacks==1,
            "callback completion comes from actual returned counter");
    for (unsigned i=0;i<2;++i) {
        f.Reject(f.Frame(),"consumed activation cannot rearm on another release");
        f.Reject(f.Frame(Circle),"consumed activation cannot post twice"); f.Update();
    }
    f.BeforeAck(); f.Update();
    Require(f.interaction.Snapshot().event_posted && f.interaction.Snapshot().posted==1
            && f.interaction.Snapshot().generation==1,"same captured frame/retained trace cannot rearm after callback return");
    f.vm->clock_tick(109); const auto executed=f.vm->executed();
    StatusIs(f.vm->run(50),script::Status::Yield,"preserved deadline is still pending");
    Require(f.vm->executed()==executed && f.continuations==0,"event does not run the caller before its retained deadline");
    f.vm->clock_tick(110); StatusIs(f.vm->run(50),script::Status::Complete,"retained main continuation completes");
    Require(f.continuations==1 && f.owner_callbacks==1 && f.vm->result().signed_integer()==91,
            "one owner event and one original synthetic continuation execute");
}

inline void HeldRefocusSequenceIdentityAndOverflow() {
    Fixture f; f.Admit();
    f.Reject(f.Frame(Circle),"held Circle at activation is refused");
    f.Release(); f.Reject(f.Frame(0,false),"focus loss drops armed release");
    f.Reject(f.Frame(Circle),"held refocus needs a new focused release");
    Require(!f.interaction.Snapshot().armed,"refocus held edge stays unarmed");
    auto foreign=f.Frame(); ++foreign.returned_owner;
    f.Reject(foreign,"foreign interaction owner release cannot arm");
    auto stale=f.Frame(); --stale.returned_generation;
    f.Reject(stale,"stale returned generation release cannot arm");
    auto old_script=f.Frame(); --old_script.message_owner_generation;
    f.Reject(old_script,"retired script generation release cannot arm");
    auto old_context=f.Frame(); --old_context.message_context;
    f.Reject(old_context,"old message context release cannot arm");
    f.Reject(f.Frame(Circle),"invalid identity releases do not arm a subsequent held press");
    f.Release();
    auto duplicate=f.Frame(Circle); --duplicate.sequence;
    f.Reject(duplicate,"duplicate sequence cannot consume armed edge");
    auto older=f.Frame(Circle); older.sequence=1;
    f.Reject(older,"out-of-order sequence cannot consume armed edge");
    auto overflow=f.Frame(Circle); overflow.overflow_resync=true;
    f.Reject(overflow,"overflow resync cannot dispatch and drops armed release");
    Require(!f.interaction.Snapshot().armed,"overflow requires another release");
    f.Reject(f.Frame(Circle),"held Circle after overflow cannot retry");
    f.Release(); f.Press();
    Require(f.dispatches==1 && f.vm->callbacks_posted()==1,"all refused edges precede one actual post");
}

using Mutation=std::function<void(Fixture&)>;
inline void NoAdmission(const char* name,const Mutation& mutate,bool expect_returned=false) {
    Fixture f; f.Candidate(); f.AfterAck(); mutate(f); f.Update();
    const auto state=f.interaction.Snapshot();
    Require(!state.admitted && !state.eligible && state.generation==0 && !state.event_posted
            && (!expect_returned || state.returned),std::string(name)+": no published activation");
    f.Reject(f.Frame(),name); f.Reject(f.Frame(Circle),name);
    Require(f.dispatches==0 && f.vm->callbacks_posted()==0,std::string(name)+": no queue call");
}
inline void ReturnAndMainConsumerAdmissionRefusals() {
    NoAdmission("old farewell sequence",[](Fixture& f){f.actor.farewell_return.sequence=FarewellBaseline;});
    NoAdmission("wrong returned message frame",[](Fixture& f){++f.actor.farewell_return.before.frame_id;});
    NoAdmission("wrong restored caller frame",[](Fixture& f){++f.actor.farewell_return.after.frame_id;});
    NoAdmission("wrong restored caller PC",[](Fixture& f){++f.actor.farewell_return.after.current_pc;});
    NoAdmission("wrong restored caller entry",[](Fixture& f){++f.actor.farewell_return.after.code_entry;});
    NoAdmission("wrong return before PC",[](Fixture& f){++f.actor.farewell_return.before.current_pc;});
    NoAdmission("different farewell run",[](Fixture& f){++f.actor.farewell_return.before.run_id;});
    NoAdmission("different farewell code",[](Fixture& f){f.actor.farewell_return.after.code_sha256[0]='!';});
    NoAdmission("farewell before result TRUE is not NIL",[](Fixture& f){f.actor.farewell_return.before.result=Scalar(0x10000001u);});
    NoAdmission("farewell after result TRUE is not NIL",[](Fixture& f){f.actor.farewell_return.after.result=Scalar(0x10000001u);});
    NoAdmission("integer0 is not canonical NIL",[](Fixture& f){f.actor.farewell_return.after.result=Scalar(0x20000000u);});
    NoAdmission("caller Frame1 NIL is not retained TRUE",[](Fixture& f){f.actor.farewell_return.after.frame_values[1]=Scalar(0);});
    NoAdmission("retained85438 TRUE mutated at same sequence",[](Fixture& f){f.actor.normal_return.after.result=Scalar(0x10000000u);});
    NoAdmission("retained85438 sequence replaced",[](Fixture& f){++f.actor.normal_return.sequence;});
    NoAdmission("return deadline changed",[](Fixture& f){++f.actor.farewell_return.after.deadline_tick;});
    NoAdmission("return relative deadline changed",[](Fixture& f){++f.actor.farewell_return.after.relative_deadline23;});
    NoAdmission("return clock changed",[](Fixture& f){++f.actor.farewell_return.after.clock_tick;});
    NoAdmission("ACK missing",[](Fixture& f){--f.actor.message_acknowledgements;});
    NoAdmission("ACK from another message",[](Fixture& f){++f.actor.messages;});
    NoAdmission("old game3 call count",[](Fixture& f){--f.actor.game3_calls;},true);
    NoAdmission("historical last_service3 alone",[](Fixture& f){
        f.actor.game3_execution={}; f.actor.last_service=3; f.actor.last_service_returned=true;
    },true);
    NoAdmission("game3 before farewell return",[](Fixture& f){
        f.actor.game3_execution.executed=f.actor.farewell_return.after.executed;
    },true);
    NoAdmission("game3 foreign run",[](Fixture& f){++f.actor.game3_execution.run_id;},true);
    NoAdmission("game3 foreign code",[](Fixture& f){f.actor.game3_execution.code_sha256[0]='!';},true);
    NoAdmission("game3 wrong entry",[](Fixture& f){f.actor.game3_execution.code_entry=0x7D548;},true);
    NoAdmission("game3 wrong depth",[](Fixture& f){++f.actor.game3_execution.call_depth;},true);
    NoAdmission("game3 wrong main caller entry",[](Fixture& f){++f.actor.game3_execution.caller_entry;},true);
    NoAdmission("game3 wrong main caller frame",[](Fixture& f){++f.actor.game3_execution.caller_frame_id;},true);
    NoAdmission("game3 wrong main call PC",[](Fixture& f){++f.actor.game3_execution.caller_call_pc;},true);
    NoAdmission("game3 wrong main return PC",[](Fixture& f){++f.actor.game3_execution.caller_return_pc;},true);
    NoAdmission("game3 belongs to callback",[](Fixture& f){f.actor.game3_execution.event_envelope_present=true;},true);
    NoAdmission("deadline expiry without return",[](Fixture& f){
        f.actor.farewell_return.valid=false; f.actor.vm_current.clock_tick=1000;
        f.actor.vm_current.deadline_tick=0; f.actor.vm_current.relative_deadline23=0;
    });
    // Seeing only the after-ACK state must not retroactively fabricate the
    // mandatory BEFOREACK25 capture, even with plausible return metadata.
    Fixture late; late.AfterAck(); late.Update();
    Require(!late.interaction.Snapshot().candidate && !late.interaction.Snapshot().admitted,
            "a post-ACK first observation has no candidate lineage");
}

inline void ReadinessExclusionsAndBusyCutRequiresRelease() {
    const std::vector<std::pair<const char*,Mutation>> cuts{
        {"UI busy",[](Fixture& f){f.gate.ui_idle=false;}},
        {"inventory not retired",[](Fixture& f){f.gate.inventory_retired=false;}},
        {"old post-modal unconsumed",[](Fixture& f){f.gate.old_post_modal_consumed=false;}},
        {"source469 absent",[](Fixture& f){f.gate.source469=false;}},
        {"manager9 absent",[](Fixture& f){f.gate.manager9=false;}},
        {"script blocked",[](Fixture& f){f.actor.blocked=true;}},
        {"script completed",[](Fixture& f){f.actor.completed=true;}},
        {"script error",[](Fixture& f){f.actor.error="explicit synthetic busy error";}},
        {"awaiting message",[](Fixture& f){f.actor.awaiting_message=true;}},
        {"awaiting choice",[](Fixture& f){f.actor.awaiting_choice=true;}},
        {"inventory requested",[](Fixture& f){f.actor.inventory_requested=true;}},
        {"source input suppressed48",[](Fixture& f){f.actor.source_main_input_suppressed48=true;}},
        {"context disabled",[](Fixture& f){f.actor.context_enabled=false;}},
        {"context unregistered",[](Fixture& f){f.actor.vm_current.context_registered=false;}},
        {"VM stopped",[](Fixture& f){f.actor.vm_current.stopped=true;}},
        {"callback envelope active",[](Fixture& f){f.actor.vm_current.event_envelope_present=true;}},
        {"callback depth busy",[](Fixture& f){f.actor.callback_depth=1;}},
        {"pending callback busy",[](Fixture& f){f.actor.queued_callbacks=1;}},
        {"unreturned callback busy",[](Fixture& f){f.actor.callbacks_posted=1;}},
        {"current entry wrong",[](Fixture& f){f.actor.vm_current.code_entry=0x7D548;}},
        {"current depth wrong",[](Fixture& f){f.actor.vm_current.call_depth=2;}},
        {"flow retired",[](Fixture& f){f.flow.live=false;}},
        {"flow nonzero phase",[](Fixture& f){f.flow.phase1C=1;}},
        {"flow nonzero substate",[](Fixture& f){f.flow.substate24=1;}},
        {"flow pending return",[](Fixture& f){f.flow.return20=1;}},
        {"flow lacks outer return",[](Fixture& f){f.flow.outer_returns=0;}}
    };
    for (const auto& cut:cuts) NoAdmission(cut.first,cut.second,true);
    // A busy interval after admission keeps the token but drops its release.
    // manager9 is an admission witness; the ongoing eligibility check does
    // not require inventing a second manager9 producer.
    Fixture f; f.Admit(); f.Release(); const auto token=f.interaction.Snapshot().generation;
    f.gate.ui_idle=false; f.Update();
    Require(f.interaction.Snapshot().admitted && !f.interaction.Snapshot().eligible
            && !f.interaction.Snapshot().armed,"busy cut suspends an already admitted token");
    f.Reject(f.Frame(Circle),"busy UI cannot consume earlier release");
    f.gate.ui_idle=true; f.Update();
    Require(f.interaction.Snapshot().generation==token && f.interaction.Snapshot().eligible,
            "idle recovery republishes eligibility for the same token");
    f.Reject(f.Frame(Circle),"held Circle across busy cut cannot dispatch"); f.Release(); f.Press();
}

inline void QueueFullRetainsTokenButRequiresNewRelease() {
    Fixture f; f.Admit(); f.Release(); const auto token=f.interaction.Snapshot();
    const auto code=f.vm->read_slot(script::Table::Local,0);
    Require(f.vm->post_owner_event(code,77,88)==0,"explicit filler occupies real event envelope");
    for (unsigned i=0;i<64;++i)
        Require(f.vm->post_owner_event(code,77,88)==0,"fill each of the real64 pending owning queue nodes");
    Require(f.vm->queued_callbacks()==64 && f.vm->callbacks_posted()==65,"actual queue reaches checked capacity");
    const auto full=f.vm->current_witness(); const auto posted=f.vm->callbacks_posted();
    // Snapshot/dispatch race is explicit: admission was idle before another
    // producer filled the queue. Do not fake queue_result in the lambda.
    Require(!f.interaction.Input(f.Frame(Circle)),"real queue result1 refuses current edge");
    const auto refused=f.interaction.Snapshot();
    Require(refused.live && refused.admitted && refused.eligible && !refused.event_posted && !refused.armed
            && refused.generation==token.generation && refused.owner==token.owner && refused.queue_result==1
            && refused.posted==0 && f.dispatches==1 && f.dispatch_results.back()==1
            && f.vm->callbacks_posted()==posted && Same(full,f.vm->current_witness()),
            "queue full consumes neither activation nor VM state and drops only the release");
    f.Reject(f.Frame(Circle),"held queue-full edge does not automatically retry");
    for (unsigned i=0;i<65;++i)
        StatusIs(f.vm->run(5),script::Status::HostYield,"drain one finite filler callback");
    Require(f.filler_callbacks==65 && f.owner_callbacks==0 && f.vm->queued_callbacks()==0
            && f.vm->callbacks_returned()==65 && !f.vm->current_witness().event_envelope_present,
            "all actual filler callbacks return before retry");
    f.RefreshActual(); f.Update();
    f.Reject(f.Frame(Circle),"draining capacity does not turn a held button into a fresh edge");
    f.Release(); f.Press();
    Require(f.dispatches==2 && f.dispatch_results==std::vector<int32_t>({1,0})
            && f.vm->callbacks_posted()==66 && f.interaction.Snapshot().posted==1,
            "same token is consumed exactly once by the later real queue result0");
    StatusIs(f.vm->run(5),script::Status::HostYield,"returned owner callback after queue retry");
    f.RefreshActual(); f.Update();
    Require(f.owner_callbacks==1 && f.interaction.Snapshot().returned_callbacks==1,
            "returned callback count excludes65 earlier filler callbacks");
}

inline void FrozenAcceptedWitnessesAndRetiredQueueRefusal() {
    {
        Fixture f; f.Candidate(); f.AfterAck(); f.gate.manager9=false; f.Update();
        Require(f.interaction.Snapshot().returned && !f.interaction.Snapshot().admitted,
                "accepted farewell can precede the final manager admission witness");
        const auto sequence=f.actor.farewell_return.sequence;
        ++f.actor.farewell_return.after.frame_values[0].cp932_bytes;
        f.gate.manager9=true; f.Update();
        Require(f.actor.farewell_return.sequence==sequence && f.interaction.Snapshot().revoked
                && !f.interaction.Snapshot().admitted,"same-sequence mutation of accepted farewell cannot admit later");
        f.Reject(f.Frame(),"mutated accepted farewell cannot arm");
    }
    {
        Fixture f; f.Admit(); f.Release();
        ++f.actor.normal_return.before.operand_cells; f.Update();
        Require(f.interaction.Snapshot().revoked && !f.interaction.Snapshot().event_posted,
                "full retained85438 witness remains frozen after token publication");
        f.Reject(f.Frame(Circle),"same-sequence retained metadata mutation cannot consume armed edge");
    }
    {
        Fixture f; f.Admit(); f.Release();
        f.actor.farewell_return.after.result=Scalar(0x10000001u); f.Update();
        Require(f.interaction.Snapshot().revoked && !f.interaction.Snapshot().event_posted,
                "full accepted farewell remains frozen after token publication");
        f.Reject(f.Frame(Circle),"accepted NIL-to-TRUE mutation cannot consume armed edge");
    }
    {
        Fixture f; f.Admit(); f.Release();
        // Actual owner can retire between the last readiness snapshot and
        // dispatch while its weak registry token still names the same Vm.
        f.vm->clock_tick(107); StatusIs(f.vm->run(50),script::Status::Complete,"actual owning Vm retires");
        const auto stopped=f.vm->current_witness();
        Require(!f.interaction.Input(f.Frame(Circle)),"actual post_owner_event result-1 cannot consume token");
        const auto state=f.interaction.Snapshot();
        Require(state.revoked && !state.live && !state.event_posted && state.posted==0 && state.queue_result==-1
                && f.dispatches==1 && f.dispatch_results==std::vector<int32_t>({-1})
                && f.vm->callbacks_posted()==0 && Same(stopped,f.vm->current_witness()),
                "retired owner result-1 is terminal and preserves actual stopped VM");
    }
}

inline void RetirementRegistryReplacementAndUniqueOwners() {
    {
        Fixture f; f.Admit(); f.Release(); f.SetLocalCallback(Code(AlternateCallbackPc));
        f.Reject(f.Frame(Circle),"real Local0 replacement refuses captured callback identity");
        Require(f.interaction.Snapshot().revoked && !f.interaction.Snapshot().live,
                "callback replacement is terminal for this owner");
    }
    {
        Fixture f; f.Admit(); f.Release(); ++f.published_script_generation;
        f.Reject(f.Frame(Circle),"resolver generation replacement refuses old target");
        Require(f.interaction.Snapshot().revoked,"registry generation replacement revokes token");
    }
    {
        Fixture f; f.Candidate(); f.AfterAck(); f.SetLocalCallback(script::Value::integer(0)); f.Update();
        Require(f.interaction.Snapshot().revoked && !f.interaction.Snapshot().admitted && f.dispatches==0,
                "non-code actual Local0 cannot publish a token");
    }
    {
        Fixture f; f.Admit(); const auto old=f.Target(); f.vm.reset();
        Require(old.context.context_owner.expired(),"retired actual Vm releases registry lease");
        f.NewVm(); const auto replacement=f.Target();
        Require(replacement.context.word==old.context.word && !replacement.context.context_owner.expired(),
                "real registry slot0 is reused with a different lifetime owner");
        f.Reject(f.Frame(),"old lifetime token cannot arm new registry owner at same numeric slot");
        Require(f.interaction.Snapshot().revoked && f.dispatches==0,"registry slot reuse is terminal for old activation");
    }
    const std::vector<std::pair<const char*,Mutation>> retirements{
        {"script generation retired",[](Fixture& f){++f.actor.generation;}},
        {"script run retired",[](Fixture& f){++f.actor.vm_current.run_id;}},
        {"script code replaced",[](Fixture& f){f.actor.sha256[0]='!';}},
        {"script owner retired",[](Fixture& f){f.actor.live=false;}}
    };
    for (const auto& retirement:retirements) {
        Fixture f; f.Admit(); retirement.second(f); f.Update();
        Require(f.interaction.Snapshot().revoked && !f.interaction.Snapshot().eligible,retirement.first);
        f.Reject(f.Frame(),retirement.first); f.Reject(f.Frame(Circle),retirement.first);
    }
    {
        Fixture f; f.Admit(); const auto first=f.interaction.Snapshot();
        GameReturnedConversationInteraction other;
        Good(other.Initialize([&f]{return f.Target();},
            [&f](const GameOwnerEventTarget& target,int32_t& result){
                result=f.vm->post_owner_event(target.callback,0,0); return GameFsStatus{};
            }),"second interaction owner on same actual Vm");
        f.BeforeAck(); Good(other.Update(f.actor,f.flow,f.gate),"second owner's synthetic candidate");
        f.AfterAck(); Good(other.Update(f.actor,f.flow,f.gate),"second owner's synthetic returned token");
        const auto second=other.Snapshot();
        Require(second.admitted && second.owner!=first.owner && second.generation==first.generation,
                "published owner is unique even when token generation and registry target match");
        auto cross=f.Frame(); cross.returned_owner=second.owner;
        f.Reject(cross,"same-generation release from another live interaction owner is foreign");
        f.interaction.Close(); f.Reject(f.Frame(),"closed owner cannot arm");
        f.Reject(f.Frame(Circle),"closed owner cannot post");
        Require(f.interaction.Snapshot().revoked && !f.interaction.Initialize([&f]{return f.Target();},
            [](const GameOwnerEventTarget&,int32_t&){return GameFsStatus{};}),"retired interaction object cannot be initialized again");
    }
}
} // namespace dokodemo::returned_interaction_regression_detail

inline void GameReturnedInteractionRegressionChecks() {
    using namespace dokodemo::returned_interaction_regression_detail;
    FreshTokenActualCallbackRestoresAndNeverRearms();
    HeldRefocusSequenceIdentityAndOverflow();
    ReturnAndMainConsumerAdmissionRefusals();
    ReadinessExclusionsAndBusyCutRequiresRelease();
    QueueFullRetainsTokenButRequiresNewRelease();
    FrozenAcceptedWitnessesAndRetiredQueueRefusal();
    RetirementRegistryReplacementAndUniqueOwners();
}
