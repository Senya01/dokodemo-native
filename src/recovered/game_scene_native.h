#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "game_graphics_native.h"
#include "game_filesystem_native.h"

namespace dokodemo::recovered {
class GameArena;
class GameFilesystem;
struct GameSceneSpan { uint8_t* bytes = nullptr; uint32_t size = 0; };
struct GameSceneSnapshot {
    bool workspaces_ready = false, pools_ready = false, camera_ready = false, returned_9dc = false;
    bool filesystem_bound = false, resource_prefix_empty = false;
    bool resource_lookup_bound = false, registry_ready = false;
    uint32_t registered_subsystems = 0;
    uint32_t group_count = 0, active_context = 0, finished_contexts = 0;
    std::array<uint32_t, 7> descriptor_offsets{};
    uint64_t context_buffer_bytes = 0, pool_bytes = 0;
    uint32_t native_clear_count = 0, light_count = 0, scene_flags = 0;
    float camera_y = 0, camera_near = 0, camera_far = 0, camera_angle = 0;
};
struct GameSceneLight {
    std::array<float, 4> position{}, parameters{};
    uint32_t source_type = 5;
    bool enabled = true;
};
struct GameSceneCamera {
    std::array<float, 4> position{}, target{}, up{{0, 0, 1, 1}}, fallback{{1, 0, 0, 1}};
    float near_plane = 10, far_plane = 70000, angle = 0.7853981852531433f;
    GameMatrix view{}, projection{};
};
// Native typed control objects replace PSP528-byte/32-bit pointer layouts and
// GE command list plumbing. Actual backing buffers come from the live PPK arena.
// Uninitialized source pools are not silently declared zero/known.
class GameScene9DC final {
public:
    GameScene9DC() = default;
    GameScene9DC(const GameScene9DC&) = delete;
    GameScene9DC& operator=(const GameScene9DC&) = delete;
    ~GameScene9DC();
    bool InitializeWorkspaces(GameArena& arena, GameGraphics& graphics);
    bool BindFilesystem(GameFilesystem& filesystem);
    bool InitializeCamera(GameGraphics& graphics);
    bool InitializeResourceLookupA90();
    bool InitializeSubsystemRegistryAC4();
    GameFsStatus LoadRootResource(const std::string& name, PackageView& out);
    bool ProjectWorldPoint(const std::array<float, 3>& point, uint32_t color, GameGraphics& graphics, GameVertex& out) const;
    bool GetLight(uint32_t index, bool prototype, GameSceneLight& out) const;
    bool SetLightPosition(uint32_t index, const std::array<float, 4>& position);
    bool SelectRenderGroup(int32_t group);
    GameSceneSpan ContextBuffer(uint32_t index) const;
    GameSceneSpan PoolElements(uint32_t bank) const;
    GameSceneSpan PoolScratch(uint32_t bank) const;
    GameSceneSnapshot Snapshot() const { return state_; }
    bool Close(); // caller has joined all consumers, before arena retirement
private:
    struct Context { uint8_t* buffer = nullptr; uint32_t capacity = 0, cursor = 0; bool finished = false; };
    struct Group { Context* context = nullptr; uint32_t start = 0, end = 0; };
    struct PoolBank {
        uint8_t* elements = nullptr;
        uint8_t* scratch = nullptr;
        uint8_t* records = nullptr;
        uint32_t count = 0, capacity = 0;
    };
    // Actual reached callback94E5C is JR with return1 in its delay slot.
    // Other unrecovered methods of this source object are not exposed.
    struct RegisteredSubsystem { int32_t Start94E5C() const noexcept { return 1; } };
    void* Allocate(uint32_t bytes);
    bool AppendNativeCommand(uint32_t operation, uint32_t argument);
    GameArena* arena_ = nullptr;
    GameFilesystem* filesystem_ = nullptr;
    std::array<Context, 2> contexts_{};
    std::vector<Group> groups_;
    std::array<PoolBank, 2> banks_{};
    std::vector<void*> allocations_;
    std::array<GameSceneLight, 4> light_prototypes_{}, live_lights_{};
    GameSceneCamera camera_prototype_, camera_;
    // Explicit attachment order, borrowing independent mutable clones.
    std::vector<uint32_t> attachments_;
    RegisteredSubsystem subsystem_;
    std::vector<RegisteredSubsystem*> registered_;
    GameFilesystem* lookup_filesystem_ = nullptr;
    int32_t current_group_ = -1;
    GameSceneSnapshot state_;
};
} // namespace dokodemo::recovered
