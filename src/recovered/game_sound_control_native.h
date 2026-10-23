#pragma once
#include "game_filesystem_native.h"
#include <array>

namespace dokodemo::recovered {
// Typed fields actually initialized by17F0. PSP padding and the native audio
// queue/metadata slot are separate domains; neither is a source playback ID.
struct GameSourceSoundRecord {
    uint32_t name=0,context54=0,context58=0; // independent context+0/+54/+58
    uint32_t resource=0,resource_state=0,vtable=0; // record+4/+8/+0
    int32_t bank_handle=-1,bank_index=-1,handle=-1;
    uint32_t gain=128,state18=0,state1C=0,state20=0,step=4,position=0,request=0,flags=0;
    float rate=1.0f;
    bool bank_attached=false,bank_handle_known=true,name_known=true;
};
struct GameSourceSoundSnapshot {
    bool live=false,cold_initialized=false,bank_live=false,failed=false;
    uint64_t owner=0,generation=0,cold_receipt=0,id_writes=0,bank_bindings=0,queries=0,bgm_attempts=0;
    uint32_t constructor=0,last_id_writer=0,se_clean_mask=0,bgm_clean_mask=0,automatic_phase=0;
    int32_t current_id=0,previous_id=0,pending_id=0,last_query_gain=0;
    uint8_t automatic=0;
    PackageViewDescription bank;
};
bool GameSourceSoundOwnerStable(const GameSourceSoundSnapshot&,const GameSourceSoundSnapshot&)noexcept;
bool GameSourceSoundSnapshotExact(const GameSourceSoundSnapshot&,const GameSourceSoundSnapshot&)noexcept;
bool GameSourceSoundNegativeReady(const GameSourceSoundSnapshot&)noexcept;
struct GameSourceSoundGainReceipt {
    GameSourceSoundSnapshot before,after;
    uint32_t read_pc=0,return_pc=0;
    int32_t id=0,value=0;
    bool returned=false,negative_guard=false,dispatched=false;
};
bool GameSourceSoundGainValid(const GameSourceSoundGainReceipt&)noexcept;
bool GameSourceSoundGainExact(const GameSourceSoundGainReceipt&,const GameSourceSoundGainReceipt&)noexcept;
class GameSourceSoundControl final {
  public:
    GameSourceSoundControl()=default;
    GameSourceSoundControl(const GameSourceSoundControl&)=delete;
    GameSourceSoundControl& operator=(const GameSourceSoundControl&)=delete;
    GameFsStatus Initialize17F0();
    GameFsStatus BindSe5(PackageView);
    GameFsStatus Gain4AFBC(const GameSourceSoundSnapshot&,int32_t,GameSourceSoundGainReceipt&);
    // A BGM request/start/reset is outside this finite ported owner. Refusal
    // is terminal before mutation, never permission to keep using cold ID.
    GameFsStatus RequestBgm(int32_t);
    GameSourceSoundSnapshot Snapshot()const;
    const GameSourceSoundRecord& Record(uint32_t index)const{return records_.at(index);}
    void Close()noexcept;
  private:
    GameSourceSoundSnapshot state_;
    std::array<GameSourceSoundRecord,31> records_{};
    PackageView bank_;
    GameFsStatus failure_;
};
}
