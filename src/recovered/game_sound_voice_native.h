#pragma once
#include "game_filesystem_native.h"
#include <array>
#include <functional>
namespace dokodemo::recovered {
class GameSourceSoundControl;
struct GameSourceSequenceSnapshot {
 bool live=false,active=false,allocation_returned=false,lookup_returned=false,clear_returned=false;
 bool command_entered=false,bank_retained=false,record_lookup_returned=false,track_reserved=false;
 bool track_source_initialized=false,track_activated=false,port_attempted=false,port_result_known=false,handler_returned=false;
 uint64_t owner=0,generation=0,allocation=0,voice_owner=0,voice_generation=0;
 uintptr_t ids_storage=0,output_storage=0;
 int32_t id=-1,bank=-1,sentinel_id=0,track_id=-1,port_id=-1;
 uint32_t flags=0,sentinel_flags=0,allocation_pc=0,return_pc=0,result_store_pc=0,lookup_pc=0,clear_pc=0;
 uint32_t command=0,subindex=0,group=0,event=0,command_pc=0,retain_writer=0,refs_before=0,refs_after=0;
 uint32_t record_offset=0,record_kind=0,record_divisor=0,record_bytes=0,track_pool_stride=0,port_call_pc=0,frontier=0;
 float initial_factor=0,selected_factor=0;
 bool sentinel_self=false,child_head_same=false,output_unchanged=false;
 std::array<int32_t,4> bank_ids{{-1,-1,-1,-1}};
 std::array<uint32_t,8> output_before{},output_after{};
};
bool GameSourceSequenceExact(const GameSourceSequenceSnapshot&,const GameSourceSequenceSnapshot&)noexcept;
// NativeIndexedSequenceContextPool16. IDs and pointers are actual Windows
// ownership; PSP pool capacity/initialization and PSP addresses remain unknown.
class GameSourceSequencePool final {
 public:
 GameSourceSequencePool();
 GameSourceSequencePool(const GameSourceSequencePool&)=delete;
 GameSourceSequencePool& operator=(const GameSourceSequencePool&)=delete;
 GameSourceSequenceSnapshot Snapshot()const;
 void Close()noexcept;
 private:
 friend class GameSourceVoicePool;
 friend class GameSourceSoundControl;
 struct Link { Link* next=nullptr;Link* previous=nullptr;int16_t id=-1;uint16_t flags=0; };
 struct Node { Link link,child;Link* child_head=nullptr;const std::array<int32_t,4>* ids=nullptr;std::array<uint32_t,8>* output=nullptr;uint16_t word18=0,word1A=0; };
 struct Track { int16_t id=-1;bool free=true,reserved=false; };
 GameFsStatus Allocate(const std::array<int32_t,4>&,std::array<uint32_t,8>&,uint64_t,uint64_t);
 bool Lookup(int32_t);
 bool Clear58540();
 GameFsStatus Start580D4(uint32_t,uint16_t,uint16_t,uint32_t);
 GameFsStatus AllocatePort5A5AC();
 std::array<Node,16> nodes_{};
 std::array<Track,16> tracks_{};
 Link free_,active_;
 Node* current_=nullptr;
 GameSourceSequenceSnapshot state_;
 uint64_t generation_=0; //survives retired observation reset between owning voices
};
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
    GameSourceSequenceSnapshot sequence;
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
    GameSourceVoiceSnapshot Snapshot()const{auto out=state_;out.sequence=sequences_.Snapshot();return out;}
    void Close()noexcept;
 private:
    friend class GameSourceSoundControl;
    GameFsStatus Allocate(int32_t,const std::array<int32_t,4>&,uint32_t,uint32_t,const std::function<GameFsStatus()>&);
    bool Gain(int32_t,int32_t,int32_t);
    bool Rate(int32_t,float);
    GameFsStatus Service(bool advance_sequence=false,const std::function<GameFsStatus()>& command={});
    GameFsStatus Callback578CC(bool,const std::function<GameFsStatus()>&);
    GameFsStatus AllocateSequence57CF0(const std::array<int32_t,4>&,std::array<uint32_t,8>&);
    bool Parameter(uint32_t,int32_t);
    std::array<bool,16> allocated_{};
    GameSourceVoiceSnapshot state_;
    GameSourceSequencePool sequences_;
};
}
