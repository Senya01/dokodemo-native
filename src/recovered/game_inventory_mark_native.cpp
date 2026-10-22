#include "game_inventory_mark_native.h"
#include <stdexcept>
#include <algorithm>

namespace dokodemo::recovered {
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool Identity(const GameInventoryRowsObservation& a,const GameInventoryRowsObservation& b){
    return a.valid&&b.valid&&a.generation==b.generation&&a.run_id==b.run_id&&a.code_sha256==b.code_sha256
        &&a.table==b.table&&a.index==b.index&&a.header==b.header&&a.rows==b.rows&&a.cells==b.cells&&a.valid_items==b.valid_items;
}
bool Shape(const GameInventoryRowShape& a,const GameInventoryRowShape& b){
    return a.table==b.table&&a.index==b.index&&a.header==b.header&&a.flag_offset==b.flag_offset
        &&a.model_offset==b.model_offset&&a.cell_offsets==b.cell_offsets;
}
uint32_t Word(const std::vector<uint8_t>& bytes,uint32_t at){
    Need(uint64_t(at)+4<=bytes.size(),"Inventory canonical word extent");
    uint32_t word=0;for(uint32_t i=0;i<4;++i)word|=uint32_t(bytes[at+i])<<(8*i);return word;
}
void Patch(std::vector<uint8_t>& bytes,uint32_t at,uint32_t word){
    const uint32_t values[]={uint32_t(script::Value::Kind::Scalar),word,uint32_t(script::Table::Frame),0,0};
    Need(uint64_t(at)+20<=bytes.size(),"Inventory patch extent");
    for(uint32_t i=0;i<5;++i)for(uint32_t b=0;b<4;++b)bytes[at+4*i+b]=uint8_t(values[i]>>(8*b));
}
}
void GameInventoryMarkChain::Begin(GameInventoryRowsImage image){
    Need(image.observation.valid&&image.observation.rows==170&&!image.bytes.empty(),"Mark chain requires complete registered inventory image");
    expected_=std::move(image);state_={};state_.live=true;
    state_.initial_sha256=state_.verified_sha256=expected_.observation.sha256;
}
bool GameInventoryMarkChain::Matches(const GameInventoryRowsImage& image)const noexcept{
    if(!state_.live||!Identity(expected_.observation,image.observation)||expected_.bytes!=image.bytes)return false;
    for(uint32_t i=0;i<170;++i)if(!Shape(expected_.rows[i],image.rows[i]))return false;
    return true;
}
bool GameInventoryMarkChain::LocateCell(uint32_t table,uint32_t index,uint32_t& row,uint32_t& field)const{
    bool found=false;
    if(table==expected_.observation.table&&index>=expected_.observation.index&&uint64_t(index)<=uint64_t(expected_.observation.index)+170)
        throw std::runtime_error("Placement destination aliases registered table/header");
    for(uint32_t r=0;r<170;++r){const auto& shape=expected_.rows[r];
        if(table!=shape.table||index<shape.index||uint64_t(index)>uint64_t(shape.index)+(shape.header&0xFFF))continue;
        Need(index!=shape.index&&!found,"Placement destination aliases another row/header");
        row=r;field=index-shape.index-1;found=true;
    }
    return found;
}
void GameInventoryMarkChain::CheckPlacementBefore(const GameInventoryRowsImage& image,uint32_t event,uint32_t row)const{
    Need((event==13||event==14)&&row<170&&Matches(image),"Placement admission changed approved image");
    const auto& selected=expected_.rows[row];const auto at=selected.cell_offsets[6];
    Need(Word(expected_.bytes,at)==uint32_t(script::Value::Kind::Scalar),"Placement model field must be Scalar");
    const auto value=Word(expected_.bytes,at+4);
    Need(event==13?value==0:(value>>28)==2,"Placement event/model state mismatch");
    std::vector<std::pair<uint32_t,uint32_t>> extents;
    for(const auto& shape:expected_.rows){Need(shape.table>=uint32_t(script::Table::Local)&&shape.table<=uint32_t(script::Table::Ctx28)
        &&shape.cell_offsets.size()==(shape.header&0xFFF),"Placement row reference/offset map must be stable and complete");for(uint32_t f=0;f<(shape.header&0xFFF);++f){
        const auto offset=shape.cell_offsets[f];const uint64_t end=uint64_t(offset)+20+Word(expected_.bytes,offset+16);
        Need(end<=expected_.bytes.size(),"Placement canonical cell extent");extents.push_back({offset,uint32_t(end)});
    }}
    std::sort(extents.begin(),extents.end());
    for(size_t i=1;i<extents.size();++i)Need(extents[i-1].second<=extents[i].first,"Placement duplicate/overlapping canonical offsets");
    // Every potentially written stable cell must have one physical identity and
    // one complete scalar encoding. No string/ref/header can be patched as20B.
    for(uint32_t r=0;r<170;++r)for(uint32_t f:{5u,6u,7u,8u,9u}){
        const auto& shape=expected_.rows[r];uint32_t actual=0,field=0;
        Need(LocateCell(shape.table,shape.index+1+f,actual,field)&&actual==r&&field==f,"Placement alias map");
        const auto offset=shape.cell_offsets[f];
        const auto end=f+1<(shape.header&0xFFF)?shape.cell_offsets[f+1]:uint32_t(expected_.bytes.size());
        Need(uint64_t(offset)+20<=expected_.bytes.size()&&end>=offset+20,"Placement mutable cell extent");
        const auto kind=Word(expected_.bytes,offset);
        Need((kind==uint32_t(script::Value::Kind::Scalar)||kind==uint32_t(script::Value::Kind::Timestamp))
             &&Word(expected_.bytes,offset+4)>>28<=3&&Word(expected_.bytes,offset+16)==0,"Placement mutable cell scalar domain");
    }
}
void GameInventoryMarkChain::CommitPlacement(const GameInventoryRowsImage& after,uint32_t event,uint32_t row,uint64_t receipt,
                                            const std::vector<GameInventorySourceStore>& stores){
    CheckPlacementBefore(expected_,event,row);
    Need(receipt>state_.receipt&&!stores.empty()&&stores.size()<=1024,"Placement store receipt/extent");
    auto next=expected_.bytes;std::array<std::array<uint8_t,64>,170> counts{};uint64_t sequence=0;
    uint32_t flags=0;bool selected_candidate=false,selected_selector=false,selected_model=false,selected_x=false;
    for(const auto& store:stores){
        Need(store.row<170&&store.field<(expected_.rows[store.row].header&0xFFF)&&store.executed>sequence,"Placement source store ordering");sequence=store.executed;
        const auto r=store.row,f=store.field;bool allowed=false;
        if(store.pc==0x2FDA&&store.entry==0x2FBC&&r==row&&f==5&&store.word==0x10000000u){allowed=true;++flags;}
        if(store.entry==0x3405&&((store.pc==0x342F&&f==6&&store.word==0)||(store.pc==0x3437&&f==7&&store.word==0)
             ||(store.pc==0x343F&&f==9&&store.word==0x20000000u)))allowed=event==13||r==row;
        if(event==13){
            if(store.entry==0x3569&&r==row&&f==7&&store.pc==0x3767&&store.word>>28==2&&(store.word&0xFFFFFFF)<20){allowed=true;selected_candidate=true;}
            if(store.entry==0x3569&&r==row&&f==8&&store.pc==0x3777&&(store.word>>28==0||store.word>>28==2)){allowed=true;selected_selector=true;}
            if(store.entry==0x3569&&f==9&&store.pc==0x38C1&&store.word>>28==2){allowed=true;if(r==row)selected_x=true;}
            if(store.entry==0x33D1&&f==6&&store.pc==0x33F7&&store.word>>28==2){allowed=true;if(r==row)selected_model=true;}
        }
        Need(allowed&&flags<=1&&counts[r][f]++==0,"Unexpected/duplicate protected placement store");
        Patch(next,expected_.rows[r].cell_offsets[f],store.word);
    }
    Need(flags==1&&stores.back().pc==0x2FDA,"Placement flag store must follow all model/row effects");
    if(event==13)Need(selected_candidate&&selected_selector&&selected_model&&selected_x,"Decoration selected source stores missing");
    else Need(counts[row][6]==1&&counts[row][7]==1&&counts[row][9]==1,"Putaway selected clears missing");
    Need(Identity(expected_.observation,after.observation)&&after.bytes==next,"Placement changed unapproved canonical bytes");
    for(uint32_t i=0;i<170;++i)Need(Shape(expected_.rows[i],after.rows[i]),"Placement replaced row shape");
    expected_.bytes=std::move(next);expected_.observation=after.observation;
    ++state_.completions;state_.event=event;state_.row=row;state_.receipt=receipt;state_.store_pc=0x2FDA;state_.verified_sha256=after.observation.sha256;
}
bool GameInventoryMarkChain::OwnsCell(uint32_t table,uint32_t index)const noexcept{
    if(!state_.live)return false;
    if(table==expected_.observation.table&&index>=expected_.observation.index&&uint64_t(index)<=uint64_t(expected_.observation.index)+170)return true;
    for(const auto& row:expected_.rows)if(table==row.table&&index>=row.index&&uint64_t(index)<=uint64_t(row.index)+(row.header&0xFFF))return true;
    return false;
}
void GameInventoryMarkChain::CheckBefore(const GameInventoryRowsImage& image,uint32_t event,uint32_t row)const{
    Need((event==14||event==15)&&row<170&&Matches(image),"Mark admission changed approved typed inventory");
    const auto& selected=expected_.rows[row];const auto target=uint64_t(selected.index)+6;
    Need(uint64_t(selected.flag_offset)+20<=expected_.bytes.size(),"Mark flag byte extent");
    Need(uint64_t(selected.model_offset)+20<=expected_.bytes.size(),"Mark model byte extent");
    const auto word=[&](uint32_t at){uint32_t n=0;for(uint32_t i=0;i<4;++i)n|=uint32_t(expected_.bytes[at+i])<<(8*i);return n;};
    Need(word(selected.model_offset)==uint32_t(script::Value::Kind::Scalar)&&word(selected.model_offset+4)==0,
         "Mark supports canonical Scalar NIL6 only");
    Need((word(selected.flag_offset)==uint32_t(script::Value::Kind::Scalar)||word(selected.flag_offset)==uint32_t(script::Value::Kind::Timestamp))
         &&word(selected.flag_offset+4)>>28<=3&&word(selected.flag_offset+16)==0,"Mark flag source scalar extent");
    Need(!(selected.table==expected_.observation.table&&target>=expected_.observation.index
        &&target<=uint64_t(expected_.observation.index)+170),"Mark flag aliases registered table/header");
    for(uint32_t i=0;i<170;++i){const auto& other=expected_.rows[i];
        if(i!=row&&selected.table==other.table&&target>=other.index&&target<=uint64_t(other.index)+(other.header&0xFFF))
            throw std::runtime_error("Mark flag aliases another protected row/header");}
}
void GameInventoryMarkChain::Commit(const GameInventoryRowsImage& after,uint32_t event,uint32_t row,uint64_t receipt,uint32_t pc,uint32_t stores){
    Need(state_.live&&(event==14||event==15)&&row<170&&receipt>state_.receipt&&pc==0x2FDA&&stores==1,"Mark transaction lacks unique source store/receipt");
    CheckBefore(expected_,event,row);auto next=expected_.bytes;const auto at=expected_.rows[row].flag_offset;
    const uint32_t words[]={uint32_t(script::Value::Kind::Scalar),event==15?0x10000001u:0x10000000u,uint32_t(script::Table::Frame),0,0};
    for(uint32_t i=0;i<5;++i)for(uint32_t b=0;b<4;++b)next[at+4*i+b]=uint8_t(words[i]>>(8*b));
    Need(Identity(expected_.observation,after.observation)&&after.bytes==next,"Mark callback changed protected row/registration/typed RAM");
    for(uint32_t i=0;i<170;++i){const auto& a=expected_.rows[i];const auto& b=after.rows[i];
        Need(Shape(a,b),"Mark row shape replaced");}
    // This is an exact, selected-cell patch comparison, never a blind rebase.
    expected_.bytes=std::move(next);expected_.observation=after.observation;
    ++state_.completions;state_.event=event;state_.row=row;state_.receipt=receipt;state_.store_pc=pc;
    state_.verified_sha256=after.observation.sha256;
}
void GameInventoryMarkChain::End()noexcept{expected_={};state_.live=false;}
}
