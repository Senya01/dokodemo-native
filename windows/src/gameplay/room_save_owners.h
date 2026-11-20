#pragma once
#include "ksc_save_graph.h"
#include <array>

namespace dokodemo::gameplay {

class RoomActivityPhase;
class RoomActivityEvent;
class RoomInventory;
class RoomSceneRows;
class RoomTalkState;

// Common layout of the five actor serializers. Field comments use NEKO names.
struct RoomSaveOwners {
    KscSaveValue dictionary;                    // root[1], G3DD2
    KscSaveValue inventory;                     // root[2], G4FAD
    KscSaveValue scene_rows;                    // root[3], G5897
    std::array<KscSaveValue,3> contact_history; // root[12,14,15], G59A8/59AF/59B6
    std::array<KscSaveValue,2> progress_flags;  // root[6,13], G59A2/59AE
    std::array<KscSaveValue,2> day_words;       // root[7,9], G59A3/59A5
    KscSaveValue outing_counter;                // root[17], G59C2
    KscSaveValue message_counter;               // root[18], G59C7
    KscSaveValue contact_count;                 // root[19], G59CC
    KscSaveValue used_diaries;                  // root[21], G59CE
    KscSaveValue questions;                     // root[25], G58DF
    KscSaveValue diary_staging;                 // root[26], G44C8

    KscSaveValue rank_item, rumors;
    std::array<KscSaveValue,2> progression_words;  // root[8,10], G59A4/59A6
    KscSaveValue relation_group, home_counter, exchange_points;
    KscSaveValue topic_history, fallback_history, partner_history;

    KscSaveValue ToActorGraph() const;
    static RoomSaveOwners Export(const RoomTalkState& talk, const RoomSceneRows& scene, const RoomActivityPhase& day,
                                 const RoomInventory& inventory, const RoomActivityEvent& topics);
    static RoomSaveOwners FromActorGraph(const KscSaveValue& root);
    void Restore(RoomTalkState& talk, RoomSceneRows& scene, RoomActivityPhase& day, RoomInventory& inventory,
                 RoomActivityEvent& topics) const;
};

} // namespace dokodemo::gameplay
