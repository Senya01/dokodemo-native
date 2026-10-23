#pragma once
#include "game_ui_resource_native.h"
#include "game_input_native.h"

namespace dokodemo::recovered {
class GameFontResource;
struct GameTrcPromptSnapshot {
    bool live=false,active=false,failed=false,cancel_enabled=false,resource_live=false,font_live=false;
    uint64_t owner=0,generation=0,parent=0,invocation=0,input_epoch=0,last_sequence=0;
    uint64_t result_sequence=0,result_epoch=0,completed_invocation=0;
    uint32_t publication=0,result_update=0;
    int32_t state864=3,result86C=0;
    uint32_t mode=0,selection=0,choices=2,columns=0,rows=0,shape_frame=1;
    uint32_t opens=0,returns=0,no_choices=0,yes_choices=0,yes_refusals=0,disabled_cross=0,rejected=0,updates=0,draws=0,callbacks=0,glyphs=0,flushes=0,sound_intents=0;
    float progress=0;
    GameFsStatus failure;
};
// Actual global47784/CB768 owner borrowed from Bootstrap, not a new dialog
// clone. Plain DA500/mode0 publishes real Yes1 or No2 before natural closing.
// A result alone grants no Options writer/completion authority.
class GameTrcPrompt final:private GameUiCallbacks {
  public:
    GameTrcPrompt()=default;
    GameTrcPrompt(const GameTrcPrompt&)=delete;
    GameTrcPrompt& operator=(const GameTrcPrompt&)=delete;
    GameFsStatus Initialize(GameUiInstance&,std::shared_ptr<const GameUiResource>,const GameFontResource&,void* records,uint32_t capacity);
    GameFsStatus Open(uint64_t parent,uint64_t input_epoch);
    bool Input(const GameInputFrame&);
    GameFsStatus Tick();
    GameFsStatus Draw(GameGraphics&);
    bool SetInputEpoch(uint64_t);
    void Disarm(){input_.BeginContext();}
    GameTrcPromptSnapshot Snapshot()const;
    void Abort(uint64_t parent)noexcept;
    void Close()noexcept;
  private:
    bool Live()const;
    GameFsStatus Refuse(GameFsStatus);
    bool DrawUiCallback(uint32_t,uint32_t,const GameUiMatrix&,uint32_t,GameGraphics&,std::string&)override;
    GameUiInstance* clone_=nullptr;
    std::shared_ptr<const GameUiResource> resource_;
    const GameFontResource* font_=nullptr;
    PackageViewDescription image_identity_,font_identity_;
    void* records_=nullptr;uint32_t capacity_=0;
    std::vector<uint32_t> pixels_;
    uint32_t width_=0,height_=0;
    GameMenuInput input_;
    GameTrcPromptSnapshot state_;
};
}
