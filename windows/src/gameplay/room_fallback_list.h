#pragma once
#include "ksc_save_graph.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dokodemo::gameplay {

// The two source mode-3 selectors have separate lists, exclusion histories and write cursors.
class RoomFallbackList {
public:
    bool Load(const std::filesystem::path& post_profile,std::string& error);
    unsigned CandidateCount() const { return static_cast<unsigned>(ordinary_.eligible.size()); }
    unsigned Consume(unsigned rank) { return ordinary_.Consume(rank); }
    unsigned PartnerCandidateCount() const { return static_cast<unsigned>(partner_.eligible.size()); }
    unsigned ConsumePartner(unsigned rank) { return partner_.Consume(rank); }
    KscSaveValue SaveTable() const { return ordinary_.SaveTable(); }
    KscSaveValue PartnerSaveTable() const { return partner_.SaveTable(); }
    void RestoreSaveTable(const KscSaveValue& table) { ordinary_.Restore(table); }
    void RestorePartnerSaveTable(const KscSaveValue& table) { partner_.Restore(table); }
private:
    struct Selector {
        void Refresh();
        unsigned Consume(unsigned rank);
        void Restore(const KscSaveValue& table);
        KscSaveValue SaveTable() const;
        std::vector<std::uint32_t> entries,history;
        std::vector<unsigned> eligible;
        unsigned cursor=0;
    };
    Selector ordinary_,partner_;
};
}
