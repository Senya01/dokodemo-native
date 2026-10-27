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
// Inverse transpose of the linear3x3; translation is ignored. Not normalized,
// so weighted normal contributions can be blended before normalization.
GameVector4 ModelTransformNormal(const GameMatrix &matrix, const GameVector4 &normal);
uint32_t ModelWindowsActorSoftDiffuse(uint32_t color, const GameVector4 &world_normal);

struct GameModelMaterial {
    uint16_t flags = 0;
    std::shared_ptr<GameImage> texture;
    std::vector<std::pair<unsigned,std::shared_ptr<GameImage>>> secondary_textures;
};
struct GameModelStatistics {
    uint32_t nodes = 0, meshes = 0, vertices = 0, triangles = 0, weighted_vertices = 0, materials = 0;
    uint32_t particle_emitters = 0; //descriptor/common transforms only,no spawning
    uint32_t secondary_textures = 0; //owned source images;native diffuse-only shading
    uint32_t area_markers=0; //typed13 metadata/common transform, not own geometry
};
enum class GameModelTransformPolicy {
    // Existing normalized native common-transform path; not PSP matching.
    SourceCommon,
    // 8C684's oriented-scale sequence with real native canonical axes.
    // Its referenced PSP NOBITS basis initializer is not recovered here.
    WindowsAreaMarkerCanonicalBasis
};
// 80384/80DA8: immutable source arrays/material images are shared; every clone
// has independent pose/world/palette state. No PSP pointers or padding layouts.
class GameModelAsset final {
  public:
    GameFsStatus Load(GameResourceSource &source, const std::string &path, const std::string &texture_prefix,
                      GameModelTransformPolicy policy=GameModelTransformPolicy::SourceCommon);
    GameFsStatus Decode(PackageView image, GameResourceSource *source = nullptr,
                        const std::string &texture_prefix = {},
                        GameModelTransformPolicy policy=GameModelTransformPolicy::SourceCommon);
    GameModelTransformPolicy TransformPolicy()const noexcept{return transform_policy_;}
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
    GameModelTransformPolicy transform_policy_=GameModelTransformPolicy::SourceCommon;
};
struct GameModelPose {
    GameVector4 position{{0, 0, 0, 1}}, base_rotation{{0, 0, 0, 1}}, rotation{{0, 0, 0, 1}},
        scale{{1, 1, 1, 1}};
    GameMatrix world{};
    GameVector4 scale_orientation{{0,0,0,1}};
    int32_t material = -1;
    float opacity=1,world_opacity=1;
    // Reached8A214/8A1F4 outputs. The current native diffuse-only shader does
    // not yet consume these source light parameters.
    float source210=0;
    GameVector4 source230{{0,0,0,1}};
    bool source210_written=false,source230_written=false;
    model::ParticleEmitterParameters emitter;
};
struct GameAreaMarkerState {
    uint32_t node=0;
    int32_t parent=-1,material=-1;
    model::AreaMarkerParameters parameters;
    GameMatrix world{},final_matrix{};
    float world_opacity=0;
    GameModelTransformPolicy transform_policy=GameModelTransformPolicy::SourceCommon;
};
enum class GameModelColorPolicy {
    SourceRgb,
    WindowsDvsTexturedModulation2x // functional approximation, PSP combiner unknown
};
enum class GameModelLightingPolicy {
    Unlit,
    WindowsActorSoftDiffuse // fixed native key, not recovered PSP room lighting
};
struct GameContactFootprint {
    bool valid=false;
    float x=0,y=0,foot_z=0,radius_x=0,radius_y=0,body_height=0,opacity=0;
};
class GameModelClone final {
  public:
    GameFsStatus Initialize(std::shared_ptr<const GameModelAsset> asset);
    GameFsStatus Update(const GameMatrix &root);
    // Current typed copy only. No pointer/lease/issuer escapes; expected asset
    // prevents foreign-template mapping. Field interpretation stays at source.
    GameFsStatus AreaMarkers(const GameModelAsset& expected,std::vector<GameAreaMarkerState>& out)const;
    GameFsStatus Draw(GameGraphics &graphics, const GameMatrix &view_projection,
                      GameModelColorPolicy color_policy=GameModelColorPolicy::SourceRgb,
                      GameModelLightingPolicy lighting_policy=GameModelLightingPolicy::Unlit) const;
    std::vector<GameModelPose> &Pose() noexcept {
        marker_pose_current_=false;
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
        marker_pose_current_=false;
        lights_={};
        pose_.clear();
        inverse_bind_.clear();
        asset_.reset();
    }
    // The same reached4palette consumer is used by weighted draw and tests.
    static GameVector4 Skin(const model::Geometry &geometry, uint32_t vertex,
                            const std::vector<GameMatrix> &palette);
    static GameVector4 SkinNormal(const model::Geometry &geometry, uint32_t vertex,
                                  const std::vector<GameMatrix> &palette);
    // Render-only native queries, from the current owned pose/source lease.
    std::vector<GameVector4> WorldVertices(uint32_t node) const;
    GameFsStatus ContactFootprint(GameContactFootprint &out) const;
    GameFsStatus HorizontalSurfaceBelow(float x,float y,float ceiling,float max_drop,
                                        bool &found,float &height) const;

  private:
    struct GeometrySpace {GameMatrix rigid;std::vector<GameMatrix> palette;};
    GeometrySpace GeometryTransforms(uint32_t node) const;
    std::array<uint8_t,32> MarkerPoseDigest()const;
    std::shared_ptr<const GameModelAsset> asset_;
    bool marker_pose_current_=false;
    std::array<uint8_t,32> marker_pose_digest_{};
    std::vector<GameModelPose> pose_;
    LightSelection lights_;
    std::vector<GameMatrix> inverse_bind_;
};
GameFsStatus DrawWindowsContactShadow(GameGraphics &graphics,const GameMatrix &camera,
                                      const GameContactFootprint &footprint,float height);
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

// WindowsBackgroundPreparation: a separately owned, read-only background
// package/model staging job. It grants NO manager, scene or script readiness.
// 25438 uses inline20-byte registrations; 4EF4 supplies the nested model path.
// The caller must supply current candidate/period provenance. This component
// checks its own owner/epoch/request; it does not issue a gameplay capability.
struct GameBackgroundCandidate {
    uint32_t id=0, selector=0, period=0;
    uint64_t request=0;
};
struct GameBackgroundPreparationSnapshot {
    bool live=false,pending=false,package_read=false,staged=false,failed=false,model_motions_staged=false;
    uint64_t epoch=0,request=0;
    uint32_t candidate=0,selector=0,period=0,variant=0,resource=0;
    uint64_t package_owner=0,package_generation=0,package_serial=0;
    GameModelStatistics model;
    uint32_t model_motions=0,model_motion_deferred=0;
    GameFsStatus status;
    GameModelTransformPolicy transform_policy=GameModelTransformPolicy::SourceCommon;
    std::string registration;
    // Preparation never publishes source4A8/4DC, manager14 or VM completion.
    bool source_committed=false;
};
class GameBackgroundPreparation final {
  public:
    GameBackgroundPreparation()=default;
    GameBackgroundPreparation(const GameBackgroundPreparation&)=delete;
    GameBackgroundPreparation& operator=(const GameBackgroundPreparation&)=delete;
    GameFsStatus Begin(std::shared_ptr<const void> owner,uint64_t epoch,
                       const GameBackgroundCandidate&,PackageView registrations,
                       std::unique_ptr<GameFileSource> source,
                       GameModelTransformPolicy policy=GameModelTransformPolicy::SourceCommon);
    GameFsStatus Load(const std::shared_ptr<const void>& owner,uint64_t epoch);
    // 52AC's selected three (presence,count) pairs, supplied as current owned
    // user data. Only template loading; no635C random selection/playback.
    GameFsStatus LoadModelMotions(const std::shared_ptr<const void>& owner,uint64_t epoch,
                                  PackageView counts);
    GameFsStatus Draw(const std::shared_ptr<const void>& owner,uint64_t epoch,
                      GameGraphics&,const GameMatrix& view_projection) const;
    GameFsStatus AreaMarkers(const std::shared_ptr<const void>& owner,uint64_t epoch,
                             std::vector<GameAreaMarkerState>& out)const;
    GameFsStatus Close();
    GameBackgroundPreparationSnapshot Snapshot()const;
  private:
    bool Current(const std::shared_ptr<const void>&,uint64_t)const;
    bool RegistrationsCurrent()const;
    bool MotionCountsCurrent()const;
    std::weak_ptr<const void> owner_;
    PackageView registrations_,package_;
    std::array<uint8_t,32> registrations_digest_{};
    PackageView model_motion_counts_;
    std::array<uint8_t,32> model_motion_counts_digest_{};
    GameFilesystem filesystem_;
    std::shared_ptr<GameModelAsset> asset_;
    GameModelClone clone_;
    std::vector<GameModelMotion> model_motions_;
    GameBackgroundPreparationSnapshot state_;
    uint64_t last_epoch_=0,last_request_=0;
};
} // namespace dokodemo::recovered
