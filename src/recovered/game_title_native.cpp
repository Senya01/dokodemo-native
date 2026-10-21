#include "game_title_native.h"
#include "game_boot_native.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
constexpr unsigned order[]{0, 4, 3, 2, 1};
constexpr const char *names[]{"toro", "suzuki", "jun", "pierre", "ricky", "kuro"};
constexpr char prefix[] = "title\\charmodel_title\\";
GameMatrix ActorRoot(const GameTitleActorSnapshot &actor) {
    // 3B01C heading+pi=-pi/2, native column-vector equivalent.
    auto m = ModelIdentity();
    m[0] = 0;
    m[1] = -1;
    m[4] = 1;
    m[5] = 0;
    m[12] = actor.x;
    m[13] = actor.y;
    m[14] = actor.z;
    return m;
}
} // namespace
uint32_t TitleMovementStep(uint32_t type, uint32_t counter, uint32_t &segment) {
    constexpr uint32_t speeds[]{25, 58, 0, 33, 25, 25};
    if (type >= 6 || segment >= 3)
        throw std::runtime_error("TITLE movement table index");
    if (type != 2)
        return speeds[type];
    constexpr uint32_t durations[]{3, 19, 24};
    const auto step = segment == 1 ? 52u : 0u;
    if (counter == 1)
        segment = 0;
    else if (counter > 1 && counter % durations[segment] == 1)
        segment = (segment + 1) % 3;
    return step; // advance is for the next08A88 invocation
}
GameTitleScene::~GameTitleScene() {
    (void)Close();
}
GameFsStatus GameTitleScene::Initialize(GameResourceSource &source, GameArena &arena, GameVmBoot &manager,
                                        NativeTitleLayout layout, uint32_t source_profile_date) {
    if (arena_)
        return {GameFsCode::Busy, 0, "TITLE owner already allocated"};
    arena_ = &arena;
    manager_ = &manager;
    state_ = {};
    state_.layout = layout == NativeTitleLayout::CharacterEntrance ? 0u : 5u;
    // 30E98->342D8->24D00 reads gamestate+4C. The actual249D0 reset
    // zeroed that range and no missing AUTOLOAD imported it. With0,342D8
    // returns0 immediately, before its profile/date/string consumers.
    if (source_profile_date != 0)
        return {GameFsCode::Unavailable, 0, "TITLE342D8 nonzero profile path unimplemented"};
    try {
        owner_ = arena.Allocate(376);
        text_ = arena.Allocate(1024 * 44);
        task_ = manager.AcquireTask();
        if (!owner_ || !text_ || task_ == UINT32_MAX)
            throw std::runtime_error("TITLE owner/text/task allocation failed");
        // Immutable arrays may be shared across the five independent kuro clones.
        // Actual constructors request these six identities and light, not a scan.
        constexpr char suffix[]{'T', 'S', 'J', 'P', 'R'};
        for (unsigned type = 0; type < 6; ++type) {
            auto asset = std::make_shared<GameModelAsset>();
            auto status = asset->Load(source, std::string(prefix) + names[type] + ".DVS",
                                      std::string(prefix) + "gim\\");
            if (!status)
                return status;
            assets_[type] = std::move(asset);
            ++state_.model_assets;
            const auto stats = assets_[type]->Statistics();
            state_.vertices += stats.vertices;
            state_.triangles += stats.triangles;
            std::vector<std::string> paths;
            if (type < 5)
                paths = {"_tati", "_run_l", "_dance", std::string("_") + suffix[type] + "_guruguru02"};
            else
                paths = {"_run_l"};
            for (const auto &path : paths) {
                GameModelMotion motion;
                status =
                    motion.Load(source, std::string(prefix) + names[type] + path + ".DAK", *assets_[type]);
                if (!status)
                    return status;
                state_.approximate_curves += motion.Statistics().approximate_curves;
                motions_[type].push_back(std::move(motion));
                ++state_.motions;
            }
        }
        assets_[6] = std::make_shared<GameModelAsset>();
        auto status =
            assets_[6]->Load(source, std::string(prefix) + "light.DVS", std::string(prefix) + "gim\\");
        if (!status)
            return status;
        ++state_.model_assets;
        for (unsigned i = 0; i < 10; ++i) {
            status = actors_[i].model.Initialize(assets_[i < 5 ? i : 5]);
            if (!status)
                return status;
            ++state_.clones;
        }
        // 3B264/3B344 position and lookpoint, FOV20deg. The file camera's
        // one-unit lookpoint is not used as a position. Host lookAt is nonmatching.
        auto view = ModelIdentity();
        view[0] = 1;
        view[5] = 0;
        view[9] = 1;
        view[6] = -1;
        view[10] = 0;
        view[13] = -560;
        view[14] = -4400;
        camera_ = ModelMultiply(ModelPerspective(state_.field_of_view, 480.0f / 272.0f, 100, 10400), view);
        state_.camera_ready = true;
        status = ResetEntrance();
        if (!status)
            return status;
        state_.live = state_.constructor_returned = true;
        state_.state = 2;
        state_.frontier_pc = 0x30D44;
        if (layout == NativeTitleLayout::Menu5)
            return {GameFsCode::Unavailable, 0,
                    "TITLE menu5 callbacks/rendering unimplemented; explicit native layout policy"};
        return {};
    } catch (const std::exception &e) {
        return {GameFsCode::Unavailable, 0, e.what()};
    }
}
GameFsStatus GameTitleScene::ResetEntrance() {
    for (unsigned i = 0; i < 10; ++i) {
        auto &actor = actors_[i];
        auto &s = state_.actors[i];
        const auto position = order[i % 5];
        s = {};
        s.type = i < 5 ? i : 5;
        s.x = 2500.0f + (i >= 5 ? 800.0f * position : 0);
        s.y = 2000.0f + 500.0f * position;
        s.target_y = s.y;
        s.z = -400;
        s.moving = i < 5;
        s.target_produced=i<5;
        if(i>=5){s.target_x=0;s.target_y=0;} //no cold3B134 producer for kuro
        s.desired_visible = i < 5;
        s.visible = false;
        actor.time = actor.segment = 0;
        auto status = motions_[s.type][s.type < 5 ? 1 : 0].Apply(actor.model, 0);
        if (!status)
            return status;
        status = actor.model.Update(ActorRoot(s));
        if (!status)
            return status;
    }
    return {};
}
GameFsStatus GameTitleScene::TickAndDraw(GameGraphics &graphics, GameTransition &fade) {
    if (!state_.live)
        return {GameFsCode::Unavailable, 0, "TITLE not live"};
    try {
        // 30C34 case2 waits on the actual D54C owner, resets335A4, then
        // 30E48 DA64 starts source fade1. No direct199DC fade call is invented.
        bool reset = false;
        if (state_.state == 2 && fade.Snapshot().state == 0) {
            auto status = ResetEntrance();
            if (!status)
                return status;
            fade.SetDuration(30);
            fade.Open();
            reset = true;
            if (fade.Snapshot().state != 1)
                return {GameFsCode::Unavailable, 0, "TITLE DA64 fade refused"};
            state_.state = 0;
        } else if (state_.state == 0 && fade.Snapshot().state == 2)
            state_.state = 1;
        ++state_.updates;
        state_.visible_actors = 0;
        for (unsigned i = 0; i < 10; ++i) {
            auto &actor = actors_[i];
            auto &s = state_.actors[i];
            // 08A88 uses the previous animation counter;3B01C publishes the
            // new floor(time/160) after movement. Native80 units/tick at60Hz.
            if (s.moving && !reset) {
                const float step = float(TitleMovementStep(s.type, s.movement_counter, actor.segment));
                if (s.x - s.target_x <= step) {
                    s.x = s.target_x;
                    s.moving = false;
                    s.desired_visible = false;
                    state_.arrival_branch_pending=true;
                } else
                    s.x -= step;
            }
            auto &motion = motions_[s.type][s.type < 5 ? 1 : 0];
            auto status = motion.Apply(actor.model, actor.time);
            if (!status)
                return status;
            s.animation_time = actor.time;
            s.movement_counter = actor.time / 160;
            // The reached run completion path seeks160 rather than0.
            actor.time += state_.native_tick_units;
            if (actor.time >= motion.Statistics().duration)
                actor.time = (std::min)(160u, motion.Statistics().duration);
            // Debug identity of local animated values, excluding the actor's
            // outer movement matrix. Used to verify real DAK changes in a run.
            s.local_pose_digest = 14695981039346656037ull;
            for (const auto &pose : actor.model.Pose())
                for (const auto &vector : {pose.position, pose.rotation})
                    for (auto value : vector) {
                        uint32_t word;
                        std::memcpy(&word, &value, 4);
                        s.local_pose_digest ^= word;
                        s.local_pose_digest *= 1099511628211ull;
                    }
            status = actor.model.Update(ActorRoot(s));
            if (!status)
                return status;
            // 3177C/33B90 only copies desired flags at CC1,FC0. Kuro is
            // initially hidden/nonmoving, not enabled by a table speed alone.
            s.visible = state_.state == 1 && s.desired_visible;
            if (s.visible)
                ++state_.visible_actors;
        }
        if(state_.arrival_branch_pending) {
            state_.frontier_pc=0x33B90;
            return {GameFsCode::Unavailable,0,"TITLE primary arrived;source media/menu/counter/random restart-or-kuro branch not implemented"};
        }
        if (!graphics.Clear(0xFF000000, 0))
            return {GameFsCode::Unavailable, 0, "TITLE surface clear failed"};
        const auto before = graphics.Snapshot().mesh_pixels;
        for (unsigned i = 0; i < 10; ++i)
            if (state_.actors[i].visible) {
                auto status = actors_[i].model.Draw(graphics, camera_);
                if (!status)
                    return status;
            }
        fade.Update();
        if (!fade.Draw(graphics) || !graphics.SwapBuffers())
            return {GameFsCode::Unavailable, 0, "TITLE fade/present failed"};
        state_.pixel_writes += graphics.Snapshot().mesh_pixels - before;
        ++state_.frames;
        state_.frame_drawn = state_.pixel_writes > 0;
        // Scene3's1A89C still waitsCC3. Unrecovered menu/random-restart/input
        // consumers are not turned into a completed scene or guessed KSC call.
        state_.frontier_pc = state_.state == 1 ? 0x33B90 : 0x30EB0;
        return {};
    } catch (const std::exception &e) {
        return {GameFsCode::Unavailable, 0, e.what()};
    }
}
bool GameTitleScene::Close() {
    for (auto &a : actors_)
        a.model.Close();
    for (auto &m : motions_)
        m.clear();
    for (auto &a : assets_)
        a.reset();
    if (manager_ && task_ != UINT32_MAX) {
        if (!manager_->ReleaseTask(task_))
            return false;
        task_ = UINT32_MAX;
    }
    if (arena_ && text_) {
        if (!arena_->Release(text_))
            return false;
        text_ = nullptr;
    }
    if (arena_ && owner_) {
        if (!arena_->Release(owner_))
            return false;
        owner_ = nullptr;
    }
    arena_ = nullptr;
    manager_ = nullptr;
    state_.live = false;
    state_.clones = 0;
    return true;
}
} // namespace dokodemo::recovered
