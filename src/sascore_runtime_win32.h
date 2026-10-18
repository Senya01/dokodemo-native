#pragma once
#include "recovered/sascore_module_native.h"
namespace dokodemo {
class WindowsStartupBackend;
std::shared_ptr<recovered::NativeSascoreBackend> CreateWindowsSascoreBackend(
    std::shared_ptr<WindowsStartupBackend>, std::shared_ptr<recovered::NativeCodecSessionState>, recovered::NativeSascoreCode&);
uint32_t PendingWindowsSascoreCleanup() noexcept;
recovered::NativeSascoreCode RetryWindowsSascoreCleanup() noexcept;
} // namespace dokodemo
