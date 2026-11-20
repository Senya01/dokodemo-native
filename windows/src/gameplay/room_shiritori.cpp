#include "room_shiritori.h"
#include "room_learned_word.h"
#include "room_vocabulary.h"
#include "recovered/game_primitives.h"
#include <algorithm>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr std::uint8_t play=1, pass=2, pass_received=3;
constexpr unsigned max_turns=30, word_ticks=36;

bool ValidText(std::string_view text) {
    if (text.empty() || text.size()>60) return false;
    for (std::size_t cursor=0;cursor<text.size();) {
        recovered::Glyph glyph;
        if (!recovered::read_glyph(reinterpret_cast<const std::uint8_t*>(text.data()+cursor),
                text.size()-cursor,glyph) || glyph.index<32 || glyph.index>=8224) return false;
        cursor+=glyph.consumed;
    }
    return true;
}

std::optional<RoomShiritori::Word> ReadWord(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size()<3 || bytes.front()!=play || !bytes[1] || bytes[1]>=bytes.size()-2) return {};
    const auto* text=reinterpret_cast<const char*>(bytes.data()+2);
    RoomShiritori::Word word{std::string(text,bytes[1]),std::string(text+bytes[1],bytes.size()-2-bytes[1])};
    const auto kana=ShiritoriKana(word.reading);
    if (!kana || !ValidText(word.text)) return {};
    word.kana=*kana;
    return word;
}
} // namespace

RoomShiritori::RoomShiritori(std::unique_ptr<RoomMeetingChannel> channel,const RoomVocabulary& vocabulary)
    : channel_(std::move(channel)) {
    for (unsigned row=0;row<vocabulary.row_count();++row) {
        if (!(vocabulary.FieldInt(row,0)&0x4000) || !vocabulary.HasField(row,1)) continue;
        const auto written=vocabulary.FieldText(row,1), reading=vocabulary.FieldText(row,2);
        const auto kana=ShiritoriKana(reading);
        if (kana && ValidText(written)) candidates_.push_back({std::string(written),std::string(reading),*kana,row});
    }
    history_.reserve(max_turns);
    used_rows_.reserve(max_turns/2);
}

bool RoomShiritori::Eligible(const Word& word) const {
    if (!history_.empty() && history_.back().kana[1]!=word.kana[0]) return false;
    return std::none_of(history_.begin(),history_.end(),[&](const Word& previous) {
        return previous.reading==word.reading || previous.text==word.text;
    });
}

void RoomShiritori::Accept(Word word,bool local) {
    if (local) used_rows_.push_back(word.row);
    history_.push_back(std::move(word));
    last_local_=local;
    turn_=!local;
    delay_=word_ticks;
}

ShiritoriState RoomShiritori::Poll() {
    if (state_!=ShiritoriState::waiting && state_!=ShiritoriState::playing) return state_;
    if (!channel_) return state_=ShiritoriState::failed;
    const auto connection=channel_->Poll();
    if (connection==MeetingConnection::looking) return state_;
    if (connection!=MeetingConnection::connected) return state_=ShiritoriState::failed;
    if (state_==ShiritoriState::waiting) {
        state_=ShiritoriState::playing;
        turn_=channel_->FirstParticipant();
    }
    if (const auto payload=channel_->Receive()) {
        if (payload->size()==1 && payload->front()==pass_received && passed_)
            return state_=ShiritoriState::lost;
        if (passed_ || turn_ || peer_passed_ || turns()==max_turns) return state_=ShiritoriState::failed;
        if (payload->size()==1 && payload->front()==pass) {
            peer_passed_=true;
        } else {
            auto word=ReadWord(*payload);
            if (!word || !Eligible(*word)) return state_=ShiritoriState::failed;
            Accept(std::move(*word),false);
        }
    }
    if (peer_passed_) {
        if (!acknowledged_) acknowledged_=channel_->Send({pass_received});
        if (acknowledged_ && !channel_->HasPendingOutput()) state_=ShiritoriState::won;
        return state_;
    }
    if (turns()==max_turns) {
        if (!channel_->HasPendingOutput()) state_=ShiritoriState::draw;
        return state_;
    }
    if (!turn_ || passed_ || (delay_ && --delay_)) return state_;
    const auto next=std::find_if(candidates_.begin(),candidates_.end(),[&](const Word& word) {
        return Eligible(word);
    });
    if (next==candidates_.end()) {
        passed_=channel_->Send({pass});
        return state_;
    }
    std::vector<std::uint8_t> payload{play,std::uint8_t(next->text.size())};
    payload.insert(payload.end(),next->text.begin(),next->text.end());
    payload.insert(payload.end(),next->reading.begin(),next->reading.end());
    if (channel_->Send(payload)) Accept(*next,true);
    return state_;
}

} // namespace dokodemo::gameplay
