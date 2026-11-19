#include "roof_signs.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
void Require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
unsigned Byte(std::istream& in) {
    const int c = in.get();
    Require(c != std::char_traits<char>::eof(), "Incomplete roof sign resource");
    return static_cast<unsigned>(c);
}
uint32_t Word(std::istream& in) {
    uint32_t value = 0;
    for (unsigned i=0; i<4; ++i) value |= uint32_t(Byte(in)) << (8*i);
    return value;
}
std::string Field(std::istream& in) {
    const auto size = Byte(in);
    std::string result(size, '\0');
    Require(bool(in.read(result.data(), size)), "Incomplete roof sign text field");
    return result;
}
std::vector<unsigned> TextUnits(const std::string& src, const std::array<std::string,128>& mapping) {
    std::string text;
    for (size_t i=0; i<src.size();) {
        const auto c = static_cast<unsigned char>(src[i]);
        if (c < 128) {
            text += mapping[c];
            ++i;
        } else {
            Require(i+1 < src.size() && src[i+1], "Truncated roof sign character");
            text.append(src,i,2);
            i+=2;
        }
    }
    std::vector<unsigned> result;
    for (size_t i=0; i<text.size() && result.size()<8;) {
        unsigned c = static_cast<unsigned char>(text[i++]);
        if (c>=128) {
            Require(i<text.size(), "Truncated normalized roof sign character");
            c = (c<<8) | static_cast<unsigned char>(text[i++]);
        }
        result.push_back(c);
    }
    return result;
}

std::vector<PokepiTexture> Glyphs(const std::filesystem::path& path, const std::vector<unsigned>& codes) {
    std::ifstream input(path, std::ios::binary);
    Require(Word(input)==0x31474652, "Missing or invalid roof sign glyph bank");
    const auto count=Word(input);
    constexpr unsigned stride=2+64+128;
    Require(count>0 && count<=65536, "Invalid roof sign glyph count");
    std::vector<unsigned char> bank(size_t(count)*stride);
    Require(bool(input.read(reinterpret_cast<char*>(bank.data()), bank.size())) &&
            input.peek()==std::char_traits<char>::eof(), "Invalid roof sign glyph bank extent");
    auto key=[&](unsigned i) {
        const auto* ptr=bank.data()+size_t(i)*stride;
        return unsigned(ptr[0]) | (unsigned(ptr[1])<<8);
    };
    std::vector<PokepiTexture> result;
    for (const auto code:codes) {
        unsigned lo=0, hi=count;
        while (lo<hi) {
            const auto mid=lo+(hi-lo)/2;
            if (key(mid)<code) lo=mid+1;
            else hi=mid;
        }
        Require(lo<count && key(lo)==code, "Missing source glyph for current roof sign");
        const auto* palette=bank.data()+size_t(lo)*stride+2;
        const auto* indices=palette+64;
        PokepiTexture texture{16,16,std::vector<uint8_t>(16*16*4)};
        for (unsigned i=0; i<256; ++i) {
            const auto index=(indices[i/2]>>(4*(i%2))) & 15;
            std::copy_n(palette+index*4,4,texture.pixels.begin()+i*4);
        }
        result.push_back(std::move(texture));
    }
    return result;
}
}

bool RoofSigns::Load(const std::filesystem::path& dir, unsigned resource,
                     const std::array<std::optional<std::string>,8>& labels, std::string& error) try {
    std::ifstream input(dir/"roof-signs.bin",std::ios::binary);
    Require(Word(input)==0x31475352, "Missing or invalid roof sign placement data");
    const auto fallback=Field(input);
    std::array<std::string,128> mapping;
    for (unsigned i=0; i<96; ++i) {
        const auto c=Byte(input);
        Require(c<mapping.size(), "Invalid source roof character map");
        mapping[c]=Field(input);
    }
    std::vector<Sign> next;
    bool found=false;
    for (unsigned group=0; group<3; ++group) {
        const unsigned reg=Word(input), count=Word(input);
        Require(count<=64, "Too many roof sign placements");
        found |= reg==resource;
        for (unsigned i=0; i<count; ++i) {
            const unsigned node=Word(input), cell=Word(input), mode=Word(input);
            const unsigned style=Word(input), diffuse=Word(input);
            if (reg!=resource) continue;
            Require(cell<labels.size() && (mode==1 || mode==2), "Unknown roof sign cell or layout");
            const auto units=TextUnits(labels[cell].value_or(fallback),mapping);
            if (units.empty()) continue;
            const unsigned panel=unsigned(units.size())+(mode==1?1:9);
            Sign sign{node,diffuse,{},Glyphs(dir/("roof-sign-font-"+std::to_string(style)+".bin"),units)};
            if (!sign.panel.Load(dir/("roof-sign-panel-"+std::to_string(panel)+".mesh"),error)) return false;
            next.push_back(std::move(sign));
        }
    }
    Require(found && input.peek()==std::char_traits<char>::eof(), "Missing current roof sign registration");
    signs_=std::move(next);
    return true;
} catch (const std::exception& ex) { error=ex.what(); return false; }

bool RoofSigns::Draw(const RoomMesh& background, const std::vector<std::pair<unsigned,uint32_t>>& clips,
                     const std::array<float,16>& camera, std::vector<uint8_t>& vram, size_t offset) const {
    for (const auto& sign:signs_) {
        std::array<float,16> world;
        if (!background.SignWorld(sign.node,clips,world) ||
            !sign.panel.DrawSign(world,camera,sign.glyphs,sign.diffuse,vram,offset)) return false;
    }
    return true;
}
}
