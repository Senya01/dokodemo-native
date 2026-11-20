#pragma once
#include "room_meeting_channel.h"
#include "profile_adoption.h"
#include "room_conversation_flow.h"
#include "room_activity_event.h"
#include "room_vocabulary.h"
#include "room_fallback.h"
#include "room_fallback_list.h"
#include "room_word_traits.h"
#include "room_sound.h"
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dokodemo::recovered { class PackageView; class RngNativeOwner; }
namespace dokodemo::gameplay {
class RoomInventory;

struct RoomOutingDescriptor {
    unsigned destination, kind, word;
    int eligibility_word; // 2D8A prelookup in the outing menu, absent from a 12F39 offer.
    unsigned primary_filter, secondary_filter;
    std::string written, label, title, description;
};

// Toro's introductory and ordinary KSC conversation frames share the room's
// vocabulary. The room supplies actor callbacks, randomness and controller input.
class RoomTalkState {
public:
    bool Load(const std::filesystem::path& post_profile,const AcceptedProfile& profile,std::string& error);
    bool BeginOrdinaryTopic(unsigned selected_pc,bool queued=false);
    bool BeginRoboContactGreeting();
    unsigned named_contact_entry() const;
    unsigned attention_entry() const;
    bool BeginNamedContact();
    bool TakeRoboContactGreetingReturn();
    struct ScheduledProgress {
        unsigned contacts, diary_count;
        std::uint32_t adoption_minute;
    };
    bool BeginScheduledRoomEvent(unsigned event, std::uint32_t midnight, ScheduledProgress progress);
    bool scheduled_room_event_active() const { return scheduled_room_event_ != 0; }
    std::optional<bool> TakeScheduledNavigation();
    std::optional<RoomSound> TakeScheduledSound();
    struct FarewellSites { unsigned game75, game46, reset; };
    FarewellSites farewell_sites() const;
    std::vector<unsigned> TakeScheduledActorSelections();
    std::optional<unsigned> TakeScheduledCamera();
    std::optional<std::uint32_t> TakeScheduledActivityDeadline();
    std::array<std::optional<unsigned>,2> TakeScheduledStayWrites();
    struct ScheduledDiary {
        unsigned descriptor, priority;
        const std::string& text;
    };
    std::vector<ScheduledDiary> scheduled_diaries() const;
    ScheduledDiary expired_stay_diary() const;
    void FinishScheduledDiary(bool recorded);
    bool TakeScheduledMenuRestriction();
    enum class FarewellAction { wait_for_cover, write_slot, wait_for_slot };
    std::optional<FarewellAction> pending_farewell_action() const;
    void FinishFarewellAction(FarewellAction action);
    bool TakeFarewellComplete();
    unsigned farewell_mode() const { return farewell_mode_; }
    std::optional<bool> TakeScheduledReturn();
    std::optional<unsigned> TakeScheduledActorCommand(unsigned record);
    bool BeginOrdinaryFarewell();
    void EnterWithoutIntroduction() { phase_=Phase::done; }
    unsigned SelectHomeInvitation(unsigned mode,unsigned period,recovered::RngNativeOwner& rng);
    bool BeginHomeInvitation();
    void BindHomeInvitationReturn(std::function<void()> callback) { home_invitation_return_=std::move(callback); }
    bool BeginOuting(unsigned period);
    bool TakeOutingRoomRequest();
    bool BeginOutingReturn();
    RoomContactLocation contact_location() const;
    std::vector<std::string> current_outing_names() const;
    const auto& outing_descriptor() const { return outing_snapshot_; }
    const auto& outing_sites() const { return outing_sites_; }
    void SetOutingEnvironment(bool game28,bool lamp,unsigned period,unsigned camera_mode);
    void SetOutingActorFlags(unsigned interrupt,bool locked) { outing_interrupt_=interrupt; outing_locked_=locked; }
    bool TakeOutingInterrupt();
    bool TakeOutingWideCamera();
    std::optional<bool> TakeOutingWipe();
    std::optional<unsigned> outing_return_deadline() const;
    std::optional<unsigned> outing_scene_request() const { return outing_scene_request_; }
    bool FinishOutingScene(unsigned selector);
    std::array<std::optional<std::string>,8> outing_labels() const;
    bool outing_inventory_restore_requested() const { return phase_==Phase::outing_inventory; }
    bool FinishOutingInventoryRestore();
    bool BeginTeaching();
    bool BeginFriendsIntroduction();
    bool BeginFriendsExchange(unsigned argument,const RoomInventory& inventory);
    void EnablePeerMeetings() { peer_meetings_enabled_=true; }
    std::optional<MeetingPlace> meeting_request() const { return meeting_request_; }
    void FinishPeerMeeting(bool completed);
    bool friends_reset_contact_timer() const { return friend_timer_requested_; }
    std::optional<bool> TakeFriendsBanner();
    void SetMessageSpeed(unsigned speed) { message_speed_ = speed; }
    bool counted_contact() const { return counted_contact_; }
    unsigned learned_word_serial() const { return learned_serial_; }
    std::optional<unsigned> learned_word_row() const { return learned_row_; }
    const std::string& learned_word() const { return learned_written_; }
    const std::string& learned_reading() const { return learned_reading_; }
    bool BeginRoomAttentionMessage();
    bool room_attention_return() const { return attention_return_; }
    unsigned cols() const { return columns_; }
    unsigned rows() const { return rows_; }
    RoomFallbackList& fallback_list() { return fallback_list_; }
    const RoomFallbackList& fallback_list() const { return fallback_list_; }
    RoomRumors& rumors() { return rumors_; }
    const RoomRumors& rumors() const { return rumors_; }
    void StampContactLocation(std::uint32_t minute);
    RoomVocabulary& vocabulary() { return vocabulary_; }
    const RoomVocabulary& vocabulary() const { return vocabulary_; }
    // Cells 12, 14 and 15 of NEKO's actor save array, in source order.
    std::array<KscSaveValue,3> ContactHistoryTables() const;
    // Cells 6 and 13 are tagged progress flags read by the same script owner.
    std::array<KscSaveValue,2> ProgressFlagWords() const;
    void RestoreContactHistory(const std::array<KscSaveValue,3>& tables,
                               const std::array<KscSaveValue,2>& progress);
    bool supports_ordinary_topic(unsigned selected_pc) const;
    bool ordinary_active() const { return ordinary_; }
    bool ordinary_source_return() const { return ordinary_return_; }
    bool ordinary_rejected() const { return ordinary_rejected_; }
    std::optional<unsigned> ordinary_frontier() const { return frontier_; }
    void ReleaseOrdinaryFrontier();
    bool word_entry_requested() const { return phase_==Phase::word_entry; }
    void FinishWordEntry(std::string_view written,std::string_view reading);
    bool answer_visible() const { return phase_==Phase::answer; }
    bool answer_closing() const { return answer_close_; }
    unsigned answer_frame() const { return answer_frame_; }
    unsigned answer_ticks() const { return answer_ticks_; }
    unsigned answer_cursor() const { return answer_cursor_; }
    int last_answer() const { return last_answer_; }
    bool answer_interactive() const { return answer_visible() && !answer_close_ && answer_frame_==10; }
    const std::vector<std::string>& answer_options() const { return answer_options_; }
    std::vector<unsigned> TakeEffects();
    std::vector<std::optional<unsigned>> TakeTopicEnqueues();
    bool TakeFirstDayDiaryRequest();
    void Step(std::uint32_t buttons,bool available,bool actor_ready,const recovered::PackageView* font,
              bool prompt_expired=false,std::optional<bool> invitation_event={},
              recovered::RngNativeOwner* rng=nullptr,bool secondary_ready=false);
    bool busy_for_room_input() const { return visible() || answer_visible() || gift_requested() ||
                                             word_entry_requested() || phase_==Phase::answer_pending || phase_==Phase::inventory; }
    bool gift_requested() const { return phase_==Phase::gift; }
    void SupplyGift(int slot);
    std::optional<int> inventory_requested() const;
    void FinishInventory(bool emptied_slot);
    std::optional<unsigned> random_requested() const;
    void SupplyRandom(unsigned value);
    bool prompt_visible() const { return available_ && phase_==Phase::prompt; }
    bool consumed() const { return consumed_; }
    bool visible() const { return appearance_>0; }
    int mood() const { return mood_; }
    unsigned mood_age() const { return (script_ticks_ - mood_set_tick_) & 0x0fffffffu; }
    void SetMood(int mood) { mood_=mood; mood_set_tick_=script_ticks_; }
    void SetActorField50(int value) { actor_field50_=value; }
    void SetCalendarMinute(uint32_t minute) { calendar_minute_=minute; }
    void SetRoomDay(int day) { room_day_=day; }
    bool finished() const { return phase_==Phase::done; }
    bool wide_camera_requested() const { return wide_camera_requested_; }
    const std::string& error() const { return error_; }
    std::optional<unsigned> actor_command() const { return actor_command_; }
    const std::vector<std::string>& lines() const { return lines_; }
    float appearance() const { return appearance_; }
    float scroll() const { return lines_.size()>rows_?18.f-scroll_:0.f; }
    unsigned message_pc() const { return message_pc_; }
    std::string_view text() const { return message_; }
    unsigned message_indirect_topic() const { return message_indirect_topic_; }
    const std::vector<int>& message_word_rows() const { return message_word_rows_; }
    const std::vector<RoomMessageWord>& message_word_operands() const { return message_word_operands_; }
    bool message_revealed() const { return appearance_==1 && scroll_==0 && text_cursor_==message_.size(); }
    const std::optional<std::string>& keyboard_initial_reading() const { return keyboard_initial_reading_; }
    bool event20_cleanup_enabled() const { return event20_cleanup_; }
private:
    friend struct RoomTalkOpcodeTests;
    enum class Phase { unsupported,prompt,running,suspended,gift,inventory,random,answer_pending,answer,word_entry,outing_scene,outing_inventory,scheduled_diary,scheduled_farewell,meeting,frontier,done,failed };
    enum class Kind { nil,integer,truth,frame_ref,global_ref,global_element,constant_ref,word_row,word_field,array,element,teach_field,string,code,calendar_time,context,question_row,question_field,fixed14,script_local_ref,script_local_array,association,association_ref };
    struct Value {
        Kind kind=Kind::nil;
        int data=0;
        std::string text;
        std::size_t depth=0;
        std::shared_ptr<std::vector<Value>> cells;
        std::vector<RoomMessageWord> word_operands;
        std::shared_ptr<std::map<std::string,Value>> entries;
        std::size_t procedure_cursor=0;
        std::uint64_t frame_owner=0;
    };
    struct Frame { unsigned resume; std::vector<Value> locals; unsigned indirect_topic; };
    Phase phase_=Phase::unsupported;
    std::map<unsigned,std::vector<unsigned char>> code_;
    std::map<unsigned,std::string> pool_;
    std::string nil_inline_text_;
    std::map<unsigned,std::vector<unsigned>> predicates_;
    std::map<unsigned,std::vector<unsigned>> phrase_lists_;
    std::map<unsigned,std::vector<unsigned>> procedure_lists_;
    // Mutable NEKO arrays belong to this script instance across room contacts.
    std::map<unsigned,std::vector<Value>> global_arrays_;
    std::map<unsigned,std::size_t> global_procedure_cursors_;
    std::vector<Value> script_locals_;
    void LoadScriptLocals(const std::filesystem::path& dir);
    void InitializeArray(unsigned opcode);
    void RememberProcedure(unsigned table,unsigned index,unsigned target);
    RoomVocabulary vocabulary_;
    bool first_day_diary_request_ = false;
    bool friends_introduction_=false,friend_timer_requested_=false;
    const RoomInventory* exchange_inventory_=nullptr;
    unsigned meeting_event_depth_=0;
    bool peer_meetings_enabled_=false;
    std::optional<MeetingPlace> meeting_request_;
    std::optional<bool> friends_banner_;
    std::map<unsigned,Value> friends_cells_;
    bool event20_cleanup_=true; // CT0 starts as tagged true in NEKO.KSC.
    RoomRumors rumors_;
    static RoomRumors::Pair RumorPair(const Value& subject,const Value& statement);
    void SupplyRumor(const Value& subject,const Value& statement,unsigned pick);
    RoomFallbackList fallback_list_;
    RoomWordTraits traits_;
    std::vector<unsigned> unasked_traits_;
    int trait_row_=-1;
    Value selected_fallback_;
    bool queued_root_=false;
    unsigned scheduled_room_event_=0;
    std::uint32_t scheduled_midnight_=0;
    ScheduledProgress scheduled_progress_{};
    unsigned farewell_mode_=0;
    bool ScheduledFarewell() const { return scheduled_room_event_==3 || scheduled_room_event_==5; }
    unsigned FarewellReturnPc() const {
        if (alternate_greeting_)
            return alternate_greeting_->schedule.callbacks[scheduled_room_event_-2] +
                   (scheduled_room_event_==5 ? 0x76 : 0x9e);
        if (robo_greeting_) return scheduled_room_event_==5 ? 0x67618 : 0x6759f;
        return scheduled_room_event_==5 ? 0x89701 : 0x89688;
    }
    std::optional<bool> scheduled_navigation_;
    std::optional<RoomSound> scheduled_sound_;
    std::optional<Value> scheduled_deadline_;
    std::optional<std::uint32_t> scheduled_activity_deadline_;
    std::optional<unsigned> scheduled_camera_request_;
    std::array<std::optional<unsigned>,2> scheduled_stay_writes_;
    bool scheduled_menu_restriction_=false;
    enum class FarewellStage { inactive, requested, cover_wait, covered, writing,
                               written, slot_wait, acknowledged, reset, complete };
    FarewellStage farewell_stage_=FarewellStage::inactive;
    std::optional<bool> scheduled_count_contact_;
    std::vector<unsigned> scheduled_selections_;
    std::array<std::optional<unsigned>,2> scheduled_actor_commands_;
    unsigned scheduled_camera_=0;
    bool scheduled_secondary_=false, actor_wait_secondary_=false;
    bool scheduled_return_=false;
    std::vector<std::optional<unsigned>> topic_enqueues_;
    std::array<Value,12> teaching_;
    int reserved_word_=-1;
    Value editor_pair_;
    std::optional<std::string> keyboard_initial_reading_;
    void OpenWordKeyboard(int mode,std::string initial,Value destination);
    void LoadOutings(const std::filesystem::path& dir);
    struct ControlBlock { unsigned close; std::size_t depth,base; };
    std::vector<ControlBlock> blocks_;
    struct ChildScript {
        Phase phase=Phase::done;
        std::uint64_t generation=0;
        unsigned pc=0,timer_base=0,deadline=0,indirect_topic=0;
        int delay=0,instruction_cost=1;
        std::vector<Value> stack,locals;
        std::vector<Frame> calls;
        std::vector<ControlBlock> blocks;
        Value result;
        std::vector<std::size_t> children;
        void SwapExecution(RoomTalkState& vm);
    };
    // A flat pool keeps slot indices stable and releases deep trees iteratively.
    std::deque<ChildScript> child_scripts_;
    std::vector<std::size_t> root_children_;
    ChildScript* executing_child_=nullptr; // Set only for the duration of a child slice.
    std::uint64_t child_generation_=0;
    unsigned child_budget_=0;
    void StartChildScript(unsigned target,unsigned argc);
    void StepChildren(recovered::RngNativeOwner* rng);
    void AdvanceScript(recovered::RngNativeOwner* rng);
    std::uint64_t FrameOwner() const { return executing_child_?executing_child_->generation:0; }
    struct OutingCallback {
        unsigned resume;
        std::vector<Value> stack,locals;
        std::vector<Frame> calls;
        std::vector<ControlBlock> blocks;
        Value result;
        int delay;
        std::optional<bool> run_override;
    };
    std::optional<OutingCallback> outing_callback_;
    std::optional<unsigned> queued_outing_callback_;
    int instruction_cost_=1;
    std::map<unsigned,std::vector<Value>> outing_tables_;
    std::function<void()> home_invitation_return_;
    unsigned home_invitation_mode_=0,home_invitation_id_=0;
    std::map<unsigned,unsigned> eligibility_words_;
    std::map<unsigned,Value> outing_script_cells_;
    std::optional<RoomOutingDescriptor> outing_snapshot_;
    std::optional<unsigned> outing_scene_request_;
    std::optional<bool> outing_wipe_;
    std::vector<unsigned> outing_sites_;
    std::array<unsigned,19> outing_details_{};
    unsigned outing_interrupt_=0,outing_camera_=0,outing_current_scene_=0;
    bool outing_game28_=false,outing_lamp_=false,outing_locked_=false,outing_interrupt_requested_=false;
    bool outing_wide_=false,outing_returning_=false;
    unsigned outing_enabled_=0,outing_period_=0,outing_index_=0;
    std::array<unsigned,20> outing_period_masks_{};
    bool excursion_=false,outing_room_request_=false;
    bool direct_teaching_=false,counted_contact_=false;
    std::optional<unsigned> learned_row_;
    unsigned learned_serial_=0;
    std::string learned_written_,learned_reading_;
    Value DictionaryField(int row,int field) const;
    std::vector<Value> stack_,locals_;
    std::vector<Frame> callers_;
    std::string nickname_,player_,suffix_,farewell_word_,error_;
    std::array<std::vector<unsigned>,2> break_classes_;
    std::string message_;
    unsigned indirect_topic_=0, message_indirect_topic_=0;
    std::vector<int> message_word_rows_;
    std::vector<RoomMessageWord> message_word_operands_;
    std::vector<std::string> lines_;
    std::optional<unsigned> actor_command_;
    std::optional<unsigned> frontier_;
    Value result_;
    unsigned pc_=0,message_pc_=0,previous_=0,random_bound_=0;
    unsigned script_ticks_=0,script_timer_base_=0,script_deadline_=0,mood_set_tick_=0,intro_flags_=0;
    std::optional<uint32_t> calendar_minute_, adoption_minute_;
    std::optional<int> room_day_;
    std::size_t text_cursor_=0,group_end_=0;
    unsigned message_speed_=2;
    int delay_=0,glyph_delay_=0,idle_=0,gift_slot_=-1;
    float appearance_=0,scroll_=0;
    bool consumed_=false,available_=false,actor_wait_=false,message_wait_=false,wide_camera_requested_=false;
    bool closing_=false,new_line_=false,slot_empty_=false,farewell_=false,skip_farewell_=false;
    bool page_close_=false;
    int mood_=0,actor_field50_=0;
    std::vector<std::string> answer_options_;
    std::vector<unsigned> effects_;
    unsigned answer_frame_=0,answer_cursor_=0,answer_ticks_=0;
    int last_answer_=-1;
    std::array<unsigned,2> answer_held_{};
    bool answer_close_=false,ordinary_=false,ordinary_return_=false,ordinary_rejected_=false;
    bool attention_=false,attention_return_=false;
    unsigned columns_=9,rows_=4;
    enum class RandomUse { rumor_pick,rumor_replace,value,word,fallback,trait,repeated_trait,phrase,procedure,array_reference };
    unsigned pending_procedure_list_=0,pending_procedure_arguments_=0;
    RandomUse random_use_=RandomUse::value;
    void StepAnswer(std::uint32_t buttons,std::uint32_t edges);
    bool HasCodeAt(unsigned pc) const;
    unsigned Read(unsigned n);
    Value Pop();
    Value Dereference(Value) const;
    void Assign(const Value& ref,Value value);
    int ApplyArrayRecords(bool filtered);
    void Execute(recovered::RngNativeOwner* rng);
    void StepRoom(std::uint32_t buttons,std::uint32_t edges,bool actor_ready,
                  const recovered::PackageView* font,bool prompt_expired,
                  std::optional<bool> invitation_event,recovered::RngNativeOwner* rng,bool secondary_ready);
    void EnterProcedure(unsigned target,std::vector<Value> args,bool indirect,bool preserve_result);
    void Call(unsigned target,std::vector<Value> args,bool indirect,bool preserve_result=false);
    void ReturnFromGreeting();
    struct NamedSchedule {
        unsigned table, wrapper, contact_cleanup;
        unsigned event_cell, location_cell, motion_cell, midnight_global;
        std::array<unsigned,5> callbacks; // Calendar events 2–6 in source order.
        unsigned farewell, utility_save, reset, diary_count, contacts_global;
    };
    struct GreetingScript {
        const char* filename;
        unsigned entry, farewell, flags, gift, gift_open, inventory, modal_wait, skip_farewell, diary, diary_resume,
                 dictionary;
        NamedSchedule schedule;
    };
    const GreetingScript* alternate_greeting_=nullptr;
    std::array<unsigned,5> named_scheduled_diary_offsets_{};
    std::map<unsigned,unsigned> speech_suffixes_;
    unsigned ContactGlobal(unsigned bank) const;
    bool LoadNamedGreeting(const std::filesystem::path& dir,const AcceptedProfile& profile,std::string& error);
    bool robo_greeting_=false;
    unsigned actor_number_=0;
    bool robo_contact_greeting_=false, robo_contact_greeting_return_=false;
    void StartTopic(unsigned pc);
    void AdvanceBalloon(const recovered::PackageView& font);
    void AppendUnit(std::string_view unit,const recovered::PackageView& font);
    std::size_t BreakGroupEnd() const;
    Value ReadText();
    void Message();
};
}
