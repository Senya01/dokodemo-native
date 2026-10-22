#include "game_inventory_mark_native.h"
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool Identity(const GameInventoryRowsObservation& a,const GameInventoryRowsObservation& b){
    return a.valid&&b.valid&&a.generation==b.generation&&a.run_id==b.run_id&&a.code_sha256==b.code_sha256
        &&a.table==b.table&&a.index==b.index&&a.header==b.header&&a.rows==b.rows&&a.cells==b.cells&&a.valid_items==b.valid_items;
}
}
void GameInventoryMarkChain::Begin(GameInventoryRowsImage image){
    Need(image.observation.valid&&image.observation.rows==170&&!image.bytes.empty(),"Mark chain requires complete registered inventory image");
    expected_=std::move(image);state_={};state_.live=true;
    state_.initial_sha256=state_.verified_sha256=expected_.observation.sha256;
}
bool GameInventoryMarkChain::Matches(const GameInventoryRowsImage& image)const noexcept{
    if(!state_.live||!Identity(expected_.observation,image.observation)||expected_.bytes!=image.bytes)return false;
    for(uint32_t i=0;i<170;++i){const auto& a=expected_.rows[i];const auto& b=image.rows[i];
        if(a.table!=b.table||a.index!=b.index||a.header!=b.header||a.flag_offset!=b.flag_offset||a.model_offset!=b.model_offset)return false;}
    return true;
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
        Need(a.table==b.table&&a.index==b.index&&a.header==b.header&&a.flag_offset==b.flag_offset&&a.model_offset==b.model_offset,"Mark row shape replaced");}
    // This is an exact, selected-cell patch comparison, never a blind rebase.
    expected_.bytes=std::move(next);expected_.observation=after.observation;
    ++state_.completions;state_.event=event;state_.row=row;state_.receipt=receipt;state_.store_pc=pc;
    state_.verified_sha256=after.observation.sha256;
}
void GameInventoryMarkChain::End()noexcept{expected_={};state_.live=false;}
}
