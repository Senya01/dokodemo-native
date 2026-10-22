#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include "audio_native.h"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

#pragma comment(lib, "winmm.lib")

namespace dokodemo::audio {
namespace {

std::uint16_t U16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}

std::uint32_t U32(const unsigned char* p) {
    return p[0] | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool MmResult(MMRESULT result, const char* operation, std::string& error) {
    if (result == MMSYSERR_NOERROR) return true;
    std::array<char, MAXERRORLENGTH> detail{};
    waveOutGetErrorTextA(result, detail.data(), static_cast<UINT>(detail.size()));
    error = std::string(operation) + ": " + detail.data() + " (" + std::to_string(result) + ")";
    return false;
}

} // namespace

bool LoadPcm16Wave(const std::filesystem::path& path,
                  std::shared_ptr<const PcmClip>& clip, std::string& error) {
    clip.reset();
    error.clear();
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) { error = "Cannot open PCM WAV"; return false; }
        const auto length = input.tellg();
        if (length < 44 || length > static_cast<std::streamoff>(std::numeric_limits<std::uint32_t>::max())) {
            error = "Invalid PCM WAV length"; return false;
        }
        std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) {
            error = "Cannot read complete PCM WAV"; return false;
        }
        if (std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4) ||
            static_cast<std::uint64_t>(U32(bytes.data() + 4)) + 8 != bytes.size()) {
            error = "Invalid RIFF WAVE header or declared length"; return false;
        }
        const unsigned char* format = nullptr;
        const unsigned char* samples = nullptr;
        std::size_t data_size = 0;
        std::size_t pos = 12;
        while (pos < bytes.size()) {
            if (bytes.size() - pos < 8) { error = "Truncated WAV chunk header"; return false; }
            const auto count = U32(bytes.data() + pos + 4);
            if (count > bytes.size() - pos - 8) { error = "WAV chunk outside file"; return false; }
            if (!std::memcmp(bytes.data() + pos, "fmt ", 4)) {
                if (format || count < 16) { error = "Invalid or duplicate WAV format"; return false; }
                format = bytes.data() + pos + 8;
            } else if (!std::memcmp(bytes.data() + pos, "data", 4)) {
                if (samples) { error = "Duplicate WAV data"; return false; }
                samples = bytes.data() + pos + 8;
                data_size = count;
            }
            pos += 8 + static_cast<std::size_t>(count) + (count & 1U);
            if (pos > bytes.size()) { error = "Missing WAV chunk padding"; return false; }
        }
        if (!format || !samples || !data_size) { error = "Missing WAV format or samples"; return false; }
        const auto channels = U16(format + 2);
        const auto rate = U32(format + 4);
        const auto align = U16(format + 12);
        if (U16(format) != WAVE_FORMAT_PCM || U16(format + 14) != 16 ||
            (channels != 1 && channels != 2) || !rate || rate > 192000 ||
            align != channels * 2 || U32(format + 8) != rate * align || data_size % align) {
            error = "Unsupported or inconsistent PCM WAV format"; return false;
        }
        auto loaded = std::make_shared<PcmClip>();
        loaded->channels = channels;
        loaded->sample_rate = rate;
        loaded->samples.resize(data_size / sizeof(std::int16_t));
        std::memcpy(loaded->samples.data(), samples, data_size);
        clip = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        error = std::string("WAV load failed: ") + exception.what();
        return false;
    }
}

struct Win32Player::Impl {
    HWAVEOUT output = nullptr;
    std::shared_ptr<const PcmClip> clip;
    std::array<WAVEHDR, 2> headers{};
    std::size_t count = 0;
    bool paused = false;
};

Win32Player::Win32Player() : impl_(std::make_unique<Impl>()) {}
Win32Player::~Win32Player() { Stop(); }

void Win32Player::Stop() noexcept {
    if (impl_->output) {
        waveOutReset(impl_->output);
        for (std::size_t i = 0; i < impl_->count; ++i) {
            if (impl_->headers[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(impl_->output, &impl_->headers[i], sizeof(WAVEHDR));
        }
        waveOutClose(impl_->output);
        impl_->output = nullptr;
    }
    impl_->clip.reset();
    impl_->count = 0;
    impl_->paused = false;
    impl_->headers = {};
}

bool Win32Player::Play(std::shared_ptr<const PcmClip> clip,
                       std::optional<SampleLoop> loop, std::string& error) {
    error.clear();
    if (!clip || (clip->channels != 1 && clip->channels != 2) || !clip->sample_rate ||
        clip->sample_rate > 192000 || clip->samples.empty() || clip->samples.size() % clip->channels ||
        clip->samples.size() > std::numeric_limits<DWORD>::max() / sizeof(std::int16_t)) {
        error = "Invalid PCM clip"; return false;
    }
    if (loop && (loop->begin_frame >= loop->end_frame || loop->end_frame > clip->FrameCount())) {
        error = "Loop interval outside PCM clip"; return false;
    }
    Stop();
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = clip->channels;
    format.nSamplesPerSec = clip->sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(clip->channels * 2);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    if (!MmResult(waveOutOpen(&impl_->output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL), "waveOutOpen", error)) {
        impl_->output = nullptr;
        return false;
    }
    impl_->clip = std::move(clip);
    const auto add = [&](std::uint64_t begin, std::uint64_t end, bool repeat) {
        auto& header = impl_->headers[impl_->count++];
        header.lpData = reinterpret_cast<LPSTR>(const_cast<std::int16_t*>(impl_->clip->samples.data() + begin * format.nChannels));
        header.dwBufferLength = static_cast<DWORD>((end - begin) * format.nBlockAlign);
        if (repeat) {
            header.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
            header.dwLoops = std::numeric_limits<DWORD>::max();
        }
    };
    if (loop) {
        if (loop->begin_frame) add(0, loop->begin_frame, false);
        add(loop->begin_frame, loop->end_frame, true);
    } else {
        add(0, impl_->clip->FrameCount(), false);
    }
    // Pause before queuing both buffers so a short intro cannot finish before
    // its loop buffer reaches the device. Resume after the queue is complete.
    if (!MmResult(waveOutPause(impl_->output), "waveOutPause", error)) { Stop(); return false; }
    for (std::size_t i = 0; i < impl_->count; ++i) {
        if (!MmResult(waveOutPrepareHeader(impl_->output, &impl_->headers[i], sizeof(WAVEHDR)), "waveOutPrepareHeader", error) ||
            !MmResult(waveOutWrite(impl_->output, &impl_->headers[i], sizeof(WAVEHDR)), "waveOutWrite", error)) {
            Stop(); return false;
        }
    }
    if (!MmResult(waveOutRestart(impl_->output), "waveOutRestart", error)) { Stop(); return false; }
    return true;
}

bool Win32Player::Pause(std::string& error) {
    error.clear();
    if (!impl_->output) { error = "No active PCM output"; return false; }
    if (!MmResult(waveOutPause(impl_->output), "waveOutPause", error)) return false;
    impl_->paused = true;
    return true;
}

bool Win32Player::Resume(std::string& error) {
    error.clear();
    if (!impl_->output) { error = "No active PCM output"; return false; }
    if (!MmResult(waveOutRestart(impl_->output), "waveOutRestart", error)) return false;
    impl_->paused = false;
    return true;
}

bool Win32Player::SetVolume(float gain, std::string& error) {
    error.clear();
    if (!impl_->output) { error = "No active PCM output"; return false; }
    if (!std::isfinite(gain) || gain < 0.0f || gain > 1.0f) { error = "Volume outside 0..1"; return false; }
    const DWORD level = static_cast<DWORD>(gain * 65535.0f + 0.5f);
    return MmResult(waveOutSetVolume(impl_->output, level | (level << 16)), "waveOutSetVolume", error);
}

bool Win32Player::IsPlaying() const noexcept {
    return impl_->output && impl_->count && !(impl_->headers[impl_->count - 1].dwFlags & WHDR_DONE);
}

bool Win32Player::IsPaused() const noexcept { return impl_->paused; }
std::uint32_t Win32Player::DeviceCount() noexcept { return waveOutGetNumDevs(); }

} // namespace dokodemo::audio
