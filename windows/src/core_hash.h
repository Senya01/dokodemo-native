// SPDX-License-Identifier: MIT
// Original implementation for dokodemo-native. See research/portability/LICENSE
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dokodemo::core {
using Sha256Digest = std::array<std::uint8_t, 32>;

// Byte-oriented SHA-256 (FIPS 180-4), without platform APIs or external libraries.
// update(nullptr, 0) is valid; a nonempty null input or a total >= 2^64 bits
// throws. digest() observes a snapshot; more input may subsequently be appended.
// A single instance is mutable and is not intended for concurrent updates.
class Sha256 {
public:
    Sha256() noexcept;
    void update(const std::uint8_t* bytes, std::size_t size);
    Sha256Digest digest() const noexcept;
private:
    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> pending_{};
    std::uint64_t bytes_ = 0;
    std::size_t pending_size_ = 0;
    void block(const std::uint8_t* bytes) noexcept;
};

Sha256Digest sha256(const std::uint8_t* bytes, std::size_t size);
std::string hex_digest(const Sha256Digest& digest);
std::string sha256_hex(const std::uint8_t* bytes, std::size_t size);
} // namespace dokodemo::core
