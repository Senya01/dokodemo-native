// SPDX-License-Identifier: MIT
// Deliberately Windows-only external oracle. This file is not a core source.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "core_hash.h"
#include "core/cp932.h"
#include "script_vm.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
struct HashVector { std::size_t size; std::uint32_t seed; const char* expected; };
#include "core_hash_vectors.inc"
void require(bool good, const char* text) { if (!good) throw std::runtime_error(text); }
std::uint32_t step(std::uint32_t value) {
    value ^= value << 13; value ^= value >> 17; value ^= value << 5; return value;
}
Bytes binary(std::size_t size, std::uint32_t seed) {
    Bytes result(size);
    for (auto& byte : result) { seed = step(seed); byte = std::uint8_t(seed); }
    return result;
}
struct Algorithm {
    BCRYPT_ALG_HANDLE value = nullptr;
    Algorithm() { require(BCryptOpenAlgorithmProvider(&value, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "BCrypt provider failed"); }
    ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;
};
struct Hash {
    BCRYPT_HASH_HANDLE value = nullptr;
    ~Hash() { if (value) BCryptDestroyHash(value); }
};
dokodemo::core::Sha256Digest bcrypt(const Algorithm& algorithm, const Bytes& bytes) {
    require(bytes.size() <= std::numeric_limits<ULONG>::max(), "Oracle input too large");
    DWORD object_size = 0, used = 0;
    require(BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &used, 0) >= 0, "BCrypt object query failed");
    Bytes object(object_size); Hash hash;
    require(BCryptCreateHash(algorithm.value, &hash.value, object.data(), object_size, nullptr, 0, 0) >= 0, "BCrypt create failed");
    if (!bytes.empty()) require(BCryptHashData(hash.value, const_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()), 0) >= 0, "BCrypt data failed");
    dokodemo::core::Sha256Digest result{};
    require(BCryptFinishHash(hash.value, result.data(), static_cast<ULONG>(result.size()), 0) >= 0, "BCrypt finish failed");
    return result;
}
std::size_t cp932_comparisons = 0;
void cp932_check(const std::string& bytes) {
    const auto expected = bytes.empty() ? 0 : MultiByteToWideChar(932, MB_ERR_INVALID_CHARS,
        bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    const auto result = dokodemo::core::count_cp932(bytes);
    const auto accepted = expected > 0 || bytes.empty();
    require(result.valid == accepted, "Portable CP932 acceptance differs from Windows");
    if (accepted) require(result.code_units == static_cast<std::size_t>(expected), "Portable CP932 length differs from Windows");
    ++cp932_comparisons;
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: core-windows-oracle <assets/native directory>");
        const std::filesystem::path original = std::filesystem::path(argv[1]) / "scripts/original";
        const Algorithm algorithm;
        std::size_t sha_comparisons = 0;
        for (const auto& vector : hash_vectors) {
            const auto bytes = binary(vector.size, vector.seed);
            require(dokodemo::core::sha256(bytes.data(), bytes.size()) == bcrypt(algorithm, bytes), "SHA-256 differs from BCrypt");
            ++sha_comparisons;
        }
        Bytes all_bytes(256);
        for (unsigned i = 0; i < 256; ++i) all_bytes[i] = std::uint8_t(i);
        require(dokodemo::core::sha256(all_bytes.data(), all_bytes.size()) == bcrypt(algorithm, all_bytes), "All-byte SHA input differs from BCrypt");
        ++sha_comparisons;
        for (unsigned i = 0; i < 256; ++i) cp932_check(std::string(1, static_cast<char>(i)));
        for (unsigned i = 0; i < 65536; ++i) {
            const char pair[] = {static_cast<char>(i >> 8), static_cast<char>(i & 255)};
            cp932_check(std::string(pair, 2));
        }
        std::uint32_t seed = 0xd0c0de17;
        for (unsigned i = 0; i < 512; ++i) {
            seed = step(seed); const auto bytes = binary(seed % 129, seed);
            cp932_check(std::string(bytes.begin(), bytes.end()));
        }
        for (const char* name : {"INU", "KAERU", "NEKO", "ROBO", "USAGI"}) {
            for (const char* extension : {".KSC", ".DIC"}) {
                std::ifstream file(original / (std::string(name) + extension), std::ios::binary);
                require(bool(file), "Missing original Windows oracle fixture");
                const Bytes bytes(std::istreambuf_iterator<char>(file), {});
                require(dokodemo::core::sha256(bytes.data(), bytes.size()) == bcrypt(algorithm, bytes), "Original fixture hash differs from BCrypt");
                ++sha_comparisons;
            }
            dokodemo::script::Dictionary dictionary; dokodemo::script::Diagnostic error;
            require(dictionary.load_original(original / (std::string(name) + ".DIC"), error), "Dictionary did not load for CP932 oracle");
            for (const auto& record : dictionary.records()) {
                cp932_check(record.written_cp932); cp932_check(record.reading_cp932);
                cp932_check(record.alternate_cp932); cp932_check(record.initial_cp932);
            }
        }
        std::cout << "{\"test\":\"windows_oracles\",\"sha256_bcrypt_comparisons\":" << sha_comparisons
                  << ",\"cp932_single_inputs\":256,\"cp932_pair_inputs\":65536,\"cp932_random_inputs\":512,"
                  << "\"cp932_dictionary_fields\":3240,\"cp932_comparisons\":" << cp932_comparisons << ",\"passed\":true}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
