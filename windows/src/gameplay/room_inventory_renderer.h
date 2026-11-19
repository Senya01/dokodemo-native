#pragma once
#include "cb2a_scene.h"

namespace dokodemo::recovered { class PackageView; }
namespace dokodemo::gameplay {
class RoomInventory;

class RoomInventoryRenderer {
public:
    bool Load(const std::filesystem::path& directory, std::string& error);
    bool Draw(const RoomInventory& inventory, const recovered::PackageView& font,
              std::vector<std::uint8_t>& vram, std::size_t offset) const;
private:
    Cb2aScene list_,select_,question_;
};
}
