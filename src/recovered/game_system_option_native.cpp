#include "game_system_option_native.h"
#include "game_content_native.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void NeedOption(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool SameSettings(const GameOptionSettingsSnapshot& a,const GameOptionSettingsSnapshot& b){
    return a.live&&b.live&&a.owner&&a.generation&&a.owner==b.owner&&a.generation==b.generation&&a.words==b.words;
}
constexpr char help_text[]="\x82\xBD\x82\xBE\x82\xA2\x82\xDC\x83\x65\x83\x58\x83\x67\x92\x86\x81\x63\n";
}
struct GameOwnedMessageShared {
    std::shared_ptr<GameUiResource> resource;
    GameUiInstance clone;
    const GameFontResource* font=nullptr;
    std::vector<uint8_t> records;
    uint32_t references=0;
};
GameFsStatus GameOwnedOptionMessage::Initialize(GameResourceSource& source,const GameFontResource& font,uint32_t setting,std::shared_ptr<GameOwnedMessageShared> shared){
    try{
        NeedOption(!shared_&&!state_.live&&font.Snapshot().ready,"153D4 live borrowed FONT/owner required");
        NeedOption(setting==2,"153D4 unsupported message rate row (finite actual setting2)");
        state_={};glyph_ends_.clear();unit_kinds_.clear();unit_cursor_=0;lines_.clear();
        shared_=shared?std::move(shared):std::make_shared<GameOwnedMessageShared>();
        if(!shared_->references){
            auto image=std::make_shared<GameUiResource>();
            auto status=image->Load(source,"data\\gui\\dialog\\DIALOG.CB2A","data\\gui\\dialog\\");if(!status)return status;
            shared_->resource=std::move(image);state_.template_owner=true;
            NeedOption(shared_->clone.Clone(shared_->resource),"153D4 shared clone refused");state_.clone_owner=true;
            static constexpr uint32_t binds[][2]={{0,0},{1,2},{2,3},{3,4}};
            for(const auto& b:binds)NeedOption(shared_->clone.Bind(b[0],b[1],0),"153D4 source root binding refused");
            for(uint32_t clip:{0u,1u,3u,4u}){auto* c=shared_->clone.AddControl(clip);NeedOption(c!=nullptr,"153D4 control owner");c->frame=1;c->mode=2;c->active=0;}
            NeedOption(shared_->clone.SetSourceCallback(26,0x16D24)&&shared_->clone.SetSourceCallback(48,0x16D24),"153D4 callback node owner");
            shared_->records.resize(1024*44);shared_->font=&font;
        }
        NeedOption(shared_->font==&font&&shared_->records.size()==1024*44,"153D4 shared FONT identity replaced");
        ++shared_->references;state_.live=state_.template_owner=state_.clone_owner=state_.text_owner=true;
        state_.capacity=1024;state_.references=shared_->references;state_.state64=0;state_.appearance=0;state_.complete68=true;
        //153D4 default config then Options1595C(9,2,4); mode4 uses root3,
        // before the normal-stack60 gate. No fabricated active stack flag.
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameOwnedOptionMessage::Text(const std::string& text){
    try{
        NeedOption(state_.live&&shared_&&shared_->font&&shared_->font->Snapshot().ready,"15C88 current owner retired");
        NeedOption(!text.empty()&&text.size()<2048,"15C88 plain text extent");
        std::vector<size_t> ends;std::vector<uint8_t> kinds;size_t pos=0;uint32_t glyphs=0;
        while(pos<text.size()){
            auto c=uint8_t(text[pos]);if(c==10){NeedOption(pos+1==text.size(),"15C88 unsupported multiline/control domain");ends.push_back(++pos);kinds.push_back(10);continue;}
            NeedOption(c>=32,"15C88 unsupported message control code");
            const size_t n=((c>=0x81&&c<=0x9F)||(c>=0xE0&&c<=0xFC))?2:1;
            NeedOption(pos+n<=text.size()&&(n==1||uint8_t(text[pos+1])>=0x40),"15C88 truncated CP932");pos+=n;ends.push_back(pos);kinds.push_back(0);++glyphs;
        }
        NeedOption(glyphs<=9,"15C88 unsupported selected message width domain");
        // Admission uses the real FONT atlas before accepting a line; never
        // turn a source width failure into automatic native wrapping.
        GameFontBitmap measured;auto line=text;if(!line.empty()&&line.back()=='\n')line.pop_back();
        const auto metric=shared_->font->RasterizeCp932({line},480,measured,nullptr,0,0xFF552F0D,18,0);
        NeedOption(bool(metric),metric.detail.c_str());NeedOption(measured.advance_width<=144&&measured.height<=18,"15C88 actual atlas line exceeds144x18");
        // Native owned-ring representation: keep rendered lines in their own
        // strings and reclaim only consumed input units. Repeated help must
        // not spend a lifetime-sized append budget or retire live consumers.
        if(unit_cursor_){const auto consumed=glyph_ends_[unit_cursor_-1];state_.text.erase(0,consumed);
            glyph_ends_.erase(glyph_ends_.begin(),glyph_ends_.begin()+unit_cursor_);unit_kinds_.erase(unit_kinds_.begin(),unit_kinds_.begin()+unit_cursor_);
            for(auto& end:glyph_ends_)end-=consumed;unit_cursor_=0;}
        NeedOption(glyph_ends_.size()+ends.size()<=1024,"15C88 unconsumed owned text ring capacity reached");
        const auto base=state_.text.size();state_.text+=text;
        for(auto end:ends)glyph_ends_.push_back(base+end);unit_kinds_.insert(unit_kinds_.end(),kinds.begin(),kinds.end());state_.total+=glyphs;
        state_.complete68=false;state_.idle74=90;++state_.text_calls;
        if(lines_.empty())lines_.emplace_back();
        if(!state_.state64){state_.state64=1;state_.appearance=0;state_.registered=true;state_.close69=false;}
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameOwnedOptionMessage::Tick(bool){
    try{
        NeedOption(state_.live&&shared_&&shared_->font&&shared_->font->Snapshot().ready,"15E28 current FONT owner retired");++state_.updates;
        if(state_.state64==1){state_.appearance=(std::min)(1.0f,state_.appearance+0.1f);if(state_.appearance>=1)state_.state64=2;return {};}
        if(state_.state64==3){state_.appearance=(std::max)(0.0f,state_.appearance-0.1f);
            if(state_.appearance<=0){state_.appearance=0;state_.close69=false;lines_.clear();lines_.emplace_back();
                if(unit_cursor_<glyph_ends_.size()){state_.state64=1;state_.registered=true;}
                else{state_.state64=0;state_.registered=false;state_.complete68=true;state_.text.clear();state_.shown=state_.total=0;glyph_ends_.clear();unit_kinds_.clear();unit_cursor_=0;}}
            return {};}
        if(state_.state64!=2)return {};
        if(state_.close69){state_.complete68=true;state_.state64=3;return {};}
        // Native worker signed tick=1. Actual PH1 setting2 row is
        // {4,10,14,30,24}, factor18/24=.75; other rate rows refuse.
        if(state_.delay70>0){--state_.delay70;if(state_.delay70>0)return {};}
        if(unit_cursor_<glyph_ends_.size()){
            const auto begin=unit_cursor_?glyph_ends_[unit_cursor_-1]:0;const auto end=glyph_ends_[unit_cursor_];
            const auto kind=unit_kinds_[unit_cursor_++];
            if(kind==10){lines_.emplace_back();if(lines_.size()>2)lines_.erase(lines_.begin());state_.delay70=10;}
            else{lines_.back()+=state_.text.substr(begin,end-begin);++state_.shown;state_.delay70=4;}
        }else {state_.complete68=true;if(state_.idle74>0){--state_.idle74;return {};}state_.close69=true;}
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameOwnedOptionMessage::DrawUiCallback(uint32_t pc,uint32_t node,const GameUiMatrix& world,uint32_t,GameGraphics& graphics,std::string& error){
    try{
        NeedOption(pc==0x16D24&&node==48&&state_.live&&state_.mode98C==4&&shared_&&shared_->references,"153D4 unsupported callback/view owner");++state_.callbacks;
        if(state_.appearance!=1)return true; //16B4C exact appearance gate
        NeedOption(shared_->font&&shared_->font->Snapshot().ready&&shared_->records.size()==1024*44,"16B4C shared FONT/batch retired");
        state_.batch_stage=1;state_.used=0;state_.batch_stage=2;state_.scale_x=state_.scale_y=0;state_.batch_stage=3;
        state_.scale_x=state_.scale_y=1;state_.batch_stage=4;++state_.resets;
        // NativeUiViewspace converts source node world to the existing480x272
        // host surface. Source16B4C width144/height36 and fixed restore remain.
        const auto x=int32_t(world.tx+240),y=int32_t(world.ty+136);
        NeedOption(graphics.SetScissor(x,y,x+144,y+36),"16B4C scissor consumer refused");
        std::array<GameFontDrawRecord,1024> records;GameFontBitmap bitmap;
        const auto status=shared_->font->RasterizeCp932(lines_,144,bitmap,records.data(),uint32_t(records.size()),0xFF552F0D,18,0);
        NeedOption(bool(status),status.detail.c_str());state_.used=bitmap.glyphs;state_.glyphs+=bitmap.glyphs;++state_.emissions;
        std::memcpy(shared_->records.data(),records.data(),bitmap.glyphs*sizeof(GameFontDrawRecord));
        NeedOption(graphics.DrawImage(bitmap.pixels,bitmap.width,bitmap.height,x,y,0xFFFFFFFF,65535),"16B4C FONT atlas flush refused");
        state_.flush_selector=-1;state_.batch_stage=5;++state_.flushes;
        NeedOption(graphics.SetScissor(0,0,480,272),"16B4C final scissor restore refused");return true;
    }catch(const std::exception& e){graphics.SetScissor(0,0,480,272);error=e.what();return false;}
}
GameFsStatus GameOwnedOptionMessage::Draw(GameGraphics& graphics){
    if(!state_.live||!state_.state64)return {};
    try{NeedOption(shared_&&shared_->font&&shared_->font->Snapshot().ready,"167DC message/FONT retired");
        auto* c=shared_->clone.Control(3);NeedOption(c!=nullptr,"167DC mode4 control retired");*c={2,4,2,1};
        std::string error;NeedOption(shared_->clone.Update(error),error.c_str());GameUiDrawStats stats;
        NeedOption(shared_->clone.Draw(graphics,stats,error,this,{},0,UINT32_MAX,3),error.c_str());++state_.draws;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameOwnedMessageSnapshot GameOwnedOptionMessage::Snapshot()const{auto s=state_;if(shared_){s.references=shared_->references;s.template_owner=bool(shared_->resource);s.clone_owner=shared_->clone.Node(0)!=nullptr;s.text_owner=shared_->records.size()==1024*44;}return s;}
void GameOwnedOptionMessage::Close()noexcept{
    if(shared_){if(state_.live&&shared_->references)--shared_->references;
        if(!shared_->references){std::vector<uint8_t>().swap(shared_->records);shared_->font=nullptr;shared_->clone.Close();shared_->resource.reset();}
        shared_.reset();}
    state_.live=state_.template_owner=state_.clone_owner=state_.text_owner=state_.registered=false;state_.references=state_.capacity=state_.used=0;lines_.clear();glyph_ends_.clear();unit_kinds_.clear();unit_cursor_=0;
}
GameFsStatus GameSystemOptionFrontier::Request(GameMainMenuLease lease,std::array<uint32_t,3> settings){
    if(state_.requested||state_.failed||!lease.script_generation||!lease.run_id||!lease.available_owner
       ||!lease.available_generation||!lease.message_context||!lease.input_epoch)
        return {GameFsCode::Unavailable,0,"System resource10 request owner unavailable"};
    const auto old=state_;state_={};state_.generation=old.generation+1;state_.requests=old.requests+1;
    state_.requested=true;state_.phase=1;state_.lease=lease;state_.passive_settings=settings;return {};
}
GameFsStatus GameSystemOptionFrontier::Refuse(GameFsStatus status){
    if(!state_.failed){state_.failed=true;state_.failure=std::move(status);}
    return state_.failure;
}
GameFsStatus GameSystemOptionFrontier::Advance(GameResourceSource& source){
    const auto status=PrepareTemplate(source);if(!status)return status;
    return Refuse({GameFsCode::Unavailable,0,"Unsupported System resource10 owned message constructor153D4 at34A08"});
}
GameFsStatus GameSystemOptionFrontier::PrepareTemplate(GameResourceSource& source){
    if(state_.failed)return state_.failure;
    if(!state_.requested||state_.phase!=1)return {GameFsCode::Unavailable,0,"System initializer has no request"};
    ++state_.initializer_attempts;state_.constructor_entered=true;
    try{
        auto image=std::make_shared<GameUiResource>();
        auto status=image->Load(source,"option\\gui\\option\\OPTION.CB2A","option\\gui\\option\\");
        if(!status)return Refuse(status);
        GameUiInstance copy;
        if(!copy.Clone(image))throw std::runtime_error("System constructor clone refused");
        //347C8 exact source constructor binding order and frames.
        for(uint32_t root:{0u,1u,3u})if(!copy.Bind(root,root,0))throw std::runtime_error("System constructor authored root binding");
        for(uint32_t root:{2u,4u})if(!copy.Bind(root,root,1))throw std::runtime_error("System constructor static root binding");
        if(!copy.ShowRoot(0,false))throw std::runtime_error("System constructor hidden root");
        for(uint32_t clip=0;clip<3;++clip){
            auto* c=copy.AddControl(clip);if(!c)throw std::runtime_error("System constructor control");
            if(clip){c->frame=1;c->mode=2;c->active=1;}
        }
        // Source stores direct node pointers while root0 is hidden. A Draw
        // world traversal intentionally cannot find descendants of that root.
        for(uint32_t node:{2u,97u,98u,99u})if(!copy.Node(node))throw std::runtime_error("System constructor source node owner");
        resource_=std::move(image);clone_=std::move(copy);
        state_.template_owner=state_.clone_owner=true;state_.source_consumer=0x153D4;state_.phase=2;
        //Retain this real prefix if the distinct9F0 message constructor fails.
        //474 may be published only by Initialize after that constructor returns.
        return {};
    }catch(const std::exception& e){return Refuse({GameFsCode::Unavailable,0,e.what()});}
}
void GameSystemOptionFrontier::Abort()noexcept{
    message_.Close();settings_=nullptr;font_=nullptr;input_.BeginContext();state_.live=state_.visible=state_.request10=state_.owner474=state_.retirement_pending=false;
    clone_.Close();resource_.reset();state_.template_owner=state_.clone_owner=false;
    state_.requested=false; // Native failed-construction teardown, not93504/poll.
}

GameFsStatus GameSystemOptionFrontier::Initialize(GameResourceSource& source,const GameFontResource& font,GameOptionSettings& settings,GameTransition& fade){
    if(state_.failed)return state_.failure;
    const auto current=settings.OptionSettings();
    if(!current.live||!current.owner||!current.generation||current.words!=state_.passive_settings||!fade.Snapshot().live)
        return Refuse({GameFsCode::Unavailable,0,"Options current settings/transition owner unavailable"});
    auto status=PrepareTemplate(source);if(!status)return status;
    status=message_.Initialize(source,font,current.words[0]);if(!status)return Refuse(status);
    //347C8 returned: only now1A3B0 can publish474 and invoke34F1C.
    settings_=&settings;font_=&font;state_.settings=current;state_.owner474=state_.live=state_.visible=state_.request10=true;
    state_.state44=0;state_.previous50=state_.selection56=state_.substate48=0;state_.loader469=1;
    if(!clone_.ShowRoot(0)||!clone_.Bind(0,0,0))return Refuse({GameFsCode::Unavailable,0,"34F1C Open root refused"});
    *clone_.Control(0)={0,0,1,1};
    if(!fade.StartGameOpen())return Refuse({GameFsCode::Unavailable,0,"34F1C actual DA64 refused"});
    state_.source_consumer=0x34F1C;
    ++state_.source_ui_sound_intents; //source4AC38(5,0,12,128); audio equivalence remains qualified
    state_.opened=true;++state_.opens;state_.phase=2;input_.BeginContext();return {};
}
bool GameSystemOptionFrontier::SetInputEpoch(uint64_t epoch){if(!state_.live||state_.phase!=3||epoch<=state_.input_epoch)return false;state_.input_epoch=epoch;input_.BeginContext();return true;}
bool GameSystemOptionFrontier::Input(const GameInputFrame& f){
    if(f.overflow_resync||!f.focused){input_.BeginContext();++state_.rejected;return false;}
    const auto& l=state_.child_lease;
    if(!state_.live||state_.phase!=3||f.sequence<=state_.last_sequence||f.main_menu_epoch!=state_.input_epoch
       ||f.message_owner_generation!=l.script_generation||f.message_context!=l.context||f.returned_owner!=l.available_owner||f.returned_generation!=l.available_generation){++state_.rejected;return false;}
    if(!settings_||!SameSettings(state_.settings,settings_->OptionSettings())||!font_||!font_->Snapshot().ready){++state_.rejected;return false;}
    state_.last_sequence=f.sequence;input_.Observe(f);return true;
}
GameFsStatus GameSystemOptionFrontier::Tick(GameTransition& fade){
    if(state_.failed)return state_.failure;
    if(!state_.live)return {}; // retired/current-generation callbacks cannot replay
    try{
        NeedOption(state_.live&&settings_&&SameSettings(state_.settings,settings_->OptionSettings())&&font_&&font_->Snapshot().ready,"Options bound settings/FONT owner replaced");
        NeedOption(fade.Snapshot().live,"Options transition owner retired");
        if(state_.phase==6){message_.Close();clone_.Close();resource_.reset();state_.live=state_.visible=state_.owner474=state_.retirement_pending=false;
            state_.template_owner=state_.clone_owner=state_.requested=false;state_.retired=true;state_.retired_generation=state_.generation;++state_.retirements;state_.phase=7;return {};}
        std::string error;NeedOption(clone_.Update(error),error.c_str());
        if(state_.phase==2){if(fade.Snapshot().state==2){state_.state44=1;state_.phase=3;input_.BeginContext();}return {};}
        if(state_.phase==4){if(fade.Snapshot().state==0){NeedOption(clone_.ShowRoot(0,false),"Options close hide root");clone_.Control(0)->mode=clone_.Control(0)->active=0;
            state_.state44=3;state_.visible=false;++state_.close_completions;state_.loader469=0;state_.request10=false;++state_.polls;state_.poll_completed=true;
            state_.retirement_pending=true;++state_.retirement_requests;state_.phase=6;}return {};}
        if(state_.phase!=3)return {};
        input_.Tick();
        // NativeOptionsReadonlyFirstRow: consume directions/confirm as precise
        // unsupported routes. No setting/gain/modal effect is silently applied.
        const auto down=input_.Repeat(Down,10,2),up=input_.Repeat(Up,10,2),right=input_.Repeat(Right,10,2),left=input_.Repeat(Left,10,2);
        if(down||up||right||left||input_.First(Circle)){
            ++state_.unsupported;state_.source_consumer=down?0x350B0:up?0x3512C:right?0x3516C:left?0x351EC:0x352BC;
            return Refuse({GameFsCode::Unavailable,0,"Unsupported Options readonly input consumer at "+std::to_string(state_.source_consumer)});
        }
        else if(input_.First(Cross)){
            auto status=settings_->RestoreOptionSettings(state_.settings,state_.passive_settings);NeedOption(bool(status),status.detail.c_str());
            NeedOption(SameSettings(state_.settings,settings_->OptionSettings()),"Options backup readback mismatch");
            state_.settings.writes=settings_->OptionSettings().writes;state_.state44=2;state_.phase=4;++state_.close_requests;
            state_.source_ui_sound_intents+=2;fade.CloseWipe(false);input_.BeginContext();
        }
        //35078 returns into34C54 even after Close:352E0 with48=0 and
        // unchanged50==56 skips gain, then still services the real message.
        ++state_.after_key_updates;
        if(message_.Snapshot().complete68){const auto status=message_.Text(help_text);NeedOption(bool(status),status.detail.c_str());}
        const auto status=message_.Tick();NeedOption(bool(status),status.detail.c_str());return {};
    }catch(const std::exception& e){return Refuse({GameFsCode::Unavailable,0,e.what()});}
}
GameFsStatus GameSystemOptionFrontier::Draw(GameGraphics& graphics,const GameFontResource& font){
    if(state_.failed)return state_.failure;
    if(!state_.live||!state_.visible||state_.state44==3)return {};
    try{
        NeedOption(font_==&font&&font.Snapshot().ready&&settings_&&SameSettings(state_.settings,settings_->OptionSettings()),"Options draw dependency retired");
        GameUiDrawStats automatic;std::string error;NeedOption(clone_.Draw(graphics,automatic,error),error.c_str());uint32_t quads=automatic.quads;
        //34DC4 source manual root2 three bars; current readonly selection0.
        for(uint32_t row=0;row<3;++row){
            const auto frame=state_.passive_settings[row]+(row==state_.previous50?6:1);
            GameUiMatrix anchor;NeedOption(clone_.WorldMatrix(97+row,anchor),"Options bar anchor owner unavailable");
            // Named NativeUiViewspace source manual-root placement. Exact
            // source offsets DA520/DA524 are supplied in the integration pin.
            NeedOption(clone_.Bind(2,2,frame),"Options bar authored frame refused");
            auto* n=clone_.Node(2);n->position=n->base_position;n->scale=n->base_scale;n->angle=n->base_angle;n->color=n->base_color;
            //Source A0 is screen space; this native WorldMatrix is already
            // centered view space. Converting to source screen then subtracting
            // actual DA520=240/DA524=136 gives the same native anchor.
            n->position={{(anchor.tx+240.0f)-240.0f,(anchor.ty+136.0f)-136.0f}};
            GameUiDrawStats bar;NeedOption(clone_.Draw(graphics,bar,error,nullptr,{},0,UINT32_MAX,2),error.c_str());quads+=bar.quads;++state_.manual_bars;
        }
        auto status=message_.Draw(graphics);NeedOption(bool(status),status.detail.c_str());++state_.draw_frames;state_.quads=quads;return {};
    }catch(const std::exception& e){return Refuse({GameFsCode::Unavailable,0,e.what()});}
}
GameSystemOptionSnapshot GameSystemOptionFrontier::Snapshot()const{auto out=state_;out.message=message_.Snapshot();if(auto* c=clone_.Control(0)){out.frame=c->frame;out.clip=c->clip;}return out;}
}
