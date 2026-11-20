#include "room_motion_effects.h"
#include <algorithm>
#include <stdexcept>

namespace dokodemo::gameplay {

bool RoomMotionEffects::Spawn(const std::filesystem::path& media, const RoomMotionEffectSpawn& spawn) {
    // 4F30C finds one of twenty instances and checks the descriptor before allocating it.
    const auto resource=spawn.resource;
    if (active_.size()>=20 || !((resource>=1 && resource<=4) || resource==9)) return false;
    auto& mesh=meshes_[resource];
    if (!mesh) {
        RoomMesh candidate;
        std::string error;
        if (!candidate.Load(media/("motion-ef"+std::to_string(resource)+".bin"),error) ||
            !candidate.Duration(0))
            throw std::runtime_error("Cannot load motion effect "+std::to_string(resource)+": "+error);
        mesh=std::move(candidate);
    }
    active_.push_back({spawn,0});
    return true;
}

std::array<unsigned,14> RoomMotionEffects::Draw(const std::array<float,16>& view,
                                               std::vector<std::uint8_t>& vram, std::size_t offset) {
    std::array<unsigned,14> finished{};
    for (auto& effect : active_) {
        const auto& p=effect.spawn.position;
        const std::array<float,16> transform{1,0,0,0, 0,1,0,0, 0,0,1,0, p[0],p[1],p[2],1};
        if (!meshes_[effect.spawn.resource]->Draw({{0,effect.tick}},transform,view,vram,offset))
            throw std::runtime_error("Motion effect draw failed");
        effect.tick+=160;
    }
    const auto remaining=std::remove_if(active_.begin(),active_.end(),[&](const Instance& effect) {
        const auto resource=effect.spawn.resource;
        if (effect.tick<=meshes_[resource]->Duration(0)) return false;
        ++finished[resource];
        return true;
    });
    active_.erase(remaining,active_.end());
    return finished;
}

} // namespace dokodemo::gameplay
