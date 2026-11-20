#pragma once
#include "room_activity_selector.h"
#include "ksc_save_graph.h"
#include <array>

namespace dokodemo::gameplay {

struct RoomActivitySnapshot {
    unsigned serial = 0;
    RoomActivityInputs inputs{};
    std::optional<unsigned> selected_row, selected_command, started_command;
    unsigned pause_ticks = 0, remaining_ticks = 0, eligible_rows = 0;
    int effective_field58 = 0;
    bool actor_wait_released = false;
};

struct RoomActivityFrame {
    unsigned period;
    bool lamp;
    int mood;
    unsigned mood_age, posture, interrupt;
    bool locked;
    std::optional<unsigned> started;
    bool ready;
    unsigned scene = 0;
    std::optional<std::uint32_t> outing_deadline; // 2875 for the current nonzero destination
};

// The ordinary room loop reached after the cold Toro introduction.
class RoomActivityPhase {
public:
    enum class State { unloaded, introduction, return_wait, select, wait_actor, resume_actor, pause,
                       lamp_control, lamp_posture, wait_lamp_actor, resume_lamp_actor, ordinary, other_source_path,
                       return_home };
    // 1B34 returns whether the daily reset callbacks should run.
    bool RegisterDay(std::uint32_t minute);
    // Initial registration for callers entering a newly created room.
    bool RegisterFirstDay(std::uint32_t minute);
    int day_number() const { return day_number_; }
    std::array<KscSaveValue,2> DaySaveWords() const;
    static void ValidateDaySaveWords(const std::array<KscSaveValue,2>& words);
    void RestoreDaySaveWords(const std::array<KscSaveValue,2>& words);
    // G59A4 (last day of the stay), G59A6 (scheduled return day).
    std::array<KscSaveValue,2> ProgressionSaveWords() const;
    void RestoreProgressionSaveWords(const std::array<KscSaveValue,2>& words);
    unsigned ScheduledEvent(unsigned progress_flags) const; // 8979F -> 8DD91
    int last_day() const { return last_day_; }
    void Enter(std::uint32_t minute, recovered::RngNativeOwner& rng);
    void EnterUnscheduledLoop();
    void SetAmbientDeadline(std::uint32_t minute) { next_event_=minute; }
    bool BeginOrdinary();
    void ReleaseUnavailableContact();
    void ReturnToRoom(std::uint32_t minute, int mood, recovered::RngNativeOwner& rng,
                      bool reset_contact_timer = true);
    void Advance(std::uint32_t minute, const RoomActivityFrame& frame, recovered::RngNativeOwner& rng);
    std::optional<unsigned> lamp_command() const { return lamp_request_; }
    void RefuseLampCommand();
    void FinishSelection(const RoomActivityInputs& inputs, const RoomActivitySelection& choice,
                         bool accepted, recovered::RngNativeOwner& rng);
    State state() const { return state_; }
    bool first_after_cleanup() const { return first_after_cleanup_; }
    std::optional<unsigned> first_started() const { return first_started_; }
    const RoomActivitySnapshot& snapshot() const { return current_; }
    std::optional<int> mood_write() const { return mood_write_; }
    unsigned alternate_pc() const { return alternate_pc_; }
private:
    int last_day_=14, scheduled_return_day_=0;
    State state_ = State::unloaded;
    RoomActivitySnapshot current_;
    int day_number_ = 0;
    std::optional<std::uint32_t> registered_midnight_;
    std::uint32_t next_event_ = 0, sleep_ = 0, quiet_ = 0, long_action_ = 0, short_action_ = 0;
    unsigned entry_delay_ = 0, alternate_pc_ = 0;
    bool first_after_cleanup_ = false, wanted_lamp_ = false, lamp_actor_started_ = false;
    unsigned lamp_actor_ = 0;
    std::optional<unsigned> lamp_request_;
    std::optional<unsigned> first_started_;
    std::optional<int> mood_write_;
    void CheckRoomTurn(std::uint32_t minute, const RoomActivityFrame& frame);
    void PrepareLamp(std::uint32_t minute, const RoomActivityFrame& frame);
    void RequestLampActor(unsigned command);
    void CheckRest(std::uint32_t minute, int mood, unsigned mood_age, unsigned scene = 0);
    void StartPause(recovered::RngNativeOwner& rng);
    void ResetRestTimes(std::uint32_t minute, int mood, recovered::RngNativeOwner& rng);
    void OtherPath(unsigned pc);
};
}
