#pragma once
#include "../script_vm.h"

namespace dokodemo::recovered {
enum class GameConversationOrigin : uint8_t { None=0, Returned=1, PostModal=2 };
// Source common conversation owner, independent of which dialogue body it calls.
// Values below are diagnostic copies; no VM/context/frame lease is retained.
struct GameOwnedConversationSnapshot {
    GameConversationOrigin origin=GameConversationOrigin::None;
    bool accepted=false,failed=false,phase_written=false,callback_returned=false;
    bool owner_started=false,owner_returned=false,cleanup_started=false,game3=false,completed=false,main_live=false,dispatch_ready=false;
    uint64_t receipt=0,origin_activation=0,generation=0,run_id=0,main_frame=0,suspended_frame=0;
    uint64_t post_modal_generation=0,inventory_generation=0,input_sequence=0;
    uint64_t callback_frame=0,setter_frame=0,owner_frame=0,cleanup_frame=0;
    uint64_t accepted_executed=0,callback_executed=0,owner_executed=0,game3_executed=0,completed_executed=0;
    uint32_t context_word=0,owner_return_pc=0,cleanup_return_pc=0;
    std::array<char,65> code_sha256{};
    script::ValueWitness owner_result;
};
class GameOwnedConversation final {
  public:
    bool Begin(const script::VmStateWitness& suspended,uint64_t generation,uint64_t origin,
               uint64_t receipt,uint64_t main_frame)noexcept;
    bool BeginPostModal(const script::VmStateWitness&,uint64_t generation,uint64_t post_modal_generation,
                        uint64_t inventory_generation,uint64_t input_sequence,uint64_t receipt,uint64_t main_frame)noexcept;
    void Observe(const script::FrameTransition&,const script::Vm&)noexcept;
    void Game3(const script::VmStateWitness&)noexcept;
    GameOwnedConversationSnapshot Snapshot(const script::Vm&)const noexcept;
    static bool DispatchReady(const script::Vm&,uint64_t main_frame)noexcept;
    void SupersedePostModal()noexcept{if(state_.origin==GameConversationOrigin::PostModal)state_.failed=true;}
    const GameOwnedConversationSnapshot& State()const noexcept{return state_;}
    void Close()noexcept{state_.main_live=false;state_.failed=state_.accepted;}
  private:
    bool Owner(const script::VmStateWitness&)const noexcept;
    GameOwnedConversationSnapshot state_;
    script::VmStateWitness suspended_;
    uint64_t last_receipt_=0;
    bool post_modal_claimed_=false;
};
}
