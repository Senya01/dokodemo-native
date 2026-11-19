#include "room_answer_renderer.h"
#include "nickname_owner.h"
#include "recovered/game_primitives.h"
#include <algorithm>
#include <string_view>

namespace dokodemo::gameplay {
namespace {
constexpr Cb2aMedia select_media{
    "SELECT.CB2A",11307,"6473316977b01dcce46022b643a364efae5f3a9e6da2770b67eda4c42e422d06",
    "select-textures.bin",8864,"ffbe0f582015f9761ba0b8da3e7b19b87ab9ff7daab0b74316424314cf09ed72"};
constexpr int row_width=160,row_step=21;
using Lines=std::vector<std::string_view>;

// 1046C starts a new line on a line break or when the next glyph no
// longer fits in the mode-0 160-pixel text region.
bool Split(std::string_view text,const recovered::PackageView& font,Lines& lines) {
    std::size_t start=0;
    for (std::size_t i=0;i<text.size();) {
        if (text[i]=='\n') {
            lines.push_back(text.substr(start,i-start));
            start=++i;
            continue;
        }
        recovered::Glyph glyph;
        if (static_cast<unsigned char>(text[i])<32 ||
            !recovered::read_glyph(reinterpret_cast<const std::uint8_t*>(text.data()+i),
                                  text.size()-i,glyph) || glyph.index<32 || glyph.index>=8224) return false;
        const auto end=i+glyph.consumed;
        if (MeasureUiText(text.substr(start,end-start),font)>row_width && i>start) {
            lines.push_back(text.substr(start,i-start));
            start=i;
        }
        i=end;
    }
    lines.push_back(text.substr(start));
    return true;
}
}
bool RoomAnswerRenderer::Load(const std::filesystem::path& game_directory,std::string& error) {
    return select_.Load(game_directory/"title",select_media,error);
}
bool RoomAnswerRenderer::Draw(const std::vector<std::string>& options,unsigned cursor,unsigned frame,
                              bool closing,const recovered::PackageView& font,
                              std::vector<std::uint8_t>& vram,std::size_t offset,unsigned active_ticks) {
    if (cursor>=options.size()) return false;
    // 1046C's control dispatch drops inline 04 and 16 from SELECT labels.
    // Message emphasis can therefore enclose a word interpolated by KSC
    // without changing either the selection style or the measured width.
    std::vector<std::string> visible;
    visible.reserve(options.size());
    for (const auto& option:options) {
        std::string label;
        for (char c:option) if (c!=0x04 && c!=0x16) label.push_back(c);
        visible.push_back(std::move(label));
    }
    std::vector<Lines> rows(options.size());
    int total=0;
    for (unsigned i=0;i<options.size();++i) {
        if (!Split(visible[i],font,rows[i])) return false;
        total+=int(rows[i].size());
    }
    // 10088..100EC selects frame 14 for an overflowing mode-0 SELECT.
    const bool scrollable=total>11;
    const int height=std::min(total,11)*row_step;
    if (!active_ticks || active_ticks<last_tick_) {
        scroll_=destination_=0;
        last_tick_=0;
    }
    if (scrollable && active_ticks!=last_tick_) {
        int top=0;
        for (unsigned i=0;i<cursor;++i) top+=int(rows[i].size())*row_step;
        const int bottom=top+int(rows[cursor].size())*row_step;
        // 10C2C/10D20 keep the selected row in view. 10CA8 eases the
        // displayed origin once per selector update, including held input.
        if (bottom>destination_+height) destination_=float(bottom-height);
        else if (top<destination_) destination_=float(top);
        scroll_+=(destination_-scroll_)*0.125f;
        last_tick_=active_ticks;
    }
    const unsigned clip=closing?14:13;
    // 1E638 resets the SELECT owner, appends KSC strings, then calls FFFC.
    // FFA8 leaves the placement vector at zero; 10134 writes it to node 43.
    const std::vector<Cb2aClip> controls{{0,0},{13,0},{clip,0},{25,0},
        {clip,int(std::min(frame,10u)),true},{0,scrollable?14:total,true},{25,1,true}};
    bool success=true;
    auto items=[&](unsigned node,int x,int y) {
        if (node!=34) return;
        // 10E44 scissors the row layer at the animated node. Leave the
        // SELECT border and the room outside that rectangle untouched.
        const int view_y=y;
        const int left=std::clamp(x,0,480), right=std::clamp(x+row_width,0,480);
        const int top=std::max(0,y), bottom=std::min(272,y+height);
        if (scrollable) {
            clipped_.resize(4*512*272);
            for (int iy=top;iy<bottom;++iy) {
                const auto at=4*(512*iy+left);
                std::copy_n(vram.data()+offset+at,4*(right-left),clipped_.data()+at);
            }
        }
        auto& layer=scrollable?clipped_:vram;
        const auto layer_offset=scrollable?0:offset;
        int row_offset=0;
        for (std::size_t row=0;row<rows.size();++row) {
            y=int(float(view_y+row_offset)-(scrollable?scroll_:0.f));
            const bool selected=row==cursor;
            const int row_height=int(rows[row].size())*row_step;
            row_offset+=row_height;
            if (scrollable && (y+row_height<view_y || y>view_y+height)) continue;
            if (row)
                success=select_.Draw(3,controls,layer,layer_offset,{float(x-240),float(y-136-row_step)}) && success;
            if (selected) {
                // The source fills the selected text rectangle before its
                // FONT draw, preserving the final four-pixel separator gap.
                for (int iy=std::max(0,y);iy<std::min(272,y+row_height-4);++iy) {
                    for (int ix=std::max(0,x);ix<std::min(480,x+row_width);++ix) {
                        auto* out=layer.data()+layer_offset+4*(512*iy+ix);
                        out[0]=234; out[1]=125; out[2]=126;
                    }
                }
            }
            const std::array<unsigned,3> ink=selected?std::array<unsigned,3>{253,253,253}:
                                                      std::array<unsigned,3>{85,47,13};
            for (const auto& label:rows[row]) {
                DrawUiText(label,x,y,font,layer,layer_offset,ink);
                y+=row_step;
            }
        }
        if (scrollable) {
            for (int iy=top;iy<bottom;++iy) {
                const auto at=4*(512*iy+left);
                std::copy_n(layer.data()+at,4*(right-left),vram.data()+offset+at);
            }
        }
    };
    return select_.Draw(2,controls,vram,offset,{},items,{{43,{},0.f,0.f}}) && success;
}
}
