#include "game_content_native.h"
#include "game_boot_native.h"
#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace dokodemo::recovered {
namespace {
GameFsStatus FormatFailure(const char* text) { return {GameFsCode::MalformedDirectory, 0, text}; }
}
GameFsStatus GameFontResource::Initialize(PackageView image) {
    Close();
    try {
        const auto size = image.Describe().size;
        if (size < 0x3C || image.ReadLE32(0) != 0x31544658) return FormatFailure("expected XFT1 image");
        const int32_t gw = int16_t(image.ReadLE16(4)), gh = int16_t(image.ReadLE16(6));
        const int32_t count = int16_t(image.ReadLE16(0x18));
        const auto stride = image.ReadLE32(0x28), offset = image.ReadLE32(0x2C);
        const int32_t auxiliary_div8 = int32_t(image.ReadLE32(0x34)) / 8;
        const auto auxiliary_offset = image.ReadLE32(0x38);
        if (gw <= 0 || gh <= 0 || count <= 0 || count > 4096 || !stride || auxiliary_div8 <= 0
            || uint64_t(offset) + uint64_t(count) * stride > size
            || auxiliary_offset >= size
            || !image.ReadLE16(0x20) || !image.ReadLE16(0x24))
            return FormatFailure("XFT texture/palette geometry outside actual member");
        std::vector<GameFontTexture> textures;
        textures.reserve(size_t(count));
        for (int32_t i = 0; i < count; ++i)
            textures.push_back({image.Subview(uint64_t(offset) + uint64_t(i) * stride, stride),
                image.ReadU8(0x1C), image.ReadLE16(0x20), image.ReadLE16(0x24)});
        //9435C publishes a pointer plus trunc(image.s32[34]/8). It does not
        // establish a full glyph/palette array length. Borrow the checked tail;
        // later consumers must check their own requested extent.
        auto auxiliary = image.Subview(auxiliary_offset, size - auxiliary_offset);
        image_ = std::move(image); auxiliary_ = std::move(auxiliary); textures_ = std::move(textures);
        state_ = {}; state_.ready = true; state_.image_bytes = uint32_t(size);
        state_.texture_count = uint32_t(count); state_.texture_bytes = stride;
        state_.auxiliary_offset = auxiliary_offset; state_.auxiliary_div8 = uint32_t(auxiliary_div8);
        state_.glyph_width = gw; state_.glyph_height = gh;
        return {};
    } catch (const PackageStorageError& error) { return {GameFsCode::MalformedDirectory, 0, error.what()}; }
}
const GameFontTexture* GameFontResource::Texture(uint32_t index) const {
    return state_.ready && index < textures_.size() ? &textures_[index] : nullptr;
}
void GameFontResource::Close() { textures_.clear(); auxiliary_.Reset(); image_.Reset(); state_ = {}; }

GameContentBoot::~GameContentBoot() { (void)Close(); }
bool GameContentBoot::InitializeBB8(GameArena& arena, GameVmBoot& manager) {
    if (arena_) return false;
    arena_ = &arena; manager_ = &manager; state_ = {};
    // Source request sizes40/1268 are metadata. Native typed layouts and task
    // records use their actual sizeof/alignment, not fake PSP object padding.
    if (void* memory = arena.Allocate(sizeof(Vsync), alignof(Vsync))) {
        vsync_ = new (memory) Vsync{};
        vsync_task_ = manager.AcquireTask();
        if (vsync_task_ == UINT32_MAX) { (void)Close(); return false; }
        state_.vsync_owner = true; state_.registered_records += 5;
    }
    if (void* memory = arena.Allocate(sizeof(LoadMan), alignof(LoadMan))) {
        loadman_ = new (memory) LoadMan{};
        constexpr char name[] = "/PSP_GAME/USRDIR/data/ksc/neko.ksc";
        static_assert(sizeof(name) == 35);
        std::copy_n(name, sizeof(name), loadman_->configured_script.begin());
        load_task_ = manager.AcquireTask();
        if (load_task_ == UINT32_MAX) { (void)Close(); return false; }
        state_.loadman_owner = true; ++state_.registered_records;
    }
    state_.bb8_returned = true; return true; // each failed allocation skips only its ctor
}
bool GameContentBoot::Reset24994() {
    if (!state_.bb8_returned || state_.loading_ticks) return false;
    reset_bytes_.fill(0); // explicit source reset, not BSS inference
    const auto word = [this](uint32_t at, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) reset_bytes_[at + i] = uint8_t(value >> (i * 8));
    };
    //39B80/39B20/39D64:11 slots of6196 bytes,28 entries of220 bytes.
    // These are the actual explicit -1 producers following the whole clear.
    for (uint32_t slot = 0; slot < 11; ++slot) {
        const auto base = 276 + slot * 6196;
        for (uint32_t entry = 0; entry < 28; ++entry)
            for (uint32_t offset : {4u, 0xD0u, 0xD4u, 0xD8u}) word(base + entry * 220 + offset, UINT32_MAX);
        word(base + 0x1830, UINT32_MAX);
    }
    reset_lookup_.fill(-1); // separate raw946C0 table,28 signed words
    // Reached setters24B1C/24B34/24B4C. Native byte backing preserves the
    // remaining explicit reset without assigning unknown resource meanings.
    reset_bytes_[0x10] = 2; reset_bytes_[0x14] = 3; reset_bytes_[0x18] = 3;
    state_.state_reset = true; return true;
}
uint32_t GameContentBoot::StateWord(uint32_t at) const {
    if (at > reset_bytes_.size() - 4) throw std::out_of_range("game state word");
    uint32_t word = 0;
    for (unsigned i = 0; i < 4; ++i) word |= uint32_t(reset_bytes_[at + i]) << (8 * i);
    return word;
}
int32_t GameContentBoot::ResetLookupWord(uint32_t index) const { return reset_lookup_.at(index); }
bool GameContentBoot::ConfigureImportCallback5334C() {
    if (!state_.state_reset) return false;
    // Native registration only. PSP subinterrupt30 is not installed or
    // synthetically fired;1424's SGX update remains disabled with SGX itself.
    state_.import_callback_configured = true;
    state_.import_callback_registered = false; // explicitly unavailable, not a successful service result
    return true;
}
bool GameContentBoot::TickLoading(GameFilesystem& filesystem) {
    if (!state_.state_reset || !state_.import_callback_configured || !vsync_ || !loadman_ || state_.closed) {
        state_.status = {GameFsCode::Unavailable, 0, "cold loading owners not available"}; return false;
    }
    if (state_.blocked) return true;
    ++state_.loading_ticks;
    try {
        // CVSync phase0/25258 runs BEFORE CLoadMan phase0/199DC.
        if (state_.mount_state == 1) {
            //2974/1E28 creates the queued node; it does NOT read yet.
            state_.mount_state = 2;
        } else if (state_.mount_state == 2) {
            // Next290C/1C3C really reads and binds the completed native node.
            PackageView package;
            auto status = filesystem.LoadMember("DATA.BPM", package);
            if (!status) { state_.status = status; return false; }
            auto directory = BpDirectory::Parse(package, package.Describe().size);
            package_ = std::move(package); directory_ = std::move(directory);
            state_.package_bytes = uint32_t(package_.Describe().size);
            state_.package_slots = directory_.Header().slots;
            state_.package_mounted = true;
            // Native read has really completed; source poll2CA4 then250A4
            // publishes the completed package bit and retires the mount job.
            state_.completed_bits |= 1; state_.mount_state = 0;
        }
        switch (state_.load_state) {
        case 1: //17B4C ->19AA8(next1,flag1)
            state_.next_scene = 1; state_.load_state = 2; break;
        case 2: //17B84's cold17BCC branch
            state_.previous_scene = state_.current_scene; state_.current_scene = state_.next_scene;
            state_.requested_bits |= 1; state_.load_state = 4; break;
        case 4: //18160/251F0
            state_.owned_bits |= 1; state_.mount_state = 1; state_.load_state = 5; break;
        case 5: //182C0 waits for actual published package completion
            if (state_.completed_bits & 1) state_.load_state = 6;
            break;
        case 6: { //18E84's first resource and real E364 consumer
            PackageView image = directory_.Open("FONT\\FONT.XFT");
            auto status = font_.Initialize(std::move(image));
            if (!status) { state_.status = status; return false; }
            state_.font = font_.Snapshot(); state_.font_ready = true;
            // Next source operation18EE8 allocates80 then invokesD54C.
            // No unimplemented constructor or following scene change succeeds.
            state_.frontier_pc = 0x18EE8; state_.blocked = true; break;
        }
        default: state_.status = {GameFsCode::Unavailable, 0, "unsupported cold CLoadMan state"}; return false;
        }
        state_.status = {}; return true;
    } catch (const PackageStorageError& error) { state_.status = {GameFsCode::MalformedDirectory, 0, error.what()}; return false; }
}
bool GameContentBoot::Close() {
    font_.Close(); directory_.Reset(); package_.Reset(); // before filesystem leases
    if (load_task_ != UINT32_MAX && !manager_->ReleaseTask(load_task_)) return false;
    load_task_ = UINT32_MAX;
    if (loadman_) { loadman_->~LoadMan(); if (!arena_->Release(loadman_)) return false; loadman_ = nullptr; }
    if (vsync_task_ != UINT32_MAX && !manager_->ReleaseTask(vsync_task_)) return false;
    vsync_task_ = UINT32_MAX;
    if (vsync_) { vsync_->~Vsync(); if (!arena_->Release(vsync_)) return false; vsync_ = nullptr; }
    arena_ = nullptr; manager_ = nullptr;
    state_.vsync_owner = state_.loadman_owner = state_.font_ready = state_.package_mounted = false;
    state_.registered_records = 0; state_.font = {}; state_.closed = true; return true;
}
} // namespace dokodemo::recovered
