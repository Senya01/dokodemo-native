#include "game_notebook_native.h"
#include "game_content_native.h"
#include "core_hash.h"
#include <stdexcept>
#include <cmath>

namespace dokodemo::recovered {
namespace {void Require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}}
GameNotebookSlotObservation GameObserveNotebookSlot(const script::Vm& vm,uint64_t generation,const script::Value& registered,uint32_t index)noexcept{
    using V=script::Value;GameNotebookSlotObservation out;out.index=index;out.script_generation=generation;
    try{
        const auto w=vm.current_witness();out.error=1;Require(generation&&w.valid&&w.context_registered,"NOTE preview probe VM owner");out.run_id=w.run_id;
        Require(registered.kind==V::Kind::Reference&&vm.stable_value_owner(registered),"NOTE preview probe registration owner");
        const auto h=vm.peek_reference(registered);out.error=2;
        Require(h.kind==V::Kind::ArrayHeader&&h.word>>28==9&&index<(h.word&0xFFF),"NOTE preview probe index/header");
        const auto at=[&](V ref,uint32_t offset){Require(uint64_t(ref.index)+offset<=UINT16_MAX,"NOTE preview probe table extent");ref.index=uint16_t(ref.index+offset);return vm.peek_reference(ref);};
        const auto ref=at(registered,index+1);out.error=3;
        Require(ref.kind==V::Kind::Reference&&vm.stable_value_owner(ref),"NOTE preview probe indexed owner");
        out.row_table=uint32_t(ref.table);out.row_index=ref.index;const auto rh=vm.peek_reference(ref);out.row_header=rh.word;out.cells=rh.word&0xFFF;
        Require(rh.kind==V::Kind::ArrayHeader&&rh.word>>28==9&&out.cells&&out.cells<=64,"NOTE preview probe row shape");
        std::vector<uint8_t> bytes;
        const auto n=[&](uint32_t x){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(x>>(8*i)));};
        const auto cell=[&](const V& v){Require(vm.stable_value_owner(v)&&(v.kind==V::Kind::String||v.cp932.empty()),"NOTE preview probe value owner/shape");
            n(uint32_t(v.kind));n(v.word);n(uint32_t(v.table));n(v.index);n(uint32_t(v.cp932.size()));bytes.insert(bytes.end(),v.cp932.begin(),v.cp932.end());};
        cell(registered);cell(h);cell(ref);cell(rh);out.error=4;
        for(uint32_t i=0;i<out.cells;++i){const auto v=at(ref,i+1);cell(v);
            if(i<out.first.size()){auto& x=out.first[i];x.kind=v.kind;x.word=v.word;x.table=uint32_t(v.table);x.index=v.index;
                x.cp932_bytes=v.cp932.size();x.cp932_sha256=core::sha256(reinterpret_cast<const uint8_t*>(v.cp932.data()),v.cp932.size());}}
        out.sha=core::sha256(bytes.data(),bytes.size());out.valid=true;out.error=0;
    }catch(...){/* Diagnostic failure must not narrow the accepted empty NOTE path. */}
    return out;
}
GameNotebookRows GameObserveNotebookRows(const script::Vm& vm,uint64_t generation,const script::Value& registered){
    using V=script::Value;GameNotebookRows out;const auto witness=vm.current_witness();
    Require(generation&&witness.valid&&witness.run_id,"NOTE needs current registered GAME11 VM run");
    Require(registered.kind==V::Kind::Reference&&vm.stable_value_owner(registered),"NOTE GAME11 stable registration unavailable");
    const auto header=vm.peek_reference(registered);
    Require(header.kind==V::Kind::ArrayHeader&&header.word>>28==9,"NOTE GAME11 requires actual Array9 registration");
    const auto count=header.word&0xFFF;Require(count<=1024,"NOTE record count exceeds finite native bound1024");
    out.script_generation=generation;out.run_id=witness.run_id;out.code_sha=witness.code_sha256;
    out.root_table=uint32_t(registered.table);out.root_index=registered.index;out.root_header=header.word;out.records=count;
    std::vector<uint8_t> bytes;
    const auto number=[&](uint32_t n){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(n>>(8*i)));};
    const auto capture=[&](const V& v){Require(vm.stable_value_owner(v),"NOTE retired/ephemeral value owner");
        Require(v.kind==V::Kind::String||v.cp932.empty(),"NOTE malformed scalar text");
        number(uint32_t(v.kind));number(v.word);number(uint32_t(v.table));number(v.index);number(uint32_t(v.cp932.size()));
        bytes.insert(bytes.end(),v.cp932.begin(),v.cp932.end());};
    const auto at=[&](V ref,uint32_t n){Require(uint64_t(ref.index)+n<=UINT16_MAX,"NOTE typed table extent");ref.index=uint16_t(ref.index+n);return vm.peek_reference(ref);};
    capture(registered);capture(header);
    for(uint32_t id=0;id<count;++id){const auto row=at(registered,id+1);capture(row);
        // Original enumeration starts at1. Preserve slot0 without inventing its purpose.
        if(!id){out.slot0=GameObserveNotebookSlot(vm,generation,registered,0);continue;}
        Require(row.kind==V::Kind::Reference&&vm.stable_value_owner(row),"NOTE indexed row has no stable owner");
        const auto rh=vm.peek_reference(row);capture(rh);const auto n=rh.word&0xFFF;
        Require(rh.kind==V::Kind::ArrayHeader&&rh.word>>28==9&&n&&n<=64,"NOTE row shape outside finite typed accessor");
        for(uint32_t field=0;field<n;++field){const auto value=at(row,field+1);capture(value);++out.cells;
            if(!field){
                if((value.word>>28)!=0)++out.populated;
                else Require(value.kind==V::Kind::Scalar&&value.word==0&&value.cp932.empty(),"NOTE empty field0 requires actual canonical Scalar NIL");
            }
        }
    }
    out.sha=core::sha256(bytes.data(),bytes.size());out.valid=true;return out;
}
GameFsStatus GameNotebook::Request(GameNotebookRows rows,GameNotebookLease lease,uint32_t prior498){
    if(Modal()||state_.live||state_.retirement_pending||!rows.valid||!lease.script_generation||!lease.run_id||!lease.context
       ||!lease.parent_generation||!lease.available_owner||!lease.available_generation||!lease.input_epoch
       ||rows.script_generation!=lease.script_generation||rows.run_id!=lease.run_id)
        return {GameFsCode::Unavailable,0,"NOTE direct resource9 owner/registration unavailable"};
    if(prior498!=0)return {GameFsCode::Unavailable,0,"NOTE source nonzero498/event21 consumer unsupported"};
    if(rows.populated)return {GameFsCode::Unavailable,0,"NOTE populated records: Unsupported text/sort consumer"};
    const auto old=state_;state_={};state_.generation=old.generation+1;state_.requests=old.requests+1;
    state_.opens=old.opens;state_.retirements=old.retirements;state_.retirement_requests=old.retirement_requests;
    state_.close_requests=old.close_requests;state_.close_completions=old.close_completions;state_.polls=old.polls;
    state_.lease=lease;state_.input_epoch=lease.input_epoch;state_.rows=rows;state_.loader498=prior498;
    state_.count248=rows.records;state_.live=state_.request9=true;state_.phase=1;input_.BeginContext();return {};
}
GameFsStatus GameNotebook::Initialize(GameResourceSource& source,GameTransition& fade){
    Require(fade.Snapshot().live,"NOTE linked NativeSharedTransition131F0 unavailable");
    auto image=std::make_shared<GameUiResource>();auto status=image->Load(source,"mtomo\\gui\\tomodachi_note\\NOTE.CB2A","mtomo\\gui\\tomodachi_note\\");
    if(!status)return status;
    GameUiInstance copy;Require(copy.Clone(image),"NOTE clone failed");
    for(uint32_t i=0;i<10;++i)Require(copy.Bind(i,i,0),"NOTE constructor root binding");
    for(uint32_t r:{0u,6u})Require(copy.ShowRoot(r,false),"NOTE constructor hidden root");
    static constexpr uint32_t clips[]={0,1,5,3,4,6,8};
    for(uint32_t i=0;i<7;++i){auto* c=copy.AddControl(clips[i]);Require(c!=nullptr,"NOTE constructor control");
        if(i!=0&&i!=5){c->frame=i==4?5:1;c->mode=2;c->active=1;}}
    const uint32_t nodes[]={92,94,95,96},callbacks[]={0x2BA60,0x2BAE4,0x2BB8C,0x2BC34};
    for(uint32_t i=0;i<4;++i)Require(copy.SetSourceCallback(nodes[i],callbacks[i]),"NOTE four callback nodes");
    // Preview is not silently blanked. Its actual callbacks retain their PCs.
    const uint32_t preview[]={0x2BED8,0x2BFDC,0x2C0DC,0x2C1DC,0x2C340,0x2C440,0x2C544,0x2C648};
    for(uint32_t i=0;i<8;++i)Require(copy.SetSourceCallback(211+i,preview[i]),"NOTE preview callback node");
    std::vector<uint8_t> records(1024*44);
    Require(copy.ShowRoot(0)&&copy.Bind(0,0,0),"NOTE real opening root");
    *copy.Control(0)={0,0,1,1};copy.Control(1)->frame=1;copy.Control(2)->frame=1;
    Require(fade.StartGameOpen(),"NOTE source DA64 open producer refused");
    resource_=std::move(image);clone_=std::move(copy);text_records_.swap(records);
    row_text_present_={};row_numbers_={{-1,-1}};
    state_.text_owner=true;state_.text_capacity=1024;
    state_.owner46C=state_.visible=true;state_.state23C=0;state_.phase=2;state_.loader469=1;++state_.opens;return {};
}
bool GameNotebook::SetInputEpoch(uint64_t epoch){
    if(!state_.live||state_.phase!=3||epoch<=state_.input_epoch)return false;
    state_.input_epoch=epoch;input_.BeginContext();return true;
}
bool GameNotebook::Input(const GameInputFrame& f){
    if(f.overflow_resync||!f.focused){input_.BeginContext();++state_.rejected;return false;}
    const auto& l=state_.lease;
    if(!state_.live||state_.phase!=3||f.sequence<=state_.last_sequence||f.main_menu_epoch!=state_.input_epoch
       ||f.message_owner_generation!=l.script_generation||f.message_context!=l.context
       ||f.returned_owner!=l.available_owner||f.returned_generation!=l.available_generation){++state_.rejected;return false;}
    state_.last_sequence=f.sequence;input_.Observe(f);return true;
}
GameFsStatus GameNotebook::Tick(GameResourceSource& source,GameTransition& fade){
    try{
        if(state_.phase==1)return Initialize(source,fade);
        if(!state_.live)return {};
        Require(fade.Snapshot().live,"NOTE shared transition retired");
        if(state_.phase==6){
            // NativeSingleWorkerDeferredPlus4: a distinct invocation after the
            // actual completed poll. Do not turn queueing into immediate free.
            text_records_.clear();font_=nullptr;state_.text_owner=false;state_.text_capacity=state_.text_used=0;
            clone_.Close();resource_.reset();
            state_.live=state_.owner46C=state_.retirement_pending=false;state_.retired_generation=state_.generation;
            ++state_.retirements;state_.phase=7;return {};
        }
        std::string error;Require(clone_.Update(error),error.c_str());
        if(state_.phase==2){if(fade.Snapshot().state==2){state_.state23C=1;state_.phase=3;input_.BeginContext();}return {};}
        if(state_.phase==4){if(fade.Snapshot().state==0){
            Require(clone_.ShowRoot(0,false)&&clone_.ShowRoot(6,false),"NOTE close hidden roots");
            for(uint32_t i:{0u,5u}){auto* c=clone_.Control(i);c->mode=c->active=0;}
            state_.state23C=3;state_.visible=false;++state_.close_completions;
            //1A7DC reads this exact current owner after a real open/close.
            state_.loader469=0;state_.request9=false;++state_.polls;
            state_.retirement_pending=true;++state_.retirement_requests;state_.phase=6;
        }return {};}
        if(state_.phase!=3)return {};
        input_.Tick();
        state_.selection250=-1; //actual empty clamp24C-1; no invented selected row
        if(input_.Repeat(Down,10,2)||input_.Repeat(Up,10,2))return {};
        const auto right=input_.Repeat(Right,10,2),left=input_.Repeat(Left,10,2);
        if(right||left){
            if(right){if(state_.category264<4)++state_.category264;else if(right==1)state_.category264=1;}
            else{if(state_.category264>1)--state_.category264;else if(left==1)state_.category264=4;}
            // The frozen registered rows are all empty, so every category's
            // actual filter result is empty. No text/sort/default data is made.
            state_.category268=state_.category264;state_.count24C=0;
            auto* category=clone_.Control(1);category->frame=state_.category264;category->mode=2;category->active=1;
            return {};
        }
        if(input_.First(Circle))return {};
        if(input_.First(Triangle)){
            ++state_.unsupported;
            state_.error="Unsupported NOTE self-details: source2C91C index0 producer/text consumer not implemented";
            return {}; // nonfatal refusal: preserve real ordinary Cancel and owner
        }
        if(input_.First(Cross)){state_.state23C=2;state_.phase=4;++state_.close_requests;fade.CloseWipe(false);input_.BeginContext();}
        return {};
    }catch(const std::exception& e){state_.error=e.what();return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameNotebook::DrawUiCallback(uint32_t pc,uint32_t node,const GameUiMatrix&,uint32_t,GameGraphics&,std::string& error){
    ++state_.callback_reaches;
    if(state_.live&&state_.owner46C&&resource_){
        bool empty=false;
        if(pc==0x2BA60&&node==92)empty=!row_text_present_[0];
        else if(pc==0x2BAE4&&node==94)empty=!row_text_present_[1];
        else if(pc==0x2BB8C&&node==95)empty=row_numbers_[0]==-1;
        else if(pc==0x2BC34&&node==96)empty=row_numbers_[1]==-1;
        if(empty){++state_.callback_empty_returns;return true;}
    }
    // Empty24C suppresses the explicit row pass, not generic traversal. Never
    // replace an unexpectedly reached text/preview producer with a success stub.
    error="NOTE unsupported source callback emission/context at "+std::to_string(pc);return false;
}
GameFsStatus GameNotebook::Draw(GameGraphics& graphics,const GameFontResource& font){
    if(!state_.live||!state_.visible||state_.state23C==3)return {};
    if(!font.Snapshot().ready||text_records_.size()!=1024*44)return {GameFsCode::Unavailable,0,"NOTE borrowed FONT/text owner retired"};
    GameUiDrawStats stats;std::string error;
    if(!clone_.Draw(graphics,stats,error,this))return {GameFsCode::Unavailable,0,error};
    GameUiMatrix view;
    if(!clone_.WorldMatrix(76,view))return {GameFsCode::Unavailable,0,"NOTE node76 current world/scissor owner unavailable"};
    // NativeUiViewspaceScissor: native UI coordinates are centered, while
    // D1DC takes framebuffer coordinates. This is not PSP matrix matching.
    const float y=view.ty+136.0f;
    if(!std::isfinite(y)||std::abs(y)>1e6f)return {GameFsCode::Unavailable,0,"NOTE source scissor bound"};
    state_.scissor_top=int32_t(y);state_.scissor_bottom=int32_t(y+160.0f);
    if(!graphics.SetScissor(0,state_.scissor_top,480,state_.scissor_bottom))return {GameFsCode::Unavailable,0,"NOTE D1DC scissor consumer refused"};
    // NativeEmptyFontBatch represents the source wrapper+4/reset/scale/flush
    // sequence. The only admitted batch contains no row text or glyph records.
    font_=&font;state_.text_setup_stage=1;state_.text_capacity=uint32_t(text_records_.size()/44);state_.text_used=0;
    state_.text_setup_stage=2; //938F8 reset typed pen/context
    state_.text_scale_x=state_.text_scale_y=0;state_.text_setup_stage=3; //93900 reset font state
    state_.text_scale_x=state_.text_scale_y=1;state_.text_setup_stage=4; //94330
    ++state_.font_resets;
    if(!state_.text_owner||!font_->Snapshot().ready||state_.text_capacity!=1024||state_.text_used){
        (void)graphics.SetScissor(0,0,480,272);return {GameFsCode::Unavailable,0,"NOTE empty FONT batch owner/extent"};
    }
    state_.text_flush_selector=-1;state_.text_setup_stage=5;++state_.font_flushes;
    if(!graphics.SetScissor(0,0,480,272))return {GameFsCode::Unavailable,0,"NOTE source final scissor restore"};
    ++state_.draw_frames;state_.quads=stats.quads;return {};
}
GameNotebookSnapshot GameNotebook::Snapshot()const{auto s=state_;if(auto* c=clone_.Control(0)){s.frame=c->frame;s.clip=c->clip;s.mode=c->mode;}return s;}
void GameNotebook::Close(){text_records_.clear();font_=nullptr;state_.text_owner=false;state_.text_capacity=state_.text_used=0;
    clone_.Close();resource_.reset();input_.BeginContext();
    state_.live=state_.visible=state_.owner46C=state_.request9=state_.retirement_pending=false;state_.phase=7;}
}
