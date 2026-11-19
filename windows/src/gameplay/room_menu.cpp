#include "room_menu.h"
#include <algorithm>

namespace dokodemo::gameplay {
namespace {
constexpr Cb2aMedia media{"MMENU.CB2A",31512,"5a8c8df5cfc2011850fa8c9403ef2cb8a071ed8d715e38cd50df5d4bc2ba896b",
    "menu-textures.bin",895312,"016761963e0de150c7604b7b0909b87c48ce8cd5a9dda4784e32400a28944d5c"};
// BOOT 13B28, full room menu (D8410).
constexpr int outcomes[7][4]={{0,-1,-1,-1},{2,-1,-1,-1},{1,-1,-1,-1},{3,4,6,7},
                               {8,9,10,11},{5,-1,-1,-1},{13,14,15,-1}};
}
bool RoomMenu::Load(const std::filesystem::path& dir,std::string& error) {
    return scene_.Load(dir,media,error);
}
void RoomMenu::Open(std::uint32_t buttons,bool away,bool other_profiles,bool media_mounted) {
    if(active()) return;
    away_=away; others_=other_profiles; mounted_=media_mounted; row_=column_=sub_=0; result_=-1;
    frame_=clip_=boundary_=0; idle_=1800; holds_={}; previous_=buttons;
    highlights_.fill(1); highlights_[0]=9;
    phase_=Phase::opening; effects_={13};
}
void RoomMenu::Close(int result) {
    result_=result; clip_=1; frame_=0; phase_=Phase::closing; effects_.push_back(14);
}
std::optional<int> RoomMenu::Step(std::uint32_t buttons) {
    effects_.clear();
    const unsigned edge=buttons & ~previous_;
    previous_=buttons;
    if(!active()) return {};
    const unsigned length=clip_<=1?10:21;
    if(frame_<length) ++frame_;
    if(phase_==Phase::opening) {
        if(frame_==length) phase_=Phase::input;
        return {};
    }
    if(phase_==Phase::closing) {
        if(frame_<length) return {};
        phase_=Phase::closed; return result_;
    }
    if(buttons & (0xf0|0xf000)) idle_=1800;
    if(--idle_==0) { Close(-1); return {}; }
    for(auto& h:highlights_) if(h>1 && h<9) ++h;
    if(frame_<length) return {};
    int direction=0;
    bool first=false;
    for(unsigned i=0;i<2;++i) {
        const unsigned mask=i?0x10:0x40;
        if(!(buttons&mask)) { holds_[i]=0; continue; }
        const bool repeated=holds_[i]>=10 && !(holds_[i]%2);
        ++holds_[i];
        if(!direction && ((edge&mask)||repeated)) { direction=i?-1:1; first=bool(edge&mask); }
    }
    if(direction) {
        int& sel=sub_?column_:row_;
        // 12E4C/12F20: the ordinary local friends menu has no GAME49 session.
        const int submenu_last=(sub_==2 || (sub_==1 && others_)) ? 3 : 2;
        const int last=sub_?submenu_last:6;
        int next=sel+direction;
        if(next<0 || next>last) {
            if(!first) return {};
            next=next<0?last:0;
        }
        // 1309C/130E8 skip the save row when 4E67C reports no media.
        if(sub_==3 && !mounted_ && next==1) next+=direction;
        // 128A8 skips おでかけ (row 1) when away and 部屋に帰る (row 2) at home.
        if(!sub_ && next==(away_?1:2)) next+=direction;
        if(!sub_) { highlights_[sel]=1; highlights_[next]=2; }
        sel=next; effects_.push_back(6); return {};
    }
    // 12D94, 12F48 and 1311C test Circle inside the three submenus.
    const unsigned select_buttons = sub_ ? 0x2000 : (0x2000 | 0x20);
    if(edge & select_buttons) {
        if(!sub_ && (row_==3 || row_==4 || row_==6)) {
            sub_=row_==3?1:row_==4?2:3; column_=0;
            clip_=sub_==1?(others_?3:2):sub_==2?4:5;
            frame_=0; effects_.push_back(4);
        } else if((edge&0x2000) && (sub_ || !(edge&0x20))) {
            effects_.push_back(4); Close(outcomes[row_][column_]);
        }
    } else if(edge & (0x4000|0x80)) {
        effects_.push_back(5);
        if(sub_) {
            // 12DBC/12F70 resume the active parent with its current row selected.
            frame_ = 10;
            sub_=0; column_=0; clip_=0;
        }
        else Close(-1);
    }
    return {};
}
bool RoomMenu::Draw(std::vector<std::uint8_t>& ram,std::size_t offset) const {
    if(!active()) return true;
    // 11B54..11E08: root0, the context switch, seven category
    // controls, and the four submenu marker controls are independent tracks.
    std::vector<Cb2aClip> tracks;
    // 12ADC rebinds the root to clip 4. Nodes 22..27 keep their parent
    // sprite references from clip 0; the friends and System popup clips overlay the open parent.
    if(sub_==2 || clip_==5) tracks.push_back({0,10,false});
    tracks.push_back({clip_,int(frame_),false});
    tracks.push_back({6,away_?2:1,false});
    for(unsigned i=0;i<7;++i) tracks.push_back({7+i,highlights_[i],false});
    tracks.push_back({14,sub_==1&&!others_?column_+2:5,false});
    tracks.push_back({15,sub_==1&&others_?column_+2:6,false});
    tracks.push_back({16,sub_==2?column_+2:6,false});
    tracks.push_back({18,sub_==3?column_+2:5,false});
    return scene_.Draw(0,tracks,ram,offset);
}
}
