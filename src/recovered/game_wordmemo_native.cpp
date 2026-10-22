#include "game_wordmemo_native.h"
#include "game_content_native.h"
#include "core_hash.h"
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace dokodemo::recovered {
namespace {void Need(bool yes,const char* error){if(!yes)throw std::runtime_error(error);}}
bool GameWordMemoPausedOwnerExact(const script::VmStateWitness& s,const script::VmStateWitness& c)noexcept{
    const auto same=[](const script::ValueWitness& a,const script::ValueWitness& b){return a.kind==b.kind&&a.word==b.word&&a.table==b.table&&a.index==b.index&&a.cp932_bytes==b.cp932_bytes&&a.cp932_sha256==b.cp932_sha256;};
    if(!s.valid||!c.valid||!s.context_registered||!c.context_registered||s.stopped||c.stopped
       ||s.event_envelope_present||c.event_envelope_present||s.queued_callbacks||c.queued_callbacks
       ||!s.event_scope_depth||c.event_scope_depth+1!=s.event_scope_depth
       ||s.run_id!=c.run_id||s.frame_id!=c.frame_id||s.executed!=c.executed||s.current_pc!=c.current_pc||s.code_entry!=c.code_entry
       ||s.context_word!=c.context_word||s.code_sha256!=c.code_sha256||s.deadline_tick!=c.deadline_tick
       ||s.call_depth!=c.call_depth||s.active_blocks!=c.active_blocks||s.caller_entry!=c.caller_entry
       ||s.caller_call_pc!=c.caller_call_pc||s.caller_return_pc!=c.caller_return_pc||s.caller_frame_id!=c.caller_frame_id
       ||s.caller_stack_base!=c.caller_stack_base||s.frame_cells!=c.frame_cells||s.operand_cells!=c.operand_cells
       ||s.frame_captured!=s.frame_cells||c.frame_captured!=c.frame_cells||s.operands_captured!=s.operand_cells||c.operands_captured!=c.operand_cells
       ||s.frame_captured>s.frame_values.size()||c.frame_captured>c.frame_values.size()
       ||s.operands_captured>s.operands.size()||c.operands_captured>c.operands.size()||!same(s.result,c.result))return false;
    for(uint32_t i=0;i<s.frame_captured;++i)if(!same(s.frame_values[i],c.frame_values[i]))return false;
    for(uint32_t i=0;i<s.operands_captured;++i)if(!same(s.operands[i],c.operands[i]))return false;
    // The modal clock can advance; the exact suspended absolute deadline and
    // executed/caller state cannot. Restored is sampled inside callback scope.
    return true;
}
GameWordMemoRows GameObserveWordMemoRows(const script::Vm& vm,uint64_t generation,const script::Value& registered){
    using V=script::Value;GameWordMemoRows out;const auto witness=vm.current_witness();
    Need(generation&&witness.valid&&witness.run_id&&witness.context_registered,"Wordmemo current registered VM owner unavailable");
    const auto empty_owner=[](const auto& owner){using W=std::decay_t<decltype(owner)>;const W empty;return !owner.owner_before(empty)&&!empty.owner_before(owner);};
    const auto global_owner=[&](const V& ref){return ref.kind==V::Kind::Reference&&ref.table==script::Table::Global
        &&empty_owner(ref.frame_owner)&&empty_owner(ref.context_owner)&&vm.stable_value_owner(ref);};
    // NativeGlobalGAME10Only: the VM owns this one global table for this run.
    // Foreign or weak-owner references cannot alias identical numeric keys.
    Need(global_owner(registered),"Wordmemo GAME10 requires this VM's unborrowed Global table");
    const auto at=[&](V ref,uint32_t offset){Need(uint64_t(ref.index)+offset<=UINT16_MAX,"Wordmemo table extent");ref.index=uint16_t(ref.index+offset);return vm.peek_reference(ref);};
    const auto header=vm.peek_reference(registered);
    Need(header.kind==V::Kind::ArrayHeader&&header.word>>28==9&&(header.word&0xFFF)<=600,"Wordmemo actual Array9 header/capacity600");
    out.script_generation=generation;out.run_id=witness.run_id;out.code_sha=witness.code_sha256;
    out.root_table=uint32_t(registered.table);out.root_index=registered.index;out.root_header=header.word;out.records=header.word&0xFFF;
    std::vector<uint8_t> bytes;
    const auto n=[&](uint32_t x){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(x>>(8*i)));};
    const auto capture=[&](const V& v){Need(vm.stable_value_owner(v)&&(v.kind==V::Kind::String||v.cp932.empty()),"Wordmemo protected value owner/shape");
        Need(v.cp932.size()<=65536&&bytes.size()+v.cp932.size()+20<=8*1024*1024,"Wordmemo canonical image bound");
        n(uint32_t(v.kind));n(v.word);n(uint32_t(v.table));n(v.index);n(uint32_t(v.cp932.size()));bytes.insert(bytes.end(),v.cp932.begin(),v.cp932.end());};
    capture(registered);capture(header);
    for(uint32_t i=0;i<out.records;++i){
        const auto row=at(registered,i+1);capture(row);
        Need(global_owner(row),"Wordmemo indexed row requires the same unborrowed Global table");
        const auto rh=vm.peek_reference(row);capture(rh);const auto count=rh.word&0xFFF;
        Need(rh.kind==V::Kind::ArrayHeader&&rh.word>>28==9&&count&&count<=64,"Wordmemo row shape outside finite domain");
        for(uint32_t field=0;field<count;++field){const auto v=at(row,field+1);
            // No opaque nested reference is admitted as a scalar leaf. The
            // finite image has only root->record edges and owned leaf values;
            // unsupported additional backing cannot evade the close hash.
            Need(v.kind==V::Kind::Scalar||v.kind==V::Kind::Timestamp||v.kind==V::Kind::String,"Unsupported wordmemo nested/opaque backing dependency");
            capture(v);++out.cells;
            if(!field){
                Need(v.kind==V::Kind::Scalar&&v.cp932.empty()&&(v.word==0||v.word>>28==2),"Unsupported wordmemo flag conversion (requires NIL/tag2)");
                // 148D0(row,96), then148D0(row,16). With no eligible records,
                // every source category/subset and its sort are actually empty.
                if((v.word&0x60)&&!(v.word&0x10))++out.eligible_records;
            }
        }
    }
    out.sha=core::sha256(bytes.data(),bytes.size());out.valid=true;return out;
}
GameFsStatus GameWordMemo::Request(GameWordMemoRows rows,GameWordMemoLease lease){
    if(Modal()||state_.live||state_.retirement_pending||!rows.valid||!lease.script_generation||!lease.run_id||!lease.context
       ||!lease.parent_generation||!lease.available_owner||!lease.available_generation||!lease.input_epoch
       ||rows.script_generation!=lease.script_generation||rows.run_id!=lease.run_id)
        return {GameFsCode::Unavailable,0,"Wordmemo direct resource8 owner/registration unavailable"};
    if(rows.eligible_records)return {GameFsCode::Unavailable,0,"Wordmemo populated records: Unsupported row text/sort consumer"};
    const auto old=state_;state_={};state_.generation=old.generation+1;state_.requests=old.requests+1;
    state_.opens=old.opens;state_.close_requests=old.close_requests;state_.close_completions=old.close_completions;
    state_.polls=old.polls;state_.retirement_requests=old.retirement_requests;state_.retirements=old.retirements;
    state_.lease=lease;state_.input_epoch=lease.input_epoch;state_.rows=rows;
    state_.live=state_.request8=true;state_.phase=1;input_.BeginContext();return {};
}
GameFsStatus GameWordMemo::Initialize(GameResourceSource& source,GameTransition& fade){
    Need(fade.Snapshot().live,"Wordmemo linked NativeSharedTransition131F0 unavailable");
    auto image=std::make_shared<GameUiResource>();auto status=image->Load(source,"mkotoba\\gui\\kotoba_memo\\MEMO.CB2A","mkotoba\\gui\\kotoba_memo\\");if(!status)return status;
    GameUiInstance copy;Need(copy.Clone(image),"Wordmemo clone failed");
    for(uint32_t i:{0u,2u,4u,5u,6u,7u})Need(copy.Bind(i,i,0),"Wordmemo constructor root binding");
    for(uint32_t i:{1u,3u})Need(copy.Bind(i,i,1),"Wordmemo constructor static root binding");
    Need(copy.ShowRoot(0,false),"Wordmemo constructor hidden root");
    static constexpr uint32_t clips[]={0,2,6,7};
    for(uint32_t i=0;i<4;++i){auto* c=copy.AddControl(clips[i]);Need(c!=nullptr,"Wordmemo source control");if(i){c->frame=1;c->mode=2;c->active=1;}}
    Need(copy.SetSourceCallback(304,0x2EF4C)&&copy.SetSourceCallback(305,0x2F01C),"Wordmemo two source callback nodes");
    // NativeUiViewspace FONT owner. The source wrapper borrows shared10ED0;
    // its record storage remains owned through the distinct +4 drain.
    std::vector<uint8_t> records(1024*44);
    Need(copy.ShowRoot(0)&&copy.Bind(0,0,0),"Wordmemo authored Open root");*copy.Control(0)={0,0,1,1};
    Need(fade.StartGameOpen(),"Wordmemo actual DA64 Open producer refused");
    resource_=std::move(image);clone_=std::move(copy);text_records_.swap(records);
    state_.owner470=state_.visible=state_.text_owner=true;state_.text_capacity=1024;
    state_.state9B0=0;state_.loader469=1;state_.phase=2;++state_.opens;return {};
}
bool GameWordMemo::SetInputEpoch(uint64_t epoch){
    if(!state_.live||state_.phase!=3||epoch<=state_.input_epoch)return false;
    state_.input_epoch=epoch;input_.BeginContext();return true;
}
bool GameWordMemo::Input(const GameInputFrame& f){
    if(f.overflow_resync||!f.focused){input_.BeginContext();++state_.rejected;return false;}
    const auto& l=state_.lease;
    if(!state_.live||state_.phase!=3||f.sequence<=state_.last_sequence||f.main_menu_epoch!=state_.input_epoch
       ||f.message_owner_generation!=l.script_generation||f.message_context!=l.context
       ||f.returned_owner!=l.available_owner||f.returned_generation!=l.available_generation){++state_.rejected;return false;}
    state_.last_sequence=f.sequence;input_.Observe(f);return true;
}
GameFsStatus GameWordMemo::Tick(GameResourceSource& source,GameTransition& fade){
    try{
        if(state_.phase==1)return Initialize(source,fade);
        if(!state_.live)return {};
        Need(fade.Snapshot().live,"Wordmemo shared transition retired");
        if(state_.phase==6){
            // NativeSingleWorkerDeferredPlus4, separate from completed poll.
            std::vector<uint8_t>().swap(text_records_);font_=nullptr;state_.text_owner=false;state_.text_capacity=state_.text_used=0;
            clone_.Close();resource_.reset();state_.live=state_.owner470=state_.retirement_pending=false;
            state_.retired_generation=state_.generation;++state_.retirements;state_.phase=7;return {};
        }
        std::string error;Need(clone_.Update(error),error.c_str());
        if(state_.phase==2){if(fade.Snapshot().state==2){state_.state9B0=1;state_.phase=3;input_.BeginContext();}return {};}
        if(state_.phase==4){if(fade.Snapshot().state==0){
            Need(clone_.ShowRoot(0,false),"Wordmemo actual closing hide root");clone_.Control(0)->mode=clone_.Control(0)->active=0;
            state_.state9B0=3;state_.visible=false;++state_.close_completions;
            //1A84C completed resource8 clears469, then unrequests8.
            state_.loader469=0;state_.request8=false;++state_.polls;
            state_.retirement_pending=true;++state_.retirement_requests;state_.phase=6;
        }return {};}
        if(state_.phase!=3)return {};
        input_.Tick();state_.selection9C6=-1; // source signed clamp count-1
        if(input_.Repeat(Down,10,2)||input_.Repeat(Up,10,2))return {};
        const auto right=input_.Repeat(Right,10,2),left=input_.Repeat(Left,10,2);
        if(right||left){
            if(right){if(state_.category9D4<13)++state_.category9D4;else if(right==1)state_.category9D4=1;}
            else{if(state_.category9D4>1)--state_.category9D4;else if(left==1)state_.category9D4=13;}
            state_.category9D8=state_.category9D4;auto* c=clone_.Control(1);c->frame=state_.category9D4;c->mode=2;c->active=1;++state_.navigation;return {};
        }
        if(input_.First(Circle))return {}; // actual empty9BC returns before submenu
        if(input_.First(Cross)){state_.state9B0=2;state_.phase=4;++state_.close_requests;fade.CloseWipe(false);input_.BeginContext();}
        return {};
    }catch(const std::exception& e){state_.error=e.what();return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameWordMemo::DrawUiCallback(uint32_t pc,uint32_t node,const GameUiMatrix&,uint32_t,GameGraphics&,std::string& error){
    ++state_.callback_reaches;
    // Actual Open48/4C=NULL; the empty row pass never runs2F038. Both source
    // wrappers return before EC68 when their corresponding pointer is NULL.
    if(state_.live&&state_.owner470&&resource_&&font_&&font_->Snapshot().ready&&state_.text_owner
       &&text_records_.size()==1024*44&&state_.rows.valid&&!state_.rows.eligible_records
       &&((pc==0x2EF4C&&node==304)||(pc==0x2F01C&&node==305))){++state_.callback_null_returns;return true;}
    error="Wordmemo unsupported source text callback/owner at "+std::to_string(pc);return false;
}
GameFsStatus GameWordMemo::Draw(GameGraphics& graphics,const GameFontResource& font){
    if(!state_.live||!state_.visible||state_.state9B0==3)return {};
    if(!font.Snapshot().ready||text_records_.size()!=1024*44)return {GameFsCode::Unavailable,0,"Wordmemo borrowed FONT/text owner retired"};
    font_=&font;GameUiDrawStats stats;std::string error;
    if(!clone_.Draw(graphics,stats,error,this))return {GameFsCode::Unavailable,0,error};
    GameUiMatrix view; if(!clone_.WorldMatrix(70,view))return {GameFsCode::Unavailable,0,"Wordmemo node70 source scissor owner"};
    const float y=view.ty+136.0f;
    if(!std::isfinite(y)||std::abs(y)>1e6f)return {GameFsCode::Unavailable,0,"Wordmemo scissor extent"};
    state_.scissor_top=int32_t(y);state_.scissor_bottom=int32_t(y+154.0f+4.0f);
    if(!graphics.SetScissor(0,state_.scissor_top,480,state_.scissor_bottom))return {GameFsCode::Unavailable,0,"Wordmemo D1DC consumer"};
    // NativeEmptyFontBatch: actual2E994 keeps setup/reset/scale1/flush(-1)
    // even with empty9BC. Common FONT bodies remain separately qualified.
    state_.text_setup_stage=1;state_.text_used=0;state_.text_setup_stage=2;
    state_.text_scale_x=state_.text_scale_y=0;state_.text_setup_stage=3;
    state_.text_scale_x=state_.text_scale_y=1;state_.text_setup_stage=4;++state_.font_resets;
    state_.text_flush_selector=-1;state_.text_setup_stage=5;++state_.font_flushes;
    if(!graphics.SetScissor(0,0,480,272))return {GameFsCode::Unavailable,0,"Wordmemo final scissor restore"};
    ++state_.draw_frames;state_.quads=stats.quads;return {};
}
GameWordMemoSnapshot GameWordMemo::Snapshot()const{auto s=state_;if(auto* c=clone_.Control(0)){s.frame=c->frame;s.clip=c->clip;s.mode=c->mode;}return s;}
void GameWordMemo::Close(){std::vector<uint8_t>().swap(text_records_);font_=nullptr;state_.text_owner=false;state_.text_capacity=state_.text_used=0;
    clone_.Close();resource_.reset();input_.BeginContext();state_.live=state_.visible=state_.owner470=state_.request8=state_.retirement_pending=false;state_.phase=7;}
}
