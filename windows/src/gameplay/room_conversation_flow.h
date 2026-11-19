#pragma once

#include <optional>
#include <functional>
#include <string>
#include <vector>
#include <tuple>

namespace dokodemo::gameplay {

// An actual dictionary string consumed by an inline KSC 14 local substitution.
// The offset and length index the expanded message in CP932 bytes.
struct RoomMessageWord {
    int row;
    unsigned field;
    std::size_t offset, length;
    unsigned control_pc=0, local=0;
    bool operator==(const RoomMessageWord& other) const {
        return std::tie(row,field,offset,length,control_pc,local)==
               std::tie(other.row,other.field,other.offset,other.length,other.control_pc,other.local);
    }
};

struct RoomConversationSnapshot {
    unsigned serial = 0, argument = 0, topic_pc = 0, boundary_pc = 0, message_pc = 0, option_count = 0;
    int chosen_option = -1;
    bool running = false, completed = false, word_entry_interactive = false;
    unsigned indirect_topic = 0;
    bool message_revealed = false;
    std::string message;
    std::vector<int> word_rows;
    std::vector<RoomMessageWord> word_operands;
    std::vector<std::string> displayed_lines;
};

struct RoomConversationFrame {
    bool lamp, locked;
    unsigned posture, interrupt, period;
    int mood;
    std::optional<unsigned> started;
    bool ready;
    unsigned scene = 0;
};

// NEKO 8DF10/8D8CB prepares the actor before dispatching the room event argument.
class RoomConversationFlow {
public:
    bool Begin(unsigned argument = 0);
    void Advance(const RoomConversationFrame& frame, const std::function<bool()>& query_region,
                 const std::function<bool()>& approach_done);
    bool wide_camera() const { return wide_camera_; }
    bool interrupt_requested() const { return interrupt_requested_; }
    unsigned actor_request_site() const { return request_site_; }
    bool start_approach() const { return start_approach_; }
    std::optional<unsigned> actor_request() const { return request_; }
    bool wants_attention() const { return state_ == State::attention; }
    void FinishAttention();
    bool close_camera() const { return close_camera_; }
    bool wants_topic() const { return state_ == State::choose; }
    void Play(unsigned pc);
    void Finish();
    void Boundary(unsigned pc);
    const RoomConversationSnapshot& snapshot() const { return snapshot_; }
private:
    enum class State { idle, begin, attention, lamp, face, posture, posture_case, interrupt_wait,
                       posture_compare, posture_done, wait_actor, resume_actor,
                       outside, approach_wait, zoom, zoom_wait, choose, playing, finished, boundary };
    State state_ = State::idle, after_posture_ = State::idle;
    RoomConversationSnapshot snapshot_;
    std::optional<unsigned> request_;
    State on_ready_ = State::resume_actor;
    unsigned current_command_ = 0, delay_ = 0, saved_posture_ = 0, request_site_ = 0;
    bool wanted_lamp_ = false, started_ = false, close_camera_ = false;
    bool wide_camera_ = false, start_approach_ = false;
    bool interrupt_requested_ = false;
    void AskActor(unsigned command, unsigned site, State on_ready = State::resume_actor);
};
}
