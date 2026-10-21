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
};
struct GameModelStatistics {
    uint32_t nodes = 0, meshes = 0, vertices = 0, triangles = 0, weighted_vertices = 0, materials = 0;
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
};
struct GameModelPose {
    GameVector4 position{{0, 0, 0, 1}}, base_rotation{{0, 0, 0, 1}}, rotation{{0, 0, 0, 1}},
        scale{{1, 1, 1, 1}};
    GameMatrix world{};
    int32_t material = -1;
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
    void Close() {
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
    std::vector<GameMatrix> inverse_bind_;
};
struct GameMotionStatistics {
    uint32_t tracks = 0, keys = 0, duration = 0, approximate_curves = 0, null_callback_tracks = 0;
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
} // namespace dokodemo::recovered
