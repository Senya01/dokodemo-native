#include "startup_modules.h"

namespace dokodemo::recovered {
namespace {
// BOOT 4F5E4..4F6C4 and table EA4D0 read by 4F6C8. Digests identify
// the actual (possibly encoded) files; firmware exports are a separate ABI.
constexpr StartupModuleDescriptor Modules[]{
    {"sc_sascore.prx", 5920, "f4cbbc90733c6b48b5a6b0d0c8b827f3478333b2a2d0c87b845231ab1916a0f5", 0x4F660, 1},
    {"ifhandle.prx", 10832, "c2803caab3ad288f91e03f5ac7cc5c5c05d8f945f92cfc55ca42da60d6fd916e", 0x4F680, 1},
    {"pspnet.prx", 60256, "ab8b28e24f1d17c6bfffec47b6d9717665fab78b0de11d3d2711ad25b5c65404", 0x4F6A0, 0},
    {"memab.prx", 15216, "07f07637b64268df841892adf9e21ccc418c5c8641e60e44447ba74b1c31a1a8", 0x4F71C, 1},
    {"pspnet_adhoc_auth.prx", 10816, "c67770d8199bdd510fd932546285f1fe87674afd6426a5f9f2212b1901443f7f", 0x4F71C, 1},
    {"pspnet_adhoc.prx", 40140, "47c98edb5d62e069bed77eb5afd69e3877ae3991a163876ea6012536a223826f", 0x4F71C, 0},
    {"pspnet_adhocctl.prx", 36416, "3ac8e2211a9b66fe5161a3eac61ec09e258b474068d25bed4adefd5a24bc0f22", 0x4F71C, 0},
};
}
const StartupModuleDescriptor* FindStartupModule(std::string_view filename) noexcept {
    for (const auto& m : Modules) if (filename == m.filename) return &m;
    return nullptr;
}
const StartupModuleDescriptor* FindStartupModule(std::string_view sha256, uint32_t size) noexcept {
    for (const auto& m : Modules) if (size == m.bytes && sha256 == m.sha256) return &m;
    return nullptr;
}
}
