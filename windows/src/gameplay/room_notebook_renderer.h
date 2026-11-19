#pragma once
#include "cb2a_scene.h"

namespace dokodemo::recovered { class PackageView; }
namespace dokodemo::gameplay {
class RoomNotebook;

class RoomNotebookRenderer {
public:
    bool Load(const std::filesystem::path& game_directory, std::string& error);
    bool Draw(const RoomNotebook&, const recovered::PackageView& font,
              std::vector<std::uint8_t>& vram, std::size_t offset) const;
private:
    Cb2aScene memo_, select_, question_;
};
}
