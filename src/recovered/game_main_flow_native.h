#pragma once
#include "game_actor_runtime_native.h"
#include "game_input_native.h"
#include "game_actor_script_native.h"
#include <functional>

namespace dokodemo::recovered {
struct GameMainFlowGate {
    bool extension_busy=false,manager_switch_pending=false,manager_flag68=false,loader469=false;
    uint32_t loader_state20=6,fade_state=0;
};
struct GameMainFlowSnapshot {
    bool live=false,input_suppressed48=false; //23234 explicit cold0
    uint64_t generation=0;
    uint32_t phase1C=0,return20=0,substate24=0,steps=0,last_pc=0;
    uint32_t phase5_entries=0,request8_clears=0,request9_clears=0;
    uint32_t request9_consumed=0,phase1_request9_consumed=0,outer_returns=0;
    uint32_t busy_blocks=0,loader_blocks=0,circle_source_blocks=0,events=0,last_event=UINT32_MAX;
    int32_t last_event_argument=0;
    bool last_extension_busy=false,last_loader469=false;
    int32_t circle_counter=-1;
};
//230E8/23238 source owner starts at0,never manufactured at phase5.
//Only current byte8 path and actual manager9 return are implemented here.
class GameMainFlow final {
  public:
    using EventDispatch=std::function<GameFsStatus(uint32_t,int32_t)>;
    GameFsStatus Initialize(GameActorRuntime&,EventDispatch);
    bool Input(const GameInputFrame&);
    GameFsStatus Tick(const GameMainFlowGate&);
    GameMainFlowSnapshot Snapshot()const{return state_;}
    void Close();
  private:
    GameFsStatus Clear8();
    GameFsStatus Clear9();
    GameFsStatus Event(uint32_t,int32_t);
    GameActorRuntime* runtime_=nullptr;
    EventDispatch dispatch_;
    GameMenuInput input_;
    GameMainFlowSnapshot state_;
};
struct GamePostModalSnapshot {
    bool live=false,eligible=false,armed=false,source_gate_blocked=false;
    bool fresh_input=false,event_posted=false,callback_returned=false;
    uint64_t generation=0,inventory_generation=0,script_generation=0,last_sequence=0;
    uint32_t context_word=0,callback_word=0,posted=0,returned=0,rejected=0;
    std::string error;
};
// WindowsPostModalInteraction: explicit FUNCTIONAL_FIRST policy,not a
// recovered LoadMan469 clear or original post32 wake. One owned event per
// actual inventory-retirement/mainflow-return context;camera is independent.
class GamePostModalInteraction final {
  public:
    GameFsStatus Update(const GameInventorySnapshot&,const GameMainFlowSnapshot&,GameActorScript&,bool source469);
    bool Input(const GameInputFrame&);
    GamePostModalSnapshot Snapshot()const{return state_;}
    void Close();
  private:
    GameActorScript* script_=nullptr;
    GameOwnerEventTarget target_;
    GameButtonGate input_;
    GamePostModalSnapshot state_;
    uint32_t return_baseline_=0;
};
} //namespace dokodemo::recovered
