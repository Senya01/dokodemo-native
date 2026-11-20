#include "title_effects.h"
#include "core_hash.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {

TitleEffects::TitleEffects(std::filesystem::path directory) : directory_(std::move(directory)) {}

void TitleEffects::SetVolumeSetting(unsigned setting) {
    if (setting >= 5) throw std::out_of_range("Invalid SE setting");
    volume_setting_ = setting;
}

bool TitleEffects::Play(int sequence) try {
    error_.clear();
    if (sequence < 0 || static_cast<std::size_t>(sequence) >= clips_.size()) {
        error_ = "Unsupported menu SE_SYS sequence";
        return false;
    }
    // BOOT 14834 selector 0, D85B8. Packaged WAVs already carry the default gain.
    constexpr unsigned gains[]{0, 15, 45, 70, 105};
    if (volume_setting_ != 3)
        return PlayNote(5, 0, sequence, gains[volume_setting_], sequence == 3);
    auto& clip = clips_[sequence];
    if (!clip) {
        const auto name = "SE_SYS-" + std::to_string(sequence) + ".wav";
        if (!audio::LoadPcm16Wave(directory_ / name, clip, error_)) {
            error_ = name + ": " + error_;
            return false;
        }
    }
    if (sequence == 3) return character_.Play(clip, std::nullopt, error_);
    return PlayOneShot(clip);
} catch (const std::exception& e) {
    error_ = e.what();
    return false;
}

bool TitleEffects::PlayOneShot(std::shared_ptr<const audio::PcmClip> clip) {
    for (auto& voice : menu_)
        if (!voice->IsPlaying()) return voice->Play(clip, std::nullopt, error_);
    menu_.push_back(std::make_unique<audio::Win32Player>());
    return menu_.back()->Play(clip, std::nullopt, error_);
}

void TitleEffects::LoadRoomNotes() {
    std::ifstream in(directory_ / "room-effects.bin", std::ios::binary | std::ios::ate);
    const auto size = in.tellg();
    if (size < 8 || size > 16 * 1024 * 1024)
        throw std::runtime_error("Missing or invalid room-effects.bin");
    in.seekg(0);
    in.exceptions(std::ios::failbit | std::ios::badbit);
    std::array<char,4> tag{};
    in.read(tag.data(), tag.size());
    auto word = [&]() {
        std::uint32_t value;
        in.read(reinterpret_cast<char*>(&value), sizeof(value));
        return value;
    };
    const bool has_absent_keys = std::memcmp(tag.data(), "RSE2", 4) == 0;
    const auto count = word();
    if ((!has_absent_keys && std::memcmp(tag.data(), "RSE1", 4)) || !count || count > 256)
        throw std::runtime_error("Invalid room effect bank header");
    auto data_end = size;
    if (has_absent_keys) {
        if (size < 44) throw std::runtime_error("Truncated room effect bank");
        core::Sha256Digest digest;
        data_end -= static_cast<std::streamoff>(digest.size());
        std::vector<std::uint8_t> body(static_cast<size_t>(data_end));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(body.data()), body.size());
        in.read(reinterpret_cast<char*>(digest.data()), digest.size());
        if (core::sha256(body.data(), body.size()) != digest)
            throw std::runtime_error("Room effect bank checksum mismatch");
        in.seekg(8);
    }
    decltype(room_notes_) notes;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::array<unsigned,3> key{word(), word(), word()};
        Note note{word(), word(), {}};
        const auto frames = word();
        if (key[0] > 5 || key[1] > 4 || key[2] > 255 || !note.rate || note.rate > 192000 ||
            note.level > 4096 || !frames || in.tellg() > data_end ||
            std::uint64_t(frames) * 2 > std::uint64_t(data_end - in.tellg()))
            throw std::runtime_error("Invalid room effect note");
        note.samples.resize(frames);
        in.read(reinterpret_cast<char*>(note.samples.data()), frames * 2);
        if (!notes.emplace(key,std::move(note)).second)
            throw std::runtime_error("Duplicate room effect note");
    }
    if (has_absent_keys) {
        const auto count_absent = word();
        if (count_absent > 256 || data_end - in.tellg() != std::streamoff(count_absent) * 12)
            throw std::runtime_error("Invalid absent room effect list");
        for (std::uint32_t i = 0; i < count_absent; ++i) {
            const std::array<unsigned,3> key{word(), word(), word()};
            if (key[0] > 5 || key[1] > 255 || key[2] > 255 || !notes.emplace(key,std::nullopt).second)
                throw std::runtime_error("Invalid absent room effect key");
        }
    }
    if (in.tellg() != data_end) throw std::runtime_error("Extra data after room effect bank");
    room_notes_ = std::move(notes);
}

bool TitleEffects::SetSuspended(bool suspended) {
    bool ok=character_.SetSuspended(suspended, error_);
    for (const auto& voice : menu_)
        if (!voice->SetSuspended(suspended, error_)) ok=false;
    return ok;
}

bool TitleEffects::Play(const RoomSound& request) try {
    if (!request.scaled && request.bank == 5 && request.group == 0 && request.sequence < clips_.size())
        return Play(static_cast<int>(request.sequence));
    error_.clear();
    if (!request.scaled || request.volume > 255) {
        error_ = "Unsupported room sound volume mode";
        return false;
    }
    // 4AD30 scales the event volume using the live SE setting.
    return PlayNote(request.bank, request.group, request.sequence,
                    std::min(128u, (request.volume / 4) * volume_setting_), false);
} catch (const std::exception& e) {
    error_ = e.what();
    return false;
}

bool TitleEffects::PlayNote(unsigned bank, unsigned group, unsigned sequence, unsigned gain, bool character) {
    if (room_notes_.empty()) LoadRoomNotes();
    const auto it = room_notes_.find({bank, group, sequence});
    if (it == room_notes_.end()) {
        error_ = "Unknown room sound note";
        return false;
    }
    // 88E8's motion callback discards the sound handle. A source lookup
    // which yields null starts no note; unknown requests still fail above.
    if (!it->second) return true;
    const auto& note = *it->second;
    const int level = static_cast<int>((note.level * gain / 128) * 2896 / 4096);
    auto clip = std::make_shared<audio::PcmClip>();
    clip->channels = 2;
    clip->sample_rate = note.rate;
    clip->samples.reserve(note.samples.size() * 2);
    for (auto raw : note.samples) {
        const auto sample = static_cast<std::int16_t>(raw * level >> 12);
        clip->samples.push_back(sample);
        clip->samples.push_back(sample);
    }
    if (character) return character_.Play(std::move(clip), std::nullopt, error_);
    return PlayOneShot(std::move(clip));
}

} // namespace dokodemo::gameplay
