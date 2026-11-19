#include "boot_to_display.h"
#include "retail_slot_files.h"
#include "core_hash.h"
#include "radial_wipe.h"
#include "room_music.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <limits>
#include <new>

namespace dokodemo::gameplay {
namespace boot_detail {
constexpr std::size_t color_surface = 512 * Display::height * 4;
std::filesystem::path ColdSaveSlot(const std::filesystem::path& package_root) {
    return package_root / "savedata" / "UCJS10002000";
}
constexpr std::array<std::size_t,3> capacity = {0xb94000, 0x74e000, 0x46000};

// The root directory is read once from DATA.BP. It owns its bytes for the
// entire mount, while payloads remain on disk until a caller requests one.
class DirectoryLease final : public recovered::PackageStorageLease {
public:
    explicit DirectoryLease(std::vector<std::uint8_t> data)
        : bytes(std::move(data)), known(bytes.size(), 1) {
        static std::atomic<std::uint64_t> next{1};
        owner = next.fetch_add(1, std::memory_order_relaxed);
    }
    bool Span(recovered::PackageStorageSpan& out, std::string&) const override {
        out.bytes = bytes.data();
        out.initialized = known.data();
        out.size = bytes.size();
        out.host_address = reinterpret_cast<std::uintptr_t>(bytes.data());
        out.owner = owner;
        out.generation = out.serial = 1;
        // Relative-coordinate mount. No claim about the PSP malloc address.
        out.source_base = 0;
        out.readable = true;
        out.writable = false;
        return true;
    }
    mutable std::vector<std::uint8_t> bytes, known;
    std::uint64_t owner;
};

std::uint32_t ReadWord(const std::uint8_t* in) {
    return std::uint32_t(in[0]) | std::uint32_t(in[1]) << 8 |
        std::uint32_t(in[2]) << 16 | std::uint32_t(in[3]) << 24;
}

struct MountedPackage {
    std::shared_ptr<DirectoryLease> lease;
    recovered::BpDirectory directory;
};

MountedPackage MountPackage(std::vector<std::uint8_t> bytes) {
    const auto extent = bytes.size();
    auto lease = std::make_shared<DirectoryLease>(std::move(bytes));
    auto directory = recovered::BpDirectory::Parse(
        recovered::PackageStorage::Attach(lease, extent), extent);
    return {std::move(lease), std::move(directory)};
}
} // namespace boot_detail

using namespace boot_detail;

void BootToDisplay::ReleaseAligned::operator()(std::uint8_t* p) const noexcept {
    ::operator delete(p, std::align_val_t(16));
}

std::uint8_t* BootToDisplay::Arena::Allocate(std::size_t bytes) {
    const auto aligned = (used + 15u) & ~std::size_t(15);
    if (aligned > size || bytes > size - aligned) return nullptr;
    used = aligned + bytes;
    return data + aligned;
}

bool BootToDisplay::Initialize() {
    if (Ready()) return true;
    sampling_mode_ = 1;
    sampling_cycle_ = 0; // 52D58 analog input sampled once per refresh
    frontier_ = 0x100c;
    try {
        for (std::size_t i = 0; i < capacity.size(); ++i) {
            storage_[i].reset(static_cast<std::uint8_t*>(
                ::operator new(capacity[i], std::align_val_t(16))));
        }
        global_ = {storage_[0].get(), capacity[0], 0};
        static_assert(sizeof(Arena) <= 32, "BOOT 12FC child arena controller extent");
        for (std::size_t i = 0; i < children_.size(); ++i) {
            auto* location = global_.Allocate(32);
            if (!location) return false;
            children_[i] = new(location) Arena{storage_[i+1].get(), capacity[i+1], 0};
        }
        default_child_ = children_[0]; // 1360 binds PPK, children_[1] is KS.

        frontier_ = 0x1014;
        auto memory = std::make_shared<std::vector<std::uint8_t>>(2u << 20);
        Display candidate;
        if (!candidate.SetMode(0, Display::width, Display::height) ||
            !candidate.SetFrameBuf({memory, color_surface, 512, 3}, DisplaySync::vertical) ||
            !candidate.OnBlank(DisplaySync::vertical) ||
            !candidate.SetFrameBuf({memory, 0, 512, 3}, DisplaySync::horizontal) ||
            !candidate.OnBlank(DisplaySync::horizontal)) return false;
        display_ = std::move(candidate);
        vram_ = std::move(memory);
        draw_offset_ = color_surface;
        // DC4 requests color/stencil/depth clear after DBC's swap. Zeroed
        // host allocation has no evidence value as original screen content.
        clear_queued_ = true;
        frontier_ = 0x101c;
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool BootToDisplay::InitializeClock(const std::array<std::uint8_t,16>& date,
                                    std::uint32_t seconds) {
    if (frontier_ != 0x101c) return false;
    for (std::size_t i = 0; i < 6; ++i) {
        calendar_[6-i] = std::uint32_t(date[2*i]) | std::uint32_t(date[2*i+1]) << 8;
    }
    rng_.ApplyInitializerTimeT32(seconds, 0, "BOOT 1068 local civil epoch seconds");
    package_requests_.fill(0); // 253B8
    package_status_.fill(0);   // 258D4
    frontier_ = 0x107c;
    return true;
}

bool BootToDisplay::MountDataArchive(const std::filesystem::path& path) {
    if (archive_.is_open()) return true;
    if (frontier_ != 0x107c && frontier_ != 0x64d20) return false;
    frontier_ = 0x64d20;
    // Native counterparts of BioFsPspEMU/BioPfs created by 3F0/2200.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff extent = file.tellg();
    std::array<std::uint8_t,16> header{};
    file.seekg(0);
    if (extent < 16 || !file.read(reinterpret_cast<char*>(header.data()), header.size()))
        return false;
    const std::uint64_t bytes = 16ull + 16ull * ReadWord(header.data()+4) + ReadWord(header.data()+8);
    if (bytes > static_cast<std::uint64_t>(extent) || bytes > ReadWord(header.data()+12) ||
        bytes > capacity[1]) return false;
    try {
        auto lease = std::make_shared<DirectoryLease>(std::vector<std::uint8_t>(
            static_cast<std::size_t>(bytes)));
        std::copy(header.begin(), header.end(), lease->bytes.begin());
        if (!file.read(reinterpret_cast<char*>(lease->bytes.data()+16),
                       static_cast<std::streamsize>(bytes-16))) return false;
        auto directory = recovered::BpDirectory::Parse(
            recovered::PackageStorage::Attach(lease, bytes), static_cast<std::uint64_t>(extent));
        // DATA.BP is at assets/iso/PSP_GAME/USRDIR/data/DATA.BP.
        std::error_code path_error;
        auto assets = std::filesystem::absolute(path, path_error);
        if (path_error) return false;
        for (unsigned i = 0; i < 5; ++i) assets = assets.parent_path();
        assets_ = std::move(assets);
        directory_ = std::move(directory);
        archive_ = std::move(file);
        frontier_ = 0xb1c;
        return true;
    } catch (const recovered::PackageStorageError&) {
        return false;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

// Decode only the bytes delivered by the checked on-disk utility. Boot and
// GuiTitle issue separate mode-0 reads. The explicit menu selection then
// consumes the successful staging buffer without a third disk read.
bool BootToDisplay::LoadSavedRoomRequest(bool title_query) try {
    saved_room_.reset();
    slot_error_.clear();
    auto request = LoadRetailSlot(ColdSaveSlot(assets_.parent_path()));
    if (!request) {
        if (title_query) title_.SubmitSlotAbsent(true);
        return true;
    }
    auto saved = DecodeRetailUtilityRequest(*request,RetailSaveProfile{});
    const auto route = SelectLoadedBootRoute(saved.profile,calendar_);
    if (route == LoadedBootRoute::clock_notice) {
        slot_error_ = "Saved game time is ahead of the current clock";
        return false;
    }
    if (route != LoadedBootRoute::title_11 && route != LoadedBootRoute::resume_15) {
        slot_error_ = "Unsupported saved game profile or calendar";
        return false;
    }
    if (title_query && !IsLoadedTitleClockCurrent(saved.profile,calendar_)) {
        slot_error_ = "The title menu requires a time after the save";
        return false;
    }
    if (title_query && !saved.profile.ReadProfile().packed_birthday) {
        slot_error_ = "Title cannot continue this slot without a registered birthday";
        return false;
    }
    if (title_query) title_.SubmitLoadedSlot();
    else if (route == LoadedBootRoute::resume_15) title_.EnterSavedRoom();
    std::fprintf(stderr,"Verified first-day utility read at %05X, graph %zu bytes, %s\n",
                 title_query ? 0x3409c : 0x190bc, saved.graph_size,
                 title_query ? "title continue selection" :
                 route == LoadedBootRoute::resume_15 ? "cold room entry" : "cold title entry");
    saved_room_ = std::move(saved);
    return true;
} catch (const std::exception& ex) {
    slot_error_ = ex.what();
    return false;
}

bool BootToDisplay::PrepareTitle() {
    if (title_ready_) return true;
    if (!archive_.is_open()) return false;
    // Bounded cold-session CLoadMan adaptation. Run the resource requests
    // synchronously. The PSP async scheduler, sound system and save dialog
    // are outside this startup sequence.
    try {
        frontier_ = 0x17bcc; // current state 1 requests package 0 = DATA
        std::vector<std::uint8_t> payload;
        if (!ReadMember("DATA.BPM", payload)) return false;
        auto common = MountPackage(std::move(payload));
        frontier_ = 0x18ea4;
        auto font = common.directory.Open("FONT\\FONT.XFT");
        if (font.ReadLE32(0) != 0x31544658) return false; // XFT1

        // The first probe belongs to the boot owner. A protected file requires
        // an authenticated utility payload before 190DC can adopt its header.
        frontier_ = 0x190bc;
        std::error_code ec;
        const auto package_root = assets_.parent_path();
        if (!LoadSavedRoomRequest(false)) return false;
        if (!saved_room_) {
            frontier_ = 0x191e4;
            const auto space = std::filesystem::space(package_root, ec);
            if (ec || space.available < 800u * 1024u) return false;
        }

        // 1914C selects the title; 1916C selects the saved room. The port
        // prepares the shared GUI resources also used by the room notebook.
        frontier_ = title_.title_complete() ? 0x1916C : 0x1914c;
        if (!ReadMember("TITLE.BPM", payload)) return false;
        auto title = MountPackage(std::move(payload));
        frontier_ = 0x30940;
        const auto asset = title.directory.Resolve("GUI\\TITLE\\TITLE.CB2A");
        const auto& data = title.lease->bytes;
        const auto begin = data.begin() + static_cast<std::ptrdiff_t>(asset.offset);
        const std::vector<std::uint8_t> scene(begin,
            begin + static_cast<std::ptrdiff_t>(asset.size));
        // Confirm the prepared texture cache corresponds to this mounted
        // package. StartupLogos separately verifies both decoded RGBAs.
        auto texture_matches = [&](const char* name, const char* digest) {
            const auto file = title.directory.Resolve(name);
            return core::sha256_hex(data.data() + file.offset,
                static_cast<std::size_t>(file.size)) == digest;
        };
        if (!texture_matches("GUI\\TITLE\\SCELOGO.GIM",
                "e54837b49aed329656ecd2eea68542679e7c2d54c0eadf697ed1cc04c1020fb3") ||
            !texture_matches("GUI\\TITLE\\BEXIDELOGO.GIM",
                "ad0eb12e0f10e0ab73e75ea5e8f8eb79601d4afbf6165be953d970aa058a26bf"))
            return false;
        // Fresh session: PH1+7E65C starts in zero-filled BSS. 30FB4 selects
        // group/animation 5; 31040 binds it, and 31070/31084 enable playback.
        std::string error;
        const auto title_directory = assets_ / "game" / "title";
        if (!logos_.Load(scene, title_directory, error) ||
            !title_renderer_.Load(title_directory, error) ||
            !title_renderer_.LoadSoftkey(title_directory, error) ||
            !title_renderer_.LoadSelect(title_directory, error) ||
            !title_renderer_.LoadTrc(title_directory, error) ||
            !nickname_.Load(title_directory, error) ||
            !actors_.Load(title_directory, error)) return false;
        title_.BindRng(rng_);
        common_font_ = std::move(font);
        title_ready_ = true;
        frontier_ = 0x10d4;
        return true;
    } catch (const recovered::PackageStorageError&) {
        return false;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool BootToDisplay::AdvanceTitle(std::uint32_t buttons, bool character_voice_playing) {
    if (!title_ready_) return false;
    room_request_ = room_preload_ = false;
    const unsigned old_panel = title_.panel();
    title_.Advance(buttons, character_voice_playing);
    if (old_panel != 0 && title_.panel() == 0) {
        chosen_nickname_ = {};
        nickname_actor_.reset();
        player_name_ = {};
        phone_ = {};
    }
    // The later menu query observes the mounted package at request time.
    // A slot may have appeared since the boot owner's earlier probe.
    std::error_code media_error;
    switch (title_.media_request()) {
    case GuiTitleMediaRequest::slot: {
        frontier_ = 0x3409c;
        if (!LoadSavedRoomRequest(true)) return false;
        break;
    }
    case GuiTitleMediaRequest::free_space: {
        frontier_ = 0x34130;
        const auto space = std::filesystem::space(assets_.parent_path(), media_error);
        if (media_error) return false;
        title_.SubmitFreeBytes(space.available);
        break;
    }
    case GuiTitleMediaRequest::none:
        break;
    }
    if (title_.overwrite_requested()) {
        frontier_ = 0x314e4;
        slot_error_ = "Starting a replacement Pokepi with inherited words is not supported";
        return false;
    }
    if (title_.media_blocked()) return false;
    if (!UpdateProfile(buttons)) return false;
    if (clear_queued_ || title_.title_complete() || title_.panel() != 5 || title_frame_ >= 319) {
        // BOOT DC4: color, stencil and depth zero. Row padding is preserved.
        constexpr unsigned pitch = 512;
        for (unsigned y = 0; y < Display::height; ++y) {
            auto* color = vram_->data() + draw_offset_ + y*pitch*4;
            auto* depth = vram_->data() + 2*color_surface + y*pitch*2;
            std::fill_n(color, Display::width*4, std::uint8_t{0});
            std::fill_n(depth, Display::width*2, std::uint8_t{0});
        }
        clear_queued_ = false;
    }
    if (title_.title_complete()) {
        if (!next_scene_ready_) {
            if (!profile_ && !saved_room_) return false;
            std::string error;
            const bool loaded = saved_room_
                ? next_scene_.Load(assets_.parent_path(), *saved_room_, calendar_, error)
                : next_scene_.Load(assets_.parent_path(), *profile_, calendar_, error);
            if (!loaded) {
                std::fprintf(stderr, "After profile: %s\n", error.c_str());
                return false;
            }
            saved_room_.reset();
            next_scene_ready_ = true;
            room_bank_ = SelectFirstRoomBank(calendar_[3], calendar_[2], rng_.NextC3234());
            view_bank_ = room_bank_;
            room_preload_ = true;
        }
        if (!next_scene_.Draw(*vram_, draw_offset_, buttons, &common_font_, &rng_, &calendar_, &title_renderer_)) {
            if (!next_scene_.last_error().empty())
                std::fprintf(stderr, "Room update: %s\n", next_scene_.last_error().c_str());
            return false;
        }
        if (const auto bank = next_scene_.new_scene_music()) {
            view_bank_ = room_bank_ = *bank;
            room_preload_ = true;
        } else if (next_scene_.changed_view()) {
            const int scene_bank = next_scene_.selected_music();
            // 18890 reuses the stored room bank after the inventory closes.
            view_bank_ = scene_bank < 0 ? room_bank_ : scene_bank;
            room_preload_ = true;
        }
        room_request_ = next_scene_.music_requested();
        if (next_scene_.return_to_title_requested()) {
            next_scene_ = {};
            next_scene_ready_ = false;
            profile_.reset();
            title_.ReturnToSelectMenu();
            room_request_ = room_preload_ = false;
            room_bank_ = view_bank_ = -1;
        }
    } else if (title_.panel() == 5) {
        if (title_.frame() <= 319 && !logos_.Render(title_.frame(), *vram_, draw_offset_)) return false;
    } else if (!DrawTitleScene()) return false;
    if (!display_.SetFrameBuf({vram_, draw_offset_, 512, 3}, DisplaySync::horizontal) ||
        !display_.OnBlank(DisplaySync::horizontal)) return false;
    draw_offset_ = color_surface - draw_offset_;
    ++title_frame_;
    frontier_ = title_.panel() == 5 ? 0x10d4 : 0x30c34;
    return true;
}

bool BootToDisplay::UpdateProfile(std::uint32_t buttons) {
    editor_sounds_ = 0;
    if (title_.nickname_requested()) {
        if (title_.panel() == 3 && nickname_actor_ != title_.selected_actor()) nickname_.Open(title_.selected_actor(), buttons);
        else nickname_.OpenName(chosen_nickname_.text(), buttons);
        editor_ = Editor::nickname;
        editor_sounds_ = nickname_.se_mask();
    } else if (title_.player_name_requested()) {
        nickname_.OpenName(player_name_.text(), buttons);
        editor_ = Editor::player;
        editor_sounds_ = nickname_.se_mask();
    } else if (title_.phone_requested()) {
        nickname_.OpenPhone(phone_.text(), buttons);
        editor_ = Editor::phone;
        editor_sounds_ = nickname_.se_mask();
    } else if (title_.date_requested()) {
        date_editor_.Open(calendar_[6], {title_.year(),title_.month(),title_.day(),title_.zodiac()},
                          buttons, title_.profile_present_mask() == 127);
        editor_ = Editor::date;
        editor_sounds_ = date_editor_.se_mask();
    } else if (title_.confirmation_requested()) {
        confirmation_.Open(buttons);
        editor_ = Editor::confirmation;
        editor_sounds_ = confirmation_.se_mask();
    } else if (editor_ == Editor::date) {
        date_editor_.Step(buttons);
        editor_sounds_ = date_editor_.se_mask();
        if (const auto& result = date_editor_.result()) {
            title_.FinishDate(result->year, result->month, result->day, result->zodiac);
            editor_ = Editor::none;
        }
    } else if (editor_ == Editor::confirmation) {
        confirmation_.Step(buttons);
        editor_sounds_ = confirmation_.se_mask();
        if (const auto& result = confirmation_.result()) {
            title_.FinishConfirmation(*result == 1);
            editor_ = Editor::none;
        }
    } else if (editor_ != Editor::none) {
        nickname_.Step(buttons);
        editor_sounds_ = nickname_.se_mask();
        if (const auto& result = nickname_.result()) {
            // A source-null return restores the field the editor was opened on.
            const bool accepted = !result->empty();
            switch (editor_) {
            case Editor::nickname:
                if (accepted && !chosen_nickname_.Assign(*result)) return false;
                if (accepted) nickname_actor_ = title_.selected_actor();
                title_.FinishNickname(accepted);
                break;
            case Editor::player:
                if (accepted && !player_name_.Assign(*result)) return false;
                title_.FinishPlayerName(accepted);
                break;
            case Editor::phone:
                if (accepted && !phone_.Assign(*result)) return false;
                title_.FinishPhone(accepted);
                break;
            default: return false;
            }
            editor_ = Editor::none;
        }
    }
    if (title_.profile_yes_requested()) {
        profile_ = AdoptProfile(title_.selected_actor(), chosen_nickname_, player_name_, title_.gender_value(),
                                title_.year(), title_.month(), title_.day(), title_.blood_value(), phone_, calendar_);
        title_.FinishAdoption();
    }
    title_.UpdateGuide(nickname_.editing(), false, date_editor_.editing() ? int(date_editor_.mode()) : -1);
    return true;
}

namespace {
constexpr std::array<std::string_view,2> genders{"\x8f\x97", "\x92\x6a"};
constexpr std::array<std::string_view,4> blood_types{"\x82\x60","\x82\x61","AB","\x82\x6e"};
}

void BootToDisplay::DrawField(unsigned field, int x, int y) {
    const unsigned row = title_.active_profile_row();
    const ProfileDate committed{title_.year(),title_.month(),title_.day(),title_.zodiac()};
    const auto& date = editor_ == Editor::date ? date_editor_.visible_date() : committed;
    std::string value;
    switch (field) {
    case 0: if (editor_ != Editor::nickname) value = chosen_nickname_.text(); break;
    case 1: if (editor_ != Editor::player) value = player_name_.text(); break;
    case 2:
        if (title_.gender_value() > 0 && !title_.gender_list_visible())
            value = genders[title_.gender_value() == 2 ? 0 : 1];
        break;
    case 3: value = ProfileDateText(date); break;
    case 4: value = ProfileZodiacText(date.zodiac); break;
    case 5:
        if (title_.blood_value() >= 0 && !title_.blood_list_visible()) {
            value = blood_types.at(title_.blood_value());
            value += "\x8c\x5e";
        }
        break;
    case 6: if (editor_ != Editor::phone) value = phone_.text(); break;
    }
    DrawProfileField(value, x, y, field == row || (field == 4 && row == 3), common_font_,
                     *vram_, draw_offset_, field == 6 ? .75f : 1.f);
}

bool BootToDisplay::DrawPanel(bool foreground) {
    if (title_.panel() == 4)
        return title_renderer_.RenderProfile(title_.active_profile_row(), title_.profile_present_mask(),
            foreground, *vram_, draw_offset_, [&](unsigned field,int x,int y){ DrawField(field,x,y); });
    return title_renderer_.Render(title_.panel(), title_.frame(), title_.menu_variant(), *vram_, draw_offset_, foreground,
                                  {title_.arrow_frame(7), title_.arrow_frame(8)});
}

bool BootToDisplay::DrawTitleScene() {
    if (!DrawPanel(false)) return false;
    if (title_.guide_index() >= 0 && !title_renderer_.RenderGuide(
            title_.guide_index(), title_.guide_frame(), *vram_, draw_offset_)) return false;
    for (unsigned index=0; index<10; ++index) {
        const auto& actor = title_.actor(index);
        if (actor.visible && !actors_.Draw(actor.type, actor.motion, actor.time, actor.x, actor.y, actor.z,
                                          title_.panel(), *vram_, draw_offset_)) return false;
    }
    if (!DrawPanel(true)) return false;
    if (editor_ == Editor::nickname || editor_ == Editor::player || editor_ == Editor::phone) {
        if (!nickname_.Draw(title_renderer_, common_font_, *vram_, draw_offset_)) return false;
    } else if (editor_ == Editor::date || title_.gender_list_visible() || title_.blood_list_visible()) {
        const bool date = editor_ == Editor::date;
        const unsigned mode = date ? date_editor_.mode() : (title_.gender_list_visible() ? 1u : 2u);
        const unsigned cursor = date ? date_editor_.cursor() : title_.profile_list_cursor();
        const unsigned count = date ? date_editor_.count() : (mode == 1 ? 2u : 4u);
        if (!title_renderer_.RenderSelect(mode, date ? date_editor_.frame() : title_.profile_list_frame(),
                date ? date_editor_.closing() : title_.profile_list_closing(), cursor, count,
                date ? date_editor_.scroll_rows() : 0.f, *vram_, draw_offset_,
                [&](unsigned item,int x,int y,float scale,int top,int bottom){
                    std::string label;
                    if (mode == 1) label = genders.at(item);
                    else if (mode == 2) label = blood_types.at(item);
                    else label = std::to_string((mode == 3 ? date_editor_.base_year() : 1) + int(item));
                    DrawProfileField(label,x,y,item == cursor,common_font_,*vram_,draw_offset_,scale,top,bottom);
                })) return false;
    } else if (editor_ == Editor::confirmation && confirmation_.visible()) {
        if (!title_renderer_.RenderConfirmation(confirmation_.progress(),confirmation_.cursor(),*vram_,draw_offset_,
                [&](int x,int y,float scale){
                    DrawConfirmationText(ProfileConfirmation::question(),x,y,scale,common_font_,*vram_,draw_offset_);
                })) return false;
    }
    if (title_.wipe_visible()) ApplyRadialWipe(title_.radius(), *vram_, draw_offset_);
    return true;
}

bool BootToDisplay::ReadMember(const std::string& name, std::vector<std::uint8_t>& bytes) {
    if (!archive_.is_open()) return false;
    try {
        const auto entry = directory_.Resolve(name);
        if (entry.size > std::numeric_limits<std::size_t>::max()) return false;
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.size));
        archive_.clear();
        archive_.seekg(static_cast<std::streamoff>(entry.offset));
        if (!archive_.read(reinterpret_cast<char*>(payload.data()),
                           static_cast<std::streamsize>(payload.size()))) return false;
        bytes.swap(payload);
        return true;
    } catch (const recovered::PackageStorageError&) {
        return false;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace dokodemo::gameplay
