#include "room_activity_phase.h"
#include "recovered/rng_native.h"
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
// 2919 calls builtin 11 with the inclusive range width.
unsigned Between(unsigned lower, unsigned upper, recovered::RngNativeOwner& rng) {
    return lower + rng.NextC3234() % (upper - lower + 1);
}
int SignedCalendarMinute(std::uint32_t value) {
    const int payload = static_cast<int>(value & 0x0fffffffu);
    return payload & 0x08000000 ? payload - 0x10000000 : payload;
}

std::uint32_t StartOfDay(std::uint32_t value) {
    const int minute = SignedCalendarMinute(value);
    return static_cast<std::uint32_t>(minute - minute % 1440) & 0x0fffffffu;
}

bool Sleeping(int mood) { return mood == 4 || mood == 5; }
}

void RoomActivityPhase::ResetRestTimes(std::uint32_t now, int mood, recovered::RngNativeOwner& rng) {
    const auto clock = now % 1440;
    if (!Sleeping(mood)) {
        // 135DD uses 135CA's inclusive 21:00..07:00 window; outside it
        // the next sleep deadline is relative to midnight.
        sleep_ = clock >= 21 * 60 || clock <= 7 * 60
            ? now + Between(5, 10, rng)
            : now - clock + Between(21 * 60, 1439, rng);
    }
    quiet_ = now + Between(2, 4, rng); // 13652
}

bool RoomActivityPhase::RegisterFirstDay(std::uint32_t minute) {
    return !registered_midnight_ && day_number_ == 0 && RegisterDay(minute);
}

bool RoomActivityPhase::RegisterDay(std::uint32_t minute) {
    // 1B36..1B53 returns before the daily callbacks for a disabled stay.
    if (last_day_ == 0) day_number_ = -1;
    if (day_number_ < 0) return false;

    const auto midnight = StartOfDay(minute);
    const int current = SignedCalendarMinute(midnight);
    const int previous = SignedCalendarMinute(registered_midnight_.value_or(0));
    int next = day_number_;
    if (!registered_midnight_) {
        ++next; // G59A5 starts as integer zero.
    } else if (previous < current) {
        // 1BB3 compares against a tagged calendar-minute sum, before the day limit.
        const int limit = SignedCalendarMinute(static_cast<unsigned>(previous) + 20160u);
        if (current >= limit) {
            next = -1;
        } else {
            // Only the penultimate day adds the elapsed whole days (1BD3..1BF3).
            next += next == last_day_ - 1 ? (current - previous) / 1440 : 1;
            next = SignedCalendarMinute(static_cast<unsigned>(next));
            if (next > last_day_) next = -1;
        }
    } else if (current < previous) {
        next = -1;
    }
    if (next == day_number_) return false;
    day_number_ = next;
    if (next >= 0) registered_midnight_ = midnight; // 1C34..1C42
    return true;
}

std::array<KscSaveValue,2> RoomActivityPhase::DaySaveWords() const {
    std::array<KscSaveValue,2> words{};
    words[0].word = 0x20000000u | (static_cast<unsigned>(day_number_) & 0x0fffffffu);
    words[1].word = registered_midnight_ ? (0x30000000u | *registered_midnight_) : 0x20000000u;
    return words;
}

void RoomActivityPhase::ValidateDaySaveWords(const std::array<KscSaveValue,2>& words) {
    for (const auto& value : words)
        if (!value.text.empty() || !value.children.empty())
            throw std::invalid_argument("Expected scalar room day fields");
    const int day = SignedCalendarMinute(words[0].word);
    if ((words[0].word >> 28) != 2u || day < -1)
        throw std::invalid_argument("Invalid room day type");
    if (words[1].word == 0x20000000u && (day == 0 || day == -1)) return;
    if (day == 0 || (words[1].word >> 28) != 3u || SignedCalendarMinute(words[1].word) % 1440 != 0)
        throw std::invalid_argument("Invalid registered room day or midnight");
}

void RoomActivityPhase::RestoreDaySaveWords(const std::array<KscSaveValue,2>& words) {
    ValidateDaySaveWords(words);
    day_number_ = SignedCalendarMinute(words[0].word);
    if (words[1].word == 0x20000000u)
        registered_midnight_.reset();
    else
        registered_midnight_ = words[1].word & 0x0fffffffu;
}

std::array<KscSaveValue,2> RoomActivityPhase::ProgressionSaveWords() const {
    return {KscSaveValue{0x20000000u | unsigned(last_day_),{},{}},
            KscSaveValue{0x20000000u | unsigned(scheduled_return_day_),{},{}}};
}

void RoomActivityPhase::RestoreProgressionSaveWords(const std::array<KscSaveValue,2>& words) {
    for (const auto& w : words)
        if ((w.word & 0xf8000000u)!=0x20000000u || !w.text.empty() || !w.children.empty())
            throw std::invalid_argument("Invalid actor stay or scheduled return day");
    last_day_=int(words[0].word & 0x0fffffffu);
    scheduled_return_day_=int(words[1].word & 0x0fffffffu);
}

unsigned RoomActivityPhase::ScheduledEvent(unsigned flags) const {
    const auto day=day_number();
    if (day==1 && !(flags&1)) return 1;
    if (day==scheduled_return_day_ && !(flags&0x80)) return 7;
    if (last_day_==14) {
        if (day>=last_day_-1 && !(flags&2)) return 2;
        if (day==last_day_ && !(flags&4)) return 3;
    } else if (last_day_>14) {
        if (!(flags&8)) return 6;
        if (day>=last_day_-1 && !(flags&0x10)) return 4;
        if (day>=last_day_ && !(flags&0x20)) return 5;
    }
    return 0;
}

void RoomActivityPhase::Enter(std::uint32_t now, recovered::RngNativeOwner& rng) {
    if (state_ != State::unloaded) return;
    RegisterFirstDay(now);
    // 8DDF5 initializes all four rest/activity deadlines and 2868, in order,
    // before 8DE14 dispatches the first-day introduction.
    ResetRestTimes(now, 0, rng);
    long_action_ = now + Between(30, 240, rng); // 1366F
    short_action_ = now + Between(30, 60, rng); // 13686
    next_event_ = now + Between(15, 120, rng); // 89787 -> 1A88
    state_ = State::introduction;
}

void RoomActivityPhase::EnterUnscheduledLoop() {
    // 8DE30 sets case 1; 8DE3B clears 9A79. There is no farewell or sixty-tick delay.
    if (state_ != State::introduction) throw std::logic_error("Unexpected resumed room loop");
    entry_delay_=1;
    state_=State::return_wait;
}

bool RoomActivityPhase::BeginOrdinary() {
    if (state_ == State::unloaded || state_ == State::introduction ||
        state_ == State::ordinary || state_ == State::other_source_path) return false;
    // The event's 8DA49 (KSC 1C) unwinds the suspended VM wait to
    // the enclosing loop. It has no actor opcode of its own.
    current_.remaining_ticks = 0;
    lamp_request_.reset();
    state_ = State::ordinary;
    return true;
}

void RoomActivityPhase::ReleaseUnavailableContact() {
    if (state_ == State::ordinary) {
        entry_delay_ = 1;
        state_ = State::return_wait;
    }
}

void RoomActivityPhase::ReturnToRoom(std::uint32_t now, int mood, recovered::RngNativeOwner& rng,
                                     bool reset_contact_timer) {
    if (state_ != State::introduction && state_ != State::ordinary) return;
    // 89A69 precedes 8D932(false). Only the two rest deadlines are reset
    // here; 3DF5/3DF6 retain their actual first-room values.
    if (reset_contact_timer) next_event_ = now + Between(15, 120, rng);
    ResetRestTimes(now, mood, rng);
    // 8D9BB -> 8D4CC sets 9A76=1 and 9A79=true. On entry to that case
    // 8DE63 clears 9A79 and 8DE68 waits 60 KSC updates.
    first_after_cleanup_ = true; // 8D948
    entry_delay_ = 60;
    state_ = State::return_wait;
}

void RoomActivityPhase::OtherPath(unsigned pc) {
    alternate_pc_ = pc;
    state_ = State::other_source_path;
}

void RoomActivityPhase::Advance(std::uint32_t now, const RoomActivityFrame& frame,
                                recovered::RngNativeOwner& rng) {
    mood_write_.reset();
    lamp_request_.reset();
    switch (state_) {
    case State::return_wait:
        if (--entry_delay_) return;
        break;
    case State::wait_actor:
        // B1D4 posts the new request. Its actor update runs 24700 and
        // resets the old command's 258/25E before a new receipt can count.
        if (!current_.started_command && frame.started && frame.started == current_.selected_command) {
            current_.started_command = frame.started;
            if (!first_started_) first_started_ = frame.started;
        }
        if (current_.started_command && frame.ready) {
            current_.actor_wait_released = true;
            state_ = State::resume_actor;
        }
        return;
    case State::resume_actor:
        // 1E768 wakes the suspended caller. The next scheduler update
        // resumes at 1384B before 2919 can consume the random draw.
        return StartPause(rng);
    case State::lamp_control:
    case State::lamp_posture:
        return PrepareLamp(now, frame);
    case State::wait_lamp_actor:
        if (frame.started == lamp_actor_) lamp_actor_started_ = true;
        if (lamp_actor_started_ && frame.ready) state_ = State::resume_lamp_actor;
        return;
    case State::resume_lamp_actor:
        // 8D862 releases 8D7DA with true. Its caller then waits for 157.
        // After the second return it resumes at 13893, past the daylight test.
        if (lamp_actor_ != 157) return RequestLampActor(157);
        return CheckRest(now, frame.mood, frame.mood_age);
    case State::pause:
        if (--current_.remaining_ticks) return;
        // 13856 -> 138A9 -> 8DEEA -> 8E067 restores the loop frame.
        // 9A79 was cleared at the initial 8DE63. It stays clear through
        // these ordinary turns; there is no second 60-tick entry wait.
        break;
    default:
        return;
    }
    CheckRoomTurn(now, frame);
}

void RoomActivityPhase::CheckRoomTurn(std::uint32_t now, const RoomActivityFrame& frame) {
    // Cold event switches 9A78/9A7A are false. A changed calendar day
    // re-enters 1B34's daily event owner.
    if (StartOfDay(now) != registered_midnight_) return OtherPath(0x1b34);
    if (now >= next_event_) return OtherPath(0x8dea9);
    if (frame.scene) {
        if (frame.outing_deadline && now > *frame.outing_deadline) {
            state_ = State::return_home; // 125A1 -> 8DED9 -> 8D4CC(2,4)
            return;
        }
        // 8D7DA returns false for the nonzero place type. 1387B skips 157.
        return CheckRest(now, frame.mood, frame.mood_age, frame.scene);
    }
    wanted_lamp_ = frame.period < 1 || frame.period > 3;
    if (!Sleeping(frame.mood) && wanted_lamp_ != frame.lamp) {
        state_ = State::lamp_control;
        return PrepareLamp(now, frame);
    }
    CheckRest(now, frame.mood, frame.mood_age);
}

void RoomActivityPhase::PrepareLamp(std::uint32_t now, const RoomActivityFrame& frame) {
    // 8D5D2 waits one tick and re-reads GAME45 until ASC+25A clears.
    if (state_ == State::lamp_control && frame.locked) return;
    state_ = State::lamp_posture;
    // Cases 0/1: if GAME27 is zero and the caller asked for 1, 8D61F
    // and 8D64B return directly. Other postures have their own ASCs.
    switch (frame.posture) {
    case 0:
    case 1:
        if (frame.interrupt) return OtherPath(0x8d5c1); // GAME26 and its interruption handler
        if (frame.lamp == wanted_lamp_) return CheckRest(now, frame.mood, frame.mood_age);
        // GAME40 is re-read at 8D82F. Camera request 2962(0) is
        // suppressed by 296B in this already-wide room.
        return RequestLampActor(wanted_lamp_ ? 153 : 154);
    case 3: return OtherPath(0x8d662);
    case 4: return OtherPath(0x8d6af);
    case 5: return OtherPath(0x8d6e6);
    case 7: return OtherPath(0x8d71d);
    default:
        // 8D75D/8D76C waits for a change and re-enters the posture switch.
        return;
    }
}

void RoomActivityPhase::RequestLampActor(unsigned command) {
    lamp_request_ = lamp_actor_ = command;
    lamp_actor_started_ = false;
    state_ = State::wait_lamp_actor;
}

void RoomActivityPhase::RefuseLampCommand() {
    if (lamp_request_)
        OtherPath(lamp_actor_ == 153 ? 0x8d851 : lamp_actor_ == 154 ? 0x8d85d : 0x1388b);
}

void RoomActivityPhase::CheckRest(std::uint32_t now, int mood, unsigned mood_age, unsigned scene) {
    const auto time = now % 1440;
    const bool quiet_hours = time >= 20 * 60 || time <= 8 * 60;
    const bool sleep_hours = time >= 21 * 60 || time <= 7 * 60;
    // 1369C handles early mood transitions before its scene switch. Each
    // early false return goes straight to 1357E, bypassing the later timers.
    if (quiet_hours && now >= quiet_ && mood == 0) {
        mood_write_ = mood = 3;
    } else if (!quiet_hours && mood == 3) {
        mood_write_ = mood = 0;
    } else if (scene == 0 && Sleeping(mood) && !sleep_hours) {
        return OtherPath(0x1372d);
    } else if (scene == 0 && now >= sleep_) {
        if (Sleeping(mood)) return OtherPath(0x1375e);
        if (mood == 3) return OtherPath(0x1379b);
        // 13798 returns false immediately for the other moods.
    } else if (scene == 0 && now >= long_action_) {
        return OtherPath(0x137eb);
    } else if (scene == 0 && now >= short_action_) {
        return OtherPath(0x13815);
    }

    if ((mood == 1 || mood == 2) && mood_age > 600)
        mood_write_ = 0; // 1357E -> 2151(0)
    // 13857's 5994 starts false. The cold introduction does not call
    // 8BF19 (the writer in the separate event script).
    state_ = State::select;
}

void RoomActivityPhase::StartPause(recovered::RngNativeOwner& rng) {
    // 1384B/1384D -> 2919 passes 31 to builtin 11. An empty result from
    // 13554 skips only builtin23, and reaches the very same random delay.
    current_.pause_ticks = Between(30, 60, rng);
    current_.remaining_ticks = current_.pause_ticks;
    state_ = State::pause;
}

void RoomActivityPhase::FinishSelection(const RoomActivityInputs& inputs, const RoomActivitySelection& choice,
                                       bool accepted, recovered::RngNativeOwner& rng) {
    if (state_ != State::select) return;
    const unsigned serial = current_.serial + 1;
    current_ = {};
    current_.serial = serial;
    current_.inputs = inputs;
    current_.selected_row = choice.selected_row;
    current_.selected_command = choice.command;
    current_.eligible_rows = static_cast<unsigned>(choice.rows.size());
    current_.effective_field58 = choice.effective_field58;
    first_after_cleanup_ = false; // 13563, including the empty-list path.
    if (!choice.command) return StartPause(rng);
    if (!accepted) return OtherPath(0x13573);
    state_ = State::wait_actor;
}
}
