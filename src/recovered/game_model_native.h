#pragma once
#include "game_ui_resource_native.h"
#include "game_graphics_native.h"
#include "model_formats.hpp"
#include "rng_native.h"
#include "game_sound_control_native.h"
#include <memory>
#include <functional>
#include <optional>

namespace dokodemo::script {class Vm;struct Value;struct NativeScriptContext;enum class Table:uint8_t;}
namespace dokodemo {struct StartupRuntimeTestAccess;}

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
    bool SourceCurrent()const noexcept {
        try{return Live()&&image_.InitializedDigest()==source_digest_;}catch(...){return false;}
    }

  private:
    PackageView image_;
    model::Dvs data_;
    std::vector<GameModelMaterial> materials_;
    GameModelStatistics stats_;
    uint32_t header_color_=0;
    std::array<uint8_t,32> source_digest_{};
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
    uint64_t Generation()const noexcept{return generation_;}
    // Per-geometry native binding; immutable shared asset/materials are untouched.
    GameFsStatus BindDiffuseTextures(uint64_t generation,const std::vector<std::pair<uint32_t,std::shared_ptr<const GameImage>>>&);
    // 2925C/28DD4: per-clone descriptor copy, never shared-template mutation.
    GameFsStatus BindGlyphTextures(uint64_t generation,const std::shared_ptr<const GameModelClone>& parent,uint32_t marker,
                                  const std::vector<std::pair<uint32_t,std::shared_ptr<const GameImage>>>&);
    bool DiffuseTexturesCurrent()const noexcept;
    uint32_t DiffuseTextureCount()const noexcept{return uint32_t(diffuse_.size());}
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
        generation_=0;diffuse_.clear();glyph_material_.reset();
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
    struct DiffuseBinding {uint32_t node=0;std::shared_ptr<const GameImage> image;uint64_t generation=0;};
    std::vector<DiffuseBinding> diffuse_;
    struct GlyphMaterialBinding {
        std::weak_ptr<const GameModelClone> parent;
        uint64_t generation=0;
        uint32_t marker=0;
        int32_t material=-1;
        model::Material descriptor;
        std::vector<int32_t> child_materials;
    };
    std::optional<GlyphMaterialBinding> glyph_material_;
    uint64_t generation_=0;
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
    bool Live()const noexcept {try{return ready_&&image_.Describe().readable;}catch(...){return false;}}
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

// Source5070 triplets are supplied as initialized user data. Nonzero words
// are source tokens resolved to bounded owned text, never cast host pointers.
using GameAuxiliaryNameResolver=std::function<GameFsStatus(uint32_t,PackageView&)>;
enum class GameAuxiliaryTarget { Child, MainTemplate, MainClone };
struct GameAuxiliaryBindingState {
    uint32_t entry=0,slot=0,source_word=0;
    GameAuxiliaryTarget target=GameAuxiliaryTarget::Child;
    GamePlaybackState playback;
    GameMotionStatistics motion;
    GameModelStatistics model;
};
// Caller-held data packets for the finite source text conversion domain.
// Actual GAME16 registration below is distinct from supplying these tables.
struct GameSupportTextMap {
    PackageView keys; // exact96 signed lookup bytes, first match wins
    std::array<PackageView,96> expansions;
    PackageView fallback,integer_format;
};
struct GameSupportTemplatePaths {
    std::array<PackageView,18> models;
    PackageView textures;
};
struct GameSupportGlyph {uint16_t code=0;int16_t style=0;};
struct GameSupportTextureState {
    GameSupportGlyph glyph;uint32_t width=0,height=0;uint64_t generation=0;
    std::array<uint8_t,32> pixels_sha256{};
};
struct GameSupportPlacement {
    uint32_t node=0,text_cell=0,characters=0,model=0;
    uint16_t mode=0;
    GameMatrix parent_world{};
};
struct GameSupportSnapshot {
    bool live=false,failed=false,templates_loaded=false,attached=false;
    uint64_t epoch=0,run=0;
    uint32_t converted=0,loaded=0;
    std::array<std::array<uint8_t,32>,8> cells{};
    std::vector<GameSupportPlacement> placements;
    std::vector<GameSupportGlyph> glyph_requests;
    bool glyph_capacity_reached=false;
    bool glyph_pixels_ready=false; // Windows synchronous resource stage, never source readiness
    uint32_t glyph_decoded=0,bound_texture_nodes=0;
    std::vector<GameSupportTextureState> textures;
    // A bound registration is current manager+20 data, not game/scene ready.
    bool source_current_bound=false;
    uint64_t source_registration=0,manager_epoch=0;
};
struct GameSupportRegistrationSnapshot {
    bool live=false,registered=false;uint64_t manager_epoch=0,serial=0,run=0,frame=0;
    uint32_t table=0,index=0,source_pc=0;std::array<char,65> module{};
};
// Actual GAME16 -> manager+20 producer. Native typed-reference/base mapping,
// not a supplied PSP pointer or default/null current-data authority.
class GameSupportRegistration final {
  public:
    GameSupportRegistration()=default;
    GameSupportRegistration(const GameSupportRegistration&)=delete;
    GameSupportRegistration& operator=(const GameSupportRegistration&)=delete;
    GameFsStatus Initialize(std::shared_ptr<const void> manager,uint64_t epoch);
    GameFsStatus Register16(script::Vm&,const script::Value&);
    bool Current(script::Vm&)const;
    GameSupportRegistrationSnapshot Snapshot()const;
    void Close();
  private:
    friend class GameSupportTextOwner;
    struct State;
    std::shared_ptr<State> binding_;
    std::weak_ptr<const void> manager_;
    uint64_t epoch_=0,serial_=0;
};
class GameSupportTextOwner final {
  public:
    GameSupportTextOwner()=default;
    GameSupportTextOwner(const GameSupportTextOwner&)=delete;
    GameSupportTextOwner& operator=(const GameSupportTextOwner&)=delete;
    GameFsStatus Initialize(std::shared_ptr<const void> owner,uint64_t epoch,script::Vm&,
        const script::Value& current_context,script::Table,uint16_t header_index,
        PackageView module,const GameSupportTextMap&);
    GameFsStatus InitializeRegistered(std::shared_ptr<const void> owner,uint64_t epoch,script::Vm&,
        const GameSupportRegistration&,PackageView module,const GameSupportTextMap&);
    GameFsStatus LoadTemplates(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,
        GameResourceSource&,const GameSupportTemplatePaths&);
    // WindowsSupportMarkerWorld: real same-marker parent world as clone root;
    // optional geometry matrices are not applied a second time.
    GameFsStatus Attach(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,
        std::shared_ptr<const GameModelClone> parent);
    GameFsStatus Update(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&);
    // Actual282D4/283B4 tuple -> held GIM -> owned pixels -> clone texture.
    // Supplied path format has exactly three integer slots: style,style,code.
    GameFsStatus PrepareGlyphTextures(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,GameResourceSource&,PackageView path_format);
    GameFsStatus DrawGlyphPreview(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,GameGraphics&,const GameMatrix&)const;
    GameFsStatus DrawPreview(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,
        GameGraphics&,const GameMatrix&)const;
    GameFsStatus Read(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,GameSupportSnapshot&)const;
    GameSupportSnapshot Progress()const{return state_;} // diagnostic partial state, never readiness
    void Close();
  private:
    script::Value Cell(script::Vm&,unsigned)const;
    GameFsStatus InitializeImpl(std::shared_ptr<const void>,uint64_t,script::Vm&,const script::Value&,script::Table,uint16_t,PackageView,const GameSupportTextMap&);
    std::weak_ptr<GameSupportRegistration::State> registration_;
    struct Input {PackageView view;std::array<uint8_t,32> digest{};};
    struct Atom {uint32_t kind=0,word=0;uint64_t owner=0,epoch=0,serial=0;std::string text;bool operator==(const Atom& b)const{return kind==b.kind&&word==b.word&&owner==b.owner&&epoch==b.epoch&&serial==b.serial&&text==b.text;}};
    Atom Capture(script::Vm&,const script::Value&)const;
    bool Current(const std::shared_ptr<const void>&,uint64_t,script::Vm&)const;
    bool InputsCurrent()const;
    std::weak_ptr<const void> owner_;
    std::weak_ptr<script::NativeScriptContext> context_;
    std::weak_ptr<const GameModelClone> parent_;
    std::vector<Input> inputs_;
    std::array<Atom,9> atoms_{};
    std::array<std::string,96> expansions_;
    std::array<uint8_t,96> keys_{};
    std::string fallback_,integer_format_;
    std::vector<std::shared_ptr<GameModelAsset>> templates_;
    std::vector<GameModelClone> children_;
    std::vector<std::shared_ptr<const GameImage>> glyph_images_;
    std::vector<bool> glyph_visible_;
    script::Table table_{};
    uint16_t index_=0;
    uint64_t frame_=0,last_epoch_=0;
    uint64_t parent_generation_=0;
    std::array<char,65> module_sha_{};
    GameSupportSnapshot state_;
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
// Selected5448 names/count are unbound. This explicit Windows visual subset
// omits that secondary-motion layer; it cannot service6D6C or grant readiness.
enum class GameBackgroundEffectsPolicy { Source5448, WindowsReducedBackgroundEffects };
// Held main-module DATA only. This never executes/interprets a BOOT instruction.
// Ordinary coordinator supplies the fixed original identity; synthetic tests use
// their distinct explicit identity and do not create an original VM/module.
struct GameMainSceneDataSnapshot {
    bool live=false;uint64_t owner=0,generation=0,serial=0;uint32_t names=0;
    std::array<uint8_t,32> identity{};
};
// Held selected source DATA, never current VM strings or readiness.
struct GameOutingData {
    GameOutingData(const GameOutingData&)=delete;
    GameOutingData& operator=(const GameOutingData&)=delete;
    std::weak_ptr<const void> lifetime;
    PackageView origin,registrations,motion_counts,triplets,model_format,light_format,glyph_format;
    PackageView position_row,placement_heading; // SAME DATA, selected BD28 inputs
    GameSupportTextMap text_map;
    GameSupportTemplatePaths support_paths;
    std::map<uint32_t,PackageView> auxiliary_names;
    std::vector<std::pair<PackageView,std::array<uint8_t,32>>> pins;
    bool Current()const;
private:
    friend class GameMainSceneData;
    GameOutingData()=default;
};
class GameMainSceneData final {
  public:
    ~GameMainSceneData(){(void)Close();}
    GameFsStatus Load(std::unique_ptr<GameFileSource>,const std::array<uint8_t,32>& expected);
    PackageView Lists()const;PackageView Gains()const;PackageView Root()const;PackageView Name(uint32_t)const;
    PackageView PositionRow(uint32_t scene)const;PackageView PlacementHeading()const;
    GameFsStatus Placement(uint32_t scene,std::array<float,4>& position_heading)const;
    GameFsStatus Outing12(std::shared_ptr<const GameOutingData>&)const;
    GameMainSceneDataSnapshot Snapshot()const;
    GameFsStatus Close();
  private:
    bool Current()const;
    std::unique_ptr<GameFileSource> file_;PackageView image_,lists_,gains_,root_;
    std::array<PackageView,25> names_{};
    uint32_t position_file_=0,heading_file_=0;
    std::array<uint32_t,2> segment_files_{},segment_sizes_{};
    std::map<uint32_t,uint8_t> outing_relocations_;
    std::vector<std::pair<PackageView,std::array<uint8_t,32>>> selected_inputs_;
    std::array<uint8_t,32> digest_{};
    bool live_=false;
    std::shared_ptr<const void> lifetime_;
};
class GameOptionSettings;
struct GameOptionSettingsSnapshot;
GameFsStatus ReadBgmGain14834(const GameOptionSettingsSnapshot&,PackageView ten_words,uint32_t&);
// Worker-owned live clock/RNG binding. No default date/seed; Close or seed
// reinitialization revokes every weak job borrower. Not gameplay authority.
class GameBackgroundRuntime final {
  public:
    GameBackgroundRuntime()=default;
    GameBackgroundRuntime(const GameBackgroundRuntime&)=delete;
    GameBackgroundRuntime& operator=(const GameBackgroundRuntime&)=delete;
    using Clock=std::function<std::array<uint32_t,7>()>;
    GameFsStatus Initialize(Clock,RngNativeOwner&);
    void Close()noexcept{clock_={};rng_=nullptr;}
    bool Live()const noexcept;
    uint64_t Identity()const noexcept{return identity_;}
  private:
    friend class GameBackgroundPreparation;
    friend class GameSceneBgm;
    friend class GameContentBoot;
    GameFsStatus BindSceneSettings(const std::shared_ptr<GameBackgroundRuntime>&,GameOptionSettings&);
    bool SettingsCurrent(const std::shared_ptr<GameBackgroundRuntime>&,const GameOptionSettings&)const;
    std::weak_ptr<GameBackgroundRuntime> settings_runtime_;
    const GameOptionSettings* settings_=nullptr;
    std::weak_ptr<const void> settings_life_;
    uint64_t settings_owner_=0,settings_generation_=0;
    bool bgm_constructor_issued_=false;
    GameFsStatus Read(std::array<uint32_t,7>&);
    Clock clock_;
    RngNativeOwner* rng_=nullptr; // parent closes this binding before its RNG
    std::weak_ptr<const void> rng_lifetime_;
    uint64_t identity_=0,lineage_=0,reads_=0;
};
class GameOptionSettings;
class GameActorRuntime;
class GameManagerSwitchControl;
struct GameSceneBgmSnapshot {
    bool initialized=false,current=false,attempted=false,selected=false,requested=false,cache_refreshed=false;
    bool source_scene_retired=false;
    uint32_t selection_writer=0;
    uint64_t epoch=0,actor_epoch=0;
    uint32_t initial_clock52=0,period4B4=0,variant4AC=1;
    uint64_t cache_revision=0;
    int32_t stored4BC=0,control_return=0;
    std::optional<GameBgmControlRecord> cleanup_control;
    std::array<uint32_t,7> calendar{};
    GameBgmSelection selection;
    GameBgmFileResult file_result;
    GameBgmFileRecord file,previous_file; // current request and retained predecessor, copied readback only
};
// Native current scene/BGM owner: original static tables are held user data.
// Fresh scene clock0 ->7398 ->4B4 ->19B6C/store4BC/file BEFORE5F58.
// The separate4AD4C prefix consumes actual root18 and its held gain table.
class GameSceneBgm final {
  public:
    GameSceneBgm();~GameSceneBgm();
    GameSceneBgm(const GameSceneBgm&)=delete;
    GameSceneBgm& operator=(const GameSceneBgm&)=delete;
    GameFsStatus Initialize(std::shared_ptr<GameBackgroundRuntime>,uint64_t,GameOptionSettings&,
        PackageView lists,PackageView gains,PackageView root,std::function<PackageView(uint32_t)>,GameBgmFileQueue::Factory);
    GameFsStatus BeginInitial(const std::shared_ptr<GameBackgroundRuntime>&,GameActorRuntime&);
    // Selected1B114(flag1): refresh from the SAME current scene before target mapping.
    GameFsStatus RefreshCachedPeriod(const std::shared_ptr<GameBackgroundRuntime>&,GameActorRuntime&);
    GameFsStatus RequestControl(const std::shared_ptr<GameBackgroundRuntime>&,uint32_t argument);
    GameFsStatus RequestCleanupControl(const std::shared_ptr<GameBackgroundRuntime>&,bool loader469,uint32_t current_ui464);
    GameFsStatus Pump(const std::shared_ptr<GameBackgroundRuntime>&);
    bool CurrentChoice(int32_t&)const;
    bool SharesDataBacking(PackageView)const;
    GameSceneBgmSnapshot Snapshot()const;
    GameFsStatus Close();
  private:
    friend class GameBackgroundPreparation;
    struct Data;std::unique_ptr<Data> data_;
    friend class GameActorRuntime;
    std::weak_ptr<const void> Lifetime()const;
    GameFsStatus BindSelectionCell(const std::shared_ptr<GameManagerSwitchControl>&,uint64_t);
    GameFsStatus DepartureControlCurrent(const std::shared_ptr<GameBackgroundRuntime>&)const;
    GameFsStatus RetireSceneRecord(const std::shared_ptr<GameBackgroundRuntime>&);
};
struct GameBackgroundLightBinding {
    uint32_t slot=0;
    GamePlaybackState playback;
    GameMotionStatistics motion;
};
struct GameBackgroundAppliedState {
    uint64_t runtime=0,clock_reads=0,rng_before=0,rng_after=0;
    uint32_t rng_calls=0,light_slot=0,samples=0;
    std::array<uint32_t,7> calendar{};
    std::vector<uint32_t> selected_entries;
    std::vector<int32_t> child_phases;
    std::vector<GameModelPose> main_pose;
    std::vector<std::vector<GameModelPose>> child_poses; // selected order, empty when no child
    GameModelClone::LightSelection lights;
};
struct GameBackgroundPreparationSnapshot {
    bool live=false,pending=false,package_read=false,staged=false,failed=false,model_motions_staged=false;
    uint64_t epoch=0,request=0;
    bool clock_initialized=false;
    uint32_t clock_table52=0,clock_writer=0;
    uint32_t candidate=0,selector=0,period=0,variant=0,resource=0;
    uint64_t package_owner=0,package_generation=0,package_serial=0;
    GameModelStatistics model;
    uint32_t model_motions=0,model_motion_deferred=0;
    bool auxiliary_staged=false;
    uint32_t auxiliary_entries=0,auxiliary_children=0,auxiliary_templates=0,auxiliary_bindings=0;
    GameModelStatistics auxiliary_model;
    bool effects_prepared=false,secondary_motions_omitted=false,special_motion_not_applicable=false;
    uint32_t light_templates=0,light_bindings=0,light_deferred=0;
    GameFsStatus status;
    GameModelTransformPolicy transform_policy=GameModelTransformPolicy::SourceCommon;
    std::string registration;
    // Preparation never publishes source4A8/4DC, manager14 or VM completion.
    bool source_committed=false;
    bool bgm_attempted=false,bgm_requested=false;
    int32_t bgm_choice=-1;
    GameBgmSelection bgm_selection;
    GameBgmFileResult bgm_result;
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
    //19DD4: selection/store4BC and file request precede5F58/635C. The old
    // choice is held CURRENT data, not a default/placement row. This remains
    // conditional preparation, without a current VM candidate issuer.
    GameFsStatus RequestBgm(const std::shared_ptr<const void>&,uint64_t epoch,
        const std::shared_ptr<GameBackgroundRuntime>&,PackageView previous4BC,PackageView lists,
        PackageView root,const std::function<PackageView(uint32_t)>& names,GameBgmFileQueue::Factory);
    GameFsStatus RequestBgm(const std::shared_ptr<const void>&,uint64_t epoch,
        const std::shared_ptr<GameBackgroundRuntime>&,GameSceneBgm& current);
    GameFsStatus PumpBgm(const std::shared_ptr<const void>&,uint64_t epoch);
    GameBgmFileRecord BgmFile()const;
    // 52AC's selected three (presence,count) pairs, supplied as current owned
    // user data. Only template loading; no635C random selection/playback.
    GameFsStatus LoadModelMotions(const std::shared_ptr<const void>& owner,uint64_t epoch,
                                  PackageView counts);
    // 50AC/4A0C owned recipe/templates. Windows eagerly prepares all candidate
    // bindings (635C targets), without random selection, sampling or activation
    // publication. Constructor state1/time0 is retained; only635C child/visible
    // targets receive its explicit state3 store. No control is sampled.
    GameFsStatus LoadAuxiliary(const std::shared_ptr<const void>& owner,uint64_t epoch,
                               PackageView triplets,PackageView model_format,
                               const GameAuxiliaryNameResolver& resolve);
    GameFsStatus Auxiliary(const std::shared_ptr<const void>& owner,uint64_t epoch,
                           std::vector<GameAuxiliaryBindingState>& out)const;
    // 5610/6A40: real seven template/control bindings to the main clone, still
    // in constructor state1. Path format is owned user data, not a name table.
    // No709C/72B8 current-calendar activation or635C selection is issued here.
    GameFsStatus PrepareEffects(const std::shared_ptr<const void>& owner,uint64_t epoch,
                                GameBackgroundEffectsPolicy,PackageView light_format);
    GameFsStatus LightBindings(const std::shared_ptr<const void>& owner,uint64_t epoch,
                                std::vector<GameBackgroundLightBinding>& out)const;
    // Actual635C/61E4 selection plus709C/72B8 activation. Samples are an
    // explicit Windows scheduling pass, not source job/VM completion.
    GameFsStatus ActivateEffects(const std::shared_ptr<const void>& owner,uint64_t epoch,
                                 std::weak_ptr<GameBackgroundRuntime> runtime);
    GameFsStatus SampleEffects(const std::shared_ptr<const void>& owner,uint64_t epoch,uint32_t delta);
    GameFsStatus AppliedEffects(const std::shared_ptr<const void>& owner,uint64_t epoch,
                                GameBackgroundAppliedState& out)const;
    GameFsStatus ReadScenePeriod(const std::shared_ptr<const void>& owner,uint64_t epoch,
                                uint32_t hour,uint32_t minute,int32_t& result,uint32_t& table52)const;
    // Explicit component preview of one prepared child; not source visibility.
    GameFsStatus DrawAuxiliary(const std::shared_ptr<const void>& owner,uint64_t epoch,uint32_t entry,
                               GameGraphics&,const GameMatrix& view_projection)const;
    GameFsStatus Draw(const std::shared_ptr<const void>& owner,uint64_t epoch,
                      GameGraphics&,const GameMatrix& view_projection) const;
    GameFsStatus AreaMarkers(const std::shared_ptr<const void>& owner,uint64_t epoch,
                             std::vector<GameAreaMarkerState>& out)const;
    GameFsStatus PrepareSupport(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,
        const GameSupportRegistration&,PackageView module,const GameSupportTextMap&,GameResourceSource&,
        const GameSupportTemplatePaths&,PackageView glyph_format);
    GameFsStatus UpdateSupport(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&);
    GameFsStatus ReadSupport(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,GameSupportSnapshot&)const;
    GameFsStatus DrawWithSupport(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,GameGraphics&,const GameMatrix&)const;
    GameFsStatus DrawSelected(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,GameGraphics&,const GameMatrix&)const;
    GameFsStatus PrepareOwnedSupport(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm&,
        const GameSupportRegistration&,PackageView module,const GameOutingData&);
    GameFsStatus Close();
    GameBackgroundPreparationSnapshot Snapshot()const;
  private:
    friend class GameActorRuntime;
    friend class GameCompletedOutingMenuBorrow;
    friend struct ::dokodemo::StartupRuntimeTestAccess;
    bool Current(const std::shared_ptr<const void>&,uint64_t)const;
    bool RegistrationsCurrent()const;
    bool MotionCountsCurrent()const;
    bool AuxiliaryCurrent()const;
    bool EffectsCurrent()const;
    bool RuntimeCurrent()const;
    struct AuxiliaryEntry {
        std::array<uint32_t,3> words{};
        std::array<std::unique_ptr<GameModelMotion>,3> motions;
        std::unique_ptr<GameModelClone> child;
    };
    struct AuxiliaryBinding {
        uint32_t entry=0,slot=0;
        GameAuxiliaryTarget target=GameAuxiliaryTarget::Child;
        GameModelPlayback playback;
    };
    struct AuxiliaryInput {PackageView view;std::array<uint8_t,32> digest{};};
    std::vector<AuxiliaryInput> auxiliary_inputs_;
    std::vector<AuxiliaryEntry> auxiliary_entries_;
    std::vector<AuxiliaryBinding> auxiliary_bindings_;
    std::shared_ptr<GameModelAsset> auxiliary_asset_;
    GameModelClone main_template_clone_;
    std::weak_ptr<const void> owner_;
    PackageView registrations_,package_;
    std::array<uint8_t,32> registrations_digest_{};
    PackageView model_motion_counts_;
    std::array<uint8_t,32> model_motion_counts_digest_{};
    GameFilesystem filesystem_;
    GameBgmFileQueue bgm_files_;
    GameSceneBgm* scene_bgm_=nullptr;
    uint64_t scene_bgm_cache_revision_=0;
    std::weak_ptr<const void> scene_bgm_lifetime_;
    std::weak_ptr<GameBackgroundRuntime> bgm_runtime_;
    std::array<PackageView,2> bgm_inputs_;
    std::array<std::array<uint8_t,32>,2> bgm_hashes_{};
    bool BgmCurrent()const;
    std::shared_ptr<GameModelAsset> asset_;
    std::shared_ptr<GameModelClone> clone_;
    GameSupportTextOwner support_;
    std::shared_ptr<GameResourceSource> support_resources_;
    bool support_prepared_=false;
    bool SupportJobCurrent(const std::shared_ptr<const void>& owner,uint64_t epoch)const;
    std::vector<GameModelMotion> model_motions_;
    PackageView light_format_;
    std::array<uint8_t,32> light_format_digest_{};
    std::vector<GameModelMotion> light_motions_;
    std::vector<GameModelPlayback> light_playbacks_;
    std::weak_ptr<GameBackgroundRuntime> runtime_;
    GameBackgroundAppliedState applied_;
    bool activated_=false;
    GameBackgroundPreparationSnapshot state_;
    uint64_t last_epoch_=0,last_request_=0;
};
} // namespace dokodemo::recovered
