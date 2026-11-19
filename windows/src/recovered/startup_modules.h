#pragma once
#include <cstdint>
#include <string_view>

namespace dokodemo::recovered {
struct StartupModuleDescriptor {
    const char* filename;
    uint32_t bytes;
    const char* sha256;
    uint32_t callsite;
    uint32_t kind;
};
const StartupModuleDescriptor* FindStartupModule(std::string_view filename) noexcept;
const StartupModuleDescriptor* FindStartupModule(std::string_view sha256, uint32_t size) noexcept;
}
