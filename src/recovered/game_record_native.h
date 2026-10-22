#pragma once
#include "game_filesystem_native.h"
#include <array>

namespace dokodemo::recovered {
using GameRecordDate=std::array<int32_t,3>; //year,month,day
struct GameRecordSlot {
    uint8_t flag=0;
    int32_t value=-1;
    std::array<uint8_t,200> payload{};
    GameRecordDate date{{-1,-1,-1}};
    uint32_t source_known_payload_bytes=0;
};
struct GameRecordInput {int32_t value=-1;std::string text;};
struct GameRecordStoreSnapshot {
    bool live=false,last_copied=false,last_protected=false;
    uint64_t generation=0;
    uint32_t calls=0,writes=0,protected_calls=0,capacity_errors=0;
    int32_t selected=-1;
    GameRecordDate date{{-1,-1,-1}};
    std::array<GameRecordSlot,28> bank0{};
};
// Native typed249D0/39B80 storage,independent of transient inventory480.
// Source39F40 blindly copies200B. Native safety policy zero-fills backing
// then copies min(text bytes,200);only prefix+NUL is source-known for short text.
class GameRecordStore final {
  public:
    void ResetCold249D0();
    GameFsStatus Capture71(const GameRecordDate&,const GameRecordInput* input);
    int32_t Candidate71(const GameRecordDate&)const;
    GameRecordStoreSnapshot Snapshot()const;
    const GameRecordSlot& At(uint32_t bank,uint32_t row)const{return banks_.at(bank).at(row);}
    GameRecordSlot& At(uint32_t bank,uint32_t row){return banks_.at(bank).at(row);}
    bool Live()const{return state_.live;}
    // Read-only3E35C/25020 interpretation of the native owned record banks.
    // Cold marker1..10 is not PSP-save/profile availability evidence.
    bool OtherProfilesPresent()const{if(!state_.live)return false;for(size_t i=1;i<markers_.size();++i)if(markers_[i]!=-1)return true;return false;}
    void Close(){state_.live=false;}
  private:
    std::array<std::array<GameRecordSlot,28>,11> banks_{};
    std::array<std::array<uint8_t,32>,11> names_{};
    std::array<int32_t,11> markers_{};
    GameRecordStoreSnapshot state_;
};
// Named live-calendar adapter,using the same minute-tag clock as game code.
// It does not substitute the previously confirmed profile timestamp.
GameRecordDate GameRecordDateFromClockWord(uint32_t word);
} // namespace dokodemo::recovered
