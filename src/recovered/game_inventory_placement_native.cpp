#include "game_inventory_placement_native.h"
#include "game_model_resources_native.h"
#include <algorithm>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
using V=script::Value;using T=script::Table;using VM=script::Vm;
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int32_t Number(const V& v){
    Need((v.kind==V::Kind::Scalar||v.kind==V::Kind::Timestamp)&&v.word>>28<=3&&v.cp932.empty(),"Placement scalar domain");
    const auto p=v.word&0xFFFFFFF;return int32_t(p&0x8000000?p|0xF0000000:p);
}
V At(const VM& vm,const V& ref,uint32_t field){
    Need(ref.kind==V::Kind::Reference&&uint64_t(ref.index)+1+field<=UINT16_MAX,"Placement stable row extent");
    auto value=ref;value.index=uint16_t(ref.index+1+field);return vm.peek_reference(value);
}
bool SameRef(const V& a,const V& b){
    return a.kind==V::Kind::Reference&&b.kind==V::Kind::Reference&&a.table==b.table&&a.index==b.index&&a.word==b.word
      &&(a.table!=T::Frame||(!a.frame_owner.expired()&&a.frame_owner.lock()==b.frame_owner.lock()));
}
bool SameOwner(const GameRoomModelSlotSnapshot& a,const GameRoomModelSlotSnapshot& b){
    return a.active==b.active&&a.generation==b.generation&&a.resource==b.resource&&a.category==b.category
        &&a.retirement==b.retirement&&a.selector==b.selector&&a.source_x==b.source_x
        &&a.anchor_node==b.anchor_node&&a.native_root==b.native_root
        &&a.asset_identity==b.asset_identity&&a.clone_identity==b.clone_identity
        &&a.draw_frames==b.draw_frames&&a.draw_pixel_writes==b.draw_pixel_writes;
}
bool Nil(const V& v){return v.kind==V::Kind::Scalar&&v.word==0&&v.cp932.empty();}
}
GameInventoryPlacementProof::Atom GameInventoryPlacementProof::Capture(const V& v){
    Need(v.kind==V::Kind::String||v.cp932.empty(),"Malformed non-string placement value owns text");
    return {uint32_t(v.kind),v.word,uint32_t(v.table),v.index,v.kind==V::Kind::String?v.cp932:std::string{}};
}
uint64_t GameInventoryPlacementProof::Key(const V& v){
    Need(v.kind==V::Kind::Reference&&v.table!=T::Frame&&v.table!=T::InputResult,"Placement requires stable physical reference");
    return (uint64_t(v.table)<<32)|v.index;
}
V GameInventoryPlacementProof::Ref(uint32_t table,uint32_t index){
    Need(index<=UINT16_MAX,"Placement reference extent");V r;r.kind=V::Kind::Reference;r.table=T(table);r.index=uint16_t(index);return r;
}
GameInventoryPlacementProof::Cells GameInventoryPlacementProof::CaptureAuxiliary(const VM& vm,std::vector<V>* controls)const{
    Cells out;
    const auto add=[&](V ref,Role role,uint32_t row,uint32_t field){
        Need(vm.stable_value_owner(ref),"Placement dependency destination has foreign owner");
        const auto key=Key(ref);const auto value=vm.peek_reference(ref);
        Need(vm.stable_value_owner(value),"Placement stable graph contains ephemeral/foreign value owner");
        const Cell cell{Capture(value),role,row,field};
        auto i=out.find(key);
        if(i!=out.end())Need(i->second.value==cell.value&&i->second.role==Role::ReadOnly&&role==Role::ReadOnly,"Placement dependency alias");
        else out.emplace(key,cell);
    };
    const auto array=[&](V root,Role role,bool records,uint32_t limit,uint32_t exact){
        const auto h=vm.peek_reference(root);Need(h.kind==V::Kind::ArrayHeader,"Placement dependency header");
        const auto count=h.word&0xFFF;
        if(count>limit||(exact&&count!=exact)){
            std::string why="Placement dependency count table="+std::to_string(uint32_t(root.table))+" index="+std::to_string(root.index)
                +" actual="+std::to_string(count)+" limit="+std::to_string(limit)+" exact="+std::to_string(exact)+" roots(kind:word)=";
            for(const auto r:{Ref(2,0x44ED),Ref(2,0x4EAE),Ref(2,0x57A6),Ref(2,0x60AD),Ref(2,0x60B1),Ref(2,0x60B5),Ref(3,0x243F),Ref(3,0x24DF)}){
                const auto v=vm.peek_reference(r);why+=std::to_string(uint32_t(v.kind))+":"+std::to_string(v.word)+",";
            }
            throw std::runtime_error(why);
        }
        add(root,Role::ReadOnly,0,UINT32_MAX);
        for(uint32_t i=0;i<count;++i){auto cell=root;cell.index=uint16_t(root.index+1+i);
            Need(uint64_t(root.index)+1+i<=UINT16_MAX,"Placement dependency array extent");
            if(!records){add(cell,role,0,i);continue;}
            add(cell,Role::ReadOnly,i,UINT32_MAX);const auto row=vm.peek_reference(cell);(void)Key(row);
            const auto rh=vm.peek_reference(row);Need(rh.kind==V::Kind::ArrayHeader&&(rh.word&0xFFF)>=2&&(rh.word&0xFFF)<=64,"Placement dependency row header");
            if(role==Role::Control){Need((rh.word&0xFFF)>=10,"Placement control extent");if(controls)controls->push_back(row);}
            add(row,Role::ReadOnly,i,UINT32_MAX);
            for(uint32_t f=0;f<(rh.word&0xFFF);++f){auto r=row;Need(uint64_t(row.index)+1+f<=UINT16_MAX,"Placement dependency row extent");r.index=uint16_t(row.index+1+f);add(r,role,i,f);}
        }
    };
    array(Ref(2,0x44ED),Role::ReadOnly,true,208,208);
    array(Ref(2,0x4EAE),Role::ReadOnly,true,20,20);
    array(Ref(2,0x57A6),Role::Control,true,208,0);
    array(Ref(2,0x60AD),Role::PredicateAD,false,3,3);
    array(Ref(2,0x60B1),Role::PredicateB1,false,3,3);
    array(Ref(2,0x60B5),Role::PredicateB5,false,3,3);
    array(Ref(3,0x243F),Role::ReadOnly,false,64,0);
    // Current original-owned table declares700 cells. Filter96 still writes
    // only actual matches; every unused cell and the header remain protected.
    array(Ref(3,0x24DF),Role::Scratch,false,700,0);
    return out;
}
bool GameInventoryPlacementProof::SameAuxiliary(const Cells& a,const Cells& b)const{
    if(a.size()!=b.size())return false;
    auto j=b.begin();for(auto i=a.begin();i!=a.end();++i,++j)
        if(i->first!=j->first||!(i->second.value==j->second.value)||i->second.role!=j->second.role||i->second.row!=j->second.row||i->second.field!=j->second.field)return false;
    return true;
}
void GameInventoryPlacementProof::BindModels(const GameActorRuntimeSnapshot& room){
    End();state_={};effects_.clear();Need(room.live&&room.owner_epoch,"Placement requires actual runtime owner epoch");baseline_models_=room.room_models;runtime_epoch_=room.owner_epoch;state_.bound=true;
}
void GameInventoryPlacementProof::CheckContext(const VM& vm,const GameActorRuntimeSnapshot& room)const{
    const auto w=vm.current_witness();Need(w.valid&&w.run_id==run_id_&&w.code_sha256==code_&&room.live&&room.owner_epoch==runtime_epoch_
        &&room.manager_scene14==0,"Placement home/runtime/run identity changed");
    const auto gate=At(vm,Ref(3,0x243F),0);Need(gate.kind==V::Kind::Scalar&&gate.word==0x20000000&&gate.cp932.empty(),"Placement actual integer-zero gate closed");
}
void GameInventoryPlacementProof::Claim(const VM& vm,const V& ref,const GameActorRuntimeSnapshot& room){
    const auto model=At(vm,ref,6);if(Nil(model))return;
    Need(model.kind==V::Kind::Scalar&&model.word>>28==2,"Placement model reference type");const auto slot=Number(model);
    Need(slot>=0&&slot<100,"Placement actual model slot");const auto& owner=room.room_models[size_t(slot)];const auto& bound=baseline_models_[size_t(slot)];
    Need(owner.active&&!owner.retirement&&owner.generation&&owner.asset_identity&&owner.clone_identity&&owner.generation==bound.generation&&owner.resource==bound.resource
         &&owner.resource==Number(At(vm,ref,0))&&owner.category==Number(At(vm,ref,3)),"Placement row owns foreign/reused model");
    const auto key=Key(ref);for(const auto& pair:claims_)if(pair.first!=key)Need(pair.second.generation!=owner.generation,"Two placement records claim one model publication");
    claims_[key]=owner;
}
void GameInventoryPlacementProof::CheckInventoryOwners(const VM& vm)const{
    const auto root=Ref(before_.observation.table,before_.observation.index);
    const auto header=vm.peek_reference(root);Need(vm.stable_value_owner(root)&&header.kind==V::Kind::ArrayHeader&&header.word==before_.observation.header,"Placement registered table identity changed");
    for(uint32_t r=0;r<before_.rows.size();++r){const auto& row=before_.rows[r];
        auto ref=At(vm,root,r);
        Need(vm.stable_value_owner(ref)&&Key(ref)==Key(Ref(row.table,row.index)),"Placement actual registered row owner changed");
        for(uint32_t i=0;i<=(row.header&0xFFF);++i){ref.index=uint16_t(row.index+i);
            Need(vm.stable_value_owner(vm.peek_reference(ref)),"Placement inventory contains ephemeral/foreign value owner");}
    }
}
void GameInventoryPlacementProof::Begin(const VM& vm,const GameInventoryRowsImage& image,uint32_t event,uint32_t row,const GameActorRuntimeSnapshot& room){
    auto next=*this;next.BeginImpl(vm,image,event,row,room);*this=std::move(next);
}
void GameInventoryPlacementProof::BeginImpl(const VM& vm,const GameInventoryRowsImage& image,uint32_t event,uint32_t row,const GameActorRuntimeSnapshot& room){
    Need(state_.bound&&!state_.pending&&(event==13||event==14)&&row<170,"Placement transaction state");
    const auto w=vm.current_witness();run_id_=w.run_id;code_=w.code_sha256;CheckContext(vm,room);
    Need(!vm.context_enabled()&&!w.event_envelope_present&&!vm.callback_depth()&&!vm.queued_callbacks(),"Placement needs disabled idle owning VM");
    suspended_=w;suspended_result_=vm.result();suspended_frame_.clear();for(uint32_t i=0;i<w.frame_cells;++i)suspended_frame_.push_back(vm.read_slot(T::Frame,uint16_t(i)));
    suspended_operands_=vm.stack();
    std::vector<V> controls;auto current=CaptureAuxiliary(vm,&controls);
    if(!approved_.empty())Need(SameAuxiliary(approved_,current),"Placement protected dependencies changed outside source receipt");
    controls_=std::move(controls);next_=std::move(current);before_=image;
    CheckInventoryOwners(vm);
    for(const auto& r:image.rows)for(uint32_t n=0;n<=(r.header&0xFFF);++n)
        Need(next_.find((uint64_t(r.table)<<32)|(r.index+n))==next_.end(),"Inventory aliases protected placement dependency");
    for(uint32_t base:{0x60ADu,0x60B1u,0x60B5u}){
        const auto field=At(vm,Ref(2,base),0),op=At(vm,Ref(2,base),1);
        Need(field.kind==V::Kind::Scalar&&field.word==(base==0x60B1?0x20000005u:0x20000007u)
             &&op.kind==V::Kind::Scalar&&op.word==0x20000000u,"Placement actual predicate shape unsupported");
    }
    claims_.clear();for(const auto& r:image.rows)Claim(vm,Ref(r.table,r.index),room);for(const auto& ref:controls_)Claim(vm,ref,room);
    const auto done=state_.completions;state_={};state_.bound=state_.pending=true;state_.event=event;state_.row=row;state_.completions=done;
    stores_.clear();effects_.clear();filter_indices_.clear();filter_pc_=0;
    control_filter_seen_=control_store_seen_=control_service_started_=control_service_seen_=false;control_service_=0;control_helper_=0;control_match_={};
    source_list_={};source_frame_=0;chosen_rows_.clear();reflow_rows_.clear();reflow_index_=0;reflow_store_seen_=false;
}
void GameInventoryPlacementProof::Receipt(uint64_t receipt,uint64_t frame){Need(state_.pending&&receipt&&frame,"Placement receipt/frame missing");state_.receipt=receipt;callback_frame_=frame;}
void GameInventoryPlacementProof::ScalarPatch(uint64_t key,const V& value){
    auto i=next_.find(key);Need(i!=next_.end(),"Placement patch missing physical cell");
    Need((i->second.value.kind==uint32_t(V::Kind::Scalar)||i->second.value.kind==uint32_t(V::Kind::Timestamp))
         &&i->second.value.word>>28<=3&&i->second.value.text.empty()&&value.kind==V::Kind::Scalar&&value.cp932.empty(),"Placement patch replaced non-scalar cell");
    i->second.value=Capture(value);++state_.auxiliary_stores;
}
void GameInventoryPlacementProof::BeforeFilter(const VM& vm,const std::vector<V>& args){
    if(!state_.pending)return;const auto w=vm.current_witness();
    Need(args.size()==3&&w.event_envelope_present&&state_.receipt,"Placement filter envelope/args");
    const bool controls=w.current_pc==0x348F||w.current_pc==0x34D1;
    Need(controls||w.current_pc==0x3515,"Unexpected placement array96 consumer");
    const auto input=controls?0x57A6u:0x4FADu,predicate=w.current_pc==0x348F?0x60ADu:w.current_pc==0x34D1?0x60B1u:0x60B5u;
    Need(args[0].kind==V::Kind::Reference&&args[2].kind==V::Kind::Reference&&args[0].table==T::Global&&args[0].index==input&&args[2].table==T::Global&&args[2].index==predicate,"Placement filter actual input/predicate");
    if(controls)Need(args[1].table==T::Ctx28&&args[1].index==0x24DF,"Placement control output owner");
    else Need(args[1].table==T::Frame&&!args[1].frame_owner.expired()&&SameRef(args[1],vm.read_slot(T::Frame,0)),"Placement local output frame alias");
    const auto h=vm.peek_reference(args[1]);Need(h.kind==V::Kind::ArrayHeader&&(h.word&0xFFF)>0,"Placement filter output capacity");
    filter_pc_=w.current_pc;filter_frame_=w.frame_id;filter_capacity_=h.word&0xFFF;filter_written_=0;filter_indices_.clear();
    const auto field=Number(At(vm,args[2],0));const auto match=At(vm,args[2],2);const auto count=vm.peek_reference(args[0]).word&0xFFF;
    for(uint32_t i=0;i<count&&filter_indices_.size()<filter_capacity_;++i){const auto ref=At(vm,args[0],i);const auto value=At(vm,ref,uint32_t(field));
        if(Capture(value)==Capture(match))filter_indices_.push_back(i);
    }
    if(controls){
        Need(!control_filter_seen_&&((state_.event==13&&w.current_pc==0x348F&&w.code_entry==0x3477&&w.caller_entry==0x3569&&w.caller_call_pc==0x364B)
            ||(state_.event==14&&w.current_pc==0x34D1&&w.code_entry==0x34B9&&w.caller_entry==0x38DA&&w.caller_call_pc==0x3914)),"Placement control filter source lineage");
        control_filter_seen_=true;control_helper_=w.frame_id;
        if(!filter_indices_.empty()){
            control_match_=controls_.at(filter_indices_.front());
            control_service_=state_.event==13?(Nil(At(vm,control_match_,6))?0u:32u):(Nil(At(vm,control_match_,6))?31u:33u);
        }
    }else{
        Need(w.code_entry==0x34FD&&w.caller_entry==0x3569&&args[1].index==5&&vm.reference_belongs_to_frame(args[1],w.caller_frame_id),"Placement row-list caller identity");
        Need(w.caller_call_pc==0x35E1||w.caller_call_pc==0x366F,"Placement row-list source call");
        if(w.caller_call_pc==0x366F){Need(!source_frame_,"Placement chosen row-list repeated");source_list_=args[1];source_frame_=w.caller_frame_id;chosen_rows_=filter_indices_;}
    }
}
void GameInventoryPlacementProof::AfterFilter(){
    if(!state_.pending)return;
    // Frame-output96 stores are validated in ObserveStore too.
    Need(filter_pc_&&filter_written_==filter_indices_.size(),"Placement array96 output incomplete");filter_pc_=0;
}
void GameInventoryPlacementProof::ObserveStore(const VM& vm,const V& ref,GameInventoryMarkChain& chain){
    Need(state_.pending&&state_.receipt,"Placement store without accepted receipt");const auto w=vm.current_witness();const auto value=vm.peek_reference(ref);
    Need(w.valid&&w.event_envelope_present&&w.run_id==run_id_&&w.code_sha256==code_&&w.context_word==suspended_.context_word
         &&vm.last_callback_receipt()==state_.receipt,"Placement store foreign run/context/receipt");
    if(filter_pc_&&w.current_pc==filter_pc_){
        Need(w.frame_id==filter_frame_&&filter_written_<filter_indices_.size()&&value.kind==V::Kind::Scalar
             &&value.word==V::integer(int32_t(filter_indices_[filter_written_])).word,"Placement96 output value/order");
        if(filter_pc_==0x3515){const auto output=vm.read_slot(T::Frame,0);Need(ref.table==T::Frame&&ref.frame_owner.lock()==output.frame_owner.lock()
             &&ref.index==uint32_t(output.index)+1+filter_written_,"Placement96 frame output identity");}
        else {Need(ref.table==T::Ctx28&&ref.index==0x24E0+filter_written_,"Placement96 stable output ordinal");ScalarPatch(Key(ref),value);}
        ++filter_written_;return;
    }
    if(ref.table==T::Frame||ref.table==T::InputResult)return;
    uint32_t row=0,field=0;
    if(chain.LocateCell(uint32_t(ref.table),ref.index,row,field)){
        Need(value.kind==V::Kind::Scalar&&value.cp932.empty(),"Placement source scalar store type");
        bool allowed=false;
        if(w.current_pc==0x2FDA&&w.code_entry==0x2FBC){
            Need(state_.returned&&row==state_.row&&field==5&&value.word==0x10000000u&&Number(vm.read_slot(T::Frame,0))==int32_t(row),"Placement callback flag target/order");allowed=true;
        }
        if(w.code_entry==0x3405&&(w.current_pc==0x342F||w.current_pc==0x3437||w.current_pc==0x343F)){
            Need((w.current_pc==0x342F&&field==6&&Nil(value))||(w.current_pc==0x3437&&field==7&&Nil(value))
                 ||(w.current_pc==0x343F&&field==9&&value.word==0x20000000u),"Placement literal clear target/value");
            Need(SameRef(vm.read_slot(T::Frame,0),Ref(before_.rows[row].table,before_.rows[row].index))||Key(vm.read_slot(T::Frame,0))==Key(Ref(before_.rows[row].table,before_.rows[row].index)),"Placement clear actual helper row");
            Need(!effects_.empty()&&effects_.back().service==32&&effects_.back().row_table==before_.rows[row].table&&effects_.back().row_index==before_.rows[row].index,"Placement clear without actual32 owner");allowed=true;
        }
        if(w.code_entry==0x3569&&state_.event==13){
            const auto actual=vm.read_slot(T::Frame,1);Need(Key(actual)==Key(Ref(before_.rows[row].table,before_.rows[row].index)),"Placement3569 actual rebound row");
            if(w.current_pc==0x3767){const auto candidate=vm.read_slot(T::Frame,26),mask=vm.read_slot(T::Frame,4);const auto k=Number(candidate);
                Need(row==state_.row&&field==7&&Capture(value)==Capture(candidate)&&k>=0&&k<20&&(uint32_t(Number(mask))&(1u<<k)),"Placement selected candidate from actual mask");
                if(k>=6&&k<=8)Need(control_filter_seen_&&(control_match_.kind!=V::Kind::Reference||(control_store_seen_&&(!control_service_||control_service_seen_))),"Decoration skipped required control removal");allowed=true;}
            if(w.current_pc==0x3777){const auto candidate=Number(vm.read_slot(T::Frame,26));const auto layout=At(vm,Ref(2,0x4EAE),uint32_t(candidate));
                Need(row==state_.row&&field==8&&Capture(value)==Capture(At(vm,layout,0)),"Placement actual layout selector");allowed=true;}
            if(w.current_pc==0x38C1){const auto row_index=Number(vm.read_slot(T::Frame,53));const auto definition=At(vm,Ref(2,0x44ED),uint32_t(Number(At(vm,actual,0))));
                Need(source_frame_==w.frame_id&&vm.reference_belongs_to_frame(source_list_,source_frame_),"Placement reflow lost actual row-list owner");
                const auto count=Number(vm.read_slot(T::Frame,22)),ordinal=Number(vm.read_slot(T::Frame,23));
                Need(count>0&&count<=16&&ordinal>=0&&uint32_t(ordinal)==reflow_index_&&!reflow_store_seen_,"Placement reflow count/sequence");
                if(reflow_rows_.empty()){
                    for(int32_t i=0;i<count;++i){const auto n=Number(At(vm,source_list_,uint32_t(i)));
                        Need(n>=0&&n<170&&std::find(reflow_rows_.begin(),reflow_rows_.end(),uint32_t(n))==reflow_rows_.end()
                             &&(uint32_t(n)==state_.row||std::find(chosen_rows_.begin(),chosen_rows_.end(),uint32_t(n))!=chosen_rows_.end()),"Placement reflow list membership/duplicate");
                        reflow_rows_.push_back(uint32_t(n));
                    }
                    Need(std::count(reflow_rows_.begin(),reflow_rows_.end(),state_.row)==1,"Placement reflow omitted selected row");
                    for(const auto n:chosen_rows_)if(std::find(reflow_rows_.begin(),reflow_rows_.end(),n)==reflow_rows_.end())
                        Need(std::any_of(effects_.begin(),effects_.end(),[&](const auto& e){return e.service==32&&e.row_table==before_.rows[n].table&&e.row_index==before_.rows[n].index&&e.helper_return_executed>e.executed;}),"Placement omitted row lacks actual eviction");
                }
                Need(reflow_rows_.size()==size_t(count)&&reflow_index_<reflow_rows_.size()&&reflow_rows_[reflow_index_]==row,"Placement reflow source list changed");
                for(uint32_t i=0;i<reflow_rows_.size();++i)Need(Number(At(vm,source_list_,i))==int32_t(reflow_rows_[i]),"Placement reflow list mutated between rows");
                reflow_store_seen_=true;
                const auto width=At(vm,definition,8);Need(row_index==int32_t(row)&&field==9&&Capture(width)==Capture(vm.read_slot(T::Frame,25)),"Placement actual width/reflow row");
                const auto expected=V::integer(int32_t((uint32_t(Number(vm.read_slot(T::Frame,34)))+uint32_t(Number(width)/2))&0xFFFFFFF));
                Need(value.word==expected.word,"Placement source operand spacing");allowed=true;}
        }
        if(w.current_pc==0x33F7&&w.code_entry==0x33D1){
            Need(field==6&&!effects_.empty(),"Placement31 result store missing");auto& e=effects_.back();
            Need(e.service==31&&!e.result_stored&&e.frame==w.frame_id&&e.row_table==before_.rows[row].table&&e.row_index==before_.rows[row].index&&value.word==e.result_word,"Placement31 result store linkage");e.result_stored=true;allowed=true;
        }
        Need(allowed&&stores_.size()<1024,"Unexpected protected inventory placement write");
        stores_.push_back({row,field,w.current_pc,w.code_entry,value.word,w.executed});state_.stores=uint32_t(stores_.size());return;
    }
    const auto key=Key(ref);const auto cell=next_.find(key);if(cell==next_.end())return;
    const auto& c=cell->second;bool allowed=false;
    if(c.field==2&&((c.role==Role::PredicateAD&&w.current_pc==0x3482&&w.code_entry==0x3477)
       ||(c.role==Role::PredicateB1&&w.current_pc==0x34C4&&w.code_entry==0x34B9)
       ||(c.role==Role::PredicateB5&&w.current_pc==0x3508&&w.code_entry==0x34FD))){
        const auto source=vm.read_slot(T::Frame,c.role==Role::PredicateB5?1:0);Need(Capture(value)==Capture(source),"Placement predicate operand source");allowed=true;
    }
    if(c.role==Role::Control){
        Need(c.row<controls_.size(),"Placement control index");const auto& control=controls_[c.row];
        if(c.field==7&&((w.current_pc==0x34B3&&w.code_entry==0x3477)||(w.current_pc==0x34F7&&w.code_entry==0x34B9))){
            Need(control_filter_seen_&&!control_store_seen_&&control_helper_==w.frame_id&&!filter_indices_.empty()
                &&c.row==filter_indices_.front()&&Key(control_match_)==Key(control),"Placement control is not first actual filter match");
            allowed=Number(vm.read_slot(T::Frame,1))==int32_t(c.row)&&(state_.event==13?Nil(value):Capture(value)==Capture(vm.read_slot(T::Frame,0)));
            if(allowed)control_store_seen_=true;
        }
        if(w.code_entry==0x3405&&Key(vm.read_slot(T::Frame,0))==Key(control)){
            allowed=(c.field==6&&w.current_pc==0x342F&&Nil(value))||(c.field==7&&w.current_pc==0x3437&&Nil(value))||(c.field==9&&w.current_pc==0x343F&&value.word==0x20000000);
            if(allowed)Need(!effects_.empty()&&effects_.back().service==32&&effects_.back().row_table==uint32_t(control.table)&&effects_.back().row_index==control.index,"Control clear without actual32");
        }
        if(c.field==6&&w.current_pc==0x33F7&&w.code_entry==0x33D1&&!effects_.empty()){
            auto& e=effects_.back();allowed=e.service==31&&!e.result_stored&&e.frame==w.frame_id&&e.row_table==uint32_t(control.table)&&e.row_index==control.index&&value.word==e.result_word;if(allowed)e.result_stored=true;
        }
    }
    Need(allowed,"Unexpected protected placement dependency write");ScalarPatch(key,value);
}
void GameInventoryPlacementProof::BeforeModel(const VM& vm,uint32_t id,const V& arg,const GameActorRuntimeSnapshot& room){
    Need(state_.pending&&state_.receipt,"Placement model service outside transaction");CheckContext(vm,room);const auto w=vm.current_witness();
    Need(w.event_envelope_present&&((id==31&&w.current_pc==0x33F3&&w.code_entry==0x33D1)
        ||(id==33&&w.current_pc==0x3401&&w.code_entry==0x33D1)||(id==32&&w.current_pc==0x3425&&w.code_entry==0x3405)),"Placement model call source");
    const auto ref=vm.read_slot(T::Frame,0);(void)Key(ref);bool known=false;
    for(const auto& r:before_.rows)if(Key(ref)==((uint64_t(r.table)<<32)|r.index))known=true;
    bool control=false;for(const auto& r:controls_)if(Key(ref)==Key(r)){known=true;control=true;}Need(known,"Placement model uses foreign row");
    if(control){
        Need(control_filter_seen_&&control_store_seen_&&!control_service_started_&&Key(ref)==Key(control_match_)&&id==control_service_,"Placement default model obligation/first control mismatch");
        Need((state_.event==13&&id==32&&w.caller_entry==0x3569&&w.caller_call_pc==0x3661)
            ||(state_.event==14&&id!=32&&w.caller_entry==0x38DA&&w.caller_call_pc==0x392A),"Placement default service source caller");
        control_service_started_=true;
    }else if(id==32&&state_.event==14){
        Need(Key(ref)==Key(Ref(before_.rows[state_.row].table,before_.rows[state_.row].index)),"Putaway attempted unrelated model removal");
        const auto candidate=Number(At(vm,ref,7));
        if(candidate>=6&&candidate<=8)Need(control_filter_seen_&&(control_match_.kind!=V::Kind::Reference||(control_store_seen_&&control_service_seen_)),"Putaway skipped required default restoration");
    }else if(id!=32){
        Need(state_.event==13&&w.caller_entry==0x3569&&w.caller_frame_id==source_frame_&&w.caller_call_pc==0x38C5
            &&reflow_store_seen_&&reflow_index_<reflow_rows_.size()&&Key(ref)==Key(Ref(before_.rows[reflow_rows_[reflow_index_]].table,before_.rows[reflow_rows_[reflow_index_]].index))
            &&id==(Nil(At(vm,ref,6))?31u:33u),"Placement model missing ordered reflow row/store");
    }else{
        Need(state_.event==13&&w.caller_entry==0x3569&&w.caller_frame_id==source_frame_&&w.caller_call_pc==0x374A
            &&vm.reference_belongs_to_frame(source_list_,source_frame_),"Placement eviction outside actual subset removal");
        auto frame=source_list_;frame.index=23;const auto ordinal=Number(vm.peek_reference(frame));
        Need(ordinal>=0&&size_t(ordinal)<chosen_rows_.size(),"Placement eviction subset ordinal");
        const auto row=Number(At(vm,source_list_,uint32_t(ordinal)));frame.index=32;const auto subset=Number(vm.peek_reference(frame));
        Need(row>=0&&row<170&&!(uint32_t(subset)&(1u<<uint32_t(ordinal)))
            &&Key(ref)==Key(Ref(before_.rows[size_t(row)].table,before_.rows[size_t(row)].index)),"Placement eviction actual subset row");
    }
    if(id!=32)Need(SameRef(ref,arg),"Placement model full argument identity");
    const auto resource=Number(At(vm,ref,0));Need(resource>=0&&size_t(resource)<GameModelResources.size(),"Placement resource domain");
    if(id!=32)Need(GameModelResources[size_t(resource)].motions==0,"Placement per-object motion consumer unsupported");
    if(id==31)Need(Nil(At(vm,ref,6)),"Placement31 requires source NIL slot");
    else{
        if(id==32)for(const auto& e:effects_)Need(e.helper_return_executed>e.executed,"Default/reflow model helper must return before later removal");
        const auto slot=Number(At(vm,ref,6));Need(slot>=0&&slot<100,"Placement existing slot domain");
        const auto i=claims_.find(Key(ref));Need(i!=claims_.end(),"Placement model publication has no source row claim");const auto& owner=room.room_models[size_t(slot)];
        Need(owner.active&&!owner.retirement&&owner.generation==i->second.generation&&owner.resource==resource&&owner.category==Number(At(vm,ref,3)),"Placement model publication reused/foreign");
        if(id==32)Need(Capture(arg)==Capture(At(vm,ref,6)),"Placement32 raw slot argument identity");
    }
}
void GameInventoryPlacementProof::AfterModel(const VM& vm,uint32_t id,const V& arg,const V& result,const GameActorRuntimeSnapshot& before,const GameActorRuntimeSnapshot& after){
    const auto w=vm.current_witness();const auto ref=vm.read_slot(T::Frame,0);GameInventoryModelEffect e;
    e.service=id;e.pc=w.current_pc;e.entry=w.code_entry;e.executed=w.executed;e.receipt=state_.receipt;e.frame=w.frame_id;e.result_word=result.word;e.row_table=uint32_t(ref.table);e.row_index=ref.index;
    e.argument_kind=uint32_t(arg.kind);e.argument_word=arg.word;e.argument_table=uint32_t(arg.table);e.argument_index=arg.index;e.runtime_epoch=after.owner_epoch;e.owner_phase=after.room_owner_phases;
    e.before_counts={before.model_slots,before.model_publications,before.model_updates,before.model_retire_requests,before.model_retirements,before.model_asset_acquisitions,before.model_clone_initializations};
    e.after_counts={after.model_slots,after.model_publications,after.model_updates,after.model_retire_requests,after.model_retirements,after.model_asset_acquisitions,after.model_clone_initializations};
    e.resource=Number(At(vm,ref,0));e.category=Number(At(vm,ref,3));e.selector=Number(At(vm,ref,8));e.x=Number(At(vm,ref,9));
    e.slot=id==31?Number(result):Number(At(vm,ref,6));
    if(e.slot>=0&&e.slot<100){e.before=before.room_models[size_t(e.slot)];e.after=after.room_models[size_t(e.slot)];e.generation=e.after.generation;}
    effects_.push_back(e);state_.services=uint32_t(effects_.size());Need(effects_.size()<=64&&after.owner_epoch==runtime_epoch_,"Placement bounded native service/epoch");
    Need(before.owner_epoch==after.owner_epoch&&before.room_owner_phases==after.room_owner_phases
        &&before.model_retirements==after.model_retirements,"Placement service crossed native owner phase/retirement");
    Need(after.model_slots==before.model_slots+(id==31?1:0)
        &&after.model_publications==before.model_publications+(id==31?1:0)
        &&after.model_asset_acquisitions==before.model_asset_acquisitions+(id==31?1:0)
        &&after.model_clone_initializations==before.model_clone_initializations+(id==31?1:0)
        &&after.model_updates==before.model_updates+(id==33?1:0)
        &&after.model_retire_requests==before.model_retire_requests+(id==32?1:0),"Placement service count/allocation effects");
    Need(e.slot>=0&&e.slot<100,"Placement31 returned integer-1; allocation success unproved");
    for(size_t i=0;i<100;++i)if(i!=size_t(e.slot))Need(SameOwner(before.room_models[i],after.room_models[i]),"Placement service changed unrelated model owner");
    if(id==31){Need(result.kind==V::Kind::Scalar&&result.word>>28==2&&!e.before.active&&e.after.active&&!e.after.retirement&&e.after.generation>e.before.generation
          &&e.after.asset_identity==after.model_asset_acquisitions&&e.after.clone_identity==after.model_clone_initializations
          &&e.after.resource==e.resource&&e.after.category==e.category&&e.after.selector==e.selector&&e.after.source_x==float(e.x),"Placement actual31 publication");claims_[Key(ref)]=e.after;}
    else{
        Need(result.kind==V::Kind::Scalar&&result.word==0x10000000u&&e.after.generation==e.before.generation&&e.after.resource==e.before.resource,"Placement32/33 FALSE/owner identity");
        Need(e.after.asset_identity&&e.after.clone_identity&&e.after.asset_identity==e.before.asset_identity&&e.after.clone_identity==e.before.clone_identity
            &&e.after.category==e.before.category&&e.after.draw_frames==e.before.draw_frames&&e.after.draw_pixel_writes==e.before.draw_pixel_writes,"Placement32/33 replaced asset/clone owner");
        if(id==33)Need(e.after.active&&!e.after.retirement&&e.after.selector==e.selector&&e.after.source_x==float(e.x),"Placement actual33 controller update");
        else{
            Need(e.after.active&&e.after.retirement==1,"Placement32 must request, not destroy");
            auto unchanged=e.after;unchanged.retirement=e.before.retirement;
            Need(SameOwner(e.before,unchanged),"Placement32 changed owner before natural retirement");
            const auto i=std::find_if(after.model_retirement_history.rbegin(),after.model_retirement_history.rend(),[&](const auto& x){return x.slot==e.slot&&x.generation==e.generation&&x.resource==e.resource;});
            Need(i!=after.model_retirement_history.rend()&&i->request&&i->requested_phase==after.room_owner_phases&&!i->disabled_phase&&!i->destroyed_phase,"Placement32 fresh retirement request");
            effects_.back().retirement_request=i->request;
            retirements_.push_back(effects_.back());Need(retirements_.size()<=16,"Placement retirement journal scope exceeded");
        }
    }
    baseline_models_[size_t(e.slot)]=e.after;
    if(control_match_.kind==V::Kind::Reference&&Key(ref)==Key(control_match_)){
        Need(control_service_started_&&!control_service_seen_&&id==control_service_,"Placement unmatched default effect");control_service_seen_=true;
    }
}
void GameInventoryPlacementProof::ObserveReturn(const script::FrameTransition& t){
    if(!state_.pending)return;const auto& b=t.before;
    Need(t.callback_receipt==state_.receipt&&b.run_id==run_id_&&b.code_sha256==code_,"Placement transition foreign receipt/run/code");
    if(t.opcode==0x31&&(b.code_entry==0x33D1||b.code_entry==0x3405)){
        Need(b.current_pc==(b.code_entry==0x33D1?0x3404u:0x3440u)||b.current_pc==0x341C,"Placement model helper return PC");
        for(auto& e:effects_)if(e.frame==b.frame_id&&e.entry==b.code_entry){e.helper_return_pc=b.current_pc;e.helper_return_executed=t.after.executed;}
        if(b.code_entry==0x33D1&&state_.event==13&&t.after.frame_id==source_frame_&&t.after.current_pc==0x38CA){
            Need(reflow_store_seen_&&reflow_index_<reflow_rows_.size()&&!effects_.empty(),"Placement unexpected reflow helper return");
            const auto& e=effects_.back();const auto& row=before_.rows[reflow_rows_[reflow_index_]];
            Need(e.frame==b.frame_id&&e.entry==0x33D1&&(e.service==31||e.service==33)&&e.row_table==row.table&&e.row_index==row.index
                &&e.helper_return_executed==t.after.executed,"Placement reflow return lacks actual successful model effect");
            ++reflow_index_;reflow_store_seen_=false;
        }
    }
    if(t.opcode==0x31&&((state_.event==13&&b.code_entry==0x3569)||(state_.event==14&&b.code_entry==0x38DA))){
        Need(b.current_pc==(state_.event==13?0x38D9u:0x393Bu)&&t.after.code_entry==0x8DA28,"Placement main helper return");
        Need(t.after.frame_id==callback_frame_&&t.after.current_pc==(state_.event==13?0x8DBA6u:0x8DBC4u),"Placement main return foreign callback continuation");state_.returned=true;state_.source_return_pc=b.current_pc;state_.source_return_executed=t.after.executed;
    }
    if(t.opcode==0x31&&b.code_entry==0x2FBC){
        Need(state_.returned&&!state_.flag_returned&&b.current_pc==0x2FED&&t.after.code_entry==0x8DA28&&t.after.frame_id==callback_frame_
            &&t.after.current_pc==(state_.event==13?0x8DBAFu:0x8DBCDu)&&!stores_.empty()&&stores_.back().pc==0x2FDA
            &&stores_.back().executed<t.after.executed,"Placement flag helper return/continuation missing");state_.flag_returned=true;
    }
    if(t.opcode==0x32){const auto& a=t.after;const auto& s=suspended_;
        Need(state_.flag_returned&&b.frame_id==callback_frame_&&b.code_entry==0x8DA28&&b.current_pc==0x8DD8B&&b.event_envelope_present&&!a.event_envelope_present
          &&a.frame_id==s.frame_id&&a.current_pc==s.current_pc&&a.code_entry==s.code_entry&&a.call_depth==s.call_depth&&a.active_blocks==s.active_blocks
          &&a.context_word==s.context_word&&a.context_registered==s.context_registered&&a.queued_callbacks==s.queued_callbacks
          &&a.caller_entry==s.caller_entry&&a.caller_call_pc==s.caller_call_pc&&a.caller_return_pc==s.caller_return_pc&&a.caller_frame_id==s.caller_frame_id
          &&a.caller_stack_base==s.caller_stack_base&&a.event_scope_depth==s.event_scope_depth+1
          &&a.deadline_tick==(s.relative_deadline23>0?a.clock_tick+uint32_t(s.relative_deadline23):0),"Placement callback32 restoration");
        state_.callback_returned=true;
    }
}
void GameInventoryPlacementProof::Commit(const VM& vm,const GameInventoryRowsImage& image,GameInventoryMarkChain& chain){
    Need(state_.pending&&state_.returned&&state_.callback_returned&&state_.receipt&&!stores_.empty()&&!vm.context_enabled()
         &&vm.callback_depth()==suspended_.event_scope_depth,"Placement source/32 disabled restoration incomplete");
    const auto same=[](const V& a,const V& b){return Capture(a)==Capture(b)&&(a.kind!=V::Kind::Reference
        ||((a.table!=T::Frame||(!a.frame_owner.expired()&&a.frame_owner.lock()==b.frame_owner.lock()))&&a.context_owner.lock()==b.context_owner.lock()));};
    const auto w=vm.current_witness();Need(w.frame_cells==suspended_frame_.size()&&vm.stack().size()==suspended_operands_.size(),"Placement restored value extents");
    for(uint32_t i=0;i<w.frame_cells;++i)Need(same(vm.read_slot(T::Frame,uint16_t(i)),suspended_frame_[i]),"Placement restored complete caller frame");
    for(size_t i=0;i<vm.stack().size();++i)Need(same(vm.stack()[i],suspended_operands_[i]),"Placement restored complete operands");
    Need(same(vm.result(),suspended_result_),"Placement restored suspended result");
    for(const auto& e:effects_){Need(e.helper_return_executed>e.executed,"Placement model helper return missing");if(e.service==31)Need(e.result_stored,"Placement31 returned slot was not stored by source");}
    if(state_.event==13)Need(!reflow_rows_.empty()&&reflow_index_==reflow_rows_.size()&&!reflow_store_seen_,"Placement incomplete required reflow trace");
    if(control_match_.kind==V::Kind::Reference)Need(control_store_seen_&&(!control_service_||control_service_seen_),"Placement required control/default trace missing");
    if(state_.event==13)Need(std::any_of(effects_.begin(),effects_.end(),[&](const auto& e){return e.service==31&&e.row_table==before_.rows[state_.row].table&&e.row_index==before_.rows[state_.row].index;}),"Decoration selected model publication missing");
    else Need(std::any_of(effects_.begin(),effects_.end(),[&](const auto& e){return e.service==32&&e.row_table==before_.rows[state_.row].table&&e.row_index==before_.rows[state_.row].index;}),"Putaway selected model request missing");
    CheckInventoryOwners(vm);const auto current=CaptureAuxiliary(vm);Need(SameAuxiliary(next_,current),"Placement changed unapproved dependency bytes/shape");
    auto promoted=next_; // all allocating auxiliary preparation precedes inventory promotion
    chain.CommitPlacement(image,state_.event,state_.row,state_.receipt,stores_);
    approved_.swap(promoted);state_.pending=false;++state_.completions;
}
bool GameInventoryPlacementProof::MatchesAuxiliary(const VM& vm)const{
    if(state_.pending)return false;if(approved_.empty())return true;
    CheckInventoryOwners(vm);return SameAuxiliary(approved_,CaptureAuxiliary(vm));
}
bool GameInventoryPlacementProof::OwnsAuxiliaryCell(const V& ref)const noexcept{
    if(ref.kind!=V::Kind::Reference||ref.table==T::Frame||ref.table==T::InputResult)return false;
    return approved_.find((uint64_t(ref.table)<<32)|ref.index)!=approved_.end();
}
bool GameInventoryPlacementProof::RetirementsComplete(const GameActorRuntimeSnapshot& room)const{
    if(room.owner_epoch!=runtime_epoch_)return false;
    for(const auto& e:retirements_){const auto i=std::find_if(room.model_retirement_history.begin(),room.model_retirement_history.end(),[&](const auto& r){return r.request==e.retirement_request&&r.slot==e.slot&&r.generation==e.generation&&r.resource==e.resource;});
        if(i==room.model_retirement_history.end()||!(i->requested_phase<i->disabled_phase&&i->disabled_phase<i->destroyed_phase)
             ||!i->assets_retained_at_disable||!i->clone_retained_at_disable||!i->asset_owner_expired||!i->clone_owner_cleared)return false;
    }
    return true;
}
void GameInventoryPlacementProof::Abort()noexcept{if(!state_.pending)return;state_.pending=false;next_.clear();stores_.clear();effects_.clear();filter_pc_=0;}
void GameInventoryPlacementProof::End()noexcept{
    state_.bound=state_.pending=false;approved_.clear();next_.clear();controls_.clear();claims_.clear();baseline_models_={};before_={};
    retirements_.clear();filter_indices_.clear();filter_pc_=0;suspended_frame_.clear();suspended_operands_.clear();suspended_result_={};
}
}
