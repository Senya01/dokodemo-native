#include "post_profile.h"
#include "cold_room_actor.h"
#include "post_profile_mesh.h"
#include "profile_adoption.h"
#include "retail_save_profile.h"
#include "retail_save_slot.h"
#include "retail_slot_files.h"
#include "room_save_owners.h"
#include "room_scene_models.h"
#include "room_scene_rows.h"
#include "room_calendar_items.h"
#include "room_activity_event.h"
#include "room_input_gate.h"
#include "room_answer_renderer.h"
#include "room_word_entry.h"
#include "room_menu.h"
#include "room_friends_banner.h"
#include "roof_scene.h"
#include "roof_signs.h"
#include "room_music.h"
#include "room_options.h"
#include "room_options_renderer.h"
#include "room_notebook.h"
#include "room_notebook_renderer.h"
#include "room_notebook_extra.h"
#include "room_notebook_diary.h"
#include "room_conversation_flow.h"
#include "room_activity_phase.h"
#include "room_activity_selector.h"
#include "room_inventory.h"
#include "room_inventory_renderer.h"
#include "room_talk_renderer.h"
#include "room_talk_state.h"
#include "nickname_owner.h"
#include "recovered/rng_native.h"
#include "recovered/calendar_native.h"
#include <optional>
#include "radial_wipe.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <fstream>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
using Matrix = std::array<float, 16>;
constexpr Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr std::size_t pixels = 512 * 272;
constexpr unsigned season_rows[]{1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0};

// 709C chooses the solar table on the seventh of each month. 7398
// classifies the half-open intervals between that table's time endpoints.
unsigned RoomPeriod(unsigned row, unsigned hour, unsigned minute) {
    const unsigned now = hour * 60 + minute;
    if (now < 240 - 10 * row)
        return 5;
    if (now < 360 - 10 * row)
        return 0;
    if (now < 600)
        return 1;
    if (now < 930 + 10 * row)
        return 2;
    if (now < 1080 + 10 * row)
        return 3;
    return 4;
}

// The original writes immediately. The Windows port cannot yet export all 27
// current actor roots used by GAME(46), so this is explicitly a host notice.
void DrawSaveUnavailable(const recovered::PackageView& font, std::vector<std::uint8_t>& vram,
                         std::size_t offset) {
    constexpr std::string_view lines[]{
        "Windows port: Save unavailable", "Cannot write the protected slot.", "Enter / Esc: return to room"
    };
    int half_width = 0;
    for (auto line : lines)
        half_width = std::max(half_width, int(std::ceil(MeasureUiText(line,font) / 2.f)));
    half_width = std::min(240, half_width + 12);
    for (int y=100; y<173; ++y) {
        for (int x=240-half_width; x<240+half_width; ++x) {
            const auto p=offset + 4*(512*y+x);
            vram[p]=28; vram[p+1]=34; vram[p+2]=44; vram[p+3]=255;
        }
    }
    int y=106;
    for (auto line : lines) {
        const int left=240-int(MeasureUiText(line,font)/2.f);
        DrawUiText(line,left,y,font,vram,offset,{255,255,255});
        y+=21;
    }
}

unsigned Bcd(unsigned n) { return (n / 10) * 16 + n % 10; }

int SignedTimeOperand(std::uint32_t minute) {
    const int payload = int(minute & 0x0fffffffu);
    return payload & 0x08000000 ? payload - 0x10000000 : payload;
}

unsigned Half(const unsigned char *p) { return p[0] + 256u * p[1]; }

std::vector<unsigned> RoomDecorations(const std::filesystem::path &dir, unsigned month, unsigned day) {
    std::ifstream stream(dir / "season-dates.bin", std::ios::binary);
    if (!stream)
        throw std::runtime_error("Missing room decoration calendar");
    const unsigned date = Bcd(month) * 256 + Bcd(day);
    std::vector<unsigned> chosen;
    unsigned char row[6]{};
    while (stream.read(reinterpret_cast<char *>(row), sizeof(row))) {
        const unsigned resource = Half(row), start = Half(row + 2), end = Half(row + 4);
        if (resource < 170 || resource > 195 || start > end)
            throw std::runtime_error("Invalid room decoration record");
        if (date >= start && date <= end && std::find(chosen.begin(), chosen.end(), resource) == chosen.end())
            chosen.push_back(resource);
    }
    if (!stream.eof() || stream.gcount() != 0 || chosen.empty())
        throw std::runtime_error("Truncated or empty room decoration table");
    return chosen;
}

std::string DecorationName(unsigned id) {
    if (id == 170)
        return "wallpaper0";
    if (id == 171)
        return "carpet0";
    std::string season = std::to_string(id - 171);
    return "season" + (season.size() < 2 ? std::string("0") : std::string()) + season;
}

Matrix RoomCamera(float x, float z, float fov) {
    // BOOT 7E28 / 80E4 / 8140. Source world uses Z up and the camera
    // looks toward positive Y. These are projection * view in column order.
    const float fy = 1.f / std::tan(fov * 3.14159265358979323846f / 360.f);
    const float fx = fy * 272.f / 480.f;
    const float near_z = 100.f, far_z = 10400.f;
    const float z_scale = (far_z + near_z) / (far_z - near_z);
    const float z_shift = -2.f * far_z * near_z / (far_z - near_z);
    return {fx, 0, 0, 0, 0, 0, z_scale, 1, 0, fy, 0, 0, -x * fx, -z * fy, 4400.f * z_scale + z_shift, 4400.f};
}
struct RoomCameraState {
    float x = 0, z = 980, fov = 30;
    float from_x = 0, from_z = 0, from_fov = 0, target_fov = 0, ratio = 0;
    enum class Mode { wide, approaching, close, retreating };
    Mode mode = Mode::wide;
    float wide_target_x = 0;

    static float FovForY(float y) {
        // 79D8 chooses between the four entries of this actor's PH1 curve.
        // The five cold-room actor entries all point to the same table.
        constexpr float depths[]{-1000, 0, 2000, 3000}, fields[]{23, 18, 13, 11};
        if (y <= depths[0])
            return fields[0];
        if (y >= depths[3])
            return fields[3];
        float key = std::nearbyint(10.f * y) * .1f;
        for (unsigned i = 0; i < 3; ++i)
            if (key >= depths[i] && key < depths[i + 1]) {
                const auto t = std::clamp((y - depths[i]) / (depths[i + 1] - depths[i]), 0.f, 1.f);
                return (1.f - t) * fields[i] + t * fields[i + 1];
            }
        return 30.f;
    }

    void Move(const std::array<float, 2> &point, bool request, bool return_wide = false) {
        if (return_wide && (mode == Mode::approaching || mode == Mode::close)) {
            // 234F8 -> 7844 records the current pose and samples the
            // wide horizontal clamp once before reversing the interpolation.
            from_fov = fov;
            from_x = x;
            from_z = z;
            wide_target_x = std::clamp(x, -950.f, 950.f);
            ratio = 1.f;
            mode = Mode::retreating;
        }
        if (request && mode == Mode::wide) {
            // GAME44(1) -> 2352C -> 7844 captures the existing wide camera.
            from_x = x;
            from_z = z;
            from_fov = fov;
            target_fov = FovForY(point[1]);
            ratio = 0;
            mode = Mode::approaching;
        }
        if (mode == Mode::wide) {
            x = std::nearbyint(.95f * x + .05f * std::clamp(point[0], -950.f, 950.f));
        } else if (mode == Mode::approaching) {
            // 7FCC samples 4308 with zero endpoint tangents, then advances
            // by 1/(D98D0 * (60 / source tick scalar 2)).
            const float s = ratio * ratio * (3.f - 2.f * ratio);
            fov = (1.f - s) * from_fov + s * target_fov;
            x = (1.f - s) * from_x + s * point[0];
            z = (1.f - s) * from_z + s * 600.f;
            ratio += 1.f / ((2.f / 3.f) * 30.f);
            if (ratio > 1.f) {
                ratio = 1.f;
                if (fov <= target_fov)
                    mode = Mode::close;
            }
        } else if (mode == Mode::retreating) {
            const float s = ratio * ratio * (3.f - 2.f * ratio);
            fov = (1.f - s) * 30.f + s * from_fov;
            x = (1.f - s) * wide_target_x + s * from_x;
            z = (1.f - s) * 980.f + s * from_z;
            ratio -= 1.f / (.833333373f * 30.f);
            if (ratio < 0.f) {
                ratio = 0;
                if (fov >= 30.f)
                    mode = Mode::wide;
            }
        } else {
            x = std::nearbyint(.95f * x + .05f * point[0]);
        }
    }
};

} // namespace

struct PostProfile::Data {
    RoomMesh background;
    std::vector<RoomMesh> decorations;
    ColdRoomActor actor;
    unsigned season = 0;
    std::optional<unsigned> television_clip;
    std::uint32_t television_time = 0;
    std::uint32_t light_time = 0, lamp_time = 0, paired_time = 0, door_time = 0;
    std::optional<unsigned> door_clip;
    bool music = false;
    RoofScene roof;
    RoofSigns signs;
    unsigned scene = 0;
    std::optional<unsigned> pending_scene;
    std::optional<int> scene_music;
    bool fade_music = false;
    enum class TravelCover { inactive, closing, closed, opening };
    TravelCover travel_cover = TravelCover::inactive;
    bool departure_wide = false, report_view = false, report_control = false;
    unsigned descriptor_serial = 0;
    bool caller_parked = false;
    std::optional<unsigned> last_travel_command;
    std::vector<RoomDepartureEvent> departure_events;
    using EventValues = std::initializer_list<std::pair<const char*,int>>;
    void DepartureEvent(const char* name, EventValues values) {
        departure_events.push_back({conversation.snapshot().serial,name,values});
    }
    unsigned CurrentMinute() const {
        const auto record = recovered::CalendarRecord689B8(
            {static_cast<int>(clock[6]), static_cast<int>(clock[5]), static_cast<int>(clock[4]),
             static_cast<int>(clock[3]), static_cast<int>(clock[2])});
        return recovered::CalendarScalar7C660(record);
    }
    void RecordMenuDestination(const char* event, bool record_deadline = false) {
        if (!scene) return;
        const auto place = talk.contact_location();
        DepartureEvent(event, {{"scene",int(scene)},{"index",int(place.descriptor_id)},
                               {"kind",int(place.kind)},{"resource",int(roof.resource())}});
        if (const auto deadline = talk.outing_return_deadline(); record_deadline && deadline) {
            departure_events.back().values.emplace_back("minute",int(CurrentMinute()));
            departure_events.back().values.emplace_back("deadline",int(*deadline));
        }
        departure_events.back().text = talk.current_outing_names();
    }
    void HandleTravel(unsigned period, recovered::RngNativeOwner& rng);
    void AdvanceTravelCover(std::vector<std::uint8_t>& vram, std::size_t offset);
    enum class View { room, inventory, notebook, friends, diary, options };
    View current_view = View::room, next_view = View::room;
    bool switching = false, view_changed = false;
    unsigned prompt_frame = 0;
    int gift_slot = -1;
    std::vector<RoomSound> effects;
    std::filesystem::path media;
    std::string error;
    std::array<std::uint32_t, 7> clock{};
    std::optional<AcceptedProfile> profile;
    RetailSaveProfile save_profile;
    RoomTalkState talk;
    RoomInputGate input_gate;
    RoomInputEvent pending_input = RoomInputEvent::none;
    RoomConversationFlow conversation;
    RoomActorRegion conversation_region{};
    RoomActivityEvent topics;
    RoomAnswerRenderer answer_gui;
    RoomMenu menu;
    RoomFriendsBanner friends_banner;
    RoomOptions options;
    RoomOptionsRenderer options_gui;
    int last_menu_result = -1;
    bool friends_destination_pending = false;
    RoomWordEntry word_entry;
    bool word_open = false;
    bool introduction_done = false;
    bool loaded_save = false, awaiting_entrance = false, return_to_title = false;
    std::filesystem::path game_directory;
    RoomActivitySelector activities;
    RoomActivityPhase activity;
    bool activity_flag = false; // 249D0 initializes the word read by GAME28.
    RoomTalkRenderer talk_gui;
    RoomInventory inventory;
    RoomInventoryRenderer inventory_gui;
    RoomNotebook notebook;
    RoomNotebookRenderer notebook_gui;
    RoomFriendNotebook friends;
    RoomNotebookDiary diary;
    RoomCalendarItems calendar_items;
    RoomSceneRows scene_rows;
    RoomSceneModels scene_models;
    RoomCameraState camera;
    float radius = -50;

    // Called when the ordinary save script reaches the mode-1 staging
    // request. The following VM serialization and utility operation own
    // their own results.
    void RecordTable(const char* event, const KscSaveValue& cell, EventValues fields) {
        const auto bytes = EncodeKscSaveGraph(cell.children.at(0));
        DepartureEvent(event, fields);
        departure_events.back().text.emplace_back(bytes.begin(), bytes.end());
    }

    void BindHomeModels() {
        if (!profile || profile->actor != 0) return;
        if (!scene_models.Bind(media, calendar_items, scene_rows, inventory, error))
            throw std::runtime_error(error);
        RecordTable("home-model-rows", scene_rows.SaveTable(), {});
    }

    void StageSaveHeader() {
        save_profile.SetOptions(options.values());
        if (diary.has_entry() && !save_profile.SetFirstDayDiary(
                diary.entry_text(), diary.entry_date(), diary.entry_revealed()))
            throw std::logic_error("Cannot stage the current first-day diary row");
        save_profile.StampForSave(clock);
    }

    void SwitchTo(View target) {
        next_view = target;
        switching = true;
    }

    void AdvanceCover(std::vector<uint8_t> &vram, std::size_t offset, std::uint32_t buttons) {
        const float limit = std::hypot(240.5f, 136.5f);
        if (!switching && radius == limit)
            return;
        ApplyRadialWipe(radius, vram, offset);
        if (switching) {
            radius = std::max(-50.f, radius - limit / 30.f);
            if (radius == -50.f) {
                switching = false;
                const auto previous = current_view;
                current_view = next_view;
                view_changed = true;
                if (current_view == View::inventory) {
                    inventory.Open(input_gate.owns_inventory() ? -1 : gift_slot, buttons);
                    ReceiveSystemSounds(inventory.TakeEffects());
                } else if (current_view == View::notebook) {
                    notebook.Open(talk.vocabulary(), buttons);
                    RecordMenuDestination("notebook-open");
                    ReceiveSystemSounds(notebook.TakeEffects());
                } else if (current_view == View::options) {
                    options.Open(buttons);
                    ReceiveSystemSounds(options.TakeEffects());
                } else if (current_view == View::friends) {
                    friends.Open(buttons);
                    ReceiveSystemSounds(friends.TakeEffects());
                } else if (current_view == View::diary) {
                    if (previous == View::diary) {
                        diary.FinishPageTurn(buttons);
                    } else {
                        diary.Open(buttons, clock[6], clock[5], clock[4], clock[3]);
                        DepartureEvent("diary-page", {{"year",int(clock[6])},{"month",int(clock[5])},
                            {"day",int(clock[4])},{"hour",int(clock[3])},{"pages",int(diary.page_count())},
                            {"index",int(diary.selection())},{"content",int(diary.page_has_content())},
                            {"recorded",int(diary.has_entry())}});
                        RecordMenuDestination("diary-open");
                    }
                    ReceiveSystemSounds(diary.TakeEffects());
                } else if (previous == View::diary) {
                    diary.Step(buttons, true);
                    input_gate.FinishNotebook();
                    RecordMenuDestination("diary-return");
                } else if (previous == View::options) {
                    input_gate.FinishOptions();
                } else if (previous == View::friends) {
                    friends.Step(buttons, true, nullptr);
                    input_gate.FinishNotebook();
                } else if (previous == View::notebook) {
                    notebook.Step(buttons, true);
                    input_gate.FinishNotebook();
                    RecordMenuDestination("notebook-return");
                } else if (previous == View::inventory) {
                    inventory.Step(buttons, true);
                    if (input_gate.owns_inventory())
                        input_gate.FinishInventory();
                    else
                        talk.FinishInventory(!inventory.slot_item(gift_slot));
                }
            }
        } else {
            radius = std::min(limit, radius + limit / 30.f);
            if (radius == limit)
                music = true;
        }
    }

    void ReceiveSystemSounds(const std::vector<unsigned>& cues) {
        for (const auto sound : cues)
            effects.push_back({5, 0, sound, 128, false});
    }

    void ReceiveWordEntrySounds() {
        const auto sounds = word_entry.se_mask();
        for (unsigned seq = 0; seq < 12; ++seq)
            if (sounds & (1u << seq)) effects.push_back({5, 0, seq, 128, false});
    }

    bool DrawNotebook(std::vector<std::uint8_t>& vram, std::size_t offset, std::uint32_t buttons,
                      const recovered::PackageView* font, const TitleRenderer* softkey) {
        if (!font) return false;
        const bool editing = notebook.reading_edit_active();
        notebook.Step(buttons, !switching && radius == std::hypot(240.5f, 136.5f));
        if(notebook.deletion_pending()) notebook.FinishDeletion(talk.vocabulary());
        ReceiveSystemSounds(notebook.TakeEffects());
        if (notebook.reading_edit_active()) {
            if (!softkey) return false;
            if (editing) {
                word_entry.Step(buttons, font);
            } else {
                const auto* word = notebook.selected_item();
                if (!word) return false;
                const std::string_view initial = word->reading;
                word_entry.OpenReading(initial == "(yomi=NULL)" ? std::string_view{} : initial, buttons);
            }
            ReceiveWordEntrySounds();
            using Phase = RoomWordEntry::Phase;
            switch (word_entry.phase()) {
            case Phase::closed_word:
                notebook.FinishReading(talk.vocabulary(), word_entry.written(), buttons);
                break;
            case Phase::closed_empty:
            case Phase::timed_out:
                notebook.FinishReading(talk.vocabulary(), std::nullopt, buttons);
                break;
            default: break;
            }
        }
        if (notebook.closing()) SwitchTo(View::room);
        if (!notebook_gui.Draw(notebook, *font, vram, offset)) return false;
        return !notebook.reading_edit_active() || word_entry.Draw(*softkey, vram, offset, font);
    }
    bool DrawOptions(std::vector<std::uint8_t>& vram, std::size_t offset, std::uint32_t buttons,
                     const recovered::PackageView* font) {
        if (!font) return false;
        options.Step(buttons, !switching && radius == std::hypot(240.5f, 136.5f));
        ReceiveSystemSounds(options.TakeEffects());
        if (options.closing()) SwitchTo(View::room);
        return options_gui.Draw(options, *font, vram, offset);
    }

    bool DrawFriends(std::vector<std::uint8_t>& vram, std::size_t offset, std::uint32_t buttons,
                     const recovered::PackageView* font, recovered::RngNativeOwner* rng) {
        if (!font || !profile) return false;
        friends.Step(buttons, !switching && radius == std::hypot(240.5f, 136.5f), rng);
        ReceiveSystemSounds(friends.TakeEffects());
        if (friends.closing()) SwitchTo(View::room);
        return friends.Draw(*profile, talk.vocabulary().player_saying(), *font, vram, offset);
    }
    bool DrawDiary(std::vector<std::uint8_t>& vram, std::size_t offset, std::uint32_t buttons,
                   const recovered::PackageView* font) {
        if (!font) return false;
        diary.Step(buttons, !switching && radius == std::hypot(240.5f, 136.5f));
        ReceiveSystemSounds(diary.TakeEffects());
        if (diary.closing()) SwitchTo(View::room);
        else if (diary.turning_page() && !switching) SwitchTo(View::diary);
        return diary.Draw(*font, vram, offset);
    }

    void UpdateWordEntry(std::uint32_t buttons, const recovered::PackageView* font);
    void UpdateConversation(unsigned minute, unsigned period, recovered::RngNativeOwner& rng);
    void OpenTopic(const RoomEventSelection& selection);
    bool DrawScene(std::vector<std::uint8_t> &vram, std::size_t offset, std::uint32_t buttons,
                   const recovered::PackageView *font, recovered::RngNativeOwner *rng, const TitleRenderer *softkey);
    bool DrawBag(std::vector<std::uint8_t> &vram, std::size_t offset, std::uint32_t buttons,
                 const recovered::PackageView *font, recovered::RngNativeOwner *rng);
};

bool PostProfile::Load(const std::filesystem::path &game_directory, unsigned actor,
                       const std::array<std::uint32_t, 7> &calendar, std::string &error) try {
    const unsigned month = calendar[5], day = calendar[4], hour = calendar[3], minute = calendar[2],
                   second = calendar[1];
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour >= 24 || minute >= 60 || second >= 60)
        throw std::runtime_error("Invalid scene calendar");
    const auto dir = game_directory / "assets/game/post-profile";
    auto next = std::make_shared<Data>();
    next->clock = calendar;
    next->media = dir;
    next->game_directory = game_directory;
    if (!next->background.Load(dir / "bg_room.bin", error, true))
        return false;
    if (!next->background.Duration(11))
        throw std::runtime_error("Missing authored room light control");
    for (const auto resource : RoomDecorations(dir, month, day)) {
        auto &decoration = next->decorations.emplace_back();
        if (!decoration.Load(dir / (DecorationName(resource) + ".bin"), error))
            return false;
    }
    next->season = season_rows[(month + 12 - (day >= 7 ? 1 : 2)) % 12];
    next->light_time = hour * 216000 + minute * 3600 + second * 60;
    if (!next->actor.Load(game_directory, actor, RoomPeriod(next->season, hour, minute), error))
        return false;
    // 8110 initializes the camera before the first room command can move the actor.
    next->camera.x = std::clamp(next->actor.position()[0], -950.f, 950.f);
    data_ = std::move(next);
    return true;
} catch (const std::exception &ex) {
    error = ex.what();
    return false;
}

bool PostProfile::Load(const std::filesystem::path &game_directory, const AcceptedProfile &profile,
                       const std::array<std::uint32_t, 7> &calendar, std::string &error) {
    if (!Load(game_directory, profile.actor, calendar, error))
        return false;
    auto &room = *data_;
    if ((profile.actor == 0 && (!room.activities.Load(room.media, error) ||
                                !room.topics.Load(room.media, profile.packed_birthday, error) ||
                                !room.menu.Load(room.media, error) ||
                                !room.friends_banner.Load(game_directory / "assets/game/room-talk", error) ||
                                !room.options_gui.Load(room.media, error) ||
                                !room.notebook_gui.Load(game_directory, error) ||
                                !room.friends.Load(game_directory / "assets/game/notebook", error) ||
                                !room.diary.Load(game_directory / "assets/game/notebook", error) ||
                                !room.word_entry.Load(game_directory / "assets/game/title", error) ||
                                !room.answer_gui.Load(game_directory / "assets/game", error))) ||
        !room.talk.Load(room.media, profile, error) ||
        !room.talk_gui.Load(game_directory / "assets/game", error) ||
        !room.inventory.Load(game_directory / "assets/game", error) ||
        !room.inventory_gui.Load(game_directory / "assets/game/inventory", error)) {
        data_.reset();
        return false;
    }
    if (profile.actor == 0) {
        try {
            room.scene_rows.Load(room.media);
            if (!room.calendar_items.Load(room.media, error)) throw std::runtime_error(error);
        } catch (const std::exception& ex) {
            error = ex.what();
            data_.reset();
            return false;
        }
    }
    room.talk.BindHomeInvitationReturn([owner=&room] { owner->topics.HomeInvitationReturned(); });
    room.save_profile.Adopt(profile);
    room.save_profile.EnterFirstDayRoom(profile.actor);
    room.profile = room.save_profile.ReadProfile();
    return true;
}

bool PostProfile::Load(const std::filesystem::path& game_directory,const RetailUtilityContents& saved,
                       const std::array<std::uint32_t,7>& calendar,std::string& error) try {
    const auto profile=saved.profile.ReadProfile();
    if (profile.actor!=0) throw std::invalid_argument("Only saved Toro rooms are currently supported");
    // Build a separate set of live owners. A rejected graph must not replace the active room.
    PostProfile resumed;
    if (!resumed.Load(game_directory,profile,calendar,error)) return false;
    auto& room=*resumed.data_;
    const auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    owners.Restore(room.talk,room.scene_rows,room.activity,room.inventory,room.topics);
    room.save_profile=saved.profile;
    room.save_profile.EnterFirstDayRoom(profile.actor);
    room.options.RestoreValues(room.save_profile.Options());
    if (const auto page=room.save_profile.FirstDayDiary())
        room.diary.RestoreFirstDay(page->text,page->date,page->revealed);
    room.loaded_save=true;
    data_=std::move(resumed.data_);
    return true;
} catch (const std::exception& ex) {
    error=ex.what();
    return false;
}

bool PostProfile::return_to_title_requested() const {
    return data_ && data_->return_to_title;
}

bool PostProfile::Draw(std::vector<std::uint8_t> &vram, std::size_t offset, std::uint32_t buttons,
                       const recovered::PackageView *common_font, recovered::RngNativeOwner *rng,
                       const std::array<std::uint32_t, 7> *calendar, const TitleRenderer *softkey) try {
    if (!data_ || offset > pixels * 4 || vram.size() < pixels * 10)
        return false;
    auto &room = *data_;
    room.music = false;
    if (calendar)
        room.clock = *calendar;
    room.effects.clear();
    room.departure_events.clear();
    room.fade_music = false;
    room.scene_music.reset();
    room.view_changed = false;
    std::fill_n(vram.begin() + offset, pixels * 4, 0);
    std::fill_n(vram.begin() + pixels * 8, pixels * 2, 0);
    bool ok = false;
    switch (room.current_view) {
    case Data::View::options: ok = room.DrawOptions(vram, offset, buttons, common_font); break;
    case Data::View::room: ok = room.DrawScene(vram, offset, buttons, common_font, rng, softkey); break;
    case Data::View::inventory: ok = room.DrawBag(vram, offset, buttons, common_font, rng); break;
    case Data::View::notebook: ok = room.DrawNotebook(vram, offset, buttons, common_font, softkey); break;
    case Data::View::friends: ok = room.DrawFriends(vram, offset, buttons, common_font, rng); break;
    case Data::View::diary: ok = room.DrawDiary(vram, offset, buttons, common_font); break;
    }
    if (!ok)
        return false;
    if (room.profile && room.profile->actor == 0) {
        const bool active = room.current_view == Data::View::room && !room.switching;
        const bool open = room.radius == std::hypot(240.5f, 136.5f);
        // Manager posts at the end of this update. The queued event is
        // delivered to the script on the next update, with the same owner.
        std::optional<int> menu_result;
        const bool was_open = room.menu.active();
        if (was_open) {
            const int column = room.menu.friends_column();
            const bool ready = room.menu.friends_ready();
            menu_result = room.menu.Step(buttons);
            const int next_column = room.menu.friends_column();
            if (column != next_column || ready != room.menu.friends_ready()) {
                if (column < 0 && next_column >= 0) {
                    room.RecordMenuDestination("friends-open",true);
                    room.friends_destination_pending = true;
                }
                room.DepartureEvent("friends-menu", {{"active",int(next_column >= 0)},{"column",next_column},
                                                      {"ready",int(room.menu.friends_ready())}});
            }
            if (menu_result) room.last_menu_result = *menu_result;
        }
        room.pending_input = room.input_gate.Step(buttons, {active, open,
                              !active || room.awaiting_entrance || room.talk.busy_for_room_input() || room.word_open, true}, menu_result);
        if (room.pending_input == RoomInputEvent::menu) {
            // This owner opens the full MMENU (13B28, mode from 2508C).
            room.save_profile.SetFullMenuMode();
            room.menu.Open(buttons, room.scene != 0);
            room.last_menu_result = -1;
        } else if (room.pending_input == RoomInputEvent::inventory) {
            room.SwitchTo(Data::View::inventory);
        } else if (room.pending_input == RoomInputEvent::save_request) {
            const bool return_to_title = room.last_menu_result == 15;
            // 2384C and 2388C enter different manager phases. Both require
            // the current actor data before their utility write can complete.
            room.DepartureEvent(return_to_title ? "system-title-request" : "system-save-request",
                                {{"result",room.last_menu_result},{"event",return_to_title ? 7 : 8}});
            try {
                room.StageSaveHeader();
                room.topics.messages().PrepareUsedDescriptorsForSave(); // 8D360 -> A0F3
                room.topics.PrepareTopicHistoryForSave(); // 8D36B -> 1D62
                const auto owners = RoomSaveOwners::Export(room.talk, room.scene_rows, room.activity,
                                                          room.inventory, room.topics);
                room.RecordTable("system-save-inventory", owners.inventory, {{"root",2}});
                room.RecordTable("system-save-scene-rows", owners.scene_rows, {{"root",3}});
                room.RecordTable("system-save-home-timer", owners.home_counter, {{"root",16}});
                room.RecordTable("system-save-message-timer", owners.message_counter, {{"root",18}});
                room.RecordTable("system-save-used-diaries", owners.used_diaries, {{"root",21}});
                room.RecordTable("system-save-messages", owners.diary_staging, {{"root",26}});
                const auto request = BuildRetailUtilityRequest(room.save_profile, owners.ToActorGraph());
                room.DepartureEvent("system-save-utility", {{"bytes",int(request.size())}});
                // The full request contains the player profile. Capture its bytes only in the private save test.
                const char* capture = std::getenv("DOKODEMO_TEST_SAVE_SNAPSHOT");
                if (capture && std::string_view(capture) == "1")
                    room.departure_events.back().text.emplace_back(request.begin(), request.end());
                WriteRetailSlot(room.game_directory/"savedata"/"UCJS10002000",room.game_directory,request);
                room.DepartureEvent("system-save-written", {{"result",room.last_menu_result},{"bytes",int(request.size())}});
                room.input_gate.SaveCompleted();
                room.return_to_title=return_to_title;
            } catch (const std::exception& ex) {
                room.DepartureEvent(return_to_title ? "system-title-save-unavailable" : "system-save-unavailable",
                                    {{"result",room.last_menu_result},{"game",46},{"script_site",0x8D382}});
                room.departure_events.back().text.emplace_back(ex.what());
            }
        } else if (room.pending_input == RoomInputEvent::dismiss_save_notice) {
            room.DepartureEvent("system-save-dismissed", {});
        } else if (room.pending_input == RoomInputEvent::options) {
            room.SwitchTo(Data::View::options);
        } else if (room.pending_input == RoomInputEvent::notebook) {
            switch (room.last_menu_result) {
            case 3: room.SwitchTo(Data::View::friends); break;
            case 4: room.SwitchTo(Data::View::notebook); break;
            case 6: room.SwitchTo(Data::View::diary); break;
            }
        }
        if (was_open || room.pending_input == RoomInputEvent::menu) {
            for (const auto cue : room.menu.effects()) room.effects.push_back({5, 0, cue, 128, false});
            if (!room.menu.Draw(vram, offset)) return false;
        }
    }
    if (room.travel_cover == Data::TravelCover::inactive) room.AdvanceCover(vram, offset, buttons);
    else room.AdvanceTravelCover(vram, offset);
    if (room.input_gate.shows_save_notice()) {
        if (!common_font) throw std::runtime_error("Missing room font for save notice");
        DrawSaveUnavailable(*common_font,vram,offset);
    }
    if (room.friends_destination_pending && room.input_gate.room_controls_ready() && !room.menu.active() &&
        !room.talk.busy_for_room_input() && room.current_view == Data::View::room) {
        room.RecordMenuDestination("friends-return",true);
        room.friends_destination_pending = false;
    }
    if (room.report_control && room.input_gate.room_controls_ready() && !room.report_view &&
        room.travel_cover == Data::TravelCover::inactive && !room.talk.busy_for_room_input()) {
        room.DepartureEvent(room.scene ? "control" : "return", {{"scene",int(room.scene)}});
        room.report_control = false;
    }
    return true;
} catch (const std::exception &ex) {
    if (data_)
        data_->error = ex.what();
    return false;
}

void PostProfile::Data::AdvanceTravelCover(std::vector<std::uint8_t>& vram, std::size_t offset) {
    const float full = std::hypot(240.5f,136.5f);
    ApplyRadialWipe(radius,vram,offset);
    if (travel_cover == TravelCover::closing) {
        radius = std::max(-50.f,radius - full/30.f);
        if (radius == -50.f) travel_cover = TravelCover::closed;
    } else if (travel_cover == TravelCover::opening) {
        radius = std::min(full,radius + full/30.f);
        if (radius == full) {
            travel_cover = TravelCover::inactive;
            music = true;
        }
    }
}

void PostProfile::Data::HandleTravel(unsigned period, recovered::RngNativeOwner& rng) {
    const auto& flow = conversation.snapshot();
    if (!flow.running || (flow.argument != 3 && flow.argument != 4 && flow.topic_pc != 0x12f39)) return;
    if ((flow.topic_pc == 0x13219 || flow.topic_pc == 0x12f39) && talk.outing_descriptor() && descriptor_serial != flow.serial) {
        const auto& d = *talk.outing_descriptor();
        DepartureEvent("selected", {{"index",int(d.destination)},{"kind",int(d.kind)},{"row",int(d.word)},
            {"eligibility",int(d.eligibility_word)},{"primary",int(d.primary_filter)},{"secondary",int(d.secondary_filter)}});
        departure_events.back().text = {d.written,d.label,d.title,d.description};
        descriptor_serial = flow.serial;
    }
    for (const unsigned site : talk.outing_sites()) {
        DepartureEvent("signal", {{"site",int(site)}});
        if (site == 0x1c5a) caller_parked = true;
    }
    departure_wide = talk.TakeOutingWideCamera();
    if (const auto open = talk.TakeOutingWipe()) {
        travel_cover = *open ? TravelCover::opening : TravelCover::closing;
        DepartureEvent("wipe", {{"open",int(*open)}});
    }
    if (talk.outing_inventory_restore_requested()) {
        BindHomeModels();
        if (!talk.FinishOutingInventoryRestore()) throw std::logic_error("Lost outing inventory owner");
        int first_placed = -1;
        for (int slot = 0; slot < RoomInventory::slot_count; ++slot) {
            if (inventory.slot_item(slot) && inventory.PlacementWord(slot,7)) {
                first_placed = slot;
                break;
            }
        }
        DepartureEvent("inventory", {{"slot",first_placed}});
    }
    const auto request = talk.outing_scene_request();
    if (!request) return;
    if (!pending_scene) {
        if (!((*request == 12 && scene == 0) || (*request == 0 && scene == 12)))
            throw std::runtime_error("Unsupported outing scene request " + std::to_string(*request));
        pending_scene = request;
        fade_music = true;
        if (travel_cover != TravelCover::closed) travel_cover = TravelCover::closing;
        DepartureEvent("requested", {{"scene",int(*request)}});
        return;
    }
    if (pending_scene != request) throw std::logic_error("Outing selector changed during scene preparation");
    if (travel_cover != TravelCover::closed || !caller_parked) return;

    // Scene unload clears the old bank. 19B6C chooses the new bank before 5F58 loads the model.
    const int bank = SelectTravelRoomBank(period,rng.NextC3234());
    if (*request == 12) {
        scene_models.Clear();
        if (!roof.Load(media,period,rng,error) || !signs.Load(media,roof.resource(),talk.outing_labels(),error))
            throw std::runtime_error(error);
    } else {
        // Restore the room's own scene controls. Its mesh stays resident during the outing.
        television_clip.reset();
        door_clip.reset();
        paired_time = lamp_time = door_time = 0;
    }
    if (!actor.AdoptScene(*request)) throw std::runtime_error("Outing actor placement failed");
    light_time = clock[3]*216000 + clock[2]*3600 + clock[1]*60;
    scene = *request;
    scene_music = bank;
    report_view = true;
    view_changed = true;
    if (!talk.FinishOutingScene(scene)) throw std::logic_error("No waiting outing caller for adopted scene");
    pending_scene.reset();
    caller_parked = false;
    if (scene == 12) DepartureEvent("consumed", {{"scene",12},{"resource",int(roof.resource())},{"period",int(period)},{"bank",bank}});
    else DepartureEvent("consumed", {{"scene",0},{"period",int(period)},{"bank",bank}});
}

bool PostProfile::Data::DrawScene(std::vector<std::uint8_t> &vram, std::size_t offset, std::uint32_t buttons,
                                  const recovered::PackageView *font, recovered::RngNativeOwner *rng, const TitleRenderer *softkey) {
    talk.SetMessageSpeed(options.values()[0]);
    const auto minute = CurrentMinute();
    if (profile && profile->actor == 0 && activity.state() == RoomActivityPhase::State::unloaded) {
        if (!rng) return false;
        // 183E0 selects the room bank before this VM starts. 17A6 picks
        // the adopted player row's initial saying before 8DDF5 sets timers.
        if (!loaded_save) {
            talk.vocabulary().StartActorScript(rng->NextC3234());
            topics.StartActorScript(minute);
        }
        const int previous_day = activity.day_number();
        if (activity.RegisterDay(minute)) { // 8E13A calls 1B34 before calendar/model setup.
            topics.ResetMessageDay(minute);
            RecordTable("diary-staging-clear", topics.messages().SaveTable(), {{"root",26}});
        }
        if (loaded_save) {
            if (activity.day_number() < 1)
                throw std::runtime_error("Saved stay reached the departure path at 8E146");
            DepartureEvent("room-load-restored", {{"day",activity.day_number()},
                           {"previous_day",previous_day},{"graph_fields",27}});
        }
        // Calendar builtins consume the signed 28-bit payload of the VM time value.
        const int signed_minute = SignedTimeOperand(minute);
        const unsigned date = recovered::CalendarBcdDate68FE0(signed_minute);
        if (activity.day_number() >= 0) {
            const int selector = (signed_minute / 1440 + 5) % 7;
            const auto seasonal = calendar_items.Update(date, date, profile->packed_birthday);
            RecordTable("scene-rows-before", scene_rows.SaveTable(),
                            {{"day",activity.day_number()},{"selector",selector},{"date",int(date)},
                             {"calendar_items",int(seasonal)}});
            const auto draws = scene_rows.Update(selector, date, *rng);
            RecordTable("scene-rows-updated", scene_rows.SaveTable(),
                            {{"roll0",draws[0]},{"roll1",draws[1]},{"roll2",draws[2]},
                             {"roll3",draws[3]},{"roll4",draws[4]}});
        }
        BindHomeModels(); // 8DDFC schedules 3A91/393C before the room deadlines.
        activity.Enter(minute, *rng);
        const auto scheduled=activity.ScheduledEvent(talk.ProgressFlagWords()[0].word & 0x0fffffffu);
        if (loaded_save && scheduled==0) {
            actor.EnterWithoutInvitation();
            talk.EnterWithoutIntroduction();
            topics.ResumeDay(minute);
            awaiting_entrance=true;
        }
        if (scheduled==1) input_gate.RequestAction(); // 8DDA3: dispatch first-day introduction.
        else if (scheduled) throw std::runtime_error("Unsupported room event at 8DD8C: "+std::to_string(scheduled));
    }
    if (!actor.Advance(minute))
        throw std::runtime_error(actor.error().empty() ? "Room actor update failed" : actor.error());
    if (awaiting_entrance && actor.entrance_finished()) {
        activity.EnterUnscheduledLoop();
        awaiting_entrance=false;
        introduction_done=true;
    }
    if (const auto light = actor.lamp_write()) {
        if (*light) {
            lamp_time = 0; // 707C seeks the LIGHT_ON control to zero.
        } else {
            // 7048 -> 709C -> 72B8 selects the current seasonal track
            // and seeks it to the source hour, minute and second.
            season = season_rows[(clock[5] + 12 - (clock[4] >= 7 ? 1 : 2)) % 12];
            light_time = clock[3] * 216000 + clock[2] * 3600 + clock[1] * 60;
        }
    }
    if (const auto value = actor.game28_write())
        activity_flag = *value != 0;
    if (!switching && radius == std::hypot(240.5f, 136.5f))
        effects.insert(effects.end(), actor.sounds().begin(), actor.sounds().end());
    if (profile && !switching) {
        if (last_travel_command && actor.command_ready()) {
            DepartureEvent("ready", {{"command",int(*last_travel_command)}});
            last_travel_command.reset();
        }
        talk.SetCalendarMinute(minute);
        talk.SetOutingEnvironment(activity_flag, actor.light_on(), RoomPeriod(season, clock[3], clock[2]),
                                  camera.mode == RoomCameraState::Mode::approaching || camera.mode == RoomCameraState::Mode::close);
        talk.SetOutingActorFlags(actor.source_field5c(), actor.source_flag25a());
        talk.SetActorField50(int(actor.source_field50()));
        const bool answer_was_ready = talk.answer_interactive();
        const unsigned previous_answer = talk.answer_cursor();
        talk.Step(buttons, actor.talk_ready(), actor.command_ready(), font, actor.invitation_expired(),
                  pending_input == RoomInputEvent::during_action, rng);
        if (conversation.snapshot().argument == 2 && talk.answer_interactive() &&
            (!answer_was_ready || previous_answer != talk.answer_cursor())) {
            DepartureEvent("friends-ask", {{"cursor",int(talk.answer_cursor())},
                                            {"count",int(talk.answer_options().size())}});
            departure_events.back().text = talk.answer_options();
        }
        friends_banner.Step();
        if (const auto show = talk.TakeFriendsBanner()) {
            friends_banner.Set(*show);
            DepartureEvent("friends-banner", {{"show",int(*show)}});
        }
        actor.SetMood(talk.mood());
        if (!talk.error().empty())
            throw std::runtime_error(talk.error());
        if (talk.TakeFirstDayDiaryRequest()) {
            if (!rng) throw std::logic_error("Missing first-day diary selector RNG");
            if (diary.RecordFirstDay(topics.messages(), *rng, profile->player.text(), clock[6], clock[5], clock[4])) {
                RecordTable("diary-staging-written", topics.messages().SaveTable(), {{"root",26}});
                DepartureEvent("diary-created", {{"year",int(clock[6])},{"month",int(clock[5])},
                                                 {"day",int(clock[4])},{"hour",int(clock[3])}});
                departure_events.back().text = {std::string(profile->player.text())};
            }
        }
        for(const auto topic : talk.TakeTopicEnqueues()) topics.Enqueue(topic);
        if (talk.consumed())
            effects.push_back({5, 0, 4, 128, true}); // 1D05C
        for (const auto sound : talk.TakeEffects())
            effects.push_back({5, 0, sound, 128, false});
        if (talk.TakeOutingInterrupt() && !actor.InterruptCommand())
            throw std::runtime_error("Outing actor interrupt failed");
        if (const auto command = talk.actor_command()) {
            if (!actor.Request(*command)) throw std::runtime_error("Unimplemented room actor request " + std::to_string(*command));
            if (talk.outing_descriptor() && conversation.snapshot().running &&
                (conversation.snapshot().argument == 3 || conversation.snapshot().argument == 4)) {
                last_travel_command = command;
                DepartureEvent("task", {{"command",int(*command)}});
            }
        }
        if (const auto modulus = talk.random_requested()) {
            if (!rng || !*modulus)
                return false;
            talk.SupplyRandom(rng->NextC3234() % *modulus);
        }
        if (talk.word_entry_requested()) UpdateWordEntry(buttons, font);
        if (talk.gift_requested()) {
            if (!rng)
                return false;
            const unsigned choice = rng->NextC3234() % 5;
            const auto date = recovered::CalendarBcdDate68FE0(SignedTimeOperand(minute));
            gift_slot = inventory.AwardGiftOnDate(profile->actor, choice, profile->nickname.text(), date);
            if (const auto* gift = inventory.slot_item(gift_slot))
                RecordTable("gift-received", inventory.SaveRow(gift_slot),
                            {{"slot",gift_slot},{"id",gift->id}});
            talk.SupplyGift(gift_slot);
        }
        if (const auto wanted = talk.inventory_requested()) {
            gift_slot = *wanted;
            SwitchTo(View::inventory);
        }
    }
    bool return_wide = false;
    if (profile && profile->actor == 0 && !switching) {
        if (!rng) return false;
        const auto period = RoomPeriod(season, clock[3], clock[2]);
        HandleTravel(period, *rng);
        if (talk.wide_camera_requested() || talk.ordinary_source_return()) {
            introduction_done = true;
            unsigned boundary = 0;
            if (talk.ordinary_source_return()) {
                const unsigned argument = conversation.snapshot().argument;
                if (argument == 3 && !scene && !talk.TakeOutingRoomRequest())
                    throw std::logic_error("Outing returned without a current-room request");
                if ((argument == 3 && scene == 12) || argument == 4) {
                    if (pending_scene || (argument == 4 && scene != 0))
                        throw std::logic_error("Outing caller returned before its scene was adopted");
                    report_control = true;
                }
                boundary = topics.FinishConversation(minute, argument == 0 || talk.counted_contact());
                if (boundary) conversation.Boundary(boundary);
                else conversation.Finish();
            } else {
                topics.FinishIntroduction(minute);
            }
            if (!boundary) {
                talk.vocabulary().BeginConversation(); // 8D934 before return to room
                input_gate.FinishAction(); // GAME3 in 8D932(false)
                const auto argument = conversation.snapshot().argument;
                const bool reset_timer = !talk.ordinary_source_return() || argument < 3 ||
                                         (argument >= 7 && argument <= 9 && talk.friends_reset_contact_timer());
                activity.ReturnToRoom(minute, talk.mood(), *rng, reset_timer);
                return_wide = true;
            }
        } else if (pending_input == RoomInputEvent::ordinary || pending_input == RoomInputEvent::teaching ||
                   pending_input == RoomInputEvent::outing || pending_input == RoomInputEvent::return_home ||
                   pending_input == RoomInputEvent::friends_action) {
            unsigned argument = 0;
            switch (pending_input) {
            case RoomInputEvent::teaching: argument = 1; break;
            case RoomInputEvent::friends_action: {
                // BOOT D9874 entries 8..11 dispatch event (0, argument).
                constexpr unsigned arguments[]{2,7,8,9};
                argument = arguments[last_menu_result - 8];
                break;
            }
            case RoomInputEvent::outing: argument = 3; break;
            case RoomInputEvent::return_home: argument = 4; break;
            default: break;
            }
            if (conversation.Begin(argument)) {
                if (pending_input == RoomInputEvent::friends_action)
                    DepartureEvent("friends-choice", {{"result", last_menu_result},{"argument",int(argument)}});
                if (activity.BeginOrdinary()) {
                    input_gate.RequestAction(); // 8DA44 -> 8D4CC(2,0)
                    UpdateConversation(minute, period, *rng);
                } else {
                    // The manager delivered (0,0), but this VM is no longer
                    // executing the admitted ambient branch.
                    conversation.Boundary(activity.alternate_pc() ? activity.alternate_pc() : 0x8da35);
                }
            }
        } else if (activity.state() == RoomActivityPhase::State::ordinary) {
            UpdateConversation(minute, period, *rng);
        } else {
            const RoomActivityFrame frame{period, actor.light_on(), talk.mood(), talk.mood_age(),
                                          actor.source_field50(), actor.source_field5c(), actor.source_flag25a(),
                                          actor.started_activity(), actor.command_ready(),scene, talk.outing_return_deadline()};
            activity.Advance(minute, frame, *rng);
            if (activity.state() == RoomActivityPhase::State::return_home && conversation.Begin(4) && activity.BeginOrdinary()) {
                input_gate.RequestAction();
                UpdateConversation(minute,period,*rng);
            }
            if (const auto command = activity.lamp_command(); command && !actor.RequestRoomControl(*command))
                activity.RefuseLampCommand();
            if (const auto mood = activity.mood_write()) {
                talk.SetMood(*mood);
                actor.SetMood(*mood);
            }
            if (activity.state() == RoomActivityPhase::State::select) {
                // Scene 0 is the room registered at 8E161. GAME23/24/28/36
                // and mood are sampled again by 13482 on every turn.
                const RoomActivityInputs input{activity.first_after_cleanup(), int(scene), int(actor.source_field58()),
                                               int(actor.source_field50()), int(period), talk.mood(),
                                               activity_flag};
                const auto selection = activities.Select(input, *rng);
                const bool accepted = !selection.command || actor.RequestActivity(*selection.command);
                activity.FinishSelection(input, selection, accepted, *rng);
            }
        }
    }
    const auto pair = actor.scene_pair_requested();
    if (pair == 2 || pair == 3) {
        door_clip = unsigned(pair);
        door_time = 0;
    }
    if (pair == 0 || pair == 1) {
        television_clip = 12u + unsigned(pair);
        television_time = 0;
    }
    camera.Move(actor.position(), actor.zoom_requested() || conversation.close_camera(),
                return_wide || departure_wide || conversation.wide_camera());
    departure_wide = false;
    const auto view = RoomCamera(camera.x, camera.z, camera.fov);
    const unsigned seasonal_clip = actor.light_on() ? 11 : 4 + season;
    const auto time = actor.light_on() ? lamp_time : light_time;
    std::vector<std::pair<unsigned, std::uint32_t>> clips{
        {seasonal_clip, time}, {1, paired_time}, {0, paired_time}};
    if (door_clip) clips.emplace_back(*door_clip, door_time);
    if (television_clip)
        clips.emplace_back(*television_clip, television_time);
    RoomLighting lighting;
    const bool measuring = report_view && travel_cover == TravelCover::inactive && !switching &&
                           radius == std::hypot(240.5f,136.5f);
    bool drawn = false;
    if (scene == 12) {
        if (!roof.Draw(view,season,light_time,vram,offset,lighting,measuring ? &drawn : nullptr) ||
            !signs.Draw(roof.Background(),roof.main_clips(),view,vram,offset)) return false;
    } else {
        lighting = background.Lighting(clips);
        if (scene_models.bound()) {
            if (!scene_models.Draw(background, clips, view, vram, offset, &lighting)) return false;
        } else {
            for (const auto &decoration : decorations)
                if (!decoration.Draw({}, identity, view, vram, offset, &lighting)) return false;
        }
        if (!background.Draw(clips, identity, view, vram, offset, &lighting) ||
            (scene_models.bound() && !scene_models.DrawGifts(background, clips, view, vram, offset, &lighting))) return false;
        if (measuring) {
            for (std::size_t i=offset; i<offset+pixels*4 && !drawn; i+=4)
                drawn = vram[i] || vram[i+1] || vram[i+2];
        }
    }
    if (measuring && drawn) {
        DepartureEvent("view", {{"scene",int(scene)},{"drawn",1},{"wipe",0}});
        report_view = false;
    }
    if (!actor.Draw(view, vram, offset, &lighting))
        return false;
    if (!friends_banner.Draw(vram, offset)) return false;
    if (profile && talk.prompt_visible()) {
        prompt_frame = std::min(8u, prompt_frame + 1);
        if (!talk_gui.DrawPrompt(vram, offset, prompt_frame))
            return false;
    } else
        prompt_frame = 0;
    if (profile && talk.visible()) {
        if (!font)
            return false;
        const auto xy = actor.position();
        RoomSpeechPlacement placement;
        std::array<float, 3> body;
        if (!actor.SpeechAnchor(body) ||
            !RoomTalkRenderer::Project(view, camera.fov, body, {xy[0], xy[1], 0}, placement, talk.rows()))
            return false;
        std::vector<RoomTextLine> text;
        for (const auto &line : talk.lines())
            text.push_back({line, float(text.size() * 18) - talk.scroll(), 0});
        if (!talk_gui.DrawMessage(placement, talk.appearance(), text, vram, offset,
                                  [&](const RoomTextLine &line, int left, int top, int width, int height) {
                                      DrawUiText(line.cp932, left + int(line.x), top + int(line.y), *font,
                                                 vram, offset, {85, 47, 13}, 1.f,
                                                 {left, top, left + width, top + height});
                                  }, talk.cols(), talk.rows()))
            return false;
    }
    if (talk.answer_visible() &&
        (!font || !answer_gui.Draw(talk.answer_options(), talk.answer_cursor(), talk.answer_frame(),
                                   talk.answer_closing(), *font, vram, offset, talk.answer_ticks()))) return false;
    if (word_open && (!softkey || !word_entry.Draw(*softkey, vram, offset, font)))
        throw std::runtime_error("Missing room word-entry SOFTKEY renderer");
    paired_time = std::min(paired_time + 160, std::max(background.Duration(0), background.Duration(1)));
    if (door_clip) door_time = std::min(door_time + 160, background.Duration(*door_clip));
    if (television_clip) {
        const auto duration = background.Duration(*television_clip);
        if (!duration) throw std::runtime_error("Missing room television animation");
        television_time = *television_clip == 12 ? (television_time + 160) % duration
                                                : std::min(television_time + 160, duration);
    }
    if (actor.light_on())
        lamp_time = std::min(lamp_time + 160, background.Duration(11));
    // 89ACC with delta 160 and seasonal speed 30 advances one DAK time unit.
    light_time = (light_time + 1) % (24u * 216000);
    return true;
}

void PostProfile::Data::UpdateWordEntry(std::uint32_t buttons, const recovered::PackageView* font) {
    using Phase = RoomWordEntry::Phase;
    if (word_open) {
        word_entry.Step(buttons, font);
    } else {
        if (auto& initial = talk.keyboard_initial_reading(); initial)
            word_entry.OpenReading(*initial, buttons, true);
        else
            word_entry.Open(buttons);
        word_open = true;
    }
    ReceiveWordEntrySounds();
    switch (word_entry.phase()) {
    case Phase::closed_empty:
    case Phase::closed_word:
        if (conversation.snapshot().argument == 2) {
            DepartureEvent("friends-word", {{"empty",int(word_entry.written().empty())}});
            departure_events.back().text = {word_entry.written(),word_entry.reading()};
        }
        talk.FinishWordEntry(word_entry.written(), word_entry.reading());
        word_open = false;
        break;
    case Phase::timed_out:
        conversation.Boundary(0x20fb8);
        break;
    case Phase::character_input:
        conversation.Boundary(word_entry.boundary_pc());
        break;
    default:
        break;
    }
}

void PostProfile::Data::OpenTopic(const RoomEventSelection& selection) {
    if (scene==12 && conversation.snapshot().argument==0) {
        const auto place=talk.contact_location();
        DepartureEvent("contact-topic", {{"place",int(place.descriptor_id)},{"kind",int(place.kind)},
            {"location_check",int(topics.last_location_check())},{"topic",int(selection.topic_pc.value_or(0))},
            {"boundary",int(selection.boundary_pc)}});
    }
    if (selection.greeting) {
        conversation.Boundary(0x7d4cc);
    } else if (selection.topic_pc) {
        conversation.Play(*selection.topic_pc);
        const bool started = *selection.topic_pc == 0x8dba ? talk.BeginTeaching()
                           : *selection.topic_pc == 0x12f39 ? talk.BeginHomeInvitation()
                           : talk.BeginOrdinaryTopic(*selection.topic_pc, selection.queued);
        if (!started) conversation.Boundary(*selection.topic_pc);
    } else if (selection.finished) {
        if (!talk.BeginOrdinaryFarewell()) conversation.Boundary(0x899f9);
    } else {
        conversation.Boundary(selection.boundary_pc ? selection.boundary_pc : 0x89f81);
    }
}

void PostProfile::Data::UpdateConversation(unsigned minute, unsigned period, recovered::RngNativeOwner& rng) {
    if (auto pc = talk.ordinary_frontier()) {
        conversation.Boundary(*pc);
        return;
    }
    if (talk.ordinary_rejected()) {
        OpenTopic(topics.ResumeAfterRejectedTopic(conversation.snapshot().topic_pc, rng,
                                                 [&] { return talk.vocabulary().FirstContactSpecial(); }));
        return;
    }
    if (!conversation.snapshot().running || talk.ordinary_active()) return;
    if (talk.room_attention_return())
        conversation.FinishAttention();
    else if (conversation.wants_attention())
        return;
    const RoomConversationFrame frame{actor.light_on(), actor.source_flag25a(), actor.source_field50(),
                                      actor.source_field5c(), period, talk.mood(), actor.started_activity(),
                                      actor.command_ready(), talk.contact_location().kind};
    const bool roof_contact=scene==12 && conversation.snapshot().argument==0;
    conversation.Advance(frame, [&] {
        conversation_region = (scene == 12 ? roof.Background() : background).QueryActorRegion(actor.position());
        if (roof_contact) DepartureEvent("contact-region", {{"inside",int(conversation_region.inside)}});
        return conversation_region.inside;
    }, [&] {
        const bool arrived=actor.ConversationApproachDone();
        if (arrived && roof_contact) DepartureEvent("contact-arrived", {{"scene",int(scene)}});
        return arrived;
    });
    if (roof_contact && conversation.close_camera()) DepartureEvent("contact-zoom", {{"scene",int(scene)}});
    if (conversation.wants_attention()) {
        if (!talk.BeginRoomAttentionMessage()) conversation.Boundary(0x8d3e4);
    } else if (conversation.interrupt_requested()) {
        if (!actor.InterruptCommand()) conversation.Boundary(0x8d5c4);
    } else if (auto command = conversation.actor_request()) {
        if (!actor.RequestConversationControl(*command)) conversation.Boundary(conversation.actor_request_site());
        else if (roof_contact) DepartureEvent("contact-command", {{"command",int(*command)},{"site",int(conversation.actor_request_site())}});
    } else if (conversation.start_approach()) {
        if (!actor.BeginConversationApproach(scene == 12 ? roof.Background() : background, conversation_region))
            conversation.Boundary(0x8d7bf);
        else if (roof_contact) DepartureEvent("contact-approach", {{"scene",int(scene)}});
    } else if (conversation.wants_topic()) {
        const auto argument = conversation.snapshot().argument;
        if (argument == 0 || argument == 1 || argument == 3) {
            talk.SetMood(0); // Ordinary/teaching and 13219 initialize mood.
            actor.SetMood(0);
        }
        const auto reset_vocabulary = [&] { talk.vocabulary().BeginConversation(); };
        if (argument == 3 || argument == 4) {
            const bool returning = conversation.snapshot().argument == 4;
            const auto outing = topics.SelectOuting(returning);
            conversation.Play(*outing.topic_pc);
            if (!(returning ? talk.BeginOutingReturn() : talk.BeginOuting(period))) conversation.Boundary(*outing.topic_pc);
        } else if (argument == 2 || (argument >= 7 && argument <= 9)) {
            const auto action = topics.SelectFriendsAction(argument);
            conversation.Play(*action.topic_pc);
            const bool started = argument == 2 ? talk.BeginFriendsIntroduction() : talk.BeginFriendsExchange(argument, inventory);
            if (!started) conversation.Boundary(*action.topic_pc);
        } else if (conversation.snapshot().argument == 1)
            OpenTopic(topics.SelectTeaching(minute, reset_vocabulary));
        else {
            auto location=talk.contact_location();
            location.stamp_transfer_minute=[this](std::uint32_t now){ talk.StampContactLocation(now); };
            location.current_minute=[this]{ return CurrentMinute(); };
            location.current_destination_id=[this]{ return talk.contact_location().descriptor_id; };
            location.select_home_offer=[this](unsigned mode,recovered::RngNativeOwner& owner) {
                return talk.SelectHomeInvitation(mode,RoomPeriod(season,clock[3],clock[2]),owner);
            };
            OpenTopic(topics.SelectConversation(clock[5], clock[4], minute, rng,
                                                [&] { return talk.vocabulary().FirstContactSpecial(); },
                                                reset_vocabulary, std::move(location)));
        }
    }
}

bool PostProfile::Data::DrawBag(std::vector<std::uint8_t> &vram, std::size_t offset, std::uint32_t buttons,
                                const recovered::PackageView *font, recovered::RngNativeOwner *rng) {
    if (!font)
        return false;
    inventory.Step(buttons, !switching && radius == std::hypot(240.5f, 136.5f));
    ReceiveSystemSounds(inventory.TakeEffects());
    if (auto event = inventory.requested_action()) {
        const auto *item = inventory.action_item();
        if (!item)
            return false;
        const int slot = item->slot;
        const int id = item->id;
        if (event == 13) {
            if (!rng) throw std::runtime_error("Missing live placement RNG");
            if (!scene_models.PlaceGift(media, calendar_items, inventory, slot, *rng, error))
                throw std::runtime_error(error);
        } else if (!scene_models.StoreGift(calendar_items, inventory, slot, error)) {
            throw std::runtime_error(error);
        }
        inventory.CompleteAction();
        RecordTable("inventory-action-applied", inventory.SaveRow(slot),
                    {{"slot",slot},{"id",id},{"event",int(event)}});
    }
    if (inventory.closing())
        SwitchTo(View::room);
    return inventory_gui.Draw(inventory, *font, vram, offset);
}

std::optional<unsigned> PostProfile::first_activity() const {
    return data_ ? data_->activity.first_started() : std::nullopt;
}

RoomActivitySnapshot PostProfile::activity_snapshot() const {
    return data_ ? data_->activity.snapshot() : RoomActivitySnapshot{};
}

bool PostProfile::introduction_finished() const { return data_ && data_->introduction_done; }

RoomOptionsSnapshot PostProfile::options_snapshot() const {
    RoomOptionsSnapshot result;
    if (!data_) return result;
    const auto& options = data_->options;
    result.values = options.values();
    if (data_->current_view == Data::View::options) {
        result.visible = true;
        result.active = options.interactive();
        result.row = options.cursor();
        if (options.prompt().active()) result.prompt_choice = int(options.prompt().cursor());
    }
    return result;
}

RoomInventorySnapshot PostProfile::inventory_snapshot() const {
    RoomInventorySnapshot result;
    if (!data_ || data_->current_view != Data::View::inventory) return result;
    const auto& bag = data_->inventory;
    result.visible = bag.visible();
    result.list_ready = bag.list_ready();
    result.actions_ready = bag.actions_ready();
    result.filter = bag.filter();
    result.selection = bag.selection();
    result.count = bag.count();
    result.action_cursor = bag.action_cursor();
    result.discard_choice = bag.active_discard_choice();
    if (const auto* item = bag.selected_item()) {
        result.slot = item->slot;
        result.id = item->id;
        result.category = item->category;
    }
    return result;
}

RoomNotebookSnapshot PostProfile::notebook_snapshot() const {
    RoomNotebookSnapshot result;
    if (!data_) return result;
    result.menu_column = data_->menu.notebook_column();
    if (data_->current_view == Data::View::diary) {
        result.resource = 12;
        result.visible = data_->diary.visible();
        result.count = data_->diary.page_count();
        result.selection = int(data_->diary.selection());
        result.list_ready = data_->diary.ready();
        result.detail = result.list_ready && data_->diary.page_has_content();
        return result;
    }
    if (data_->current_view == Data::View::friends) {
        result.resource = 9;
        result.visible = true;
        result.detail = data_->friends.detail();
        result.list_ready = data_->friends.ready() && !result.detail;
        result.tab = int(data_->friends.tab()) + 1;
        return result;
    }
    if (data_->current_view != Data::View::notebook) return result;
    result.resource = 8;
    const auto& note = data_->notebook;
    result.visible = note.visible();
    result.list_ready = note.list_ready();
    result.delete_choice = note.active_delete_choice();
    result.tab = int(note.tab());
    result.count = note.count();
    result.selection = result.count ? int(note.selection()) : -1;
    if (note.actions_ready()) result.action_cursor = int(note.action_cursor());
    if (const auto* word = note.selected_item()) {
        result.row = int(word->row);
        result.written = word->written;
        result.reading = word->reading;
        if (note.reading_edit_active()) {
            const auto& keyboard = data_->word_entry;
            result.edit.word_row = int(word->row);
            result.edit.phase = unsigned(keyboard.phase());
            result.edit.keyboard_row = keyboard.row();
            result.edit.column = keyboard.column();
            result.edit.page = keyboard.page();
            result.edit.picking = keyboard.picking();
            result.edit.written = keyboard.written();
            result.edit.composition = keyboard.composition();
        }
    }
    return result;
}

RoomConversationSnapshot PostProfile::conversation_snapshot() const {
    if (!data_) return {};
    auto status = data_->conversation.snapshot();
    if (status.serial) {
        const auto& talk = data_->talk;
        status.word_entry_interactive = data_->word_open && data_->word_entry.phase() == RoomWordEntry::Phase::editing;
        if (talk.visible()) {
            status.message_pc = talk.message_pc();
            status.indirect_topic = talk.message_indirect_topic();
            status.message = talk.text();
            status.word_rows = talk.message_word_rows();
            status.word_operands = talk.message_word_operands();
            status.message_revealed = talk.message_revealed();
            if (status.message_revealed) status.displayed_lines = talk.lines();
        }
        if (talk.answer_interactive()) status.option_count = static_cast<unsigned>(talk.answer_options().size());
        if (status.topic_pc) status.chosen_option = talk.last_answer();
    }
    return status;
}

RoomTeachingSnapshot PostProfile::teaching_snapshot() const {
    RoomTeachingSnapshot result;
    if (!data_) return result;
    result.menu_open = data_->menu.active();
    if (result.menu_open) {
        result.menu_cursor = data_->menu.cursor();
        result.menu_column = data_->menu.column();
    }
    result.menu_boundary_pc = data_->menu.boundary_pc();
    result.menu_result = data_->last_menu_result;
    const auto& talk = data_->talk;
    result.learned_serial = talk.learned_word_serial();
    if (auto row = talk.learned_word_row()) {
        result.learned_row = int(*row);
        result.learned_written = talk.learned_word();
        result.learned_reading = talk.learned_reading();
    }
    const auto& editor = data_->word_entry;
    if (editor.is_reading_edit()) return result;
    result.word_open = data_->word_open;
    result.word_phase = unsigned(editor.phase());
    result.row = editor.row();
    result.column = editor.column();
    result.page = editor.page();
    result.picking = result.word_open && editor.picking();
    result.choosing_candidate = result.word_open && editor.choosing_candidate();
    result.written_bytes = static_cast<unsigned>(editor.written().size());
    result.reading_bytes = static_cast<unsigned>(editor.reading().size());
    result.composition_bytes = static_cast<unsigned>(editor.composition().size());
    result.written = editor.written();
    result.reading = editor.reading();
    result.composition = editor.composition();
    if (result.word_open) {
        result.candidates_known = editor.candidates_known();
        result.candidates = editor.candidate_texts();
        result.candidate_count = static_cast<unsigned>(result.candidates.size());
        if (editor.choosing_candidate()) result.candidate_index = int(editor.candidate_index());
    }
    return result;
}

bool PostProfile::changed_view() const { return data_ && data_->view_changed; }
bool PostProfile::fade_scene_music() const { return data_ && data_->fade_music; }
std::optional<int> PostProfile::new_scene_music() const { return data_ ? data_->scene_music : std::nullopt; }
const std::vector<RoomDepartureEvent>& PostProfile::departure_events() const {
    static const std::vector<RoomDepartureEvent> empty;
    return data_ ? data_->departure_events : empty;
}
int PostProfile::selected_music() const { return data_ && data_->current_view != Data::View::room ? 2 : -1; }
const std::vector<RoomSound> &PostProfile::requested_effects() const {
    static const std::vector<RoomSound> empty;
    return data_ ? data_->effects : empty;
}

const std::string &PostProfile::last_error() const {
    static const std::string empty;
    return data_ ? data_->error : empty;
}

bool PostProfile::music_requested() const { return data_ && data_->music; }

} // namespace dokodemo::gameplay
