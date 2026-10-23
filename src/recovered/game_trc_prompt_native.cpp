#include "game_trc_prompt_native.h"
#include "game_content_native.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace dokodemo::recovered {
namespace {
bool SameBorrow(const PackageViewDescription& a,const PackageViewDescription& b){
    return a.readable&&b.readable&&a.owner==b.owner&&a.generation==b.generation&&a.size==b.size&&a.host_address==b.host_address;
}
bool BorrowLive(const PackageView& view,const PackageViewDescription& identity)noexcept{
    try{return SameBorrow(view.Describe(),identity);}catch(...){return false;}
}
const std::string question="\x90\xDD\x92\xE8\x82\xF0\x94\xBD\x89\x66\x82\xB5\x82\xDC\x82\xB7\x82\xA9\x81\x48";
}
bool GameTrcPrompt::Live()const{
    return state_.live&&clone_&&clone_->Node(27)&&clone_->Control(0)&&clone_->Control(1)&&resource_&&font_&&font_->Snapshot().ready
        &&records_&&capacity_==1024&&BorrowLive(resource_->Image(),image_identity_)
        &&BorrowLive(font_->AuxiliaryData(),font_identity_);
}
GameFsStatus GameTrcPrompt::Refuse(GameFsStatus s){if(!state_.failed){state_.failed=true;state_.failure=std::move(s);}return state_.failure;}
GameFsStatus GameTrcPrompt::Initialize(GameUiInstance& clone,std::shared_ptr<const GameUiResource> resource,const GameFontResource& font,void* records,uint32_t capacity){
    if(state_.live)return {GameFsCode::Busy,0,"TRC global already initialized"};
    if(!resource||!font.Snapshot().ready||!clone.Node(27)||!clone.Control(1)||!records||capacity!=1024)
        return {GameFsCode::Unavailable,0,"TRC actual global clone/FONT/text owner unavailable"};
    static std::atomic<uint64_t> next{1};state_={};state_.live=true;state_.owner=next.fetch_add(1);state_.generation=state_.owner;
    clone_=&clone;resource_=std::move(resource);font_=&font;records_=records;capacity_=capacity;
    image_identity_=resource_->Image().Describe();font_identity_=font.AuxiliaryData().Describe();
    if(!Live()||!clone.SetSourceCallback(22,0x481E4)){Close();return {GameFsCode::Unavailable,0,"TRC source callback/backing preflight"};}
    return {};
}
GameFsStatus GameTrcPrompt::Open(uint64_t parent,uint64_t epoch){
    if(state_.failed)return state_.failure;
    if(!Live()||state_.active||state_.state864!=3||!parent||!epoch)return {GameFsCode::Unavailable,0,"TRC idle current owner unavailable"};
    //48278/482E0: the ten plain fullwidth codes fit without rich dispatch or
    //auto-wrap. NativeTrcAtlas reuses current original XFT metrics/ink.
    std::vector<GameFontDrawRecord> records(capacity_);GameFontBitmap bitmap;
    auto status=font_->RasterizeCp932({question},320,bitmap,records.data(),capacity_,0xFF2B1807,23,0);
    if(!status)return Refuse(status);
    if(bitmap.glyphs!=10||bitmap.advance_width>160||bitmap.height>23)return Refuse({GameFsCode::Unavailable,0,"TRC DA500 actual atlas exceeds source10x1 cells"});
    // RasterizeCp932 returns a canvas with width320; advance_width is the
    // actual no-wrap extent. Preserve its row stride while cropping the
    // transparent canvas to the source160-wide question grid.
    width_=160;height_=bitmap.height;pixels_.resize(size_t(width_)*height_);
    for(uint32_t y=0;y<height_;++y)std::copy_n(bitmap.pixels.data()+size_t(y)*bitmap.width,width_,pixels_.data()+size_t(y)*width_);
    std::memcpy(records_,records.data(),bitmap.glyphs*sizeof(GameFontDrawRecord));
    state_.parent=parent;state_.input_epoch=epoch;++state_.invocation;++state_.opens;state_.last_sequence=0;state_.active=true;
    state_.result_sequence=state_.result_epoch=0;state_.result_update=0;
    state_.state864=-1;state_.result86C=0;state_.mode=state_.selection=0;state_.choices=2;state_.cancel_enabled=false;state_.progress=0;
    state_.columns=10;state_.rows=2;state_.shape_frame=30;state_.glyphs=bitmap.glyphs;input_.BeginContext();return {};
}
bool GameTrcPrompt::SetInputEpoch(uint64_t epoch){if(!Live()||epoch<=state_.input_epoch)return false;state_.input_epoch=epoch;input_.BeginContext();return true;}
bool GameTrcPrompt::Input(const GameInputFrame& f){
    if(!f.focused||f.overflow_resync){input_.BeginContext();++state_.rejected;return false;}
    if(state_.failed||!Live()||!state_.active||state_.state864!=1||f.main_menu_epoch!=state_.input_epoch||f.sequence<=state_.last_sequence){++state_.rejected;return false;}
    state_.last_sequence=f.sequence;input_.Observe(f);return true;
}
GameFsStatus GameTrcPrompt::Tick(){
    if(state_.failed)return state_.failure;
    if(!Live())return Refuse({GameFsCode::Unavailable,0,"TRC borrowed current owner retired"});
    if(!state_.active)return {};
    ++state_.updates;std::string error;
    if(state_.state864==-1){
        if(!clone_->ShowRoot(0)||!clone_->Bind(0,0,0))return Refuse({GameFsCode::Unavailable,0,"47C28 TRC root preflight"});
        *clone_->Control(0)={state_.shape_frame,0,2,1};*clone_->Control(1)={1,1,2,1};state_.state864=0;++state_.sound_intents;
    }else if(state_.state864==0){
        // NativeSignedTick1, source signed quanta*.1; no clock override.
        state_.progress+=.1f;if(state_.progress>=1){state_.progress=1;state_.state864=1;input_.BeginContext();}
    }else if(state_.state864==1){
        input_.Tick();const auto down=input_.Repeat(Down,10,2),up=down?0:input_.Repeat(Up,10,2);
        if(down){if(state_.selection+1<2)++state_.selection;else if(down==1)state_.selection=0;++state_.sound_intents;}
        else if(up){if(state_.selection) --state_.selection;else if(up==1)state_.selection=1;++state_.sound_intents;}
        else if(input_.First(Circle)){
            state_.result86C=int32_t(state_.selection+1);state_.state864=2;
            state_.result_sequence=state_.last_sequence;state_.result_epoch=state_.input_epoch;
            state_.result_update=state_.updates;++state_.publication;
            if(state_.selection)++state_.no_choices;else ++state_.yes_choices;
            ++state_.sound_intents;input_.BeginContext();
        }else if(input_.First(Cross))++state_.disabled_cross; //source8D4==0: consume, never bubble to parent Cancel
        clone_->Control(1)->frame=state_.selection+1;
    }else if(state_.state864==2){
        state_.progress-=.1f;
        if(state_.progress<=0){state_.progress=0;state_.state864=3;state_.active=false;++state_.returns;state_.completed_invocation=state_.invocation;
            clone_->ShowRoot(0,false);clone_->Control(0)->mode=clone_->Control(0)->active=0;input_.BeginContext();}
    }
    if(!clone_->Update(error))return Refuse({GameFsCode::Unavailable,0,error});return {};
}
GameFsStatus GameTrcPrompt::Draw(GameGraphics& graphics){
    if(state_.failed)return state_.failure;
    if(!state_.active)return {};
    if(!Live())return Refuse({GameFsCode::Unavailable,0,"TRC draw borrowed owner retired"});
    //47F00 source default center0,scale1, DC428 margin14, choice offset4.
    const float w=160,h=46,p=state_.progress;
    auto* root=clone_->Node(0);auto* choice=clone_->Node(1);
    root->base_position=root->position={{-(w*p*.5f+14),-(h*p*.5f+14)}};root->base_scale=root->scale={{p,p}};
    choice->base_position=choice->position={{0,h*p*.5f-10}};choice->base_scale=choice->scale={{p,p}};
    std::string error;GameUiDrawStats background,choices;
    if(!clone_->Draw(graphics,background,error,this,{},0,UINT32_MAX,0)||!clone_->Draw(graphics,choices,error,this,{},0,UINT32_MAX,1))return Refuse({GameFsCode::Unavailable,0,error});
    ++state_.draws;return {};
}
bool GameTrcPrompt::DrawUiCallback(uint32_t pc,uint32_t node,const GameUiMatrix& world,uint32_t,GameGraphics& graphics,std::string& error){
    if(pc!=0x481E4||node!=22||!Live()||!state_.active){error="TRC callback current owner retired";return false;}
    ++state_.callbacks;++state_.flushes;
    if(state_.progress<=0||!width_||!height_)return true;
    const auto cfg=graphics.Snapshot().config;
    std::array<GameMeshVertex,4> v;
    for(unsigned i=0;i<4;++i){const float x=world.tx+(i&1?float(width_)*state_.progress:0),y=world.ty+(i&2?float(height_)*state_.progress:0);
        v[i].clip={{x*2/float(cfg.width),-y*2/float(cfg.height),0,1}};v[i].u=i&1?1.f:0.f;v[i].v=i&2?1.f:0.f;v[i].color=0xFFFFFFFF;}
    GameTextureSpan image{pixels_.data(),width_,height_,pixels_.size()};GameRasterPolicy policy;policy.two_sided=true;policy.depth_test=policy.depth_write=false;policy.repeat=false;
    if(!graphics.DrawMeshTriangle({{v[0],v[1],v[2]}},image,policy)||!graphics.DrawMeshTriangle({{v[2],v[1],v[3]}},image,policy)){error="TRC actual atlas flush failed";return false;}return true;
}
GameTrcPromptSnapshot GameTrcPrompt::Snapshot()const{auto s=state_;s.resource_live=resource_&&BorrowLive(resource_->Image(),image_identity_);s.font_live=font_&&font_->Snapshot().ready&&BorrowLive(font_->AuxiliaryData(),font_identity_);return s;}
void GameTrcPrompt::Abort(uint64_t parent)noexcept{if(state_.parent!=parent)return;if(clone_){clone_->ShowRoot(0,false);if(auto* c=clone_->Control(0))c->mode=c->active=0;}state_.active=false;state_.state864=3;input_.BeginContext();}
void GameTrcPrompt::Close()noexcept{Abort(state_.parent);state_.live=false;clone_=nullptr;resource_.reset();font_=nullptr;records_=nullptr;capacity_=0;pixels_.clear();}
}
