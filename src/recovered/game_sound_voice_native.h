#pragma once
#include "game_filesystem_native.h"
#include <array>
#include <functional>
#include <memory>
namespace dokodemo::recovered {
class GameSourceSoundControl;
struct GameSourcePortSnapshot {
 bool live=false,active=false,returned=false,free_unlinked=false,active_linked=false;
 uint64_t owner=0,generation=0,allocation=0,known_halfwords=0;
 uintptr_t storage=0;
 int32_t id=-1;
 uint32_t flags=0,initialize_pc=0,return_pc=0;
 std::array<int16_t,2> sentinel_ids{};
 std::array<uint16_t,2> sentinel_flags{};
 std::array<bool,2> sentinel_self{};
 std::array<uint16_t,36> parameters{};
 std::array<uint32_t,4> zero70{};
};
bool GameSourcePortExact(const GameSourcePortSnapshot&,const GameSourcePortSnapshot&)noexcept;
struct GameSourceTrackSnapshot {
 bool live=false,initialized=false,free_unlinked=false,child_linked=false,lane_initialized=false,returned=false;
 uintptr_t storage=0,context_storage=0;
 int32_t allocation_index=-1,subindex=-1,bank=-1;
 uint32_t record_offset=0,body_offset=0,end_offset=0,cursor_offset=0,word20=0,wordCC=0,wordE4=0,wordE6=0,byte28=0,byte29=0;
 uint32_t allocated_ports=0,lanes=0,retain_pc=0,refs_before=0,refs_after=0,return_pc=0;
 float factor=0;
 std::array<int16_t,16> ports{};
 std::array<uint8_t,7> lane_controls{}; //8C,AC,BC,6C,5C,7C,9C
};
bool GameSourceTrackExact(const GameSourceTrackSnapshot&,const GameSourceTrackSnapshot&)noexcept;
struct GameSourceProcessorSnapshot {
 bool entered=false,sentinel_terminated=false,controls_read=false,kind_dispatched=false,opcode_read=false,operands_returned=false,control_attempted=false;
 bool control_returned=false,kind_returned=false,context_returned=false;
 uintptr_t context_storage=0,track_storage=0,command_storage=0;
 uint32_t sentinel_pc=0,dispatch_pc=0,kind_target=0,opcode_pc=0,opcode=0,cursor_before=0,cursor_after=0,body_end=0;
 uint32_t effective_flags=0,context_word18=0,track_e4=0,track_e6=0,byte29=0,countdown=0,operand_count=0,control_pc=0,frontier=0;
 int32_t quantum=0,port_id=-1;
 std::array<int32_t,4> operands{};
 std::array<uint32_t,4> operand_begin{},operand_end{},operand_call{};
 std::array<uint8_t,9> bytes{};
 std::array<uint32_t,6> command{};
};
bool GameSourceProcessorExact(const GameSourceProcessorSnapshot&,const GameSourceProcessorSnapshot&)noexcept;
struct GameSourceControlSnapshot {
 bool entered=false,lookup_returned=false,command_dispatched=false,prepare_entered=false,prepare_returned=false,bank_retained=false;
 bool region_returned=false,region_selected=false,child_entered=false,wave_attempted=false,wave_output_known=false,wave_result_known=false;
 bool wave_returned=false,child_returned=false,handler_returned=false,control_returned=false;
 uintptr_t port_storage=0,command_storage=0,wave_output_storage=0;
 uint64_t port_owner=0,port_generation=0;
 int32_t port_id=-1,note=0,bank=-1,program=0;
 uint32_t lookup_pc=0,source_stride=0,pool_count=0,lookup_flags=0,dispatch_pc=0,handler_target=0,prepare_pc=0,prepare_return_pc=0;
 uint32_t retain_pc=0,retain_writer=0,refs_before=0,refs_after=0,rgnd_base=0,group_count=0,region_count=0,region_first=0,region_offset=0;
 uint32_t walk_count=0,stride_reads=0,selected_flags=0,selected_byteC=0,selected_byteD=0,selected_wave=0,child_call_pc=0,child_target=0,wave_call_pc=0,frontier=0;
 std::array<uint32_t,33> offsets{},strides{};
 std::array<int32_t,33> lows{},highs{};
 std::array<int32_t,7> child_args{};
};
bool GameSourceControlExact(const GameSourceControlSnapshot&,const GameSourceControlSnapshot&)noexcept;
struct GameSourceWaveSnapshot {
 bool entered=false,bank_lookup_returned=false,wave_chunk_known=false,count_read=false,mode_read=false,output_written=false,output_readback=false;
 bool result_known=false,result_null=false,returned=false,borrow_live=false,child_continued=false,port_word24_attempted=false,port_word24_known=false,port_word24_returned=false;
 uintptr_t output_storage=0,control_slot_storage=0,output_value=0,result_pointer=0,port_storage=0;
 uint64_t registry=0,registry_generation=0,registry_serial=0,image_owner=0,image_generation=0,image_serial=0,image_offset=0,image_host_address=0;
 int32_t bank=-1,wave=-1,port_id=-1,byte8=0,byte9=0,channels=0;
 uint32_t lookup_pc=0,wave_load_pc=0,count_load_pc=0,mode_load_pc=0,output_store_pc=0,return_pc=0,return_delay_pc=0;
 uint32_t wave_payload=0,wave_size=0,wave_count=0,stride=0,record_offset=0,control_mode=0,record_flags=0,record_read_mask=0;
 uint32_t word10=0,word14=0,word1C=0,stack_known=0,stack_writes=0,port_read_pc=0,frontier=0;
 std::array<int32_t,7> parameters{};
 std::array<uint32_t,7> store_pc{},store_index{};
};
bool GameSourceWaveExact(const GameSourceWaveSnapshot&,const GameSourceWaveSnapshot&)noexcept;
struct GameSourcePortInputSnapshot {
 bool entered=false,allocation_live=false,zeroed=false,bound=false,read_known=false,read_returned=false,packet_complete=false,child_entered=false,child_returned=false;
 uint64_t block_owner=0,block_generation=0,block_serial=0,sound_owner=0,sound_generation=0,port_owner=0,port_generation=0;
 uintptr_t block_base=0,node_address=0,port_storage=0,packet_storage=0,mode_slot=0;
 uint32_t bytes=0,count=0,stride=0,zero_writes=0,zero_readbacks=0,index=0,flags=0,read_pc=0,read_offset=0,value=0,stack_known=0,stack_writes=0,child_pc=0,child_delay_pc=0,frontier=0;
 int32_t port_id=-1,note=0,bank=-1,channels=0;
 std::array<uint32_t,13> words{};
 std::array<uint32_t,8> store_index{},store_pc{};
};
bool GameSourcePortInputExact(const GameSourcePortInputSnapshot&,const GameSourcePortInputSnapshot&)noexcept;
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
 GameSourcePortSnapshot port;
 GameSourceTrackSnapshot track;
 bool admission_returned=false,command_released=false,update_entered=false,processor_attempted=false;
 uint32_t current_refs=0,release_pc=0,release_before=0,release_after=0,update_pc=0,processor_pc=0;
 uint64_t native_elapsed_ns=0;
 uint64_t worker_tick_serial=0,worker_previous_ns=0,worker_current_ns=0;
 float native_delta_seconds=0;
 int32_t update_quantum=0,processor_quantum=0;
 uintptr_t processor_context=0;
 GameSourceProcessorSnapshot processor;
 GameSourceControlSnapshot control;
 GameSourceWaveSnapshot wave;
 GameSourcePortInputSnapshot port_input;
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
 struct Track { Link link;int16_t allocation_index=-1;bool free=true,reserved=false;GameSourceTrackSnapshot state; };
 struct Port { Link link,child0,child1;std::array<uint16_t,36> parameters{};std::array<uint32_t,4> zero70{};uint64_t known=0; };
 GameFsStatus Allocate(const std::array<int32_t,4>&,std::array<uint32_t,8>&,uint64_t,uint64_t);
 bool Lookup(int32_t);
 bool Clear58540();
 GameFsStatus Start580D4(uint32_t,uint16_t,uint16_t,uint32_t,bool=false,const std::function<GameFsStatus()>& retain={});
 GameFsStatus AllocatePort5A5AC();
 GameFsStatus InitializeNativePortComponent(uint64_t,uint64_t);
 GameFsStatus ReadProducedPort24();
 struct ChildPacket;
 GameFsStatus AllocateChild5ABF0(Port&,int32_t,int32_t,int32_t,const ChildPacket&);
 GameFsStatus CompleteTrack580D4(const std::function<GameFsStatus()>&);
 using ReadByte=std::function<uint8_t(uint32_t)>;
 using ControlCall=std::function<GameFsStatus()>;
 using WaveCall=std::function<GameFsStatus(int32_t,uint32_t,uintptr_t&)>;
 GameFsStatus Update5A06C(uint64_t,const ReadByte& read={},const ControlCall& control={});
 GameFsStatus ProcessContext59DD4(Node&,int32_t,const ReadByte&,const ControlCall&);
 GameFsStatus ProcessKind59168(Node&,Track&,int32_t,const ReadByte&,const ControlCall&);
 GameFsStatus Control5900C(Node&,Track&,const ControlCall&);
 GameFsStatus Submit5B9E4(Port&,const std::array<uint32_t,6>&,const ControlCall&);
 GameFsStatus Dispatch5B9E4(uint32_t,uint32_t,const ReadByte&,const ControlCall&,const WaveCall& wave={},const ControlCall& port_input={});
 Port* LookupPort5A1B4(int32_t);
 GameFsStatus Command5B2F4(Port&,uint32_t,uint32_t,const ReadByte&,const ControlCall&,const WaveCall&,const ControlCall&);
 GameFsStatus Prepare5B040(Port&,int32_t);
 GameFsStatus Child5B130(Port&,int32_t,int32_t,uint32_t,int32_t,int32_t,int32_t,const WaveCall&,const ControlCall&);
 GameFsStatus Wave557B8(int32_t,uint32_t,uintptr_t&,const WaveCall&);
 GameFsStatus ContinueChild5B180(Port&,int32_t,int32_t,const ControlCall&);
 GameFsStatus ReadPortWord24(Port&);
 //NativeOwnedZeroedPortPool16: source-layout allocation/clear and +24
 //persistence only. Native Link pointers are NEVER packed into PSP32 slots.
 std::unique_ptr<uint32_t[]> port_source_;
 uint64_t port_source_owner_=0,port_source_generation_=0,port_source_serial_=0;
 uint64_t port_sound_owner_=0,port_sound_generation_=0;
 Port* port_source_binding_=nullptr;
 uint64_t port_binding_owner_=0,port_binding_generation_=0;
 uint32_t port_binding_index_=0,port_zero_writes_=0,port_zero_readbacks_=0;
 struct ChildPacket {std::array<uint32_t,13> words{};uintptr_t mode_slot=0;uint32_t port_word24=0;};
 ChildPacket child_packet_;
 std::array<Node,16> nodes_{};
 std::array<Track,16> tracks_{};
 std::array<Port,16> ports_{};
 Link free_,active_,track_free_,port_free_,port_active_;
 Port* current_port_=nullptr;
 Track* current_track_=nullptr;
 Node* current_=nullptr;
 GameSourceSequenceSnapshot state_;
 uintptr_t wave_output_=0; //source-known only after actual55830 writer
 PackageView wave_view_; //actual immutable owning borrow, retired before bank teardown
 std::array<uint32_t,6> command_{}; //actual stable native storage for source stack command
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
