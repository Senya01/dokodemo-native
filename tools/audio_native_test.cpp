#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "audio_native.h"

#include <algorithm>
#include <fstream>
#include <iostream>

using namespace dokodemo::audio;

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Usage: audio_native_test.exe <original-decoded.wav>\n"; return 2; }
    std::shared_ptr<const PcmClip> clip;
    std::string error;
    if (!LoadPcm16Wave(argv[1], clip, error)) { std::cerr << error << '\n'; return 1; }
    std::cout << "loaded channels=" << clip->channels << " rate=" << clip->sample_rate
              << " frames=" << clip->FrameCount() << " duration=" << clip->DurationSeconds() << '\n';
    auto malformed = std::filesystem::path(argv[1]).parent_path() / L"audio_invalid_test.tmp";
    { std::ofstream out(malformed, std::ios::binary); out << "RIFF\xFF\xFF\xFF\xFFWAVEinvalid"; }
    std::shared_ptr<const PcmClip> invalid;
    const bool rejected = !LoadPcm16Wave(malformed, invalid, error);
    std::filesystem::remove(malformed);
    if (!rejected) { std::cerr << "Malformed WAV accepted\n"; return 1; }
    std::cout << "malformed WAV rejected: " << error << '\n';
    Win32Player player;
    if (player.Play(clip, SampleLoop{clip->FrameCount(), clip->FrameCount()}, error)) {
        std::cerr << "Invalid loop accepted\n"; return 1;
    }
    if (!Win32Player::DeviceCount()) { std::cerr << "No WinMM output device; playback unverified\n"; return 3; }
    std::cout << "WinMM devices=" << Win32Player::DeviceCount() << '\n';
    if (clip->FrameCount() < clip->sample_rate / 10) {
        std::cerr << "Playback test requires at least 0.1 seconds of original PCM\n"; return 1;
    }
    // All playback uses authentic decoded PCM. A quiet short interval checks
    // hardware queue, pause/resume, completion, and infinite loop behavior.
    auto short_clip = std::make_shared<PcmClip>();
    short_clip->channels = clip->channels;
    short_clip->sample_rate = clip->sample_rate;
    const std::size_t size = clip->sample_rate / 10 * clip->channels;
    const auto start_frame = std::min<std::uint64_t>(clip->sample_rate, clip->FrameCount() - clip->sample_rate / 10);
    const auto first = static_cast<std::ptrdiff_t>(start_frame * clip->channels);
    short_clip->samples.assign(clip->samples.begin() + first, clip->samples.begin() + first + size);
    if (!player.Play(short_clip, std::nullopt, error) || !player.SetVolume(0.02f, error)) {
        std::cerr << error << '\n'; return 1;
    }
    Sleep(300);
    if (player.IsPlaying()) { std::cerr << "One-shot did not finish\n"; return 1; }
    std::cout << "one-shot completed\n";
    if (!player.Play(short_clip, SampleLoop{100, short_clip->FrameCount()}, error) ||
        !player.SetVolume(0.02f, error) || !player.Pause(error) || !player.IsPaused()) {
        std::cerr << error << '\n'; return 1;
    }
    Sleep(50);
    if (!player.IsPlaying() || !player.Resume(error) || player.IsPaused()) {
        std::cerr << "Pause/resume failed: " << error << '\n'; return 1;
    }
    Sleep(300);
    if (!player.IsPlaying()) { std::cerr << "Loop did not repeat\n"; return 1; }
    player.Stop();
    if (player.IsPlaying()) { std::cerr << "Stop failed\n"; return 1; }
    std::cout << "intro+loop, pause/resume, volume, stop passed\n";
    return 0;
}
