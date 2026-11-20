#include "room_word_conversion.h"
#include "room_word_reading.h"
#include <stdexcept>
#include <fstream>
#include <algorithm>
#include <array>
#include <cstdint>

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
bool Hiragana(std::string_view text) {
    if(text.empty() || text.size()%2) return false;
    for(std::size_t at=0;at<text.size();) {
        const auto glyph=Unit(text,at);
        if(glyph<0x829f || glyph>0x82f1) return false;
    }
    return true;
}

std::string Katakana(std::string_view reading) {
    std::string written;
    written.reserve(reading.size());
    for(std::size_t at=0;at<reading.size();) {
        const auto glyph=Unit(reading,at);
        const auto mapped=glyph+(glyph<0x82de?0xa1:0xa2);
        written.push_back(static_cast<char>(mapped>>8));
        written.push_back(static_cast<char>(mapped&255));
    }
    return written;
}

std::uint32_t Dword(std::istream& input) {
    const int low=Word(input),high=Word(input);
    if(low<0 || high<0) return UINT32_MAX;
    return static_cast<std::uint32_t>(low)|(static_cast<std::uint32_t>(high)<<16);
}
}

bool RoomWordConversion::Lexicon::Allows(unsigned next,unsigned previous) const {
    if(!next || next>left_count || !previous || previous>right_count) return false;
    const auto stride=(left_count+7)/8;
    const auto index=(previous-1)*stride+(next-1)/8;
    return (connections[index] & (0x80u>>((next-1)%8)))!=0;
}

bool RoomWordConversion::LoadLexicon(const std::filesystem::path& path,Lexicon& lexicon) {
    std::error_code fs_error;
    if(!std::filesystem::exists(path,fs_error)) return !fs_error;
    std::ifstream in(path,std::ios::binary);
    char tag[4]{};
    in.read(tag,4);
    const int left=Word(in),right=Word(in),initial=Word(in),terminal=Word(in),
              raw_left=Word(in),raw_right=Word(in);
    const auto stems=Dword(in),predictions=Dword(in);
    if(std::string_view(tag,4)!="NJL1" || left<1 || left>511 || right<1 || right>511 ||
       initial<1 || initial>right || terminal<1 || terminal>left ||
       raw_left<1 || raw_left>left || raw_right<1 || raw_right>right ||
       !stems || stems>300000 || predictions>16384) return false;
    lexicon.left_count=unsigned(left); lexicon.right_count=unsigned(right);
    lexicon.initial_class=unsigned(initial); lexicon.terminal_class=unsigned(terminal);
    lexicon.raw_left=unsigned(raw_left); lexicon.raw_right=unsigned(raw_right);
    lexicon.connections.resize(((left+7)/8)*right);
    if(!in.read(reinterpret_cast<char*>(lexicon.connections.data()),lexicon.connections.size())) return false;
    for(std::uint32_t i=0;i<stems;++i) {
        const int grade=Word(in);
        Candidate form;
        if(grade<0 || grade>63 || !ReadCandidate(in,form) || form.reading.size()>16 ||
           !form.left_pos || form.left_pos>lexicon.left_count ||
           !form.right_pos || form.right_pos>lexicon.right_count) return false;
        auto& alternatives=lexicon.stems[form.reading];
        alternatives.push_back({std::move(form),unsigned(grade)});
    }
    return LoadCandidates(in,predictions,lexicon.predictions) && in.peek()==EOF && !in.bad();
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
    Lexicon lexicon;
    if(!LoadLexicon(dir/"room-word-lexicon.bin",lexicon)) {
        error="Invalid room kana lexicon";
        return false;
    }
    lexicon_=std::move(lexicon);
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
    if(auto reading=LookupKana(s)) return reading;
    if(at!=s.size()) return std::nullopt;
    if(!cold_keys_.count(first)) return std::nullopt;
    return std::vector<Candidate>{{std::string(s),std::string(s),185,31}};
}

std::optional<std::vector<RoomWordConversion::Candidate>> RoomWordConversion::LookupKana(
        std::string_view query) const {
    constexpr unsigned max_units=8;
    if(lexicon_.stems.empty() || query.size()>2*max_units || !Hiragana(query)) return std::nullopt;

    struct Path {
        std::string written;
        unsigned first_class=0, parts=0, grade=0, clause_breaks=0;
    };
    const auto better=[](const Path& a,const Path& b) {
        if(a.clause_breaks!=b.clause_breaks) return a.clause_breaks<b.clause_breaks;
        if(a.parts!=b.parts) return a.parts<b.parts;
        return a.grade>b.grade;
    };
    // Keep a bounded set for each position and outgoing class. Subsequent
    // connections depend on that class, so one busy class cannot crowd out
    // every path that could legally finish the reading.
    const auto keep_best=[&](std::vector<Path>& paths,Path candidate,std::size_t limit) {
        const auto same=std::find_if(paths.begin(),paths.end(),[&](const Path& path) {
            return path.written==candidate.written && path.first_class==candidate.first_class;
        });
        if(same!=paths.end()) {
            if(!better(candidate,*same)) return;
            paths.erase(same);
        }
        const auto where=std::upper_bound(paths.begin(),paths.end(),candidate,better);
        if(std::size_t(where-paths.begin())>=limit) return;
        paths.insert(where,std::move(candidate));
        if(paths.size()>limit) paths.pop_back();
    };
    const std::size_t count=query.size()/2;
    std::array<std::map<unsigned,std::vector<Path>>,max_units+1> ending_at;
    ending_at[0][lexicon_.initial_class].emplace_back();
    std::vector<Candidate> forms;
    for(std::size_t start=0;start<count;++start) {
        for(std::size_t end=start+1;end<=count;++end) {
            const auto group=lexicon_.stems.find(query.substr(start*2,(end-start)*2));
            if(group==lexicon_.stems.end()) continue;
            for(const auto& stem:group->second) {
                const auto& form=stem.form;
                if(end==count && !lexicon_.Allows(lexicon_.terminal_class,form.right_pos)) continue;
                for(const auto& [previous,paths]:ending_at[start]) {
                    const bool direct=lexicon_.Allows(form.left_pos,previous);
                    if(!direct && (!lexicon_.Allows(lexicon_.terminal_class,previous) ||
                                   !lexicon_.Allows(form.left_pos,lexicon_.initial_class))) continue;
                    for(const auto& path:paths) {
                        const auto length=path.written.size()+form.written.size();
                        // The learned bank has one 27-byte payload per entry
                        // in this implementation. Don't offer an inert choice.
                        if(length+query.size()>27) continue;
                        const unsigned first=start ? path.first_class : form.left_pos;
                        keep_best(ending_at[end][form.right_pos],
                                  {path.written+form.written,first,path.parts+1,path.grade+stem.grade,
                                   path.clause_breaks+unsigned(!direct)},16);
                    }
                }
            }
        }
    }
    struct RankedForm {
        Path path;
        unsigned last;
    };
    std::vector<RankedForm> final;
    for(auto& [last,paths]:ending_at[count])
        for(auto& path:paths) final.push_back({std::move(path),last});
    std::stable_sort(final.begin(),final.end(),[&](const RankedForm& a,const RankedForm& b) {
        return better(a.path,b.path);
    });
    for(auto& entry:final) {
        if(forms.size()==62) break;
        if(std::none_of(forms.begin(),forms.end(),[&](const Candidate& form) {
            return form.written==entry.path.written;
        })) forms.push_back({std::string(query),std::move(entry.path.written),entry.path.first_class,entry.last});
    }
    if(query.size()*2<=27) {
        forms.push_back({std::string(query),std::string(query),lexicon_.raw_left,lexicon_.raw_right});
        forms.push_back({std::string(query),Katakana(query),lexicon_.raw_left,lexicon_.raw_right});
    }

    std::vector<Candidate> predictions;
    for(const auto& row:lexicon_.predictions) {
        if(row.reading.compare(0,query.size(),query)==0) predictions.push_back(row);
    }
    auto choices=history_.Compose(query,predictions,forms);
    if(choices.size()>64) choices.resize(64);
    return choices;
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
