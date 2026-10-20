#include "game_scene_native.h"
#include "game_boot_native.h"
#include <cstring>
#include <cmath>

namespace dokodemo::recovered {
GameScene9DC::~GameScene9DC() { (void)Close(); }
void* GameScene9DC::Allocate(uint32_t bytes) {
    auto* block = arena_->Allocate(bytes);
    if (block) allocations_.push_back(block); // capacity reserved before any arena acquire
    return block;
}
bool GameScene9DC::InitializeWorkspaces(GameArena& arena, GameGraphics& graphics) {
    if (arena_) return false;
    allocations_.reserve(8);
    // A flag byte contributes the eight implicit slots, even when size is0.
    constexpr std::array<uint32_t, 7> sizes{{1, 0, 0, 3, 4, 2, 1}};
    constexpr std::array<uint8_t, 7> flags{{0, 1, 1, 0, 0, 0, 0}};
    GameSceneSnapshot candidate;
    for (size_t i = 0; i < sizes.size(); ++i) {
        candidate.descriptor_offsets[i] = candidate.group_count;
        candidate.group_count += sizes[i] + (flags[i] ? 8 : 0);
    }
    groups_.resize(candidate.group_count);
    arena_ = &arena;
    for (auto& context : contexts_) {
        context.buffer = static_cast<uint8_t*>(Allocate(0xAF000));
        if (!context.buffer) { (void)Close(); return false; }
        context.capacity = 0xAF000;
    }
    // Real native segments/commands replace GE address patching. These words
    // are our typed format (select/clear/finish), never decoded PSP commands.
    candidate.workspaces_ready = true; state_ = candidate;
    if (!SelectRenderGroup(0) || !AppendNativeCommand(2, 23) || !graphics.Clear(0, 0)
        || !SelectRenderGroup(-1) || !AppendNativeCommand(3, 0)) { (void)Close(); return false; }
    contexts_[0].finished = true;
    state_.active_context = 1; current_group_ = -1;
    for (auto& group : groups_) group = {};
    if (!SelectRenderGroup(0) || !AppendNativeCommand(2, 23) || !graphics.Clear(0, 0)) { (void)Close(); return false; }
    candidate.active_context = 1; candidate.finished_contexts = 1; candidate.native_clear_count = 2;
    candidate.context_buffer_bytes = 2 * uint64_t(0xAF000);
    for (auto& bank : banks_) {
        auto* allocation = static_cast<uint8_t*>(Allocate(512 * 16 + 4));
        if (!allocation) { (void)Close(); return false; }
        std::memset(allocation, 0, 512 * 16 + 4); // actual905F0/90D04 producer
        allocation[1] = 2; // little-endian512 header; elements alias+4
        bank.elements = allocation + 4; bank.count = 512;
        bank.scratch = static_cast<uint8_t*>(Allocate(2501 * 16));
        if (!bank.scratch) { (void)Close(); return false; }
        bank.records = static_cast<uint8_t*>(Allocate(20));
        if (!bank.records) { (void)Close(); return false; }
        bank.capacity = 2500;
        // scratch/records remain untouched until reached producers write them.
    }
    candidate.pool_bytes = 2 * uint64_t(8196 + 40016 + 20);
    candidate.pools_ready = true; state_ = candidate; return true;
}
bool GameScene9DC::AppendNativeCommand(uint32_t operation, uint32_t argument) {
    auto& context = contexts_[state_.active_context];
    const std::array<uint32_t, 2> command{{operation, argument}};
    if (!context.buffer || context.capacity - context.cursor < sizeof(command)) return false;
    std::memcpy(context.buffer + context.cursor, command.data(), sizeof(command));
    context.cursor += uint32_t(sizeof(command)); return true;
}
bool GameScene9DC::SelectRenderGroup(int32_t group) {
    if (!state_.workspaces_ready) return false;
    if (group < 0) group += int32_t(groups_.size());
    if (group < 0 || uint32_t(group) >= groups_.size()) return false;
    if (group == current_group_) return true;
    auto& context = contexts_[state_.active_context];
    if (current_group_ >= 0) groups_[uint32_t(current_group_)].end = context.cursor;
    auto& selected = groups_[uint32_t(group)];
    selected.context = &context; selected.start = context.cursor;
    if (!AppendNativeCommand(1, uint32_t(group))) return false;
    current_group_ = group; return true;
}
bool GameScene9DC::BindFilesystem(GameFilesystem& filesystem) {
    if (!state_.pools_ready || !filesystem.Snapshot().mounted) return false;
    filesystem_ = &filesystem;
    state_.filesystem_bound = state_.resource_prefix_empty = true; //7D05C/7D0A0, actual D2A08 NUL
    return true;
}
bool GameScene9DC::InitializeCamera(GameGraphics& graphics) {
    if (!state_.filesystem_bound || state_.camera_ready) return false;
    attachments_.reserve(2);
    const std::array<std::array<float, 4>, 4> positions{{
        {{-40000, -40000, 45000, 1}}, {{0, 30000, 30000, 1}},
        {{30000, 0, 30000, 1}}, {{0, 0, -30000, 1}}}};
    const std::array<std::array<float, 4>, 4> parameters{{
        {{.5f, .5f, .5f, .5f}}, {{.2f, .2f, .25f, .5f}},
        {{.2f, .2f, .25f, .5f}}, {{.2f, .25f, .2f, .5f}}}};
    for (size_t i = 0; i < light_prototypes_.size(); ++i) {
        light_prototypes_[i].position = positions[i];
        light_prototypes_[i].parameters = parameters[i];
    }
    live_lights_ = light_prototypes_; // independent mutable type5 clones
    attachments_.push_back(5);
    GameSceneCamera camera;
    const float tangent = std::tan(camera.angle * .5f);
    if (!std::isfinite(tangent) || tangent <= 0) return false;
    camera.position = {{0, -240 / tangent, 0, 0}};
    // Source cold look-at: target0, forward+Y, up+Z. Host column-major
    // view/projection math replaces general VFPU/scalar-library matching.
    camera.view = {{1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,camera.position[1],1}};
    const float aspect = 480.0f / 272.0f;
    camera.projection[0] = 1 / (aspect * tangent);
    camera.projection[5] = 1 / tangent;
    camera.projection[10] = (camera.far_plane + camera.near_plane) / (camera.near_plane - camera.far_plane);
    camera.projection[11] = -1;
    camera.projection[14] = 2 * camera.far_plane * camera.near_plane / (camera.near_plane - camera.far_plane);
    camera_prototype_ = camera; camera_ = camera; // independent mutable type4 clone
    attachments_.push_back(4); // lights then camera
    GameMatrix identity{}; identity[0] = identity[5] = identity[10] = identity[15] = 1;
    if (!graphics.SetMatrixMode(0) || !graphics.LoadMatrix(camera_.projection)
        || !graphics.SetMatrixMode(1) || !graphics.LoadMatrix(camera_.view)
        || !graphics.SetMatrixMode(2) || !graphics.LoadMatrix(identity)) return false;
    state_.camera_y = camera.position[1]; state_.camera_near = camera.near_plane;
    state_.camera_far = camera.far_plane; state_.camera_angle = camera.angle;
    state_.camera_ready = state_.returned_9dc = true;
    state_.light_count = 4; state_.scene_flags = 0x110; return true;
}
bool GameScene9DC::InitializeResourceLookupA90() {
    if (!state_.camera_ready || !filesystem_ || !filesystem_->Snapshot().mounted) return false;
    lookup_filesystem_ = filesystem_; //95730's actual root store; following97D8C/C56C are JR/NOP
    state_.resource_lookup_bound = true; return true;
}
bool GameScene9DC::InitializeSubsystemRegistryAC4() {
    if (!state_.resource_lookup_bound || state_.registry_ready) return false;
    registered_.reserve(32); // native pointer width/layout, not source128-byte pointer table
    if (subsystem_.Start94E5C() != 1) return false;
    registered_.push_back(&subsystem_);
    state_.registered_subsystems = 1; state_.registry_ready = true; return true;
}
GameFsStatus GameScene9DC::LoadRootResource(const std::string& name, PackageView& out) {
    if (!lookup_filesystem_ || !state_.registry_ready)
        return {GameFsCode::Unavailable, 0, "resource lookup owner not initialized"};
    return lookup_filesystem_->LoadMember(name, out); // actual bounded I/O; nested mounts are separate future work
}
bool GameScene9DC::ProjectWorldPoint(const std::array<float, 3>& point, uint32_t color,
                                    GameGraphics& graphics, GameVertex& out) const {
    if (!state_.camera_ready) return false;
    for (float value : point) if (!std::isfinite(value)) return false;
    const auto transform = [](const GameMatrix& m, const std::array<float, 4>& p) {
        std::array<float, 4> out{};
        for (size_t row = 0; row < 4; ++row)
            for (size_t col = 0; col < 4; ++col) out[row] += m[col * 4 + row] * p[col];
        return out;
    };
    auto clip = transform(camera_.projection, transform(camera_.view, {{point[0], point[1], point[2], 1}}));
    if (!std::isfinite(clip[3]) || clip[3] <= 0) return false;
    return graphics.ProjectClipVertex({clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3], color}, out);
}
bool GameScene9DC::GetLight(uint32_t index, bool prototype, GameSceneLight& out) const {
    if (!state_.camera_ready || index >= live_lights_.size()) return false;
    out = prototype ? light_prototypes_[index] : live_lights_[index]; return true;
}
bool GameScene9DC::SetLightPosition(uint32_t index, const std::array<float, 4>& position) {
    if (!state_.camera_ready || index >= live_lights_.size()) return false;
    for (float value : position) if (!std::isfinite(value)) return false;
    live_lights_[index].position = position; return true;
}
GameSceneSpan GameScene9DC::ContextBuffer(uint32_t index) const {
    return state_.workspaces_ready && index < contexts_.size()
        ? GameSceneSpan{contexts_[index].buffer, contexts_[index].capacity} : GameSceneSpan{};
}
GameSceneSpan GameScene9DC::PoolElements(uint32_t index) const {
    return state_.pools_ready && index < banks_.size()
        ? GameSceneSpan{banks_[index].elements, banks_[index].count * 16} : GameSceneSpan{};
}
GameSceneSpan GameScene9DC::PoolScratch(uint32_t index) const {
    return state_.pools_ready && index < banks_.size()
        ? GameSceneSpan{banks_[index].scratch, (banks_[index].capacity + 1) * 16} : GameSceneSpan{};
}
bool GameScene9DC::Close() {
    registered_.clear(); lookup_filesystem_ = nullptr;
    current_group_ = -1;
    attachments_.clear(); // detach renderer references before clones/defaults
    while (!allocations_.empty()) {
        if (!arena_->Release(allocations_.back())) return false;
        allocations_.pop_back();
    }
    arena_ = nullptr; filesystem_ = nullptr; contexts_ = {}; banks_ = {}; groups_.clear();
    state_.filesystem_bound = false;
    state_.resource_lookup_bound = state_.registry_ready = false;
    state_.registered_subsystems = state_.light_count = 0;
    state_.workspaces_ready = state_.pools_ready = state_.camera_ready = false;
    state_.context_buffer_bytes = state_.pool_bytes = 0;
    return true;
}
} // namespace dokodemo::recovered
