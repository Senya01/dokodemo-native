#include "room_friends.h"
#include "room_talk_state.h"
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
unsigned CountIntroductionFriends(const RoomVocabulary& vocabulary) {
    // 16F4 places G3151's 100 references after G0018's 600 in G3DD2.
    if(vocabulary.row_count()!=700) throw std::runtime_error("Friend notebook rows unavailable");
    unsigned count=0;
    for(unsigned row=600;row<700;++row) {
        if(vocabulary.HasField(row,1) && !(vocabulary.FieldInt(row,0)&2)) ++count;
    }
    return count;
}

bool RoomTalkState::BeginFriendsIntroduction() {
    if(phase_!=Phase::done || ordinary_ || !code_.count(0x8ddb5) || !code_.count(0x10c9b)) return false;
    // The room owns 8D8CB. Its caller's argument-2 case resumes in 8DF4B.
    pc_=0x8df4b; phase_=Phase::running; indirect_topic_=0x10c9b;
    ordinary_=friends_introduction_=true; ordinary_return_=ordinary_rejected_=false;
    frontier_.reset(); friend_timer_requested_=false; counted_contact_=false;
    if(!friends_cells_.count(0x2a3c)) {
        // A718 changes the operand of CT2A3C's field-0 contains-all filter.
        auto filter=std::make_shared<std::vector<Value>>(
            std::initializer_list<Value>{{Kind::integer,0},{Kind::integer,6},{}});
        friends_cells_[0x2a3c]={Kind::array,0,{},0,std::move(filter)};
        // A769 replaces four operands in this five-triple comparison.
        auto match=std::make_shared<std::vector<Value>>(15);
        friends_cells_[0x2a44]={Kind::array,0,{},0,std::move(match)};
    }
    last_answer_=-1; delay_=0; result_={}; locals_.clear(); stack_.clear(); callers_.clear();
    farewell_=actor_wait_=message_wait_=false;
    return true;
}

bool RoomTalkState::BeginFriendsExchange(unsigned argument,const RoomInventory& inventory) {
    if(phase_!=Phase::done || ordinary_ || argument<7 || argument>9 ||
       !code_.count(0x8d028) || !global_arrays_.count(0x9a26)) return false;
    constexpr unsigned arms[]={0x8dfc0,0x8dfe2,0x8e004};
    constexpr unsigned entries[]={0x8b0bd,0x8b10b,0x8b0e2};
    pc_=arms[argument-7]; indirect_topic_=entries[argument-7]; phase_=Phase::running;
    ordinary_=true; ordinary_return_=ordinary_rejected_=false;
    exchange_inventory_=&inventory; counted_contact_=friend_timer_requested_=false;
    meeting_event_depth_=0;
    // G9A35 holds ten independently mutable two-cell records in this KSC instance.
    if(!global_arrays_.count(0x9a35)) {
        auto& roster=global_arrays_[0x9a35];
        for(unsigned i=0;i<10;++i)
            roster.push_back({Kind::array,0,{},0,std::make_shared<std::vector<Value>>(2)});
    }
    frontier_.reset(); last_answer_=-1; delay_=0; result_={};
    locals_.clear(); stack_.clear(); callers_.clear();
    // The prior completed contact clears CT2873 at 89A67.
    farewell_=skip_farewell_=actor_wait_=message_wait_=false;
    return true;
}

void RoomTalkState::FinishPeerMeeting(bool completed) {
    if (phase_!=Phase::meeting || !meeting_request_ || !exchange_inventory_ || pc_!=0x8d0b8 ||
        blocks_.empty() || blocks_.back().close!=0x8d19e)
        throw std::logic_error("No active meeting script");
    meeting_request_.reset();
    phase_=Phase::running;
    if (completed) {
        // The native connection owns the exchange. Rejoin the common
        // protected-block close and normal room/contact cleanup.
        pc_=0x8d1f0;
    } else {
        Call(0x8d025,{},false); // Retain the original cancellation unwind and farewell.
    }
}

std::optional<bool> RoomTalkState::TakeFriendsBanner() {
    return std::exchange(friends_banner_,{});
}
}
