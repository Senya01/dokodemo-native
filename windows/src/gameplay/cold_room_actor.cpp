#include "cold_room_actor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
constexpr float pi = 3.14159265358979323846f;
float Wrap(float a) { return std::remainder(a, 2 * pi); }
// 248DC visits the next opcodes of the active ASC frame in file order.
// Neither loop backedges nor the actor's live cursor are taken by this scan.
auto InterruptExit(const std::vector<uint32_t>& words, unsigned cursor) {
    return std::find_if(words.begin() + cursor, words.end(), [](uint32_t word) {
        return word == 0x8043 || word == 0x8000;
    });
}
struct ControlFile {
    explicit ControlFile(const std::filesystem::path &p) : input(p, std::ios::binary) {}
    uint32_t Word() {
        uint32_t value;
        if (!input.read(reinterpret_cast<char *>(&value), 4))
            throw std::runtime_error("Short room actor control");
        return value;
    }
    unsigned Count(unsigned max) {
        const auto n = Word();
        if (!n || n > max)
            throw std::runtime_error("Room actor control count");
        return n;
    }
    float Float() {
        const auto w = Word();
        float v;
        std::memcpy(&v, &w, 4);
        if (!std::isfinite(v))
            throw std::runtime_error("Invalid actor position");
        return v;
    }
    std::ifstream input;
};
} // namespace
bool ColdRoomActor::Load(const std::filesystem::path &dir, unsigned actor, int period, std::string &error) {
    try {
        if (actor > 5)
            throw std::runtime_error("Invalid room actor species");
        ColdRoomActor next;
        const auto base = dir / "assets" / "game" / "post-profile";
        const auto name = "actor-" + std::to_string(actor);
        if (!next.mesh_.Load(base / (name + ".bin"), error))
            return false;
        ControlFile f(base / (name + ".ascbin"));
        const auto format = f.Word();
        if (format != 0x34415343 && format != 0x35415343)
            throw std::runtime_error("Invalid room actor header");
        next.species_ = actor;
        next.invite_motion_ = f.Word();
        next.anchor_ = f.Word();
        for (unsigned i = 0, n = f.Count(256); i < n; ++i) {
            if (!next.mesh_.Duration(i) || !next.clips_.emplace(f.Word(), i).second)
                throw std::runtime_error("Invalid room actor clip");
            next.sound_keys_.push_back(next.mesh_.SoundKeys(i));
            next.effect_keys_.push_back(next.mesh_.EffectKeys(i));
        }
        for (unsigned i = 0, n = f.Count(1294); i < n; ++i) {
            auto &words = next.commands_[f.Word()];
            words.resize(f.Count(128));
            for (auto &w : words)
                w = f.Word();
        }
        const auto activity_count = f.Word();
        if (activity_count > next.commands_.size())
            throw std::runtime_error("invalid activity count");
        for (unsigned i = 0; i < activity_count; ++i) {
            const auto id = f.Word();
            if (!next.commands_.count(id) || !next.activity_ids_.insert(id).second)
                throw std::runtime_error("invalid activity record binding");
        }
        for (unsigned i = 0, n = f.Count(1024); i < n; ++i) {
            const auto index = f.Word();
            const float x = float(int32_t(f.Word())), y = float(int32_t(f.Word()));
            next.points_[index] = {x, y};
        }
        for (unsigned i = 0, n = f.Count(128); i < n; ++i)
            next.secondary_nodes_.push_back(f.Word());
        next.x_ = f.Float();
        next.y_ = f.Float();
        next.z_ = f.Float();
        next.heading_ = f.Float();
        next.room_placement_ = {next.x_, next.y_, next.z_};
        next.scene_heading_ = next.heading_;
        if (format == 0x35415343)
            next.roof_placement_ = std::array<float,3>{f.Float(), f.Float(), f.Float()};
        for (auto &table : next.strides_) {
            for (unsigned i = 0, n = f.Count(8); i < n; ++i) {
                const auto distance = f.Word(), ticks = f.Word();
                if (!ticks || distance > 1000)
                    throw std::runtime_error("Invalid actor stride");
                table.emplace_back(distance, ticks);
            }
        }
        if (f.input.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("Trailing actor control");
        next.period_ = period;
        if (!next.StartLogical(9))
            throw std::runtime_error("Missing actor standing motion");
        if (actor == 5) next.entry_sequence_ = false;
        else next.StartCommand(176);
        *this = std::move(next);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
bool ColdRoomActor::StartClip(uint32_t motion, unsigned blend_ticks) {
    // BA30/BA20/BA10 resolve all three standard requests using A690(+54).
    const auto variant = MotionVariant();
    if (variant) {
        for (const unsigned logical : {9u, 1u, 5u}) {
            if (motion == LogicalMotion(logical))
                motion = LogicalMotion(logical + variant);
        }
    }
    const auto it = clips_.find(motion);
    if (it == clips_.end())
        return false;
    const int previous_secondary = secondary_clip_;
    blend_ = {clip_, time_, 0, previous_secondary, secondary_time_};
    secondary_clip_ = -1;
    // B9A4/B9B8 select the requested channel plus the logical seated
    // motion for mode 3; BCB0 still checks the requested channel to wake ASC.
    if (field50_ == 3) {
        secondary_clip_ = int(clips_.at(LogicalMotion(13)));
    }
    secondary_time_ = 0;
    blend_ticks_ = clip_started_ && (clip_ != it->second || previous_secondary != secondary_clip_) ? blend_ticks : 0;
    blend_elapsed_ = 0;
    clip_started_ = true;
    first_sample_ = true;
    clip_ = it->second;
    time_ = 0;
    segment_ = 0;
    return true;
}
unsigned ColdRoomActor::MotionVariant() const {
    // A690 keeps 0..3, maps moods 4/5 to 3/2, otherwise returns zero.
    if (mood_ >= 0 && mood_ <= 3) return unsigned(mood_);
    if (mood_ == 4) return 3;
    if (mood_ == 5) return 2;
    return 0;
}
uint32_t ColdRoomActor::LogicalMotion(unsigned record) const {
    const auto &words = commands_.at(record);
    // BA60 resolves the first 8010 of this actor's logical ASC record.
    unsigned p = 0;
    while (p < words.size() && words[p] == 0x8020)
        p += 2;
    if (words.at(p) != 0x8010)
        throw std::runtime_error("missing logical actor motion");
    return words.at(p + 1);
}
bool ColdRoomActor::StartLogical(unsigned record) {
    return StartClip(LogicalMotion(record));
}
void ColdRoomActor::StartCommand(unsigned record) {
    command_ = record;
    cursor_ = 0;
    in_loop_ = false;
    interrupt_region_ = false;
    interrupt_pending_ = false;
    interrupt_requested_ = false;
    flag25a_ = false;
    field5c_ = 0; // 24758/24760 at each new external command.
    ready_ = false;
    repeat_ready_ = false;
    wait_clip_ = false;
}
bool ColdRoomActor::Request(unsigned command) {
    if (command < 20 || !commands_.count(command))
        return false;
    pending_activity_.reset();
    activity_active_ = false;
    activity_was_ready_ = false;
    moving_ = -1;
    turning_ = false;
    entry_sequence_ = false;
    greeting_ = false;
    repeat_delay_ = -1;
    StartCommand(command);
    return true;
}
bool ColdRoomActor::AdoptScene(unsigned selector,const std::array<float,3>* destination) {
    if (selector==0) destination=&room_placement_;
    else if (selector==12 && roof_placement_) destination=&*roof_placement_;
    if (!destination) return false;
    // D2D0 reads three floats from the selector's 16-byte DATA row.
    // BD28 writes position and heading only. The running ASC keeps its cursor.
    const auto& placement=*destination;
    x_ = placement[0];
    y_ = placement[1];
    z_ = placement[2];
    heading_ = scene_heading_;
    return true;
}
bool ColdRoomActor::RequestActivity(unsigned command) {
    return activity_ids_.count(command) && QueueCommand(command);
}
bool ColdRoomActor::RequestRoomControl(unsigned command) {
    return (command == 153 || command == 154 || command == 157) && commands_.count(command) &&
           QueueCommand(command);
}
bool ColdRoomActor::RequestConversationControl(unsigned command) {
    // A newly offered VM script issues its own B1D4 request. B1D4 writes
    // +38C even when the former neutral VM was waiting on another ASC.
    if (((command < 144 || command > 154) && command != 156 && command != 157 && command != 178 && command != 186) ||
        !commands_.count(command))
        return false;
    pending_activity_ = command;
    return true;
}
bool ColdRoomActor::InterruptCommand() {
    if (!command_) return false;
    const auto& words = commands_.at(command_);
    const auto end = InterruptExit(words, cursor_);
    if (end == words.end()) {
        error_ = "GAME26 found no interrupt exit in the current ASC frame";
        return false;
    }
    // C1CC sets 25C/254/25B. 248DC can signal 25E immediately only
    // when it encounters 8000 before 8043; that scan leaves playback alone.
    interrupt_requested_ = interrupt_region_ = interrupt_pending_ = true;
    if (*end == 0x8000) {
        field5c_ = 0;
        ready_ = true;
    }
    return true;
}
bool ColdRoomActor::BeginConversationApproach(const RoomMesh& room, const RoomActorRegion& saved_query) {
    if (!saved_query.inside && !room.DirectActorRoute(position(),saved_query.target)) {
        error_ = "GAME17 route crosses a selector-2 obstacle; 2732C detour is unavailable";
        return false;
    }
    // GAME17 -> C144 terminates the ASC and sets its 258 ready bit.
    // The independent movement owner (+298) is what GAME18 polls.
    StartCommand(0);
    ready_ = true;
    conversation_approach_ = true;
    moving_ = -1;
    turning_ = false;
    if (saved_query.inside) {
        target_heading_ = pi; // 8B5C converts zero degrees by its +180 convention.
        turning_ = true;
        return true;
    }
    target_ = saved_query.target;
    // A278 measures the entire path. Direct mode-4 routes below D7E98
    // (600 units) select logical walk 1; the other ones select run 5.
    moving_ = std::hypot(target_[0]-x_,target_[1]-y_) < 600.f ? 0 : 1;
    field50_ = 0;
    return StartLogical(moving_ ? 5 : 1);
}
bool ColdRoomActor::QueueCommand(unsigned command) {
    if (pending_activity_ || (activity_active_ && !activity_was_ready_ && !command_ready()))
        return false;
    // B1D4 queues +38C and sets +394. The new record consumes no DAK time
    // and none of its ready bits exist until the next B774/24700 admission.
    pending_activity_ = command;
    return true;
}
bool ColdRoomActor::FinishTravel() {
    // B124/B178 finish modes 3/4 and 2 once their target tests succeed.
    // B188/B194 replaces a cleared actor+50 at this point.
    if (field50_ == 0)
        field50_ = 1;
    conversation_approach_ = false;
    return StartLogical(9);
}
bool ColdRoomActor::AdvanceTurn() {
    const float difference = Wrap(target_heading_ - heading_);
    // Same finite native angular interpolation used by the recovered actor
    // owner. Reopen the ASC only when its one-degree angle test succeeds.
    heading_ = Wrap(heading_ + std::clamp(difference, -pi / 18, pi / 18));
    if (std::abs(Wrap(target_heading_ - heading_)) < pi / 180) {
        heading_ = target_heading_;
        turning_ = false;
        return FinishTravel();
    }
    return true;
}
bool ColdRoomActor::AdvanceMovement() {
    const float dx = target_[0] - x_, dy = target_[1] - y_;
    const float remaining = std::hypot(dx, dy);
    if (remaining < 2) {
        x_ = target_[0];
        y_ = target_[1];
        moving_ = -1;
        return FinishTravel();
    }
    const auto &steps = strides_.at(4 * moving_ + MotionVariant());
    const auto [distance, duration] = steps.at(segment_);
    const float step = std::min(remaining, float((distance * 10) / duration));
    // 08A88 returns the current segment's integral stride before advancing.
    const auto frame = time_ / 160;
    if (frame == 1)
        segment_ = 0;
    else if (frame > 1 && frame % duration == 1)
        segment_ = (segment_ + 1) % steps.size();
    heading_ = std::atan2(-dx, dy);
    x_ += dx * step / remaining;
    y_ += dy * step / remaining;
    return true;
}
bool ColdRoomActor::Parameter(uint32_t key, uint32_t value) {
    switch (key) {
    case 0:
        field50_ = value;
        return true;
    case 1:
        // D9910[1] -> 24544 writes the actor's +54 mood field.
        mood_ = static_cast<int>(value);
        return true;
    case 5:
        field58_ = value;
        return true;
    case 2:
        target_heading_ = Wrap((float(int32_t(value)) + 180) * pi / 180);
        turning_ = true;
        return true;
    case 3: {
        const auto p = points_.at(value);
        x_ = p[0];
        y_ = p[1];
        heading_ = pi;
        return StartLogical(9);
    }
    case 4:
    case 11:
        target_ = points_.at(value);
        moving_ = key == 4 ? 0 : 1;
        field50_ = 0;
        return StartLogical(moving_ ? 5 : 1);
    case 6:
        game28_write_ = value;
        return true;
    case 7:
        field5c_ = value;
        return true;
    case 9:
        field60_ = value;
        return true;
    case 8:
        light_ = value != 0;
        lamp_write_ = light_;
        return true;
    case 10:
        if (value > 3)
            return false;
        scene_pair_ = int(value);
        return true;
    default:
        return false;
    }
}
bool ColdRoomActor::Advance(std::optional<uint32_t> calendar_minute) {
    try {
        // A KSC wait can wake on 258 before a later ASC loop clears it.
        // Once the script has that receipt, its 30..60-tick delay need not
        // finish while the same raw flag is still asserted.
        if (activity_active_ && command_ready())
            activity_was_ready_ = true;
        started_activity_.reset();
        if (pending_activity_ && !conversation_approach_) {
            const unsigned request = *pending_activity_;
            if (!Request(request))
                throw std::runtime_error("pending room activity was not bound");
            activity_active_ = true;
            started_activity_ = request;
        }
        sounds_.clear();
        effect_spawns_.clear();
        invitation_expired_ = false;
        scene_pair_ = -1;
        game28_write_.reset();
        lamp_write_.reset();
        zoom_requested_ = false;
        // 8D922 waits 15 script ticks following GAME44(1). The subsequent
        // selected invitation at 8940D enables GAME43 and enters 1A44.
        if (zoom_delay_ > 0 && --zoom_delay_ == 0) {
            greeting_ = true;
            if (calendar_minute)
                invitation_deadline_ = *calendar_minute + 5; // 89947 passes 5 to 8940D.
            StartCommand(invite_motion_);
        }
        // 1A69 yields for the active command, then 1A6C waits 60 script
        // ticks before 1A62 may repeat the invitation motion.
        if (greeting_ && repeat_delay_ < 0 && command_ready())
            repeat_delay_ = 60;
        else if (greeting_ && repeat_delay_ > 0 && --repeat_delay_ == 0) {
            if (calendar_minute && invitation_deadline_ && *calendar_minute >= *invitation_deadline_) {
                greeting_ = false;
                invitation_expired_ = true;
            } else
                StartCommand(invite_motion_);
            repeat_delay_ = -1;
        }
        const auto duration = mesh_.Duration(clip_);
        if (!duration)
            return false;
        // 89BA8 compares the time at entry to the DAK duration. The
        // update which first advances to the end still has playback state 3.
        const bool clip_ended = blend_elapsed_ >= blend_ticks_ && time_ >= duration;
        if (blend_elapsed_ < blend_ticks_) {
            ++blend_elapsed_;
            blend_.weight = float(blend_elapsed_) / blend_ticks_;
        } else {
            if (secondary_clip_ >= 0)
                secondary_time_ = std::min(mesh_.Duration(secondary_clip_), secondary_time_ + 160);
            const auto previous = time_;
            if (time_ < duration)
                time_ = std::min(duration, time_ + 160);
            else if (!wait_clip_) {
                time_ = 0;
                first_sample_ = true;
            }
            for (const auto& key : effect_keys_.at(clip_)) {
                if ((!first_sample_ && key.tick <= previous) || key.tick > time_) continue;
                const auto resource=key.control & 255;
                if ((key.control >> 24) != 128 || key.node < 0)
                    throw std::runtime_error("Unsupported timed DAK effect control "+std::to_string(key.control));
                std::array<float,16> node;
                const RoomJointLayer layer{secondary_nodes_,secondary_clip_,secondary_time_};
                if (!mesh_.NodeWorld(unsigned(key.node),{{clip_,time_}},node,nullptr,&layer))
                    throw std::runtime_error("Missing actor node for timed DAK effect");
                const auto root = World();
                std::array<float,3> local{}, position{};
                for (unsigned axis=0;axis<3;++axis)
                    local[axis]=node[12+axis]+node[axis]*key.offset[0]+node[axis+4]*key.offset[1]+node[axis+8]*key.offset[2];
                for (unsigned axis=0;axis<3;++axis)
                    position[axis]=root[12+axis]+root[axis]*local[0]+root[axis+4]*local[1]+root[axis+8]*local[2];
                effect_spawns_.push_back({resource,position});
            }
            for (const auto &[at, word] : sound_keys_.at(clip_)) {
                if ((at > previous || first_sample_) && at <= time_)
                    sounds_.push_back({species_, (word >> 16) & 255, word & 255, (word >> 8) & 255, true});
            }
            first_sample_ = false;
        }
        if (moving_ >= 0)
            return AdvanceMovement();
        if (turning_)
            return AdvanceTurn();
        if (wait_clip_ && !clip_ended)
            return true;
        wait_clip_ = false;
        if (!command_)
            return true;
        // 24168 -> 24700 replaces the active ASC and rejoins the dispatcher.
        // A malformed cycle must not prevent the next game update.
        for (unsigned instructions = 0; instructions < 4096; ++instructions) {
            const auto &words = commands_.at(command_);
            if (cursor_ >= words.size())
                throw std::runtime_error("ASC record ended without return");
            if (interrupt_pending_ && !flag25a_) {
                // Mask 0x01000000 on control[258..25B] suppresses the
                // ordinary ASC handlers. Neither 8043 nor 8000 is masked.
                const auto exit = InterruptExit(words, cursor_);
                if (exit == words.end())
                    throw std::runtime_error("missing interrupt exit");
                cursor_ = static_cast<unsigned>(exit - words.begin());
            }
            const auto op = words.at(cursor_++);
            switch (op) {
            case 0x8040:
                flag25a_ = words.at(cursor_++) != 0;
                break;
            case 0x8030: {
                const auto parameter = words.at(cursor_++), value = words.at(cursor_++);
                if (!Parameter(parameter, value))
                    return false;
                if (parameter != 10)
                    return true;
                break;
            }
            case 0x8020:
                if (in_loop_)
                    return false;
                loop_remaining_ = words.at(cursor_++);
                loop_start_ = cursor_;
                in_loop_ = true;
                repeat_ready_ = false;
                break;
            case 0x8021:
                if (!in_loop_)
                    return false;
                if (loop_remaining_ == 1) {
                    // 2484C first pushed the owning command at depth zero.
                    // 24328 pops the embedded finite loop back to that owner.
                    // Only depth -1 would set 258 in 24334/24338.
                    in_loop_ = false;
                    break;
                } else {
                    if (loop_remaining_)
                        --loop_remaining_;
                    else
                        repeat_ready_ = true;
                    cursor_ = loop_start_;
                }
                break; // 24308 rejoins the ASC dispatcher on this update.
            case 0x8010: {
                const auto motion = words.at(cursor_++);
                const auto blend_ticks = words.at(cursor_++);
                wait_clip_ = true;
                return StartClip(motion, blend_ticks);
            }
            case 0x8042:
                interrupt_region_ = true;  // 24434, command+254.
                break;
            case 0x8041:
                interrupt_region_ = interrupt_pending_ = false;
                if (interrupt_requested_) {
                    ready_ = true; // 244E0: locked normal path releases 25E.
                    return true;
                }
                break;
            case 0x8043:
                interrupt_region_ = false;
                if (interrupt_pending_) {
                    // 244B0 continues this very frame into its cleanup tail.
                    interrupt_pending_ = false;
                    break;
                }
                if (in_loop_ && !interrupt_requested_)
                    return false;
                StartCommand(0);
                ready_ = true;
                return true;
            case 0x8031:
                ready_ = true;
                if (entry_sequence_ && command_ == 178 && zoom_delay_ < 0) {
                    zoom_requested_ = true;
                    zoom_delay_ = 15;
                }
                return true;
            case 0x8001: {
                const auto target = words.at(cursor_++);
                if (!commands_.count(target))
                    throw std::runtime_error("Missing ASC transfer target");
                StartCommand(target);
                break;
            }
            case 0x8000:
                // Cold route 8DE18/8DE46 and 8D798. The lighting helper
                // asks for light outside scene periods
                // 1..3 and only issues an ASC if the scene light differs.
                if (in_loop_ && !interrupt_requested_)
                    return false;
                if (entry_sequence_ && command_ == 176) {
                    const bool wanted = period_ < 1 || period_ > 3;
                    StartCommand(wanted != light_ ? (wanted ? 153 : 154) : 157);
                } else if (entry_sequence_ && (command_ == 153 || command_ == 154))
                    StartCommand(157);
                else if (entry_sequence_ && command_ == 157 && invite_after_entry_)
                    StartCommand(178);
                else {
                    const auto finished = command_;
                    if (finished == 157 && !invite_after_entry_) entry_sequence_ = false;
                    StartCommand(0);
                    ready_ = true;
                    if (entry_sequence_ && finished == 178 && zoom_delay_ < 0) {
                        zoom_requested_ = true;
                        zoom_delay_ = 15;
                    }
                    return true;
                }
                return true;
            default:
                return false;
            }
        }
        throw std::runtime_error("ASC did not yield");
    } catch (const std::exception &e) {
        error_ = "ASC " + std::to_string(command_) + " at word " + std::to_string(cursor_) + ": " + e.what();
        return false;
    }
}
std::array<float,16> ColdRoomActor::World() const {
    const float angle = Wrap(heading_ + pi), c = std::cos(angle), s = std::sin(angle);
    return {{c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, x_, y_, z_, 1}};
}
bool ColdRoomActor::SpeechAnchor(std::array<float,3>& position) const {
    std::array<float,16> node;
    const RoomJointLayer layer{secondary_nodes_,secondary_clip_,secondary_time_};
    if (!mesh_.NodeWorld(anchor_, {{clip_,time_}}, node, blend_elapsed_ < blend_ticks_ ? &blend_ : nullptr, &layer))
        return false;
    const auto world = World();
    for (unsigned i=0; i<3; ++i)
        position[i] = world[12+i] + world[i]*node[12] + world[4+i]*node[13] + world[8+i]*node[14];
    return true;
}
bool ColdRoomActor::Draw(const std::array<float, 16> &vp, std::vector<uint8_t> &vram, size_t offset,
                         const RoomLighting *lights) const {
    const RoomJointLayer layer{secondary_nodes_,secondary_clip_,secondary_time_};
    return mesh_.Draw({{clip_, time_}}, World(), vp, vram, offset, lights,
                      blend_elapsed_ < blend_ticks_ ? &blend_ : nullptr, &layer);
}
} // namespace dokodemo::gameplay
