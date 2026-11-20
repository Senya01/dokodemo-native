#include "gameplay/room_talk_state.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>

using namespace dokodemo;
namespace {
using Talk = gameplay::RoomTalkState;
using Font = recovered::PackageView;
void Require(bool condition,const char* reason) {
    if(!condition) throw std::runtime_error(reason);
}

struct Visit {
    std::vector<unsigned> messages, questions;
    std::map<unsigned,int> weakness_rows;
    bool saw_person=false;
    bool Saw(unsigned pc) const {
        return std::find(messages.begin(),messages.end(),pc)!=messages.end();
    }
};

struct AlienSelections {
    unsigned continuation, fear_line, last_draw;
    bool selected=false, rolled_fear=false, rolled_phrase=false, rolled_after_answer=false;
    std::map<unsigned,std::string> texts;
    std::map<unsigned,std::vector<std::string>> labels;
    std::map<unsigned,std::vector<gameplay::RoomMessageWord>> words;
    unsigned Draw(unsigned bound,unsigned message) {
        if(message==0x4f52e && bound==3) {
            if(!selected) { selected=true; return continuation; }
            Require(continuation==2 && !rolled_fear,"Repeated CT75F6 selection");
            rolled_fear=true; return fear_line;
        }
        if(message==0x4f52e && bound==6 && rolled_fear && fear_line==2) {
            Require(!rolled_phrase,"Repeated CT2485 phrase draw");
            rolled_phrase=true; return 5;
        }
        if(message==0x2b45 && bound==2) {
            Require(!rolled_after_answer,"Repeated 97BD response draw");
            rolled_after_answer=true; return last_draw;
        }
        return 0;
    }
};

// Component-controlled rank from the actual filtered D090 array.
Visit Contact(Talk& talk,const Font& font,unsigned list_count,unsigned rank,
              unsigned first_choice=0,unsigned later_choice=0,AlienSelections* alien=nullptr) {
    talk.vocabulary().BeginConversation();
    Require(talk.BeginOrdinaryTopic(0x7d43a),"Start D090 face-topic test");
    Visit result;
    bool drawn=false,answered=false;
    unsigned previous=0;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            unsigned pick=0;
            if(*bound>=957) {
                Require(!drawn && *bound==list_count && rank<*bound,"Unexpected fallback retry or D090 rank");
                pick=rank; drawn=true;
            } else if(alien) {
                if(talk.message_pc()==0x4f52e && !alien->selected && *bound==3) {
                    talk.SupplyRandom(*bound);
                    Require(talk.random_requested()==bound && talk.message_indirect_topic()==0x4f47b,
                            "Invalid procedure rank consumed the pending selection");
                }
                pick=alien->Draw(*bound,talk.message_pc());
            }
            talk.SupplyRandom(pick);
        }
        if(!talk.answer_visible()) answered=false;
        unsigned buttons=0;
        if(talk.answer_interactive() && !answered && !previous) {
            const unsigned pc=talk.message_pc();
            if(pc==0x55f69)
                Require(talk.answer_options()==std::vector<std::string>{"\x82\xd6\x81\x60","\x92\x6d\x82\xc1\x82\xc4\x82\xe9"},
                        "Wrong 55EE5 answer labels or order");
            const bool followup=alien ? pc!=0x4f4be : pc==0x1f1c8;
            const unsigned choice=followup ? later_choice : first_choice;
            if(talk.answer_cursor()!=choice) buttons=0x40;
            else {
                buttons=0x2000; answered=true;
                result.questions.push_back(pc);
                if(alien) alien->labels.emplace(pc,talk.answer_options());
            }
        }
        previous=buttons;
        talk.Step(buttons,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Face test source boundary "+std::to_string(*pc));
        Require(!talk.word_entry_requested(),"Unexpected second lesson in face test");
        const auto pc=talk.message_pc();
        if(pc && (result.messages.empty() || pc!=result.messages.back())) {
            result.messages.push_back(pc);
            if(pc==0x55f56 || pc==0x55f69 || pc==0x56006 || pc==0x5602b) {
                const auto& operands=talk.message_word_operands();
                const unsigned local=(pc==0x55f56 || pc==0x56006) ? 1u : 2u;
                Require(talk.message_indirect_topic()==0x55ee5 && operands.size()==1 &&
                        operands.front().local==local && operands.front().field==1,
                        "Weakness conversation lost the owner of its person or body-word substitution");
                const auto& op=operands.front();
                Require(talk.text().substr(op.offset,op.length)==talk.vocabulary().FieldText(op.row,op.field),
                        "Weakness conversation did not display the selected word");
                result.weakness_rows.emplace(pc,op.row);
            }
            if(alien && ((pc>=0x4f4b1 && pc<=0x4f52e) || (pc>=0x97bd && pc<0x99fb) ||
                         pc==0x2b45 || pc==0x2b5e)) {
                Require(talk.message_indirect_topic()==0x4f47b,"Nested reply lost its owning D090 conversation");
                alien->texts.emplace(pc,talk.text());
                alien->words.emplace(pc,talk.message_word_operands());
            }
        }
        if(pc==0x1f1c8) {
            for(const auto& op:talk.message_word_operands()) {
                if(op.row==163 && op.field==1 && op.local==4 &&
                   talk.text().substr(op.offset,op.length)==talk.vocabulary().FieldText(163,1))
                    result.saw_person=true;
            }
        }
    }
    Require(drawn && talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected(),
            "Missing positive source return from selected room topic");
    return result;
}

void AlienVisit(Talk talk,const Font& font,unsigned continuation,unsigned first,unsigned second,
                unsigned fear_line=0,unsigned last_draw=0,bool vehicle=false) {
    // The lesson supplies row 163. Only the explicit vehicle component changes
    // its category to give CT8A6E a matching entry. The other runs use the UFO fallback.
    if(vehicle) talk.vocabulary().WriteNumber(163,7,64);
    AlienSelections path{continuation,fear_line,last_draw};
    auto visit=Contact(talk,font,1057,453,first,second,&path);
    Require(path.selected && visit.Saw(0x4f4b1) && visit.Saw(0x4f4be) &&
            visit.Saw(0x4f4fc) && visit.Saw(0x4f52e),"Missing 4F47B conversation or CT75F6 call");
    const auto& opening=path.words.at(0x4f4be);
    const auto& later=path.words.at(0x4f52e);
    Require(opening.size()==1 && later.size()==1 && opening[0].row>=60 && opening[0].row<=69 &&
            later[0].row==opening[0].row && later[0].field==opening[0].field &&
            later[0].local==0 && opening[0].local==0,"Lost 4F47B body-word source field");
    const std::string body(talk.vocabulary().FieldText(opening[0].row,opening[0].field));
    Require(path.texts.at(0x4f4be).substr(opening[0].offset,opening[0].length)==body &&
            path.texts.at(0x4f52e).substr(later[0].offset,later[0].length)==body &&
            path.labels.at(0x4f4be)==std::vector<std::string>{
                "\x16"+body+"\x16\x90\xaf\x90\x6c\x81\x48",
                "\x82\xc7\x82\xf1\x82\xc8\x89\x46\x92\x88\x90\x6c\x81\x48"},
            "Wrong word substitution or initial alien reply labels");
    const auto& ride=path.words.at(0x4f4fc);
    if(vehicle) {
        Require(ride.size()==1 && ride[0].row==163 && ride[0].local==1 &&
                path.texts.at(0x4f4fc).substr(ride[0].offset,ride[0].length)==talk.vocabulary().FieldText(163,ride[0].field),
                "Missing eligible vehicle word in alien reply");
    } else Require(ride.empty() && path.texts.at(0x4f4fc).find("\x82\x74\x82\x65\x82\x6e")!=std::string::npos,
                   "Missing original UFO fallback");

    if(continuation<2) {
        const unsigned question=continuation==0 ? 0x997d : 0x9909;
        const unsigned reply=continuation==0 ? (second==0 ? 0x99b1 : 0x99d8) : (second==0 ? 0x9938 : 0x994f);
        Require(visit.questions==std::vector<unsigned>{0x4f4be,question} &&
                path.labels.at(question)==std::vector<std::string>{"\x95\x7c\x82\xa2\x82\xe6\x81\x60","\x91\x53\x91\x52"} &&
                visit.Saw(reply) && !path.rolled_fear,"Incorrect CT75F6 question or consequence");
    } else {
        const unsigned fear_lines[]={0x97d6,0x97f7,0x9824};
        const unsigned question=fear_lines[fear_line];
        const unsigned reply=second==0 ? 0x984e : last_draw==0 ? 0x989d : 0x98d0;
        Require(visit.questions==std::vector<unsigned>{0x4f4be,question} && path.rolled_fear &&
                path.rolled_phrase==(fear_line==2) && path.rolled_after_answer==(second==1) &&
                visit.Saw(second==0 ? 0x2b5e : 0x2b45) && visit.Saw(reply),
                "Incorrect 97BD phrase, comfort/poke response or return");
    }
    // D090 removes source slot 453. The old face topic at 970 now has rank 969.
    auto next=Contact(talk,font,1056,969,1);
    Require(next.questions==std::vector<unsigned>{0x1ef52} && next.Saw(0x1f090),
            "Following 4F47B return did not open a fresh ordinary conversation");
}

void ExpireFromFallback(Talk& talk,const Font& font) {
    // Source indices of 100 ordinary single-contact topics, in ascending
    // D090 order, all below both topics' indices 970 and 979. Drive them to
    // a real return so the unchanged 100-cell history evicts the first visit.
    const unsigned indices[]={40,41,42,50,70,76,77,133,134,138,143,155,160,162,166,169,176,185,186,203,
        207,210,212,219,243,244,246,252,264,268,269,279,284,285,286,291,294,303,314,329,
        332,333,334,336,337,338,355,359,362,364,399,406,411,414,420,423,443,445,448,472,
        489,507,511,514,533,535,557,558,570,576,578,579,581,590,593,610,614,624,625,629,
        630,636,644,645,649,650,652,653,654,655,657,658,663,664,667,668,669,670,671,672};
    static_assert(std::size(indices)==100);
    for(unsigned i=0;i<std::size(indices);++i)
        Contact(talk,font,1056-i,indices[i]-i);
}

enum class OccupationCase { decline, clarify_decline, empty, known_stop, new_yes, known_retry_new_no };

void OccupationVisit(Talk& talk,const Font& font,OccupationCase test,bool repeat=false,unsigned preface=0,
                     bool no_context_words=false) {
    const bool known=test==OccupationCase::known_stop || test==OccupationCase::known_retry_new_no;
    const bool learns=test==OccupationCase::new_yes || test==OccupationCase::known_retry_new_no;
    const bool disagree=test==OccupationCase::known_retry_new_no;
    auto& words=talk.vocabulary();
    const std::string imported(words.FieldText(2,1));
    const std::string taught="Luna",reading="\x82\xe9\x82\xc8";
    Require(!imported.empty() && words.FindWritten(imported)==2 && words.FindWritten(taught)==-1,
            "Expected imported occupation and one unused test spelling");
    // In this separate component case the imported rides and body parts are no
    // longer eligible for the two lookup predicates. Let 128C select its fallback.
    if(no_context_words)
        for(unsigned row=51;row<=69;++row) words.WriteNumber(row,0,words.FieldInt(row,0)&~4);
    const auto previous_lessons=talk.learned_word_serial();
    words.BeginConversation();
    Require(talk.BeginOrdinaryTopic(0x7d43a),"Start D090 for occupation question");
    bool chosen=false,sent=false;
    unsigned held=0,editors=0;
    std::vector<unsigned> asks;
    Visit seen;
    std::map<unsigned,int> spoken;
    for(unsigned ticks=0;ticks<16000 && !talk.finished();++ticks) {
        if(auto bound=talk.random_requested()) {
            unsigned pick=0;
            if(*bound>950) {
                Require(!chosen && *bound==(repeat?957u:1057u),"Occupation dispatcher re-rolled");
                chosen=true; pick=repeat?879u:979u;
            } else if(repeat && *bound==3 && talk.message_pc()==0x29cd) pick=preface;
            talk.SupplyRandom(pick);
        }
        if(talk.word_entry_requested()) {
            const bool duplicate=known && editors==0;
            ++editors;
            Require(editors<=(disagree?2u:1u),"Unexpected further occupation editor");
            if(test==OccupationCase::empty) talk.FinishWordEntry("","");
            else if(duplicate) talk.FinishWordEntry(imported, std::string(words.FieldText(2,2)));
            else {
                Require(learns,"Unexpected occupation editor");
                talk.FinishWordEntry(taught,reading);
            }
        }
        unsigned buttons=0;
        if(!talk.answer_visible()) sent=false;
        if(talk.answer_interactive() && !sent && !held) {
            const auto pc=talk.message_pc();
            unsigned pick=0;
            if(pc==0x1d6b7) pick=test==OccupationCase::clarify_decline ? 2u : test==OccupationCase::decline ? 1u : 0u;
            if(pc==0x8f5d) pick=1;
            if(pc==0x418a) pick=test==OccupationCase::known_stop ? 1u : 0u;
            if(pc==0x1d779 || pc==0x1d7e3) pick=disagree?1u:0u;
            Require(pick<talk.answer_options().size(),"Missing occupation answer label");
            if(talk.answer_cursor()!=pick) buttons=0x40;
            else {buttons=0x2000; sent=true; asks.push_back(pc);}
        }
        held=buttons;
        talk.Step(buttons,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Occupation source boundary "+std::to_string(*pc));
        const auto pc=talk.message_pc();
        if(talk.message_indirect_topic()!=0x1d66d || (!seen.messages.empty() && seen.messages.back()==pc)) continue;
        seen.messages.push_back(pc);
        if(pc==0x1d753 || pc==0x1d779 || pc==0x1d7b0 || pc==0x1d7e3 || pc==0x1d86e || pc==0x1d88b) {
            const auto& operands=talk.message_word_operands();
            const unsigned local=pc==0x1d753 ? 3u : (pc==0x1d779 || pc==0x1d86e) ? 4u : 2u;
            if(no_context_words && local!=2) {
                const char* fallback=local==3 ? "\x83\x5f\x83\x93\x83\x76\x83\x4a\x81\x5b" : "\x82\xb3\x82\xb1\x82\xc2";
                Require(operands.empty() && talk.text().find(fallback)!=std::string_view::npos,
                        "Wrong vehicle or body fallback after empty occupation lookup");
                spoken[pc]=-1; continue;
            }
            Require(operands.size()==1,"Missing occupation response word");
            const auto& op=operands.front();
            Require(op.local==local && talk.text().substr(op.offset,op.length)==words.FieldText(op.row,op.field),
                    "Occupation reply lost its source word or local");
            spoken[pc]=op.row;
            if(local==2)
                Require((words.FieldInt(op.row,11)&2)!=0,"Successful 8F84 did not mark the row before its reply");
        }
    }
    Require(chosen && talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
            seen.Saw(0x1d683) && seen.Saw(0x1d6b7) && !asks.empty() && asks.front()==0x1d6b7,
            "Missing owned occupation question, response or ordinary return");
    const unsigned prefaces[]={0x9c14,0x9c35,0x9c5f};
    for(unsigned i=0;i<3;++i)
        Require(seen.Saw(prefaces[i])==(repeat && preface==i),"Wrong first/repeated occupation preface");
    Require(seen.Saw(0x8f5d)==(test==OccupationCase::clarify_decline) &&
            seen.Saw(0x3fcf)==known && seen.Saw(0x418a)==known &&
            talk.learned_word_serial()==previous_lessons+unsigned(learns) && talk.TakeTopicEnqueues().empty(),
            "Wrong occupation clarification, existing-word retry or registration");
    Require(editors==(test==OccupationCase::decline || test==OccupationCase::clarify_decline ? 0u : disagree ? 2u : 1u),
            "Wrong number of editor openings");
    if(known)
        Require(words.FindWritten(imported)==2 && (words.FieldInt(2,0)&32),"Lost source update on existing occupation");
    if(!learns) {
        Require(!seen.Saw(0x1d753) && seen.Saw(0x3f59) && words.FindWritten(taught)==-1,
                "Declined or canceled occupation continued as a new word");
        return;
    }
    Require(talk.learned_word_row().has_value(),"No registered occupation row");
    const int row=int(*talk.learned_word_row());
    Require(words.FieldText(row,1)==taught && words.FieldText(row,2)==reading &&
            words.FieldInt(row,7)==8 && (words.FieldInt(row,8)&4) && (words.FieldInt(row,12)&6) &&
            (words.FieldInt(row,14)&1) && (words.FieldInt(row,15)&1),"Incorrect occupation teaching fields");
    if(!no_context_words)
        Require(spoken.at(0x1d753)>=51 && spoken.at(0x1d753)<=59 && spoken.at(0x1d779)>=60 && spoken.at(0x1d779)<=69,
                "Wrong vehicle or body row in occupation reply");
    Require(spoken.at(0x1d7b0)==row && spoken.at(0x1d7e3)==row && seen.Saw(0x1d822)==!disagree &&
            seen.Saw(0x1d856)==disagree && (!disagree || (spoken.at(0x1d86e)==spoken.at(0x1d779) && spoken.at(0x1d88b)==row)),
            "Missing vehicle/body/occupation continuation or wrong final answer");
}

void FirstVisit(Talk& talk,const Font& font,bool yes) {
    auto first=Contact(talk,font,1057,970,yes ? 0 : 1);
    Require(first.questions==std::vector<unsigned>{0x1ef52} &&
            first.Saw(yes ? 0x1efeb : 0x1f090) && !first.Saw(yes ? 0x1f090 : 0x1efeb) &&
            !first.Saw(0x1f1c8),"Wrong initial face answer or premature repeat");
}

void RepeatVisit(Talk& talk,const Font& font,bool remembered) {
    // Supply an otherwise missing eligible learned person in this component
    // by changing the taught row's category. Other cases retain the robot.
    talk.vocabulary().WriteNumber(163,7,4);
    for(unsigned choice:{0u,1u}) {
        Talk branch=talk;
        auto repeat=Contact(branch,font,957,870,choice,choice);
        const unsigned recall=remembered ? 0x1f12c : 0x1f17e;
        Require(repeat.questions==std::vector<unsigned>{recall,0x1f1c8} &&
                !repeat.Saw(0x1ef52) && !repeat.Saw(remembered ? 0x1f17e : 0x1f12c) &&
                repeat.saw_person && repeat.Saw(choice==0 ? 0x1f23e : 0x1f2a9) &&
                !repeat.Saw(choice==0 ? 0x1f2a9 : 0x1f23e),
                "Repeated face conversation lost the stored answer or selected person's response");
    }
}

// The private package adds drivers for 181A/186A calls and inspection of the
// five notes written by 26A5. All three source functions come from the game.
class FlagDriver {
public:
    explicit FlagDriver(unsigned address):entry(address) {}
    unsigned entry;
    std::vector<unsigned char> code{0x30,0};

    void Expect(bool expected) {
        const auto next=entry+static_cast<unsigned>(code.size())+7;
        code.insert(code.end(),{static_cast<unsigned char>(expected ? 0x12 : 0x11),
            static_cast<unsigned char>(next),static_cast<unsigned char>(next>>8),static_cast<unsigned char>(next>>16),
            0x86,0x70,0x31}); // Return false immediately on mismatch.
    }
    void CheckCall(unsigned bank,unsigned bit,int write,bool expected) {
        code.insert(code.end(),{0x95,static_cast<unsigned char>(bank),0x59,
            0x81,static_cast<unsigned char>(bit),static_cast<unsigned char>(bit>>8)});
        if(write>=0) code.push_back(write ? 0x87 : 0x86);
        if(write>=0) code.insert(code.end(),{0x20,3,0x1a,0x18,0});
        else code.insert(code.end(),{0x20,2,0x6a,0x18,0});
        code.push_back(0x89);
        Expect(expected);
    }
    void String(const std::string& s) {
        code.push_back(0x8e);
        code.insert(code.end(),s.begin(),s.end());
        code.push_back(0);
    }
    void Remember(const std::string& s) {
        String(s);
        code.insert(code.end(),{0x20,1,0xa5,0x26,0});
    }
    void CheckNote(unsigned slot,const std::string& s) {
        code.insert(code.end(),{0x95,0xa8,0x59,0x80,static_cast<unsigned char>(slot),0xf4});
        if(s.empty()) code.push_back(0xb3); // nil, not an empty string
        else { String(s); code.push_back(0xc0); }
        Expect(true);
    }
    void CheckTeaching(unsigned slot,unsigned expected) {
        code.insert(code.end(),{0x99,0x46,0x24,0x80,static_cast<unsigned char>(slot),0xf4,
            0x80,static_cast<unsigned char>(expected),0xc0});
        Expect(true);
    }
};

struct FixtureDirectory {
    std::filesystem::path dir;
    std::vector<std::filesystem::path> files;
    FixtureDirectory() {
        dir=std::filesystem::temp_directory_path()/("dokodemo-face-flags-"+std::to_string(std::random_device{}()));
        Require(std::filesystem::create_directory(dir),"Create private flag test directory");
    }
    ~FixtureDirectory() {
        std::error_code ec;
        for(const auto& path:files) std::filesystem::remove(path,ec);
        std::filesystem::remove(dir,ec);
    }
    std::filesystem::path Add(const char* name) {
        files.push_back(dir/name);
        return files.back();
    }
};

void BoundaryCalls(const std::filesystem::path& dir,const Font& font) {
    FlagDriver test(0xf00000),after_yes(0xf02000),after_no(0xf03000);
    FlagDriver canceled(0xf04000),learned(0xf05000),weakness_note(0xf06000);
    // The second answer expands both locals in 55FCC, including their emphasis controls.
    // Fixture row 2 is マンガ家 and the first matching body row (60) is 顔.
    weakness_note.CheckNote(0,"\x16\x83\x7d\x83\x93\x83\x4b\x89\xc6\x16\x82\xcc \x8e\xe3\x93\x5f\x82\xcd "
                              "\x16\x8a\xe7\x16\x82\xc1\x82\xc4 \x83\x52\x83\x67\x82\xf0 \x92\x6d\x82\xc1\x82\xc4\x82\xa2\x82\xe9");
    for(unsigned slot=1;slot<5;++slot) weakness_note.CheckNote(slot,"");
    for(auto* driver:{&canceled,&learned}) {
        driver->CheckCall(0xaf,22,-1,true);
        driver->CheckCall(0xb6,22,-1,false);
        driver->CheckTeaching(1,driver==&canceled ? 2 : 3);
        driver->CheckTeaching(3,8);
        driver->CheckTeaching(4,64);
        driver->CheckTeaching(7,4);
        driver->CheckTeaching(8,1);
        driver->CheckTeaching(9,1);
    }
    for(unsigned bank:{0xafu,0xb6u}) {
        const unsigned other=bank==0xaf ? 0xb6 : 0xaf;
        for(unsigned bit:{0u,27u,28u,167u}) {
            test.CheckCall(bank,bit,-1,false);
            test.CheckCall(bank,bit,1,true);
            test.CheckCall(bank,bit,1,true);
            test.CheckCall(bank,bit,-1,true);
            test.CheckCall(other,bit,-1,false);
        }
        for(int write:{-1,0,1}) test.CheckCall(bank,168,write,false);
        for(unsigned bit:{27u,0u,28u,167u}) {
            test.CheckCall(bank,bit,-1,true);
            test.CheckCall(bank,bit,0,true);
            test.CheckCall(bank,bit,0,true);
            test.CheckCall(bank,bit,-1,false);
        }
    }
    // 26A5 measures game characters. Include double-byte kana across the 64-character edge.
    std::string kana64;
    for(int i=0;i<64;++i) kana64+="\x82\xa0";
    test.Remember(kana64+"\x82\xa0");
    for(unsigned i=0;i<5;++i) test.CheckNote(i,"");
    test.Remember(kana64);
    for(const auto* value:{"B","C","D","E","replacement"}) test.Remember(value);
    test.CheckNote(0,kana64); test.CheckNote(1,"B"); test.CheckNote(2,"C");
    test.CheckNote(3,"replacement"); test.CheckNote(4,"E");
    // The phrase at 1F002 is stored only on the affirmative first answer.
    after_yes.CheckNote(0,"\x95\x5c\x82\xcc\x8a\xe7\x82\xc6 \x83\x45\x83\x89\x82\xcc\x8a\xe7\x82\xf0 \x8e\x67\x82\xa2\x95\xaa\x82\xaf\x82\xc4\x82\xe9");
    for(unsigned i=0;i<5;++i) {
        after_no.CheckNote(i,"");
        if(i) after_yes.CheckNote(i,"");
    }
    FixtureDirectory fixture;
    for(const char* name:{"toro-introduction.bin","neko-vocabulary.bin","toro-fallback-list.bin","toro-trait-labels.bin","toro-outings.bin"})
        std::filesystem::copy_file(dir/name,fixture.Add(name));
    std::ifstream input(dir/"toro-topics.bin",std::ios::binary);
    std::vector<unsigned char> image{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>{}};
    auto word=[&](std::size_t pos) {
        unsigned value=0;
        for(unsigned n=0;n<4;++n) value|=unsigned(image.at(pos+n))<<(8*n);
        return value;
    };
    Require(word(0)==0x35505452,"Flag driver requires an RTP5 package");
    const unsigned count=word(4);
    std::size_t insert_at=28;
    for(unsigned i=0;i<count;++i) insert_at+=8+word(insert_at+4);
    for(unsigned n=0;n<4;++n) image[4+n]=static_cast<unsigned char>((count+6)>>(8*n));
    std::vector<unsigned char> record;
    for(auto* driver:{&test,&after_yes,&after_no,&canceled,&learned,&weakness_note}) {
        driver->code.insert(driver->code.end(),{0x87,0x70,0x31});
        for(unsigned value:{driver->entry,static_cast<unsigned>(driver->code.size())})
            for(unsigned n=0;n<4;++n) record.push_back(static_cast<unsigned char>(value>>(8*n)));
        record.insert(record.end(),driver->code.begin(),driver->code.end());
    }
    image.insert(image.begin()+insert_at,record.begin(),record.end());
    const auto fixture_topics=fixture.Add("toro-topics.bin");
    auto write_topics=[&]() {
        std::ofstream out(fixture_topics,std::ios::binary);
        out.write(reinterpret_cast<const char*>(image.data()),image.size());
        out.close(); Require(bool(out),"Write private flag test package");
    };
    write_topics();
    Talk talk;
    gameplay::AcceptedProfile profile{};
    profile.nickname.Assign("Toro"); profile.player.Assign("Nana");
    std::string error;
    if(!talk.Load(fixture.dir,profile,error)) throw std::runtime_error(error);
    talk.Step(0,true,true,&font,true);
    for(unsigned tick=0;tick<8000 && !talk.finished();++tick) {
        if(talk.gift_requested()) talk.SupplyGift(-1);
        talk.Step(0,true,true,&font);
    }
    auto run=[&](Talk& owner,const FlagDriver& driver) {
        Require(owner.BeginOrdinaryTopic(driver.entry,true),"Start component-only inspection");
        for(unsigned tick=0;tick<8000 && !owner.finished();++tick) {
            if(auto bound=owner.random_requested()) owner.SupplyRandom(*bound==5 ? 3 : 0);
            owner.Step(0,true,true,&font);
            if(!owner.error().empty()) throw std::runtime_error(owner.error());
        }
        Require(owner.ordinary_source_return() && !owner.ordinary_rejected(),
                "Flag boundary, note slot or owner isolation mismatch");
    };
    Talk yes=talk,no=talk;
    FirstVisit(yes,font,true); run(yes,after_yes);
    FirstVisit(no,font,false); run(no,after_no);
    Talk missing=talk;
    auto decline=Contact(missing,font,1057,380,1);
    Require(decline.questions==std::vector<unsigned>{0x9389} && decline.weakness_rows.empty() &&
            !missing.learned_word_serial() && missing.TakeTopicEnqueues().empty(),
            "Refusing the 55EE5 name request entered its story or queued a topic");
    run(missing,after_no);
    for(unsigned answer:{0u,1u}) {
        Talk weakness=talk;
        auto& vocab=weakness.vocabulary();
        // Cover each source group in CT9528 with an independently owned copy of row 2.
        if(answer==0) vocab.WriteNumber(2,7,4);
        vocab.WriteNumber(2,0,vocab.FieldInt(2,0)|(answer==0 ? 32 : 256));
        auto visit=Contact(weakness,font,1057,380,answer);
        Require(visit.questions==std::vector<unsigned>{0x55f69} && !visit.Saw(0x9389) &&
                visit.weakness_rows==std::map<unsigned,int>{{0x55f56,2},{0x55f69,60},{0x56006,2},{0x5602b,60}} &&
                visit.Saw(0x55f94)==(answer==0) && visit.Saw(0x55fab)==(answer==1) &&
                visit.Saw(0x55fc1)==(answer==1) && weakness.TakeTopicEnqueues().empty(),
                "Wrong CT9528/CT9535 row, weakness answer or common return");
        run(weakness,answer==0 ? after_no : weakness_note);
    }
    run(talk,test);
    for(auto test_case:{OccupationCase::decline,OccupationCase::clarify_decline,OccupationCase::empty,
                        OccupationCase::known_stop,OccupationCase::new_yes,OccupationCase::known_retry_new_no}) {
        // A fresh positive copy has neither the face history nor another
        // user's profession. Every insertion and retry below goes through 93BA.
        Talk occupation=talk;
        OccupationVisit(occupation,font,test_case);
        run(occupation,(test_case==OccupationCase::new_yes || test_case==OccupationCase::known_retry_new_no) ? learned : canceled);
        if(test_case==OccupationCase::decline) {
            ExpireFromFallback(occupation,font);
            for(unsigned preface=0;preface<3;++preface) {
                Talk repeated=occupation;
                OccupationVisit(repeated,font,OccupationCase::empty,true,preface);
                run(repeated,canceled);
            }
        }
    }
    Talk no_context_words=talk;
    OccupationVisit(no_context_words,font,OccupationCase::new_yes,false,0,true);
    run(no_context_words,learned);

    // A valid direct-call target must be both a code reference and the start
    // of a registered function. Damage only the private projection for each case.
    std::size_t record_pos=28;
    for(unsigned section=0;section<5;++section)
        for(unsigned i=0;i<word(4+4*section);++i) record_pos+=8+word(record_pos+4);
    std::size_t target_pos=0;
    for(unsigned i=0;i<word(24);++i) {
        if(word(record_pos)==0x75f6) target_pos=record_pos+12;
        record_pos+=8+word(record_pos+4);
    }
    Require(target_pos && word(target_pos)==0x70009972,"Missing first tagged CT75F6 procedure");
    for(unsigned invalid:{0x50009972u,0x70009974u}) {
        for(unsigned n=0;n<4;++n) image.at(target_pos+n)=static_cast<unsigned char>(invalid>>(8*n));
        write_topics();
        Talk damaged;
        error.clear();
        Require(!damaged.Load(fixture.dir,profile,error) && error=="Missing script procedure in call list",
                "Malformed CT75F6 target did not fail package validation");
    }
}
#include "room_contact_history_test.inc"
} // namespace

void CheckFaceQuestions(const std::filesystem::path& dir,const Font& font,Talk cold) {
    BoundaryCalls(dir,font);
    for(unsigned continuation:{0u,1u})
        for(unsigned answer:{0u,1u})
            AlienVisit(cold,font,continuation,answer,answer,0,0,continuation==1 && answer==1);
    AlienVisit(cold,font,2,1,0,0);
    AlienVisit(cold,font,2,0,1,1,0);
    AlienVisit(cold,font,2,1,1,2,1);
    Talk yes=cold,no=cold;
    FirstVisit(yes,font,true);
    FirstVisit(no,font,false);
    ExpireFromFallback(yes,font);
    ExpireFromFallback(no,font);
    Talk missing=no;
    auto declined=Contact(missing,font,957,870,1);
    Require(declined.Saw(0x9389) && !declined.Saw(0x1f12c) && !declined.Saw(0x1f17e) &&
            !declined.Saw(0x1f1c8) && missing.learned_word_serial()==1 && missing.TakeTopicEnqueues().empty(),
            "Declining the missing-person lesson entered the face follow-up");
    CheckContactHistory(cold,yes,no,font);
    RepeatVisit(yes,font,true);
    RepeatVisit(no,font,false);
}
