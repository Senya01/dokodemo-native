#pragma once
#include "game_filesystem_native.h"
#include "game_scene_ui_native.h"
#include "game_model_native.h"

namespace dokodemo::recovered {
struct GameActorRuntimeSnapshot {
    bool live=false,camera_follows_actor=false,pose_dirty=false;
    bool actor_special_ready=false,command_complete=true,command_ready=false;
    bool manager_request8=false; //1B188 zeroed6C owner,1C210 request
    int32_t active_command=-1,camera_mode=0;
    int32_t source_field50=1,source_field54=0; // AC60 AD5C/AD64 stores
    uint32_t actor=0,camera_bindings=0,transition_requests=0,loader_interval=0;
    uint32_t model_slots=0,model_nodes=0,model_vertices=0,model_triangles=0;
    uint32_t motion_templates=0,model_frames=0;
    uint64_t model_pixel_writes=0;
    uint32_t visible_pixels=0;
    bool actor_model_live=false;
    uint32_t actor_model_nodes=0,actor_model_vertices=0,actor_model_triangles=0;
    int32_t requested_command=-1;
    bool actor_request_pending=false,selected_extension_pending=false;
    uint32_t event_requests=0;
    bool command_blocked=false,command_flag25A=false,heading_known=false;
    uint32_t command_pc=0,command_effects=0,actor_phases=0,actor_math_mode=0,command_frontier=0;
    int32_t source_field58=0;
    std::array<float,2> logical_xy{};
    float logical_heading=0;
    int32_t pending_logical_motion=-1;
    std::string command_error;
    int32_t last_model_slot=-1;
    std::string last_model_path;
    std::array<float,4> actor_position{{0,0,600,1}};
    std::array<float,4> camera_up{{0,0,1,1}},camera_fallback{{1,0,0,1}};
    std::array<float,4> camera_look{{0,0,650,1}},camera_position{{0,-4400,980,1}};
};
// Finite source7650/9480/23FD0 control owners required by72/BCFC. This is
// deliberately not a declaration that wholeAC60,182F4 or scene4 models ran.
class GameActorRuntime final {
  public:
    GameFsStatus Initialize(uint32_t actor,PackageView selection,GameTransition& transition,
                            GameResourceSource* resources=nullptr);
    GameFsStatus BindCamera(int32_t mode);
    GameFsStatus StartTransition();
    bool WaitReady() const;
    GameFsStatus SetField54(int32_t value);
    GameFsStatus RequestManager();
    GameFsStatus RequestActorEvent(int32_t first,int32_t second);
    GameFsStatus AdvanceActorFrame();
    GameFsStatus CreateModel(int32_t resource,int32_t category,int32_t motion,float x,int32_t& slot);
    GameFsStatus Draw(GameGraphics&);
    GameActorRuntimeSnapshot Snapshot() const { return state_; }
    void Close();
  private:
    bool Live() const;
    PackageView selection_;
    GameTransition* transition_=nullptr;
    GameResourceSource* resources_=nullptr;
    struct ModelSlot {
        bool active=false;
        int32_t resource=-1,category=0;
        std::shared_ptr<GameModelAsset> asset;
        GameModelClone clone;
        std::vector<GameModelMotion> motions;
    };
    std::array<ModelSlot,100> models_;
    GameActorRuntimeSnapshot state_;
    PackageView actor_asc_;
    std::vector<std::string> actor_motion_names_;
    std::shared_ptr<GameModelAsset> actor_asset_;
    GameModelClone actor_clone_;
    GameFsStatus PrepareActor();
    PackageView command_image_;
    PackageView coordinate_image_;
    bool coordinate_loaded_=false;
};
} // namespace dokodemo::recovered
