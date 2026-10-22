#pragma once
#include "game_scene_ui_native.h"
#include <functional>

namespace dokodemo::recovered {
struct GameInventoryItem {
    uint32_t row=0,date_word=0;
    int32_t id=-1,category=-1;
    bool valid=false;
    std::array<uint32_t,3> source_words{};
    std::array<uint8_t,3> source_kinds{};
    std::string name,description;
};
struct GameInventoryActionAdmission {
    GameFsStatus status;int32_t queue_result=-1;uint64_t receipt=0;
};
using GameInventoryActionDispatch=std::function<GameInventoryActionAdmission(uint64_t,uint32_t,int32_t)>;
struct GameInventorySnapshot {
    bool live=false,requested=false,visible=false,actions=false,retirement_pending=false;
    bool request14=false,owner480=false,input_focused=false;
    uint64_t generation=0;
    uint32_t phase=0,stateB2C=3,stateB30=0,filter=1,count=0,selection=0;
    int32_t selected_row=-1,selected_id=-1,select_result=0;
    uint32_t loader464=0,loader468=0,loader469=0,loader498=0;
    uint32_t requests=0,opens=0,close_requests=0,close_completions=0,pending_polls=0,completed_polls=0;
    uint32_t retirement_requests=0,retirements=0,navigation=0,action_opens=0,action_cancels=0;
    uint32_t draw_frames=0,quads=0,glyphs=0,resource_nodes=0,resource_clips=0,texture_count=0;
    uint32_t action_selection=0,action_frontier=0,action_events=0,action_completions=0;
    bool action_pending=false;uint64_t action_receipt=0;int32_t action_queue_result=-1;
    uint32_t action_event=0;int32_t action_row=-1;
    std::string selected_name,selected_description,error;
    std::vector<uint32_t> filtered_rows;
    std::vector<GameInventoryItem> items;
};
// Source41 -> resource14. Typed native ownership and modal SELECT layout;
// List/filter/navigation plus owned script events14/15; placed-item removal,
// decorate13 and confirmed discard16 remain explicit frontiers,not direct awards.
class GameInventory final {
  public:
    GameFsStatus Request(std::vector<GameInventoryItem> items,bool home_context=true,GameInventoryActionDispatch dispatch={});
    GameFsStatus CompleteAction(uint64_t generation,uint64_t receipt,std::vector<GameInventoryItem> items);
    GameFsStatus Tick(GameResourceSource&,GameTransition&);
    bool Input(const GameInputFrame&);
    GameFsStatus Draw(GameGraphics&,const GameFontResource&);
    GameInventorySnapshot Snapshot()const{return state_;}
    bool Modal()const{return state_.phase>=1&&state_.phase<=6;}
    bool ClosedAndRetired()const{return state_.phase==7&&state_.retirements>0;}
    void Close();
  private:
    GameInventorySnapshot state_;
    std::shared_ptr<GameUiResource> resource_;
    GameUiInstance clone_;
    GameMenuInput input_;
    GameTransition* transition_=nullptr;
    bool home_=true;
    GameInventoryActionDispatch dispatch_;
    void Rebuild();
    void Select();
    GameFsStatus Initialize(GameResourceSource&,GameTransition&);
};
} // namespace dokodemo::recovered
