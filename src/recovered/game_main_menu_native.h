#pragma once
#include "game_scene_ui_native.h"

namespace dokodemo::recovered {
// WindowsAvailableMainMenuStableTransition: borrow either stationary endpoint.
// Source Right still requires state2; this policy never changes the fade owner.
inline bool GameMainMenuCanBorrowTransition(const GameTransitionSnapshot& fade)noexcept{
    return fade.live&&(fade.state==0||fade.state==2);
}
struct GameMainMenuLease {
    uint64_t script_generation=0,run_id=0,available_owner=0,available_generation=0,message_context=0,input_epoch=0;
};
struct GameMainMenuSnapshot {
    bool live=false,full=false,source_scene_nonzero=false,render_live=false,notebook_enabled=false,other_profiles=false;
    uint64_t generation=0;
    GameMainMenuLease lease;
    uint32_t state78=3,substate7C=0,opens=0,cancels=0,closed=0,rejected=0,unsupported=0;
    uint32_t unsupported_pc=0;
    int32_t row=0,column=0,result=-1;
    uint32_t frame=0,clip=0,mode=0,draw_frames=0,quads=0,render_retirements=0;
};
// Finite full-mode leaves include teach row0, inventory row5 and Cancel.
// Results publish only after real closing-control completion.
class GameMainMenuControl final {
  public:
    bool Begin(GameMainMenuLease,bool full,bool source_scene_nonzero,bool notebook=false,bool other_profiles=false);
    bool Input(const GameInputFrame&);
    void Tick(uint8_t actual_main_mode);
    void Close()noexcept;
    GameMainMenuSnapshot Snapshot()const{return state_;}
    static int32_t Resolve(bool full,int32_t row,int32_t column)noexcept;
  private:
    GameMenuInput input_;
    GameMainMenuSnapshot state_;
    uint64_t sequence_=0;
};
class GameMainMenu final {
  public:
    GameFsStatus Open(GameResourceSource&,GameMainMenuLease,bool full,bool source_scene_nonzero,bool notebook=false,bool other_profiles=false);
    bool Input(const GameInputFrame& frame){return control_.Input(frame);}
    GameFsStatus Tick();
    GameFsStatus Draw(GameGraphics&);
    // NativeClosedMenuRenderRetirement: after result5 and callback18, release
    // only the hidden template/clone. Keep the resolved control/borrowed lease.
    GameFsStatus RetireClosedRender();
    GameMainMenuSnapshot Snapshot()const;
    bool Modal()const{return control_.Snapshot().live&&control_.Snapshot().state78!=3;}
    void Close();
  private:
    GameFsStatus Load(GameResourceSource&);
    void Auxiliary();
    std::shared_ptr<GameUiResource> resource_;
    GameUiInstance clone_;
    GameMainMenuControl control_;
    uint32_t draw_frames_=0,quads_=0,render_retirements_=0,last_frame_=0,last_clip_=0,last_mode_=0;
};
}
