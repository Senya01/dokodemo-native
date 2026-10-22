#include "game_main_flow_native.h"

namespace dokodemo::recovered {
GameFsStatus GameMainFlow::Initialize(GameActorRuntime& runtime,EventDispatch dispatch){
    if(state_.live||!runtime.OwnersLive()||!dispatch)
        return {GameFsCode::Unavailable,0,"Main-flow requires its live actor/manager/context owners"};
    const auto generation=state_.generation+1;state_={};state_.generation=generation;
    runtime_=&runtime;dispatch_=std::move(dispatch);input_.BeginContext();state_.live=true;return {};
}
bool GameMainFlow::Input(const GameInputFrame& frame,bool circle_owned_by_modal){
    if(!state_.live||!runtime_||!runtime_->OwnersLive())return false;
    input_.Observe(frame);
    // A message/choice/input confirmation cannot migrate into phase5 when
    // its owner clears busy during this same packet. Prefix keys are intact.
    if(circle_owned_by_modal)input_.Disarm(Circle);
    return true;
}
GameFsStatus GameMainFlow::Clear8(){const auto s=runtime_->ClearManager8();if(s)++state_.request8_clears;return s;}
GameFsStatus GameMainFlow::Clear9(){const auto s=runtime_->ClearManager9();if(s)++state_.request9_clears;return s;}
GameFsStatus GameMainFlow::Event(uint32_t id,int32_t arg){
    const auto s=dispatch_(id,arg);if(s){++state_.events;state_.last_event=id;state_.last_event_argument=arg;}return s;
}
GameFsStatus GameMainFlow::Tick(const GameMainFlowGate& gate){
    if(!state_.live||!runtime_||!runtime_->OwnersLive())
        return {GameFsCode::Unavailable,0,"Main-flow owner retired"};
    //1E154/1E4A0 set this source main owner.byte48 for INPUT8/SELECT9 and clear
    //it after the completion bridge. Source prefix routes remain enabled
    //outside that exact modal interval.
    state_.input_suppressed48=gate.choice_busy48;
    input_.Tick();++state_.steps;state_.last_extension_busy=gate.extension_busy;
    state_.last_loader469=gate.loader469;state_.circle_counter=input_.Counter(Circle);
    auto current=runtime_->Snapshot();GameFsStatus status;
    //23AA0..23AE4 is BEFORE the outer phase/readiness switch. CounterF4C
    //aliases input10F0C+40:95194 tests internalbit6,52DEC/52E00 map PSPTriangle.
    //The other aliases are +4C Square,+48 Cross,+50 L,+58 R,in this order.
    //No loader469 clear or fabricated Circle readiness is involved.
    if(!state_.input_suppressed48){
        const uint32_t buttons[]{Triangle,Square,Cross,LTrigger,RTrigger};
        const int32_t arguments[]{1,0,2,3,4};
        for(size_t i=0;i<5;++i)if(input_.First(buttons[i])){
            if(!(status=Event(12,arguments[i])))return status;
            break;
        }
    }
    if(state_.phase1C==0){
        state_.last_pc=0x23B24;
        if(gate.loader469||gate.loader_state20!=6){++state_.loader_blocks;if(input_.First(Circle))++state_.circle_source_blocks;return {};}
        //Busy==1 bypasses input but still reaches byte8 handling23C30.
        if(!gate.extension_busy&&gate.fade_state==2){
            if(input_.First(Right)&&gate.manager_flag68){state_.phase1C=7;state_.last_pc=0x23C90;return {};}
            if(input_.First(Circle)){
                state_.last_pc=0x23C6C;
                if(!(status=Clear9())||!(status=Clear8())||!(status=Event(0,0)))return status;
                state_.phase1C=1;return {};
            }
        }
        state_.last_pc=0x23C30;
        if(current.manager_request8){state_.phase1C=1;return {};}
        if(gate.manager_switch_pending)return {GameFsCode::Unavailable,0,"Reached main-flow1B238 scene switch not implemented"};
        return {};
    }
    if(state_.phase1C==1){
        state_.last_pc=0x23CA8;
        if(current.manager_request8){
            if(!(status=Clear9())||!(status=Clear8()))return status;
            state_.phase1C=5;++state_.phase5_entries;return {};
        }
        if(current.manager_request9){
            if(!(status=Clear9()))return status;
            ++state_.phase1_request9_consumed;state_.phase1C=0;input_.BeginContext();
        }
        return {};
    }
    if(state_.phase1C==2||state_.phase1C==3||state_.phase1C==4){state_.phase1C=1;return {};}
    if(state_.phase1C==6)return {};
    if(state_.phase1C!=5)return {GameFsCode::Unavailable,0,"Reached main-flow outer consumer not implemented"};
    state_.last_pc=0x235B0;
    if(state_.substate24==0){
        if(!(status=Clear9()))return status;
        state_.substate24=4;state_.last_pc=0x235EC;return {};
    }
    if(state_.substate24==11){
        state_.last_pc=0x236B0;state_.phase1C=state_.return20;state_.return20=state_.substate24=0;
        ++state_.outer_returns;input_.BeginContext();return {};
    }
    if(state_.substate24==1||state_.substate24==2||state_.substate24==3||
       (state_.substate24>=6&&state_.substate24<=9))return {};
    if(state_.substate24!=4&&state_.substate24!=5)
        return {GameFsCode::Unavailable,0,"Reached main-flow substate not implemented"};
    if(state_.substate24==4){
        state_.last_pc=0x2360C;
        if(gate.extension_busy){++state_.busy_blocks;return {};}
        if(gate.manager_switch_pending)return {GameFsCode::Unavailable,0,"Reached235B0 scene-switch branch not implemented"};
    }
    if(input_.First(Circle)){state_.last_pc=0x2363C;return Event(12,5);}
    if(current.manager_request9){
        if(!(status=Clear9()))return status;
        ++state_.request9_consumed;state_.substate24=11;state_.last_pc=0x2365C;
    }
    return {};
}
void GameMainFlow::Close(){state_.live=false;runtime_=nullptr;dispatch_={};input_.BeginContext();}
GameFsStatus GameMainFlow::BeginAvailableMenu(){
    if(!state_.live||!runtime_||!runtime_->OwnersLive()||state_.phase1C||state_.substate24||state_.return20)
        return {GameFsCode::Unavailable,0,"Main menu requires its idle live main-flow owner"};
    state_.phase1C=7;state_.menu58=0;state_.last_pc=0x23D0C;input_.BeginContext();return {};
}
GameFsStatus GameMainFlow::AdvanceMenuOpening(){
    if(state_.phase1C!=7||state_.menu58!=0)return {GameFsCode::Unavailable,0,"Main menu state0 owner mismatch"};
    state_.menu58=1;state_.last_pc=0x23970;return {};
}
GameFsStatus GameMainFlow::MenuOpened(){
    if(state_.phase1C!=7||state_.menu58!=1)return {GameFsCode::Unavailable,0,"Main menu opening owner mismatch"};
    state_.menu58=2;state_.last_pc=0x2399C;return {};
}
GameFsStatus GameMainFlow::PollMenuCancel(){
    return PollMenuResult(-1);
}
GameFsStatus GameMainFlow::PollMenuResult(int32_t result){
    if(state_.phase1C!=7||state_.menu58!=2)return {GameFsCode::Unavailable,0,"Main menu completion owner mismatch"};
    if(result!=-1&&result!=5&&result!=3&&result!=4)return {GameFsCode::Unavailable,0,"Main menu result outside finite Cancel/inventory/Notebook/Wordmemo domain"};
    state_.menu_result54=result;state_.menu58=5;state_.last_pc=0x23748;return {};
}
GameFsStatus GameMainFlow::RetireMenuCancel(){
    if(state_.menu_result54!=-1)return {GameFsCode::Unavailable,0,"Main menu Cancel result mismatch"};
    return RetireMenuResult();
}
GameFsStatus GameMainFlow::RetireMenuResult(){
    if(state_.phase1C!=7||state_.menu58!=5||(state_.menu_result54!=-1&&state_.menu_result54!=5&&state_.menu_result54!=3&&state_.menu_result54!=4)||state_.return20)
        return {GameFsCode::Unavailable,0,"Main menu Cancel retirement owner mismatch"};
    state_.menu_result50=state_.menu_result54;state_.menu_result54=-1;state_.menu58=0;
    state_.phase1C=state_.return20;state_.return20=0;state_.last_pc=0x238B0;input_.BeginContext();return {};
}
GameFsStatus GamePostModalInteraction::Update(const GameInventorySnapshot& inventory,const GameMainFlowSnapshot& flow,GameActorScript& script,bool source469){
    if(state_.spent_history)return {};
    if(!state_.error.empty())return {GameFsCode::Unavailable,0,state_.error};
    const auto current=script.Snapshot();
    state_.source_gate_blocked=source469&&flow.phase1C==0;
    if(state_.event_posted&&current.callbacks_returned>return_baseline_){
        state_.callback_returned=true;state_.returned=current.callbacks_returned-return_baseline_;
    }
    const bool ready=inventory.phase==7&&!inventory.live&&!inventory.owner480&&!inventory.request14
        &&!inventory.retirement_pending&&inventory.stateB2C==3&&inventory.loader468==1&&inventory.loader498==0
        &&inventory.completed_polls>0&&inventory.retirements>0&&flow.live&&flow.outer_returns>0
        &&flow.phase1C==0&&flow.substate24==0&&flow.return20==0&&current.live&&!current.completed&&!current.blocked
        &&!current.awaiting_message&&current.callback_depth==0&&current.queued_callbacks==0
        &&current.callbacks_posted==current.callbacks_returned;
    if(state_.live&&(current.generation!=state_.script_generation||inventory.generation!=state_.inventory_generation||!flow.live||!current.live)){
        Close();state_.error="Post-modal owner generation retired or replaced";return {GameFsCode::Unavailable,0,state_.error};
    }
    if(!state_.live&&ready&&state_.error.empty()){
        try{target_=script.OwnerEventTarget();}
        catch(const std::exception& e){state_.error=e.what();return {GameFsCode::Unavailable,0,state_.error};}
        if(target_.callback.word!=0x7008DA28u||target_.context.word!=0x80000000u){
            state_.error="Post-modal finite Local0/context identity mismatch";return {GameFsCode::Unavailable,0,state_.error};
        }
        ++state_.generation;state_.inventory_generation=inventory.generation;state_.script_generation=current.generation;
        state_.context_word=target_.context.word;state_.callback_word=target_.callback.word;
        state_.live=true;script_=&script;input_.Begin();
    }
    const bool was_eligible=state_.eligible;
    state_.eligible=state_.live&&ready&&!state_.event_posted&&state_.error.empty();
    if(state_.eligible&&!was_eligible)input_.Begin();
    if(!state_.eligible){input_.Stop();state_.armed=false;}
    return {};
}
bool GamePostModalInteraction::Input(const GameInputFrame& frame){
    if(state_.spent_history)return false;
    if(frame.overflow_resync){if(state_.eligible)input_.Begin();else input_.Stop();state_.armed=false;++state_.rejected;return false;}
    if(frame.sequence<=state_.last_sequence){++state_.rejected;return false;}
    state_.last_sequence=frame.sequence;
    if(!state_.eligible||!script_||frame.interaction_generation!=state_.generation){++state_.rejected;return false;}
    const bool edge=input_.Observe(frame,Circle);state_.armed=input_.Armed();
    if(!edge)return false;
    state_.fresh_input=true;return_baseline_=script_->Snapshot().callbacks_returned;
    int32_t result=-1;
    const auto status=script_->PostPostModalInteraction(target_,state_.generation,state_.inventory_generation,frame.sequence,result);
    if(!status||result!=0){
        state_.error=status?"Post-modal owned event queue refused admission":status.detail;
        state_.eligible=false;input_.Stop();++state_.rejected;return false;
    }
    state_.event_posted=true;state_.eligible=false;state_.armed=false;++state_.posted;input_.Stop();return true;
}
void GamePostModalInteraction::Close(){state_.live=state_.eligible=state_.armed=false;script_=nullptr;target_={};input_.Stop();state_.error="Post-modal interaction retired";}
GameFsStatus GamePostModalInteraction::RetireConsumedInventory(){
    if(state_.spent_history)return {};
    if(!state_.event_posted||!state_.callback_returned||!state_.error.empty())
        return {GameFsCode::Unavailable,0,"Only consumed post-modal origin may become spent history"};
    state_.spent_history=true;state_.live=state_.eligible=state_.armed=false;script_=nullptr;target_={};input_.Stop();return {};
}
} //namespace dokodemo::recovered
