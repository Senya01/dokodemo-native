#include "game_font.h"
#include "recovered/game_primitives.h"
#include <algorithm>
#include <fstream>

namespace native_port {
bool GameFont::Load(const std::filesystem::path& directory, std::wstring& error) {
    loaded_ = false;
    std::ifstream in(directory / L"widths.bin", std::ios::binary);
    if (!in.read(reinterpret_cast<char*>(widths_.data()), widths_.size()) || in.peek() != EOF) { error = L"XFT requires exactly 8192 widths"; return false; }
    for (auto width : widths_) if (width > 16) { error = L"Invalid original XFT width"; return false; }
    for (int page=0;page<32;++page) {
        wchar_t name[32]; swprintf_s(name,L"atlas_%02d.bmp",page);
        if (!LoadBmp(directory / name,pages_[page],error)) return false;
        if (pages_[page].width != 256 || pages_[page].height != 256) { error = L"Invalid XFT page dimensions"; return false; }
    }
    loaded_=true; error.clear(); return true;
}
bool GameFont::RasterizePlainCp932(const std::string& text, Image& image, std::wstring& error) const {
    if (!loaded_) { error = L"Original XFT not loaded"; return false; }
    struct Position { uint32_t index; int x,y; };
    std::vector<Position> glyphs;
    int x=0,y=0,width=1; size_t pos=0;
    while (pos<text.size()) {
        auto lead=static_cast<uint8_t>(text[pos]);
        if (lead == 0x0a) {
            if(y>4096-16-18) {error=L"Font preview exceeds dimension budget";return false;}
            width=std::max(width,x); x=0; y+=18; ++pos; continue;
        }
        // Display-control expansion/timing belongs to the restored text renderer.
        if (lead<0x20 || lead==0x7f || (lead>=0xa0 && lead<0xe0)) { error=L"Unsupported control or CP932 sequence in plain font preview"; return false; }
        dokodemo::recovered::Glyph glyph{};
        if (!dokodemo::recovered::read_glyph(reinterpret_cast<const uint8_t*>(text.data()+pos),text.size()-pos,glyph)) { error=L"Truncated CP932 font input"; return false; }
        if (glyph.consumed==2) {
            auto trail=static_cast<uint8_t>(text[pos+1]);
            if (!((lead>=0x81 && lead<=0x9f)||(lead>=0xe0 && lead<=0xfc)) || trail<0x40 || trail>0xfc || trail==0x7f) { error=L"Invalid CP932 font input"; return false; }
        }
        if (glyph.index<32 || glyph.index-32>=widths_.size()) { error=L"CP932 glyph outside original XFT"; return false; }
        auto index=glyph.index-32;
        glyphs.push_back({index,x,y}); x+=widths_[index]+2; pos+=glyph.consumed;
        if (x>4096 || y>4096-16) { error=L"Font preview exceeds dimension budget"; return false; }
    }
    width=std::max(width,x); int height=y+16;
    Image result; result.width=width; result.height=height;
    result.bgra.resize(size_t(width)*height*4);
    for (const auto& glyph:glyphs) {
        auto& page=pages_[glyph.index/256]; auto slot=glyph.index%256;
        int sx=int(slot%16)*16,sy=int(slot/16)*16;
        for (int gy=0;gy<16;++gy) for(int gx=0;gx<widths_[glyph.index];++gx) {
            auto source=page.bgra.data()+(size_t(sy+gy)*256+sx+gx)*4;
            auto target=result.bgra.data()+(size_t(glyph.y+gy)*width+glyph.x+gx)*4;
            std::copy(source,source+4,target);
        }
    }
    image=std::move(result); error.clear(); return true;
}
}
