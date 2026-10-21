#pragma once
#include "recovered/game_filesystem_native.h"
#include "startup_memory_backend_win32.h"

namespace dokodemo {
std::unique_ptr<recovered::GameFileSource> CreateWindowsGameFileSource(
    std::shared_ptr<WindowsStartupBackend> backend, std::wstring path, uint32_t native_coordinate = 0x74000000);
} // namespace dokodemo
