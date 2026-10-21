#include "game_record_native.h"
#include "game_profile_native.h"
#include <algorithm>
#include <stdexcept>

namespace dokodemo::recovered {
void GameRecordStore::ResetCold249D0(){
    const auto generation=state_.generation+1;state_={};state_.generation=generation;state_.live=true;
    banks_={};names_={};markers_.fill(-1);
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
} // namespace dokodemo::recovered
