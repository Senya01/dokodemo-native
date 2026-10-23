#include "game_system_option_native.h"
#include "game_content_native.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void NeedOption(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool SameSettings(const GameOptionSettingsSnapshot& a,const GameOptionSettingsSnapshot& b){
    return a.live&&b.live&&a.owner&&a.generation&&a.owner==b.owner&&a.generation==b.generation&&a.words==b.words&&a.writes==b.writes&&a.protected_root==b.protected_root;
}
constexpr char help_text[]="\x82\xBD\x82\xBE\x82\xA2\x82\xDC\x83\x65\x83\x58\x83\x67\x92\x86\x81\x63\n";
}
bool GameOptionSnapshotExact(const GameOptionSettingsSnapshot& a,const GameOptionSettingsSnapshot& b)noexcept{
    return a.live==b.live&&a.owner==b.owner&&a.generation==b.generation&&a.writes==b.writes&&a.words==b.words&&a.protected_root==b.protected_root;
}
bool GameOptionReceiptExact(const GameOptionWriteReceipt& a,const GameOptionWriteReceipt& b)noexcept{
    return a.gui_generation==b.gui_generation&&a.input_sequence==b.input_sequence&&a.input_epoch==b.input_epoch
        &&a.stage==b.stage&&a.field==b.field&&a.call_pc==b.call_pc&&a.setter==b.setter&&a.value==b.value
        &&GameOptionSnapshotExact(a.before,b.before)&&GameOptionSnapshotExact(a.after,b.after);
}
bool GameOptionRightReceiptValid(const GameOptionWriteReceipt& r)noexcept{
    static constexpr uint32_t fields[]={1,0,1,2},calls[]={0x351CC,0x34CCC,0x34CD4,0x34CDC},setters[]={0x24B34,0x24B1C,0x24B34,0x24B4C};
    if(r.stage>=4||!r.gui_generation||!r.input_sequence||!r.input_epoch||r.field!=fields[r.stage]||r.call_pc!=calls[r.stage]
       ||r.setter!=setters[r.stage]||r.value!=3||!r.before.live||!r.before.owner||!r.before.generation||r.before.writes==UINT64_MAX)return false;
    auto expected=r.before;expected.words[r.field]=r.value;++expected.writes;
    return GameOptionSnapshotExact(expected,r.after);
}
bool GameOptionBackupReceiptValid(const GameOptionWriteReceipt& r)noexcept{
    if(r.stage>=3||!r.gui_generation||!r.input_sequence||!r.input_epoch||r.field!=r.stage
       ||r.call_pc!=0x3526C+r.stage*8||r.setter!=0x24B1C+r.stage*0x18
       ||!r.before.live||!r.before.owner||!r.before.generation||r.before.writes==UINT64_MAX)return false;
    auto expected=r.before;expected.words[r.field]=r.value;++expected.writes;
    return GameOptionSnapshotExact(expected,r.after);
}
bool GameOptionYesReceiptValid(const GameOptionWriteReceipt& r)noexcept{
    if(r.stage>=3||!r.gui_generation||!r.input_sequence||!r.input_epoch||r.field!=r.stage
       ||r.call_pc!=0x3534C+r.stage*8||r.setter!=0x24B1C+r.stage*0x18
       ||!r.before.live||!r.before.owner||!r.before.generation||r.before.writes==UINT64_MAX)return false;
    auto expected=r.before;expected.words[r.field]=r.value;++expected.writes;
    return GameOptionSnapshotExact(expected,r.after);
}
bool GameOptionRowReceiptValid(const GameOptionRowWriteReceipt& r)noexcept{
    const auto& w=r.write;if(r.row_count!=3||r.level_count!=5||r.selection_after!=1||!w.gui_generation||!w.input_sequence||!w.input_epoch
       ||!w.before.live||!w.before.owner||!w.before.generation||w.before.writes==UINT64_MAX||w.before.words!=r.local_before)return false;
    if(r.cause==GameOptionRowCause::Down){
        if(r.selection_before||r.local_before!=r.local_after||w.stage>=3||w.field!=w.stage||w.call_pc!=0x34CCC+8*w.stage)return false;
    }else if(r.cause==GameOptionRowCause::Left){
        if(r.selection_before!=1||w.stage||w.field!=1||w.call_pc!=0x351CC||!r.local_before[1]||r.local_before[1]>INT32_MAX)return false;
        auto expected=r.local_before;--expected[1];if(expected!=r.local_after)return false;
    }else if(r.cause==GameOptionRowCause::LeftCommon){
        if(r.selection_before!=1||r.local_before!=r.local_after||w.stage>=3||w.field!=w.stage||w.call_pc!=0x34CCC+8*w.stage)return false;
    }else return false;
    if(w.setter!=0x24B1C+0x18*w.field||w.value!=r.local_after[w.field])return false;
    auto after=w.before;after.words[w.field]=w.value;++after.writes;return GameOptionSnapshotExact(after,w.after);
}
bool GameOptionRowReceiptExact(const GameOptionRowWriteReceipt& a,const GameOptionRowWriteReceipt& b)noexcept{
    return a.cause==b.cause&&a.selection_before==b.selection_before&&a.selection_after==b.selection_after&&a.row_count==b.row_count
        &&a.level_count==b.level_count&&a.local_before==b.local_before&&a.local_after==b.local_after&&GameOptionReceiptExact(a.write,b.write);
}
struct GameOwnedMessageShared {
    std::shared_ptr<GameUiResource> resource;
    GameUiInstance clone;
    const GameFontResource* font=nullptr;
    std::vector<uint8_t> records;
    uint32_t references=0;
    uint32_t rate_setting=2,rate_updates=0;
    std::array<int16_t,5> rate_row{{4,10,14,30,24}};
    float rate=0.75f;
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
        const auto rate=SetRate(setting);if(!rate)return rate;
        //153D4 default config then Options1595C(9,2,4); mode4 uses root3,
        // before the normal-stack60 gate. No fabricated active stack flag.
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameOwnedOptionMessage::BindRateSource(GameOptionSettings& source){
    const auto current=source.OptionSettings();
    if(!state_.live||!current.live||!current.owner||!current.generation||rate_source_)
        return {GameFsCode::Unavailable,0,"150B8 current root owner unavailable"};
    rate_source_=&source;rate_owner_=current.owner;rate_generation_=current.generation;return {};
}
GameFsStatus GameOwnedOptionMessage::SetRate(uint32_t setting){
    if(!state_.live||!shared_||!shared_->references||(setting!=2&&setting!=3))
        return {GameFsCode::Unavailable,0,"Unsupported150B8 rate outside actual rows2/3"};
    // PH1:4340/434A, shared4360 row and4364 factor. Existing queued delay is
    // preserved; only the next source delay consumer reads the new row.
    shared_->rate_setting=setting;shared_->rate_row=setting==2?std::array<int16_t,5>{{4,10,14,30,24}}:std::array<int16_t,5>{{3,7,10,22,20}};
    shared_->rate=18.0f/float(shared_->rate_row[4]);++shared_->rate_updates;return {};
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
        if(rate_source_){const auto root=rate_source_->OptionSettings();NeedOption(root.live&&root.owner==rate_owner_&&root.generation==rate_generation_,"150B8 bound current root owner retired");}
        if(state_.state64==1){state_.appearance=(std::min)(1.0f,state_.appearance+0.1f);if(state_.appearance>=1){
            if(rate_source_){const auto status=SetRate(rate_source_->OptionSettings().words[0]);NeedOption(bool(status),status.detail.c_str());}
            state_.state64=2;}return {};}
        if(state_.state64==3){state_.appearance=(std::max)(0.0f,state_.appearance-0.1f);
            if(state_.appearance<=0){state_.appearance=0;state_.close69=false;lines_.clear();lines_.emplace_back();
                if(unit_cursor_<glyph_ends_.size()){state_.state64=1;state_.registered=true;}
                else{state_.state64=0;state_.registered=false;state_.complete68=true;state_.text.clear();state_.shown=state_.total=0;glyph_ends_.clear();unit_kinds_.clear();unit_cursor_=0;}}
            return {};}
        if(state_.state64!=2)return {};
        if(state_.close69){state_.complete68=true;state_.state64=3;return {};}
        // Native worker signed tick=1. Source-shared row2/3 supplies the real
        // ordinary/LF delays. General classification/scroll stays NativeRing.
        if(state_.delay70>0){--state_.delay70;if(state_.delay70>0)return {};}
        if(unit_cursor_<glyph_ends_.size()){
            const auto begin=unit_cursor_?glyph_ends_[unit_cursor_-1]:0;const auto end=glyph_ends_[unit_cursor_];
            const auto kind=unit_kinds_[unit_cursor_++];
            if(kind==10){lines_.emplace_back();if(lines_.size()>2)lines_.erase(lines_.begin());state_.delay70=shared_->rate_row[1];}
            else{lines_.back()+=state_.text.substr(begin,end-begin);++state_.shown;state_.delay70=shared_->rate_row[0];}
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
GameOwnedMessageSnapshot GameOwnedOptionMessage::Snapshot()const{auto s=state_;if(shared_){s.references=shared_->references;s.template_owner=bool(shared_->resource);s.clone_owner=shared_->clone.Node(0)!=nullptr;s.text_owner=shared_->records.size()==1024*44;s.rate=shared_->rate;s.rate_setting=shared_->rate_setting;s.rate_updates=shared_->rate_updates;}return s;}
void GameOwnedOptionMessage::Close()noexcept{
    if(shared_){if(state_.live&&shared_->references)--shared_->references;
        if(!shared_->references){std::vector<uint8_t>().swap(shared_->records);shared_->font=nullptr;shared_->clone.Close();shared_->resource.reset();}
        shared_.reset();}
    state_.live=state_.template_owner=state_.clone_owner=state_.text_owner=state_.registered=false;state_.references=state_.capacity=state_.used=0;lines_.clear();glyph_ends_.clear();unit_kinds_.clear();unit_cursor_=0;rate_source_=nullptr;rate_owner_=rate_generation_=0;
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
    if(prompt_)prompt_->Abort(state_.generation);prompt_=nullptr;
    message_.Close();settings_=nullptr;font_=nullptr;input_.BeginContext();state_.live=state_.visible=state_.request10=state_.owner474=state_.retirement_pending=false;
    clone_.Close();resource_.reset();state_.template_owner=state_.clone_owner=false;
    state_.requested=false; // Native failed-construction teardown, not93504/poll.
}

GameFsStatus GameSystemOptionFrontier::Initialize(GameResourceSource& source,const GameFontResource& font,GameOptionSettings& settings,GameTransition& fade,GameTrcPrompt* prompt){
    if(state_.failed)return state_.failure;
    const auto current=settings.OptionSettings();
    if(!current.live||!current.owner||!current.generation||current.words!=state_.passive_settings||!fade.Snapshot().live)
        return Refuse({GameFsCode::Unavailable,0,"Options current settings/transition owner unavailable"});
    auto status=PrepareTemplate(source);if(!status)return status;
    status=message_.Initialize(source,font,current.words[0]);if(!status)return Refuse(status);
    status=message_.BindRateSource(settings);if(!status)return Refuse(status);
    //347C8 returned: only now1A3B0 can publish474 and invoke34F1C.
    prompt_=prompt;
    if(prompt_){const auto p=prompt_->Snapshot();if(!p.live||p.failed||!p.resource_live||!p.font_live)return Refuse({GameFsCode::Unavailable,0,"Options actual TRC borrow retired before Open"});
        state_.prompt_bound_owner=p.owner;state_.prompt_bound_generation=p.generation;}
    settings_=&settings;font_=&font;state_.settings=current;state_.sound_owner=settings.OptionSoundOwner();state_.current_words=current.words;state_.owner474=state_.live=state_.visible=state_.request10=true;
    state_.state44=0;state_.previous50=state_.selection56=state_.substate48=0;state_.loader469=1;
    if(!clone_.ShowRoot(0)||!clone_.Bind(0,0,0))return Refuse({GameFsCode::Unavailable,0,"34F1C Open root refused"});
    *clone_.Control(0)={0,0,1,1};
    if(!fade.StartGameOpen())return Refuse({GameFsCode::Unavailable,0,"34F1C actual DA64 refused"});
    state_.source_consumer=0x34F1C;
    ++state_.source_ui_sound_intents; //source4AC38(5,0,12,128); audio equivalence remains qualified
    state_.opened=true;++state_.opens;state_.phase=2;input_.BeginContext();return {};
}
bool GameSystemOptionFrontier::SetInputEpoch(uint64_t epoch){if(!state_.live||state_.phase!=3||epoch<=state_.input_epoch)return false;
    if(prompt_&&state_.substate48&& !prompt_->SetInputEpoch(epoch))return false;
    state_.input_epoch=epoch;input_.BeginContext();return true;}
bool GameSystemOptionFrontier::Input(const GameInputFrame& f){
    if(f.overflow_resync||!f.focused){DisarmInput();++state_.rejected;return false;}
    if(state_.failed)return false;
    if(!PromptOwnerLive()){++state_.rejected;return false;}
    const auto& l=state_.child_lease;
    if(!state_.live||state_.phase!=3||f.sequence<=state_.last_sequence||f.main_menu_epoch!=state_.input_epoch
       ||f.message_owner_generation!=l.script_generation||f.message_context!=l.context||f.returned_owner!=l.available_owner||f.returned_generation!=l.available_generation){++state_.rejected;return false;}
    if(!settings_||!SameSettings(state_.settings,settings_->OptionSettings())||!font_||!font_->Snapshot().ready){++state_.rejected;return false;}
    if(state_.sound_owner.live&&!GameSourceSoundSnapshotExact(state_.sound_owner,settings_->OptionSoundOwner())){++state_.rejected;return false;}
    if(state_.substate48){if(!prompt_||prompt_->Snapshot().state864!=1){++state_.rejected;return false;}
        if(!prompt_->Input(f)){++state_.rejected;return false;}state_.last_sequence=f.sequence;return true;}
    state_.last_sequence=f.sequence;input_.Observe(f);return true;
}
bool GameSystemOptionFrontier::PublishFeedback(const GameSourceFeedbackRead& r){
    if(state_.failed||state_.phase!=3||state_.row_writes!=4||state_.row_left_steps!=1||state_.row_feedback_attempts!=1
       ||state_.source_consumer!=0x4AC84||(!GameSourceFeedbackReachedValid(r)&&!GameSourceVoiceFeedbackValid(r))
       ||!GameSourceSoundSnapshotExact(r.owner,state_.sound_owner)||!settings_
       ||!GameSourceSoundSnapshotExact(r.after,settings_->OptionSoundOwner()))return false;
    state_.row_feedback=r;state_.sound_owner=r.after;state_.source_consumer=r.frontier;return true;
}
bool GameSystemOptionFrontier::PublishVoiceService(const GameSourceFeedbackRead& r){
 if(state_.failed||state_.phase!=3||state_.row_writes!=7||state_.gain_queries!=2||state_.voice_service_attempts!=1||(!GameSourceVoiceServiceValid(r)&&!GameSourceSequencePrefixValid(r)&&!GameSourcePortPrefixValid(r)&&!GameSourceProcessorPrefixValid(r))
    ||!GameSourceSoundSnapshotExact(r.owner,state_.sound_owner)||!GameSourceSoundSnapshotExact(r.after,settings_->OptionSoundOwner()))return false;
 state_.row_voice_service=r;state_.sound_owner=r.after;state_.source_consumer=r.frontier;return true;
}
GameFsStatus GameSystemOptionFrontier::Tick(GameTransition& fade){
    if(state_.failed)return state_.failure;
    if(!state_.live)return {}; // retired/current-generation callbacks cannot replay
    try{
        NeedOption(PromptOwnerLive(),"Options frozen actual TRC owner replaced/retired");
        NeedOption(state_.live&&settings_&&SameSettings(state_.settings,settings_->OptionSettings())&&font_&&font_->Snapshot().ready,"Options bound settings/FONT owner replaced");
        NeedOption(fade.Snapshot().live,"Options transition owner retired");
        NeedOption(!state_.sound_owner.live||GameSourceSoundSnapshotExact(state_.sound_owner,settings_->OptionSoundOwner()),"Options bound source sound owner replaced");
        if(state_.phase==6){message_.Close();clone_.Close();resource_.reset();state_.live=state_.visible=state_.owner474=state_.retirement_pending=false;
            state_.template_owner=state_.clone_owner=state_.requested=false;state_.retired=true;state_.retired_generation=state_.generation;++state_.retirements;state_.phase=7;return {};}
        std::string error;NeedOption(clone_.Update(error),error.c_str());
        if(state_.phase==2){if(fade.Snapshot().state==2){state_.state44=1;state_.phase=3;input_.BeginContext();++state_.input_boundaries;state_.boundary_cause=1;}return {};}
        if(state_.phase==4){if(fade.Snapshot().state==0){NeedOption(clone_.ShowRoot(0,false),"Options close hide root");clone_.Control(0)->mode=clone_.Control(0)->active=0;
            state_.state44=3;state_.visible=false;++state_.close_completions;state_.loader469=0;state_.request10=false;++state_.polls;state_.poll_completed=true;
            state_.retirement_pending=true;++state_.retirement_requests;state_.phase=6;}return {};}
        if(state_.phase!=3)return {};
        if(state_.row_feedback.outcome==1&&state_.row_writes==7){
            //NativeWorkerVoiceServiceAfterOfferedFrame: a separate worker
            //invocation after the actual changed authored frame was drawn.
            if(state_.draw_frames<=state_.row_left_draw_frames_before)return {};
            ++state_.voice_service_attempts;const auto status=settings_->ServiceOptionVoice(state_.settings,state_.row_voice_service);
            if(GameSourceVoiceServiceValid(state_.row_voice_service)||GameSourceSequencePrefixValid(state_.row_voice_service)||GameSourcePortPrefixValid(state_.row_voice_service)||GameSourceProcessorPrefixValid(state_.row_voice_service)){state_.sound_owner=state_.row_voice_service.after;state_.source_consumer=state_.row_voice_service.frontier;}
            if(!status)return Refuse(status);
            return Refuse({GameFsCode::Unavailable,0,"Unsupported selected sequence callback continuation"});
        }
        bool nested_at_entry=state_.substate48!=0;
        if(prompt_){const auto before=prompt_->Snapshot();const auto status=prompt_->Tick();if(!status)return Refuse(status);
            if(nested_at_entry&&before.state864==0&&prompt_->Snapshot().state864==1){++state_.input_boundaries;state_.boundary_cause=3;}}
        if(!nested_at_entry)input_.Tick();
        bool right_completed=false,after_key_done=false,circle_completed=false;
        // Query in source priority. Later counters are not sampled after an
        // earlier nonzero direction consumed this key invocation.
        const auto down=nested_at_entry?0:input_.Repeat(Down,10,2),up=(nested_at_entry||down)?0:input_.Repeat(Up,10,2),right=(nested_at_entry||down||up)?0:input_.Repeat(Right,10,2),left=(nested_at_entry||down||up||right)?0:input_.Repeat(Left,10,2);
        if(down&&settings_->OptionRowWritable()){
            state_.source_consumer=0x350B0;
            NeedOption(!state_.row_selections&&!state_.right_steps&&!state_.selection56&&!state_.previous50&&!state_.substate48
                &&state_.current_words==state_.passive_settings,"Unsupported Options Down outside fresh row0-to1 domain");
            state_.row_message_before=message_.Snapshot();state_.row_draw_frames_before=state_.draw_frames;
            state_.row_before=state_.current_words;state_.selection56=1;++state_.row_selections;
            ++state_.source_ui_sound_intents; //35128 selection store then35114 event6, existing UI-intent policy
            ++state_.after_key_updates;after_key_done=true;state_.previous50=1;*clone_.Control(1)={2,1,2,1};
            for(uint32_t stage=0;stage<3;++stage){
                GameOptionRowWriteReceipt receipt;state_.source_consumer=0x34CCC+8*stage;
                const auto status=settings_->WriteOptionRow(state_.settings,GameOptionRowCause::Down,stage,state_.current_words[stage],receipt);if(!status)return Refuse(status);
                NeedOption(GameOptionRowReceiptValid(receipt)&&receipt.cause==GameOptionRowCause::Down&&receipt.write.stage==stage
                   &&receipt.write.gui_generation==state_.generation&&receipt.write.input_sequence==state_.last_sequence&&receipt.write.input_epoch==state_.input_epoch
                   &&receipt.local_before==state_.row_before&&receipt.local_after==state_.current_words
                   &&GameOptionSnapshotExact(receipt.write.before,state_.settings)&&GameOptionSnapshotExact(receipt.write.after,settings_->OptionSettings()),"Options Down common writer receipt/readback refused");
                state_.row_receipts[stage]=receipt;++state_.row_writes;state_.settings=receipt.write.after;
            }
            NeedOption(state_.settings.words[2]==3,"Unsupported selected root18 gain index");
            state_.source_consumer=0x4AFC8;++state_.gain_queries;state_.gain_value=105;
            const auto status=settings_->QueryOptionGain(state_.settings,105,state_.gain);if(!status)return Refuse(status);
            NeedOption(state_.gain.source_id_known&&!state_.gain.source_dispatched&&GameSourceSoundGainValid(state_.gain.source)
                &&state_.gain.source.value==105&&GameSourceSoundSnapshotExact(state_.sound_owner,state_.gain.source.before)
                &&GameSourceSoundSnapshotExact(state_.gain.source.after,settings_->OptionSoundOwner()),"Options Down genuine gain return refused");
            state_.sound_owner=state_.gain.source.after;state_.gain_returned=true;right_completed=true;
        }else if(left&&settings_->OptionRowWritable()){
            state_.source_consumer=0x351EC;
            NeedOption(state_.row_selections==1&&!state_.row_left_steps&&state_.row_writes==3&&state_.selection56==1&&state_.previous50==1&&!state_.substate48
                &&state_.current_words==state_.row_before,"Unsupported Options Left outside selected row1 domain");
            right_completed=true;state_.source_consumer=0x351EC;
            if(state_.current_words[1]&&state_.current_words[1]<=INT32_MAX){
                state_.row_left_after_key_before=state_.after_key_updates;state_.row_left_draw_frames_before=state_.draw_frames;
                --state_.current_words[1];state_.previous50=UINT32_MAX;++state_.row_left_steps;
                state_.source_consumer=0x351CC;GameOptionRowWriteReceipt receipt;
                const auto status=settings_->WriteOptionRow(state_.settings,GameOptionRowCause::Left,0,state_.current_words[1],receipt);if(!status)return Refuse(status);
                NeedOption(GameOptionRowReceiptValid(receipt)&&receipt.cause==GameOptionRowCause::Left&&receipt.write.gui_generation==state_.generation
                   &&receipt.write.input_sequence==state_.last_sequence&&receipt.write.input_epoch==state_.input_epoch&&receipt.local_before==state_.row_before
                   &&receipt.local_after==state_.current_words&&GameOptionSnapshotExact(receipt.write.before,state_.settings)
                   &&GameOptionSnapshotExact(receipt.write.after,settings_->OptionSettings()),"Options Left direct writer receipt/readback refused");
                state_.row_receipts[3]=receipt;++state_.row_writes;state_.settings=receipt.write.after;
                state_.source_consumer=0x4AC84;++state_.row_feedback_attempts;
                const auto feedback=settings_->ReadOptionFeedback(state_.settings,state_.row_feedback);
                if(GameSourceFeedbackReachedValid(state_.row_feedback)||GameSourceVoiceFeedbackValid(state_.row_feedback)){
                    NeedOption(GameSourceSoundSnapshotExact(state_.row_feedback.after,settings_->OptionSoundOwner()),"Options reached feedback current owner readback");
                    state_.sound_owner=state_.row_feedback.after;state_.source_consumer=state_.row_feedback.frontier;
                }
                if(!feedback)return Refuse(feedback);
                NeedOption(GameSourceVoiceFeedbackValid(state_.row_feedback),"Options returned voice outcome unavailable");
                ++state_.source_ui_sound_intents; //actual feedback returned, then352E0/common
                ++state_.after_key_updates;after_key_done=true;state_.previous50=state_.selection56;*clone_.Control(1)={2,1,2,1};
                for(uint32_t stage=0;stage<3;++stage){GameOptionRowWriteReceipt common;state_.source_consumer=0x34CCC+stage*8;
                    const auto written=settings_->WriteOptionRow(state_.settings,GameOptionRowCause::LeftCommon,stage,state_.current_words[stage],common);if(!written)return Refuse(written);
                    NeedOption(GameOptionRowReceiptValid(common)&&common.cause==GameOptionRowCause::LeftCommon&&common.write.gui_generation==state_.generation
                         &&common.write.input_sequence==state_.last_sequence&&common.write.input_epoch==state_.input_epoch&&common.local_after==state_.current_words
                         &&GameOptionSnapshotExact(common.write.before,state_.settings)&&GameOptionSnapshotExact(common.write.after,settings_->OptionSettings()),"Options Left common receipt/readback");
                    state_.row_receipts[4+stage]=common;++state_.row_writes;state_.settings=common.write.after;
                }
                state_.source_consumer=0x4AFC8;++state_.gain_queries;state_.gain_returned=false;state_.gain_value=105;
                const auto queried=settings_->QueryOptionGain(state_.settings,105,state_.gain);if(!queried)return Refuse(queried);
                NeedOption(state_.gain.source_id_known&&!state_.gain.source_dispatched&&GameSourceSoundGainValid(state_.gain.source)&&state_.gain.source.value==105
                     &&GameSourceSoundSnapshotExact(state_.gain.source.before,state_.sound_owner)
                     &&GameSourceSoundSnapshotExact(state_.gain.source.after,settings_->OptionSoundOwner()),"Options second actual gain return");
                state_.sound_owner=state_.gain.source.after;state_.gain_returned=true;right_completed=true;
            } //source row1 <=0 is a no-op; no wrap, setter or feedback
        }
        if(right&&settings_->OptionRightWritable()){
            NeedOption(!state_.right_steps&&state_.selection56==0&&state_.previous50==0&&state_.substate48==0
                       &&state_.current_words==std::array<uint32_t,3>{{2,3,3}}&&state_.passive_settings==state_.current_words,
                       "Unsupported Options Right outside exact current2/limit5/row0 domain");
            //351DC/351E8: signed2<4, local increment and previous invalidation.
            state_.current_words[0]=3;state_.previous50=UINT32_MAX;++state_.right_steps;
            state_.right_message_updates_before=message_.Snapshot().updates;state_.right_draw_frames_before=state_.draw_frames;
            const auto write=[&](uint32_t stage)->GameFsStatus{
                GameOptionWriteReceipt receipt;state_.source_consumer=stage?0x34CCC+(stage-1)*8:0x351CC;
                const auto status=settings_->WriteOptionRight(state_.settings,stage,3,receipt);
                if(!status)return status;
                NeedOption(GameOptionRightReceiptValid(receipt)&&receipt.stage==stage&&receipt.gui_generation==state_.generation
                           &&receipt.input_sequence==state_.last_sequence&&receipt.input_epoch==state_.input_epoch
                           &&GameOptionSnapshotExact(receipt.before,state_.settings)&&GameOptionSnapshotExact(receipt.after,settings_->OptionSettings()),
                           "Options actual source writer receipt/readback mismatch");
                state_.right_receipts[stage]=receipt;++state_.right_writes;state_.settings=receipt.after;
                return {};
            };
            auto direct=write(0);if(!direct)return Refuse(direct);++state_.source_ui_sound_intents; //direct root14, then event6
            ++state_.after_key_updates; //352E0 substate0 returns, then common gate
            after_key_done=true;
            state_.previous50=state_.selection56;*clone_.Control(1)={1,1,2,1};
            for(uint32_t stage=1;stage<4;++stage){const auto status=write(stage);if(!status)return Refuse(status);}
            //14834(1): actual root18=3 -> index8 -> exact retained105.
            state_.source_consumer=0x4AFC8;++state_.gain_queries;state_.gain_value=105;
            const auto gain=settings_->QueryOptionGain(state_.settings,105,state_.gain);
            if(!gain)return Refuse(gain);
            NeedOption(state_.gain.source_id_known&&!state_.gain.source_dispatched&&GameSourceSoundGainValid(state_.gain.source)
                       &&state_.gain.source.value==105&&GameSourceSoundSnapshotExact(state_.sound_owner,state_.gain.source.before)
                       &&GameSourceSoundSnapshotExact(state_.gain.source.after,settings_->OptionSoundOwner()),"Options genuine source gain return receipt refused");
            state_.sound_owner=state_.gain.source.after;state_.gain_returned=true;right_completed=true;
        }
        if(!nested_at_entry&&!right_completed&&!down&&!up&&!right&&!left&&input_.First(Circle)){
            if(state_.row_selections){++state_.unsupported;state_.source_consumer=0x352BC;return Refuse({GameFsCode::Unavailable,0,"Unsupported selected row1 prompt"});}
            if(!prompt_){++state_.unsupported;state_.source_consumer=0x352BC;
                return Refuse({GameFsCode::Unavailable,0,"Unsupported Options actual global TRC owner unavailable at352BC"});}
            NeedOption(prompt_&&prompt_->Snapshot().live&&!prompt_->Snapshot().failed&&prompt_->Snapshot().state864==3,"Unsupported Options actual global TRC owner unavailable at352BC");
            state_.substate48=2;circle_completed=true;++state_.source_ui_sound_intents;
        }
        if(!right_completed&&!circle_completed&&(down||up||right||left)){
            ++state_.unsupported;state_.source_consumer=down?0x350B0:up?0x3512C:right?0x3516C:left?0x351EC:0x352BC;
            return Refuse({GameFsCode::Unavailable,0,"Unsupported Options readonly input consumer at "+std::to_string(state_.source_consumer)});
        }
        else if(!nested_at_entry&&!right_completed&&!circle_completed&&input_.First(Cross)){
            NeedOption(!state_.row_selections,"Unsupported selected row1 Cancel before feedback continuation");
            if(settings_->OptionBackupWritable()){
                for(uint32_t stage=0;stage<3;++stage){GameOptionWriteReceipt receipt;state_.source_consumer=0x3526C+stage*8;
                    const auto status=settings_->WriteOptionBackup(state_.settings,stage,state_.passive_settings[stage],receipt);if(!status)return Refuse(status);
                    NeedOption(GameOptionBackupReceiptValid(receipt)&&receipt.stage==stage&&receipt.gui_generation==state_.generation
                               &&receipt.input_sequence==state_.last_sequence&&receipt.input_epoch==state_.input_epoch&&receipt.value==state_.passive_settings[stage]
                               &&GameOptionSnapshotExact(receipt.before,state_.settings)&&GameOptionSnapshotExact(receipt.after,settings_->OptionSettings()),"Options actual backup setter receipt/readback mismatch");
                    state_.restore_receipts[stage]=receipt;++state_.restore_writes;state_.settings=receipt.after;
                }
            }else{
                NeedOption(!state_.right_steps,"Options changed backup requires source writer receipts");
                const auto status=settings_->RestoreOptionSettings(state_.settings,state_.passive_settings);if(!status)return Refuse(status);
                state_.settings.words=state_.passive_settings;state_.settings.writes+=3;
            }
            auto restored=state_.settings;
            NeedOption(SameSettings(restored,settings_->OptionSettings()),"Options backup readback mismatch");
            state_.settings=restored;state_.close_kind=1;state_.state44=2;state_.phase=4;++state_.close_requests;
            state_.source_ui_sound_intents+=2;fade.CloseWipe(false);input_.BeginContext();
        }
        //35078 returns into34C54 even after Close:352E0 with48=0 and
        // unchanged50==56 skips gain, then still services the real message.
        if(state_.substate48==2){
            const auto status=prompt_->Open(state_.generation,state_.input_epoch);if(!status)return Refuse(status);
            state_.substate48=3;++state_.prompt_opens;++state_.input_boundaries;state_.boundary_cause=2;input_.BeginContext();
        }else if(state_.substate48==3&&prompt_->Snapshot().state864==3){
            const auto p=prompt_->Snapshot();
            NeedOption(p.parent==state_.generation&&p.completed_invocation==p.invocation&&!p.active
                       &&p.result_sequence==state_.last_sequence&&p.result_epoch==state_.input_epoch,"TRC natural result/current owner unavailable");
            if(p.result86C==2){
                state_.substate48=0;++state_.prompt_returns;++state_.input_boundaries;state_.boundary_cause=4;input_.BeginContext();
            }else if(p.result86C==1){
                NeedOption(!p.selection&&!state_.yes_consumed&&!state_.yes_writes&&!state_.restore_writes&&!state_.close_requests,"Options Yes current chain already consumed");
                for(uint32_t stage=0;stage<3;++stage){
                    GameOptionWriteReceipt receipt;state_.source_consumer=0x3534C+stage*8;
                    const auto status=settings_->WriteOptionYes(state_.settings,stage,state_.current_words[stage],receipt);if(!status)return Refuse(status);
                    NeedOption(GameOptionYesReceiptValid(receipt)&&receipt.stage==stage&&receipt.gui_generation==state_.generation
                               &&receipt.input_sequence==p.result_sequence&&receipt.input_epoch==p.result_epoch&&receipt.value==state_.current_words[stage]
                               &&GameOptionSnapshotExact(receipt.before,state_.settings)&&GameOptionSnapshotExact(receipt.after,settings_->OptionSettings()),"Options actual current Yes setter receipt/readback mismatch");
                    state_.yes_receipts[stage]=receipt;++state_.yes_writes;state_.settings=receipt.after;
                }
                ++state_.yes_consumed;state_.source_consumer=0x35364;state_.close_kind=2;state_.state44=2;state_.phase=4;++state_.close_requests;
                //35364 calls35028 directly: Close event12, no Cancel event5.
                ++state_.source_ui_sound_intents;fade.CloseWipe(false);input_.BeginContext();
            }else return Refuse({GameFsCode::Unavailable,0,"Unsupported TRC result partition"});
        }
        if(!after_key_done)++state_.after_key_updates;
        // The finite route keeps50==56 after Right and after distinct Cancel.
        // Cancel never changes localC, and never returns before this tail.
        NeedOption(state_.previous50==state_.selection56&&(state_.substate48==0||state_.substate48==3),"Unsupported Options dirty common update");
        if(!state_.previous50){
        if(message_.Snapshot().complete68){auto status=message_.SetRate(settings_->OptionSettings().words[0]);NeedOption(bool(status),status.detail.c_str());
            status=message_.Text(help_text);NeedOption(bool(status),status.detail.c_str());}
        const auto status=message_.Tick();NeedOption(bool(status),status.detail.c_str());
        }
        if(state_.gain_returned&&!state_.row_selections)state_.right_message_continued=true;return {};
    }catch(const std::exception& e){return Refuse({GameFsCode::Unavailable,0,e.what()});}
}
GameFsStatus GameSystemOptionFrontier::Draw(GameGraphics& graphics,const GameFontResource& font){
    if(state_.failed)return state_.failure;
    if(!state_.live||!state_.visible||state_.state44==3)return {};
    try{
        NeedOption(PromptOwnerLive(),"Options draw frozen TRC borrow retired");
        NeedOption(font_==&font&&font.Snapshot().ready&&settings_&&SameSettings(state_.settings,settings_->OptionSettings()),"Options draw dependency retired");
        NeedOption(!state_.sound_owner.live||GameSourceSoundSnapshotExact(state_.sound_owner,settings_->OptionSoundOwner()),"Options draw source sound owner retired");
        // NativeJoinedManualRoots:34DC4 owns exactly three explicit root2
        // samples. Exclude root2 and its references from the automatic pass.
        // Original B8/global scheduler lifetime remains qualified; B0 is a
        // render-state ID, not a proved visibility/frame suppression flag.
        GameUiDrawStats automatic;std::string error;NeedOption(clone_.Draw(graphics,automatic,error,nullptr,{},0,UINT32_MAX,-1,2),error.c_str());uint32_t quads=automatic.quads;
        //34DC4 source manual root2 three bars; current readonly selection0.
        for(uint32_t row=0;row<3;++row){
            const auto frame=state_.current_words[row]+(row==state_.previous50?6:1);
            GameUiMatrix anchor;NeedOption(clone_.WorldMatrix(97+row,anchor),"Options bar anchor owner unavailable");
            // Named NativeUiViewspace source manual-root placement. Exact
            // source offsets DA520/DA524 are supplied in the integration pin.
            auto* control=clone_.Control(2);NeedOption(control!=nullptr,"Options bar control owner retired");
            control->frame=frame;
            NeedOption(clone_.SampleControl(2,*control,error),error.c_str());
            auto* n=clone_.Node(2);n->position=n->base_position;n->scale=n->base_scale;n->angle=n->base_angle;n->color=n->base_color;
            //Source A0 is screen space; this native WorldMatrix is already
            // centered view space. Converting to source screen then subtracting
            // actual DA520=240/DA524=136 gives the same native anchor.
            //97B18 writes base+20, then97AE4 refreshes this root's working
            // pose. Descendants retain the just-sampled control positions.
            n->base_position={{(anchor.tx+240.0f)-240.0f,(anchor.ty+136.0f)-136.0f}};
            n->position=n->base_position;
            GameUiDrawStats bar;NeedOption(clone_.Draw(graphics,bar,error,nullptr,{},0,UINT32_MAX,2),error.c_str());quads+=bar.quads;++state_.manual_bars;state_.bar_frames[row]=frame;
        }
        GameFsStatus status;if(!state_.selection56){status=message_.Draw(graphics);NeedOption(bool(status),status.detail.c_str());}if(prompt_&&state_.substate48){status=prompt_->Draw(graphics);if(!status)return Refuse(status);}
        ++state_.draw_frames;state_.quads=quads;return {};
    }catch(const std::exception& e){return Refuse({GameFsCode::Unavailable,0,e.what()});}
}
bool GameSystemOptionFrontier::PromptOwnerLive()const{if(!prompt_)return !state_.prompt_bound_owner;const auto p=prompt_->Snapshot();return p.live&&!p.failed&&p.resource_live&&p.font_live&&p.owner==state_.prompt_bound_owner&&p.generation==state_.prompt_bound_generation;}
GameSystemOptionSnapshot GameSystemOptionFrontier::Snapshot()const{auto out=state_;out.message=message_.Snapshot();if(prompt_)out.prompt=prompt_->Snapshot();if(auto* c=clone_.Control(0)){out.frame=c->frame;out.clip=c->clip;}if(auto* c=clone_.Control(1))out.marker_frame=c->frame;return out;}
}
