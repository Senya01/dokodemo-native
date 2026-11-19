#include "room_save_owners.h"
#include "room_activity_phase.h"
#include "room_activity_event.h"
#include "room_inventory.h"
#include "room_scene_rows.h"
#include "room_talk_state.h"
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace dokodemo::gameplay {
namespace {
// 8D1FD stores references to globals even when a global contains a single word.
using WordPair = std::array<KscSaveValue,2>;

WordPair GlobalRefs(const WordPair& words) {
    return {KscSaveValue{0xb0000000u, {}, {words[0]}},
            KscSaveValue{0xb0000000u, {}, {words[1]}}};
}

WordPair ReferencedWords(const WordPair& refs) {
    WordPair words;
    for (unsigned i = 0; i < refs.size(); ++i) {
        const auto& ref = refs[i];
        if (ref.word != 0xb0000000u || !ref.text.empty() || ref.children.size() != 1)
            throw std::invalid_argument("Expected an actor global reference");
        words[i] = ref.children.front();
    }
    return words;
}
} // namespace

RoomSaveOwners RoomSaveOwners::Export(const RoomTalkState& talk, const RoomSceneRows& scene, const RoomActivityPhase& day,
                                     const RoomInventory& inventory, const RoomActivityEvent& topics) {
    const auto& words = talk.vocabulary();
    return {words.DictionarySaveTable(), inventory.SaveTable(), scene.SaveTable(), talk.ContactHistoryTables(),
            GlobalRefs(talk.ProgressFlagWords()), GlobalRefs(day.DaySaveWords()), topics.OutingCounterSaveTable(),
            topics.MessageCounterSaveTable(), topics.ContactCountSaveWord(), topics.messages().UsedDescriptorsSaveTable(), words.QuestionSaveTable(),
            topics.messages().SaveTable(), scene.exchange().ItemSaveTable(), talk.rumors().SaveTable(),
            GlobalRefs(day.ProgressionSaveWords()), words.RelationGroupCounter(), topics.HomeCounterSaveTable(),
            scene.exchange().PointsSaveWord(), topics.TopicHistorySaveTable(), talk.fallback_list().SaveTable(),
            talk.fallback_list().PartnerSaveTable()};
}

KscSaveValue RoomSaveOwners::ToActorGraph() const {
    // 8D214 builds the fixed scenario version with builtin9(2004,11,16,0,0).
    return {0x9000001bu,{},{
        {0x30272220u,{},{}}, dictionary, inventory, scene_rows, rank_item, rumors,
        progress_flags[0], day_words[0], progression_words[0], day_words[1], progression_words[1],
        relation_group, contact_history[0], progress_flags[1], contact_history[1], contact_history[2],
        home_counter, outing_counter, message_counter, contact_count, exchange_points, used_diaries,
        topic_history, fallback_history, partner_history, questions, diary_staging
    }};
}

RoomSaveOwners RoomSaveOwners::FromActorGraph(const KscSaveValue& root) {
    if (root.word >> 28 != 9 || (root.word & 0x3fff) != 27 || root.children.size() != 27)
        throw std::invalid_argument("Expected the 27-field actor save graph");
    const auto& cells = root.children;
    if (cells[0].word!=0x30272220u || !cells[0].text.empty() || !cells[0].children.empty())
        throw std::invalid_argument("Unsupported actor graph version");
    return {cells[1], cells[2], cells[3], {cells[12], cells[14], cells[15]}, {cells[6], cells[13]},
            {cells[7], cells[9]}, cells[17], cells[18], cells[19], cells[21], cells[25], cells[26], cells[4], cells[5], {cells[8],cells[10]},
            cells[11], cells[16], cells[20], cells[22], cells[23], cells[24]};
}

void RoomSaveOwners::Restore(RoomTalkState& talk, RoomSceneRows& scene, RoomActivityPhase& day, RoomInventory& live_inventory,
                             RoomActivityEvent& topics) const {
    auto staged_topics = topics;
    staged_topics.RestoreHomeCounter(home_counter);
    staged_topics.RestoreOutingCounter(outing_counter);
    staged_topics.RestoreContactCount(contact_count);
    staged_topics.RestoreMessageCounter(message_counter);
    staged_topics.RestoreTopicHistory(topic_history);
    staged_topics.messages().RestoreSaveTable(diary_staging);
    staged_topics.messages().RestoreUsedDescriptors(used_diaries);
    auto staged_rumors = talk.rumors();
    staged_rumors.RestoreSaveTable(rumors);
    auto staged_fallback = talk.fallback_list();
    staged_fallback.RestoreSaveTable(fallback_history);
    staged_fallback.RestorePartnerSaveTable(partner_history);
    auto staged_inventory = live_inventory;
    staged_inventory.RestoreSaveTable(inventory);
    auto staged_day = day;
    staged_day.RestoreDaySaveWords(ReferencedWords(day_words));
    staged_day.RestoreProgressionSaveWords(ReferencedWords(progression_words));
    auto staged_scene = scene;
    staged_scene.exchange().Restore(rank_item,exchange_points);
    staged_scene.RestoreSaveTable(scene_rows);
    auto staged_words = talk.vocabulary().WithSaveTables(dictionary, questions);
    staged_words.RestoreRelationGroupCounter(relation_group);
    // The script owner validates all its fields before committing any changes.
    talk.RestoreContactHistory(contact_history, ReferencedWords(progress_flags));
    static_assert(std::is_nothrow_move_assignable_v<RoomVocabulary>);
    static_assert(std::is_nothrow_move_assignable_v<RoomSceneRows>);
    talk.vocabulary() = std::move(staged_words);
    scene = std::move(staged_scene);
    static_assert(std::is_nothrow_move_assignable_v<RoomActivityPhase>);
    day = std::move(staged_day);
    static_assert(std::is_nothrow_move_assignable_v<RoomInventory>);
    live_inventory = std::move(staged_inventory);
    static_assert(std::is_nothrow_move_assignable_v<RoomActivityEvent>);
    static_assert(std::is_nothrow_move_assignable_v<RoomRumors>);
    static_assert(std::is_nothrow_move_assignable_v<RoomFallbackList>);
    topics = std::move(staged_topics);
    talk.rumors() = std::move(staged_rumors);
    talk.fallback_list() = std::move(staged_fallback);
}

} // namespace dokodemo::gameplay
