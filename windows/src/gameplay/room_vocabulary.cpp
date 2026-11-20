#include "room_vocabulary.h"
#include "room_learned_word.h"
#include "recovered/game_primitives.h"
#include "recovered/service39_native.h"
#include "recovered/rng_native.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
namespace {
void Require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

unsigned Read(std::istream& in, unsigned size) {
    unsigned value = 0;
    for (unsigned i = 0; i < size; ++i) {
        const int byte = in.get();
        Require(byte != std::char_traits<char>::eof(), "Truncated Toro vocabulary");
        value |= unsigned(byte) << (8*i);
    }
    return value;
}

std::string ReadString(std::istream& in) {
    std::string value(Read(in, 2), '\0');
    Require(bool(in.read(value.data(), value.size())), "Truncated dictionary word");
    return value;
}

int Number(std::uint32_t word) {
    Require((word >> 28) <= 3, "Nonnumeric dictionary field");
    const int n = int(word & 0x0fffffff);
    return (n & 0x08000000) ? n - 0x10000000 : n;
}

std::uint32_t Integer(int n) { return 0x20000000u | (std::uint32_t(n) & 0x0fffffffu); }

std::uint32_t CurrentMinute() {
    const auto now = std::time(nullptr);
    std::tm t{};
#ifdef _WIN32
    Require(localtime_s(&t, &now) == 0, "Cannot read local dictionary clock");
#else
    Require(localtime_r(&now, &t) != nullptr, "Cannot read local dictionary clock");
#endif
    int year = t.tm_year+1900, month = t.tm_mon+1;
    if (month < 3) { --year; month += 12; }
    const auto days = 365ll*year + year/4 - year/100 + year/400 + 30*month +
                      (3*month+3)/5 + t.tm_mday - 33;
    const auto minute = days*1440 + t.tm_hour*60 + t.tm_min - 1051898400ll;
    return 0x30000000u | (std::uint32_t(minute) & 0x0fffffffu);
}

KscSaveValue TextValue(std::string_view text) {
    return {0x50000000u,std::string(text),{}};
}

std::string NameReading(std::string_view name) {
    std::vector<std::uint8_t> input(name.begin(),name.end());
    input.push_back(0);
    const auto bytes=recovered::Service39Normalize(input).bytes;
    return std::string(bytes.begin(),bytes.end()-1);
}

// The source stores a reference to an array, including one for each member row.
KscSaveValue Table(std::size_t n) {
    KscSaveValue array{0x90000000u | std::uint32_t(n),{},{}};
    array.children.reserve(n);
    return {0xb0000000u,{},{std::move(array)}};
}

const std::vector<KscSaveValue>& TableFields(const KscSaveValue& ref, std::size_t count) {
    Require(ref.word>>28==11 && ref.children.size()==1,"Invalid saved table reference");
    const auto& a=ref.children.front();
    Require(a.word==(0x90000000u|count) && a.children.size()==count,"Wrong saved table shape");
    return a.children;
}

bool Predicate(std::uint32_t field, unsigned op, std::uint32_t arg) {
    if (op == 0) return field == arg;
    if (op == 1) return field != arg;
    const auto lhs = Number(field), rhs = Number(arg);
    switch (op) {
    case 2: return lhs > rhs;
    case 3: return lhs >= rhs;
    case 4: return lhs < rhs;
    case 5: return lhs <= rhs;
    case 6: return (lhs & rhs) == rhs;
    case 7: return (lhs & rhs) == 0;
    case 8: return (lhs & rhs) != 0;
    default: throw std::runtime_error("Unknown dictionary predicate");
    }
}
}

bool RoomVocabulary::Load(const std::filesystem::path& dir, const AcceptedProfile& profile,
                          std::string& error) {
    try {
        constexpr std::array dictionaries{"neko-vocabulary.bin", "robo-vocabulary.bin", "usagi-vocabulary.bin",
                                           "inu-vocabulary.bin", "kaeru-vocabulary.bin"};
        Require(profile.actor < dictionaries.size(), "Missing first-conversation dictionary for actor");
        const bool alternate=profile.actor>=2;
        std::ifstream in(dir/dictionaries[profile.actor], std::ios::binary);
        Require(in && Read(in, 4) == (alternate ? 0x34435652u : 0x33435652u) &&
                Read(in, 2) == 162, "Missing actor vocabulary");
        RoomVocabulary next;
        if(profile.actor==1) next.saying_filters_={0x28e1,0x28eb};
        next.rows_.resize(700);
        for (unsigned i=1; i<=162; ++i) {
            Row& row=next.rows_[i];
            for (auto& field : row.fields) field=Read(in, 4);
            row.written=ReadString(in);
            row.reading=ReadString(in);
            row.alternate=ReadString(in);
        }
        const unsigned filters=Read(in, 2);
        Require(filters > 0, "Invalid dictionary filter count");
        for (unsigned i=0; i<filters; ++i) {
            const unsigned ct=Read(in,2), count=Read(in,2);
            Require(!next.conditions_.count(ct) && count && count <= 16, "Invalid dictionary filter");
            auto& groups=next.conditions_[ct];
            for (unsigned j=0; j<count; ++j) {
                const unsigned terms=Read(in,2);
                Require(terms && terms <= 32, "Invalid dictionary term count");
                auto& group=groups.emplace_back();
                for (unsigned k=0; k<terms; ++k) {
                    const unsigned field=Read(in,1), op=Read(in,1), arg=Read(in,4);
                    Require(field < (profile.actor==0?19u:30u) && op <= 8 && (arg>>28 == 2 || (arg==0 && op<=1)),
                            "Invalid dictionary term");
                    group.push_back({field,op,arg});
                }
            }
        }
        const unsigned exclusions=Read(in,2);
        for (unsigned j=0; j<exclusions; ++j) {
            const unsigned ct=Read(in,2), count=Read(in,2);
            Require(!next.exclusions_.count(ct), "Duplicate dictionary exclusion list");
            auto& list=next.exclusions_[ct];
            for (unsigned k=0; k<count; ++k) list.push_back(ReadString(in));
        }
        for (auto& row : next.questions_) row[0]=Read(in,4);
        unsigned sayings=5;
        if(alternate) {
            sayings=Read(in,2);
            next.nonzero_saying_filter_=Read(in,2);
            next.saying_filters_={Read(in,2),Read(in,2)};
            Require(sayings && sayings<=16 && next.conditions_.count(*next.nonzero_saying_filter_) &&
                    next.conditions_.count(next.saying_filters_[0]) && next.conditions_.count(next.saying_filters_[1]),
                    "Invalid alternate saying selector");
        }
        next.initial_sayings_.resize(sayings);
        for (auto& saying : next.initial_sayings_) saying=ReadString(in);
        Require(in.peek() == std::char_traits<char>::eof(), "Trailing actor vocabulary data");

        // 1936 assigns nickname/player to the live composite rows. 17D5 previously
        // ran on the cold field9 masks, before 19E2 supplies the player's gender.
        next.rows_[0].fields[0]=next.rows_[600].fields[0]=Integer(2);
        next.WriteText(0,1,std::string(profile.nickname.text()));
        next.WriteText(0,2,NameReading(profile.nickname.text()));
        next.WriteText(600,1,std::string(profile.player.text()));
        next.WriteText(600,2,NameReading(profile.player.text()));
        // 1992..19E1: player record fields 21..25 from the live profile.
        next.friend_tails_.resize(100);
        auto& player=next.friend_tails_.front();
        player[21-19]=TextValue(profile.nickname.text());
        player[22-19].word=Integer(0);
        player[23-19].word=Integer(int(profile.packed_birthday));
        player[24-19].word=Integer(profile.blood);
        player[25-19]=TextValue(profile.phone.text());
        next.rows_[600].fields[9]=Integer(profile.gender == 1 ? 1 : 2);
        *this=std::move(next);
        error.clear();
        return true;
    } catch (const std::exception& e) { error=e.what(); return false; }
}

KscSaveValue RoomVocabulary::DictionarySaveRow(unsigned id) const {
    auto row=Table(id<600?19:30);
    auto& fields=row.children.front().children;
    for (unsigned field=0;field<19;++field) {
        const auto word=rows_.at(id).fields[field];
        if ((word>>28)==5 || (word>>28)==6) fields.push_back(TextValue(FieldText(id,field)));
        else fields.push_back({word,{},{}});
    }
    if (id>=600) {
        const auto& tail=friend_tails_.at(id-600);
        fields.insert(fields.end(),tail.begin(),tail.end());
        if (id==600) fields[26]=TextValue(player_saying_);
    }
    return row;
}

KscSaveValue RoomVocabulary::DictionarySaveTable() const {
    Require(started_ && !pending_ && rows_.size()==700 && friend_tails_.size()==100,"Dictionary not ready for save");
    auto result=Table(rows_.size());
    for (unsigned id=0;id<rows_.size();++id)
        result.children.front().children.push_back(DictionarySaveRow(id));
    return result;
}

KscSaveValue RoomVocabulary::OwnFriendCard() const {
    return FriendCard(600);
}

KscSaveValue RoomVocabulary::FriendCard(unsigned row) const {
    Require(started_ && rows_.size()==700 && row>=600 && row<700,"Friend card unavailable");
    return DictionarySaveRow(row).children.front();
}

std::optional<unsigned> RoomVocabulary::FriendCardSlot(const KscSaveValue& row) const {
    Require(started_ && rows_.size()==700 && row.word==0x9000001eu &&
            row.children.size()==30 && row.text.empty(),"Invalid friend card shape");
    const auto& fields=row.children;
    for (unsigned i=0;i<fields.size();++i) {
        const auto& field=fields[i];
        const auto tag=field.word>>28;
        Require(field.children.empty(),"Invalid child in a friend card");
        if (tag==5 || tag==6) {
            Require(i==1 || i==2 || i==13 || i==21 || i==25 || i==26,"Unexpected text in a friend card");
            Require(field.text.size()<=128,"Oversized friend card text");
            for (std::size_t cursor=0;cursor<field.text.size();) {
                recovered::Glyph glyph;
                Require(recovered::read_glyph(reinterpret_cast<const std::uint8_t*>(field.text.data()+cursor),
                        field.text.size()-cursor,glyph) && glyph.index>=32 && glyph.index<8224,
                        "Invalid glyph in a friend card");
                cursor+=glyph.consumed;
            }
        } else {
            Require(tag<=3 && field.text.empty(),"Invalid scalar in a friend card");
        }
    }
    Require((Number(fields[0].word)&2) && !fields[1].text.empty() &&
            !fields[2].text.empty() && !fields[21].text.empty(),"Incomplete peer identity");
    std::optional<unsigned> empty;
    for (unsigned id=601;id<rows_.size();++id) {
        if (!HasField(id,1)) {
            if (!empty) empty=id;
            continue;
        }
        const auto& tail=friend_tails_.at(id-600);
        if (FieldText(id,1)==fields[1].text && tail[2].text==fields[21].text &&
            tail[4].word==fields[23].word && tail[6].text==fields[25].text) return id;
    }
    return empty;
}

unsigned RoomVocabulary::ReceiveFriendCard(const KscSaveValue& row) {
    const auto target=FriendCardSlot(row);
    Require(target.has_value(),"Friend notebook has no available slot");
    KscSaveValue saved{0xb0000000u,{},{row}};
    auto& flags=saved.children.front().children[0].word;
    flags=Integer(Number(flags)&~2);
    RestoreDictionaryRow(*target,saved);
    return *target;
}

KscSaveValue RoomVocabulary::QuestionSaveTable() const {
    auto result=Table(question_slots);
    for(unsigned i=0;i<question_slots;++i) {
        auto row=Table(7);
        for(unsigned k=0;k<7;++k) {
            const auto word=QuestionField(i,k);
            row.children.front().children.push_back((word>>28)==5 || (word>>28)==6 ?
                TextValue(QuestionText(i,k)) : KscSaveValue{word,{},{}});
        }
        result.children.front().children.push_back(std::move(row));
    }
    return result;
}

void RoomVocabulary::RestoreDictionaryRow(unsigned id,const KscSaveValue& saved) {
    const auto& fields=TableFields(saved,id<600?19:30);
    Row row;
    for(unsigned k=0;k<19;++k) {
        const auto& v=fields[k];
        const auto tag=v.word>>28;
        if(tag==5 || tag==6) {
            Require(k==1 || k==2 || k==13,"Invalid saved vocabulary text column");
            (k==1 ? row.written : k==2 ? row.reading : row.alternate)=v.text;
            row.fields[k]=0x50000000u;
        } else {
            Require(tag<=3,"Unsupported dictionary scalar");
            row.fields[k]=v.word;
        }
    }
    if(id>=600) {
        std::array<KscSaveValue,11> tail;
        std::copy(fields.begin()+19,fields.end(),tail.begin());
        if(id==600) {
            Require(fields[26].word>>28==5 || fields[26].word>>28==6,"Missing player saying");
            player_saying_=fields[26].text;
        }
        friend_tails_.at(id-600)=std::move(tail);
    }
    rows_.at(id)=std::move(row);
}

void RoomVocabulary::InheritDictionary(const KscSaveValue& dictionary,const KscSaveValue& relation_group) {
    Require(!started_ && !pending_ && rows_.size()==700,"Expected a freshly adopted vocabulary");
    auto next=*this;
    const auto& saved=TableFields(dictionary,700);
    for(unsigned id=1;id<saved.size();++id) {
        // 1936..1A27 installs the current Pokepi and the player's own card.
        if(id==600) continue;
        next.RestoreDictionaryRow(id,saved[id]);
        // NEKO 8D3A3 / CTE2A5 retains bit 0 of field 11.
        auto& flags=next.rows_[id].fields[11];
        flags=Integer(Number(flags)&1);
    }
    next.RestoreRelationGroupCounter(relation_group);
    *this=std::move(next);
}

RoomVocabulary RoomVocabulary::WithSaveTables(const KscSaveValue& dictionary,const KscSaveValue& questions) const {
    Require(!pending_ && rows_.size()==700,"Vocabulary must be loaded before restoring");
    auto next=*this;
    const auto& saved=TableFields(dictionary,700);
    for(unsigned id=0;id<saved.size();++id) next.RestoreDictionaryRow(id,saved[id]);
    const auto& table=TableFields(questions,question_slots);
    for(unsigned i=0;i<question_slots;++i) {
        const auto& fields=TableFields(table[i],7);
        for(unsigned k=0;k<7;++k) {
            const auto& v=fields[k];
            if((v.word>>28)==5 || (v.word>>28)==6) next.WriteQuestionText(i,k,v.text);
            else {
                Require((v.word>>28)<=3,"Unsupported question field");
                next.WriteQuestionField(i,k,v.word);
            }
        }
    }
    next.started_=true;
    return next;
}

void RoomVocabulary::RestoreSaveTables(const KscSaveValue& dictionary,const KscSaveValue& questions) {
    *this=WithSaveTables(dictionary,questions);
}

void RoomVocabulary::StartActorScript(std::uint32_t draw,recovered::RngNativeOwner* rng) {
    Require(!started_ && !initial_sayings_.empty(), "Unexpected actor cold script start");
    const std::string& fallback=initial_sayings_[draw % initial_sayings_.size()]; // F6 mode 0
    auto primary=saying_filters_[0];
    if(nonzero_saying_filter_) {
        Require(rng!=nullptr,"Missing actor saying selector RNG");
        if(rng->NextC3234()%2) primary=*nonzero_saying_filter_;
    }
    // 179E then searches for a learned saying. Even with no result, EA0
    // can leave secondary marks. 1211 bypasses 122F on an empty result.
    const unsigned matches=Candidates(primary,saying_filters_[1],true);
    Require(!matches || rng,"Missing cold saying RNG");
    const int row=FinishLookup(matches ? rng->NextC3234()%matches : 0);
    player_saying_=Word(row,fallback,0,false);
    started_=true;
}

void RoomVocabulary::BeginConversation() {
    Require(!pending_, "Conversation began with pending dictionary draw");
    for (Row& row : rows_) row.fields[11]=Integer(Number(row.fields[11]) & ~2);
}

std::uint32_t RoomVocabulary::PredicateWord(unsigned row,unsigned field) const {
    if(field<rows_.at(row).fields.size()) return rows_[row].fields[field];
    return row>=600 ? friend_tails_.at(row-600).at(field-19).word : 0;
}

void RoomVocabulary::Mark(unsigned ct, unsigned mask, bool honor_repeat) {
    const auto found=conditions_.find(ct);
    if(found==conditions_.end()) throw std::runtime_error("Unknown Toro dictionary filter "+std::to_string(ct));
    for (const auto& group : found->second) {
        for (unsigned row=0;row<rows_.size();++row) {
            bool match=true;
            for (unsigned i=0; i<group.size(); ++i) {
                // ED4 temporarily sets the second comparison's opcode to 16.
                if (!honor_repeat && i == 1) continue;
                const auto& [field,op,arg]=group[i];
                if (!Predicate(PredicateWord(row,field),op,arg)) { match=false; break; }
            }
            auto& marks=rows_[row].fields[17];
            if (match) marks=Integer(Number(marks) | int(mask));
        }
    }
}

void RoomVocabulary::Collect(unsigned mask) {
    // Cold 8E166 sets CT243F[0] to zero. 1113 prefers usage <5,
    // then all flagged rows in the order visited by builtin96.
    for (bool low_usage : {true,false}) {
        for (unsigned i=0; i<rows_.size(); ++i) {
            const auto& f=rows_[i].fields;
            if ((unsigned(Number(f[17])) & mask)==mask && (!low_usage || Number(f[4])<5))
                matches_.push_back(int(i));
        }
        if (!matches_.empty()) return;
    }
}

unsigned RoomVocabulary::Candidates(unsigned primary_ct, unsigned secondary_ct, bool mark) {
    Require(!rows_.empty() && !pending_, "Dictionary lookup unavailable");
    matches_.clear();
    if (primary_ct) {
        Mark(primary_ct,1,mark);
        if (secondary_ct) { Mark(secondary_ct,2,true); Collect(3); }
        if (matches_.empty()) Collect(1);
    }
    mark_=mark;
    pending_=true;
    return unsigned(matches_.size());
}

bool RoomVocabulary::MatchesPredicate(unsigned row,unsigned ct) const {
    const auto& group=conditions_.at(ct).at(0);
    return std::all_of(group.begin(),group.end(),[&](const Term& term) {
        return Predicate(PredicateWord(row,term.field),term.op,term.arg);
    });
}

unsigned RoomVocabulary::CountMatches(unsigned ct) const {
    unsigned count=0;
    for(unsigned row=0;row<rows_.size();++row)
        count+=MatchesPredicate(row,ct);
    return count;
}

int RoomVocabulary::FirstOutingMatch(unsigned ct) const {
    for(const auto& group : conditions_.at(ct)) {
        // 0F45 temporarily makes the group's second term inactive for builtin96.
        for(unsigned row=0;row<rows_.size();++row) {
            bool match=true;
            for(unsigned i=0;i<group.size();++i) {
                const auto& term=group[i];
                if(i!=1 && !Predicate(PredicateWord(row,term.field),term.op,term.arg)) {
                    match=false; break;
                }
            }
            if(match) return static_cast<int>(row);
        }
    }
    return -1;
}

std::vector<int> RoomVocabulary::PredicateGroup(unsigned ct,unsigned group) const {
    const auto& terms=conditions_.at(ct).at(group);
    std::vector<int> values;
    values.reserve(3*terms.size());
    for(const auto& term : terms) {
        values.push_back(int(term.field));
        values.push_back(int(term.op));
        values.push_back(Number(term.arg));
    }
    return values;
}

int RoomVocabulary::FinishLookup(unsigned pick) {
    Require(pending_, "Missing dictionary lookup");
    if (matches_.empty()) {
        Require(!pick, "No candidate for dictionary draw");
        pending_=false;
        return -1;
    }
    Require(pick<matches_.size(), "Dictionary draw outside candidate list");
    const int id=matches_[pick];
    auto& f=rows_[id].fields;
    const auto stamp=CurrentMinute();
    if (mark_) f[11]=Integer(Number(f[11]) | 2);
    f[4]=Integer(Number(f[4])+1);
    f[6]=stamp;
    for (Row& row : rows_) row.fields[17]=Integer(0);
    pending_=false;
    return id;
}

std::string RoomVocabulary::Word(int id, const std::string& fallback, unsigned excluded_ct,
                                 bool alternate, bool* used_row) const {
    auto form=ResolveWord(id,fallback,excluded_ct,alternate);
    if (used_row) *used_row=form.field!=0;
    return std::move(form.text);
}

RoomVocabulary::WordForm RoomVocabulary::ResolveWord(int id,const std::string& fallback,
                                                     unsigned excluded_ct,bool alternate) const {
    if (id == -1) return {fallback,0,0};
    const auto& row=rows_.at(id);
    if (excluded_ct) {
        const auto& list=exclusions_.at(excluded_ct);
        if (std::find(list.begin(),list.end(),row.written)!=list.end()) return {fallback,0,0};
    }
    if (alternate && row.fields[13]) return {row.alternate,13,row.alternate.size()};
    return {row.written,1,row.written.size()};
}

std::string RoomVocabulary::DisplayName(unsigned id) const {
    return ResolveDisplayName(id).text;
}

RoomVocabulary::WordForm RoomVocabulary::ResolveDisplayName(unsigned id) const {
    const Row& row=rows_.at(id);
    if (id!=600 && row.fields[13]) return {row.alternate,13,row.alternate.size()};
    return {id>600 ? row.written+"\x82\xb3\x82\xf1" : row.written,1,row.written.size()};
}

int RoomVocabulary::FieldInt(unsigned id, unsigned field) const {
    // These are the projected scalar columns. String references have no portable address.
    Require(field!=1 && field!=2 && field!=13, "String dictionary field requested as integer");
    return Number(rows_.at(id).fields.at(field));
}

bool RoomVocabulary::HasField(unsigned id,unsigned field) const {
    const Row& row=rows_.at(id);
    if(field==1) return !row.written.empty() || row.fields[1];
    return row.fields.at(field)!=0;
}

std::string_view RoomVocabulary::FieldText(unsigned id,unsigned field) const {
    const Row& row=rows_.at(id);
    if(field==1) return row.written;
    if(field==2) return row.reading;
    Require(field==13,"Invalid vocabulary string column");
    return row.alternate;
}

void RoomVocabulary::WriteNumber(unsigned id,unsigned field,std::optional<int> value) {
    Require(!pending_ && field!=1 && field!=2 && field!=13,"Invalid vocabulary numeric write");
    rows_.at(id).fields.at(field)=value?Integer(*value):0;
}

KscSaveValue RoomVocabulary::RelationGroupCounter() const {
    return {0xb0000000u,{},{{0x20000000u | next_relation_group_,{},{}}}};
}

void RoomVocabulary::RestoreRelationGroupCounter(const KscSaveValue& ref) {
    if (ref.word!=0xb0000000u || !ref.text.empty() || ref.children.size()!=1)
        throw std::invalid_argument("Missing dictionary relation group counter");
    const auto& value=ref.children.front();
    if (value.word>>28!=2 || !value.text.empty() || !value.children.empty())
        throw std::invalid_argument("Invalid dictionary relation group counter");
    next_relation_group_=value.word & 0x0fffffffu;
}

void RoomVocabulary::LinkWordGroup(unsigned source_row,unsigned target_row) {
    Require(!pending_, "Cannot link rows during a dictionary selection");
    auto& source=rows_.at(source_row).fields[10];
    auto& target=rows_.at(target_row).fields[10];
    if (!source) {
        source=0x20000000u | next_relation_group_;
        target=source;
        next_relation_group_=(next_relation_group_+1)&0x0fffffffu;
    } else {
        target=source;
    }
}

void RoomVocabulary::WriteText(unsigned id,unsigned field,const std::string& value) {
    auto& row=rows_.at(id);
    Require(!pending_ && (field==1 || field==2 || field==13),"Invalid vocabulary text write");
    (field==1?row.written:field==2?row.reading:row.alternate)=value;
    row.fields[field]=0x50000000; // live string tag, no serialized KSC address
}

void RoomVocabulary::InitializeReserved(unsigned id,std::string written,std::string reading) {
    auto& row=rows_.at(id);
    Require(row.fields[0]==Integer(1),"Word entry lost its reserved dictionary row");
    row.fields[4]=Integer(0);
    row.fields[5]=Integer(Number(CurrentMinute()));
    row.fields[6]=Integer(Number(CurrentMinute()));
    WriteText(id,1,written);
    WriteText(id,2,reading);
}

bool RoomVocabulary::AcceptReserved(unsigned id) {
    auto& row=rows_.at(id);
    if(row.fields[0]!=Integer(1)) return false;
    auto result=FinalizeLearnedWord(FieldInt(id,0),row.written,
        HasField(id,2)?std::optional<std::string_view>(row.reading):std::nullopt);
    if(!result) return false;
    WriteNumber(id,0,result->flags);
    WriteNumber(id,3,result->character_count);
    if(result->reading_index) WriteNumber(id,18,*result->reading_index);
    return true;
}

void RoomVocabulary::ChangeMemoReading(unsigned id,const std::string& reading) {
    // 2E738 replaces column 2, then event 28 (NEKO 8DC76) invokes 1527.
    Require(!reading.empty(),"Empty memo editor result has no dictionary event");
    WriteText(id,2,reading);
    WriteNumber(id,0,FieldInt(id,0)|0x2000);
    // 1527 counts the written characters before it calls 1488.
    WriteNumber(id,3,RefreshWordReading(0,FieldText(id,1),std::nullopt).character_count);
    RefreshReading(id);
}

void RoomVocabulary::RefreshReading(unsigned id) {
    if (!HasField(id,2)) return;
    const auto fields=RefreshWordReading(FieldInt(id,0),{},FieldText(id,2));
    WriteNumber(id,18,*fields.reading_index);
    WriteNumber(id,0,fields.flags);
}

int RoomVocabulary::FindWritten(const std::string& written) const {
    // 0F02 places the input in CT24CF's equality operand for column 1.
    // Builtin 60 writes the first matched row into a one-element result array.
    for(unsigned i=0;i<rows_.size();++i)
        if(HasField(i,1) && rows_[i].written==written) return int(i);
    return -1;
}

void RoomVocabulary::SetRelationship(unsigned id, int field12) {
    Require(!pending_, "Relationship write during dictionary selection");
    rows_.at(id).fields[12]=Integer(field12);
}

void RoomVocabulary::RecordTrait(unsigned id, unsigned index, bool set) {
    Require(!pending_ && index<22, "Invalid Toro trait choice");
    auto& fields=rows_.at(id).fields;
    const int bit=1 << index;
    fields[15]=Integer(Number(fields[15]) | bit);
    const int previous=Number(fields[14]);
    fields[14]=Integer(set ? (previous | bit) : (previous & ~bit));
}

int RoomVocabulary::ReserveWord() {
    Require(rows_.size()==700 && !pending_, "Dictionary unavailable for word reservation");
    int replace=-1;
    for (int i=162; i<600; ++i) {
        auto& f=rows_[i].fields;
        if (f[0]==0) {
            f[0]=Integer(1);
            f[4]=Integer(0);
            return i;
        }
        if ((Number(f[0]) & 0xb0) && f[11]==Integer(0) &&
            (replace<0 || Number(f[6])<Number(rows_[replace].fields[6]))) replace=i;
    }
    Require(replace>=0, "Toro word table has no available slot");
    ReleaseWord(replace);
    rows_[replace].fields[0]=Integer(1);
    rows_[replace].fields[4]=Integer(0);
    return replace;
}

void RoomVocabulary::ReleaseWord(int row) {
    Require(row>=162 && row<600 && !pending_, "Invalid reserved dictionary row");
    ClearMemoWord(row);
}

void RoomVocabulary::ClearMemoWord(unsigned row) {
    Require(row<600 && !pending_, "Invalid ordinary dictionary clear");
    rows_.at(row)={}; // 0DBC clears fields 18 through 0 to NIL.
}

std::optional<bool> RoomVocabulary::FirstContactSpecial() {
    // 10B3D(-1) -> AD47. A learned word requires 10A16. The alternate
    // DED7/A718 tests 0x200014, 0x200004, then 4 in G58DF[20].
    for (unsigned ct : {0x2af0u,0x2affu}) {
        if (Candidates(ct,0,false)) { pending_=false; return std::nullopt; }
        FinishLookup(0);
    }
    for (unsigned mask : {0x200014u,0x200004u,4u})
        for (const auto& row : questions_)
            if ((unsigned(Number(row[0])) & mask)==mask) return std::nullopt;
    return false;
}
} // namespace dokodemo::gameplay
