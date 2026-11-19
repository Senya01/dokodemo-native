#include "first_frame.h"
#include "display.h"
#include "core_hash.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
namespace {
using Bytes = std::vector<std::uint8_t>;

Bytes ReadAsset(const std::filesystem::path& path, std::size_t size, const char* digest) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() != static_cast<std::streamoff>(size))
        throw std::runtime_error("Missing or incorrect size: " + path.filename().string());
    Bytes bytes(size);
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)) ||
        core::sha256_hex(bytes.data(), bytes.size()) != digest)
        throw std::runtime_error("Invalid game asset: " + path.filename().string());
    return bytes;
}

unsigned U16(const Bytes& b, std::size_t p) {
    return b.at(p) | (unsigned(b.at(p + 1)) << 8);
}
unsigned U32(const Bytes& b, std::size_t p) {
    return U16(b, p) | (U16(b, p + 2) << 16);
}
int S16(const Bytes& b, std::size_t p) {
    const auto value = U16(b, p);
    return value < 32768 ? static_cast<int>(value) : static_cast<int>(value) - 65536;
}
}

bool StartupLogos::Load(const std::filesystem::path& directory, std::string& error) {
    try {
        const auto cb = ReadAsset(directory / "TITLE.CB2A", 37363,
            "04635d29d45ff148c55e3acc3df40dcb0d261bda1146772ecec3b5d00ebc6bd4");
        return Load(cb, directory, error);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool StartupLogos::Load(const Bytes& cb, const std::filesystem::path& directory, std::string& error) {
    try {
        if (cb.size() != 37363 || core::sha256_hex(cb.data(), cb.size()) !=
            "04635d29d45ff148c55e3acc3df40dcb0d261bda1146772ecec3b5d00ebc6bd4")
            throw std::runtime_error("Unsupported TITLE.CB2A");
        // The game activates animation 5 through BOOT 30F40..31084. Records
        // are 52 bytes per animation, with five track count/pointer pairs.
        const auto animation = U32(cb, 24) + 5 * 52;
        auto keys = [&](unsigned node, unsigned channel) {
            std::vector<Key> result;
            const auto pair = animation + channel * 8;
            const unsigned tracks = 0u - U32(cb, pair);
            for (unsigned i = 0; i < tracks; ++i) {
                const auto track = U32(cb, pair + 4) + i * 8;
                if (U16(cb, track) != node) continue;
                const int count = -S16(cb, track + 2);
                for (int k = 0; k < count; ++k) {
                    const auto key = U32(cb, track + 4) + static_cast<unsigned>(k) * 12;
                    const auto packed = U32(cb, key);
                    result.push_back({U32(cb, key + 8), channel == 4 ? packed >> 24 : packed});
                }
            }
            if (result.empty()) throw std::runtime_error("Missing startup animation track");
            return result;
        };
        std::vector<Logo> loaded;
        auto logo = [&](unsigned node, unsigned sprite, unsigned width, unsigned height,
                        const char* filename, const char* digest) {
            const auto record = U32(cb, 8) + (sprite - 1) * 8;
            const int x = static_cast<int>(Display::width / 2) + S16(cb, record);
            const int y = static_cast<int>(Display::height / 2) + S16(cb, record + 2);
            if (x < 0 || y < 0 || static_cast<unsigned>(x) + width > Display::width ||
                static_cast<unsigned>(y) + height > Display::height)
                throw std::runtime_error("Unsupported startup sprite extent");
            loaded.push_back({width, height, static_cast<unsigned>(x), static_cast<unsigned>(y),
                ReadAsset(directory / filename, width * height * 4u, digest), keys(node, 4),
                keys(node, 0), sprite});
        };
        logo(272, 52, 416, 51, "SCELOGO.rgba",
             "16b4edab4c0434d9c491cbf55504984254328c3c08c249992d061886b1fdd36d");
        logo(273, 53, 192, 71, "BEXIDELOGO.rgba",
             "87a9af6a708b25f61b143c5855a17abaa866ed0e1cbe300b77c4f8cdc2a31933");
        logos_ = std::move(loaded);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool StartupLogos::Render(unsigned frame, Bytes& vram, std::size_t offset) const {
    constexpr std::size_t pitch = 512 * 4;
    constexpr std::size_t needed = (Display::height - 1) * pitch + Display::width * 4;
    if (logos_.empty() || frame < 1 || frame >= 320 || offset > vram.size() ||
        needed > vram.size() - offset) return false;
    // Sibling 271 is the opaque black background during these frames.
    for (unsigned y = 0; y < Display::height; ++y) {
        auto* row = vram.data() + offset + y * pitch;
        for (unsigned x = 0; x < Display::width; ++x)
            std::fill_n(row + x * 4, 3, std::uint8_t{0});
    }
    for (const Logo& logo : logos_) {
        unsigned sprite = 0;
        for (const auto& key : logo.sprites) {
            if (key.frame > frame) break;
            sprite = key.value;
        }
        if (sprite != logo.sprite) continue;
        unsigned alpha = logo.colors.back().value;
        for (std::size_t i = 1; i < logo.colors.size(); ++i) {
            const auto& next = logo.colors[i];
            if (frame > next.frame) continue;
            const auto& previous = logo.colors[i - 1];
            // The signed change truncates toward zero before adding to the
            // starting channel. Fade-out therefore differs from rounding
            // down the interpolated positive value.
            const int delta = static_cast<int>(next.value) - static_cast<int>(previous.value);
            const int elapsed = static_cast<int>(frame - previous.frame);
            const int span = static_cast<int>(next.frame - previous.frame);
            alpha = static_cast<unsigned>(static_cast<int>(previous.value) + delta * elapsed / span);
            break;
        }
        const auto opacity = alpha * 255u / 128u;
        for (unsigned y = 0; y < logo.height; ++y) {
            auto* dst = vram.data() + offset + (logo.y + y) * pitch + logo.x * 4;
            const auto* src = logo.pixels.data() + y * logo.width * 4;
            for (unsigned x = 0; x < logo.width; ++x) {
                // These two opaque textures blend over black. Alpha of the
                // CB2A modulation is expressed on a 0..128 scale.
                for (unsigned c = 0; c < 3; ++c)
                    dst[x * 4 + c] = static_cast<std::uint8_t>((src[x * 4 + c] * opacity + 127) / 255);
            }
        }
    }
    return true;
}
}
