#include "startup_modules_win32.h"
#include "recovered/startup_modules.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <limits>

namespace dokodemo::platform {
// Named implementation types keep MSVC RTTI stable across checkout paths.
namespace startup_bindings {
class Mixer final : public StartupHostModule {
public:
    explicit Mixer(std::string_view name) : StartupHostModule(name) {}
    bool MixStereo(const int16_t* const* voices, size_t count,
                   int16_t* output, size_t frames) override {
        if (!output || count > 32 || (count && !voices) ||
            frames > (std::numeric_limits<size_t>::max)() / 2) return false;
        for (size_t v = 0; v < count; ++v) if (!voices[v]) return false;
        for (size_t i = 0; i < frames * 2; ++i) {
            int32_t sum = 0;
            for (size_t v = 0; v < count; ++v) sum += voices[v][i];
            output[i] = static_cast<int16_t>(std::clamp(sum, -32768, 32767));
        }
        return true;
    }
};

class Interfaces final : public StartupHostModule {
public:
    explicit Interfaces(std::string_view name) : StartupHostModule(name) {}
    ~Interfaces() override { if (interfaces_) FreeMibTable(interfaces_); }
    bool Open() { return GetIfTable2(&interfaces_) == NO_ERROR; }
private:
    MIB_IF_TABLE2* interfaces_ = nullptr;
};

// Per-module reference to the local Winsock provider. Startup does not
// bind/listen, contact peers, join a group or complete PSP authentication.
class Network final : public StartupHostModule {
public:
    explicit Network(std::string_view name) : StartupHostModule(name) {}
    ~Network() override { if (started_) WSACleanup(); }
    bool Open() {
        WSADATA data{};
        started_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        return started_ && data.wVersion == MAKEWORD(2, 2);
    }
private:
    bool started_ = false;
};

// Host entropy provider reserved for the two offline authentication modules.
// The registry may release them without ever issuing an authentication call.
class OfflineAuth final : public StartupHostModule {
public:
    explicit OfflineAuth(std::string_view name) : StartupHostModule(name) {}
    ~OfflineAuth() override { if (entropy_) BCryptCloseAlgorithmProvider(entropy_, 0); }
    bool Open() { return BCryptOpenAlgorithmProvider(&entropy_, BCRYPT_RNG_ALGORITHM, nullptr, 0) >= 0; }
private:
    BCRYPT_ALG_HANDLE entropy_ = nullptr;
};
template<class T>
std::unique_ptr<StartupHostModule> OpenModule(std::string_view name) {
    auto module = std::make_unique<T>(name);
    if (!module->Open()) return {};
    return module;
}
}

std::unique_ptr<StartupHostModule> StartHostModule(std::string_view filename) {
    const auto* descriptor = recovered::FindStartupModule(filename);
    if (!descriptor) return {};
    const std::string_view name = descriptor->filename;
    using namespace startup_bindings;
    if (name == "sc_sascore.prx") return std::make_unique<Mixer>(name);
    if (name == "ifhandle.prx") return OpenModule<Interfaces>(name);
    if (name == "memab.prx" || name == "pspnet_adhoc_auth.prx") return OpenModule<OfflineAuth>(name);
    return OpenModule<Network>(name);
}
}
