#include "room_messages.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr std::uint32_t integer = 0x20000000u;
constexpr std::uint32_t string = 0x60000000u;
constexpr std::uint32_t mask = 0x0fffffffu;
}

void RoomMessages::Clear() { fields_ = {}; } // A145: assign NIL to every field.

void RoomMessages::ResetActor() {
    Clear();
    used_.reset();
    saved_used_.fill(0);
}

bool RoomMessages::DescriptorUsed(unsigned id) const { return used_.test(id); }

void RoomMessages::PrepareUsedDescriptorsForSave() {
    // A0F3 calls 181A(true) for active entries; previously saved bits stay set.
    for (unsigned id=0; id<used_.size(); ++id)
        if (used_[id]) saved_used_[id/28] |= integer | (1u << (id%28));
}

KscSaveValue RoomMessages::UsedDescriptorsSaveTable() const {
    KscSaveValue body{0x90000005u, {}, {}};
    for (const auto word : saved_used_) body.children.push_back({word, {}, {}});
    return {0xb0000000u, {}, {std::move(body)}};
}

void RoomMessages::RestoreUsedDescriptors(const KscSaveValue& ref) {
    if (ref.word!=0xb0000000u || !ref.text.empty() || ref.children.size()!=1)
        throw std::invalid_argument("Expected G59CE reference");
    const auto& body=ref.children[0];
    if (body.word!=0x90000005u || !body.text.empty() || body.children.size()!=saved_used_.size())
        throw std::invalid_argument("Expected five G59CE words");
    std::array<std::uint32_t,5> saved;
    for (unsigned i=0; i<saved.size(); ++i) {
        const auto& field=body.children[i];
        if ((field.word && field.word>>28!=2) || !field.text.empty() || !field.children.empty())
            throw std::invalid_argument("Invalid G59CE word");
        saved[i]=field.word;
    }
    // The graph is loaded into a fresh actor whose compiled descriptor flags start clear.
    // A0BD iterates the actual descriptor count; unused high bits stay in the saved owner.
    for (unsigned id=0; id<used_.size(); ++id)
        used_.set(id,(saved[id/28] & (1u << (id%28)))!=0);
    saved_used_=saved;
}

bool RoomMessages::AdmitsPriority(unsigned candidate) const {
    const auto prior = fields_[3].word;
    if (!prior) return true;
    const auto priority = prior & mask;
    return priority < 0x500000u && candidate > priority; // A168
}

bool RoomMessages::RecordDiary(unsigned descriptor_id, std::string_view text, unsigned priority) {
    if (!AdmitsPriority(priority)) return false;
    if (descriptor_id >= used_.size() || (priority & ~0xf00000u) || text.find('\0') != text.npos)
        throw std::invalid_argument("Invalid diary descriptor or formatted text");
    KscSaveValue copy{string, std::string(text), {}};
    if (fields_[1].word)
        used_.reset(fields_[1].word & mask);
    used_.set(descriptor_id);
    fields_[1] = {integer | descriptor_id, {}, {}};
    fields_[2] = std::move(copy);
    fields_[3] = {integer | priority, {}, {}};
    return true;
}

std::uint32_t RoomMessages::ContactSelectionBoundary() const {
    // A496 tests [0] first. The normal first-day GAME71 flag is zero.
    if (fields_[0].word & mask) return 0;
    // A39C asks A168 before examining G408F for a calendar match.
    if (!AdmitsPriority(0x400000)) return 0;
    return 0xa366; // The next selector requires live G408F descriptor flags and the current date.
}

KscSaveValue RoomMessages::SaveTable() const {
    KscSaveValue body{0x90000007u, {}, {fields_.begin(), fields_.end()}};
    return {0xb0000000u, {}, {std::move(body)}};
}

void RoomMessages::ValidateSaveTable(const KscSaveValue& ref) {
    if (ref.word != 0xb0000000u || !ref.text.empty() || ref.children.size() != 1)
        throw std::invalid_argument("Expected G44C8 reference");
    const auto& row = ref.children.front();
    if (row.word != 0x90000007u || !row.text.empty() || row.children.size() != 7)
        throw std::invalid_argument("Expected seven G44C8 fields");
    for (unsigned i = 0; i < row.children.size(); ++i) {
        const auto& field = row.children[i];
        const bool text_column = i == 2 || i == 5 || i == 6;
        const bool is_text = field.word == string;
        const auto tag = field.word >> 28;
        const bool value_ok = field.word == 0 || (text_column ? is_text :
            (i == 0 ? field.word == 1 || tag == 1 || tag == 2 : tag == 2));
        if ((i==1 && (field.word&mask)>=135) || !value_ok || !field.children.empty() || (!is_text && !field.text.empty()) ||
            field.text.find('\0') != field.text.npos)
            throw std::invalid_argument("Invalid G44C8 field");
    }
}

void RoomMessages::RestoreSaveTable(const KscSaveValue& ref) {
    ValidateSaveTable(ref);
    std::array<KscSaveValue,7> staged;
    const auto& cells = ref.children.front().children;
    std::copy(cells.begin(), cells.end(), staged.begin());
    fields_ = std::move(staged);
}
} // namespace dokodemo::gameplay
