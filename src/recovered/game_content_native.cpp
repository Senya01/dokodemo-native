#include "game_content_native.h"
#include "game_boot_native.h"
#include "game_primitives.h"
#include "model_formats.hpp"
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
                                              uint32_t capacity,uint32_t native_modulation) const {
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
        uint32_t x = 0, y = 0,advance_width=0;
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
                advance_width=(std::max)(advance_width,x);
            }
            x = 0;
            y += 23;
        }
        GameFontBitmap bitmap;
        bitmap.width = max_width;
        bitmap.height = y ? y : 23;
        bitmap.glyphs = uint32_t(placed.size());
        bitmap.advance_width=advance_width;
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
                    uint32_t modulated = 0;
                    for (unsigned shift = 0; shift < 32; shift += 8)
                        modulated |= ((((color >> shift) & 255) * ((native_modulation >> shift) & 255)) / 255)
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
            if (mount == "TITLE" && state_.title_mounted) {
                out = title_directory_.Open(path.substr(split + 1));
                ++state_.opened_resources;
                return {};
            }
            if(mount=="BG_ROOM"&&game_filesystem_) {
                // Named functional deferred mount: only the actual requested
                // source31 package is read,with full checked extent/lease.
                if(!room_mounted_) {
                    const auto status=game_filesystem_->LoadMember("BG_ROOM.BPM",room_package_);
                    if(!status)return status;
                    room_directory_=BpDirectory::Parse(room_package_,room_package_.Describe().size);
                    room_mounted_=true;
                }
                out=room_directory_.Open(path.substr(split+1));++state_.opened_resources;return {};
            }
            if(mount=="NEKO"&&state_.title.selected_actor==0&&game_filesystem_){
                // Source8E64:25438(actor+21),relocation-bound actor0 NEKO.
                // Lazy native admission keeps the original requested names.
                if(!actor_model_mounted_){
                    const auto status=game_filesystem_->LoadMember("NEKO.BPM",actor_model_package_);
                    if(!status)return status;
                    actor_model_directory_=BpDirectory::Parse(actor_model_package_,actor_model_package_.Describe().size);
                    actor_model_mounted_=true;
                }
                out=actor_model_directory_.Open(path.substr(split+1));++state_.opened_resources;return {};
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
bool GameContentBoot::ProcessDialogInput(const GameInputFrame &frame) {
    if (state_.closed)
        return false;
    if(state_.actor_script.started){
        const bool accepted=state_.actor_script.awaiting_message&&actor_message_gate_.Observe(frame,Circle)
                            &&actor_script_.AcknowledgeMessage();
        state_.actor_message_input_armed=actor_message_gate_.Armed();
        return accepted;
    }
    if(state_.title_constructor_returned){
        if(scene_ui_.Snapshot().storage_dialog)return scene_ui_.HandleDialogInput(frame);
        if(scene_ui_.Editor().live&&scene_ui_.Editor().source_state!=3)return false;
        title_.Input(frame);return false;
    }
    const auto accepted = scene_ui_.HandleDialogInput(frame);
    state_.scene_ui = scene_ui_.Snapshot();
    return accepted;
}
GameFsStatus GameContentBoot::BeginStorage(GameStorage &storage, bool test_prompt) {
    if (state_.frontier_pc != 0x4E67C)
        return {GameFsCode::Unavailable, 0, "native storage query not reached"};
    const auto available = storage.Probe();
    const bool dialog = test_prompt || !available;
    autoload_.Begin(dialog);
    state_.autoload = autoload_.Snapshot();
    if (dialog)
        return ShowUnavailableMemoryStick(test_prompt ? 50 : available.native_error);
    state_.scene_substate = 3;
    state_.blocked = false;
    state_.frontier_pc = 0x4E100;
    return {};
}
bool GameContentBoot::TickInteractive(GameFilesystem &filesystem, GameStorage &storage,
                                      GameGraphics &graphics) {
    if (state_.closed || !state_.scene_ui.live)
        return false;
    if(state_.title_constructor_returned) {
        if(state_.title.scene3_completed)return TickActorDictionary(filesystem,graphics);
        if(state_.load_state==2) {
            state_.previous_scene=state_.current_scene;state_.current_scene=state_.next_scene;
            //Native phase0 folds the no-mount-delta transition into the
            //scene3 handler.193C4->1A7A0->1A89C returns pending whileCC!=3;
            //it does not release TITLE or produce a scene return.
            state_.load_state=6;state_.scene_substate=0;
        }
        state_.status=title_.TickAndDraw(graphics,scene_ui_.TitleTransitionOwner());
        state_.title=title_.Snapshot();state_.scene_ui=scene_ui_.Snapshot();
        state_.frontier_pc=state_.title.frontier_pc;
        state_.blocked=!bool(state_.status);
        if(state_.title.scene3_completed){
            state_.next_scene=4;state_.load_state=2;
            constexpr const char* dictionaries[]{"KSNEKO\\neko.dic","KSROBO\\robo.dic","KSUSAGI\\usagi.dic","KSINU\\inu.dic","KSKAERU\\kaeru.dic"};
            state_.next_resource=dictionaries[state_.title.selected_actor];
        }else if(state_.title.softkey_requested)state_.next_resource="Live native CP932/editor/profile;no KSC request";
        return bool(state_.status);
    }
    //Phase0 observes the previous phase6 result. Reaching TRC3 below does
    //not let CLoadMan consume completion in that same scheduler iteration.
    const auto prior_dialog_state=state_.scene_ui.dialog_state;
    if (!DrawColdFrame(graphics)) {
        state_.status={GameFsCode::Unavailable,0,"native phase6/phase9 drawing failed"};
        return false;
    }
    if (state_.autoload.phase == GameAutoloadPhase::Dialog) {
        if (autoload_.DialogCompleted(prior_dialog_state)) {
            state_.scene_substate = 3;
            state_.blocked = false;
            state_.frontier_pc = 0x4E100;
        }
        state_.autoload = autoload_.Snapshot();
        return true;
    }
    if (state_.autoload.phase == GameAutoloadPhase::Begin) {
        // 249D0 resets the game state before mode0. A missing file cannot
        // invent loaded game fields; the reset0 is what25058 later consumes.
        const auto zero = [this](size_t at, size_t count) {
            std::fill_n(reset_bytes_.begin() + at, count, uint8_t(0));
        };
        for (size_t at : {0u, 4u, 8u, 0x10u, 0x14u, 0x18u, 0x1Cu, 0x20u, 0xA4u, 0xA8u, 0xACu})
            zero(at, 4);
        zero(0xC, 1);
        zero(0x24, 64);
        zero(0x64, 64);
        zero(0xB0, 84);
        zero(0x114, 0x10A3C);
        for (uint32_t slot = 0; slot < 11; ++slot) {
            const auto base = 276 + slot * 6196;
            for (uint32_t entry = 0; entry < 28; ++entry)
                for (uint32_t offset : {4u, 0xD0u, 0xD4u, 0xD8u})
                    std::fill_n(reset_bytes_.begin() + base + entry * 220 + offset, 4, uint8_t(255));
            std::fill_n(reset_bytes_.begin() + base + 0x1830, 4, uint8_t(255));
        }
        reset_bytes_[0x10] = 2;
        reset_bytes_[0x14] = 3;
        reset_bytes_[0x18] = 3;
        autoload_.Tick(storage);
        state_.autoload = autoload_.Snapshot();
        if (state_.autoload.phase != GameAutoloadPhase::TitleRequested) {
            state_.blocked = true;
            state_.frontier_pc = 0x4DF18;
            return true;
        }
        state_.requested_bits |= 1u << 11;
        state_.scene_substate = 9;
        state_.owned_bits |= 1u << 11;
        state_.mount_state = 1;
        state_.blocked = false;
        state_.frontier_pc = 0x18160;
        return true;
    }
    if (state_.mount_state == 1 && (state_.requested_bits & (1u << 11))) {
        state_.mount_state = 2;
        return true; // queued1E28 node, no read on creation
    }
    if (state_.mount_state == 2 && (state_.requested_bits & (1u << 11))) {
        try {
            auto status = filesystem.LoadMember("TITLE.BPM", title_package_);
            if (!status) {
                state_.status = status;
                return false;
            }
            title_directory_ = BpDirectory::Parse(title_package_, title_package_.Describe().size);
            state_.title_mounted = true;
            state_.title_bytes = uint32_t(title_package_.Describe().size);
            state_.title_slots = title_directory_.Header().slots;
            state_.completed_bits |= 1u << 11;
            state_.mount_state = 0;
            // 192D4 frees the first storage buffer before the TITLE constructor.
            autoload_.Close();
            state_.autoload = autoload_.Snapshot();
            // 1A238(active11)->1A3E8->30188. This is an actual requested
            // resource/texture consumer, not a guessed title screen.
            title_ui_ = std::make_shared<GameUiResource>();
            status = title_ui_->Load(*this, "title\\gui\\title\\TITLE.CB2A", "title\\gui\\title\\");
            if (!status) {
                state_.status = status;
                return false;
            }
            if (!title_clone_.Clone(title_ui_)) {
                state_.status={GameFsCode::MalformedDirectory,0,"TITLE native clone failed"};
                return false;
            }
            for (uint32_t i = 0; i < 30; ++i)
                if (!title_clone_.Bind(i, i, 0)) {
                    state_.status = {GameFsCode::MalformedDirectory, 0,
                                     "30188 requires30 valid root/clip bindings"};
                    return false;
                }
            state_.title_ui_ready = true;
            state_.title_nodes = uint32_t(title_ui_->InitialNodes().size());
            state_.title_clips = uint32_t(title_ui_->Clips().size());
            state_.title_textures = uint32_t(title_ui_->Textures().size());
            // 33484's first of ten actor constructors selects toro.DVS.
            // Keep its real image and validate the existing typed model parser.
            // No character transform/skin/animation callback is substituted.
            state_.next_resource = "title\\charmodel_title\\toro.DVS";
            status = OpenResource(state_.next_resource, title_model_image_);
            if (!status) {
                state_.status = status;
                return false;
            }
            model::Bytes bytes(size_t(title_model_image_.Describe().size));
            for (size_t i = 0; i < bytes.size(); ++i)
                bytes[i] = title_model_image_.ReadU8(i);
            const auto model = model::parseDvs(bytes);
            state_.title_model_bytes = uint32_t(bytes.size());
            state_.title_model_nodes = uint32_t(model.nodes.size());
            state_.title_model_materials = uint32_t(model.materials.size());
            state_.frontier_pc = 0x7F68C;
            for (const auto &material : model.materials)
                for (const auto &texture : material.textures) {
                    std::shared_ptr<GameImage> actual_texture;
                    state_.next_resource = "title\\charmodel_title\\gim\\" + texture.name + ".GIM";
                    status = TextureResource(state_.next_resource, actual_texture);
                    if (!status) {
                        state_.status = status;
                        return false;
                    }
                    state_.title_material_resources.push_back(state_.next_resource);
                }
            state_.title_model_read = true;
            status=title_.Initialize(*this,*arena_,*manager_,title_layout_,StateWord(0x4C));
            title_.SetCalendar(calendar_);
            if(status) {
                if(!title_rng_)status={GameFsCode::Unavailable,0,"TITLE requires worker-owned initialized RNG"};
                else status=title_.AttachMenu(title_clone_,font_,*title_rng_,&scene_ui_,calendar_year_);
            }
            state_.title=title_.Snapshot();
            if(!status){state_.status=status;return false;}
            state_.title_constructor_returned=true;
            state_.registered_records+=2;
            state_.next_scene=3;state_.load_state=2;
            state_.next_resource = "Scene3 waits titleCC3; remaining TITLE menu/input/random restart consumers unsupported";
            state_.status = {};state_.blocked = false;state_.frontier_pc=0x30C34;
        } catch (const std::exception &e) {
            state_.status = {GameFsCode::MalformedDirectory, 0, e.what()};
            return false;
        }
    }
    return true;
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
    actor_script_.Close();actor_message_gate_.Stop();state_.actor_message_input_armed=false;state_.actor_script=actor_script_.Snapshot();actor_clock_={};
    actor_runtime_.Close();state_.actor_script.runtime=actor_runtime_.Snapshot();
    actor_model_directory_.Reset();actor_model_package_.Reset();actor_model_mounted_=false;
    room_directory_.Reset();room_package_.Reset();room_mounted_=false;game_filesystem_=nullptr;
    actor_dictionary_.Reset();actor_directory_.Reset();actor_package_.Reset();state_.actor_dictionary_live=false;
    if(!title_.Close())return false;
    state_.title=title_.Snapshot();
    title_clone_.Close();
    title_ui_.reset();
    title_model_image_.Reset();
    title_directory_.Reset();
    title_package_.Reset();
    autoload_.Close();
    state_.autoload = autoload_.Snapshot();
    state_.title_mounted = state_.title_ui_ready = false;
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
bool GameContentBoot::TickActorDictionary(GameFilesystem& filesystem,GameGraphics& graphics){
    if(state_.actor_dictionary_requested){
        if(!state_.actor_script.started)return false;
        const auto before=state_.actor_script;
        state_.status=actor_script_.Tick();state_.actor_script=actor_script_.Snapshot();
        // Existing phase6 transition and group2 authored room consumers stay
        // live even when a later unimplemented script service stops progress.
        scene_ui_.TitleTransitionOwner().Update();
        //Publish ONCE,after both room and text. CopyGameFrame must never see
        //an intermediate room-only front whileFONT is still being decoded.
        const auto rendered=actor_runtime_.Draw(graphics,[&](GameGraphics& surface)->GameFsStatus{
            if(!state_.actor_script.awaiting_message)return {};
            std::string visible;
            for(unsigned char c:state_.actor_script.message){
                if(c==0x16)continue; //NativeNoHighlight adaptation;rawrequest remains unchanged
                if(c<32&&c!=10)return {GameFsCode::Unavailable,0,"Unimplemented reached KSC text formatting control"};
                visible.push_back(char(c));
            }
            GameFontBitmap text;const auto drawn=font_.RasterizeCp932({visible},440,text);
            if(!drawn)return drawn;
            //Named native KSC panel layout,not originalpopup pixelmatching.
            if(!surface.FillRectangle(12,12,468,24+int32_t(text.height),0xFFF5E8CD,65535)
               ||!surface.DrawImage(text.pixels,text.width,text.height,20,16,0xFFFFFFFF,65535))
                return {GameFsCode::Unavailable,0,"Native KSC message draw failed"};
            state_.actor_message_glyphs=text.glyphs;state_.actor_message_ink_pixels=0;
            for(auto p:text.pixels)if(p>>24)++state_.actor_message_ink_pixels;
            ++state_.actor_message_draw_frames;
            return {};
        });
        if(!rendered){state_.status=rendered;state_.blocked=true;return false;}
        state_.actor_script.runtime=actor_runtime_.Snapshot();
        if(state_.actor_script.dictionary.imported){actor_dictionary_.Reset();state_.actor_dictionary_live=false;}
        if(state_.actor_script.awaiting_message&&(!before.awaiting_message||before.messages!=state_.actor_script.messages)){
            actor_message_gate_.Begin();++state_.actor_message_contexts;state_.actor_message_input_armed=false;
        }
        state_.blocked=!bool(state_.status);state_.frontier_pc=0x74C18;
        const auto script_ok=bool(state_.status);
        if(state_.actor_script.runtime.command_blocked){
            state_.blocked=true;state_.frontier_pc=state_.actor_script.runtime.command_frontier;
            state_.status={GameFsCode::Unavailable,0,state_.actor_script.runtime.command_error};
            // Keep independent renderer/input owners responsive while the VM
            // genuinely waits. This is not successful ASC completion.
        }
        // A stopped VM is a semantic frontier,not retirement of independent
        // actor/render owners. Let their real pending phase (including ASC
        // root unwind after8031) run;retain blocked/status/error unchanged.
        // This Boolean reports a live host pump,never game-service success.
        return script_ok||(state_.actor_script.blocked&&state_.actor_script.runtime.live
                          &&!state_.actor_script.runtime.command_blocked);
    }
    state_.actor_dictionary_requested=true;
    const auto actor=state_.title.selected_actor;
    state_.frontier_pc=0x1AF20;
    if(actor>4||!state_.title.profile.stored.live||state_.title.profile.stored.actor!=actor){
        state_.status={GameFsCode::Unavailable,0,"scene4 requires actual confirmed actor profile"};state_.blocked=true;return false;
    }
    constexpr const char* mounts[]{"KSNEKO","KSROBO","KSUSAGI","KSINU","KSKAERU"};
    constexpr const char* names[]{"neko.dic","robo.dic","usagi.dic","inu.dic","kaeru.dic"};
    state_.actor_dictionary_path=std::string(mounts[actor])+"\\"+names[actor];
    state_.next_resource=state_.actor_dictionary_path;
    //Named synchronous native LoadMan adapter: source1A89C requests scene4,
    //25438(actor+3) queues its mount;1C3C reads the real selected BPM before
    //182F4->1AF20 can perform2E7C. No bootstrap prefix masquerades as a body.
    state_.previous_scene=3;state_.current_scene=state_.next_scene=4;
    state_.requested_bits=(state_.requested_bits&~(1u<<11))|(1u<<(actor+3));
    state_.status=filesystem.LoadMember(std::string(mounts[actor])+".BPM",actor_package_);
    if(!state_.status){state_.blocked=true;return false;}
    try{
        state_.actor_package_bytes=uint32_t(actor_package_.Describe().size);
        actor_directory_=BpDirectory::Parse(actor_package_,actor_package_.Describe().size);
        actor_dictionary_=actor_directory_.Open(names[actor]);
        const auto bytes=actor_dictionary_.Describe().size;
        if(!bytes||bytes>UINT32_MAX)throw std::runtime_error("actor dictionary empty/oversized");
        state_.actor_dictionary_bytes=uint32_t(bytes);
        for(uint32_t i=0;i<(std::min)(16u,uint32_t(bytes));++i)state_.actor_dictionary_prefix[i]=actor_dictionary_.ReadU8(i);
        state_.actor_dictionary_ready=state_.actor_dictionary_live=true;++state_.opened_resources;
        state_.owned_bits=(state_.owned_bits&~(1u<<11))|(1u<<(actor+3));state_.completed_bits=state_.owned_bits;
        state_.load_state=6;
        constexpr const char* scripts[]{"NEKO.KSC","ROBO.KSC","USAGI.KSC","INU.KSC","KAERU.KSC"};
        state_.next_resource=std::string(mounts[actor])+"\\"+scripts[actor];
        const auto script_image=actor_directory_.Open(scripts[actor]);
        game_filesystem_=&filesystem;
        state_.status=actor_runtime_.Initialize(actor,actor_package_,scene_ui_.TitleTransitionOwner(),this);
        if(!state_.status)throw std::runtime_error(state_.status.detail);
        state_.frontier_pc=0x19DD4;
        state_.status=actor_runtime_.InitializeScene0(calendar_);
        if(!state_.status)throw std::runtime_error(state_.status.detail);
        state_.frontier_pc=0x230EC;
        state_.status=actor_script_.Initialize(actor_dictionary_,script_image,state_.next_resource,state_.title.profile.stored,actor_clock_,title_rng_,&actor_runtime_);
        state_.actor_script=actor_script_.Snapshot();
        if(state_.status){
            ++state_.opened_resources;state_.blocked=false;
            if(!graphics.Clear(0xFF000000,0)||!graphics.SwapBuffers())throw std::runtime_error("Scene4 clear failed");
            return true;
        }
    }catch(const std::exception& e){state_.status={GameFsCode::MalformedDirectory,0,e.what()};}
    state_.blocked=true;return false;
}
} // namespace dokodemo::recovered
