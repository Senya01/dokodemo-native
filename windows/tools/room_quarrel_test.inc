// The common dispatcher selects 59D30 at D090[339]. The two name lookups
// share a criterion but consume distinct rows within this conversation.
enum class QuarrelVisit { refuse, teach_first, teach_second, story };

std::vector<std::optional<unsigned>> QuarrelContact(gameplay::RoomTalkState& talk,
        const recovered::PackageView& font,QuarrelVisit visit,std::optional<unsigned> queued={},
        bool dismiss=false,bool fallbacks=false) {
    auto& words=talk.vocabulary();
    words.BeginConversation();
    talk.SetMood(2);
    const auto before=talk.learned_word_serial();
    Check(talk.BeginOrdinaryTopic(queued.value_or(0x7d43a),queued.has_value()),"Start quarrel conversation");
    unsigned last=talk.message_pc(),held=0,dispatches=0,prompts=0,editors=0,answers=0;
    bool sent=false;
    int names[2]={-1,-1};
    std::vector<unsigned> lines;
    for(unsigned tick=0;tick<24000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            const bool dispatch=*bound==1057;
            dispatches+=dispatch;
            talk.SupplyRandom(dispatch?339:0);
        }
        if(talk.word_entry_requested()) {
            Check((visit==QuarrelVisit::teach_first || visit==QuarrelVisit::teach_second) &&
                  editors++==0,"Unexpected name editor in quarrel");
            if(visit==QuarrelVisit::teach_first) talk.FinishWordEntry("Mira","\x82\xdd\x82\xe7");
            else talk.FinishWordEntry("Nori","\x82\xcc\x82\xe8");
        }
        unsigned buttons=0;
        if(!talk.answer_visible()) sent=false;
        if(talk.answer_interactive() && !sent && !held) {
            const unsigned pc=talk.message_pc();
            const bool final=pc==0x59ec7;
            if(final) {
                Check(visit==QuarrelVisit::story && talk.answer_options()==std::vector<std::string>{
                      "\x83\x5f\x83\x81\x82\xbe\x82\xe6","\x82\xb5\x82\xe5\x82\xa4\x82\xaa\x82\xc8\x82\xa2\x81\x63"},
                      "Wrong quarrel answers");
            }
            const unsigned index=(final && dismiss) || (pc==0x9389 && visit==QuarrelVisit::refuse);
            if(talk.answer_cursor()!=index) buttons=0x40;
            else { buttons=0x2000; sent=true; answers+=final; }
        }
        held=buttons;
        talk.Step(buttons,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Quarrel source boundary "+std::to_string(*pc));
        const unsigned pc=talk.message_pc();
        if(last==pc) continue;
        last=pc;
        if(pc==0x9389) {
            ++prompts;
            Check((talk.text().find("Nana")!=std::string::npos)==(visit==QuarrelVisit::teach_second),
                  "The player name belongs only to the second person prompt");
        }
        if(pc<0x59e2a || pc>0x59fa5) continue;
        lines.push_back(pc);
        const auto& ops=talk.message_word_operands();
        for(const auto& op:ops)
            Check(talk.text().substr(op.offset,op.length)==words.FieldText(op.row,op.field),
                  "Quarrel dialogue lost its selected dictionary field");
        if(pc==0x59e2a) {
            Check(ops.size()==2 && ops[0].local==1 && ops[1].local==3 && ops[0].row!=ops[1].row,
                  "Two quarrelling people did not keep distinct selected rows");
            names[0]=ops[0].row; names[1]=ops[1].row;
            Check(words.FieldText(names[0],1)=="Mira" && words.FieldText(names[1],1)=="Nori",
                  "Wrong people on deterministic quarrel draw");
        }
        if(pc==0x59e67 || pc==0x59e98 || pc==0x59f19 || pc==0x59f74) {
            const bool first=pc==0x59e67, body=pc==0x59f19 || pc==0x59f74;
            if(!body) Check(!ops.empty() && ops.front().row==names[first?0:1],"Person changed mid story");
            if(fallbacks) {
                const std::string fallback=body?"\x82\xb3\x82\xb1\x82\xc2":
                    first?"\x83\x4c\x83\x58":"\x82\xc5\x82\xb1\x83\x73\x83\x93";
                Check(ops.size()==unsigned(!body) && talk.text().find(fallback)!=std::string::npos,
                      "Wrong gesture or body-part fallback");
            } else {
                Check(ops.size()==(body?1u:2u) && ops.back().row==(body?60:first?107:108) &&
                      ops.back().field==1 && ops.back().local==(body?7u:first?5u:6u),
                      "Expected distinct starter gestures and a body-part row");
            }
        }
    }
    const bool story=visit==QuarrelVisit::story;
    const bool taught=visit==QuarrelVisit::teach_first || visit==QuarrelVisit::teach_second;
    Check(talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
          dispatches==unsigned(!queued) && prompts==unsigned(!story) && answers==unsigned(story) &&
          editors==unsigned(taught) && talk.learned_word_serial()==before+editors,
          "Quarrel did not exit the selected lookup or finish its question");
    if(story) {
        std::vector<unsigned> expected{0x59e2a,0x59e67,0x59e98,0x59ec7};
        if(dismiss) expected.insert(expected.end(),{0x59f51,0x59f74,0x59fa5});
        else expected.insert(expected.end(),{0x59f01,0x59f19});
        Check(lines==expected && talk.mood()==1,"Both quarrel replies must reach 2151(1)");
    } else Check(lines.empty(),"Inline lesson or refusal fell through into quarrel");
    return talk.TakeTopicEnqueues();
}

void QuarrelQuestions(gameplay::RoomTalkState cold,const recovered::PackageView& font) {
    auto refused=cold;
    Check(QuarrelContact(refused,font,QuarrelVisit::refuse).empty(),"Refused first name enqueued a quarrel");
    const std::vector<std::optional<unsigned>> expected{0x59d30};
    const auto first=QuarrelContact(cold,font,QuarrelVisit::teach_first);
    Check(first==expected && cold.learned_word()=="Mira","Missing deferred quarrel after first name");
    const auto second=QuarrelContact(cold,font,QuarrelVisit::teach_second,first.front());
    Check(second==expected && cold.learned_word()=="Nori","Missing deferred quarrel after second name");
    for(bool dismiss:{false,true}) {
        auto ordinary=cold;
        Check(QuarrelContact(ordinary,font,QuarrelVisit::story,second.front(),dismiss).empty(),
              "Completed quarrel did not consume its deferred visit");
    }
    // Component-only vocabulary with the two optional gesture/body lookups empty.
    for(unsigned row=107;row<=124;++row) cold.vocabulary().WriteNumber(row,7,0);
    for(unsigned row=60;row<=69;++row) cold.vocabulary().WriteNumber(row,7,0);
    Check(QuarrelContact(cold,font,QuarrelVisit::story,second.front(),true,true).empty(),
          "Fallback quarrel enqueued a visit");
}
