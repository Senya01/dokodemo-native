#pragma once
#include "recovered/game_filesystem_native.h"
#include "startup_memory_backend_win32.h"
#include <functional>

namespace dokodemo {
// native_coordinate is a page-aligned lower bound in the shared backend's
// Windows file namespace. It is not an exact source/PSP allocation address.
std::unique_ptr<recovered::GameFileSource> CreateWindowsGameFileSource(
    std::shared_ptr<WindowsStartupBackend> backend, std::wstring path, uint32_t native_coordinate = 0x74000000);
// URI grammar belongs to the source queue. This adapter retains the actual
// Windows backend/media root and constructs the same ordinary file owners.
std::function<recovered::GameFsStatus(const std::string&,std::unique_ptr<recovered::GameFileSource>&)>
CreateWindowsGameFileFactory(std::shared_ptr<WindowsStartupBackend> backend,std::wstring media_root,
                             uint32_t native_coordinate);
} // namespace dokodemo
