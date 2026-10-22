#pragma once
#include "game_scene_ui_native.h"
#include "script_vm.h"

namespace dokodemo::recovered {
class GameFontResource;
// Optional passive probe for the source preview's index0. It is separate from
// the accepted rows1..N image and never grants populated/preview admission.
struct GameNotebookSlotObservation {
    bool valid=false;
    uint32_t error=0,index=0,row_table=0,row_index=0,row_header=0,cells=0;
    uint64_t run_id=0,script_generation=0;
    std::array<script::ValueWitness,2> first{};
    std::array<uint8_t,32> sha{};
};
GameNotebookSlotObservation GameObserveNotebookSlot(const script::Vm&,uint64_t,const script::Value&,uint32_t)noexcept;
// NativeTypedScriptArrayReference: the existing native VM representation of
// GAME11/manager+10. It is not a new proof of the original22F38 pointer resolver.
struct GameNotebookRows {
    GameNotebookSlotObservation slot0;
    bool valid=false;
    uint64_t script_generation=0,run_id=0;
    uint32_t root_table=0,root_index=0,root_header=0,records=0,populated=0,cells=0;
    std::array<char,65> code_sha{};
    std::array<uint8_t,32> sha{};
};
GameNotebookRows GameObserveNotebookRows(const script::Vm&,uint64_t,const script::Value&);
struct GameNotebookLease {
    uint64_t script_generation=0,run_id=0,context=0,parent_generation=0,
             available_owner=0,available_generation=0,input_epoch=0;
};
struct GameNotebookSnapshot {
    bool live=false,visible=false,request9=false,owner46C=false,retirement_pending=false;
    uint64_t generation=0,retired_generation=0,input_epoch=0,last_sequence=0;
    GameNotebookLease lease;
    GameNotebookRows rows;
    uint32_t phase=0,state23C=3,state240=0,mode244=0,count248=0,count24C=0,category264=1,category268=1,ordinary270=3;
    int32_t selection250=0;
    uint32_t loader498=0,loader469=0,requests=0,opens=0,close_requests=0,close_completions=0,
             polls=0,retirement_requests=0,retirements=0,rejected=0,unsupported=0;
    uint32_t frame=0,clip=0,mode=0,draw_frames=0,quads=0,font_resets=0,font_flushes=0,callback_reaches=0,callback_empty_returns=0;
    bool text_owner=false;
    uint32_t text_capacity=0,text_used=0,text_setup_stage=0;
    float text_scale_x=0,text_scale_y=0;
    int32_t text_flush_selector=0,scissor_top=0,scissor_bottom=0;
    std::string error;
};
// Finite actual-empty NOTE resource9. Text/populated records and preview paths
// stop at their real consumer; no invented notebook rows or labels.
class GameNotebook final:private GameUiCallbacks {
  public:
    GameFsStatus Request(GameNotebookRows,GameNotebookLease,uint32_t prior_owned498);
    GameFsStatus Tick(GameResourceSource&,GameTransition&);
    bool Input(const GameInputFrame&);
    void DisarmInput(){input_.BeginContext();}
    bool SetInputEpoch(uint64_t epoch);
    GameFsStatus Draw(GameGraphics&,const GameFontResource&);
    GameNotebookSnapshot Snapshot()const;
    bool Modal()const{return state_.phase>=1&&state_.phase<=6;}
    bool ClosedAndRetired()const{return state_.generation&&state_.phase==7&&state_.retired_generation==state_.generation;}
    void Close(); // teardown is not a completed poll or deferred retirement
  private:
    GameFsStatus Initialize(GameResourceSource&,GameTransition&);
    bool DrawUiCallback(uint32_t,uint32_t,const GameUiMatrix&,uint32_t,GameGraphics&,std::string&)override;
    GameNotebookSnapshot state_;
    GameMenuInput input_;
    std::shared_ptr<GameUiResource> resource_;
    GameUiInstance clone_;
    const GameFontResource* font_=nullptr;
    // Actual Open resets80/84 toNULL and98/9C to-1. The missing populated
    // current-row producer is the only prospective writer; it is not admitted.
    std::array<bool,2> row_text_present_{};
    std::array<int32_t,2> row_numbers_{{-1,-1}};
    // E43C/E4A0/E4D0 owner: borrowed XFT, owned typed1024*44 record buffer.
    std::vector<uint8_t> text_records_;
};
}
