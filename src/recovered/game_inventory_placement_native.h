#pragma once
#include "game_inventory_mark_native.h"
#include "game_actor_runtime_native.h"
#include <map>

namespace dokodemo::recovered {
struct GameInventoryPlacementSnapshot {
    bool bound=false,pending=false,returned=false,flag_returned=false,callback_returned=false;
    uint32_t event=0,row=0,source_return_pc=0,stores=0,auxiliary_stores=0,services=0,completions=0;
    uint64_t receipt=0,source_return_executed=0;
};
struct GameInventoryModelEffect {
    bool result_stored=false;
    uint32_t service=0,pc=0,entry=0,result_word=0,row_table=0,row_index=0;
    int32_t slot=-1,resource=-1,category=0,selector=0,x=0;
    uint64_t executed=0,receipt=0,frame=0,generation=0,retirement_request=0,helper_return_executed=0;
    uint32_t helper_return_pc=0;
    uint32_t argument_kind=0,argument_word=0,argument_table=0,argument_index=0;
    uint64_t runtime_epoch=0,owner_phase=0;
    std::array<uint64_t,7> before_counts{},after_counts{}; // slots,pub,updates,requests,retired,assets,clones
    GameRoomModelSlotSnapshot before,after;
};
// Passive stable-input graph and source-write/model-effect ledger. No VM,
// context, frame, asset, package or clone strong owner is retained here.
class GameInventoryPlacementProof final {
  public:
    void BindModels(const GameActorRuntimeSnapshot&);
    void Begin(const script::Vm&,const GameInventoryRowsImage&,uint32_t event,uint32_t row,const GameActorRuntimeSnapshot&);
    void Receipt(uint64_t receipt,uint64_t callback_frame);
    void ObserveStore(const script::Vm&,const script::Value&,GameInventoryMarkChain&);
    void BeforeFilter(const script::Vm&,const std::vector<script::Value>&);
    void AfterFilter();
    void BeforeModel(const script::Vm&,uint32_t,const script::Value&,const GameActorRuntimeSnapshot&);
    void AfterModel(const script::Vm&,uint32_t,const script::Value&,const script::Value&,const GameActorRuntimeSnapshot&,const GameActorRuntimeSnapshot&);
    void ObserveReturn(const script::FrameTransition&);
    void Commit(const script::Vm&,const GameInventoryRowsImage&,GameInventoryMarkChain&);
    bool RetirementsComplete(const GameActorRuntimeSnapshot&)const;
    bool MatchesAuxiliary(const script::Vm&)const;
    bool OwnsAuxiliaryCell(const script::Value&)const noexcept;
    void Abort()noexcept;
    void End()noexcept;
    bool Pending()const noexcept{return state_.pending;}
    const std::vector<GameInventorySourceStore>& Stores()const noexcept{return stores_;}
    const std::vector<GameInventoryModelEffect>& Effects()const noexcept{return effects_;}
    GameInventoryPlacementSnapshot Snapshot()const{return state_;}
  private:
    struct Atom {
        uint32_t kind=0,word=0,table=0,index=0;std::string text;
        bool operator==(const Atom& b)const{return kind==b.kind&&word==b.word&&table==b.table&&index==b.index&&text==b.text;}
    };
    enum class Role:uint32_t { ReadOnly,Control,PredicateAD,PredicateB1,PredicateB5,Scratch };
    struct Cell {Atom value;Role role=Role::ReadOnly;uint32_t row=0,field=UINT32_MAX;};
    using Cells=std::map<uint64_t,Cell>;
    Cells approved_,next_;
    std::vector<script::Value> controls_; // stable references contain weak owners only
    std::map<uint64_t,GameRoomModelSlotSnapshot> claims_;
    std::array<GameRoomModelSlotSnapshot,100> baseline_models_{};
    std::vector<GameInventorySourceStore> stores_;
    std::vector<GameInventoryModelEffect> effects_;
    std::vector<GameInventoryModelEffect> retirements_;
    std::vector<uint32_t> filter_indices_;
    uint32_t filter_written_=0,filter_pc_=0,filter_capacity_=0;
    uint64_t filter_frame_=0,run_id_=0;
    bool control_filter_seen_=false,control_store_seen_=false,control_service_started_=false,control_service_seen_=false;
    uint32_t control_service_=0;
    uint64_t control_helper_=0;
    script::Value control_match_,source_list_;
    uint64_t source_frame_=0;
    std::vector<uint32_t> chosen_rows_,reflow_rows_;
    uint32_t reflow_index_=0;
    bool reflow_store_seen_=false;
    uint64_t runtime_epoch_=0;
    uint64_t callback_frame_=0;
    script::VmStateWitness suspended_;
    std::vector<script::Value> suspended_frame_,suspended_operands_;
    script::Value suspended_result_;
    std::array<char,65> code_{};
    GameInventoryRowsImage before_;
    GameInventoryPlacementSnapshot state_;
    static Atom Capture(const script::Value&);
    static uint64_t Key(const script::Value&);
    static script::Value Ref(uint32_t table,uint32_t index);
    Cells CaptureAuxiliary(const script::Vm&,std::vector<script::Value>* controls=nullptr)const;
    bool SameAuxiliary(const Cells&,const Cells&)const;
    void ScalarPatch(uint64_t,const script::Value&);
    void Claim(const script::Vm&,const script::Value&,const GameActorRuntimeSnapshot&);
    void CheckContext(const script::Vm&,const GameActorRuntimeSnapshot&)const;
    void CheckInventoryOwners(const script::Vm&)const;
    void BeginImpl(const script::Vm&,const GameInventoryRowsImage&,uint32_t,uint32_t,const GameActorRuntimeSnapshot&);
};
}
