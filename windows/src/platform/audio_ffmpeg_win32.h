#pragma once
#include "recovered/audio_sessions.h"
#include <filesystem>

namespace dokodemo::audio {
// Explicit optional backend. The caller supplies the installed DLL paths.
std::unique_ptr<AtracDecoderFactory> LoadFfmpegAtracDecoder(
    const std::filesystem::path& avcodec, const std::filesystem::path& avutil);
}
