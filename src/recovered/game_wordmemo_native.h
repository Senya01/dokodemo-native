#pragma once
#include "game_notebook_native.h"

namespace dokodemo::recovered {
// Separate resource8/GAME10 image. This uses the existing qualified native
// Array9/reference representation, not original22F38 pointer equivalence.
struct GameWordMemoRows {
    bool valid=false;
    uint64_t script_generation=0,run_id=0;
    uint32_t root_table=0,root_index=0,root_header=0,records=0,cells=0,eligible_records=0;
    std::array<char,65> code_sha{};
    std::array<uint8_t,32> sha{};
};
GameWordMemoRows GameObserveWordMemoRows(const script::Vm&,uint64_t,const script::Value&);
bool GameWordMemoPausedOwnerExact(const script::VmStateWitness& restored,const script::VmStateWitness& current)noexcept;
using GameWordMemoLease=GameNotebookLease; // same borrowed identity fields, separate owning instance
struct GameWordMemoSnapshot {
    bool live=false,visible=false,request8=false,owner470=false,retirement_pending=false;
    uint64_t generation=0,retired_generation=0,input_epoch=0,last_sequence=0;
    GameWordMemoLease lease;
    GameWordMemoRows rows;
    uint32_t phase=0,state9B0=3,state9B4=0,category9D4=1,category9D8=1,loader469=0;
    int32_t selection9C6=0;
    uint32_t requests=0,opens=0,close_requests=0,close_completions=0,polls=0,
             retirement_requests=0,retirements=0,rejected=0,unsupported=0,navigation=0;
    uint32_t frame=0,clip=0,mode=0,draw_frames=0,quads=0,callback_reaches=0,callback_null_returns=0;
    bool text_owner=false;
    uint32_t text_capacity=0,text_used=0,text_setup_stage=0,font_resets=0,font_flushes=0;
    float text_scale_x=0,text_scale_y=0;
    int32_t text_flush_selector=0,scissor_top=0,scissor_bottom=0;
    std::string error;
};
// Actual source-empty wordmemo only: no invented learned words, selected row,
// row text, edit prompt, event27/28/29 or persistence success.
class GameWordMemo final:private GameUiCallbacks {
  public:
    GameFsStatus Request(GameWordMemoRows,GameWordMemoLease);
    GameFsStatus Tick(GameResourceSource&,GameTransition&);
    bool Input(const GameInputFrame&);
    bool SetInputEpoch(uint64_t);
    void DisarmInput(){input_.BeginContext();}
    GameFsStatus Draw(GameGraphics&,const GameFontResource&);
    GameWordMemoSnapshot Snapshot()const;
    bool Modal()const{return state_.phase>=1&&state_.phase<=6;}
    bool ClosedAndRetired()const{return state_.generation&&state_.phase==7&&state_.retired_generation==state_.generation;}
    void Close(); // teardown cannot mint a completed poll/drain receipt
  private:
    GameFsStatus Initialize(GameResourceSource&,GameTransition&);
    bool DrawUiCallback(uint32_t,uint32_t,const GameUiMatrix&,uint32_t,GameGraphics&,std::string&)override;
    GameWordMemoSnapshot state_;
    GameMenuInput input_;
    std::shared_ptr<GameUiResource> resource_;
    GameUiInstance clone_;
    const GameFontResource* font_=nullptr;
    std::vector<uint8_t> text_records_;
};
}
