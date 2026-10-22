#pragma once
#include "game_inventory_native.h"
#include "../script_vm.h"

namespace dokodemo::recovered {
struct GameInventoryRowsObservation {
    bool valid=false;
    uint64_t generation=0,run_id=0;
    uint32_t table=0,index=0,header=0,rows=0,cells=0,valid_items=0;
    std::array<char,65> code_sha256{};
    std::array<uint8_t,32> sha256{};
};
struct GameInventoryRowShape {
    uint32_t table=0,index=0,header=0,flag_offset=0,model_offset=0;
};
// Owned canonical typed bytes. No VM reference/context/frame lease escapes.
struct GameInventoryRowsImage {
    GameInventoryRowsObservation observation;
    std::array<GameInventoryRowShape,170> rows{};
    std::vector<uint8_t> bytes;
};
GameInventoryRowsObservation GameObserveInventoryRows(const script::Vm&,uint64_t generation,
    const script::Value& registered,std::vector<GameInventoryItem>&,GameInventoryRowsImage* image=nullptr);
struct GameInventoryMarkSnapshot {
    bool live=false;
    uint32_t completions=0,event=0,row=0,store_pc=0;
    uint64_t receipt=0;
    std::array<uint8_t,32> initial_sha256{},verified_sha256{};
};
// Expected state evolves ONLY by a receipt-qualified source store at2FDA.
// Every byte outside the selected field5 remains in the immutable comparison.
class GameInventoryMarkChain final {
  public:
    void Begin(GameInventoryRowsImage);
    void CheckBefore(const GameInventoryRowsImage&,uint32_t event,uint32_t row)const;
    void Commit(const GameInventoryRowsImage&,uint32_t event,uint32_t row,uint64_t receipt,uint32_t store_pc,uint32_t stores);
    bool Matches(const GameInventoryRowsImage&)const noexcept;
    bool OwnsCell(uint32_t table,uint32_t index)const noexcept;
    void End()noexcept;
    GameInventoryMarkSnapshot Snapshot()const{return state_;}
  private:
    GameInventoryRowsImage expected_;
    GameInventoryMarkSnapshot state_;
};
}
