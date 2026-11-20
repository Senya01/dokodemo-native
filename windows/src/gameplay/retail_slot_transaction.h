#pragma once

#include "retail_slot_files.h"
#include <functional>

namespace dokodemo::gameplay::detail {

// Boundaries of the directory transaction, also used to exercise interrupted writes.
enum class SlotWritePhase { create_stage, write_file, validate_stage, preserve_previous, publish, retire };
using SlotWriteObserver = std::function<void(SlotWritePhase, const std::filesystem::path&)>;

void CommitRetailSlot(const std::filesystem::path& slot, const std::filesystem::path& game,
                      const std::vector<std::uint8_t>& request, const SlotWriteObserver& observe);

} // namespace dokodemo::gameplay::detail
