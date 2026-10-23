#pragma once
#include "game_filesystem_native.h"
#include <array>
#include <functional>
namespace dokodemo::recovered {
class GameSourceSoundControl;
struct GameSourceVoiceSnapshot {
    bool live=false,active=false,returned=false,auxiliary_null=false,callback_entered=false,callback_returned=false,sequence_result_known=false;
    uint64_t owner=0,generation=0,allocation=0,services=0;
    int32_t id=-1,bank=-1,sequence_slot=0;
    uint32_t flags=0,mode=0,group=0,event=0,extra=0,descriptor=0,bank_refs_before=0,bank_refs_after=0;
    uint32_t retain_writer=0,initial_left=0,initial_right=0,initial_dirty_mask=0,initial_port_writes=0;
    uint32_t initialized_mask=0,port_writes=0,return_pc=0,callback_pc=0,frontier=0,initializer_store_pc=0;
    std::array<uint32_t,2> words74{};
    std::array<uint32_t,4> words80{};
    std::array<uint32_t,8> wordsA0{};
    std::array<uint16_t,6> initial_lane_controls{};
    std::array<uint8_t,6> initial_lane_flags{};
    std::array<int32_t,4> bank_ids{{-1,-1,-1,-1}};
    std::array<uint16_t,12> working{},published{},consumer{};
    uint8_t dirty=0,persistent=0,published_dirty=0,published_persistent=0,consumer_dirty=0,consumer_persistent=0;
    uint32_t master_left=128,master_right=128; // NativeVoiceControlDefaults, source global placement qualified
};
bool GameSourceVoiceExact(const GameSourceVoiceSnapshot&,const GameSourceVoiceSnapshot&)noexcept;
// NativeIndexedVoicePool:16 owned Windows nodes. SDK pool initialization and
// PSP addresses are separate. Allocation is actual free-node ownership.
class GameSourceVoicePool final {
 public:
    GameSourceVoiceSnapshot Snapshot()const{return state_;}
    void Close()noexcept;
 private:
    friend class GameSourceSoundControl;
    GameFsStatus Allocate(int32_t,const std::array<int32_t,4>&,uint32_t,uint32_t,const std::function<GameFsStatus()>&);
    bool Gain(int32_t,int32_t,int32_t);
    bool Rate(int32_t,float);
    GameFsStatus Service();
    GameFsStatus Callback578CC();
    GameFsStatus AllocateSequence57CF0(const std::array<int32_t,4>&,std::array<uint32_t,8>&);
    bool Parameter(uint32_t,int32_t);
    std::array<bool,16> allocated_{};
    GameSourceVoiceSnapshot state_;
};
}
