#include "game_content_native.h"
#include "game_boot_native.h"
#include "game_primitives.h"
#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace dokodemo::recovered {
namespace {
GameFsStatus FormatFailure(const char *text) {
    return {GameFsCode::MalformedDirectory, 0, text};
}
} // namespace
GameFsStatus GameFontResource::Initialize(PackageView image) {
    Close();
    try {
        const auto size = image.Describe().size;
        if (size < 0x3C || image.ReadLE32(0) != 0x31544658)
            return FormatFailure("expected XFT1 image");
        const int32_t gw = int16_t(image.ReadLE16(4)), gh = int16_t(image.ReadLE16(6));
        const int32_t count = int16_t(image.ReadLE16(0x18));
        const auto stride = image.ReadLE32(0x28), offset = image.ReadLE32(0x2C);
        const int32_t auxiliary_div8 = int32_t(image.ReadLE32(0x34)) / 8;
        const auto auxiliary_offset = image.ReadLE32(0x38);
        if (gw <= 0 || gh <= 0 || count <= 0 || count > 4096 || !stride || auxiliary_div8 <= 0 ||
            uint64_t(offset) + uint64_t(count) * stride > size || auxiliary_offset >= size ||
            !image.ReadLE16(0x20) || !image.ReadLE16(0x24))
            return FormatFailure("XFT texture/palette geometry outside actual member");
        std::vector<GameFontTexture> textures;
        textures.reserve(size_t(count));
        for (int32_t i = 0; i < count; ++i)
            textures.push_back({image.Subview(uint64_t(offset) + uint64_t(i) * stride, stride),
                                image.ReadU8(0x1C), image.ReadLE16(0x20), image.ReadLE16(0x24)});
        // 9435C publishes a pointer plus trunc(image.s32[34]/8). It does not
        //  establish a full glyph/palette array length. Borrow the checked tail;
        //  later consumers must check their own requested extent.
        auto auxiliary = image.Subview(auxiliary_offset, size - auxiliary_offset);
        image_ = std::move(image);
        auxiliary_ = std::move(auxiliary);
        textures_ = std::move(textures);
        state_ = {};
        state_.ready = true;
        state_.image_bytes = uint32_t(size);
        state_.texture_count = uint32_t(count);
        state_.texture_bytes = stride;
        state_.auxiliary_offset = auxiliary_offset;
        state_.auxiliary_div8 = uint32_t(auxiliary_div8);
        state_.glyph_width = gw;
        state_.glyph_height = gh;
        return {};
    } catch (const PackageStorageError &error) {
        return {GameFsCode::MalformedDirectory, 0, error.what()};
    }
}
const GameFontTexture *GameFontResource::Texture(uint32_t index) const {
    return state_.ready && index < textures_.size() ? &textures_[index] : nullptr;
}
void GameFontResource::Close() {
    textures_.clear();
    auxiliary_.Reset();
    image_.Reset();
    state_ = {};
}
GameFsStatus GameFontResource::RasterizeCp932(const std::vector<std::string> &lines, uint32_t max_width,
                                              GameFontBitmap &out, GameFontDrawRecord *records,
                                              uint32_t capacity) const {
    try {
        if (!state_.ready || !max_width || max_width > 480)
            return FormatFailure("font raster ownership/width");
        const auto type = image_.ReadU8(12);
        const auto format = image_.ReadLE32(28), palette_format = image_.ReadLE32(48);
        const auto per_page = int16_t(image_.ReadLE16(20)), columns = int16_t(image_.ReadLE16(22));
        const auto palette_entries = image_.ReadLE32(52), glyph_count = image_.ReadLE32(8),
                   width_at = image_.ReadLE32(16);
        // Checked finite atlas domain. No >=2 glyph-cache fallback and no
        // guess about CLUT format/length from an arbitrary auxiliary tail.
        if (type > 1 || format != 4 || palette_format != 3 || palette_entries != 16 || per_page <= 0 ||
            columns <= 0)
            return {GameFsCode::Unavailable, 0, "unsupported XFT atlas/CLUT domain"};
        const auto palette = image_.Subview(state_.auxiliary_offset, 64);
        struct Placed {
            uint32_t index, x, y, width;
        };
        std::vector<Placed> placed;
        uint32_t x = 0, y = 0;
        for (const auto &text : lines) {
            for (size_t pos = 0; pos < text.size();) {
                Glyph glyph;
                if (!read_glyph(reinterpret_cast<const uint8_t *>(text.data() + pos), text.size() - pos,
                                glyph))
                    return FormatFailure("truncated actual CP932 message");
                pos += glyph.consumed;
                if (glyph.index == 10) {
                    x = 0;
                    y += 23;
                    continue;
                }
                if (glyph.index < 32 || uint32_t(uint16_t(glyph.index - 32)) >= glyph_count)
                    return FormatFailure("message glyph index outside XFT");
                const auto index = uint32_t(uint16_t(glyph.index - 32));
                const auto width = int32_t(int8_t(image_.ReadU8(uint64_t(width_at) + index)));
                if (width < 0 || width > state_.glyph_width)
                    return FormatFailure("source glyph width outside native atlas cell");
                if (x + uint32_t(width) > max_width) {
                    x = 0;
                    y += 23;
                }
                if (y + uint32_t(state_.glyph_height) > 230 || placed.size() >= 1024)
                    return {GameFsCode::Bounds, 0, "TRC ten-line/1024-record native text cap"};
                placed.push_back({index, x, y, uint32_t(width)});
                x += uint32_t(width) + 2;
            }
            x = 0;
            y += 23;
        }
        GameFontBitmap bitmap;
        bitmap.width = max_width;
        bitmap.height = y ? y : 23;
        bitmap.glyphs = uint32_t(placed.size());
        if (bitmap.height > 230)
            return {GameFsCode::Bounds, 0, "TRC native text exceeds ten lines"};
        bitmap.pixels.resize(size_t(bitmap.width) * bitmap.height, 0);
        std::vector<GameFontDrawRecord> local;
        if (!records) {
            local.resize(placed.size());
            records = local.data();
            capacity = uint32_t(local.size());
        }
        if (placed.size() > capacity)
            return {GameFsCode::Bounds, 0, "live glyph record capacity exceeded"};
        uint32_t record_count = 0;
        for (const auto &p : placed) {
            const auto plane = type == 0 ? (p.index / uint32_t(per_page)) % 4 : 0;
            const auto page = type == 0 ? (p.index / uint32_t(per_page)) / 4 : p.index / uint32_t(per_page);
            if (plane)
                return {GameFsCode::Unavailable, 0, "XFT multi-plane CLUT selection unsupported"};
            const auto *texture = Texture(page);
            if (!texture)
                return FormatFailure("glyph page outside source texture records");
            const auto u = (p.index % uint32_t(columns)) * uint32_t(state_.glyph_width);
            const auto v =
                ((p.index % uint32_t(per_page)) / uint32_t(columns)) * uint32_t(state_.glyph_height);
            if (u + p.width > texture->width || v + uint32_t(state_.glyph_height) > texture->height)
                return FormatFailure("glyph quad outside texture page");
            auto *record = new (records + record_count++) GameFontDrawRecord{};
            record->page = page;
            record->clut_mode = image_.ReadU8(48);
            record->plane = uint8_t(plane);
            record->x = float(p.x);
            record->y = float(p.y);
            record->u = float(u);
            record->v = float(v);
            record->width = int16_t(p.width);
            record->height = int16_t(state_.glyph_height);
        }
        // The real TRC-owned1024*44 buffer is the draw consumer's input. Test
        // callers without a live text owner use a local typed record vector.
        for (uint32_t ri = 0; ri < record_count; ++ri) {
            const auto &record = records[ri];
            const auto *texture = Texture(record.page);
            for (uint32_t gy = 0; gy < uint32_t(record.height); ++gy)
                for (uint32_t gx = 0; gx < uint32_t(record.width); ++gx) {
                    const auto pixel =
                        uint64_t(uint32_t(record.v) + gy) * texture->width + uint32_t(record.u) + gx;
                    const auto index = (texture->pixels.ReadU8(pixel / 2) >> (4 * (pixel % 2))) & 15;
                    const auto color = NativeColorFromPspAbgr(palette.ReadLE32(index * 4));
                    // 4808C source ABGR8007182B times E4D0's255/128 factor,
                    // native ARGB modulator FF552F0D. Alpha stays from real CLUT.
                    uint32_t modulated = color & 0xFF000000;
                    constexpr uint32_t modulation = 0xFF552F0D;
                    for (unsigned shift = 0; shift < 24; shift += 8)
                        modulated |= ((((color >> shift) & 255) * ((modulation >> shift) & 255)) / 255)
                                     << shift;
                    bitmap.pixels[size_t(uint32_t(record.y) + gy) * bitmap.width + uint32_t(record.x) + gx] =
                        modulated;
                }
        }
        out = std::move(bitmap);
        return {};
    } catch (const std::exception &e) {
        return {GameFsCode::MalformedDirectory, 0, e.what()};
    }
}

GameContentBoot::~GameContentBoot() {
    (void)Close();
}
bool GameContentBoot::InitializeBB8(GameArena &arena, GameVmBoot &manager) {
    if (arena_)
        return false;
    arena_ = &arena;
    manager_ = &manager;
    state_ = {};
    // Source request sizes40/1268 are metadata. Native typed layouts and task
    // records use their actual sizeof/alignment, not fake PSP object padding.
    if (void *memory = arena.Allocate(sizeof(Vsync), alignof(Vsync))) {
        vsync_ = new (memory) Vsync{};
        vsync_task_ = manager.AcquireTask();
        if (vsync_task_ == UINT32_MAX) {
            (void)Close();
            return false;
        }
        state_.vsync_owner = true;
        state_.registered_records += 5;
    }
    if (void *memory = arena.Allocate(sizeof(LoadMan), alignof(LoadMan))) {
        loadman_ = new (memory) LoadMan{};
        constexpr char name[] = "/PSP_GAME/USRDIR/data/ksc/neko.ksc";
        static_assert(sizeof(name) == 35);
        std::copy_n(name, sizeof(name), loadman_->configured_script.begin());
        load_task_ = manager.AcquireTask();
        if (load_task_ == UINT32_MAX) {
            (void)Close();
            return false;
        }
        state_.loadman_owner = true;
        ++state_.registered_records;
    }
    state_.bb8_returned = true;
    return true; // each failed allocation skips only its ctor
}
bool GameContentBoot::Reset24994() {
    if (!state_.bb8_returned || state_.loading_ticks)
        return false;
    reset_bytes_.fill(0); // explicit source reset, not BSS inference
    const auto word = [this](uint32_t at, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            reset_bytes_[at + i] = uint8_t(value >> (i * 8));
    };
    // 39B80/39B20/39D64:11 slots of6196 bytes,28 entries of220 bytes.
    //  These are the actual explicit -1 producers following the whole clear.
    for (uint32_t slot = 0; slot < 11; ++slot) {
        const auto base = 276 + slot * 6196;
        for (uint32_t entry = 0; entry < 28; ++entry)
            for (uint32_t offset : {4u, 0xD0u, 0xD4u, 0xD8u})
                word(base + entry * 220 + offset, UINT32_MAX);
        word(base + 0x1830, UINT32_MAX);
    }
    reset_lookup_.fill(-1); // separate raw946C0 table,28 signed words
    // Reached setters24B1C/24B34/24B4C. Native byte backing preserves the
    // remaining explicit reset without assigning unknown resource meanings.
    reset_bytes_[0x10] = 2;
    reset_bytes_[0x14] = 3;
    reset_bytes_[0x18] = 3;
    state_.state_reset = true;
    return true;
}
uint32_t GameContentBoot::StateWord(uint32_t at) const {
    if (at > reset_bytes_.size() - 4)
        throw std::out_of_range("game state word");
    uint32_t word = 0;
    for (unsigned i = 0; i < 4; ++i)
        word |= uint32_t(reset_bytes_[at + i]) << (8 * i);
    return word;
}
int32_t GameContentBoot::ResetLookupWord(uint32_t index) const {
    return reset_lookup_.at(index);
}
bool GameContentBoot::ConfigureImportCallback5334C() {
    if (!state_.state_reset)
        return false;
    // Native registration only. PSP subinterrupt30 is not installed or
    // synthetically fired;1424's SGX update remains disabled with SGX itself.
    state_.import_callback_configured = true;
    state_.import_callback_registered = false; // explicitly unavailable, not a successful service result
    return true;
}
bool GameContentBoot::TickLoading(GameFilesystem &filesystem) {
    if (!state_.state_reset || !state_.import_callback_configured || !vsync_ || !loadman_ || state_.closed) {
        state_.status = {GameFsCode::Unavailable, 0, "cold loading owners not available"};
        return false;
    }
    if (state_.blocked)
        return true;
    ++state_.loading_ticks;
    try {
        // CVSync phase0/25258 runs BEFORE CLoadMan phase0/199DC.
        if (state_.mount_state == 1) {
            // 2974/1E28 creates the queued node; it does NOT read yet.
            state_.mount_state = 2;
        } else if (state_.mount_state == 2) {
            // Next290C/1C3C really reads and binds the completed native node.
            PackageView package;
            auto status = filesystem.LoadMember("DATA.BPM", package);
            if (!status) {
                state_.status = status;
                return false;
            }
            auto directory = BpDirectory::Parse(package, package.Describe().size);
            package_ = std::move(package);
            directory_ = std::move(directory);
            state_.package_bytes = uint32_t(package_.Describe().size);
            state_.package_slots = directory_.Header().slots;
            state_.package_mounted = true;
            // Native read has really completed; source poll2CA4 then250A4
            // publishes the completed package bit and retires the mount job.
            state_.completed_bits |= 1;
            state_.mount_state = 0;
        }
        switch (state_.load_state) {
        case 1: // 17B4C ->19AA8(next1,flag1)
            state_.next_scene = 1;
            state_.load_state = 2;
            break;
        case 2: // 17B84's cold17BCC branch
            state_.previous_scene = state_.current_scene;
            state_.current_scene = state_.next_scene;
            if (state_.current_scene == 2) {
                state_.load_state = 3;
                state_.scene_substate = 0;
            } else {
                state_.requested_bits |= 1;
                state_.load_state = 4;
            }
            break;
        case 3: // 17D90 scene2, no changed mount bits or old4F0 owner
            if (state_.scene_substate == 0)
                state_.scene_substate = 4;
            else if (state_.scene_substate == 4)
                state_.scene_substate = 5;
            else if (state_.scene_substate == 5) {
                state_.scene_substate = 0;
                state_.load_state = 4;
            } else {
                state_.status = {GameFsCode::Unavailable, 0, "unsupported scene2 transition substate"};
                return false;
            }
            break;
        case 4: // 18160/251F0
            if (state_.current_scene == 2)
                state_.load_state = 6; // no requested/owned difference,18224
            else {
                state_.owned_bits |= 1;
                state_.mount_state = 1;
                state_.load_state = 5;
            }
            break;
        case 5: // 182C0 waits for actual published package completion
            if (state_.completed_bits & 1)
                state_.load_state = 6;
            break;
        case 6: { // 18E84's first resource and real E364 consumer
            if (state_.current_scene == 2) {
                // 4A91C's real source-selected file, then551E4/5536C metadata
                // consumer. Native pool registration is separate from a mixer.
                PackageView image;
                auto status = OpenResource("/PSP_GAME/USRDIR/data/sound/se/SE_SYS.sgd", image);
                if (!status) {
                    state_.status = status;
                    return false;
                }
                status = sound_bank_.Register(std::move(image));
                if (!status) {
                    state_.status = status;
                    return false;
                }
                state_.sound_bank = sound_bank_.Snapshot();
                if (!state_.sound_bank.metadata_ready || state_.sound_bank.native_metadata_slot <= 0) {
                    state_.status = {GameFsCode::Unavailable, 0, "4AAD4 bank metadata not ready"};
                    return false;
                }
                state_.scene_substate = 1;
                // Do not invent a MemoryStick device state. The next real call
                // 4E67C must select an actual native policy and dialog consumer.
                state_.frontier_pc = 0x4E67C;
                state_.blocked = true;
                break;
            }
            PackageView image = directory_.Open("FONT\\FONT.XFT");
            auto status = font_.Initialize(std::move(image));
            if (!status) {
                state_.status = status;
                return false;
            }
            state_.font = font_.Snapshot();
            state_.font_ready = true;
            // Next source operation18EE8 allocates80 then invokesD54C.
            // No unimplemented constructor or following scene change succeeds.
            state_.frontier_pc = 0x18EE8;
            state_.blocked = true;
            break;
        }
        default:
            state_.status = {GameFsCode::Unavailable, 0, "unsupported cold CLoadMan state"};
            return false;
        }
        state_.status = {};
        return true;
    } catch (const PackageStorageError &error) {
        state_.status = {GameFsCode::MalformedDirectory, 0, error.what()};
        return false;
    }
}
GameFsStatus GameContentBoot::OpenResource(const std::string &path, PackageView &out) {
    try {
        // 20DC: FIRST backslash; only if none, FIRST slash. The nested name is
        // passed unchanged, including interior backslashes and letter case.
        auto split = path.find('\\');
        if (split == std::string::npos)
            split = path.find('/');
        if (split != std::string::npos) {
            auto mount = path.substr(0, split);
            for (auto &c : mount)
                if (c >= 'a' && c <= 'z')
                    c = char(c - 'a' + 'A');
            if (mount == "DATA" && state_.package_mounted) {
                out = directory_.Open(path.substr(split + 1));
                ++state_.opened_resources;
                return {};
            }
        }
        if (platform_) {
            auto status = platform_->OpenResource(path, out);
            if (status)
                ++state_.opened_resources;
            return status;
        }
        return {GameFsCode::NotFound, 0, "resource mount/direct source absent"};
    } catch (const PackageStorageError &e) {
        return {e.code() == PackageStorageCode::NotFound ? GameFsCode::NotFound
                                                         : GameFsCode::MalformedDirectory,
                0, e.what()};
    }
}
GameFsStatus GameContentBoot::InitializeSceneResources(GameResourceSource &platform) {
    if (!state_.font_ready || state_.frontier_pc != 0x18EE8 || !arena_ || !manager_)
        return {GameFsCode::Unavailable, 0, "scene continuation requires live scene1 font/package"};
    platform_ = &platform;
    const auto status = scene_ui_.Initialize(*this, *arena_, *manager_, font_);
    state_.scene_ui = scene_ui_.Snapshot();
    state_.status = status;
    if (!status)
        return status;
    state_.registered_records += state_.scene_ui.records;
    // 18F38 calls39B80's slot defaults again, not24994's global clear.
    for (uint32_t slot = 0; slot < 11; ++slot) {
        const auto base = 276 + slot * 6196;
        for (uint32_t entry = 0; entry < 28; ++entry)
            for (uint32_t offset : {4u, 0xD0u, 0xD4u, 0xD8u})
                std::fill_n(reset_bytes_.begin() + base + entry * 220 + offset, 4, uint8_t(255));
        std::fill_n(reset_bytes_.begin() + base + 0x1830, 4, uint8_t(255));
    }
    // Cold flag460 is actually1:19AA8(next2,flag1). No KSC request occurs.
    state_.next_scene = 2;
    state_.load_state = 2;
    state_.scene_substate = 0;
    state_.blocked = false;
    state_.frontier_pc = 0x17B84;
    return {};
}
bool GameContentBoot::DrawColdFrame(GameGraphics &graphics) {
    const auto ok = scene_ui_.DrawColdFrame(graphics);
    state_.scene_ui = scene_ui_.Snapshot();
    return ok;
}
GameFsStatus GameContentBoot::ShowUnavailableMemoryStick(uint32_t native_error) {
    if (state_.frontier_pc != 0x4E67C || !state_.sound_bank.metadata_ready)
        return {GameFsCode::Unavailable, 0, "MemoryStick query not reached"};
    const auto status = scene_ui_.ShowStorageDialog(font_, native_error);
    state_.status = status;
    state_.scene_ui = scene_ui_.Snapshot();
    if (status) {
        ++state_.loading_ticks;
        state_.scene_substate = 2;
        state_.frontier_pc = 0x18FCC;
        state_.blocked = true;
    }
    return status;
}
bool GameContentBoot::Close() {
    if (!scene_ui_.Close())
        return false;
    sound_bank_.Close();
    ClearTextureCache();
    platform_ = nullptr;
    font_.Close();
    directory_.Reset();
    package_.Reset(); // before filesystem leases
    if (load_task_ != UINT32_MAX && !manager_->ReleaseTask(load_task_))
        return false;
    load_task_ = UINT32_MAX;
    if (loadman_) {
        loadman_->~LoadMan();
        if (!arena_->Release(loadman_))
            return false;
        loadman_ = nullptr;
    }
    if (vsync_task_ != UINT32_MAX && !manager_->ReleaseTask(vsync_task_))
        return false;
    vsync_task_ = UINT32_MAX;
    if (vsync_) {
        vsync_->~Vsync();
        if (!arena_->Release(vsync_))
            return false;
        vsync_ = nullptr;
    }
    arena_ = nullptr;
    manager_ = nullptr;
    state_.vsync_owner = state_.loadman_owner = state_.font_ready = state_.package_mounted = false;
    state_.registered_records = 0;
    state_.font = {};
    state_.scene_ui = scene_ui_.Snapshot();
    state_.sound_bank = sound_bank_.Snapshot();
    state_.closed = true;
    return true;
}
} // namespace dokodemo::recovered
