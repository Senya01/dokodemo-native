#pragma once

#include "audio_native.h"
#include <array>
#include <filesystem>

namespace dokodemo {
class StartupCoordinator;
}

namespace dokodemo::gameplay {

// The title, guide, room and belongings banks each have a single wave.
// Preparation is silent; the current scene's music event starts playback.
class TitleBgm {
public:
    TitleBgm(StartupCoordinator& startup, std::filesystem::path audio_directory);
    bool Prepare(int bank_index = 0);
    bool Start(int bank_index);
    bool SetVolumeSetting(unsigned setting);
    bool FadeOut();
    bool DuckForCharacter(bool active);
    bool SetSuspended(bool suspended) { return player_.SetSuspended(suspended, error_); }
    void Stop() noexcept;
    const std::string& Error() const noexcept { return error_; }
    const audio::PcmClip* PreparedClip(int bank_index = 0) const noexcept;
    bool Playing() const noexcept { return player_.IsPlaying(); }

private:
    StartupCoordinator& startup_;
    std::filesystem::path directory_;
    std::array<std::shared_ptr<const audio::PcmClip>, 25> clips_;
    bool RefreshGain(bool duck, bool ramp);
    int gain_ = 105; // 14834, BGM setting 3
    int playing_bank_ = -1;
    std::uint64_t start_frame_ = 0;
    bool ducked_ = false;
    audio::Win32Player player_;
    std::string error_;
};

} // namespace dokodemo::gameplay
