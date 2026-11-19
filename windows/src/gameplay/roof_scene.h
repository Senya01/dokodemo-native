#pragma once

#include "post_profile_mesh.h"

namespace dokodemo::recovered { class RngNativeOwner; }

namespace dokodemo::gameplay {

class RoofScene {
public:
    bool Load(const std::filesystem::path& post_profile_dir, unsigned period,
              recovered::RngNativeOwner& rng, std::string& error);
    bool Draw(const std::array<float,16>& camera, unsigned season, std::uint32_t light_time,
              std::vector<std::uint8_t>& vram, std::size_t offset, RoomLighting& lighting,
              bool* changed_pixels = nullptr);

    const RoomMesh& Background() const { return background_; }
    const std::array<float,4>& Placement() const { return placement_; }
    unsigned resource() const { return resource_; }
    const std::vector<std::pair<unsigned,std::uint32_t>>& main_clips() const { return main_clips_; }

private:
    struct Motion {
        bool child = false, loop = false;
        unsigned clip = 0;
        std::uint32_t time = 0, duration = 0;
        unsigned child_phase = 0;
    };

    RoomMesh background_, auxiliary_;
    std::array<float,4> placement_{};
    RoomMaterialVariant child_material_;
    std::vector<Motion> motions_;
    std::vector<std::pair<unsigned,std::uint32_t>> main_clips_;
    unsigned resource_ = 0;
};

} // namespace dokodemo::gameplay
