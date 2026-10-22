#pragma once
#include "game_main_flow_native.h"
#include "game_conversation_activation_native.h"

namespace dokodemo::recovered {
struct GameReturnedInteractionGate {
    bool ui_idle=false,inventory_retired=false,old_post_modal_consumed=false;
    bool source469=false,manager9=false;
    uint64_t message_context=0;
};
struct GameReturnedInteractionSnapshot {
    bool live=false,candidate=false,returned=false,admitted=false,eligible=false,armed=false;
    bool event_posted=false,callback_returned=false,revoked=false,source_gate_blocked=false;
    uint64_t owner=0,generation=0,script_generation=0,run_id=0,message_context=0;
    uint64_t message_frame=0,caller_frame=0,prior_sequence=0,return_sequence=0,retained_sequence=0;
    uint64_t last_sequence=0;
    uint32_t message=0,caller_entry=0,caller_pc=0,game3_baseline=0,game3_calls=0,game3_pc=0;
    uint32_t context_word=0,callback_word=0,posted=0,rejected=0,returned_callbacks=0;
    int32_t queue_result=-1;
    std::string code_sha256,error;
    GameConversationActivationSnapshot activation;
};
// WindowsReturnedConversationInteraction is a Windows scheduling/input policy.
// It does not clear source469, restore original post32 wake, or rearm the old
// post-modal route. It admits one owning event per witnessed completed activation.
class GameReturnedConversationInteraction final {
  public:
    GameReturnedConversationInteraction()=default;
    GameReturnedConversationInteraction(const GameReturnedConversationInteraction&)=delete;
    GameReturnedConversationInteraction& operator=(const GameReturnedConversationInteraction&)=delete;
    GameReturnedConversationInteraction(GameReturnedConversationInteraction&&)=delete;
    GameReturnedConversationInteraction& operator=(GameReturnedConversationInteraction&&)=delete;
    using Resolve=std::function<GameOwnerEventTarget()>;
    using Dispatch=std::function<GameFsStatus(const GameOwnerEventTarget&,int32_t&)>;
    GameFsStatus Initialize(Resolve,Dispatch);
    GameFsStatus Update(const GameActorScriptSnapshot&,const GameMainFlowSnapshot&,const GameReturnedInteractionGate&);
    bool Input(const GameInputFrame&);
    void CheckInputOwners(const GameActorScriptSnapshot&,const GameMainFlowSnapshot&,const GameReturnedInteractionGate&);
    void SuspendInput(){input_.Stop();state_.eligible=state_.armed=false;}
    GameReturnedInteractionSnapshot Snapshot()const{return state_;}
    void Close();
  private:
    bool Ready(const GameActorScriptSnapshot&,const GameMainFlowSnapshot&,const GameReturnedInteractionGate&)const;
    bool SameTarget()const;
    void Revoke(std::string);
    Resolve resolve_;
    Dispatch dispatch_;
    GameOwnerEventTarget target_;
    GameButtonGate input_;
    GameReturnedInteractionSnapshot state_;
    GameConversationActivation activation_;
    uint64_t activation_id_=0;
    uint32_t callback_baseline_=0;
};
}
