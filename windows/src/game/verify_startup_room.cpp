#include "verify_startup_room.h"

#include "gameplay/boot_to_display.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace dokodemo::gameplay {
namespace {
constexpr unsigned circle = 0x2000;
constexpr unsigned cross = 0x4000;
constexpr std::string_view player = "AKI";

void Require(bool passed, const char* error) {
    if (!passed) throw std::runtime_error(error);
}

void Advance(BootToDisplay& boot, unsigned buttons = 0) {
    if (!boot.AdvanceTitle(buttons))
        throw std::runtime_error(boot.SaveReadBoundary() ? boot.SaveReadBoundary() : "Game frame update failed");
}

void Rest(BootToDisplay& boot, unsigned frames) {
    while (frames--) Advance(boot);
}

void Press(BootToDisplay& boot, unsigned buttons) {
    Advance(boot, buttons);
    Advance(boot);
}

template<class Ready>
void Await(BootToDisplay& boot, Ready ready, const char* error) {
    for (unsigned remaining = 750; remaining && !ready(); --remaining) Advance(boot);
    Require(ready(), error);
}

void AdoptToro(BootToDisplay& boot) {
    const auto& title = boot.Title();
    const auto panel = [&](unsigned index) { return title.panel() == index && title.interactive(); };
    const auto finish_name = [&] {
        Rest(boot, 12);
        Press(boot, 0x8000);
        Press(boot, circle);
        Rest(boot, 12);
    };
    // The first title actor arrives after the slot and free-space requests.
    Await(boot, [&] { return panel(0) && title.menu_variant() == 1 && title.actor_visible(0); },
          "Missing new game menu");
    Press(boot, circle);
    Await(boot, [&] { return panel(2) && title.actor_x(0) == 0; }, "Missing character selection");
    Require(title.selected_actor() == 0, "Expected Toro as the initial selection");
    Press(boot, circle);
    Await(boot, [&] { return panel(3); }, "Missing nickname editor");
    finish_name();
    Await(boot, [&] { return panel(4) && title.profile_present_mask() == 1; }, "Missing profile editor");
    Rest(boot, 12);
    Require(boot.CommitNickname(player), "Could not enter the player name");
    finish_name();
    Await(boot, [&] { return title.gender_list_visible(); }, "Missing gender list");
    Rest(boot, 12);
    Press(boot, circle);
    Rest(boot, 34);
    for (unsigned field = 0; field < 3; ++field) {
        Press(boot, circle);
        Rest(boot, 24);
    }
    Await(boot, [&] { return title.blood_list_visible(); }, "Missing blood group list");
    Rest(boot, 12);
    Press(boot, circle);
    Rest(boot, 26);
    Require(boot.CommitNickname("123"), "Could not enter the phone number");
    finish_name();
    Await(boot, [&] { return title.profile_present_mask() == 127; }, "Profile did not accept all seven fields");
    Rest(boot, 24);
    Press(boot, circle);
    Await(boot, [&] { return title.title_complete(); }, "The completed profile did not enter the room");
    Require(!title.replacing_slot() && title.selected_actor() == 0, "The title did not adopt a new Toro");
    std::printf("verified_toro_profile at_frame=%u\n", boot.TitleFrame());
}
}

void VerifyStartupRoom(BootToDisplay& boot) {
    // Keep the room in the midday period while advancing the test's frames.
    // The startup worker still used the Windows clock to initialize the session.
    auto calendar = boot.Calendar();
    calendar[1] = calendar[2] = 0;
    calendar[3] = 12;
    boot.SetUpdateCalendar(calendar);
    AdoptToro(boot);

    unsigned gifts = 0, diaries = 0, previous = 0;
    bool invited = false, bag_closed = false;
    for (unsigned frame = 0; frame < 6500; ++frame) {
        const auto conversation = boot.RoomConversation();
        const auto bag = boot.RoomBag();
        unsigned buttons = 0;
        if (!previous && !invited && conversation.invitation_ready) {
            buttons = circle;
            invited = true;
        } else if (!previous && invited && conversation.option_count) {
            buttons = circle;
        } else if (!previous && invited && !bag_closed && bag.list_ready) {
            Require(bag.count == 1 && bag.slot == 0 && bag.id >= 0, "Missing gift in the introductory inventory");
            buttons = cross;
            bag_closed = true;
        }
        Advance(boot, buttons);
        previous = buttons;

        for (const auto& event : boot.DepartureEvents()) {
            const std::string_view name = event.name;
            if (name == "gift-received") ++gifts;
            if (name == "diary-created") {
                Require(event.text.size() == 1 && event.text.front() == player, "Room did not use the adopted player name");
                ++diaries;
            }
        }
        const auto activity = boot.RoomActivity();
        if (activity.serial == 1 && activity.pause_ticks) {
            Require(invited && gifts == 1 && bag_closed && !boot.RoomBag().visible && diaries == 1 &&
                    !buttons && !boot.RoomConversation().running, "The room started activity before the introduction returned");
            Require(activity.inputs.scene == 0 && activity.inputs.first_after_cleanup &&
                    activity.started_command && activity.started_command == activity.selected_command &&
                    activity.actor_wait_released && activity.pause_ticks >= 30 && activity.pause_ticks <= 60,
                    "First room activity did not finish and schedule its pause");
            std::vector<std::uint32_t> pixels;
            Require(boot.Screen().CopyPixels(pixels) && pixels.size() == Display::width * Display::height &&
                    std::count_if(pixels.begin(), pixels.end(), [](auto color) { return color & 0xffffffu; }) > 1000,
                    "Missing visible room frame");
            std::printf("verified_first_room_activity serial=%u asc=%u pause=%u at_frame=%u\n",
                        activity.serial, *activity.started_command, activity.pause_ticks, boot.TitleFrame());
            return;
        }
    }
    throw std::runtime_error("Timed out waiting for the first room activity to finish");
}
}
