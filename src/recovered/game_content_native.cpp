#include "game_content_native.h"
#include <atomic>
#include "../core_hash.h"
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
GameFsStatus GameFontResource::RasterizeDialogueCp932(std::string_view message,uint32_t max_width,
                                                    GameFontBitmap& out) const {
    std::string visible;
    for(unsigned char c:message){
        if(c==0x16)continue; // same NativeNoHighlight projection; no source mutation
        if(c<32&&c!=10)return {GameFsCode::Unavailable,0,"Unimplemented reached KSC text formatting control"};
        visible.push_back(char(c));
    }
    // The terminal LF does not place a glyph. Retain interior/explicit blank
    // lines, but do not grow the native panel by an extra trailing empty row.
    if(!visible.empty()&&visible.back()=='\n')visible.pop_back();
    return RasterizeCp932({visible},max_width,out);
}
GameFsStatus GameFontResource::RasterizeCp932(const std::vector<std::string> &lines, uint32_t max_width,
                                              GameFontBitmap &out, GameFontDrawRecord *records,
                                              uint32_t capacity,uint32_t native_modulation,uint32_t native_line_advance,
                                              uint32_t native_letter_spacing) const {
    try {
        if (!state_.ready || !max_width || max_width > 480 || native_line_advance < uint32_t(state_.glyph_height) || native_line_advance > 64 || native_letter_spacing > 16)
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
                    y += native_line_advance;
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
                    y += native_line_advance;
                }
                if (y + uint32_t(state_.glyph_height) > 230 || placed.size() >= 1024)
                    return {GameFsCode::Bounds, 0, "TRC ten-line/1024-record native text cap"};
                placed.push_back({index, x, y, uint32_t(width)});
                x += uint32_t(width) + native_letter_spacing;
                advance_width=(std::max)(advance_width,x);
            }
            x = 0;
            y += native_line_advance;
        }
        GameFontBitmap bitmap;
        bitmap.width = max_width;
        bitmap.height = y ? y : native_line_advance;
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
    static std::atomic<uint64_t> next_diary_io{0};
    diary_io_owner_=++next_diary_io;
    arena_ = &arena;
    manager_ = &manager;
    state_ = {};
    // Native lifecycle placement of17B0->15F8(1,FFFF)->17F0, once before SE5
    // registration. This is a functional lifecycle adaptation, not PSP CRT.
    state_.status=sound_bank_.InitializeSource17F0();if(!state_.status)return false;
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
    records_.ResetCold249D0();
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
    sound_bank_.SetProfileGain(3); // explicit249D0 profile producer, not BSS or per-request reset
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
GameOptionSettingsSnapshot GameContentBoot::OptionSettings()const{
    GameOptionSettingsSnapshot out{!state_.closed&&state_.loadman_owner&&state_.state_reset,diary_io_owner_,1,option_settings_writes_,
        {{StateWord(0x10),StateWord(0x14),StateWord(0x18)}}};
    dokodemo::core::Sha256 hash;hash.update(reset_bytes_.data(),0x10);hash.update(reset_bytes_.data()+0x1C,reset_bytes_.size()-0x1C);
    out.protected_root=hash.digest();return out;
}
GameFsStatus GameContentBoot::RestoreOptionSettings(const GameOptionSettingsSnapshot& expected,const std::array<uint32_t,3>& backup){
    const auto now=OptionSettings();
    if(!now.live||!GameOptionSnapshotExact(now,expected)||backup!=expected.words)
        return {GameFsCode::Unavailable,0,"Options readonly current-owner backup restoration refused"};
    for(uint32_t field=0;field<3;++field){for(uint32_t byte=0;byte<4;++byte)reset_bytes_[0x10+field*4+byte]=uint8_t(backup[field]>>(8*byte));++option_settings_writes_;}
    if(OptionSettings().words!=backup)return {GameFsCode::Unavailable,0,"Options three source setter readbacks disagree"};return {};
}
GameFsStatus GameContentBoot::WriteOptionRight(const GameOptionSettingsSnapshot& expected,uint32_t stage,uint32_t value,GameOptionWriteReceipt& receipt){
    if(state_.closed||state_.main_menu_phase!=7||!state_.main_menu_child_ready||!GameOptionSnapshotExact(expected,OptionSettings()))
        return {GameFsCode::Unavailable,0,"Options current source writer owner unavailable"};
    const auto gui=system_option_.Snapshot();
    auto status=actor_script_.PrepareMenuOptionWrite(main_menu_target_,gui,stage,value,receipt);if(!status)return status;
    const auto at=0x10+receipt.field*4;
    for(uint32_t byte=0;byte<4;++byte)reset_bytes_.at(at+byte)=uint8_t(value>>(8*byte));++option_settings_writes_;
    // The committed receipt is privately retained only after actual full-state
    // readback. It does not accept an externally supplied expected RAM image.
    status=actor_script_.CommitMenuOptionWrite(main_menu_target_,gui,receipt);return status;
}
GameFsStatus GameContentBoot::WriteOptionRow(const GameOptionSettingsSnapshot& expected,GameOptionRowCause cause,uint32_t stage,uint32_t value,GameOptionRowWriteReceipt& receipt){
    if(state_.closed||state_.main_menu_phase!=7||!state_.main_menu_child_ready||!GameOptionSnapshotExact(expected,OptionSettings()))return {GameFsCode::Unavailable,0,"Options selected current owner unavailable"};
    const auto gui=system_option_.Snapshot();auto status=actor_script_.PrepareMenuOptionRowWrite(main_menu_target_,gui,cause,stage,value,receipt);if(!status)return status;
    const auto at=0x10+4*receipt.write.field;for(uint32_t byte=0;byte<4;++byte)reset_bytes_.at(at+byte)=uint8_t(value>>(8*byte));++option_settings_writes_;
    return actor_script_.CommitMenuOptionRowWrite(main_menu_target_,gui,receipt);
}
GameFsStatus GameContentBoot::ReadOptionFeedback(const GameOptionSettingsSnapshot& expected,GameSourceFeedbackRead& out){
    if(state_.closed||state_.main_menu_phase!=7||!GameOptionSnapshotExact(expected,OptionSettings()))return {GameFsCode::Unavailable,0,"Options selected feedback owner unavailable"};
    const auto gui=system_option_.Snapshot();const auto checked=actor_script_.CheckMenuOption(main_menu_target_,gui);if(!checked)return checked;
    if(gui.source_consumer!=0x4AC84||gui.row_writes!=4||gui.row_left_steps!=1||gui.row_feedback_attempts!=1)return {GameFsCode::Unavailable,0,"Options selected feedback source cursor unavailable"};
    if(!gui.sound_owner.registration.live)return sound_bank_.ReadOptionsFeedback(gui.sound_owner,out);
    auto status=actor_script_.PrepareMenuOptionFeedback(main_menu_target_,gui);if(!status)return status;
    const auto effect=sound_bank_.ReadOptionsVoiceFeedback(gui.sound_owner,OptionSettings().words[1],out);
    if(!GameSourceFeedbackReachedValid(out)&&!GameSourceVoiceFeedbackValid(out))return effect;
    if(!system_option_.PublishFeedback(out))return {GameFsCode::Unavailable,0,"Options actual feedback frontier publication refused"};
    status=actor_script_.CommitMenuOptionFeedback(main_menu_target_,out);if(!status)return status;
    return effect;
}
GameFsStatus GameContentBoot::ServiceOptionVoice(const GameOptionSettingsSnapshot& expected,GameSourceFeedbackRead& out){
 if(state_.closed||state_.main_menu_phase!=7||!GameOptionSnapshotExact(expected,OptionSettings()))return {GameFsCode::Unavailable,0,"Options voice service settings owner"};
 GameOptionsChildCallScope scope;const auto gui=system_option_.Snapshot();
 auto status=actor_script_.PrepareMenuOptionSfxService(main_menu_target_,gui,scope);if(!status)return status;
 const auto effect=sound_bank_.ServiceOptionsSfxScoped(gui.sound_owner,out,scope);if(!GameSourceSfxPrefixValid(out))return effect;
 if(!system_option_.PublishChildVoiceService(out,scope))return {GameFsCode::Unavailable,0,"Options actual scoped child publication"};
 status=actor_script_.CommitMenuOptionVoiceService(main_menu_target_,out);if(!status)return status;
 if(!system_option_.CommitChildVoiceScope(scope))return {GameFsCode::Unavailable,0,"Options actual child commit scope"};return effect;
}
GameFsStatus GameContentBoot::QueryOptionGain(const GameOptionSettingsSnapshot& expected,int32_t value,GameOptionGainObservation& out){
    if(!GameOptionSnapshotExact(expected,OptionSettings())||value!=105||state_.main_menu_phase!=7)
        return {GameFsCode::Unavailable,0,"Options exact selected gain query owner unavailable"};
    const auto metadata=sound_bank_.Snapshot();out.metadata_ready=metadata.metadata_ready;out.native_metadata_slot=metadata.native_metadata_slot;
    const auto gui=system_option_.Snapshot();auto status=actor_script_.PrepareMenuOptionGain(main_menu_target_,gui);if(!status)return status;
    const auto owner=sound_bank_.SourceControl();status=sound_bank_.QuerySourceGain(owner,value,out.source);if(!status)return status;
    out.source_id_known=true;out.source_dispatched=out.source.dispatched;
    return actor_script_.CommitMenuOptionGain(main_menu_target_,gui,out.source);
}
GameFsStatus GameContentBoot::WriteOptionBackup(const GameOptionSettingsSnapshot& expected,uint32_t stage,uint32_t value,GameOptionWriteReceipt& receipt){
    if(state_.closed||state_.main_menu_phase!=7||!state_.main_menu_child_ready||!GameOptionSnapshotExact(expected,OptionSettings()))
        return {GameFsCode::Unavailable,0,"Options backup current source owner unavailable"};
    const auto gui=system_option_.Snapshot();auto status=actor_script_.PrepareMenuOptionBackup(main_menu_target_,gui,stage,value,receipt);if(!status)return status;
    const auto at=0x10+receipt.field*4;
    for(uint32_t byte=0;byte<4;++byte)reset_bytes_.at(at+byte)=uint8_t(value>>(8*byte));++option_settings_writes_;
    return actor_script_.CommitMenuOptionWrite(main_menu_target_,gui,receipt);
}
GameFsStatus GameContentBoot::WriteOptionYes(const GameOptionSettingsSnapshot& expected,uint32_t stage,uint32_t value,GameOptionWriteReceipt& receipt){
    // Human-input ready is false during natural TRC contraction. Authority is
    // the private prior publication + actual pending source tick, not ready.
    if(state_.closed||state_.main_menu_phase!=7||!GameOptionSnapshotExact(expected,OptionSettings()))
        return {GameFsCode::Unavailable,0,"Options Yes current source owner unavailable"};
    const auto gui=system_option_.Snapshot();auto status=actor_script_.PrepareMenuOptionYes(main_menu_target_,gui,stage,value,receipt);if(!status)return status;
    const auto at=0x10+receipt.field*4;
    for(uint32_t byte=0;byte<4;++byte)reset_bytes_.at(at+byte)=uint8_t(value>>(8*byte));++option_settings_writes_;
    return actor_script_.CommitMenuOptionWrite(main_menu_target_,gui,receipt);
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
            if(mount=="ENIKKI"&&game_filesystem_){
                if(!diary_mounted_){const auto status=game_filesystem_->LoadMember("ENIKKI.BPM",diary_package_);if(!status)return status;
                    diary_directory_=BpDirectory::Parse(diary_package_,diary_package_.Describe().size);diary_mounted_=true;}
                out=diary_directory_.Open(path.substr(split+1));++state_.opened_resources;return {};
            }
            if(mount=="OPTION"&&game_filesystem_){
                const auto request=system_option_.Snapshot();
                if(!request.requested||!request.constructor_entered||request.failed)
                    return {GameFsCode::Unavailable,0,"OPTION package requires current resource10 constructor"};
                if(!system_option_mounted_){
                    const auto status=game_filesystem_->LoadMember("OPTION.BPM",system_option_package_);if(!status)return status;
                    system_option_directory_=BpDirectory::Parse(system_option_package_,system_option_package_.Describe().size);system_option_mounted_=true;
                }
                out=system_option_directory_.Open(path.substr(split+1));++state_.opened_resources;return {};
            }
            if(mount=="MTOMO"&&game_filesystem_){
                // Actual resource9 ctor prefix selects this package; retain its
                // parent backing independently of child clone/template teardown.
                if(!notebook_mounted_){
                    const auto status=game_filesystem_->LoadMember("MTOMO.BPM",notebook_package_);if(!status)return status;
                    notebook_directory_=BpDirectory::Parse(notebook_package_,notebook_package_.Describe().size);notebook_mounted_=true;
                }
                out=notebook_directory_.Open(path.substr(split+1));++state_.opened_resources;return {};
            }
            if(mount=="MKOTOBA"&&game_filesystem_){
                if(!wordmemo_mounted_){
                    const auto status=game_filesystem_->LoadMember("MKOTOBA.BPM",wordmemo_package_);if(!status)return status;
                    wordmemo_directory_=BpDirectory::Parse(wordmemo_package_,wordmemo_package_.Describe().size);wordmemo_mounted_=true;
                }
                out=wordmemo_directory_.Open(path.substr(split+1));++state_.opened_resources;return {};
            }
            if(mount=="MOCHI"&&game_filesystem_){
                if(!inventory_mounted_){
                    const auto status=game_filesystem_->LoadMember("MOCHI.BPM",inventory_package_);if(!status)return status;
                    inventory_directory_=BpDirectory::Parse(inventory_package_,inventory_package_.Describe().size);inventory_mounted_=true;
                }
                out=inventory_directory_.Open(path.substr(split+1));++state_.opened_resources;return {};
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
GameDiaryIoSnapshot GameContentBoot::DiaryIo()const{
    const auto fs=game_filesystem_?game_filesystem_->Snapshot():GameFilesystemSnapshot{};
    const bool live=loadman_&&state_.loadman_owner&&!state_.closed&&fs.mounted&&!fs.closed;
    const auto pending=diary_io_pending_+uint32_t(state_.mount_state!=0);
    return {live,live&&!pending&&diary_io_requests_==diary_io_completed_,diary_io_owner_,1,diary_io_requests_,diary_io_completed_,pending};
}
GameFsStatus GameContentBoot::RequestDiaryImage(const std::string& stem,std::shared_ptr<GameImage>& image){
    if(!DiaryIo().live||stem!="d11_N")return {GameFsCode::Unavailable,0,"Unsupported actual diary image package owner/stem"};
    ++diary_io_requests_;++diary_io_pending_;
    GameFsStatus status;
    try{
        // Native synchronous2974/2E7C adapter: actual source-selected BPM and
        // checked GIM backing. Its real completed read precedes2CA4 readiness.
        if(diary_picture_stem_!=stem){
            PackageView package;status=game_filesystem_->LoadMember("D11_N.BPM",package);
            if(status){auto directory=BpDirectory::Parse(package,package.Describe().size);diary_picture_package_=std::move(package);diary_picture_directory_=std::move(directory);diary_picture_stem_=stem;}
        }
        if(status){PackageView raw=diary_picture_directory_.Open(stem+".GIM");auto decoded=std::make_shared<GameImage>();status=decoded->DecodeGim(std::move(raw));if(status)image=std::move(decoded);}
    }catch(const std::exception& e){status={GameFsCode::MalformedDirectory,0,e.what()};}
    --diary_io_pending_;if(status)++diary_io_completed_;return status;
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
        // Mailbox overflow is an out-of-band focus/release loss. Its synthetic
        // frame has no sequence or epoch, so disarm before either filter.
        if(frame.overflow_resync||!frame.focused){
            main_menu_entry_gate_.Stop();main_menu_entry_ready_=false;main_flow_.ResetInput();
            returned_interaction_.SuspendInput();state_.returned_interaction=returned_interaction_.Snapshot();
            auto loss=frame;loss.overflow_resync=true;
            post_modal_.Input(loss);state_.post_modal=post_modal_.Snapshot();
            if(state_.main_menu_phase){
                main_menu_.Input(loss);if(inventory_.Modal())inventory_.Input(loss);if(notebook_.Modal())notebook_.Input(loss);if(wordmemo_.Modal())wordmemo_.Input(loss);if(diary_.Modal())diary_.Input(loss);if(system_option_.Modal()){system_option_.Input(loss);actor_script_.DisarmMenuOptionInput();}
            }
            else if(inventory_.Modal())inventory_.Input(loss);
            else if(state_.actor_script.awaiting_choice)scene_ui_.HandleChoiceInput(loss);
            if(state_.actor_script.awaiting_message)actor_message_gate_.Begin(state_.actor_message_contexts,state_.actor_script.generation);
            else actor_message_gate_.Stop();
            state_.actor_message_input_armed=false;return false;
        }
        if(frame.main_menu_epoch!=state_.main_menu_input_epoch)return false;
        if(state_.main_menu_phase){
            const auto& lease=main_menu_lease_;
            if(frame.message_owner_generation!=lease.script_generation||frame.message_context!=lease.message_context
               ||frame.returned_owner!=lease.available_owner||frame.returned_generation!=lease.available_generation
               ||frame.sequence<=main_menu_last_sequence_)return false;
            main_menu_last_sequence_=frame.sequence;
            if(state_.main_menu_phase==7&&system_option_.Modal()){
                if(!state_.main_menu_child_ready||system_option_.Snapshot().phase!=3)return false;
                auto checked=CheckMainMenuLease();if(checked)checked=actor_script_.CheckMenuOption(main_menu_target_,system_option_.Snapshot());
                if(!checked){state_.status=checked;state_.blocked=true;return false;}
                const bool accepted=system_option_.Input(frame);
                if(accepted){checked=actor_script_.ObserveMenuOptionInput(main_menu_target_,system_option_.Snapshot(),frame);
                    if(!checked){state_.status=checked;state_.blocked=true;return false;}}
                return accepted;
            }
            if(state_.main_menu_phase==7&&diary_.Modal()){
                if(!state_.main_menu_child_ready||diary_.Snapshot().phase!=3)return false;
                auto checked=CheckMainMenuLease();if(checked)checked=actor_script_.CheckMenuDiary(main_menu_target_,diary_.Snapshot());
                if(!checked){state_.status=checked;state_.blocked=true;return false;}return diary_.Input(frame);
            }
            if(state_.main_menu_phase==7&&wordmemo_.Modal()){
                if(!state_.main_menu_child_ready||wordmemo_.Snapshot().phase!=3)return false;
                auto status=CheckMainMenuLease();
                if(status){const auto child=wordmemo_.Snapshot();status=actor_script_.CheckMenuWordMemo(main_menu_target_,child.generation,child.lease.parent_generation);}
                if(!status){state_.status=status;state_.blocked=true;return false;}
                return wordmemo_.Input(frame);
            }
            if(state_.main_menu_phase==7&&notebook_.Modal()){
                if(!state_.main_menu_child_ready||notebook_.Snapshot().phase!=3)return false;
                const auto status=CheckMainMenuLease();if(!status){state_.status=status;state_.blocked=true;return false;}
                return notebook_.Input(frame);
            }
            if(state_.main_menu_phase==7&&inventory_.Modal()){
                if(!state_.main_menu_child_ready||inventory_.Snapshot().phase!=3)return false;
                const auto status=CheckMainMenuLease();if(!status){state_.status=status;state_.blocked=true;return false;}
                return inventory_.Input(frame);
            }
            return main_menu_.Input(frame);
        }
        const auto returned=returned_interaction_.Snapshot();
        const bool current_input=ordinary_input_identity_.Accept(frame,{state_.actor_script.generation,state_.actor_message_contexts,
               returned.owner,returned.generation,state_.main_menu_input_epoch});
        state_.ordinary_input_sequence=ordinary_input_identity_.Sequence();state_.ordinary_input_rejected=ordinary_input_identity_.Rejected();
        if(!current_input)return false; // stale packets cannot erase already accepted prefix edges
        const auto menu_route=MainMenuInput(frame);
        if(menu_route!=MenuInputRoute::NoMatch)return menu_route==MenuInputRoute::Consumed;
        if(state_.actor_script.awaiting_input||(scene_ui_.Editor().live&&scene_ui_.Editor().source_state!=3))return false;
        if(!inventory_.Modal())main_flow_.Input(frame,state_.actor_script.awaiting_message||state_.actor_script.awaiting_choice||state_.actor_script.awaiting_input);
        const bool posted=post_modal_.Input(frame);state_.post_modal=post_modal_.Snapshot();
        if(posted)return true;
        // Recheck live owners before the queue call;the published input token
        // must already have existed when this raw frame was sampled.
        returned_interaction_.CheckInputOwners(actor_script_.Snapshot(),main_flow_.Snapshot(),ReturnedGate());
        const bool returned_posted=returned_interaction_.Input(frame);
        state_.returned_interaction=returned_interaction_.Snapshot();
        if(returned_posted){state_.actor_script=actor_script_.Snapshot();return true;}
        if(inventory_.Modal())return inventory_.Input(frame);
        if(state_.actor_script.awaiting_choice)return scene_ui_.HandleChoiceInput(frame);
        const bool edge=actor_message_gate_.Observe(frame);
        const bool accepted=state_.actor_script.awaiting_message&&edge&&actor_script_.AcknowledgeMessage(&actor_message_target_);
        if(accepted)state_.actor_script=actor_script_.Snapshot();
        state_.actor_message_stale_inputs=actor_message_gate_.Rejected();
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
bool GameContentBoot::ProcessText(const GameTextEvent& event){
    if(state_.closed||inventory_.Modal()||state_.main_menu_phase)return false;
    if(state_.actor_script.awaiting_input&&GameInput32KeyboardActivity(scene_ui_.Editor(),event))
        state_.actor_input_remaining=9000;
    //WindowsInput32LatinKana229:source14E44/14F90 direct-success subset.
    //Unknown/drop/fallback characters refuse;profile editors keep their domain.
    if(state_.actor_script.awaiting_input&&event.kind==GameTextKind::Commit){
        if(event.bytes>event.text.size())return false;
        if(!GameInput32TextAllowed(std::string_view(event.text.data(),event.bytes)))return false;
    }
    return scene_ui_.HandleText(event);
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
        records_.ResetCold249D0();
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
    main_menu_entry_gate_.Stop();main_menu_.Close();notebook_.Close();wordmemo_.Close();diary_.Close();system_option_.Abort();state_.system_option=system_option_.Snapshot();state_.diary=diary_.Snapshot();state_.wordmemo=wordmemo_.Snapshot();state_.main_menu=main_menu_.Snapshot();state_.notebook=notebook_.Snapshot();
    main_menu_target_={};main_menu_lease_={};state_.main_menu_available=false;state_.main_menu_phase=0;
    returned_interaction_.Close();state_.returned_interaction=returned_interaction_.Snapshot();
    post_modal_.Close();state_.post_modal=post_modal_.Snapshot();
    main_flow_.Close();state_.main_flow=main_flow_.Snapshot();
    inventory_.Close();state_.inventory=inventory_.Snapshot();
    actor_script_.Close();actor_message_gate_.Stop();state_.actor_message_input_armed=false;state_.actor_script=actor_script_.Snapshot();actor_clock_={};
    records_.Close();state_.actor_script.records=records_.Snapshot();
    actor_runtime_.Close();state_.actor_script.runtime=actor_runtime_.Snapshot();
    actor_model_directory_.Reset();actor_model_package_.Reset();actor_model_mounted_=false;
    inventory_directory_.Reset();inventory_package_.Reset();inventory_mounted_=false;
    notebook_directory_.Reset();notebook_package_.Reset();notebook_mounted_=false;
    wordmemo_directory_.Reset();wordmemo_package_.Reset();wordmemo_mounted_=false;
    system_option_directory_.Reset();system_option_package_.Reset();system_option_mounted_=false;
    diary_directory_.Reset();diary_package_.Reset();diary_mounted_=false;
    diary_picture_directory_.Reset();diary_picture_package_.Reset();diary_picture_stem_.clear();
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
GameReturnedInteractionGate GameContentBoot::ReturnedGate()const{
    GameReturnedInteractionGate gate;
    const auto ui=scene_ui_.Snapshot();const auto choice=scene_ui_.Choice();
    gate.ui_idle=!state_.main_menu_phase&&ui.live&&!ui.storage_dialog&&!ui.awaiting_confirmation&&ui.dialog_state==3
        &&(!ui.editor.live||ui.editor.source_state==3)&&choice.state==3&&!state_.closed&&!state_.blocked;
    const auto inventory=inventory_.Snapshot();
    gate.inventory_retired=inventory.phase==7&&!inventory.live&&!inventory.owner480&&!inventory.request14
        &&!inventory.retirement_pending&&inventory.stateB2C==3&&inventory.loader468==1&&inventory.loader498==0
        &&inventory.completed_polls>0&&inventory.retirements>0;
    gate.old_post_modal_consumed=post_modal_.Snapshot().event_posted;
    gate.source469=state_.source_loader469;gate.manager9=actor_runtime_.Snapshot().manager_request9;
    gate.message_context=state_.actor_message_contexts;return gate;
}
void GameContentBoot::PublishInventory469(){
    const auto i=inventory_.Snapshot();const auto serial=uint64_t(i.pending_polls)+i.completed_polls;
    if(i.generation&&i.loader469==1&&serial&&(i.generation!=inventory469_generation_||serial>inventory469_poll_)){
        inventory469_generation_=i.generation;inventory469_poll_=serial;
        state_.source_loader469=true;state_.source469_resource=14;state_.source469_generation=i.generation;state_.source469_poll=serial;
    }
}
bool GameContentBoot::MainMenuEligible()const{
    const auto available=returned_interaction_.Snapshot();const auto flow=main_flow_.Snapshot();
    const auto actor=actor_script_.Snapshot();const auto ui=scene_ui_.Snapshot();
    const auto transition=scene_ui_.TitleTransitionOwner().Snapshot();
    // NativeSourceZeroMenuBorrow: after an actual resource9 poll, the same
    // already admitted available activation may still lend menu ownership.
    // Returned Circle's source469 gate and token remain unchanged.
    const bool source_zero=state_.source469_resource==9&&!state_.source_loader469&&state_.main_menu_notebook_completions
        &&available.admitted&&available.script_generation==actor.generation&&available.run_id==actor.vm_current.run_id
        &&actor.owned_conversation.dispatch_ready;
    return !state_.closed&&!state_.blocked&&!state_.main_menu_phase&&state_.current_scene==4&&state_.load_state==6
        &&available.live&&(available.eligible||source_zero)&&!available.revoked&&!available.event_posted
        &&available.owner&&available.generation&&actor.manager_flag68&&actor.runtime.camera_mode!=0
        &&actor.live&&!actor.blocked&&!actor.completed&&actor.context_enabled&&!actor.awaiting_message&&!actor.awaiting_choice&&!actor.awaiting_input
        &&!actor.inventory_requested&&!actor.source_main_input_suppressed48&&actor.callback_depth==0&&actor.queued_callbacks==0
        &&actor.callbacks_posted==actor.callbacks_returned&&actor.vm_current.valid&&!actor.vm_current.event_envelope_present
        &&flow.live&&flow.phase1C==0&&flow.substate24==0&&flow.return20==0
        &&GameMainMenuCanBorrowTransition(transition)&&ui.live&&!ui.storage_dialog&&!ui.awaiting_confirmation;
}
GameContentBoot::MenuInputRoute GameContentBoot::MainMenuInput(const GameInputFrame& frame){
    // WindowsAvailableMainMenu borrows an existing source-qualified available
    // lease. It does not clear469, consume/rearm that token, or add a PC route.
    returned_interaction_.CheckInputOwners(actor_script_.Snapshot(),main_flow_.Snapshot(),ReturnedGate());
    const auto available=returned_interaction_.Snapshot();state_.returned_interaction=available;
    state_.main_menu_available=MainMenuEligible();state_.main_menu_source_right_blocked=state_.source_loader469;
    if(!state_.main_menu_available){main_menu_entry_gate_.Stop();main_menu_entry_ready_=false;return MenuInputRoute::NoMatch;}
    if(frame.returned_owner!=available.owner||frame.returned_generation!=available.generation
       ||frame.message_context!=state_.actor_message_contexts||frame.message_owner_generation!=state_.actor_script.generation){
        main_menu_entry_gate_.Stop();main_menu_entry_ready_=false;return MenuInputRoute::Consumed;
    }
    if(!main_menu_entry_ready_||main_menu_entry_owner_!=available.owner||main_menu_entry_generation_!=available.generation){
        main_menu_entry_gate_.Begin();main_menu_entry_ready_=true;main_menu_entry_owner_=available.owner;main_menu_entry_generation_=available.generation;
    }
    if(frame.overflow_resync){main_menu_entry_gate_.Begin();return MenuInputRoute::NoMatch;}
    if(!main_menu_entry_gate_.Observe(frame,Right))return MenuInputRoute::NoMatch;
    try{
        const auto actor=actor_script_.Snapshot();main_menu_target_=actor_script_.OwnerEventTarget();
        main_menu_lease_={actor.generation,actor.vm_current.run_id,available.owner,available.generation,state_.actor_message_contexts,state_.main_menu_input_epoch+1};
        const auto status=main_flow_.BeginAvailableMenu();if(!status){state_.status=status;state_.blocked=true;return MenuInputRoute::Error;}
        state_.main_menu_input_epoch=main_menu_lease_.input_epoch;state_.main_menu_phase=1;state_.main_menu_available=false;
        main_menu_last_sequence_=frame.sequence;state_.main_menu_child_ready=false;
        main_menu_entry_gate_.Stop();main_menu_entry_ready_=false;returned_interaction_.SuspendInput();
        actor_message_gate_.Stop();main_flow_.ResetInput();state_.main_flow=main_flow_.Snapshot();state_.returned_interaction=returned_interaction_.Snapshot();
        return MenuInputRoute::Consumed;
    }catch(const std::exception& e){state_.status={GameFsCode::Unavailable,0,e.what()};state_.blocked=true;return MenuInputRoute::Error;}
}
GameFsStatus GameContentBoot::CheckMainMenuLease()const{
    try{
        const auto actor=actor_script_.Snapshot();const auto available=returned_interaction_.Snapshot();const auto target=actor_script_.OwnerEventTarget();
        if(!actor.live||actor.blocked||actor.generation!=main_menu_lease_.script_generation||actor.vm_current.run_id!=main_menu_lease_.run_id
           ||!actor_runtime_.OwnersLive()||actor.runtime.camera_mode==0||state_.current_scene!=4
           ||!available.live||available.revoked||available.owner!=main_menu_lease_.available_owner||available.generation!=main_menu_lease_.available_generation
           ||available.event_posted||target.generation!=main_menu_target_.generation||target.callback.kind!=main_menu_target_.callback.kind
           ||target.callback.word!=main_menu_target_.callback.word||target.context.word!=main_menu_target_.context.word
           ||main_menu_target_.context.context_owner.expired()||target.context.context_owner.lock()!=main_menu_target_.context.context_owner.lock())
            return {GameFsCode::Unavailable,0,"Main menu borrowed owner replaced or retired"};
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameContentBoot::TickMainMenu(GameGraphics& graphics){
    if(state_.main_menu_phase&&state_.system_option.failed){
        // A stopped unsupported consumer gets no subsequent owner VM/service
        // slice. Normal Close still aborts its partial native construction.
        state_.status=state_.system_option.failure;state_.blocked=true;return false;
    }
    sound_bank_.ObserveOptionsWorkerTick(); //actual consecutive worker samples, no clock override
    auto status=CheckMainMenuLease();
    if(status)status=actor_script_.TickMenuOwner();
    const auto finish=[&]{
        ++state_.main_menu_returns;++state_.main_menu_input_epoch;state_.main_menu_phase=0;
        state_.main_menu_child_ready=false;
        main_menu_target_={};main_menu_lease_={};main_flow_.ResetInput();returned_interaction_.SuspendInput();
        main_menu_entry_gate_.Stop();main_menu_entry_ready_=false;
    };
    if(status){
        switch(state_.main_menu_phase){
        case 1:status=main_flow_.AdvanceMenuOpening();if(status)state_.main_menu_phase=2;break;
        case 2:{
            const auto runtime=actor_runtime_.Snapshot();status=main_menu_.Open(*this,main_menu_lease_,runtime.camera_mode!=0,runtime.manager_scene14!=0,true,records_.OtherProfilesPresent());
            if(status)status=main_flow_.MenuOpened();
            int32_t queue=-1;if(status)status=actor_script_.PostMenuEvent(main_menu_target_,10,main_menu_.Snapshot().generation,queue);
            if(status&&queue!=0)status={GameFsCode::Unavailable,0,"Main menu event10 queue refused"};
            if(status)state_.main_menu_phase=3;break;
        }
        case 3:
            status=main_menu_.Tick();
            if(status&&main_menu_.Snapshot().state78==3){
                const auto result=main_menu_.Snapshot().result;status=main_flow_.PollMenuResult(result);
                if(status&&result==13){
                    // Source23844 direct24FF4(10,1). The source initializer
                    // precedes publication474/Open; it may genuinely refuse.
                    status=system_option_.Request(main_menu_lease_,{{StateWord(0x10),StateWord(0x14),StateWord(0x18)}});
                    if(status){const auto actor=actor_script_.Snapshot();system_option_.SetChildLease({actor.generation,actor.vm_current.run_id,state_.actor_message_contexts,
                        main_menu_.Snapshot().generation,main_menu_lease_.available_owner,main_menu_lease_.available_generation,state_.main_menu_input_epoch+1});
                        status=actor_script_.BindMenuOption(main_menu_target_,system_option_.Snapshot().generation,main_menu_.Snapshot().generation,*this,system_option_.Snapshot().child_lease,system_option_);if(!status)system_option_.Abort();}
                    state_.system_option=system_option_.Snapshot();
                    if(status){++state_.main_menu_input_epoch;state_.main_menu_child_ready=false;state_.main_menu_phase=8;break;}
                }
                if(status&&result==6){
                    GameRecordImage image;GameDiaryOpenInputs inputs;
                    if(!loadman_||!loadman_->confirmed58)status={GameFsCode::Unavailable,0,"Diary raw LoadMan58 has no confirmed TITLE producer"};
                    if(status)status=actor_script_.ReadMenuDiary(main_menu_target_,loadman_->actor58,StateWord(8),image,inputs);
                    if(image.live)state_.diary_observed=std::make_shared<const GameRecordImage>(image);const auto actor=actor_script_.Snapshot();
                    const GameNotebookLease lease{actor.generation,actor.vm_current.run_id,state_.actor_message_contexts,
                        main_menu_.Snapshot().generation,main_menu_lease_.available_owner,main_menu_lease_.available_generation,state_.main_menu_input_epoch+1};
                    if(status)status=actor_script_.PrepareMenuDiary(main_menu_target_,diary_.Snapshot().generation+1,main_menu_.Snapshot().generation,image,inputs);
                    if(status)status=diary_.Request(image,inputs,lease);
                    if(status){status=actor_script_.BindMenuDiary(main_menu_target_,diary_.Snapshot().generation,main_menu_.Snapshot().generation,image,inputs,*this);if(!status)diary_.Close();}
                    if(status){++state_.main_menu_diary_requests;++state_.main_menu_input_epoch;state_.main_menu_child_ready=false;}
                }
                if(status&&result==4){
                    GameWordMemoRows rows;status=actor_script_.ReadMenuWordMemo(main_menu_target_,rows);state_.wordmemo_observed=rows;
                    const auto actor=actor_script_.Snapshot();
                    const GameWordMemoLease lease{actor.generation,actor.vm_current.run_id,state_.actor_message_contexts,
                        main_menu_.Snapshot().generation,main_menu_lease_.available_owner,main_menu_lease_.available_generation,state_.main_menu_input_epoch+1};
                    if(status)status=actor_script_.PrepareMenuWordMemo(main_menu_target_,wordmemo_.Snapshot().generation+1,main_menu_.Snapshot().generation,rows);
                    if(status)status=wordmemo_.Request(rows,lease);
                    if(status){
                        status=actor_script_.BindMenuWordMemo(main_menu_target_,wordmemo_.Snapshot().generation,main_menu_.Snapshot().generation,rows);
                        if(!status)wordmemo_.Close(); // failed admission teardown, never a successful poll/drain
                    }
                    if(status){++state_.main_menu_wordmemo_requests;++state_.main_menu_input_epoch;state_.main_menu_child_ready=false;}
                }
                if(status&&result==3){
                    GameNotebookRows rows;status=actor_script_.ReadMenuNotebook(main_menu_target_,rows);
                    state_.notebook_observed=rows; // retain actual reached data even on Unsupported
                    const auto actor=actor_script_.Snapshot();
                    const GameNotebookLease lease{actor.generation,actor.vm_current.run_id,state_.actor_message_contexts,
                        main_menu_.Snapshot().generation,main_menu_lease_.available_owner,main_menu_lease_.available_generation,state_.main_menu_input_epoch+1};
                    // Prior498 belongs to the real last resource14 owner. Action3
                    // does not write498/4A0 or manufacture a GAME41 request.
                    if(status)status=notebook_.Request(rows,lease,inventory_.Snapshot().loader498);
                    if(status)status=actor_script_.BindMenuNotebook(main_menu_target_,notebook_.Snapshot().generation,main_menu_.Snapshot().generation,rows);
                    if(status){++state_.main_menu_notebook_requests;++state_.main_menu_input_epoch;state_.main_menu_child_ready=false;}
                }
                if(status&&result==5){
                    // Source237F0: direct resource14 request; no GAME41 call.
                    // Only the spent initial observer retires, retaining origin history.
                    status=post_modal_.RetireConsumedInventory();state_.post_modal=post_modal_.Snapshot();
                    std::vector<GameInventoryItem> items;
                    if(status)status=actor_script_.ReadMenuInventory(main_menu_target_,items);
                    if(status)status=inventory_.Request(std::move(items),actor_runtime_.Snapshot().manager_scene14==0,[this](uint64_t generation,uint32_t event,int32_t row){
                        return actor_script_.PostMenuInventoryAction(main_menu_target_,generation,event,row);
                    },GameInventoryOrigin::MainMenu5,actor_script_.MenuPlacementAvailable(),[this](const GameInventoryActionCertificate& c){
                        return actor_script_.ValidateMenuInventoryCertificate(main_menu_target_,c);
                    });
                    if(status)status=actor_script_.BindMenuInventory(main_menu_target_,inventory_.Snapshot().generation,main_menu_.Snapshot().generation);
                    if(status){
                        ++state_.main_menu_inventory_requests;++state_.main_menu_input_epoch;
                        const auto request=inventory_.Snapshot();state_.main_menu_inventory_request_phase=request.phase;
                        state_.main_menu_inventory_request498=request.loader498;state_.main_menu_inventory_request_clock=actor_script_.Snapshot().vm_current.clock_tick;
                        state_.main_menu_inventory_child_clock=state_.main_menu_inventory_retire_clock=0;
                    }
                }
                if(status)state_.main_menu_phase=4;
            }
            break;
        case 4:{
            // A separate main-flow invocation after GUIready/result poll.
            status=main_flow_.RetireMenuResult();int32_t queue=-1;
            if(status)status=actor_script_.PostMenuEvent(main_menu_target_,11,main_menu_.Snapshot().generation,queue);
            if(status&&queue!=0)status={GameFsCode::Unavailable,0,"Main menu event11 queue refused"};
            if(status)state_.main_menu_phase=5;break;
        }
        case 5:
            if(actor_script_.Snapshot().menu_close_return.returned
               &&actor_script_.Snapshot().menu_close_return.menu_generation==main_menu_.Snapshot().generation){
                if(main_flow_.Snapshot().menu_result50==5||main_flow_.Snapshot().menu_result50==3||main_flow_.Snapshot().menu_result50==4||main_flow_.Snapshot().menu_result50==6||main_flow_.Snapshot().menu_result50==13){
                    int32_t queue=-1;status=actor_script_.PostMenuEvent(main_menu_target_,18,main_menu_.Snapshot().generation,queue);
                    if(status&&queue!=0)status={GameFsCode::Unavailable,0,"Main menu event18 queue refused"};
                    if(status)state_.main_menu_phase=6;
                }else finish();
            }
            break;
        case 6:{
            const auto actor=actor_script_.Snapshot();const auto& pause=actor.menu_pause_return;
            if(pause.returned&&pause.menu_generation==main_menu_.Snapshot().generation
               &&pause.receipt>actor.menu_close_return.receipt&&!pause.enabled_after&&!actor.context_enabled)state_.main_menu_phase=7;
            break;
        }
        case 8:
            // NativeSingleWorkerResource10BeforeRetire: original allocator vs
            // callback scheduling is unknown. The distinct153D4 owner must
            // return before474/Open and before this worker posts11 then18.
            status=scene_ui_.PrepareOptionsPrompt();
            if(status)status=system_option_.Initialize(*this,font_,*this,scene_ui_.TitleTransitionOwner(),&scene_ui_.OptionsPromptOwner());state_.system_option=system_option_.Snapshot();
            if(status)state_.main_menu_phase=4;break;
        case 7:
            // NativeMenuInventorySingleWorkerAfterCallbacks. Original relative
            // allocator-vs11/18 phase order is unknown; request-before-retire is proved.
            scene_ui_.TitleTransitionOwner().Update();
            // NativeClosedMenuRenderRetirement: the hidden MMENU images are
            // no longer render owners. Preserve its result/lease and actual
            // callbacks; only subsequent cache pressure can evict those images.
            status=main_menu_.RetireClosedRender();
            if(!status)break;
            if(main_flow_.Snapshot().menu_result50==13){
                status=actor_script_.PrepareMenuOptionTick(main_menu_target_,system_option_.Snapshot());
                const auto before_boundary=system_option_.Snapshot().input_boundaries;
                if(status)status=system_option_.Tick(scene_ui_.TitleTransitionOwner());auto child=system_option_.Snapshot();
                if(!status&&child.failed&&child.right_writes)actor_script_.VerifyMenuOptionPrefix(main_menu_target_,child); //diagnostic proof; never success/wake
                if(!status&&child.failed&&child.row_writes){
                    if(child.row_voice_service.outcome==11)actor_script_.VerifyMenuOptionSfxPrefix(main_menu_target_,child);
                    else if(child.row_voice_service.outcome==10)actor_script_.VerifyMenuOptionSlotPrefix(main_menu_target_,child);
                    else if(child.row_voice_service.outcome==9)actor_script_.VerifyMenuOptionChildPrefix(main_menu_target_,child);
                    else if(child.row_voice_service.outcome==8)actor_script_.VerifyMenuOptionPort24Prefix(main_menu_target_,child);
                    else if(child.row_voice_service.outcome==7)actor_script_.VerifyMenuOptionWavePrefix(main_menu_target_,child);
                    else if(child.row_voice_service.outcome==6)actor_script_.VerifyMenuOptionControlPrefix(main_menu_target_,child);
                    else if(child.row_voice_service.outcome==5)actor_script_.VerifyMenuOptionProcessorPrefix(main_menu_target_,child);
                    else if(child.row_voice_service.outcome==4)actor_script_.VerifyMenuOptionPortPrefix(main_menu_target_,child);
                    else if(child.row_voice_service.outcome==3)actor_script_.VerifyMenuOptionSequencePrefix(main_menu_target_,child);
                    else if(child.row_voice_service.outcome==2)actor_script_.VerifyMenuOptionVoiceServicePrefix(main_menu_target_,child);
                    else if(child.row_feedback.issuer==0x56598)actor_script_.VerifyMenuOptionFeedbackPrefix(main_menu_target_,child);
                    else actor_script_.VerifyMenuOptionRowPrefix(main_menu_target_,child);
                }
                if(status)status=actor_script_.CommitMenuOptionTick(main_menu_target_,child);
                if(status&&child.input_boundaries!=before_boundary){status=actor_script_.AdvanceMenuOptionInputEpoch(main_menu_target_,system_option_);child=system_option_.Snapshot();state_.main_menu_input_epoch=child.input_epoch;}
                if(status)status=actor_script_.CheckMenuOption(main_menu_target_,child);
                if(status&&child.polls&&!child.loader469){state_.source_loader469=false;state_.source469_resource=10;state_.source469_generation=child.generation;state_.source469_poll=child.polls;}
                state_.main_menu_child_ready=status&&child.phase==3&&(!child.substate48||child.prompt.state864==1)&&child.row_feedback.outcome!=1;
                if(status&&system_option_.ClosedAndRetired()){status=actor_script_.CompleteMenuOption(main_menu_target_,child);if(status)finish();}
                break;
            }
            if(main_flow_.Snapshot().menu_result50==6){
                const auto before=diary_.Snapshot();status=actor_script_.CheckMenuDiary(main_menu_target_,before);
                if(status)status=diary_.Tick(*this,*this,records_,scene_ui_.TitleTransitionOwner());const auto child=diary_.Snapshot();
                if(status)status=actor_script_.CheckMenuDiary(main_menu_target_,child);
                if(status&&child.polls&&!child.loader469){state_.source_loader469=false;state_.source469_resource=12;state_.source469_generation=child.generation;state_.source469_poll=child.polls;}
                if(child.phase!=3)state_.main_menu_child_ready=false;
                if(status&&!state_.main_menu_child_ready&&child.phase==3){++state_.main_menu_input_epoch;state_.main_menu_child_ready=true;
                    if(!diary_.SetInputEpoch(state_.main_menu_input_epoch))status={GameFsCode::Unavailable,0,"Diary ready input epoch refused"};}
                if(status&&diary_.ClosedAndRetired()){status=actor_script_.CompleteMenuDiary(main_menu_target_,diary_.Snapshot());if(status){++state_.main_menu_diary_completions;finish();}}
                break;
            }
            if(main_flow_.Snapshot().menu_result50==4){
                const auto before=wordmemo_.Snapshot();
                status=actor_script_.CheckMenuWordMemo(main_menu_target_,before.generation,before.lease.parent_generation);
                if(status)status=wordmemo_.Tick(*this,scene_ui_.TitleTransitionOwner());
                const auto child=wordmemo_.Snapshot();
                if(status&&child.polls&&child.loader469==0&&child.generation==actor_script_.Snapshot().menu_wordmemo_generation){
                    state_.source_loader469=false;state_.source469_resource=8;state_.source469_generation=child.generation;state_.source469_poll=child.polls;
                }
                if(status&&!state_.main_menu_child_ready&&child.phase==3){
                    ++state_.main_menu_input_epoch;state_.main_menu_child_ready=true;
                    if(!wordmemo_.SetInputEpoch(state_.main_menu_input_epoch))status={GameFsCode::Unavailable,0,"Wordmemo ready input epoch refused"};
                }
                if(status&&wordmemo_.ClosedAndRetired()){
                    status=actor_script_.CompleteMenuWordMemo(main_menu_target_,child);
                    if(status){++state_.main_menu_wordmemo_completions;finish();}
                }
                break;
            }
            if(main_flow_.Snapshot().menu_result50==3){
                const auto prior=notebook_.Snapshot();
                if(prior.phase==3){
                    GameNotebookSelfImage self;
                    status=actor_script_.ReadMenuNotebookSelf(main_menu_target_,prior.generation,prior.lease.parent_generation,self,
                                                           prior.state26C==2||prior.state26C==3||prior.preview_opens);
                    if(status&&prior.state26C==2){
                        status=notebook_.PrepareSelf(std::move(self));
                        // A finite conversion refusal is reported by the child,
                        // while its ordinary Cancel remains usable. An actor
                        // identity/integrity failure above still blocks.
                        const auto refused=notebook_.Snapshot();
                        if(!status&&refused.state26C==1&&refused.unsupported==prior.unsupported+1)status={};
                    }
                }
                if(status)status=notebook_.Tick(*this,scene_ui_.TitleTransitionOwner(),title_rng_);
                const auto child=notebook_.Snapshot();
                if(status&&child.phase==3&&child.input_boundaries!=prior.input_boundaries){
                    ++state_.main_menu_input_epoch;
                    if(!notebook_.SetInputEpoch(state_.main_menu_input_epoch))status={GameFsCode::Unavailable,0,"NOTE nested input epoch refused"};
                }
                if(status&&child.polls&&child.loader469==0&&child.generation==actor_script_.Snapshot().menu_notebook_generation){
                    state_.source_loader469=false;state_.source469_resource=9;state_.source469_generation=child.generation;state_.source469_poll=child.polls;
                }
                if(status&&!state_.main_menu_child_ready&&child.phase==3){
                    ++state_.main_menu_input_epoch;state_.main_menu_child_ready=true;
                    if(!notebook_.SetInputEpoch(state_.main_menu_input_epoch))status={GameFsCode::Unavailable,0,"NOTE ready input epoch refused"};
                }
                if(status&&notebook_.ClosedAndRetired()){
                    status=actor_script_.CompleteMenuNotebook(main_menu_target_,notebook_.Snapshot());
                    if(status){++state_.main_menu_notebook_completions;finish();}
                }
                break;
            }
            if(inventory_.Snapshot().phase==1)state_.main_menu_inventory_child_clock=actor_script_.Snapshot().vm_current.clock_tick;
            status=inventory_.Tick(*this,scene_ui_.TitleTransitionOwner());
            if(status)PublishInventory469();
            if(status&&inventory_.Snapshot().action_pending&&actor_script_.Snapshot().inventory_action.completed){
                const auto actor=actor_script_.Snapshot();const auto& action=actor.inventory_action;
                status=inventory_.CompleteAction(action.generation,action.receipt,actor.inventory_items,&action.certificate);
                if(status)++state_.main_menu_input_epoch;
            }
            if(status&&!state_.main_menu_child_ready&&inventory_.Snapshot().phase==3){
                ++state_.main_menu_input_epoch;state_.main_menu_child_ready=true;
            }
            if(status&&inventory_.ClosedAndRetired()){
                state_.main_menu_inventory_retire_clock=actor_script_.Snapshot().vm_current.clock_tick;
                status=actor_script_.CompleteMenuInventory(main_menu_target_,inventory_.Snapshot());
                if(status){++state_.main_menu_inventory_completions;finish();}
            }
            break;
        default:status={GameFsCode::Unavailable,0,"Main menu finite native owner phase"};break;
        }
    }
    state_.actor_script=actor_script_.Snapshot();state_.main_flow=main_flow_.Snapshot();state_.main_menu=main_menu_.Snapshot();
    state_.inventory=inventory_.Snapshot();state_.scene_ui=scene_ui_.Snapshot();
    state_.notebook=notebook_.Snapshot();
    state_.wordmemo=wordmemo_.Snapshot();state_.diary=diary_.Snapshot();if(state_.diary.generation)state_.diary_current=std::make_shared<const GameRecordImage>(records_.Image());
    state_.system_option=system_option_.Snapshot();state_.system_option.woken=actor_script_.Snapshot().menu_option_completions!=0;
    if(status)status=actor_runtime_.Draw(graphics,[&](GameGraphics& surface){
        if(system_option_.Modal())return system_option_.Draw(surface,font_);
        if(diary_.Modal())return diary_.Draw(surface,font_);
        if(wordmemo_.Modal())return wordmemo_.Draw(surface,font_);
        if(notebook_.Modal())return notebook_.Draw(surface,font_);
        return inventory_.Modal()?inventory_.Draw(surface,font_):main_menu_.Draw(surface);
    });
    state_.main_menu=main_menu_.Snapshot();
    state_.notebook=notebook_.Snapshot(); // publish actual post-draw empty batch/lease observations
    state_.wordmemo=wordmemo_.Snapshot();state_.diary=diary_.Snapshot();if(state_.diary.generation)state_.diary_current=std::make_shared<const GameRecordImage>(records_.Image());
    state_.system_option=system_option_.Snapshot();state_.system_option.woken=actor_script_.Snapshot().menu_option_completions!=0;
    state_.actor_script.runtime=actor_runtime_.Snapshot();state_.status=status;state_.blocked=!bool(status);
    if(status&&!state_.main_menu_phase){
        returned_interaction_.Update(state_.actor_script,state_.main_flow,ReturnedGate());state_.returned_interaction=returned_interaction_.Snapshot();
        state_.main_menu_available=MainMenuEligible();
    }
    return bool(status);
}
bool GameContentBoot::TickActorDictionary(GameFilesystem& filesystem,GameGraphics& graphics){
    if(state_.actor_dictionary_requested){
        if(!state_.actor_script.started)return false;
        if(state_.main_menu_phase)return TickMainMenu(graphics);
        const auto before=state_.actor_script;
        //Named native single-worker phase0 poll before the next KSC slice.
        //Use the prior published text phase: an acknowledgement cannot also
        //become a new main-flow action in the same message context.
        //Existing modal scheduler remains paused until actual close/poll/
        //deferred retirement;this is not a recovered post32 wake producer.
        if(!inventory_.Modal()&&!before.inventory_requested){
            GameMainFlowGate gate;gate.extension_busy=before.awaiting_message||before.awaiting_choice||before.awaiting_input;
            gate.choice_busy48=before.source_main_input_suppressed48;
            gate.manager_flag68=before.manager_flag68;
            gate.loader469=state_.source_loader469;gate.loader_state20=state_.load_state;
            gate.fade_state=scene_ui_.TitleTransitionOwner().Snapshot().state;
            const auto flow=main_flow_.Tick(gate);state_.main_flow=main_flow_.Snapshot();
            if(!flow){state_.status=flow;state_.blocked=true;state_.frontier_pc=state_.main_flow.last_pc;return false;}
        }
        sound_bank_.SetProfileGain(int32_t(StateWord(0x14))); // current249D0/24B34 owner, not a per-query default
        state_.status=actor_script_.Tick();state_.actor_script=actor_script_.Snapshot();
        if(state_.status&&state_.actor_script.awaiting_choice){
            if(actor_choice_target_.generation!=state_.actor_script.generation||actor_choice_target_.request!=state_.actor_script.choice_requests){
                actor_choice_target_=actor_script_.ChoiceTarget();++state_.actor_message_contexts;
                actor_message_gate_.Stop();state_.actor_message_input_armed=false;
                state_.status=scene_ui_.BeginChoice(state_.actor_script.choice_options,state_.actor_message_contexts,state_.actor_script.generation);
            }
            if(state_.status)state_.status=scene_ui_.TickChoice();
            state_.actor_choice=scene_ui_.Choice();
            if(state_.status&&state_.actor_choice.state==3){
                state_.status=actor_script_.CompleteChoice(actor_choice_target_,state_.actor_choice.result);
                state_.actor_script=actor_script_.Snapshot();
            }
        }
        if(state_.status&&state_.actor_script.awaiting_input){
            if(actor_input_target_.generation!=state_.actor_script.generation||actor_input_target_.request!=state_.actor_script.input_requests){
                actor_input_target_=actor_script_.InputTarget();
                actor_message_gate_.Stop();state_.actor_message_input_armed=false;
                state_.actor_input_remaining=9000;state_.actor_input_active_updates=0;state_.actor_input_timed_out=false;
                state_.status=scene_ui_.BeginEditor(state_.actor_script.input_initial,8,"",0,0,0,true,true);
            }
            const auto editor=scene_ui_.Editor();
            if(state_.status&&editor.source_state==1){
                ++state_.actor_input_active_updates;
                if(state_.actor_input_remaining&&!--state_.actor_input_remaining){
                    state_.actor_input_timed_out=true;
                    if(!scene_ui_.ExpireEditor())state_.status={GameFsCode::Unavailable,0,"Input32 timeout close refused"};
                }
            }
            if(state_.status&&!scene_ui_.TickEditor())state_.status={GameFsCode::Unavailable,0,"Input32 actual editor clip update failed"};
            const auto finished=scene_ui_.Editor();
            if(state_.status&&finished.live&&finished.source_state==3&&finished.result_ready){
                const auto* borrowed=scene_ui_.EditorResult();
                const std::string text=borrowed?*borrowed:std::string();std::string aux;
                if(!GameInput32Normalize(text,aux))state_.status={GameFsCode::Unavailable,0,"Input32 auxiliary normalization outside finite Latin/kana domain"};
                else state_.status=actor_script_.CompleteInput(actor_input_target_,text,aux,state_.actor_input_timed_out);
                state_.actor_script=actor_script_.Snapshot();
            }
        }
        state_.scene_ui=scene_ui_.Snapshot(); //publishes editor context to Windows text sampler
        // Existing phase6 transition and group2 authored room consumers stay
        // live even when a later unimplemented script service stops progress.
        scene_ui_.TitleTransitionOwner().Update();
        if(state_.status&&state_.actor_script.inventory_requests>state_.consumed_script_inventory_requests){
            state_.status=inventory_.Request(state_.actor_script.inventory_items,true,[this](uint64_t generation,uint32_t event,int32_t row){return actor_script_.PostInventoryAction(generation,event,row);});
            if(state_.status)state_.status=actor_script_.BindScriptInventory(inventory_.Snapshot().generation,state_.actor_script.inventory_requests);
            if(state_.status)state_.consumed_script_inventory_requests=state_.actor_script.inventory_requests;
            actor_message_gate_.Stop();state_.actor_message_input_armed=false;
        }
        if(state_.status&&inventory_.Modal())state_.status=inventory_.Tick(*this,scene_ui_.TitleTransitionOwner());
        state_.actor_script=actor_script_.Snapshot();
        if(state_.status&&inventory_.Snapshot().action_pending&&state_.actor_script.inventory_action.completed){
            const auto& action=state_.actor_script.inventory_action;
            state_.status=inventory_.CompleteAction(action.generation,action.receipt,state_.actor_script.inventory_items);
        }
        if(state_.status&&inventory_.ClosedAndRetired()&&actor_script_.CanResumeInventory()){
            //NamedNativeModalSuspendResumeTriggeredOnlyByActualClose: original
            //post32 enable38 producer remains unrecovered. No frame/ack wake.
            state_.status=actor_script_.CompleteInventory(inventory_.Snapshot());
            actor_message_gate_.Stop();state_.actor_script=actor_script_.Snapshot();
        }
        state_.inventory=inventory_.Snapshot();
        //1A9B4's stable469 is1 in BOTH pending and ready branches. Neither
        //468 completion nor deferred retirement proves a persistent clear.
        PublishInventory469();
        if(state_.status){state_.status=post_modal_.Update(state_.inventory,state_.main_flow,actor_script_,state_.source_loader469);state_.post_modal=post_modal_.Snapshot();}
        state_.sound_bank=sound_bank_.Snapshot();
        //Publish ONCE,after both room and text. CopyGameFrame must never see
        //an intermediate room-only front whileFONT is still being decoded.
        const auto rendered=actor_runtime_.Draw(graphics,[&](GameGraphics& surface)->GameFsStatus{
            if(inventory_.Modal())return inventory_.Draw(surface,font_);
            if(state_.actor_script.awaiting_input){
                if(!scene_ui_.DrawEditor(surface))return {GameFsCode::Unavailable,0,"Input32 editor draw failed"};
                return {};
            }
            if(!state_.actor_script.awaiting_message&&!state_.actor_script.awaiting_choice)return {};
            GameFontBitmap text;const auto drawn=font_.RasterizeDialogueCp932(state_.actor_script.message,440,text);
            if(!drawn)return drawn;
            //Named native KSC panel layout,not originalpopup pixelmatching.
            if(!surface.FillRectangle(12,12,468,24+int32_t(text.height),0xFFF5E8CD,65535)
               ||!surface.DrawImage(text.pixels,text.width,text.height,20,16,0xFFFFFFFF,65535))
                return {GameFsCode::Unavailable,0,"Native KSC message draw failed"};
            state_.actor_message_glyphs=text.glyphs;state_.actor_message_ink_pixels=0;
            for(auto p:text.pixels)if(p>>24)++state_.actor_message_ink_pixels;
            ++state_.actor_message_draw_frames;
            if(state_.actor_script.awaiting_choice&&!scene_ui_.DrawChoice(surface))return {GameFsCode::Unavailable,0,"Native SELECT text draw failed"};
            return {};
        });
        if(!rendered){state_.status=rendered;state_.blocked=true;return false;}
        state_.inventory=inventory_.Snapshot();
        state_.actor_script.runtime=actor_runtime_.Snapshot();
        if(state_.actor_script.dictionary.imported){actor_dictionary_.Reset();state_.actor_dictionary_live=false;}
        if(state_.actor_script.awaiting_message&&(!before.awaiting_message||before.messages!=state_.actor_script.messages)){
            ++state_.actor_message_contexts;actor_message_target_=actor_script_.MessageTarget();
            actor_message_gate_.Begin(state_.actor_message_contexts,state_.actor_script.generation);state_.actor_message_input_armed=false;
        }
        if(state_.status){
            returned_interaction_.Update(state_.actor_script,state_.main_flow,ReturnedGate());
            state_.returned_interaction=returned_interaction_.Snapshot();
        }
        state_.main_menu_available=MainMenuEligible();state_.main_menu_source_right_blocked=state_.source_loader469;
        if(!state_.main_menu_available){main_menu_entry_gate_.Stop();main_menu_entry_ready_=false;}
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
    // Native confirmed TITLE24B94 -> LoadMan24B88 writer (19410/194D4).
    // Separate from ASC actor.source_field58.
    if(loadman_&&state_.title.profile.stored.live&&state_.title.profile.stored.actor==actor){loadman_->actor58=int32_t(actor);loadman_->confirmed58=true;}
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
        state_.status=actor_script_.Initialize(actor_dictionary_,script_image,state_.next_resource,state_.title.profile.stored,actor_clock_,title_rng_,&actor_runtime_,&records_,&sound_bank_);
        state_.actor_script=actor_script_.Snapshot();
        if(state_.status){
            state_.status=main_flow_.Initialize(actor_runtime_,[this](uint32_t event,int32_t arg){return actor_script_.PostMainFlowEvent(event,arg);});
            if(!state_.status)throw std::runtime_error(state_.status.detail);
            state_.main_flow=main_flow_.Snapshot();
            state_.status=returned_interaction_.Initialize([this]{return actor_script_.OwnerEventTarget();},
                [this](const GameOwnerEventTarget& target,int32_t& result){
                    const auto activation=returned_interaction_.Snapshot().activation;
                    return actor_script_.PostConversationInteraction(target,activation.id,activation.kind==GameConversationProofKind::OwnedMain,result);
                });
            if(!state_.status)throw std::runtime_error(state_.status.detail);
            ++state_.opened_resources;state_.blocked=false;
            if(!graphics.Clear(0xFF000000,0)||!graphics.SwapBuffers())throw std::runtime_error("Scene4 clear failed");
            return true;
        }
    }catch(const std::exception& e){state_.status={GameFsCode::MalformedDirectory,0,e.what()};}
    state_.blocked=true;return false;
}
} // namespace dokodemo::recovered
