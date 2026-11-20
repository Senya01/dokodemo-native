#pragma once
#include "ksc_save_graph.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <string_view>

namespace dokodemo::gameplay {

// G44C8, the current seven diary-script fields registered by GAME29.
// GAME71 consumes the current ID and text into a separate dated bank.
class RoomMessages {
public:
    void Clear();
    void ResetActor();
    bool DescriptorUsed(unsigned id) const;
    void PrepareUsedDescriptorsForSave();
    KscSaveValue UsedDescriptorsSaveTable() const;
    void RestoreUsedDescriptors(const KscSaveValue& ref);
    bool RecordDiary(unsigned descriptor_id, std::string_view text, unsigned priority);
    std::uint32_t ContactSelectionBoundary() const;
    KscSaveValue SaveTable() const;
    static void ValidateSaveTable(const KscSaveValue& ref);
    void RestoreSaveTable(const KscSaveValue& ref);
private:
    static constexpr unsigned descriptor_limit=138; // INU's largest source table.
    bool AdmitsPriority(unsigned candidate) const;
    std::array<KscSaveValue,7> fields_{};
    std::bitset<descriptor_limit> used_; // Bit 24 in each descriptor's first integer.
    std::array<std::uint32_t,5> saved_used_{}; // Persist the original NIL vs integer word tags.
};

} // namespace dokodemo::gameplay
