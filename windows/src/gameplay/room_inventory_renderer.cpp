#include "room_inventory_renderer.h"
#include "room_inventory.h"
#include "room_item_actions_renderer.h"
#include "nickname_owner.h"
#include "recovered/game_primitives.h"
#include <algorithm>
#include <cstdio>

namespace dokodemo::gameplay {
namespace {
constexpr Cb2aMedia inventory_media{
    "MOCHIMONO.CB2A",23590,"ca7dc478112e51a46266815398dfff7f160ceba69cb034987a95299fbefe2a18",
    "inventory-textures.bin",1118412,"eeeb357e12e384669ecfbc144d81d49bdb722257e9f1dbe38438cf47d5b6c33f"};
using Bytes=std::vector<std::uint8_t>;
using Ink=std::array<unsigned,3>;
constexpr Ink white{253,253,253},brown{85,47,13}, cream{253,225,203}, dark{137,101,71};

std::string DateText(int value,const char* unit) {
    char field[16]{};
    std::snprintf(field,sizeof(field),"%2d%s",value,unit);
    return field;
}

// 37B10 -> 14AD0 supplies a 210-pixel column at 7/8 scale and 2px spacing.
void DrawDescription(std::string_view text,int x,int y,const recovered::PackageView& font,
                     Bytes& vram,std::size_t offset,Ink ink) {
    for (unsigned line=0;!text.empty();++line) {
        while (!text.empty() && text.front()==' ') text.remove_prefix(1);
        std::size_t end=0;
        while (end<text.size()) {
            recovered::Glyph glyph;
            if (!recovered::read_glyph(reinterpret_cast<const std::uint8_t*>(text.data()+end),
                                       text.size()-end,glyph)) return;
            const auto next=end+glyph.consumed;
            if (text[end]=='\n' || MeasureUiText(text.substr(0,next),font,.875f)>210) break;
            end=next;
        }
        DrawUiText(text.substr(0,end),x,y+int(18.f*.875f*line),font,vram,offset,ink,.875f);
        text.remove_prefix(end);
        if (!text.empty() && text.front()=='\n') text.remove_prefix(1);
    }
}
}

bool RoomInventoryRenderer::Load(const std::filesystem::path& directory,std::string& error) {
    const auto common=directory.parent_path()/"title";
    return list_.Load(directory,inventory_media,error) && select_.Load(common,item_select_media,error)
        && question_.Load(common,item_question_media,error);
}

bool RoomInventoryRenderer::Draw(const RoomInventory& inventory,const recovered::PackageView& font,
                                 Bytes& vram,std::size_t offset) const {
    if (!inventory.visible()) return false;
    const auto count=inventory.count();
    std::vector<Cb2aClip> pose;
    for (unsigned clip=0;clip<=10;++clip) pose.push_back({clip,0,false});
    pose.insert(pose.end(),{{0,1,true},{1,1,true},{2,int(inventory.filter()),true},
                            {8,count?1:2,true},{10,1,true}});
    int anchor_x=0,anchor_y=0;
    if (!list_.Draw(0,pose,vram,offset,{},[&](unsigned node,int x,int y){
        if (node==57) { anchor_x=x; anchor_y=y; }
    }) || !list_.Draw(10,pose,vram,offset)) return false;

    // 36D1C scissors the scrolling contents to the three row viewport.
    const int top=std::clamp(anchor_y-31,0,272);
    const int bottom=std::clamp(anchor_y-31+186,0,272);
    const unsigned first=inventory.selection()>2 ? inventory.selection()-2 : 0;
    if (count) {
        Bytes rows(vram.begin()+offset,vram.begin()+offset+4*(512*271+480));
        const float selected_y=float(anchor_y-136+62*int(inventory.selection()-first));
        if (!list_.Draw(3,pose,rows,0,{float(anchor_x-240),selected_y})) return false;
        for (unsigned index=first;index<std::min(first+3,count);++index) {
            const auto* item=inventory.item_at(index);
            if (!item) return false;
            const bool selected=index==inventory.selection();
            const Cb2aPlacement at{0,float(62*(index-first))};
            auto row_pose=pose;
            row_pose.insert(row_pose.end(),{{4,selected?1:2,true},{5,item->category>0?2:1,true},
                {6,2*item->category+(selected?1:2),true},{9,item->id+1,true}});
            auto text=[&](unsigned node,int x,int y){
                Ink ink=selected?white:brown;
                if (node==169) DrawUiText(item->name,x,y,font,rows,0,ink);
                if (node==170) DrawDescription(item->description,x,y,font,rows,0,selected?cream:dark);
                if (node==172 && item->month>=0) {
                    const auto month=DateText(item->month,"\x8c\x8e");
                    DrawUiText(month,x-int(MeasureUiText(month,font)),y,font,rows,0,ink);
                }
                if (node==173 && item->day>=0)
                    DrawUiText(DateText(item->day,"\x93\xfa"),x,y,font,rows,0,ink);
            };
            if (!list_.Draw(4,row_pose,rows,0,at) || !list_.Draw(5,row_pose,rows,0,at) ||
                !list_.Draw(7,row_pose,rows,0,at,text) || !list_.Draw(9,row_pose,rows,0,at)) return false;
        }
        for (int y=top;y<bottom;++y) std::copy_n(rows.data()+4*y*512,4*480,vram.data()+offset+4*y*512);
    }
    if (!list_.Draw(1,pose,vram,offset) || !list_.Draw(2,pose,vram,offset)) return false;
    if (inventory.actions_visible()) {
        if (!DrawItemActions(select_, inventory.action_clip(), inventory.action_frame(), 5,
                             inventory.action_cursor(), 64.f, RoomInventory::ActionLabel, font, vram, offset))
            return false;
    }
    if (inventory.discard_visible()) {
        // 36B0C passes DA62C to TRC at (0,0); one line and the Yes/No row.
        constexpr std::string_view prompt="\x96\x7b\x93\x96\x82\xc9\x82\xb7\x82\xc4\x82\xdc\x82\xb7\x82\xa9\x81\x48";
        return DrawItemQuestion(question_,prompt,0,0,inventory.discard_progress(),
                                inventory.discard_cursor(),font,vram,offset);
    }
    return true;
}
}
