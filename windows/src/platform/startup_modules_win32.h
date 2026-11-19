#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace dokodemo::platform {
// Game startup uses host module registration. No PSP module entry is executed.
class StartupHostModule {
public:
    virtual ~StartupHostModule() = default;
    std::string_view Name() const noexcept { return name_; }
    // Host mixer for a block of signed interleaved stereo PCM, one input
    // pointer per voice. Network bindings do not implement this operation.
    virtual bool MixStereo(const int16_t* const*, size_t, int16_t*, size_t) { return false; }
protected:
    explicit StartupHostModule(std::string_view name) : name_(name) {}
private:
    std::string_view name_; // references a canonical static module descriptor
};
std::unique_ptr<StartupHostModule> StartHostModule(std::string_view filename);
}
