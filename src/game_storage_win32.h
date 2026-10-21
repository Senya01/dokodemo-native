#pragma once
#include "recovered/game_storage_native.h"
#include <string>
#include <vector>
namespace dokodemo {
struct WindowsGameStorageOptions {
    std::wstring root;
    std::vector<std::wstring> forbidden_roots;
    bool read_only = false, unavailable = false;
};
std::unique_ptr<recovered::GameStorage> CreateWindowsGameStorage(WindowsGameStorageOptions options);
} // namespace dokodemo
