#pragma once

#include "audio_native.h"
#include "room_sound.h"
#include <array>
#include <map>
#include <optional>

namespace dokodemo::gameplay {

class TitleEffects {
public:
    explicit TitleEffects(std::filesystem::path audio_directory);
    bool Play(int sequence);
    void SetVolumeSetting(unsigned setting);
    // Returns true for a handled request, including a DAK callback whose
    // verified original sequence lookup produces no sound.
    bool Play(const RoomSound& request);
    bool CharacterVoicePlaying() const noexcept { return character_.IsPlaying(); }
    const std::string& Error() const noexcept { return error_; }
private:
    struct Note {
        unsigned rate, level;
        std::vector<std::int16_t> samples;
    };
    bool PlayNote(unsigned bank, unsigned group, unsigned sequence, unsigned gain, bool character);
    unsigned volume_setting_ = 3;
    bool PlayOneShot(std::shared_ptr<const audio::PcmClip> clip);
    void LoadRoomNotes();
    // nullopt denotes a selected DAK key whose original bank lacks the sequence.
    std::map<std::array<unsigned,3>, std::optional<Note>> room_notes_;
    std::filesystem::path directory_;
    std::array<std::shared_ptr<const audio::PcmClip>, 15> clips_;
    audio::Win32Player character_;
    std::vector<std::unique_ptr<audio::Win32Player>> menu_;
    std::string error_;
};

} // namespace dokodemo::gameplay
