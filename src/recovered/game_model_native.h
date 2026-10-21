#pragma once
#include "game_ui_resource_native.h"
#include "game_graphics_native.h"
#include "model_formats.hpp"
#include <memory>

namespace dokodemo::recovered {
using GameVector4 = std::array<float, 4>;
GameMatrix ModelIdentity() noexcept;
GameMatrix ModelMultiply(const GameMatrix &left, const GameMatrix &right) noexcept;
GameVector4 ModelTransform(const GameMatrix &matrix, const GameVector4 &value) noexcept;
GameMatrix ModelQuaternion(GameVector4 quaternion);
GameMatrix ModelTRS(GameVector4 position, GameVector4 base_rotation, GameVector4 rotation, GameVector4 scale);
GameMatrix ModelPerspective(float field_of_view, float aspect, float near_plane, float far_plane);
GameMatrix ModelInverseRigid(const GameMatrix &matrix);

struct GameModelMaterial {
    uint16_t flags = 0;
    std::shared_ptr<GameImage> texture;
    std::vector<std::pair<unsigned,std::shared_ptr<GameImage>>> secondary_textures;
};
struct GameModelStatistics {
    uint32_t nodes = 0, meshes = 0, vertices = 0, triangles = 0, weighted_vertices = 0, materials = 0;
    uint32_t particle_emitters = 0; //descriptor/common transforms only,no spawning
    uint32_t secondary_textures = 0; //owned source images;native diffuse-only shading
};
// 80384/80DA8: immutable source arrays/material images are shared; every clone
// has independent pose/world/palette state. No PSP pointers or padding layouts.
class GameModelAsset final {
  public:
    GameFsStatus Load(GameResourceSource &source, const std::string &path, const std::string &texture_prefix);
    GameFsStatus Decode(PackageView image, GameResourceSource *source = nullptr,
                        const std::string &texture_prefix = {});
    const model::Dvs &Data() const noexcept {
        return data_;
    }
    const std::vector<GameModelMaterial> &Materials() const noexcept {
        return materials_;
    }
    GameModelStatistics Statistics() const noexcept {
        return stats_;
    }
    uint32_t HeaderColor() const noexcept { return header_color_; }
    bool Live() const noexcept {
        try {
            return image_.Describe().readable;
        } catch (...) {
            return false;
        }
    }

  private:
    PackageView image_;
    model::Dvs data_;
    std::vector<GameModelMaterial> materials_;
    GameModelStatistics stats_;
    uint32_t header_color_=0;
};
struct GameModelPose {
    GameVector4 position{{0, 0, 0, 1}}, base_rotation{{0, 0, 0, 1}}, rotation{{0, 0, 0, 1}},
        scale{{1, 1, 1, 1}};
    GameMatrix world{};
    int32_t material = -1;
    float opacity=1,world_opacity=1;
    // Reached8A214/8A1F4 outputs. The current native diffuse-only shader does
    // not yet consume these source light parameters.
    float source210=0;
    GameVector4 source230{{0,0,0,1}};
    bool source210_written=false,source230_written=false;
    model::ParticleEmitterParameters emitter;
};
class GameModelClone final {
  public:
    GameFsStatus Initialize(std::shared_ptr<const GameModelAsset> asset);
    GameFsStatus Update(const GameMatrix &root);
    GameFsStatus Draw(GameGraphics &graphics, const GameMatrix &view_projection) const;
    std::vector<GameModelPose> &Pose() noexcept {
        return pose_;
    }
    const std::vector<GameModelPose> &Pose() const noexcept {
        return pose_;
    }
    const GameModelAsset *Asset() const noexcept {
        return asset_.get();
    }
    struct LightSelection {
        std::vector<uint32_t> type5_nodes;
        uint32_t packed_color14=0, color_writes=0;
        bool present=false;
    };
    const LightSelection& Lights() const noexcept { return lights_; }
    void SetLightColor14(uint32_t color) { if(lights_.present){lights_.packed_color14=color;++lights_.color_writes;} }
    void Close() {
        lights_={};
        pose_.clear();
        inverse_bind_.clear();
        asset_.reset();
    }
    // The same reached4palette consumer is used by weighted draw and tests.
    static GameVector4 Skin(const model::Geometry &geometry, uint32_t vertex,
                            const std::vector<GameMatrix> &palette);

  private:
    std::shared_ptr<const GameModelAsset> asset_;
    std::vector<GameModelPose> pose_;
    LightSelection lights_;
    std::vector<GameMatrix> inverse_bind_;
};
struct GameMotionStatistics {
    uint32_t tracks = 0, keys = 0, duration = 0, approximate_curves = 0, null_callback_tracks = 0;
    bool loop = false;
    uint32_t deferred_sections = 0;
};
// 804F0/89E54: checked source key values and actual material selection. Native
// slerp/piecewise-linear interpolation for known tangent curves is nonmatching;
// tangent bytes stay retained, unrecognized section types are refused.
class GameModelMotion final {
  public:
    GameFsStatus Load(GameResourceSource &source, const std::string &path, const GameModelAsset &target);
    GameFsStatus Decode(PackageView image, const GameModelAsset &target);
    GameFsStatus Apply(GameModelClone &clone, uint32_t time) const;
    GameMotionStatistics Statistics() const noexcept {
        return stats_;
    }

  private:
    bool ready_ = false;
    PackageView image_;
    model::Dak data_;
    GameMotionStatistics stats_;
};
// Typed 88728/89954/89A88 control. Sampling precedes time advancement. A
// looping channel never reports state2 merely because its duration elapsed.
struct GamePlaybackState {
    uint32_t state=1,time=0,sampled_time=0,duration=0,loops=0,samples=0;
    bool loop=false;
    int32_t stop_limit=-1,speed=4800;
};
class GameModelPlayback final {
  public:
    void Configure(uint32_t duration,bool loop);
    void Start(uint32_t time=0);
    void Stop() noexcept { state_.state=1; }
    void SetSpeed(int32_t value) { if(value<0)throw std::invalid_argument("Negative native motion speed");state_.speed=value; }
    GameFsStatus Apply(const GameModelMotion&,GameModelClone&,uint32_t delta);
    GamePlaybackState Snapshot() const noexcept { return state_; }
    bool Complete() const noexcept { return state_.state==2; }
    void Close() noexcept { state_={}; }
  private:
    GamePlaybackState state_;
};
} // namespace dokodemo::recovered
