#pragma once
#include <string>
#include <string_view>

namespace dokodemo {
// Named Windows committed-text adapter,not the PSP NJ prediction engine.
// No best-fit/default replacement; round trip must preserve every UTF16 scalar.
bool EncodeGameText932(std::wstring_view committed, std::string &cp932, std::string &error);
} // namespace dokodemo
