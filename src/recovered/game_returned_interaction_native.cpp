#include "game_returned_interaction_native.h"
#include <atomic>

namespace dokodemo::recovered {
namespace {
std::atomic<uint64_t> next_owner{1};

}
GameFsStatus GameReturnedConversationInteraction::Initialize(Resolve resolve,Dispatch dispatch){
    if(state_.live||state_.revoked||!resolve||!dispatch)
        return {GameFsCode::Unavailable,0,"Returned interaction requires a fresh owner and dispatch"};
    resolve_=std::move(resolve);dispatch_=std::move(dispatch);
    state_.owner=next_owner.fetch_add(1);state_.live=true;activation_.Start();return {};
}
void GameReturnedConversationInteraction::Revoke(std::string reason){
    state_.live=state_.eligible=state_.armed=false;state_.revoked=true;
    state_.error=std::move(reason);input_.Stop();target_={};resolve_={};dispatch_={};activation_.Close();state_.activation=activation_.Snapshot();
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
    if(state_.menu_only){
        if(!RefreshCompletedOutingMenu())return false;
        const auto proof=menu_script_->completed_outing_menu_.Snapshot();
        return proof.id==state_.outing_menu.id&&proof.ready&&!proof.borrowed&&!proof.retired&&!proof.revoked
            &&s.generation==proof.script_generation&&v.run_id==proof.run&&v.context_word==proof.context
            &&s.live&&!s.blocked&&!s.completed&&s.context_enabled&&!s.awaiting_message&&!s.awaiting_choice&&!s.awaiting_input
            &&!s.source_main_input_suppressed48&&!s.inventory_requested&&!s.callback_depth&&!s.queued_callbacks
            &&s.callbacks_posted==s.callbacks_returned&&g.ui_idle&&g.inventory_retired&&g.old_post_modal_consumed&&g.source469
            &&f.live&&!f.phase1C&&!f.substate24&&!f.return20;
    }
    const bool owned=activation_.State().kind==GameConversationProofKind::OwnedMain;
    const bool main=owned?s.owned_conversation.dispatch_ready:(v.code_entry==0x8DDB5&&v.call_depth==1);
    return s.live&&s.started&&!s.completed&&!s.blocked&&s.error.empty()&&s.context_enabled
        &&!s.awaiting_message&&!s.awaiting_choice&&!s.awaiting_input&&!s.inventory_requested&&!s.source_main_input_suppressed48
        &&v.valid&&v.context_registered&&!v.stopped&&!v.event_envelope_present&&s.callback_depth==0&&s.queued_callbacks==0
        &&s.callbacks_posted==s.callbacks_returned&&main
        &&v.run_id==state_.run_id&&s.generation==state_.script_generation
        &&g.ui_idle&&g.inventory_retired&&g.old_post_modal_consumed&&g.source469
        &&f.live&&f.phase1C==0&&f.substate24==0&&f.return20==0&&f.outer_returns>0;
}
GameFsStatus GameReturnedConversationInteraction::Update(const GameActorScriptSnapshot& s,const GameMainFlowSnapshot& f,const GameReturnedInteractionGate& g){
    if(!state_.live)return {};
    state_.source_gate_blocked=g.source469&&f.phase1C==0;
    if(state_.menu_only){
        if(!RefreshCompletedOutingMenu()){Revoke("Completed outing menu owner retired/drifted");return {};}
        state_.outing_menu=menu_script_->completed_outing_menu_.Snapshot();
        // Keep the old consumed activation unchanged while this lease owns UI.
        // After retirement, only a genuinely new preexisting proof kind may
        // replace this spent menu-only publication.
        if(state_.outing_menu.retired){
            activation_.Observe(s,g.message_context);
            if(activation_.State().id==activation_id_){state_.eligible=state_.armed=false;return {};}
            state_.menu_only=false;
        }else{
            state_.eligible=!state_.event_posted&&Ready(s,f,g);
            if(!state_.eligible){state_.armed=false;input_.Stop();}
            return {};
        }
    }
    activation_.Observe(s,g.message_context);state_.activation=activation_.Snapshot();
    const auto& a=activation_.State();
    if(a.revoked){Revoke(a.error);return {};}
    if(!a.id)return {};
    if(a.id!=activation_id_){
        activation_id_=a.id;state_.candidate=true;state_.admitted=state_.eligible=state_.armed=false;
        state_.event_posted=state_.callback_returned=false;state_.queue_result=-1;state_.error.clear();
        input_.Stop();target_={};
    }
    state_.script_generation=a.script_generation;state_.run_id=a.run_id;state_.code_sha256=a.code_sha256;
    state_.message=a.message;state_.message_frame=a.message_frame;state_.message_context=a.message_context;
    state_.caller_frame=a.caller_frame;state_.caller_entry=a.caller_entry;state_.caller_pc=a.caller_pc;
    state_.prior_sequence=a.prior_sequence;state_.retained_sequence=a.body_sequence;state_.return_sequence=a.return_sequence;
    state_.returned=a.returned;state_.game3_baseline=a.game3_baseline;
    if(state_.event_posted&&s.callbacks_returned>callback_baseline_){state_.callback_returned=true;state_.returned_callbacks=s.callbacks_returned-callback_baseline_;}
    if(!state_.event_posted)callback_baseline_=s.callbacks_returned;
    const bool new_main_consumer=a.main_consumed;
    if(!state_.admitted&&new_main_consumer&&(a.kind==GameConversationProofKind::OwnedMain||g.manager9)&&Ready(s,f,g)){
        try{
            target_=resolve_();
            if(target_.generation!=s.generation||target_.context.kind!=script::Value::Kind::Context
               ||target_.context.word!=0x80000000u||target_.context.context_owner.expired()
               ||target_.callback.kind!=script::Value::Kind::Code||target_.callback.word!=0x7008DA28u){
                Revoke("Returned activation Local0/context registry identity refused");return {};
            }
        }catch(const std::exception& e){Revoke(e.what());return {};}
        ++state_.generation;state_.admitted=true;state_.game3_calls=a.game3_calls;state_.game3_pc=a.game3_pc;
        state_.context_word=target_.context.word;state_.callback_word=target_.callback.word;
        state_.message_context=g.message_context;input_.Begin();
    }
    const bool eligible=state_.admitted&&!state_.event_posted&&Ready(s,f,g);
    if(!eligible){input_.Stop();state_.armed=false;}
    else if(!state_.eligible)input_.Begin(); // same token;new release needed after any busy cut
    state_.eligible=eligible;
    return {};
}
bool GameReturnedConversationInteraction::RefreshCompletedOutingMenu()const{
    if(!menu_script_||menu_script_life_.expired())return false;
    menu_script_->PollCompletedOutingMenu();const auto proof=menu_script_->completed_outing_menu_.Snapshot();
    return proof.published&&!proof.revoked&&proof.id==last_outing_proof_;
}
GameFsStatus GameReturnedConversationInteraction::Update(GameActorScript& script,const GameMainFlowSnapshot& f,const GameReturnedInteractionGate& g){
    script.PollCompletedOutingMenu();const auto proof=script.completed_outing_menu_.Snapshot();
    if(state_.live&&!state_.revoked&&proof.published&&!proof.revoked&&proof.id!=last_outing_proof_){
        const auto old=state_;const auto actor=script.Snapshot();
        if(!old.event_posted||!old.activation.consumed||old.owner!=proof.prior_owner||old.generation!=proof.prior_generation
            ||old.activation.id!=proof.prior_activation||old.script_generation!=proof.script_generation||old.run_id!=proof.run)
            {Revoke("Completed outing cannot replace a different available history");return {GameFsCode::Unavailable,0,state_.error};}
        GameReturnedInteractionSnapshot next;next.live=next.candidate=next.returned=next.admitted=true;next.menu_only=true;
        next.owner=old.owner;next.generation=old.generation+1;next.previous_generation=old.generation;next.previous_event_posted=old.event_posted;
        next.prior_activation=old.activation;next.activation=old.activation;next.outing_menu=proof;
        next.script_generation=proof.script_generation;next.run_id=proof.run;next.context_word=proof.context;next.code_sha256=actor.sha256;
        next.caller_frame=proof.main;next.message_context=g.message_context;state_=std::move(next);
        menu_script_=&script;menu_script_life_=script.input_policy_lifetime_;last_outing_proof_=proof.id;input_.Stop();target_=script.OwnerEventTarget();
    }
    return Update(script.Snapshot(),f,g);
}
bool GameReturnedConversationInteraction::Input(const GameInputFrame& frame){
    if(state_.menu_only){input_.Stop();state_.armed=false;++state_.rejected;return false;} // never Returned Circle/arg0
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
    state_.event_posted=true;state_.eligible=state_.armed=false;++state_.posted;input_.Stop();
    if(!activation_.Consume())Revoke("Accepted queue consumed an invalid activation");
    state_.activation=activation_.Snapshot();return true;
}
void GameReturnedConversationInteraction::CheckInputOwners(const GameActorScriptSnapshot& s,const GameMainFlowSnapshot& f,const GameReturnedInteractionGate& g){
    if(state_.menu_only){if(!RefreshCompletedOutingMenu()){Revoke("Completed outing menu history retired");return;}
        if(!Ready(s,f,g)){state_.eligible=state_.armed=false;input_.Stop();}return;}
    if(!activation_.HistoryMatches(s)){
        Revoke("Returned input activation owner/history retired or replaced");return;
    }
    // This path can only disarm. Admission and publication happen in Tick.
    if(!Ready(s,f,g)){state_.eligible=state_.armed=false;input_.Stop();}
}
GameFsStatus GameReturnedConversationInteraction::PostMenuTeach(GameActorScript& script,uint64_t owner,uint64_t generation,uint64_t menu_generation){
    return PostMenuAction(script,owner,generation,menu_generation,1);
}
GameFsStatus GameReturnedConversationInteraction::PostMenuOuting(GameActorScript& script,uint64_t owner,uint64_t generation,uint64_t menu_generation,const GameMainMenuLease& lease){
    return PostMenuAction(script,owner,generation,menu_generation,3,&lease);
}
GameFsStatus GameReturnedConversationInteraction::PostMenuAction(GameActorScript& script,uint64_t owner,uint64_t generation,uint64_t menu_generation,int32_t argument,const GameMainMenuLease* lease){
    if(state_.menu_only)return {GameFsCode::Unavailable,0,"Completed outing menu-only proof cannot dispatch Teach/Outing"};
    const auto before=script.Snapshot();
    if(!state_.live||!state_.admitted||state_.event_posted||state_.revoked||!menu_generation
       ||owner!=state_.owner||generation!=state_.generation||activation_.State().consumed
       ||!activation_.State().main_consumed||!activation_.HistoryMatches(before)||!SameTarget())
        return {GameFsCode::Unavailable,0,"Menu action lost its borrowed available activation"};
    if(argument==3){
        if(!lease||lease->available_owner!=owner||lease->available_generation!=generation||!script.vm_||!script.runtime_)
            return {GameFsCode::Unavailable,0,"Outing dispatch needs the actual borrowed menu lease"};
        const auto prepared=script.completed_outing_menu_.Prepare(*script.vm_,*script.runtime_,*lease,activation_.State().id);if(!prepared)return prepared;
    }
    int32_t queue=-1;const auto status=script.PostMenuEvent(target_,0,menu_generation,queue,argument);
    if(!status||queue!=0){if(argument==3)script.completed_outing_menu_.Close();
        if(!status)return status;
        return {GameFsCode::Unavailable,0,"Teach menu integer event queue refused"};}
    if(argument==3){const auto bound=script.completed_outing_menu_.Accept(*script.vm_,script.state_.menu_callback);if(!bound)return bound;}
    // The genuine selected menu callback consumes the borrowed activation.
    // Its argument1 branch earns no argument0 common-conversation completion.
    state_.queue_result=queue;state_.event_posted=true;state_.eligible=state_.armed=false;
    ++state_.posted;input_.Stop();
    if(!activation_.Consume()){Revoke("Teach menu consumed an invalid activation");return {GameFsCode::Unavailable,0,state_.error};}
    state_.activation=activation_.Snapshot();return {};
}
bool GameReturnedConversationInteraction::BorrowCompletedOutingMenu(GameActorScript& script,const GameMainMenuLease& lease){
    if(!state_.menu_only||menu_script_!=&script||state_.event_posted||!RefreshCompletedOutingMenu()
        ||lease.available_owner!=state_.owner||lease.available_generation!=state_.generation
        ||!script.completed_outing_menu_.Borrow(lease))return false;
    state_.event_posted=true;state_.eligible=state_.armed=false;state_.outing_menu=script.completed_outing_menu_.Snapshot();return true;
}
bool GameReturnedConversationInteraction::BindCompletedOutingMenu(GameActorScript& script,const GameMainMenuLease& lease,uint64_t generation){
    return state_.menu_only&&menu_script_==&script&&RefreshCompletedOutingMenu()&&script.completed_outing_menu_.Menu(lease,generation);
}
bool GameReturnedConversationInteraction::CompletedOutingLeaseCurrent(GameActorScript& script,const GameMainMenuLease& lease,uint64_t generation)const{
    return state_.menu_only&&menu_script_==&script&&RefreshCompletedOutingMenu()&&script.completed_outing_menu_.LeaseCurrent(lease,generation);
}
bool GameReturnedConversationInteraction::RetireCompletedOutingMenu(GameActorScript& script,const GameMainMenuLease& lease,uint64_t generation){
    if(!CompletedOutingLeaseCurrent(script,lease,generation)||!script.completed_outing_menu_.Retire(lease,generation))return false;
    state_.outing_menu=script.completed_outing_menu_.Snapshot();state_.eligible=state_.armed=false;return true;
}
void GameReturnedConversationInteraction::Close(){Revoke("Returned interaction owner retired");menu_script_=nullptr;menu_script_life_.reset();}
}
