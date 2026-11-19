#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dokodemo::gameplay {
struct PokepiTexture;
struct RoomLight {
    std::array<float, 3> position{}, color{};
    std::array<float, 3> spot_direction{};
    float spot_exponent = -1, quadratic_attenuation = 0;
    float SpotFactor(const std::array<float,3>& unit_to_light, float distance_squared) const;
};

struct RoomLighting {
    std::array<float, 3> ambient{};
    std::vector<RoomLight> points;
};

struct RoomPoseBlend {
    unsigned clip, time;
    float weight;
    int previous_secondary = -1;
    unsigned previous_secondary_time = 0;
};

struct RoomJointLayer {
    const std::vector<unsigned> &nodes;
    int clip;
    unsigned time;
};

struct RoomMaterialVariant {
    std::int32_t node = -1;
    std::array<std::int32_t,4> source_materials{};
    unsigned phase = 0;
};

struct RoomActorRegion {
    bool inside;
    std::array<float,2> target;
};

class RoomMesh {
  public:
    bool Load(const std::filesystem::path &path, std::string &error, bool build_actor_regions = false);
    bool NodeWorld(unsigned node, const std::vector<std::pair<unsigned, uint32_t>>& active_clips,
                   std::array<float,16>& out, const RoomPoseBlend* blend = nullptr, const RoomJointLayer* layer = nullptr) const;
    bool ContainsActor(const std::array<float,2>& point) const;
    RoomActorRegion QueryActorRegion(const std::array<float,2>& point) const;
    bool DirectActorRoute(const std::array<float,2>& from, const std::array<float,2>& to) const;
    RoomLighting Lighting(unsigned clip, uint32_t time) const { return Lighting({{clip, time}}); }
    RoomLighting Lighting(const std::vector<std::pair<unsigned, uint32_t>>& active_clips) const;
    std::vector<std::pair<unsigned,uint32_t>> SoundKeys(unsigned clip) const;
    uint32_t Duration(unsigned clip) const;
    bool Draw(const std::vector<std::pair<unsigned, uint32_t>> &active_clips,
              const std::array<float, 16> &root, const std::array<float, 16> &view_projection,
              std::vector<uint8_t> &vram, size_t offset, const RoomLighting *lights = nullptr,
              const RoomPoseBlend *blend = nullptr, const RoomJointLayer *layer = nullptr,
              const RoomMaterialVariant* variant = nullptr) const;
    bool DrawSign(const std::array<float,16>& root, const std::array<float,16>& camera,
                  const std::vector<PokepiTexture>& glyphs, uint32_t diffuse_rgb,
                  std::vector<uint8_t>& vram, size_t offset) const;
    bool SignWorld(unsigned marker, const std::vector<std::pair<unsigned,uint32_t>>& clips,
                   std::array<float,16>& out) const;

  private:
    struct Data;
    std::shared_ptr<const Data> data_;
};
} // namespace dokodemo::gameplay
