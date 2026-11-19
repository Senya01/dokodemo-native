#include "room_inventory.h"
#include "recovered/calendar_native.h"
#include <algorithm>
#include <fstream>
#include <tuple>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
constexpr std::uint32_t integer = 0x20000000u, truth = 0x10000000u;
constexpr std::uint32_t string_tag = 0x60000000u;
int NumberFromBcd(unsigned n) { return int(n / 16 * 10 + n % 16); }

void RefreshView(RoomInventoryItem& item) {
    item.id = int(item.fields[0] & 0x0fffffffu);
    const auto date = item.fields[4] & 0x0fffffffu;
    item.year = NumberFromBcd(date >> 16);
    item.month = NumberFromBcd((date >> 8) & 255);
    item.day = NumberFromBcd(date & 255);
    item.category = item.fields[7] ? 0 : (item.fields[5] & 0x0fffffffu) ? 2 : 1;
}

bool ReadWord(std::istream& in, unsigned& value) {
    unsigned char b[2];
    if (!in.read(reinterpret_cast<char*>(b), 2)) return false;
    value = b[0] | (unsigned(b[1]) << 8);
    return true;
}
bool ReadString(std::istream& in, std::string& text) {
    unsigned length;
    if (!ReadWord(in,length) || length>1024) return false;
    text.resize(length);
    return bool(in.read(text.data(),length)) && text.find('\0')==text.npos;
}

std::string WithGiver(std::string_view format, std::string_view giver) {
    std::string out;
    constexpr std::string_view variable="$_WORD";
    for (;;) {
        const auto at=format.find(variable);
        out.append(format.substr(0,at));
        if (at==format.npos) return out;
        out.append(giver);
        format.remove_prefix(at+variable.size());
    }
}
}

bool RoomInventory::Load(const std::filesystem::path& game_directory,std::string& error) {
    std::ifstream in(game_directory/"post-profile/gift-templates.bin",std::ios::binary);
    std::array<char,4> magic{};
    decltype(gifts_) data{};
    if (!in.read(magic.data(),magic.size()) || std::string_view(magic.data(),4)!="RIG2") {
        error="Missing or invalid KSC gift templates";
        return false;
    }
    for (auto& actor:data) for (auto& gift:actor) {
        unsigned low, high;
        if (!ReadWord(in,gift.id) || gift.id>=208 || !ReadWord(in,low) || !ReadWord(in,high) ||
            (high >> 12) != 2 || !ReadString(in,gift.name) ||
            !ReadString(in,gift.description)) {
            error="Malformed KSC gift record";
            return false;
        }
        gift.property = low | (high << 16);
    }
    if (in.peek()!=std::istream::traits_type::eof()) {
        error="Unexpected trailing KSC gift data";
        return false;
    }
    gifts_=std::move(data);
    loaded_=true;
    return true;
}

int RoomInventory::AwardGift(unsigned actor,unsigned choice,std::string_view nickname,int year,int month,int day) {
    const auto record=recovered::CalendarRecord689B8({year,month,day,0,0});
    const int payload=int(recovered::CalendarScalar7C660(record)&0x0fffffffu);
    const int minute=payload&0x08000000 ? payload-0x10000000 : payload;
    return AwardGiftOnDate(actor,choice,nickname,recovered::CalendarBcdDate68FE0(minute));
}

int RoomInventory::AwardGiftOnDate(unsigned actor,unsigned choice,std::string_view nickname,std::uint32_t date) {
    if (!loaded_ || actor>=gifts_.size() || choice>=gifts_[actor].size()) return -1;
    const auto empty=std::find_if(slots_.begin(),slots_.end(),[](const auto& item){return item.id<0;});
    if (empty==slots_.end()) return -1;
    const auto& gift=gifts_[actor][choice];
    const int row=int(empty-slots_.begin());
    // 3161 reads the catalogue property, 31B5 substitutes both strings and
    // 304E copies the constructed row into the first vacancy and stamps its date.
    RoomInventoryItem item;
    item.slot = row;
    item.fields = {integer | gift.id, string_tag, string_tag, gift.property,
                   integer | (date & 0x0fffffffu), integer, 0, 0, 0, integer};
    item.name = WithGiver(gift.name,nickname);
    item.description = WithGiver(gift.description,nickname);
    RefreshView(item);
    *empty = std::move(item);
    Rebuild();
    return row;
}

namespace {
KscSaveValue Reference(KscSaveValue array) {
    KscSaveValue result{0xb0000000u,{},{}};
    result.children.push_back(std::move(array));
    return result;
}

const std::vector<KscSaveValue>& RowElements(const KscSaveValue& ref, unsigned length) {
    if (ref.word >> 28 != 11 || !ref.text.empty() || ref.children.size()!=1)
        throw std::invalid_argument("Missing inventory array reference");
    const auto& list=ref.children.front();
    if (list.word!=(0x90000000u|length) || !list.text.empty() || list.children.size()!=length)
        throw std::invalid_argument("Unexpected inventory array dimensions");
    return list.children;
}
}

KscSaveValue RoomInventory::SaveRow(unsigned slot) const {
    if (!loaded_) throw std::logic_error("Inventory definitions not loaded");
    const auto& item=slots_.at(slot);
    KscSaveValue array{0x9000000au,{},{}};
    array.children.resize(10);
    for (unsigned i=0;i<10;++i) array.children[i].word=item.fields[i];
    array.children[1].text=item.name;
    array.children[2].text=item.description;
    return Reference(std::move(array));
}

KscSaveValue RoomInventory::SaveTable() const {
    KscSaveValue table{0x90000000u|slot_count,{},{}};
    table.children.reserve(slot_count);
    for (unsigned i=0;i<slot_count;++i) table.children.push_back(SaveRow(i));
    return Reference(std::move(table));
}

void RoomInventory::RestoreSaveTable(const KscSaveValue& table) {
    if (!loaded_) throw std::logic_error("Inventory definitions not loaded");
    const auto& rows=RowElements(table,slot_count);
    RoomInventory next;
    next.gifts_=gifts_;
    next.loaded_=true;
    for (unsigned i=0;i<slot_count;++i) {
        const auto& row=RowElements(rows[i],10);
        const bool vacant=row.front().word==0;
        auto& item=next.slots_[i];
        for (unsigned j=0;j<10;++j) {
            const auto& cell=row[j];
            const auto word=cell.word;
            const bool text_field=j==1 || j==2;
            if (!cell.children.empty() || (vacant && word) ||
                ((!text_field || !word) && !cell.text.empty()) ||
                cell.text.find(char(0))!=std::string::npos)
                throw std::invalid_argument("Invalid inventory cell");
            if (vacant) continue;
            bool valid=false;
            switch (j) {
            case 0: valid=word>=integer && word<integer+208; break;
            case 1: case 2: valid=!word || word==string_tag; break;
            case 3: case 4: case 9: valid=(word>>28)==2; break;
            case 5: valid=word==integer || word==truth || word==truth+1; break;
            case 6: case 7: case 8: valid=!word || (word>>28)==2; break;
            }
            if (!valid) throw std::invalid_argument("Unsupported first-day inventory field");
            item.fields[j]=word;
        }
        if (!vacant) {
            item.slot=int(i);
            item.name=row[1].text;
            item.description=row[2].text;
            RefreshView(item);
        }
    }
    next.Rebuild();
    *this=std::move(next);
}

std::uint32_t RoomInventory::PlacementWord(int slot,unsigned field) const {
    const auto* item=slot_item(slot);
    if (!item || field<6 || field>9) throw std::out_of_range("Invalid inventory placement field");
    return item->fields[field];
}

void RoomInventory::SetPlacementWord(int slot,unsigned field,std::uint32_t word) {
    if (!slot_item(slot) || field<6 || field>9) throw std::out_of_range("Invalid inventory placement field");
    if (word>>28!=2 && (word || field==9)) throw std::invalid_argument("Invalid KSC placement value");
    auto& item=slots_[slot];
    item.fields[field]=word;
    RefreshView(item);
}

void RoomInventory::Rebuild() {
    visible_rows_.clear();
    for (const auto& item:slots_) {
        if (item.id>=0 && (filter_==1 || filter_==5 || int(filter_)-2==item.category))
            visible_rows_.push_back(unsigned(item.slot));
    }
    if (filter_==5) {
        const auto date=[&](unsigned i){const auto& r=slots_[i];return std::tie(r.year,r.month,r.day);};
        std::stable_sort(visible_rows_.begin(),visible_rows_.end(),[&](unsigned a,unsigned b){return date(a)>date(b);});
    }
    selection_=visible_rows_.empty()?0:std::min(selection_,count()-1);
}

const RoomInventoryItem* RoomInventory::slot_item(int slot) const {
    return slot>=0 && unsigned(slot)<slots_.size() && slots_[slot].id>=0 ? &slots_[slot] : nullptr;
}
const RoomInventoryItem* RoomInventory::item_at(unsigned i) const {
    return i<count()? &slots_[visible_rows_[i]]:nullptr;
}

void RoomInventory::Open(int requested_slot,std::uint32_t held_buttons) {
    if (visible()) return;
    filter_=1; selection_=event_=0; event_slot_=-1;
    action_cursor_=action_frame_=0;
    effects_.push_back(12); // 364CC
    previous_=held_buttons; held_.fill(0);
    Rebuild();
    const auto row=std::find(visible_rows_.begin(),visible_rows_.end(),unsigned(requested_slot));
    if (row!=visible_rows_.end()) selection_=unsigned(row-visible_rows_.begin());
    phase_=Phase::opening;
}

std::string_view RoomInventory::ActionLabel(unsigned i) {
    // BOOT DA5F4/DA604/DA5D0/DA5E8/DA5C4, source mode 0 at home.
    static constexpr std::array<std::string_view,5> labels{
        "\x82\xa8\x95\x94\x89\xae\x82\xc9\x82\xa9\x82\xb4\x82\xe9",
        "\x82\xb5\x82\xdc\x82\xc1\x82\xc4\x82\xa8\x82\xad",
        "\x83\x76\x83\x8c\x83\x5b\x83\x93\x83\x67\x8c\xf0\x8a\xb7\x82\xc9\x8e\x67\x82\xa4",
        "\x82\xb7\x82\xc4\x82\xbf\x82\xe1\x82\xa4",
        "\x82\xe2\x82\xc1\x82\xcf\x83\x84\x83\x81"};
    return i<labels.size()?labels[i]:std::string_view{};
}

std::vector<unsigned> RoomInventory::TakeEffects() {
    std::vector<unsigned> result;
    result.swap(effects_);
    return result;
}

void RoomInventory::OpenActions(unsigned cursor) {
    phase_=Phase::action_open;
    action_cursor_=cursor;
    action_frame_=0;
    effects_.push_back(10); // FFFC starts SELECT clip 13.
}

void RoomInventory::Queue(unsigned event) {
    if (const auto* item=selected_item()) {
        event_=event; event_slot_=item->slot; phase_=Phase::pending;
    }
}
void RoomInventory::CompleteAction() {
    if (phase_!=Phase::pending) return;
    auto& item=slots_.at(event_slot_);
    switch (event_) {
    case 13: case 14: case 15:
        // 8DBA9, 8DBC7 and 8DBE5 supply FALSE, FALSE and TRUE to 2FBC.
        item.fields[5] = truth | unsigned(event_ == 15);
        RefreshView(item);
        break;
    case 16: item={}; break;
    default: return;
    }
    event_=0; event_slot_=-1; Rebuild(); phase_=Phase::list;
}

void RoomInventory::Step(std::uint32_t buttons,bool fade_complete) {
    const auto edges=buttons & ~previous_;
    previous_=buttons;
    constexpr unsigned dirs[]={0x40,0x10,0x20,0x80};
    std::array<bool,4> move{};
    for (unsigned i=0;i<4;++i) {
        if (!(buttons&dirs[i])) { held_[i]=0; continue; }
        auto& t=held_[i];
        move[i]=bool(edges&dirs[i]) || (t>=10 && !(t%2));
        t=t<12?t+1:11;
    }
    const bool ok=(edges&0x2000)!=0, back=(edges&0x4000)!=0;
    if (phase_==Phase::opening || phase_==Phase::closing) {
        if (fade_complete) phase_=phase_==Phase::opening?Phase::list:Phase::closed;
    } else if (phase_==Phase::list) {
        if (move[0]) {
            if (selection_+1<count()) ++selection_;
            else if (count() && (edges&dirs[0])) selection_=0;
            else return;
            effects_.push_back(6);
        } else if (move[1]) {
            if (selection_) --selection_;
            else if (count() && (edges&dirs[1])) selection_=count()-1;
            else return;
            effects_.push_back(6);
        } else if (move[2] || move[3]) {
            const unsigned prev=filter_;
            if (move[2]) { if (filter_<5) ++filter_; else if (edges&dirs[2]) filter_=1; }
            else { if (filter_>1) --filter_; else if (edges&dirs[3]) filter_=5; }
            if (prev!=filter_) { selection_=0; Rebuild(); effects_.push_back(6); }
        } else if (ok) {
            if (count()) { OpenActions(0); effects_.push_back(4); } // 368B8 then 368DC
        } else if (back) {
            effects_.push_back(5); effects_.push_back(12); // 36990 then 365BC
            phase_=Phase::closing;
        }
    } else if (phase_==Phase::action_open || phase_==Phase::action_close) {
        if (++action_frame_<10) return;
        if (phase_==Phase::action_open) phase_=Phase::actions;
        else if (action_cursor_==4) phase_=Phase::list;
        else if (action_cursor_==3) { discard_.Open(buttons); phase_=Phase::discard; }
        else Queue(13+action_cursor_);
    } else if (phase_==Phase::actions) {
        if (move[0]) {
            if (action_cursor_<4 || (edges&dirs[0])) {
                action_cursor_=(action_cursor_+1)%5;
                effects_.push_back(6);
            }
        } else if (move[1] && (action_cursor_ || (edges&dirs[1]))) {
            action_cursor_=(action_cursor_+4)%5;
            effects_.push_back(6);
        }
        // 368B4 gives FFFC a disabled Cross cancellation flag. Last list
        // entry returns to the inventory when the player accepts it.
        if (ok) {
            effects_.push_back(4); effects_.push_back(11);
            action_frame_=0;
            phase_=Phase::action_close;
        }
    } else if (phase_==Phase::discard) {
        discard_.Step(buttons);
        const auto mask=discard_.se_mask();
        for (unsigned i:{4u,6u,10u,11u}) if (mask&(1u<<i)) effects_.push_back(i);
        if (discard_.result()==1) Queue(16);
        else if (discard_.result()) OpenActions(3); // 36C2C,119D4 one-based 4
    }
}
} // namespace dokodemo::gameplay
