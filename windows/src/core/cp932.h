// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <string_view>

namespace dokodemo::core {
struct Cp932Count {
    bool valid = true;
    std::size_t code_units = 0;
    std::size_t error_offset = 0;
};

// Counts the accepted CP932 code units used by the immutable DIC validator.
// Embedded NUL is a counted byte, as in an explicitly sized Windows input.
// No Unicode conversion or glyph semantics. Invalid input reports its first
// unaccepted byte. The acceptance table is pinned to the recorded Windows
// oracle; see research/portability/cp932-oracle.json and README.md.
Cp932Count count_cp932(std::string_view bytes) noexcept;
} // namespace dokodemo::core
