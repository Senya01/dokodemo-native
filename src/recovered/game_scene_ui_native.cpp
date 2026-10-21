#include "game_scene_ui_native.h"
#include "game_boot_native.h"
#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>

namespace dokodemo::recovered {
bool GameTransition::Initialize(const std::array<float, 2> &center) {
    if (state_.live)
        return false;
    state_ = {};
    if (!SetCenter(center))
        return false;
    state_.live = true;
    return true;
}
bool GameTransition::SetCenter(const std::array<float, 2> &center) {
    if (!std::isfinite(center[0]) || !std::isfinite(center[1]))
        return false;
    state_.x = (std::clamp)(center[0], -240.0f, 240.0f);
    state_.y = (std::clamp)(center[1], -136.0f, 136.0f);
    state_.maximum_radius = 0;
    for (float x : {-240.5f, 240.5f})
        for (float y : {-136.5f, 136.5f})
            state_.maximum_radius = (std::max)(state_.maximum_radius, std::hypot(x - state_.x, y - state_.y));
    SetDuration(state_.duration);
    return true;
}
void GameTransition::SetDuration(uint32_t ticks) {
    state_.duration = ticks;
    state_.increment = ticks ? state_.maximum_radius / float(ticks) : 0;
}
void GameTransition::Open(bool immediate) {
    if (!state_.live || state_.state == 1 || state_.state == 2)
        return;
    state_.state = immediate ? 2u : 1u;
    if (immediate)
        state_.radius = state_.maximum_radius;
}
void GameTransition::CloseWipe(bool immediate) {
    if (!state_.live)
        return;
    state_.state = immediate ? 0u : 3u;
    if (immediate)
        state_.radius = -50;
}
void GameTransition::Update() {
    if (!state_.live)
        return;
    if (state_.state == 1) {
        state_.radius += state_.increment;
        if (state_.radius >= state_.maximum_radius) {
            state_.radius = state_.maximum_radius;
            state_.state = 2;
        }
    } else if (state_.state == 3) {
        state_.radius -= state_.increment;
        if (state_.radius <= -50) {
            state_.radius = -50;
            state_.state = 0;
        }
    }
}
bool GameTransition::Draw(GameGraphics &graphics) const {
    if (!state_.live)
        return false;
    if (state_.state == 2)
        return true; // actual D754 branch skips drawing
    const float inner = (std::max)(state_.radius, 0.0f);
    const float outer = (std::max)(state_.radius + 50, inner);
    return graphics.DrawRadialFade(state_.x + 240, state_.y + 136, inner, outer);
}

GameSceneUiBootstrap::~GameSceneUiBootstrap() {
    (void)Close();
}
GameFsStatus GameSceneUiBootstrap::Initialize(GameResourceSource &source, GameArena &arena,
                                              GameVmBoot &manager, const GameFontResource &font) {
    if (arena_ || !font.Snapshot().ready)
        return {GameFsCode::Busy, 0, "scene/font ownership precondition"};
    arena_ = &arena;
    manager_ = &manager;
    state_ = {};
    state_.live = true;
    try {
        constexpr uint32_t record_counts[]{2, 1, 1, 2, 2};
        const auto owner = [&](uint32_t index) {
            auto *p = arena.Allocate(sizeof(Owner), alignof(Owner));
            if (!p)
                throw std::runtime_error("native scene owner allocation failed");
            owners_[index] = new (p) Owner{};
            ++state_.owners;
            tasks_[index] = manager.AcquireTask();
            if (tasks_[index] == UINT32_MAX)
                throw std::runtime_error("native scene task slots exhausted");
            state_.records += record_counts[index];
        };
        owner(0);
        owners_[0]->state = 0;
        owners_[0]->group = 5;
        // Static ctor17B0->15F8 really produced this point; not BSS inference.
        if (!transition_.Initialize({{0, 0}}))
            throw std::runtime_error("fade initialization");
        constexpr const char *prefixes[]{"data\\gui\\select\\", "data\\gui\\softkey\\", "data\\gui\\trc\\"};
        constexpr const char *names[]{"SELECT.CB2A", "SOFTKEY.CB2A", "TRC.CB2A"};
        constexpr uint32_t capacities[]{1024, 41, 1024};
        for (uint32_t i = 0; i < 3; ++i) {
            owner(i + 1);
            auto resource = std::make_shared<GameUiResource>();
            auto status = resource->Load(source, std::string(prefixes[i]) + names[i], prefixes[i]);
            if (!status)
                return status;
            resources_[i] = std::move(resource);
            if (!clones_[i].Clone(resources_[i]))
                throw std::runtime_error("UI clone construction failed");
            const auto bind = [&](uint32_t node, uint32_t clip, uint32_t frame = 0) {
                if (!clones_[i].Bind(node, clip, frame))
                    throw std::runtime_error("source-selected UI base pose is invalid");
            };
            const auto control = [&](uint32_t clip, uint32_t frame = 0, uint8_t mode = 0,
                                     uint8_t active = 0) {
                auto *c = clones_[i].AddControl(clip);
                if (!c)
                    throw std::runtime_error("source-selected UI control unavailable");
                c->frame = frame;
                c->mode = mode;
                c->active = active;
            };
            if (i == 0) {
                bind(0, 0);
                bind(1, 1);
                bind(2, 13);
                bind(3, 25, 1);
                control(1);
                control(0, 1, 2, 1);
                control(25, 2, 2, 1);
            } else if (i == 1) {
                bind(0, 0);
                bind(151, 1);
                bind(1, 7);
                bind(2, 8);
                bind(3, 9);
                bind(4, 10);
                bind(7, 13);
                bind(6, 12);
                bind(5, 11);
                bind(8, 14);
                bind(9, 15);
                control(15);
                control(0, 1, 2, 1);
                control(2, 1, 2, 1);
                control(6, 1, 2, 1);
                control(11, 1, 2, 1);
                control(5, 13, 2, 1);
                control(14, 1, 2, 1);
                for (uint32_t clip : {4u, 7u, 8u, 9u, 10u, 13u, 12u})
                    control(clip);
            } else {
                bind(0, 0);
                bind(1, 1);
                control(0, 1);
                control(1, 1, 2, 1);
            }
            ++state_.ui_resources;
            state_.ui_nodes += uint32_t(resources_[i]->InitialNodes().size());
            state_.ui_clips += uint32_t(resources_[i]->Clips().size());
            // E4A0 capacities count44-byte draw records, not individual bytes.
            const auto bytes = capacities[i] * 44;
            text_buffers_[i] = arena.Allocate(bytes);
            if (!text_buffers_[i])
                throw std::runtime_error("glyph record allocation failed");
            state_.text_capacity_bytes += bytes;
            if (i == 1) {
                constexpr const char *dictionaries[]{"NJCON.A",  "NJBASE.A",  "NJFZK.A", "NJTAN.A",
                                                     "NJYOMI.A", "NJLEARN.A", "CUSTOM.A"};
                for (uint32_t d = 0; d < 7; ++d) {
                    PackageView data;
                    status = source.OpenResource(std::string("data\\gui\\DIC\\") + dictionaries[d], data);
                    if (!status)
                        return status;
                    status = dictionaries_[d].Attach(std::move(data), d ? 3 : 0);
                    if (!status) {
                        status.detail = std::string(dictionaries[d]) + ": " + status.detail;
                        return status;
                    }
                    ++state_.borrowed_dictionaries;
                }
                if (!dictionaries_[7].InitializeMutable(arena, 1) ||
                    !dictionaries_[8].InitializeMutable(arena, 2))
                    throw std::runtime_error("mutable NJ dictionaries unavailable");
                state_.mutable_dictionaries = 2;
            }
        }
        owner(4);
        auto status = source.TextureResource("data\\gui\\moving\\moving.GIM", moving_);
        if (!status)
            return status;
        state_.textures = source.TextureCacheSize();
        state_.texture_pixels = source.TextureCachePixels();
        // All cloned roots remain hidden by97A54/964F8. We do not call the
        // unimplemented visible96F20/callback renderer or advertise text draw.
        state_.constructors_returned = true;
        state_.scene1_returned = true;
        return {};
    } catch (const std::exception &e) {
        return {GameFsCode::Unavailable, 0, e.what()};
    }
}
bool GameSceneUiBootstrap::DrawColdFrame(GameGraphics &graphics) {
    if (!state_.constructors_returned)
        return false;
    for (size_t i = 0; i < clones_.size(); ++i) {
        auto &clone = clones_[i];
        if (clone.VisibleRoots() && !(i == 2 && state_.storage_dialog))
            return false;
        std::string error;
        if (!clone.Update(error))
            return false;
    }
    // Source cold phase6 owners are inert; phase9 D708 really draws its fade.
    if (!graphics.Clear(0xFF000000, 0) || !transition_.Draw(graphics))
        return false;
    if (state_.storage_dialog) {
        // Native tick quanta2 /10, using the retained backed initial value.
        // PSP runtime timing producer/matrix matching remain unrecovered.
        state_.dialog_progress = (std::min)(1.0f, state_.dialog_progress + 0.2f);
        const auto scale = state_.dialog_progress;
        const auto w = (std::max)(1u, uint32_t(float(dialog_width_) * scale));
        const auto h = (std::max)(1u, uint32_t(float(dialog_height_) * scale));
        std::vector<uint32_t> pixels(size_t(w) * h);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                pixels[size_t(y) * w + x] =
                    dialog_pixels_[size_t(uint64_t(y) * dialog_height_ / h) * dialog_width_ +
                                   uint64_t(x) * dialog_width_ / w];
        const auto x = (480 - int32_t(w)) / 2, y = (272 - int32_t(h)) / 2;
        // Explicit functional TRC panel adapter. Original FONT glyphs/text and
        // state progression are used; this rectangle is not claimed CB2A/VFPU
        // pixel matching or the general96F20 callback renderer.
        if (!graphics.FillRectangle(x - 16, y - 16, x + int32_t(w) + 16, y + int32_t(h) + 16, 0xFFE9E3CE) ||
            !graphics.DrawImage(pixels, w, h, x, y))
            return false;
        state_.text_drawn = true;
        state_.awaiting_confirmation = scale == 1;
    }
    if (!graphics.SwapBuffers())
        return false;
    ++state_.native_frames;
    return true;
}
GameFsStatus GameSceneUiBootstrap::ShowStorageDialog(const GameFontResource &font, uint32_t error) {
    if (!state_.constructors_returned || state_.storage_dialog)
        return {GameFsCode::Busy, 0, "TRC native dialog ownership/state"};
    // Exact source CP932 bytes from D8BA8/D8B2C/D8B64, retained in
    // functional-first/scene-source-v1/LITERALS.json. No translated placeholder.
    const std::vector<std::string> messages{
        "\x22\x83\x81\x83\x82\x83\x8a\x81\x5b\x83\x58\x83\x65\x83\x42\x83\x62\x83\x4e\x22\x82\xaa\x90\xb3\x82"
        "\xb5\x82\xad\x8d\xb7\x82\xb5\x8d\x9e\x82\xdc\x82\xea\x82\xc4\x82\xa2\x82\xdc\x82\xb9\x82\xf1\x81"
        "\x42",
        "\x82\xb1\x82\xcc\x83\x51\x81\x5b\x83\x80\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xf0\x83\x5a\x81\x5b\x83"
        "\x75\x82\xb7\x82\xe9\x82\xc9\x82\xcd\x38\x30\x30\x4b\x42\x88\xc8\x8f\xe3\x95\x4b\x97\x76\x82\xc5\x82"
        "\xb7\x81\x42",
        "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
        "\x20\x20\x20\x20\x20\x20\x20\x81\x9b\x81\x46\x8a\x6d\x94\x46"};
    GameFontBitmap bitmap;
    auto status =
        font.RasterizeCp932(messages, 320, bitmap, static_cast<GameFontDrawRecord *>(text_buffers_[2]), 1024);
    if (!status)
        return status;
    if (!clones_[2].ShowRoot(0))
        return {GameFsCode::Unavailable, 0, "TRC root0 unavailable"};
    dialog_pixels_ = std::move(bitmap.pixels);
    dialog_width_ = bitmap.width;
    dialog_height_ = bitmap.height;
    state_.storage_dialog = true;
    state_.dialog_glyphs = bitmap.glyphs;
    state_.storage_error = error;
    state_.dialog_progress = 0;
    return {};
}
GameSceneUiSnapshot GameSceneUiBootstrap::Snapshot() const {
    auto out = state_;
    out.transition = transition_.Snapshot();
    out.visible_roots = 0;
    for (const auto &clone : clones_)
        out.visible_roots += clone.VisibleRoots();
    return out;
}
bool GameSceneUiBootstrap::Close() {
    std::vector<uint32_t>().swap(dialog_pixels_);
    dialog_width_ = dialog_height_ = 0;
    moving_.reset();
    for (auto &c : clones_)
        c.Close();
    for (auto &r : resources_)
        r.reset();
    for (auto d = dictionaries_.rbegin(); d != dictionaries_.rend(); ++d)
        if (!d->Close())
            return false;
    if (arena_) {
        for (auto b = text_buffers_.rbegin(); b != text_buffers_.rend(); ++b)
            if (*b) {
                if (!arena_->Release(*b))
                    return false;
                *b = nullptr;
            }
        for (size_t i = owners_.size(); i--;) {
            if (tasks_[i] != UINT32_MAX) {
                if (!manager_->ReleaseTask(tasks_[i]))
                    return false;
                tasks_[i] = UINT32_MAX;
            }
            if (owners_[i]) {
                owners_[i]->~Owner();
                if (!arena_->Release(owners_[i]))
                    return false;
                owners_[i] = nullptr;
            }
        }
    }
    arena_ = nullptr;
    manager_ = nullptr;
    transition_.Retire();
    state_.live = false;
    state_.storage_dialog = state_.awaiting_confirmation = false;
    state_.owners = state_.records = state_.ui_resources = state_.ui_nodes = state_.ui_clips =
        state_.textures = state_.texture_pixels = 0;
    state_.borrowed_dictionaries = state_.mutable_dictionaries = state_.text_capacity_bytes = 0;
    return true;
}
GameFsStatus GameSoundBank::Register(PackageView image) {
    if (state_.metadata_ready)
        return {GameFsCode::Busy, 0, "sound bank already registered"};
    try {
        const auto size = image.Describe().size;
        if (size < 16 || size > UINT32_MAX || image.ReadLE32(0) != 0x44584753)
            return {GameFsCode::MalformedDirectory, 0, "SGXD signature/extent"};
        const auto end = image.ReadLE32(8);
        if (end < 16 || end > size)
            return {GameFsCode::MalformedDirectory, 0, "SGXD chunk end outside file"};
        GameSoundBankSnapshot next;
        next.image_bytes = uint32_t(size);
        for (uint64_t at = 16; at < end;) {
            if (end - at < 8)
                return {GameFsCode::MalformedDirectory, 0, "SGXD truncated chunk header"};
            const auto tag = image.ReadLE32(at), bytes = image.ReadLE32(at + 4);
            if (bytes > end - at - 8)
                return {GameFsCode::MalformedDirectory, 0, "SGXD chunk extent"};
            ++next.chunks;
            next.regions |= tag == 0x444E4752;
            next.waves |= tag == 0x45564157;
            next.sequences |= tag == 0x44514553;
            next.names |= tag == 0x454D414E;
            at += uint64_t(bytes) + 8;
        }
        // 55250's actual default64/head insertion gives slot63 first. Native
        // metadata slots are kept distinct from SGX mixer/channel acceptance.
        int slot = -1;
        for (int i = 63; i >= 0; --i)
            if (!slots_[size_t(i)]) {
                slot = i;
                break;
            }
        if (slot < 0)
            return {GameFsCode::Busy, 0, "native metadata bank pool exhausted"};
        slots_[size_t(slot)] = true;
        next.native_metadata_slot = slot;
        next.metadata_ready = true;
        image_ = std::move(image);
        state_ = next;
        return {};
    } catch (const std::exception &e) {
        return {GameFsCode::MalformedDirectory, 0, e.what()};
    }
}
} // namespace dokodemo::recovered
