#include "game_conversation_activation_native.h"
#include <tuple>
#include <algorithm>

namespace dokodemo::recovered {
namespace {
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
bool CodeLabel(const GameActorScriptSnapshot& s){
    const auto& code=s.vm_current.code_sha256;
    return s.sha256.size()==64&&code.back()==0&&std::equal(code.begin(),code.end()-1,s.sha256.begin());
}
bool Live(const GameActorScriptSnapshot& s){
    const auto& v=s.vm_current;
    return s.live&&s.started&&!s.blocked&&!s.completed&&s.error.empty()&&v.valid&&!v.stopped&&v.context_registered&&CodeLabel(s);
}
bool Ordinary(const script::NormalReturnWitness& r,const script::VmStateWitness& bound,uint32_t pc,uint32_t result){
    const auto& a=r.before;const auto& b=r.after;
    if(a.operands_captured>a.operands.size()||b.operands_captured>b.operands.size()
       ||a.frame_captured>a.frame_values.size()||b.frame_captured>b.frame_values.size()
       ||b.operands_captured>a.operands_captured)return false;
    if(!r.valid||r.opcode_pc!=pc||!a.valid||!b.valid||a.stopped||b.stopped
       ||a.event_envelope_present||b.event_envelope_present||!a.context_registered||!b.context_registered
       ||a.run_id!=bound.run_id||b.run_id!=bound.run_id||!SameCode(a,bound)||!SameCode(b,a)
       ||a.context_word!=bound.context_word||b.context_word!=a.context_word
       ||a.frame_id!=bound.frame_id||a.code_entry!=bound.code_entry||a.current_pc!=pc
       ||a.executed<=bound.executed||a.caller_frame_id!=bound.caller_frame_id
       ||a.caller_entry!=bound.caller_entry||a.caller_call_pc!=bound.caller_call_pc||a.caller_return_pc!=bound.caller_return_pc
       ||b.frame_id!=a.caller_frame_id||b.code_entry!=a.caller_entry||b.current_pc!=a.caller_return_pc
       ||a.call_depth!=b.call_depth+1||b.executed!=a.executed+1
       ||a.clock_tick!=b.clock_tick||a.deadline_tick!=b.deadline_tick||a.relative_deadline23!=b.relative_deadline23
       ||a.active_blocks!=b.active_blocks||a.event_scope_depth!=b.event_scope_depth
       ||b.operand_cells!=a.caller_stack_base||!Scalar(a.result,result)||!SameValue(a.result,b.result))return false;
    for(size_t i=0;i<b.operands_captured;++i)if(!SameValue(a.operands[i],b.operands[i]))return false;
    return true;
}
}
void GameConversationActivation::Start(){if(!state_.revoked&&!state_.live)state_.live=true;}
void GameConversationActivation::Fail(std::string error){state_.live=false;state_.revoked=true;state_.error=std::move(error);}
void GameConversationActivation::Begin(GameConversationProofKind kind,const GameActorScriptSnapshot& s){
    const auto id=state_.id+1;state_={};state_.id=id;state_.kind=kind;state_.live=true;
    state_.script_generation=s.generation;state_.run_id=s.vm_current.run_id;state_.code_sha256=s.sha256;
    state_.context_word=s.vm_current.context_word;
    body_wait_={};farewell_wait_={};prior_body_={};body_return_={};prior_farewell_={};farewell_return_={};
}
void GameConversationActivation::BindFarewell(const GameActorScriptSnapshot& s,uint64_t context){
    const auto& v=s.vm_current;farewell_wait_=v;prior_farewell_=s.farewell_return;farewell_ack_=s.message_acknowledgements;
    last_farewell_frame_=v.frame_id;state_.farewell_bound=true;state_.message=s.messages;
    state_.message_frame=v.frame_id;state_.message_context=context;state_.farewell_pc=s.pc;
    state_.farewell_current_pc=v.current_pc;
    state_.caller_frame=v.caller_frame_id;state_.caller_entry=v.caller_entry;state_.caller_pc=v.caller_return_pc;
    state_.prior_sequence=s.farewell_return.sequence;state_.game3_baseline=s.game3_calls;
}
bool GameConversationActivation::HistoryMatches(const GameActorScriptSnapshot& s)const{
    if(!state_.id)return true;
    // A later busy/stopped cut does not erase an already observed return.
    // Registry and runnable-state admission are checked separately by the
    // main-consumer boundary and Windows input owner, before any dispatch.
    if(!s.live||!s.started||!s.vm_current.valid||!CodeLabel(s)||s.generation!=state_.script_generation
       ||s.vm_current.run_id!=state_.run_id||s.sha256!=state_.code_sha256
       ||s.vm_current.context_word!=state_.context_word)return false;
    if(state_.consumed)return true;
    if(state_.kind==GameConversationProofKind::OwnedMain){
        const auto& o=s.owned_conversation;
        return o.accepted&&!o.failed&&o.completed&&o.receipt==owned_completion_.receipt
            &&o.origin_activation==owned_completion_.origin_activation&&o.main_frame==owned_completion_.main_frame
            &&o.owner_frame==owned_completion_.owner_frame&&o.cleanup_frame==owned_completion_.cleanup_frame
            &&o.owner_executed==owned_completion_.owner_executed&&o.game3_executed==owned_completion_.game3_executed
            &&o.completed_executed==owned_completion_.completed_executed&&SameValue(o.owner_result,owned_completion_.owner_result);
    }
    if(state_.body_completed&&!SameReturn(s.normal_return,body_return_))return false;
    if(state_.returned&&!SameReturn(s.farewell_return,farewell_return_))return false;
    return true;
}
void GameConversationActivation::Observe(const GameActorScriptSnapshot& s,uint64_t context){
    if(!state_.live)return;
    if(!HistoryMatches(s)){Fail("Conversation activation owner or frozen completion history changed");return;}
    const auto& v=s.vm_current;const auto& r=s.normal_return;
    const auto& own=s.owned_conversation;
    if(owned_pending_receipt_&&(!own.accepted||own.origin_activation!=state_.id||own.receipt!=owned_pending_receipt_)){
        Fail("Pending owned conversation receipt disappeared or changed origin");return;
    }
    if(state_.consumed&&own.accepted&&own.origin_activation==state_.id){
        if(!owned_pending_receipt_)owned_pending_receipt_=own.receipt;
        if(own.receipt!=owned_pending_receipt_||own.receipt<=last_owned_receipt_||own.failed
           ||own.generation!=state_.script_generation||own.run_id!=state_.run_id||own.code_sha256!=v.code_sha256){
            Fail("Owned conversation receipt replaced, replayed or failed");return;
        }
        // This pending source transaction excludes unrelated Topic/Legacy
        // history. A successor is earned only by its complete common owner.
        if(!own.completed)return;
        if(!own.phase_written||!own.callback_returned||!own.owner_started||!own.owner_returned
           ||!own.cleanup_started||!own.game3||own.owner_return_pc!=0x8DF2D||own.cleanup_return_pc!=0x8E029
           ||own.owner_executed<=own.callback_executed||own.game3_executed<=own.owner_executed
           ||own.completed_executed<=own.game3_executed){Fail("Owned conversation completion is incomplete");return;}
        const auto origin=state_.id;Begin(GameConversationProofKind::OwnedMain,s);
        owned_completion_=own;state_.owned_receipt=last_owned_receipt_=own.receipt;owned_pending_receipt_=0;
        state_.origin_activation=origin;state_.body_bound=state_.body_completed=state_.returned=state_.main_consumed=true;
        state_.message_context=context; // retain the actual published input context across later Update calls
        state_.body_frame=own.owner_frame;state_.body_caller_frame=own.main_frame;
        state_.body_caller_entry=0x8DDB5;state_.body_caller_pc=0x8DF2D;
        state_.game3_calls=s.game3_calls;state_.game3_pc=0x8D9B5;return;
    }
    const bool may_begin=!state_.id||state_.consumed;
    if(may_begin&&Live(s)&&s.awaiting_message&&!s.awaiting_choice&&s.pc==0x2B462
       &&v.code_entry==0x2B372&&v.current_pc==0x2B471&&v.caller_entry==0x2660
       &&v.caller_call_pc==0x267F&&v.caller_return_pc==0x2680&&v.frame_id!=last_body_frame_
       &&!v.event_envelope_present&&s.messages==s.message_acknowledgements+1){
        Begin(GameConversationProofKind::Topic2B483,s);state_.body_bound=true;body_wait_=v;prior_body_=r;
        last_body_frame_=v.frame_id;state_.body_frame=v.frame_id;state_.body_caller_frame=v.caller_frame_id;
        state_.body_prior_sequence=r.sequence;state_.body_message=s.messages;state_.body_ack=s.message_acknowledgements;
        state_.body_pc=s.pc;state_.body_caller_entry=v.caller_entry;state_.body_caller_pc=v.caller_return_pc;
    }
    // Preserve the previously accepted retained85438 contract verbatim. The
    // Topic proof instead requires a pre-ACK body binding and an actual return.
    if(may_begin&&Live(s)&&s.awaiting_message&&!s.awaiting_choice&&s.pc==0x7D5FA
       &&v.code_entry==0x7D548&&v.current_pc==0x7D607&&v.frame_id!=last_farewell_frame_
       &&(!state_.id||r.sequence>last_body_sequence_)&&r.before.frame_id!=last_body_frame_
       &&r.valid&&r.opcode_pc==0x85438&&r.before.run_id==v.run_id&&SameCode(r.before,v)&&Scalar(r.after.result,0x10000001)){
        Begin(GameConversationProofKind::Legacy85438,s);body_return_=r;state_.body_completed=true;
        state_.body_frame=r.before.frame_id;state_.body_caller_frame=r.after.frame_id;state_.body_sequence=r.sequence;
        last_body_frame_=r.before.frame_id;last_body_sequence_=r.sequence;
        state_.body_caller_entry=r.after.code_entry;state_.body_caller_pc=r.after.current_pc;BindFarewell(s,context);
    }
    if(!state_.id||state_.consumed)return;
    if(state_.body_bound&&!state_.body_completed&&!SameReturn(r,prior_body_)){
        if(s.message_acknowledgements==state_.body_ack+1&&s.messages>=state_.body_message&&s.messages<=state_.body_message+1
           &&r.sequence>state_.body_prior_sequence&&r.sequence>last_body_sequence_
           &&Ordinary(r,body_wait_,0x2B483,0x10000001)){
            body_return_=r;state_.body_completed=true;state_.body_sequence=r.sequence;
            last_body_sequence_=r.sequence;
        }else{Fail("Conversation ordinary return does not complete the bound source body");return;}
    }
    if(state_.kind==GameConversationProofKind::Topic2B483&&state_.body_completed&&!state_.farewell_bound
       &&Live(s)&&s.awaiting_message&&!s.awaiting_choice&&s.pc==0x7D577&&v.code_entry==0x7D548&&v.current_pc==0x7D581
       &&s.messages==state_.body_message+1&&s.message_acknowledgements==state_.body_message
       &&v.frame_id!=last_farewell_frame_&&v.frame_id!=state_.body_frame&&v.frame_id!=state_.body_caller_frame
       &&v.executed>body_return_.after.executed
       &&v.caller_entry==0x899F9&&v.caller_call_pc==0x89A1D&&v.caller_return_pc==0x89A21){
        BindFarewell(s,context);
    }
    if(!state_.farewell_bound)return;
    const auto& farewell=s.farewell_return;
    if(!state_.returned&&!SameReturn(farewell,prior_farewell_)){
        // Legacy's already accepted synthetic/API contract does not project
        // every source caller field; keep its exact relational requirements.
        const auto& a=farewell.before;const auto& b=farewell.after;
        const bool identity=a.run_id==state_.run_id&&b.run_id==state_.run_id&&SameCode(a,farewell_wait_)&&SameCode(b,a)
            &&a.frame_id==state_.message_frame&&a.code_entry==0x7D548&&a.current_pc==0x7D628
            &&a.executed>farewell_wait_.executed&&a.caller_frame_id==state_.caller_frame
            &&a.caller_entry==state_.caller_entry&&a.caller_return_pc==state_.caller_pc
            &&b.frame_id==state_.caller_frame&&b.code_entry==state_.caller_entry&&b.current_pc==state_.caller_pc
            &&a.call_depth==b.call_depth+1&&b.executed==a.executed+1
            &&a.clock_tick==b.clock_tick&&a.deadline_tick==b.deadline_tick&&a.relative_deadline23==b.relative_deadline23
            &&Scalar(a.result,0)&&Scalar(b.result,0)&&b.frame_captured>1&&Scalar(b.frame_values[1],0x10000001);
        const bool topic=state_.kind!=GameConversationProofKind::Topic2B483||Ordinary(farewell,farewell_wait_,0x7D628,0);
        if(s.message_acknowledgements==farewell_ack_+1&&s.messages==state_.message&&farewell.valid
           &&farewell.opcode_pc==0x7D628&&farewell.sequence>state_.prior_sequence&&identity&&topic){
            farewell_return_=farewell;state_.returned=true;state_.return_sequence=farewell.sequence;
        }else{Fail("Conversation farewell does not complete this owned activation");return;}
    }
    const auto& game=s.game3_execution;
    if(state_.returned&&!state_.main_consumed&&s.game3_calls>state_.game3_baseline&&game.valid
       &&game.run_id==state_.run_id&&SameCode(game,farewell_return_.after)&&game.executed>farewell_return_.after.executed
       &&game.code_entry==0x8D932&&game.current_pc==0x8D9B5&&game.call_depth==2&&!game.event_envelope_present
       &&game.context_registered&&!game.stopped&&game.context_word==state_.context_word&&game.frame_id!=v.frame_id
       &&game.caller_entry==0x8DDB5&&game.caller_frame_id==v.frame_id&&game.caller_call_pc==0x8E024&&game.caller_return_pc==0x8E029
       &&v.valid&&v.context_registered&&!v.stopped&&!v.event_envelope_present&&v.code_entry==0x8DDB5&&v.call_depth==1){
        if(state_.kind==GameConversationProofKind::Topic2B483&&v.executed<=game.executed)return;
        state_.main_consumed=true;state_.game3_calls=s.game3_calls;state_.game3_pc=game.current_pc;
    }
}
bool GameConversationActivation::Consume()noexcept{
    if(!state_.live||!state_.main_consumed||state_.consumed)return false;
    state_.consumed=true;return true;
}
void GameConversationActivation::Close(){if(!state_.revoked)Fail("Conversation activation owner retired");}
}
