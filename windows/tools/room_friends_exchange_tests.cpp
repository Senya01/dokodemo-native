#include "gameplay/room_friends.h"
#include "gameplay/room_inventory.h"
#include "gameplay/room_talk_state.h"

#include <algorithm>
#include <map>
#include <stdexcept>

using namespace dokodemo;
namespace {
void Check(bool value,const char* reason) {
    if(!value) throw std::runtime_error(reason);
}

const std::vector<std::string> places{
    "\x82\xc7\x82\xf1\x82\xae\x82\xe8\x82\xcc\x96\xd8", // どんぐりの木
    "\x83\x52\x83\x58\x83\x82\x83\x58\x94\xa8",         // コスモス畑
    "\x82\xa2\x82\xbf\x82\xe5\x82\xa4\x95\xc0\x96\xd8", // いちょう並木
    "\x82\xc2\x82\xc2\x82\xb6\x82\xcc\x8b\x75",         // つつじの丘
    "\x82\xe2\x82\xc1\x82\xcf\x83\x84\x83\x81"          // やっぱヤメ
};

struct ExchangeResult {
    std::map<unsigned,std::string> messages;
    std::vector<unsigned> order;
    bool visible_mode=false,clean_location_prompt=false;
    void ExpectOrder(std::initializer_list<unsigned> sites) const {
        auto from=order.begin();
        for(unsigned site:sites) {
            from=std::find(from,order.end(),site);
            if(from==order.end()) throw std::runtime_error("Missing exchange message "+std::to_string(site));
            ++from;
        }
    }
};

const char* ActivityName(unsigned argument) {
    switch(argument) {
    case 7: return "\x82\xdf\x82\xa2\x82\xb5\x8c\xf0\x8a\xb7"; // めいし交換
    case 8: return "\x82\xb5\x82\xe8\x82\xc6\x82\xe8"; // しりとり
    case 9: return "\x83\x76\x83\x8c\x83\x5b\x83\x93\x83\x67\x8c\xf0\x8a\xb7"; // プレゼント交換
    default: throw std::runtime_error("Unknown friends action");
    }
}

ExchangeResult Exchange(gameplay::RoomTalkState& talk,const gameplay::RoomInventory& bag,
        const recovered::PackageView& font,unsigned argument,std::optional<unsigned> cursor) {
    Check(talk.BeginFriendsExchange(argument,bag),"Fresh local exchange did not start");
    Check(!talk.BeginFriendsExchange(argument,bag),"Local exchange was reentered");
    ExchangeResult result;
    unsigned last=0,confirmed=0;
    for(unsigned tick=0;tick<14000 && !talk.finished() && !talk.ordinary_frontier();++tick) {
        if(auto range=talk.random_requested()) {
            Check(*range>0,"Empty farewell draw");
            talk.SupplyRandom(0);
        }
        unsigned input=0;
        if(talk.answer_interactive()) {
            Check(cursor && talk.answer_options()==places,"Wrong meeting ASK or early refusal");
            if(!last) {
                if(talk.answer_cursor()==*cursor) { input=0x2000; ++confirmed; }
                else input=0x40;
            }
        }
        last=input;
        talk.Step(input,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(talk.visible()) {
            const auto pc=talk.message_pc();
            if(result.order.empty() || pc!=result.order.back()) result.order.push_back(pc);
            result.messages[pc]=std::string(talk.text());
            if(pc==0x89fd7 || (pc==0x89ff9 && talk.message_revealed())) {
                bool has_mode=false;
                for(const auto& line:talk.lines()) {
                    Check(line.find('\x04')==std::string::npos,"Room message displayed the control byte 04");
                    has_mode|=line.find(ActivityName(argument))!=std::string::npos;
                }
                if(pc==0x89fd7) result.visible_mode|=has_mode;
                else {
                    Check(!has_mode,"Meeting question retained text from before the 04 close");
                    result.clean_location_prompt=true;
                }
            }
        }
    }
    Check(confirmed==unsigned(cursor.has_value()),"Wrong number of confirmed meeting choices");
    if(cursor) {
        const char* mode=ActivityName(argument);
        result.ExpectOrder({0x89fd7,0x89ff9});
        Check(result.visible_mode && result.clean_location_prompt,"Missing visible offer and its 04 transition to the question");
        Check(result.messages.at(0x89fd7).find(mode)!=std::string::npos,"Wrong activity in Toro's offer");
        Check(talk.last_answer()==int(*cursor+1),"Meeting place lost its source menu index");
        if(*cursor!=4) {
            Check(talk.ordinary_frontier()==0x8d0b5 && !talk.ordinary_source_return() &&
                  !talk.finished() && !result.messages.count(0x8a0de),"Meeting-place choice fabricated a local return");
            return result;
        }
        result.ExpectOrder({0x89ff9,0x8a0de});
        Check(result.messages.at(0x8a0de).find(mode)!=std::string::npos,"Wrong activity in Toro's cancellation reply");
    } else {
        Check(!result.messages.count(0x89fd7) && !result.messages.count(0x8a0de),
              "Prerequisite refusal opened the meeting or its cancellation reply");
    }
    Check(talk.finished() && talk.ordinary_source_return() && !talk.ordinary_frontier(),"Local friends action failed to return");
    Check(talk.friends_reset_contact_timer()==cursor.has_value(),"Wrong cleanup after refusal or meeting cancellation");
    return result;
}
}

void CheckFriendsExchange(const std::filesystem::path& media,const recovered::PackageView& font) {
    gameplay::AcceptedProfile profile{};
    profile.nickname.Assign("Toro"); profile.player.Assign("Nana");
    gameplay::RoomInventory bag;
    gameplay::RoomTalkState talk;
    std::string error;
    if(!bag.Load(media.parent_path(),error) || !talk.Load(media,profile,error)) throw std::runtime_error(error);
    talk.SetCalendarMinute(400);
    Check(!talk.BeginFriendsExchange(7,bag),"Friends action started before room readiness");
    talk.Step(0,true,true,&font,true);
    for(unsigned tick=0;tick<8000 && !talk.finished();++tick) {
        if(talk.gift_requested()) talk.SupplyGift(-1);
        if(talk.random_requested()) talk.SupplyRandom(0);
        talk.Step(0,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
    }
    auto& vocab=talk.vocabulary();
    Check(talk.finished() && gameplay::CountIntroductionFriends(vocab)==0,"Missing cold exchange profile");
    Exchange(talk,bag,font,7,4);
    Exchange(talk,bag,font,9,{}).ExpectOrder({0x8ab44,0x8ab6a,0x8ab94});

    // Use the normal item owner to issue Toro's stored backpack in this component.
    const int slot=bag.AwardGift(0,3,profile.nickname.text(),2026,11,19);
    Check(slot==0 && bag.slot_item(slot) && bag.slot_item(slot)->id==98,"No owned backpack for present exchange");
    Exchange(talk,bag,font,9,4);
    Check(bag.slot_item(slot) && bag.slot_item(slot)->id==98 && bag.slot_item(slot)->category==1,
          "Cancelling present exchange changed the owned item");

    // Explicit component rows exercise E25E's three-word threshold on its real table.
    for(unsigned count=0;count<3;++count) {
        const auto reply=Exchange(talk,bag,font,8,{});
        reply.ExpectOrder({0x8abb6,0x8abc6,0x8ac02});
        const std::string missing="\x82\xa0\x82\xc6 "+std::to_string(3-count)+"\x8c\xc2\x82\xae\x82\xe7\x82\xa2";
        Check(reply.messages.at(0x8ac02).find(missing)!=std::string::npos,"Wrong missing learned word count");
        vocab.WriteNumber(520+count,0,0x4000);
    }
    Exchange(talk,bag,font,8,4);

    for(unsigned row=601;row<700;++row) vocab.WriteText(row,1,"Card");
    Check(gameplay::CountIntroductionFriends(vocab)==99,"Wrong full-card test state");
    Exchange(talk,bag,font,7,{}).ExpectOrder({0x8aaa7,0x8aace,0x8ab01,0x8ab16});
    Check(gameplay::CountIntroductionFriends(vocab)==99,"Refusal changed card ownership");

    // A genuine place choice reaches GAME49. This component provides no peer.
    Exchange(talk,bag,font,9,0);
    Check(bag.slot_item(slot) && bag.slot_item(slot)->id==98,"Starting a meeting consumed the gift");
}
