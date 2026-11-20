void CheckContactHistory(const Talk& cold,const Talk& yes,const Talk& no,const Font& font) {
    using gameplay::KscSaveValue;
    auto pack=[](const std::array<KscSaveValue,3>& fields) {
        return gameplay::EncodeKscSaveGraph({0x90000003u,{}, {fields.begin(),fields.end()}});
    };
    const auto accepted=yes.ContactHistoryTables(), declined=no.ContactHistoryTables();
    auto field=[](const auto& history,unsigned bank,unsigned index)->const KscSaveValue& {
        return history.at(bank).children.at(0).children.at(index);
    };
    const auto initial=cold.ContactHistoryTables();
    Require(field(initial,1,3).word==0 && field(initial,2,3).word==0 &&
            field(accepted,1,3).word==0x20000004 && field(accepted,2,3).word==0x20000004 &&
            field(declined,1,3).word==0x20000004 && field(declined,2,3).word==0x20000000,
            "86 must occupy bit 2 in word 3 with NIL distinct from the cleared integer");
    Require(field(accepted,0,0).word==0x60000000 && !field(accepted,0,0).text.empty(),
            "The affirmative contact lost its note");
    auto bytes=pack(accepted);
    std::size_t used=0;
    auto decoded=gameplay::DecodeKscSaveGraph(bytes.data(),bytes.size(),used);
    Require(used==bytes.size(),"Incomplete history round trip");
    std::array<KscSaveValue,3> restored{decoded.children.at(0),decoded.children.at(1),decoded.children.at(2)};
    Talk owner=no;
    auto damaged=restored;
    damaged[2].children.front().children.back().word=0x70000000;
    bool refused=false;
    try { owner.RestoreContactHistory(damaged,yes.ProgressFlagWords()); } catch(const std::exception&) { refused=true; }
    Require(refused && pack(owner.ContactHistoryTables())==pack(declined),
            "A malformed final flag changed earlier contact tables");
    auto progress=yes.ProgressFlagWords();
    Require(progress[0].word>>28==2 && (progress[1].word & 0x20001000u)==0x20001000u,
            "First-day room lost its tagged source progress flags");
    owner.RestoreContactHistory(restored,progress);
    Require(owner.ProgressFlagWords()[0].word==progress[0].word &&
            owner.ProgressFlagWords()[1].word==progress[1].word,"Restored room flags changed");
    Require(pack(owner.ContactHistoryTables())==bytes,"Contact restore changed the saved word or note");
    RepeatVisit(owner,font,true);
}
