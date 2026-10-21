#pragma once
#include "game_actor_dictionary_native.h"
#include "game_actor_runtime_native.h"
#include "game_inventory_native.h"
#include "game_record_native.h"
#include "game_profile_native.h"
#include "rng_native.h"
#include <map>

namespace dokodemo::recovered {
//6A350/7BF18 reached string-only ABI. Bytewise,nonoverlapping replacement;
//empty needle is refused (original explicitly traps),result owns its bytes.
script::Value GameStringReplace51(const std::vector<script::Value>&);
struct GameOwnerEventTarget {
    uint64_t generation=0;
    script::Value context,callback;
};
struct GameMessageTarget {
    uint64_t generation=0;
    uint32_t message=0,pc=0;
    script::Value context;
};
struct GameChoiceTarget {
    uint64_t generation=0;
    uint32_t request=0,pc=0;
    script::Value context;
};
struct GameActorScriptSnapshot {
    bool live=false,started=false,completed=false,blocked=false,awaiting_message=false;
    uint64_t generation=0;
    bool autoload_imported=false,profile_loaded_marker=false,manager_flag68=false;
    // Native new-profile state has no optional imported save fields. Their
    // source names/producers remain unknown;this is not a BSS-zero claim.
    bool native_optional_fields_absent=true;
    bool source_flag2A980=false; // explicit24A38 reset,new game without PSP save import
    uint32_t scene_flag_queries=0,actor_flag_queries=0;
    uint32_t bytes=0,entry_pc=0,pc=0,last_opcode=0,ticks=0,yields=0,messages=0;
    uint64_t native_clock_ticks=0;
    script::VmStateWitness vm_current;
    script::NormalReturnWitness normal_return;
    script::NormalReturnWitness farewell_return;
    uint32_t b3_calls=0,b3_pc=0,b3_operand_kind=0,b3_operand_word=0,b3_result_word=0;
    uint32_t builtin_calls=0,game_service_calls=0,last_service=UINT32_MAX;
    uint32_t rng17_calls=0,rng17_pc=0,rng17_bound=0,rng17_raw=0,rng17_result_word=0;
    uint32_t actor22_calls=0,actor22_pc=0,actor22_command_word=0,actor22_parameter_word=0;
    uint32_t last_service_argument_word=0,last_service_result_word=0;
    bool last_service_returned=false;
    uint32_t scene_period_queries=0,scene_period_clock_word=0,scene_period_result_word=0;
    uint32_t position_result_word=0;
    uint32_t game28_queries=0,game28_result_word=0;
    uint32_t camera_setter_returns=0,camera_setter_result_word=0;
    uint32_t local_config_writes=0,message_acknowledgements=0;
    bool awaiting_choice=false;
    bool awaiting_input=false,input_timed_out=false;
    uint32_t input_requests=0,input_completions=0,input_mode=0;
    std::string input_initial,input_text,input_auxiliary;
    bool source_main_input_suppressed48=false;
    uint32_t choice_requests=0,choice_completions=0,choice_result_word=0;
    uint32_t b4_calls=0,b4_operand_word=0,b4_result_word=0;
    uint32_t e2_calls=0,e2_left_word=0,e2_right_word=0,e2_result_word=0;
    uint32_t d0_calls=0,d0_left_word=0,d0_right_word=0,d0_result_word=0;
    bool native_reserved_local_nil=false;
    std::vector<std::string> choice_options;
    uint32_t string_replace_calls=0;
    std::string last_replacement;
    std::vector<std::string> replacement_results;
    uint32_t call_depth=0;
    bool inventory_requested=false;
    uint32_t inventory_requests=0,inventory_setter_result_word=0;
    int32_t inventory_mode=0;
    bool context_enabled=true;
    uint32_t callbacks_posted=0,callbacks_returned=0;
    uint32_t owner_event_calls=0,owner_event_callback_word=0,context_word=0,callback_depth=0,queued_callbacks=0,operand_count=0;
    uint32_t sound73_requests=0,sound73_result_word=0;
    int32_t sound73_playback_result=-1;
    int32_t owner_event_result=-1;
    uint64_t deadline_tick=0;
    uint32_t inventory_completions=0;
    uint32_t game71_calls=0,game71_result_word=0,record_clock_word=0,record_cell0_writes=0;
    uint32_t game3_calls=0,game3_result_word=0;
    script::VmStateWitness game3_execution; // actual dispatch,not historical last_service
    uint32_t record_cell0_before=0,record_cell0_after=0;
    std::string record_input_text;
    GameRecordStoreSnapshot records;
    std::vector<GameInventoryItem> inventory_items;
    std::array<uint32_t,5> message_local_words{};
    std::array<uint32_t,3> intro_caller_words{};
    uint32_t clock_calls=0,last_clock_word=0,intro_clock_origin=0,intro_clock_expired=0;
    std::vector<std::string> message_history;
    std::vector<uint32_t> message_pcs,message_actor_commands;
    int32_t scene_period=-1;
    uint32_t dictionary_game_reads=0,dictionary_game_writes=0;
    uint32_t array_query_calls=0,array_query_matches=0,array_actions=0;
    uint64_t random_selections=0;
    uint32_t selected_value_word=0;
    uint32_t root_functions_returned=0;
    uint32_t control_blocks_entered=0,control_blocks_closed=0,active_control_blocks=0,switches=0;
    bool waiting_for_event=false;
    uint32_t event_signals=0,event_repeat_signals=0,native_budget_yields=0;
    uint32_t source_timestamp_writes=0,source_deadline_writes=0;
    bool waiting_deadline=false;
    GameActorRuntimeSnapshot runtime;
    std::vector<uint32_t> last_query_indices;
    uint64_t instructions=0;
    std::vector<uint32_t> service_ids;
    std::string path,sha256,message,error;
    std::string last_normalized;
    GameActorDictionarySnapshot dictionary;
};
// Actual230EC ->6656C ->67FD4 entry. This is the game's native KSC bytecode
// interpreter,not a PSP/MIPS runtime. Named host services bind actual current
// native profile/state; unsupported operations retain an explicit boundary.
class GameActorScript final {
  public:
    using ClockWord = std::function<uint32_t()>;
    GameFsStatus Initialize(PackageView dictionary,PackageView script,std::string path,
                            const GameProfileRecord& profile,ClockWord clock = {},RngNativeOwner* rng=nullptr,
                            GameActorRuntime* runtime=nullptr,GameRecordStore* records=nullptr,GameSoundBank* sound=nullptr);
    GameFsStatus Tick();
    bool AcknowledgeMessage(const GameMessageTarget* expected=nullptr);
    GameMessageTarget MessageTarget()const;
    GameChoiceTarget ChoiceTarget()const;
    GameFsStatus CompleteChoice(const GameChoiceTarget&,uint32_t result);
    GameChoiceTarget InputTarget()const;
    GameFsStatus CompleteInput(const GameChoiceTarget&,std::string text,std::string auxiliary,bool timed_out);
    bool CanResumeInventory()const;
    GameFsStatus CompleteInventory(const GameInventorySnapshot&);
    GameOwnerEventTarget OwnerEventTarget()const;
    GameFsStatus PostMainFlowEvent(uint32_t event_id,int32_t argument,const GameOwnerEventTarget* expected=nullptr);
    GameActorScriptSnapshot Snapshot() const;
    void Close();
  private:
    script::Value Service(uint8_t,const std::vector<script::Value>&);
    script::Value GameService(const std::vector<script::Value>&);
    script::Value DispatchGameService(const std::vector<script::Value>&);
    PackageView script_image_;
    script::Program program_;
    std::unique_ptr<script::Vm> vm_;
    GameActorDictionary dictionary_;
    GameProfileRecord profile_;
    ClockWord clock_;
    RngNativeOwner* rng_=nullptr;
    GameActorRuntime* runtime_=nullptr;
    GameRecordStore* records_=nullptr;
    GameSoundBank* sound_=nullptr;
    std::map<uint32_t,script::Value> bindings_;
    GameActorScriptSnapshot state_;
    uint64_t owner_generation_=0;
};
} // namespace dokodemo::recovered
