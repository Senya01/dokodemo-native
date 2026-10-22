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
    std::array<uint32_t,3> control_words{}; //actual row3/type,row8/selector,row9/spacing
    std::array<uint8_t,3> control_kinds{};
    std::string name,description;
};
enum class GameInventoryOrigin:uint32_t { Script41,MainMenu5 };
struct GameInventoryActionDescriptor {uint32_t event=0,source_result=0;const char* label=nullptr;};
uint32_t GameInventoryActionCount(GameInventoryOrigin,bool home,bool placement=false)noexcept;
GameInventoryActionDescriptor GameInventoryActionAt(GameInventoryOrigin,bool home,uint32_t index,bool placement=false)noexcept;
std::array<uint8_t,32> GameInventoryItemsHash(const std::vector<GameInventoryItem>&);
struct GameInventoryActionCertificate {
    bool valid=false;uint64_t generation=0,receipt=0;uint32_t event=0,row=0;
    std::array<uint8_t,32> before_display{},after_display{},approved_image{};
    std::array<uint32_t,170> mutable_fields{};
};
using GameInventoryCompletionVerify=std::function<bool(const GameInventoryActionCertificate&)>;
struct GameInventoryActionAdmission {
    GameFsStatus status;int32_t queue_result=-1;uint64_t receipt=0;
};
using GameInventoryActionDispatch=std::function<GameInventoryActionAdmission(uint64_t,uint32_t,int32_t)>;
struct GameInventorySnapshot {
    GameInventoryOrigin origin=GameInventoryOrigin::Script41;
    bool live=false,requested=false,visible=false,actions=false,retirement_pending=false;
    bool placement_enabled=false;
    bool request14=false,owner480=false,input_focused=false;
    uint64_t generation=0;
    uint64_t retired_generation=0;
    uint32_t phase=0,stateB2C=3,stateB30=0,filter=1,count=0,selection=0;
    int32_t selected_row=-1,selected_id=-1,select_result=0;
    uint32_t loader464=0,loader468=0,loader469=0,loader498=0;
    uint32_t requests=0,opens=0,close_requests=0,close_completions=0,pending_polls=0,completed_polls=0;
    uint32_t retirement_requests=0,retirements=0,navigation=0,action_opens=0,action_cancels=0;
    uint32_t draw_frames=0,quads=0,glyphs=0,resource_nodes=0,resource_clips=0,texture_count=0;
    uint32_t action_selection=0,action_frontier=0,action_events=0,action_completions=0;
    uint32_t unsupported_actions=0;
    bool discard_confirmation=false;
    uint32_t discard_prompts=0,discard_cancelled=0,discard_accepted=0;
    int32_t discard_dialog_state=3,discard_result=0,discard_row=-1,discard_item_id=-1;
    float discard_progress=0;
    bool action_pending=false;uint64_t action_receipt=0;int32_t action_queue_result=-1;
    uint32_t action_event=0;int32_t action_row=-1;
    std::string selected_name,selected_description,error;
    std::vector<uint32_t> filtered_rows;
    std::vector<GameInventoryItem> items;
};
// Source41 -> resource14. Typed native ownership and modal SELECT layout;
// List/filter/navigation and owned script events13..16. Source result1
//confirmation precedes16;NativeInventoryTRCXYesZNo is a nonmatching panel/input adapter.
class GameInventory final {
  public:
    GameFsStatus Request(std::vector<GameInventoryItem> items,bool home_context=true,GameInventoryActionDispatch dispatch={},GameInventoryOrigin origin=GameInventoryOrigin::Script41,bool placement=false,GameInventoryCompletionVerify verify={});
    GameFsStatus CompleteAction(uint64_t generation,uint64_t receipt,std::vector<GameInventoryItem> items,const GameInventoryActionCertificate* certificate=nullptr);
    GameFsStatus Tick(GameResourceSource&,GameTransition&);
    bool Input(const GameInputFrame&);
    GameFsStatus Draw(GameGraphics&,const GameFontResource&);
    GameInventorySnapshot Snapshot()const{return state_;}
    bool Modal()const{return state_.phase>=1&&state_.phase<=6;}
    bool ClosedAndRetired()const{return state_.generation&&state_.phase==7&&state_.retired_generation==state_.generation;}
    void Close();
  private:
    GameInventorySnapshot state_;
    std::shared_ptr<GameUiResource> resource_;
    GameUiInstance clone_;
    GameMenuInput input_;
    GameDialogController discard_dialog_;
    GameTransition* transition_=nullptr;
    bool home_=true;
    GameInventoryActionDispatch dispatch_;
    GameInventoryCompletionVerify verify_;
    GameFsStatus DispatchAction(uint32_t event,int32_t row);
    void Rebuild();
    void Select();
    GameFsStatus Initialize(GameResourceSource&,GameTransition&);
};
} // namespace dokodemo::recovered
