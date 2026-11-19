#include "room_input_gate.h"

namespace dokodemo::gameplay {

RoomInputEvent RoomInputGate::Step(std::uint32_t buttons, const RoomInputFrame& frame,
                                   std::optional<int> closed_menu_result) {
    constexpr std::uint32_t circle = 0x2000, right = 0x20;
    const auto pressed = buttons & ~previous_;
    previous_ = buttons;

    switch (phase_) {
    case Phase::room: // BOOT 23B24/23BD8
        if (!frame.room_active) break;
        if (!frame.vm_busy && frame.fade_open) {
            if ((pressed & right) && frame.menu_enabled) {
                phase_ = Phase::menu;
                return RoomInputEvent::menu;
            }
            if (pressed & circle) {
                // 23C6C clears both pending flags before posting event (0,0).
                request9_ = request8_ = false;
                phase_ = Phase::await_request;
                return RoomInputEvent::ordinary;
            }
        }
        if (request8_) phase_ = Phase::await_request; // 23C30, also while VM-busy
        break;
    case Phase::await_request: // 23CA8
        if (request8_) {
            request9_ = request8_ = false;
            phase_ = Phase::begin_action;
        } else if (request9_) {
            request9_ = false;
            phase_ = Phase::room;
        }
        break;
    case Phase::begin_action: // 235EC: phase 5/substate 0 -> substate 4
        request9_ = false;
        phase_ = Phase::action;
        break;
    case Phase::action: // 2360C, cold room with unchanged scene selector
        if (frame.vm_busy) break;
        if (pressed & circle) return RoomInputEvent::during_action; // (12,5)
        if (request9_) {
            request9_ = false;
            phase_ = Phase::return_room;
        }
        break;
    case Phase::return_room: // 236B0: cold owner has pending phase 0
        phase_ = Phase::room;
        break;
    case Phase::menu:
        if (closed_menu_result) {
            if ((*closed_menu_result >= 0 && *closed_menu_result <= 2) ||
                (*closed_menu_result >= 8 && *closed_menu_result <= 11)) {
                // 23748 retains the resolved result. Both travel rows go through 23734.
                menu_action_ = *closed_menu_result;
                phase_ = Phase::post_menu_action;
            }
            else if (*closed_menu_result == 3 || *closed_menu_result == 4 || *closed_menu_result == 6) {
                // D9818: friend notebook (9), word memo (8), picture diary (12).
                phase_ = Phase::notebook;
                return RoomInputEvent::notebook;
            }
            else if (*closed_menu_result == 5) {
                // D982C -> 237F0: clear the two selection fields, then 24FF4(14,1).
                phase_ = Phase::inventory;
                return RoomInputEvent::inventory;
            }
            else if (*closed_menu_result == 13) { // D9818 -> 23844, request resource 10
                phase_ = Phase::options;
                return RoomInputEvent::options;
            }
            else if (*closed_menu_result == 14 || *closed_menu_result == 15) {
                phase_ = Phase::save;
                return RoomInputEvent::save_request;
            }
            else if (*closed_menu_result == -1) phase_ = Phase::cancel_menu;
            else phase_ = Phase::other_menu;
        }
        break;
    case Phase::post_menu_action: {
        // 239BC advances the menu, clears 9 then 8, and dispatches by the
        // saved result: 23A00 -> (0,1), 23A18 -> (0,4), 23A24 -> (0,3).
        constexpr RoomInputEvent actions[]{RoomInputEvent::teaching, RoomInputEvent::return_home,
                                          RoomInputEvent::outing};
        phase_ = Phase::finish_menu;
        request9_ = false;
        request8_ = false;
        return menu_action_ >= 8 ? RoomInputEvent::friends_action : actions[menu_action_];
    }
    case Phase::finish_menu: // substate 5 -> phase 3 at 238B0
        menu_action_ = -1;
        phase_ = Phase::after_menu;
        break;
    case Phase::after_menu: // 23CF4 -> phase 1
        phase_ = Phase::await_request;
        break;
    case Phase::cancel_menu:
        phase_ = Phase::room;
        break;
    case Phase::save:
        // Port notice. Keep the selection edge out of both the notice and the room.
        if (pressed & (circle | 0x4000)) {
            phase_ = Phase::room;
            return RoomInputEvent::dismiss_save_notice;
        }
        break;
    case Phase::options:
    case Phase::other_menu:
    case Phase::inventory:
    case Phase::notebook:
        break;
    }
    return RoomInputEvent::none;
}

} // namespace dokodemo::gameplay
