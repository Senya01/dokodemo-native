#pragma once
#include "room_word_conversion.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dokodemo::recovered { class PackageView; }

namespace dokodemo::gameplay {
class TitleRenderer;

// SOFTKEY for a room lesson (type 0) or the notebook's reading field (type 2).
class RoomWordEntry {
public:
    enum class Phase { idle, opening, editing, closing, closed_empty, closed_word, timed_out, character_input };
    bool Load(const std::filesystem::path& title_directory, std::string& error);
    void Open(std::uint32_t buttons);
    void OpenReading(std::string_view initial, std::uint32_t buttons, bool script_owned=false);
    bool is_reading_edit() const { return reading_edit_; }
    void Step(std::uint32_t buttons, const recovered::PackageView* font=nullptr);
    bool Draw(const TitleRenderer& renderer, std::vector<std::uint8_t>& vram,
              std::size_t offset, const recovered::PackageView* font=nullptr) const;
    Phase phase() const { return phase_; }
    unsigned se_mask() const { return se_mask_; }
    unsigned row() const { return row_; }
    unsigned column() const { return column_; }
    unsigned page() const { return page_; }
    bool picking() const { return picking_; }
    bool choosing_candidate() const { return choosing_candidate_; }
    unsigned boundary_pc() const { return boundary_pc_; }
    unsigned candidate_count() const { return static_cast<unsigned>(candidates_.size()); }
    unsigned candidate_index() const { return candidate_; }
    const std::vector<std::string>& candidate_texts() const { return candidates_; }
    bool candidates_known() const { return lookup_known_; }
    const std::string& written() const { return written_; }
    const std::string& reading() const { return reading_; }
    const std::string& composition() const { return composition_; }

private:
    void Close(bool timeout);
    void Confirm();
    void Erase();
    void SelectCell();
    void ChooseKana();
    void UpdateCandidates();
    void MoveCandidate(const std::array<bool,4>& move, std::uint32_t pressed, const recovered::PackageView* font);
    struct Group {
        std::vector<std::vector<std::string>> rows;
        unsigned columns=0;
    };
    const Group& ActiveGroup() const;
    void SetKeyOrder(std::string_view unit);
    using Pages=std::array<std::array<Group,12>,2>;
    Pages groups_{}, other_groups_{}, reading_groups_{};
    bool reading_edit_=false, cancelled_=false;
    std::array<bool,2> alternate_{};
    bool subpage_=false, choosing_candidate_=false;
    bool lookup_known_=true;
    unsigned boundary_pc_=0;
    RoomWordConversion conversion_;
    mutable float text_scroll_=0;
    unsigned kana_x_=0, kana_y_=0, candidate_=0;
    bool picking_=false;
    std::string composition_, written_, reading_;
    std::vector<RoomWordConversion::Candidate> candidate_records_;
    std::vector<std::string> candidates_;
    Phase phase_ = Phase::idle;
    std::array<unsigned,4> held_{};
    std::uint32_t previous_ = 0;
    unsigned frame_ = 0, row_ = 1, column_ = 1, page_ = 0;
    unsigned pressed_clip_ = 0, pressed_frame_ = 0, remaining_ = 0, se_mask_ = 0;
    bool timeout_ = false;
};
}
