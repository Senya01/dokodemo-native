#pragma once

#include "post_profile_mesh.h"
#include <optional>

namespace dokodemo::gameplay {

class RoomMotionEffects {
public:
    bool Spawn(const std::filesystem::path& media, const RoomMotionEffectSpawn& spawn);
    std::array<unsigned,14> Draw(const std::array<float,16>& view, std::vector<std::uint8_t>& vram, std::size_t offset);
    void Clear() { active_.clear(); }

private:
    struct Instance {
        RoomMotionEffectSpawn spawn;
        unsigned tick;
    };
    std::array<std::optional<RoomMesh>,14> meshes_;
    std::vector<Instance> active_;
};

} // namespace dokodemo::gameplay
