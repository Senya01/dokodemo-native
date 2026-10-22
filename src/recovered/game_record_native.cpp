#include "game_record_native.h"
#include "game_profile_native.h"
#include "core_hash.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace dokodemo::recovered {
void GameRecordStore::ResetCold249D0(){
    const auto generation=state_.generation+1;state_={};state_.generation=generation;state_.live=true;
    banks_={};names_={};markers_.fill(-1);lookup_.fill(-1);
}
GameRecordDate GameRecordDateFromClockWord(uint32_t word){
    if(word>>28!=3||(word&0x08000000))throw std::runtime_error("Record calendar needs positive current minute-tag clock");
    auto days=int32_t((word&0x0FFFFFFF)/1440);int32_t year=2000,month=1;
    while(days>=365+(GameProfile::Days(year,2)==29)){days-=365+(GameProfile::Days(year,2)==29);++year;}
    while(days>=GameProfile::Days(year,month)){days-=GameProfile::Days(year,month);++month;}
    return {{year,month,days+1}};
}
GameFsStatus GameRecordStore::Capture71(const GameRecordDate& date,const GameRecordInput* input){
    if(!state_.live)return {GameFsCode::Unavailable,0,"Record bank owner retired or not reset"};
    if(date[0]<2000||date[0]>9999||date[1]<1||date[1]>12||date[2]<1||date[2]>GameProfile::Days(date[0],date[1]))
        return {GameFsCode::Bounds,0,"Record live civil calendar outside native domain"};
    ++state_.calls;state_.last_copied=state_.last_protected=false;state_.selected=-1;state_.date=date;
    auto& bank=banks_[0];const auto row=Candidate71(date);
    if(row<0){
        ++state_.capacity_errors;
        return {GameFsCode::Bounds,0,"NativeRecordCapacityGuard:source negative-index scan would select row28"};
    }
    state_.selected=row;auto& record=bank[size_t(row)];
    if(record.flag==1){state_.last_protected=true;++state_.protected_calls;return {};}
    record.flag=0;record.value=-1;record.date=date;
    // Source leaves the200B body alone when required pointers/text are absent.
    if(input){record.value=input->value;record.payload.fill(0);
        const auto n=(std::min)(record.payload.size(),input->text.size());
        std::copy_n(input->text.begin(),n,record.payload.begin());
        record.source_known_payload_bytes=uint32_t((std::min)(size_t(200),input->text.size()+1));
        state_.last_copied=true;}
    ++state_.writes;return {};
}
int32_t GameRecordStore::Candidate71(const GameRecordDate& date)const{
    const auto& bank=banks_[0];
    for(size_t i=0;i<bank.size();++i)if(bank[i].value==-1||bank[i].date==date)return int32_t(i);
    return -1;
}
GameRecordStoreSnapshot GameRecordStore::Snapshot()const{auto out=state_;out.bank0=banks_[0];return out;}
std::array<uint8_t,32> GameHashRecordImage(const GameRecordImage& image){
    std::vector<uint8_t> bytes;
    const auto word=[&](uint32_t value){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(8*i)));};
    word(image.live);word(uint32_t(image.generation));word(uint32_t(image.generation>>32));
    for(const auto& bank:image.banks){
        for(const auto& row:bank.rows){bytes.push_back(row.flag);bytes.insert(bytes.end(),row.reserved.begin(),row.reserved.end());
            word(uint32_t(row.value));bytes.insert(bytes.end(),row.payload.begin(),row.payload.end());
            for(auto d:row.date)word(uint32_t(d));word(row.source_known_payload_bytes);}
        bytes.insert(bytes.end(),bank.names.begin(),bank.names.end());word(uint32_t(bank.marker));
    }
    for(auto id:image.lookup)word(uint32_t(id));
    return core::sha256(bytes.data(),bytes.size());
}
GameRecordImage GameRecordStore::Image()const{
    GameRecordImage image;image.live=state_.live;image.generation=state_.generation;
    for(size_t i=0;i<11;++i)image.banks[i]={banks_[i],names_[i],markers_[i]};
    image.lookup=lookup_;
    image.sha=GameHashRecordImage(image);return image;
}
bool GameRecordStore::Matches(const GameRecordImage& before)const{
    return before.live&&before.sha==GameHashRecordImage(before)&&Image().sha==before.sha;
}
GameDiarySelection GameSelectDiary39824(GameRecordBank bank,const GameRecordDate& date,uint32_t hour,uint32_t mode){
    if(hour>23)throw std::runtime_error("Diary current hour outside native calendar domain");
    GameDiarySelection out;out.rows.fill(-1);out.lookup.fill(-1);
    uint32_t candidate=0;
    for(;candidate<28;++candidate){auto& row=bank.rows[candidate];if(row.value==-1||row.date==date)break;row.flag=1;}
    if(candidate==28)candidate=27;
    if(!(mode!=0&&hour<19)&&bank.rows[candidate].value!=-1)bank.rows[candidate].flag=1;
    out.candidate=candidate;uint32_t lookup=0;
    for(uint32_t i=0;i<28;++i){if(bank.rows[i].flag)out.lookup[lookup++]=bank.rows[i].value;
        if(i==candidate){out.selection=out.count;out.rows[out.count++]=int32_t(i);}
        else if(bank.rows[i].flag)out.rows[out.count++]=int32_t(i);}
    out.bank=std::move(bank);return out;
}
GameFsStatus GameRecordStore::OpenDiary(const GameRecordImage& before,const std::array<uint8_t,16>& nickname,
    const std::array<uint8_t,16>& player,int32_t manager58,const GameRecordDate& date,uint32_t hour,uint32_t mode,
    GameDiarySelection& selection,GameRecordImage& approved){
    try{
        if(!Matches(before))throw std::runtime_error("Diary bank owner/image changed before source Open");
        auto bank=before.banks[0];std::copy(nickname.begin(),nickname.end(),bank.names.begin());
        std::copy(player.begin(),player.end(),bank.names.begin()+16);bank.marker=manager58;
        auto next=GameSelectDiary39824(std::move(bank),date,hour,mode);
        auto promoted=before;promoted.banks[0]=next.bank;promoted.lookup=next.lookup;promoted.sha=GameHashRecordImage(promoted);
        // All allocations/copies and comparisons precede the noexcept typed publication.
        selection=next;approved=promoted;
        banks_[0]=next.bank.rows;names_[0]=next.bank.names;markers_[0]=next.bank.marker;lookup_=next.lookup;
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
} // namespace dokodemo::recovered
