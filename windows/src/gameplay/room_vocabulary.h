#pragma once
#include "profile_adoption.h"
#include "ksc_save_graph.h"
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dokodemo::gameplay {

class RoomVocabulary {
public:
    bool Load(const std::filesystem::path& post_profile, const AcceptedProfile& profile, std::string& error);
    KscSaveValue DictionarySaveTable() const; // 8D22A, G3DD2
    // G59A7 is the next number allocated by 12E7 to column 10 of word rows.
    KscSaveValue RelationGroupCounter() const;
    void RestoreRelationGroupCounter(const KscSaveValue& ref);
    void LinkWordGroup(unsigned source_row,unsigned target_row);
    KscSaveValue QuestionSaveTable() const; // 8D34A, G58DF
    RoomVocabulary WithSaveTables(const KscSaveValue& dictionary,const KscSaveValue& questions) const;
    void RestoreSaveTables(const KscSaveValue& dictionary,const KscSaveValue& questions);
    void StartActorScript(std::uint32_t draw); // 179E, after first bank and before 8DDB5
    const std::string& player_saying() const { return player_saying_; } // row600[26]
    void BeginConversation(); // 0FD1
    unsigned Candidates(unsigned primary_ct, unsigned secondary_ct, bool mark);
    bool HasPredicate(unsigned ct) const { return conditions_.count(ct)!=0; }
    std::vector<int> PredicateGroup(unsigned ct,unsigned group) const;
    int FirstOutingMatch(unsigned ct) const; // 0F30, builtin96 without marking or a random draw
    int FinishLookup(unsigned pick); // caller owns builtin17(count)
    struct WordForm {
        std::string text;
        unsigned field; // zero when the caller's fallback was returned
        std::size_t field_bytes;
    };
    WordForm ResolveWord(int row, const std::string& fallback, unsigned excluded_ct, bool alternate) const;
    WordForm ResolveDisplayName(unsigned row) const;
    std::string Word(int row, const std::string& fallback, unsigned excluded_ct, bool alternate,
                     bool* used_row = nullptr) const;
    std::string DisplayName(unsigned row) const;
    int FieldInt(unsigned row, unsigned field) const;
    std::size_t row_count() const { return rows_.size(); }
    void SetPlayerSaying(const std::string& saying) { player_saying_=saying; }
    void ClearAlternate(unsigned row) { rows_.at(row).alternate.clear(); rows_.at(row).fields[13]=0; }
    bool HasField(unsigned row,unsigned field) const;
    std::string_view FieldText(unsigned row,unsigned field) const;
    void WriteNumber(unsigned row,unsigned field,std::optional<int> value);
    void WriteText(unsigned row,unsigned field,const std::string& value);
    void InitializeReserved(unsigned row,std::string written,std::string reading);
    bool AcceptReserved(unsigned row);
    void RefreshReading(unsigned row); // 1488
    void ChangeMemoReading(unsigned row,const std::string& reading);
    int FindWritten(const std::string& written) const;
    void SetRelationship(unsigned row, int field12);
    void RecordTrait(unsigned row, unsigned index, bool set);
    std::optional<bool> FirstContactSpecial();
    static constexpr unsigned question_slots = 20;
    std::uint32_t QuestionField(unsigned row,unsigned field) const { return questions_.at(row).at(field); }
    const std::string& QuestionText(unsigned row,unsigned field) const { return question_text_.at(row).at(field); }
    void WriteQuestionField(unsigned row,unsigned field,std::uint32_t value) {
        questions_.at(row).at(field)=value;
        question_text_.at(row).at(field).clear();
    }
    void WriteQuestionText(unsigned row,unsigned field,std::string_view text) {
        question_text_.at(row).at(field)=text;
        questions_.at(row).at(field)=0x50000000u;
    }
    int ReserveWord();
    void ReleaseWord(int row);
    void ClearMemoWord(unsigned row);

private:
    struct Row {
        std::array<std::uint32_t, 19> fields{};
        std::string written, reading, alternate;
    };
    struct Term { unsigned field, op; std::uint32_t arg; };
    void Mark(unsigned ct, unsigned mask, bool honor_repeat);
    void Collect(unsigned mask);
    std::vector<Row> rows_;
    unsigned next_relation_group_=0;
    // G3151 has 30 fields per row, compared with 19 in G0018.
    std::vector<std::array<KscSaveValue,11>> friend_tails_;
    std::map<unsigned, std::vector<std::vector<Term>>> conditions_;
    std::map<unsigned, std::vector<std::string>> exclusions_;
    std::array<std::array<std::uint32_t,7>,question_slots> questions_{};
    std::array<std::array<std::string,7>,question_slots> question_text_{};
    std::array<std::string,5> initial_sayings_;
    std::string player_saying_;
    std::vector<int> matches_;
    bool pending_ = false, mark_ = false, started_ = false;
};

} // namespace dokodemo::gameplay
