#include "gameplay/room_word_entry.h"
#include "gameplay/room_word_conversion.h"
#include "gameplay/room_word_renderer.h"
#include "core_hash.h"
#include <algorithm>
#include <fstream>

namespace dokodemo::gameplay {
namespace {
constexpr unsigned up=0x10, right=0x20, down=0x40, left=0x80;
constexpr unsigned triangle=0x1000, circle=0x2000, cross=0x4000, square=0x8000;
constexpr std::array<unsigned,4> directions{down,up,right,left};
constexpr unsigned timeout_ticks=9000; // 1E37C
constexpr unsigned word_limit=8; // 1FEC8 -> 200A4
constexpr unsigned reading_limit=16; // 1FED0 -> 20098
unsigned UnitCount(const std::string& bytes) {
    unsigned count=0;
    for(std::size_t i=0;i<bytes.size();++count)
        i+=static_cast<unsigned char>(bytes[i])<0x80?1:2;
    return count;
}
}

bool RoomWordEntry::Load(const std::filesystem::path& dir,std::string& error) {
    const auto load=[&](const char* name,std::size_t size,const char* hash,Pages& pages) {
        std::vector<std::uint8_t> raw(size);
        std::ifstream file(dir/name,std::ios::binary|std::ios::ate);
        if (!file || file.tellg()!=std::streamoff(raw.size()) ||
            !file.seekg(0).read(reinterpret_cast<char*>(raw.data()),raw.size()) ||
            core::sha256_hex(raw.data(),raw.size())!=hash) return false;
        std::size_t p=4;
        for(auto& page:pages) for(auto& group:page) {
            group={};
            group.rows.emplace_back();
            while(raw[p]) {
                if(raw[p]=='\n') { ++p; group.rows.emplace_back(); continue; }
                const unsigned n=raw[p]<0x80?1:2;
                group.rows.back().emplace_back(reinterpret_cast<const char*>(raw.data()+p),n);
                group.columns=std::max(group.columns,static_cast<unsigned>(group.rows.back().size()));
                p+=n;
            }
            ++p;
        }
        return true;
    };
    if(!load("word-groups.bin",512,"9ab46675a8423b483e293dd279faa4d8eab7c3b2badb20ac732c125c57e1d1db",groups_) ||
       !load("word-groups-alternate.bin",512,"77cda310c9685791c30c72c52c54fe16985a855a84623676144c97a909716e59",other_groups_) ||
       !load("reading-groups.bin",255,"aca969c12bb0eeba0d336fc07a9820d5a40b8eea7231d6de4e55267d7e86d278",reading_groups_)) {
        error="Invalid GuiSoftkey word groups";
        return false;
    }
    return conversion_.Load(dir.parent_path()/"post-profile",error);
}
void RoomWordEntry::Open(std::uint32_t buttons) {
    auto groups=std::move(groups_);
    auto other=std::move(other_groups_);
    auto reading_groups=std::move(reading_groups_);
    auto conversion=std::move(conversion_);
    *this = RoomWordEntry{};
    groups_=std::move(groups);
    other_groups_=std::move(other);
    reading_groups_=std::move(reading_groups);
    conversion_=std::move(conversion);
    previous_=buttons;
    remaining_=timeout_ticks;
    phase_=Phase::opening;
    se_mask_=1u<<10;
}
void RoomWordEntry::OpenReading(std::string_view initial,std::uint32_t buttons,bool script_owned) {
    Open(buttons);
    reading_edit_=true;
    written_=initial;
    // 1E378 starts the script's type-2 editor with 9000 ticks. The
    // notebook calls 1FD80 directly with the inactivity timer disabled.
    if(!script_owned) remaining_=0;
}
const RoomWordEntry::Group& RoomWordEntry::ActiveGroup() const {
    if(reading_edit_) return reading_groups_[page_][row_*3+column_];
    const auto& pages=alternate_[page_]?other_groups_:groups_;
    return pages[page_][row_*3+column_];
}
void RoomWordEntry::SetKeyOrder(std::string_view unit) {
    unsigned code=static_cast<unsigned char>(unit[0]);
    if(unit.size()==2) code=(code<<8)|static_cast<unsigned char>(unit[1]);
    if((code>='A' && code<='Z') || (code>='a' && code<='z')) {
        subpage_=code>='a';
        alternate_[1]=subpage_;
    } else if((code>=0x829f && code<=0x82f1) || (code>=0x8340 && code<=0x8396)) {
        subpage_=code>=0x8340;
        alternate_[0]=subpage_;
    }
}

void RoomWordEntry::Close(bool timeout) {
    timeout_=timeout;
    phase_=Phase::closing;
    frame_=0;
}
void RoomWordEntry::Confirm() {
    se_mask_=1u<<4;
    if (composition_.empty()) {
        if(!written_.empty() && !conversion_.FinishWord(written_,reading_)) {
            boundary_pc_=0x98d98;
            phase_=Phase::character_input;
            return;
        }
        se_mask_|=1u<<8;
        Close(false);
        return;
    }
    // 21238 commits the composed run. Only a later DONE on an empty
    // composition reaches 21294 and closes the keyboard.
    const bool fit=RoomWordConversion::CommitRaw(written_,reading_,composition_,word_limit);
    UpdateCandidates();
    se_mask_|=1u<<(fit?8:9);
}
void RoomWordEntry::Erase() {
    auto& text=composition_.empty()?written_:composition_;
    if(text.empty()) { se_mask_|=1u<<9; return; }
    std::size_t last=0;
    for(std::size_t p=0;p<text.size();) {
        last=p;
        p+=static_cast<unsigned char>(text[p])<0x80?1:2;
    }
    text.resize(last);
    // 21574..21588 keeps the accumulated reading until both runs are empty.
    if(written_.empty() && composition_.empty()) reading_.clear();
    UpdateCandidates();
}
void RoomWordEntry::SelectCell() {
    pressed_clip_=4;
    pressed_frame_=1;
    const auto& group=ActiveGroup();
    const unsigned limit=reading_edit_?reading_limit:word_limit;
    const unsigned cell=row_*3+column_;
    if(!group.columns || UnitCount(written_)>=limit ||
       (reading_edit_ && ((cell==9 && written_.empty()) || (page_==1 && cell==0))) ||
       (page_==1 && row_==3 && column_==1 && written_.empty() && composition_.empty())) {
        se_mask_=1u<<9;
        return;
    }
    kana_x_=kana_y_=0;
    se_mask_=1u<<4;
    if(group.rows.size()==1 && group.columns==1) {
        // 21740 directly inserts and commits a one-cell descriptor.
        if(UnitCount(composition_)<word_limit) composition_+=group.rows[0][0];
        const bool fit=RoomWordConversion::CommitRaw(written_,reading_,composition_,limit);
        UpdateCandidates();
        if(!fit) se_mask_|=1u<<9;
    } else {
        picking_=true;
    }
}
void RoomWordEntry::ChooseKana() {
    const auto& row=ActiveGroup().rows.at(kana_y_);
    if(kana_x_>=row.size() || row[kana_x_]=="\t" || UnitCount(composition_)>=word_limit) {
        se_mask_=1u<<9;
        return;
    }
    // 206C8 rejects a leading small tsu and leaves the picker open.
    if(reading_edit_ && written_.empty() && row[kana_x_]=="\x82\xc1") {
        se_mask_=1u<<9;
        return;
    }
    composition_+=row[kana_x_];
    if(reading_edit_) RoomWordConversion::CommitRaw(written_,reading_,composition_,reading_limit);
    else SetKeyOrder(row[kana_x_]);
    picking_=false;
    se_mask_=1u<<4;
    UpdateCandidates();
}
void RoomWordEntry::UpdateCandidates() {
    if(reading_edit_) return;
    candidate_=0;
    candidates_.clear();
    auto list=conversion_.Lookup(composition_);
    lookup_known_=list.has_value();
    candidate_records_.clear();
    if(list) {
        candidate_records_=std::move(*list);
        for(const auto& item:candidate_records_) candidates_.push_back(item.written);
    }
}
void RoomWordEntry::MoveCandidate(const std::array<bool,4>& move,std::uint32_t pressed,
                                  const recovered::PackageView* font) {
    const auto old=candidate_;
    if(move[3]) {
        if(candidate_) --candidate_;
    } else if(move[2]) {
        if(candidate_+1<candidates_.size()) ++candidate_;
    } else if((move[0] || move[1]) && font) {
        const auto cells=MeasureWordCandidates(candidates_,*font);
        const auto& current=cells[candidate_];
        const auto line=[](const auto& c){return 4*c.page+c.row;};
        const int y=line(current),last=line(cells.back());
        int row=y;
        if(move[1] && (y>0 || pressed&up)) row=y>0?y-1:last;
        else if(move[0] && (y<last || pressed&down)) row=y<last?y+1:0;
        if(y!=row) {
            const int center=current.x+current.width/2;
            for(unsigned i=0;i<cells.size();++i) {
                if(line(cells[i])!=row) continue;
                if(i && line(cells[i-1])==row && center < cells[i].x-4) break;
                candidate_=i;
            }
        }
    }
    if(old!=candidate_) se_mask_=1u<<6;
}

void RoomWordEntry::Step(std::uint32_t buttons,const recovered::PackageView* font) {
    const auto pressed=buttons & ~previous_;
    previous_=buttons;
    se_mask_=0;
    if (pressed_clip_) {
        const unsigned end=pressed_clip_==4?4u:(pressed_clip_>=12?5u:6u);
        if (++pressed_frame_>=end) pressed_clip_=0;
    }
    if (phase_==Phase::opening || phase_==Phase::closing) {
        if (++frame_==9) {
            if (phase_==Phase::opening) phase_=Phase::editing;
            else if(timeout_) phase_=Phase::timed_out;
            else {
                if(cancelled_) written_.clear();
                phase_=written_.empty()?Phase::closed_empty:Phase::closed_word;
            }
        }
        return;
    }
    if (phase_!=Phase::editing) return;
    // 20E84 resets on held face/direction input. 20FB8 additionally emits
    // event 20 through 1E200 when this timer expires.
    if (remaining_ && (buttons & (down|up|right|left|triangle|circle|cross|square))) remaining_=timeout_ticks;
    if (remaining_ && --remaining_==0) {
        written_.clear();
        reading_.clear();
        se_mask_=1u<<11;
        Close(true);
        return;
    }
    std::array<bool,4> move{};
    for (unsigned i=0;i<directions.size();++i) {
        auto& n=held_[i];
        if (!(buttons&directions[i])) { n=0; continue; }
        move[i]=n==0 || (n>=10 && (n-10)%2==0);
        n=n<12?n+1:11;
    }
    if (pressed_clip_) return;
    if(choosing_candidate_) {
        if(pressed&(circle|square)) {
            const auto fit=conversion_.CommitCandidate(candidate_records_.at(candidate_),written_,
                                                        reading_,composition_,word_limit);
            if(!fit.has_value()) {
                boundary_pc_=0x98d7c;
                phase_=Phase::character_input;
                return;
            }
            choosing_candidate_=false;
            UpdateCandidates();
            se_mask_=1u<<4;
            if(!*fit) se_mask_|=1u<<9;
        } else if(pressed&cross) {
            choosing_candidate_=false;
            se_mask_=1u<<5;
        } else MoveCandidate(move,pressed,font);
        return;
    }
    if(picking_) {
        const auto& group=ActiveGroup();
        const unsigned h=static_cast<unsigned>(group.rows.size()),w=group.columns;
        if(pressed&circle) { ChooseKana(); return; }
        if(pressed&cross) { picking_=false; se_mask_=1u<<5; return; }
        const auto x=kana_x_,y=kana_y_;
        if(move[0] && (y+1<h || (pressed&down))) kana_y_=(y+1)%h;
        else if(move[1] && (y || (pressed&up))) kana_y_=(y+h-1)%h;
        else if(move[2] && (x+1<w || (pressed&right))) kana_x_=(x+1)%w;
        else if(move[3] && (x || (pressed&left))) kana_x_=(x+w-1)%w;
        if(x!=kana_x_ || y!=kana_y_) se_mask_=1u<<6;
        return;
    }
    if (pressed&circle) {
        if (row_<4) {
            SelectCell();
            return;
        }
        pressed_clip_=7+column_;
        pressed_frame_=2;
        se_mask_=1u<<4;
        if (column_==1) Erase();
        else if(column_==2) Confirm();
        else {
            if(reading_edit_) cancelled_=true;
            else written_.clear();
            reading_.clear();
            se_mask_|=1u<<11;
            Close(false);
        }
        return;
    }
    if (pressed&cross) {
        pressed_clip_=8;
        pressed_frame_=2;
        se_mask_=1u<<7;
        Erase();
        return;
    }
    if (pressed&triangle) {
        // 21114 needs the result of 98B8C. Raw DONE (21238) is independent
        // of that lookup. Preserve the exact conversion boundary until a
        // dictionary has supplied this composition's candidates.
        if (!composition_.empty() && !lookup_known_) {
            boundary_pc_=0x98b8c;
            phase_=Phase::character_input;
        } else if(!candidates_.empty()) {
            choosing_candidate_=true;
            se_mask_=1u<<4;
        }
        return;
    }
    if (pressed&square) {
        if (row_==4 && column_==2) Confirm();
        else { row_=4; column_=2; se_mask_=1u<<6; }
        return;
    }
    const auto old_row=row_, old_col=column_;
    if (move[0] && (row_<4 || (pressed&down))) row_=(row_+1)%5;
    else if (move[1] && (row_>0 || (pressed&up))) row_=(row_+4)%5;
    else if (move[2]) {
        if (column_<2) ++column_;
        else if (pressed&right) {
            column_=0; page_^=1;
            pressed_clip_=12; pressed_frame_=1;
        }
    } else if (move[3]) {
        if (column_>0) --column_;
        else if (pressed&left) {
            column_=2; page_^=1;
            pressed_clip_=13; pressed_frame_=1;
        }
    }
    if (row_!=old_row || column_!=old_col) se_mask_=1u<<6;
}

} // namespace dokodemo::gameplay
