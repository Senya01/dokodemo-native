#pragma once
#include "profile_fields.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dokodemo::gameplay {
class RoomVocabulary;

struct NotebookWord {
    unsigned row = 0;
    std::string written, reading;
    int date = 0;
    bool protected_word = false;
};

class RoomNotebook {
public:
    void Open(const RoomVocabulary& vocabulary, std::uint32_t held);
    void Step(std::uint32_t buttons, bool fade_complete = true);
    bool visible() const { return phase_ != Phase::closed; }
    bool closing() const { return phase_ == Phase::closing; }
    bool list_ready() const { return phase_ == Phase::list; }
    bool actions_visible() const;
    bool actions_ready() const { return phase_ == Phase::actions; }
    bool reading_edit_active() const { return phase_ == Phase::reading_edit; }
    bool deletion_pending() const { return phase_ == Phase::delete_word; }
    void FinishDeletion(RoomVocabulary& vocabulary);
    void FinishReading(RoomVocabulary& vocabulary,std::optional<std::string> returned_written,
                       std::uint32_t held_buttons);
    unsigned action_clip() const { return phase_ == Phase::action_close ? 14 : 13; }
    unsigned action_frame() const { return frame_; }
    unsigned action_cursor() const { return action_; }
    unsigned action_count() const;
    bool delete_visible() const { return phase_ == Phase::confirm_delete && question_.visible(); }
    unsigned delete_cursor() const { return question_.cursor(); }
    float delete_progress() const { return question_.progress(); }
    int active_delete_choice() const {
        return phase_==Phase::confirm_delete && question_.active() ? int(question_.cursor()) : -1;
    }
    float scroll() const { return scroll_; }
    unsigned tab() const { return tab_; }
    unsigned count() const { return static_cast<unsigned>(visible_.size()); }
    unsigned selection() const { return selected_; }
    const NotebookWord* item_at(unsigned i) const;
    const NotebookWord* selected_item() const { return item_at(selected_); }
    std::vector<unsigned> TakeEffects();
    unsigned boundary_pc() const { return boundary_; }
private:
    enum class Phase { closed, opening, list, action_open, actions, action_close, reading_edit,
                       confirm_delete, delete_word, closing };
    Phase phase_ = Phase::closed;
    std::vector<NotebookWord> words_;
    std::vector<unsigned> visible_, effects_;
    std::array<unsigned,4> held_{};
    unsigned tab_=1, selected_=0, action_=0, frame_=0, boundary_=0;
    std::uint32_t previous_=0;
    ProfileConfirmation question_;
    float scroll_=0, target_scroll_=0;
    void Filter();
    void ReadWords(const RoomVocabulary& vocabulary);
};
}
