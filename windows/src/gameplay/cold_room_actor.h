#pragma once
#include "post_profile_mesh.h"
#include "room_sound.h"
#include <map>
#include <optional>
#include <set>

namespace dokodemo::gameplay {
class ColdRoomActor {
  public:
    bool Load(const std::filesystem::path &game_directory, unsigned actor, int period, std::string &error);
    // 8938B and 8DE46 issue the entrance and posture ASCs on both room entries.
    // The following 178 invitation belongs only to scheduled action 6.
    void EnterWithoutInvitation() { invite_after_entry_ = false; }
    bool entrance_finished() const { return !entry_sequence_; }
    bool Advance(std::optional<uint32_t> calendar_minute = {});
    bool Request(unsigned command);
    bool AdoptScene(unsigned selector,const std::array<float,3>* destination=nullptr);
    bool RequestActivity(unsigned command);
    bool RequestRoomControl(unsigned command);
    bool RequestConversationControl(unsigned command);
    bool InterruptCommand(); // GAME26 operates on the admitted ASC.
    bool BeginConversationApproach(const RoomMesh& room, const RoomActorRegion& saved_query);
    bool ConversationApproachDone() const { return moving_ < 0 && !turning_; } // BCF0 mode == 0
    std::optional<unsigned> started_activity() const { return started_activity_; }
    unsigned source_field5c() const { return field5c_; } // GAME27
    bool source_flag25a() const { return flag25a_; }    // GAME45
    void SetMood(int mood) { mood_ = mood; }  // GAME21 stores actor+54.
    bool command_ready() const { return !pending_activity_ && (ready_ || repeat_ready_); }
    bool zoom_requested() const { return zoom_requested_; }
    bool invitation_expired() const { return invitation_expired_; }
    bool talk_ready() const { return greeting_; }
    bool SpeechAnchor(std::array<float,3>& position) const;
    bool Draw(const std::array<float, 16> &view_projection, std::vector<uint8_t> &vram, size_t offset,
              const RoomLighting *lights = nullptr) const;
    bool door_close_requested() const { return scene_pair_ == 3; }
    int scene_pair_requested() const { return scene_pair_; }
    std::optional<unsigned> game28_write() const { return game28_write_; }
    const std::string &error() const { return error_; }
    const std::vector<RoomSound> &sounds() const { return sounds_; }
    const std::vector<RoomMotionEffectSpawn>& effect_spawns() const { return effect_spawns_; }
    unsigned source_field50() const { return field50_; }  // GAME23
    unsigned source_field58() const { return field58_; }  // GAME24
    std::optional<bool> lamp_write() const { return lamp_write_; }
    bool light_on() const { return light_; }
    std::array<float, 2> position() const { return {x_, y_}; }
    unsigned command() const { return command_; }

  private:
    bool StartClip(uint32_t motion, unsigned blend_ticks = 0);
    unsigned MotionVariant() const;
    uint32_t LogicalMotion(unsigned record) const;
    bool StartLogical(unsigned record);
    void StartCommand(unsigned record);
    bool QueueCommand(unsigned command);
    bool FinishTravel();
    bool AdvanceMovement();
    bool AdvanceTurn();
    std::array<float,16> World() const;
    bool Parameter(uint32_t key, uint32_t value);
    RoomMesh mesh_;
    std::vector<unsigned> secondary_nodes_;
    int secondary_clip_ = -1;
    unsigned secondary_time_ = 0;
    RoomPoseBlend blend_{};
    unsigned blend_ticks_ = 0, blend_elapsed_ = 0, species_ = 0, invite_motion_ = 0, anchor_ = 0;
    bool clip_started_ = false, first_sample_ = false;
    std::vector<std::vector<std::pair<unsigned, uint32_t>>> sound_keys_;
    std::vector<std::vector<RoomMotionEffectKey>> effect_keys_;
    std::vector<RoomSound> sounds_;
    std::vector<RoomMotionEffectSpawn> effect_spawns_;
    std::map<unsigned, std::vector<uint32_t>> commands_;
    std::map<unsigned, unsigned> clips_;
    std::set<unsigned> activity_ids_;
    std::map<unsigned, std::array<float, 2>> points_;
    std::array<float,3> room_placement_{};
    std::optional<std::array<float,3>> roof_placement_;
    float scene_heading_ = 0;
    std::array<std::vector<std::pair<unsigned, unsigned>>, 8> strides_;
    float x_ = 0, y_ = 0, z_ = 0, heading_ = 0, target_heading_ = 0;
    std::array<float, 2> target_{};
    unsigned command_ = 0, cursor_ = 0, clip_ = 0, time_ = 0, segment_ = 0, field50_ = 1, field58_ = 0; // AD5C initializes actor+50.
    // A selected ASC contains at most one active 8020 block.
    unsigned loop_start_ = 0, loop_remaining_ = 0;
    int zoom_delay_ = -1, repeat_delay_ = -1;
    bool in_loop_ = false, ready_ = false, repeat_ready_ = false;
    bool zoom_requested_ = false, greeting_ = false, invitation_expired_ = false;
    std::optional<uint32_t> invitation_deadline_;
    std::optional<unsigned> pending_activity_, started_activity_;
    bool activity_active_ = false, activity_was_ready_ = false;
    int moving_ = -1, period_ = 0, mood_ = 0;
    bool conversation_approach_ = false;
    int scene_pair_ = -1;
    std::optional<unsigned> game28_write_;
    std::optional<bool> lamp_write_;
    unsigned field5c_ = 0, field60_ = 0;
    bool invite_after_entry_ = true;
    bool entry_sequence_ = true, interrupt_region_ = false;
    bool interrupt_pending_ = false, interrupt_requested_ = false; // 25B/25C
    std::string error_;
    bool turning_ = false, wait_clip_ = false, light_ = false, flag25a_ = false;
};
} // namespace dokodemo::gameplay
