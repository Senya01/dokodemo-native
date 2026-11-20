#pragma once

#include "room_meeting_channel.h"
#include <array>
#include <string>

namespace dokodemo::gameplay {

class RoomVocabulary;
enum class ShiritoriState { waiting, playing, won, lost, draw, failed };

class RoomShiritori {
public:
    struct Word {
        std::string text, reading;
        std::array<unsigned,2> kana{};
        unsigned row=0;
    };
    RoomShiritori(std::unique_ptr<RoomMeetingChannel> channel,const RoomVocabulary& vocabulary);
    ShiritoriState Poll();
    ShiritoriState state() const { return state_; }
    unsigned turns() const { return unsigned(history_.size()); }
    const Word& last_word() const { return history_.back(); }
    bool last_word_local() const { return last_local_; }
    const std::vector<unsigned>& used_rows() const { return used_rows_; }
private:
    bool Eligible(const Word& word) const;
    void Accept(Word word,bool local);
    std::unique_ptr<RoomMeetingChannel> channel_;
    std::vector<Word> candidates_, history_;
    std::vector<unsigned> used_rows_;
    ShiritoriState state_=ShiritoriState::waiting;
    unsigned delay_=36;
    bool turn_=false, last_local_=false, passed_=false, peer_passed_=false, acknowledged_=false;
};

} // namespace dokodemo::gameplay
