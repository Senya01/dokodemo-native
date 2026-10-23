#pragma once
#include "game_filesystem_native.h"
#include <array>
#include "game_sound_voice_native.h"

namespace dokodemo::recovered {
class GameSoundBank;
struct GameSourceBankRegistration {
    bool live=false,attached=false,coherent=false;
    uint64_t registry=0,generation=0,serial=0;
    int32_t id=-1;
    uint32_t active=0,mode=0,state=0,writer=0;
    uint32_t word0C=0,word30=0,initial_zero24_mask=0;
    std::array<int32_t,4> ids{{-1,-1,-1,-1}};
    std::array<uint32_t,4> chunks{},sizes{};
    std::array<uint8_t,32> digest{};
    PackageViewDescription image,tail;
};
bool GameSourceBankRegistrationExact(const GameSourceBankRegistration&,const GameSourceBankRegistration&)noexcept;
// NativeIndexedBankPool: owned Windows IDs, not PSP pool addresses. Only
// GameSoundBank can allocate/publish entries or issue a source +5C store.
class GameSourceBankRegistry final {
  public:
    GameSourceBankRegistry()=default;
    GameSourceBankRegistry(const GameSourceBankRegistry&)=delete;
    GameSourceBankRegistry& operator=(const GameSourceBankRegistry&)=delete;
    void Close()noexcept;
  private:
    friend class GameSoundBank;
    friend class GameSourceSoundControl;
    GameFsStatus Allocate(PackageView,int32_t&);
    bool Readback(int32_t,GameSourceBankRegistration&)const;
    bool Retain(int32_t,uint32_t&,uint32_t&);
    bool Release(int32_t,uint32_t&,uint32_t&);
    bool BorrowWaveSource(int32_t,PackageView&,uintptr_t&)const;
    bool ReadModeSlot(int32_t,uintptr_t,uint32_t&)const;
    std::array<uint32_t,64> references_{};
    std::array<GameSourceBankRegistration,64> entries_{};
    std::array<PackageView,64> images_{},tails_{};
    uint64_t owner_=0,generation_=0,serial_=0;
};
// Typed fields actually initialized by17F0. PSP padding and the native audio
// queue/metadata slot are separate domains; neither is a source playback ID.
struct GameSourceSoundRecord {
    uint32_t name=0,context54=0,context58=0; // independent context+0/+54/+58
    uint32_t resource=0,resource_state=0,vtable=0; // record+4/+8/+0
    int32_t bank_handle=-1,bank_index=-1,handle=-1;
    uint32_t gain=128,state18=0,state1C=0,state20=0,step=4,position=0,request=0,flags=0;
    float rate=1.0f;
    bool bank_attached=false,bank_handle_known=true,name_known=true;
    uint64_t context_owner=0,context_generation=0; // typed record+4 reference
};
struct GameSourceSoundSnapshot {
    bool live=false,cold_initialized=false,bank_live=false,failed=false;
    uint64_t owner=0,generation=0,cold_receipt=0,id_writes=0,bank_bindings=0,queries=0,bgm_attempts=0;
    uint32_t constructor=0,last_id_writer=0,se_clean_mask=0,bgm_clean_mask=0,automatic_phase=0;
    int32_t current_id=0,previous_id=0,pending_id=0,last_query_gain=0;
    uint8_t automatic=0;
    PackageViewDescription bank;
    GameSourceBankRegistration registration;
    uint64_t feedback_attempts=0;
    uint32_t feedback_writes=0,feedback_frontier=0;
    int32_t feedback_gain=0;
    bool feedback_failed=false;
    GameSourceSoundRecord se5;
    GameSourceVoiceSnapshot voice;
};
bool GameSourceSoundOwnerStable(const GameSourceSoundSnapshot&,const GameSourceSoundSnapshot&)noexcept;
bool GameSourceSoundSnapshotExact(const GameSourceSoundSnapshot&,const GameSourceSoundSnapshot&)noexcept;
bool GameSourceSoundNegativeReady(const GameSourceSoundSnapshot&)noexcept;
struct GameSourceFeedbackRead {
    GameSourceSoundSnapshot owner;
    uint32_t context_index=5,read_pc=0x4AC84;
    bool attempted=false,bank_attached=false,bank_handle_known=false,read_returned=false;
    GameSourceSoundSnapshot after;
    uint32_t frontier=0,selector=0,params_written=0,virtual_target=0,wrapper=0,issuer=0;
    int32_t bank_handle=-1,gain=0,group=0,event=0,extra=0,mode=0;
    float rate=0;
    uint64_t context_owner=0,context_generation=0;
    uint32_t outcome=0; //0 historic56598 cut,1 actual returned voice/feedback,2 historic next-service57CF0 cut,3 returned sequence/context and selected5A5AC cut,4 actual port/track/callback return then59DD4 update cut
    bool getter_returned=false,start_returned=false,handle_stored=false,feedback_returned=false;
};
class GameSystemOptionFrontier;
//Private synchronous C++ capability; it is neither a VM/input token nor a
//public receipt. Only the guarded actor can bind it to an actual GUI.
class GameOptionsChildCallScope final {
 GameOptionsChildCallScope()=default;
 ~GameOptionsChildCallScope(){active_=false;}
 GameOptionsChildCallScope(const GameOptionsChildCallScope&)=delete;
 GameOptionsChildCallScope& operator=(const GameOptionsChildCallScope&)=delete;
 GameOptionsChildCallScope(GameOptionsChildCallScope&&)=delete;
 GameOptionsChildCallScope& operator=(GameOptionsChildCallScope&&)=delete;
 friend class GameActorScript;
 friend class GameContentBoot;
 friend class GameSystemOptionFrontier;
 friend class GameSourceSoundControl;
 const GameSystemOptionFrontier* gui_=nullptr;
 GameSourceSoundControl* source_=nullptr;
 GameSourceSoundSnapshot expected_;
 GameSourceFeedbackRead issued_;
 uint64_t serial_=0,gui_generation_=0,input_epoch_=0;
 bool slot_=false;
 bool active_=false,actor_prepared_=false,component_=false,sealed_=false,published_=false,committed_=false;
};
bool GameSourceFeedbackReachedValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourceVoiceFeedbackValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourceVoiceServiceValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourceSequencePrefixValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourcePortPrefixValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourceProcessorPrefixValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourceControlPrefixValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourceWavePrefixValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourcePort24PrefixValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourceChildPrefixValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourceSlotPrefixValid(const GameSourceFeedbackRead&)noexcept;
bool GameSourceFeedbackExact(const GameSourceFeedbackRead&,const GameSourceFeedbackRead&)noexcept;
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
    GameFsStatus Feedback4AC38(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&);
    GameFsStatus Feedback4AC38(const GameSourceSoundSnapshot&,uint32_t root14,GameSourceFeedbackRead&);
    GameFsStatus VoiceFeedback4AC38(const GameSourceSoundSnapshot&,uint32_t,GameSourceFeedbackRead&);
    GameFsStatus ServiceVoice(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&);
    GameFsStatus ServiceSequence(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&);
    GameFsStatus ServicePort(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&);
    GameFsStatus ServiceProcessor(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&);
    GameFsStatus ServiceControl(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&);
    GameFsStatus ServiceWave(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&);
    GameFsStatus ServicePort24(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&);
    GameFsStatus ServiceChild(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&);
    GameFsStatus ServiceSlot(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&);
    void ObserveWorkerTick()noexcept;
    GameSourceSoundSnapshot Snapshot()const;
    const GameSourceSoundRecord& Record(uint32_t index)const{return records_.at(index);}
    void Close()noexcept;
  private:
    friend class GameSoundBank;
    GameFsStatus StoreRegisteredSe5(GameSourceBankRegistry&,int32_t);
    GameFsStatus Start4AF54(GameSourceFeedbackRead&);
    GameFsStatus Start56930(GameSourceFeedbackRead&,int32_t,int32_t,int32_t,int32_t);
    GameFsStatus Issue56598(GameSourceFeedbackRead&,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t);
    GameFsStatus AdmitSequence58C04(bool advance_port=false);
    GameFsStatus DispatchControl5B9E4(bool advance_wave=false,bool advance_port24=false,bool advance_child=false);
    GameFsStatus LookupWave557B8(int32_t,uint32_t,uintptr_t&);
    GameFsStatus ReadProducedPort24(bool advance_child=false);
    GameFsStatus BeginOwnedChild5ABF0(const GameSourceSequencePool::ChildPacket&);
    GameFsStatus ServiceChildScoped(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&,GameOptionsChildCallScope&);
    GameFsStatus ServiceSlotScoped(const GameSourceSoundSnapshot&,GameSourceFeedbackRead&,GameOptionsChildCallScope&);
    GameOptionsChildCallScope* active_child_scope_=nullptr;
    uint64_t child_component_serial_=0;
    GameSourceVoicePool voices_;
    bool voice_admission_=false;
    uint32_t owned_bank_refs_=0;
    uint64_t worker_tick_serial_=0,worker_previous_ns_=0,worker_current_ns_=0;
    GameSourceBankRegistry* registry_=nullptr;
    GameSourceFeedbackRead feedback_;
    GameFsStatus feedback_failure_;
    GameSourceSoundSnapshot state_;
    std::array<GameSourceSoundRecord,31> records_{};
    PackageView bank_;
    GameFsStatus failure_;
};
}
