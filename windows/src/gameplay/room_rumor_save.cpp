#include "room_fallback.h"
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
KscSaveValue Text(const std::optional<std::string>& s) {
    return s ? KscSaveValue{0x60000000u,*s,{}} : KscSaveValue{};
}
const std::vector<KscSaveValue>& Fields(const KscSaveValue& ref,unsigned count) {
    if (ref.word!=0xb0000000u || !ref.text.empty() || ref.children.size()!=1)
        throw std::invalid_argument("Expected G5995 object reference");
    const auto& array=ref.children.front();
    if (array.word!=(0x90000000u|count) || !array.text.empty() || array.children.size()!=count)
        throw std::invalid_argument("Wrong G5995 array extent");
    return array.children;
}
std::optional<std::string> ReadText(const KscSaveValue& field) {
    if (field.children.empty()) {
        if (!field.word && field.text.empty()) return {};
        if (field.word==0x60000000u && field.text.find('\0')==std::string::npos) return field.text;
    }
    throw std::invalid_argument("Invalid G5995 string or NIL");
}
}
KscSaveValue RoomRumors::SaveTable() const {
    KscSaveValue array{0x90000003u,{},{}};
    for (const auto& [subject,statement] : slots_) {
        KscSaveValue pair{0x90000002u,{}, {Text(subject),Text(statement)}};
        array.children.push_back({0xb0000000u,{}, {std::move(pair)}});
    }
    return {0xb0000000u,{}, {std::move(array)}};
}
void RoomRumors::RestoreSaveTable(const KscSaveValue& ref) {
    Slots slots;
    const auto& records=Fields(ref,slots.size());
    for (unsigned i=0;i<slots.size();++i) {
        const auto& fields=Fields(records[i],2);
        slots[i]={ReadText(fields[0]),ReadText(fields[1])};
    }
    slots_=std::move(slots);
}
} // namespace dokodemo::gameplay
