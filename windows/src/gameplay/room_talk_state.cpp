#include "room_talk_state.h"
#include "script_vm.h"
#include "room_friends.h"
#include "room_inventory.h"
#include "message_speed.h"
#include "room_learned_word.h"
#include "core_hash.h"
#include "recovered/bulk_array_native.h"
#include "recovered/calendar_native.h"
#include "recovered/game_primitives.h"
#include "recovered/rng_native.h"
#include "recovered/package_storage_native.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr unsigned circle=0x2000;
int ScriptInteger(unsigned bits) {
    return static_cast<int>((bits & 0x0fffffffu) ^ 0x08000000u)-0x08000000;
}
void Require(bool value,const char* error) { if (!value) throw std::runtime_error(error); }
int SignedClock(unsigned bits) {
    return bits<=0x7fffffffu ? int(bits) : int(std::int64_t(bits)-0x100000000ll);
}
unsigned ArithmeticShiftRight(int value,unsigned count) {
    const unsigned bits=unsigned(value);
    return value<0 ? ~(~bits>>count) : bits>>count;
}

// The even/odd BOOT compound assignments share the operation, but only the
// even form leaves the assigned value on the expression stack.
int IntegerCompound(unsigned op,int lhs,int rhs) {
    lhs=ScriptInteger(unsigned(lhs));
    rhs=ScriptInteger(unsigned(rhs));
    const unsigned left=unsigned(lhs),right=unsigned(rhs);
    switch(op & ~1u) {
    case 0x5a: return ScriptInteger(left+right);
    case 0x5c: return ScriptInteger(left-right);
    case 0x5e: return ScriptInteger(left*right);
    case 0x60:
        Require(rhs!=0,"KSC division by zero");
        return ScriptInteger(unsigned(lhs/rhs));
    case 0x62:
        Require(rhs!=0,"KSC remainder by zero");
        return ScriptInteger(unsigned(lhs%rhs));
    case 0x64: return ScriptInteger(left&right);
    case 0x66: return ScriptInteger(left|right);
    case 0x68: return ScriptInteger(left^right);
    case 0x6a: return ScriptInteger(ArithmeticShiftRight(lhs,right&31u));
    case 0x6c: return ScriptInteger(left<<(right&31u));
    }
    throw std::runtime_error("Unknown KSC compound operation");
}
// BOOT 7C2D8 and 7C360 share the hiragana normalization table.
std::string KanaEndpoint(std::string_view text,bool last) {
    unsigned selected=0;
    for(std::size_t i=0;i<text.size() && text[i]!=0;i+=2) {
        if(i+1>=text.size() || text[i+1]==0) return {};
        const auto lead=static_cast<unsigned char>(text[i]),trail=static_cast<unsigned char>(text[i+1]);
        if(lead==0x81 && trail==0x5b) continue; // Long vowel mark.
        if(lead!=0x82 || trail<0x9f || trail>0xf1) return {};
        unsigned normalized=trail;
        if(trail>=0xee && trail<=0xf0) {
            constexpr unsigned legacy_vowels[]={0xa2,0xa6,0xa8};
            normalized=legacy_vowels[trail-0xee];
        } else if((trail>=0x9f && trail<=0xa7 && (trail&1u)) ||
                  trail==0xe1 || trail==0xe3 || trail==0xe5 || trail==0xec) {
            ++normalized;
        } else if(trail==0xc1) { // Small tsu contributes no endpoint.
            continue;
        }
        selected=normalized;
        if(!last) break;
    }
    return selected ? std::string{static_cast<char>(0x82),static_cast<char>(selected)} : std::string{};
}

unsigned Word(const unsigned char* p,unsigned bytes) {
    unsigned v=0;
    for(unsigned i=0;i<bytes;++i) v|=unsigned(p[i])<<(8*i);
    return v;
}
recovered::Glyph GlyphAt(std::string_view s,std::size_t at) {
    recovered::Glyph glyph{};
    Require(at<s.size() && recovered::read_glyph(reinterpret_cast<const unsigned char*>(s.data()+at),
                    s.size()-at,glyph),"Invalid CP932 break group");
    return glyph;
}
unsigned TextWidth(std::string_view line,const recovered::PackageView& font) {
    unsigned width=0;
    for(std::size_t pos=0;pos<line.size();) {
        const auto glyph=GlyphAt(line,pos);
        if(glyph.index!=0x16) {
            Require(glyph.index>=32 && glyph.index<8224,"Invalid conversation glyph");
            width+=font.ReadU8(0x3c+glyph.index-32)+2;
        }
        pos+=glyph.consumed;
    }
    return width?width-2:0;
}
}

namespace {
constexpr std::array<unsigned,3> contact_globals{0x59a8,0x59af,0x59b6};
constexpr std::array<unsigned,3> contact_sizes{5,6,6};
struct ActorContactScript {
    unsigned entry, roster, history, slot, mood_tick, attention, clear_text;
    unsigned lesson_fields, display_name, keyboard, initialize_word, accept_again, enqueue_topic, flags;
};
constexpr ActorContactScript contact_scripts[]{
    {0x7d43a,0xd090,0x6018,0x2878,0x2872,0x8d3e2,0x8d8fe,0x2446,0x497a,0x4929,0x87a2,0x18a5,0x249f,0x59a2},
    {0x5bdb8,0xbab1,0x6020,0x292c,0x2926,0x6b2a7,0x6b7bf,0x249f,0x497c,0x492b,0x87ed,0x18a5,0x24a0,0x59aa},
    {0x6206e,0x989d,0x6021,0x258c,0x2586,0x67188,0x676a2,0x2137,0x49c7,0x4976,0x87ed,0x18ba,0x24b5,0x59ab},
    {0x654e7,0x92b1,0x6031,0x2472,0x246c,0x6b35f,0x6b877,0x2019,0x48d8,0x4887,0x878c,0x18ba,0x24b5,0x59bb},
    {0x66058,0x9723,0x601c,0x2508,0x2502,0x6b170,0x6b68a,0x20bb,0x489a,0x4849,0x8715,0x18ba,0x24b5,0x59a6},
};

// Each actor's startup enables one destination kind. Local media preparation
// checks these values against the original KSC startup calls.
constexpr unsigned named_starting_outings[]{19,3,16,11};
static_assert(std::size(named_starting_outings)+1==std::size(contact_scripts));

}

unsigned RoomTalkState::ContactGlobal(unsigned bank) const {
    const auto flags=alternate_greeting_?alternate_greeting_->flags:robo_greeting_?0x59aau:0x59a2u;
    return contact_globals.at(bank)+flags-0x59a2u;
}

std::array<KscSaveValue,3> RoomTalkState::ContactHistoryTables() const {
    std::array<KscSaveValue,3> tables;
    for (unsigned bank=0; bank<tables.size(); ++bank) {
        const auto& live=global_arrays_.at(ContactGlobal(bank));
        Require(live.size()==contact_sizes[bank],"Wrong live contact table size");
        KscSaveValue array{0x90000000u | contact_sizes[bank]};
        for (const auto& cell : live) {
            KscSaveValue value;
            if (cell.kind!=Kind::nil) {
                if (bank==0) {
                    Require(cell.kind==Kind::string,"Invalid live contact note");
                    value.word=0x60000000u;
                    value.text=cell.text;
                } else {
                    Require(cell.kind==Kind::integer,"Invalid live contact flag word");
                    value.word=0x20000000u | (static_cast<unsigned>(cell.data) & 0x0fffffffu);
                }
            }
            array.children.push_back(std::move(value));
        }
        tables[bank]={0xb0000000u,{},{std::move(array)}};
    }
    return tables;
}

std::array<KscSaveValue,2> RoomTalkState::ProgressFlagWords() const {
    return {KscSaveValue{0x20000000u | (intro_flags_ & 0x0fffffffu)},
            KscSaveValue{0x20000000u | (outing_enabled_ & 0x0fffffffu)}};
}

void RoomTalkState::RestoreContactHistory(const std::array<KscSaveValue,3>& tables,
                                         const std::array<KscSaveValue,2>& progress) {
    for (const auto& word : progress)
        Require(word.word>>28==2 && word.children.empty() && word.text.empty(),
                "Invalid saved room progress word");
    std::array<std::vector<Value>,3> staged;
    for (unsigned bank=0; bank<tables.size(); ++bank) {
        const auto& ref=tables[bank];
        Require(global_arrays_.at(ContactGlobal(bank)).size()==contact_sizes[bank],
                "Contact table owner not initialized");
        Require(ref.word==0xb0000000u && ref.text.empty() && ref.children.size()==1,
                "Invalid saved contact table reference");
        const auto& array=ref.children.front();
        Require(array.word==(0x90000000u | contact_sizes[bank]) && array.text.empty() &&
                array.children.size()==contact_sizes[bank],"Wrong saved contact table shape");
        for (const auto& cell : array.children) {
            Require(cell.children.empty(),"Unexpected object in saved contact table");
            Value value;
            if (bank==0 && (cell.word>>28==5 || cell.word>>28==6)) {
                Require(cell.text.find('\0')==std::string::npos,"Embedded NUL in saved contact note");
                value.kind=Kind::string;
                value.text=cell.text;
            } else {
                Require(cell.text.empty(),"Unexpected saved contact text");
                if (cell.word!=0) {
                    Require(bank!=0 && cell.word>>28==2,"Invalid saved contact flag or note");
                    value.kind=Kind::integer;
                    value.data=ScriptInteger(cell.word);
                }
            }
            staged[bank].push_back(std::move(value));
        }
    }
    for (unsigned bank=0; bank<tables.size(); ++bank)
        staged[bank].swap(global_arrays_.at(ContactGlobal(bank)));
    intro_flags_=progress[0].word & 0x0fffffffu;
    outing_enabled_=progress[1].word & 0x0fffffffu;
}

void RoomTalkState::LoadScriptLocals(const std::filesystem::path& dir) {
    std::ifstream file(dir/"room-ksc-locals.bin",std::ios::binary|std::ios::ate);
    if(!file) return; // Earlier media can still run paths that never use these cells.
    const auto size=file.tellg();
    Require(size>=4 && size<=65536,"Invalid KSC local table size");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    Require(bool(file.seekg(0).read(reinterpret_cast<char*>(bytes.data()),size)) &&
            std::string_view(reinterpret_cast<const char*>(bytes.data()),4)=="RLS1",
            "Invalid KSC local table format");
    std::size_t at=4;
    const auto read=[&](unsigned count) {
        Require(count<=bytes.size()-at,"Truncated KSC local table");
        const unsigned value=Word(bytes.data()+at,count);
        at+=count;
        return value;
    };
    for(unsigned actor=0;actor<5;++actor) {
        const unsigned count=read(2);
        std::vector<Value> table;
        for(unsigned i=0;i<count;++i) {
            const unsigned tag=read(1),length=read(2);
            if(tag==5) {
                Require(length<=bytes.size()-at,"Truncated KSC local string");
                table.push_back({Kind::string,0,
                    std::string(reinterpret_cast<const char*>(bytes.data()+at),length)});
                at+=length;
            } else if(tag==9) {
                Require(length<=0xfff && i+length<count,"Invalid KSC local array range");
                table.push_back({Kind::script_local_array,int(length),{},i});
            } else {
                Require(!tag && !length,"Unsupported KSC local cell");
                table.push_back({});
            }
        }
        if(actor==actor_number_) script_locals_=std::move(table);
    }
    Require(at==bytes.size(),"Unexpected KSC local table data");
}

bool RoomTalkState::Load(const std::filesystem::path& dir,const AcceptedProfile& p,std::string& error) {
    *this={};
    try {
        Require(p.actor<std::size(contact_scripts),"Unknown actor contact layout");
        actor_number_=p.actor;
        LoadScriptLocals(dir);
        const auto& adopted=p.adopted_at;
        if (adopted[6] && adopted[5] && adopted[4])
            adoption_minute_=recovered::CalendarScalar7C660(recovered::CalendarRecord689B8(
                {int(adopted[6]),int(adopted[5]),int(adopted[4]),int(adopted[3]),int(adopted[2])}));
        if(p.actor>=2) {
            static constexpr GreetingScript scripts[]{
                {"usagi-introduction.bin",0x63396,0x6211e,0x59ab,0x3360,0x63372,0x4fb6,0x1c71,0x2587,0xa455,0x633df,0x3dd3,
                 {0x96a0,0x636c3,0x63839,0x257b,0x2130,0x3b91,0x59ae,
                  {0x63544,0x6342a,0x63585,0x634cb,0x6340b},0x67779,0x67128,0x67270,0x9fb3,0x59d5}},
                {"inu-introduction.bin",0x675a6,0x65588,0x59bb,0x3354,0x67582,0x4fc6,0x1c71,0x246d,0xb150,0x675ef,0x3dd3,
                 {0x972d,0x678d3,0x67a49,0x2461,0x2012,0x3af2,0x59be,
                  {0x67754,0x6763a,0x67795,0x676db,0x6761b},0x6b94e,0x6b2ff,0x6b445,0xacae,0x59e5}},
                {"kaeru-introduction.bin",0x6739f,0x660ff,0x59a6,0x322a,0x6737b,0x4fb1,0x1c71,0x2503,0xae21,0x673e8,0x3dd6,
                 {0x94f1,0x676cc,0x67842,0x24f7,0x20b4,0x3d09,0x59a9,
                  {0x6754d,0x67433,0x6758e,0x674d4,0x67414},0x6b761,0x6b110,0x6b258,0xa97f,0x59d0}},
            };
            Require(p.actor-2<std::size(scripts),"Unsupported introduction actor");
            alternate_greeting_=&scripts[p.actor-2];
        }
        if(p.actor>=1) {
            outing_enabled_=1u<<named_starting_outings[p.actor-1];
            if(!LoadNamedGreeting(dir,p,error)) return false;
            LoadNamedOutings(dir);
            LoadReturnLetter(dir);
            return true;
        }
        std::ifstream file(dir/"toro-introduction.bin",std::ios::binary|std::ios::ate);
        Require(file && file.tellg()==std::streamoff(1743),"Missing NEKO introduction");
        std::vector<unsigned char> image(1743);
        Require(bool(file.seekg(0).read(reinterpret_cast<char*>(image.data()),image.size())) &&
            core::sha256_hex(image.data(),image.size())=="3b92c05af8a7ddbcf95bceea5537412269f00b75280a6cfff23250826b90415c",
            "Wrong NEKO introduction");
        auto* bytes=image.data();
        const unsigned count=Word(bytes+4,2), suffix=Word(bytes+6,2);
        const unsigned fallback=Word(bytes+8,2);
        suffix_.assign(reinterpret_cast<const char*>(bytes+14),suffix);
        farewell_word_.assign(reinterpret_cast<const char*>(bytes+14+suffix),fallback);
        std::size_t pos=14+suffix+fallback;
        for(unsigned i=0;i<2;++i) {
            const unsigned length=Word(bytes+10+i*2,2);
            std::string_view set(reinterpret_cast<const char*>(bytes+pos),length);
            for(std::size_t j=0;j<set.size();) {
                const auto glyph=GlyphAt(set,j);
                break_classes_[i].push_back(glyph.index);
                j+=glyph.consumed;
            }
            pos+=length;
        }
        for(unsigned i=0;i<count;++i) {
            const unsigned entry=Word(bytes+pos,4), size=Word(bytes+pos+4,4);
            pos+=8;
            code_[entry].assign(bytes+pos,bytes+pos+size);
            pos+=size;
        }
        std::ifstream topics(dir/"toro-topics.bin",std::ios::binary|std::ios::ate);
        const auto topic_bytes=topics.tellg();
        Require(topics && topic_bytes>=std::streamoff(16) && topic_bytes<=std::streamoff(1u<<24),
                "Missing or oversized NEKO topic package");
        image.resize(static_cast<std::size_t>(topic_bytes));
        Require(bool(topics.seekg(0).read(reinterpret_cast<char*>(image.data()),image.size())),
                "Incomplete topic package");
        bytes=image.data();
        const auto format=Word(bytes,4);
        Require(format==0x32505452 || format==0x33505452 || format==0x34505452 || format==0x35505452,
                "Invalid topic package");
        const unsigned sections=(format >> 24)-'2'+3;
        pos=4+4*sections;
        Require(pos<=image.size(),"Incomplete topic header");
        for(unsigned section=0;section<sections;++section) {
            unsigned previous_end=0;
            for(unsigned i=0;i<Word(bytes+4+4*section,4);++i) {
                Require(image.size()-pos>=8,"Incomplete topic index");
                const unsigned at=Word(bytes+pos,4),size=Word(bytes+pos+4,4);
                pos+=8;
                Require(at<=0xffffff && size<=0x1000000-at && size<=image.size()-pos &&
                        at>=previous_end,"Invalid topic range");
                if(section==0) {
                    Require(size && bytes[pos]==0x30,"Invalid KSC function entry");
                    const auto previous=code_.find(at);
                    if(previous!=code_.end()) {
                        Require(previous->second.size()==size && std::equal(previous->second.begin(),
                            previous->second.end(),bytes+pos),"Conflicting projected topic function");
                        previous_end=at+size; pos+=size; continue;
                    }
                    const auto next=code_.lower_bound(at);
                    Require((next==code_.end() || at+size<=next->first) &&
                        (next==code_.begin() || std::prev(next)->first+std::prev(next)->second.size()<=at),
                        "Overlapping topic code");
                    code_[at].assign(bytes+pos,bytes+pos+size);
                } else if(section==2) {
                    Require(size%12==0,"Invalid word filter triples");
                    auto& predicate=predicates_[at];
                    for(unsigned j=0;j<size;j+=4) predicate.push_back(Word(bytes+pos+j,4));
                } else if(section==3) {
                    Require(size>=8 && size%4==0,"Invalid phrase list size");
                    const unsigned count=size/4-1;
                    Require(count<=0xfff && Word(bytes+pos,4)==(0x90000000u|count),
                            "Invalid phrase list header");
                    auto& list=phrase_lists_[at];
                    for(unsigned j=1;j<=count;++j) {
                        const unsigned entry=Word(bytes+pos+4*j,4), offset=entry&0x0fffffff;
                        Require(entry>>28==5 && pool_.count(offset),"Phrase has no script-pool string");
                        list.push_back(offset);
                    }
                } else if(section==4) {
                    if(at==0x98a2) {
                        Require((size==12 || size==20 || size==28 || size==36 || size==44) && Word(bytes+pos,4)==(0x90000000u|(size/4-1)),
                                "Invalid scheduled room event projection");
                        auto& events=global_arrays_[at];
                        constexpr std::array<unsigned,5> projected_events{2,3,6,4,5};
                        const auto count=(size-4)/8;
                        events.resize(*std::max_element(projected_events.begin(),projected_events.begin()+count)+1);
                        for(unsigned index=0;index<count;++index) {
                            const auto i=projected_events[index];
                            const auto start=pos+4+index*8;
                            const auto motion=Word(bytes+start,4), callback=Word(bytes+start+4,4);
                            Require(motion>>28==2 && (motion&0x0fffffff)>=20 && callback>>28==7 &&
                                    code_.count(callback&0x0fffffff),"Invalid scheduled room event row");
                            auto row=std::make_shared<std::vector<Value>>();
                            row->push_back({Kind::integer,int(motion&0x0fffffff)});
                            row->push_back({Kind::code,int(callback&0x0fffffff)});
                            events[i]={Kind::array,0,{},0,std::move(row)};
                        }
                        previous_end=at+1; pos+=size; continue;
                    }
                    const bool labels=at==0x6092 || at==0x6097 || at==0x9a26;
                    const unsigned cells=at==0x9a26?3:at==0x6092?4:at==0x59a8?5:at==0x6097?7:6;
                    Require((labels || at==0x59a8 || at==0x59af || at==0x59b6) &&
                            size==4*(cells+1) && Word(bytes+pos,4)==(0x90000000u|cells),
                            "Invalid room script global array");
                    auto& array=global_arrays_[at];
                    array.resize(cells);
                    for(unsigned j=0;j<cells;++j) {
                        const unsigned cell=Word(bytes+pos+4*(j+1),4);
                        if(labels) {
                            const unsigned offset=cell&0x0fffffff;
                            Require(cell>>28==5 && pool_.count(offset),"Missing table label in script pool");
                            array[j]={Kind::string,0,pool_.at(offset)};
                        } else Require(cell==0,"Invalid initial room script global cell");
                    }
                } else if(section==5) {
                    Require(size>=8 && size%4==0,"Invalid procedure list size");
                    const unsigned count=size/4-1;
                    Require(count<=0xfff && Word(bytes+pos,4)==(0x90000000u|count),
                            "Invalid procedure list header");
                    auto& list=procedure_lists_[at];
                    for(unsigned j=1;j<=count;++j) {
                        const unsigned entry=Word(bytes+pos+4*j,4),target=entry&0x0fffffff;
                        Require(entry>>28==7 && code_.count(target),"Missing script procedure in call list");
                        list.push_back(target);
                    }
                } else {
                    Require(!pool_.count(at),"Duplicate topic string offset");
                    pool_[at].assign(reinterpret_cast<const char*>(bytes+pos),size);
                }
                // Pool offsets may name a suffix of another CP932 string.
                previous_end=section==0?at+size:at+1;
                pos+=size;
            }
        }
        Require(pos==image.size(),"Trailing topic package data");
        Require(sections<5 || (global_arrays_.count(0x59a8) && global_arrays_.count(0x59af) &&
                global_arrays_.count(0x59b6)),"Missing room script global array");
        Require(!code_.count(0x2262) || global_arrays_.count(0x6092),"Missing season name array");
        for(unsigned entry:{0x123du,0x29b7u,0x4e27u,0x7d43au,0x846abu,0x848f9u,0x84b63u,
                            0x84d7au,0x84e93u,0x84f41u,0x8506du,0x8528bu,0x89accu,0x8d3e2u})
            Require(code_.count(entry)!=0,"Missing required NEKO topic");
        pool_[0xa207]=farewell_word_;
        nickname_=p.nickname.text(); player_=p.player.text();
        if(!vocabulary_.Load(dir,p,error) || !fallback_list_.Load(dir,error) || !traits_.Load(dir,error)) return false;
        global_arrays_[0x243f].resize(6);
        global_arrays_[0x243f][0]={Kind::integer,0}; // 8E166, initial room selector.
        if(code_.count(0x13219)) LoadOutings(dir);
        LoadReturnLetter(dir);
        phase_=Phase::prompt;
        return true;
    } catch(const std::exception& e) { error=e.what(); return false; }
}

void RoomTalkState::LoadReturnLetter(const std::filesystem::path& dir) {
    std::ifstream file(dir/"room-return-letters.bin",std::ios::binary|std::ios::ate);
    if(!file) return; // Existing media can still run stays without event 7.
    const auto size=file.tellg();
    Require(size>=24 && size<=32768,"Invalid return-letter resource size");
    std::vector<unsigned char> image(static_cast<std::size_t>(size));
    Require(bool(file.seekg(0).read(reinterpret_cast<char*>(image.data()),size)) &&
            std::string_view(reinterpret_cast<const char*>(image.data()),4)=="RLT1",
            "Invalid return-letter resource");
    const auto* at=image.data()+4;
    const auto* limit=image.data()+image.size();
    const auto take=[&](std::size_t count) {
        Require(count<=std::size_t(limit-at),"Truncated return-letter data");
        const auto* value=at;
        at+=count;
        return value;
    };
    const auto read=[&] { return Word(take(4),4); };
    for(unsigned index=0;index<5;++index) {
        const auto length=read();
        const auto* bytes=take(length);
        if(index!=actor_number_) continue;
        const auto* next=at;
        const auto* image_end=limit;
        at=bytes;
        limit=next;
        const auto table=read(),motion=read(),callback=read(),selector=read(),blocks=read(),strings=read();
        const auto expected=alternate_greeting_?alternate_greeting_->schedule.table:robo_greeting_?0x940bu:0x98a2u;
        Require(table==expected && motion==79 && blocks>=2 && blocks<=3,"Invalid letter event table");
        for(unsigned i=0;i<blocks;++i) {
            const auto pc=read(),count=read();
            const auto* body=take(count);
            Require(count>=3 && pc<0x1000000 && count<=0x1000000-pc && body[0]==0x30 && body[count-1]==0x31,
                    "Invalid letter procedure");
            const auto found=code_.lower_bound(pc);
            if(found!=code_.end() && found->first==pc) {
                Require(found->second.size()==count && std::equal(found->second.begin(),found->second.end(),body),
                        "Conflicting letter procedure");
            } else {
                Require((found==code_.end() || pc+count<=found->first) &&
                        (found==code_.begin() || std::prev(found)->first+std::prev(found)->second.size()<=pc),
                        "Overlapping letter procedure");
                code_.emplace_hint(found,pc,std::vector<unsigned char>(body,body+count));
            }
        }
        for(unsigned i=0;i<strings;++i) {
            const auto offset=read(), count=read();
            const auto* raw=take(count);
            std::string value(reinterpret_cast<const char*>(raw),count);
            const auto [existing, inserted]=pool_.try_emplace(offset,value);
            Require(value.find('\0')==value.npos && (inserted || existing->second==value),
                    "Conflicting letter text");
        }
        LetterScript script{callback,selector,{}};
        for(auto& descriptor:script.descriptors) {
            descriptor.id=read();
            const auto count=read();
            const auto* raw=take(count);
            descriptor.text.assign(reinterpret_cast<const char*>(raw),count);
            Require(descriptor.id<138 && count>0 && descriptor.text.find('\0')==descriptor.text.npos,
                    "Invalid letter diary descriptor");
        }
        const auto& body=code_.at(callback);
        Require(at==limit && body.size()>0x15 && body[0x10]==0x20 && body[0x11]==1 &&
                Word(body.data()+0x12,3)==selector,"Incomplete return-letter entry");
        auto& rows=global_arrays_[table];
        rows.resize(8);
        rows[7]={Kind::array,0,{},0,std::make_shared<std::vector<Value>>(
            std::initializer_list<Value>{{Kind::integer,int(motion)},{Kind::code,int(callback)}})};
        letter_script_=std::move(script);
        limit=image_end;
    }
    Require(at==limit,"Unexpected return-letter resource tail");
}

bool RoomTalkState::LoadNamedGreeting(const std::filesystem::path& dir,const AcceptedProfile& profile,
                                      std::string& error) {
    std::ifstream file(dir/(alternate_greeting_?alternate_greeting_->filename:"robo-introduction.bin"),
                       std::ios::binary|std::ios::ate);
    const auto size=file.tellg();
    Require(file && size>=16 && size<=(1<<20),"Missing named actor room script");
    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    Require(bool(file.seekg(0).read(reinterpret_cast<char*>(data.data()),data.size())),
            "Truncated ROBO introduction");
    const auto format=Word(data.data(),4);
    Require(alternate_greeting_ ? format==0x34494152u || format==0x35494152u :
            format>=0x31495252 && format<=0x36495252 && (format&0xffffff)==0x495252,
            "Invalid actor introduction");
    std::size_t at=4;
    auto take=[&](std::size_t length) {
        Require(length<=data.size()-at,"Truncated ROBO introduction");
        const auto* begin=data.data()+at;
        at+=length;
        return begin;
    };
    auto number=[&](unsigned length) { return Word(take(length),length); };
    const auto blocks=number(2), strings=number(2), phrases=number(2), suffix=number(2);
    const std::array<unsigned,2> breaks{number(2),number(2)};
    suffix_.assign(reinterpret_cast<const char*>(take(suffix)),suffix);
    for(unsigned i=0;i<2;++i) {
        const std::string_view text(reinterpret_cast<const char*>(take(breaks[i])),breaks[i]);
        for(std::size_t k=0;k<text.size();) {
            const auto glyph=GlyphAt(text,k);
            break_classes_[i].push_back(glyph.index);
            k+=glyph.consumed;
        }
    }
    unsigned previous_end=0;
    for(unsigned i=0;i<blocks;++i) {
        const auto pc=number(4), length=number(4);
        const auto* body=take(length);
        const bool trait_loop=pc==0xb5fdu && length>=4 && Word(body+length-4,4)==0x00b65710;
        Require(pc>=previous_end && pc<=0xffffff && length<=0x1000000-pc && length>=3 && body[0]==0x30 &&
                (body[length-1]==0x31 || trait_loop || (pc==0x6b88b && body[length-1]==0x1b) ||
                 (alternate_greeting_ && pc+11==alternate_greeting_->schedule.farewell && body[length-1]==0x1b)),
                "Invalid ROBO procedure");
        code_[pc].assign(body,body+length);
        previous_end=pc+length;
    }
    for(unsigned i=0;i<strings;++i) {
        const auto offset=number(4), length=number(4);
        const auto* bytes=take(length);
        Require(pool_.emplace(offset,std::string(reinterpret_cast<const char*>(bytes),length)).second,
                "Duplicate ROBO script string");
    }
    for(unsigned i=0;i<phrases;++i) {
        const auto id=number(2), count=number(2);
        Require(count && count<=16 && !phrase_lists_.count(id),"Invalid ROBO phrase list");
        for(unsigned j=0;j<count;++j) {
            const auto offset=number(4);
            Require(pool_.count(offset)!=0,"Missing ROBO phrase text");
            phrase_lists_[id].push_back(offset);
        }
    }
    if(alternate_greeting_) {
        const auto count=number(2);
        Require(count>0 && count<=4,"Invalid alternate speech suffix count");
        for(unsigned i=0;i<count;++i) {
            const auto index=number(2), offset=number(4);
            Require(index==0x18+i && pool_.count(offset),"Missing alternate actor speech suffix");
            speech_suffixes_.emplace(index,offset);
        }
        const auto& source=alternate_greeting_->schedule;
        auto& rows=global_arrays_[source.table];
        rows.resize(8);
        constexpr std::array motions{79u,48u,79u,48u,85u};
        for(unsigned i=0;i<motions.size();++i) {
            const auto motion=number(4), callback=number(4);
            Require(motion==motions[i] && callback==source.callbacks[i] && code_.count(callback),
                    "Invalid named actor scheduled event row");
            auto row=std::make_shared<std::vector<Value>>();
            row->push_back({Kind::integer,int(motion)});
            row->push_back({Kind::code,int(callback)});
            rows[2+i]={Kind::array,0,{},0,std::move(row)};
        }
        Require(code_.count(source.wrapper),"Missing named actor scheduled wrapper");
        for(auto& offset:named_scheduled_diary_offsets_) {
            offset=number(4);
            Require(pool_.count(offset),"Missing named actor scheduled diary text");
        }
        if(format==0x34494152u) {
            const auto season_count=number(2);
            Require(season_count==(profile.actor==2?4u:0u),"Invalid named actor season table");
            for(unsigned i=0;i<season_count;++i)
                global_arrays_[0x609b].push_back({Kind::string,0,pool_.at(number(4))});
        }
        auto& location=global_arrays_[source.location_cell];
        location.resize(6);
        location[0]={Kind::integer,0}; // Actor bootstrap sets the cold room selector.
        outing_script_cells_[alternate_greeting_->skip_farewell-2]={Kind::integer,0};
    } else if(format!=0x31495252) {
        auto& rows=global_arrays_[0x940b];
        rows.resize(8);
        const unsigned count=format>=0x35495252?5:(format>>24)-'1';
        constexpr std::array events{2u,3u,6u,4u,5u};
        constexpr std::array motions{79u,48u,85u,79u,48u};
        constexpr std::array callbacks{0x6761bu,0x67501u,0x674e2u,0x6765cu,0x675a2u};
        for(unsigned i=0;i<count;++i) {
            const unsigned motion=number(4), callback=number(4);
            Require(motion==motions[i] && callback==callbacks[i] &&
                    code_.count(callback) && code_.count(0x6779a),"Invalid ROBO scheduled event row");
            auto row=std::make_shared<std::vector<Value>>();
            row->push_back({Kind::integer,int(motion)});
            row->push_back({Kind::code,int(callback)});
            rows[events[i]]={Kind::array,0,{},0,std::move(row)};
        }
        auto& reactions=procedure_lists_[0x2a98];
        for(unsigned i=0;i<3;++i) {
            const auto pc=number(4);
            Require(code_.count(pc)!=0,"Missing ROBO event reaction");
            reactions.push_back(pc);
        }
    }
    if(format==0x35494152u || format==0x36495252u) {
        nil_inline_text_.assign(reinterpret_cast<const char*>(take(4)),4);
        Require(ScriptCharacterCount(nil_inline_text_)==2,"Invalid inline text fallback");
        constexpr std::array<std::array<unsigned,2>,4> ids{{
            {0x609a,0x609f},{0x609b,0x60a0},{0x60ab,0},{0x6096,0}
        }};
        const auto expected=ids.at(profile.actor-1);
        const auto table_count=number(2);
        Require(table_count==(expected[1]?2u:1u),"Invalid actor calendar table count");
        for(unsigned i=0;i<table_count;++i) {
            const auto index=number(2), length=number(2);
            Require(index==expected[i] && length==(i==0?4u:7u),"Invalid actor calendar labels");
            auto [table, inserted]=global_arrays_.try_emplace(index);
            Require(inserted,"Duplicate actor calendar table");
            for(unsigned j=0;j<length;++j)
                table->second.push_back({Kind::string,0,pool_.at(number(4))});
        }
    }
    Require(at==data.size() && code_.count(alternate_greeting_?alternate_greeting_->entry:0x6746d) &&
            code_.count(alternate_greeting_?alternate_greeting_->farewell:0x5be76),"Incomplete actor introduction");
    if(!vocabulary_.Load(dir,profile,error) || !fallback_list_.Load(dir,error,profile.actor)) return false;
    nickname_=profile.nickname.text();
    player_=profile.player.text();
    phase_=Phase::prompt;
    robo_greeting_=!alternate_greeting_;
    for (unsigned bank=0;bank<contact_globals.size();++bank)
        global_arrays_[ContactGlobal(bank)].resize(contact_sizes[bank]);
    if(alternate_greeting_) return true;
    if(!traits_.Load(dir,error)) return false;
    for (const unsigned id : {0x2a0au,0x2a3bu,0x2a42u,0x2a49u,0x2a50u,0x2a57u,0x2a5eu,0x2a65u,0x2a6cu,0x2a73u}) {
        if (!vocabulary_.HasPredicate(id)) continue;
        const auto cells=vocabulary_.PredicateGroup(id,0);
        predicates_[id].assign(cells.begin(),cells.end());
    }
    // 6C022..6C028 writes the home kind to the initially empty descriptor.
    global_arrays_[0x2498].resize(6);
    global_arrays_[0x2498][0]={Kind::integer,0};
    return true;
}

bool RoomTalkState::supports_ordinary_topic(unsigned selected_pc) const {
    if(robo_greeting_)
        return (selected_pc==0x5bdb8 || selected_pc==0x5c7dc || selected_pc==0x5cc52) && code_.count(selected_pc);
    return (selected_pc==0x4e27 || selected_pc==0x89acc || selected_pc==0x7d43a ||
            selected_pc==0x810a0 || selected_pc==0x8121c || selected_pc==0x82bfa || selected_pc==0x8304e ||
            (selected_pc>=0x846ab && selected_pc<=0x85439)) && code_.count(selected_pc);
}

bool RoomTalkState::BeginOrdinaryTopic(unsigned selected_pc,bool queued) {
    if(phase_!=Phase::done || ordinary_ ||
       (queued?!code_.count(selected_pc):!supports_ordinary_topic(selected_pc))) return false;
    queued_root_=queued;
    if(queued) {
        // 2573 copies the selected queue slot into CT2878 before the call at 25A5.
        selected_fallback_={Kind::code,int(selected_pc)};
        indirect_topic_=selected_pc;
    }
    StartTopic(selected_pc);
    if(robo_greeting_) {
        columns_=9;
        rows_=4;
    }
    return true;
}

bool RoomTalkState::BeginRoboContactGreeting() {
    if(!robo_greeting_ || phase_!=Phase::done || ordinary_ || !code_.count(0x5bdc3)) return false;
    StartTopic(0x5bdc3);
    robo_contact_greeting_=true;
    columns_=9;
    rows_=4;
    return true;
}

bool RoomTalkState::TakeRoboContactGreetingReturn() {
    return std::exchange(robo_contact_greeting_return_,false);
}

bool RoomTalkState::BeginScheduledRoomEvent(unsigned event, std::uint32_t midnight, ScheduledProgress progress) {
    const unsigned entry=alternate_greeting_?alternate_greeting_->schedule.wrapper:robo_greeting_?0x6779au:0x89883u;
    const auto rows=global_arrays_.find(alternate_greeting_?alternate_greeting_->schedule.table:robo_greeting_?0x940bu:0x98a2u);
    if(event<2 || event>7 || phase_!=Phase::done || ordinary_ || !code_.count(entry) ||
       rows==global_arrays_.end() || rows->second.size()<=event || rows->second[event].kind!=Kind::array) return false;
    scheduled_room_event_=event;
    scheduled_midnight_=midnight;
    scheduled_progress_=progress;
    StartTopic(entry);
    return true;
}

std::optional<bool> RoomTalkState::TakeScheduledNavigation() {
    return std::exchange(scheduled_navigation_,std::nullopt);
}

std::optional<RoomSound> RoomTalkState::TakeScheduledSound() {
    return std::exchange(scheduled_sound_,std::nullopt);
}

std::vector<unsigned> RoomTalkState::TakeScheduledActorSelections() {
    return std::exchange(scheduled_selections_,{});
}

std::optional<unsigned> RoomTalkState::TakeScheduledActorCommand(unsigned record) {
    return std::exchange(scheduled_actor_commands_.at(record),std::nullopt);
}

std::optional<unsigned> RoomTalkState::TakeScheduledCamera() {
    return std::exchange(scheduled_camera_request_,std::nullopt);
}

std::optional<std::uint32_t> RoomTalkState::TakeScheduledActivityDeadline() {
    return std::exchange(scheduled_activity_deadline_,std::nullopt);
}

std::array<std::optional<unsigned>,2> RoomTalkState::TakeScheduledStayWrites() {
    return std::exchange(scheduled_stay_writes_,{});
}

std::vector<RoomTalkState::ScheduledDiary> RoomTalkState::scheduled_diaries() const {
    if(phase_!=Phase::scheduled_diary) return {};
    if(scheduled_room_event_==7) {
        const auto& letter=letter_script_->descriptors.at(*scheduled_progress_.letter_sender);
        return {{letter.id,0x500000,letter.text}};
    }
    if(alternate_greeting_) {
        const auto& offsets=named_scheduled_diary_offsets_;
        if(scheduled_room_event_==6) return {{7,0x900000,pool_.at(offsets[0])}};
        if(scheduled_room_event_==5) return {{10,0x800000,pool_.at(offsets[2])},
                                             {11,0x800000,pool_.at(offsets[3])}};
        return {{8,0x700000,pool_.at(offsets[1])}};
    }
    if(scheduled_room_event_==6) return {{7,0x900000,pool_.at(robo_greeting_?0x305:0x379)}};
    if(scheduled_room_event_==5)
        return {{10,0x800000,pool_.at(robo_greeting_?0x465:0x4ce)},
                {11,0x800000,pool_.at(robo_greeting_?0x4f1:0x545)}};
    return {{8,0x700000,pool_.at(robo_greeting_?0x381:0x3ff)}};
}

RoomTalkState::FarewellSites RoomTalkState::farewell_sites() const {
    if(alternate_greeting_) {
        const auto& source=alternate_greeting_->schedule;
        return {source.farewell,source.utility_save,source.farewell+0x16};
    }
    return robo_greeting_ ? FarewellSites{0x6b896,0x6b247,0x6b8ac} :
                            FarewellSites{0x8d9d5,0x8d382,0x8d9eb};
}

RoomTalkState::ScheduledDiary RoomTalkState::expired_stay_diary() const {
    const auto offset=alternate_greeting_ ? named_scheduled_diary_offsets_[4] : robo_greeting_ ? 0x405 : 0x477;
    return {9,0xa00000,pool_.at(offset)};
}

void RoomTalkState::FinishScheduledDiary(bool recorded) {
    Require(phase_==Phase::scheduled_diary,"Unexpected farewell diary return");
    result_={Kind::truth,recorded};
    phase_=Phase::running;
}

bool RoomTalkState::TakeScheduledMenuRestriction() {
    return std::exchange(scheduled_menu_restriction_,false);
}

std::optional<RoomTalkState::FarewellAction> RoomTalkState::pending_farewell_action() const {
    if(phase_!=Phase::scheduled_farewell) return {};
    switch(farewell_stage_) {
    case FarewellStage::cover_wait: return FarewellAction::wait_for_cover;
    case FarewellStage::writing: return FarewellAction::write_slot;
    case FarewellStage::slot_wait: return FarewellAction::wait_for_slot;
    default: throw std::logic_error("Missing pending farewell action");
    }
}

void RoomTalkState::FinishFarewellAction(FarewellAction action) {
    Require(pending_farewell_action()==action,"Unexpected farewell host completion");
    switch(action) {
    case FarewellAction::wait_for_cover: farewell_stage_=FarewellStage::covered; break;
    case FarewellAction::write_slot: farewell_stage_=FarewellStage::written; break;
    case FarewellAction::wait_for_slot: farewell_stage_=FarewellStage::acknowledged; break;
    }
    phase_=Phase::running;
}

bool RoomTalkState::TakeFarewellComplete() {
    if(farewell_stage_!=FarewellStage::complete) return false;
    farewell_stage_=FarewellStage::inactive;
    return true;
}

unsigned RoomTalkState::named_contact_entry() const {
    return contact_scripts[actor_number_].entry;
}

unsigned RoomTalkState::attention_entry() const {
    return contact_scripts[actor_number_].attention;
}

bool RoomTalkState::BeginNamedContact() {
    const auto pc=named_contact_entry();
    if (!alternate_greeting_ || phase_!=Phase::done || ordinary_ || !code_.count(pc))
        return false;
    vocabulary_.BeginConversation();
    StartTopic(pc);
    columns_=9; rows_=4;
    return true;
}

std::optional<bool> RoomTalkState::TakeScheduledReturn() {
    if(!std::exchange(scheduled_return_,false)) return {};
    return std::exchange(scheduled_count_contact_,std::nullopt);
}

void RoomTalkState::StartTopic(unsigned pc) {
    phase_=Phase::running; pc_=pc;
    ordinary_=true; ordinary_return_=ordinary_rejected_=false;
    frontier_.reset();
    farewell_=skip_farewell_=actor_wait_=message_wait_=false;
    last_answer_=-1; delay_=0; result_={}; locals_.clear(); stack_.clear(); callers_.clear();
    control_resumes_.clear();
}

void RoomTalkState::ReleaseOrdinaryFrontier() {
    if (!ordinary_ || phase_ != Phase::frontier) return;
    ordinary_ = false;
    direct_teaching_=false;
    if(reserved_word_>=0) vocabulary_.ReleaseWord(std::exchange(reserved_word_,-1));
    frontier_.reset();
    selected_fallback_={};
    indirect_topic_=0;
    robo_contact_greeting_=false;
    locals_.clear(); stack_.clear(); callers_.clear(); blocks_.clear();
    control_resumes_.clear();
    phase_ = Phase::done;
}

void RoomTalkState::LoadOutings(const std::filesystem::path& dir) {
    std::ifstream file(dir/"toro-outings.bin",std::ios::binary);
    auto read=[&](unsigned size) {
        unsigned char bytes[4]{};
        Require(size<=4 && bool(file.read(reinterpret_cast<char*>(bytes),size)),"Incomplete outing package");
        return Word(bytes,size);
    };
    const auto version=read(4);
    Require(version==0x35544f52 || version==0x34544f52,"Missing Toro outing data");
    outing_enabled_=read(4);
    for(auto& mask : outing_period_masks_) mask=read(1);
    auto& order=global_arrays_[0x5f92];
    std::array<bool,133> seen{};
    for(unsigned i=0;i<132;++i) {
        const unsigned row=read(1);
        Require(row>0 && row<seen.size() && !seen[row],"Invalid outing index");
        seen[row]=true; order.push_back({Kind::integer,int(row)});
    }
    auto& destinations=global_arrays_[0x59da];
    for(unsigned i=0;i<133;++i) {
        const unsigned kind=read(1),primary=read(2),secondary=read(2),offer=read(1);
        auto fields=std::make_shared<std::vector<Value>>(9);
        if(kind) {
            Require(kind<outing_period_masks_.size() && vocabulary_.HasPredicate(primary) &&
                    (!secondary || vocabulary_.HasPredicate(secondary)),"Invalid outing descriptor");
            fields->at(0)={Kind::integer,int(kind)};
            fields->at(1)={Kind::constant_ref,int(primary)};
            Require(offer<=1,"Invalid outing offer flag");
            fields->at(6)={Kind::truth,int(offer)};
            if(secondary) fields->at(2)={Kind::constant_ref,int(secondary)};
        } else Require(!primary && !secondary,"Invalid empty outing descriptor");
        for(unsigned slot : {3u,4u,5u,7u,8u}) {
            const auto length=read(2);
            if(length==0xffff) continue;
            Require(kind!=0,"Unexpected empty outing text");
            std::string text(length,'\0');
            Require(bool(file.read(text.data(),length)),"Incomplete outing text");
            fields->at(slot)={Kind::string,0,std::move(text)};
        }
        Require(!kind || fields->at(4).kind==Kind::string,"Missing outing label");
        destinations.push_back({Kind::array,0,{},0,std::move(fields)});
    }
    if (version==0x34544f52) outing_details_[11]=read(2);
    else for (auto& details:outing_details_) details=read(2);
    const unsigned table_count=read(2);
    for(unsigned i=0;i<table_count;++i) {
        const unsigned ct=read(2),count=read(2);
        Require(!outing_tables_.count(ct) && count && count<=7,"Invalid rooftop detail list");
        auto& cells=outing_tables_[ct];
        std::vector<unsigned> phrases;
        for(unsigned j=0;j<count;++j) {
            const auto tag=read(4);
            if(!tag) cells.emplace_back();
            else if(tag>>16==0xb003) cells.push_back({Kind::constant_ref,int(tag&0xffff)});
            else {
                Require(tag>>28==5,"Invalid rooftop detail type");
                const unsigned offset=tag&0x0fffffff;
                std::string text(read(2),'\0');
                Require(bool(file.read(text.data(),text.size())),"Incomplete rooftop detail text");
                const auto saved=pool_.emplace(offset,text);
                Require(saved.first->second==text,"Conflicting rooftop text");
                cells.push_back({Kind::string,0,std::move(text)});
                phrases.push_back(offset);
            }
        }
        if(phrases.size()==count) phrase_lists_[ct]=std::move(phrases);
    }
    for (const auto details:outing_details_)
        if (details) Require(outing_tables_.count(details)!=0,"Missing destination detail list");
    Require(outing_details_[11]!=0,"Missing roof detail registration");
    global_arrays_[0].resize(8); // The eight mutable destination strings read by the scene.
    outing_script_cells_[0x2871]={Kind::integer,0};
    Require(file.peek()==std::char_traits<char>::eof(),"Trailing outing data");
}

unsigned RoomTalkState::SelectHomeInvitation(unsigned mode,unsigned period,recovered::RngNativeOwner& rng) {
    Require(mode<=1 && period<6 && outing_current_scene_==0,
            "Invalid home invitation context");
    const auto& destinations=global_arrays_.at(0x59da);
    std::vector<unsigned> eligible;
    // 2EB8/2F41 scan the descriptor IDs in numerical order. The shuffled
    // G5F92 index belongs to the player's outing menu, not this selection.
    for(unsigned id=1;id<destinations.size();++id) {
        const auto& row=*destinations[id].cells;
        const unsigned kind=row[0].data;
        if(!kind || row[8].kind!=Kind::string || row[8].text=="DEFAULT") continue;
        if(!(outing_period_masks_.at(kind)&(1u<<period))) continue;
        const bool enabled=(outing_enabled_&(1u<<kind))!=0;
        if(mode ? enabled : (row[6].data && !enabled)) eligible.push_back(id);
    }
    home_invitation_mode_=mode;
    home_invitation_id_=eligible.empty()?0:eligible.at(rng.NextC3234()%eligible.size());
    return home_invitation_id_;
}

void RoomTalkState::LoadNamedOutings(const std::filesystem::path& dir) {
    const char* names[]{"robo-outings.bin","usagi-outings.bin","inu-outings.bin","kaeru-outings.bin"};
    const auto path=dir/names[actor_number_-1];
    std::ifstream file(path,std::ios::binary);
    if(!file) {
        Require(!std::filesystem::exists(path),"Cannot read actor outing data");
        return;
    }
    const auto read=[&](unsigned size) {
        unsigned char bytes[4]{};
        Require(bool(file.read(reinterpret_cast<char*>(bytes),size)),"Truncated actor outing data");
        return Word(bytes,size);
    };
    const auto text=[&] {
        const auto size=read(2);
        Require(size<=512,"Invalid actor outing text length");
        std::string result(size,'\0');
        Require(bool(file.read(result.data(),size)),"Truncated actor outing text");
        return result;
    };
    Require(read(4)==0x31554f4e,"Invalid actor outing header");
    named_outing_departure_=read(4);
    named_outing_return_=read(4);
    Require(named_outing_departure_ && named_outing_return_ &&
            read(1)==named_starting_outings[actor_number_-1],"Actor outing profile mismatch");
    for(auto& mask:outing_period_masks_) {
        mask=read(1);
        Require(mask<64,"Invalid actor outing period");
    }
    for(auto& message:named_outing_messages_) {
        message={read(4),text()};
        Require(message.site && !message.pattern.empty(),"Missing actor outing message");
    }
    for(auto& choice:named_outing_choices_) {
        choice=text();
        Require(!choice.empty(),"Missing actor outing option");
    }
    for(unsigned i=0;i<133;++i) {
        NamedOutingDestination destination{read(1),read(2),read(2),text(),text(),text()};
        Require(destination.kind<outing_period_masks_.size() &&
                (destination.kind ? i && !destination.label.empty() && vocabulary_.HasPredicate(destination.primary) &&
                    (!destination.secondary || vocabulary_.HasPredicate(destination.secondary))
                    : !destination.primary && !destination.secondary && destination.label.empty()),
                "Invalid actor outing destination");
        named_outing_destinations_.push_back(std::move(destination));
    }
    Require(file.peek()==std::char_traits<char>::eof(),"Trailing actor outing data");
}

void RoomTalkState::NamedOutingSpeech(unsigned line,const std::string& label,unsigned verb) {
    const auto& source=named_outing_messages_.at(line);
    std::string result;
    for(std::size_t i=0;i<source.pattern.size();++i) {
        const auto unit=static_cast<unsigned char>(source.pattern[i]);
        if(unit!=0x14 && unit!=0x17) {
            result+=source.pattern[i];
            continue;
        }
        Require(i+2<source.pattern.size(),"Incomplete actor outing message operand");
        const auto operand=Word(reinterpret_cast<const unsigned char*>(source.pattern.data()+i+1),2);
        i+=2;
        if(unit==0x17) result+=operand==0x17?suffix_:pool_.at(speech_suffixes_.at(operand));
        else {
            Require(operand<=1,"Unknown actor outing message operand");
            result+=operand?named_outing_choices_.at(verb):label;
        }
    }
    StartMessage(source.site,{Kind::string,0,std::move(result)});
}

bool RoomTalkState::BeginNamedOuting(unsigned period,bool returning,recovered::RngNativeOwner& rng) {
    if(!has_named_outings() || ordinary_ || phase_!=Phase::done || period>=6 ||
       (returning ? !outing_current_scene_ || !outing_snapshot_ : outing_current_scene_!=0)) return false;
    phase_=Phase::running;
    ordinary_=true;
    ordinary_return_=ordinary_rejected_=counted_contact_=false;
    frontier_.reset();
    last_answer_=-1;
    indirect_topic_=named_outing_entry(returning);
    outing_returning_=returning;
    if(returning) {
        named_outing_stage_=NamedOutingStage::prepare_return;
        return true;
    }
    outing_snapshot_.reset();
    named_outing_options_.clear();
    std::vector<unsigned> eligible;
    for(unsigned id=1;id<named_outing_destinations_.size();++id) {
        const auto& item=named_outing_destinations_[id];
        if(item.kind && (outing_enabled_ & (1u<<item.kind)) &&
           (outing_period_masks_[item.kind] & (1u<<period)) && vocabulary_.FirstOutingMatch(item.primary)>=0)
            eligible.push_back(id);
    }
    unsigned kinds=0;
    while(!eligible.empty() && named_outing_options_.size()<4) {
        const auto id=eligible[rng.NextC3234()%eligible.size()];
        const unsigned kind=1u<<named_outing_destinations_[id].kind;
        if(!(kinds&kind)) named_outing_options_.push_back(id);
        kinds|=kind;
        eligible.erase(std::find(eligible.begin(),eligible.end(),id));
    }
    named_outing_stage_=named_outing_options_.empty()?NamedOutingStage::finish:NamedOutingStage::menu;
    NamedOutingSpeech(named_outing_options_.empty()?0:1);
    return true;
}

void RoomTalkState::AdvanceNamedOuting(recovered::RngNativeOwner& rng,bool actor_ready) {
    switch(named_outing_stage_) {
    case NamedOutingStage::menu:
        answer_options_.clear();
        for(const auto id:named_outing_options_) answer_options_.push_back(named_outing_destinations_[id].label);
        answer_options_.push_back(named_outing_choices_[0]);
        answer_frame_=answer_cursor_=answer_ticks_=0;
        answer_close_=false; answer_held_={};
        phase_=Phase::answer_pending;
        named_outing_stage_=NamedOutingStage::answer;
        return;
    case NamedOutingStage::answer: {
        if(last_answer_<=0 || unsigned(last_answer_)>named_outing_options_.size()) {
            outing_interrupt_requested_=!actor_ready && outing_interrupt_!=0;
            named_outing_stage_=NamedOutingStage::finish;
            return;
        }
        const auto id=named_outing_options_[last_answer_-1];
        const auto& item=named_outing_destinations_[id];
        const int eligible=vocabulary_.FirstOutingMatch(item.primary);
        const unsigned count=vocabulary_.Candidates(item.primary,item.secondary,true);
        const int preferred=vocabulary_.FinishLookup(count?rng.NextC3234()%count:0);
        const int row=preferred>=0?preferred:eligible;
        Require(row>=0,"Actor outing lost the selected dictionary word");
        const auto written=vocabulary_.DisplayName(unsigned(row));
        const auto expand=[&](std::string value) {
            for(std::size_t at=0;(at=value.find("$_WORD",at))!=std::string::npos;at+=written.size())
                value.replace(at,6,written);
            return value;
        };
        outing_snapshot_=RoomOutingDescriptor{id,item.kind,unsigned(row),eligible,item.primary,item.secondary,
                                             written,item.label,expand(item.title),expand(item.description)};
        NamedOutingSpeech(2,item.label,item.kind==8?2:1);
        named_outing_stage_=NamedOutingStage::depart;
        return;
    }
    case NamedOutingStage::depart:
        if(visible() || closing_) return;
        actor_command_=174;
        named_outing_stage_=NamedOutingStage::walk;
        return;
    case NamedOutingStage::walk:
        if(!actor_ready) return;
        actor_command_=175;
        named_outing_stage_=NamedOutingStage::scene_request;
        return;
    case NamedOutingStage::prepare_return:
        outing_interrupt_requested_=!actor_ready && outing_interrupt_!=0;
        named_outing_stage_=NamedOutingStage::scene_request;
        return;
    case NamedOutingStage::scene_request:
        if(!actor_ready) return;
        outing_scene_request_=outing_returning_?0:outing_snapshot_->kind;
        phase_=Phase::outing_scene;
        return;
    case NamedOutingStage::arrived:
        named_outing_stage_=NamedOutingStage::finish;
        if(!outing_returning_) {
            const auto& destination=*outing_snapshot_;
            if(!destination.title.empty())
                StartMessage(indirect_topic_,{Kind::string,0,destination.title+"\n"+destination.description});
        }
        return;
    case NamedOutingStage::finish:
        // Both cancel and return can leave an earlier ASC in flight. Keep
        // ownership until it can actually accept the next ambient request.
        if(visible() || closing_ || !actor_ready) return;
        named_outing_stage_=NamedOutingStage::inactive;
        ordinary_=false;
        ordinary_return_=true;
        phase_=Phase::done;
        return;
    case NamedOutingStage::inactive: return;
    }
}

bool RoomTalkState::BeginHomeInvitation() {
    if(!home_invitation_id_ || phase_!=Phase::done || ordinary_ || outing_current_scene_!=0 ||
       !code_.count(0x12f39)) return false;
    // Resume the same 12F39 frame after its immediate mode-dependent selector.
    // 12F4B/12F58 have stored that one RNG selection in local 1.
    locals_.assign(16,Value{});
    locals_[0]={Kind::integer,int(home_invitation_mode_)};
    locals_[1]={Kind::integer,int(std::exchange(home_invitation_id_,0))};
    phase_=Phase::running; pc_=0x12f59; indirect_topic_=0x12f39;
    excursion_=ordinary_=true; outing_returning_=false;
    ordinary_return_=ordinary_rejected_=outing_room_request_=false;
    eligibility_words_.clear(); outing_snapshot_.reset();
    frontier_.reset(); last_answer_=-1; delay_=0;
    farewell_=skip_farewell_=actor_wait_=message_wait_=false;
    result_={}; stack_.clear(); callers_.clear();
    return true;
}

bool RoomTalkState::BeginOuting(unsigned period) {
    if(phase_!=Phase::done || ordinary_ || !code_.count(0x13219) || period>=6) return false;
    if(outing_current_scene_!=0) return false;
    outing_returning_=false; eligibility_words_.clear(); outing_snapshot_.reset();
    phase_=Phase::running; pc_=0x13219;
    excursion_=ordinary_=true; outing_period_=period; indirect_topic_=0x13219;
    ordinary_return_=ordinary_rejected_=false; outing_room_request_=false;
    frontier_.reset(); last_answer_=-1; delay_=0;
    farewell_=skip_farewell_=actor_wait_=message_wait_=false;
    result_={}; locals_.clear(); stack_.clear(); callers_.clear();
    return true;
}

bool RoomTalkState::BeginOutingReturn() {
    if(phase_!=Phase::done || ordinary_ || outing_current_scene_==0 || !outing_snapshot_) return false;
    excursion_=ordinary_=outing_returning_=true; ordinary_return_=ordinary_rejected_=false;
    phase_=Phase::running; pc_=indirect_topic_=0x1320e;
    frontier_.reset(); result_={}; locals_.clear(); stack_.clear(); callers_.clear();
    return true;
}

void RoomTalkState::SetOutingEnvironment(bool game28,bool lamp,unsigned period,unsigned camera_mode) {
    outing_game28_=game28; outing_lamp_=lamp; outing_period_=period; outing_camera_=camera_mode;
}
bool RoomTalkState::TakeOutingInterrupt() { return std::exchange(outing_interrupt_requested_,false); }
bool RoomTalkState::TakeOutingWideCamera() { return std::exchange(outing_wide_,false); }
std::optional<bool> RoomTalkState::TakeOutingWipe() { return std::exchange(outing_wipe_,{}); }
void RoomTalkState::StampContactLocation(std::uint32_t minute) {
    outing_script_cells_[0x3de7]={Kind::calendar_time,static_cast<int>(minute & 0x0fffffffu)};
}

RoomContactLocation RoomTalkState::contact_location() const {
    RoomContactLocation place;
    place.scene=outing_current_scene_;
    if(actor_number_==0) {
        const auto& slots=global_arrays_.at(0x243f);
        Require(slots.at(0).kind==Kind::integer,"Invalid contact destination index");
        place.descriptor_id=unsigned(slots[0].data);
        if(place.descriptor_id) {
            const auto& record=slots.at(1);
            Require(record.kind==Kind::array && record.cells && record.cells->at(0).kind==Kind::integer,
                    "Missing current contact destination type");
            place.kind=unsigned(record.cells->at(0).data);
        }
    } else if(place.scene && outing_snapshot_) {
        place.kind=outing_snapshot_->kind;
        place.descriptor_id=outing_snapshot_->destination;
    }
    const auto transfer=outing_script_cells_.find(0x3de7);
    if(transfer!=outing_script_cells_.end() && transfer->second.kind==Kind::calendar_time)
        place.transfer_minute=unsigned(transfer->second.data)&0x0fffffffu;
    return place;
}

// The active CT243F descriptor owns both strings through the outing.
std::vector<std::string> RoomTalkState::current_outing_names() const {
    if(actor_number_!=0)
        return outing_current_scene_ && outing_snapshot_
            ? std::vector<std::string>{outing_snapshot_->label,outing_snapshot_->title}
            : std::vector<std::string>{};
    const auto& slots = global_arrays_.at(0x243f);
    const auto& descriptor = slots.at(1);
    if (!descriptor.cells || descriptor.cells->size() <= 4) return {};
    return {descriptor.cells->at(4).text, slots.at(3).text};
}

std::optional<unsigned> RoomTalkState::outing_return_deadline() const {
    const auto it=outing_script_cells_.find(0x2875);
    if(!outing_current_scene_ || it==outing_script_cells_.end() || it->second.kind!=Kind::calendar_time)
        return {};
    return unsigned(it->second.data)&0x0fffffffu;
}
std::array<std::optional<std::string>,8> RoomTalkState::outing_labels() const {
    std::array<std::optional<std::string>,8> labels{};
    const auto found=global_arrays_.find(0);
    if(found!=global_arrays_.end())
        for(unsigned i=0;i<labels.size();++i)
            if(const auto& cell=found->second.at(i); cell.kind==Kind::string) labels[i]=cell.text;
    return labels;
}
bool RoomTalkState::FinishOutingScene(unsigned selector) {
    if(phase_!=Phase::outing_scene || outing_scene_request_!=selector) return false;
    // The actual scene owner releases the caller suspended by the callback.
    outing_current_scene_=selector; outing_scene_request_.reset(); phase_=Phase::running;
    if(named_outing_stage_!=NamedOutingStage::inactive) {
        if(calendar_minute_) StampContactLocation(*calendar_minute_);
        named_outing_stage_=NamedOutingStage::arrived;
        outing_wipe_=true;
    }
    return true;
}
bool RoomTalkState::FinishOutingInventoryRestore() {
    if(phase_!=Phase::outing_inventory) return false;
    phase_=Phase::running; return true;
}

bool RoomTalkState::TakeOutingRoomRequest() {
    return std::exchange(outing_room_request_,false);
}

bool RoomTalkState::BeginTeaching() {
    const auto entry=robo_greeting_?0x8e05u:0x8dbau;
    if(phase_!=Phase::done || ordinary_ || !code_.count(entry)) return false;
    StartTopic(entry);
    direct_teaching_=true;
    counted_contact_=false;
    if(robo_greeting_) { columns_=9; rows_=4; }
    return true;
}

bool RoomTalkState::BeginOrdinaryFarewell() {
    if(phase_!=Phase::done || ordinary_) return false;
    ordinary_=farewell_=true; ordinary_return_=ordinary_rejected_=false;
    vocabulary_.BeginConversation(); phase_=Phase::running; pc_=0x7d548;
    locals_.clear(); stack_.clear(); callers_.clear(); result_={}; delay_=0;
    return true;
}

bool RoomTalkState::BeginRoomAttentionMessage() {
    const auto entry=attention_entry();
    if(phase_!=Phase::done || ordinary_ || !code_.count(entry)) return false;
    // 8D8EE chooses DIALOG(0,5,2,100,100) and calls the 8D3E2 text.
    columns_=5; rows_=2; attention_=true; attention_return_=false;
    phase_=Phase::running; pc_=entry; locals_.clear(); stack_.clear(); callers_.clear();
    result_={}; delay_=0; return true;
}

std::vector<unsigned> RoomTalkState::TakeEffects() {
    std::vector<unsigned> result;
    result.swap(effects_);
    return result;
}

std::vector<std::optional<unsigned>> RoomTalkState::TakeTopicEnqueues() {
    return std::exchange(topic_enqueues_,{});
}

bool RoomTalkState::TakeFirstDayDiaryRequest() {
    return std::exchange(first_day_diary_request_, false);
}

bool RoomTalkState::HasCodeAt(unsigned pc) const {
    if(const auto* module=ActiveModule()) {
        const auto* command=module->command(pc);
        return command && command->opcode>=0;
    }
    const auto after=code_.upper_bound(pc);
    if(after==code_.begin()) return false;
    const auto& [start,bytes]=*std::prev(after);
    return pc-start<bytes.size();
}

unsigned RoomTalkState::Read(unsigned n) {
    if(const auto* module=ActiveModule()) {
        const auto limit=module->header().code_size;
        Require(pc_<=limit && n<=limit-pc_,"Outside registered KSC bytecode");
        const auto value=Word(module->bytes().data()+28+pc_,n);
        pc_+=n;
        return value;
    }
    auto it=code_.upper_bound(pc_);
    Require(it!=code_.begin(),"Outside the cold NEKO callback");
    --it;
    const unsigned at=pc_-it->first;
    Require(n<=it->second.size() && at<=it->second.size()-n,"Outside selected KSC routine");
    pc_+=n;
    return Word(it->second.data()+at,n);
}

RoomTalkState::Value RoomTalkState::Pop() {
    Require(!stack_.empty(),"KSC talk operand underflow");
    auto value=std::move(stack_.back()); stack_.pop_back();
    return value;
}

RoomTalkState::Value RoomTalkState::Dereference(Value v) const {
    if(v.kind==Kind::frame_ref) {
        Require(v.frame_owner==FrameOwner(),"KSC frame reference belongs to a different context");
        return (v.depth==callers_.size()?locals_:callers_.at(v.depth).locals).at(v.data);
    }
    if(v.kind==Kind::word_field) return DictionaryField(v.data,int(v.depth));
    if(v.kind==Kind::question_field) {
        const auto raw=vocabulary_.QuestionField(v.data,unsigned(v.depth));
        if(!raw) return {};
        if(raw>>28==5) return {Kind::string,0,vocabulary_.QuestionText(v.data,unsigned(v.depth))};
        Require(raw>>28==2 || raw>>28==3,"Unexpected prepared question field");
        return {raw>>28==3?Kind::calendar_time:Kind::integer,ScriptInteger(raw)};
    }
    if(v.kind==Kind::association_ref) return v.entries->at(v.text);
    if(v.kind==Kind::script_local_ref) return ActiveScriptLocals().at(v.data);
    if(v.kind==Kind::element) return v.cells->at(v.data);
    if(v.kind==Kind::global_element) return global_arrays_.at(v.data).at(v.depth);
    if(v.kind==Kind::teach_field) return teaching_.at(v.data);
    if(v.kind==Kind::global_ref && (v.data==21 || v.data==22 || v.data==23))
        return {Kind::string,0,v.data==21?nickname_:v.data==22?player_:suffix_};
    throw std::runtime_error("Unknown cold KSC reference");
}

RoomTalkState::Value RoomTalkState::DictionaryField(int row,int field) const {
    if(row==600 && field==26) {
        const auto& saying=vocabulary_.player_saying();
        return {Kind::string,0,saying,0,{}, {{row,26,0,saying.size()}}};
    }
    if (field>=19) {
        Require(row>=600 && unsigned(row)<vocabulary_.row_count() && field<30,
                "Invalid player dictionary field");
        const auto card=vocabulary_.FriendCard(unsigned(row));
        const auto& cell=card.children.at(field);
        const auto tag=cell.word>>28;
        if (tag==2 || tag==3)
            return {tag==2?Kind::integer:Kind::calendar_time,ScriptInteger(cell.word)};
        if (tag==5 || tag==6)
            return {Kind::string,0,cell.text,0,{},{{row,unsigned(field),0,cell.text.size()}}};
        Require(!cell.word,"Unsupported player dictionary field type");
        return {};
    }
    if(!vocabulary_.HasField(row,field)) return {};
    if(field==1 || field==2 || field==13) {
        auto text=vocabulary_.FieldText(row,field);
        return {Kind::string,0,std::string(text),0,{}, {{row,unsigned(field),0,text.size()}}};
    }
    return {Kind::integer,vocabulary_.FieldInt(row,field)};
}

void RoomTalkState::Assign(const Value& ref,Value value) {
    if(executing_child_)
        Require(ref.kind==Kind::frame_ref || ref.kind==Kind::script_local_ref ||
                ref.kind==Kind::element || ref.kind==Kind::global_element ||
                ref.kind==Kind::association_ref,"Child KSC cannot write a room-owned field");
    if(ref.kind==Kind::frame_ref) {
        Require(ref.frame_owner==FrameOwner(),"KSC frame reference belongs to a different context");
        (ref.depth==callers_.size()?locals_:callers_.at(ref.depth).locals).at(ref.data)=std::move(value);
    } else if(ref.kind==Kind::teach_field) teaching_.at(ref.data)=std::move(value);
    else if(ref.kind==Kind::question_field) {
        if(value.kind==Kind::string) {
            vocabulary_.WriteQuestionText(ref.data,unsigned(ref.depth),value.text);
            return;
        }
        Require(value.kind==Kind::nil || value.kind==Kind::integer || value.kind==Kind::calendar_time,
                "Unsupported prepared question value");
        const unsigned tag=value.kind==Kind::calendar_time?0x30000000u:0x20000000u;
        vocabulary_.WriteQuestionField(ref.data,unsigned(ref.depth),
                value.kind==Kind::nil?0:tag|(unsigned(value.data)&0x0fffffffu));
    }
    else if(ref.kind==Kind::association_ref) {
        Require(value.kind==Kind::nil || value.kind==Kind::integer || value.kind==Kind::truth ||
                value.kind==Kind::string || value.kind==Kind::calendar_time || value.kind==Kind::fixed14 ||
                value.kind==Kind::code,"Unsupported associative table value");
        ref.entries->at(ref.text)=std::move(value);
    }
    else if(ref.kind==Kind::script_local_ref) ActiveScriptLocals().at(ref.data)=std::move(value);
    else if(ref.kind==Kind::element) ref.cells->at(ref.data)=std::move(value);
    else if(ref.kind==Kind::global_element) global_arrays_.at(ref.data).at(ref.depth)=std::move(value);
    else if(ref.kind==Kind::word_field) {
        if(ref.data==600 && ref.depth==26 && value.kind==Kind::string)
            vocabulary_.SetPlayerSaying(value.text);
        else if(value.kind==Kind::string) vocabulary_.WriteText(ref.data,unsigned(ref.depth),value.text);
        else if(value.kind==Kind::nil && ref.depth==13) vocabulary_.ClearAlternate(ref.data);
        else {
            Require(value.kind==Kind::integer || value.kind==Kind::nil,"Invalid KSC word field value");
            vocabulary_.WriteNumber(ref.data,unsigned(ref.depth),value.kind==Kind::nil?std::nullopt:std::optional<int>(value.data));
        }
    }
    else if(scheduled_room_event_ && ref.kind==Kind::constant_ref &&
            unsigned(ref.data)==(alternate_greeting_?alternate_greeting_->schedule.motion_cell:robo_greeting_?0x3ec9u:0x3df7u) &&
            value.kind==Kind::integer)
        outing_script_cells_[ref.data]=std::move(value);
    else if(excursion_ && ref.kind==Kind::constant_ref &&
            (ref.data==0x2871 || ref.data==0x2874 || ref.data==0x2875 || ref.data==0x3de7 || ref.data==0x3df7))
        outing_script_cells_[ref.data]=std::move(value);
    else if(excursion_ && ref.kind==Kind::global_ref && ref.data==0x6105)
        outing_script_cells_[ref.data]=std::move(value);
    else if(exchange_inventory_ && ref.kind==Kind::global_ref &&
            (ref.data==0x98c3 || ref.data==0x9a32 ||
             (ref.data==0x9a33 && pc_==0x8d1c4 && value.kind==Kind::truth && value.data==1)))
        friends_cells_[ref.data]=std::move(value);
    else if(friends_introduction_ && ref.kind==Kind::constant_ref &&
            (ref.data==0x2869 || ref.data==0x2877 || ref.data==0x2d54 || ref.data==0x2d55))
        friends_cells_[ref.data]=std::move(value);
    else if((friends_introduction_ || scheduled_room_event_) &&
            ref.kind==Kind::constant_ref &&
            unsigned(ref.data)==(alternate_greeting_?alternate_greeting_->skip_farewell:robo_greeting_?0x2927u:0x2873u)
            && value.kind==Kind::nil)
        skip_farewell_=false;
    else if(scheduled_room_event_ && ref.kind==Kind::constant_ref &&
            unsigned(ref.data)==(alternate_greeting_?alternate_greeting_->schedule.event_cell-1:robo_greeting_?0x291au:0x2866u) &&
            (value.kind==Kind::calendar_time || value.kind==Kind::integer))
        scheduled_deadline_=std::move(value);
    else if(scheduled_room_event_ && ref.kind==Kind::constant_ref &&
            unsigned(ref.data)==(alternate_greeting_?alternate_greeting_->skip_farewell+1:robo_greeting_?0x2928u:0x2874u) &&
            value.kind==Kind::integer &&
            (value.data==0 || value.data==1 || (ScheduledFarewell() && value.data==3)))
        scheduled_camera_=unsigned(value.data);
    else if(alternate_greeting_ && scheduled_room_event_==5 && ref.kind==Kind::global_ref &&
            ref.data==0x16 && pc_==0x64c5d && value.kind==Kind::string && value.text==player_)
        player_=std::move(value.text); // INU 64C54–64C5C restores the player name from the profile.
    else if(alternate_greeting_ && ScheduledFarewell() && ref.kind==Kind::global_ref &&
            value.kind==Kind::integer) {
        const auto& live=alternate_greeting_->schedule;
        const unsigned offset=pc_-live.callbacks[scheduled_room_event_-2];
        if(unsigned(ref.data)==live.midnight_global-1 &&
           ((scheduled_room_event_==3 && ((offset==0x66 && value.data==0) ||
                                          (offset==0x90 && value.data>14))) ||
            (scheduled_room_event_==5 && offset==0x6f && value.data==0))) {
            scheduled_stay_writes_[0]=unsigned(value.data);
        } else if(unsigned(ref.data)==live.midnight_global+1 &&
                  offset==(scheduled_room_event_==3?0x7cu:0x64u) && value.data>=3 && value.data<=10) {
            scheduled_stay_writes_[1]=unsigned(value.data);
        } else throw std::runtime_error("Unsupported named actor farewell assignment");
    }
    else if(scheduled_room_event_==3 && ref.kind==Kind::global_ref && value.kind==Kind::integer &&
            ((ref.data==(robo_greeting_?0x59ac:0x59a4) &&
              ((pc_==(robo_greeting_?0x67567u:0x89650u) && value.data==0) ||
               (pc_==(robo_greeting_?0x67591u:0x8967au) && value.data>14))) ||
             (ref.data==(robo_greeting_?0x59ae:0x59a6) && pc_==(robo_greeting_?0x6757du:0x89666u) &&
              value.data>=3 && value.data<=10)))
        scheduled_stay_writes_[ref.data==(robo_greeting_?0x59ae:0x59a6)]=unsigned(value.data);
    else if(scheduled_room_event_==5 && ref.kind==Kind::global_ref && value.kind==Kind::integer &&
            ((ref.data==(robo_greeting_?0x59ac:0x59a4) && pc_==(robo_greeting_?0x67611u:0x896fau) && !value.data) ||
             (ref.data==(robo_greeting_?0x59ae:0x59a6) && pc_==(robo_greeting_?0x67606u:0x896efu) &&
              value.data>=3 && value.data<=10)))
        scheduled_stay_writes_[ref.data==(robo_greeting_?0x59ae:0x59a6)]=unsigned(value.data);
    else if(robo_greeting_ && ordinary_ && ref.kind==Kind::global_ref && ref.data==0x17 &&
            (pc_==0x56bd3 || pc_==0x56a4c || pc_==0x56a08)) {
        Require(value.kind==Kind::string,"Invalid conversation speech suffix");
        suffix_=std::move(value.text);
    }
    else if(((scheduled_room_event_==4 && pc_==0x7c9e9) ||
             (scheduled_room_event_==5 && pc_==0x7cbe9)) && ref.kind==Kind::global_ref && ref.data==0x17) {
        Require(value.kind==Kind::string && value.text==pool_.at(0),"Unexpected scheduled speech suffix");
        suffix_=std::move(value.text);
    }
    else if(ref.kind==Kind::constant_ref && unsigned(ref.data)==contact_scripts[actor_number_].slot)
        selected_fallback_=std::move(value);
    else if(ref.kind==Kind::constant_ref && ref.data==0 && value.kind==Kind::truth)
        event20_cleanup_=value.data!=0; // 4E/46 around the address-form ASK in 49DE.
    else if(scheduled_room_event_ && alternate_greeting_ && ref.kind==Kind::constant_ref &&
            unsigned(ref.data)==alternate_greeting_->skip_farewell-1 &&
            pc_==0x2176 && value.kind==Kind::integer)
        mood_set_tick_=value.data;
    else if(scheduled_room_event_ && alternate_greeting_ && ref.kind==Kind::constant_ref &&
            unsigned(ref.data)==alternate_greeting_->skip_farewell-2 &&
            pc_==0x20a7 && value.kind==Kind::integer)
        outing_script_cells_[ref.data]=std::move(value);
    else if(ref.kind==Kind::constant_ref && unsigned(ref.data)==contact_scripts[actor_number_].mood_tick &&
            value.kind==Kind::integer) mood_set_tick_=value.data;
    else if(ref.kind==Kind::constant_ref &&
            unsigned(ref.data)==(alternate_greeting_ ? alternate_greeting_->skip_farewell : robo_greeting_?0x2927u:0x2873u)
            && value.kind==Kind::integer && value.data==2) skip_farewell_=true;
    else throw std::runtime_error("Unsupported KSC assignment");
}

RoomTalkState::Value RoomTalkState::ReadText() {
    Value text{Kind::string};
    for(unsigned c=Read(1);c;c=Read(1)) {
        if(c==0x14) {
            const auto control_pc=pc_-1;
            const auto local=Read(2);
            const auto& value=locals_.at(local);
            if(value.kind==Kind::integer) {
                // BOOT 75778 converts a tagged script integer with "%d".
                text.text+=std::to_string(ScriptInteger(unsigned(value.data)));
                continue;
            }
            if(value.kind==Kind::nil && !nil_inline_text_.empty()) {
                text.text+=nil_inline_text_;
                continue;
            }
            Require(value.kind==Kind::string,"Unsupported KSC message operand");
            for(auto operand : value.word_operands) {
                operand.offset+=text.text.size();
                operand.control_pc=control_pc;
                operand.local=local;
                text.word_operands.push_back(operand);
            }
            text.text+=value.text;
        } else if(c==0x17) {
            const auto index=Read(2);
            if(index==0x17) text.text+=suffix_;
            else {
                const auto it=speech_suffixes_.find(index);
                Require(it!=speech_suffixes_.end(),"Unknown KSC speech suffix");
                text.text+=pool_.at(it->second);
            }
        } else text.text+=static_cast<char>(c);
    }
    return text;
}

void RoomTalkState::Message() {
    const auto site=pc_-1;
    StartMessage(site,ReadText());
}

void RoomTalkState::StartMessage(unsigned site,Value text) {
    message_pc_=site;
    message_=std::move(text.text);
    message_word_operands_=std::move(text.word_operands);
    message_word_rows_.clear();
    for(const auto& operand : message_word_operands_) message_word_rows_.push_back(operand.row);
    message_indirect_topic_=indirect_topic_;
    message_wait_=true; text_cursor_=group_end_=0; idle_=90; closing_=false;
    if(lines_.empty()) lines_.emplace_back();
}

void RoomTalkState::ReturnFromGreeting() {
    // Cold action6 returns via 89966 and 8DFB5 into the main update. Its
    // common false cleanup (8E023 -> 8D932) requests camera mode zero at
    // 8D9AC/8D9AD. 2962 suppresses an unchanged mode; the room camera owns
    // that state. The later independent idle selector is a different action.
    wide_camera_requested_=true;
    phase_=Phase::done;
}

void RoomTalkState::Call(unsigned target,std::vector<Value> args,bool indirect,bool preserve_result) {
    if(executing_child_) {
        Require(HasCodeAt(target),"Missing child KSC procedure");
        EnterProcedure(target,std::move(args),indirect,preserve_result);
        return;
    }
    const auto argc=args.size();
    const auto* source=alternate_greeting_? &alternate_greeting_->schedule : nullptr;
    if(scheduled_room_event_==7 && letter_script_ && target==letter_script_->diary_selector &&
       pc_==letter_script_->callback+0x15) {
        Require(argc==1 && args[0].kind==Kind::integer && args[0].data==0x500000,
                "Invalid return-letter diary priority");
        if(scheduled_progress_.letter_sender) phase_=Phase::scheduled_diary;
        else result_={Kind::truth,0};
        return;
    }
    if(scheduled_room_event_ && ((robo_greeting_ && target==0x1a89 && pc_==0x676b5) ||
                                 (source && target==0x1a9e && pc_==source->callbacks[0]+0x9a))) {
        Require(argc==2 && args[0].kind==Kind::calendar_time &&
                args[1].kind==Kind::integer && args[1].data==0,"Invalid ROBO ambient deadline");
        scheduled_activity_deadline_=unsigned(args[0].data)&0x0fffffffu;
        return;
    }
    if(scheduled_room_event_ && ((target==0x1aac && pc_==0x8995d) ||
                                  (robo_greeting_ && target==0x1aad && pc_==0x67874) ||
                                  (alternate_greeting_ && target==0x1ac2 && pc_==alternate_greeting_->schedule.wrapper+0xda))) {
        Require(!argc,"Invalid scheduled contact timer clear");
        // CT2868 belongs to the ambient contact timer, re-armed at 89A69 below.
        return;
    }
    if(scheduled_room_event_ && ((target==0x899f9 && pc_==0x89966) ||
                                  (robo_greeting_ && target==0x67910 && pc_==0x6787d) ||
                                  (alternate_greeting_ && target==alternate_greeting_->schedule.contact_cleanup &&
                                   pc_==alternate_greeting_->schedule.wrapper+0xe3))) {
        Require(argc==2 && args[0].kind==Kind::integer && !args[0].data &&
                args[1].kind==Kind::truth && !scheduled_secondary_,
                "Invalid scheduled contact cleanup");
        vocabulary_.BeginConversation();
        scheduled_count_contact_=args[1].data!=0;
        if(skip_farewell_) {
            scheduled_actor_commands_[0]=74; // CT2927/CT2873 == 2.
            return;
        }
        // 67934 (ROBO) and 89A1D (NEKO) call the actor's own farewell.
        target=alternate_greeting_?alternate_greeting_->farewell:robo_greeting_?0x5be76u:0x7d548u;
        args.clear();
    }
    if(exchange_inventory_ && peer_meetings_enabled_ && !argc &&
       ((target==0x8bff6 && pc_==0x8d1e4) || (target==0x8bf9b && pc_==0x8d1e8))) {
        // The host socket and the waiting overlay were closed before this
        // cancellation continuation. The native owner has no pending callback.
        const auto& pending=friends_cells_.at(0x9a33);
        Require(pending.kind==Kind::truth && pending.data==0,"Meeting close still has a callback");
        return;
    }
    if(exchange_inventory_ && target==0x899f9 && pc_==0x8d1f8) {
        Require(argc==2 && args[0].kind==Kind::integer && args[0].data==0 &&
                args[1].kind==Kind::truth && args[1].data,"Invalid meeting farewell");
        // 899FB clears conversation marks, followed by the source farewell. The
        // existing room activity owners commit the contact count and 89787 timer.
        counted_contact_=friend_timer_requested_=true;
        vocabulary_.BeginConversation();
        if(skip_farewell_) { actor_command_=74; return; }
        target=0x7d548; args.clear();
    }
    if(exchange_inventory_ && target==0x8d932 && pc_==0x8e029) {
        Require(argc==1 && args[0].kind==Kind::truth && !args[0].data && callers_.empty() &&
                !meeting_event_depth_ && blocks_.empty(),"Unbalanced exchange cleanup");
        exchange_inventory_=nullptr; ordinary_=false; ordinary_return_=true;
        phase_=Phase::done; indirect_topic_=0;
        return;
    }
    if(friends_introduction_ && target==0x89787 && pc_==0x89a6d && !argc) {
        // The activity owner applies 89787's clock/RNG together with the ensuing 8D932(false).
        friend_timer_requested_=true;
        return;
    }
    if(friends_introduction_ && target==0x8d932 && pc_==0x8e029) {
        Require(argc==1 && args[0].kind==Kind::truth && !args[0].data && callers_.empty() &&
                friend_timer_requested_,"Unexpected friends introduction cleanup");
        friends_introduction_=ordinary_=false; ordinary_return_=true;
        phase_=Phase::done; indirect_topic_=0;
        return;
    }
    if(target==0xf30 && (excursion_ || (ordinary_ && actor_number_==4 && pc_==0x1c2e1))) {
        Require(argc==1 && args[0].kind==Kind::constant_ref,"Invalid KSC ordered word lookup");
        result_={Kind::integer,vocabulary_.FirstOutingMatch(args[0].data)};
        if(excursion_ && result_.data>=0) eligibility_words_[unsigned(locals_.at(7).data)]=unsigned(result_.data);
        return;
    }
    const auto& contact=contact_scripts[actor_number_];
    if(target==contact.enqueue_topic) {
        Require(argc==0 && (selected_fallback_.kind==Kind::code || selected_fallback_.kind==Kind::nil),
                "Invalid pending topic in CT2878");
        topic_enqueues_.push_back(selected_fallback_.kind==Kind::code
            ?std::optional<unsigned>(selected_fallback_.data):std::nullopt);
        return;
    }
    if(target==0x333b || (robo_greeting_ && target==0x3325) ||
       (alternate_greeting_ && target==alternate_greeting_->gift)) {
        // The inventory owner takes the actual giver and the gift RNG draw.
        Require(argc==1 && args[0].kind==Kind::string && args[0].text==nickname_,"Invalid KSC giver");
        phase_=Phase::gift; return;
    }
    if(target==0x393c && excursion_ && outing_returning_ && pc_==0x3a98) {
        phase_=Phase::outing_inventory; return;
    }
    const auto farewell=farewell_sites();
    if(ScheduledFarewell() && target==(source?alternate_greeting_->modal_wait:robo_greeting_?0x1c5c:0x1c5b) &&
       (pc_==farewell.game75+7 || pc_==farewell.game75+15)) {
        const bool first=pc_==farewell.game75+7;
        Require(!argc && farewell_stage_==(first?FarewellStage::requested:FarewellStage::written),
                "Invalid farewell callback order");
        farewell_stage_=first?FarewellStage::cover_wait:FarewellStage::slot_wait;
        phase_=Phase::scheduled_farewell;
        return;
    }
    if(ScheduledFarewell() && target==(source?source->utility_save-(0x67128-0x67101):robo_greeting_?0x6b220:0x8d35b) &&
       pc_==farewell.game75+11) {
        Require(!argc && farewell_stage_==FarewellStage::covered,"Invalid farewell save request");
        farewell_stage_=FarewellStage::writing;
        phase_=Phase::scheduled_farewell;
        return;
    }
    if(ScheduledFarewell() && target==(source?source->reset:robo_greeting_?0x6b38d:0x8d4cc) &&
       pc_==farewell.reset) {
        Require(argc==2 && args[0].kind==Kind::integer && !args[0].data &&
                args[1].kind==Kind::nil && farewell_stage_==FarewellStage::acknowledged,
                "Invalid farewell context reset");
        farewell_stage_=FarewellStage::reset;
        return;
    }
    if((target==0x1c5b || (robo_greeting_ && target==0x1c5c) ||
        (alternate_greeting_ && target==alternate_greeting_->modal_wait)) && !excursion_) {
        // The inventory modal owns this wait until its close completes.
        Require(gift_slot_>=0,"No active inventory slot");
        phase_=Phase::inventory; return;
    }
    if(scheduled_room_event_==3 && target==(source?source->diary_count:robo_greeting_?0xab55:0xa078) &&
       pc_==(source?0x1f38u:robo_greeting_?0x1f23u:0x1f22u)) {
        Require(!argc,"Invalid last-day diary count request");
        result_={Kind::integer,int(scheduled_progress_.diary_count)};
        return;
    }
    if(scheduled_room_event_==3 && target==(source?0x1ebcu:robo_greeting_?0x1ea7:0x1ea6) &&
       (pc_==(source?0x1f4eu:robo_greeting_?0x1f39u:0x1f38u) || (source && argc==0))) {
        Require(!argc && calendar_minute_,"Invalid last-day elapsed time request");
        // GAME20(3) is the adoption clock. 1EA6 subtracts its midnight,
        // divides the signed minute difference by 1440, and adds one.
        const int adoption=ScriptInteger(scheduled_progress_.adoption_minute);
        const auto elapsed=ScriptInteger(*calendar_minute_-unsigned(adoption-adoption%1440));
        result_={Kind::integer,elapsed/1440+1};
        return;
    }
    if(scheduled_room_event_==3 && target==0x178d && pc_==(source?0x1f68u:robo_greeting_?0x1f53u:0x1f52u)) {
        const unsigned predicate=source?source->event_cell+3:robo_greeting_?0x291e:0x286a;
        Require(argc==1 && args[0].kind==Kind::constant_ref && unsigned(args[0].data)==predicate,
                "Invalid last-day learned word count request");
        result_={Kind::integer,int(vocabulary_.CountMatches(predicate))};
        return;
    }
    if(alternate_greeting_ && target==alternate_greeting_->diary && pc_==alternate_greeting_->diary_resume) {
        Require(argc==1 && args[0].kind==Kind::integer && args[0].data==0x600000,
                "Invalid first-day actor diary request");
        first_day_diary_request_=true;
        return;
    }
    if(source && scheduled_room_event_>=2 && target==alternate_greeting_->diary) {
        const auto entry=source->callbacks.at(scheduled_room_event_-2);
        const auto offset=pc_-entry;
        const unsigned priority=scheduled_room_event_==3 && offset==0x6f ? 0x700000 :
                                scheduled_room_event_==5 && offset==0x57 ? 0x800000 :
                                scheduled_room_event_==6 && offset==0x16 ? 0x900000 : 0;
        Require(priority && argc==1 && args[0].kind==Kind::integer && unsigned(args[0].data)==priority,
                "Invalid named actor diary request");
        phase_=Phase::scheduled_diary;
        return;
    }
    if(target==0xa519 || (robo_greeting_ && target==0xaff7)) {
        if(scheduled_room_event_==3 && pc_==(robo_greeting_?0x67570u:0x89659u)) {
            Require(argc==1 && args[0].kind==Kind::integer && args[0].data==0x700000 &&
                    pool_.count(robo_greeting_?0x381:0x3ff),"Invalid farewell diary request");
            phase_=Phase::scheduled_diary;
            return;
        }
        if(scheduled_room_event_==5 && pc_==(robo_greeting_?0x675f9u:0x896e2u)) {
            Require(argc==1 && args[0].kind==Kind::integer && args[0].data==0x800000 &&
                    pool_.count(robo_greeting_?0x465:0x4ce) &&
                    pool_.count(robo_greeting_?0x4f1:0x545),"Invalid final extension diary request");
            phase_=Phase::scheduled_diary;
            return;
        }
        if(scheduled_room_event_==6 && pc_==(robo_greeting_?0x674f8u:0x895e1u)) {
            Require(argc==1 && args[0].kind==Kind::integer && args[0].data==0x900000 &&
                    pool_.count(robo_greeting_?0x305:0x379),"Invalid return diary request");
            phase_=Phase::scheduled_diary;
            return;
        }
        // The introductory KSC call at 8959A requests the first-day entry;
        // both the gift path and the no-slot branch join here.
        Require(argc==1 && args[0].data==0x600000,"Invalid follow-on schedule");
        if ((pc_==0x8959f || (robo_greeting_ && pc_==0x674b6)) && args[0].kind==Kind::integer)
            first_day_diary_request_=true;
        return;
    }
    if(target==0x11b0) {
        Require(argc==3 && args[0].kind==Kind::constant_ref &&
            (args[1].kind==Kind::constant_ref || args[1].kind==Kind::nil) &&
            args[2].kind==Kind::truth,"Invalid word selection");
        random_bound_=vocabulary_.Candidates(args[0].data,args[1].data,args[2].data!=0);
        if(random_bound_) { random_use_=RandomUse::word; phase_=Phase::random; return; }
        result_={Kind::integer,vocabulary_.FinishLookup(0)}; return;
    }
    // 0FDD compares the selected written form (row field 1) with each
    // string in the caller's exclusion list, returning on the first match.
    if(target==0x0fdd && argc==2 && (args[1].kind==Kind::constant_ref || args[1].kind==Kind::nil)) {
        Require(args[0].kind==Kind::integer,"Invalid word for spelling exclusion");
        if(args[1].kind==Kind::nil) { result_={Kind::truth,0}; return; }
        const auto& list=phrase_lists_.at(args[1].data);
        const auto spelling=vocabulary_.FieldText(args[0].data,1);
        const bool excluded=std::any_of(list.begin(),list.end(),[&](unsigned offset) {
            return pool_.at(offset)==spelling;
        });
        result_={Kind::truth,excluded}; return;
    }
    if(target==0x12e7) {
        Require(argc==2 && args[0].kind==Kind::integer && args[1].kind==Kind::integer,"Invalid word relation");
        vocabulary_.LinkWordGroup(args[0].data,args[1].data);
        return;
    }
    if(target==0x1011) {
        Require(argc==4 && args[0].kind==Kind::integer &&
            (args[1].kind==Kind::string || args[1].kind==Kind::nil) &&
            args[3].kind==Kind::truth,"Invalid word form lookup");
        const auto exclusion=args[2].kind==Kind::frame_ref ? Dereference(args[2]) : args[2];
        Require(exclusion.kind==Kind::nil || exclusion.kind==Kind::constant_ref || exclusion.kind==Kind::array,
                "Invalid word form exclusion list");
        int selected=args[0].data;
        if(selected>=0 && exclusion.kind==Kind::array) {
            const auto written=vocabulary_.FieldText(selected,1);
            // 0FDD compares the written form with each live list element.
            for(const auto& entry:*exclusion.cells) {
                if(entry.kind==Kind::string && entry.text==written) {
                    selected=-1;
                    break;
                }
            }
        }
        const unsigned fixed_exclusion=exclusion.kind==Kind::constant_ref ? unsigned(exclusion.data) : 0;
        auto form=vocabulary_.ResolveWord(selected,args[1].text,fixed_exclusion,args[3].data!=0);
        if(!form.field && args[1].kind==Kind::nil) { result_={}; return; }
        result_={Kind::string,0,std::move(form.text)};
        if(form.field) result_.word_operands.push_back({args[0].data,form.field,0,form.field_bytes});
        else result_.word_operands=std::move(args[1].word_operands);
        return;
    }
    if(target==contact.display_name) {
        Require(argc==1 && args[0].kind==Kind::integer,"Invalid displayed word row");
        auto form=vocabulary_.ResolveDisplayName(args[0].data);
        result_={Kind::string,0,std::move(form.text),0,{}, {{args[0].data,form.field,0,form.field_bytes}}};
        return;
    }
    const bool new_trait=target==(robo_greeting_?0xb4a7u:0xa9c5u);
    const bool repeated_trait=target==(robo_greeting_?0xb424u:0xa942u);
    if(new_trait || repeated_trait) {
        Require(argc==1 && args[0].kind==Kind::integer,"Missing word for trait selection");
        trait_row_=args[0].data;
        if(new_trait) {
            unasked_traits_=RoomWordTraits::Unasked(trait_row_,vocabulary_.FieldInt(trait_row_,12),
                                                    vocabulary_.FieldInt(trait_row_,15));
            if(unasked_traits_.empty()) { result_={Kind::integer,-1}; return; }
            random_use_=RandomUse::trait; random_bound_=unsigned(unasked_traits_.size());
        } else { random_use_=RandomUse::repeated_trait; random_bound_=22; }
        phase_=Phase::random; return;
    }
    if(target==(robo_greeting_?0xb384u:0xa8a2u)) {
        Require(argc==3 && args[0].kind==Kind::integer && args[1].kind==Kind::integer && args[2].kind==Kind::truth,
                "Invalid answer to word trait");
        vocabulary_.RecordTrait(args[0].data,args[1].data,args[2].data!=0);
        return;
    }
    if(target==0xfd1) { vocabulary_.BeginConversation(); return; }
    if(target==0x27ee) {
        Require(argc==2,"Expected two rumor output references");
        random_bound_=rumors_.candidates();
        if (!random_bound_) { SupplyRumor(args[0],args[1],0); return; }
        stack_.push_back(args[0]); stack_.push_back(args[1]);
        random_use_=RandomUse::rumor_pick; phase_=Phase::random;
        return;
    }
    if(target==0x2761) {
        Require(argc==2,"Expected two meeting rumor fields");
        if (rumors_.Remember(RumorPair(args[0],args[1]))) return;
        stack_.push_back(std::move(args[0])); stack_.push_back(std::move(args[1]));
        random_bound_=3; random_use_=RandomUse::rumor_replace; phase_=Phase::random;
        return;
    }
    // Older packs carry these two fixed introductory predicates without their reducer body.
    // Other projected lessons execute 92E2 on the actual dictionary criteria below.
    if(target==0x92e2 && argc==1 && args[0].kind==Kind::constant_ref &&
       (args[0].data==0xdd98 || args[0].data==0xddaa)) {
        teaching_={};
        teaching_[3]={Kind::integer,0x8000};
        teaching_[4]={Kind::integer,0x80};
        teaching_[7]={Kind::integer,args[0].data==0xdd98?0x10:0x20};
        return;
    }
    if(target==0x139b) {
        Require(reserved_word_==-1,"Nested word reservation");
        reserved_word_=vocabulary_.ReserveWord();
        result_={Kind::integer,reserved_word_}; return;
    }
    if(target==0xdbc) {
        Require(args.at(0).data==reserved_word_,"Wrong vocabulary reservation on release");
        vocabulary_.ReleaseWord(reserved_word_); reserved_word_=-1; return;
    }
    if(target==contact.keyboard) {
        Require(argc==3 && args[1].kind==Kind::string && args[2].kind==Kind::integer,
                "Invalid word keyboard arguments");
        if(args[2].data!=2 || pc_!=0x4cb7) {
            const bool question=friends_introduction_ && (pc_==0xcf85 || pc_==0xd176);
            Require(args[2].data==0 && args[1].text.empty() && (reserved_word_>=0 || question),
                    "Unsupported word keyboard owner");
        }
        auto destination=Dereference(args[0]);
        Require(destination.kind==Kind::array && destination.cells && destination.cells->size()==2,
                "Invalid word output pair");
        OpenWordKeyboard(args[2].data,std::move(args[1].text),std::move(destination));
        return;
    }
    if(target==0xf02) {
        Require(argc==1 && args[0].kind==Kind::string,"Missing lookup spelling");
        result_={Kind::integer,vocabulary_.FindWritten(args[0].text)}; return;
    }
    if(target==0x1488) {
        Require(argc==1 && args[0].kind==Kind::integer,"Invalid reading refresh row");
        vocabulary_.RefreshReading(args[0].data); return;
    }
    if(target==contact.initialize_word) {
        Require(argc==2,"Invalid word initializer arguments");
        const auto pair=Dereference(args.at(1));
        Require(pair.kind==Kind::array && pair.cells->size()==2 &&
                args[0].data==reserved_word_,"Invalid initial dictionary write");
        vocabulary_.InitializeReserved(reserved_word_,pair.cells->at(0).text,pair.cells->at(1).text);
        return;
    }
    if(target==0x154b) {
        Require(args.at(0).data==reserved_word_ && args.at(1).data==32,"Wrong final dictionary row");
        Require(vocabulary_.AcceptReserved(reserved_word_),"Lost dictionary row before acceptance");
        learned_row_=reserved_word_; ++learned_serial_;
        learned_written_=vocabulary_.FieldText(reserved_word_,1);
        learned_reading_=vocabulary_.FieldText(reserved_word_,2);
        reserved_word_=-1; return;
    }
    if(target==contact.accept_again) {
        Require(argc==3 && args[1].data==0 && args[2].data==32,"Wrong existing word update");
        const int row=args[0].data;
        vocabulary_.WriteNumber(row,0,vocabulary_.FieldInt(row,0)|32); return;
    }
    if(target==(robo_greeting_?0x67910u:0x899f9u) && direct_teaching_) {
        Require(argc==2 && args[1].kind==Kind::truth,"Invalid teaching farewell");
        counted_contact_=args[1].data!=0;
        vocabulary_.BeginConversation();
        if(skip_farewell_) { actor_command_=74; return; }
        target=robo_greeting_?0x5be76:0x7d548; args.clear();
    }
    if(ordinary_ && !code_.count(target)) {
        frontier_=target; phase_=Phase::frontier; return;
    }
    EnterProcedure(target,std::move(args),indirect,preserve_result);
}

void RoomTalkState::EnterProcedure(unsigned target,std::vector<Value> args,bool indirect,bool preserve_result) {
    callers_.push_back({pc_,std::move(locals_),indirect_topic_});
    if (indirect) indirect_topic_=target;
    pc_=target; locals_=std::move(args);
    if(!preserve_result) result_={};
}

int RoomTalkState::ApplyArrayRecords(bool filtered) {
    const auto array=[this](Value value) {
        if(value.kind!=Kind::array) value=Dereference(std::move(value));
        Require(value.kind==Kind::array && value.cells,"Expected an owned KSC table");
        return value.cells;
    };
    const auto actions=array(Pop());
    const auto conditions=filtered?array(Pop()):nullptr;
    const auto rows=array(Pop());
    Require(actions->size()%3==0 && (!conditions || conditions->size()%3==0),
            "KSC table records require triples");
    const auto numeric=[](const Value& v) {
        if(v.kind==Kind::fixed14)
            return int(recovered::BulkNumeric({0x40000000u | (unsigned(v.data)&0x0fffffffu)}));
        if(v.kind==Kind::array && v.cells) return int(v.cells->size());
        Require(v.kind==Kind::nil || v.kind==Kind::integer || v.kind==Kind::truth ||
                v.kind==Kind::calendar_time || v.kind==Kind::code,"Invalid KSC record number");
        return v.data;
    };
    const auto update=[&](std::vector<Value>& row,int index,int op,const Value& argument) {
        if(op==16) return;
        auto& cell=row.at(index);
        if(op==0) {
            cell=argument;
            return;
        }
        Require(op>=1 && op<=3,"Unsupported KSC table action");
        constexpr unsigned operations[]={0x66,0x64,0x68};
        cell={Kind::integer,IntegerCompound(operations[op-1],numeric(cell),numeric(argument))};
    };
    const auto matches=[&](const std::vector<Value>& row,std::size_t at) {
        const int index=numeric(conditions->at(at)),op=numeric(conditions->at(at+1));
        if(op==16) return true;
        const auto& left=row.at(index);
        const auto& right=conditions->at(at+2);
        if(op==0 || op==1) {
            const bool eq=left.kind==right.kind && left.data==right.data &&
                (left.kind!=Kind::string || std::string_view(left.text.c_str())==std::string_view(right.text.c_str()));
            // Original equality rejects array headers and references.
            const bool comparable=left.kind==Kind::nil || left.kind==Kind::integer || left.kind==Kind::truth ||
                left.kind==Kind::calendar_time || left.kind==Kind::code || left.kind==Kind::fixed14 ||
                left.kind==Kind::string;
            return (comparable && eq)==(op==0);
        }
        const int a=numeric(left),b=numeric(right);
        switch(op) {
        case 2: return a>b;
        case 3: return a>=b;
        case 4: return a<b;
        case 5: return a<=b;
        case 6: return (a&b)==b;
        case 7: return (a&b)==0;
        case 8: return (a&b)!=0;
        default: throw std::runtime_error("Unsupported KSC table condition");
        }
    };
    if(!filtered) {
        // 7773C applies each action to all rows before starting the next action.
        for(std::size_t at=0;at<actions->size();at+=3) {
            const int index=numeric(actions->at(at)),op=numeric(actions->at(at+1));
            if(op==16) continue;
            for(const auto& target:*rows)
                update(*array(target),index,op,actions->at(at+2));
        }
        return 0;
    }
    // 7A64C filters and applies all actions to one row before reading the next.
    int count=0;
    for(const auto& target:*rows) {
        const auto row=array(target);
        if(conditions->empty()) continue;
        std::size_t at=0;
        while(at<conditions->size() && matches(*row,at)) at+=3;
        if(at<conditions->size()) continue;
        for(at=0;at<actions->size();at+=3)
            update(*row,numeric(actions->at(at)),numeric(actions->at(at+1)),actions->at(at+2));
        ++count;
    }
    return count;
}

void RoomTalkState::InitializeArray(unsigned opcode) {
    const unsigned index=Read(2);
    const auto count=Pop();
    std::vector<Value>* cells=nullptr;
    std::size_t begin=0,capacity=0;
    if(opcode==0x72) {
        auto& script_locals=ActiveScriptLocals();
        const auto& row=script_locals.at(index);
        Require(row.kind==Kind::script_local_array,"Expected a KSC script-local array");
        cells=&script_locals;
        begin=row.depth+1;
        capacity=unsigned(row.data);
    } else if(opcode==0x73) {
        cells=&global_arrays_.at(index);
        capacity=cells->size();
    } else {
        if(opcode==0x75)
            Require(friends_introduction_ && (index==0x2a3c || index==0x2a44),
                    "Unknown KSC context table array");
        const auto& row=opcode==0x71 ? locals_.at(index) : friends_cells_.at(index);
        Require(row.kind==Kind::array && row.cells,"Expected an owned KSC array");
        cells=row.cells.get();
        capacity=cells->size();
    }
    Require(count.kind==Kind::integer && count.data>0 &&
            unsigned(count.data)<=capacity && unsigned(count.data)<=stack_.size(),
            "Invalid KSC array initializer");
    // 6B554 consumes stack arguments in reverse and overwrites the leading cells.
    for(unsigned i=unsigned(count.data);i>0;--i) cells->at(begin+i-1)=Pop();
}

void RoomTalkState::RememberProcedure(unsigned table,unsigned index,unsigned target) {
    const auto remember=[target](std::vector<Value>& cells,std::size_t first,std::size_t count,
                                 std::size_t& cursor) {
        // BOOT 77354 stores the code value at the current index, then
        // advances the index modulo the positive signed 14-bit length.
        Require(count && count<0x2000 && cursor<count && first<=cells.size() &&
                count<=cells.size()-first,"Invalid KSC procedure ring");
        cells[first+cursor]={Kind::code,int(target)};
        cursor=(cursor+1)%count;
    };
    switch(table) {
    case 1:
    case 4: {
        auto& row=table==1?locals_.at(index):friends_cells_.at(index);
        Require(row.kind==Kind::array && row.cells,"Expected an owned KSC procedure ring");
        remember(*row.cells,0,row.cells->size(),row.procedure_cursor);
        break;
    }
    case 2: {
        auto& script_locals=ActiveScriptLocals();
        auto& row=script_locals.at(index);
        Require(row.kind==Kind::script_local_array && row.depth==index && row.data>0,
                "Invalid KSC script-local procedure ring");
        remember(script_locals,index+1,unsigned(row.data),row.procedure_cursor);
        break;
    }
    case 3: {
        auto& row=global_arrays_.at(index);
        remember(row,0,row.size(),global_procedure_cursors_[index]);
        break;
    }
    default:
        throw std::runtime_error("Unsupported KSC procedure ring owner");
    }
}

int RoomTalkState::HostNumber(const Value& value) {
    switch(value.kind) {
    case Kind::nil: return 0;
    case Kind::truth: case Kind::integer: case Kind::calendar_time: case Kind::code:
        return ScriptInteger(unsigned(value.data));
    case Kind::fixed14: {
        const auto high=ScriptInteger(ArithmeticShiftRight(value.data,14));
        const auto low=int((unsigned(value.data)&0x3fffu)^0x2000u)-0x2000;
        return int(std::sqrt(float(high*high+low*low)));
    }
    case Kind::string: {
        // C1E0C is a base-ten conversion with a signed 32-bit result.
        const auto number=std::strtoll(value.text.c_str(),nullptr,10);
        return int(std::clamp(number,static_cast<long long>(std::numeric_limits<std::int32_t>::min()),
                                     static_cast<long long>(std::numeric_limits<std::int32_t>::max())));
    }
    case Kind::array:
        Require(bool(value.cells),"Missing KSC array for host conversion");
        return int(value.cells->size()&0xfffu);
    case Kind::script_local_array:
        return value.data&0xfff;
    default:
        throw std::runtime_error("Unsupported KSC host selector type");
    }
}

RoomTalkState::ScriptHostValue RoomTalkState::HostValue(const Value& value) {
    using Tag=ScriptHostValue::Tag;
    switch(value.kind) {
    case Kind::nil: return {Tag::nil};
    case Kind::truth: return {Tag::truth,value.data};
    case Kind::integer: return {Tag::integer,value.data};
    case Kind::calendar_time: return {Tag::calendar_time,value.data};
    case Kind::fixed14: return {Tag::vector,value.data};
    case Kind::code: return {Tag::code,value.data};
    case Kind::string: return {Tag::string,0,value.text};
    default: throw std::runtime_error("Unsupported KSC host query argument");
    }
}

void RoomTalkState::InvokeHostBuiltin(HostBuiltin operation,unsigned argc) {
    Require(argc<=stack_.size(),"Insufficient KSC host arguments");
    const auto first=stack_.size()-argc;
    result_={};
    // An absent callback still consumes the operands and returns nil.
    switch(operation) {
    case HostBuiltin::command:
        if(script_host_.command) {
            Require(argc>=1,"Missing KSC host command selector");
            const auto selector=HostNumber(stack_[first]);
            stack_.resize(first);
            result_={Kind::integer,ScriptInteger(unsigned(script_host_.command(selector,-1)))};
            return;
        }
        break;
    case HostBuiltin::query:
        if(script_host_.query) {
            Require(argc>=3,"Missing KSC host query operands");
            const auto selector=HostNumber(stack_[first]);
            const auto a=HostValue(stack_[first+1]),b=HostValue(stack_[first+2]);
            stack_.resize(first);
            result_={Kind::integer,ScriptInteger(unsigned(script_host_.query(selector,a,b)))};
            return;
        }
        break;
    case HostBuiltin::notify:
        if(script_host_.notify) {
            Require(argc>=2,"Missing KSC host notification operands");
            const auto selector=HostNumber(stack_[first]),value=HostNumber(stack_[first+1]);
            stack_.resize(first);
            script_host_.notify(selector,value);
            return;
        }
        break;
    }
    stack_.resize(first);
}

void RoomTalkState::LoadScriptModule(unsigned argc) {
    Require(argc!=0 && argc<=stack_.size(),"Missing KSC module name");
    const auto first=stack_.size()-argc;
    auto argument=std::move(stack_[first]);
    stack_.resize(first);
    result_={Kind::integer,-1};
    // The eight entries precede the module count at E58A8. The script receives
    // the previous count only after a successful open and registration.
    constexpr unsigned module_slots=8;
    if(!script_module_reader_ || script_modules_.size()>=module_slots) return;
    std::string name;
    if(argument.kind==Kind::string) name=argument.text.c_str();
    else if(argument.kind==Kind::integer) name=std::to_string(ScriptInteger(unsigned(argument.data)));
    if(name.empty()) return;
    std::vector<std::uint8_t> bytes;
    if(!script_module_reader_(name,bytes)) return;
    if(bytes.size()>script::Program::resource_limit) return;
    const auto index=int(script_modules_.size());
    // 6A854 retains the raw member. The consuming builtin chooses its format.
    script_modules_.push_back({std::move(name),std::move(bytes),{}});
    result_={Kind::integer,index};
}

void RoomTalkState::RegisterScriptMap(unsigned argc) {
    Require(argc!=0 && argc<=stack_.size(),"Missing KSC map module index");
    const auto first=stack_.size()-argc;
    const auto index=HostNumber(stack_[first]);
    stack_.resize(first);
    result_={Kind::integer,-1};
    // E58AC contains eight map slots before the count at E58CC.
    if(index<0 || std::size_t(index)>=script_modules_.size() || script_maps_.size()>=8) return;
    const auto& bytes=script_modules_[index].bytes;
    if(bytes.size()<12 || bytes[0]!='k' || bytes[1]!='s' || bytes[2]!='m') return;
    auto offset=std::size_t{8};
    const auto name_count=Word(bytes.data()+4,4);
    if(name_count>bytes.size()-offset) return;
    ScriptMap map;
    for(unsigned i=0;i<name_count;++i) {
        const auto start=bytes.begin()+offset;
        const auto end=std::find(start,bytes.end(),std::uint8_t{0});
        if(end==bytes.end()) return;
        map.names.emplace_back(start,end);
        offset=std::size_t(end-bytes.begin())+1;
    }
    // 65ECC advances to the next aligned position before reading 8-byte rows.
    offset=(offset+3)&~std::size_t{3};
    if(offset>bytes.size() || bytes.size()-offset<4) return;
    const auto record_count=Word(bytes.data()+offset,4);
    offset+=4;
    if(record_count>(bytes.size()-offset)/8) return;
    map.records.reserve(record_count);
    for(unsigned i=0;i<record_count;++i,offset+=8)
        map.records.push_back({Word(bytes.data()+offset,4),Word(bytes.data()+offset+4,4)});
    const auto handle=int(script_maps_.size());
    script_maps_.push_back(std::move(map));
    result_={Kind::integer,handle};
}

void RoomTalkState::RegisterScriptContext(unsigned argc) {
    Require(argc>=2 && argc<=stack_.size(),"Missing KSC context registration operands");
    const auto first=stack_.size()-argc;
    const auto module_index=HostNumber(stack_[first]);
    const auto map_index=stack_[first+1].kind==Kind::nil
        ? std::optional<int>{} : std::optional<int>{HostNumber(stack_[first+1])};
    stack_.resize(first);
    result_={Kind::integer,-1};
    // 66FDC prepares the image and local table; 67F1C starts execution later.
    // E58FC..E591B holds eight context pointers, separate from module and map indices.
    if(module_index<0 || std::size_t(module_index)>=script_modules_.size() ||
       (map_index && (*map_index<0 || std::size_t(*map_index)>=script_maps_.size())) ||
       registered_scripts_.size()>=8) return;
    auto& module=script_modules_[module_index];
    if(!module.program) {
        auto program=std::make_shared<script::Program>();
        script::Diagnostic error;
        if(!program->load_module_bytes(module.bytes,module.name,error)) return;
        module.program=std::move(program);
    }
    ChildScript context;
    context.program=module.program;
    if(map_index) context.map_index=unsigned(*map_index);
    const auto& program=*context.program;
    const auto count=program.header().local_count;
    context.script_locals.reserve(count);
    for(unsigned index=0;index<count;++index) {
        const auto word=program.initial_word(script::Table::Local,std::uint16_t(index));
        const auto payload=word&0x0fffffffu;
        switch(word>>28) {
        case 0: context.script_locals.push_back({}); break;
        case 1: context.script_locals.push_back({Kind::truth,payload!=0}); break;
        case 2: context.script_locals.push_back({Kind::integer,ScriptInteger(word)}); break;
        case 3: context.script_locals.push_back({Kind::calendar_time,ScriptInteger(word)}); break;
        case 4: context.script_locals.push_back({Kind::fixed14,ScriptInteger(word)}); break;
        case 5: context.script_locals.push_back({Kind::string,0,program.pool_bytes(payload)}); break;
        case 7: context.script_locals.push_back({Kind::code,int(payload)}); break;
        case 9:
            if((payload&0xfffu)>=count-index) return;
            context.script_locals.push_back({Kind::script_local_array,int(payload&0xfffu),{},index});
            break;
        default: return;
        }
    }
    const auto handle=int(registered_scripts_.size());
    context.registered_root=unsigned(handle);
    context.generation=++child_generation_;
    registered_scripts_.push_back(std::move(context));
    result_={Kind::integer,handle};
}

void RoomTalkState::ActivateScriptContext(unsigned argc) {
    Require(argc!=0 && argc<=stack_.size(),"Missing KSC activation operands");
    const auto first=stack_.end()-argc;
    const auto index=first->kind==Kind::context?first->data:HostNumber(*first);
    const auto args=std::next(first);
    const auto transferred=std::none_of(args,stack_.end(),[](const Value& value) {
        return value.kind==Kind::frame_ref || value.kind==Kind::script_local_ref ||
               value.kind==Kind::script_local_array;
    });
    result_={Kind::integer,-1};
    if(index>=0 && std::size_t(index)<registered_scripts_.size() && transferred) {
        auto& target=registered_scripts_[index];
        // 67F5C copies the ordered arguments. 67F1C resets the cursor and clock
        // and enables the context while preserving its loaded local table.
        if(&target!=executing_child_) {
            std::vector<Value> locals(args,stack_.end());
            target.locals=std::move(locals);
            target.stack.clear();
            target.calls.clear();
            target.blocks.clear();
            target.control_resumes.clear();
            target.pc=target.program->header().entrypoint;
            target.timer_base=script_ticks_;
            target.deadline=0;
            target.delay=0;
            target.instruction_cost=1;
            target.indirect_topic=0;
            target.generation=++child_generation_;
            target.phase=Phase::running;
            result_={Kind::integer,0};
        }
    }
    stack_.erase(first,stack_.end());
}

void RoomTalkState::RendezvousScriptContexts(unsigned argc) {
    Require(argc<=stack_.size(),"Insufficient KSC rendezvous operands");
    const auto first=stack_.end()-argc;
    std::vector<ChildScript*> targets;
    targets.reserve(argc);
    for(auto operand=first;operand!=stack_.end();++operand) {
        const int handle=operand->kind==Kind::context?operand->data:HostNumber(*operand);
        Require(handle>=0 && std::size_t(handle)<registered_scripts_.size(),
                "Unknown KSC rendezvous context");
        targets.push_back(&registered_scripts_[handle]);
    }
    // 69BA0 increments every supplied context, including repeated handles.
    // Only a transition to zero moves that context's deadline to the next tick.
    const auto next_tick=script_ticks_+1;
    for(auto* target:targets) {
        auto& credit=target==executing_child_?rendezvous_credit_:target->rendezvous_credit;
        auto& deadline=target==executing_child_?script_deadline_:target->deadline;
        if(++credit==0) deadline=next_tick;
    }
    stack_.erase(first,stack_.end());
    result_={};
    rendezvous_credit_=std::uint16_t(rendezvous_credit_-argc);
    script_deadline_=next_tick;
    phase_=Phase::rendezvous;
}

void RoomTalkState::EnterProtectedHandler(unsigned flags) {
    Require(!blocks_.empty(),"No KSC protected block to transfer");
    const auto block=blocks_.back();
    Require(block.depth==callers_.size() && block.base<=stack_.size() && HasCodeAt(block.close),
            "Unsupported KSC protected transfer");
    // 6D060 releases operands above the protected block. 6D2AC..6D320 save
    // the requested state, prior stack cursor and next PC in the handler frame.
    control_resumes_.push_back({block,pc_,flags,stack_.size(),blocks_.size()-1,{}});
    blocks_.pop_back();
    stack_.resize(block.base);
    pc_=block.close;
}

void RoomTalkState::ResumeProtectedHandler() {
    Require(!control_resumes_.empty(),"No KSC protected continuation");
    auto resume=control_resumes_.back();
    Require(resume.outer_blocks==blocks_.size() && resume.block.base==stack_.size() &&
            resume.block.depth==callers_.size() && HasCodeAt(resume.pc),
            "Unbalanced KSC protected continuation");
    // 6C6FC reads the saved cursor and state. Raising the operand cursor
    // exposes nil in slots released by the transfer, never the old values.
    const auto state=(resume.flags>>24)&15u;
    const bool enabled=resume.run_override.value_or((resume.flags&0x00800000u)!=0);
    Require(state==0 || state==1 || state==2 || state==3 || state==8,
            "Unsupported KSC protected state");
    stack_.resize(resume.operands);
    pc_=resume.pc;
    resume.block.restored=true;
    blocks_.push_back(resume.block);
    control_resumes_.pop_back();
    if(state==0) phase_=Phase::done;
    else if(!enabled || state==1 || state==8) phase_=Phase::suspended;
    else phase_=state==3?Phase::rendezvous:Phase::running;
}

const script::Program* RoomTalkState::ActiveModule() const {
    return executing_child_?executing_child_->program.get():nullptr;
}

const std::vector<RoomTalkState::Value>& RoomTalkState::ActiveScriptLocals() const {
    return executing_child_ && executing_child_->registered_root
        ? registered_scripts_.at(*executing_child_->registered_root).script_locals : script_locals_;
}

std::vector<RoomTalkState::Value>& RoomTalkState::ActiveScriptLocals() {
    return const_cast<std::vector<Value>&>(std::as_const(*this).ActiveScriptLocals());
}

namespace {
constexpr unsigned script_instruction_limit=16384;

bool IsChildOpcode(unsigned op) {
    // Data operations can share the script image and actor-local storage. Modal
    // services need a separate host owner and cannot use the root's state.
    switch(op) {
    case 0x0b: case 0x0c: case 0x0f: case 0x10: case 0x11: case 0x13:
    case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c:
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x28: case 0x29: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
    case 0x30: case 0x31: case 0x38: case 0x3a: case 0x3b:
    case 0x40: case 0x41: case 0x43: case 0x44:
    case 0x4e: case 0x4f: case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57: case 0x70: case 0x71:
    case 0x72: case 0x73: case 0x78: case 0x89: case 0x8a: case 0x8b: case 0x8c:
    case 0x8d: case 0x8f: case 0x90: case 0x91: case 0x92: case 0x93:
    case 0x94: case 0x95: case 0xa0: case 0xd0: case 0xd1: case 0xd2:
    case 0xd4: case 0xd5: case 0xd6: case 0xd7: case 0xe0: case 0xe1:
    case 0xe2: case 0xe3: case 0xf4: case 0xf5: case 0xfc: case 0xfd:
        return true;
    default:
        return (op>=0x5a && op<=0x6d && op!=0x65 && op!=0x67) ||
               (op>=0x80 && op<=0x88) || (op>=0xb0 && op<=0xb4) ||
               (op>=0xb8 && op<=0xbd) || (op>=0xc0 && op<=0xc5);
    }
}
}

void RoomTalkState::ChildScript::SwapExecution(RoomTalkState& vm) {
    using std::swap;
    swap(phase,vm.phase_);
    swap(pc,vm.pc_);
    swap(timer_base,vm.script_timer_base_);
    swap(deadline,vm.script_deadline_);
    swap(rendezvous_credit,vm.rendezvous_credit_);
    swap(indirect_topic,vm.indirect_topic_);
    swap(delay,vm.delay_);
    swap(instruction_cost,vm.instruction_cost_);
    swap(stack,vm.stack_);
    swap(locals,vm.locals_);
    swap(calls,vm.callers_);
    swap(blocks,vm.blocks_);
    swap(control_resumes,vm.control_resumes_);
    swap(result,vm.result_);
}

void RoomTalkState::StartChildScript(unsigned target,unsigned argc) {
    Require(HasCodeAt(target),"Missing child KSC entry");
    Require(stack_.size()>=argc,"Insufficient child KSC arguments");
    const auto first=stack_.end()-argc;
    Require(std::none_of(first,stack_.end(),[](const Value& value) {
        return value.kind==Kind::frame_ref;
    }),"Cannot pass a frame reference to a child KSC context");
    std::vector<Value> args(first,stack_.end());
    auto& children=executing_child_?executing_child_->children:root_children_;
    const auto inactive=std::find_if(children.begin(),children.end(),[this](std::size_t index) {
        return child_scripts_[index].phase==Phase::done;
    });
    const auto index=inactive==children.end()?child_scripts_.size():*inactive;
    if(index==child_scripts_.size()) {
        child_scripts_.emplace_back();
        children.push_back(index);
    }
    auto& slot=child_scripts_[index];
    // 680A4 takes the first inactive child. 680D0 gives the activation
    // its own PC, argument frame and clock base without running the caller's frame.
    ChildScript fresh;
    fresh.generation=++child_generation_;
    fresh.pc=target;
    fresh.timer_base=script_ticks_;
    fresh.locals=std::move(args);
    fresh.phase=Phase::running;
    if(executing_child_) {
        fresh.program=executing_child_->program;
        fresh.registered_root=executing_child_->registered_root;
    }
    fresh.children.swap(slot.children);
    slot=std::move(fresh);
    stack_.erase(first,stack_.end());
}

void RoomTalkState::StepScript(ChildScript& child,recovered::RngNativeOwner* rng) {
    if(child.phase!=Phase::running && child.phase!=Phase::rendezvous) return;
    child.SwapExecution(*this);
    executing_child_=&child;
    try {
        AdvanceScript(rng);
    } catch(const std::exception& e) {
        child.SwapExecution(*this);
        executing_child_=nullptr;
        throw std::runtime_error("Child KSC PC "+std::to_string(child.pc)+": "+e.what());
    }
    child.SwapExecution(*this);
    executing_child_=nullptr;
}

void RoomTalkState::StepScriptTree(const std::vector<std::size_t>& roots,recovered::RngNativeOwner* rng) {
    if(roots.empty()) return;
    std::vector<const std::vector<std::size_t>*> pending{&roots};
    while(!pending.empty()) {
        const auto& siblings=*pending.back();
        pending.pop_back();
        // 74CF8 advances the next sibling before it descends into the current
        // context's first child. Queue descendants for the corresponding unwind.
        for(const auto index:siblings) {
            auto& child=child_scripts_[index];
            StepScript(child,rng);
            if(!child.children.empty()) pending.push_back(&child.children);
        }
    }
}

void RoomTalkState::StepRegisteredScripts(recovered::RngNativeOwner* rng) {
    // 66320 links newly registered roots at the head of the list that 74DFC
    // walks. Contexts created during a slice join the next walk.
    for(auto i=registered_scripts_.size();i>0;--i) {
        auto& context=registered_scripts_[i-1];
        StepScript(context,rng);
        StepScriptTree(context.children,rng);
    }
}

void RoomTalkState::StepChildren(recovered::RngNativeOwner* rng) {
    if(phase_==Phase::failed || phase_==Phase::unsupported) return;
    StepScriptTree(root_children_,rng);
}

void RoomTalkState::Execute(recovered::RngNativeOwner* rng) {
    const auto* named=alternate_greeting_? &alternate_greeting_->schedule : nullptr;
    for(unsigned budget=0;budget<script_instruction_limit;++budget) {
        if(executing_child_) {
            Require(child_budget_!=0,"Child KSC instruction budget exhausted");
            --child_budget_;
        }
        if(ordinary_ && !executing_child_ && !HasCodeAt(pc_)) {
            frontier_=pc_; phase_=Phase::frontier; return;
        }
        const auto site=pc_, op=Read(1);
        if(executing_child_) Require(IsChildOpcode(op),"Unsupported child KSC operation");
        if(excursion_ && !executing_child_) {
            switch(site) {
            case 0x13090: case 0x130e0: case 0x130c9: case 0x130d6: case 0x130e9:
            case 0x1269f: case 0x1c57: case 0x1c59: case 0x1c5a: case 0x1c64: case 0x1c69: case 0x126ad: case 0x13411: case 0x13218:
                outing_sites_.push_back(site); break;
            }
            if(site==0x133c8 || site==0x13015) {
                const auto& d=*locals_.at(site==0x13015?2:13).cells;
                const auto& r=*d.at(1).cells;
                Require(d.at(0).kind==Kind::integer && d.at(2).kind==Kind::integer &&
                        d.at(3).kind==Kind::string && d.at(4).kind==Kind::string,"Incomplete outing descriptor");
                const auto id=unsigned(d[0].data),word=unsigned(d[2].data);
                outing_snapshot_=RoomOutingDescriptor{id,unsigned(r[0].data),word,site==0x13015?-1:int(eligibility_words_.at(id)),
                    unsigned(r[1].data),unsigned(r[2].data),std::string(vocabulary_.FieldText(word,1)),
                    r[4].text,d[3].text,d[4].text};
            }
        }
        switch(op) {
        case 0x0f:
            phase_=Phase::suspended; // 6B9AC sets the context to state 1.
            return;
        case 0x38:
            phase_=Phase::done; // 6E41C exits the context without a procedure return.
            return;
        case 0x18:
            blocks_.push_back({Read(3),callers_.size(),stack_.size()}); break;
        case 0x19:
            ResumeProtectedHandler();
            return;
        case 0x1a:
            Require(!blocks_.empty() && (blocks_.back().restored || blocks_.back().close==site ||
                    (scheduled_room_event_ && blocks_.back().close==(alternate_greeting_?0x1a8du:robo_greeting_?0x1a78u:0x1a77u) &&
                     site==(alternate_greeting_?0x1a95u:robo_greeting_?0x1a80u:0x1a7fu)) ||
                    (exchange_inventory_ && blocks_.back().close==0x8d19e && site==0x8d1f0)) &&
                    blocks_.back().depth==callers_.size() &&
                    blocks_.back().base==stack_.size(),"Unbalanced outing protected block");
            blocks_.pop_back(); break;
        case 0x1b: {
            if(!executing_child_ && ScheduledFarewell() && site==farewell_sites().reset) {
                Require(farewell_stage_==FarewellStage::reset && !callers_.empty() &&
                        callers_.back().resume==FarewellReturnPc(),"Invalid farewell script unwind");
                callers_.clear();
                blocks_.clear();
                control_resumes_.clear();
                stack_.clear();
                locals_.clear();
                ordinary_=false;
                scheduled_room_event_=0;
                farewell_stage_=FarewellStage::complete;
                phase_=Phase::done;
                return;
            }
            if(executing_child_ || !exchange_inventory_ || site!=0x8d027) {
                EnterProtectedHandler(0x08000000u);
                break;
            }
            Require(!blocks_.empty(),"Unexpected script unwind");
            const auto& handler=blocks_.back();
            Require(handler.close==0x8d19e && callers_.size()==handler.depth+1 &&
                    handler.base<=stack_.size(),"Invalid meeting cancellation frame");
            auto parent=std::move(callers_.back()); callers_.pop_back();
            locals_=std::move(parent.locals); indirect_topic_=parent.indirect_topic;
            stack_.resize(handler.base); pc_=handler.close;
            break;
        }
        case 0x0d:
            Require(exchange_inventory_ && site==0x8d19e && !meeting_event_depth_,"Unexpected event lock");
            ++meeting_event_depth_; break;
        case 0x0e:
            Require(exchange_inventory_ && site==0x8d1ac && meeting_event_depth_==1,"Unbalanced event lock");
            --meeting_event_depth_; break;
        case 0x0b:
            stack_.push_back({Kind::integer,instruction_cost_}); instruction_cost_=0; break;
        case 0x0c: {
            const auto cost=Pop();
            Require(cost.kind==Kind::integer,"Invalid saved instruction cost");
            instruction_cost_=cost.data; break;
        }
        case 0x3a:
            if(!control_resumes_.empty()) {
                control_resumes_.back().run_override=false;
                break;
            }
            Require(outing_callback_ && site==0x1c59,"Invalid outing callback context");
            outing_callback_->run_override=false; break;
        case 0x3b:
            if(!control_resumes_.empty()) {
                control_resumes_.back().run_override=true;
                break;
            }
            Require(outing_callback_.has_value(),"No active KSC callback to re-enable its caller");
            // 6E44C forces the effective run flag on when the caller is restored.
            outing_callback_->run_override=true; break;
        case 0x1c:
        case 0x32: {
            if(op==0x1c && !blocks_.empty()) {
                const auto flags=Pop();
                Require(flags.kind==Kind::integer,"Invalid KSC protected state flags");
                EnterProtectedHandler(unsigned(flags.data));
                script_deadline_=0;
                break;
            }
            // 1C joins 32 at 6D95C when there is no active exception
            // block. Its return discards the callback's procedure frames.
            Require(outing_callback_ && blocks_.empty() && (op==0x1c || callers_.empty()),
                    "No supported KSC callback continuation to restore");
            auto saved=std::move(*outing_callback_);
            outing_callback_.reset();
            pc_=saved.resume; stack_=std::move(saved.stack); callers_=std::move(saved.calls);
            locals_=std::move(saved.locals); blocks_=std::move(saved.blocks); result_=std::move(saved.result);
            control_resumes_=std::move(saved.control_resumes);
            delay_=saved.delay;
            phase_=saved.run_override.value_or(true)?Phase::running:Phase::outing_scene;
            return;
        }
        case 0x8c:
            if(executing_child_ && executing_child_->registered_root &&
               executing_child_==&registered_scripts_.at(*executing_child_->registered_root))
                stack_.push_back({Kind::context,int(*executing_child_->registered_root)});
            else {
                Require(!executing_child_ && excursion_ && site==0x1c5d,"Unsupported callback owner");
                stack_.push_back({Kind::context,-1});
            }
            break;
        case 0x30: locals_.resize(Read(1)); break;
        case 0x4e: {
            const unsigned index=Read(2),length=Read(2);
            Require(index+length<locals_.size(),"Invalid local KSC array");
            locals_.at(index)={Kind::array,0,{},0,std::make_shared<std::vector<Value>>(length)};
            break;
        }
        case 0x71: case 0x72: case 0x73: case 0x75: InitializeArray(op); break;
        case 0x4f: {
            auto ref=Pop();
            // The BOOT handler leaves the destination reference in the
            // result register after clearing the cell it addresses.
            Require(ref.kind==Kind::frame_ref || ref.kind==Kind::element || ref.kind==Kind::script_local_ref ||
                    ref.kind==Kind::association_ref,"Expected an owned KSC reference to clear");
            Assign(ref,{});
            result_=std::move(ref);
            break;
        }
        case 0xa0: {
            // The selector chooses a signed 28-bit or 14-bit limit in 71764.
            constexpr int limits[]={-0x08000000,0x07ffffff,-8192,8191};
            const auto index=Read(1);
            stack_.push_back({Kind::integer,index<4?limits[index]:0});
            break;
        }
        case 0x80: stack_.push_back({Kind::integer,static_cast<std::int8_t>(Read(1))}); break;
        case 0x81: stack_.push_back({Kind::integer,static_cast<std::int16_t>(Read(2))}); break;
        case 0x82: stack_.push_back({Kind::integer,static_cast<int>(Read(3))}); break;
        case 0x83: stack_.push_back({Kind::integer,ScriptInteger(Read(4))}); break;
        case 0x84: stack_.push_back({}); break;
        case 0x85: stack_.push_back({Kind::integer,0}); break;
        case 0x86: stack_.push_back({Kind::truth,0}); break;
        case 0x87: stack_.push_back({Kind::truth,1}); break;
        case 0x89: stack_.push_back(std::move(result_)); result_={}; break;
        case 0x8d: stack_.push_back({Kind::integer,ScriptInteger(script_ticks_-script_timer_base_)}); break;
        case 0x88: {
            const auto offset=Read(3);
            const auto* module=ActiveModule();
            stack_.push_back({Kind::string,0,module?module->pool_bytes(offset):pool_.at(offset)});
            break;
        }
        case 0x8e: stack_.push_back(ReadText()); break;
        case 0x8f: {
            // 77224 allocates a fresh associative table and moves its handle
            // to the operand stack, clearing the result register.
            Value table{Kind::association};
            table.entries=std::make_shared<std::map<std::string,Value>>();
            stack_.push_back(std::move(table));
            result_={};
            break;
        }
        case 0xfc: case 0xfd: {
            auto key=Pop(),table=Pop();
            Require(key.kind==Kind::string && table.kind==Kind::association && table.entries,
                    "Invalid KSC associative table access");
            const std::string name(key.text.c_str());
            Require(name.size()<255,"KSC table key exceeds the terminated source buffer");
            if(op==0xfc) {
                const auto it=table.entries->find(name);
                stack_.push_back(it==table.entries->end()?Value{}:it->second);
            } else {
                // 76F50 clears the slot on both insertion and replacement.
                table.entries->insert_or_assign(name,Value{});
                Value ref{Kind::association_ref,0,name};
                ref.entries=std::move(table.entries);
                stack_.push_back(std::move(ref));
            }
            result_={};
            break;
        }
        case 0x90: stack_.push_back(locals_.at(Read(2))); break;
        case 0x91: {
            Value ref{Kind::frame_ref,static_cast<int>(Read(2)),{},callers_.size()};
            ref.frame_owner=FrameOwner();
            stack_.push_back(std::move(ref));
            break;
        }
        case 0x92:
            stack_.push_back(ActiveScriptLocals().at(Read(2)));
            break;
        case 0x93: {
            const unsigned index=Read(2);
            Require(index<ActiveScriptLocals().size(),"Invalid KSC local reference");
            stack_.push_back({Kind::script_local_ref,int(index)});
            break;
        }
        case 0x96: case 0x97: {
            const unsigned index=Read(2);
            const auto owner=Pop();
            Require(owner.kind==Kind::context && index<script_locals_.size(),
                    "Unsupported foreign KSC context local access");
            if(op==0x96) stack_.push_back(script_locals_.at(index));
            else stack_.push_back({Kind::script_local_ref,int(index)});
            break;
        }
        case 0x9a: {
            const auto owner=Pop();
            Require(owner.kind==Kind::context,"Unsupported foreign KSC timer owner");
            stack_.push_back({Kind::integer,ScriptInteger(script_ticks_-script_timer_base_)});
            break;
        }
        case 0x94: case 0x95: {
            const int id=Read(2);
            Require(!ActiveModule(),"Registered KSC global table access is not bound");
            const unsigned flags=contact_scripts[actor_number_].flags;
            if(op==0x94 && unsigned(id)==flags)
                stack_.push_back({Kind::integer,int(intro_flags_)});
            else if(op==0x94 && unsigned(id)==flags+1 && room_day_)
                stack_.push_back({Kind::integer,*room_day_});
            else if(op==0x94 && scheduled_room_event_ && unsigned(id)==(alternate_greeting_?alternate_greeting_->schedule.midnight_global:robo_greeting_?0x59adu:0x59a5u))
                stack_.push_back({Kind::calendar_time,ScriptInteger(scheduled_midnight_)});
            else if(op==0x94 && scheduled_room_event_==3 && named && unsigned(id)==named->contacts_global)
                stack_.push_back({Kind::integer,ScriptInteger(scheduled_progress_.contacts)});
            else if(op==0x94 && scheduled_room_event_==3 && id==(robo_greeting_?0x59d4:0x59cc))
                stack_.push_back({Kind::integer,ScriptInteger(scheduled_progress_.contacts)});
            else if(op==0x94 && id==0x9a35 && exchange_inventory_)
                stack_.push_back({Kind::integer,int(global_arrays_.at(id).size())});
            else if(op==0x94 && id==0x98c3 && exchange_inventory_) stack_.push_back(friends_cells_.at(id));
            else if(op==0x94 && unsigned(id)==flags+12) stack_.push_back({Kind::integer,int(outing_enabled_)});
            else stack_.push_back({Kind::global_ref,id});
            break;
        }
        case 0x99: stack_.push_back({Kind::constant_ref,static_cast<int>(Read(2))}); break;
        case 0x98: {
            const auto index=Read(2);
            if(index==0) stack_.push_back({Kind::truth,event20_cleanup_}); // event 20 at 8DC38
            else if(robo_greeting_ && index==0x291d) stack_.push_back({Kind::integer,0}); // Cold local room.
            else if(excursion_ && index==0x2874) stack_.push_back({Kind::integer,int(outing_camera_)});
            else if(excursion_ && index==0x2871) stack_.push_back(outing_script_cells_.at(index));
            else if(friends_introduction_ && (index==0x2d54 || index==0x2869))
                stack_.push_back(friends_cells_.at(index));
            else if(friends_introduction_ && index==0x2873)
                stack_.push_back(skip_farewell_?Value{Kind::integer,2}:Value{});
            else if(scheduled_room_event_ && index==(alternate_greeting_?alternate_greeting_->schedule.event_cell:robo_greeting_?0x291bu:0x2867u))
                stack_.push_back({Kind::integer,int(scheduled_room_event_)});
            else if(scheduled_room_event_ && index==(alternate_greeting_?alternate_greeting_->schedule.event_cell-1:robo_greeting_?0x291au:0x2866u) && scheduled_deadline_)
                stack_.push_back(*scheduled_deadline_);
            else if(scheduled_room_event_ && index==(alternate_greeting_?alternate_greeting_->skip_farewell+1:robo_greeting_?0x2928u:0x2874u))
                stack_.push_back({Kind::integer,int(scheduled_camera_)});
            else if(scheduled_room_event_ && alternate_greeting_ && index==alternate_greeting_->skip_farewell-2)
                stack_.push_back(outing_script_cells_.at(index));
            else if(index==0x2869) stack_.push_back({Kind::integer,0}); // cold room conversation
            else {
                Require(index==contact_scripts[actor_number_].slot,"Unknown indirect topic slot");
                stack_.push_back(selected_fallback_);
            }
            break;
        }
        case 0xf6: {
            const unsigned mode=Read(1);
            if(mode==0) {
                Require(!stack_.empty() && stack_.back().kind==Kind::constant_ref,"Missing phrase array");
                random_bound_=static_cast<unsigned>(phrase_lists_.at(stack_.back().data).size());
                random_use_=RandomUse::phrase; phase_=Phase::random; return;
            }
            const auto& contact=contact_scripts[actor_number_];
            Require(mode==3 && Read(2)==contact.history,"Wrong fallback topic history");
            const auto list=Pop();
            Require(list.kind==Kind::constant_ref && unsigned(list.data)==contact.roster,
                    "Wrong fallback topic table");
            random_bound_=fallback_list_.CandidateCount();
            Require(random_bound_>0,"Empty fallback procedure array");
            random_use_=RandomUse::fallback; phase_=Phase::random; return;
        }
        case 0xf7: {
            // 77674 draws once from a nonempty array without a history list.
            // Unlike F6 this returns a reference to the chosen cell.
            Require(Read(1)==0,"Unsupported KSC reference selection history");
            auto array=Dereference(Pop());
            Require(array.kind==Kind::array && array.cells && !array.cells->empty(),
                    "Expected a nonempty KSC array reference");
            random_bound_=unsigned(array.cells->size());
            stack_.push_back(std::move(array));
            random_use_=RandomUse::array_reference;
            phase_=Phase::random;
            return;
        }
        case 0xf0: {
            auto ref=Pop();
            if(ref.kind==Kind::script_local_ref) ref=Dereference(std::move(ref));
            if(ref.kind==Kind::script_local_array)
                stack_.push_back({Kind::integer,ref.data});
            else if(excursion_ && ref.kind==Kind::constant_ref && ref.data==0x243f)
                stack_.push_back({Kind::integer,int(global_arrays_.at(0x243f).size())});
            else if(excursion_ && ref.kind==Kind::constant_ref && outing_tables_.count(ref.data))
                stack_.push_back({Kind::array,0,{},0,std::make_shared<std::vector<Value>>(outing_tables_.at(ref.data))});
            else if(ref.kind==Kind::global_ref && global_arrays_.count(ref.data))
                stack_.push_back({Kind::array,0,{},0,std::make_shared<std::vector<Value>>(global_arrays_.at(ref.data))});
            else if(ref.kind==Kind::array) stack_.push_back({Kind::integer,int(ref.cells->size())});
            else if(ref.kind==Kind::constant_ref && unsigned(ref.data)==contact_scripts[actor_number_].lesson_fields)
                stack_.push_back({Kind::integer,int(teaching_.size())});
            else {
                auto value=Dereference(ref);
                if(value.kind==Kind::array) value={Kind::integer,int(value.cells->size())};
                stack_.push_back(std::move(value));
            }
            break;
        }
        case 0xf4: {
            auto index=Pop(),array=Pop();
            if(array.kind==Kind::frame_ref || array.kind==Kind::script_local_ref) array=Dereference(array);
            if(array.kind==Kind::script_local_array) {
                Require(index.kind==Kind::integer && index.data>=0 && index.data<array.data,
                        "Invalid KSC local array index");
                stack_.push_back(ActiveScriptLocals().at(array.depth+1+unsigned(index.data)));
                break;
            }
            // The two actor scripts address the same verified trait forms at different slots.
            const auto trait_table=unsigned(array.data)-(robo_greeting_?0xfcu:0u);
            if(array.kind==Kind::array) stack_.push_back(array.cells->at(index.data));
            else if(friends_introduction_ && array.kind==Kind::constant_ref && array.data==0x2b0c) {
                const unsigned label=phrase_lists_.at(0x2b0c).at(index.data);
                auto row=std::make_shared<std::vector<Value>>(1,Value{Kind::string,0,pool_.at(label)});
                stack_.push_back({Kind::array,0,{},0,std::move(row)});
            }
            else if(array.kind==Kind::global_ref && global_arrays_.count(array.data) && index.kind==Kind::integer)
                stack_.push_back(global_arrays_.at(array.data).at(index.data));
            else if(array.kind==Kind::constant_ref && array.data==0x28f6 && index.kind==Kind::integer)
                stack_.push_back({Kind::integer,int(outing_period_masks_.at(index.data))});
            else if(array.kind==Kind::constant_ref && index.kind==Kind::integer &&
                    (array.data==0x243f || (scheduled_room_event_ && alternate_greeting_ &&
                                           unsigned(array.data)==alternate_greeting_->schedule.location_cell)))
                stack_.push_back(global_arrays_.at(array.data).at(index.data));
            else if(robo_greeting_ && array.kind==Kind::constant_ref && array.data==0x2498 &&
                    index.kind==Kind::integer)
                stack_.push_back(global_arrays_.at(0x2498).at(index.data));
            else if(excursion_ && array.kind==Kind::constant_ref && array.data==0x2df7 && index.kind==Kind::integer) {
                const auto details=outing_details_.at(index.data);
                Require(details!=0,"Missing text for current destination");
                stack_.push_back({Kind::constant_ref,int(details)});
            }
            else if(excursion_ && array.kind==Kind::constant_ref && outing_tables_.count(array.data))
                stack_.push_back(outing_tables_.at(array.data).at(index.data));
            else if(robo_greeting_ && ordinary_ && array.kind==Kind::global_ref && array.data==9 && index.data==2)
                stack_.push_back(DictionaryField(600,23));
            else if(array.kind==Kind::global_ref && array.data==9 && index.data==3 && adoption_minute_)
                stack_.push_back({Kind::calendar_time,ScriptInteger(*adoption_minute_)});
            else if(array.kind==Kind::global_ref && array.data==9 && (index.data==8 || index.data==9)) {
                // 204C reads previous-character profile data. New adoption has
                // no preceding Poképi. The original first handler takes B3.
                stack_.push_back({});
            } else if(scheduled_room_event_ && named && array.kind==Kind::global_ref && array.data==9 &&
                      index.kind==Kind::integer && (index.data==0 || index.data==1)) {
                stack_.push_back({Kind::string,0,index.data==0?player_:nickname_});
            } else if(array.kind==Kind::global_ref &&
                      (array.data==0x4fad || (robo_greeting_ && array.data==0x4fb5) ||
                       (alternate_greeting_ && unsigned(array.data)==alternate_greeting_->inventory)) &&
                      index.data==gift_slot_)
                stack_.push_back({Kind::global_ref,-1});
            else if(array.kind==Kind::global_ref &&
                    (array.data==0x3dd2 ||
                     (alternate_greeting_ && unsigned(array.data)==alternate_greeting_->dictionary)) &&
                    index.kind==Kind::integer)
                stack_.push_back({Kind::word_row,index.data});
            else if(array.kind==Kind::global_ref && array.data==0x58df && index.kind==Kind::integer) {
                Require(unsigned(index.data)<RoomVocabulary::question_slots,"Invalid prepared question row");
                stack_.push_back({Kind::question_row,index.data});
            }
            else if(array.kind==Kind::question_row && index.kind==Kind::integer)
                stack_.push_back(Dereference({Kind::question_field,array.data,{},unsigned(index.data)}));
            else if(array.kind==Kind::word_row && index.kind==Kind::integer)
                stack_.push_back(DictionaryField(array.data,index.data));
            else if(array.kind==Kind::constant_ref && unsigned(array.data)==contact_scripts[actor_number_].lesson_fields &&
                    index.kind==Kind::integer)
                stack_.push_back(teaching_.at(index.data));
            else if(array.kind==Kind::constant_ref && index.kind==Kind::integer &&
                    vocabulary_.HasPredicate(array.data)) {
                Value group{Kind::array,0,{},0,std::make_shared<std::vector<Value>>()};
                for(int number : vocabulary_.PredicateGroup(array.data,index.data))
                    group.cells->push_back({Kind::integer,number});
                stack_.push_back(std::move(group));
            }
            else if(array.kind==Kind::constant_ref && RoomWordTraits::IsTextTable(trait_table) && index.kind==Kind::integer)
                stack_.push_back({Kind::string,0,traits_.Label(trait_table,index.data)});
            else if(array.kind==Kind::constant_ref && index.kind==Kind::integer &&
                    phrase_lists_.count(array.data))
                stack_.push_back({Kind::string,0,pool_.at(phrase_lists_.at(array.data).at(index.data))});
            else if(array.kind==Kind::global_ref && array.data==-1 && index.data==0)
                stack_.push_back(slot_empty_?Value{}:Value{Kind::integer,0});
            else throw std::runtime_error("Missing cold KSC indexed value");
            break;
        }
        case 0xf5: {
            auto index=Pop(),array=Pop();
            Require(index.kind==Kind::integer,"Invalid KSC field");
            if(array.kind==Kind::frame_ref || array.kind==Kind::script_local_ref) array=Dereference(array);
            if(array.kind==Kind::script_local_array) {
                Require(index.data>=0 && index.data<array.data,"Invalid KSC local array reference");
                stack_.push_back({Kind::script_local_ref,int(array.depth+1+unsigned(index.data))});
            }
            else if((excursion_ || scheduled_room_event_) && array.kind==Kind::constant_ref && array.data==0x243f)
                stack_.push_back({Kind::global_element,0x243f,{},unsigned(index.data)});
            else if(array.kind==Kind::constant_ref && unsigned(array.data)==contact_scripts[actor_number_].lesson_fields)
                stack_.push_back({Kind::teach_field,index.data});
            else if(array.kind==Kind::global_ref && global_arrays_.count(array.data)) {
                Require(index.data>=0 && unsigned(index.data)<global_arrays_.at(array.data).size(),
                        "Room script global index outside array");
                stack_.push_back({Kind::global_element,array.data,{},unsigned(index.data)});
            }
            else if(array.kind==Kind::array) stack_.push_back({Kind::element,index.data,{},0,array.cells});
            else if(friends_introduction_ && array.kind==Kind::constant_ref && array.data==0x2a3c && index.data==2)
                stack_.push_back({Kind::element,index.data,{},0,friends_cells_.at(array.data).cells});
            else if(friends_introduction_ && array.kind==Kind::constant_ref && array.data==0x2a44 &&
                    (index.data==2 || index.data==8 || index.data==11 || index.data==14))
                stack_.push_back({Kind::element,index.data,{},0,friends_cells_.at(array.data).cells});
            else if(array.kind==Kind::question_row)
                stack_.push_back({Kind::question_field,array.data,{},unsigned(index.data)});
            else if(array.kind==Kind::word_row) stack_.push_back({Kind::word_field,array.data,{},unsigned(index.data)});
            else throw std::runtime_error("Unsupported mutable KSC array");
            break;
        }
        case 0x50: case 0x51: {
            auto value=Pop(),ref=Pop();
            if(op==0x50) stack_.push_back(value); // Assignment expression retains the assigned value.
            Assign(ref,std::move(value));
            break;
        }
        case 0x78: {
            auto right=Pop(),left=Pop();
            auto left_value=Dereference(left),right_value=Dereference(right);
            Assign(left,std::move(right_value)); Assign(right,std::move(left_value)); break;
        }
        case 0x14: {
            auto array=Pop(),slot=Pop();
            Require(site==0x2db2 && array.kind==Kind::global_ref && array.data==0x5f92 &&
                    slot.kind==Kind::frame_ref && slot.data==7,"Unknown outing iteration");
            outing_index_=0;
            Assign(slot,global_arrays_.at(0x5f92).front()); break;
        }
        case 0x15: {
            const auto loop=Read(3);
            Require(site==0x2e37 && loop==0x2db3,"Unknown outing loop");
            const auto& order=global_arrays_.at(0x5f92);
            if(++outing_index_<order.size()) { locals_.at(7)=order[outing_index_]; pc_=loop; }
            break;
        }
        case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x66:
        case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: {
            const auto rhs=Pop(),ref=Pop(),lhs=Dereference(ref);
            Require(lhs.kind==Kind::integer && rhs.kind==Kind::integer,"Invalid integer compound assignment");
            const Value result{Kind::integer,IntegerCompound(op,lhs.data,rhs.data)};
            Assign(ref,result);
            if(!(op&1u)) stack_.push_back(result);
            break;
        }
        case 0x52: case 0x53: case 0x54: case 0x55: {
            const auto ref=Pop(),value=Dereference(ref);
            Require(value.kind==Kind::integer,"Invalid KSC increment or decrement");
            const auto bits=op<0x54 ? unsigned(value.data)+1u : unsigned(value.data)-1u;
            Assign(ref,{Kind::integer,ScriptInteger(bits)});
            // The decrement handler also compares against 0x52 at 6EEB8.
            // Neither decrement opcode leaves the old value on the stack.
            if(op==0x52) stack_.push_back(value);
            break;
        }
        case 0x56: case 0x57: {
            auto source_ref=Pop(),target_ref=Pop();
            const auto target=target_ref.kind==Kind::array?std::move(target_ref):Dereference(std::move(target_ref));
            const auto source=source_ref.kind==Kind::array?source_ref:Dereference(source_ref);
            Require(target.kind==Kind::array && source.kind==Kind::array && target.cells && source.cells,
                    "Expected two KSC array references");
            const auto count=std::min(target.cells->size(),source.cells->size());
            for(std::size_t i=0;i<count;++i) target.cells->at(i)=source.cells->at(i);
            std::fill(target.cells->begin()+count,target.cells->end(),Value{});
            if(op==0x56) stack_.push_back(std::move(source_ref));
            break;
        }
        case 0xc9: case 0xc8: {
            auto rhs=Pop(),lhs=Pop();
            stack_.push_back({Kind::truth,op==0xc9?(bool(rhs.data)||bool(lhs.data)):(bool(rhs.data)&&bool(lhs.data))}); break;
        }
        case 0x12: {
            const auto target=Read(3);
            if(Pop().data) pc_=target;
            break;
        }
        case 0x65: case 0x67: {
            auto value=Pop(),ref=Pop();
            if(ref.kind==Kind::word_field || ref.kind==Kind::question_field || ref.kind==Kind::element || ref.kind==Kind::global_element || ref.kind==Kind::teach_field ||
               ref.kind==Kind::frame_ref) {
                auto current=Dereference(ref);
                Assign(ref,{Kind::integer,op==0x67?(current.data|value.data):(current.data&value.data)});
            } else {
                if(excursion_ && ref.kind==Kind::global_ref && ref.data==0x59ae && op==0x67) {
                    outing_enabled_|=unsigned(value.data); break;
                }
                Require(ref.kind==Kind::global_ref &&
                        (ref.data==0x59a2 || (robo_greeting_ && ref.data==0x59aa) ||
                         (alternate_greeting_ && unsigned(ref.data)==alternate_greeting_->flags)) &&
                        (value.data==1 || value.data==64 || (excursion_ && value.data==0x100) ||
                         (scheduled_room_event_==2 && site==(named?named->callbacks[0]+0x3d:robo_greeting_?0x67658u:0x89741u) && value.data==2) ||
                         (scheduled_room_event_==6 && site==(named?named->callbacks[4]+0xb:robo_greeting_?0x674edu:0x895d6u) && value.data==8) ||
                         (scheduled_room_event_==4 && site==(named?named->callbacks[2]+0x3e:robo_greeting_?0x6769au:0x89783u) && value.data==16) ||
                         (scheduled_room_event_==3 && site==(named?named->callbacks[1]+0x18:robo_greeting_?0x67519u:0x89602u) && value.data==4) ||
                         (scheduled_room_event_==5 && site==(named?named->callbacks[3]+0xb:robo_greeting_?0x675adu:0x89696u) && value.data==32) ||
                         (scheduled_room_event_==7 && letter_script_ && site==letter_script_->callback+8 && value.data==128)),
                        "Invalid intro completion flag");
                intro_flags_|=value.data;
            }
            break;
        }
        case 0xb0: {
            const auto value=Pop();
            Require(value.kind==Kind::integer,"Invalid KSC negation");
            stack_.push_back({Kind::integer,ScriptInteger(0u-unsigned(value.data))});
            break;
        }
        case 0xb1: {
            const auto value=Pop();
            Require(value.kind==Kind::truth,"Expected a predicate result for logical NOT");
            stack_.push_back({Kind::truth,!value.data});
            break;
        }
        case 0xb2: case 0xb3: {
            auto v=Pop(); stack_.push_back({Kind::truth,(v.kind==Kind::nil)==(op==0xb3)}); break;
        }
        case 0xb9: { auto v=Pop(); stack_.push_back({Kind::truth,v.data!=0}); break; }
        case 0xba: {
            const auto value=Pop();
            Require(value.kind==Kind::integer,"Invalid KSC positive test");
            stack_.push_back({Kind::truth,value.data>0});
            break;
        }
        case 0xbc: { auto v=Pop(); stack_.push_back({Kind::truth,v.kind==Kind::integer && v.data<0}); break; }
        case 0xbd: {
            const auto v=Pop();
            Require(v.kind==Kind::integer,"Invalid count in KSC nonpositive test");
            stack_.push_back({Kind::truth,v.data<=0}); break;
        }
        case 0xb8: { auto v=Pop(); stack_.push_back({Kind::truth,v.kind==Kind::integer && v.data==0}); break; }
        case 0xbb: {
            auto value=Pop();
            // BOOT 71EBC–72014 tests the sign of the numeric value or the string/array length.
            Require(value.kind==Kind::integer || value.kind==Kind::truth || value.kind==Kind::string ||
                    value.kind==Kind::nil || value.kind==Kind::array,"Unsupported KSC nonnegative operand");
            stack_.push_back({Kind::truth,value.data>=0});
            break;
        }
        case 0xc2: case 0xc3: case 0xc4: case 0xc5: {
            auto rhs=Pop(),lhs=Pop();
            Require((lhs.kind==Kind::integer && rhs.kind==Kind::integer) ||
                    ((site==0xa5e9 || (scheduled_room_event_ && site==(alternate_greeting_?0x1a73u:robo_greeting_?0x1a5eu:0x1a5du))) &&
                     lhs.kind==Kind::calendar_time && rhs.kind==Kind::calendar_time),
                    "Invalid numeric comparison");
            const bool match=op==0xc2?lhs.data<rhs.data:op==0xc3?lhs.data<=rhs.data:op==0xc4?lhs.data>rhs.data:lhs.data>=rhs.data;
            stack_.push_back({Kind::truth,match}); break;
        }
        case 0xc0: case 0xc1: {
            auto rhs=Pop(),lhs=Pop();
            const bool equal=rhs.kind==lhs.kind && rhs.data==lhs.data && rhs.text==lhs.text;
            stack_.push_back({Kind::truth,equal==(op==0xc0)}); break;
        }
        case 0xd4: {
            auto rhs=Pop(),lhs=Pop();
            Require((lhs.kind==Kind::integer || lhs.kind==Kind::nil) && rhs.kind==Kind::integer,"Invalid word attribute mask");
            stack_.push_back({Kind::integer,lhs.data&rhs.data}); break;
        }
        case 0xd5: {
            const auto rhs=Pop(),lhs=Pop();
            Require(lhs.kind==Kind::integer && rhs.kind==Kind::integer,"Invalid KSC bitwise OR");
            stack_.push_back({Kind::integer,lhs.data|rhs.data}); break;
        }
        case 0xd6: case 0xd7: {
            const auto rhs=Pop(),lhs=Pop();
            Require(lhs.kind==Kind::integer && rhs.kind==Kind::integer,"Invalid KSC bitwise operands");
            const unsigned left=unsigned(lhs.data),right=unsigned(rhs.data);
            stack_.push_back({Kind::integer,ScriptInteger(op==0xd6 ? left^right : left&~right)});
            break;
        }
        case 0xd1: case 0xd2: {
            const auto divisor=Pop(),dividend=Pop();
            Require(divisor.kind==Kind::integer && dividend.kind==Kind::integer,
                    "Invalid KSC integer division");
            const auto value=IntegerCompound(op==0xd1 ? 0x60 : 0x62,dividend.data,divisor.data);
            stack_.push_back({Kind::integer,value}); break;
        }
        case 0xe2: case 0xe3: {
            const auto count=Pop(),value=Pop();
            Require(count.kind==Kind::integer && value.kind==Kind::integer,"Invalid KSC shift");
            const unsigned shift=unsigned(count.data)&31u;
            const unsigned bits=unsigned(value.data);
            // BOOT 73D5C uses SRAV of the signed tag-2 payload.
            const unsigned shifted=op==0xe2 ? ArithmeticShiftRight(value.data,shift) : bits<<shift;
            stack_.push_back({Kind::integer,ScriptInteger(shifted)}); break;
        }
        case 0xb4: {
            const auto value=Pop();
            Require(value.kind==Kind::integer,"Invalid KSC bitwise complement");
            stack_.push_back({Kind::integer,~value.data}); break;
        }
        case 0xd0: case 0xe0: case 0xe1: {
            const auto rhs=Pop(),lhs=Pop();
            if(excursion_ && site==0x1259e && lhs.kind==Kind::calendar_time && rhs.kind==Kind::integer) {
                stack_.push_back({Kind::calendar_time,ScriptInteger(unsigned(lhs.data)+unsigned(rhs.data))}); break;
            }
            if(scheduled_room_event_ && site==(alternate_greeting_?0x1a6au:robo_greeting_?0x1a55u:0x1a54u) && op==0xe0 &&
               lhs.kind==Kind::integer && rhs.kind==Kind::calendar_time) {
                stack_.push_back({Kind::calendar_time,ScriptInteger(unsigned(lhs.data)+unsigned(rhs.data))}); break;
            }
            if(scheduled_room_event_ && (named ? site==named->callbacks[0]+0x11 || site==named->callbacks[0]+0x93 ||
                                                    site==named->callbacks[2]+0x12 :
                                         robo_greeting_ ? site==0x6762c || site==0x6766e || site==0x676ae :
                                                        site==0x89715 || site==0x89757) && op==0xe0 &&
               lhs.kind==Kind::calendar_time && rhs.kind==Kind::integer) {
                stack_.push_back({Kind::calendar_time,ScriptInteger(unsigned(lhs.data)+unsigned(rhs.data))}); break;
            }
            Require(lhs.kind==Kind::integer && rhs.kind==Kind::integer,"Expected two KSC integers");
            const unsigned left=unsigned(lhs.data),right=unsigned(rhs.data);
            const unsigned bits=op==0xd0 ? left*right : op==0xe0 ? left+right : left-right;
            stack_.push_back({Kind::integer,ScriptInteger(bits)}); break;
        }
        case 0x8a: stack_.push_back({Kind::code,int(Read(3))}); break;
        case 0x10: pc_=Read(3); break;
        case 0x11: {
            const auto target=Read(3);
            const auto value=Pop();
            const bool set=value.kind==Kind::string ? !value.text.empty() : value.data!=0;
            if(!set) pc_=target;
            break;
        }
        case 0x13: {
            const auto key=Pop();
            for(unsigned tag=Read(4);tag;tag=Read(4)) {
                const unsigned next=Read(3);
                if((tag>>28)==2 && key.kind==Kind::integer && int(tag&0x0fffffff)==key.data) break;
                pc_=next;
            }
            break;
        }
        case 0x24: case 0x25: {
            const unsigned arguments=op==0x24?Read(1):0;
            const unsigned table=Read(2),mode=Read(1);
            Require(mode==0 && procedure_lists_.count(table),"Unsupported KSC procedure selector");
            Require(arguments<=stack_.size(),"Insufficient KSC procedure arguments");
            pending_procedure_list_=table;
            pending_procedure_arguments_=arguments;
            random_bound_=static_cast<unsigned>(procedure_lists_.at(table).size());
            random_use_=RandomUse::procedure; phase_=Phase::random; return;
        }
        case 0x28:
        case 0x29: {
            const auto argc=op==0x28?Read(1):0u;
            const auto target=Read(3);
            StartChildScript(target,argc);
            break;
        }
        case 0x2c:
        case 0x2d:
        case 0x2e:
        case 0x2f: {
            // The argument-bearing direct form places the count first and
            // the owner after the index. The other forms start with the owner.
            unsigned argc=0,table=0,index=0;
            if((op&3)==0) {
                argc=Read(1); index=Read(2); table=Read(1);
            } else {
                table=Read(1); index=Read(2);
                if((op&3)==2) argc=Read(1);
            }
            const bool indirect=(op&2)!=0;
            unsigned target=indirect?0:Read(3);
            Require(stack_.size()>=argc+unsigned(indirect),"Insufficient KSC procedure ring arguments");
            if(indirect) {
                const auto& selected=stack_[stack_.size()-argc-1];
                Require(selected.kind==Kind::code && selected.data>=0 && selected.data<=0x00ffffff,
                        "Invalid KSC procedure ring target");
                target=unsigned(selected.data);
            }
            RememberProcedure(table,index,target);
            std::vector<Value> args(argc);
            for(unsigned i=argc;i>0;--i) args[i-1]=Pop();
            if(indirect) (void)Pop();
            Call(target,std::move(args),false,true);
            if(phase_!=Phase::running) return;
            break;
        }
        case 0x20: case 0x21: case 0x22: case 0x23: {
            const unsigned argc=(op==0x20 || op==0x22)?Read(1):0;
            const bool indirect=op==0x22 || op==0x23;
            unsigned target=indirect?0:Read(3);
            std::vector<Value> args(argc);
            for(unsigned i=argc;i>0;--i) args[i-1]=Pop();
            if(indirect) {
                const auto selected=Pop();
                Require(selected.kind==Kind::code,"Invalid indirect KSC call");
                target=selected.data;
            }
            Call(target,std::move(args),op==0x23);
            if(phase_!=Phase::running) return;
            break;
        }
        case 0x70: result_=Pop(); break;
        case 0x31: {
            if(callers_.empty()) {
                if(executing_child_) { phase_=Phase::done; return; }
                if(robo_contact_greeting_) {
                    Require(site==0x5be18 && result_.kind==Kind::truth && result_.data,
                            "Unexpected Robo contact greeting return");
                    robo_contact_greeting_=ordinary_=false;
                    robo_contact_greeting_return_=true;
                    phase_=Phase::done;
                    return;
                }
                if(scheduled_room_event_) {
                    const unsigned flag=scheduled_room_event_==7?128:scheduled_room_event_==6?8:scheduled_room_event_==4?16:2;
                    Require(site==(alternate_greeting_?alternate_greeting_->schedule.wrapper+0xe3:robo_greeting_?0x6787du:0x89966u) && (intro_flags_&flag) && scheduled_count_contact_ &&
                            blocks_.empty(),"Incomplete scheduled room return");
                    scheduled_room_event_=0;
                    ordinary_=false; indirect_topic_=0; scheduled_return_=true;
                    phase_=Phase::done; return;
                }
                if(queued_root_) {
                    // 25A8..25B1 clears CT2878 after the selected function returns.
                    selected_fallback_={}; indirect_topic_=0; queued_root_=false;
                }
                if(excursion_ && indirect_topic_==0x12f39) {
                    Require((site==0x12fcb || site==0x13006 || site==0x1308d) &&
                            result_.kind==Kind::truth && result_.data,"Unexpected invitation return");
                    if(home_invitation_return_) home_invitation_return_();
                    excursion_=false;
                }
                if(excursion_) {
                    Require(site==0x13252 || site==0x133a6 || site==0x13411 || site==0x13218,
                            "Unexpected outing root return");
                    excursion_=ordinary_=false; ordinary_return_=true; indirect_topic_=0;
                    phase_=Phase::done; return;
                }
                if(direct_teaching_) {
                    Require(robo_greeting_ ? (site==0x8e3b || site==0x8eaa || site==0x8f6c)
                                          : (site==0x8df0 || site==0x8e5f || site==0x8f21),
                            "Unexpected teaching root return");
                    direct_teaching_=ordinary_=false; ordinary_return_=true; phase_=Phase::done; return;
                }
                if(attention_) {
                    // The immediate next message at 8D8FE is the single clear control 04.
                    Require(code_.at(attention_entry()).size()==pc_-attention_entry(),"Wrong attention-message return");
                    message_pc_=contact_scripts[actor_number_].clear_text; message_.clear(); lines_.clear();
                    message_wait_=closing_=new_line_=false; appearance_=scroll_=0;
                    columns_=9; rows_=4; attention_=false; attention_return_=true;
                    phase_=Phase::done; return;
                }
                if(ordinary_ && (farewell_ || (result_.kind==Kind::truth && !result_.data))) {
                    ordinary_rejected_=!farewell_;
                    ordinary_return_=farewell_;
                    ordinary_=false; phase_=Phase::done; return;
                }
                if(farewell_) { ReturnFromGreeting(); return; }
                // 89590 sets ctx28[2873]=2 if the new gift had no slot.
                // In that case 89A08..89A19 bypass the farewell entirely.
                if(skip_farewell_) {
                    actor_command_=74;
                    if (ordinary_) {
                        // 89A08..89A19 skips only the farewell. The root still
                        // returns to its ordinary room cleanup.
                        ordinary_=false; ordinary_return_=true; phase_=Phase::done;
                    } else ReturnFromGreeting();
                    return;
                }
                // 8942E indirect return, 899F9 -> 89A1D normal farewell.
                // GAME21/22 carries this encounter's changed actor field.
                vocabulary_.BeginConversation(); // 899FB enters 0FD1 before 89A1D
                farewell_=true;
                pc_=alternate_greeting_?alternate_greeting_->farewell:robo_greeting_?0x5be76u:0x7d548u;
                locals_.clear();
                break;
            }
            auto parent=std::move(callers_.back()); callers_.pop_back();
            pc_=parent.resume; locals_=std::move(parent.locals); indirect_topic_=parent.indirect_topic; break;
        }
        case 0x40: {
            const auto offset=Pop();
            Require(offset.kind==Kind::integer,"Invalid KSC deadline offset");
            script_deadline_=script_timer_base_+unsigned(ScriptInteger(unsigned(offset.data)));
            return;
        }
        case 0x41: {
            const auto offset=Pop();
            Require(offset.kind==Kind::integer,"Invalid KSC timer-base offset");
            script_timer_base_=script_ticks_+unsigned(ScriptInteger(unsigned(offset.data)));
            break;
        }
        case 0x43:
            // 6E684 has no operand. It sets the deadline to the next tick.
            script_deadline_=script_ticks_+1;
            return;
        case 0x44:
            delay_=Pop().data;
            if(!executing_child_ && queued_outing_callback_ && site==0x1c69) {
                // 1C64 posted to this context. Deliver at its first scheduler yield,
                // preserving the caller's next PC, operands and one-tick deadline.
                Require(delay_==1 && !outing_callback_,"Invalid outing callback deadline");
                outing_callback_.emplace(OutingCallback{pc_,std::move(stack_),std::move(locals_),
                                          std::move(callers_),std::move(blocks_),std::move(result_),delay_,{},
                                          std::move(control_resumes_)});
                pc_=*queued_outing_callback_; queued_outing_callback_.reset();
                stack_.clear(); locals_.clear(); callers_.clear(); blocks_.clear(); result_={};
                control_resumes_.clear();
                delay_=0;
                break;
            }
            return;
        case 0x48: Message(); return;
        case 0x49: case 0x4a:
            // The retail debug sink at 5156C returns without output. Both
            // commands still expand and consume the inline text.
            (void)ReadText();
            break;
        case 0x8b: {
            const unsigned id=Read(1),argc=Read(1);
            if(executing_child_)
                Require(id<=0x10 || id==0x50 || id==0x52 || (id>=0x70 && id<=0x72) ||
                        (ActiveModule() && (id==0x26 || id==0x80 || id==0x90 || id==0x91 || id==0xa0)),
                        "Unsupported child KSC service");
            if(id==0x01 && argc==1) {
                // 6857C returns the original tag, so only kinds with an
                // unambiguous source representation can use this branch.
                const auto value=Pop();
                switch(value.kind) {
                case Kind::nil: result_={Kind::integer,0}; break;
                case Kind::truth: result_={Kind::integer,1}; break;
                case Kind::integer: result_={Kind::integer,2}; break;
                case Kind::calendar_time: result_={Kind::integer,3}; break;
                case Kind::fixed14: result_={Kind::integer,4}; break;
                case Kind::code: result_={Kind::integer,7}; break;
                case Kind::association: result_={Kind::integer,10}; break;
                default: throw std::runtime_error("Unsupported KSC tag query");
                }
            } else if(id==0x02) {
                Require(argc<=stack_.size(),"Invalid KSC discard argument count");
                stack_.resize(stack_.size()-argc);
                result_={};
            } else if(id==0x03 && argc==1) {
                const auto value=Pop();
                Require(value.kind==Kind::integer || value.kind==Kind::truth ||
                        value.kind==Kind::nil || value.kind==Kind::calendar_time,
                        "Invalid KSC scalar truth conversion");
                result_={Kind::truth,value.data!=0};
            } else if(id==0x05 && argc==1) {
                const auto value=Pop();
                Require(value.kind==Kind::integer || value.kind==Kind::fixed14,
                        "Invalid KSC high-half operand");
                result_={Kind::integer,ScriptInteger(ArithmeticShiftRight(value.data,14))};
            } else if(id==0x06 && argc==1) {
                const auto value=Pop();
                Require(value.kind==Kind::integer || value.kind==Kind::fixed14,
                        "Invalid KSC low-half operand");
                const auto bits=unsigned(value.data)&0x3fffu;
                result_={Kind::integer,static_cast<int>(bits^0x2000u)-0x2000};
            } else if(id==0x07 && argc==2) {
                const auto low=Pop(),high=Pop();
                Require(low.kind==Kind::integer && high.kind==Kind::integer,
                        "Invalid KSC fixed-point operands");
                // 752B8 clamps two signed 14-bit halves before packing tag 4.
                const unsigned hi=unsigned(std::clamp(high.data,-8192,8191))&0x3fffu;
                const unsigned lo=unsigned(std::clamp(low.data,-8192,8191))&0x3fffu;
                result_={Kind::fixed14,ScriptInteger((hi<<14)|lo)};
            } else if(id==0x08 && argc==1) {
                auto value=Pop();
                if(value.kind==Kind::integer) result_={Kind::string,0,std::to_string(value.data)};
                else {
                    Require(value.kind==Kind::string,"Invalid KSC string conversion");
                    result_=std::move(value);
                }
            } else if(id==0x0d && argc==1) {
                const auto value=Pop();
                Require(value.kind==Kind::string && !value.text.empty(),"Invalid KSC two-byte text operand");
                // The source reads two bytes. A single-byte C string has
                // its terminator in the high byte at this address.
                result_={Kind::integer,int(static_cast<unsigned char>(value.text[0]) |
                                          (unsigned(static_cast<unsigned char>(value.text[1]))<<8))};
            } else if(id==0x0e && argc==1) {
                const auto value=Pop();
                Require(value.kind==Kind::integer,"Invalid integer for KSC byte string");
                const auto bits=unsigned(value.data);
                const char bytes[]={static_cast<char>(bits&0xffu),static_cast<char>((bits>>8)&0xffu),0};
                result_={Kind::string,0,std::string(bytes)};
            } else if(id==0x10 && argc==1) {
                const auto value=Pop();
                Require(value.kind==Kind::integer,"Invalid KSC absolute value operand");
                const auto bits=unsigned(value.data);
                result_={Kind::integer,ScriptInteger(value.data<0?0u-bits:bits)};
            } else if(id==0x50 && argc==2) {
                const auto needle=Pop(),haystack=Pop();
                Require(needle.kind==Kind::string && haystack.kind==Kind::string,
                        "Invalid KSC text lookup operands");
                // 7BE34 searches byte positions in NUL-terminated text.
                // At 6A328 both no match and byte position zero yield nil.
                const std::string_view source{haystack.text.c_str()},query{needle.text.c_str()};
                const auto pos=query.empty()?std::string_view::npos:source.find(query);
                result_=pos==0 || pos==std::string_view::npos
                    ? Value{} : Value{Kind::integer,ScriptInteger(unsigned(pos))};
            } else if(id==0x70 && argc==1) {
                const auto value=Pop();
                Require(value.kind==Kind::string,"Invalid KSC kana eligibility operand");
                // After the normal script initialization at 76AF4 the
                // accepted branch is true and the default branch is false.
                const std::string_view text(value.text.c_str());
                result_={Kind::truth,text.size()<=30 && !KanaEndpoint(text,true).empty()};
            } else if(id==0x71 && argc==1) {
                const auto value=Pop();
                Require(value.kind==Kind::string,"Invalid KSC first kana operand");
                auto text=KanaEndpoint(value.text,false);
                result_=text.empty()?Value{}:Value{Kind::string,0,std::move(text)};
            } else if(id==0x72 && argc==1) {
                const auto value=Pop();
                Require(value.kind==Kind::string,"Invalid KSC last kana operand");
                auto text=KanaEndpoint(value.text,true);
                result_=text.empty()?Value{}:Value{Kind::string,0,std::move(text)};
            } else if(id==0x20 && argc==2) {
                Require(stack_.size()>=argc,"Insufficient KSC word keyboard arguments");
                const auto& initial=stack_.back();
                const auto& mode=stack_[stack_.size()-2];
                Require(mode.kind==Kind::integer &&
                        (initial.kind==Kind::string || initial.kind==Kind::nil) &&
                        (mode.data==2 || (mode.data==0 && initial.text.empty())),
                        "Unsupported KSC word keyboard mode");
                auto argument=Pop();
                const auto editor_mode=Pop().data;
                // 697C0 waits for the host. 682B8 publishes the two-cell result
                // before the script resumes after BI20.
                result_={};
                OpenWordKeyboard(editor_mode,std::move(argument.text),{});
                return;
            } else if(id==0x80) {
                LoadScriptModule(argc);
            } else if(id==0x90) {
                RegisterScriptContext(argc);
            } else if(id==0x91) {
                RegisterScriptMap(argc);
            } else if(id==0xa0) {
                ActivateScriptContext(argc);
            } else if(id==0x24) {
                InvokeHostBuiltin(HostBuiltin::command,argc);
            } else if(id==0x26) {
                RendezvousScriptContexts(argc);
                return;
            } else if(id==0x40) {
                InvokeHostBuiltin(HostBuiltin::query,argc);
            } else if(id==0x42) {
                InvokeHostBuiltin(HostBuiltin::notify,argc);
            } else if(id==0x27 && argc==4 && excursion_ && site==0x1c64) {
                const auto mode=Pop(),deadline=Pop(),callback=Pop(),caller=Pop();
                Require(caller.kind==Kind::context && callback.kind==Kind::code && callback.data==0x1c57 &&
                        mode.kind==Kind::integer && !mode.data && deadline.kind==Kind::integer && !deadline.data &&
                        outing_scene_request_.has_value(),"Invalid outing completion callback");
                Require(!queued_outing_callback_ && !outing_callback_,"Outing callback already active");
                queued_outing_callback_=unsigned(callback.data);
            } else if(id==0x04 && argc==1) {
                const auto array=Pop();
                Require(array.kind==Kind::array,"Expected script array for length");
                result_={Kind::integer,static_cast<int>(array.cells->size())};
            } else if(id==9 && argc==0) {
                Require(calendar_minute_.has_value(),"Missing room calendar minute");
                result_={Kind::calendar_time,ScriptInteger(*calendar_minute_)};
            } else if(id==10 && argc==1) {
                const auto time=Pop();
                Require(time.kind==Kind::calendar_time,"Expected a KSC time for the calendar date");
                result_={Kind::integer,ScriptInteger(recovered::CalendarBcdDate68FE0(time.data))};
            } else if(id==11 && argc==1) {
                const auto time=Pop();
                Require(time.kind==Kind::calendar_time,"Expected a KSC time for the clock");
                result_={Kind::integer,ScriptInteger(recovered::CalendarBcdTime6912C(time.data))};
            } else if(id==12 && argc==1) {
                const auto time=Pop();
                Require(time.kind==Kind::calendar_time,"Expected a KSC time for the weekday");
                result_={Kind::integer,(time.data/1440+5)%7}; // BOOT 692BC–69340.
            } else if(id==0x52 && argc==3) {
                const auto length=Pop(),start=Pop();
                auto value=Pop();
                Require(value.kind==Kind::string && start.kind==Kind::integer &&
                        (length.kind==Kind::integer || length.kind==Kind::nil),"Invalid KSC substring operands");
                // 7C048 counts source characters before copying bytes.
                std::vector<std::size_t> offsets;
                for (std::size_t byte=0;byte<value.text.size();) {
                    offsets.push_back(byte);
                    byte+=GlyphAt(value.text,byte).consumed;
                }
                const auto count=int(offsets.size());
                offsets.push_back(value.text.size());
                const auto first=std::clamp(start.data<0?count+start.data:start.data,0,count);
                const auto last=length.kind==Kind::nil || length.data<0
                    ? count : std::min(count,first+length.data);
                const auto begin=offsets.at(first), end=offsets.at(last);
                value.text=value.text.substr(begin,end-begin);
                std::vector<RoomMessageWord> kept;
                for (auto operand:value.word_operands) {
                    if (operand.offset<begin || operand.offset>end || operand.length>end-operand.offset) continue;
                    operand.offset-=begin; kept.push_back(operand);
                }
                value.word_operands=std::move(kept);
                result_=std::move(value);
            } else if(id==0x51 && argc==3 && excursion_) {
                auto substitution=Pop(),token=Pop(),text=Pop();
                Require(substitution.kind==Kind::string && token.kind==Kind::string && !token.text.empty() &&
                        text.kind==Kind::string,"Invalid outing text substitution");
                std::size_t start=0;
                while((start=text.text.find(token.text,start))!=std::string::npos) {
                    text.text.replace(start,token.text.size(),substitution.text);
                    start+=substitution.text.size();
                }
                result_={Kind::string,0,std::move(text.text)};
            } else if(id==0x53 && argc==1) {
                const auto value=Pop();
                Require(value.kind==Kind::string || value.kind==Kind::nil,"Invalid KSC text for length");
                result_={Kind::integer,static_cast<int>(ScriptCharacterCount(value.text))};
            } else if(id==0x60 && argc==3 && friends_introduction_ && site==0xa7a4) {
                const auto filter=Pop(),dest=Dereference(Pop()),source=Pop();
                Require(source.kind==Kind::global_ref && source.data==0x58df &&
                        filter.kind==Kind::constant_ref && filter.data==0x2a44 &&
                        dest.kind==Kind::array && dest.cells->size()==1,"Invalid question duplicate lookup");
                const auto& terms=*friends_cells_.at(filter.data).cells;
                const auto mask=terms[2];
                Require(mask.kind==Kind::integer,"Missing question flags");
                result_={Kind::integer,0};
                for(unsigned i=0;i<RoomVocabulary::question_slots;++i) {
                    const auto flags=vocabulary_.QuestionField(i,0);
                    if((flags&unsigned(mask.data))!=unsigned(mask.data) || (flags&16)) continue;
                    auto equal=[&](unsigned field,const Value& expected) {
                        const auto cell=Dereference({Kind::question_field,int(i),{},field});
                        return cell.kind==expected.kind && cell.data==expected.data && cell.text==expected.text;
                    };
                    if(equal(1,terms[8]) && equal(3,terms[11]) && equal(5,terms[14])) {
                        dest.cells->front()={Kind::integer,int(i)};
                        result_.data=1; break;
                    }
                }
            } else if(id==0x60 && argc==3 && friends_introduction_ && site==0xa735) {
                const auto predicate=Pop(),dest=Dereference(Pop()),source=Pop();
                Require(source.kind==Kind::global_ref && source.data==0x58df &&
                        dest.kind==Kind::array && dest.cells->size()==RoomVocabulary::question_slots &&
                        predicate.kind==Kind::constant_ref && predicate.data==0x2a3c,
                        "Invalid prepared question search");
                const auto mask=friends_cells_.at(predicate.data).cells->at(2);
                Require(mask.kind==Kind::integer,"Invalid prepared question mask");
                unsigned count=0;
                for(unsigned i=0;i<RoomVocabulary::question_slots;++i) {
                    const unsigned flags=vocabulary_.QuestionField(i,0)&0x0fffffffu;
                    if((flags & unsigned(mask.data))==unsigned(mask.data))
                        dest.cells->at(count++)={Kind::integer,int(i)};
                }
                result_={Kind::integer,int(count)};
            } else if(id==0x60 && argc==3 && exchange_inventory_ && site==0x1798) {
                const auto filter=Pop(),dest=Pop(),source=Pop();
                Require(source.kind==Kind::global_ref && source.data==0x3dd2 &&
                        dest.kind==Kind::constant_ref && dest.data==0x24df &&
                        filter.kind==Kind::constant_ref && filter.data==0xe25e,"Invalid shiritori word filter");
                int count=0;
                for(unsigned i=0;i<vocabulary_.row_count();++i)
                    if(vocabulary_.FieldInt(i,0)&0x4000) ++count;
                result_={Kind::integer,count};
            } else if(id==0x60 && argc==3 && exchange_inventory_ && site==0x8b0f8) {
                const auto filter=Pop(),dest=Pop(),source=Pop();
                const auto matches=Dereference(dest);
                Require(source.kind==Kind::global_ref && source.data==0x4fad &&
                        filter.kind==Kind::global_ref && filter.data==0x9a2a &&
                        matches.kind==Kind::array && matches.cells->size()==1,"Invalid gift exchange filter");
                result_={Kind::integer,0};
                for(int i=0;i<RoomInventory::slot_count;++i) if(exchange_inventory_->slot_item(i)) {
                    matches.cells->front()={Kind::integer,i};
                    result_.data=1;
                    break;
                }
            } else if(id==0x60 && argc==3 && (friends_introduction_ || exchange_inventory_) && site==0xb014) {
                const auto predicate=Pop(),dest=Pop(),source=Pop();
                Require(source.kind==Kind::global_ref && source.data==0x3151 &&
                        dest.kind==Kind::constant_ref && dest.data==0x24df &&
                        predicate.kind==Kind::constant_ref && predicate.data==0x2b01,
                        "Invalid friend notebook count operands");
                result_={Kind::integer,int(CountIntroductionFriends(vocabulary_))};
            } else if(id==0x35 && argc==5) {
                Require(script_locals_.size()>=7 && stack_.size()>=5,
                        "KSC local argument cells are unavailable");
                const auto start=stack_.size()-5;
                for(unsigned i=0;i<5;++i) script_locals_.at(2+i)=std::move(stack_.at(start+i));
                stack_.resize(start);
                result_={};
            } else if(id==0x61 && argc==2) {
                result_={Kind::integer,ApplyArrayRecords(false)};
            } else if(id==0x62 && argc==3) {
                result_={Kind::integer,ApplyArrayRecords(true)};
            } else if(id==0x63 && argc==2) {
                const auto filter=Pop(),row=Pop();
                Require(filter.kind==Kind::constant_ref && row.kind==Kind::word_row,"Invalid word predicate");
                if(robo_greeting_ || alternate_greeting_)
                    result_={Kind::truth,vocabulary_.MatchesPredicate(row.data,filter.data)};
                else {
                    const auto& p=predicates_.at(filter.data);
                    bool matched=true;
                    for(unsigned i=0;matched && i<p.size();i+=3) {
                        const auto value=DictionaryField(row.data,p[i]&0xffff);
                        const unsigned condition=p[i+1]&0xffff;
                        const int rhs=p[i+2]&0x0fffffff;
                        if(condition==0) matched=value.kind==Kind::integer && value.data==rhs;
                        else if(condition==6) matched=(value.data&rhs)==rhs;
                        else if(condition==7) matched=(value.data&rhs)==0;
                        else if(condition==8) matched=(value.data&rhs)!=0;
                        else throw std::runtime_error("Unknown teaching predicate operator");
                    }
                    result_={Kind::truth,matched};
                }
            } else if(id==0x21 && argc>0 && (ordinary_ || robo_greeting_ || alternate_greeting_)) {
                answer_options_.resize(argc);
                for(unsigned i=argc;i>0;--i) {
                    auto value=Pop();
                    Require(value.kind==Kind::string && !value.text.empty(),"Invalid answer label");
                    answer_options_[i-1]=std::move(value.text);
                }
                answer_frame_=answer_cursor_=answer_ticks_=0; answer_close_=false; answer_held_={};
                phase_=Phase::answer_pending; return;
            } else if(id==0x22 && argc==2) {
                Require(Pop().kind==Kind::nil,"Missing default KSC actor");
                const auto command=static_cast<unsigned>(Pop().data);
                if(scheduled_room_event_) scheduled_actor_commands_[scheduled_secondary_]=command;
                else actor_command_=command;
            } else if(id==0x23 && argc==0) {
                actor_wait_secondary_=scheduled_secondary_;
                actor_wait_=true; return;
            } else if(id==0xf0 && argc==2 &&
                      site==(alternate_greeting_?alternate_greeting_->gift_open:robo_greeting_?0x67449u:0x89532u)) {
                auto slot=Pop(),service=Pop();
                Require(slot.data==gift_slot_ && service.data==41,"Invalid inventory request");
            } else if(id==0xf0 && argc==2) {
                const auto v=Pop(),service=Pop();
                if(service.data==21) { mood_=v.data; result_={Kind::truth,0}; }
                else if(scheduled_room_event_ && service.data==37 && v.kind==Kind::integer) {
                    scheduled_secondary_=v.data!=0;
                    scheduled_selections_.push_back(unsigned(scheduled_secondary_));
                }
                else if(scheduled_room_event_ && service.data==44 && v.kind==Kind::integer &&
                        (v.data==0 || v.data==1 || (ScheduledFarewell() && v.data==3)))
                    scheduled_camera_request_=unsigned(v.data);
                else if(scheduled_room_event_ && service.data==43 && v.kind==Kind::integer && (v.data==0 || v.data==1))
                    scheduled_navigation_=v.data!=0;
                else if(service.data==72 && ((scheduled_room_event_==3 && site==(named?named->callbacks[1]+0x7f:robo_greeting_?0x67580u:0x89669u)) ||
                        (scheduled_room_event_==5 && site==(named?named->callbacks[3]+0x67:robo_greeting_?0x67609u:0x896f2u))) &&
                        v.kind==Kind::integer && v.data==0)
                    scheduled_menu_restriction_=true;
                else if(ScheduledFarewell() && service.data==75 && site==farewell_sites().game75) {
                    Require(v.kind==Kind::integer && (scheduled_room_event_==5 ? v.data==2 : v.data==0 || v.data==1) &&
                            farewell_stage_==FarewellStage::inactive,"Unsupported farewell GAME75 mode");
                    Require(!callers_.empty() && callers_.back().resume==FarewellReturnPc(),
                            "GAME75 lacks its last-day caller");
                    farewell_mode_=unsigned(v.data);
                    farewell_stage_=FarewellStage::requested;
                }
                else if(scheduled_room_event_ && service.data==73 &&
                        site==(alternate_greeting_?0x1f17u:robo_greeting_?0x1f02u:0x1f01u)) {
                    const auto request=Dereference(v);
                    Require(request.kind==Kind::array && request.cells->size()==3 && !scheduled_sound_,
                            "Invalid scheduled GAME73 request");
                    const auto& fields=*request.cells;
                    Require(std::all_of(fields.begin(),fields.end(),[](const Value& field) {
                        return field.kind==Kind::integer && field.data>=0;
                    }) && fields[2].data<=128,"Invalid scheduled GAME73 parameters");
                    scheduled_sound_=RoomSound{5,unsigned(fields[0].data),unsigned(fields[1].data),
                                               unsigned(fields[2].data),true};
                }
                else if(exchange_inventory_ && site==0x8d0b5 && service.data==49 &&
                        v.kind==Kind::integer && v.data>=0 && v.data<=2) {
                    if (peer_meetings_enabled_) {
                        const auto venue=locals_.at(2);
                        Require(venue.kind==Kind::integer && venue.data>=0 && venue.data<4,
                                "Invalid native meeting place");
                        meeting_request_=MeetingPlace{MeetingMode(v.data),std::uint8_t(venue.data)};
                        phase_=Phase::meeting;
                    } else {
                        frontier_=site;
                        phase_=Phase::frontier;
                    }
                    return;
                }
                else if (exchange_inventory_ && peer_meetings_enabled_ &&
                         ((service.data==68 && site==0x8d1d1 && v.kind==Kind::nil) ||
                          (service.data==62 && site==0x8d1dd && v.kind==Kind::integer && v.data==0))) {
                    Require(!meeting_request_,"Meeting socket was not closed before cancellation");
                    friends_cells_[0x9a33]={Kind::truth,0}; // Native socket close has completed.
                }
                else if(friends_introduction_ && service.data==42 &&
                        (site==0x8df4f || site==0x8df7a)) {
                    Require(v.kind==Kind::integer && v.data==(site==0x8df4f?1:0),
                            "Invalid SYOUKAI channel-1 request");
                    friends_banner_=bool(v.data);
                }
                else if((excursion_ || ScheduledFarewell()) && service.data==28 && v.kind==Kind::nil)
                    result_={Kind::truth,outing_game28_};
                else if((excursion_ || ScheduledFarewell()) && service.data==40 && v.kind==Kind::nil)
                    result_={Kind::truth,outing_lamp_};
                else if((excursion_ || scheduled_room_event_) && service.data==45 && v.kind==Kind::nil)
                    result_={Kind::truth,outing_locked_};
                else if((excursion_ || scheduled_room_event_) && service.data==27 && v.kind==Kind::nil)
                    result_={Kind::integer,int(outing_interrupt_)};
                else if((excursion_ || scheduled_room_event_) && service.data==26 && v.kind==Kind::nil)
                    outing_interrupt_requested_=true;
                else if(excursion_ && service.data==44 && v.kind==Kind::integer && !v.data) {
                    outing_wide_=true; outing_camera_=0;
                } else if((excursion_ || ScheduledFarewell()) &&
                          (service.data==6 || service.data==7) && v.kind==Kind::nil)
                    outing_wipe_=service.data==6;
                else if(excursion_ && service.data==12 && site==0x1269f && v.kind==Kind::integer) {
                    Require(outing_snapshot_ && !outing_scene_request_ &&
                            (unsigned(v.data)==outing_snapshot_->kind || (outing_returning_ && !v.data)),
                            "Invalid outing scene request");
                    outing_scene_request_=unsigned(v.data);
                } else if(service.data==36 && (excursion_ || ScheduledFarewell() || robo_contact_greeting_ ||
                           (robo_greeting_ && ordinary_ && site==0x350f3)) && v.kind==Kind::nil)
                    result_={Kind::integer,int(outing_period_)};
                else if(service.data==12 && excursion_ && (site==0x13249 || site==0x133a3) &&
                        v.kind==Kind::integer && v.data==0) {
                    outing_room_request_=true;
                }
                else if(service.data==22) result_={Kind::integer,mood_};
                else if(service.data==23) result_={Kind::integer,actor_field50_};
                else throw std::runtime_error("Unsupported conversation GAME service "+std::to_string(service.data));
            } else if(id==0x11 && argc==1) {
                random_bound_=Pop().data;
                // The two synchronous builtin17 calls inside 2092 draw and swap
                // in this update; a shuffle does not advance the game by 132 frames.
                if(rng && excursion_ && (site==0x20c0 || site==0x20cb)) {
                    Require(random_bound_!=0,"Empty outing shuffle");
                    result_={Kind::integer,int(rng->NextC3234()%random_bound_)};
                } else { phase_=Phase::random; return; }
            } else throw std::runtime_error("Unsupported NEKO builtin");
            break;
        }
        default: throw std::runtime_error("Unsupported opcode in cold NEKO introduction");
        }
    }
    throw std::runtime_error("KSC introduction did not yield");
}

void RoomTalkState::OpenWordKeyboard(int mode,std::string initial,Value destination) {
    editor_pair_=std::move(destination);
    keyboard_initial_reading_.reset();
    if(mode==2) keyboard_initial_reading_=std::move(initial);
    phase_=Phase::word_entry;
}

void RoomTalkState::FinishWordEntry(std::string_view written,std::string_view reading) {
    if(phase_!=Phase::word_entry) return;
    if(editor_pair_.kind==Kind::nil) {
        auto cells=std::make_shared<std::vector<Value>>(2);
        // On cancellation the host supplies null pointers for both strings.
        if(!written.empty()) {
            cells->at(0)={Kind::string,0,std::string(written)};
            cells->at(1)={Kind::string,0,std::string(reading)};
        }
        result_={Kind::array,0,{},0,std::move(cells)};
    } else {
        editor_pair_.cells->at(0)={Kind::string,0,std::string(written)};
        editor_pair_.cells->at(1)={Kind::string,0,std::string(reading)};
    }
    editor_pair_={}; keyboard_initial_reading_.reset(); phase_=Phase::running;
}

std::size_t RoomTalkState::BreakGroupEnd() const {
    const auto in=[&](unsigned cls,unsigned glyph) {
        const auto& set=break_classes_[cls];
        return std::find(set.begin(),set.end(),glyph)!=set.end();
    };
    const auto first=GlyphAt(message_,text_cursor_);
    std::size_t pos=text_cursor_+first.consumed;
    if(in(1,first.index)) {
        if(pos<message_.size() && static_cast<unsigned char>(message_[pos])>=32)
            pos+=GlyphAt(message_,pos).consumed;
    } else while(pos<message_.size()) {
        const auto next=GlyphAt(message_,pos);
        if(!in(0,next.index)) break;
        pos+=next.consumed;
    }
    return pos;
}

void RoomTalkState::AppendUnit(std::string_view unit,const recovered::PackageView& font) {
    bool wrap=false;
    if(text_cursor_>=group_end_) {
        // 16350..16494 measures the entire nonbreaking group before the
        // glyph-reveal cursor crosses its first unit.
        group_end_=BreakGroupEnd();
        const auto group=message_.substr(text_cursor_,group_end_-text_cursor_);
        wrap=!lines_.back().empty() && TextWidth(lines_.back()+group,font)>columns_*16;
    }
    if(new_line_ || wrap) {
        lines_.emplace_back(); new_line_=false;
        if(lines_.size()>rows_) scroll_=18.f;
    }
    lines_.back()+=unit;
}

void RoomTalkState::AdvanceBalloon(const recovered::PackageView& font) {
    // UI updates consume the normal two game clock units per displayed frame.
    constexpr int tick=2;
    const auto timing=MessageSpeed(message_speed_);
    if(closing_) {
        appearance_=std::max(0.f,appearance_-.1f*tick);
        if(appearance_==0) {
            lines_.clear(); new_line_=false; closing_=false; scroll_=0;
            if(page_close_) {
                page_close_=false;
                message_wait_=text_cursor_<message_.size();
                if(message_wait_) lines_.emplace_back();
            }
        }
        return;
    }
    if(!message_wait_) {
        if(appearance_>0 && (idle_-=tick)<=0) closing_=true;
        return;
    }
    if(appearance_<1) { appearance_=std::min(1.f,appearance_+.1f*tick); return; }
    if(scroll_>0) {
        scroll_=std::max(0.f,scroll_-(18.f/timing.scroll)*tick);
        if(!scroll_ && lines_.size()>rows_) lines_.erase(lines_.begin());
        return;
    }
    if((glyph_delay_-=tick)>0) return;
    if(page_close_) { closing_=true; return; }
    while(text_cursor_<message_.size()) {
        unsigned c=static_cast<unsigned char>(message_[text_cursor_]);
        if(c==4) {
            // D87D4[0] -> 160A4: request closure after the source hold for
            // the number of displayed lines. D8844[4] gives it no glyph.
            ++text_cursor_; page_close_=true;
            glyph_delay_=timing.page*int(lines_.size());
            return;
        }
        if (c==9) {
            // D87D4[5] takes the default glyph delay. 93FF0 skips
            // non-newline controls below 0x20 in the width calculation.
            ++text_cursor_; glyph_delay_=timing.glyph; return;
        }
        if(c==0x16 || c==10) {
            ++text_cursor_;
            if(c==10) new_line_=true;
            glyph_delay_=timing.line; return;
        }
        const unsigned count=c>=0x80?2:1;
        if (text_cursor_+count>message_.size() || c<32)
            throw std::runtime_error("Invalid room message byte "+std::to_string(c)+
                                     " at offset "+std::to_string(text_cursor_));
        AppendUnit(std::string_view(message_).substr(text_cursor_,count),font);
        text_cursor_+=count; glyph_delay_=timing.glyph; return;
    }
    message_wait_=false;
    if(!message_.empty() && message_.back()!=10) new_line_=true;
}

void RoomTalkState::StepAnswer(std::uint32_t buttons,std::uint32_t edges) {
    // 1E638 opens SELECT mode zero with 5400 active updates and Cross disabled.
    if(answer_frame_<10) {
        if(++answer_frame_==10 && answer_close_) phase_=Phase::running;
        return;
    }
    const unsigned count=static_cast<unsigned>(answer_options_.size());
    bool moved=false;
    for(unsigned i=0;i<2;++i) {
        const unsigned mask=i?0x10:0x40;
        auto& held=answer_held_[i];
        if(!(buttons&mask)) { held=0; continue; }
        const bool first=bool(edges&mask);
        const bool repeat=held>=10 && !(held%2);
        ++held;
        if(moved || (!first && !repeat)) continue;
        moved=true;
        if((i && answer_cursor_>0) || (!i && answer_cursor_+1<count) || first) {
            answer_cursor_=(answer_cursor_+(i?count-1:1))%count;
            effects_.push_back(6);
        }
    }
    const bool timeout=++answer_ticks_==5400;
    if(timeout || (edges&circle)) {
        result_={Kind::integer,timeout?0:int(answer_cursor_+1)};
        if(!timeout) { last_answer_=int(answer_cursor_+1); effects_.push_back(4); }
        effects_.push_back(11);
        answer_close_=true; answer_frame_=0;
    }
}

void RoomTalkState::Step(std::uint32_t buttons,bool available,bool actor_ready,const recovered::PackageView* font,
                         bool prompt_expired,std::optional<bool> invitation_event,recovered::RngNativeOwner* rng,
                         bool secondary_ready) {
    const auto edges=buttons&~previous_;
    previous_=buttons; available_=available; consumed_=false; actor_command_.reset();
    wide_camera_requested_=false;
    ordinary_return_=ordinary_rejected_=false;
    attention_return_=false; outing_sites_.clear();
    ++script_ticks_; // 74DD4 advances the KSC scheduler clock once, independent of DIALOG's tick.
    try {
        child_budget_=script_instruction_limit;
        if(phase_!=Phase::unsupported && phase_!=Phase::failed) StepRegisteredScripts(rng);
        StepRoom(buttons,edges,actor_ready,font,prompt_expired,invitation_event,rng,secondary_ready);
        StepChildren(rng);
    } catch(const std::exception& e) { error_=std::string(e.what())+" (next KSC PC "+std::to_string(pc_)+")"; phase_=Phase::failed; }
}

void RoomTalkState::StepRoom(std::uint32_t buttons,std::uint32_t edges,bool actor_ready,
                             const recovered::PackageView* font,bool prompt_expired,
                             std::optional<bool> invitation_event,recovered::RngNativeOwner* rng,
                             bool secondary_ready) {
    const bool edge=(edges&circle)!=0;
    if(phase_==Phase::unsupported || phase_==Phase::failed) return;
    if(visible() || message_wait_) {
        Require(font!=nullptr,"Missing conversation FONT.XFT");
        AdvanceBalloon(*font);
    }
    if(phase_==Phase::prompt && ((available_ && invitation_event.value_or(edge)) || prompt_expired)) {
        consumed_=available_ && invitation_event.value_or(edge);
        if((robo_greeting_ || alternate_greeting_) && !consumed_) {
            // 677FA cancels the invitation and requests ASC 30. An
            // expired prompt does not execute the gift callback.
            actor_command_=30;
            ReturnFromGreeting();
            return;
        }
        phase_=Phase::running;
        pc_=alternate_greeting_?alternate_greeting_->entry:robo_greeting_?0x6746du:0x89556u;
    }
    if(phase_==Phase::answer_pending) {
        // MSG (1E0C8) and ASK (1E618) use the same actor's balloon slot.
        // ASK waits for its +64 state to reach zero before opening SELECT.
        if(message_wait_ || visible() || closing_) return;
        effects_.push_back(10);
        phase_=Phase::answer;
        return;
    }
    if(phase_==Phase::answer) { StepAnswer(buttons,edges); return; }
    if((phase_!=Phase::running && phase_!=Phase::rendezvous) || message_wait_) return;
    if(named_outing_stage_!=NamedOutingStage::inactive) {
        Require(rng!=nullptr,"Actor outing requires game RNG");
        AdvanceNamedOuting(*rng,actor_ready);
        return;
    }
    if(scheduled_room_event_ && invitation_event.value_or(false) && scheduled_deadline_ &&
       scheduled_deadline_->kind==Kind::calendar_time && !blocks_.empty()) {
        const auto block=blocks_.back();
        Require(block.close==(alternate_greeting_?0x1a8du:robo_greeting_?0x1a78u:0x1a77u) && block.depth==callers_.size() &&
                block.base<=stack_.size(),
                "Scheduled invitation no longer owns its protected wait");
        // 8DA99 plays the confirm cue, then 8DA9E unwinds to the 1A44 handler.
        scheduled_sound_=RoomSound{5,0,4,128,true};
        blocks_.pop_back(); stack_.resize(block.base); pc_=block.close;
        actor_wait_=false; delay_=0; script_deadline_=0;
    }
    if(actor_wait_) {
        if(!(actor_wait_secondary_?secondary_ready:actor_ready)) return;
        actor_wait_=false;
    }
    AdvanceScript(rng);
}

void RoomTalkState::AdvanceScript(recovered::RngNativeOwner* rng) {
    // 74C2C checks a positive signed deadline against the signed
    // scheduler clock. Expired and nonpositive deadlines clear.
    if(SignedClock(script_deadline_)>0 &&
       SignedClock(script_ticks_)<SignedClock(script_deadline_)) return;
    script_deadline_=0;
    if(phase_==Phase::rendezvous) {
        // 74CBC tests the signed 16-bit balance before resuming and clears it.
        if(rendezvous_credit_&0x8000u) return;
        rendezvous_credit_=0;
        phase_=Phase::running;
    }
    if(delay_>0 && --delay_>0) return;
    Execute(rng);
}

void RoomTalkState::SupplyGift(int slot) {
    if(phase_!=Phase::gift) return;
    gift_slot_=slot; result_={Kind::integer,slot}; phase_=Phase::running;
}
std::optional<int> RoomTalkState::inventory_requested() const {
    if(phase_==Phase::inventory) return gift_slot_;
    return {};
}
void RoomTalkState::FinishInventory(bool emptied_slot) {
    if(phase_!=Phase::inventory) return;
    slot_empty_=emptied_slot;
    // 1C67 pushes one script tick after registering the wakeup handler.
    delay_=1; phase_=Phase::running;
}
std::optional<unsigned> RoomTalkState::random_requested() const {
    if(phase_==Phase::random) return random_bound_;
    return {};
}
RoomRumors::Pair RoomTalkState::RumorPair(const Value& subject,const Value& statement) {
    const auto field=[](const Value& value) -> std::optional<std::string> {
        if (value.kind==Kind::nil) return {};
        Require(value.kind==Kind::string,"Invalid meeting rumor field");
        return value.text;
    };
    return {field(subject),field(statement)};
}
void RoomTalkState::SupplyRumor(const Value& subject,const Value& statement,unsigned pick) {
    auto pair=rumors_.Take(pick);
    Assign(subject,pair.first ? Value{Kind::string,0,std::move(*pair.first)} : Value{});
    Assign(statement,pair.second ? Value{Kind::string,0,std::move(*pair.second)} : Value{});
}
void RoomTalkState::SupplyRandom(unsigned value) {
    if(phase_!=Phase::random || value>=random_bound_) return;
    if(random_use_==RandomUse::procedure) {
        const auto target=procedure_lists_.at(pending_procedure_list_).at(value);
        random_use_=RandomUse::value; phase_=Phase::running;
        // Both selectors open the nested frame after the draw. Opcode 24
        // carries the arguments in caller order.
        std::vector<Value> arguments(std::exchange(pending_procedure_arguments_,0));
        for(std::size_t i=arguments.size();i>0;--i) arguments[i-1]=Pop();
        Call(target,std::move(arguments),false);
        return;
    }
    if(random_use_==RandomUse::rumor_pick || random_use_==RandomUse::rumor_replace) {
        auto second=Pop();
        auto first=Pop();
        if (random_use_==RandomUse::rumor_pick) SupplyRumor(first,second,value);
        else rumors_.Replace(value,RumorPair(first,second));
    } else if(random_use_==RandomUse::fallback) {
        stack_.push_back({Kind::code,int(fallback_list_.Consume(value))});
    } else if(random_use_==RandomUse::phrase) {
        // The BOOT selector returns a one-based slot. This vector omits the list header.
        const auto offset=phrase_lists_.at(stack_.back().data).at(value);
        stack_.back()={Kind::string,0,pool_.at(offset)};
    } else if(random_use_==RandomUse::array_reference) {
        auto array=Pop();
        stack_.push_back({Kind::element,int(value),{},0,std::move(array.cells)});
    } else if(random_use_==RandomUse::trait) {
        result_={Kind::integer,int(unasked_traits_.at(value))};
    } else if(random_use_==RandomUse::repeated_trait) {
        result_={Kind::integer,int(RoomWordTraits::Repeated(trait_row_,vocabulary_.FieldInt(trait_row_,12),value))};
    } else {
        const int picked=random_use_==RandomUse::word?vocabulary_.FinishLookup(value):static_cast<int>(value);
        result_={Kind::integer,picked};
    }
    random_use_=RandomUse::value; phase_=Phase::running;
}
}
