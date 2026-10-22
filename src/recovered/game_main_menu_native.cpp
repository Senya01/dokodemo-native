#include "game_main_menu_native.h"
#include <utility>

namespace dokodemo::recovered {
int32_t GameMainMenuControl::Resolve(bool full,int32_t row,int32_t column)noexcept{
    static constexpr int32_t a[7][4]={{0,-1,-1,-1},{2,-1,-1,-1},{1,-1,-1,-1},{3,4,6,7},{8,9,10,11},{5,-1,-1,-1},{13,14,15,-1}};
    static constexpr int32_t b[4][4]={{16,-1,-1,-1},{3,4,6,7},{5,-1,-1,-1},{13,14,15,-1}};
    if(row<0||column<0||column>=4||row>=(full?7:4))return -1;
    return full?a[row][column]:b[row][column];
}
bool GameMainMenuControl::Begin(GameMainMenuLease lease,bool full,bool scene,bool notebook,bool profiles){
    if((state_.live&&state_.state78!=3)||!full||!lease.script_generation||!lease.run_id
       ||!lease.available_owner||!lease.available_generation||!lease.message_context||!lease.input_epoch)return false;
    const auto old=state_;state_={};state_.generation=old.generation+1;state_.opens=old.opens+1;
    state_.cancels=old.cancels;state_.closed=old.closed;state_.rejected=old.rejected;state_.unsupported=old.unsupported;
    state_.live=true;state_.full=full;state_.source_scene_nonzero=scene;state_.lease=lease;state_.state78=0;
    state_.notebook_enabled=notebook;state_.other_profiles=profiles;
    input_.BeginContext();return true;
}
bool GameMainMenuControl::Input(const GameInputFrame& f){
    if(f.overflow_resync){input_.BeginContext();++state_.rejected;return false;}
    if(f.sequence<=sequence_){++state_.rejected;return false;}sequence_=f.sequence;
    const auto& l=state_.lease;
    if(!state_.live||state_.state78!=1||f.message_owner_generation!=l.script_generation
       ||f.message_context!=l.message_context||f.returned_owner!=l.available_owner||f.returned_generation!=l.available_generation||f.main_menu_epoch!=l.input_epoch){
        ++state_.rejected;return false;
    }
    input_.Observe(f);return true;
}
void GameMainMenuControl::Tick(uint8_t mode){
    if(!state_.live)return;
    if(state_.state78==0){if(mode!=1){state_.state78=1;input_.BeginContext();}return;}
    if(state_.state78==2){if(mode!=1){state_.state78=3;state_.result=Resolve(state_.full,state_.row,state_.column);++state_.closed;input_.BeginContext();}return;}
    if(state_.state78!=1)return;
    if(mode==1){input_.BeginContext();return;}
    input_.Tick();
    const auto down=input_.Repeat(Down,10,2),up=input_.Repeat(Up,10,2);
    if(state_.substate7C==3){
        // NativeSystemFirstLeafOnly. Down from0 needs the unavailable4E67C
        // registry result to choose1/2. Fresh Up from0 wraps directly to2 at
        //1310C; that other leaf is outside this finite native selection domain.
        // Both refusals consume priority, without a claimed source no-op.
        if(down||up){++state_.unsupported;state_.unsupported_pc=down?0x4E67C:0x1310C;return;}
        // At column0 source1311C takes7C7 before the conditional gate query.
        if(input_.First(Circle)){state_.state78=2;input_.BeginContext();return;}
        if(input_.First(Cross)||input_.First(Left)){state_.substate7C=0;input_.BeginContext();}
        return;
    }
    if(state_.substate7C==1){
        const auto max=state_.other_profiles?3:2;
        if(down){if(state_.column<max)++state_.column;else if(down==1)state_.column=0;return;}
        if(up){if(state_.column>0)--state_.column;else if(up==1)state_.column=max;return;}
        if(input_.First(Circle)){
            if(state_.column<=2){state_.state78=2;input_.BeginContext();}else ++state_.unsupported;
            return;
        }
        if(input_.First(Cross)||input_.First(Left)){state_.substate7C=0;input_.BeginContext();}
        return;
    }
    if(down){
        if(state_.row<6)++state_.row;else if(down==1)state_.row=0;
        if((!state_.source_scene_nonzero&&state_.row==2)||(state_.source_scene_nonzero&&state_.row==1))++state_.row;
    }else if(up){
        if(state_.row>0)--state_.row;else if(up==1)state_.row=6;
        if((!state_.source_scene_nonzero&&state_.row==2)||(state_.source_scene_nonzero&&state_.row==1))--state_.row;
    }
    if(down||up)return; // source directional path returns before confirmation
    // Source confirm priority remains ahead of Cancel. This finite host domain
    // retains the owner and selection when an unimplemented action is offered.
    if(input_.First(Circle)){
        if(state_.row==6){state_.substate7C=3;state_.column=0;input_.BeginContext();return;}
        if(state_.row==3&&state_.notebook_enabled){state_.substate7C=1;state_.column=0;input_.BeginContext();return;}
        if(state_.row==5){if(!input_.First(Right)){state_.state78=2;input_.BeginContext();}}
        else ++state_.unsupported;
        return;
    }
    if(input_.First(Right)){
        if(state_.row==6){state_.substate7C=3;state_.column=0;input_.BeginContext();return;}
        if(state_.row==3&&state_.notebook_enabled){state_.substate7C=1;state_.column=0;input_.BeginContext();return;}
        if(state_.row==3||state_.row==4||state_.row==6)++state_.unsupported;
        return;
    }
    if(input_.First(Cross)||input_.First(Left)){
        state_.row=state_.column=-1;state_.result=-1;state_.state78=2;++state_.cancels;input_.BeginContext();
    }
}
void GameMainMenuControl::Close()noexcept{state_.live=false;input_.BeginContext();}
GameFsStatus GameMainMenu::Load(GameResourceSource& source){
    if(resource_)return {};
    auto image=std::make_shared<GameUiResource>();
    auto status=image->Load(source,"data\\gui\\main_menu\\MMENU.CB2A","data\\gui\\main_menu\\");
    if(!status)return status;
    GameUiInstance copy;if(!copy.Clone(image))return {GameFsCode::Unavailable,0,"Main menu clone refused"};
    static constexpr uint32_t bindings[][3]={{0,0,0},{1,6,0},{2,7,0},{3,8,0},{4,9,0},{5,10,0},{6,11,0},{7,12,0},{8,13,0},{9,14,0},{10,15,0},{11,16,0},{13,18,0},{14,19,0},{15,23,1},{16,24,0},{17,25,0},{18,26,0},{19,27,0},{20,28,0},{21,29,0}};
    for(const auto& b:bindings)if(!copy.Bind(b[0],b[1],b[2]))return {GameFsCode::Unavailable,0,"Main menu constructor binding"};
    for(uint32_t root:{0u,3u,4u,14u})if(!copy.ShowRoot(root,false))return {GameFsCode::Unavailable,0,"Main menu source root"};
    static constexpr uint32_t clips[]={0,6,7,8,9,10,11,12,13,14,15,16,18,24,25,26,27,28,29};
    for(uint32_t i=0;i<19;++i){auto* c=copy.AddControl(clips[i]);if(!c)return {GameFsCode::Unavailable,0,"Main menu constructor control"};if(i){c->frame=1;c->mode=2;c->active=1;}}
    resource_=std::move(image);clone_=std::move(copy);return {};
}
void GameMainMenu::Auxiliary(){
    const uint32_t indices[]={9,10,11,12,17,18},frames[]={5,6,6,5,6,5};
    for(uint32_t i=0;i<6;++i){auto* c=clone_.Control(indices[i]);c->frame=frames[i];c->mode=2;c->active=1;}
}
GameFsStatus GameMainMenu::Open(GameResourceSource& source,GameMainMenuLease lease,bool full,bool scene,bool notebook,bool profiles){
    if(Modal()||!full)return {GameFsCode::Unavailable,0,"Main menu finite full-mode owner only"};
    auto status=Load(source);if(!status)return status;
    if(!control_.Begin(lease,full,scene,notebook,profiles))return {GameFsCode::Unavailable,0,"Main menu owner identity unavailable"};
    if(!clone_.ShowRoot(0)||!clone_.Bind(0,0,0))return {GameFsCode::Unavailable,0,"Main menu opening clip"};
    auto* main=clone_.Control(0);*main={0,0,1,1};
    auto* context=clone_.Control(1);context->frame=scene?2:1;context->mode=2;context->active=1;
    for(uint32_t i=0;i<7;++i){auto* c=clone_.Control(2+i);c->frame=i?1:9;c->mode=2;c->active=1;}
    for(uint32_t i=0;i<4;++i){auto* c=clone_.Control(13+i);c->frame=i?1:9;c->mode=2;c->active=1;}
    Auxiliary();return {};
}
GameFsStatus GameMainMenu::Tick(){
    const auto before=control_.Snapshot();if(!before.live||before.state78==3)return {};
    std::string error;if(!clone_.Update(error))return {GameFsCode::Unavailable,0,error};
    auto* main=clone_.Control(0);if(!main)return {GameFsCode::Unavailable,0,"Main menu primary control retired"};
    control_.Tick(main->mode);const auto after=control_.Snapshot();
    if(before.state78==1&&after.state78==1&&before.substate7C!=after.substate7C){
        const auto system=after.substate7C==3||before.substate7C==3;
        const auto clip=after.substate7C==3?5u:after.substate7C==1?(after.other_profiles?3u:2u):0u;
        if(!clone_.Bind(0,clip,0))return {GameFsCode::Unavailable,0,"Main menu authored submenu clip"};
        *main={0,clip,1,1};
        auto* selected=clone_.Control(system?12:after.other_profiles?10:9);
        selected->frame=after.substate7C?uint32_t(after.column+2):(system?5u:after.other_profiles?4u:3u);
    }
    if(after.state78==1&&after.substate7C==1)clone_.Control(after.other_profiles?10:9)->frame=uint32_t(after.column+2);
    if(after.state78==1&&after.substate7C==3)clone_.Control(12)->frame=uint32_t(after.column+2);
    if(before.state78==1&&after.state78==2){
        if(!clone_.ShowRoot(0)||!clone_.Bind(0,1,0))return {GameFsCode::Unavailable,0,"Main menu closing clip"};
        *main={0,1,1,1};Auxiliary();
    }
    // NativeMainMenuSteadyHighlight: source initial highlight poses retained;
    // transition animation of a moving cursor is not claimed matching.
    if(after.state78==1)for(uint32_t i=0;i<7;++i)clone_.Control(2+i)->frame=int32_t(i)==after.row?9:1;
    if(before.state78==2&&after.state78==3){
        for(uint32_t root:{0u,3u,4u,14u})clone_.ShowRoot(root,false);
        main->mode=main->active=0;
    }
    return {};
}
GameFsStatus GameMainMenu::Draw(GameGraphics& graphics){
    if(!Modal())return {};
    GameUiDrawStats stats;std::string error;if(!clone_.Draw(graphics,stats,error))return {GameFsCode::Unavailable,0,error};
    ++draw_frames_;quads_=stats.quads;return {};
}
GameMainMenuSnapshot GameMainMenu::Snapshot()const{
    auto out=control_.Snapshot();out.frame=last_frame_;out.clip=last_clip_;out.mode=last_mode_;
    if(auto* c=clone_.Control(0)){out.frame=c->frame;out.clip=c->clip;out.mode=c->mode;}
    out.render_live=bool(resource_);out.render_retirements=render_retirements_;
    out.draw_frames=draw_frames_;out.quads=quads_;return out;
}
GameFsStatus GameMainMenu::RetireClosedRender(){
    const auto s=Snapshot();
    if(!s.live||s.state78!=3||(s.result!=5&&s.result!=3&&s.result!=4&&s.result!=6))return {GameFsCode::Unavailable,0,"Only resolved finite child menu render may retire"};
    if(!resource_)return {};
    last_frame_=s.frame;last_clip_=s.clip;last_mode_=s.mode;
    clone_.Close();resource_.reset();++render_retirements_;return {};
}
void GameMainMenu::Close(){control_.Close();clone_.Close();resource_.reset();}
}
