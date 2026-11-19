#include "room_notebook_extra.h"
#include "nickname_owner.h"
#include "profile_date.h"
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
void RoomFriendNotebook::Open(std::uint32_t buttons) {
    phase_=Phase::opening;
    tab_=0;
    previous_=buttons;
    held_.fill(0);
    effects_={12};
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
        if (old!=tab_) effects_.push_back(6);
    } else if (edges&0x1000) {
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
        return scene_.Draw(0,pose,vram,offset);
    }
    constexpr int actor_stamp[]{5,4,1,2,3}; // D84F4 -> 1466C
    if (profile.actor>=5) return false;
    const int seal=3*(actor_stamp[profile.actor]-1)+int(stamp_);
    pose.insert(pose.end(),{{6,1,true},{7,1,true},{8,seal,true},{9,1,true}});
    const unsigned date=profile.packed_birthday;
    const int month=Bcd((date>>8)&255), day=Bcd(date&255);
    const std::string birthday=date==UINT32_MAX ? std::string{} :
        ProfileDateText({1900+Bcd(date>>16),month,day,-1});
    // 144C0/144E8 read the CP932 names from the BOOT profile tables.
    constexpr std::string_view genders[]{"\x81\x48","\x92\x6a","\x8f\x97"};
    constexpr std::string_view blood[]{"\x82\x60\x8c\x5e","\x82\x61\x8c\x5e","AB\x8c\x5e","\x82\x6e\x8c\x5e","\x81\x48"};
    const std::array<std::string_view,8> fields{profile.player.text(),profile.nickname.text(),
        unsigned(profile.gender)<3?genders[profile.gender]:std::string_view{},
        unsigned(profile.blood)<5?blood[profile.blood]:std::string_view{},birthday,
        ProfileZodiacText(ProfileZodiac(month,day)),profile.phone.text(),saying};
    const auto draw_field=[&](unsigned node,int x,int y){
        if (node<211 || node>218) return;
        const auto text=fields[node-211];
        if (node==211 || node==212 || node==218) x=int(x-MeasureUiText(text,font)*.5f); // 93E9C..93EB4 centers before the vertex is truncated
        DrawUiText(text,x,y,font,vram,offset,{85,47,13});
    };
    return scene_.Draw(6,pose,vram,offset,{},draw_field);
}
}
