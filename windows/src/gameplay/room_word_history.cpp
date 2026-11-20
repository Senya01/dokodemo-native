#include "room_word_history.h"
#include <algorithm>

namespace dokodemo::gameplay {
namespace {
// NJDC 80020000 initialized by 989E8: 961 slots of 32 bytes with a
// five-byte stem header. Every candidate in the one-key projection fits one.
constexpr std::size_t learning_slots=961, registration_slots=100;
bool Same(const RoomNjCandidate& a,const RoomNjCandidate& b) {
    return a.written==b.written && a.reading==b.reading;
}

std::optional<RoomNjCandidate> JoinedPrefix(const RoomNjCandidate& head,std::string_view query,
                                             const RoomNjSuffix& suffix) {
    if(head.reading!=query.substr(0,2)) return {};
    const auto end=suffix.resulting_class.find(head.right_pos);
    if(end==suffix.resulting_class.end()) return {};
    return RoomNjCandidate{std::string(query),head.written+std::string(query.substr(2)),head.left_pos,end->second};
}

std::optional<RoomNjCandidate> FirstResult(std::string_view query,const RoomNjSuffix& suffix,
        const std::vector<RoomNjCandidate>& learned,const std::vector<RoomNjCandidate>& registered) {
    if(suffix.whole_slots.empty() && suffix.prefix_slots.empty()) return {};
    // A270C..A2718 examines at most five dictionary rows per reading
    // length. Duplicate spellings and disconnected stems occupy slots.
    constexpr unsigned slot_limit=5;
    unsigned prefix_left=slot_limit;
    for(const auto* bank : {&learned,&registered}) {
        for(auto it=bank->rbegin();it!=bank->rend();++it) {
            if(it->reading==query) return *it;
            if(prefix_left && it->reading==query.substr(0,2)) {
                --prefix_left;
                if(auto result=JoinedPrefix(*it,query,suffix)) return result;
            }
        }
    }
    const RoomNjFirstSlot* best=nullptr;
    auto consider=[&](const std::vector<RoomNjFirstSlot>& slots,unsigned left) {
        for(std::size_t i=0;i<std::min<std::size_t>(left,slots.size());++i) {
            const auto& slot=slots[i];
            if(slot.candidate && (!best || slot.grade>best->grade)) best=&slot;
        }
    };
    consider(suffix.whole_slots,slot_limit);
    consider(suffix.prefix_slots,prefix_left);
    if(best) return best->candidate;
    return {};
}
}
bool RoomWordHistory::Learn(const RoomNjCandidate& selected) {
    if(selected.reading.empty() || selected.written.empty() ||
       selected.reading.size()+selected.written.size()>27) return false;
    auto entry=selected;
    // 9FC44..9FD54 remap NJCON's single-kanji classes (fields 4/5)
    // to the learned noun classes (fields 10/11) from this game's NJCON.
    if(entry.left_pos==186) entry.left_pos=190;
    if(entry.right_pos==32) entry.right_pos=37;
    if(learned_.size()==learning_slots) learned_.erase(learned_.begin());
    learned_.push_back(std::move(entry));
    return true;
}

bool RoomWordHistory::Register(std::string_view reading,std::string_view written) {
    // 98D98 category 0 selects NJCON fields 6/7 (198,101). 99C94
    // rejects duplicate entries in the separate 80030000 user dictionary.
    RoomNjCandidate entry{std::string(reading),std::string(written),198,101};
    if(registered_.size()==registration_slots ||
       std::any_of(registered_.begin(),registered_.end(),[&](const auto& r){return Same(r,entry);}))
        return false;
    if(!Learn(entry)) return false;
    registered_.push_back(std::move(entry));
    return true;
}

std::vector<std::uint8_t> RoomWordHistory::Save() const {
    if(learned_.empty() && registered_.empty()) return {};
    std::vector<std::uint8_t> bytes;
    auto word=[&](std::size_t value) {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value>>8));
    };
    for(const auto* bank:{&learned_,&registered_}) {
        word(bank->size());
        for(const auto& row:*bank) {
            word(row.left_pos);
            word(row.right_pos);
            bytes.push_back(static_cast<std::uint8_t>(row.reading.size()));
            bytes.push_back(static_cast<std::uint8_t>(row.written.size()));
            bytes.insert(bytes.end(),row.reading.begin(),row.reading.end());
            bytes.insert(bytes.end(),row.written.begin(),row.written.end());
        }
    }
    return bytes;
}

bool RoomWordHistory::Restore(const std::vector<std::uint8_t>& data) {
    if(data.empty()) {
        *this={};
        return true;
    }
    const auto text_ok=[](std::string_view text) {
        for(std::size_t i=0;i<text.size();) {
            const auto lead=static_cast<unsigned char>(text[i++]);
            if(lead>=0x20 && lead<0x7f) continue;
            if(!((lead>=0x81 && lead<=0x9f) || (lead>=0xe0 && lead<=0xfc)) || i==text.size()) return false;
            const auto trail=static_cast<unsigned char>(text[i++]);
            if(trail<0x40 || trail>0xfc || trail==0x7f) return false;
        }
        return true;
    };
    RoomWordHistory restored;
    std::size_t at=0;
    auto read_bank=[&](std::vector<RoomNjCandidate>& bank,std::size_t limit) {
        if(data.size()-at<2) return false;
        const std::size_t count=data[at]+256u*data[at+1];
        if(count>limit) return false;
        at+=2;
        bank.reserve(count);
        for(std::size_t i=0;i<count;++i) {
            if(data.size()-at<6) return false;
            const unsigned left=data[at]+256u*data[at+1], right=data[at+2]+256u*data[at+3];
            const std::size_t reading=data[at+4],writing=data[at+5];
            at+=6;
            if(!left || left>511 || !right || right>511 || !reading || !writing ||
               reading+writing>27 || reading+writing>data.size()-at) return false;
            const auto* text=reinterpret_cast<const char*>(data.data()+at);
            const std::string_view r(text,reading), w(text+reading,writing);
            if(!text_ok(r) || !text_ok(w)) return false;
            bank.push_back({std::string(r),std::string(w),left,right});
            at+=reading+writing;
        }
        return true;
    };
    if(!read_bank(restored.learned_,learning_slots) || !read_bank(restored.registered_,registration_slots) ||
       at!=data.size()) return false;
    *this=std::move(restored);
    return true;
}

std::vector<RoomNjCandidate> RoomWordHistory::Compose(std::string_view query,
        const std::vector<RoomNjCandidate>& predictions,const std::vector<RoomNjCandidate>& exact,
        const RoomNjSuffix& suffix) const {
    std::vector<RoomNjCandidate> out;
    auto emit=[&](const RoomNjCandidate& row) {
        if(std::none_of(out.begin(),out.end(),[&](const auto& r){return r.written==row.written;}))
            out.push_back(row);
    };
    // 9FF74 starts with 3 current learning predictions, then 4's
    // dictionary predictions up to 9 cumulative unique choices (A0144).
    for(auto it=learned_.rbegin(); it!=learned_.rend() && out.size()<3; ++it)
        if(it->reading.compare(0,query.size(),query)==0) emit(*it);
    for(const auto& row:predictions) {
        if(out.size()>=9) break;
        emit(row);
    }
    if(auto first=FirstResult(query,suffix,learned_,registered_)) emit(*first);
    // Older complete readings and connectable first stems from a writable
    // bank compete in that bank's recency order after the prediction pass.
    auto writable = [&](const std::vector<RoomNjCandidate>& bank) {
        for (auto it = bank.rbegin(); it != bank.rend(); ++it) {
            if (it->reading == query) {
                emit(*it);
            } else if (auto joined=JoinedPrefix(*it,query,suffix)) emit(*joined);
        }
    };
    writable(learned_);
    writable(registered_);
    for(const auto& row:exact) emit(row);
    return out;
}
}
