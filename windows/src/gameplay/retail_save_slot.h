#pragma once

#include "retail_save_profile.h"
#include "ksc_save_graph.h"
#include <filesystem>

namespace dokodemo::gameplay {

enum class RetailSlotStatus { absent, unreadable, unrecognized, protected_slot };
enum class LoadedBootRoute { invalid_header, unsupported_calendar, clock_notice, title_11, resume_15 };

// Contents on the game side of the PSP utility call, before disk protection.
inline constexpr std::size_t kRetailUtilitySize = 0x40000;
struct RetailUtilityContents {
    RetailSaveProfile profile;
    KscSaveValue actor;
    std::size_t graph_size;
    std::vector<std::uint8_t> word_history;
};

std::vector<std::uint8_t> BuildRetailUtilityRequest(const RetailSaveProfile& profile,
                                                    const KscSaveValue& actor,
                                                    const std::vector<std::uint8_t>& word_history = {});
RetailUtilityContents DecodeRetailUtilityRequest(const std::vector<std::uint8_t>& request,
                                                const RetailSaveProfile& current_profile);

// Inspect only the package's single PSP slot. Recognition does not decrypt or
// authenticate DATA.BIN and cannot authorize a transition to the saved room.
RetailSlotStatus InspectRetailSaveSlot(const std::filesystem::path& slot_directory);
const char* RetailSlotReadBoundary(RetailSlotStatus status);

// BOOT 190DC..19184 consumes the decoded utility header. This operation never
// accepts a disk path or the protected DATA.BIN as its input.
bool IsLoadedTitleClockCurrent(const RetailSaveProfile& profile, const RetailSaveProfile::Calendar& now);
LoadedBootRoute SelectLoadedBootRoute(const RetailSaveProfile& profile,
                                      const RetailSaveProfile::Calendar& now);

} // namespace dokodemo::gameplay
