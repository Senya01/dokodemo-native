#pragma once
#include "recovered/native_codec_workspace.h"
#include <string>
namespace dokodemo {
class WindowsStartupBackend;
std::shared_ptr<recovered::NativeCodecWorkspaceBackend> CreateWindowsNativeCodecWorkspaceBackend(
    std::shared_ptr<WindowsStartupBackend>, std::shared_ptr<recovered::NativeCodecSessionState>,
    const std::wstring& dependency_root, recovered::NativeCodecBackendStatus&);
// Owning delayed cleanup is observable/retryable if a standalone backend owner
// is dropped after an explicit Free failure. No abandoned pimpl or hidden leak.
uint32_t PendingWindowsNativeCodecWorkspaceCleanup() noexcept;
recovered::NativeCodecBackendStatus RetryWindowsNativeCodecWorkspaceCleanup() noexcept;
} // namespace dokodemo
