#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace dokodemo::platform {
using AesBlock = std::array<std::uint8_t,16>;
// The save format uses CBC without padding and an all-zero IV.
void RetailAesCbc(const AesBlock& key, bool encrypt, const std::uint8_t* input,
                  std::uint8_t* output, std::size_t length);
void RetailRandomBytes(std::uint8_t* output, std::size_t length);
}
