#pragma once
#include "game_actor_dictionary_native.h"
#include "game_actor_runtime_native.h"
#include "game_inventory_native.h"
#include "game_record_native.h"
#include "game_profile_native.h"
#include "rng_native.h"
#include "game_owned_conversation_native.h"
#include <map>

namespace dokodemo::recovered {
//6A350/7BF18 reached string-only ABI. Bytewise,nonoverlapping replacement;
//empty needle is refused (original explicitly traps),result owns its bytes.
script::Value GameStringReplace51(const std::vector<script::Value>&);
//6A5DC ->7C000 byte count using all256 source E07CC bit4 entries.
//Checked NUL/extent boundaries;75778 numeric formatting is unsupported.
script::Value GameStringLength53(const std::vector<script::Value>&);
//Historical API names retain ASCII callers. Latin/kana direct branches now
//include empty owned strings; WindowsPrefixOwnedText deep-copies a proved
//static prefix instead of fabricating its original tag5/pool-relative address.
script::Value GameAsciiPrefix71(const std::vector<script::Value>&);
//6A7E4 ->7C360: LAST mapped prefix, or NIL if any non-kana unit occurs.
//Same WindowsPrefixOwnedText adapter as71; source static tag5 is qualified.
script::Value GameKanaSuffix72(const std::vector<script::Value>&);
//693E4: canonical NIL maps to E0760 bytes83,73 -> integer7383.
//A proved two-byte kana-map result is packed little-endian while its native
//owning bytes remain live. Other75778 conversions are refused.
script::Value GameNilBytePair0D(const std::vector<script::Value>&);
//76A50 static-constructor defaults, owned for this native script lifetime.
//Finite229-unit7C1EC scan includes empty strings and the83-entry hira map.
//No interface fabricates later aliased PSP global writes.
class GameAsciiClass70Owner final {
  public:
    void Begin(uint64_t generation) noexcept;
    void Close() noexcept;
    script::Value Query(const std::vector<script::Value>&)const;
    bool Live()const noexcept{return live_;}
    uint64_t Generation()const noexcept{return generation_;}
    uint32_t CurrentWord()const noexcept{return current_.word;}
    uint32_t OtherWord()const noexcept{return other_.word;}
  private:
    bool live_=false;
    uint64_t generation_=0;
    script::Value current_,other_;
};
struct GameInventoryModelService {
    bool returned=false;
    uint32_t service=0,pc=0,entry=0,result_word=0;
    uint64_t receipt=0,frame=0;
    int32_t slot=-1,resource=-1,category=0,selector=0,x=0;
};
struct GameInventoryActionSnapshot {
    bool pending=false,returned=false,completed=false,failed=false;
    uint64_t generation=0,receipt=0,callback_frame=0;
    uint32_t event=0,row=0,completions=0,row_table=0,row_index=0,row_header=0;
    int32_t item_id=-1;std::array<uint32_t,3> before_words{},after_words{};
    std::array<uint8_t,3> before_kinds{},after_kinds{};
    std::array<uint32_t,3> before_controls{},after_controls{};
    std::array<uint8_t,3> before_control_kinds{},after_control_kinds{};
    uint32_t row_cells=0;
    std::array<uint32_t,10> before_row_words{},after_row_words{};
    std::array<uint8_t,10> before_row_kinds{},after_row_kinds{};
    uint32_t gate_word=0,definition_table=0,definition_index=0,definition_header=0;
    uint32_t definition_mask=0,definition_width=0,layout_table=0,layout_index=0,layout_header=0;
    std::array<uint32_t,2> layout_words{};
    uint32_t model_service_count=0;
    std::array<GameInventoryModelService,64> model_services{};
    script::VmStateWitness suspended,restored;
};
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
// Passive, bounded current-table evidence. No Value or owner lease escapes.
// The imported dictionary snapshot intentionally remains its original 162 rows.
struct GameWordRowObservation {
    bool valid=false;
    uint64_t generation=0,run_id=0;
    uint32_t row=0,table=0,index=0,global_header_word=0,record_header_word=0,error_code=0;
    std::array<char,65> code_sha256{};
    std::array<script::ValueWitness,19> fields{};
};
GameWordRowObservation GameObserveWordRow(const script::Vm&,uint64_t generation,uint32_t row) noexcept;
struct GameActorScriptSnapshot {
    GameOwnedConversationSnapshot owned_conversation;
    uint64_t post_modal_observation_refusals=0;
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
    GameWordRowObservation candidate_word_row;
    //Independent stable-table watch bound at the actual first builtin53
    //consumer frame. A successor message need not have numeric Frame[0].
    GameWordRowObservation word_consumer_row;
    uint32_t word_consumer_source_pc=0,word_consumer_source_entry=0;
    uint64_t word_consumer_source_frame_id=0;
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
    uint32_t d5_calls=0,d5_first_pc=0,d5_first_left_word=0,d5_first_right_word=0,d5_first_result_word=0;
    uint32_t ctx_reference_branches=0,ctx_reference_first_pc=0,ctx_reference_first_index=0;
    bool native_reserved_local_nil=false;
    std::vector<std::string> choice_options;
    uint32_t string_replace_calls=0;
    uint32_t string_length53_calls=0,string_length53_pc=0,string_length53_result_word=0;
    std::string string_length53_input;
    uint32_t string_length53_extended_calls=0,string_length53_extended_pc=0,string_length53_extended_result=0;
    std::string string_length53_extended_input;
    uint32_t string_prefix71_calls=0,string_prefix71_pc=0,string_prefix71_result_word=0,string_prefix71_result_kind=0;
    std::string string_prefix71_input,string_prefix71_result_text;
    uint32_t string_suffix72_calls=0,string_suffix72_pc=0,string_suffix72_result_word=0,string_suffix72_result_kind=0;
    std::string string_suffix72_input,string_suffix72_result_text;
    uint32_t nil_code0d_calls=0,nil_code0d_pc=0,nil_code0d_result_word=0;
    bool ascii_class70_live=false;
    uint64_t ascii_class70_generation=0;
    uint32_t ascii_class70_current_word=0,ascii_class70_other_word=0;
    uint32_t ascii_class70_calls=0,ascii_class70_pc=0,ascii_class70_result_word=0;
    std::string ascii_class70_input;
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
    GameInventoryActionSnapshot inventory_action;
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
    GameInventoryActionAdmission PostInventoryAction(uint64_t inventory_generation,uint32_t event,int32_t row);
    bool CanResumeInventory()const;
    GameFsStatus CompleteInventory(const GameInventorySnapshot&);
    GameOwnerEventTarget OwnerEventTarget()const;
    GameFsStatus PostMainFlowEvent(uint32_t event_id,int32_t argument,const GameOwnerEventTarget* expected=nullptr);
    GameFsStatus PostConversationInteraction(const GameOwnerEventTarget&,uint64_t origin_activation,bool owned_dispatch,int32_t& queue_result);
    GameFsStatus PostPostModalInteraction(const GameOwnerEventTarget&,uint64_t post_modal_generation,
                                         uint64_t inventory_generation,uint64_t input_sequence,int32_t& queue_result);
    GameActorScriptSnapshot Snapshot() const;
    GameWordRowObservation ObserveWordRow(uint32_t row)const noexcept;
    void Close();
  private:
    void CaptureInventoryItems();
    void ObserveInventoryAction(const script::FrameTransition&)noexcept;
    void RecordInventoryModelService(uint32_t,int32_t,int32_t,int32_t,int32_t,int32_t,uint32_t,bool=true);
    script::Value Service(uint8_t,const std::vector<script::Value>&);
    script::Value GameService(const std::vector<script::Value>&);
    script::Value DispatchGameService(const std::vector<script::Value>&);
    PackageView script_image_;
    script::Program program_;
    std::unique_ptr<script::Vm> vm_;
    GameActorDictionary dictionary_;
    GameAsciiClass70Owner ascii_class70_;
    GameOwnedConversation owned_conversation_;
    GameProfileRecord profile_;
    ClockWord clock_;
    RngNativeOwner* rng_=nullptr;
    GameActorRuntime* runtime_=nullptr;
    GameRecordStore* records_=nullptr;
    GameSoundBank* sound_=nullptr;
    std::map<uint32_t,script::Value> bindings_;
    GameActorScriptSnapshot state_;
    uint64_t owner_generation_=0;
    uint32_t word_consumer_row_index_=UINT32_MAX;
};
} // namespace dokodemo::recovered
