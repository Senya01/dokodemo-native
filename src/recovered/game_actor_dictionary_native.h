#pragma once
#include "game_filesystem_native.h"
#include "script_vm.h"

namespace dokodemo::recovered {
struct GameActorDictionaryRecordState {
    uint32_t index=0;
    // Field0/5/6/7/8/17: logical numeric tag words,not PSP pointers.
    std::array<uint32_t,6> numeric{};
    // Reached A8A2 helpers mutate these bit fields. Retain exact tagged words;
    // this is separate from the six historical numeric diagnostic fields.
    std::array<uint32_t,2> fields14_15{};
    std::string written,reading;
};
struct GameActorDictionarySnapshot {
    bool source_live = false, imported = false, closed = false;
    uint32_t bytes = 0, records = 0, copied_values = 0, copied_strings = 0;
    uint32_t destination_index = 0, queries = 0;
    std::string first_written, first_reading;
    std::vector<GameActorDictionaryRecordState> current_records;
};
// Actor DIC is a tagged value tree,not the NJ keyboard dictionary. Service48
// imports it into an already allocated script tree (7CE00/7CBF4),then drops
// the file handle. Native references replace pointer rebasing; strings own data.
class GameActorDictionary final {
  public:
    GameFsStatus Attach(PackageView image);
    GameFsStatus Import(script::Vm& vm, const script::Value& destination);
    std::pair<std::string,std::string> ReadRecord(script::Vm& vm,uint32_t index);
    bool OwnsCell(const script::Value& reference) const noexcept;
    void Capture(script::Vm&);
    GameActorDictionarySnapshot Snapshot() const { return state_; }
    void Close();
  private:
    PackageView image_;
    script::Value destination_;
    std::vector<script::Value> records_;
    GameActorDictionarySnapshot state_;
};
} // namespace dokodemo::recovered
