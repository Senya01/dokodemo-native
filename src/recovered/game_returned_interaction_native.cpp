#include "game_returned_interaction_native.h"
#include <atomic>
#include <tuple>

namespace dokodemo::recovered {
namespace {
std::atomic<uint64_t> next_owner{1};
bool Scalar(const script::ValueWitness& v,uint32_t word){return v.kind==script::Value::Kind::Scalar&&v.word==word;}
bool SameCode(const script::VmStateWitness& a,const script::VmStateWitness& b){return a.code_sha256==b.code_sha256;}
bool SameValue(const script::ValueWitness& a,const script::ValueWitness& b){
    return std::tie(a.kind,a.word,a.table,a.index,a.cp932_bytes,a.cp932_sha256)==std::tie(b.kind,b.word,b.table,b.index,b.cp932_bytes,b.cp932_sha256);
}
bool SameState(const script::VmStateWitness& a,const script::VmStateWitness& b){
    if(std::tie(a.valid,a.context_registered,a.event_envelope_present,a.stopped,a.run_id,a.frame_id,a.executed,a.clock_tick,a.deadline_tick,
        a.relative_deadline23,a.current_pc,a.code_entry,a.context_word,a.call_depth,a.active_blocks,a.event_scope_depth,a.queued_callbacks,
        a.caller_entry,a.caller_call_pc,a.caller_return_pc,a.caller_frame_id,a.caller_stack_base,a.frame_cells,a.operand_cells,a.frame_captured,a.operands_captured,a.code_sha256)
       !=std::tie(b.valid,b.context_registered,b.event_envelope_present,b.stopped,b.run_id,b.frame_id,b.executed,b.clock_tick,b.deadline_tick,
        b.relative_deadline23,b.current_pc,b.code_entry,b.context_word,b.call_depth,b.active_blocks,b.event_scope_depth,b.queued_callbacks,
        b.caller_entry,b.caller_call_pc,b.caller_return_pc,b.caller_frame_id,b.caller_stack_base,b.frame_cells,b.operand_cells,b.frame_captured,b.operands_captured,b.code_sha256))return false;
    if(!SameValue(a.result,b.result))return false;
    for(size_t i=0;i<a.frame_values.size();++i)if(!SameValue(a.frame_values[i],b.frame_values[i])||!SameValue(a.operands[i],b.operands[i]))return false;
    return true;
}
bool SameReturn(const script::NormalReturnWitness& a,const script::NormalReturnWitness& b){
    return a.valid==b.valid&&a.sequence==b.sequence&&a.opcode_pc==b.opcode_pc&&SameState(a.before,b.before)&&SameState(a.after,b.after);
}
}
GameFsStatus GameReturnedConversationInteraction::Initialize(Resolve resolve,Dispatch dispatch){
    if(state_.live||state_.revoked||!resolve||!dispatch)
        return {GameFsCode::Unavailable,0,"Returned interaction requires a fresh owner and dispatch"};
    resolve_=std::move(resolve);dispatch_=std::move(dispatch);
    state_.owner=next_owner.fetch_add(1);state_.live=true;return {};
}
void GameReturnedConversationInteraction::Revoke(std::string reason){
    state_.live=state_.eligible=state_.armed=false;state_.revoked=true;
    state_.error=std::move(reason);input_.Stop();target_={};resolve_={};dispatch_={};
}
bool GameReturnedConversationInteraction::SameTarget()const{
    if(!resolve_||target_.context.context_owner.expired())return false;
    const auto now=resolve_();
    return now.generation==target_.generation&&now.context.kind==script::Value::Kind::Context
        &&now.context.word==target_.context.word&&now.context.context_owner.lock()==target_.context.context_owner.lock()
        &&now.callback.kind==script::Value::Kind::Code&&now.callback.word==target_.callback.word;
}
bool GameReturnedConversationInteraction::Ready(const GameActorScriptSnapshot& s,const GameMainFlowSnapshot& f,const GameReturnedInteractionGate& g)const{
    const auto& v=s.vm_current;
    return s.live&&s.started&&!s.completed&&!s.blocked&&s.error.empty()&&s.context_enabled
        &&!s.awaiting_message&&!s.awaiting_choice&&!s.inventory_requested&&!s.source_main_input_suppressed48
        &&v.valid&&v.context_registered&&!v.stopped&&!v.event_envelope_present&&s.callback_depth==0&&s.queued_callbacks==0
        &&s.callbacks_posted==s.callbacks_returned&&v.code_entry==0x8DDB5&&v.call_depth==1
        &&v.run_id==state_.run_id&&s.generation==state_.script_generation
        &&g.ui_idle&&g.inventory_retired&&g.old_post_modal_consumed&&g.source469
        &&f.live&&f.phase1C==0&&f.substate24==0&&f.return20==0&&f.outer_returns>0;
}
GameFsStatus GameReturnedConversationInteraction::Update(const GameActorScriptSnapshot& s,const GameMainFlowSnapshot& f,const GameReturnedInteractionGate& g){
    if(!state_.live)return {};
    state_.source_gate_blocked=g.source469&&f.phase1C==0;
    if(state_.candidate&&(s.generation!=state_.script_generation||!s.live||s.vm_current.run_id!=state_.run_id||s.sha256!=state_.code_sha256)){
        Revoke("Returned activation script generation/run/code retired or replaced");return {};
    }
    if(state_.event_posted&&s.callbacks_returned>callback_baseline_){state_.callback_returned=true;state_.returned_callbacks=s.callbacks_returned-callback_baseline_;}
    const auto& v=s.vm_current;const auto& r=s.farewell_return;
    // Capture while the real message is still waiting,BEFORE its host ACK.
    if(!s.blocked&&s.awaiting_message&&!s.awaiting_choice&&s.pc==0x7D5FA&&v.valid&&v.code_entry==0x7D548
       &&v.current_pc==0x7D607&&v.frame_id!=last_activation_frame_
       &&(!state_.candidate||s.normal_return.sequence>state_.retained_sequence)
       &&s.normal_return.valid&&s.normal_return.opcode_pc==0x85438
       &&s.normal_return.before.run_id==v.run_id&&SameCode(s.normal_return.before,v)
       &&Scalar(s.normal_return.after.result,0x10000001u)){
        last_activation_frame_=v.frame_id;message_witness_=v;ack_baseline_=s.message_acknowledgements;
        state_.candidate=true;state_.returned=state_.admitted=state_.eligible=state_.armed=false;
        state_.event_posted=state_.callback_returned=false;state_.queue_result=-1;state_.error.clear();
        state_.script_generation=s.generation;state_.run_id=v.run_id;state_.code_sha256=s.sha256;
        state_.message=s.messages;state_.message_frame=v.frame_id;state_.message_context=g.message_context;
        state_.caller_frame=v.caller_frame_id;state_.caller_entry=v.caller_entry;state_.caller_pc=v.caller_return_pc;
        state_.prior_sequence=r.sequence;state_.retained_sequence=s.normal_return.sequence;state_.game3_baseline=s.game3_calls;
        retained_witness_=s.normal_return;
        state_.return_sequence=0;input_.Stop();target_={};
    }
    if(!state_.candidate)return {};
    if(!state_.event_posted&&(!SameReturn(s.normal_return,retained_witness_)
       ||(state_.returned&&!SameReturn(r,accepted_return_)))){
        Revoke("Returned activation retained history changed before consumption");return {};
    }
    if(!state_.event_posted)callback_baseline_=s.callbacks_returned;
    if(!state_.returned&&s.message_acknowledgements==ack_baseline_+1&&s.messages==state_.message
       &&r.valid&&r.opcode_pc==0x7D628&&r.sequence>state_.prior_sequence
       &&s.normal_return.sequence==state_.retained_sequence){
        const auto& a=r.before;const auto& b=r.after;
        if(a.run_id==state_.run_id&&b.run_id==state_.run_id&&SameCode(a,message_witness_)&&SameCode(b,a)
           &&a.frame_id==state_.message_frame&&a.code_entry==0x7D548&&a.current_pc==0x7D628
           &&a.executed>message_witness_.executed&&a.caller_frame_id==state_.caller_frame
           &&a.caller_entry==state_.caller_entry&&a.caller_return_pc==state_.caller_pc
           &&b.frame_id==state_.caller_frame&&b.code_entry==state_.caller_entry&&b.current_pc==state_.caller_pc
           &&a.call_depth==b.call_depth+1&&b.executed==a.executed+1
           &&a.clock_tick==b.clock_tick&&a.deadline_tick==b.deadline_tick&&a.relative_deadline23==b.relative_deadline23
           &&Scalar(a.result,0)&&Scalar(b.result,0)&&b.frame_captured>1&&Scalar(b.frame_values[1],0x10000001u)){
            state_.returned=true;state_.return_sequence=r.sequence;
            accepted_return_=r;
        }
    }
    const auto& game=s.game3_execution;
    const bool new_main_consumer=state_.returned&&s.game3_calls>state_.game3_baseline&&game.valid
        &&game.run_id==state_.run_id&&SameCode(game,r.after)&&game.executed>r.after.executed
        &&game.code_entry==0x8D932&&game.current_pc==0x8D9B5&&game.call_depth==2&&!game.event_envelope_present
        &&game.caller_entry==0x8DDB5&&game.caller_frame_id==v.frame_id
        &&game.caller_call_pc==0x8E024&&game.caller_return_pc==0x8E029;
    if(!state_.admitted&&new_main_consumer&&g.manager9&&Ready(s,f,g)){
        try{
            target_=resolve_();
            if(target_.generation!=s.generation||target_.context.kind!=script::Value::Kind::Context
               ||target_.context.word!=0x80000000u||target_.context.context_owner.expired()
               ||target_.callback.kind!=script::Value::Kind::Code||target_.callback.word!=0x7008DA28u){
                Revoke("Returned activation Local0/context registry identity refused");return {};
            }
        }catch(const std::exception& e){Revoke(e.what());return {};}
        ++state_.generation;state_.admitted=true;state_.game3_calls=s.game3_calls;state_.game3_pc=game.current_pc;
        state_.context_word=target_.context.word;state_.callback_word=target_.callback.word;
        state_.message_context=g.message_context;input_.Begin();
    }
    const bool eligible=state_.admitted&&!state_.event_posted&&Ready(s,f,g);
    if(!eligible){input_.Stop();state_.armed=false;}
    else if(!state_.eligible)input_.Begin(); // same token;new release needed after any busy cut
    state_.eligible=eligible;
    return {};
}
bool GameReturnedConversationInteraction::Input(const GameInputFrame& frame){
    if(frame.overflow_resync){if(state_.eligible)input_.Begin();else input_.Stop();state_.armed=false;++state_.rejected;return false;}
    if(frame.sequence<=state_.last_sequence){++state_.rejected;return false;}
    state_.last_sequence=frame.sequence;
    if(!state_.live||!state_.eligible||frame.returned_owner!=state_.owner||frame.returned_generation!=state_.generation
       ||frame.message_owner_generation!=state_.script_generation||frame.message_context!=state_.message_context){++state_.rejected;return false;}
    try{if(!SameTarget()){Revoke("Returned input registry/callback replaced or retired");++state_.rejected;return false;}}
    catch(const std::exception& e){Revoke(e.what());++state_.rejected;return false;}
    const bool edge=input_.Observe(frame,Circle);state_.armed=input_.Armed();if(!edge)return false;
    // Queue refusal retains the activation token,but requires another release
    // and press. Registry/lifetime refusal is terminal. Never retry a held edge.
    int32_t result=-1;GameFsStatus status;
    try{status=dispatch_(target_,result);}
    catch(const std::exception& e){Revoke(e.what());++state_.rejected;return false;}
    state_.queue_result=result;
    if(!status){Revoke(status.detail);++state_.rejected;return false;}
    if(result==1){++state_.rejected;input_.Begin();state_.armed=false;return false;}
    if(result!=0){Revoke("Returned event owner retired or returned an unknown queue code");++state_.rejected;return false;}
    state_.event_posted=true;state_.eligible=state_.armed=false;++state_.posted;input_.Stop();return true;
}
void GameReturnedConversationInteraction::CheckInputOwners(const GameActorScriptSnapshot& s,const GameMainFlowSnapshot& f,const GameReturnedInteractionGate& g){
    if(state_.candidate&&(s.generation!=state_.script_generation||!s.live||s.vm_current.run_id!=state_.run_id||s.sha256!=state_.code_sha256)){
        Revoke("Returned input script owner retired or replaced");return;
    }
    if(state_.admitted&&!state_.event_posted&&(!SameReturn(s.normal_return,retained_witness_)||!SameReturn(s.farewell_return,accepted_return_))){
        Revoke("Returned input retained history changed");return;
    }
    // This path can only disarm. Admission and publication happen in Tick.
    if(!Ready(s,f,g)){state_.eligible=state_.armed=false;input_.Stop();}
}
void GameReturnedConversationInteraction::Close(){Revoke("Returned interaction owner retired");}
}
