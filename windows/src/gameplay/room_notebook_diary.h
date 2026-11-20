#pragma once
#include "cb2a_scene.h"
#include <array>
#include <string_view>

namespace dokodemo::recovered { class PackageView; class RngNativeOwner; }
namespace dokodemo::gameplay {
class RoomMessages;
class RoomNotebookDiary {
public:
    bool Load(const std::filesystem::path& directory,std::string& error,unsigned actor=0);
    bool RecordFirstDay(RoomMessages& staging,recovered::RngNativeOwner& rng,std::string_view player,unsigned year,unsigned month,unsigned day);
    void RestoreFirstDay(std::string text, const std::array<unsigned,3>& date, bool revealed);
    void Open(std::uint32_t buttons,unsigned year,unsigned month,unsigned day,unsigned hour);
    void Step(std::uint32_t buttons,bool fade_complete=true);
    void FinishPageTurn(std::uint32_t buttons);
    bool visible() const { return phase_!=Phase::closed; }
    bool closing() const { return phase_==Phase::closing; }
    bool turning_page() const { return phase_==Phase::turning; }
    bool ready() const { return phase_==Phase::reading; }
    bool has_entry() const { return !entry_.empty(); }
    std::string_view entry_text() const { return entry_; }
    const std::array<unsigned,3>& entry_date() const { return entry_date_; }
    bool entry_revealed() const { return revealed_; }
    unsigned page_count() const { return pages_; }
    unsigned selection() const { return selection_; }
    bool page_has_content() const { return revealed_ && current_==0; }
    std::vector<unsigned> TakeEffects();
    bool Draw(const recovered::PackageView& font,std::vector<std::uint8_t>& vram,std::size_t offset) const;
private:
    enum class Phase { closed, opening, reading, turning, closing };
    Phase phase_=Phase::closed;
    Cb2aScene scene_;
    std::string template_,entry_,date_;
    std::string_view marker_;
    unsigned descriptor_id_=0, descriptor_kind_=0;
    std::vector<std::uint8_t> photo_;
    std::vector<unsigned> effects_;
    std::uint32_t previous_=0;
    std::array<unsigned,2> direction_ticks_{};
    std::array<unsigned,3> entry_date_{};
    bool revealed_=false;
    unsigned pages_=1, selection_=0, current_=0;
};
}
