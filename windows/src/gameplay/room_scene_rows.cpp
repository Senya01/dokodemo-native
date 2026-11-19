#include "room_scene_rows.h"
#include "recovered/rng_native.h"
#include <fstream>
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr std::uint32_t integer = 0x20000000u;
std::uint32_t ReadWord(std::istream& input) {
    unsigned char b[4];
    if (!input.read(reinterpret_cast<char*>(b),4)) throw std::runtime_error("Truncated scene item table");
    return b[0] | (unsigned(b[1])<<8) | (unsigned(b[2])<<16) | (unsigned(b[3])<<24);
}
KscSaveValue Reference(KscSaveValue array) {
    KscSaveValue ref{0xb0000000u,{},{}};
    ref.children.push_back(std::move(array));
    return ref;
}
const auto& Elements(const KscSaveValue& ref, unsigned count) {
    if (ref.word>>28!=11 || !ref.text.empty() || ref.children.size()!=1)
        throw std::invalid_argument("Invalid scene row reference");
    const auto& a=ref.children.front();
    if (a.word!=(0x90000000u|count) || !a.text.empty() || a.children.size()!=count)
        throw std::invalid_argument("Invalid scene row dimensions");
    return a.children;
}
}

void RoomSceneRows::Load(const std::filesystem::path& directory) {
    std::ifstream in(directory/"room-scene-rows.bin",std::ios::binary);
    if (ReadWord(in)!=0x31525347u) throw std::runtime_error("Unknown scene item table");
    auto items=items_;
    for (auto& item:items) {
        item={ReadWord(in),ReadWord(in),ReadWord(in),ReadWord(in),ReadWord(in)};
        if (item.id>>28!=2 || item.selector>>28!=2 || item.value3>>28!=2 ||
            item.placement>>28!=2 || (item.value8 && item.value8>>28!=2))
            throw std::runtime_error("Invalid scene item fields");
    }
    if (in.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Trailing scene item data");
    items_=items;
    loaded_=true;
}

std::array<int,5> RoomSceneRows::Update(int selector, std::uint32_t date,
                                        recovered::RngNativeOwner& rng) {
    if (!loaded_ || !rng.Available()) throw std::logic_error("Scene row update requires room media and RNG");
    std::array<int,5> draws{};
    for (unsigned i=0;i<rows_.size();++i) {
        auto& row=rows_[i];
        const auto& item=items_[i];
        if (item.selector==(integer|(std::uint32_t(selector)&0x0fffffffu))) {
            row={}; // 2105 clears all ten cells of the referenced row.
            draws[i]=-1;
            continue;
        }
        const bool vacant=row[7]==0;
        draws[i]=static_cast<int>(rng.NextC3234()%3); // 3BE5 happens even on occupied rows.
        if (!vacant || draws[i]!=0) continue;

        // 31EF -> 3161. Fields 1 and 2 retain their previous contents.
        row[0]=item.id;
        row[3]=item.value3;
        row[4]=integer|(date&0x0fffffffu);
        row[5]=integer;
        row[6]=0;
        row[7]=0;
        row[8]=0;
        row[9]=integer;
        // All five item definitions have nil at [2], so 31EF returns directly.
        // 3C0A..3C3B take the first set placement bit and its anchor value.
        row[7]=item.placement;
        row[8]=item.value8;
    }
    return draws;
}

void RoomSceneRows::ClearModelHandle(std::size_t row) {
    rows_.at(row)[6]=0; // 3A4F..3A56 before 33D1.
}

void RoomSceneRows::SetModelHandle(std::size_t row, int handle) {
    rows_.at(row)[6]=integer|(std::uint32_t(handle)&0x0fffffffu); // 33F7
}

KscSaveValue RoomSceneRows::SaveTable() const {
    if (!loaded_) throw std::logic_error("Scene row media not loaded for save");
    KscSaveValue table{0x90000005u,{},{}};
    for (const auto& row:rows_) {
        KscSaveValue array{0x9000000au,{},{}};
        for (auto word:row) array.children.push_back({word,{},{}});
        table.children.push_back(Reference(std::move(array)));
    }
    return Reference(std::move(table));
}

void RoomSceneRows::RestoreSaveTable(const KscSaveValue& table) {
    if (!loaded_) throw std::logic_error("Scene row media not loaded for restore");
    const auto& outer=Elements(table,rows_.size());
    Table next{};
    for (unsigned i=0;i<next.size();++i) {
        const auto& source=Elements(outer[i],next[i].size());
        for (unsigned j=0;j<source.size();++j) {
            const auto& cell=source[j];
            if ((cell.word && cell.word>>28!=2) || !cell.children.empty() || !cell.text.empty())
                throw std::invalid_argument("Unsupported saved scene item field");
            next[i][j]=cell.word;
        }
    }
    rows_=next;
}
} // namespace dokodemo::gameplay
