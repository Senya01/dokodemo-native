#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace dokodemo::recovered {
// The image's .data seed1 is separate from the game's RTC GetTime_t setter.
enum class RngSeedOrigin : uint8_t {
    Unavailable, InitialImageModel, InitializerTimeT32, VerifiedCurrentState64
};
struct RngSnapshot {
    bool seed_available = false, scratch_available = false;
    RngSeedOrigin origin = RngSeedOrigin::Unavailable;
    uint64_t seed = 0, scenario_identity = 0;
    std::string provenance;
    // Includes the physical cursor word; candidate halfwords alias this area.
    std::array<uint8_t, 0x4004> scratch{};
};
class RngDomainError : public std::runtime_error {
public:
    RngDomainError(uint32_t pc, const char* reason);
    uint32_t source_pc;
};
// Copies share storage until mutation. Independent scenario owners must be
// initialized separately. An explicit shared-owner contract may instead hold
// the same RngNativeOwner object. No process-global RNG is installed here.
class RngNativeOwner {
public:
    bool Available() const noexcept;
    uint64_t CurrentSeed64()const noexcept{return storage_?storage_->seed:0;}
    RngSnapshot Snapshot() const;
    void ImportCurrentState64(uint64_t state, uint64_t scenario_identity,
                              std::string provenance);
    // Exact C321C setter effect, conditional on the supplied backend result.
    // This does not reconstruct calls between startup and a later checkpoint.
    void ApplyInitializerTimeT32(uint32_t time_t_word, uint64_t scenario_identity,
                               std::string provenance);
    void ImportInitialImageModel(uint64_t scenario_identity, std::string provenance);
    void ImportScratch(const std::array<uint8_t, 0x4004>& bytes);
    uint32_t NextC3234();
    uint32_t ScratchWord(uint32_t source_va) const;
    void ScratchStore(uint32_t source_va, uint32_t value, uint32_t source_pc);
    void ScratchHalfStore(uint32_t source_va, uint16_t value, uint32_t source_pc);
private:
    void Detach();
    std::shared_ptr<RngSnapshot> storage_;
};
// Words include the header at[0]. Borrowed views preserve list/ring aliases;
// the caller proves their mapped writable/readable extent and lifetime.
struct RngListView { const uint32_t* words = nullptr; size_t word_count = 0; };
struct RngExclusionView { uint32_t* words = nullptr; size_t word_count = 0; };
// Source77404 list-tag9 domain. Returns a one-based index for caller7581C.
// A null exclusion view means raw a1=0 and leaves scratch unchanged. Non-null
// exclusions retain source hash chains, candidate halfwords, all-excluded
// fallback and ring-header updates. Other numeric/string input branches are
// explicit cuts. Errors may retain partial source-like seed/scratch effects;
// StartupProbe uses its existing candidate transaction for protective refusal.
uint32_t RngSelect77404(RngNativeOwner& owner, RngListView list,
                       RngExclusionView exclusion = {});
} // namespace dokodemo::recovered
