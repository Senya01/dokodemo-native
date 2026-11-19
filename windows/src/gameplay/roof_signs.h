#pragma once
#include "pokepi_raster.h"
#include "post_profile_mesh.h"
#include <optional>

namespace dokodemo::gameplay {
class RoofSigns {
public:
    bool Load(const std::filesystem::path& dir, unsigned resource,
              const std::array<std::optional<std::string>,8>& labels, std::string& error);
    bool Draw(const RoomMesh& background, const std::vector<std::pair<unsigned,uint32_t>>& clips,
              const std::array<float,16>& camera, std::vector<uint8_t>& vram, size_t offset) const;
private:
    struct Sign {
        unsigned node, diffuse;
        RoomMesh panel;
        std::vector<PokepiTexture> glyphs;
    };
    std::vector<Sign> signs_;
};
}
