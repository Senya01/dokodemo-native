#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dokodemo::gameplay {
using RetailFileMac = std::array<std::uint8_t,16>;
struct ProtectedRetailData {
    std::vector<std::uint8_t> bytes;
    RetailFileMac mac;
};

ProtectedRetailData ProtectRetailData(const std::vector<std::uint8_t>& request);
std::vector<std::uint8_t> OpenRetailData(const std::vector<std::uint8_t>& bytes, const RetailFileMac& expected);
void StampRetailSfo(std::vector<std::uint8_t>& sfo, std::size_t params_offset);
bool VerifyRetailSfo(const std::vector<std::uint8_t>& sfo, std::size_t params_offset);
}
