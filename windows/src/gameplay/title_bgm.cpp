#include "title_bgm.h"

#include "audio_catalog.h"
#include "startup_runtime_win32.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {

namespace {
// BOOT's original indices, with entries for the music requests reached
// during startup, the first visit to the room and the belongings screen.
constexpr const char* bank_names[] = {
    "BGM_TITLE", "BGM_GUIDE", "BGM_DAYORI", nullptr, nullptr, nullptr,
    "asa1", "asa2", nullptr,
    "hiru1", "hiru2", "hiru3", "hiru4", "hiru5", nullptr,
    "yuu1", "yuu2", nullptr,
    "yoru1", "yoru2", "yoru3", "yoru4", "yoru5", "yoru6", nullptr,
};
// Cold BGM setting 3 (BOOT 24A78). 14834 selects entry 3+5 = 105,
// with gain units of 1/128 at 56E74. The voice's step is 4 at 4B0B0.
constexpr std::array<int,5> bgm_gains{0,35,70,105,128};
constexpr int fade_step = 4;

std::uint64_t SourceFrame(std::uint64_t frame, std::uint64_t begin, std::uint64_t end) {
    return frame < end ? frame : begin + (frame - begin) % (end - begin);
}
}

TitleBgm::TitleBgm(StartupCoordinator& startup, std::filesystem::path audio_directory)
    : startup_(startup), directory_(std::move(audio_directory)) {}

const audio::PcmClip* TitleBgm::PreparedClip(int index) const noexcept {
    return index >= 0 && static_cast<size_t>(index) < clips_.size() ? clips_[index].get() : nullptr;
}

bool TitleBgm::Prepare(int index) {
    error_.clear();
    if (index < 0 || static_cast<size_t>(index) >= clips_.size() || !bank_names[index]) {
        error_ = "Unsupported music bank";
        return false;
    }
    if (clips_[index]) return true;
    int32_t id = -1;
    try {
        const auto& bank = *audio::FindOriginalClip(bank_names[index]);
        const auto riff_path = directory_ / (std::string(bank.id) + ".at3");
        std::ifstream in(riff_path, std::ios::binary | std::ios::ate);
        const auto bytes = in.tellg();
        if (bytes <= 0 || bytes > UINT32_MAX) throw std::runtime_error("Invalid or absent ATRAC3 input");
        std::vector<uint8_t> riff(static_cast<size_t>(bytes));
        in.seekg(0);
        if (!in.read(reinterpret_cast<char*>(riff.data()), bytes))
            throw std::runtime_error("Cannot read ATRAC3 input");
        auto decoded = std::make_shared<audio::PcmClip>();
        decoded->sample_rate = bank.rate;
        decoded->channels = bank.channels;
        decoded->samples.reserve(static_cast<size_t>(bank.frames * bank.channels));
        id = startup_.OpenAtrac(static_cast<uint32_t>(audio::AtracCodec::Atrac3), std::move(riff));
        if (id < 0) throw std::runtime_error("Cannot open ATRAC3 stream");
        audio::AtracPcmFrame packet;
        do {
            if (!startup_.DecodeAtrac(id, packet) || packet.sample_rate != bank.rate ||
                packet.channels != bank.channels || packet.samples.empty() ||
                packet.samples.size() % bank.channels ||
                packet.samples.size() / bank.channels > bank.frames - decoded->FrameCount())
                throw std::runtime_error("Invalid ATRAC3 packet");
            decoded->samples.insert(decoded->samples.end(), packet.samples.begin(), packet.samples.end());
        } while (!packet.end);
        if (decoded->FrameCount() != bank.frames)
            throw std::runtime_error("Incomplete ATRAC3 stream");
        const bool released = startup_.ReleaseAtrac(id);
        id = -1;
        if (!released) throw std::runtime_error("Cannot release ATRAC3 stream");
        clips_[index] = std::move(decoded);
        return true;
    } catch (const std::exception& e) {
        if (id >= 0) startup_.ReleaseAtrac(id);
        error_ = std::string(bank_names[index]) + ": " + e.what();
        return false;
    }
}

bool TitleBgm::Start(int index) try {
    if (!Prepare(index)) return false;
    if (playing_bank_ == index && Playing()) return true;
    const auto& bank = *audio::FindOriginalClip(bank_names[index]);
    audio::SampleLoop loop{static_cast<uint64_t>(bank.loop_begin), static_cast<uint64_t>(bank.loop_end)};
    auto scaled = std::make_shared<audio::PcmClip>(*clips_[index]);
    for (auto& sample : scaled->samples)
        sample = static_cast<std::int16_t>(sample * gain_ / 128);
    if (!player_.Play(std::move(scaled), loop, error_)) {
        playing_bank_ = -1;
        return false;
    }
    playing_bank_ = index;
    start_frame_ = 0;
    ducked_ = false;
    return true;
} catch (const std::exception& e) {
    error_ = e.what();
    return false;
}

bool TitleBgm::FadeOut() try {
    if (playing_bank_ < 0 || !Playing()) return true;
    // The sound-manager tick runs once per title update. 4B5EC enters
    // phase 2 at the current gain; 4B714 subtracts four on each update.
    // Render the remaining tail in software so attenuation stays per voice.
    const auto source = clips_[playing_bank_];
    const auto& bank = *audio::FindOriginalClip(bank_names[playing_bank_]);
    if (!gain_) { Stop(); return true; }
    const unsigned ticks = (gain_ + fade_step - 1) / fade_step;
    const auto tick_numerator = std::uint64_t(source->sample_rate) * 1001;
    const auto frames = (tick_numerator * ticks + 29999) / 30000;
    auto tail = std::make_shared<audio::PcmClip>();
    tail->sample_rate = source->sample_rate;
    tail->channels = source->channels;
    tail->samples.resize(static_cast<size_t>(frames * tail->channels));
    if (!player_.Pause(error_)) return false;
    std::uint32_t position = 0;
    if (!player_.ElapsedPcmFrames(position, error_)) { Stop(); return false; }
    const auto begin = static_cast<std::uint64_t>(bank.loop_begin);
    const auto end = static_cast<std::uint64_t>(bank.loop_end);
    for (std::uint64_t i = 0; i < frames; ++i) {
        const auto frame = SourceFrame(start_frame_ + position + i, begin, end);
        const int gain = std::max(0, (ducked_ ? gain_ / 2 : gain_) -
                                     fade_step * static_cast<int>(i * 30000 / tick_numerator));
        for (unsigned ch = 0; ch < tail->channels; ++ch)
            tail->samples[i * tail->channels + ch] =
                static_cast<std::int16_t>(source->samples[frame * tail->channels + ch] * gain / 128);
    }
    playing_bank_ = -1;
    return player_.Play(std::move(tail), std::nullopt, error_);
} catch (const std::exception& e) {
    error_ = e.what();
    return false;
}

bool TitleBgm::SetVolumeSetting(unsigned setting) {
    if (setting >= bgm_gains.size()) { error_ = "Invalid BGM setting"; return false; }
    if (gain_ == bgm_gains[setting]) return true;
    gain_ = bgm_gains[setting];
    return RefreshGain(ducked_, false);
}

bool TitleBgm::DuckForCharacter(bool active) {
    if (active == ducked_) return true;
    return RefreshGain(active, !active);
}

bool TitleBgm::RefreshGain(bool active, bool ramp) try {
    if (playing_bank_ < 0 || !Playing()) return true;
    const auto& bank = *audio::FindOriginalClip(bank_names[playing_bank_]);
    const auto source = clips_[playing_bank_];
    const auto begin = static_cast<std::uint64_t>(bank.loop_begin);
    const auto end = static_cast<std::uint64_t>(bank.loop_end);
    if (!player_.Pause(error_)) return false;
    std::uint32_t elapsed = 0;
    if (!player_.ElapsedPcmFrames(elapsed, error_)) { Stop(); return false; }
    const auto first = SourceFrame(start_frame_ + elapsed, begin, end);
    // 4B350 halves the current BGM setting while the SE is active. On its
    // completion 4B374 starts phase 1 from half (+4 per sound-manager tick).
    const int half = gain_ / 2;
    const std::uint64_t tick = std::uint64_t(source->sample_rate) * 1001;
    const auto ramp_frames = !ramp ? 0 : (tick * ((gain_ - half + fade_step - 1) / fade_step) + 29999) / 30000;
    const auto intro = std::max<std::uint64_t>(ramp_frames, first < begin ? begin - first : 0);
    const auto frames = intro + end - begin;
    auto clip = std::make_shared<audio::PcmClip>();
    clip->sample_rate = source->sample_rate;
    clip->channels = source->channels;
    clip->samples.resize(static_cast<size_t>(frames * clip->channels));
    for (std::uint64_t i = 0; i < frames; ++i) {
        const auto src = SourceFrame(first + i, begin, end);
        int gain = active ? half : gain_;
        if (ramp && i < ramp_frames) gain = std::min(gain_, half + fade_step * static_cast<int>(i * 30000 / tick));
        for (unsigned ch = 0; ch < clip->channels; ++ch)
            clip->samples[i * clip->channels + ch] =
                static_cast<std::int16_t>(source->samples[src * clip->channels + ch] * gain / 128);
    }
    if (!player_.Play(std::move(clip), audio::SampleLoop{intro, frames}, error_)) { Stop(); return false; }
    start_frame_ = first;
    ducked_ = active;
    return true;
} catch (const std::exception& e) {
    Stop();
    error_ = e.what();
    return false;
}

void TitleBgm::Stop() noexcept {
    player_.Stop();
    playing_bank_ = -1;
}

} // namespace dokodemo::gameplay
