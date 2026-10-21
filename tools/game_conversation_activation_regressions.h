#pragma once
#include "../src/recovered/game_conversation_activation_native.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// DOMAIN-ONLY, EXPLICIT SYNTHETIC METADATA. No Vm, resources, input, queue or
// original execution is constructed here. PCs/forms and code hash come from
// ROOT's released source evidence; witness values/timelines are synthetic.
// This suite claims domain-policy checks only, not real-trace/PSP coverage.
namespace dokodemo::conversation_activation_regression_detail {
using recovered::GameActorScriptSnapshot;
using recovered::GameConversationActivation;
using recovered::GameConversationProofKind;
using Witness=script::VmStateWitness;
using Return=script::NormalReturnWitness;
constexpr uint32_t TrueWord=0x10000001u;
constexpr uint64_t MessageContext=901;
constexpr const char* SourceSha="c64773090b1c331d043b264b291c8948a8b99787f541ddc44d8e1d027958c93b";
inline void CopyCodeSha(std::string& out){out.assign(SourceSha,64);}
inline void CopyCodeSha(std::array<char,65>& out){std::copy_n(SourceSha,65,out.begin());}
static_assert(static_cast<unsigned>(GameConversationProofKind::Legacy85438)==1);
static_assert(static_cast<unsigned>(GameConversationProofKind::Topic2B483)==2);

inline void Require(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error("conversation activation regression: "+message);
}
inline script::ValueWitness Scalar(uint32_t word) {
    script::ValueWitness value;value.kind=script::Value::Kind::Scalar;value.word=word;return value;
}
inline Witness State(uint64_t frame,uint32_t entry,uint32_t pc,uint32_t depth,uint64_t executed) {
    Witness v;v.valid=v.context_registered=true;v.run_id=31;v.frame_id=frame;
    CopyCodeSha(v.code_sha256);v.context_word=0x80000000u;v.code_entry=entry;
    v.current_pc=pc;v.call_depth=depth;v.executed=executed;v.active_blocks=1;
    v.clock_tick=100;v.deadline_tick=99;v.relative_deadline23=-1;
    v.frame_cells=v.frame_captured=2;v.frame_values[0]=Scalar(0x20000011u);
    v.frame_values[1]=Scalar(TrueWord);v.result=Scalar(0x20000000u);return v;
}
inline Return Ordinary(const Witness& wait,uint32_t opcode,uint64_t sequence,uint32_t result) {
    Return r;r.valid=true;r.sequence=sequence;r.opcode_pc=opcode;r.before=wait;
    auto& a=r.before;auto& b=r.after;a.current_pc=opcode;a.executed+=20;a.result=Scalar(result);
    b=a;b.frame_id=a.caller_frame_id;b.code_entry=a.caller_entry;b.current_pc=a.caller_return_pc;
    b.call_depth=a.call_depth-1;b.executed=a.executed+1;b.operand_cells=a.caller_stack_base;
    b.frame_cells=b.frame_captured=2;b.frame_values[0]=Scalar(0x20000000u);b.frame_values[1]=Scalar(TrueWord);
    b.caller_frame_id=0;b.caller_entry=b.caller_call_pc=b.caller_return_pc=0;return r;
}

struct Fixture {
    GameConversationActivation domain;
    GameActorScriptSnapshot actor;
    Witness body_wait,farewell_wait;
    uint32_t body_message=23;

    Fixture() {
        actor.live=actor.started=true;actor.generation=17;actor.sha256=SourceSha;actor.game3_calls=11;
        // An explicitly synthetic older farewell cannot complete a new body.
        auto old=State(150,0x7D548,0x7D581,4,500);
        old.caller_frame_id=148;old.caller_entry=0x899F9;
        old.caller_call_pc=0x89A1D;old.caller_return_pc=0x89A21;
        actor.farewell_return=Ordinary(old,0x7D628,7,0);domain.Start();
    }
    void Observe(){domain.Observe(actor,MessageContext);}
    void PrepareBody(uint64_t frame=244,uint32_t message=23,uint64_t executed=1000) {
        body_message=message;actor.messages=message;actor.message_acknowledgements=message-1;
        actor.awaiting_message=true;actor.pc=0x2B462;
        body_wait=State(frame,0x2B372,0x2B471,6,executed);
        body_wait.caller_frame_id=frame-1;body_wait.caller_entry=0x2660;
        body_wait.caller_call_pc=0x267F;body_wait.caller_return_pc=0x2680;
        actor.vm_current=body_wait;
    }
    void PrepareBodyReturn() {
        actor.normal_return=Ordinary(body_wait,0x2B483,actor.normal_return.sequence+1,TrueWord);
        actor.messages=body_message;actor.message_acknowledgements=body_message;
        actor.awaiting_message=false;actor.vm_current=actor.normal_return.after;actor.pc=actor.vm_current.current_pc;
    }
    void PrepareFarewell(uint64_t frame=258) {
        actor.messages=body_message+1;actor.message_acknowledgements=body_message;
        actor.awaiting_message=true;actor.pc=0x7D577;
        farewell_wait=State(frame,0x7D548,0x7D581,4,body_wait.executed+100);
        farewell_wait.frame_cells=farewell_wait.frame_captured=1;
        farewell_wait.caller_frame_id=frame-2;farewell_wait.caller_entry=0x899F9;
        farewell_wait.caller_call_pc=0x89A1D;farewell_wait.caller_return_pc=0x89A21;
        actor.vm_current=farewell_wait;
    }
    void PrepareFarewellReturn() {
        actor.farewell_return=Ordinary(farewell_wait,0x7D628,actor.farewell_return.sequence+1,0);
        actor.awaiting_message=false;actor.message_acknowledgements=actor.messages;
        actor.vm_current=actor.farewell_return.after;actor.pc=actor.vm_current.current_pc;
    }
    void PrepareMain() {
        const auto executed=actor.farewell_return.after.executed;
        actor.awaiting_message=false;actor.vm_current=State(73,0x8DDB5,0x8E029,1,executed+40);
        actor.pc=actor.vm_current.current_pc;++actor.game3_calls;
        auto game=State(275,0x8D932,0x8D9B5,2,executed+30);
        game.caller_frame_id=73;game.caller_entry=0x8DDB5;
        game.caller_call_pc=0x8E024;game.caller_return_pc=0x8E029;actor.game3_execution=game;
    }
    void BindBody() {
        PrepareBody();Observe();const auto& s=domain.State();
        Require(s.live&&s.body_bound&&!s.body_completed&&!s.farewell_bound
                &&s.kind==GameConversationProofKind::Topic2B483&&s.body_pc==0x2B462,
                "pre-ACK Topic body binds its concrete source form only");
    }
    void CompleteBody() {
        PrepareBodyReturn();Observe();
        Require(domain.State().body_completed&&!domain.State().farewell_bound
                &&actor.normal_return.opcode_pc==0x2B483,"source2B483 metadata completes the bound body only");
    }
    void BindFarewell() {
        PrepareFarewell();Observe();const auto& s=domain.State();
        Require(s.farewell_bound&&!s.returned&&s.farewell_pc==0x7D577&&s.farewell_current_pc==0x7D581
                &&s.message==body_message+1&&s.message_context==MessageContext,
                "next early farewell binds independently before ACK");
    }
    void FinishPreparedBodyReturn() {
        Observe();PrepareFarewell();Observe();PrepareFarewellReturn();Observe();PrepareMain();Observe();
    }
    void NoCompletion(const char* name) {
        Require(!domain.State().main_consumed&&!domain.State().consumed&&!domain.Consume(),
                std::string(name)+": incomplete/foreign lineage cannot publish consumable activation");
    }
};
using Mutation=std::function<void(Fixture&)>;
using Cases=std::vector<std::pair<const char*,Mutation>>;

inline void TopicBodyFarewellAndMainAreSeparateEvidence() {
    Fixture f;Require(!f.domain.Consume(),"empty live domain cannot be consumed");
    f.BindBody();Require(!f.domain.Consume(),"pending body is not a completed conversation");
    f.CompleteBody();Require(!f.domain.Consume(),"TRUE body alone has no farewell or main consumer");
    const auto completed=f.domain.Snapshot();f.Observe();
    Require(f.domain.State().id==completed.id&&f.domain.HistoryMatches(f.actor),
            "reobserving unchanged body proof neither replaces nor consumes activation");
    f.BindFarewell();Require(!f.domain.Consume(),"pending farewell cannot consume TRUE body");
    f.PrepareFarewellReturn();f.Observe();const auto returned=f.domain.Snapshot();
    Require(returned.returned&&!returned.main_consumed&&!f.domain.Consume()
            &&returned.body_sequence==f.actor.normal_return.sequence
            &&returned.return_sequence>returned.prior_sequence,
            "fresh NIL farewell remains separate from bodyTRUE and mainGAME3");
    f.PrepareMain();f.Observe();const auto ready=f.domain.Snapshot();
    Require(ready.live&&ready.main_consumed&&!ready.consumed&&!ready.revoked
            &&ready.kind==GameConversationProofKind::Topic2B483&&ready.body_frame==f.body_wait.frame_id
            &&ready.body_caller_entry==0x2660&&ready.body_caller_pc==0x2680
            &&ready.message_frame!=ready.body_frame&&ready.game3_pc==0x8D9B5
            &&f.actor.normal_return.opcode_pc==0x2B483,
            "Topic kind2 and actual opcode survive the full independently completed lifecycle");
    Require(f.domain.Consume()&&!f.domain.Consume()&&f.domain.State().consumed,
            "one complete domain activation can be consumed exactly once");
}

inline void ForeignOrIncompleteBodyCannotActivate() {
    const Cases pending{
        {"unpromoted next-frame entry2B484",[](Fixture& f){f.actor.vm_current.code_entry=0x2B484;}},
        {"incorrect pending message PC",[](Fixture& f){f.actor.pc=0x2B45C;}},
        {"foreign direct body caller",[](Fixture& f){++f.actor.vm_current.caller_entry;}},
        {"body observed after ACK",[](Fixture& f){++f.actor.message_acknowledgements;}},
        {"body in callback envelope",[](Fixture& f){f.actor.vm_current.event_envelope_present=true;}}
    };
    for(const auto& test:pending) {
        Fixture f;f.PrepareBody();test.second(f);f.Observe();
        Require(!f.domain.State().body_bound,std::string(test.first)+": cannot bind a source body");
        f.PrepareBodyReturn();f.FinishPreparedBodyReturn();f.NoCompletion(test.first);
    }
    const Cases returns{
        {"foreign body generation",[](Fixture& f){++f.actor.generation;}},
        {"foreign body run",[](Fixture& f){++f.actor.normal_return.before.run_id;}},
        {"foreign body code",[](Fixture& f){f.actor.normal_return.after.code_sha256[0]='!';}},
        {"different body frame",[](Fixture& f){++f.actor.normal_return.before.frame_id;}},
        {"different body caller frame",[](Fixture& f){++f.actor.normal_return.before.caller_frame_id;}},
        {"different body call site",[](Fixture& f){++f.actor.normal_return.before.caller_call_pc;}},
        {"different restored caller",[](Fixture& f){++f.actor.normal_return.after.frame_id;}},
        {"return2B484 substitution",[](Fixture& f){f.actor.normal_return.opcode_pc=0x2B484;f.actor.normal_return.before.current_pc=0x2B484;}},
        {"fake legacy85438 substitution",[](Fixture& f){f.actor.normal_return.opcode_pc=0x85438;f.actor.normal_return.before.current_pc=0x85438;}},
        {"body NIL branch",[](Fixture& f){f.actor.normal_return.before.result=f.actor.normal_return.after.result=Scalar(0);}},
        {"body FALSE branch",[](Fixture& f){f.actor.normal_return.before.result=f.actor.normal_return.after.result=Scalar(0x10000000u);}},
        {"body integer1 is not TRUE",[](Fixture& f){f.actor.normal_return.before.result=f.actor.normal_return.after.result=Scalar(0x20000001u);}},
        {"body ACK incomplete",[](Fixture& f){--f.actor.message_acknowledgements;}},
        {"stale body sequence",[](Fixture& f){--f.actor.normal_return.sequence;}},
        {"body callback return",[](Fixture& f){f.actor.normal_return.before.event_envelope_present=true;}}
    };
    for(const auto& test:returns) {
        Fixture f;f.BindBody();f.PrepareBodyReturn();test.second(f);f.FinishPreparedBodyReturn();
        Require(!f.domain.State().body_completed,std::string(test.first)+": no completed body proof");
        f.NoCompletion(test.first);
    }
    Fixture late;late.PrepareBody();late.PrepareBodyReturn();late.FinishPreparedBodyReturn();
    late.NoCompletion("first observation after body return has no pre-ACK lineage");
}

inline void SameSequenceCannotSubstituteCompletedBodyHistory() {
    const Cases changed{
        {"body result changed at same sequence",[](Fixture& f){f.actor.normal_return.after.result=Scalar(0x10000000u);}},
        {"body frame changed at same sequence",[](Fixture& f){++f.actor.normal_return.before.frame_id;}},
        {"body captured value changed at same sequence",[](Fixture& f){++f.actor.normal_return.before.frame_values[0].word;}},
        {"body typed value metadata changed",[](Fixture& f){++f.actor.normal_return.after.frame_values[1].cp932_bytes;}},
        {"body operand history changed",[](Fixture& f){++f.actor.normal_return.before.operand_cells;}},
        {"new sequence alone is not same completion",[](Fixture& f){++f.actor.normal_return.sequence;}}
    };
    for(const auto& test:changed) {
        Fixture f;f.BindBody();f.CompleteBody();const auto frozen=f.domain.Snapshot();
        test.second(f);Require(!f.domain.HistoryMatches(f.actor),std::string(test.first)+": frozen proof differs");
        f.PrepareFarewell();f.Observe();f.PrepareFarewellReturn();f.Observe();f.PrepareMain();f.Observe();
        Require(f.domain.State().revoked&&f.domain.State().id==frozen.id,
                std::string(test.first)+": no silent replacement or new activation");f.NoCompletion(test.first);
    }
}

inline void OrderingFormsAndStaleFarewellCannotSubstitute() {
    const Cases farewell_cuts{
        {"skipped next message ordinal",[](Fixture& f){++f.actor.messages;++f.actor.message_acknowledgements;}},
        {"same ordinal as body",[](Fixture& f){--f.actor.messages;--f.actor.message_acknowledgements;}},
        {"late form cannot replace Topic early form",[](Fixture& f){f.actor.pc=0x7D5FA;f.actor.vm_current.current_pc=0x7D607;}},
        {"mixed early-message late-current form",[](Fixture& f){f.actor.vm_current.current_pc=0x7D607;}},
        {"foreign farewell source caller",[](Fixture& f){f.actor.vm_current.caller_entry=0x8DDB5;}},
        {"reused completed body frame",[](Fixture& f){f.actor.vm_current.frame_id=f.body_wait.frame_id;}},
        {"reused body caller frame",[](Fixture& f){f.actor.vm_current.frame_id=f.body_wait.caller_frame_id;}},
        {"foreign farewell generation",[](Fixture& f){++f.actor.generation;}},
        {"foreign farewell run",[](Fixture& f){++f.actor.vm_current.run_id;}}
    };
    for(const auto& test:farewell_cuts) {
        Fixture f;f.BindBody();f.CompleteBody();f.PrepareFarewell();test.second(f);f.Observe();
        Require(!f.domain.State().farewell_bound,std::string(test.first)+": no owned next farewell");
        f.PrepareFarewellReturn();f.Observe();f.PrepareMain();f.Observe();f.NoCompletion(test.first);
    }
    {
        Fixture f;f.BindBody();f.CompleteBody();f.BindFarewell();
        const auto old_sequence=f.actor.farewell_return.sequence;f.PrepareFarewellReturn();
        f.actor.farewell_return.sequence=old_sequence;f.Observe();f.PrepareMain();f.Observe();
        Require(!f.domain.State().returned,"stale sequence cannot relabel a new NIL farewell");f.NoCompletion("stale farewell");
    }
    {
        Fixture f;f.BindBody();f.CompleteBody();f.BindFarewell();
        f.actor.awaiting_message=false;++f.actor.message_acknowledgements;f.PrepareMain();f.Observe();
        Require(!f.domain.State().returned,"unchanged historical farewell never becomes a fresh return");
        f.NoCompletion("old farewell plus new GAME3");
    }
    {
        Fixture f;f.BindBody();f.PrepareFarewell();f.Observe();
        Require(!f.domain.State().farewell_bound,"farewell observed before body completion has no proof");
        f.PrepareBodyReturn();f.Observe();f.PrepareFarewellReturn();f.Observe();f.PrepareMain();f.Observe();
        f.NoCompletion("late body proof cannot fabricate an earlier farewell binding");
    }
    {
        Fixture f;f.PrepareBody();f.PrepareFarewell();f.Observe();
        f.PrepareFarewellReturn();f.Observe();f.PrepareMain();f.Observe();
        Require(f.domain.State().kind==GameConversationProofKind::None,"generic7D628 is no proof kind");
        f.NoCompletion("farewell without any body");
    }
}

inline void LegacyKindAndExistingInputShapeArePreserved() {
    Fixture f;f.PrepareBody();f.PrepareFarewell();f.actor.pc=0x7D5FA;
    f.actor.vm_current.current_pc=0x7D607;f.farewell_wait=f.actor.vm_current;
    // Deliberately keep the old API's minimal retained85438 shape, as in the
    // existing owning regression fixture. This is not an actual85438 trace.
    auto retained=State(73,0x8DDB5,0x8E000,1,800);
    f.actor.normal_return.valid=true;f.actor.normal_return.sequence=1;f.actor.normal_return.opcode_pc=0x85438;
    f.actor.normal_return.before=f.actor.normal_return.after=retained;f.actor.normal_return.after.result=Scalar(TrueWord);
    f.Observe();Require(f.domain.State().kind==GameConversationProofKind::Legacy85438
            &&f.domain.State().body_completed&&f.domain.State().farewell_bound
            &&f.domain.State().farewell_pc==0x7D5FA&&f.domain.State().farewell_current_pc==0x7D607&&!f.domain.State().body_bound,
            "unchanged legacy shape retains kind1 and exact late farewell form");
    f.PrepareFarewellReturn();f.Observe();f.PrepareMain();f.Observe();
    Require(f.domain.State().main_consumed&&f.actor.normal_return.opcode_pc==0x85438
            &&f.domain.State().kind==GameConversationProofKind::Legacy85438&&f.domain.Consume(),
            "legacy completion remains consumable without masquerading as Topic2B483");
}

inline void SecondActivationRequiresItsOwnCompletedBody() {
    Fixture f;f.BindBody();const auto a_wait=f.actor;f.CompleteBody();const auto a_return=f.actor;
    f.BindFarewell();f.PrepareFarewellReturn();f.Observe();
    f.PrepareMain();f.Observe();const auto first=f.domain.Snapshot();Require(f.domain.Consume(),"consume first Topic activation");
    f.PrepareBody(444,25,3000);f.Observe();
    Require(f.domain.State().id>first.id&&f.domain.State().body_bound&&!f.domain.State().body_completed
            &&!f.domain.Consume(),"new frame starts another pending body, not a precompleted activation");
    f.CompleteBody();f.PrepareFarewell(458);f.Observe();
    Require(f.domain.State().farewell_bound,"second body owns its independently observed next farewell");
    f.PrepareFarewellReturn();f.Observe();f.PrepareMain();f.Observe();const auto second=f.domain.Snapshot();
    Require(second.main_consumed&&second.kind==GameConversationProofKind::Topic2B483
            &&second.id>first.id&&second.body_frame!=first.body_frame&&second.message_frame!=first.message_frame
            &&second.body_sequence>first.body_sequence&&second.return_sequence>first.return_sequence
            &&second.message==26&&f.domain.Consume()&&!f.domain.Consume(),
            "second independently completed Topic body+farewell+main yields exactly one new activation");
    // Replay A after consumed A then B. Preserve A's original execution counts,
    // ACK ordinal and prior slots; deadline/executed rejection must not mask
    // the non-adjacent old body sequence replay.
    f.actor=a_wait;f.body_wait=a_wait.vm_current;f.body_message=a_wait.messages;f.Observe();
    f.actor=a_return;f.FinishPreparedBodyReturn();const auto replay=f.domain.Snapshot();
    Require((replay.id==second.id||(!replay.body_completed&&!replay.main_consumed))&&!f.domain.Consume(),
            "non-adjacent replay A after consumed A and B cannot publish another body proof or activation");
}
} // namespace dokodemo::conversation_activation_regression_detail

inline void GameConversationActivationRegressionChecks() {
    using namespace dokodemo::conversation_activation_regression_detail;
    TopicBodyFarewellAndMainAreSeparateEvidence();
    ForeignOrIncompleteBodyCannotActivate();
    SameSequenceCannotSubstituteCompletedBodyHistory();
    OrderingFormsAndStaleFarewellCannotSubstitute();
    LegacyKindAndExistingInputShapeArePreserved();
    SecondActivationRequiresItsOwnCompletedBody();
}
