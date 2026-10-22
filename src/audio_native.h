#pragma once
#include "audio_pcm.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dokodemo::audio {

// Accepts RIFF PCM16 little-endian mono/stereo only; fails explicitly on other
// codecs, truncated chunks, duplicate data/fmt chunks, or inconsistent sizes.
bool LoadPcm16Wave(const std::filesystem::path& path,
                  std::shared_ptr<const PcmClip>& clip, std::string& error);

struct SampleLoop {
    std::uint64_t begin_frame = 0;
    std::uint64_t end_frame = 0; // Exclusive. Frames use the decoded PCM clock.
};

// A native WinMM output voice. Use independent objects for simultaneous music
// and effects. All methods belong to one caller thread. The shared clip stays
// alive until Stop/destruction. No FFmpeg, ISO, PSP library, or emulator is used
// at runtime. An optional loop plays its intro once, then repeats the exact PCM
// interval; it deliberately does not interpret SGXD sequences or envelopes.
class Win32Player {
public:
    Win32Player();
    ~Win32Player();
    Win32Player(const Win32Player&) = delete;
    Win32Player& operator=(const Win32Player&) = delete;
    Win32Player(Win32Player&&) = delete;
    Win32Player& operator=(Win32Player&&) = delete;

    bool Play(std::shared_ptr<const PcmClip> clip,
              std::optional<SampleLoop> loop, std::string& error);
    bool Pause(std::string& error);
    bool Resume(std::string& error);
    bool SetVolume(float gain, std::string& error); // 0..1, both channels.
    bool IsPlaying() const noexcept;
    bool IsPaused() const noexcept;
    void Stop() noexcept;
    static std::uint32_t DeviceCount() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dokodemo::audio
