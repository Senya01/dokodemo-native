#include "gameplay/room_friends.h"
#include "gameplay/room_talk_state.h"
#include "recovered/package_storage_native.h"

#include <iostream>
#include <map>
#include <stdexcept>

using namespace dokodemo;
namespace {
void Check(bool value,const char* reason) {
    if(!value) throw std::runtime_error(reason);
}

#include "room_friends_custom_question_test.inc"

void PreparedBirthday(gameplay::RoomTalkState& talk,const recovered::PackageView& font) {
    struct Choice { unsigned rows,cursor; std::string first; };
    const std::string offer="\x82\xb2\x8f\xd0\x89\xee \x8e\x6e\x82\xdf\x82\xe9";
    const std::string recipient="\x8f\x89\x82\xdf\x82\xc4\x82\xcc\x90\x6c\x82\xd6";
    const std::string finish="\x8e\xbf\x96\xe2\x82\xcc\x97\x70\x88\xd3\x82\xa8\x82\xed\x82\xe8";
    const std::string birthday="\x92\x61\x90\xb6\x93\xfa";
    const std::string keep=birthday+"\x82\xf0\x95\xb7\x82\xb1\x82\xa4\x81\x49";
    for(unsigned visit=0;visit<3;++visit) {
        // First prepare a birthday, then retain it on reopening, then clear it.
        std::vector<Choice> menu{{3,1,offer},{2,0,recipient}};
        if(visit) menu.push_back({2,visit==1?0u:1u,keep});
        if(visit!=1) menu.push_back({5,visit==0?1u:0u,finish});
        menu.push_back({3,2,offer});
        talk.SetCalendarMinute(400+visit);
        Check(talk.BeginFriendsIntroduction(),"Could not reopen local introduction");
        std::size_t asked=0;
        unsigned last=0,draws=0;
        bool acknowledged=false,discarded=false,existing=false,pressed_before_menu=false,clean_reply=false;
        for(unsigned i=0;i<12000 && !talk.finished();++i) {
            if(auto count=talk.random_requested()) {
                Check(visit && *count==1,"Unexpected random question selection");
                talk.SupplyRandom(0); ++draws;
            }
            unsigned input=0;
            if(!visit && !pressed_before_menu && talk.message_pc()==0xca61 && talk.visible() &&
               talk.answer_options().size()==5 && !talk.answer_visible()) {
                Check(talk.busy_for_room_input(),"Pending question released room input");
                input=0x2040; // A fresh Circle + Down during the preceding balloon.
                pressed_before_menu=true;
            }
            if(talk.answer_visible()) Check(!talk.visible(),"ASK overlapped its own actor's previous message");
            if(talk.answer_interactive() && !last) {
                Check(asked<menu.size(),"Unexpected question after introduction");
                const auto& choice=menu.at(asked);
                const auto& labels=talk.answer_options();
                Check(labels.size()==choice.rows && labels.front()==choice.first,"Wrong prepared question options");
                if(choice.rows==5) Check(labels.at(1)==birthday,"Birthday missing from first-meeting choices");
                if(talk.answer_cursor()==choice.cursor) { input=0x2000; ++asked; }
                else input=0x40;
            }
            last=input;
            talk.Step(input,true,true,&font);
            if(talk.visible()) {
                existing|=talk.message_pc()==0xca79;
                acknowledged|=talk.message_pc()==0xc9f3;
                discarded|=talk.message_pc()==0xca0f;
                if(!visit && talk.message_pc()==0xc9f3 && talk.message_revealed()) {
                    const auto& lines=talk.lines();
                    Check(lines.size()==2 && lines.front().find(birthday)==0,
                          "Birthday reply retained the pre-ASK recipient question");
                    clean_reply=true;
                }
            }
            if(!talk.error().empty()) throw std::runtime_error(talk.error());
            if(talk.ordinary_frontier())
                throw std::runtime_error("Prepared question source frontier "+std::to_string(*talk.ordinary_frontier()));
        }
        Check(asked==menu.size() && talk.finished() && talk.ordinary_source_return(),"Incomplete prepared question return");
        if(!visit) Check(pressed_before_menu && clean_reply,"Missing balloon-to-ASK transition or birthday acknowledgement");
        Check(existing==(visit!=0) && draws==unsigned(visit!=0) && acknowledged==(visit<2) && discarded==(visit==2),
              "Prepared question took the wrong confirmation branch");
        const auto& vocab=talk.vocabulary();
        for(unsigned row=0;row<vocab.question_slots;++row) {
            for(unsigned field=0;field<7;++field) {
                unsigned expected=0;
                if(row==0 && visit<2) {
                    if(field==0) expected=0x2020040a;
                    if(field==2) expected=0x30000000u+400+visit;
                }
                Check(vocab.QuestionField(row,field)==expected,"Wrong question row, flags, timestamp or discarded field");
            }
        }
        Check(!gameplay::CountIntroductionFriends(vocab),"Prepared question created a friend");
    }
}

void Cancel(gameplay::RoomTalkState& talk,const recovered::PackageView& font,bool full,bool proceed) {
    Check(talk.BeginFriendsIntroduction(),"Could not start introduction from the room");
    Check(!talk.BeginFriendsIntroduction(),"Re-entered the pending introduction");
    std::vector<bool> transitions;
    bool confirm=false,offer=false,actor_close=false;
    unsigned last=0;
    for(unsigned tick=0;tick<10000 && !talk.finished();++tick) {
        unsigned input=0;
        if(talk.answer_interactive()) {
            const auto n=talk.answer_options().size();
            Check(n==3 || (full && n==2),"Unexpected introduction ASK");
            confirm|=n==2; offer|=n==3;
            const unsigned target=n==3?2u:(proceed?0u:1u);
            if(!last) input=talk.answer_cursor()==target?0x2000:0x40;
        }
        last=input;
        talk.Step(input,true,true,&font);
        if(auto banner=talk.TakeFriendsBanner()) transitions.push_back(*banner);
        actor_close|=talk.actor_command()==74;
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        Check(!talk.ordinary_frontier(),"Unexpected source frontier cancelling introduction");
    }
    Check(transitions==std::vector<bool>({true,false}),"Wrong SYOUKAI open/close sequence");
    Check(talk.finished() && talk.ordinary_source_return() && actor_close,"Missing local cleanup");
    Check(confirm==full && offer==(!full || proceed),"Wrong notebook capacity branches");
    Check(talk.last_answer()==(offer?3:2),"Wrong one-based introduction answer");
    Check(!talk.counted_contact(),"Cancelled introduction counted as a conversation");
}
}

void CheckFriendsIntroduction(const std::filesystem::path& media, const recovered::PackageView& font) {
        gameplay::AcceptedProfile profile{};
        profile.nickname.Assign("Toro"); profile.player.Assign("Nana");
        gameplay::RoomTalkState talk;
        std::string error;
        if(!talk.Load(media,profile,error)) throw std::runtime_error(error);
        Check(!talk.BeginFriendsIntroduction(),"Missing room readiness gate");
        talk.Step(0,true,true,&font,true);
        for(unsigned tick=0;tick<8000 && !talk.finished();++tick) {
            if(talk.gift_requested()) talk.SupplyGift(-1);
            if(auto count=talk.random_requested()) talk.SupplyRandom(0);
            talk.Step(0,true,true,&font);
            if(!talk.error().empty()) throw std::runtime_error(talk.error());
        }
        auto& words=talk.vocabulary();
        Check(gameplay::CountIntroductionFriends(words)==0,"Player was counted in friend notebook");
        Cancel(talk,font,false,false);

        PreparedBirthday(talk,font);
        PreparedCustomQuestion(talk,font);

        // Explicit component data for the boundary, without an exchange or a save.
        for(unsigned i=601;i<700;++i) words.WriteText(i,1,"Card");
        words.WriteNumber(699,0,2);
        Check(gameplay::CountIntroductionFriends(words)==98,"CT2B01 failed to exclude mask 2");
        Cancel(talk,font,false,false);
        words.WriteNumber(699,0,0);
        Check(gameplay::CountIntroductionFriends(words)==99,"CT2B01 full notebook boundary");
        Cancel(talk,font,true,false);
        Cancel(talk,font,true,true);
        std::cout<<"Friends introduction, notebook predicate, ASK cancellation and room cleanup passed\n";
}
