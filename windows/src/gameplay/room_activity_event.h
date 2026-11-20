#pragma once
#include "room_pending_topics.h"
#include "room_messages.h"
#include "ksc_save_graph.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dokodemo::recovered { class RngNativeOwner; }
namespace dokodemo::gameplay {

// Current script descriptor CT243F[0], CT243F[1][0], and the transfer
// timestamp written to CT3DE7. The scene manager owns the loaded selector.
struct RoomContactLocation {
    unsigned scene=0, descriptor_id=0, kind=0;
    std::optional<std::uint32_t> transfer_minute;
    std::function<void(std::uint32_t)> stamp_transfer_minute;
    std::function<unsigned(unsigned,recovered::RngNativeOwner&)> select_home_offer;
    std::function<std::uint32_t()> current_minute;
    std::function<unsigned()> current_destination_id;
};

struct RoomEventSelection {
    std::optional<unsigned> topic_pc;
    unsigned boundary_pc = 0;
    std::optional<unsigned> selected_row;
    std::vector<unsigned> eligible_rows;
    bool finished = false;
    bool greeting = false;
    bool queued = false;
};

// Builtin 60 scans a script-owned callback table. The caller owns which
// category to test and what to do with the selected callback.
class RoomTopicTable {
  public:
    enum class Actor : unsigned { toro, robo, usagi, inu, kaeru };
    bool Load(const std::filesystem::path& directory, Actor actor,
              std::uint32_t packed_birthday, std::string& error);
    RoomEventSelection Select(unsigned key, bool mask, recovered::RngNativeOwner& rng);
    void RecordConsumedRows(std::array<std::uint32_t,5>& words) const;
    void RemoveConsumedRows(const std::array<std::uint32_t,5>& words);
  private:
    struct Row { unsigned category; std::vector<unsigned> entries; bool present = true; };
    std::vector<Row> rows_;
    unsigned capacity_ = 0;
};

// Room conversations in a new Toro game. The main flow owns admission and the
// camera, and the topic owner supplies the actual callback and farewell returns.
class RoomActivityEvent {
  public:
    bool Load(const std::filesystem::path& post_profile, std::uint32_t packed_birthday, std::string& error,
              RoomTopicTable::Actor actor=RoomTopicTable::Actor::toro);
    RoomEventSelection SelectCatalogTopic(unsigned key, bool mask, recovered::RngNativeOwner& rng);
    void CountContact();
    KscSaveValue ContactCountSaveWord() const;
    unsigned total_contacts() const { return total_contacts_; }
    static void ValidateContactCount(const KscSaveValue& word);
    void RestoreContactCount(const KscSaveValue& word);
    KscSaveValue HomeCounterSaveTable() const;
    static void ValidateHomeCounter(const KscSaveValue& table);
    void RestoreHomeCounter(const KscSaveValue& table);
    void PrepareTopicHistoryForSave();
    KscSaveValue TopicHistorySaveTable() const;
    void RestoreTopicHistory(const KscSaveValue& table);
    KscSaveValue OutingCounterSaveTable() const;
    static void ValidateOutingCounter(const KscSaveValue& table);
    void RestoreOutingCounter(const KscSaveValue& table);
    KscSaveValue MessageCounterSaveTable() const;
    static void ValidateMessageCounter(const KscSaveValue& table);
    void RestoreMessageCounter(const KscSaveValue& table);
    RoomMessages& messages() { return messages_; }
    const RoomMessages& messages() const { return messages_; }
    void ResetMessageDay(std::uint32_t minute);
    void StartActorScript(std::uint32_t minute);
    void ResumeDay(std::uint32_t registered_minute);
    void FinishIntroduction(std::uint32_t minute);
    unsigned last_location_check() const { return location_check_; }
    void Enqueue(std::optional<unsigned> current_topic) { pending_callbacks_.Enqueue(current_topic); }
    void HomeInvitationReturned();
    unsigned FinishConversation(std::uint32_t minute, bool count_contact = true);
    unsigned FinishScheduledEvent(std::uint32_t minute, bool count_contact);
    RoomEventSelection SelectConversation(unsigned month, unsigned day, std::uint32_t minute,
                                               recovered::RngNativeOwner& rng,
                                               const std::function<std::optional<bool>()>& first_contact_special = {},
                                               const std::function<void()>& begin_vocabulary = {},
                                               RoomContactLocation location = {});
    RoomEventSelection SelectTeaching(std::uint32_t minute, const std::function<void()>& begin_vocabulary = {});
    RoomEventSelection SelectFriendsAction(unsigned argument);
    RoomEventSelection SelectOuting(bool return_home = false);
    RoomEventSelection ResumeAfterGreeting(unsigned month, unsigned day, std::uint32_t minute,
                                          recovered::RngNativeOwner& rng,
                                          const std::function<std::optional<bool>()>& first_contact_special = {},
                                          const std::function<void()>& begin_vocabulary = {});
    RoomEventSelection ResumeAfterRejectedTopic(unsigned returned_topic, recovered::RngNativeOwner& rng,
                                               const std::function<std::optional<bool>()>& first_contact_special = {});
    // 89A6E may select a word-dependent handler. Resume only after its actual
    // false return. A topic or a missing dictionary input remains with its owner.
    RoomEventSelection ResumeAfterEmptyWordLookup(recovered::RngNativeOwner& rng);
  private:
    enum class Stage { unloaded, introduction, idle, greeting, queued, word, calendar, day, fallback, terminal, direct_menu };
    Stage stage_ = Stage::unloaded;
    RoomTopicTable topic_table_;
    RoomPendingTopics pending_callbacks_;
    std::optional<unsigned> pending_topic_;
    std::uint32_t cleanup_minute_ = 0, minute_ = 0, first_day_ = 0, hour_topic_after_ = 0;
    RoomContactLocation location_;
    std::array<std::uint32_t,5> saved_topic_rows_{};
    std::optional<std::uint32_t> home_timer_start_, outing_timer_start_;
    unsigned home_count_=0, home_interval_=10;
    unsigned outing_count_=0,outing_interval_=30,location_check_=0;
    RoomMessages messages_;
    std::optional<std::uint32_t> message_timer_start_;
    unsigned counted_contacts_ = 0; // current G59C7 trigger count
    unsigned total_contacts_ = 0;   // G59CC, accumulated across timer resets
    unsigned CompleteContact(std::uint32_t minute, bool count_contact);
    std::optional<unsigned> home_offer_mode_;
    RoomEventSelection CheckHomeOffers(recovered::RngNativeOwner& rng);
    std::uint32_t CurrentMinute() const;
    bool teaching_ = false, hourly_topic_ = false, awaiting_cleanup_ = false;
    unsigned month_ = 0, day_ = 0;
    RoomEventSelection Continue(recovered::RngNativeOwner& rng);
    RoomEventSelection AfterQueue(recovered::RngNativeOwner& rng,
                                 const std::function<std::optional<bool>()>& first_contact_special);
    std::optional<RoomEventSelection> Begin(std::uint32_t minute, bool teaching);
    RoomEventSelection Dispatch(unsigned month, unsigned day, std::uint32_t minute,
                                recovered::RngNativeOwner& rng,
                                const std::function<std::optional<bool>()>& first_contact_special,
                                const std::function<void()>& begin_vocabulary);
    RoomEventSelection Teach(const std::function<void()>& begin_vocabulary);
    RoomEventSelection Choose(unsigned key, bool mask, recovered::RngNativeOwner& rng);
};
}
