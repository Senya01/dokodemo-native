#pragma once
#include "game_actor_dictionary_native.h"
#include "game_profile_native.h"
#include "rng_native.h"
#include <map>

namespace dokodemo::recovered {
struct GameActorScriptSnapshot {
    bool live=false,started=false,completed=false,blocked=false,awaiting_message=false;
    bool autoload_imported=false,profile_loaded_marker=false,manager_flag68=false;
    // Native new-profile state has no optional imported save fields. Their
    // source names/producers remain unknown;this is not a BSS-zero claim.
    bool native_optional_fields_absent=true;
    uint32_t bytes=0,entry_pc=0,pc=0,last_opcode=0,ticks=0,yields=0,messages=0;
    uint32_t builtin_calls=0,game_service_calls=0,last_service=UINT32_MAX;
    uint32_t dictionary_game_reads=0,dictionary_game_writes=0;
    uint32_t array_query_calls=0,array_query_matches=0,array_actions=0;
    uint64_t random_selections=0;
    uint32_t selected_value_word=0;
    uint32_t root_functions_returned=0;
    std::vector<uint32_t> last_query_indices;
    uint64_t instructions=0;
    std::vector<uint32_t> service_ids;
    std::string path,sha256,message,error;
    std::string last_normalized;
    GameActorDictionarySnapshot dictionary;
};
// Actual230EC ->6656C ->67FD4 entry. This is the game's native KSC bytecode
// interpreter,not a PSP/MIPS runtime. Named host services bind actual current
// native profile/state; unsupported operations retain an explicit boundary.
class GameActorScript final {
  public:
    using ClockWord = std::function<uint32_t()>;
    GameFsStatus Initialize(PackageView dictionary,PackageView script,std::string path,
                            const GameProfileRecord& profile,ClockWord clock = {},RngNativeOwner* rng=nullptr);
    GameFsStatus Tick();
    bool AcknowledgeMessage();
    GameActorScriptSnapshot Snapshot() const;
    void Close();
  private:
    script::Value Service(uint8_t,const std::vector<script::Value>&);
    script::Value GameService(const std::vector<script::Value>&);
    PackageView script_image_;
    script::Program program_;
    std::unique_ptr<script::Vm> vm_;
    GameActorDictionary dictionary_;
    GameProfileRecord profile_;
    ClockWord clock_;
    RngNativeOwner* rng_=nullptr;
    std::map<uint32_t,script::Value> bindings_;
    GameActorScriptSnapshot state_;
};
} // namespace dokodemo::recovered
