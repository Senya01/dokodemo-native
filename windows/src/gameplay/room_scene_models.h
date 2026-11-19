#pragma once
#include "room_item_placement.h"

namespace dokodemo::gameplay {
class RoomInventory;
class RoomSceneRows;
class RoomCalendarItems;

// BG_ROOM's item manager (495FC). The background and actor have separate owners.
class RoomSceneModels {
public:
    bool Bind(const std::filesystem::path& directory, RoomCalendarItems& calendar,
              RoomSceneRows& scene, RoomInventory& inventory, std::string& error);
    bool PlaceGift(const std::filesystem::path& media, RoomCalendarItems& calendar, RoomInventory& inventory,
                   int slot, recovered::RngNativeOwner& rng, std::string& error);
    bool StoreGift(RoomCalendarItems& calendar, RoomInventory& inventory, int slot, std::string& error);
    bool DrawGifts(const RoomMesh& room, const std::vector<std::pair<unsigned,uint32_t>>& clips,
                   const std::array<float,16>& view, std::vector<uint8_t>& vram, size_t offset,
                   const RoomLighting* lighting);
    void Clear() { *this = {}; }
    bool bound() const { return bound_; }
    bool Draw(const RoomMesh& room, const std::vector<std::pair<unsigned,uint32_t>>& clips,
              const std::array<float,16>& view, std::vector<uint8_t>& vram, size_t offset,
              const RoomLighting* lighting) const;
private:
    struct Model {
        RoomMesh mesh;
        RoomItemPlacement gift;
        int inventory_slot = -1;
        int item = -1, parent = 0, x = 0;
    };
    std::array<Model,100> slots_{};
    std::filesystem::path directory_;
    bool bound_ = false;

    int Free() const;
    int LoadModel(const std::array<uint32_t,10>& row);
    void HideCalendar(RoomCalendarItems& calendar, int spot, bool mounted);
    void RegisterGift(RoomInventory& inventory, int slot);
    void ReleaseGift(RoomInventory& inventory, int slot);
};
}
