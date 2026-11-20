#pragma once

#include "display.h"
#include "first_frame.h"
#include "gui_title_state.h"
#include "title_renderer.h"
#include "pokepi_models.h"
#include "nickname_owner.h"
#include "profile_adoption.h"
#include "profile_date.h"
#include "profile_fields.h"
#include "post_profile.h"
#include "room_activity_phase.h"
#include "room_conversation_flow.h"
#include "room_sound.h"
#include "retail_save_slot.h"
#include "recovered/package_storage_native.h"
#include "recovered/rng_native.h"
#include <array>
#include <filesystem>
#include <fstream>

namespace dokodemo::gameplay {

// Single worker owner. An initialized scanout can exist before the game has
// drawn anything. Only the caller that actually renders a scene may publish it.
class BootToDisplay {
public:
    BootToDisplay() = default;
    BootToDisplay(const BootToDisplay&) = delete;
    BootToDisplay& operator=(const BootToDisplay&) = delete;
    bool Initialize();
    bool InitializeClock(const std::array<std::uint8_t,16>& date, std::uint32_t seconds);
    bool MountDataArchive(const std::filesystem::path& path);
    bool ReadMember(const std::string& name, std::vector<std::uint8_t>& bytes);
    bool PrepareTitle();
    bool AdvanceTitle(std::uint32_t buttons = 0, bool character_voice_playing = false);
    void BindMeetingOpener(OpenRoomMeeting open) { open_meeting_=std::move(open); }
    int RequestedMusic() const { return room_request_ ? view_bank_ : title_.bgm_bank(); }
    int PreloadMusic() const { return room_preload_ ? view_bank_ : -1; }
    unsigned RequestedEffects() const { return title_.se_mask() | editor_sounds_; }
    const std::vector<RoomSound>& RoomEffects() const { return next_scene_.requested_effects(); }
    RoomActivitySnapshot RoomActivity() const { return next_scene_.activity_snapshot(); }
    RoomConversationSnapshot RoomConversation() const { return next_scene_.conversation_snapshot(); }
    const std::vector<RoomDepartureEvent>& DepartureEvents() const { return next_scene_.departure_events(); }
    bool SceneMusicFade() const { return next_scene_.fade_scene_music(); }
    RoomTeachingSnapshot RoomTeaching() const { return next_scene_.teaching_snapshot(); }
    RoomOptionsSnapshot Options() const { return next_scene_.options_snapshot(); }
    RoomInventorySnapshot RoomBag() const { return next_scene_.inventory_snapshot(); }
    RoomNotebookSnapshot Notebook() const { return next_scene_.notebook_snapshot(); }
    void SetUpdateCalendar(const std::array<std::uint32_t,7>& calendar) { calendar_ = calendar; }
    const GuiTitleState& Title() const { return title_; }
    bool CommitNickname(std::string_view cp932) {
        return nickname_.AppendCommitted(cp932);
    }
    bool BackspaceNickname() { return nickname_.EraseLast(); }
    unsigned TitleFrame() const { return title_frame_; }
    bool Ready() const { return bool(vram_); }
    std::uint32_t FrontierPc() const { return frontier_; }
    const char* SaveReadBoundary() const { return slot_error_.empty() ? nullptr : slot_error_.c_str(); }
    const Display& Screen() const { return display_; }
    std::size_t DrawOffset() const { return draw_offset_; }
    bool ClearQueued() const { return clear_queued_; }
    const std::array<std::uint32_t,7>& Calendar() const { return calendar_; }
    const recovered::RngNativeOwner& Rng() const { return rng_; }

private:
    bool UpdateProfile(std::uint32_t buttons);
    bool DrawTitleScene();
    bool DrawPanel(bool foreground);
    void DrawField(unsigned field, int x, int y);
    struct ReleaseAligned { void operator()(std::uint8_t* p) const noexcept; };
    struct Arena {
        std::uint8_t* data;
        std::size_t size, used;
        std::uint8_t* Allocate(std::size_t bytes);
    };
    std::array<std::unique_ptr<std::uint8_t,ReleaseAligned>,3> storage_;
    Arena global_{};
    std::array<Arena*,2> children_{};
    Arena* default_child_ = nullptr;
    int sampling_mode_ = -1, sampling_cycle_ = -1;
    std::uint32_t frontier_ = 0x1004;
    bool LoadSavedRoomRequest(bool title_query);
    std::optional<RetailUtilityContents> saved_room_;
    std::string slot_error_;
    Display display_;
    std::shared_ptr<std::vector<std::uint8_t>> vram_;
    std::size_t draw_offset_ = 0;
    bool clear_queued_ = false;
    std::array<std::uint32_t,7> calendar_{};
    recovered::RngNativeOwner rng_;
    std::array<std::uint8_t,192> package_requests_{};
    std::array<std::uint8_t,160> package_status_{};
    std::ifstream archive_;
    recovered::BpDirectory directory_;
    std::filesystem::path assets_;
    recovered::PackageView common_font_;
    StartupLogos logos_;
    GuiTitleState title_;
    TitleRenderer title_renderer_;
    PokepiModels actors_;
    NicknameEditor nickname_;
    enum class Editor { none, nickname, player, date, phone, confirmation, overwrite };
    Editor editor_ = Editor::none;
    unsigned editor_sounds_ = 0;
    ProfileText chosen_nickname_, player_name_, phone_;
    std::optional<unsigned> nickname_actor_;
    ProfileDateEditor date_editor_;
    ProfileConfirmation confirmation_;
    std::vector<std::string> replacement_prompt_;
    std::optional<AcceptedProfile> profile_;
    PostProfile next_scene_;
    OpenRoomMeeting open_meeting_;
    bool next_scene_ready_ = false;
    int room_bank_ = -1;
    int view_bank_ = -1;
    bool room_request_ = false, room_preload_ = false;
    bool title_ready_ = false;
    unsigned title_frame_ = 0;
};

} // namespace dokodemo::gameplay
