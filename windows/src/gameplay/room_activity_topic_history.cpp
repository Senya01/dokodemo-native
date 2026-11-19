#include "room_activity_event.h"
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {

void RoomActivityEvent::PrepareTopicHistoryForSave() {
    // 1D62 only sets bits. Previously saved removals survive subsequent saves.
    for (size_t i=0; i<rows_.size(); ++i) {
        if (!rows_[i].present) saved_topic_rows_.at(i/28) |= 0x20000000u | (1u << (i%28));
    }
}

KscSaveValue RoomActivityEvent::TopicHistorySaveTable() const {
    KscSaveValue body{0x90000005u,{},{}};
    for (auto word:saved_topic_rows_) body.children.push_back({word,{},{}});
    return {0xb0000000u,{}, {std::move(body)}};
}

void RoomActivityEvent::RestoreTopicHistory(const KscSaveValue& table) {
    if (table.word!=0xb0000000u || !table.text.empty() || table.children.size()!=1)
        throw std::invalid_argument("Invalid G59D4 reference");
    const auto& body=table.children[0];
    if (body.word!=0x90000005u || !body.text.empty() || body.children.size()!=saved_topic_rows_.size())
        throw std::invalid_argument("Invalid G59D4 removal history");
    auto saved=saved_topic_rows_;
    for (size_t i=0; i<saved.size(); ++i) {
        const auto& cell=body.children[i];
        if ((cell.word && cell.word>>28!=2) || !cell.text.empty() || !cell.children.empty())
            throw std::invalid_argument("Invalid G59D4 bitset word");
        saved[i]=cell.word;
    }
    // 1D8F runs against the newly loaded G96C9. All 118 starting category words
    // are tagged integers, including index 0 before its birthday override.
    for (size_t i=0;i<rows_.size();++i)
        if (saved.at(i/28) & (1u << (i%28))) rows_[i].present=false;
    saved_topic_rows_=saved;
}
}
