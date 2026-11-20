#pragma once
#include "profile_fields.h"
#include "ksc_save_graph.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dokodemo::gameplay {
struct RoomInventoryItem {
    int slot=-1, id=-1, category=1, year=0, month=0, day=0;
    std::array<std::uint32_t,10> fields{}; // KSC tags and scalar payloads of this inventory row.
    std::string name, description;  // CP932 after the script's giver substitution.
};

class RoomInventory {
public:
    static constexpr int slot_count=170;
    bool Load(const std::filesystem::path& game_directory,std::string& error);
    int AwardGift(unsigned actor,unsigned choice,std::string_view nickname,int year,int month,int day);
    int AwardGiftOnDate(unsigned actor,unsigned choice,std::string_view nickname,std::uint32_t bcd_date);
    KscSaveValue SaveRow(unsigned slot) const;
    std::vector<unsigned> ExchangeableGifts() const;
    KscSaveValue OfferGift(unsigned slot) const;
    RoomInventory WithGiftSwap(unsigned slot,const KscSaveValue& gift,std::uint32_t date) const;
    KscSaveValue SaveTable() const;
    void RestoreSaveTable(const KscSaveValue& table);
    std::uint32_t PlacementWord(int slot,unsigned field) const;
    void SetPlacementWord(int slot,unsigned field,std::uint32_t word);
    void Open(int requested_slot,std::uint32_t held_buttons);
    // The room fade owner can hold the open gate while its surface is still covered.
    void Step(std::uint32_t buttons,bool fade_complete=true);
    bool visible() const { return phase_!=Phase::idle && phase_!=Phase::closed; }
    bool closed() const { return phase_==Phase::closed; }
    bool closing() const { return phase_==Phase::closing; }
    bool opening() const { return phase_==Phase::opening; }
    bool list_ready() const { return phase_==Phase::list; }
    bool actions_ready() const { return phase_==Phase::actions; }
    bool actions_visible() const { return phase_==Phase::action_open || phase_==Phase::actions || phase_==Phase::action_close; }
    unsigned action_clip() const { return phase_==Phase::action_close ? 14 : 13; }
    unsigned action_frame() const { return action_frame_; }
    float discard_progress() const { return discard_.progress(); }
    std::vector<unsigned> TakeEffects();
    bool discard_visible() const { return phase_==Phase::discard && discard_.visible(); }
    unsigned discard_cursor() const { return discard_.cursor(); }
    int active_discard_choice() const {
        return phase_==Phase::discard && discard_.active() ? int(discard_.cursor()) : -1;
    }
    unsigned filter() const { return filter_; }
    unsigned action_cursor() const { return action_cursor_; }
    unsigned selection() const { return selection_; }
    unsigned count() const { return static_cast<unsigned>(visible_rows_.size()); }
    const RoomInventoryItem* item_at(unsigned visible_index) const;
    const RoomInventoryItem* selected_item() const { return item_at(selection_); }
    const RoomInventoryItem* slot_item(int slot) const;
    static std::string_view ActionLabel(unsigned index);
    unsigned requested_action() const { return event_; }
    const RoomInventoryItem* action_item() const { return event_ ? slot_item(event_slot_) : nullptr; }
    void CompleteAction();
private:
    enum class Phase { idle,opening,list,action_open,actions,action_close,discard,pending,closing,closed };
    struct Gift { unsigned id=0, property=0; std::string name,description; };
    std::array<std::array<Gift,5>,5> gifts_{};
    bool loaded_=false;
    Phase phase_=Phase::idle;
    std::array<RoomInventoryItem,slot_count> slots_{};
    std::vector<unsigned> visible_rows_;
    unsigned filter_=1,selection_=0,action_cursor_=0,action_frame_=0,event_=0;
    int event_slot_=-1;
    std::uint32_t previous_=0;
    std::array<unsigned,4> held_{};
    ProfileConfirmation discard_;
    std::vector<unsigned> effects_;
    void OpenActions(unsigned cursor);
    void Rebuild();
    void Queue(unsigned event);
};
} // namespace dokodemo::gameplay
