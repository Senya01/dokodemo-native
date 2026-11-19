#include "room_item_placement.h"
#include "recovered/rng_native.h"
#include <fstream>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
unsigned ReadHalf(std::istream& in) {
    unsigned char bytes[2];
    if (!in.read(reinterpret_cast<char*>(bytes), sizeof(bytes)))
        throw std::runtime_error("Truncated gift layout");
    return bytes[0] + 256u * bytes[1];
}

}

GiftPlacementLayout ReadGiftPlacementLayout(const std::filesystem::path& directory,unsigned item) {
    std::ifstream masks(directory / "gift-placement-masks.bin",std::ios::binary);
    masks.seekg(4*item);
    const unsigned low = ReadHalf(masks);
    const unsigned mask = low | (ReadHalf(masks)<<16);
    std::ifstream input(directory / ("gift-" + std::to_string(item) + ".layout"),std::ios::binary);
    if (ReadHalf(input) != item) throw std::runtime_error("Gift layout ID mismatch");
    GiftPlacementLayout layout{ReadHalf(input),{}};
    const unsigned count = ReadHalf(input);
    for (unsigned bit=0; bit<20; ++bit) {
        if (!(mask & (1u<<bit))) continue;
        auto& loc=layout.locations.emplace_back();
        loc.spot=bit;
        loc.selector=ReadHalf(input);
        loc.half_span=ReadHalf(input);
        if (loc.selector>9 || 2*loc.half_span<layout.width) throw std::runtime_error("Invalid room gift span");
    }
    if (!count || !layout.width || layout.locations.size()!=count || input.peek()!=std::char_traits<char>::eof())
        throw std::runtime_error("Invalid gift layout extent");
    return layout;
}

bool RoomItemPlacement::Load(const std::filesystem::path& directory, unsigned item_id,
                             recovered::RngNativeOwner& rng, std::string& error) try {
    if (item_id_ >= 0 && unsigned(item_id_) == item_id) return true;
    if (item_id_ >= 0 || item_id >= 208 || !rng.Available()) throw std::runtime_error("Invalid first-gift placement");
    auto layout = ReadGiftPlacementLayout(directory,item_id);
    const auto& layouts=layout.locations;
    const unsigned width=layout.width;
    const auto count=layouts.size();
    // KSC 3569 with one newly received row and no prior placed items.
    // Every allowed candidate consumes a coin, including the first. Empty
    // spans tie at zero cost. The following two weights divide their slack.
    unsigned chosen = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (rng.NextC3234() % 2)
            chosen = i;
    }
    const int first = 1 + int(rng.NextC3234() % 20);
    const int last = 1 + int(rng.NextC3234() % 20);
    const int half = int(layouts[chosen].half_span);
    const int x = -half + (2 * half - int(width)) * first / (first + last) + int(width) / 2;
    layout_index_=chosen;
    return LoadSaved(directory,item_id,layouts[chosen].selector,x,error);
} catch (const std::exception& ex) {
    error=ex.what();
    return false;
}

bool RoomItemPlacement::LoadSaved(const std::filesystem::path& directory,unsigned item,unsigned selector,int x,
                                  std::string& error) {
    if (selector>9) { error="Unsupported gift model attachment"; return false; }
    RoomMesh next;
    if (!next.Load(directory / ("gift-"+std::to_string(item)+".bin"),error)) return false;
    mesh_=std::move(next);
    selector_=selector;
    x_=x;
    item_id_=int(item);
    clip_time_=0;
    return true;
}

bool RoomItemPlacement::Draw(const RoomMesh& room, const std::vector<std::pair<unsigned,uint32_t>>& clips,
                             const std::array<float,16>& camera, std::vector<uint8_t>& vram, size_t offset,
                             const RoomLighting* lights) {
    if (item_id_ < 0)
        return true;
    // BOOT 4865C room bone lookup. Selector zero has no parent. Translate
    // along the selected parent's local X before the gift's authored pose.
    constexpr unsigned nodes[]{54,55,56,141,61,57,58,59,60};
    std::array<float,16> basis{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    if (selector_ && !room.NodeWorld(nodes[selector_-1], clips, basis))
        return false;
    for (unsigned row = 0; row < 4; ++row)
        basis[12+row] += basis[row] * float(x_);
    const uint32_t duration = mesh_.Duration(0);
    if (!duration)
        return mesh_.Draw({}, basis, camera, vram, offset, lights);
    if (!mesh_.Draw({{0,clip_time_}}, basis, camera, vram, offset, lights))
        return false;
    clip_time_ = (clip_time_ + 160) % duration;
    return true;
}
} // namespace dokodemo::gameplay
