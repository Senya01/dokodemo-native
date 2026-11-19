#include "room_word_traits.h"
#include "core_hash.h"
#include <fstream>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
bool UseFirstGroup(int row,int field12) {
    return row==600 || (field12&0x18)==0;
}
}

bool RoomWordTraits::Load(const std::filesystem::path& directory,std::string& error) {
    std::ifstream file(directory/"toro-trait-labels.bin",std::ios::binary|std::ios::ate);
    std::array<std::uint8_t,655> data{};
    if (!file || file.tellg()!=std::streamoff(data.size()) ||
        !file.seekg(0).read(reinterpret_cast<char*>(data.data()),data.size()) ||
        core::sha256_hex(data.data(),data.size())!="136b09c3207f1904417fce21f5b7b961ae4f900158b9c394d74d81f0c3b955d6") {
        error="Missing or invalid Toro word-trait forms";
        return false;
    }
    std::size_t at=4;
    for (auto& table:forms_) for (auto& text:table) {
        const auto length=data[at] | (unsigned(data[at+1])<<8);
        at+=2;
        text.assign(reinterpret_cast<const char*>(data.data()+at),length);
        at+=length;
    }
    return true;
}

std::vector<unsigned> RoomWordTraits::Unasked(int row,int field12,int field15) {
    std::vector<unsigned> indices;
    const unsigned excluded=UseFirstGroup(row,field12)?3:0;
    for (unsigned i=0;i<22;++i) {
        if (i>=excluded && i<excluded+3) continue;
        if (!(unsigned(field15)&(1u<<i))) indices.push_back(i);
    }
    return indices;
}

unsigned RoomWordTraits::Repeated(int row,int field12,unsigned draw) {
    if (draw>=22) throw std::out_of_range("Invalid trait draw");
    if (UseFirstGroup(row,field12)) return draw>=3 && draw<=5 ? draw-3 : draw;
    return draw<3 ? draw+3 : draw;
}

bool RoomWordTraits::IsTextTable(unsigned table) {
    return table==0x2a9e || table==0x2ab5 || table==0x2acc;
}

const std::string& RoomWordTraits::Label(unsigned table,unsigned index) const {
    if (!IsTextTable(table)) throw std::invalid_argument("Unknown KSC trait form table");
    const auto& text=forms_[table==0x2a9e?0:table==0x2ab5?1:2].at(index);
    if (text.empty()) throw std::out_of_range("Unavailable KSC trait form");
    return text;
}

} // namespace dokodemo::gameplay
