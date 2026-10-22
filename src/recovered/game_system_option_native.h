#pragma once
#include "game_main_menu_native.h"

namespace dokodemo::recovered {
struct GameSystemOptionSnapshot {
    bool requested=false,constructor_entered=false,template_owner=false,clone_owner=false,failed=false;
    // These stay false at the stopped constructor. No idle-state completion.
    bool owner474=false,opened=false,poll_completed=false,retired=false,woken=false;
    uint64_t generation=0;
    uint32_t requests=0,initializer_attempts=0,resource=10,result=13,allocation_bytes=0x7C;
    uint32_t source_constructor=0x347C8,source_consumer=0,phase=0;
    GameMainMenuLease lease;
    std::array<uint32_t,3> passive_settings{};
    GameFsStatus failure;
};
// A stopped, owning initializer frontier, not an Options Open implementation.
// The real template/clone precede the first unsupported owned message consumer.
// Source1A3B0 cannot publish LoadMan474 or call34F1C until347C8 returns.
class GameSystemOptionFrontier final {
  public:
    GameFsStatus Request(GameMainMenuLease,std::array<uint32_t,3> passive_settings);
    GameFsStatus Advance(GameResourceSource&);
    GameSystemOptionSnapshot Snapshot()const{return state_;}
    void Abort()noexcept;
  private:
    GameFsStatus Refuse(GameFsStatus);
    GameSystemOptionSnapshot state_;
    std::shared_ptr<GameUiResource> resource_;
    GameUiInstance clone_;
};
}
