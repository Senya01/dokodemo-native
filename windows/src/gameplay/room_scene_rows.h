#pragma once
#include "ksc_save_graph.h"
#include "room_exchange_save.h"
#include <array>
#include <filesystem>

namespace dokodemo::recovered { class RngNativeOwner; }

namespace dokodemo::gameplay {
class RoomSceneRows {
public:
    using Row = std::array<std::uint32_t,10>;
    using Table = std::array<Row,5>;
    void Load(const std::filesystem::path& post_profile_directory);
    // One call of NEKO 3B9D. For the equal branch there was no draw (-1).
    std::array<int,5> Update(int calendar_selector, std::uint32_t bcd_date,
                             recovered::RngNativeOwner& rng);
    const Table& rows() const { return rows_; }
    RoomExchangeSave& exchange() { return exchange_; }
    const RoomExchangeSave& exchange() const { return exchange_; }
    void ClearModelHandle(std::size_t row);
    void SetModelHandle(std::size_t row, int handle);
    KscSaveValue SaveTable() const;
    void RestoreSaveTable(const KscSaveValue& table);
private:
    struct Item {
        std::uint32_t id, selector, value3, placement, value8;
    };
    std::array<Item,5> items_{};
    Table rows_{};
    RoomExchangeSave exchange_;
    bool loaded_ = false;
};
} // namespace dokodemo::gameplay
