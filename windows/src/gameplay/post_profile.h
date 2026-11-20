#pragma once
#include "room_sound.h"
#include "room_meeting_channel.h"
#include "room_activity_phase.h"
#include "room_conversation_flow.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>

namespace dokodemo::recovered {
class PackageView;
class RngNativeOwner;
} // namespace dokodemo::recovered

namespace dokodemo::gameplay {
struct AcceptedProfile;
struct RetailUtilityContents;
class TitleRenderer;

struct MemoReadingEditSnapshot {
    int word_row = -1;
    unsigned phase = 0, keyboard_row = 0, column = 0, page = 0;
    bool picking = false;
    std::string written, composition;
};

struct RoomNotebookSnapshot {
    MemoReadingEditSnapshot edit;
    int menu_column = -1;
    bool visible = false, list_ready = false;
    int resource = -1;
    bool detail = false;
    int tab = -1, selection = -1;
    unsigned count = 0;
    int row = -1, action_cursor = -1, delete_choice = -1;
    std::string written, reading;
};

struct RoomOptionsSnapshot {
    bool visible = false, active = false;
    int row = -1;
    std::array<int,3> values{2,3,3};
    int prompt_choice = -1;
};

struct RoomInventorySnapshot {
    bool visible = false, list_ready = false;
    unsigned filter = 0, selection = 0, count = 0;
    int slot = -1, id = -1, category = -1;
    bool actions_ready = false;
    unsigned action_cursor = 0;
    int discard_choice = -1;
};

struct RoomTeachingSnapshot {
    bool menu_open = false, word_open = false, picking = false, choosing_candidate = false;
    int menu_cursor = -1, menu_column = -1, menu_result = -1;
    unsigned menu_boundary_pc = 0;
    unsigned word_phase = 0, row = 0, column = 0, page = 0;
    unsigned written_bytes = 0, reading_bytes = 0, composition_bytes = 0;
    std::string written, reading, composition;
    unsigned candidate_count = 0;
    int candidate_index = -1;
    bool candidates_known = false;
    std::vector<std::string> candidates;
    unsigned learned_serial = 0;
    int learned_row = -1;
    std::string learned_written, learned_reading;
};

// Emitted only by the room owner when the corresponding operation executes.
struct RoomDepartureEvent {
    unsigned serial;
    const char* name;
    std::vector<std::pair<const char*,int>> values;
    std::vector<std::string> text;
};

class PostProfile {
  public:
    bool Load(const std::filesystem::path &game_directory, unsigned actor,
              const std::array<std::uint32_t, 7> &calendar, std::string &error);
    bool Load(const std::filesystem::path &game_directory, const AcceptedProfile &profile,
              const std::array<std::uint32_t, 7> &calendar, std::string &error,
              const RetailUtilityContents* previous = nullptr);
    bool Draw(std::vector<std::uint8_t> &vram, std::size_t offset, std::uint32_t buttons = 0,
              const recovered::PackageView *common_font = nullptr, recovered::RngNativeOwner *rng = nullptr,
              const std::array<std::uint32_t, 7> *calendar = nullptr, const TitleRenderer *softkey = nullptr);
    bool Load(const std::filesystem::path& game_directory, const RetailUtilityContents& saved,
              const std::array<std::uint32_t,7>& calendar, std::string& error);
    void BindMeetingOpener(OpenRoomMeeting open);
    bool return_to_title_requested() const;
    bool music_requested() const;
    const std::string &last_error() const;
    bool changed_view() const;
    bool introduction_finished() const;
    std::optional<unsigned> first_activity() const;
    RoomActivitySnapshot activity_snapshot() const;
    RoomConversationSnapshot conversation_snapshot() const;
    RoomTeachingSnapshot teaching_snapshot() const;
    RoomInventorySnapshot inventory_snapshot() const;
    RoomOptionsSnapshot options_snapshot() const;
    RoomNotebookSnapshot notebook_snapshot() const;
    int selected_music() const;
    std::optional<int> new_scene_music() const;
    bool fade_scene_music() const;
    const std::vector<RoomDepartureEvent>& departure_events() const;
    const std::vector<RoomSound> &requested_effects() const;

  private:
    struct Data;
    std::shared_ptr<Data> data_;
};

} // namespace dokodemo::gameplay
