#include "gameplay/room_vocabulary.h"
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>

using namespace dokodemo::gameplay;

namespace {
void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void CheckSavedVocabulary(RoomVocabulary& words,const std::filesystem::path& dir,
                          const AcceptedProfile& profile,const char* captured) {
    auto dictionary=words.DictionarySaveTable();
    auto questions=words.QuestionSaveTable();
    if(captured) {
        std::ifstream in(captured,std::ios::binary);
        const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(in),{}};
        Check(data.size()==0x40000,"Expected clear retail utility buffer");
        std::size_t used=0;
        auto root=DecodeKscSaveGraph(data.data()+0x10b50,data.size()-0x10b50,used);
        Check(root.children.size()==27,"Invalid retail save root");
        dictionary=std::move(root.children[1]);
        questions=std::move(root.children[25]);
    }
    RoomVocabulary restored;
    std::string error;
    Check(restored.Load(dir,profile,error),"Cannot prepare vocabulary owner");
    restored.RestoreSaveTables(dictionary,questions);
    auto packed=[](const KscSaveValue& table) {
        return EncodeKscSaveGraph({0x90000001u,{},{table}});
    };
    const auto bytes=packed(dictionary);
    Check(packed(restored.DictionarySaveTable())==bytes && packed(restored.QuestionSaveTable())==packed(questions),
          "Vocabulary/question owner changed file values on round trip");
    if(!captured) {
        Check(restored.FieldText(163,1)==words.FieldText(163,1) &&
              restored.FieldText(163,2)==words.FieldText(163,2) &&
              restored.FieldInt(163,0)==words.FieldInt(163,0),
              "The restored learned word lost identity, reading or flags");
    }
    questions.children.clear();
    bool refused=false;
    try { restored.RestoreSaveTables(dictionary,questions); } catch(const std::runtime_error&) { refused=true; }
    Check(refused && packed(restored.DictionarySaveTable())==bytes,"Refused table changed its owner");
}

void CheckLearnedConversation(RoomVocabulary& words) {
    const std::string spelling="\x8b\x9b"; // 魚
    const std::string reading="\x82\xb3\x82\xa9\x82\xc8"; // さかな
    const int row=words.ReserveWord();
    words.RefreshReading(row);
    Check(!words.HasField(row,18) && words.FieldInt(row,0)==1, "1488 skips a nil reading");
    words.InitializeReserved(row,spelling,reading);
    words.BeginConversation();
    Check(words.Candidates(0x2931,0,true)==0 && words.FinishLookup(0)==-1,
          "Reading question skips a reserved word");
    Check(words.AcceptReserved(row), "Commit the taught word");
    words.BeginConversation();
    Check(words.Candidates(0x2931,0,true)==1 && words.FinishLookup(0)==row,
          "4E27 selects the taught row with an unconfirmed reading");
    Check(words.FieldText(row,1)==spelling && words.FieldText(row,2)==reading &&
          words.FieldInt(row,4)==1, "Reading question uses two distinct dictionary fields");
    Check(words.Candidates(0x2931,0,true)==0 && words.FinishLookup(0)==-1,
          "Reading question respects the same-conversation repeat mark");

    // The answer in 4CF7 sets the confirmed bit, then calls 1488.
    words.WriteNumber(row,0,words.FieldInt(row,0)|0x2000);
    words.RefreshReading(row);
    Check(words.FieldInt(row,18)==0xb382 && words.FieldInt(row,0)==0x7024 &&
          words.FieldInt(row,3)==1, "Affirming the reading retains its index and spelling length");
    words.BeginConversation();
    Check(words.Candidates(0x2931,0,true)==0 && words.FinishLookup(0)==-1,
          "Confirmed reading excludes the same row from 4E27 on a later visit");
    Check(words.Candidates(0x28dc,0x28e2,true)==1 && words.FinishLookup(0)==row,
          "2626 still prefers the taught row for the ordinary fallback");
    bool used_row=false;
    Check(words.Word(row,spelling,0,true,&used_row)==spelling && used_row,
          "1011 returns the row even when it matches the fallback bytes");
    words.ChangeMemoReading(row,"A");
    Check(words.FieldText(row,1)==spelling && words.FieldText(row,2)=="A" &&
          words.FieldInt(row,0)==0x2024 && words.FieldInt(row,18)==0x7383,
          "Revised non-kana reading clears kana flags and preserves the confirmed spelling");
    // Topic 37132 passes exclusion CT6983, which includes 魚.
    Check(words.Word(row,spelling,0x6983,true,&used_row)==spelling && !used_row,
          "Excluded spelling is the fallback even with identical bytes");
    used_row=true;
    Check(words.Word(-1,spelling,0,true,&used_row)==spelling && !used_row,
          "Missing row has no display origin");
}

void CheckPersonQuestionFilters(RoomVocabulary& words) {
    // 91FF reads these triples when 92F9 opens a lesson for a missing word.
    Check(words.PredicateGroup(0x9f16,0)==std::vector<int>{0,6,4,11,7,2,7,8,5,12,8,6,0,6,32} &&
          words.PredicateGroup(0x9f1f,0)==std::vector<int>{14,6,16384,15,6,16384},
          "5C31B primary and secondary teaching predicates");
    const int ordinary=words.ReserveWord();
    words.InitializeReserved(ordinary,"HANA","HANA");
    words.WriteNumber(ordinary,7,4);
    words.SetRelationship(ordinary,66);
    Check(words.AcceptReserved(ordinary),"Commit ordinary person");
    words.RecordTrait(ordinary,14,false);
    const int famous=words.ReserveWord();
    words.InitializeReserved(famous,"YUKI","YUKI");
    words.WriteNumber(famous,7,1);
    words.SetRelationship(famous,66);
    Check(words.AcceptReserved(famous),"Commit famous person");
    words.RecordTrait(famous,14,true);
    words.BeginConversation();
    Check(words.Candidates(0x9f16,0x9f1f,true)==1 && words.FinishLookup(0)==famous,
          "Person question prefers a matching positive trait over its recorded negative");
    Check(words.Candidates(0x9f16,0x9f1f,true)==1 && words.FinishLookup(0)==ordinary,
          "Empty preferred intersection still selects a primary person after the repeat mark");
    Check(words.Candidates(0x9f16,0x9f1f,true)==0 && words.FinishLookup(0)==-1,
          "Both people excluded by same-conversation repeat marks");
}
}

int main(int argc, char** argv) try {
    Check(argc==2 || argc==3, "Expected game directory and optional clear retail utility buffer");
    const auto dir=std::filesystem::path(argv[1])/"assets/game/post-profile";
    AcceptedProfile profile{};
    profile.player.Assign("AKI");
    profile.nickname.Assign("Toro");
    RoomVocabulary words;
    std::string error;
    if (!words.Load(dir,profile,error)) throw std::runtime_error(error);
    words.StartActorScript(9);
    Check(words.player_saying()=="\x88\xa4\x82\xc1\x82\xc4\x89\xbd\x82\xbe\x82\xeb\x82\xa4\x81\x48", "Initial F6 saying, fifth slot");
    Check(words.DisplayName(600)=="AKI" && words.DisplayName(0)=="Toro", "Profile rows");
    Check(words.FieldInt(600,9)==2 && words.FieldInt(162,0)==20, "Cold rows");
    Check(words.FirstContactSpecial()==false, "Unlearned special-contact words");
    Check(words.Candidates(0x2931,0,true)==0 && words.FinishLookup(0)==-1, "Cold learned-word query");
    words.BeginConversation();
    // 33 original secondary flags survive a failed search for a learned saying.
    // 23 are also selected by DE37; flags disappear only after a successful 11B0.
    Check(words.Candidates(0xde37,0xde3d,true)==23 && words.FinishLookup(0)==1, "Cold dual filter");
    Check(words.DisplayName(1)=="\x83\x6c\x83\x52" && words.FieldInt(1,4)==6 && words.FieldInt(1,11)==3,
          "Selected word/use/repeat mark");
    Check(words.Candidates(0xde37,0xde3d,true)==108 && words.FinishLookup(0)==2, "Dual fallback and repeat exclusion");
    words.BeginConversation();
    Check(words.Candidates(0xde10,0,true)==109 && words.FinishLookup(0)==1, "Reset repeat flags");
    // A relationship answer changes row12. Starting a new conversation clears
    // the repeat mark, while the new relationship still excludes row1 at DE10.
    words.SetRelationship(1,words.FieldInt(1,12)|0x100);
    words.BeginConversation();
    Check(words.Candidates(0xde10,0,true)==108 && words.FinishLookup(0)==2, "Relationship answer in a later filter");
    words.RecordTrait(2,7,true);
    Check(words.FieldInt(2,14)==128 && words.FieldInt(2,15)==128, "Recorded trait");
    words.RecordTrait(2,7,false);
    Check(words.FieldInt(2,14)==0 && words.FieldInt(2,15)==128, "Trait declined, answer retained");
    Check(words.Candidates(0xde80,0,true)==30 && words.FinishLookup(0)==77, "DE80 matching DIC rows");
    Check(words.Word(77,"fallback",0,true)=="\x82\xb3\x82\xf1\x82\xdb", "Written form fallback");
    Check(words.Word(-1,"fallback",0,false)=="fallback", "No row fallback");
    Check(words.Candidates(0xd4bf,0,true)==1 && words.FinishLookup(0)==76, "Farewell word");
    Check(words.Candidates(0xd4bf,0,true)==0 && words.FinishLookup(0)==-1, "Repeat mark");
    Check(words.Candidates(0xd4bf,0,false)==1 && words.FinishLookup(0)==76, "Unmarked lookup");
    const int reserved=words.ReserveWord();
    Check(reserved==163 && words.FieldInt(reserved,0)==1 && words.FieldInt(reserved,4)==0, "Teaching reserve");
    words.ReleaseWord(reserved);
    Check(words.ReserveWord()==reserved, "Cancel releases the original slot");
    profile.gender=1;
    Check(words.Load(dir,profile,error) && words.FieldInt(600,9)==1, "Gender source branch");
    words.StartActorScript(5);
    Check(words.player_saying()=="\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd\x83\x6a\x83\x83", "Initial F6 saying, first slot");
    CheckLearnedConversation(words);
    CheckPersonQuestionFilters(words);
    CheckSavedVocabulary(words,dir,profile,argc==3?argv[2]:nullptr);
    std::cout << "Toro word import, filters, repeat, display, cold special-contact and reservation passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
