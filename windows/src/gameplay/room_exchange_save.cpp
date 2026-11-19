#include "room_exchange_save.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr std::uint32_t integer = 0x20000000u;
const KscSaveValue& Target(const KscSaveValue& ref) {
    if (ref.word != 0xb0000000u || !ref.text.empty() || ref.children.size() != 1)
        throw std::invalid_argument("Invalid exchange global reference");
    return ref.children.front();
}
bool Scalar(const KscSaveValue& v) {
    return v.text.empty() && v.children.empty();
}
}

int RoomExchangeSave::rank() const {
    // 8BD7F, used by 8BE51 before the positive award is added to G59CD.
    constexpr std::array thresholds{20, 50, 100, 250, 500, 1000};
    return int(std::upper_bound(thresholds.begin(), thresholds.end(), points_) - thresholds.begin()) - 1;
}
void RoomExchangeSave::ClearModelHandle() { item_[6] = 0; }
void RoomExchangeSave::SetModelHandle(int handle) {
    item_[6] = integer | (std::uint32_t(handle) & 0x0fffffffu);
}

KscSaveValue RoomExchangeSave::ItemSaveTable() const {
    KscSaveValue array{0x9000000au, {}, {}};
    for (const auto word : item_) array.children.push_back({word, {}, {}});
    return {0xb0000000u, {}, {std::move(array)}};
}
KscSaveValue RoomExchangeSave::PointsSaveWord() const {
    return {0xb0000000u, {}, {{integer | std::uint32_t(points_), {}, {}}}};
}

void RoomExchangeSave::Restore(const KscSaveValue& item_ref, const KscSaveValue& points_ref) {
    const auto& array = Target(item_ref);
    if (array.word != 0x9000000au || !array.text.empty() || array.children.size() != item_.size())
        throw std::invalid_argument("Invalid exchange item row");
    Row next{};
    for (std::size_t i = 0; i < next.size(); ++i) {
        const auto& cell = array.children[i];
        if (!Scalar(cell) || (cell.word && cell.word >> 28 != 2))
            throw std::invalid_argument("Invalid exchange item field");
        next[i] = cell.word;
    }
    if (next != Row{}) {
        // 3C45 selects these six resources from G4FA6. 31EF leaves [1,2]
        // untouched (initially NIL), and each uses placement mask 0x20.
        if (next[0] < integer + 197 || next[0] > integer + 202 || next[1] || next[2] ||
            next[3] != integer || next[4] >> 28 != 2 || next[5] != integer ||
            (next[6] && next[6] != 0x2fffffffu && (next[6] < integer || next[6] >= integer + 100)) ||
            next[7] != integer + 5 || next[8] || next[9] != integer)
            throw std::invalid_argument("Unsupported saved exchange display item");
    }
    const auto& saved_points = Target(points_ref);
    if (!Scalar(saved_points) || saved_points.word < integer || saved_points.word > integer + 1000)
        throw std::invalid_argument("Invalid saved exchange points");
    item_ = next;
    points_ = int(saved_points.word - integer);
}
}
