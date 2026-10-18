#pragma once
#include "recovered/memab_module_native.h"
#include <atomic>
namespace dokodemo {
std::shared_ptr<recovered::NativeMemabBackend> CreateWindowsMemabBackend(
    std::shared_ptr<std::atomic<bool>>, recovered::NativeMemabCode&);
uint32_t PendingWindowsMemabCleanup() noexcept;
recovered::NativeMemabCode RetryWindowsMemabCleanup() noexcept;
} // namespace dokodemo
