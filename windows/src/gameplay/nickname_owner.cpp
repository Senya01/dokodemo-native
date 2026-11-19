#include "nickname_owner.h"
#include "title_renderer.h"
#include "core_hash.h"
#include "recovered/game_primitives.h"
#include "recovered/package_storage_native.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace dokodemo::gameplay {
namespace {
// BOOT 14490, selected actor's initial TITLE+80. The keyboard displays and
// edits this value in its own copy until its close animation has completed.
constexpr std::array<const char*,5> default_names{
    "\x83\x67\x83\x8d", "\x83\x58\x83\x59\x83\x4c",
    "\x83\x57\x83\x85\x83\x93", "\x83\x73\x83\x47\x81\x5b\x83\x8b",
    "\x83\x8a\x83\x62\x83\x4c\x81\x5b"
};

std::optional<unsigned> CountUnits(std::string_view text) {
    unsigned count=0;
    for (std::size_t i=0;i<text.size();) {
        const auto* p=reinterpret_cast<const std::uint8_t*>(text.data()+i);
        recovered::Glyph glyph;
        if (!recovered::read_glyph(p,text.size()-i,glyph) || glyph.index<32 ||
            glyph.index>=8192+32 || (p[0]>=0x7f &&
            (!(p[0]>=0x81 && p[0]<=0x9f) && !(p[0]>=0xe0 && p[0]<=0xfc))))
            return {};
        if (glyph.consumed==2 && (p[1]<0x40 || p[1]==0x7f || p[1]>0xfc)) return {};
        i+=glyph.consumed;
        ++count;
    }
    return count;
}

// 222E0/221FC draws actual FONT.XFT cells with a fixed 16-pixel advance
// inside the clip beginning at SOFTKEY node 42's transformed origin.
using Ink=std::array<unsigned,3>;
constexpr Ink brown{85,47,13}, highlight{227,99,79}, white{253,253,253};
struct TextClip { int left=0,top=0,right=480,bottom=272; };
void DrawText(std::string_view text, float x, int y, const recovered::PackageView& font,
              std::vector<std::uint8_t>& vram, std::size_t offset, bool centered=false,
              Ink ink=brown, bool variable_advance=false,float scale=1.f,TextClip clip={},
              float vertical_scale=1.f,float spacing=2.f) {
    for (std::size_t i=0;i<text.size();) {
        recovered::Glyph glyph;
        if (!recovered::read_glyph(reinterpret_cast<const std::uint8_t*>(text.data()+i),
                                   text.size()-i,glyph) || glyph.index<32 || glyph.index>=8224) return;
        i+=glyph.consumed;
        const unsigned slot=glyph.index-32;
        const unsigned page=slot/256, u=(slot%16)*16, v=(slot%256/16)*16;
        const unsigned width=std::min<unsigned>(16,font.ReadU8(0x3c+slot));
        const float left=centered?x-width*scale*.5f:x;
        const int origin=static_cast<int>(left);
        const int end=static_cast<int>(left+width*scale);
        const int height=static_cast<int>(y+16*vertical_scale)-y;
        for (int sy=y;sy<y+height;++sy) for (int sx=origin;sx<end;++sx) {
            if (sx<clip.left || sx>=clip.right || sy<clip.top || sy>=clip.bottom) continue;
            const auto opacity=[&](unsigned col,unsigned row) {
                const unsigned pixel=(v+row)*256+u+col;
                const unsigned pair=font.ReadU8(0x2040+page*32768+pixel/2);
                return font.ReadU8(0x102040+4*((pair>>(4*(pixel%2)))&15)+3);
            };
            const float texel=std::clamp((sx-origin+.5f)*width/(end-origin)-.5f,0.f,float(width-1));
            const unsigned a=static_cast<unsigned>(texel);
            const float vpos=std::clamp((sy-y+.5f)*16/height-.5f,0.f,15.f);
            const auto b=static_cast<unsigned>(vpos);
            const auto coverage=[&](unsigned row) {
                const int first=opacity(a,row);
                return texel>a?first+(texel-a)*(opacity(a+1,row)-first):float(first);
            };
            float value=coverage(b);
            if (vpos>b) value+=(vpos-b)*(coverage(b+1)-value);
            const auto alpha=static_cast<unsigned>(value+.5f);
            auto* out=vram.data()+offset+4*(sy*512+sx);
            for (unsigned channel=0;channel<3;++channel)
                out[channel]=std::uint8_t((ink[channel]*alpha+out[channel]*(255-alpha)+127)/255);
        }
        // 93EB8 truncates the scaled glyph width before 93E4C adds the
        // separate floating-point letter spacing to the drawing pen.
        x+=variable_advance?static_cast<int>(width*scale)+spacing*scale:16.f*scale;
    }
}
}

float MeasureUiText(std::string_view text,const recovered::PackageView& font,float scale,float spacing) {
    float width=0;
    for (std::size_t i=0;i<text.size();) {
        recovered::Glyph g;
        if (!recovered::read_glyph(reinterpret_cast<const std::uint8_t*>(text.data()+i),
                text.size()-i,g) || g.index<32 || g.index>=8224) break;
        width+=static_cast<int>(font.ReadU8(0x3c+g.index-32)*scale)+spacing*scale;
        i+=g.consumed;
    }
    return width>0?width-spacing*scale:0.f;
}

void DrawUiText(std::string_view text,int left,int y,const recovered::PackageView& font,
                std::vector<std::uint8_t>& vram,std::size_t offset,std::array<unsigned,3> ink,
                float scale,std::array<int,4> clip,float spacing) {
    if (offset>vram.size() || 4*(512*271+480)>vram.size()-offset || !(scale>0 && scale<=1.f)) return;
    DrawText(text,float(left),y,font,vram,offset,false,ink,true,scale,
             {std::max(clip[0],0),std::max(clip[1],0),std::min(clip[2],480),std::min(clip[3],272)},scale,spacing);
}

void DrawConfirmationText(std::string_view text,int left,int y,float scale,
                          const recovered::PackageView& font,std::vector<std::uint8_t>& vram,
                          std::size_t offset) {
    if (offset>vram.size() || 4*(512*271+480)>vram.size()-offset || !(scale>0 && scale<=1.f)) return;
    DrawText(text,float(left),y,font,vram,offset,false,brown,true,scale,{},scale);
}

void DrawProfileField(std::string_view text,int center_x,int y,bool active,
                      const recovered::PackageView& font,std::vector<std::uint8_t>& vram,
                      std::size_t offset,float horizontal_scale,int clip_top,int clip_bottom) {
    if (offset>vram.size() || 4*(512*271+480)>vram.size()-offset) return;
    unsigned advance=0;
    for (std::size_t i=0;i<text.size();) {
        recovered::Glyph g;
        if (!recovered::read_glyph(reinterpret_cast<const std::uint8_t*>(text.data()+i),
                text.size()-i,g) || g.index<32 || g.index>=8224) return;
        const int scaled=static_cast<int>(font.ReadU8(0x3c+g.index-32)*horizontal_scale);
        advance+=static_cast<unsigned>(scaled+2.f*horizontal_scale);
        i+=g.consumed;
    }
    // Font spacing separates adjoining glyphs. The last glyph ends at its
    // ink width when EC68 centers the label (same convention as one cell).
    if (advance) advance-=static_cast<unsigned>(2.f*horizontal_scale);
    DrawText(text,center_x-advance*.5f,y,font,vram,offset,
             false,active?white:brown,true,horizontal_scale,
             {0,std::max(clip_top,0),480,std::min(clip_bottom,272)});
}

bool NicknameEditor::Load(const std::filesystem::path& directory,std::string& error) {
    std::array<std::uint8_t,380> data{};
    std::ifstream file(directory/"nickname-groups.bin",std::ios::binary|std::ios::ate);
    if (!file || file.tellg()!=std::streamoff(data.size()) ||
        !file.seekg(0).read(reinterpret_cast<char*>(data.data()),data.size()) ||
        core::sha256_hex(data.data(),data.size())!=
            "9c9974559e2534628fcf743a426a5629782161e4914a1174cf069b2e15e44348") {
        error="Incorrect SOFTKEY nickname group strings"; return false;
    }
    // 202FC/203F0 split each NUL-terminated BOOT string into rows and
    // original font units. Tabs are the source keyboard's empty cells.
    std::size_t p=0;
    for (auto& group:groups_) {
        group={};
        group.rows.emplace_back();
        while (data.at(p)) {
            if (data[p]=='\n') { ++p; group.rows.emplace_back(); continue; }
            const unsigned size=data[p]<0x80?1:2;
            group.rows.back().emplace_back(reinterpret_cast<const char*>(data.data()+p),size);
            group.columns=std::max(group.columns,static_cast<unsigned>(group.rows.back().size()));
            p+=size;
        }
        ++p;
    }
    error.clear();
    return true;
}

void NicknameEditor::Open(unsigned actor,std::uint32_t buttons) {
    Begin(default_names.at(actor),buttons);
}

void NicknameEditor::OpenPlayer(std::uint32_t buttons) { Begin("",buttons); }

void NicknameEditor::OpenName(std::string_view previous,std::uint32_t buttons) { Begin(previous,buttons); }
void NicknameEditor::OpenPhone(std::string_view previous,std::uint32_t buttons) { Begin(previous,buttons,true); }

void NicknameEditor::Begin(std::string_view initial,std::uint32_t buttons,bool phone) {
    phone_=phone;
    text_scroll_=0;
    se_mask_=1u<<10; // 1FFFC
    value_=initial;
    result_.reset();
    group_.reset();
    pressed_clip_=pressed_frame_=0;
    phase_=Phase::opening;
    frame_=0;
    row_=column_=1;
    held_.fill(0);
    previous_=buttons;
}

bool NicknameEditor::AppendCommitted(std::string_view bytes) {
    if (!editing() || bytes.empty()) return false;
    if (phone_ && bytes.find_first_not_of("0123456789-")!=std::string_view::npos) return false;
    const auto count=CountUnits(bytes);
    if (!count || *count+CountUnits(value_).value()>(phone_?13u:4u)) return false;
    value_.append(bytes);
    return true;
}

bool NicknameEditor::EraseLast() {
    if (!editing() || value_.empty()) return false;
    std::size_t i=0,last=0;
    while (i<value_.size()) {
        last=i;
        i+=static_cast<unsigned char>(value_[i])<0x80?1:2;
    }
    value_.resize(last);
    return true;
}

void NicknameEditor::Select() {
    if (group_) {
        const auto& cells=groups_[*group_].rows[kana_y_];
        if (kana_x_<cells.size() && cells[kana_x_]!="\t" && AppendCommitted(cells[kana_x_])) {
            group_.reset(); // 21CA0 -> 205BC, type1 immediately commits.
            se_mask_=1u<<4;
        } else se_mask_=1u<<9;
        return;
    }
    // 219E0 starts the key's control before 215D0 dispatches the button.
    pressed_clip_=row_<4?4:7+column_;
    pressed_frame_=row_<4?1:2;
    if (row_!=4) {
        const auto index=row_*3+column_;
        if (phone_) {
            // BOOT 22AC8 -> 200B8 -> 202FC, type 3 first page. Each enabled
            // cell is a one-by-one direct character, so no kana subpicker.
            constexpr std::string_view keypad="123456789-0";
            se_mask_=index<keypad.size() && AppendCommitted(keypad.substr(index,1))?1u<<4:1u<<9;
            return;
        }
        if (index>=groups_.size() || !groups_[index].columns || *CountUnits(value_)>=4) {
            se_mask_=1u<<9; return;
        }
        se_mask_=1u<<4;
        group_=index; kana_x_=kana_y_=0;
        return;
    }
    if (column_==1) { se_mask_=(1u<<4)|(EraseLast()?0u:1u<<9); return; }
    if (column_==0) value_.clear();
    se_mask_=(1u<<4)|(1u<<(column_==0?11:8));
    phase_=Phase::closing;
    frame_=0;
}

void NicknameEditor::Step(std::uint32_t buttons) {
    const auto pressed=buttons&~previous_;
    previous_=buttons;
    se_mask_=0;
    if (pressed_clip_ && ++pressed_frame_ >= (pressed_clip_==4?4u:6u)) pressed_clip_=0;
    if (phase_==Phase::opening || phase_==Phase::closing) {
        if (++frame_==9) { // clip 17/18 end frame, source control.mode -> 2
            if (phase_==Phase::opening) phase_=Phase::editing;
            else { phase_=Phase::idle; result_=value_; }
        }
        return;
    }
    if (!editing()) return;
    constexpr unsigned dirs[]{0x40,0x10,0x20,0x80};
    std::array<bool,4> moves{};
    for (unsigned i=0;i<4;++i) {
        if (!(buttons&dirs[i])) {held_[i]=0; continue;}
        const auto n=held_[i];
        moves[i]=n==0 || (n>=10 && (n-10)%2==0); // 94F90(held,10,2)
        held_[i]=n<12?n+1:11;
    }
    if (pressed_clip_) return; // 20F14 waits until active pressed controls finish.
    const auto old_x=column_, old_y=row_, old_kx=kana_x_, old_ky=kana_y_;
    if (pressed&0x2000) Select();
    else if (group_) {
        const auto& group=groups_[*group_];
        const unsigned h=static_cast<unsigned>(group.rows.size()), w=group.columns;
        if (pressed&0x4000) { group_.reset(); se_mask_=1u<<5; }
        else if (moves[0] && (kana_y_+1<h || (pressed&dirs[0]))) kana_y_=(kana_y_+1)%h;
        else if (moves[1] && (kana_y_ || (pressed&dirs[1]))) kana_y_=(kana_y_+h-1)%h;
        else if (moves[2] && (kana_x_+1<w || (pressed&dirs[2]))) kana_x_=(kana_x_+1)%w;
        else if (moves[3] && (kana_x_ || (pressed&dirs[3]))) kana_x_=(kana_x_+w-1)%w;
    }
    // 21190 tests input+4C, logical bit7. 52E38 maps raw Square to it.
    else if (pressed&0x8000) {
        if (column_==2 && row_==4) Select();
        else {column_=2; row_=4;}
    } else if (pressed&0x4000) {
        pressed_clip_=8; pressed_frame_=2; // shortcut flashes the delete key
        se_mask_=(1u<<7)|(EraseLast()?0u:1u<<9);
    }
    else if (moves[0] && (row_<4 || (pressed&dirs[0]))) row_=(row_+1)%5;
    else if (moves[1] && (row_>0 || (pressed&dirs[1]))) row_=(row_+4)%5;
    else if (moves[2] && column_<2) ++column_;
    else if (moves[3] && column_>0) --column_;
    if (old_x!=column_ || old_y!=row_ || (group_ && (old_kx!=kana_x_ || old_ky!=kana_y_)))
        se_mask_=1u<<6;
}

bool NicknameEditor::Draw(const TitleRenderer& renderer,const recovered::PackageView& font,
                          std::vector<std::uint8_t>& vram,std::size_t offset) {
    if (phase_==Phase::idle) return true;
    // 1FD80's controls, followed by 21898's type1/base-key/bottom frames.
    const bool group_visible=group_ && !pressed_clip_;
    const unsigned group_frame=group_?groups_[*group_].columns+5*unsigned(groups_[*group_].rows.size()-1):1;
    std::vector<UiClipFrame> controls{
        {0,phone_?5u:8u},{1,row_==4?13:1+column_+3*row_},{6,1},{11,1},{5,group_visible?*group_+1:13},{14,group_frame},
        {7,1u+unsigned(row_==4&&column_==0)}, {8,1u+unsigned(row_==4&&column_==1)},
        {9,1u+unsigned(row_==4&&column_==2)}, {10,1},{13,1},{12,1}
    };
    if (pressed_clip_==4) controls.insert(controls.begin()+6,{4,pressed_frame_});
    else if (pressed_clip_) controls[6+pressed_clip_-7].frame=pressed_frame_;
    std::vector<UiNodeOverride> bindings{{9,std::nullopt,0.f,0.f,{}}};
    if (group_visible) bindings.push_back({161,{},{},{},5u});
    return renderer.RenderSoftkey(phase_==Phase::closing?18:17,frame_,controls,
        bindings,[&](unsigned node,int x,int y){
            if (node==42) {
                const int count=static_cast<int>(CountUnits(value_).value());
                // 22484/225E8 reserve space for a new character unless the
                // direct committed buffer has reached its source maximum.
                const int target=count<=7?0:16*((phone_ && count==13?8:7)-count);
                text_scroll_+=(float(target)-text_scroll_)*.125f;
                DrawText(value_,x+text_scroll_,y,font,vram,offset,false,brown,false,1.f,
                    {std::max(0,x),std::max(0,y),std::min(480,x+128),std::min(272,y+16)});
            }
            if (node==251 && group_visible) {
                const auto& group=groups_[*group_];
                for (unsigned row=0;row<group.rows.size();++row) {
                    for (unsigned col=0;col<group.columns;++col) {
                        const bool selected=col==kana_x_ && row==kana_y_;
                        const auto& cells=group.rows[row];
                        std::string_view cell=col<cells.size()?cells[col]:std::string_view{};
                        if (cell.empty() || cell=="\t") cell=selected?"_":"";
                        DrawText(cell,x+int(col)*16+8,y+int(row)*17,font,vram,offset,
                                 true,selected?highlight:brown);
                    }
                }
            }
        },vram,offset);
}
}
