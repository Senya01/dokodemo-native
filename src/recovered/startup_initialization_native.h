#pragma once
#include "../startup_memory_backend_win32.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace dokodemo::recovered {
struct StartupDataRelocation {
    uint32_t offset = 0, type = 0, location_segment = 0, target_segment = 0;
};
struct StartupSparseFileData {
    uint32_t module_offset = 0;
    std::vector<uint8_t> bytes;
};
struct StartupModuleDataInput {
    // Explicit NEW native mapping, not a historic PSP module base.
    uint32_t module_base = 0;
    std::vector<uint8_t> PH1_file_bytes;
    std::vector<StartupSparseFileData> sparse_PH0_file_data;
    std::vector<StartupDataRelocation> relocations;
};
enum class StartupModuleDataCode { Ok, Geometry, InputUnavailable, UnsupportedRelocation, NativeFailure, Verification };
struct StartupModuleDataEffect {
    enum class Origin { FileBytes, Relocation, ExplicitTailWrite };
    Origin origin = Origin::FileBytes;
    uint32_t module_offset = 0, size = 0, before_word = 0, after_word = 0;
};
class FreshStartupModuleData;
struct StartupModuleDataLoad {
    StartupModuleDataCode code = StartupModuleDataCode::Ok;
    StartupHostStatus host_status;
    std::unique_ptr<FreshStartupModuleData> owner;
};
// DATA-only native action. Only22 explicitly supplied PH0 DATA bytes are
// written; every other PH0/gap byte stays UNKNOWN. This is no executable loader.
class FreshStartupModuleData final {
public:
    static StartupModuleDataLoad Load(const StartupModuleDataInput& input);
    ~FreshStartupModuleData();
    StartupAllocationBorrow Borrow(StartupHostStatus& status);
    StartupNativeDescription Description();
    StartupNativeBytes ReadKnown(uint32_t module_offset, uint32_t count);
    const std::vector<StartupModuleDataEffect>& Effects() const { return effects_; }
    std::vector<StartupHostCall> HostCalls() const;
    // Same live owning service for a separately reviewed source frame/import
    // bridge. Borrowed reference; this DATA owner must outlive every use.
    WindowsStartupBackend& Backend() { return *backend_; }
    StartupHostStatus Close();
private:
    std::unique_ptr<WindowsStartupBackend> backend_;
    StartupNativeBlock block_;
    std::vector<StartupModuleDataEffect> effects_;
};
} // namespace dokodemo::recovered
