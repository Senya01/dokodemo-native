#include "room_word_renderer.h"

#include "nickname_owner.h"
#include "room_word_entry.h"
#include "title_renderer.h"
#include "recovered/game_primitives.h"
#include "recovered/package_storage_native.h"

#include <algorithm>

namespace dokodemo::gameplay {
namespace {
using Ink=std::array<unsigned,3>;
using Bytes=std::vector<std::uint8_t>;
constexpr Ink brown{85,47,13}, selected{227,99,79}, white{253,253,253}, preview{101,77,55};
constexpr Ink composing_background{118,187,203};

std::string_view Unit(std::string_view text) {
    recovered::Glyph glyph{};
    if (!recovered::read_glyph(reinterpret_cast<const std::uint8_t*>(text.data()),text.size(),glyph)) return {};
    return text.substr(0,glyph.consumed);
}

unsigned Units(std::string_view text) {
    unsigned count=0;
    while (!text.empty()) {
        const auto unit=Unit(text);
        if (unit.empty()) break;
        text.remove_prefix(unit.size());
        ++count;
    }
    return count;
}

void FieldText(std::string_view text,float x,int y,const recovered::PackageView& font,Bytes& vram,
               std::size_t offset,Ink color,const std::array<int,4>& clip) {
    while (!text.empty()) {
        auto unit=Unit(text);
        if (unit.empty()) break;
        DrawUiText(unit,int(x),y,font,vram,offset,color,1.f,clip);
        text.remove_prefix(unit.size());
        x+=16.f;
    }
}
}

std::vector<WordCandidateCell> MeasureWordCandidates(const std::vector<std::string>& candidates,
                                                     const recovered::PackageView& font) {
    std::vector<WordCandidateCell> cells;
    cells.reserve(candidates.size());
    int page=0,row=0,x=0;
    for (const auto& text:candidates) {
        const auto width=static_cast<int>(MeasureUiText(text,font));
        // 20C34..20CA8 leaves an oversized first candidate on its own row.
        if (x && x+width>144) {
            x=0;
            if (++row==4) { ++page; row=0; }
        }
        cells.push_back({page,x,row,width});
        x+=width+8;
    }
    return cells;
}

bool RoomWordEntry::Draw(const TitleRenderer& renderer,Bytes& vram,std::size_t offset,
                         const recovered::PackageView* font) const {
    if (phase_!=Phase::opening && phase_!=Phase::editing && phase_!=Phase::closing) return true;
    if (!font && (picking_ || !written_.empty() || !composition_.empty() || !candidates_.empty()))
        return false;

    const auto cells=font?MeasureWordCandidates(candidates_,*font):std::vector<WordCandidateCell>{};
    const bool converting=choosing_candidate_ && candidate_<cells.size();
    const bool show_group=picking_ && !pressed_clip_;
    const unsigned key=column_+3*row_;
    const auto* group=show_group?&ActiveGroup():nullptr;
    const unsigned group_size=group?group->columns+5*(unsigned(group->rows.size())-1):1;
    // 21898 selects type 2's authored base frame 6/7 and 2198C
    // binds key-highlight clip 2/3 for the kana/alphabet page.
    const unsigned board=is_reading_edit()?6u+page_:1u+2*page_+unsigned(subpage_);
    const unsigned highlight=is_reading_edit()?2u+page_:2u;
    std::vector<UiClipFrame> controls{
        {0,board},{highlight,row_==4 || converting?13:1+key},
        {6,converting?2u:1u},{11,converting?3u+unsigned(cells.back().page>0):1u+unsigned(!cells.empty())},
        {5,show_group?key+1:13u},{14,group_size},
        {7,1u+unsigned(!converting && row_==4 && column_==0)},
        {8,1u+unsigned(!converting && row_==4 && column_==1)},
        {9,1u+unsigned(!converting && row_==4 && column_==2)},
        {10,converting?7u:1u+unsigned(!cells.empty())},{13,1},{12,1}
    };
    if (pressed_clip_==4) controls.push_back({4,pressed_frame_});
    for (auto& item:controls)
        if (pressed_clip_ && item.clip==pressed_clip_) item.frame=pressed_frame_;
    std::vector<UiNodeOverride> bindings;
    if (is_reading_edit()) bindings.push_back({151,{},{},{},highlight});
    if (show_group) bindings.push_back({161,{},{},{},5u});

    return renderer.RenderSoftkey(phase_==Phase::closing?16:15,frame_,controls,bindings,
        [&](unsigned node,int x,int y) {
            if (!font) return;
            if (node==42) {
                const auto committed=Units(written_),pending=Units(composition_);
                const int count=static_cast<int>(committed+pending);
                const unsigned limit=is_reading_edit()?16u:8u;
                const int edge=committed==limit || pending==limit?8:7;
                const int target=count>7?16*(edge-count):0;
                text_scroll_+=(float(target)-text_scroll_)*.125f;
                // A closing reading editor samples +120 at 22500. CANCEL
                // already cleared it at 21838, while +172 still drives scrolling.
                if (is_reading_edit() && phase_==Phase::closing && cancelled_) return;
                const std::array<int,4> clip{std::max(0,x),std::max(0,y),
                                             std::min(480,x+128),std::min(272,y+16)};
                const float start=x+text_scroll_, split=start+16.f*committed;
                // 2255C..225DC submits two font runs and the separate C574
                // background rectangle for the currently composing span.
                const int right=std::min(clip[2],int(split+16.f*pending));
                for (int py=clip[1];py<clip[3];++py)
                    for (int px=std::max(clip[0],int(split));px<right;++px)
                        std::copy(composing_background.begin(),composing_background.end(),
                                  vram.begin()+offset+4*(512*py+px));
                FieldText(written_,start,y,*font,vram,offset,brown,clip);
                FieldText(composition_,split,y,*font,vram,offset,white,clip);
            } else if (node==251 && group) {
                for (unsigned row=0;row<group->rows.size();++row) {
                    const auto& line=group->rows[row];
                    for (unsigned col=0;col<group->columns;++col) {
                        const bool current=col==kana_x_ && row==kana_y_;
                        std::string_view cell=col<line.size()?std::string_view(line[col]):std::string_view{};
                        if (cell.empty() || cell=="\t") cell=current?"_":"";
                        const int left=int(x+16*int(col)+8-MeasureUiText(cell,*font)*.5f);
                        DrawUiText(cell,left,y+17*int(row),*font,vram,offset,current?selected:brown);
                    }
                }
            } else if (node==218 && candidate_<cells.size()) {
                const int page=cells[candidate_].page;
                for (std::size_t i=0;i<cells.size();++i) {
                    const auto& cell=cells[i];
                    if (cell.page!=page) continue;
                    // 22798 terminates the display copy at byte 16.
                    const auto text=std::string_view(candidates_[i]).substr(0,16);
                    DrawUiText(text,x+cell.x,y+cell.row*18,*font,vram,offset,
                               converting?(candidate_==i?selected:brown):preview);
                }
            }
        },vram,offset);
}
}
