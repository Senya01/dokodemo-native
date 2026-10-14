// SPDX-License-Identifier: MIT
// Original implementation, written from FIPS 180-4 sections 4.1.2, 4.2.2,
// 5.1.1, 5.2.1, 5.3.3 and 6.2. No third-party implementation code copied.
// Specification: https://nvlpubs.nist.gov/nistpubs/fips/nist.fips.180-4.pdf
// Component license/provenance: research/portability/LICENSE and README.md.
#include "core_hash.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace dokodemo::core {
namespace {
constexpr std::array<std::uint32_t, 64> constants = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
constexpr std::uint32_t rotate(std::uint32_t word, unsigned bits) noexcept {
    return (word >> bits) | (word << (32u - bits));
}
std::uint32_t big_endian_word(const std::uint8_t* bytes) noexcept {
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
           (std::uint32_t(bytes[2]) << 8) | std::uint32_t(bytes[3]);
}
}

Sha256::Sha256() noexcept : state_{
    0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19} {}

void Sha256::block(const std::uint8_t* bytes) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) words[i] = big_endian_word(bytes + i * 4);
    for (std::size_t i = 16; i < words.size(); ++i) {
        const auto x = words[i - 15], y = words[i - 2];
        const auto small0 = rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3);
        const auto small1 = rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10);
        words[i] = words[i - 16] + small0 + words[i - 7] + small1;
    }
    auto a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    auto e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto large1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
        const auto choose = (e & f) ^ (~e & g);
        const auto first = h + large1 + choose + constants[i] + words[i];
        const auto large0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto second = large0 + majority;
        h = g; g = f; f = e; e = d + first;
        d = c; c = b; b = a; a = first + second;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const std::uint8_t* bytes, std::size_t size) {
    if (size && !bytes) throw std::invalid_argument("Nonempty SHA-256 input is null");
    constexpr auto maximum_bytes = std::numeric_limits<std::uint64_t>::max() / 8;
    if (size > maximum_bytes - bytes_) throw std::length_error("SHA-256 input reaches 2^64 bits");
    bytes_ += size;
    if (!size) return;
    if (pending_size_) {
        const auto take = std::min(size, pending_.size() - pending_size_);
        std::copy_n(bytes, take, pending_.data() + pending_size_);
        pending_size_ += take; bytes += take; size -= take;
        if (pending_size_ == pending_.size()) { block(pending_.data()); pending_size_ = 0; }
    }
    while (size >= pending_.size()) { block(bytes); bytes += pending_.size(); size -= pending_.size(); }
    if (size) { std::copy_n(bytes, size, pending_.data()); pending_size_ = size; }
}

Sha256Digest Sha256::digest() const noexcept {
    auto final = *this;
    final.pending_[final.pending_size_++] = 0x80;
    if (final.pending_size_ > 56) {
        std::fill(final.pending_.begin() + final.pending_size_, final.pending_.end(), std::uint8_t{0});
        final.block(final.pending_.data()); final.pending_size_ = 0;
    }
    std::fill(final.pending_.begin() + final.pending_size_, final.pending_.begin() + 56, std::uint8_t{0});
    const auto bits = final.bytes_ * 8;
    for (unsigned i = 0; i < 8; ++i) final.pending_[63 - i] = std::uint8_t(bits >> (i * 8));
    final.block(final.pending_.data());
    Sha256Digest result{};
    for (std::size_t i = 0; i < final.state_.size(); ++i)
        for (unsigned j = 0; j < 4; ++j) result[4 * i + j] = std::uint8_t(final.state_[i] >> (24 - 8 * j));
    return result;
}

Sha256Digest sha256(const std::uint8_t* bytes, std::size_t size) {
    Sha256 hash; hash.update(bytes, size); return hash.digest();
}
std::string hex_digest(const Sha256Digest& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text(digest.size() * 2, '0');
    for (std::size_t i = 0; i < digest.size(); ++i) {
        text[2 * i] = digits[digest[i] >> 4]; text[2 * i + 1] = digits[digest[i] & 15];
    }
    return text;
}
std::string sha256_hex(const std::uint8_t* bytes, std::size_t size) {
    return hex_digest(sha256(bytes, size));
}
} // namespace dokodemo::core
