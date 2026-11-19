#include "room_notebook.h"
#include "room_vocabulary.h"
#include <algorithm>
#include <utility>

namespace dokodemo::gameplay {
namespace {
// BOOT D9EC8..D9F3C, range endpoints used by the bounded byte comparisons.
struct Range { unsigned tab, low, high, bytes; };
constexpr Range ranges[]{
    {1,0x829f,0x82a8,2},{2,0x82a9,0x82b2,2},{3,0x82b3,0x82bc,2},
    {4,0x82bd,0x82c7,2},{5,0x82c8,0x82cc,2},{6,0x82cd,0x82db,2},
    {7,0x82dc,0x82e0,2},{8,0x82e1,0x82e6,2},{9,0x82e7,0x82eb,2},
    {10,0x82ec,0x82f1,2},{11,'A','Z',1},{11,'a','z',1},{11,0x8260,0x829a,2},
    {12,'0','9',1},{12,0x824f,0x8258,2}};

// 2F47C/2F5F4 swap equal keys from opposite ends of the partition.
template<class Before>
void SortMemo(std::vector<unsigned>& ids, int first, int last, const Before& before) {
    while(first<last) {
        int left=first, right=last;
        const unsigned pivot=ids[(first+last)/2];
        for(;;) {
            while(before(ids[left],pivot)) ++left;
            while(before(pivot,ids[right])) --right;
            if(left>=right) break;
            std::swap(ids[left++],ids[right--]);
        }
        SortMemo(ids,first,left-1,before);
        first=right+1;
    }
}
}

void RoomNotebook::Open(const RoomVocabulary& vocabulary,std::uint32_t held) {
    if(visible()) return;
    ReadWords(vocabulary);
    tab_=1; action_=frame_=boundary_=0;
    previous_=held; held_.fill(0);
    Filter();
    phase_=Phase::opening;
    effects_={12};
}

void RoomNotebook::ReadWords(const RoomVocabulary& vocabulary) {
    words_.clear();
    for(unsigned i=0;i<vocabulary.row_count();++i) {
        const int flags=vocabulary.FieldInt(i,0);
        if(!(flags&0x60) || (flags&0x10)) continue;
        words_.push_back({i,std::string(vocabulary.FieldText(i,1)),std::string(vocabulary.FieldText(i,2)),
                          vocabulary.FieldInt(i,5),bool(vocabulary.FieldInt(i,11)&4)});
    }
}

void RoomNotebook::Filter() {
    visible_.clear();
    selected_=0;
    scroll_=target_scroll_=0;
    if(tab_==13) {
        for(unsigned i=0;i<words_.size();++i) visible_.push_back(i);
    } else for(const auto range:ranges) {
        if(range.tab!=tab_) continue;
        for(unsigned i=0;i<words_.size();++i) {
            const auto& reading=words_[i].reading;
            if(reading.empty()) continue;
            unsigned key=static_cast<unsigned char>(reading[0]);
            if(range.bytes==2) key=(key<<8)|static_cast<unsigned char>(reading[1]);
            if(key>=range.low && key<=range.high) visible_.push_back(i);
        }
    }
    SortMemo(visible_,0,int(visible_.size())-1,[&](unsigned a,unsigned b) {
        if(tab_==13) return words_[a].date>words_[b].date;
        return words_[a].reading<words_[b].reading;
    });
}

const NotebookWord* RoomNotebook::item_at(unsigned i) const {
    return i<count() ? &words_[visible_[i]] : nullptr;
}
bool RoomNotebook::actions_visible() const {
    return phase_==Phase::action_open || phase_==Phase::actions || phase_==Phase::action_close;
}
unsigned RoomNotebook::action_count() const {
    const auto* word=selected_item();
    return word ? (word->protected_word?2u:3u) : 0u;
}
std::vector<unsigned> RoomNotebook::TakeEffects() {
    return std::exchange(effects_,{});
}

void RoomNotebook::FinishReading(RoomVocabulary& vocabulary,std::optional<std::string> returned_written,
                                 std::uint32_t held_buttons) {
    if(!reading_edit_active()) return;
    if(returned_written && !returned_written->empty()) {
        auto& word=words_[visible_[selected_]];
        vocabulary.ChangeMemoReading(word.row,*returned_written);
        word.reading=std::move(*returned_written);
    }
    // 2E75C clears the modal owner. 2DE60 rebuilds the list only on a tab change.
    previous_=held_buttons; held_.fill(0);
    action_=frame_=0; phase_=Phase::list;
}

void RoomNotebook::FinishDeletion(RoomVocabulary& vocabulary) {
    if(!deletion_pending()) return;
    const unsigned row=selected_item()->row;
    if(row>=600) {
        // 0DBC has an additional cleanup path for extended relationship rows.
        boundary_=0x2e954;
        return;
    }
    phase_=Phase::list;
    if(!(vocabulary.FieldInt(row,0)&0x60)) return;
    vocabulary.ClearMemoWord(row);
    // 2E990 invalidates the tab cache. 2DE60 rebuilds and only clamps a
    // cursor that is now past the end; freshly entering a tab resets it.
    const unsigned cursor=selected_;
    const float scroll=scroll_, target=target_scroll_;
    ReadWords(vocabulary);
    Filter();
    selected_=count()?std::min(cursor,count()-1):0;
    scroll_=scroll;
    target_scroll_=(count()>=6 && cursor>=count())?float((count()-6)*22):target;
    frame_=action_=0;
}

void RoomNotebook::Step(std::uint32_t buttons,bool fade_complete) {
    if(phase_ != Phase::list) scroll_+=(target_scroll_-scroll_)*.125f;
    const unsigned edge=buttons&~previous_;
    previous_=buttons;
    constexpr unsigned down=0x40,up=0x10,right=0x20,left=0x80,ok=0x2000,back=0x4000;
    constexpr unsigned dirs[]{down,up,right,left};
    std::array<bool,4> move{};
    for(unsigned i=0;i<4;++i) {
        auto& ticks=held_[i];
        if(!(buttons&dirs[i])) { ticks=0; continue; }
        move[i]=(edge&dirs[i]) || (ticks>=10 && !(ticks%2));
        ticks=ticks<12?ticks+1:11;
    }
    switch(phase_) {
    case Phase::closed:
    case Phase::delete_word:
    case Phase::reading_edit: return;
    case Phase::opening:
        if(fade_complete) phase_=Phase::list;
        return;
    case Phase::closing:
        if(fade_complete) phase_=Phase::closed;
        return;
    case Phase::action_open:
        if(++frame_>=10) phase_=Phase::actions;
        return;
    case Phase::action_close:
        if(++frame_>=10) {
            if(action_==0) phase_=Phase::reading_edit;
            else if(action_==1 && !selected_item()->protected_word) {
                question_.Open(buttons); phase_=Phase::confirm_delete;
            } else phase_=Phase::list;
        }
        return;
    case Phase::confirm_delete:
        question_.Step(buttons);
        for(unsigned cue:{4u,6u,10u,11u})
            if(question_.se_mask()&(1u<<cue)) effects_.push_back(cue);
        if(question_.result()) {
            if(*question_.result()==1) {
                phase_=Phase::delete_word;
            } else {
                action_=1; frame_=0; phase_=Phase::action_open; effects_.push_back(10);
            }
        }
        return;
    case Phase::actions:
        if(move[0] && (action_+1<action_count() || (edge&down))) {
            action_=(action_+1)%action_count(); effects_.push_back(6);
        } else if(move[1] && (action_ || (edge&up))) {
            action_=(action_+action_count()-1)%action_count(); effects_.push_back(6);
        } else if(edge&ok) {
            effects_.push_back(4); effects_.push_back(11);
            phase_=Phase::action_close; frame_=0;
        }
        return;
    case Phase::list: break;
    }
    if(move[0] || move[1]) {
        const bool forward=move[0];
        const bool wrap=edge&(forward?down:up);
        if(count() && (wrap || (forward?selected_+1<count():selected_>0))) {
            selected_=forward?(selected_+1)%count():(selected_+count()-1)%count();
            const float top=22.f*selected_;
            target_scroll_=std::clamp(target_scroll_,std::max(0.f,top+22.f-132.f),top);
            effects_.push_back(6);
        }
    } else if(move[2] || move[3]) {
        const bool forward=move[2];
        const bool wrap=edge&(forward?right:left);
        if(wrap || (forward?tab_<13:tab_>1)) {
            tab_=forward?tab_%13+1:(tab_==1?13:tab_-1);
            Filter(); effects_.push_back(6);
        }
    } else if(edge&ok) {
        if(count()) {
            phase_=Phase::action_open; frame_=action_=0;
            effects_.push_back(10); effects_.push_back(4);
        }
    } else if(edge&back) {
        phase_=Phase::closing; effects_.push_back(5); effects_.push_back(12);
    }
    scroll_+=(target_scroll_-scroll_)*.125f;
}
}
