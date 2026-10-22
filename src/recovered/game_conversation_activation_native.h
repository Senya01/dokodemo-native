#pragma once
#include "game_actor_script_native.h"

namespace dokodemo::recovered {
enum class GameConversationProofKind : uint8_t { None=0, Legacy85438=1, Topic2B483=2, OwnedMain=3 };
struct GameConversationActivationSnapshot {
    bool live=false,body_bound=false,body_completed=false,farewell_bound=false;
    bool returned=false,main_consumed=false,consumed=false,revoked=false;
    GameConversationProofKind kind=GameConversationProofKind::None;
    uint64_t id=0,script_generation=0,run_id=0,body_frame=0,body_caller_frame=0;
    uint64_t owned_receipt=0,origin_activation=0;
    uint64_t body_prior_sequence=0,body_sequence=0,message_frame=0,caller_frame=0;
    uint64_t message_context=0,prior_sequence=0,return_sequence=0;
    uint32_t body_message=0,body_ack=0,body_pc=0,body_caller_entry=0,body_caller_pc=0;
    uint32_t message=0,caller_entry=0,caller_pc=0,farewell_pc=0,farewell_current_pc=0;
    uint32_t context_word=0;
    uint32_t game3_baseline=0,game3_calls=0,game3_pc=0;
    std::string code_sha256,error;
};
// Recovered-domain completion evidence, independent of Windows input/queues.
// Concrete source proof kinds share one body -> farewell -> main-consumer
// lifecycle. A new PC, empty panel or old callback is never a completion kind.
class GameConversationActivation final {
  public:
    GameConversationActivation()=default;
    GameConversationActivation(const GameConversationActivation&)=delete;
    GameConversationActivation& operator=(const GameConversationActivation&)=delete;
    GameConversationActivation(GameConversationActivation&&)=delete;
    GameConversationActivation& operator=(GameConversationActivation&&)=delete;
    void Start();
    void Observe(const GameActorScriptSnapshot&,uint64_t message_context);
    bool HistoryMatches(const GameActorScriptSnapshot&)const;
    bool Consume() noexcept;
    void Close();
    const GameConversationActivationSnapshot& State()const noexcept{return state_;}
    GameConversationActivationSnapshot Snapshot()const{return state_;}
  private:
    void Begin(GameConversationProofKind,const GameActorScriptSnapshot&);
    void Fail(std::string);
    void BindFarewell(const GameActorScriptSnapshot&,uint64_t);
    GameConversationActivationSnapshot state_;
    script::VmStateWitness body_wait_,farewell_wait_;
    script::NormalReturnWitness prior_body_,body_return_,prior_farewell_,farewell_return_;
    uint64_t last_body_frame_=0,last_body_sequence_=0,last_farewell_frame_=0;
    uint64_t owned_pending_receipt_=0,last_owned_receipt_=0;
    GameOwnedConversationSnapshot owned_completion_;
    uint32_t farewell_ack_=0;
};
}
