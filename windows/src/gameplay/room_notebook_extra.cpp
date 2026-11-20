#include "room_notebook_extra.h"
#include "nickname_owner.h"
#include "profile_date.h"
#include "room_vocabulary.h"
#include <algorithm>
#include "recovered/rng_native.h"
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr Cb2aMedia note{"NOTE.CB2A",23129,"bb08a19dc8f597f2fdc2421ba8697b2622a7201923fd6111e16b5c0680461958",
    "note-textures.bin",477800,"2d84b736adc9cc93f2ad45007e396f14e6dd336d75612cbfb708407f9ad90d3c"};
int Bcd(unsigned n) { return int((n&15)+10*((n>>4)&15)+100*((n>>8)&15)); }
}
bool RoomFriendNotebook::Load(const std::filesystem::path& dir,std::string& error) {
    return scene_.Load(dir,note,error);
}
void RoomFriendNotebook::Open(std::uint32_t buttons,const RoomVocabulary* vocabulary) {
    phase_=Phase::opening;
    vocabulary_=vocabulary;
    contacts_.clear();
    tab_=selected_=0;
    own_card_=true;
    if (vocabulary_) {
        for (unsigned i=601;i<vocabulary_->row_count();++i)
            if (vocabulary_->HasField(i,1) && !(vocabulary_->FieldInt(i,0)&2)) contacts_.push_back(i);
        SortContacts();
    }
    previous_=buttons;
    held_.fill(0);
    effects_={12};
}
void RoomFriendNotebook::SortContacts() {
    const unsigned column=tab_==0 ? 2 : 1;
    std::sort(contacts_.begin(),contacts_.end(),[&](unsigned a,unsigned b) {
        if (tab_>=2) return tab_==2 ? a<b : a>b;
        const auto x=vocabulary_->FieldText(a,column), y=vocabulary_->FieldText(b,column);
        return x==y ? a<b : x<y;
    });
    selected_=0;
}

std::vector<unsigned> RoomFriendNotebook::TakeEffects() {
    return std::exchange(effects_,{});
}
void RoomFriendNotebook::Step(std::uint32_t buttons,bool fade_complete,recovered::RngNativeOwner* rng) {
    const auto edges=buttons & ~previous_;
    previous_=buttons;
    if (phase_==Phase::opening) {
        if (fade_complete) phase_=Phase::list;
        return;
    }
    if (!ready()) return;
    if (detail()) {
        if (edges&0x4000) { phase_=Phase::list; effects_.push_back(5); }
        return;
    }
    std::array<bool,2> move{};
    constexpr unsigned dirs[]{0x20,0x80};
    for (unsigned i=0;i<2;++i) {
        auto& count=held_[i];
        if (!(buttons&dirs[i])) { count=0; continue; }
        move[i]=bool(edges&dirs[i]) || (count>=10 && count%2==0);
        count=count<12?count+1:11;
    }
    if (move[0] || move[1]) {
        const unsigned old=tab_;
        if (move[0] && (tab_<3 || (edges&dirs[0]))) tab_=(tab_+1)%4;
        else if (!move[0] && (tab_ || (edges&dirs[1]))) tab_=(tab_+3)%4;
        if (old!=tab_) {
            SortContacts();
            effects_.push_back(6);
        }
    } else if (!contacts_.empty() && (edges&0x50)) {
        const unsigned n=count();
        selected_=(selected_+((edges&0x40)?1:n-1))%n;
        effects_.push_back(6);
    } else if (!contacts_.empty() && (edges&0x2000)) {
        own_card_=false;
        phase_=Phase::detail;
        effects_.push_back(4);
    } else if (edges&0x1000) {
        own_card_=true;
        // 2B238 populates the self fields; 2B240 selects one of three seals.
        if (!rng) throw std::runtime_error("Missing room RNG for friend notebook seal");
        stamp_=1+rng->NextC3234()%3;
        phase_=Phase::detail;
        effects_.push_back(4);
    } else if (edges&0x4000) {
        effects_={5,12};
        phase_=Phase::closing;
    }
}
bool RoomFriendNotebook::Draw(const AcceptedProfile& profile,std::string_view saying,
                              const recovered::PackageView& font,std::vector<std::uint8_t>& vram,
                              std::size_t offset) const {
    std::vector<Cb2aClip> pose;
    for (unsigned i=0;i<10;++i) pose.push_back({i,0});
    if (!detail()) {
        pose.insert(pose.end(),{{0,1,true},{1,int(tab_+1),true},{5,1,true}});
        if (!scene_.Draw(0,pose,vram,offset)) return false;
        constexpr unsigned page=8;
        const unsigned start=(selected_/page)*page;
        for (unsigned i=start;i<std::min(start+page,count());++i) {
            const auto name=vocabulary_->FieldText(contacts_.at(i),1);
            const auto color=i==selected_ ? std::array<unsigned,3>{78,45,156} : std::array<unsigned,3>{85,47,13};
            DrawUiText(name,112,69+int(20*(i-start)),font,vram,offset,color,1.f,{112,64,415,242});
        }
        return true;
    }
    constexpr int actor_stamp[]{5,4,1,2,3}; // D84F4 -> 1466C
    if (profile.actor>=5) return false;
    const int seal=own_card_ ? 3*(actor_stamp[profile.actor]-1)+int(stamp_) : 0;
    pose.insert(pose.end(),{{6,1,true},{7,1,true},{8,seal,true},{9,1,true}});
    KscSaveValue card;
    if (!own_card_) card=vocabulary_->FriendCard(contacts_.at(selected_));
    const auto field=[&](unsigned i) -> std::string_view { return card.children.at(i).text; };
    const auto numeric=[&](unsigned i) { return card.children.at(i).word & 0x0fffffffu; };
    const unsigned date=own_card_ ? profile.packed_birthday : numeric(23);
    const int month=Bcd((date>>8)&255), day=Bcd(date&255);
    const std::string birthday=date==UINT32_MAX ? std::string{} :
        ProfileDateText({1900+Bcd(date>>16),month,day,-1});
    // 144C0/144E8 read the CP932 names from the BOOT profile tables.
    constexpr std::string_view genders[]{"\x81\x48","\x92\x6a","\x8f\x97"};
    constexpr std::string_view blood[]{"\x82\x60\x8c\x5e","\x82\x61\x8c\x5e","AB\x8c\x5e","\x82\x6e\x8c\x5e","\x81\x48"};
    const unsigned gender=own_card_ ? unsigned(profile.gender) : numeric(9)&3;
    const unsigned blood_group=own_card_ ? unsigned(profile.blood) : numeric(24);
    const std::array<std::string_view,8> fields{
        own_card_ ? profile.player.text() : field(1), own_card_ ? profile.nickname.text() : field(21),
        gender<3 ? genders[gender] : std::string_view{},
        blood_group<5 ? blood[blood_group] : std::string_view{},birthday,
        ProfileZodiacText(ProfileZodiac(month,day)),
        own_card_ ? profile.phone.text() : field(25), own_card_ ? saying : field(26)};
    const auto draw_field=[&](unsigned node,int x,int y){
        if (node<211 || node>218) return;
        const auto text=fields[node-211];
        if (node==211 || node==212 || node==218) x=int(x-MeasureUiText(text,font)*.5f); // 93E9C..93EB4 centers before the vertex is truncated
        DrawUiText(text,x,y,font,vram,offset,{85,47,13});
    };
    return scene_.Draw(6,pose,vram,offset,{},draw_field);
}
}
