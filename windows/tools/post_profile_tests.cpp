#include "gameplay/boot_to_display.h"
#include "gameplay/post_profile.h"
#include "gameplay/retail_save_slot.h"
#include "gameplay/retail_slot_files.h"
#include "gameplay/room_save_owners.h"
#include "gameplay/room_vocabulary.h"
#include "gameplay/room_messages.h"
#include "gameplay/room_inventory.h"
#include "gameplay/room_learned_word.h"
#include "gameplay/room_notebook_diary.h"
#include "platform/room_meeting_win32.h"
#include "core_hash.h"
#include "gameplay/roof_scene.h"
#include <cmath>
#include "gameplay/title_renderer.h"
#include "gameplay/cold_room_actor.h"
#include "gameplay/room_activity_phase.h"
#include "gameplay/profile_adoption.h"
#include "gameplay/profile_date.h"
#include "recovered/calendar_native.h"
#include "recovered/package_storage_native.h"
#include "recovered/rng_native.h"
#include <algorithm>
#include <fstream>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

using dokodemo::gameplay::PostProfile;

namespace {
constexpr std::size_t surface = 512 * 272 * 4;
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

class Directory final : public dokodemo::recovered::PackageStorageLease {
  public:
    explicit Directory(std::size_t size) : bytes(size), known(size, 1) {}
    bool Span(dokodemo::recovered::PackageStorageSpan &out, std::string &) const override {
        out = {bytes.data(),
               known.data(),
               bytes.size(),
               reinterpret_cast<std::uintptr_t>(bytes.data()),
               1,
               1,
               1,
               0,
               true,
               false};
        return true;
    }
    mutable std::vector<uint8_t> bytes, known;
};

dokodemo::recovered::PackageView RoomFont(const std::filesystem::path &game) {
    using namespace dokodemo::recovered;
    std::ifstream file(game / "assets/iso/PSP_GAME/USRDIR/data/DATA.BP", std::ios::ate | std::ios::binary);
    const auto total = file.tellg();
    std::array<uint32_t, 4> header{};
    Require(total > 16 && bool(file.seekg(0).read(reinterpret_cast<char *>(header.data()), 16)),
            "Missing packaged data directory");
    const auto length = 16ull + 16ull * header[1] + header[2];
    Require(length <= header[3] && length <= uint64_t(total), "Bad data directory size");
    auto root = std::make_shared<Directory>(std::size_t(length));
    Require(bool(file.seekg(0).read(reinterpret_cast<char *>(root->bytes.data()), length)),
            "Short data directory");
    auto entry =
        BpDirectory::Parse(PackageStorage::Attach(root, length), uint64_t(total)).Resolve("DATA.BPM");
    auto common = std::make_shared<Directory>(std::size_t(entry.size));
    Require(bool(file.seekg(entry.offset).read(reinterpret_cast<char *>(common->bytes.data()), entry.size)),
            "Short common package");
    return BpDirectory::Parse(PackageStorage::Attach(common, entry.size), entry.size).Open("FONT\\FONT.XFT");
}

struct Frame {
    uint64_t hash;
    unsigned visible;
};
Frame Visible(const std::vector<uint8_t> &vram, std::size_t offset) {
    Frame result{1469598103934665603ull, 0};
    for (unsigned y = 0; y < 272; ++y)
        for (unsigned x = 0; x < 480; ++x) {
            const auto *p = &vram[offset + 4 * (512 * y + x)];
            if (p[0] || p[1] || p[2])
                ++result.visible;
            for (unsigned c = 0; c < 3; ++c) {
                result.hash ^= p[c];
                result.hash *= 1099511628211ull;
            }
        }
    return result;
}

#include "roof_scene_test.inc"
#include "room_day_resume_test.inc"
#include "room_final_day_test.inc"
#include "room_replacement_slot_test.inc"
#include "room_meeting_test.inc"
void FirstActivityPhase() {
    using namespace dokodemo::gameplay;
    using State = RoomActivityPhase::State;
    const std::uint32_t start = 1440u * 30 + 11 * 60;
    dokodemo::recovered::RngNativeOwner rng;
    const auto generate = [&](std::uint32_t begin, std::uint32_t finish, std::uint32_t at_turn,
                              bool light = false, unsigned period = 2, int mood = 0, unsigned age = 0) {
        RoomActivityPhase phase;
        rng.ApplyInitializerTimeT32(168, 1, "Cold room activity phase");
        const RoomActivityFrame frame{period, light, mood, age, 1, 0, false, {}, false};
        const auto advance = [&] { phase.Advance(at_turn, frame, rng); };
        advance();
        Require(phase.state() == State::unloaded, "Inactive room requested an action");
        phase.Enter(begin, rng);
        advance();
        Require(phase.state() == State::introduction, "First activity preceded introduction return");
        phase.ReturnToRoom(finish, mood, rng);
        for (unsigned i = 0; i < 59; ++i) advance();
        Require(phase.state() == State::return_wait, "Source 8DE68 wait returned early");
        advance();
        return phase;
    };
    auto phase = generate(start, start, start);
    Require(phase.state() == State::select && !phase.mood_write(), "Ordinary first turn did not reach 13554");
    const RoomActivityInputs first{true, 0, 0, 1, 2, 0, false};
    RoomActivitySelection choice;
    choice.command = 371;
    choice.rows = {6, 14};
    choice.selected_row = 14;

    auto expected_rng = rng;
    const auto ordinary = [&](std::optional<unsigned> entered, bool ready) {
        phase.Advance(start, {2, false, 0, 0, 1, 0, false, entered, ready}, rng);
    };
    phase.FinishSelection(first, choice, true, rng);
    // Stale readiness, a sibling receipt, or a busy current ASC cannot
    // consume the draw. The fresh actor's 258/25E controls 13848.
    ordinary({}, true);
    ordinary(300u, true);
    Require(!phase.snapshot().started_command, "Foreign command released builtin23");
    ordinary(371u, false);
    for (unsigned i = 0; i < 93; ++i) ordinary({}, false);
    Require(phase.snapshot().started_command == 371 && phase.snapshot().serial == 1 &&
            !phase.snapshot().pause_ticks && rng.Snapshot().seed == expected_rng.Snapshot().seed,
            "Busy first actor consumed a selector/pause draw");
    ordinary({}, true);
    Require(phase.snapshot().actor_wait_released && !phase.snapshot().pause_ticks &&
            rng.Snapshot().seed == expected_rng.Snapshot().seed, "Poll consumed the next update's 2919 draw");
    ordinary({}, false);
    unsigned pause = 30 + expected_rng.NextC3234() % 31;
    Require(phase.snapshot().pause_ticks == pause && phase.snapshot().remaining_ticks == pause &&
            rng.Snapshot().seed == expected_rng.Snapshot().seed, "Wrong 2919 draw after builtin23");
    for (unsigned i = 1; i < pause; ++i) ordinary({}, true);
    Require(phase.state() == State::pause && phase.snapshot().remaining_ticks == 1,
            "13855 pause returned early");
    ordinary({}, true);
    Require(phase.state() == State::select && !phase.first_after_cleanup(), "Ordinary loop reapplied 9A79");
    const RoomActivityInputs next{false, 0, 5, 1, 2, 0, false};
    choice.effective_field58 = 5;
    phase.FinishSelection(next, choice, true, rng);
    ordinary({}, true);
    Require(phase.snapshot().serial == 2 && !phase.snapshot().started_command && !phase.snapshot().pause_ticks &&
            phase.snapshot().selected_row == 14 && phase.snapshot().eligible_rows == 2 &&
            phase.snapshot().inputs.actor_field58 == 5 && phase.snapshot().effective_field58 == 5,
            "Same-ID selection lost its own row, inputs or wait");
    ordinary(371u, true);
    Require(phase.snapshot().actor_wait_released && !phase.snapshot().pause_ticks, "Repeat resumed early");
    ordinary({}, false);
    pause = 30 + expected_rng.NextC3234() % 31;
    Require(phase.snapshot().pause_ticks == pause, "Repeat did not wait for fresh actor entry");
    for (unsigned i = 0; i < pause; ++i) ordinary({}, false);
    phase.FinishSelection(next, {}, true, rng);
    pause = 30 + expected_rng.NextC3234() % 31;
    Require(phase.state() == State::pause && phase.snapshot().serial == 3 &&
            !phase.snapshot().selected_command && !phase.snapshot().started_command &&
            phase.snapshot().pause_ticks == pause && rng.Snapshot().seed == expected_rng.Snapshot().seed,
            "Empty 13554 result failed to bypass 13848 and take one pause draw");
    for (unsigned i = 1; i < pause; ++i) ordinary({}, false);
    phase.Advance(start + 121, {2, false, 0, 0, 1, 0, false, {}, false}, rng);
    Require(phase.alternate_pc() == 0x8dea9 && phase.snapshot().serial == 3 &&
            rng.Snapshot().seed == expected_rng.Snapshot().seed,
            "Repeated dispatch ignored the live deadline or consumed an extra draw");

    phase = generate(start, start, start);
    phase.FinishSelection(first, choice, false, rng);
    Require(phase.state() == State::other_source_path && phase.alternate_pc() == 0x13573 &&
            phase.snapshot().selected_command == 371 && !phase.snapshot().started_command,
            "Unsupported ASC lost the actual request");
    phase = generate(start, start, start + 121);
    Require(phase.alternate_pc() == 0x8dea9, "Due 2868 was ignored");
    phase = generate(start, start, start + 1440);
    Require(phase.alternate_pc() == 0x1b34, "Date change bypassed daily event processing");
    phase = generate(start, start, start, true);
    Require(phase.lamp_command() == 154 && !phase.snapshot().serial, "Daylight failed to request light off");
    phase = {};
    phase.Enter(start, rng);
    phase.ReturnToRoom(start, 0, rng);
    const auto before_light = rng.Snapshot().seed;
    RoomActivityFrame locked{4, false, 0, 0, 1, 0, true, {}, false};
    for (unsigned i = 0; i < 79; ++i) phase.Advance(start, locked, rng);
    Require(!phase.lamp_command() && phase.state() == State::lamp_control && !phase.snapshot().serial &&
            rng.Snapshot().seed == before_light, "Busy GAME45 owner was interrupted");
    // The owner may have changed the actual light while 8D5D0 waited.
    // 8D82F re-reads GAME40, with the original caller's local target.
    locked.locked = false;
    locked.lamp = true;
    phase.Advance(start, locked, rng);
    Require(phase.state() == State::select && !phase.lamp_command() && rng.Snapshot().seed == before_light,
            "Light already set by its owner was requested again");
    phase = {};
    phase.Enter(start, rng);
    phase.ReturnToRoom(start, 0, rng);
    RoomActivityFrame interruptible{4, false, 0, 0, 1, 1, false, {}, true};
    for (unsigned i = 0; i < 60; ++i) phase.Advance(start, interruptible, rng);
    Require(phase.alternate_pc() == 0x8d5c1 && !phase.lamp_command(),
            "Unimplemented GAME26 interruption was replaced with a lamp request");
    phase = generate(start, start + 241, start + 241);
    Require(phase.alternate_pc() == 0x137eb, "3DF5 was incorrectly reset after a long introduction");
    phase = generate(start, start, start, false, 2, 1, 601);
    Require(phase.mood_write() == 0, "Expired 1357E mood was sent to selector");
    phase = generate(start, start, start, false, 2, 1, 600);
    Require(!phase.mood_write(), "Mood expiry did not preserve strict greater-than comparison");
}

// Enter just before a room period boundary. The introductory history is
// isolated here; both light requests and the walk back use the shipped ASCs.
void NaturalRoomLamp(const std::filesystem::path& game, bool start_lit) {
    using namespace dokodemo::gameplay;
    using State = RoomActivityPhase::State;
    const unsigned before = start_lit ? 0 : 3, after = start_lit ? 1 : 4;
    const unsigned command = start_lit ? 154 : 153;
    const std::uint32_t now = 1440u * 30 + (start_lit ? 349 : 1089);
    ColdRoomActor actor;
    std::string error;
    Require(actor.Load(game, 0, before, error), "Missing original actor for lamp change");
    for (unsigned i = 0; !actor.talk_ready() && i < 3000; ++i)
        Require(actor.Advance(now), "Cold entrance failed before lamp change");
    Require(actor.talk_ready() && actor.light_on() == start_lit && actor.Request(87),
            "Incorrect pre-change room light or pose");
    for (unsigned i = 0; !actor.command_ready() && i < 1500; ++i)
        Require(actor.Advance(now), "Farewell pose failed before lamp change");
    Require(actor.command_ready(), "Farewell pose did not release before lamp change");

    RoomActivityPhase phase;
    dokodemo::recovered::RngNativeOwner rng;
    rng.ApplyInitializerTimeT32(218, 1, "Source room lighting fixture");
    phase.Enter(now, rng);
    phase.ReturnToRoom(now, 0, rng);
    const auto seed = rng.Snapshot().seed;
    std::vector<unsigned> requested, started;
    bool source_lamp_write = false;
    for (unsigned i = 0; i < 2500 && phase.state() != State::select; ++i) {
        Require(actor.Advance(now + 1), "Actual light/walk ASC failed");
        if (actor.started_activity()) started.push_back(*actor.started_activity());
        if (actor.lamp_write()) source_lamp_write |= *actor.lamp_write() == !start_lit;
        phase.Advance(now + 1, {after, actor.light_on(), 0, 0, actor.source_field50(),
                      actor.source_field5c(), actor.source_flag25a(), actor.started_activity(),
                      actor.command_ready()}, rng);
        if (auto cmd = phase.lamp_command()) {
            Require(actor.RequestRoomControl(*cmd), "Actual room light request was not accepted");
            requested.push_back(*cmd);
        }
    }
    Require(requested == std::vector<unsigned>{command, 157} && started == requested &&
            source_lamp_write && actor.light_on() == !start_lit && phase.state() == State::select &&
            !phase.snapshot().serial && rng.Snapshot().seed == seed,
            "8D7DA did not return via the actual lamp and recenter motions");
}

// Circle inside a room whose live TV control and actor both came from ASC444.
// The action's subtype is zero, so 8D901 skips the TV-off subroutine.
void ConversationWithTelevision(const std::filesystem::path& game) {
    using namespace dokodemo::gameplay;
    ColdRoomActor actor;
    RoomMesh background;
    std::string error;
    Require(actor.Load(game, 0, 2, error) &&
            background.Load(game / "assets/game/post-profile/bg_room.bin", error, true),
            "Missing source assets for the television conversation");
    constexpr unsigned now = 45000;
    for (unsigned i = 0; i < 2000 && !actor.talk_ready(); ++i)
        Require(actor.Advance(now), "Room entrance failed");
    Require(actor.RequestActivity(444), "TV activity unavailable");
    bool television = false, tv_on = false;
    for (unsigned i = 0; i < 600 && (!tv_on || !actor.command_ready()); ++i) {
        Require(actor.Advance(now), "TV actor failed");
        if (actor.game28_write()) television = *actor.game28_write() != 0;
        if (actor.scene_pair_requested() == 0) tv_on = true;
    }
    Require(television && tv_on && actor.command_ready() && !background.ContainsActor(actor.position()),
            "ASC444 did not reach its source position by the active television");
    RoomConversationFlow flow;
    unsigned queries = 0, polls = 0;
    RoomActorRegion current{};
    const auto step = [&] {
        const RoomConversationFrame frame{actor.light_on(), actor.source_flag25a(), actor.source_field50(),
                                          actor.source_field5c(), 2, 0, actor.started_activity(), actor.command_ready()};
        flow.Advance(frame, [&] {
            ++queries;
            current = background.QueryActorRegion(actor.position());
            return current.inside;
        }, [&] { ++polls; return actor.ConversationApproachDone(); });
    };
    Require(flow.Begin(), "Missing fresh Circle invocation");
    step();
    Require(flow.wants_attention() && !flow.actor_request() && queries == 1, "Outside-region Circle skipped 8D3E4");
    flow.FinishAttention(); // the message and its inline clear have separate source tests
    step();
    Require(queries == 2 && !current.inside && flow.actor_request() == 186 && flow.wide_camera(),
            "Ordinary Circle switched off the TV or skipped the outside-region branch");
    const auto target = current.target;
    Require(actor.RequestConversationControl(186), "186 was not posted");
    bool approach = false, moved = false;
    for (unsigned i = 0; i < 300 && !flow.close_camera(); ++i) {
        const auto previous_position = actor.position();
        Require(actor.Advance(now), "Actor did not execute the room approach");
        moved |= previous_position != actor.position();
        step();
        Require(queries == 2 && target == current.target && !flow.actor_request() && !actor.game28_write() &&
                actor.scene_pair_requested() != 1, "Approach resampled its target or switched off the television");
        if (flow.start_approach()) {
            Require(!approach && !polls && actor.BeginConversationApproach(background, current),
                    "Premature poll or wrong genuine 186/17 handoff");
            approach = true;
        }
    }
    Require(approach && moved && polls > 1 && flow.close_camera() && actor.ConversationApproachDone() &&
            background.ContainsActor(actor.position()) && actor.position() == target,
            "GAME18 did not return through the actual approach to the saved centre");

}

// Test both sides of the suspended 178 request without depending on an
// incidental timing relationship between two real actors.
void ConversationPreparation(unsigned argument) {
    using namespace dokodemo::gameplay;
    RoomConversationFlow flow;
    RoomConversationFrame frame{false, false, 1, 0, 2, 0, {}, false};
    const auto step = [&] { flow.Advance(frame, [] { return true; }, [] { return false; }); };
    Require(!flow.Begin(5), "Unsupported script event argument was admitted");
    Require(flow.Begin(argument) && flow.snapshot().argument == argument, "Room input was not staged");
    Require(!flow.Begin(3), "A second outing displaced an active approach");
    step();
    Require(flow.actor_request() == 178 && !flow.wants_topic(), "Wrong ordinary actor command");
    frame.ready = true;
    frame.started = 157;
    step();
    step();
    Require(!flow.close_camera() && !flow.wants_topic(), "Another actor receipt opened the camera");
    frame.started = 178;
    frame.ready = false;
    step();
    frame.started.reset();
    step();
    Require(!flow.close_camera(), "Busy actor opened the camera");
    frame.ready = true;
    step();
    Require(!flow.close_camera(), "KSC resumed in the native wait poll");
    step();
    Require(flow.close_camera(), "Returned 178 did not request GAME44(1)");
    for (unsigned i = 1; i < 15; ++i) {
        step();
        Require(!flow.wants_topic() && !flow.close_camera(), "8D922 did not hold its 15 ticks");
    }
    step();
    Require(flow.wants_topic(), "8DF10 did not dispatch after the approach");
    flow.Play(argument == 3 ? 0x13219u : argument == 2 ? 0x10c9bu : 0x852acu);
    Require(flow.snapshot().running && !flow.snapshot().completed, "Selected topic counted as returned");
    flow.Finish();
    Require(flow.snapshot().completed && !flow.snapshot().running, "Topic return missing");
}

void ConversationPlaceLamp() {
    using namespace dokodemo::gameplay;
    for (const bool lamp : {false, true}) {
        // At home these two combinations require the lamp to change.
        RoomConversationFrame frame{lamp, false, 1, 0, lamp ? 2u : 4u, 0, {}, false};
        RoomConversationFlow home;
        Require(home.Begin(), "Home Circle did not start");
        home.Advance(frame, [] { return true; }, [] { return false; });
        Require(home.actor_request() == (lamp ? 154u : 153u) &&
                home.actor_request_site() == (lamp ? 0x8d85du : 0x8d851u),
                "Home contact lost the time-dependent lamp command");

        frame.scene = 12;
        for (const bool inside : {false, true}) {
            RoomConversationFlow roof;
            unsigned queries = 0;
            const auto step = [&] {
                roof.Advance(frame, [&] { ++queries; return inside; }, [] { return false; });
            };
            Require(roof.Begin(), "Roof Circle did not start");
            step();
            if (!inside) {
                Require(roof.wants_attention() && queries == 1, "Roof skipped outside attention");
                roof.FinishAttention();
                step();
            }
            Require(queries == 2 && roof.wide_camera() == !inside &&
                    roof.actor_request() == (inside ? 178u : 186u) &&
                    roof.actor_request_site() == (inside ? 0x8d79bu : 0x8d7b4u),
                    "Roof contact did not reach its region-selected actor command");
        }
    }
}

void ConversationInterrupt() {
    using namespace dokodemo::gameplay;
    for (bool inside : {true, false}) {
        RoomConversationFlow flow;
        RoomConversationFrame frame{false, true, inside ? 3u : 1u, 1, 2, 0, {}, false};
        auto step = [&] { flow.Advance(frame, [=] { return inside; }, [] { return false; }); };
        Require(flow.Begin(1), "Teaching conversation did not begin");
        step();
        Require(flow.wants_attention(), "Busy ASC bypassed the source attention message");
        flow.FinishAttention();
        step();
        Require(!flow.interrupt_requested() && !flow.actor_request(), "GAME45 lock was bypassed");
        frame.locked = false;
        step();
        Require(flow.interrupt_requested() && !flow.actor_request() && !flow.snapshot().boundary_pc,
                "GAME27 did not issue one GAME26 command");
        step();
        Require(!flow.interrupt_requested() && !flow.actor_request(), "Busy ASC tail was interrupted twice");
        frame.interrupt = 0;
        frame.posture = inside ? 1u : 5u;
        frame.ready = true;
        step();
        Require(!flow.actor_request() && !flow.wants_topic(), "Wait23 advanced the VM while polling");
        step();
        if (inside) {
            // Requested nil differed from the saved posture, so 8D76E
            // waits one tick before testing the new standing posture.
            Require(!flow.actor_request(), "The source posture resample happened early");
            step();
        }
        // Outside requests posture 1. Its saved 1 matches despite the change
        // during GAME26. Neither path requires a second GAME45 poll.
        Require(flow.actor_request() == (inside ? 178u : 186u) && !flow.interrupt_requested() &&
                flow.actor_request_site() == (inside ? 0x8d79bu : 0x8d7b4u),
                "Wrong command after the interrupting ASC returned");
    }

    RoomConversationFlow other_posture;
    RoomConversationFrame frame{false, false, 2, 1, 2, 0, {}, false};
    const auto tick = [&] { other_posture.Advance(frame, [] { return true; }, [] { return false; }); };
    Require(other_posture.Begin(1), "Waiting-posture conversation was not admitted");
    tick();
    Require(other_posture.wants_attention(), "Active GAME27 skipped attention");
    other_posture.FinishAttention();
    for (unsigned i = 0; i < 4; ++i) {
        tick();
        Require(!other_posture.interrupt_requested() && !other_posture.actor_request(),
                "Unspecialized posture 2 entered the interruption handler");
    }
    frame.posture = 1;
    frame.interrupt = 0;
    tick();
    Require(other_posture.actor_request() == 178, "Fresh posture did not release 8D5D0");
}

void FirstConversation(const std::filesystem::path &game, const std::array<uint32_t, 7> &calendar,
                       unsigned session_seed, bool test_following_lessons) {
    using namespace dokodemo::gameplay;
    constexpr uint32_t circle = 0x2000, back = 0x4000;
    AcceptedProfile profile{};
    Require(profile.nickname.Assign("TORO") && profile.player.Assign(test_following_lessons ? "\x82\xc9" : "AKI"), "Test profile text");
    profile.actor = 0;
    profile.gender = 2;
    profile.adopted_at = calendar;
    const auto font = RoomFont(game);
    TitleRenderer keyboard;
    std::string keyboard_error;
    Require(keyboard.LoadSoftkey(game / "assets/game/title", keyboard_error), "Missing original SOFTKEY");
    dokodemo::recovered::RngNativeOwner rng;
    rng.ApplyInitializerTimeT32(session_seed, 1, "Controlled room test session");
    PostProfile room;
    std::string error;
    if (!room.Load(game, profile, calendar, error))
        throw std::runtime_error(error);
    std::vector<uint8_t> vram(2 * 1024 * 1024);
    unsigned frame = 0, confirmations = 0, tracks = 0;
    std::size_t offset = 0;
    const auto tick = [&](uint32_t held) {
        offset = (frame++ % 2) * surface;
        if (!room.Draw(vram, offset, held, &font, &rng, &calendar, &keyboard))
            throw std::runtime_error("Room update " + std::to_string(frame) +
                                     " failed: " + room.last_error());
        for (const auto &cue : room.requested_effects())
            if (cue.bank == 5 && cue.group == 0 && cue.sequence == 4 && cue.scaled)
                ++confirmations;
        if (room.music_requested())
            ++tracks;
    };
    // Oracle SYOUKAI circle and text pixels once the prompt's clip is open.
    const auto prompt = [&]() {
        const auto *first = &vram[offset + 4 * (254 * 512 + 400)];
        const auto *second = &vram[offset + 4 * (255 * 512 + 415)];
        return first[0] == 59 && first[1] == 59 && second[0] == 254 && second[1] == 201;
    };
    do {
        tick(circle);
    } while (!prompt() && frame < 850);
    Require(prompt(), "Intro did not reach real talk prompt");
    for (unsigned i = 0; i < 5; ++i)
        tick(circle);
    Require(confirmations == 0 && tracks == 1, "Held Circle crossed the room intro");
    tick(0);
    tick(circle);
    tick(circle);
    tick(0);
    Require(confirmations == 1, "Single talk press did not produce exactly one scaled source confirm");
    bool saw_balloon = false;
    while (room.selected_music() != 2 && frame < 4500) {
        tick(0);
        if (!saw_balloon) {
            unsigned matches = 0;
            for (unsigned y = 22; y < 115; y += 2)
                for (unsigned x = 292; x < 468; x += 2) {
                    const auto *p = &vram[offset + 4 * (512 * y + x)];
                    if (p[0] > 230 && p[1] > 220 && p[2] < 215 && p[2] > 130)
                        ++matches;
                }
            saw_balloon = matches > 450;
        }
    }
    Require(saw_balloon && room.selected_music() == 2 && confirmations == 1,
            "Neutral first dialogue did not reach the awarded item");
    for (unsigned i = 0; i < 48; ++i)
        tick(0);
    Require(tracks == 2, "Inventory did not request its own track");
    tick(circle);
    for (unsigned i = 0; i < 12; ++i)
        tick(0); // SELECT frame 10 accepts the choice.
    tick(circle);
    for (unsigned i = 0; i < 12; ++i)
        tick(0); // Defer event 13 until SELECT has closed.
    tick(back);
    tick(0);
    const unsigned after_return = frame + 140;
    while (frame < after_return && tracks < 3)
        tick(0);
    Require(room.selected_music() == -1 && tracks == 3, "Selected item did not return to the room");
    // Original display 340 looks at the cabinet at this patch; once the
    // genuine camera-out request finishes, the same patch looks at the wall.
    const auto patch_red = [&]() {
        unsigned sum = 0;
        for (unsigned y = 112; y < 130; ++y)
            for (unsigned x = 40; x < 72; ++x)
                sum += vram[offset + 4 * (512 * y + x)];
        return sum / (18u * 32u);
    };
    Require(patch_red() < 100, "Close camera missing before farewell");
    const unsigned goodbye_limit = frame + 900;
    while (!room.introduction_finished() && frame < goodbye_limit)
        tick(0);
    Require(room.introduction_finished(), "Returned gift dialogue did not finish its own script path");
    for (unsigned i = 0; i < 30; ++i)
        tick(0);
    Require(patch_red() > 140, "Camera did not return to the wide room after the source farewell");
    for (unsigned i = 30; i < 59; ++i)
        tick(0);
    Require(!room.activity_snapshot().serial, "First activity ran before source 8DE68 cleared");
    tick(0);
    auto turn = room.activity_snapshot();
    Require(turn.serial == 1 && turn.inputs.first_after_cleanup && turn.selected_command &&
            !turn.started_command, "First selection was prematurely counted as executed");
    const auto command = *turn.selected_command;
    tick(0);
    Require(room.first_activity() == command && room.activity_snapshot().started_command == command,
            "First neutral request failed to enter the selected actor");
    turn = room.activity_snapshot();
    unsigned entered = 1;
    const auto limit = frame + 3000;
    const auto first = Visible(vram, offset).hash;
    bool changed = false;
    while (entered < 3 && frame < limit) {
        tick(0);
        changed |= Visible(vram, offset).hash != first;
        const auto current = room.activity_snapshot();
        Require(room.first_activity() == command, "Subsequent room turn overwrote first actor entry");
        if (current.serial != turn.serial) {
            Require(current.serial == turn.serial + 1 && !current.inputs.first_after_cleanup &&
                    !current.started_command && turn.remaining_ticks == 1 &&
                    turn.pause_ticks >= 30 && turn.pause_ticks <= 60,
                    "New selection preceded the prior pause or reused the previous start");
        }
        if (current.started_command && !turn.started_command) {
            Require(current.started_command == current.selected_command, "Wrong actor accepted new turn");
            ++entered;
        }
        turn = current;
    }
    Require(changed && entered >= 3, "Neutral room did not render three separately started activities");
    Require(!room.conversation_snapshot().serial, "Neutral room delivered an unsolicited conversation");
    const unsigned previous_turn = turn.serial;
    tick(circle);
    Require(!room.conversation_snapshot().serial, "Room VM consumed a manager post in the same update");
    tick(circle);
    Require(room.conversation_snapshot().serial == 1 && room.activity_snapshot().serial == previous_turn,
            "Fresh ordinary press lost its room owner");
    bool saw_text = false, saw_answer = false, saw_keyboard = false;
    const auto conversation_limit = frame + 3500;
    while (room.conversation_snapshot().running && frame < conversation_limit) {
        const auto current = room.conversation_snapshot();
        saw_text |= current.message_pc != 0;
        if (current.option_count) {
            saw_answer = true;
            tick(0);
            tick(circle);
        } else if (current.word_entry_interactive) {
            saw_keyboard = true;
            // Square selects the final control; the second press confirms
            // both empty buffers using the original keyboard owner.
            tick(0); tick(0x8000); tick(0); tick(0x8000);
        } else {
            tick(circle);
        }
        Require(room.conversation_snapshot().serial == 1 && room.activity_snapshot().serial == previous_turn,
                "Held input started a duplicate conversation or the suspended activity advanced");
    }
    const auto last = room.conversation_snapshot();
    std::cout << "Ordinary Circle topic 0x" << std::hex << last.topic_pc << ", boundary 0x" << last.boundary_pc
              << std::dec << ", returned " << last.completed << ", message " << saw_text
              << ", answer " << saw_answer << ", selected option " << last.chosen_option << ", keyboard " << saw_keyboard << ".\n";
    Require(last.serial == 1 && !last.running && (last.completed != bool(last.boundary_pc)),
            "Missing selected conversation return or precise source boundary");
    if (test_following_lessons) {
        Require(last.topic_pc != 0 && last.completed && saw_text && (!saw_answer || last.chosen_option == 1),
                "The selected conversation did not return after its message and applicable first answer");
        const auto resume_limit = frame + 180;
        while (room.activity_snapshot().serial == previous_turn && frame < resume_limit) {
            tick(circle);
            Require(room.conversation_snapshot().serial == 1 && !room.conversation_snapshot().running,
                    "Held Circle rearmed ordinary input after the farewell");
        }
        Require(room.activity_snapshot().serial == previous_turn + 1 && room.activity_snapshot().inputs.first_after_cleanup,
                "Returned ordinary conversation did not re-enter the source room loop");

        // The source menu's first row dispatches event (0,1). A second
        // conversation shares the actor, RNG, profile and dictionary.
        const auto tap = [&](uint32_t button) { tick(0); tick(button); tick(0); };
        const auto open_teaching = [&](unsigned serial) {
            tap(0x20);
            Require(room.teaching_snapshot().menu_open && room.teaching_snapshot().menu_cursor == 0,
                    "Fresh Right did not open the room's teaching item");
            for (unsigned i = 0; i < 10; ++i) tick(0);
            tap(circle);
            const unsigned limit = frame + 2500;
            while (!room.conversation_snapshot().word_entry_interactive && frame < limit &&
                   !room.conversation_snapshot().boundary_pc) tick(0);
            Require(room.conversation_snapshot().serial == serial && room.conversation_snapshot().argument == 1 &&
                    room.conversation_snapshot().topic_pc == 0x8dba && room.teaching_snapshot().menu_result == 0 &&
                    room.conversation_snapshot().word_entry_interactive,
                    "Accepted teaching item did not open the owned editor");
        };
        open_teaching(2);
        tap(circle);
        for (unsigned i = 0; i < 4; ++i) tick(0);
        Require(room.teaching_snapshot().picking, "Missing genuine kana group");
        tap(circle);
        Require(room.teaching_snapshot().composition_bytes == 2, "Kana selection was not composed");
        tap(0x8000); tap(0x8000);
        Require(room.teaching_snapshot().written_bytes == 2 && room.teaching_snapshot().reading_bytes == 2 &&
                !room.teaching_snapshot().learned_serial, "Editor commit was confused with dictionary acceptance");
        tap(0x8000);
        const auto end_limit = frame + 5000;
        unsigned answers = 0;
        unsigned response_pc = 0;
        while (room.conversation_snapshot().running && frame < end_limit) {
            const auto status = room.conversation_snapshot();
            if (room.teaching_snapshot().learned_serial && status.message_pc) response_pc = status.message_pc;
            const auto count = status.option_count;
            if (count) {
                if (count == 19) for (unsigned i = 0; i < 4; ++i) tap(0x40);
                tap(circle);
                ++answers;
            } else tick(0);
        }
        const auto learned = room.teaching_snapshot();
        const auto taught = room.conversation_snapshot();
        std::cout << "Room menu event (0,1) -> " << std::hex << taught.topic_pc
                  << ", boundary " << taught.boundary_pc << ", response " << response_pc << std::dec << ", learned row " << learned.learned_row
                  << ", returned " << taught.completed << ", answers " << answers << ".\n";
        Require(taught.serial == 2 && taught.completed && !taught.boundary_pc && response_pc && answers == 4,
                "The teaching script did not complete its category, reaction and farewell");
        Require(learned.learned_serial == 1 && learned.learned_row == 163 &&
                learned.learned_written == "\x82\xc8" && learned.learned_reading == "\x82\xc8",
                "Actual selected category did not accept the kana pair into the live dictionary");

        const auto resume = [&] {
            // 8D932 can return while the farewell balloon is still visible.
            // The manager consumes GAME3 after its current message owner leaves.
            const auto limit = frame + 240;
            while (room.conversation_snapshot().message_pc && frame < limit) tick(0);
            Require(!room.conversation_snapshot().message_pc, "Farewell balloon did not retire");
            tick(0);
            tick(0);
        };
        const auto kana = [&](bool second) {
            tap(circle);
            for (unsigned i = 0; i < 4; ++i) tick(0);
            if (second) tap(0x20);
            tap(circle);
        };
        const auto await_return = [&](bool categorize) {
            std::set<unsigned> messages;
            unsigned asked = 0;
            const auto until = frame + 5000;
            while (room.conversation_snapshot().running && frame < until) {
                const auto current = room.conversation_snapshot();
                if (current.message_pc) messages.insert(current.message_pc);
                if (!current.option_count) { tick(0); continue; }
                Require(categorize, "Unsolicited category prompt for existing player name");
                // Category, type, two traits, then the two memo questions.
                constexpr std::array<unsigned, 6> choices{5, 1, 3, 2, 1, 1};
                Require(asked < choices.size() && choices[asked] <= current.option_count,
                        "Unexpected word teaching question");
                for (unsigned i = 1; i < choices[asked]; ++i) tap(0x40);
                ++asked;
                tap(circle);
            }
            const auto result = room.conversation_snapshot();
            if (!result.completed || result.boundary_pc)
                throw std::runtime_error("Teaching " + std::to_string(result.serial) +
                    " did not return at source PC " + std::to_string(result.boundary_pc));
            Require(!categorize || asked == 6, "Missing the new word's category or memo confirmation");
            return messages;
        };
        resume();
        open_teaching(3);
        kana(true); // Second kana is the adopted player name: dictionary row 600 already owns it
        tap(0x8000); tap(0x8000); tap(0x8000);
        const auto known = await_return(false);
        Require(known.count(0x3fcf) && known.count(0x3fee) && room.teaching_snapshot().learned_serial == 1,
                "A known player name was learned as a new dictionary row");

        resume();
        open_teaching(4);
        kana(false);
        const std::string name="\x96\xbc", na="\x82\xc8";
        const auto initial = room.teaching_snapshot();
        Require(initial.candidate_count == 24 && initial.candidates.at(9) == name && initial.composition == na,
                "Wrong cold NJ results for the entered kana");
        tap(0x1000);
        Require(room.teaching_snapshot().choosing_candidate && room.teaching_snapshot().candidate_index == 0,
                "Triangle did not enter the source candidate list");
        tap(back);
        Require(!room.teaching_snapshot().choosing_candidate && room.teaching_snapshot().composition == na &&
                room.teaching_snapshot().written.empty() && room.teaching_snapshot().candidates == initial.candidates,
                "Candidate cancellation changed the buffers or learning order");
        tap(0x1000); tap(0x40); tap(0x40); tap(0x20);
        Require(room.teaching_snapshot().candidate_index == 9, "Original NJ movement missed the expected spelling");
        tap(circle);
        const auto committed = room.teaching_snapshot();
        Require(committed.written == name && committed.reading == na && committed.composition.empty() &&
                !committed.candidate_count && committed.learned_serial == 1,
                "NJ candidate commit did not preserve selected spelling/reading before KSC");
        tap(0x8000); tap(0x8000);
        const auto replies = await_return(true);
        const auto learned_name = room.teaching_snapshot();
        Require(learned_name.learned_serial == 2 && learned_name.learned_written == name &&
                learned_name.learned_reading == na && learned_name.learned_row == 164 &&
                replies.count(0x4557) && replies.count(0x45b0) && replies.count(0x466d),
                "Confirmed NJ word did not reach a distinct live dictionary row");
        resume();
        open_teaching(5);
        const auto empty_editor = room.teaching_snapshot();
        Require(empty_editor.written.empty() && empty_editor.reading.empty() && empty_editor.composition.empty(),
                "A fresh keyboard invocation inherited its prior edit buffers");
        kana(false);
        const auto repeat = room.teaching_snapshot();
        auto expected = initial.candidates;
        expected.erase(expected.begin() + 8, expected.begin() + 10);
        expected.insert(expected.begin(), name);
        Require(repeat.candidates_known && repeat.candidate_count == 23 &&
                repeat.candidates == expected && repeat.candidate_index == -1,
                "Second editor lost learned order or retained an excess predictive candidate");
        tap(0x1000);
        Require(room.teaching_snapshot().candidate_index == 0 && room.teaching_snapshot().candidates.front() == name,
                "Historical spelling was not the first selectable candidate after re-entry");
        tap(back);
        tap(0x8000); // Select DONE, then move to the authored cancel control.
        tap(0x80); tap(0x80); tap(circle);
        const auto cancelled = await_return(false);
        // 3F47 draws one of three empty-lesson reactions. Exactly one message site runs.
        unsigned reaction = 0;
        for (unsigned site : {0x3f59u, 0x3f76u, 0x3f95u}) {
            if (!cancelled.count(site)) continue;
            Require(!reaction, "Cancelled lesson emitted multiple exclusive reactions");
            reaction = site;
        }
        const auto after_cancel = room.teaching_snapshot();
        Require(reaction && after_cancel.learned_serial == learned_name.learned_serial &&
                after_cancel.learned_row == learned_name.learned_row &&
                after_cancel.learned_written == learned_name.learned_written &&
                after_cancel.learned_reading == learned_name.learned_reading,
                "Cancelled lesson missed its reaction or changed the accepted dictionary entry");
        std::cout << "Known-name refusal 3FCF/3FEE; NJ accepted candidate 9 -> row "
                  << learned_name.learned_row << "; six answers through 466D, 23 warm candidates and cancel 0x"
                  << std::hex << reaction << std::dec << ".\n";

    }
    std::cout << "First neutral ASC " << command << ", last of three " << *turn.started_command << ". Held-input, real Circle, neutral dialogue, decorate, return and goodbye passed in " << frame
              << " room updates.\n";
}

void CheckRoboHomeMenus(PostProfile& room,const dokodemo::recovered::PackageView& font,
                        dokodemo::recovered::RngNativeOwner& rng,std::vector<uint8_t>& vram,
                        dokodemo::gameplay::RetailSaveProfile::Calendar calendar,unsigned frame) {
    using namespace dokodemo::gameplay;
    constexpr unsigned circle=0x2000, cross=0x4000, up=0x10, right=0x20, down=0x40;
    unsigned placed=0,stored=0;
    bool diary_stamped=false;
    const auto step=[&](unsigned held=0) {
        const auto offset=(frame++%2)*surface;
        if(!room.Draw(vram,offset,held,&font,&rng,&calendar))
            throw std::runtime_error("Robo home menu at update "+std::to_string(frame)+": "+room.last_error());
        for(const auto& event:room.departure_events()) {
            const std::string_view name=event.name;
            if(name=="inventory-action-applied") {
                const auto action=std::find_if(event.values.begin(),event.values.end(),
                    [](const auto& field){return std::string_view(field.first)=="event";});
                Require(action!=event.values.end() && event.text.size()==1,"Missing inventory action receipt");
                const auto& raw=event.text.front();
                std::size_t consumed=0;
                const auto row=DecodeKscSaveGraph(reinterpret_cast<const uint8_t*>(raw.data()),raw.size(),consumed);
                Require(consumed==raw.size() && row.children.size()==10,"Wrong Robo gift graph shape");
                if(action->second==13) {
                    Require(row.children[6].word>>28==2 && row.children[7].word>>28==2,
                            "Robo gift has no mounted scene model");
                    ++placed;
                } else if(action->second==14) {
                    Require(!row.children[6].word && !row.children[7].word,
                            "Stored Robo gift retains its scene model");
                    ++stored;
                }
            } else if(name=="diary-page") {
                diary_stamped=std::any_of(event.values.begin(),event.values.end(),[](const auto& field) {
                    return std::string_view(field.first)=="recorded" && field.second==1;
                });
            }
        }
    };
    const auto rest=[&](unsigned ticks) { while(ticks--) step(); };
    const auto key=[&](unsigned button) { step(); step(button); step(); };
    const auto until=[&](const auto& ready,const char* error) {
        for(unsigned ticks=0;ticks<190 && !ready();++ticks) step();
        Require(ready(),error);
    };
    const auto open_menu=[&] {
        rest(40);
        key(right);
        until([&]{return room.teaching_snapshot().menu_open;},"Robo home menu did not open");
        rest(22);
    };
    const auto open_bag=[&] {
        open_menu(); key(up); key(up);
        Require(room.teaching_snapshot().menu_cursor==5,"Wrong Robo inventory row");
        key(circle);
        until([&]{return room.inventory_snapshot().list_ready;},"Robo inventory did not open");
    };
    const auto notebook=[&](int column,int resource) {
        open_menu(); key(down); key(down);
        Require(room.teaching_snapshot().menu_cursor==3,"Wrong Robo notebook category");
        key(circle); rest(24);
        for(int i=0;i<column;++i) key(down);
        Require(room.notebook_snapshot().menu_column==column,"Wrong Robo notebook submenu row");
        key(circle);
        until([&]{const auto page=room.notebook_snapshot();return page.resource==resource && page.list_ready;},
              "Selected Robo notebook page did not open");
    };
    const auto open_options=[&] {
        open_menu(); key(up);
        Require(room.teaching_snapshot().menu_cursor==6,"Wrong Robo System menu row");
        key(circle); rest(24);
        key(circle);
    };
    open_bag();
    const auto first_gift=room.inventory_snapshot();
    Require(first_gift.count==1 && first_gift.category==1 && first_gift.slot==0,"Robo gift missing from home menu");
    key(circle);
    until([&]{return room.inventory_snapshot().actions_ready;},"Robo gift actions did not open");
    key(circle);
    until([&]{return placed==1 && room.inventory_snapshot().category==0;},"Robo gift was not placed");
    key(cross);
    until([&]{return !room.inventory_snapshot().visible;},"Placed Robo gift did not return to the room");
    open_bag();
    Require(room.inventory_snapshot().id==first_gift.id && room.inventory_snapshot().category==0,
            "Robo gift placement was not retained");
    key(circle);
    until([&]{return room.inventory_snapshot().actions_ready;},"Robo gift actions could not reopen");
    key(down); key(circle);
    until([&]{return stored==1 && room.inventory_snapshot().category==1;},"Robo gift was not stored");
    key(cross);
    until([&]{return !room.inventory_snapshot().visible;},"Stored Robo gift did not return to the room");
    std::cout<<"Robo gift "<<first_gift.id<<" placed, rendered in the room and stored\n";

    notebook(0,9);
    key(cross);
    until([&]{return !room.notebook_snapshot().visible;},"Robo profile card did not close");
    notebook(1,8);
    Require(room.notebook_snapshot().count==0,"A cold Robo vocabulary showed unexpected taught words");
    key(cross);
    until([&]{return !room.notebook_snapshot().visible;},"Robo word memo did not close");
    notebook(2,12);
    Require(diary_stamped && !room.notebook_snapshot().detail,"Robo diary was missing or revealed too soon");
    key(cross);
    until([&]{return !room.notebook_snapshot().visible;},"Robo diary did not close");

    open_options();
    until([&]{return room.options_snapshot().active;},"Robo Options did not open");
    const auto initial=room.options_snapshot().values;
    key(right); key(cross);
    until([&]{return !room.options_snapshot().visible;},"Robo Options Cancel did not close");
    Require(room.options_snapshot().values==initial,"Robo Options Cancel lost the original values");
    open_options();
    until([&]{return room.options_snapshot().active;},"Robo Options did not reopen");
    key(right); key(circle);
    until([&]{return room.options_snapshot().prompt_choice==0;},"Robo Options did not show apply confirmation");
    rest(24); key(circle);
    until([&]{return !room.options_snapshot().visible;},"Robo Options Yes did not close");
    auto changed=initial;
    ++changed[0];
    Require(room.options_snapshot().values==changed,"Robo Options change did not survive room return");
    const auto serial=room.activity_snapshot().serial;
    for(unsigned ticks=0;ticks<1800 && room.activity_snapshot().serial==serial;++ticks) step();
    Require(room.activity_snapshot().serial>serial,"Room menu did not resume Robo's activity loop");

    // The same diary record becomes readable at the actual room owner's
    // evening threshold. This changes the test calendar only.
    calendar[3]=19;
    notebook(2,12);
    Require(diary_stamped && room.notebook_snapshot().detail,"Robo diary did not reveal its first-day record");
    const auto offset=((frame-1)%2)*surface;
    Require(Visible(vram,offset).visible>5000,"Robo's readable diary did not render");
    key(cross);
    until([&]{return !room.notebook_snapshot().visible;},"Evening Robo diary did not close");
    std::cout<<"Robo profile card, word memo, hidden and revealed diary, Options and room return passed\n";
}

void CheckRoboDatedContact(PostProfile& room,const dokodemo::recovered::PackageView& font,
                           dokodemo::recovered::RngNativeOwner& rng,std::vector<uint8_t>& vram,
                           std::array<uint32_t,7> calendar,unsigned answer,
                           const dokodemo::gameplay::TitleRenderer& keyboard,
                           bool ordinary=false,bool learn=false) {
    constexpr unsigned circle=0x2000,down=0x40,right=0x20;
    const bool july=calendar[5]==7;
    const unsigned topic=ordinary?0x5bdb8:july?0x5cc52:0x5c7dc;
    if (ordinary) {
        calendar[2]+=5;
        // First greeting draw, then 57/91/93 and rank 59 in the cold F6 list.
        rng.ImportCurrentState64(319,1,"Controlled Robo ordinary contact regression");
    }
    const unsigned initial_serial=room.activity_snapshot().serial;
    unsigned frame=0,previous=0;
    auto step=[&](unsigned buttons) {
        if(!room.Draw(vram,(++frame%2)*surface,buttons,&font,&rng,&calendar,&keyboard))
            throw std::runtime_error("Robo contact update "+std::to_string(frame)+": "+room.last_error());
        previous=buttons;
        return room.conversation_snapshot();
    };
    step(circle);
    bool selected=false,answered=false,moved=false,real_indirect=false;
    std::set<unsigned> sites;
    while(frame<3000) {
        unsigned buttons=0;
        const auto before=room.conversation_snapshot();
        if(before.option_count && !answered && !previous) {
            Require(before.option_count==2,"Unexpected Robo calendar offer");
            if(answer==2 && !moved) {
                moved=true;
                buttons=down;
            } else {
                answered=true;
                buttons=circle;
            }
        }
        const auto contact=step(buttons);
        if(contact.boundary_pc) {
            std::ostringstream error;
            error<<"Unfinished Robo topic "<<std::hex<<contact.topic_pc<<" at "<<contact.boundary_pc;
            throw std::runtime_error(error.str());
        }
        selected|=contact.topic_pc==topic;
        real_indirect|=contact.indirect_topic==0x5105f && contact.message_pc==0x510a2;
        if(contact.message_pc && contact.message_revealed) sites.insert(contact.message_pc);
        const auto later=room.activity_snapshot();
        if(contact.completed && later.serial>initial_serial && later.started_command) break;
    }
    const auto contact=room.conversation_snapshot();
    Require(frame<3000 && contact.completed && selected && answered && contact.chosen_option==int(answer),
            "Robo did not complete its topic, answer and ambient restart");
    if(ordinary) {
        Require(sites.count(0x5bde2) && real_indirect &&
                sites.count(0x510a2) && sites.count(0x510e3) && sites.count(0x5110a),
                "Robo did not resume the greeting into the real ordinary script");
    } else {
        Require(sites.count(0x5c2ba) && sites.count(july?0x5cc63:0x5c7ed) &&
                sites.count(july?0x5cc88:0x5c812),"Robo did not show the owning dated topic messages");
    }
    Require(sites.count(0x5bea5)||sites.count(0x5bec3)||sites.count(0x5beec)||
            sites.count(0x5bf03)||sites.count(0x5bf1d),"Robo's farewell was not shown");
    std::cout<<(ordinary?"Robo ordinary topic 5105f via ":"Robo dated topic ")<<std::hex<<topic<<std::dec<<", answer "<<answer
             <<", own farewell and restarted ASC "<<*room.activity_snapshot().started_command<<'\n';

    // Wait for the real activity return. The removed date must stay removed and
    // the next source draw must reach the lesson through the ordinary wrapper.
    if (july || ordinary) return;
    const unsigned second_limit=frame+6000;
    while(!room.activity_snapshot().pause_ticks && frame<second_limit) step(0);
    Require(frame<second_limit,"Robo's resumed activity did not release");
    const auto initial_turn=room.activity_snapshot().serial;
    step(circle);
    const auto tap=[&](unsigned input) { step(0); step(input); step(0); };
    bool editor_used=false, saw_owned_lesson=false;
    unsigned prompts=0;
    while(frame<second_limit) {
        const auto lesson=room.conversation_snapshot();
        saw_owned_lesson|=lesson.indirect_topic==0x346b4;
        if(lesson.boundary_pc) {
            std::ostringstream error;
            error<<"Robo lesson frontier "<<std::hex<<lesson.boundary_pc;
            throw std::runtime_error(error.str());
        }
        if(lesson.word_entry_interactive && !editor_used) {
            editor_used=true;
            if(learn) {
                tap(circle);
                for(unsigned i=0;i<4;++i) step(0);
                Require(room.teaching_snapshot().picking,"Robo did not open the real kana group");
                tap(circle);
                tap(0x8000); tap(0x8000);
                Require(room.teaching_snapshot().written_bytes==2 &&
                        room.teaching_snapshot().reading_bytes==2 && !room.teaching_snapshot().learned_serial,
                        "Robo editor did not keep the unaccepted word separate");
            }
            tap(0x8000);
            if(!learn) tap(0x8000);
        } else if(lesson.option_count) {
            tap(circle);
            ++prompts;
        } else step(0);
        const auto turn=room.activity_snapshot();
        if(lesson.completed && turn.serial>initial_turn && turn.started_command) break;
    }
    const auto lesson=room.conversation_snapshot();
    const auto record=room.teaching_snapshot();
    Require(frame<second_limit && editor_used && saw_owned_lesson && prompts && lesson.serial==contact.serial+1 &&
            lesson.completed && lesson.topic_pc==0x5bdb8,
            "Robo's second contact did not finish the source lesson and restart activity");
    Require(learn ? record.learned_serial==1 && record.learned_row==163 &&
                    record.learned_written.size()==2 && record.learned_reading.size()==2
                  : record.learned_serial==0,
            "Robo committed the wrong dictionary result");
    if (learn) {
        const unsigned recall_limit=frame+6000;
        while(!room.activity_snapshot().pause_ticks && frame<recall_limit) step(0);
        Require(frame<recall_limit,"Robo did not resume after registering the lesson");
        const auto before_recall=room.activity_snapshot().serial;
        // A non-full source queue admits the first roll 18 and draws its only slot.
        rng.ImportCurrentState64(2,1,"Controlled Robo queued lesson regression");
        step(circle);
        bool queued=false,reopened_editor=false,used_word=false;
        unsigned questions=0;
        while(frame<recall_limit) {
            const auto status=room.conversation_snapshot();
            if(status.boundary_pc) {
                std::ostringstream error;
                error<<"Robo queued lesson frontier "<<std::hex<<status.boundary_pc;
                throw std::runtime_error(error.str());
            }
            if(status.serial==lesson.serial+1) {
                queued|=status.topic_pc==0x346b4;
                reopened_editor|=status.word_entry_interactive;
                used_word|=std::any_of(status.word_operands.begin(),status.word_operands.end(),
                    [](const auto& operand){return operand.row==163;});
                if(status.option_count) { tap(circle); ++questions; }
                else step(0);
                const auto later=room.activity_snapshot();
                if(status.completed && later.serial>before_recall && later.started_command) break;
            } else step(0);
        }
        Require(frame<recall_limit && queued && !reopened_editor && used_word && questions==5 &&
                room.teaching_snapshot().learned_serial==1,
                "Queued topic did not return through its own questions or learned the same word twice");
        std::cout<<"Robo queued 346B4, "<<questions<<" answers, learned spelling in dialogue "<<used_word<<'\n';
    }
    step(0);
    step(right);
    for(unsigned i=0;i<60 && !room.teaching_snapshot().menu_open;++i) step(0);
    Require(room.teaching_snapshot().menu_open,"Robo lesson kept the room input owner");
    for(unsigned i=0;i<22;++i) step(0);
    tap(down); tap(down);
    Require(room.teaching_snapshot().menu_cursor==3,"Robo lesson missed the notebook category");
    tap(circle);
    for(unsigned i=0;i<24;++i) step(0);
    tap(down);
    Require(room.notebook_snapshot().menu_column==1,"Robo lesson missed the word memo");
    tap(circle);
    for(unsigned i=0;i<180 && !room.notebook_snapshot().list_ready;++i) step(0);
    Require(room.notebook_snapshot().resource==8 && room.notebook_snapshot().list_ready,"Robo word memo did not open");
    tap(0x80); // Date tab contains every learned row regardless of its reading group.
    const auto memo=room.notebook_snapshot();
    Require(memo.tab==13 && (learn ? memo.count==1 && memo.row==163 &&
                memo.written==record.learned_written && memo.reading==record.learned_reading : memo.count==0),
            "Robo word memo disagreed with the completed lesson");
    tap(0x4000);
    for (unsigned tick=0;tick<120 && room.notebook_snapshot().visible;++tick) step(0);
    Require(!room.notebook_snapshot().visible,"Robo word memo did not return to the room");
    std::cout<<"Robo 346B4, editor "<<(learn?"accepted row 163":"cancelled")
             <<", "<<prompts<<" prompts, activity and word memo verified\n";
}

enum class RoboMenuLesson { cancel, film, robot };

void CheckRoboMenuLesson(PostProfile& room,const dokodemo::recovered::PackageView& font,
                         dokodemo::recovered::RngNativeOwner& rng,std::vector<uint8_t>& vram,
                         const std::array<uint32_t,7>& calendar,
                         const dokodemo::gameplay::TitleRenderer& keyboard,RoboMenuLesson lesson) {
    constexpr unsigned circle=0x2000, right=0x20, down=0x40, square=0x8000;
    const auto initial_activity=room.activity_snapshot().serial;
    const bool cancel=lesson==RoboMenuLesson::cancel;
    unsigned frame=0;
    bool farewell=false;
    auto step=[&](unsigned buttons) {
        if(!room.Draw(vram,(++frame%2)*surface,buttons,&font,&rng,&calendar,&keyboard))
            throw std::runtime_error("Robo menu lesson update "+std::to_string(frame)+": "+room.last_error());
        const auto status=room.conversation_snapshot();
        if(status.message_revealed)
            farewell|=status.message_pc==0x5bea5 || status.message_pc==0x5bec3 || status.message_pc==0x5beec ||
                      status.message_pc==0x5bf03 || status.message_pc==0x5bf1d;
    };
    const auto tap=[&](unsigned input) { step(0); step(input); step(0); };
    const auto rest=[&](unsigned ticks) { for(unsigned i=0;i<ticks;++i) step(0); };
    tap(right); rest(22);
    Require(room.teaching_snapshot().menu_open && room.teaching_snapshot().menu_cursor==0,
            "Robo room menu did not offer a separate lesson");
    tap(circle);

    bool editor=false,category=false,resumed=false;
    unsigned extra_questions=0;
    std::string written,reading;
    for(;frame<6000;) {
        const auto status=room.conversation_snapshot();
        if(status.boundary_pc) {
            std::ostringstream reason;
            reason<<"Separate Robo lesson frontier "<<std::hex<<status.boundary_pc;
            throw std::runtime_error(reason.str());
        }
        if(status.word_entry_interactive && !editor) {
            Require(status.argument==1 && status.topic_pc==0x8e05 && room.teaching_snapshot().menu_result==0,
                    "Robo editor belongs to the wrong room action");
            editor=true;
            if(!cancel) {
                tap(circle); rest(4);
                Require(room.teaching_snapshot().picking,"Robo menu lesson did not show the kana group");
                tap(circle);
                tap(square); tap(square);
                const auto draft=room.teaching_snapshot();
                written=draft.written; reading=draft.reading;
                Require(written.size()==2 && reading.size()==2 && !draft.learned_serial,
                        "Robo editor committed the word before the lesson accepted it");
            }
            tap(square);
            if(cancel) tap(square);
        } else if(status.option_count==19) {
            Require(!category,"Unexpected repeated category selection");
            category=true;
            const auto choice=lesson==RoboMenuLesson::robot?5u:11u;
            for(unsigned i=1;i<choice;++i) tap(down);
            tap(circle);
        } else if(status.option_count) {
            tap(circle);
            ++extra_questions;
        } else step(0);
        const auto activity=room.activity_snapshot();
        if(status.completed && activity.serial>initial_activity && activity.started_command) {
            resumed=true;
            break;
        }
    }
    const auto record=room.teaching_snapshot();
    std::cout<<"Robo lesson result category="<<category
             <<" farewell="<<farewell<<" serial="<<record.learned_serial
             <<" row="<<record.learned_row<<" written="<<record.learned_written.size()
             <<" reading="<<record.learned_reading.size()<<" followups="<<extra_questions<<'\n';
    Require(editor && resumed,"Robo menu lesson did not return from its editor and start another activity");
    Require(cancel ? !category && !extra_questions && !farewell && !record.learned_serial
                   : category && extra_questions==3 && farewell && record.learned_serial==1 &&
                     record.learned_row==163 && record.learned_written==written && record.learned_reading==reading,
            "Wrong cancellation or learned word result from separate Robo lesson");
    tap(right); rest(22); tap(down); tap(down);
    Require(room.teaching_snapshot().menu_cursor==3,"Robo menu lesson did not return control to the room");
    tap(circle); rest(24); tap(down); tap(circle);
    for(unsigned i=0;i<180 && !room.notebook_snapshot().list_ready;++i) step(0);
    Require(room.notebook_snapshot().resource==8 && room.notebook_snapshot().list_ready,
            "Robo menu lesson did not reopen the word memo");
    tap(0x80);
    const auto memo=room.notebook_snapshot();
    Require(memo.tab==13 && (cancel ? memo.count==0 :
            memo.count==1 && memo.row==163 && memo.written==record.learned_written && memo.reading==record.learned_reading),
            "Robo memo disagreed with the result of its separate lesson");
    tap(0x4000);
    for (unsigned i=0;i<120 && room.notebook_snapshot().visible;++i) step(0);
    Require(!room.notebook_snapshot().visible,"Robo memo did not return to the room");
    std::cout<<"Robo 8E05 separate lesson "<<(cancel?"cancelled":lesson==RoboMenuLesson::film?"film":"robot")
             <<", "<<extra_questions<<" follow-up questions, activity and memo verified\n";
}

enum class RoboSlotCheck { none, save, penultimate, final_day, contact_save };

void CheckRoboPenultimate(const std::filesystem::path& game,
                          const dokodemo::gameplay::RetailUtilityContents& source,
                          dokodemo::gameplay::RetailSaveProfile::Calendar calendar,
                          RoboSlotCheck check) {
    using namespace dokodemo::gameplay;
    const auto slot=game/"savedata"/"UCJS10002000";
    const auto day_one=ReadRetailSlot(slot);
    const auto original_page=source.profile.FirstDayDiary();
    auto input=source;
    auto owners=RoomSaveOwners::FromActorGraph(source.actor);
    owners.day_words[0].children.at(0).word=0x2000000c;
    input.actor=owners.ToActorGraph();
    ++calendar[4];
    calendar[3]=14; calendar[2]=53;
    enum class Start { cold_circle, live_circle, deadline };
    for (const auto start : {Start::cold_circle, Start::live_circle, Start::deadline}) {
        const bool live=start==Start::live_circle;
        const bool expire=start==Start::deadline;
        auto start_date=calendar;
        if (live) {
            --start_date[4];
            start_date[3]=23; start_date[2]=59; start_date[1]=59;
        }
        ResumeDayRoom run(game,input,start_date);
        if (live) {
            run.Start(12,12);
            ++run.calendar[4];
            run.calendar[3]=0; run.calendar[2]=0; run.calendar[1]=0;
        }
        const auto first_serial=run.room.activity_snapshot().serial;
        const auto resumed=[&] {
            const auto activity=run.room.activity_snapshot();
            return activity.serial>first_serial && activity.started_command.has_value();
        };
        unsigned buttons=0, offers=0, answers=0, returns=0, nav_on=0, nav_off=0, cue=0, confirms=0;
        std::vector<int> actors, primary, secondary;
        std::set<unsigned> messages;
        for (unsigned tick=0;tick<5600 && !(returns && (expire || resumed()));++tick) {
            const unsigned held=std::exchange(buttons,0u);
            run.Step(held);
            const auto talk=run.room.conversation_snapshot();
            if(talk.message_pc && talk.message_revealed) messages.insert(talk.message_pc);
            for(const auto& effect:run.room.requested_effects()) {
                if(effect.bank==5 && effect.group==1 && effect.sequence==8 && effect.volume==96) ++cue;
                if(!answers && effect.bank==5 && effect.group==0 && effect.sequence==4 && effect.volume==128) ++confirms;
            }
            for(const auto& event:run.room.departure_events()) {
                const std::string_view name=event.name;
                if(name=="scheduled-room-event" || name=="scheduled-room-return") {
                    Require(event.values[0].second==2 && event.values[1].second==13,"Wrong Robo scheduled day");
                    if(name=="scheduled-room-return") ++returns;
                } else if(name=="scheduled-navigation") {
                    if(event.values[0].second) ++nav_on;
                    else ++nav_off;
                } else if(name=="scheduled-actor-selection") {
                    actors.push_back(event.values[0].second);
                } else if(name=="scheduled-actor-command") {
                    const int command=event.values[0].second;
                    (event.values[1].second?secondary:primary).push_back(command);
                    if(command==79 && !answers) {
                        ++offers;
                        if(expire && offers==1) ++run.calendar[3];
                    }
                } else if(name=="scheduled-room-choice") {
                    Require(event.values[0].second==2,"Robo scheduled question differs");
                    ++answers;
                }
            }
            const bool invitation=!expire && offers && nav_on>nav_off && !answers && tick%4==0;
            if (!held && (talk.option_count || invitation)) buttons=0x2000;
        }
        std::cout<<"Robo penultimate "<<(live?"live Circle":expire?"deadline":"cold Circle")
                 <<" offers "<<offers<<" nav "
                 <<nav_on<<" / "<<nav_off<<" questions "<<answers<<" returns "<<returns<<" cues "<<cue<<" / "<<confirms
                 <<", ASC "<<run.room.activity_snapshot().started_command.value_or(0)
                 <<", updates "<<run.frames<<" switches";
        for(int i:actors) std::cout<<" "<<i;
        std::cout<<", secondary";
        for(int i:secondary) std::cout<<" "<<i;
        std::cout<<", primary";
        for(int i:primary) std::cout<<" "<<i;
        std::cout<<std::endl;
        const bool farewell=messages.count(0x5bea5) || messages.count(0x5bec3) || messages.count(0x5beec) ||
                            messages.count(0x5bf03) || messages.count(0x5bf1d);
        Require(run.previous==12 && run.day==(live?12:13) && run.clears==1 && run.scheduled==1 && returns==1 &&
                run.room.introduction_finished() && (expire || resumed()) && answers==5 &&
                offers>0 && nav_on==1 && nav_off==1 && cue==1 &&
                messages.count(0x67161) && messages.count(0x5b084) && messages.count(0x5b3fe) &&
                (expire?confirms==0:confirms>0) &&
                actors==std::vector<int>{1,0,1,0,1,0,1,0} && secondary==std::vector<int>{280,39,281,282} &&
                std::find(primary.begin(),primary.end(),276)!=primary.end() &&
                (farewell || primary.back()==74),"Robo's event, partner motions or actual return are incomplete");
        if (start==Start::cold_circle) Require(ReadRetailSlot(slot)==day_one,"Playback modified the slot before Save");
        run.OpenSection(1,8);
        run.Key(0x80);
        const auto memo=run.room.notebook_snapshot();
        const auto& lesson=owners.dictionary.children.at(0).children.at(163).children.at(0).children;
        Require(memo.tab==13 && memo.count==1 && memo.row==163 &&
                memo.written==lesson.at(1).text && memo.reading==lesson.at(2).text,"Robo lost the existing lesson");
        run.CloseSection();
        run.Save();
        const auto saved=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
        const auto after=RoomSaveOwners::FromActorGraph(saved.actor);
        const auto page=saved.profile.FirstDayDiary();
        Require(after.day_words[0].children.at(0).word==0x2000000d &&
                after.progress_flags[0].children.at(0).word==(owners.progress_flags[0].children.at(0).word | 2) &&
                after.contact_count.children.at(0).word==owners.contact_count.children.at(0).word+1 &&
                page && original_page && page->text==original_page->text && page->date==original_page->date,
                "Protected Robo save lost the penultimate-day flag, day or diary");
        ResumeDayRoom reloaded(game,saved,run.calendar);
        reloaded.Start(13,13);
        Require(reloaded.scheduled==0,"Robo repeated the completed event after reload");
        if (check==RoboSlotCheck::final_day) return;
    }
    std::cout<<"Robo day 13 protected save, memo retained and no event replay for all three routes\n";
}

void ReportContactWord(const char* checkpoint,std::string_view written,std::string_view reading) {
    const auto hash=[](std::string_view value) {
        return dokodemo::core::sha256_hex(reinterpret_cast<const std::uint8_t*>(value.data()),value.size());
    };
    std::cout<<checkpoint<<" row=163 written="<<hash(written)<<" reading="<<hash(reading)<<'\n';
}

void CheckColdRoboWord(const std::filesystem::path& game,
                       dokodemo::gameplay::RetailSaveProfile::Calendar calendar,
                       std::string_view written,std::string_view reading,
                       const std::vector<std::uint8_t>& request) {
    using namespace dokodemo::gameplay;
    std::array<uint8_t,16> date{};
    for(unsigned i=0;i<6;++i) {
        date[2*i]=uint8_t(calendar[6-i]);
        date[2*i+1]=uint8_t(calendar[6-i]>>8);
    }
    BootToDisplay boot;
    Require(boot.Initialize() && boot.InitializeClock(date,168) &&
            boot.MountDataArchive(game/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP") &&
            boot.PrepareTitle() && boot.Title().title_complete(),"Robo slot did not select cold room entry");
    unsigned restored=0;
    const auto advance=[&](unsigned buttons=0) {
        if(!boot.AdvanceTitle(buttons))
            throw std::runtime_error(boot.SaveReadBoundary()?boot.SaveReadBoundary():"Robo restart failed");
        for(const auto& event:boot.DepartureEvents()) {
            const std::string_view name=event.name;
            if(name=="room-load-restored") ++restored;
            Require(name!="gift-received" && name!="diary-created","Robo replayed introductory rewards");
        }
    };
    for(unsigned i=0;i<1800 && !boot.RoomActivity().started_command;++i) advance();
    Require(restored==1 && boot.RoomActivity().started_command,"Cold Robo did not start an owning room activity");
    const auto wait=[&](unsigned ticks) { while(ticks--) advance(); };
    const auto press=[&](unsigned buttons) { advance(); advance(buttons); wait(16); };
    press(0x20); wait(22); press(0x40); press(0x40);
    Require(boot.RoomTeaching().menu_cursor==3,"Reloaded Robo cannot open the notebook category");
    press(0x2000); wait(24); press(0x40); press(0x2000);
    for(unsigned i=0;i<180 && !boot.Notebook().list_ready;++i) advance();
    Require(boot.Notebook().resource==8 && boot.Notebook().list_ready,"Reloaded Robo memo missing");
    press(0x80);
    const auto memo=boot.Notebook();
    Require(memo.tab==13 && memo.count==1 && memo.row==163 && memo.written==written && memo.reading==reading &&
            ReadRetailSlot(game/"savedata"/"UCJS10002000")==request,
            "Cold Robo did not restore the exact word from its unchanged slot");
    ReportContactWord("loaded_contact_word",memo.written,memo.reading);
}

int CheckColdRoboContactSlot(const std::filesystem::path& game) {
    using namespace dokodemo::gameplay;
    const auto request=ReadRetailSlot(game/"savedata"/"UCJS10002000");
    const auto saved=DecodeRetailUtilityRequest(request,RetailSaveProfile{});
    const auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    const auto& word=owners.dictionary.children.at(0).children.at(163).children.at(0).children;
    const auto profile=saved.profile.ReadProfile();
    Require(profile.actor==1 && profile.player.text()=="AKI" &&
            !word.at(1).text.empty() && !word.at(2).text.empty(),"Missing saved Robo contact word");
    CheckColdRoboWord(game,saved.profile.SavedAt(),word.at(1).text,word.at(2).text,request);
    return 0;
}

void CheckRoboSlot(const std::filesystem::path& game,PostProfile& room,
                   const dokodemo::recovered::PackageView& font,dokodemo::recovered::RngNativeOwner& rng,
                   const dokodemo::gameplay::TitleRenderer& keyboard,std::vector<uint8_t>& vram,
                   dokodemo::gameplay::RetailSaveProfile::Calendar calendar,RoboSlotCheck check) {
    using namespace dokodemo::gameplay;
    const auto slot=game/"savedata"/"UCJS10002000";
    Require(!std::filesystem::exists(slot.parent_path()),"Robo slot test requires a fresh private installation");
    const auto lesson=room.teaching_snapshot();
    unsigned frame=0,saves=0;
    const auto step=[&](unsigned buttons=0) {
        if(!room.Draw(vram,(++frame%2)*surface,buttons,&font,&rng,&calendar,&keyboard))
            throw std::runtime_error(room.last_error());
        for(const auto& event:room.departure_events()) {
            const std::string_view name=event.name;
            if(name=="system-save-written") ++saves;
            if(name=="system-save-unavailable")
                throw std::runtime_error(event.text.empty()?"Robo save failed":event.text.front());
        }
    };
    const auto rest=[&](unsigned ticks) { while(ticks--) step(); };
    const auto key=[&](unsigned buttons) { step(); step(buttons); rest(16); };
    rest(40); key(0x20); rest(20);
    Require(room.teaching_snapshot().menu_open,"Robo save menu did not open");
    key(0x10);
    Require(room.teaching_snapshot().menu_cursor==6,"Robo Save must belong to the System menu");
    key(0x2000); rest(24); key(0x40); key(0x2000); rest(40);
    Require(saves==1,"Robo System Save did not persist a slot");
    const auto request=ReadRetailSlot(slot);
    const auto saved=DecodeRetailUtilityRequest(request,RetailSaveProfile{});
    const auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    const auto page=saved.profile.FirstDayDiary();
    const auto& word=owners.dictionary.children.at(0).children.at(163).children.at(0).children;
    const auto& catalog=owners.topic_history.children.at(0).children;
    const auto p=saved.profile.ReadProfile();
    Require(p.actor==1 && p.nickname.text()=="ROBO" && p.player.text()=="AKI" && page &&
            lesson.learned_serial==1 && word.at(1).text==lesson.learned_written && word.at(2).text==lesson.learned_reading &&
            std::any_of(catalog.begin(),catalog.end(),[](const auto& cell){return (cell.word&0x0fffffffu)!=0;}),
            "Robo protected slot omitted live profile, memo, diary or date topic history");

    if (check==RoboSlotCheck::contact_save) {
        ReportContactWord("saved_contact_word",lesson.learned_written,lesson.learned_reading);
        return;
    }
    if (check==RoboSlotCheck::penultimate || check==RoboSlotCheck::final_day) {
        CheckRoboPenultimate(game,saved,calendar,check);
        return;
    }
    ++calendar[1];
    CheckColdRoboWord(game,calendar,lesson.learned_written,lesson.learned_reading,request);
    std::cout<<"Robo protected System Save and cold BootToDisplay returned to activity and restored word 163\n";

    ++calendar[4];
    ResumeDayRoom next_day(game,saved,calendar);
    next_day.Start(1,2);
    next_day.RevealDiary();
    next_day.Save();
    const auto next=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
    const auto next_owners=RoomSaveOwners::FromActorGraph(next.actor);
    const auto next_page=next.profile.FirstDayDiary();
    Require(next_owners.day_words[0].children.at(0).word==0x20000002 &&
            EncodeKscSaveGraph(next_owners.topic_history.children.at(0))==EncodeKscSaveGraph(owners.topic_history.children.at(0)) &&
            next_page && next_page->revealed && next_page->text==page->text &&
            next_page->date==page->date,"Robo day-two save lost history or the first-day diary");
    std::cout<<"Robo day 1 -> 2 retained topic history and opened the previous diary page\n";
}

void CheckNamedFirstDaySlot(const std::filesystem::path& game, PostProfile& room,
                            const dokodemo::recovered::PackageView& font, dokodemo::recovered::RngNativeOwner& rng,
                            std::vector<std::uint8_t>& frame, const dokodemo::gameplay::AcceptedProfile& profile,
                            dokodemo::gameplay::RetailSaveProfile::Calendar calendar) {
    using namespace dokodemo::gameplay;
    const auto slot=game/"savedata"/"UCJS10002000";
    Require(!std::filesystem::exists(slot.parent_path()),"The first-day test requires a private slot");
    unsigned draws=0, writes=0;
    const auto step=[&](unsigned buttons=0) {
        if(!room.Draw(frame,(++draws%2)*surface,buttons,&font,&rng,&calendar))
            throw std::runtime_error(room.last_error());
        for (const auto& event:room.departure_events()) {
            const std::string_view name=event.name;
            if (name=="system-save-written") ++writes;
            if (name=="system-save-unavailable") throw std::runtime_error(event.text.at(0));
        }
    };
    const auto wait=[&](unsigned ticks) { while(ticks--) step(); };
    const auto press=[&](unsigned buttons) { step(); step(buttons); wait(18); };
    wait(40);
    press(0x20);
    wait(22);
    Require(room.teaching_snapshot().menu_open,"Character's first-day room menu did not open");
    press(0x10);
    Require(room.teaching_snapshot().menu_cursor==6,"Character's System row did not open");
    press(0x2000); wait(24); press(0x40); press(0x2000); wait(48);
    Require(writes==1,"Character's System Save did not commit");
    const auto request=ReadRetailSlot(slot);
    const auto saved=DecodeRetailUtilityRequest(request,RetailSaveProfile{});
    const auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    const auto& rows=owners.inventory.children.at(0).children;
    const auto saved_profile=saved.profile.ReadProfile();
    Require(saved_profile.actor==profile.actor && saved_profile.nickname.text()==profile.nickname.text() &&
            saved_profile.player.text()==profile.player.text() && saved.profile.FirstDayDiary() &&
            (owners.progress_flags[0].children.at(0).word&1) && rows.size()==170 &&
            (rows.front().children.at(0).children.at(0).word>>28)==2 &&
            !rows.front().children.at(0).children.at(1).text.empty(),
            "Protected save omitted the character or current first-day owners");
    Require(owners.day_words[0].children.at(0).word==0x20000001 &&
            owners.diary_staging.children.at(0).children.at(1).word==0x2000000c &&
            owners.contact_count.children.at(0).word==0x20000001,
            "Protected save omitted the first-day event or diary");
    ++calendar[1];
    for (int day : {1,2}) {
        std::array<std::uint8_t,16> date{};
        for(unsigned i=0;i<6;++i) {
            date[2*i]=std::uint8_t(calendar[6-i]);
            date[2*i+1]=std::uint8_t(calendar[6-i]>>8);
        }
        BootToDisplay cold;
        Require(cold.Initialize() && cold.InitializeClock(date,168) &&
                cold.MountDataArchive(game/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP") &&
                cold.PrepareTitle() && cold.Title().title_complete(),"Character did not select cold room continuation");
        unsigned restored=0;
        for(unsigned i=0;i<1800 && !cold.RoomActivity().started_command;++i) {
            if(!cold.AdvanceTitle()) throw std::runtime_error("Character's cold room failed to advance");
            for(const auto& event:cold.DepartureEvents()) {
                const std::string_view name=event.name;
                if(name=="room-load-restored") {
                    Require(event.values.at(0).second==day && event.values.at(1).second==1,
                            "Cold room reported the wrong registered day");
                    ++restored;
                }
                Require(name!="gift-received" && name!="diary-created","Character replayed the introductory event");
            }
        }
        Require(restored==1 && cold.RoomActivity().started_command && ReadRetailSlot(slot)==request,
                "Cold room did not restore its slot and start an activity");
        ++calendar[4];
    }
    std::cout<<profile.nickname.text()<<" protected slot and cold room returns on days 1 and 2 verified\n";
}

void CheckNamedPenultimate(const std::filesystem::path& game, dokodemo::gameplay::RetailSaveProfile::Calendar calendar) {
    using namespace dokodemo::gameplay;
    const auto slot=game/"savedata"/"UCJS10002000";
    const auto baseline=ReadRetailSlot(slot);
    auto saved=DecodeRetailUtilityRequest(baseline,RetailSaveProfile{});
    const auto profile=saved.profile.ReadProfile();
    Require(profile.actor>=2 && profile.actor<5,"Wrong saved character for the penultimate scene");
    auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    owners.day_words[0].children.at(0).word=0x2000000c;
    saved.actor=owners.ToActorGraph();
    ++calendar[4];
    calendar[3]=14;
    calendar[2]=53;
    ResumeDayRoom run(game,saved,calendar);
    unsigned buttons=0, choices=0, nav_on=0, nav_off=0;
    bool offered=false, returned=false;
    std::vector<int> records, companion;
    for(unsigned tick=0;tick<8500 && !(returned && run.room.first_activity());++tick) {
        const unsigned held=std::exchange(buttons,0u);
        run.Step(held);
        for(const auto& event:run.room.departure_events()) {
            const std::string_view name=event.name;
            if(name=="scheduled-room-event" || name=="scheduled-room-return") {
                Require(event.values.at(0).second==2 && event.values.at(1).second==13,
                        "Scheduled the wrong penultimate scene");
                returned|=name=="scheduled-room-return";
            } else if(name=="scheduled-actor-selection") {
                records.push_back(event.values.at(0).second);
            } else if(name=="scheduled-navigation") {
                if(event.values.at(0).second) ++nav_on;
                else ++nav_off;
            } else if(name=="scheduled-actor-command") {
                const auto action=event.values.at(0).second;
                if(event.values.at(1).second) companion.push_back(action);
                if(action==79) offered=true;
            } else if(name=="scheduled-room-choice") {
                Require(event.values.at(0).second==2,"Scheduled menu lacks both replies");
                ++choices;
            }
        }
        if(!held && (run.room.conversation_snapshot().option_count ||
                      (offered && !choices && nav_on>nav_off && tick%4==0))) buttons=0x2000;
    }
    std::cout<<profile.nickname.text()<<" day 13 updates="<<run.frames<<" choices="<<choices<<" returns="<<returned
             <<" navigation="<<nav_on<<"/"<<nav_off<<" record switches="<<records.size()<<" KURO commands";
    for(int command:companion) std::cout<<" "<<command;
    std::cout<<'\n';
    Require(run.scheduled==1 && run.previous==12 && run.day==13 && returned && choices>0 &&
            records==std::vector<int>{1,0,1,0,1,0,1,0} && companion==std::vector<int>{280,39,281,282} &&
            run.room.first_activity() && ReadRetailSlot(slot)==baseline,"Penultimate scene did not return to activity");
    run.Save();
    const auto next=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
    const auto retained=RoomSaveOwners::FromActorGraph(next.actor);
    Require(retained.day_words[0].children.at(0).word==0x2000000d &&
            (retained.progress_flags[0].children.at(0).word&3)==3,"Day 13 state was lost in the save");
    ResumeDayRoom restarted(game,next,run.calendar);
    restarted.Start(13,13);
    Require(!restarted.scheduled,"Day 13 repeated after its protected save");
}

int CheckRoboIntroduction(const std::filesystem::path& game,unsigned answer,bool menus=false,unsigned dated_answer=0,
                          std::optional<RoboMenuLesson> menu_lesson={},RoboSlotCheck slot_check=RoboSlotCheck::none,
                          unsigned actor=1) {
    using namespace dokodemo::gameplay;
    constexpr unsigned circle=0x2000,cross=0x4000;
    std::array<uint32_t,7> calendar{0,0,32,11,18,11,2026};
    if (dated_answer && dated_answer!=3) {
        calendar[4]=dated_answer==2?26:22;
        calendar[5]=dated_answer==2?7:2;
    }
    ProfileText nickname, player, phone;
    constexpr const char* names[]{"NEKO","ROBO","USAGI","INU","KAERU"};
    Require(actor>=1 && actor<std::size(names),"Invalid named introduction fixture");
    const auto name=names[actor];
    Require(nickname.Assign(name) && player.Assign("AKI") && phone.Assign("123"),"Test profile setup");
    const auto profile=AdoptProfile(actor,nickname,player,2,1980,2,29,1,phone,calendar);
    const auto font=RoomFont(game);
    dokodemo::recovered::RngNativeOwner rng;
    rng.ApplyInitializerTimeT32(answer ? 3 : 54,1,"Robo cold headless component session");
    PostProfile room;
    std::string error;
    if(!room.Load(game,profile,calendar,error)) throw std::runtime_error(error);
    std::vector<uint8_t> vram(2*1024*1024);
    bool invited=false,opened_bag=false,bag_closed=false,diary_created=false;
    unsigned presents=0,previous=0,last_message=0;
    bool moved=false, introduction_checked=false;
    unsigned completed_activities=0;
    RoomActivitySnapshot previous_activity{};
    std::set<unsigned> message_sites;
    for(unsigned frame=0;frame<6000;++frame) {
        const auto before=room.conversation_snapshot();
        const auto bag=room.inventory_snapshot();
        unsigned buttons=0;
        // Hold Circle through the entrance. Only a release and fresh edge
        // after the actual invitation is visible may start contact.
        if(!before.invitation_ready && !invited) buttons=circle;
        else if(before.invitation_ready && !invited && !previous) { buttons=circle; invited=true; }
        else if(before.option_count && !previous) {
            Require((actor>=2 || answer!=0) && before.option_count==2,"Unexpected first-day question");
            if(answer==2 && !moved) { buttons=0x40; moved=true; }
            else buttons=circle;
        }
        else if(bag.list_ready && !opened_bag) {
            opened_bag=true;
            Require(bag.count==1 && bag.slot==0 && bag.id>=0,"Robo gift did not reach its inventory slot");
            buttons=cross;
        }
        const auto offset=(frame%2)*surface;
        if(!room.Draw(vram,offset,buttons,&font,&rng,&calendar))
            throw std::runtime_error(std::string(name)+" room at update "+std::to_string(frame)+": "+room.last_error());
        previous=buttons;
        const auto state=room.conversation_snapshot();
        if(state.message_pc && state.message_pc!=last_message) {
            Require(invited,"Held Circle accepted the first-day invitation");
            message_sites.insert(state.message_pc);
            last_message=state.message_pc;
            std::cout<<name<<" message "<<std::hex<<last_message<<std::dec<<" at "<<frame<<'\n';
        }
        for(const auto& event:room.departure_events()) {
            const std::string_view name=event.name;
            if(name=="gift-received") ++presents;
            if(name=="diary-created") diary_created=true;
        }
        if(opened_bag && !room.inventory_snapshot().visible) bag_closed=true;
        if(room.introduction_finished() && !introduction_checked) {
            if (actor>=2) {
                constexpr unsigned first[]{0x5f96b,0x63751,0x6279c};
                constexpr unsigned gift[]{0x630b9,0x672d4,0x670d9};
                const bool goodbye=std::any_of(message_sites.begin(),message_sites.end(),[&](unsigned site) {
                    if (actor==2)
                        return (site>=0x6211e && site<0x621c0) || (site>=0x967f && site<0x96e5) ||
                               (site>=0x9e1e && site<0x9e80);
                    if (actor==3) return site>=0x9e8c && site<=0xa884;
                    return (site>=0x9594 && site<=0x9813) || site==0xa4a7;
                });
                std::cout<<name<<" invitation="<<invited<<" gift="<<presents<<" bag="<<opened_bag<<"/"<<bag_closed
                         <<" diary="<<diary_created<<'\n';
                Require(invited && presents==1 && opened_bag && bag_closed && diary_created &&
                        message_sites.count(first[actor-2]) && message_sites.count(gift[actor-2]) && goodbye,
                        "The character did not finish the greeting, gift, diary and farewell");
                Require(Visible(vram,offset).visible>0,"The room did not render after the first contact");
                std::cout<<name<<" finished the greeting and returned after the diary\n";
                introduction_checked=true;
                continue;
            }
            std::cout<<"Robo results invited="<<invited<<" gifts="<<presents<<" bag_opened="<<opened_bag
                     <<" bag_closed="<<bag_closed<<" diary="<<diary_created<<" choice="<<state.chosen_option<<'\n';
            Require(invited && presents==1 && opened_bag && bag_closed && diary_created &&
                    state.chosen_option==(answer ? int(answer) : -1),
                    "Robo conversation returned before its question, gift and diary");
            if(answer) Require(message_sites.count(0x5aec2) &&
                               message_sites.count(answer==1?0x5af0d:0x5af55),
                               "Robo answered from the wrong branch");
            else Require(!message_sites.count(0x5aec2),"No-question branch showed the ASK");
            Require(message_sites.count(0x5ac3e) && message_sites.count(0x6719a) &&
                    (message_sites.count(0x5bea5)||message_sites.count(0x5bec3)||
                     message_sites.count(0x5beec)||message_sites.count(0x5bf03)||message_sites.count(0x5bf1d)),
                    "Missing Robo greeting, gift or own farewell");
            Require(Visible(vram,offset).visible>0,"Robo room did not render on return");
            std::cout<<"Robo invitation, gift modal, diary and farewell returned at update "<<frame<<'\n';
            introduction_checked=true;
        }
        if(!introduction_checked) continue;
        Require(buttons==0,"Player input drove a Robo activity");
        const auto turn=room.activity_snapshot();
        if(turn.serial!=previous_activity.serial) {
            Require(turn.serial==previous_activity.serial+1 && turn.selected_row && turn.selected_command &&
                    !turn.started_command && !turn.pause_ticks, "Robo selector did not post a new activity");
            if(previous_activity.serial)
                Require(previous_activity.remaining_ticks==1 && !turn.inputs.first_after_cleanup,
                        "Robo selected before the previous pause ended");
            else Require(turn.inputs.first_after_cleanup,"Robo lost its first selection flag");
        }
        if(turn.pause_ticks && !previous_activity.pause_ticks) {
            Require(previous_activity.serial==turn.serial && previous_activity.actor_wait_released &&
                    turn.started_command==turn.selected_command && turn.pause_ticks>=30 && turn.pause_ticks<=60,
                    "Robo activity pause did not follow the owning actor return");
            std::cout<<name<<" activity "<<turn.serial<<" row="<<*turn.selected_row
                     <<" ASC="<<*turn.started_command<<" pause="<<turn.pause_ticks<<" update="<<frame<<'\n';
            if(++completed_activities==3) {
                if(actor>=2 && slot_check!=RoboSlotCheck::none) {
                    CheckNamedFirstDaySlot(game,room,font,rng,vram,profile,calendar);
                    if (slot_check==RoboSlotCheck::penultimate || slot_check==RoboSlotCheck::final_day)
                        CheckNamedPenultimate(game,calendar);
                }
                if(menus) CheckRoboHomeMenus(room,font,rng,vram,calendar,frame+1);
                if(dated_answer || menu_lesson) {
                    TitleRenderer keyboard;
                    std::string keyboard_error;
                    Require(keyboard.LoadSoftkey(game/"assets/game/title",keyboard_error),"Missing original SOFTKEY");
                    if(dated_answer) CheckRoboDatedContact(room,font,rng,vram,calendar,dated_answer==2?2:1,
                                                           keyboard,dated_answer==3,dated_answer==4);
                    if(menu_lesson) CheckRoboMenuLesson(room,font,rng,vram,calendar,keyboard,*menu_lesson);
                    if(slot_check!=RoboSlotCheck::none) CheckRoboSlot(game,room,font,rng,keyboard,vram,calendar,slot_check);
                }
                return 0;
            }
        }
        previous_activity=turn;
    }
    throw std::runtime_error("Robo did not complete its introduction and three activities");
}

} // namespace

void CheckAlternateVocabulary(const std::filesystem::path& game) {
    using namespace dokodemo::gameplay;
    using dokodemo::recovered::RngNativeOwner;
    struct Saying {
        unsigned last, primary, secondary;
        const char* first_sha;
        const char* last_sha;
    };
    constexpr Saying sayings[]{
        {4,0x2541,0x254b,"9aa300d8211d72b7d7b59781dd4618538c214914d1c564d287a948ec27ac34d2",
                        "ef0d81b4364eccdecc8e40781eb6d0dd808eb599b32e944d1d4d7273a0979572"},
        {12,0x2427,0x2431,"507f8acd124c909d388ffc10a353e3aaca2ad2bcd076e004333de511013ac3df",
                         "61fa63d3cbf2806cfa2db22ff7b85ebcf349eaa2615c9f7b89e8c78c7f89f8fd"},
        {4,0x24bd,0x24c7,"fda80cafca3167931469f2d461fe65ecece9289a531e656a432ef5177a631c4e",
                        "df7fcd03663d536e2830f42892dcdbd3856d15849c776f6a18a47aa185037735"},
    };
    const auto media=game/"assets/game/post-profile";
    const RetailSaveProfile::Calendar calendar{0,0,32,11,18,11,2026};
    ProfileText nickname, player, phone;
    Require(nickname.Assign("FRIEND") && player.Assign("AKI") && phone.Assign("123"),"Alternate profile fixture");
    for (unsigned i=0;i<std::size(sayings);++i) {
        const auto& entry=sayings[i];
        const auto profile=AdoptProfile(i+2,nickname,player,2,1980,2,29,1,phone,calendar);
        std::string error;
        for (unsigned draw : {0u,entry.last}) {
            RoomVocabulary words;
            Require(words.Load(media,profile,error),"Unable to load the alternate dictionary");
            RngNativeOwner rng, expected;
            rng.ImportCurrentState64(1,1,"alternate cold saying");
            expected=rng;
            Require((expected.NextC3234()&1)==1,"Wrong alternate filter fixture");
            words.StartActorScript(draw,&rng);
            const auto& text=words.player_saying();
            const auto sha=dokodemo::core::sha256_hex(reinterpret_cast<const std::uint8_t*>(text.data()),text.size());
            Require(sha==(draw?entry.last_sha:entry.first_sha) &&
                    rng.Snapshot().seed==expected.Snapshot().seed,"Cold fallback or RNG draw differs");
            const auto restored=words.WithSaveTables(words.DictionarySaveTable(),words.QuestionSaveTable());
            Require(restored.player_saying()==text && restored.FieldText(0,1)=="FRIEND" &&
                    restored.FieldText(600,1)=="AKI","Alternate dictionary owner did not round-trip");
        }
        RoomVocabulary live, reference;
        Require(live.Load(media,profile,error),"Unable to load the alternate dictionary");
        reference=live;
        RngNativeOwner rng;
        rng.ImportCurrentState64(0,1,"alternate live dictionary selection");
        auto expected=rng;
        Require(!(expected.NextC3234()&1),"Wrong alternate zero-filter fixture");
        const auto candidates=reference.Candidates(entry.primary,entry.secondary,true);
        Require(candidates>0,"Expected an alternate initial saying candidate");
        const int row=reference.FinishLookup(expected.NextC3234()%candidates);
        live.StartActorScript(0,&rng);
        Require(live.player_saying()==reference.Word(row,{},0,false) &&
                rng.Snapshot().seed==expected.Snapshot().seed,"Alternate dictionary selection differs");
        PostProfile scene;
        Require(scene.Load(game,profile,calendar,error),"Alternate profile did not load its room vocabulary");
        std::vector<std::uint8_t> frame(2*1024*1024);
        Require(scene.Draw(frame,0,0,nullptr,&rng,&calendar),"Alternate profile did not enter the room");

        RoomNotebookDiary diary;
        RoomMessages staging;
        const auto name=i==1?nickname.text():player.text();
        Require(diary.Load(game/"assets/game/notebook",error,profile.actor) &&
                diary.RecordFirstDay(staging,rng,name,calendar[6],calendar[5],calendar[4]) &&
                staging.DescriptorUsed(12),"Alternate first-day diary did not record its own descriptor");
        const auto text=diary.entry_text();
        unsigned substitutions=0;
        for (std::size_t at=0;(at=text.find(name,at))!=text.npos;at+=name.size()) ++substitutions;
        constexpr unsigned expected_substitutions[]{0,2,1};
        Require(text.find("$_")==text.npos && substitutions==expected_substitutions[i] &&
                !diary.RecordFirstDay(staging,rng,name,calendar[6],calendar[5],calendar[4]),
                "Alternate first-day diary substituted the wrong name or repeated its entry");
    }
    std::cout<<"Usagi, Inu and Kaeru dictionary, source saying selection, diary and cold room load verified\n";
}

int main(int argc, char **argv) try {
    if(argc==3) {
        if(std::string_view(argv[2])=="--named-penultimate-from-slot") {
            using namespace dokodemo::gameplay;
            const std::filesystem::path game=argv[1];
            const auto saved=DecodeRetailUtilityRequest(
                ReadRetailSlot(game/"savedata"/"UCJS10002000"),RetailSaveProfile{});
            CheckNamedPenultimate(game,saved.profile.SavedAt());
            return 0;
        }
        constexpr std::string_view options[]{"--usagi-introduction","--inu-introduction","--kaeru-introduction"};
        constexpr std::string_view saves[]{"--usagi-save","--inu-save","--kaeru-save"};
        constexpr std::string_view penultimate[]{"--usagi-penultimate","--inu-penultimate","--kaeru-penultimate"};
        constexpr std::string_view final_day[]{"--usagi-final-day","--inu-final-day","--kaeru-final-day"};
        for (unsigned i=0;i<std::size(options);++i) {
            if (options[i]==argv[2]) return CheckRoboIntroduction(argv[1],0,false,0,{},RoboSlotCheck::none,i+2);
            else if (saves[i]==argv[2]) return CheckRoboIntroduction(argv[1],0,false,0,{},RoboSlotCheck::save,i+2);
            else if (penultimate[i]==argv[2]) return CheckRoboIntroduction(argv[1],0,false,0,{},RoboSlotCheck::penultimate,i+2);
            else if (final_day[i]==argv[2]) {
                const std::filesystem::path game=argv[1];
                if (const int status=CheckRoboIntroduction(game,0,false,0,{},RoboSlotCheck::final_day,i+2)) return status;
                const auto slot=game/"savedata"/"UCJS10002000";
                const auto baseline=game/"penultimate-slot"/slot.filename();
                std::filesystem::create_directories(baseline.parent_path());
                std::filesystem::copy(slot,baseline,std::filesystem::copy_options::recursive);
                CheckFinalDaySlot(game,baseline);
                CheckDepartedRoom(game);
                return 0;
            }
        }
    }
    if(argc==3 && std::string_view(argv[2])=="--robo-home") return CheckRoboIntroduction(argv[1],0,true);
    if(argc==5 && std::string_view(argv[2])=="--return-letter-slot")
        return CheckReturnLetter(argv[1],argv[3],std::stoul(argv[4]),false);
    if(argc==5 && std::string_view(argv[2])=="--return-letter-live")
        return CheckReturnLetter(argv[1],argv[3],std::stoul(argv[4]),true);
    if(argc==4 && std::string_view(argv[2])=="--robo-contact") {
        const std::string_view answer=argv[3];
        Require(answer=="1" || answer=="2","Invalid Robo contact answer");
        return CheckRoboIntroduction(argv[1],0,false,unsigned(answer.front()-'0'));
    }
    if(argc==3 && std::string_view(argv[2])=="--robo-contact-ordinary")
        return CheckRoboIntroduction(argv[1],0,false,3);
    if(argc==3 && std::string_view(argv[2])=="--robo-contact-teach")
        return CheckRoboIntroduction(argv[1],0,false,4);
    if(argc==3 && std::string_view(argv[2])=="--contact-save-write")
        return CheckRoboIntroduction(argv[1],0,false,4,{},RoboSlotCheck::contact_save);
    if(argc==3 && std::string_view(argv[2])=="--contact-save-read")
        return CheckColdRoboContactSlot(argv[1]);
    if(argc==4 && std::string_view(argv[2])=="--robo-menu-lesson") {
        const std::string_view choice=argv[3];
        Require(choice=="cancel" || choice=="film" || choice=="robot","Invalid Robo menu lesson fixture");
        return CheckRoboIntroduction(argv[1],0,false,0,
            choice=="cancel"?RoboMenuLesson::cancel:choice=="film"?RoboMenuLesson::film:RoboMenuLesson::robot);
    }
    if(argc==4 && std::string_view(argv[2])=="--robo-introduction") {
        const auto answer=std::string_view(argv[3]);
        Require(answer=="0" || answer=="1" || answer=="2","Invalid headless Robo answer");
        return CheckRoboIntroduction(argv[1],unsigned(answer.front()-'0'));
    }
    if (argc==3) {
        const std::string_view operation=argv[2];
        if (operation=="--robo-final-day") {
            using namespace dokodemo::gameplay;
            const std::filesystem::path game=argv[1];
            const int prepared=CheckRoboIntroduction(game,0,false,2,RoboMenuLesson::film,RoboSlotCheck::final_day);
            if (prepared) return prepared;
            const auto slot=game/"savedata"/"UCJS10002000";
            const auto baseline=game/"penultimate-slot"/slot.filename();
            std::filesystem::create_directories(baseline.parent_path());
            std::filesystem::copy(slot,baseline,std::filesystem::copy_options::recursive);
            std::cout<<"Robo day 14, ordinary departure"<<std::endl;
            CheckFinalDaySlot(game,baseline);
            CheckDepartedRoom(game);
            WriteRetailSlot(slot,game,ReadRetailSlot(baseline));
            std::cout<<"Robo day 14, extended stay"<<std::endl;
            return CheckExtendedDays(game,baseline);
        }
        if (operation=="--robo-save" || operation=="--robo-penultimate")
            return CheckRoboIntroduction(argv[1],0,false,2,RoboMenuLesson::film,
                operation=="--robo-penultimate"?RoboSlotCheck::penultimate:RoboSlotCheck::save);
    }
    if (argc==3 && std::string_view(argv[2])=="--extended-return-slot")
        return CheckExtendedReturn(argv[1]);
    if (argc==3 && std::string_view(argv[2])=="--departed-room-slot")
        return CheckDepartedRoom(argv[1]);
    if (argc==4 && std::string_view(argv[2])=="--extended-last-day-slot")
        return CheckExtendedLastDay(argv[1],argv[3]);
    if (argc==4 && std::string_view(argv[2])=="--extended-penultimate-slot")
        return CheckExtendedPenultimate(argv[1],argv[3]);
    if (argc==4 && std::string_view(argv[2])=="--extended-stay-slot")
        return CheckFinalDaySlot(argv[1],argv[3],true);
    if (argc==4 && std::string_view(argv[2])=="--final-day-slot")
        return CheckFinalDaySlot(argv[1],argv[3]);
    if (argc==6) {
        const std::string_view operation=argv[2];
        using dokodemo::gameplay::MeetingMode;
        if (operation=="--card-exchange-slot" || operation=="--gift-exchange-slot" ||
            operation=="--shiritori-slot")
            return CheckRecordExchange(argv[1],argv[3],unsigned(std::stoul(argv[4])),unsigned(std::stoul(argv[5])),
                operation=="--gift-exchange-slot"?MeetingMode::gifts:
                operation=="--shiritori-slot"?MeetingMode::shiritori:MeetingMode::cards);
    }
    if (argc==4 && std::string_view(argv[2])=="--cold-meeting-card")
        return CheckColdMeetingCard(argv[1],unsigned(std::stoul(argv[3])));
    if (argc==7 && std::string_view(argv[2])=="--gift-exchange-slot")
        return CheckRecordExchange(argv[1],argv[3],unsigned(std::stoul(argv[4])),unsigned(std::stoul(argv[5])),
                                   dokodemo::gameplay::MeetingMode::gifts,argv[6]);
    if (argc==7 && std::string_view(argv[2])=="--shiritori-slot") {
        const auto move=std::string_view(argv[6]);
        Require(move=="wrong-link" || move=="repeat" || move=="terminal-n","Unknown rejected Shiritori move");
        const auto bad_move=move=="wrong-link" ? BadShiritoriMove::wrong_link :
            move=="repeat" ? BadShiritoriMove::repeated_word : BadShiritoriMove::terminal_n;
        return CheckRecordExchange(argv[1],argv[3],unsigned(std::stoul(argv[4])),unsigned(std::stoul(argv[5])),
                                   dokodemo::gameplay::MeetingMode::shiritori,{},bad_move);
    }
    if (argc==3 && std::string_view(argv[2])=="--cold-meeting-gift")
        return CheckColdMeetingGift(argv[1]);
    if (argc==4 && std::string_view(argv[2])=="--replacement-slot")
        return CheckReplacementSlot(argv[1],argv[3]);
    if (argc==4 && std::string_view(argv[2])=="--expired-stay-slot")
        return CheckExpiredStay(argv[1],argv[3]);
    if (argc==4 && std::string_view(argv[2])=="--named-extended-days-slot")
        return CheckExtendedDays(argv[1],argv[3]);
    if (argc==5 && std::string_view(argv[2])=="--named-contact-slot")
        return CheckNamedContactSlot(argv[1],argv[3],std::stoul(argv[4]));
    if (argc==5 && std::string_view(argv[2])=="--destination-slot")
        return CheckDestinationSlot(argv[1],argv[3],std::stoul(argv[4]));
    if (argc==3 && std::string_view(argv[2])=="--destination-resources") return CheckDestinationResources(argv[1]);
    if (argc==4 && std::string_view(argv[2])=="--next-day-slot") return CheckNextDaySlot(argv[1],argv[3]);
    if (argc==4 && std::string_view(argv[2])=="--penultimate-clock")
        return CheckPenultimateSlot(argv[1],argv[3],false);
    if (argc==4 && std::string_view(argv[2])=="--penultimate-circle")
        return CheckPenultimateSlot(argv[1],argv[3],true);
    if (argc==4 && std::string_view(argv[2])=="--prepare-penultimate-slot")
        return MakePenultimateWindowSlot(argv[1],argv[3]);
    if (argc==4 && std::string_view(argv[2])=="--resume-utility") {
        using namespace dokodemo::gameplay;
        std::ifstream file(argv[3],std::ios::binary);
        const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file),{});
        const auto saved=DecodeRetailUtilityRequest(bytes,RetailSaveProfile{});
        auto calendar=saved.profile.SavedAt();
        PostProfile room;
        std::string error;
        if (!room.Load(argv[1],saved,calendar,error)) throw std::runtime_error(error);
        const auto font=RoomFont(argv[1]);
        dokodemo::recovered::RngNativeOwner rng;
        rng.ApplyInitializerTimeT32(168,1,"component resumed room entrance");
        std::vector<uint8_t> frame(2*1024*1024);
        unsigned restored=0;
        for (unsigned tick=0;tick<1800;++tick) {
            if (!room.Draw(frame,(tick%2)*surface,0,&font,&rng,&calendar)) throw std::runtime_error(room.last_error());
            for (const auto& event:room.departure_events()) {
                if (std::string_view(event.name)=="room-load-restored") ++restored;
                Require(std::string_view(event.name)!="gift-received" && std::string_view(event.name)!="diary-created",
                        "Saved room replayed first-day rewards");
            }
            if (const auto command=room.first_activity()) {
                Require(room.introduction_finished() && restored==1,"Resumed room entered the wrong action");
                std::cout<<"restored_live_owners=26 natural_activity="<<*command<<" at_update="<<tick<<'\n';
                return 0;
            }
        }
        throw std::runtime_error("Resumed neutral room never started its activity");
    }
    if (argc != 2)
        throw std::runtime_error("Expected unpacked game directory");
    const std::filesystem::path game = argv[1];
    const std::array<uint32_t, 7> calendar{0, 0, 32, 11, 18, 11, 2026};
    CheckAlternateVocabulary(game);
    std::set<std::uint64_t> actor_frames;
    std::vector<uint8_t> vram(2 * 1024 * 1024);
    for (unsigned actor = 0; actor < 5; ++actor) {
        PostProfile room;
        std::string error;
        if (!room.Load(game, actor, calendar, error))
            throw std::runtime_error(error);
        unsigned requests = 0;
        bool saw_partial = false;
        Frame opened{};
        // Draw both live colour buffers, as the ordinary boot/display does.
        for (unsigned frame = 0; frame < (actor == 0 ? 235u : 105u); ++frame) {
            const std::size_t offset = (frame % 2) * surface;
            Require(room.Draw(vram, offset), "Room update or mesh draw failed");
            if (room.music_requested())
                ++requests;
            if (frame == 0)
                Require(Visible(vram, offset).visible == 0, "Initial wipe must cover the room");
            if (frame == 20) {
                const auto pixels = Visible(vram, offset).visible;
                saw_partial = pixels > 0 && pixels < 480 * 272;
            }
            if (frame == 42) {
                opened = Visible(vram, offset);
                Require(opened.visible > 100000 && requests == 1,
                        "Expected open room and first music request");
            }
            if (frame == 104)
                actor_frames.insert(Visible(vram, offset).hash);
            if (actor == 0 && frame == 234)
                Require(Visible(vram, offset).hash != opened.hash,
                        "Toro room remained on the entrance frame");
        }
        Require(saw_partial && requests == 1, "Expected one opening and a single room music request");
    }
    RoofSceneResources(game);
    ConversationPreparation(0);
    ConversationPreparation(2);
    ConversationPreparation(3);
    for (unsigned arg : {7u,8u,9u}) ConversationPreparation(arg);
    ConversationPlaceLamp();
    ConversationInterrupt();
    ConversationWithTelevision(game);
    FirstActivityPhase();
    NaturalRoomLamp(game, false);
    NaturalRoomLamp(game, true);
    FirstConversation(game, calendar, 168, false);  // F6 common-list selection
    FirstConversation(game, calendar, 1431, true);  // same-session teaching and the converter after the ordinary return
    Require(actor_frames.size() == 5, "Character selections produced indistinguishable room frames");
    std::cout << "Five adopted characters produced five distinct entrance frames. Open, music and Toro "
                 "motion passed.\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
}
