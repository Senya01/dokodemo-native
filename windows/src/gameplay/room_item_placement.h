#pragma once
#include "post_profile_mesh.h"

namespace dokodemo::recovered { class RngNativeOwner; }

namespace dokodemo::gameplay {
struct GiftPlacementLayout {
    struct Location { unsigned spot, selector, half_span; };
    unsigned width;
    std::vector<Location> locations;
};
GiftPlacementLayout ReadGiftPlacementLayout(const std::filesystem::path& directory, unsigned item);

class RoomItemPlacement {
public:
    bool Load(const std::filesystem::path& directory, unsigned item_id,
              recovered::RngNativeOwner& rng, std::string& error);
    bool LoadSaved(const std::filesystem::path& directory,unsigned item,unsigned selector,int x,std::string& error);
    void Move(int x) { x_ = x; }
    void Clear() { *this = {}; }
    bool Draw(const RoomMesh& room, const std::vector<std::pair<unsigned,uint32_t>>& clips,
              const std::array<float,16>& camera, std::vector<uint8_t>& vram, size_t offset,
              const RoomLighting* lights);
    int item_id() const { return item_id_; }
    unsigned selector() const { return selector_; }
    unsigned layout_index() const { return layout_index_; }
    int local_x() const { return x_; }

private:
    RoomMesh mesh_;
    int item_id_ = -1;
    unsigned selector_ = 0;
    unsigned layout_index_ = 0;
    int x_ = 0;
    uint32_t clip_time_ = 0;
};
} // namespace dokodemo::gameplay
