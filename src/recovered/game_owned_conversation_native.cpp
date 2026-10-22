#include "game_owned_conversation_native.h"

namespace dokodemo::recovered {
namespace {
bool Scalar(const script::ValueWitness& v,uint32_t word){return v.kind==script::Value::Kind::Scalar&&v.word==word;}
bool Same(const script::ValueWitness& a,const script::ValueWitness& b){
    return a.kind==b.kind&&a.word==b.word&&a.table==b.table&&a.index==b.index
        &&a.cp932_bytes==b.cp932_bytes&&a.cp932_sha256==b.cp932_sha256;
}
bool Return(const script::FrameTransition& t,uint64_t frame,uint32_t entry,uint32_t pc,
            uint64_t caller,uint32_t caller_entry,uint32_t resume,bool envelope){
    const auto& a=t.before;const auto& b=t.after;
    return t.opcode==0x31&&a.frame_id==frame&&a.code_entry==entry&&a.current_pc==pc
        &&a.caller_frame_id==caller&&a.caller_entry==caller_entry&&a.caller_return_pc==resume
        &&b.frame_id==caller&&b.code_entry==caller_entry&&b.current_pc==resume
        &&a.event_envelope_present==envelope&&b.event_envelope_present==envelope
        &&a.call_depth==b.call_depth+1&&b.executed==a.executed+1&&a.clock_tick==b.clock_tick
        &&a.deadline_tick==b.deadline_tick&&a.relative_deadline23==b.relative_deadline23
        &&a.active_blocks==b.active_blocks&&a.event_scope_depth==b.event_scope_depth
        &&b.operand_cells==a.caller_stack_base&&Same(a.result,b.result);
}
}
bool GameOwnedConversation::Begin(const script::VmStateWitness& s,uint64_t gen,uint64_t origin,uint64_t receipt,uint64_t main)noexcept{
    if(!gen||!origin||!main||receipt<=last_receipt_||!s.valid||!s.context_registered||s.stopped
       ||s.event_envelope_present||s.queued_callbacks||(state_.accepted&&!state_.completed&&!state_.failed))return false;
    state_={};state_.origin=GameConversationOrigin::Returned;state_.accepted=true;state_.receipt=last_receipt_=receipt;
    state_.generation=gen;state_.origin_activation=origin;state_.run_id=s.run_id;
    state_.context_word=s.context_word;state_.code_sha256=s.code_sha256;state_.main_frame=main;
    state_.suspended_frame=s.frame_id;state_.accepted_executed=s.executed;suspended_=s;return true;
}
bool GameOwnedConversation::BeginPostModal(const script::VmStateWitness& s,uint64_t gen,uint64_t post,uint64_t inventory,
                                         uint64_t sequence,uint64_t receipt,uint64_t main)noexcept{
    if(post_modal_claimed_||!post||!inventory||!sequence||!Begin(s,gen,post,receipt,main))return false;
    post_modal_claimed_=true;state_.origin=GameConversationOrigin::PostModal;state_.origin_activation=0;
    state_.post_modal_generation=post;state_.inventory_generation=inventory;state_.input_sequence=sequence;return true;
}
bool GameOwnedConversation::Owner(const script::VmStateWitness& v)const noexcept{
    return v.valid&&v.context_registered&&!v.stopped&&v.run_id==state_.run_id
        &&v.context_word==state_.context_word&&v.code_sha256==state_.code_sha256;
}
void GameOwnedConversation::Observe(const script::FrameTransition& t,const script::Vm& vm)noexcept{
    if(!state_.accepted||state_.failed||state_.completed)return;
    const auto& a=t.before;const auto& b=t.after;
    if(!Owner(a)||!Owner(b)){state_.failed=true;return;}
    if(t.callback_receipt){
        if(t.callback_receipt!=state_.receipt){state_.failed=true;return;}
        // True event0 arm: real setter call(2,integer0), not its fallback GAME3.
        if(a.code_entry==0x8DA28&&a.current_pc==0x8DA44&&b.code_entry==0x8D4CC){
            if(t.opcode!=0x20||!a.event_envelope_present||!b.event_envelope_present||b.frame_captured<2
               ||a.frame_captured<2||!Scalar(a.frame_values[0],0x20000000)||!Scalar(a.frame_values[1],0x20000000)
               ||!Scalar(b.frame_values[0],0x20000002)||!Scalar(b.frame_values[1],0x20000000))state_.failed=true;
            else{state_.callback_frame=a.frame_id;state_.setter_frame=b.frame_id;}
        }
        if(a.code_entry==0x8D4CC&&a.current_pc==0x8D523&&state_.setter_frame){
            if(!Return(t,state_.setter_frame,0x8D4CC,0x8D523,state_.callback_frame,0x8DA28,0x8DA49,true))state_.failed=true;
            else try{
                const auto phase=vm.read_slot(script::Table::Global,0x9A76),arg=vm.read_slot(script::Table::Global,0x9A77);
                state_.phase_written=phase.kind==script::Value::Kind::Scalar&&phase.word==0x20000002
                    &&arg.kind==script::Value::Kind::Scalar&&arg.word==0x20000000;
                if(!state_.phase_written)state_.failed=true;
            }catch(...){state_.failed=true;}
        }
        if(t.opcode==0x1C||t.opcode==0x32){
            if(!state_.phase_written||t.opcode!=0x1C||a.current_pc!=0x8DA49||a.frame_id!=state_.callback_frame
               ||!a.event_envelope_present||b.event_envelope_present||b.frame_id!=suspended_.frame_id
               ||b.code_entry!=suspended_.code_entry||b.current_pc!=suspended_.current_pc
               ||!Same(b.result,suspended_.result)||b.operand_cells!=suspended_.operand_cells
               ||b.active_blocks!=suspended_.active_blocks)state_.failed=true;
            else{state_.callback_returned=true;state_.callback_executed=b.executed;}
        }
        return;
    }
    if(a.event_envelope_present||b.event_envelope_present){state_.failed=true;return;}
    if(state_.callback_returned&&!state_.owner_started)try{
        const auto phase=vm.read_slot(script::Table::Global,0x9A76),arg=vm.read_slot(script::Table::Global,0x9A77);
        if(phase.kind!=script::Value::Kind::Scalar||phase.word!=0x20000002
           ||arg.kind!=script::Value::Kind::Scalar||arg.word!=0x20000000){state_.failed=true;return;}
    }catch(...){state_.failed=true;return;}
    if(a.code_entry==0x8DDB5&&a.current_pc==0x8DF29&&b.code_entry==0x89F86){
        if(!state_.callback_returned||state_.owner_started||a.frame_id!=state_.main_frame
           ||a.executed<=state_.callback_executed||t.opcode!=0x21||b.caller_return_pc!=0x8DF2D)state_.failed=true;
        else{state_.owner_started=true;state_.owner_frame=b.frame_id;}
    }
    if(state_.owner_started&&a.code_entry==0x89F86&&a.current_pc==0x89FC3){
        if(!state_.owner_started||!Return(t,state_.owner_frame,0x89F86,0x89FC3,state_.main_frame,0x8DDB5,0x8DF2D,false))state_.failed=true;
        else{state_.owner_returned=true;state_.owner_executed=b.executed;state_.owner_result=b.result;state_.owner_return_pc=b.current_pc;}
    }
    if(a.code_entry==0x8DDB5&&a.current_pc==0x8E024&&b.code_entry==0x8D932){
        if(!state_.owner_returned||a.frame_id!=state_.main_frame||t.opcode!=0x20||b.frame_captured<1
           ||!Scalar(b.frame_values[0],0x10000000)||b.caller_return_pc!=0x8E029)state_.failed=true;
        else{state_.cleanup_started=true;state_.cleanup_frame=b.frame_id;}
    }
    if(state_.cleanup_started&&a.code_entry==0x8D932&&a.current_pc==0x8D9C9){
        if(!state_.game3||!Return(t,state_.cleanup_frame,0x8D932,0x8D9C9,state_.main_frame,0x8DDB5,0x8E029,false))state_.failed=true;
        else{state_.completed=true;state_.completed_executed=b.executed;state_.cleanup_return_pc=b.current_pc;}
    }
}
void GameOwnedConversation::Game3(const script::VmStateWitness& v)noexcept{
    if(state_.accepted&&!state_.failed&&!state_.completed&&state_.cleanup_started&&Owner(v)
       &&v.frame_id==state_.cleanup_frame&&v.code_entry==0x8D932&&v.current_pc==0x8D9B5
       &&v.caller_frame_id==state_.main_frame&&v.executed>state_.owner_executed&&!v.event_envelope_present){
        state_.game3=true;state_.game3_executed=v.executed;
    }
}
GameOwnedConversationSnapshot GameOwnedConversation::Snapshot(const script::Vm& vm)const noexcept{
    auto out=state_;const auto v=vm.current_witness();
    out.main_live=out.accepted&&!out.failed&&Owner(v)&&!vm.frame_observer_failed()
        &&vm.owns_active_frame(out.main_frame,0x8DDB5);
    // Actual phase1 continuation returns13856 ->138A9 -> main8DEEA,
    // then jumps8E063, bypassing the phase-resetting conversation cleanup.
    // Frame liveness alone is not permission from an arbitrary nested wait.
    out.dispatch_ready=out.main_live&&out.completed&&DispatchReady(vm,out.main_frame);
    return out;
}
bool GameOwnedConversation::DispatchReady(const script::Vm& vm,uint64_t main)noexcept{
    const auto v=vm.current_witness();
    if(!v.valid||v.stopped||!v.context_registered||v.event_envelope_present||v.queued_callbacks
       ||vm.frame_observer_failed()||!vm.owns_active_frame(main,0x8DDB5))return false;
    try{
        const auto phase=vm.read_slot(script::Table::Global,0x9A76);
        return phase.kind==script::Value::Kind::Scalar&&phase.word==0x20000001
            &&v.code_entry==0x1383D&&v.current_pc==0x13856&&v.caller_entry==0x1387B
            &&v.caller_call_pc==0x138A5&&v.caller_return_pc==0x138A9
            &&vm.active_call_matches(main,0x8DDB5,0x8DEE6,0x8DEEA,v.caller_frame_id,0x1387B);
    }catch(...){return false;}
}
}
