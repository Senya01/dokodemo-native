#pragma once

#include <cstdint>
#include <optional>

namespace dokodemo::gameplay {

// Inputs owned by the room loader, transition and current script context.
// The 1B62C busy poll concerns script/GUI extensions; ASC motion is separate.
struct RoomInputFrame {
    // Resource 14's 1A9F0 zero is transient. After unrequest, 1808C
    // retires it, 1AA7C clears current kind and 185EC publishes the
    // kind-0 initializer result. The caller supplies its own room owner.
    bool room_active = false;  // LoadMan.20 == 6 and LoadMan.469 == 0
    bool fade_open = false;    // GameTransition state 2
    bool vm_busy = false;      // context state 6..9 or a live extension's word 64
    bool menu_enabled = false; // manager byte 68 written by GAME78
    bool actor_present = true;
};

enum class RoomInputEvent { none, ordinary, during_action, menu, teaching, outing, inventory, notebook, options, return_home, friends_action, departed_action, save_request, dismiss_save_notice };

class RoomInputGate {
public:
    // NEKO 8D4F6 in setter(2,action) calls GAME0; 8D9B5 on the
    // completed cold action calls GAME3. These set manager bytes 8 and 9.
    void RequestAction() { request8_ = true; }
    void FinishAction() { request9_ = true; }
    void ReleaseUnavailableContact() {
        request8_ = request9_ = false;
        phase_ = Phase::room;
    }
    bool room_controls_ready() const { return phase_ == Phase::room; }
    bool owns_inventory() const { return phase_ == Phase::inventory; }
    void FinishInventory() { if (owns_inventory()) phase_ = Phase::room; }
    void SaveCompleted() { if (phase_ == Phase::save) phase_ = Phase::room; }
    bool shows_save_notice() const { return phase_ == Phase::save; }
    void FinishOptions() { if (phase_ == Phase::options) phase_ = Phase::room; }
    void FinishNotebook() { if (phase_ == Phase::notebook) phase_ = Phase::room; }

    // One invocation of main-flow, including frames occupied by other input
    // owners. A held Circle cannot become a new press when the owner changes.
    RoomInputEvent Step(std::uint32_t buttons, const RoomInputFrame& frame,
                        std::optional<int> closed_menu_result = {});

private:
    // Only the uninterrupted room's source phases are used here. A Right
    // request transfers control to the separate menu owner.
    enum class Phase { room, await_request, begin_action, action, return_room, menu, inventory, notebook, options, post_menu_action, finish_menu, after_menu, cancel_menu, other_menu, save };
    Phase phase_ = Phase::room;
    int menu_action_ = -1;
    std::uint32_t previous_ = 0;
    bool request8_ = false;
    bool request9_ = false;
};

} // namespace dokodemo::gameplay
