#pragma once
#include "ksc_save_graph.h"
#include <array>

namespace dokodemo::gameplay {
// The local G58D4 display row and G59CD points persist independently. Opening
// the local shiritori menu or cancelling a meeting does not award a rank.
class RoomExchangeSave {
public:
    using Row = std::array<std::uint32_t, 10>;
    const Row& item_row() const { return item_; }
    int points() const { return points_; }
    int rank() const;
    void ClearModelHandle();
    void SetModelHandle(int handle);
    KscSaveValue ItemSaveTable() const;
    KscSaveValue PointsSaveWord() const;
    void Restore(const KscSaveValue& item_ref, const KscSaveValue& points_ref);
private:
    Row item_{}; // NEKO initial table: ten NILs.
    int points_ = 0;
};
}
