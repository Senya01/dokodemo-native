// In this component the keyboard modal supplies a spelling and reading to the
// actual KSC caller. The window check covers the keyboard and physical keys.
class CustomQuestionVisit {
public:
    CustomQuestionVisit(gameplay::RoomTalkState& talk,const recovered::PackageView& font)
        : talk_(talk),font_(font) {
        Check(talk_.BeginFriendsIntroduction(),"Could not reopen question preparation");
    }
    void Choose(std::size_t count,unsigned index,const std::string& selected) {
        Wait(Modal::ask);
        const auto& options=talk_.answer_options();
        Check(options.size()==count && options.at(index)==selected,"Wrong custom question ASK or substituted label");
        for(std::size_t moves=0;talk_.answer_cursor()!=index && moves<count;++moves) {
            Tick(0x40); Tick(0);
        }
        Check(talk_.answer_cursor()==index,"Could not highlight the custom question choice");
        Tick(0x2000);
        Check(talk_.answer_closing(),"Custom question choice was not accepted");
        Tick(0);
    }
    void Edit(const std::string& spelling,const std::string& reading) {
        Wait(Modal::keyboard);
        Check(!talk_.keyboard_initial_reading(),"Custom question used the correction keyboard");
        talk_.FinishWordEntry(spelling,reading);
    }
    void Finish() {
        Wait(Modal::done);
        Check(talk_.ordinary_source_return() && !talk_.counted_contact(),"Wrong local question cleanup");
    }
    void WaitForAsk() { Wait(Modal::ask); }
    std::map<unsigned,std::string> messages;
private:
    enum class Modal { ask,keyboard,done };
    gameplay::RoomTalkState& talk_;
    const recovered::PackageView& font_;
    void Tick(unsigned buttons) {
        if(auto count=talk_.random_requested()) {
            Check(*count==1,"Unexpected number of prepared first-meeting questions");
            talk_.SupplyRandom(0);
        }
        talk_.Step(buttons,true,true,&font_);
        if(!talk_.error().empty()) throw std::runtime_error(talk_.error());
        if(auto pc=talk_.ordinary_frontier())
            throw std::runtime_error("Custom question source frontier "+std::to_string(*pc));
        if(talk_.visible() && talk_.message_revealed())
            messages[talk_.message_pc()]=std::string(talk_.text());
    }
    void Wait(Modal expected) {
        for(unsigned i=0;i<5000;++i) {
            if(talk_.answer_interactive() || talk_.word_entry_requested() || talk_.finished()) {
                const auto actual=talk_.answer_interactive()?Modal::ask:
                                  talk_.word_entry_requested()?Modal::keyboard:Modal::done;
                Check(actual==expected,"Unexpected modal in custom question");
                return;
            }
            Tick(0);
        }
        throw std::runtime_error("Custom question did not reach its next input");
    }
};

void PreparedCustomQuestion(gameplay::RoomTalkState& talk,const recovered::PackageView& font) {
    const std::string prepare="\x8e\xbf\x96\xe2\x82\xf0\x97\x70\x88\xd3\x82\xb7\x82\xe9";
    const std::string recipient="\x8f\x89\x82\xdf\x82\xc4\x82\xcc\x90\x6c\x82\xd6";
    const std::string finish="\x8e\xbf\x96\xe2\x82\xcc\x97\x70\x88\xd3\x82\xa8\x82\xed\x82\xe8";
    const std::string cancel="\x82\xe2\x82\xc1\x82\xcf\x83\x84\x83\x81";
    const std::string back="\x82\xe0\x82\xc7\x82\xe9";
    const std::string blank_subject="\x81\x9b\x81\x9b",blank_predicate="\x81\x7e\x81\x7e";
    const std::string tte="\x82\xc1\x82\xc4",question="\x81\x48";
    const std::string custom=blank_subject+tte+" "+blank_predicate+question;
    const std::string subject="\x89\xd4",subject_reading="\x82\xcd\x82\xc8"; // 花 / はな
    const std::string predicate="\x8d\x44\x82\xab",predicate_reading="\x82\xb7\x82\xab"; // 好き / すき
    const std::string ok="\x82\xb1\x82\xea\x82\xc5 OK",entrust="\x82\xdc\x82\xa9\x82\xb9\x82\xbd";
    const auto subject_label=[&](const std::string& text) { return "\x81\x75\x16"+text+"\x16"+tte; };
    const auto predicate_label=[&](const std::string& text) { return " \x16"+text+"\x16"+question+"\x81\x76"; };
    const auto compose=[&](CustomQuestionVisit& visit) {
        visit.Choose(3,1,prepare);
        visit.Choose(2,0,recipient);
        visit.Choose(5,4,custom);
    };
    const std::vector<std::string> presets={
        "\x92\x6d\x82\xc1\x82\xc4\x82\xe9", "\x8b\xb9\x83\x4c\x83\x85\x83\x93",
        "\x91\xe5\x90\xd8", "\x82\xc7\x82\xa4"}; // 知ってる, 胸キュン, 大切, どう
    const auto choose_predicate=[&](CustomQuestionVisit& visit,unsigned which,const std::string& label) {
        visit.WaitForAsk();
        const auto& options=talk.answer_options();
        Check(options.size()==7,"Expected seven authored predicate choices");
        for(unsigned i=0;i<4;++i)
            Check(options[i]=="\x16"+presets[i]+"\x16"+question,"Wrong CT2B0C preset spelling or order");
        visit.Choose(7,which,label);
    };
    const auto expect_row=[&](unsigned expected_mask,unsigned minute) {
        const auto& vocab=talk.vocabulary();
        for(unsigned row=0;row<vocab.question_slots;++row) {
            for(unsigned field=0;field<7;++field) {
                const auto raw=vocab.QuestionField(row,field);
                if(!row && expected_mask) {
                    if(field==0) { Check(raw==(0x20000000|expected_mask),"Wrong custom question flags"); continue; }
                    if(field==2) { Check(raw==(0x30000000|minute),"Wrong custom question minute"); continue; }
                    if(expected_mask==0x20010a && (field==3 || field==5)) {
                        Check(raw>>28==5,"Missing stored custom question string"); continue;
                    }
                }
                Check(raw==0,"Unexpected surviving field in question owner");
            }
        }
        Check(!gameplay::CountIntroductionFriends(vocab),"Custom question added a friend");
    };
    talk.SetCalendarMinute(610);
    {
        CustomQuestionVisit visit(talk,font);
        compose(visit);
        visit.WaitForAsk();
        expect_row(0x20010b,610); // A59E allocated a temporary row before D1FE.
        visit.Choose(3,1,subject_label(blank_subject));
        visit.Edit({},{});
        visit.Choose(3,1,subject_label(blank_subject));
        visit.Edit(subject,subject_reading);
        visit.Choose(3,1,subject_label(subject));
        visit.Edit({},{}); // CF71 returns nil and D11C keeps the first slot.
        visit.WaitForAsk();
        Check(talk.answer_options().at(1)==subject_label(subject),"Cancelling subject erased its draft");
        visit.Choose(3,2,predicate_label(blank_predicate));
        choose_predicate(visit,2,"\x16"+presets[2]+"\x16"+question);
        visit.Choose(4,3,predicate_label(presets[2]));
        choose_predicate(visit,5,back);
        visit.Choose(4,3,predicate_label(presets[2]));
        choose_predicate(visit,4,blank_predicate+question);
        visit.Edit({},{}); // Unlike the subject, D194 explicitly clears the predicate.
        visit.Choose(3,2,predicate_label(blank_predicate));
        choose_predicate(visit,6,cancel); // Abort the form, release A59E's row, return to types.
        visit.WaitForAsk();
        expect_row(0,0);
        visit.Choose(5,4,custom);
        visit.Choose(3,0,"\x82\xe2\x82\xc1\x82\xcf \x83\x84\x83\x81"); // Form's やっぱ ヤメ.
        visit.WaitForAsk();
        expect_row(0,0);
        visit.Choose(5,4,custom);
        visit.Choose(3,1,subject_label(blank_subject));
        visit.Edit(subject,subject_reading);
        visit.Choose(3,2,predicate_label(blank_predicate));
        choose_predicate(visit,1,"\x16"+presets[1]+"\x16"+question);
        visit.Choose(4,1,ok);
        visit.Choose(3,2,cancel); // CF4D discard after Toro spoke a fully formed question.
        visit.Choose(5,0,finish);
        visit.Choose(3,2,cancel);
        visit.Finish();
        expect_row(0,0);
    }
    {
        CustomQuestionVisit visit(talk,font);
        compose(visit);
        visit.Choose(3,1,subject_label(blank_subject));
        visit.Edit(subject,subject_reading);
        visit.Choose(3,2,predicate_label(blank_predicate));
        choose_predicate(visit,4,blank_predicate+question);
        visit.Edit(predicate,predicate_reading);
        visit.Choose(4,1,ok);
        visit.Choose(3,1,back);
        visit.WaitForAsk();
        Check(talk.answer_options().at(2)==subject_label(subject) &&
              talk.answer_options().at(3)==predicate_label(predicate),"Preview Back lost the two draft strings");
        expect_row(0x20010b,610);
        visit.Choose(4,1,ok);
        visit.Choose(3,0,entrust);
        visit.Choose(3,2,cancel);
        visit.Finish();
        const std::string preview=subject+tte+"\x81\x63\n"+predicate+question+"\n";
        Check(visit.messages.at(0xb19d)==preview && visit.messages.count(0xb1b2) &&
              visit.messages.count(0xc9f3),"Custom question preview, request or acknowledgement missing");
        expect_row(0x20010a,610);
    }
    for(bool withdraw:{false,true}) {
        talk.SetCalendarMinute(withdraw?612:611);
        CustomQuestionVisit visit(talk,font);
        visit.Choose(3,1,prepare);
        visit.Choose(2,0,recipient);
        visit.WaitForAsk();
        const std::string stored=subject+tte+"\x81\x63\n"+predicate+question+"\n"+tte+"\x95\xb7\x82\xb1\x82\xa4\x81\x49";
        Check(talk.answer_options().at(0)==stored,"104DF did not reuse both stored spellings");
        if(withdraw) {
            visit.Choose(2,1,"\x82\xe2\x82\xc1\x82\xcf\x95\xb7\x82\xa9\x82\xc8\x82\xa2\x82\xc5");
            visit.Choose(5,0,finish);
        } else visit.Choose(2,0,stored);
        visit.Choose(3,2,cancel);
        visit.Finish();
        expect_row(withdraw?0:0x20010a,611);
    }
}
