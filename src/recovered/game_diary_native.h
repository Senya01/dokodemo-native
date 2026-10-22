#pragma once
#include "game_notebook_native.h"
#include "game_record_native.h"
#include "game_profile_native.h"

namespace dokodemo::recovered {
struct GameDiaryIoSnapshot {
    bool live=false,ready=false;
    uint64_t owner=0,generation=0,requests=0,completed=0;
    uint32_t pending=0;
};
// Borrow the content session's real completed-I/O owner. This is a named
// synchronous Windows manager adapter, not a new empty PSP manager.
class GameDiaryIo {
  public:
    virtual ~GameDiaryIo()=default;
    virtual GameDiaryIoSnapshot DiaryIo()const=0;
    virtual GameFsStatus RequestDiaryImage(const std::string& stem,std::shared_ptr<GameImage>&)=0;
};
struct GameDiaryOpenInputs {
    GameProfileRecord profile;
    uint64_t script_generation=0,run_id=0;
    uint32_t clock_word=0,hour=0,mode=0;
    GameRecordDate date{{-1,-1,-1}};
    int32_t manager58=-1;
};
struct GameDiarySnapshot {
    bool live=false,visible=false,request12=false,owner47C=false,text_owner=false,nested_owner=false,retirement_pending=false;
    uint64_t generation=0,opened_generation=0,retired_generation=0,input_epoch=0,last_sequence=0;
    bool failed=false;
    GameFsStatus failure;
    GameNotebookLease lease;
    uint32_t phase=0,state154=3,state150=0,nested20=0,loader469=0;
    uint32_t requests=0,opens=0,close_requests=0,close_completions=0,polls=0,retirement_requests=0,retirements=0,rejected=0,navigation=0;
    uint32_t count15C=0,selection160=0,current164=0,candidate=0,selected_row=0;
    int32_t selected_id=-1,image_actor=-1,image_frame=0;
    uint32_t image_requests=0,image_ready=0,image_draws=0,draw_frames=0,quads=0,callback_reaches=0,callback_mask=0,emissions=0,glyphs=0;
    uint32_t text_capacity=0,text_used=0,text_stage=0,font_resets=0,font_flushes=0;
    float text_scale_x=0,text_scale_y=0;
    int32_t flush_selector=0;
    uint32_t frame=0,clip=0,mode=0;
    GameDiaryIoSnapshot io;
    GameDiaryOpenInputs inputs;
    std::array<uint8_t,32> before_sha{},approved_sha{};
    std::string image_stem,error;
};
GameRecordImage GameProjectDiaryOpen(const GameRecordImage&,const GameDiaryOpenInputs&,GameDiarySelection&);
class GameDiary final:private GameUiCallbacks {
  public:
    GameFsStatus Request(const GameRecordImage&,GameDiaryOpenInputs,GameNotebookLease);
    GameFsStatus Tick(GameResourceSource&,GameDiaryIo&,GameRecordStore&,GameTransition&);
    bool Input(const GameInputFrame&);
    bool SetInputEpoch(uint64_t);
    void DisarmInput(){input_.BeginContext();}
    GameFsStatus Draw(GameGraphics&,const GameFontResource&);
    GameDiarySnapshot Snapshot()const;
    bool Modal()const{return state_.phase>=1&&state_.phase<=6;}
    bool ClosedAndRetired()const{return state_.phase==7&&state_.generation&&state_.retired_generation==state_.generation;}
    void Close();
  private:
    GameFsStatus Initialize(GameResourceSource&,GameDiaryIo&,GameRecordStore&,GameTransition&);
    GameFsStatus PrepareSelected();
    bool DrawUiCallback(uint32_t,uint32_t,const GameUiMatrix&,uint32_t,GameGraphics&,std::string&)override;
    bool IoLive()const;
    GameFsStatus Refuse(GameFsStatus);
    GameDiaryIo* io_=nullptr;
    GameDiarySnapshot state_;
    GameRecordImage before_,approved_;
    GameDiarySelection selection_;
    GameMenuInput input_;
    std::shared_ptr<GameUiResource> resource_;
    GameUiInstance clone_;
    std::shared_ptr<GameImage> picture_;
    const GameFontResource* font_=nullptr;
    std::vector<uint8_t> text_records_;
    std::string text_,secondary_;
};
}
