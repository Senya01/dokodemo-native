#include "game_diary_native.h"
#include "game_content_native.h"
#include "core/cp932.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {void Need(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}}
GameRecordImage GameProjectDiaryOpen(const GameRecordImage& before,const GameDiaryOpenInputs& in,GameDiarySelection& selected){
    Need(before.live&&before.generation&&before.sha==GameHashRecordImage(before),"Diary immutable bank owner invalid");
    Need(in.profile.live&&in.profile.actor<=4&&int32_t(in.profile.actor)==in.manager58&&in.script_generation&&in.run_id,
         "Diary actual TITLE24B94 -> LoadMan24B88 current actor/profile witness missing");
    Need(in.date==GameRecordDateFromClockWord(in.clock_word)&&in.hour==((in.clock_word&0x0FFFFFFF)%1440)/60,
         "Diary same current clock/calendar witness inconsistent");
    auto next=before;auto& bank=next.banks[0];
    std::copy(in.profile.source_nickname.begin(),in.profile.source_nickname.end(),bank.names.begin());
    std::copy(in.profile.source_player.begin(),in.profile.source_player.end(),bank.names.begin()+16);bank.marker=in.manager58;
    selected=GameSelectDiary39824(bank,in.date,in.hour,in.mode);bank=selected.bank;next.lookup=selected.lookup;
    next.sha=GameHashRecordImage(next);return next;
}
GameFsStatus GameDiary::Request(const GameRecordImage& before,GameDiaryOpenInputs inputs,GameNotebookLease lease){
    try{
        Need(!state_.live&&!state_.retirement_pending&&lease.script_generation==inputs.script_generation&&lease.run_id==inputs.run_id
             &&lease.context&&lease.parent_generation&&lease.available_owner&&lease.available_generation&&lease.input_epoch,"Diary request/lease unavailable");
        GameDiarySelection selection;auto approved=GameProjectDiaryOpen(before,inputs,selection);
        const auto old=state_;GameDiarySnapshot next;next.generation=old.generation+1;next.requests=old.requests+1;
        next.opens=old.opens;next.close_requests=old.close_requests;next.close_completions=old.close_completions;next.polls=old.polls;
        next.retirement_requests=old.retirement_requests;next.retirements=old.retirements;
        next.live=next.request12=true;next.phase=1;next.lease=lease;next.input_epoch=lease.input_epoch;next.inputs=std::move(inputs);
        next.before_sha=before.sha;next.approved_sha=approved.sha;
        before_=before;approved_=std::move(approved);selection_=std::move(selection);state_=std::move(next);input_.BeginContext();return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameDiary::Initialize(GameResourceSource& source,GameDiaryIo& io,GameRecordStore& records,GameTransition& fade){
    Need(fade.Snapshot().live&&records.Matches(before_),"Diary current transition/bank owner replaced before Open");
    const auto manager=io.DiaryIo();Need(manager.live&&manager.owner&&manager.generation,"Diary actual borrowed IO manager unavailable");
    auto resource=std::make_shared<GameUiResource>();auto status=resource->Load(source,"enikki\\gui\\enikki\\ENIKKI.CB2A","enikki\\gui\\enikki\\");if(!status)return status;
    GameUiInstance clone;Need(clone.Clone(resource),"Diary actual clone failed");
    for(uint32_t i=0;i<4;++i)Need(clone.Bind(i,i,0),"Diary constructor source binding");
    for(uint32_t i=0;i<4;++i){auto* c=clone.AddControl(i);Need(c!=nullptr,"Diary four controls missing");if(i)*c={1,i,2,1};}
    // Source backing has208-byte nodes; native typed node mapping is qualified.
    Need(clone.SetSourceCallback(92,0x393A4)&&clone.SetSourceCallback(93,0x3953C)
         &&clone.SetSourceCallback(94,0x39684)&&clone.SetSourceCallback(78,0x3973C),"Diary four actual callback nodes missing");
    Need(clone.ShowRoot(0)&&clone.Bind(0,0,0),"Diary actual Open root");*clone.Control(0)={0,0,1,1};
    std::vector<uint8_t> batch(1024*44);
    GameDiarySelection selected;GameRecordImage promoted;
    const auto& in=state_.inputs;status=records.OpenDiary(before_,in.profile.source_nickname,in.profile.source_player,in.manager58,in.date,in.hour,in.mode,selected,promoted);
    if(!status)return status;Need(promoted.sha==approved_.sha,"Diary source Open patch differs from private expected image");
    selection_=std::move(selected);resource_=std::move(resource);clone_=std::move(clone);text_records_.swap(batch);
    state_.candidate=selection_.candidate;state_.count15C=selection_.count;state_.selection160=state_.current164=selection_.selection;
    state_.selected_row=uint32_t(selection_.rows[state_.current164]);state_.selected_id=selection_.bank.rows[state_.selected_row].value;
    state_.owner47C=state_.nested_owner=state_.text_owner=state_.visible=true;state_.text_capacity=1024;
    state_.image_actor=selection_.bank.marker;state_.io=manager;io_=&io;state_.opened_generation=state_.generation;
    state_.state154=4;state_.loader469=1;state_.phase=2;++state_.opens;return {};
}
GameFsStatus GameDiary::PrepareSelected(){
    Need(state_.current164<selection_.count,"Diary selected ordinal outside actual row-index list");
    state_.selected_row=uint32_t(selection_.rows[state_.current164]);const auto& row=selection_.bank.rows.at(state_.selected_row);
    state_.selected_id=row.value;state_.image_stem.clear();state_.image_frame=0;
    if(row.value>=0&&state_.image_actor>=0){
        // Exact relocated PH1+62D8[0] ->4D98 +8*12 -> PH0+DA778,frame1.
        Need(state_.image_actor==0&&row.value==12,"UnsupportedDiaryImagePair:actual actor/record has no recovered pair");
        state_.image_stem="d11_N";state_.image_frame=1;
    }
    text_.clear();secondary_.clear();
    if(row.flag){
        Need(row.source_known_payload_bytes>0&&row.source_known_payload_bytes<=200,"UnsupportedDiaryTextKnownExtent");
        const auto end=std::find(row.payload.begin(),row.payload.begin()+row.source_known_payload_bytes,uint8_t(0));
        Need(end!=row.payload.begin()+row.source_known_payload_bytes,"UnsupportedDiaryTextTerminator");
        text_.assign(row.payload.begin(),end);
        Need(state_.image_frame!=3,"UnsupportedDiary14D2CSecondaryText");
    }
    const uint32_t frame=row.flag?uint32_t(state_.image_frame):1;
    for(uint32_t i:{1u,2u}){auto* c=clone_.Control(i);c->frame=frame;c->mode=2;c->active=1;}
    auto* nav=clone_.Control(3);nav->frame=state_.count15C==1?1:state_.selection160==0?2:state_.selection160==state_.count15C-1?4:3;
    nav->mode=2;nav->active=1;state_.state150=1;state_.nested20=0;state_.image_ready=0;return {};
}
GameFsStatus GameDiary::Tick(GameResourceSource& source,GameDiaryIo& io,GameRecordStore& records,GameTransition& fade){
    try{
        if(state_.failed)return state_.failure;
        if(state_.phase==1)return Refuse(Initialize(source,io,records,fade));
        if(!state_.live)return {};
        Need(io_==&io&&IoLive(),"Diary borrowed current IO/filesystem owner retired or replaced");
        state_.io=io.DiaryIo();
        Need(fade.Snapshot().live&&records.Matches(approved_),"Diary approved bank/transition changed during modal");
        if(state_.phase==6){
            // Actual deleting+4: nested first, then text, clone, template.
            picture_.reset();state_.nested_owner=false;
            std::vector<uint8_t>().swap(text_records_);font_=nullptr;state_.text_owner=false;state_.text_capacity=state_.text_used=0;
            clone_.Close();resource_.reset();state_.live=state_.owner47C=state_.retirement_pending=false;
            state_.retired_generation=state_.generation;++state_.retirements;state_.phase=7;return {};
        }
        std::string error;Need(clone_.Update(error),error.c_str());
        if(state_.state154==4){if(fade.Snapshot().state==0){state_.state154=5;state_.current164=state_.selection160;}return {};}
        if(state_.state154==5){
            if(!state_.state150)return PrepareSelected();
            const auto current=io.DiaryIo();Need(current.live&&current.owner==state_.io.owner&&current.generation==state_.io.generation,"Diary borrowed IO manager replaced");state_.io=current;
            switch(state_.nested20){
            case 0:picture_.reset();state_.image_ready=0;state_.nested20=1;return {};
            case 1:state_.nested20=2;return {}; // old image lease already released, no fabricated unrequest
            case 2:
                if(!state_.image_stem.empty()){const auto status=io.RequestDiaryImage(state_.image_stem,picture_);if(!status)return Refuse(status);++state_.image_requests;}
                state_.nested20=3;return {};
            case 3:if(current.ready&&!current.pending){state_.nested20=4;}return {};
            case 4:
                Need(state_.image_stem.empty()||bool(picture_),"Diary requested image backing missing at Prepare3824C");
                state_.image_ready=1;state_.nested20=0;state_.state150=0;
                Need(fade.StartGameOpen(),"Diary actual ready DA64 Open refused");state_.state154=0;return {};
            default:throw std::runtime_error("Diary nested image phase outside source table");}
        }
        if(state_.state154==0){if(fade.Snapshot().state==2){state_.state154=1;state_.phase=3;input_.BeginContext();}return {};}
        if(state_.state154==2){if(fade.Snapshot().state==0){
            Need(clone_.ShowRoot(0,false),"Diary actual closing hide");clone_.Control(0)->mode=clone_.Control(0)->active=0;
            state_.state154=3;state_.visible=false;++state_.close_completions;
            state_.loader469=0;state_.request12=false;++state_.polls;state_.retirement_pending=true;++state_.retirement_requests;state_.phase=6;
        }return {};}
        if(state_.state154!=1)return {};
        input_.Tick();const auto right=input_.Repeat(Right,10,2),left=input_.Repeat(Left,10,2);
        if(right||left){const auto old=state_.selection160;
            if(right&&state_.selection160+1<state_.count15C)++state_.selection160;
            else if(!right&&left&&state_.selection160)--state_.selection160;
            if(old!=state_.selection160){++state_.navigation;fade.CloseWipe(false);state_.state154=4;state_.phase=2;input_.BeginContext();}
            return {};
        }
        if(input_.First(Cross)){state_.state154=2;state_.phase=4;++state_.close_requests;fade.CloseWipe(false);input_.BeginContext();}return {};
    }catch(const std::exception& e){return Refuse({GameFsCode::Unavailable,0,e.what()});}
}
bool GameDiary::Input(const GameInputFrame& f){
    if(f.overflow_resync||!f.focused){input_.BeginContext();++state_.rejected;return false;}
    const auto& l=state_.lease;
    if(state_.failed||!IoLive()||!state_.live||state_.phase!=3||f.sequence<=state_.last_sequence||f.main_menu_epoch!=state_.input_epoch
       ||f.message_owner_generation!=l.script_generation||f.message_context!=l.context||f.returned_owner!=l.available_owner
       ||f.returned_generation!=l.available_generation){++state_.rejected;return false;}
    state_.last_sequence=f.sequence;input_.Observe(f);return true;
}
bool GameDiary::SetInputEpoch(uint64_t epoch){if(!state_.live||state_.phase!=3||epoch<=state_.input_epoch)return false;
    state_.input_epoch=epoch;input_.BeginContext();return true;}
bool GameDiary::DrawUiCallback(uint32_t pc,uint32_t node,const GameUiMatrix& world,uint32_t,GameGraphics& graphics,std::string& error){
    try{
        ++state_.callback_reaches;
        const uint32_t pcs[]={0x393A4,0x3953C,0x39684,0x3973C},nodes[]={92,93,94,78};uint32_t kind=4;
        for(uint32_t i=0;i<4;++i)if(pcs[i]==pc&&nodes[i]==node)kind=i;
        Need(kind<4&&state_.live&&state_.owner47C&&state_.nested_owner&&resource_&&font_&&font_->Snapshot().ready,"Diary callback current owner/PC/node mismatch");
        const auto& row=selection_.bank.rows.at(state_.selected_row);if(!row.flag)return true;
        state_.callback_mask|=1u<<kind;
        if(kind==3){if(state_.image_ready&&picture_){Need(graphics.DrawImage(picture_->Pixels(),picture_->Width(),picture_->Height(),int32_t(world.tx+240),int32_t(world.ty+136),0xFFFFFFFF,65535),"Diary actual textured image draw failed");++state_.image_draws;}return true;}
        Need(state_.text_owner&&text_records_.size()==1024*44,"Diary callback borrowed FONT/batch retired");
        state_.text_stage=1;state_.text_used=0;state_.text_stage=2;state_.text_scale_x=state_.text_scale_y=0;state_.text_stage=3;
        state_.text_scale_x=1;state_.text_scale_y=1;state_.text_stage=4;++state_.font_resets;
        std::string text;if(kind==0){if(row.date[1]&&row.date[2]){char buffer[64];std::snprintf(buffer,sizeof(buffer),"%2d\x8c\x8e%2d\x93\xfa",row.date[1],row.date[2]);text=buffer;}}
        else if(kind==1)text=text_;else text=secondary_;
        if(!text.empty()){
            GameFontBitmap bitmap;std::array<GameFontDrawRecord,1024> records;
            // NativeAtlas width for source93CFC flag2 right alignment; paragraph
            // source94324 gaps1/2 and scale1. Original14AD0/93F98 width remains qualified.
            const auto status=font_->RasterizeCp932({text},kind==1?170u:440u,bitmap,records.data(),uint32_t(records.size()),kind==2?0xFF7F7F7F:0xFF07182B,
                kind==1?uint32_t(font_->Snapshot().glyph_height)+2:23,kind==1?1:2);
            Need(bool(status),status.detail.c_str());state_.text_used=bitmap.glyphs;state_.glyphs+=bitmap.glyphs;++state_.emissions;
            std::memcpy(text_records_.data(),records.data(),bitmap.glyphs*sizeof(GameFontDrawRecord));
            Need(graphics.DrawImage(bitmap.pixels,bitmap.width,bitmap.height,int32_t(world.tx+240-(kind==1?5:kind==0?bitmap.advance_width:0)),int32_t(world.ty+136),0xFFFFFFFF,65535),"Diary FONT atlas flush failed");
        }
        state_.text_scale_x=state_.text_scale_y=1;state_.flush_selector=-1;state_.text_stage=5;++state_.font_flushes;return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
GameFsStatus GameDiary::Draw(GameGraphics& graphics,const GameFontResource& font){
    if(!state_.live||!state_.visible||state_.state154==3||state_.state154==5)return {};
    if(!selection_.bank.rows.at(state_.selected_row).flag)return {}; // actual391E8 flag gate
    font_=&font;GameUiDrawStats stats;std::string error;
    if(state_.failed)return state_.failure;
    if(!IoLive())return Refuse({GameFsCode::Unavailable,0,"Diary draw borrowed IO owner retired"});
    if(!clone_.Draw(graphics,stats,error,this))return Refuse({GameFsCode::Unavailable,0,error});
    // Native joined UI schedule:96870 draws visible roots; source phase11
    // 391E8 then calls96848(instance,1,-1), including hidden manual root1.
    GameUiDrawStats manual;
    if(!clone_.Draw(graphics,manual,error,this,{},0,UINT32_MAX,1))return Refuse({GameFsCode::Unavailable,0,error});
    ++state_.draw_frames;state_.quads=stats.quads+manual.quads;return {};
}
bool GameDiary::IoLive()const{if(!io_)return false;const auto current=io_->DiaryIo();return current.live&&current.owner==state_.io.owner&&current.generation==state_.io.generation;}
GameFsStatus GameDiary::Refuse(GameFsStatus status){if(!status&&!state_.failed){state_.failed=true;state_.failure=status;state_.error=status.detail.empty()?"Diary service refused without detail":status.detail;}return status;}
GameDiarySnapshot GameDiary::Snapshot()const{auto out=state_;if(auto* c=clone_.Control(0)){out.frame=c->frame;out.clip=c->clip;out.mode=c->mode;}return out;}
void GameDiary::Close(){io_=nullptr;picture_.reset();state_.nested_owner=false;std::vector<uint8_t>().swap(text_records_);font_=nullptr;
    state_.text_owner=false;state_.text_capacity=state_.text_used=0;clone_.Close();resource_.reset();input_.BeginContext();
    state_.live=state_.visible=state_.request12=state_.owner47C=state_.retirement_pending=false;state_.phase=7;}
}
