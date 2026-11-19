#include "room_word_conversion.h"
#include "room_word_reading.h"
#include <stdexcept>
#include <fstream>
#include <algorithm>

namespace dokodemo::gameplay {
namespace {
unsigned Unit(std::string_view text, std::size_t& at) {
    const unsigned lead=static_cast<unsigned char>(text[at++]);
    if (lead<0x80) return lead;
    if (at==text.size()) throw std::runtime_error("Incomplete room keyboard character");
    return lead*256u+static_cast<unsigned char>(text[at++]);
}

std::size_t Cut(std::string& text, unsigned count) {
    std::size_t at=0;
    unsigned n=0;
    while (n<count && at<text.size()) { Unit(text,at); ++n; }
    text.resize(at);
    return n;
}
int Word(std::istream& in) {
    const int a=in.get(), b=in.get();
    return a<0 || b<0 ? -1 : a+256*b;
}
bool ReadCandidate(std::istream& in,RoomNjCandidate& item) {
    const int l=Word(in),r=Word(in),nr=Word(in),nw=Word(in);
    if(l<0 || r<0 || nr<=0 || nw<=0 || nr>40 || nw>40) return false;
    item={std::string(nr,'\0'),std::string(nw,'\0'),unsigned(l),unsigned(r)};
    if(!in.read(item.reading.data(),nr) || !in.read(item.written.data(),nw)) return false;
    for(const auto* field:{&item.reading,&item.written}) {
        for(std::size_t j=0;j<field->size();) {
            const auto lead=static_cast<unsigned char>((*field)[j]);
            if(!lead || j+(lead<128?1:2)>field->size()) return false;
            j+=lead<128?1:2;
        }
    }
    return true;
}
bool LoadCandidates(std::istream& in,unsigned count,std::vector<RoomWordConversion::Candidate>& out) {
    for(unsigned i=0;i<count;++i) {
        RoomNjCandidate item;
        if (!ReadCandidate(in,item)) return false;
        out.push_back(std::move(item));
    }
    return true;
}
bool ReadFirstSlots(std::istream& in,std::string_view query,std::vector<RoomNjFirstSlot>& slots) {
    const int count=Word(in);
    if(count<0 || count>5) return false;
    for(int i=0;i<count;++i) {
        RoomNjFirstSlot slot;
        const int eligible=Word(in);
        if(eligible==1) {
            slot.grade=Word(in);
            RoomNjCandidate candidate;
            if(slot.grade<0 || !ReadCandidate(in,candidate) || candidate.reading!=query) return false;
            slot.candidate=std::move(candidate);
        } else if(eligible!=0) return false;
        slots.push_back(std::move(slot));
    }
    return true;
}
bool ReadQuery(std::istream& input,int size,std::string& query) {
    if(size<1 || size>16) return false;
    query.resize(size);
    if(!input.read(query.data(),size)) return false;
    unsigned units=0;
    for(std::size_t at=0; at<query.size(); ++units) {
        const unsigned lead=static_cast<unsigned char>(query[at++]);
        if(!lead) return false;
        if(lead>=128 && (at==query.size() || !query[at++])) return false;
    }
    return units<=8;
}

bool ReadSuffix(std::istream& input, const std::string& query, RoomNjSuffix& suffix, bool class_pairs) {
    const int count = Word(input);
    if (!count) return true;
    if (count < 0 || count > 511 || query.size() != 4) return false;
    for (int i = 0; i < count; ++i) {
        const int pos = Word(input);
        if (pos <= 0 || pos > 511) return false;
        const int right = class_pairs ? Word(input) : 0;
        if (class_pairs && (right <= 0 || right > 511)) return false;
        if (!suffix.resulting_class.emplace(pos,right).second) return false;
    }
    if (!class_pairs) {
        const int right = Word(input);
        if (right <= 0 || right > 511) return false;
        for (auto& rule : suffix.resulting_class) rule.second = static_cast<unsigned>(right);
    }
    return true;
}
}

bool RoomWordConversion::Load(const std::filesystem::path& dir,std::string& error) {
    std::ifstream in(dir/"room-word-keys.bin",std::ios::binary);
    char tag[4]{};
    in.read(tag,4);
    const int n=Word(in);
    std::set<unsigned> keys;
    for (int i=0;i<n;++i) {
        const int key=Word(in);
        if (key<0 || !keys.insert(unsigned(key)).second) break;
    }
    if(std::string_view(tag,4)!="NJW1" || n<=0 || keys.size()!=std::size_t(n) || in.peek()!=EOF) {
        error="Invalid room word conversion dictionary projection";
        return false;
    }
    std::ifstream file(dir/"room-word-dictionary.bin",std::ios::binary);
    file.read(tag,4);
    const int count=Word(file);
    decltype(dictionary_) dictionary;
    const bool first_slots = std::string_view(tag,4) == "NJQ4";
    const bool class_pairs = first_slots || std::string_view(tag,4) == "NJQ3";
    const bool with_suffixes = class_pairs || std::string_view(tag,4) == "NJQ2";
    if((!with_suffixes && std::string_view(tag,4)!="NJQ1") || count<=0) {
        error="Invalid room kana dictionary header";
        return false;
    }
    for(int i=0;i<count;++i) {
        const int size=Word(file),predictions=Word(file),forms=Word(file);
        std::string query;
        Group group;
        if(predictions<0 || forms<=0 || !ReadQuery(file,size,query) ||
           !LoadCandidates(file,predictions,group.predictions) || !LoadCandidates(file,forms,group.forms) ||
           (with_suffixes && !ReadSuffix(file,query,group.suffix,class_pairs)) ||
           (first_slots && !group.suffix.resulting_class.empty() &&
            (!ReadFirstSlots(file,query,group.suffix.whole_slots) ||
             !ReadFirstSlots(file,query,group.suffix.prefix_slots))) ||
           !dictionary.emplace(std::move(query),std::move(group)).second) {
            error="Invalid room kana dictionary group";
            return false;
        }
    }
    if(file.peek()!=EOF) { error="Trailing room kana dictionary data"; return false; }
    cold_keys_=std::move(keys);
    dictionary_=std::move(dictionary);
    history_=RoomWordHistory{};
    error.clear();
    return true;
}

std::optional<std::vector<RoomWordConversion::Candidate>> RoomWordConversion::Lookup(std::string_view s) const {
    if(s.empty()) return std::vector<Candidate>{};
    if(const auto it=dictionary_.find(s);it!=dictionary_.end())
        return history_.Compose(s,it->second.predictions,it->second.forms,it->second.suffix);
    std::size_t at=0;
    const auto first=Unit(s,at);
    if(at!=s.size()) return std::nullopt;
    if(!cold_keys_.count(first)) return std::nullopt;
    return std::vector<Candidate>{{std::string(s),std::string(s),185,31}};
}

bool RoomWordConversion::FinishWord(std::string_view written,std::string& reading) {
    return FinishWordReading(written,reading,[&](std::string_view r,std::string_view w){
        if(r.size()+w.size()>27) return false; // Multi-slot NJ bank writes.
        history_.Register(r,w); // 20260 ignores the NJ status, including duplicates.
        return true;
    });
}

std::optional<bool> RoomWordConversion::CommitCandidate(const Candidate& item,std::string& written,
        std::string& reading,std::string& composition,unsigned max_units) {
    const auto choices=Lookup(composition);
    if(!choices || std::none_of(choices->begin(),choices->end(),[&](const auto& row){
        return row.reading==item.reading && row.written==item.written &&
               row.left_pos==item.left_pos && row.right_pos==item.right_pos;
    })) return std::nullopt;
    // The source learns the selected record even if the joined spelling
    // later exceeds the text-field limit (20A20 precedes 20910).
    if(!history_.Learn(item)) return std::nullopt;
    reading+=item.reading;
    Cut(reading,16);
    reading=Reading(reading);
    Cut(reading,16);
    written+=item.written;
    composition.clear();
    const auto attempted=Cut(written,max_units+1);
    Cut(written,max_units);
    return attempted<=max_units;
}

std::string RoomWordConversion::Reading(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t at=0; at<text.size();) {
        unsigned value=Unit(text,at);
        if (value>='a' && value<='z') value-=0x20;
        else if (value>=0x8340 && value<=0x836e) value-=0xa1;
        else if (value>=0x8380 && value<=0x8393) value-=0xa2;
        else if (value==0x8394) value=0x82d4;
        else if (value==0x8395) value=0x82a9;
        else if (value==0x8396) value=0x82af;
        if (value>=256) out.push_back(static_cast<char>(value>>8));
        out.push_back(static_cast<char>(value&255));
    }
    return out;
}

bool RoomWordConversion::CommitRaw(std::string& written,std::string& reading,
                                   std::string& composition,unsigned max_units) {
    reading+=composition;
    Cut(reading,16);
    reading=Reading(reading);
    written+=composition;
    composition.clear();
    const auto attempted=Cut(written,max_units+1);
    Cut(written,max_units);
    return attempted<=max_units;
}
} // namespace dokodemo::gameplay
