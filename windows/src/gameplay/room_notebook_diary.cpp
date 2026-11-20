#include "room_notebook_diary.h"
#include "room_messages.h"
#include "recovered/rng_native.h"
#include "core_hash.h"
#include "nickname_owner.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr Cb2aMedia media{"ENIKKI.CB2A",11031,"448dd1de77376ed9707910b9124239b0aca2545ebf40c44296e3081c34f4eabd",
    "diary-textures.bin",141848,"59aea41db661cfe9f7bf752a3cf8eb74070495cb7a8d6cb95d119b03abfad1c3"};
constexpr std::array<unsigned,3> ink{85,47,13};
}
bool RoomNotebookDiary::Load(const std::filesystem::path& dir,std::string& error,unsigned actor) {
    constexpr std::array records{"first-diary.bin", "robo-first-diary.bin", "usagi-first-diary.bin",
                                 "inu-first-diary.bin", "kaeru-first-diary.bin"};
    constexpr std::array<std::string_view,5> markers{"$_YOU","$_ME","","$_ME","$_YOU"};
    if(actor>=records.size()) { error="Invalid diary actor"; return false; }
    if(!scene_.Load(dir,media,error)) return false;
    std::ifstream in(dir/records[actor],std::ios::binary|std::ios::ate);
    const auto size=in.tellg();
    constexpr unsigned photo_size=192*144*4;
    if(!in || size<=std::streamoff(10+photo_size) || size>std::streamoff(10+photo_size+1024)) {
        error="Invalid first-day diary size"; return false;
    }
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(size));
    if(!in.seekg(0).read(reinterpret_cast<char*>(raw.data()),raw.size()) ||
       (actor==0 && core::sha256_hex(raw.data(),raw.size())!="aedc44e7bbf92b10002379f7a00426c054acedf5affd48196de08d9f437085b3")) {
        error="Invalid first-day diary record or photograph"; return false;
    }
    const unsigned length=raw[0]+256u*raw[1];
    if(raw.size()!=10+length+photo_size) { error="Invalid first-day diary extent"; return false; }
    descriptor_id_=raw[4] | (unsigned(raw[5])<<8);
    descriptor_kind_=raw[6] | (unsigned(raw[7])<<8) | (unsigned(raw[8])<<16) | (unsigned(raw[9])<<24);
    template_.assign(reinterpret_cast<const char*>(raw.data()+10),length);
    marker_=markers[actor];
    if(!marker_.empty() && template_.find(marker_)==template_.npos) {
        error="Missing diary name marker"; return false;
    }
    photo_.assign(raw.begin()+10+length,raw.end());
    return true;
}
bool RoomNotebookDiary::RecordFirstDay(RoomMessages& staging,recovered::RngNativeOwner& rng,std::string_view player,unsigned year,unsigned month,unsigned day) {
    if(template_.empty() || staging.DescriptorUsed(descriptor_id_)) return false;
    // A366 found the sole first-day match. Builtin17 at A391 still draws for index modulo one.
    rng.NextC3234();
    auto text=template_;
    if (!marker_.empty()) {
        for(std::size_t at=0; (at=text.find(marker_,at))!=std::string::npos; at+=player.size())
            text.replace(at,marker_.size(),player);
    }
    // A202/A19F updates the script row before GAME71 copies the dated page.
    if(!staging.RecordDiary(descriptor_id_,text,descriptor_kind_)) return false;
    if(has_entry()) return false; // The dated bank already owns its first-day page.
    RestoreFirstDay(std::move(text), {year,month,day},false);
    return true;
}

void RoomNotebookDiary::RestoreFirstDay(std::string text,const std::array<unsigned,3>& saved_date,bool revealed) {
    entry_=std::move(text);
    char date[32]{};
    std::snprintf(date,sizeof(date),"%2u\x8c\x8e%2u\x93\xfa",saved_date[1],saved_date[2]);
    date_=date;
    entry_date_=saved_date;
    revealed_=revealed;
}
void RoomNotebookDiary::Open(std::uint32_t buttons,unsigned year,unsigned month,unsigned day,unsigned hour) {
    if(visible()) return;
    // 39824, full MMENU: pass earlier occupied rows, then select today's
    // matching/empty slot. Only an occupied selection can become readable.
    const bool other_date=has_entry() && entry_date_!=std::array<unsigned,3>{year,month,day};
    if(has_entry() && (other_date || hour>=19)) revealed_=true;
    pages_=other_date?2:1;
    current_=selection_=pages_-1;
    direction_ticks_={unsigned((buttons&0x20)!=0),unsigned((buttons&0x80)!=0)};
    previous_=buttons; phase_=Phase::opening; effects_.push_back(12);
}
void RoomNotebookDiary::Step(std::uint32_t buttons,bool fade_complete) {
    const unsigned edges=buttons&~previous_;
    previous_=buttons;
    int direction=0;
    for(unsigned i=0;i<direction_ticks_.size();++i) {
        const unsigned mask=i?0x80:0x20;
        unsigned& ticks=direction_ticks_[i];
        if(!(buttons&mask)) { ticks=0; continue; }
        // 390B0/39184 consult 94F90(held,10,2) before checking Back.
        if(!direction && (!ticks || (ticks>=10 && ticks%2==0))) direction=i?-1:1;
        ticks=ticks>=12?11:ticks+1;
    }
    if(phase_==Phase::opening && fade_complete) phase_=Phase::reading;
    else if(phase_==Phase::closing && fade_complete) phase_=Phase::closed;
    else if(ready() && direction) {
        const int next=int(selection_)+direction;
        if(next>=0 && unsigned(next)<pages_) {
            selection_=unsigned(next);
            phase_=Phase::turning;
            effects_.push_back(6); effects_.push_back(12);
        }
    }
    else if(ready() && (edges&0x4000)) {
        effects_.push_back(5); effects_.push_back(12); phase_=Phase::closing;
    }
}
void RoomNotebookDiary::FinishPageTurn(std::uint32_t buttons) {
    if(!turning_page()) return;
    // 38EF4..38F04 copies selection160 to current164 only under the cover.
    current_=selection_;
    previous_=buttons;
    phase_=Phase::opening;
    effects_.push_back(12);
}
std::vector<unsigned> RoomNotebookDiary::TakeEffects() {
    std::vector<unsigned> cues;
    cues.swap(effects_);
    return cues;
}
bool RoomNotebookDiary::Draw(const recovered::PackageView& font,std::vector<std::uint8_t>& ram,std::size_t offset) const {
    // ENIKKI controls 0..3: main, landscape photograph, landscape text, single-page nav.
    const int navigation=pages_==1?1:current_==0?2:4;
    const std::vector<Cb2aClip> pose{{0,1},{1,1},{2,1},{3,navigation}};
    const auto draw_record=[&](unsigned node,int x,int y) {
        if(!page_has_content()) return;
        if(node==78) { // 38790 -> 396A0
            for(int row=0;row<144;++row) {
                const int yy=y+row, left=std::max(0,-x),right=std::min(192,480-x);
                if(yy<0 || yy>=272 || left>=right) continue;
                std::copy_n(photo_.data()+4*(row*192+left),4*(right-left),
                            ram.data()+offset+4*(yy*512+x+left));
            }
        } else if(node==92) {
            DrawUiText(date_,x-int(MeasureUiText(date_,font)),y,font,ram,offset,ink);
        } else if(node==93) {
            std::string_view rest=entry_;
            // The landscape page uses the 170px text column of 399CC..39A40.
            while(!rest.empty()) {
                std::size_t end=0;
                while(end<rest.size() && rest[end]!='\n') {
                    const auto next=end+(static_cast<unsigned char>(rest[end])<128?1u:2u);
                    if(end && MeasureUiText(rest.substr(0,next),font,1.f,1.f)>170.f) break;
                    end=next;
                }
                DrawUiText(rest.substr(0,end),x-5,y,font,ram,offset,ink,1.f,{0,0,480,272},1.f); // 394E8..39518: 1px horizontal spacing
                rest.remove_prefix(end); if(!rest.empty() && rest.front()=='\n') rest.remove_prefix(1);
                y+=18;
            }
        }
    };
    // 399E4 assigns a covered page's writing lines (root 2, referenced by
    // node 65) to a pass behind the opaque paper. The footer still draws.
    std::vector<Cb2aNode> overrides;
    if(!page_has_content()) overrides.push_back({65,0});
    return scene_.Draw(0,pose,ram,offset,{},draw_record,overrides) &&
           (!page_has_content() || scene_.Draw(1,pose,ram,offset,{},draw_record));
}
}
