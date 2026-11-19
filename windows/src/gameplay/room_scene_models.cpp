#include "room_scene_models.h"
#include "room_calendar_items.h"
#include "room_inventory.h"
#include "recovered/rng_native.h"
#include <algorithm>
#include "room_scene_rows.h"
#include <fstream>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
int Value(std::uint32_t word) {
    if (!word) return 0; // KSC nil converted by 1D81C when a transform operand is consumed.
    if ((word >> 28) != 2) throw std::runtime_error("Unknown room model operand");
    return static_cast<std::int32_t>(word << 4) >> 4;
}
std::string ModelName(int id) {
    if (id == 170) return "wallpaper0";
    if (id == 171) return "carpet0";
    if (id >= 172 && id <= 196) {
        const auto season = id - 171;
        return (season < 10 ? "season0" : "season") + std::to_string(season);
    }
    if (id >= 197 && id <= 202) return "shiritori0" + std::to_string(id - 196);
    if (id >= 203 && id <= 207) return "garbage0" + std::to_string(id - 202);
    throw std::runtime_error("Unsupported room scene model " + std::to_string(id));
}
}

int RoomSceneModels::Free() const {
    // 49944 walks flags in ascending order across the 100 actual item slots.
    for (int i = 0; i < static_cast<int>(slots_.size()); ++i)
        if (slots_[i].item < 0) return i;
    return -1;
}

int RoomSceneModels::LoadModel(const std::array<uint32_t,10>& row) {
    const int slot = Free();
    if (slot < 0) return -1;
    Model model;
    model.item = Value(row[0]);
    model.parent = Value(row[8]);
    model.x = Value(row[9]);
    if (model.parent < 0 || model.parent > 9)
        throw std::runtime_error("Unknown room model attachment");
    std::string error;
    if (!model.mesh.Load(directory_ / (ModelName(model.item) + ".bin"), error))
        throw std::runtime_error(error);
    slots_[slot] = std::move(model);
    return slot;
}

namespace {
uint32_t Integer(int n) { return 0x20000000u | (uint32_t(n)&0x0fffffffu); }
}

void RoomSceneModels::RegisterGift(RoomInventory& inventory,int slot) {
    const auto* item=inventory.slot_item(slot);
    if (!item) throw std::logic_error("Cannot register an empty inventory slot");
    const auto previous=inventory.PlacementWord(slot,6);
    if (previous) {
        const int index=Value(previous);
        if (index<0 || index>=int(slots_.size()) || slots_[index].inventory_slot!=slot)
            throw std::runtime_error("Gift lost its current room model");
        slots_[index].gift.Move(Value(inventory.PlacementWord(slot,9)));
        return;
    }
    const int index=Free();
    if (index>=0) {
        Model model;
        model.item=item->id;
        model.inventory_slot=slot;
        std::string error;
        if (!model.gift.LoadSaved(directory_,item->id,Value(inventory.PlacementWord(slot,8)),
                                  Value(inventory.PlacementWord(slot,9)),error))
            throw std::runtime_error(error);
        slots_[index]=std::move(model);
    }
    inventory.SetPlacementWord(slot,6,Integer(index));
}

void RoomSceneModels::HideCalendar(RoomCalendarItems& calendar,int spot,bool mounted) {
    if (spot<6 || spot>8) return;
    for(auto& row:calendar.rows()) if (row[7]==Integer(spot)) {
        const int old=Value(row[6]);
        if (mounted && row[6] && old>=0) slots_.at(old)={};
        row[7]=row[6]=0;
        break;
    }
}

bool RoomSceneModels::Bind(const std::filesystem::path& directory, RoomCalendarItems& calendar,
                            RoomSceneRows& scene, RoomInventory& inventory, std::string& error) try {
    Clear();
    directory_=directory;
    for (auto& row:calendar.rows()) if (row[0]) row[7]=row[5];
    for (int slot=0;slot<RoomInventory::slot_count;++slot) {
        if (!inventory.slot_item(slot)) continue;
        const auto spot=inventory.PlacementWord(slot,7);
        if (!spot) continue;
        // 39BD calls 3477 alone. It hides the saved seasonal position here;
        // this pass has not registered any seasonal model in the new manager.
        if (Value(spot)>=6 && Value(spot)<=8)
            for (auto& row:calendar.rows()) {
                if (row[7]==spot) { row[7]=0; break; }
            }
        inventory.SetPlacementWord(slot,6,0);
        RegisterGift(inventory,slot);
    }
    // Each 33D1 call owns its return. Clear old handles before native registration.
    for (auto& row : calendar.rows()) {
        if (row[7]) {
            row[6] = 0;
            row[6] = 0x20000000u | (static_cast<unsigned>(LoadModel(row)) & 0x0fffffffu);
        }
    }
    for (size_t i=0; i<scene.rows().size(); ++i) {
        if (scene.rows()[i][7]) {
            scene.ClearModelHandle(i);
            scene.SetModelHandle(i, LoadModel(scene.rows()[i]));
        }
    }
    auto& exchange = scene.exchange();
    if (exchange.item_row()[7]) { // 3A6A: register G58D4 after the G5897 rows.
        exchange.ClearModelHandle();
        exchange.SetModelHandle(LoadModel(exchange.item_row()));
    }
    bound_ = true;
    return true;
} catch (const std::exception& ex) {
    error = ex.what();
    return false;
}

void RoomSceneModels::ReleaseGift(RoomInventory& inventory,int slot) {
    const auto handle=inventory.PlacementWord(slot,6);
    if (bound_ && !handle) return; // 3405 at home returns for an unbound row.
    if (bound_ && Value(handle)>=0) {
        auto& model=slots_.at(Value(handle));
        if (model.inventory_slot!=slot) throw std::runtime_error("Wrong room model for put-away");
        model={};
    }
    inventory.SetPlacementWord(slot,6,0);
    inventory.SetPlacementWord(slot,7,0);
    inventory.SetPlacementWord(slot,9,Integer(0));
}

bool RoomSceneModels::StoreGift(RoomCalendarItems& calendar,RoomInventory& inventory,int slot,
                                std::string& error) try {
    if (!inventory.PlacementWord(slot,6)) return true; // 38DA
    const int spot=Value(inventory.PlacementWord(slot,7));
    if (spot>=6 && spot<=8) {
        for (auto& row:calendar.rows()) if (row[5]==Integer(spot)) {
            row[7]=row[5];
            if (bound_ && !row[6]) row[6]=Integer(LoadModel(row));
            break;
        }
    }
    ReleaseGift(inventory,slot);
    return true;
} catch(const std::exception& ex) { error=ex.what(); return false; }

// 3569 chooses the least occupied permitted spot, makes room, then distributes
// the surviving items and the new item together. It updates existing models via 33D1.
bool RoomSceneModels::PlaceGift(const std::filesystem::path& media,RoomCalendarItems& calendar,
                                RoomInventory& inventory,int slot,recovered::RngNativeOwner& rng,
                                std::string& error) try {
    if (inventory.PlacementWord(slot,6)) return true;
    const auto* target=inventory.slot_item(slot);
    if (!target || !rng.Available()) throw std::runtime_error("Missing live item or placement RNG");
    const auto layout=ReadGiftPlacementLayout(media,target->id);
    std::array<std::vector<int>,20> by_spot;
    std::array<int,RoomInventory::slot_count> widths{};
    widths[slot]=int(layout.width);
    for (int i=0;i<RoomInventory::slot_count;++i) {
        const auto* item=inventory.slot_item(i);
        if (!item || !inventory.PlacementWord(i,7)) continue;
        const unsigned spot=Value(inventory.PlacementWord(i,7));
        auto& occupied=by_spot.at(spot);
        occupied.push_back(i);
        widths[i]=int(ReadGiftPlacementLayout(media,item->id).width);
    }

    int best_width=0xffffff;
    GiftPlacementLayout::Location selected{};
    for (auto loc:layout.locations) {
        int used=0;
        for (const int i:by_spot.at(loc.spot)) used+=widths[i];
        const bool coin=(rng.NextC3234()%2)==1; // evaluated even when the previous cost is lower
        if (used<best_width || (used==best_width && coin)) {
            selected=loc;
            best_width=used;
        }
    }
    const int capacity=2*int(selected.half_span);
    auto kept=by_spot.at(selected.spot);
    if (kept.size()>=16) throw std::runtime_error("Gift placement exceeds the source row scratch array");
    HideCalendar(calendar,int(selected.spot),bound_);
    if (best_width+widths[slot]>capacity) {
        int best=0;
        unsigned subset=0;
        for (unsigned mask=1;mask<(1u<<kept.size());++mask) {
            int required=widths[slot];
            for (unsigned k=0;k<kept.size();++k)
                if (mask&(1u<<k)) required+=widths[kept[k]];
            const bool coin=(rng.NextC3234()%2)==1;
            if (required<capacity && (required>best || (required==best && coin))) {
                best=required;
                subset=mask;
            }
        }
        std::vector<int> compact;
        for (unsigned k=0;k<kept.size();++k) {
            if (subset&(1u<<k)) compact.push_back(kept[k]);
            else ReleaseGift(inventory,kept[k]);
        }
        kept.swap(compact);
    }
    inventory.SetPlacementWord(slot,7,Integer(int(selected.spot)));
    inventory.SetPlacementWord(slot,8,selected.selector ? Integer(int(selected.selector)) : 0);
    kept.push_back(slot);
    // Ten original shuffle passes. 2919 takes an inclusive upper bound.
    for (int pass=0;pass<10;++pass)
        for (size_t i=0;i+2<kept.size();++i) {
            const auto other=i+1+rng.NextC3234()%(kept.size()-1-i);
            std::swap(kept[i],kept[other]);
        }
    int slack=capacity;
    for (int i:kept) slack-=widths[i];
    std::vector<int> weights(kept.size()+1);
    int total=0;
    for(int& weight:weights) {
        weight=1+int(rng.NextC3234()%20);
        total+=weight;
    }
    int x=-int(selected.half_span);
    for(size_t k=0;k<kept.size();++k) {
        const int i=kept[k];
        x+=slack*weights[k]/total;
        inventory.SetPlacementWord(i,9,Integer(x+widths[i]/2));
        if (bound_) RegisterGift(inventory,i);
        x+=widths[i];
    }
    return true;
} catch(const std::exception& ex) { error=ex.what(); return false; }

bool RoomSceneModels::DrawGifts(const RoomMesh& room,const std::vector<std::pair<unsigned,uint32_t>>& clips,
                                const std::array<float,16>& view,std::vector<uint8_t>& vram,size_t offset,
                                const RoomLighting* lighting) {
    for(auto& model:slots_) if(model.inventory_slot>=0 && !model.gift.Draw(room,clips,view,vram,offset,lighting)) return false;
    return true;
}

bool RoomSceneModels::Draw(const RoomMesh& room, const std::vector<std::pair<unsigned,uint32_t>>& clips,
                            const std::array<float,16>& view, std::vector<uint8_t>& vram,
                            size_t offset, const RoomLighting* lighting) const {
    constexpr unsigned nodes[]{54,55,56,141,61,57,58,59,60};
    for (size_t i=0;i<slots_.size();++i) {
        const auto& model = slots_[i];
        if (model.item < 0 || model.inventory_slot>=0) continue;
        std::array<float,16> base{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        if (model.parent && !room.NodeWorld(nodes[model.parent-1],clips,base)) return false;
        for (unsigned n=0; n<4; ++n) base[12+n] += base[n]*model.x;
        if (!model.mesh.Draw({},base,view,vram,offset,lighting)) return false;
    }
    return true;
}
}
