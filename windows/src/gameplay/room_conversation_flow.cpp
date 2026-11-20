#include "room_conversation_flow.h"

namespace dokodemo::gameplay {

bool RoomConversationFlow::Begin(unsigned argument) {
    if (argument > 4 && (argument < 7 || argument > 9)) return false;
    if (state_ != State::idle && state_ != State::finished) return false;
    const auto serial = snapshot_.serial + 1;
    snapshot_ = {};
    snapshot_.serial = serial;
    snapshot_.argument = argument;
    snapshot_.running = true;
    robo_ = false;
    state_ = State::begin;
    return true;
}

bool RoomConversationFlow::BeginRoboContact(unsigned argument) {
    if (argument>1 || !Begin(argument)) return false;
    robo_ = true;
    return true;
}

void RoomConversationFlow::AskActor(unsigned command, unsigned site, State on_ready) {
    request_site_ = site;
    on_ready_ = on_ready;
    request_ = current_command_ = command;
    started_ = false;
    state_ = State::wait_actor;
}

void RoomConversationFlow::Advance(const RoomConversationFrame& f, const std::function<bool()>& query_region,
                                  const std::function<bool()>& approach_done) {
    wide_camera_ = false;
    start_approach_ = false;
    close_camera_ = false;
    request_.reset();
    interrupt_requested_ = false;
    for (;;) {
        switch (state_) {
        case State::begin:
            // Case 2 first calls 8D524. The ordinary awake branch then
            // checks GAME19, GAME27 and GAME45 at the start of 8D8CB.
            if (f.mood == 4 || f.mood == 5) return Boundary(robo_ ? 0x6b3e5 : 0x8d542);
            if (snapshot_.argument == 4) { // 8DF16 skips 8D8CB after the mood check.
                state_ = State::choose;
                return;
            }
            if (!query_region() || f.interrupt || f.locked) {
                state_ = State::attention;
                return;
            }
            // Circle (0), teaching (1), and outing (3) all skip 8D868
            // because 8D906 compares G9A77 with 6.
            state_ = State::lamp;
            break;
        case State::lamp:
            // 2192 supplies the current place kind. 8D7E8 returns before GAME36/GAME40 away from home.
            if (f.scene != 0) {
                state_ = State::face;
                break;
            }
            wanted_lamp_ = f.period < 1 || f.period > 3;
            if (wanted_lamp_ != f.lamp) {
                state_ = State::posture;
                after_posture_ = State::lamp;
                break;
            }
            state_ = State::face;
            break;
        case State::face:
            // GAME19 at 8D784 also records the actual nearest region centre
            // outside. GAME17 uses this saved query after the ASC186 return.
            after_posture_ = query_region() ? State::face : State::outside;
            if (after_posture_ == State::outside) wide_camera_ = true; // 8D7A5
            state_ = State::posture;
            break;
        case State::posture:
            if (f.locked) return; // 8D5D2: wait one tick and poll GAME45 again.
            state_ = State::posture_case;
            break;
        case State::posture_case: {
            // GAME23 is read once into local 1 for this pass through 8D5F4.
            // The face caller supplies nil; the outside and lamp callers supply 1.
            saved_posture_ = f.posture;
            const bool any_posture = after_posture_ == State::face;
            switch (saved_posture_) {
            case 0: case 1: case 3: case 4: case 5: case 7:
                if (f.interrupt) {
                    interrupt_requested_ = true; // 8D5C4: GAME26 on the current ASC
                    state_ = State::interrupt_wait;
                    return;
                }
                if (saved_posture_ == 7)
                    return AskActor(f.mood == 5 ? 150 : 146,
                                    robo_ ? (f.mood == 5 ? 0x6b602 : 0x6b60e)
                                          : (f.mood == 5 ? 0x8d741 : 0x8d74d),
                                    State::posture_compare);
                if (robo_ && !any_posture && saved_posture_ == 1)
                    return AskActor(144,0x6b510,State::posture_compare);
                if (!any_posture && saved_posture_ >= 3) {
                    if (saved_posture_ == 3) return AskActor(147, robo_ ? 0x6b551 : 0x8d690, State::posture_compare);
                    if (saved_posture_ == 4) return AskActor(151, robo_ ? 0x6b594 : 0x8d6d3, State::posture_compare);
                    return AskActor(152, robo_ ? 0x6b5cb : 0x8d70a, State::posture_compare);
                }
                state_ = State::posture_done;
                break;
            default:
                state_ = State::posture_compare;
                break;
            }
            break;
        }
        case State::interrupt_wait:
            // 8D5C7 waits for the interrupted script's own completion.
            if (f.ready) state_ = State::posture_compare;
            return;
        case State::posture_compare:
            // 8D75D compares the saved value, even if GAME26 changed the
            // actor's posture. An unequal value waits once before fresh GAME23.
            if (after_posture_ != State::face && saved_posture_ == 1) {
                state_ = State::posture_done;
                break;
            }
            state_ = State::posture_case;
            return;
        case State::posture_done:
            if (after_posture_ == State::lamp) {
                if (f.lamp != wanted_lamp_)
                    return AskActor(wanted_lamp_ ? 153 : 154,
                                    robo_ ? (wanted_lamp_ ? 0x6b712 : 0x6b71e)
                                          : (wanted_lamp_ ? 0x8d851 : 0x8d85d));
                state_ = State::face;
                break;
            }
            if (after_posture_ == State::outside) return AskActor(186, robo_ ? 0x6b675 : 0x8d7b4);
            return AskActor(178, robo_ ? 0x6b65c : 0x8d79b);
        case State::wait_actor:
            if (f.started == current_command_) started_ = true;
            if (started_ && f.ready) state_ = on_ready_;
            return;
        case State::resume_actor:
            if (current_command_ == 186) {
                start_approach_ = true; // GAME17 at 8D7BF, then wait one tick
                state_ = State::approach_wait;
                return;
            }
            state_ = current_command_ == 178 ? State::zoom : State::face;
            break;
        case State::approach_wait:
            if (!approach_done()) return; // GAME18 at 8D7C8, wait one tick and poll again
            state_ = State::zoom;
            break;
        case State::zoom:
            // 8D91B requests GAME44(1) on return from either path.
            close_camera_ = true;
            delay_ = 15;
            state_ = State::zoom_wait;
            return;
        case State::zoom_wait:
            if (--delay_ == 0) state_ = State::choose; // 8DF10 dispatches on the saved argument.
            return;
        default:
            return;
        }
    }
}

void RoomConversationFlow::FinishAttention() {
    if (state_ == State::attention) state_ = State::lamp;
}

void RoomConversationFlow::Play(unsigned pc) {
    snapshot_.topic_pc = pc;
    state_ = State::playing;
}

void RoomConversationFlow::Finish() {
    snapshot_.running = false;
    snapshot_.completed = true;
    state_ = State::finished;
}

void RoomConversationFlow::Boundary(unsigned pc) {
    snapshot_.running = false;
    snapshot_.boundary_pc = pc;
    state_ = State::boundary;
}

void RoomConversationFlow::ReleaseBoundary() {
    if (state_ == State::boundary) state_ = State::finished;
}

}
