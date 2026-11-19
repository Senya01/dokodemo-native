#include "room_notebook_renderer.h"
#include "room_notebook.h"
#include "room_item_actions_renderer.h"
#include <algorithm>
#include <cmath>

namespace dokodemo::gameplay {
namespace {
constexpr Cb2aMedia media{
    "MEMO.CB2A",27943,"361af2782e34f1d8ad3c03b27519adfd7122622c21c9ab0445ccb3fe18ad9169",
    "memo-textures.bin",252592,"ad287b89cb36cb459caad2f64cf59301cf0e9d1f05fcb4486318ebf16fb5fbe0"};
// D9E70, D9E88, D9E7C used by the home word-memo SELECT.
constexpr std::string_view change_reading="\x82\xe6\x82\xdd\x82\xf0\x92\xbc\x82\xb7";
constexpr std::string_view erase_word="\x92\x50\x8c\xea\x82\xf0\x8f\xc1\x82\xb7";
constexpr std::string_view cancel="\x82\xe2\x82\xc1\x82\xcf\x83\x84\x83\x81";
using Bytes=std::vector<std::uint8_t>;
}

bool RoomNotebookRenderer::Load(const std::filesystem::path& game,std::string& error) {
    return memo_.Load(game/"assets/game/notebook",media,error) &&
        select_.Load(game/"assets/game/title",item_select_media,error) &&
        question_.Load(game/"assets/game/title",item_question_media,error);
}

bool RoomNotebookRenderer::Draw(const RoomNotebook& book,const recovered::PackageView& font,
                                Bytes& vram,std::size_t offset) const {
    if (!book.visible()) return false;
    // 2D630..2D6CC binds the eight authored controls. Root zero also
    // traverses the nested tab strip, selected tab and bottom navigation.
    std::vector<Cb2aClip> pose;
    for (unsigned i=0;i<8;++i) pose.push_back({i,0});
    pose.insert(pose.end(),{{0,1,true},{1,1,true},{2,int(book.tab()),true},{3,1,true},{6,1,true},{7,1,true}});
    std::array<Cb2aPlacement,3> anchors{};
    if (!memo_.Draw(0,pose,vram,offset,{},[&](unsigned node,int x,int y){
        if (node>=69 && node<=71) anchors[node-69]={float(x-240),float(y-136)};
    })) return false;
    // 2E994 scissors the moving dots, selection bars and text to the
    // paper below the column headings. Row positions advance by 22 pixels.
    const int top=std::clamp(int(anchors[1].y)+136,0,272);
    const int bottom=std::min(272,top+158);
    Bytes rows(vram.begin()+offset,vram.begin()+offset+4*(512*271+480));
    constexpr int step=22;
    const float scroll=book.scroll();
    const unsigned first=static_cast<unsigned>(std::max(0.f,scroll)/step);
    auto dots=anchors[0];
    dots.y-=std::fmod(scroll,544.f);
    if (!memo_.Draw(3,pose,rows,0,dots)) return false;
    dots.y+=544;
    if (!memo_.Draw(3,pose,rows,0,dots)) return false;
    if (book.count()) {
        for (unsigned col=0;col<2;++col) {
            auto at=anchors[1+col];
            at.y+=float(step*book.selection())-scroll;
            if (!memo_.Draw(4+col,pose,rows,0,at)) return false;
        }
    }
    for (unsigned i=first;i<std::min(first+8,book.count());++i) {
        const auto* word=book.item_at(i);
        if (!word) return false;
        const std::array<unsigned,3> ink=i==book.selection()?std::array<unsigned,3>{253,253,253}:
                                                            std::array<unsigned,3>{85,47,13};
        if (!memo_.Draw(6,pose,rows,0,{0,float(step*i)-scroll},[&](unsigned node,int x,int y){
            if (node==304) DrawUiText(word->written,x,y,font,rows,0,ink,1.f,{0,0,480,272},0.f);
            if (node==305) DrawUiText(word->reading,x,y,font,rows,0,ink,1.f,{0,0,480,272},0.f);
        })) return false;
    }
    for (int y=top;y<bottom;++y) std::copy_n(rows.data()+y*2048,1920,vram.data()+offset+y*2048);
    if (book.delete_visible()) {
        constexpr std::string_view prompt="\x96\x7b\x93\x96\x82\xc9\x8f\xc1\x82\xb5\x82\xdc\x82\xb7\x82\xa9\x81\x48";
        return DrawItemQuestion(question_,prompt,128,24,book.delete_progress(),book.delete_cursor(),
                                font,vram,offset);
    }
    if (!book.actions_visible()) return true;
    const auto label=[&](unsigned row){
        if (!row) return change_reading;
        return row+1==book.action_count()?cancel:erase_word;
    };
    return DrawItemActions(select_,book.action_clip(),book.action_frame(),book.action_count(),
                           book.action_cursor(),80.f,label,font,vram,offset);
}
}
